/*
 * Copyright (c) 2007-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "core.h"
#include "target.h"
#include "hif-ops.h"
#include "htc_hif.h"
#include "debug.h"

#define MAILBOX_FOR_BLOCK_SIZE          1

#define ATH6KL_TIME_QUANTUM	10  /* in ms */

static int ath6kldev_cp_scat_dma_buf(struct hif_scatter_req *req, bool from_dma)
{
	u8 *buf;
	int i;

	buf = req->virt_dma_buf;

	for (i = 0; i < req->scat_entries; i++) {

		if (from_dma)
			memcpy(req->scat_list[i].buf, buf,
			       req->scat_list[i].len);
		else
			memcpy(buf, req->scat_list[i].buf,
			       req->scat_list[i].len);

		buf += req->scat_list[i].len;
	}

	return 0;
}

int ath6kldev_rw_comp_handler(void *context, int status)
{
	struct htc_packet *packet = context;

	ath6kl_dbg(ATH6KL_DBG_HTC_RECV,
		   "ath6kldev_rw_comp_handler (pkt:0x%p , status: %d\n",
		   packet, status);

	packet->status = status;
	packet->completion(packet->context, packet);

	return 0;
}

static int ath6kldev_proc_dbg_intr(struct ath6kl_device *dev)
{
	u32 dummy;
	int status;

	ath6kl_err("target debug interrupt\n");

	ath6kl_target_failure(dev->ar);

	/*
	 * read counter to clear the interrupt, the debug error interrupt is
	 * counter 0.
	 */
	status = hif_read_write_sync(dev->ar, COUNT_DEC_ADDRESS,
				     (u8 *)&dummy, 4, HIF_RD_SYNC_BYTE_INC);
	if (status)
		WARN_ON(1);

	return status;
}

/* mailbox recv message polling */
int ath6kldev_poll_mboxmsg_rx(struct ath6kl_device *dev, u32 *lk_ahd,
			      int timeout)
{
	struct ath6kl_irq_proc_registers *rg;
	int status = 0, i;
	u8 htc_mbox = 1 << HTC_MAILBOX;

	for (i = timeout / ATH6KL_TIME_QUANTUM; i > 0; i--) {
		/* this is the standard HIF way, load the reg table */
		status = hif_read_write_sync(dev->ar, HOST_INT_STATUS_ADDRESS,
					     (u8 *) &dev->irq_proc_reg,
					     sizeof(dev->irq_proc_reg),
					     HIF_RD_SYNC_BYTE_INC);

		if (status) {
			ath6kl_err("failed to read reg table\n");
			return status;
		}

		/* check for MBOX data and valid lookahead */
		if (dev->irq_proc_reg.host_int_status & htc_mbox) {
			if (dev->irq_proc_reg.rx_lkahd_valid &
			    htc_mbox) {
				/*
				 * Mailbox has a message and the look ahead
				 * is valid.
				 */
				rg = &dev->irq_proc_reg;
				*lk_ahd =
					le32_to_cpu(rg->rx_lkahd[HTC_MAILBOX]);
				break;
			}
		}

		/* delay a little  */
		mdelay(ATH6KL_TIME_QUANTUM);
		ath6kl_dbg(ATH6KL_DBG_HTC_RECV, "retry mbox poll : %d\n", i);
	}

	if (i == 0) {
		ath6kl_err("timeout waiting for recv message\n");
		status = -ETIME;
		/* check if the target asserted */
		if (dev->irq_proc_reg.counter_int_status &
		    ATH6KL_TARGET_DEBUG_INTR_MASK)
			/*
			 * Target failure handler will be called in case of
			 * an assert.
			 */
			ath6kldev_proc_dbg_intr(dev);
	}

	return status;
}

/*
 * Disable packet reception (used in case the host runs out of buffers)
 * using the interrupt enable registers through the host I/F
 */
int ath6kldev_rx_control(struct ath6kl_device *dev, bool enable_rx)
{
	struct ath6kl_irq_enable_reg regs;
	int status = 0;

	/* take the lock to protect interrupt enable shadows */
	spin_lock_bh(&dev->lock);

	if (enable_rx)
		dev->irq_en_reg.int_status_en |=
			SM(INT_STATUS_ENABLE_MBOX_DATA, 0x01);
	else
		dev->irq_en_reg.int_status_en &=
		    ~SM(INT_STATUS_ENABLE_MBOX_DATA, 0x01);

	memcpy(&regs, &dev->irq_en_reg, sizeof(regs));

	spin_unlock_bh(&dev->lock);

	status = hif_read_write_sync(dev->ar, INT_STATUS_ENABLE_ADDRESS,
				     &regs.int_status_en,
				     sizeof(struct ath6kl_irq_enable_reg),
				     HIF_WR_SYNC_BYTE_INC);

	return status;
}

int ath6kldev_submit_scat_req(struct ath6kl_device *dev,
			      struct hif_scatter_req *scat_req, bool read)
{
	int status = 0;

	if (read) {
		scat_req->req = HIF_RD_SYNC_BLOCK_FIX;
		scat_req->addr = dev->ar->mbox_info.htc_addr;
	} else {
		scat_req->req = HIF_WR_ASYNC_BLOCK_INC;

		scat_req->addr =
			(scat_req->len > HIF_MBOX_WIDTH) ?
			dev->ar->mbox_info.htc_ext_addr :
			dev->ar->mbox_info.htc_addr;
	}

	ath6kl_dbg((ATH6KL_DBG_HTC_RECV | ATH6KL_DBG_HTC_SEND),
		   "ath6kldev_submit_scat_req, entries: %d, total len: %d mbox:0x%X (mode: %s : %s)\n",
		   scat_req->scat_entries, scat_req->len,
		   scat_req->addr, !read ? "async" : "sync",
		   (read) ? "rd" : "wr");

	if (!read && scat_req->virt_scat) {
		status = ath6kldev_cp_scat_dma_buf(scat_req, false);
		if (status) {
			scat_req->status = status;
			scat_req->complete(dev->ar->htc_target, scat_req);
			return 0;
		}
	}

	status = ath6kl_hif_scat_req_rw(dev->ar, scat_req);

	if (read) {
		/* in sync mode, we can touch the scatter request */
		scat_req->status = status;
		if (!status && scat_req->virt_scat)
			scat_req->status =
				ath6kldev_cp_scat_dma_buf(scat_req, true);
	}

	return status;
}

static int ath6kldev_proc_counter_intr(struct ath6kl_device *dev)
{
	u8 counter_int_status;

	ath6kl_dbg(ATH6KL_DBG_IRQ, "counter interrupt\n");

	counter_int_status = dev->irq_proc_reg.counter_int_status &
			     dev->irq_en_reg.cntr_int_status_en;

	ath6kl_dbg(ATH6KL_DBG_IRQ,
		"valid interrupt source(s) in COUNTER_INT_STATUS: 0x%x\n",
		counter_int_status);

	/*
	 * NOTE: other modules like GMBOX may use the counter interrupt for
	 * credit flow control on other counters, we only need to check for
	 * the debug assertion counter interrupt.
	 */
	if (counter_int_status & ATH6KL_TARGET_DEBUG_INTR_MASK)
		return ath6kldev_proc_dbg_intr(dev);

	return 0;
}

static int ath6kldev_proc_err_intr(struct ath6kl_device *dev)
{
	int status;
	u8 error_int_status;
	u8 reg_buf[4];

	ath6kl_dbg(ATH6KL_DBG_IRQ, "error interrupt\n");

	error_int_status = dev->irq_proc_reg.error_int_status & 0x0F;
	if (!error_int_status) {
		WARN_ON(1);
		return -EIO;
	}

	ath6kl_dbg(ATH6KL_DBG_IRQ,
		   "valid interrupt source(s) in ERROR_INT_STATUS: 0x%x\n",
		   error_int_status);

	if (MS(ERROR_INT_STATUS_WAKEUP, error_int_status))
		ath6kl_dbg(ATH6KL_DBG_IRQ, "error : wakeup\n");

	if (MS(ERROR_INT_STATUS_RX_UNDERFLOW, error_int_status))
		ath6kl_err("rx underflow\n");

	if (MS(ERROR_INT_STATUS_TX_OVERFLOW, error_int_status))
		ath6kl_err("tx overflow\n");

	/* Clear the interrupt */
	dev->irq_proc_reg.error_int_status &= ~error_int_status;

	/* set W1C value to clear the interrupt, this hits the register first */
	reg_buf[0] = error_int_status;
	reg_buf[1] = 0;
	reg_buf[2] = 0;
	reg_buf[3] = 0;

	status = hif_read_write_sync(dev->ar, ERROR_INT_STATUS_ADDRESS,
				     reg_buf, 4, HIF_WR_SYNC_BYTE_FIX);

	if (status)
		WARN_ON(1);

	return status;
}

static int ath6kldev_proc_cpu_intr(struct ath6kl_device *dev)
{
	int status;
	u8 cpu_int_status;
	u8 reg_buf[4];

	ath6kl_dbg(ATH6KL_DBG_IRQ, "cpu interrupt\n");

	cpu_int_status = dev->irq_proc_reg.cpu_int_status &
			 dev->irq_en_reg.cpu_int_status_en;
	if (!cpu_int_status) {
		WARN_ON(1);
		return -EIO;
	}

	ath6kl_dbg(ATH6KL_DBG_IRQ,
		"valid interrupt source(s) in CPU_INT_STATUS: 0x%x\n",
		cpu_int_status);

	/* Clear the interrupt */
	dev->irq_proc_reg.cpu_int_status &= ~cpu_int_status;

	/*
	 * Set up the register transfer buffer to hit the register 4 times ,
	 * this is done to make the access 4-byte aligned to mitigate issues
	 * with host bus interconnects that restrict bus transfer lengths to
	 * be a multiple of 4-bytes.
	 */

	/* set W1C value to clear the interrupt, this hits the register first */
	reg_buf[0] = cpu_int_status;
	/* the remaining are set to zero which have no-effect  */
	reg_buf[1] = 0;
	reg_buf[2] = 0;
	reg_buf[3] = 0;

	status = hif_read_write_sync(dev->ar, CPU_INT_STATUS_ADDRESS,
				     reg_buf, 4, HIF_WR_SYNC_BYTE_FIX);

	if (status)
		WARN_ON(1);

	return status;
}

/* process pending interrupts synchronously */
static int proc_pending_irqs(struct ath6kl_device *dev, bool *done)
{
	struct ath6kl_irq_proc_registers *rg;
	int status = 0;
	u8 host_int_status = 0;
	u32 lk_ahd = 0;
	u8 htc_mbox = 1 << HTC_MAILBOX;

	ath6kl_dbg(ATH6KL_DBG_IRQ, "proc_pending_irqs: (dev: 0x%p)\n", dev);

	/*
	 * NOTE: HIF implementation guarantees that the context of this
	 * call allows us to perform SYNCHRONOUS I/O, that is we can block,
	 * sleep or call any API that can block or switch thread/task
	 * contexts. This is a fully schedulable context.
	 */

	/*
	 * Process pending intr only when int_status_en is clear, it may
	 * result in unnecessary bus transaction otherwise. Target may be
	 * unresponsive at the time.
	 */
	if (dev->irq_en_reg.int_status_en) {
		/*
		 * Read the first 28 bytes of the HTC register table. This
		 * will yield us the value of different int status
		 * registers and the lookahead registers.
		 *
		 *    length = sizeof(int_status) + sizeof(cpu_int_status)
		 *             + sizeof(error_int_status) +
		 *             sizeof(counter_int_status) +
		 *             sizeof(mbox_frame) + sizeof(rx_lkahd_valid)
		 *             + sizeof(hole) + sizeof(rx_lkahd) +
		 *             sizeof(int_status_en) +
		 *             sizeof(cpu_int_status_en) +
		 *             sizeof(err_int_status_en) +
		 *             sizeof(cntr_int_status_en);
		 */
		status = hif_read_write_sync(dev->ar, HOST_INT_STATUS_ADDRESS,
					     (u8 *) &dev->irq_proc_reg,
					     sizeof(dev->irq_proc_reg),
					     HIF_RD_SYNC_BYTE_INC);
		if (status)
			goto out;

		if (AR_DBG_LVL_CHECK(ATH6KL_DBG_IRQ))
			ath6kl_dump_registers(dev, &dev->irq_proc_reg,
					 &dev->irq_en_reg);

		/* Update only those registers that are enabled */
		host_int_status = dev->irq_proc_reg.host_int_status &
				  dev->irq_en_reg.int_status_en;

		/* Look at mbox status */
		if (host_int_status & htc_mbox) {
			/*
			 * Mask out pending mbox value, we use "lookAhead as
			 * the real flag for mbox processing.
			 */
			host_int_status &= ~htc_mbox;
			if (dev->irq_proc_reg.rx_lkahd_valid &
			    htc_mbox) {
				rg = &dev->irq_proc_reg;
				lk_ahd = le32_to_cpu(rg->rx_lkahd[HTC_MAILBOX]);
				if (!lk_ahd)
					ath6kl_err("lookAhead is zero!\n");
			}
		}
	}

	if (!host_int_status && !lk_ahd) {
		*done = true;
		goto out;
	}

	if (lk_ahd) {
		int fetched = 0;

		ath6kl_dbg(ATH6KL_DBG_IRQ,
			   "pending mailbox msg, lk_ahd: 0x%X\n", lk_ahd);
		/*
		 * Mailbox Interrupt, the HTC layer may issue async
		 * requests to empty the mailbox. When emptying the recv
		 * mailbox we use the async handler above called from the
		 * completion routine of the callers read request. This can
		 * improve performance by reducing context switching when
		 * we rapidly pull packets.
		 */
		status = ath6kl_htc_rxmsg_pending_handler(dev->htc_cnxt,
							  &lk_ahd, &fetched);
		if (status)
			goto out;

		if (!fetched)
			/*
			 * HTC could not pull any messages out due to lack
			 * of resources.
			 */
			dev->htc_cnxt->chk_irq_status_cnt = 0;
	}

	/* now handle the rest of them */
	ath6kl_dbg(ATH6KL_DBG_IRQ,
		   "valid interrupt source(s) for other interrupts: 0x%x\n",
		   host_int_status);

	if (MS(HOST_INT_STATUS_CPU, host_int_status)) {
		/* CPU Interrupt */
		status = ath6kldev_proc_cpu_intr(dev);
		if (status)
			goto out;
	}

	if (MS(HOST_INT_STATUS_ERROR, host_int_status)) {
		/* Error Interrupt */
		status = ath6kldev_proc_err_intr(dev);
		if (status)
			goto out;
	}

	if (MS(HOST_INT_STATUS_COUNTER, host_int_status))
		/* Counter Interrupt */
		status = ath6kldev_proc_counter_intr(dev);

out:
	/*
	 * An optimization to bypass reading the IRQ status registers
	 * unecessarily which can re-wake the target, if upper layers
	 * determine that we are in a low-throughput mode, we can rely on
	 * taking another interrupt rather than re-checking the status
	 * registers which can re-wake the target.
	 *
	 * NOTE : for host interfaces that makes use of detecting pending
	 * mbox messages at hif can not use this optimization due to
	 * possible side effects, SPI requires the host to drain all
	 * messages from the mailbox before exiting the ISR routine.
	 */

	ath6kl_dbg(ATH6KL_DBG_IRQ,
		   "bypassing irq status re-check, forcing done\n");

	if (!dev->htc_cnxt->chk_irq_status_cnt)
		*done = true;

	ath6kl_dbg(ATH6KL_DBG_IRQ,
		   "proc_pending_irqs: (done:%d, status=%d\n", *done, status);

	return status;
}

/* interrupt handler, kicks off all interrupt processing */
int ath6kldev_intr_bh_handler(struct ath6kl *ar)
{
	struct ath6kl_device *dev = ar->htc_target->dev;
	int status = 0;
	bool done = false;

	/*
	 * Reset counter used to flag a re-scan of IRQ status registers on
	 * the target.
	 */
	dev->htc_cnxt->chk_irq_status_cnt = 0;

	/*
	 * IRQ processing is synchronous, interrupt status registers can be
	 * re-read.
	 */
	while (!done) {
		status = proc_pending_irqs(dev, &done);
		if (status)
			break;
	}

	return status;
}

static int ath6kldev_enable_intrs(struct ath6kl_device *dev)
{
	struct ath6kl_irq_enable_reg regs;
	int status;

	spin_lock_bh(&dev->lock);

	/* Enable all but ATH6KL CPU interrupts */
	dev->irq_en_reg.int_status_en =
			SM(INT_STATUS_ENABLE_ERROR, 0x01) |
			SM(INT_STATUS_ENABLE_CPU, 0x01) |
			SM(INT_STATUS_ENABLE_COUNTER, 0x01);

	/*
	 * NOTE: There are some cases where HIF can do detection of
	 * pending mbox messages which is disabled now.
	 */
	dev->irq_en_reg.int_status_en |= SM(INT_STATUS_ENABLE_MBOX_DATA, 0x01);

	/* Set up the CPU Interrupt status Register */
	dev->irq_en_reg.cpu_int_status_en = 0;

	/* Set up the Error Interrupt status Register */
	dev->irq_en_reg.err_int_status_en =
		SM(ERROR_STATUS_ENABLE_RX_UNDERFLOW, 0x01) |
		SM(ERROR_STATUS_ENABLE_TX_OVERFLOW, 0x1);

	/*
	 * Enable Counter interrupt status register to get fatal errors for
	 * debugging.
	 */
	dev->irq_en_reg.cntr_int_status_en = SM(COUNTER_INT_STATUS_ENABLE_BIT,
						ATH6KL_TARGET_DEBUG_INTR_MASK);
	memcpy(&regs, &dev->irq_en_reg, sizeof(regs));

	spin_unlock_bh(&dev->lock);

	status = hif_read_write_sync(dev->ar, INT_STATUS_ENABLE_ADDRESS,
				     &regs.int_status_en, sizeof(regs),
				     HIF_WR_SYNC_BYTE_INC);

	if (status)
		ath6kl_err("failed to update interrupt ctl reg err: %d\n",
			   status);

	return status;
}

int ath6kldev_disable_intrs(struct ath6kl_device *dev)
{
	struct ath6kl_irq_enable_reg regs;

	spin_lock_bh(&dev->lock);
	/* Disable all interrupts */
	dev->irq_en_reg.int_status_en = 0;
	dev->irq_en_reg.cpu_int_status_en = 0;
	dev->irq_en_reg.err_int_status_en = 0;
	dev->irq_en_reg.cntr_int_status_en = 0;
	memcpy(&regs, &dev->irq_en_reg, sizeof(regs));
	spin_unlock_bh(&dev->lock);

	return hif_read_write_sync(dev->ar, INT_STATUS_ENABLE_ADDRESS,
				   &regs.int_status_en, sizeof(regs),
				   HIF_WR_SYNC_BYTE_INC);
}

/* enable device interrupts */
int ath6kldev_unmask_intrs(struct ath6kl_device *dev)
{
	int status = 0;

	/*
	 * Make sure interrupt are disabled before unmasking at the HIF
	 * layer. The rationale here is that between device insertion
	 * (where we clear the interrupts the first time) and when HTC
	 * is finally ready to handle interrupts, other software can perform
	 * target "soft" resets. The ATH6KL interrupt enables reset back to an
	 * "enabled" state when this happens.
	 */
	ath6kldev_disable_intrs(dev);

	/* unmask the host controller interrupts */
	ath6kl_hif_irq_enable(dev->ar);
	status = ath6kldev_enable_intrs(dev);

	return status;
}

/* disable all device interrupts */
int ath6kldev_mask_intrs(struct ath6kl_device *dev)
{
	/*
	 * Mask the interrupt at the HIF layer to avoid any stray interrupt
	 * taken while we zero out our shadow registers in
	 * ath6kldev_disable_intrs().
	 */
	ath6kl_hif_irq_disable(dev->ar);

	return ath6kldev_disable_intrs(dev);
}

int ath6kldev_setup(struct ath6kl_device *dev)
{
	int status = 0;

	spin_lock_init(&dev->lock);

	/*
	 * NOTE: we actually get the block size of a mailbox other than 0,
	 * for SDIO the block size on mailbox 0 is artificially set to 1.
	 * So we use the block size that is set for the other 3 mailboxes.
	 */
	dev->htc_cnxt->block_sz = dev->ar->mbox_info.block_size;

	/* must be a power of 2 */
	if ((dev->htc_cnxt->block_sz & (dev->htc_cnxt->block_sz - 1)) != 0) {
		WARN_ON(1);
		goto fail_setup;
	}

	/* assemble mask, used for padding to a block */
	dev->htc_cnxt->block_mask = dev->htc_cnxt->block_sz - 1;

	ath6kl_dbg(ATH6KL_DBG_TRC, "block size: %d, mbox addr:0x%X\n",
		   dev->htc_cnxt->block_sz, dev->ar->mbox_info.htc_addr);

	ath6kl_dbg(ATH6KL_DBG_TRC,
		   "hif interrupt processing is sync only\n");

	status = ath6kldev_disable_intrs(dev);

fail_setup:
	return status;

}
