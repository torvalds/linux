// SPDX-License-Identifier: GPL-2.0
/*
 * MHI Endpoint bus stack
 *
 * Copyright (C) 2022 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dma-direction.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mhi_ep.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include "internal.h"

#define M0_WAIT_DELAY_MS	100
#define M0_WAIT_COUNT		100

static DEFINE_IDA(mhi_ep_cntrl_ida);

static int mhi_ep_create_device(struct mhi_ep_cntrl *mhi_cntrl, u32 ch_id);
static int mhi_ep_destroy_device(struct device *dev, void *data);

static int mhi_ep_send_event(struct mhi_ep_cntrl *mhi_cntrl, u32 ring_idx,
			     struct mhi_ring_element *el, bool bei)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	union mhi_ep_ring_ctx *ctx;
	struct mhi_ep_ring *ring;
	int ret;

	mutex_lock(&mhi_cntrl->event_lock);
	ring = &mhi_cntrl->mhi_event[ring_idx].ring;
	ctx = (union mhi_ep_ring_ctx *)&mhi_cntrl->ev_ctx_cache[ring_idx];
	if (!ring->started) {
		ret = mhi_ep_ring_start(mhi_cntrl, ring, ctx);
		if (ret) {
			dev_err(dev, "Error starting event ring (%u)\n", ring_idx);
			goto err_unlock;
		}
	}

	/* Add element to the event ring */
	ret = mhi_ep_ring_add_element(ring, el);
	if (ret) {
		dev_err(dev, "Error adding element to event ring (%u)\n", ring_idx);
		goto err_unlock;
	}

	mutex_unlock(&mhi_cntrl->event_lock);

	/*
	 * Raise IRQ to host only if the BEI flag is not set in TRE. Host might
	 * set this flag for interrupt moderation as per MHI protocol.
	 */
	if (!bei)
		mhi_cntrl->raise_irq(mhi_cntrl, ring->irq_vector);

	return 0;

err_unlock:
	mutex_unlock(&mhi_cntrl->event_lock);

	return ret;
}

static int mhi_ep_send_completion_event(struct mhi_ep_cntrl *mhi_cntrl, struct mhi_ep_ring *ring,
					struct mhi_ring_element *tre, u32 len, enum mhi_ev_ccs code)
{
	struct mhi_ring_element event = {};

	event.ptr = cpu_to_le64(ring->rbase + ring->rd_offset * sizeof(*tre));
	event.dword[0] = MHI_TRE_EV_DWORD0(code, len);
	event.dword[1] = MHI_TRE_EV_DWORD1(ring->ch_id, MHI_PKT_TYPE_TX_EVENT);

	return mhi_ep_send_event(mhi_cntrl, ring->er_index, &event, MHI_TRE_DATA_GET_BEI(tre));
}

int mhi_ep_send_state_change_event(struct mhi_ep_cntrl *mhi_cntrl, enum mhi_state state)
{
	struct mhi_ring_element event = {};

	event.dword[0] = MHI_SC_EV_DWORD0(state);
	event.dword[1] = MHI_SC_EV_DWORD1(MHI_PKT_TYPE_STATE_CHANGE_EVENT);

	return mhi_ep_send_event(mhi_cntrl, 0, &event, 0);
}

int mhi_ep_send_ee_event(struct mhi_ep_cntrl *mhi_cntrl, enum mhi_ee_type exec_env)
{
	struct mhi_ring_element event = {};

	event.dword[0] = MHI_EE_EV_DWORD0(exec_env);
	event.dword[1] = MHI_SC_EV_DWORD1(MHI_PKT_TYPE_EE_EVENT);

	return mhi_ep_send_event(mhi_cntrl, 0, &event, 0);
}

static int mhi_ep_send_cmd_comp_event(struct mhi_ep_cntrl *mhi_cntrl, enum mhi_ev_ccs code)
{
	struct mhi_ep_ring *ring = &mhi_cntrl->mhi_cmd->ring;
	struct mhi_ring_element event = {};

	event.ptr = cpu_to_le64(ring->rbase + ring->rd_offset * sizeof(struct mhi_ring_element));
	event.dword[0] = MHI_CC_EV_DWORD0(code);
	event.dword[1] = MHI_CC_EV_DWORD1(MHI_PKT_TYPE_CMD_COMPLETION_EVENT);

	return mhi_ep_send_event(mhi_cntrl, 0, &event, 0);
}

static int mhi_ep_process_cmd_ring(struct mhi_ep_ring *ring, struct mhi_ring_element *el)
{
	struct mhi_ep_cntrl *mhi_cntrl = ring->mhi_cntrl;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_result result = {};
	struct mhi_ep_chan *mhi_chan;
	struct mhi_ep_ring *ch_ring;
	u32 tmp, ch_id;
	int ret;

	ch_id = MHI_TRE_GET_CMD_CHID(el);
	mhi_chan = &mhi_cntrl->mhi_chan[ch_id];
	ch_ring = &mhi_cntrl->mhi_chan[ch_id].ring;

	switch (MHI_TRE_GET_CMD_TYPE(el)) {
	case MHI_PKT_TYPE_START_CHAN_CMD:
		dev_dbg(dev, "Received START command for channel (%u)\n", ch_id);

		mutex_lock(&mhi_chan->lock);
		/* Initialize and configure the corresponding channel ring */
		if (!ch_ring->started) {
			ret = mhi_ep_ring_start(mhi_cntrl, ch_ring,
				(union mhi_ep_ring_ctx *)&mhi_cntrl->ch_ctx_cache[ch_id]);
			if (ret) {
				dev_err(dev, "Failed to start ring for channel (%u)\n", ch_id);
				ret = mhi_ep_send_cmd_comp_event(mhi_cntrl,
							MHI_EV_CC_UNDEFINED_ERR);
				if (ret)
					dev_err(dev, "Error sending completion event: %d\n", ret);

				goto err_unlock;
			}
		}

		/* Set channel state to RUNNING */
		mhi_chan->state = MHI_CH_STATE_RUNNING;
		tmp = le32_to_cpu(mhi_cntrl->ch_ctx_cache[ch_id].chcfg);
		tmp &= ~CHAN_CTX_CHSTATE_MASK;
		tmp |= FIELD_PREP(CHAN_CTX_CHSTATE_MASK, MHI_CH_STATE_RUNNING);
		mhi_cntrl->ch_ctx_cache[ch_id].chcfg = cpu_to_le32(tmp);

		ret = mhi_ep_send_cmd_comp_event(mhi_cntrl, MHI_EV_CC_SUCCESS);
		if (ret) {
			dev_err(dev, "Error sending command completion event (%u)\n",
				MHI_EV_CC_SUCCESS);
			goto err_unlock;
		}

		mutex_unlock(&mhi_chan->lock);

		/*
		 * Create MHI device only during UL channel start. Since the MHI
		 * channels operate in a pair, we'll associate both UL and DL
		 * channels to the same device.
		 *
		 * We also need to check for mhi_dev != NULL because, the host
		 * will issue START_CHAN command during resume and we don't
		 * destroy the device during suspend.
		 */
		if (!(ch_id % 2) && !mhi_chan->mhi_dev) {
			ret = mhi_ep_create_device(mhi_cntrl, ch_id);
			if (ret) {
				dev_err(dev, "Error creating device for channel (%u)\n", ch_id);
				mhi_ep_handle_syserr(mhi_cntrl);
				return ret;
			}
		}

		/* Finally, enable DB for the channel */
		mhi_ep_mmio_enable_chdb(mhi_cntrl, ch_id);

		break;
	case MHI_PKT_TYPE_STOP_CHAN_CMD:
		dev_dbg(dev, "Received STOP command for channel (%u)\n", ch_id);
		if (!ch_ring->started) {
			dev_err(dev, "Channel (%u) not opened\n", ch_id);
			return -ENODEV;
		}

		mutex_lock(&mhi_chan->lock);
		/* Disable DB for the channel */
		mhi_ep_mmio_disable_chdb(mhi_cntrl, ch_id);

		/* Send channel disconnect status to client drivers */
		if (mhi_chan->xfer_cb) {
			result.transaction_status = -ENOTCONN;
			result.bytes_xferd = 0;
			mhi_chan->xfer_cb(mhi_chan->mhi_dev, &result);
		}

		/* Set channel state to STOP */
		mhi_chan->state = MHI_CH_STATE_STOP;
		tmp = le32_to_cpu(mhi_cntrl->ch_ctx_cache[ch_id].chcfg);
		tmp &= ~CHAN_CTX_CHSTATE_MASK;
		tmp |= FIELD_PREP(CHAN_CTX_CHSTATE_MASK, MHI_CH_STATE_STOP);
		mhi_cntrl->ch_ctx_cache[ch_id].chcfg = cpu_to_le32(tmp);

		ret = mhi_ep_send_cmd_comp_event(mhi_cntrl, MHI_EV_CC_SUCCESS);
		if (ret) {
			dev_err(dev, "Error sending command completion event (%u)\n",
				MHI_EV_CC_SUCCESS);
			goto err_unlock;
		}

		mutex_unlock(&mhi_chan->lock);
		break;
	case MHI_PKT_TYPE_RESET_CHAN_CMD:
		dev_dbg(dev, "Received RESET command for channel (%u)\n", ch_id);
		if (!ch_ring->started) {
			dev_err(dev, "Channel (%u) not opened\n", ch_id);
			return -ENODEV;
		}

		mutex_lock(&mhi_chan->lock);
		/* Stop and reset the transfer ring */
		mhi_ep_ring_reset(mhi_cntrl, ch_ring);

		/* Send channel disconnect status to client driver */
		if (mhi_chan->xfer_cb) {
			result.transaction_status = -ENOTCONN;
			result.bytes_xferd = 0;
			mhi_chan->xfer_cb(mhi_chan->mhi_dev, &result);
		}

		/* Set channel state to DISABLED */
		mhi_chan->state = MHI_CH_STATE_DISABLED;
		tmp = le32_to_cpu(mhi_cntrl->ch_ctx_cache[ch_id].chcfg);
		tmp &= ~CHAN_CTX_CHSTATE_MASK;
		tmp |= FIELD_PREP(CHAN_CTX_CHSTATE_MASK, MHI_CH_STATE_DISABLED);
		mhi_cntrl->ch_ctx_cache[ch_id].chcfg = cpu_to_le32(tmp);

		ret = mhi_ep_send_cmd_comp_event(mhi_cntrl, MHI_EV_CC_SUCCESS);
		if (ret) {
			dev_err(dev, "Error sending command completion event (%u)\n",
				MHI_EV_CC_SUCCESS);
			goto err_unlock;
		}

		mutex_unlock(&mhi_chan->lock);
		break;
	default:
		dev_err(dev, "Invalid command received: %lu for channel (%u)\n",
			MHI_TRE_GET_CMD_TYPE(el), ch_id);
		return -EINVAL;
	}

	return 0;

err_unlock:
	mutex_unlock(&mhi_chan->lock);

	return ret;
}

bool mhi_ep_queue_is_empty(struct mhi_ep_device *mhi_dev, enum dma_data_direction dir)
{
	struct mhi_ep_chan *mhi_chan = (dir == DMA_FROM_DEVICE) ? mhi_dev->dl_chan :
								mhi_dev->ul_chan;
	struct mhi_ep_cntrl *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct mhi_ep_ring *ring = &mhi_cntrl->mhi_chan[mhi_chan->chan].ring;

	return !!(ring->rd_offset == ring->wr_offset);
}
EXPORT_SYMBOL_GPL(mhi_ep_queue_is_empty);

static int mhi_ep_read_channel(struct mhi_ep_cntrl *mhi_cntrl,
				struct mhi_ep_ring *ring,
				struct mhi_result *result,
				u32 len)
{
	struct mhi_ep_chan *mhi_chan = &mhi_cntrl->mhi_chan[ring->ch_id];
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	size_t tr_len, read_offset, write_offset;
	struct mhi_ring_element *el;
	bool tr_done = false;
	void *write_addr;
	u64 read_addr;
	u32 buf_left;
	int ret;

	buf_left = len;

	do {
		/* Don't process the transfer ring if the channel is not in RUNNING state */
		if (mhi_chan->state != MHI_CH_STATE_RUNNING) {
			dev_err(dev, "Channel not available\n");
			return -ENODEV;
		}

		el = &ring->ring_cache[ring->rd_offset];

		/* Check if there is data pending to be read from previous read operation */
		if (mhi_chan->tre_bytes_left) {
			dev_dbg(dev, "TRE bytes remaining: %u\n", mhi_chan->tre_bytes_left);
			tr_len = min(buf_left, mhi_chan->tre_bytes_left);
		} else {
			mhi_chan->tre_loc = MHI_TRE_DATA_GET_PTR(el);
			mhi_chan->tre_size = MHI_TRE_DATA_GET_LEN(el);
			mhi_chan->tre_bytes_left = mhi_chan->tre_size;

			tr_len = min(buf_left, mhi_chan->tre_size);
		}

		read_offset = mhi_chan->tre_size - mhi_chan->tre_bytes_left;
		write_offset = len - buf_left;
		read_addr = mhi_chan->tre_loc + read_offset;
		write_addr = result->buf_addr + write_offset;

		dev_dbg(dev, "Reading %zd bytes from channel (%u)\n", tr_len, ring->ch_id);
		ret = mhi_cntrl->read_from_host(mhi_cntrl, read_addr, write_addr, tr_len);
		if (ret < 0) {
			dev_err(&mhi_chan->mhi_dev->dev, "Error reading from channel\n");
			return ret;
		}

		buf_left -= tr_len;
		mhi_chan->tre_bytes_left -= tr_len;

		/*
		 * Once the TRE (Transfer Ring Element) of a TD (Transfer Descriptor) has been
		 * read completely:
		 *
		 * 1. Send completion event to the host based on the flags set in TRE.
		 * 2. Increment the local read offset of the transfer ring.
		 */
		if (!mhi_chan->tre_bytes_left) {
			/*
			 * The host will split the data packet into multiple TREs if it can't fit
			 * the packet in a single TRE. In that case, CHAIN flag will be set by the
			 * host for all TREs except the last one.
			 */
			if (MHI_TRE_DATA_GET_CHAIN(el)) {
				/*
				 * IEOB (Interrupt on End of Block) flag will be set by the host if
				 * it expects the completion event for all TREs of a TD.
				 */
				if (MHI_TRE_DATA_GET_IEOB(el)) {
					ret = mhi_ep_send_completion_event(mhi_cntrl, ring, el,
								     MHI_TRE_DATA_GET_LEN(el),
								     MHI_EV_CC_EOB);
					if (ret < 0) {
						dev_err(&mhi_chan->mhi_dev->dev,
							"Error sending transfer compl. event\n");
						return ret;
					}
				}
			} else {
				/*
				 * IEOT (Interrupt on End of Transfer) flag will be set by the host
				 * for the last TRE of the TD and expects the completion event for
				 * the same.
				 */
				if (MHI_TRE_DATA_GET_IEOT(el)) {
					ret = mhi_ep_send_completion_event(mhi_cntrl, ring, el,
								     MHI_TRE_DATA_GET_LEN(el),
								     MHI_EV_CC_EOT);
					if (ret < 0) {
						dev_err(&mhi_chan->mhi_dev->dev,
							"Error sending transfer compl. event\n");
						return ret;
					}
				}

				tr_done = true;
			}

			mhi_ep_ring_inc_index(ring);
		}

		result->bytes_xferd += tr_len;
	} while (buf_left && !tr_done);

	return 0;
}

static int mhi_ep_process_ch_ring(struct mhi_ep_ring *ring, struct mhi_ring_element *el)
{
	struct mhi_ep_cntrl *mhi_cntrl = ring->mhi_cntrl;
	struct mhi_result result = {};
	u32 len = MHI_EP_DEFAULT_MTU;
	struct mhi_ep_chan *mhi_chan;
	int ret;

	mhi_chan = &mhi_cntrl->mhi_chan[ring->ch_id];

	/*
	 * Bail out if transfer callback is not registered for the channel.
	 * This is most likely due to the client driver not loaded at this point.
	 */
	if (!mhi_chan->xfer_cb) {
		dev_err(&mhi_chan->mhi_dev->dev, "Client driver not available\n");
		return -ENODEV;
	}

	if (ring->ch_id % 2) {
		/* DL channel */
		result.dir = mhi_chan->dir;
		mhi_chan->xfer_cb(mhi_chan->mhi_dev, &result);
	} else {
		/* UL channel */
		result.buf_addr = kzalloc(len, GFP_KERNEL);
		if (!result.buf_addr)
			return -ENOMEM;

		do {
			ret = mhi_ep_read_channel(mhi_cntrl, ring, &result, len);
			if (ret < 0) {
				dev_err(&mhi_chan->mhi_dev->dev, "Failed to read channel\n");
				kfree(result.buf_addr);
				return ret;
			}

			result.dir = mhi_chan->dir;
			mhi_chan->xfer_cb(mhi_chan->mhi_dev, &result);
			result.bytes_xferd = 0;
			memset(result.buf_addr, 0, len);

			/* Read until the ring becomes empty */
		} while (!mhi_ep_queue_is_empty(mhi_chan->mhi_dev, DMA_TO_DEVICE));

		kfree(result.buf_addr);
	}

	return 0;
}

/* TODO: Handle partially formed TDs */
int mhi_ep_queue_skb(struct mhi_ep_device *mhi_dev, struct sk_buff *skb)
{
	struct mhi_ep_cntrl *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct mhi_ep_chan *mhi_chan = mhi_dev->dl_chan;
	struct device *dev = &mhi_chan->mhi_dev->dev;
	struct mhi_ring_element *el;
	u32 buf_left, read_offset;
	struct mhi_ep_ring *ring;
	enum mhi_ev_ccs code;
	void *read_addr;
	u64 write_addr;
	size_t tr_len;
	u32 tre_len;
	int ret;

	buf_left = skb->len;
	ring = &mhi_cntrl->mhi_chan[mhi_chan->chan].ring;

	mutex_lock(&mhi_chan->lock);

	do {
		/* Don't process the transfer ring if the channel is not in RUNNING state */
		if (mhi_chan->state != MHI_CH_STATE_RUNNING) {
			dev_err(dev, "Channel not available\n");
			ret = -ENODEV;
			goto err_exit;
		}

		if (mhi_ep_queue_is_empty(mhi_dev, DMA_FROM_DEVICE)) {
			dev_err(dev, "TRE not available!\n");
			ret = -ENOSPC;
			goto err_exit;
		}

		el = &ring->ring_cache[ring->rd_offset];
		tre_len = MHI_TRE_DATA_GET_LEN(el);

		tr_len = min(buf_left, tre_len);
		read_offset = skb->len - buf_left;
		read_addr = skb->data + read_offset;
		write_addr = MHI_TRE_DATA_GET_PTR(el);

		dev_dbg(dev, "Writing %zd bytes to channel (%u)\n", tr_len, ring->ch_id);
		ret = mhi_cntrl->write_to_host(mhi_cntrl, read_addr, write_addr, tr_len);
		if (ret < 0) {
			dev_err(dev, "Error writing to the channel\n");
			goto err_exit;
		}

		buf_left -= tr_len;
		/*
		 * For all TREs queued by the host for DL channel, only the EOT flag will be set.
		 * If the packet doesn't fit into a single TRE, send the OVERFLOW event to
		 * the host so that the host can adjust the packet boundary to next TREs. Else send
		 * the EOT event to the host indicating the packet boundary.
		 */
		if (buf_left)
			code = MHI_EV_CC_OVERFLOW;
		else
			code = MHI_EV_CC_EOT;

		ret = mhi_ep_send_completion_event(mhi_cntrl, ring, el, tr_len, code);
		if (ret) {
			dev_err(dev, "Error sending transfer completion event\n");
			goto err_exit;
		}

		mhi_ep_ring_inc_index(ring);
	} while (buf_left);

	mutex_unlock(&mhi_chan->lock);

	return 0;

err_exit:
	mutex_unlock(&mhi_chan->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(mhi_ep_queue_skb);

static int mhi_ep_cache_host_cfg(struct mhi_ep_cntrl *mhi_cntrl)
{
	size_t cmd_ctx_host_size, ch_ctx_host_size, ev_ctx_host_size;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	int ret;

	/* Update the number of event rings (NER) programmed by the host */
	mhi_ep_mmio_update_ner(mhi_cntrl);

	dev_dbg(dev, "Number of Event rings: %u, HW Event rings: %u\n",
		 mhi_cntrl->event_rings, mhi_cntrl->hw_event_rings);

	ch_ctx_host_size = sizeof(struct mhi_chan_ctxt) * mhi_cntrl->max_chan;
	ev_ctx_host_size = sizeof(struct mhi_event_ctxt) * mhi_cntrl->event_rings;
	cmd_ctx_host_size = sizeof(struct mhi_cmd_ctxt) * NR_OF_CMD_RINGS;

	/* Get the channel context base pointer from host */
	mhi_ep_mmio_get_chc_base(mhi_cntrl);

	/* Allocate and map memory for caching host channel context */
	ret = mhi_cntrl->alloc_map(mhi_cntrl, mhi_cntrl->ch_ctx_host_pa,
				   &mhi_cntrl->ch_ctx_cache_phys,
				   (void __iomem **) &mhi_cntrl->ch_ctx_cache,
				   ch_ctx_host_size);
	if (ret) {
		dev_err(dev, "Failed to allocate and map ch_ctx_cache\n");
		return ret;
	}

	/* Get the event context base pointer from host */
	mhi_ep_mmio_get_erc_base(mhi_cntrl);

	/* Allocate and map memory for caching host event context */
	ret = mhi_cntrl->alloc_map(mhi_cntrl, mhi_cntrl->ev_ctx_host_pa,
				   &mhi_cntrl->ev_ctx_cache_phys,
				   (void __iomem **) &mhi_cntrl->ev_ctx_cache,
				   ev_ctx_host_size);
	if (ret) {
		dev_err(dev, "Failed to allocate and map ev_ctx_cache\n");
		goto err_ch_ctx;
	}

	/* Get the command context base pointer from host */
	mhi_ep_mmio_get_crc_base(mhi_cntrl);

	/* Allocate and map memory for caching host command context */
	ret = mhi_cntrl->alloc_map(mhi_cntrl, mhi_cntrl->cmd_ctx_host_pa,
				   &mhi_cntrl->cmd_ctx_cache_phys,
				   (void __iomem **) &mhi_cntrl->cmd_ctx_cache,
				   cmd_ctx_host_size);
	if (ret) {
		dev_err(dev, "Failed to allocate and map cmd_ctx_cache\n");
		goto err_ev_ctx;
	}

	/* Initialize command ring */
	ret = mhi_ep_ring_start(mhi_cntrl, &mhi_cntrl->mhi_cmd->ring,
				(union mhi_ep_ring_ctx *)mhi_cntrl->cmd_ctx_cache);
	if (ret) {
		dev_err(dev, "Failed to start the command ring\n");
		goto err_cmd_ctx;
	}

	return ret;

err_cmd_ctx:
	mhi_cntrl->unmap_free(mhi_cntrl, mhi_cntrl->cmd_ctx_host_pa, mhi_cntrl->cmd_ctx_cache_phys,
			      (void __iomem *) mhi_cntrl->cmd_ctx_cache, cmd_ctx_host_size);

err_ev_ctx:
	mhi_cntrl->unmap_free(mhi_cntrl, mhi_cntrl->ev_ctx_host_pa, mhi_cntrl->ev_ctx_cache_phys,
			      (void __iomem *) mhi_cntrl->ev_ctx_cache, ev_ctx_host_size);

err_ch_ctx:
	mhi_cntrl->unmap_free(mhi_cntrl, mhi_cntrl->ch_ctx_host_pa, mhi_cntrl->ch_ctx_cache_phys,
			      (void __iomem *) mhi_cntrl->ch_ctx_cache, ch_ctx_host_size);

	return ret;
}

static void mhi_ep_free_host_cfg(struct mhi_ep_cntrl *mhi_cntrl)
{
	size_t cmd_ctx_host_size, ch_ctx_host_size, ev_ctx_host_size;

	ch_ctx_host_size = sizeof(struct mhi_chan_ctxt) * mhi_cntrl->max_chan;
	ev_ctx_host_size = sizeof(struct mhi_event_ctxt) * mhi_cntrl->event_rings;
	cmd_ctx_host_size = sizeof(struct mhi_cmd_ctxt) * NR_OF_CMD_RINGS;

	mhi_cntrl->unmap_free(mhi_cntrl, mhi_cntrl->cmd_ctx_host_pa, mhi_cntrl->cmd_ctx_cache_phys,
			      (void __iomem *) mhi_cntrl->cmd_ctx_cache, cmd_ctx_host_size);

	mhi_cntrl->unmap_free(mhi_cntrl, mhi_cntrl->ev_ctx_host_pa, mhi_cntrl->ev_ctx_cache_phys,
			      (void __iomem *) mhi_cntrl->ev_ctx_cache, ev_ctx_host_size);

	mhi_cntrl->unmap_free(mhi_cntrl, mhi_cntrl->ch_ctx_host_pa, mhi_cntrl->ch_ctx_cache_phys,
			      (void __iomem *) mhi_cntrl->ch_ctx_cache, ch_ctx_host_size);
}

static void mhi_ep_enable_int(struct mhi_ep_cntrl *mhi_cntrl)
{
	/*
	 * Doorbell interrupts are enabled when the corresponding channel gets started.
	 * Enabling all interrupts here triggers spurious irqs as some of the interrupts
	 * associated with hw channels always get triggered.
	 */
	mhi_ep_mmio_enable_ctrl_interrupt(mhi_cntrl);
	mhi_ep_mmio_enable_cmdb_interrupt(mhi_cntrl);
}

static int mhi_ep_enable(struct mhi_ep_cntrl *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	enum mhi_state state;
	bool mhi_reset;
	u32 count = 0;
	int ret;

	/* Wait for Host to set the M0 state */
	do {
		msleep(M0_WAIT_DELAY_MS);
		mhi_ep_mmio_get_mhi_state(mhi_cntrl, &state, &mhi_reset);
		if (mhi_reset) {
			/* Clear the MHI reset if host is in reset state */
			mhi_ep_mmio_clear_reset(mhi_cntrl);
			dev_info(dev, "Detected Host reset while waiting for M0\n");
		}
		count++;
	} while (state != MHI_STATE_M0 && count < M0_WAIT_COUNT);

	if (state != MHI_STATE_M0) {
		dev_err(dev, "Host failed to enter M0\n");
		return -ETIMEDOUT;
	}

	ret = mhi_ep_cache_host_cfg(mhi_cntrl);
	if (ret) {
		dev_err(dev, "Failed to cache host config\n");
		return ret;
	}

	mhi_ep_mmio_set_env(mhi_cntrl, MHI_EE_AMSS);

	/* Enable all interrupts now */
	mhi_ep_enable_int(mhi_cntrl);

	return 0;
}

static void mhi_ep_cmd_ring_worker(struct work_struct *work)
{
	struct mhi_ep_cntrl *mhi_cntrl = container_of(work, struct mhi_ep_cntrl, cmd_ring_work);
	struct mhi_ep_ring *ring = &mhi_cntrl->mhi_cmd->ring;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_ring_element *el;
	int ret;

	/* Update the write offset for the ring */
	ret = mhi_ep_update_wr_offset(ring);
	if (ret) {
		dev_err(dev, "Error updating write offset for ring\n");
		return;
	}

	/* Sanity check to make sure there are elements in the ring */
	if (ring->rd_offset == ring->wr_offset)
		return;

	/*
	 * Process command ring element till write offset. In case of an error, just try to
	 * process next element.
	 */
	while (ring->rd_offset != ring->wr_offset) {
		el = &ring->ring_cache[ring->rd_offset];

		ret = mhi_ep_process_cmd_ring(ring, el);
		if (ret)
			dev_err(dev, "Error processing cmd ring element: %zu\n", ring->rd_offset);

		mhi_ep_ring_inc_index(ring);
	}
}

static void mhi_ep_ch_ring_worker(struct work_struct *work)
{
	struct mhi_ep_cntrl *mhi_cntrl = container_of(work, struct mhi_ep_cntrl, ch_ring_work);
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_ep_ring_item *itr, *tmp;
	struct mhi_ring_element *el;
	struct mhi_ep_ring *ring;
	struct mhi_ep_chan *chan;
	unsigned long flags;
	LIST_HEAD(head);
	int ret;

	spin_lock_irqsave(&mhi_cntrl->list_lock, flags);
	list_splice_tail_init(&mhi_cntrl->ch_db_list, &head);
	spin_unlock_irqrestore(&mhi_cntrl->list_lock, flags);

	/* Process each queued channel ring. In case of an error, just process next element. */
	list_for_each_entry_safe(itr, tmp, &head, node) {
		list_del(&itr->node);
		ring = itr->ring;

		chan = &mhi_cntrl->mhi_chan[ring->ch_id];
		mutex_lock(&chan->lock);

		/*
		 * The ring could've stopped while we waited to grab the (chan->lock), so do
		 * a sanity check before going further.
		 */
		if (!ring->started) {
			mutex_unlock(&chan->lock);
			kfree(itr);
			continue;
		}

		/* Update the write offset for the ring */
		ret = mhi_ep_update_wr_offset(ring);
		if (ret) {
			dev_err(dev, "Error updating write offset for ring\n");
			mutex_unlock(&chan->lock);
			kfree(itr);
			continue;
		}

		/* Sanity check to make sure there are elements in the ring */
		if (ring->rd_offset == ring->wr_offset) {
			mutex_unlock(&chan->lock);
			kfree(itr);
			continue;
		}

		el = &ring->ring_cache[ring->rd_offset];

		dev_dbg(dev, "Processing the ring for channel (%u)\n", ring->ch_id);
		ret = mhi_ep_process_ch_ring(ring, el);
		if (ret) {
			dev_err(dev, "Error processing ring for channel (%u): %d\n",
				ring->ch_id, ret);
			mutex_unlock(&chan->lock);
			kfree(itr);
			continue;
		}

		mutex_unlock(&chan->lock);
		kfree(itr);
	}
}

static void mhi_ep_state_worker(struct work_struct *work)
{
	struct mhi_ep_cntrl *mhi_cntrl = container_of(work, struct mhi_ep_cntrl, state_work);
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	struct mhi_ep_state_transition *itr, *tmp;
	unsigned long flags;
	LIST_HEAD(head);
	int ret;

	spin_lock_irqsave(&mhi_cntrl->list_lock, flags);
	list_splice_tail_init(&mhi_cntrl->st_transition_list, &head);
	spin_unlock_irqrestore(&mhi_cntrl->list_lock, flags);

	list_for_each_entry_safe(itr, tmp, &head, node) {
		list_del(&itr->node);
		dev_dbg(dev, "Handling MHI state transition to %s\n",
			 mhi_state_str(itr->state));

		switch (itr->state) {
		case MHI_STATE_M0:
			ret = mhi_ep_set_m0_state(mhi_cntrl);
			if (ret)
				dev_err(dev, "Failed to transition to M0 state\n");
			break;
		case MHI_STATE_M3:
			ret = mhi_ep_set_m3_state(mhi_cntrl);
			if (ret)
				dev_err(dev, "Failed to transition to M3 state\n");
			break;
		default:
			dev_err(dev, "Invalid MHI state transition: %d\n", itr->state);
			break;
		}
		kfree(itr);
	}
}

static void mhi_ep_queue_channel_db(struct mhi_ep_cntrl *mhi_cntrl, unsigned long ch_int,
				    u32 ch_idx)
{
	struct mhi_ep_ring_item *item;
	struct mhi_ep_ring *ring;
	bool work = !!ch_int;
	LIST_HEAD(head);
	u32 i;

	/* First add the ring items to a local list */
	for_each_set_bit(i, &ch_int, 32) {
		/* Channel index varies for each register: 0, 32, 64, 96 */
		u32 ch_id = ch_idx + i;

		ring = &mhi_cntrl->mhi_chan[ch_id].ring;
		item = kzalloc(sizeof(*item), GFP_ATOMIC);
		if (!item)
			return;

		item->ring = ring;
		list_add_tail(&item->node, &head);
	}

	/* Now, splice the local list into ch_db_list and queue the work item */
	if (work) {
		spin_lock(&mhi_cntrl->list_lock);
		list_splice_tail_init(&head, &mhi_cntrl->ch_db_list);
		spin_unlock(&mhi_cntrl->list_lock);

		queue_work(mhi_cntrl->wq, &mhi_cntrl->ch_ring_work);
	}
}

/*
 * Channel interrupt statuses are contained in 4 registers each of 32bit length.
 * For checking all interrupts, we need to loop through each registers and then
 * check for bits set.
 */
static void mhi_ep_check_channel_interrupt(struct mhi_ep_cntrl *mhi_cntrl)
{
	u32 ch_int, ch_idx, i;

	/* Bail out if there is no channel doorbell interrupt */
	if (!mhi_ep_mmio_read_chdb_status_interrupts(mhi_cntrl))
		return;

	for (i = 0; i < MHI_MASK_ROWS_CH_DB; i++) {
		ch_idx = i * MHI_MASK_CH_LEN;

		/* Only process channel interrupt if the mask is enabled */
		ch_int = mhi_cntrl->chdb[i].status & mhi_cntrl->chdb[i].mask;
		if (ch_int) {
			mhi_ep_queue_channel_db(mhi_cntrl, ch_int, ch_idx);
			mhi_ep_mmio_write(mhi_cntrl, MHI_CHDB_INT_CLEAR_n(i),
							mhi_cntrl->chdb[i].status);
		}
	}
}

static void mhi_ep_process_ctrl_interrupt(struct mhi_ep_cntrl *mhi_cntrl,
					 enum mhi_state state)
{
	struct mhi_ep_state_transition *item;

	item = kzalloc(sizeof(*item), GFP_ATOMIC);
	if (!item)
		return;

	item->state = state;
	spin_lock(&mhi_cntrl->list_lock);
	list_add_tail(&item->node, &mhi_cntrl->st_transition_list);
	spin_unlock(&mhi_cntrl->list_lock);

	queue_work(mhi_cntrl->wq, &mhi_cntrl->state_work);
}

/*
 * Interrupt handler that services interrupts raised by the host writing to
 * MHICTRL and Command ring doorbell (CRDB) registers for state change and
 * channel interrupts.
 */
static irqreturn_t mhi_ep_irq(int irq, void *data)
{
	struct mhi_ep_cntrl *mhi_cntrl = data;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	enum mhi_state state;
	u32 int_value;
	bool mhi_reset;

	/* Acknowledge the ctrl interrupt */
	int_value = mhi_ep_mmio_read(mhi_cntrl, MHI_CTRL_INT_STATUS);
	mhi_ep_mmio_write(mhi_cntrl, MHI_CTRL_INT_CLEAR, int_value);

	/* Check for ctrl interrupt */
	if (FIELD_GET(MHI_CTRL_INT_STATUS_MSK, int_value)) {
		dev_dbg(dev, "Processing ctrl interrupt\n");
		mhi_ep_mmio_get_mhi_state(mhi_cntrl, &state, &mhi_reset);
		if (mhi_reset) {
			dev_info(dev, "Host triggered MHI reset!\n");
			disable_irq_nosync(mhi_cntrl->irq);
			schedule_work(&mhi_cntrl->reset_work);
			return IRQ_HANDLED;
		}

		mhi_ep_process_ctrl_interrupt(mhi_cntrl, state);
	}

	/* Check for command doorbell interrupt */
	if (FIELD_GET(MHI_CTRL_INT_STATUS_CRDB_MSK, int_value)) {
		dev_dbg(dev, "Processing command doorbell interrupt\n");
		queue_work(mhi_cntrl->wq, &mhi_cntrl->cmd_ring_work);
	}

	/* Check for channel interrupts */
	mhi_ep_check_channel_interrupt(mhi_cntrl);

	return IRQ_HANDLED;
}

static void mhi_ep_abort_transfer(struct mhi_ep_cntrl *mhi_cntrl)
{
	struct mhi_ep_ring *ch_ring, *ev_ring;
	struct mhi_result result = {};
	struct mhi_ep_chan *mhi_chan;
	int i;

	/* Stop all the channels */
	for (i = 0; i < mhi_cntrl->max_chan; i++) {
		mhi_chan = &mhi_cntrl->mhi_chan[i];
		if (!mhi_chan->ring.started)
			continue;

		mutex_lock(&mhi_chan->lock);
		/* Send channel disconnect status to client drivers */
		if (mhi_chan->xfer_cb) {
			result.transaction_status = -ENOTCONN;
			result.bytes_xferd = 0;
			mhi_chan->xfer_cb(mhi_chan->mhi_dev, &result);
		}

		mhi_chan->state = MHI_CH_STATE_DISABLED;
		mutex_unlock(&mhi_chan->lock);
	}

	flush_workqueue(mhi_cntrl->wq);

	/* Destroy devices associated with all channels */
	device_for_each_child(&mhi_cntrl->mhi_dev->dev, NULL, mhi_ep_destroy_device);

	/* Stop and reset the transfer rings */
	for (i = 0; i < mhi_cntrl->max_chan; i++) {
		mhi_chan = &mhi_cntrl->mhi_chan[i];
		if (!mhi_chan->ring.started)
			continue;

		ch_ring = &mhi_cntrl->mhi_chan[i].ring;
		mutex_lock(&mhi_chan->lock);
		mhi_ep_ring_reset(mhi_cntrl, ch_ring);
		mutex_unlock(&mhi_chan->lock);
	}

	/* Stop and reset the event rings */
	for (i = 0; i < mhi_cntrl->event_rings; i++) {
		ev_ring = &mhi_cntrl->mhi_event[i].ring;
		if (!ev_ring->started)
			continue;

		mutex_lock(&mhi_cntrl->event_lock);
		mhi_ep_ring_reset(mhi_cntrl, ev_ring);
		mutex_unlock(&mhi_cntrl->event_lock);
	}

	/* Stop and reset the command ring */
	mhi_ep_ring_reset(mhi_cntrl, &mhi_cntrl->mhi_cmd->ring);

	mhi_ep_free_host_cfg(mhi_cntrl);
	mhi_ep_mmio_mask_interrupts(mhi_cntrl);

	mhi_cntrl->enabled = false;
}

static void mhi_ep_reset_worker(struct work_struct *work)
{
	struct mhi_ep_cntrl *mhi_cntrl = container_of(work, struct mhi_ep_cntrl, reset_work);
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	enum mhi_state cur_state;
	int ret;

	mhi_ep_abort_transfer(mhi_cntrl);

	spin_lock_bh(&mhi_cntrl->state_lock);
	/* Reset MMIO to signal host that the MHI_RESET is completed in endpoint */
	mhi_ep_mmio_reset(mhi_cntrl);
	cur_state = mhi_cntrl->mhi_state;
	spin_unlock_bh(&mhi_cntrl->state_lock);

	/*
	 * Only proceed further if the reset is due to SYS_ERR. The host will
	 * issue reset during shutdown also and we don't need to do re-init in
	 * that case.
	 */
	if (cur_state == MHI_STATE_SYS_ERR) {
		mhi_ep_mmio_init(mhi_cntrl);

		/* Set AMSS EE before signaling ready state */
		mhi_ep_mmio_set_env(mhi_cntrl, MHI_EE_AMSS);

		/* All set, notify the host that we are ready */
		ret = mhi_ep_set_ready_state(mhi_cntrl);
		if (ret)
			return;

		dev_dbg(dev, "READY state notification sent to the host\n");

		ret = mhi_ep_enable(mhi_cntrl);
		if (ret) {
			dev_err(dev, "Failed to enable MHI endpoint: %d\n", ret);
			return;
		}

		enable_irq(mhi_cntrl->irq);
	}
}

/*
 * We don't need to do anything special other than setting the MHI SYS_ERR
 * state. The host will reset all contexts and issue MHI RESET so that we
 * could also recover from error state.
 */
void mhi_ep_handle_syserr(struct mhi_ep_cntrl *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	int ret;

	ret = mhi_ep_set_mhi_state(mhi_cntrl, MHI_STATE_SYS_ERR);
	if (ret)
		return;

	/* Signal host that the device went to SYS_ERR state */
	ret = mhi_ep_send_state_change_event(mhi_cntrl, MHI_STATE_SYS_ERR);
	if (ret)
		dev_err(dev, "Failed sending SYS_ERR state change event: %d\n", ret);
}

int mhi_ep_power_up(struct mhi_ep_cntrl *mhi_cntrl)
{
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	int ret, i;

	/*
	 * Mask all interrupts until the state machine is ready. Interrupts will
	 * be enabled later with mhi_ep_enable().
	 */
	mhi_ep_mmio_mask_interrupts(mhi_cntrl);
	mhi_ep_mmio_init(mhi_cntrl);

	mhi_cntrl->mhi_event = kzalloc(mhi_cntrl->event_rings * (sizeof(*mhi_cntrl->mhi_event)),
					GFP_KERNEL);
	if (!mhi_cntrl->mhi_event)
		return -ENOMEM;

	/* Initialize command, channel and event rings */
	mhi_ep_ring_init(&mhi_cntrl->mhi_cmd->ring, RING_TYPE_CMD, 0);
	for (i = 0; i < mhi_cntrl->max_chan; i++)
		mhi_ep_ring_init(&mhi_cntrl->mhi_chan[i].ring, RING_TYPE_CH, i);
	for (i = 0; i < mhi_cntrl->event_rings; i++)
		mhi_ep_ring_init(&mhi_cntrl->mhi_event[i].ring, RING_TYPE_ER, i);

	mhi_cntrl->mhi_state = MHI_STATE_RESET;

	/* Set AMSS EE before signaling ready state */
	mhi_ep_mmio_set_env(mhi_cntrl, MHI_EE_AMSS);

	/* All set, notify the host that we are ready */
	ret = mhi_ep_set_ready_state(mhi_cntrl);
	if (ret)
		goto err_free_event;

	dev_dbg(dev, "READY state notification sent to the host\n");

	ret = mhi_ep_enable(mhi_cntrl);
	if (ret) {
		dev_err(dev, "Failed to enable MHI endpoint\n");
		goto err_free_event;
	}

	enable_irq(mhi_cntrl->irq);
	mhi_cntrl->enabled = true;

	return 0;

err_free_event:
	kfree(mhi_cntrl->mhi_event);

	return ret;
}
EXPORT_SYMBOL_GPL(mhi_ep_power_up);

void mhi_ep_power_down(struct mhi_ep_cntrl *mhi_cntrl)
{
	if (mhi_cntrl->enabled)
		mhi_ep_abort_transfer(mhi_cntrl);

	kfree(mhi_cntrl->mhi_event);
	disable_irq(mhi_cntrl->irq);
}
EXPORT_SYMBOL_GPL(mhi_ep_power_down);

void mhi_ep_suspend_channels(struct mhi_ep_cntrl *mhi_cntrl)
{
	struct mhi_ep_chan *mhi_chan;
	u32 tmp;
	int i;

	for (i = 0; i < mhi_cntrl->max_chan; i++) {
		mhi_chan = &mhi_cntrl->mhi_chan[i];

		if (!mhi_chan->mhi_dev)
			continue;

		mutex_lock(&mhi_chan->lock);
		/* Skip if the channel is not currently running */
		tmp = le32_to_cpu(mhi_cntrl->ch_ctx_cache[i].chcfg);
		if (FIELD_GET(CHAN_CTX_CHSTATE_MASK, tmp) != MHI_CH_STATE_RUNNING) {
			mutex_unlock(&mhi_chan->lock);
			continue;
		}

		dev_dbg(&mhi_chan->mhi_dev->dev, "Suspending channel\n");
		/* Set channel state to SUSPENDED */
		mhi_chan->state = MHI_CH_STATE_SUSPENDED;
		tmp &= ~CHAN_CTX_CHSTATE_MASK;
		tmp |= FIELD_PREP(CHAN_CTX_CHSTATE_MASK, MHI_CH_STATE_SUSPENDED);
		mhi_cntrl->ch_ctx_cache[i].chcfg = cpu_to_le32(tmp);
		mutex_unlock(&mhi_chan->lock);
	}
}

void mhi_ep_resume_channels(struct mhi_ep_cntrl *mhi_cntrl)
{
	struct mhi_ep_chan *mhi_chan;
	u32 tmp;
	int i;

	for (i = 0; i < mhi_cntrl->max_chan; i++) {
		mhi_chan = &mhi_cntrl->mhi_chan[i];

		if (!mhi_chan->mhi_dev)
			continue;

		mutex_lock(&mhi_chan->lock);
		/* Skip if the channel is not currently suspended */
		tmp = le32_to_cpu(mhi_cntrl->ch_ctx_cache[i].chcfg);
		if (FIELD_GET(CHAN_CTX_CHSTATE_MASK, tmp) != MHI_CH_STATE_SUSPENDED) {
			mutex_unlock(&mhi_chan->lock);
			continue;
		}

		dev_dbg(&mhi_chan->mhi_dev->dev, "Resuming channel\n");
		/* Set channel state to RUNNING */
		mhi_chan->state = MHI_CH_STATE_RUNNING;
		tmp &= ~CHAN_CTX_CHSTATE_MASK;
		tmp |= FIELD_PREP(CHAN_CTX_CHSTATE_MASK, MHI_CH_STATE_RUNNING);
		mhi_cntrl->ch_ctx_cache[i].chcfg = cpu_to_le32(tmp);
		mutex_unlock(&mhi_chan->lock);
	}
}

static void mhi_ep_release_device(struct device *dev)
{
	struct mhi_ep_device *mhi_dev = to_mhi_ep_device(dev);

	if (mhi_dev->dev_type == MHI_DEVICE_CONTROLLER)
		mhi_dev->mhi_cntrl->mhi_dev = NULL;

	/*
	 * We need to set the mhi_chan->mhi_dev to NULL here since the MHI
	 * devices for the channels will only get created in mhi_ep_create_device()
	 * if the mhi_dev associated with it is NULL.
	 */
	if (mhi_dev->ul_chan)
		mhi_dev->ul_chan->mhi_dev = NULL;

	if (mhi_dev->dl_chan)
		mhi_dev->dl_chan->mhi_dev = NULL;

	kfree(mhi_dev);
}

static struct mhi_ep_device *mhi_ep_alloc_device(struct mhi_ep_cntrl *mhi_cntrl,
						 enum mhi_device_type dev_type)
{
	struct mhi_ep_device *mhi_dev;
	struct device *dev;

	mhi_dev = kzalloc(sizeof(*mhi_dev), GFP_KERNEL);
	if (!mhi_dev)
		return ERR_PTR(-ENOMEM);

	dev = &mhi_dev->dev;
	device_initialize(dev);
	dev->bus = &mhi_ep_bus_type;
	dev->release = mhi_ep_release_device;

	/* Controller device is always allocated first */
	if (dev_type == MHI_DEVICE_CONTROLLER)
		/* for MHI controller device, parent is the bus device (e.g. PCI EPF) */
		dev->parent = mhi_cntrl->cntrl_dev;
	else
		/* for MHI client devices, parent is the MHI controller device */
		dev->parent = &mhi_cntrl->mhi_dev->dev;

	mhi_dev->mhi_cntrl = mhi_cntrl;
	mhi_dev->dev_type = dev_type;

	return mhi_dev;
}

/*
 * MHI channels are always defined in pairs with UL as the even numbered
 * channel and DL as odd numbered one. This function gets UL channel (primary)
 * as the ch_id and always looks after the next entry in channel list for
 * the corresponding DL channel (secondary).
 */
static int mhi_ep_create_device(struct mhi_ep_cntrl *mhi_cntrl, u32 ch_id)
{
	struct mhi_ep_chan *mhi_chan = &mhi_cntrl->mhi_chan[ch_id];
	struct device *dev = mhi_cntrl->cntrl_dev;
	struct mhi_ep_device *mhi_dev;
	int ret;

	/* Check if the channel name is same for both UL and DL */
	if (strcmp(mhi_chan->name, mhi_chan[1].name)) {
		dev_err(dev, "UL and DL channel names are not same: (%s) != (%s)\n",
			mhi_chan->name, mhi_chan[1].name);
		return -EINVAL;
	}

	mhi_dev = mhi_ep_alloc_device(mhi_cntrl, MHI_DEVICE_XFER);
	if (IS_ERR(mhi_dev))
		return PTR_ERR(mhi_dev);

	/* Configure primary channel */
	mhi_dev->ul_chan = mhi_chan;
	get_device(&mhi_dev->dev);
	mhi_chan->mhi_dev = mhi_dev;

	/* Configure secondary channel as well */
	mhi_chan++;
	mhi_dev->dl_chan = mhi_chan;
	get_device(&mhi_dev->dev);
	mhi_chan->mhi_dev = mhi_dev;

	/* Channel name is same for both UL and DL */
	mhi_dev->name = mhi_chan->name;
	ret = dev_set_name(&mhi_dev->dev, "%s_%s",
		     dev_name(&mhi_cntrl->mhi_dev->dev),
		     mhi_dev->name);
	if (ret) {
		put_device(&mhi_dev->dev);
		return ret;
	}

	ret = device_add(&mhi_dev->dev);
	if (ret)
		put_device(&mhi_dev->dev);

	return ret;
}

static int mhi_ep_destroy_device(struct device *dev, void *data)
{
	struct mhi_ep_device *mhi_dev;
	struct mhi_ep_cntrl *mhi_cntrl;
	struct mhi_ep_chan *ul_chan, *dl_chan;

	if (dev->bus != &mhi_ep_bus_type)
		return 0;

	mhi_dev = to_mhi_ep_device(dev);
	mhi_cntrl = mhi_dev->mhi_cntrl;

	/* Only destroy devices created for channels */
	if (mhi_dev->dev_type == MHI_DEVICE_CONTROLLER)
		return 0;

	ul_chan = mhi_dev->ul_chan;
	dl_chan = mhi_dev->dl_chan;

	if (ul_chan)
		put_device(&ul_chan->mhi_dev->dev);

	if (dl_chan)
		put_device(&dl_chan->mhi_dev->dev);

	dev_dbg(&mhi_cntrl->mhi_dev->dev, "Destroying device for chan:%s\n",
		 mhi_dev->name);

	/* Notify the client and remove the device from MHI bus */
	device_del(dev);
	put_device(dev);

	return 0;
}

static int mhi_ep_chan_init(struct mhi_ep_cntrl *mhi_cntrl,
			    const struct mhi_ep_cntrl_config *config)
{
	const struct mhi_ep_channel_config *ch_cfg;
	struct device *dev = mhi_cntrl->cntrl_dev;
	u32 chan, i;
	int ret = -EINVAL;

	mhi_cntrl->max_chan = config->max_channels;

	/*
	 * Allocate max_channels supported by the MHI endpoint and populate
	 * only the defined channels
	 */
	mhi_cntrl->mhi_chan = kcalloc(mhi_cntrl->max_chan, sizeof(*mhi_cntrl->mhi_chan),
				      GFP_KERNEL);
	if (!mhi_cntrl->mhi_chan)
		return -ENOMEM;

	for (i = 0; i < config->num_channels; i++) {
		struct mhi_ep_chan *mhi_chan;

		ch_cfg = &config->ch_cfg[i];

		chan = ch_cfg->num;
		if (chan >= mhi_cntrl->max_chan) {
			dev_err(dev, "Channel (%u) exceeds maximum available channels (%u)\n",
				chan, mhi_cntrl->max_chan);
			goto error_chan_cfg;
		}

		/* Bi-directional and direction less channels are not supported */
		if (ch_cfg->dir == DMA_BIDIRECTIONAL || ch_cfg->dir == DMA_NONE) {
			dev_err(dev, "Invalid direction (%u) for channel (%u)\n",
				ch_cfg->dir, chan);
			goto error_chan_cfg;
		}

		mhi_chan = &mhi_cntrl->mhi_chan[chan];
		mhi_chan->name = ch_cfg->name;
		mhi_chan->chan = chan;
		mhi_chan->dir = ch_cfg->dir;
		mutex_init(&mhi_chan->lock);
	}

	return 0;

error_chan_cfg:
	kfree(mhi_cntrl->mhi_chan);

	return ret;
}

/*
 * Allocate channel and command rings here. Event rings will be allocated
 * in mhi_ep_power_up() as the config comes from the host.
 */
int mhi_ep_register_controller(struct mhi_ep_cntrl *mhi_cntrl,
				const struct mhi_ep_cntrl_config *config)
{
	struct mhi_ep_device *mhi_dev;
	int ret;

	if (!mhi_cntrl || !mhi_cntrl->cntrl_dev || !mhi_cntrl->mmio || !mhi_cntrl->irq)
		return -EINVAL;

	ret = mhi_ep_chan_init(mhi_cntrl, config);
	if (ret)
		return ret;

	mhi_cntrl->mhi_cmd = kcalloc(NR_OF_CMD_RINGS, sizeof(*mhi_cntrl->mhi_cmd), GFP_KERNEL);
	if (!mhi_cntrl->mhi_cmd) {
		ret = -ENOMEM;
		goto err_free_ch;
	}

	INIT_WORK(&mhi_cntrl->state_work, mhi_ep_state_worker);
	INIT_WORK(&mhi_cntrl->reset_work, mhi_ep_reset_worker);
	INIT_WORK(&mhi_cntrl->cmd_ring_work, mhi_ep_cmd_ring_worker);
	INIT_WORK(&mhi_cntrl->ch_ring_work, mhi_ep_ch_ring_worker);

	mhi_cntrl->wq = alloc_workqueue("mhi_ep_wq", 0, 0);
	if (!mhi_cntrl->wq) {
		ret = -ENOMEM;
		goto err_free_cmd;
	}

	INIT_LIST_HEAD(&mhi_cntrl->st_transition_list);
	INIT_LIST_HEAD(&mhi_cntrl->ch_db_list);
	spin_lock_init(&mhi_cntrl->state_lock);
	spin_lock_init(&mhi_cntrl->list_lock);
	mutex_init(&mhi_cntrl->event_lock);

	/* Set MHI version and AMSS EE before enumeration */
	mhi_ep_mmio_write(mhi_cntrl, EP_MHIVER, config->mhi_version);
	mhi_ep_mmio_set_env(mhi_cntrl, MHI_EE_AMSS);

	/* Set controller index */
	ret = ida_alloc(&mhi_ep_cntrl_ida, GFP_KERNEL);
	if (ret < 0)
		goto err_destroy_wq;

	mhi_cntrl->index = ret;

	irq_set_status_flags(mhi_cntrl->irq, IRQ_NOAUTOEN);
	ret = request_irq(mhi_cntrl->irq, mhi_ep_irq, IRQF_TRIGGER_HIGH,
			  "doorbell_irq", mhi_cntrl);
	if (ret) {
		dev_err(mhi_cntrl->cntrl_dev, "Failed to request Doorbell IRQ\n");
		goto err_ida_free;
	}

	/* Allocate the controller device */
	mhi_dev = mhi_ep_alloc_device(mhi_cntrl, MHI_DEVICE_CONTROLLER);
	if (IS_ERR(mhi_dev)) {
		dev_err(mhi_cntrl->cntrl_dev, "Failed to allocate controller device\n");
		ret = PTR_ERR(mhi_dev);
		goto err_free_irq;
	}

	ret = dev_set_name(&mhi_dev->dev, "mhi_ep%u", mhi_cntrl->index);
	if (ret)
		goto err_put_dev;

	mhi_dev->name = dev_name(&mhi_dev->dev);
	mhi_cntrl->mhi_dev = mhi_dev;

	ret = device_add(&mhi_dev->dev);
	if (ret)
		goto err_put_dev;

	dev_dbg(&mhi_dev->dev, "MHI EP Controller registered\n");

	return 0;

err_put_dev:
	put_device(&mhi_dev->dev);
err_free_irq:
	free_irq(mhi_cntrl->irq, mhi_cntrl);
err_ida_free:
	ida_free(&mhi_ep_cntrl_ida, mhi_cntrl->index);
err_destroy_wq:
	destroy_workqueue(mhi_cntrl->wq);
err_free_cmd:
	kfree(mhi_cntrl->mhi_cmd);
err_free_ch:
	kfree(mhi_cntrl->mhi_chan);

	return ret;
}
EXPORT_SYMBOL_GPL(mhi_ep_register_controller);

/*
 * It is expected that the controller drivers will power down the MHI EP stack
 * using "mhi_ep_power_down()" before calling this function to unregister themselves.
 */
void mhi_ep_unregister_controller(struct mhi_ep_cntrl *mhi_cntrl)
{
	struct mhi_ep_device *mhi_dev = mhi_cntrl->mhi_dev;

	destroy_workqueue(mhi_cntrl->wq);

	free_irq(mhi_cntrl->irq, mhi_cntrl);

	kfree(mhi_cntrl->mhi_cmd);
	kfree(mhi_cntrl->mhi_chan);

	device_del(&mhi_dev->dev);
	put_device(&mhi_dev->dev);

	ida_free(&mhi_ep_cntrl_ida, mhi_cntrl->index);
}
EXPORT_SYMBOL_GPL(mhi_ep_unregister_controller);

static int mhi_ep_driver_probe(struct device *dev)
{
	struct mhi_ep_device *mhi_dev = to_mhi_ep_device(dev);
	struct mhi_ep_driver *mhi_drv = to_mhi_ep_driver(dev->driver);
	struct mhi_ep_chan *ul_chan = mhi_dev->ul_chan;
	struct mhi_ep_chan *dl_chan = mhi_dev->dl_chan;

	ul_chan->xfer_cb = mhi_drv->ul_xfer_cb;
	dl_chan->xfer_cb = mhi_drv->dl_xfer_cb;

	return mhi_drv->probe(mhi_dev, mhi_dev->id);
}

static int mhi_ep_driver_remove(struct device *dev)
{
	struct mhi_ep_device *mhi_dev = to_mhi_ep_device(dev);
	struct mhi_ep_driver *mhi_drv = to_mhi_ep_driver(dev->driver);
	struct mhi_result result = {};
	struct mhi_ep_chan *mhi_chan;
	int dir;

	/* Skip if it is a controller device */
	if (mhi_dev->dev_type == MHI_DEVICE_CONTROLLER)
		return 0;

	/* Disconnect the channels associated with the driver */
	for (dir = 0; dir < 2; dir++) {
		mhi_chan = dir ? mhi_dev->ul_chan : mhi_dev->dl_chan;

		if (!mhi_chan)
			continue;

		mutex_lock(&mhi_chan->lock);
		/* Send channel disconnect status to the client driver */
		if (mhi_chan->xfer_cb) {
			result.transaction_status = -ENOTCONN;
			result.bytes_xferd = 0;
			mhi_chan->xfer_cb(mhi_chan->mhi_dev, &result);
		}

		mhi_chan->state = MHI_CH_STATE_DISABLED;
		mhi_chan->xfer_cb = NULL;
		mutex_unlock(&mhi_chan->lock);
	}

	/* Remove the client driver now */
	mhi_drv->remove(mhi_dev);

	return 0;
}

int __mhi_ep_driver_register(struct mhi_ep_driver *mhi_drv, struct module *owner)
{
	struct device_driver *driver = &mhi_drv->driver;

	if (!mhi_drv->probe || !mhi_drv->remove)
		return -EINVAL;

	/* Client drivers should have callbacks defined for both channels */
	if (!mhi_drv->ul_xfer_cb || !mhi_drv->dl_xfer_cb)
		return -EINVAL;

	driver->bus = &mhi_ep_bus_type;
	driver->owner = owner;
	driver->probe = mhi_ep_driver_probe;
	driver->remove = mhi_ep_driver_remove;

	return driver_register(driver);
}
EXPORT_SYMBOL_GPL(__mhi_ep_driver_register);

void mhi_ep_driver_unregister(struct mhi_ep_driver *mhi_drv)
{
	driver_unregister(&mhi_drv->driver);
}
EXPORT_SYMBOL_GPL(mhi_ep_driver_unregister);

static int mhi_ep_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct mhi_ep_device *mhi_dev = to_mhi_ep_device(dev);

	return add_uevent_var(env, "MODALIAS=" MHI_EP_DEVICE_MODALIAS_FMT,
					mhi_dev->name);
}

static int mhi_ep_match(struct device *dev, struct device_driver *drv)
{
	struct mhi_ep_device *mhi_dev = to_mhi_ep_device(dev);
	struct mhi_ep_driver *mhi_drv = to_mhi_ep_driver(drv);
	const struct mhi_device_id *id;

	/*
	 * If the device is a controller type then there is no client driver
	 * associated with it
	 */
	if (mhi_dev->dev_type == MHI_DEVICE_CONTROLLER)
		return 0;

	for (id = mhi_drv->id_table; id->chan[0]; id++)
		if (!strcmp(mhi_dev->name, id->chan)) {
			mhi_dev->id = id;
			return 1;
		}

	return 0;
};

struct bus_type mhi_ep_bus_type = {
	.name = "mhi_ep",
	.dev_name = "mhi_ep",
	.match = mhi_ep_match,
	.uevent = mhi_ep_uevent,
};

static int __init mhi_ep_init(void)
{
	return bus_register(&mhi_ep_bus_type);
}

static void __exit mhi_ep_exit(void)
{
	bus_unregister(&mhi_ep_bus_type);
}

postcore_initcall(mhi_ep_init);
module_exit(mhi_ep_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MHI Bus Endpoint stack");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
