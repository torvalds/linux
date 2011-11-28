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
#define B43_PHY_HT_BW1				0x1CE
#define B43_PHY_HT_BW2				0x1CF
#define B43_PHY_HT_BW3				0x1D0
#define B43_PHY_HT_BW4				0x1D1
#define B43_PHY_HT_BW5				0x1D2
#define B43_PHY_HT_BW6				0x1D3

#define B43_PHY_HT_C1_CLIP1THRES		B43_PHY_OFDM(0x00E)
#define B43_PHY_HT_C2_CLIP1THRES		B43_PHY_OFDM(0x04E)
#define B43_PHY_HT_C3_CLIP1THRES		B43_PHY_OFDM(0x08E)

#define B43_PHY_HT_RF_SEQ_MODE			B43_PHY_EXTG(0x000)
#define B43_PHY_HT_RF_SEQ_TRIG			B43_PHY_EXTG(0x003)
#define  B43_PHY_HT_RF_SEQ_TRIG_RX2TX		0x0001 /* RX2TX */
#define  B43_PHY_HT_RF_SEQ_TRIG_TX2RX		0x0002 /* TX2RX */
#define  B43_PHY_HT_RF_SEQ_TRIG_UPGH		0x0004 /* Update gain H */
#define  B43_PHY_HT_RF_SEQ_TRIG_UPGL		0x0008 /* Update gain L */
#define  B43_PHY_HT_RF_SEQ_TRIG_UPGU		0x0010 /* Update gain U */
#define  B43_PHY_HT_RF_SEQ_TRIG_RST2RX		0x0020 /* Reset to RX */
#define B43_PHY_HT_RF_SEQ_STATUS		B43_PHY_EXTG(0x004)
/* Values for the status are the same as for the trigger */

#define B43_PHY_HT_RF_CTL1			B43_PHY_EXTG(0x010)

#define B43_PHY_HT_AFE_CTL1			B43_PHY_EXTG(0x110)
#define B43_PHY_HT_AFE_CTL2			B43_PHY_EXTG(0x111)
#define B43_PHY_HT_AFE_CTL3			B43_PHY_EXTG(0x114)
#define B43_PHY_HT_AFE_CTL4			B43_PHY_EXTG(0x115)
#define B43_PHY_HT_AFE_CTL5			B43_PHY_EXTG(0x118)
#define B43_PHY_HT_AFE_CTL6			B43_PHY_EXTG(0x119)


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
};


struct b43_phy_operations;
extern const struct b43_phy_operations b43_phyops_ht;

#endif /* B43_PHY_HT_H_ */
