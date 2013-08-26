/*
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * based on METAG mach/arch.h (which in turn was based on ARM)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_MACH_DESC_H_
#define _ASM_ARC_MACH_DESC_H_

/**
 * struct machine_desc - Board specific callbacks, called from ARC common code
 *	Provided by each ARC board using MACHINE_START()/MACHINE_END(), so
 *	a multi-platform kernel builds with array of such descriptors.
 *	We extend the early DT scan to also match the DT's "compatible" string
 *	against the @dt_compat of all such descriptors, and one with highest
 *	"DT score" is selected as global @machine_desc.
 *
 * @name:		Board/SoC name
 * @dt_compat:		Array of device tree 'compatible' strings
 * 			(XXX: although only 1st entry is looked at)
 * @init_early:		Very early callback [called from setup_arch()]
 * @init_irq:		setup external IRQ controllers [called from init_IRQ()]
 * @init_smp:		for each CPU (e.g. setup IPI)
 * 			[(M):init_IRQ(), (o):start_kernel_secondary()]
 * @init_time:		platform specific clocksource/clockevent registration
 * 			[called from time_init()]
 * @init_machine:	arch initcall level callback (e.g. populate static
 * 			platform devices or parse Devicetree)
 * @init_late:		Late initcall level callback
 *
 */
struct machine_desc {
	const char		*name;
	const char		**dt_compat;

	void			(*init_early)(void);
	void			(*init_irq)(void);
#ifdef CONFIG_SMP
	void			(*init_smp)(unsigned int);
#endif
	void			(*init_time)(void);
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
 * Set of macros to define architecture features.
 * This is built into a table by the linker.
 */
#define MACHINE_START(_type, _name)			\
static const struct machine_desc __mach_desc_##_type	\
__used							\
__attribute__((__section__(".arch.info.init"))) = {	\
	.name		= _name,

#define MACHINE_END				\
};

extern struct machine_desc *setup_machine_fdt(void *dt);

#endif
