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
#include "coresight-priv.h"
#include "coresight-tmc.h"

void tmc_etb_enable_hw(struct tmc_drvdata *drvdata)
{
	/* Zero out the memory to help with debug */
	memset(drvdata->buf, 0, drvdata->size);

	CS_UNLOCK(drvdata->base);

	/* Wait for TMCSReady bit to be set */
	tmc_wait_for_tmcready(drvdata);

	writel_relaxed(TMC_MODE_CIRCULAR_BUFFER, drvdata->base + TMC_MODE);
	writel_relaxed(TMC_FFCR_EN_FMT | TMC_FFCR_EN_TI |
		       TMC_FFCR_FON_FLIN | TMC_FFCR_FON_TRIG_EVT |
		       TMC_FFCR_TRIGON_TRIGIN,
		       drvdata->base + TMC_FFCR);

	writel_relaxed(drvdata->trigger_cntr, drvdata->base + TMC_TRG);
	tmc_enable_hw(drvdata);

	CS_LOCK(drvdata->base);
}

static void tmc_etb_dump_hw(struct tmc_drvdata *drvdata)
{
	enum tmc_mem_intf_width memwidth;
	u8 memwords;
	char *bufp;
	u32 read_data;
	int i;

	memwidth = BMVAL(readl_relaxed(drvdata->base + CORESIGHT_DEVID), 8, 10);
	if (memwidth == TMC_MEM_INTF_WIDTH_32BITS)
		memwords = 1;
	else if (memwidth == TMC_MEM_INTF_WIDTH_64BITS)
		memwords = 2;
	else if (memwidth == TMC_MEM_INTF_WIDTH_128BITS)
		memwords = 4;
	else
		memwords = 8;

	bufp = drvdata->buf;
	while (1) {
		for (i = 0; i < memwords; i++) {
			read_data = readl_relaxed(drvdata->base + TMC_RRD);
			if (read_data == 0xFFFFFFFF)
				return;
			memcpy(bufp, &read_data, 4);
			bufp += 4;
		}
	}
}

static void tmc_etb_disable_hw(struct tmc_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	tmc_flush_and_stop(drvdata);
	tmc_etb_dump_hw(drvdata);
	tmc_disable_hw(drvdata);

	CS_LOCK(drvdata->base);
}

static void tmc_etf_enable_hw(struct tmc_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	/* Wait for TMCSReady bit to be set */
	tmc_wait_for_tmcready(drvdata);

	writel_relaxed(TMC_MODE_HARDWARE_FIFO, drvdata->base + TMC_MODE);
	writel_relaxed(TMC_FFCR_EN_FMT | TMC_FFCR_EN_TI,
		       drvdata->base + TMC_FFCR);
	writel_relaxed(0x0, drvdata->base + TMC_BUFWM);
	tmc_enable_hw(drvdata);

	CS_LOCK(drvdata->base);
}

static void tmc_etf_disable_hw(struct tmc_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	tmc_flush_and_stop(drvdata);
	tmc_disable_hw(drvdata);

	CS_LOCK(drvdata->base);
}

static int tmc_enable_etf_sink(struct coresight_device *csdev, u32 mode)
{
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return -EBUSY;
	}

	tmc_etb_enable_hw(drvdata);
	drvdata->enable = true;
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	dev_info(drvdata->dev, "TMC-ETB/ETF enabled\n");
	return 0;
}

static void tmc_disable_etf_sink(struct coresight_device *csdev)
{
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return;
	}

	tmc_etb_disable_hw(drvdata);
	drvdata->enable = false;
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	dev_info(drvdata->dev, "TMC-ETB/ETF disabled\n");
}

static int tmc_enable_etf_link(struct coresight_device *csdev,
			       int inport, int outport)
{
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return -EBUSY;
	}

	tmc_etf_enable_hw(drvdata);
	drvdata->enable = true;
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	dev_info(drvdata->dev, "TMC-ETF enabled\n");
	return 0;
}

static void tmc_disable_etf_link(struct coresight_device *csdev,
				 int inport, int outport)
{
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return;
	}

	tmc_etf_disable_hw(drvdata);
	drvdata->enable = false;
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	dev_info(drvdata->dev, "TMC disabled\n");
}

static const struct coresight_ops_sink tmc_etf_sink_ops = {
	.enable		= tmc_enable_etf_sink,
	.disable	= tmc_disable_etf_sink,
};

static const struct coresight_ops_link tmc_etf_link_ops = {
	.enable		= tmc_enable_etf_link,
	.disable	= tmc_disable_etf_link,
};

const struct coresight_ops tmc_etb_cs_ops = {
	.sink_ops	= &tmc_etf_sink_ops,
};

const struct coresight_ops tmc_etf_cs_ops = {
	.sink_ops	= &tmc_etf_sink_ops,
	.link_ops	= &tmc_etf_link_ops,
};

int tmc_read_prepare_etb(struct tmc_drvdata *drvdata)
{
	enum tmc_mode mode;
	int ret = 0;
	unsigned long flags;

	/* config types are set a boot time and never change */
	if (WARN_ON_ONCE(drvdata->config_type != TMC_CONFIG_TYPE_ETB &&
			 drvdata->config_type != TMC_CONFIG_TYPE_ETF))
		return -EINVAL;

	spin_lock_irqsave(&drvdata->spinlock, flags);

	/* There is no point in reading a TMC in HW FIFO mode */
	mode = readl_relaxed(drvdata->base + TMC_MODE);
	if (mode != TMC_MODE_CIRCULAR_BUFFER) {
		ret = -EINVAL;
		goto out;
	}

	/* Disable the TMC if need be */
	if (drvdata->enable)
		tmc_etb_disable_hw(drvdata);

	drvdata->reading = true;
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	return ret;
}

int tmc_read_unprepare_etb(struct tmc_drvdata *drvdata)
{
	enum tmc_mode mode;
	unsigned long flags;

	/* config types are set a boot time and never change */
	if (WARN_ON_ONCE(drvdata->config_type != TMC_CONFIG_TYPE_ETB &&
			 drvdata->config_type != TMC_CONFIG_TYPE_ETF))
		return -EINVAL;

	spin_lock_irqsave(&drvdata->spinlock, flags);

	/* There is no point in reading a TMC in HW FIFO mode */
	mode = readl_relaxed(drvdata->base + TMC_MODE);
	if (mode != TMC_MODE_CIRCULAR_BUFFER) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return -EINVAL;
	}

	/* Re-enable the TMC if need be */
	if (drvdata->enable)
		tmc_etb_enable_hw(drvdata);

	drvdata->reading = false;
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	return 0;
}
