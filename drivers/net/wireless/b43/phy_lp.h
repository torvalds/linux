#ifndef LINUX_B43_PHY_LP_H_
#define LINUX_B43_PHY_LP_H_

/* Definitions for the LP-PHY */

struct b43_phy_lp {
	bool initialised;
};


struct b43_phy_operations;
extern const struct b43_phy_operations b43_phyops_lp;

#endif /* LINUX_B43_PHY_LP_H_ */
