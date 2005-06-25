/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Flat APIC subarch code.  Maximum 8 CPUs, logical delivery.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 * Ashok Raj <ashok.raj@intel.com>
 * 	Removed IPI broadcast shortcut to support CPU hotplug
 */
#include <linux/config.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <asm/smp.h>
#include <asm/ipi.h>

/*
 * The following permit choosing broadcast IPI shortcut v.s sending IPI only
 * to online cpus via the send_IPI_mask varient.
 * The mask version is my preferred option, since it eliminates a lot of
 * other extra code that would need to be written to cleanup intrs sent
 * to a CPU while offline.
 *
 * Sending broadcast introduces lots of trouble in CPU hotplug situations.
 * These IPI's are delivered to cpu's irrespective of their offline status
 * and could pickup stale intr data when these CPUS are turned online.
 *
 * Not using broadcast is a cleaner approach IMO, but Andi Kleen disagrees with
 * the idea of not using broadcast IPI's anymore. Hence the run time check
 * is introduced, on his request so we can choose an alternate mechanism.
 *
 * Initial wacky performance tests that collect cycle counts show
 * no increase in using mask v.s broadcast version. In fact they seem
 * identical in terms of cycle counts.
 *
 * if we need to use broadcast, we need to do the following.
 *
 * cli;
 * hold call_lock;
 * clear any pending IPI, just ack and clear all pending intr
 * set cpu_online_map;
 * release call_lock;
 * sti;
 *
 * The complicated dummy irq processing shown above is not required if
 * we didnt sent IPI's to wrong CPU's in the first place.
 *
 * - Ashok Raj <ashok.raj@intel.com>
 */
#ifdef CONFIG_HOTPLUG_CPU
#define DEFAULT_SEND_IPI	(1)
#else
#define DEFAULT_SEND_IPI	(0)
#endif

static int no_broadcast=DEFAULT_SEND_IPI;

static cpumask_t flat_target_cpus(void)
{
	return cpu_online_map;
}

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LDR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static void flat_init_apic_ldr(void)
{
	unsigned long val;
	unsigned long num, id;

	num = smp_processor_id();
	id = 1UL << num;
	x86_cpu_to_log_apicid[num] = id;
	apic_write_around(APIC_DFR, APIC_DFR_FLAT);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val |= SET_APIC_LOGICAL_ID(id);
	apic_write_around(APIC_LDR, val);
}

static void flat_send_IPI_mask(cpumask_t cpumask, int vector)
{
	unsigned long mask = cpus_addr(cpumask)[0];
	unsigned long cfg;
	unsigned long flags;

	local_save_flags(flags);
	local_irq_disable();

	/*
	 * Wait for idle.
	 */
	apic_wait_icr_idle();

	/*
	 * prepare target chip field
	 */
	cfg = __prepare_ICR2(mask);
	apic_write_around(APIC_ICR2, cfg);

	/*
	 * program the ICR
	 */
	cfg = __prepare_ICR(0, vector, APIC_DEST_LOGICAL);

	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write_around(APIC_ICR, cfg);
	local_irq_restore(flags);
}

static inline void __local_flat_send_IPI_allbutself(int vector)
{
	if (no_broadcast) {
		cpumask_t mask = cpu_online_map;
		int this_cpu = get_cpu();

		cpu_clear(this_cpu, mask);
		flat_send_IPI_mask(mask, vector);
		put_cpu();
	}
	else
		__send_IPI_shortcut(APIC_DEST_ALLBUT, vector, APIC_DEST_LOGICAL);
}

static inline void __local_flat_send_IPI_all(int vector)
{
	if (no_broadcast)
		flat_send_IPI_mask(cpu_online_map, vector);
	else
		__send_IPI_shortcut(APIC_DEST_ALLINC, vector, APIC_DEST_LOGICAL);
}

static void flat_send_IPI_allbutself(int vector)
{
	if (((num_online_cpus()) - 1) >= 1)
		__local_flat_send_IPI_allbutself(vector);
}

static void flat_send_IPI_all(int vector)
{
	__local_flat_send_IPI_all(vector);
}

static int flat_apic_id_registered(void)
{
	return physid_isset(GET_APIC_ID(apic_read(APIC_ID)), phys_cpu_present_map);
}

static unsigned int flat_cpu_mask_to_apicid(cpumask_t cpumask)
{
	return cpus_addr(cpumask)[0] & APIC_ALL_CPUS;
}

static unsigned int phys_pkg_id(int index_msb)
{
	u32 ebx;

	ebx = cpuid_ebx(1);
	return ((ebx >> 24) & 0xFF) >> index_msb;
}

static __init int no_ipi_broadcast(char *str)
{
	get_option(&str, &no_broadcast);
	printk ("Using %s mode\n", no_broadcast ? "No IPI Broadcast" :
											"IPI Broadcast");
	return 1;
}

__setup("no_ipi_broadcast", no_ipi_broadcast);

struct genapic apic_flat =  {
	.name = "flat",
	.int_delivery_mode = dest_LowestPrio,
	.int_dest_mode = (APIC_DEST_LOGICAL != 0),
	.int_delivery_dest = APIC_DEST_LOGICAL | APIC_DM_LOWEST,
	.target_cpus = flat_target_cpus,
	.apic_id_registered = flat_apic_id_registered,
	.init_apic_ldr = flat_init_apic_ldr,
	.send_IPI_all = flat_send_IPI_all,
	.send_IPI_allbutself = flat_send_IPI_allbutself,
	.send_IPI_mask = flat_send_IPI_mask,
	.cpu_mask_to_apicid = flat_cpu_mask_to_apicid,
	.phys_pkg_id = phys_pkg_id,
};

static int __init print_ipi_mode(void)
{
	printk ("Using IPI %s mode\n", no_broadcast ? "No-Shortcut" :
											"Shortcut");
	return 0;
}

late_initcall(print_ipi_mode);
