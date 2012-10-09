/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000,2012 MIPS Technologies, Inc.  All rights reserved.
 *      Douglas Leung <douglas@mips.com>
 *      Steven J. Hill <sjhill@mips.com>
 */
#ifndef _MIPS_SEAD3INT_H
#define _MIPS_SEAD3INT_H

/* SEAD-3 GIC address space definitions. */
#define GIC_BASE_ADDR		0x1b1c0000
#define GIC_ADDRSPACE_SZ	(128 * 1024)

#define MIPS_GIC_IRQ_BASE	(MIPS_CPU_IRQ_BASE + 0)

#endif /* !(_MIPS_SEAD3INT_H) */
