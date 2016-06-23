/*
 * Software PHY emulation
 *
 * Code taken from fixed_phy.c by Russell King <rmk+kernel@arm.linux.org.uk>
 *
 * Author: Vitaly Bordug <vbordug@ru.mvista.com>
 *         Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * Copyright (c) 2006-2007 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/export.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>

#include "swphy.h"

#define MII_REGS_NUM 29

struct swmii_regs {
	u16 bmcr;
	u16 bmsr;
	u16 lpa;
	u16 lpagb;
};

enum {
	SWMII_SPEED_10 = 0,
	SWMII_SPEED_100,
	SWMII_SPEED_1000,
	SWMII_DUPLEX_HALF = 0,
	SWMII_DUPLEX_FULL,
};

/*
 * These two tables get bitwise-anded together to produce the final result.
 * This means the speed table must contain both duplex settings, and the
 * duplex table must contain all speed settings.
 */
static const struct swmii_regs speed[] = {
	[SWMII_SPEED_10] = {
		.bmcr  = BMCR_FULLDPLX,
		.lpa   = LPA_10FULL | LPA_10HALF,
	},
	[SWMII_SPEED_100] = {
		.bmcr  = BMCR_FULLDPLX | BMCR_SPEED100,
		.bmsr  = BMSR_100FULL | BMSR_100HALF,
		.lpa   = LPA_100FULL | LPA_100HALF,
	},
	[SWMII_SPEED_1000] = {
		.bmcr  = BMCR_FULLDPLX | BMCR_SPEED1000,
		.bmsr  = BMSR_ESTATEN,
		.lpagb = LPA_1000FULL | LPA_1000HALF,
	},
};

static const struct swmii_regs duplex[] = {
	[SWMII_DUPLEX_HALF] = {
		.bmcr  = ~BMCR_FULLDPLX,
		.bmsr  = BMSR_ESTATEN | BMSR_100HALF,
		.lpa   = LPA_10HALF | LPA_100HALF,
		.lpagb = LPA_1000HALF,
	},
	[SWMII_DUPLEX_FULL] = {
		.bmcr  = ~0,
		.bmsr  = BMSR_ESTATEN | BMSR_100FULL,
		.lpa   = LPA_10FULL | LPA_100FULL,
		.lpagb = LPA_1000FULL,
	},
};

static int swphy_decode_speed(int speed)
{
	switch (speed) {
	case 1000:
		return SWMII_SPEED_1000;
	case 100:
		return SWMII_SPEED_100;
	case 10:
		return SWMII_SPEED_10;
	default:
		return -EINVAL;
	}
}

/**
 * swphy_validate_state - validate the software phy status
 * @state: software phy status
 *
 * This checks that we can represent the state stored in @state can be
 * represented in the emulated MII registers.  Returns 0 if it can,
 * otherwise returns -EINVAL.
 */
int swphy_validate_state(const struct fixed_phy_status *state)
{
	int err;

	if (state->link) {
		err = swphy_decode_speed(state->speed);
		if (err < 0) {
			pr_warn("swphy: unknown speed\n");
			return -EINVAL;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(swphy_validate_state);

/**
 * swphy_read_reg - return a MII register from the fixed phy state
 * @reg: MII register
 * @state: fixed phy status
 *
 * Return the MII @reg register generated from the fixed phy state @state.
 */
int swphy_read_reg(int reg, const struct fixed_phy_status *state)
{
	int speed_index, duplex_index;
	u16 bmsr = BMSR_ANEGCAPABLE;
	u16 bmcr = 0;
	u16 lpagb = 0;
	u16 lpa = 0;

	if (reg > MII_REGS_NUM)
		return -1;

	speed_index = swphy_decode_speed(state->speed);
	if (WARN_ON(speed_index < 0))
		return 0;

	duplex_index = state->duplex ? SWMII_DUPLEX_FULL : SWMII_DUPLEX_HALF;

	bmsr |= speed[speed_index].bmsr & duplex[duplex_index].bmsr;

	if (state->link) {
		bmsr |= BMSR_LSTATUS | BMSR_ANEGCOMPLETE;

		bmcr  |= speed[speed_index].bmcr  & duplex[duplex_index].bmcr;
		lpa   |= speed[speed_index].lpa   & duplex[duplex_index].lpa;
		lpagb |= speed[speed_index].lpagb & duplex[duplex_index].lpagb;

		if (state->pause)
			lpa |= LPA_PAUSE_CAP;

		if (state->asym_pause)
			lpa |= LPA_PAUSE_ASYM;
	}

	switch (reg) {
	case MII_BMCR:
		return bmcr;
	case MII_BMSR:
		return bmsr;
	case MII_PHYSID1:
	case MII_PHYSID2:
		return 0;
	case MII_LPA:
		return lpa;
	case MII_STAT1000:
		return lpagb;

	/*
	 * We do not support emulating Clause 45 over Clause 22 register
	 * reads.  Return an error instead of bogus data.
	 */
	case MII_MMD_CTRL:
	case MII_MMD_DATA:
		return -1;

	default:
		return 0xffff;
	}
}
EXPORT_SYMBOL_GPL(swphy_read_reg);
