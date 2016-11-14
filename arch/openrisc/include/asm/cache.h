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

#ifndef __ASM_OPENRISC_CACHE_H
#define __ASM_OPENRISC_CACHE_H

/* FIXME: How can we replace these with values from the CPU...
 * they shouldn't be hard-coded!
 */

#define __ro_after_init __read_mostly

#define L1_CACHE_BYTES 16
#define L1_CACHE_SHIFT 4

#endif /* __ASM_OPENRISC_CACHE_H */
