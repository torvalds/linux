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
#include <linux/kvm_types.h>

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

struct irq_stat_info {
	unsigned int	skip_vector;
	const char	*symbol;
	const char	*text;
};

#define DEFAULT_SUPPRESSED_VECTOR	UINT_MAX

#define ISS(idx, sym, txt) [IRQ_COUNT_##idx] = { .symbol = sym, .text = txt }

#define ITS(idx, sym, txt) [IRQ_COUNT_##idx] =				\
	{ .skip_vector = idx## _VECTOR, .symbol = sym, .text = txt }

#define IDS(idx, sym, txt) [IRQ_COUNT_##idx] =				\
	{ .skip_vector = DEFAULT_SUPPRESSED_VECTOR, .symbol = sym, .text = txt }

static const struct irq_stat_info irq_stat_info[IRQ_COUNT_MAX] = {
	ISS(NMI,			"NMI",	"  Non-maskable interrupts\n"),
#ifdef CONFIG_X86_LOCAL_APIC
	ISS(APIC_TIMER,			"LOC",	"  Local timer interrupts\n"),
	IDS(SPURIOUS,			"SPU",	"  Spurious interrupts\n"),
	ISS(APIC_PERF,			"PMI",	"  Performance monitoring interrupts\n"),
	ISS(IRQ_WORK,			"IWI",	"  IRQ work interrupts\n"),
	IDS(ICR_READ_RETRY,		"RTR",	"  APIC ICR read retries\n"),
	ISS(X86_PLATFORM_IPI,		"PLT",	"  Platform interrupts\n"),
#endif
#ifdef CONFIG_SMP
	ISS(RESCHEDULE,			"RES",	"  Rescheduling interrupts\n"),
	ISS(CALL_FUNCTION,		"CAL",	"  Function call interrupts\n"),
#endif
	ISS(TLB,			"TLB",	"  TLB shootdowns\n"),
#ifdef CONFIG_X86_THERMAL_VECTOR
	ISS(THERMAL_APIC,		"TRM",	"  Thermal event interrupts\n"),
#endif
#ifdef CONFIG_X86_MCE_THRESHOLD
	ISS(THRESHOLD_APIC,		"THR",	"  Threshold APIC interrupts\n"),
#endif
#ifdef CONFIG_X86_MCE_AMD
	ISS(DEFERRED_ERROR,		"DFR",	"  Deferred Error APIC interrupts\n"),
#endif
#ifdef CONFIG_X86_MCE
	ISS(MCE_EXCEPTION,		"MCE",	"  Machine check exceptions\n"),
	ISS(MCE_POLL,			"MCP",	"  Machine check polls\n"),
#endif
#ifdef CONFIG_X86_HV_CALLBACK_VECTOR
	ITS(HYPERVISOR_CALLBACK,	"HYP",	"  Hypervisor callback interrupts\n"),
#endif
#if IS_ENABLED(CONFIG_HYPERV)
	ITS(HYPERV_REENLIGHTENMENT,	"HRE",	"  Hyper-V reenlightenment interrupts\n"),
	ITS(HYPERV_STIMER0,		"HVS",	"  Hyper-V stimer0 interrupts\n"),
#endif
#if IS_ENABLED(CONFIG_KVM)
	ITS(POSTED_INTR,		"PIN",	"  Posted-interrupt notification event\n"),
	ITS(POSTED_INTR_NESTED,		"NPI",	"  Nested posted-interrupt event\n"),
	ITS(POSTED_INTR_WAKEUP,		"PIW",	"  Posted-interrupt wakeup event\n"),
#endif
#ifdef CONFIG_GUEST_PERF_EVENTS
	ISS(PERF_GUEST_MEDIATED_PMI,	"VPMI",	"  Perf Guest Mediated PMI\n"),
#endif
#ifdef CONFIG_X86_POSTED_MSI
	ISS(POSTED_MSI_NOTIFICATION,	"PMN",	"  Posted MSI notification event\n"),
#endif
	IDS(PIC_APIC_ERROR,		"ERR",	"  PIC/APIC error interrupts\n"),
#ifdef CONFIG_X86_IO_APIC
	IDS(IOAPIC_MISROUTED,		"MIS",	"  Misrouted IO/APIC interrupts\n"),
#endif
};

static DECLARE_BITMAP(irq_stat_count_show, IRQ_COUNT_MAX) __read_mostly;

static int __init irq_init_stats(void)
{
	const struct irq_stat_info *info = irq_stat_info;

	for (unsigned int i = 0; i < ARRAY_SIZE(irq_stat_info); i++, info++) {
		if (!info->skip_vector || (info->skip_vector != DEFAULT_SUPPRESSED_VECTOR &&
					   test_bit(info->skip_vector, system_vectors)))
			set_bit(i, irq_stat_count_show);
	}

#ifdef CONFIG_X86_LOCAL_APIC
	if (!x86_platform_ipi_callback)
		clear_bit(IRQ_COUNT_X86_PLATFORM_IPI, irq_stat_count_show);
#endif

#ifdef CONFIG_X86_POSTED_MSI
	if (!posted_msi_enabled())
		clear_bit(IRQ_COUNT_POSTED_MSI_NOTIFICATION, irq_stat_count_show);
#endif

#ifdef CONFIG_X86_MCE_AMD
	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD &&
	    boot_cpu_data.x86_vendor != X86_VENDOR_HYGON)
		clear_bit(IRQ_COUNT_DEFERRED_ERROR, irq_stat_count_show);
#endif
	return 0;
}
late_initcall(irq_init_stats);

/*
 * Used for default disabled counters to increment the stats and to enable the
 * entry for /proc/interrupts output.
 */
void irq_stat_inc_and_enable(enum irq_stat_counts which)
{
	this_cpu_inc(irq_stat.counts[which]);
	set_bit(which, irq_stat_count_show);
}

#ifdef CONFIG_PROC_FS
/*
 * /proc/interrupts printing for arch specific interrupts
 */
int arch_show_interrupts(struct seq_file *p, int prec)
{
	const struct irq_stat_info *info = irq_stat_info;

	for (unsigned int i = 0; i < ARRAY_SIZE(irq_stat_info); i++, info++) {
		if (!test_bit(i, irq_stat_count_show))
			continue;

		seq_printf(p, "%*s:", prec, info->symbol);
		irq_proc_emit_counts(p, &irq_stat.counts[i]);
		seq_puts(p, info->text);
	}
	return 0;
}

/*
 * /proc/stat helpers
 */
u64 arch_irq_stat_cpu(unsigned int cpu)
{
	irq_cpustat_t *p = per_cpu_ptr(&irq_stat, cpu);
	u64 sum = 0;

	for (unsigned int i = 0; i < ARRAY_SIZE(irq_stat_info); i++)
		sum += p->counts[i];
	return sum;
}
#endif /* CONFIG_PROC_FS */

static __always_inline void handle_irq(struct irq_desc *desc,
				       struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_X86_64))
		generic_handle_irq_desc(desc);
	else
		__handle_irq(desc, regs);
}

static struct irq_desc *reevaluate_vector(int vector)
{
	struct irq_desc *desc = __this_cpu_read(vector_irq[vector]);

	if (!IS_ERR_OR_NULL(desc))
		return desc;

	if (desc == VECTOR_UNUSED)
		pr_emerg_ratelimited("No irq handler for %d.%u\n", smp_processor_id(), vector);
	else
		__this_cpu_write(vector_irq[vector], VECTOR_UNUSED);
	return NULL;
}

static __always_inline bool call_irq_handler(int vector, struct pt_regs *regs)
{
	struct irq_desc *desc = __this_cpu_read(vector_irq[vector]);

	if (likely(!IS_ERR_OR_NULL(desc))) {
		handle_irq(desc, regs);
		return true;
	}

	/*
	 * Reevaluate with vector_lock held to prevent a race against
	 * request_irq() setting up the vector:
	 *
	 * CPU0				CPU1
	 *				interrupt is raised in APIC IRR
	 *				but not handled
	 * free_irq()
	 *   per_cpu(vector_irq, CPU1)[vector] = VECTOR_SHUTDOWN;
	 *
	 * request_irq()		common_interrupt()
	 *				  d = this_cpu_read(vector_irq[vector]);
	 *
	 * per_cpu(vector_irq, CPU1)[vector] = desc;
	 *
	 *				  if (d == VECTOR_SHUTDOWN)
	 *				    this_cpu_write(vector_irq[vector], VECTOR_UNUSED);
	 *
	 * This requires that the same vector on the same target CPU is
	 * handed out or that a spurious interrupt hits that CPU/vector.
	 */
	lock_vector_lock();
	desc = reevaluate_vector(vector);
	unlock_vector_lock();

	if (!desc)
		return false;

	handle_irq(desc, regs);
	return true;
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

	if (unlikely(!call_irq_handler(vector, regs)))
		apic_eoi();

	set_irq_regs(old_regs);
}

#ifdef CONFIG_X86_LOCAL_APIC
/* Function pointer for generic interrupt vector handling */
void (*x86_platform_ipi_callback)(void) __ro_after_init = NULL;
/*
 * Handler for X86_PLATFORM_IPI_VECTOR.
 */
DEFINE_IDTENTRY_SYSVEC(sysvec_x86_platform_ipi)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	apic_eoi();
	trace_x86_platform_ipi_entry(X86_PLATFORM_IPI_VECTOR);
	inc_irq_stat(X86_PLATFORM_IPI);
	if (x86_platform_ipi_callback)
		x86_platform_ipi_callback();
	trace_x86_platform_ipi_exit(X86_PLATFORM_IPI_VECTOR);
	set_irq_regs(old_regs);
}
#endif

#ifdef CONFIG_GUEST_PERF_EVENTS
/*
 * Handler for PERF_GUEST_MEDIATED_PMI_VECTOR.
 */
DEFINE_IDTENTRY_SYSVEC(sysvec_perf_guest_mediated_pmi_handler)
{
	 apic_eoi();
	 inc_irq_stat(PERF_GUEST_MEDIATED_PMI);
	 perf_guest_handle_mediated_pmi();
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
EXPORT_SYMBOL_FOR_KVM(kvm_set_posted_intr_wakeup_handler);

/*
 * Handler for POSTED_INTERRUPT_VECTOR.
 */
DEFINE_IDTENTRY_SYSVEC_SIMPLE(sysvec_kvm_posted_intr_ipi)
{
	apic_eoi();
	inc_irq_stat(POSTED_INTR);
}

/*
 * Handler for POSTED_INTERRUPT_WAKEUP_VECTOR.
 */
DEFINE_IDTENTRY_SYSVEC(sysvec_kvm_posted_intr_wakeup_ipi)
{
	apic_eoi();
	inc_irq_stat(POSTED_INTR_WAKEUP);
	kvm_posted_intr_wakeup_handler();
}

/*
 * Handler for POSTED_INTERRUPT_NESTED_VECTOR.
 */
DEFINE_IDTENTRY_SYSVEC_SIMPLE(sysvec_kvm_posted_intr_nested_ipi)
{
	apic_eoi();
	inc_irq_stat(POSTED_INTR_NESTED);
}
#endif

#ifdef CONFIG_X86_POSTED_MSI

/* Posted Interrupt Descriptors for coalesced MSIs to be posted */
DEFINE_PER_CPU_ALIGNED(struct pi_desc, posted_msi_pi_desc);
static DEFINE_PER_CPU_CACHE_HOT(bool, posted_msi_handler_active);

void intel_posted_msi_init(void)
{
	u32 destination, apic_id;

	this_cpu_write(posted_msi_pi_desc.nv, POSTED_MSI_NOTIFICATION_VECTOR);
	/*
	 * APIC destination ID is stored in bit 8:15 while in XAPIC mode.
	 * VT-d spec. CH 9.11
	 */
	apic_id = this_cpu_read(x86_cpu_to_apicid);
	destination = x2apic_enabled() ? apic_id : apic_id << 8;
	this_cpu_write(posted_msi_pi_desc.ndst, destination);
}

void intel_ack_posted_msi_irq(struct irq_data *irqd)
{
	irq_move_irq(irqd);

	/*
	 * Handle the rare case that irq_retrigger() raised the actual
	 * assigned vector on the target CPU, which means that it was not
	 * invoked via the posted MSI handler below. In that case APIC EOI
	 * is required as otherwise the ISR entry becomes stale and lower
	 * priority interrupts are never going to be delivered after that.
	 *
	 * If the posted handler invoked the device interrupt handler then
	 * the EOI would be premature because it would acknowledge the
	 * posted vector.
	 */
	if (unlikely(!__this_cpu_read(posted_msi_handler_active)))
		apic_eoi();
}

static __always_inline bool handle_pending_pir(unsigned long *pir, struct pt_regs *regs)
{
	unsigned long pir_copy[NR_PIR_WORDS];
	int vec = FIRST_EXTERNAL_VECTOR;

	if (!pi_harvest_pir(pir, pir_copy))
		return false;

	for_each_set_bit_from(vec, pir_copy, FIRST_SYSTEM_VECTOR)
		call_irq_handler(vec, regs);

	return true;
}

/*
 * Performance data shows that 3 is good enough to harvest 90+% of the
 * benefit on high interrupt rate workloads.
 */
#define MAX_POSTED_MSI_COALESCING_LOOP 3

/*
 * For MSIs that are delivered as posted interrupts, the CPU notifications
 * can be coalesced if the MSIs arrive in high frequency bursts.
 */
DEFINE_IDTENTRY_SYSVEC(sysvec_posted_msi_notification)
{
	struct pi_desc *pid = this_cpu_ptr(&posted_msi_pi_desc);
	struct pt_regs *old_regs = set_irq_regs(regs);

	/* Mark the handler active for intel_ack_posted_msi_irq() */
	__this_cpu_write(posted_msi_handler_active, true);
	inc_irq_stat(POSTED_MSI_NOTIFICATION);
	irq_enter();

	/*
	 * Loop only MAX_POSTED_MSI_COALESCING_LOOP - 1 times here to take
	 * the final handle_pending_pir() invocation after clearing the
	 * outstanding notification bit into account.
	 */
	for (int i = 1; i < MAX_POSTED_MSI_COALESCING_LOOP; i++) {
		if (!handle_pending_pir(pid->pir, regs))
			break;
	}

	/*
	 * Clear the outstanding notification bit to rearm the notification
	 * mechanism.
	 */
	pi_clear_on(pid);

	/*
	 * Clearing the ON bit can race with a notification. Process the
	 * PIR bits one last time so that handling the new interrupts is
	 * not delayed until the next notification happens.
	 */
	handle_pending_pir(pid->pir, regs);

	apic_eoi();
	irq_exit();
	__this_cpu_write(posted_msi_handler_active, false);
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
	inc_irq_stat(THERMAL_APIC);
	smp_thermal_vector();
	trace_thermal_apic_exit(THERMAL_APIC_VECTOR);
	apic_eoi();
}
#endif
