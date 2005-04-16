/*
 *	x86 SMP booting functions
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998, 1999, 2000 Ingo Molnar <mingo@redhat.com>
 *	Copyright 2001 Andi Kleen, SuSE Labs.
 *
 *	Much of the core SMP work is based on previous work by Thomas Radke, to
 *	whom a great many thanks are extended.
 *
 *	Thanks to Intel for making available several different Pentium,
 *	Pentium Pro and Pentium-II/Xeon MP machines.
 *	Original development of Linux SMP code supported by Caldera.
 *
 *	This code is released under the GNU General Public License version 2 or
 *	later.
 *
 *	Fixes
 *		Felix Koop	:	NR_CPUS used properly
 *		Jose Renau	:	Handle single CPU case.
 *		Alan Cox	:	By repeated request 8) - Total BogoMIP report.
 *		Greg Wright	:	Fix for kernel stacks panic.
 *		Erich Boleyn	:	MP v1.4 and additional changes.
 *	Matthias Sattler	:	Changes for 2.1 kernel map.
 *	Michel Lespinasse	:	Changes for 2.1 kernel map.
 *	Michael Chastain	:	Change trampoline.S to gnu as.
 *		Alan Cox	:	Dumb bug: 'B' step PPro's are fine
 *		Ingo Molnar	:	Added APIC timers, based on code
 *					from Jose Renau
 *		Ingo Molnar	:	various cleanups and rewrites
 *		Tigran Aivazian	:	fixed "0.00 in /proc/uptime on SMP" bug.
 *	Maciej W. Rozycki	:	Bits for genuine 82489DX APICs
 *	Andi Kleen		:	Changed for SMP boot into long mode.
 *		Rusty Russell	:	Hacked into shape for new "hotplug" boot process. 
 */

#include <linux/config.h>
#include <linux/init.h>

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/smp_lock.h>
#include <linux/irq.h>
#include <linux/bootmem.h>
#include <linux/thread_info.h>
#include <linux/module.h>

#include <linux/delay.h>
#include <linux/mc146818rtc.h>
#include <asm/mtrr.h>
#include <asm/pgalloc.h>
#include <asm/desc.h>
#include <asm/kdebug.h>
#include <asm/tlbflush.h>
#include <asm/proto.h>

/* Number of siblings per CPU package */
int smp_num_siblings = 1;
/* Package ID of each logical CPU */
u8 phys_proc_id[NR_CPUS] = { [0 ... NR_CPUS-1] = BAD_APICID };
EXPORT_SYMBOL(phys_proc_id);

/* Bitmask of currently online CPUs */
cpumask_t cpu_online_map;

cpumask_t cpu_callin_map;
cpumask_t cpu_callout_map;
static cpumask_t smp_commenced_mask;

/* Per CPU bogomips and other parameters */
struct cpuinfo_x86 cpu_data[NR_CPUS] __cacheline_aligned;

cpumask_t cpu_sibling_map[NR_CPUS] __cacheline_aligned;

/*
 * Trampoline 80x86 program as an array.
 */

extern unsigned char trampoline_data [];
extern unsigned char trampoline_end  [];

/*
 * Currently trivial. Write the real->protected mode
 * bootstrap into the page concerned. The caller
 * has made sure it's suitably aligned.
 */

static unsigned long __init setup_trampoline(void)
{
	void *tramp = __va(SMP_TRAMPOLINE_BASE); 
	memcpy(tramp, trampoline_data, trampoline_end - trampoline_data);
	return virt_to_phys(tramp);
}

/*
 * The bootstrap kernel entry code has set these up. Save them for
 * a given CPU
 */

static void __init smp_store_cpu_info(int id)
{
	struct cpuinfo_x86 *c = cpu_data + id;

	*c = boot_cpu_data;
	identify_cpu(c);
}

/*
 * TSC synchronization.
 *
 * We first check whether all CPUs have their TSC's synchronized,
 * then we print a warning if not, and always resync.
 */

static atomic_t tsc_start_flag = ATOMIC_INIT(0);
static atomic_t tsc_count_start = ATOMIC_INIT(0);
static atomic_t tsc_count_stop = ATOMIC_INIT(0);
static unsigned long long tsc_values[NR_CPUS];

#define NR_LOOPS 5

extern unsigned int fast_gettimeoffset_quotient;

static void __init synchronize_tsc_bp (void)
{
	int i;
	unsigned long long t0;
	unsigned long long sum, avg;
	long long delta;
	long one_usec;
	int buggy = 0;

	printk(KERN_INFO "checking TSC synchronization across %u CPUs: ",num_booting_cpus());

	one_usec = cpu_khz; 

	atomic_set(&tsc_start_flag, 1);
	wmb();

	/*
	 * We loop a few times to get a primed instruction cache,
	 * then the last pass is more or less synchronized and
	 * the BP and APs set their cycle counters to zero all at
	 * once. This reduces the chance of having random offsets
	 * between the processors, and guarantees that the maximum
	 * delay between the cycle counters is never bigger than
	 * the latency of information-passing (cachelines) between
	 * two CPUs.
	 */
	for (i = 0; i < NR_LOOPS; i++) {
		/*
		 * all APs synchronize but they loop on '== num_cpus'
		 */
		while (atomic_read(&tsc_count_start) != num_booting_cpus()-1) mb();
		atomic_set(&tsc_count_stop, 0);
		wmb();
		/*
		 * this lets the APs save their current TSC:
		 */
		atomic_inc(&tsc_count_start);

		sync_core();
		rdtscll(tsc_values[smp_processor_id()]);
		/*
		 * We clear the TSC in the last loop:
		 */
		if (i == NR_LOOPS-1)
			write_tsc(0, 0);

		/*
		 * Wait for all APs to leave the synchronization point:
		 */
		while (atomic_read(&tsc_count_stop) != num_booting_cpus()-1) mb();
		atomic_set(&tsc_count_start, 0);
		wmb();
		atomic_inc(&tsc_count_stop);
	}

	sum = 0;
	for (i = 0; i < NR_CPUS; i++) {
		if (cpu_isset(i, cpu_callout_map)) {
		t0 = tsc_values[i];
		sum += t0;
	}
	}
	avg = sum / num_booting_cpus();

	sum = 0;
	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_isset(i, cpu_callout_map))
			continue;

		delta = tsc_values[i] - avg;
		if (delta < 0)
			delta = -delta;
		/*
		 * We report bigger than 2 microseconds clock differences.
		 */
		if (delta > 2*one_usec) {
			long realdelta;
			if (!buggy) {
				buggy = 1;
				printk("\n");
			}
			realdelta = delta / one_usec;
			if (tsc_values[i] < avg)
				realdelta = -realdelta;

			printk("BIOS BUG: CPU#%d improperly initialized, has %ld usecs TSC skew! FIXED.\n",
				i, realdelta);
		}

		sum += delta;
	}
	if (!buggy)
		printk("passed.\n");
}

static void __init synchronize_tsc_ap (void)
{
	int i;

	/*
	 * Not every cpu is online at the time
	 * this gets called, so we first wait for the BP to
	 * finish SMP initialization:
	 */
	while (!atomic_read(&tsc_start_flag)) mb();

	for (i = 0; i < NR_LOOPS; i++) {
		atomic_inc(&tsc_count_start);
		while (atomic_read(&tsc_count_start) != num_booting_cpus()) mb();

		sync_core();
		rdtscll(tsc_values[smp_processor_id()]);
		if (i == NR_LOOPS-1)
			write_tsc(0, 0);

		atomic_inc(&tsc_count_stop);
		while (atomic_read(&tsc_count_stop) != num_booting_cpus()) mb();
	}
}
#undef NR_LOOPS

static atomic_t init_deasserted;

static void __init smp_callin(void)
{
	int cpuid, phys_id;
	unsigned long timeout;

	/*
	 * If waken up by an INIT in an 82489DX configuration
	 * we may get here before an INIT-deassert IPI reaches
	 * our local APIC.  We have to wait for the IPI or we'll
	 * lock up on an APIC access.
	 */
	while (!atomic_read(&init_deasserted));

	/*
	 * (This works even if the APIC is not enabled.)
	 */
	phys_id = GET_APIC_ID(apic_read(APIC_ID));
	cpuid = smp_processor_id();
	if (cpu_isset(cpuid, cpu_callin_map)) {
		panic("smp_callin: phys CPU#%d, CPU#%d already present??\n",
					phys_id, cpuid);
	}
	Dprintk("CPU#%d (phys ID: %d) waiting for CALLOUT\n", cpuid, phys_id);

	/*
	 * STARTUP IPIs are fragile beasts as they might sometimes
	 * trigger some glue motherboard logic. Complete APIC bus
	 * silence for 1 second, this overestimates the time the
	 * boot CPU is spending to send the up to 2 STARTUP IPIs
	 * by a factor of two. This should be enough.
	 */

	/*
	 * Waiting 2s total for startup (udelay is not yet working)
	 */
	timeout = jiffies + 2*HZ;
	while (time_before(jiffies, timeout)) {
		/*
		 * Has the boot CPU finished it's STARTUP sequence?
		 */
		if (cpu_isset(cpuid, cpu_callout_map))
			break;
		rep_nop();
	}

	if (!time_before(jiffies, timeout)) {
		panic("smp_callin: CPU%d started up but did not get a callout!\n",
			cpuid);
	}

	/*
	 * the boot CPU has finished the init stage and is spinning
	 * on callin_map until we finish. We are free to set up this
	 * CPU, first the APIC. (this is probably redundant on most
	 * boards)
	 */

	Dprintk("CALLIN, before setup_local_APIC().\n");
	setup_local_APIC();

	local_irq_enable();

	/*
	 * Get our bogomips.
	 */
	calibrate_delay();
	Dprintk("Stack at about %p\n",&cpuid);

	disable_APIC_timer();

	/*
	 * Save our processor parameters
	 */
 	smp_store_cpu_info(cpuid);

	local_irq_disable();

	/*
	 * Allow the master to continue.
	 */
	cpu_set(cpuid, cpu_callin_map);

	/*
	 *      Synchronize the TSC with the BP
	 */
	if (cpu_has_tsc)
		synchronize_tsc_ap();
}

static int cpucount;

/*
 * Activate a secondary processor.
 */
void __init start_secondary(void)
{
	/*
	 * Dont put anything before smp_callin(), SMP
	 * booting is too fragile that we want to limit the
	 * things done here to the most necessary things.
	 */
	cpu_init();
	smp_callin();

	/* otherwise gcc will move up the smp_processor_id before the cpu_init */
	barrier();

	Dprintk("cpu %d: waiting for commence\n", smp_processor_id()); 
	while (!cpu_isset(smp_processor_id(), smp_commenced_mask))
		rep_nop();

	Dprintk("cpu %d: setting up apic clock\n", smp_processor_id()); 	
	setup_secondary_APIC_clock();

	Dprintk("cpu %d: enabling apic timer\n", smp_processor_id()); 

	if (nmi_watchdog == NMI_IO_APIC) {
		disable_8259A_irq(0);
		enable_NMI_through_LVT0(NULL);
		enable_8259A_irq(0);
	}


	enable_APIC_timer(); 

	/*
	 * low-memory mappings have been cleared, flush them from
	 * the local TLBs too.
	 */
	local_flush_tlb();

	Dprintk("cpu %d eSetting cpu_online_map\n", smp_processor_id()); 
	cpu_set(smp_processor_id(), cpu_online_map);
	wmb();
	
	cpu_idle();
}

extern volatile unsigned long init_rsp; 
extern void (*initial_code)(void);

#if APIC_DEBUG
static inline void inquire_remote_apic(int apicid)
{
	unsigned i, regs[] = { APIC_ID >> 4, APIC_LVR >> 4, APIC_SPIV >> 4 };
	char *names[] = { "ID", "VERSION", "SPIV" };
	int timeout, status;

	printk(KERN_INFO "Inquiring remote APIC #%d...\n", apicid);

	for (i = 0; i < sizeof(regs) / sizeof(*regs); i++) {
		printk("... APIC #%d %s: ", apicid, names[i]);

		/*
		 * Wait for idle.
		 */
		apic_wait_icr_idle();

		apic_write_around(APIC_ICR2, SET_APIC_DEST_FIELD(apicid));
		apic_write_around(APIC_ICR, APIC_DM_REMRD | regs[i]);

		timeout = 0;
		do {
			udelay(100);
			status = apic_read(APIC_ICR) & APIC_ICR_RR_MASK;
		} while (status == APIC_ICR_RR_INPROG && timeout++ < 1000);

		switch (status) {
		case APIC_ICR_RR_VALID:
			status = apic_read(APIC_RRR);
			printk("%08x\n", status);
			break;
		default:
			printk("failed\n");
		}
	}
}
#endif

static int __init wakeup_secondary_via_INIT(int phys_apicid, unsigned int start_rip)
{
	unsigned long send_status = 0, accept_status = 0;
	int maxlvt, timeout, num_starts, j;

	Dprintk("Asserting INIT.\n");

	/*
	 * Turn INIT on target chip
	 */
	apic_write_around(APIC_ICR2, SET_APIC_DEST_FIELD(phys_apicid));

	/*
	 * Send IPI
	 */
	apic_write_around(APIC_ICR, APIC_INT_LEVELTRIG | APIC_INT_ASSERT
				| APIC_DM_INIT);

	Dprintk("Waiting for send to finish...\n");
	timeout = 0;
	do {
		Dprintk("+");
		udelay(100);
		send_status = apic_read(APIC_ICR) & APIC_ICR_BUSY;
	} while (send_status && (timeout++ < 1000));

	mdelay(10);

	Dprintk("Deasserting INIT.\n");

	/* Target chip */
	apic_write_around(APIC_ICR2, SET_APIC_DEST_FIELD(phys_apicid));

	/* Send IPI */
	apic_write_around(APIC_ICR, APIC_INT_LEVELTRIG | APIC_DM_INIT);

	Dprintk("Waiting for send to finish...\n");
	timeout = 0;
	do {
		Dprintk("+");
		udelay(100);
		send_status = apic_read(APIC_ICR) & APIC_ICR_BUSY;
	} while (send_status && (timeout++ < 1000));

	atomic_set(&init_deasserted, 1);

	/*
	 * Should we send STARTUP IPIs ?
	 *
	 * Determine this based on the APIC version.
	 * If we don't have an integrated APIC, don't send the STARTUP IPIs.
	 */
	if (APIC_INTEGRATED(apic_version[phys_apicid]))
		num_starts = 2;
	else
		num_starts = 0;

	/*
	 * Run STARTUP IPI loop.
	 */
	Dprintk("#startup loops: %d.\n", num_starts);

	maxlvt = get_maxlvt();

	for (j = 1; j <= num_starts; j++) {
		Dprintk("Sending STARTUP #%d.\n",j);
		apic_read_around(APIC_SPIV);
		apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
		Dprintk("After apic_write.\n");

		/*
		 * STARTUP IPI
		 */

		/* Target chip */
		apic_write_around(APIC_ICR2, SET_APIC_DEST_FIELD(phys_apicid));

		/* Boot on the stack */
		/* Kick the second */
		apic_write_around(APIC_ICR, APIC_DM_STARTUP
					| (start_rip >> 12));

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		udelay(300);

		Dprintk("Startup point 1.\n");

		Dprintk("Waiting for send to finish...\n");
		timeout = 0;
		do {
			Dprintk("+");
			udelay(100);
			send_status = apic_read(APIC_ICR) & APIC_ICR_BUSY;
		} while (send_status && (timeout++ < 1000));

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		udelay(200);
		/*
		 * Due to the Pentium erratum 3AP.
		 */
		if (maxlvt > 3) {
			apic_read_around(APIC_SPIV);
			apic_write(APIC_ESR, 0);
		}
		accept_status = (apic_read(APIC_ESR) & 0xEF);
		if (send_status || accept_status)
			break;
	}
	Dprintk("After Startup.\n");

	if (send_status)
		printk(KERN_ERR "APIC never delivered???\n");
	if (accept_status)
		printk(KERN_ERR "APIC delivery error (%lx).\n", accept_status);

	return (send_status | accept_status);
}

static void __init do_boot_cpu (int apicid)
{
	struct task_struct *idle;
	unsigned long boot_error;
	int timeout, cpu;
	unsigned long start_rip;

	cpu = ++cpucount;
	/*
	 * We can't use kernel_thread since we must avoid to
	 * reschedule the child.
	 */
	idle = fork_idle(cpu);
	if (IS_ERR(idle))
		panic("failed fork for CPU %d", cpu);
	x86_cpu_to_apicid[cpu] = apicid;

	cpu_pda[cpu].pcurrent = idle;

	start_rip = setup_trampoline();

	init_rsp = idle->thread.rsp; 
	per_cpu(init_tss,cpu).rsp0 = init_rsp;
	initial_code = start_secondary;
	clear_ti_thread_flag(idle->thread_info, TIF_FORK);

	printk(KERN_INFO "Booting processor %d/%d rip %lx rsp %lx\n", cpu, apicid, 
	       start_rip, init_rsp);

	/*
	 * This grunge runs the startup process for
	 * the targeted processor.
	 */

	atomic_set(&init_deasserted, 0);

	Dprintk("Setting warm reset code and vector.\n");

	CMOS_WRITE(0xa, 0xf);
	local_flush_tlb();
	Dprintk("1.\n");
	*((volatile unsigned short *) phys_to_virt(0x469)) = start_rip >> 4;
	Dprintk("2.\n");
	*((volatile unsigned short *) phys_to_virt(0x467)) = start_rip & 0xf;
	Dprintk("3.\n");

	/*
	 * Be paranoid about clearing APIC errors.
	 */
	if (APIC_INTEGRATED(apic_version[apicid])) {
		apic_read_around(APIC_SPIV);
		apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
	}

	/*
	 * Status is now clean
	 */
	boot_error = 0;

	/*
	 * Starting actual IPI sequence...
	 */
	boot_error = wakeup_secondary_via_INIT(apicid, start_rip); 

	if (!boot_error) {
		/*
		 * allow APs to start initializing.
		 */
		Dprintk("Before Callout %d.\n", cpu);
		cpu_set(cpu, cpu_callout_map);
		Dprintk("After Callout %d.\n", cpu);

		/*
		 * Wait 5s total for a response
		 */
		for (timeout = 0; timeout < 50000; timeout++) {
			if (cpu_isset(cpu, cpu_callin_map))
				break;	/* It has booted */
			udelay(100);
		}

		if (cpu_isset(cpu, cpu_callin_map)) {
			/* number CPUs logically, starting from 1 (BSP is 0) */
			Dprintk("OK.\n");
			print_cpu_info(&cpu_data[cpu]);
			Dprintk("CPU has booted.\n");
		} else {
			boot_error = 1;
			if (*((volatile unsigned char *)phys_to_virt(SMP_TRAMPOLINE_BASE))
					== 0xA5)
				/* trampoline started but...? */
				printk("Stuck ??\n");
			else
				/* trampoline code not run */
				printk("Not responding.\n");
#if APIC_DEBUG
			inquire_remote_apic(apicid);
#endif
		}
	}
	if (boot_error) {
		cpu_clear(cpu, cpu_callout_map); /* was set here (do_boot_cpu()) */
		clear_bit(cpu, &cpu_initialized); /* was set by cpu_init() */
		cpucount--;
		x86_cpu_to_apicid[cpu] = BAD_APICID;
		x86_cpu_to_log_apicid[cpu] = BAD_APICID;
	}
}

static void smp_tune_scheduling (void)
{
	int cachesize;       /* kB   */
	unsigned long bandwidth = 1000; /* MB/s */
	/*
	 * Rough estimation for SMP scheduling, this is the number of
	 * cycles it takes for a fully memory-limited process to flush
	 * the SMP-local cache.
	 *
	 * (For a P5 this pretty much means we will choose another idle
	 *  CPU almost always at wakeup time (this is due to the small
	 *  L1 cache), on PIIs it's around 50-100 usecs, depending on
	 *  the cache size)
	 */

	if (!cpu_khz) {
		return;
	} else {
		cachesize = boot_cpu_data.x86_cache_size;
		if (cachesize == -1) {
			cachesize = 16; /* Pentiums, 2x8kB cache */
			bandwidth = 100;
		}
	}
}

/*
 * Cycle through the processors sending APIC IPIs to boot each.
 */

static void __init smp_boot_cpus(unsigned int max_cpus)
{
	unsigned apicid, cpu, bit, kicked;

	nmi_watchdog_default();

	/*
	 * Setup boot CPU information
	 */
	smp_store_cpu_info(0); /* Final full version of the data */
	printk(KERN_INFO "CPU%d: ", 0);
	print_cpu_info(&cpu_data[0]);

	current_thread_info()->cpu = 0;
	smp_tune_scheduling();

	if (!physid_isset(hard_smp_processor_id(), phys_cpu_present_map)) {
		printk("weird, boot CPU (#%d) not listed by the BIOS.\n",
		       hard_smp_processor_id());
		physid_set(hard_smp_processor_id(), phys_cpu_present_map);
	}

	/*
	 * If we couldn't find an SMP configuration at boot time,
	 * get out of here now!
	 */
	if (!smp_found_config) {
		printk(KERN_NOTICE "SMP motherboard not detected.\n");
		io_apic_irqs = 0;
		cpu_online_map = cpumask_of_cpu(0);
		cpu_set(0, cpu_sibling_map[0]);
		phys_cpu_present_map = physid_mask_of_physid(0);
		if (APIC_init_uniprocessor())
			printk(KERN_NOTICE "Local APIC not detected."
					   " Using dummy APIC emulation.\n");
		goto smp_done;
	}

	/*
	 * Should not be necessary because the MP table should list the boot
	 * CPU too, but we do it for the sake of robustness anyway.
	 */
	if (!physid_isset(boot_cpu_id, phys_cpu_present_map)) {
		printk(KERN_NOTICE "weird, boot CPU (#%d) not listed by the BIOS.\n",
								 boot_cpu_id);
		physid_set(hard_smp_processor_id(), phys_cpu_present_map);
	}

	/*
	 * If we couldn't find a local APIC, then get out of here now!
	 */
	if (APIC_INTEGRATED(apic_version[boot_cpu_id]) && !cpu_has_apic) {
		printk(KERN_ERR "BIOS bug, local APIC #%d not detected!...\n",
			boot_cpu_id);
		printk(KERN_ERR "... forcing use of dummy APIC emulation. (tell your hw vendor)\n");
		io_apic_irqs = 0;
		cpu_online_map = cpumask_of_cpu(0);
		cpu_set(0, cpu_sibling_map[0]);
		phys_cpu_present_map = physid_mask_of_physid(0);
		disable_apic = 1;
		goto smp_done;
	}

	verify_local_APIC();

	/*
	 * If SMP should be disabled, then really disable it!
	 */
	if (!max_cpus) {
		smp_found_config = 0;
		printk(KERN_INFO "SMP mode deactivated, forcing use of dummy APIC emulation.\n");
		io_apic_irqs = 0;
		cpu_online_map = cpumask_of_cpu(0);
		cpu_set(0, cpu_sibling_map[0]);
		phys_cpu_present_map = physid_mask_of_physid(0);
		disable_apic = 1;
		goto smp_done;
	}

	connect_bsp_APIC();
	setup_local_APIC();

	if (GET_APIC_ID(apic_read(APIC_ID)) != boot_cpu_id)
		BUG();

	x86_cpu_to_apicid[0] = boot_cpu_id;

	/*
	 * Now scan the CPU present map and fire up the other CPUs.
	 */
	Dprintk("CPU present map: %lx\n", physids_coerce(phys_cpu_present_map));

	kicked = 1;
	for (bit = 0; kicked < NR_CPUS && bit < MAX_APICS; bit++) {
		apicid = cpu_present_to_apicid(bit);
		/*
		 * Don't even attempt to start the boot CPU!
		 */
		if (apicid == boot_cpu_id || (apicid == BAD_APICID))
			continue;

		if (!physid_isset(apicid, phys_cpu_present_map))
			continue;
		if ((max_cpus >= 0) && (max_cpus <= cpucount+1))
			continue;

		do_boot_cpu(apicid);
		++kicked;
	}

	/*
	 * Cleanup possible dangling ends...
	 */
	{
		/*
		 * Install writable page 0 entry to set BIOS data area.
		 */
		local_flush_tlb();

		/*
		 * Paranoid:  Set warm reset code and vector here back
		 * to default values.
		 */
		CMOS_WRITE(0, 0xf);

		*((volatile int *) phys_to_virt(0x467)) = 0;
	}

	/*
	 * Allow the user to impress friends.
	 */

	Dprintk("Before bogomips.\n");
	if (!cpucount) {
		printk(KERN_INFO "Only one processor found.\n");
	} else {
		unsigned long bogosum = 0;
		for (cpu = 0; cpu < NR_CPUS; cpu++)
			if (cpu_isset(cpu, cpu_callout_map))
				bogosum += cpu_data[cpu].loops_per_jiffy;
		printk(KERN_INFO "Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
			cpucount+1,
			bogosum/(500000/HZ),
			(bogosum/(5000/HZ))%100);
		Dprintk("Before bogocount - setting activated=1.\n");
	}

	/*
	 * Construct cpu_sibling_map[], so that we can tell the
	 * sibling CPU efficiently.
	 */
	for (cpu = 0; cpu < NR_CPUS; cpu++)
		cpus_clear(cpu_sibling_map[cpu]);

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		int siblings = 0;
		int i;
		if (!cpu_isset(cpu, cpu_callout_map))
			continue;

		if (smp_num_siblings > 1) {
			for (i = 0; i < NR_CPUS; i++) {
				if (!cpu_isset(i, cpu_callout_map))
					continue;
				if (phys_proc_id[cpu] == phys_proc_id[i]) {
					siblings++;
					cpu_set(i, cpu_sibling_map[cpu]);
				}
			}
		} else { 
			siblings++;
			cpu_set(cpu, cpu_sibling_map[cpu]);
		}

		if (siblings != smp_num_siblings) {
			printk(KERN_WARNING 
	       "WARNING: %d siblings found for CPU%d, should be %d\n", 
			       siblings, cpu, smp_num_siblings);
			smp_num_siblings = siblings;
		}       
	}

	Dprintk("Boot done.\n");

	/*
	 * Here we can be sure that there is an IO-APIC in the system. Let's
	 * go and set it up:
	 */
	if (!skip_ioapic_setup && nr_ioapics)
		setup_IO_APIC();
	else
		nr_ioapics = 0;

	setup_boot_APIC_clock();

	/*
	 * Synchronize the TSC with the AP
	 */
	if (cpu_has_tsc && cpucount)
		synchronize_tsc_bp();

 smp_done:
	time_init_smp();
}

/* These are wrappers to interface to the new boot process.  Someone
   who understands all this stuff should rewrite it properly. --RR 15/Jul/02 */
void __init smp_prepare_cpus(unsigned int max_cpus)
{
	smp_boot_cpus(max_cpus);
}

void __devinit smp_prepare_boot_cpu(void)
{
	cpu_set(smp_processor_id(), cpu_online_map);
	cpu_set(smp_processor_id(), cpu_callout_map);
}

int __devinit __cpu_up(unsigned int cpu)
{
	/* This only works at boot for x86.  See "rewrite" above. */
	if (cpu_isset(cpu, smp_commenced_mask)) {
		local_irq_enable();
		return -ENOSYS;
	}

	/* In case one didn't come up */
	if (!cpu_isset(cpu, cpu_callin_map)) {
		local_irq_enable();
		return -EIO;
	}
	local_irq_enable();

	/* Unleash the CPU! */
	Dprintk("waiting for cpu %d\n", cpu);

	cpu_set(cpu, smp_commenced_mask);
	while (!cpu_isset(cpu, cpu_online_map))
		mb();
	return 0;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
#ifdef CONFIG_X86_IO_APIC
	setup_ioapic_dest();
#endif
	zap_low_mappings();
}

