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

/**
 * swphy_update_regs - update MII register array with fixed phy state
 * @regs: array of 32 registers to update
 * @state: fixed phy status
 *
 * Update the array of MII registers with the fixed phy link, speed,
 * duplex and pause mode settings.
 */
int swphy_update_regs(u16 *regs, const struct fixed_phy_status *state)
{
	u16 bmsr = BMSR_ANEGCAPABLE;
	u16 bmcr = 0;
	u16 lpagb = 0;
	u16 lpa = 0;

	if (state->duplex) {
		switch (state->speed) {
		case 1000:
			bmsr |= BMSR_ESTATEN;
			break;
		case 100:
			bmsr |= BMSR_100FULL;
			break;
		case 10:
			bmsr |= BMSR_10FULL;
			break;
		default:
			break;
		}
	} else {
		switch (state->speed) {
		case 1000:
			bmsr |= BMSR_ESTATEN;
			break;
		case 100:
			bmsr |= BMSR_100HALF;
			break;
		case 10:
			bmsr |= BMSR_10HALF;
			break;
		default:
			break;
		}
	}

	if (state->link) {
		bmsr |= BMSR_LSTATUS | BMSR_ANEGCOMPLETE;

		if (state->duplex) {
			bmcr |= BMCR_FULLDPLX;

			switch (state->speed) {
			case 1000:
				bmcr |= BMCR_SPEED1000;
				lpagb |= LPA_1000FULL;
				break;
			case 100:
				bmcr |= BMCR_SPEED100;
				lpa |= LPA_100FULL;
				break;
			case 10:
				lpa |= LPA_10FULL;
				break;
			default:
				pr_warn("swphy: unknown speed\n");
				return -EINVAL;
			}
		} else {
			switch (state->speed) {
			case 1000:
				bmcr |= BMCR_SPEED1000;
				lpagb |= LPA_1000HALF;
				break;
			case 100:
				bmcr |= BMCR_SPEED100;
				lpa |= LPA_100HALF;
				break;
			case 10:
				lpa |= LPA_10HALF;
				break;
			default:
				pr_warn("swphy: unknown speed\n");
				return -EINVAL;
			}
		}

		if (state->pause)
			lpa |= LPA_PAUSE_CAP;

		if (state->asym_pause)
			lpa |= LPA_PAUSE_ASYM;
	}

	regs[MII_PHYSID1] = 0;
	regs[MII_PHYSID2] = 0;

	regs[MII_BMSR] = bmsr;
	regs[MII_BMCR] = bmcr;
	regs[MII_LPA] = lpa;
	regs[MII_STAT1000] = lpagb;

	return 0;
}
EXPORT_SYMBOL_GPL(swphy_update_regs);
