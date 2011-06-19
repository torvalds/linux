#ifndef B43_PHY_HT_H_
#define B43_PHY_HT_H_

#include "phy_common.h"


#define B43_PHY_HT_TABLE_ADDR			0x072 /* Table address */
#define B43_PHY_HT_TABLE_DATALO			0x073 /* Table data low */
#define B43_PHY_HT_TABLE_DATAHI			0x074 /* Table data high */

#define B43_PHY_HT_RF_CTL1			B43_PHY_EXTG(0x010)

#define B43_PHY_HT_AFE_CTL1			B43_PHY_EXTG(0x110)
#define B43_PHY_HT_AFE_CTL2			B43_PHY_EXTG(0x111)
#define B43_PHY_HT_AFE_CTL3			B43_PHY_EXTG(0x114)
#define B43_PHY_HT_AFE_CTL4			B43_PHY_EXTG(0x115)
#define B43_PHY_HT_AFE_CTL5			B43_PHY_EXTG(0x118)
#define B43_PHY_HT_AFE_CTL6			B43_PHY_EXTG(0x119)


struct b43_phy_ht {
};


struct b43_phy_operations;
extern const struct b43_phy_operations b43_phyops_ht;

#endif /* B43_PHY_HT_H_ */
