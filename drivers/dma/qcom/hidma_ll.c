// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Technologies HIDMA DMA engine low level code
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 */

#include <linux/dmaengine.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/atomic.h>
#include <linux/iopoll.h>
#include <linux/kfifo.h>
#include <linux/bitops.h>

#include "hidma.h"

#define HIDMA_EVRE_SIZE			16	/* each EVRE is 16 bytes */

#define HIDMA_TRCA_CTRLSTS_REG			0x000
#define HIDMA_TRCA_RING_LOW_REG		0x008
#define HIDMA_TRCA_RING_HIGH_REG		0x00C
#define HIDMA_TRCA_RING_LEN_REG		0x010
#define HIDMA_TRCA_DOORBELL_REG		0x400

#define HIDMA_EVCA_CTRLSTS_REG			0x000
#define HIDMA_EVCA_INTCTRL_REG			0x004
#define HIDMA_EVCA_RING_LOW_REG		0x008
#define HIDMA_EVCA_RING_HIGH_REG		0x00C
#define HIDMA_EVCA_RING_LEN_REG		0x010
#define HIDMA_EVCA_WRITE_PTR_REG		0x020
#define HIDMA_EVCA_DOORBELL_REG		0x400

#define HIDMA_EVCA_IRQ_STAT_REG		0x100
#define HIDMA_EVCA_IRQ_CLR_REG			0x108
#define HIDMA_EVCA_IRQ_EN_REG			0x110

#define HIDMA_EVRE_CFG_IDX			0

#define HIDMA_EVRE_ERRINFO_BIT_POS		24
#define HIDMA_EVRE_CODE_BIT_POS		28

#define HIDMA_EVRE_ERRINFO_MASK		GENMASK(3, 0)
#define HIDMA_EVRE_CODE_MASK			GENMASK(3, 0)

#define HIDMA_CH_CONTROL_MASK			GENMASK(7, 0)
#define HIDMA_CH_STATE_MASK			GENMASK(7, 0)
#define HIDMA_CH_STATE_BIT_POS			0x8

#define HIDMA_IRQ_EV_CH_EOB_IRQ_BIT_POS	0
#define HIDMA_IRQ_EV_CH_WR_RESP_BIT_POS	1
#define HIDMA_IRQ_TR_CH_TRE_RD_RSP_ER_BIT_POS	9
#define HIDMA_IRQ_TR_CH_DATA_RD_ER_BIT_POS	10
#define HIDMA_IRQ_TR_CH_DATA_WR_ER_BIT_POS	11
#define HIDMA_IRQ_TR_CH_INVALID_TRE_BIT_POS	14

#define ENABLE_IRQS (BIT(HIDMA_IRQ_EV_CH_EOB_IRQ_BIT_POS)	| \
		     BIT(HIDMA_IRQ_EV_CH_WR_RESP_BIT_POS)	| \
		     BIT(HIDMA_IRQ_TR_CH_TRE_RD_RSP_ER_BIT_POS)	| \
		     BIT(HIDMA_IRQ_TR_CH_DATA_RD_ER_BIT_POS)	| \
		     BIT(HIDMA_IRQ_TR_CH_DATA_WR_ER_BIT_POS)	| \
		     BIT(HIDMA_IRQ_TR_CH_INVALID_TRE_BIT_POS))

#define HIDMA_INCREMENT_ITERATOR(iter, size, ring_size)	\
do {								\
	iter += size;						\
	if (iter >= ring_size)					\
		iter -= ring_size;				\
} while (0)

#define HIDMA_CH_STATE(val)	\
	((val >> HIDMA_CH_STATE_BIT_POS) & HIDMA_CH_STATE_MASK)

#define HIDMA_ERR_INT_MASK				\
	(BIT(HIDMA_IRQ_TR_CH_INVALID_TRE_BIT_POS)   |	\
	 BIT(HIDMA_IRQ_TR_CH_TRE_RD_RSP_ER_BIT_POS) |	\
	 BIT(HIDMA_IRQ_EV_CH_WR_RESP_BIT_POS)	    |	\
	 BIT(HIDMA_IRQ_TR_CH_DATA_RD_ER_BIT_POS)    |	\
	 BIT(HIDMA_IRQ_TR_CH_DATA_WR_ER_BIT_POS))

enum ch_command {
	HIDMA_CH_DISABLE = 0,
	HIDMA_CH_ENABLE = 1,
	HIDMA_CH_SUSPEND = 2,
	HIDMA_CH_RESET = 9,
};

enum ch_state {
	HIDMA_CH_DISABLED = 0,
	HIDMA_CH_ENABLED = 1,
	HIDMA_CH_RUNNING = 2,
	HIDMA_CH_SUSPENDED = 3,
	HIDMA_CH_STOPPED = 4,
};

enum err_code {
	HIDMA_EVRE_STATUS_COMPLETE = 1,
	HIDMA_EVRE_STATUS_ERROR = 4,
};

static int hidma_is_chan_enabled(int state)
{
	switch (state) {
	case HIDMA_CH_ENABLED:
	case HIDMA_CH_RUNNING:
		return true;
	default:
		return false;
	}
}

void hidma_ll_free(struct hidma_lldev *lldev, u32 tre_ch)
{
	struct hidma_tre *tre;

	if (tre_ch >= lldev->nr_tres) {
		dev_err(lldev->dev, "invalid TRE number in free:%d", tre_ch);
		return;
	}

	tre = &lldev->trepool[tre_ch];
	if (atomic_read(&tre->allocated) != true) {
		dev_err(lldev->dev, "trying to free an unused TRE:%d", tre_ch);
		return;
	}

	atomic_set(&tre->allocated, 0);
}

int hidma_ll_request(struct hidma_lldev *lldev, u32 sig, const char *dev_name,
		     void (*callback)(void *data), void *data, u32 *tre_ch)
{
	unsigned int i;
	struct hidma_tre *tre;
	u32 *tre_local;

	if (!tre_ch || !lldev)
		return -EINVAL;

	/* need to have at least one empty spot in the queue */
	for (i = 0; i < lldev->nr_tres - 1; i++) {
		if (atomic_add_unless(&lldev->trepool[i].allocated, 1, 1))
			break;
	}

	if (i == (lldev->nr_tres - 1))
		return -ENOMEM;

	tre = &lldev->trepool[i];
	tre->dma_sig = sig;
	tre->dev_name = dev_name;
	tre->callback = callback;
	tre->data = data;
	tre->idx = i;
	tre->status = 0;
	tre->queued = 0;
	tre->err_code = 0;
	tre->err_info = 0;
	tre->lldev = lldev;
	tre_local = &tre->tre_local[0];
	tre_local[HIDMA_TRE_CFG_IDX] = (lldev->chidx & 0xFF) << 8;
	tre_local[HIDMA_TRE_CFG_IDX] |= BIT(16);	/* set IEOB */
	*tre_ch = i;
	if (callback)
		callback(data);
	return 0;
}

/*
 * Multiple TREs may be queued and waiting in the pending queue.
 */
static void hidma_ll_tre_complete(unsigned long arg)
{
	struct hidma_lldev *lldev = (struct hidma_lldev *)arg;
	struct hidma_tre *tre;

	while (kfifo_out(&lldev->handoff_fifo, &tre, 1)) {
		/* call the user if it has been read by the hardware */
		if (tre->callback)
			tre->callback(tre->data);
	}
}

static int hidma_post_completed(struct hidma_lldev *lldev, u8 err_info,
				u8 err_code)
{
	struct hidma_tre *tre;
	unsigned long flags;
	u32 tre_iterator;

	spin_lock_irqsave(&lldev->lock, flags);

	tre_iterator = lldev->tre_processed_off;
	tre = lldev->pending_tre_list[tre_iterator / HIDMA_TRE_SIZE];
	if (!tre) {
		spin_unlock_irqrestore(&lldev->lock, flags);
		dev_warn(lldev->dev, "tre_index [%d] and tre out of sync\n",
			 tre_iterator / HIDMA_TRE_SIZE);
		return -EINVAL;
	}
	lldev->pending_tre_list[tre->tre_index] = NULL;

	/*
	 * Keep track of pending TREs that SW is expecting to receive
	 * from HW. We got one now. Decrement our counter.
	 */
	if (atomic_dec_return(&lldev->pending_tre_count) < 0) {
		dev_warn(lldev->dev, "tre count mismatch on completion");
		atomic_set(&lldev->pending_tre_count, 0);
	}

	HIDMA_INCREMENT_ITERATOR(tre_iterator, HIDMA_TRE_SIZE,
				 lldev->tre_ring_size);
	lldev->tre_processed_off = tre_iterator;
	spin_unlock_irqrestore(&lldev->lock, flags);

	tre->err_info = err_info;
	tre->err_code = err_code;
	tre->queued = 0;

	kfifo_put(&lldev->handoff_fifo, tre);
	tasklet_schedule(&lldev->task);

	return 0;
}

/*
 * Called to handle the interrupt for the channel.
 * Return a positive number if TRE or EVRE were consumed on this run.
 * Return a positive number if there are pending TREs or EVREs.
 * Return 0 if there is nothing to consume or no pending TREs/EVREs found.
 */
static int hidma_handle_tre_completion(struct hidma_lldev *lldev)
{
	u32 evre_ring_size = lldev->evre_ring_size;
	u32 err_info, err_code, evre_write_off;
	u32 evre_iterator;
	u32 num_completed = 0;

	evre_write_off = readl_relaxed(lldev->evca + HIDMA_EVCA_WRITE_PTR_REG);
	evre_iterator = lldev->evre_processed_off;

	if ((evre_write_off > evre_ring_size) ||
	    (evre_write_off % HIDMA_EVRE_SIZE)) {
		dev_err(lldev->dev, "HW reports invalid EVRE write offset\n");
		return 0;
	}

	/*
	 * By the time control reaches here the number of EVREs and TREs
	 * may not match. Only consume the ones that hardware told us.
	 */
	while ((evre_iterator != evre_write_off)) {
		u32 *current_evre = lldev->evre_ring + evre_iterator;
		u32 cfg;

		cfg = current_evre[HIDMA_EVRE_CFG_IDX];
		err_info = cfg >> HIDMA_EVRE_ERRINFO_BIT_POS;
		err_info &= HIDMA_EVRE_ERRINFO_MASK;
		err_code =
		    (cfg >> HIDMA_EVRE_CODE_BIT_POS) & HIDMA_EVRE_CODE_MASK;

		if (hidma_post_completed(lldev, err_info, err_code))
			break;

		HIDMA_INCREMENT_ITERATOR(evre_iterator, HIDMA_EVRE_SIZE,
					 evre_ring_size);

		/*
		 * Read the new event descriptor written by the HW.
		 * As we are processing the delivered events, other events
		 * get queued to the SW for processing.
		 */
		evre_write_off =
		    readl_relaxed(lldev->evca + HIDMA_EVCA_WRITE_PTR_REG);
		num_completed++;

		/*
		 * An error interrupt might have arrived while we are processing
		 * the completed interrupt.
		 */
		if (!hidma_ll_isenabled(lldev))
			break;
	}

	if (num_completed) {
		u32 evre_read_off = (lldev->evre_processed_off +
				     HIDMA_EVRE_SIZE * num_completed);
		evre_read_off = evre_read_off % evre_ring_size;
		writel(evre_read_off, lldev->evca + HIDMA_EVCA_DOORBELL_REG);

		/* record the last processed tre offset */
		lldev->evre_processed_off = evre_read_off;
	}

	return num_completed;
}

void hidma_cleanup_pending_tre(struct hidma_lldev *lldev, u8 err_info,
			       u8 err_code)
{
	while (atomic_read(&lldev->pending_tre_count)) {
		if (hidma_post_completed(lldev, err_info, err_code))
			break;
	}
}

static int hidma_ll_reset(struct hidma_lldev *lldev)
{
	u32 val;
	int ret;

	val = readl(lldev->trca + HIDMA_TRCA_CTRLSTS_REG);
	val &= ~(HIDMA_CH_CONTROL_MASK << 16);
	val |= HIDMA_CH_RESET << 16;
	writel(val, lldev->trca + HIDMA_TRCA_CTRLSTS_REG);

	/*
	 * Delay 10ms after reset to allow DMA logic to quiesce.
	 * Do a polled read up to 1ms and 10ms maximum.
	 */
	ret = readl_poll_timeout(lldev->trca + HIDMA_TRCA_CTRLSTS_REG, val,
				 HIDMA_CH_STATE(val) == HIDMA_CH_DISABLED,
				 1000, 10000);
	if (ret) {
		dev_err(lldev->dev, "transfer channel did not reset\n");
		return ret;
	}

	val = readl(lldev->evca + HIDMA_EVCA_CTRLSTS_REG);
	val &= ~(HIDMA_CH_CONTROL_MASK << 16);
	val |= HIDMA_CH_RESET << 16;
	writel(val, lldev->evca + HIDMA_EVCA_CTRLSTS_REG);

	/*
	 * Delay 10ms after reset to allow DMA logic to quiesce.
	 * Do a polled read up to 1ms and 10ms maximum.
	 */
	ret = readl_poll_timeout(lldev->evca + HIDMA_EVCA_CTRLSTS_REG, val,
				 HIDMA_CH_STATE(val) == HIDMA_CH_DISABLED,
				 1000, 10000);
	if (ret)
		return ret;

	lldev->trch_state = HIDMA_CH_DISABLED;
	lldev->evch_state = HIDMA_CH_DISABLED;
	return 0;
}

/*
 * The interrupt handler for HIDMA will try to consume as many pending
 * EVRE from the event queue as possible. Each EVRE has an associated
 * TRE that holds the user interface parameters. EVRE reports the
 * result of the transaction. Hardware guarantees ordering between EVREs
 * and TREs. We use last processed offset to figure out which TRE is
 * associated with which EVRE. If two TREs are consumed by HW, the EVREs
 * are in order in the event ring.
 *
 * This handler will do a one pass for consuming EVREs. Other EVREs may
 * be delivered while we are working. It will try to consume incoming
 * EVREs one more time and return.
 *
 * For unprocessed EVREs, hardware will trigger another interrupt until
 * all the interrupt bits are cleared.
 *
 * Hardware guarantees that by the time interrupt is observed, all data
 * transactions in flight are delivered to their respective places and
 * are visible to the CPU.
 *
 * On demand paging for IOMMU is only supported for PCIe via PRI
 * (Page Request Interface) not for HIDMA. All other hardware instances
 * including HIDMA work on pinned DMA addresses.
 *
 * HIDMA is not aware of IOMMU presence since it follows the DMA API. All
 * IOMMU latency will be built into the data movement time. By the time
 * interrupt happens, IOMMU lookups + data movement has already taken place.
 *
 * While the first read in a typical PCI endpoint ISR flushes all outstanding
 * requests traditionally to the destination, this concept does not apply
 * here for this HW.
 */
static void hidma_ll_int_handler_internal(struct hidma_lldev *lldev, int cause)
{
	unsigned long irqflags;

	if (cause & HIDMA_ERR_INT_MASK) {
		dev_err(lldev->dev, "error 0x%x, disabling...\n",
				cause);

		/* Clear out pending interrupts */
		writel(cause, lldev->evca + HIDMA_EVCA_IRQ_CLR_REG);

		/* No further submissions. */
		hidma_ll_disable(lldev);

		/* Driver completes the txn and intimates the client.*/
		hidma_cleanup_pending_tre(lldev, 0xFF,
					  HIDMA_EVRE_STATUS_ERROR);

		return;
	}

	spin_lock_irqsave(&lldev->lock, irqflags);
	writel_relaxed(cause, lldev->evca + HIDMA_EVCA_IRQ_CLR_REG);
	spin_unlock_irqrestore(&lldev->lock, irqflags);

	/*
	 * Fine tuned for this HW...
	 *
	 * This ISR has been designed for this particular hardware. Relaxed
	 * read and write accessors are used for performance reasons due to
	 * interrupt delivery guarantees. Do not copy this code blindly and
	 * expect that to work.
	 *
	 * Try to consume as many EVREs as possible.
	 */
	hidma_handle_tre_completion(lldev);
}

irqreturn_t hidma_ll_inthandler(int chirq, void *arg)
{
	struct hidma_lldev *lldev = arg;
	u32 status;
	u32 enable;
	u32 cause;

	status = readl_relaxed(lldev->evca + HIDMA_EVCA_IRQ_STAT_REG);
	enable = readl_relaxed(lldev->evca + HIDMA_EVCA_IRQ_EN_REG);
	cause = status & enable;

	while (cause) {
		hidma_ll_int_handler_internal(lldev, cause);

		/*
		 * Another interrupt might have arrived while we are
		 * processing this one. Read the new cause.
		 */
		status = readl_relaxed(lldev->evca + HIDMA_EVCA_IRQ_STAT_REG);
		enable = readl_relaxed(lldev->evca + HIDMA_EVCA_IRQ_EN_REG);
		cause = status & enable;
	}

	return IRQ_HANDLED;
}

irqreturn_t hidma_ll_inthandler_msi(int chirq, void *arg, int cause)
{
	struct hidma_lldev *lldev = arg;

	hidma_ll_int_handler_internal(lldev, cause);
	return IRQ_HANDLED;
}

int hidma_ll_enable(struct hidma_lldev *lldev)
{
	u32 val;
	int ret;

	val = readl(lldev->evca + HIDMA_EVCA_CTRLSTS_REG);
	val &= ~(HIDMA_CH_CONTROL_MASK << 16);
	val |= HIDMA_CH_ENABLE << 16;
	writel(val, lldev->evca + HIDMA_EVCA_CTRLSTS_REG);

	ret = readl_poll_timeout(lldev->evca + HIDMA_EVCA_CTRLSTS_REG, val,
				 hidma_is_chan_enabled(HIDMA_CH_STATE(val)),
				 1000, 10000);
	if (ret) {
		dev_err(lldev->dev, "event channel did not get enabled\n");
		return ret;
	}

	val = readl(lldev->trca + HIDMA_TRCA_CTRLSTS_REG);
	val &= ~(HIDMA_CH_CONTROL_MASK << 16);
	val |= HIDMA_CH_ENABLE << 16;
	writel(val, lldev->trca + HIDMA_TRCA_CTRLSTS_REG);

	ret = readl_poll_timeout(lldev->trca + HIDMA_TRCA_CTRLSTS_REG, val,
				 hidma_is_chan_enabled(HIDMA_CH_STATE(val)),
				 1000, 10000);
	if (ret) {
		dev_err(lldev->dev, "transfer channel did not get enabled\n");
		return ret;
	}

	lldev->trch_state = HIDMA_CH_ENABLED;
	lldev->evch_state = HIDMA_CH_ENABLED;

	/* enable irqs */
	writel(ENABLE_IRQS, lldev->evca + HIDMA_EVCA_IRQ_EN_REG);

	return 0;
}

void hidma_ll_start(struct hidma_lldev *lldev)
{
	unsigned long irqflags;

	spin_lock_irqsave(&lldev->lock, irqflags);
	writel(lldev->tre_write_offset, lldev->trca + HIDMA_TRCA_DOORBELL_REG);
	spin_unlock_irqrestore(&lldev->lock, irqflags);
}

bool hidma_ll_isenabled(struct hidma_lldev *lldev)
{
	u32 val;

	val = readl(lldev->trca + HIDMA_TRCA_CTRLSTS_REG);
	lldev->trch_state = HIDMA_CH_STATE(val);
	val = readl(lldev->evca + HIDMA_EVCA_CTRLSTS_REG);
	lldev->evch_state = HIDMA_CH_STATE(val);

	/* both channels have to be enabled before calling this function */
	if (hidma_is_chan_enabled(lldev->trch_state) &&
	    hidma_is_chan_enabled(lldev->evch_state))
		return true;

	return false;
}

void hidma_ll_queue_request(struct hidma_lldev *lldev, u32 tre_ch)
{
	struct hidma_tre *tre;
	unsigned long flags;

	tre = &lldev->trepool[tre_ch];

	/* copy the TRE into its location in the TRE ring */
	spin_lock_irqsave(&lldev->lock, flags);
	tre->tre_index = lldev->tre_write_offset / HIDMA_TRE_SIZE;
	lldev->pending_tre_list[tre->tre_index] = tre;
	memcpy(lldev->tre_ring + lldev->tre_write_offset,
			&tre->tre_local[0], HIDMA_TRE_SIZE);
	tre->err_code = 0;
	tre->err_info = 0;
	tre->queued = 1;
	atomic_inc(&lldev->pending_tre_count);
	lldev->tre_write_offset = (lldev->tre_write_offset + HIDMA_TRE_SIZE)
					% lldev->tre_ring_size;
	spin_unlock_irqrestore(&lldev->lock, flags);
}

/*
 * Note that even though we stop this channel if there is a pending transaction
 * in flight it will complete and follow the callback. This request will
 * prevent further requests to be made.
 */
int hidma_ll_disable(struct hidma_lldev *lldev)
{
	u32 val;
	int ret;

	/* The channel needs to be in working state */
	if (!hidma_ll_isenabled(lldev))
		return 0;

	val = readl(lldev->trca + HIDMA_TRCA_CTRLSTS_REG);
	val &= ~(HIDMA_CH_CONTROL_MASK << 16);
	val |= HIDMA_CH_SUSPEND << 16;
	writel(val, lldev->trca + HIDMA_TRCA_CTRLSTS_REG);

	/*
	 * Start the wait right after the suspend is confirmed.
	 * Do a polled read up to 1ms and 10ms maximum.
	 */
	ret = readl_poll_timeout(lldev->trca + HIDMA_TRCA_CTRLSTS_REG, val,
				 HIDMA_CH_STATE(val) == HIDMA_CH_SUSPENDED,
				 1000, 10000);
	if (ret)
		return ret;

	val = readl(lldev->evca + HIDMA_EVCA_CTRLSTS_REG);
	val &= ~(HIDMA_CH_CONTROL_MASK << 16);
	val |= HIDMA_CH_SUSPEND << 16;
	writel(val, lldev->evca + HIDMA_EVCA_CTRLSTS_REG);

	/*
	 * Start the wait right after the suspend is confirmed
	 * Delay up to 10ms after reset to allow DMA logic to quiesce.
	 */
	ret = readl_poll_timeout(lldev->evca + HIDMA_EVCA_CTRLSTS_REG, val,
				 HIDMA_CH_STATE(val) == HIDMA_CH_SUSPENDED,
				 1000, 10000);
	if (ret)
		return ret;

	lldev->trch_state = HIDMA_CH_SUSPENDED;
	lldev->evch_state = HIDMA_CH_SUSPENDED;

	/* disable interrupts */
	writel(0, lldev->evca + HIDMA_EVCA_IRQ_EN_REG);
	return 0;
}

void hidma_ll_set_transfer_params(struct hidma_lldev *lldev, u32 tre_ch,
				  dma_addr_t src, dma_addr_t dest, u32 len,
				  u32 flags, u32 txntype)
{
	struct hidma_tre *tre;
	u32 *tre_local;

	if (tre_ch >= lldev->nr_tres) {
		dev_err(lldev->dev, "invalid TRE number in transfer params:%d",
			tre_ch);
		return;
	}

	tre = &lldev->trepool[tre_ch];
	if (atomic_read(&tre->allocated) != true) {
		dev_err(lldev->dev, "trying to set params on an unused TRE:%d",
			tre_ch);
		return;
	}

	tre_local = &tre->tre_local[0];
	tre_local[HIDMA_TRE_CFG_IDX] &= ~GENMASK(7, 0);
	tre_local[HIDMA_TRE_CFG_IDX] |= txntype;
	tre_local[HIDMA_TRE_LEN_IDX] = len;
	tre_local[HIDMA_TRE_SRC_LOW_IDX] = lower_32_bits(src);
	tre_local[HIDMA_TRE_SRC_HI_IDX] = upper_32_bits(src);
	tre_local[HIDMA_TRE_DEST_LOW_IDX] = lower_32_bits(dest);
	tre_local[HIDMA_TRE_DEST_HI_IDX] = upper_32_bits(dest);
	tre->int_flags = flags;
}

/*
 * Called during initialization and after an error condition
 * to restore hardware state.
 */
int hidma_ll_setup(struct hidma_lldev *lldev)
{
	int rc;
	u64 addr;
	u32 val;
	u32 nr_tres = lldev->nr_tres;

	atomic_set(&lldev->pending_tre_count, 0);
	lldev->tre_processed_off = 0;
	lldev->evre_processed_off = 0;
	lldev->tre_write_offset = 0;

	/* disable interrupts */
	writel(0, lldev->evca + HIDMA_EVCA_IRQ_EN_REG);

	/* clear all pending interrupts */
	val = readl(lldev->evca + HIDMA_EVCA_IRQ_STAT_REG);
	writel(val, lldev->evca + HIDMA_EVCA_IRQ_CLR_REG);

	rc = hidma_ll_reset(lldev);
	if (rc)
		return rc;

	/*
	 * Clear all pending interrupts again.
	 * Otherwise, we observe reset complete interrupts.
	 */
	val = readl(lldev->evca + HIDMA_EVCA_IRQ_STAT_REG);
	writel(val, lldev->evca + HIDMA_EVCA_IRQ_CLR_REG);

	/* disable interrupts again after reset */
	writel(0, lldev->evca + HIDMA_EVCA_IRQ_EN_REG);

	addr = lldev->tre_dma;
	writel(lower_32_bits(addr), lldev->trca + HIDMA_TRCA_RING_LOW_REG);
	writel(upper_32_bits(addr), lldev->trca + HIDMA_TRCA_RING_HIGH_REG);
	writel(lldev->tre_ring_size, lldev->trca + HIDMA_TRCA_RING_LEN_REG);

	addr = lldev->evre_dma;
	writel(lower_32_bits(addr), lldev->evca + HIDMA_EVCA_RING_LOW_REG);
	writel(upper_32_bits(addr), lldev->evca + HIDMA_EVCA_RING_HIGH_REG);
	writel(HIDMA_EVRE_SIZE * nr_tres,
			lldev->evca + HIDMA_EVCA_RING_LEN_REG);

	/* configure interrupts */
	hidma_ll_setup_irq(lldev, lldev->msi_support);

	rc = hidma_ll_enable(lldev);
	if (rc)
		return rc;

	return rc;
}

void hidma_ll_setup_irq(struct hidma_lldev *lldev, bool msi)
{
	u32 val;

	lldev->msi_support = msi;

	/* disable interrupts again after reset */
	writel(0, lldev->evca + HIDMA_EVCA_IRQ_CLR_REG);
	writel(0, lldev->evca + HIDMA_EVCA_IRQ_EN_REG);

	/* support IRQ by default */
	val = readl(lldev->evca + HIDMA_EVCA_INTCTRL_REG);
	val &= ~0xF;
	if (!lldev->msi_support)
		val = val | 0x1;
	writel(val, lldev->evca + HIDMA_EVCA_INTCTRL_REG);

	/* clear all pending interrupts and enable them */
	writel(ENABLE_IRQS, lldev->evca + HIDMA_EVCA_IRQ_CLR_REG);
	writel(ENABLE_IRQS, lldev->evca + HIDMA_EVCA_IRQ_EN_REG);
}

struct hidma_lldev *hidma_ll_init(struct device *dev, u32 nr_tres,
				  void __iomem *trca, void __iomem *evca,
				  u8 chidx)
{
	u32 required_bytes;
	struct hidma_lldev *lldev;
	int rc;
	size_t sz;

	if (!trca || !evca || !dev || !nr_tres)
		return NULL;

	/* need at least four TREs */
	if (nr_tres < 4)
		return NULL;

	/* need an extra space */
	nr_tres += 1;

	lldev = devm_kzalloc(dev, sizeof(struct hidma_lldev), GFP_KERNEL);
	if (!lldev)
		return NULL;

	lldev->evca = evca;
	lldev->trca = trca;
	lldev->dev = dev;
	sz = sizeof(struct hidma_tre);
	lldev->trepool = devm_kcalloc(lldev->dev, nr_tres, sz, GFP_KERNEL);
	if (!lldev->trepool)
		return NULL;

	required_bytes = sizeof(lldev->pending_tre_list[0]);
	lldev->pending_tre_list = devm_kcalloc(dev, nr_tres, required_bytes,
					       GFP_KERNEL);
	if (!lldev->pending_tre_list)
		return NULL;

	sz = (HIDMA_TRE_SIZE + 1) * nr_tres;
	lldev->tre_ring = dmam_alloc_coherent(dev, sz, &lldev->tre_dma,
					      GFP_KERNEL);
	if (!lldev->tre_ring)
		return NULL;

	lldev->tre_ring_size = HIDMA_TRE_SIZE * nr_tres;
	lldev->nr_tres = nr_tres;

	/* the TRE ring has to be TRE_SIZE aligned */
	if (!IS_ALIGNED(lldev->tre_dma, HIDMA_TRE_SIZE)) {
		u8 tre_ring_shift;

		tre_ring_shift = lldev->tre_dma % HIDMA_TRE_SIZE;
		tre_ring_shift = HIDMA_TRE_SIZE - tre_ring_shift;
		lldev->tre_dma += tre_ring_shift;
		lldev->tre_ring += tre_ring_shift;
	}

	sz = (HIDMA_EVRE_SIZE + 1) * nr_tres;
	lldev->evre_ring = dmam_alloc_coherent(dev, sz, &lldev->evre_dma,
					       GFP_KERNEL);
	if (!lldev->evre_ring)
		return NULL;

	lldev->evre_ring_size = HIDMA_EVRE_SIZE * nr_tres;

	/* the EVRE ring has to be EVRE_SIZE aligned */
	if (!IS_ALIGNED(lldev->evre_dma, HIDMA_EVRE_SIZE)) {
		u8 evre_ring_shift;

		evre_ring_shift = lldev->evre_dma % HIDMA_EVRE_SIZE;
		evre_ring_shift = HIDMA_EVRE_SIZE - evre_ring_shift;
		lldev->evre_dma += evre_ring_shift;
		lldev->evre_ring += evre_ring_shift;
	}
	lldev->nr_tres = nr_tres;
	lldev->chidx = chidx;

	sz = nr_tres * sizeof(struct hidma_tre *);
	rc = kfifo_alloc(&lldev->handoff_fifo, sz, GFP_KERNEL);
	if (rc)
		return NULL;

	rc = hidma_ll_setup(lldev);
	if (rc)
		return NULL;

	spin_lock_init(&lldev->lock);
	tasklet_init(&lldev->task, hidma_ll_tre_complete, (unsigned long)lldev);
	lldev->initialized = 1;
	writel(ENABLE_IRQS, lldev->evca + HIDMA_EVCA_IRQ_EN_REG);
	return lldev;
}

int hidma_ll_uninit(struct hidma_lldev *lldev)
{
	u32 required_bytes;
	int rc = 0;
	u32 val;

	if (!lldev)
		return -ENODEV;

	if (!lldev->initialized)
		return 0;

	lldev->initialized = 0;

	required_bytes = sizeof(struct hidma_tre) * lldev->nr_tres;
	tasklet_kill(&lldev->task);
	memset(lldev->trepool, 0, required_bytes);
	lldev->trepool = NULL;
	atomic_set(&lldev->pending_tre_count, 0);
	lldev->tre_write_offset = 0;

	rc = hidma_ll_reset(lldev);

	/*
	 * Clear all pending interrupts again.
	 * Otherwise, we observe reset complete interrupts.
	 */
	val = readl(lldev->evca + HIDMA_EVCA_IRQ_STAT_REG);
	writel(val, lldev->evca + HIDMA_EVCA_IRQ_CLR_REG);
	writel(0, lldev->evca + HIDMA_EVCA_IRQ_EN_REG);
	return rc;
}

enum dma_status hidma_ll_status(struct hidma_lldev *lldev, u32 tre_ch)
{
	enum dma_status ret = DMA_ERROR;
	struct hidma_tre *tre;
	unsigned long flags;
	u8 err_code;

	spin_lock_irqsave(&lldev->lock, flags);

	tre = &lldev->trepool[tre_ch];
	err_code = tre->err_code;

	if (err_code & HIDMA_EVRE_STATUS_COMPLETE)
		ret = DMA_COMPLETE;
	else if (err_code & HIDMA_EVRE_STATUS_ERROR)
		ret = DMA_ERROR;
	else
		ret = DMA_IN_PROGRESS;
	spin_unlock_irqrestore(&lldev->lock, flags);

	return ret;
}
