// SPDX-License-Identifier:	GPL-2.0
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sizes.h>

#include "panfrost_device.h"
#include "panfrost_mmu.h"
#include "panfrost_gem.h"
#include "panfrost_features.h"
#include "panfrost_regs.h"

#define mmu_write(dev, reg, data) writel(data, dev->iomem + reg)
#define mmu_read(dev, reg) readl(dev->iomem + reg)

struct panfrost_mmu {
	struct io_pgtable_cfg pgtbl_cfg;
	struct io_pgtable_ops *pgtbl_ops;
	struct mutex lock;
};

static int wait_ready(struct panfrost_device *pfdev, u32 as_nr)
{
	int ret;
	u32 val;

	/* Wait for the MMU status to indicate there is no active command, in
	 * case one is pending. */
	ret = readl_relaxed_poll_timeout_atomic(pfdev->iomem + AS_STATUS(as_nr),
		val, !(val & AS_STATUS_AS_ACTIVE), 10, 1000);

	if (ret)
		dev_err(pfdev->dev, "AS_ACTIVE bit stuck\n");

	return ret;
}

static int write_cmd(struct panfrost_device *pfdev, u32 as_nr, u32 cmd)
{
	int status;

	/* write AS_COMMAND when MMU is ready to accept another command */
	status = wait_ready(pfdev, as_nr);
	if (!status)
		mmu_write(pfdev, AS_COMMAND(as_nr), cmd);

	return status;
}

static void lock_region(struct panfrost_device *pfdev, u32 as_nr,
			u64 iova, size_t size)
{
	u8 region_width;
	u64 region = iova & PAGE_MASK;
	/*
	 * fls returns:
	 * 1 .. 32
	 *
	 * 10 + fls(num_pages)
	 * results in the range (11 .. 42)
	 */

	size = round_up(size, PAGE_SIZE);

	region_width = 10 + fls(size >> PAGE_SHIFT);
	if ((size >> PAGE_SHIFT) != (1ul << (region_width - 11))) {
		/* not pow2, so must go up to the next pow2 */
		region_width += 1;
	}
	region |= region_width;

	/* Lock the region that needs to be updated */
	mmu_write(pfdev, AS_LOCKADDR_LO(as_nr), region & 0xFFFFFFFFUL);
	mmu_write(pfdev, AS_LOCKADDR_HI(as_nr), (region >> 32) & 0xFFFFFFFFUL);
	write_cmd(pfdev, as_nr, AS_COMMAND_LOCK);
}


static int mmu_hw_do_operation(struct panfrost_device *pfdev, u32 as_nr,
		u64 iova, size_t size, u32 op)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pfdev->hwaccess_lock, flags);

	if (op != AS_COMMAND_UNLOCK)
		lock_region(pfdev, as_nr, iova, size);

	/* Run the MMU operation */
	write_cmd(pfdev, as_nr, op);

	/* Wait for the flush to complete */
	ret = wait_ready(pfdev, as_nr);

	spin_unlock_irqrestore(&pfdev->hwaccess_lock, flags);

	return ret;
}

void panfrost_mmu_enable(struct panfrost_device *pfdev, u32 as_nr)
{
	struct io_pgtable_cfg *cfg = &pfdev->mmu->pgtbl_cfg;
	u64 transtab = cfg->arm_mali_lpae_cfg.transtab;
	u64 memattr = cfg->arm_mali_lpae_cfg.memattr;

	mmu_write(pfdev, MMU_INT_CLEAR, ~0);
	mmu_write(pfdev, MMU_INT_MASK, ~0);

	mmu_write(pfdev, AS_TRANSTAB_LO(as_nr), transtab & 0xffffffffUL);
	mmu_write(pfdev, AS_TRANSTAB_HI(as_nr), transtab >> 32);

	/* Need to revisit mem attrs.
	 * NC is the default, Mali driver is inner WT.
	 */
	mmu_write(pfdev, AS_MEMATTR_LO(as_nr), memattr & 0xffffffffUL);
	mmu_write(pfdev, AS_MEMATTR_HI(as_nr), memattr >> 32);

	write_cmd(pfdev, as_nr, AS_COMMAND_UPDATE);
}

static void mmu_disable(struct panfrost_device *pfdev, u32 as_nr)
{
	mmu_write(pfdev, AS_TRANSTAB_LO(as_nr), 0);
	mmu_write(pfdev, AS_TRANSTAB_HI(as_nr), 0);

	mmu_write(pfdev, AS_MEMATTR_LO(as_nr), 0);
	mmu_write(pfdev, AS_MEMATTR_HI(as_nr), 0);

	write_cmd(pfdev, as_nr, AS_COMMAND_UPDATE);
}

static size_t get_pgsize(u64 addr, size_t size)
{
	if (addr & (SZ_2M - 1) || size < SZ_2M)
		return SZ_4K;

	return SZ_2M;
}

int panfrost_mmu_map(struct panfrost_gem_object *bo)
{
	struct drm_gem_object *obj = &bo->base.base;
	struct panfrost_device *pfdev = to_panfrost_device(obj->dev);
	struct io_pgtable_ops *ops = pfdev->mmu->pgtbl_ops;
	u64 iova = bo->node.start << PAGE_SHIFT;
	unsigned int count;
	struct scatterlist *sgl;
	struct sg_table *sgt;
	int ret;

	if (WARN_ON(bo->is_mapped))
		return 0;

	sgt = drm_gem_shmem_get_pages_sgt(obj);
	if (WARN_ON(IS_ERR(sgt)))
		return PTR_ERR(sgt);

	ret = pm_runtime_get_sync(pfdev->dev);
	if (ret < 0)
		return ret;

	mutex_lock(&pfdev->mmu->lock);

	for_each_sg(sgt->sgl, sgl, sgt->nents, count) {
		unsigned long paddr = sg_dma_address(sgl);
		size_t len = sg_dma_len(sgl);

		dev_dbg(pfdev->dev, "map: iova=%llx, paddr=%lx, len=%zx", iova, paddr, len);

		while (len) {
			size_t pgsize = get_pgsize(iova | paddr, len);

			ops->map(ops, iova, paddr, pgsize, IOMMU_WRITE | IOMMU_READ);
			iova += pgsize;
			paddr += pgsize;
			len -= pgsize;
		}
	}

	mmu_hw_do_operation(pfdev, 0, bo->node.start << PAGE_SHIFT,
			    bo->node.size << PAGE_SHIFT, AS_COMMAND_FLUSH_PT);

	mutex_unlock(&pfdev->mmu->lock);

	pm_runtime_mark_last_busy(pfdev->dev);
	pm_runtime_put_autosuspend(pfdev->dev);
	bo->is_mapped = true;

	return 0;
}

void panfrost_mmu_unmap(struct panfrost_gem_object *bo)
{
	struct drm_gem_object *obj = &bo->base.base;
	struct panfrost_device *pfdev = to_panfrost_device(obj->dev);
	struct io_pgtable_ops *ops = pfdev->mmu->pgtbl_ops;
	u64 iova = bo->node.start << PAGE_SHIFT;
	size_t len = bo->node.size << PAGE_SHIFT;
	size_t unmapped_len = 0;
	int ret;

	if (WARN_ON(!bo->is_mapped))
		return;

	dev_dbg(pfdev->dev, "unmap: iova=%llx, len=%zx", iova, len);

	ret = pm_runtime_get_sync(pfdev->dev);
	if (ret < 0)
		return;

	mutex_lock(&pfdev->mmu->lock);

	while (unmapped_len < len) {
		size_t unmapped_page;
		size_t pgsize = get_pgsize(iova, len - unmapped_len);

		unmapped_page = ops->unmap(ops, iova, pgsize);
		if (!unmapped_page)
			break;

		iova += unmapped_page;
		unmapped_len += unmapped_page;
	}

	mmu_hw_do_operation(pfdev, 0, bo->node.start << PAGE_SHIFT,
			    bo->node.size << PAGE_SHIFT, AS_COMMAND_FLUSH_PT);

	mutex_unlock(&pfdev->mmu->lock);

	pm_runtime_mark_last_busy(pfdev->dev);
	pm_runtime_put_autosuspend(pfdev->dev);
	bo->is_mapped = false;
}

static void mmu_tlb_inv_context_s1(void *cookie)
{
	struct panfrost_device *pfdev = cookie;

	mmu_hw_do_operation(pfdev, 0, 0, ~0UL, AS_COMMAND_FLUSH_MEM);
}

static void mmu_tlb_inv_range_nosync(unsigned long iova, size_t size,
				     size_t granule, bool leaf, void *cookie)
{}

static void mmu_tlb_sync_context(void *cookie)
{
	//struct panfrost_device *pfdev = cookie;
	// TODO: Wait 1000 GPU cycles for HW_ISSUE_6367/T60X
}

static const struct iommu_gather_ops mmu_tlb_ops = {
	.tlb_flush_all	= mmu_tlb_inv_context_s1,
	.tlb_add_flush	= mmu_tlb_inv_range_nosync,
	.tlb_sync	= mmu_tlb_sync_context,
};

static const char *access_type_name(struct panfrost_device *pfdev,
		u32 fault_status)
{
	switch (fault_status & AS_FAULTSTATUS_ACCESS_TYPE_MASK) {
	case AS_FAULTSTATUS_ACCESS_TYPE_ATOMIC:
		if (panfrost_has_hw_feature(pfdev, HW_FEATURE_AARCH64_MMU))
			return "ATOMIC";
		else
			return "UNKNOWN";
	case AS_FAULTSTATUS_ACCESS_TYPE_READ:
		return "READ";
	case AS_FAULTSTATUS_ACCESS_TYPE_WRITE:
		return "WRITE";
	case AS_FAULTSTATUS_ACCESS_TYPE_EX:
		return "EXECUTE";
	default:
		WARN_ON(1);
		return NULL;
	}
}

static irqreturn_t panfrost_mmu_irq_handler(int irq, void *data)
{
	struct panfrost_device *pfdev = data;
	u32 status = mmu_read(pfdev, MMU_INT_STAT);
	int i;

	if (!status)
		return IRQ_NONE;

	dev_err(pfdev->dev, "mmu irq status=%x\n", status);

	for (i = 0; status; i++) {
		u32 mask = BIT(i) | BIT(i + 16);
		u64 addr;
		u32 fault_status;
		u32 exception_type;
		u32 access_type;
		u32 source_id;

		if (!(status & mask))
			continue;

		fault_status = mmu_read(pfdev, AS_FAULTSTATUS(i));
		addr = mmu_read(pfdev, AS_FAULTADDRESS_LO(i));
		addr |= (u64)mmu_read(pfdev, AS_FAULTADDRESS_HI(i)) << 32;

		/* decode the fault status */
		exception_type = fault_status & 0xFF;
		access_type = (fault_status >> 8) & 0x3;
		source_id = (fault_status >> 16);

		/* terminal fault, print info about the fault */
		dev_err(pfdev->dev,
			"Unhandled Page fault in AS%d at VA 0x%016llX\n"
			"Reason: %s\n"
			"raw fault status: 0x%X\n"
			"decoded fault status: %s\n"
			"exception type 0x%X: %s\n"
			"access type 0x%X: %s\n"
			"source id 0x%X\n",
			i, addr,
			"TODO",
			fault_status,
			(fault_status & (1 << 10) ? "DECODER FAULT" : "SLAVE FAULT"),
			exception_type, panfrost_exception_name(pfdev, exception_type),
			access_type, access_type_name(pfdev, fault_status),
			source_id);

		mmu_write(pfdev, MMU_INT_CLEAR, mask);

		status &= ~mask;
	}

	return IRQ_HANDLED;
};

int panfrost_mmu_init(struct panfrost_device *pfdev)
{
	struct io_pgtable_ops *pgtbl_ops;
	int err, irq;

	pfdev->mmu = devm_kzalloc(pfdev->dev, sizeof(*pfdev->mmu), GFP_KERNEL);
	if (!pfdev->mmu)
		return -ENOMEM;

	mutex_init(&pfdev->mmu->lock);

	irq = platform_get_irq_byname(to_platform_device(pfdev->dev), "mmu");
	if (irq <= 0)
		return -ENODEV;

	err = devm_request_irq(pfdev->dev, irq, panfrost_mmu_irq_handler,
			       IRQF_SHARED, "mmu", pfdev);

	if (err) {
		dev_err(pfdev->dev, "failed to request mmu irq");
		return err;
	}
	mmu_write(pfdev, MMU_INT_CLEAR, ~0);
	mmu_write(pfdev, MMU_INT_MASK, ~0);

	pfdev->mmu->pgtbl_cfg = (struct io_pgtable_cfg) {
		.pgsize_bitmap	= SZ_4K | SZ_2M,
		.ias		= FIELD_GET(0xff, pfdev->features.mmu_features),
		.oas		= FIELD_GET(0xff00, pfdev->features.mmu_features),
		.tlb		= &mmu_tlb_ops,
		.iommu_dev	= pfdev->dev,
	};

	pgtbl_ops = alloc_io_pgtable_ops(ARM_MALI_LPAE, &pfdev->mmu->pgtbl_cfg,
					 pfdev);
	if (!pgtbl_ops)
		return -ENOMEM;

	pfdev->mmu->pgtbl_ops = pgtbl_ops;

	panfrost_mmu_enable(pfdev, 0);

	return 0;
}

void panfrost_mmu_fini(struct panfrost_device *pfdev)
{
	mmu_write(pfdev, MMU_INT_MASK, 0);
	mmu_disable(pfdev, 0);

	free_io_pgtable_ops(pfdev->mmu->pgtbl_ops);
}
