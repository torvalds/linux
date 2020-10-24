// SPDX-License-Identifier: GPL-2.0
/*
 * NOOP APIC driver.
 *
 * Does almost nothing and should be substituted by a real apic driver via
 * probe routine.
 *
 * Though in case if apic is disabled (for some reason) we try
 * to not uglify the caller's code and allow to call (some) apic routines
 * like self-ipi, etc...
 */
#include <linux/cpumask.h>
#include <linux/thread_info.h>

#include <asm/apic.h>

static void noop_init_apic_ldr(void) { }
static void noop_send_IPI(int cpu, int vector) { }
static void noop_send_IPI_mask(const struct cpumask *cpumask, int vector) { }
static void noop_send_IPI_mask_allbutself(const struct cpumask *cpumask, int vector) { }
static void noop_send_IPI_allbutself(int vector) { }
static void noop_send_IPI_all(int vector) { }
static void noop_send_IPI_self(int vector) { }
static void noop_apic_wait_icr_idle(void) { }
static void noop_apic_icr_write(u32 low, u32 id) { }

static int noop_wakeup_secondary_cpu(int apicid, unsigned long start_eip)
{
	return -1;
}

static u32 noop_safe_apic_wait_icr_idle(void)
{
	return 0;
}

static u64 noop_apic_icr_read(void)
{
	return 0;
}

static int noop_phys_pkg_id(int cpuid_apic, int index_msb)
{
	return 0;
}

static unsigned int noop_get_apic_id(unsigned long x)
{
	return 0;
}

static int noop_probe(void)
{
	/*
	 * NOOP apic should not ever be
	 * enabled via probe routine
	 */
	return 0;
}

static int noop_apic_id_registered(void)
{
	/*
	 * if we would be really "pedantic"
	 * we should pass read_apic_id() here
	 * but since NOOP suppose APIC ID = 0
	 * lets save a few cycles
	 */
	return physid_isset(0, phys_cpu_present_map);
}

static u32 noop_apic_read(u32 reg)
{
	WARN_ON_ONCE(boot_cpu_has(X86_FEATURE_APIC) && !disable_apic);
	return 0;
}

static void noop_apic_write(u32 reg, u32 v)
{
	WARN_ON_ONCE(boot_cpu_has(X86_FEATURE_APIC) && !disable_apic);
}

#ifdef CONFIG_X86_32
static int noop_x86_32_early_logical_apicid(int cpu)
{
	return BAD_APICID;
}
#endif

struct apic apic_noop __ro_after_init = {
	.name				= "noop",
	.probe				= noop_probe,
	.acpi_madt_oem_check		= NULL,

	.apic_id_valid			= default_apic_id_valid,
	.apic_id_registered		= noop_apic_id_registered,

	.delivery_mode			= APIC_DELIVERY_MODE_FIXED,
	/* logical delivery broadcast to all CPUs: */
	.irq_dest_mode			= 1,

	.disable_esr			= 0,
	.dest_logical			= APIC_DEST_LOGICAL,
	.check_apicid_used		= default_check_apicid_used,

	.init_apic_ldr			= noop_init_apic_ldr,

	.ioapic_phys_id_map		= default_ioapic_phys_id_map,
	.setup_apic_routing		= NULL,

	.cpu_present_to_apicid		= default_cpu_present_to_apicid,
	.apicid_to_cpu_present		= physid_set_mask_of_physid,

	.check_phys_apicid_present	= default_check_phys_apicid_present,

	.phys_pkg_id			= noop_phys_pkg_id,

	.get_apic_id			= noop_get_apic_id,
	.set_apic_id			= NULL,

	.calc_dest_apicid		= apic_flat_calc_apicid,

	.send_IPI			= noop_send_IPI,
	.send_IPI_mask			= noop_send_IPI_mask,
	.send_IPI_mask_allbutself	= noop_send_IPI_mask_allbutself,
	.send_IPI_allbutself		= noop_send_IPI_allbutself,
	.send_IPI_all			= noop_send_IPI_all,
	.send_IPI_self			= noop_send_IPI_self,

	.wakeup_secondary_cpu		= noop_wakeup_secondary_cpu,

	.inquire_remote_apic		= NULL,

	.read				= noop_apic_read,
	.write				= noop_apic_write,
	.eoi_write			= noop_apic_write,
	.icr_read			= noop_apic_icr_read,
	.icr_write			= noop_apic_icr_write,
	.wait_icr_idle			= noop_apic_wait_icr_idle,
	.safe_wait_icr_idle		= noop_safe_apic_wait_icr_idle,

#ifdef CONFIG_X86_32
	.x86_32_early_logical_apicid	= noop_x86_32_early_logical_apicid,
#endif
};
