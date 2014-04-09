/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _ASM_NIOS2_REGISTERS_H
#define _ASM_NIOS2_REGISTERS_H

/* control register numbers */
#define CTL_STATUS	0
#define CTL_ESTATUS	1
#define CTL_BSTATUS	2
#define CTL_IENABLE	3
#define CTL_IPENDING	4
#define CTL_CPUID	5
#define CTL_RSV1	6
#define CTL_EXCEPTION	7
#define CTL_PTEADDR	8
#define CTL_TLBACC	9
#define CTL_TLBMISC	10
#define CTL_RSV2	11
#define CTL_BADADDR	12
#define CTL_CONFIG	13
#define CTL_MPUBASE	14
#define CTL_MPUACC	15

/* access control registers using GCC builtins */
#define RDCTL(r)	__builtin_rdctl(r)
#define WRCTL(r, v)	__builtin_wrctl(r, v)

/* status register bits */
#define STATUS_PIE	(1 << 0)	/* processor interrupt enable */
#define STATUS_U	(1 << 1)	/* user mode */
#define STATUS_EH	(1 << 2)	/* Exception mode */

/* estatus register bits */
#define ESTATUS_EPIE	(1 << 0)	/* processor interrupt enable */
#define ESTATUS_EU	(1 << 1)	/* user mode */
#define ESTATUS_EH	(1 << 2)	/* Exception mode */

/* tlbmisc register bits */
#define TLBMISC_PID_SHIFT	4
#define TLBMISC_PID_MASK	((1UL << cpuinfo.tlb_pid_num_bits) - 1)
#define TLBMISC_WAY_MASK	0xf
#define TLBMISC_WAY_SHIFT	20

#define TLBMISC_PID	(TLBMISC_PID_MASK << TLBMISC_PID_SHIFT)	/* TLB PID */
#define TLBMISC_WE	(1 << 18)	/* TLB write enable */
#define TLBMISC_RD	(1 << 19)	/* TLB read */
#define TLBMISC_WAY	(TLBMISC_WAY_MASK << TLBMISC_WAY_SHIFT) /* TLB way */

#endif /* _ASM_NIOS2_REGISTERS_H */
