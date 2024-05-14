/* SPDX-License-Identifier: GPL-2.0-or-later */
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
 */

#ifndef __ASM_OPENRISC_TLB_H__
#define __ASM_OPENRISC_TLB_H__

/*
 * OpenRISC doesn't have an efficient flush_tlb_range() so use flush_tlb_mm()
 * for everything.
 */

#include <linux/pagemap.h>
#include <asm-generic/tlb.h>

#endif /* __ASM_OPENRISC_TLB_H__ */
