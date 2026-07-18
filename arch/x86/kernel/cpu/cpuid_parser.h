/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_X86_CPUID_PARSER_H
#define _ARCH_X86_CPUID_PARSER_H

#include <asm/cpuid/types.h>

/*
 * Since accessing the CPUID leaves at 'struct cpuid_leaves' require compile time
 * tokenization, split the CPUID parser into two stages: compile time macros for
 * tokenizing the leaf/subleaf output offsets within the table, and generic runtime
 * code to write to the relevant CPUID leaves using such offsets.
 *
 * The output of the compile time macros is cached by a compile time "parse entry"
 * table (see 'struct cpuid_parse_entry').  The runtime parser code will utilize
 * such offsets by passing them to the cpuid_table_*_p() functions.
 */

/*
 * Compile time CPUID table offset calculations:
 *
 * @_leaf:	CPUID leaf, in 0xN format
 * @_subleaf:	CPUID subleaf, in decimal format
 */

#define __cpuid_leaves_regs_offset(_leaf, _subleaf)			\
	offsetof(struct cpuid_leaves, leaf_ ## _leaf ## _ ## _subleaf)

#define __cpuid_leaves_info_offset(_leaf, _subleaf)			\
	offsetof(struct cpuid_leaves, leaf_ ## _leaf ## _ ## _subleaf ## _ ## info)

#define __cpuid_leaves_regs_maxcnt(_leaf, _subleaf)			\
	ARRAY_SIZE(((struct cpuid_leaves *)NULL)->leaf_ ## _leaf ## _ ## _subleaf)

/*
 * Translation of compile time offsets to generic runtime pointers:
 */

static inline struct cpuid_regs *
cpuid_table_regs_p(const struct cpuid_table *t, unsigned long regs_offset)
{
	return (struct cpuid_regs *)((unsigned long)(&t->leaves) + regs_offset);
}

static inline struct leaf_parse_info *
cpuid_table_info_p(const struct cpuid_table *t, unsigned long info_offset)
{
	return (struct leaf_parse_info *)((unsigned long)(&t->leaves) + info_offset);
}

/**
 * struct cpuid_read_output - Output of a CPUID read operation
 * @regs:	Pointer to an array of CPUID outputs, where each array element covers the
 *		full EAX->EDX output range.
 * @info:	Pointer to query info; for saving the number of filled elements at @regs.
 *
 * A CPUID parser read function like cpuid_read_generic() or cpuid_read_0xN() uses this
 * structure to save the CPUID query outputs.  Actual storage for @regs and @info is
 * provided by the read function caller, and is typically within the CPU's CPUID table.
 *
 * See struct cpuid_parse_entry.read().
 */
struct cpuid_read_output {
	struct cpuid_regs	*regs;
	struct leaf_parse_info	*info;
};

/**
 * struct cpuid_parse_entry - CPUID parse table entry
 * @leaf:	Leaf number to be parsed
 * @subleaf:	Subleaf number to be parsed
 * @regs_offs:	Offset within 'struct cpuid_leaves' for saving the CPUID query output; to be
 *		passed to cpuid_table_regs_p().
 * @info_offs:	Offset within 'struct cpuid_leaves' for saving the CPUID query parse info; to be
 *		passed to cpuid_table_info_p().
 * @maxcnt:	Maximum number of output storage entries available for the CPUID query.
 * @read:	Read function for this entry.  It must save the parsed CPUID output to the passed
 *		'struct cpuid_read_output'->regs array of size >= @maxcnt.  It must set
 *		'struct cpuid_read_output'->info.nr_entries to the number of CPUID output entries
 *		parsed and filled.  A generic implementation is provided at cpuid_read_generic().
 */
struct cpuid_parse_entry {
	unsigned int	leaf;
	unsigned int	subleaf;
	unsigned int	regs_offs;
	unsigned int	info_offs;
	unsigned int	maxcnt;
	void		(*read)(const struct cpuid_parse_entry *e, const struct cpuid_read_output *o);
};

#define __CPUID_PARSE_ENTRY(_leaf, _subleaf, _suffix, _reader_fn)		\
	{									\
		.leaf		= _leaf,					\
		.subleaf	= _subleaf,					\
		.regs_offs	= __cpuid_leaves_regs_offset(_leaf, _suffix),	\
		.info_offs	= __cpuid_leaves_info_offset(_leaf, _suffix),	\
		.maxcnt		= __cpuid_leaves_regs_maxcnt(_leaf, _suffix),	\
		.read		= cpuid_read_ ## _reader_fn,			\
	}

/*
 * CPUID_PARSE_ENTRY_N() is for parsing CPUID leaves with a subleaf range.
 * Check <asm/cpuid/types.h> __CPUID_LEAF() vs. CPUID_LEAF_N().
 */

#define CPUID_PARSE_ENTRY(_leaf, _subleaf, _reader_fn)				\
	__CPUID_PARSE_ENTRY(_leaf, _subleaf, _subleaf, _reader_fn)

#define CPUID_PARSE_ENTRY_N(_leaf, _reader_fn)					\
	__CPUID_PARSE_ENTRY(_leaf, __cpuid_leaf_first_subleaf(_leaf), n, _reader_fn)

/*
 * CPUID parser table:
 */

#define CPUID_PARSE_ENTRIES									\
	/*			Leaf		Subleaf		Reader function */		\
	CPUID_PARSE_ENTRY   (	0x0,		0,		generic			),	\
	CPUID_PARSE_ENTRY   (	0x1,		0,		generic			),	\

#endif /* _ARCH_X86_CPUID_PARSER_H */
