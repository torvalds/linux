/*
 * Based on arch/arm/include/asm/barrier.h
 *
 * Copyright (C) 2012 ARM Ltd.
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
#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#ifndef __ASSEMBLY__

#define sev()		asm volatile("sev" : : : "memory")
#define wfe()		asm volatile("wfe" : : : "memory")
#define wfi()		asm volatile("wfi" : : : "memory")

#define isb()		asm volatile("isb" : : : "memory")
#define dmb(opt)	asm volatile("dmb sy" : : : "memory")
#define dsb(opt)	asm volatile("dsb sy" : : : "memory")

#define mb()		dsb(sy)
#define rmb()		asm volatile("dsb ld" : : : "memory")
#define wmb()		asm volatile("dsb st" : : : "memory")

#ifndef CONFIG_SMP
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()

#define smp_store_release(p, v)						\
do {									\
	compiletime_assert_atomic_type(*p);				\
	smp_mb();							\
	ACCESS_ONCE(*p) = (v);						\
} while (0)

#define smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = ACCESS_ONCE(*p);				\
	compiletime_assert_atomic_type(*p);				\
	smp_mb();							\
	___p1;								\
})

#else

#define smp_mb()	asm volatile("dmb ish" : : : "memory")
#define smp_rmb()	asm volatile("dmb ishld" : : : "memory")
#define smp_wmb()	asm volatile("dmb ishst" : : : "memory")

#define smp_store_release(p, v)						\
do {									\
	compiletime_assert_atomic_type(*p);				\
	switch (sizeof(*p)) {						\
	case 4:								\
		asm volatile ("stlr %w1, %0"				\
				: "=Q" (*p) : "r" (v) : "memory");	\
		break;							\
	case 8:								\
		asm volatile ("stlr %1, %0"				\
				: "=Q" (*p) : "r" (v) : "memory");	\
		break;							\
	}								\
} while (0)

#define smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1;						\
	compiletime_assert_atomic_type(*p);				\
	switch (sizeof(*p)) {						\
	case 4:								\
		asm volatile ("ldar %w0, %1"				\
			: "=r" (___p1) : "Q" (*p) : "memory");		\
		break;							\
	case 8:								\
		asm volatile ("ldar %0, %1"				\
			: "=r" (___p1) : "Q" (*p) : "memory");		\
		break;							\
	}								\
	___p1;								\
})

#endif

#define read_barrier_depends()		do { } while(0)
#define smp_read_barrier_depends()	do { } while(0)

#define set_mb(var, value)	do { var = value; smp_mb(); } while (0)
#define nop()		asm volatile("nop");

#endif	/* __ASSEMBLY__ */

#endif	/* __ASM_BARRIER_H */
