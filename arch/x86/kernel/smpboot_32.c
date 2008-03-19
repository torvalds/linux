/*
 *	x86 SMP booting functions
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998, 1999, 2000 Ingo Molnar <mingo@redhat.com>
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
 *		Alan Cox	:	By repeated request 8) - Total BogoMIPS report.
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
 *		Martin J. Bligh	: 	Added support for multi-quad systems
 *		Dave Jones	:	Report invalid combinations of Athlon CPUs.
*		Rusty Russell	:	Hacked into shape for new "hotplug" boot process. */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/bootmem.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/nmi.h>

#include <linux/delay.h>
#include <linux/mc146818rtc.h>
#include <asm/tlbflush.h>
#include <asm/desc.h>
#include <asm/arch_hooks.h>
#include <asm/nmi.h>

#include <mach_apic.h>
#include <mach_wakecpu.h>
#include <smpboot_hooks.h>
#include <asm/vmi.h>
#include <asm/mtrr.h>

/* which logical CPU number maps to which CPU (physical APIC ID) */
u16 x86_cpu_to_apicid_init[NR_CPUS] __initdata =
			{ [0 ... NR_CPUS-1] = BAD_APICID };
void *x86_cpu_to_apicid_early_ptr;
DEFINE_PER_CPU(u16, x86_cpu_to_apicid) = BAD_APICID;
EXPORT_PER_CPU_SYMBOL(x86_cpu_to_apicid);

u16 x86_bios_cpu_apicid_init[NR_CPUS] __initdata
				= { [0 ... NR_CPUS-1] = BAD_APICID };
void *x86_bios_cpu_apicid_early_ptr;
DEFINE_PER_CPU(u16, x86_bios_cpu_apicid) = BAD_APICID;
EXPORT_PER_CPU_SYMBOL(x86_bios_cpu_apicid);

u8 apicid_2_node[MAX_APICID];

extern void map_cpu_to_logical_apicid(void);
extern void unmap_cpu_to_logical_apicid(int cpu);

/* State of each CPU. */
DEFINE_PER_CPU(int, cpu_state) = { 0 };

/* Store all idle threads, this can be reused instead of creating
* a new thread. Also avoids complicated thread destroy functionality
* for idle threads.
*/
#ifdef CONFIG_HOTPLUG_CPU
/*
 * Needed only for CONFIG_HOTPLUG_CPU because __cpuinitdata is
 * removed after init for !CONFIG_HOTPLUG_CPU.
 */
static DEFINE_PER_CPU(struct task_struct *, idle_thread_array);
#define get_idle_for_cpu(x)      (per_cpu(idle_thread_array, x))
#define set_idle_for_cpu(x, p)   (per_cpu(idle_thread_array, x) = (p))
#else
struct task_struct *idle_thread_array[NR_CPUS] __cpuinitdata ;
#define get_idle_for_cpu(x)      (idle_thread_array[(x)])
#define set_idle_for_cpu(x, p)   (idle_thread_array[(x)] = (p))
#endif

static atomic_t init_deasserted;

static void __cpuinit smp_callin(void)
{
	int cpuid, phys_id;
	unsigned long timeout;

	/*
	 * If waken up by an INIT in an 82489DX configuration
	 * we may get here before an INIT-deassert IPI reaches
	 * our local APIC.  We have to wait for the IPI or we'll
	 * lock up on an APIC access.
	 */
	wait_for_init_deassert(&init_deasserted);

	/*
	 * (This works even if the APIC is not enabled.)
	 */
	phys_id = GET_APIC_ID(apic_read(APIC_ID));
	cpuid = smp_processor_id();
	if (cpu_isset(cpuid, cpu_callin_map)) {
		printk("huh, phys CPU#%d, CPU#%d already present??\n",
					phys_id, cpuid);
		BUG();
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
		cpu_relax();
	}

	if (!time_before(jiffies, timeout)) {
		printk("BUG: CPU%d started up but did not get a callout!\n",
			cpuid);
		BUG();
	}

	/*
	 * the boot CPU has finished the init stage and is spinning
	 * on callin_map until we finish. We are free to set up this
	 * CPU, first the APIC. (this is probably redundant on most
	 * boards)
	 */

	Dprintk("CALLIN, before setup_local_APIC().\n");
	smp_callin_clear_local_apic();
	setup_local_APIC();
	end_local_APIC_setup();
	map_cpu_to_logical_apicid();

	/*
	 * Get our bogomips.
	 */
	local_irq_enable();
	calibrate_delay();
	local_irq_disable();
	Dprintk("Stack at about %p\n",&cpuid);

	/*
	 * Save our processor parameters
	 */
	smp_store_cpu_info(cpuid);

	/*
	 * Allow the master to continue.
	 */
	cpu_set(cpuid, cpu_callin_map);
}

/*
 * Activate a secondary processor.
 */
static void __cpuinit start_secondary(void *unused)
{
	/*
	 * Don't put *anything* before cpu_init(), SMP booting is too
	 * fragile that we want to limit the things done here to the
	 * most necessary things.
	 */
#ifdef CONFIG_VMI
	vmi_bringup();
#endif
	cpu_init();
	preempt_disable();
	smp_callin();

	/* otherwise gcc will move up smp_processor_id before the cpu_init */
	barrier();
	/*
	 * Check TSC synchronization with the BP:
	 */
	check_tsc_sync_target();

	if (nmi_watchdog == NMI_IO_APIC) {
		disable_8259A_irq(0);
		enable_NMI_through_LVT0();
		enable_8259A_irq(0);
	}

	/* This must be done before setting cpu_online_map */
	set_cpu_sibling_map(raw_smp_processor_id());
	wmb();

	/*
	 * We need to hold call_lock, so there is no inconsistency
	 * between the time smp_call_function() determines number of
	 * IPI recipients, and the time when the determination is made
	 * for which cpus receive the IPI. Holding this
	 * lock helps us to not include this cpu in a currently in progress
	 * smp_call_function().
	 */
	lock_ipi_call_lock();
	cpu_set(smp_processor_id(), cpu_online_map);
	unlock_ipi_call_lock();
	per_cpu(cpu_state, smp_processor_id()) = CPU_ONLINE;

	setup_secondary_clock();

	wmb();
	cpu_idle();
}

/*
 * Everything has been set up for the secondary
 * CPUs - they just need to reload everything
 * from the task structure
 * This function must not return.
 */
void __devinit initialize_secondary(void)
{
	/*
	 * We don't actually need to load the full TSS,
	 * basically just the stack pointer and the ip.
	 */

	asm volatile(
		"movl %0,%%esp\n\t"
		"jmp *%1"
		:
		:"m" (current->thread.sp),"m" (current->thread.ip));
}

static inline void __inquire_remote_apic(int apicid)
{
	unsigned i, regs[] = { APIC_ID >> 4, APIC_LVR >> 4, APIC_SPIV >> 4 };
	char *names[] = { "ID", "VERSION", "SPIV" };
	int timeout;
	u32 status;

	printk(KERN_INFO "Inquiring remote APIC #%d...\n", apicid);

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		printk(KERN_INFO "... APIC #%d %s: ", apicid, names[i]);

		/*
		 * Wait for idle.
		 */
		status = safe_apic_wait_icr_idle();
		if (status)
			printk(KERN_CONT
			       "a previous APIC delivery may have failed\n");

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
			printk(KERN_CONT "%08x\n", status);
			break;
		default:
			printk(KERN_CONT "failed\n");
		}
	}
}

#ifdef WAKE_SECONDARY_VIA_NMI
/* 
 * Poke the other CPU in the eye via NMI to wake it up. Remember that the normal
 * INIT, INIT, STARTUP sequence will reset the chip hard for us, and this
 * won't ... remember to clear down the APIC, etc later.
 */
static int __devinit
wakeup_secondary_cpu(int logical_apicid, unsigned long start_eip)
{
	unsigned long send_status, accept_status = 0;
	int maxlvt;

	/* Target chip */
	apic_write_around(APIC_ICR2, SET_APIC_DEST_FIELD(logical_apicid));

	/* Boot on the stack */
	/* Kick the second */
	apic_write_around(APIC_ICR, APIC_DM_NMI | APIC_DEST_LOGICAL);

	Dprintk("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	/*
	 * Give the other CPU some time to accept the IPI.
	 */
	udelay(200);
	/*
	 * Due to the Pentium erratum 3AP.
	 */
	maxlvt = lapic_get_maxlvt();
	if (maxlvt > 3) {
		apic_read_around(APIC_SPIV);
		apic_write(APIC_ESR, 0);
	}
	accept_status = (apic_read(APIC_ESR) & 0xEF);
	Dprintk("NMI sent.\n");

	if (send_status)
		printk("APIC never delivered???\n");
	if (accept_status)
		printk("APIC delivery error (%lx).\n", accept_status);

	return (send_status | accept_status);
}
#endif	/* WAKE_SECONDARY_VIA_NMI */

#ifdef WAKE_SECONDARY_VIA_INIT
static int __devinit
wakeup_secondary_cpu(int phys_apicid, unsigned long start_eip)
{
	unsigned long send_status, accept_status = 0;
	int maxlvt, num_starts, j;

	/*
	 * Be paranoid about clearing APIC errors.
	 */
	if (APIC_INTEGRATED(apic_version[phys_apicid])) {
		apic_read_around(APIC_SPIV);
		apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
	}

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
	send_status = safe_apic_wait_icr_idle();

	mdelay(10);

	Dprintk("Deasserting INIT.\n");

	/* Target chip */
	apic_write_around(APIC_ICR2, SET_APIC_DEST_FIELD(phys_apicid));

	/* Send IPI */
	apic_write_around(APIC_ICR, APIC_INT_LEVELTRIG | APIC_DM_INIT);

	Dprintk("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	mb();
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
	 * Paravirt / VMI wants a startup IPI hook here to set up the
	 * target processor state.
	 */
	startup_ipi_hook(phys_apicid, (unsigned long) start_secondary,
		         (unsigned long) stack_start.sp);

	/*
	 * Run STARTUP IPI loop.
	 */
	Dprintk("#startup loops: %d.\n", num_starts);

	maxlvt = lapic_get_maxlvt();

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
					| (start_eip >> 12));

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		udelay(300);

		Dprintk("Startup point 1.\n");

		Dprintk("Waiting for send to finish...\n");
		send_status = safe_apic_wait_icr_idle();

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
		printk("APIC never delivered???\n");
	if (accept_status)
		printk("APIC delivery error (%lx).\n", accept_status);

	return (send_status | accept_status);
}
#endif	/* WAKE_SECONDARY_VIA_INIT */

extern cpumask_t cpu_initialized;

struct create_idle {
	struct work_struct work;
	struct task_struct *idle;
	struct completion done;
	int cpu;
};

static void __cpuinit do_fork_idle(struct work_struct *work)
{
	struct create_idle *c_idle =
		container_of(work, struct create_idle, work);

	c_idle->idle = fork_idle(c_idle->cpu);
	complete(&c_idle->done);
}
static int __cpuinit do_boot_cpu(int apicid, int cpu)
/*
 * NOTE - on most systems this is a PHYSICAL apic ID, but on multiquad
 * (ie clustered apic addressing mode), this is a LOGICAL apic ID.
 * Returns zero if CPU booted OK, else error code from wakeup_secondary_cpu.
 */
{
	unsigned long boot_error = 0;
	int timeout;
	unsigned long start_eip;
	unsigned short nmi_high = 0, nmi_low = 0;
	struct create_idle c_idle = {
		.cpu = cpu,
		.done = COMPLETION_INITIALIZER_ONSTACK(c_idle.done),
	};
	INIT_WORK(&c_idle.work, do_fork_idle);

	alternatives_smp_switch(1);

	c_idle.idle = get_idle_for_cpu(cpu);

	/*
	 * We can't use kernel_thread since we must avoid to
	 * reschedule the child.
	 */
	if (c_idle.idle) {
		c_idle.idle->thread.sp = (unsigned long) (((struct pt_regs *)
			(THREAD_SIZE +  task_stack_page(c_idle.idle))) - 1);
		init_idle(c_idle.idle, cpu);
		goto do_rest;
	}

	if (!keventd_up() || current_is_keventd())
		c_idle.work.func(&c_idle.work);
	else {
		schedule_work(&c_idle.work);
		wait_for_completion(&c_idle.done);
	}

	if (IS_ERR(c_idle.idle)) {
		printk(KERN_ERR "failed fork for CPU %d\n", cpu);
		return PTR_ERR(c_idle.idle);
	}

	set_idle_for_cpu(cpu, c_idle.idle);
do_rest:
	per_cpu(current_task, cpu) = c_idle.idle;
	init_gdt(cpu);
	early_gdt_descr.address = (unsigned long)get_cpu_gdt_table(cpu);

	c_idle.idle->thread.ip = (unsigned long) start_secondary;
	/* start_eip had better be page-aligned! */
	start_eip = setup_trampoline();

	/* So we see what's up   */
	printk("Booting processor %d/%d ip %lx\n", cpu, apicid, start_eip);
	/* Stack for startup_32 can be just as for start_secondary onwards */
	stack_start.sp = (void *) c_idle.idle->thread.sp;

	irq_ctx_init(cpu);

	/*
	 * This grunge runs the startup process for
	 * the targeted processor.
	 */

	atomic_set(&init_deasserted, 0);

	Dprintk("Setting warm reset code and vector.\n");

	store_NMI_vector(&nmi_high, &nmi_low);

	smpboot_setup_warm_reset_vector(start_eip);
	/*
	 * Be paranoid about clearing APIC errors.
	 */
	apic_write(APIC_ESR, 0);
	apic_read(APIC_ESR);


	/*
	 * Starting actual IPI sequence...
	 */
	boot_error = wakeup_secondary_cpu(apicid, start_eip);

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
			printk("CPU%d: ", cpu);
			print_cpu_info(&cpu_data(cpu));
			Dprintk("CPU has booted.\n");
		} else {
			boot_error= 1;
			if (*((volatile unsigned char *)trampoline_base)
					== 0xA5)
				/* trampoline started but...? */
				printk("Stuck ??\n");
			else
				/* trampoline code not run */
				printk("Not responding.\n");
			inquire_remote_apic(apicid);
		}
	}

	if (boot_error) {
		/* Try to put things back the way they were before ... */
		unmap_cpu_to_logical_apicid(cpu);
		cpu_clear(cpu, cpu_callout_map); /* was set here (do_boot_cpu()) */
		cpu_clear(cpu, cpu_initialized); /* was set by cpu_init() */
		cpu_clear(cpu, cpu_possible_map);
		per_cpu(x86_cpu_to_apicid, cpu) = BAD_APICID;
	}

	/* mark "stuck" area as not stuck */
	*((volatile unsigned long *)trampoline_base) = 0;

	return boot_error;
}

#ifdef CONFIG_HOTPLUG_CPU
void cpu_exit_clear(void)
{
	int cpu = raw_smp_processor_id();

	idle_task_exit();

	cpu_uninit();
	irq_ctx_exit(cpu);

	cpu_clear(cpu, cpu_callout_map);
	cpu_clear(cpu, cpu_callin_map);

	unmap_cpu_to_logical_apicid(cpu);
}
#endif

static int boot_cpu_logical_apicid;
/* Where the IO area was mapped on multiquad, always 0 otherwise */
void *xquad_portio;
#ifdef CONFIG_X86_NUMAQ
EXPORT_SYMBOL(xquad_portio);
#endif

static void __init disable_smp(void)
{
	cpu_possible_map = cpumask_of_cpu(0);
	cpu_present_map = cpumask_of_cpu(0);
	smpboot_clear_io_apic_irqs();
	phys_cpu_present_map = physid_mask_of_physid(0);
	map_cpu_to_logical_apicid();
	cpu_set(0, per_cpu(cpu_sibling_map, 0));
	cpu_set(0, per_cpu(cpu_core_map, 0));
}

static int __init smp_sanity_check(unsigned max_cpus)
{
	/*
	 * If we couldn't find an SMP configuration at boot time,
	 * get out of here now!
	 */
	if (!smp_found_config && !acpi_lapic) {
		printk(KERN_NOTICE "SMP motherboard not detected.\n");
		disable_smp();
		if (APIC_init_uniprocessor())
			printk(KERN_NOTICE "Local APIC not detected."
					   " Using dummy APIC emulation.\n");
		return -1;
	}

	/*
	 * Should not be necessary because the MP table should list the boot
	 * CPU too, but we do it for the sake of robustness anyway.
	 * Makes no sense to do this check in clustered apic mode, so skip it
	 */
	if (!check_phys_apicid_present(boot_cpu_physical_apicid)) {
		printk("weird, boot CPU (#%d) not listed by the BIOS.\n",
				boot_cpu_physical_apicid);
		physid_set(hard_smp_processor_id(), phys_cpu_present_map);
	}

	/*
	 * If we couldn't find a local APIC, then get out of here now!
	 */
	if (APIC_INTEGRATED(apic_version[boot_cpu_physical_apicid]) && !cpu_has_apic) {
		printk(KERN_ERR "BIOS bug, local APIC #%d not detected!...\n",
			boot_cpu_physical_apicid);
		printk(KERN_ERR "... forcing use of dummy APIC emulation. (tell your hw vendor)\n");
		return -1;
	}

	verify_local_APIC();

	/*
	 * If SMP should be disabled, then really disable it!
	 */
	if (!max_cpus) {
		smp_found_config = 0;
		printk(KERN_INFO "SMP mode deactivated, forcing use of dummy APIC emulation.\n");

		if (nmi_watchdog == NMI_LOCAL_APIC) {
			printk(KERN_INFO "activating minimal APIC for NMI watchdog use.\n");
			connect_bsp_APIC();
			setup_local_APIC();
			end_local_APIC_setup();
		}
		return -1;
	}
	return 0;
}

/*
 * Cycle through the processors sending APIC IPIs to boot each.
 */
static void __init smp_boot_cpus(unsigned int max_cpus)
{
	/*
	 * Setup boot CPU information
	 */
	smp_store_cpu_info(0); /* Final full version of the data */
	printk(KERN_INFO "CPU%d: ", 0);
	print_cpu_info(&cpu_data(0));

	boot_cpu_physical_apicid = GET_APIC_ID(apic_read(APIC_ID));
	boot_cpu_logical_apicid = logical_smp_processor_id();

	current_thread_info()->cpu = 0;

	set_cpu_sibling_map(0);

	if (smp_sanity_check(max_cpus) < 0) {
		printk(KERN_INFO "SMP disabled\n");
		disable_smp();
		return;
	}

	connect_bsp_APIC();
	setup_local_APIC();
	end_local_APIC_setup();
	map_cpu_to_logical_apicid();


	setup_portio_remap();

	smpboot_setup_io_apic();

	setup_boot_clock();
}

/* These are wrappers to interface to the new boot process.  Someone
   who understands all this stuff should rewrite it properly. --RR 15/Jul/02 */
void __init native_smp_prepare_cpus(unsigned int max_cpus)
{
	nmi_watchdog_default();
	cpu_callin_map = cpumask_of_cpu(0);
	mb();
	smp_boot_cpus(max_cpus);
}

void __init native_smp_prepare_boot_cpu(void)
{
	unsigned int cpu = smp_processor_id();

	init_gdt(cpu);
	switch_to_new_gdt();

	cpu_set(cpu, cpu_callout_map);
	__get_cpu_var(cpu_state) = CPU_ONLINE;
}

int __cpuinit native_cpu_up(unsigned int cpu)
{
	int apicid = cpu_present_to_apicid(cpu);
	unsigned long flags;
	int err;

	WARN_ON(irqs_disabled());

	Dprintk("++++++++++++++++++++=_---CPU UP  %u\n", cpu);

	if (apicid == BAD_APICID || apicid == boot_cpu_physical_apicid ||
	    !physid_isset(apicid, phys_cpu_present_map)) {
		printk(KERN_ERR "%s: bad cpu %d\n", __func__, cpu);
		return -EINVAL;
	}

	/*
	 * Already booted CPU?
	 */
	if (cpu_isset(cpu, cpu_callin_map)) {
		Dprintk("do_boot_cpu %d Already started\n", cpu);
		return -ENOSYS;
	}

	/*
	 * Save current MTRR state in case it was changed since early boot
	 * (e.g. by the ACPI SMI) to initialize new CPUs with MTRRs in sync:
	 */
	mtrr_save_state();

	per_cpu(cpu_state, cpu) = CPU_UP_PREPARE;

	/* init low mem mapping */
	clone_pgd_range(swapper_pg_dir, swapper_pg_dir + USER_PGD_PTRS,
			min_t(unsigned long, KERNEL_PGD_PTRS, USER_PGD_PTRS));
	flush_tlb_all();

	err = do_boot_cpu(apicid, cpu);
	if (err < 0) {
		Dprintk("do_boot_cpu failed %d\n", err);
		return err;
	}

	/*
	 * Check TSC synchronization with the AP (keep irqs disabled
	 * while doing so):
	 */
	local_irq_save(flags);
	check_tsc_sync_source(cpu);
	local_irq_restore(flags);

	while (!cpu_isset(cpu, cpu_online_map)) {
		cpu_relax();
		touch_nmi_watchdog();
	}

	return 0;
}

extern void impress_friends(void);
extern void smp_checks(void);

void __init native_smp_cpus_done(unsigned int max_cpus)
{
	/*
	 * Cleanup possible dangling ends...
	 */
	smpboot_restore_warm_reset_vector();

	Dprintk("Boot done.\n");

	impress_friends();
	smp_checks();
#ifdef CONFIG_X86_IO_APIC
	setup_ioapic_dest();
#endif
	check_nmi_watchdog();
	zap_low_mappings();
}
