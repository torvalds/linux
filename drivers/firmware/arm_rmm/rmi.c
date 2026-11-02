// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023-2026 ARM Ltd.
 */

#include <linux/arm-rmi-cmds.h>
#include <linux/cpufeature.h>
#include <linux/memblock.h>
#include <linux/memory.h>
#include <linux/mmzone.h>
#include <linux/slab.h>

#include <asm/memory.h>
#include <asm/pgtable-hwdef.h>

/* RMM v2.0 defines RmiFeatureRegister0 to RmiFeatureRegister4. */
static unsigned long rmi_feat_reg_cache[5] __ro_after_init;
static bool arm64_rmm_active;
static bool arm64_rmi_is_available;

/* is_rmm_active: Returns if the RMM is ACTIVE */
bool is_rmm_active(void)
{
	return READ_ONCE(arm64_rmm_active);
}
EXPORT_SYMBOL_GPL(is_rmm_active);

/*
 * is_rmi_available: Returns if the RMM is ACTIVE and is configured with static
 * carveout for metadata nad L1GPTs.
 */
bool is_rmi_available(void)
{
	return READ_ONCE(arm64_rmi_is_available);
}
EXPORT_SYMBOL_GPL(is_rmi_available);

/*
 * rmi_granule_range_delegate() - Delegate granules
 * @base: PA of the first granule of the range
 * @top: PA of the first granule after the range
 * @out_top: PA of the first granule not delegated
 *
 * Delegate a range of granule for use by the realm world. If the entire range
 * was delegated then @out_top == @top, otherwise the function should be called
 * again with @base == @out_top.
 *
 * Return: 0 on success, positive RMI result code or negative Linux error code
 */
static long rmi_granule_range_delegate(unsigned long base,
				       unsigned long top,
				       unsigned long *out_top)
{
	struct arm_smccc_1_2_regs regs = {
		SMC_RMI_GRANULE_RANGE_DELEGATE, base, top
	};
	long ret = rmi_sro_execute(&regs);

	if (ret == RMI_SUCCESS && out_top)
		*out_top = regs.a1;

	return ret;
}

/*
 * rmi_delegate_range: Delegate a physically contiguous range.
 * We iterate over the range until we hit an error. So we may
 * return an error, but with a partially delegated range. The
 * caller must always look at the @out_phys to figure out, how
 * much progress was made.
 *
 * @phys:	Base of the physical address range
 * @size:	Size of the physical address range
 * @out_phys:	Top of the range that was completed. This is always
 *		valid, irrespective of the result.
 *
 * Returns RMI_SUCCESS on successful completion. Otherwise, returns
 * the Linux error number or the RMI status code as described
 * by the RMM spec for RMI_GRANULE_DELEGATE_RANGE or RMI_BLOCKED.
 */
int rmi_delegate_range(phys_addr_t phys,
		       unsigned long size,
		       phys_addr_t *out_phys)
{
	long ret = 0;
	unsigned long top = phys + size;
	unsigned long out_top;

	while (phys < top) {
		ret = rmi_granule_range_delegate(phys, top, &out_top);

		if (ret != RMI_SUCCESS)
			break;
		/*
		 * Buggy RMM ? Let the caller handle the failure.
		 * We can't know how far the RMM delegated in this
		 * iteration, so we return the best known good limit.
		 * RMM can deal with granules already in "undelegated"
		 * in a given range. So, it is fine for the caller to
		 * try the range we return.
		 */
		if (WARN_ON(out_top <= phys)) {
			ret = -ENXIO;
			break;
		}
		phys = out_top;
	}

	if (out_phys)
		*out_phys = phys;

	return ret;
}
EXPORT_SYMBOL_GPL(rmi_delegate_range);

/*
 * rmi_granule_range_undelegate() - Undelegate a range of granules
 * @base: Base PA of the target range
 * @top: Top PA of the target range
 * @out_top: Returns the top PA of range whose state is undelegated
 *
 * Undelegate a range of granules to allow use by the normal world. Will fail
 * if the granules are in use by RMM. RMM can ignore granules that are already
 * undelegated and thus is safe to be called on a range with a mix of delegated
 * and undelegated granules.
 *
 * Return: 0 on success, positive RMI result code or negative Linux error code
 */
static long rmi_granule_range_undelegate(unsigned long base,
					 unsigned long top,
					 unsigned long *out_top)
{
	struct arm_smccc_1_2_regs regs = {
		SMC_RMI_GRANULE_RANGE_UNDELEGATE, base, top
	};
	long ret = rmi_sro_execute(&regs);

	if (ret == RMI_SUCCESS && out_top)
		*out_top = regs.a1;

	return ret;
}

int rmi_undelegate_range(phys_addr_t phys,
			 unsigned long size)
{
	long ret = 0;
	unsigned long top = phys + size;
	unsigned long out_top;

	while (phys < top) {
		ret = rmi_granule_range_undelegate(phys, top, &out_top);

		if (ret != RMI_SUCCESS)
			break;
		/* Buggy RMM ? Let the caller leak the pages */
		if (WARN_ON(out_top <= phys))
			return -ENXIO;
		phys = out_top;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(rmi_undelegate_range);

/*
 * rmi_free_delegated_page: Undelegate and free a page that has been previously
 * delegated to the Realm world. If we are unable to undelegate it, the page is
 * leaked.
 * NOTE: Do not use this helper if the page could be concurrently operated by
 * another thread, as it may get leaked if the undelegation fails due to RMI_BLOCKED
 */
int rmi_free_delegated_page(phys_addr_t phys)
{
	if (WARN_ON_ONCE(rmi_undelegate_page(phys))) {
		/* Undelegate failed: leak the page */
		return -EBUSY;
	}

	free_page((unsigned long)phys_to_virt(phys));

	return 0;
}
EXPORT_SYMBOL_GPL(rmi_free_delegated_page);

/*
 * Convert the RmiAddrBlockSize to actual size. This is used in RmiDonateReq
 * and RmiAddrRangeDesc*.
 */
static unsigned long rmi_addr_block_size_to_bytes(unsigned long block_size_fld)
{
	return BIT(ARM64_HW_PGTABLE_LEVEL_SHIFT(3 - block_size_fld));
}

/*
 * free_addr_range: Free memory described by the address range entry, which may
 *		    be partially consumed by RMM.
 *
 * @entry: RMI_ADDR_RANGE descriptor
 * @consumed_size: Page aligned size consumed by the RMM from the address range.
 *
 * If the state of the address is DELEGATED, undelegate it back, before freeing.
 * Leaks the memory if we cannot undelegate the range.
 */
static void free_addr_range(unsigned long entry, unsigned long consumed_size)
{
	unsigned long phys = RMI_ADDR_RANGE_ADDR(entry);
	unsigned long block_size_fld = RMI_ADDR_RANGE_BLOCK_SIZE(entry);
	unsigned long count = RMI_ADDR_RANGE_COUNT(entry);
	unsigned long state = RMI_ADDR_RANGE_STATE(entry);
	unsigned long size = rmi_addr_block_size_to_bytes(block_size_fld) * count;

	WARN_ON(!PAGE_ALIGNED(phys) || !PAGE_ALIGNED(consumed_size));

	/* We shouldn't see this in reclaim path, leak it for now  */
	if (WARN_ON((state != RMI_OP_MEM_DELEGATED) &&
		    (state != RMI_OP_MEM_UNDELEGATED)))
		return;

	/* Adjust the address and size for partially consumed entry */
	phys += consumed_size;
	size -= consumed_size;
	/*
	 * Undelegate the pages back if required. If we can't
	 * change them back, leak the pages.
	 */
	if (state == RMI_OP_MEM_DELEGATED &&
	    WARN_ON(rmi_undelegate_range(phys, size)))
		return;
	free_pages_exact(phys_to_virt(phys), size);
}

static void rmi_op_continue(unsigned long sro_handle, unsigned long flags,
			    struct arm_smccc_1_2_regs *out_regs)
{
	*out_regs = (struct arm_smccc_1_2_regs) {
		SMC_RMI_OP_CONTINUE, sro_handle, flags
	};

	rmi_smccc_invoke(out_regs);
}

static void rmi_op_cancel(unsigned long sro_handle,
			  struct arm_smccc_1_2_regs *out_regs)
{
	*out_regs = (struct arm_smccc_1_2_regs) {
		SMC_RMI_OP_CANCEL, sro_handle
	};

	rmi_smccc_invoke(out_regs);
}

static void rmi_op_mem_donate(unsigned long sro_handle, unsigned long list_addr,
			      unsigned long list_count, unsigned long flags,
			      struct arm_smccc_1_2_regs *out_regs)
{
	*out_regs = (struct arm_smccc_1_2_regs) {
		SMC_RMI_OP_MEM_DONATE, sro_handle, list_addr, list_count, flags
	};

	/*
	 * The output donated count (a1) is always valid, irrespective
	 * of the return result. i.e., 0 if there was an error
	 */
	rmi_smccc_invoke(out_regs);
}

static void rmi_op_mem_reclaim(unsigned long sro_handle,
			       unsigned long list_addr,
			       unsigned long list_count,
			       struct arm_smccc_1_2_regs *out_regs)
{
	*out_regs = (struct arm_smccc_1_2_regs) {
		SMC_RMI_OP_MEM_RECLAIM, sro_handle, list_addr, list_count
	};

	rmi_smccc_invoke(out_regs);
}

static int rmi_sro_ensure_capacity(struct rmi_sro_state *sro,
				   unsigned long count)
{
	if (WARN_ON_ONCE(sro->addr_count > RMI_MAX_ADDR_LIST))
		return -EOVERFLOW;

	if (count > RMI_MAX_ADDR_LIST - sro->addr_count)
		return -ENOSPC;

	return 0;
}

static int rmi_sro_donate_contig(struct rmi_sro_state *sro,
				 unsigned long sro_handle,
				 unsigned long donatereq,
				 struct arm_smccc_1_2_regs *out_regs,
				 gfp_t gfp)
{
	unsigned long block_size_fld = RMI_DONATE_BLOCK_SIZE(donatereq);
	unsigned long block_size = rmi_addr_block_size_to_bytes(block_size_fld);
	unsigned long count = RMI_DONATE_COUNT(donatereq);
	unsigned long state = RMI_DONATE_STATE(donatereq);
	unsigned long size = block_size * count;
	unsigned long addr_range;
	unsigned long donated_size;
	int ret;
	void *virt;
	phys_addr_t phys;

	/*
	 * The RMM specification requires contiguous allocations are always a
	 * power of 2
	 */
	if (WARN_ON_ONCE(!is_power_of_2(size)))
		return -EINVAL;
	/*
	 * RMM clamps the Maximum value of RmiOpMemDonateReq:count to prevent
	 * overflow in the RMI_ADDR_RANGE_COUNT field.
	 */
	if (WARN_ON_ONCE(count > FIELD_MAX(RMI_ADDR_RANGE_COUNT_MASK)))
		return -EINVAL;

	/*
	 * We can't allocate blocks more than what the buddy allocator can
	 * satisfy. TODO: Add support for larger allocations.
	 */
	if (get_order(size) > MAX_PAGE_ORDER)
		return -ENOMEM;

	/* Reuse the cached address range if we have one */
	for (int i = 0; i < sro->addr_count; i++) {
		unsigned long entry = sro->addr_list[i];

		if (RMI_ADDR_RANGE_BLOCK_SIZE(entry) == block_size_fld &&
		    RMI_ADDR_RANGE_COUNT(entry) == count &&
		    RMI_ADDR_RANGE_STATE(entry) == state &&
		    IS_ALIGNED(RMI_ADDR_RANGE_ADDR(entry), size)) {
			sro->addr_count--;
			swap(sro->addr_list[sro->addr_count],
			     sro->addr_list[i]);

			goto mem_donate;
		}
	}

	ret = rmi_sro_ensure_capacity(sro, 1);
	/* In the odd case we have filled up the cache, free up the entire list */
	if (ret)
		rmi_sro_free(sro);

	virt = alloc_pages_exact(size, gfp);
	if (!virt)
		return -ENOMEM;
	phys = virt_to_phys(virt);

	if (state == RMI_OP_MEM_DELEGATED) {
		phys_addr_t delegated_phys;

		if (rmi_delegate_range(phys, size, &delegated_phys)) {
			if (!rmi_undelegate_range(phys, delegated_phys - phys))
				free_pages_exact(virt, size);
			return -ENXIO;
		}
	}

	addr_range = phys & RMI_ADDR_RANGE_ADDR_MASK;
	FIELD_MODIFY(RMI_ADDR_RANGE_BLOCK_SIZE_MASK, &addr_range, block_size_fld);
	FIELD_MODIFY(RMI_ADDR_RANGE_COUNT_MASK, &addr_range, count);
	FIELD_MODIFY(RMI_ADDR_RANGE_STATE_MASK, &addr_range, state);

	sro->addr_list[sro->addr_count] = addr_range;

mem_donate:
	rmi_op_mem_donate(sro_handle,
			  virt_to_phys(&sro->addr_list[sro->addr_count]), 1,
			  0, out_regs);
	donated_size = out_regs->a1 << PAGE_SHIFT;

	if (WARN_ON(out_regs->a1 > (size >> PAGE_SHIFT)))
		donated_size = size;

	/* All granules consumed by the RMM */
	if (donated_size == size)
		return 0;
	/*
	 * No granules were consumed by the RMM, cache them. This is a valid
	 * case, where the RMM was donated memory by another competing thread.
	 */
	if (donated_size == 0) {
		sro->addr_count++;
		return 0;
	}

	/* The granules were partially consumed, reclaim the unused ones. */
	free_addr_range(sro->addr_list[sro->addr_count], donated_size);

	return 0;
}

static int rmi_sro_donate_noncontig(struct rmi_sro_state *sro,
				    unsigned long sro_handle,
				    unsigned long donatereq,
				    struct arm_smccc_1_2_regs *out_regs,
				    gfp_t gfp)
{
	unsigned long block_size_fld = RMI_DONATE_BLOCK_SIZE(donatereq);
	unsigned long block_size = rmi_addr_block_size_to_bytes(block_size_fld);
	unsigned long count = RMI_DONATE_COUNT(donatereq);
	unsigned long state = RMI_DONATE_STATE(donatereq);
	unsigned long found = 0;
	unsigned long donated_granules;
	unsigned long granules_per_block = block_size >> PAGE_SHIFT;
	unsigned long consumed_blocks;
	int addr_list_start = sro->addr_count;
	int ret, i, src;

	/*
	 * We can't allocate blocks more than what the buddy allocator can
	 * satisfy. TODO: Add support for larger allocations.
	 */
	if (get_order(block_size) > MAX_PAGE_ORDER)
		return -ENOMEM;
	/*
	 * Maximum value of RmiOpMemDonateReq:count is 2^(PAGE_SHIFT-2) - 1.
	 * But we further clamp it down by the number of entries we can do
	 * in one go. The RMM can request  the remaining in the next iteration.
	 */
	if (count > RMI_MAX_ADDR_LIST)
		count = RMI_MAX_ADDR_LIST;

	/* Gather the suitable entries to the end of the list */
	i = 0;
	while (i <  addr_list_start && found < count) {
		unsigned long entry = sro->addr_list[i];

		if (RMI_ADDR_RANGE_BLOCK_SIZE(entry) == block_size_fld &&
		    RMI_ADDR_RANGE_COUNT(entry) == 1 &&
		    RMI_ADDR_RANGE_STATE(entry) == state) {
			addr_list_start--;
			swap(sro->addr_list[addr_list_start],
			     sro->addr_list[i]);
			found++;
			/* Continue from the swapped in entry */
			continue;
		}
		/* Skip past the entry */
		i++;
	}

	ret = rmi_sro_ensure_capacity(sro, count - found);
	if (ret) {
		/* If we have found some entries, donate them and try again */
		if (found)
			goto mem_donate;
		/* Otherwise free up the list and start again */
		rmi_sro_free(sro);
		/* Reset the addr_list_start to match sro->addr_count */
		addr_list_start = 0;
	}

	for (; found < count; found++) {
		unsigned long addr_range;
		void *virt = alloc_pages_exact(block_size, gfp);
		phys_addr_t phys;

		if (!virt)
			return -ENOMEM;

		phys = virt_to_phys(virt);

		if (state == RMI_OP_MEM_DELEGATED) {
			phys_addr_t delegated_phys;

			if (rmi_delegate_range(phys, block_size, &delegated_phys)) {
				if (!rmi_undelegate_range(phys, delegated_phys - phys))
					free_pages_exact(virt, block_size);
				return -ENXIO;
			}
		}

		addr_range = phys & RMI_ADDR_RANGE_ADDR_MASK;
		FIELD_MODIFY(RMI_ADDR_RANGE_BLOCK_SIZE_MASK, &addr_range, block_size_fld);
		FIELD_MODIFY(RMI_ADDR_RANGE_COUNT_MASK, &addr_range, 1);
		FIELD_MODIFY(RMI_ADDR_RANGE_STATE_MASK, &addr_range, state);

		sro->addr_list[sro->addr_count++] = addr_range;
	}

mem_donate:
	rmi_op_mem_donate(sro_handle,
			  virt_to_phys(&sro->addr_list[addr_list_start]),
			  found, 0, out_regs);

	donated_granules = out_regs->a1;
	/*
	 * The RMM shouldn't report more granules than we provided, but clamp
	 * just in case.
	 */
	if (WARN_ON_ONCE(donated_granules > found * granules_per_block))
		donated_granules = found * granules_per_block;

	/*
	 * The RMM reports the consumed memory in terms of granules, but we
	 * track in the address lists in block-sized ranges. So divide to get
	 * the number of (complete) consumed blocks.
	 */
	consumed_blocks = donated_granules / granules_per_block;
	if (donated_granules % granules_per_block) {
		/*
		 * A block has been partially consumed, the start is owned by
		 * the RMM, the tail is owned by the host
		 */
		unsigned long entry =
			sro->addr_list[addr_list_start + consumed_blocks];
		unsigned long donated_size =
			(donated_granules % granules_per_block) << PAGE_SHIFT;

		free_addr_range(entry, donated_size);
		/*
		 * This block is now fully 'consumed' (either held by the RMM or
		 * freed)
		 */
		consumed_blocks++;
	}

	/*
	 * Keep just the blocks the RMM didn't use in addr_list
	 * RMM claimed consumed_blocks entries from addr_list_start.
	 * Move the entries left out at the end i.e.,
	 * [ addr_list_start + consumed_blocks, addr_list_start + found)
	 * to the rest of the valid entries and adjust the addr_count to
	 * reflect the available entries.
	 */
	for (i = 0, src = addr_list_start + consumed_blocks;
		i < found - consumed_blocks; i++)
		sro->addr_list[addr_list_start + i] = sro->addr_list[src + i];

	sro->addr_count -= consumed_blocks;

	return 0;
}

static int rmi_sro_donate(struct rmi_sro_state *sro,
			  unsigned long sro_handle,
			  unsigned long donatereq,
			  struct arm_smccc_1_2_regs *regs,
			  gfp_t gfp)
{
	unsigned long state = RMI_DONATE_STATE(donatereq);

	if (WARN_ON_ONCE(!RMI_DONATE_COUNT(donatereq)))
		return -EINVAL;

	/*
	 * We do not support RMI_OP_MEM_CONDITIONAL yet. This is only required
	 * for use in RMI_GRANULE_TRACKING_SET, which we don't support yet.
	 */
	if (WARN_ON_ONCE((state != RMI_OP_MEM_DELEGATED) &&
			 (state != RMI_OP_MEM_UNDELEGATED)))
		return -EINVAL;

	if (RMI_DONATE_CONTIG(donatereq) == RMI_OP_MEM_CONTIG)
		return rmi_sro_donate_contig(sro, sro_handle, donatereq, regs, gfp);
	else
		return rmi_sro_donate_noncontig(sro, sro_handle, donatereq, regs, gfp);
}

static int rmi_sro_reclaim(struct rmi_sro_state *sro,
			   unsigned long sro_handle,
			   struct arm_smccc_1_2_regs *out_regs)
{
	unsigned long capacity;

	/*
	 * We don't do a partial free of the entries. So for now free the
	 * entire address list as we prepare to reclaim more from the RMM.
	 */
	if (rmi_sro_ensure_capacity(sro, 1))
		rmi_sro_free(sro);

	capacity = RMI_MAX_ADDR_LIST - sro->addr_count;

	rmi_op_mem_reclaim(sro_handle,
			   virt_to_phys(&sro->addr_list[sro->addr_count]),
			   capacity, out_regs);

	/*
	 * RMI_OP_MEM_RECLAIM always return RMI_INCOMPLETE, except when the
	 * input parameters were invalid.
	 */
	if (WARN_ON_ONCE(RMI_RESULT_STATUS(out_regs->a0) != RMI_INCOMPLETE))
		return -EINVAL;
	if (WARN_ON_ONCE(out_regs->a1 > capacity))
		out_regs->a1 = capacity;

	sro->addr_count += out_regs->a1;

	return 0;
}

void rmi_sro_free(struct rmi_sro_state *sro)
{
	/* Handle the worse */
	if (WARN_ON(sro->addr_count < 0))
		return;

	if (WARN_ON(sro->addr_count > RMI_MAX_ADDR_LIST))
		sro->addr_count = RMI_MAX_ADDR_LIST;

	for (int i = 0; i < sro->addr_count; i++)
		free_addr_range(sro->addr_list[i], 0);

	sro->addr_count = 0;
}
EXPORT_SYMBOL_GPL(rmi_sro_free);

long rmi_sro_memxfer_execute(struct rmi_sro_state *sro, gfp_t gfp)
{
	struct arm_smccc_1_2_regs *regs = &sro->regs;
	bool cancelled = false;
	unsigned long sro_handle;

	rmi_smccc_invoke(regs);

	sro_handle = regs->a1;
	while (RMI_RESULT_STATUS(regs->a0) == RMI_INCOMPLETE) {
		bool can_cancel = RMI_RESULT_CAN_CANCEL(regs->a0) == RMI_OP_CAN_CANCEL;
		int ret = 0;

		switch (RMI_RESULT_MEMREQ(regs->a0)) {
		case RMI_OP_MEM_REQ_NONE:
			rmi_op_continue(sro_handle, RMI_CONTINUE_KEEP_GOING,
					regs);
			break;
		case RMI_OP_MEM_REQ_DONATE:
			ret = rmi_sro_donate(sro, sro_handle, regs->a2, regs,
					     gfp);
			break;
		case RMI_OP_MEM_REQ_RECLAIM:
			ret = rmi_sro_reclaim(sro, sro_handle, regs);
			break;
		default:
			WARN_ON_ONCE(1);
			ret = -ENXIO;
		}

		if (ret) {
			/*
			 * All memory donating SROs must be cancellable. So a
			 * failure in memory allocation shouldn't be an issue.
			 * However, if we encounter a random failure (e.g.,
			 * buggy RMM), don't loop forever, just give up.
			 */
			if (WARN_ON_ONCE(!can_cancel))
				return ret;
			/*
			 * If we have already cancelled, and came back here due
			 * to an error in MEMREQ, then there is no point
			 * in going in loops.
			 */
			if (WARN_ON_ONCE(cancelled))
				break;
			rmi_op_cancel(sro_handle, regs);
			cancelled = true;

			if (WARN_ON_ONCE(RMI_RESULT_STATUS(regs->a0) != RMI_INCOMPLETE))
				return ret;
		}
	}

	if (cancelled)
		return -ECANCELED;

	return regs->a0;
}
EXPORT_SYMBOL_GPL(rmi_sro_memxfer_execute);

/*
 * rmi_sro_execute: Execute an RMI command that is stateful but not memory
 * transferring. Takes regs, filled with the FIDs and the arguments in place.
 *
 * Returns :
 *  -ECANCELED - If the operation had to be aborted and SRO was cancellable.
 *  Otherwise, returns the result of the RMI command.
 */
long rmi_sro_execute(struct arm_smccc_1_2_regs *regs)
{
	bool cancelled = false;
	unsigned long sro_handle;

	rmi_smccc_invoke(regs);

	sro_handle = regs->a1;
	while (RMI_RESULT_STATUS(regs->a0) == RMI_INCOMPLETE) {
		bool can_cancel = RMI_RESULT_CAN_CANCEL(regs->a0) == RMI_OP_CAN_CANCEL;

		switch (RMI_RESULT_MEMREQ(regs->a0)) {
		case RMI_OP_MEM_REQ_NONE:
			rmi_op_continue(sro_handle, RMI_CONTINUE_KEEP_GOING,
					regs);
			break;
		default:
			WARN_ON_ONCE(1);
			if (!can_cancel)
				return regs->a0;
			/*
			 * We can't get here normally, but handle this anyway
			 * for a buggy RMM implementation.
			 */
			if (cancelled)
				return -ECANCELED;
			rmi_op_cancel(sro_handle, regs);
			cancelled = true;
		}
	}

	if (cancelled)
		return -ECANCELED;

	return regs->a0;
}
EXPORT_SYMBOL_GPL(rmi_sro_execute);

static int rmi_check_version(void)
{
	unsigned short version_major, version_minor;
	unsigned long host_version = RMI_ABI_VERSION(RMI_ABI_MAJOR_VERSION,
						     RMI_ABI_MINOR_VERSION);
	unsigned long aa64pfr0 = read_sanitised_ftr_reg(SYS_ID_AA64PFR0_EL1);
	struct arm_smccc_1_2_regs res = {
		SMC_RMI_VERSION, host_version,
	};

	/* If RME isn't supported, then RMI can't be */
	if (cpuid_feature_extract_unsigned_field(aa64pfr0, ID_AA64PFR0_EL1_RME_SHIFT) == 0)
		return -ENXIO;

	if (arm_smccc_1_1_get_conduit() != SMCCC_CONDUIT_SMC)
		return -ENXIO;

	rmi_smccc_invoke(&res);
	if (res.a0 == SMCCC_RET_NOT_SUPPORTED)
		return -ENXIO;

	version_major = RMI_ABI_VERSION_GET_MAJOR(res.a1);
	version_minor = RMI_ABI_VERSION_GET_MINOR(res.a1);

	if (res.a0 != RMI_SUCCESS) {
		unsigned short high_version_major, high_version_minor;

		high_version_major = RMI_ABI_VERSION_GET_MAJOR(res.a2);
		high_version_minor = RMI_ABI_VERSION_GET_MINOR(res.a2);

		pr_err("Unsupported RMI ABI (v%d.%d - v%d.%d) we want v%d.%d\n",
		       version_major, version_minor,
		       high_version_major, high_version_minor,
		       RMI_ABI_MAJOR_VERSION,
		       RMI_ABI_MINOR_VERSION);
		return -ENXIO;
	}

	pr_info("RMI ABI version %d.%d\n", version_major, version_minor);

	return 0;
}

static int rmi_read_features(void)
{
	/*
	 * Since we've negotiated a compatible version these feature registers
	 * should always be available
	 */
	for (int i = 0; i < ARRAY_SIZE(rmi_feat_reg_cache); i++) {
		struct arm_smccc_1_2_regs args = {
			SMC_RMI_FEATURES, i,
		};

		rmi_smccc_invoke(&args);
		if (WARN_ON(args.a0 != RMI_SUCCESS))
			return -EINVAL;

		rmi_feat_reg_cache[i] = args.a1;
	}

	return 0;
}

unsigned long rmi_feat_reg(unsigned int index)
{
	if (WARN_ON(index >= ARRAY_SIZE(rmi_feat_reg_cache)))
		return 0;

	return rmi_feat_reg_cache[index];
}
EXPORT_SYMBOL_GPL(rmi_feat_reg);

static int rmi_rmm_config_set(unsigned long cfg_ptr)
{
	struct arm_smccc_1_2_regs regs = {
		SMC_RMI_RMM_CONFIG_SET, cfg_ptr,
	};

	rmi_smccc_invoke(&regs);

	return regs.a0;
}

static int rmi_configure(void)
{
	unsigned long granule_feature;
	unsigned long granule_size;
	int ret;

	switch (PAGE_SIZE) {
	case SZ_4K:
		granule_size = RMI_GRANULE_SIZE_4KB;
		granule_feature = RMI_FEATURE_REGISTER_1_RMI_GRAN_SZ_4KB;
		break;
	case SZ_16K:
		granule_size = RMI_GRANULE_SIZE_16KB;
		granule_feature = RMI_FEATURE_REGISTER_1_RMI_GRAN_SZ_16KB;
		break;
	case SZ_64K:
		granule_size = RMI_GRANULE_SIZE_64KB;
		granule_feature = RMI_FEATURE_REGISTER_1_RMI_GRAN_SZ_64KB;
		break;
	default:
		BUILD_BUG();
	}

	if (!(rmi_feat_reg(1) & granule_feature)) {
		pr_err("RMM does not support %luKB granules\n",
		       PAGE_SIZE >> 10);
		return -ENXIO;
	}

	struct rmm_config *config __free(free_page) =
		(struct rmm_config *)get_zeroed_page(GFP_KERNEL);

	if (!config) {
		pr_err("Unable to allocate memory for RMM config\n");
		return -ENOMEM;
	}

	config->rmi_granule_size = granule_size;

	/*
	 * For now we set the tracking_region_size to 0 which is the only option
	 * for 4KB PAGE_SIZE (1GB for 4KB PAGE_SIZE, 32MB/512MB for 16KB/64KB).
	 * TODO: Support other tracking sizes via Kconfig option for other
	 * PAGE_SIZES
	 */
	config->tracking_region_size = 0;

	ret = rmi_rmm_config_set(virt_to_phys(config));
	if (ret) {
		pr_err("RMM config set failed (%d)\n", ret);
		return -EINVAL;
	}

	return 0;
}

/**
 * rmi_granule_tracking_get() - Get configuration of a Granule tracking region
 * @start: Base PA of the tracking region
 * @end: End of the PA region
 * @out_category: Memory category
 * @out_state: Tracking region state
 * @out_top: Top of the memory region
 *
 * Return: RMI return code
 */
static int rmi_granule_tracking_get(unsigned long start,
				    unsigned long end,
				    unsigned long *out_category,
				    unsigned long *out_state,
				    unsigned long *out_top)
{
	struct arm_smccc_1_2_regs regs = {
		SMC_RMI_GRANULE_TRACKING_GET, start, end,
	};

	rmi_smccc_invoke(&regs);

	if (regs.a0 != RMI_SUCCESS)
		return regs.a0;

	if (out_category)
		*out_category = regs.a1;
	if (out_state)
		*out_state = regs.a2;
	if (out_top)
		*out_top = regs.a3;

	return RMI_SUCCESS;
}

/*
 * Make sure the area is tracked by RMM at FINE granularity.
 * We do not support changing the tracking yet.
 */
static int rmi_verify_memory_tracking(phys_addr_t start, phys_addr_t end)
{
	while (start < end) {
		unsigned long ret, category, state, next;

		ret = rmi_granule_tracking_get(start, end, &category, &state, &next);
		if (ret != RMI_SUCCESS) {
			pr_err("RMI_GRANULE_TRACKING_GET failed for %llx-%llx: (%lu)\n",
			       start, end, ret);
			return -ENXIO;
		}

		if (WARN_ON(next <= start))
			return -ENXIO;

		if (state != RMI_TRACKING_FINE ||
		    category != RMI_MEM_CATEGORY_CONVENTIONAL) {
			/* TODO: Set granule tracking in this case */
			pr_err("Granule tracking for region isn't fine/conventional: %llx-%lx\n",
			       start, next);
			return -ENODEV;
		}
		start = next;
	}

	return 0;
}

/*
 * rmi_gpt_info - Query the GPT info for the given PAR.
 * @start: Base of the physical address region
 * @end: Top of the physical address region
 * @out_top: Top of the physical address region for which the GPT @out_gpt_par_state
 *		is valid
 * @out_gpt_par_state: State of the GPT covered by [start, out_top)
 */
static long rmi_gpt_info(unsigned long start, unsigned long end,
			 unsigned long *out_top,
			 unsigned long *out_gpt_par_state)
{
	struct arm_smccc_1_2_regs regs = {
		SMC_RMI_GPT_INFO, start, end,
	};

	rmi_smccc_invoke(&regs);
	if (regs.a0 != RMI_SUCCESS)
		return regs.a0;

	if (out_top)
		*out_top = regs.a1;
	if (out_gpt_par_state)
		*out_gpt_par_state = regs.a2;

	return RMI_SUCCESS;
}

/*
 * We do not support creating L1 GPTs yet. So, make sure that all the regions
 * are managed by the firmware.
 */
static int rmi_verify_gpt_firmware_managed(phys_addr_t start, phys_addr_t end)
{
	unsigned long l0gpt_sz;
	unsigned long next, par_state;

	l0gpt_sz = 1UL << (30 + FIELD_GET(RMI_FEATURE_REGISTER_1_L0GPTSZ,
					  rmi_feat_reg(1)));
	start = ALIGN_DOWN(start, l0gpt_sz);
	end = ALIGN(end, l0gpt_sz);

	while (start < end) {
		long ret = rmi_gpt_info(start, end, &next, &par_state);

		if (ret != RMI_SUCCESS) {
			pr_err("RMI_GPT_INFO failed for %llx-%llx: (%ld)\n",
			       start, end, ret);
			return -ENXIO;
		}

		if (WARN_ON(next <= start))
			return -ENXIO;

		if (par_state != RMI_GPT_PAR_PLAT) {
			pr_err("GPT for the region is not managed by firmware %llx-%lx\n",
			       start, next);
			return -ENODEV;
		}
		start = next;
	}

	return 0;
}

static int rmi_prepare_memory(phys_addr_t start, phys_addr_t end)
{
	int ret;

	if (start >= end)
		return -EINVAL;

	ret = rmi_verify_memory_tracking(start, end);
	if (ret)
		return ret;

	return rmi_verify_gpt_firmware_managed(start, end);
}

static int rmi_init_metadata(void)
{
	phys_addr_t start, end;
	struct memblock_region *r;

	for_each_mem_region(r) {
		int ret;

		/* Firmware-reserved NOMAP regions are not usable system RAM */
		if (memblock_is_nomap(r))
			continue;

		start = PAGE_ALIGN(r->base);
		/*
		 * We always deal with PAGE_SIZE and if in the odd case this
		 * region boundary is not PAGE aligned, we stick to the page
		 * that we can use from the region.
		 */
		end = PAGE_ALIGN_DOWN(r->base + r->size);
		/* Too small ? */
		if (start >= end)
			continue;

		ret = rmi_prepare_memory(start, end);
		if (ret)
			return ret;
	}

	return 0;
}

static int rmi_memory_notifier(struct notifier_block *nb,
			       unsigned long action, void *data)
{
	struct memory_notify *arg = data;
	phys_addr_t start, end;
	int ret;

	if (action != MEM_GOING_ONLINE)
		return NOTIFY_DONE;

	start = PFN_PHYS(arg->start_pfn);
	end = PFN_PHYS(arg->start_pfn + arg->nr_pages);

	/*
	 * If the hotplugged memory region is in ZONE_MOVABLE, neither the
	 * guest-memfd nor the kernel allocations can come from there. So we are
	 * fine to skip this.
	 * For boot time memory, we don't allow this discount, as the
	 * decision to move boot memory pages to ZONE_MOVABLE is purely
	 * a kernel choice and the firmware must have decided what it covers.
	 */
	if (is_zone_movable_page(pfn_to_page(arg->start_pfn)))
		return NOTIFY_DONE;

	ret = rmi_prepare_memory(start, end);

	return notifier_from_errno(ret);
}

static struct notifier_block rmi_memory_nb = {
	.notifier_call = rmi_memory_notifier,
};

static int rmi_init_memory(void)
{
	int ret;

	ret = rmi_init_metadata();
	if (ret)
		return ret;

	return register_memory_notifier(&rmi_memory_nb);
}

static int __init arm64_init_rmi(void)
{
	int ret;

	/* If we can't agree on the RMI ABI version, don't proceed further */
	ret = rmi_check_version();
	if (ret)
		return ret;

	ret = rmi_read_features();
	if (ret)
		return ret;

	ret = rmi_configure();
	if (ret)
		return ret;

	/* Activate the RMM */
	struct rmi_sro_state *sro __free(kfree) = kmalloc_obj(*sro);
	if (!sro)
		return -ENOMEM;

	ret = rmi_sro_memxfer_cmd(sro, GFP_KERNEL, SMC_RMI_RMM_ACTIVATE);
	if (ret) {
		pr_err("RMM activate failed (%d)\n", ret);
		ret = ret < 0 ? ret : -ENXIO;
		return ret;
	}

	WRITE_ONCE(arm64_rmm_active, true);

	ret = rmi_init_memory();
	if (ret) {
		/* Deactivate the RMM */
		if (!WARN_ON(rmi_sro_memxfer_cmd(sro, GFP_KERNEL, SMC_RMI_RMM_DEACTIVATE)))
			WRITE_ONCE(arm64_rmm_active, false);
		return ret;
	}

	/* Mark the RMM is usable */
	WRITE_ONCE(arm64_rmi_is_available, true);
	pr_info("RMI configured\n");

	return 0;
}

/*
 * Note arm64_init_rmi() must be called before kvm_init_rmi() otherwise KVM
 * will not support realm guests. subsys_initcall() is called before
 * module_init() (used for KVM) so this is OK.
 */
subsys_initcall(arm64_init_rmi);
