#include <linux/init.h>
#include <linux/mm.h>
#include <asm/mtrr.h>
#include <asm/msr.h>
#include <asm/io.h>
#include <asm/processor-cyrix.h>
#include <asm/processor-flags.h>
#include "mtrr.h"

static void
cyrix_get_arr(unsigned int reg, unsigned long *base,
	      unsigned long *size, mtrr_type * type)
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
cyrix_get_free_region(unsigned long base, unsigned long size, int replace_reg)
/*  [SUMMARY] Get a free ARR.
    <base> The starting (base) address of the region.
    <size> The size (in bytes) of the region.
    [RETURNS] The index of the region on success, else -1 on error.
*/
{
	int i;
	mtrr_type ltype;
	unsigned long lbase, lsize;

	switch (replace_reg) {
	case 7:
		if (size < 0x40)
			break;
	case 6:
	case 5:
	case 4:
		return replace_reg;
	case 3:
	case 2:
	case 1:
	case 0:
		return replace_reg;
	}
	/* If we are to set up a region >32M then look at ARR7 immediately */
	if (size > 0x2000) {
		cyrix_get_arr(7, &lbase, &lsize, &ltype);
		if (lsize == 0)
			return 7;
		/*  Else try ARR0-ARR6 first  */
	} else {
		for (i = 0; i < 7; i++) {
			cyrix_get_arr(i, &lbase, &lsize, &ltype);
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
		write_cr4(cr4 & ~X86_CR4_PGE);
	}

	/*  Disable and flush caches. Note that wbinvd flushes the TLBs as
	    a side-effect  */
	cr0 = read_cr0() | X86_CR0_CD;
	wbinvd();
	write_cr0(cr0);
	wbinvd();

	/* Cyrix ARRs - everything else was excluded at the top */
	ccr3 = getCx86(CX86_CCR3);

	/* Cyrix ARRs - everything else was excluded at the top */
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
	unsigned long size;
	mtrr_type type;
} arr_state_t;

static arr_state_t arr_state[8] = {
	{0UL, 0UL, 0UL}, {0UL, 0UL, 0UL}, {0UL, 0UL, 0UL}, {0UL, 0UL, 0UL},
	{0UL, 0UL, 0UL}, {0UL, 0UL, 0UL}, {0UL, 0UL, 0UL}, {0UL, 0UL, 0UL}
};

static unsigned char ccr_state[7] = { 0, 0, 0, 0, 0, 0, 0 };

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
