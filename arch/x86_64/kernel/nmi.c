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

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/sysdev.h>
#include <linux/nmi.h>
#include <linux/sysctl.h>
#include <linux/kprobes.h>

#include <asm/smp.h>
#include <asm/mtrr.h>
#include <asm/mpspec.h>
#include <asm/nmi.h>
#include <asm/msr.h>
#include <asm/proto.h>
#include <asm/kdebug.h>
#include <asm/local.h>

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
int nmi_active;		/* oprofile uses this */
int panic_on_timeout;

unsigned int nmi_watchdog = NMI_DEFAULT;
static unsigned int nmi_hz = HZ;
static unsigned int nmi_perfctr_msr;	/* the MSR to reset in NMI handler */
static unsigned int nmi_p4_cccr_val;

/* Note that these events don't tick when the CPU idles. This means
   the frequency varies with CPU load. */

#define K7_EVNTSEL_ENABLE	(1 << 22)
#define K7_EVNTSEL_INT		(1 << 20)
#define K7_EVNTSEL_OS		(1 << 17)
#define K7_EVNTSEL_USR		(1 << 16)
#define K7_EVENT_CYCLES_PROCESSOR_IS_RUNNING	0x76
#define K7_NMI_EVENT		K7_EVENT_CYCLES_PROCESSOR_IS_RUNNING

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

static __cpuinit inline int nmi_known_cpu(void)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		return boot_cpu_data.x86 == 15;
	case X86_VENDOR_INTEL:
		return boot_cpu_data.x86 == 15;
	}
	return 0;
}

/* Run after command line and cpu_init init, but before all other checks */
void __cpuinit nmi_watchdog_default(void)
{
	if (nmi_watchdog != NMI_DEFAULT)
		return;
	if (nmi_known_cpu())
		nmi_watchdog = NMI_LOCAL_APIC;
	else
		nmi_watchdog = NMI_IO_APIC;
}

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

int __init check_nmi_watchdog (void)
{
	volatile int endflag = 0;
	int *counts;
	int cpu;

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
	mdelay((10*1000)/nmi_hz); // wait 10 ticks

	for_each_online_cpu(cpu) {
		if (cpu_pda(cpu)->__nmi_count - counts[cpu] <= 5) {
			endflag = 1;
			printk("CPU#%d: NMI appears to be stuck (%d->%d)!\n",
			       cpu,
			       counts[cpu],
			       cpu_pda(cpu)->__nmi_count);
			nmi_active = 0;
			lapic_nmi_owner &= ~LAPIC_NMI_WATCHDOG;
			nmi_perfctr_msr = 0;
			kfree(counts);
			return -1;
		}
	}
	endflag = 1;
	printk("OK.\n");

	/* now that we know it works we can reduce NMI frequency to
	   something more reasonable; makes a difference in some configs */
	if (nmi_watchdog == NMI_LOCAL_APIC)
		nmi_hz = 1;

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

	if (nmi >= NMI_INVALID)
		return 0;
	nmi_watchdog = nmi;
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
		if (boot_cpu_data.x86 == 15) {
			wrmsr(MSR_P4_IQ_CCCR0, 0, 0);
			wrmsr(MSR_P4_CRU_ESCR0, 0, 0);
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
		touch_nmi_watchdog();
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

	disable_irq(0);
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
		enable_irq(0);
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
	.id		= 0,
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

static void setup_k7_watchdog(void)
{
	int i;
	unsigned int evntsel;

	nmi_perfctr_msr = MSR_K7_PERFCTR0;

	for(i = 0; i < 4; ++i) {
		/* Simulator may not support it */
		if (checking_wrmsrl(MSR_K7_EVNTSEL0+i, 0UL)) {
			nmi_perfctr_msr = 0;
			return;
		}
		wrmsrl(MSR_K7_PERFCTR0+i, 0UL);
	}

	evntsel = K7_EVNTSEL_INT
		| K7_EVNTSEL_OS
		| K7_EVNTSEL_USR
		| K7_NMI_EVENT;

	wrmsr(MSR_K7_EVNTSEL0, evntsel, 0);
	wrmsrl(MSR_K7_PERFCTR0, -((u64)cpu_khz * 1000 / nmi_hz));
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	evntsel |= K7_EVNTSEL_ENABLE;
	wrmsr(MSR_K7_EVNTSEL0, evntsel, 0);
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
	Dprintk("setting P4_IQ_COUNTER0 to 0x%08lx\n", -(cpu_khz * 1000UL / nmi_hz));
	wrmsrl(MSR_P4_IQ_COUNTER0, -((u64)cpu_khz * 1000 / nmi_hz));
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	wrmsr(MSR_P4_IQ_CCCR0, nmi_p4_cccr_val, 0);
	return 1;
}

void setup_apic_nmi_watchdog(void)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		if (boot_cpu_data.x86 != 15)
			return;
		if (strstr(boot_cpu_data.x86_model_id, "Screwdriver"))
			return;
		setup_k7_watchdog();
		break;
	case X86_VENDOR_INTEL:
		if (boot_cpu_data.x86 != 15)
			return;
		if (!setup_p4_watchdog())
			return;
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

void __kprobes nmi_watchdog_tick(struct pt_regs * regs, unsigned reason)
{
	int sum;
	int touched = 0;

	sum = read_pda(apic_timer_irqs);
	if (__get_cpu_var(nmi_touch)) {
		__get_cpu_var(nmi_touch) = 0;
		touched = 1;
	}
	if (!touched && __get_cpu_var(last_irq_sum) == sum) {
		/*
		 * Ayiee, looks like this CPU is stuck ...
		 * wait a few IRQs (5 seconds) before doing the oops ...
		 */
		local_inc(&__get_cpu_var(alert_counter));
		if (local_read(&__get_cpu_var(alert_counter)) == 5*nmi_hz) {
			if (notify_die(DIE_NMI, "nmi", regs, reason, 2, SIGINT)
							== NOTIFY_STOP) {
				local_set(&__get_cpu_var(alert_counter), 0);
				return;
			}
			die_nmi("NMI Watchdog detected LOCKUP on CPU %d\n", regs);
		}
	} else {
		__get_cpu_var(last_irq_sum) = sum;
		local_set(&__get_cpu_var(alert_counter), 0);
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
		wrmsrl(nmi_perfctr_msr, -((u64)cpu_khz * 1000 / nmi_hz));
	}
}

static __kprobes int dummy_nmi_callback(struct pt_regs * regs, int cpu)
{
	return 0;
}
 
static nmi_callback_t nmi_callback = dummy_nmi_callback;
 
asmlinkage __kprobes void do_nmi(struct pt_regs * regs, long error_code)
{
	int cpu = safe_smp_processor_id();

	nmi_enter();
	add_pda(__nmi_count,1);
	if (!rcu_dereference(nmi_callback)(regs, cpu))
		default_do_nmi(regs);
	nmi_exit();
}

void set_nmi_callback(nmi_callback_t callback)
{
	vmalloc_sync_all();
	rcu_assign_pointer(nmi_callback, callback);
}

void unset_nmi_callback(void)
{
	nmi_callback = dummy_nmi_callback;
}

#ifdef CONFIG_SYSCTL

static int unknown_nmi_panic_callback(struct pt_regs *regs, int cpu)
{
	unsigned char reason = get_nmi_reason();
	char buf[64];

	if (!(reason & 0xc0)) {
		sprintf(buf, "NMI received for unknown reason %02x\n", reason);
		die_nmi(buf,regs);
	}
	return 0;
}

/*
 * proc handler for /proc/sys/kernel/unknown_nmi_panic
 */
int proc_unknown_nmi_panic(struct ctl_table *table, int write, struct file *file,
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
EXPORT_SYMBOL(touch_nmi_watchdog);
