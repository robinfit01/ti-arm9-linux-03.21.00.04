/*
 * pru/hal/uart/src/suart_api.c
 *
 * Copyright (C) 2010 Texas Instruments Incorporated
 * Author: Jitendra Kumar <jitendra@mistralsolutions.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as  published by the
 * Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/*
 *====================
 * Includes
 *====================
 */

#include "suart_api.h"
#include "suart_pru_regs.h"
#include "pru.h"
#include "omapl_suart_board.h"
#include "suart_utils.h"
#include "suart_err.h"

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/module.h>
#include <linux/io.h>

#include <linux/delay.h>

#include <mach/hardware.h>

#include "csl/cslr_mcasp.h"
#include "csl/cslr_syscfg0_OMAPL138.h"
#include "csl/cslr_gpio.h"
#include "csl/cslr_ehrpwm.h"

static unsigned char gUartStatuTable[8];
static arm_pru_iomap pru_arm_iomap;
static int suart_set_pru_id (unsigned int pru_no);
static void pru_set_rx_tx_mode(Uint32 pru_mode, Uint32 pruNum);
static void pru_set_delay_count (Uint32 pru_freq);
#define PRU_SC_UART_PORT   5

#if (PRU_ACTIVE == BOTH_PRU)
void pru_set_ram_data (arm_pru_iomap * arm_iomap_pru)
{ 
    PRU_SUART_RegsOvly pru_suart_regs = (PRU_SUART_RegsOvly) arm_iomap_pru->pru_io_addr;
    unsigned int * pu32SrCtlAddr = (unsigned int *) (arm_iomap_pru->mcasp_io_addr 
							+ 0x180);	
    pru_suart_tx_cntx_priv * pru_suart_tx_priv = NULL;			
    pru_suart_rx_cntx_priv * pru_suart_rx_priv = NULL;
    unsigned char * pu32_pru_ram_base = (unsigned char *) arm_iomap_pru->pru_io_addr;	
							
    /* ***************************** UART 0  **************************************** */

    /* Channel 0 context information is Tx */
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_TX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART1_CONFIG_TX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART1_CONFIG_DUPLEX & PRU_SUART_HALF_TX_DISABLED) == PRU_SUART_HALF_TX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART1_CONFIG_TX_SER)) =  MCASP_SRCTL_TX_MODE;
#endif
    pru_suart_regs->Reserved1 = 1; 
    pru_suart_tx_priv = (pru_suart_tx_cntx_priv *) (pu32_pru_ram_base + 0x0B0); /* SUART1 TX context base addr */
    pru_suart_tx_priv->asp_xsrctl_base = (unsigned int)(MCASP_SRCTL_BASE_ADDR + (PRU_SUART1_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->asp_xbuf_base = (unsigned int)(MCASP_XBUF_BASE_ADDR + (PRU_SUART1_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->buff_addr = 0x0090; /* SUART1 TX formatted data base addr */
    
    /* Channel 1 is Rx context information */
    pru_suart_regs++;
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_RX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART1_CONFIG_RX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART1_CONFIG_DUPLEX & PRU_SUART_HALF_RX_DISABLED) == PRU_SUART_HALF_RX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART1_CONFIG_RX_SER)) = MCASP_SRCTL_RX_MODE;
#endif
    /* RX is active by default, write the dummy received data at PRU RAM addr 0x1FC to avoid memory corruption */
    pru_suart_regs->CH_TXRXData = RX_DEFAULT_DATA_DUMP_ADDR;
    pru_suart_regs->Reserved1 = 0;
    pru_suart_rx_priv = (pru_suart_rx_cntx_priv *) (pu32_pru_ram_base + 0x0C0); /* SUART1 RX context base addr */
    pru_suart_rx_priv->asp_rbuf_base = (unsigned int)(MCASP_RBUF_BASE_ADDR + (PRU_SUART1_CONFIG_RX_SER << 2));
    pru_suart_rx_priv->asp_rsrctl_base = (unsigned int) (MCASP_SRCTL_BASE_ADDR + (PRU_SUART1_CONFIG_RX_SER << 2));
    
    
    /* ***************************** UART 1  **************************************** */
    /* Channel 2 context information is Tx */
    pru_suart_regs++;    
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_TX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART2_CONFIG_TX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART2_CONFIG_DUPLEX & PRU_SUART_HALF_TX_DISABLED) == PRU_SUART_HALF_TX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART2_CONFIG_TX_SER)) = MCASP_SRCTL_TX_MODE;
#endif
    pru_suart_regs->Reserved1 = 1; 
    pru_suart_tx_priv = (pru_suart_tx_cntx_priv *) (pu32_pru_ram_base + 0x100); /* SUART2 TX context base addr */
    pru_suart_tx_priv->asp_xsrctl_base = (unsigned int)(MCASP_SRCTL_BASE_ADDR + (PRU_SUART2_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->asp_xbuf_base = (unsigned int)(MCASP_XBUF_BASE_ADDR + (PRU_SUART2_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->buff_addr = 0x00E0; /* SUART2 TX formatted data base addr */
    
    /* Channel 3 is Rx context information */
    pru_suart_regs++;
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_RX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART2_CONFIG_RX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART2_CONFIG_DUPLEX & PRU_SUART_HALF_RX_DISABLED) == PRU_SUART_HALF_RX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART2_CONFIG_RX_SER)) = MCASP_SRCTL_RX_MODE;
#endif
    /* RX is active by default, write the dummy received data at PRU RAM addr 0x1FC to avoid memory corruption */
    pru_suart_regs->CH_TXRXData = RX_DEFAULT_DATA_DUMP_ADDR;
    pru_suart_regs->Reserved1 = 0;
    pru_suart_rx_priv = (pru_suart_rx_cntx_priv *) (pu32_pru_ram_base + 0x110); /* SUART2 RX context base addr */
    pru_suart_rx_priv->asp_rbuf_base = (unsigned int)(MCASP_RBUF_BASE_ADDR + (PRU_SUART2_CONFIG_RX_SER << 2));
    pru_suart_rx_priv->asp_rsrctl_base = (unsigned int) (MCASP_SRCTL_BASE_ADDR + (PRU_SUART2_CONFIG_RX_SER << 2));

    /* ***************************** UART 2  **************************************** */
    /* Channel 4 context information is Tx */
    pru_suart_regs++;    
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_TX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART3_CONFIG_TX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART3_CONFIG_DUPLEX & PRU_SUART_HALF_TX_DISABLED) == PRU_SUART_HALF_TX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART3_CONFIG_TX_SER)) = MCASP_SRCTL_TX_MODE;
#endif
    pru_suart_regs->Reserved1 = 1; 
    pru_suart_tx_priv = (pru_suart_tx_cntx_priv *) (pu32_pru_ram_base + 0x150); /* SUART3 TX context base addr */
    pru_suart_tx_priv->asp_xsrctl_base = (unsigned int)( MCASP_SRCTL_BASE_ADDR + (PRU_SUART3_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->asp_xbuf_base = (unsigned int)(MCASP_XBUF_BASE_ADDR + (PRU_SUART3_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->buff_addr = 0x0130; /* SUART3 TX formatted data base addr */
    
    /* Channel 5 is Rx context information */
    pru_suart_regs++;
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_RX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART3_CONFIG_RX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART3_CONFIG_DUPLEX & PRU_SUART_HALF_RX_DISABLED) == PRU_SUART_HALF_RX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART3_CONFIG_RX_SER)) = MCASP_SRCTL_RX_MODE;
#endif
    /* RX is active by default, write the dummy received data at PRU RAM addr 0x1FC to avoid memory corruption */
    pru_suart_regs->CH_TXRXData = RX_DEFAULT_DATA_DUMP_ADDR;
    pru_suart_regs->Reserved1 = 0;
    pru_suart_rx_priv = (pru_suart_rx_cntx_priv *) (pu32_pru_ram_base + 0x160); /* SUART3 RX context base addr */
    pru_suart_rx_priv->asp_rbuf_base = (unsigned int)(MCASP_RBUF_BASE_ADDR + (PRU_SUART3_CONFIG_RX_SER << 2));
    pru_suart_rx_priv->asp_rsrctl_base = (unsigned int) (MCASP_SRCTL_BASE_ADDR + (PRU_SUART3_CONFIG_RX_SER << 2));

    /* ***************************** UART 3  **************************************** */
    /* Channel 6 context information is Tx */
    pru_suart_regs++;    
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_TX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART4_CONFIG_TX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART4_CONFIG_DUPLEX & PRU_SUART_HALF_TX_DISABLED) == PRU_SUART_HALF_TX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART4_CONFIG_TX_SER)) = MCASP_SRCTL_TX_MODE;
#endif
    pru_suart_regs->Reserved1 = 1; 
    pru_suart_tx_priv = (pru_suart_tx_cntx_priv *) (pu32_pru_ram_base + 0x1A0); /* SUART4 TX context base addr */
    pru_suart_tx_priv->asp_xsrctl_base = (unsigned int)(MCASP_SRCTL_BASE_ADDR + (PRU_SUART4_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->asp_xbuf_base = (unsigned int)(MCASP_XBUF_BASE_ADDR + (PRU_SUART4_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->buff_addr = 0x0180; /* SUART4 TX formatted data base addr */
    
    /* Channel 7 is Rx context information */
    pru_suart_regs++;
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_RX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART4_CONFIG_RX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART4_CONFIG_DUPLEX & PRU_SUART_HALF_RX_DISABLED) == PRU_SUART_HALF_RX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART4_CONFIG_RX_SER)) = MCASP_SRCTL_RX_MODE;
#endif
    /* RX is active by default, write the dummy received data at PRU RAM addr 0x1FC to avoid memory corruption */
    pru_suart_regs->CH_TXRXData = RX_DEFAULT_DATA_DUMP_ADDR;
    pru_suart_regs->Reserved1 = 0;
    pru_suart_rx_priv = (pru_suart_rx_cntx_priv *) (pu32_pru_ram_base + 0x1B0); /* SUART4 RX context base addr */
    pru_suart_rx_priv->asp_rbuf_base = (unsigned int)(MCASP_RBUF_BASE_ADDR + (PRU_SUART4_CONFIG_RX_SER << 2));
    pru_suart_rx_priv->asp_rsrctl_base = (unsigned int) (MCASP_SRCTL_BASE_ADDR + (PRU_SUART4_CONFIG_RX_SER << 2));

    /* ****************************** PRU1 RAM BASE ADDR ******************************** */
    pru_suart_regs = (PRU_SUART_RegsOvly) (arm_iomap_pru->pru_io_addr + 0x2000);
    pu32_pru_ram_base = (unsigned char *) (arm_iomap_pru->pru_io_addr + 0x2000);

    /* ***************************** UART 4 **************************************** */

    /* Channel 0 context information is Tx */
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_TX;
    pru_suart_regs->CH_Ctrl_Config1.iso7816mode = 1;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART5_CONFIG_TX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART5_CONFIG_DUPLEX & PRU_SUART_HALF_TX_DISABLED) == PRU_SUART_HALF_TX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART5_CONFIG_TX_SER)) =  MCASP_SRCTL_TX_MODE;
#endif
    pru_suart_regs->Reserved1 = 1; 
    pru_suart_tx_priv = (pru_suart_tx_cntx_priv *) (pu32_pru_ram_base + 0x0B0); /* SUART5 TX context base addr */
    pru_suart_tx_priv->asp_xsrctl_base = (unsigned int)(MCASP_SRCTL_BASE_ADDR + (PRU_SUART5_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->asp_xbuf_base = (unsigned int)(MCASP_XBUF_BASE_ADDR + (PRU_SUART5_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->buff_addr = 0x0090; /* SUART5 TX formatted data base addr */
    
    /* Channel 1 is Rx context information */
    pru_suart_regs++;
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_RX;
    pru_suart_regs->CH_Ctrl_Config1.iso7816mode = 1;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART5_CONFIG_RX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART5_CONFIG_DUPLEX & PRU_SUART_HALF_RX_DISABLED) == PRU_SUART_HALF_RX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART5_CONFIG_RX_SER)) = MCASP_SRCTL_RX_MODE;
#endif
    /* RX is active by default, write the dummy received data at PRU RAM addr 0x1FC to avoid memory corruption */
    pru_suart_regs->CH_TXRXData = RX_DEFAULT_DATA_DUMP_ADDR;
    pru_suart_regs->Reserved1 = 0;
    pru_suart_rx_priv = (pru_suart_rx_cntx_priv *) (pu32_pru_ram_base + 0x0C0); /* SUART5 RX context base addr */
    pru_suart_rx_priv->asp_rbuf_base = (unsigned int)(MCASP_RBUF_BASE_ADDR + (PRU_SUART5_CONFIG_RX_SER << 2));
    pru_suart_rx_priv->asp_rsrctl_base = (unsigned int) (MCASP_SRCTL_BASE_ADDR + (PRU_SUART5_CONFIG_RX_SER << 2));
    
    
    /* ***************************** UART 5  **************************************** */
    /* Channel 2 context information is Tx */
    pru_suart_regs++;    
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_TX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART6_CONFIG_TX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART6_CONFIG_DUPLEX & PRU_SUART_HALF_TX_DISABLED) == PRU_SUART_HALF_TX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART6_CONFIG_TX_SER)) = MCASP_SRCTL_TX_MODE;
#endif
    pru_suart_regs->Reserved1 = 1; 
    pru_suart_tx_priv = (pru_suart_tx_cntx_priv *) (pu32_pru_ram_base + 0x100); /* SUART6 TX context base addr */
    pru_suart_tx_priv->asp_xsrctl_base = (unsigned int)(MCASP_SRCTL_BASE_ADDR + (PRU_SUART6_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->asp_xbuf_base = (unsigned int)(MCASP_XBUF_BASE_ADDR + (PRU_SUART6_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->buff_addr = 0x00E0; /* SUART6 TX formatted data base addr */
    
    /* Channel 3 is Rx context information */
    pru_suart_regs++;
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_RX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART6_CONFIG_RX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART6_CONFIG_DUPLEX & PRU_SUART_HALF_RX_DISABLED) == PRU_SUART_HALF_RX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART6_CONFIG_RX_SER)) = MCASP_SRCTL_RX_MODE;
#endif
    /* RX is active by default, write the dummy received data at PRU RAM addr 0x1FC to avoid memory corruption */
    pru_suart_regs->CH_TXRXData = RX_DEFAULT_DATA_DUMP_ADDR;
    pru_suart_regs->Reserved1 = 0;
    pru_suart_rx_priv = (pru_suart_rx_cntx_priv *) (pu32_pru_ram_base + 0x110); /* SUART6 RX context base addr */
    pru_suart_rx_priv->asp_rbuf_base = (unsigned int)(MCASP_RBUF_BASE_ADDR + (PRU_SUART6_CONFIG_RX_SER << 2));
    pru_suart_rx_priv->asp_rsrctl_base = (unsigned int) (MCASP_SRCTL_BASE_ADDR + (PRU_SUART6_CONFIG_RX_SER << 2));

    /* ***************************** UART 6  **************************************** */
    /* Channel 4 context information is Tx */
    pru_suart_regs++;    
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_TX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART7_CONFIG_TX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART7_CONFIG_DUPLEX & PRU_SUART_HALF_TX_DISABLED) == PRU_SUART_HALF_TX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART7_CONFIG_TX_SER)) = MCASP_SRCTL_TX_MODE;
#endif
    pru_suart_regs->Reserved1 = 1; 
    pru_suart_tx_priv = (pru_suart_tx_cntx_priv *) (pu32_pru_ram_base + 0x150); /* SUART7 TX context base addr */
    pru_suart_tx_priv->asp_xsrctl_base = (unsigned int)( MCASP_SRCTL_BASE_ADDR + (PRU_SUART7_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->asp_xbuf_base = (unsigned int)(MCASP_XBUF_BASE_ADDR + (PRU_SUART7_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->buff_addr = 0x0130; /* SUART7 TX formatted data base addr */
    
    /* Channel 5 is Rx context information */
    pru_suart_regs++;
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_RX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART7_CONFIG_RX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART7_CONFIG_DUPLEX & PRU_SUART_HALF_RX_DISABLED) == PRU_SUART_HALF_RX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART7_CONFIG_RX_SER)) = MCASP_SRCTL_RX_MODE;
#endif
    /* RX is active by default, write the dummy received data at PRU RAM addr 0x1FC to avoid memory corruption */
    pru_suart_regs->CH_TXRXData = RX_DEFAULT_DATA_DUMP_ADDR;
    pru_suart_regs->Reserved1 = 0;
    pru_suart_rx_priv = (pru_suart_rx_cntx_priv *) (pu32_pru_ram_base + 0x160); /* SUART7 RX context base addr */
    pru_suart_rx_priv->asp_rbuf_base = (unsigned int)(MCASP_RBUF_BASE_ADDR + (PRU_SUART7_CONFIG_RX_SER << 2));
    pru_suart_rx_priv->asp_rsrctl_base = (unsigned int) (MCASP_SRCTL_BASE_ADDR + (PRU_SUART7_CONFIG_RX_SER << 2));

    /* ***************************** UART 7  **************************************** */
    /* Channel 6 context information is Tx */
    pru_suart_regs++;    
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_TX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART8_CONFIG_TX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART8_CONFIG_DUPLEX & PRU_SUART_HALF_TX_DISABLED) == PRU_SUART_HALF_TX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART8_CONFIG_TX_SER)) = MCASP_SRCTL_TX_MODE;
#endif
    pru_suart_regs->Reserved1 = 1; 
    pru_suart_tx_priv = (pru_suart_tx_cntx_priv *) (pu32_pru_ram_base + 0x1A0); /* SUART8 TX context base addr */
    pru_suart_tx_priv->asp_xsrctl_base = (unsigned int)(MCASP_SRCTL_BASE_ADDR + (PRU_SUART8_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->asp_xbuf_base = (unsigned int)(MCASP_XBUF_BASE_ADDR + (PRU_SUART8_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->buff_addr = 0x0180; /* SUART8 TX formatted data base addr */
    
    /* Channel 7 is Rx context information */
    pru_suart_regs++;
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_RX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART8_CONFIG_RX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART8_CONFIG_DUPLEX & PRU_SUART_HALF_RX_DISABLED) == PRU_SUART_HALF_RX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART8_CONFIG_RX_SER)) = MCASP_SRCTL_RX_MODE;
#endif
    /* RX is active by default, write the dummy received data at PRU RAM addr 0x1FC to avoid memory corruption */
    pru_suart_regs->CH_TXRXData = RX_DEFAULT_DATA_DUMP_ADDR;
    pru_suart_regs->Reserved1 = 0;
    pru_suart_rx_priv = (pru_suart_rx_cntx_priv *) (pu32_pru_ram_base + 0x1B0); /* SUART8 RX context base addr */
    pru_suart_rx_priv->asp_rbuf_base = (unsigned int)(MCASP_RBUF_BASE_ADDR + (PRU_SUART8_CONFIG_RX_SER << 2));
    pru_suart_rx_priv->asp_rsrctl_base = (unsigned int) (MCASP_SRCTL_BASE_ADDR + (PRU_SUART8_CONFIG_RX_SER << 2));
}
#else
void pru_set_ram_data (arm_pru_iomap * arm_iomap_pru)
{
    
    PRU_SUART_RegsOvly pru_suart_regs = (PRU_SUART_RegsOvly) arm_iomap_pru->pru_io_addr;
    unsigned int * pu32SrCtlAddr = (unsigned int *) ((unsigned int) 
					arm_iomap_pru->mcasp_io_addr + 0x180);	
    pru_suart_tx_cntx_priv * pru_suart_tx_priv = NULL;			
    pru_suart_rx_cntx_priv * pru_suart_rx_priv = NULL;
    unsigned char * pu32_pru_ram_base = (unsigned char *) arm_iomap_pru->pru_io_addr;	
							
    /* ***************************** UART 0  **************************************** */

    /* Channel 0 context information is Tx */
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_TX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART1_CONFIG_TX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART1_CONFIG_DUPLEX & PRU_SUART_HALF_TX_DISABLED) == PRU_SUART_HALF_TX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART1_CONFIG_TX_SER)) =  MCASP_SRCTL_TX_MODE;
#endif
    pru_suart_regs->Reserved1 = 1; 
    pru_suart_tx_priv = (pru_suart_tx_cntx_priv *) (pu32_pru_ram_base + 0x0B0); /* SUART1 TX context base addr */
    pru_suart_tx_priv->asp_xsrctl_base = (unsigned int)(MCASP_SRCTL_BASE_ADDR + (PRU_SUART1_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->asp_xbuf_base = (unsigned int)(MCASP_XBUF_BASE_ADDR + (PRU_SUART1_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->buff_addr = 0x0090; /* SUART1 TX formatted data base addr */
    
    /* Channel 1 is Rx context information */
    pru_suart_regs++;
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_RX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART1_CONFIG_RX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART1_CONFIG_DUPLEX & PRU_SUART_HALF_RX_DISABLED) == PRU_SUART_HALF_RX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART1_CONFIG_RX_SER)) = MCASP_SRCTL_RX_MODE;
#endif
    /* RX is active by default, write the dummy received data at PRU RAM addr 0x1FC to avoid memory corruption */
    pru_suart_regs->CH_TXRXData = RX_DEFAULT_DATA_DUMP_ADDR;
    pru_suart_regs->Reserved1 = 0;
    pru_suart_rx_priv = (pru_suart_rx_cntx_priv *) (pu32_pru_ram_base + 0x0C0); /* SUART1 RX context base addr */
    pru_suart_rx_priv->asp_rbuf_base = (unsigned int)(MCASP_RBUF_BASE_ADDR + (PRU_SUART1_CONFIG_RX_SER << 2));
    pru_suart_rx_priv->asp_rsrctl_base = (unsigned int) (MCASP_SRCTL_BASE_ADDR + (PRU_SUART1_CONFIG_RX_SER << 2));
    
    
    /* ***************************** UART 1  **************************************** */
    /* Channel 2 context information is Tx */
    pru_suart_regs++;    
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_TX;
    pru_suart_regs->CH_Ctrl_Config1.iso7816mode = 1;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART2_CONFIG_TX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART2_CONFIG_DUPLEX & PRU_SUART_HALF_TX_DISABLED) == PRU_SUART_HALF_TX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART2_CONFIG_TX_SER)) = MCASP_SRCTL_TX_MODE;
#endif
    pru_suart_regs->Reserved1 = 1; 
    pru_suart_tx_priv = (pru_suart_tx_cntx_priv *) (pu32_pru_ram_base + 0x100); /* SUART2 TX context base addr */
    pru_suart_tx_priv->asp_xsrctl_base = (unsigned int)(MCASP_SRCTL_BASE_ADDR + (PRU_SUART2_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->asp_xbuf_base = (unsigned int)(MCASP_XBUF_BASE_ADDR + (PRU_SUART2_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->buff_addr = 0x00E0; /* SUART2 TX formatted data base addr */
    
    /* Channel 3 is Rx context information */
    pru_suart_regs++;
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_RX;
    pru_suart_regs->CH_Ctrl_Config1.iso7816mode = 1;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART2_CONFIG_RX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART2_CONFIG_DUPLEX & PRU_SUART_HALF_RX_DISABLED) == PRU_SUART_HALF_RX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART2_CONFIG_RX_SER)) = MCASP_SRCTL_RX_MODE;
#endif
    /* RX is active by default, write the dummy received data at PRU RAM addr 0x1FC to avoid memory corruption */
    pru_suart_regs->CH_TXRXData = RX_DEFAULT_DATA_DUMP_ADDR;
    pru_suart_regs->Reserved1 = 0;
    pru_suart_rx_priv = (pru_suart_rx_cntx_priv *) (pu32_pru_ram_base + 0x110); /* SUART2 RX context base addr */
    pru_suart_rx_priv->asp_rbuf_base = (unsigned int)(MCASP_RBUF_BASE_ADDR + (PRU_SUART2_CONFIG_RX_SER << 2));
    pru_suart_rx_priv->asp_rsrctl_base = (unsigned int) (MCASP_SRCTL_BASE_ADDR + (PRU_SUART2_CONFIG_RX_SER << 2));

    /* ***************************** UART 2  **************************************** */
    /* Channel 4 context information is Tx */
    pru_suart_regs++;    
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_TX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART3_CONFIG_TX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART3_CONFIG_DUPLEX & PRU_SUART_HALF_TX_DISABLED) == PRU_SUART_HALF_TX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART3_CONFIG_TX_SER)) = MCASP_SRCTL_TX_MODE;
#endif
    pru_suart_regs->Reserved1 = 1; 
    pru_suart_tx_priv = (pru_suart_tx_cntx_priv *) (pu32_pru_ram_base + 0x150); /* SUART3 TX context base addr */
    pru_suart_tx_priv->asp_xsrctl_base = (unsigned int)( MCASP_SRCTL_BASE_ADDR + (PRU_SUART3_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->asp_xbuf_base = (unsigned int)(MCASP_XBUF_BASE_ADDR + (PRU_SUART3_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->buff_addr = 0x0130; /* SUART3 TX formatted data base addr */
    
    /* Channel 5 is Rx context information */
    pru_suart_regs++;
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_RX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART3_CONFIG_RX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART3_CONFIG_DUPLEX & PRU_SUART_HALF_RX_DISABLED) == PRU_SUART_HALF_RX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART3_CONFIG_RX_SER)) = MCASP_SRCTL_RX_MODE;
#endif
    /* RX is active by default, write the dummy received data at PRU RAM addr 0x1FC to avoid memory corruption */
    pru_suart_regs->CH_TXRXData = RX_DEFAULT_DATA_DUMP_ADDR;
    pru_suart_regs->Reserved1 = 0;
    pru_suart_rx_priv = (pru_suart_rx_cntx_priv *) (pu32_pru_ram_base + 0x160); /* SUART3 RX context base addr */
    pru_suart_rx_priv->asp_rbuf_base = (unsigned int)(MCASP_RBUF_BASE_ADDR + (PRU_SUART3_CONFIG_RX_SER << 2));
    pru_suart_rx_priv->asp_rsrctl_base = (unsigned int) (MCASP_SRCTL_BASE_ADDR + (PRU_SUART3_CONFIG_RX_SER << 2));

    /* ***************************** UART 3  **************************************** */
    /* Channel 6 context information is Tx */
    pru_suart_regs++;    
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_TX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART4_CONFIG_TX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART4_CONFIG_DUPLEX & PRU_SUART_HALF_TX_DISABLED) == PRU_SUART_HALF_TX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART4_CONFIG_TX_SER)) = MCASP_SRCTL_TX_MODE;
#endif
    pru_suart_regs->Reserved1 = 1; 
    pru_suart_tx_priv = (pru_suart_tx_cntx_priv *) (pu32_pru_ram_base + 0x1A0); /* SUART4 TX context base addr */
    pru_suart_tx_priv->asp_xsrctl_base = (unsigned int)(MCASP_SRCTL_BASE_ADDR + (PRU_SUART4_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->asp_xbuf_base = (unsigned int)(MCASP_XBUF_BASE_ADDR + (PRU_SUART4_CONFIG_TX_SER << 2));
    pru_suart_tx_priv->buff_addr = 0x0180; /* SUART4 TX formatted data base addr */
    
    /* Channel 7 is Rx context information */
    pru_suart_regs++;
    pru_suart_regs->CH_Ctrl_Config1.mode = SUART_CHN_RX;
    pru_suart_regs->CH_Ctrl_Config1.serializer_num = (0xF & PRU_SUART4_CONFIG_RX_SER);
    pru_suart_regs->CH_Ctrl_Config1.over_sampling = SUART_DEFAULT_OVRSMPL;
    pru_suart_regs->CH_Config2_TXRXStatus.bits_per_char = 8;
#if ((PRU_SUART4_CONFIG_DUPLEX & PRU_SUART_HALF_RX_DISABLED) == PRU_SUART_HALF_RX_DISABLED)
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_DISABLED;
#else
    pru_suart_regs->CH_Config2_TXRXStatus.chn_state = SUART_CHN_ENABLED;
    *((unsigned int *) (pu32SrCtlAddr + PRU_SUART4_CONFIG_RX_SER)) = MCASP_SRCTL_RX_MODE;
#endif
    /* RX is active by default, write the dummy received data at PRU RAM addr 0x1FC to avoid memory corruption */
    pru_suart_regs->CH_TXRXData = RX_DEFAULT_DATA_DUMP_ADDR;
    pru_suart_regs->Reserved1 = 0;
    pru_suart_rx_priv = (pru_suart_rx_cntx_priv *) (pu32_pru_ram_base + 0x1B0); /* SUART4 RX context base addr */
    pru_suart_rx_priv->asp_rbuf_base = (unsigned int)(MCASP_RBUF_BASE_ADDR + (PRU_SUART4_CONFIG_RX_SER << 2));
    pru_suart_rx_priv->asp_rsrctl_base = (unsigned int) (MCASP_SRCTL_BASE_ADDR + (PRU_SUART4_CONFIG_RX_SER << 2));
}

#endif

/*
 * suart Initialization routine 
 */
short pru_softuart_init(unsigned int txBaudValue,
			unsigned int rxBaudValue,
			unsigned int oversampling,
			unsigned char *pru_suart_emu0_code,
			unsigned int fw0_size,
			unsigned char *pru_suart_emu1_code,
			unsigned int fw1_size,
            arm_pru_iomap * arm_iomap_pru)
{
	unsigned int omapl_addr;
	unsigned int u32loop;
	short status = PRU_SUART_SUCCESS;
	short idx;
	short retval;

	pru_arm_iomap.pru_io_addr = arm_iomap_pru->pru_io_addr;
	pru_arm_iomap.mcasp_io_addr = arm_iomap_pru->mcasp_io_addr;
	pru_arm_iomap.psc0_io_addr = arm_iomap_pru->psc0_io_addr;
	pru_arm_iomap.psc1_io_addr = arm_iomap_pru->psc1_io_addr;
	pru_arm_iomap.syscfg_io_addr = arm_iomap_pru->syscfg_io_addr;
    pru_arm_iomap.gpio_io_addr = arm_iomap_pru->gpio_io_addr;
    pru_arm_iomap.ehrpwm1_io_addr = arm_iomap_pru->ehrpwm1_io_addr;
	pru_arm_iomap.pFifoBufferPhysBase = arm_iomap_pru->pFifoBufferPhysBase;
	pru_arm_iomap.pFifoBufferVirtBase = arm_iomap_pru->pFifoBufferVirtBase;
	pru_arm_iomap.pru_clk_freq = arm_iomap_pru->pru_clk_freq;

	omapl_addr = (unsigned int)arm_iomap_pru->syscfg_io_addr;

	omapl_addr = (unsigned int)arm_iomap_pru->psc1_io_addr;
	suart_mcasp_psc_enable(omapl_addr);
        scrdr_ehrpwm_psc_enable(omapl_addr);

	omapl_addr = (unsigned int)arm_iomap_pru->mcasp_io_addr;
	/* Configure McASP0  */
	suart_mcasp_config(omapl_addr, txBaudValue, rxBaudValue, oversampling,
			   arm_iomap_pru);	

	/* Configure Timer2 for DIR and clear DIR pins */
       if (PRU_SUART1_CONFIG_DIR_EN | PRU_SUART2_CONFIG_DIR_EN | PRU_SUART3_CONFIG_DIR_EN | PRU_SUART4_CONFIG_DIR_EN){
               suart_timer2_config( );
       }

#if (PRU0_MODE != PRU_MODE_INVALID)
	pru_enable(0, arm_iomap_pru);
#endif
#if (PRU1_MODE != PRU_MODE_INVALID)
	pru_enable(1, arm_iomap_pru);
#endif

	omapl_addr = (unsigned int) arm_iomap_pru->pru_io_addr;

	for (u32loop = 0; u32loop < 512; u32loop++)
	{
#if (PRU0_MODE != PRU_MODE_INVALID)
		*(unsigned int *)(omapl_addr | u32loop) = 0x0;
#endif
#if (PRU1_MODE != PRU_MODE_INVALID)
		*(unsigned int *)(omapl_addr | u32loop | 0x2000) = 0x0;
#endif
	}

#if (PRU0_MODE != PRU_MODE_INVALID)
	pru_load(PRU_NUM0, (unsigned int *)pru_suart_emu0_code,
		 (fw0_size / sizeof(unsigned int)), arm_iomap_pru);
#endif

#if (PRU1_MODE != PRU_MODE_INVALID)
	pru_load(PRU_NUM1, (unsigned int *)pru_suart_emu1_code,
		 (fw1_size / sizeof(unsigned int)), arm_iomap_pru);
#endif

	retval = arm_to_pru_intr_init();
	if (-1 == retval) {
		return status;
	}
	pru_set_delay_count (pru_arm_iomap.pru_clk_freq);

#if (PRU0_MODE != PRU_MODE_INVALID)
	suart_set_pru_id(0);
	pru_set_rx_tx_mode(PRU0_MODE, PRU_NUM0);
#endif

#if (PRU1_MODE != PRU_MODE_INVALID)
	suart_set_pru_id(1);
	pru_set_rx_tx_mode(PRU1_MODE, PRU_NUM1);
#endif
	
        pru_set_ram_data (arm_iomap_pru);

#if (PRU0_MODE != PRU_MODE_INVALID)
	pru_run(PRU_NUM0, arm_iomap_pru);
#endif

#if (PRU1_MODE != PRU_MODE_INVALID)
	pru_run(PRU_NUM1, arm_iomap_pru); 
#endif

	/* Initialize gUartStatuTable */
	for (idx = 0; idx < 8; idx++) {
		gUartStatuTable[idx] = ePRU_SUART_UART_FREE;
	}

	return status;
}

static void pru_set_rx_tx_mode(Uint32 pru_mode, Uint32 pruNum)
{

	unsigned int pruOffset;

	if (pruNum == PRU_NUM0) 
	{
		/* PRU0 */
		pruOffset = PRU_SUART_PRU0_RX_TX_MODE;
	} 
	else if (pruNum == PRU_NUM1) {
		/* PRU1 */
		pruOffset = PRU_SUART_PRU1_RX_TX_MODE;
	}
	else	
	{
		return;
	}

	pru_ram_write_data(pruOffset, (Uint8 *)&pru_mode, 1, &pru_arm_iomap);

}


void pru_set_fifo_timeout(Uint32 timeout)
{
#if (PRU0_MODE != PRU_MODE_INVALID)
	/* PRU 0 */
	pru_ram_write_data(PRU_SUART_PRU0_IDLE_TIMEOUT_OFFSET, 
				(Uint8 *)&timeout, 2, &pru_arm_iomap);
#endif

#if (PRU1_MODE != PRU_MODE_INVALID)
    	/* PRU 1 */
	pru_ram_write_data(PRU_SUART_PRU1_IDLE_TIMEOUT_OFFSET, 
				(Uint8 *)&timeout, 2, &pru_arm_iomap);
#endif
}

/* Not needed as PRU Soft Uart Firmware is implemented as Mcasp Event Based */
static void pru_set_delay_count (Uint32 pru_freq)
{
	Uint32 u32delay_cnt;

	if (pru_freq == 228 )
	{
		u32delay_cnt = 5;
	}else  if (pru_freq == 186)
    	{
        	u32delay_cnt = 5;
    	}
	else
	{
		u32delay_cnt =3;
	}

#if (PRU0_MODE != PRU_MODE_INVALID)
    	/* PRU 0 */
    	pru_ram_write_data(PRU_SUART_PRU0_DELAY_OFFSET,
        	        (Uint8 *)&u32delay_cnt, 1, &pru_arm_iomap);
#endif

#if (PRU1_MODE != PRU_MODE_INVALID)
    	/* PRU 1 */
    	pru_ram_write_data(PRU_SUART_PRU1_DELAY_OFFSET,
        	        (Uint8 *)&u32delay_cnt, 1, &pru_arm_iomap);
#endif

}

void pru_mcasp_deinit (void)
{
	suart_mcasp_reset (&pru_arm_iomap);
}

short pru_softuart_deinit(void)
{
	unsigned int offset;
    	short s16retval = 0;
    	unsigned int u32value = 0;

	pru_disable(&pru_arm_iomap);	

        offset =
        (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_STATCLRINT1 &
                               0xFFFF);
        u32value = 0xFFFFFFFF;
        s16retval =
        pru_ram_write_data_4byte(offset, (unsigned int *)&u32value, 1);
        if (-1 == s16retval) {
                return -1;
        }
        offset =
        (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_STATCLRINT0 &
                               0xFFFF);
        u32value = 0xFFFFFFFF;
        s16retval =
        pru_ram_write_data_4byte(offset, (unsigned int *)&u32value, 1);
        if (-1 == s16retval) {
                return -1;
        }

	return PRU_SUART_SUCCESS;
}

/*
 * suart Instance open routine
 */
short pru_softuart_open(suart_handle hSuart)
{
	short status = PRU_SUART_SUCCESS;

	switch (hSuart->uartNum) {
		/* ************ PRU 0 ************** */
	case PRU_SUART_UART1:
		if (gUartStatuTable[PRU_SUART_UART1 - 1] ==
		    ePRU_SUART_UART_IN_USE) {
			status = SUART_UART_IN_USE;
			return status;
		} else {
			hSuart->uartStatus = ePRU_SUART_UART_IN_USE;
			hSuart->uartType = PRU_SUART1_CONFIG_DUPLEX;
			hSuart->uartTxChannel = PRU_SUART1_CONFIG_TX_SER;
			hSuart->uartRxChannel = PRU_SUART1_CONFIG_RX_SER;

			gUartStatuTable[PRU_SUART_UART1 - 1] =
			    ePRU_SUART_UART_IN_USE;
		}

		break;

	case PRU_SUART_UART2:
		if (gUartStatuTable[PRU_SUART_UART2 - 1] ==
		    ePRU_SUART_UART_IN_USE) {
			status = SUART_UART_IN_USE;
			return status;
		} else {

			hSuart->uartStatus = ePRU_SUART_UART_IN_USE;
            		hSuart->uartType = PRU_SUART2_CONFIG_DUPLEX;
   		        hSuart->uartTxChannel = PRU_SUART2_CONFIG_TX_SER;
        		hSuart->uartRxChannel = PRU_SUART2_CONFIG_RX_SER;

			gUartStatuTable[PRU_SUART_UART2 - 1] =
			    ePRU_SUART_UART_IN_USE;
		}

		break;

	case PRU_SUART_UART3:
		if (gUartStatuTable[PRU_SUART_UART3 - 1] ==
		    ePRU_SUART_UART_IN_USE) {
			status = SUART_UART_IN_USE;
			return status;
		} else {

			hSuart->uartStatus = ePRU_SUART_UART_IN_USE;
		        hSuart->uartType = PRU_SUART3_CONFIG_DUPLEX;
            		hSuart->uartTxChannel = PRU_SUART3_CONFIG_TX_SER;
            		hSuart->uartRxChannel = PRU_SUART3_CONFIG_RX_SER;

			gUartStatuTable[PRU_SUART_UART3 - 1] =
			    ePRU_SUART_UART_IN_USE;
		}

		break;

	case PRU_SUART_UART4:
		if (gUartStatuTable[PRU_SUART_UART4 - 1] ==
		    ePRU_SUART_UART_IN_USE) {
			status = SUART_UART_IN_USE;
			return status;
		} else {

			hSuart->uartStatus = ePRU_SUART_UART_IN_USE;
            		hSuart->uartType = PRU_SUART4_CONFIG_DUPLEX;
            		hSuart->uartTxChannel = PRU_SUART4_CONFIG_TX_SER;
            		hSuart->uartRxChannel = PRU_SUART4_CONFIG_RX_SER;

			gUartStatuTable[PRU_SUART_UART4 - 1] =
			    ePRU_SUART_UART_IN_USE;
		}
		break;

		/* ************ PRU 1 ************** */
	case PRU_SUART_UART5:
		if (gUartStatuTable[PRU_SUART_UART5 - 1] ==
		    ePRU_SUART_UART_IN_USE) {
			status = SUART_UART_IN_USE;
			return status;
		} else {
			hSuart->uartStatus = ePRU_SUART_UART_IN_USE;
            		hSuart->uartType = PRU_SUART5_CONFIG_DUPLEX;
            		hSuart->uartTxChannel = PRU_SUART5_CONFIG_TX_SER;
            		hSuart->uartRxChannel = PRU_SUART5_CONFIG_RX_SER;

			gUartStatuTable[PRU_SUART_UART5 - 1] =
			    ePRU_SUART_UART_IN_USE;
		}
		break;

	case PRU_SUART_UART6:
		if (gUartStatuTable[PRU_SUART_UART6 - 1] ==
		    ePRU_SUART_UART_IN_USE) {
			status = SUART_UART_IN_USE;
			return status;
		} else {
			hSuart->uartStatus = ePRU_SUART_UART_IN_USE;
            		hSuart->uartType = PRU_SUART6_CONFIG_DUPLEX;
            		hSuart->uartTxChannel = PRU_SUART6_CONFIG_TX_SER;
            		hSuart->uartRxChannel = PRU_SUART6_CONFIG_RX_SER;

			gUartStatuTable[PRU_SUART_UART6 - 1] =
			    ePRU_SUART_UART_IN_USE;
		}

		break;

	case PRU_SUART_UART7:
		if (gUartStatuTable[PRU_SUART_UART7 - 1] ==
		    ePRU_SUART_UART_IN_USE) {
			status = SUART_UART_IN_USE;
			return status;
		} else {
			hSuart->uartStatus = ePRU_SUART_UART_IN_USE;
            		hSuart->uartType = PRU_SUART7_CONFIG_DUPLEX;
            		hSuart->uartTxChannel = PRU_SUART7_CONFIG_TX_SER;
            		hSuart->uartRxChannel = PRU_SUART7_CONFIG_RX_SER;

			gUartStatuTable[PRU_SUART_UART7 - 1] =
			    ePRU_SUART_UART_IN_USE;
		}
		break;

	case PRU_SUART_UART8:
		if (gUartStatuTable[PRU_SUART_UART8 - 1] ==
		    ePRU_SUART_UART_IN_USE) {
			status = SUART_UART_IN_USE;
			return status;
		} else {
			hSuart->uartStatus = ePRU_SUART_UART_IN_USE;
            		hSuart->uartType = PRU_SUART8_CONFIG_DUPLEX;
            		hSuart->uartTxChannel = PRU_SUART8_CONFIG_TX_SER;
            		hSuart->uartRxChannel = PRU_SUART8_CONFIG_RX_SER;

			gUartStatuTable[PRU_SUART_UART8 - 1] =
			    ePRU_SUART_UART_IN_USE;
		}
		break;

	default:
		/* return invalid UART */
		status = SUART_INVALID_UART_NUM;
		break;
	}
	return (status);
}

/*
 * suart instance close routine 
 */
short pru_softuart_close(suart_handle hUart)
{
	short status = SUART_SUCCESS;

	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	} else {
		gUartStatuTable[hUart->uartNum - 1] = ePRU_SUART_UART_FREE;
		/* Reset the Instance to Invalid */
		hUart->uartNum = PRU_SUART_UARTx_INVALID;
		hUart->uartStatus = ePRU_SUART_UART_FREE;
	}
	return (status);
}

/*
 * suart routine for setting relative baud rate 
 */
short pru_softuart_setbaud
    (suart_handle hUart,
     unsigned short txClkDivisor, 
	 unsigned short rxClkDivisor) 
{
	unsigned int offset;
	unsigned int pruOffset;
	short status = SUART_SUCCESS;
	unsigned short chNum;
	unsigned short regval = 0;
	CSL_ePWMRegsOvly   ePwm       = (CSL_ePWMRegsOvly) pru_arm_iomap.ehrpwm1_io_addr;

	if (hUart == NULL) 
	{
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	/* Set the clock divisor value into the McASP */
	if ((txClkDivisor > 385) || (txClkDivisor == 0)) 
	{
		return SUART_INVALID_CLKDIVISOR;
	}

	if ((rxClkDivisor > 385) || (rxClkDivisor == 0)) 
	{
		return SUART_INVALID_CLKDIVISOR;
	}

	
	chNum = hUart->uartNum - 1;
 
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
		}
	}	
	else if (PRU0_MODE == PRU_MODE_TX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_TX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}
	



	if (txClkDivisor != 0) 
	{
		offset =
		    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG1_OFFSET;
		pru_ram_read_data(offset, (Uint8 *) &regval, 2,
				   &pru_arm_iomap);
		regval &= (~0x3FF);
		regval |= txClkDivisor;
		pru_ram_write_data(offset, (Uint8 *) &regval, 2,
				   &pru_arm_iomap);
	}

	if (PRU0_MODE == PRU_MODE_RX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_RX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}	
	else	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		chNum++;
	}	
	else
	{
		return PRU_MODE_INVALID;
	}	
	
	regval = 0;
	if (rxClkDivisor != 0) 
	{
		offset =
		    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG1_OFFSET;
		pru_ram_read_data(offset, (Uint8 *) &regval, 2,
				   &pru_arm_iomap);
		regval &= (~0x3FF);
		regval |= txClkDivisor;
		pru_ram_write_data(offset, (Uint8 *) &regval, 2,
				   &pru_arm_iomap);
	}

	if(hUart->uartNum == PRU_SC_UART_PORT) {
		switch(txClkDivisor) {
		case 0xC0: // Baud 300
			ePwm->CMPA    = 0x672;
			ePwm->CMPB    = 0x1356;
			break;
		case 0x60: // Baud 600
			ePwm->CMPA    = 0x334;
			ePwm->CMPB    = 0x992;
			break;
		case 0x20: // Baud 1800
			ePwm->CMPA    = 0xFA;
			ePwm->CMPB    = 0x320;
			break;
		case 0x18: // Baud 2400
			ePwm->CMPA    = 0xC8;
			ePwm->CMPB    = 0x258;
			break;
		case 0x0C: // Baud 4800
			ePwm->CMPA    = 0x64;
			ePwm->CMPB    = 0x12C;
			break;
		case 0x06: // Baud 9600
			ePwm->CMPA    = 0x2B;
			ePwm->CMPB    = 0x77;
			break;
		case 0x03: // Baud 19200 
			ePwm->CMPA    = 0x10;
			ePwm->CMPB    = 0x41;
			break;
		case 0x01: // Baud 38400 
			ePwm->CMPA    = 0x05;
			ePwm->CMPB    = 0x1E;
			break;
		default:
			break;
		}
	}
	return status;
}

/*
 * suart routine for setting dir delay for a specific uart
 */
short pru_softuart_setdir
    (suart_handle hUart, unsigned short dir_delay_offset, unsigned int timer_freq, unsigned short txClkDivisor) {
        unsigned int offset;
        unsigned int pruOffset;
        short status = SUART_SUCCESS;
        unsigned short chNum;
        unsigned int regval = 0;
	unsigned int  dir_delay;
	unsigned int temp;
	unsigned int tempR;

        if (hUart == NULL)
        {
                return (PRU_SUART_ERR_HANDLE_INVALID);
        }

        /* Set the clock divisor value into the McASP */
        if ((txClkDivisor > 385) || (txClkDivisor == 0))
        {
                return SUART_INVALID_CLKDIVISOR;
        }

	if (dir_delay_offset > 16)
	{
		return SUART_INVALID_DIR_DELAY_OFFSET;
	}

        chNum = hUart->uartNum - 1;

        if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
        {
                /* channel starts from 0 and uart instance starts from 1 */
                chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

                if (hUart->uartNum <= 4)
                {
                        /* PRU0 */
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
                } else {
                        /* PRU1 */
                        pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
                        /* First 8 channel corresponds to PRU0 */
                        chNum -= 8;
                }
        }
        else if (PRU0_MODE == PRU_MODE_TX_ONLY )
        {
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
        }
        else if (PRU1_MODE == PRU_MODE_TX_ONLY)
        {
                pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
        }
        else
        {
                return PRU_MODE_INVALID;
        }

        offset =
            pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
            PRU_SUART_CH_CTRL_OFFSET;

        pru_ram_read_data(offset, (Uint8 *) &regval, 2,
                                   &pru_arm_iomap);
        regval &= (~0x0FFF);

	if (regval > 0) {

		if ( hUart->uartNum == 1 )
			dir_delay = PRU_SUART1_CONFIG_DIR_DEL;
		else if ( hUart->uartNum == 2 )
                        dir_delay = PRU_SUART2_CONFIG_DIR_DEL;
                else if ( hUart->uartNum == 3 )
                        dir_delay = PRU_SUART3_CONFIG_DIR_DEL;
                else if ( hUart->uartNum == 4 )
                        dir_delay = PRU_SUART4_CONFIG_DIR_DEL;
		else
			return SUART_INVALID_DIR_UART;

                offset =
                     pruOffset + ((hUart->uartNum - 1) * 4) + PRU_SUART_CH_DIRDEL_OFFSET;

                dir_delay_offset = (dir_delay_offset * txClkDivisor) % 16;
                dir_delay = (dir_delay * txClkDivisor) + dir_delay_offset;
		// integer division
		temp = (dir_delay / txClkDivisor) * (timer_freq / 100);
		tempR = (dir_delay % txClkDivisor) * (timer_freq / 100);
		regval = (temp / ((SUART_DEFAULT_BAUD/100) / txClkDivisor)) + (tempR / ((SUART_DEFAULT_BAUD/100) / txClkDivisor));

                pru_ram_write_data(offset, (Uint8 *) &regval, 4,
                                   &pru_arm_iomap);

        }

	return (status);
}



/*
 * suart routine for setting parity for a specific uart
 */
short pru_softuart_setparity
    (suart_handle hUart, unsigned short tx_parity, unsigned short rx_parity) {
	unsigned int offset;
        unsigned int pruOffset;
        short status = SUART_SUCCESS;
        unsigned short chNum;
        unsigned int  reg_val;
	
	if (hUart == NULL) {
                return (PRU_SUART_ERR_HANDLE_INVALID);
        }
	
	 /*
         * NOTE:
         * The supported parities are none, odd, and even
         */

	if (tx_parity > ePRU_SUART_PARITY_EVEN || rx_parity > ePRU_SUART_PARITY_EVEN) {
                return PRU_SUART_ERR_PARAMETER_INVALID;
        }

	chNum = hUart->uartNum - 1;

        if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
        {
                /* channel starts from 0 and uart instance starts from 1 */
                chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

                if (hUart->uartNum <= 4)
                {
                        /* PRU0 */
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
                } else {
                        /* PRU1 */
                        pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
                        /* First 8 channel corresponds to PRU0 */
                        chNum -= 8;
                }
        }
        else if (PRU0_MODE == PRU_MODE_TX_ONLY )
        {
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
        }
        else if (PRU1_MODE == PRU_MODE_TX_ONLY)
        {
                pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
        }
        else
        {
                return PRU_MODE_INVALID;
        }

	if (tx_parity >= 0)
	{

		offset =
	                pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
        	        PRU_SUART_CH_CONFIG2_OFFSET;

        	pru_ram_read_data(offset, (Uint8 *) &reg_val, 1,
                	&pru_arm_iomap);

		reg_val &= ~(PRU_SUART_CH_CONFIG2_PARITY_MASK);
		reg_val |= (tx_parity << PRU_SUART_CH_CONFIG2_PARITY_SHIFT);

 	        pru_ram_write_data(offset, (Uint8 *) & reg_val, 1,
         	       &pru_arm_iomap);
	}

        if (PRU0_MODE == PRU_MODE_RX_ONLY )
        {
                pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
        }
        else if (PRU1_MODE == PRU_MODE_RX_ONLY)
        {
                pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
        }
        else    if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
        {
                chNum++;
        }
        else
        {
                return PRU_MODE_INVALID;
        }

	if (rx_parity >= 0)
        {

                offset =
                        pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
                        PRU_SUART_CH_CONFIG2_OFFSET;

                pru_ram_read_data(offset, (Uint8 *) &reg_val, 1,
                        &pru_arm_iomap);

                reg_val &= ~(PRU_SUART_CH_CONFIG2_PARITY_MASK);
                reg_val |= (rx_parity << PRU_SUART_CH_CONFIG2_PARITY_SHIFT);

                pru_ram_write_data(offset, (Uint8 *) & reg_val, 1,
                       &pru_arm_iomap);
        }

	return (status);
}

/*
 * suart routine for setting parity for a specific uart
 */
short pru_softuart_setstopbits
    (suart_handle hUart, unsigned short stop_bits) {
        unsigned int offset;
        unsigned int pruOffset;
        short status = SUART_SUCCESS;
        unsigned short chNum;
        unsigned int  reg_val;

        if (hUart == NULL) {
                return (PRU_SUART_ERR_HANDLE_INVALID);
        }

         /*
         * NOTE:
         * The supported parities are none, odd, and even
         */

        if (stop_bits > ePRU_SUART_2STOPBITS || stop_bits < ePRU_SUART_1STOPBITS) {
                return PRU_SUART_ERR_PARAMETER_INVALID;
        }

        chNum = hUart->uartNum - 1;

        if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
        {
                /* channel starts from 0 and uart instance starts from 1 */
                chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

                if (hUart->uartNum <= 4)
                {
                        /* PRU0 */
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
                } else {
                        /* PRU1 */
                        pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
                        /* First 8 channel corresponds to PRU0 */
                        chNum -= 8;
                }
        }
        else if (PRU0_MODE == PRU_MODE_TX_ONLY )
        {
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
        }
        else if (PRU1_MODE == PRU_MODE_TX_ONLY)
        {
                pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
        }
        else
        {
                return PRU_MODE_INVALID;
        }

	if (PRU1_MODE == PRU_MODE_RX_TX_BOTH && stop_bits == 2)
		stop_bits = ePRU_SUART_1STOPBITS;

        if (stop_bits == 1 || stop_bits == 2)
        {

                offset =
                        pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
                        PRU_SUART_CH_CTRL_OFFSET;

		pru_ram_read_data(offset, (Uint8 *) &reg_val, 1,
                        &pru_arm_iomap);

		reg_val &= ~(PRU_SUART_CH_CNTL_STOPBIT_MASK);
		reg_val |= ((stop_bits - 1) << PRU_SUART_CH_CNTL_STOPBIT_SHIFT);

                pru_ram_write_data(offset, (Uint8 *) &reg_val, 1,
                                   &pru_arm_iomap);
	}

	return (status);

}

/*
 * suart routine for setting number of bits per character for a specific uart 
 */
short pru_softuart_setdatabits
    (suart_handle hUart, unsigned short txDataBits, unsigned short rxDataBits, unsigned short stopBits) {
	unsigned int offset;
	unsigned int pruOffset;
	short status = SUART_SUCCESS;
	unsigned short chNum;
    	unsigned int  reg_val;
	unsigned int  temp;

	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	/*
	 * NOTE:
	 * The supported data bits are 6, 7, 8, 9, 10, 11 and 12 bits per character
	 */

	if ((txDataBits < ePRU_SUART_DATA_BITS6) || (txDataBits > ePRU_SUART_DATA_BITS12)) {
		return PRU_SUART_ERR_PARAMETER_INVALID;
	}

	if ((rxDataBits < ePRU_SUART_DATA_BITS6) || (rxDataBits > ePRU_SUART_DATA_BITS12)) {
		return PRU_SUART_ERR_PARAMETER_INVALID;
	}

	chNum = hUart->uartNum - 1;
	
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
		}
	}	
	else if (PRU0_MODE == PRU_MODE_TX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_TX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}


	if (txDataBits != 0) {
		offset =
		    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG2_OFFSET;
		pru_ram_read_data(offset, (Uint8 *) &reg_val, 1,
					   &pru_arm_iomap);

		temp = (reg_val & PRU_SUART_CH_CONFIG2_PARITY_MASK) >> PRU_SUART_CH_CONFIG2_PARITY_SHIFT;
		/* Check if parity implemented */
		if ((temp == 0x1) | (temp == 0x2))
			txDataBits += 1;

		/* Check if 2 stop bits implemented */
		if((stopBits == 2) & (PRU1_MODE != PRU_MODE_RX_TX_BOTH)){
			txDataBits += 1;
		}


        	reg_val &= ~(0xF);
        	reg_val |= txDataBits;
		pru_ram_write_data(offset, (Uint8 *) &reg_val, 1,
				   &pru_arm_iomap);
	}

	if (PRU0_MODE == PRU_MODE_RX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_RX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}	
	else	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		chNum++;
	}	
	else
	{
		return PRU_MODE_INVALID;
	}

	if (rxDataBits != 0) {
		offset =
		    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG2_OFFSET;

       		 pru_ram_read_data(offset, (Uint8 *) &reg_val, 1,
                	   &pru_arm_iomap);

		temp = (reg_val & PRU_SUART_CH_CONFIG2_PARITY_MASK) >> PRU_SUART_CH_CONFIG2_PARITY_SHIFT;
		if ((temp == 0x1) | (temp == 0x2))
                        rxDataBits += 1;
	        reg_val &= ~(0xF);
        	reg_val |= rxDataBits;
		
		pru_ram_write_data(offset, (Uint8 *) & reg_val, 1,
				   &pru_arm_iomap);

// Below is for debug
                pru_ram_read_data(offset, (Uint8 *) &reg_val, 4,
                                           &pru_arm_iomap);

// SHOULD THIS BE & reg_val, not rxDataBits?

	}

	return (status);
}

/*
 * suart routine to configure specific uart 
 */
short pru_softuart_setconfig(suart_handle hUart, suart_config * configUart, unsigned int timer_freq)
{
	unsigned int offset;
	unsigned int pruOffset;
	short status = SUART_SUCCESS;
	unsigned short chNum;
	unsigned short regVal = 0;
	unsigned int dir_delay_offset;
	unsigned int dir_delay;
	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	/*
	 * NOTE:
	 * Dependent baud rate for the given UART, the value MUST BE LESS THAN OR 
	 * EQUAL TO 64, preScalarValue <= 64
	 */
	/* Validate the value of relative buad rate */
	if ((configUart->txClkDivisor > 384) || (configUart->rxClkDivisor > 384)) {
		return SUART_INVALID_CLKDIVISOR;
	}
	/* Validate the bits per character */
	if ((configUart->txBitsPerChar < 8) || (configUart->txBitsPerChar > 14)) {
		return PRU_SUART_ERR_PARAMETER_INVALID;
	}

	if ((configUart->rxBitsPerChar < 8) || (configUart->rxBitsPerChar > 14)) {
		return PRU_SUART_ERR_PARAMETER_INVALID;
	}

	chNum = hUart->uartNum - 1;
	
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
	
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;
		
		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
		}
	}	
	else if (PRU0_MODE == PRU_MODE_TX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_TX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}

	/* Configuring the Transmit part of the given UART */
	if (configUart->TXSerializer != PRU_SUART_SERIALIZER_NONE) {
		/* Serializer has been as TX in mcasp config, by writing 1 in bits corresponding to tx serializer 
		   in PFUNC regsiter i.e. already set to GPIO mode PRU code will set then back to MCASP mode once
		   TX request for that serializer is posted. It is required because at this point Mcasp is accessed
		   by both PRU and DSP have lower priority for Mcasp in comparison to PRU and DPS keeps on looping 
		   there only. 
        	*/

		/*
		  suart_mcasp_tx_serialzier_set (configUart->TXSerializer, &pru_arm_iomap);
		*/

		/* Configuring TX serializer  */
		offset =
		    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CTRL_OFFSET;
		pru_ram_read_data(offset, (Uint8 *) & regVal, 2,
				  &pru_arm_iomap);

		regVal |= (configUart->TXSerializer <<
			      PRU_SUART_CH_CTRL_SR_SHIFT);

		pru_ram_write_data(offset, (Uint8 *) & regVal, 2,
				   &pru_arm_iomap);

		/* Configuring DIR Enable value */
		regVal |= (configUart->DIREnable << PRU_SUART_CH_CTRL_DIR_EN_SHIFT);
		pru_ram_write_data(offset, (Uint8 *) & regVal, 2,
                                   &pru_arm_iomap);

		/* Configuring the Transmit part of the given UART */
		/* Configuring TX prescalar value */
		offset =
		    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG1_OFFSET;
		pru_ram_read_data(offset, (Uint8 *) & regVal, 2,
				  &pru_arm_iomap);
		regVal =
		    regVal | (configUart->txClkDivisor <<
			      PRU_SUART_CH_CONFIG1_DIVISOR_SHIFT);
		pru_ram_write_data(offset, (Uint8 *) & regVal, 2,
				   &pru_arm_iomap);
		/* Configuring TX bits per character value */
		offset =
		    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG2_OFFSET;
		pru_ram_read_data(offset, (Uint8 *) & regVal, 2,
				  &pru_arm_iomap);
		regVal =
		    regVal | (configUart->txBitsPerChar <<
			      PRU_SUART_CH_CONFIG2_BITPERCHAR_SHIFT);
		pru_ram_write_data(offset, (Uint8 *) & regVal, 2,
				   &pru_arm_iomap);
		/* Configuring TX DIR values */
		if (configUart->DIREnable != PRU_SUART_DIR_DISABLED) {
			offset =
		            pruOffset + ((hUart->uartNum - 1) * 4) + PRU_SUART_CH_DIRDEL_OFFSET;

			dir_delay_offset = ((configUart->txBitsPerChar + 2) * configUart->txClkDivisor) % 16;
			dir_delay_offset = (configUart->DIRDelay * configUart->txClkDivisor) + dir_delay_offset;
			dir_delay =  (dir_delay_offset/configUart->txClkDivisor) * timer_freq / (SUART_DEFAULT_BAUD / configUart->txClkDivisor);

			pru_ram_write_data(offset, (Uint8 *) & dir_delay, 4,
                                   &pru_arm_iomap);

			offset =
			    pruOffset + ((hUart->uartNum - 1) * 4) + PRU_SUART_CH_DIRPIN_OFFSET;

			pru_ram_read_data(offset, (Uint8 *) & regVal, 2,
                                  &pru_arm_iomap);
			if((configUart->DIRPin >= 0) & (configUart->DIRPin < 32)) {
                                regVal =
                                   regVal | configUart->DIRPin;

				pru_ram_write_data(offset, (Uint8 *) & regVal, 2,
                                     &pru_arm_iomap);
			 } else {
                                return PRU_INVALID_DIR_PIN;
                        }
		}
	}

	if (PRU0_MODE == PRU_MODE_RX_ONLY )
	{
		pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_RX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}	
	else	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		chNum++;
	}	
	else
	{
		return PRU_MODE_INVALID;
	}

	/* Configuring the Transmit part of the given UART */
	if (configUart->RXSerializer != PRU_SUART_SERIALIZER_NONE) {
		/* Configuring RX serializer  */
		offset =
		    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CTRL_OFFSET;
		pru_ram_read_data(offset, (Uint8 *) & regVal, 2,
				  &pru_arm_iomap);

		regVal |=  (configUart->RXSerializer <<
				 PRU_SUART_CH_CTRL_SR_SHIFT);
		pru_ram_write_data(offset, (Uint8 *) & regVal, 2,
				   &pru_arm_iomap);

		/* Configuring RX prescalar value and Oversampling */
		offset =
		    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG1_OFFSET;
		pru_ram_read_data(offset, (Uint8 *) & regVal, 2,
				  &pru_arm_iomap);
		regVal =
		    regVal | (configUart->rxClkDivisor <<
			      PRU_SUART_CH_CONFIG1_DIVISOR_SHIFT) |
		    (configUart->Oversampling <<
		     PRU_SUART_CH_CONFIG1_OVS_SHIFT);
		pru_ram_write_data(offset, (Uint8 *) & regVal, 2,
				   &pru_arm_iomap);

		/* Configuring RX bits per character value */
		offset =
		    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG2_OFFSET;
		pru_ram_read_data(offset, (Uint8 *) & regVal, 2,
				  &pru_arm_iomap);
		regVal =
		    regVal | (configUart->rxBitsPerChar <<
			      PRU_SUART_CH_CONFIG1_DIVISOR_SHIFT);
		pru_ram_write_data(offset, (Uint8 *) & regVal, 2,
				   &pru_arm_iomap);
	}
	return (status);
}

/*
 * suart routine for getting the number of bytes transfered
 */
short pru_softuart_getTxDataLen(suart_handle hUart)
{
	unsigned int offset;
	unsigned int pruOffset;
	unsigned short chNum;
	unsigned short u16ReadValue = 0;

	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	chNum = hUart->uartNum - 1;
	
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;
	
		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
		}
	}	
	else if (PRU0_MODE == PRU_MODE_TX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_TX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}	

	/* Transmit channel number is (UartNum * 2) - 2  */

	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_CONFIG2_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & u16ReadValue, 2, &pru_arm_iomap);

	u16ReadValue = ((u16ReadValue & PRU_SUART_CH_CONFIG1_DIVISOR_MASK) >>
			PRU_SUART_CH_CONFIG2_DATALEN_SHIFT);
	return (u16ReadValue);
}

/*
 * suart routine for getting the number of bytes received
 */
short pru_softuart_getRxDataLen(suart_handle hUart)
{
	unsigned int offset;
	unsigned int pruOffset;
	unsigned short chNum;
	unsigned short u16ReadValue = 0;

	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	chNum = hUart->uartNum - 1;
	
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;
	
		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
		}
		
		chNum++;
	}	
	else if (PRU0_MODE == PRU_MODE_RX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_RX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}
	
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_CONFIG2_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & u16ReadValue, 2, &pru_arm_iomap);

	u16ReadValue = ((u16ReadValue & PRU_SUART_CH_CONFIG1_DIVISOR_MASK) >>
			PRU_SUART_CH_CONFIG2_DATALEN_SHIFT);

	return (u16ReadValue);
}

/*
 * suart routine to get the configuration information from a specific uart 
 */
short pru_softuart_getconfig(suart_handle hUart, suart_config * configUart)
{
	unsigned int offset;
	unsigned int pruOffset;
	unsigned short chNum;
	unsigned short regVal = 0;
	short status = SUART_SUCCESS;

	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	/*
	 * NOTE:
	 * Dependent baud rate for the given UART, the value MUST BE LESS THAN OR 
	 * EQUAL TO 64, preScalarValue <= 64
	 */

	chNum = hUart->uartNum - 1;
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

	
		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
		}
	}	
	else if (PRU0_MODE == PRU_MODE_TX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_TX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}

	/* Configuring the Transmit part of the given UART */
	/* Configuring TX serializer  */
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_CTRL_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);
	configUart->TXSerializer =
	    ((regVal & PRU_SUART_CH_CTRL_SR_MASK) >>
	     PRU_SUART_CH_CTRL_SR_SHIFT);

	/* Configuring TX prescalar value */
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_CONFIG1_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);
	configUart->txClkDivisor =
	    ((regVal & PRU_SUART_CH_CONFIG1_DIVISOR_MASK) >>
	     PRU_SUART_CH_CONFIG1_DIVISOR_SHIFT);

	/* Configuring TX bits per character value */
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_CONFIG2_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);
	configUart->txBitsPerChar =
	    ((regVal & PRU_SUART_CH_CONFIG1_DIVISOR_MASK) >>
	     PRU_SUART_CH_CONFIG1_DIVISOR_SHIFT);
		 
	if (PRU0_MODE == PRU_MODE_RX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_RX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}	
	else	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		chNum++;
	}	
	else
	{
		return PRU_MODE_INVALID;
	}

	/* Configuring the Transmit part of the given UART */
	/* Configuring RX serializer  */
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_CTRL_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);
	configUart->RXSerializer =
	    ((regVal & PRU_SUART_CH_CTRL_SR_MASK) >>
	     PRU_SUART_CH_CTRL_SR_SHIFT);

	/* Configuring RX prescalar value and Oversampling */
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_CONFIG1_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);
	configUart->rxClkDivisor =
	    ((regVal & PRU_SUART_CH_CONFIG1_DIVISOR_MASK) >>
	     PRU_SUART_CH_CONFIG1_DIVISOR_SHIFT);
	configUart->Oversampling =
	    ((regVal & PRU_SUART_CH_CONFIG1_OVS_MASK) >>
	     PRU_SUART_CH_CONFIG1_OVS_SHIFT);

	/* Configuring RX bits per character value */
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_CONFIG2_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);
	configUart->rxBitsPerChar =
	    ((regVal & PRU_SUART_CH_CONFIG1_DIVISOR_MASK) >>
	     PRU_SUART_CH_CONFIG1_DIVISOR_SHIFT);

	return (status);
}


int pru_softuart_pending_tx_request(void) 
{
    unsigned int offset = 0;
    unsigned int u32ISRValue = 0;
	
    if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
    {
		return SUART_SUCCESS;
    }
    else if (PRU0_MODE == PRU_MODE_TX_ONLY )
    {
        /* Read PRU Interrupt Status Register from PRU */
        offset =
            (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_STATCLRINT1 &
                                   0xFFFF);
        pru_ram_read_data_4byte(offset, (unsigned int *)&u32ISRValue, 1);

        if ((u32ISRValue & 0x1) == 0x1)
        {
            return PRU_SUART_FAILURE;
        }
    }
    else if (PRU1_MODE == PRU_MODE_TX_ONLY)
    {
        /* Read PRU Interrupt Status Register from PRU */
        offset =
            (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_STATCLRINT1 &
                                   0xFFFF);
        pru_ram_read_data_4byte(offset, (unsigned int *)&u32ISRValue, 1);

        if ((u32ISRValue & 0x2) == 0x2)
        {
            return PRU_SUART_FAILURE;
        }
    }
    else
    {
        return PRU_MODE_INVALID;
    }

    return SUART_SUCCESS;		
}	

/*
 * suart data transmit routine 
 */
short pru_softuart_write
    (suart_handle hUart, unsigned int *ptTxDataBuf, unsigned short dataLen) {
	unsigned int offset = 0;
	unsigned int pruOffset;
	short status = SUART_SUCCESS;
	unsigned short chNum;
	unsigned short regVal = 0;

	unsigned short pru_num;
	
	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	chNum = hUart->uartNum - 1;
	
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
	
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
			pru_num = hUart->uartNum;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
			pru_num = hUart->uartNum;
		}
	}	
	else if (PRU0_MODE == PRU_MODE_TX_ONLY )
	{
		pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		pru_num = 0;
	}		
	else if (PRU1_MODE == PRU_MODE_TX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
		pru_num = 1;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}

	/* Writing data length to SUART channel register */
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_CONFIG2_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);
	regVal &= ~PRU_SUART_CH_CONFIG2_DATALEN_MASK;
	regVal = regVal | (dataLen << PRU_SUART_CH_CONFIG2_DATALEN_SHIFT);
	pru_ram_write_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);

	/* Writing the data pointer to channel TX data pointer */
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_TXRXDATA_OFFSET;
	pru_ram_write_data(offset, (Uint8 *) ptTxDataBuf, 4, &pru_arm_iomap);

	/* Service Request to PRU */
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_CTRL_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);

	regVal &= ~(PRU_SUART_CH_CTRL_MODE_MASK |PRU_SUART_CH_CTRL_SREQ_MASK);

	regVal |= (PRU_SUART_CH_CTRL_TX_MODE << PRU_SUART_CH_CTRL_MODE_SHIFT) | 
			 (PRU_SUART_CH_CTRL_SREQ    <<    PRU_SUART_CH_CTRL_SREQ_SHIFT);

	pru_ram_write_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);

	/* generate ARM->PRU event */
	suart_arm_to_pru_intr(pru_num);

	return (status);
}

/*
 * suart data receive routine 
 */
short pru_softuart_read
    (suart_handle hUart, unsigned int *ptDataBuf, unsigned short dataLen) {
	unsigned int offset = 0;
	unsigned int pruOffset;
	short status = SUART_SUCCESS;
	unsigned short chNum;
	unsigned short regVal = 0;
	unsigned short pru_num;
	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	chNum = hUart->uartNum - 1;

	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
			pru_num = hUart->uartNum;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
			pru_num = hUart->uartNum;
		}
		chNum++;	
	}	
	else if (PRU0_MODE == PRU_MODE_RX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
			pru_num = 0;
	}		
	else if (PRU1_MODE == PRU_MODE_RX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
		pru_num = 1;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}

	/* Writing data length to SUART channel register */
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_CONFIG2_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);
	regVal &= ~PRU_SUART_CH_CONFIG2_DATALEN_MASK;
	regVal = regVal | (dataLen << PRU_SUART_CH_CONFIG2_DATALEN_SHIFT);
	pru_ram_write_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);

	/* Writing the data pointer to channel RX data pointer */
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_TXRXDATA_OFFSET;
	pru_ram_write_data(offset, (Uint8 *) ptDataBuf, 4, &pru_arm_iomap);

	/* Service Request to PRU */
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_CTRL_OFFSET;

	
	pru_ram_read_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);

	regVal &= ~(PRU_SUART_CH_CTRL_MODE_MASK |PRU_SUART_CH_CTRL_SREQ_MASK);

	regVal |=  ( PRU_SUART_CH_CTRL_RX_MODE << PRU_SUART_CH_CTRL_MODE_SHIFT) | 
				(PRU_SUART_CH_CTRL_SREQ << PRU_SUART_CH_CTRL_SREQ_SHIFT);
				
	pru_ram_write_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);
	
	/* enable the timeout interrupt */
	suart_intr_setmask (hUart->uartNum, PRU_RX_INTR, CHN_TXRX_IE_MASK_TIMEOUT);

	/* generate ARM->PRU event */
	suart_arm_to_pru_intr(pru_num);

	return (status);
}

/* 
 * suart routine to read the data from the RX FIFO
 */
short pru_softuart_read_data (
			suart_handle hUart, 
			Uint8 * pDataBuffer, 
			Int32 s32MaxLen, 
			Uint32 * pu32DataRead, 
			unsigned short u16Status,
			Uint32 u32DataRead,
			Uint32 u32DataLen,
			Uint8 * pu8SrcAddr,
			unsigned short u16ctrlReg)
{
	short retVal = PRU_SUART_SUCCESS;
	Uint32	u32CharLen = 0;
	Uint32  u32Parity = 0;
	unsigned int offset = 0;
	unsigned int pruOffset;
	unsigned short chNum;

	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	chNum = hUart->uartNum  - 1;
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
		}
		chNum++;
	}	
	else if (PRU0_MODE == PRU_MODE_RX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_RX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}
	
	/* read the character length */
	u32CharLen = u32DataLen & PRU_SUART_CH_CONFIG2_BITPERCHAR_MASK;

	/* read the parity value */
	u32Parity = u32DataLen & PRU_SUART_CH_CONFIG2_PARITY_MASK;
	
	/* Check if parity is enabled.  If set, subtract u32CharLen - 3. 
	   Else, subtract u32CharLen - 2.
	*/
	if( u32Parity > 0 )
	{
		u32CharLen -= 3; /* remove the START, PARITY, & STOP bit */
	}
	else
	{
		u32CharLen -= 2; /* remove the START & STOP bit */
	}
	
	u32DataLen &= PRU_SUART_CH_CONFIG2_DATALEN_MASK;
	u32DataLen = u32DataLen >> PRU_SUART_CH_CONFIG2_DATALEN_SHIFT;
	u32DataLen ++;	
	
	/* if the character length is greater than 8, then the size doubles */
	if (u32CharLen > 8)
	{
		u32DataLen *= 2;
	}
		
	/* Check if the time-out had occured. If, yes, then we need to find the 
	 * number of bytes read from PRU. Else, we need to read the requested bytes
	 */
	
	if (u16Status & (CHN_TXRX_STATUS_TIMEOUT | CHN_TXRX_STATUS_OVRNERR | 
		CHN_TXRX_STATUS_PE | CHN_TXRX_STATUS_FE | CHN_TXRX_STATUS_BI))
	{				 
		/* if the character length is greater than 8, then the size doubles */
		if (u32CharLen > 8)
		{
			u32DataRead *= 2;
		}
		
		/* the data corresponding is loaded in second half during the timeout */
		if (u32DataRead > u32DataLen)
		{
			u32DataRead -= u32DataLen;
			pu8SrcAddr +=  u32DataLen; 
		}
		
		pru_softuart_clrRxFifo (hUart, (unsigned short) u32DataRead, u16ctrlReg);
	}
	else
	{
		u32DataRead = u32DataLen;
		/* Determine the buffer index by reading the FIFO_OddEven flag*/
		if (u16Status & CHN_TXRX_STATUS_CMPLT)
		{
			/* if the bit is set, the data is in the first half of the FIFO else
			 * the data is in the second half
			 */
			pu8SrcAddr += u32DataLen;
		}
     }

	/* we should be copying only max len given by the application */
	if (u32DataRead > s32MaxLen)
	{
		u32DataRead = s32MaxLen;
	}
	
	/* evaluate the virtual address of the FIFO address based on the physical addr */
	pu8SrcAddr = (Uint8 *) ((Uint32) pu8SrcAddr - (Uint32) pru_arm_iomap.pFifoBufferPhysBase + 
							(Uint32) pru_arm_iomap.pFifoBufferVirtBase);
	
	/* Now we have both the data length and the source address. copy */
	for (offset = 0; offset < u32DataRead; offset++)
		{
		* pDataBuffer++ = * pu8SrcAddr++;
		}
	* pu32DataRead = u32DataRead;	
	
	retVal = PRU_SUART_SUCCESS;
	
	return (retVal);
}			

/*
 * suart routine to disable the receive functionality. This routine stops the PRU
 * from receiving on selected UART and also disables the McASP serializer corresponding
 * to this UART Rx line.
 */
short pru_softuart_stopReceive (suart_handle hUart)
{
	unsigned short status = SUART_SUCCESS;
	unsigned int offset;
	unsigned int pruOffset;
	unsigned short chNum;
	unsigned short u16Status;

	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	chNum = hUart->uartNum - 1;
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
	
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;	
	
		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
		}
		chNum++;
	}	
	else if (PRU0_MODE == PRU_MODE_RX_ONLY )
	{
		pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_RX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}
	
	/* read the existing value of status flag */
	offset = pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
			 PRU_SUART_CH_TXRXSTATUS_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) &u16Status, 1, &pru_arm_iomap);
	
	/* we need to clear the busy bit corresponding to this receive channel */
	u16Status &= ~(CHN_TXRX_STATUS_RDY);
	pru_ram_write_data(offset, (Uint8 *) & u16Status, 1, &pru_arm_iomap);
	
	/* get the serizlizer number being used for this Rx channel */
	offset = pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
			 PRU_SUART_CH_CTRL_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) &u16Status, 2, &pru_arm_iomap);
	u16Status &= PRU_SUART_CH_CTRL_SR_MASK;
	u16Status = u16Status >> PRU_SUART_CH_CTRL_SR_SHIFT;	
	
	/* we need to de-activate the serializer corresponding to this receive */
	status =  suart_asp_serializer_deactivate (u16Status, &pru_arm_iomap);
	
	return (status);

}

/*
 * suart routine to get the tx status for a specific uart 
 */
short pru_softuart_getTxStatus(suart_handle hUart)
{
	unsigned int offset;
	unsigned int pruOffset;
	unsigned short status = SUART_SUCCESS;
	unsigned short chNum;

	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	chNum = hUart->uartNum - 1 ;
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

	
		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
		}
	}	
	else if (PRU0_MODE == PRU_MODE_TX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_TX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}

	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_TXRXSTATUS_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & status, 1, &pru_arm_iomap);
	return (status);
}

short pru_softuart_clrTxStatus(suart_handle hUart)
{
	unsigned int offset;
	unsigned int pruOffset;
	unsigned short status = SUART_SUCCESS;
	unsigned short chNum;

	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}
	
	chNum = hUart->uartNum - 1 ;
	
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{

		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;
	
		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
		}
	}	
	else if (PRU0_MODE == PRU_MODE_TX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_TX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}
	
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_TXRXSTATUS_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & status, 1, &pru_arm_iomap);

	status &= ~(0x2);
	pru_ram_write_data(offset, (Uint8 *) & status, 1, &pru_arm_iomap);
	return (status);
}

/*
 * suart routine to get the rx status for a specific uart 
 */
short pru_softuart_getRxStatus(suart_handle hUart)
{
	unsigned int offset;
	unsigned int pruOffset;
	unsigned short status = SUART_SUCCESS;
	unsigned short chNum;

	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	chNum = hUart->uartNum - 1 ;
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;
	
		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
		}
		chNum++;
	}	
	else if (PRU0_MODE == PRU_MODE_RX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_RX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}
	
	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_TXRXSTATUS_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) &status, 1, &pru_arm_iomap);
	return (status);
}

/*
 * suart routine to get the Config2 register for a specific uart
 */
int pru_softuart_getRxFifoBytes(suart_handle hUart)
{
        unsigned int offset;
        unsigned int pruOffset;
	Uint32  u32DataRead = 0;        
        unsigned short chNum;

        if (hUart == NULL) {
                return (PRU_SUART_ERR_HANDLE_INVALID);
        }

        chNum = hUart->uartNum - 1 ;
        if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
        {
                /* channel starts from 0 and uart instance starts from 1 */
                chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

                if (hUart->uartNum <= 4)
                {
                        /* PRU0 */
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
                } else {
                        /* PRU1 */
                        pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
                        /* First 8 channel corresponds to PRU0 */
                        chNum -= 8;
                }
                chNum++;
        }
        else if (PRU0_MODE == PRU_MODE_RX_ONLY )
        {
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
        }
        else if (PRU1_MODE == PRU_MODE_RX_ONLY)
	{
                pruOffset = PRU_SUART_PRU1_CH0_OFFSET;

        }
        else
        {
                return PRU_MODE_INVALID;
        }

	offset = 
		pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
                                 PRU_SUART_CH_BYTESDONECNTR_OFFSET;
        pru_ram_read_data(offset, (Uint8 *) & u32DataRead, 1, &pru_arm_iomap);

        return (u32DataRead);
}

/*
 * suart routine to get the TXRX data pointer for a specific uart
 */
int pru_softuart_getRxDataPointer(suart_handle hUart)
{
        unsigned int offset;
        unsigned int pruOffset;
        Uint8 * pu8SrcAddr = NULL;
        unsigned short chNum;

        if (hUart == NULL) {
                return (PRU_SUART_ERR_HANDLE_INVALID);
        }

        chNum = hUart->uartNum - 1 ;
        if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
        {
                /* channel starts from 0 and uart instance starts from 1 */
                chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

                if (hUart->uartNum <= 4)
                {
                        /* PRU0 */
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
                } else {
                        /* PRU1 */
                        pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
                        /* First 8 channel corresponds to PRU0 */
                        chNum -= 8;
                }
                chNum++;
        }
        else if (PRU0_MODE == PRU_MODE_RX_ONLY )
        {
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
        }
        else if (PRU1_MODE == PRU_MODE_RX_ONLY)
        {
                pruOffset = PRU_SUART_PRU1_CH0_OFFSET;

        }
        else
        {
                return PRU_MODE_INVALID;
        }

        /* Get the data pointer from channel RX data pointer */
        offset =
            pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
            PRU_SUART_CH_TXRXDATA_OFFSET;
        pru_ram_read_data(offset, (Uint8 *) &pu8SrcAddr, 4, &pru_arm_iomap);

        return (pu8SrcAddr);
}

/*
 * suart routine to get the number of bytes in the FIFO for a specific uart
 */
int pru_softuart_getRxConfig2(suart_handle hUart)
{
        unsigned int offset;
        unsigned int pruOffset;
        Uint32  u32DataLen = 0;
        unsigned short chNum;

        if (hUart == NULL) {
                return (PRU_SUART_ERR_HANDLE_INVALID);
        }

        chNum = hUart->uartNum - 1 ;
        if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
        {
                /* channel starts from 0 and uart instance starts from 1 */
                chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

                if (hUart->uartNum <= 4)
                {
                        /* PRU0 */
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
                } else {
                        /* PRU1 */
                        pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
                        /* First 8 channel corresponds to PRU0 */
                        chNum -= 8;
                }
                chNum++;
        }
        else if (PRU0_MODE == PRU_MODE_RX_ONLY )
        {
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
        }
        else if (PRU1_MODE == PRU_MODE_RX_ONLY)
        {
                pruOffset = PRU_SUART_PRU1_CH0_OFFSET;

        }
        else
        {
                return PRU_MODE_INVALID;
        }

        /* Reading data length from SUART channel register */
        offset = pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
                         PRU_SUART_CH_CONFIG2_OFFSET;
        pru_ram_read_data(offset, (Uint8 *) & u32DataLen, 2, &pru_arm_iomap);

        return (u32DataLen);
}

int pru_softuart_getRxCntrlReg(suart_handle hUart)
{
        unsigned int offset;
        unsigned int pruOffset;
        unsigned short regVal;
	unsigned short chNum;

        if (hUart == NULL) {
                return (PRU_SUART_ERR_HANDLE_INVALID);
        }

        chNum = hUart->uartNum - 1 ;
        if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
        {
                /* channel starts from 0 and uart instance starts from 1 */
                chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

                if (hUart->uartNum <= 4)
                {
                        /* PRU0 */
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
                } else {
                        /* PRU1 */
                        pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
                        /* First 8 channel corresponds to PRU0 */
                        chNum -= 8;
                }
                chNum++;
        }
        else if (PRU0_MODE == PRU_MODE_RX_ONLY )
        {
                        pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
        }
        else if (PRU1_MODE == PRU_MODE_RX_ONLY)
        {
                pruOffset = PRU_SUART_PRU1_CH0_OFFSET;

        }

        else
        {
                return PRU_MODE_INVALID;
        }

        /* Reading data length from SUART channel register */
	offset = pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
                         PRU_SUART_CH_CTRL_OFFSET;

        pru_ram_read_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);

        return (regVal);
}

short pru_softuart_clrRxFifo(suart_handle hUart, unsigned short regVal, unsigned short cntrlReg)
{
	unsigned int offset;
	unsigned int pruOffset;
	unsigned short status = SUART_SUCCESS;
	unsigned short chNum;
//	unsigned short regVal;
	unsigned short uartNum;
	
	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	uartNum = hUart->uartNum;

	chNum = hUart->uartNum - 1;
	
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
	
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;
		
		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
		}
		chNum++;
	}	
	else if (PRU0_MODE == PRU_MODE_RX_ONLY )
	{
		pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		uartNum = 0;
	}		
	else if (PRU1_MODE == PRU_MODE_RX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
		uartNum = 1;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}

	/* Reset the number of bytes read into the FIFO */
      offset = pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
                        PRU_SUART_CH_BYTESDONECNTR_OFFSET;

//        pru_ram_read_data(offset, (Uint8 *) & regVal, 1, &pru_arm_iomap);
	regVal &= 0x00;
	
        pru_ram_write_data(offset, (Uint8 *) & regVal, 1, &pru_arm_iomap);

	/* Service Request to PRU */
	offset = pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
			 PRU_SUART_CH_CTRL_OFFSET;

//	pru_ram_read_data(offset, (Uint8 *) & regVal, 2, &pru_arm_iomap);

	cntrlReg &= ~(PRU_SUART_CH_CTRL_MODE_MASK |PRU_SUART_CH_CTRL_SREQ_MASK);

	cntrlReg |=  ( PRU_SUART_CH_CTRL_RX_MODE << PRU_SUART_CH_CTRL_MODE_SHIFT) | 
				(PRU_SUART_CH_CTRL_SREQ << PRU_SUART_CH_CTRL_SREQ_SHIFT);

	pru_ram_write_data(offset, (Uint8 *) & cntrlReg, 2, &pru_arm_iomap);
	suart_intr_setmask (hUart->uartNum, PRU_RX_INTR, CHN_TXRX_IE_MASK_TIMEOUT);
	
	/* generate ARM->PRU event */
	suart_arm_to_pru_intr(uartNum);

	return (status);
}


short pru_softuart_clrRxStatus(suart_handle hUart)
{
	unsigned int offset;
	unsigned int pruOffset;
	unsigned short status = SUART_SUCCESS;
	unsigned short chNum;

	if (hUart == NULL) {
		return (PRU_SUART_ERR_HANDLE_INVALID);
	}

	chNum = hUart->uartNum - 1;
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
	
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = (hUart->uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;
		
		if (hUart->uartNum <= 4) 
		{
			/* PRU0 */
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		} else {
			/* PRU1 */
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chNum -= 8;
		}
		chNum++;
	}	
	else if (PRU0_MODE == PRU_MODE_RX_ONLY )
	{
			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
	}		
	else if (PRU1_MODE == PRU_MODE_RX_ONLY)
	{
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}

	offset =
	    pruOffset + (chNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
	    PRU_SUART_CH_TXRXSTATUS_OFFSET;
	pru_ram_read_data(offset, (Uint8 *) & status, 1, &pru_arm_iomap);

	status &= ~(0xBC);
	pru_ram_write_data(offset, (Uint8 *) & status, 1, &pru_arm_iomap);
	return (status);
}

/*
 * suart_intr_status_read: Gets the Global Interrupt status register 
 * for the specified SUART.
 * uartNum < 1 to 6 >
 * txrxFlag < Indicates TX or RX interrupt for the uart >
 */
short pru_softuart_get_isrstatus(unsigned short uartNum, unsigned short *txrxFlag)
{
	unsigned int u32IntcOffset;
	unsigned int chNum = 0xFF;
	unsigned int regVal = 0;
	unsigned int u32RegVal = 0;
	unsigned int u32ISRValue = 0;
	unsigned int u32AckRegVal = 0;
	unsigned int  u32StatInxClrRegoffset = 0;
	
	/* initialize the status & Flag to known value */
	*txrxFlag = 0;
	
	u32StatInxClrRegoffset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_STATIDXCLR &
									   0xFFFF);
									   
	/* Read PRU Interrupt Status Register from PRU */
	u32IntcOffset =
        (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_STATCLRINT1 &
                               0xFFFF);

	pru_ram_read_data_4byte(u32IntcOffset, (unsigned int *)&u32ISRValue, 1);

	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{	
		/* channel starts from 0 and uart instance starts from 1 */
		chNum = uartNum * 2 - 2;

                /* Check if the interrupt occured for Tx */
                u32RegVal = PRU_SUART0_TX_EVT_BIT << ((uartNum - 1)* 2);
                if (u32ISRValue & u32RegVal)
                {
                        /* interupt occured for TX */
                        *txrxFlag |= PRU_TX_INTR;

                        /* acknowledge the RX interrupt  */
                        u32AckRegVal  = chNum + PRU_SUART0_TX_EVT;
                        pru_ram_write_data_4byte(u32StatInxClrRegoffset, (unsigned int *)&u32AckRegVal, 1);          

                }

                /* Check if the interrupt occured for Rx */
                u32RegVal = PRU_SUART0_RX_EVT_BIT << ((uartNum - 1)* 2);
                pru_ram_read_data_4byte(u32IntcOffset, (unsigned int *)&u32ISRValue, 1);
                if (u32ISRValue & u32RegVal)
                {
                        /* interupt occured for RX */
                        *txrxFlag |= PRU_RX_INTR;
                        chNum += 1;

                        /* acknowledge the RX interrupt  */
                        u32AckRegVal  = chNum + PRU_SUART0_TX_EVT;
                        pru_ram_write_data_4byte(u32StatInxClrRegoffset, (unsigned int *)&u32AckRegVal, 1);          
                }		
	}
	else
	{
		chNum = uartNum - 1;
		if ((u32ISRValue & 0x03FC) != 0)
		{
			/* PRU0 */
			u32RegVal = 1 << (uartNum + 1);
			if (u32ISRValue & u32RegVal)
			{
				/* acknowledge the interrupt  */
				u32AckRegVal  = chNum + PRU_SUART0_TX_EVT;
				pru_ram_write_data_4byte(u32StatInxClrRegoffset, (unsigned int *)&u32AckRegVal, 1);			
				*txrxFlag |= PRU_RX_INTR;
			}
		}	
	
		pru_ram_read_data_4byte(u32IntcOffset, (unsigned int *)&u32ISRValue, 1);
		if (u32ISRValue & 0x3FC00)
		{
			/* PRU1 */
			u32RegVal = 1 << (uartNum + 9);
			if (u32ISRValue & u32RegVal)
			{
				/* acknowledge the interrupt  */
				u32AckRegVal  = chNum + PRU_SUART4_TX_EVT;
				pru_ram_write_data_4byte(u32StatInxClrRegoffset, (unsigned int *)&u32AckRegVal, 1);			
                               *txrxFlag |= PRU_TX_INTR;
			}
		}
	
	}

	return regVal;
}

int pru_intr_clr_isrstatus(unsigned short uartNum, unsigned int txrxmode)
{
	unsigned int offset;
	unsigned short txrxFlag = 0;
	unsigned short chnNum;

	chnNum = uartNum - 1;
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* channel starts from 0 and uart instance starts from 1 */
		chnNum = (uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

		if (uartNum <= 4) 
		{
			/* PRU0 */
			offset = PRU_SUART_PRU0_ISR_OFFSET + 1;
		} else {
			/* PRU1 */
			offset = PRU_SUART_PRU1_ISR_OFFSET + 1;
			/* First 8 channel corresponds to PRU0 */
			chnNum -= 8;
		}
		
		if (2 == txrxmode)
			chnNum++;
	}	
	else if (PRU0_MODE == txrxmode)
	{
			offset = PRU_SUART_PRU0_ISR_OFFSET + 1;
	}		
	else if (PRU1_MODE == txrxmode)
	{
		offset = PRU_SUART_PRU1_ISR_OFFSET + 1;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}		
		
	pru_ram_read_data(offset, (Uint8 *) & txrxFlag, 1, &pru_arm_iomap);
	txrxFlag &= ~(0x2);
	pru_ram_write_data(offset, (Uint8 *) & txrxFlag, 1, &pru_arm_iomap);

	return 0;
}

short suart_arm_to_pru_intr(unsigned short uartNum)
{
	unsigned int u32offset;
	unsigned int u32value;
	short s16retval;
	
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		if ((uartNum > 0) && (uartNum <= 4)) {
			/* PRU0 SYS_EVT32 */
			u32value = 0x20;
		} else if ((uartNum > 4) && (uartNum <= 8)) {
			/* PRU1 SYS_EVT33 */
			u32value = 0x21;
		} else {
			return SUART_INVALID_UART_NUM;
		}
	}
	
	if ((PRU0_MODE == PRU_MODE_RX_ONLY) || (PRU1_MODE == PRU_MODE_RX_ONLY) || 
		(PRU0_MODE == PRU_MODE_TX_ONLY) || (PRU1_MODE == PRU_MODE_TX_ONLY))
	{
		if (uartNum == PRU_NUM0) 
		{
			/* PRU0 SYS_EVT32 */
			u32value = 0x20;
		}
	
		if (uartNum == PRU_NUM1) 
		{
			/* PRU0 SYS_EVT33 */
			u32value = 0x21;
		}
	}
	
	u32offset =
	    (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_STATIDXSET &
						       0xFFFF);
	s16retval =
	    pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
	if (s16retval == -1) {
		return -1;
	}
	return 0;
}

short arm_to_pru_intr_init(void)
{
	unsigned int u32offset;
	unsigned int u32value;
	unsigned int intOffset;
	short s16retval = -1;
#if 0
	/* Set the MCASP Event to PRU0 as Edge Triggered */
	u32offset =
        (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_TYPE0&
                        0xFFFF);
	u32value = 0x80000000;
    	s16retval =
    	pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
    	if (s16retval == -1) {
       		return -1;
    	}
#endif

    /* Set PRUSSEVTSEL = 1 */
    /* Unlock CFG priveledges by writing to KICK0R */
	__raw_writel(0x83E70B13, IO_ADDRESS(SYSCFGBASE + KICK0R_OFFSET));
    /* Unlock CFG priveledges by writing to KICK1R */
	__raw_writel(0x95A4F1E0, IO_ADDRESS(SYSCFGBASE + KICK1R_OFFSET));
    /* Set CFG3[3] */
	u32value = __raw_readl(IO_ADDRESS(SYSCFGBASE + CFGCHIP3_OFFSET));
       __raw_writel((u32value | 0x00000008), IO_ADDRESS(SYSCFGBASE + CFGCHIP3_OFFSET));
    /* Re-lock CFG priveledges by writing to KICK0R */
	__raw_writel(0x00000000, IO_ADDRESS(SYSCFGBASE + KICK0R_OFFSET));
    /* Re-lock CFG priveledges by writing to KICK1R */
	__raw_writel(0x00000000, IO_ADDRESS(SYSCFGBASE + KICK1R_OFFSET));

	/* Clear all the host interrupts */
    for (intOffset = 0; intOffset <= PRU_INTC_HOSTINTLVL_MAX; intOffset++)
    {
	    u32offset =
    	    (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_HSTINTENIDXCLR&
                               0xFFFF);
    	u32value = intOffset;
    	s16retval =
        	pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
    	if (s16retval == -1) {
        	return -1;
    	}
	}
	
	/* Enable the global interrupt */
	u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_GLBLEN &
						       0xFFFF);
	u32value = 0x1;
	s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
	if (s16retval == -1) {
		return -1;
	}

	/* Enable the Host interrupts for all host channels */
    for (intOffset = 0; intOffset <=PRU_INTC_HOSTINTLVL_MAX; intOffset++)
	{
		u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_HSTINTENIDXSET &
						       0xFFFF);
		u32value = intOffset;
		s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
		if (s16retval == -1) {
			return -1;
		}
	}

	/* host to channel mapping : Setting the host interrupt for channels 0,1,2,3 */
	u32offset =
	    (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_HOSTMAP0 &
						       0xFFFF);
	u32value = 0x03020100;
	s16retval =
	    pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
	if (-1 == s16retval) {
		return -1;
	}

	/* host to channel mapping : Setting the host interrupt for channels 4,5,6,7 */
	u32offset =
	    (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_HOSTMAP1 &
						       0xFFFF);
	u32value = 0x07060504;
	s16retval =
	    pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
	if (-1 == s16retval) {
		return -1;
	}

	/* host to channel mapping : Setting the host interrupt for channels 8,9 */
	u32offset =
	    (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_HOSTMAP2 &
						       0xFFFF);
	u32value = 0x00000908;
	s16retval =
	    pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
	if (-1 == s16retval) {
		return -1;
	}

       /* Set the channel for System intrrupts
        * MAP Channel 0 to SYS_EVT1
        * MAP Channel 0 to SYS_EVT2
        * MAP Channel 0 to SYS_EVT3
        */
       u32offset =
                (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_CHANMAP0 &
                                                        0xFFFF);
        u32value = 0x0000000000;
        s16retval =
                pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
        if (-1 == s16retval) {
                return -1;
                }

       /* Set the channel for System intrrupts
        * MAP Channel 0 to SYS_EVT4
        */
        u32offset =
                (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_CHANMAP1 &
                                                        0xFFFF);
        u32value = 0x0000000000;
        s16retval =
                pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
        if (-1 == s16retval) {
                return -1;
                }

    	/* Set the channel for System intrrupts
     	* MAP Channel 0 to SYS_EVT31 
     	*/
	u32offset =
		(unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_CHANMAP7 &
							0xFFFF);
	u32value = 0x0000000000;
	s16retval =
		pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
	if (-1 == s16retval) {
		return -1;
		}
    

	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* Sets the channel for the system interrupt 
		* MAP channel 0 to SYS_EVT32
		* MAP channel 1 to SYS_EVT33 
		* MAP channel 2 to SYS_EVT34  SUART0-Tx
		* MAP channel 2 to SYS_EVT35  SUART0-Rx
		*/
		u32offset =
			(unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_CHANMAP8 &
								0xFFFF);
		u32value = 0x02020100;
		s16retval =
			pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
		if (-1 == s16retval) {
			return -1;
		}

		/* Sets the channel for the system interrupt 
		* MAP channel 3 to SYS_EVT36	SUART1-Tx
		* MAP channel 3 to SYS_EVT37 	SUART1-Rx
		* MAP channel 4 to SYS_EVT38 	SUART2-Tx
		* MAP channel 4 to SYS_EVT39 	SUART2-Rx
		*/
		u32offset =
			(unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_CHANMAP9 &
								0xFFFF);
		u32value = 0x04040303;
		s16retval =
			pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
		if (-1 == s16retval) {
			return -1;
		}

		/* Sets the channel for the system interrupt 
		* MAP channel 5 to SYS_EVT40	SUART3-Tx
		* MAP channel 5 to SYS_EVT41 	SUART3-Rx
		* MAP channel 6 to SYS_EVT42 	SUART4-Tx
		* MAP channel 6 to SYS_EVT43 	SUART4-Rx
		*/
		u32offset =
			(unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_CHANMAP10 &
								0xFFFF);
		u32value = 0x06060505;
		s16retval =
			pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
		if (-1 == s16retval) {
			return -1;
		}

		/* Sets the channel for the system interrupt 
		* MAP channel 7 to SYS_EVT44	SUART5-Tx
		* MAP channel 7 to SYS_EVT45 	SUART5-Rx
		* MAP channel 8 to SYS_EVT46 	SUART6-Tx
		* MAP channel 8 to SYS_EVT47 	SUART6-Rx
		*/
		u32offset =
			(unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_CHANMAP11 &
						       0xFFFF);
		u32value = 0x08080707;
		s16retval =
			pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
		if (-1 == s16retval) {
			return -1;
		}

		/* Sets the channel for the system interrupt 
		* MAP channel 9 to SYS_EVT48	SUART7-Tx
		* MAP channel 9 to SYS_EVT49 	SUART7-Rx
		* MAP Channel 1 to SYS_EVT50
		*/
		u32offset =
			(unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_CHANMAP12 &
								0xFFFF);
		u32value = 0x00010909;
		s16retval =
			pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
		if (-1 == s16retval) {
			return -1;
		}
	}
	
	if ((PRU0_MODE == PRU_MODE_RX_ONLY) || (PRU1_MODE == PRU_MODE_RX_ONLY) || 
		(PRU0_MODE == PRU_MODE_TX_ONLY) || (PRU1_MODE == PRU_MODE_TX_ONLY))
	{
		/* Sets the channel for the system interrupt 
		* MAP channel 0 to SYS_EVT32
		* MAP channel 1 to SYS_EVT33 
		* MAP channel 2 to SYS_EVT34  SUART0
		* MAP channel 3 to SYS_EVT35  SUART1
		*/
		u32offset =
			(unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_CHANMAP8 &
								0xFFFF);
		u32value = 0x03020100;
		s16retval =
			pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
		if (-1 == s16retval) {
			return -1;
		}

		/* Sets the channel for the system interrupt 
		* MAP channel 4 to SYS_EVT36	SUART2
		* MAP channel 5 to SYS_EVT37 	SUART3
		* MAP channel 6 to SYS_EVT38 	SUART4
		* MAP channel 7 to SYS_EVT39 	SUART5
		*/
		u32offset =
			(unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_CHANMAP9 &
								0xFFFF);
		u32value = 0x07060504;
		s16retval =
			pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
		if (-1 == s16retval) {
			return -1;
		}

		/* Sets the channel for the system interrupt 
		* MAP channel 8 to SYS_EVT40	SUART6
		* MAP channel 9 to SYS_EVT41 	SUART7
		* MAP channel 2 to SYS_EVT42 	SUART0
		* MAP channel 3 to SYS_EVT43 	SUART1
		*/
		u32offset =
			(unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_CHANMAP10 &
								0xFFFF);
		u32value = 0x03020908;
		s16retval =
			pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
		if (-1 == s16retval) {
			return -1;
		}

		/* Sets the channel for the system interrupt 
		* MAP channel 4 to SYS_EVT44	SUART2
		* MAP channel 5 to SYS_EVT45 	SUART3
		* MAP channel 6 to SYS_EVT46 	SUART4
		* MAP channel 7 to SYS_EVT47 	SUART5
		*/
		u32offset =
			(unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_CHANMAP11 &
						       0xFFFF);
		u32value = 0x07060504;
		s16retval =
			pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
		if (-1 == s16retval) {
			return -1;
		}

		/* Sets the channel for the system interrupt 
		* MAP channel 8 to SYS_EVT48	SUART6
		* MAP channel 9 to SYS_EVT49 	SUART7
		* MAP Channel 1 to SYS_EVT50    PRU to PRU Intr
		*/
		u32offset =
			(unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_CHANMAP12 &
								0xFFFF);
		u32value = 0x00010908;
		s16retval =
			pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
		if (-1 == s16retval) {
			return -1;
		}
	}

	/* Set the Event Polarity as Active High*/
        u32offset =
        (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_POLARITY0&
                        0xFFFF);
        u32value = 0x0000FFFF;
        s16retval =
        pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
        if (s16retval == -1) {
                return -1;
        }
	
	/* Clear required set of system events and enable them using indexed register */
	for  (intOffset = 2; intOffset < 7; intOffset++)
        {
        u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_STATIDXCLR & 0xFFFF);
        u32value = intOffset;
        s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int *) &u32value, 1);
        if (s16retval == -1) {
                return -1;
        }
        }

	for  (intOffset = 0; intOffset < 18; intOffset++)
	{
    	u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_STATIDXCLR & 0xFFFF);
    	u32value = 32 + intOffset;
    	s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int *) &u32value, 1);
    	if (s16retval == -1) {
        	return -1;
    	}
		
	}
	/* enable only the HOST to PRU interrupts and let the PRU to Host events be
	 * enabled by the separate API on demand basis.
	 */

	if (PRU_SUART1_CONFIG_DIR_EN) {
		u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_ENIDXSET & 0xFFFF);
	        u32value = 2;
	        s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int*) &u32value, 1);
	        if (s16retval == -1) {
		        return -1;
	        }
	}

	if (PRU_SUART2_CONFIG_DIR_EN) {
                u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_ENIDXSET & 0xFFFF);
                u32value = 3;
                s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int*) &u32value, 1);
                if (s16retval == -1) {
                        return -1;
                }
        }

	if (PRU_SUART3_CONFIG_DIR_EN) {
                u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_ENIDXSET & 0xFFFF);
                u32value = 4;
                s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int*) &u32value, 1);
                if (s16retval == -1) {
                        return -1;
                }
        }

	if (PRU_SUART4_CONFIG_DIR_EN) {
                u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_ENIDXSET & 0xFFFF);
                u32value = 5;
                s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int*) &u32value, 1);
                if (s16retval == -1) {
                        return -1;
                }
        }

	u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_ENIDXSET & 0xFFFF);
	u32value = 31;
	s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int*) &u32value, 1);
	if (s16retval == -1) {
		return -1;
	}
	u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_ENIDXSET & 0xFFFF);
	u32value = 32;
	s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int*) &u32value, 1);
	if (s16retval == -1) {
		return -1;
	}
	u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_ENIDXSET & 0xFFFF);
	u32value = 33;
	s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int*) &u32value, 1);
	if (s16retval == -1) {
		return -1;
	}
	u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_ENIDXSET & 0xFFFF);
	u32value = 50;
	s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int*) &u32value, 1);
	if (s16retval == -1) {
		return -1;
	}

	/* Enable the global interrupt */
	u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_GLBLEN &
						       0xFFFF);
	u32value = 0x1;
	s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
	if (s16retval == -1) {
		return -1;
	}

	/* Enable the Host interrupts for all host channels */
    	for (intOffset = 0; intOffset <=PRU_INTC_HOSTINTLVL_MAX; intOffset++)
	{
		u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_HSTINTENIDXSET &
						       0xFFFF);
		u32value = intOffset;
		s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int *)&u32value, 1);
		if (s16retval == -1) {
			return -1;
		}
	}

	return 0;
}

int suart_pru_to_host_intr_enable (unsigned short uartNum,
		       unsigned int txrxmode, int s32Flag)
{
	int    retVal = 0;
	unsigned int u32offset;
	unsigned int chnNum;
	unsigned int u32value;
	short s16retval = 0;
	
	if (uartNum > 8) {
		return SUART_INVALID_UART_NUM;
	}

	chnNum = uartNum - 1;
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		chnNum = (uartNum * 2) - 2;
		if (2 == txrxmode) {	/* Rx mode */
			chnNum++;
		}
		u32value = 34 + chnNum;
	}
	else if ((PRU_MODE_RX_ONLY == txrxmode) && (PRU0_MODE == PRU_MODE_RX_ONLY))
			u32value = 34 + chnNum;
	else if ((PRU_MODE_RX_ONLY == txrxmode) && (PRU1_MODE == PRU_MODE_RX_ONLY))
			u32value = 42 + chnNum;	
	else if ((PRU_MODE_TX_ONLY == txrxmode) && (PRU0_MODE == PRU_MODE_TX_ONLY))
			u32value = 34 + chnNum;
	else if ((PRU_MODE_TX_ONLY == txrxmode) && (PRU1_MODE == PRU_MODE_TX_ONLY))
			u32value = 42 + chnNum;	
	else	
			return -1;

	if (TRUE == s32Flag)
	{
		u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_ENIDXSET & 0xFFFF);
		s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int*) &u32value, 1);
		if (s16retval == -1) {
			return -1;
		}
	}
	else
	{
		u32offset = (unsigned int)pru_arm_iomap.pru_io_addr | (PRU_INTC_ENIDXCLR & 0xFFFF);
		s16retval = pru_ram_write_data_4byte(u32offset, (unsigned int*) &u32value, 1);
		if (s16retval == -1) {
			return -1;
		}
	}
	return (retVal);
}

int suart_intr_setmask(unsigned short uartNum,
		       unsigned int txrxmode, unsigned int intrmask)
{
	unsigned int offset;
	unsigned int pruOffset;
	unsigned int txrxFlag;
	unsigned int regval = 0;
	unsigned int chnNum;

	chnNum = uartNum -1;
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* channel starts from 0 and uart instance starts from 1 */
		chnNum = (uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

		if ((uartNum > 0) && (uartNum <= 4)) {

			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
			offset = PRU_SUART_PRU0_IMR_OFFSET;
		} else if ((uartNum > 4) && (uartNum <= 8)) {
			/* PRU1 */
			offset = PRU_SUART_PRU1_IMR_OFFSET;
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chnNum -= 8;
		} else {
			return SUART_INVALID_UART_NUM;
		}

		if (2 == txrxmode) {	/* rx mode */
			chnNum++;
		}	
	}
	else if (PRU0_MODE == txrxmode)
	{
		pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		offset = PRU_SUART_PRU0_IMR_OFFSET;
	}		
	else if (PRU1_MODE == txrxmode)
	{
		offset = PRU_SUART_PRU1_IMR_OFFSET;
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}

	
	regval = 1 << chnNum;

	if (CHN_TXRX_IE_MASK_CMPLT == (intrmask & CHN_TXRX_IE_MASK_CMPLT)) 
	{
		pru_ram_read_data(offset, (Uint8 *) & txrxFlag, 2,
				  &pru_arm_iomap);
		txrxFlag &= ~(regval);
		txrxFlag |= regval;
		pru_ram_write_data(offset, (Uint8 *) & txrxFlag, 2,
				   &pru_arm_iomap);
	}

	if ((intrmask & SUART_GBL_INTR_ERR_MASK) == SUART_GBL_INTR_ERR_MASK) {
		regval = 0;
		pru_ram_read_data(offset, (Uint8 *) & regval, 2,
				  &pru_arm_iomap);
		regval &= ~(SUART_GBL_INTR_ERR_MASK);
		regval |= (SUART_GBL_INTR_ERR_MASK);
		pru_ram_write_data(offset, (Uint8 *) & regval, 2,
				   &pru_arm_iomap);

	}
	/* Framing Error Interrupt Masked */
	if ((intrmask & CHN_TXRX_IE_MASK_FE) == CHN_TXRX_IE_MASK_FE) {
		regval = 0;
		offset =
		    pruOffset + (chnNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG1_OFFSET;
		pru_ram_read_data(offset, (Uint8 *) & regval, 2,
				  &pru_arm_iomap);
		regval &= ~(CHN_TXRX_IE_MASK_FE);
		regval |= CHN_TXRX_IE_MASK_FE;
		pru_ram_write_data(offset, (Uint8 *) & regval, 2,
				   &pru_arm_iomap);
	}
	/* Break Indicator Interrupt Masked */
	if (CHN_TXRX_IE_MASK_BI == (intrmask & CHN_TXRX_IE_MASK_BI)) {
		regval = 0;
		offset =
		    pruOffset + (chnNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG1_OFFSET;

		pru_ram_read_data(offset, (Uint8 *) & regval, 2,
				  &pru_arm_iomap);
		regval &= ~(CHN_TXRX_IE_MASK_BI);
		regval |= CHN_TXRX_IE_MASK_BI;
		pru_ram_write_data(offset, (Uint8 *) & regval, 2,
				   &pru_arm_iomap);
	}
	/* Timeout error Interrupt Masked */
	if (CHN_TXRX_IE_MASK_TIMEOUT == (intrmask & CHN_TXRX_IE_MASK_TIMEOUT)) {
		regval = 0;
		offset =
		    pruOffset + (chnNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG1_OFFSET;

		pru_ram_read_data(offset, (Uint8 *) & regval, 2,
				  &pru_arm_iomap);
		regval &= ~(CHN_TXRX_IE_MASK_TIMEOUT);
		regval |= CHN_TXRX_IE_MASK_TIMEOUT;
		pru_ram_write_data(offset, (Uint8 *) & regval, 2,
				   &pru_arm_iomap);
	}

        /* Overrun error Interrupt Masked */
        if (CHN_RX_IE_MASK_OVRN == (intrmask & CHN_RX_IE_MASK_OVRN)) {
                regval = 0;
                offset =
                    pruOffset + (chnNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
                    PRU_SUART_CH_CONFIG1_OFFSET;

                pru_ram_read_data(offset, (Uint8 *) & regval, 2,
                                  &pru_arm_iomap);
                regval &= ~(CHN_RX_IE_MASK_OVRN);
                regval |= CHN_RX_IE_MASK_OVRN;
                pru_ram_write_data(offset, (Uint8 *) & regval, 2,
                                   &pru_arm_iomap);
        }

       /* Parity error Interrupt Masked */
        if (CHN_TXRX_IE_MASK_PE == (intrmask & CHN_TXRX_IE_MASK_PE)) {
                regval = 0;
                offset =
                    pruOffset + (chnNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
                    PRU_SUART_CH_TXRXSTATUS_OFFSET;

                pru_ram_read_data(offset, (Uint8 *) & regval, 2,
                                  &pru_arm_iomap);
                regval &= ~(CHN_TXRX_IE_MASK_PE);
                regval |= CHN_TXRX_IE_MASK_PE;
                pru_ram_write_data(offset, (Uint8 *) & regval, 2,
                                   &pru_arm_iomap);
        }
	return 0;
}

int suart_intr_clrmask(unsigned short uartNum,
		       unsigned int txrxmode, unsigned int intrmask)
{
	unsigned int offset;
	unsigned int pruOffset;
	unsigned short txrxFlag;
	unsigned short regval = 0;
	unsigned short chnNum;

	chnNum = uartNum -1;
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* channel starts from 0 and uart instance starts from 1 */
		chnNum = (uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

		if ((uartNum > 0) && (uartNum <= 4)) {

			pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
			offset = PRU_SUART_PRU0_IMR_OFFSET;
		} else if ((uartNum > 4) && (uartNum <= 8)) {
			/* PRU1 */
			offset = PRU_SUART_PRU1_IMR_OFFSET;
			pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chnNum -= 8;
		} else {
			return SUART_INVALID_UART_NUM;
		}

		if (2 == txrxmode) {	/* rx mode */
			chnNum++;
		}	
	}
	else if (PRU0_MODE == txrxmode) 
	{
		pruOffset = PRU_SUART_PRU0_CH0_OFFSET;
		offset = PRU_SUART_PRU0_IMR_OFFSET;
	}		
	else if (PRU1_MODE == txrxmode)
	{
		offset = PRU_SUART_PRU1_IMR_OFFSET;
		pruOffset = PRU_SUART_PRU1_CH0_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}
	
	regval = 1 << chnNum;

	if (CHN_TXRX_IE_MASK_CMPLT == (intrmask & CHN_TXRX_IE_MASK_CMPLT)) {
		pru_ram_read_data(offset, (Uint8 *) & txrxFlag, 2,
				  &pru_arm_iomap);
		txrxFlag &= ~(regval);
		pru_ram_write_data(offset, (Uint8 *) & txrxFlag, 2,
				   &pru_arm_iomap);
	}

	if ((intrmask & SUART_GBL_INTR_ERR_MASK) == SUART_GBL_INTR_ERR_MASK) {
		regval = 0;
		pru_ram_read_data(offset, (Uint8 *) & regval, 2,
				  &pru_arm_iomap);
		regval &= ~(SUART_GBL_INTR_ERR_MASK);
		pru_ram_write_data(offset, (Uint8 *) & regval, 2,
				   &pru_arm_iomap);

	}
	/* Framing Error Interrupt Masked */
	if ((intrmask & CHN_TXRX_IE_MASK_FE) == CHN_TXRX_IE_MASK_FE) {
		regval = 0;
		offset =
		    pruOffset + (chnNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG1_OFFSET;
		pru_ram_read_data(offset, (Uint8 *) & regval, 2,
				  &pru_arm_iomap);
		regval &= ~(CHN_TXRX_IE_MASK_FE);
		pru_ram_write_data(offset, (Uint8 *) & regval, 2,
				   &pru_arm_iomap);
	}
	/* Break Indicator Interrupt Masked */
	if (CHN_TXRX_IE_MASK_BI == (intrmask & CHN_TXRX_IE_MASK_BI)) {
		regval = 0;
		offset =
		    pruOffset + (chnNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG1_OFFSET;

		pru_ram_read_data(offset, (Uint8 *) & regval, 2,
				  &pru_arm_iomap);
		regval &= ~(CHN_TXRX_IE_MASK_BI);
		pru_ram_write_data(offset, (Uint8 *) & regval, 2,
				   &pru_arm_iomap);
	}
	
	/* Timeout error Interrupt Masked */
	if (CHN_TXRX_IE_MASK_TIMEOUT == (intrmask & CHN_TXRX_IE_MASK_TIMEOUT)) {
		regval = 0;
		offset =
		    pruOffset + (chnNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
		    PRU_SUART_CH_CONFIG1_OFFSET;

		pru_ram_read_data(offset, (Uint8 *) & regval, 2,
				  &pru_arm_iomap);
		regval &= ~(CHN_TXRX_IE_MASK_TIMEOUT);
		pru_ram_write_data(offset, (Uint8 *) & regval, 2,
				   &pru_arm_iomap);
	}

       /* Overrun error Interrupt Masked */
        if (CHN_RX_IE_MASK_OVRN == (intrmask & CHN_RX_IE_MASK_OVRN)) {
                regval = 0;
                offset =
                    pruOffset + (chnNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
                    PRU_SUART_CH_CONFIG1_OFFSET;

                pru_ram_read_data(offset, (Uint8 *) & regval, 2,
                                  &pru_arm_iomap);
                regval &= ~(CHN_RX_IE_MASK_OVRN);
                pru_ram_write_data(offset, (Uint8 *) & regval, 2,
                                   &pru_arm_iomap);
        }

	/* Parity error Interrupt Masked */
        if (CHN_TXRX_IE_MASK_PE == (intrmask & CHN_TXRX_IE_MASK_PE)) {
                regval = 0;
                offset =
                    pruOffset + (chnNum * SUART_NUM_OF_BYTES_PER_CHANNEL) +
                    PRU_SUART_CH_TXRXSTATUS_OFFSET;

                pru_ram_read_data(offset, (Uint8 *) & regval, 2,
                                  &pru_arm_iomap);
                regval &= ~(CHN_TXRX_IE_MASK_PE);
                pru_ram_write_data(offset, (Uint8 *) & regval, 2,
                                   &pru_arm_iomap);
        }
	
	return 0;
}

int suart_intr_getmask(unsigned short uartNum,
		       unsigned int txrxmode, unsigned int intrmask)
{
	unsigned short chnNum;
	unsigned int offset;
	unsigned short txrxFlag;
	unsigned short regval = 1;

	chnNum = uartNum -1;
	if ((PRU0_MODE == PRU_MODE_RX_TX_BOTH) || (PRU1_MODE == PRU_MODE_RX_TX_BOTH))
	{
		/* channel starts from 0 and uart instance starts from 1 */
		chnNum = (uartNum * SUART_NUM_OF_CHANNELS_PER_SUART) - 2;

		if ((uartNum > 0) && (uartNum <= 4)) {

			offset = PRU_SUART_PRU0_IMR_OFFSET;
		} else if ((uartNum > 4) && (uartNum <= 8)) {
			/* PRU1 */
			offset = PRU_SUART_PRU1_IMR_OFFSET;
			/* First 8 channel corresponds to PRU0 */
			chnNum -= 8;
		} else {
			return SUART_INVALID_UART_NUM;
		}

		if (2 == txrxmode) {	/* rx mode */
			chnNum++;
		}	
	}
	else if (PRU0_MODE == txrxmode) 
	{
		offset = PRU_SUART_PRU0_IMR_OFFSET;
	}		
	else if (PRU1_MODE == txrxmode) 
	{
		offset = PRU_SUART_PRU1_IMR_OFFSET;
	}
	else  
	{
		return PRU_MODE_INVALID;
	}

	regval = regval << chnNum;
	
	pru_ram_read_data(offset, (Uint8 *) & txrxFlag, 2, &pru_arm_iomap);
	txrxFlag &= regval;
	if (0 == intrmask) {
		if (txrxFlag == 0)
			return 1;
	}

	if (1 == intrmask) {
		if (txrxFlag == regval)
			return 1;
	}
	return 0;
}

static int suart_set_pru_id (unsigned int pru_no)
{
	unsigned int offset;
	unsigned short reg_val = 0;

	if (0 == pru_no)
	{	
		offset = PRU_SUART_PRU0_ID_ADDR;
	}
	else if (1 == pru_no)
	{
		offset = PRU_SUART_PRU1_ID_ADDR;	
	}
	else
    	{
		return PRU_SUART_FAILURE;
	}

	pru_ram_read_data(offset, (Uint8 *) & reg_val, 1, &pru_arm_iomap);
	reg_val &=~SUART_PRU_ID_MASK;
	reg_val = pru_no;
	pru_ram_write_data(offset, (Uint8 *) & reg_val, 1, &pru_arm_iomap);

	return PRU_SUART_SUCCESS;
}

void pru_screader_init(arm_pru_iomap * pru_arm_iomap)
{
	CSL_SyscfgRegsOvly SyscfgRegs = (CSL_SyscfgRegsOvly) pru_arm_iomap->syscfg_io_addr;
	CSL_GpioRegsOvly   Gpio       = (CSL_GpioRegsOvly) pru_arm_iomap->gpio_io_addr;
    CSL_ePWMRegsOvly   ePwm       = (CSL_ePWMRegsOvly) pru_arm_iomap->ehrpwm1_io_addr;
	Uint32		   TmpRegVal  = 0;

/* Set PRUSSEVTSEL = 1 */
    /* Unlock CFG priveledges by writing to KICK0R */
        __raw_writel(0x83E70B13, IO_ADDRESS(SYSCFGBASE + KICK0R_OFFSET));
    /* Unlock CFG priveledges by writing to KICK1R */
        __raw_writel(0x95A4F1E0, IO_ADDRESS(SYSCFGBASE + KICK1R_OFFSET));

    SyscfgRegs->CFGCHIP1 |= (CSL_SYSCFG_CFGCHIP1_TBCLKSYNC_ENABLE << CSL_SYSCFG_CFGCHIP1_TBCLKSYNC_SHIFT);

    ePwm->TBCTL   = 0x12B3;

    /* Parity */
    TmpRegVal  = SyscfgRegs->PINMUX5;
    TmpRegVal &= ~(CSL_SYSCFG_PINMUX5_PINMUX5_3_0_MASK);
    TmpRegVal |=  (CSL_SYSCFG_PINMUX5_PINMUX5_3_0_EPWM1A << CSL_SYSCFG_PINMUX5_PINMUX5_3_0_SHIFT);
    SyscfgRegs->PINMUX5 = TmpRegVal;

    /* Setup EPWM */
    ePwm->TBPHS   = 0x0;
    ePwm->TBCNT   = 0x0;
    ePwm->TBPRD   = 0x1A4;
    ePwm->CMPCTL  = 0x0;
    ePwm->AQCTLA  = 0x212;
    ePwm->CMPA    = 0x2B;
    ePwm->CMPB    = 0x77;
    ePwm->TBCTL   = 0x12B3;


	/* CARD RESET */
	TmpRegVal  = SyscfgRegs->PINMUX0;
	TmpRegVal &= ~(CSL_SYSCFG_PINMUX0_PINMUX0_19_16_MASK);
	TmpRegVal |=  (CSL_SYSCFG_PINMUX0_PINMUX0_19_16_GPIO0_11 << CSL_SYSCFG_PINMUX0_PINMUX0_19_16_SHIFT);
	SyscfgRegs->PINMUX0 = TmpRegVal;

	/* CARDDETECT */
	TmpRegVal  = SyscfgRegs->PINMUX1;
	TmpRegVal &= ~(CSL_SYSCFG_PINMUX1_PINMUX1_23_20_MASK);
	TmpRegVal |=  (CSL_SYSCFG_PINMUX1_PINMUX1_23_20_GPIO0_2 << CSL_SYSCFG_PINMUX1_PINMUX1_23_20_SHIFT);
	SyscfgRegs->PINMUX1 = TmpRegVal;

	/* VCCEN */
	TmpRegVal  = SyscfgRegs->PINMUX2;
	TmpRegVal &= ~(CSL_SYSCFG_PINMUX2_PINMUX2_3_0_MASK);
	TmpRegVal |=  (CSL_SYSCFG_PINMUX2_PINMUX2_3_0_GPIO1_15 << CSL_SYSCFG_PINMUX2_PINMUX2_3_0_SHIFT);
	SyscfgRegs->PINMUX2 = TmpRegVal;

	/* VPP */
	TmpRegVal  = SyscfgRegs->PINMUX0;
	TmpRegVal &= ~(CSL_SYSCFG_PINMUX0_PINMUX0_23_20_MASK);
	TmpRegVal |=  (CSL_SYSCFG_PINMUX0_PINMUX0_23_20_GPIO0_10 << CSL_SYSCFG_PINMUX0_PINMUX0_23_20_SHIFT);
	SyscfgRegs->PINMUX0 = TmpRegVal;

	    /* Re-lock CFG priveledges by writing to KICK0R */
        __raw_writel(0x00000000, IO_ADDRESS(SYSCFGBASE + KICK0R_OFFSET));
    /* Re-lock CFG priveledges by writing to KICK1R */
        __raw_writel(0x00000000, IO_ADDRESS(SYSCFGBASE + KICK1R_OFFSET));

	TmpRegVal  = Gpio->BANK[GP0].DIR;
    TmpRegVal &= ~(GP0P11);  // Set the RESET As out pin
	TmpRegVal &= ~(GP1P15);  // Set the VCCEN As out pin
	TmpRegVal &= ~(GP0P10);  // Set the VPP   As out pin
	TmpRegVal |=  (GP0P2);   // Set the CARD DETECT As Input pin
	Gpio->BANK[GP0].DIR = TmpRegVal;

	// Pull the VCC, VPP and Reset Down at the time of Initialisation
	Gpio->BANK[GP0].CLR_DATA = (GP0P11);
	Gpio->BANK[GP0].CLR_DATA = (GP1P15);
	Gpio->BANK[GP0].CLR_DATA = (GP0P10);

	return;
}

void pru_scrdr_powerup_card(arm_pru_iomap * pru_arm_iomap)
{
	CSL_GpioRegsOvly   Gpio = (CSL_GpioRegsOvly) pru_arm_iomap->gpio_io_addr;

	printk(KERN_DEBUG "%s : Smart Card Powered Up\n", __FUNCTION__);
	// RESET the Card
	Gpio->BANK[GP0].CLR_DATA = (GP0P11);
	mdelay(1000);
	// ENABLE VCC
	Gpio->BANK[GP0].SET_DATA = (GP1P15);  
	//wait for a while
	mdelay(1000);
	// Go out of RESET
	Gpio->BANK[GP0].SET_DATA = (GP0P11);  

	return;
}

void pru_scrdr_powerdown_card(arm_pru_iomap * pru_arm_iomap)
{
	CSL_GpioRegsOvly   Gpio = (CSL_GpioRegsOvly) pru_arm_iomap->gpio_io_addr;

	printk(KERN_DEBUG "%s : Smart Card Powered Down\n", __FUNCTION__);
	// RESET the Card
	Gpio->BANK[GP0].CLR_DATA = (GP0P11);
	//wait for a while
	mdelay(1000);
	// Disable VCC
	Gpio->BANK[GP0].CLR_DATA = (GP1P15);

	return;
}

void pru_scrdr_reset_card(arm_pru_iomap * pru_arm_iomap)
{
	CSL_GpioRegsOvly   Gpio = (CSL_GpioRegsOvly) pru_arm_iomap->gpio_io_addr;

	/* If Card is not powered up then power up the card */
	if(!(Gpio->BANK[GP0].IN_DATA & GP1P15)) {
		printk(KERN_DEBUG "%s : Smart Card Not Powered Up, So Powering Up now\n", __FUNCTION__);
		pru_scrdr_powerup_card(pru_arm_iomap);
		return;
	}
	
	/* If Card is powered up then just toggle the reset line alone */
	// Reset the Card
	
	printk(KERN_DEBUG "%s : Smart Card RESET Line Pulled LOW\n", __FUNCTION__);
	Gpio->BANK[GP0].CLR_DATA = (GP0P11);
	//wait for a while
	mdelay(1000);
	// Go out of Reset
	printk(KERN_DEBUG "%s : Smart Card RESET Line Pulled HIGH\n", __FUNCTION__);
	Gpio->BANK[GP0].SET_DATA = (GP0P11);

	return;
}

short pru_scrdr_iscard_present(arm_pru_iomap * pru_arm_iomap)
{
	CSL_GpioRegsOvly   Gpio = (CSL_GpioRegsOvly) pru_arm_iomap->gpio_io_addr;

	if(Gpio->BANK[GP0].IN_DATA & GP0P2) {
		printk(KERN_DEBUG "%s : Smart Card NOT PRESENT in slot\n", __FUNCTION__);
		return PRU_SUART_FAILURE;
	}

	printk(KERN_DEBUG "%s : Smart Card PRESENT in Slot\n", __FUNCTION__);
	return PRU_SUART_SUCCESS;
}

void pru_scrdr_enable_vcc(arm_pru_iomap * pru_arm_iomap)
{
   CSL_GpioRegsOvly   Gpio = (CSL_GpioRegsOvly) pru_arm_iomap->gpio_io_addr;

   printk(KERN_DEBUG "%s : Make VCC UP\n", __FUNCTION__);
   // ENABLE VCC
   Gpio->BANK[GP0].SET_DATA = (GP1P15);  
   mdelay(1000);

   return;
}

void pru_scrdr_enable_reset(arm_pru_iomap * pru_arm_iomap)
{
   CSL_GpioRegsOvly   Gpio = (CSL_GpioRegsOvly) pru_arm_iomap->gpio_io_addr;

   printk(KERN_DEBUG "%s : Make RESET UP\n", __FUNCTION__);
   Gpio->BANK[GP0].SET_DATA = (GP0P11);  

   return;
}

int pru_scrdr_isvccenabled(arm_pru_iomap * pru_arm_iomap)
{
   CSL_GpioRegsOvly   Gpio = (CSL_GpioRegsOvly) pru_arm_iomap->gpio_io_addr;

   if(Gpio->BANK[GP0].IN_DATA & GP1P15)
	   return PRU_SUART_SUCCESS;

   return PRU_SUART_FAILURE;
}
/* End of file */
