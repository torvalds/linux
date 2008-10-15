/*
 *  linux/arch/m32r/kernel/smp.c
 *
 *  M32R SMP support routines.
 *
 *  Copyright (c) 2001, 2002  Hitoshi Yamamoto
 *
 *  Taken from i386 version.
 *    (c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *    (c) 1998-99, 2000 Ingo Molnar <mingo@redhat.com>
 *
 *  This code is released under the GNU General Public License version 2 or
 *  later.
 */

#undef DEBUG_SMP

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/profile.h>
#include <linux/cpu.h>

#include <asm/cacheflush.h>
#include <asm/pgalloc.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/m32r.h>

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
/* Data structures and variables                                             */
/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/

/*
 * For flush_cache_all()
 */
static DEFINE_SPINLOCK(flushcache_lock);
static volatile unsigned long flushcache_cpumask = 0;

/*
 * For flush_tlb_others()
 */
static volatile cpumask_t flush_cpumask;
static struct mm_struct *flush_mm;
static struct vm_area_struct *flush_vma;
static volatile unsigned long flush_va;
static DEFINE_SPINLOCK(tlbstate_lock);
#define FLUSH_ALL 0xffffffff

DECLARE_PER_CPU(int, prof_multiplier);
DECLARE_PER_CPU(int, prof_old_multiplier);
DECLARE_PER_CPU(int, prof_counter);

extern spinlock_t ipi_lock[];

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
/* Function Prototypes                                                       */
/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/

void smp_send_reschedule(int);
void smp_reschedule_interrupt(void);

void smp_flush_cache_all(void);
void smp_flush_cache_all_interrupt(void);

void smp_flush_tlb_all(void);
static void flush_tlb_all_ipi(void *);

void smp_flush_tlb_mm(struct mm_struct *);
void smp_flush_tlb_range(struct vm_area_struct *, unsigned long, \
	unsigned long);
void smp_flush_tlb_page(struct vm_area_struct *, unsigned long);
static void flush_tlb_others(cpumask_t, struct mm_struct *,
	struct vm_area_struct *, unsigned long);
void smp_invalidate_interrupt(void);

void smp_send_stop(void);
static void stop_this_cpu(void *);

void smp_send_timer(void);
void smp_ipi_timer_interrupt(struct pt_regs *);
void smp_local_timer_interrupt(void);

static void send_IPI_allbutself(int, int);
static void send_IPI_mask(cpumask_t, int, int);
unsigned long send_IPI_mask_phys(cpumask_t, int, int);

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
/* Rescheduling request Routines                                             */
/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/

/*==========================================================================*
 * Name:         smp_send_reschedule
 *
 * Description:  This routine requests other CPU to execute rescheduling.
 *               1.Send 'RESCHEDULE_IPI' to other CPU.
 *                 Request other CPU to execute 'smp_reschedule_interrupt()'.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    cpu_id - Target CPU ID
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
void smp_send_reschedule(int cpu_id)
{
	WARN_ON(cpu_is_offline(cpu_id));
	send_IPI_mask(cpumask_of_cpu(cpu_id), RESCHEDULE_IPI, 1);
}

/*==========================================================================*
 * Name:         smp_reschedule_interrupt
 *
 * Description:  This routine executes on CPU which received
 *               'RESCHEDULE_IPI'.
 *               Rescheduling is processed at the exit of interrupt
 *               operation.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    NONE
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
void smp_reschedule_interrupt(void)
{
	/* nothing to do */
}

/*==========================================================================*
 * Name:         smp_flush_cache_all
 *
 * Description:  This routine sends a 'INVALIDATE_CACHE_IPI' to all other
 *               CPUs in the system.
 *
 * Born on Date: 2003-05-28
 *
 * Arguments:    NONE
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
void smp_flush_cache_all(void)
{
	cpumask_t cpumask;
	unsigned long *mask;

	preempt_disable();
	cpumask = cpu_online_map;
	cpu_clear(smp_processor_id(), cpumask);
	spin_lock(&flushcache_lock);
	mask=cpus_addr(cpumask);
	atomic_set_mask(*mask, (atomic_t *)&flushcache_cpumask);
	send_IPI_mask(cpumask, INVALIDATE_CACHE_IPI, 0);
	_flush_cache_copyback_all();
	while (flushcache_cpumask)
		mb();
	spin_unlock(&flushcache_lock);
	preempt_enable();
}

void smp_flush_cache_all_interrupt(void)
{
	_flush_cache_copyback_all();
	clear_bit(smp_processor_id(), &flushcache_cpumask);
}

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
/* TLB flush request Routines                                                */
/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/

/*==========================================================================*
 * Name:         smp_flush_tlb_all
 *
 * Description:  This routine flushes all processes TLBs.
 *               1.Request other CPU to execute 'flush_tlb_all_ipi()'.
 *               2.Execute 'do_flush_tlb_all_local()'.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    NONE
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
void smp_flush_tlb_all(void)
{
	unsigned long flags;

	preempt_disable();
	local_irq_save(flags);
	__flush_tlb_all();
	local_irq_restore(flags);
	smp_call_function(flush_tlb_all_ipi, NULL, 1);
	preempt_enable();
}

/*==========================================================================*
 * Name:         flush_tlb_all_ipi
 *
 * Description:  This routine flushes all local TLBs.
 *               1.Execute 'do_flush_tlb_all_local()'.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    *info - not used
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
static void flush_tlb_all_ipi(void *info)
{
	__flush_tlb_all();
}

/*==========================================================================*
 * Name:         smp_flush_tlb_mm
 *
 * Description:  This routine flushes the specified mm context TLB's.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    *mm - a pointer to the mm struct for flush TLB
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
void smp_flush_tlb_mm(struct mm_struct *mm)
{
	int cpu_id;
	cpumask_t cpu_mask;
	unsigned long *mmc;
	unsigned long flags;

	preempt_disable();
	cpu_id = smp_processor_id();
	mmc = &mm->context[cpu_id];
	cpu_mask = mm->cpu_vm_mask;
	cpu_clear(cpu_id, cpu_mask);

	if (*mmc != NO_CONTEXT) {
		local_irq_save(flags);
		*mmc = NO_CONTEXT;
		if (mm == current->mm)
			activate_context(mm);
		else
			cpu_clear(cpu_id, mm->cpu_vm_mask);
		local_irq_restore(flags);
	}
	if (!cpus_empty(cpu_mask))
		flush_tlb_others(cpu_mask, mm, NULL, FLUSH_ALL);

	preempt_enable();
}

/*==========================================================================*
 * Name:         smp_flush_tlb_range
 *
 * Description:  This routine flushes a range of pages.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    *mm - a pointer to the mm struct for flush TLB
 *               start - not used
 *               end - not used
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
void smp_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
	unsigned long end)
{
	smp_flush_tlb_mm(vma->vm_mm);
}

/*==========================================================================*
 * Name:         smp_flush_tlb_page
 *
 * Description:  This routine flushes one page.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    *vma - a pointer to the vma struct include va
 *               va - virtual address for flush TLB
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
void smp_flush_tlb_page(struct vm_area_struct *vma, unsigned long va)
{
	struct mm_struct *mm = vma->vm_mm;
	int cpu_id;
	cpumask_t cpu_mask;
	unsigned long *mmc;
	unsigned long flags;

	preempt_disable();
	cpu_id = smp_processor_id();
	mmc = &mm->context[cpu_id];
	cpu_mask = mm->cpu_vm_mask;
	cpu_clear(cpu_id, cpu_mask);

#ifdef DEBUG_SMP
	if (!mm)
		BUG();
#endif

	if (*mmc != NO_CONTEXT) {
		local_irq_save(flags);
		va &= PAGE_MASK;
		va |= (*mmc & MMU_CONTEXT_ASID_MASK);
		__flush_tlb_page(va);
		local_irq_restore(flags);
	}
	if (!cpus_empty(cpu_mask))
		flush_tlb_others(cpu_mask, mm, vma, va);

	preempt_enable();
}

/*==========================================================================*
 * Name:         flush_tlb_others
 *
 * Description:  This routine requests other CPU to execute flush TLB.
 *               1.Setup parameters.
 *               2.Send 'INVALIDATE_TLB_IPI' to other CPU.
 *                 Request other CPU to execute 'smp_invalidate_interrupt()'.
 *               3.Wait for other CPUs operation finished.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    cpumask - bitmap of target CPUs
 *               *mm -  a pointer to the mm struct for flush TLB
 *               *vma -  a pointer to the vma struct include va
 *               va - virtual address for flush TLB
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
static void flush_tlb_others(cpumask_t cpumask, struct mm_struct *mm,
	struct vm_area_struct *vma, unsigned long va)
{
	unsigned long *mask;
#ifdef DEBUG_SMP
	unsigned long flags;
	__save_flags(flags);
	if (!(flags & 0x0040))	/* Interrupt Disable NONONO */
		BUG();
#endif /* DEBUG_SMP */

	/*
	 * A couple of (to be removed) sanity checks:
	 *
	 * - we do not send IPIs to not-yet booted CPUs.
	 * - current CPU must not be in mask
	 * - mask must exist :)
	 */
	BUG_ON(cpus_empty(cpumask));

	BUG_ON(cpu_isset(smp_processor_id(), cpumask));
	BUG_ON(!mm);

	/* If a CPU which we ran on has gone down, OK. */
	cpus_and(cpumask, cpumask, cpu_online_map);
	if (cpus_empty(cpumask))
		return;

	/*
	 * i'm not happy about this global shared spinlock in the
	 * MM hot path, but we'll see how contended it is.
	 * Temporarily this turns IRQs off, so that lockups are
	 * detected by the NMI watchdog.
	 */
	spin_lock(&tlbstate_lock);

	flush_mm = mm;
	flush_vma = vma;
	flush_va = va;
	mask=cpus_addr(cpumask);
	atomic_set_mask(*mask, (atomic_t *)&flush_cpumask);

	/*
	 * We have to send the IPI only to
	 * CPUs affected.
	 */
	send_IPI_mask(cpumask, INVALIDATE_TLB_IPI, 0);

	while (!cpus_empty(flush_cpumask)) {
		/* nothing. lockup detection does not belong here */
		mb();
	}

	flush_mm = NULL;
	flush_vma = NULL;
	flush_va = 0;
	spin_unlock(&tlbstate_lock);
}

/*==========================================================================*
 * Name:         smp_invalidate_interrupt
 *
 * Description:  This routine executes on CPU which received
 *               'INVALIDATE_TLB_IPI'.
 *               1.Flush local TLB.
 *               2.Report flush TLB process was finished.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    NONE
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
void smp_invalidate_interrupt(void)
{
	int cpu_id = smp_processor_id();
	unsigned long *mmc = &flush_mm->context[cpu_id];

	if (!cpu_isset(cpu_id, flush_cpumask))
		return;

	if (flush_va == FLUSH_ALL) {
		*mmc = NO_CONTEXT;
		if (flush_mm == current->active_mm)
			activate_context(flush_mm);
		else
			cpu_clear(cpu_id, flush_mm->cpu_vm_mask);
	} else {
		unsigned long va = flush_va;

		if (*mmc != NO_CONTEXT) {
			va &= PAGE_MASK;
			va |= (*mmc & MMU_CONTEXT_ASID_MASK);
			__flush_tlb_page(va);
		}
	}
	cpu_clear(cpu_id, flush_cpumask);
}

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
/* Stop CPU request Routines                                                 */
/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/

/*==========================================================================*
 * Name:         smp_send_stop
 *
 * Description:  This routine requests stop all CPUs.
 *               1.Request other CPU to execute 'stop_this_cpu()'.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    NONE
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 0);
}

/*==========================================================================*
 * Name:         stop_this_cpu
 *
 * Description:  This routine halt CPU.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    NONE
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
static void stop_this_cpu(void *dummy)
{
	int cpu_id = smp_processor_id();

	/*
	 * Remove this CPU:
	 */
	cpu_clear(cpu_id, cpu_online_map);

	/*
	 * PSW IE = 1;
	 * IMASK = 0;
	 * goto SLEEP
	 */
	local_irq_disable();
	outl(0, M32R_ICU_IMASK_PORTL);
	inl(M32R_ICU_IMASK_PORTL);	/* dummy read */
	local_irq_enable();

	for ( ; ; );
}

void arch_send_call_function_ipi(cpumask_t mask)
{
	send_IPI_mask(mask, CALL_FUNCTION_IPI, 0);
}

void arch_send_call_function_single_ipi(int cpu)
{
	send_IPI_mask(cpumask_of_cpu(cpu), CALL_FUNC_SINGLE_IPI, 0);
}

/*==========================================================================*
 * Name:         smp_call_function_interrupt
 *
 * Description:  This routine executes on CPU which received
 *               'CALL_FUNCTION_IPI'.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    NONE
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
void smp_call_function_interrupt(void)
{
	irq_enter();
	generic_smp_call_function_interrupt();
	irq_exit();
}

void smp_call_function_single_interrupt(void)
{
	irq_enter();
	generic_smp_call_function_single_interrupt();
	irq_exit();
}

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
/* Timer Routines                                                            */
/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/

/*==========================================================================*
 * Name:         smp_send_timer
 *
 * Description:  This routine sends a 'LOCAL_TIMER_IPI' to all other CPUs
 *               in the system.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    NONE
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
void smp_send_timer(void)
{
	send_IPI_allbutself(LOCAL_TIMER_IPI, 1);
}

/*==========================================================================*
 * Name:         smp_send_timer
 *
 * Description:  This routine executes on CPU which received
 *               'LOCAL_TIMER_IPI'.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    *regs - a pointer to the saved regster info
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
void smp_ipi_timer_interrupt(struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	old_regs = set_irq_regs(regs);
	irq_enter();
	smp_local_timer_interrupt();
	irq_exit();
	set_irq_regs(old_regs);
}

/*==========================================================================*
 * Name:         smp_local_timer_interrupt
 *
 * Description:  Local timer interrupt handler. It does both profiling and
 *               process statistics/rescheduling.
 *               We do profiling in every local tick, statistics/rescheduling
 *               happen only every 'profiling multiplier' ticks. The default
 *               multiplier is 1 and it can be changed by writing the new
 *               multiplier value into /proc/profile.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    *regs - a pointer to the saved regster info
 *
 * Returns:      void (cannot fail)
 *
 * Original:     arch/i386/kernel/apic.c
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 * 2003-06-24 hy  use per_cpu structure.
 *==========================================================================*/
void smp_local_timer_interrupt(void)
{
	int user = user_mode(get_irq_regs());
	int cpu_id = smp_processor_id();

	/*
	 * The profiling function is SMP safe. (nothing can mess
	 * around with "current", and the profiling counters are
	 * updated with atomic operations). This is especially
	 * useful with a profiling multiplier != 1
	 */

	profile_tick(CPU_PROFILING);

	if (--per_cpu(prof_counter, cpu_id) <= 0) {
		/*
		 * The multiplier may have changed since the last time we got
		 * to this point as a result of the user writing to
		 * /proc/profile. In this case we need to adjust the APIC
		 * timer accordingly.
		 *
		 * Interrupts are already masked off at this point.
		 */
		per_cpu(prof_counter, cpu_id)
			= per_cpu(prof_multiplier, cpu_id);
		if (per_cpu(prof_counter, cpu_id)
			!= per_cpu(prof_old_multiplier, cpu_id))
		{
			per_cpu(prof_old_multiplier, cpu_id)
				= per_cpu(prof_counter, cpu_id);
		}

		update_process_times(user);
	}
}

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
/* Send IPI Routines                                                         */
/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/

/*==========================================================================*
 * Name:         send_IPI_allbutself
 *
 * Description:  This routine sends a IPI to all other CPUs in the system.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    ipi_num - Number of IPI
 *               try -  0 : Send IPI certainly.
 *                     !0 : The following IPI is not sent when Target CPU
 *                          has not received the before IPI.
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
static void send_IPI_allbutself(int ipi_num, int try)
{
	cpumask_t cpumask;

	cpumask = cpu_online_map;
	cpu_clear(smp_processor_id(), cpumask);

	send_IPI_mask(cpumask, ipi_num, try);
}

/*==========================================================================*
 * Name:         send_IPI_mask
 *
 * Description:  This routine sends a IPI to CPUs in the system.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    cpu_mask - Bitmap of target CPUs logical ID
 *               ipi_num - Number of IPI
 *               try -  0 : Send IPI certainly.
 *                     !0 : The following IPI is not sent when Target CPU
 *                          has not received the before IPI.
 *
 * Returns:      void (cannot fail)
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
static void send_IPI_mask(cpumask_t cpumask, int ipi_num, int try)
{
	cpumask_t physid_mask, tmp;
	int cpu_id, phys_id;
	int num_cpus = num_online_cpus();

	if (num_cpus <= 1)	/* NO MP */
		return;

	cpus_and(tmp, cpumask, cpu_online_map);
	BUG_ON(!cpus_equal(cpumask, tmp));

	physid_mask = CPU_MASK_NONE;
	for_each_cpu_mask(cpu_id, cpumask){
		if ((phys_id = cpu_to_physid(cpu_id)) != -1)
			cpu_set(phys_id, physid_mask);
	}

	send_IPI_mask_phys(physid_mask, ipi_num, try);
}

/*==========================================================================*
 * Name:         send_IPI_mask_phys
 *
 * Description:  This routine sends a IPI to other CPUs in the system.
 *
 * Born on Date: 2002.02.05
 *
 * Arguments:    cpu_mask - Bitmap of target CPUs physical ID
 *               ipi_num - Number of IPI
 *               try -  0 : Send IPI certainly.
 *                     !0 : The following IPI is not sent when Target CPU
 *                          has not received the before IPI.
 *
 * Returns:      IPICRi regster value.
 *
 * Modification log:
 * Date       Who Description
 * ---------- --- --------------------------------------------------------
 *
 *==========================================================================*/
unsigned long send_IPI_mask_phys(cpumask_t physid_mask, int ipi_num,
	int try)
{
	spinlock_t *ipilock;
	volatile unsigned long *ipicr_addr;
	unsigned long ipicr_val;
	unsigned long my_physid_mask;
	unsigned long mask = cpus_addr(physid_mask)[0];


	if (mask & ~physids_coerce(phys_cpu_present_map))
		BUG();
	if (ipi_num >= NR_IPIS)
		BUG();

	mask <<= IPI_SHIFT;
	ipilock = &ipi_lock[ipi_num];
	ipicr_addr = (volatile unsigned long *)(M32R_ICU_IPICR_ADDR
		+ (ipi_num << 2));
	my_physid_mask = ~(1 << smp_processor_id());

	/*
	 * lock ipi_lock[i]
	 * check IPICRi == 0
	 * write IPICRi (send IPIi)
	 * unlock ipi_lock[i]
	 */
	spin_lock(ipilock);
	__asm__ __volatile__ (
		";; CHECK IPICRi == 0		\n\t"
		".fillinsn			\n"
		"1:				\n\t"
		"ld	%0, @%1			\n\t"
		"and	%0, %4			\n\t"
		"beqz	%0, 2f			\n\t"
		"bnez	%3, 3f			\n\t"
		"bra	1b			\n\t"
		";; WRITE IPICRi (send IPIi)	\n\t"
		".fillinsn			\n"
		"2:				\n\t"
		"st	%2, @%1			\n\t"
		".fillinsn			\n"
		"3:				\n\t"
		: "=&r"(ipicr_val)
		: "r"(ipicr_addr), "r"(mask), "r"(try), "r"(my_physid_mask)
		: "memory"
	);
	spin_unlock(ipilock);

	return ipicr_val;
}
