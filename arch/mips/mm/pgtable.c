/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <asm/pgalloc.h>

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *init, *ret = NULL;
	struct ptdesc *ptdesc = pagetable_alloc(GFP_KERNEL & ~__GFP_HIGHMEM,
			PGD_TABLE_ORDER);

	if (ptdesc) {
		ret = ptdesc_address(ptdesc);
		init = pgd_offset(&init_mm, 0UL);
		pgd_init(ret);
		memcpy(ret + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
		       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}

	return ret;
}
EXPORT_SYMBOL_GPL(pgd_alloc);
