// SPDX-License-Identifier: GPL-2.0-only
/*
 * LocalPlus Bus FIFO driver for the Freescale MPC52xx.
 *
 * Copyright (C) 2009 Secret Lab Technologies Ltd.
 *
 * Todo:
 * - Add support for multiple requests to be queued.
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <asm/io.h>
#include <asm/mpc52xx.h>
#include <asm/time.h>

#include <linux/fsl/bestcomm/bestcomm.h>
#include <linux/fsl/bestcomm/bestcomm_priv.h>
#include <linux/fsl/bestcomm/gen_bd.h>

MODULE_AUTHOR("Grant Likely <grant.likely@secretlab.ca>");
MODULE_DESCRIPTION("MPC5200 LocalPlus FIFO device driver");
MODULE_LICENSE("GPL");

#define LPBFIFO_REG_PACKET_SIZE		(0x00)
#define LPBFIFO_REG_START_ADDRESS	(0x04)
#define LPBFIFO_REG_CONTROL		(0x08)
#define LPBFIFO_REG_ENABLE		(0x0C)
#define LPBFIFO_REG_BYTES_DONE_STATUS	(0x14)
#define LPBFIFO_REG_FIFO_DATA		(0x40)
#define LPBFIFO_REG_FIFO_STATUS		(0x44)
#define LPBFIFO_REG_FIFO_CONTROL	(0x48)
#define LPBFIFO_REG_FIFO_ALARM		(0x4C)

struct mpc52xx_lpbfifo {
	struct device *dev;
	phys_addr_t regs_phys;
	void __iomem *regs;
	int irq;
	spinlock_t lock;

	struct bcom_task *bcom_tx_task;
	struct bcom_task *bcom_rx_task;
	struct bcom_task *bcom_cur_task;

	/* Current state data */
	struct mpc52xx_lpbfifo_request *req;
	int dma_irqs_enabled;
};

/* The MPC5200 has only one fifo, so only need one instance structure */
static struct mpc52xx_lpbfifo lpbfifo;

/**
 * mpc52xx_lpbfifo_kick - Trigger the next block of data to be transferred
 */
static void mpc52xx_lpbfifo_kick(struct mpc52xx_lpbfifo_request *req)
{
	size_t transfer_size = req->size - req->pos;
	struct bcom_bd *bd;
	void __iomem *reg;
	u32 *data;
	int i;
	int bit_fields;
	int dma = !(req->flags & MPC52XX_LPBFIFO_FLAG_NO_DMA);
	int write = req->flags & MPC52XX_LPBFIFO_FLAG_WRITE;
	int poll_dma = req->flags & MPC52XX_LPBFIFO_FLAG_POLL_DMA;

	/* Set and clear the reset bits; is good practice in User Manual */
	out_be32(lpbfifo.regs + LPBFIFO_REG_ENABLE, 0x01010000);

	/* set master enable bit */
	out_be32(lpbfifo.regs + LPBFIFO_REG_ENABLE, 0x00000001);
	if (!dma) {
		/* While the FIFO can be setup for transfer sizes as large as
		 * 16M-1, the FIFO itself is only 512 bytes deep and it does
		 * not generate interrupts for FIFO full events (only transfer
		 * complete will raise an IRQ).  Therefore when not using
		 * Bestcomm to drive the FIFO it needs to either be polled, or
		 * transfers need to constrained to the size of the fifo.
		 *
		 * This driver restricts the size of the transfer
		 */
		if (transfer_size > 512)
			transfer_size = 512;

		/* Load the FIFO with data */
		if (write) {
			reg = lpbfifo.regs + LPBFIFO_REG_FIFO_DATA;
			data = req->data + req->pos;
			for (i = 0; i < transfer_size; i += 4)
				out_be32(reg, *data++);
		}

		/* Unmask both error and completion irqs */
		out_be32(lpbfifo.regs + LPBFIFO_REG_ENABLE, 0x00000301);
	} else {
		/* Choose the correct direction
		 *
		 * Configure the watermarks so DMA will always complete correctly.
		 * It may be worth experimenting with the ALARM value to see if
		 * there is a performance impact.  However, if it is wrong there
		 * is a risk of DMA not transferring the last chunk of data
		 */
		if (write) {
			out_be32(lpbfifo.regs + LPBFIFO_REG_FIFO_ALARM, 0x1e4);
			out_8(lpbfifo.regs + LPBFIFO_REG_FIFO_CONTROL, 7);
			lpbfifo.bcom_cur_task = lpbfifo.bcom_tx_task;
		} else {
			out_be32(lpbfifo.regs + LPBFIFO_REG_FIFO_ALARM, 0x1ff);
			out_8(lpbfifo.regs + LPBFIFO_REG_FIFO_CONTROL, 0);
			lpbfifo.bcom_cur_task = lpbfifo.bcom_rx_task;

			if (poll_dma) {
				if (lpbfifo.dma_irqs_enabled) {
					disable_irq(bcom_get_task_irq(lpbfifo.bcom_rx_task));
					lpbfifo.dma_irqs_enabled = 0;
				}
			} else {
				if (!lpbfifo.dma_irqs_enabled) {
					enable_irq(bcom_get_task_irq(lpbfifo.bcom_rx_task));
					lpbfifo.dma_irqs_enabled = 1;
				}
			}
		}

		bd = bcom_prepare_next_buffer(lpbfifo.bcom_cur_task);
		bd->status = transfer_size;
		if (!write) {
			/*
			 * In the DMA read case, the DMA doesn't complete,
			 * possibly due to incorrect watermarks in the ALARM
			 * and CONTROL regs. For now instead of trying to
			 * determine the right watermarks that will make this
			 * work, just increase the number of bytes the FIFO is
			 * expecting.
			 *
			 * When submitting another operation, the FIFO will get
			 * reset, so the condition of the FIFO waiting for a
			 * non-existent 4 bytes will get cleared.
			 */
			transfer_size += 4; /* BLECH! */
		}
		bd->data[0] = req->data_phys + req->pos;
		bcom_submit_next_buffer(lpbfifo.bcom_cur_task, NULL);

		/* error irq & master enabled bit */
		bit_fields = 0x00000201;

		/* Unmask irqs */
		if (write && (!poll_dma))
			bit_fields |= 0x00000100; /* completion irq too */
		out_be32(lpbfifo.regs + LPBFIFO_REG_ENABLE, bit_fields);
	}

	/* Set transfer size, width, chip select and READ mode */
	out_be32(lpbfifo.regs + LPBFIFO_REG_START_ADDRESS,
		 req->offset + req->pos);
	out_be32(lpbfifo.regs + LPBFIFO_REG_PACKET_SIZE, transfer_size);

	bit_fields = req->cs << 24 | 0x000008;
	if (!write)
		bit_fields |= 0x010000; /* read mode */
	out_be32(lpbfifo.regs + LPBFIFO_REG_CONTROL, bit_fields);

	/* Kick it off */
	if (!lpbfifo.req->defer_xfer_start)
		out_8(lpbfifo.regs + LPBFIFO_REG_PACKET_SIZE, 0x01);
	if (dma)
		bcom_enable(lpbfifo.bcom_cur_task);
}

/**
 * mpc52xx_lpbfifo_irq - IRQ handler for LPB FIFO
 *
 * On transmit, the dma completion irq triggers before the fifo completion
 * triggers.  Handle the dma completion here instead of the LPB FIFO Bestcomm
 * task completion irq because everything is not really done until the LPB FIFO
 * completion irq triggers.
 *
 * In other words:
 * For DMA, on receive, the "Fat Lady" is the bestcom completion irq. on
 * transmit, the fifo completion irq is the "Fat Lady". The opera (or in this
 * case the DMA/FIFO operation) is not finished until the "Fat Lady" sings.
 *
 * Reasons for entering this routine:
 * 1) PIO mode rx and tx completion irq
 * 2) DMA interrupt mode tx completion irq
 * 3) DMA polled mode tx
 *
 * Exit conditions:
 * 1) Transfer aborted
 * 2) FIFO complete without DMA; more data to do
 * 3) FIFO complete without DMA; all data transferred
 * 4) FIFO complete using DMA
 *
 * Condition 1 can occur regardless of whether or not DMA is used.
 * It requires executing the callback to report the error and exiting
 * immediately.
 *
 * Condition 2 requires programming the FIFO with the next block of data
 *
 * Condition 3 requires executing the callback to report completion
 *
 * Condition 4 means the same as 3, except that we also retrieve the bcom
 * buffer so DMA doesn't get clogged up.
 *
 * To make things trickier, the spinlock must be dropped before
 * executing the callback, otherwise we could end up with a deadlock
 * or nested spinlock condition.  The out path is non-trivial, so
 * extra fiddling is done to make sure all paths lead to the same
 * outbound code.
 */
static irqreturn_t mpc52xx_lpbfifo_irq(int irq, void *dev_id)
{
	struct mpc52xx_lpbfifo_request *req;
	u32 status = in_8(lpbfifo.regs + LPBFIFO_REG_BYTES_DONE_STATUS);
	void __iomem *reg;
	u32 *data;
	int count, i;
	int do_callback = 0;
	u32 ts;
	unsigned long flags;
	int dma, write, poll_dma;

	spin_lock_irqsave(&lpbfifo.lock, flags);
	ts = mftb();

	req = lpbfifo.req;
	if (!req) {
		spin_unlock_irqrestore(&lpbfifo.lock, flags);
		pr_err("bogus LPBFIFO IRQ\n");
		return IRQ_HANDLED;
	}

	dma = !(req->flags & MPC52XX_LPBFIFO_FLAG_NO_DMA);
	write = req->flags & MPC52XX_LPBFIFO_FLAG_WRITE;
	poll_dma = req->flags & MPC52XX_LPBFIFO_FLAG_POLL_DMA;

	if (dma && !write) {
		spin_unlock_irqrestore(&lpbfifo.lock, flags);
		pr_err("bogus LPBFIFO IRQ (dma and not writing)\n");
		return IRQ_HANDLED;
	}

	if ((status & 0x01) == 0) {
		goto out;
	}

	/* check abort bit */
	if (status & 0x10) {
		out_be32(lpbfifo.regs + LPBFIFO_REG_ENABLE, 0x01010000);
		do_callback = 1;
		goto out;
	}

	/* Read result from hardware */
	count = in_be32(lpbfifo.regs + LPBFIFO_REG_BYTES_DONE_STATUS);
	count &= 0x00ffffff;

	if (!dma && !write) {
		/* copy the data out of the FIFO */
		reg = lpbfifo.regs + LPBFIFO_REG_FIFO_DATA;
		data = req->data + req->pos;
		for (i = 0; i < count; i += 4)
			*data++ = in_be32(reg);
	}

	/* Update transfer position and count */
	req->pos += count;

	/* Decide what to do next */
	if (req->size - req->pos)
		mpc52xx_lpbfifo_kick(req); /* more work to do */
	else
		do_callback = 1;

 out:
	/* Clear the IRQ */
	out_8(lpbfifo.regs + LPBFIFO_REG_BYTES_DONE_STATUS, 0x01);

	if (dma && (status & 0x11)) {
		/*
		 * Count the DMA as complete only when the FIFO completion
		 * status or abort bits are set.
		 *
		 * (status & 0x01) should always be the case except sometimes
		 * when using polled DMA.
		 *
		 * (status & 0x10) {transfer aborted}: This case needs more
		 * testing.
		 */
		bcom_retrieve_buffer(lpbfifo.bcom_cur_task, &status, NULL);
	}
	req->last_byte = ((u8 *)req->data)[req->size - 1];

	/* When the do_callback flag is set; it means the transfer is finished
	 * so set the FIFO as idle */
	if (do_callback)
		lpbfifo.req = NULL;

	if (irq != 0) /* don't increment on polled case */
		req->irq_count++;

	req->irq_ticks += mftb() - ts;
	spin_unlock_irqrestore(&lpbfifo.lock, flags);

	/* Spinlock is released; it is now safe to call the callback */
	if (do_callback && req->callback)
		req->callback(req);

	return IRQ_HANDLED;
}

/**
 * mpc52xx_lpbfifo_bcom_irq - IRQ handler for LPB FIFO Bestcomm task
 *
 * Only used when receiving data.
 */
static irqreturn_t mpc52xx_lpbfifo_bcom_irq(int irq, void *dev_id)
{
	struct mpc52xx_lpbfifo_request *req;
	unsigned long flags;
	u32 status;
	u32 ts;

	spin_lock_irqsave(&lpbfifo.lock, flags);
	ts = mftb();

	req = lpbfifo.req;
	if (!req || (req->flags & MPC52XX_LPBFIFO_FLAG_NO_DMA)) {
		spin_unlock_irqrestore(&lpbfifo.lock, flags);
		return IRQ_HANDLED;
	}

	if (irq != 0) /* don't increment on polled case */
		req->irq_count++;

	if (!bcom_buffer_done(lpbfifo.bcom_cur_task)) {
		spin_unlock_irqrestore(&lpbfifo.lock, flags);

		req->buffer_not_done_cnt++;
		if ((req->buffer_not_done_cnt % 1000) == 0)
			pr_err("transfer stalled\n");

		return IRQ_HANDLED;
	}

	bcom_retrieve_buffer(lpbfifo.bcom_cur_task, &status, NULL);

	req->last_byte = ((u8 *)req->data)[req->size - 1];

	req->pos = status & 0x00ffffff;

	/* Mark the FIFO as idle */
	lpbfifo.req = NULL;

	/* Release the lock before calling out to the callback. */
	req->irq_ticks += mftb() - ts;
	spin_unlock_irqrestore(&lpbfifo.lock, flags);

	if (req->callback)
		req->callback(req);

	return IRQ_HANDLED;
}

/**
 * mpc52xx_lpbfifo_bcom_poll - Poll for DMA completion
 */
void mpc52xx_lpbfifo_poll(void)
{
	struct mpc52xx_lpbfifo_request *req = lpbfifo.req;
	int dma = !(req->flags & MPC52XX_LPBFIFO_FLAG_NO_DMA);
	int write = req->flags & MPC52XX_LPBFIFO_FLAG_WRITE;

	/*
	 * For more information, see comments on the "Fat Lady" 
	 */
	if (dma && write)
		mpc52xx_lpbfifo_irq(0, NULL);
	else 
		mpc52xx_lpbfifo_bcom_irq(0, NULL);
}
EXPORT_SYMBOL(mpc52xx_lpbfifo_poll);

/**
 * mpc52xx_lpbfifo_submit - Submit an LPB FIFO transfer request.
 * @req: Pointer to request structure
 */
int mpc52xx_lpbfifo_submit(struct mpc52xx_lpbfifo_request *req)
{
	unsigned long flags;

	if (!lpbfifo.regs)
		return -ENODEV;

	spin_lock_irqsave(&lpbfifo.lock, flags);

	/* If the req pointer is already set, then a transfer is in progress */
	if (lpbfifo.req) {
		spin_unlock_irqrestore(&lpbfifo.lock, flags);
		return -EBUSY;
	}

	/* Setup the transfer */
	lpbfifo.req = req;
	req->irq_count = 0;
	req->irq_ticks = 0;
	req->buffer_not_done_cnt = 0;
	req->pos = 0;

	mpc52xx_lpbfifo_kick(req);
	spin_unlock_irqrestore(&lpbfifo.lock, flags);
	return 0;
}
EXPORT_SYMBOL(mpc52xx_lpbfifo_submit);

int mpc52xx_lpbfifo_start_xfer(struct mpc52xx_lpbfifo_request *req)
{
	unsigned long flags;

	if (!lpbfifo.regs)
		return -ENODEV;

	spin_lock_irqsave(&lpbfifo.lock, flags);

	/*
	 * If the req pointer is already set and a transfer was
	 * started on submit, then this transfer is in progress
	 */
	if (lpbfifo.req && !lpbfifo.req->defer_xfer_start) {
		spin_unlock_irqrestore(&lpbfifo.lock, flags);
		return -EBUSY;
	}

	/*
	 * If the req was previously submitted but not
	 * started, start it now
	 */
	if (lpbfifo.req && lpbfifo.req == req &&
	    lpbfifo.req->defer_xfer_start) {
		out_8(lpbfifo.regs + LPBFIFO_REG_PACKET_SIZE, 0x01);
	}

	spin_unlock_irqrestore(&lpbfifo.lock, flags);
	return 0;
}
EXPORT_SYMBOL(mpc52xx_lpbfifo_start_xfer);

void mpc52xx_lpbfifo_abort(struct mpc52xx_lpbfifo_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&lpbfifo.lock, flags);
	if (lpbfifo.req == req) {
		/* Put it into reset and clear the state */
		bcom_gen_bd_rx_reset(lpbfifo.bcom_rx_task);
		bcom_gen_bd_tx_reset(lpbfifo.bcom_tx_task);
		out_be32(lpbfifo.regs + LPBFIFO_REG_ENABLE, 0x01010000);
		lpbfifo.req = NULL;
	}
	spin_unlock_irqrestore(&lpbfifo.lock, flags);
}
EXPORT_SYMBOL(mpc52xx_lpbfifo_abort);

static int mpc52xx_lpbfifo_probe(struct platform_device *op)
{
	struct resource res;
	int rc = -ENOMEM;

	if (lpbfifo.dev != NULL)
		return -ENOSPC;

	lpbfifo.irq = irq_of_parse_and_map(op->dev.of_node, 0);
	if (!lpbfifo.irq)
		return -ENODEV;

	if (of_address_to_resource(op->dev.of_node, 0, &res))
		return -ENODEV;
	lpbfifo.regs_phys = res.start;
	lpbfifo.regs = of_iomap(op->dev.of_node, 0);
	if (!lpbfifo.regs)
		return -ENOMEM;

	spin_lock_init(&lpbfifo.lock);

	/* Put FIFO into reset */
	out_be32(lpbfifo.regs + LPBFIFO_REG_ENABLE, 0x01010000);

	/* Register the interrupt handler */
	rc = request_irq(lpbfifo.irq, mpc52xx_lpbfifo_irq, 0,
			 "mpc52xx-lpbfifo", &lpbfifo);
	if (rc)
		goto err_irq;

	/* Request the Bestcomm receive (fifo --> memory) task and IRQ */
	lpbfifo.bcom_rx_task =
		bcom_gen_bd_rx_init(2, res.start + LPBFIFO_REG_FIFO_DATA,
				    BCOM_INITIATOR_SCLPC, BCOM_IPR_SCLPC,
				    16*1024*1024);
	if (!lpbfifo.bcom_rx_task)
		goto err_bcom_rx;

	rc = request_irq(bcom_get_task_irq(lpbfifo.bcom_rx_task),
			 mpc52xx_lpbfifo_bcom_irq, 0,
			 "mpc52xx-lpbfifo-rx", &lpbfifo);
	if (rc)
		goto err_bcom_rx_irq;

	lpbfifo.dma_irqs_enabled = 1;

	/* Request the Bestcomm transmit (memory --> fifo) task and IRQ */
	lpbfifo.bcom_tx_task =
		bcom_gen_bd_tx_init(2, res.start + LPBFIFO_REG_FIFO_DATA,
				    BCOM_INITIATOR_SCLPC, BCOM_IPR_SCLPC);
	if (!lpbfifo.bcom_tx_task)
		goto err_bcom_tx;

	lpbfifo.dev = &op->dev;
	return 0;

 err_bcom_tx:
	free_irq(bcom_get_task_irq(lpbfifo.bcom_rx_task), &lpbfifo);
 err_bcom_rx_irq:
	bcom_gen_bd_rx_release(lpbfifo.bcom_rx_task);
 err_bcom_rx:
	free_irq(lpbfifo.irq, &lpbfifo);
 err_irq:
	iounmap(lpbfifo.regs);
	lpbfifo.regs = NULL;

	dev_err(&op->dev, "mpc52xx_lpbfifo_probe() failed\n");
	return -ENODEV;
}


static int mpc52xx_lpbfifo_remove(struct platform_device *op)
{
	if (lpbfifo.dev != &op->dev)
		return 0;

	/* Put FIFO in reset */
	out_be32(lpbfifo.regs + LPBFIFO_REG_ENABLE, 0x01010000);

	/* Release the bestcomm transmit task */
	free_irq(bcom_get_task_irq(lpbfifo.bcom_tx_task), &lpbfifo);
	bcom_gen_bd_tx_release(lpbfifo.bcom_tx_task);
	
	/* Release the bestcomm receive task */
	free_irq(bcom_get_task_irq(lpbfifo.bcom_rx_task), &lpbfifo);
	bcom_gen_bd_rx_release(lpbfifo.bcom_rx_task);

	free_irq(lpbfifo.irq, &lpbfifo);
	iounmap(lpbfifo.regs);
	lpbfifo.regs = NULL;
	lpbfifo.dev = NULL;

	return 0;
}

static const struct of_device_id mpc52xx_lpbfifo_match[] = {
	{ .compatible = "fsl,mpc5200-lpbfifo", },
	{},
};
MODULE_DEVICE_TABLE(of, mpc52xx_lpbfifo_match);

static struct platform_driver mpc52xx_lpbfifo_driver = {
	.driver = {
		.name = "mpc52xx-lpbfifo",
		.of_match_table = mpc52xx_lpbfifo_match,
	},
	.probe = mpc52xx_lpbfifo_probe,
	.remove = mpc52xx_lpbfifo_remove,
};
module_platform_driver(mpc52xx_lpbfifo_driver);
