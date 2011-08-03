/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __ASM_OPENRISC_SYSTEM_H
#define __ASM_OPENRISC_SYSTEM_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <asm/spr.h>
#include <asm-generic/system.h>

/* We probably need this definition, but the generic system.h provides it
 * and it's not used on our arch anyway...
 */
/*#define nop() __asm__ __volatile__ ("l.nop"::)*/

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* __ASM_OPENRISC_SYSTEM_H */
