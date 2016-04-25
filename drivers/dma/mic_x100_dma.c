/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC X100 DMA Driver.
 *
 * Adapted from IOAT dma driver.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>

#include "mic_x100_dma.h"

#define MIC_DMA_MAX_XFER_SIZE_CARD  (1 * 1024 * 1024 -\
				       MIC_DMA_ALIGN_BYTES)
#define MIC_DMA_MAX_XFER_SIZE_HOST  (1 * 1024 * 1024 >> 1)
#define MIC_DMA_DESC_TYPE_SHIFT	60
#define MIC_DMA_MEMCPY_LEN_SHIFT 46
#define MIC_DMA_STAT_INTR_SHIFT 59

/* high-water mark for pushing dma descriptors */
static int mic_dma_pending_level = 4;

/* Status descriptor is used to write a 64 bit value to a memory location */
enum mic_dma_desc_format_type {
	MIC_DMA_MEMCPY = 1,
	MIC_DMA_STATUS,
};

static inline u32 mic_dma_hw_ring_inc(u32 val)
{
	return (val + 1) % MIC_DMA_DESC_RX_SIZE;
}

static inline u32 mic_dma_hw_ring_dec(u32 val)
{
	return val ? val - 1 : MIC_DMA_DESC_RX_SIZE - 1;
}

static inline void mic_dma_hw_ring_inc_head(struct mic_dma_chan *ch)
{
	ch->head = mic_dma_hw_ring_inc(ch->head);
}

/* Prepare a memcpy desc */
static inline void mic_dma_memcpy_desc(struct mic_dma_desc *desc,
	dma_addr_t src_phys, dma_addr_t dst_phys, u64 size)
{
	u64 qw0, qw1;

	qw0 = src_phys;
	qw0 |= (size >> MIC_DMA_ALIGN_SHIFT) << MIC_DMA_MEMCPY_LEN_SHIFT;
	qw1 = MIC_DMA_MEMCPY;
	qw1 <<= MIC_DMA_DESC_TYPE_SHIFT;
	qw1 |= dst_phys;
	desc->qw0 = qw0;
	desc->qw1 = qw1;
}

/* Prepare a status desc. with @data to be written at @dst_phys */
static inline void mic_dma_prep_status_desc(struct mic_dma_desc *desc, u64 data,
	dma_addr_t dst_phys, bool generate_intr)
{
	u64 qw0, qw1;

	qw0 = data;
	qw1 = (u64) MIC_DMA_STATUS << MIC_DMA_DESC_TYPE_SHIFT | dst_phys;
	if (generate_intr)
		qw1 |= (1ULL << MIC_DMA_STAT_INTR_SHIFT);
	desc->qw0 = qw0;
	desc->qw1 = qw1;
}

static void mic_dma_cleanup(struct mic_dma_chan *ch)
{
	struct dma_async_tx_descriptor *tx;
	u32 tail;
	u32 last_tail;

	spin_lock(&ch->cleanup_lock);
	tail = mic_dma_read_cmp_cnt(ch);
	/*
	 * This is the barrier pair for smp_wmb() in fn.
	 * mic_dma_tx_submit_unlock. It's required so that we read the
	 * updated cookie value from tx->cookie.
	 */
	smp_rmb();
	for (last_tail = ch->last_tail; tail != last_tail;) {
		tx = &ch->tx_array[last_tail];
		if (tx->cookie) {
			dma_cookie_complete(tx);
			if (tx->callback) {
				tx->callback(tx->callback_param);
				tx->callback = NULL;
			}
		}
		last_tail = mic_dma_hw_ring_inc(last_tail);
	}
	/* finish all completion callbacks before incrementing tail */
	smp_mb();
	ch->last_tail = last_tail;
	spin_unlock(&ch->cleanup_lock);
}

static u32 mic_dma_ring_count(u32 head, u32 tail)
{
	u32 count;

	if (head >= tail)
		count = (tail - 0) + (MIC_DMA_DESC_RX_SIZE - head);
	else
		count = tail - head;
	return count - 1;
}

/* Returns the num. of free descriptors on success, -ENOMEM on failure */
static int mic_dma_avail_desc_ring_space(struct mic_dma_chan *ch, int required)
{
	struct device *dev = mic_dma_ch_to_device(ch);
	u32 count;

	count = mic_dma_ring_count(ch->head, ch->last_tail);
	if (count < required) {
		mic_dma_cleanup(ch);
		count = mic_dma_ring_count(ch->head, ch->last_tail);
	}

	if (count < required) {
		dev_dbg(dev, "Not enough desc space");
		dev_dbg(dev, "%s %d required=%u, avail=%u\n",
			__func__, __LINE__, required, count);
		return -ENOMEM;
	} else {
		return count;
	}
}

/* Program memcpy descriptors into the descriptor ring and update s/w head ptr*/
static int mic_dma_prog_memcpy_desc(struct mic_dma_chan *ch, dma_addr_t src,
				    dma_addr_t dst, size_t len)
{
	size_t current_transfer_len;
	size_t max_xfer_size = to_mic_dma_dev(ch)->max_xfer_size;
	/* 3 is added to make sure we have enough space for status desc */
	int num_desc = len / max_xfer_size + 3;
	int ret;

	if (len % max_xfer_size)
		num_desc++;

	ret = mic_dma_avail_desc_ring_space(ch, num_desc);
	if (ret < 0)
		return ret;
	do {
		current_transfer_len = min(len, max_xfer_size);
		mic_dma_memcpy_desc(&ch->desc_ring[ch->head],
				    src, dst, current_transfer_len);
		mic_dma_hw_ring_inc_head(ch);
		len -= current_transfer_len;
		dst = dst + current_transfer_len;
		src = src + current_transfer_len;
	} while (len > 0);
	return 0;
}

/* It's a h/w quirk and h/w needs 2 status descriptors for every status desc */
static void mic_dma_prog_intr(struct mic_dma_chan *ch)
{
	mic_dma_prep_status_desc(&ch->desc_ring[ch->head], 0,
				 ch->status_dest_micpa, false);
	mic_dma_hw_ring_inc_head(ch);
	mic_dma_prep_status_desc(&ch->desc_ring[ch->head], 0,
				 ch->status_dest_micpa, true);
	mic_dma_hw_ring_inc_head(ch);
}

/* Wrapper function to program memcpy descriptors/status descriptors */
static int mic_dma_do_dma(struct mic_dma_chan *ch, int flags, dma_addr_t src,
			  dma_addr_t dst, size_t len)
{
	if (len && -ENOMEM == mic_dma_prog_memcpy_desc(ch, src, dst, len)) {
		return -ENOMEM;
	} else {
		/* 3 is the maximum number of status descriptors */
		int ret = mic_dma_avail_desc_ring_space(ch, 3);

		if (ret < 0)
			return ret;
	}

	/* Above mic_dma_prog_memcpy_desc() makes sure we have enough space */
	if (flags & DMA_PREP_FENCE) {
		mic_dma_prep_status_desc(&ch->desc_ring[ch->head], 0,
					 ch->status_dest_micpa, false);
		mic_dma_hw_ring_inc_head(ch);
	}

	if (flags & DMA_PREP_INTERRUPT)
		mic_dma_prog_intr(ch);

	return 0;
}

static inline void mic_dma_issue_pending(struct dma_chan *ch)
{
	struct mic_dma_chan *mic_ch = to_mic_dma_chan(ch);

	spin_lock(&mic_ch->issue_lock);
	/*
	 * Write to head triggers h/w to act on the descriptors.
	 * On MIC, writing the same head value twice causes
	 * a h/w error. On second write, h/w assumes we filled
	 * the entire ring & overwrote some of the descriptors.
	 */
	if (mic_ch->issued == mic_ch->submitted)
		goto out;
	mic_ch->issued = mic_ch->submitted;
	/*
	 * make descriptor updates visible before advancing head,
	 * this is purposefully not smp_wmb() since we are also
	 * publishing the descriptor updates to a dma device
	 */
	wmb();
	mic_dma_write_reg(mic_ch, MIC_DMA_REG_DHPR, mic_ch->issued);
out:
	spin_unlock(&mic_ch->issue_lock);
}

static inline void mic_dma_update_pending(struct mic_dma_chan *ch)
{
	if (mic_dma_ring_count(ch->issued, ch->submitted)
			> mic_dma_pending_level)
		mic_dma_issue_pending(&ch->api_ch);
}

static dma_cookie_t mic_dma_tx_submit_unlock(struct dma_async_tx_descriptor *tx)
{
	struct mic_dma_chan *mic_ch = to_mic_dma_chan(tx->chan);
	dma_cookie_t cookie;

	dma_cookie_assign(tx);
	cookie = tx->cookie;
	/*
	 * We need an smp write barrier here because another CPU might see
	 * an update to submitted and update h/w head even before we
	 * assigned a cookie to this tx.
	 */
	smp_wmb();
	mic_ch->submitted = mic_ch->head;
	spin_unlock(&mic_ch->prep_lock);
	mic_dma_update_pending(mic_ch);
	return cookie;
}

static inline struct dma_async_tx_descriptor *
allocate_tx(struct mic_dma_chan *ch)
{
	u32 idx = mic_dma_hw_ring_dec(ch->head);
	struct dma_async_tx_descriptor *tx = &ch->tx_array[idx];

	dma_async_tx_descriptor_init(tx, &ch->api_ch);
	tx->tx_submit = mic_dma_tx_submit_unlock;
	return tx;
}

/* Program a status descriptor with dst as address and value to be written */
static struct dma_async_tx_descriptor *
mic_dma_prep_status_lock(struct dma_chan *ch, dma_addr_t dst, u64 src_val,
			 unsigned long flags)
{
	struct mic_dma_chan *mic_ch = to_mic_dma_chan(ch);
	int result;

	spin_lock(&mic_ch->prep_lock);
	result = mic_dma_avail_desc_ring_space(mic_ch, 4);
	if (result < 0)
		goto error;
	mic_dma_prep_status_desc(&mic_ch->desc_ring[mic_ch->head], src_val, dst,
				 false);
	mic_dma_hw_ring_inc_head(mic_ch);
	result = mic_dma_do_dma(mic_ch, flags, 0, 0, 0);
	if (result < 0)
		goto error;

	return allocate_tx(mic_ch);
error:
	dev_err(mic_dma_ch_to_device(mic_ch),
		"Error enqueueing dma status descriptor, error=%d\n", result);
	spin_unlock(&mic_ch->prep_lock);
	return NULL;
}

/*
 * Prepare a memcpy descriptor to be added to the ring.
 * Note that the temporary descriptor adds an extra overhead of copying the
 * descriptor to ring. So, we copy directly to the descriptor ring
 */
static struct dma_async_tx_descriptor *
mic_dma_prep_memcpy_lock(struct dma_chan *ch, dma_addr_t dma_dest,
			 dma_addr_t dma_src, size_t len, unsigned long flags)
{
	struct mic_dma_chan *mic_ch = to_mic_dma_chan(ch);
	struct device *dev = mic_dma_ch_to_device(mic_ch);
	int result;

	if (!len && !flags)
		return NULL;

	spin_lock(&mic_ch->prep_lock);
	result = mic_dma_do_dma(mic_ch, flags, dma_src, dma_dest, len);
	if (result >= 0)
		return allocate_tx(mic_ch);
	dev_err(dev, "Error enqueueing dma, error=%d\n", result);
	spin_unlock(&mic_ch->prep_lock);
	return NULL;
}

static struct dma_async_tx_descriptor *
mic_dma_prep_interrupt_lock(struct dma_chan *ch, unsigned long flags)
{
	struct mic_dma_chan *mic_ch = to_mic_dma_chan(ch);
	int ret;

	spin_lock(&mic_ch->prep_lock);
	ret = mic_dma_do_dma(mic_ch, flags, 0, 0, 0);
	if (!ret)
		return allocate_tx(mic_ch);
	spin_unlock(&mic_ch->prep_lock);
	return NULL;
}

/* Return the status of the transaction */
static enum dma_status
mic_dma_tx_status(struct dma_chan *ch, dma_cookie_t cookie,
		  struct dma_tx_state *txstate)
{
	struct mic_dma_chan *mic_ch = to_mic_dma_chan(ch);

	if (DMA_COMPLETE != dma_cookie_status(ch, cookie, txstate))
		mic_dma_cleanup(mic_ch);

	return dma_cookie_status(ch, cookie, txstate);
}

static irqreturn_t mic_dma_thread_fn(int irq, void *data)
{
	mic_dma_cleanup((struct mic_dma_chan *)data);
	return IRQ_HANDLED;
}

static irqreturn_t mic_dma_intr_handler(int irq, void *data)
{
	struct mic_dma_chan *ch = ((struct mic_dma_chan *)data);

	mic_dma_ack_interrupt(ch);
	return IRQ_WAKE_THREAD;
}

static int mic_dma_alloc_desc_ring(struct mic_dma_chan *ch)
{
	u64 desc_ring_size = MIC_DMA_DESC_RX_SIZE * sizeof(*ch->desc_ring);
	struct device *dev = &to_mbus_device(ch)->dev;

	desc_ring_size = ALIGN(desc_ring_size, MIC_DMA_ALIGN_BYTES);
	ch->desc_ring = kzalloc(desc_ring_size, GFP_KERNEL);

	if (!ch->desc_ring)
		return -ENOMEM;

	ch->desc_ring_micpa = dma_map_single(dev, ch->desc_ring,
					     desc_ring_size, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, ch->desc_ring_micpa))
		goto map_error;

	ch->tx_array = vzalloc(MIC_DMA_DESC_RX_SIZE * sizeof(*ch->tx_array));
	if (!ch->tx_array)
		goto tx_error;
	return 0;
tx_error:
	dma_unmap_single(dev, ch->desc_ring_micpa, desc_ring_size,
			 DMA_BIDIRECTIONAL);
map_error:
	kfree(ch->desc_ring);
	return -ENOMEM;
}

static void mic_dma_free_desc_ring(struct mic_dma_chan *ch)
{
	u64 desc_ring_size = MIC_DMA_DESC_RX_SIZE * sizeof(*ch->desc_ring);

	vfree(ch->tx_array);
	desc_ring_size = ALIGN(desc_ring_size, MIC_DMA_ALIGN_BYTES);
	dma_unmap_single(&to_mbus_device(ch)->dev, ch->desc_ring_micpa,
			 desc_ring_size, DMA_BIDIRECTIONAL);
	kfree(ch->desc_ring);
	ch->desc_ring = NULL;
}

static void mic_dma_free_status_dest(struct mic_dma_chan *ch)
{
	dma_unmap_single(&to_mbus_device(ch)->dev, ch->status_dest_micpa,
			 L1_CACHE_BYTES, DMA_BIDIRECTIONAL);
	kfree(ch->status_dest);
}

static int mic_dma_alloc_status_dest(struct mic_dma_chan *ch)
{
	struct device *dev = &to_mbus_device(ch)->dev;

	ch->status_dest = kzalloc(L1_CACHE_BYTES, GFP_KERNEL);
	if (!ch->status_dest)
		return -ENOMEM;
	ch->status_dest_micpa = dma_map_single(dev, ch->status_dest,
					L1_CACHE_BYTES, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, ch->status_dest_micpa)) {
		kfree(ch->status_dest);
		ch->status_dest = NULL;
		return -ENOMEM;
	}
	return 0;
}

static int mic_dma_check_chan(struct mic_dma_chan *ch)
{
	if (mic_dma_read_reg(ch, MIC_DMA_REG_DCHERR) ||
	    mic_dma_read_reg(ch, MIC_DMA_REG_DSTAT) & MIC_DMA_CHAN_QUIESCE) {
		mic_dma_disable_chan(ch);
		mic_dma_chan_mask_intr(ch);
		dev_err(mic_dma_ch_to_device(ch),
			"%s %d error setting up mic dma chan %d\n",
			__func__, __LINE__, ch->ch_num);
		return -EBUSY;
	}
	return 0;
}

static int mic_dma_chan_setup(struct mic_dma_chan *ch)
{
	if (MIC_DMA_CHAN_MIC == ch->owner)
		mic_dma_chan_set_owner(ch);
	mic_dma_disable_chan(ch);
	mic_dma_chan_mask_intr(ch);
	mic_dma_write_reg(ch, MIC_DMA_REG_DCHERRMSK, 0);
	mic_dma_chan_set_desc_ring(ch);
	ch->last_tail = mic_dma_read_reg(ch, MIC_DMA_REG_DTPR);
	ch->head = ch->last_tail;
	ch->issued = 0;
	mic_dma_chan_unmask_intr(ch);
	mic_dma_enable_chan(ch);
	return mic_dma_check_chan(ch);
}

static void mic_dma_chan_destroy(struct mic_dma_chan *ch)
{
	mic_dma_disable_chan(ch);
	mic_dma_chan_mask_intr(ch);
}

static void mic_dma_unregister_dma_device(struct mic_dma_device *mic_dma_dev)
{
	dma_async_device_unregister(&mic_dma_dev->dma_dev);
}

static int mic_dma_setup_irq(struct mic_dma_chan *ch)
{
	ch->cookie =
		to_mbus_hw_ops(ch)->request_threaded_irq(to_mbus_device(ch),
			mic_dma_intr_handler, mic_dma_thread_fn,
			"mic dma_channel", ch, ch->ch_num);
	if (IS_ERR(ch->cookie))
		return PTR_ERR(ch->cookie);
	return 0;
}

static inline void mic_dma_free_irq(struct mic_dma_chan *ch)
{
	to_mbus_hw_ops(ch)->free_irq(to_mbus_device(ch), ch->cookie, ch);
}

static int mic_dma_chan_init(struct mic_dma_chan *ch)
{
	int ret = mic_dma_alloc_desc_ring(ch);

	if (ret)
		goto ring_error;
	ret = mic_dma_alloc_status_dest(ch);
	if (ret)
		goto status_error;
	ret = mic_dma_chan_setup(ch);
	if (ret)
		goto chan_error;
	return ret;
chan_error:
	mic_dma_free_status_dest(ch);
status_error:
	mic_dma_free_desc_ring(ch);
ring_error:
	return ret;
}

static int mic_dma_drain_chan(struct mic_dma_chan *ch)
{
	struct dma_async_tx_descriptor *tx;
	int err = 0;
	dma_cookie_t cookie;

	tx = mic_dma_prep_memcpy_lock(&ch->api_ch, 0, 0, 0, DMA_PREP_FENCE);
	if (!tx) {
		err = -ENOMEM;
		goto error;
	}

	cookie = tx->tx_submit(tx);
	if (dma_submit_error(cookie))
		err = -ENOMEM;
	else
		err = dma_sync_wait(&ch->api_ch, cookie);
	if (err) {
		dev_err(mic_dma_ch_to_device(ch), "%s %d TO chan 0x%x\n",
			__func__, __LINE__, ch->ch_num);
		err = -EIO;
	}
error:
	mic_dma_cleanup(ch);
	return err;
}

static inline void mic_dma_chan_uninit(struct mic_dma_chan *ch)
{
	mic_dma_chan_destroy(ch);
	mic_dma_cleanup(ch);
	mic_dma_free_status_dest(ch);
	mic_dma_free_desc_ring(ch);
}

static int mic_dma_init(struct mic_dma_device *mic_dma_dev,
			enum mic_dma_chan_owner owner)
{
	int i, first_chan = mic_dma_dev->start_ch;
	struct mic_dma_chan *ch;
	int ret;

	for (i = first_chan; i < first_chan + MIC_DMA_NUM_CHAN; i++) {
		unsigned long data;
		ch = &mic_dma_dev->mic_ch[i];
		data = (unsigned long)ch;
		ch->ch_num = i;
		ch->owner = owner;
		spin_lock_init(&ch->cleanup_lock);
		spin_lock_init(&ch->prep_lock);
		spin_lock_init(&ch->issue_lock);
		ret = mic_dma_setup_irq(ch);
		if (ret)
			goto error;
	}
	return 0;
error:
	for (i = i - 1; i >= first_chan; i--)
		mic_dma_free_irq(ch);
	return ret;
}

static void mic_dma_uninit(struct mic_dma_device *mic_dma_dev)
{
	int i, first_chan = mic_dma_dev->start_ch;
	struct mic_dma_chan *ch;

	for (i = first_chan; i < first_chan + MIC_DMA_NUM_CHAN; i++) {
		ch = &mic_dma_dev->mic_ch[i];
		mic_dma_free_irq(ch);
	}
}

static int mic_dma_alloc_chan_resources(struct dma_chan *ch)
{
	int ret = mic_dma_chan_init(to_mic_dma_chan(ch));
	if (ret)
		return ret;
	return MIC_DMA_DESC_RX_SIZE;
}

static void mic_dma_free_chan_resources(struct dma_chan *ch)
{
	struct mic_dma_chan *mic_ch = to_mic_dma_chan(ch);
	mic_dma_drain_chan(mic_ch);
	mic_dma_chan_uninit(mic_ch);
}

/* Set the fn. handlers and register the dma device with dma api */
static int mic_dma_register_dma_device(struct mic_dma_device *mic_dma_dev,
				       enum mic_dma_chan_owner owner)
{
	int i, first_chan = mic_dma_dev->start_ch;

	dma_cap_zero(mic_dma_dev->dma_dev.cap_mask);
	/*
	 * This dma engine is not capable of host memory to host memory
	 * transfers
	 */
	dma_cap_set(DMA_MEMCPY, mic_dma_dev->dma_dev.cap_mask);

	if (MIC_DMA_CHAN_HOST == owner)
		dma_cap_set(DMA_PRIVATE, mic_dma_dev->dma_dev.cap_mask);
	mic_dma_dev->dma_dev.device_alloc_chan_resources =
		mic_dma_alloc_chan_resources;
	mic_dma_dev->dma_dev.device_free_chan_resources =
		mic_dma_free_chan_resources;
	mic_dma_dev->dma_dev.device_tx_status = mic_dma_tx_status;
	mic_dma_dev->dma_dev.device_prep_dma_memcpy = mic_dma_prep_memcpy_lock;
	mic_dma_dev->dma_dev.device_prep_dma_imm_data =
		mic_dma_prep_status_lock;
	mic_dma_dev->dma_dev.device_prep_dma_interrupt =
		mic_dma_prep_interrupt_lock;
	mic_dma_dev->dma_dev.device_issue_pending = mic_dma_issue_pending;
	mic_dma_dev->dma_dev.copy_align = MIC_DMA_ALIGN_SHIFT;
	INIT_LIST_HEAD(&mic_dma_dev->dma_dev.channels);
	for (i = first_chan; i < first_chan + MIC_DMA_NUM_CHAN; i++) {
		mic_dma_dev->mic_ch[i].api_ch.device = &mic_dma_dev->dma_dev;
		dma_cookie_init(&mic_dma_dev->mic_ch[i].api_ch);
		list_add_tail(&mic_dma_dev->mic_ch[i].api_ch.device_node,
			      &mic_dma_dev->dma_dev.channels);
	}
	return dma_async_device_register(&mic_dma_dev->dma_dev);
}

/*
 * Initializes dma channels and registers the dma device with the
 * dma engine api.
 */
static struct mic_dma_device *mic_dma_dev_reg(struct mbus_device *mbdev,
					      enum mic_dma_chan_owner owner)
{
	struct mic_dma_device *mic_dma_dev;
	int ret;
	struct device *dev = &mbdev->dev;

	mic_dma_dev = kzalloc(sizeof(*mic_dma_dev), GFP_KERNEL);
	if (!mic_dma_dev) {
		ret = -ENOMEM;
		goto alloc_error;
	}
	mic_dma_dev->mbdev = mbdev;
	mic_dma_dev->dma_dev.dev = dev;
	mic_dma_dev->mmio = mbdev->mmio_va;
	if (MIC_DMA_CHAN_HOST == owner) {
		mic_dma_dev->start_ch = 0;
		mic_dma_dev->max_xfer_size = MIC_DMA_MAX_XFER_SIZE_HOST;
	} else {
		mic_dma_dev->start_ch = 4;
		mic_dma_dev->max_xfer_size = MIC_DMA_MAX_XFER_SIZE_CARD;
	}
	ret = mic_dma_init(mic_dma_dev, owner);
	if (ret)
		goto init_error;
	ret = mic_dma_register_dma_device(mic_dma_dev, owner);
	if (ret)
		goto reg_error;
	return mic_dma_dev;
reg_error:
	mic_dma_uninit(mic_dma_dev);
init_error:
	kfree(mic_dma_dev);
	mic_dma_dev = NULL;
alloc_error:
	dev_err(dev, "Error at %s %d ret=%d\n", __func__, __LINE__, ret);
	return mic_dma_dev;
}

static void mic_dma_dev_unreg(struct mic_dma_device *mic_dma_dev)
{
	mic_dma_unregister_dma_device(mic_dma_dev);
	mic_dma_uninit(mic_dma_dev);
	kfree(mic_dma_dev);
}

/* DEBUGFS CODE */
static int mic_dma_reg_seq_show(struct seq_file *s, void *pos)
{
	struct mic_dma_device *mic_dma_dev = s->private;
	int i, chan_num, first_chan = mic_dma_dev->start_ch;
	struct mic_dma_chan *ch;

	seq_printf(s, "SBOX_DCR: %#x\n",
		   mic_dma_mmio_read(&mic_dma_dev->mic_ch[first_chan],
				     MIC_DMA_SBOX_BASE + MIC_DMA_SBOX_DCR));
	seq_puts(s, "DMA Channel Registers\n");
	seq_printf(s, "%-10s| %-10s %-10s %-10s %-10s %-10s",
		   "Channel", "DCAR", "DTPR", "DHPR", "DRAR_HI", "DRAR_LO");
	seq_printf(s, " %-11s %-14s %-10s\n", "DCHERR", "DCHERRMSK", "DSTAT");
	for (i = first_chan; i < first_chan + MIC_DMA_NUM_CHAN; i++) {
		ch = &mic_dma_dev->mic_ch[i];
		chan_num = ch->ch_num;
		seq_printf(s, "%-10i| %-#10x %-#10x %-#10x %-#10x",
			   chan_num,
			   mic_dma_read_reg(ch, MIC_DMA_REG_DCAR),
			   mic_dma_read_reg(ch, MIC_DMA_REG_DTPR),
			   mic_dma_read_reg(ch, MIC_DMA_REG_DHPR),
			   mic_dma_read_reg(ch, MIC_DMA_REG_DRAR_HI));
		seq_printf(s, " %-#10x %-#10x %-#14x %-#10x\n",
			   mic_dma_read_reg(ch, MIC_DMA_REG_DRAR_LO),
			   mic_dma_read_reg(ch, MIC_DMA_REG_DCHERR),
			   mic_dma_read_reg(ch, MIC_DMA_REG_DCHERRMSK),
			   mic_dma_read_reg(ch, MIC_DMA_REG_DSTAT));
	}
	return 0;
}

static int mic_dma_reg_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, mic_dma_reg_seq_show, inode->i_private);
}

static int mic_dma_reg_debug_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

static const struct file_operations mic_dma_reg_ops = {
	.owner   = THIS_MODULE,
	.open    = mic_dma_reg_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = mic_dma_reg_debug_release
};

/* Debugfs parent dir */
static struct dentry *mic_dma_dbg;

static int mic_dma_driver_probe(struct mbus_device *mbdev)
{
	struct mic_dma_device *mic_dma_dev;
	enum mic_dma_chan_owner owner;

	if (MBUS_DEV_DMA_MIC == mbdev->id.device)
		owner = MIC_DMA_CHAN_MIC;
	else
		owner = MIC_DMA_CHAN_HOST;

	mic_dma_dev = mic_dma_dev_reg(mbdev, owner);
	dev_set_drvdata(&mbdev->dev, mic_dma_dev);

	if (mic_dma_dbg) {
		mic_dma_dev->dbg_dir = debugfs_create_dir(dev_name(&mbdev->dev),
							  mic_dma_dbg);
		if (mic_dma_dev->dbg_dir)
			debugfs_create_file("mic_dma_reg", 0444,
					    mic_dma_dev->dbg_dir, mic_dma_dev,
					    &mic_dma_reg_ops);
	}
	return 0;
}

static void mic_dma_driver_remove(struct mbus_device *mbdev)
{
	struct mic_dma_device *mic_dma_dev;

	mic_dma_dev = dev_get_drvdata(&mbdev->dev);
	debugfs_remove_recursive(mic_dma_dev->dbg_dir);
	mic_dma_dev_unreg(mic_dma_dev);
}

static struct mbus_device_id id_table[] = {
	{MBUS_DEV_DMA_MIC, MBUS_DEV_ANY_ID},
	{MBUS_DEV_DMA_HOST, MBUS_DEV_ANY_ID},
	{0},
};

static struct mbus_driver mic_dma_driver = {
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table = id_table,
	.probe = mic_dma_driver_probe,
	.remove = mic_dma_driver_remove,
};

static int __init mic_x100_dma_init(void)
{
	int rc = mbus_register_driver(&mic_dma_driver);
	if (rc)
		return rc;
	mic_dma_dbg = debugfs_create_dir(KBUILD_MODNAME, NULL);
	return 0;
}

static void __exit mic_x100_dma_exit(void)
{
	debugfs_remove_recursive(mic_dma_dbg);
	mbus_unregister_driver(&mic_dma_driver);
}

module_init(mic_x100_dma_init);
module_exit(mic_x100_dma_exit);

MODULE_DEVICE_TABLE(mbus, id_table);
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) MIC X100 DMA Driver");
MODULE_LICENSE("GPL v2");
