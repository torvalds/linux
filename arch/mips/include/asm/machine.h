/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MIPS_ASM_MACHINE_H__
#define __MIPS_ASM_MACHINE_H__

#include <linux/libfdt.h>
#include <linux/of.h>

struct mips_machine {
	const struct of_device_id *matches;
	const void *fdt;
	bool (*detect)(void);
	const void *(*fixup_fdt)(const void *fdt, const void *match_data);
	unsigned int (*measure_hpt_freq)(void);
};

extern long __mips_machines_start;
extern long __mips_machines_end;

#define MIPS_MACHINE(name)						\
	static const struct mips_machine __mips_mach_##name		\
		__used __section(.mips.machines.init)

#define for_each_mips_machine(mach)					\
	for ((mach) = (struct mips_machine *)&__mips_machines_start;	\
	     (mach) < (struct mips_machine *)&__mips_machines_end;	\
	     (mach)++)

/**
 * mips_machine_is_compatible() - check if a machine is compatible with an FDT
 * @mach: the machine struct to check
 * @fdt: the FDT to check for compatibility with
 *
 * Check whether the given machine @mach is compatible with the given flattened
 * device tree @fdt, based upon the compatibility property of the root node.
 *
 * Return: the device id matched if any, else NULL
 */
static inline const struct of_device_id *
mips_machine_is_compatible(const struct mips_machine *mach, const void *fdt)
{
	const struct of_device_id *match;

	if (!mach->matches)
		return NULL;

	for (match = mach->matches; match->compatible; match++) {
		if (fdt_node_check_compatible(fdt, 0, match->compatible) == 0)
			return match;
	}

	return NULL;
}

#endif /* __MIPS_ASM_MACHINE_H__ */
