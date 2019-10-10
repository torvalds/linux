/*
 *
 * (C) COPYRIGHT 2010-2014, 2016-2019 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */


#include "mali_kbase.h"
#include "mali_midg_regmap.h"
#include "mali_kbase_defs.h"

#define ENTRY_TYPE_MASK     3ULL
/* For valid ATEs bit 1 = ((level == 3) ? 1 : 0).
 * Valid ATE entries at level 3 are flagged with the value 3.
 * Valid ATE entries at level 0-2 are flagged with the value 1.
 */
#define ENTRY_IS_ATE_L3		3ULL
#define ENTRY_IS_ATE_L02	1ULL
#define ENTRY_IS_INVAL		2ULL
#define ENTRY_IS_PTE		3ULL

#define ENTRY_ATTR_BITS (7ULL << 2)	/* bits 4:2 */
#define ENTRY_ACCESS_RW (1ULL << 6)     /* bits 6:7 */
#define ENTRY_ACCESS_RO (3ULL << 6)
#define ENTRY_SHARE_BITS (3ULL << 8)	/* bits 9:8 */
#define ENTRY_ACCESS_BIT (1ULL << 10)
#define ENTRY_NX_BIT (1ULL << 54)

/* Helper Function to perform assignment of page table entries, to
 * ensure the use of strd, which is required on LPAE systems.
 */
static inline void page_table_entry_set(u64 *pte, u64 phy)
{
#if KERNEL_VERSION(3, 18, 13) <= LINUX_VERSION_CODE
	WRITE_ONCE(*pte, phy);
#else
#ifdef CONFIG_64BIT
	barrier();
	*pte = phy;
	barrier();
#elif defined(CONFIG_ARM)
	barrier();
	asm volatile("ldrd r0, [%1]\n\t"
		     "strd r0, %0\n\t"
		     : "=m" (*pte)
		     : "r" (&phy)
		     : "r0", "r1");
	barrier();
#else
#error "64-bit atomic write must be implemented for your architecture"
#endif
#endif
}

static void mmu_get_as_setup(struct kbase_mmu_table *mmut,
		struct kbase_mmu_setup * const setup)
{
	/* Set up the required caching policies at the correct indices
	 * in the memattr register.
	 */
	setup->memattr =
		(AS_MEMATTR_IMPL_DEF_CACHE_POLICY <<
			(AS_MEMATTR_INDEX_IMPL_DEF_CACHE_POLICY * 8)) |
		(AS_MEMATTR_FORCE_TO_CACHE_ALL    <<
			(AS_MEMATTR_INDEX_FORCE_TO_CACHE_ALL * 8)) |
		(AS_MEMATTR_WRITE_ALLOC           <<
			(AS_MEMATTR_INDEX_WRITE_ALLOC * 8)) |
		(AS_MEMATTR_AARCH64_OUTER_IMPL_DEF   <<
			(AS_MEMATTR_INDEX_OUTER_IMPL_DEF * 8)) |
		(AS_MEMATTR_AARCH64_OUTER_WA         <<
			(AS_MEMATTR_INDEX_OUTER_WA * 8)) |
		(AS_MEMATTR_AARCH64_NON_CACHEABLE    <<
			(AS_MEMATTR_INDEX_NON_CACHEABLE * 8));

	setup->transtab = (u64)mmut->pgd & AS_TRANSTAB_BASE_MASK;
	setup->transcfg = AS_TRANSCFG_ADRMODE_AARCH64_4K;
}

static void mmu_update(struct kbase_device *kbdev, struct kbase_mmu_table *mmut,
		int as_nr)
{
	struct kbase_as *as;
	struct kbase_mmu_setup *current_setup;

	if (WARN_ON(as_nr == KBASEP_AS_NR_INVALID))
		return;

	as = &kbdev->as[as_nr];
	current_setup = &as->current_setup;

	mmu_get_as_setup(mmut, current_setup);

	/* Apply the address space setting */
	kbase_mmu_hw_configure(kbdev, as);
}

static void mmu_disable_as(struct kbase_device *kbdev, int as_nr)
{
	struct kbase_as * const as = &kbdev->as[as_nr];
	struct kbase_mmu_setup * const current_setup = &as->current_setup;

	current_setup->transtab = 0ULL;
	current_setup->transcfg = AS_TRANSCFG_ADRMODE_UNMAPPED;

	/* Apply the address space setting */
	kbase_mmu_hw_configure(kbdev, as);
}

static phys_addr_t pte_to_phy_addr(u64 entry)
{
	if (!(entry & 1))
		return 0;

	return entry & ~0xFFF;
}

static int ate_is_valid(u64 ate, int const level)
{
	if (level == MIDGARD_MMU_BOTTOMLEVEL)
		return ((ate & ENTRY_TYPE_MASK) == ENTRY_IS_ATE_L3);
	else
		return ((ate & ENTRY_TYPE_MASK) == ENTRY_IS_ATE_L02);
}

static int pte_is_valid(u64 pte, int const level)
{
	/* PTEs cannot exist at the bottom level */
	if (level == MIDGARD_MMU_BOTTOMLEVEL)
		return false;
	return ((pte & ENTRY_TYPE_MASK) == ENTRY_IS_PTE);
}

/*
 * Map KBASE_REG flags to MMU flags
 */
static u64 get_mmu_flags(unsigned long flags)
{
	u64 mmu_flags;

	/* store mem_attr index as 4:2 (macro called ensures 3 bits already) */
	mmu_flags = KBASE_REG_MEMATTR_VALUE(flags) << 2;

	/* Set access flags - note that AArch64 stage 1 does not support
	 * write-only access, so we use read/write instead
	 */
	if (flags & KBASE_REG_GPU_WR)
		mmu_flags |= ENTRY_ACCESS_RW;
	else if (flags & KBASE_REG_GPU_RD)
		mmu_flags |= ENTRY_ACCESS_RO;

	/* nx if requested */
	mmu_flags |= (flags & KBASE_REG_GPU_NX) ? ENTRY_NX_BIT : 0;

	if (flags & KBASE_REG_SHARE_BOTH) {
		/* inner and outer shareable */
		mmu_flags |= SHARE_BOTH_BITS;
	} else if (flags & KBASE_REG_SHARE_IN) {
		/* inner shareable coherency */
		mmu_flags |= SHARE_INNER_BITS;
	}

	return mmu_flags;
}

static void entry_set_ate(u64 *entry,
		struct tagged_addr phy,
		unsigned long flags,
		int const level)
{
	if (level == MIDGARD_MMU_BOTTOMLEVEL)
		page_table_entry_set(entry, as_phys_addr_t(phy) |
				get_mmu_flags(flags) |
				ENTRY_ACCESS_BIT | ENTRY_IS_ATE_L3);
	else
		page_table_entry_set(entry, as_phys_addr_t(phy) |
				get_mmu_flags(flags) |
				ENTRY_ACCESS_BIT | ENTRY_IS_ATE_L02);
}

static void entry_set_pte(u64 *entry, phys_addr_t phy)
{
	page_table_entry_set(entry, (phy & PAGE_MASK) |
			ENTRY_ACCESS_BIT | ENTRY_IS_PTE);
}

static void entry_invalidate(u64 *entry)
{
	page_table_entry_set(entry, ENTRY_IS_INVAL);
}

static struct kbase_mmu_mode const aarch64_mode = {
	.update = mmu_update,
	.get_as_setup = mmu_get_as_setup,
	.disable_as = mmu_disable_as,
	.pte_to_phy_addr = pte_to_phy_addr,
	.ate_is_valid = ate_is_valid,
	.pte_is_valid = pte_is_valid,
	.entry_set_ate = entry_set_ate,
	.entry_set_pte = entry_set_pte,
	.entry_invalidate = entry_invalidate,
	.flags = KBASE_MMU_MODE_HAS_NON_CACHEABLE
};

struct kbase_mmu_mode const *kbase_mmu_mode_get_aarch64(void)
{
	return &aarch64_mode;
}
