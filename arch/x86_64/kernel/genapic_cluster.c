/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Clustered APIC subarch code.  Up to 255 CPUs, physical delivery.
 * (A more realistic maximum is around 230 CPUs.)
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


/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LDR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static void cluster_init_apic_ldr(void)
{
	unsigned long val, id;
	long i, count;
	u8 lid;
	u8 my_id = hard_smp_processor_id();
	u8 my_cluster = APIC_CLUSTER(my_id);

	/* Create logical APIC IDs by counting CPUs already in cluster. */
	for (count = 0, i = NR_CPUS; --i >= 0; ) {
		lid = x86_cpu_to_log_apicid[i];
		if (lid != BAD_APICID && APIC_CLUSTER(lid) == my_cluster)
			++count;
	}
	/*
	 * We only have a 4 wide bitmap in cluster mode.  There's no way
	 * to get above 60 CPUs and still give each one it's own bit.
	 * But, we're using physical IRQ delivery, so we don't care.
	 * Use bit 3 for the 4th through Nth CPU in each cluster.
	 */
	if (count >= XAPIC_DEST_CPUS_SHIFT)
		count = 3;
	id = my_cluster | (1UL << count);
	x86_cpu_to_log_apicid[smp_processor_id()] = id;
	apic_write(APIC_DFR, APIC_DFR_CLUSTER);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val |= SET_APIC_LOGICAL_ID(id);
	apic_write(APIC_LDR, val);
}

/* Start with all IRQs pointing to boot CPU.  IRQ balancing will shift them. */

static cpumask_t cluster_target_cpus(void)
{
	return cpumask_of_cpu(0);
}

static void cluster_send_IPI_mask(cpumask_t mask, int vector)
{
	send_IPI_mask_sequence(mask, vector);
}

static void cluster_send_IPI_allbutself(int vector)
{
	cpumask_t mask = cpu_online_map;
	int me = get_cpu(); /* Ensure we are not preempted when we clear */

	cpu_clear(me, mask);

	if (!cpus_empty(mask))
		cluster_send_IPI_mask(mask, vector);

	put_cpu();
}

static void cluster_send_IPI_all(int vector)
{
	cluster_send_IPI_mask(cpu_online_map, vector);
}

static int cluster_apic_id_registered(void)
{
	return 1;
}

static unsigned int cluster_cpu_mask_to_apicid(cpumask_t cpumask)
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

/* cpuid returns the value latched in the HW at reset, not the APIC ID
 * register's value.  For any box whose BIOS changes APIC IDs, like
 * clustered APIC systems, we must use hard_smp_processor_id.
 *
 * See Intel's IA-32 SW Dev's Manual Vol2 under CPUID.
 */
static unsigned int phys_pkg_id(int index_msb)
{
	return hard_smp_processor_id() >> index_msb;
}

struct genapic apic_cluster = {
	.name = "clustered",
	.int_delivery_mode = dest_Fixed,
	.int_dest_mode = (APIC_DEST_PHYSICAL != 0),
	.int_delivery_dest = APIC_DEST_PHYSICAL | APIC_DM_FIXED,
	.target_cpus = cluster_target_cpus,
	.apic_id_registered = cluster_apic_id_registered,
	.init_apic_ldr = cluster_init_apic_ldr,
	.send_IPI_all = cluster_send_IPI_all,
	.send_IPI_allbutself = cluster_send_IPI_allbutself,
	.send_IPI_mask = cluster_send_IPI_mask,
	.cpu_mask_to_apicid = cluster_cpu_mask_to_apicid,
	.phys_pkg_id = phys_pkg_id,
};
