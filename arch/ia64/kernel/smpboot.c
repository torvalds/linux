/*
 * SMP boot-related support
 *
 * Copyright (C) 1998-2003, 2005 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2001, 2004-2005 Intel Corp
 * 	Rohit Seth <rohit.seth@intel.com>
 * 	Suresh Siddha <suresh.b.siddha@intel.com>
 * 	Gordon Jin <gordon.jin@intel.com>
 *	Ashok Raj  <ashok.raj@intel.com>
 *
 * 01/05/16 Rohit Seth <rohit.seth@intel.com>	Moved SMP booting functions from smp.c to here.
 * 01/04/27 David Mosberger <davidm@hpl.hp.com>	Added ITC synching code.
 * 02/07/31 David Mosberger <davidm@hpl.hp.com>	Switch over to hotplug-CPU boot-sequence.
 *						smp_boot_cpus()/smp_commence() is replaced by
 *						smp_prepare_cpus()/__cpu_up()/smp_cpus_done().
 * 04/06/21 Ashok Raj		<ashok.raj@intel.com> Added CPU Hotplug Support
 * 04/12/26 Jin Gordon <gordon.jin@intel.com>
 * 04/12/26 Rohit Seth <rohit.seth@intel.com>
 *						Add multi-threading and multi-core detection
 * 05/01/30 Suresh Siddha <suresh.b.siddha@intel.com>
 *						Setup cpu_sibling_map and cpu_core_map
 */
#include <linux/config.h>

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/bootmem.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/efi.h>
#include <linux/percpu.h>
#include <linux/bitops.h>

#include <asm/atomic.h>
#include <asm/cache.h>
#include <asm/current.h>
#include <asm/delay.h>
#include <asm/ia32.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/machvec.h>
#include <asm/mca.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/tlbflush.h>
#include <asm/unistd.h>

#define SMP_DEBUG 0

#if SMP_DEBUG
#define Dprintk(x...)  printk(x)
#else
#define Dprintk(x...)
#endif

#ifdef CONFIG_HOTPLUG_CPU
/*
 * Store all idle threads, this can be reused instead of creating
 * a new thread. Also avoids complicated thread destroy functionality
 * for idle threads.
 */
struct task_struct *idle_thread_array[NR_CPUS];

/*
 * Global array allocated for NR_CPUS at boot time
 */
struct sal_to_os_boot sal_boot_rendez_state[NR_CPUS];

/*
 * start_ap in head.S uses this to store current booting cpu
 * info.
 */
struct sal_to_os_boot *sal_state_for_booting_cpu = &sal_boot_rendez_state[0];

#define set_brendez_area(x) (sal_state_for_booting_cpu = &sal_boot_rendez_state[(x)]);

#define get_idle_for_cpu(x)		(idle_thread_array[(x)])
#define set_idle_for_cpu(x,p)	(idle_thread_array[(x)] = (p))

#else

#define get_idle_for_cpu(x)		(NULL)
#define set_idle_for_cpu(x,p)
#define set_brendez_area(x)
#endif


/*
 * ITC synchronization related stuff:
 */
#define MASTER	0
#define SLAVE	(SMP_CACHE_BYTES/8)

#define NUM_ROUNDS	64	/* magic value */
#define NUM_ITERS	5	/* likewise */

static DEFINE_SPINLOCK(itc_sync_lock);
static volatile unsigned long go[SLAVE + 1];

#define DEBUG_ITC_SYNC	0

extern void __devinit calibrate_delay (void);
extern void start_ap (void);
extern unsigned long ia64_iobase;

task_t *task_for_booting_cpu;

/*
 * State for each CPU
 */
DEFINE_PER_CPU(int, cpu_state);

/* Bitmasks of currently online, and possible CPUs */
cpumask_t cpu_online_map;
EXPORT_SYMBOL(cpu_online_map);
cpumask_t cpu_possible_map;
EXPORT_SYMBOL(cpu_possible_map);

cpumask_t cpu_core_map[NR_CPUS] __cacheline_aligned;
cpumask_t cpu_sibling_map[NR_CPUS] __cacheline_aligned;
int smp_num_siblings = 1;
int smp_num_cpucores = 1;

/* which logical CPU number maps to which CPU (physical APIC ID) */
volatile int ia64_cpu_to_sapicid[NR_CPUS];
EXPORT_SYMBOL(ia64_cpu_to_sapicid);

static volatile cpumask_t cpu_callin_map;

struct smp_boot_data smp_boot_data __initdata;

unsigned long ap_wakeup_vector = -1; /* External Int use to wakeup APs */

char __initdata no_int_routing;

unsigned char smp_int_redirect; /* are INT and IPI redirectable by the chipset? */

static int __init
nointroute (char *str)
{
	no_int_routing = 1;
	printk ("no_int_routing on\n");
	return 1;
}

__setup("nointroute", nointroute);

void
sync_master (void *arg)
{
	unsigned long flags, i;

	go[MASTER] = 0;

	local_irq_save(flags);
	{
		for (i = 0; i < NUM_ROUNDS*NUM_ITERS; ++i) {
			while (!go[MASTER])
				cpu_relax();
			go[MASTER] = 0;
			go[SLAVE] = ia64_get_itc();
		}
	}
	local_irq_restore(flags);
}

/*
 * Return the number of cycles by which our itc differs from the itc on the master
 * (time-keeper) CPU.  A positive number indicates our itc is ahead of the master,
 * negative that it is behind.
 */
static inline long
get_delta (long *rt, long *master)
{
	unsigned long best_t0 = 0, best_t1 = ~0UL, best_tm = 0;
	unsigned long tcenter, t0, t1, tm;
	long i;

	for (i = 0; i < NUM_ITERS; ++i) {
		t0 = ia64_get_itc();
		go[MASTER] = 1;
		while (!(tm = go[SLAVE]))
			cpu_relax();
		go[SLAVE] = 0;
		t1 = ia64_get_itc();

		if (t1 - t0 < best_t1 - best_t0)
			best_t0 = t0, best_t1 = t1, best_tm = tm;
	}

	*rt = best_t1 - best_t0;
	*master = best_tm - best_t0;

	/* average best_t0 and best_t1 without overflow: */
	tcenter = (best_t0/2 + best_t1/2);
	if (best_t0 % 2 + best_t1 % 2 == 2)
		++tcenter;
	return tcenter - best_tm;
}

/*
 * Synchronize ar.itc of the current (slave) CPU with the ar.itc of the MASTER CPU
 * (normally the time-keeper CPU).  We use a closed loop to eliminate the possibility of
 * unaccounted-for errors (such as getting a machine check in the middle of a calibration
 * step).  The basic idea is for the slave to ask the master what itc value it has and to
 * read its own itc before and after the master responds.  Each iteration gives us three
 * timestamps:
 *
 *	slave		master
 *
 *	t0 ---\
 *             ---\
 *		   --->
 *			tm
 *		   /---
 *	       /---
 *	t1 <---
 *
 *
 * The goal is to adjust the slave's ar.itc such that tm falls exactly half-way between t0
 * and t1.  If we achieve this, the clocks are synchronized provided the interconnect
 * between the slave and the master is symmetric.  Even if the interconnect were
 * asymmetric, we would still know that the synchronization error is smaller than the
 * roundtrip latency (t0 - t1).
 *
 * When the interconnect is quiet and symmetric, this lets us synchronize the itc to
 * within one or two cycles.  However, we can only *guarantee* that the synchronization is
 * accurate to within a round-trip time, which is typically in the range of several
 * hundred cycles (e.g., ~500 cycles).  In practice, this means that the itc's are usually
 * almost perfectly synchronized, but we shouldn't assume that the accuracy is much better
 * than half a micro second or so.
 */
void
ia64_sync_itc (unsigned int master)
{
	long i, delta, adj, adjust_latency = 0, done = 0;
	unsigned long flags, rt, master_time_stamp, bound;
#if DEBUG_ITC_SYNC
	struct {
		long rt;	/* roundtrip time */
		long master;	/* master's timestamp */
		long diff;	/* difference between midpoint and master's timestamp */
		long lat;	/* estimate of itc adjustment latency */
	} t[NUM_ROUNDS];
#endif

	/*
	 * Make sure local timer ticks are disabled while we sync.  If
	 * they were enabled, we'd have to worry about nasty issues
	 * like setting the ITC ahead of (or a long time before) the
	 * next scheduled tick.
	 */
	BUG_ON((ia64_get_itv() & (1 << 16)) == 0);

	go[MASTER] = 1;

	if (smp_call_function_single(master, sync_master, NULL, 1, 0) < 0) {
		printk(KERN_ERR "sync_itc: failed to get attention of CPU %u!\n", master);
		return;
	}

	while (go[MASTER])
		cpu_relax();	/* wait for master to be ready */

	spin_lock_irqsave(&itc_sync_lock, flags);
	{
		for (i = 0; i < NUM_ROUNDS; ++i) {
			delta = get_delta(&rt, &master_time_stamp);
			if (delta == 0) {
				done = 1;	/* let's lock on to this... */
				bound = rt;
			}

			if (!done) {
				if (i > 0) {
					adjust_latency += -delta;
					adj = -delta + adjust_latency/4;
				} else
					adj = -delta;

				ia64_set_itc(ia64_get_itc() + adj);
			}
#if DEBUG_ITC_SYNC
			t[i].rt = rt;
			t[i].master = master_time_stamp;
			t[i].diff = delta;
			t[i].lat = adjust_latency/4;
#endif
		}
	}
	spin_unlock_irqrestore(&itc_sync_lock, flags);

#if DEBUG_ITC_SYNC
	for (i = 0; i < NUM_ROUNDS; ++i)
		printk("rt=%5ld master=%5ld diff=%5ld adjlat=%5ld\n",
		       t[i].rt, t[i].master, t[i].diff, t[i].lat);
#endif

	printk(KERN_INFO "CPU %d: synchronized ITC with CPU %u (last diff %ld cycles, "
	       "maxerr %lu cycles)\n", smp_processor_id(), master, delta, rt);
}

/*
 * Ideally sets up per-cpu profiling hooks.  Doesn't do much now...
 */
static inline void __devinit
smp_setup_percpu_timer (void)
{
}

static void __devinit
smp_callin (void)
{
	int cpuid, phys_id;
	extern void ia64_init_itm(void);

#ifdef CONFIG_PERFMON
	extern void pfm_init_percpu(void);
#endif

	cpuid = smp_processor_id();
	phys_id = hard_smp_processor_id();

	if (cpu_online(cpuid)) {
		printk(KERN_ERR "huh, phys CPU#0x%x, CPU#0x%x already present??\n",
		       phys_id, cpuid);
		BUG();
	}

	lock_ipi_calllock();
	cpu_set(cpuid, cpu_online_map);
	unlock_ipi_calllock();
	per_cpu(cpu_state, cpuid) = CPU_ONLINE;

	smp_setup_percpu_timer();

	ia64_mca_cmc_vector_setup();	/* Setup vector on AP */

#ifdef CONFIG_PERFMON
	pfm_init_percpu();
#endif

	local_irq_enable();

	if (!(sal_platform_features & IA64_SAL_PLATFORM_FEATURE_ITC_DRIFT)) {
		/*
		 * Synchronize the ITC with the BP.  Need to do this after irqs are
		 * enabled because ia64_sync_itc() calls smp_call_function_single(), which
		 * calls spin_unlock_bh(), which calls spin_unlock_bh(), which calls
		 * local_bh_enable(), which bugs out if irqs are not enabled...
		 */
		Dprintk("Going to syncup ITC with BP.\n");
		ia64_sync_itc(0);
	}

	/*
	 * Get our bogomips.
	 */
	ia64_init_itm();
	calibrate_delay();
	local_cpu_data->loops_per_jiffy = loops_per_jiffy;

#ifdef CONFIG_IA32_SUPPORT
	ia32_gdt_init();
#endif

	/*
	 * Allow the master to continue.
	 */
	cpu_set(cpuid, cpu_callin_map);
	Dprintk("Stack on CPU %d at about %p\n",cpuid, &cpuid);
}


/*
 * Activate a secondary processor.  head.S calls this.
 */
int __devinit
start_secondary (void *unused)
{
	/* Early console may use I/O ports */
	ia64_set_kr(IA64_KR_IO_BASE, __pa(ia64_iobase));
	Dprintk("start_secondary: starting CPU 0x%x\n", hard_smp_processor_id());
	efi_map_pal_code();
	cpu_init();
	preempt_disable();
	smp_callin();

	cpu_idle();
	return 0;
}

struct pt_regs * __devinit idle_regs(struct pt_regs *regs)
{
	return NULL;
}

struct create_idle {
	struct task_struct *idle;
	struct completion done;
	int cpu;
};

void
do_fork_idle(void *_c_idle)
{
	struct create_idle *c_idle = _c_idle;

	c_idle->idle = fork_idle(c_idle->cpu);
	complete(&c_idle->done);
}

static int __devinit
do_boot_cpu (int sapicid, int cpu)
{
	int timeout;
	struct create_idle c_idle = {
		.cpu	= cpu,
		.done	= COMPLETION_INITIALIZER(c_idle.done),
	};
	DECLARE_WORK(work, do_fork_idle, &c_idle);

 	c_idle.idle = get_idle_for_cpu(cpu);
 	if (c_idle.idle) {
		init_idle(c_idle.idle, cpu);
 		goto do_rest;
	}

	/*
	 * We can't use kernel_thread since we must avoid to reschedule the child.
	 */
	if (!keventd_up() || current_is_keventd())
		work.func(work.data);
	else {
		schedule_work(&work);
		wait_for_completion(&c_idle.done);
	}

	if (IS_ERR(c_idle.idle))
		panic("failed fork for CPU %d", cpu);

	set_idle_for_cpu(cpu, c_idle.idle);

do_rest:
	task_for_booting_cpu = c_idle.idle;

	Dprintk("Sending wakeup vector %lu to AP 0x%x/0x%x.\n", ap_wakeup_vector, cpu, sapicid);

	set_brendez_area(cpu);
	platform_send_ipi(cpu, ap_wakeup_vector, IA64_IPI_DM_INT, 0);

	/*
	 * Wait 10s total for the AP to start
	 */
	Dprintk("Waiting on callin_map ...");
	for (timeout = 0; timeout < 100000; timeout++) {
		if (cpu_isset(cpu, cpu_callin_map))
			break;  /* It has booted */
		udelay(100);
	}
	Dprintk("\n");

	if (!cpu_isset(cpu, cpu_callin_map)) {
		printk(KERN_ERR "Processor 0x%x/0x%x is stuck.\n", cpu, sapicid);
		ia64_cpu_to_sapicid[cpu] = -1;
		cpu_clear(cpu, cpu_online_map);  /* was set in smp_callin() */
		return -EINVAL;
	}
	return 0;
}

static int __init
decay (char *str)
{
	int ticks;
	get_option (&str, &ticks);
	return 1;
}

__setup("decay=", decay);

/*
 * Initialize the logical CPU number to SAPICID mapping
 */
void __init
smp_build_cpu_map (void)
{
	int sapicid, cpu, i;
	int boot_cpu_id = hard_smp_processor_id();

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		ia64_cpu_to_sapicid[cpu] = -1;
#ifdef CONFIG_HOTPLUG_CPU
		cpu_set(cpu, cpu_possible_map);
#endif
	}

	ia64_cpu_to_sapicid[0] = boot_cpu_id;
	cpus_clear(cpu_present_map);
	cpu_set(0, cpu_present_map);
	cpu_set(0, cpu_possible_map);
	for (cpu = 1, i = 0; i < smp_boot_data.cpu_count; i++) {
		sapicid = smp_boot_data.cpu_phys_id[i];
		if (sapicid == boot_cpu_id)
			continue;
		cpu_set(cpu, cpu_present_map);
		cpu_set(cpu, cpu_possible_map);
		ia64_cpu_to_sapicid[cpu] = sapicid;
		cpu++;
	}
}

/*
 * Cycle through the APs sending Wakeup IPIs to boot each.
 */
void __init
smp_prepare_cpus (unsigned int max_cpus)
{
	int boot_cpu_id = hard_smp_processor_id();

	/*
	 * Initialize the per-CPU profiling counter/multiplier
	 */

	smp_setup_percpu_timer();

	/*
	 * We have the boot CPU online for sure.
	 */
	cpu_set(0, cpu_online_map);
	cpu_set(0, cpu_callin_map);

	local_cpu_data->loops_per_jiffy = loops_per_jiffy;
	ia64_cpu_to_sapicid[0] = boot_cpu_id;

	printk(KERN_INFO "Boot processor id 0x%x/0x%x\n", 0, boot_cpu_id);

	current_thread_info()->cpu = 0;

	/*
	 * If SMP should be disabled, then really disable it!
	 */
	if (!max_cpus) {
		printk(KERN_INFO "SMP mode deactivated.\n");
		cpus_clear(cpu_online_map);
		cpus_clear(cpu_present_map);
		cpus_clear(cpu_possible_map);
		cpu_set(0, cpu_online_map);
		cpu_set(0, cpu_present_map);
		cpu_set(0, cpu_possible_map);
		return;
	}
}

void __devinit smp_prepare_boot_cpu(void)
{
	cpu_set(smp_processor_id(), cpu_online_map);
	cpu_set(smp_processor_id(), cpu_callin_map);
	per_cpu(cpu_state, smp_processor_id()) = CPU_ONLINE;
}

/*
 * mt_info[] is a temporary store for all info returned by
 * PAL_LOGICAL_TO_PHYSICAL, to be copied into cpuinfo_ia64 when the
 * specific cpu comes.
 */
static struct {
	__u32   socket_id;
	__u16   core_id;
	__u16   thread_id;
	__u16   proc_fixed_addr;
	__u8    valid;
} mt_info[NR_CPUS] __devinitdata;

#ifdef CONFIG_HOTPLUG_CPU
static inline void
remove_from_mtinfo(int cpu)
{
	int i;

	for_each_cpu(i)
		if (mt_info[i].valid &&  mt_info[i].socket_id ==
		    				cpu_data(cpu)->socket_id)
			mt_info[i].valid = 0;
}

static inline void
clear_cpu_sibling_map(int cpu)
{
	int i;

	for_each_cpu_mask(i, cpu_sibling_map[cpu])
		cpu_clear(cpu, cpu_sibling_map[i]);
	for_each_cpu_mask(i, cpu_core_map[cpu])
		cpu_clear(cpu, cpu_core_map[i]);

	cpu_sibling_map[cpu] = cpu_core_map[cpu] = CPU_MASK_NONE;
}

static void
remove_siblinginfo(int cpu)
{
	int last = 0;

	if (cpu_data(cpu)->threads_per_core == 1 &&
	    cpu_data(cpu)->cores_per_socket == 1) {
		cpu_clear(cpu, cpu_core_map[cpu]);
		cpu_clear(cpu, cpu_sibling_map[cpu]);
		return;
	}

	last = (cpus_weight(cpu_core_map[cpu]) == 1 ? 1 : 0);

	/* remove it from all sibling map's */
	clear_cpu_sibling_map(cpu);

	/* if this cpu is the last in the core group, remove all its info 
	 * from mt_info structure
	 */
	if (last)
		remove_from_mtinfo(cpu);
}

extern void fixup_irqs(void);
/* must be called with cpucontrol mutex held */
int __cpu_disable(void)
{
	int cpu = smp_processor_id();

	/*
	 * dont permit boot processor for now
	 */
	if (cpu == 0)
		return -EBUSY;

	remove_siblinginfo(cpu);
	cpu_clear(cpu, cpu_online_map);
	fixup_irqs();
	local_flush_tlb_all();
	cpu_clear(cpu, cpu_callin_map);
	return 0;
}

void __cpu_die(unsigned int cpu)
{
	unsigned int i;

	for (i = 0; i < 100; i++) {
		/* They ack this in play_dead by setting CPU_DEAD */
		if (per_cpu(cpu_state, cpu) == CPU_DEAD)
		{
			printk ("CPU %d is now offline\n", cpu);
			return;
		}
		msleep(100);
	}
 	printk(KERN_ERR "CPU %u didn't die...\n", cpu);
}
#else /* !CONFIG_HOTPLUG_CPU */
int __cpu_disable(void)
{
	return -ENOSYS;
}

void __cpu_die(unsigned int cpu)
{
	/* We said "no" in __cpu_disable */
	BUG();
}
#endif /* CONFIG_HOTPLUG_CPU */

void
smp_cpus_done (unsigned int dummy)
{
	int cpu;
	unsigned long bogosum = 0;

	/*
	 * Allow the user to impress friends.
	 */

	for_each_online_cpu(cpu) {
		bogosum += cpu_data(cpu)->loops_per_jiffy;
	}

	printk(KERN_INFO "Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
	       (int)num_online_cpus(), bogosum/(500000/HZ), (bogosum/(5000/HZ))%100);
}

static inline void __devinit
set_cpu_sibling_map(int cpu)
{
	int i;

	for_each_online_cpu(i) {
		if ((cpu_data(cpu)->socket_id == cpu_data(i)->socket_id)) {
			cpu_set(i, cpu_core_map[cpu]);
			cpu_set(cpu, cpu_core_map[i]);
			if (cpu_data(cpu)->core_id == cpu_data(i)->core_id) {
				cpu_set(i, cpu_sibling_map[cpu]);
				cpu_set(cpu, cpu_sibling_map[i]);
			}
		}
	}
}

int __devinit
__cpu_up (unsigned int cpu)
{
	int ret;
	int sapicid;

	sapicid = ia64_cpu_to_sapicid[cpu];
	if (sapicid == -1)
		return -EINVAL;

	/*
	 * Already booted cpu? not valid anymore since we dont
	 * do idle loop tightspin anymore.
	 */
	if (cpu_isset(cpu, cpu_callin_map))
		return -EINVAL;

	per_cpu(cpu_state, cpu) = CPU_UP_PREPARE;
	/* Processor goes to start_secondary(), sets online flag */
	ret = do_boot_cpu(sapicid, cpu);
	if (ret < 0)
		return ret;

	if (cpu_data(cpu)->threads_per_core == 1 &&
	    cpu_data(cpu)->cores_per_socket == 1) {
		cpu_set(cpu, cpu_sibling_map[cpu]);
		cpu_set(cpu, cpu_core_map[cpu]);
		return 0;
	}

	set_cpu_sibling_map(cpu);

	return 0;
}

/*
 * Assume that CPU's have been discovered by some platform-dependent interface.  For
 * SoftSDV/Lion, that would be ACPI.
 *
 * Setup of the IPI irq handler is done in irq.c:init_IRQ_SMP().
 */
void __init
init_smp_config(void)
{
	struct fptr {
		unsigned long fp;
		unsigned long gp;
	} *ap_startup;
	long sal_ret;

	/* Tell SAL where to drop the AP's.  */
	ap_startup = (struct fptr *) start_ap;
	sal_ret = ia64_sal_set_vectors(SAL_VECTOR_OS_BOOT_RENDEZ,
				       ia64_tpa(ap_startup->fp), ia64_tpa(ap_startup->gp), 0, 0, 0, 0);
	if (sal_ret < 0)
		printk(KERN_ERR "SMP: Can't set SAL AP Boot Rendezvous: %s\n",
		       ia64_sal_strerror(sal_ret));
}

static inline int __devinit
check_for_mtinfo_index(void)
{
	int i;
	
	for_each_cpu(i)
		if (!mt_info[i].valid)
			return i;

	return -1;
}

/*
 * Search the mt_info to find out if this socket's cid/tid information is
 * cached or not. If the socket exists, fill in the core_id and thread_id 
 * in cpuinfo
 */
static int __devinit
check_for_new_socket(__u16 logical_address, struct cpuinfo_ia64 *c)
{
	int i;
	__u32 sid = c->socket_id;

	for_each_cpu(i) {
		if (mt_info[i].valid && mt_info[i].proc_fixed_addr == logical_address
		    && mt_info[i].socket_id == sid) {
			c->core_id = mt_info[i].core_id;
			c->thread_id = mt_info[i].thread_id;
			return 1; /* not a new socket */
		}
	}
	return 0;
}

/*
 * identify_siblings(cpu) gets called from identify_cpu. This populates the 
 * information related to logical execution units in per_cpu_data structure.
 */
void __devinit
identify_siblings(struct cpuinfo_ia64 *c)
{
	s64 status;
	u16 pltid;
	u64 proc_fixed_addr;
	int count, i;
	pal_logical_to_physical_t info;

	if (smp_num_cpucores == 1 && smp_num_siblings == 1)
		return;

	if ((status = ia64_pal_logical_to_phys(0, &info)) != PAL_STATUS_SUCCESS) {
		printk(KERN_ERR "ia64_pal_logical_to_phys failed with %ld\n",
		       status);
		return;
	}
	if ((status = ia64_sal_physical_id_info(&pltid)) != PAL_STATUS_SUCCESS) {
		printk(KERN_ERR "ia64_sal_pltid failed with %ld\n", status);
		return;
	}
	if ((status = ia64_pal_fixed_addr(&proc_fixed_addr)) != PAL_STATUS_SUCCESS) {
		printk(KERN_ERR "ia64_pal_fixed_addr failed with %ld\n", status);
		return;
	}

	c->socket_id =  (pltid << 8) | info.overview_ppid;
	c->cores_per_socket = info.overview_cpp;
	c->threads_per_core = info.overview_tpc;
	count = c->num_log = info.overview_num_log;

	/* If the thread and core id information is already cached, then
	 * we will simply update cpu_info and return. Otherwise, we will
	 * do the PAL calls and cache core and thread id's of all the siblings.
	 */
	if (check_for_new_socket(proc_fixed_addr, c))
		return;

	for (i = 0; i < count; i++) {
		int index;

		if (i && (status = ia64_pal_logical_to_phys(i, &info))
			  != PAL_STATUS_SUCCESS) {
                	printk(KERN_ERR "ia64_pal_logical_to_phys failed"
					" with %ld\n", status);
                	return;
		}
		if (info.log2_la == proc_fixed_addr) {
			c->core_id = info.log1_cid;
			c->thread_id = info.log1_tid;
		}

		index = check_for_mtinfo_index();
		/* We will not do the mt_info caching optimization in this case.
		 */
		if (index < 0)
			continue;

		mt_info[index].valid = 1;
		mt_info[index].socket_id = c->socket_id;
		mt_info[index].core_id = info.log1_cid;
		mt_info[index].thread_id = info.log1_tid;
		mt_info[index].proc_fixed_addr = info.log2_la;
	}
}
