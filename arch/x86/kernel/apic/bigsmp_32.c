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

static unsigned long bigsmp_check_apicid_used(physid_mask_t *map, int apicid)
{
	return 0;
}

static int bigsmp_early_logical_apicid(int cpu)
{
	/* on bigsmp, logical apicid is the same as physical */
	return early_per_cpu(x86_cpu_to_apicid, cpu);
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

static int bigsmp_cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < nr_cpu_ids)
		return (int) per_cpu(x86_bios_cpu_apicid, mps_cpu);

	return BAD_APICID;
}

static void bigsmp_ioapic_phys_id_map(physid_mask_t *phys_map, physid_mask_t *retmap)
{
	/* For clustered we don't have a good way to do this yet - hack */
	physids_promote(0xFFL, retmap);
}

static int bigsmp_check_phys_apicid_present(int phys_apicid)
{
	return 1;
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

static int probe_bigsmp(void)
{
	if (def_to_bigsmp)
		dmi_bigsmp = 1;
	else
		dmi_check_system(bigsmp_dmi_table);

	return dmi_bigsmp;
}

static struct apic apic_bigsmp = {

	.name				= "bigsmp",
	.probe				= probe_bigsmp,
	.acpi_madt_oem_check		= NULL,
	.apic_id_valid			= default_apic_id_valid,
	.apic_id_registered		= bigsmp_apic_id_registered,

	.irq_delivery_mode		= dest_Fixed,
	/* phys delivery to target CPU: */
	.irq_dest_mode			= 0,

	.target_cpus			= default_target_cpus,
	.disable_esr			= 1,
	.dest_logical			= 0,
	.check_apicid_used		= bigsmp_check_apicid_used,

	.vector_allocation_domain	= default_vector_allocation_domain,
	.init_apic_ldr			= bigsmp_init_apic_ldr,

	.ioapic_phys_id_map		= bigsmp_ioapic_phys_id_map,
	.setup_apic_routing		= bigsmp_setup_apic_routing,
	.cpu_present_to_apicid		= bigsmp_cpu_present_to_apicid,
	.apicid_to_cpu_present		= physid_set_mask_of_physid,
	.setup_portio_remap		= NULL,
	.check_phys_apicid_present	= bigsmp_check_phys_apicid_present,
	.enable_apic_mode		= NULL,
	.phys_pkg_id			= bigsmp_phys_pkg_id,

	.get_apic_id			= bigsmp_get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= 0xFF << 24,

	.cpu_mask_to_apicid_and		= default_cpu_mask_to_apicid_and,

	.send_IPI_mask			= bigsmp_send_IPI_mask,
	.send_IPI_mask_allbutself	= NULL,
	.send_IPI_allbutself		= bigsmp_send_IPI_allbutself,
	.send_IPI_all			= bigsmp_send_IPI_all,
	.send_IPI_self			= default_send_IPI_self,

	.wait_for_init_deassert		= true,
	.inquire_remote_apic		= default_inquire_remote_apic,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.eoi_write			= native_apic_mem_write,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= native_apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_apic_wait_icr_idle,

	.x86_32_early_logical_apicid	= bigsmp_early_logical_apicid,
};

void __init generic_bigsmp_probe(void)
{
	unsigned int cpu;

	if (!probe_bigsmp())
		return;

	apic = &apic_bigsmp;

	for_each_possible_cpu(cpu) {
		if (early_per_cpu(x86_cpu_to_logical_apicid,
				  cpu) == BAD_APICID)
			continue;
		early_per_cpu(x86_cpu_to_logical_apicid, cpu) =
			bigsmp_early_logical_apicid(cpu);
	}

	pr_info("Overriding APIC driver with %s\n", apic_bigsmp.name);
}

apic_driver(apic_bigsmp);
