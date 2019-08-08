// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 */
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <asm/mips_machine.h>
#include <asm/prom.h>

static struct mips_machine *mips_machine __initdata;

#define for_each_machine(mach) \
	for ((mach) = (struct mips_machine *)&__mips_machines_start; \
	     (mach) && \
	     (unsigned long)(mach) < (unsigned long)&__mips_machines_end; \
	     (mach)++)

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

	if (mips_machine->mach_setup)
		mips_machine->mach_setup();
}
