// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CPUID parser; for populating the system's CPUID tables.
 */

#include <linux/kernel.h>

#include <asm/cpuid/api.h>
#include <asm/processor.h>

#include "cpuid_parser.h"

/* Clear a single CPUID table entry */
static void cpuid_clear(const struct cpuid_parse_entry *e, const struct cpuid_read_output *output)
{
	struct cpuid_regs *regs = output->regs;

	for (int i = 0; i < e->maxcnt; i++, regs++)
		memset(regs, 0, sizeof(*regs));

	memset(output->info, 0, sizeof(*output->info));
}

/*
 * Leaf read functions:
 */

/*
 * Default CPUID read function
 * Satisfies the requirements stated at 'struct cpuid_parse_entry'->read().
 */
static void
cpuid_read_generic(const struct cpuid_parse_entry *e, const struct cpuid_read_output *output)
{
	struct cpuid_regs *regs = output->regs;

	for (int i = 0; i < e->maxcnt; i++, regs++, output->info->nr_entries++)
		cpuid_read_subleaf(e->leaf, e->subleaf + i, regs);
}

/*
 * CPUID parser table:
 */

static const struct cpuid_parse_entry cpuid_parse_entries[] = {
	CPUID_PARSE_ENTRIES
};

/*
 * Leaf-independent parser code:
 */

static unsigned int cpuid_range_max_leaf(const struct cpuid_table *t, unsigned int range)
{
	const struct leaf_0x0_0 *l0 = __cpuid_table_subleaf(t, 0x0, 0);

	switch (range) {
	case CPUID_BASE_START:	return l0  ?  l0->max_std_leaf : 0;
	default:		return 0;
	}
}

static void
__cpuid_reset_table(struct cpuid_table *t, const struct cpuid_parse_entry entries[],
		    unsigned int nr_entries, unsigned int start, unsigned int end, bool fill)
{
	const struct cpuid_parse_entry *entry = entries;
	unsigned int range = CPUID_RANGE(start);

	for (unsigned int i = 0; i < nr_entries; i++, entry++) {
		struct cpuid_read_output output = {
			.regs = cpuid_table_regs_p(t, entry->regs_offs),
			.info = cpuid_table_info_p(t, entry->info_offs),
		};

		if (entry->leaf < start || entry->leaf > end)
			continue;

		cpuid_clear(entry, &output);

		/*
		 * Read the range's anchor leaf unconditionally so that the cached
		 * maximum valid leaf value is available for the remaining entries.
		 */
		if (fill && (entry->leaf == range || entry->leaf <= cpuid_range_max_leaf(t, range)))
			entry->read(entry, &output);
	}
}

/*
 * Zero all cached CPUID entries within [@start-@end] range.  This is needed when
 * certain operations like MSR writes induce changes to the CPU's CPUID layout.
 */
static void
__cpuid_zero_table(struct cpuid_table *t, const struct cpuid_parse_entry entries[],
		   unsigned int nr_entries, unsigned int start, unsigned int end)
{
	__cpuid_reset_table(t, entries, nr_entries, start, end, false);
}

static void
__cpuid_fill_table(struct cpuid_table *t, const struct cpuid_parse_entry entries[],
		   unsigned int nr_entries, unsigned int start, unsigned int end)
{
	__cpuid_reset_table(t, entries, nr_entries, start, end, true);
}

static void
cpuid_fill_table(struct cpuid_table *t, const struct cpuid_parse_entry entries[], unsigned int nr_entries)
{
	static const struct {
		unsigned int start;
		unsigned int end;
	} ranges[] = {
		{ CPUID_BASE_START, CPUID_BASE_END },
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(ranges); i++)
		__cpuid_fill_table(t, entries, nr_entries, ranges[i].start, ranges[i].end);
}

static void __cpuid_scan_cpu_full(struct cpuinfo_x86 *c)
{
	unsigned int nr_entries = ARRAY_SIZE(cpuid_parse_entries);
	struct cpuid_table *table = &c->cpuid;

	cpuid_fill_table(table, cpuid_parse_entries, nr_entries);
}

static void
__cpuid_scan_cpu_partial(struct cpuinfo_x86 *c, unsigned int start_leaf, unsigned int end_leaf)
{
	unsigned int nr_entries = ARRAY_SIZE(cpuid_parse_entries);
	struct cpuid_table *table = &c->cpuid;

	__cpuid_zero_table(table, cpuid_parse_entries, nr_entries, start_leaf, end_leaf);
	__cpuid_fill_table(table, cpuid_parse_entries, nr_entries, start_leaf, end_leaf);
}

/*
 * Call-site APIs:
 */

/**
 * cpuid_scan_cpu() - Populate current CPU's CPUID table
 * @c:		CPU capability structure associated with the current CPU
 *
 * Populate the CPUID table embedded within @c with parsed CPUID data.  All CPUID
 * instructions are invoked locally, so this must be called on the CPU associated
 * with @c.
 */
void cpuid_scan_cpu(struct cpuinfo_x86 *c)
{
	__cpuid_scan_cpu_full(c);
}

/**
 * cpuid_refresh_range() - Rescan a CPUID table's leaf range
 * @c:		CPU capability structure associated with the current CPU
 * @start:	Start of leaf range to be re-scanned
 * @end:	End of leaf range
 */
void cpuid_refresh_range(struct cpuinfo_x86 *c, u32 start, u32 end)
{
	if (WARN_ON_ONCE(start > end))
		return;

	if (WARN_ON_ONCE(CPUID_RANGE(start) != CPUID_RANGE(end)))
		return;

	__cpuid_scan_cpu_partial(c, start, end);
}

/**
 * cpuid_refresh_leaf() - Rescan a CPUID table's leaf
 * @c:		CPU capability structure associated with the current CPU
 * @leaf:	Leaf to be re-scanned
 */
void cpuid_refresh_leaf(struct cpuinfo_x86 *c, u32 leaf)
{
	cpuid_refresh_range(c, leaf, leaf);
}
