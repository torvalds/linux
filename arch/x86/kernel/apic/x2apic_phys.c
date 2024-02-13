// SPDX-License-Identifier: GPL-2.0

#include <linux/cpumask.h>
#include <linux/acpi.h>

#include "local.h"

int x2apic_phys;

static struct apic apic_x2apic_phys;
u32 x2apic_max_apicid __ro_after_init = UINT_MAX;

void __init x2apic_set_max_apicid(u32 apicid)
{
	x2apic_max_apicid = apicid;
	if (apic->x2apic_set_max_apicid)
		apic->max_apic_id = apicid;
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

	/* x2apic MSRs are special and need a special fence: */
	weak_wrmsr_fence();
	__x2apic_send_IPI_dest(dest, vector, APIC_DEST_PHYSICAL);
}

static void
__x2apic_send_IPI_mask(const struct cpumask *mask, int vector, int apic_dest)
{
	unsigned long query_cpu;
	unsigned long this_cpu;
	unsigned long flags;

	/* x2apic MSRs are special and need a special fence: */
	weak_wrmsr_fence();

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

static void __x2apic_send_IPI_shorthand(int vector, u32 which)
{
	unsigned long cfg = __prepare_ICR(which, vector, 0);

	/* x2apic MSRs are special and need a special fence: */
	weak_wrmsr_fence();
	native_x2apic_icr_write(cfg, 0);
}

void x2apic_send_IPI_allbutself(int vector)
{
	__x2apic_send_IPI_shorthand(vector, APIC_DEST_ALLBUT);
}

void x2apic_send_IPI_all(int vector)
{
	__x2apic_send_IPI_shorthand(vector, APIC_DEST_ALLINC);
}

void x2apic_send_IPI_self(int vector)
{
	apic_write(APIC_SELF_IPI, vector);
}

void __x2apic_send_IPI_dest(unsigned int apicid, int vector, unsigned int dest)
{
	unsigned long cfg = __prepare_ICR(0, vector, dest);
	native_x2apic_icr_write(cfg, apicid);
}

static int x2apic_phys_probe(void)
{
	if (!x2apic_mode)
		return 0;

	if (x2apic_phys || x2apic_fadt_phys())
		return 1;

	return apic == &apic_x2apic_phys;
}

u32 x2apic_get_apic_id(u32 id)
{
	return id;
}

static struct apic apic_x2apic_phys __ro_after_init = {

	.name				= "physical x2apic",
	.probe				= x2apic_phys_probe,
	.acpi_madt_oem_check		= x2apic_acpi_madt_oem_check,

	.dest_mode_logical		= false,

	.disable_esr			= 0,

	.cpu_present_to_apicid		= default_cpu_present_to_apicid,

	.max_apic_id			= UINT_MAX,
	.x2apic_set_max_apicid		= true,
	.get_apic_id			= x2apic_get_apic_id,

	.calc_dest_apicid		= apic_default_calc_apicid,

	.send_IPI			= x2apic_send_IPI,
	.send_IPI_mask			= x2apic_send_IPI_mask,
	.send_IPI_mask_allbutself	= x2apic_send_IPI_mask_allbutself,
	.send_IPI_allbutself		= x2apic_send_IPI_allbutself,
	.send_IPI_all			= x2apic_send_IPI_all,
	.send_IPI_self			= x2apic_send_IPI_self,
	.nmi_to_offline_cpu		= true,

	.read				= native_apic_msr_read,
	.write				= native_apic_msr_write,
	.eoi				= native_apic_msr_eoi,
	.icr_read			= native_x2apic_icr_read,
	.icr_write			= native_x2apic_icr_write,
};

apic_driver(apic_x2apic_phys);
