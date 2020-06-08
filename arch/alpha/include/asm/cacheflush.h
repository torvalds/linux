/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_CACHEFLUSH_H
#define _ALPHA_CACHEFLUSH_H

#include <linux/mm.h>

/* Note that the following two definitions are _highly_ dependent
   on the contexts in which they are used in the kernel.  I personally
   think it is criminal how loosely defined these macros are.  */

/* We need to flush the kernel's icache after loading modules.  The
   only other use of this macro is in load_aout_interp which is not
   used on Alpha. 

   Note that this definition should *not* be used for userspace
   icache flushing.  While functional, it is _way_ overkill.  The
   icache is tagged with ASNs and it suffices to allocate a new ASN
   for the process.  */
#ifndef CONFIG_SMP
#define flush_icache_range(start, end)		imb()
#else
#define flush_icache_range(start, end)		smp_imb()
extern void smp_imb(void);
#endif

/* We need to flush the userspace icache after setting breakpoints in
   ptrace.

   Instead of indiscriminately using imb, take advantage of the fact
   that icache entries are tagged with the ASN and load a new mm context.  */
/* ??? Ought to use this in arch/alpha/kernel/signal.c too.  */

#ifndef CONFIG_SMP
#include <linux/sched.h>

extern void __load_new_mm_context(struct mm_struct *);
static inline void
flush_icache_user_page(struct vm_area_struct *vma, struct page *page,
			unsigned long addr, int len)
{
	if (vma->vm_flags & VM_EXEC) {
		struct mm_struct *mm = vma->vm_mm;
		if (current->active_mm == mm)
			__load_new_mm_context(mm);
		else
			mm->context[smp_processor_id()] = 0;
	}
}
#define flush_icache_user_page flush_icache_user_page
#else /* CONFIG_SMP */
extern void flush_icache_user_page(struct vm_area_struct *vma,
		struct page *page, unsigned long addr, int len);
#define flush_icache_user_page flush_icache_user_page
#endif /* CONFIG_SMP */

/* This is used only in __do_fault and do_swap_page.  */
#define flush_icache_page(vma, page) \
	flush_icache_user_page((vma), (page), 0, 0)

#include <asm-generic/cacheflush.h>

#endif /* _ALPHA_CACHEFLUSH_H */
