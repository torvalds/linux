/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef __REALTEK_92S_REG_H__
#define __REALTEK_92S_REG_H__

/* 1. System Configuration Registers  */
#define	REG_SYS_ISO_CTRL			0x0000
#define	REG_SYS_FUNC_EN				0x0002
#define	PMC_FSM					0x0004
#define	SYS_CLKR				0x0008
#define	EPROM_CMD				0x000A
#define	EE_VPD					0x000C
#define	AFE_MISC				0x0010
#define	SPS0_CTRL				0x0011
#define	SPS1_CTRL				0x0018
#define	RF_CTRL					0x001F
#define	LDOA15_CTRL				0x0020
#define	LDOV12D_CTRL				0x0021
#define	LDOHCI12_CTRL				0x0022
#define	LDO_USB_SDIO				0x0023
#define	LPLDO_CTRL				0x0024
#define	AFE_XTAL_CTRL				0x0026
#define	AFE_PLL_CTRL				0x0028
#define	REG_EFUSE_CTRL				0x0030
#define	REG_EFUSE_TEST				0x0034
#define	PWR_DATA				0x0038
#define	DBG_PORT				0x003A
#define	DPS_TIMER				0x003C
#define	RCLK_MON				0x003E

/* 2. Command Control Registers	  */
#define	CMDR					0x0040
#define	TXPAUSE					0x0042
#define	LBKMD_SEL				0x0043
#define	TCR					0x0044
#define	RCR					0x0048
#define	MSR					0x004C
#define	SYSF_CFG				0x004D
#define	RX_PKY_LIMIT				0x004E
#define	MBIDCTRL				0x004F

/* 3. MACID Setting Registers	 */
#define	MACIDR					0x0050
#define	MACIDR0					0x0050
#define	MACIDR4					0x0054
#define	BSSIDR					0x0058
#define	HWVID					0x005E
#define	MAR					0x0060
#define	MBIDCAMCONTENT				0x0068
#define	MBIDCAMCFG				0x0070
#define	BUILDTIME				0x0074
#define	BUILDUSER				0x0078

#define	IDR0					MACIDR0
#define	IDR4					MACIDR4

/* 4. Timing Control Registers	 */
#define	TSFR					0x0080
#define	SLOT_TIME				0x0089
#define	USTIME					0x008A
#define	SIFS_CCK				0x008C
#define	SIFS_OFDM				0x008E
#define	PIFS_TIME				0x0090
#define	ACK_TIMEOUT				0x0091
#define	EIFSTR					0x0092
#define	BCN_INTERVAL				0x0094
#define	ATIMWND					0x0096
#define	BCN_DRV_EARLY_INT			0x0098
#define	BCN_DMATIME				0x009A
#define	BCN_ERR_THRESH				0x009C
#define	MLT					0x009D
#define	RSVD_MAC_TUNE_US			0x009E

/* 5. FIFO Control Registers	  */
#define RQPN					0x00A0
#define	RQPN1					0x00A0
#define	RQPN2					0x00A1
#define	RQPN3					0x00A2
#define	RQPN4					0x00A3
#define	RQPN5					0x00A4
#define	RQPN6					0x00A5
#define	RQPN7					0x00A6
#define	RQPN8					0x00A7
#define	RQPN9					0x00A8
#define	RQPN10					0x00A9
#define	LD_RQPN					0x00AB
#define	RXFF_BNDY				0x00AC
#define	RXRPT_BNDY				0x00B0
#define	TXPKTBUF_PGBNDY				0x00B4
#define	PBP					0x00B5
#define	RXDRVINFO_SZ				0x00B6
#define	TXFF_STATUS				0x00B7
#define	RXFF_STATUS				0x00B8
#define	TXFF_EMPTY_TH				0x00B9
#define	SDIO_RX_BLKSZ				0x00BC
#define	RXDMA					0x00BD
#define	RXPKT_NUM				0x00BE
#define	C2HCMD_UDT_SIZE				0x00C0
#define	C2HCMD_UDT_ADDR				0x00C2
#define	FIFOPAGE1				0x00C4
#define	FIFOPAGE2				0x00C8
#define	FIFOPAGE3				0x00CC
#define	FIFOPAGE4				0x00D0
#define	FIFOPAGE5				0x00D4
#define	FW_RSVD_PG_CRTL				0x00D8
#define	RXDMA_AGG_PG_TH				0x00D9
#define	TXDESC_MSK				0x00DC
#define	TXRPTFF_RDPTR				0x00E0
#define	TXRPTFF_WTPTR				0x00E4
#define	C2HFF_RDPTR				0x00E8
#define	C2HFF_WTPTR				0x00EC
#define	RXFF0_RDPTR				0x00F0
#define	RXFF0_WTPTR				0x00F4
#define	RXFF1_RDPTR				0x00F8
#define	RXFF1_WTPTR				0x00FC
#define	RXRPT0_RDPTR				0x0100
#define	RXRPT0_WTPTR				0x0104
#define	RXRPT1_RDPTR				0x0108
#define	RXRPT1_WTPTR				0x010C
#define	RX0_UDT_SIZE				0x0110
#define	RX1PKTNUM				0x0114
#define	RXFILTERMAP				0x0116
#define	RXFILTERMAP_GP1				0x0118
#define	RXFILTERMAP_GP2				0x011A
#define	RXFILTERMAP_GP3				0x011C
#define	BCNQ_CTRL				0x0120
#define	MGTQ_CTRL				0x0124
#define	HIQ_CTRL				0x0128
#define	VOTID7_CTRL				0x012c
#define	VOTID6_CTRL				0x0130
#define	VITID5_CTRL				0x0134
#define	VITID4_CTRL				0x0138
#define	BETID3_CTRL				0x013c
#define	BETID0_CTRL				0x0140
#define	BKTID2_CTRL				0x0144
#define	BKTID1_CTRL				0x0148
#define	CMDQ_CTRL				0x014c
#define	TXPKT_NUM_CTRL				0x0150
#define	TXQ_PGADD				0x0152
#define	TXFF_PG_NUM				0x0154
#define	TRXDMA_STATUS				0x0156

/* 6. Adaptive Control Registers   */
#define	INIMCS_SEL				0x0160
#define	TX_RATE_REG				INIMCS_SEL
#define	INIRTSMCS_SEL				0x0180
#define	RRSR					0x0181
#define	ARFR0					0x0184
#define	ARFR1					0x0188
#define	ARFR2					0x018C
#define	ARFR3					0x0190
#define	ARFR4					0x0194
#define	ARFR5					0x0198
#define	ARFR6					0x019C
#define	ARFR7					0x01A0
#define	AGGLEN_LMT_H				0x01A7
#define	AGGLEN_LMT_L				0x01A8
#define	DARFRC					0x01B0
#define	RARFRC					0x01B8
#define	MCS_TXAGC				0x01C0
#define	CCK_TXAGC				0x01C8

/* 7. EDCA Setting Registers */
#define	EDCAPARA_VO				0x01D0
#define	EDCAPARA_VI				0x01D4
#define	EDCAPARA_BE				0x01D8
#define	EDCAPARA_BK				0x01DC
#define	BCNTCFG					0x01E0
#define	CWRR					0x01E2
#define	ACMAVG					0x01E4
#define	AcmHwCtrl				0x01E7
#define	VO_ADMTM				0x01E8
#define	VI_ADMTM				0x01EC
#define	BE_ADMTM				0x01F0
#define	RETRY_LIMIT				0x01F4
#define	SG_RATE					0x01F6

/* 8. WMAC, BA and CCX related Register. */
#define	NAV_CTRL				0x0200
#define	BW_OPMODE				0x0203
#define	BACAMCMD				0x0204
#define	BACAMCONTENT				0x0208

/* the 0x2xx register WMAC definition */
#define	LBDLY					0x0210
#define	FWDLY					0x0211
#define	HWPC_RX_CTRL				0x0218
#define	MQIR					0x0220
#define	MAIR					0x0222
#define	MSIR					0x0224
#define	CLM_RESULT				0x0227
#define	NHM_RPI_CNT				0x0228
#define	RXERR_RPT				0x0230
#define	NAV_PROT_LEN				0x0234
#define	CFEND_TH				0x0236
#define	AMPDU_MIN_SPACE				0x0237
#define	TXOP_STALL_CTRL				0x0238

/* 9. Security Control Registers */
#define	REG_RWCAM				0x0240
#define	REG_WCAMI				0x0244
#define	REG_RCAMO				0x0248
#define	REG_CAMDBG				0x024C
#define	REG_SECR				0x0250

/* 10. Power Save Control Registers */
#define	WOW_CTRL				0x0260
#define	PSSTATUS				0x0261
#define	PSSWITCH				0x0262
#define	MIMOPS_WAIT_PERIOD			0x0263
#define	LPNAV_CTRL				0x0264
#define	WFM0					0x0270
#define	WFM1					0x0280
#define	WFM2					0x0290
#define	WFM3					0x02A0
#define	WFM4					0x02B0
#define	WFM5					0x02C0
#define	WFCRC					0x02D0
#define	FW_RPT_REG				0x02c4

/* 11. General Purpose Registers */
#define	PSTIME					0x02E0
#define	TIMER0					0x02E4
#define	TIMER1					0x02E8
#define	GPIO_IN_SE				0x02EC
#define	GPIO_IO_SEL				0x02EE
#define	MAC_PINMUX_CFG				0x02F1
#define	LEDCFG					0x02F2
#define	PHY_REG					0x02F3
#define	PHY_REG_DATA				0x02F4
#define	REG_EFUSE_CLK				0x02F8

/* 12. Host Interrupt Status Registers */
#define	INTA_MASK				0x0300
#define	ISR					0x0308

/* 13. Test Mode and Debug Control Registers */
#define	DBG_PORT_SWITCH				0x003A
#define	BIST					0x0310
#define	DBS					0x0314
#define	CPUINST					0x0318
#define	CPUCAUSE				0x031C
#define	LBUS_ERR_ADDR				0x0320
#define	LBUS_ERR_CMD				0x0324
#define	LBUS_ERR_DATA_L				0x0328
#define	LBUS_ERR_DATA_H				0x032C
#define	LX_EXCEPTION_ADDR			0x0330
#define	WDG_CTRL				0x0334
#define	INTMTU					0x0338
#define	INTM					0x033A
#define	FDLOCKTURN0				0x033C
#define	FDLOCKTURN1				0x033D
#define	TRXPKTBUF_DBG_DATA			0x0340
#define	TRXPKTBUF_DBG_CTRL			0x0348
#define	DPLL					0x034A
#define	CBUS_ERR_ADDR				0x0350
#define	CBUS_ERR_CMD				0x0354
#define	CBUS_ERR_DATA_L				0x0358
#define	CBUS_ERR_DATA_H				0x035C
#define	USB_SIE_INTF_ADDR			0x0360
#define	USB_SIE_INTF_WD				0x0361
#define	USB_SIE_INTF_RD				0x0362
#define	USB_SIE_INTF_CTRL			0x0363
#define LBUS_MON_ADDR				0x0364
#define LBUS_ADDR_MASK				0x0368

/* Boundary is 0x37F */

/* 14. PCIE config register */
#define	TP_POLL					0x0500
#define	PM_CTRL					0x0502
#define	PCIF					0x0503

#define	THPDA					0x0514
#define	TMDA					0x0518
#define	TCDA					0x051C
#define	HDA					0x0520
#define	TVODA					0x0524
#define	TVIDA					0x0528
#define	TBEDA					0x052C
#define	TBKDA					0x0530
#define	TBDA					0x0534
#define	RCDA					0x0538
#define	RDQDA					0x053C
#define	DBI_WDATA				0x0540
#define	DBI_RDATA				0x0544
#define	DBI_CTRL				0x0548
#define	MDIO_DATA				0x0550
#define	MDIO_CTRL				0x0554
#define	PCI_RPWM				0x0561
#define	PCI_CPWM				0x0563

/* Config register	(Offset 0x800-) */
#define	PHY_CCA					0x803

/* Min Spacing related settings. */
#define	MAX_MSS_DENSITY_2T			0x13
#define	MAX_MSS_DENSITY_1T			0x0A

/* Rx DMA Control related settings */
#define	RXDMA_AGG_EN				BIT(7)

#define	RPWM					PCI_RPWM

/* Regsiter Bit and Content definition  */

#define	ISO_MD2PP				BIT(0)
#define	ISO_PA2PCIE				BIT(3)
#define	ISO_PLL2MD				BIT(4)
#define	ISO_PWC_DV2RP				BIT(11)
#define	ISO_PWC_RV2RP				BIT(12)


#define	FEN_MREGEN				BIT(15)
#define	FEN_DCORE				BIT(11)
#define	FEN_CPUEN				BIT(10)

#define	PAD_HWPD_IDN				BIT(22)

#define	SYS_CLKSEL_80M				BIT(0)
#define	SYS_PS_CLKSEL				BIT(1)
#define	SYS_CPU_CLKSEL				BIT(2)
#define	SYS_MAC_CLK_EN				BIT(11)
#define	SYS_SWHW_SEL				BIT(14)
#define	SYS_FWHW_SEL				BIT(15)

#define	CmdEEPROM_En				BIT(5)
#define	CmdEERPOMSEL				BIT(4)
#define	Cmd9346CR_9356SEL			BIT(4)

#define	AFE_MBEN				BIT(1)
#define	AFE_BGEN				BIT(0)

#define	SPS1_SWEN				BIT(1)
#define	SPS1_LDEN				BIT(0)

#define	RF_EN					BIT(0)
#define	RF_RSTB					BIT(1)
#define	RF_SDMRSTB				BIT(2)

#define	LDA15_EN				BIT(0)

#define	LDV12_EN				BIT(0)
#define	LDV12_SDBY				BIT(1)

#define	XTAL_GATE_AFE				BIT(10)

#define	APLL_EN					BIT(0)

#define	AFR_CardBEn				BIT(0)
#define	AFR_CLKRUN_SEL				BIT(1)
#define	AFR_FuncRegEn				BIT(2)

#define	APSDOFF_STATUS				BIT(15)
#define	APSDOFF					BIT(14)
#define	BBRSTN					BIT(13)
#define	BB_GLB_RSTN				BIT(12)
#define	SCHEDULE_EN				BIT(10)
#define	MACRXEN					BIT(9)
#define	MACTXEN					BIT(8)
#define	DDMA_EN					BIT(7)
#define	FW2HW_EN				BIT(6)
#define	RXDMA_EN				BIT(5)
#define	TXDMA_EN				BIT(4)
#define	HCI_RXDMA_EN				BIT(3)
#define	HCI_TXDMA_EN				BIT(2)

#define	StopHCCA				BIT(6)
#define	StopHigh				BIT(5)
#define	StopMgt					BIT(4)
#define	StopVO					BIT(3)
#define	StopVI					BIT(2)
#define	StopBE					BIT(1)
#define	StopBK					BIT(0)

#define	LBK_NORMAL				0x00
#define	LBK_MAC_LB				(BIT(0) | BIT(1) | BIT(3))
#define	LBK_MAC_DLB				(BIT(0) | BIT(1))
#define	LBK_DMA_LB				(BIT(0) | BIT(1) | BIT(2))

#define	TCP_OFDL_EN				BIT(25)
#define	HWPC_TX_EN				BIT(24)
#define	TXDMAPRE2FULL				BIT(23)
#define	DISCW					BIT(20)
#define	TCRICV					BIT(19)
#define	CfendForm				BIT(17)
#define	TCRCRC					BIT(16)
#define	FAKE_IMEM_EN				BIT(15)
#define	TSFRST					BIT(9)
#define	TSFEN					BIT(8)
#define	FWALLRDY				(BIT(0) | BIT(1) | BIT(2) | \
						BIT(3) | BIT(4) | BIT(5) | \
						BIT(6) | BIT(7))
#define	FWRDY					BIT(7)
#define	BASECHG					BIT(6)
#define	IMEM					BIT(5)
#define	DMEM_CODE_DONE				BIT(4)
#define	EXT_IMEM_CHK_RPT			BIT(3)
#define	EXT_IMEM_CODE_DONE			BIT(2)
#define	IMEM_CHK_RPT				BIT(1)
#define	IMEM_CODE_DONE				BIT(0)
#define	IMEM_CODE_DONE				BIT(0)
#define	IMEM_CHK_RPT				BIT(1)
#define	EMEM_CODE_DONE				BIT(2)
#define	EMEM_CHK_RPT				BIT(3)
#define	DMEM_CODE_DONE				BIT(4)
#define	IMEM_RDY				BIT(5)
#define	BASECHG					BIT(6)
#define	FWRDY					BIT(7)
#define	LOAD_FW_READY				(IMEM_CODE_DONE | \
						IMEM_CHK_RPT | \
						EMEM_CODE_DONE | \
						EMEM_CHK_RPT | \
						DMEM_CODE_DONE | \
						IMEM_RDY | \
						BASECHG | \
						FWRDY)
#define	TCR_TSFEN				BIT(8)
#define	TCR_TSFRST				BIT(9)
#define	TCR_FAKE_IMEM_EN			BIT(15)
#define	TCR_CRC					BIT(16)
#define	TCR_ICV					BIT(19)
#define	TCR_DISCW				BIT(20)
#define	TCR_HWPC_TX_EN				BIT(24)
#define	TCR_TCP_OFDL_EN				BIT(25)
#define	TXDMA_INIT_VALUE			(IMEM_CHK_RPT | \
						EXT_IMEM_CHK_RPT)

#define	RCR_APPFCS				BIT(31)
#define	RCR_DIS_ENC_2BYTE			BIT(30)
#define	RCR_DIS_AES_2BYTE			BIT(29)
#define	RCR_HTC_LOC_CTRL			BIT(28)
#define	RCR_ENMBID				BIT(27)
#define	RCR_RX_TCPOFDL_EN			BIT(26)
#define	RCR_APP_PHYST_RXFF			BIT(25)
#define	RCR_APP_PHYST_STAFF			BIT(24)
#define	RCR_CBSSID				BIT(23)
#define	RCR_APWRMGT				BIT(22)
#define	RCR_ADD3				BIT(21)
#define	RCR_AMF					BIT(20)
#define	RCR_ACF					BIT(19)
#define	RCR_ADF					BIT(18)
#define	RCR_APP_MIC				BIT(17)
#define	RCR_APP_ICV				BIT(16)
#define	RCR_RXFTH				BIT(13)
#define	RCR_AICV				BIT(12)
#define	RCR_RXDESC_LK_EN			BIT(11)
#define	RCR_APP_BA_SSN				BIT(6)
#define	RCR_ACRC32				BIT(5)
#define	RCR_RXSHFT_EN				BIT(4)
#define	RCR_AB					BIT(3)
#define	RCR_AM					BIT(2)
#define	RCR_APM					BIT(1)
#define	RCR_AAP					BIT(0)
#define	RCR_MXDMA_OFFSET			8
#define	RCR_FIFO_OFFSET				13


#define MSR_LINK_MASK				((1 << 0) | (1 << 1))
#define MSR_LINK_MANAGED			2
#define MSR_LINK_NONE				0
#define MSR_LINK_SHIFT				0
#define MSR_LINK_ADHOC				1
#define MSR_LINK_MASTER				3
#define	MSR_NOLINK				0x00
#define	MSR_ADHOC				0x01
#define	MSR_INFRA				0x02
#define	MSR_AP					0x03

#define	ENUART					BIT(7)
#define	ENJTAG					BIT(3)
#define	BTMODE					(BIT(2) | BIT(1))
#define	ENBT					BIT(0)

#define	ENMBID					BIT(7)
#define	BCNUM					(BIT(6) | BIT(5) | BIT(4))

#define	USTIME_EDCA				0xFF00
#define	USTIME_TSF				0x00FF

#define	SIFS_TRX				0xFF00
#define	SIFS_CTX				0x00FF

#define	ENSWBCN					BIT(15)
#define	DRVERLY_TU				0x0FF0
#define	DRVERLY_US				0x000F
#define	BCN_TCFG_CW_SHIFT			8
#define	BCN_TCFG_IFS				0

#define	RRSR_RSC_OFFSET				21
#define	RRSR_SHORT_OFFSET			23
#define	RRSR_RSC_BW_40M				0x600000
#define	RRSR_RSC_UPSUBCHNL			0x400000
#define	RRSR_RSC_LOWSUBCHNL			0x200000
#define	RRSR_SHORT				0x800000
#define	RRSR_1M					BIT(0)
#define	RRSR_2M					BIT(1)
#define	RRSR_5_5M				BIT(2)
#define	RRSR_11M				BIT(3)
#define	RRSR_6M					BIT(4)
#define	RRSR_9M					BIT(5)
#define	RRSR_12M				BIT(6)
#define	RRSR_18M				BIT(7)
#define	RRSR_24M				BIT(8)
#define	RRSR_36M				BIT(9)
#define	RRSR_48M				BIT(10)
#define	RRSR_54M				BIT(11)
#define	RRSR_MCS0				BIT(12)
#define	RRSR_MCS1				BIT(13)
#define	RRSR_MCS2				BIT(14)
#define	RRSR_MCS3				BIT(15)
#define	RRSR_MCS4				BIT(16)
#define	RRSR_MCS5				BIT(17)
#define	RRSR_MCS6				BIT(18)
#define	RRSR_MCS7				BIT(19)
#define	BRSR_AckShortPmb			BIT(23)

#define	RATR_1M					0x00000001
#define	RATR_2M					0x00000002
#define	RATR_55M				0x00000004
#define	RATR_11M				0x00000008
#define	RATR_6M					0x00000010
#define	RATR_9M					0x00000020
#define	RATR_12M				0x00000040
#define	RATR_18M				0x00000080
#define	RATR_24M				0x00000100
#define	RATR_36M				0x00000200
#define	RATR_48M				0x00000400
#define	RATR_54M				0x00000800
#define	RATR_MCS0				0x00001000
#define	RATR_MCS1				0x00002000
#define	RATR_MCS2				0x00004000
#define	RATR_MCS3				0x00008000
#define	RATR_MCS4				0x00010000
#define	RATR_MCS5				0x00020000
#define	RATR_MCS6				0x00040000
#define	RATR_MCS7				0x00080000
#define	RATR_MCS8				0x00100000
#define	RATR_MCS9				0x00200000
#define	RATR_MCS10				0x00400000
#define	RATR_MCS11				0x00800000
#define	RATR_MCS12				0x01000000
#define	RATR_MCS13				0x02000000
#define	RATR_MCS14				0x04000000
#define	RATR_MCS15				0x08000000

#define	RATE_ALL_CCK				(RATR_1M | RATR_2M | \
						RATR_55M | RATR_11M)
#define	RATE_ALL_OFDM_AG			(RATR_6M | RATR_9M | \
						RATR_12M | RATR_18M | \
						RATR_24M | RATR_36M | \
						RATR_48M | RATR_54M)
#define	RATE_ALL_OFDM_1SS			(RATR_MCS0 | RATR_MCS1 | \
						RATR_MCS2 | RATR_MCS3 | \
						RATR_MCS4 | RATR_MCS5 | \
						RATR_MCS6 | RATR_MCS7)
#define	RATE_ALL_OFDM_2SS			(RATR_MCS8 | RATR_MCS9 | \
						RATR_MCS10 | RATR_MCS11 | \
						RATR_MCS12 | RATR_MCS13 | \
						RATR_MCS14 | RATR_MCS15)

#define	AC_PARAM_TXOP_LIMIT_OFFSET		16
#define	AC_PARAM_ECW_MAX_OFFSET			12
#define	AC_PARAM_ECW_MIN_OFFSET			8
#define	AC_PARAM_AIFS_OFFSET			0

#define	AcmHw_HwEn				BIT(0)
#define	AcmHw_BeqEn				BIT(1)
#define	AcmHw_ViqEn				BIT(2)
#define	AcmHw_VoqEn				BIT(3)
#define	AcmHw_BeqStatus				BIT(4)
#define	AcmHw_ViqStatus				BIT(5)
#define	AcmHw_VoqStatus				BIT(6)

#define	RETRY_LIMIT_SHORT_SHIFT			8
#define	RETRY_LIMIT_LONG_SHIFT			0

#define	NAV_UPPER_EN				BIT(16)
#define	NAV_UPPER				0xFF00
#define	NAV_RTSRST				0xFF

#define	BW_OPMODE_20MHZ				BIT(2)
#define	BW_OPMODE_5G				BIT(1)
#define	BW_OPMODE_11J				BIT(0)

#define	RXERR_RPT_RST				BIT(27)
#define	RXERR_OFDM_PPDU				0
#define	RXERR_OFDM_FALSE_ALARM			1
#define	RXERR_OFDM_MPDU_OK			2
#define	RXERR_OFDM_MPDU_FAIL			3
#define	RXERR_CCK_PPDU				4
#define	RXERR_CCK_FALSE_ALARM			5
#define	RXERR_CCK_MPDU_OK			6
#define	RXERR_CCK_MPDU_FAIL			7
#define	RXERR_HT_PPDU				8
#define	RXERR_HT_FALSE_ALARM			9
#define	RXERR_HT_MPDU_TOTAL			10
#define	RXERR_HT_MPDU_OK			11
#define	RXERR_HT_MPDU_FAIL			12
#define	RXERR_RX_FULL_DROP			15

#define	SCR_TXUSEDK				BIT(0)
#define	SCR_RXUSEDK				BIT(1)
#define	SCR_TXENCENABLE				BIT(2)
#define	SCR_RXENCENABLE				BIT(3)
#define	SCR_SKBYA2				BIT(4)
#define	SCR_NOSKMC				BIT(5)

#define	CAM_VALID				BIT(15)
#define	CAM_NOTVALID				0x0000
#define	CAM_USEDK				BIT(5)

#define	CAM_NONE				0x0
#define	CAM_WEP40				0x01
#define	CAM_TKIP				0x02
#define	CAM_AES					0x04
#define	CAM_WEP104				0x05

#define	TOTAL_CAM_ENTRY				32
#define	HALF_CAM_ENTRY				16

#define	CAM_WRITE				BIT(16)
#define	CAM_READ				0x00000000
#define	CAM_POLLINIG				BIT(31)

#define	WOW_PMEN				BIT(0)
#define	WOW_WOMEN				BIT(1)
#define	WOW_MAGIC				BIT(2)
#define	WOW_UWF					BIT(3)

#define	GPIOMUX_EN				BIT(3)
#define	GPIOSEL_GPIO				0
#define	GPIOSEL_PHYDBG				1
#define	GPIOSEL_BT				2
#define	GPIOSEL_WLANDBG				3
#define	GPIOSEL_GPIO_MASK			(~(BIT(0)|BIT(1)))

#define	HST_RDBUSY				BIT(0)
#define	CPU_WTBUSY				BIT(1)

#define	IMR8190_DISABLED			0x0
#define	IMR_CPUERR				BIT(5)
#define	IMR_ATIMEND				BIT(4)
#define	IMR_TBDOK				BIT(3)
#define	IMR_TBDER				BIT(2)
#define	IMR_BCNDMAINT8				BIT(1)
#define	IMR_BCNDMAINT7				BIT(0)
#define	IMR_BCNDMAINT6				BIT(31)
#define	IMR_BCNDMAINT5				BIT(30)
#define	IMR_BCNDMAINT4				BIT(29)
#define	IMR_BCNDMAINT3				BIT(28)
#define	IMR_BCNDMAINT2				BIT(27)
#define	IMR_BCNDMAINT1				BIT(26)
#define	IMR_BCNDOK8				BIT(25)
#define	IMR_BCNDOK7				BIT(24)
#define	IMR_BCNDOK6				BIT(23)
#define	IMR_BCNDOK5				BIT(22)
#define	IMR_BCNDOK4				BIT(21)
#define	IMR_BCNDOK3				BIT(20)
#define	IMR_BCNDOK2				BIT(19)
#define	IMR_BCNDOK1				BIT(18)
#define	IMR_TIMEOUT2				BIT(17)
#define	IMR_TIMEOUT1				BIT(16)
#define	IMR_TXFOVW				BIT(15)
#define	IMR_PSTIMEOUT				BIT(14)
#define	IMR_BCNINT				BIT(13)
#define	IMR_RXFOVW				BIT(12)
#define	IMR_RDU					BIT(11)
#define	IMR_RXCMDOK				BIT(10)
#define	IMR_BDOK				BIT(9)
#define	IMR_HIGHDOK				BIT(8)
#define	IMR_COMDOK				BIT(7)
#define	IMR_MGNTDOK				BIT(6)
#define	IMR_HCCADOK				BIT(5)
#define	IMR_BKDOK				BIT(4)
#define	IMR_BEDOK				BIT(3)
#define	IMR_VIDOK				BIT(2)
#define	IMR_VODOK				BIT(1)
#define	IMR_ROK					BIT(0)

#define	TPPOLL_BKQ				BIT(0)
#define	TPPOLL_BEQ				BIT(1)
#define	TPPOLL_VIQ				BIT(2)
#define	TPPOLL_VOQ				BIT(3)
#define	TPPOLL_BQ				BIT(4)
#define	TPPOLL_CQ				BIT(5)
#define	TPPOLL_MQ				BIT(6)
#define	TPPOLL_HQ				BIT(7)
#define	TPPOLL_HCCAQ				BIT(8)
#define	TPPOLL_STOPBK				BIT(9)
#define	TPPOLL_STOPBE				BIT(10)
#define	TPPOLL_STOPVI				BIT(11)
#define	TPPOLL_STOPVO				BIT(12)
#define	TPPOLL_STOPMGT				BIT(13)
#define	TPPOLL_STOPHIGH				BIT(14)
#define	TPPOLL_STOPHCCA				BIT(15)
#define	TPPOLL_SHIFT				8

#define	CCX_CMD_CLM_ENABLE			BIT(0)
#define	CCX_CMD_NHM_ENABLE			BIT(1)
#define	CCX_CMD_FUNCTION_ENABLE			BIT(8)
#define	CCX_CMD_IGNORE_CCA			BIT(9)
#define	CCX_CMD_IGNORE_TXON			BIT(10)
#define	CCX_CLM_RESULT_READY			BIT(16)
#define	CCX_NHM_RESULT_READY			BIT(16)
#define	CCX_CMD_RESET				0x0


#define	HWSET_MAX_SIZE_92S			128
#define EFUSE_MAX_SECTION			16
#define EFUSE_REAL_CONTENT_LEN			512
#define EFUSE_OOB_PROTECT_BYTES			15

#define RTL8190_EEPROM_ID			0x8129
#define EEPROM_HPON				0x02
#define EEPROM_CLK				0x06
#define EEPROM_TESTR				0x08

#define EEPROM_VID				0x0A
#define EEPROM_DID				0x0C
#define EEPROM_SVID				0x0E
#define EEPROM_SMID				0x10

#define EEPROM_MAC_ADDR				0x12
#define EEPROM_NODE_ADDRESS_BYTE_0		0x12

#define EEPROM_PWDIFF				0x54

#define EEPROM_TXPOWERBASE			0x50
#define	EEPROM_TX_PWR_INDEX_RANGE		28

#define EEPROM_TX_PWR_HT20_DIFF			0x62
#define DEFAULT_HT20_TXPWR_DIFF			2
#define EEPROM_TX_PWR_OFDM_DIFF			0x65

#define	EEPROM_TXPWRGROUP			0x67
#define EEPROM_REGULATORY			0x6D

#define TX_PWR_SAFETY_CHK			0x6D
#define EEPROM_TXPWINDEX_CCK_24G		0x5D
#define EEPROM_TXPWINDEX_OFDM_24G		0x6B
#define EEPROM_HT2T_CH1_A			0x6c
#define EEPROM_HT2T_CH7_A			0x6d
#define EEPROM_HT2T_CH13_A			0x6e
#define EEPROM_HT2T_CH1_B			0x6f
#define EEPROM_HT2T_CH7_B			0x70
#define EEPROM_HT2T_CH13_B			0x71

#define EEPROM_TSSI_A				0x74
#define EEPROM_TSSI_B				0x75

#define	EEPROM_RFIND_POWERDIFF			0x76
#define	EEPROM_DEFAULT_LEGACYHTTXPOWERDIFF	0x3

#define EEPROM_THERMALMETER			0x77
#define	EEPROM_BLUETOOTH_COEXIST		0x78
#define	EEPROM_BLUETOOTH_TYPE			0x4f

#define	EEPROM_OPTIONAL				0x78
#define	EEPROM_WOWLAN				0x78

#define EEPROM_CRYSTALCAP			0x79
#define EEPROM_CHANNELPLAN			0x7B
#define EEPROM_VERSION				0x7C
#define	EEPROM_CUSTOMID				0x7A
#define EEPROM_BOARDTYPE			0x7E

#define	EEPROM_CHANNEL_PLAN_FCC			0x0
#define	EEPROM_CHANNEL_PLAN_IC			0x1
#define	EEPROM_CHANNEL_PLAN_ETSI		0x2
#define	EEPROM_CHANNEL_PLAN_SPAIN		0x3
#define	EEPROM_CHANNEL_PLAN_FRANCE		0x4
#define	EEPROM_CHANNEL_PLAN_MKK			0x5
#define	EEPROM_CHANNEL_PLAN_MKK1		0x6
#define	EEPROM_CHANNEL_PLAN_ISRAEL		0x7
#define	EEPROM_CHANNEL_PLAN_TELEC		0x8
#define	EEPROM_CHANNEL_PLAN_GLOBAL_DOMAIN	0x9
#define	EEPROM_CHANNEL_PLAN_WORLD_WIDE_13	0xA
#define	EEPROM_CHANNEL_PLAN_NCC			0xB
#define	EEPROM_CHANNEL_PLAN_BY_HW_MASK		0x80

#define	FW_DIG_DISABLE				0xfd00cc00
#define	FW_DIG_ENABLE				0xfd000000
#define	FW_DIG_HALT				0xfd000001
#define	FW_DIG_RESUME				0xfd000002
#define	FW_HIGH_PWR_DISABLE			0xfd000008
#define	FW_HIGH_PWR_ENABLE			0xfd000009
#define	FW_ADD_A2_ENTRY				0xfd000016
#define	FW_TXPWR_TRACK_ENABLE			0xfd000017
#define	FW_TXPWR_TRACK_DISABLE			0xfd000018
#define	FW_TXPWR_TRACK_THERMAL			0xfd000019
#define	FW_TXANT_SWITCH_ENABLE			0xfd000023
#define	FW_TXANT_SWITCH_DISABLE			0xfd000024
#define	FW_RA_INIT				0xfd000026
#define	FW_CTRL_DM_BY_DRIVER			0Xfd00002a
#define	FW_RA_IOT_BG_COMB			0xfd000030
#define	FW_RA_IOT_N_COMB			0xfd000031
#define	FW_RA_REFRESH				0xfd0000a0
#define	FW_RA_UPDATE_MASK			0xfd0000a2
#define	FW_RA_DISABLE				0xfd0000a4
#define	FW_RA_ACTIVE				0xfd0000a6
#define	FW_RA_DISABLE_RSSI_MASK			0xfd0000ac
#define	FW_RA_ENABLE_RSSI_MASK			0xfd0000ad
#define	FW_RA_RESET				0xfd0000af
#define	FW_DM_DISABLE				0xfd00aa00
#define	FW_IQK_ENABLE				0xf0000020
#define	FW_IQK_SUCCESS				0x0000dddd
#define	FW_IQK_FAIL				0x0000ffff
#define	FW_OP_FAILURE				0xffffffff
#define	FW_TX_FEEDBACK_NONE			0xfb000000
#define	FW_TX_FEEDBACK_DTM_ENABLE		(FW_TX_FEEDBACK_NONE | 0x1)
#define	FW_TX_FEEDBACK_CCX_ENABL		(FW_TX_FEEDBACK_NONE | 0x2)
#define	FW_BB_RESET_ENABLE			0xff00000d
#define	FW_BB_RESET_DISABLE			0xff00000e
#define	FW_CCA_CHK_ENABLE			0xff000011
#define	FW_CCK_RESET_CNT			0xff000013
#define	FW_LPS_ENTER				0xfe000010
#define	FW_LPS_LEAVE				0xfe000011
#define	FW_INDIRECT_READ			0xf2000000
#define	FW_INDIRECT_WRITE			0xf2000001
#define	FW_CHAN_SET				0xf3000001

#define RFPC					0x5F
#define RCR_9356SEL				BIT(6)
#define TCR_LRL_OFFSET				0
#define TCR_SRL_OFFSET				8
#define TCR_MXDMA_OFFSET			21
#define TCR_SAT					BIT(24)
#define RCR_MXDMA_OFFSET			8
#define RCR_FIFO_OFFSET				13
#define RCR_OnlyErlPkt				BIT(31)
#define CWR					0xDC
#define RETRYCTR				0xDE

#define CPU_GEN_SYSTEM_RESET			0x00000001

#define	CCX_COMMAND_REG				0x890
#define	CLM_PERIOD_REG				0x894
#define	NHM_PERIOD_REG				0x896

#define	NHM_THRESHOLD0				0x898
#define	NHM_THRESHOLD1				0x899
#define	NHM_THRESHOLD2				0x89A
#define	NHM_THRESHOLD3				0x89B
#define	NHM_THRESHOLD4				0x89C
#define	NHM_THRESHOLD5				0x89D
#define	NHM_THRESHOLD6				0x89E
#define	CLM_RESULT_REG				0x8D0
#define	NHM_RESULT_REG				0x8D4
#define	NHM_RPI_COUNTER0			0x8D8
#define	NHM_RPI_COUNTER1			0x8D9
#define	NHM_RPI_COUNTER2			0x8DA
#define	NHM_RPI_COUNTER3			0x8DB
#define	NHM_RPI_COUNTER4			0x8DC
#define	NHM_RPI_COUNTER5			0x8DD
#define	NHM_RPI_COUNTER6			0x8DE
#define	NHM_RPI_COUNTER7			0x8DF

#define	HAL_8192S_HW_GPIO_OFF_BIT		BIT(3)
#define	HAL_8192S_HW_GPIO_OFF_MASK		0xF7
#define	HAL_8192S_HW_GPIO_WPS_BIT		BIT(4)

#define	RPMAC_RESET				0x100
#define	RPMAC_TXSTART				0x104
#define	RPMAC_TXLEGACYSIG			0x108
#define	RPMAC_TXHTSIG1				0x10c
#define	RPMAC_TXHTSIG2				0x110
#define	RPMAC_PHYDEBUG				0x114
#define	RPMAC_TXPACKETNNM			0x118
#define	RPMAC_TXIDLE				0x11c
#define	RPMAC_TXMACHEADER0			0x120
#define	RPMAC_TXMACHEADER1			0x124
#define	RPMAC_TXMACHEADER2			0x128
#define	RPMAC_TXMACHEADER3			0x12c
#define	RPMAC_TXMACHEADER4			0x130
#define	RPMAC_TXMACHEADER5			0x134
#define	RPMAC_TXDATATYPE			0x138
#define	RPMAC_TXRANDOMSEED			0x13c
#define	RPMAC_CCKPLCPPREAMBLE			0x140
#define	RPMAC_CCKPLCPHEADER			0x144
#define	RPMAC_CCKCRC16				0x148
#define	RPMAC_OFDMRXCRC32OK			0x170
#define	RPMAC_OFDMRXCRC32ER			0x174
#define	RPMAC_OFDMRXPARITYER			0x178
#define	RPMAC_OFDMRXCRC8ER			0x17c
#define	RPMAC_CCKCRXRC16ER			0x180
#define	RPMAC_CCKCRXRC32ER			0x184
#define	RPMAC_CCKCRXRC32OK			0x188
#define	RPMAC_TXSTATUS				0x18c

#define	RF_BB_CMD_ADDR				0x02c0
#define	RF_BB_CMD_DATA				0x02c4

#define	RFPGA0_RFMOD				0x800

#define	RFPGA0_TXINFO				0x804
#define	RFPGA0_PSDFUNCTION			0x808

#define	RFPGA0_TXGAINSTAGE			0x80c

#define	RFPGA0_RFTIMING1			0x810
#define	RFPGA0_RFTIMING2			0x814
#define	RFPGA0_XA_HSSIPARAMETER1		0x820
#define	RFPGA0_XA_HSSIPARAMETER2		0x824
#define	RFPGA0_XB_HSSIPARAMETER1		0x828
#define	RFPGA0_XB_HSSIPARAMETER2		0x82c
#define	RFPGA0_XC_HSSIPARAMETER1		0x830
#define	RFPGA0_XC_HSSIPARAMETER2		0x834
#define	RFPGA0_XD_HSSIPARAMETER1		0x838
#define	RFPGA0_XD_HSSIPARAMETER2		0x83c
#define	RFPGA0_XA_LSSIPARAMETER			0x840
#define	RFPGA0_XB_LSSIPARAMETER			0x844
#define	RFPGA0_XC_LSSIPARAMETER			0x848
#define	RFPGA0_XD_LSSIPARAMETER			0x84c

#define	RFPGA0_RFWAKEUP_PARAMETER		0x850
#define	RFPGA0_RFSLEEPUP_PARAMETER		0x854

#define	RFPGA0_XAB_SWITCHCONTROL		0x858
#define	RFPGA0_XCD_SWITCHCONTROL		0x85c

#define	RFPGA0_XA_RFINTERFACEOE			0x860
#define	RFPGA0_XB_RFINTERFACEOE			0x864
#define	RFPGA0_XC_RFINTERFACEOE			0x868
#define	RFPGA0_XD_RFINTERFACEOE			0x86c

#define	RFPGA0_XAB_RFINTERFACESW		0x870
#define	RFPGA0_XCD_RFINTERFACESW		0x874

#define	RFPGA0_XAB_RFPARAMETER			0x878
#define	RFPGA0_XCD_RFPARAMETER			0x87c

#define	RFPGA0_ANALOGPARAMETER1			0x880
#define	RFPGA0_ANALOGPARAMETER2			0x884
#define	RFPGA0_ANALOGPARAMETER3			0x888
#define	RFPGA0_ANALOGPARAMETER4			0x88c

#define	RFPGA0_XA_LSSIREADBACK			0x8a0
#define	RFPGA0_XB_LSSIREADBACK			0x8a4
#define	RFPGA0_XC_LSSIREADBACK			0x8a8
#define	RFPGA0_XD_LSSIREADBACK			0x8ac

#define	RFPGA0_PSDREPORT			0x8b4
#define	TRANSCEIVERA_HSPI_READBACK		0x8b8
#define	TRANSCEIVERB_HSPI_READBACK		0x8bc
#define	RFPGA0_XAB_RFINTERFACERB		0x8e0
#define	RFPGA0_XCD_RFINTERFACERB		0x8e4
#define	RFPGA1_RFMOD				0x900

#define	RFPGA1_TXBLOCK				0x904
#define	RFPGA1_DEBUGSELECT			0x908
#define	RFPGA1_TXINFO				0x90c

#define	RCCK0_SYSTEM				0xa00

#define	RCCK0_AFESETTING			0xa04
#define	RCCK0_CCA				0xa08

#define	RCCK0_RXAGC1				0xa0c
#define	RCCK0_RXAGC2				0xa10

#define	RCCK0_RXHP				0xa14

#define	RCCK0_DSPPARAMETER1			0xa18
#define	RCCK0_DSPPARAMETER2			0xa1c

#define	RCCK0_TXFILTER1				0xa20
#define	RCCK0_TXFILTER2				0xa24
#define	RCCK0_DEBUGPORT				0xa28
#define	RCCK0_FALSEALARMREPORT			0xa2c
#define	RCCK0_TRSSIREPORT			0xa50
#define	RCCK0_RXREPORT				0xa54
#define	RCCK0_FACOUNTERLOWER			0xa5c
#define	RCCK0_FACOUNTERUPPER			0xa58

#define	ROFDM0_LSTF				0xc00

#define	ROFDM0_TRXPATHENABLE			0xc04
#define	ROFDM0_TRMUXPAR				0xc08
#define	ROFDM0_TRSWISOLATION			0xc0c

#define	ROFDM0_XARXAFE				0xc10
#define	ROFDM0_XARXIQIMBALANCE			0xc14
#define	ROFDM0_XBRXAFE				0xc18
#define	ROFDM0_XBRXIQIMBALANCE			0xc1c
#define	ROFDM0_XCRXAFE				0xc20
#define	ROFDM0_XCRXIQIMBALANCE			0xc24
#define	ROFDM0_XDRXAFE				0xc28
#define	ROFDM0_XDRXIQIMBALANCE			0xc2c

#define	ROFDM0_RXDETECTOR1			0xc30
#define	ROFDM0_RXDETECTOR2			0xc34
#define	ROFDM0_RXDETECTOR3			0xc38
#define	ROFDM0_RXDETECTOR4			0xc3c

#define	ROFDM0_RXDSP				0xc40
#define	ROFDM0_CFO_AND_DAGC			0xc44
#define	ROFDM0_CCADROP_THRESHOLD		0xc48
#define	ROFDM0_ECCA_THRESHOLD			0xc4c

#define	ROFDM0_XAAGCCORE1			0xc50
#define	ROFDM0_XAAGCCORE2			0xc54
#define	ROFDM0_XBAGCCORE1			0xc58
#define	ROFDM0_XBAGCCORE2			0xc5c
#define	ROFDM0_XCAGCCORE1			0xc60
#define	ROFDM0_XCAGCCORE2			0xc64
#define	ROFDM0_XDAGCCORE1			0xc68
#define	ROFDM0_XDAGCCORE2			0xc6c

#define	ROFDM0_AGCPARAMETER1			0xc70
#define	ROFDM0_AGCPARAMETER2			0xc74
#define	ROFDM0_AGCRSSITABLE			0xc78
#define	ROFDM0_HTSTFAGC				0xc7c

#define	ROFDM0_XATXIQIMBALANCE			0xc80
#define	ROFDM0_XATXAFE				0xc84
#define	ROFDM0_XBTXIQIMBALANCE			0xc88
#define	ROFDM0_XBTXAFE				0xc8c
#define	ROFDM0_XCTXIQIMBALANCE			0xc90
#define	ROFDM0_XCTXAFE				0xc94
#define	ROFDM0_XDTXIQIMBALANCE			0xc98
#define	ROFDM0_XDTXAFE				0xc9c

#define	ROFDM0_RXHP_PARAMETER			0xce0
#define	ROFDM0_TXPSEUDO_NOISE_WGT		0xce4
#define	ROFDM0_FRAME_SYNC			0xcf0
#define	ROFDM0_DFSREPORT			0xcf4
#define	ROFDM0_TXCOEFF1				0xca4
#define	ROFDM0_TXCOEFF2				0xca8
#define	ROFDM0_TXCOEFF3				0xcac
#define	ROFDM0_TXCOEFF4				0xcb0
#define	ROFDM0_TXCOEFF5				0xcb4
#define	ROFDM0_TXCOEFF6				0xcb8


#define	ROFDM1_LSTF				0xd00
#define	ROFDM1_TRXPATHENABLE			0xd04

#define	ROFDM1_CFO				0xd08
#define	ROFDM1_CSI1				0xd10
#define	ROFDM1_SBD				0xd14
#define	ROFDM1_CSI2				0xd18
#define	ROFDM1_CFOTRACKING			0xd2c
#define	ROFDM1_TRXMESAURE1			0xd34
#define	ROFDM1_INTF_DET				0xd3c
#define	ROFDM1_PSEUDO_NOISESTATEAB		0xd50
#define	ROFDM1_PSEUDO_NOISESTATECD		0xd54
#define	ROFDM1_RX_PSEUDO_NOISE_WGT		0xd58

#define	ROFDM_PHYCOUNTER1			0xda0
#define	ROFDM_PHYCOUNTER2			0xda4
#define	ROFDM_PHYCOUNTER3			0xda8

#define	ROFDM_SHORT_CFOAB			0xdac
#define	ROFDM_SHORT_CFOCD			0xdb0
#define	ROFDM_LONG_CFOAB			0xdb4
#define	ROFDM_LONG_CFOCD			0xdb8
#define	ROFDM_TAIL_CFOAB			0xdbc
#define	ROFDM_TAIL_CFOCD			0xdc0
#define	ROFDM_PW_MEASURE1			0xdc4
#define	ROFDM_PW_MEASURE2			0xdc8
#define	ROFDM_BW_REPORT				0xdcc
#define	ROFDM_AGC_REPORT			0xdd0
#define	ROFDM_RXSNR				0xdd4
#define	ROFDM_RXEVMCSI				0xdd8
#define	ROFDM_SIG_REPORT			0xddc


#define	RTXAGC_RATE18_06			0xe00
#define	RTXAGC_RATE54_24			0xe04
#define	RTXAGC_CCK_MCS32			0xe08
#define	RTXAGC_MCS03_MCS00			0xe10
#define	RTXAGC_MCS07_MCS04			0xe14
#define	RTXAGC_MCS11_MCS08			0xe18
#define	RTXAGC_MCS15_MCS12			0xe1c


#define	RF_AC					0x00
#define	RF_IQADJ_G1				0x01
#define	RF_IQADJ_G2				0x02
#define	RF_POW_TRSW				0x05
#define	RF_GAIN_RX				0x06
#define	RF_GAIN_TX				0x07
#define	RF_TXM_IDAC				0x08
#define	RF_BS_IQGEN				0x0F

#define	RF_MODE1				0x10
#define	RF_MODE2				0x11
#define	RF_RX_AGC_HP				0x12
#define	RF_TX_AGC				0x13
#define	RF_BIAS					0x14
#define	RF_IPA					0x15
#define	RF_POW_ABILITY				0x17
#define	RF_MODE_AG				0x18
#define	RF_CHANNEL				0x18
#define	RF_CHNLBW				0x18
#define	RF_TOP					0x19
#define	RF_RX_G1				0x1A
#define	RF_RX_G2				0x1B
#define	RF_RX_BB2				0x1C
#define	RF_RX_BB1				0x1D
#define	RF_RCK1					0x1E
#define	RF_RCK2					0x1F

#define	RF_TX_G1				0x20
#define	RF_TX_G2				0x21
#define	RF_TX_G3				0x22
#define	RF_TX_BB1				0x23
#define	RF_T_METER				0x24
#define	RF_SYN_G1				0x25
#define	RF_SYN_G2				0x26
#define	RF_SYN_G3				0x27
#define	RF_SYN_G4				0x28
#define	RF_SYN_G5				0x29
#define	RF_SYN_G6				0x2A
#define	RF_SYN_G7				0x2B
#define	RF_SYN_G8				0x2C

#define	RF_RCK_OS				0x30
#define	RF_TXPA_G1				0x31
#define	RF_TXPA_G2				0x32
#define	RF_TXPA_G3				0x33

#define	BRFMOD					0x1
#define	BCCKEN					0x1000000
#define	BOFDMEN					0x2000000

#define	BXBTXAGC				0xf00
#define	BXCTXAGC				0xf000
#define	BXDTXAGC				0xf0000

#define	B3WIRE_DATALENGTH			0x800
#define	B3WIRE_ADDRESSLENGTH			0x400

#define	BRFSI_RFENV				0x10

#define	BLSSI_READADDRESS			0x7f800000
#define	BLSSI_READEDGE				0x80000000
#define	BLSSI_READBACK_DATA			0xfffff

#define	BADCLKPHASE				0x4000000

#define	BCCK_SIDEBAND				0x10

#define	BTX_AGCRATECCK				0x7f00

#define	MASKBYTE0				0xff
#define	MASKBYTE1				0xff00
#define	MASKBYTE2				0xff0000
#define	MASKBYTE3				0xff000000
#define	MASKHWORD				0xffff0000
#define	MASKLWORD				0x0000ffff
#define	MASKDWORD				0xffffffff

#define	MAKS12BITS				0xfffff
#define	MASK20BITS				0xfffff
#define RFREG_OFFSET_MASK			0xfffff

#endif
