#include <linux/init.h>
#include <linux/mm.h>
#include <asm/mtrr.h>
#include <asm/msr.h>
#include "mtrr.h"

static struct {
	unsigned long high;
	unsigned long low;
} centaur_mcr[8];

static u8 centaur_mcr_reserved;
static u8 centaur_mcr_type;	/* 0 for winchip, 1 for winchip2 */

/*
 *	Report boot time MCR setups 
 */

static int
centaur_get_free_region(unsigned long base, unsigned long size, int replace_reg)
/*  [SUMMARY] Get a free MTRR.
    <base> The starting (base) address of the region.
    <size> The size (in bytes) of the region.
    [RETURNS] The index of the region on success, else -1 on error.
*/
{
	int i, max;
	mtrr_type ltype;
	unsigned long lbase, lsize;

	max = num_var_ranges;
	if (replace_reg >= 0 && replace_reg < max)
		return replace_reg;
	for (i = 0; i < max; ++i) {
		if (centaur_mcr_reserved & (1 << i))
			continue;
		mtrr_if->get(i, &lbase, &lsize, &ltype);
		if (lsize == 0)
			return i;
	}
	return -ENOSPC;
}

void
mtrr_centaur_report_mcr(int mcr, u32 lo, u32 hi)
{
	centaur_mcr[mcr].low = lo;
	centaur_mcr[mcr].high = hi;
}

static void
centaur_get_mcr(unsigned int reg, unsigned long *base,
		unsigned long *size, mtrr_type * type)
{
	*base = centaur_mcr[reg].high >> PAGE_SHIFT;
	*size = -(centaur_mcr[reg].low & 0xfffff000) >> PAGE_SHIFT;
	*type = MTRR_TYPE_WRCOMB;	/*  If it is there, it is write-combining  */
	if (centaur_mcr_type == 1 && ((centaur_mcr[reg].low & 31) & 2))
		*type = MTRR_TYPE_UNCACHABLE;
	if (centaur_mcr_type == 1 && (centaur_mcr[reg].low & 31) == 25)
		*type = MTRR_TYPE_WRBACK;
	if (centaur_mcr_type == 0 && (centaur_mcr[reg].low & 31) == 31)
		*type = MTRR_TYPE_WRBACK;

}

static void centaur_set_mcr(unsigned int reg, unsigned long base,
			    unsigned long size, mtrr_type type)
{
	unsigned long low, high;

	if (size == 0) {
		/*  Disable  */
		high = low = 0;
	} else {
		high = base << PAGE_SHIFT;
		if (centaur_mcr_type == 0)
			low = -size << PAGE_SHIFT | 0x1f;	/* only support write-combining... */
		else {
			if (type == MTRR_TYPE_UNCACHABLE)
				low = -size << PAGE_SHIFT | 0x02;	/* NC */
			else
				low = -size << PAGE_SHIFT | 0x09;	/* WWO,WC */
		}
	}
	centaur_mcr[reg].high = high;
	centaur_mcr[reg].low = low;
	wrmsr(MSR_IDT_MCR0 + reg, low, high);
}

#if 0
/*
 *	Initialise the later (saner) Winchip MCR variant. In this version
 *	the BIOS can pass us the registers it has used (but not their values)
 *	and the control register is read/write
 */

static void __init
centaur_mcr1_init(void)
{
	unsigned i;
	u32 lo, hi;

	/* Unfortunately, MCR's are read-only, so there is no way to
	 * find out what the bios might have done.
	 */

	rdmsr(MSR_IDT_MCR_CTRL, lo, hi);
	if (((lo >> 17) & 7) == 1) {	/* Type 1 Winchip2 MCR */
		lo &= ~0x1C0;	/* clear key */
		lo |= 0x040;	/* set key to 1 */
		wrmsr(MSR_IDT_MCR_CTRL, lo, hi);	/* unlock MCR */
	}

	centaur_mcr_type = 1;

	/*
	 *  Clear any unconfigured MCR's.
	 */

	for (i = 0; i < 8; ++i) {
		if (centaur_mcr[i].high == 0 && centaur_mcr[i].low == 0) {
			if (!(lo & (1 << (9 + i))))
				wrmsr(MSR_IDT_MCR0 + i, 0, 0);
			else
				/*
				 *      If the BIOS set up an MCR we cannot see it
				 *      but we don't wish to obliterate it
				 */
				centaur_mcr_reserved |= (1 << i);
		}
	}
	/*  
	 *  Throw the main write-combining switch... 
	 *  However if OOSTORE is enabled then people have already done far
	 *  cleverer things and we should behave. 
	 */

	lo |= 15;		/* Write combine enables */
	wrmsr(MSR_IDT_MCR_CTRL, lo, hi);
}

/*
 *	Initialise the original winchip with read only MCR registers
 *	no used bitmask for the BIOS to pass on and write only control
 */

static void __init
centaur_mcr0_init(void)
{
	unsigned i;

	/* Unfortunately, MCR's are read-only, so there is no way to
	 * find out what the bios might have done.
	 */

	/* Clear any unconfigured MCR's.
	 * This way we are sure that the centaur_mcr array contains the actual
	 * values. The disadvantage is that any BIOS tweaks are thus undone.
	 *
	 */
	for (i = 0; i < 8; ++i) {
		if (centaur_mcr[i].high == 0 && centaur_mcr[i].low == 0)
			wrmsr(MSR_IDT_MCR0 + i, 0, 0);
	}

	wrmsr(MSR_IDT_MCR_CTRL, 0x01F0001F, 0);	/* Write only */
}

/*
 *	Initialise Winchip series MCR registers
 */

static void __init
centaur_mcr_init(void)
{
	struct set_mtrr_context ctxt;

	set_mtrr_prepare_save(&ctxt);
	set_mtrr_cache_disable(&ctxt);

	if (boot_cpu_data.x86_model == 4)
		centaur_mcr0_init();
	else if (boot_cpu_data.x86_model == 8 || boot_cpu_data.x86_model == 9)
		centaur_mcr1_init();

	set_mtrr_done(&ctxt);
}
#endif

static int centaur_validate_add_page(unsigned long base, 
				     unsigned long size, unsigned int type)
{
	/*
	 *  FIXME: Winchip2 supports uncached
	 */
	if (type != MTRR_TYPE_WRCOMB && 
	    (centaur_mcr_type == 0 || type != MTRR_TYPE_UNCACHABLE)) {
		printk(KERN_WARNING
		       "mtrr: only write-combining%s supported\n",
		       centaur_mcr_type ? " and uncacheable are"
		       : " is");
		return -EINVAL;
	}
	return 0;
}

static struct mtrr_ops centaur_mtrr_ops = {
	.vendor            = X86_VENDOR_CENTAUR,
//	.init              = centaur_mcr_init,
	.set               = centaur_set_mcr,
	.get               = centaur_get_mcr,
	.get_free_region   = centaur_get_free_region,
	.validate_add_page = centaur_validate_add_page,
	.have_wrcomb       = positive_have_wrcomb,
};

int __init centaur_init_mtrr(void)
{
	set_mtrr_ops(&centaur_mtrr_ops);
	return 0;
}

//arch_initcall(centaur_init_mtrr);
