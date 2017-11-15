/*
 * Based on arch/arm/include/asm/barrier.h
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2013 Regents of the University of California
 * Copyright (C) 2017 SiFive
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ASM_RISCV_BARRIER_H
#define _ASM_RISCV_BARRIER_H

#ifndef __ASSEMBLY__

#define nop()		__asm__ __volatile__ ("nop")

#define RISCV_FENCE(p, s) \
	__asm__ __volatile__ ("fence " #p "," #s : : : "memory")

/* These barriers need to enforce ordering on both devices or memory. */
#define mb()		RISCV_FENCE(iorw,iorw)
#define rmb()		RISCV_FENCE(ir,ir)
#define wmb()		RISCV_FENCE(ow,ow)

/* These barriers do not need to enforce ordering on devices, just memory. */
#define smp_mb()	RISCV_FENCE(rw,rw)
#define smp_rmb()	RISCV_FENCE(r,r)
#define smp_wmb()	RISCV_FENCE(w,w)

/*
 * These fences exist to enforce ordering around the relaxed AMOs.  The
 * documentation defines that
 * "
 *     atomic_fetch_add();
 *   is equivalent to:
 *     smp_mb__before_atomic();
 *     atomic_fetch_add_relaxed();
 *     smp_mb__after_atomic();
 * "
 * So we emit full fences on both sides.
 */
#define __smb_mb__before_atomic()	smp_mb()
#define __smb_mb__after_atomic()	smp_mb()

/*
 * These barriers prevent accesses performed outside a spinlock from being moved
 * inside a spinlock.  Since RISC-V sets the aq/rl bits on our spinlock only
 * enforce release consistency, we need full fences here.
 */
#define smb_mb__before_spinlock()	smp_mb()
#define smb_mb__after_spinlock()	smp_mb()

#include <asm-generic/barrier.h>

#endif /* __ASSEMBLY__ */

#endif /* _ASM_RISCV_BARRIER_H */
