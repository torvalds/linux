// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2013, 2015, 2017-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/* BAM-DMA Manager. */

#ifdef CONFIG_SPS_SUPPORT_BAMDMA

#include <linux/export.h>
#include <linux/memory.h>

#include "spsi.h"
#include "bam.h"
#include "sps_bam.h"
#include "sps_core.h"

/**
 * registers
 */

#define DMA_ENBL			(0x00000000)
#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
#define DMA_REVISION			(0x00000004)
#define DMA_CONFIG			(0x00000008)
#define DMA_CHNL_CONFIG(n)		(0x00001000 + 4096 * (n))
#else
#define DMA_CHNL_CONFIG(n)		(0x00000004 + 4 * (n))
#define DMA_CONFIG			(0x00000040)
#endif

/**
 * masks
 */

/* DMA_CHNL_confign */
#ifdef CONFIG_SPS_SUPPORT_NDP_BAM
#define DMA_CHNL_PRODUCER_PIPE_ENABLED	0x40000
#define DMA_CHNL_CONSUMER_PIPE_ENABLED	0x20000
#endif
#define DMA_CHNL_HALT_DONE		0x10000
#define DMA_CHNL_HALT			0x1000
#define DMA_CHNL_ENABLE                 0x100
#define DMA_CHNL_ACT_THRESH             0x30
#define DMA_CHNL_WEIGHT                 0x7

/* DMA_CONFIG */
#define TESTBUS_SELECT                  0x3

/**
 *
 * Write register with debug info.
 *
 * @base - bam base virtual address.
 * @offset - register offset.
 * @val - value to write.
 *
 */
static inline void dma_write_reg(void *base, u32 offset, u32 val)
{
	iowrite32(val, base + offset);
	SPS_DBG(sps, "sps:bamdma: write reg 0x%x w_val 0x%x\n", offset, val);
}

/**
 * Write register masked field with debug info.
 *
 * @base - bam base virtual address.
 * @offset - register offset.
 * @mask - register bitmask.
 * @val - value to write.
 *
 */
static inline void dma_write_reg_field(void *base, u32 offset,
				       const u32 mask, u32 val)
{
	unsigned long lmask = mask;
	u32 shift = find_first_bit(&lmask, 32);
	u32 tmp = ioread32(base + offset);

	tmp &= ~mask;		/* clear written bits */
	val = tmp | (val << shift);
	iowrite32(val, base + offset);
	SPS_DBG(sps, "sps:bamdma: write reg 0x%x w_val 0x%x\n", offset, val);
}

/* Round max number of pipes to nearest multiple of 2 */
#define DMA_MAX_PIPES         ((BAM_MAX_PIPES / 2) * 2)

/* Maximum number of BAM-DMAs supported */
#define MAX_BAM_DMA_DEVICES   1

/* Maximum number of BAMs that will be registered */
#define MAX_BAM_DMA_BAMS      1

/* Pipe enable check values */
#define DMA_PIPES_STATE_DIFF     0
#define DMA_PIPES_BOTH_DISABLED  1
#define DMA_PIPES_BOTH_ENABLED   2

/* Even pipe is tx/dest/input/write, odd pipe is rx/src/output/read */
#define DMA_PIPE_IS_DEST(p)   (((p) & 1) == 0)
#define DMA_PIPE_IS_SRC(p)    (((p) & 1) != 0)

/* BAM DMA pipe state */
enum bamdma_pipe_state {
	PIPE_INACTIVE = 0,
	PIPE_ACTIVE
};

/* BAM DMA channel state */
enum bamdma_chan_state {
	DMA_CHAN_STATE_FREE = 0,
	DMA_CHAN_STATE_ALLOC_EXT,	/* Client allocation */
	DMA_CHAN_STATE_ALLOC_INT	/* Internal (resource mgr) allocation */
};

struct bamdma_chan {
	/* Allocation state */
	enum bamdma_chan_state state;

	/* BAM DMA channel configuration parameters */
	u32 threshold;
	enum sps_dma_priority priority;

	/* HWIO channel configuration parameters */
	enum bam_dma_thresh_dma thresh;
	enum bam_dma_weight_dma weight;

};

/* BAM DMA device state */
struct bamdma_device {
	/* BAM-DMA device state */
	int enabled;
	int local;

	/* BAM device state */
	struct sps_bam *bam;

	/* BAM handle, for deregistration */
	unsigned long h;

	/* BAM DMA device virtual mapping */
	void *virt_addr;
	int virtual_mapped;
	phys_addr_t phys_addr;
	void *hwio;

	/* BAM DMA pipe/channel state */
	u32 num_pipes;
	enum bamdma_pipe_state pipes[DMA_MAX_PIPES];
	struct bamdma_chan chans[DMA_MAX_PIPES / 2];

};

/* BAM-DMA devices */
static struct bamdma_device bam_dma_dev[MAX_BAM_DMA_DEVICES];
static struct mutex bam_dma_lock;

/*
 * The BAM DMA module registers all BAMs in the BSP properties, but only
 * uses the first BAM-DMA device for allocations.  References to the others
 * are stored in the following data array.
 */
static int num_bams;
static unsigned long bam_handles[MAX_BAM_DMA_BAMS];

/**
 * Find BAM-DMA device
 *
 * This function finds the BAM-DMA device associated with the BAM handle.
 *
 * @h - BAM handle
 *
 * @return - pointer to BAM-DMA device, or NULL on error
 *
 */
static struct bamdma_device *sps_dma_find_device(unsigned long h)
{
	return &bam_dma_dev[0];
}

/**
 * BAM DMA device enable
 *
 * This function enables a BAM DMA device and the associated BAM.
 *
 * @dev - pointer to BAM DMA device context
 *
 * @return 0 on success, negative value on error
 *
 */
static int sps_dma_device_enable(struct bamdma_device *dev)
{
	if (dev->enabled)
		return 0;

	/*
	 *  If the BAM-DMA device is locally controlled then enable BAM-DMA
	 *  device
	 */
	if (dev->local)
		dma_write_reg(dev->virt_addr, DMA_ENBL, 1);

	/* Enable BAM device */
	if (sps_bam_enable(dev->bam)) {
		SPS_ERR(sps, "sps:Failed to enable BAM DMA's BAM: %pa\n",
			&dev->phys_addr);
		return SPS_ERROR;
	}

	dev->enabled = true;

	return 0;
}

/**
 * BAM DMA device enable
 *
 * This function initializes a BAM DMA device.
 *
 * @dev - pointer to BAM DMA device context
 *
 * @return 0 on success, negative value on error
 *
 */
static int sps_dma_device_disable(struct bamdma_device *dev)
{
	u32 pipe_index;

	if (!dev->enabled)
		return 0;

	/* Do not disable if channels active */
	for (pipe_index = 0; pipe_index < dev->num_pipes; pipe_index++) {
		if (dev->pipes[pipe_index] != PIPE_INACTIVE)
			break;
	}

	if (pipe_index < dev->num_pipes) {
		SPS_ERR(sps,
			"sps:Fail to disable BAM-DMA %pa:channels are active\n",
			&dev->phys_addr);
		return SPS_ERROR;
	}

	dev->enabled = false;

	/* Disable BAM device */
	if (sps_bam_disable(dev->bam)) {
		SPS_ERR(sps,
			"sps:Fail to disable BAM-DMA BAM:%pa\n",
				&dev->phys_addr);
		return SPS_ERROR;
	}

	/* Is the BAM-DMA device locally controlled? */
	if (dev->local)
		/* Disable BAM-DMA device */
		dma_write_reg(dev->virt_addr, DMA_ENBL, 0);

	return 0;
}

/**
 * Initialize BAM DMA device
 *
 */
int sps_dma_device_init(unsigned long h)
{
	struct bamdma_device *dev;
	struct sps_bam_props *props;
	int result = SPS_ERROR;

	mutex_lock(&bam_dma_lock);

	/* Find a free BAM-DMA device slot */
	dev = NULL;
	if (bam_dma_dev[0].bam != NULL) {
		SPS_ERR(sps,
			"sps:%s:BAM-DMA BAM device is already initialized\n",
			__func__);
		goto exit_err;
	} else {
		dev = &bam_dma_dev[0];
	}

	/* Record BAM */
	memset(dev, 0, sizeof(*dev));
	dev->h = h;
	dev->bam = sps_h2bam(h);

	if (dev->bam == NULL) {
		SPS_ERR(sps,
			"sps:%s:BAM-DMA BAM device is not found from the handle\n",
			__func__);
		goto exit_err;
	}

	/* Map the BAM DMA device into virtual space, if necessary */
	props = &dev->bam->props;
	dev->phys_addr = props->periph_phys_addr;
	if (props->periph_virt_addr != NULL) {
		dev->virt_addr = props->periph_virt_addr;
		dev->virtual_mapped = false;
	} else {
		if (props->periph_virt_size == 0) {
			SPS_ERR(sps,
				"sps:Unable to map BAM DMA IO memory: %pa %x\n",
				&dev->phys_addr, props->periph_virt_size);
			goto exit_err;
		}

		dev->virt_addr = ioremap(dev->phys_addr,
					  props->periph_virt_size);
		if (dev->virt_addr == NULL) {
			SPS_ERR(sps,
				"sps:Unable to map BAM DMA IO memory: %pa %x\n",
				&dev->phys_addr, props->periph_virt_size);
			goto exit_err;
		}
		dev->virtual_mapped = true;
	}
	dev->hwio = (void *) dev->virt_addr;

	/* Is the BAM-DMA device locally controlled? */
	if ((props->manage & SPS_BAM_MGR_DEVICE_REMOTE) == 0) {
		SPS_DBG3(sps, "sps:BAM-DMA is controlled locally: %pa\n",
			&dev->phys_addr);
		dev->local = true;
	} else {
		SPS_DBG3(sps, "sps:BAM-DMA is controlled remotely: %pa\n",
			&dev->phys_addr);
		dev->local = false;
	}

	/*
	 * Enable the BAM DMA and determine the number of pipes/channels.
	 * Leave the BAM-DMA enabled, since it is always a shared device.
	 */
	if (sps_dma_device_enable(dev))
		goto exit_err;

	dev->num_pipes = dev->bam->props.num_pipes;

	result = 0;
exit_err:
	if (result) {
		if (dev != NULL) {
			if (dev->virtual_mapped)
				iounmap(dev->virt_addr);

			dev->bam = NULL;
		}
	}

	mutex_unlock(&bam_dma_lock);

	return result;
}

/**
 * De-initialize BAM DMA device
 *
 */
int sps_dma_device_de_init(unsigned long h)
{
	struct bamdma_device *dev;
	u32 pipe_index;
	u32 chan;
	int result = 0;

	mutex_lock(&bam_dma_lock);

	dev = sps_dma_find_device(h);
	if (dev == NULL) {
		SPS_ERR(sps, "sps:BAM-DMA: not registered: %pK\n", (void *)h);
		result = SPS_ERROR;
		goto exit_err;
	}

	/* Check for channel leaks */
	for (chan = 0; chan < dev->num_pipes / 2; chan++) {
		if (dev->chans[chan].state != DMA_CHAN_STATE_FREE) {
			SPS_ERR(sps, "sps:BAM-DMA: channel not free: %d\n",
					chan);
			result = SPS_ERROR;
			dev->chans[chan].state = DMA_CHAN_STATE_FREE;
		}
	}
	for (pipe_index = 0; pipe_index < dev->num_pipes; pipe_index++) {
		if (dev->pipes[pipe_index] != PIPE_INACTIVE) {
			SPS_ERR(sps, "sps:BAM-DMA: pipe not inactive: %d\n",
					pipe_index);
			result = SPS_ERROR;
			dev->pipes[pipe_index] = PIPE_INACTIVE;
		}
	}

	/* Disable BAM and BAM-DMA */
	if (sps_dma_device_disable(dev))
		result = SPS_ERROR;

	dev->h = BAM_HANDLE_INVALID;
	dev->bam = NULL;
	if (dev->virtual_mapped)
		iounmap(dev->virt_addr);

exit_err:
	mutex_unlock(&bam_dma_lock);

	return result;
}

/**
 * Initialize BAM DMA module
 *
 */
int sps_dma_init(const struct sps_bam_props *bam_props)
{
	struct sps_bam_props props;
	const struct sps_bam_props *bam_reg;
	unsigned long h;

	/* Init local data */
	memset(&bam_dma_dev, 0, sizeof(bam_dma_dev));
	num_bams = 0;
	memset(bam_handles, 0, sizeof(bam_handles));

	/* Create a mutex to control access to the BAM-DMA devices */
	mutex_init(&bam_dma_lock);

	/* Are there any BAM DMA devices? */
	if (bam_props == NULL)
		return 0;

	/*
	 * Registers all BAMs in the BSP properties, but only uses the first
	 * BAM-DMA device for allocations.
	 */
	if (bam_props->phys_addr) {
		/* Force multi-EE option for all BAM-DMAs */
		bam_reg = bam_props;
		if ((bam_props->options & SPS_BAM_OPT_BAMDMA) &&
		    (bam_props->manage & SPS_BAM_MGR_MULTI_EE) == 0) {
			SPS_DBG(sps,
				"sps:Setting multi-EE options for BAM-DMA: %pa\n",
				&bam_props->phys_addr);
			props = *bam_props;
			props.manage |= SPS_BAM_MGR_MULTI_EE;
			bam_reg = &props;
		}

		/* Register the BAM */
		if (sps_register_bam_device(bam_reg, &h)) {
			SPS_ERR(sps,
				"sps:Fail to register BAM-DMA BAM device: phys %pa\n",
				&bam_props->phys_addr);
			return SPS_ERROR;
		}

		/* Record the BAM so that it may be deregistered later */
		if (num_bams < MAX_BAM_DMA_BAMS) {
			bam_handles[num_bams] = h;
			num_bams++;
		} else {
			SPS_ERR(sps, "sps:BAM-DMA: BAM limit exceeded: %d\n",
					num_bams);
			return SPS_ERROR;
		}
	} else {
		SPS_ERR(sps,
			"sps:%s:BAM-DMA phys_addr is zero\n",
			__func__);
		return SPS_ERROR;
	}


	return 0;
}

/**
 * De-initialize BAM DMA module
 *
 */
void sps_dma_de_init(void)
{
	int n;

	/* De-initialize the BAM devices */
	for (n = 0; n < num_bams; n++)
		sps_deregister_bam_device(bam_handles[n]);

	/* Clear local data */
	memset(&bam_dma_dev, 0, sizeof(bam_dma_dev));
	num_bams = 0;
	memset(bam_handles, 0, sizeof(bam_handles));
}

/**
 * Allocate a BAM DMA channel
 *
 */
int sps_alloc_dma_chan(const struct sps_alloc_dma_chan *alloc,
		       struct sps_dma_chan *chan_info)
{
	struct bamdma_device *dev;
	struct bamdma_chan *chan;
	u32 pipe_index;
	enum bam_dma_thresh_dma thresh = (enum bam_dma_thresh_dma) 0;
	enum bam_dma_weight_dma weight = (enum bam_dma_weight_dma) 0;
	int result = SPS_ERROR;

	if (alloc == NULL || chan_info == NULL) {
		SPS_ERR(sps,
			"sps:%s:invalid parameters\n", __func__);
		return SPS_ERROR;
	}

	/* Translate threshold and priority to hwio values */
	if (alloc->threshold != SPS_DMA_THRESHOLD_DEFAULT) {
		if (alloc->threshold >= 512)
			thresh = BAM_DMA_THRESH_512;
		else if (alloc->threshold >= 256)
			thresh = BAM_DMA_THRESH_256;
		else if (alloc->threshold >= 128)
			thresh = BAM_DMA_THRESH_128;
		else
			thresh = BAM_DMA_THRESH_64;
	}

	weight = alloc->priority;

	if ((u32)alloc->priority > (u32)BAM_DMA_WEIGHT_HIGH) {
		SPS_ERR(sps, "sps:BAM-DMA: invalid priority: %x\n",
						alloc->priority);
		return SPS_ERROR;
	}

	mutex_lock(&bam_dma_lock);

	dev = sps_dma_find_device(alloc->dev);
	if (dev == NULL) {
		SPS_ERR(sps, "sps:BAM-DMA: invalid BAM handle: %pK\n",
							(void *)alloc->dev);
		goto exit_err;
	}

	/* Search for a free set of pipes */
	for (pipe_index = 0, chan = dev->chans;
	      pipe_index < dev->num_pipes; pipe_index += 2, chan++) {
		if (chan->state == DMA_CHAN_STATE_FREE) {
			/* Just check pipes for safety */
			if (dev->pipes[pipe_index] != PIPE_INACTIVE ||
			    dev->pipes[pipe_index + 1] != PIPE_INACTIVE) {
				SPS_ERR(sps,
					"sps:BAM-DMA: channel %d state error:%d %d\n",
					pipe_index / 2, dev->pipes[pipe_index],
				 dev->pipes[pipe_index + 1]);
				goto exit_err;
			}
			break; /* Found free pipe */
		}
	}

	if (pipe_index >= dev->num_pipes) {
		SPS_ERR(sps, "sps:BAM-DMA: no free channel num_pipes = %d\n",
			dev->num_pipes);
		goto exit_err;
	}

	chan->state = DMA_CHAN_STATE_ALLOC_EXT;

	/* Store config values for use when pipes are activated */
	chan = &dev->chans[pipe_index / 2];
	chan->threshold = alloc->threshold;
	chan->thresh = thresh;
	chan->priority = alloc->priority;
	chan->weight = weight;

	SPS_DBG3(sps, "sps:%s. pipe %d\n", __func__, pipe_index);

	/* Report allocated pipes to client */
	chan_info->dev = dev->h;
	/* Dest/input/write pipex */
	chan_info->dest_pipe_index = pipe_index;
	/* Source/output/read pipe */
	chan_info->src_pipe_index = pipe_index + 1;

	result = 0;
exit_err:
	mutex_unlock(&bam_dma_lock);

	return result;
}
EXPORT_SYMBOL(sps_alloc_dma_chan);

/**
 * Free a BAM DMA channel
 *
 */
int sps_free_dma_chan(struct sps_dma_chan *chan)
{
	struct bamdma_device *dev;
	u32 pipe_index;
	int result = 0;

	if (chan == NULL) {
		SPS_ERR(sps,
			"sps:%s:chan is NULL\n", __func__);
		return SPS_ERROR;
	}

	mutex_lock(&bam_dma_lock);

	dev = sps_dma_find_device(chan->dev);
	if (dev == NULL) {
		SPS_ERR(sps, "sps:BAM-DMA: invalid BAM handle: %pK\n",
			(void *)chan->dev);
		result = SPS_ERROR;
		goto exit_err;
	}

	/* Verify the pipe indices */
	pipe_index = chan->dest_pipe_index;
	if (pipe_index >= dev->num_pipes || ((pipe_index & 1)) ||
	    (pipe_index + 1) != chan->src_pipe_index) {
		SPS_ERR(sps,
			"sps:%s. Invalid pipe indices num_pipes=%d dest=%d src=%d\n",
			__func__, dev->num_pipes,
			chan->dest_pipe_index,
			chan->src_pipe_index);
		result = SPS_ERROR;
		goto exit_err;
	}

	/* Are both pipes inactive? */
	if (dev->chans[pipe_index / 2].state != DMA_CHAN_STATE_ALLOC_EXT ||
	    dev->pipes[pipe_index] != PIPE_INACTIVE ||
	    dev->pipes[pipe_index + 1] != PIPE_INACTIVE) {
		SPS_ERR(sps,
			"sps:BAM-DMA: attempt to free active chan %d: %d %d\n",
			pipe_index / 2, dev->pipes[pipe_index],
			dev->pipes[pipe_index + 1]);
		result = SPS_ERROR;
		goto exit_err;
	}

	/* Free the channel */
	dev->chans[pipe_index / 2].state = DMA_CHAN_STATE_FREE;

exit_err:
	mutex_unlock(&bam_dma_lock);

	return result;
}
EXPORT_SYMBOL(sps_free_dma_chan);

/**
 * Activate a BAM DMA pipe
 *
 * This function activates a BAM DMA pipe.
 *
 * @dev - pointer to BAM-DMA device descriptor
 *
 * @pipe_index - pipe index
 *
 * @return 0 on success, negative value on error
 *
 */
static u32 sps_dma_check_pipes(struct bamdma_device *dev, u32 pipe_index)
{
	u32 pipe_in;
	u32 pipe_out;
	int enabled_in;
	int enabled_out;
	u32 check;

	pipe_in = pipe_index & ~1;
	pipe_out = pipe_in + 1;
	enabled_in = bam_pipe_is_enabled(&dev->bam->base, pipe_in);
	enabled_out = bam_pipe_is_enabled(&dev->bam->base, pipe_out);

	if (!enabled_in && !enabled_out)
		check = DMA_PIPES_BOTH_DISABLED;
	else if (enabled_in && enabled_out)
		check = DMA_PIPES_BOTH_ENABLED;
	else
		check = DMA_PIPES_STATE_DIFF;

	return check;
}

/**
 * Allocate a BAM DMA pipe
 *
 */
int sps_dma_pipe_alloc(void *bam_arg, u32 pipe_index, enum sps_mode dir)
{
	struct sps_bam *bam = bam_arg;
	struct bamdma_device *dev;
	struct bamdma_chan *chan;
	u32 channel;
	int result = SPS_ERROR;

	if (bam == NULL) {
		SPS_ERR(sps, "%s\n", "sps:BAM context is NULL");
		return SPS_ERROR;
	}

	/* Check pipe direction */
	if ((DMA_PIPE_IS_DEST(pipe_index) && dir != SPS_MODE_DEST) ||
	    (DMA_PIPE_IS_SRC(pipe_index) && dir != SPS_MODE_SRC)) {
		SPS_ERR(sps, "sps:BAM-DMA: wrong dir for BAM %pa pipe %d\n",
			&bam->props.phys_addr, pipe_index);
		return SPS_ERROR;
	}

	mutex_lock(&bam_dma_lock);

	dev = sps_dma_find_device((unsigned long) bam);
	if (dev == NULL) {
		SPS_ERR(sps, "sps:BAM-DMA: invalid BAM: %pa\n",
			&bam->props.phys_addr);
		goto exit_err;
	}
	if (pipe_index >= dev->num_pipes) {
		SPS_ERR(sps, "sps:BAM-DMA: BAM %pa invalid pipe: %d\n",
			&bam->props.phys_addr, pipe_index);
		goto exit_err;
	}
	if (dev->pipes[pipe_index] != PIPE_INACTIVE) {
		SPS_ERR(sps, "sps:BAM-DMA: BAM %pa pipe %d already active\n",
			&bam->props.phys_addr, pipe_index);
		goto exit_err;
	}

	/* Mark pipe active */
	dev->pipes[pipe_index] = PIPE_ACTIVE;

	/* If channel is not allocated, make an internal allocation */
	channel = pipe_index / 2;
	chan = &dev->chans[channel];
	if (chan->state != DMA_CHAN_STATE_ALLOC_EXT &&
	    chan->state != DMA_CHAN_STATE_ALLOC_INT) {
		chan->state = DMA_CHAN_STATE_ALLOC_INT;
	}

	result = 0;
exit_err:
	mutex_unlock(&bam_dma_lock);

	return result;
}

/**
 * Enable a BAM DMA pipe
 *
 */
int sps_dma_pipe_enable(void *bam_arg, u32 pipe_index)
{
	struct sps_bam *bam = bam_arg;
	struct bamdma_device *dev;
	struct bamdma_chan *chan;
	u32 channel;
	int result = SPS_ERROR;

	SPS_DBG3(sps, "sps:%s pipe %d\n", __func__, pipe_index);

	mutex_lock(&bam_dma_lock);

	dev = sps_dma_find_device((unsigned long) bam);
	if (dev == NULL) {
		SPS_ERR(sps, "sps:%s:BAM-DMA: invalid BAM\n", __func__);
		goto exit_err;
	}
	if (pipe_index >= dev->num_pipes) {
		SPS_ERR(sps, "sps:BAM-DMA: BAM %pa invalid pipe: %d\n",
			&bam->props.phys_addr, pipe_index);
		goto exit_err;
	}
	if (dev->pipes[pipe_index] != PIPE_ACTIVE) {
		SPS_ERR(sps, "sps:BAM-DMA: BAM %pa pipe %d not active\n",
			&bam->props.phys_addr, pipe_index);
		goto exit_err;
	}

      /*
       * The channel must be enabled when the dest/input/write pipe
       * is enabled
       */
	if (DMA_PIPE_IS_DEST(pipe_index)) {
		/* Configure and enable the channel */
		channel = pipe_index / 2;
		chan = &dev->chans[channel];

		if (chan->threshold != SPS_DMA_THRESHOLD_DEFAULT)
			dma_write_reg_field(dev->virt_addr,
					    DMA_CHNL_CONFIG(channel),
					    DMA_CHNL_ACT_THRESH,
					    chan->thresh);

		if (chan->priority != SPS_DMA_PRI_DEFAULT)
			dma_write_reg_field(dev->virt_addr,
					    DMA_CHNL_CONFIG(channel),
					    DMA_CHNL_WEIGHT,
					    chan->weight);

		dma_write_reg_field(dev->virt_addr,
				    DMA_CHNL_CONFIG(channel),
				    DMA_CHNL_ENABLE, 1);
	}

	result = 0;
exit_err:
	mutex_unlock(&bam_dma_lock);

	return result;
}

/**
 * Deactivate a BAM DMA pipe
 *
 * This function deactivates a BAM DMA pipe.
 *
 * @dev - pointer to BAM-DMA device descriptor
 *
 * @bam - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @return 0 on success, negative value on error
 *
 */
static int sps_dma_deactivate_pipe_atomic(struct bamdma_device *dev,
					  struct sps_bam *bam,
					  u32 pipe_index)
{
	u32 channel;

	if (dev->bam != bam)
		return SPS_ERROR;
	if (pipe_index >= dev->num_pipes)
		return SPS_ERROR;
	if (dev->pipes[pipe_index] != PIPE_ACTIVE)
		return SPS_ERROR;	/* Pipe is not active */

	SPS_DBG3(sps, "sps:BAM-DMA: deactivate pipe %d\n", pipe_index);

	/* Mark pipe inactive */
	dev->pipes[pipe_index] = PIPE_INACTIVE;

	/*
	 * Channel must be reset when either pipe is disabled, so just always
	 * reset regardless of other pipe's state
	 */
	channel = pipe_index / 2;
	dma_write_reg_field(dev->virt_addr, DMA_CHNL_CONFIG(channel),
			    DMA_CHNL_ENABLE, 0);

	/* If the peer pipe is also inactive, reset the channel */
	if (sps_dma_check_pipes(dev, pipe_index) == DMA_PIPES_BOTH_DISABLED) {
		/* Free channel if allocated internally */
		if (dev->chans[channel].state == DMA_CHAN_STATE_ALLOC_INT)
			dev->chans[channel].state = DMA_CHAN_STATE_FREE;
	}

	return 0;
}

/**
 * Free a BAM DMA pipe
 *
 */
int sps_dma_pipe_free(void *bam_arg, u32 pipe_index)
{
	struct bamdma_device *dev;
	struct sps_bam *bam = bam_arg;
	int result;

	mutex_lock(&bam_dma_lock);

	dev = sps_dma_find_device((unsigned long) bam);
	if (dev == NULL) {
		SPS_ERR(sps, "sps:%s:BAM-DMA: invalid BAM\n", __func__);
		result = SPS_ERROR;
		goto exit_err;
	}

	result = sps_dma_deactivate_pipe_atomic(dev, bam, pipe_index);

exit_err:
	mutex_unlock(&bam_dma_lock);

	return result;
}

/**
 * Get the BAM handle for BAM-DMA.
 *
 * The BAM handle should be use as source/destination in the sps_connect().
 *
 * @return bam handle on success, zero on error
 */
unsigned long sps_dma_get_bam_handle(void)
{
	return (unsigned long)bam_dma_dev[0].bam;
}
EXPORT_SYMBOL(sps_dma_get_bam_handle);

/**
 * Free the BAM handle for BAM-DMA.
 *
 */
void sps_dma_free_bam_handle(unsigned long h)
{
}
EXPORT_SYMBOL(sps_dma_free_bam_handle);

#endif /* CONFIG_SPS_SUPPORT_BAMDMA */
