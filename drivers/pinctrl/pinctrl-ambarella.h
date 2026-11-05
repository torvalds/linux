/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Ambarella pinctrl SoC data
 *
 * Copyright (C) 2012-2026, Ambarella, Inc.
 */

#ifndef _PINCTRL_AMBARELLA_H
#define _PINCTRL_AMBARELLA_H

#include <linux/types.h>

#define AMBA_MAX_BANKS			8

struct pingroup;
struct pinfunction;

struct amb_pinmux_group {
	const struct pingroup *grp;
	const u8 *alts;
};

struct amb_pinctrl_data {
	const struct amb_pinmux_group *groups;
	const struct pinfunction *functions;
	unsigned int ngroups;
	unsigned int nfunctions;
	unsigned int nr_banks;
	unsigned int npins;
	unsigned int ds0[AMBA_MAX_BANKS];
	unsigned int ds1[AMBA_MAX_BANKS];
	unsigned int ds2[AMBA_MAX_BANKS];
	unsigned int pull_en[AMBA_MAX_BANKS];
	unsigned int pull_dir[AMBA_MAX_BANKS];
	bool have_ds2;
};

extern const struct amb_pinctrl_data ambarella_cv75_pinctrl_data;

#endif /* _PINCTRL_AMBARELLA_H */
