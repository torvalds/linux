#include <linux/init.h>
#include <linux/mm.h>
#include <asm/mtrr.h>
#include <asm/msr.h>
#include <asm/io.h>
#include "mtrr.h"

int arr3_protected;

static void
cyrix_get_arr(unsigned int reg, unsigned long *base,
	      unsigned int *size, mtrr_type * type)
{
	unsigned long flags;
	unsigned char arr, ccr3, rcr, shift;

	arr = CX86_ARR_BASE + (reg << 1) + reg;	/* avoid multiplication by 3 */

	/* Save flags and disable interrupts */
	local_irq_save(flags);

	ccr3 = getCx86(CX86_CCR3);
	setCx86(CX86_CCR3, (ccr3 & 0x0f) | 0x10);	/* enable MAPEN */
	((unsigned char *) base)[3] = getCx86(arr);
	((unsigned char *) base)[2] = getCx86(arr + 1);
	((unsigned char *) base)[1] = getCx86(arr + 2);
	rcr = getCx86(CX86_RCR_BASE + reg);
	setCx86(CX86_CCR3, ccr3);	/* disable MAPEN */

	/* Enable interrupts if it was enabled previously */
	local_irq_restore(flags);
	shift = ((unsigned char *) base)[1] & 0x0f;
	*base >>= PAGE_SHIFT;

	/* Power of two, at least 4K on ARR0-ARR6, 256K on ARR7
	 * Note: shift==0xf means 4G, this is unsupported.
	 */
	if (shift)
		*size = (reg < 7 ? 0x1UL : 0x40UL) << (shift - 1);
	else
		*size = 0;

	/* Bit 0 is Cache Enable on ARR7, Cache Disable on ARR0-ARR6 */
	if (reg < 7) {
		switch (rcr) {
		case 1:
			*type = MTRR_TYPE_UNCACHABLE;
			break;
		case 8:
			*type = MTRR_TYPE_WRBACK;
			break;
		case 9:
			*type = MTRR_TYPE_WRCOMB;
			break;
		case 24:
		default:
			*type = MTRR_TYPE_WRTHROUGH;
			break;
		}
	} else {
		switch (rcr) {
		case 0:
			*type = MTRR_TYPE_UNCACHABLE;
			break;
		case 8:
			*type = MTRR_TYPE_WRCOMB;
			break;
		case 9:
			*type = MTRR_TYPE_WRBACK;
			break;
		case 25:
		default:
			*type = MTRR_TYPE_WRTHROUGH;
			break;
		}
	}
}

static int
cyrix_get_free_region(unsigned long base, unsigned long size)
/*  [SUMMARY] Get a free ARR.
    <base> The starting (base) address of the region.
    <size> The size (in bytes) of the region.
    [RETURNS] The index of the region on success, else -1 on error.
*/
{
	int i;
	mtrr_type ltype;
	unsigned long lbase;
	unsigned int  lsize;

	/* If we are to set up a region >32M then look at ARR7 immediately */
	if (size > 0x2000) {
		cyrix_get_arr(7, &lbase, &lsize, &ltype);
		if (lsize == 0)
			return 7;
		/*  Else try ARR0-ARR6 first  */
	} else {
		for (i = 0; i < 7; i++) {
			cyrix_get_arr(i, &lbase, &lsize, &ltype);
			if ((i == 3) && arr3_protected)
				continue;
			if (lsize == 0)
				return i;
		}
		/* ARR0-ARR6 isn't free, try ARR7 but its size must be at least 256K */
		cyrix_get_arr(i, &lbase, &lsize, &ltype);
		if ((lsize == 0) && (size >= 0x40))
			return i;
	}
	return -ENOSPC;
}

static u32 cr4 = 0;
static u32 ccr3;

static void prepare_set(void)
{
	u32 cr0;

	/*  Save value of CR4 and clear Page Global Enable (bit 7)  */
	if ( cpu_has_pge ) {
		cr4 = read_cr4();
		write_cr4(cr4 & (unsigned char) ~(1 << 7));
	}

	/*  Disable and flush caches. Note that wbinvd flushes the TLBs as
	    a side-effect  */
	cr0 = read_cr0() | 0x40000000;
	wbinvd();
	write_cr0(cr0);
	wbinvd();

	/* Cyrix ARRs - everything else were excluded at the top */
	ccr3 = getCx86(CX86_CCR3);

	/* Cyrix ARRs - everything else were excluded at the top */
	setCx86(CX86_CCR3, (ccr3 & 0x0f) | 0x10);

}

static void post_set(void)
{
	/*  Flush caches and TLBs  */
	wbinvd();

	/* Cyrix ARRs - everything else was excluded at the top */
	setCx86(CX86_CCR3, ccr3);
		
	/*  Enable caches  */
	write_cr0(read_cr0() & 0xbfffffff);

	/*  Restore value of CR4  */
	if ( cpu_has_pge )
		write_cr4(cr4);
}

static void cyrix_set_arr(unsigned int reg, unsigned long base,
			  unsigned long size, mtrr_type type)
{
	unsigned char arr, arr_type, arr_size;

	arr = CX86_ARR_BASE + (reg << 1) + reg;	/* avoid multiplication by 3 */

	/* count down from 32M (ARR0-ARR6) or from 2G (ARR7) */
	if (reg >= 7)
		size >>= 6;

	size &= 0x7fff;		/* make sure arr_size <= 14 */
	for (arr_size = 0; size; arr_size++, size >>= 1) ;

	if (reg < 7) {
		switch (type) {
		case MTRR_TYPE_UNCACHABLE:
			arr_type = 1;
			break;
		case MTRR_TYPE_WRCOMB:
			arr_type = 9;
			break;
		case MTRR_TYPE_WRTHROUGH:
			arr_type = 24;
			break;
		default:
			arr_type = 8;
			break;
		}
	} else {
		switch (type) {
		case MTRR_TYPE_UNCACHABLE:
			arr_type = 0;
			break;
		case MTRR_TYPE_WRCOMB:
			arr_type = 8;
			break;
		case MTRR_TYPE_WRTHROUGH:
			arr_type = 25;
			break;
		default:
			arr_type = 9;
			break;
		}
	}

	prepare_set();

	base <<= PAGE_SHIFT;
	setCx86(arr, ((unsigned char *) &base)[3]);
	setCx86(arr + 1, ((unsigned char *) &base)[2]);
	setCx86(arr + 2, (((unsigned char *) &base)[1]) | arr_size);
	setCx86(CX86_RCR_BASE + reg, arr_type);

	post_set();
}

typedef struct {
	unsigned long base;
	unsigned int size;
	mtrr_type type;
} arr_state_t;

static arr_state_t arr_state[8] __devinitdata = {
	{0UL, 0UL, 0UL}, {0UL, 0UL, 0UL}, {0UL, 0UL, 0UL}, {0UL, 0UL, 0UL},
	{0UL, 0UL, 0UL}, {0UL, 0UL, 0UL}, {0UL, 0UL, 0UL}, {0UL, 0UL, 0UL}
};

static unsigned char ccr_state[7] __devinitdata = { 0, 0, 0, 0, 0, 0, 0 };

static void cyrix_set_all(void)
{
	int i;

	prepare_set();

	/* the CCRs are not contiguous */
	for (i = 0; i < 4; i++)
		setCx86(CX86_CCR0 + i, ccr_state[i]);
	for (; i < 7; i++)
		setCx86(CX86_CCR4 + i, ccr_state[i]);
	for (i = 0; i < 8; i++)
		cyrix_set_arr(i, arr_state[i].base, 
			      arr_state[i].size, arr_state[i].type);

	post_set();
}

#if 0
/*
 * On Cyrix 6x86(MX) and M II the ARR3 is special: it has connection
 * with the SMM (System Management Mode) mode. So we need the following:
 * Check whether SMI_LOCK (CCR3 bit 0) is set
 *   if it is set, write a warning message: ARR3 cannot be changed!
 *     (it cannot be changed until the next processor reset)
 *   if it is reset, then we can change it, set all the needed bits:
 *   - disable access to SMM memory through ARR3 range (CCR1 bit 7 reset)
 *   - disable access to SMM memory (CCR1 bit 2 reset)
 *   - disable SMM mode (CCR1 bit 1 reset)
 *   - disable write protection of ARR3 (CCR6 bit 1 reset)
 *   - (maybe) disable ARR3
 * Just to be sure, we enable ARR usage by the processor (CCR5 bit 5 set)
 */
static void __init
cyrix_arr_init(void)
{
	struct set_mtrr_context ctxt;
	unsigned char ccr[7];
	int ccrc[7] = { 0, 0, 0, 0, 0, 0, 0 };
#ifdef CONFIG_SMP
	int i;
#endif

	/* flush cache and enable MAPEN */
	set_mtrr_prepare_save(&ctxt);
	set_mtrr_cache_disable(&ctxt);

	/* Save all CCRs locally */
	ccr[0] = getCx86(CX86_CCR0);
	ccr[1] = getCx86(CX86_CCR1);
	ccr[2] = getCx86(CX86_CCR2);
	ccr[3] = ctxt.ccr3;
	ccr[4] = getCx86(CX86_CCR4);
	ccr[5] = getCx86(CX86_CCR5);
	ccr[6] = getCx86(CX86_CCR6);

	if (ccr[3] & 1) {
		ccrc[3] = 1;
		arr3_protected = 1;
	} else {
		/* Disable SMM mode (bit 1), access to SMM memory (bit 2) and
		 * access to SMM memory through ARR3 (bit 7).
		 */
		if (ccr[1] & 0x80) {
			ccr[1] &= 0x7f;
			ccrc[1] |= 0x80;
		}
		if (ccr[1] & 0x04) {
			ccr[1] &= 0xfb;
			ccrc[1] |= 0x04;
		}
		if (ccr[1] & 0x02) {
			ccr[1] &= 0xfd;
			ccrc[1] |= 0x02;
		}
		arr3_protected = 0;
		if (ccr[6] & 0x02) {
			ccr[6] &= 0xfd;
			ccrc[6] = 1;	/* Disable write protection of ARR3 */
			setCx86(CX86_CCR6, ccr[6]);
		}
		/* Disable ARR3. This is safe now that we disabled SMM. */
		/* cyrix_set_arr_up (3, 0, 0, 0, FALSE); */
	}
	/* If we changed CCR1 in memory, change it in the processor, too. */
	if (ccrc[1])
		setCx86(CX86_CCR1, ccr[1]);

	/* Enable ARR usage by the processor */
	if (!(ccr[5] & 0x20)) {
		ccr[5] |= 0x20;
		ccrc[5] = 1;
		setCx86(CX86_CCR5, ccr[5]);
	}
#ifdef CONFIG_SMP
	for (i = 0; i < 7; i++)
		ccr_state[i] = ccr[i];
	for (i = 0; i < 8; i++)
		cyrix_get_arr(i,
			      &arr_state[i].base, &arr_state[i].size,
			      &arr_state[i].type);
#endif

	set_mtrr_done(&ctxt);	/* flush cache and disable MAPEN */

	if (ccrc[5])
		printk(KERN_INFO "mtrr: ARR usage was not enabled, enabled manually\n");
	if (ccrc[3])
		printk(KERN_INFO "mtrr: ARR3 cannot be changed\n");
/*
    if ( ccrc[1] & 0x80) printk ("mtrr: SMM memory access through ARR3 disabled\n");
    if ( ccrc[1] & 0x04) printk ("mtrr: SMM memory access disabled\n");
    if ( ccrc[1] & 0x02) printk ("mtrr: SMM mode disabled\n");
*/
	if (ccrc[6])
		printk(KERN_INFO "mtrr: ARR3 was write protected, unprotected\n");
}
#endif

static struct mtrr_ops cyrix_mtrr_ops = {
	.vendor            = X86_VENDOR_CYRIX,
//	.init              = cyrix_arr_init,
	.set_all	   = cyrix_set_all,
	.set               = cyrix_set_arr,
	.get               = cyrix_get_arr,
	.get_free_region   = cyrix_get_free_region,
	.validate_add_page = generic_validate_add_page,
	.have_wrcomb       = positive_have_wrcomb,
};

int __init cyrix_init_mtrr(void)
{
	set_mtrr_ops(&cyrix_mtrr_ops);
	return 0;
}

//arch_initcall(cyrix_init_mtrr);
