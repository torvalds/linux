/*
 * arch/metag/include/asm/mach/arch.h
 *
 * Copyright (C) 2012 Imagination Technologies Ltd.
 *
 * based on the ARM version:
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _METAG_MACH_ARCH_H_
#define _METAG_MACH_ARCH_H_

#include <linux/stddef.h>

/**
 * struct machine_desc - Describes a board controlled by a Meta.
 * @name:		Board/SoC name.
 * @dt_compat:		Array of device tree 'compatible' strings.
 *
 * @nr_irqs:		Maximum number of IRQs.
 *			If 0, defaults to NR_IRQS in asm-generic/irq.h.
 *
 * @init_early:		Early init callback.
 * @init_irq:		IRQ init callback for setting up IRQ controllers.
 * @init_machine:	Arch init callback for setting up devices.
 * @init_late:		Late init callback.
 *
 * This structure is provided by each board which can be controlled by a Meta.
 * It is chosen by matching the compatible strings in the device tree provided
 * by the bootloader with the strings in @dt_compat, and sets up any aspects of
 * the machine that aren't configured with device tree (yet).
 */
struct machine_desc {
	const char		*name;
	const char		**dt_compat;

	unsigned int		nr_irqs;

	void			(*init_early)(void);
	void			(*init_irq)(void);
	void			(*init_machine)(void);
	void			(*init_late)(void);
};

/*
 * Current machine - only accessible during boot.
 */
extern struct machine_desc *machine_desc;

/*
 * Machine type table - also only accessible during boot
 */
extern struct machine_desc __arch_info_begin[], __arch_info_end[];
#define for_each_machine_desc(p)			\
	for (p = __arch_info_begin; p < __arch_info_end; p++)

static inline struct machine_desc *default_machine_desc(void)
{
	/* the default machine is the last one linked in */
	if (__arch_info_end - 1 < __arch_info_begin)
		return NULL;
	return __arch_info_end - 1;
}

/*
 * Set of macros to define architecture features.  This is built into
 * a table by the linker.
 */
#define MACHINE_START(_type, _name)			\
static const struct machine_desc __mach_desc_##_type	\
__used							\
__attribute__((__section__(".arch.info.init"))) = {	\
	.name		= _name,

#define MACHINE_END				\
};

#endif /* _METAG_MACH_ARCH_H_ */
