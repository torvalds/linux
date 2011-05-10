/*
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 */
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <asm/mips_machine.h>

static struct mips_machine *mips_machine __initdata;
static char *mips_machine_name = "Unknown";

#define for_each_machine(mach) \
	for ((mach) = (struct mips_machine *)&__mips_machines_start; \
	     (mach) && \
	     (unsigned long)(mach) < (unsigned long)&__mips_machines_end; \
	     (mach)++)

__init void mips_set_machine_name(const char *name)
{
	char *p;

	if (name == NULL)
		return;

	p = kstrdup(name, GFP_KERNEL);
	if (!p)
		pr_err("MIPS: no memory for machine_name\n");

	mips_machine_name = p;
}

char *mips_get_machine_name(void)
{
	return mips_machine_name;
}

__init int mips_machtype_setup(char *id)
{
	struct mips_machine *mach;

	for_each_machine(mach) {
		if (mach->mach_id == NULL)
			continue;

		if (strcmp(mach->mach_id, id) == 0) {
			mips_machtype = mach->mach_type;
			return 0;
		}
	}

	pr_err("MIPS: no machine found for id '%s', supported machines:\n", id);
	pr_err("%-24s %s\n", "id", "name");
	for_each_machine(mach)
		pr_err("%-24s %s\n", mach->mach_id, mach->mach_name);

	return 1;
}

__setup("machtype=", mips_machtype_setup);

__init void mips_machine_setup(void)
{
	struct mips_machine *mach;

	for_each_machine(mach) {
		if (mips_machtype == mach->mach_type) {
			mips_machine = mach;
			break;
		}
	}

	if (!mips_machine)
		return;

	mips_set_machine_name(mips_machine->mach_name);
	pr_info("MIPS: machine is %s\n", mips_machine_name);

	if (mips_machine->mach_setup)
		mips_machine->mach_setup();
}
