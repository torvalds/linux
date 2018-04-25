/*
 * ispstat.c
 *
 * TI OMAP3 ISP - Statistics core
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc
 *
 * Contacts: David Cohen <dacohen@gmail.com>
 *	     Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>

#include "isp.h"

#define ISP_STAT_USES_DMAENGINE(stat)	((stat)->dma_ch != NULL)

/*
 * MAGIC_SIZE must always be the greatest common divisor of
 * AEWB_PACKET_SIZE and AF_PAXEL_SIZE.
 */
#define MAGIC_SIZE		16
#define MAGIC_NUM		0x55

/* HACK: AF module seems to be writing one more paxel data than it should. */
#define AF_EXTRA_DATA		OMAP3ISP_AF_PAXEL_SIZE

/*
 * HACK: H3A modules go to an invalid state after have a SBL overflow. It makes
 * the next buffer to start to be written in the same point where the overflow
 * occurred instead of the configured address. The only known way to make it to
 * go back to a valid state is having a valid buffer processing. Of course it
 * requires at least a doubled buffer size to avoid an access to invalid memory
 * region. But it does not fix everything. It may happen more than one
 * consecutive SBL overflows. In that case, it might be unpredictable how many
 * buffers the allocated memory should fit. For that case, a recover
 * configuration was created. It produces the minimum buffer size for each H3A
 * module and decrease the change for more SBL overflows. This recover state
 * will be enabled every time a SBL overflow occur. As the output buffer size
 * isn't big, it's possible to have an extra size able to fit many recover
 * buffers making it extreamily unlikely to have an access to invalid memory
 * region.
 */
#define NUM_H3A_RECOVER_BUFS	10

/*
 * HACK: Because of HW issues the generic layer sometimes need to have
 * different behaviour for different statistic modules.
 */
#define IS_H3A_AF(stat)		((stat) == &(stat)->isp->isp_af)
#define IS_H3A_AEWB(stat)	((stat) == &(stat)->isp->isp_aewb)
#define IS_H3A(stat)		(IS_H3A_AF(stat) || IS_H3A_AEWB(stat))

static void __isp_stat_buf_sync_magic(struct ispstat *stat,
				      struct ispstat_buffer *buf,
				      u32 buf_size, enum dma_data_direction dir,
				      void (*dma_sync)(struct device *,
					dma_addr_t, unsigned long, size_t,
					enum dma_data_direction))
{
	/* Sync the initial and final magic words. */
	dma_sync(stat->isp->dev, buf->dma_addr, 0, MAGIC_SIZE, dir);
	dma_sync(stat->isp->dev, buf->dma_addr + (buf_size & PAGE_MASK),
		 buf_size & ~PAGE_MASK, MAGIC_SIZE, dir);
}

static void isp_stat_buf_sync_magic_for_device(struct ispstat *stat,
					       struct ispstat_buffer *buf,
					       u32 buf_size,
					       enum dma_data_direction dir)
{
	if (ISP_STAT_USES_DMAENGINE(stat))
		return;

	__isp_stat_buf_sync_magic(stat, buf, buf_size, dir,
				  dma_sync_single_range_for_device);
}

static void isp_stat_buf_sync_magic_for_cpu(struct ispstat *stat,
					    struct ispstat_buffer *buf,
					    u32 buf_size,
					    enum dma_data_direction dir)
{
	if (ISP_STAT_USES_DMAENGINE(stat))
		return;

	__isp_stat_buf_sync_magic(stat, buf, buf_size, dir,
				  dma_sync_single_range_for_cpu);
}

static int isp_stat_buf_check_magic(struct ispstat *stat,
				    struct ispstat_buffer *buf)
{
	const u32 buf_size = IS_H3A_AF(stat) ?
			     buf->buf_size + AF_EXTRA_DATA : buf->buf_size;
	u8 *w;
	u8 *end;
	int ret = -EINVAL;

	isp_stat_buf_sync_magic_for_cpu(stat, buf, buf_size, DMA_FROM_DEVICE);

	/* Checking initial magic numbers. They shouldn't be here anymore. */
	for (w = buf->virt_addr, end = w + MAGIC_SIZE; w < end; w++)
		if (likely(*w != MAGIC_NUM))
			ret = 0;

	if (ret) {
		dev_dbg(stat->isp->dev,
			"%s: beginning magic check does not match.\n",
			stat->subdev.name);
		return ret;
	}

	/* Checking magic numbers at the end. They must be still here. */
	for (w = buf->virt_addr + buf_size, end = w + MAGIC_SIZE;
	     w < end; w++) {
		if (unlikely(*w != MAGIC_NUM)) {
			dev_dbg(stat->isp->dev,
				"%s: ending magic check does not match.\n",
				stat->subdev.name);
			return -EINVAL;
		}
	}

	isp_stat_buf_sync_magic_for_device(stat, buf, buf_size,
					   DMA_FROM_DEVICE);

	return 0;
}

static void isp_stat_buf_insert_magic(struct ispstat *stat,
				      struct ispstat_buffer *buf)
{
	const u32 buf_size = IS_H3A_AF(stat) ?
			     stat->buf_size + AF_EXTRA_DATA : stat->buf_size;

	isp_stat_buf_sync_magic_for_cpu(stat, buf, buf_size, DMA_FROM_DEVICE);

	/*
	 * Inserting MAGIC_NUM at the beginning and end of the buffer.
	 * buf->buf_size is set only after the buffer is queued. For now the
	 * right buf_size for the current configuration is pointed by
	 * stat->buf_size.
	 */
	memset(buf->virt_addr, MAGIC_NUM, MAGIC_SIZE);
	memset(buf->virt_addr + buf_size, MAGIC_NUM, MAGIC_SIZE);

	isp_stat_buf_sync_magic_for_device(stat, buf, buf_size,
					   DMA_BIDIRECTIONAL);
}

static void isp_stat_buf_sync_for_device(struct ispstat *stat,
					 struct ispstat_buffer *buf)
{
	if (ISP_STAT_USES_DMAENGINE(stat))
		return;

	dma_sync_sg_for_device(stat->isp->dev, buf->sgt.sgl,
			       buf->sgt.nents, DMA_FROM_DEVICE);
}

static void isp_stat_buf_sync_for_cpu(struct ispstat *stat,
				      struct ispstat_buffer *buf)
{
	if (ISP_STAT_USES_DMAENGINE(stat))
		return;

	dma_sync_sg_for_cpu(stat->isp->dev, buf->sgt.sgl,
			    buf->sgt.nents, DMA_FROM_DEVICE);
}

static void isp_stat_buf_clear(struct ispstat *stat)
{
	int i;

	for (i = 0; i < STAT_MAX_BUFS; i++)
		stat->buf[i].empty = 1;
}

static struct ispstat_buffer *
__isp_stat_buf_find(struct ispstat *stat, int look_empty)
{
	struct ispstat_buffer *found = NULL;
	int i;

	for (i = 0; i < STAT_MAX_BUFS; i++) {
		struct ispstat_buffer *curr = &stat->buf[i];

		/*
		 * Don't select the buffer which is being copied to
		 * userspace or used by the module.
		 */
		if (curr == stat->locked_buf || curr == stat->active_buf)
			continue;

		/* Don't select uninitialised buffers if it's not required */
		if (!look_empty && curr->empty)
			continue;

		/* Pick uninitialised buffer over anything else if look_empty */
		if (curr->empty) {
			found = curr;
			break;
		}

		/* Choose the oldest buffer */
		if (!found ||
		    (s32)curr->frame_number - (s32)found->frame_number < 0)
			found = curr;
	}

	return found;
}

static inline struct ispstat_buffer *
isp_stat_buf_find_oldest(struct ispstat *stat)
{
	return __isp_stat_buf_find(stat, 0);
}

static inline struct ispstat_buffer *
isp_stat_buf_find_oldest_or_empty(struct ispstat *stat)
{
	return __isp_stat_buf_find(stat, 1);
}

static int isp_stat_buf_queue(struct ispstat *stat)
{
	if (!stat->active_buf)
		return STAT_NO_BUF;

	ktime_get_ts64(&stat->active_buf->ts);

	stat->active_buf->buf_size = stat->buf_size;
	if (isp_stat_buf_check_magic(stat, stat->active_buf)) {
		dev_dbg(stat->isp->dev, "%s: data wasn't properly written.\n",
			stat->subdev.name);
		return STAT_NO_BUF;
	}
	stat->active_buf->config_counter = stat->config_counter;
	stat->active_buf->frame_number = stat->frame_number;
	stat->active_buf->empty = 0;
	stat->active_buf = NULL;

	return STAT_BUF_DONE;
}

/* Get next free buffer to write the statistics to and mark it active. */
static void isp_stat_buf_next(struct ispstat *stat)
{
	if (unlikely(stat->active_buf))
		/* Overwriting unused active buffer */
		dev_dbg(stat->isp->dev,
			"%s: new buffer requested without queuing active one.\n",
			stat->subdev.name);
	else
		stat->active_buf = isp_stat_buf_find_oldest_or_empty(stat);
}

static void isp_stat_buf_release(struct ispstat *stat)
{
	unsigned long flags;

	isp_stat_buf_sync_for_device(stat, stat->locked_buf);
	spin_lock_irqsave(&stat->isp->stat_lock, flags);
	stat->locked_buf = NULL;
	spin_unlock_irqrestore(&stat->isp->stat_lock, flags);
}

/* Get buffer to userspace. */
static struct ispstat_buffer *isp_stat_buf_get(struct ispstat *stat,
					       struct omap3isp_stat_data *data)
{
	int rval = 0;
	unsigned long flags;
	struct ispstat_buffer *buf;

	spin_lock_irqsave(&stat->isp->stat_lock, flags);

	while (1) {
		buf = isp_stat_buf_find_oldest(stat);
		if (!buf) {
			spin_unlock_irqrestore(&stat->isp->stat_lock, flags);
			dev_dbg(stat->isp->dev, "%s: cannot find a buffer.\n",
				stat->subdev.name);
			return ERR_PTR(-EBUSY);
		}
		if (isp_stat_buf_check_magic(stat, buf)) {
			dev_dbg(stat->isp->dev,
				"%s: current buffer has corrupted data\n.",
				stat->subdev.name);
			/* Mark empty because it doesn't have valid data. */
			buf->empty = 1;
		} else {
			/* Buffer isn't corrupted. */
			break;
		}
	}

	stat->locked_buf = buf;

	spin_unlock_irqrestore(&stat->isp->stat_lock, flags);

	if (buf->buf_size > data->buf_size) {
		dev_warn(stat->isp->dev,
			 "%s: userspace's buffer size is not enough.\n",
			 stat->subdev.name);
		isp_stat_buf_release(stat);
		return ERR_PTR(-EINVAL);
	}

	isp_stat_buf_sync_for_cpu(stat, buf);

	rval = copy_to_user(data->buf,
			    buf->virt_addr,
			    buf->buf_size);

	if (rval) {
		dev_info(stat->isp->dev,
			 "%s: failed copying %d bytes of stat data\n",
			 stat->subdev.name, rval);
		buf = ERR_PTR(-EFAULT);
		isp_stat_buf_release(stat);
	}

	return buf;
}

static void isp_stat_bufs_free(struct ispstat *stat)
{
	struct device *dev = ISP_STAT_USES_DMAENGINE(stat)
			   ? NULL : stat->isp->dev;
	unsigned int i;

	for (i = 0; i < STAT_MAX_BUFS; i++) {
		struct ispstat_buffer *buf = &stat->buf[i];

		if (!buf->virt_addr)
			continue;

		sg_free_table(&buf->sgt);

		dma_free_coherent(dev, stat->buf_alloc_size, buf->virt_addr,
				  buf->dma_addr);

		buf->dma_addr = 0;
		buf->virt_addr = NULL;
		buf->empty = 1;
	}

	dev_dbg(stat->isp->dev, "%s: all buffers were freed.\n",
		stat->subdev.name);

	stat->buf_alloc_size = 0;
	stat->active_buf = NULL;
}

static int isp_stat_bufs_alloc_one(struct device *dev,
				   struct ispstat_buffer *buf,
				   unsigned int size)
{
	int ret;

	buf->virt_addr = dma_alloc_coherent(dev, size, &buf->dma_addr,
					    GFP_KERNEL | GFP_DMA);
	if (!buf->virt_addr)
		return -ENOMEM;

	ret = dma_get_sgtable(dev, &buf->sgt, buf->virt_addr, buf->dma_addr,
			      size);
	if (ret < 0) {
		dma_free_coherent(dev, size, buf->virt_addr, buf->dma_addr);
		buf->virt_addr = NULL;
		buf->dma_addr = 0;
		return ret;
	}

	return 0;
}

/*
 * The device passed to the DMA API depends on whether the statistics block uses
 * ISP DMA, external DMA or PIO to transfer data.
 *
 * The first case (for the AEWB and AF engines) passes the ISP device, resulting
 * in the DMA buffers being mapped through the ISP IOMMU.
 *
 * The second case (for the histogram engine) should pass the DMA engine device.
 * As that device isn't accessible through the OMAP DMA engine API the driver
 * passes NULL instead, resulting in the buffers being mapped directly as
 * physical pages.
 *
 * The third case (for the histogram engine) doesn't require any mapping. The
 * buffers could be allocated with kmalloc/vmalloc, but we still use
 * dma_alloc_coherent() for consistency purpose.
 */
static int isp_stat_bufs_alloc(struct ispstat *stat, u32 size)
{
	struct device *dev = ISP_STAT_USES_DMAENGINE(stat)
			   ? NULL : stat->isp->dev;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&stat->isp->stat_lock, flags);

	BUG_ON(stat->locked_buf != NULL);

	/* Are the old buffers big enough? */
	if (stat->buf_alloc_size >= size) {
		spin_unlock_irqrestore(&stat->isp->stat_lock, flags);
		return 0;
	}

	if (stat->state != ISPSTAT_DISABLED || stat->buf_processing) {
		dev_info(stat->isp->dev,
			 "%s: trying to allocate memory when busy\n",
			 stat->subdev.name);
		spin_unlock_irqrestore(&stat->isp->stat_lock, flags);
		return -EBUSY;
	}

	spin_unlock_irqrestore(&stat->isp->stat_lock, flags);

	isp_stat_bufs_free(stat);

	stat->buf_alloc_size = size;

	for (i = 0; i < STAT_MAX_BUFS; i++) {
		struct ispstat_buffer *buf = &stat->buf[i];
		int ret;

		ret = isp_stat_bufs_alloc_one(dev, buf, size);
		if (ret < 0) {
			dev_err(stat->isp->dev,
				"%s: Failed to allocate DMA buffer %u\n",
				stat->subdev.name, i);
			isp_stat_bufs_free(stat);
			return ret;
		}

		buf->empty = 1;

		dev_dbg(stat->isp->dev,
			"%s: buffer[%u] allocated. dma=%pad virt=%p",
			stat->subdev.name, i, &buf->dma_addr, buf->virt_addr);
	}

	return 0;
}

static void isp_stat_queue_event(struct ispstat *stat, int err)
{
	struct video_device *vdev = stat->subdev.devnode;
	struct v4l2_event event;
	struct omap3isp_stat_event_status *status = (void *)event.u.data;

	memset(&event, 0, sizeof(event));
	if (!err) {
		status->frame_number = stat->frame_number;
		status->config_counter = stat->config_counter;
	} else {
		status->buf_err = 1;
	}
	event.type = stat->event_type;
	v4l2_event_queue(vdev, &event);
}


/*
 * omap3isp_stat_request_statistics - Request statistics.
 * @data: Pointer to return statistics data.
 *
 * Returns 0 if successful.
 */
int omap3isp_stat_request_statistics(struct ispstat *stat,
				     struct omap3isp_stat_data *data)
{
	struct ispstat_buffer *buf;

	if (stat->state != ISPSTAT_ENABLED) {
		dev_dbg(stat->isp->dev, "%s: engine not enabled.\n",
			stat->subdev.name);
		return -EINVAL;
	}

	mutex_lock(&stat->ioctl_lock);
	buf = isp_stat_buf_get(stat, data);
	if (IS_ERR(buf)) {
		mutex_unlock(&stat->ioctl_lock);
		return PTR_ERR(buf);
	}

	data->ts.tv_sec = buf->ts.tv_sec;
	data->ts.tv_usec = buf->ts.tv_nsec / NSEC_PER_USEC;
	data->config_counter = buf->config_counter;
	data->frame_number = buf->frame_number;
	data->buf_size = buf->buf_size;

	buf->empty = 1;
	isp_stat_buf_release(stat);
	mutex_unlock(&stat->ioctl_lock);

	return 0;
}

int omap3isp_stat_request_statistics_time32(struct ispstat *stat,
					struct omap3isp_stat_data_time32 *data)
{
	struct omap3isp_stat_data data64;
	int ret;

	ret = omap3isp_stat_request_statistics(stat, &data64);
	if (ret)
		return ret;

	data->ts.tv_sec = data64.ts.tv_sec;
	data->ts.tv_usec = data64.ts.tv_usec;
	memcpy(&data->buf, &data64.buf, sizeof(*data) - sizeof(data->ts));

	return 0;
}

/*
 * omap3isp_stat_config - Receives new statistic engine configuration.
 * @new_conf: Pointer to config structure.
 *
 * Returns 0 if successful, -EINVAL if new_conf pointer is NULL, -ENOMEM if
 * was unable to allocate memory for the buffer, or other errors if parameters
 * are invalid.
 */
int omap3isp_stat_config(struct ispstat *stat, void *new_conf)
{
	int ret;
	unsigned long irqflags;
	struct ispstat_generic_config *user_cfg = new_conf;
	u32 buf_size = user_cfg->buf_size;

	if (!new_conf) {
		dev_dbg(stat->isp->dev, "%s: configuration is NULL\n",
			stat->subdev.name);
		return -EINVAL;
	}

	mutex_lock(&stat->ioctl_lock);

	dev_dbg(stat->isp->dev,
		"%s: configuring module with buffer size=0x%08lx\n",
		stat->subdev.name, (unsigned long)buf_size);

	ret = stat->ops->validate_params(stat, new_conf);
	if (ret) {
		mutex_unlock(&stat->ioctl_lock);
		dev_dbg(stat->isp->dev, "%s: configuration values are invalid.\n",
			stat->subdev.name);
		return ret;
	}

	if (buf_size != user_cfg->buf_size)
		dev_dbg(stat->isp->dev,
			"%s: driver has corrected buffer size request to 0x%08lx\n",
			stat->subdev.name,
			(unsigned long)user_cfg->buf_size);

	/*
	 * Hack: H3A modules may need a doubled buffer size to avoid access
	 * to a invalid memory address after a SBL overflow.
	 * The buffer size is always PAGE_ALIGNED.
	 * Hack 2: MAGIC_SIZE is added to buf_size so a magic word can be
	 * inserted at the end to data integrity check purpose.
	 * Hack 3: AF module writes one paxel data more than it should, so
	 * the buffer allocation must consider it to avoid invalid memory
	 * access.
	 * Hack 4: H3A need to allocate extra space for the recover state.
	 */
	if (IS_H3A(stat)) {
		buf_size = user_cfg->buf_size * 2 + MAGIC_SIZE;
		if (IS_H3A_AF(stat))
			/*
			 * Adding one extra paxel data size for each recover
			 * buffer + 2 regular ones.
			 */
			buf_size += AF_EXTRA_DATA * (NUM_H3A_RECOVER_BUFS + 2);
		if (stat->recover_priv) {
			struct ispstat_generic_config *recover_cfg =
				stat->recover_priv;
			buf_size += recover_cfg->buf_size *
				    NUM_H3A_RECOVER_BUFS;
		}
		buf_size = PAGE_ALIGN(buf_size);
	} else { /* Histogram */
		buf_size = PAGE_ALIGN(user_cfg->buf_size + MAGIC_SIZE);
	}

	ret = isp_stat_bufs_alloc(stat, buf_size);
	if (ret) {
		mutex_unlock(&stat->ioctl_lock);
		return ret;
	}

	spin_lock_irqsave(&stat->isp->stat_lock, irqflags);
	stat->ops->set_params(stat, new_conf);
	spin_unlock_irqrestore(&stat->isp->stat_lock, irqflags);

	/*
	 * Returning the right future config_counter for this setup, so
	 * userspace can *know* when it has been applied.
	 */
	user_cfg->config_counter = stat->config_counter + stat->inc_config;

	/* Module has a valid configuration. */
	stat->configured = 1;
	dev_dbg(stat->isp->dev,
		"%s: module has been successfully configured.\n",
		stat->subdev.name);

	mutex_unlock(&stat->ioctl_lock);

	return 0;
}

/*
 * isp_stat_buf_process - Process statistic buffers.
 * @buf_state: points out if buffer is ready to be processed. It's necessary
 *	       because histogram needs to copy the data from internal memory
 *	       before be able to process the buffer.
 */
static int isp_stat_buf_process(struct ispstat *stat, int buf_state)
{
	int ret = STAT_NO_BUF;

	if (!atomic_add_unless(&stat->buf_err, -1, 0) &&
	    buf_state == STAT_BUF_DONE && stat->state == ISPSTAT_ENABLED) {
		ret = isp_stat_buf_queue(stat);
		isp_stat_buf_next(stat);
	}

	return ret;
}

int omap3isp_stat_pcr_busy(struct ispstat *stat)
{
	return stat->ops->busy(stat);
}

int omap3isp_stat_busy(struct ispstat *stat)
{
	return omap3isp_stat_pcr_busy(stat) | stat->buf_processing |
		(stat->state != ISPSTAT_DISABLED);
}

/*
 * isp_stat_pcr_enable - Disables/Enables statistic engines.
 * @pcr_enable: 0/1 - Disables/Enables the engine.
 *
 * Must be called from ISP driver when the module is idle and synchronized
 * with CCDC.
 */
static void isp_stat_pcr_enable(struct ispstat *stat, u8 pcr_enable)
{
	if ((stat->state != ISPSTAT_ENABLING &&
	     stat->state != ISPSTAT_ENABLED) && pcr_enable)
		/* Userspace has disabled the module. Aborting. */
		return;

	stat->ops->enable(stat, pcr_enable);
	if (stat->state == ISPSTAT_DISABLING && !pcr_enable)
		stat->state = ISPSTAT_DISABLED;
	else if (stat->state == ISPSTAT_ENABLING && pcr_enable)
		stat->state = ISPSTAT_ENABLED;
}

void omap3isp_stat_suspend(struct ispstat *stat)
{
	unsigned long flags;

	spin_lock_irqsave(&stat->isp->stat_lock, flags);

	if (stat->state != ISPSTAT_DISABLED)
		stat->ops->enable(stat, 0);
	if (stat->state == ISPSTAT_ENABLED)
		stat->state = ISPSTAT_SUSPENDED;

	spin_unlock_irqrestore(&stat->isp->stat_lock, flags);
}

void omap3isp_stat_resume(struct ispstat *stat)
{
	/* Module will be re-enabled with its pipeline */
	if (stat->state == ISPSTAT_SUSPENDED)
		stat->state = ISPSTAT_ENABLING;
}

static void isp_stat_try_enable(struct ispstat *stat)
{
	unsigned long irqflags;

	if (stat->priv == NULL)
		/* driver wasn't initialised */
		return;

	spin_lock_irqsave(&stat->isp->stat_lock, irqflags);
	if (stat->state == ISPSTAT_ENABLING && !stat->buf_processing &&
	    stat->buf_alloc_size) {
		/*
		 * Userspace's requested to enable the engine but it wasn't yet.
		 * Let's do that now.
		 */
		stat->update = 1;
		isp_stat_buf_next(stat);
		stat->ops->setup_regs(stat, stat->priv);
		isp_stat_buf_insert_magic(stat, stat->active_buf);

		/*
		 * H3A module has some hw issues which forces the driver to
		 * ignore next buffers even if it was disabled in the meantime.
		 * On the other hand, Histogram shouldn't ignore buffers anymore
		 * if it's being enabled.
		 */
		if (!IS_H3A(stat))
			atomic_set(&stat->buf_err, 0);

		isp_stat_pcr_enable(stat, 1);
		spin_unlock_irqrestore(&stat->isp->stat_lock, irqflags);
		dev_dbg(stat->isp->dev, "%s: module is enabled.\n",
			stat->subdev.name);
	} else {
		spin_unlock_irqrestore(&stat->isp->stat_lock, irqflags);
	}
}

void omap3isp_stat_isr_frame_sync(struct ispstat *stat)
{
	isp_stat_try_enable(stat);
}

void omap3isp_stat_sbl_overflow(struct ispstat *stat)
{
	unsigned long irqflags;

	spin_lock_irqsave(&stat->isp->stat_lock, irqflags);
	/*
	 * Due to a H3A hw issue which prevents the next buffer to start from
	 * the correct memory address, 2 buffers must be ignored.
	 */
	atomic_set(&stat->buf_err, 2);

	/*
	 * If more than one SBL overflow happen in a row, H3A module may access
	 * invalid memory region.
	 * stat->sbl_ovl_recover is set to tell to the driver to temporarily use
	 * a soft configuration which helps to avoid consecutive overflows.
	 */
	if (stat->recover_priv)
		stat->sbl_ovl_recover = 1;
	spin_unlock_irqrestore(&stat->isp->stat_lock, irqflags);
}

/*
 * omap3isp_stat_enable - Disable/Enable statistic engine as soon as possible
 * @enable: 0/1 - Disables/Enables the engine.
 *
 * Client should configure all the module registers before this.
 * This function can be called from a userspace request.
 */
int omap3isp_stat_enable(struct ispstat *stat, u8 enable)
{
	unsigned long irqflags;

	dev_dbg(stat->isp->dev, "%s: user wants to %s module.\n",
		stat->subdev.name, enable ? "enable" : "disable");

	/* Prevent enabling while configuring */
	mutex_lock(&stat->ioctl_lock);

	spin_lock_irqsave(&stat->isp->stat_lock, irqflags);

	if (!stat->configured && enable) {
		spin_unlock_irqrestore(&stat->isp->stat_lock, irqflags);
		mutex_unlock(&stat->ioctl_lock);
		dev_dbg(stat->isp->dev,
			"%s: cannot enable module as it's never been successfully configured so far.\n",
			stat->subdev.name);
		return -EINVAL;
	}

	if (enable) {
		if (stat->state == ISPSTAT_DISABLING)
			/* Previous disabling request wasn't done yet */
			stat->state = ISPSTAT_ENABLED;
		else if (stat->state == ISPSTAT_DISABLED)
			/* Module is now being enabled */
			stat->state = ISPSTAT_ENABLING;
	} else {
		if (stat->state == ISPSTAT_ENABLING) {
			/* Previous enabling request wasn't done yet */
			stat->state = ISPSTAT_DISABLED;
		} else if (stat->state == ISPSTAT_ENABLED) {
			/* Module is now being disabled */
			stat->state = ISPSTAT_DISABLING;
			isp_stat_buf_clear(stat);
		}
	}

	spin_unlock_irqrestore(&stat->isp->stat_lock, irqflags);
	mutex_unlock(&stat->ioctl_lock);

	return 0;
}

int omap3isp_stat_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct ispstat *stat = v4l2_get_subdevdata(subdev);

	if (enable) {
		/*
		 * Only set enable PCR bit if the module was previously
		 * enabled through ioctl.
		 */
		isp_stat_try_enable(stat);
	} else {
		unsigned long flags;
		/* Disable PCR bit and config enable field */
		omap3isp_stat_enable(stat, 0);
		spin_lock_irqsave(&stat->isp->stat_lock, flags);
		stat->ops->enable(stat, 0);
		spin_unlock_irqrestore(&stat->isp->stat_lock, flags);

		/*
		 * If module isn't busy, a new interrupt may come or not to
		 * set the state to DISABLED. As Histogram needs to read its
		 * internal memory to clear it, let interrupt handler
		 * responsible of changing state to DISABLED. If the last
		 * interrupt is coming, it's still safe as the handler will
		 * ignore the second time when state is already set to DISABLED.
		 * It's necessary to synchronize Histogram with streamoff, once
		 * the module may be considered idle before last SDMA transfer
		 * starts if we return here.
		 */
		if (!omap3isp_stat_pcr_busy(stat))
			omap3isp_stat_isr(stat);

		dev_dbg(stat->isp->dev, "%s: module is being disabled\n",
			stat->subdev.name);
	}

	return 0;
}

/*
 * __stat_isr - Interrupt handler for statistic drivers
 */
static void __stat_isr(struct ispstat *stat, int from_dma)
{
	int ret = STAT_BUF_DONE;
	int buf_processing;
	unsigned long irqflags;
	struct isp_pipeline *pipe;

	/*
	 * stat->buf_processing must be set before disable module. It's
	 * necessary to not inform too early the buffers aren't busy in case
	 * of SDMA is going to be used.
	 */
	spin_lock_irqsave(&stat->isp->stat_lock, irqflags);
	if (stat->state == ISPSTAT_DISABLED) {
		spin_unlock_irqrestore(&stat->isp->stat_lock, irqflags);
		return;
	}
	buf_processing = stat->buf_processing;
	stat->buf_processing = 1;
	stat->ops->enable(stat, 0);

	if (buf_processing && !from_dma) {
		if (stat->state == ISPSTAT_ENABLED) {
			spin_unlock_irqrestore(&stat->isp->stat_lock, irqflags);
			dev_err(stat->isp->dev,
				"%s: interrupt occurred when module was still processing a buffer.\n",
				stat->subdev.name);
			ret = STAT_NO_BUF;
			goto out;
		} else {
			/*
			 * Interrupt handler was called from streamoff when
			 * the module wasn't busy anymore to ensure it is being
			 * disabled after process last buffer. If such buffer
			 * processing has already started, no need to do
			 * anything else.
			 */
			spin_unlock_irqrestore(&stat->isp->stat_lock, irqflags);
			return;
		}
	}
	spin_unlock_irqrestore(&stat->isp->stat_lock, irqflags);

	/* If it's busy we can't process this buffer anymore */
	if (!omap3isp_stat_pcr_busy(stat)) {
		if (!from_dma && stat->ops->buf_process)
			/* Module still need to copy data to buffer. */
			ret = stat->ops->buf_process(stat);
		if (ret == STAT_BUF_WAITING_DMA)
			/* Buffer is not ready yet */
			return;

		spin_lock_irqsave(&stat->isp->stat_lock, irqflags);

		/*
		 * Histogram needs to read its internal memory to clear it
		 * before be disabled. For that reason, common statistic layer
		 * can return only after call stat's buf_process() operator.
		 */
		if (stat->state == ISPSTAT_DISABLING) {
			stat->state = ISPSTAT_DISABLED;
			spin_unlock_irqrestore(&stat->isp->stat_lock, irqflags);
			stat->buf_processing = 0;
			return;
		}
		pipe = to_isp_pipeline(&stat->subdev.entity);
		stat->frame_number = atomic_read(&pipe->frame_number);

		/*
		 * Before this point, 'ret' stores the buffer's status if it's
		 * ready to be processed. Afterwards, it holds the status if
		 * it was processed successfully.
		 */
		ret = isp_stat_buf_process(stat, ret);

		if (likely(!stat->sbl_ovl_recover)) {
			stat->ops->setup_regs(stat, stat->priv);
		} else {
			/*
			 * Using recover config to increase the chance to have
			 * a good buffer processing and make the H3A module to
			 * go back to a valid state.
			 */
			stat->update = 1;
			stat->ops->setup_regs(stat, stat->recover_priv);
			stat->sbl_ovl_recover = 0;

			/*
			 * Set 'update' in case of the module needs to use
			 * regular configuration after next buffer.
			 */
			stat->update = 1;
		}

		isp_stat_buf_insert_magic(stat, stat->active_buf);

		/*
		 * Hack: H3A modules may access invalid memory address or send
		 * corrupted data to userspace if more than 1 SBL overflow
		 * happens in a row without re-writing its buffer's start memory
		 * address in the meantime. Such situation is avoided if the
		 * module is not immediately re-enabled when the ISR misses the
		 * timing to process the buffer and to setup the registers.
		 * Because of that, pcr_enable(1) was moved to inside this 'if'
		 * block. But the next interruption will still happen as during
		 * pcr_enable(0) the module was busy.
		 */
		isp_stat_pcr_enable(stat, 1);
		spin_unlock_irqrestore(&stat->isp->stat_lock, irqflags);
	} else {
		/*
		 * If a SBL overflow occurs and the H3A driver misses the timing
		 * to process the buffer, stat->buf_err is set and won't be
		 * cleared now. So the next buffer will be correctly ignored.
		 * It's necessary due to a hw issue which makes the next H3A
		 * buffer to start from the memory address where the previous
		 * one stopped, instead of start where it was configured to.
		 * Do not "stat->buf_err = 0" here.
		 */

		if (stat->ops->buf_process)
			/*
			 * Driver may need to erase current data prior to
			 * process a new buffer. If it misses the timing, the
			 * next buffer might be wrong. So should be ignored.
			 * It happens only for Histogram.
			 */
			atomic_set(&stat->buf_err, 1);

		ret = STAT_NO_BUF;
		dev_dbg(stat->isp->dev,
			"%s: cannot process buffer, device is busy.\n",
			stat->subdev.name);
	}

out:
	stat->buf_processing = 0;
	isp_stat_queue_event(stat, ret != STAT_BUF_DONE);
}

void omap3isp_stat_isr(struct ispstat *stat)
{
	__stat_isr(stat, 0);
}

void omap3isp_stat_dma_isr(struct ispstat *stat)
{
	__stat_isr(stat, 1);
}

int omap3isp_stat_subscribe_event(struct v4l2_subdev *subdev,
				  struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub)
{
	struct ispstat *stat = v4l2_get_subdevdata(subdev);

	if (sub->type != stat->event_type)
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, STAT_NEVENTS, NULL);
}

int omap3isp_stat_unsubscribe_event(struct v4l2_subdev *subdev,
				    struct v4l2_fh *fh,
				    struct v4l2_event_subscription *sub)
{
	return v4l2_event_unsubscribe(fh, sub);
}

void omap3isp_stat_unregister_entities(struct ispstat *stat)
{
	v4l2_device_unregister_subdev(&stat->subdev);
}

int omap3isp_stat_register_entities(struct ispstat *stat,
				    struct v4l2_device *vdev)
{
	return v4l2_device_register_subdev(vdev, &stat->subdev);
}

static int isp_stat_init_entities(struct ispstat *stat, const char *name,
				  const struct v4l2_subdev_ops *sd_ops)
{
	struct v4l2_subdev *subdev = &stat->subdev;
	struct media_entity *me = &subdev->entity;

	v4l2_subdev_init(subdev, sd_ops);
	snprintf(subdev->name, V4L2_SUBDEV_NAME_SIZE, "OMAP3 ISP %s", name);
	subdev->grp_id = 1 << 16;	/* group ID for isp subdevs */
	subdev->flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;
	v4l2_set_subdevdata(subdev, stat);

	stat->pad.flags = MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	me->ops = NULL;

	return media_entity_pads_init(me, 1, &stat->pad);
}

int omap3isp_stat_init(struct ispstat *stat, const char *name,
		       const struct v4l2_subdev_ops *sd_ops)
{
	int ret;

	stat->buf = kcalloc(STAT_MAX_BUFS, sizeof(*stat->buf), GFP_KERNEL);
	if (!stat->buf)
		return -ENOMEM;

	isp_stat_buf_clear(stat);
	mutex_init(&stat->ioctl_lock);
	atomic_set(&stat->buf_err, 0);

	ret = isp_stat_init_entities(stat, name, sd_ops);
	if (ret < 0) {
		mutex_destroy(&stat->ioctl_lock);
		kfree(stat->buf);
	}

	return ret;
}

void omap3isp_stat_cleanup(struct ispstat *stat)
{
	media_entity_cleanup(&stat->subdev.entity);
	mutex_destroy(&stat->ioctl_lock);
	isp_stat_bufs_free(stat);
	kfree(stat->buf);
}
