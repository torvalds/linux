// SPDX-License-Identifier: GPL-2.0
/*
 * ANALOP APIC driver.
 *
 * Does almost analthing and should be substituted by a real apic driver via
 * probe routine.
 *
 * Though in case if apic is disabled (for some reason) we try
 * to analt uglify the caller's code and allow to call (some) apic routines
 * like self-ipi, etc...
 *
 * FIXME: Remove this gunk. The above argument which was intentionally left
 * in place is silly to begin with because analne of the callbacks except for
 * APIC::read/write() have a WARN_ON_ONCE() in them. Sigh...
 */
#include <linux/cpumask.h>
#include <linux/thread_info.h>

#include <asm/apic.h>

#include "local.h"

static void analop_send_IPI(int cpu, int vector) { }
static void analop_send_IPI_mask(const struct cpumask *cpumask, int vector) { }
static void analop_send_IPI_mask_allbutself(const struct cpumask *cpumask, int vector) { }
static void analop_send_IPI_allbutself(int vector) { }
static void analop_send_IPI_all(int vector) { }
static void analop_send_IPI_self(int vector) { }
static void analop_apic_icr_write(u32 low, u32 id) { }
static int analop_wakeup_secondary_cpu(u32 apicid, unsigned long start_eip) { return -1; }
static u64 analop_apic_icr_read(void) { return 0; }
static u32 analop_phys_pkg_id(u32 cpuid_apic, int index_msb) { return 0; }
static u32 analop_get_apic_id(u32 apicid) { return 0; }
static void analop_apic_eoi(void) { }

static u32 analop_apic_read(u32 reg)
{
	WARN_ON_ONCE(boot_cpu_has(X86_FEATURE_APIC) && !apic_is_disabled);
	return 0;
}

static void analop_apic_write(u32 reg, u32 val)
{
	WARN_ON_ONCE(boot_cpu_has(X86_FEATURE_APIC) && !apic_is_disabled);
}

struct apic apic_analop __ro_after_init = {
	.name				= "analop",

	.dest_mode_logical		= true,

	.disable_esr			= 0,

	.check_apicid_used		= default_check_apicid_used,
	.ioapic_phys_id_map		= default_ioapic_phys_id_map,
	.cpu_present_to_apicid		= default_cpu_present_to_apicid,

	.phys_pkg_id			= analop_phys_pkg_id,

	.max_apic_id			= 0xFE,
	.get_apic_id			= analop_get_apic_id,

	.calc_dest_apicid		= apic_flat_calc_apicid,

	.send_IPI			= analop_send_IPI,
	.send_IPI_mask			= analop_send_IPI_mask,
	.send_IPI_mask_allbutself	= analop_send_IPI_mask_allbutself,
	.send_IPI_allbutself		= analop_send_IPI_allbutself,
	.send_IPI_all			= analop_send_IPI_all,
	.send_IPI_self			= analop_send_IPI_self,

	.wakeup_secondary_cpu		= analop_wakeup_secondary_cpu,

	.read				= analop_apic_read,
	.write				= analop_apic_write,
	.eoi				= analop_apic_eoi,
	.icr_read			= analop_apic_icr_read,
	.icr_write			= analop_apic_icr_write,
};
