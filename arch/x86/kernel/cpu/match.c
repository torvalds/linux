// SPDX-License-Identifier: GPL-2.0
#include <asm/cpu_device_id.h>
#include <asm/cpufeature.h>
#include <linux/cpu.h>
#include <linux/export.h>
#include <linux/slab.h>

/**
 * x86_match_cpu - match current CPU again an array of x86_cpu_ids
 * @match: Pointer to array of x86_cpu_ids. Last entry terminated with
 *         {}.
 *
 * Return the entry if the current CPU matches the entries in the
 * passed x86_cpu_id match table. Otherwise NULL.  The match table
 * contains vendor (X86_VENDOR_*), family, model and feature bits or
 * respective wildcard entries.
 *
 * A typical table entry would be to match a specific CPU
 *
 * X86_MATCH_VFM_FEATURE(INTEL_BROADWELL, X86_FEATURE_ANY, NULL);
 *
 * Fields can be wildcarded with %X86_VENDOR_ANY, %X86_FAMILY_ANY,
 * %X86_MODEL_ANY, %X86_FEATURE_ANY (except for vendor)
 *
 * asm/cpu_device_id.h contains a set of useful macros which are shortcuts
 * for various common selections. The above can be shortened to:
 *
 * X86_MATCH_VFM(INTEL_BROADWELL, NULL);
 *
 * Arrays used to match for this should also be declared using
 * MODULE_DEVICE_TABLE(x86cpu, ...)
 *
 * This always matches against the boot cpu, assuming models and features are
 * consistent over all CPUs.
 */
const struct x86_cpu_id *x86_match_cpu(const struct x86_cpu_id *match)
{
	const struct x86_cpu_id *m;
	struct cpuinfo_x86 *c = &boot_cpu_data;

	for (m = match;
	     m->vendor | m->family | m->model | m->steppings | m->feature;
	     m++) {
		if (m->vendor != X86_VENDOR_ANY && c->x86_vendor != m->vendor)
			continue;
		if (m->family != X86_FAMILY_ANY && c->x86 != m->family)
			continue;
		if (m->model != X86_MODEL_ANY && c->x86_model != m->model)
			continue;
		if (m->steppings != X86_STEPPING_ANY &&
		    !(BIT(c->x86_stepping) & m->steppings))
			continue;
		if (m->feature != X86_FEATURE_ANY && !cpu_has(c, m->feature))
			continue;
		return m;
	}
	return NULL;
}
EXPORT_SYMBOL(x86_match_cpu);

static const struct x86_cpu_desc *
x86_match_cpu_with_stepping(const struct x86_cpu_desc *match)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;
	const struct x86_cpu_desc *m;

	for (m = match; m->x86_family | m->x86_model; m++) {
		if (c->x86_vendor != m->x86_vendor)
			continue;
		if (c->x86 != m->x86_family)
			continue;
		if (c->x86_model != m->x86_model)
			continue;
		if (c->x86_stepping != m->x86_stepping)
			continue;
		return m;
	}
	return NULL;
}

bool x86_cpu_has_min_microcode_rev(const struct x86_cpu_desc *table)
{
	const struct x86_cpu_desc *res = x86_match_cpu_with_stepping(table);

	if (!res || res->x86_microcode_rev > boot_cpu_data.microcode)
		return false;

	return true;
}
EXPORT_SYMBOL_GPL(x86_cpu_has_min_microcode_rev);
