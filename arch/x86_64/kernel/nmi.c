/*
 *  linux/arch/x86_64/nmi.c
 *
 *  NMI watchdog support on APIC systems
 *
 *  Started by Ingo Molnar <mingo@redhat.com>
 *
 *  Fixes:
 *  Mikael Pettersson	: AMD K7 support for local APIC NMI watchdog.
 *  Mikael Pettersson	: Power Management for local APIC NMI watchdog.
 *  Pavel Machek and
 *  Mikael Pettersson	: PM converted to driver model. Disable/enable API.
 */

#include <linux/nmi.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/sysdev.h>
#include <linux/sysctl.h>
#include <linux/kprobes.h>
#include <linux/cpumask.h>

#include <asm/smp.h>
#include <asm/nmi.h>
#include <asm/proto.h>
#include <asm/kdebug.h>
#include <asm/mce.h>
#include <asm/intel_arch_perfmon.h>

int unknown_nmi_panic;
int nmi_watchdog_enabled;
int panic_on_unrecovered_nmi;

/* perfctr_nmi_owner tracks the ownership of the perfctr registers:
 * evtsel_nmi_owner tracks the ownership of the event selection
 * - different performance counters/ event selection may be reserved for
 *   different subsystems this reservation system just tries to coordinate
 *   things a little
 */
static DEFINE_PER_CPU(unsigned, perfctr_nmi_owner);
static DEFINE_PER_CPU(unsigned, evntsel_nmi_owner[2]);

static cpumask_t backtrace_mask = CPU_MASK_NONE;

/* this number is calculated from Intel's MSR_P4_CRU_ESCR5 register and it's
 * offset from MSR_P4_BSU_ESCR0.  It will be the max for all platforms (for now)
 */
#define NMI_MAX_COUNTER_BITS 66

/* nmi_active:
 * >0: the lapic NMI watchdog is active, but can be disabled
 * <0: the lapic NMI watchdog has not been set up, and cannot
 *     be enabled
 *  0: the lapic NMI watchdog is disabled, but can be enabled
 */
atomic_t nmi_active = ATOMIC_INIT(0);		/* oprofile uses this */
int panic_on_timeout;

unsigned int nmi_watchdog = NMI_DEFAULT;
static unsigned int nmi_hz = HZ;

struct nmi_watchdog_ctlblk {
	int enabled;
	u64 check_bit;
	unsigned int cccr_msr;
	unsigned int perfctr_msr;  /* the MSR to reset in NMI handler */
	unsigned int evntsel_msr;  /* the MSR to select the events to handle */
};
static DEFINE_PER_CPU(struct nmi_watchdog_ctlblk, nmi_watchdog_ctlblk);

/* local prototypes */
static int unknown_nmi_panic_callback(struct pt_regs *regs, int cpu);

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
		else
			return (msr - MSR_P4_BPU_PERFCTR0);
	}
	return 0;
}

/* converts an msr to an appropriate reservation bit */
static inline unsigned int nmi_evntsel_msr_to_bit(unsigned int msr)
{
	/* returns the bit offset of the event selection register */
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		return (msr - MSR_K7_EVNTSEL0);
	case X86_VENDOR_INTEL:
		if (cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON))
			return (msr - MSR_ARCH_PERFMON_EVENTSEL0);
		else
			return (msr - MSR_P4_BSU_ESCR0);
	}
	return 0;
}

/* checks for a bit availability (hack for oprofile) */
int avail_to_resrv_perfctr_nmi_bit(unsigned int counter)
{
	int cpu;
	BUG_ON(counter > NMI_MAX_COUNTER_BITS);
	for_each_possible_cpu (cpu) {
		if (test_bit(counter, &per_cpu(perfctr_nmi_owner, cpu)))
			return 0;
	}
	return 1;
}

/* checks the an msr for availability */
int avail_to_resrv_perfctr_nmi(unsigned int msr)
{
	unsigned int counter;
	int cpu;

	counter = nmi_perfctr_msr_to_bit(msr);
	BUG_ON(counter > NMI_MAX_COUNTER_BITS);

	for_each_possible_cpu (cpu) {
		if (test_bit(counter, &per_cpu(perfctr_nmi_owner, cpu)))
			return 0;
	}
	return 1;
}

static int __reserve_perfctr_nmi(int cpu, unsigned int msr)
{
	unsigned int counter;
	if (cpu < 0)
		cpu = smp_processor_id();

	counter = nmi_perfctr_msr_to_bit(msr);
	BUG_ON(counter > NMI_MAX_COUNTER_BITS);

	if (!test_and_set_bit(counter, &per_cpu(perfctr_nmi_owner, cpu)))
		return 1;
	return 0;
}

static void __release_perfctr_nmi(int cpu, unsigned int msr)
{
	unsigned int counter;
	if (cpu < 0)
		cpu = smp_processor_id();

	counter = nmi_perfctr_msr_to_bit(msr);
	BUG_ON(counter > NMI_MAX_COUNTER_BITS);

	clear_bit(counter, &per_cpu(perfctr_nmi_owner, cpu));
}

int reserve_perfctr_nmi(unsigned int msr)
{
	int cpu, i;
	for_each_possible_cpu (cpu) {
		if (!__reserve_perfctr_nmi(cpu, msr)) {
			for_each_possible_cpu (i) {
				if (i >= cpu)
					break;
				__release_perfctr_nmi(i, msr);
			}
			return 0;
		}
	}
	return 1;
}

void release_perfctr_nmi(unsigned int msr)
{
	int cpu;
	for_each_possible_cpu (cpu)
		__release_perfctr_nmi(cpu, msr);
}

int __reserve_evntsel_nmi(int cpu, unsigned int msr)
{
	unsigned int counter;
	if (cpu < 0)
		cpu = smp_processor_id();

	counter = nmi_evntsel_msr_to_bit(msr);
	BUG_ON(counter > NMI_MAX_COUNTER_BITS);

	if (!test_and_set_bit(counter, &per_cpu(evntsel_nmi_owner, cpu)[0]))
		return 1;
	return 0;
}

static void __release_evntsel_nmi(int cpu, unsigned int msr)
{
	unsigned int counter;
	if (cpu < 0)
		cpu = smp_processor_id();

	counter = nmi_evntsel_msr_to_bit(msr);
	BUG_ON(counter > NMI_MAX_COUNTER_BITS);

	clear_bit(counter, &per_cpu(evntsel_nmi_owner, cpu)[0]);
}

int reserve_evntsel_nmi(unsigned int msr)
{
	int cpu, i;
	for_each_possible_cpu (cpu) {
		if (!__reserve_evntsel_nmi(cpu, msr)) {
			for_each_possible_cpu (i) {
				if (i >= cpu)
					break;
				__release_evntsel_nmi(i, msr);
			}
			return 0;
		}
	}
	return 1;
}

void release_evntsel_nmi(unsigned int msr)
{
	int cpu;
	for_each_possible_cpu (cpu) {
		__release_evntsel_nmi(cpu, msr);
	}
}

static __cpuinit inline int nmi_known_cpu(void)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		return boot_cpu_data.x86 == 15 || boot_cpu_data.x86 == 16;
	case X86_VENDOR_INTEL:
		if (cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON))
			return 1;
		else
			return (boot_cpu_data.x86 == 15);
	}
	return 0;
}

/* Run after command line and cpu_init init, but before all other checks */
void nmi_watchdog_default(void)
{
	if (nmi_watchdog != NMI_DEFAULT)
		return;
	nmi_watchdog = NMI_NONE;
}

static int endflag __initdata = 0;

#ifdef CONFIG_SMP
/* The performance counters used by NMI_LOCAL_APIC don't trigger when
 * the CPU is idle. To make sure the NMI watchdog really ticks on all
 * CPUs during the test make them busy.
 */
static __init void nmi_cpu_busy(void *data)
{
	local_irq_enable_in_hardirq();
	/* Intentionally don't use cpu_relax here. This is
	   to make sure that the performance counter really ticks,
	   even if there is a simulator or similar that catches the
	   pause instruction. On a real HT machine this is fine because
	   all other CPUs are busy with "useless" delay loops and don't
	   care if they get somewhat less cycles. */
	while (endflag == 0)
		mb();
}
#endif

static unsigned int adjust_for_32bit_ctr(unsigned int hz)
{
	unsigned int retval = hz;

	/*
	 * On Intel CPUs with ARCH_PERFMON only 32 bits in the counter
	 * are writable, with higher bits sign extending from bit 31.
	 * So, we can only program the counter with 31 bit values and
	 * 32nd bit should be 1, for 33.. to be 1.
	 * Find the appropriate nmi_hz
	 */
 	if ((((u64)cpu_khz * 1000) / retval) > 0x7fffffffULL) {
		retval = ((u64)cpu_khz * 1000) / 0x7fffffffUL + 1;
	}
	return retval;
}

int __init check_nmi_watchdog (void)
{
	int *counts;
	int cpu;

	if ((nmi_watchdog == NMI_NONE) || (nmi_watchdog == NMI_DEFAULT))
		return 0;

	if (!atomic_read(&nmi_active))
		return 0;

	counts = kmalloc(NR_CPUS * sizeof(int), GFP_KERNEL);
	if (!counts)
		return -1;

	printk(KERN_INFO "testing NMI watchdog ... ");

#ifdef CONFIG_SMP
	if (nmi_watchdog == NMI_LOCAL_APIC)
		smp_call_function(nmi_cpu_busy, (void *)&endflag, 0, 0);
#endif

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		counts[cpu] = cpu_pda(cpu)->__nmi_count;
	local_irq_enable();
	mdelay((20*1000)/nmi_hz); // wait 20 ticks

	for_each_online_cpu(cpu) {
		if (!per_cpu(nmi_watchdog_ctlblk, cpu).enabled)
			continue;
		if (cpu_pda(cpu)->__nmi_count - counts[cpu] <= 5) {
			printk("CPU#%d: NMI appears to be stuck (%d->%d)!\n",
			       cpu,
			       counts[cpu],
			       cpu_pda(cpu)->__nmi_count);
			per_cpu(nmi_watchdog_ctlblk, cpu).enabled = 0;
			atomic_dec(&nmi_active);
		}
	}
	if (!atomic_read(&nmi_active)) {
		kfree(counts);
		atomic_set(&nmi_active, -1);
		endflag = 1;
		return -1;
	}
	endflag = 1;
	printk("OK.\n");

	/* now that we know it works we can reduce NMI frequency to
	   something more reasonable; makes a difference in some configs */
	if (nmi_watchdog == NMI_LOCAL_APIC) {
		struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);

		nmi_hz = 1;
	 	if (wd->perfctr_msr == MSR_ARCH_PERFMON_PERFCTR0)
			nmi_hz = adjust_for_32bit_ctr(nmi_hz);
	}

	kfree(counts);
	return 0;
}

int __init setup_nmi_watchdog(char *str)
{
	int nmi;

	if (!strncmp(str,"panic",5)) {
		panic_on_timeout = 1;
		str = strchr(str, ',');
		if (!str)
			return 1;
		++str;
	}

	get_option(&str, &nmi);

	if ((nmi >= NMI_INVALID) || (nmi < NMI_NONE))
		return 0;

	nmi_watchdog = nmi;
	return 1;
}

__setup("nmi_watchdog=", setup_nmi_watchdog);

static void disable_lapic_nmi_watchdog(void)
{
	BUG_ON(nmi_watchdog != NMI_LOCAL_APIC);

	if (atomic_read(&nmi_active) <= 0)
		return;

	on_each_cpu(stop_apic_nmi_watchdog, NULL, 0, 1);

	BUG_ON(atomic_read(&nmi_active) != 0);
}

static void enable_lapic_nmi_watchdog(void)
{
	BUG_ON(nmi_watchdog != NMI_LOCAL_APIC);

	/* are we already enabled */
	if (atomic_read(&nmi_active) != 0)
		return;

	/* are we lapic aware */
	if (nmi_known_cpu() <= 0)
		return;

	on_each_cpu(setup_apic_nmi_watchdog, NULL, 0, 1);
	touch_nmi_watchdog();
}

void disable_timer_nmi_watchdog(void)
{
	BUG_ON(nmi_watchdog != NMI_IO_APIC);

	if (atomic_read(&nmi_active) <= 0)
		return;

	disable_irq(0);
	on_each_cpu(stop_apic_nmi_watchdog, NULL, 0, 1);

	BUG_ON(atomic_read(&nmi_active) != 0);
}

void enable_timer_nmi_watchdog(void)
{
	BUG_ON(nmi_watchdog != NMI_IO_APIC);

	if (atomic_read(&nmi_active) == 0) {
		touch_nmi_watchdog();
		on_each_cpu(setup_apic_nmi_watchdog, NULL, 0, 1);
		enable_irq(0);
	}
}

static void __acpi_nmi_disable(void *__unused)
{
	apic_write(APIC_LVT0, APIC_DM_NMI | APIC_LVT_MASKED);
}

/*
 * Disable timer based NMIs on all CPUs:
 */
void acpi_nmi_disable(void)
{
	if (atomic_read(&nmi_active) && nmi_watchdog == NMI_IO_APIC)
		on_each_cpu(__acpi_nmi_disable, NULL, 0, 1);
}

static void __acpi_nmi_enable(void *__unused)
{
	apic_write(APIC_LVT0, APIC_DM_NMI);
}

/*
 * Enable timer based NMIs on all CPUs:
 */
void acpi_nmi_enable(void)
{
	if (atomic_read(&nmi_active) && nmi_watchdog == NMI_IO_APIC)
		on_each_cpu(__acpi_nmi_enable, NULL, 0, 1);
}
#ifdef CONFIG_PM

static int nmi_pm_active; /* nmi_active before suspend */

static int lapic_nmi_suspend(struct sys_device *dev, pm_message_t state)
{
	/* only CPU0 goes here, other CPUs should be offline */
	nmi_pm_active = atomic_read(&nmi_active);
	stop_apic_nmi_watchdog(NULL);
	BUG_ON(atomic_read(&nmi_active) != 0);
	return 0;
}

static int lapic_nmi_resume(struct sys_device *dev)
{
	/* only CPU0 goes here, other CPUs should be offline */
	if (nmi_pm_active > 0) {
		setup_apic_nmi_watchdog(NULL);
		touch_nmi_watchdog();
	}
	return 0;
}

static struct sysdev_class nmi_sysclass = {
	set_kset_name("lapic_nmi"),
	.resume		= lapic_nmi_resume,
	.suspend	= lapic_nmi_suspend,
};

static struct sys_device device_lapic_nmi = {
	.id		= 0,
	.cls	= &nmi_sysclass,
};

static int __init init_lapic_nmi_sysfs(void)
{
	int error;

	/* should really be a BUG_ON but b/c this is an
	 * init call, it just doesn't work.  -dcz
	 */
	if (nmi_watchdog != NMI_LOCAL_APIC)
		return 0;

	if ( atomic_read(&nmi_active) < 0 )
		return 0;

	error = sysdev_class_register(&nmi_sysclass);
	if (!error)
		error = sysdev_register(&device_lapic_nmi);
	return error;
}
/* must come after the local APIC's device_initcall() */
late_initcall(init_lapic_nmi_sysfs);

#endif	/* CONFIG_PM */

/*
 * Activate the NMI watchdog via the local APIC.
 * Original code written by Keith Owens.
 */

/* Note that these events don't tick when the CPU idles. This means
   the frequency varies with CPU load. */

#define K7_EVNTSEL_ENABLE	(1 << 22)
#define K7_EVNTSEL_INT		(1 << 20)
#define K7_EVNTSEL_OS		(1 << 17)
#define K7_EVNTSEL_USR		(1 << 16)
#define K7_EVENT_CYCLES_PROCESSOR_IS_RUNNING	0x76
#define K7_NMI_EVENT		K7_EVENT_CYCLES_PROCESSOR_IS_RUNNING

static int setup_k7_watchdog(void)
{
	unsigned int perfctr_msr, evntsel_msr;
	unsigned int evntsel;
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);

	perfctr_msr = MSR_K7_PERFCTR0;
	evntsel_msr = MSR_K7_EVNTSEL0;
	if (!__reserve_perfctr_nmi(-1, perfctr_msr))
		goto fail;

	if (!__reserve_evntsel_nmi(-1, evntsel_msr))
		goto fail1;

	/* Simulator may not support it */
	if (checking_wrmsrl(evntsel_msr, 0UL))
		goto fail2;
	wrmsrl(perfctr_msr, 0UL);

	evntsel = K7_EVNTSEL_INT
		| K7_EVNTSEL_OS
		| K7_EVNTSEL_USR
		| K7_NMI_EVENT;

	/* setup the timer */
	wrmsr(evntsel_msr, evntsel, 0);
	wrmsrl(perfctr_msr, -((u64)cpu_khz * 1000 / nmi_hz));
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	evntsel |= K7_EVNTSEL_ENABLE;
	wrmsr(evntsel_msr, evntsel, 0);

	wd->perfctr_msr = perfctr_msr;
	wd->evntsel_msr = evntsel_msr;
	wd->cccr_msr = 0;  //unused
	wd->check_bit = 1ULL<<63;
	return 1;
fail2:
	__release_evntsel_nmi(-1, evntsel_msr);
fail1:
	__release_perfctr_nmi(-1, perfctr_msr);
fail:
	return 0;
}

static void stop_k7_watchdog(void)
{
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);

	wrmsr(wd->evntsel_msr, 0, 0);

	__release_evntsel_nmi(-1, wd->evntsel_msr);
	__release_perfctr_nmi(-1, wd->perfctr_msr);
}

/* Note that these events don't tick when the CPU idles. This means
   the frequency varies with CPU load. */

#define MSR_P4_MISC_ENABLE_PERF_AVAIL	(1<<7)
#define P4_ESCR_EVENT_SELECT(N)	((N)<<25)
#define P4_ESCR_OS		(1<<3)
#define P4_ESCR_USR		(1<<2)
#define P4_CCCR_OVF_PMI0	(1<<26)
#define P4_CCCR_OVF_PMI1	(1<<27)
#define P4_CCCR_THRESHOLD(N)	((N)<<20)
#define P4_CCCR_COMPLEMENT	(1<<19)
#define P4_CCCR_COMPARE		(1<<18)
#define P4_CCCR_REQUIRED	(3<<16)
#define P4_CCCR_ESCR_SELECT(N)	((N)<<13)
#define P4_CCCR_ENABLE		(1<<12)
#define P4_CCCR_OVF 		(1<<31)
/* Set up IQ_COUNTER0 to behave like a clock, by having IQ_CCCR0 filter
   CRU_ESCR0 (with any non-null event selector) through a complemented
   max threshold. [IA32-Vol3, Section 14.9.9] */

static int setup_p4_watchdog(void)
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

	/* performance counters are shared resources
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
	} else {
		/* logical cpu 1 */
		perfctr_msr = MSR_P4_IQ_PERFCTR1;
		evntsel_msr = MSR_P4_CRU_ESCR0;
		cccr_msr = MSR_P4_IQ_CCCR1;
		cccr_val = P4_CCCR_OVF_PMI1 | P4_CCCR_ESCR_SELECT(4);
	}

	if (!__reserve_perfctr_nmi(-1, perfctr_msr))
		goto fail;

	if (!__reserve_evntsel_nmi(-1, evntsel_msr))
		goto fail1;

	evntsel = P4_ESCR_EVENT_SELECT(0x3F)
	 	| P4_ESCR_OS
		| P4_ESCR_USR;

	cccr_val |= P4_CCCR_THRESHOLD(15)
		 | P4_CCCR_COMPLEMENT
		 | P4_CCCR_COMPARE
		 | P4_CCCR_REQUIRED;

	wrmsr(evntsel_msr, evntsel, 0);
	wrmsr(cccr_msr, cccr_val, 0);
	wrmsrl(perfctr_msr, -((u64)cpu_khz * 1000 / nmi_hz));
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	cccr_val |= P4_CCCR_ENABLE;
	wrmsr(cccr_msr, cccr_val, 0);

	wd->perfctr_msr = perfctr_msr;
	wd->evntsel_msr = evntsel_msr;
	wd->cccr_msr = cccr_msr;
	wd->check_bit = 1ULL<<39;
	return 1;
fail1:
	__release_perfctr_nmi(-1, perfctr_msr);
fail:
	return 0;
}

static void stop_p4_watchdog(void)
{
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);

	wrmsr(wd->cccr_msr, 0, 0);
	wrmsr(wd->evntsel_msr, 0, 0);

	__release_evntsel_nmi(-1, wd->evntsel_msr);
	__release_perfctr_nmi(-1, wd->perfctr_msr);
}

#define ARCH_PERFMON_NMI_EVENT_SEL	ARCH_PERFMON_UNHALTED_CORE_CYCLES_SEL
#define ARCH_PERFMON_NMI_EVENT_UMASK	ARCH_PERFMON_UNHALTED_CORE_CYCLES_UMASK

static int setup_intel_arch_watchdog(void)
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
		goto fail;

	perfctr_msr = MSR_ARCH_PERFMON_PERFCTR0;
	evntsel_msr = MSR_ARCH_PERFMON_EVENTSEL0;

	if (!__reserve_perfctr_nmi(-1, perfctr_msr))
		goto fail;

	if (!__reserve_evntsel_nmi(-1, evntsel_msr))
		goto fail1;

	wrmsrl(perfctr_msr, 0UL);

	evntsel = ARCH_PERFMON_EVENTSEL_INT
		| ARCH_PERFMON_EVENTSEL_OS
		| ARCH_PERFMON_EVENTSEL_USR
		| ARCH_PERFMON_NMI_EVENT_SEL
		| ARCH_PERFMON_NMI_EVENT_UMASK;

	/* setup the timer */
	wrmsr(evntsel_msr, evntsel, 0);

	nmi_hz = adjust_for_32bit_ctr(nmi_hz);
	wrmsr(perfctr_msr, (u32)(-((u64)cpu_khz * 1000 / nmi_hz)), 0);

	apic_write(APIC_LVTPC, APIC_DM_NMI);
	evntsel |= ARCH_PERFMON_EVENTSEL0_ENABLE;
	wrmsr(evntsel_msr, evntsel, 0);

	wd->perfctr_msr = perfctr_msr;
	wd->evntsel_msr = evntsel_msr;
	wd->cccr_msr = 0;  //unused
	wd->check_bit = 1ULL << (eax.split.bit_width - 1);
	return 1;
fail1:
	__release_perfctr_nmi(-1, perfctr_msr);
fail:
	return 0;
}

static void stop_intel_arch_watchdog(void)
{
	unsigned int ebx;
	union cpuid10_eax eax;
	unsigned int unused;
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);

	/*
	 * Check whether the Architectural PerfMon supports
	 * Unhalted Core Cycles Event or not.
	 * NOTE: Corresponding bit = 0 in ebx indicates event present.
	 */
	cpuid(10, &(eax.full), &ebx, &unused, &unused);
	if ((eax.split.mask_length < (ARCH_PERFMON_UNHALTED_CORE_CYCLES_INDEX+1)) ||
	    (ebx & ARCH_PERFMON_UNHALTED_CORE_CYCLES_PRESENT))
		return;

	wrmsr(wd->evntsel_msr, 0, 0);

	__release_evntsel_nmi(-1, wd->evntsel_msr);
	__release_perfctr_nmi(-1, wd->perfctr_msr);
}

void setup_apic_nmi_watchdog(void *unused)
{
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);

	/* only support LOCAL and IO APICs for now */
	if ((nmi_watchdog != NMI_LOCAL_APIC) &&
	    (nmi_watchdog != NMI_IO_APIC))
	    	return;

	if (wd->enabled == 1)
		return;

	/* cheap hack to support suspend/resume */
	/* if cpu0 is not active neither should the other cpus */
	if ((smp_processor_id() != 0) && (atomic_read(&nmi_active) <= 0))
		return;

	if (nmi_watchdog == NMI_LOCAL_APIC) {
		switch (boot_cpu_data.x86_vendor) {
		case X86_VENDOR_AMD:
			if (strstr(boot_cpu_data.x86_model_id, "Screwdriver"))
				return;
			if (!setup_k7_watchdog())
				return;
			break;
		case X86_VENDOR_INTEL:
			if (cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON)) {
				if (!setup_intel_arch_watchdog())
					return;
				break;
			}
			if (!setup_p4_watchdog())
				return;
			break;
		default:
			return;
		}
	}
	wd->enabled = 1;
	atomic_inc(&nmi_active);
}

void stop_apic_nmi_watchdog(void *unused)
{
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);

	/* only support LOCAL and IO APICs for now */
	if ((nmi_watchdog != NMI_LOCAL_APIC) &&
	    (nmi_watchdog != NMI_IO_APIC))
	    	return;

	if (wd->enabled == 0)
		return;

	if (nmi_watchdog == NMI_LOCAL_APIC) {
		switch (boot_cpu_data.x86_vendor) {
		case X86_VENDOR_AMD:
			if (strstr(boot_cpu_data.x86_model_id, "Screwdriver"))
				return;
			stop_k7_watchdog();
			break;
		case X86_VENDOR_INTEL:
			if (cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON)) {
				stop_intel_arch_watchdog();
				break;
			}
			stop_p4_watchdog();
			break;
		default:
			return;
		}
	}
	wd->enabled = 0;
	atomic_dec(&nmi_active);
}

/*
 * the best way to detect whether a CPU has a 'hard lockup' problem
 * is to check it's local APIC timer IRQ counts. If they are not
 * changing then that CPU has some problem.
 *
 * as these watchdog NMI IRQs are generated on every CPU, we only
 * have to check the current processor.
 */

static DEFINE_PER_CPU(unsigned, last_irq_sum);
static DEFINE_PER_CPU(local_t, alert_counter);
static DEFINE_PER_CPU(int, nmi_touch);

void touch_nmi_watchdog (void)
{
	if (nmi_watchdog > 0) {
		unsigned cpu;

		/*
 		 * Tell other CPUs to reset their alert counters. We cannot
		 * do it ourselves because the alert count increase is not
		 * atomic.
		 */
		for_each_present_cpu (cpu)
			per_cpu(nmi_touch, cpu) = 1;
	}

 	touch_softlockup_watchdog();
}

int __kprobes nmi_watchdog_tick(struct pt_regs * regs, unsigned reason)
{
	int sum;
	int touched = 0;
	int cpu = smp_processor_id();
	struct nmi_watchdog_ctlblk *wd = &__get_cpu_var(nmi_watchdog_ctlblk);
	u64 dummy;
	int rc=0;

	/* check for other users first */
	if (notify_die(DIE_NMI, "nmi", regs, reason, 2, SIGINT)
			== NOTIFY_STOP) {
		rc = 1;
		touched = 1;
	}

	sum = read_pda(apic_timer_irqs);
	if (__get_cpu_var(nmi_touch)) {
		__get_cpu_var(nmi_touch) = 0;
		touched = 1;
	}

	if (cpu_isset(cpu, backtrace_mask)) {
		static DEFINE_SPINLOCK(lock);	/* Serialise the printks */

		spin_lock(&lock);
		printk("NMI backtrace for cpu %d\n", cpu);
		dump_stack();
		spin_unlock(&lock);
		cpu_clear(cpu, backtrace_mask);
	}

#ifdef CONFIG_X86_MCE
	/* Could check oops_in_progress here too, but it's safer
	   not too */
	if (atomic_read(&mce_entry) > 0)
		touched = 1;
#endif
	/* if the apic timer isn't firing, this cpu isn't doing much */
	if (!touched && __get_cpu_var(last_irq_sum) == sum) {
		/*
		 * Ayiee, looks like this CPU is stuck ...
		 * wait a few IRQs (5 seconds) before doing the oops ...
		 */
		local_inc(&__get_cpu_var(alert_counter));
		if (local_read(&__get_cpu_var(alert_counter)) == 5*nmi_hz)
			die_nmi("NMI Watchdog detected LOCKUP on CPU %d\n", regs,
				panic_on_timeout);
	} else {
		__get_cpu_var(last_irq_sum) = sum;
		local_set(&__get_cpu_var(alert_counter), 0);
	}

	/* see if the nmi watchdog went off */
	if (wd->enabled) {
		if (nmi_watchdog == NMI_LOCAL_APIC) {
			rdmsrl(wd->perfctr_msr, dummy);
			if (dummy & wd->check_bit){
				/* this wasn't a watchdog timer interrupt */
				goto done;
			}

			/* only Intel uses the cccr msr */
	 		if (wd->cccr_msr != 0) {
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
				wrmsrl(wd->perfctr_msr,
				       -((u64)cpu_khz * 1000 / nmi_hz));
	 		} else if (wd->perfctr_msr == MSR_ARCH_PERFMON_PERFCTR0) {
				/*
				 * ArchPerfom/Core Duo needs to re-unmask
				 * the apic vector
				 */
				apic_write(APIC_LVTPC, APIC_DM_NMI);
				/* ARCH_PERFMON has 32 bit counter writes */
				wrmsr(wd->perfctr_msr,
				     (u32)(-((u64)cpu_khz * 1000 / nmi_hz)), 0);
			} else {
				/* start the cycle over again */
				wrmsrl(wd->perfctr_msr,
				       -((u64)cpu_khz * 1000 / nmi_hz));
			}
			rc = 1;
		} else 	if (nmi_watchdog == NMI_IO_APIC) {
			/* don't know how to accurately check for this.
			 * just assume it was a watchdog timer interrupt
			 * This matches the old behaviour.
			 */
			rc = 1;
		} else
			printk(KERN_WARNING "Unknown enabled NMI hardware?!\n");
	}
done:
	return rc;
}

asmlinkage __kprobes void do_nmi(struct pt_regs * regs, long error_code)
{
	nmi_enter();
	add_pda(__nmi_count,1);
	default_do_nmi(regs);
	nmi_exit();
}

int do_nmi_callback(struct pt_regs * regs, int cpu)
{
#ifdef CONFIG_SYSCTL
	if (unknown_nmi_panic)
		return unknown_nmi_panic_callback(regs, cpu);
#endif
	return 0;
}

#ifdef CONFIG_SYSCTL

static int unknown_nmi_panic_callback(struct pt_regs *regs, int cpu)
{
	unsigned char reason = get_nmi_reason();
	char buf[64];

	sprintf(buf, "NMI received for unknown reason %02x\n", reason);
	die_nmi(buf, regs, 1);	/* Always panic here */
	return 0;
}

/*
 * proc handler for /proc/sys/kernel/nmi
 */
int proc_nmi_enabled(struct ctl_table *table, int write, struct file *file,
			void __user *buffer, size_t *length, loff_t *ppos)
{
	int old_state;

	nmi_watchdog_enabled = (atomic_read(&nmi_active) > 0) ? 1 : 0;
	old_state = nmi_watchdog_enabled;
	proc_dointvec(table, write, file, buffer, length, ppos);
	if (!!old_state == !!nmi_watchdog_enabled)
		return 0;

	if (atomic_read(&nmi_active) < 0) {
		printk( KERN_WARNING "NMI watchdog is permanently disabled\n");
		return -EIO;
	}

	/* if nmi_watchdog is not set yet, then set it */
	nmi_watchdog_default();

	if (nmi_watchdog == NMI_LOCAL_APIC) {
		if (nmi_watchdog_enabled)
			enable_lapic_nmi_watchdog();
		else
			disable_lapic_nmi_watchdog();
	} else {
		printk( KERN_WARNING
			"NMI watchdog doesn't know what hardware to touch\n");
		return -EIO;
	}
	return 0;
}

#endif

void __trigger_all_cpu_backtrace(void)
{
	int i;

	backtrace_mask = cpu_online_map;
	/* Wait for up to 10 seconds for all CPUs to do the backtrace */
	for (i = 0; i < 10 * 1000; i++) {
		if (cpus_empty(backtrace_mask))
			break;
		mdelay(1);
	}
}

EXPORT_SYMBOL(nmi_active);
EXPORT_SYMBOL(nmi_watchdog);
EXPORT_SYMBOL(avail_to_resrv_perfctr_nmi);
EXPORT_SYMBOL(avail_to_resrv_perfctr_nmi_bit);
EXPORT_SYMBOL(reserve_perfctr_nmi);
EXPORT_SYMBOL(release_perfctr_nmi);
EXPORT_SYMBOL(reserve_evntsel_nmi);
EXPORT_SYMBOL(release_evntsel_nmi);
EXPORT_SYMBOL(disable_timer_nmi_watchdog);
EXPORT_SYMBOL(enable_timer_nmi_watchdog);
EXPORT_SYMBOL(touch_nmi_watchdog);
