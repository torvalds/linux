/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _WX_TYPE_H_
#define _WX_TYPE_H_

/* Vendor ID */
#ifndef PCI_VENDOR_ID_WANGXUN
#define PCI_VENDOR_ID_WANGXUN                   0x8088
#endif

#define WX_NCSI_SUP                             0x8000
#define WX_NCSI_MASK                            0x8000
#define WX_WOL_SUP                              0x4000
#define WX_WOL_MASK                             0x4000

/**************** Global Registers ****************************/
/* chip control Registers */
#define WX_MIS_PWR                   0x10000
#define WX_MIS_RST                   0x1000C
#define WX_MIS_RST_LAN_RST(_i)       BIT((_i) + 1)
#define WX_MIS_RST_ST                0x10030
#define WX_MIS_RST_ST_RST_INI_SHIFT  8
#define WX_MIS_RST_ST_RST_INIT       (0xFF << WX_MIS_RST_ST_RST_INI_SHIFT)

/* FMGR Registers */
#define WX_SPI_CMD                   0x10104
#define WX_SPI_CMD_READ_DWORD        0x1
#define WX_SPI_CLK_DIV               0x3
#define WX_SPI_CMD_CMD(_v)           (((_v) & 0x7) << 28)
#define WX_SPI_CMD_CLK(_v)           (((_v) & 0x7) << 25)
#define WX_SPI_CMD_ADDR(_v)          (((_v) & 0xFFFFFF))
#define WX_SPI_DATA                  0x10108
#define WX_SPI_DATA_BYPASS           BIT(31)
#define WX_SPI_DATA_STATUS(_v)       (((_v) & 0xFF) << 16)
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

/*********************** Transmit DMA registers **************************/
/* transmit global control */
#define WX_TDM_CTL                   0x18000
/* TDM CTL BIT */
#define WX_TDM_CTL_TE                BIT(0) /* Transmit Enable */

/***************************** RDB registers *********************************/
/* receive packet buffer */
#define WX_RDB_PB_CTL                0x19000
#define WX_RDB_PB_CTL_RXEN           BIT(31) /* Enable Receiver */
#define WX_RDB_PB_CTL_DISABLED       BIT(0)
/* statistic */
#define WX_RDB_PFCMACDAL             0x19210
#define WX_RDB_PFCMACDAH             0x19214

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
/* mcasst/ucast overflow tbl */
#define WX_PSR_MC_TBL(_i)            (0x15200  + ((_i) * 4))
#define WX_PSR_UC_TBL(_i)            (0x15400 + ((_i) * 4))

/* Management */
#define WX_PSR_MNG_FLEX_SEL          0x1582C
#define WX_PSR_MNG_FLEX_DW_L(_i)     (0x15A00 + ((_i) * 16))
#define WX_PSR_MNG_FLEX_DW_H(_i)     (0x15A04 + ((_i) * 16))
#define WX_PSR_MNG_FLEX_MSK(_i)      (0x15A08 + ((_i) * 16))
#define WX_PSR_LAN_FLEX_SEL          0x15B8C
#define WX_PSR_LAN_FLEX_DW_L(_i)     (0x15C00 + ((_i) * 16))
#define WX_PSR_LAN_FLEX_DW_H(_i)     (0x15C04 + ((_i) * 16))
#define WX_PSR_LAN_FLEX_MSK(_i)      (0x15C08 + ((_i) * 16))

/* mac switcher */
#define WX_PSR_MAC_SWC_AD_L          0x16200
#define WX_PSR_MAC_SWC_AD_H          0x16204
#define WX_PSR_MAC_SWC_AD_H_AD(v)       (((v) & 0xFFFF))
#define WX_PSR_MAC_SWC_AD_H_ADTYPE(v)   (((v) & 0x1) << 30)
#define WX_PSR_MAC_SWC_AD_H_AV       BIT(31)
#define WX_PSR_MAC_SWC_VM_L          0x16208
#define WX_PSR_MAC_SWC_VM_H          0x1620C
#define WX_PSR_MAC_SWC_IDX           0x16210
#define WX_CLEAR_VMDQ_ALL            0xFFFFFFFFU

/************************************* ETH MAC *****************************/
#define WX_MAC_TX_CFG                0x11000
#define WX_MAC_TX_CFG_TE             BIT(0)
#define WX_MAC_RX_CFG                0x11004
#define WX_MAC_RX_CFG_RE             BIT(0)
#define WX_MAC_RX_CFG_JE             BIT(8)
#define WX_MAC_PKT_FLT               0x11008
#define WX_MAC_PKT_FLT_PR            BIT(0) /* promiscuous mode */
#define WX_MAC_RX_FLOW_CTRL          0x11090
#define WX_MAC_RX_FLOW_CTRL_RFE      BIT(0) /* receive fc enable */
#define WX_MMC_CONTROL               0x11800
#define WX_MMC_CONTROL_RSTONRD       BIT(2) /* reset on read */

/********************************* BAR registers ***************************/
/* Interrupt Registers */
#define WX_BME_CTL                   0x12020
#define WX_PX_MISC_IC                0x100
#define WX_PX_IMS(_i)                (0x140 + (_i) * 4)
#define WX_PX_TRANSACTION_PENDING    0x168

/* transmit DMA Registers */
#define WX_PX_TR_CFG(_i)             (0x03010 + ((_i) * 0x40))
/* Transmit Config masks */
#define WX_PX_TR_CFG_ENABLE          BIT(0) /* Ena specific Tx Queue */
#define WX_PX_TR_CFG_TR_SIZE_SHIFT   1 /* tx desc number per ring */
#define WX_PX_TR_CFG_SWFLSH          BIT(26) /* Tx Desc. wr-bk flushing */
#define WX_PX_TR_CFG_WTHRESH_SHIFT   16 /* shift to WTHRESH bits */
#define WX_PX_TR_CFG_THRE_SHIFT      8

/* Receive DMA Registers */
#define WX_PX_RR_CFG(_i)             (0x01010 + ((_i) * 0x40))
/* PX_RR_CFG bit definitions */
#define WX_PX_RR_CFG_RR_EN           BIT(0)

/* Number of 80 microseconds we wait for PCI Express master disable */
#define WX_PCI_MASTER_DISABLE_TIMEOUT        80000

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

struct wx_mac_info {
	enum wx_mac_type type;
	bool set_lben;
	u8 addr[ETH_ALEN];
	u8 perm_addr[ETH_ALEN];
	s32 mc_filter_type;
	u32 mcft_size;
	u32 num_rar_entries;
	u32 max_tx_queues;
	u32 max_rx_queues;
	struct wx_thermal_sensor_data sensor;
};

struct wx_addr_filter_info {
	u32 num_mc_addrs;
	u32 mta_in_use;
	bool user_set_promisc;
};

struct wx_hw {
	u8 __iomem *hw_addr;
	struct pci_dev *pdev;
	struct wx_bus_info bus;
	struct wx_mac_info mac;
	struct wx_addr_filter_info addr_ctrl;
	u16 device_id;
	u16 vendor_id;
	u16 subsystem_device_id;
	u16 subsystem_vendor_id;
	u8 revision_id;
	u16 oem_ssid;
	u16 oem_svid;
	bool adapter_stopped;
};

#define WX_INTR_ALL (~0ULL)

/* register operations */
#define wr32(a, reg, value)	writel((value), ((a)->hw_addr + (reg)))
#define rd32(a, reg)		readl((a)->hw_addr + (reg))

static inline u32
rd32m(struct wx_hw *wxhw, u32 reg, u32 mask)
{
	u32 val;

	val = rd32(wxhw, reg);
	return val & mask;
}

static inline void
wr32m(struct wx_hw *wxhw, u32 reg, u32 mask, u32 field)
{
	u32 val;

	val = rd32(wxhw, reg);
	val = ((val & ~mask) | (field & mask));

	wr32(wxhw, reg, val);
}

/* On some domestic CPU platforms, sometimes IO is not synchronized with
 * flushing memory, here use readl() to flush PCI read and write.
 */
#define WX_WRITE_FLUSH(H) rd32(H, WX_MIS_PWR)

#define wx_err(wxhw, fmt, arg...) \
	dev_err(&(wxhw)->pdev->dev, fmt, ##arg)

#define wx_dbg(wxhw, fmt, arg...) \
	dev_dbg(&(wxhw)->pdev->dev, fmt, ##arg)

#endif /* _WX_TYPE_H_ */
