/* SPDX-License-Identifier: GPL-2.0 */
#ifndef B43_PHY_HT_H_
#define B43_PHY_HT_H_

#include "phy_common.h"


#define B43_PHY_HT_BBCFG			0x001 /* BB config */
#define  B43_PHY_HT_BBCFG_RSTCCA		0x4000 /* Reset CCA */
#define  B43_PHY_HT_BBCFG_RSTRX			0x8000 /* Reset RX */
#define B43_PHY_HT_BANDCTL			0x009 /* Band control */
#define  B43_PHY_HT_BANDCTL_5GHZ		0x0001 /* Use the 5GHz band */
#define B43_PHY_HT_TABLE_ADDR			0x072 /* Table address */
#define B43_PHY_HT_TABLE_DATALO			0x073 /* Table data low */
#define B43_PHY_HT_TABLE_DATAHI			0x074 /* Table data high */
#define B43_PHY_HT_CLASS_CTL			0x0B0 /* Classifier control */
#define  B43_PHY_HT_CLASS_CTL_CCK_EN		0x0001 /* CCK enable */
#define  B43_PHY_HT_CLASS_CTL_OFDM_EN		0x0002 /* OFDM enable */
#define  B43_PHY_HT_CLASS_CTL_WAITED_EN		0x0004 /* Waited enable */
#define B43_PHY_HT_IQLOCAL_CMDGCTL		0x0C2	/* I/Q LO cal command G control */
#define B43_PHY_HT_SAMP_CMD			0x0C3	/* Sample command */
#define  B43_PHY_HT_SAMP_CMD_STOP		0x0002	/* Stop */
#define B43_PHY_HT_SAMP_LOOP_CNT		0x0C4	/* Sample loop count */
#define B43_PHY_HT_SAMP_WAIT_CNT		0x0C5	/* Sample wait count */
#define B43_PHY_HT_SAMP_DEP_CNT			0x0C6	/* Sample depth count */
#define B43_PHY_HT_SAMP_STAT			0x0C7	/* Sample status */
#define B43_PHY_HT_EST_PWR_C1			0x118
#define B43_PHY_HT_EST_PWR_C2			0x119
#define B43_PHY_HT_EST_PWR_C3			0x11A
#define B43_PHY_HT_TSSIMODE			0x122	/* TSSI mode */
#define  B43_PHY_HT_TSSIMODE_EN			0x0001	/* TSSI enable */
#define  B43_PHY_HT_TSSIMODE_PDEN		0x0002	/* Power det enable */
#define B43_PHY_HT_BW1				0x1CE
#define B43_PHY_HT_BW2				0x1CF
#define B43_PHY_HT_BW3				0x1D0
#define B43_PHY_HT_BW4				0x1D1
#define B43_PHY_HT_BW5				0x1D2
#define B43_PHY_HT_BW6				0x1D3
#define B43_PHY_HT_TXPCTL_CMD_C1		0x1E7	/* TX power control command */
#define  B43_PHY_HT_TXPCTL_CMD_C1_INIT		0x007F	/* Init */
#define  B43_PHY_HT_TXPCTL_CMD_C1_COEFF		0x2000	/* Power control coefficients */
#define  B43_PHY_HT_TXPCTL_CMD_C1_HWPCTLEN	0x4000	/* Hardware TX power control enable */
#define  B43_PHY_HT_TXPCTL_CMD_C1_PCTLEN	0x8000	/* TX power control enable */
#define B43_PHY_HT_TXPCTL_N			0x1E8	/* TX power control N num */
#define  B43_PHY_HT_TXPCTL_N_TSSID		0x00FF	/* N TSSI delay */
#define  B43_PHY_HT_TXPCTL_N_TSSID_SHIFT	0
#define  B43_PHY_HT_TXPCTL_N_NPTIL2		0x0700	/* N PT integer log2 */
#define  B43_PHY_HT_TXPCTL_N_NPTIL2_SHIFT	8
#define B43_PHY_HT_TXPCTL_IDLE_TSSI		0x1E9	/* TX power control idle TSSI */
#define  B43_PHY_HT_TXPCTL_IDLE_TSSI_C1		0x003F
#define  B43_PHY_HT_TXPCTL_IDLE_TSSI_C1_SHIFT	0
#define  B43_PHY_HT_TXPCTL_IDLE_TSSI_C2		0x3F00
#define  B43_PHY_HT_TXPCTL_IDLE_TSSI_C2_SHIFT	8
#define  B43_PHY_HT_TXPCTL_IDLE_TSSI_BINF	0x8000	/* Raw TSSI offset bin format */
#define B43_PHY_HT_TXPCTL_TARG_PWR		0x1EA	/* TX power control target power */
#define  B43_PHY_HT_TXPCTL_TARG_PWR_C1		0x00FF	/* Power 0 */
#define  B43_PHY_HT_TXPCTL_TARG_PWR_C1_SHIFT	0
#define  B43_PHY_HT_TXPCTL_TARG_PWR_C2		0xFF00	/* Power 1 */
#define  B43_PHY_HT_TXPCTL_TARG_PWR_C2_SHIFT	8
#define B43_PHY_HT_TX_PCTL_STATUS_C1		0x1ED
#define B43_PHY_HT_TX_PCTL_STATUS_C2		0x1EE
#define B43_PHY_HT_TXPCTL_CMD_C2		0x222
#define  B43_PHY_HT_TXPCTL_CMD_C2_INIT		0x007F
#define B43_PHY_HT_RSSI_C1			0x219
#define B43_PHY_HT_RSSI_C2			0x21A
#define B43_PHY_HT_RSSI_C3			0x21B

#define B43_PHY_HT_C1_CLIP1THRES		B43_PHY_OFDM(0x00E)
#define B43_PHY_HT_C2_CLIP1THRES		B43_PHY_OFDM(0x04E)
#define B43_PHY_HT_C3_CLIP1THRES		B43_PHY_OFDM(0x08E)

#define B43_PHY_HT_RF_SEQ_MODE			B43_PHY_EXTG(0x000)
#define  B43_PHY_HT_RF_SEQ_MODE_CA_OVER		0x0001	/* Core active override */
#define  B43_PHY_HT_RF_SEQ_MODE_TR_OVER		0x0002	/* Trigger override */
#define B43_PHY_HT_RF_SEQ_TRIG			B43_PHY_EXTG(0x003)
#define  B43_PHY_HT_RF_SEQ_TRIG_RX2TX		0x0001 /* RX2TX */
#define  B43_PHY_HT_RF_SEQ_TRIG_TX2RX		0x0002 /* TX2RX */
#define  B43_PHY_HT_RF_SEQ_TRIG_UPGH		0x0004 /* Update gain H */
#define  B43_PHY_HT_RF_SEQ_TRIG_UPGL		0x0008 /* Update gain L */
#define  B43_PHY_HT_RF_SEQ_TRIG_UPGU		0x0010 /* Update gain U */
#define  B43_PHY_HT_RF_SEQ_TRIG_RST2RX		0x0020 /* Reset to RX */
#define B43_PHY_HT_RF_SEQ_STATUS		B43_PHY_EXTG(0x004)
/* Values for the status are the same as for the trigger */

#define B43_PHY_HT_RF_CTL_CMD			0x810
#define  B43_PHY_HT_RF_CTL_CMD_FORCE		0x0001
#define  B43_PHY_HT_RF_CTL_CMD_CHIP0_PU		0x0002

#define B43_PHY_HT_RF_CTL_INT_C1		B43_PHY_EXTG(0x04c)
#define B43_PHY_HT_RF_CTL_INT_C2		B43_PHY_EXTG(0x06c)
#define B43_PHY_HT_RF_CTL_INT_C3		B43_PHY_EXTG(0x08c)

#define B43_PHY_HT_AFE_C1_OVER			B43_PHY_EXTG(0x110)
#define B43_PHY_HT_AFE_C1			B43_PHY_EXTG(0x111)
#define B43_PHY_HT_AFE_C2_OVER			B43_PHY_EXTG(0x114)
#define B43_PHY_HT_AFE_C2			B43_PHY_EXTG(0x115)
#define B43_PHY_HT_AFE_C3_OVER			B43_PHY_EXTG(0x118)
#define B43_PHY_HT_AFE_C3			B43_PHY_EXTG(0x119)

#define B43_PHY_HT_TXPCTL_CMD_C3		B43_PHY_EXTG(0x164)
#define  B43_PHY_HT_TXPCTL_CMD_C3_INIT		0x007F
#define B43_PHY_HT_TXPCTL_IDLE_TSSI2		B43_PHY_EXTG(0x165)	/* TX power control idle TSSI */
#define  B43_PHY_HT_TXPCTL_IDLE_TSSI2_C3	0x003F
#define  B43_PHY_HT_TXPCTL_IDLE_TSSI2_C3_SHIFT	0
#define B43_PHY_HT_TXPCTL_TARG_PWR2		B43_PHY_EXTG(0x166)	/* TX power control target power */
#define  B43_PHY_HT_TXPCTL_TARG_PWR2_C3		0x00FF
#define  B43_PHY_HT_TXPCTL_TARG_PWR2_C3_SHIFT	0
#define B43_PHY_HT_TX_PCTL_STATUS_C3		B43_PHY_EXTG(0x169)

#define B43_PHY_B_BBCFG				B43_PHY_N_BMODE(0x001)
#define  B43_PHY_B_BBCFG_RSTCCA			0x4000 /* Reset CCA */
#define  B43_PHY_B_BBCFG_RSTRX			0x8000 /* Reset RX */
#define B43_PHY_HT_TEST				B43_PHY_N_BMODE(0x00A)


/* Values for PHY registers used on channel switching */
struct b43_phy_ht_channeltab_e_phy {
	u16 bw1;
	u16 bw2;
	u16 bw3;
	u16 bw4;
	u16 bw5;
	u16 bw6;
};


struct b43_phy_ht {
	u16 rf_ctl_int_save[3];

	bool tx_pwr_ctl;
	u8 tx_pwr_idx[3];

	s32 bb_mult_save[3];

	u8 idle_tssi[3];
};


struct b43_phy_operations;
extern const struct b43_phy_operations b43_phyops_ht;

#endif /* B43_PHY_HT_H_ */
