/* arch/arm/mach-msm/vreg.c
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>
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

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <mach/vreg.h>

#include "proc_comm.h"

struct vreg {
	const char *name;
	unsigned id;
};

#define VREG(_name, _id) { .name = _name, .id = _id, }

static struct vreg vregs[] = {
	VREG("msma",	0),
	VREG("msmp",	1),
	VREG("msme1",	2),
	VREG("msmc1",	3),
	VREG("msmc2",	4),
	VREG("gp3",	5),
	VREG("msme2",	6),
	VREG("gp4",	7),
	VREG("gp1",	8),
	VREG("tcxo",	9),
	VREG("pa",	10),
	VREG("rftx",	11),
	VREG("rfrx1",	12),
	VREG("rfrx2",	13),
	VREG("synt",	14),
	VREG("wlan",	15),
	VREG("usb",	16),
	VREG("boost",	17),
	VREG("mmc",	18),
	VREG("ruim",	19),
	VREG("msmc0",	20),
	VREG("gp2",	21),
	VREG("gp5",	22),
	VREG("gp6",	23),
	VREG("rf",	24),
	VREG("rf_vco",	26),
	VREG("mpll",	27),
	VREG("s2",	28),
	VREG("s3",	29),
	VREG("rfubm",	30),
	VREG("ncp",	31),
};

struct vreg *vreg_get(struct device *dev, const char *id)
{
	int n;
	for (n = 0; n < ARRAY_SIZE(vregs); n++) {
		if (!strcmp(vregs[n].name, id))
			return vregs + n;
	}
	return 0;
}

void vreg_put(struct vreg *vreg)
{
}

int vreg_enable(struct vreg *vreg)
{
	unsigned id = vreg->id;
	unsigned enable = 1;
	return msm_proc_comm(PCOM_VREG_SWITCH, &id, &enable);
}

void vreg_disable(struct vreg *vreg)
{
	unsigned id = vreg->id;
	unsigned enable = 0;
	msm_proc_comm(PCOM_VREG_SWITCH, &id, &enable);
}

int vreg_set_level(struct vreg *vreg, unsigned mv)
{
	unsigned id = vreg->id;
	return msm_proc_comm(PCOM_VREG_SET_LEVEL, &id, &mv);
}

#if defined(CONFIG_DEBUG_FS)

static int vreg_debug_set(void *data, u64 val)
{
	struct vreg *vreg = data;
	switch (val) {
	case 0:
		vreg_disable(vreg);
		break;
	case 1:
		vreg_enable(vreg);
		break;
	default:
		vreg_set_level(vreg, val);
		break;
	}
	return 0;
}

static int vreg_debug_get(void *data, u64 *val)
{
	return -ENOSYS;
}

DEFINE_SIMPLE_ATTRIBUTE(vreg_fops, vreg_debug_get, vreg_debug_set, "%llu\n");

static int __init vreg_debug_init(void)
{
	struct dentry *dent;
	int n;

	dent = debugfs_create_dir("vreg", 0);
	if (IS_ERR(dent))
		return 0;

	for (n = 0; n < ARRAY_SIZE(vregs); n++)
		(void) debugfs_create_file(vregs[n].name, 0644,
					   dent, vregs + n, &vreg_fops);

	return 0;
}

device_initcall(vreg_debug_init);
#endif
