/*
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 */

#ifndef __ASM_MIPS_MACHINE_H
#define __ASM_MIPS_MACHINE_H

#include <linux/init.h>
#include <linux/stddef.h>

#include <asm/bootinfo.h>

struct mips_machine {
	unsigned long		mach_type;
	const char		*mach_id;
	const char		*mach_name;
	void			(*mach_setup)(void);
};

#define MIPS_MACHINE(_type, _id, _name, _setup)			\
static const char machine_name_##_type[] __initconst		\
			__aligned(1) = _name;			\
static const char machine_id_##_type[] __initconst		\
			__aligned(1) = _id;			\
static struct mips_machine machine_##_type			\
		__used __section(.mips.machines.init) =		\
{								\
	.mach_type	= _type,				\
	.mach_id	= machine_id_##_type,			\
	.mach_name	= machine_name_##_type,			\
	.mach_setup	= _setup,				\
};

extern long __mips_machines_start;
extern long __mips_machines_end;

#ifdef CONFIG_MIPS_MACHINE
int  mips_machtype_setup(char *id) __init;
void mips_machine_setup(void) __init;
#else
static inline int mips_machtype_setup(char *id) { return 1; }
static inline void mips_machine_setup(void) { }
#endif /* CONFIG_MIPS_MACHINE */

#endif /* __ASM_MIPS_MACHINE_H */
