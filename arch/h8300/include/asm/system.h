#ifndef _H8300_SYSTEM_H
#define _H8300_SYSTEM_H

#include <linux/linkage.h>

/*
 * switch_to(n) should switch tasks to task ptr, first checking that
 * ptr isn't the current task, in which case it does nothing.  This
 * also clears the TS-flag if the task we switched to has used the
 * math co-processor latest.
 */
/*
 * switch_to() saves the extra registers, that are not saved
 * automatically by SAVE_SWITCH_STACK in resume(), ie. d0-d5 and
 * a0-a1. Some of these are used by schedule() and its predecessors
 * and so we might get see unexpected behaviors when a task returns
 * with unexpected register values.
 *
 * syscall stores these registers itself and none of them are used
 * by syscall after the function in the syscall has been called.
 *
 * Beware that resume now expects *next to be in d1 and the offset of
 * tss to be in a1. This saves a few instructions as we no longer have
 * to push them onto the stack and read them back right after.
 *
 * 02/17/96 - Jes Sorensen (jds@kom.auc.dk)
 *
 * Changed 96/09/19 by Andreas Schwab
 * pass prev in a0, next in a1, offset of tss in d1, and whether
 * the mm structures are shared in d2 (to avoid atc flushing).
 *
 * H8/300 Porting 2002/09/04 Yoshinori Sato
 */

asmlinkage void resume(void);
#define switch_to(prev,next,last) {                         \
  void *_last;						    \
  __asm__ __volatile__(					    \
  			"mov.l	%1, er0\n\t"		    \
			"mov.l	%2, er1\n\t"		    \
                        "mov.l  %3, er2\n\t"                \
			"jsr @_resume\n\t"                  \
                        "mov.l  er2,%0\n\t"                 \
		       : "=r" (_last)			    \
		       : "r" (&(prev->thread)),		    \
			 "r" (&(next->thread)),		    \
                         "g" (prev)                         \
		       : "cc", "er0", "er1", "er2", "er3"); \
  (last) = _last; 					    \
}

#define __sti() asm volatile ("andc #0x7f,ccr")
#define __cli() asm volatile ("orc  #0x80,ccr")

#define __save_flags(x) \
       asm volatile ("stc ccr,%w0":"=r" (x))

#define __restore_flags(x) \
       asm volatile ("ldc %w0,ccr": :"r" (x))

#define	irqs_disabled()			\
({					\
	unsigned char flags;		\
	__save_flags(flags);	        \
	((flags & 0x80) == 0x80);	\
})

#define iret() __asm__ __volatile__ ("rte": : :"memory", "sp", "cc")

/* For spinlocks etc */
#define local_irq_disable()	__cli()
#define local_irq_enable()      __sti()
#define local_irq_save(x)	({ __save_flags(x); local_irq_disable(); })
#define local_irq_restore(x)	__restore_flags(x)
#define local_save_flags(x)     __save_flags(x)

/*
 * Force strict CPU ordering.
 * Not really required on H8...
 */
#define nop()  asm volatile ("nop"::)
#define mb()   asm volatile (""   : : :"memory")
#define rmb()  asm volatile (""   : : :"memory")
#define wmb()  asm volatile (""   : : :"memory")
#define set_mb(var, value) do { xchg(&var, value); } while (0)

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define smp_read_barrier_depends()	read_barrier_depends()
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do { } while(0)
#endif

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((volatile struct __xchg_dummy *)(x))

static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
  unsigned long tmp, flags;

  local_irq_save(flags);

  switch (size) {
  case 1:
    __asm__ __volatile__
    ("mov.b %2,%0\n\t"
     "mov.b %1,%2"
    : "=&r" (tmp) : "r" (x), "m" (*__xg(ptr)) : "memory");
    break;
  case 2:
    __asm__ __volatile__
    ("mov.w %2,%0\n\t"
     "mov.w %1,%2"
    : "=&r" (tmp) : "r" (x), "m" (*__xg(ptr)) : "memory");
    break;
  case 4:
    __asm__ __volatile__
    ("mov.l %2,%0\n\t"
     "mov.l %1,%2"
    : "=&r" (tmp) : "r" (x), "m" (*__xg(ptr)) : "memory");
    break;
  default:
    tmp = 0;	  
  }
  local_irq_restore(flags);
  return tmp;
}

#define HARD_RESET_NOW() ({		\
        local_irq_disable();		\
        asm("jmp @@0");			\
})

#include <asm-generic/cmpxchg-local.h>

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)				  	       \
	((__typeof__(*(ptr)))__cmpxchg_local_generic((ptr), (unsigned long)(o),\
			(unsigned long)(n), sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

#ifndef CONFIG_SMP
#include <asm-generic/cmpxchg.h>
#endif

#define arch_align_stack(x) (x)

void die(char *str, struct pt_regs *fp, unsigned long err);

#endif /* _H8300_SYSTEM_H */
