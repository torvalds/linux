/*
 * Copyright(C) 2016 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/coresight.h>
#include <linux/dma-mapping.h>
#include "coresight-priv.h"
#include "coresight-tmc.h"

void tmc_etr_enable_hw(struct tmc_drvdata *drvdata)
{
	u32 axictl;

	/* Zero out the memory to help with debug */
	memset(drvdata->vaddr, 0, drvdata->size);

	CS_UNLOCK(drvdata->base);

	/* Wait for TMCSReady bit to be set */
	tmc_wait_for_tmcready(drvdata);

	writel_relaxed(drvdata->size / 4, drvdata->base + TMC_RSZ);
	writel_relaxed(TMC_MODE_CIRCULAR_BUFFER, drvdata->base + TMC_MODE);

	axictl = readl_relaxed(drvdata->base + TMC_AXICTL);
	axictl |= TMC_AXICTL_WR_BURST_16;
	writel_relaxed(axictl, drvdata->base + TMC_AXICTL);
	axictl &= ~TMC_AXICTL_SCT_GAT_MODE;
	writel_relaxed(axictl, drvdata->base + TMC_AXICTL);
	axictl = (axictl &
		  ~(TMC_AXICTL_PROT_CTL_B0 | TMC_AXICTL_PROT_CTL_B1)) |
		  TMC_AXICTL_PROT_CTL_B1;
	writel_relaxed(axictl, drvdata->base + TMC_AXICTL);

	writel_relaxed(drvdata->paddr, drvdata->base + TMC_DBALO);
	writel_relaxed(0x0, drvdata->base + TMC_DBAHI);
	writel_relaxed(TMC_FFCR_EN_FMT | TMC_FFCR_EN_TI |
		       TMC_FFCR_FON_FLIN | TMC_FFCR_FON_TRIG_EVT |
		       TMC_FFCR_TRIGON_TRIGIN,
		       drvdata->base + TMC_FFCR);
	writel_relaxed(drvdata->trigger_cntr, drvdata->base + TMC_TRG);
	tmc_enable_hw(drvdata);

	CS_LOCK(drvdata->base);
}

static void tmc_etr_dump_hw(struct tmc_drvdata *drvdata)
{
	u32 rwp, val;

	rwp = readl_relaxed(drvdata->base + TMC_RWP);
	val = readl_relaxed(drvdata->base + TMC_STS);

	/* How much memory do we still have */
	if (val & BIT(0))
		drvdata->buf = drvdata->vaddr + rwp - drvdata->paddr;
	else
		drvdata->buf = drvdata->vaddr;
}

static void tmc_etr_disable_hw(struct tmc_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	tmc_flush_and_stop(drvdata);
	/*
	 * When operating in sysFS mode the content of the buffer needs to be
	 * read before the TMC is disabled.
	 */
	if (local_read(&drvdata->mode) == CS_MODE_SYSFS)
		tmc_etr_dump_hw(drvdata);
	tmc_disable_hw(drvdata);

	CS_LOCK(drvdata->base);
}

static int tmc_enable_etr_sink_sysfs(struct coresight_device *csdev, u32 mode)
{
	int ret = 0;
	bool used = false;
	long val;
	unsigned long flags;
	void __iomem *vaddr = NULL;
	dma_addr_t paddr;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	 /* This shouldn't be happening */
	if (WARN_ON(mode != CS_MODE_SYSFS))
		return -EINVAL;

	/*
	 * If we don't have a buffer release the lock and allocate memory.
	 * Otherwise keep the lock and move along.
	 */
	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (!drvdata->vaddr) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);

		/*
		 * Contiguous  memory can't be allocated while a spinlock is
		 * held.  As such allocate memory here and free it if a buffer
		 * has already been allocated (from a previous session).
		 */
		vaddr = dma_alloc_coherent(drvdata->dev, drvdata->size,
					   &paddr, GFP_KERNEL);
		if (!vaddr)
			return -ENOMEM;

		/* Let's try again */
		spin_lock_irqsave(&drvdata->spinlock, flags);
	}

	if (drvdata->reading) {
		ret = -EBUSY;
		goto out;
	}

	val = local_xchg(&drvdata->mode, mode);
	/*
	 * In sysFS mode we can have multiple writers per sink.  Since this
	 * sink is already enabled no memory is needed and the HW need not be
	 * touched.
	 */
	if (val == CS_MODE_SYSFS)
		goto out;

	/*
	 * If drvdata::buf == NULL, use the memory allocated above.
	 * Otherwise a buffer still exists from a previous session, so
	 * simply use that.
	 */
	if (drvdata->buf == NULL) {
		used = true;
		drvdata->vaddr = vaddr;
		drvdata->paddr = paddr;
		drvdata->buf = drvdata->vaddr;
	}

	memset(drvdata->vaddr, 0, drvdata->size);

	tmc_etr_enable_hw(drvdata);
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	/* Free memory outside the spinlock if need be */
	if (!used && vaddr)
		dma_free_coherent(drvdata->dev, drvdata->size, vaddr, paddr);

	if (!ret)
		dev_info(drvdata->dev, "TMC-ETR enabled\n");

	return ret;
}

static int tmc_enable_etr_sink_perf(struct coresight_device *csdev, u32 mode)
{
	int ret = 0;
	long val;
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	 /* This shouldn't be happening */
	if (WARN_ON(mode != CS_MODE_PERF))
		return -EINVAL;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		ret = -EINVAL;
		goto out;
	}

	val = local_xchg(&drvdata->mode, mode);
	/*
	 * In Perf mode there can be only one writer per sink.  There
	 * is also no need to continue if the ETR is already operated
	 * from sysFS.
	 */
	if (val != CS_MODE_DISABLED) {
		ret = -EINVAL;
		goto out;
	}

	tmc_etr_enable_hw(drvdata);
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	return ret;
}

static int tmc_enable_etr_sink(struct coresight_device *csdev, u32 mode)
{
	switch (mode) {
	case CS_MODE_SYSFS:
		return tmc_enable_etr_sink_sysfs(csdev, mode);
	case CS_MODE_PERF:
		return tmc_enable_etr_sink_perf(csdev, mode);
	}

	/* We shouldn't be here */
	return -EINVAL;
}

static void tmc_disable_etr_sink(struct coresight_device *csdev)
{
	long val;
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return;
	}

	val = local_xchg(&drvdata->mode, CS_MODE_DISABLED);
	/* Disable the TMC only if it needs to */
	if (val != CS_MODE_DISABLED)
		tmc_etr_disable_hw(drvdata);

	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	dev_info(drvdata->dev, "TMC-ETR disabled\n");
}

static const struct coresight_ops_sink tmc_etr_sink_ops = {
	.enable		= tmc_enable_etr_sink,
	.disable	= tmc_disable_etr_sink,
};

const struct coresight_ops tmc_etr_cs_ops = {
	.sink_ops	= &tmc_etr_sink_ops,
};

int tmc_read_prepare_etr(struct tmc_drvdata *drvdata)
{
	int ret = 0;
	long val;
	unsigned long flags;

	/* config types are set a boot time and never change */
	if (WARN_ON_ONCE(drvdata->config_type != TMC_CONFIG_TYPE_ETR))
		return -EINVAL;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		ret = -EBUSY;
		goto out;
	}

	val = local_read(&drvdata->mode);
	/* Don't interfere if operated from Perf */
	if (val == CS_MODE_PERF) {
		ret = -EINVAL;
		goto out;
	}

	/* If drvdata::buf is NULL the trace data has been read already */
	if (drvdata->buf == NULL) {
		ret = -EINVAL;
		goto out;
	}

	/* Disable the TMC if need be */
	if (val == CS_MODE_SYSFS)
		tmc_etr_disable_hw(drvdata);

	drvdata->reading = true;
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	return ret;
}

int tmc_read_unprepare_etr(struct tmc_drvdata *drvdata)
{
	unsigned long flags;
	dma_addr_t paddr;
	void __iomem *vaddr = NULL;

	/* config types are set a boot time and never change */
	if (WARN_ON_ONCE(drvdata->config_type != TMC_CONFIG_TYPE_ETR))
		return -EINVAL;

	spin_lock_irqsave(&drvdata->spinlock, flags);

	/* RE-enable the TMC if need be */
	if (local_read(&drvdata->mode) == CS_MODE_SYSFS) {
		/*
		 * The trace run will continue with the same allocated trace
		 * buffer. As such zero-out the buffer so that we don't end
		 * up with stale data.
		 *
		 * Since the tracer is still enabled drvdata::buf
		 * can't be NULL.
		 */
		memset(drvdata->buf, 0, drvdata->size);
		tmc_etr_enable_hw(drvdata);
	} else {
		/*
		 * The ETR is not tracing and the buffer was just read.
		 * As such prepare to free the trace buffer.
		 */
		vaddr = drvdata->vaddr;
		paddr = drvdata->paddr;
		drvdata->buf = NULL;
	}

	drvdata->reading = false;
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	/* Free allocated memory out side of the spinlock */
	if (vaddr)
		dma_free_coherent(drvdata->dev, drvdata->size, vaddr, paddr);

	return 0;
}
