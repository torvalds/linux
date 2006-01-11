/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Flat APIC subarch code.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
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
	apic_write(APIC_DFR, APIC_DFR_FLAT);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val |= SET_APIC_LOGICAL_ID(id);
	apic_write(APIC_LDR, val);
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
	apic_write(APIC_ICR2, cfg);

	/*
	 * program the ICR
	 */
	cfg = __prepare_ICR(0, vector, APIC_DEST_LOGICAL);

	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write(APIC_ICR, cfg);
	local_irq_restore(flags);
}

static void flat_send_IPI_allbutself(int vector)
{
#ifndef CONFIG_HOTPLUG_CPU
	if (((num_online_cpus()) - 1) >= 1)
		__send_IPI_shortcut(APIC_DEST_ALLBUT, vector,APIC_DEST_LOGICAL);
#else
	cpumask_t allbutme = cpu_online_map;

	cpu_clear(smp_processor_id(), allbutme);

	if (!cpus_empty(allbutme))
		flat_send_IPI_mask(allbutme, vector);
#endif
}

static void flat_send_IPI_all(int vector)
{
	__send_IPI_shortcut(APIC_DEST_ALLINC, vector, APIC_DEST_LOGICAL);
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

/*
 * Physflat mode is used when there are more than 8 CPUs on a AMD system.
 * We cannot use logical delivery in this case because the mask
 * overflows, so use physical mode.
 */

static cpumask_t physflat_target_cpus(void)
{
	return cpumask_of_cpu(0);
}

static void physflat_send_IPI_mask(cpumask_t cpumask, int vector)
{
	send_IPI_mask_sequence(cpumask, vector);
}

static void physflat_send_IPI_allbutself(int vector)
{
	cpumask_t allbutme = cpu_online_map;

	cpu_clear(smp_processor_id(), allbutme);
	physflat_send_IPI_mask(allbutme, vector);
}

static void physflat_send_IPI_all(int vector)
{
	physflat_send_IPI_mask(cpu_online_map, vector);
}

static unsigned int physflat_cpu_mask_to_apicid(cpumask_t cpumask)
{
	int cpu;

	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	cpu = first_cpu(cpumask);
	if ((unsigned)cpu < NR_CPUS)
		return x86_cpu_to_apicid[cpu];
	else
		return BAD_APICID;
}

struct genapic apic_physflat =  {
	.name = "physical flat",
	.int_delivery_mode = dest_Fixed,
	.int_dest_mode = (APIC_DEST_PHYSICAL != 0),
	.int_delivery_dest = APIC_DEST_PHYSICAL | APIC_DM_FIXED,
	.target_cpus = physflat_target_cpus,
	.apic_id_registered = flat_apic_id_registered,
	.init_apic_ldr = flat_init_apic_ldr,/*not needed, but shouldn't hurt*/
	.send_IPI_all = physflat_send_IPI_all,
	.send_IPI_allbutself = physflat_send_IPI_allbutself,
	.send_IPI_mask = physflat_send_IPI_mask,
	.cpu_mask_to_apicid = physflat_cpu_mask_to_apicid,
	.phys_pkg_id = phys_pkg_id,
};
