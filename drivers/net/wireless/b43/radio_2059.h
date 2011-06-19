#ifndef B43_RADIO_2059_H_
#define B43_RADIO_2059_H_

#include <linux/types.h>

#include "phy_ht.h"

/* Values for various registers uploaded on channel switching */
struct b43_phy_ht_channeltab_e_radio2059 {
	/* The channel frequency in MHz */
	u16 freq;
	/* Values for radio registers */
	/* TODO */
	/* Values for PHY registers */
	struct b43_phy_ht_channeltab_e_phy phy_regs;
};

const struct b43_phy_ht_channeltab_e_radio2059
*b43_phy_ht_get_channeltab_e_r2059(struct b43_wldev *dev, u16 freq);

#endif /* B43_RADIO_2059_H_ */
