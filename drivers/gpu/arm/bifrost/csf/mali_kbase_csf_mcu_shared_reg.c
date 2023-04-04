// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2022-2023 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

#include <linux/protected_memory_allocator.h>
#include <mali_kbase.h>
#include "mali_kbase_csf.h"
#include "mali_kbase_csf_mcu_shared_reg.h"
#include <mali_kbase_mem_migrate.h>

/* Scaling factor in pre-allocating shared regions for suspend bufs and userios */
#define MCU_SHARED_REGS_PREALLOCATE_SCALE (8)

/* MCU shared region map attempt limit */
#define MCU_SHARED_REGS_BIND_ATTEMPT_LIMIT (4)

/* Convert a VPFN to its start addr */
#define GET_VPFN_VA(vpfn) ((vpfn) << PAGE_SHIFT)

/* Macros for extract the corresponding VPFNs from a CSG_REG */
#define CSG_REG_SUSP_BUF_VPFN(reg, nr_susp_pages) (reg->start_pfn)
#define CSG_REG_PMOD_BUF_VPFN(reg, nr_susp_pages) (reg->start_pfn + nr_susp_pages)
#define CSG_REG_USERIO_VPFN(reg, csi, nr_susp_pages) (reg->start_pfn + 2 * (nr_susp_pages + csi))

/* MCU shared segment dummy page mapping flags */
#define DUMMY_PAGE_MAP_FLAGS (KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_DEFAULT) | KBASE_REG_GPU_NX)

/* MCU shared segment suspend buffer mapping flags */
#define SUSP_PAGE_MAP_FLAGS                                                                        \
	(KBASE_REG_GPU_RD | KBASE_REG_GPU_WR | KBASE_REG_GPU_NX |                                  \
	 KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_DEFAULT))

/**
 * struct kbase_csg_shared_region - Wrapper object for use with a CSG on runtime
 *                                  resources for suspend buffer pages, userio pages
 *                                  and their corresponding mapping GPU VA addresses
 *                                  from the MCU shared interface segment
 *
 * @link:       Link to the managing list for the wrapper object.
 * @reg:        pointer to the region allocated from the shared interface segment, which
 *              covers the normal/P-mode suspend buffers, userio pages of the queues
 * @grp:        Pointer to the bound kbase_queue_group, or NULL if no binding (free).
 * @pmode_mapped: Boolean for indicating the region has MMU mapped with the bound group's
 *              protected mode suspend buffer pages.
 */
struct kbase_csg_shared_region {
	struct list_head link;
	struct kbase_va_region *reg;
	struct kbase_queue_group *grp;
	bool pmode_mapped;
};

static unsigned long get_userio_mmu_flags(struct kbase_device *kbdev)
{
	unsigned long userio_map_flags;

	if (kbdev->system_coherency == COHERENCY_NONE)
		userio_map_flags =
			KBASE_REG_GPU_RD | KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_NON_CACHEABLE);
	else
		userio_map_flags = KBASE_REG_GPU_RD | KBASE_REG_SHARE_BOTH |
				   KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_SHARED);

	return (userio_map_flags | KBASE_REG_GPU_NX);
}

static void set_page_meta_status_not_movable(struct tagged_addr phy)
{
	if (kbase_page_migration_enabled) {
		struct kbase_page_metadata *page_md = kbase_page_private(as_page(phy));

		if (page_md) {
			spin_lock(&page_md->migrate_lock);
			page_md->status = PAGE_STATUS_SET(page_md->status, (u8)NOT_MOVABLE);
			spin_unlock(&page_md->migrate_lock);
		}
	}
}

static struct kbase_csg_shared_region *get_group_bound_csg_reg(struct kbase_queue_group *group)
{
	return (struct kbase_csg_shared_region *)group->csg_reg;
}

static inline int update_mapping_with_dummy_pages(struct kbase_device *kbdev, u64 vpfn,
						  u32 nr_pages)
{
	struct kbase_csf_mcu_shared_regions *shared_regs = &kbdev->csf.scheduler.mcu_regs_data;
	const unsigned long mem_flags = DUMMY_PAGE_MAP_FLAGS;

	return kbase_mmu_update_csf_mcu_pages(kbdev, vpfn, shared_regs->dummy_phys, nr_pages,
					      mem_flags, KBASE_MEM_GROUP_CSF_FW);
}

static inline int insert_dummy_pages(struct kbase_device *kbdev, u64 vpfn, u32 nr_pages)
{
	struct kbase_csf_mcu_shared_regions *shared_regs = &kbdev->csf.scheduler.mcu_regs_data;
	const unsigned long mem_flags = DUMMY_PAGE_MAP_FLAGS;
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	return kbase_mmu_insert_pages(kbdev, &kbdev->csf.mcu_mmu, vpfn, shared_regs->dummy_phys,
				      nr_pages, mem_flags, MCU_AS_NR, KBASE_MEM_GROUP_CSF_FW,
				      mmu_sync_info, NULL, false);
}

/* Reset consecutive retry count to zero */
static void notify_group_csg_reg_map_done(struct kbase_queue_group *group)
{
	lockdep_assert_held(&group->kctx->kbdev->csf.scheduler.lock);

	/* Just clear the internal map retry count */
	group->csg_reg_bind_retries = 0;
}

/* Return true if a fatal group error has already been triggered */
static bool notify_group_csg_reg_map_error(struct kbase_queue_group *group)
{
	struct kbase_device *kbdev = group->kctx->kbdev;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (group->csg_reg_bind_retries < U8_MAX)
		group->csg_reg_bind_retries++;

	/* Allow only one fatal error notification */
	if (group->csg_reg_bind_retries == MCU_SHARED_REGS_BIND_ATTEMPT_LIMIT) {
		struct base_gpu_queue_group_error const err_payload = {
			.error_type = BASE_GPU_QUEUE_GROUP_ERROR_FATAL,
			.payload = { .fatal_group = { .status = GPU_EXCEPTION_TYPE_SW_FAULT_0 } }
		};

		dev_err(kbdev->dev, "Fatal: group_%d_%d_%d exceeded shared region map retry limit",
			group->kctx->tgid, group->kctx->id, group->handle);
		kbase_csf_add_group_fatal_error(group, &err_payload);
		kbase_event_wakeup(group->kctx);
	}

	return group->csg_reg_bind_retries >= MCU_SHARED_REGS_BIND_ATTEMPT_LIMIT;
}

/* Replace the given phys at vpfn (reflecting a queue's userio_pages) mapping.
 * If phys is NULL, the internal dummy_phys is used, which effectively
 * restores back to the initialized state for the given queue's userio_pages
 * (i.e. mapped to the default dummy page).
 * In case of CSF mmu update error on a queue, the dummy phy is used to restore
 * back the default 'unbound' (i.e. mapped to dummy) condition.
 *
 * It's the caller's responsibility to ensure that the given vpfn is extracted
 * correctly from a CSG_REG object, for example, using CSG_REG_USERIO_VPFN().
 */
static int userio_pages_replace_phys(struct kbase_device *kbdev, u64 vpfn, struct tagged_addr *phys)
{
	struct kbase_csf_mcu_shared_regions *shared_regs = &kbdev->csf.scheduler.mcu_regs_data;
	int err = 0, err1;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (phys) {
		unsigned long mem_flags_input = shared_regs->userio_mem_rd_flags;
		unsigned long mem_flags_output = mem_flags_input | KBASE_REG_GPU_WR;

		/* Dealing with a queue's INPUT page */
		err = kbase_mmu_update_csf_mcu_pages(kbdev, vpfn, &phys[0], 1, mem_flags_input,
						     KBASE_MEM_GROUP_CSF_IO);
		/* Dealing with a queue's OUTPUT page */
		err1 = kbase_mmu_update_csf_mcu_pages(kbdev, vpfn + 1, &phys[1], 1,
						      mem_flags_output, KBASE_MEM_GROUP_CSF_IO);
		if (unlikely(err1))
			err = err1;
	}

	if (unlikely(err) || !phys) {
		/* Restore back to dummy_userio_phy */
		update_mapping_with_dummy_pages(kbdev, vpfn, KBASEP_NUM_CS_USER_IO_PAGES);
	}

	return err;
}

/* Update a group's queues' mappings for a group with its runtime bound group region */
static int csg_reg_update_on_csis(struct kbase_device *kbdev, struct kbase_queue_group *group,
				  struct kbase_queue_group *prev_grp)
{
	struct kbase_csg_shared_region *csg_reg = get_group_bound_csg_reg(group);
	const u32 nr_susp_pages = PFN_UP(kbdev->csf.global_iface.groups[0].suspend_size);
	const u32 nr_csis = kbdev->csf.global_iface.groups[0].stream_num;
	struct tagged_addr *phy;
	int err = 0, err1;
	u32 i;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (WARN_ONCE(!csg_reg, "Update_userio pages: group has no bound csg_reg"))
		return -EINVAL;

	for (i = 0; i < nr_csis; i++) {
		struct kbase_queue *queue = group->bound_queues[i];
		struct kbase_queue *prev_queue = prev_grp ? prev_grp->bound_queues[i] : NULL;

		/* Set the phy if the group's queue[i] needs mapping, otherwise NULL */
		phy = (queue && queue->enabled && !queue->user_io_gpu_va) ? queue->phys : NULL;

		/* Either phy is valid, or this update is for a transition change from
		 * prev_group, and the prev_queue was mapped, so an update is required.
		 */
		if (phy || (prev_queue && prev_queue->user_io_gpu_va)) {
			u64 vpfn = CSG_REG_USERIO_VPFN(csg_reg->reg, i, nr_susp_pages);

			err1 = userio_pages_replace_phys(kbdev, vpfn, phy);

			if (unlikely(err1)) {
				dev_warn(kbdev->dev,
					 "%s: Error in update queue-%d mapping for csg_%d_%d_%d",
					 __func__, i, group->kctx->tgid, group->kctx->id,
					 group->handle);
				err = err1;
			} else if (phy)
				queue->user_io_gpu_va = GET_VPFN_VA(vpfn);

			/* Mark prev_group's queue has lost its mapping */
			if (prev_queue)
				prev_queue->user_io_gpu_va = 0;
		}
	}

	return err;
}

/* Bind a group to a given csg_reg, any previous mappings with the csg_reg are replaced
 * with the given group's phy pages, or, if no replacement, the default dummy pages.
 * Note, the csg_reg's fields are in transition step-by-step from the prev_grp to its
 * new binding owner in this function. At the end, the prev_grp would be completely
 * detached away from the previously bound csg_reg.
 */
static int group_bind_csg_reg(struct kbase_device *kbdev, struct kbase_queue_group *group,
			      struct kbase_csg_shared_region *csg_reg)
{
	const unsigned long mem_flags = SUSP_PAGE_MAP_FLAGS;
	const u32 nr_susp_pages = PFN_UP(kbdev->csf.global_iface.groups[0].suspend_size);
	struct kbase_queue_group *prev_grp = csg_reg->grp;
	struct kbase_va_region *reg = csg_reg->reg;
	struct tagged_addr *phy;
	int err = 0, err1;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	/* The csg_reg is expected still on the unused list so its link is not empty */
	if (WARN_ON_ONCE(list_empty(&csg_reg->link))) {
		dev_dbg(kbdev->dev, "csg_reg is marked in active use");
		return -EINVAL;
	}

	if (WARN_ON_ONCE(prev_grp && prev_grp->csg_reg != csg_reg)) {
		dev_dbg(kbdev->dev, "Unexpected bound lost on prev_group");
		prev_grp->csg_reg = NULL;
		return -EINVAL;
	}

	/* Replacing the csg_reg bound group to the newly given one */
	csg_reg->grp = group;
	group->csg_reg = csg_reg;

	/* Resolving mappings, deal with protected mode first */
	if (group->protected_suspend_buf.pma) {
		/* We are binding a new group with P-mode ready, the prev_grp's P-mode mapping
		 * status is now stale during this transition of ownership. For the new owner,
		 * its mapping would have been updated away when it lost its binding previously.
		 * So it needs an update to this pma map. By clearing here the mapped flag
		 * ensures it reflects the new owner's condition.
		 */
		csg_reg->pmode_mapped = false;
		err = kbase_csf_mcu_shared_group_update_pmode_map(kbdev, group);
	} else if (csg_reg->pmode_mapped) {
		/* Need to unmap the previous one, use the dummy pages */
		err = update_mapping_with_dummy_pages(
			kbdev, CSG_REG_PMOD_BUF_VPFN(reg, nr_susp_pages), nr_susp_pages);

		if (unlikely(err))
			dev_warn(kbdev->dev, "%s: Failed to update P-mode dummy for csg_%d_%d_%d",
				 __func__, group->kctx->tgid, group->kctx->id, group->handle);

		csg_reg->pmode_mapped = false;
	}

	/* Unlike the normal suspend buf, the mapping of the protected mode suspend buffer is
	 * actually reflected by a specific mapped flag (due to phys[] is only allocated on
	 * in-need basis). So the GPU_VA is always updated to the bound region's corresponding
	 * VA, as a reflection of the binding to the csg_reg.
	 */
	group->protected_suspend_buf.gpu_va =
		GET_VPFN_VA(CSG_REG_PMOD_BUF_VPFN(reg, nr_susp_pages));

	/* Deal with normal mode suspend buffer */
	phy = group->normal_suspend_buf.phy;
	err1 = kbase_mmu_update_csf_mcu_pages(kbdev, CSG_REG_SUSP_BUF_VPFN(reg, nr_susp_pages), phy,
					      nr_susp_pages, mem_flags, KBASE_MEM_GROUP_CSF_FW);

	if (unlikely(err1)) {
		dev_warn(kbdev->dev, "%s: Failed to update suspend buffer for csg_%d_%d_%d",
			 __func__, group->kctx->tgid, group->kctx->id, group->handle);

		/* Attempt a restore to default dummy for removing previous mapping */
		if (prev_grp)
			update_mapping_with_dummy_pages(
				kbdev, CSG_REG_SUSP_BUF_VPFN(reg, nr_susp_pages), nr_susp_pages);
		err = err1;
		/* Marking the normal suspend buffer is not mapped (due to error) */
		group->normal_suspend_buf.gpu_va = 0;
	} else {
		/* Marking the normal suspend buffer is actually mapped */
		group->normal_suspend_buf.gpu_va =
			GET_VPFN_VA(CSG_REG_SUSP_BUF_VPFN(reg, nr_susp_pages));
	}

	/* Deal with queue uerio_pages */
	err1 = csg_reg_update_on_csis(kbdev, group, prev_grp);
	if (likely(!err1))
		err = err1;

	/* Reset the previous group's suspend buffers' GPU_VAs as it has lost its bound */
	if (prev_grp) {
		prev_grp->normal_suspend_buf.gpu_va = 0;
		prev_grp->protected_suspend_buf.gpu_va = 0;
		prev_grp->csg_reg = NULL;
	}

	return err;
}

/* Notify the group is placed on-slot, hence the bound csg_reg is active in use */
void kbase_csf_mcu_shared_set_group_csg_reg_active(struct kbase_device *kbdev,
						   struct kbase_queue_group *group)
{
	struct kbase_csg_shared_region *csg_reg = get_group_bound_csg_reg(group);

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (WARN_ONCE(!csg_reg || csg_reg->grp != group, "Group_%d_%d_%d has no csg_reg bounding",
		      group->kctx->tgid, group->kctx->id, group->handle))
		return;

	/* By dropping out the csg_reg from the unused list, it becomes active and is tracked
	 * by its bound group that is on-slot. The design is that, when this on-slot group is
	 * moved to off-slot, the scheduler slot-clean up will add it back to the tail of the
	 * unused list.
	 */
	if (!WARN_ON_ONCE(list_empty(&csg_reg->link)))
		list_del_init(&csg_reg->link);
}

/* Notify the group is placed off-slot, hence the bound csg_reg is not in active use
 * anymore. Existing bounding/mappings are left untouched. These would only be dealt with
 * if the bound csg_reg is to be reused with another group.
 */
void kbase_csf_mcu_shared_set_group_csg_reg_unused(struct kbase_device *kbdev,
						   struct kbase_queue_group *group)
{
	struct kbase_csg_shared_region *csg_reg = get_group_bound_csg_reg(group);
	struct kbase_csf_mcu_shared_regions *shared_regs = &kbdev->csf.scheduler.mcu_regs_data;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (WARN_ONCE(!csg_reg || csg_reg->grp != group, "Group_%d_%d_%d has no csg_reg bound",
		      group->kctx->tgid, group->kctx->id, group->handle))
		return;

	/* By adding back the csg_reg to the unused list, it becomes available for another
	 * group to break its existing binding and set up a new one.
	 */
	if (!list_empty(&csg_reg->link)) {
		WARN_ONCE(group->csg_nr >= 0, "Group is assumed vacated from slot");
		list_move_tail(&csg_reg->link, &shared_regs->unused_csg_regs);
	} else
		list_add_tail(&csg_reg->link, &shared_regs->unused_csg_regs);
}

/* Adding a new queue to an existing on-slot group */
int kbase_csf_mcu_shared_add_queue(struct kbase_device *kbdev, struct kbase_queue *queue)
{
	struct kbase_queue_group *group = queue->group;
	struct kbase_csg_shared_region *csg_reg;
	const u32 nr_susp_pages = PFN_UP(kbdev->csf.global_iface.groups[0].suspend_size);
	u64 vpfn;
	int err;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (WARN_ONCE(!group || group->csg_nr < 0, "No bound group, or group is not on-slot"))
		return -EIO;

	csg_reg = get_group_bound_csg_reg(group);
	if (WARN_ONCE(!csg_reg || !list_empty(&csg_reg->link),
		      "No bound csg_reg, or in wrong state"))
		return -EIO;

	vpfn = CSG_REG_USERIO_VPFN(csg_reg->reg, queue->csi_index, nr_susp_pages);
	err = userio_pages_replace_phys(kbdev, vpfn, queue->phys);
	if (likely(!err)) {
		/* Mark the queue has been successfully mapped */
		queue->user_io_gpu_va = GET_VPFN_VA(vpfn);
	} else {
		/* Mark the queue has no mapping on its phys[] */
		queue->user_io_gpu_va = 0;
		dev_dbg(kbdev->dev,
			"%s: Error in mapping userio pages for queue-%d of csg_%d_%d_%d", __func__,
			queue->csi_index, group->kctx->tgid, group->kctx->id, group->handle);

		/* notify the error for the bound group */
		if (notify_group_csg_reg_map_error(group))
			err = -EIO;
	}

	return err;
}

/* Unmap a given queue's userio pages, when the queue is deleted */
void kbase_csf_mcu_shared_drop_stopped_queue(struct kbase_device *kbdev, struct kbase_queue *queue)
{
	struct kbase_queue_group *group;
	struct kbase_csg_shared_region *csg_reg;
	const u32 nr_susp_pages = PFN_UP(kbdev->csf.global_iface.groups[0].suspend_size);
	u64 vpfn;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	/* The queue has no existing mapping, nothing to do */
	if (!queue || !queue->user_io_gpu_va)
		return;

	group = queue->group;
	if (WARN_ONCE(!group || !group->csg_reg, "Queue/Group has no bound region"))
		return;

	csg_reg = get_group_bound_csg_reg(group);

	vpfn = CSG_REG_USERIO_VPFN(csg_reg->reg, queue->csi_index, nr_susp_pages);

	WARN_ONCE(userio_pages_replace_phys(kbdev, vpfn, NULL),
		  "Unexpected restoring to dummy map update error");
	queue->user_io_gpu_va = 0;
}

int kbase_csf_mcu_shared_group_update_pmode_map(struct kbase_device *kbdev,
						struct kbase_queue_group *group)
{
	struct kbase_csf_mcu_shared_regions *shared_regs = &kbdev->csf.scheduler.mcu_regs_data;
	struct kbase_csg_shared_region *csg_reg = get_group_bound_csg_reg(group);
	const u32 nr_susp_pages = PFN_UP(kbdev->csf.global_iface.groups[0].suspend_size);
	int err = 0, err1;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	if (WARN_ONCE(!csg_reg, "Update_pmode_map: the bound csg_reg can't be NULL"))
		return -EINVAL;

	/* If the pmode already mapped, nothing to do */
	if (csg_reg->pmode_mapped)
		return 0;

	/* P-mode map not in place and the group has allocated P-mode pages, map it */
	if (group->protected_suspend_buf.pma) {
		unsigned long mem_flags = SUSP_PAGE_MAP_FLAGS;
		struct tagged_addr *phy = shared_regs->pma_phys;
		struct kbase_va_region *reg = csg_reg->reg;
		u64 vpfn = CSG_REG_PMOD_BUF_VPFN(reg, nr_susp_pages);
		u32 i;

		/* Populate the protected phys from pma to phy[] */
		for (i = 0; i < nr_susp_pages; i++)
			phy[i] = as_tagged(group->protected_suspend_buf.pma[i]->pa);

		/* Add the P-mode suspend buffer mapping */
		err = kbase_mmu_update_csf_mcu_pages(kbdev, vpfn, phy, nr_susp_pages, mem_flags,
						     KBASE_MEM_GROUP_CSF_FW);

		/* If error, restore to default dummpy */
		if (unlikely(err)) {
			err1 = update_mapping_with_dummy_pages(kbdev, vpfn, nr_susp_pages);
			if (unlikely(err1))
				dev_warn(
					kbdev->dev,
					"%s: Failed in recovering to P-mode dummy for csg_%d_%d_%d",
					__func__, group->kctx->tgid, group->kctx->id,
					group->handle);

			csg_reg->pmode_mapped = false;
		} else
			csg_reg->pmode_mapped = true;
	}

	return err;
}

void kbase_csf_mcu_shared_clear_evicted_group_csg_reg(struct kbase_device *kbdev,
						      struct kbase_queue_group *group)
{
	struct kbase_csf_mcu_shared_regions *shared_regs = &kbdev->csf.scheduler.mcu_regs_data;
	struct kbase_csg_shared_region *csg_reg = get_group_bound_csg_reg(group);
	struct kbase_va_region *reg;
	const u32 nr_susp_pages = PFN_UP(kbdev->csf.global_iface.groups[0].suspend_size);
	u32 nr_csis = kbdev->csf.global_iface.groups[0].stream_num;
	int err = 0;
	u32 i;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	/* Nothing to do for clearing up if no bound csg_reg */
	if (!csg_reg)
		return;

	reg = csg_reg->reg;
	/* Restore mappings default dummy pages for any mapped pages */
	if (csg_reg->pmode_mapped) {
		err = update_mapping_with_dummy_pages(
			kbdev, CSG_REG_PMOD_BUF_VPFN(reg, nr_susp_pages), nr_susp_pages);
		WARN_ONCE(unlikely(err), "Restore dummy failed for clearing pmod buffer mapping");

		csg_reg->pmode_mapped = false;
	}

	if (group->normal_suspend_buf.gpu_va) {
		err = update_mapping_with_dummy_pages(
			kbdev, CSG_REG_SUSP_BUF_VPFN(reg, nr_susp_pages), nr_susp_pages);
		WARN_ONCE(err, "Restore dummy failed for clearing suspend buffer mapping");
	}

	/* Deal with queue uerio pages */
	for (i = 0; i < nr_csis; i++)
		kbase_csf_mcu_shared_drop_stopped_queue(kbdev, group->bound_queues[i]);

	group->normal_suspend_buf.gpu_va = 0;
	group->protected_suspend_buf.gpu_va = 0;

	/* Break the binding */
	group->csg_reg = NULL;
	csg_reg->grp = NULL;

	/* Put the csg_reg to the front of the unused list */
	if (WARN_ON_ONCE(list_empty(&csg_reg->link)))
		list_add(&csg_reg->link, &shared_regs->unused_csg_regs);
	else
		list_move(&csg_reg->link, &shared_regs->unused_csg_regs);
}

int kbase_csf_mcu_shared_group_bind_csg_reg(struct kbase_device *kbdev,
					    struct kbase_queue_group *group)
{
	struct kbase_csf_mcu_shared_regions *shared_regs = &kbdev->csf.scheduler.mcu_regs_data;
	struct kbase_csg_shared_region *csg_reg;
	int err;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	csg_reg = get_group_bound_csg_reg(group);
	if (!csg_reg)
		csg_reg = list_first_entry_or_null(&shared_regs->unused_csg_regs,
						   struct kbase_csg_shared_region, link);

	if (!WARN_ON_ONCE(!csg_reg)) {
		struct kbase_queue_group *prev_grp = csg_reg->grp;

		/* Deal with the previous binding and lazy unmap, i.e if the previous mapping not
		 * the required one, unmap it.
		 */
		if (prev_grp == group) {
			/* Update existing bindings, if there have been some changes */
			err = kbase_csf_mcu_shared_group_update_pmode_map(kbdev, group);
			if (likely(!err))
				err = csg_reg_update_on_csis(kbdev, group, NULL);
		} else
			err = group_bind_csg_reg(kbdev, group, csg_reg);
	} else {
		/* This should not have been possible if the code operates rightly */
		dev_err(kbdev->dev, "%s: Unexpected NULL csg_reg for group %d of context %d_%d",
			__func__, group->handle, group->kctx->tgid, group->kctx->id);
		return -EIO;
	}

	if (likely(!err))
		notify_group_csg_reg_map_done(group);
	else
		notify_group_csg_reg_map_error(group);

	return err;
}

static int shared_mcu_csg_reg_init(struct kbase_device *kbdev,
				   struct kbase_csg_shared_region *csg_reg)
{
	struct kbase_csf_mcu_shared_regions *shared_regs = &kbdev->csf.scheduler.mcu_regs_data;
	const u32 nr_susp_pages = PFN_UP(kbdev->csf.global_iface.groups[0].suspend_size);
	u32 nr_csis = kbdev->csf.global_iface.groups[0].stream_num;
	const size_t nr_csg_reg_pages = 2 * (nr_susp_pages + nr_csis);
	struct kbase_va_region *reg;
	u64 vpfn;
	int err, i;

	INIT_LIST_HEAD(&csg_reg->link);
	reg = kbase_alloc_free_region(kbdev, &kbdev->csf.shared_reg_rbtree, 0, nr_csg_reg_pages,
				      KBASE_REG_ZONE_MCU_SHARED);

	if (!reg) {
		dev_err(kbdev->dev, "%s: Failed to allocate a MCU shared region for %zu pages\n",
			__func__, nr_csg_reg_pages);
		return -ENOMEM;
	}

	/* Insert the region into rbtree, so it becomes ready to use */
	mutex_lock(&kbdev->csf.reg_lock);
	err = kbase_add_va_region_rbtree(kbdev, reg, 0, nr_csg_reg_pages, 1);
	reg->flags &= ~KBASE_REG_FREE;
	mutex_unlock(&kbdev->csf.reg_lock);
	if (err) {
		kfree(reg);
		dev_err(kbdev->dev, "%s: Failed to add a region of %zu pages into rbtree", __func__,
			nr_csg_reg_pages);
		return err;
	}

	/* Initialize the mappings so MMU only need to update the the corresponding
	 * mapped phy-pages at runtime.
	 * Map the normal suspend buffer pages to the prepared dummy phys[].
	 */
	vpfn = CSG_REG_SUSP_BUF_VPFN(reg, nr_susp_pages);
	err = insert_dummy_pages(kbdev, vpfn, nr_susp_pages);

	if (unlikely(err))
		goto fail_susp_map_fail;

	/* Map the protected suspend buffer pages to the prepared dummy phys[] */
	vpfn = CSG_REG_PMOD_BUF_VPFN(reg, nr_susp_pages);
	err = insert_dummy_pages(kbdev, vpfn, nr_susp_pages);

	if (unlikely(err))
		goto fail_pmod_map_fail;

	for (i = 0; i < nr_csis; i++) {
		vpfn = CSG_REG_USERIO_VPFN(reg, i, nr_susp_pages);
		err = insert_dummy_pages(kbdev, vpfn, KBASEP_NUM_CS_USER_IO_PAGES);

		if (unlikely(err))
			goto fail_userio_pages_map_fail;
	}

	/* Replace the previous NULL-valued field with the successully initialized reg */
	csg_reg->reg = reg;

	return 0;

fail_userio_pages_map_fail:
	while (i-- > 0) {
		vpfn = CSG_REG_USERIO_VPFN(reg, i, nr_susp_pages);
		kbase_mmu_teardown_pages(kbdev, &kbdev->csf.mcu_mmu, vpfn, shared_regs->dummy_phys,
					 KBASEP_NUM_CS_USER_IO_PAGES, KBASEP_NUM_CS_USER_IO_PAGES,
					 MCU_AS_NR, true);
	}

	vpfn = CSG_REG_PMOD_BUF_VPFN(reg, nr_susp_pages);
	kbase_mmu_teardown_pages(kbdev, &kbdev->csf.mcu_mmu, vpfn, shared_regs->dummy_phys,
				 nr_susp_pages, nr_susp_pages, MCU_AS_NR, true);
fail_pmod_map_fail:
	vpfn = CSG_REG_SUSP_BUF_VPFN(reg, nr_susp_pages);
	kbase_mmu_teardown_pages(kbdev, &kbdev->csf.mcu_mmu, vpfn, shared_regs->dummy_phys,
				 nr_susp_pages, nr_susp_pages, MCU_AS_NR, true);
fail_susp_map_fail:
	mutex_lock(&kbdev->csf.reg_lock);
	kbase_remove_va_region(kbdev, reg);
	mutex_unlock(&kbdev->csf.reg_lock);
	kfree(reg);

	return err;
}

/* Note, this helper can only be called on scheduler shutdown */
static void shared_mcu_csg_reg_term(struct kbase_device *kbdev,
				    struct kbase_csg_shared_region *csg_reg)
{
	struct kbase_csf_mcu_shared_regions *shared_regs = &kbdev->csf.scheduler.mcu_regs_data;
	struct kbase_va_region *reg = csg_reg->reg;
	const u32 nr_susp_pages = PFN_UP(kbdev->csf.global_iface.groups[0].suspend_size);
	const u32 nr_csis = kbdev->csf.global_iface.groups[0].stream_num;
	u64 vpfn;
	int i;

	for (i = 0; i < nr_csis; i++) {
		vpfn = CSG_REG_USERIO_VPFN(reg, i, nr_susp_pages);
		kbase_mmu_teardown_pages(kbdev, &kbdev->csf.mcu_mmu, vpfn, shared_regs->dummy_phys,
					 KBASEP_NUM_CS_USER_IO_PAGES, KBASEP_NUM_CS_USER_IO_PAGES,
					 MCU_AS_NR, true);
	}

	vpfn = CSG_REG_PMOD_BUF_VPFN(reg, nr_susp_pages);
	kbase_mmu_teardown_pages(kbdev, &kbdev->csf.mcu_mmu, vpfn, shared_regs->dummy_phys,
				 nr_susp_pages, nr_susp_pages, MCU_AS_NR, true);
	vpfn = CSG_REG_SUSP_BUF_VPFN(reg, nr_susp_pages);
	kbase_mmu_teardown_pages(kbdev, &kbdev->csf.mcu_mmu, vpfn, shared_regs->dummy_phys,
				 nr_susp_pages, nr_susp_pages, MCU_AS_NR, true);

	mutex_lock(&kbdev->csf.reg_lock);
	kbase_remove_va_region(kbdev, reg);
	mutex_unlock(&kbdev->csf.reg_lock);
	kfree(reg);
}

int kbase_csf_mcu_shared_regs_data_init(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	struct kbase_csf_mcu_shared_regions *shared_regs = &scheduler->mcu_regs_data;
	struct kbase_csg_shared_region *array_csg_regs;
	const size_t nr_susp_pages = PFN_UP(kbdev->csf.global_iface.groups[0].suspend_size);
	const u32 nr_groups = kbdev->csf.global_iface.group_num;
	const u32 nr_csg_regs = MCU_SHARED_REGS_PREALLOCATE_SCALE * nr_groups;
	const u32 nr_dummy_phys = MAX(nr_susp_pages, KBASEP_NUM_CS_USER_IO_PAGES);
	u32 i;
	int err;

	shared_regs->userio_mem_rd_flags = get_userio_mmu_flags(kbdev);
	INIT_LIST_HEAD(&shared_regs->unused_csg_regs);

	shared_regs->dummy_phys =
		kcalloc(nr_dummy_phys, sizeof(*shared_regs->dummy_phys), GFP_KERNEL);
	if (!shared_regs->dummy_phys)
		return -ENOMEM;

	if (kbase_mem_pool_alloc_pages(&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW], 1,
				       &shared_regs->dummy_phys[0], false, NULL) <= 0)
		return -ENOMEM;

	shared_regs->dummy_phys_allocated = true;
	set_page_meta_status_not_movable(shared_regs->dummy_phys[0]);

	/* Replicate the allocated single shared_regs->dummy_phys[0] to the full array */
	for (i = 1; i < nr_dummy_phys; i++)
		shared_regs->dummy_phys[i] = shared_regs->dummy_phys[0];

	shared_regs->pma_phys = kcalloc(nr_susp_pages, sizeof(*shared_regs->pma_phys), GFP_KERNEL);
	if (!shared_regs->pma_phys)
		return -ENOMEM;

	array_csg_regs = kcalloc(nr_csg_regs, sizeof(*array_csg_regs), GFP_KERNEL);
	if (!array_csg_regs)
		return -ENOMEM;
	shared_regs->array_csg_regs = array_csg_regs;

	/* All fields in scheduler->mcu_regs_data except the shared_regs->array_csg_regs
	 * are properly populated and ready to use. Now initialize the items in
	 * shared_regs->array_csg_regs[]
	 */
	for (i = 0; i < nr_csg_regs; i++) {
		err = shared_mcu_csg_reg_init(kbdev, &array_csg_regs[i]);
		if (err)
			return err;

		list_add_tail(&array_csg_regs[i].link, &shared_regs->unused_csg_regs);
	}

	return 0;
}

void kbase_csf_mcu_shared_regs_data_term(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	struct kbase_csf_mcu_shared_regions *shared_regs = &scheduler->mcu_regs_data;
	struct kbase_csg_shared_region *array_csg_regs =
		(struct kbase_csg_shared_region *)shared_regs->array_csg_regs;
	const u32 nr_groups = kbdev->csf.global_iface.group_num;
	const u32 nr_csg_regs = MCU_SHARED_REGS_PREALLOCATE_SCALE * nr_groups;

	if (array_csg_regs) {
		struct kbase_csg_shared_region *csg_reg;
		u32 i, cnt_csg_regs = 0;

		for (i = 0; i < nr_csg_regs; i++) {
			csg_reg = &array_csg_regs[i];
			/* There should not be any group mapping bindings */
			WARN_ONCE(csg_reg->grp, "csg_reg has a bound group");

			if (csg_reg->reg) {
				shared_mcu_csg_reg_term(kbdev, csg_reg);
				cnt_csg_regs++;
			}
		}

		/* The nr_susp_regs counts should match the array_csg_regs' length */
		list_for_each_entry(csg_reg, &shared_regs->unused_csg_regs, link)
			cnt_csg_regs--;

		WARN_ONCE(cnt_csg_regs, "Unmatched counts of susp_regs");
		kfree(shared_regs->array_csg_regs);
	}

	if (shared_regs->dummy_phys_allocated) {
		struct page *page = as_page(shared_regs->dummy_phys[0]);

		kbase_mem_pool_free(&kbdev->mem_pools.small[KBASE_MEM_GROUP_CSF_FW], page, false);
	}

	kfree(shared_regs->dummy_phys);
	kfree(shared_regs->pma_phys);
}
