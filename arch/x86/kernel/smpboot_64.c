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
 *	This code is released under the GNU General Public License version 2
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
 *      Andi Kleen              :       Converted to new state machine.
 *					Various cleanups.
 *					Probably mostly hotplug CPU ready now.
 *	Ashok Raj			: CPU hotplug support
 */


#include <linux/init.h>

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/bootmem.h>
#include <linux/thread_info.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mc146818rtc.h>
#include <linux/smp.h>
#include <linux/kdebug.h>

#include <asm/mtrr.h>
#include <asm/pgalloc.h>
#include <asm/desc.h>
#include <asm/tlbflush.h>
#include <asm/proto.h>
#include <asm/nmi.h>
#include <asm/irq.h>
#include <asm/hw_irq.h>
#include <asm/numa.h>

#include <mach_wakecpu.h>
#include <mach_apic.h>
#include <smpboot_hooks.h>
#include <mach_apic.h>

/* Set when the idlers are all forked */
int smp_threads_ready;

cycles_t cacheflush_time;
unsigned long cache_decay_ticks;

/*
 * Fall back to non SMP mode after errors.
 *
 * RED-PEN audit/test this more. I bet there is more state messed up here.
 */
static __init void disable_smp(void)
{
	cpu_present_map = cpumask_of_cpu(0);
	cpu_possible_map = cpumask_of_cpu(0);
	if (smp_found_config)
		phys_cpu_present_map =
				physid_mask_of_physid(boot_cpu_physical_apicid);
	else
		phys_cpu_present_map = physid_mask_of_physid(0);
	cpu_set(0, per_cpu(cpu_sibling_map, 0));
	cpu_set(0, per_cpu(cpu_core_map, 0));
}

/*
 * Various sanity checks.
 */
static int __init smp_sanity_check(unsigned max_cpus)
{
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
		disable_smp();
		if (APIC_init_uniprocessor())
			printk(KERN_NOTICE "Local APIC not detected."
					   " Using dummy APIC emulation.\n");
		return -1;
	}

	/*
	 * Should not be necessary because the MP table should list the boot
	 * CPU too, but we do it for the sake of robustness anyway.
	 */
	if (!physid_isset(boot_cpu_physical_apicid, phys_cpu_present_map)) {
		printk(KERN_NOTICE
			"weird, boot CPU (#%d) not listed by the BIOS.\n",
			boot_cpu_physical_apicid);
		physid_set(hard_smp_processor_id(), phys_cpu_present_map);
	}

	/*
	 * If we couldn't find a local APIC, then get out of here now!
	 */
	if (!cpu_has_apic) {
		printk(KERN_ERR "BIOS bug, local APIC #%d not detected!...\n",
			boot_cpu_physical_apicid);
		printk(KERN_ERR "... forcing use of dummy APIC emulation. (tell your hw vendor)\n");
		nr_ioapics = 0;
		return -1;
	}

	/*
	 * If SMP should be disabled, then really disable it!
	 */
	if (!max_cpus) {
		printk(KERN_INFO "SMP mode deactivated, forcing use of dummy APIC emulation.\n");
		nr_ioapics = 0;
		return -1;
	}

	return 0;
}

static void __init smp_cpu_index_default(void)
{
	int i;
	struct cpuinfo_x86 *c;

	for_each_cpu_mask(i, cpu_possible_map) {
		c = &cpu_data(i);
		/* mark all to hotplug */
		c->cpu_index = NR_CPUS;
	}
}

/*
 * Prepare for SMP bootup.  The MP table or ACPI has been read
 * earlier.  Just do some sanity checking here and enable APIC mode.
 */
void __init native_smp_prepare_cpus(unsigned int max_cpus)
{
	nmi_watchdog_default();
	smp_cpu_index_default();
	current_cpu_data = boot_cpu_data;
	current_thread_info()->cpu = 0;  /* needed? */
	set_cpu_sibling_map(0);

	if (smp_sanity_check(max_cpus) < 0) {
		printk(KERN_INFO "SMP disabled\n");
		disable_smp();
		return;
	}


	/*
	 * Switch from PIC to APIC mode.
	 */
	setup_local_APIC();

	/*
	 * Enable IO APIC before setting up error vector
	 */
	if (!skip_ioapic_setup && nr_ioapics)
		enable_IO_APIC();
	end_local_APIC_setup();

	if (GET_APIC_ID(apic_read(APIC_ID)) != boot_cpu_physical_apicid) {
		panic("Boot APIC ID in local APIC unexpected (%d vs %d)",
		     GET_APIC_ID(apic_read(APIC_ID)), boot_cpu_physical_apicid);
		/* Or can we switch back to PIC here? */
	}

	/*
	 * Now start the IO-APICs
	 */
	if (!skip_ioapic_setup && nr_ioapics)
		setup_IO_APIC();
	else
		nr_ioapics = 0;

	/*
	 * Set up local APIC timer on boot CPU.
	 */

	setup_boot_clock();
	printk(KERN_INFO "CPU%d: ", 0);
	print_cpu_info(&cpu_data(0));
}
