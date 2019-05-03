/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Generic code to add IPMI platform devices.
 */

#include <linux/ipmi.h>

struct ipmi_plat_data {
	unsigned int type; /* si_type for si, SI_INVALID for others */
	unsigned int space; /* addr_space for si, intf# for ssif. */
	unsigned long addr;
	unsigned int regspacing;
	unsigned int regsize;
	unsigned int regshift;
	unsigned int irq;
	unsigned int slave_addr;
	enum ipmi_addr_src addr_source;
};

struct platform_device *ipmi_platform_add(const char *name, unsigned int inst,
					  struct ipmi_plat_data *p);
