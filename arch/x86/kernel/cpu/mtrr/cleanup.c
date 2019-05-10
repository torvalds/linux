/*
 * MTRR (Memory Type Range Register) cleanup
 *
 *  Copyright (C) 2009 Yinghai Lu
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/kvm_para.h>
#include <linux/range.h>

#include <asm/processor.h>
#include <asm/e820/api.h>
#include <asm/mtrr.h>
#include <asm/msr.h>

#include "mtrr.h"

struct var_mtrr_range_state {
	unsigned long	base_pfn;
	unsigned long	size_pfn;
	mtrr_type	type;
};

struct var_mtrr_state {
	unsigned long	range_startk;
	unsigned long	range_sizek;
	unsigned long	chunk_sizek;
	unsigned long	gran_sizek;
	unsigned int	reg;
};

/* Should be related to MTRR_VAR_RANGES nums */
#define RANGE_NUM				256

static struct range __initdata		range[RANGE_NUM];
static int __initdata				nr_range;

static struct var_mtrr_range_state __initdata	range_state[RANGE_NUM];

static int __initdata debug_print;
#define Dprintk(x...) do { if (debug_print) pr_debug(x); } while (0)

#define BIOS_BUG_MSG \
	"WARNING: BIOS bug: VAR MTRR %d contains strange UC entry under 1M, check with your system vendor!\n"

static int __init
x86_get_mtrr_mem_range(struct range *range, int nr_range,
		       unsigned long extra_remove_base,
		       unsigned long extra_remove_size)
{
	unsigned long base, size;
	mtrr_type type;
	int i;

	for (i = 0; i < num_var_ranges; i++) {
		type = range_state[i].type;
		if (type != MTRR_TYPE_WRBACK)
			continue;
		base = range_state[i].base_pfn;
		size = range_state[i].size_pfn;
		nr_range = add_range_with_merge(range, RANGE_NUM, nr_range,
						base, base + size);
	}
	if (debug_print) {
		pr_debug("After WB checking\n");
		for (i = 0; i < nr_range; i++)
			pr_debug("MTRR MAP PFN: %016llx - %016llx\n",
				 range[i].start, range[i].end);
	}

	/* Take out UC ranges: */
	for (i = 0; i < num_var_ranges; i++) {
		type = range_state[i].type;
		if (type != MTRR_TYPE_UNCACHABLE &&
		    type != MTRR_TYPE_WRPROT)
			continue;
		size = range_state[i].size_pfn;
		if (!size)
			continue;
		base = range_state[i].base_pfn;
		if (base < (1<<(20-PAGE_SHIFT)) && mtrr_state.have_fixed &&
		    (mtrr_state.enabled & MTRR_STATE_MTRR_ENABLED) &&
		    (mtrr_state.enabled & MTRR_STATE_MTRR_FIXED_ENABLED)) {
			/* Var MTRR contains UC entry below 1M? Skip it: */
			pr_warn(BIOS_BUG_MSG, i);
			if (base + size <= (1<<(20-PAGE_SHIFT)))
				continue;
			size -= (1<<(20-PAGE_SHIFT)) - base;
			base = 1<<(20-PAGE_SHIFT);
		}
		subtract_range(range, RANGE_NUM, base, base + size);
	}
	if (extra_remove_size)
		subtract_range(range, RANGE_NUM, extra_remove_base,
				 extra_remove_base + extra_remove_size);

	if  (debug_print) {
		pr_debug("After UC checking\n");
		for (i = 0; i < RANGE_NUM; i++) {
			if (!range[i].end)
				continue;
			pr_debug("MTRR MAP PFN: %016llx - %016llx\n",
				 range[i].start, range[i].end);
		}
	}

	/* sort the ranges */
	nr_range = clean_sort_range(range, RANGE_NUM);
	if  (debug_print) {
		pr_debug("After sorting\n");
		for (i = 0; i < nr_range; i++)
			pr_debug("MTRR MAP PFN: %016llx - %016llx\n",
				 range[i].start, range[i].end);
	}

	return nr_range;
}

#ifdef CONFIG_MTRR_SANITIZER

static unsigned long __init sum_ranges(struct range *range, int nr_range)
{
	unsigned long sum = 0;
	int i;

	for (i = 0; i < nr_range; i++)
		sum += range[i].end - range[i].start;

	return sum;
}

static int enable_mtrr_cleanup __initdata =
	CONFIG_MTRR_SANITIZER_ENABLE_DEFAULT;

static int __init disable_mtrr_cleanup_setup(char *str)
{
	enable_mtrr_cleanup = 0;
	return 0;
}
early_param("disable_mtrr_cleanup", disable_mtrr_cleanup_setup);

static int __init enable_mtrr_cleanup_setup(char *str)
{
	enable_mtrr_cleanup = 1;
	return 0;
}
early_param("enable_mtrr_cleanup", enable_mtrr_cleanup_setup);

static int __init mtrr_cleanup_debug_setup(char *str)
{
	debug_print = 1;
	return 0;
}
early_param("mtrr_cleanup_debug", mtrr_cleanup_debug_setup);

static void __init
set_var_mtrr(unsigned int reg, unsigned long basek, unsigned long sizek,
	     unsigned char type, unsigned int address_bits)
{
	u32 base_lo, base_hi, mask_lo, mask_hi;
	u64 base, mask;

	if (!sizek) {
		fill_mtrr_var_range(reg, 0, 0, 0, 0);
		return;
	}

	mask = (1ULL << address_bits) - 1;
	mask &= ~((((u64)sizek) << 10) - 1);

	base = ((u64)basek) << 10;

	base |= type;
	mask |= 0x800;

	base_lo = base & ((1ULL<<32) - 1);
	base_hi = base >> 32;

	mask_lo = mask & ((1ULL<<32) - 1);
	mask_hi = mask >> 32;

	fill_mtrr_var_range(reg, base_lo, base_hi, mask_lo, mask_hi);
}

static void __init
save_var_mtrr(unsigned int reg, unsigned long basek, unsigned long sizek,
	      unsigned char type)
{
	range_state[reg].base_pfn = basek >> (PAGE_SHIFT - 10);
	range_state[reg].size_pfn = sizek >> (PAGE_SHIFT - 10);
	range_state[reg].type = type;
}

static void __init set_var_mtrr_all(unsigned int address_bits)
{
	unsigned long basek, sizek;
	unsigned char type;
	unsigned int reg;

	for (reg = 0; reg < num_var_ranges; reg++) {
		basek = range_state[reg].base_pfn << (PAGE_SHIFT - 10);
		sizek = range_state[reg].size_pfn << (PAGE_SHIFT - 10);
		type = range_state[reg].type;

		set_var_mtrr(reg, basek, sizek, type, address_bits);
	}
}

static unsigned long to_size_factor(unsigned long sizek, char *factorp)
{
	unsigned long base = sizek;
	char factor;

	if (base & ((1<<10) - 1)) {
		/* Not MB-aligned: */
		factor = 'K';
	} else if (base & ((1<<20) - 1)) {
		factor = 'M';
		base >>= 10;
	} else {
		factor = 'G';
		base >>= 20;
	}

	*factorp = factor;

	return base;
}

static unsigned int __init
range_to_mtrr(unsigned int reg, unsigned long range_startk,
	      unsigned long range_sizek, unsigned char type)
{
	if (!range_sizek || (reg >= num_var_ranges))
		return reg;

	while (range_sizek) {
		unsigned long max_align, align;
		unsigned long sizek;

		/* Compute the maximum size with which we can make a range: */
		if (range_startk)
			max_align = __ffs(range_startk);
		else
			max_align = BITS_PER_LONG - 1;

		align = __fls(range_sizek);
		if (align > max_align)
			align = max_align;

		sizek = 1UL << align;
		if (debug_print) {
			char start_factor = 'K', size_factor = 'K';
			unsigned long start_base, size_base;

			start_base = to_size_factor(range_startk, &start_factor);
			size_base = to_size_factor(sizek, &size_factor);

			Dprintk("Setting variable MTRR %d, "
				"base: %ld%cB, range: %ld%cB, type %s\n",
				reg, start_base, start_factor,
				size_base, size_factor,
				(type == MTRR_TYPE_UNCACHABLE) ? "UC" :
				   ((type == MTRR_TYPE_WRBACK) ? "WB" : "Other")
				);
		}
		save_var_mtrr(reg++, range_startk, sizek, type);
		range_startk += sizek;
		range_sizek -= sizek;
		if (reg >= num_var_ranges)
			break;
	}
	return reg;
}

static unsigned __init
range_to_mtrr_with_hole(struct var_mtrr_state *state, unsigned long basek,
			unsigned long sizek)
{
	unsigned long hole_basek, hole_sizek;
	unsigned long second_sizek;
	unsigned long range0_basek, range0_sizek;
	unsigned long range_basek, range_sizek;
	unsigned long chunk_sizek;
	unsigned long gran_sizek;

	hole_basek = 0;
	hole_sizek = 0;
	second_sizek = 0;
	chunk_sizek = state->chunk_sizek;
	gran_sizek = state->gran_sizek;

	/* Align with gran size, prevent small block used up MTRRs: */
	range_basek = ALIGN(state->range_startk, gran_sizek);
	if ((range_basek > basek) && basek)
		return second_sizek;

	state->range_sizek -= (range_basek - state->range_startk);
	range_sizek = ALIGN(state->range_sizek, gran_sizek);

	while (range_sizek > state->range_sizek) {
		range_sizek -= gran_sizek;
		if (!range_sizek)
			return 0;
	}
	state->range_sizek = range_sizek;

	/* Try to append some small hole: */
	range0_basek = state->range_startk;
	range0_sizek = ALIGN(state->range_sizek, chunk_sizek);

	/* No increase: */
	if (range0_sizek == state->range_sizek) {
		Dprintk("rangeX: %016lx - %016lx\n",
			range0_basek<<10,
			(range0_basek + state->range_sizek)<<10);
		state->reg = range_to_mtrr(state->reg, range0_basek,
				state->range_sizek, MTRR_TYPE_WRBACK);
		return 0;
	}

	/* Only cut back when it is not the last: */
	if (sizek) {
		while (range0_basek + range0_sizek > (basek + sizek)) {
			if (range0_sizek >= chunk_sizek)
				range0_sizek -= chunk_sizek;
			else
				range0_sizek = 0;

			if (!range0_sizek)
				break;
		}
	}

second_try:
	range_basek = range0_basek + range0_sizek;

	/* One hole in the middle: */
	if (range_basek > basek && range_basek <= (basek + sizek))
		second_sizek = range_basek - basek;

	if (range0_sizek > state->range_sizek) {

		/* One hole in middle or at the end: */
		hole_sizek = range0_sizek - state->range_sizek - second_sizek;

		/* Hole size should be less than half of range0 size: */
		if (hole_sizek >= (range0_sizek >> 1) &&
		    range0_sizek >= chunk_sizek) {
			range0_sizek -= chunk_sizek;
			second_sizek = 0;
			hole_sizek = 0;

			goto second_try;
		}
	}

	if (range0_sizek) {
		Dprintk("range0: %016lx - %016lx\n",
			range0_basek<<10,
			(range0_basek + range0_sizek)<<10);
		state->reg = range_to_mtrr(state->reg, range0_basek,
				range0_sizek, MTRR_TYPE_WRBACK);
	}

	if (range0_sizek < state->range_sizek) {
		/* Need to handle left over range: */
		range_sizek = state->range_sizek - range0_sizek;

		Dprintk("range: %016lx - %016lx\n",
			 range_basek<<10,
			 (range_basek + range_sizek)<<10);

		state->reg = range_to_mtrr(state->reg, range_basek,
				 range_sizek, MTRR_TYPE_WRBACK);
	}

	if (hole_sizek) {
		hole_basek = range_basek - hole_sizek - second_sizek;
		Dprintk("hole: %016lx - %016lx\n",
			 hole_basek<<10,
			 (hole_basek + hole_sizek)<<10);
		state->reg = range_to_mtrr(state->reg, hole_basek,
				 hole_sizek, MTRR_TYPE_UNCACHABLE);
	}

	return second_sizek;
}

static void __init
set_var_mtrr_range(struct var_mtrr_state *state, unsigned long base_pfn,
		   unsigned long size_pfn)
{
	unsigned long basek, sizek;
	unsigned long second_sizek = 0;

	if (state->reg >= num_var_ranges)
		return;

	basek = base_pfn << (PAGE_SHIFT - 10);
	sizek = size_pfn << (PAGE_SHIFT - 10);

	/* See if I can merge with the last range: */
	if ((basek <= 1024) ||
	    (state->range_startk + state->range_sizek == basek)) {
		unsigned long endk = basek + sizek;
		state->range_sizek = endk - state->range_startk;
		return;
	}
	/* Write the range mtrrs: */
	if (state->range_sizek != 0)
		second_sizek = range_to_mtrr_with_hole(state, basek, sizek);

	/* Allocate an msr: */
	state->range_startk = basek + second_sizek;
	state->range_sizek  = sizek - second_sizek;
}

/* Mininum size of mtrr block that can take hole: */
static u64 mtrr_chunk_size __initdata = (256ULL<<20);

static int __init parse_mtrr_chunk_size_opt(char *p)
{
	if (!p)
		return -EINVAL;
	mtrr_chunk_size = memparse(p, &p);
	return 0;
}
early_param("mtrr_chunk_size", parse_mtrr_chunk_size_opt);

/* Granularity of mtrr of block: */
static u64 mtrr_gran_size __initdata;

static int __init parse_mtrr_gran_size_opt(char *p)
{
	if (!p)
		return -EINVAL;
	mtrr_gran_size = memparse(p, &p);
	return 0;
}
early_param("mtrr_gran_size", parse_mtrr_gran_size_opt);

static unsigned long nr_mtrr_spare_reg __initdata =
				 CONFIG_MTRR_SANITIZER_SPARE_REG_NR_DEFAULT;

static int __init parse_mtrr_spare_reg(char *arg)
{
	if (arg)
		nr_mtrr_spare_reg = simple_strtoul(arg, NULL, 0);
	return 0;
}
early_param("mtrr_spare_reg_nr", parse_mtrr_spare_reg);

static int __init
x86_setup_var_mtrrs(struct range *range, int nr_range,
		    u64 chunk_size, u64 gran_size)
{
	struct var_mtrr_state var_state;
	int num_reg;
	int i;

	var_state.range_startk	= 0;
	var_state.range_sizek	= 0;
	var_state.reg		= 0;
	var_state.chunk_sizek	= chunk_size >> 10;
	var_state.gran_sizek	= gran_size >> 10;

	memset(range_state, 0, sizeof(range_state));

	/* Write the range: */
	for (i = 0; i < nr_range; i++) {
		set_var_mtrr_range(&var_state, range[i].start,
				   range[i].end - range[i].start);
	}

	/* Write the last range: */
	if (var_state.range_sizek != 0)
		range_to_mtrr_with_hole(&var_state, 0, 0);

	num_reg = var_state.reg;
	/* Clear out the extra MTRR's: */
	while (var_state.reg < num_var_ranges) {
		save_var_mtrr(var_state.reg, 0, 0, 0);
		var_state.reg++;
	}

	return num_reg;
}

struct mtrr_cleanup_result {
	unsigned long	gran_sizek;
	unsigned long	chunk_sizek;
	unsigned long	lose_cover_sizek;
	unsigned int	num_reg;
	int		bad;
};

/*
 * gran_size: 64K, 128K, 256K, 512K, 1M, 2M, ..., 2G
 * chunk size: gran_size, ..., 2G
 * so we need (1+16)*8
 */
#define NUM_RESULT	136
#define PSHIFT		(PAGE_SHIFT - 10)

static struct mtrr_cleanup_result __initdata result[NUM_RESULT];
static unsigned long __initdata min_loss_pfn[RANGE_NUM];

static void __init print_out_mtrr_range_state(void)
{
	char start_factor = 'K', size_factor = 'K';
	unsigned long start_base, size_base;
	mtrr_type type;
	int i;

	for (i = 0; i < num_var_ranges; i++) {

		size_base = range_state[i].size_pfn << (PAGE_SHIFT - 10);
		if (!size_base)
			continue;

		size_base = to_size_factor(size_base, &size_factor),
		start_base = range_state[i].base_pfn << (PAGE_SHIFT - 10);
		start_base = to_size_factor(start_base, &start_factor),
		type = range_state[i].type;

		pr_debug("reg %d, base: %ld%cB, range: %ld%cB, type %s\n",
			i, start_base, start_factor,
			size_base, size_factor,
			(type == MTRR_TYPE_UNCACHABLE) ? "UC" :
			    ((type == MTRR_TYPE_WRPROT) ? "WP" :
			     ((type == MTRR_TYPE_WRBACK) ? "WB" : "Other"))
			);
	}
}

static int __init mtrr_need_cleanup(void)
{
	int i;
	mtrr_type type;
	unsigned long size;
	/* Extra one for all 0: */
	int num[MTRR_NUM_TYPES + 1];

	/* Check entries number: */
	memset(num, 0, sizeof(num));
	for (i = 0; i < num_var_ranges; i++) {
		type = range_state[i].type;
		size = range_state[i].size_pfn;
		if (type >= MTRR_NUM_TYPES)
			continue;
		if (!size)
			type = MTRR_NUM_TYPES;
		num[type]++;
	}

	/* Check if we got UC entries: */
	if (!num[MTRR_TYPE_UNCACHABLE])
		return 0;

	/* Check if we only had WB and UC */
	if (num[MTRR_TYPE_WRBACK] + num[MTRR_TYPE_UNCACHABLE] !=
	    num_var_ranges - num[MTRR_NUM_TYPES])
		return 0;

	return 1;
}

static unsigned long __initdata range_sums;

static void __init
mtrr_calc_range_state(u64 chunk_size, u64 gran_size,
		      unsigned long x_remove_base,
		      unsigned long x_remove_size, int i)
{
	/*
	 * range_new should really be an automatic variable, but
	 * putting 4096 bytes on the stack is frowned upon, to put it
	 * mildly. It is safe to make it a static __initdata variable,
	 * since mtrr_calc_range_state is only called during init and
	 * there's no way it will call itself recursively.
	 */
	static struct range range_new[RANGE_NUM] __initdata;
	unsigned long range_sums_new;
	int nr_range_new;
	int num_reg;

	/* Convert ranges to var ranges state: */
	num_reg = x86_setup_var_mtrrs(range, nr_range, chunk_size, gran_size);

	/* We got new setting in range_state, check it: */
	memset(range_new, 0, sizeof(range_new));
	nr_range_new = x86_get_mtrr_mem_range(range_new, 0,
				x_remove_base, x_remove_size);
	range_sums_new = sum_ranges(range_new, nr_range_new);

	result[i].chunk_sizek = chunk_size >> 10;
	result[i].gran_sizek = gran_size >> 10;
	result[i].num_reg = num_reg;

	if (range_sums < range_sums_new) {
		result[i].lose_cover_sizek = (range_sums_new - range_sums) << PSHIFT;
		result[i].bad = 1;
	} else {
		result[i].lose_cover_sizek = (range_sums - range_sums_new) << PSHIFT;
	}

	/* Double check it: */
	if (!result[i].bad && !result[i].lose_cover_sizek) {
		if (nr_range_new != nr_range || memcmp(range, range_new, sizeof(range)))
			result[i].bad = 1;
	}

	if (!result[i].bad && (range_sums - range_sums_new < min_loss_pfn[num_reg]))
		min_loss_pfn[num_reg] = range_sums - range_sums_new;
}

static void __init mtrr_print_out_one_result(int i)
{
	unsigned long gran_base, chunk_base, lose_base;
	char gran_factor, chunk_factor, lose_factor;

	gran_base = to_size_factor(result[i].gran_sizek, &gran_factor);
	chunk_base = to_size_factor(result[i].chunk_sizek, &chunk_factor);
	lose_base = to_size_factor(result[i].lose_cover_sizek, &lose_factor);

	pr_info("%sgran_size: %ld%c \tchunk_size: %ld%c \t",
		result[i].bad ? "*BAD*" : " ",
		gran_base, gran_factor, chunk_base, chunk_factor);
	pr_cont("num_reg: %d  \tlose cover RAM: %s%ld%c\n",
		result[i].num_reg, result[i].bad ? "-" : "",
		lose_base, lose_factor);
}

static int __init mtrr_search_optimal_index(void)
{
	int num_reg_good;
	int index_good;
	int i;

	if (nr_mtrr_spare_reg >= num_var_ranges)
		nr_mtrr_spare_reg = num_var_ranges - 1;

	num_reg_good = -1;
	for (i = num_var_ranges - nr_mtrr_spare_reg; i > 0; i--) {
		if (!min_loss_pfn[i])
			num_reg_good = i;
	}

	index_good = -1;
	if (num_reg_good != -1) {
		for (i = 0; i < NUM_RESULT; i++) {
			if (!result[i].bad &&
			    result[i].num_reg == num_reg_good &&
			    !result[i].lose_cover_sizek) {
				index_good = i;
				break;
			}
		}
	}

	return index_good;
}

int __init mtrr_cleanup(unsigned address_bits)
{
	unsigned long x_remove_base, x_remove_size;
	unsigned long base, size, def, dummy;
	u64 chunk_size, gran_size;
	mtrr_type type;
	int index_good;
	int i;

	if (!is_cpu(INTEL) || enable_mtrr_cleanup < 1)
		return 0;

	rdmsr(MSR_MTRRdefType, def, dummy);
	def &= 0xff;
	if (def != MTRR_TYPE_UNCACHABLE)
		return 0;

	/* Get it and store it aside: */
	memset(range_state, 0, sizeof(range_state));
	for (i = 0; i < num_var_ranges; i++) {
		mtrr_if->get(i, &base, &size, &type);
		range_state[i].base_pfn = base;
		range_state[i].size_pfn = size;
		range_state[i].type = type;
	}

	/* Check if we need handle it and can handle it: */
	if (!mtrr_need_cleanup())
		return 0;

	/* Print original var MTRRs at first, for debugging: */
	pr_debug("original variable MTRRs\n");
	print_out_mtrr_range_state();

	memset(range, 0, sizeof(range));
	x_remove_size = 0;
	x_remove_base = 1 << (32 - PAGE_SHIFT);
	if (mtrr_tom2)
		x_remove_size = (mtrr_tom2 >> PAGE_SHIFT) - x_remove_base;

	/*
	 * [0, 1M) should always be covered by var mtrr with WB
	 * and fixed mtrrs should take effect before var mtrr for it:
	 */
	nr_range = add_range_with_merge(range, RANGE_NUM, 0, 0,
					1ULL<<(20 - PAGE_SHIFT));
	/* add from var mtrr at last */
	nr_range = x86_get_mtrr_mem_range(range, nr_range,
					  x_remove_base, x_remove_size);

	range_sums = sum_ranges(range, nr_range);
	pr_info("total RAM covered: %ldM\n",
	       range_sums >> (20 - PAGE_SHIFT));

	if (mtrr_chunk_size && mtrr_gran_size) {
		i = 0;
		mtrr_calc_range_state(mtrr_chunk_size, mtrr_gran_size,
				      x_remove_base, x_remove_size, i);

		mtrr_print_out_one_result(i);

		if (!result[i].bad) {
			set_var_mtrr_all(address_bits);
			pr_debug("New variable MTRRs\n");
			print_out_mtrr_range_state();
			return 1;
		}
		pr_info("invalid mtrr_gran_size or mtrr_chunk_size, will find optimal one\n");
	}

	i = 0;
	memset(min_loss_pfn, 0xff, sizeof(min_loss_pfn));
	memset(result, 0, sizeof(result));
	for (gran_size = (1ULL<<16); gran_size < (1ULL<<32); gran_size <<= 1) {

		for (chunk_size = gran_size; chunk_size < (1ULL<<32);
		     chunk_size <<= 1) {

			if (i >= NUM_RESULT)
				continue;

			mtrr_calc_range_state(chunk_size, gran_size,
				      x_remove_base, x_remove_size, i);
			if (debug_print) {
				mtrr_print_out_one_result(i);
				pr_info("\n");
			}

			i++;
		}
	}

	/* Try to find the optimal index: */
	index_good = mtrr_search_optimal_index();

	if (index_good != -1) {
		pr_info("Found optimal setting for mtrr clean up\n");
		i = index_good;
		mtrr_print_out_one_result(i);

		/* Convert ranges to var ranges state: */
		chunk_size = result[i].chunk_sizek;
		chunk_size <<= 10;
		gran_size = result[i].gran_sizek;
		gran_size <<= 10;
		x86_setup_var_mtrrs(range, nr_range, chunk_size, gran_size);
		set_var_mtrr_all(address_bits);
		pr_debug("New variable MTRRs\n");
		print_out_mtrr_range_state();
		return 1;
	} else {
		/* print out all */
		for (i = 0; i < NUM_RESULT; i++)
			mtrr_print_out_one_result(i);
	}

	pr_info("mtrr_cleanup: can not find optimal value\n");
	pr_info("please specify mtrr_gran_size/mtrr_chunk_size\n");

	return 0;
}
#else
int __init mtrr_cleanup(unsigned address_bits)
{
	return 0;
}
#endif

static int disable_mtrr_trim;

static int __init disable_mtrr_trim_setup(char *str)
{
	disable_mtrr_trim = 1;
	return 0;
}
early_param("disable_mtrr_trim", disable_mtrr_trim_setup);

/*
 * Newer AMD K8s and later CPUs have a special magic MSR way to force WB
 * for memory >4GB. Check for that here.
 * Note this won't check if the MTRRs < 4GB where the magic bit doesn't
 * apply to are wrong, but so far we don't know of any such case in the wild.
 */
#define Tom2Enabled		(1U << 21)
#define Tom2ForceMemTypeWB	(1U << 22)

int __init amd_special_default_mtrr(void)
{
	u32 l, h;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD &&
	    boot_cpu_data.x86_vendor != X86_VENDOR_HYGON)
		return 0;
	if (boot_cpu_data.x86 < 0xf)
		return 0;
	/* In case some hypervisor doesn't pass SYSCFG through: */
	if (rdmsr_safe(MSR_K8_SYSCFG, &l, &h) < 0)
		return 0;
	/*
	 * Memory between 4GB and top of mem is forced WB by this magic bit.
	 * Reserved before K8RevF, but should be zero there.
	 */
	if ((l & (Tom2Enabled | Tom2ForceMemTypeWB)) ==
		 (Tom2Enabled | Tom2ForceMemTypeWB))
		return 1;
	return 0;
}

static u64 __init
real_trim_memory(unsigned long start_pfn, unsigned long limit_pfn)
{
	u64 trim_start, trim_size;

	trim_start = start_pfn;
	trim_start <<= PAGE_SHIFT;

	trim_size = limit_pfn;
	trim_size <<= PAGE_SHIFT;
	trim_size -= trim_start;

	return e820__range_update(trim_start, trim_size, E820_TYPE_RAM, E820_TYPE_RESERVED);
}

/**
 * mtrr_trim_uncached_memory - trim RAM not covered by MTRRs
 * @end_pfn: ending page frame number
 *
 * Some buggy BIOSes don't setup the MTRRs properly for systems with certain
 * memory configurations.  This routine checks that the highest MTRR matches
 * the end of memory, to make sure the MTRRs having a write back type cover
 * all of the memory the kernel is intending to use.  If not, it'll trim any
 * memory off the end by adjusting end_pfn, removing it from the kernel's
 * allocation pools, warning the user with an obnoxious message.
 */
int __init mtrr_trim_uncached_memory(unsigned long end_pfn)
{
	unsigned long i, base, size, highest_pfn = 0, def, dummy;
	mtrr_type type;
	u64 total_trim_size;
	/* extra one for all 0 */
	int num[MTRR_NUM_TYPES + 1];

	/*
	 * Make sure we only trim uncachable memory on machines that
	 * support the Intel MTRR architecture:
	 */
	if (!is_cpu(INTEL) || disable_mtrr_trim)
		return 0;

	rdmsr(MSR_MTRRdefType, def, dummy);
	def &= 0xff;
	if (def != MTRR_TYPE_UNCACHABLE)
		return 0;

	/* Get it and store it aside: */
	memset(range_state, 0, sizeof(range_state));
	for (i = 0; i < num_var_ranges; i++) {
		mtrr_if->get(i, &base, &size, &type);
		range_state[i].base_pfn = base;
		range_state[i].size_pfn = size;
		range_state[i].type = type;
	}

	/* Find highest cached pfn: */
	for (i = 0; i < num_var_ranges; i++) {
		type = range_state[i].type;
		if (type != MTRR_TYPE_WRBACK)
			continue;
		base = range_state[i].base_pfn;
		size = range_state[i].size_pfn;
		if (highest_pfn < base + size)
			highest_pfn = base + size;
	}

	/* kvm/qemu doesn't have mtrr set right, don't trim them all: */
	if (!highest_pfn) {
		pr_info("CPU MTRRs all blank - virtualized system.\n");
		return 0;
	}

	/* Check entries number: */
	memset(num, 0, sizeof(num));
	for (i = 0; i < num_var_ranges; i++) {
		type = range_state[i].type;
		if (type >= MTRR_NUM_TYPES)
			continue;
		size = range_state[i].size_pfn;
		if (!size)
			type = MTRR_NUM_TYPES;
		num[type]++;
	}

	/* No entry for WB? */
	if (!num[MTRR_TYPE_WRBACK])
		return 0;

	/* Check if we only had WB and UC: */
	if (num[MTRR_TYPE_WRBACK] + num[MTRR_TYPE_UNCACHABLE] !=
		num_var_ranges - num[MTRR_NUM_TYPES])
		return 0;

	memset(range, 0, sizeof(range));
	nr_range = 0;
	if (mtrr_tom2) {
		range[nr_range].start = (1ULL<<(32 - PAGE_SHIFT));
		range[nr_range].end = mtrr_tom2 >> PAGE_SHIFT;
		if (highest_pfn < range[nr_range].end)
			highest_pfn = range[nr_range].end;
		nr_range++;
	}
	nr_range = x86_get_mtrr_mem_range(range, nr_range, 0, 0);

	/* Check the head: */
	total_trim_size = 0;
	if (range[0].start)
		total_trim_size += real_trim_memory(0, range[0].start);

	/* Check the holes: */
	for (i = 0; i < nr_range - 1; i++) {
		if (range[i].end < range[i+1].start)
			total_trim_size += real_trim_memory(range[i].end,
							    range[i+1].start);
	}

	/* Check the top: */
	i = nr_range - 1;
	if (range[i].end < end_pfn)
		total_trim_size += real_trim_memory(range[i].end,
							 end_pfn);

	if (total_trim_size) {
		pr_warn("WARNING: BIOS bug: CPU MTRRs don't cover all of memory, losing %lluMB of RAM.\n",
			total_trim_size >> 20);

		if (!changed_by_mtrr_cleanup)
			WARN_ON(1);

		pr_info("update e820 for mtrr\n");
		e820__update_table_print();

		return 1;
	}

	return 0;
}
