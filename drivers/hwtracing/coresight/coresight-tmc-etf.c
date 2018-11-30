// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2016 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <linux/atomic.h>
#include <linux/circ_buf.h>
#include <linux/coresight.h>
#include <linux/perf_event.h>
#include <linux/slab.h>
#include "coresight-priv.h"
#include "coresight-tmc.h"
#include "coresight-etm-perf.h"

static int tmc_set_etf_buffer(struct coresight_device *csdev,
			      struct perf_output_handle *handle);

static void __tmc_etb_enable_hw(struct tmc_drvdata *drvdata)
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

static int tmc_etb_enable_hw(struct tmc_drvdata *drvdata)
{
	int rc = coresight_claim_device(drvdata->base);

	if (rc)
		return rc;

	__tmc_etb_enable_hw(drvdata);
	return 0;
}

static void tmc_etb_dump_hw(struct tmc_drvdata *drvdata)
{
	char *bufp;
	u32 read_data, lost;
	int i;

	/* Check if the buffer wrapped around. */
	lost = readl_relaxed(drvdata->base + TMC_STS) & TMC_STS_FULL;
	bufp = drvdata->buf;
	drvdata->len = 0;
	while (1) {
		for (i = 0; i < drvdata->memwidth; i++) {
			read_data = readl_relaxed(drvdata->base + TMC_RRD);
			if (read_data == 0xFFFFFFFF)
				goto done;
			memcpy(bufp, &read_data, 4);
			bufp += 4;
			drvdata->len += 4;
		}
	}
done:
	if (lost)
		coresight_insert_barrier_packet(drvdata->buf);
	return;
}

static void __tmc_etb_disable_hw(struct tmc_drvdata *drvdata)
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

static void tmc_etb_disable_hw(struct tmc_drvdata *drvdata)
{
	coresight_disclaim_device(drvdata->base);
	__tmc_etb_disable_hw(drvdata);
}

static void __tmc_etf_enable_hw(struct tmc_drvdata *drvdata)
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

static int tmc_etf_enable_hw(struct tmc_drvdata *drvdata)
{
	int rc = coresight_claim_device(drvdata->base);

	if (rc)
		return rc;

	__tmc_etf_enable_hw(drvdata);
	return 0;
}

static void tmc_etf_disable_hw(struct tmc_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	tmc_flush_and_stop(drvdata);
	tmc_disable_hw(drvdata);
	coresight_disclaim_device_unlocked(drvdata->base);
	CS_LOCK(drvdata->base);
}

/*
 * Return the available trace data in the buffer from @pos, with
 * a maximum limit of @len, updating the @bufpp on where to
 * find it.
 */
ssize_t tmc_etb_get_sysfs_trace(struct tmc_drvdata *drvdata,
				loff_t pos, size_t len, char **bufpp)
{
	ssize_t actual = len;

	/* Adjust the len to available size @pos */
	if (pos + actual > drvdata->len)
		actual = drvdata->len - pos;
	if (actual > 0)
		*bufpp = drvdata->buf + pos;
	return actual;
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
	if (drvdata->mode == CS_MODE_SYSFS) {
		atomic_inc(csdev->refcnt);
		goto out;
	}

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

	ret = tmc_etb_enable_hw(drvdata);
	if (!ret) {
		drvdata->mode = CS_MODE_SYSFS;
		atomic_inc(csdev->refcnt);
	} else {
		/* Free up the buffer if we failed to enable */
		used = false;
	}
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	/* Free memory outside the spinlock if need be */
	if (!used)
		kfree(buf);

	return ret;
}

static int tmc_enable_etf_sink_perf(struct coresight_device *csdev, void *data)
{
	int ret = 0;
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	struct perf_output_handle *handle = data;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	do {
		ret = -EINVAL;
		if (drvdata->reading)
			break;
		/*
		 * In Perf mode there can be only one writer per sink.  There
		 * is also no need to continue if the ETB/ETF is already
		 * operated from sysFS.
		 */
		if (drvdata->mode != CS_MODE_DISABLED)
			break;

		ret = tmc_set_etf_buffer(csdev, handle);
		if (ret)
			break;
		ret  = tmc_etb_enable_hw(drvdata);
		if (!ret) {
			drvdata->mode = CS_MODE_PERF;
			atomic_inc(csdev->refcnt);
		}
	} while (0);
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	return ret;
}

static int tmc_enable_etf_sink(struct coresight_device *csdev,
			       u32 mode, void *data)
{
	int ret;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	switch (mode) {
	case CS_MODE_SYSFS:
		ret = tmc_enable_etf_sink_sysfs(csdev);
		break;
	case CS_MODE_PERF:
		ret = tmc_enable_etf_sink_perf(csdev, data);
		break;
	/* We shouldn't be here */
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		return ret;

	dev_dbg(drvdata->dev, "TMC-ETB/ETF enabled\n");
	return 0;
}

static int tmc_disable_etf_sink(struct coresight_device *csdev)
{
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock_irqsave(&drvdata->spinlock, flags);

	if (drvdata->reading) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return -EBUSY;
	}

	if (atomic_dec_return(csdev->refcnt)) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return -EBUSY;
	}

	/* Complain if we (somehow) got out of sync */
	WARN_ON_ONCE(drvdata->mode == CS_MODE_DISABLED);
	tmc_etb_disable_hw(drvdata);
	drvdata->mode = CS_MODE_DISABLED;

	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	dev_dbg(drvdata->dev, "TMC-ETB/ETF disabled\n");
	return 0;
}

static int tmc_enable_etf_link(struct coresight_device *csdev,
			       int inport, int outport)
{
	int ret = 0;
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	bool first_enable = false;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return -EBUSY;
	}

	if (atomic_read(&csdev->refcnt[0]) == 0) {
		ret = tmc_etf_enable_hw(drvdata);
		if (!ret) {
			drvdata->mode = CS_MODE_SYSFS;
			first_enable = true;
		}
	}
	if (!ret)
		atomic_inc(&csdev->refcnt[0]);
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	if (first_enable)
		dev_dbg(drvdata->dev, "TMC-ETF enabled\n");
	return ret;
}

static void tmc_disable_etf_link(struct coresight_device *csdev,
				 int inport, int outport)
{
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	bool last_disable = false;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return;
	}

	if (atomic_dec_return(&csdev->refcnt[0]) == 0) {
		tmc_etf_disable_hw(drvdata);
		drvdata->mode = CS_MODE_DISABLED;
		last_disable = true;
	}
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	if (last_disable)
		dev_dbg(drvdata->dev, "TMC-ETF disabled\n");
}

static void *tmc_alloc_etf_buffer(struct coresight_device *csdev,
				  struct perf_event *event, void **pages,
				  int nr_pages, bool overwrite)
{
	int node, cpu = event->cpu;
	struct cs_buffers *buf;

	node = (cpu == -1) ? NUMA_NO_NODE : cpu_to_node(cpu);

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
			      struct perf_output_handle *handle)
{
	int ret = 0;
	unsigned long head;
	struct cs_buffers *buf = etm_perf_sink_config(handle);

	if (!buf)
		return -EINVAL;

	/* wrap head around to the amount of space we have */
	head = handle->head & ((buf->nr_pages << PAGE_SHIFT) - 1);

	/* find the page to write to */
	buf->cur = head / PAGE_SIZE;

	/* and offset within that page */
	buf->offset = head % PAGE_SIZE;

	local_set(&buf->data_size, 0);

	return ret;
}

static unsigned long tmc_update_etf_buffer(struct coresight_device *csdev,
				  struct perf_output_handle *handle,
				  void *sink_config)
{
	bool lost = false;
	int i, cur;
	const u32 *barrier;
	u32 *buf_ptr;
	u64 read_ptr, write_ptr;
	u32 status;
	unsigned long offset, to_read, flags;
	struct cs_buffers *buf = sink_config;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	if (!buf)
		return 0;

	/* This shouldn't happen */
	if (WARN_ON_ONCE(drvdata->mode != CS_MODE_PERF))
		return 0;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	CS_UNLOCK(drvdata->base);

	tmc_flush_and_stop(drvdata);

	read_ptr = tmc_read_rrp(drvdata);
	write_ptr = tmc_read_rwp(drvdata);

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
	 * get the latest trace data.  In snapshot mode none of that matters
	 * since we are expected to clobber stale data in favour of the latest
	 * traces.
	 */
	if (!buf->snapshot && to_read > handle->size) {
		u32 mask = tmc_get_memwidth_mask(drvdata);

		/*
		 * Make sure the new size is aligned in accordance with the
		 * requirement explained in function tmc_get_memwidth_mask().
		 */
		to_read = handle->size & mask;
		/* Move the RAM read pointer up */
		read_ptr = (write_ptr + drvdata->size) - to_read;
		/* Make sure we are still within our limits */
		if (read_ptr > (drvdata->size - 1))
			read_ptr -= drvdata->size;
		/* Tell the HW */
		tmc_write_rrp(drvdata, read_ptr);
		lost = true;
	}

	/*
	 * Don't set the TRUNCATED flag in snapshot mode because 1) the
	 * captured buffer is expected to be truncated and 2) a full buffer
	 * prevents the event from being re-enabled by the perf core,
	 * resulting in stale data being send to user space.
	 */
	if (!buf->snapshot && lost)
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

	/* In snapshot mode we have to update the head */
	if (buf->snapshot) {
		handle->head = (cur * PAGE_SIZE) + offset;
		to_read = buf->nr_pages << PAGE_SHIFT;
	}
	CS_LOCK(drvdata->base);
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	return to_read;
}

static const struct coresight_ops_sink tmc_etf_sink_ops = {
	.enable		= tmc_enable_etf_sink,
	.disable	= tmc_disable_etf_sink,
	.alloc_buffer	= tmc_alloc_etf_buffer,
	.free_buffer	= tmc_free_etf_buffer,
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
		__tmc_etb_disable_hw(drvdata);

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

	/* Re-enable the TMC if need be */
	if (drvdata->mode == CS_MODE_SYSFS) {
		/* There is no point in reading a TMC in HW FIFO mode */
		mode = readl_relaxed(drvdata->base + TMC_MODE);
		if (mode != TMC_MODE_CIRCULAR_BUFFER) {
			spin_unlock_irqrestore(&drvdata->spinlock, flags);
			return -EINVAL;
		}
		/*
		 * The trace run will continue with the same allocated trace
		 * buffer. As such zero-out the buffer so that we don't end
		 * up with stale data.
		 *
		 * Since the tracer is still enabled drvdata::buf
		 * can't be NULL.
		 */
		memset(drvdata->buf, 0, drvdata->size);
		__tmc_etb_enable_hw(drvdata);
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
