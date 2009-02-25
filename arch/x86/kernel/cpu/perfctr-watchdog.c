/*
 * local apic based NMI watchdog for various CPUs.
 *
 * This file also handles reservation of performance counters for coordination
 * with other users (like oprofile).
 *
 * Note that these events normally don't tick when the CPU idles. This means
 * the frequency varies with CPU load.
 *
 * Original code for K7/P6 written by Keith Owens
 *
 */

#include <linux/percpu.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/smp.h>
#include <linux/nmi.h>
#include <linux/kprobes.h>

#include <asm/genapic.h>
#include <asm/intel_arch_perfmon.h>

struct nmi_watchdog_ctlblk {
	unsigned int cccr_msr;
	unsigned int perfctr_msr;  /* the MSR to reset in NMI handler */
	unsigned int evntsel_msr;  /* the MSR to select the events to handle */
};

/* Interface defining a CPU specific perfctr watchdog */
struct wd_ops {
	int (*reserve)(void);
	void (*unreserve)(void);
	int (*setup)(unsigned nmi_hz);
	void (*rearm)(struct nmi_watchdog_ctlblk *wd, unsigned nmi_hz);
	void (*stop)(void);
	unsigned perfctr;
	unsigned evntsel;
	u64 checkbit;
};

static const struct wd_ops *wd_ops;

/*
 * this number is calculated from Intel's MSR_P4_CRU_ESCR5 register and it's
 * offset from MSR_P4_BSU_ESCR0.
 *
 * It will be the max for all platforms (for now)
 */
#define NMI_MAX_COUNTER_BITS 66

/*
 * perfctr_nmi_owner tracks the ownership of the perfctr registers:
 * evtsel_nmi_owner tracks the ownership of the event selection
 * - different performance counters/ event selection may be reserved for
 *   different subsystems this reservation system just tries to coordinate
 *   things a little
 */
static DECLARE_BITMAP(perfctr_nmi_owner, NMI_MAX_COUNTER_BITS);
static DECLARE_BITMAP(evntsel_nmi_owner, NMI_MAX_COUNTER_BITS);

static DEFINE_PER_CPU(struct nmi_watchdog_ctlblk, nmi_watchdog_ctlblk);

/* converts an msr to an appropriate reservation bit */
static inline unsigned int nmi_perfctr_msr_to_bit(unsigned int msr)
{
	/* returns the bit offset of the performance counter register */
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		return (msr - MSR_K7_PERFCTR0);
	case X86_VENDOR_INTEL:
		if (cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON))
			return (msr - MSR_ARCH_PERFMON_PERFCTR0);

		switch (boot_cpu_data.x86) {
		case 6:
			return (msr - MSR_P6_PERFCTR0);
		case 15:
			return (msr - MSR_P4_BPU_PERFCTR0);
		}
	}
	return 0;
}

/*
 * converts an msr to an appropriate reservation bit
 * returns the bit offset of the event selection register
 */
static inline unsigned int nmi_evntsel_msr_to_bit(unsigned int msr)
{
	/* returns the bit offset of the event selection register */
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		return (msr - MSR_K7_EVNTSEL0);
	case X86_VENDOR_INTEL:
		if (cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON))
			return (msr - MSR_ARCH_PERFMON_EVENTSEL0);

		switch (boot_cpu_data.x86) {
		case 6:
			return (msr - MSR_P6_EVNTSEL0);
		case 15:
			return (msr - MSR_P4_BSU_ESCR0);
		}
	}
	return 0;

}

/* checks for a bit availability (hack for oprofile) */
int avail_to_resrv_perfctr_nmi_bit(unsigned int counter)
{
	BUG_ON(counter > NMI_MAX_COUNTER_BITS);

	return (!test_bit(counter, perfctr_nmi_owner));
}

/* checks the an msr for availability */
int avail_to_resrv_perfctr_nmi(unsigned int msr)
{
	unsigned int counter;

	counter = nmi_perfctr_msr_to_bit(msr);
	BUG_ON(counter > NMI_MAX_COUNTER_BITS);

	return (!test_bit(counter, perfctr_nmi_owner));
}
EXPORT_SYMBOL(avail_to_resrv_perfctr_nmi_bit);

int reserve_perfctr_nmi(unsigned int msr)
{
	unsigned int counter;

	counter = nmi_perfctr_msr_to_bit(msr);
	/* register not managed by the allocator? */
	if (counter > NMI_MAX_COUNTER_BITS)
		return 1;

	if (!test_and_set_bit(counter, perfctr_nmi_owner))
		return 1;
	return 0;
}
EXPORT_SYMBOL(reserve_perfctr_nmi);

void release_perfctr_nmi(unsigned int msr)
{
	unsigned int counter;

	counter = nmi_perfctr_msr_to_bit(msr);
	/* register not managed by the allocator? */
	if (counter > NMI_MAX_COUNTER_BITS)
		return;

	clear_bit(counter, perfctr_nmi_owner);
}
EXPORT_SYMBOL(release_perfctr_nmi);

int reserve_evntsel_nmi(unsigned int msr)
{
	unsigned int counter;

	counter = nmi_evntsel_msr_to_bit(msr);
	/* register not managed by the allocator? */
	if (counter > NMI_MAX_COUNTER_BITS)
		return 1;

	if (!test_and_set_bit(counter, evntsel_nmi_owner))
		return 1;
	return 0;
}
EXPORT_SYMBOL(reserve_evntsel_nmi);

void release_evntsel_nmi(unsigned int msr)
{
	unsigned int counter;

	counter = nmi_evntsel_msr_to_bit(msr);
	/* register not managed by the allocator? */
	if (counter > NMI_MAX_COUNTER_BITS)
		return;

	clear_bit(counter, evntsel_nmi_owner);
}
EXPORT_SYMBOL(release_evntsel_nmi);

void disable_lapic_nmi_watchdog(void)
{
	BUG_ON(nmi_watchdog != NMI_LOCAL_APIC);

	if (atomic_read(&nmi_active) <= 0)
		return;

	on_each_cpu(stop_apic_nmi_watchdog, NULL, 1);

	if (wd_ops)
		wd_ops->unreserve();

	BUG_ON(atomic_read(&nmi_active) != 0);
}

void enable_lapic_nmi_watchdog(void)
{
	BUG_ON(nmi_watchdog != NMI_LOCAL_APIC);

	/* are we already enabled */
	if (atomic_read(&nmi_active) != 0)
		return;

	/* are we lapic aware */
	if (!wd_ops)
		return;
	if (!wd_ops->reserve()) {
		printk(KERN_ERR "NMI watchdog: cannot reserve perfctrs\n");
		return;
	}

	on_each_cpu(setup_apic_nmi_watchdog, NULL, 1);
	touch_nmi_watchdog();
}

/*
 * Activate the NMI watchdog via the local APIC.
 */

static unsigned int adjust_for_32bit_ctr(unsigned int hz)
{
	u64 counter_val;
	unsigned int retval = hz;

	/*
	 * On Intel CPUs with P6/ARCH_PERFMON only 32 bits in the counter
	 * are writable, with higher bits sign extending from bit 31.
	 * So, we can only program the counter with 31 bit values and
	 * 32nd bit should be 1, for 33.. to be 1.
	 * Find the appropriate nmi_hz
	 */
	counter_val = (u64)cpu_khz * 1000;
	do_div(counter_val, retval);
 	if (counter_val > 0x7fffffffULL) {
		u64 count = (u64)cpu_khz * 1000;
		do_div(count, 0x7fffffffUL);
		retval = count + 1;
	}
	return retval;
}

static void write_watchdog_counter(unsigned int perfctr_msr,
				const char *descr, unsigned nmi_hz)
{
	u64 count = (u64)cpu_khz * 1000;

	do_div(count, nmi_hz);
	if(descr)
		pr_debug("setting %s to -0x%08Lx\n", descr, count);
	wrmsrl(perfctr_msr, 0 - count);
}

static void write_watchdog_counter32(unsigned int perfctr_msr,
				const char *descr, unsigned nmi_hz)
{
	u64 count = (u64)cpu_khz * 1000;

	do_div(count, nmi_hz);
	if(descr)
		pr_debug("setting %s to -0x%08Lx\n", descr, count);
	wrmsr(perfctr_msr, (u32)(-count), 0);
}

/*
 * AMD K7/K8/Family10h/Family11h support.
 * AMD keeps this interface nicely stable so there is not much variety
 */
#define K7_EVNTSEL_ENABLE	(1 << 22)
#define K7_EVNTSEL_INT		(1 << 20)
#define K7_EVNTSEL_OS		(1 << 17)
#define K7_EVNTSEL_USR		(1 << 16)
#define K7_EVENT_CYCLES_PROCESSOR_IS_RUNNING	0x76
#define K7_NMI_EVENT		K7_EVENT_CYCLES_PROCESSOR_IS_RUNNING

static int setup_k7_watchdog(unsigned nmi_hz)
{
	unsigned int perfctr_msr, evntsel_msr;
	unsigned int evntsel;
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);

	perfctr_msr = wd_ops->perfctr;
	evntsel_msr = wd_ops->evntsel;

	wrmsrl(perfctr_msr, 0UL);

	evntsel = K7_EVNTSEL_INT
		| K7_EVNTSEL_OS
		| K7_EVNTSEL_USR
		| K7_NMI_EVENT;

	/* setup the timer */
	wrmsr(evntsel_msr, evntsel, 0);
	write_watchdog_counter(perfctr_msr, "K7_PERFCTR0",nmi_hz);

	/* initialize the wd struct before enabling */
	wd->perfctr_msr = perfctr_msr;
	wd->evntsel_msr = evntsel_msr;
	wd->cccr_msr = 0;  /* unused */

	/* ok, everything is initialized, announce that we're set */
	cpu_nmi_set_wd_enabled();

	apic_write(APIC_LVTPC, APIC_DM_NMI);
	evntsel |= K7_EVNTSEL_ENABLE;
	wrmsr(evntsel_msr, evntsel, 0);

	return 1;
}

static void single_msr_stop_watchdog(void)
{
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);

	wrmsr(wd->evntsel_msr, 0, 0);
}

static int single_msr_reserve(void)
{
	if (!reserve_perfctr_nmi(wd_ops->perfctr))
		return 0;

	if (!reserve_evntsel_nmi(wd_ops->evntsel)) {
		release_perfctr_nmi(wd_ops->perfctr);
		return 0;
	}
	return 1;
}

static void single_msr_unreserve(void)
{
	release_evntsel_nmi(wd_ops->evntsel);
	release_perfctr_nmi(wd_ops->perfctr);
}

static void __kprobes
single_msr_rearm(struct nmi_watchdog_ctlblk *wd, unsigned nmi_hz)
{
	/* start the cycle over again */
	write_watchdog_counter(wd->perfctr_msr, NULL, nmi_hz);
}

static const struct wd_ops k7_wd_ops = {
	.reserve	= single_msr_reserve,
	.unreserve	= single_msr_unreserve,
	.setup		= setup_k7_watchdog,
	.rearm		= single_msr_rearm,
	.stop		= single_msr_stop_watchdog,
	.perfctr	= MSR_K7_PERFCTR0,
	.evntsel	= MSR_K7_EVNTSEL0,
	.checkbit	= 1ULL << 47,
};

/*
 * Intel Model 6 (PPro+,P2,P3,P-M,Core1)
 */
#define P6_EVNTSEL0_ENABLE	(1 << 22)
#define P6_EVNTSEL_INT		(1 << 20)
#define P6_EVNTSEL_OS		(1 << 17)
#define P6_EVNTSEL_USR		(1 << 16)
#define P6_EVENT_CPU_CLOCKS_NOT_HALTED	0x79
#define P6_NMI_EVENT		P6_EVENT_CPU_CLOCKS_NOT_HALTED

static int setup_p6_watchdog(unsigned nmi_hz)
{
	unsigned int perfctr_msr, evntsel_msr;
	unsigned int evntsel;
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);

	perfctr_msr = wd_ops->perfctr;
	evntsel_msr = wd_ops->evntsel;

	/* KVM doesn't implement this MSR */
	if (wrmsr_safe(perfctr_msr, 0, 0) < 0)
		return 0;

	evntsel = P6_EVNTSEL_INT
		| P6_EVNTSEL_OS
		| P6_EVNTSEL_USR
		| P6_NMI_EVENT;

	/* setup the timer */
	wrmsr(evntsel_msr, evntsel, 0);
	nmi_hz = adjust_for_32bit_ctr(nmi_hz);
	write_watchdog_counter32(perfctr_msr, "P6_PERFCTR0",nmi_hz);

	/* initialize the wd struct before enabling */
	wd->perfctr_msr = perfctr_msr;
	wd->evntsel_msr = evntsel_msr;
	wd->cccr_msr = 0;  /* unused */

	/* ok, everything is initialized, announce that we're set */
	cpu_nmi_set_wd_enabled();

	apic_write(APIC_LVTPC, APIC_DM_NMI);
	evntsel |= P6_EVNTSEL0_ENABLE;
	wrmsr(evntsel_msr, evntsel, 0);

	return 1;
}

static void __kprobes p6_rearm(struct nmi_watchdog_ctlblk *wd, unsigned nmi_hz)
{
	/*
	 * P6 based Pentium M need to re-unmask
	 * the apic vector but it doesn't hurt
	 * other P6 variant.
	 * ArchPerfom/Core Duo also needs this
	 */
	apic_write(APIC_LVTPC, APIC_DM_NMI);

	/* P6/ARCH_PERFMON has 32 bit counter write */
	write_watchdog_counter32(wd->perfctr_msr, NULL,nmi_hz);
}

static const struct wd_ops p6_wd_ops = {
	.reserve	= single_msr_reserve,
	.unreserve	= single_msr_unreserve,
	.setup		= setup_p6_watchdog,
	.rearm		= p6_rearm,
	.stop		= single_msr_stop_watchdog,
	.perfctr	= MSR_P6_PERFCTR0,
	.evntsel	= MSR_P6_EVNTSEL0,
	.checkbit	= 1ULL << 39,
};

/*
 * Intel P4 performance counters.
 * By far the most complicated of all.
 */
#define MSR_P4_MISC_ENABLE_PERF_AVAIL	(1 << 7)
#define P4_ESCR_EVENT_SELECT(N)	((N) << 25)
#define P4_ESCR_OS		(1 << 3)
#define P4_ESCR_USR		(1 << 2)
#define P4_CCCR_OVF_PMI0	(1 << 26)
#define P4_CCCR_OVF_PMI1	(1 << 27)
#define P4_CCCR_THRESHOLD(N)	((N) << 20)
#define P4_CCCR_COMPLEMENT	(1 << 19)
#define P4_CCCR_COMPARE		(1 << 18)
#define P4_CCCR_REQUIRED	(3 << 16)
#define P4_CCCR_ESCR_SELECT(N)	((N) << 13)
#define P4_CCCR_ENABLE		(1 << 12)
#define P4_CCCR_OVF 		(1 << 31)

#define P4_CONTROLS 18
static unsigned int p4_controls[18] = {
	MSR_P4_BPU_CCCR0,
	MSR_P4_BPU_CCCR1,
	MSR_P4_BPU_CCCR2,
	MSR_P4_BPU_CCCR3,
	MSR_P4_MS_CCCR0,
	MSR_P4_MS_CCCR1,
	MSR_P4_MS_CCCR2,
	MSR_P4_MS_CCCR3,
	MSR_P4_FLAME_CCCR0,
	MSR_P4_FLAME_CCCR1,
	MSR_P4_FLAME_CCCR2,
	MSR_P4_FLAME_CCCR3,
	MSR_P4_IQ_CCCR0,
	MSR_P4_IQ_CCCR1,
	MSR_P4_IQ_CCCR2,
	MSR_P4_IQ_CCCR3,
	MSR_P4_IQ_CCCR4,
	MSR_P4_IQ_CCCR5,
};
/*
 * Set up IQ_COUNTER0 to behave like a clock, by having IQ_CCCR0 filter
 * CRU_ESCR0 (with any non-null event selector) through a complemented
 * max threshold. [IA32-Vol3, Section 14.9.9]
 */
static int setup_p4_watchdog(unsigned nmi_hz)
{
	unsigned int perfctr_msr, evntsel_msr, cccr_msr;
	unsigned int evntsel, cccr_val;
	unsigned int misc_enable, dummy;
	unsigned int ht_num;
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);

	rdmsr(MSR_IA32_MISC_ENABLE, misc_enable, dummy);
	if (!(misc_enable & MSR_P4_MISC_ENABLE_PERF_AVAIL))
		return 0;

#ifdef CONFIG_SMP
	/* detect which hyperthread we are on */
	if (smp_num_siblings == 2) {
		unsigned int ebx, apicid;

        	ebx = cpuid_ebx(1);
	        apicid = (ebx >> 24) & 0xff;
        	ht_num = apicid & 1;
	} else
#endif
		ht_num = 0;

	/*
	 * performance counters are shared resources
	 * assign each hyperthread its own set
	 * (re-use the ESCR0 register, seems safe
	 * and keeps the cccr_val the same)
	 */
	if (!ht_num) {
		/* logical cpu 0 */
		perfctr_msr = MSR_P4_IQ_PERFCTR0;
		evntsel_msr = MSR_P4_CRU_ESCR0;
		cccr_msr = MSR_P4_IQ_CCCR0;
		cccr_val = P4_CCCR_OVF_PMI0 | P4_CCCR_ESCR_SELECT(4);

		/*
		 * If we're on the kdump kernel or other situation, we may
		 * still have other performance counter registers set to
		 * interrupt and they'll keep interrupting forever because
		 * of the P4_CCCR_OVF quirk. So we need to ACK all the
		 * pending interrupts and disable all the registers here,
		 * before reenabling the NMI delivery. Refer to p4_rearm()
		 * about the P4_CCCR_OVF quirk.
		 */
		if (reset_devices) {
			unsigned int low, high;
			int i;

			for (i = 0; i < P4_CONTROLS; i++) {
				rdmsr(p4_controls[i], low, high);
				low &= ~(P4_CCCR_ENABLE | P4_CCCR_OVF);
				wrmsr(p4_controls[i], low, high);
			}
		}
	} else {
		/* logical cpu 1 */
		perfctr_msr = MSR_P4_IQ_PERFCTR1;
		evntsel_msr = MSR_P4_CRU_ESCR0;
		cccr_msr = MSR_P4_IQ_CCCR1;

		/* Pentium 4 D processors don't support P4_CCCR_OVF_PMI1 */
		if (boot_cpu_data.x86_model == 4 && boot_cpu_data.x86_mask == 4)
			cccr_val = P4_CCCR_OVF_PMI0;
		else
			cccr_val = P4_CCCR_OVF_PMI1;
		cccr_val |= P4_CCCR_ESCR_SELECT(4);
	}

	evntsel = P4_ESCR_EVENT_SELECT(0x3F)
	 	| P4_ESCR_OS
		| P4_ESCR_USR;

	cccr_val |= P4_CCCR_THRESHOLD(15)
		 | P4_CCCR_COMPLEMENT
		 | P4_CCCR_COMPARE
		 | P4_CCCR_REQUIRED;

	wrmsr(evntsel_msr, evntsel, 0);
	wrmsr(cccr_msr, cccr_val, 0);
	write_watchdog_counter(perfctr_msr, "P4_IQ_COUNTER0", nmi_hz);

	wd->perfctr_msr = perfctr_msr;
	wd->evntsel_msr = evntsel_msr;
	wd->cccr_msr = cccr_msr;

	/* ok, everything is initialized, announce that we're set */
	cpu_nmi_set_wd_enabled();

	apic_write(APIC_LVTPC, APIC_DM_NMI);
	cccr_val |= P4_CCCR_ENABLE;
	wrmsr(cccr_msr, cccr_val, 0);
	return 1;
}

static void stop_p4_watchdog(void)
{
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);
	wrmsr(wd->cccr_msr, 0, 0);
	wrmsr(wd->evntsel_msr, 0, 0);
}

static int p4_reserve(void)
{
	if (!reserve_perfctr_nmi(MSR_P4_IQ_PERFCTR0))
		return 0;
#ifdef CONFIG_SMP
	if (smp_num_siblings > 1 && !reserve_perfctr_nmi(MSR_P4_IQ_PERFCTR1))
		goto fail1;
#endif
	if (!reserve_evntsel_nmi(MSR_P4_CRU_ESCR0))
		goto fail2;
	/* RED-PEN why is ESCR1 not reserved here? */
	return 1;
 fail2:
#ifdef CONFIG_SMP
	if (smp_num_siblings > 1)
		release_perfctr_nmi(MSR_P4_IQ_PERFCTR1);
 fail1:
#endif
	release_perfctr_nmi(MSR_P4_IQ_PERFCTR0);
	return 0;
}

static void p4_unreserve(void)
{
#ifdef CONFIG_SMP
	if (smp_num_siblings > 1)
		release_perfctr_nmi(MSR_P4_IQ_PERFCTR1);
#endif
	release_evntsel_nmi(MSR_P4_CRU_ESCR0);
	release_perfctr_nmi(MSR_P4_IQ_PERFCTR0);
}

static void __kprobes p4_rearm(struct nmi_watchdog_ctlblk *wd, unsigned nmi_hz)
{
	unsigned dummy;
	/*
 	 * P4 quirks:
	 * - An overflown perfctr will assert its interrupt
	 *   until the OVF flag in its CCCR is cleared.
	 * - LVTPC is masked on interrupt and must be
	 *   unmasked by the LVTPC handler.
	 */
	rdmsrl(wd->cccr_msr, dummy);
	dummy &= ~P4_CCCR_OVF;
	wrmsrl(wd->cccr_msr, dummy);
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	/* start the cycle over again */
	write_watchdog_counter(wd->perfctr_msr, NULL, nmi_hz);
}

static const struct wd_ops p4_wd_ops = {
	.reserve	= p4_reserve,
	.unreserve	= p4_unreserve,
	.setup		= setup_p4_watchdog,
	.rearm		= p4_rearm,
	.stop		= stop_p4_watchdog,
	/* RED-PEN this is wrong for the other sibling */
	.perfctr	= MSR_P4_BPU_PERFCTR0,
	.evntsel	= MSR_P4_BSU_ESCR0,
	.checkbit	= 1ULL << 39,
};

/*
 * Watchdog using the Intel architected PerfMon.
 * Used for Core2 and hopefully all future Intel CPUs.
 */
#define ARCH_PERFMON_NMI_EVENT_SEL	ARCH_PERFMON_UNHALTED_CORE_CYCLES_SEL
#define ARCH_PERFMON_NMI_EVENT_UMASK	ARCH_PERFMON_UNHALTED_CORE_CYCLES_UMASK

static struct wd_ops intel_arch_wd_ops;

static int setup_intel_arch_watchdog(unsigned nmi_hz)
{
	unsigned int ebx;
	union cpuid10_eax eax;
	unsigned int unused;
	unsigned int perfctr_msr, evntsel_msr;
	unsigned int evntsel;
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);

	/*
	 * Check whether the Architectural PerfMon supports
	 * Unhalted Core Cycles Event or not.
	 * NOTE: Corresponding bit = 0 in ebx indicates event present.
	 */
	cpuid(10, &(eax.full), &ebx, &unused, &unused);
	if ((eax.split.mask_length < (ARCH_PERFMON_UNHALTED_CORE_CYCLES_INDEX+1)) ||
	    (ebx & ARCH_PERFMON_UNHALTED_CORE_CYCLES_PRESENT))
		return 0;

	perfctr_msr = wd_ops->perfctr;
	evntsel_msr = wd_ops->evntsel;

	wrmsrl(perfctr_msr, 0UL);

	evntsel = ARCH_PERFMON_EVENTSEL_INT
		| ARCH_PERFMON_EVENTSEL_OS
		| ARCH_PERFMON_EVENTSEL_USR
		| ARCH_PERFMON_NMI_EVENT_SEL
		| ARCH_PERFMON_NMI_EVENT_UMASK;

	/* setup the timer */
	wrmsr(evntsel_msr, evntsel, 0);
	nmi_hz = adjust_for_32bit_ctr(nmi_hz);
	write_watchdog_counter32(perfctr_msr, "INTEL_ARCH_PERFCTR0", nmi_hz);

	wd->perfctr_msr = perfctr_msr;
	wd->evntsel_msr = evntsel_msr;
	wd->cccr_msr = 0;  /* unused */

	/* ok, everything is initialized, announce that we're set */
	cpu_nmi_set_wd_enabled();

	apic_write(APIC_LVTPC, APIC_DM_NMI);
	evntsel |= ARCH_PERFMON_EVENTSEL0_ENABLE;
	wrmsr(evntsel_msr, evntsel, 0);
	intel_arch_wd_ops.checkbit = 1ULL << (eax.split.bit_width - 1);
	return 1;
}

static struct wd_ops intel_arch_wd_ops __read_mostly = {
	.reserve	= single_msr_reserve,
	.unreserve	= single_msr_unreserve,
	.setup		= setup_intel_arch_watchdog,
	.rearm		= p6_rearm,
	.stop		= single_msr_stop_watchdog,
	.perfctr	= MSR_ARCH_PERFMON_PERFCTR1,
	.evntsel	= MSR_ARCH_PERFMON_EVENTSEL1,
};

static void probe_nmi_watchdog(void)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		if (boot_cpu_data.x86 != 6 && boot_cpu_data.x86 != 15 &&
		    boot_cpu_data.x86 != 16)
			return;
		wd_ops = &k7_wd_ops;
		break;
	case X86_VENDOR_INTEL:
		/*
		 * Work around Core Duo (Yonah) errata AE49 where perfctr1
		 * doesn't have a working enable bit.
		 */
		if (boot_cpu_data.x86 == 6 && boot_cpu_data.x86_model == 14) {
			intel_arch_wd_ops.perfctr = MSR_ARCH_PERFMON_PERFCTR0;
			intel_arch_wd_ops.evntsel = MSR_ARCH_PERFMON_EVENTSEL0;
		}
		if (cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON)) {
			wd_ops = &intel_arch_wd_ops;
			break;
		}
		switch (boot_cpu_data.x86) {
		case 6:
			if (boot_cpu_data.x86_model > 13)
				return;

			wd_ops = &p6_wd_ops;
			break;
		case 15:
			wd_ops = &p4_wd_ops;
			break;
		default:
			return;
		}
		break;
	}
}

/* Interface to nmi.c */

int lapic_watchdog_init(unsigned nmi_hz)
{
	if (!wd_ops) {
		probe_nmi_watchdog();
		if (!wd_ops) {
			printk(KERN_INFO "NMI watchdog: CPU not supported\n");
			return -1;
		}

		if (!wd_ops->reserve()) {
			printk(KERN_ERR
				"NMI watchdog: cannot reserve perfctrs\n");
			return -1;
		}
	}

	if (!(wd_ops->setup(nmi_hz))) {
		printk(KERN_ERR "Cannot setup NMI watchdog on CPU %d\n",
		       raw_smp_processor_id());
		return -1;
	}

	return 0;
}

void lapic_watchdog_stop(void)
{
	if (wd_ops)
		wd_ops->stop();
}

unsigned lapic_adjust_nmi_hz(unsigned hz)
{
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);
	if (wd->perfctr_msr == MSR_P6_PERFCTR0 ||
	    wd->perfctr_msr == MSR_ARCH_PERFMON_PERFCTR1)
		hz = adjust_for_32bit_ctr(hz);
	return hz;
}

int __kprobes lapic_wd_event(unsigned nmi_hz)
{
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);
	u64 ctr;

	rdmsrl(wd->perfctr_msr, ctr);
	if (ctr & wd_ops->checkbit) /* perfctr still running? */
		return 0;

	wd_ops->rearm(wd, nmi_hz);
	return 1;
}

int lapic_watchdog_ok(void)
{
	return wd_ops != NULL;
}
