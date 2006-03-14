/*
 *  linux/arch/i386/nmi.c
 *
 *  NMI watchdog support on APIC systems
 *
 *  Started by Ingo Molnar <mingo@redhat.com>
 *
 *  Fixes:
 *  Mikael Pettersson	: AMD K7 support for local APIC NMI watchdog.
 *  Mikael Pettersson	: Power Management for local APIC NMI watchdog.
 *  Mikael Pettersson	: Pentium 4 support for local APIC NMI watchdog.
 *  Pavel Machek and
 *  Mikael Pettersson	: PM converted to driver model. Disable/enable API.
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/nmi.h>
#include <linux/sysdev.h>
#include <linux/sysctl.h>

#include <asm/smp.h>
#include <asm/div64.h>
#include <asm/nmi.h>

#include "mach_traps.h"

unsigned int nmi_watchdog = NMI_NONE;
extern int unknown_nmi_panic;
static unsigned int nmi_hz = HZ;
static unsigned int nmi_perfctr_msr;	/* the MSR to reset in NMI handler */
static unsigned int nmi_p4_cccr_val;
extern void show_registers(struct pt_regs *regs);

/*
 * lapic_nmi_owner tracks the ownership of the lapic NMI hardware:
 * - it may be reserved by some other driver, or not
 * - when not reserved by some other driver, it may be used for
 *   the NMI watchdog, or not
 *
 * This is maintained separately from nmi_active because the NMI
 * watchdog may also be driven from the I/O APIC timer.
 */
static DEFINE_SPINLOCK(lapic_nmi_owner_lock);
static unsigned int lapic_nmi_owner;
#define LAPIC_NMI_WATCHDOG	(1<<0)
#define LAPIC_NMI_RESERVED	(1<<1)

/* nmi_active:
 * +1: the lapic NMI watchdog is active, but can be disabled
 *  0: the lapic NMI watchdog has not been set up, and cannot
 *     be enabled
 * -1: the lapic NMI watchdog is disabled, but can be enabled
 */
int nmi_active;

#define K7_EVNTSEL_ENABLE	(1 << 22)
#define K7_EVNTSEL_INT		(1 << 20)
#define K7_EVNTSEL_OS		(1 << 17)
#define K7_EVNTSEL_USR		(1 << 16)
#define K7_EVENT_CYCLES_PROCESSOR_IS_RUNNING	0x76
#define K7_NMI_EVENT		K7_EVENT_CYCLES_PROCESSOR_IS_RUNNING

#define P6_EVNTSEL0_ENABLE	(1 << 22)
#define P6_EVNTSEL_INT		(1 << 20)
#define P6_EVNTSEL_OS		(1 << 17)
#define P6_EVNTSEL_USR		(1 << 16)
#define P6_EVENT_CPU_CLOCKS_NOT_HALTED	0x79
#define P6_NMI_EVENT		P6_EVENT_CPU_CLOCKS_NOT_HALTED

#define MSR_P4_MISC_ENABLE	0x1A0
#define MSR_P4_MISC_ENABLE_PERF_AVAIL	(1<<7)
#define MSR_P4_MISC_ENABLE_PEBS_UNAVAIL	(1<<12)
#define MSR_P4_PERFCTR0		0x300
#define MSR_P4_CCCR0		0x360
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
/* Set up IQ_COUNTER0 to behave like a clock, by having IQ_CCCR0 filter
   CRU_ESCR0 (with any non-null event selector) through a complemented
   max threshold. [IA32-Vol3, Section 14.9.9] */
#define MSR_P4_IQ_COUNTER0	0x30C
#define P4_NMI_CRU_ESCR0	(P4_ESCR_EVENT_SELECT(0x3F)|P4_ESCR_OS|P4_ESCR_USR)
#define P4_NMI_IQ_CCCR0	\
	(P4_CCCR_OVF_PMI0|P4_CCCR_THRESHOLD(15)|P4_CCCR_COMPLEMENT|	\
	 P4_CCCR_COMPARE|P4_CCCR_REQUIRED|P4_CCCR_ESCR_SELECT(4)|P4_CCCR_ENABLE)

#ifdef CONFIG_SMP
/* The performance counters used by NMI_LOCAL_APIC don't trigger when
 * the CPU is idle. To make sure the NMI watchdog really ticks on all
 * CPUs during the test make them busy.
 */
static __init void nmi_cpu_busy(void *data)
{
	volatile int *endflag = data;
	local_irq_enable();
	/* Intentionally don't use cpu_relax here. This is
	   to make sure that the performance counter really ticks,
	   even if there is a simulator or similar that catches the
	   pause instruction. On a real HT machine this is fine because
	   all other CPUs are busy with "useless" delay loops and don't
	   care if they get somewhat less cycles. */
	while (*endflag == 0)
		barrier();
}
#endif

static int __init check_nmi_watchdog(void)
{
	volatile int endflag = 0;
	unsigned int *prev_nmi_count;
	int cpu;

	if (nmi_watchdog == NMI_NONE)
		return 0;

	prev_nmi_count = kmalloc(NR_CPUS * sizeof(int), GFP_KERNEL);
	if (!prev_nmi_count)
		return -1;

	printk(KERN_INFO "Testing NMI watchdog ... ");

	if (nmi_watchdog == NMI_LOCAL_APIC)
		smp_call_function(nmi_cpu_busy, (void *)&endflag, 0, 0);

	for_each_cpu(cpu)
		prev_nmi_count[cpu] = per_cpu(irq_stat, cpu).__nmi_count;
	local_irq_enable();
	mdelay((10*1000)/nmi_hz); // wait 10 ticks

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
#ifdef CONFIG_SMP
		/* Check cpu_callin_map here because that is set
		   after the timer is started. */
		if (!cpu_isset(cpu, cpu_callin_map))
			continue;
#endif
		if (nmi_count(cpu) - prev_nmi_count[cpu] <= 5) {
			endflag = 1;
			printk("CPU#%d: NMI appears to be stuck (%d->%d)!\n",
				cpu,
				prev_nmi_count[cpu],
				nmi_count(cpu));
			nmi_active = 0;
			lapic_nmi_owner &= ~LAPIC_NMI_WATCHDOG;
			kfree(prev_nmi_count);
			return -1;
		}
	}
	endflag = 1;
	printk("OK.\n");

	/* now that we know it works we can reduce NMI frequency to
	   something more reasonable; makes a difference in some configs */
	if (nmi_watchdog == NMI_LOCAL_APIC)
		nmi_hz = 1;

	kfree(prev_nmi_count);
	return 0;
}
/* This needs to happen later in boot so counters are working */
late_initcall(check_nmi_watchdog);

static int __init setup_nmi_watchdog(char *str)
{
	int nmi;

	get_option(&str, &nmi);

	if (nmi >= NMI_INVALID)
		return 0;
	if (nmi == NMI_NONE)
		nmi_watchdog = nmi;
	/*
	 * If any other x86 CPU has a local APIC, then
	 * please test the NMI stuff there and send me the
	 * missing bits. Right now Intel P6/P4 and AMD K7 only.
	 */
	if ((nmi == NMI_LOCAL_APIC) &&
			(boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) &&
			(boot_cpu_data.x86 == 6 || boot_cpu_data.x86 == 15))
		nmi_watchdog = nmi;
	if ((nmi == NMI_LOCAL_APIC) &&
			(boot_cpu_data.x86_vendor == X86_VENDOR_AMD) &&
	  		(boot_cpu_data.x86 == 6 || boot_cpu_data.x86 == 15))
		nmi_watchdog = nmi;
	/*
	 * We can enable the IO-APIC watchdog
	 * unconditionally.
	 */
	if (nmi == NMI_IO_APIC) {
		nmi_active = 1;
		nmi_watchdog = nmi;
	}
	return 1;
}

__setup("nmi_watchdog=", setup_nmi_watchdog);

static void disable_lapic_nmi_watchdog(void)
{
	if (nmi_active <= 0)
		return;
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		wrmsr(MSR_K7_EVNTSEL0, 0, 0);
		break;
	case X86_VENDOR_INTEL:
		switch (boot_cpu_data.x86) {
		case 6:
			if (boot_cpu_data.x86_model > 0xd)
				break;

			wrmsr(MSR_P6_EVNTSEL0, 0, 0);
			break;
		case 15:
			if (boot_cpu_data.x86_model > 0x4)
				break;

			wrmsr(MSR_P4_IQ_CCCR0, 0, 0);
			wrmsr(MSR_P4_CRU_ESCR0, 0, 0);
			break;
		}
		break;
	}
	nmi_active = -1;
	/* tell do_nmi() and others that we're not active any more */
	nmi_watchdog = 0;
}

static void enable_lapic_nmi_watchdog(void)
{
	if (nmi_active < 0) {
		nmi_watchdog = NMI_LOCAL_APIC;
		setup_apic_nmi_watchdog();
	}
}

int reserve_lapic_nmi(void)
{
	unsigned int old_owner;

	spin_lock(&lapic_nmi_owner_lock);
	old_owner = lapic_nmi_owner;
	lapic_nmi_owner |= LAPIC_NMI_RESERVED;
	spin_unlock(&lapic_nmi_owner_lock);
	if (old_owner & LAPIC_NMI_RESERVED)
		return -EBUSY;
	if (old_owner & LAPIC_NMI_WATCHDOG)
		disable_lapic_nmi_watchdog();
	return 0;
}

void release_lapic_nmi(void)
{
	unsigned int new_owner;

	spin_lock(&lapic_nmi_owner_lock);
	new_owner = lapic_nmi_owner & ~LAPIC_NMI_RESERVED;
	lapic_nmi_owner = new_owner;
	spin_unlock(&lapic_nmi_owner_lock);
	if (new_owner & LAPIC_NMI_WATCHDOG)
		enable_lapic_nmi_watchdog();
}

void disable_timer_nmi_watchdog(void)
{
	if ((nmi_watchdog != NMI_IO_APIC) || (nmi_active <= 0))
		return;

	unset_nmi_callback();
	nmi_active = -1;
	nmi_watchdog = NMI_NONE;
}

void enable_timer_nmi_watchdog(void)
{
	if (nmi_active < 0) {
		nmi_watchdog = NMI_IO_APIC;
		touch_nmi_watchdog();
		nmi_active = 1;
	}
}

#ifdef CONFIG_PM

static int nmi_pm_active; /* nmi_active before suspend */

static int lapic_nmi_suspend(struct sys_device *dev, pm_message_t state)
{
	nmi_pm_active = nmi_active;
	disable_lapic_nmi_watchdog();
	return 0;
}

static int lapic_nmi_resume(struct sys_device *dev)
{
	if (nmi_pm_active > 0)
		enable_lapic_nmi_watchdog();
	return 0;
}


static struct sysdev_class nmi_sysclass = {
	set_kset_name("lapic_nmi"),
	.resume		= lapic_nmi_resume,
	.suspend	= lapic_nmi_suspend,
};

static struct sys_device device_lapic_nmi = {
	.id	= 0,
	.cls	= &nmi_sysclass,
};

static int __init init_lapic_nmi_sysfs(void)
{
	int error;

	if (nmi_active == 0 || nmi_watchdog != NMI_LOCAL_APIC)
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

static void clear_msr_range(unsigned int base, unsigned int n)
{
	unsigned int i;

	for(i = 0; i < n; ++i)
		wrmsr(base+i, 0, 0);
}

static void write_watchdog_counter(const char *descr)
{
	u64 count = (u64)cpu_khz * 1000;

	do_div(count, nmi_hz);
	if(descr)
		Dprintk("setting %s to -0x%08Lx\n", descr, count);
	wrmsrl(nmi_perfctr_msr, 0 - count);
}

static void setup_k7_watchdog(void)
{
	unsigned int evntsel;

	nmi_perfctr_msr = MSR_K7_PERFCTR0;

	clear_msr_range(MSR_K7_EVNTSEL0, 4);
	clear_msr_range(MSR_K7_PERFCTR0, 4);

	evntsel = K7_EVNTSEL_INT
		| K7_EVNTSEL_OS
		| K7_EVNTSEL_USR
		| K7_NMI_EVENT;

	wrmsr(MSR_K7_EVNTSEL0, evntsel, 0);
	write_watchdog_counter("K7_PERFCTR0");
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	evntsel |= K7_EVNTSEL_ENABLE;
	wrmsr(MSR_K7_EVNTSEL0, evntsel, 0);
}

static void setup_p6_watchdog(void)
{
	unsigned int evntsel;

	nmi_perfctr_msr = MSR_P6_PERFCTR0;

	clear_msr_range(MSR_P6_EVNTSEL0, 2);
	clear_msr_range(MSR_P6_PERFCTR0, 2);

	evntsel = P6_EVNTSEL_INT
		| P6_EVNTSEL_OS
		| P6_EVNTSEL_USR
		| P6_NMI_EVENT;

	wrmsr(MSR_P6_EVNTSEL0, evntsel, 0);
	write_watchdog_counter("P6_PERFCTR0");
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	evntsel |= P6_EVNTSEL0_ENABLE;
	wrmsr(MSR_P6_EVNTSEL0, evntsel, 0);
}

static int setup_p4_watchdog(void)
{
	unsigned int misc_enable, dummy;

	rdmsr(MSR_P4_MISC_ENABLE, misc_enable, dummy);
	if (!(misc_enable & MSR_P4_MISC_ENABLE_PERF_AVAIL))
		return 0;

	nmi_perfctr_msr = MSR_P4_IQ_COUNTER0;
	nmi_p4_cccr_val = P4_NMI_IQ_CCCR0;
#ifdef CONFIG_SMP
	if (smp_num_siblings == 2)
		nmi_p4_cccr_val |= P4_CCCR_OVF_PMI1;
#endif

	if (!(misc_enable & MSR_P4_MISC_ENABLE_PEBS_UNAVAIL))
		clear_msr_range(0x3F1, 2);
	/* MSR 0x3F0 seems to have a default value of 0xFC00, but current
	   docs doesn't fully define it, so leave it alone for now. */
	if (boot_cpu_data.x86_model >= 0x3) {
		/* MSR_P4_IQ_ESCR0/1 (0x3ba/0x3bb) removed */
		clear_msr_range(0x3A0, 26);
		clear_msr_range(0x3BC, 3);
	} else {
		clear_msr_range(0x3A0, 31);
	}
	clear_msr_range(0x3C0, 6);
	clear_msr_range(0x3C8, 6);
	clear_msr_range(0x3E0, 2);
	clear_msr_range(MSR_P4_CCCR0, 18);
	clear_msr_range(MSR_P4_PERFCTR0, 18);

	wrmsr(MSR_P4_CRU_ESCR0, P4_NMI_CRU_ESCR0, 0);
	wrmsr(MSR_P4_IQ_CCCR0, P4_NMI_IQ_CCCR0 & ~P4_CCCR_ENABLE, 0);
	write_watchdog_counter("P4_IQ_COUNTER0");
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	wrmsr(MSR_P4_IQ_CCCR0, nmi_p4_cccr_val, 0);
	return 1;
}

void setup_apic_nmi_watchdog (void)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		if (boot_cpu_data.x86 != 6 && boot_cpu_data.x86 != 15)
			return;
		setup_k7_watchdog();
		break;
	case X86_VENDOR_INTEL:
		switch (boot_cpu_data.x86) {
		case 6:
			if (boot_cpu_data.x86_model > 0xd)
				return;

			setup_p6_watchdog();
			break;
		case 15:
			if (boot_cpu_data.x86_model > 0x4)
				return;

			if (!setup_p4_watchdog())
				return;
			break;
		default:
			return;
		}
		break;
	default:
		return;
	}
	lapic_nmi_owner = LAPIC_NMI_WATCHDOG;
	nmi_active = 1;
}

/*
 * the best way to detect whether a CPU has a 'hard lockup' problem
 * is to check it's local APIC timer IRQ counts. If they are not
 * changing then that CPU has some problem.
 *
 * as these watchdog NMI IRQs are generated on every CPU, we only
 * have to check the current processor.
 *
 * since NMIs don't listen to _any_ locks, we have to be extremely
 * careful not to rely on unsafe variables. The printk might lock
 * up though, so we have to break up any console locks first ...
 * [when there will be more tty-related locks, break them up
 *  here too!]
 */

static unsigned int
	last_irq_sums [NR_CPUS],
	alert_counter [NR_CPUS];

void touch_nmi_watchdog (void)
{
	int i;

	/*
	 * Just reset the alert counters, (other CPUs might be
	 * spinning on locks we hold):
	 */
	for (i = 0; i < NR_CPUS; i++)
		alert_counter[i] = 0;

	/*
	 * Tickle the softlockup detector too:
	 */
	touch_softlockup_watchdog();
}

extern void die_nmi(struct pt_regs *, const char *msg);

void nmi_watchdog_tick (struct pt_regs * regs)
{

	/*
	 * Since current_thread_info()-> is always on the stack, and we
	 * always switch the stack NMI-atomically, it's safe to use
	 * smp_processor_id().
	 */
	int sum, cpu = smp_processor_id();

	sum = per_cpu(irq_stat, cpu).apic_timer_irqs;

	if (last_irq_sums[cpu] == sum) {
		/*
		 * Ayiee, looks like this CPU is stuck ...
		 * wait a few IRQs (5 seconds) before doing the oops ...
		 */
		alert_counter[cpu]++;
		if (alert_counter[cpu] == 5*nmi_hz)
			/*
			 * die_nmi will return ONLY if NOTIFY_STOP happens..
			 */
			die_nmi(regs, "NMI Watchdog detected LOCKUP");
	} else {
		last_irq_sums[cpu] = sum;
		alert_counter[cpu] = 0;
	}
	if (nmi_perfctr_msr) {
		if (nmi_perfctr_msr == MSR_P4_IQ_COUNTER0) {
			/*
			 * P4 quirks:
			 * - An overflown perfctr will assert its interrupt
			 *   until the OVF flag in its CCCR is cleared.
			 * - LVTPC is masked on interrupt and must be
			 *   unmasked by the LVTPC handler.
			 */
			wrmsr(MSR_P4_IQ_CCCR0, nmi_p4_cccr_val, 0);
			apic_write(APIC_LVTPC, APIC_DM_NMI);
		}
		else if (nmi_perfctr_msr == MSR_P6_PERFCTR0) {
			/* Only P6 based Pentium M need to re-unmask
			 * the apic vector but it doesn't hurt
			 * other P6 variant */
			apic_write(APIC_LVTPC, APIC_DM_NMI);
		}
		write_watchdog_counter(NULL);
	}
}

#ifdef CONFIG_SYSCTL

static int unknown_nmi_panic_callback(struct pt_regs *regs, int cpu)
{
	unsigned char reason = get_nmi_reason();
	char buf[64];

	if (!(reason & 0xc0)) {
		sprintf(buf, "NMI received for unknown reason %02x\n", reason);
		die_nmi(regs, buf);
	}
	return 0;
}

/*
 * proc handler for /proc/sys/kernel/unknown_nmi_panic
 */
int proc_unknown_nmi_panic(ctl_table *table, int write, struct file *file,
			void __user *buffer, size_t *length, loff_t *ppos)
{
	int old_state;

	old_state = unknown_nmi_panic;
	proc_dointvec(table, write, file, buffer, length, ppos);
	if (!!old_state == !!unknown_nmi_panic)
		return 0;

	if (unknown_nmi_panic) {
		if (reserve_lapic_nmi() < 0) {
			unknown_nmi_panic = 0;
			return -EBUSY;
		} else {
			set_nmi_callback(unknown_nmi_panic_callback);
		}
	} else {
		release_lapic_nmi();
		unset_nmi_callback();
	}
	return 0;
}

#endif

EXPORT_SYMBOL(nmi_active);
EXPORT_SYMBOL(nmi_watchdog);
EXPORT_SYMBOL(reserve_lapic_nmi);
EXPORT_SYMBOL(release_lapic_nmi);
EXPORT_SYMBOL(disable_timer_nmi_watchdog);
EXPORT_SYMBOL(enable_timer_nmi_watchdog);
