/*
 * This only handles 32bit MTRR on 32bit hosts. This is strictly wrong
 * because MTRRs can span upto 40 bits (36bits on most modern x86)
 */
#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/mm.h>

#include <asm/processor-flags.h>
#include <asm/cpufeature.h>
#include <asm/tlbflush.h>
#include <asm/system.h>
#include <asm/mtrr.h>
#include <asm/msr.h>
#include <asm/pat.h>

#include "mtrr.h"

struct fixed_range_block {
	int base_msr;		/* start address of an MTRR block */
	int ranges;		/* number of MTRRs in this block  */
};

static struct fixed_range_block fixed_range_blocks[] = {
	{ MSR_MTRRfix64K_00000, 1 }, /* one   64k MTRR  */
	{ MSR_MTRRfix16K_80000, 2 }, /* two   16k MTRRs */
	{ MSR_MTRRfix4K_C0000,  8 }, /* eight  4k MTRRs */
	{}
};

static unsigned long smp_changes_mask;
static int mtrr_state_set;
u64 mtrr_tom2;

struct mtrr_state_type mtrr_state;
EXPORT_SYMBOL_GPL(mtrr_state);

/*
 * BIOS is expected to clear MtrrFixDramModEn bit, see for example
 * "BIOS and Kernel Developer's Guide for the AMD Athlon 64 and AMD
 * Opteron Processors" (26094 Rev. 3.30 February 2006), section
 * "13.2.1.2 SYSCFG Register": "The MtrrFixDramModEn bit should be set
 * to 1 during BIOS initalization of the fixed MTRRs, then cleared to
 * 0 for operation."
 */
static inline void k8_check_syscfg_dram_mod_en(void)
{
	u32 lo, hi;

	if (!((boot_cpu_data.x86_vendor == X86_VENDOR_AMD) &&
	      (boot_cpu_data.x86 >= 0x0f)))
		return;

	rdmsr(MSR_K8_SYSCFG, lo, hi);
	if (lo & K8_MTRRFIXRANGE_DRAM_MODIFY) {
		printk(KERN_ERR FW_WARN "MTRR: CPU %u: SYSCFG[MtrrFixDramModEn]"
		       " not cleared by BIOS, clearing this bit\n",
		       smp_processor_id());
		lo &= ~K8_MTRRFIXRANGE_DRAM_MODIFY;
		mtrr_wrmsr(MSR_K8_SYSCFG, lo, hi);
	}
}

/*
 * Returns the effective MTRR type for the region
 * Error returns:
 * - 0xFE - when the range is "not entirely covered" by _any_ var range MTRR
 * - 0xFF - when MTRR is not enabled
 */
u8 mtrr_type_lookup(u64 start, u64 end)
{
	int i;
	u64 base, mask;
	u8 prev_match, curr_match;

	if (!mtrr_state_set)
		return 0xFF;

	if (!mtrr_state.enabled)
		return 0xFF;

	/* Make end inclusive end, instead of exclusive */
	end--;

	/* Look in fixed ranges. Just return the type as per start */
	if (mtrr_state.have_fixed && (start < 0x100000)) {
		int idx;

		if (start < 0x80000) {
			idx = 0;
			idx += (start >> 16);
			return mtrr_state.fixed_ranges[idx];
		} else if (start < 0xC0000) {
			idx = 1 * 8;
			idx += ((start - 0x80000) >> 14);
			return mtrr_state.fixed_ranges[idx];
		} else if (start < 0x1000000) {
			idx = 3 * 8;
			idx += ((start - 0xC0000) >> 12);
			return mtrr_state.fixed_ranges[idx];
		}
	}

	/*
	 * Look in variable ranges
	 * Look of multiple ranges matching this address and pick type
	 * as per MTRR precedence
	 */
	if (!(mtrr_state.enabled & 2))
		return mtrr_state.def_type;

	prev_match = 0xFF;
	for (i = 0; i < num_var_ranges; ++i) {
		unsigned short start_state, end_state;

		if (!(mtrr_state.var_ranges[i].mask_lo & (1 << 11)))
			continue;

		base = (((u64)mtrr_state.var_ranges[i].base_hi) << 32) +
		       (mtrr_state.var_ranges[i].base_lo & PAGE_MASK);
		mask = (((u64)mtrr_state.var_ranges[i].mask_hi) << 32) +
		       (mtrr_state.var_ranges[i].mask_lo & PAGE_MASK);

		start_state = ((start & mask) == (base & mask));
		end_state = ((end & mask) == (base & mask));
		if (start_state != end_state)
			return 0xFE;

		if ((start & mask) != (base & mask))
			continue;

		curr_match = mtrr_state.var_ranges[i].base_lo & 0xff;
		if (prev_match == 0xFF) {
			prev_match = curr_match;
			continue;
		}

		if (prev_match == MTRR_TYPE_UNCACHABLE ||
		    curr_match == MTRR_TYPE_UNCACHABLE) {
			return MTRR_TYPE_UNCACHABLE;
		}

		if ((prev_match == MTRR_TYPE_WRBACK &&
		     curr_match == MTRR_TYPE_WRTHROUGH) ||
		    (prev_match == MTRR_TYPE_WRTHROUGH &&
		     curr_match == MTRR_TYPE_WRBACK)) {
			prev_match = MTRR_TYPE_WRTHROUGH;
			curr_match = MTRR_TYPE_WRTHROUGH;
		}

		if (prev_match != curr_match)
			return MTRR_TYPE_UNCACHABLE;
	}

	if (mtrr_tom2) {
		if (start >= (1ULL<<32) && (end < mtrr_tom2))
			return MTRR_TYPE_WRBACK;
	}

	if (prev_match != 0xFF)
		return prev_match;

	return mtrr_state.def_type;
}

/* Get the MSR pair relating to a var range */
static void
get_mtrr_var_range(unsigned int index, struct mtrr_var_range *vr)
{
	rdmsr(MTRRphysBase_MSR(index), vr->base_lo, vr->base_hi);
	rdmsr(MTRRphysMask_MSR(index), vr->mask_lo, vr->mask_hi);
}

/* Fill the MSR pair relating to a var range */
void fill_mtrr_var_range(unsigned int index,
		u32 base_lo, u32 base_hi, u32 mask_lo, u32 mask_hi)
{
	struct mtrr_var_range *vr;

	vr = mtrr_state.var_ranges;

	vr[index].base_lo = base_lo;
	vr[index].base_hi = base_hi;
	vr[index].mask_lo = mask_lo;
	vr[index].mask_hi = mask_hi;
}

static void get_fixed_ranges(mtrr_type *frs)
{
	unsigned int *p = (unsigned int *)frs;
	int i;

	k8_check_syscfg_dram_mod_en();

	rdmsr(MSR_MTRRfix64K_00000, p[0], p[1]);

	for (i = 0; i < 2; i++)
		rdmsr(MSR_MTRRfix16K_80000 + i, p[2 + i * 2], p[3 + i * 2]);
	for (i = 0; i < 8; i++)
		rdmsr(MSR_MTRRfix4K_C0000 + i, p[6 + i * 2], p[7 + i * 2]);
}

void mtrr_save_fixed_ranges(void *info)
{
	if (cpu_has_mtrr)
		get_fixed_ranges(mtrr_state.fixed_ranges);
}

static unsigned __initdata last_fixed_start;
static unsigned __initdata last_fixed_end;
static mtrr_type __initdata last_fixed_type;

static void __init print_fixed_last(void)
{
	if (!last_fixed_end)
		return;

	pr_debug("  %05X-%05X %s\n", last_fixed_start,
		 last_fixed_end - 1, mtrr_attrib_to_str(last_fixed_type));

	last_fixed_end = 0;
}

static void __init update_fixed_last(unsigned base, unsigned end,
				     mtrr_type type)
{
	last_fixed_start = base;
	last_fixed_end = end;
	last_fixed_type = type;
}

static void __init
print_fixed(unsigned base, unsigned step, const mtrr_type *types)
{
	unsigned i;

	for (i = 0; i < 8; ++i, ++types, base += step) {
		if (last_fixed_end == 0) {
			update_fixed_last(base, base + step, *types);
			continue;
		}
		if (last_fixed_end == base && last_fixed_type == *types) {
			last_fixed_end = base + step;
			continue;
		}
		/* new segments: gap or different type */
		print_fixed_last();
		update_fixed_last(base, base + step, *types);
	}
}

static void prepare_set(void);
static void post_set(void);

static void __init print_mtrr_state(void)
{
	unsigned int i;
	int high_width;

	pr_debug("MTRR default type: %s\n",
		 mtrr_attrib_to_str(mtrr_state.def_type));
	if (mtrr_state.have_fixed) {
		pr_debug("MTRR fixed ranges %sabled:\n",
			 mtrr_state.enabled & 1 ? "en" : "dis");
		print_fixed(0x00000, 0x10000, mtrr_state.fixed_ranges + 0);
		for (i = 0; i < 2; ++i)
			print_fixed(0x80000 + i * 0x20000, 0x04000,
				    mtrr_state.fixed_ranges + (i + 1) * 8);
		for (i = 0; i < 8; ++i)
			print_fixed(0xC0000 + i * 0x08000, 0x01000,
				    mtrr_state.fixed_ranges + (i + 3) * 8);

		/* tail */
		print_fixed_last();
	}
	pr_debug("MTRR variable ranges %sabled:\n",
		 mtrr_state.enabled & 2 ? "en" : "dis");
	if (size_or_mask & 0xffffffffUL)
		high_width = ffs(size_or_mask & 0xffffffffUL) - 1;
	else
		high_width = ffs(size_or_mask>>32) + 32 - 1;
	high_width = (high_width - (32 - PAGE_SHIFT) + 3) / 4;

	for (i = 0; i < num_var_ranges; ++i) {
		if (mtrr_state.var_ranges[i].mask_lo & (1 << 11))
			pr_debug("  %u base %0*X%05X000 mask %0*X%05X000 %s\n",
				 i,
				 high_width,
				 mtrr_state.var_ranges[i].base_hi,
				 mtrr_state.var_ranges[i].base_lo >> 12,
				 high_width,
				 mtrr_state.var_ranges[i].mask_hi,
				 mtrr_state.var_ranges[i].mask_lo >> 12,
				 mtrr_attrib_to_str(mtrr_state.var_ranges[i].base_lo & 0xff));
		else
			pr_debug("  %u disabled\n", i);
	}
	if (mtrr_tom2)
		pr_debug("TOM2: %016llx aka %lldM\n", mtrr_tom2, mtrr_tom2>>20);
}

/* Grab all of the MTRR state for this CPU into *state */
void __init get_mtrr_state(void)
{
	struct mtrr_var_range *vrs;
	unsigned long flags;
	unsigned lo, dummy;
	unsigned int i;

	vrs = mtrr_state.var_ranges;

	rdmsr(MSR_MTRRcap, lo, dummy);
	mtrr_state.have_fixed = (lo >> 8) & 1;

	for (i = 0; i < num_var_ranges; i++)
		get_mtrr_var_range(i, &vrs[i]);
	if (mtrr_state.have_fixed)
		get_fixed_ranges(mtrr_state.fixed_ranges);

	rdmsr(MSR_MTRRdefType, lo, dummy);
	mtrr_state.def_type = (lo & 0xff);
	mtrr_state.enabled = (lo & 0xc00) >> 10;

	if (amd_special_default_mtrr()) {
		unsigned low, high;

		/* TOP_MEM2 */
		rdmsr(MSR_K8_TOP_MEM2, low, high);
		mtrr_tom2 = high;
		mtrr_tom2 <<= 32;
		mtrr_tom2 |= low;
		mtrr_tom2 &= 0xffffff800000ULL;
	}

	print_mtrr_state();

	mtrr_state_set = 1;

	/* PAT setup for BP. We need to go through sync steps here */
	local_irq_save(flags);
	prepare_set();

	pat_init();

	post_set();
	local_irq_restore(flags);
}

/* Some BIOS's are messed up and don't set all MTRRs the same! */
void __init mtrr_state_warn(void)
{
	unsigned long mask = smp_changes_mask;

	if (!mask)
		return;
	if (mask & MTRR_CHANGE_MASK_FIXED)
		pr_warning("mtrr: your CPUs had inconsistent fixed MTRR settings\n");
	if (mask & MTRR_CHANGE_MASK_VARIABLE)
		pr_warning("mtrr: your CPUs had inconsistent variable MTRR settings\n");
	if (mask & MTRR_CHANGE_MASK_DEFTYPE)
		pr_warning("mtrr: your CPUs had inconsistent MTRRdefType settings\n");

	printk(KERN_INFO "mtrr: probably your BIOS does not setup all CPUs.\n");
	printk(KERN_INFO "mtrr: corrected configuration.\n");
}

/*
 * Doesn't attempt to pass an error out to MTRR users
 * because it's quite complicated in some cases and probably not
 * worth it because the best error handling is to ignore it.
 */
void mtrr_wrmsr(unsigned msr, unsigned a, unsigned b)
{
	if (wrmsr_safe(msr, a, b) < 0) {
		printk(KERN_ERR
			"MTRR: CPU %u: Writing MSR %x to %x:%x failed\n",
			smp_processor_id(), msr, a, b);
	}
}

/**
 * set_fixed_range - checks & updates a fixed-range MTRR if it
 *		     differs from the value it should have
 * @msr: MSR address of the MTTR which should be checked and updated
 * @changed: pointer which indicates whether the MTRR needed to be changed
 * @msrwords: pointer to the MSR values which the MSR should have
 */
static void set_fixed_range(int msr, bool *changed, unsigned int *msrwords)
{
	unsigned lo, hi;

	rdmsr(msr, lo, hi);

	if (lo != msrwords[0] || hi != msrwords[1]) {
		mtrr_wrmsr(msr, msrwords[0], msrwords[1]);
		*changed = true;
	}
}

/**
 * generic_get_free_region - Get a free MTRR.
 * @base: The starting (base) address of the region.
 * @size: The size (in bytes) of the region.
 * @replace_reg: mtrr index to be replaced; set to invalid value if none.
 *
 * Returns: The index of the region on success, else negative on error.
 */
int
generic_get_free_region(unsigned long base, unsigned long size, int replace_reg)
{
	unsigned long lbase, lsize;
	mtrr_type ltype;
	int i, max;

	max = num_var_ranges;
	if (replace_reg >= 0 && replace_reg < max)
		return replace_reg;

	for (i = 0; i < max; ++i) {
		mtrr_if->get(i, &lbase, &lsize, &ltype);
		if (lsize == 0)
			return i;
	}

	return -ENOSPC;
}

static void generic_get_mtrr(unsigned int reg, unsigned long *base,
			     unsigned long *size, mtrr_type *type)
{
	unsigned int mask_lo, mask_hi, base_lo, base_hi;
	unsigned int tmp, hi;
	int cpu;

	/*
	 * get_mtrr doesn't need to update mtrr_state, also it could be called
	 * from any cpu, so try to print it out directly.
	 */
	cpu = get_cpu();

	rdmsr(MTRRphysMask_MSR(reg), mask_lo, mask_hi);

	if ((mask_lo & 0x800) == 0) {
		/*  Invalid (i.e. free) range */
		*base = 0;
		*size = 0;
		*type = 0;
		goto out_put_cpu;
	}

	rdmsr(MTRRphysBase_MSR(reg), base_lo, base_hi);

	/* Work out the shifted address mask: */
	tmp = mask_hi << (32 - PAGE_SHIFT) | mask_lo >> PAGE_SHIFT;
	mask_lo = size_or_mask | tmp;

	/* Expand tmp with high bits to all 1s: */
	hi = fls(tmp);
	if (hi > 0) {
		tmp |= ~((1<<(hi - 1)) - 1);

		if (tmp != mask_lo) {
			printk(KERN_WARNING "mtrr: your BIOS has configured an incorrect mask, fixing it.\n");
			mask_lo = tmp;
		}
	}

	/*
	 * This works correctly if size is a power of two, i.e. a
	 * contiguous range:
	 */
	*size = -mask_lo;
	*base = base_hi << (32 - PAGE_SHIFT) | base_lo >> PAGE_SHIFT;
	*type = base_lo & 0xff;

out_put_cpu:
	put_cpu();
}

/**
 * set_fixed_ranges - checks & updates the fixed-range MTRRs if they
 *		      differ from the saved set
 * @frs: pointer to fixed-range MTRR values, saved by get_fixed_ranges()
 */
static int set_fixed_ranges(mtrr_type *frs)
{
	unsigned long long *saved = (unsigned long long *)frs;
	bool changed = false;
	int block = -1, range;

	k8_check_syscfg_dram_mod_en();

	while (fixed_range_blocks[++block].ranges) {
		for (range = 0; range < fixed_range_blocks[block].ranges; range++)
			set_fixed_range(fixed_range_blocks[block].base_msr + range,
					&changed, (unsigned int *)saved++);
	}

	return changed;
}

/*
 * Set the MSR pair relating to a var range.
 * Returns true if changes are made.
 */
static bool set_mtrr_var_ranges(unsigned int index, struct mtrr_var_range *vr)
{
	unsigned int lo, hi;
	bool changed = false;

	rdmsr(MTRRphysBase_MSR(index), lo, hi);
	if ((vr->base_lo & 0xfffff0ffUL) != (lo & 0xfffff0ffUL)
	    || (vr->base_hi & (size_and_mask >> (32 - PAGE_SHIFT))) !=
		(hi & (size_and_mask >> (32 - PAGE_SHIFT)))) {

		mtrr_wrmsr(MTRRphysBase_MSR(index), vr->base_lo, vr->base_hi);
		changed = true;
	}

	rdmsr(MTRRphysMask_MSR(index), lo, hi);

	if ((vr->mask_lo & 0xfffff800UL) != (lo & 0xfffff800UL)
	    || (vr->mask_hi & (size_and_mask >> (32 - PAGE_SHIFT))) !=
		(hi & (size_and_mask >> (32 - PAGE_SHIFT)))) {
		mtrr_wrmsr(MTRRphysMask_MSR(index), vr->mask_lo, vr->mask_hi);
		changed = true;
	}
	return changed;
}

static u32 deftype_lo, deftype_hi;

/**
 * set_mtrr_state - Set the MTRR state for this CPU.
 *
 * NOTE: The CPU must already be in a safe state for MTRR changes.
 * RETURNS: 0 if no changes made, else a mask indicating what was changed.
 */
static unsigned long set_mtrr_state(void)
{
	unsigned long change_mask = 0;
	unsigned int i;

	for (i = 0; i < num_var_ranges; i++) {
		if (set_mtrr_var_ranges(i, &mtrr_state.var_ranges[i]))
			change_mask |= MTRR_CHANGE_MASK_VARIABLE;
	}

	if (mtrr_state.have_fixed && set_fixed_ranges(mtrr_state.fixed_ranges))
		change_mask |= MTRR_CHANGE_MASK_FIXED;

	/*
	 * Set_mtrr_restore restores the old value of MTRRdefType,
	 * so to set it we fiddle with the saved value:
	 */
	if ((deftype_lo & 0xff) != mtrr_state.def_type
	    || ((deftype_lo & 0xc00) >> 10) != mtrr_state.enabled) {

		deftype_lo = (deftype_lo & ~0xcff) | mtrr_state.def_type |
			     (mtrr_state.enabled << 10);
		change_mask |= MTRR_CHANGE_MASK_DEFTYPE;
	}

	return change_mask;
}


static unsigned long cr4;
static DEFINE_RAW_SPINLOCK(set_atomicity_lock);

/*
 * Since we are disabling the cache don't allow any interrupts,
 * they would run extremely slow and would only increase the pain.
 *
 * The caller must ensure that local interrupts are disabled and
 * are reenabled after post_set() has been called.
 */
static void prepare_set(void) __acquires(set_atomicity_lock)
{
	unsigned long cr0;

	/*
	 * Note that this is not ideal
	 * since the cache is only flushed/disabled for this CPU while the
	 * MTRRs are changed, but changing this requires more invasive
	 * changes to the way the kernel boots
	 */

	raw_spin_lock(&set_atomicity_lock);

	/* Enter the no-fill (CD=1, NW=0) cache mode and flush caches. */
	cr0 = read_cr0() | X86_CR0_CD;
	write_cr0(cr0);
	wbinvd();

	/* Save value of CR4 and clear Page Global Enable (bit 7) */
	if (cpu_has_pge) {
		cr4 = read_cr4();
		write_cr4(cr4 & ~X86_CR4_PGE);
	}

	/* Flush all TLBs via a mov %cr3, %reg; mov %reg, %cr3 */
	__flush_tlb();

	/* Save MTRR state */
	rdmsr(MSR_MTRRdefType, deftype_lo, deftype_hi);

	/* Disable MTRRs, and set the default type to uncached */
	mtrr_wrmsr(MSR_MTRRdefType, deftype_lo & ~0xcff, deftype_hi);
}

static void post_set(void) __releases(set_atomicity_lock)
{
	/* Flush TLBs (no need to flush caches - they are disabled) */
	__flush_tlb();

	/* Intel (P6) standard MTRRs */
	mtrr_wrmsr(MSR_MTRRdefType, deftype_lo, deftype_hi);

	/* Enable caches */
	write_cr0(read_cr0() & 0xbfffffff);

	/* Restore value of CR4 */
	if (cpu_has_pge)
		write_cr4(cr4);
	raw_spin_unlock(&set_atomicity_lock);
}

static void generic_set_all(void)
{
	unsigned long mask, count;
	unsigned long flags;

	local_irq_save(flags);
	prepare_set();

	/* Actually set the state */
	mask = set_mtrr_state();

	/* also set PAT */
	pat_init();

	post_set();
	local_irq_restore(flags);

	/* Use the atomic bitops to update the global mask */
	for (count = 0; count < sizeof mask * 8; ++count) {
		if (mask & 0x01)
			set_bit(count, &smp_changes_mask);
		mask >>= 1;
	}

}

/**
 * generic_set_mtrr - set variable MTRR register on the local CPU.
 *
 * @reg: The register to set.
 * @base: The base address of the region.
 * @size: The size of the region. If this is 0 the region is disabled.
 * @type: The type of the region.
 *
 * Returns nothing.
 */
static void generic_set_mtrr(unsigned int reg, unsigned long base,
			     unsigned long size, mtrr_type type)
{
	unsigned long flags;
	struct mtrr_var_range *vr;

	vr = &mtrr_state.var_ranges[reg];

	local_irq_save(flags);
	prepare_set();

	if (size == 0) {
		/*
		 * The invalid bit is kept in the mask, so we simply
		 * clear the relevant mask register to disable a range.
		 */
		mtrr_wrmsr(MTRRphysMask_MSR(reg), 0, 0);
		memset(vr, 0, sizeof(struct mtrr_var_range));
	} else {
		vr->base_lo = base << PAGE_SHIFT | type;
		vr->base_hi = (base & size_and_mask) >> (32 - PAGE_SHIFT);
		vr->mask_lo = -size << PAGE_SHIFT | 0x800;
		vr->mask_hi = (-size & size_and_mask) >> (32 - PAGE_SHIFT);

		mtrr_wrmsr(MTRRphysBase_MSR(reg), vr->base_lo, vr->base_hi);
		mtrr_wrmsr(MTRRphysMask_MSR(reg), vr->mask_lo, vr->mask_hi);
	}

	post_set();
	local_irq_restore(flags);
}

int generic_validate_add_page(unsigned long base, unsigned long size,
			      unsigned int type)
{
	unsigned long lbase, last;

	/*
	 * For Intel PPro stepping <= 7
	 * must be 4 MiB aligned and not touch 0x70000000 -> 0x7003FFFF
	 */
	if (is_cpu(INTEL) && boot_cpu_data.x86 == 6 &&
	    boot_cpu_data.x86_model == 1 &&
	    boot_cpu_data.x86_mask <= 7) {
		if (base & ((1 << (22 - PAGE_SHIFT)) - 1)) {
			pr_warning("mtrr: base(0x%lx000) is not 4 MiB aligned\n", base);
			return -EINVAL;
		}
		if (!(base + size < 0x70000 || base > 0x7003F) &&
		    (type == MTRR_TYPE_WRCOMB
		     || type == MTRR_TYPE_WRBACK)) {
			pr_warning("mtrr: writable mtrr between 0x70000000 and 0x7003FFFF may hang the CPU.\n");
			return -EINVAL;
		}
	}

	/*
	 * Check upper bits of base and last are equal and lower bits are 0
	 * for base and 1 for last
	 */
	last = base + size - 1;
	for (lbase = base; !(lbase & 1) && (last & 1);
	     lbase = lbase >> 1, last = last >> 1)
		;
	if (lbase != last) {
		pr_warning("mtrr: base(0x%lx000) is not aligned on a size(0x%lx000) boundary\n", base, size);
		return -EINVAL;
	}
	return 0;
}

static int generic_have_wrcomb(void)
{
	unsigned long config, dummy;
	rdmsr(MSR_MTRRcap, config, dummy);
	return config & (1 << 10);
}

int positive_have_wrcomb(void)
{
	return 1;
}

/*
 * Generic structure...
 */
const struct mtrr_ops generic_mtrr_ops = {
	.use_intel_if		= 1,
	.set_all		= generic_set_all,
	.get			= generic_get_mtrr,
	.get_free_region	= generic_get_free_region,
	.set			= generic_set_mtrr,
	.validate_add_page	= generic_validate_add_page,
	.have_wrcomb		= generic_have_wrcomb,
};
