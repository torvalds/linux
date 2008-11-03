#ifndef __ASM_CRIS_SYSTEM_H
#define __ASM_CRIS_SYSTEM_H

#include <arch/system.h>

/* the switch_to macro calls resume, an asm function in entry.S which does the actual
 * task switching.
 */

extern struct task_struct *resume(struct task_struct *prev, struct task_struct *next, int);
#define switch_to(prev,next,last) last = resume(prev,next, \
					 (int)&((struct task_struct *)0)->thread)

#define barrier() __asm__ __volatile__("": : :"memory")
#define mb() barrier()
#define rmb() mb()
#define wmb() mb()
#define read_barrier_depends() do { } while(0)
#define set_mb(var, value)  do { var = value; mb(); } while (0)

#ifdef CONFIG_SMP
#define smp_mb()        mb()
#define smp_rmb()       rmb()
#define smp_wmb()       wmb()
#define smp_read_barrier_depends()     read_barrier_depends()
#else
#define smp_mb()        barrier()
#define smp_rmb()       barrier()
#define smp_wmb()       barrier()
#define smp_read_barrier_depends()     do { } while(0)
#endif

#define iret()

/*
 * disable hlt during certain critical i/o operations
 */
#define HAVE_DISABLE_HLT
void disable_hlt(void);
void enable_hlt(void);

static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
  /* since Etrax doesn't have any atomic xchg instructions, we need to disable
     irq's (if enabled) and do it with move.d's */
  unsigned long flags,temp;
  local_irq_save(flags); /* save flags, including irq enable bit and shut off irqs */
  switch (size) {
  case 1:
    *((unsigned char *)&temp) = x;
    x = *(unsigned char *)ptr;
    *(unsigned char *)ptr = *((unsigned char *)&temp);
    break;
  case 2:
    *((unsigned short *)&temp) = x;
    x = *(unsigned short *)ptr;
    *(unsigned short *)ptr = *((unsigned short *)&temp);
    break;
  case 4:
    temp = x;
    x = *(unsigned long *)ptr;
    *(unsigned long *)ptr = temp;
    break;
  }
  local_irq_restore(flags); /* restore irq enable bit */
  return x;
}

#include <asm-generic/cmpxchg-local.h>

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)				 	       \
	((__typeof__(*(ptr)))__cmpxchg_local_generic((ptr), (unsigned long)(o),\
			(unsigned long)(n), sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

#ifndef CONFIG_SMP
#include <asm-generic/cmpxchg.h>
#endif

#define arch_align_stack(x) (x)

void default_idle(void);

#endif
