#ifndef B43_PHY_HT_H_
#define B43_PHY_HT_H_

#include "phy_common.h"


#define B43_PHY_HT_TABLE_ADDR			0x072 /* Table address */
#define B43_PHY_HT_TABLE_DATALO			0x073 /* Table data low */
#define B43_PHY_HT_TABLE_DATAHI			0x074 /* Table data high */


struct b43_phy_ht {
};


struct b43_phy_operations;
extern const struct b43_phy_operations b43_phyops_ht;

#endif /* B43_PHY_HT_H_ */
