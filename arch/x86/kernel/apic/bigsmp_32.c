/*
 * APIC driver for "bigsmp" xAPIC machines with more than 8 virtual CPUs.
 *
 * Drives the local APIC in "clustered mode".
 */
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/smp.h>

#include <asm/apicdef.h>
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#include <asm/apic.h>
#include <asm/ipi.h>

static unsigned bigsmp_get_apic_id(unsigned long x)
{
	return (x >> 24) & 0xFF;
}

static int bigsmp_apic_id_registered(void)
{
	return 1;
}

static const struct cpumask *bigsmp_target_cpus(void)
{
#ifdef CONFIG_SMP
	return cpu_online_mask;
#else
	return cpumask_of(0);
#endif
}

static unsigned long bigsmp_check_apicid_used(physid_mask_t bitmap, int apicid)
{
	return 0;
}

static unsigned long bigsmp_check_apicid_present(int bit)
{
	return 1;
}

static inline unsigned long calculate_ldr(int cpu)
{
	unsigned long val, id;

	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	id = per_cpu(x86_bios_cpu_apicid, cpu);
	val |= SET_APIC_LOGICAL_ID(id);

	return val;
}

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LDR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static void bigsmp_init_apic_ldr(void)
{
	unsigned long val;
	int cpu = smp_processor_id();

	apic_write(APIC_DFR, APIC_DFR_FLAT);
	val = calculate_ldr(cpu);
	apic_write(APIC_LDR, val);
}

static void bigsmp_setup_apic_routing(void)
{
	printk(KERN_INFO
		"Enabling APIC mode:  Physflat.  Using %d I/O APICs\n",
		nr_ioapics);
}

static int bigsmp_apicid_to_node(int logical_apicid)
{
	return apicid_2_node[hard_smp_processor_id()];
}

static int bigsmp_cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < nr_cpu_ids)
		return (int) per_cpu(x86_bios_cpu_apicid, mps_cpu);

	return BAD_APICID;
}

static physid_mask_t bigsmp_apicid_to_cpu_present(int phys_apicid)
{
	return physid_mask_of_physid(phys_apicid);
}

/* Mapping from cpu number to logical apicid */
static inline int bigsmp_cpu_to_logical_apicid(int cpu)
{
	if (cpu >= nr_cpu_ids)
		return BAD_APICID;
	return cpu_physical_id(cpu);
}

static physid_mask_t bigsmp_ioapic_phys_id_map(physid_mask_t phys_map)
{
	/* For clustered we don't have a good way to do this yet - hack */
	return physids_promote(0xFFL);
}

static int bigsmp_check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return 1;
}

/* As we are using single CPU as destination, pick only one CPU here */
static unsigned int bigsmp_cpu_mask_to_apicid(const struct cpumask *cpumask)
{
	return bigsmp_cpu_to_logical_apicid(cpumask_first(cpumask));
}

static unsigned int bigsmp_cpu_mask_to_apicid_and(const struct cpumask *cpumask,
			      const struct cpumask *andmask)
{
	int cpu;

	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	for_each_cpu_and(cpu, cpumask, andmask) {
		if (cpumask_test_cpu(cpu, cpu_online_mask))
			break;
	}
	if (cpu < nr_cpu_ids)
		return bigsmp_cpu_to_logical_apicid(cpu);

	return BAD_APICID;
}

static int bigsmp_phys_pkg_id(int cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

static inline void bigsmp_send_IPI_mask(const struct cpumask *mask, int vector)
{
	default_send_IPI_mask_sequence_phys(mask, vector);
}

static void bigsmp_send_IPI_allbutself(int vector)
{
	default_send_IPI_mask_allbutself_phys(cpu_online_mask, vector);
}

static void bigsmp_send_IPI_all(int vector)
{
	bigsmp_send_IPI_mask(cpu_online_mask, vector);
}

static int dmi_bigsmp; /* can be set by dmi scanners */

static int hp_ht_bigsmp(const struct dmi_system_id *d)
{
	printk(KERN_NOTICE "%s detected: force use of apic=bigsmp\n", d->ident);
	dmi_bigsmp = 1;

	return 0;
}


static const struct dmi_system_id bigsmp_dmi_table[] = {
	{ hp_ht_bigsmp, "HP ProLiant DL760 G2",
		{	DMI_MATCH(DMI_BIOS_VENDOR, "HP"),
			DMI_MATCH(DMI_BIOS_VERSION, "P44-"),
		}
	},

	{ hp_ht_bigsmp, "HP ProLiant DL740",
		{	DMI_MATCH(DMI_BIOS_VENDOR, "HP"),
			DMI_MATCH(DMI_BIOS_VERSION, "P47-"),
		}
	},
	{ } /* NULL entry stops DMI scanning */
};

static void bigsmp_vector_allocation_domain(int cpu, struct cpumask *retmask)
{
	cpumask_clear(retmask);
	cpumask_set_cpu(cpu, retmask);
}

static int probe_bigsmp(void)
{
	if (def_to_bigsmp)
		dmi_bigsmp = 1;
	else
		dmi_check_system(bigsmp_dmi_table);

	return dmi_bigsmp;
}

struct apic apic_bigsmp = {

	.name				= "bigsmp",
	.probe				= probe_bigsmp,
	.acpi_madt_oem_check		= NULL,
	.apic_id_registered		= bigsmp_apic_id_registered,

	.irq_delivery_mode		= dest_Fixed,
	/* phys delivery to target CPU: */
	.irq_dest_mode			= 0,

	.target_cpus			= bigsmp_target_cpus,
	.disable_esr			= 1,
	.dest_logical			= 0,
	.check_apicid_used		= bigsmp_check_apicid_used,
	.check_apicid_present		= bigsmp_check_apicid_present,

	.vector_allocation_domain	= bigsmp_vector_allocation_domain,
	.init_apic_ldr			= bigsmp_init_apic_ldr,

	.ioapic_phys_id_map		= bigsmp_ioapic_phys_id_map,
	.setup_apic_routing		= bigsmp_setup_apic_routing,
	.multi_timer_check		= NULL,
	.apicid_to_node			= bigsmp_apicid_to_node,
	.cpu_to_logical_apicid		= bigsmp_cpu_to_logical_apicid,
	.cpu_present_to_apicid		= bigsmp_cpu_present_to_apicid,
	.apicid_to_cpu_present		= bigsmp_apicid_to_cpu_present,
	.setup_portio_remap		= NULL,
	.check_phys_apicid_present	= bigsmp_check_phys_apicid_present,
	.enable_apic_mode		= NULL,
	.phys_pkg_id			= bigsmp_phys_pkg_id,
	.mps_oem_check			= NULL,

	.get_apic_id			= bigsmp_get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= 0xFF << 24,

	.cpu_mask_to_apicid		= bigsmp_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= bigsmp_cpu_mask_to_apicid_and,

	.send_IPI_mask			= bigsmp_send_IPI_mask,
	.send_IPI_mask_allbutself	= NULL,
	.send_IPI_allbutself		= bigsmp_send_IPI_allbutself,
	.send_IPI_all			= bigsmp_send_IPI_all,
	.send_IPI_self			= default_send_IPI_self,

	.trampoline_phys_low		= DEFAULT_TRAMPOLINE_PHYS_LOW,
	.trampoline_phys_high		= DEFAULT_TRAMPOLINE_PHYS_HIGH,

	.wait_for_init_deassert		= default_wait_for_init_deassert,

	.smp_callin_clear_local_apic	= NULL,
	.inquire_remote_apic		= default_inquire_remote_apic,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= native_apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_apic_wait_icr_idle,
};
