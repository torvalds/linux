// SPDX-License-Identifier: GPL-2.0

#include <linux/cpumask.h>
#include <linux/acpi.h>

#include "local.h"

int x2apic_phys;

static struct apic apic_x2apic_phys;
static u32 x2apic_max_apicid __ro_after_init;

void __init x2apic_set_max_apicid(u32 apicid)
{
	x2apic_max_apicid = apicid;
}

static int __init set_x2apic_phys_mode(char *arg)
{
	x2apic_phys = 1;
	return 0;
}
early_param("x2apic_phys", set_x2apic_phys_mode);

static bool x2apic_fadt_phys(void)
{
#ifdef CONFIG_ACPI
	if ((acpi_gbl_FADT.header.revision >= FADT2_REVISION_ID) &&
		(acpi_gbl_FADT.flags & ACPI_FADT_APIC_PHYSICAL)) {
		printk(KERN_DEBUG "System requires x2apic physical mode\n");
		return true;
	}
#endif
	return false;
}

static int x2apic_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return x2apic_enabled() && (x2apic_phys || x2apic_fadt_phys());
}

static void x2apic_send_IPI(int cpu, int vector)
{
	u32 dest = per_cpu(x86_cpu_to_apicid, cpu);

	x2apic_wrmsr_fence();
	__x2apic_send_IPI_dest(dest, vector, APIC_DEST_PHYSICAL);
}

static void
__x2apic_send_IPI_mask(const struct cpumask *mask, int vector, int apic_dest)
{
	unsigned long query_cpu;
	unsigned long this_cpu;
	unsigned long flags;

	x2apic_wrmsr_fence();

	local_irq_save(flags);

	this_cpu = smp_processor_id();
	for_each_cpu(query_cpu, mask) {
		if (apic_dest == APIC_DEST_ALLBUT && this_cpu == query_cpu)
			continue;
		__x2apic_send_IPI_dest(per_cpu(x86_cpu_to_apicid, query_cpu),
				       vector, APIC_DEST_PHYSICAL);
	}
	local_irq_restore(flags);
}

static void x2apic_send_IPI_mask(const struct cpumask *mask, int vector)
{
	__x2apic_send_IPI_mask(mask, vector, APIC_DEST_ALLINC);
}

static void
 x2apic_send_IPI_mask_allbutself(const struct cpumask *mask, int vector)
{
	__x2apic_send_IPI_mask(mask, vector, APIC_DEST_ALLBUT);
}

static void x2apic_send_IPI_allbutself(int vector)
{
	__x2apic_send_IPI_shorthand(vector, APIC_DEST_ALLBUT);
}

static void x2apic_send_IPI_all(int vector)
{
	__x2apic_send_IPI_shorthand(vector, APIC_DEST_ALLINC);
}

static void init_x2apic_ldr(void)
{
}

static int x2apic_phys_probe(void)
{
	if (x2apic_mode && (x2apic_phys || x2apic_fadt_phys()))
		return 1;

	return apic == &apic_x2apic_phys;
}

/* Common x2apic functions, also used by x2apic_cluster */
int x2apic_apic_id_valid(u32 apicid)
{
	if (x2apic_max_apicid && apicid > x2apic_max_apicid)
		return 0;

	return 1;
}

int x2apic_apic_id_registered(void)
{
	return 1;
}

void __x2apic_send_IPI_dest(unsigned int apicid, int vector, unsigned int dest)
{
	unsigned long cfg = __prepare_ICR(0, vector, dest);
	native_x2apic_icr_write(cfg, apicid);
}

void __x2apic_send_IPI_shorthand(int vector, u32 which)
{
	unsigned long cfg = __prepare_ICR(which, vector, 0);

	x2apic_wrmsr_fence();
	native_x2apic_icr_write(cfg, 0);
}

unsigned int x2apic_get_apic_id(unsigned long id)
{
	return id;
}

u32 x2apic_set_apic_id(unsigned int id)
{
	return id;
}

int x2apic_phys_pkg_id(int initial_apicid, int index_msb)
{
	return initial_apicid >> index_msb;
}

void x2apic_send_IPI_self(int vector)
{
	apic_write(APIC_SELF_IPI, vector);
}

static struct apic apic_x2apic_phys __ro_after_init = {

	.name				= "physical x2apic",
	.probe				= x2apic_phys_probe,
	.acpi_madt_oem_check		= x2apic_acpi_madt_oem_check,
	.apic_id_valid			= x2apic_apic_id_valid,
	.apic_id_registered		= x2apic_apic_id_registered,

	.irq_delivery_mode		= dest_Fixed,
	.irq_dest_mode			= 0, /* physical */

	.disable_esr			= 0,
	.dest_logical			= 0,
	.check_apicid_used		= NULL,

	.init_apic_ldr			= init_x2apic_ldr,

	.ioapic_phys_id_map		= NULL,
	.setup_apic_routing		= NULL,
	.cpu_present_to_apicid		= default_cpu_present_to_apicid,
	.apicid_to_cpu_present		= NULL,
	.check_phys_apicid_present	= default_check_phys_apicid_present,
	.phys_pkg_id			= x2apic_phys_pkg_id,

	.get_apic_id			= x2apic_get_apic_id,
	.set_apic_id			= x2apic_set_apic_id,

	.calc_dest_apicid		= apic_default_calc_apicid,

	.send_IPI			= x2apic_send_IPI,
	.send_IPI_mask			= x2apic_send_IPI_mask,
	.send_IPI_mask_allbutself	= x2apic_send_IPI_mask_allbutself,
	.send_IPI_allbutself		= x2apic_send_IPI_allbutself,
	.send_IPI_all			= x2apic_send_IPI_all,
	.send_IPI_self			= x2apic_send_IPI_self,

	.inquire_remote_apic		= NULL,

	.read				= native_apic_msr_read,
	.write				= native_apic_msr_write,
	.eoi_write			= native_apic_msr_eoi_write,
	.icr_read			= native_x2apic_icr_read,
	.icr_write			= native_x2apic_icr_write,
	.wait_icr_idle			= native_x2apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_x2apic_wait_icr_idle,
};

apic_driver(apic_x2apic_phys);
