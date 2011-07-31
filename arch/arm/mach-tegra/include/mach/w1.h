/*
 * include/mach/w1.h
 *
 * Copyright (C) 2010 Motorola, Inc
 * Author: Andrei Warkentin <andreiw@motorola.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __ASM_ARM_ARCH_TEGRA_W1_H
#define __ASM_ARM_ARCH_TEGRA_W1_H

struct tegra_w1_timings {

	/* tsu, trelease, trdv, tlow0, tlow1 and tslot are formed
	   into the value written into OWR_RST_PRESENCE_TCTL_0 register. */

	/* Read data setup, Tsu = N owr clks, Range = tsu < 1,
	   Typical value = 0x1 */
	uint32_t tsu;

	/* Release 1-wire time, Trelease = N owr clks,
	   Range = 0 <= trelease < 45, Typical value = 0xf */
	uint32_t trelease;

	/*  Read data valid time, Trdv = N+1 owr clks, Range = Exactly 15 */
	uint32_t trdv;

	/* Write zero time low, Tlow0 = N+1 owr clks,
	   Range = 60 <= tlow0 < tslot < 120, typical value = 0x3c. */
	uint32_t tlow0;

	/* Write one time low, or TLOWR both are same Tlow1 = N+1 owr clks,
	   Range = 1 <= tlow1 < 15 TlowR = N+1 owr clks,
	   Range = 1 <= tlowR < 15, Typical value = 0x1. */
	uint32_t tlow1;

	/* Active time slot for write or read data, Tslot = N+1 owr clks,
	   Range = 60 <= tslot < 120, Typical value = 0x77. */
	uint32_t tslot;

	/* tpdl, tpdh, trstl, trsth are formed in the the value written
	   into the OWR_RST_PRESENCE_TCTL_0 register. */

	/* Tpdl = N owr clks, Range = 60 <= tpdl < 240,
	   Typical value = 0x78. */
	uint32_t tpdl;

	/* Tpdh = N+1 owr clks, Range = 15 <= tpdh < 60.
	   Typical value = 0x1e. */
	uint32_t tpdh;

	/* Trstl = N+1 owr clks, Range = 480 <= trstl < infinity,
	   Typical value = 0x1df. */
	uint32_t trstl;

	/* Trsth = N+1 owr clks, Range = 480 <= trsth < infinity,
	 Typical value = 0x1df. */
	uint32_t trsth;

	/* Read data sample clock. Should be <= to (tlow1 - 6) clks,
	   6 clks are used for deglitch. If deglitch  bypassed it
	   is 3 clks, Typical value = 0x7. */
	uint32_t rdsclk;

	/* Presence sample clock. Should be <= to (tpdl - 6) clks,
	   6 clks are used for deglitch. If deglitch bypassed it is 3 clks,
	   Typical value = 0x50. */
	uint32_t psclk;
};

struct tegra_w1_platform_data {
	const char *clk_id;
	struct tegra_w1_timings *timings;
};

#endif
