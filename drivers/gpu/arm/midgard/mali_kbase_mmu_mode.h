/*
 *
 * (C) COPYRIGHT 2014-2015, 2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





#ifndef _MALI_KBASE_MMU_MODE_
#define _MALI_KBASE_MMU_MODE_

#include <linux/types.h>

/* Forward declarations */
struct kbase_context;
struct kbase_device;
struct kbase_as;
struct kbase_mmu_setup;

struct kbase_mmu_mode {
	void (*update)(struct kbase_context *kctx);
	void (*get_as_setup)(struct kbase_context *kctx,
			struct kbase_mmu_setup * const setup);
	void (*disable_as)(struct kbase_device *kbdev, int as_nr);
	phys_addr_t (*pte_to_phy_addr)(u64 entry);
	int (*ate_is_valid)(u64 ate);
	int (*pte_is_valid)(u64 pte);
	void (*entry_set_ate)(u64 *entry, phys_addr_t phy, unsigned long flags);
	void (*entry_set_pte)(u64 *entry, phys_addr_t phy);
	void (*entry_invalidate)(u64 *entry);
};

struct kbase_mmu_mode const *kbase_mmu_mode_get_lpae(void);
struct kbase_mmu_mode const *kbase_mmu_mode_get_aarch64(void);

#endif /* _MALI_KBASE_MMU_MODE_ */
