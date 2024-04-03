/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _WX_TYPE_H_
#define _WX_TYPE_H_

#include <linux/bitfield.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/phylink.h>
#include <net/ip.h>

#define WX_NCSI_SUP                             0x8000
#define WX_NCSI_MASK                            0x8000
#define WX_WOL_SUP                              0x4000
#define WX_WOL_MASK                             0x4000

/* MSI-X capability fields masks */
#define WX_PCIE_MSIX_TBL_SZ_MASK                0x7FF
#define WX_PCI_LINK_STATUS                      0xB2

/**************** Global Registers ****************************/
/* chip control Registers */
#define WX_MIS_PWR                   0x10000
#define WX_MIS_RST                   0x1000C
#define WX_MIS_RST_LAN_RST(_i)       BIT((_i) + 1)
#define WX_MIS_RST_SW_RST            BIT(0)
#define WX_MIS_ST                    0x10028
#define WX_MIS_ST_MNG_INIT_DN        BIT(0)
#define WX_MIS_SWSM                  0x1002C
#define WX_MIS_SWSM_SMBI             BIT(0)
#define WX_MIS_RST_ST                0x10030
#define WX_MIS_RST_ST_RST_INI_SHIFT  8
#define WX_MIS_RST_ST_RST_INIT       (0xFF << WX_MIS_RST_ST_RST_INI_SHIFT)

/* FMGR Registers */
#define WX_SPI_CMD                   0x10104
#define WX_SPI_CMD_READ_DWORD        0x1
#define WX_SPI_CLK_DIV               0x3
#define WX_SPI_CMD_CMD(_v)           FIELD_PREP(GENMASK(30, 28), _v)
#define WX_SPI_CMD_CLK(_v)           FIELD_PREP(GENMASK(27, 25), _v)
#define WX_SPI_CMD_ADDR(_v)          FIELD_PREP(GENMASK(23, 0), _v)
#define WX_SPI_DATA                  0x10108
#define WX_SPI_DATA_BYPASS           BIT(31)
#define WX_SPI_DATA_OP_DONE          BIT(0)
#define WX_SPI_STATUS                0x1010C
#define WX_SPI_STATUS_OPDONE         BIT(0)
#define WX_SPI_STATUS_FLASH_BYPASS   BIT(31)
#define WX_SPI_ILDR_STATUS           0x10120

/* Sensors for PVT(Process Voltage Temperature) */
#define WX_TS_EN                     0x10304
#define WX_TS_EN_ENA                 BIT(0)
#define WX_TS_ALARM_THRE             0x1030C
#define WX_TS_DALARM_THRE            0x10310
#define WX_TS_INT_EN                 0x10314
#define WX_TS_INT_EN_DALARM_INT_EN   BIT(1)
#define WX_TS_INT_EN_ALARM_INT_EN    BIT(0)
#define WX_TS_ALARM_ST               0x10318
#define WX_TS_ALARM_ST_DALARM        BIT(1)
#define WX_TS_ALARM_ST_ALARM         BIT(0)

/* statistic */
#define WX_TX_FRAME_CNT_GOOD_BAD_L   0x1181C
#define WX_TX_BC_FRAMES_GOOD_L       0x11824
#define WX_TX_MC_FRAMES_GOOD_L       0x1182C
#define WX_RX_FRAME_CNT_GOOD_BAD_L   0x11900
#define WX_RX_BC_FRAMES_GOOD_L       0x11918
#define WX_RX_MC_FRAMES_GOOD_L       0x11920
#define WX_RX_CRC_ERROR_FRAMES_L     0x11928
#define WX_RX_LEN_ERROR_FRAMES_L     0x11978
#define WX_RX_UNDERSIZE_FRAMES_GOOD  0x11938
#define WX_RX_OVERSIZE_FRAMES_GOOD   0x1193C
#define WX_MAC_LXONOFFRXC            0x11E0C

/*********************** Receive DMA registers **************************/
#define WX_RDM_DRP_PKT               0x12500
#define WX_RDM_PKT_CNT               0x12504
#define WX_RDM_BYTE_CNT_LSB          0x12508
#define WX_RDM_BMC2OS_CNT            0x12510

/************************* Port Registers ************************************/
/* port cfg Registers */
#define WX_CFG_PORT_CTL              0x14400
#define WX_CFG_PORT_CTL_DRV_LOAD     BIT(3)
#define WX_CFG_PORT_CTL_QINQ         BIT(2)
#define WX_CFG_PORT_CTL_D_VLAN       BIT(0) /* double vlan*/
#define WX_CFG_TAG_TPID(_i)          (0x14430 + ((_i) * 4))
#define WX_CFG_PORT_CTL_NUM_VT_MASK  GENMASK(13, 12) /* number of TVs */


/* GPIO Registers */
#define WX_GPIO_DR                   0x14800
#define WX_GPIO_DR_0                 BIT(0) /* SDP0 Data Value */
#define WX_GPIO_DR_1                 BIT(1) /* SDP1 Data Value */
#define WX_GPIO_DDR                  0x14804
#define WX_GPIO_DDR_0                BIT(0) /* SDP0 IO direction */
#define WX_GPIO_DDR_1                BIT(1) /* SDP1 IO direction */
#define WX_GPIO_CTL                  0x14808
#define WX_GPIO_INTEN                0x14830
#define WX_GPIO_INTEN_0              BIT(0)
#define WX_GPIO_INTEN_1              BIT(1)
#define WX_GPIO_INTMASK              0x14834
#define WX_GPIO_INTTYPE_LEVEL        0x14838
#define WX_GPIO_POLARITY             0x1483C
#define WX_GPIO_INTSTATUS            0x14844
#define WX_GPIO_EOI                  0x1484C
#define WX_GPIO_EXT                  0x14850

/*********************** Transmit DMA registers **************************/
/* transmit global control */
#define WX_TDM_CTL                   0x18000
/* TDM CTL BIT */
#define WX_TDM_CTL_TE                BIT(0) /* Transmit Enable */
#define WX_TDM_PB_THRE(_i)           (0x18020 + ((_i) * 4))
#define WX_TDM_RP_IDX                0x1820C
#define WX_TDM_PKT_CNT               0x18308
#define WX_TDM_BYTE_CNT_LSB          0x1830C
#define WX_TDM_OS2BMC_CNT            0x18314
#define WX_TDM_RP_RATE               0x18404

/***************************** RDB registers *********************************/
/* receive packet buffer */
#define WX_RDB_PB_CTL                0x19000
#define WX_RDB_PB_CTL_RXEN           BIT(31) /* Enable Receiver */
#define WX_RDB_PB_CTL_DISABLED       BIT(0)
#define WX_RDB_PB_SZ(_i)             (0x19020 + ((_i) * 4))
#define WX_RDB_PB_SZ_SHIFT           10
/* statistic */
#define WX_RDB_PFCMACDAL             0x19210
#define WX_RDB_PFCMACDAH             0x19214
#define WX_RDB_LXOFFTXC              0x19218
#define WX_RDB_LXONTXC               0x1921C
/* Flow Control Registers */
#define WX_RDB_RFCV                  0x19200
#define WX_RDB_RFCL                  0x19220
#define WX_RDB_RFCL_XONE             BIT(31)
#define WX_RDB_RFCH                  0x19260
#define WX_RDB_RFCH_XOFFE            BIT(31)
#define WX_RDB_RFCRT                 0x192A0
#define WX_RDB_RFCC                  0x192A4
#define WX_RDB_RFCC_RFCE_802_3X      BIT(3)
/* ring assignment */
#define WX_RDB_PL_CFG(_i)            (0x19300 + ((_i) * 4))
#define WX_RDB_PL_CFG_L4HDR          BIT(1)
#define WX_RDB_PL_CFG_L3HDR          BIT(2)
#define WX_RDB_PL_CFG_L2HDR          BIT(3)
#define WX_RDB_PL_CFG_TUN_TUNHDR     BIT(4)
#define WX_RDB_PL_CFG_TUN_OUTL2HDR   BIT(5)
#define WX_RDB_RSSTBL(_i)            (0x19400 + ((_i) * 4))
#define WX_RDB_RSSRK(_i)             (0x19480 + ((_i) * 4))
#define WX_RDB_RA_CTL                0x194F4
#define WX_RDB_RA_CTL_RSS_EN         BIT(2) /* RSS Enable */
#define WX_RDB_RA_CTL_RSS_IPV4_TCP   BIT(16)
#define WX_RDB_RA_CTL_RSS_IPV4       BIT(17)
#define WX_RDB_RA_CTL_RSS_IPV6       BIT(20)
#define WX_RDB_RA_CTL_RSS_IPV6_TCP   BIT(21)
#define WX_RDB_RA_CTL_RSS_IPV4_UDP   BIT(22)
#define WX_RDB_RA_CTL_RSS_IPV6_UDP   BIT(23)

/******************************* PSR Registers *******************************/
/* psr control */
#define WX_PSR_CTL                   0x15000
/* Header split receive */
#define WX_PSR_CTL_SW_EN             BIT(18)
#define WX_PSR_CTL_RSC_ACK           BIT(17)
#define WX_PSR_CTL_RSC_DIS           BIT(16)
#define WX_PSR_CTL_PCSD              BIT(13)
#define WX_PSR_CTL_IPPCSE            BIT(12)
#define WX_PSR_CTL_BAM               BIT(10)
#define WX_PSR_CTL_UPE               BIT(9)
#define WX_PSR_CTL_MPE               BIT(8)
#define WX_PSR_CTL_MFE               BIT(7)
#define WX_PSR_CTL_MO_SHIFT          5
#define WX_PSR_CTL_MO                (0x3 << WX_PSR_CTL_MO_SHIFT)
#define WX_PSR_CTL_TPE               BIT(4)
#define WX_PSR_MAX_SZ                0x15020
#define WX_PSR_VLAN_CTL              0x15088
#define WX_PSR_VLAN_CTL_CFIEN        BIT(29)  /* bit 29 */
#define WX_PSR_VLAN_CTL_VFE          BIT(30)  /* bit 30 */
/* mcasst/ucast overflow tbl */
#define WX_PSR_MC_TBL(_i)            (0x15200  + ((_i) * 4))
#define WX_PSR_UC_TBL(_i)            (0x15400 + ((_i) * 4))

/* VM L2 contorl */
#define WX_PSR_VM_L2CTL(_i)          (0x15600 + ((_i) * 4))
#define WX_PSR_VM_L2CTL_UPE          BIT(4) /* unicast promiscuous */
#define WX_PSR_VM_L2CTL_VACC         BIT(6) /* accept nomatched vlan */
#define WX_PSR_VM_L2CTL_AUPE         BIT(8) /* accept untagged packets */
#define WX_PSR_VM_L2CTL_ROMPE        BIT(9) /* accept packets in MTA tbl */
#define WX_PSR_VM_L2CTL_ROPE         BIT(10) /* accept packets in UC tbl */
#define WX_PSR_VM_L2CTL_BAM          BIT(11) /* accept broadcast packets */
#define WX_PSR_VM_L2CTL_MPE          BIT(12) /* multicast promiscuous */

/* Management */
#define WX_PSR_MNG_FLEX_SEL          0x1582C
#define WX_PSR_MNG_FLEX_DW_L(_i)     (0x15A00 + ((_i) * 16))
#define WX_PSR_MNG_FLEX_DW_H(_i)     (0x15A04 + ((_i) * 16))
#define WX_PSR_MNG_FLEX_MSK(_i)      (0x15A08 + ((_i) * 16))
#define WX_PSR_LAN_FLEX_SEL          0x15B8C
#define WX_PSR_LAN_FLEX_DW_L(_i)     (0x15C00 + ((_i) * 16))
#define WX_PSR_LAN_FLEX_DW_H(_i)     (0x15C04 + ((_i) * 16))
#define WX_PSR_LAN_FLEX_MSK(_i)      (0x15C08 + ((_i) * 16))

#define WX_PSR_WKUP_CTL              0x15B80
/* Wake Up Filter Control Bit */
#define WX_PSR_WKUP_CTL_MAG          BIT(1) /* Magic Packet Wakeup Enable */

/* vlan tbl */
#define WX_PSR_VLAN_TBL(_i)          (0x16000 + ((_i) * 4))

/* mac switcher */
#define WX_PSR_MAC_SWC_AD_L          0x16200
#define WX_PSR_MAC_SWC_AD_H          0x16204
#define WX_PSR_MAC_SWC_AD_H_AD(v)       FIELD_PREP(U16_MAX, v)
#define WX_PSR_MAC_SWC_AD_H_ADTYPE(v)   FIELD_PREP(BIT(30), v)
#define WX_PSR_MAC_SWC_AD_H_AV       BIT(31)
#define WX_PSR_MAC_SWC_VM_L          0x16208
#define WX_PSR_MAC_SWC_VM_H          0x1620C
#define WX_PSR_MAC_SWC_IDX           0x16210
#define WX_CLEAR_VMDQ_ALL            0xFFFFFFFFU

/* vlan switch */
#define WX_PSR_VLAN_SWC              0x16220
#define WX_PSR_VLAN_SWC_VM_L         0x16224
#define WX_PSR_VLAN_SWC_VM_H         0x16228
#define WX_PSR_VLAN_SWC_IDX          0x16230         /* 64 vlan entries */
/* VLAN pool filtering masks */
#define WX_PSR_VLAN_SWC_VIEN         BIT(31)  /* filter is valid */
#define WX_PSR_VLAN_SWC_ENTRIES      64

/********************************* RSEC **************************************/
/* general rsec */
#define WX_RSC_CTL                   0x17000
#define WX_RSC_CTL_SAVE_MAC_ERR      BIT(6)
#define WX_RSC_CTL_CRC_STRIP         BIT(2)
#define WX_RSC_CTL_RX_DIS            BIT(1)
#define WX_RSC_ST                    0x17004
#define WX_RSC_ST_RSEC_RDY           BIT(0)

/****************************** TDB ******************************************/
#define WX_TDB_PB_SZ(_i)             (0x1CC00 + ((_i) * 4))
#define WX_TXPKT_SIZE_MAX            0xA /* Max Tx Packet size */

/****************************** TSEC *****************************************/
/* Security Control Registers */
#define WX_TSC_CTL                   0x1D000
#define WX_TSC_CTL_TX_DIS            BIT(1)
#define WX_TSC_CTL_TSEC_DIS          BIT(0)
#define WX_TSC_ST                    0x1D004
#define WX_TSC_ST_SECTX_RDY          BIT(0)
#define WX_TSC_BUF_AE                0x1D00C
#define WX_TSC_BUF_AE_THR            GENMASK(9, 0)

/************************************** MNG ********************************/
#define WX_MNG_SWFW_SYNC             0x1E008
#define WX_MNG_SWFW_SYNC_SW_MB       BIT(2)
#define WX_MNG_SWFW_SYNC_SW_FLASH    BIT(3)
#define WX_MNG_MBOX                  0x1E100
#define WX_MNG_MBOX_CTL              0x1E044
#define WX_MNG_MBOX_CTL_SWRDY        BIT(0)
#define WX_MNG_MBOX_CTL_FWRDY        BIT(2)
#define WX_MNG_BMC2OS_CNT            0x1E090
#define WX_MNG_OS2BMC_CNT            0x1E094

/************************************* ETH MAC *****************************/
#define WX_MAC_TX_CFG                0x11000
#define WX_MAC_TX_CFG_TE             BIT(0)
#define WX_MAC_TX_CFG_SPEED_MASK     GENMASK(30, 29)
#define WX_MAC_TX_CFG_SPEED_10G      FIELD_PREP(WX_MAC_TX_CFG_SPEED_MASK, 0)
#define WX_MAC_TX_CFG_SPEED_1G       FIELD_PREP(WX_MAC_TX_CFG_SPEED_MASK, 3)
#define WX_MAC_RX_CFG                0x11004
#define WX_MAC_RX_CFG_RE             BIT(0)
#define WX_MAC_RX_CFG_JE             BIT(8)
#define WX_MAC_PKT_FLT               0x11008
#define WX_MAC_PKT_FLT_PR            BIT(0) /* promiscuous mode */
#define WX_MAC_WDG_TIMEOUT           0x1100C
#define WX_MAC_RX_FLOW_CTRL          0x11090
#define WX_MAC_RX_FLOW_CTRL_RFE      BIT(0) /* receive fc enable */
/* MDIO Registers */
#define WX_MSCA                      0x11200
#define WX_MSCA_RA(v)                FIELD_PREP(U16_MAX, v)
#define WX_MSCA_PA(v)                FIELD_PREP(GENMASK(20, 16), v)
#define WX_MSCA_DA(v)                FIELD_PREP(GENMASK(25, 21), v)
#define WX_MSCC                      0x11204
#define WX_MSCC_CMD(v)               FIELD_PREP(GENMASK(17, 16), v)

enum WX_MSCA_CMD_value {
	WX_MSCA_CMD_RSV = 0,
	WX_MSCA_CMD_WRITE,
	WX_MSCA_CMD_POST_READ,
	WX_MSCA_CMD_READ,
};

#define WX_MSCC_SADDR                BIT(18)
#define WX_MSCC_BUSY                 BIT(22)
#define WX_MDIO_CLK(v)               FIELD_PREP(GENMASK(21, 19), v)
#define WX_MDIO_CLAUSE_SELECT        0x11220
#define WX_MMC_CONTROL               0x11800
#define WX_MMC_CONTROL_RSTONRD       BIT(2) /* reset on read */

/********************************* BAR registers ***************************/
/* Interrupt Registers */
#define WX_BME_CTL                   0x12020
#define WX_PX_MISC_IC                0x100
#define WX_PX_MISC_ICS               0x104
#define WX_PX_MISC_IEN               0x108
#define WX_PX_INTA                   0x110
#define WX_PX_GPIE                   0x118
#define WX_PX_GPIE_MODEL             BIT(0)
#define WX_PX_IC(_i)                 (0x120 + (_i) * 4)
#define WX_PX_IMS(_i)                (0x140 + (_i) * 4)
#define WX_PX_IMC(_i)                (0x150 + (_i) * 4)
#define WX_PX_ISB_ADDR_L             0x160
#define WX_PX_ISB_ADDR_H             0x164
#define WX_PX_TRANSACTION_PENDING    0x168
#define WX_PX_ITRSEL                 0x180
#define WX_PX_ITR(_i)                (0x200 + (_i) * 4)
#define WX_PX_ITR_CNT_WDIS           BIT(31)
#define WX_PX_MISC_IVAR              0x4FC
#define WX_PX_IVAR(_i)               (0x500 + (_i) * 4)

#define WX_PX_IVAR_ALLOC_VAL         0x80 /* Interrupt Allocation valid */
#define WX_7K_ITR                    595
#define WX_12K_ITR                   336
#define WX_20K_ITR                   200
#define WX_SP_MAX_EITR               0x00000FF8U
#define WX_EM_MAX_EITR               0x00007FFCU

/* transmit DMA Registers */
#define WX_PX_TR_BAL(_i)             (0x03000 + ((_i) * 0x40))
#define WX_PX_TR_BAH(_i)             (0x03004 + ((_i) * 0x40))
#define WX_PX_TR_WP(_i)              (0x03008 + ((_i) * 0x40))
#define WX_PX_TR_RP(_i)              (0x0300C + ((_i) * 0x40))
#define WX_PX_TR_CFG(_i)             (0x03010 + ((_i) * 0x40))
/* Transmit Config masks */
#define WX_PX_TR_CFG_ENABLE          BIT(0) /* Ena specific Tx Queue */
#define WX_PX_TR_CFG_TR_SIZE_SHIFT   1 /* tx desc number per ring */
#define WX_PX_TR_CFG_SWFLSH          BIT(26) /* Tx Desc. wr-bk flushing */
#define WX_PX_TR_CFG_WTHRESH_SHIFT   16 /* shift to WTHRESH bits */
#define WX_PX_TR_CFG_THRE_SHIFT      8

/* Receive DMA Registers */
#define WX_PX_RR_BAL(_i)             (0x01000 + ((_i) * 0x40))
#define WX_PX_RR_BAH(_i)             (0x01004 + ((_i) * 0x40))
#define WX_PX_RR_WP(_i)              (0x01008 + ((_i) * 0x40))
#define WX_PX_RR_RP(_i)              (0x0100C + ((_i) * 0x40))
#define WX_PX_RR_CFG(_i)             (0x01010 + ((_i) * 0x40))
#define WX_PX_MPRC(_i)               (0x01020 + ((_i) * 0x40))
/* PX_RR_CFG bit definitions */
#define WX_PX_RR_CFG_VLAN            BIT(31)
#define WX_PX_RR_CFG_DROP_EN         BIT(30)
#define WX_PX_RR_CFG_SPLIT_MODE      BIT(26)
#define WX_PX_RR_CFG_RR_THER_SHIFT   16
#define WX_PX_RR_CFG_RR_HDR_SZ       GENMASK(15, 12)
#define WX_PX_RR_CFG_RR_BUF_SZ       GENMASK(11, 8)
#define WX_PX_RR_CFG_BHDRSIZE_SHIFT  6 /* 64byte resolution (>> 6)
					* + at bit 8 offset (<< 12)
					*  = (<< 6)
					*/
#define WX_PX_RR_CFG_BSIZEPKT_SHIFT  2 /* so many KBs */
#define WX_PX_RR_CFG_RR_SIZE_SHIFT   1
#define WX_PX_RR_CFG_RR_EN           BIT(0)

/* Number of 80 microseconds we wait for PCI Express master disable */
#define WX_PCI_MASTER_DISABLE_TIMEOUT        80000

/****************** Manageablility Host Interface defines ********************/
#define WX_HI_MAX_BLOCK_BYTE_LENGTH  256 /* Num of bytes in range */
#define WX_HI_COMMAND_TIMEOUT        1000 /* Process HI command limit */

#define FW_READ_SHADOW_RAM_CMD       0x31
#define FW_READ_SHADOW_RAM_LEN       0x6
#define FW_DEFAULT_CHECKSUM          0xFF /* checksum always 0xFF */
#define FW_NVM_DATA_OFFSET           3
#define FW_MAX_READ_BUFFER_SIZE      244
#define FW_RESET_CMD                 0xDF
#define FW_RESET_LEN                 0x2
#define FW_CEM_HDR_LEN               0x4
#define FW_CEM_CMD_RESERVED          0X0
#define FW_CEM_MAX_RETRIES           3
#define FW_CEM_RESP_STATUS_SUCCESS   0x1

#define WX_SW_REGION_PTR             0x1C

#define WX_MAC_STATE_DEFAULT         0x1
#define WX_MAC_STATE_MODIFIED        0x2
#define WX_MAC_STATE_IN_USE          0x4

/* BitTimes (BT) conversion */
#define WX_BT2KB(BT)         (((BT) + (8 * 1024 - 1)) / (8 * 1024))
#define WX_B2BT(BT)          ((BT) * 8)

/* Calculate Delay to respond to PFC */
#define WX_PFC_D     672
/* Calculate Cable Delay */
#define WX_CABLE_DC  5556 /* Delay Copper */
/* Calculate Delay incurred from higher layer */
#define WX_HD        6144

/* Calculate Interface Delay */
#define WX_PHY_D     12800
#define WX_MAC_D     4096
#define WX_XAUI_D    (2 * 1024)
#define WX_ID        (WX_MAC_D + WX_XAUI_D + WX_PHY_D)
/* Calculate PCI Bus delay for low thresholds */
#define WX_PCI_DELAY 10000

/* Calculate delay value in bit times */
#define WX_DV(_max_frame_link, _max_frame_tc) \
	((36 * (WX_B2BT(_max_frame_link) + WX_PFC_D + \
		(2 * WX_CABLE_DC) + (2 * WX_ID) + WX_HD) / 25 + 1) + \
	 2 * WX_B2BT(_max_frame_tc))

/* Calculate low threshold delay values */
#define WX_LOW_DV(_max_frame_tc) \
	(2 * (2 * WX_B2BT(_max_frame_tc) + (36 * WX_PCI_DELAY / 25) + 1))

/* flow control */
#define WX_DEFAULT_FCPAUSE           0xFFFF

#define WX_MAX_RXD                   8192
#define WX_MAX_TXD                   8192
#define WX_MIN_RXD                   128
#define WX_MIN_TXD                   128

/* Number of Transmit and Receive Descriptors must be a multiple of 8 */
#define WX_REQ_RX_DESCRIPTOR_MULTIPLE   8
#define WX_REQ_TX_DESCRIPTOR_MULTIPLE   8

#define WX_MAX_JUMBO_FRAME_SIZE      9432 /* max payload 9414 */
#define VMDQ_P(p)                    p

/* Supported Rx Buffer Sizes */
#define WX_RXBUFFER_256      256    /* Used for skb receive header */
#define WX_RXBUFFER_2K       2048
#define WX_MAX_RXBUFFER      16384  /* largest size for single descriptor */

#if MAX_SKB_FRAGS < 8
#define WX_RX_BUFSZ      ALIGN(WX_MAX_RXBUFFER / MAX_SKB_FRAGS, 1024)
#else
#define WX_RX_BUFSZ      WX_RXBUFFER_2K
#endif

#define WX_RX_BUFFER_WRITE   16      /* Must be power of 2 */

#define WX_MAX_DATA_PER_TXD  BIT(14)
/* Tx Descriptors needed, worst case */
#define TXD_USE_COUNT(S)     DIV_ROUND_UP((S), WX_MAX_DATA_PER_TXD)
#define DESC_NEEDED          (MAX_SKB_FRAGS + 4)

#define WX_CFG_PORT_ST               0x14404

/******************* Receive Descriptor bit definitions **********************/
#define WX_RXD_STAT_DD               BIT(0) /* Done */
#define WX_RXD_STAT_EOP              BIT(1) /* End of Packet */
#define WX_RXD_STAT_VP               BIT(5) /* IEEE VLAN Pkt */
#define WX_RXD_STAT_L4CS             BIT(7) /* L4 xsum calculated */
#define WX_RXD_STAT_IPCS             BIT(8) /* IP xsum calculated */
#define WX_RXD_STAT_OUTERIPCS        BIT(10) /* Cloud IP xsum calculated*/

#define WX_RXD_ERR_OUTERIPER         BIT(26) /* CRC IP Header error */
#define WX_RXD_ERR_RXE               BIT(29) /* Any MAC Error */
#define WX_RXD_ERR_TCPE              BIT(30) /* TCP/UDP Checksum Error */
#define WX_RXD_ERR_IPE               BIT(31) /* IP Checksum Error */

/* RSS Hash results */
#define WX_RXD_RSSTYPE_MASK          GENMASK(3, 0)
#define WX_RXD_RSSTYPE_IPV4_TCP      0x00000001U
#define WX_RXD_RSSTYPE_IPV6_TCP      0x00000003U
#define WX_RXD_RSSTYPE_IPV4_SCTP     0x00000004U
#define WX_RXD_RSSTYPE_IPV6_SCTP     0x00000006U
#define WX_RXD_RSSTYPE_IPV4_UDP      0x00000007U
#define WX_RXD_RSSTYPE_IPV6_UDP      0x00000008U

#define WX_RSS_L4_TYPES_MASK \
	((1ul << WX_RXD_RSSTYPE_IPV4_TCP) | \
	 (1ul << WX_RXD_RSSTYPE_IPV4_UDP) | \
	 (1ul << WX_RXD_RSSTYPE_IPV4_SCTP) | \
	 (1ul << WX_RXD_RSSTYPE_IPV6_TCP) | \
	 (1ul << WX_RXD_RSSTYPE_IPV6_UDP) | \
	 (1ul << WX_RXD_RSSTYPE_IPV6_SCTP))
/* TUN */
#define WX_PTYPE_TUN_IPV4            0x80
#define WX_PTYPE_TUN_IPV6            0xC0

/* PKT for TUN */
#define WX_PTYPE_PKT_IPIP            0x00 /* IP+IP */
#define WX_PTYPE_PKT_IG              0x10 /* IP+GRE */
#define WX_PTYPE_PKT_IGM             0x20 /* IP+GRE+MAC */
#define WX_PTYPE_PKT_IGMV            0x30 /* IP+GRE+MAC+VLAN */
/* PKT for !TUN */
#define WX_PTYPE_PKT_MAC             0x10
#define WX_PTYPE_PKT_IP              0x20

/* TYP for PKT=mac */
#define WX_PTYPE_TYP_MAC             0x01
/* TYP for PKT=ip */
#define WX_PTYPE_PKT_IPV6            0x08
#define WX_PTYPE_TYP_IPFRAG          0x01
#define WX_PTYPE_TYP_IP              0x02
#define WX_PTYPE_TYP_UDP             0x03
#define WX_PTYPE_TYP_TCP             0x04
#define WX_PTYPE_TYP_SCTP            0x05

#define WX_RXD_PKTTYPE(_rxd) \
	((le32_to_cpu((_rxd)->wb.lower.lo_dword.data) >> 9) & 0xFF)
#define WX_RXD_IPV6EX(_rxd) \
	((le32_to_cpu((_rxd)->wb.lower.lo_dword.data) >> 6) & 0x1)
/*********************** Transmit Descriptor Config Masks ****************/
#define WX_TXD_STAT_DD               BIT(0)  /* Descriptor Done */
#define WX_TXD_DTYP_DATA             0       /* Adv Data Descriptor */
#define WX_TXD_PAYLEN_SHIFT          13      /* Desc PAYLEN shift */
#define WX_TXD_EOP                   BIT(24) /* End of Packet */
#define WX_TXD_IFCS                  BIT(25) /* Insert FCS */
#define WX_TXD_RS                    BIT(27) /* Report Status */

/*********************** Adv Transmit Descriptor Config Masks ****************/
#define WX_TXD_MAC_TSTAMP            BIT(19) /* IEEE1588 time stamp */
#define WX_TXD_DTYP_CTXT             BIT(20) /* Adv Context Desc */
#define WX_TXD_LINKSEC               BIT(26) /* enable linksec */
#define WX_TXD_VLE                   BIT(30) /* VLAN pkt enable */
#define WX_TXD_TSE                   BIT(31) /* TCP Seg enable */
#define WX_TXD_CC                    BIT(7) /* Check Context */
#define WX_TXD_IPSEC                 BIT(8) /* enable ipsec esp */
#define WX_TXD_L4CS                  BIT(9)
#define WX_TXD_IIPCS                 BIT(10)
#define WX_TXD_EIPCS                 BIT(11)
#define WX_TXD_PAYLEN_SHIFT          13 /* Adv desc PAYLEN shift */
#define WX_TXD_MACLEN_SHIFT          9  /* Adv ctxt desc mac len shift */
#define WX_TXD_TAG_TPID_SEL_SHIFT    11

#define WX_TXD_L4LEN_SHIFT           8  /* Adv ctxt L4LEN shift */
#define WX_TXD_MSS_SHIFT             16  /* Adv ctxt MSS shift */

#define WX_TXD_OUTER_IPLEN_SHIFT     12 /* Adv ctxt OUTERIPLEN shift */
#define WX_TXD_TUNNEL_LEN_SHIFT      21 /* Adv ctxt TUNNELLEN shift */
#define WX_TXD_TUNNEL_TYPE_SHIFT     11 /* Adv Tx Desc Tunnel Type shift */
#define WX_TXD_TUNNEL_UDP            FIELD_PREP(BIT(WX_TXD_TUNNEL_TYPE_SHIFT), 0)
#define WX_TXD_TUNNEL_GRE            FIELD_PREP(BIT(WX_TXD_TUNNEL_TYPE_SHIFT), 1)

enum wx_tx_flags {
	/* cmd_type flags */
	WX_TX_FLAGS_HW_VLAN	= 0x01,
	WX_TX_FLAGS_TSO		= 0x02,
	WX_TX_FLAGS_TSTAMP	= 0x04,

	/* olinfo flags */
	WX_TX_FLAGS_CC		= 0x08,
	WX_TX_FLAGS_IPV4	= 0x10,
	WX_TX_FLAGS_CSUM	= 0x20,
	WX_TX_FLAGS_OUTER_IPV4	= 0x100,
	WX_TX_FLAGS_LINKSEC	= 0x200,
	WX_TX_FLAGS_IPSEC	= 0x400,
};

/* VLAN info */
#define WX_TX_FLAGS_VLAN_MASK			GENMASK(31, 16)
#define WX_TX_FLAGS_VLAN_SHIFT			16

/* wx_dec_ptype.mac: outer mac */
enum wx_dec_ptype_mac {
	WX_DEC_PTYPE_MAC_IP	= 0,
	WX_DEC_PTYPE_MAC_L2	= 2,
	WX_DEC_PTYPE_MAC_FCOE	= 3,
};

/* wx_dec_ptype.[e]ip: outer&encaped ip */
#define WX_DEC_PTYPE_IP_FRAG	0x4
enum wx_dec_ptype_ip {
	WX_DEC_PTYPE_IP_NONE = 0,
	WX_DEC_PTYPE_IP_IPV4 = 1,
	WX_DEC_PTYPE_IP_IPV6 = 2,
	WX_DEC_PTYPE_IP_FGV4 = WX_DEC_PTYPE_IP_FRAG | WX_DEC_PTYPE_IP_IPV4,
	WX_DEC_PTYPE_IP_FGV6 = WX_DEC_PTYPE_IP_FRAG | WX_DEC_PTYPE_IP_IPV6,
};

/* wx_dec_ptype.etype: encaped type */
enum wx_dec_ptype_etype {
	WX_DEC_PTYPE_ETYPE_NONE	= 0,
	WX_DEC_PTYPE_ETYPE_IPIP	= 1,	/* IP+IP */
	WX_DEC_PTYPE_ETYPE_IG	= 2,	/* IP+GRE */
	WX_DEC_PTYPE_ETYPE_IGM	= 3,	/* IP+GRE+MAC */
	WX_DEC_PTYPE_ETYPE_IGMV	= 4,	/* IP+GRE+MAC+VLAN */
};

/* wx_dec_ptype.proto: payload proto */
enum wx_dec_ptype_prot {
	WX_DEC_PTYPE_PROT_NONE	= 0,
	WX_DEC_PTYPE_PROT_UDP	= 1,
	WX_DEC_PTYPE_PROT_TCP	= 2,
	WX_DEC_PTYPE_PROT_SCTP	= 3,
	WX_DEC_PTYPE_PROT_ICMP	= 4,
	WX_DEC_PTYPE_PROT_TS	= 5,	/* time sync */
};

/* wx_dec_ptype.layer: payload layer */
enum wx_dec_ptype_layer {
	WX_DEC_PTYPE_LAYER_NONE = 0,
	WX_DEC_PTYPE_LAYER_PAY2 = 1,
	WX_DEC_PTYPE_LAYER_PAY3 = 2,
	WX_DEC_PTYPE_LAYER_PAY4 = 3,
};

struct wx_dec_ptype {
	u32 known:1;
	u32 mac:2;	/* outer mac */
	u32 ip:3;	/* outer ip*/
	u32 etype:3;	/* encaped type */
	u32 eip:3;	/* encaped ip */
	u32 prot:4;	/* payload proto */
	u32 layer:3;	/* payload layer */
};

/* macro to make the table lines short */
#define WX_PTT(mac, ip, etype, eip, proto, layer)\
	      {1, \
	       WX_DEC_PTYPE_MAC_##mac,		/* mac */\
	       WX_DEC_PTYPE_IP_##ip,		/* ip */ \
	       WX_DEC_PTYPE_ETYPE_##etype,	/* etype */\
	       WX_DEC_PTYPE_IP_##eip,		/* eip */\
	       WX_DEC_PTYPE_PROT_##proto,	/* proto */\
	       WX_DEC_PTYPE_LAYER_##layer	/* layer */}

/* Host Interface Command Structures */
struct wx_hic_hdr {
	u8 cmd;
	u8 buf_len;
	union {
		u8 cmd_resv;
		u8 ret_status;
	} cmd_or_resp;
	u8 checksum;
};

struct wx_hic_hdr2_req {
	u8 cmd;
	u8 buf_lenh;
	u8 buf_lenl;
	u8 checksum;
};

struct wx_hic_hdr2_rsp {
	u8 cmd;
	u8 buf_lenl;
	u8 buf_lenh_status;     /* 7-5: high bits of buf_len, 4-0: status */
	u8 checksum;
};

union wx_hic_hdr2 {
	struct wx_hic_hdr2_req req;
	struct wx_hic_hdr2_rsp rsp;
};

/* These need to be dword aligned */
struct wx_hic_read_shadow_ram {
	union wx_hic_hdr2 hdr;
	u32 address;
	u16 length;
	u16 pad2;
	u16 data;
	u16 pad3;
};

struct wx_hic_reset {
	struct wx_hic_hdr hdr;
	u16 lan_id;
	u16 reset_type;
};

/* Bus parameters */
struct wx_bus_info {
	u8 func;
	u16 device;
};

struct wx_thermal_sensor_data {
	s16 temp;
	s16 alarm_thresh;
	s16 dalarm_thresh;
};

enum wx_mac_type {
	wx_mac_unknown = 0,
	wx_mac_sp,
	wx_mac_em
};

enum sp_media_type {
	sp_media_unknown = 0,
	sp_media_fiber,
	sp_media_copper,
	sp_media_backplane
};

enum em_mac_type {
	em_mac_type_unknown = 0,
	em_mac_type_mdi,
	em_mac_type_rgmii
};

struct wx_mac_info {
	enum wx_mac_type type;
	bool set_lben;
	u8 addr[ETH_ALEN];
	u8 perm_addr[ETH_ALEN];
	u32 mta_shadow[128];
	s32 mc_filter_type;
	u32 mcft_size;
	u32 vft_shadow[128];
	u32 vft_size;
	u32 num_rar_entries;
	u32 rx_pb_size;
	u32 tx_pb_size;
	u32 max_tx_queues;
	u32 max_rx_queues;

	u16 max_msix_vectors;
	struct wx_thermal_sensor_data sensor;
};

enum wx_eeprom_type {
	wx_eeprom_uninitialized = 0,
	wx_eeprom_spi,
	wx_flash,
	wx_eeprom_none /* No NVM support */
};

struct wx_eeprom_info {
	enum wx_eeprom_type type;
	u32 semaphore_delay;
	u16 word_size;
	u16 sw_region_offset;
};

struct wx_addr_filter_info {
	u32 num_mc_addrs;
	u32 mta_in_use;
	bool user_set_promisc;
};

struct wx_mac_addr {
	u8 addr[ETH_ALEN];
	u16 state; /* bitmask */
	u64 pools;
};

enum wx_reset_type {
	WX_LAN_RESET = 0,
	WX_SW_RESET,
	WX_GLOBAL_RESET
};

struct wx_cb {
	dma_addr_t dma;
	u16     append_cnt;      /* number of skb's appended */
	bool    page_released;
	bool    dma_released;
};

#define WX_CB(skb) ((struct wx_cb *)(skb)->cb)

/* Transmit Descriptor */
union wx_tx_desc {
	struct {
		__le64 buffer_addr; /* Address of descriptor's data buf */
		__le32 cmd_type_len;
		__le32 olinfo_status;
	} read;
	struct {
		__le64 rsvd; /* Reserved */
		__le32 nxtseq_seed;
		__le32 status;
	} wb;
};

/* Receive Descriptor */
union wx_rx_desc {
	struct {
		__le64 pkt_addr; /* Packet buffer address */
		__le64 hdr_addr; /* Header buffer address */
	} read;
	struct {
		struct {
			union {
				__le32 data;
				struct {
					__le16 pkt_info; /* RSS, Pkt type */
					__le16 hdr_info; /* Splithdr, hdrlen */
				} hs_rss;
			} lo_dword;
			union {
				__le32 rss; /* RSS Hash */
				struct {
					__le16 ip_id; /* IP id */
					__le16 csum; /* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			__le32 status_error; /* ext status/error */
			__le16 length; /* Packet length */
			__le16 vlan; /* VLAN tag */
		} upper;
	} wb;  /* writeback */
};

struct wx_tx_context_desc {
	__le32 vlan_macip_lens;
	__le32 seqnum_seed;
	__le32 type_tucmd_mlhl;
	__le32 mss_l4len_idx;
};

/* if _flag is in _input, return _result */
#define WX_SET_FLAG(_input, _flag, _result) \
	(((_flag) <= (_result)) ? \
	 ((u32)((_input) & (_flag)) * ((_result) / (_flag))) : \
	 ((u32)((_input) & (_flag)) / ((_flag) / (_result))))

#define WX_RX_DESC(R, i)     \
	(&(((union wx_rx_desc *)((R)->desc))[i]))
#define WX_TX_DESC(R, i)     \
	(&(((union wx_tx_desc *)((R)->desc))[i]))
#define WX_TX_CTXTDESC(R, i) \
	(&(((struct wx_tx_context_desc *)((R)->desc))[i]))

/* wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer
 */
struct wx_tx_buffer {
	union wx_tx_desc *next_to_watch;
	struct sk_buff *skb;
	unsigned int bytecount;
	unsigned short gso_segs;
	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
	__be16 protocol;
	u32 tx_flags;
};

struct wx_rx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	dma_addr_t page_dma;
	struct page *page;
	unsigned int page_offset;
};

struct wx_queue_stats {
	u64 packets;
	u64 bytes;
};

struct wx_tx_queue_stats {
	u64 restart_queue;
	u64 tx_busy;
};

struct wx_rx_queue_stats {
	u64 non_eop_descs;
	u64 csum_good_cnt;
	u64 csum_err;
	u64 alloc_rx_buff_failed;
};

/* iterator for handling rings in ring container */
#define wx_for_each_ring(posm, headm) \
	for (posm = (headm).ring; posm; posm = posm->next)

struct wx_ring_container {
	struct wx_ring *ring;           /* pointer to linked list of rings */
	unsigned int total_bytes;       /* total bytes processed this int */
	unsigned int total_packets;     /* total packets processed this int */
	u8 count;                       /* total number of rings in vector */
	u8 itr;                         /* current ITR setting for ring */
};
struct wx_ring {
	struct wx_ring *next;           /* pointer to next ring in q_vector */
	struct wx_q_vector *q_vector;   /* backpointer to host q_vector */
	struct net_device *netdev;      /* netdev ring belongs to */
	struct device *dev;             /* device for DMA mapping */
	struct page_pool *page_pool;
	void *desc;                     /* descriptor ring memory */
	union {
		struct wx_tx_buffer *tx_buffer_info;
		struct wx_rx_buffer *rx_buffer_info;
	};
	u8 __iomem *tail;
	dma_addr_t dma;                 /* phys. address of descriptor ring */
	unsigned int size;              /* length in bytes */

	u16 count;                      /* amount of descriptors */

	u8 queue_index; /* needed for multiqueue queue management */
	u8 reg_idx;                     /* holds the special value that gets
					 * the hardware register offset
					 * associated with this ring, which is
					 * different for DCB and RSS modes
					 */
	u16 next_to_use;
	u16 next_to_clean;
	u16 next_to_alloc;

	struct wx_queue_stats stats;
	struct u64_stats_sync syncp;
	union {
		struct wx_tx_queue_stats tx_stats;
		struct wx_rx_queue_stats rx_stats;
	};
} ____cacheline_internodealigned_in_smp;

struct wx_q_vector {
	struct wx *wx;
	int cpu;        /* CPU for DCA */
	int numa_node;
	u16 v_idx;      /* index of q_vector within array, also used for
			 * finding the bit in EICR and friends that
			 * represents the vector for this ring
			 */
	u16 itr;        /* Interrupt throttle rate written to EITR */
	struct wx_ring_container rx, tx;
	struct napi_struct napi;
	struct rcu_head rcu;    /* to avoid race with update stats on free */

	char name[IFNAMSIZ + 17];

	/* for dynamic allocation of rings associated with this q_vector */
	struct wx_ring ring[] ____cacheline_internodealigned_in_smp;
};

struct wx_ring_feature {
	u16 limit;      /* upper limit on feature indices */
	u16 indices;    /* current value of indices */
	u16 mask;       /* Mask used for feature to ring mapping */
	u16 offset;     /* offset to start of feature */
};

enum wx_ring_f_enum {
	RING_F_NONE = 0,
	RING_F_RSS,
	RING_F_ARRAY_SIZE  /* must be last in enum set */
};

enum wx_isb_idx {
	WX_ISB_HEADER,
	WX_ISB_MISC,
	WX_ISB_VEC0,
	WX_ISB_VEC1,
	WX_ISB_MAX
};

struct wx_fc_info {
	u32 high_water; /* Flow Ctrl High-water */
	u32 low_water; /* Flow Ctrl Low-water */
};

/* Statistics counters collected by the MAC */
struct wx_hw_stats {
	u64 gprc;
	u64 gptc;
	u64 gorc;
	u64 gotc;
	u64 tpr;
	u64 tpt;
	u64 bprc;
	u64 bptc;
	u64 mprc;
	u64 mptc;
	u64 roc;
	u64 ruc;
	u64 lxonoffrxc;
	u64 lxontxc;
	u64 lxofftxc;
	u64 o2bgptc;
	u64 b2ospc;
	u64 o2bspc;
	u64 b2ogprc;
	u64 rdmdrop;
	u64 crcerrs;
	u64 rlec;
	u64 qmprc;
};

struct wx {
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];

	void *priv;
	u8 __iomem *hw_addr;
	struct pci_dev *pdev;
	struct net_device *netdev;
	struct wx_bus_info bus;
	struct wx_mac_info mac;
	enum em_mac_type mac_type;
	enum sp_media_type media_type;
	struct wx_eeprom_info eeprom;
	struct wx_addr_filter_info addr_ctrl;
	struct wx_fc_info fc;
	struct wx_mac_addr *mac_table;
	u16 device_id;
	u16 vendor_id;
	u16 subsystem_device_id;
	u16 subsystem_vendor_id;
	u8 revision_id;
	u16 oem_ssid;
	u16 oem_svid;
	u16 msg_enable;
	bool adapter_stopped;
	u16 tpid[8];
	char eeprom_id[32];
	char *driver_name;
	enum wx_reset_type reset_type;

	/* PHY stuff */
	unsigned int link;
	int speed;
	int duplex;
	struct phy_device *phydev;
	struct phylink *phylink;
	struct phylink_config phylink_config;

	bool wol_hw_supported;
	bool ncsi_enabled;
	bool gpio_ctrl;
	raw_spinlock_t gpio_lock;

	/* Tx fast path data */
	int num_tx_queues;
	u16 tx_itr_setting;
	u16 tx_work_limit;

	/* Rx fast path data */
	int num_rx_queues;
	u16 rx_itr_setting;
	u16 rx_work_limit;

	int num_q_vectors;      /* current number of q_vectors for device */
	int max_q_vectors;      /* upper limit of q_vectors for device */

	u32 tx_ring_count;
	u32 rx_ring_count;

	struct wx_ring *tx_ring[64] ____cacheline_aligned_in_smp;
	struct wx_ring *rx_ring[64];
	struct wx_q_vector *q_vector[64];

	unsigned int queues_per_pool;
	struct msix_entry *msix_q_entries;
	struct msix_entry *msix_entry;
	struct wx_ring_feature ring_feature[RING_F_ARRAY_SIZE];

	/* misc interrupt status block */
	dma_addr_t isb_dma;
	u32 *isb_mem;
	u32 isb_tag[WX_ISB_MAX];

#define WX_MAX_RETA_ENTRIES 128
#define WX_RSS_INDIR_TBL_MAX 64
	u8 rss_indir_tbl[WX_MAX_RETA_ENTRIES];
	bool rss_enabled;
#define WX_RSS_KEY_SIZE     40  /* size of RSS Hash Key in bytes */
	u32 *rss_key;
	u32 wol;

	u16 bd_number;

	struct wx_hw_stats stats;
	u64 tx_busy;
	u64 non_eop_descs;
	u64 restart_queue;
	u64 hw_csum_rx_good;
	u64 hw_csum_rx_error;
	u64 alloc_rx_buff_failed;
};

#define WX_INTR_ALL (~0ULL)
#define WX_INTR_Q(i) BIT((i) + 1)

/* register operations */
#define wr32(a, reg, value)	writel((value), ((a)->hw_addr + (reg)))
#define rd32(a, reg)		readl((a)->hw_addr + (reg))
#define rd32a(a, reg, offset) ( \
	rd32((a), (reg) + ((offset) << 2)))
#define wr32a(a, reg, off, val) \
	wr32((a), (reg) + ((off) << 2), (val))

static inline u32
rd32m(struct wx *wx, u32 reg, u32 mask)
{
	u32 val;

	val = rd32(wx, reg);
	return val & mask;
}

static inline void
wr32m(struct wx *wx, u32 reg, u32 mask, u32 field)
{
	u32 val;

	val = rd32(wx, reg);
	val = ((val & ~mask) | (field & mask));

	wr32(wx, reg, val);
}

static inline u64
rd64(struct wx *wx, u32 reg)
{
	u64 lsb, msb;

	lsb = rd32(wx, reg);
	msb = rd32(wx, reg + 4);

	return (lsb | msb << 32);
}

/* On some domestic CPU platforms, sometimes IO is not synchronized with
 * flushing memory, here use readl() to flush PCI read and write.
 */
#define WX_WRITE_FLUSH(H) rd32(H, WX_MIS_PWR)

#define wx_err(wx, fmt, arg...) \
	dev_err(&(wx)->pdev->dev, fmt, ##arg)

#define wx_dbg(wx, fmt, arg...) \
	dev_dbg(&(wx)->pdev->dev, fmt, ##arg)

static inline struct wx *phylink_to_wx(struct phylink_config *config)
{
	return container_of(config, struct wx, phylink_config);
}

#endif /* _WX_TYPE_H_ */
