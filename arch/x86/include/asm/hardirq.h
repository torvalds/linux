/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_HARDIRQ_H
#define _ASM_X86_HARDIRQ_H

#include <linux/threads.h>

enum irq_stat_counts {
	IRQ_COUNT_NMI,
#ifdef CONFIG_X86_LOCAL_APIC
	IRQ_COUNT_APIC_TIMER,
	IRQ_COUNT_SPURIOUS,
	IRQ_COUNT_APIC_PERF,
	IRQ_COUNT_IRQ_WORK,
	IRQ_COUNT_ICR_READ_RETRY,
	IRQ_COUNT_X86_PLATFORM_IPI,
#endif
#ifdef CONFIG_SMP
	IRQ_COUNT_RESCHEDULE,
	IRQ_COUNT_CALL_FUNCTION,
#endif
	IRQ_COUNT_TLB,
#ifdef CONFIG_X86_THERMAL_VECTOR
	IRQ_COUNT_THERMAL_APIC,
#endif
#ifdef CONFIG_X86_MCE_THRESHOLD
	IRQ_COUNT_THRESHOLD_APIC,
#endif
#ifdef CONFIG_X86_MCE_AMD
	IRQ_COUNT_DEFERRED_ERROR,
#endif
#ifdef CONFIG_X86_MCE
	IRQ_COUNT_MCE_EXCEPTION,
	IRQ_COUNT_MCE_POLL,
#endif
#ifdef CONFIG_X86_HV_CALLBACK_VECTOR
	IRQ_COUNT_HYPERVISOR_CALLBACK,
#endif
#if IS_ENABLED(CONFIG_HYPERV)
	IRQ_COUNT_HYPERV_REENLIGHTENMENT,
	IRQ_COUNT_HYPERV_STIMER0,
#endif
#if IS_ENABLED(CONFIG_KVM)
	IRQ_COUNT_POSTED_INTR,
	IRQ_COUNT_POSTED_INTR_NESTED,
	IRQ_COUNT_POSTED_INTR_WAKEUP,
#endif
#ifdef CONFIG_GUEST_PERF_EVENTS
	IRQ_COUNT_PERF_GUEST_MEDIATED_PMI,
#endif
#ifdef CONFIG_X86_POSTED_MSI
	IRQ_COUNT_POSTED_MSI_NOTIFICATION,
#endif
	IRQ_COUNT_PIC_APIC_ERROR,
#ifdef CONFIG_X86_IO_APIC
	IRQ_COUNT_IOAPIC_MISROUTED,
#endif
	IRQ_COUNT_MAX,
};

typedef struct {
#if IS_ENABLED(CONFIG_CPU_MITIGATIONS) && IS_ENABLED(CONFIG_KVM_INTEL)
	u8	     kvm_cpu_l1tf_flush_l1d;
#endif
	unsigned int counts[IRQ_COUNT_MAX];
} ____cacheline_aligned irq_cpustat_t;

DECLARE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

#ifdef CONFIG_X86_POSTED_MSI
DECLARE_PER_CPU_ALIGNED(struct pi_desc, posted_msi_pi_desc);
#endif
#define __ARCH_IRQ_STAT

#define inc_irq_stat(index)	this_cpu_inc(irq_stat.counts[IRQ_COUNT_##index])
void irq_stat_inc_and_enable(enum irq_stat_counts which);

#ifdef CONFIG_X86_LOCAL_APIC
#define inc_perf_irq_stat()	inc_irq_stat(APIC_PERF)
#else
#define inc_perf_irq_stat()	do { } while (0)
#endif

extern void ack_bad_irq(unsigned int irq);

#ifdef CONFIG_PROC_FS
extern u64 arch_irq_stat_cpu(unsigned int cpu);
#define arch_irq_stat_cpu	arch_irq_stat_cpu
#endif

DECLARE_PER_CPU_CACHE_HOT(u16, __softirq_pending);
#define local_softirq_pending_ref       __softirq_pending

#if IS_ENABLED(CONFIG_CPU_MITIGATIONS) && IS_ENABLED(CONFIG_KVM_INTEL)
/*
 * This function is called from noinstr interrupt contexts
 * and must be inlined to not get instrumentation.
 */
static __always_inline void kvm_set_cpu_l1tf_flush_l1d(void)
{
	__this_cpu_write(irq_stat.kvm_cpu_l1tf_flush_l1d, 1);
}

static __always_inline void kvm_clear_cpu_l1tf_flush_l1d(void)
{
	__this_cpu_write(irq_stat.kvm_cpu_l1tf_flush_l1d, 0);
}

static __always_inline bool kvm_get_cpu_l1tf_flush_l1d(void)
{
	return __this_cpu_read(irq_stat.kvm_cpu_l1tf_flush_l1d);
}
#else /* !IS_ENABLED(CONFIG_KVM_INTEL) */
static __always_inline void kvm_set_cpu_l1tf_flush_l1d(void) { }
#endif /* IS_ENABLED(CONFIG_KVM_INTEL) */

#endif /* _ASM_X86_HARDIRQ_H */
