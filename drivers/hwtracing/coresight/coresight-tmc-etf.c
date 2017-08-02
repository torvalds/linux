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

#include <linux/circ_buf.h>
#include <linux/coresight.h>
#include <linux/perf_event.h>
#include <linux/slab.h>
#include "coresight-priv.h"
#include "coresight-tmc.h"

static void tmc_etb_enable_hw(struct tmc_drvdata *drvdata)
{
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
	bool lost = false;
	char *bufp;
	const u32 *barrier;
	u32 read_data, status;
	int i;

	/*
	 * Get a hold of the status register and see if a wrap around
	 * has occurred.
	 */
	status = readl_relaxed(drvdata->base + TMC_STS);
	if (status & TMC_STS_FULL)
		lost = true;

	bufp = drvdata->buf;
	drvdata->len = 0;
	barrier = barrier_pkt;
	while (1) {
		for (i = 0; i < drvdata->memwidth; i++) {
			read_data = readl_relaxed(drvdata->base + TMC_RRD);
			if (read_data == 0xFFFFFFFF)
				return;

			if (lost && *barrier) {
				read_data = *barrier;
				barrier++;
			}

			memcpy(bufp, &read_data, 4);
			bufp += 4;
			drvdata->len += 4;
		}
	}
}

static void tmc_etb_disable_hw(struct tmc_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	tmc_flush_and_stop(drvdata);
	/*
	 * When operating in sysFS mode the content of the buffer needs to be
	 * read before the TMC is disabled.
	 */
	if (drvdata->mode == CS_MODE_SYSFS)
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

static int tmc_enable_etf_sink_sysfs(struct coresight_device *csdev)
{
	int ret = 0;
	bool used = false;
	char *buf = NULL;
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	/*
	 * If we don't have a buffer release the lock and allocate memory.
	 * Otherwise keep the lock and move along.
	 */
	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (!drvdata->buf) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);

		/* Allocating the memory here while outside of the spinlock */
		buf = kzalloc(drvdata->size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		/* Let's try again */
		spin_lock_irqsave(&drvdata->spinlock, flags);
	}

	if (drvdata->reading) {
		ret = -EBUSY;
		goto out;
	}

	/*
	 * In sysFS mode we can have multiple writers per sink.  Since this
	 * sink is already enabled no memory is needed and the HW need not be
	 * touched.
	 */
	if (drvdata->mode == CS_MODE_SYSFS)
		goto out;

	/*
	 * If drvdata::buf isn't NULL, memory was allocated for a previous
	 * trace run but wasn't read.  If so simply zero-out the memory.
	 * Otherwise use the memory allocated above.
	 *
	 * The memory is freed when users read the buffer using the
	 * /dev/xyz.{etf|etb} interface.  See tmc_read_unprepare_etf() for
	 * details.
	 */
	if (drvdata->buf) {
		memset(drvdata->buf, 0, drvdata->size);
	} else {
		used = true;
		drvdata->buf = buf;
	}

	drvdata->mode = CS_MODE_SYSFS;
	tmc_etb_enable_hw(drvdata);
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	/* Free memory outside the spinlock if need be */
	if (!used)
		kfree(buf);

	return ret;
}

static int tmc_enable_etf_sink_perf(struct coresight_device *csdev)
{
	int ret = 0;
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * In Perf mode there can be only one writer per sink.  There
	 * is also no need to continue if the ETB/ETR is already operated
	 * from sysFS.
	 */
	if (drvdata->mode != CS_MODE_DISABLED) {
		ret = -EINVAL;
		goto out;
	}

	drvdata->mode = CS_MODE_PERF;
	tmc_etb_enable_hw(drvdata);
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	return ret;
}

static int tmc_enable_etf_sink(struct coresight_device *csdev, u32 mode)
{
	int ret;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	switch (mode) {
	case CS_MODE_SYSFS:
		ret = tmc_enable_etf_sink_sysfs(csdev);
		break;
	case CS_MODE_PERF:
		ret = tmc_enable_etf_sink_perf(csdev);
		break;
	/* We shouldn't be here */
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		return ret;

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

	/* Disable the TMC only if it needs to */
	if (drvdata->mode != CS_MODE_DISABLED) {
		tmc_etb_disable_hw(drvdata);
		drvdata->mode = CS_MODE_DISABLED;
	}

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
	drvdata->mode = CS_MODE_SYSFS;
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
	drvdata->mode = CS_MODE_DISABLED;
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	dev_info(drvdata->dev, "TMC-ETF disabled\n");
}

static void *tmc_alloc_etf_buffer(struct coresight_device *csdev, int cpu,
				  void **pages, int nr_pages, bool overwrite)
{
	int node;
	struct cs_buffers *buf;

	if (cpu == -1)
		cpu = smp_processor_id();
	node = cpu_to_node(cpu);

	/* Allocate memory structure for interaction with Perf */
	buf = kzalloc_node(sizeof(struct cs_buffers), GFP_KERNEL, node);
	if (!buf)
		return NULL;

	buf->snapshot = overwrite;
	buf->nr_pages = nr_pages;
	buf->data_pages = pages;

	return buf;
}

static void tmc_free_etf_buffer(void *config)
{
	struct cs_buffers *buf = config;

	kfree(buf);
}

static int tmc_set_etf_buffer(struct coresight_device *csdev,
			      struct perf_output_handle *handle,
			      void *sink_config)
{
	int ret = 0;
	unsigned long head;
	struct cs_buffers *buf = sink_config;

	/* wrap head around to the amount of space we have */
	head = handle->head & ((buf->nr_pages << PAGE_SHIFT) - 1);

	/* find the page to write to */
	buf->cur = head / PAGE_SIZE;

	/* and offset within that page */
	buf->offset = head % PAGE_SIZE;

	local_set(&buf->data_size, 0);

	return ret;
}

static unsigned long tmc_reset_etf_buffer(struct coresight_device *csdev,
					  struct perf_output_handle *handle,
					  void *sink_config)
{
	long size = 0;
	struct cs_buffers *buf = sink_config;

	if (buf) {
		/*
		 * In snapshot mode ->data_size holds the new address of the
		 * ring buffer's head.  The size itself is the whole address
		 * range since we want the latest information.
		 */
		if (buf->snapshot)
			handle->head = local_xchg(&buf->data_size,
						  buf->nr_pages << PAGE_SHIFT);
		/*
		 * Tell the tracer PMU how much we got in this run and if
		 * something went wrong along the way.  Nobody else can use
		 * this cs_buffers instance until we are done.  As such
		 * resetting parameters here and squaring off with the ring
		 * buffer API in the tracer PMU is fine.
		 */
		size = local_xchg(&buf->data_size, 0);
	}

	return size;
}

static void tmc_update_etf_buffer(struct coresight_device *csdev,
				  struct perf_output_handle *handle,
				  void *sink_config)
{
	bool lost = false;
	int i, cur;
	const u32 *barrier;
	u32 *buf_ptr;
	u32 read_ptr, write_ptr;
	u32 status, to_read;
	unsigned long offset;
	struct cs_buffers *buf = sink_config;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	if (!buf)
		return;

	/* This shouldn't happen */
	if (WARN_ON_ONCE(drvdata->mode != CS_MODE_PERF))
		return;

	CS_UNLOCK(drvdata->base);

	tmc_flush_and_stop(drvdata);

	read_ptr = readl_relaxed(drvdata->base + TMC_RRP);
	write_ptr = readl_relaxed(drvdata->base + TMC_RWP);

	/*
	 * Get a hold of the status register and see if a wrap around
	 * has occurred.  If so adjust things accordingly.
	 */
	status = readl_relaxed(drvdata->base + TMC_STS);
	if (status & TMC_STS_FULL) {
		lost = true;
		to_read = drvdata->size;
	} else {
		to_read = CIRC_CNT(write_ptr, read_ptr, drvdata->size);
	}

	/*
	 * The TMC RAM buffer may be bigger than the space available in the
	 * perf ring buffer (handle->size).  If so advance the RRP so that we
	 * get the latest trace data.
	 */
	if (to_read > handle->size) {
		u32 mask = 0;

		/*
		 * The value written to RRP must be byte-address aligned to
		 * the width of the trace memory databus _and_ to a frame
		 * boundary (16 byte), whichever is the biggest. For example,
		 * for 32-bit, 64-bit and 128-bit wide trace memory, the four
		 * LSBs must be 0s. For 256-bit wide trace memory, the five
		 * LSBs must be 0s.
		 */
		switch (drvdata->memwidth) {
		case TMC_MEM_INTF_WIDTH_32BITS:
		case TMC_MEM_INTF_WIDTH_64BITS:
		case TMC_MEM_INTF_WIDTH_128BITS:
			mask = GENMASK(31, 5);
			break;
		case TMC_MEM_INTF_WIDTH_256BITS:
			mask = GENMASK(31, 6);
			break;
		}

		/*
		 * Make sure the new size is aligned in accordance with the
		 * requirement explained above.
		 */
		to_read = handle->size & mask;
		/* Move the RAM read pointer up */
		read_ptr = (write_ptr + drvdata->size) - to_read;
		/* Make sure we are still within our limits */
		if (read_ptr > (drvdata->size - 1))
			read_ptr -= drvdata->size;
		/* Tell the HW */
		writel_relaxed(read_ptr, drvdata->base + TMC_RRP);
		lost = true;
	}

	if (lost)
		perf_aux_output_flag(handle, PERF_AUX_FLAG_TRUNCATED);

	cur = buf->cur;
	offset = buf->offset;
	barrier = barrier_pkt;

	/* for every byte to read */
	for (i = 0; i < to_read; i += 4) {
		buf_ptr = buf->data_pages[cur] + offset;
		*buf_ptr = readl_relaxed(drvdata->base + TMC_RRD);

		if (lost && *barrier) {
			*buf_ptr = *barrier;
			barrier++;
		}

		offset += 4;
		if (offset >= PAGE_SIZE) {
			offset = 0;
			cur++;
			/* wrap around at the end of the buffer */
			cur &= buf->nr_pages - 1;
		}
	}

	/*
	 * In snapshot mode all we have to do is communicate to
	 * perf_aux_output_end() the address of the current head.  In full
	 * trace mode the same function expects a size to move rb->aux_head
	 * forward.
	 */
	if (buf->snapshot)
		local_set(&buf->data_size, (cur * PAGE_SIZE) + offset);
	else
		local_add(to_read, &buf->data_size);

	CS_LOCK(drvdata->base);
}

static const struct coresight_ops_sink tmc_etf_sink_ops = {
	.enable		= tmc_enable_etf_sink,
	.disable	= tmc_disable_etf_sink,
	.alloc_buffer	= tmc_alloc_etf_buffer,
	.free_buffer	= tmc_free_etf_buffer,
	.set_buffer	= tmc_set_etf_buffer,
	.reset_buffer	= tmc_reset_etf_buffer,
	.update_buffer	= tmc_update_etf_buffer,
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

	if (drvdata->reading) {
		ret = -EBUSY;
		goto out;
	}

	/* There is no point in reading a TMC in HW FIFO mode */
	mode = readl_relaxed(drvdata->base + TMC_MODE);
	if (mode != TMC_MODE_CIRCULAR_BUFFER) {
		ret = -EINVAL;
		goto out;
	}

	/* Don't interfere if operated from Perf */
	if (drvdata->mode == CS_MODE_PERF) {
		ret = -EINVAL;
		goto out;
	}

	/* If drvdata::buf is NULL the trace data has been read already */
	if (drvdata->buf == NULL) {
		ret = -EINVAL;
		goto out;
	}

	/* Disable the TMC if need be */
	if (drvdata->mode == CS_MODE_SYSFS)
		tmc_etb_disable_hw(drvdata);

	drvdata->reading = true;
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	return ret;
}

int tmc_read_unprepare_etb(struct tmc_drvdata *drvdata)
{
	char *buf = NULL;
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
	if (drvdata->mode == CS_MODE_SYSFS) {
		/*
		 * The trace run will continue with the same allocated trace
		 * buffer. As such zero-out the buffer so that we don't end
		 * up with stale data.
		 *
		 * Since the tracer is still enabled drvdata::buf
		 * can't be NULL.
		 */
		memset(drvdata->buf, 0, drvdata->size);
		tmc_etb_enable_hw(drvdata);
	} else {
		/*
		 * The ETB/ETF is not tracing and the buffer was just read.
		 * As such prepare to free the trace buffer.
		 */
		buf = drvdata->buf;
		drvdata->buf = NULL;
	}

	drvdata->reading = false;
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	/*
	 * Free allocated memory outside of the spinlock.  There is no need
	 * to assert the validity of 'buf' since calling kfree(NULL) is safe.
	 */
	kfree(buf);

	return 0;
}
