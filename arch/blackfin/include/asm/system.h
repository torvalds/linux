/*
 * File:        include/asm/system.h
 * Based on:
 * Author:      Tony Kou (tonyko@lineo.ca)
 *              Copyright (c) 2002 Arcturus Networks Inc.
 *                    (www.arcturusnetworks.com)
 *              Copyright (c) 2003 Metrowerks (www.metrowerks.com)
 *              Copyright (c) 2004 Analog Device Inc.
 * Created:     25Jan2001 - Tony Kou
 * Description: system.h include file
 *
 * Modified:     22Sep2006 - Robin Getz
 *                - move include blackfin.h down, so I can get access to
 *                   irq functions in other include files.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _BLACKFIN_SYSTEM_H
#define _BLACKFIN_SYSTEM_H

#include <linux/linkage.h>
#include <linux/irqflags.h>
#include <mach/anomaly.h>
#include <asm/cache.h>
#include <asm/pda.h>
#include <asm/irq.h>

/*
 * Force strict CPU ordering.
 */
#define nop()  __asm__ __volatile__ ("nop;\n\t" : : )
#define mb()   __asm__ __volatile__ (""   : : : "memory")
#define rmb()  __asm__ __volatile__ (""   : : : "memory")
#define wmb()  __asm__ __volatile__ (""   : : : "memory")
#define set_mb(var, value) do { (void) xchg(&var, value); } while (0)
#define read_barrier_depends() 		do { } while(0)

#ifdef CONFIG_SMP
asmlinkage unsigned long __raw_xchg_1_asm(volatile void *ptr, unsigned long value);
asmlinkage unsigned long __raw_xchg_2_asm(volatile void *ptr, unsigned long value);
asmlinkage unsigned long __raw_xchg_4_asm(volatile void *ptr, unsigned long value);
asmlinkage unsigned long __raw_cmpxchg_1_asm(volatile void *ptr,
					unsigned long new, unsigned long old);
asmlinkage unsigned long __raw_cmpxchg_2_asm(volatile void *ptr,
					unsigned long new, unsigned long old);
asmlinkage unsigned long __raw_cmpxchg_4_asm(volatile void *ptr,
					unsigned long new, unsigned long old);

#ifdef __ARCH_SYNC_CORE_DCACHE
# define smp_mb()	do { barrier(); smp_check_barrier(); smp_mark_barrier(); } while (0)
# define smp_rmb()	do { barrier(); smp_check_barrier(); } while (0)
# define smp_wmb()	do { barrier(); smp_mark_barrier(); } while (0)
#define smp_read_barrier_depends()	do { barrier(); smp_check_barrier(); } while (0)

#else
# define smp_mb()	barrier()
# define smp_rmb()	barrier()
# define smp_wmb()	barrier()
#define smp_read_barrier_depends()	barrier()
#endif

static inline unsigned long __xchg(unsigned long x, volatile void *ptr,
				   int size)
{
	unsigned long tmp;

	switch (size) {
	case 1:
		tmp = __raw_xchg_1_asm(ptr, x);
		break;
	case 2:
		tmp = __raw_xchg_2_asm(ptr, x);
		break;
	case 4:
		tmp = __raw_xchg_4_asm(ptr, x);
		break;
	}

	return tmp;
}

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */
static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	unsigned long tmp;

	switch (size) {
	case 1:
		tmp = __raw_cmpxchg_1_asm(ptr, new, old);
		break;
	case 2:
		tmp = __raw_cmpxchg_2_asm(ptr, new, old);
		break;
	case 4:
		tmp = __raw_cmpxchg_4_asm(ptr, new, old);
		break;
	}

	return tmp;
}
#define cmpxchg(ptr, o, n) \
	((__typeof__(*(ptr)))__cmpxchg((ptr), (unsigned long)(o), \
		(unsigned long)(n), sizeof(*(ptr))))

#else /* !CONFIG_SMP */

#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do { } while(0)

struct __xchg_dummy {
	unsigned long a[100];
};
#define __xg(x) ((volatile struct __xchg_dummy *)(x))

#include <mach/blackfin.h>

static inline unsigned long __xchg(unsigned long x, volatile void *ptr,
				   int size)
{
	unsigned long tmp = 0;
	unsigned long flags;

	local_irq_save_hw(flags);

	switch (size) {
	case 1:
		__asm__ __volatile__
			("%0 = b%2 (z);\n\t"
			 "b%2 = %1;\n\t"
			 : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	case 2:
		__asm__ __volatile__
			("%0 = w%2 (z);\n\t"
			 "w%2 = %1;\n\t"
			 : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	case 4:
		__asm__ __volatile__
			("%0 = %2;\n\t"
			 "%2 = %1;\n\t"
			 : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	}
	local_irq_restore_hw(flags);
	return tmp;
}

#include <asm-generic/cmpxchg-local.h>

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)				  	       \
	((__typeof__(*(ptr)))__cmpxchg_local_generic((ptr), (unsigned long)(o),\
			(unsigned long)(n), sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

#include <asm-generic/cmpxchg.h>

#endif /* !CONFIG_SMP */

#define xchg(ptr, x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x), (ptr), sizeof(*(ptr))))
#define tas(ptr) ((void)xchg((ptr), 1))

#define prepare_to_switch()     do { } while(0)

/*
 * switch_to(n) should switch tasks to task ptr, first checking that
 * ptr isn't the current task, in which case it does nothing.
 */

#include <asm/l1layout.h>
#include <asm/mem_map.h>

asmlinkage struct task_struct *resume(struct task_struct *prev, struct task_struct *next);

#ifndef CONFIG_SMP
#define switch_to(prev,next,last) \
do {    \
	memcpy (&task_thread_info(prev)->l1_task_info, L1_SCRATCH_TASK_INFO, \
		sizeof *L1_SCRATCH_TASK_INFO); \
	memcpy (L1_SCRATCH_TASK_INFO, &task_thread_info(next)->l1_task_info, \
		sizeof *L1_SCRATCH_TASK_INFO); \
	(last) = resume (prev, next);   \
} while (0)
#else
#define switch_to(prev, next, last) \
do {    \
	(last) = resume(prev, next);   \
} while (0)
#endif

#endif	/* _BLACKFIN_SYSTEM_H */
