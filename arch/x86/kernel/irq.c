// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common interrupt code for 32 and 64 bit
 */
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/of.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/ftrace.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/irq.h>

#include <asm/irq_stack.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/irq.h>
#include <asm/mce.h>
#include <asm/hw_irq.h>
#include <asm/desc.h>
#include <asm/traps.h>
#include <asm/thermal.h>
#include <asm/posted_intr.h>
#include <asm/irq_remapping.h>

#if defined(CONFIG_X86_LOCAL_APIC) || defined(CONFIG_X86_THERMAL_VECTOR)
#define CREATE_TRACE_POINTS
#include <asm/trace/irq_vectors.h>
#endif

DEFINE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);
EXPORT_PER_CPU_SYMBOL(irq_stat);

DEFINE_PER_CPU_CACHE_HOT(u16, __softirq_pending);
EXPORT_PER_CPU_SYMBOL(__softirq_pending);

DEFINE_PER_CPU_CACHE_HOT(struct irq_stack *, hardirq_stack_ptr);

atomic_t irq_err_count;

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	if (printk_ratelimit())
		pr_err("unexpected IRQ trap at vector %02x\n", irq);

	/*
	 * Currently unexpected vectors happen only on SMP and APIC.
	 * We _must_ ack these because every local APIC has only N
	 * irq slots per priority level, and a 'hanging, unacked' IRQ
	 * holds up an irq slot - in excessive cases (when multiple
	 * unexpected vectors occur) that might lock up the APIC
	 * completely.
	 * But only ack when the APIC is enabled -AK
	 */
	apic_eoi();
}

#define irq_stats(x)		(&per_cpu(irq_stat, x))
/*
 * /proc/interrupts printing for arch specific interrupts
 */
int arch_show_interrupts(struct seq_file *p, int prec)
{
	int j;

	seq_printf(p, "%*s: ", prec, "NMI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->__nmi_count);
	seq_puts(p, "  Non-maskable interrupts\n");
#ifdef CONFIG_X86_LOCAL_APIC
	seq_printf(p, "%*s: ", prec, "LOC");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->apic_timer_irqs);
	seq_puts(p, "  Local timer interrupts\n");

	seq_printf(p, "%*s: ", prec, "SPU");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_spurious_count);
	seq_puts(p, "  Spurious interrupts\n");
	seq_printf(p, "%*s: ", prec, "PMI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->apic_perf_irqs);
	seq_puts(p, "  Performance monitoring interrupts\n");
	seq_printf(p, "%*s: ", prec, "IWI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->apic_irq_work_irqs);
	seq_puts(p, "  IRQ work interrupts\n");
	seq_printf(p, "%*s: ", prec, "RTR");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->icr_read_retry_count);
	seq_puts(p, "  APIC ICR read retries\n");
	if (x86_platform_ipi_callback) {
		seq_printf(p, "%*s: ", prec, "PLT");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", irq_stats(j)->x86_platform_ipis);
		seq_puts(p, "  Platform interrupts\n");
	}
#endif
#ifdef CONFIG_SMP
	seq_printf(p, "%*s: ", prec, "RES");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_resched_count);
	seq_puts(p, "  Rescheduling interrupts\n");
	seq_printf(p, "%*s: ", prec, "CAL");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_call_count);
	seq_puts(p, "  Function call interrupts\n");
	seq_printf(p, "%*s: ", prec, "TLB");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_tlb_count);
	seq_puts(p, "  TLB shootdowns\n");
#endif
#ifdef CONFIG_X86_THERMAL_VECTOR
	seq_printf(p, "%*s: ", prec, "TRM");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_thermal_count);
	seq_puts(p, "  Thermal event interrupts\n");
#endif
#ifdef CONFIG_X86_MCE_THRESHOLD
	seq_printf(p, "%*s: ", prec, "THR");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_threshold_count);
	seq_puts(p, "  Threshold APIC interrupts\n");
#endif
#ifdef CONFIG_X86_MCE_AMD
	seq_printf(p, "%*s: ", prec, "DFR");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_deferred_error_count);
	seq_puts(p, "  Deferred Error APIC interrupts\n");
#endif
#ifdef CONFIG_X86_MCE
	seq_printf(p, "%*s: ", prec, "MCE");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(mce_exception_count, j));
	seq_puts(p, "  Machine check exceptions\n");
	seq_printf(p, "%*s: ", prec, "MCP");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(mce_poll_count, j));
	seq_puts(p, "  Machine check polls\n");
#endif
#ifdef CONFIG_X86_HV_CALLBACK_VECTOR
	if (test_bit(HYPERVISOR_CALLBACK_VECTOR, system_vectors)) {
		seq_printf(p, "%*s: ", prec, "HYP");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ",
				   irq_stats(j)->irq_hv_callback_count);
		seq_puts(p, "  Hypervisor callback interrupts\n");
	}
#endif
#if IS_ENABLED(CONFIG_HYPERV)
	if (test_bit(HYPERV_REENLIGHTENMENT_VECTOR, system_vectors)) {
		seq_printf(p, "%*s: ", prec, "HRE");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ",
				   irq_stats(j)->irq_hv_reenlightenment_count);
		seq_puts(p, "  Hyper-V reenlightenment interrupts\n");
	}
	if (test_bit(HYPERV_STIMER0_VECTOR, system_vectors)) {
		seq_printf(p, "%*s: ", prec, "HVS");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ",
				   irq_stats(j)->hyperv_stimer0_count);
		seq_puts(p, "  Hyper-V stimer0 interrupts\n");
	}
#endif
	seq_printf(p, "%*s: %10u\n", prec, "ERR", atomic_read(&irq_err_count));
#if defined(CONFIG_X86_IO_APIC)
	seq_printf(p, "%*s: %10u\n", prec, "MIS", atomic_read(&irq_mis_count));
#endif
#if IS_ENABLED(CONFIG_KVM)
	seq_printf(p, "%*s: ", prec, "PIN");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->kvm_posted_intr_ipis);
	seq_puts(p, "  Posted-interrupt notification event\n");

	seq_printf(p, "%*s: ", prec, "NPI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ",
			   irq_stats(j)->kvm_posted_intr_nested_ipis);
	seq_puts(p, "  Nested posted-interrupt event\n");

	seq_printf(p, "%*s: ", prec, "PIW");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ",
			   irq_stats(j)->kvm_posted_intr_wakeup_ipis);
	seq_puts(p, "  Posted-interrupt wakeup event\n");
#endif
#ifdef CONFIG_X86_POSTED_MSI
	seq_printf(p, "%*s: ", prec, "PMN");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ",
			   irq_stats(j)->posted_msi_notification_count);
	seq_puts(p, "  Posted MSI notification event\n");
#endif
	return 0;
}

/*
 * /proc/stat helpers
 */
u64 arch_irq_stat_cpu(unsigned int cpu)
{
	u64 sum = irq_stats(cpu)->__nmi_count;

#ifdef CONFIG_X86_LOCAL_APIC
	sum += irq_stats(cpu)->apic_timer_irqs;
	sum += irq_stats(cpu)->irq_spurious_count;
	sum += irq_stats(cpu)->apic_perf_irqs;
	sum += irq_stats(cpu)->apic_irq_work_irqs;
	sum += irq_stats(cpu)->icr_read_retry_count;
	if (x86_platform_ipi_callback)
		sum += irq_stats(cpu)->x86_platform_ipis;
#endif
#ifdef CONFIG_SMP
	sum += irq_stats(cpu)->irq_resched_count;
	sum += irq_stats(cpu)->irq_call_count;
#endif
#ifdef CONFIG_X86_THERMAL_VECTOR
	sum += irq_stats(cpu)->irq_thermal_count;
#endif
#ifdef CONFIG_X86_MCE_THRESHOLD
	sum += irq_stats(cpu)->irq_threshold_count;
#endif
#ifdef CONFIG_X86_HV_CALLBACK_VECTOR
	sum += irq_stats(cpu)->irq_hv_callback_count;
#endif
#if IS_ENABLED(CONFIG_HYPERV)
	sum += irq_stats(cpu)->irq_hv_reenlightenment_count;
	sum += irq_stats(cpu)->hyperv_stimer0_count;
#endif
#ifdef CONFIG_X86_MCE
	sum += per_cpu(mce_exception_count, cpu);
	sum += per_cpu(mce_poll_count, cpu);
#endif
	return sum;
}

u64 arch_irq_stat(void)
{
	u64 sum = atomic_read(&irq_err_count);
	return sum;
}

static __always_inline void handle_irq(struct irq_desc *desc,
				       struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_X86_64))
		generic_handle_irq_desc(desc);
	else
		__handle_irq(desc, regs);
}

static __always_inline int call_irq_handler(int vector, struct pt_regs *regs)
{
	struct irq_desc *desc;
	int ret = 0;

	desc = __this_cpu_read(vector_irq[vector]);
	if (likely(!IS_ERR_OR_NULL(desc))) {
		handle_irq(desc, regs);
	} else {
		ret = -EINVAL;
		if (desc == VECTOR_UNUSED) {
			pr_emerg_ratelimited("%s: %d.%u No irq handler for vector\n",
					     __func__, smp_processor_id(),
					     vector);
		} else {
			__this_cpu_write(vector_irq[vector], VECTOR_UNUSED);
		}
	}

	return ret;
}

/*
 * common_interrupt() handles all normal device IRQ's (the special SMP
 * cross-CPU interrupts have their own entry points).
 */
DEFINE_IDTENTRY_IRQ(common_interrupt)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	/* entry code tells RCU that we're not quiescent.  Check it. */
	RCU_LOCKDEP_WARN(!rcu_is_watching(), "IRQ failed to wake up RCU");

	if (unlikely(call_irq_handler(vector, regs)))
		apic_eoi();

	set_irq_regs(old_regs);
}

#ifdef CONFIG_X86_LOCAL_APIC
/* Function pointer for generic interrupt vector handling */
void (*x86_platform_ipi_callback)(void) = NULL;
/*
 * Handler for X86_PLATFORM_IPI_VECTOR.
 */
DEFINE_IDTENTRY_SYSVEC(sysvec_x86_platform_ipi)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	apic_eoi();
	trace_x86_platform_ipi_entry(X86_PLATFORM_IPI_VECTOR);
	inc_irq_stat(x86_platform_ipis);
	if (x86_platform_ipi_callback)
		x86_platform_ipi_callback();
	trace_x86_platform_ipi_exit(X86_PLATFORM_IPI_VECTOR);
	set_irq_regs(old_regs);
}
#endif

#if IS_ENABLED(CONFIG_KVM)
static void dummy_handler(void) {}
static void (*kvm_posted_intr_wakeup_handler)(void) = dummy_handler;

void kvm_set_posted_intr_wakeup_handler(void (*handler)(void))
{
	if (handler)
		kvm_posted_intr_wakeup_handler = handler;
	else {
		kvm_posted_intr_wakeup_handler = dummy_handler;
		synchronize_rcu();
	}
}
EXPORT_SYMBOL_GPL(kvm_set_posted_intr_wakeup_handler);

/*
 * Handler for POSTED_INTERRUPT_VECTOR.
 */
DEFINE_IDTENTRY_SYSVEC_SIMPLE(sysvec_kvm_posted_intr_ipi)
{
	apic_eoi();
	inc_irq_stat(kvm_posted_intr_ipis);
}

/*
 * Handler for POSTED_INTERRUPT_WAKEUP_VECTOR.
 */
DEFINE_IDTENTRY_SYSVEC(sysvec_kvm_posted_intr_wakeup_ipi)
{
	apic_eoi();
	inc_irq_stat(kvm_posted_intr_wakeup_ipis);
	kvm_posted_intr_wakeup_handler();
}

/*
 * Handler for POSTED_INTERRUPT_NESTED_VECTOR.
 */
DEFINE_IDTENTRY_SYSVEC_SIMPLE(sysvec_kvm_posted_intr_nested_ipi)
{
	apic_eoi();
	inc_irq_stat(kvm_posted_intr_nested_ipis);
}
#endif

#ifdef CONFIG_X86_POSTED_MSI

/* Posted Interrupt Descriptors for coalesced MSIs to be posted */
DEFINE_PER_CPU_ALIGNED(struct pi_desc, posted_msi_pi_desc);

void intel_posted_msi_init(void)
{
	u32 destination;
	u32 apic_id;

	this_cpu_write(posted_msi_pi_desc.nv, POSTED_MSI_NOTIFICATION_VECTOR);

	/*
	 * APIC destination ID is stored in bit 8:15 while in XAPIC mode.
	 * VT-d spec. CH 9.11
	 */
	apic_id = this_cpu_read(x86_cpu_to_apicid);
	destination = x2apic_enabled() ? apic_id : apic_id << 8;
	this_cpu_write(posted_msi_pi_desc.ndst, destination);
}

/*
 * De-multiplexing posted interrupts is on the performance path, the code
 * below is written to optimize the cache performance based on the following
 * considerations:
 * 1.Posted interrupt descriptor (PID) fits in a cache line that is frequently
 *   accessed by both CPU and IOMMU.
 * 2.During posted MSI processing, the CPU needs to do 64-bit read and xchg
 *   for checking and clearing posted interrupt request (PIR), a 256 bit field
 *   within the PID.
 * 3.On the other side, the IOMMU does atomic swaps of the entire PID cache
 *   line when posting interrupts and setting control bits.
 * 4.The CPU can access the cache line a magnitude faster than the IOMMU.
 * 5.Each time the IOMMU does interrupt posting to the PIR will evict the PID
 *   cache line. The cache line states after each operation are as follows:
 *   CPU		IOMMU			PID Cache line state
 *   ---------------------------------------------------------------
 *...read64					exclusive
 *...lock xchg64				modified
 *...			post/atomic swap	invalid
 *...-------------------------------------------------------------
 *
 * To reduce L1 data cache miss, it is important to avoid contention with
 * IOMMU's interrupt posting/atomic swap. Therefore, a copy of PIR is used
 * to dispatch interrupt handlers.
 *
 * In addition, the code is trying to keep the cache line state consistent
 * as much as possible. e.g. when making a copy and clearing the PIR
 * (assuming non-zero PIR bits are present in the entire PIR), it does:
 *		read, read, read, read, xchg, xchg, xchg, xchg
 * instead of:
 *		read, xchg, read, xchg, read, xchg, read, xchg
 */
static __always_inline bool handle_pending_pir(u64 *pir, struct pt_regs *regs)
{
	int i, vec = FIRST_EXTERNAL_VECTOR;
	unsigned long pir_copy[4];
	bool handled = false;

	for (i = 0; i < 4; i++)
		pir_copy[i] = pir[i];

	for (i = 0; i < 4; i++) {
		if (!pir_copy[i])
			continue;

		pir_copy[i] = arch_xchg(&pir[i], 0);
		handled = true;
	}

	if (handled) {
		for_each_set_bit_from(vec, pir_copy, FIRST_SYSTEM_VECTOR)
			call_irq_handler(vec, regs);
	}

	return handled;
}

/*
 * Performance data shows that 3 is good enough to harvest 90+% of the benefit
 * on high IRQ rate workload.
 */
#define MAX_POSTED_MSI_COALESCING_LOOP 3

/*
 * For MSIs that are delivered as posted interrupts, the CPU notifications
 * can be coalesced if the MSIs arrive in high frequency bursts.
 */
DEFINE_IDTENTRY_SYSVEC(sysvec_posted_msi_notification)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	struct pi_desc *pid;
	int i = 0;

	pid = this_cpu_ptr(&posted_msi_pi_desc);

	inc_irq_stat(posted_msi_notification_count);
	irq_enter();

	/*
	 * Max coalescing count includes the extra round of handle_pending_pir
	 * after clearing the outstanding notification bit. Hence, at most
	 * MAX_POSTED_MSI_COALESCING_LOOP - 1 loops are executed here.
	 */
	while (++i < MAX_POSTED_MSI_COALESCING_LOOP) {
		if (!handle_pending_pir(pid->pir64, regs))
			break;
	}

	/*
	 * Clear outstanding notification bit to allow new IRQ notifications,
	 * do this last to maximize the window of interrupt coalescing.
	 */
	pi_clear_on(pid);

	/*
	 * There could be a race of PI notification and the clearing of ON bit,
	 * process PIR bits one last time such that handling the new interrupts
	 * are not delayed until the next IRQ.
	 */
	handle_pending_pir(pid->pir64, regs);

	apic_eoi();
	irq_exit();
	set_irq_regs(old_regs);
}
#endif /* X86_POSTED_MSI */

#ifdef CONFIG_HOTPLUG_CPU
/* A cpu has been removed from cpu_online_mask.  Reset irq affinities. */
void fixup_irqs(void)
{
	unsigned int vector;
	struct irq_desc *desc;
	struct irq_data *data;
	struct irq_chip *chip;

	irq_migrate_all_off_this_cpu();

	/*
	 * We can remove mdelay() and then send spurious interrupts to
	 * new cpu targets for all the irqs that were handled previously by
	 * this cpu. While it works, I have seen spurious interrupt messages
	 * (nothing wrong but still...).
	 *
	 * So for now, retain mdelay(1) and check the IRR and then send those
	 * interrupts to new targets as this cpu is already offlined...
	 */
	mdelay(1);

	/*
	 * We can walk the vector array of this cpu without holding
	 * vector_lock because the cpu is already marked !online, so
	 * nothing else will touch it.
	 */
	for (vector = FIRST_EXTERNAL_VECTOR; vector < NR_VECTORS; vector++) {
		if (IS_ERR_OR_NULL(__this_cpu_read(vector_irq[vector])))
			continue;

		if (is_vector_pending(vector)) {
			desc = __this_cpu_read(vector_irq[vector]);

			raw_spin_lock(&desc->lock);
			data = irq_desc_get_irq_data(desc);
			chip = irq_data_get_irq_chip(data);
			if (chip->irq_retrigger) {
				chip->irq_retrigger(data);
				__this_cpu_write(vector_irq[vector], VECTOR_RETRIGGERED);
			}
			raw_spin_unlock(&desc->lock);
		}
		if (__this_cpu_read(vector_irq[vector]) != VECTOR_RETRIGGERED)
			__this_cpu_write(vector_irq[vector], VECTOR_UNUSED);
	}
}
#endif

#ifdef CONFIG_X86_THERMAL_VECTOR
static void smp_thermal_vector(void)
{
	if (x86_thermal_enabled())
		intel_thermal_interrupt();
	else
		pr_err("CPU%d: Unexpected LVT thermal interrupt!\n",
		       smp_processor_id());
}

DEFINE_IDTENTRY_SYSVEC(sysvec_thermal)
{
	trace_thermal_apic_entry(THERMAL_APIC_VECTOR);
	inc_irq_stat(irq_thermal_count);
	smp_thermal_vector();
	trace_thermal_apic_exit(THERMAL_APIC_VECTOR);
	apic_eoi();
}
#endif
