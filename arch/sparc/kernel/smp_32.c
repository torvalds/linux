/* smp.c: Sparc SMP support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 2004 Keith M Wesolowski (wesolows@foobazco.org)
 */

#include <asm/head.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/cache.h>
#include <linux/delay.h>

#include <asm/ptrace.h>
#include <linux/atomic.h>

#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/cpudata.h>
#include <asm/leon.h>

#include "irq.h"

volatile unsigned long cpu_callin_map[NR_CPUS] __cpuinitdata = {0,};

cpumask_t smp_commenced_mask = CPU_MASK_NONE;

/* The only guaranteed locking primitive available on all Sparc
 * processors is 'ldstub [%reg + immediate], %dest_reg' which atomically
 * places the current byte at the effective address into dest_reg and
 * places 0xff there afterwards.  Pretty lame locking primitive
 * compared to the Alpha and the Intel no?  Most Sparcs have 'swap'
 * instruction which is much better...
 */

void __cpuinit smp_store_cpu_info(int id)
{
	int cpu_node;
	int mid;

	cpu_data(id).udelay_val = loops_per_jiffy;

	cpu_find_by_mid(id, &cpu_node);
	cpu_data(id).clock_tick = prom_getintdefault(cpu_node,
						     "clock-frequency", 0);
	cpu_data(id).prom_node = cpu_node;
	mid = cpu_get_hwmid(cpu_node);

	if (mid < 0) {
		printk(KERN_NOTICE "No MID found for CPU%d at node 0x%08d", id, cpu_node);
		mid = 0;
	}
	cpu_data(id).mid = mid;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	extern void smp4m_smp_done(void);
	extern void smp4d_smp_done(void);
	unsigned long bogosum = 0;
	int cpu, num = 0;

	for_each_online_cpu(cpu) {
		num++;
		bogosum += cpu_data(cpu).udelay_val;
	}

	printk("Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
		num, bogosum/(500000/HZ),
		(bogosum/(5000/HZ))%100);

	switch(sparc_cpu_model) {
	case sun4:
		printk("SUN4\n");
		BUG();
		break;
	case sun4c:
		printk("SUN4C\n");
		BUG();
		break;
	case sun4m:
		smp4m_smp_done();
		break;
	case sun4d:
		smp4d_smp_done();
		break;
	case sparc_leon:
		leon_smp_done();
		break;
	case sun4e:
		printk("SUN4E\n");
		BUG();
		break;
	case sun4u:
		printk("SUN4U\n");
		BUG();
		break;
	default:
		printk("UNKNOWN!\n");
		BUG();
		break;
	}
}

void cpu_panic(void)
{
	printk("CPU[%d]: Returns from cpu_idle!\n", smp_processor_id());
	panic("SMP bolixed\n");
}

struct linux_prom_registers smp_penguin_ctable __cpuinitdata = { 0 };

void smp_send_reschedule(int cpu)
{
	/*
	 * CPU model dependent way of implementing IPI generation targeting
	 * a single CPU. The trap handler needs only to do trap entry/return
	 * to call schedule.
	 */
	BTFIXUP_CALL(smp_ipi_resched)(cpu);
}

void smp_send_stop(void)
{
}

void arch_send_call_function_single_ipi(int cpu)
{
	/* trigger one IPI single call on one CPU */
	BTFIXUP_CALL(smp_ipi_single)(cpu);
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	int cpu;

	/* trigger IPI mask call on each CPU */
	for_each_cpu(cpu, mask)
		BTFIXUP_CALL(smp_ipi_mask_one)(cpu);
}

void smp_resched_interrupt(void)
{
	irq_enter();
	scheduler_ipi();
	local_cpu_data().irq_resched_count++;
	irq_exit();
	/* re-schedule routine called by interrupt return code. */
}

void smp_call_function_single_interrupt(void)
{
	irq_enter();
	generic_smp_call_function_single_interrupt();
	local_cpu_data().irq_call_count++;
	irq_exit();
}

void smp_call_function_interrupt(void)
{
	irq_enter();
	generic_smp_call_function_interrupt();
	local_cpu_data().irq_call_count++;
	irq_exit();
}

void smp_flush_cache_all(void)
{
	xc0((smpfunc_t) BTFIXUP_CALL(local_flush_cache_all));
	local_flush_cache_all();
}

void smp_flush_tlb_all(void)
{
	xc0((smpfunc_t) BTFIXUP_CALL(local_flush_tlb_all));
	local_flush_tlb_all();
}

void smp_flush_cache_mm(struct mm_struct *mm)
{
	if(mm->context != NO_CONTEXT) {
		cpumask_t cpu_mask;
		cpumask_copy(&cpu_mask, mm_cpumask(mm));
		cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
		if (!cpumask_empty(&cpu_mask))
			xc1((smpfunc_t) BTFIXUP_CALL(local_flush_cache_mm), (unsigned long) mm);
		local_flush_cache_mm(mm);
	}
}

void smp_flush_tlb_mm(struct mm_struct *mm)
{
	if(mm->context != NO_CONTEXT) {
		cpumask_t cpu_mask;
		cpumask_copy(&cpu_mask, mm_cpumask(mm));
		cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
		if (!cpumask_empty(&cpu_mask)) {
			xc1((smpfunc_t) BTFIXUP_CALL(local_flush_tlb_mm), (unsigned long) mm);
			if(atomic_read(&mm->mm_users) == 1 && current->active_mm == mm)
				cpumask_copy(mm_cpumask(mm),
					     cpumask_of(smp_processor_id()));
		}
		local_flush_tlb_mm(mm);
	}
}

void smp_flush_cache_range(struct vm_area_struct *vma, unsigned long start,
			   unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	if (mm->context != NO_CONTEXT) {
		cpumask_t cpu_mask;
		cpumask_copy(&cpu_mask, mm_cpumask(mm));
		cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
		if (!cpumask_empty(&cpu_mask))
			xc3((smpfunc_t) BTFIXUP_CALL(local_flush_cache_range), (unsigned long) vma, start, end);
		local_flush_cache_range(vma, start, end);
	}
}

void smp_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			 unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	if (mm->context != NO_CONTEXT) {
		cpumask_t cpu_mask;
		cpumask_copy(&cpu_mask, mm_cpumask(mm));
		cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
		if (!cpumask_empty(&cpu_mask))
			xc3((smpfunc_t) BTFIXUP_CALL(local_flush_tlb_range), (unsigned long) vma, start, end);
		local_flush_tlb_range(vma, start, end);
	}
}

void smp_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	if(mm->context != NO_CONTEXT) {
		cpumask_t cpu_mask;
		cpumask_copy(&cpu_mask, mm_cpumask(mm));
		cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
		if (!cpumask_empty(&cpu_mask))
			xc2((smpfunc_t) BTFIXUP_CALL(local_flush_cache_page), (unsigned long) vma, page);
		local_flush_cache_page(vma, page);
	}
}

void smp_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	if(mm->context != NO_CONTEXT) {
		cpumask_t cpu_mask;
		cpumask_copy(&cpu_mask, mm_cpumask(mm));
		cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
		if (!cpumask_empty(&cpu_mask))
			xc2((smpfunc_t) BTFIXUP_CALL(local_flush_tlb_page), (unsigned long) vma, page);
		local_flush_tlb_page(vma, page);
	}
}

void smp_flush_page_to_ram(unsigned long page)
{
	/* Current theory is that those who call this are the one's
	 * who have just dirtied their cache with the pages contents
	 * in kernel space, therefore we only run this on local cpu.
	 *
	 * XXX This experiment failed, research further... -DaveM
	 */
#if 1
	xc1((smpfunc_t) BTFIXUP_CALL(local_flush_page_to_ram), page);
#endif
	local_flush_page_to_ram(page);
}

void smp_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
	cpumask_t cpu_mask;
	cpumask_copy(&cpu_mask, mm_cpumask(mm));
	cpumask_clear_cpu(smp_processor_id(), &cpu_mask);
	if (!cpumask_empty(&cpu_mask))
		xc2((smpfunc_t) BTFIXUP_CALL(local_flush_sig_insns), (unsigned long) mm, insn_addr);
	local_flush_sig_insns(mm, insn_addr);
}

extern unsigned int lvl14_resolution;

/* /proc/profile writes can call this, don't __init it please. */
static DEFINE_SPINLOCK(prof_setup_lock);

int setup_profiling_timer(unsigned int multiplier)
{
	int i;
	unsigned long flags;

	/* Prevent level14 ticker IRQ flooding. */
	if((!multiplier) || (lvl14_resolution / multiplier) < 500)
		return -EINVAL;

	spin_lock_irqsave(&prof_setup_lock, flags);
	for_each_possible_cpu(i) {
		load_profile_irq(i, lvl14_resolution / multiplier);
		prof_multiplier(i) = multiplier;
	}
	spin_unlock_irqrestore(&prof_setup_lock, flags);

	return 0;
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	extern void __init smp4m_boot_cpus(void);
	extern void __init smp4d_boot_cpus(void);
	int i, cpuid, extra;

	printk("Entering SMP Mode...\n");

	extra = 0;
	for (i = 0; !cpu_find_by_instance(i, NULL, &cpuid); i++) {
		if (cpuid >= NR_CPUS)
			extra++;
	}
	/* i = number of cpus */
	if (extra && max_cpus > i - extra)
		printk("Warning: NR_CPUS is too low to start all cpus\n");

	smp_store_cpu_info(boot_cpu_id);

	switch(sparc_cpu_model) {
	case sun4:
		printk("SUN4\n");
		BUG();
		break;
	case sun4c:
		printk("SUN4C\n");
		BUG();
		break;
	case sun4m:
		smp4m_boot_cpus();
		break;
	case sun4d:
		smp4d_boot_cpus();
		break;
	case sparc_leon:
		leon_boot_cpus();
		break;
	case sun4e:
		printk("SUN4E\n");
		BUG();
		break;
	case sun4u:
		printk("SUN4U\n");
		BUG();
		break;
	default:
		printk("UNKNOWN!\n");
		BUG();
		break;
	}
}

/* Set this up early so that things like the scheduler can init
 * properly.  We use the same cpu mask for both the present and
 * possible cpu map.
 */
void __init smp_setup_cpu_possible_map(void)
{
	int instance, mid;

	instance = 0;
	while (!cpu_find_by_instance(instance, NULL, &mid)) {
		if (mid < NR_CPUS) {
			set_cpu_possible(mid, true);
			set_cpu_present(mid, true);
		}
		instance++;
	}
}

void __init smp_prepare_boot_cpu(void)
{
	int cpuid = hard_smp_processor_id();

	if (cpuid >= NR_CPUS) {
		prom_printf("Serious problem, boot cpu id >= NR_CPUS\n");
		prom_halt();
	}
	if (cpuid != 0)
		printk("boot cpu id != 0, this could work but is untested\n");

	current_thread_info()->cpu = cpuid;
	set_cpu_online(cpuid, true);
	set_cpu_possible(cpuid, true);
}

int __cpuinit __cpu_up(unsigned int cpu)
{
	extern int __cpuinit smp4m_boot_one_cpu(int);
	extern int __cpuinit smp4d_boot_one_cpu(int);
	int ret=0;

	switch(sparc_cpu_model) {
	case sun4:
		printk("SUN4\n");
		BUG();
		break;
	case sun4c:
		printk("SUN4C\n");
		BUG();
		break;
	case sun4m:
		ret = smp4m_boot_one_cpu(cpu);
		break;
	case sun4d:
		ret = smp4d_boot_one_cpu(cpu);
		break;
	case sparc_leon:
		ret = leon_boot_one_cpu(cpu);
		break;
	case sun4e:
		printk("SUN4E\n");
		BUG();
		break;
	case sun4u:
		printk("SUN4U\n");
		BUG();
		break;
	default:
		printk("UNKNOWN!\n");
		BUG();
		break;
	}

	if (!ret) {
		cpumask_set_cpu(cpu, &smp_commenced_mask);
		while (!cpu_online(cpu))
			mb();
	}
	return ret;
}

void smp_bogo(struct seq_file *m)
{
	int i;
	
	for_each_online_cpu(i) {
		seq_printf(m,
			   "Cpu%dBogo\t: %lu.%02lu\n",
			   i,
			   cpu_data(i).udelay_val/(500000/HZ),
			   (cpu_data(i).udelay_val/(5000/HZ))%100);
	}
}

void smp_info(struct seq_file *m)
{
	int i;

	seq_printf(m, "State:\n");
	for_each_online_cpu(i)
		seq_printf(m, "CPU%d\t\t: online\n", i);
}
