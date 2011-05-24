/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#ifndef _LANTIQ_MACH_H__
#define _LANTIQ_MACH_H__

#include <asm/mips_machine.h>

enum lantiq_mach_type {
	LTQ_MACH_GENERIC = 0,
	LTQ_MACH_EASY50712,	/* Danube evaluation board */
	LTQ_MACH_EASY50601,	/* Amazon SE evaluation board */
};

#endif
