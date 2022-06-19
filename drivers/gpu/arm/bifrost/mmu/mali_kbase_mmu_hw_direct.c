// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014-2022 ARM Limited. All rights reserved.
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

#include <device/mali_kbase_device.h>
#include <linux/bitops.h>
#include <mali_kbase.h>
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_mem.h>
#include <mmu/mali_kbase_mmu_hw.h>
#include <tl/mali_kbase_tracepoints.h>
#include <linux/delay.h>


/**
 * lock_region() - Generate lockaddr to lock memory region in MMU
 *
 * @gpu_props: GPU properties for finding the MMU lock region size.
 * @lockaddr:  Address and size of memory region to lock.
 * @op_param:  Pointer to a struct containing the starting page frame number of
 *             the region to lock, the number of pages to lock and page table
 *             levels to skip when flushing (if supported).
 *
 * The lockaddr value is a combination of the starting address and
 * the size of the region that encompasses all the memory pages to lock.
 *
 * Bits 5:0 are used to represent the size, which must be a power of 2.
 * The smallest amount of memory to be locked corresponds to 32 kB,
 * i.e. 8 memory pages, because a MMU cache line is made of 64 bytes
 * and every page table entry is 8 bytes. Therefore it is not possible
 * to lock less than 8 memory pages at a time.
 *
 * The size is expressed as a logarithm minus one:
 * - A value of 14 is thus interpreted as log(32 kB) = 15, where 32 kB
 *   is the smallest possible size.
 * - Likewise, a value of 47 is interpreted as log(256 TB) = 48, where 256 TB
 *   is the largest possible size (implementation defined value according
 *   to the HW spec).
 *
 * Bits 11:6 are reserved.
 *
 * Bits 63:12 are used to represent the base address of the region to lock.
 * Only the upper bits of the address are used; lowest bits are cleared
 * to avoid confusion.
 *
 * The address is aligned to a multiple of the region size. This has profound
 * implications on the region size itself: often the MMU will lock a region
 * larger than the given number of pages, because the lock region cannot start
 * from any arbitrary address.
 *
 * Return: 0 if success, or an error code on failure.
 */
static int lock_region(struct kbase_gpu_props const *gpu_props, u64 *lockaddr,
		       const struct kbase_mmu_hw_op_param *op_param)
{
	const u64 lockaddr_base = op_param->vpfn << PAGE_SHIFT;
	const u64 lockaddr_end = ((op_param->vpfn + op_param->nr) << PAGE_SHIFT) - 1;
	u64 lockaddr_size_log2;

	if (op_param->nr == 0)
		return -EINVAL;

	/* The MMU lock region is a self-aligned region whose size
	 * is a power of 2 and that contains both start and end
	 * of the address range determined by pfn and num_pages.
	 * The size of the MMU lock region can be defined as the
	 * largest divisor that yields the same result when both
	 * start and end addresses are divided by it.
	 *
	 * For instance: pfn=0x4F000 num_pages=2 describe the
	 * address range between 0x4F000 and 0x50FFF. It is only
	 * 2 memory pages. However there isn't a single lock region
	 * of 8 kB that encompasses both addresses because 0x4F000
	 * would fall into the [0x4E000, 0x4FFFF] region while
	 * 0x50000 would fall into the [0x50000, 0x51FFF] region.
	 * The minimum lock region size that includes the entire
	 * address range is 128 kB, and the region would be
	 * [0x40000, 0x5FFFF].
	 *
	 * The region size can be found by comparing the desired
	 * start and end addresses and finding the highest bit
	 * that differs. The smallest naturally aligned region
	 * must include this bit change, hence the desired region
	 * starts with this bit (and subsequent bits) set to 0
	 * and ends with the bit (and subsequent bits) set to 1.
	 *
	 * In the example above: 0x4F000 ^ 0x50FFF = 0x1FFFF
	 * therefore the highest bit that differs is bit #16
	 * and the region size (as a logarithm) is 16 + 1 = 17, i.e. 128 kB.
	 */
	lockaddr_size_log2 = fls(lockaddr_base ^ lockaddr_end);

	/* Cap the size against minimum and maximum values allowed. */
	if (lockaddr_size_log2 > KBASE_LOCK_REGION_MAX_SIZE_LOG2)
		return -EINVAL;

	lockaddr_size_log2 =
		MAX(lockaddr_size_log2, kbase_get_lock_region_min_size_log2(gpu_props));

	/* Represent the result in a way that is compatible with HW spec.
	 *
	 * Upper bits are used for the base address, whose lower bits
	 * are cleared to avoid confusion because they are going to be ignored
	 * by the MMU anyway, since lock regions shall be aligned with
	 * a multiple of their size and cannot start from any address.
	 *
	 * Lower bits are used for the size, which is represented as
	 * logarithm minus one of the actual size.
	 */
	*lockaddr = lockaddr_base & ~((1ull << lockaddr_size_log2) - 1);
	*lockaddr |= lockaddr_size_log2 - 1;
	return 0;
}

static int wait_ready(struct kbase_device *kbdev,
		unsigned int as_nr)
{
	u32 max_loops = KBASE_AS_INACTIVE_MAX_LOOPS;

	/* Wait for the MMU status to indicate there is no active command. */
	while (--max_loops &&
	       kbase_reg_read(kbdev, MMU_AS_REG(as_nr, AS_STATUS)) &
		       AS_STATUS_AS_ACTIVE) {
		;
	}

	if (WARN_ON_ONCE(max_loops == 0)) {
		dev_err(kbdev->dev,
			"AS_ACTIVE bit stuck for as %u, might be caused by slow/unstable GPU clock or possible faulty FPGA connector",
			as_nr);
		return -1;
	}

	return 0;
}

static int write_cmd(struct kbase_device *kbdev, int as_nr, u32 cmd)
{
	int status;

	/* write AS_COMMAND when MMU is ready to accept another command */
	status = wait_ready(kbdev, as_nr);
	if (status == 0)
		kbase_reg_write(kbdev, MMU_AS_REG(as_nr, AS_COMMAND), cmd);
	else {
		dev_err(kbdev->dev,
			"Wait for AS_ACTIVE bit failed for as %u, before sending MMU command %u",
			as_nr, cmd);
	}

	return status;
}

#if MALI_USE_CSF && !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
static int wait_cores_power_trans_complete(struct kbase_device *kbdev)
{
#define WAIT_TIMEOUT 1000 /* 1ms timeout */
#define DELAY_TIME_IN_US 1
	const int max_iterations = WAIT_TIMEOUT;
	int loop;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (loop = 0; loop < max_iterations; loop++) {
		u32 lo =
		    kbase_reg_read(kbdev, GPU_CONTROL_REG(SHADER_PWRTRANS_LO));
		u32 hi =
		    kbase_reg_read(kbdev, GPU_CONTROL_REG(SHADER_PWRTRANS_HI));

		if (!lo && !hi)
			break;

		udelay(DELAY_TIME_IN_US);
	}

	if (loop == max_iterations) {
		dev_warn(kbdev->dev, "SHADER_PWRTRANS set for too long");
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * apply_hw_issue_GPU2019_3901_wa - Apply WA for the HW issue GPU2019_3901
 *
 * @kbdev:             Kbase device to issue the MMU operation on.
 * @mmu_cmd:           Pointer to the variable contain the value of MMU command
 *                     that needs to be sent to flush the L2 cache and do an
 *                     implicit unlock.
 * @as_nr:             Address space number for which MMU command needs to be
 *                     sent.
 * @hwaccess_locked:   Flag to indicate if hwaccess_lock is held by the caller.
 *
 * This functions ensures that the flush of LSC is not missed for the pages that
 * were unmapped from the GPU, due to the power down transition of shader cores.
 *
 * Return: 0 if the WA was successfully applied, non-zero otherwise.
 */
static int apply_hw_issue_GPU2019_3901_wa(struct kbase_device *kbdev,
			u32 *mmu_cmd, unsigned int as_nr, bool hwaccess_locked)
{
	unsigned long flags = 0;
	int ret = 0;

	if (!hwaccess_locked)
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	/* Check if L2 is OFF. The cores also must be OFF if L2 is not up, so
	 * the workaround can be safely skipped.
	 */
	if (kbdev->pm.backend.l2_state != KBASE_L2_OFF) {
		if (*mmu_cmd != AS_COMMAND_FLUSH_MEM) {
			dev_warn(kbdev->dev,
				 "Unexpected mmu command received");
			ret = -EINVAL;
			goto unlock;
		}

		/* Wait for the LOCK MMU command to complete, issued by the caller */
		ret = wait_ready(kbdev, as_nr);
		if (ret)
			goto unlock;

		ret = kbase_gpu_cache_flush_and_busy_wait(kbdev,
				GPU_COMMAND_CACHE_CLN_INV_LSC);
		if (ret)
			goto unlock;

		ret = wait_cores_power_trans_complete(kbdev);
		if (ret)
			goto unlock;

		/* As LSC is guaranteed to have been flushed we can use FLUSH_PT
		 * MMU command to only flush the L2.
		 */
		*mmu_cmd = AS_COMMAND_FLUSH_PT;
	}

unlock:
	if (!hwaccess_locked)
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return ret;
}
#endif

void kbase_mmu_hw_configure(struct kbase_device *kbdev, struct kbase_as *as)
{
	struct kbase_mmu_setup *current_setup = &as->current_setup;
	u64 transcfg = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&kbdev->mmu_hw_mutex);

	transcfg = current_setup->transcfg;

	/* Set flag AS_TRANSCFG_PTW_MEMATTR_WRITE_BACK
	 * Clear PTW_MEMATTR bits
	 */
	transcfg &= ~AS_TRANSCFG_PTW_MEMATTR_MASK;
	/* Enable correct PTW_MEMATTR bits */
	transcfg |= AS_TRANSCFG_PTW_MEMATTR_WRITE_BACK;
	/* Ensure page-tables reads use read-allocate cache-policy in
	 * the L2
	 */
	transcfg |= AS_TRANSCFG_R_ALLOCATE;

	if (kbdev->system_coherency != COHERENCY_NONE) {
		/* Set flag AS_TRANSCFG_PTW_SH_OS (outer shareable)
		 * Clear PTW_SH bits
		 */
		transcfg = (transcfg & ~AS_TRANSCFG_PTW_SH_MASK);
		/* Enable correct PTW_SH bits */
		transcfg = (transcfg | AS_TRANSCFG_PTW_SH_OS);
	}

	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSCFG_LO),
			transcfg);
	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSCFG_HI),
			(transcfg >> 32) & 0xFFFFFFFFUL);

	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSTAB_LO),
			current_setup->transtab & 0xFFFFFFFFUL);
	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSTAB_HI),
			(current_setup->transtab >> 32) & 0xFFFFFFFFUL);

	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_MEMATTR_LO),
			current_setup->memattr & 0xFFFFFFFFUL);
	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_MEMATTR_HI),
			(current_setup->memattr >> 32) & 0xFFFFFFFFUL);

	KBASE_TLSTREAM_TL_ATTRIB_AS_CONFIG(kbdev, as,
			current_setup->transtab,
			current_setup->memattr,
			transcfg);

	write_cmd(kbdev, as->number, AS_COMMAND_UPDATE);
#if MALI_USE_CSF
	/* Wait for UPDATE command to complete */
	wait_ready(kbdev, as->number);
#endif
}

/**
 * mmu_command_instr - Record an MMU command for instrumentation purposes.
 *
 * @kbdev:          Kbase device used to issue MMU operation on.
 * @kctx_id:        Kernel context ID for MMU command tracepoint.
 * @cmd:            Command issued to the MMU.
 * @lock_addr:      Address of memory region locked for the operation.
 * @mmu_sync_info:  Indicates whether this call is synchronous wrt MMU ops.
 */
static void mmu_command_instr(struct kbase_device *kbdev, u32 kctx_id, u32 cmd, u64 lock_addr,
				    enum kbase_caller_mmu_sync_info mmu_sync_info)
{
	u64 lock_addr_base = AS_LOCKADDR_LOCKADDR_BASE_GET(lock_addr);
	u32 lock_addr_size = AS_LOCKADDR_LOCKADDR_SIZE_GET(lock_addr);

	bool is_mmu_synchronous = (mmu_sync_info == CALLER_MMU_SYNC);

	KBASE_TLSTREAM_AUX_MMU_COMMAND(kbdev, kctx_id, cmd, is_mmu_synchronous, lock_addr_base,
				       lock_addr_size);
}

/* Helper function to program the LOCKADDR register before LOCK/UNLOCK command
 * is issued.
 */
static int mmu_hw_set_lock_addr(struct kbase_device *kbdev, int as_nr, u64 *lock_addr,
				const struct kbase_mmu_hw_op_param *op_param)
{
	int ret;

	ret = lock_region(&kbdev->gpu_props, lock_addr, op_param);

	if (!ret) {
		/* Set the region that needs to be updated */
		kbase_reg_write(kbdev, MMU_AS_REG(as_nr, AS_LOCKADDR_LO),
				*lock_addr & 0xFFFFFFFFUL);
		kbase_reg_write(kbdev, MMU_AS_REG(as_nr, AS_LOCKADDR_HI),
				(*lock_addr >> 32) & 0xFFFFFFFFUL);
	}
	return ret;
}

/**
 * mmu_hw_do_lock_no_wait - Issue LOCK command to the MMU and return without
 *                          waiting for it's completion.
 *
 * @kbdev:      Kbase device to issue the MMU operation on.
 * @as:         Address space to issue the MMU operation on.
 * @lock_addr:  Address of memory region locked for this operation.
 * @op_param:   Pointer to a struct containing information about the MMU operation.
 *
 * Return: 0 if issuing the command was successful, otherwise an error code.
 */
static int mmu_hw_do_lock_no_wait(struct kbase_device *kbdev, struct kbase_as *as, u64 *lock_addr,
				  const struct kbase_mmu_hw_op_param *op_param)
{
	int ret;

	ret = mmu_hw_set_lock_addr(kbdev, as->number, lock_addr, op_param);

	if (!ret)
		write_cmd(kbdev, as->number, AS_COMMAND_LOCK);

	return ret;
}

static int mmu_hw_do_lock(struct kbase_device *kbdev, struct kbase_as *as,
			  const struct kbase_mmu_hw_op_param *op_param)
{
	int ret;
	u64 lock_addr = 0x0;

	if (WARN_ON(kbdev == NULL) || WARN_ON(as == NULL))
		return -EINVAL;

	ret = mmu_hw_do_lock_no_wait(kbdev, as, &lock_addr, op_param);

	if (!ret)
		ret = wait_ready(kbdev, as->number);

	if (!ret)
		mmu_command_instr(kbdev, op_param->kctx_id, AS_COMMAND_LOCK, lock_addr,
				  op_param->mmu_sync_info);

	return ret;
}

int kbase_mmu_hw_do_unlock_no_addr(struct kbase_device *kbdev, struct kbase_as *as,
				   const struct kbase_mmu_hw_op_param *op_param)
{
	int ret = 0;

	if (WARN_ON(kbdev == NULL) || WARN_ON(as == NULL))
		return -EINVAL;

	ret = write_cmd(kbdev, as->number, AS_COMMAND_UNLOCK);

	/* Wait for UNLOCK command to complete */
	if (!ret)
		ret = wait_ready(kbdev, as->number);

	if (!ret) {
		u64 lock_addr = 0x0;
		/* read MMU_AS_CONTROL.LOCKADDR register */
		lock_addr |= (u64)kbase_reg_read(kbdev, MMU_AS_REG(as->number, AS_LOCKADDR_HI))
			     << 32;
		lock_addr |= (u64)kbase_reg_read(kbdev, MMU_AS_REG(as->number, AS_LOCKADDR_LO));

		mmu_command_instr(kbdev, op_param->kctx_id, AS_COMMAND_UNLOCK,
				  lock_addr, op_param->mmu_sync_info);
	}

	return ret;
}

int kbase_mmu_hw_do_unlock(struct kbase_device *kbdev, struct kbase_as *as,
			   const struct kbase_mmu_hw_op_param *op_param)
{
	int ret = 0;
	u64 lock_addr = 0x0;

	if (WARN_ON(kbdev == NULL) || WARN_ON(as == NULL))
		return -EINVAL;

	ret = mmu_hw_set_lock_addr(kbdev, as->number, &lock_addr, op_param);

	if (!ret)
		ret = kbase_mmu_hw_do_unlock_no_addr(kbdev, as,
						     op_param);

	return ret;
}

static int mmu_hw_do_flush(struct kbase_device *kbdev, struct kbase_as *as,
	const struct kbase_mmu_hw_op_param *op_param, bool hwaccess_locked)
{
	int ret;
	u64 lock_addr = 0x0;
	u32 mmu_cmd = AS_COMMAND_FLUSH_MEM;

	if (WARN_ON(kbdev == NULL) || WARN_ON(as == NULL))
		return -EINVAL;

	/* MMU operations can be either FLUSH_PT or FLUSH_MEM, anything else at
	 * this point would be unexpected.
	 */
	if (op_param->op != KBASE_MMU_OP_FLUSH_PT &&
	    op_param->op != KBASE_MMU_OP_FLUSH_MEM) {
		dev_err(kbdev->dev, "Unexpected flush operation received");
		return -EINVAL;
	}

	lockdep_assert_held(&kbdev->mmu_hw_mutex);

	if (op_param->op == KBASE_MMU_OP_FLUSH_PT)
		mmu_cmd = AS_COMMAND_FLUSH_PT;

	/* Lock the region that needs to be updated */
	ret = mmu_hw_do_lock_no_wait(kbdev, as, &lock_addr, op_param);
	if (ret)
		return ret;

#if MALI_USE_CSF && !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
	/* WA for the BASE_HW_ISSUE_GPU2019_3901. No runtime check is used here
	 * as the WA is applicable to all CSF GPUs where FLUSH_MEM/PT command is
	 * supported, and this function doesn't gets called for the GPUs where
	 * FLUSH_MEM/PT command is deprecated.
	 */
	if (mmu_cmd == AS_COMMAND_FLUSH_MEM) {
		ret = apply_hw_issue_GPU2019_3901_wa(kbdev, &mmu_cmd,
						as->number, hwaccess_locked);
		if (ret)
			return ret;
	}
#endif

	write_cmd(kbdev, as->number, mmu_cmd);

	/* Wait for the command to complete */
	ret = wait_ready(kbdev, as->number);

	if (!ret)
		mmu_command_instr(kbdev, op_param->kctx_id, mmu_cmd, lock_addr,
				  op_param->mmu_sync_info);

	return ret;
}

int kbase_mmu_hw_do_flush_locked(struct kbase_device *kbdev, struct kbase_as *as,
				 const struct kbase_mmu_hw_op_param *op_param)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	return mmu_hw_do_flush(kbdev, as, op_param, true);
}

int kbase_mmu_hw_do_flush(struct kbase_device *kbdev, struct kbase_as *as,
			  const struct kbase_mmu_hw_op_param *op_param)
{
	return mmu_hw_do_flush(kbdev, as, op_param, false);
}

int kbase_mmu_hw_do_flush_on_gpu_ctrl(struct kbase_device *kbdev, struct kbase_as *as,
				      const struct kbase_mmu_hw_op_param *op_param)
{
	int ret, ret2;
	u32 gpu_cmd = GPU_COMMAND_CACHE_CLN_INV_L2_LSC;

	if (WARN_ON(kbdev == NULL) || WARN_ON(as == NULL))
		return -EINVAL;

	/* MMU operations can be either FLUSH_PT or FLUSH_MEM, anything else at
	 * this point would be unexpected.
	 */
	if (op_param->op != KBASE_MMU_OP_FLUSH_PT &&
	    op_param->op != KBASE_MMU_OP_FLUSH_MEM) {
		dev_err(kbdev->dev, "Unexpected flush operation received");
		return -EINVAL;
	}

	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&kbdev->mmu_hw_mutex);

	if (op_param->op == KBASE_MMU_OP_FLUSH_PT)
		gpu_cmd = GPU_COMMAND_CACHE_CLN_INV_L2;

	/* 1. Issue MMU_AS_CONTROL.COMMAND.LOCK operation. */
	ret = mmu_hw_do_lock(kbdev, as, op_param);
	if (ret)
		return ret;

	/* 2. Issue GPU_CONTROL.COMMAND.FLUSH_CACHES operation */
	ret = kbase_gpu_cache_flush_and_busy_wait(kbdev, gpu_cmd);

	/* 3. Issue MMU_AS_CONTROL.COMMAND.UNLOCK operation. */
	ret2 = kbase_mmu_hw_do_unlock_no_addr(kbdev, as, op_param);

	return ret ?: ret2;
}

void kbase_mmu_hw_clear_fault(struct kbase_device *kbdev, struct kbase_as *as,
		enum kbase_mmu_fault_type type)
{
	unsigned long flags;
	u32 pf_bf_mask;

	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);

	/*
	 * A reset is in-flight and we're flushing the IRQ + bottom half
	 * so don't update anything as it could race with the reset code.
	 */
	if (kbdev->irq_reset_flush)
		goto unlock;

	/* Clear the page (and bus fault IRQ as well in case one occurred) */
	pf_bf_mask = MMU_PAGE_FAULT(as->number);
#if !MALI_USE_CSF
	if (type == KBASE_MMU_FAULT_TYPE_BUS ||
			type == KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED)
		pf_bf_mask |= MMU_BUS_ERROR(as->number);
#endif
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), pf_bf_mask);

unlock:
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}

void kbase_mmu_hw_enable_fault(struct kbase_device *kbdev, struct kbase_as *as,
		enum kbase_mmu_fault_type type)
{
	unsigned long flags;
	u32 irq_mask;

	/* Enable the page fault IRQ
	 * (and bus fault IRQ as well in case one occurred)
	 */
	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);

	/*
	 * A reset is in-flight and we're flushing the IRQ + bottom half
	 * so don't update anything as it could race with the reset code.
	 */
	if (kbdev->irq_reset_flush)
		goto unlock;

	irq_mask = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK)) |
			MMU_PAGE_FAULT(as->number);

#if !MALI_USE_CSF
	if (type == KBASE_MMU_FAULT_TYPE_BUS ||
			type == KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED)
		irq_mask |= MMU_BUS_ERROR(as->number);
#endif
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), irq_mask);

unlock:
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}
