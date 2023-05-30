// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/memory.h>
#include <linux/vmalloc.h>

#include "sps_bam.h"
#include "bam.h"
#include "spsi.h"

/* All BAM global IRQ sources */
#define BAM_IRQ_ALL (BAM_DEV_IRQ_HRESP_ERROR | BAM_DEV_IRQ_ERROR |   \
	BAM_DEV_IRQ_TIMER)

/* BAM device state flags */
#define BAM_STATE_INIT     (1UL << 1)
#define BAM_STATE_IRQ      (1UL << 2)
#define BAM_STATE_ENABLED  (1UL << 3)
#define BAM_STATE_BAM2BAM  (1UL << 4)
#define BAM_STATE_MTI      (1UL << 5)
#define BAM_STATE_REMOTE   (1UL << 6)

/* Mask for valid hardware descriptor flags */
#define BAM_IOVEC_FLAG_MASK   \
	(SPS_IOVEC_FLAG_INT | SPS_IOVEC_FLAG_EOT | SPS_IOVEC_FLAG_EOB |   \
	SPS_IOVEC_FLAG_NWD | SPS_IOVEC_FLAG_CMD | SPS_IOVEC_FLAG_LOCK |   \
	SPS_IOVEC_FLAG_UNLOCK | SPS_IOVEC_FLAG_IMME)

/* Mask for invalid BAM-to-BAM pipe options */
#define BAM2BAM_O_INVALID   \
	(SPS_O_DESC_DONE | \
	 SPS_O_EOT | \
	 SPS_O_POLL | \
	 SPS_O_NO_Q | \
	 SPS_O_ACK_TRANSFERS)

/**
 * Pipe/client pointer value indicating pipe is allocated, but no client has
 * been assigned
 */
#define BAM_PIPE_UNASSIGNED   ((struct sps_pipe *)((~0x0ul) - 0x88888888))

/* Check whether pipe has been assigned */
#define BAM_PIPE_IS_ASSIGNED(p)  \
	(((p) != NULL) && ((p) != BAM_PIPE_UNASSIGNED))

/* Is MTI use supported for a specific BAM version? */
#define BAM_VERSION_MTI_SUPPORT(ver)   (ver <= 2)

/* Event option<->event translation table entry */
struct sps_bam_opt_event_table {
	enum sps_event event_id;
	enum sps_option option;
	enum bam_pipe_irq pipe_irq;
};

static const struct sps_bam_opt_event_table opt_event_table[] = {
	{SPS_EVENT_EOT, SPS_O_EOT, BAM_PIPE_IRQ_EOT},
	{SPS_EVENT_DESC_DONE, SPS_O_DESC_DONE, BAM_PIPE_IRQ_DESC_INT},
	{SPS_EVENT_WAKEUP, SPS_O_WAKEUP, BAM_PIPE_IRQ_WAKE},
	{SPS_EVENT_INACTIVE, SPS_O_INACTIVE, BAM_PIPE_IRQ_TIMER},
	{SPS_EVENT_OUT_OF_DESC, SPS_O_OUT_OF_DESC,
		BAM_PIPE_IRQ_OUT_OF_DESC},
	{SPS_EVENT_ERROR, SPS_O_ERROR, BAM_PIPE_IRQ_ERROR},
	{SPS_EVENT_RST_ERROR, SPS_O_RST_ERROR, BAM_PIPE_IRQ_RST_ERROR},
	{SPS_EVENT_HRESP_ERROR, SPS_O_HRESP_ERROR, BAM_PIPE_IRQ_HRESP_ERROR}
};

/* Pipe event source handler */
static void pipe_handler(struct sps_bam *dev,
			struct sps_pipe *pipe);

/**
 * Pipe transfer event (EOT, DESC_DONE) source handler.
 * This function is called by pipe_handler() and other functions to process the
 * descriptor FIFO.
 */
static void pipe_handler_eot(struct sps_bam *dev,
			   struct sps_pipe *pipe);

/**
 * BAM driver initialization
 */
int sps_bam_driver_init(u32 options)
{
	int n;

	/*
	 * Check that SPS_O_ and BAM_PIPE_IRQ_ values are identical.
	 * This is required so that the raw pipe IRQ status can be passed
	 * to the client in the SPS_EVENT_IRQ.
	 */
	for (n = 0; n < ARRAY_SIZE(opt_event_table); n++) {
		if ((u32)opt_event_table[n].option !=
			(u32)opt_event_table[n].pipe_irq) {
			SPS_ERR(sps, "sps:SPS_O 0x%x != HAL IRQ 0x%x\n",
				opt_event_table[n].option,
				opt_event_table[n].pipe_irq);
			return SPS_ERROR;
		}
	}

	return 0;
}

/*
 * Check BAM interrupt
 */
int sps_bam_check_irq(struct sps_bam *dev)
{
	struct sps_pipe *pipe;
	u32 source;
	unsigned long flags = 0;
	int ret = 0;

	SPS_DBG1(dev, "sps: bam=%pa\n", BAM_ID(dev));

	spin_lock_irqsave(&dev->isr_lock, flags);

	if (dev->no_serve_irq) {
		SPS_ERR(dev, "sps:IRQ from BAM %pa can't be currently served\n",
			BAM_ID(dev));
		spin_unlock_irqrestore(&dev->isr_lock, flags);
		return ret;
	}

polling:
	/* Get BAM interrupt source(s) */
	if ((dev->state & BAM_STATE_MTI) == 0) {
		u32 mask = dev->pipe_active_mask;
		enum sps_callback_case cb_case;

		source = bam_check_irq_source(&dev->base, dev->props.ee,
						mask, &cb_case);

		SPS_DBG1(dev, "sps:bam=%pa;source=0x%x;mask=0x%x\n",
				BAM_ID(dev), source, mask);

		if ((source == 0) &&
			(dev->props.options & SPS_BAM_RES_CONFIRM)) {
			SPS_DBG2(dev,
				"sps: BAM %pa has no source (source = 0x%x)\n",
				BAM_ID(dev), source);

			spin_unlock_irqrestore(&dev->isr_lock, flags);
			return SPS_ERROR;
		}

		if ((source & (1UL << 31)) && (dev->props.callback)) {
			SPS_DBG1(dev, "sps:bam=%pa;callback for case %d\n",
				BAM_ID(dev), cb_case);
			dev->props.callback(cb_case, dev->props.user);
		}

		/* Mask any non-local source */
		source &= dev->pipe_active_mask;
	} else {
		/* If MTIs are used, must poll each active pipe */
		source = dev->pipe_active_mask;

		SPS_DBG1(dev, "sps:MTI:bam=%pa;source=0x%x\n",
				BAM_ID(dev), source);
	}

	/* Process active pipe sources */
	pipe = list_first_entry(&dev->pipes_q, struct sps_pipe, list);

	list_for_each_entry(pipe, &dev->pipes_q, list) {
		/* Check this pipe's bit in the source mask */
		if (BAM_PIPE_IS_ASSIGNED(pipe)
				&& (!pipe->disconnecting)
				&& (source & pipe->pipe_index_mask)) {
			/* This pipe has an interrupt pending */
			pipe_handler(dev, pipe);
			source &= ~pipe->pipe_index_mask;
		}
		if (source == 0)
			break;
	}

	/* Process any inactive pipe sources */
	if (source) {
		SPS_ERR(dev, "sps:IRQ from BAM %pa inactive pipe(s) 0x%x\n",
			BAM_ID(dev), source);
		dev->irq_from_disabled_pipe++;
	}

	if (dev->props.options & SPS_BAM_RES_CONFIRM) {
		u32 mask = dev->pipe_active_mask;
		enum sps_callback_case cb_case;

		source = bam_check_irq_source(&dev->base, dev->props.ee,
						mask, &cb_case);

		SPS_DBG1(dev,
			"sps:check if there is any new IRQ coming:bam=%pa;source=0x%x;mask=0x%x\n",
				BAM_ID(dev), source, mask);

		if ((source & (1UL << 31)) && (dev->props.callback)) {
			SPS_DBG1(dev, "sps:bam=%pa;callback for case %d\n",
				BAM_ID(dev), cb_case);
			dev->props.callback(cb_case, dev->props.user);
		}

		if (source)
			goto polling;
	}

	spin_unlock_irqrestore(&dev->isr_lock, flags);

	return ret;
}

/**
 * BAM interrupt service routine
 *
 * This function is the BAM interrupt service routine.
 *
 * @ctxt - pointer to ISR's registered argument
 *
 * @return void
 */
static irqreturn_t bam_isr(int irq, void *ctxt)
{
	struct sps_bam *dev = ctxt;

	SPS_DBG1(dev, "sps: bam:%pa; IRQ #:%d\n", BAM_ID(dev), irq);

	if (dev->props.options & SPS_BAM_RES_CONFIRM) {
		if (dev->props.callback) {
			bool ready = false;

			dev->props.callback(SPS_CALLBACK_BAM_RES_REQ, &ready);
			if (ready) {
				SPS_DBG1(dev,
					"sps: handle IRQ for bam:%pa IRQ #:%d\n",
					BAM_ID(dev), irq);
				if (sps_bam_check_irq(dev))
					SPS_DBG2(dev,
						"sps: callback bam:%pa IRQ #:%d to poll the pipes\n",
						BAM_ID(dev), irq);
				dev->props.callback(SPS_CALLBACK_BAM_RES_REL,
							&ready);
			} else {
				SPS_DBG1(dev,
					"sps: BAM is not ready and thus skip IRQ for bam:%pa IRQ #:%d\n",
					BAM_ID(dev), irq);
			}
		} else {
			SPS_ERR(dev,
				"sps: Client of BAM %pa requires confirmation but does not register callback\n",
				BAM_ID(dev));
		}
	} else {
		sps_bam_check_irq(dev);
	}

	return IRQ_HANDLED;
}

/**
 * BAM device enable
 */
int sps_bam_enable(struct sps_bam *dev)
{
	u32 num_pipes;
	u32 irq_mask;
	int result = 0;
	int rc;
	int MTIenabled;

	/* Is this BAM enabled? */
	if ((dev->state & BAM_STATE_ENABLED))
		return 0;	/* Yes, so no work to do */

	/* Is there any access to this BAM? */
	if ((dev->props.manage & SPS_BAM_MGR_ACCESS_MASK) == SPS_BAM_MGR_NONE) {
		SPS_ERR(dev, "sps:No local access to BAM %pa\n", BAM_ID(dev));
		return SPS_ERROR;
	}

	/* Set interrupt handling */
	if ((dev->props.options & SPS_BAM_OPT_IRQ_DISABLED) != 0 ||
	    dev->props.irq == SPS_IRQ_INVALID) {
		/* Disable the BAM interrupt */
		irq_mask = 0;
		dev->state &= ~BAM_STATE_IRQ;
	} else {
		/* Register BAM ISR */
		if (dev->props.irq > 0) {
			if (dev->props.options & SPS_BAM_RES_CONFIRM) {
				result = request_irq(dev->props.irq,
					(irq_handler_t) bam_isr,
					IRQF_TRIGGER_RISING, "sps", dev);
				SPS_DBG3(dev,
					"sps:BAM %pa uses edge for IRQ# %d\n",
					BAM_ID(dev), dev->props.irq);
			} else {
				result = request_irq(dev->props.irq,
					(irq_handler_t) bam_isr,
					IRQF_TRIGGER_HIGH, "sps", dev);
				SPS_DBG3(dev,
					"sps:BAM %pa uses level for IRQ# %d\n",
					BAM_ID(dev), dev->props.irq);
			}
		} else {
			SPS_DBG3(dev,
				"sps:BAM %pa does not have an valid IRQ# %d\n",
				BAM_ID(dev), dev->props.irq);
		}

		if (result) {
			SPS_ERR(dev, "sps:Failed to enable BAM %pa IRQ %d\n",
				BAM_ID(dev), dev->props.irq);
			return SPS_ERROR;
		}

		/* Enable the BAM interrupt */
		irq_mask = BAM_IRQ_ALL;
		dev->state |= BAM_STATE_IRQ;

		/* Register BAM IRQ for apps wakeup */
		if (dev->props.options & SPS_BAM_OPT_IRQ_WAKEUP) {
			result = enable_irq_wake(dev->props.irq);

			if (result) {
				SPS_ERR(dev,
					"sps:Fail to enable wakeup irq for BAM %pa IRQ %d\n",
					BAM_ID(dev), dev->props.irq);
				return SPS_ERROR;
			}
			SPS_DBG3(dev,
				"sps:Enable wakeup irq for BAM %pa IRQ %d\n",
				BAM_ID(dev), dev->props.irq);
		}
	}

	/* Is global BAM control managed by the local processor? */
	num_pipes = 0;
	if ((dev->props.manage & SPS_BAM_MGR_DEVICE_REMOTE) == 0)
		/* Yes, so initialize the BAM device */
		rc = bam_init(&dev->base,
				  dev->props.ee,
				  (u16) dev->props.summing_threshold,
				  irq_mask,
				  &dev->version, &num_pipes,
				  dev->props.options);
	else
		/* No, so just verify that it is enabled */
		rc = bam_check(&dev->base, &dev->version,
				dev->props.ee, &num_pipes);

	if (rc) {
		SPS_ERR(dev, "sps:Fail to init BAM %pa IRQ %d\n",
			BAM_ID(dev), dev->props.irq);
		return SPS_ERROR;
	}

	/* Check if this BAM supports MTIs (Message Triggered Interrupts) or
	 * multiple EEs (Execution Environments).
	 * MTI and EE support are mutually exclusive.
	 */
	MTIenabled = BAM_VERSION_MTI_SUPPORT(dev->version);

	if ((dev->props.manage & SPS_BAM_MGR_DEVICE_REMOTE) != 0 &&
			(dev->props.manage & SPS_BAM_MGR_MULTI_EE) != 0 &&
			dev->props.ee == 0 && MTIenabled) {
		/*
		 * BAM global is owned by remote processor and local processor
		 * must use MTI. Thus, force EE index to a non-zero value to
		 * insure that EE zero globals can't be modified.
		 */
		SPS_ERR(dev,
			"sps: EE for satellite BAM must be set to non-zero\n");
		return SPS_ERROR;
	}

	/*
	 * Enable MTI use (message triggered interrupt)
	 * if local processor does not control the global BAM config
	 * and this BAM supports MTIs.
	 */
	if ((dev->state & BAM_STATE_IRQ) != 0 &&
		(dev->props.manage & SPS_BAM_MGR_DEVICE_REMOTE) != 0 &&
		MTIenabled) {
		if (dev->props.irq_gen_addr == 0 ||
		    dev->props.irq_gen_addr == SPS_ADDR_INVALID) {
			SPS_ERR(dev,
				"sps:MTI destination address not specified for BAM %pa\n",
				BAM_ID(dev));
			return SPS_ERROR;
		}
		dev->state |= BAM_STATE_MTI;
	}

	if (num_pipes) {
		dev->props.num_pipes = num_pipes;
		SPS_DBG3(dev,
			"sps:BAM %pa number of pipes reported by hw: %d\n",
				 BAM_ID(dev), dev->props.num_pipes);
	}

	/* Check EE index */
	if (!MTIenabled && dev->props.ee >= SPS_BAM_NUM_EES) {
		SPS_ERR(dev, "sps:Invalid EE BAM %pa: %d\n", BAM_ID(dev),
				dev->props.ee);
		return SPS_ERROR;
	}

	/*
	 * Process EE configuration parameters,
	 * if specified in the properties
	 */
	if (!MTIenabled && dev->props.sec_config == SPS_BAM_SEC_DO_CONFIG) {
		struct sps_bam_sec_config_props *p_sec =
						dev->props.p_sec_config_props;
		if (p_sec == NULL) {
			SPS_ERR(dev,
				"sps:EE config table is not specified for BAM %pa\n",
				BAM_ID(dev));
			return SPS_ERROR;
		}

		/*
		 * Set restricted pipes based on the pipes assigned to local EE
		 */
		dev->props.restricted_pipes =
					~p_sec->ees[dev->props.ee].pipe_mask;

		/*
		 * If local processor manages the BAM, perform the EE
		 * configuration
		 */
		if ((dev->props.manage & SPS_BAM_MGR_DEVICE_REMOTE) == 0) {
			u32 ee;
			u32 pipe_mask;
			int n, i;

			/*
			 * Verify that there are no overlapping pipe
			 * assignments
			 */
			for (n = 0; n < SPS_BAM_NUM_EES - 1; n++) {
				for (i = n + 1; i < SPS_BAM_NUM_EES; i++) {
					if ((p_sec->ees[n].pipe_mask &
						p_sec->ees[i].pipe_mask) != 0) {
						SPS_ERR(dev,
							"sps:Overlapping pipe assignments for BAM %pa: EEs %d and %d\n",
							BAM_ID(dev), n, i);
						return SPS_ERROR;
					}
				}
			}

			for (ee = 0; ee < SPS_BAM_NUM_EES; ee++) {
				/*
				 * MSbit specifies EE for the global (top-level)
				 * BAM interrupt
				 */
				pipe_mask = p_sec->ees[ee].pipe_mask;
				if (ee == dev->props.ee)
					pipe_mask |= (1UL << 31);
				else
					pipe_mask &= ~(1UL << 31);

				bam_security_init(&dev->base, ee,
						p_sec->ees[ee].vmid, pipe_mask);
			}
		}
	}

	/*
	 * If local processor manages the BAM and the BAM supports MTIs
	 * but does not support multiple EEs, set all restricted pipes
	 * to MTI mode.
	 */
	if ((dev->props.manage & SPS_BAM_MGR_DEVICE_REMOTE) == 0
			&& MTIenabled) {
		u32 pipe_index;
		u32 pipe_mask;

		for (pipe_index = 0, pipe_mask = 1;
		    pipe_index < dev->props.num_pipes;
		    pipe_index++, pipe_mask <<= 1) {
			if ((pipe_mask & dev->props.restricted_pipes) == 0)
				continue;	/* This is a local pipe */

			/*
			 * Enable MTI with destination address of zero
			 * (and source mask zero). Pipe is in reset,
			 * so no interrupt will be generated.
			 */
			bam_pipe_satellite_mti(&dev->base, pipe_index, 0,
						       dev->props.ee);
		}
	}

	dev->state |= BAM_STATE_ENABLED;

	if (!dev->props.constrained_logging ||
		(dev->props.constrained_logging && dev->props.logging_number)) {
		if (dev->props.logging_number > 0)
			dev->props.logging_number--;
		SPS_INFO(dev,
			"sps:BAM %pa (va:0x%pK) enabled: ver:0x%x, number of pipes:%d\n",
			BAM_ID(dev), dev->base, dev->version,
			dev->props.num_pipes);
	} else
		SPS_DBG3(dev,
			"sps:BAM %pa (va:0x%pK) enabled: ver:0x%x, number of pipes:%d\n",
			BAM_ID(dev), dev->base, dev->version,
			dev->props.num_pipes);

	return 0;
}

/**
 * BAM device disable
 *
 */
int sps_bam_disable(struct sps_bam *dev)
{
	if ((dev->state & BAM_STATE_ENABLED) == 0)
		return 0;

	/* Is there any access to this BAM? */
	if ((dev->props.manage & SPS_BAM_MGR_ACCESS_MASK) == SPS_BAM_MGR_NONE) {
		SPS_ERR(dev, "sps:No local access to BAM %pa\n", BAM_ID(dev));
		return SPS_ERROR;
	}

	/* Is this BAM controlled by the local processor? */
	if ((dev->props.manage & SPS_BAM_MGR_DEVICE_REMOTE)) {
		/* No, so just mark it disabled */
		dev->state &= ~BAM_STATE_ENABLED;
		if ((dev->state & BAM_STATE_IRQ) && (dev->props.irq > 0)) {
			free_irq(dev->props.irq, dev);
			dev->state &= ~BAM_STATE_IRQ;
		}
		return 0;
	}

	/* Disable BAM (interrupts) */
	if ((dev->state & BAM_STATE_IRQ)) {
		bam_exit(&dev->base, dev->props.ee);

		/* Deregister BAM ISR */
		if ((dev->state & BAM_STATE_IRQ))
			if (dev->props.irq > 0)
				free_irq(dev->props.irq, dev);
		dev->state &= ~BAM_STATE_IRQ;
	}

	dev->state &= ~BAM_STATE_ENABLED;

	SPS_DBG3(dev, "sps:BAM %pa disabled\n", BAM_ID(dev));

	return 0;
}

/**
 * BAM device initialization
 */
int sps_bam_device_init(struct sps_bam *dev)
{
	if (dev->props.virt_addr == NULL) {
		SPS_ERR(dev, "sps: NULL BAM virtual address\n");
		return SPS_ERROR;
	}
	dev->base = (void *) dev->props.virt_addr;

	if (dev->props.num_pipes == 0) {
		/* Assume max number of pipes until BAM registers can be read */
		dev->props.num_pipes = BAM_MAX_PIPES;
		SPS_DBG3(dev, "sps:BAM %pa: assuming max number of pipes: %d\n",
			BAM_ID(dev), dev->props.num_pipes);
	}

	/* Init BAM state data */
	dev->state = 0;
	dev->pipe_active_mask = 0;
	dev->pipe_remote_mask = 0;
	dev->no_serve_irq = false;
	INIT_LIST_HEAD(&dev->pipes_q);

	spin_lock_init(&dev->isr_lock);

	spin_lock_init(&dev->connection_lock);

	if ((dev->props.options & SPS_BAM_OPT_ENABLE_AT_BOOT))
		if (sps_bam_enable(dev)) {
			SPS_ERR(dev, "sps: Fail to enable bam device\n");
			return SPS_ERROR;
		}

	SPS_DBG3(dev, "sps:BAM device: phys %pa IRQ %d\n",
			BAM_ID(dev), dev->props.irq);

	return 0;
}

/**
 * BAM device de-initialization
 *
 */
int sps_bam_device_de_init(struct sps_bam *dev)
{
	int result;

	SPS_DBG3(dev, "sps:BAM device DEINIT: phys %pa IRQ %d\n",
		BAM_ID(dev), dev->props.irq);

	result = sps_bam_disable(dev);

	return result;
}

/**
 * BAM device reset
 *
 */
int sps_bam_reset(struct sps_bam *dev)
{
	struct sps_pipe *pipe;
	u32 pipe_index;
	int result;

	SPS_DBG3(dev, "sps:BAM device RESET: phys %pa IRQ %d\n",
		BAM_ID(dev), dev->props.irq);

	/* If BAM is enabled, then disable */
	result = 0;
	if ((dev->state & BAM_STATE_ENABLED)) {
		/* Verify that no pipes are currently allocated */
		for (pipe_index = 0; pipe_index < dev->props.num_pipes;
		      pipe_index++) {
			pipe = dev->pipes[pipe_index];
			if (BAM_PIPE_IS_ASSIGNED(pipe)) {
				SPS_ERR(dev,
					"sps:BAM device %pa RESET failed: pipe %d in use\n",
					BAM_ID(dev), pipe_index);
				result = SPS_ERROR;
				break;
			}
		}

		if (result == 0)
			result = sps_bam_disable(dev);
	}

	/* BAM will be reset as part of the enable process */
	if (result == 0)
		result = sps_bam_enable(dev);

	return result;
}

/**
 * Clear the BAM pipe state struct
 *
 * This function clears the BAM pipe state struct.
 *
 * @pipe - pointer to client pipe struct
 *
 */
static void pipe_clear(struct sps_pipe *pipe)
{
	INIT_LIST_HEAD(&pipe->list);

	pipe->state = 0;
	pipe->pipe_index = SPS_BAM_PIPE_INVALID;
	pipe->pipe_index_mask = 0;
	pipe->irq_mask = 0;
	pipe->mode = -1;
	pipe->num_descs = 0;
	pipe->desc_size = 0;
	pipe->disconnecting = false;
	pipe->late_eot = false;
	memset(&pipe->sys, 0, sizeof(pipe->sys));
	INIT_LIST_HEAD(&pipe->sys.events_q);
}

/**
 * Allocate a BAM pipe
 *
 */
u32 sps_bam_pipe_alloc(struct sps_bam *dev, u32 pipe_index)
{
	u32 pipe_mask;

	if (pipe_index == SPS_BAM_PIPE_INVALID) {
		/* Allocate a pipe from the BAM */
		if ((dev->props.manage & SPS_BAM_MGR_PIPE_NO_ALLOC)) {
			SPS_ERR(dev,
				"sps:Restricted from allocating pipes on BAM %pa\n",
				BAM_ID(dev));
			return SPS_BAM_PIPE_INVALID;
		}
		for (pipe_index = 0, pipe_mask = 1;
		    pipe_index < dev->props.num_pipes;
		    pipe_index++, pipe_mask <<= 1) {
			if ((pipe_mask & dev->props.restricted_pipes))
				continue;	/* This is a restricted pipe */

			if (dev->pipes[pipe_index] == NULL)
				break;	/* Found an available pipe */
		}
		if (pipe_index >= dev->props.num_pipes) {
			SPS_ERR(dev, "sps:Fail to allocate pipe on BAM %pa\n",
				BAM_ID(dev));
			return SPS_BAM_PIPE_INVALID;
		}
	} else {
		/* Check that client-specified pipe is available */
		if (pipe_index >= dev->props.num_pipes ||
				pipe_index >= BAM_MAX_PIPES) {
			SPS_ERR(dev,
				"sps:Invalid pipe %d for allocate on BAM %pa\n",
				pipe_index, BAM_ID(dev));
			return SPS_BAM_PIPE_INVALID;
		}
		if ((dev->props.restricted_pipes & (1UL << pipe_index))) {
			SPS_ERR(dev, "sps:BAM %pa pipe %d is not local\n",
				BAM_ID(dev), pipe_index);
			return SPS_BAM_PIPE_INVALID;
		}
		if (dev->pipes[pipe_index] != NULL) {
			SPS_ERR(dev,
				"sps:Pipe %d already allocated on BAM %pa\n",
				pipe_index, BAM_ID(dev));
			return SPS_BAM_PIPE_INVALID;
		}
	}

	/* Mark pipe as allocated */
	dev->pipes[pipe_index] = BAM_PIPE_UNASSIGNED;

	return pipe_index;
}

/**
 * Free a BAM pipe
 *
 */
void sps_bam_pipe_free(struct sps_bam *dev, u32 pipe_index)
{
	struct sps_pipe *pipe;

	if (pipe_index >= dev->props.num_pipes) {
		SPS_ERR(dev, "sps:Invalid BAM %pa pipe: %d\n", BAM_ID(dev),
				pipe_index);
		return;
	}

	/* Get the client pipe struct and mark the pipe free */
	pipe = dev->pipes[pipe_index];
	dev->pipes[pipe_index] = NULL;

	/* Is the pipe currently allocated? */
	if (pipe == NULL) {
		SPS_ERR(dev,
			"sps:Attempt to free unallocated pipe %d on BAM %pa\n",
			pipe_index, BAM_ID(dev));
		return;
	}

	if (pipe == BAM_PIPE_UNASSIGNED)
		return;		/* Never assigned, so no work to do */

	/* Return pending items to appropriate pools */
	if (!list_empty(&pipe->sys.events_q)) {
		struct sps_q_event *sps_event;

		SPS_ERR(dev,
			"sps:Disconnect BAM %pa pipe %d with events pending\n",
			BAM_ID(dev), pipe_index);

		sps_event = list_entry((&pipe->sys.events_q)->next,
				typeof(*sps_event), list);

		while (&sps_event->list != (&pipe->sys.events_q)) {
			struct sps_q_event *sps_event_delete = sps_event;

			list_del(&sps_event->list);
			sps_event = list_entry(sps_event->list.next,
					typeof(*sps_event), list);
			kfree(sps_event_delete);
		}
	}

	/* Clear the BAM pipe state struct */
	pipe_clear(pipe);
}

/**
 * Establish BAM pipe connection
 *
 */
int sps_bam_pipe_connect(struct sps_pipe *bam_pipe,
			 const struct sps_bam_connect_param *params)
{
	struct bam_pipe_parameters hw_params;
	struct sps_bam *dev;
	const struct sps_connection *map = bam_pipe->map;
	const struct sps_conn_end_pt *map_pipe;
	const struct sps_conn_end_pt *other_pipe;
	void *desc_buf = NULL;
	u32 pipe_index;
	int result;

	/* Clear the client pipe state and hw init struct */
	pipe_clear(bam_pipe);
	memset(&hw_params, 0, sizeof(hw_params));

	/* Initialize the BAM state struct */
	bam_pipe->mode = params->mode;

	/* Set pipe streaming mode */
	if ((params->options & SPS_O_STREAMING) == 0)
		hw_params.stream_mode = BAM_STREAM_MODE_DISABLE;
	else
		hw_params.stream_mode = BAM_STREAM_MODE_ENABLE;

	/* Determine which end point to connect */
	if (bam_pipe->mode == SPS_MODE_SRC) {
		map_pipe = &map->src;
		other_pipe = &map->dest;
		hw_params.dir = BAM_PIPE_PRODUCER;
	} else {
		map_pipe = &map->dest;
		other_pipe = &map->src;
		hw_params.dir = BAM_PIPE_CONSUMER;
	}

	/* Process map parameters */
	dev = map_pipe->bam;
	pipe_index = map_pipe->pipe_index;

	SPS_DBG2(dev,
		"sps:BAM %pa; pipe %d; mode:%d; options:0x%x\n",
		BAM_ID(dev), pipe_index, params->mode, params->options);

	if (pipe_index >= dev->props.num_pipes) {
		SPS_ERR(dev, "sps:Invalid BAM %pa pipe: %d\n", BAM_ID(dev),
				pipe_index);
		return SPS_ERROR;
	}
	hw_params.event_threshold = (u16) map_pipe->event_threshold;
	hw_params.ee = dev->props.ee;
	hw_params.lock_group = map_pipe->lock_group;

	/* Verify that control of this pipe is allowed */
	if ((dev->props.manage & SPS_BAM_MGR_PIPE_NO_CTRL) ||
	    (dev->props.restricted_pipes & (1UL << pipe_index))) {
		SPS_ERR(dev, "sps:BAM %pa pipe %d is not local\n",
			BAM_ID(dev), pipe_index);
		return SPS_ERROR;
	}

	/* Control without configuration permission is not supported yet */
	if ((dev->props.manage & SPS_BAM_MGR_PIPE_NO_CONFIG)) {
		SPS_ERR(dev,
			"sps:BAM %pa pipe %d remote config is not supported\n",
			BAM_ID(dev), pipe_index);
		return SPS_ERROR;
	}

	/* Determine operational mode */
	if ((bam_pipe->connect.options & SPS_O_DUMMY_PEER) ||
						other_pipe->bam != NULL) {
		unsigned long iova;
		struct sps_bam *peer_bam;
		/* BAM-to-BAM mode */
		bam_pipe->state |= BAM_STATE_BAM2BAM;
		hw_params.mode = BAM_PIPE_MODE_BAM2BAM;
		if (!(bam_pipe->connect.options & SPS_O_DUMMY_PEER))
			peer_bam = (struct sps_bam *)(other_pipe->bam);
		if (dev->props.options & SPS_BAM_SMMU_EN) {
			if (bam_pipe->mode == SPS_MODE_SRC)
				iova = bam_pipe->connect.dest_iova;
			else
				iova = bam_pipe->connect.source_iova;
			SPS_DBG2(dev,
				"sps:BAM %pa pipe %d uses IOVA 0x%pK\n",
				 BAM_ID(dev), pipe_index, (void *)iova);
			hw_params.peer_phys_addr = (u32)iova;
		} else {
			if (!(bam_pipe->connect.options & SPS_O_DUMMY_PEER))
				hw_params.peer_phys_addr =
					peer_bam->props.phys_addr;
		}
		if (!(bam_pipe->connect.options & SPS_O_DUMMY_PEER)) {
			hw_params.peer_pipe = other_pipe->pipe_index;
		} else {
			hw_params.peer_phys_addr =
					bam_pipe->connect.destination;
			hw_params.peer_pipe =
					bam_pipe->connect.dest_pipe_index;
			hw_params.dummy_peer = true;
		}
		/* Verify FIFO buffers are allocated for BAM-to-BAM pipes */
		if (map->desc.phys_base == SPS_ADDR_INVALID ||
		    map->data.phys_base == SPS_ADDR_INVALID ||
		    map->desc.size == 0 || map->data.size == 0) {
			SPS_ERR(dev,
				"sps:FIFO buffers are not allocated for BAM %pa pipe %d\n",
				BAM_ID(dev), pipe_index);
			return SPS_ERROR;
		}

		if (dev->props.options & SPS_BAM_SMMU_EN) {
			hw_params.data_base =
				(phys_addr_t)bam_pipe->connect.data.iova;
			SPS_DBG2(dev,
				"sps:BAM %pa pipe %d uses IOVA 0x%pK for data FIFO\n",
				 BAM_ID(dev), pipe_index,
				 (void *)(bam_pipe->connect.data.iova));
		} else {
			hw_params.data_base = map->data.phys_base;
		}

		hw_params.data_size = map->data.size;

		/* Clear the data FIFO for debug */
		if (map->data.base != NULL && bam_pipe->mode == SPS_MODE_SRC)
			memset_io(map->data.base, 0, hw_params.data_size);

		/* set NWD bit for BAM2BAM producer pipe */
		if (bam_pipe->mode == SPS_MODE_SRC) {
			if ((params->options & SPS_O_WRITE_NWD) == 0)
				hw_params.write_nwd = BAM_WRITE_NWD_DISABLE;
			else
				hw_params.write_nwd = BAM_WRITE_NWD_ENABLE;
		}
	} else {
		/* System mode */
		hw_params.mode = BAM_PIPE_MODE_SYSTEM;
		bam_pipe->sys.desc_buf = map->desc.base;
		bam_pipe->sys.desc_offset = 0;
		bam_pipe->sys.acked_offset = 0;
	}

	/* Initialize the client pipe state */
	bam_pipe->pipe_index = pipe_index;
	bam_pipe->pipe_index_mask = 1UL << pipe_index;

	/* Get virtual address for descriptor FIFO */
	if (map->desc.phys_base != SPS_ADDR_INVALID) {
		if (map->desc.size < (2 * sizeof(struct sps_iovec))) {
			SPS_ERR(dev,
				"sps:Invalid descriptor FIFO size for BAM %pa pipe %d: %d\n",
				BAM_ID(dev), pipe_index, map->desc.size);
			return SPS_ERROR;
		}
		desc_buf = map->desc.base;

		/*
		 * Note that descriptor base and size will be left zero from
		 * the memset() above if the physical address was invalid.
		 * This allows a satellite driver to set the FIFO as
		 * local memory	for system mode.
		 */

		if (dev->props.options & SPS_BAM_SMMU_EN) {
			hw_params.desc_base =
				(phys_addr_t)bam_pipe->connect.desc.iova;
			SPS_DBG2(dev,
				"sps:BAM %pa pipe %d uses IOVA 0x%pK for desc FIFO\n",
				 BAM_ID(dev), pipe_index,
				 (void *)(bam_pipe->connect.desc.iova));
		} else {
			hw_params.desc_base = map->desc.phys_base;
		}

		hw_params.desc_size = map->desc.size;
	}

	/* Configure the descriptor FIFO for both operational modes */
	if (desc_buf != NULL)
		if (bam_pipe->mode == SPS_MODE_SRC ||
		    hw_params.mode == BAM_PIPE_MODE_SYSTEM)
			memset_io(desc_buf, 0, hw_params.desc_size);

	bam_pipe->desc_size = hw_params.desc_size;
	bam_pipe->num_descs = bam_pipe->desc_size / sizeof(struct sps_iovec);

	result = SPS_ERROR;
	/* Insure that the BAM is enabled */
	if ((dev->state & BAM_STATE_ENABLED) == 0)
		if (sps_bam_enable(dev))
			goto exit_init_err;

	/* Check pipe allocation */
	if (dev->pipes[pipe_index] != BAM_PIPE_UNASSIGNED) {
		SPS_ERR(dev, "sps:Invalid pipe %d on BAM %pa for connect\n",
			pipe_index, BAM_ID(dev));
		return SPS_ERROR;
	}

	if (bam_pipe_is_enabled(&dev->base, pipe_index)) {
		if (params->options & SPS_O_NO_DISABLE)
			SPS_DBG2(dev,
				"sps:BAM %pa pipe %d is already enabled\n",
				BAM_ID(dev), pipe_index);
		else {
			SPS_ERR(dev, "sps:BAM %pa pipe %d sharing violation\n",
				BAM_ID(dev), pipe_index);
			return SPS_ERROR;
		}
	}

	if (bam_pipe_init(&dev->base, pipe_index, &hw_params, dev->props.ee)) {
		SPS_ERR(dev, "sps:BAM %pa pipe %d init error\n",
			BAM_ID(dev), pipe_index);
		goto exit_err;
	}

	/* Assign pipe to client */
	dev->pipes[pipe_index] = bam_pipe;

	/* Process configuration parameters */
	if (params->options != 0 ||
	    (bam_pipe->state & BAM_STATE_BAM2BAM) == 0) {
		/* Process init-time only parameters */
		u32 irq_gen_addr;

		/* Set interrupt mode */
		irq_gen_addr = SPS_ADDR_INVALID;
		if ((params->options & SPS_O_IRQ_MTI))
			/* Client has directly specified the MTI address */
			irq_gen_addr = params->irq_gen_addr;
		else if ((dev->state & BAM_STATE_MTI))
			/* This BAM has MTI use enabled */
			irq_gen_addr = dev->props.irq_gen_addr;

		if (irq_gen_addr != SPS_ADDR_INVALID) {
			/*
			 * No checks - assume BAM is already setup for
			 * MTI generation,
			 * or the pipe will be set to satellite control.
			 */
			bam_pipe->state |= BAM_STATE_MTI;
			bam_pipe->irq_gen_addr = irq_gen_addr;
		}

		/* Process runtime parameters */
		if (sps_bam_pipe_set_params(dev, pipe_index,
					  params->options)) {
			dev->pipes[pipe_index] = BAM_PIPE_UNASSIGNED;
			goto exit_err;
		}
	}

	/* Indicate initialization is complete */
	dev->pipes[pipe_index] = bam_pipe;
	dev->pipe_active_mask |= 1UL << pipe_index;
	list_add_tail(&bam_pipe->list, &dev->pipes_q);

	SPS_DBG2(dev,
		"sps:BAM %pa; pipe %d; pipe_index_mask:0x%x; pipe_active_mask:0x%x\n",
		BAM_ID(dev), pipe_index,
		bam_pipe->pipe_index_mask, dev->pipe_active_mask);

	bam_pipe->state |= BAM_STATE_INIT;
	result = 0;
exit_err:
	if (result) {
		if (params->options & SPS_O_NO_DISABLE)
			SPS_DBG2(dev, "sps:BAM %pa pipe %d connection exits\n",
				BAM_ID(dev), pipe_index);
		else
			bam_pipe_exit(&dev->base, pipe_index, dev->props.ee);
	}
exit_init_err:
	if (result) {
		/* Clear the client pipe state */
		pipe_clear(bam_pipe);
	}

	return result;
}

/**
 * Disconnect a BAM pipe connection
 *
 */
int sps_bam_pipe_disconnect(struct sps_bam *dev, u32 pipe_index)
{
	struct sps_pipe *pipe;
	int result;
	unsigned long flags;

	if (pipe_index >= dev->props.num_pipes) {
		SPS_ERR(dev, "sps:Invalid BAM %pa pipe: %d\n", BAM_ID(dev),
				pipe_index);
		return SPS_ERROR;
	}

	/* Deallocate and reset the BAM pipe */
	pipe = dev->pipes[pipe_index];
	if (BAM_PIPE_IS_ASSIGNED(pipe)) {
		if ((dev->pipe_active_mask & (1UL << pipe_index))) {
			spin_lock_irqsave(&dev->isr_lock, flags);
			list_del(&pipe->list);
			dev->pipe_active_mask &= ~(1UL << pipe_index);
			spin_unlock_irqrestore(&dev->isr_lock, flags);
		}
		dev->pipe_remote_mask &= ~(1UL << pipe_index);
		if (pipe->connect.options & SPS_O_NO_DISABLE)
			SPS_DBG2(dev, "sps:BAM %pa pipe %d exits\n",
				BAM_ID(dev), pipe_index);
		else
			bam_pipe_exit(&dev->base, pipe_index, dev->props.ee);
		if (pipe->sys.desc_cache != NULL) {
			u32 size = pipe->num_descs * sizeof(void *);

			if (pipe->desc_size + size <= PAGE_SIZE) {
				if (dev->props.options & SPS_BAM_HOLD_MEM)
					memset(pipe->sys.desc_cache, 0,
						pipe->desc_size + size);
				else
					kfree(pipe->sys.desc_cache);
			} else {
				vfree(pipe->sys.desc_cache);
			}
			pipe->sys.desc_cache = NULL;
		}
		dev->pipes[pipe_index] = BAM_PIPE_UNASSIGNED;
		pipe_clear(pipe);
		result = 0;
	} else {
		result = SPS_ERROR;
	}

	if (result)
		SPS_ERR(dev, "sps:BAM %pa pipe %d already disconnected\n",
			BAM_ID(dev), pipe_index);

	return result;
}

/**
 * Set BAM pipe interrupt enable state
 *
 * This function sets the interrupt enable state for a BAM pipe.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @poll - true if SPS_O_POLL is set, false otherwise
 *
 */
static void pipe_set_irq(struct sps_bam *dev, u32 pipe_index,
				 u32 poll)
{
	struct sps_pipe *pipe = dev->pipes[pipe_index];
	enum bam_enable irq_enable;

	SPS_DBG2(dev,
		"sps:BAM:%pa; pipe %d; poll:%d, irq_mask:0x%x; pipe state:0x%x; dev state:0x%x\n",
		BAM_ID(dev), pipe_index, poll, pipe->irq_mask,
		pipe->state, dev->state);

	if (poll == 0 && pipe->irq_mask != 0 &&
	    (dev->state & BAM_STATE_IRQ)) {
		if ((pipe->state & BAM_STATE_BAM2BAM) != 0 &&
		    (pipe->state & BAM_STATE_IRQ) == 0) {
			/*
			 * If enabling the interrupt for a BAM-to-BAM pipe,
			 * clear the existing interrupt status
			 */
			(void)bam_pipe_get_and_clear_irq_status(&dev->base,
							   pipe_index);
		}
		pipe->state |= BAM_STATE_IRQ;
		irq_enable = BAM_ENABLE;
		pipe->polled = false;
	} else {
		pipe->state &= ~BAM_STATE_IRQ;
		irq_enable = BAM_DISABLE;
		pipe->polled = true;
		if (poll == 0 && pipe->irq_mask)
			SPS_DBG2(dev,
				"sps:BAM %pa pipe %d forced to use polling\n",
				 BAM_ID(dev), pipe_index);
	}
	if ((pipe->state & BAM_STATE_MTI) == 0)
		bam_pipe_set_irq(&dev->base, pipe_index, irq_enable,
					 pipe->irq_mask, dev->props.ee);
	else
		bam_pipe_set_mti(&dev->base, pipe_index, irq_enable,
					 pipe->irq_mask, pipe->irq_gen_addr);

}

/**
 * Set BAM pipe parameters
 *
 */
int sps_bam_pipe_set_params(struct sps_bam *dev, u32 pipe_index, u32 options)
{
	struct sps_pipe *pipe = dev->pipes[pipe_index];
	u32 mask;
	int wake_up_is_one_shot;
	int no_queue;
	int ack_xfers;
	u32 size;
	int n;

	SPS_DBG2(dev, "sps:BAM %pa pipe %d opt 0x%x\n",
		BAM_ID(dev), pipe_index, options);

	/* Capture some options */
	wake_up_is_one_shot = ((options & SPS_O_WAKEUP_IS_ONESHOT));
	no_queue = ((options & SPS_O_NO_Q));
	ack_xfers = ((options & SPS_O_ACK_TRANSFERS));

	pipe->hybrid = options & SPS_O_HYBRID;
	pipe->late_eot = options & SPS_O_LATE_EOT;

	/* Create interrupt source mask */
	mask = 0;
	for (n = 0; n < ARRAY_SIZE(opt_event_table); n++) {
		/* Is client registering for this event? */
		if ((options & opt_event_table[n].option) == 0)
			continue;	/* No */

		mask |= opt_event_table[n].pipe_irq;
	}

#ifdef SPS_BAM_STATISTICS
	/* Is an illegal mode change specified? */
	if (pipe->sys.desc_wr_count > 0 &&
	    (no_queue != pipe->sys.no_queue
	     || ack_xfers != pipe->sys.ack_xfers)) {
		SPS_ERR(dev,
			"sps:Queue/ack mode change after transfer: BAM %pa pipe %d opt 0x%x\n",
			BAM_ID(dev), pipe_index, options);
		return SPS_ERROR;
	}
#endif /* SPS_BAM_STATISTICS */

	/* Is client setting invalid options for a BAM-to-BAM connection? */
	if ((pipe->state & BAM_STATE_BAM2BAM) &&
	    (options & BAM2BAM_O_INVALID)) {
		SPS_ERR(dev,
			"sps:Invalid option for BAM-to-BAM: BAM %pa pipe %d opt 0x%x\n",
			BAM_ID(dev), pipe_index, options);
		return SPS_ERROR;
	}

	/* Allocate descriptor FIFO cache if NO_Q option is disabled */
	if (!no_queue && pipe->sys.desc_cache == NULL && pipe->num_descs > 0
	    && (pipe->state & BAM_STATE_BAM2BAM) == 0) {
		/* Allocate both descriptor cache and user pointer array */
		size = pipe->num_descs * sizeof(void *);

		if (pipe->desc_size + size <= PAGE_SIZE) {
			if ((dev->props.options &
						SPS_BAM_HOLD_MEM)) {
				if (dev->desc_cache_pointers[pipe_index]) {
					pipe->sys.desc_cache =
						dev->desc_cache_pointers
							[pipe_index];
				} else {
					pipe->sys.desc_cache =
						kzalloc(pipe->desc_size + size,
								GFP_KERNEL);
					dev->desc_cache_pointers[pipe_index] =
							pipe->sys.desc_cache;
				}
			} else {
				pipe->sys.desc_cache =
						kzalloc(pipe->desc_size + size,
							GFP_KERNEL);
			}
			if (pipe->sys.desc_cache == NULL) {
				SPS_ERR(dev,
					"sps:No memory for pipe%d of BAM %pa\n",
						pipe_index, BAM_ID(dev));
				return -ENOMEM;
			}
		} else {
			pipe->sys.desc_cache =
				vzalloc(pipe->desc_size + size);

			if (pipe->sys.desc_cache == NULL) {
				SPS_ERR(dev,
					"sps:No memory for pipe %d of BAM %pa\n",
					pipe_index, BAM_ID(dev));
				return -ENOMEM;
			}

		}

		if (pipe->sys.desc_cache == NULL) {
			/*** MUST BE LAST POINT OF FAILURE (see below) *****/
			SPS_ERR(dev,
				"sps:Desc cache error: BAM %pa pipe %d: %d\n",
				BAM_ID(dev), pipe_index,
				pipe->desc_size + size);
			return SPS_ERROR;
		}
		pipe->sys.user_ptrs = (void **)(pipe->sys.desc_cache +
						 pipe->desc_size);
		pipe->sys.cache_offset = pipe->sys.acked_offset;
	}

	/*
	 * No failures beyond this point. Note that malloc() is last point of
	 * failure, so no free() handling is needed.
	 */

	/* Enable/disable the pipe's interrupt sources */
	pipe->irq_mask = mask;
	pipe_set_irq(dev, pipe_index, (options & SPS_O_POLL));

	/* Store software feature enables */
	pipe->wake_up_is_one_shot = wake_up_is_one_shot;
	pipe->sys.no_queue = no_queue;
	pipe->sys.ack_xfers = ack_xfers;

	return 0;
}

/**
 * Enable all IRQs of a BAM and its pipes.
 *
 */
void sps_bam_enable_all_irqs(struct sps_bam *dev)
{
	struct sps_pipe *pipe;
	u32 irq_mask;
	unsigned long flags = 0;

	spin_lock_irqsave(&dev->isr_lock, flags);
	/* Enable global interrupt */
	irq_mask = BAM_IRQ_ALL;
	bam_set_global_irq(&dev->base, dev->props.ee, irq_mask, true);

	/* Enable all pipe interrupts */
	pipe = list_first_entry(&dev->pipes_q, struct sps_pipe, list);

	list_for_each_entry(pipe, &dev->pipes_q, list) {
		/* Check this pipe's bit in the source mask */
		if (BAM_PIPE_IS_ASSIGNED(pipe)
				&& (!pipe->disconnecting)) {
			bam_pipe_set_irq(&dev->base, pipe->pipe_index, BAM_ENABLE,
							pipe->irq_mask, dev->props.ee);
		}
	}

	// Write memory barrier
	wmb();
	dev->no_serve_irq = false;

	spin_unlock_irqrestore(&dev->isr_lock, flags);
}

/**
 * Disable all IRQs of a BAM and its pipes.
 *
 */
void sps_bam_disable_all_irqs(struct sps_bam *dev)
{
	struct sps_pipe *pipe;
	unsigned long flags = 0;

	spin_lock_irqsave(&dev->isr_lock, flags);

	/* Disable global interrupt */
	bam_set_global_irq(&dev->base, dev->props.ee, 0, false);

	/* Disable all pipe interrupts */
	pipe = list_first_entry(&dev->pipes_q, struct sps_pipe, list);

	list_for_each_entry(pipe, &dev->pipes_q, list) {
		/* Check this pipe's bit in the source mask */
		if (BAM_PIPE_IS_ASSIGNED(pipe)
				&& (!pipe->disconnecting)) {
			bam_pipe_set_irq(&dev->base, pipe->pipe_index, BAM_DISABLE,
							0, dev->props.ee);
		}
	}

	// Write memory barrier
	wmb();
	dev->no_serve_irq = true;

	spin_unlock_irqrestore(&dev->isr_lock, flags);
}

/**
 * Enable a BAM pipe
 *
 */
int sps_bam_pipe_enable(struct sps_bam *dev, u32 pipe_index)
{
	struct sps_pipe *pipe = dev->pipes[pipe_index];

	/* Enable the BAM pipe */
	bam_pipe_enable(&dev->base, pipe_index);
	pipe->state |= BAM_STATE_ENABLED;

	return 0;
}

/**
 * Disable a BAM pipe
 *
 */
int sps_bam_pipe_disable(struct sps_bam *dev, u32 pipe_index)
{
	struct sps_pipe *pipe = dev->pipes[pipe_index];

	/* Disable the BAM pipe */
	if (pipe->connect.options & SPS_O_NO_DISABLE)
		SPS_DBG2(dev, "sps:BAM %pa pipe %d enters disable state\n",
			BAM_ID(dev), pipe_index);
	else
		bam_pipe_disable(&dev->base, pipe_index);

	pipe->state &= ~BAM_STATE_ENABLED;

	return 0;
}

/**
 * Register an event for a BAM pipe
 *
 */
int sps_bam_pipe_reg_event(struct sps_bam *dev,
			   u32 pipe_index,
			   struct sps_register_event *reg)
{
	struct sps_pipe *pipe = dev->pipes[pipe_index];
	struct sps_bam_event_reg *event_reg;
	int n;

	if (pipe->sys.no_queue && reg->xfer_done != NULL &&
	    reg->mode != SPS_TRIGGER_CALLBACK) {
		SPS_ERR(dev,
			"sps:Only callback events support for NO_Q: BAM %pa pipe %d mode %d\n",
			BAM_ID(dev), pipe_index, reg->mode);
		return SPS_ERROR;
	}

	for (n = 0; n < ARRAY_SIZE(opt_event_table); n++) {
		int index;

		/* Is client registering for this event? */
		if ((reg->options & opt_event_table[n].option) == 0)
			continue;	/* No */

		index = SPS_EVENT_INDEX(opt_event_table[n].event_id);
		if (index < 0)
			SPS_ERR(dev,
				"sps:Negative event index: BAM %pa pipe %d mode %d\n",
				BAM_ID(dev), pipe_index, reg->mode);
		else {
			event_reg = &pipe->sys.event_regs[index];
			event_reg->xfer_done = reg->xfer_done;
			event_reg->callback = reg->callback;
			event_reg->mode = reg->mode;
			event_reg->user = reg->user;
		}
	}

	return 0;
}

/**
 * Submit a transfer of a single buffer to a BAM pipe
 *
 */
int sps_bam_pipe_transfer_one(struct sps_bam *dev,
				    u32 pipe_index, u32 addr, u32 size,
				    void *user, u32 flags)
{
	struct sps_pipe *pipe = dev->pipes[pipe_index];
	struct sps_iovec *desc;
	struct sps_iovec iovec;
	u32 next_write;
	static int show_recom;

	SPS_DBG(dev, "sps:BAM %pa pipe %d addr 0x%pK size 0x%x flags 0x%x\n",
			BAM_ID(dev), pipe_index,
			(void *)(long)addr, size, flags);

	/* Is this a BAM-to-BAM or satellite connection? */
	if ((pipe->state & (BAM_STATE_BAM2BAM | BAM_STATE_REMOTE))) {
		SPS_ERR(dev, "sps:Transfer on BAM-to-BAM: BAM %pa pipe %d\n",
			BAM_ID(dev), pipe_index);
		return SPS_ERROR;
	}

	/*
	 * Client identifier (user pointer) is not supported for
	 * SPS_O_NO_Q option.
	 */
	if (pipe->sys.no_queue && user != NULL) {
		SPS_ERR(dev, "sps:User pointer arg non-NULL: BAM %pa pipe %d\n",
			BAM_ID(dev), pipe_index);
		return SPS_ERROR;
	}

	/* Determine if descriptor can be queued */
	next_write = pipe->sys.desc_offset + sizeof(struct sps_iovec);
	if (next_write >= pipe->desc_size)
		next_write = 0;

	if (next_write == pipe->sys.acked_offset) {
		/*
		 * If pipe is polled and client is not ACK'ing descriptors,
		 * perform polling operation so that any outstanding ACKs
		 * can occur.
		 */
		if (!pipe->sys.ack_xfers && pipe->polled) {
			pipe_handler_eot(dev, pipe);
			if (next_write == pipe->sys.acked_offset) {
				if (!show_recom) {
					show_recom = true;
					SPS_ERR(dev,
						"sps:Client of BAM %pa pipe %d is recommended to have flow control\n",
						BAM_ID(dev), pipe_index);
				}

				SPS_DBG1(dev,
					"sps:Descriptor FIFO is full for BAM %pa pipe %d after pipe_handler_eot\n",
					BAM_ID(dev), pipe_index);
				return SPS_ERROR;
			}
		} else {
			if (!show_recom) {
				show_recom = true;
				SPS_ERR(dev,
					"sps:Client of BAM %pa pipe %d is recommended to have flow control\n",
					BAM_ID(dev), pipe_index);
			}

			SPS_DBG1(dev,
				"sps:Descriptor FIFO is full for BAM %pa pipe %d\n",
				BAM_ID(dev), pipe_index);
			return SPS_ERROR;
		}
	}

	/* Create descriptor */
	if (!pipe->sys.no_queue)
		desc = (struct sps_iovec *) (pipe->sys.desc_cache +
					      pipe->sys.desc_offset);
	else
		desc = &iovec;

	desc->addr = addr;
	desc->size = size;

	if ((flags & SPS_IOVEC_FLAG_DEFAULT) == 0) {
		desc->flags = (flags & BAM_IOVEC_FLAG_MASK)
				| DESC_UPPER_ADDR(flags);
	} else {
		if (pipe->mode == SPS_MODE_SRC)
			desc->flags = SPS_IOVEC_FLAG_INT
					| DESC_UPPER_ADDR(flags);
		else
			desc->flags = (SPS_IOVEC_FLAG_INT | SPS_IOVEC_FLAG_EOT)
					| DESC_UPPER_ADDR(flags);
	}

#ifdef SPS_BAM_STATISTICS
	if ((flags & SPS_IOVEC_FLAG_INT))
		pipe->sys.int_flags++;
	if ((flags & SPS_IOVEC_FLAG_EOT))
		pipe->sys.eot_flags++;
#endif /* SPS_BAM_STATISTICS */

	/* Update hardware descriptor FIFO - should result in burst */
	*((struct sps_iovec *) (pipe->sys.desc_buf + pipe->sys.desc_offset))
	= *desc;

	/* Record user pointer value */
	if (!pipe->sys.no_queue) {
		u32 index = pipe->sys.desc_offset / sizeof(struct sps_iovec);

		pipe->sys.user_ptrs[index] = user;
#ifdef SPS_BAM_STATISTICS
		if (user != NULL)
			pipe->sys.user_ptrs_count++;
#endif /* SPS_BAM_STATISTICS */
	}

	/* Update descriptor ACK offset */
	pipe->sys.desc_offset = next_write;

#ifdef SPS_BAM_STATISTICS
	/* Update statistics */
	pipe->sys.desc_wr_count++;
#endif /* SPS_BAM_STATISTICS */

	/* Notify pipe */
	if ((flags & SPS_IOVEC_FLAG_NO_SUBMIT) == 0) {
		wmb(); /* Memory Barrier */
		bam_pipe_set_desc_write_offset(&dev->base, pipe_index,
					       next_write);
	}

	if (dev->ipc_loglevel == 0)
		SPS_DBG(dev,
			"sps: BAM phy addr:%pa; pipe %d; write pointer to tell HW: 0x%x; write pointer read from HW: 0x%x\n",
			BAM_ID(dev), pipe_index, next_write,
			bam_pipe_get_desc_write_offset(&dev->base, pipe_index));

	return 0;
}

/**
 * Submit a transfer to a BAM pipe
 *
 */
int sps_bam_pipe_transfer(struct sps_bam *dev,
			 u32 pipe_index, struct sps_transfer *transfer)
{
	struct sps_iovec *iovec;
	u32 count;
	u32 flags;
	void *user;
	int n;
	int result;
	struct sps_pipe *pipe = dev->pipes[pipe_index];

	if (transfer->iovec_count == 0) {
		SPS_ERR(dev, "sps:iovec count zero: BAM %pa pipe %d\n",
			BAM_ID(dev), pipe_index);
		return SPS_ERROR;
	}

	if (!pipe->sys.ack_xfers && pipe->polled) {
		sps_bam_pipe_get_unused_desc_num(dev, pipe_index,
					&count);
		count = pipe->desc_size / sizeof(struct sps_iovec) - count - 1;
	} else
		sps_bam_get_free_count(dev, pipe_index, &count);

	if (count < transfer->iovec_count) {
		SPS_ERR(dev,
			"sps:Insufficient free desc: BAM %pa pipe %d: %d\n",
			BAM_ID(dev), pipe_index, count);
		return SPS_ERROR;
	}

	user = NULL;		/* NULL for all except last descriptor */
	for (n = (int)transfer->iovec_count - 1, iovec = transfer->iovec;
	    n >= 0; n--, iovec++) {
		if (n > 0) {
			/* This is *not* the last descriptor */
			flags = iovec->flags | SPS_IOVEC_FLAG_NO_SUBMIT;
		} else {
			/* This *is* the last descriptor */
			flags = iovec->flags;
			user = transfer->user;
		}
		result = sps_bam_pipe_transfer_one(dev, pipe_index,
						 iovec->addr,
						 iovec->size, user,
						 flags);
		if (result)
			return SPS_ERROR;
	}

	return 0;
}

int sps_bam_pipe_inject_zlt(struct sps_bam *dev, u32 pipe_index)
{
	struct sps_pipe *pipe = dev->pipes[pipe_index];
	struct sps_iovec *desc;
	u32 read_p, write_p, next_write;

	if (pipe->state & BAM_STATE_BAM2BAM)
		SPS_DBG2(dev, "sps: BAM-to-BAM pipe: BAM %pa pipe %d\n",
			BAM_ID(dev), pipe_index);
	else
		SPS_DBG2(dev, "sps: BAM-to-System pipe: BAM %pa pipe %d\n",
			BAM_ID(dev), pipe_index);

	if (!(pipe->state & BAM_STATE_ENABLED)) {
		SPS_ERR(dev,
			"sps: BAM %pa pipe %d is not enabled\n",
			BAM_ID(dev), pipe_index);
		return SPS_ERROR;
	}

	read_p = bam_pipe_get_desc_read_offset(&dev->base, pipe_index);
	write_p = bam_pipe_get_desc_write_offset(&dev->base, pipe_index);

	SPS_DBG2(dev,
		"sps: BAM %pa pipe %d: read pointer:0x%x; write pointer:0x%x\n",
		BAM_ID(dev), pipe_index, read_p, write_p);

	if (read_p == write_p) {
		SPS_ERR(dev,
			"sps: BAM %pa pipe %d: read pointer 0x%x is already equal to write pointer\n",
			BAM_ID(dev), pipe_index, read_p);
		return SPS_ERROR;
	}

	next_write = write_p + sizeof(struct sps_iovec);
	if (next_write >= pipe->desc_size) {
		SPS_DBG2(dev,
			"sps: BAM %pa pipe %d: next write is 0x%x: wrap around\n",
			BAM_ID(dev), pipe_index, next_write);
		next_write = 0;
	}

	desc = (struct sps_iovec *) (pipe->connect.desc.base + write_p);
	desc->addr = 0;
	desc->size = 0;
	desc->flags = SPS_IOVEC_FLAG_EOT;

	bam_pipe_set_desc_write_offset(&dev->base, pipe_index,
					       next_write);
	wmb(); /* update write pointer in HW */
	SPS_DBG2(dev,
		"sps: BAM %pa pipe %d: write pointer to tell HW: 0x%x; write pointer read from HW: 0x%x\n",
		BAM_ID(dev), pipe_index, next_write,
		bam_pipe_get_desc_write_offset(&dev->base, pipe_index));

	return 0;
}

/**
 * Allocate an event tracking struct
 *
 * This function allocates an event tracking struct.
 *
 * @pipe - pointer to pipe state
 *
 * @event_reg - pointer to event registration
 *
 * @return - pointer to event notification struct, or NULL
 *
 */
static struct sps_q_event *alloc_event(struct sps_pipe *pipe,
					struct sps_bam_event_reg *event_reg)
{
	struct sps_q_event *event;

	/* A callback event object is registered, so trigger with payload */
	event = &pipe->sys.event;
	memset(event, 0, sizeof(*event));

	return event;
}

/**
 * Trigger an event notification
 *
 * This function triggers an event notification.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe - pointer to pipe state
 *
 * @event_reg - pointer to event registration
 *
 * @sps_event - pointer to event struct
 *
 */
static void trigger_event(struct sps_bam *dev,
			  struct sps_pipe *pipe,
			  struct sps_bam_event_reg *event_reg,
			  struct sps_q_event *sps_event)
{
	if (sps_event == NULL) {
		SPS_DBG1(dev, "%s", "sps:trigger_event.sps_event is NULL\n");
		return;
	}

	if (event_reg->xfer_done) {
		complete(event_reg->xfer_done);
		SPS_DBG(dev, "sps: done=%d\n", event_reg->xfer_done->done);
	}

	if (event_reg->callback) {
		SPS_DBG(dev, "%s", "sps:trigger_event.using callback\n");
		event_reg->callback(&sps_event->notify);
	}

}

/**
 * Handle a BAM pipe's generic interrupt sources
 *
 * This function creates the event notification for a BAM pipe's
 *    generic interrupt sources.  The caller of this function must lock the BAM
 *    device's mutex.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe - pointer to pipe state
 *
 * @event_id - event identifier enum
 *
 */
static void pipe_handler_generic(struct sps_bam *dev,
			       struct sps_pipe *pipe,
			       enum sps_event event_id)
{
	struct sps_bam_event_reg *event_reg;
	struct sps_q_event *sps_event;
	int index;

	index = SPS_EVENT_INDEX(event_id);
	if (index < 0 || index >= SPS_EVENT_INDEX(SPS_EVENT_MAX))
		return;

	event_reg = &pipe->sys.event_regs[index];
	sps_event = alloc_event(pipe, event_reg);
	if (sps_event != NULL) {
		sps_event->notify.event_id = event_id;
		sps_event->notify.user = event_reg->user;
		trigger_event(dev, pipe, event_reg, sps_event);
	}
}

/**
 * Handle a BAM pipe's WAKEUP interrupt sources
 *
 * This function creates the event notification for a BAM pipe's
 *    WAKEUP interrupt source.  The caller of this function must lock the BAM
 *    device's mutex.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe - pointer to pipe state
 *
 */
static void pipe_handler_wakeup(struct sps_bam *dev, struct sps_pipe *pipe)
{
	struct sps_bam_event_reg *event_reg;
	struct sps_q_event *event;
	u32 pipe_index = pipe->pipe_index;

	if (pipe->wake_up_is_one_shot) {
		SPS_DBG2(dev,
			"sps:BAM:%pa pipe %d wake_up_is_one_shot; irq_mask:0x%x\n",
			BAM_ID(dev), pipe_index, pipe->irq_mask);
		/* Disable the pipe WAKEUP interrupt source */
		pipe->irq_mask &= ~BAM_PIPE_IRQ_WAKE;
		pipe_set_irq(dev, pipe_index, pipe->polled);
	}

	event_reg = &pipe->sys.event_regs[SPS_EVENT_INDEX(SPS_EVENT_WAKEUP)];
	event = alloc_event(pipe, event_reg);
	if (event != NULL) {
		event->notify.event_id = SPS_EVENT_WAKEUP;
		event->notify.user = event_reg->user;
		trigger_event(dev, pipe, event_reg, event);
	}
}

/**
 * Handle a BAM pipe's EOT/INT interrupt sources
 *
 * This function creates the event notification for a BAM pipe's EOT interrupt
 *    source.  The caller of this function must lock the BAM device's mutex.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe - pointer to pipe state
 *
 */
static void pipe_handler_eot(struct sps_bam *dev, struct sps_pipe *pipe)
{
	struct sps_bam_event_reg *event_reg;
	struct sps_q_event *event;
	struct sps_iovec *desc;
	struct sps_iovec *cache;
	void **user;
	u32 *update_offset;
	u32 pipe_index = pipe->pipe_index;
	u32 offset;
	u32 end_offset;
	enum sps_event event_id;
	u32 flags;
	u32 enabled;
	int producer = (pipe->mode == SPS_MODE_SRC);

	if (pipe->sys.handler_eot) {
		/*
		 * This can happen if the pipe is configured for polling
		 * (IRQ disabled) and callback event generation.
		 * The client may perform a get_iovec() inside the callback.
		 */
		SPS_DBG(dev,
			"sps: still handling EOT for pipe %d\n",
			pipe->pipe_index);
		return;
	}

	pipe->sys.handler_eot = true;

	/* Get offset of last descriptor completed by the pipe */
	end_offset = bam_pipe_get_desc_read_offset(&dev->base, pipe_index);

	if (dev->ipc_loglevel == 0)
		SPS_DBG(dev,
			"sps: pipe index:%d; read pointer:0x%x; write pointer:0x%x; sys.acked_offset:0x%x\n",
			pipe->pipe_index, end_offset,
			bam_pipe_get_desc_write_offset(&dev->base, pipe_index),
			pipe->sys.acked_offset);

	if (producer && pipe->late_eot) {
		struct sps_iovec *desc_end;

		if (end_offset == 0)
			desc_end = (struct sps_iovec *)(pipe->sys.desc_buf
				+ pipe->desc_size - sizeof(struct sps_iovec));
		else
			desc_end = (struct sps_iovec *)	(pipe->sys.desc_buf
				+ end_offset - sizeof(struct sps_iovec));

		if (!(desc_end->flags & SPS_IOVEC_FLAG_EOT)) {
			if (end_offset == 0)
				end_offset = pipe->desc_size
					- sizeof(struct sps_iovec);
			else
				end_offset -= sizeof(struct sps_iovec);
		}
	}

	/* If no queue, then do not generate any events */
	if (pipe->sys.no_queue) {
		if (!pipe->sys.ack_xfers) {
			/* Client is not ACK'ing transfers, so do it now */
			pipe->sys.acked_offset = end_offset;
		}
		pipe->sys.handler_eot = false;
		SPS_DBG(dev,
			"sps: pipe %d has no queue\n", pipe->pipe_index);
		return;
	}

	/*
	 * Get offset of last descriptor processed by software,
	 * and update to the last descriptor completed by the pipe
	 */
	if (!pipe->sys.ack_xfers) {
		update_offset = &pipe->sys.acked_offset;
		offset = *update_offset;
	} else {
		update_offset = &pipe->sys.cache_offset;
		offset = *update_offset;
	}

	/* Are there any completed descriptors to process? */
	if (offset == end_offset) {
		pipe->sys.handler_eot = false;
		SPS_DBG(dev,
			"sps: there is no completed desc to process for pipe %d\n",
			pipe->pipe_index);
		return;
	}

	/* Determine enabled events */
	enabled = 0;
	if ((pipe->irq_mask & SPS_O_EOT))
		enabled |= SPS_IOVEC_FLAG_EOT;

	if ((pipe->irq_mask & SPS_O_DESC_DONE))
		enabled |= SPS_IOVEC_FLAG_INT;

	/*
	 * For producer pipe, update the cached descriptor byte count and flags.
	 * For consumer pipe, the BAM does not update the descriptors, so just
	 * use the cached copies.
	 */
	if (producer) {
		/*
		 * Do copies in a tight loop to increase chance of
		 * multi-descriptor burst accesses on the bus
		 */
		struct sps_iovec *desc_end;

		/* Set starting point for copy */
		desc = (struct sps_iovec *) (pipe->sys.desc_buf + offset);
		cache =	(struct sps_iovec *) (pipe->sys.desc_cache + offset);

		/* Fetch all completed descriptors to end of FIFO (wrap) */
		if (end_offset < offset) {
			desc_end = (struct sps_iovec *)
				   (pipe->sys.desc_buf + pipe->desc_size);
			while (desc < desc_end)
				*cache++ = *desc++;

			desc = (void *)pipe->sys.desc_buf;
			cache = (void *)pipe->sys.desc_cache;
		}

		/* Fetch all remaining completed descriptors (no wrap) */
		desc_end = (struct sps_iovec *)	(pipe->sys.desc_buf +
						 end_offset);
		while (desc < desc_end)
			*cache++ = *desc++;
	}

	/* Process all completed descriptors */
	cache = (struct sps_iovec *) (pipe->sys.desc_cache + offset);
	user = &pipe->sys.user_ptrs[offset / sizeof(struct sps_iovec)];
	for (;;) {
		SPS_DBG(dev,
			"sps: pipe index:%d; iovec addr:0x%pK; size:0x%x; flags:0x%x; enabled:0x%x; *user is %s NULL\n",
			pipe->pipe_index, (void *)(long)cache->addr,
			cache->size, cache->flags, enabled,
			(*user == NULL) ? "" : "not");

		/*
		 * Increment offset to next descriptor and update pipe offset
		 * so a client callback can fetch the I/O vector.
		 */
		offset += sizeof(struct sps_iovec);
		if (offset >= pipe->desc_size)
			/* Roll to start of descriptor FIFO */
			offset = 0;

		*update_offset = offset;
#ifdef SPS_BAM_STATISTICS
		pipe->sys.desc_rd_count++;
#endif /* SPS_BAM_STATISTICS */

		/* Did client request notification for this descriptor? */
		flags = cache->flags & enabled;
		if (*user != NULL || flags) {
			int index;

			if ((flags & SPS_IOVEC_FLAG_EOT))
				event_id = SPS_EVENT_EOT;
			else
				event_id = SPS_EVENT_DESC_DONE;

			index = SPS_EVENT_INDEX(event_id);
			event_reg = &pipe->sys.event_regs[index];
			event = alloc_event(pipe, event_reg);
			if (event != NULL) {
				/*
				 * Store the descriptor and user pointer
				 * in the notification
				 */
				event->notify.data.transfer.iovec = *cache;
				event->notify.data.transfer.user = *user;

				event->notify.event_id = event_id;
				event->notify.user = event_reg->user;
				trigger_event(dev, pipe, event_reg, event);
			} else {
				SPS_ERR(dev,
					"sps: pipe %d: event is NULL\n",
					pipe->pipe_index);
			}
#ifdef SPS_BAM_STATISTICS
			if (*user != NULL)
				pipe->sys.user_found++;
#endif /* SPS_BAM_STATISTICS */
		}

		/* Increment to next descriptor */
		if (offset == end_offset)
			break;	/* No more descriptors */

		if (offset) {
			cache++;
			user++;
		} else {
			cache = (void *)pipe->sys.desc_cache;
			user = pipe->sys.user_ptrs;
		}
	}

	pipe->sys.handler_eot = false;
}

/**
 * Handle a BAM pipe's interrupt sources
 *
 * This function handles a BAM pipe's interrupt sources.
 *    The caller of this function must lock the BAM device's mutex.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @return void
 *
 */
static void pipe_handler(struct sps_bam *dev, struct sps_pipe *pipe)
{
	u32 pipe_index;
	u32 status;
	enum sps_event event_id;

	/* Get interrupt sources and ack all */
	pipe_index = pipe->pipe_index;
	status = bam_pipe_get_and_clear_irq_status(&dev->base, pipe_index);

	SPS_DBG(dev, "sps: bam %pa.pipe %d.status=0x%x\n",
			BAM_ID(dev), pipe_index, status);

	/* Check for enabled interrupt sources */
	status &= pipe->irq_mask;
	if (status == 0)
		/* No enabled interrupt sources are active */
		return;

	/*
	 * Process the interrupt sources in order of frequency of occurrance.
	 * Check for early exit opportunities.
	 */

	if ((status & (SPS_O_EOT | SPS_O_DESC_DONE)) &&
	    (pipe->state & BAM_STATE_BAM2BAM) == 0) {
		pipe_handler_eot(dev, pipe);
		if (pipe->sys.no_queue) {
			/*
			 * EOT handler will not generate any event if there
			 * is no queue,
			 * so generate "empty" (no descriptor) event
			 */
			if ((status & SPS_O_EOT))
				event_id = SPS_EVENT_EOT;
			else
				event_id = SPS_EVENT_DESC_DONE;

			pipe_handler_generic(dev, pipe, event_id);
		}
		status &= ~(SPS_O_EOT | SPS_O_DESC_DONE);
		if (status == 0)
			return;
	}

	if ((status & SPS_O_WAKEUP)) {
		pipe_handler_wakeup(dev, pipe);
		status &= ~SPS_O_WAKEUP;
		if (status == 0)
			return;
	}

	if ((status & SPS_O_INACTIVE)) {
		pipe_handler_generic(dev, pipe, SPS_EVENT_INACTIVE);
		status &= ~SPS_O_INACTIVE;
		if (status == 0)
			return;
	}

	if ((status & SPS_O_OUT_OF_DESC)) {
		pipe_handler_generic(dev, pipe,
					     SPS_EVENT_OUT_OF_DESC);
		status &= ~SPS_O_OUT_OF_DESC;
		if (status == 0)
			return;
	}

	if ((status & SPS_O_RST_ERROR) && enhd_pipe) {
		SPS_ERR(dev, "sps:bam %pa ;pipe 0x%x irq status=0x%x\n"
				"sps: BAM_PIPE_IRQ_RST_ERROR\n",
				BAM_ID(dev), pipe_index, status);
		bam_output_register_content(&dev->base, dev->props.ee);
		pipe_handler_generic(dev, pipe,
					     SPS_EVENT_RST_ERROR);
		status &= ~SPS_O_RST_ERROR;
		if (status == 0)
			return;
	}

	if ((status & SPS_O_HRESP_ERROR) && enhd_pipe) {
		SPS_ERR(dev, "sps:bam %pa ;pipe 0x%x irq status=0x%x\n"
				"sps: BAM_PIPE_IRQ_HRESP_ERROR\n",
				BAM_ID(dev), pipe_index, status);
		bam_output_register_content(&dev->base, dev->props.ee);
		pipe_handler_generic(dev, pipe,
					     SPS_EVENT_HRESP_ERROR);
		status &= ~SPS_O_HRESP_ERROR;
		if (status == 0)
			return;
	}

	if ((status & SPS_EVENT_ERROR))
		pipe_handler_generic(dev, pipe, SPS_EVENT_ERROR);
}

/**
 * Get a BAM pipe event
 *
 */
int sps_bam_pipe_get_event(struct sps_bam *dev,
			   u32 pipe_index, struct sps_event_notify *notify)
{
	struct sps_pipe *pipe = dev->pipes[pipe_index];
	struct sps_q_event *event_queue;

	if (pipe->sys.no_queue) {
		SPS_ERR(dev,
			"sps:Invalid connection for event: BAM %pa pipe %d context 0x%pK\n",
			BAM_ID(dev), pipe_index, pipe);
		notify->event_id = SPS_EVENT_INVALID;
		return SPS_ERROR;
	}

	/* If pipe is polled, perform polling operation */
	if (pipe->polled && (pipe->state & BAM_STATE_BAM2BAM) == 0)
		pipe_handler_eot(dev, pipe);

	/* Pull an event off the synchronous event queue */
	if (list_empty(&pipe->sys.events_q)) {
		event_queue = NULL;
		SPS_DBG(dev, "sps:events_q of bam %pa is empty\n",
							BAM_ID(dev));
	} else {
		SPS_DBG(dev, "sps:events_q of bam %pa is not empty\n",
			BAM_ID(dev));
		event_queue =
		list_first_entry(&pipe->sys.events_q, struct sps_q_event,
				 list);
		list_del(&event_queue->list);
	}

	/* Update client's event buffer */
	if (event_queue == NULL) {
		/* No event queued, so set client's event to "invalid" */
		notify->event_id = SPS_EVENT_INVALID;
	} else {
		/*
		 * Copy event into client's buffer and return the event
		 * to the pool
		 */
		*notify = event_queue->notify;
		kfree(event_queue);
#ifdef SPS_BAM_STATISTICS
		pipe->sys.get_events++;
#endif /* SPS_BAM_STATISTICS */
	}

	return 0;
}

/**
 * Get processed I/O vector
 */
int sps_bam_pipe_get_iovec(struct sps_bam *dev, u32 pipe_index,
			   struct sps_iovec *iovec)
{
	struct sps_pipe *pipe = dev->pipes[pipe_index];
	struct sps_iovec *desc;
	u32 read_offset;

	/* Is this a valid pipe configured for get_iovec use? */
	if (!pipe->sys.ack_xfers ||
	    (pipe->state & BAM_STATE_BAM2BAM) != 0 ||
	    (pipe->state & BAM_STATE_REMOTE)) {
		return SPS_ERROR;
	}

	/* If pipe is polled and queue is enabled, perform polling operation */
	if ((pipe->polled || pipe->hybrid) && !pipe->sys.no_queue) {
		SPS_DBG(dev,
			"sps: BAM: %pa; pipe index:%d; polled is %d; hybrid is %d\n",
			BAM_ID(dev), pipe_index,
			pipe->polled, pipe->hybrid);
		pipe_handler_eot(dev, pipe);
	}

	/* Is there a completed descriptor? */
	if (pipe->sys.no_queue)
		read_offset =
		bam_pipe_get_desc_read_offset(&dev->base, pipe_index);
	else
		read_offset = pipe->sys.cache_offset;

	if (read_offset == pipe->sys.acked_offset) {
		/* No, so clear the iovec to indicate FIFO is empty */
		memset(iovec, 0, sizeof(*iovec));
		SPS_DBG(dev,
			"sps: BAM: %pa; pipe index:%d; no iovec to process\n",
			BAM_ID(dev), pipe_index);
		return 0;
	}

	/* Fetch next descriptor */
	desc = (struct sps_iovec *) (pipe->sys.desc_buf +
				     pipe->sys.acked_offset);
	*iovec = *desc;
#ifdef SPS_BAM_STATISTICS
	pipe->sys.get_iovecs++;
#endif /* SPS_BAM_STATISTICS */

	/* Update read/ACK offset */
	pipe->sys.acked_offset += sizeof(struct sps_iovec);
	if (pipe->sys.acked_offset >= pipe->desc_size)
		pipe->sys.acked_offset = 0;

	SPS_DBG(dev,
		"sps: pipe index:%d; iovec addr:0x%pK; size:0x%x; flags:0x%x; acked_offset:0x%x\n",
		pipe->pipe_index, (void *)(long)desc->addr,
		desc->size, desc->flags, pipe->sys.acked_offset);

	return 0;
}

/**
 * Determine whether a BAM pipe descriptor FIFO is empty
 *
 */
int sps_bam_pipe_is_empty(struct sps_bam *dev, u32 pipe_index,
				u32 *empty)
{
	struct sps_pipe *pipe = dev->pipes[pipe_index];
	u32 end_offset;
	u32 acked_offset;

	/* Is this a satellite connection? */
	if ((pipe->state & BAM_STATE_REMOTE)) {
		SPS_ERR(dev, "sps:Is empty on remote: BAM %pa pipe %d\n",
			BAM_ID(dev), pipe_index);
		return SPS_ERROR;
	}

	/* Get offset of last descriptor completed by the pipe */
	end_offset = bam_pipe_get_desc_read_offset(&dev->base, pipe_index);

	if ((pipe->state & BAM_STATE_BAM2BAM) == 0)
		/* System mode */
		acked_offset = pipe->sys.acked_offset;
	else
		/* BAM-to-BAM */
		acked_offset = bam_pipe_get_desc_write_offset(&dev->base,
							  pipe_index);


	/* Determine descriptor FIFO state */
	if (end_offset == acked_offset) {
		*empty = true;
	} else {
		if ((pipe->state & BAM_STATE_BAM2BAM) == 0) {
			*empty = false;
			SPS_DBG1(dev,
				"sps: pipe index:%d; this sys2bam pipe is NOT empty\n",
				pipe->pipe_index);
			return 0;
		}
		if (bam_pipe_check_zlt(&dev->base, pipe_index)) {
			bool p_idc;
			u32 next_write;

			p_idc = bam_pipe_check_pipe_empty(&dev->base,
								pipe_index);

			next_write = acked_offset + sizeof(struct sps_iovec);
			if (next_write >= pipe->desc_size)
				next_write = 0;

			if (next_write == end_offset) {
				*empty = true;
				if (!p_idc)
					SPS_DBG3(dev,
						"sps:BAM %pa pipe %d pipe empty checking for ZLT\n",
						BAM_ID(dev), pipe_index);
			} else {
				*empty = false;
			}
		} else {
			*empty = false;
		}
	}

	SPS_DBG1(dev,
		"sps: pipe index:%d; this pipe is %s empty\n",
		pipe->pipe_index, *empty ? "" : "NOT");

	return 0;
}

/**
 * Get number of free slots in a BAM pipe descriptor FIFO
 *
 */
int sps_bam_get_free_count(struct sps_bam *dev, u32 pipe_index,
				 u32 *count)
{
	struct sps_pipe *pipe = dev->pipes[pipe_index];
	u32 next_write;
	u32 free;

	/* Is this a BAM-to-BAM or satellite connection? */
	if ((pipe->state & (BAM_STATE_BAM2BAM | BAM_STATE_REMOTE))) {
		SPS_ERR(dev,
			"sps:Free count on BAM-to-BAM or remote: BAM %pa pipe %d\n",
			BAM_ID(dev), pipe_index);
		*count = 0;
		return SPS_ERROR;
	}

	/* Determine descriptor FIFO state */
	next_write = pipe->sys.desc_offset + sizeof(struct sps_iovec);
	if (next_write >= pipe->desc_size)
		next_write = 0;

	if (pipe->sys.acked_offset >= next_write)
		free = pipe->sys.acked_offset - next_write;
	else
		free = pipe->desc_size - next_write + pipe->sys.acked_offset;

	free /= sizeof(struct sps_iovec);
	*count = free;

	return 0;
}

/**
 * Set BAM pipe to satellite ownership
 *
 */
int sps_bam_set_satellite(struct sps_bam *dev, u32 pipe_index)
{
	struct sps_pipe *pipe = dev->pipes[pipe_index];

	/*
	 * Switch to satellite control is only supported on processor
	 * that controls the BAM global config on multi-EE BAMs
	 */
	if ((dev->props.manage & SPS_BAM_MGR_MULTI_EE) == 0 ||
	    (dev->props.manage & SPS_BAM_MGR_DEVICE_REMOTE)) {
		SPS_ERR(dev,
			"sps:Cannot grant satellite control to BAM %pa pipe %d\n",
			BAM_ID(dev), pipe_index);
		return SPS_ERROR;
	}

	/* Is this pipe locally controlled? */
	if ((dev->pipe_active_mask & (1UL << pipe_index)) == 0) {
		SPS_ERR(dev, "sps:BAM %pa pipe %d not local and active\n",
			BAM_ID(dev), pipe_index);
		return SPS_ERROR;
	}

	/* Disable local interrupts for this pipe */
	if (!pipe->polled)
		bam_pipe_set_irq(&dev->base, pipe_index, BAM_DISABLE,
					 pipe->irq_mask, dev->props.ee);

	if (BAM_VERSION_MTI_SUPPORT(dev->version)) {
		/*
		 * Set pipe to MTI interrupt mode.
		 * Must be performed after IRQ disable,
		 * because it is necessary to re-enable the IRQ to enable
		 * MTI generation.
		 * Set both pipe IRQ mask and MTI dest address to zero.
		 */
		if ((pipe->state & BAM_STATE_MTI) == 0 || pipe->polled) {
			bam_pipe_satellite_mti(&dev->base, pipe_index, 0,
						       dev->props.ee);
			pipe->state |= BAM_STATE_MTI;
		}
	}

	/* Indicate satellite control */
	list_del(&pipe->list);
	dev->pipe_active_mask &= ~(1UL << pipe_index);
	dev->pipe_remote_mask |= pipe->pipe_index_mask;
	pipe->state |= BAM_STATE_REMOTE;

	return 0;
}

/**
 * Get the number of unused descriptors in the descriptor FIFO
 * of a pipe
 */
int sps_bam_pipe_get_unused_desc_num(struct sps_bam *dev, u32 pipe_index,
					u32 *desc_num)
{
	u32 sw_offset, peer_offset, fifo_size;
	u32 desc_size = sizeof(struct sps_iovec);
	struct sps_pipe *pipe = dev->pipes[pipe_index];

	if (pipe == NULL)
		return SPS_ERROR;

	fifo_size = pipe->desc_size;

	sw_offset = bam_pipe_get_desc_read_offset(&dev->base, pipe_index);
	if ((dev->props.options & SPS_BAM_CACHED_WP) &&
		!(pipe->state & BAM_STATE_BAM2BAM)) {
		peer_offset = pipe->sys.desc_offset;
		SPS_DBG(dev,
			"sps:BAM %pa pipe %d: peer offset in cache:0x%x\n",
			BAM_ID(dev), pipe_index, peer_offset);
	} else {
		peer_offset = bam_pipe_get_desc_write_offset(&dev->base,
				pipe_index);
	}

	if (sw_offset <= peer_offset)
		*desc_num = (peer_offset - sw_offset) / desc_size;
	else
		*desc_num = (peer_offset + fifo_size - sw_offset) / desc_size;

	return 0;
}

/*
 * Check if a pipe of a BAM has any pending descriptor
 */
bool sps_bam_pipe_pending_desc(struct sps_bam *dev, u32 pipe_index)
{
	u32 sw_offset, peer_offset;

	sw_offset = bam_pipe_get_desc_read_offset(&dev->base, pipe_index);
	peer_offset = bam_pipe_get_desc_write_offset(&dev->base, pipe_index);

	if (sw_offset == peer_offset)
		return false;
	else
		return true;
}
