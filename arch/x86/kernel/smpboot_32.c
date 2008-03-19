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

extern void smp_callin(void);

/*
 * Activate a secondary processor.
 */
void __cpuinit start_secondary(void *unused)
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
