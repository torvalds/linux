/*
 * Copyright (c) 2014 - 2016 Jes Sorensen <Jes.Sorensen@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * Register definitions taken from original Realtek rtl8723au driver
 */

/* 0x0000 ~ 0x00FF	System Configuration */
#define REG_SYS_ISO_CTRL		0x0000
#define  SYS_ISO_MD2PP			BIT(0)
#define  SYS_ISO_ANALOG_IPS		BIT(5)
#define  SYS_ISO_DIOR			BIT(9)
#define  SYS_ISO_PWC_EV25V		BIT(14)
#define  SYS_ISO_PWC_EV12V		BIT(15)

#define REG_SYS_FUNC			0x0002
#define  SYS_FUNC_BBRSTB		BIT(0)
#define  SYS_FUNC_BB_GLB_RSTN		BIT(1)
#define  SYS_FUNC_USBA			BIT(2)
#define  SYS_FUNC_UPLL			BIT(3)
#define  SYS_FUNC_USBD			BIT(4)
#define  SYS_FUNC_DIO_PCIE		BIT(5)
#define  SYS_FUNC_PCIEA			BIT(6)
#define  SYS_FUNC_PPLL			BIT(7)
#define  SYS_FUNC_PCIED			BIT(8)
#define  SYS_FUNC_DIOE			BIT(9)
#define  SYS_FUNC_CPU_ENABLE		BIT(10)
#define  SYS_FUNC_DCORE			BIT(11)
#define  SYS_FUNC_ELDR			BIT(12)
#define  SYS_FUNC_DIO_RF		BIT(13)
#define  SYS_FUNC_HWPDN			BIT(14)
#define  SYS_FUNC_MREGEN		BIT(15)

#define REG_APS_FSMCO			0x0004
#define  APS_FSMCO_PFM_ALDN		BIT(1)
#define  APS_FSMCO_PFM_WOWL		BIT(3)
#define  APS_FSMCO_ENABLE_POWERDOWN	BIT(4)
#define  APS_FSMCO_MAC_ENABLE		BIT(8)
#define  APS_FSMCO_MAC_OFF		BIT(9)
#define  APS_FSMCO_SW_LPS		BIT(10)
#define  APS_FSMCO_HW_SUSPEND		BIT(11)
#define  APS_FSMCO_PCIE			BIT(12)
#define  APS_FSMCO_HW_POWERDOWN		BIT(15)
#define  APS_FSMCO_WLON_RESET		BIT(16)

#define REG_SYS_CLKR			0x0008
#define  SYS_CLK_ANAD16V_ENABLE		BIT(0)
#define  SYS_CLK_ANA8M			BIT(1)
#define  SYS_CLK_MACSLP			BIT(4)
#define  SYS_CLK_LOADER_ENABLE		BIT(5)
#define  SYS_CLK_80M_SSC_DISABLE	BIT(7)
#define  SYS_CLK_80M_SSC_ENABLE_HO	BIT(8)
#define  SYS_CLK_PHY_SSC_RSTB		BIT(9)
#define  SYS_CLK_SEC_CLK_ENABLE		BIT(10)
#define  SYS_CLK_MAC_CLK_ENABLE		BIT(11)
#define  SYS_CLK_ENABLE			BIT(12)
#define  SYS_CLK_RING_CLK_ENABLE	BIT(13)

#define REG_9346CR			0x000a
#define  EEPROM_BOOT			BIT(4)
#define  EEPROM_ENABLE			BIT(5)

#define REG_EE_VPD			0x000c
#define REG_AFE_MISC			0x0010
#define  AFE_MISC_WL_XTAL_CTRL		BIT(6)

#define REG_SPS0_CTRL			0x0011
#define REG_SPS_OCP_CFG			0x0018
#define REG_8192E_LDOV12_CTRL		0x0014
#define REG_RSV_CTRL			0x001c

#define REG_RF_CTRL			0x001f
#define  RF_ENABLE			BIT(0)
#define  RF_RSTB			BIT(1)
#define  RF_SDMRSTB			BIT(2)

#define REG_LDOA15_CTRL			0x0020
#define  LDOA15_ENABLE			BIT(0)
#define  LDOA15_STANDBY			BIT(1)
#define  LDOA15_OBUF			BIT(2)
#define  LDOA15_REG_VOS			BIT(3)
#define  LDOA15_VOADJ_SHIFT		4

#define REG_LDOV12D_CTRL		0x0021
#define  LDOV12D_ENABLE			BIT(0)
#define  LDOV12D_STANDBY		BIT(1)
#define  LDOV12D_VADJ_SHIFT		4

#define REG_LDOHCI12_CTRL		0x0022

#define REG_LPLDO_CTRL			0x0023
#define  LPLDO_HSM			BIT(2)
#define  LPLDO_LSM_DIS			BIT(3)

#define REG_AFE_XTAL_CTRL		0x0024
#define  AFE_XTAL_ENABLE		BIT(0)
#define  AFE_XTAL_B_SELECT		BIT(1)
#define  AFE_XTAL_GATE_USB		BIT(8)
#define  AFE_XTAL_GATE_AFE		BIT(11)
#define  AFE_XTAL_RF_GATE		BIT(14)
#define  AFE_XTAL_GATE_DIG		BIT(17)
#define  AFE_XTAL_BT_GATE		BIT(20)

/*
 * 0x0028 is also known as REG_AFE_CTRL2 on 8723bu/8192eu
 */
#define REG_AFE_PLL_CTRL		0x0028
#define  AFE_PLL_ENABLE			BIT(0)
#define  AFE_PLL_320_ENABLE		BIT(1)
#define  APE_PLL_FREF_SELECT		BIT(2)
#define  AFE_PLL_EDGE_SELECT		BIT(3)
#define  AFE_PLL_WDOGB			BIT(4)
#define  AFE_PLL_LPF_ENABLE		BIT(5)

#define REG_MAC_PHY_CTRL		0x002c

#define REG_EFUSE_CTRL			0x0030
#define REG_EFUSE_TEST			0x0034
#define  EFUSE_TRPT			BIT(7)
	/*  00: Wifi Efuse, 01: BT Efuse0, 10: BT Efuse1, 11: BT Efuse2 */
#define  EFUSE_CELL_SEL			(BIT(8) | BIT(9))
#define  EFUSE_LDOE25_ENABLE		BIT(31)
#define  EFUSE_SELECT_MASK		0x0300
#define  EFUSE_WIFI_SELECT		0x0000
#define  EFUSE_BT0_SELECT		0x0100
#define  EFUSE_BT1_SELECT		0x0200
#define  EFUSE_BT2_SELECT		0x0300

#define  EFUSE_ACCESS_ENABLE		0x69	/* RTL8723 only */
#define  EFUSE_ACCESS_DISABLE		0x00	/* RTL8723 only */

#define REG_PWR_DATA			0x0038
#define  PWR_DATA_EEPRPAD_RFE_CTRL_EN	BIT(11)

#define REG_CAL_TIMER			0x003c
#define REG_ACLK_MON			0x003e
#define REG_GPIO_MUXCFG			0x0040
#define REG_GPIO_IO_SEL			0x0042
#define REG_MAC_PINMUX_CFG		0x0043
#define REG_GPIO_PIN_CTRL		0x0044
#define REG_GPIO_INTM			0x0048
#define  GPIO_INTM_EDGE_TRIG_IRQ	BIT(9)

#define REG_LEDCFG0			0x004c
#define  LEDCFG0_DPDT_SELECT		BIT(23)
#define REG_LEDCFG1			0x004d
#define REG_LEDCFG2			0x004e
#define  LEDCFG2_DPDT_SELECT		BIT(7)
#define REG_LEDCFG3			0x004f
#define REG_LEDCFG			REG_LEDCFG2
#define REG_FSIMR			0x0050
#define REG_FSISR			0x0054
#define REG_HSIMR			0x0058
#define REG_HSISR			0x005c
/*  RTL8723 WIFI/BT/GPS Multi-Function GPIO Pin Control. */
#define REG_GPIO_PIN_CTRL_2		0x0060
/*  RTL8723 WIFI/BT/GPS Multi-Function GPIO Select. */
#define REG_GPIO_IO_SEL_2		0x0062
#define  GPIO_IO_SEL_2_GPIO09_INPUT	BIT(1)
#define  GPIO_IO_SEL_2_GPIO09_IRQ	BIT(9)

/*  RTL8723B */
#define REG_PAD_CTRL1			0x0064
#define  PAD_CTRL1_SW_DPDT_SEL_DATA	BIT(0)

/*  RTL8723 only WIFI/BT/GPS Multi-Function control source. */
#define REG_MULTI_FUNC_CTRL		0x0068

#define  MULTI_FN_WIFI_HW_PWRDOWN_EN	BIT(0)	/* Enable GPIO[9] as WiFi HW
						   powerdown source */
#define  MULTI_FN_WIFI_HW_PWRDOWN_SL	BIT(1)	/* WiFi HW powerdown polarity
						   control */
#define  MULTI_WIFI_FUNC_EN		BIT(2)	/* WiFi function enable */

#define  MULTI_WIFI_HW_ROF_EN		BIT(3)	/* Enable GPIO[9] as WiFi RF HW
						   powerdown source */
#define  MULTI_BT_HW_PWRDOWN_EN		BIT(16)	/* Enable GPIO[11] as BT HW
						   powerdown source */
#define  MULTI_BT_HW_PWRDOWN_SL		BIT(17)	/* BT HW powerdown polarity
						   control */
#define  MULTI_BT_FUNC_EN		BIT(18)	/* BT function enable */
#define  MULTI_BT_HW_ROF_EN		BIT(19)	/* Enable GPIO[11] as BT/GPS
						   RF HW powerdown source */
#define  MULTI_GPS_HW_PWRDOWN_EN	BIT(20)	/* Enable GPIO[10] as GPS HW
						   powerdown source */
#define  MULTI_GPS_HW_PWRDOWN_SL	BIT(21)	/* GPS HW powerdown polarity
						   control */
#define  MULTI_GPS_FUNC_EN		BIT(22)	/* GPS function enable */

#define REG_AFE_CTRL4			0x0078	/* 8192eu/8723bu */
#define REG_LDO_SW_CTRL			0x007c	/* 8192eu */

#define REG_MCU_FW_DL			0x0080
#define  MCU_FW_DL_ENABLE		BIT(0)
#define  MCU_FW_DL_READY		BIT(1)
#define  MCU_FW_DL_CSUM_REPORT		BIT(2)
#define  MCU_MAC_INIT_READY		BIT(3)
#define  MCU_BB_INIT_READY		BIT(4)
#define  MCU_RF_INIT_READY		BIT(5)
#define  MCU_WINT_INIT_READY		BIT(6)
#define  MCU_FW_RAM_SEL			BIT(7)	/* 1: RAM, 0:ROM */
#define  MCU_CP_RESET			BIT(23)

#define REG_HMBOX_EXT_0			0x0088
#define REG_HMBOX_EXT_1			0x008a
#define REG_HMBOX_EXT_2			0x008c
#define REG_HMBOX_EXT_3			0x008e
/* Interrupt registers for 8192e/8723bu/8812 */
#define REG_HIMR0			0x00b0
#define REG_HISR0			0x00b4
#define REG_HIMR1			0x00b8
#define REG_HISR1			0x00bc

/*  Host suspend counter on FPGA platform */
#define REG_HOST_SUSP_CNT		0x00bc
/*  Efuse access protection for RTL8723 */
#define REG_EFUSE_ACCESS		0x00cf
#define REG_BIST_SCAN			0x00d0
#define REG_BIST_RPT			0x00d4
#define REG_BIST_ROM_RPT		0x00d8
#define REG_USB_SIE_INTF		0x00e0
#define REG_PCIE_MIO_INTF		0x00e4
#define REG_PCIE_MIO_INTD		0x00e8
#define REG_HPON_FSM			0x00ec
#define  HPON_FSM_BONDING_MASK		(BIT(22) | BIT(23))
#define  HPON_FSM_BONDING_1T2R		BIT(22)
#define REG_SYS_CFG			0x00f0
#define  SYS_CFG_XCLK_VLD		BIT(0)
#define  SYS_CFG_ACLK_VLD		BIT(1)
#define  SYS_CFG_UCLK_VLD		BIT(2)
#define  SYS_CFG_PCLK_VLD		BIT(3)
#define  SYS_CFG_PCIRSTB		BIT(4)
#define  SYS_CFG_V15_VLD		BIT(5)
#define  SYS_CFG_TRP_B15V_EN		BIT(7)
#define  SYS_CFG_SW_OFFLOAD_EN		BIT(7)	/* For chips with IOL support */
#define  SYS_CFG_SIC_IDLE		BIT(8)
#define  SYS_CFG_BD_MAC2		BIT(9)
#define  SYS_CFG_BD_MAC1		BIT(10)
#define  SYS_CFG_IC_MACPHY_MODE		BIT(11)
#define  SYS_CFG_CHIP_VER		(BIT(12) | BIT(13) | BIT(14) | BIT(15))
#define  SYS_CFG_BT_FUNC		BIT(16)
#define  SYS_CFG_VENDOR_ID		BIT(19)
#define  SYS_CFG_VENDOR_EXT_MASK	(BIT(18) | BIT(19))
#define   SYS_CFG_VENDOR_ID_TSMC	0
#define   SYS_CFG_VENDOR_ID_SMIC	BIT(18)
#define   SYS_CFG_VENDOR_ID_UMC		BIT(19)
#define  SYS_CFG_PAD_HWPD_IDN		BIT(22)
#define  SYS_CFG_TRP_VAUX_EN		BIT(23)
#define  SYS_CFG_TRP_BT_EN		BIT(24)
#define  SYS_CFG_SPS_LDO_SEL		BIT(24)	/* 8192eu */
#define  SYS_CFG_BD_PKG_SEL		BIT(25)
#define  SYS_CFG_BD_HCI_SEL		BIT(26)
#define  SYS_CFG_TYPE_ID		BIT(27)
#define  SYS_CFG_RTL_ID			BIT(23) /*  TestChip ID,
						    1:Test(RLE); 0:MP(RL) */
#define  SYS_CFG_SPS_SEL		BIT(24) /*  1:LDO regulator mode;
						    0:Switching regulator mode*/
#define  SYS_CFG_CHIP_VERSION_MASK	0xf000	/* Bit 12 - 15 */
#define  SYS_CFG_CHIP_VERSION_SHIFT	12

#define REG_GPIO_OUTSTS			0x00f4	/*  For RTL8723 only. */
#define  GPIO_EFS_HCI_SEL		(BIT(0) | BIT(1))
#define  GPIO_PAD_HCI_SEL		(BIT(2) | BIT(3))
#define  GPIO_HCI_SEL			(BIT(4) | BIT(5))
#define  GPIO_PKG_SEL_HCI		BIT(6)
#define  GPIO_FEN_GPS			BIT(7)
#define  GPIO_FEN_BT			BIT(8)
#define  GPIO_FEN_WL			BIT(9)
#define  GPIO_FEN_PCI			BIT(10)
#define  GPIO_FEN_USB			BIT(11)
#define  GPIO_BTRF_HWPDN_N		BIT(12)
#define  GPIO_WLRF_HWPDN_N		BIT(13)
#define  GPIO_PDN_BT_N			BIT(14)
#define  GPIO_PDN_GPS_N			BIT(15)
#define  GPIO_BT_CTL_HWPDN		BIT(16)
#define  GPIO_GPS_CTL_HWPDN		BIT(17)
#define  GPIO_PPHY_SUSB			BIT(20)
#define  GPIO_UPHY_SUSB			BIT(21)
#define  GPIO_PCI_SUSEN			BIT(22)
#define  GPIO_USB_SUSEN			BIT(23)
#define  GPIO_RF_RL_ID			(BIT(31) | BIT(30) | BIT(29) | BIT(28))

#define REG_SYS_CFG2			0x00fc	/* 8192eu */

/* 0x0100 ~ 0x01FF	MACTOP General Configuration */
#define REG_CR				0x0100
#define  CR_HCI_TXDMA_ENABLE		BIT(0)
#define  CR_HCI_RXDMA_ENABLE		BIT(1)
#define  CR_TXDMA_ENABLE		BIT(2)
#define  CR_RXDMA_ENABLE		BIT(3)
#define  CR_PROTOCOL_ENABLE		BIT(4)
#define  CR_SCHEDULE_ENABLE		BIT(5)
#define  CR_MAC_TX_ENABLE		BIT(6)
#define  CR_MAC_RX_ENABLE		BIT(7)
#define  CR_SW_BEACON_ENABLE		BIT(8)
#define  CR_SECURITY_ENABLE		BIT(9)
#define  CR_CALTIMER_ENABLE		BIT(10)

/* Media Status Register */
#define REG_MSR				0x0102
#define  MSR_LINKTYPE_MASK		0x3
#define  MSR_LINKTYPE_NONE		0x0
#define  MSR_LINKTYPE_ADHOC		0x1
#define  MSR_LINKTYPE_STATION		0x2
#define  MSR_LINKTYPE_AP		0x3

#define REG_PBP				0x0104
#define  PBP_PAGE_SIZE_RX_SHIFT		0
#define  PBP_PAGE_SIZE_TX_SHIFT		4
#define  PBP_PAGE_SIZE_64		0x0
#define  PBP_PAGE_SIZE_128		0x1
#define  PBP_PAGE_SIZE_256		0x2
#define  PBP_PAGE_SIZE_512		0x3
#define  PBP_PAGE_SIZE_1024		0x4

#define REG_TRXDMA_CTRL			0x010c
#define  TRXDMA_CTRL_RXDMA_AGG_EN	BIT(2)
#define  TRXDMA_CTRL_VOQ_SHIFT		4
#define  TRXDMA_CTRL_VIQ_SHIFT		6
#define  TRXDMA_CTRL_BEQ_SHIFT		8
#define  TRXDMA_CTRL_BKQ_SHIFT		10
#define  TRXDMA_CTRL_MGQ_SHIFT		12
#define  TRXDMA_CTRL_HIQ_SHIFT		14
#define  TRXDMA_QUEUE_LOW		1
#define  TRXDMA_QUEUE_NORMAL		2
#define  TRXDMA_QUEUE_HIGH		3

#define REG_TRXFF_BNDY			0x0114
#define REG_TRXFF_STATUS		0x0118
#define REG_RXFF_PTR			0x011c
#define REG_HIMR			0x0120
#define REG_HISR			0x0124
#define REG_HIMRE			0x0128
#define REG_HISRE			0x012c
#define REG_CPWM			0x012f
#define REG_FWIMR			0x0130
#define REG_FWISR			0x0134
#define REG_PKTBUF_DBG_CTRL		0x0140
#define REG_PKTBUF_DBG_DATA_L		0x0144
#define REG_PKTBUF_DBG_DATA_H		0x0148

#define REG_TC0_CTRL			0x0150
#define REG_TC1_CTRL			0x0154
#define REG_TC2_CTRL			0x0158
#define REG_TC3_CTRL			0x015c
#define REG_TC4_CTRL			0x0160
#define REG_TCUNIT_BASE			0x0164
#define REG_MBIST_START			0x0174
#define REG_MBIST_DONE			0x0178
#define REG_MBIST_FAIL			0x017c
#define REG_C2HEVT_MSG_NORMAL		0x01a0
/* 8192EU/8723BU/8812 */
#define REG_C2HEVT_CMD_ID_8723B		0x01ae
#define REG_C2HEVT_CLEAR		0x01af
#define REG_C2HEVT_MSG_TEST		0x01b8
#define REG_MCUTST_1			0x01c0
#define REG_FMTHR			0x01c8
#define REG_HMTFR			0x01cc
#define REG_HMBOX_0			0x01d0
#define REG_HMBOX_1			0x01d4
#define REG_HMBOX_2			0x01d8
#define REG_HMBOX_3			0x01dc

#define REG_LLT_INIT			0x01e0
#define  LLT_OP_INACTIVE		0x0
#define  LLT_OP_WRITE			(0x1 << 30)
#define  LLT_OP_READ			(0x2 << 30)
#define  LLT_OP_MASK			(0x3 << 30)

#define REG_BB_ACCEESS_CTRL		0x01e8
#define REG_BB_ACCESS_DATA		0x01ec

#define REG_HMBOX_EXT0_8723B		0x01f0
#define REG_HMBOX_EXT1_8723B		0x01f4
#define REG_HMBOX_EXT2_8723B		0x01f8
#define REG_HMBOX_EXT3_8723B		0x01fc

/* 0x0200 ~ 0x027F	TXDMA Configuration */
#define REG_RQPN			0x0200
#define  RQPN_HI_PQ_SHIFT		0
#define  RQPN_LO_PQ_SHIFT		8
#define  RQPN_PUB_PQ_SHIFT		16
#define  RQPN_LOAD			BIT(31)

#define REG_FIFOPAGE			0x0204
#define REG_TDECTRL			0x0208
#define REG_TXDMA_OFFSET_CHK		0x020c
#define  TXDMA_OFFSET_DROP_DATA_EN	BIT(9)
#define REG_TXDMA_STATUS		0x0210
#define REG_RQPN_NPQ			0x0214
#define  RQPN_NPQ_SHIFT			0
#define  RQPN_EPQ_SHIFT			16

#define REG_AUTO_LLT			0x0224
#define  AUTO_LLT_INIT_LLT		BIT(16)

#define REG_DWBCN1_CTRL_8723B		0x0228

/* 0x0280 ~ 0x02FF	RXDMA Configuration */
#define REG_RXDMA_AGG_PG_TH		0x0280	/* 0-7 : USB DMA size bits
						   8-14: USB DMA timeout
						   15  : Aggregation enable
						         Only seems to be used
							 on 8723bu/8192eu */
#define  RXDMA_USB_AGG_ENABLE		BIT(31)
#define REG_RXPKT_NUM			0x0284
#define  RXPKT_NUM_RXDMA_IDLE		BIT(17)
#define  RXPKT_NUM_RW_RELEASE_EN	BIT(18)
#define REG_RXDMA_STATUS		0x0288

/* Presumably only found on newer chips such as 8723bu */
#define REG_RX_DMA_CTRL_8723B		0x0286
#define REG_RXDMA_PRO_8723B		0x0290

#define REG_RF_BB_CMD_ADDR		0x02c0
#define REG_RF_BB_CMD_DATA		0x02c4

/*  spec version 11 */
/* 0x0400 ~ 0x047F	Protocol Configuration */
/* 8192c, 8192d */
#define REG_VOQ_INFO			0x0400
#define REG_VIQ_INFO			0x0404
#define REG_BEQ_INFO			0x0408
#define REG_BKQ_INFO			0x040c
/* 8188e, 8723a, 8812a, 8821a, 8192e, 8723b */
#define REG_Q0_INFO			0x400
#define REG_Q1_INFO			0x404
#define REG_Q2_INFO			0x408
#define REG_Q3_INFO			0x40c

#define REG_MGQ_INFO			0x0410
#define REG_HGQ_INFO			0x0414
#define REG_BCNQ_INFO			0x0418

#define REG_CPU_MGQ_INFORMATION		0x041c
#define REG_FWHW_TXQ_CTRL		0x0420
#define  FWHW_TXQ_CTRL_AMPDU_RETRY	BIT(7)
#define  FWHW_TXQ_CTRL_XMIT_MGMT_ACK	BIT(12)

#define REG_HWSEQ_CTRL			0x0423
#define REG_TXPKTBUF_BCNQ_BDNY		0x0424
#define REG_TXPKTBUF_MGQ_BDNY		0x0425
#define REG_LIFETIME_EN			0x0426
#define REG_MULTI_BCNQ_OFFSET		0x0427

#define REG_SPEC_SIFS			0x0428
#define  SPEC_SIFS_CCK_MASK		0x00ff
#define  SPEC_SIFS_CCK_SHIFT		0
#define  SPEC_SIFS_OFDM_MASK		0xff00
#define  SPEC_SIFS_OFDM_SHIFT		8

#define REG_RETRY_LIMIT			0x042a
#define  RETRY_LIMIT_LONG_SHIFT		0
#define  RETRY_LIMIT_LONG_MASK		0x003f
#define  RETRY_LIMIT_SHORT_SHIFT	8
#define  RETRY_LIMIT_SHORT_MASK		0x3f00

#define REG_DARFRC			0x0430
#define REG_RARFRC			0x0438
#define REG_RESPONSE_RATE_SET		0x0440
#define  RESPONSE_RATE_BITMAP_ALL	0xfffff
#define  RESPONSE_RATE_RRSR_CCK_ONLY_1M	0xffff1
#define  RSR_1M				BIT(0)
#define  RSR_2M				BIT(1)
#define  RSR_5_5M			BIT(2)
#define  RSR_11M			BIT(3)
#define  RSR_6M				BIT(4)
#define  RSR_9M				BIT(5)
#define  RSR_12M			BIT(6)
#define  RSR_18M			BIT(7)
#define  RSR_24M			BIT(8)
#define  RSR_36M			BIT(9)
#define  RSR_48M			BIT(10)
#define  RSR_54M			BIT(11)
#define  RSR_MCS0			BIT(12)
#define  RSR_MCS1			BIT(13)
#define  RSR_MCS2			BIT(14)
#define  RSR_MCS3			BIT(15)
#define  RSR_MCS4			BIT(16)
#define  RSR_MCS5			BIT(17)
#define  RSR_MCS6			BIT(18)
#define  RSR_MCS7			BIT(19)
#define  RSR_RSC_LOWER_SUB_CHANNEL	BIT(21)	/* 0x200000 */
#define  RSR_RSC_UPPER_SUB_CHANNEL	BIT(22)	/* 0x400000 */
#define  RSR_RSC_BANDWIDTH_40M		(RSR_RSC_UPPER_SUB_CHANNEL | \
					 RSR_RSC_LOWER_SUB_CHANNEL)
#define  RSR_ACK_SHORT_PREAMBLE		BIT(23)

#define REG_ARFR0			0x0444
#define REG_ARFR1			0x0448
#define REG_ARFR2			0x044c
#define REG_ARFR3			0x0450
#define REG_AMPDU_MAX_TIME_8723B	0x0456
#define REG_AGGLEN_LMT			0x0458
#define REG_AMPDU_MIN_SPACE		0x045c
#define REG_TXPKTBUF_WMAC_LBK_BF_HD	0x045d
#define REG_FAST_EDCA_CTRL		0x0460
#define REG_RD_RESP_PKT_TH		0x0463
#define REG_INIRTS_RATE_SEL		0x0480
/* 8723bu */
#define REG_DATA_SUBCHANNEL		0x0483
/* 8723au */
#define REG_INIDATA_RATE_SEL		0x0484
/* MACID_SLEEP_1/3 for 8723b, 8192e, 8812a, 8821a */
#define REG_MACID_SLEEP_3_8732B		0x0484
#define REG_MACID_SLEEP_1_8732B		0x0488

#define REG_POWER_STATUS		0x04a4
#define REG_POWER_STAGE1		0x04b4
#define REG_POWER_STAGE2		0x04b8
#define REG_AMPDU_BURST_MODE_8723B	0x04bc
#define REG_PKT_VO_VI_LIFE_TIME		0x04c0
#define REG_PKT_BE_BK_LIFE_TIME		0x04c2
#define REG_STBC_SETTING		0x04c4
#define REG_QUEUE_CTRL			0x04c6
#define REG_HT_SINGLE_AMPDU_8723B	0x04c7
#define REG_PROT_MODE_CTRL		0x04c8
#define REG_MAX_AGGR_NUM		0x04ca
#define REG_RTS_MAX_AGGR_NUM		0x04cb
#define REG_BAR_MODE_CTRL		0x04cc
#define REG_RA_TRY_RATE_AGG_LMT		0x04cf
/* MACID_DROP for 8723a */
#define REG_MACID_DROP_8732A		0x04d0
/* EARLY_MODE_CONTROL 8188e */
#define REG_EARLY_MODE_CONTROL_8188E	0x04d0
/* MACID_SLEEP_2 for 8723b, 8192e, 8812a, 8821a */
#define REG_MACID_SLEEP_2_8732B		0x04d0
#define REG_MACID_SLEEP			0x04d4
#define REG_NQOS_SEQ			0x04dc
#define REG_QOS_SEQ			0x04de
#define REG_NEED_CPU_HANDLE		0x04e0
#define REG_PKT_LOSE_RPT		0x04e1
#define REG_PTCL_ERR_STATUS		0x04e2
#define REG_TX_REPORT_CTRL		0x04ec
#define  TX_REPORT_CTRL_TIMER_ENABLE	BIT(1)

#define REG_TX_REPORT_TIME		0x04f0
#define REG_DUMMY			0x04fc

/* 0x0500 ~ 0x05FF	EDCA Configuration */
#define REG_EDCA_VO_PARAM		0x0500
#define REG_EDCA_VI_PARAM		0x0504
#define REG_EDCA_BE_PARAM		0x0508
#define REG_EDCA_BK_PARAM		0x050c
#define  EDCA_PARAM_ECW_MIN_SHIFT	8
#define  EDCA_PARAM_ECW_MAX_SHIFT	12
#define  EDCA_PARAM_TXOP_SHIFT		16
#define REG_BEACON_TCFG			0x0510
#define REG_PIFS			0x0512
#define REG_RDG_PIFS			0x0513
#define REG_SIFS_CCK			0x0514
#define REG_SIFS_OFDM			0x0516
#define REG_TSFTR_SYN_OFFSET		0x0518
#define REG_AGGR_BREAK_TIME		0x051a
#define REG_SLOT			0x051b
#define REG_TX_PTCL_CTRL		0x0520
#define REG_TXPAUSE			0x0522
#define REG_DIS_TXREQ_CLR		0x0523
#define REG_RD_CTRL			0x0524
#define REG_TBTT_PROHIBIT		0x0540
#define REG_RD_NAV_NXT			0x0544
#define REG_NAV_PROT_LEN		0x0546

#define REG_BEACON_CTRL			0x0550
#define REG_BEACON_CTRL_1		0x0551
#define  BEACON_ATIM			BIT(0)
#define  BEACON_CTRL_MBSSID		BIT(1)
#define  BEACON_CTRL_TX_BEACON_RPT	BIT(2)
#define  BEACON_FUNCTION_ENABLE		BIT(3)
#define  BEACON_DISABLE_TSF_UPDATE	BIT(4)

#define REG_MBID_NUM			0x0552
#define REG_DUAL_TSF_RST		0x0553
#define  DUAL_TSF_RESET_TSF0		BIT(0)
#define  DUAL_TSF_RESET_TSF1		BIT(1)
#define  DUAL_TSF_RESET_P2P		BIT(4)
#define  DUAL_TSF_TX_OK			BIT(5)

/*  The same as REG_MBSSID_BCN_SPACE */
#define REG_BCN_INTERVAL		0x0554
#define REG_MBSSID_BCN_SPACE		0x0554

#define REG_DRIVER_EARLY_INT		0x0558
#define  DRIVER_EARLY_INT_TIME		5

#define REG_BEACON_DMA_TIME		0x0559
#define  BEACON_DMA_ATIME_INT_TIME	2

#define REG_ATIMWND			0x055a
#define REG_USTIME_TSF_8723B		0x055c
#define REG_BCN_MAX_ERR			0x055d
#define REG_RXTSF_OFFSET_CCK		0x055e
#define REG_RXTSF_OFFSET_OFDM		0x055f
#define REG_TSFTR			0x0560
#define REG_TSFTR1			0x0568
#define REG_INIT_TSFTR			0x0564
#define REG_ATIMWND_1			0x0570
#define REG_PSTIMER			0x0580
#define REG_TIMER0			0x0584
#define REG_TIMER1			0x0588
#define REG_ACM_HW_CTRL			0x05c0
#define  ACM_HW_CTRL_BK			BIT(0)
#define  ACM_HW_CTRL_BE			BIT(1)
#define  ACM_HW_CTRL_VI			BIT(2)
#define  ACM_HW_CTRL_VO			BIT(3)
#define REG_ACM_RST_CTRL		0x05c1
#define REG_ACMAVG			0x05c2
#define REG_VO_ADMTIME			0x05c4
#define REG_VI_ADMTIME			0x05c6
#define REG_BE_ADMTIME			0x05c8
#define REG_EDCA_RANDOM_GEN		0x05cc
#define REG_SCH_TXCMD			0x05d0

/* define REG_FW_TSF_SYNC_CNT		0x04a0 */
#define REG_FW_RESET_TSF_CNT_1		0x05fc
#define REG_FW_RESET_TSF_CNT_0		0x05fd
#define REG_FW_BCN_DIS_CNT		0x05fe

/* 0x0600 ~ 0x07FF  WMAC Configuration */
#define REG_APSD_CTRL			0x0600
#define  APSD_CTRL_OFF			BIT(6)
#define  APSD_CTRL_OFF_STATUS		BIT(7)
#define REG_BW_OPMODE			0x0603
#define  BW_OPMODE_20MHZ		BIT(2)
#define  BW_OPMODE_5G			BIT(1)
#define  BW_OPMODE_11J			BIT(0)

#define REG_TCR				0x0604

/* Receive Configuration Register */
#define REG_RCR				0x0608
#define  RCR_ACCEPT_AP			BIT(0)  /* Accept all unicast packet */
#define  RCR_ACCEPT_PHYS_MATCH		BIT(1)  /* Accept phys match packet */
#define  RCR_ACCEPT_MCAST		BIT(2)
#define  RCR_ACCEPT_BCAST		BIT(3)
#define  RCR_ACCEPT_ADDR3		BIT(4)  /* Accept address 3 match
						 packet */
#define  RCR_ACCEPT_PM			BIT(5)  /* Accept power management
						 packet */
#define  RCR_CHECK_BSSID_MATCH		BIT(6)  /* Accept BSSID match packet */
#define  RCR_CHECK_BSSID_BEACON		BIT(7)  /* Accept BSSID match packet
						 (Rx beacon, probe rsp) */
#define  RCR_ACCEPT_CRC32		BIT(8)  /* Accept CRC32 error packet */
#define  RCR_ACCEPT_ICV			BIT(9)  /* Accept ICV error packet */
#define  RCR_ACCEPT_DATA_FRAME		BIT(11) /* Accept all data pkt or use
						   REG_RXFLTMAP2 */
#define  RCR_ACCEPT_CTRL_FRAME		BIT(12) /* Accept all control pkt or use
						   REG_RXFLTMAP1 */
#define  RCR_ACCEPT_MGMT_FRAME		BIT(13) /* Accept all mgmt pkt or use
						   REG_RXFLTMAP0 */
#define  RCR_HTC_LOC_CTRL		BIT(14) /* MFC<--HTC=1 MFC-->HTC=0 */
#define  RCR_UC_DATA_PKT_INT_ENABLE	BIT(16) /* Enable unicast data packet
						   interrupt */
#define  RCR_BM_DATA_PKT_INT_ENABLE	BIT(17) /* Enable broadcast data packet
						   interrupt */
#define  RCR_TIM_PARSER_ENABLE		BIT(18) /* Enable RX beacon TIM parser*/
#define  RCR_MFBEN			BIT(22)
#define  RCR_LSIG_ENABLE		BIT(23) /* Enable LSIG TXOP Protection
						   function. Search KEYCAM for
						   each rx packet to check if
						   LSIGEN bit is set. */
#define  RCR_MULTI_BSSID_ENABLE		BIT(24) /* Enable Multiple BssId */
#define  RCR_FORCE_ACK			BIT(26)
#define  RCR_ACCEPT_BA_SSN		BIT(27) /* Accept BA SSN */
#define  RCR_APPEND_PHYSTAT		BIT(28)
#define  RCR_APPEND_ICV			BIT(29)
#define  RCR_APPEND_MIC			BIT(30)
#define  RCR_APPEND_FCS			BIT(31) /* WMAC append FCS after */

#define REG_RX_PKT_LIMIT		0x060c
#define REG_RX_DLK_TIME			0x060d
#define REG_RX_DRVINFO_SZ		0x060f

#define REG_MACID			0x0610
#define REG_BSSID			0x0618
#define REG_MAR				0x0620
#define REG_MBIDCAMCFG			0x0628

#define REG_USTIME_EDCA			0x0638
#define REG_MAC_SPEC_SIFS		0x063a

/*  20100719 Joseph: Hardware register definition change. (HW datasheet v54) */
	/*  [15:8]SIFS_R2T_OFDM, [7:0]SIFS_R2T_CCK */
#define REG_R2T_SIFS			0x063c
	/*  [15:8]SIFS_T2T_OFDM, [7:0]SIFS_T2T_CCK */
#define REG_T2T_SIFS			0x063e
#define REG_ACKTO			0x0640
#define REG_CTS2TO			0x0641
#define REG_EIFS			0x0642

/* WMA, BA, CCX */
#define REG_NAV_CTRL			0x0650
/* In units of 128us */
#define REG_NAV_UPPER			0x0652
#define  NAV_UPPER_UNIT			128

#define REG_BACAMCMD			0x0654
#define REG_BACAMCONTENT		0x0658
#define REG_LBDLY			0x0660
#define REG_FWDLY			0x0661
#define REG_RXERR_RPT			0x0664
#define REG_WMAC_TRXPTCL_CTL		0x0668
#define  WMAC_TRXPTCL_CTL_BW_MASK	(BIT(7) | BIT(8))
#define  WMAC_TRXPTCL_CTL_BW_20		0
#define  WMAC_TRXPTCL_CTL_BW_40		BIT(7)
#define  WMAC_TRXPTCL_CTL_BW_80		BIT(8)

/*  Security */
#define REG_CAM_CMD			0x0670
#define  CAM_CMD_POLLING		BIT(31)
#define  CAM_CMD_WRITE			BIT(16)
#define  CAM_CMD_KEY_SHIFT		3
#define REG_CAM_WRITE			0x0674
#define  CAM_WRITE_VALID		BIT(15)
#define REG_CAM_READ			0x0678
#define REG_CAM_DEBUG			0x067c
#define REG_SECURITY_CFG		0x0680
#define  SEC_CFG_TX_USE_DEFKEY		BIT(0)
#define  SEC_CFG_RX_USE_DEFKEY		BIT(1)
#define  SEC_CFG_TX_SEC_ENABLE		BIT(2)
#define  SEC_CFG_RX_SEC_ENABLE		BIT(3)
#define  SEC_CFG_SKBYA2			BIT(4)
#define  SEC_CFG_NO_SKMC		BIT(5)
#define  SEC_CFG_TXBC_USE_DEFKEY	BIT(6)
#define  SEC_CFG_RXBC_USE_DEFKEY	BIT(7)

/*  Power */
#define REG_WOW_CTRL			0x0690
#define REG_PSSTATUS			0x0691
#define REG_PS_RX_INFO			0x0692
#define REG_LPNAV_CTRL			0x0694
#define REG_WKFMCAM_CMD			0x0698
#define REG_WKFMCAM_RWD			0x069c

/*
 * RX Filters: each bit corresponds to the numerical value of the subtype.
 * If it is set the subtype frame type is passed. The filter is only used when
 * the RCR_ACCEPT_DATA_FRAME, RCR_ACCEPT_CTRL_FRAME, RCR_ACCEPT_MGMT_FRAME bit
 * in the RCR are low.
 *
 * Example: Beacon subtype is binary 1000 which is decimal 8 so we have to set
 * bit 8 (0x100) in REG_RXFLTMAP0 to enable reception.
 */
#define REG_RXFLTMAP0			0x06a0	/* Management frames */
#define REG_RXFLTMAP1			0x06a2	/* Control frames */
#define REG_RXFLTMAP2			0x06a4	/* Data frames */

#define REG_BCN_PSR_RPT			0x06a8
#define REG_CALB32K_CTRL		0x06ac
#define REG_PKT_MON_CTRL		0x06b4
#define REG_BT_COEX_TABLE1		0x06c0
#define REG_BT_COEX_TABLE2		0x06c4
#define REG_BT_COEX_TABLE3		0x06c8
#define REG_BT_COEX_TABLE4		0x06cc
#define REG_WMAC_RESP_TXINFO		0x06d8

#define REG_MACID1			0x0700
#define REG_BSSID1			0x0708

/*
 * This seems to be 8723bu specific
 */
#define REG_BT_CONTROL_8723BU		0x0764
#define  BT_CONTROL_BT_GRANT		BIT(12)

#define REG_WLAN_ACT_CONTROL_8723B	0x076e

#define REG_FPGA0_RF_MODE		0x0800
#define  FPGA_RF_MODE			BIT(0)
#define  FPGA_RF_MODE_JAPAN		BIT(1)
#define  FPGA_RF_MODE_CCK		BIT(24)
#define  FPGA_RF_MODE_OFDM		BIT(25)

#define REG_FPGA0_TX_INFO		0x0804
#define REG_FPGA0_PSD_FUNC		0x0808
#define REG_FPGA0_TX_GAIN		0x080c
#define REG_FPGA0_RF_TIMING1		0x0810
#define REG_FPGA0_RF_TIMING2		0x0814
#define REG_FPGA0_POWER_SAVE		0x0818
#define  FPGA0_PS_LOWER_CHANNEL		BIT(26)
#define  FPGA0_PS_UPPER_CHANNEL		BIT(27)

#define REG_FPGA0_XA_HSSI_PARM1		0x0820	/* RF 3 wire register */
#define  FPGA0_HSSI_PARM1_PI		BIT(8)
#define REG_FPGA0_XA_HSSI_PARM2		0x0824
#define REG_FPGA0_XB_HSSI_PARM1		0x0828
#define REG_FPGA0_XB_HSSI_PARM2		0x082c
#define  FPGA0_HSSI_3WIRE_DATA_LEN	0x800
#define  FPGA0_HSSI_3WIRE_ADDR_LEN	0x400
#define  FPGA0_HSSI_PARM2_ADDR_SHIFT	23
#define  FPGA0_HSSI_PARM2_ADDR_MASK	0x7f800000	/* 0xff << 23 */
#define  FPGA0_HSSI_PARM2_CCK_HIGH_PWR	BIT(9)
#define  FPGA0_HSSI_PARM2_EDGE_READ	BIT(31)

#define REG_TX_AGC_B_RATE18_06		0x0830
#define REG_TX_AGC_B_RATE54_24		0x0834
#define REG_TX_AGC_B_CCK1_55_MCS32	0x0838
#define REG_TX_AGC_B_MCS03_MCS00	0x083c

#define REG_FPGA0_XA_LSSI_PARM		0x0840
#define REG_FPGA0_XB_LSSI_PARM		0x0844
#define  FPGA0_LSSI_PARM_ADDR_SHIFT	20
#define  FPGA0_LSSI_PARM_ADDR_MASK	0x0ff00000
#define  FPGA0_LSSI_PARM_DATA_MASK	0x000fffff

#define REG_TX_AGC_B_MCS07_MCS04	0x0848
#define REG_TX_AGC_B_MCS11_MCS08	0x084c

#define REG_FPGA0_XCD_SWITCH_CTRL	0x085c

#define REG_FPGA0_XA_RF_INT_OE		0x0860	/* RF Channel switch */
#define REG_FPGA0_XB_RF_INT_OE		0x0864
#define  FPGA0_INT_OE_ANTENNA_AB_OPEN	0x000
#define  FPGA0_INT_OE_ANTENNA_A		BIT(8)
#define  FPGA0_INT_OE_ANTENNA_B		BIT(9)
#define  FPGA0_INT_OE_ANTENNA_MASK	(FPGA0_INT_OE_ANTENNA_A | \
					 FPGA0_INT_OE_ANTENNA_B)

#define REG_TX_AGC_B_MCS15_MCS12	0x0868
#define REG_TX_AGC_B_CCK11_A_CCK2_11	0x086c

#define REG_FPGA0_XAB_RF_SW_CTRL	0x0870
#define REG_FPGA0_XA_RF_SW_CTRL		0x0870	/* 16 bit */
#define REG_FPGA0_XB_RF_SW_CTRL		0x0872	/* 16 bit */
#define REG_FPGA0_XCD_RF_SW_CTRL	0x0874
#define REG_FPGA0_XC_RF_SW_CTRL		0x0874	/* 16 bit */
#define REG_FPGA0_XD_RF_SW_CTRL		0x0876	/* 16 bit */
#define  FPGA0_RF_3WIRE_DATA		BIT(0)
#define  FPGA0_RF_3WIRE_CLOC		BIT(1)
#define  FPGA0_RF_3WIRE_LOAD		BIT(2)
#define  FPGA0_RF_3WIRE_RW		BIT(3)
#define  FPGA0_RF_3WIRE_MASK		0xf
#define  FPGA0_RF_RFENV			BIT(4)
#define  FPGA0_RF_TRSW			BIT(5)	/* Useless now */
#define  FPGA0_RF_TRSWB			BIT(6)
#define  FPGA0_RF_ANTSW			BIT(8)
#define  FPGA0_RF_ANTSWB		BIT(9)
#define  FPGA0_RF_PAPE			BIT(10)
#define  FPGA0_RF_PAPE5G		BIT(11)
#define  FPGA0_RF_BD_CTRL_SHIFT		16

#define REG_FPGA0_XAB_RF_PARM		0x0878	/* Antenna select path in ODM */
#define REG_FPGA0_XA_RF_PARM		0x0878	/* 16 bit */
#define REG_FPGA0_XB_RF_PARM		0x087a	/* 16 bit */
#define REG_FPGA0_XCD_RF_PARM		0x087c
#define REG_FPGA0_XC_RF_PARM		0x087c	/* 16 bit */
#define REG_FPGA0_XD_RF_PARM		0x087e	/* 16 bit */
#define  FPGA0_RF_PARM_RFA_ENABLE	BIT(1)
#define  FPGA0_RF_PARM_RFB_ENABLE	BIT(17)
#define  FPGA0_RF_PARM_CLK_GATE		BIT(31)

#define REG_FPGA0_ANALOG1		0x0880
#define REG_FPGA0_ANALOG2		0x0884
#define  FPGA0_ANALOG2_20MHZ		BIT(10)
#define REG_FPGA0_ANALOG3		0x0888
#define REG_FPGA0_ANALOG4		0x088c

#define REG_NHM_TH9_TH10_8723B		0x0890
#define REG_NHM_TIMER_8723B		0x0894
#define REG_NHM_TH3_TO_TH0_8723B	0x0898
#define REG_NHM_TH7_TO_TH4_8723B	0x089c

#define REG_FPGA0_XA_LSSI_READBACK	0x08a0	/* Tranceiver LSSI Readback */
#define REG_FPGA0_XB_LSSI_READBACK	0x08a4
#define REG_HSPI_XA_READBACK		0x08b8	/* Transceiver A HSPI read */
#define REG_HSPI_XB_READBACK		0x08bc	/* Transceiver B HSPI read */

#define REG_FPGA1_RF_MODE		0x0900

#define REG_FPGA1_TX_INFO		0x090c
#define REG_DPDT_CTRL			0x092c	/* 8723BU */
#define REG_RFE_CTRL_ANTA_SRC		0x0930	/* 8723BU */
#define REG_RFE_PATH_SELECT		0x0940	/* 8723BU */
#define REG_RFE_BUFFER			0x0944	/* 8723BU */
#define REG_S0S1_PATH_SWITCH		0x0948	/* 8723BU */

#define REG_CCK0_SYSTEM			0x0a00
#define  CCK0_SIDEBAND			BIT(4)

#define REG_CCK0_AFE_SETTING		0x0a04
#define  CCK0_AFE_RX_MASK		0x0f000000
#define  CCK0_AFE_RX_ANT_AB		BIT(24)
#define  CCK0_AFE_RX_ANT_A		0
#define  CCK0_AFE_RX_ANT_B		(BIT(24) | BIT(26))

#define REG_CONFIG_ANT_A		0x0b68
#define REG_CONFIG_ANT_B		0x0b6c

#define REG_OFDM0_TRX_PATH_ENABLE	0x0c04
#define OFDM_RF_PATH_RX_MASK		0x0f
#define OFDM_RF_PATH_RX_A		BIT(0)
#define OFDM_RF_PATH_RX_B		BIT(1)
#define OFDM_RF_PATH_RX_C		BIT(2)
#define OFDM_RF_PATH_RX_D		BIT(3)
#define OFDM_RF_PATH_TX_MASK		0xf0
#define OFDM_RF_PATH_TX_A		BIT(4)
#define OFDM_RF_PATH_TX_B		BIT(5)
#define OFDM_RF_PATH_TX_C		BIT(6)
#define OFDM_RF_PATH_TX_D		BIT(7)

#define REG_OFDM0_TR_MUX_PAR		0x0c08

#define REG_OFDM0_FA_RSTC		0x0c0c

#define REG_OFDM0_XA_RX_IQ_IMBALANCE	0x0c14
#define REG_OFDM0_XB_RX_IQ_IMBALANCE	0x0c1c

#define REG_OFDM0_ENERGY_CCA_THRES	0x0c4c

#define REG_OFDM0_RX_D_SYNC_PATH	0x0c40
#define  OFDM0_SYNC_PATH_NOTCH_FILTER	BIT(1)

#define REG_OFDM0_XA_AGC_CORE1		0x0c50
#define REG_OFDM0_XA_AGC_CORE2		0x0c54
#define REG_OFDM0_XB_AGC_CORE1		0x0c58
#define REG_OFDM0_XB_AGC_CORE2		0x0c5c
#define REG_OFDM0_XC_AGC_CORE1		0x0c60
#define REG_OFDM0_XC_AGC_CORE2		0x0c64
#define REG_OFDM0_XD_AGC_CORE1		0x0c68
#define REG_OFDM0_XD_AGC_CORE2		0x0c6c
#define  OFDM0_X_AGC_CORE1_IGI_MASK	0x0000007F

#define REG_OFDM0_AGC_PARM1		0x0c70

#define REG_OFDM0_AGCR_SSI_TABLE	0x0c78

#define REG_OFDM0_XA_TX_IQ_IMBALANCE	0x0c80
#define REG_OFDM0_XB_TX_IQ_IMBALANCE	0x0c88
#define REG_OFDM0_XC_TX_IQ_IMBALANCE	0x0c90
#define REG_OFDM0_XD_TX_IQ_IMBALANCE	0x0c98

#define REG_OFDM0_XC_TX_AFE		0x0c94
#define REG_OFDM0_XD_TX_AFE		0x0c9c

#define REG_OFDM0_RX_IQ_EXT_ANTA	0x0ca0

/* 8723bu */
#define REG_OFDM0_TX_PSDO_NOISE_WEIGHT	0x0ce4

#define REG_OFDM1_LSTF			0x0d00
#define  OFDM_LSTF_PRIME_CH_LOW		BIT(10)
#define  OFDM_LSTF_PRIME_CH_HIGH	BIT(11)
#define  OFDM_LSTF_PRIME_CH_MASK	(OFDM_LSTF_PRIME_CH_LOW | \
					 OFDM_LSTF_PRIME_CH_HIGH)
#define  OFDM_LSTF_CONTINUE_TX		BIT(28)
#define  OFDM_LSTF_SINGLE_CARRIER	BIT(29)
#define  OFDM_LSTF_SINGLE_TONE		BIT(30)
#define  OFDM_LSTF_MASK			0x70000000

#define REG_OFDM1_TRX_PATH_ENABLE	0x0d04

#define REG_TX_AGC_A_RATE18_06		0x0e00
#define REG_TX_AGC_A_RATE54_24		0x0e04
#define REG_TX_AGC_A_CCK1_MCS32		0x0e08
#define REG_TX_AGC_A_MCS03_MCS00	0x0e10
#define REG_TX_AGC_A_MCS07_MCS04	0x0e14
#define REG_TX_AGC_A_MCS11_MCS08	0x0e18
#define REG_TX_AGC_A_MCS15_MCS12	0x0e1c

#define REG_FPGA0_IQK			0x0e28

#define REG_TX_IQK_TONE_A		0x0e30
#define REG_RX_IQK_TONE_A		0x0e34
#define REG_TX_IQK_PI_A			0x0e38
#define REG_RX_IQK_PI_A			0x0e3c

#define REG_TX_IQK			0x0e40
#define REG_RX_IQK			0x0e44
#define REG_IQK_AGC_PTS			0x0e48
#define REG_IQK_AGC_RSP			0x0e4c
#define REG_TX_IQK_TONE_B		0x0e50
#define REG_RX_IQK_TONE_B		0x0e54
#define REG_TX_IQK_PI_B			0x0e58
#define REG_RX_IQK_PI_B			0x0e5c
#define REG_IQK_AGC_CONT		0x0e60

#define REG_BLUETOOTH			0x0e6c
#define REG_RX_WAIT_CCA			0x0e70
#define REG_TX_CCK_RFON			0x0e74
#define REG_TX_CCK_BBON			0x0e78
#define REG_TX_OFDM_RFON		0x0e7c
#define REG_TX_OFDM_BBON		0x0e80
#define REG_TX_TO_RX			0x0e84
#define REG_TX_TO_TX			0x0e88
#define REG_RX_CCK			0x0e8c

#define REG_TX_POWER_BEFORE_IQK_A	0x0e94
#define REG_TX_POWER_AFTER_IQK_A	0x0e9c

#define REG_RX_POWER_BEFORE_IQK_A	0x0ea0
#define REG_RX_POWER_BEFORE_IQK_A_2	0x0ea4
#define REG_RX_POWER_AFTER_IQK_A	0x0ea8
#define REG_RX_POWER_AFTER_IQK_A_2	0x0eac

#define REG_TX_POWER_BEFORE_IQK_B	0x0eb4
#define REG_TX_POWER_AFTER_IQK_B	0x0ebc

#define REG_RX_POWER_BEFORE_IQK_B	0x0ec0
#define REG_RX_POWER_BEFORE_IQK_B_2	0x0ec4
#define REG_RX_POWER_AFTER_IQK_B	0x0ec8
#define REG_RX_POWER_AFTER_IQK_B_2	0x0ecc

#define REG_RX_OFDM			0x0ed0
#define REG_RX_WAIT_RIFS		0x0ed4
#define REG_RX_TO_RX			0x0ed8
#define REG_STANDBY			0x0edc
#define REG_SLEEP			0x0ee0
#define REG_PMPD_ANAEN			0x0eec

#define REG_FW_START_ADDRESS		0x1000

#define REG_USB_INFO			0xfe17
#define REG_USB_HIMR			0xfe38
#define  USB_HIMR_TIMEOUT2		BIT(31)
#define  USB_HIMR_TIMEOUT1		BIT(30)
#define  USB_HIMR_PSTIMEOUT		BIT(29)
#define  USB_HIMR_GTINT4		BIT(28)
#define  USB_HIMR_GTINT3		BIT(27)
#define  USB_HIMR_TXBCNERR		BIT(26)
#define  USB_HIMR_TXBCNOK		BIT(25)
#define  USB_HIMR_TSF_BIT32_TOGGLE	BIT(24)
#define  USB_HIMR_BCNDMAINT3		BIT(23)
#define  USB_HIMR_BCNDMAINT2		BIT(22)
#define  USB_HIMR_BCNDMAINT1		BIT(21)
#define  USB_HIMR_BCNDMAINT0		BIT(20)
#define  USB_HIMR_BCNDOK3		BIT(19)
#define  USB_HIMR_BCNDOK2		BIT(18)
#define  USB_HIMR_BCNDOK1		BIT(17)
#define  USB_HIMR_BCNDOK0		BIT(16)
#define  USB_HIMR_HSISR_IND		BIT(15)
#define  USB_HIMR_BCNDMAINT_E		BIT(14)
/* RSVD	BIT(13) */
#define  USB_HIMR_CTW_END		BIT(12)
/* RSVD	BIT(11) */
#define  USB_HIMR_C2HCMD		BIT(10)
#define  USB_HIMR_CPWM2			BIT(9)
#define  USB_HIMR_CPWM			BIT(8)
#define  USB_HIMR_HIGHDOK		BIT(7)	/*  High Queue DMA OK
						    Interrupt */
#define  USB_HIMR_MGNTDOK		BIT(6)	/*  Management Queue DMA OK
						    Interrupt */
#define  USB_HIMR_BKDOK			BIT(5)	/*  AC_BK DMA OK Interrupt */
#define  USB_HIMR_BEDOK			BIT(4)	/*  AC_BE DMA OK Interrupt */
#define  USB_HIMR_VIDOK			BIT(3)	/*  AC_VI DMA OK Interrupt */
#define  USB_HIMR_VODOK			BIT(2)	/*  AC_VO DMA Interrupt */
#define  USB_HIMR_RDU			BIT(1)	/*  Receive Descriptor
						    Unavailable */
#define  USB_HIMR_ROK			BIT(0)	/*  Receive DMA OK Interrupt */

#define REG_USB_SPECIAL_OPTION		0xfe55
#define  USB_SPEC_USB_AGG_ENABLE	BIT(3)	/* Enable USB aggregation */
#define  USB_SPEC_INT_BULK_SELECT	BIT(4)	/* Use interrupt endpoint to
						   deliver interrupt packet.
						   0: Use int, 1: use bulk */
#define REG_USB_HRPWM			0xfe58
#define REG_USB_DMA_AGG_TO		0xfe5b
#define REG_USB_AGG_TIMEOUT		0xfe5c
#define REG_USB_AGG_THRESH		0xfe5d

#define REG_NORMAL_SIE_VID		0xfe60	/* 0xfe60 - 0xfe61 */
#define REG_NORMAL_SIE_PID		0xfe62	/* 0xfe62 - 0xfe63 */
#define REG_NORMAL_SIE_OPTIONAL		0xfe64
#define REG_NORMAL_SIE_EP		0xfe65	/* 0xfe65 - 0xfe67 */
#define REG_NORMAL_SIE_EP_TX		0xfe66
#define  NORMAL_SIE_EP_TX_HIGH_MASK	0x000f
#define  NORMAL_SIE_EP_TX_NORMAL_MASK	0x00f0
#define  NORMAL_SIE_EP_TX_LOW_MASK	0x0f00

#define REG_NORMAL_SIE_PHY		0xfe68	/* 0xfe68 - 0xfe6b */
#define REG_NORMAL_SIE_OPTIONAL2	0xfe6c
#define REG_NORMAL_SIE_GPS_EP		0xfe6d	/* RTL8723 only */
#define REG_NORMAL_SIE_MAC_ADDR		0xfe70	/* 0xfe70 - 0xfe75 */
#define REG_NORMAL_SIE_STRING		0xfe80	/* 0xfe80 - 0xfedf */

/* RF6052 registers */
#define RF6052_REG_AC			0x00
#define RF6052_REG_IQADJ_G1		0x01
#define RF6052_REG_IQADJ_G2		0x02
#define RF6052_REG_BS_PA_APSET_G1_G4	0x03
#define RF6052_REG_BS_PA_APSET_G5_G8	0x04
#define RF6052_REG_POW_TRSW		0x05
#define RF6052_REG_GAIN_RX		0x06
#define RF6052_REG_GAIN_TX		0x07
#define RF6052_REG_TXM_IDAC		0x08
#define RF6052_REG_IPA_G		0x09
#define RF6052_REG_TXBIAS_G		0x0a
#define RF6052_REG_TXPA_AG		0x0b
#define RF6052_REG_IPA_A		0x0c
#define RF6052_REG_TXBIAS_A		0x0d
#define RF6052_REG_BS_PA_APSET_G9_G11	0x0e
#define RF6052_REG_BS_IQGEN		0x0f
#define RF6052_REG_MODE1		0x10
#define RF6052_REG_MODE2		0x11
#define RF6052_REG_RX_AGC_HP		0x12
#define RF6052_REG_TX_AGC		0x13
#define RF6052_REG_BIAS			0x14
#define RF6052_REG_IPA			0x15
#define RF6052_REG_TXBIAS		0x16
#define RF6052_REG_POW_ABILITY		0x17
#define RF6052_REG_MODE_AG		0x18	/* RF channel and BW switch */
#define  MODE_AG_CHANNEL_MASK		0x3ff
#define  MODE_AG_CHANNEL_20MHZ		BIT(10)
#define  MODE_AG_BW_MASK		(BIT(10) | BIT(11))
#define  MODE_AG_BW_20MHZ_8723B		(BIT(10) | BIT(11))
#define  MODE_AG_BW_40MHZ_8723B		BIT(10)
#define  MODE_AG_BW_80MHZ_8723B		0

#define RF6052_REG_TOP			0x19
#define RF6052_REG_RX_G1		0x1a
#define RF6052_REG_RX_G2		0x1b
#define RF6052_REG_RX_BB2		0x1c
#define RF6052_REG_RX_BB1		0x1d
#define RF6052_REG_RCK1			0x1e
#define RF6052_REG_RCK2			0x1f
#define RF6052_REG_TX_G1		0x20
#define RF6052_REG_TX_G2		0x21
#define RF6052_REG_TX_G3		0x22
#define RF6052_REG_TX_BB1		0x23
#define RF6052_REG_T_METER		0x24
#define RF6052_REG_SYN_G1		0x25	/* RF TX Power control */
#define RF6052_REG_SYN_G2		0x26	/* RF TX Power control */
#define RF6052_REG_SYN_G3		0x27	/* RF TX Power control */
#define RF6052_REG_SYN_G4		0x28	/* RF TX Power control */
#define RF6052_REG_SYN_G5		0x29	/* RF TX Power control */
#define RF6052_REG_SYN_G6		0x2a	/* RF TX Power control */
#define RF6052_REG_SYN_G7		0x2b	/* RF TX Power control */
#define RF6052_REG_SYN_G8		0x2c	/* RF TX Power control */

#define RF6052_REG_RCK_OS		0x30	/* RF TX PA control */

#define RF6052_REG_TXPA_G1		0x31	/* RF TX PA control */
#define RF6052_REG_TXPA_G2		0x32	/* RF TX PA control */
#define RF6052_REG_TXPA_G3		0x33	/* RF TX PA control */

/*
 * NextGen regs: 8723BU
 */
#define RF6052_REG_T_METER_8723B	0x42
#define RF6052_REG_UNKNOWN_43		0x43
#define RF6052_REG_UNKNOWN_55		0x55
#define RF6052_REG_UNKNOWN_56		0x56
#define RF6052_REG_S0S1			0xb0
#define RF6052_REG_UNKNOWN_DF		0xdf
#define RF6052_REG_UNKNOWN_ED		0xed
#define RF6052_REG_WE_LUT		0xef
