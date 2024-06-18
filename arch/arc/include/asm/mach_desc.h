/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * based on METAG mach/arch.h (which in turn was based on ARM)
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
 * @init_per_cpu:	for each CPU as it is coming up (SMP as well as UP)
 * 			[(M):init_IRQ(), (o):start_kernel_secondary()]
 * @init_machine:	arch initcall level callback (e.g. populate static
 * 			platform devices or parse Devicetree)
 * @init_late:		Late initcall level callback
 *
 */
struct machine_desc {
	const char		*name;
	const char		**dt_compat;
	void			(*init_early)(void);
	void			(*init_per_cpu)(unsigned int);
	void			(*init_machine)(void);
	void			(*init_late)(void);

};

/*
 * Current machine - only accessible during boot.
 */
extern const struct machine_desc *machine_desc;

/*
 * Machine type table - also only accessible during boot
 */
extern const struct machine_desc __arch_info_begin[], __arch_info_end[];

/*
 * Set of macros to define architecture features.
 * This is built into a table by the linker.
 */
#define MACHINE_START(_type, _name)			\
static const struct machine_desc __mach_desc_##_type	\
__used __section(".arch.info.init") = {			\
	.name		= _name,

#define MACHINE_END				\
};

extern const struct machine_desc *setup_machine_fdt(void *dt);

#endif
