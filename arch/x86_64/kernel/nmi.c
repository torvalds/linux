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
#include <linux/irq.h>
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

#include <asm/smp.h>
#include <asm/mtrr.h>
#include <asm/mpspec.h>
#include <asm/nmi.h>
#include <asm/msr.h>
#include <asm/proto.h>
#include <asm/kdebug.h>

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
unsigned int nmi_perfctr_msr;	/* the MSR to reset in NMI handler */

/* Note that these events don't tick when the CPU idles. This means
   the frequency varies with CPU load. */

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

/* Run after command line and cpu_init init, but before all other checks */
void __init nmi_watchdog_default(void)
{
	if (nmi_watchdog != NMI_DEFAULT)
		return;

	/* For some reason the IO APIC watchdog doesn't work on the AMD
	   8111 chipset. For now switch to local APIC mode using
	   perfctr0 there.  On Intel CPUs we don't have code to handle
	   the perfctr and the IO-APIC seems to work, so use that.  */

	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD) {
		nmi_watchdog = NMI_LOCAL_APIC; 
		printk(KERN_INFO 
              "Using local APIC NMI watchdog using perfctr0\n");
	} else {
		printk(KERN_INFO "Using IO APIC NMI watchdog\n");
		nmi_watchdog = NMI_IO_APIC;
	}
}

/* Why is there no CPUID flag for this? */
static __init int cpu_has_lapic(void)
{
	switch (boot_cpu_data.x86_vendor) { 
	case X86_VENDOR_INTEL:
	case X86_VENDOR_AMD: 
		return boot_cpu_data.x86 >= 6; 
	/* .... add more cpus here or find a different way to figure this out. */	
	default:
		return 0;
	} 	
}

int __init check_nmi_watchdog (void)
{
	int counts[NR_CPUS];
	int cpu;

	if (nmi_watchdog == NMI_LOCAL_APIC && !cpu_has_lapic())  {
		nmi_watchdog = NMI_NONE;
		return -1; 
	}	

	printk(KERN_INFO "testing NMI watchdog ... ");

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		counts[cpu] = cpu_pda[cpu].__nmi_count; 
	local_irq_enable();
	mdelay((10*1000)/nmi_hz); // wait 10 ticks

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
#ifdef CONFIG_SMP
		/* Check cpu_callin_map here because that is set
		   after the timer is started. */
		if (!cpu_isset(cpu, cpu_callin_map))
			continue;
#endif
		if (cpu_pda[cpu].__nmi_count - counts[cpu] <= 5) {
			printk("CPU#%d: NMI appears to be stuck (%d)!\n", 
			       cpu,
			       cpu_pda[cpu].__nmi_count);
			nmi_active = 0;
			lapic_nmi_owner &= ~LAPIC_NMI_WATCHDOG;
			return -1;
		}
	}
	printk("OK.\n");

	/* now that we know it works we can reduce NMI frequency to
	   something more reasonable; makes a difference in some configs */
	if (nmi_watchdog == NMI_LOCAL_APIC)
		nmi_hz = 1;

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
		wrmsr(MSR_IA32_EVNTSEL0, 0, 0);
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

static int lapic_nmi_suspend(struct sys_device *dev, u32 state)
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

static void setup_k7_watchdog(void)
{
	int i;
	unsigned int evntsel;

	/* No check, so can start with slow frequency */
	nmi_hz = 1; 

	/* XXX should check these in EFER */

	nmi_perfctr_msr = MSR_K7_PERFCTR0;

	for(i = 0; i < 4; ++i) {
		/* Simulator may not support it */
		if (checking_wrmsrl(MSR_K7_EVNTSEL0+i, 0UL))
			return;
		wrmsrl(MSR_K7_PERFCTR0+i, 0UL);
	}

	evntsel = K7_EVNTSEL_INT
		| K7_EVNTSEL_OS
		| K7_EVNTSEL_USR
		| K7_NMI_EVENT;

	wrmsr(MSR_K7_EVNTSEL0, evntsel, 0);
	wrmsrl(MSR_K7_PERFCTR0, -((u64)cpu_khz*1000) / nmi_hz);
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	evntsel |= K7_EVNTSEL_ENABLE;
	wrmsr(MSR_K7_EVNTSEL0, evntsel, 0);
}

void setup_apic_nmi_watchdog(void)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		if (boot_cpu_data.x86 < 6)
			return;
		if (strstr(boot_cpu_data.x86_model_id, "Screwdriver"))
			return;
		setup_k7_watchdog();
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
}

void nmi_watchdog_tick (struct pt_regs * regs, unsigned reason)
{
	int sum, cpu;

	cpu = safe_smp_processor_id();
	sum = read_pda(apic_timer_irqs);
	if (last_irq_sums[cpu] == sum) {
		/*
		 * Ayiee, looks like this CPU is stuck ...
		 * wait a few IRQs (5 seconds) before doing the oops ...
		 */
		alert_counter[cpu]++;
		if (alert_counter[cpu] == 5*nmi_hz) {
			if (notify_die(DIE_NMI, "nmi", regs, reason, 2, SIGINT)
							== NOTIFY_STOP) {
				alert_counter[cpu] = 0; 
				return;
			} 
			die_nmi("NMI Watchdog detected LOCKUP on CPU%d", regs);
		}
	} else {
		last_irq_sums[cpu] = sum;
		alert_counter[cpu] = 0;
	}
	if (nmi_perfctr_msr)
		wrmsr(nmi_perfctr_msr, -(cpu_khz/nmi_hz*1000), -1);
}

static int dummy_nmi_callback(struct pt_regs * regs, int cpu)
{
	return 0;
}
 
static nmi_callback_t nmi_callback = dummy_nmi_callback;
 
asmlinkage void do_nmi(struct pt_regs * regs, long error_code)
{
	int cpu = safe_smp_processor_id();

	nmi_enter();
	add_pda(__nmi_count,1);
	if (!nmi_callback(regs, cpu))
		default_do_nmi(regs);
	nmi_exit();
}

void set_nmi_callback(nmi_callback_t callback)
{
	nmi_callback = callback;
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
