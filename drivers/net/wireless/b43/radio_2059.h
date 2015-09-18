#ifndef B43_RADIO_2059_H_
#define B43_RADIO_2059_H_

#include <linux/types.h>

#include "phy_ht.h"

#define R2059_C1			0x000
#define R2059_C2			0x400
#define R2059_C3			0x800
#define R2059_ALL			0xC00

#define R2059_RCAL_CONFIG			0x004
#define R2059_RFPLL_MASTER			0x011
#define R2059_RFPLL_MISC_EN			0x02b
#define R2059_RFPLL_MISC_CAL_RESETN		0x02e
#define R2059_XTAL_CONFIG2			0x0c0
#define R2059_RCCAL_START_R1_Q1_P1		0x13c
#define R2059_RCCAL_X1				0x13d
#define R2059_RCCAL_TRC0			0x13e
#define R2059_RCCAL_DONE_OSCCAP			0x140
#define R2059_RCAL_STATUS			0x145
#define R2059_RCCAL_MASTER			0x17f

/* Values for various registers uploaded on channel switching */
struct b43_phy_ht_channeltab_e_radio2059 {
	/* The channel frequency in MHz */
	u16 freq;
	/* Values for radio registers */
	u8 radio_syn16;
	u8 radio_syn17;
	u8 radio_syn22;
	u8 radio_syn25;
	u8 radio_syn27;
	u8 radio_syn28;
	u8 radio_syn29;
	u8 radio_syn2c;
	u8 radio_syn2d;
	u8 radio_syn37;
	u8 radio_syn41;
	u8 radio_syn43;
	u8 radio_syn47;
	u8 radio_rxtx4a;
	u8 radio_rxtx58;
	u8 radio_rxtx5a;
	u8 radio_rxtx6a;
	u8 radio_rxtx6d;
	u8 radio_rxtx6e;
	u8 radio_rxtx92;
	u8 radio_rxtx98;
	/* Values for PHY registers */
	struct b43_phy_ht_channeltab_e_phy phy_regs;
};

void r2059_upload_inittabs(struct b43_wldev *dev);

const struct b43_phy_ht_channeltab_e_radio2059
*b43_phy_ht_get_channeltab_e_r2059(struct b43_wldev *dev, u16 freq);

#endif /* B43_RADIO_2059_H_ */
