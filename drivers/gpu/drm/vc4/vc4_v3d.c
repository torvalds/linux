// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_irq.h>

#include "vc4_drv.h"
#include "vc4_regs.h"

static const struct debugfs_reg32 v3d_regs[] = {
	VC4_REG32(V3D_IDENT0),
	VC4_REG32(V3D_IDENT1),
	VC4_REG32(V3D_IDENT2),
	VC4_REG32(V3D_SCRATCH),
	VC4_REG32(V3D_L2CACTL),
	VC4_REG32(V3D_SLCACTL),
	VC4_REG32(V3D_INTCTL),
	VC4_REG32(V3D_INTENA),
	VC4_REG32(V3D_INTDIS),
	VC4_REG32(V3D_CT0CS),
	VC4_REG32(V3D_CT1CS),
	VC4_REG32(V3D_CT0EA),
	VC4_REG32(V3D_CT1EA),
	VC4_REG32(V3D_CT0CA),
	VC4_REG32(V3D_CT1CA),
	VC4_REG32(V3D_CT00RA0),
	VC4_REG32(V3D_CT01RA0),
	VC4_REG32(V3D_CT0LC),
	VC4_REG32(V3D_CT1LC),
	VC4_REG32(V3D_CT0PC),
	VC4_REG32(V3D_CT1PC),
	VC4_REG32(V3D_PCS),
	VC4_REG32(V3D_BFC),
	VC4_REG32(V3D_RFC),
	VC4_REG32(V3D_BPCA),
	VC4_REG32(V3D_BPCS),
	VC4_REG32(V3D_BPOA),
	VC4_REG32(V3D_BPOS),
	VC4_REG32(V3D_BXCF),
	VC4_REG32(V3D_SQRSV0),
	VC4_REG32(V3D_SQRSV1),
	VC4_REG32(V3D_SQCNTL),
	VC4_REG32(V3D_SRQPC),
	VC4_REG32(V3D_SRQUA),
	VC4_REG32(V3D_SRQUL),
	VC4_REG32(V3D_SRQCS),
	VC4_REG32(V3D_VPACNTL),
	VC4_REG32(V3D_VPMBASE),
	VC4_REG32(V3D_PCTRC),
	VC4_REG32(V3D_PCTRE),
	VC4_REG32(V3D_PCTR(0)),
	VC4_REG32(V3D_PCTRS(0)),
	VC4_REG32(V3D_PCTR(1)),
	VC4_REG32(V3D_PCTRS(1)),
	VC4_REG32(V3D_PCTR(2)),
	VC4_REG32(V3D_PCTRS(2)),
	VC4_REG32(V3D_PCTR(3)),
	VC4_REG32(V3D_PCTRS(3)),
	VC4_REG32(V3D_PCTR(4)),
	VC4_REG32(V3D_PCTRS(4)),
	VC4_REG32(V3D_PCTR(5)),
	VC4_REG32(V3D_PCTRS(5)),
	VC4_REG32(V3D_PCTR(6)),
	VC4_REG32(V3D_PCTRS(6)),
	VC4_REG32(V3D_PCTR(7)),
	VC4_REG32(V3D_PCTRS(7)),
	VC4_REG32(V3D_PCTR(8)),
	VC4_REG32(V3D_PCTRS(8)),
	VC4_REG32(V3D_PCTR(9)),
	VC4_REG32(V3D_PCTRS(9)),
	VC4_REG32(V3D_PCTR(10)),
	VC4_REG32(V3D_PCTRS(10)),
	VC4_REG32(V3D_PCTR(11)),
	VC4_REG32(V3D_PCTRS(11)),
	VC4_REG32(V3D_PCTR(12)),
	VC4_REG32(V3D_PCTRS(12)),
	VC4_REG32(V3D_PCTR(13)),
	VC4_REG32(V3D_PCTRS(13)),
	VC4_REG32(V3D_PCTR(14)),
	VC4_REG32(V3D_PCTRS(14)),
	VC4_REG32(V3D_PCTR(15)),
	VC4_REG32(V3D_PCTRS(15)),
	VC4_REG32(V3D_DBGE),
	VC4_REG32(V3D_FDBGO),
	VC4_REG32(V3D_FDBGB),
	VC4_REG32(V3D_FDBGR),
	VC4_REG32(V3D_FDBGS),
	VC4_REG32(V3D_ERRSTAT),
};

static int vc4_v3d_debugfs_ident(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int ret = vc4_v3d_pm_get(vc4);

	if (ret == 0) {
		uint32_t ident1 = V3D_READ(V3D_IDENT1);
		uint32_t nslc = VC4_GET_FIELD(ident1, V3D_IDENT1_NSLC);
		uint32_t tups = VC4_GET_FIELD(ident1, V3D_IDENT1_TUPS);
		uint32_t qups = VC4_GET_FIELD(ident1, V3D_IDENT1_QUPS);

		seq_printf(m, "Revision:   %d\n",
			   VC4_GET_FIELD(ident1, V3D_IDENT1_REV));
		seq_printf(m, "Slices:     %d\n", nslc);
		seq_printf(m, "TMUs:       %d\n", nslc * tups);
		seq_printf(m, "QPUs:       %d\n", nslc * qups);
		seq_printf(m, "Semaphores: %d\n",
			   VC4_GET_FIELD(ident1, V3D_IDENT1_NSEM));
		vc4_v3d_pm_put(vc4);
	}

	return 0;
}

/**
 * Wraps pm_runtime_get_sync() in a refcount, so that we can reliably
 * get the pm_runtime refcount to 0 in vc4_reset().
 */
int
vc4_v3d_pm_get(struct vc4_dev *vc4)
{
	mutex_lock(&vc4->power_lock);
	if (vc4->power_refcount++ == 0) {
		int ret = pm_runtime_get_sync(&vc4->v3d->pdev->dev);

		if (ret < 0) {
			vc4->power_refcount--;
			mutex_unlock(&vc4->power_lock);
			return ret;
		}
	}
	mutex_unlock(&vc4->power_lock);

	return 0;
}

void
vc4_v3d_pm_put(struct vc4_dev *vc4)
{
	mutex_lock(&vc4->power_lock);
	if (--vc4->power_refcount == 0) {
		pm_runtime_mark_last_busy(&vc4->v3d->pdev->dev);
		pm_runtime_put_autosuspend(&vc4->v3d->pdev->dev);
	}
	mutex_unlock(&vc4->power_lock);
}

static void vc4_v3d_init_hw(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	/* Take all the memory that would have been reserved for user
	 * QPU programs, since we don't have an interface for running
	 * them, anyway.
	 */
	V3D_WRITE(V3D_VPMBASE, 0);
}

int vc4_v3d_get_bin_slot(struct vc4_dev *vc4)
{
	struct drm_device *dev = vc4->dev;
	unsigned long irqflags;
	int slot;
	uint64_t seqno = 0;
	struct vc4_exec_info *exec;

try_again:
	spin_lock_irqsave(&vc4->job_lock, irqflags);
	slot = ffs(~vc4->bin_alloc_used);
	if (slot != 0) {
		/* Switch from ffs() bit index to a 0-based index. */
		slot--;
		vc4->bin_alloc_used |= BIT(slot);
		spin_unlock_irqrestore(&vc4->job_lock, irqflags);
		return slot;
	}

	/* Couldn't find an open slot.  Wait for render to complete
	 * and try again.
	 */
	exec = vc4_last_render_job(vc4);
	if (exec)
		seqno = exec->seqno;
	spin_unlock_irqrestore(&vc4->job_lock, irqflags);

	if (seqno) {
		int ret = vc4_wait_for_seqno(dev, seqno, ~0ull, true);

		if (ret == 0)
			goto try_again;

		return ret;
	}

	return -ENOMEM;
}

/**
 * bin_bo_alloc() - allocates the memory that will be used for
 * tile binning.
 *
 * The binner has a limitation that the addresses in the tile state
 * buffer that point into the tile alloc buffer or binner overflow
 * memory only have 28 bits (256MB), and the top 4 on the bus for
 * tile alloc references end up coming from the tile state buffer's
 * address.
 *
 * To work around this, we allocate a single large buffer while V3D is
 * in use, make sure that it has the top 4 bits constant across its
 * entire extent, and then put the tile state, tile alloc, and binner
 * overflow memory inside that buffer.
 *
 * This creates a limitation where we may not be able to execute a job
 * if it doesn't fit within the buffer that we allocated up front.
 * However, it turns out that 16MB is "enough for anybody", and
 * real-world applications run into allocation failures from the
 * overall CMA pool before they make scenes complicated enough to run
 * out of bin space.
 */
static int bin_bo_alloc(struct vc4_dev *vc4)
{
	struct vc4_v3d *v3d = vc4->v3d;
	uint32_t size = 16 * 1024 * 1024;
	int ret = 0;
	struct list_head list;

	if (!v3d)
		return -ENODEV;

	/* We may need to try allocating more than once to get a BO
	 * that doesn't cross 256MB.  Track the ones we've allocated
	 * that failed so far, so that we can free them when we've got
	 * one that succeeded (if we freed them right away, our next
	 * allocation would probably be the same chunk of memory).
	 */
	INIT_LIST_HEAD(&list);

	while (true) {
		struct vc4_bo *bo = vc4_bo_create(vc4->dev, size, true,
						  VC4_BO_TYPE_BIN);

		if (IS_ERR(bo)) {
			ret = PTR_ERR(bo);

			dev_err(&v3d->pdev->dev,
				"Failed to allocate memory for tile binning: "
				"%d. You may need to enable CMA or give it "
				"more memory.",
				ret);
			break;
		}

		/* Check if this BO won't trigger the addressing bug. */
		if ((bo->base.paddr & 0xf0000000) ==
		    ((bo->base.paddr + bo->base.base.size - 1) & 0xf0000000)) {
			vc4->bin_bo = bo;

			/* Set up for allocating 512KB chunks of
			 * binner memory.  The biggest allocation we
			 * need to do is for the initial tile alloc +
			 * tile state buffer.  We can render to a
			 * maximum of ((2048*2048) / (32*32) = 4096
			 * tiles in a frame (until we do floating
			 * point rendering, at which point it would be
			 * 8192).  Tile state is 48b/tile (rounded to
			 * a page), and tile alloc is 32b/tile
			 * (rounded to a page), plus a page of extra,
			 * for a total of 320kb for our worst-case.
			 * We choose 512kb so that it divides evenly
			 * into our 16MB, and the rest of the 512kb
			 * will be used as storage for the overflow
			 * from the initial 32b CL per bin.
			 */
			vc4->bin_alloc_size = 512 * 1024;
			vc4->bin_alloc_used = 0;
			vc4->bin_alloc_overflow = 0;
			WARN_ON_ONCE(sizeof(vc4->bin_alloc_used) * 8 !=
				     bo->base.base.size / vc4->bin_alloc_size);

			kref_init(&vc4->bin_bo_kref);

			/* Enable the out-of-memory interrupt to set our
			 * newly-allocated binner BO, potentially from an
			 * already-pending-but-masked interrupt.
			 */
			V3D_WRITE(V3D_INTENA, V3D_INT_OUTOMEM);

			break;
		}

		/* Put it on the list to free later, and try again. */
		list_add(&bo->unref_head, &list);
	}

	/* Free all the BOs we allocated but didn't choose. */
	while (!list_empty(&list)) {
		struct vc4_bo *bo = list_last_entry(&list,
						    struct vc4_bo, unref_head);

		list_del(&bo->unref_head);
		drm_gem_object_put(&bo->base.base);
	}

	return ret;
}

int vc4_v3d_bin_bo_get(struct vc4_dev *vc4, bool *used)
{
	int ret = 0;

	mutex_lock(&vc4->bin_bo_lock);

	if (used && *used)
		goto complete;

	if (vc4->bin_bo)
		kref_get(&vc4->bin_bo_kref);
	else
		ret = bin_bo_alloc(vc4);

	if (ret == 0 && used)
		*used = true;

complete:
	mutex_unlock(&vc4->bin_bo_lock);

	return ret;
}

static void bin_bo_release(struct kref *ref)
{
	struct vc4_dev *vc4 = container_of(ref, struct vc4_dev, bin_bo_kref);

	if (WARN_ON_ONCE(!vc4->bin_bo))
		return;

	drm_gem_object_put(&vc4->bin_bo->base.base);
	vc4->bin_bo = NULL;
}

void vc4_v3d_bin_bo_put(struct vc4_dev *vc4)
{
	mutex_lock(&vc4->bin_bo_lock);
	kref_put(&vc4->bin_bo_kref, bin_bo_release);
	mutex_unlock(&vc4->bin_bo_lock);
}

#ifdef CONFIG_PM
static int vc4_v3d_runtime_suspend(struct device *dev)
{
	struct vc4_v3d *v3d = dev_get_drvdata(dev);
	struct vc4_dev *vc4 = v3d->vc4;

	vc4_irq_uninstall(vc4->dev);

	clk_disable_unprepare(v3d->clk);

	return 0;
}

static int vc4_v3d_runtime_resume(struct device *dev)
{
	struct vc4_v3d *v3d = dev_get_drvdata(dev);
	struct vc4_dev *vc4 = v3d->vc4;
	int ret;

	ret = clk_prepare_enable(v3d->clk);
	if (ret != 0)
		return ret;

	vc4_v3d_init_hw(vc4->dev);

	/* We disabled the IRQ as part of vc4_irq_uninstall in suspend. */
	enable_irq(vc4->dev->irq);
	vc4_irq_postinstall(vc4->dev);

	return 0;
}
#endif

static int vc4_v3d_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	struct vc4_v3d *v3d = NULL;
	int ret;

	v3d = devm_kzalloc(&pdev->dev, sizeof(*v3d), GFP_KERNEL);
	if (!v3d)
		return -ENOMEM;

	dev_set_drvdata(dev, v3d);

	v3d->pdev = pdev;

	v3d->regs = vc4_ioremap_regs(pdev, 0);
	if (IS_ERR(v3d->regs))
		return PTR_ERR(v3d->regs);
	v3d->regset.base = v3d->regs;
	v3d->regset.regs = v3d_regs;
	v3d->regset.nregs = ARRAY_SIZE(v3d_regs);

	vc4->v3d = v3d;
	v3d->vc4 = vc4;

	v3d->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(v3d->clk)) {
		int ret = PTR_ERR(v3d->clk);

		if (ret == -ENOENT) {
			/* bcm2835 didn't have a clock reference in the DT. */
			ret = 0;
			v3d->clk = NULL;
		} else {
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Failed to get V3D clock: %d\n",
					ret);
			return ret;
		}
	}

	if (V3D_READ(V3D_IDENT0) != V3D_EXPECTED_IDENT0) {
		DRM_ERROR("V3D_IDENT0 read 0x%08x instead of 0x%08x\n",
			  V3D_READ(V3D_IDENT0), V3D_EXPECTED_IDENT0);
		return -EINVAL;
	}

	ret = clk_prepare_enable(v3d->clk);
	if (ret != 0)
		return ret;

	/* Reset the binner overflow address/size at setup, to be sure
	 * we don't reuse an old one.
	 */
	V3D_WRITE(V3D_BPOA, 0);
	V3D_WRITE(V3D_BPOS, 0);

	vc4_v3d_init_hw(drm);

	ret = drm_irq_install(drm, platform_get_irq(pdev, 0));
	if (ret) {
		DRM_ERROR("Failed to install IRQ handler\n");
		return ret;
	}

	pm_runtime_set_active(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 40); /* a little over 2 frames. */
	pm_runtime_enable(dev);

	vc4_debugfs_add_file(drm, "v3d_ident", vc4_v3d_debugfs_ident, NULL);
	vc4_debugfs_add_regset32(drm, "v3d_regs", &v3d->regset);

	return 0;
}

static void vc4_v3d_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dev *vc4 = to_vc4_dev(drm);

	pm_runtime_disable(dev);

	drm_irq_uninstall(drm);

	/* Disable the binner's overflow memory address, so the next
	 * driver probe (if any) doesn't try to reuse our old
	 * allocation.
	 */
	V3D_WRITE(V3D_BPOA, 0);
	V3D_WRITE(V3D_BPOS, 0);

	vc4->v3d = NULL;
}

static const struct dev_pm_ops vc4_v3d_pm_ops = {
	SET_RUNTIME_PM_OPS(vc4_v3d_runtime_suspend, vc4_v3d_runtime_resume, NULL)
};

static const struct component_ops vc4_v3d_ops = {
	.bind   = vc4_v3d_bind,
	.unbind = vc4_v3d_unbind,
};

static int vc4_v3d_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &vc4_v3d_ops);
}

static int vc4_v3d_dev_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vc4_v3d_ops);
	return 0;
}

const struct of_device_id vc4_v3d_dt_match[] = {
	{ .compatible = "brcm,bcm2835-v3d" },
	{ .compatible = "brcm,cygnus-v3d" },
	{ .compatible = "brcm,vc4-v3d" },
	{}
};

struct platform_driver vc4_v3d_driver = {
	.probe = vc4_v3d_dev_probe,
	.remove = vc4_v3d_dev_remove,
	.driver = {
		.name = "vc4_v3d",
		.of_match_table = vc4_v3d_dt_match,
		.pm = &vc4_v3d_pm_ops,
	},
};
