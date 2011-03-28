/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define NPHY_TBL_ID_GAIN1		0
#define NPHY_TBL_ID_GAIN2		1
#define NPHY_TBL_ID_GAINBITS1		2
#define NPHY_TBL_ID_GAINBITS2		3
#define NPHY_TBL_ID_GAINLIMIT		4
#define NPHY_TBL_ID_WRSSIGainLimit	5
#define NPHY_TBL_ID_RFSEQ		7
#define NPHY_TBL_ID_AFECTRL		8
#define NPHY_TBL_ID_ANTSWCTRLLUT	9
#define NPHY_TBL_ID_IQLOCAL		15
#define NPHY_TBL_ID_NOISEVAR		16
#define NPHY_TBL_ID_SAMPLEPLAY		17
#define NPHY_TBL_ID_CORE1TXPWRCTL	26
#define NPHY_TBL_ID_CORE2TXPWRCTL	27
#define NPHY_TBL_ID_CMPMETRICDATAWEIGHTTBL	30

#define NPHY_TBL_ID_EPSILONTBL0   31
#define NPHY_TBL_ID_SCALARTBL0    32
#define NPHY_TBL_ID_EPSILONTBL1   33
#define NPHY_TBL_ID_SCALARTBL1    34

#define	NPHY_TO_BPHY_OFF	0xc00

#define NPHY_BandControl_currentBand			0x0001
#define RFCC_CHIP0_PU			0x0400
#define RFCC_POR_FORCE			0x0040
#define RFCC_OE_POR_FORCE		0x0080
#define NPHY_RfctrlIntc_override_OFF			0
#define NPHY_RfctrlIntc_override_TRSW			1
#define NPHY_RfctrlIntc_override_PA				2
#define NPHY_RfctrlIntc_override_EXT_LNA_PU		3
#define NPHY_RfctrlIntc_override_EXT_LNA_GAIN	4
#define RIFS_ENABLE			0x80
#define BPHY_BAND_SEL_UP20		0x10
#define NPHY_MLenable			0x02

#define NPHY_RfseqMode_CoreActv_override 0x0001
#define NPHY_RfseqMode_Trigger_override	0x0002
#define NPHY_RfseqCoreActv_TxRxChain0	(0x11)
#define NPHY_RfseqCoreActv_TxRxChain1	(0x22)

#define NPHY_RfseqTrigger_rx2tx		0x0001
#define NPHY_RfseqTrigger_tx2rx		0x0002
#define NPHY_RfseqTrigger_updategainh	0x0004
#define NPHY_RfseqTrigger_updategainl	0x0008
#define NPHY_RfseqTrigger_updategainu	0x0010
#define NPHY_RfseqTrigger_reset2rx	0x0020
#define NPHY_RfseqStatus_rx2tx		0x0001
#define NPHY_RfseqStatus_tx2rx		0x0002
#define NPHY_RfseqStatus_updategainh	0x0004
#define NPHY_RfseqStatus_updategainl	0x0008
#define NPHY_RfseqStatus_updategainu	0x0010
#define NPHY_RfseqStatus_reset2rx	0x0020
#define NPHY_ClassifierCtrl_cck_en	0x1
#define NPHY_ClassifierCtrl_ofdm_en	0x2
#define NPHY_ClassifierCtrl_waited_en	0x4
#define NPHY_IQFlip_ADC1		0x0001
#define NPHY_IQFlip_ADC2		0x0010
#define NPHY_sampleCmd_STOP		0x0002

#define RX_GF_OR_MM			0x0004
#define RX_GF_MM_AUTO			0x0100

#define NPHY_iqloCalCmdGctl_IQLO_CAL_EN	0x8000

#define NPHY_IqestCmd_iqstart		0x1
#define NPHY_IqestCmd_iqMode		0x2

#define NPHY_TxPwrCtrlCmd_pwrIndex_init		0x40
#define NPHY_TxPwrCtrlCmd_pwrIndex_init_rev7	0x19

#define PRIM_SEL_UP20		0x8000

#define NPHY_RFSEQ_RX2TX		0x0
#define NPHY_RFSEQ_TX2RX		0x1
#define NPHY_RFSEQ_RESET2RX		0x2
#define NPHY_RFSEQ_UPDATEGAINH		0x3
#define NPHY_RFSEQ_UPDATEGAINL		0x4
#define NPHY_RFSEQ_UPDATEGAINU		0x5

#define NPHY_RFSEQ_CMD_NOP		0x0
#define NPHY_RFSEQ_CMD_RXG_FBW		0x1
#define NPHY_RFSEQ_CMD_TR_SWITCH	0x2
#define NPHY_RFSEQ_CMD_EXT_PA		0x3
#define NPHY_RFSEQ_CMD_RXPD_TXPD	0x4
#define NPHY_RFSEQ_CMD_TX_GAIN		0x5
#define NPHY_RFSEQ_CMD_RX_GAIN		0x6
#define NPHY_RFSEQ_CMD_SET_HPF_BW	0x7
#define NPHY_RFSEQ_CMD_CLR_HIQ_DIS	0x8
#define NPHY_RFSEQ_CMD_END		0xf

#define NPHY_REV3_RFSEQ_CMD_NOP		0x0
#define NPHY_REV3_RFSEQ_CMD_RXG_FBW	0x1
#define NPHY_REV3_RFSEQ_CMD_TR_SWITCH	0x2
#define NPHY_REV3_RFSEQ_CMD_INT_PA_PU	0x3
#define NPHY_REV3_RFSEQ_CMD_EXT_PA	0x4
#define NPHY_REV3_RFSEQ_CMD_RXPD_TXPD	0x5
#define NPHY_REV3_RFSEQ_CMD_TX_GAIN	0x6
#define NPHY_REV3_RFSEQ_CMD_RX_GAIN	0x7
#define NPHY_REV3_RFSEQ_CMD_CLR_HIQ_DIS	0x8
#define NPHY_REV3_RFSEQ_CMD_SET_HPF_H_HPC	0x9
#define NPHY_REV3_RFSEQ_CMD_SET_LPF_H_HPC	0xa
#define NPHY_REV3_RFSEQ_CMD_SET_HPF_M_HPC	0xb
#define NPHY_REV3_RFSEQ_CMD_SET_LPF_M_HPC	0xc
#define NPHY_REV3_RFSEQ_CMD_SET_HPF_L_HPC	0xd
#define NPHY_REV3_RFSEQ_CMD_SET_LPF_L_HPC	0xe
#define NPHY_REV3_RFSEQ_CMD_CLR_RXRX_BIAS	0xf
#define NPHY_REV3_RFSEQ_CMD_END		0x1f

#define NPHY_RSSI_SEL_W1 		0x0
#define NPHY_RSSI_SEL_W2 		0x1
#define NPHY_RSSI_SEL_NB 		0x2
#define NPHY_RSSI_SEL_IQ 		0x3
#define NPHY_RSSI_SEL_TSSI_2G 		0x4
#define NPHY_RSSI_SEL_TSSI_5G 		0x5
#define NPHY_RSSI_SEL_TBD 		0x6

#define NPHY_RAIL_I			0x0
#define NPHY_RAIL_Q			0x1

#define NPHY_FORCESIG_DECODEGATEDCLKS	0x8

#define NPHY_REV7_RfctrlOverride_cmd_rxrf_pu 0x0
#define NPHY_REV7_RfctrlOverride_cmd_rx_pu   0x1
#define NPHY_REV7_RfctrlOverride_cmd_tx_pu   0x2
#define NPHY_REV7_RfctrlOverride_cmd_rxgain  0x3
#define NPHY_REV7_RfctrlOverride_cmd_txgain  0x4

#define NPHY_REV7_RXGAINCODE_RFMXGAIN_MASK 0x000ff
#define NPHY_REV7_RXGAINCODE_LPFGAIN_MASK  0x0ff00
#define NPHY_REV7_RXGAINCODE_DVGAGAIN_MASK 0xf0000

#define NPHY_REV7_TXGAINCODE_TGAIN_MASK     0x7fff
#define NPHY_REV7_TXGAINCODE_LPFGAIN_MASK   0x8000
#define NPHY_REV7_TXGAINCODE_BIQ0GAIN_SHIFT 14

#define NPHY_REV7_RFCTRLOVERRIDE_ID0 0x0
#define NPHY_REV7_RFCTRLOVERRIDE_ID1 0x1
#define NPHY_REV7_RFCTRLOVERRIDE_ID2 0x2

#define NPHY_IqestIqAccLo(core)  ((core == 0) ? 0x12c : 0x134)

#define NPHY_IqestIqAccHi(core)  ((core == 0) ? 0x12d : 0x135)

#define NPHY_IqestipwrAccLo(core)  ((core == 0) ? 0x12e : 0x136)

#define NPHY_IqestipwrAccHi(core)  ((core == 0) ? 0x12f : 0x137)

#define NPHY_IqestqpwrAccLo(core)  ((core == 0) ? 0x130 : 0x138)

#define NPHY_IqestqpwrAccHi(core)  ((core == 0) ? 0x131 : 0x139)
