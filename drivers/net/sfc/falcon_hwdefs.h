/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_FALCON_HWDEFS_H
#define EFX_FALCON_HWDEFS_H

/*
 * Falcon hardware value definitions.
 * Falcon is the internal codename for the SFC4000 controller that is
 * present in SFE400X evaluation boards
 */

/**************************************************************************
 *
 * Falcon registers
 *
 **************************************************************************
 */

/* Address region register */
#define ADR_REGION_REG_KER	0x00
#define ADR_REGION0_LBN	0
#define ADR_REGION0_WIDTH	18
#define ADR_REGION1_LBN	32
#define ADR_REGION1_WIDTH	18
#define ADR_REGION2_LBN	64
#define ADR_REGION2_WIDTH	18
#define ADR_REGION3_LBN	96
#define ADR_REGION3_WIDTH	18

/* Interrupt enable register */
#define INT_EN_REG_KER 0x0010
#define KER_INT_KER_LBN 3
#define KER_INT_KER_WIDTH 1
#define DRV_INT_EN_KER_LBN 0
#define DRV_INT_EN_KER_WIDTH 1

/* Interrupt status address register */
#define INT_ADR_REG_KER	0x0030
#define NORM_INT_VEC_DIS_KER_LBN 64
#define NORM_INT_VEC_DIS_KER_WIDTH 1
#define INT_ADR_KER_LBN 0
#define INT_ADR_KER_WIDTH EFX_DMA_TYPE_WIDTH(64) /* not 46 for this one */

/* Interrupt status register (B0 only) */
#define INT_ISR0_B0 0x90
#define INT_ISR1_B0 0xA0

/* Interrupt acknowledge register (A0/A1 only) */
#define INT_ACK_REG_KER_A1 0x0050
#define INT_ACK_DUMMY_DATA_LBN 0
#define INT_ACK_DUMMY_DATA_WIDTH 32

/* Interrupt acknowledge work-around register (A0/A1 only )*/
#define WORK_AROUND_BROKEN_PCI_READS_REG_KER_A1 0x0070

/* SPI host command register */
#define EE_SPI_HCMD_REG_KER 0x0100
#define EE_SPI_HCMD_CMD_EN_LBN 31
#define EE_SPI_HCMD_CMD_EN_WIDTH 1
#define EE_WR_TIMER_ACTIVE_LBN 28
#define EE_WR_TIMER_ACTIVE_WIDTH 1
#define EE_SPI_HCMD_SF_SEL_LBN 24
#define EE_SPI_HCMD_SF_SEL_WIDTH 1
#define EE_SPI_EEPROM 0
#define EE_SPI_FLASH 1
#define EE_SPI_HCMD_DABCNT_LBN 16
#define EE_SPI_HCMD_DABCNT_WIDTH 5
#define EE_SPI_HCMD_READ_LBN 15
#define EE_SPI_HCMD_READ_WIDTH 1
#define EE_SPI_READ 1
#define EE_SPI_WRITE 0
#define EE_SPI_HCMD_DUBCNT_LBN 12
#define EE_SPI_HCMD_DUBCNT_WIDTH 2
#define EE_SPI_HCMD_ADBCNT_LBN 8
#define EE_SPI_HCMD_ADBCNT_WIDTH 2
#define EE_SPI_HCMD_ENC_LBN 0
#define EE_SPI_HCMD_ENC_WIDTH 8

/* SPI host address register */
#define EE_SPI_HADR_REG_KER 0x0110
#define EE_SPI_HADR_ADR_LBN 0
#define EE_SPI_HADR_ADR_WIDTH 24

/* SPI host data register */
#define EE_SPI_HDATA_REG_KER 0x0120

/* SPI/VPD config register */
#define EE_VPD_CFG_REG_KER 0x0140
#define EE_VPD_EN_LBN 0
#define EE_VPD_EN_WIDTH 1
#define EE_VPD_EN_AD9_MODE_LBN 1
#define EE_VPD_EN_AD9_MODE_WIDTH 1
#define EE_EE_CLOCK_DIV_LBN 112
#define EE_EE_CLOCK_DIV_WIDTH 7
#define EE_SF_CLOCK_DIV_LBN 120
#define EE_SF_CLOCK_DIV_WIDTH 7

/* PCIE CORE ACCESS REG */
#define PCIE_CORE_ADDR_PCIE_DEVICE_CTRL_STAT 0x68
#define PCIE_CORE_ADDR_PCIE_LINK_CTRL_STAT 0x70
#define PCIE_CORE_ADDR_ACK_RPL_TIMER 0x700
#define PCIE_CORE_ADDR_ACK_FREQ 0x70C

/* NIC status register */
#define NIC_STAT_REG 0x0200
#define EE_STRAP_EN_LBN 31
#define EE_STRAP_EN_WIDTH 1
#define EE_STRAP_OVR_LBN 24
#define EE_STRAP_OVR_WIDTH 4
#define ONCHIP_SRAM_LBN 16
#define ONCHIP_SRAM_WIDTH 1
#define SF_PRST_LBN 9
#define SF_PRST_WIDTH 1
#define EE_PRST_LBN 8
#define EE_PRST_WIDTH 1
#define STRAP_PINS_LBN 0
#define STRAP_PINS_WIDTH 3
/* These bit definitions are extrapolated from the list of numerical
 * values for STRAP_PINS.
 */
#define STRAP_10G_LBN 2
#define STRAP_10G_WIDTH 1
#define STRAP_PCIE_LBN 0
#define STRAP_PCIE_WIDTH 1

#define BOOTED_USING_NVDEVICE_LBN 3
#define BOOTED_USING_NVDEVICE_WIDTH 1

/* GPIO control register */
#define GPIO_CTL_REG_KER 0x0210
#define GPIO_USE_NIC_CLK_LBN (30)
#define GPIO_USE_NIC_CLK_WIDTH (1)
#define GPIO_OUTPUTS_LBN   (16)
#define GPIO_OUTPUTS_WIDTH (4)
#define GPIO_INPUTS_LBN (8)
#define GPIO_DIRECTION_LBN (24)
#define GPIO_DIRECTION_WIDTH (4)
#define GPIO_DIRECTION_OUT (1)
#define GPIO_SRAM_SLEEP (1 << 1)

#define GPIO3_OEN_LBN (GPIO_DIRECTION_LBN + 3)
#define	GPIO3_OEN_WIDTH 1
#define	GPIO2_OEN_LBN (GPIO_DIRECTION_LBN + 2)
#define	GPIO2_OEN_WIDTH 1
#define	GPIO1_OEN_LBN (GPIO_DIRECTION_LBN + 1)
#define	GPIO1_OEN_WIDTH 1
#define GPIO0_OEN_LBN (GPIO_DIRECTION_LBN + 0)
#define	GPIO0_OEN_WIDTH 1

#define	GPIO3_OUT_LBN (GPIO_OUTPUTS_LBN + 3)
#define	GPIO3_OUT_WIDTH 1
#define	GPIO2_OUT_LBN (GPIO_OUTPUTS_LBN + 2)
#define	GPIO2_OUT_WIDTH 1
#define	GPIO1_OUT_LBN (GPIO_OUTPUTS_LBN + 1)
#define	GPIO1_OUT_WIDTH 1
#define	GPIO0_OUT_LBN (GPIO_OUTPUTS_LBN + 0)
#define	GPIO0_OUT_WIDTH 1

#define GPIO3_IN_LBN (GPIO_INPUTS_LBN + 3)
#define	GPIO3_IN_WIDTH 1
#define	GPIO2_IN_WIDTH 1
#define	GPIO1_IN_WIDTH 1
#define GPIO0_IN_LBN (GPIO_INPUTS_LBN + 0)
#define	GPIO0_IN_WIDTH 1

/* Global control register */
#define GLB_CTL_REG_KER	0x0220
#define EXT_PHY_RST_CTL_LBN 63
#define EXT_PHY_RST_CTL_WIDTH 1
#define PCIE_SD_RST_CTL_LBN 61
#define PCIE_SD_RST_CTL_WIDTH 1

#define PCIE_NSTCK_RST_CTL_LBN 58
#define PCIE_NSTCK_RST_CTL_WIDTH 1
#define PCIE_CORE_RST_CTL_LBN 57
#define PCIE_CORE_RST_CTL_WIDTH 1
#define EE_RST_CTL_LBN 49
#define EE_RST_CTL_WIDTH 1
#define RST_XGRX_LBN 24
#define RST_XGRX_WIDTH 1
#define RST_XGTX_LBN 23
#define RST_XGTX_WIDTH 1
#define RST_EM_LBN 22
#define RST_EM_WIDTH 1
#define EXT_PHY_RST_DUR_LBN 1
#define EXT_PHY_RST_DUR_WIDTH 3
#define SWRST_LBN 0
#define SWRST_WIDTH 1
#define INCLUDE_IN_RESET 0
#define EXCLUDE_FROM_RESET 1

/* Fatal interrupt register */
#define FATAL_INTR_REG_KER 0x0230
#define RBUF_OWN_INT_KER_EN_LBN 39
#define RBUF_OWN_INT_KER_EN_WIDTH 1
#define TBUF_OWN_INT_KER_EN_LBN 38
#define TBUF_OWN_INT_KER_EN_WIDTH 1
#define ILL_ADR_INT_KER_EN_LBN 33
#define ILL_ADR_INT_KER_EN_WIDTH 1
#define MEM_PERR_INT_KER_LBN 8
#define MEM_PERR_INT_KER_WIDTH 1
#define INT_KER_ERROR_LBN 0
#define INT_KER_ERROR_WIDTH 12

#define DP_CTRL_REG 0x250
#define FLS_EVQ_ID_LBN 0
#define FLS_EVQ_ID_WIDTH 11

#define MEM_STAT_REG_KER 0x260

/* Debug probe register */
#define DEBUG_BLK_SEL_MISC 7
#define DEBUG_BLK_SEL_SERDES 6
#define DEBUG_BLK_SEL_EM 5
#define DEBUG_BLK_SEL_SR 4
#define DEBUG_BLK_SEL_EV 3
#define DEBUG_BLK_SEL_RX 2
#define DEBUG_BLK_SEL_TX 1
#define DEBUG_BLK_SEL_BIU 0

/* FPGA build version */
#define ALTERA_BUILD_REG_KER 0x0300
#define VER_ALL_LBN 0
#define VER_ALL_WIDTH 32

/* Spare EEPROM bits register (flash 0x390) */
#define SPARE_REG_KER 0x310
#define MEM_PERR_EN_TX_DATA_LBN 72
#define MEM_PERR_EN_TX_DATA_WIDTH 2

/* Timer table for kernel access */
#define TIMER_CMD_REG_KER 0x420
#define TIMER_MODE_LBN 12
#define TIMER_MODE_WIDTH 2
#define TIMER_MODE_DIS 0
#define TIMER_MODE_INT_HLDOFF 2
#define TIMER_VAL_LBN 0
#define TIMER_VAL_WIDTH 12

/* Driver generated event register */
#define DRV_EV_REG_KER 0x440
#define DRV_EV_QID_LBN 64
#define DRV_EV_QID_WIDTH 12
#define DRV_EV_DATA_LBN 0
#define DRV_EV_DATA_WIDTH 64

/* Buffer table configuration register */
#define BUF_TBL_CFG_REG_KER 0x600
#define BUF_TBL_MODE_LBN 3
#define BUF_TBL_MODE_WIDTH 1
#define BUF_TBL_MODE_HALF 0
#define BUF_TBL_MODE_FULL 1

/* SRAM receive descriptor cache configuration register */
#define SRM_RX_DC_CFG_REG_KER 0x610
#define SRM_RX_DC_BASE_ADR_LBN 0
#define SRM_RX_DC_BASE_ADR_WIDTH 21

/* SRAM transmit descriptor cache configuration register */
#define SRM_TX_DC_CFG_REG_KER 0x620
#define SRM_TX_DC_BASE_ADR_LBN 0
#define SRM_TX_DC_BASE_ADR_WIDTH 21

/* SRAM configuration register */
#define SRM_CFG_REG_KER 0x630
#define SRAM_OOB_BT_INIT_EN_LBN 3
#define SRAM_OOB_BT_INIT_EN_WIDTH 1
#define SRM_NUM_BANKS_AND_BANK_SIZE_LBN 0
#define SRM_NUM_BANKS_AND_BANK_SIZE_WIDTH 3
#define SRM_NB_BSZ_1BANKS_2M 0
#define SRM_NB_BSZ_1BANKS_4M 1
#define SRM_NB_BSZ_1BANKS_8M 2
#define SRM_NB_BSZ_DEFAULT 3 /* char driver will set the default */
#define SRM_NB_BSZ_2BANKS_4M 4
#define SRM_NB_BSZ_2BANKS_8M 5
#define SRM_NB_BSZ_2BANKS_16M 6
#define SRM_NB_BSZ_RESERVED 7

/* Special buffer table update register */
#define BUF_TBL_UPD_REG_KER 0x0650
#define BUF_UPD_CMD_LBN 63
#define BUF_UPD_CMD_WIDTH 1
#define BUF_CLR_CMD_LBN 62
#define BUF_CLR_CMD_WIDTH 1
#define BUF_CLR_END_ID_LBN 32
#define BUF_CLR_END_ID_WIDTH 20
#define BUF_CLR_START_ID_LBN 0
#define BUF_CLR_START_ID_WIDTH 20

/* Receive configuration register */
#define RX_CFG_REG_KER 0x800

/* B0 */
#define RX_INGR_EN_B0_LBN 47
#define RX_INGR_EN_B0_WIDTH 1
#define RX_DESC_PUSH_EN_B0_LBN 43
#define RX_DESC_PUSH_EN_B0_WIDTH 1
#define RX_XON_TX_TH_B0_LBN 33
#define RX_XON_TX_TH_B0_WIDTH 5
#define RX_XOFF_TX_TH_B0_LBN 28
#define RX_XOFF_TX_TH_B0_WIDTH 5
#define RX_USR_BUF_SIZE_B0_LBN 19
#define RX_USR_BUF_SIZE_B0_WIDTH 9
#define RX_XON_MAC_TH_B0_LBN 10
#define RX_XON_MAC_TH_B0_WIDTH 9
#define RX_XOFF_MAC_TH_B0_LBN 1
#define RX_XOFF_MAC_TH_B0_WIDTH 9
#define RX_XOFF_MAC_EN_B0_LBN 0
#define RX_XOFF_MAC_EN_B0_WIDTH 1

/* A1 */
#define RX_DESC_PUSH_EN_A1_LBN 35
#define RX_DESC_PUSH_EN_A1_WIDTH 1
#define RX_XON_TX_TH_A1_LBN 25
#define RX_XON_TX_TH_A1_WIDTH 5
#define RX_XOFF_TX_TH_A1_LBN 20
#define RX_XOFF_TX_TH_A1_WIDTH 5
#define RX_USR_BUF_SIZE_A1_LBN 11
#define RX_USR_BUF_SIZE_A1_WIDTH 9
#define RX_XON_MAC_TH_A1_LBN 6
#define RX_XON_MAC_TH_A1_WIDTH 5
#define RX_XOFF_MAC_TH_A1_LBN 1
#define RX_XOFF_MAC_TH_A1_WIDTH 5
#define RX_XOFF_MAC_EN_A1_LBN 0
#define RX_XOFF_MAC_EN_A1_WIDTH 1

/* Receive filter control register */
#define RX_FILTER_CTL_REG 0x810
#define UDP_FULL_SRCH_LIMIT_LBN 32
#define UDP_FULL_SRCH_LIMIT_WIDTH 8
#define NUM_KER_LBN 24
#define NUM_KER_WIDTH 2
#define UDP_WILD_SRCH_LIMIT_LBN 16
#define UDP_WILD_SRCH_LIMIT_WIDTH 8
#define TCP_WILD_SRCH_LIMIT_LBN 8
#define TCP_WILD_SRCH_LIMIT_WIDTH 8
#define TCP_FULL_SRCH_LIMIT_LBN 0
#define TCP_FULL_SRCH_LIMIT_WIDTH 8

/* RX queue flush register */
#define RX_FLUSH_DESCQ_REG_KER 0x0820
#define RX_FLUSH_DESCQ_CMD_LBN 24
#define RX_FLUSH_DESCQ_CMD_WIDTH 1
#define RX_FLUSH_DESCQ_LBN 0
#define RX_FLUSH_DESCQ_WIDTH 12

/* Receive descriptor update register */
#define RX_DESC_UPD_REG_KER_DWORD (0x830 + 12)
#define RX_DESC_WPTR_DWORD_LBN 0
#define RX_DESC_WPTR_DWORD_WIDTH 12

/* Receive descriptor cache configuration register */
#define RX_DC_CFG_REG_KER 0x840
#define RX_DC_SIZE_LBN 0
#define RX_DC_SIZE_WIDTH 2

#define RX_DC_PF_WM_REG_KER 0x850
#define RX_DC_PF_LWM_LBN 0
#define RX_DC_PF_LWM_WIDTH 6

/* RX no descriptor drop counter */
#define RX_NODESC_DROP_REG_KER 0x880
#define RX_NODESC_DROP_CNT_LBN 0
#define RX_NODESC_DROP_CNT_WIDTH 16

/* RX black magic register */
#define RX_SELF_RST_REG_KER 0x890
#define RX_ISCSI_DIS_LBN 17
#define RX_ISCSI_DIS_WIDTH 1
#define RX_NODESC_WAIT_DIS_LBN 9
#define RX_NODESC_WAIT_DIS_WIDTH 1
#define RX_RECOVERY_EN_LBN 8
#define RX_RECOVERY_EN_WIDTH 1

/* TX queue flush register */
#define TX_FLUSH_DESCQ_REG_KER 0x0a00
#define TX_FLUSH_DESCQ_CMD_LBN 12
#define TX_FLUSH_DESCQ_CMD_WIDTH 1
#define TX_FLUSH_DESCQ_LBN 0
#define TX_FLUSH_DESCQ_WIDTH 12

/* Transmit descriptor update register */
#define TX_DESC_UPD_REG_KER_DWORD (0xa10 + 12)
#define TX_DESC_WPTR_DWORD_LBN 0
#define TX_DESC_WPTR_DWORD_WIDTH 12

/* Transmit descriptor cache configuration register */
#define TX_DC_CFG_REG_KER 0xa20
#define TX_DC_SIZE_LBN 0
#define TX_DC_SIZE_WIDTH 2

/* Transmit checksum configuration register (A0/A1 only) */
#define TX_CHKSM_CFG_REG_KER_A1 0xa30

/* Transmit configuration register */
#define TX_CFG_REG_KER 0xa50
#define TX_NO_EOP_DISC_EN_LBN 5
#define TX_NO_EOP_DISC_EN_WIDTH 1

/* Transmit configuration register 2 */
#define TX_CFG2_REG_KER 0xa80
#define TX_CSR_PUSH_EN_LBN 89
#define TX_CSR_PUSH_EN_WIDTH 1
#define TX_RX_SPACER_LBN 64
#define TX_RX_SPACER_WIDTH 8
#define TX_SW_EV_EN_LBN 59
#define TX_SW_EV_EN_WIDTH 1
#define TX_RX_SPACER_EN_LBN 57
#define TX_RX_SPACER_EN_WIDTH 1
#define TX_PREF_THRESHOLD_LBN 19
#define TX_PREF_THRESHOLD_WIDTH 2
#define TX_ONE_PKT_PER_Q_LBN 18
#define TX_ONE_PKT_PER_Q_WIDTH 1
#define TX_DIS_NON_IP_EV_LBN 17
#define TX_DIS_NON_IP_EV_WIDTH 1
#define TX_FLUSH_MIN_LEN_EN_B0_LBN 7
#define TX_FLUSH_MIN_LEN_EN_B0_WIDTH 1

/* PHY management transmit data register */
#define MD_TXD_REG_KER 0xc00
#define MD_TXD_LBN 0
#define MD_TXD_WIDTH 16

/* PHY management receive data register */
#define MD_RXD_REG_KER 0xc10
#define MD_RXD_LBN 0
#define MD_RXD_WIDTH 16

/* PHY management configuration & status register */
#define MD_CS_REG_KER 0xc20
#define MD_GC_LBN 4
#define MD_GC_WIDTH 1
#define MD_RIC_LBN 2
#define MD_RIC_WIDTH 1
#define MD_RDC_LBN 1
#define MD_RDC_WIDTH 1
#define MD_WRC_LBN 0
#define MD_WRC_WIDTH 1

/* PHY management PHY address register */
#define MD_PHY_ADR_REG_KER 0xc30
#define MD_PHY_ADR_LBN 0
#define MD_PHY_ADR_WIDTH 16

/* PHY management ID register */
#define MD_ID_REG_KER 0xc40
#define MD_PRT_ADR_LBN 11
#define MD_PRT_ADR_WIDTH 5
#define MD_DEV_ADR_LBN 6
#define MD_DEV_ADR_WIDTH 5

/* PHY management status & mask register (DWORD read only) */
#define MD_STAT_REG_KER 0xc50
#define MD_BSERR_LBN 2
#define MD_BSERR_WIDTH 1
#define MD_LNFL_LBN 1
#define MD_LNFL_WIDTH 1
#define MD_BSY_LBN 0
#define MD_BSY_WIDTH 1

/* Port 0 and 1 MAC stats registers */
#define MAC0_STAT_DMA_REG_KER 0xc60
#define MAC_STAT_DMA_CMD_LBN 48
#define MAC_STAT_DMA_CMD_WIDTH 1
#define MAC_STAT_DMA_ADR_LBN 0
#define MAC_STAT_DMA_ADR_WIDTH EFX_DMA_TYPE_WIDTH(46)

/* Port 0 and 1 MAC control registers */
#define MAC0_CTRL_REG_KER 0xc80
#define MAC_XOFF_VAL_LBN 16
#define MAC_XOFF_VAL_WIDTH 16
#define TXFIFO_DRAIN_EN_B0_LBN 7
#define TXFIFO_DRAIN_EN_B0_WIDTH 1
#define MAC_BCAD_ACPT_LBN 4
#define MAC_BCAD_ACPT_WIDTH 1
#define MAC_UC_PROM_LBN 3
#define MAC_UC_PROM_WIDTH 1
#define MAC_LINK_STATUS_LBN 2
#define MAC_LINK_STATUS_WIDTH 1
#define MAC_SPEED_LBN 0
#define MAC_SPEED_WIDTH 2

/* 10G XAUI XGXS default values */
#define XX_TXDRV_DEQ_DEFAULT 0xe /* deq=.6 */
#define XX_TXDRV_DTX_DEFAULT 0x5 /* 1.25 */
#define XX_SD_CTL_DRV_DEFAULT 0  /* 20mA */

/* Multicast address hash table */
#define MAC_MCAST_HASH_REG0_KER 0xca0
#define MAC_MCAST_HASH_REG1_KER 0xcb0

/* GMAC configuration register 1 */
#define GM_CFG1_REG 0xe00
#define GM_SW_RST_LBN 31
#define GM_SW_RST_WIDTH 1
#define GM_LOOP_LBN 8
#define GM_LOOP_WIDTH 1
#define GM_RX_FC_EN_LBN 5
#define GM_RX_FC_EN_WIDTH 1
#define GM_TX_FC_EN_LBN 4
#define GM_TX_FC_EN_WIDTH 1
#define GM_RX_EN_LBN 2
#define GM_RX_EN_WIDTH 1
#define GM_TX_EN_LBN 0
#define GM_TX_EN_WIDTH 1

/* GMAC configuration register 2 */
#define GM_CFG2_REG 0xe10
#define GM_PAMBL_LEN_LBN 12
#define GM_PAMBL_LEN_WIDTH 4
#define GM_IF_MODE_LBN 8
#define GM_IF_MODE_WIDTH 2
#define GM_LEN_CHK_LBN 4
#define GM_LEN_CHK_WIDTH 1
#define GM_PAD_CRC_EN_LBN 2
#define GM_PAD_CRC_EN_WIDTH 1
#define GM_FD_LBN 0
#define GM_FD_WIDTH 1

/* GMAC maximum frame length register */
#define GM_MAX_FLEN_REG 0xe40
#define GM_MAX_FLEN_LBN 0
#define GM_MAX_FLEN_WIDTH 16

/* GMAC station address register 1 */
#define GM_ADR1_REG 0xf00
#define GM_HWADDR_5_LBN 24
#define GM_HWADDR_5_WIDTH 8
#define GM_HWADDR_4_LBN 16
#define GM_HWADDR_4_WIDTH 8
#define GM_HWADDR_3_LBN 8
#define GM_HWADDR_3_WIDTH 8
#define GM_HWADDR_2_LBN 0
#define GM_HWADDR_2_WIDTH 8

/* GMAC station address register 2 */
#define GM_ADR2_REG 0xf10
#define GM_HWADDR_1_LBN 24
#define GM_HWADDR_1_WIDTH 8
#define GM_HWADDR_0_LBN 16
#define GM_HWADDR_0_WIDTH 8

/* GMAC FIFO configuration register 0 */
#define GMF_CFG0_REG 0xf20
#define GMF_FTFENREQ_LBN 12
#define GMF_FTFENREQ_WIDTH 1
#define GMF_STFENREQ_LBN 11
#define GMF_STFENREQ_WIDTH 1
#define GMF_FRFENREQ_LBN 10
#define GMF_FRFENREQ_WIDTH 1
#define GMF_SRFENREQ_LBN 9
#define GMF_SRFENREQ_WIDTH 1
#define GMF_WTMENREQ_LBN 8
#define GMF_WTMENREQ_WIDTH 1

/* GMAC FIFO configuration register 1 */
#define GMF_CFG1_REG 0xf30
#define GMF_CFGFRTH_LBN 16
#define GMF_CFGFRTH_WIDTH 5
#define GMF_CFGXOFFRTX_LBN 0
#define GMF_CFGXOFFRTX_WIDTH 16

/* GMAC FIFO configuration register 2 */
#define GMF_CFG2_REG 0xf40
#define GMF_CFGHWM_LBN 16
#define GMF_CFGHWM_WIDTH 6
#define GMF_CFGLWM_LBN 0
#define GMF_CFGLWM_WIDTH 6

/* GMAC FIFO configuration register 3 */
#define GMF_CFG3_REG 0xf50
#define GMF_CFGHWMFT_LBN 16
#define GMF_CFGHWMFT_WIDTH 6
#define GMF_CFGFTTH_LBN 0
#define GMF_CFGFTTH_WIDTH 6

/* GMAC FIFO configuration register 4 */
#define GMF_CFG4_REG 0xf60
#define GMF_HSTFLTRFRM_PAUSE_LBN 12
#define GMF_HSTFLTRFRM_PAUSE_WIDTH 12

/* GMAC FIFO configuration register 5 */
#define GMF_CFG5_REG 0xf70
#define GMF_CFGHDPLX_LBN 22
#define GMF_CFGHDPLX_WIDTH 1
#define GMF_CFGBYTMODE_LBN 19
#define GMF_CFGBYTMODE_WIDTH 1
#define GMF_HSTDRPLT64_LBN 18
#define GMF_HSTDRPLT64_WIDTH 1
#define GMF_HSTFLTRFRMDC_PAUSE_LBN 12
#define GMF_HSTFLTRFRMDC_PAUSE_WIDTH 1

/* XGMAC address register low */
#define XM_ADR_LO_REG 0x1200
#define XM_ADR_3_LBN 24
#define XM_ADR_3_WIDTH 8
#define XM_ADR_2_LBN 16
#define XM_ADR_2_WIDTH 8
#define XM_ADR_1_LBN 8
#define XM_ADR_1_WIDTH 8
#define XM_ADR_0_LBN 0
#define XM_ADR_0_WIDTH 8

/* XGMAC address register high */
#define XM_ADR_HI_REG 0x1210
#define XM_ADR_5_LBN 8
#define XM_ADR_5_WIDTH 8
#define XM_ADR_4_LBN 0
#define XM_ADR_4_WIDTH 8

/* XGMAC global configuration */
#define XM_GLB_CFG_REG 0x1220
#define XM_RX_STAT_EN_LBN 11
#define XM_RX_STAT_EN_WIDTH 1
#define XM_TX_STAT_EN_LBN 10
#define XM_TX_STAT_EN_WIDTH 1
#define XM_RX_JUMBO_MODE_LBN 6
#define XM_RX_JUMBO_MODE_WIDTH 1
#define XM_INTCLR_MODE_LBN 3
#define XM_INTCLR_MODE_WIDTH 1
#define XM_CORE_RST_LBN 0
#define XM_CORE_RST_WIDTH 1

/* XGMAC transmit configuration */
#define XM_TX_CFG_REG 0x1230
#define XM_IPG_LBN 16
#define XM_IPG_WIDTH 4
#define XM_FCNTL_LBN 10
#define XM_FCNTL_WIDTH 1
#define XM_TXCRC_LBN 8
#define XM_TXCRC_WIDTH 1
#define XM_AUTO_PAD_LBN 5
#define XM_AUTO_PAD_WIDTH 1
#define XM_TX_PRMBL_LBN 2
#define XM_TX_PRMBL_WIDTH 1
#define XM_TXEN_LBN 1
#define XM_TXEN_WIDTH 1

/* XGMAC receive configuration */
#define XM_RX_CFG_REG 0x1240
#define XM_PASS_CRC_ERR_LBN 25
#define XM_PASS_CRC_ERR_WIDTH 1
#define XM_ACPT_ALL_MCAST_LBN 11
#define XM_ACPT_ALL_MCAST_WIDTH 1
#define XM_ACPT_ALL_UCAST_LBN 9
#define XM_ACPT_ALL_UCAST_WIDTH 1
#define XM_AUTO_DEPAD_LBN 8
#define XM_AUTO_DEPAD_WIDTH 1
#define XM_RXEN_LBN 1
#define XM_RXEN_WIDTH 1

/* XGMAC management interrupt mask register */
#define XM_MGT_INT_MSK_REG_B0 0x1250
#define XM_MSK_PRMBLE_ERR_LBN 2
#define XM_MSK_PRMBLE_ERR_WIDTH 1
#define XM_MSK_RMTFLT_LBN 1
#define XM_MSK_RMTFLT_WIDTH 1
#define XM_MSK_LCLFLT_LBN 0
#define XM_MSK_LCLFLT_WIDTH 1

/* XGMAC flow control register */
#define XM_FC_REG 0x1270
#define XM_PAUSE_TIME_LBN 16
#define XM_PAUSE_TIME_WIDTH 16
#define XM_DIS_FCNTL_LBN 0
#define XM_DIS_FCNTL_WIDTH 1

/* XGMAC pause time count register */
#define XM_PAUSE_TIME_REG 0x1290

/* XGMAC transmit parameter register */
#define XM_TX_PARAM_REG 0x012d0
#define XM_TX_JUMBO_MODE_LBN 31
#define XM_TX_JUMBO_MODE_WIDTH 1
#define XM_MAX_TX_FRM_SIZE_LBN 16
#define XM_MAX_TX_FRM_SIZE_WIDTH 14

/* XGMAC receive parameter register */
#define XM_RX_PARAM_REG 0x12e0
#define XM_MAX_RX_FRM_SIZE_LBN 0
#define XM_MAX_RX_FRM_SIZE_WIDTH 14

/* XGMAC management interrupt status register */
#define XM_MGT_INT_REG_B0 0x12f0
#define XM_PRMBLE_ERR 2
#define XM_PRMBLE_WIDTH 1
#define XM_RMTFLT_LBN 1
#define XM_RMTFLT_WIDTH 1
#define XM_LCLFLT_LBN 0
#define XM_LCLFLT_WIDTH 1

/* XGXS/XAUI powerdown/reset register */
#define XX_PWR_RST_REG 0x1300

#define XX_PWRDND_EN_LBN 15
#define XX_PWRDND_EN_WIDTH 1
#define XX_PWRDNC_EN_LBN 14
#define XX_PWRDNC_EN_WIDTH 1
#define XX_PWRDNB_EN_LBN 13
#define XX_PWRDNB_EN_WIDTH 1
#define XX_PWRDNA_EN_LBN 12
#define XX_PWRDNA_EN_WIDTH 1
#define XX_RSTPLLCD_EN_LBN 9
#define XX_RSTPLLCD_EN_WIDTH 1
#define XX_RSTPLLAB_EN_LBN 8
#define XX_RSTPLLAB_EN_WIDTH 1
#define XX_RESETD_EN_LBN 7
#define XX_RESETD_EN_WIDTH 1
#define XX_RESETC_EN_LBN 6
#define XX_RESETC_EN_WIDTH 1
#define XX_RESETB_EN_LBN 5
#define XX_RESETB_EN_WIDTH 1
#define XX_RESETA_EN_LBN 4
#define XX_RESETA_EN_WIDTH 1
#define XX_RSTXGXSRX_EN_LBN 2
#define XX_RSTXGXSRX_EN_WIDTH 1
#define XX_RSTXGXSTX_EN_LBN 1
#define XX_RSTXGXSTX_EN_WIDTH 1
#define XX_RST_XX_EN_LBN 0
#define XX_RST_XX_EN_WIDTH 1

/* XGXS/XAUI powerdown/reset control register */
#define XX_SD_CTL_REG 0x1310
#define XX_HIDRVD_LBN 15
#define XX_HIDRVD_WIDTH 1
#define XX_LODRVD_LBN 14
#define XX_LODRVD_WIDTH 1
#define XX_HIDRVC_LBN 13
#define XX_HIDRVC_WIDTH 1
#define XX_LODRVC_LBN 12
#define XX_LODRVC_WIDTH 1
#define XX_HIDRVB_LBN 11
#define XX_HIDRVB_WIDTH 1
#define XX_LODRVB_LBN 10
#define XX_LODRVB_WIDTH 1
#define XX_HIDRVA_LBN 9
#define XX_HIDRVA_WIDTH 1
#define XX_LODRVA_LBN 8
#define XX_LODRVA_WIDTH 1
#define XX_LPBKD_LBN 3
#define XX_LPBKD_WIDTH 1
#define XX_LPBKC_LBN 2
#define XX_LPBKC_WIDTH 1
#define XX_LPBKB_LBN 1
#define XX_LPBKB_WIDTH 1
#define XX_LPBKA_LBN 0
#define XX_LPBKA_WIDTH 1

#define XX_TXDRV_CTL_REG 0x1320
#define XX_DEQD_LBN 28
#define XX_DEQD_WIDTH 4
#define XX_DEQC_LBN 24
#define XX_DEQC_WIDTH 4
#define XX_DEQB_LBN 20
#define XX_DEQB_WIDTH 4
#define XX_DEQA_LBN 16
#define XX_DEQA_WIDTH 4
#define XX_DTXD_LBN 12
#define XX_DTXD_WIDTH 4
#define XX_DTXC_LBN 8
#define XX_DTXC_WIDTH 4
#define XX_DTXB_LBN 4
#define XX_DTXB_WIDTH 4
#define XX_DTXA_LBN 0
#define XX_DTXA_WIDTH 4

/* XAUI XGXS core status register */
#define XX_CORE_STAT_REG 0x1360
#define XX_FORCE_SIG_LBN 24
#define XX_FORCE_SIG_WIDTH 8
#define XX_FORCE_SIG_DECODE_FORCED 0xff
#define XX_XGXS_LB_EN_LBN 23
#define XX_XGXS_LB_EN_WIDTH 1
#define XX_XGMII_LB_EN_LBN 22
#define XX_XGMII_LB_EN_WIDTH 1
#define XX_ALIGN_DONE_LBN 20
#define XX_ALIGN_DONE_WIDTH 1
#define XX_SYNC_STAT_LBN 16
#define XX_SYNC_STAT_WIDTH 4
#define XX_SYNC_STAT_DECODE_SYNCED 0xf
#define XX_COMMA_DET_LBN 12
#define XX_COMMA_DET_WIDTH 4
#define XX_COMMA_DET_DECODE_DETECTED 0xf
#define XX_COMMA_DET_RESET 0xf
#define XX_CHARERR_LBN 4
#define XX_CHARERR_WIDTH 4
#define XX_CHARERR_RESET 0xf
#define XX_DISPERR_LBN 0
#define XX_DISPERR_WIDTH 4
#define XX_DISPERR_RESET 0xf

/* Receive filter table */
#define RX_FILTER_TBL0 0xF00000

/* Receive descriptor pointer table */
#define RX_DESC_PTR_TBL_KER_A1 0x11800
#define RX_DESC_PTR_TBL_KER_B0 0xF40000
#define RX_DESC_PTR_TBL_KER_P0 0x900
#define RX_ISCSI_DDIG_EN_LBN 88
#define RX_ISCSI_DDIG_EN_WIDTH 1
#define RX_ISCSI_HDIG_EN_LBN 87
#define RX_ISCSI_HDIG_EN_WIDTH 1
#define RX_DESCQ_BUF_BASE_ID_LBN 36
#define RX_DESCQ_BUF_BASE_ID_WIDTH 20
#define RX_DESCQ_EVQ_ID_LBN 24
#define RX_DESCQ_EVQ_ID_WIDTH 12
#define RX_DESCQ_OWNER_ID_LBN 10
#define RX_DESCQ_OWNER_ID_WIDTH 14
#define RX_DESCQ_LABEL_LBN 5
#define RX_DESCQ_LABEL_WIDTH 5
#define RX_DESCQ_SIZE_LBN 3
#define RX_DESCQ_SIZE_WIDTH 2
#define RX_DESCQ_SIZE_4K 3
#define RX_DESCQ_SIZE_2K 2
#define RX_DESCQ_SIZE_1K 1
#define RX_DESCQ_SIZE_512 0
#define RX_DESCQ_TYPE_LBN 2
#define RX_DESCQ_TYPE_WIDTH 1
#define RX_DESCQ_JUMBO_LBN 1
#define RX_DESCQ_JUMBO_WIDTH 1
#define RX_DESCQ_EN_LBN 0
#define RX_DESCQ_EN_WIDTH 1

/* Transmit descriptor pointer table */
#define TX_DESC_PTR_TBL_KER_A1 0x11900
#define TX_DESC_PTR_TBL_KER_B0 0xF50000
#define TX_DESC_PTR_TBL_KER_P0 0xa40
#define TX_NON_IP_DROP_DIS_B0_LBN 91
#define TX_NON_IP_DROP_DIS_B0_WIDTH 1
#define TX_IP_CHKSM_DIS_B0_LBN 90
#define TX_IP_CHKSM_DIS_B0_WIDTH 1
#define TX_TCP_CHKSM_DIS_B0_LBN 89
#define TX_TCP_CHKSM_DIS_B0_WIDTH 1
#define TX_DESCQ_EN_LBN 88
#define TX_DESCQ_EN_WIDTH 1
#define TX_ISCSI_DDIG_EN_LBN 87
#define TX_ISCSI_DDIG_EN_WIDTH 1
#define TX_ISCSI_HDIG_EN_LBN 86
#define TX_ISCSI_HDIG_EN_WIDTH 1
#define TX_DESCQ_BUF_BASE_ID_LBN 36
#define TX_DESCQ_BUF_BASE_ID_WIDTH 20
#define TX_DESCQ_EVQ_ID_LBN 24
#define TX_DESCQ_EVQ_ID_WIDTH 12
#define TX_DESCQ_OWNER_ID_LBN 10
#define TX_DESCQ_OWNER_ID_WIDTH 14
#define TX_DESCQ_LABEL_LBN 5
#define TX_DESCQ_LABEL_WIDTH 5
#define TX_DESCQ_SIZE_LBN 3
#define TX_DESCQ_SIZE_WIDTH 2
#define TX_DESCQ_SIZE_4K 3
#define TX_DESCQ_SIZE_2K 2
#define TX_DESCQ_SIZE_1K 1
#define TX_DESCQ_SIZE_512 0
#define TX_DESCQ_TYPE_LBN 1
#define TX_DESCQ_TYPE_WIDTH 2

/* Event queue pointer */
#define EVQ_PTR_TBL_KER_A1 0x11a00
#define EVQ_PTR_TBL_KER_B0 0xf60000
#define EVQ_PTR_TBL_KER_P0 0x500
#define EVQ_EN_LBN 23
#define EVQ_EN_WIDTH 1
#define EVQ_SIZE_LBN 20
#define EVQ_SIZE_WIDTH 3
#define EVQ_SIZE_32K 6
#define EVQ_SIZE_16K 5
#define EVQ_SIZE_8K 4
#define EVQ_SIZE_4K 3
#define EVQ_SIZE_2K 2
#define EVQ_SIZE_1K 1
#define EVQ_SIZE_512 0
#define EVQ_BUF_BASE_ID_LBN 0
#define EVQ_BUF_BASE_ID_WIDTH 20

/* Event queue read pointer */
#define EVQ_RPTR_REG_KER_A1 0x11b00
#define EVQ_RPTR_REG_KER_B0 0xfa0000
#define EVQ_RPTR_REG_KER_DWORD (EVQ_RPTR_REG_KER + 0)
#define EVQ_RPTR_DWORD_LBN 0
#define EVQ_RPTR_DWORD_WIDTH 14

/* RSS indirection table */
#define RX_RSS_INDIR_TBL_B0 0xFB0000
#define RX_RSS_INDIR_ENT_B0_LBN 0
#define RX_RSS_INDIR_ENT_B0_WIDTH 6

/* Special buffer descriptors (full-mode) */
#define BUF_FULL_TBL_KER_A1 0x8000
#define BUF_FULL_TBL_KER_B0 0x800000
#define IP_DAT_BUF_SIZE_LBN 50
#define IP_DAT_BUF_SIZE_WIDTH 1
#define IP_DAT_BUF_SIZE_8K 1
#define IP_DAT_BUF_SIZE_4K 0
#define BUF_ADR_REGION_LBN 48
#define BUF_ADR_REGION_WIDTH 2
#define BUF_ADR_FBUF_LBN 14
#define BUF_ADR_FBUF_WIDTH 34
#define BUF_OWNER_ID_FBUF_LBN 0
#define BUF_OWNER_ID_FBUF_WIDTH 14

/* Transmit descriptor */
#define TX_KER_PORT_LBN 63
#define TX_KER_PORT_WIDTH 1
#define TX_KER_CONT_LBN 62
#define TX_KER_CONT_WIDTH 1
#define TX_KER_BYTE_CNT_LBN 48
#define TX_KER_BYTE_CNT_WIDTH 14
#define TX_KER_BUF_REGION_LBN 46
#define TX_KER_BUF_REGION_WIDTH 2
#define TX_KER_BUF_REGION0_DECODE 0
#define TX_KER_BUF_REGION1_DECODE 1
#define TX_KER_BUF_REGION2_DECODE 2
#define TX_KER_BUF_REGION3_DECODE 3
#define TX_KER_BUF_ADR_LBN 0
#define TX_KER_BUF_ADR_WIDTH EFX_DMA_TYPE_WIDTH(46)

/* Receive descriptor */
#define RX_KER_BUF_SIZE_LBN 48
#define RX_KER_BUF_SIZE_WIDTH 14
#define RX_KER_BUF_REGION_LBN 46
#define RX_KER_BUF_REGION_WIDTH 2
#define RX_KER_BUF_REGION0_DECODE 0
#define RX_KER_BUF_REGION1_DECODE 1
#define RX_KER_BUF_REGION2_DECODE 2
#define RX_KER_BUF_REGION3_DECODE 3
#define RX_KER_BUF_ADR_LBN 0
#define RX_KER_BUF_ADR_WIDTH EFX_DMA_TYPE_WIDTH(46)

/**************************************************************************
 *
 * Falcon events
 *
 **************************************************************************
 */

/* Event queue entries */
#define EV_CODE_LBN 60
#define EV_CODE_WIDTH 4
#define RX_IP_EV_DECODE 0
#define TX_IP_EV_DECODE 2
#define DRIVER_EV_DECODE 5
#define GLOBAL_EV_DECODE 6
#define DRV_GEN_EV_DECODE 7
#define WHOLE_EVENT_LBN 0
#define WHOLE_EVENT_WIDTH 64

/* Receive events */
#define RX_EV_PKT_OK_LBN 56
#define RX_EV_PKT_OK_WIDTH 1
#define RX_EV_PAUSE_FRM_ERR_LBN 55
#define RX_EV_PAUSE_FRM_ERR_WIDTH 1
#define RX_EV_BUF_OWNER_ID_ERR_LBN 54
#define RX_EV_BUF_OWNER_ID_ERR_WIDTH 1
#define RX_EV_IF_FRAG_ERR_LBN 53
#define RX_EV_IF_FRAG_ERR_WIDTH 1
#define RX_EV_IP_HDR_CHKSUM_ERR_LBN 52
#define RX_EV_IP_HDR_CHKSUM_ERR_WIDTH 1
#define RX_EV_TCP_UDP_CHKSUM_ERR_LBN 51
#define RX_EV_TCP_UDP_CHKSUM_ERR_WIDTH 1
#define RX_EV_ETH_CRC_ERR_LBN 50
#define RX_EV_ETH_CRC_ERR_WIDTH 1
#define RX_EV_FRM_TRUNC_LBN 49
#define RX_EV_FRM_TRUNC_WIDTH 1
#define RX_EV_DRIB_NIB_LBN 48
#define RX_EV_DRIB_NIB_WIDTH 1
#define RX_EV_TOBE_DISC_LBN 47
#define RX_EV_TOBE_DISC_WIDTH 1
#define RX_EV_PKT_TYPE_LBN 44
#define RX_EV_PKT_TYPE_WIDTH 3
#define RX_EV_PKT_TYPE_ETH_DECODE 0
#define RX_EV_PKT_TYPE_LLC_DECODE 1
#define RX_EV_PKT_TYPE_JUMBO_DECODE 2
#define RX_EV_PKT_TYPE_VLAN_DECODE 3
#define RX_EV_PKT_TYPE_VLAN_LLC_DECODE 4
#define RX_EV_PKT_TYPE_VLAN_JUMBO_DECODE 5
#define RX_EV_HDR_TYPE_LBN 42
#define RX_EV_HDR_TYPE_WIDTH 2
#define RX_EV_HDR_TYPE_TCP_IPV4_DECODE 0
#define RX_EV_HDR_TYPE_UDP_IPV4_DECODE 1
#define RX_EV_HDR_TYPE_OTHER_IP_DECODE 2
#define RX_EV_HDR_TYPE_NON_IP_DECODE 3
#define RX_EV_HDR_TYPE_HAS_CHECKSUMS(hdr_type) \
	((hdr_type) <= RX_EV_HDR_TYPE_UDP_IPV4_DECODE)
#define RX_EV_MCAST_HASH_MATCH_LBN 40
#define RX_EV_MCAST_HASH_MATCH_WIDTH 1
#define RX_EV_MCAST_PKT_LBN 39
#define RX_EV_MCAST_PKT_WIDTH 1
#define RX_EV_Q_LABEL_LBN 32
#define RX_EV_Q_LABEL_WIDTH 5
#define RX_EV_JUMBO_CONT_LBN 31
#define RX_EV_JUMBO_CONT_WIDTH 1
#define RX_EV_BYTE_CNT_LBN 16
#define RX_EV_BYTE_CNT_WIDTH 14
#define RX_EV_SOP_LBN 15
#define RX_EV_SOP_WIDTH 1
#define RX_EV_DESC_PTR_LBN 0
#define RX_EV_DESC_PTR_WIDTH 12

/* Transmit events */
#define TX_EV_PKT_ERR_LBN 38
#define TX_EV_PKT_ERR_WIDTH 1
#define TX_EV_Q_LABEL_LBN 32
#define TX_EV_Q_LABEL_WIDTH 5
#define TX_EV_WQ_FF_FULL_LBN 15
#define TX_EV_WQ_FF_FULL_WIDTH 1
#define TX_EV_COMP_LBN 12
#define TX_EV_COMP_WIDTH 1
#define TX_EV_DESC_PTR_LBN 0
#define TX_EV_DESC_PTR_WIDTH 12

/* Driver events */
#define DRIVER_EV_SUB_CODE_LBN 56
#define DRIVER_EV_SUB_CODE_WIDTH 4
#define DRIVER_EV_SUB_DATA_LBN 0
#define DRIVER_EV_SUB_DATA_WIDTH 14
#define TX_DESCQ_FLS_DONE_EV_DECODE 0
#define RX_DESCQ_FLS_DONE_EV_DECODE 1
#define EVQ_INIT_DONE_EV_DECODE 2
#define EVQ_NOT_EN_EV_DECODE 3
#define RX_DESCQ_FLSFF_OVFL_EV_DECODE 4
#define SRM_UPD_DONE_EV_DECODE 5
#define WAKE_UP_EV_DECODE 6
#define TX_PKT_NON_TCP_UDP_DECODE 9
#define TIMER_EV_DECODE 10
#define RX_RECOVERY_EV_DECODE 11
#define RX_DSC_ERROR_EV_DECODE 14
#define TX_DSC_ERROR_EV_DECODE 15
#define DRIVER_EV_TX_DESCQ_ID_LBN 0
#define DRIVER_EV_TX_DESCQ_ID_WIDTH 12
#define DRIVER_EV_RX_FLUSH_FAIL_LBN 12
#define DRIVER_EV_RX_FLUSH_FAIL_WIDTH 1
#define DRIVER_EV_RX_DESCQ_ID_LBN 0
#define DRIVER_EV_RX_DESCQ_ID_WIDTH 12
#define SRM_CLR_EV_DECODE 0
#define SRM_UPD_EV_DECODE 1
#define SRM_ILLCLR_EV_DECODE 2

/* Global events */
#define RX_RECOVERY_B0_LBN 12
#define RX_RECOVERY_B0_WIDTH 1
#define XG_MNT_INTR_B0_LBN 11
#define XG_MNT_INTR_B0_WIDTH 1
#define RX_RECOVERY_A1_LBN 11
#define RX_RECOVERY_A1_WIDTH 1
#define XFP_PHY_INTR_LBN 10
#define XFP_PHY_INTR_WIDTH 1
#define XG_PHY_INTR_LBN 9
#define XG_PHY_INTR_WIDTH 1
#define G_PHY1_INTR_LBN 8
#define G_PHY1_INTR_WIDTH 1
#define G_PHY0_INTR_LBN 7
#define G_PHY0_INTR_WIDTH 1

/* Driver-generated test events */
#define EVQ_MAGIC_LBN 0
#define EVQ_MAGIC_WIDTH 32

/**************************************************************************
 *
 * Falcon MAC stats
 *
 **************************************************************************
 *
 */

#define GRxGoodOct_offset 0x0
#define GRxGoodOct_WIDTH 48
#define GRxBadOct_offset 0x8
#define GRxBadOct_WIDTH 48
#define GRxMissPkt_offset 0x10
#define GRxMissPkt_WIDTH 32
#define GRxFalseCRS_offset 0x14
#define GRxFalseCRS_WIDTH 32
#define GRxPausePkt_offset 0x18
#define GRxPausePkt_WIDTH 32
#define GRxBadPkt_offset 0x1C
#define GRxBadPkt_WIDTH 32
#define GRxUcastPkt_offset 0x20
#define GRxUcastPkt_WIDTH 32
#define GRxMcastPkt_offset 0x24
#define GRxMcastPkt_WIDTH 32
#define GRxBcastPkt_offset 0x28
#define GRxBcastPkt_WIDTH 32
#define GRxGoodLt64Pkt_offset 0x2C
#define GRxGoodLt64Pkt_WIDTH 32
#define GRxBadLt64Pkt_offset 0x30
#define GRxBadLt64Pkt_WIDTH 32
#define GRx64Pkt_offset 0x34
#define GRx64Pkt_WIDTH 32
#define GRx65to127Pkt_offset 0x38
#define GRx65to127Pkt_WIDTH 32
#define GRx128to255Pkt_offset 0x3C
#define GRx128to255Pkt_WIDTH 32
#define GRx256to511Pkt_offset 0x40
#define GRx256to511Pkt_WIDTH 32
#define GRx512to1023Pkt_offset 0x44
#define GRx512to1023Pkt_WIDTH 32
#define GRx1024to15xxPkt_offset 0x48
#define GRx1024to15xxPkt_WIDTH 32
#define GRx15xxtoJumboPkt_offset 0x4C
#define GRx15xxtoJumboPkt_WIDTH 32
#define GRxGtJumboPkt_offset 0x50
#define GRxGtJumboPkt_WIDTH 32
#define GRxFcsErr64to15xxPkt_offset 0x54
#define GRxFcsErr64to15xxPkt_WIDTH 32
#define GRxFcsErr15xxtoJumboPkt_offset 0x58
#define GRxFcsErr15xxtoJumboPkt_WIDTH 32
#define GRxFcsErrGtJumboPkt_offset 0x5C
#define GRxFcsErrGtJumboPkt_WIDTH 32
#define GTxGoodBadOct_offset 0x80
#define GTxGoodBadOct_WIDTH 48
#define GTxGoodOct_offset 0x88
#define GTxGoodOct_WIDTH 48
#define GTxSglColPkt_offset 0x90
#define GTxSglColPkt_WIDTH 32
#define GTxMultColPkt_offset 0x94
#define GTxMultColPkt_WIDTH 32
#define GTxExColPkt_offset 0x98
#define GTxExColPkt_WIDTH 32
#define GTxDefPkt_offset 0x9C
#define GTxDefPkt_WIDTH 32
#define GTxLateCol_offset 0xA0
#define GTxLateCol_WIDTH 32
#define GTxExDefPkt_offset 0xA4
#define GTxExDefPkt_WIDTH 32
#define GTxPausePkt_offset 0xA8
#define GTxPausePkt_WIDTH 32
#define GTxBadPkt_offset 0xAC
#define GTxBadPkt_WIDTH 32
#define GTxUcastPkt_offset 0xB0
#define GTxUcastPkt_WIDTH 32
#define GTxMcastPkt_offset 0xB4
#define GTxMcastPkt_WIDTH 32
#define GTxBcastPkt_offset 0xB8
#define GTxBcastPkt_WIDTH 32
#define GTxLt64Pkt_offset 0xBC
#define GTxLt64Pkt_WIDTH 32
#define GTx64Pkt_offset 0xC0
#define GTx64Pkt_WIDTH 32
#define GTx65to127Pkt_offset 0xC4
#define GTx65to127Pkt_WIDTH 32
#define GTx128to255Pkt_offset 0xC8
#define GTx128to255Pkt_WIDTH 32
#define GTx256to511Pkt_offset 0xCC
#define GTx256to511Pkt_WIDTH 32
#define GTx512to1023Pkt_offset 0xD0
#define GTx512to1023Pkt_WIDTH 32
#define GTx1024to15xxPkt_offset 0xD4
#define GTx1024to15xxPkt_WIDTH 32
#define GTx15xxtoJumboPkt_offset 0xD8
#define GTx15xxtoJumboPkt_WIDTH 32
#define GTxGtJumboPkt_offset 0xDC
#define GTxGtJumboPkt_WIDTH 32
#define GTxNonTcpUdpPkt_offset 0xE0
#define GTxNonTcpUdpPkt_WIDTH 16
#define GTxMacSrcErrPkt_offset 0xE4
#define GTxMacSrcErrPkt_WIDTH 16
#define GTxIpSrcErrPkt_offset 0xE8
#define GTxIpSrcErrPkt_WIDTH 16
#define GDmaDone_offset 0xEC
#define GDmaDone_WIDTH 32

#define XgRxOctets_offset 0x0
#define XgRxOctets_WIDTH 48
#define XgRxOctetsOK_offset 0x8
#define XgRxOctetsOK_WIDTH 48
#define XgRxPkts_offset 0x10
#define XgRxPkts_WIDTH 32
#define XgRxPktsOK_offset 0x14
#define XgRxPktsOK_WIDTH 32
#define XgRxBroadcastPkts_offset 0x18
#define XgRxBroadcastPkts_WIDTH 32
#define XgRxMulticastPkts_offset 0x1C
#define XgRxMulticastPkts_WIDTH 32
#define XgRxUnicastPkts_offset 0x20
#define XgRxUnicastPkts_WIDTH 32
#define XgRxUndersizePkts_offset 0x24
#define XgRxUndersizePkts_WIDTH 32
#define XgRxOversizePkts_offset 0x28
#define XgRxOversizePkts_WIDTH 32
#define XgRxJabberPkts_offset 0x2C
#define XgRxJabberPkts_WIDTH 32
#define XgRxUndersizeFCSerrorPkts_offset 0x30
#define XgRxUndersizeFCSerrorPkts_WIDTH 32
#define XgRxDropEvents_offset 0x34
#define XgRxDropEvents_WIDTH 32
#define XgRxFCSerrorPkts_offset 0x38
#define XgRxFCSerrorPkts_WIDTH 32
#define XgRxAlignError_offset 0x3C
#define XgRxAlignError_WIDTH 32
#define XgRxSymbolError_offset 0x40
#define XgRxSymbolError_WIDTH 32
#define XgRxInternalMACError_offset 0x44
#define XgRxInternalMACError_WIDTH 32
#define XgRxControlPkts_offset 0x48
#define XgRxControlPkts_WIDTH 32
#define XgRxPausePkts_offset 0x4C
#define XgRxPausePkts_WIDTH 32
#define XgRxPkts64Octets_offset 0x50
#define XgRxPkts64Octets_WIDTH 32
#define XgRxPkts65to127Octets_offset 0x54
#define XgRxPkts65to127Octets_WIDTH 32
#define XgRxPkts128to255Octets_offset 0x58
#define XgRxPkts128to255Octets_WIDTH 32
#define XgRxPkts256to511Octets_offset 0x5C
#define XgRxPkts256to511Octets_WIDTH 32
#define XgRxPkts512to1023Octets_offset 0x60
#define XgRxPkts512to1023Octets_WIDTH 32
#define XgRxPkts1024to15xxOctets_offset 0x64
#define XgRxPkts1024to15xxOctets_WIDTH 32
#define XgRxPkts15xxtoMaxOctets_offset 0x68
#define XgRxPkts15xxtoMaxOctets_WIDTH 32
#define XgRxLengthError_offset 0x6C
#define XgRxLengthError_WIDTH 32
#define XgTxPkts_offset 0x80
#define XgTxPkts_WIDTH 32
#define XgTxOctets_offset 0x88
#define XgTxOctets_WIDTH 48
#define XgTxMulticastPkts_offset 0x90
#define XgTxMulticastPkts_WIDTH 32
#define XgTxBroadcastPkts_offset 0x94
#define XgTxBroadcastPkts_WIDTH 32
#define XgTxUnicastPkts_offset 0x98
#define XgTxUnicastPkts_WIDTH 32
#define XgTxControlPkts_offset 0x9C
#define XgTxControlPkts_WIDTH 32
#define XgTxPausePkts_offset 0xA0
#define XgTxPausePkts_WIDTH 32
#define XgTxPkts64Octets_offset 0xA4
#define XgTxPkts64Octets_WIDTH 32
#define XgTxPkts65to127Octets_offset 0xA8
#define XgTxPkts65to127Octets_WIDTH 32
#define XgTxPkts128to255Octets_offset 0xAC
#define XgTxPkts128to255Octets_WIDTH 32
#define XgTxPkts256to511Octets_offset 0xB0
#define XgTxPkts256to511Octets_WIDTH 32
#define XgTxPkts512to1023Octets_offset 0xB4
#define XgTxPkts512to1023Octets_WIDTH 32
#define XgTxPkts1024to15xxOctets_offset 0xB8
#define XgTxPkts1024to15xxOctets_WIDTH 32
#define XgTxPkts1519toMaxOctets_offset 0xBC
#define XgTxPkts1519toMaxOctets_WIDTH 32
#define XgTxUndersizePkts_offset 0xC0
#define XgTxUndersizePkts_WIDTH 32
#define XgTxOversizePkts_offset 0xC4
#define XgTxOversizePkts_WIDTH 32
#define XgTxNonTcpUdpPkt_offset 0xC8
#define XgTxNonTcpUdpPkt_WIDTH 16
#define XgTxMacSrcErrPkt_offset 0xCC
#define XgTxMacSrcErrPkt_WIDTH 16
#define XgTxIpSrcErrPkt_offset 0xD0
#define XgTxIpSrcErrPkt_WIDTH 16
#define XgDmaDone_offset 0xD4

#define FALCON_STATS_NOT_DONE 0x00000000
#define FALCON_STATS_DONE 0xffffffff

/* Interrupt status register bits */
#define FATAL_INT_LBN 64
#define FATAL_INT_WIDTH 1
#define INT_EVQS_LBN 40
#define INT_EVQS_WIDTH 4

/**************************************************************************
 *
 * Falcon non-volatile configuration
 *
 **************************************************************************
 */

/* Board configuration v2 (v1 is obsolete; later versions are compatible) */
struct falcon_nvconfig_board_v2 {
	__le16 nports;
	u8 port0_phy_addr;
	u8 port0_phy_type;
	u8 port1_phy_addr;
	u8 port1_phy_type;
	__le16 asic_sub_revision;
	__le16 board_revision;
} __packed;

/* Board configuration v3 extra information */
struct falcon_nvconfig_board_v3 {
	__le32 spi_device_type[2];
} __packed;

/* Bit numbers for spi_device_type */
#define SPI_DEV_TYPE_SIZE_LBN 0
#define SPI_DEV_TYPE_SIZE_WIDTH 5
#define SPI_DEV_TYPE_ADDR_LEN_LBN 6
#define SPI_DEV_TYPE_ADDR_LEN_WIDTH 2
#define SPI_DEV_TYPE_ERASE_CMD_LBN 8
#define SPI_DEV_TYPE_ERASE_CMD_WIDTH 8
#define SPI_DEV_TYPE_ERASE_SIZE_LBN 16
#define SPI_DEV_TYPE_ERASE_SIZE_WIDTH 5
#define SPI_DEV_TYPE_BLOCK_SIZE_LBN 24
#define SPI_DEV_TYPE_BLOCK_SIZE_WIDTH 5
#define SPI_DEV_TYPE_FIELD(type, field)					\
	(((type) >> EFX_LOW_BIT(field)) & EFX_MASK32(EFX_WIDTH(field)))

#define NVCONFIG_OFFSET 0x300

#define NVCONFIG_BOARD_MAGIC_NUM 0xFA1C
struct falcon_nvconfig {
	efx_oword_t ee_vpd_cfg_reg;			/* 0x300 */
	u8 mac_address[2][8];			/* 0x310 */
	efx_oword_t pcie_sd_ctl0123_reg;		/* 0x320 */
	efx_oword_t pcie_sd_ctl45_reg;			/* 0x330 */
	efx_oword_t pcie_pcs_ctl_stat_reg;		/* 0x340 */
	efx_oword_t hw_init_reg;			/* 0x350 */
	efx_oword_t nic_stat_reg;			/* 0x360 */
	efx_oword_t glb_ctl_reg;			/* 0x370 */
	efx_oword_t srm_cfg_reg;			/* 0x380 */
	efx_oword_t spare_reg;				/* 0x390 */
	__le16 board_magic_num;			/* 0x3A0 */
	__le16 board_struct_ver;
	__le16 board_checksum;
	struct falcon_nvconfig_board_v2 board_v2;
	efx_oword_t ee_base_page_reg;			/* 0x3B0 */
	struct falcon_nvconfig_board_v3 board_v3;
} __packed;

#endif /* EFX_FALCON_HWDEFS_H */
