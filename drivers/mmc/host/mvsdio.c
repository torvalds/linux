/*
 * Marvell MMC/SD/SDIO driver
 *
 * Authors: Maen Suleiman, Nicolas Pitre
 * Copyright (C) 2008-2009 Marvell Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/mbus.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/mmc/host.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/pinctrl/consumer.h>

#include <asm/sizes.h>
#include <asm/unaligned.h>
#include <linux/platform_data/mmc-mvsdio.h>

#include "mvsdio.h"

#define DRIVER_NAME	"mvsdio"

static int maxfreq;
static int nodma;

struct mvsd_host {
	void __iomem *base;
	struct mmc_request *mrq;
	spinlock_t lock;
	unsigned int xfer_mode;
	unsigned int intr_en;
	unsigned int ctrl;
	unsigned int pio_size;
	void *pio_ptr;
	unsigned int sg_frags;
	unsigned int ns_per_clk;
	unsigned int clock;
	unsigned int base_clock;
	struct timer_list timer;
	struct mmc_host *mmc;
	struct device *dev;
	struct clk *clk;
};

#define mvsd_write(offs, val)	writel(val, iobase + (offs))
#define mvsd_read(offs)		readl(iobase + (offs))

static int mvsd_setup_data(struct mvsd_host *host, struct mmc_data *data)
{
	void __iomem *iobase = host->base;
	unsigned int tmout;
	int tmout_index;

	/*
	 * Hardware weirdness.  The FIFO_EMPTY bit of the HW_STATE
	 * register is sometimes not set before a while when some
	 * "unusual" data block sizes are used (such as with the SWITCH
	 * command), even despite the fact that the XFER_DONE interrupt
	 * was raised.  And if another data transfer starts before
	 * this bit comes to good sense (which eventually happens by
	 * itself) then the new transfer simply fails with a timeout.
	 */
	if (!(mvsd_read(MVSD_HW_STATE) & (1 << 13))) {
		unsigned long t = jiffies + HZ;
		unsigned int hw_state,  count = 0;
		do {
			hw_state = mvsd_read(MVSD_HW_STATE);
			if (time_after(jiffies, t)) {
				dev_warn(host->dev, "FIFO_EMPTY bit missing\n");
				break;
			}
			count++;
		} while (!(hw_state & (1 << 13)));
		dev_dbg(host->dev, "*** wait for FIFO_EMPTY bit "
				   "(hw=0x%04x, count=%d, jiffies=%ld)\n",
				   hw_state, count, jiffies - (t - HZ));
	}

	/* If timeout=0 then maximum timeout index is used. */
	tmout = DIV_ROUND_UP(data->timeout_ns, host->ns_per_clk);
	tmout += data->timeout_clks;
	tmout_index = fls(tmout - 1) - 12;
	if (tmout_index < 0)
		tmout_index = 0;
	if (tmout_index > MVSD_HOST_CTRL_TMOUT_MAX)
		tmout_index = MVSD_HOST_CTRL_TMOUT_MAX;

	dev_dbg(host->dev, "data %s at 0x%08x: blocks=%d blksz=%d tmout=%u (%d)\n",
		(data->flags & MMC_DATA_READ) ? "read" : "write",
		(u32)sg_virt(data->sg), data->blocks, data->blksz,
		tmout, tmout_index);

	host->ctrl &= ~MVSD_HOST_CTRL_TMOUT_MASK;
	host->ctrl |= MVSD_HOST_CTRL_TMOUT(tmout_index);
	mvsd_write(MVSD_HOST_CTRL, host->ctrl);
	mvsd_write(MVSD_BLK_COUNT, data->blocks);
	mvsd_write(MVSD_BLK_SIZE, data->blksz);

	if (nodma || (data->blksz | data->sg->offset) & 3 ||
	    ((!(data->flags & MMC_DATA_READ) && data->sg->offset & 0x3f))) {
		/*
		 * We cannot do DMA on a buffer which offset or size
		 * is not aligned on a 4-byte boundary.
		 *
		 * It also appears the host to card DMA can corrupt
		 * data when the buffer is not aligned on a 64 byte
		 * boundary.
		 */
		host->pio_size = data->blocks * data->blksz;
		host->pio_ptr = sg_virt(data->sg);
		if (!nodma)
			dev_dbg(host->dev, "fallback to PIO for data at 0x%p size %d\n",
				host->pio_ptr, host->pio_size);
		return 1;
	} else {
		dma_addr_t phys_addr;
		int dma_dir = (data->flags & MMC_DATA_READ) ?
			DMA_FROM_DEVICE : DMA_TO_DEVICE;
		host->sg_frags = dma_map_sg(mmc_dev(host->mmc), data->sg,
					    data->sg_len, dma_dir);
		phys_addr = sg_dma_address(data->sg);
		mvsd_write(MVSD_SYS_ADDR_LOW, (u32)phys_addr & 0xffff);
		mvsd_write(MVSD_SYS_ADDR_HI,  (u32)phys_addr >> 16);
		return 0;
	}
}

static void mvsd_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mvsd_host *host = mmc_priv(mmc);
	void __iomem *iobase = host->base;
	struct mmc_command *cmd = mrq->cmd;
	u32 cmdreg = 0, xfer = 0, intr = 0;
	unsigned long flags;

	BUG_ON(host->mrq != NULL);
	host->mrq = mrq;

	dev_dbg(host->dev, "cmd %d (hw state 0x%04x)\n",
		cmd->opcode, mvsd_read(MVSD_HW_STATE));

	cmdreg = MVSD_CMD_INDEX(cmd->opcode);

	if (cmd->flags & MMC_RSP_BUSY)
		cmdreg |= MVSD_CMD_RSP_48BUSY;
	else if (cmd->flags & MMC_RSP_136)
		cmdreg |= MVSD_CMD_RSP_136;
	else if (cmd->flags & MMC_RSP_PRESENT)
		cmdreg |= MVSD_CMD_RSP_48;
	else
		cmdreg |= MVSD_CMD_RSP_NONE;

	if (cmd->flags & MMC_RSP_CRC)
		cmdreg |= MVSD_CMD_CHECK_CMDCRC;

	if (cmd->flags & MMC_RSP_OPCODE)
		cmdreg |= MVSD_CMD_INDX_CHECK;

	if (cmd->flags & MMC_RSP_PRESENT) {
		cmdreg |= MVSD_UNEXPECTED_RESP;
		intr |= MVSD_NOR_UNEXP_RSP;
	}

	if (mrq->data) {
		struct mmc_data *data = mrq->data;
		int pio;

		cmdreg |= MVSD_CMD_DATA_PRESENT | MVSD_CMD_CHECK_DATACRC16;
		xfer |= MVSD_XFER_MODE_HW_WR_DATA_EN;
		if (data->flags & MMC_DATA_READ)
			xfer |= MVSD_XFER_MODE_TO_HOST;

		pio = mvsd_setup_data(host, data);
		if (pio) {
			xfer |= MVSD_XFER_MODE_PIO;
			/* PIO section of mvsd_irq has comments on those bits */
			if (data->flags & MMC_DATA_WRITE)
				intr |= MVSD_NOR_TX_AVAIL;
			else if (host->pio_size > 32)
				intr |= MVSD_NOR_RX_FIFO_8W;
			else
				intr |= MVSD_NOR_RX_READY;
		}

		if (data->stop) {
			struct mmc_command *stop = data->stop;
			u32 cmd12reg = 0;

			mvsd_write(MVSD_AUTOCMD12_ARG_LOW, stop->arg & 0xffff);
			mvsd_write(MVSD_AUTOCMD12_ARG_HI,  stop->arg >> 16);

			if (stop->flags & MMC_RSP_BUSY)
				cmd12reg |= MVSD_AUTOCMD12_BUSY;
			if (stop->flags & MMC_RSP_OPCODE)
				cmd12reg |= MVSD_AUTOCMD12_INDX_CHECK;
			cmd12reg |= MVSD_AUTOCMD12_INDEX(stop->opcode);
			mvsd_write(MVSD_AUTOCMD12_CMD, cmd12reg);

			xfer |= MVSD_XFER_MODE_AUTO_CMD12;
			intr |= MVSD_NOR_AUTOCMD12_DONE;
		} else {
			intr |= MVSD_NOR_XFER_DONE;
		}
	} else {
		intr |= MVSD_NOR_CMD_DONE;
	}

	mvsd_write(MVSD_ARG_LOW, cmd->arg & 0xffff);
	mvsd_write(MVSD_ARG_HI,  cmd->arg >> 16);

	spin_lock_irqsave(&host->lock, flags);

	host->xfer_mode &= MVSD_XFER_MODE_INT_CHK_EN;
	host->xfer_mode |= xfer;
	mvsd_write(MVSD_XFER_MODE, host->xfer_mode);

	mvsd_write(MVSD_NOR_INTR_STATUS, ~MVSD_NOR_CARD_INT);
	mvsd_write(MVSD_ERR_INTR_STATUS, 0xffff);
	mvsd_write(MVSD_CMD, cmdreg);

	host->intr_en &= MVSD_NOR_CARD_INT;
	host->intr_en |= intr | MVSD_NOR_ERROR;
	mvsd_write(MVSD_NOR_INTR_EN, host->intr_en);
	mvsd_write(MVSD_ERR_INTR_EN, 0xffff);

	mod_timer(&host->timer, jiffies + 5 * HZ);

	spin_unlock_irqrestore(&host->lock, flags);
}

static u32 mvsd_finish_cmd(struct mvsd_host *host, struct mmc_command *cmd,
			   u32 err_status)
{
	void __iomem *iobase = host->base;

	if (cmd->flags & MMC_RSP_136) {
		unsigned int response[8], i;
		for (i = 0; i < 8; i++)
			response[i] = mvsd_read(MVSD_RSP(i));
		cmd->resp[0] =		((response[0] & 0x03ff) << 22) |
					((response[1] & 0xffff) << 6) |
					((response[2] & 0xfc00) >> 10);
		cmd->resp[1] =		((response[2] & 0x03ff) << 22) |
					((response[3] & 0xffff) << 6) |
					((response[4] & 0xfc00) >> 10);
		cmd->resp[2] =		((response[4] & 0x03ff) << 22) |
					((response[5] & 0xffff) << 6) |
					((response[6] & 0xfc00) >> 10);
		cmd->resp[3] =		((response[6] & 0x03ff) << 22) |
					((response[7] & 0x3fff) << 8);
	} else if (cmd->flags & MMC_RSP_PRESENT) {
		unsigned int response[3], i;
		for (i = 0; i < 3; i++)
			response[i] = mvsd_read(MVSD_RSP(i));
		cmd->resp[0] =		((response[2] & 0x003f) << (8 - 8)) |
					((response[1] & 0xffff) << (14 - 8)) |
					((response[0] & 0x03ff) << (30 - 8));
		cmd->resp[1] =		((response[0] & 0xfc00) >> 10);
		cmd->resp[2] = 0;
		cmd->resp[3] = 0;
	}

	if (err_status & MVSD_ERR_CMD_TIMEOUT) {
		cmd->error = -ETIMEDOUT;
	} else if (err_status & (MVSD_ERR_CMD_CRC | MVSD_ERR_CMD_ENDBIT |
				 MVSD_ERR_CMD_INDEX | MVSD_ERR_CMD_STARTBIT)) {
		cmd->error = -EILSEQ;
	}
	err_status &= ~(MVSD_ERR_CMD_TIMEOUT | MVSD_ERR_CMD_CRC |
			MVSD_ERR_CMD_ENDBIT | MVSD_ERR_CMD_INDEX |
			MVSD_ERR_CMD_STARTBIT);

	return err_status;
}

static u32 mvsd_finish_data(struct mvsd_host *host, struct mmc_data *data,
			    u32 err_status)
{
	void __iomem *iobase = host->base;

	if (host->pio_ptr) {
		host->pio_ptr = NULL;
		host->pio_size = 0;
	} else {
		dma_unmap_sg(mmc_dev(host->mmc), data->sg, host->sg_frags,
			     (data->flags & MMC_DATA_READ) ?
				DMA_FROM_DEVICE : DMA_TO_DEVICE);
	}

	if (err_status & MVSD_ERR_DATA_TIMEOUT)
		data->error = -ETIMEDOUT;
	else if (err_status & (MVSD_ERR_DATA_CRC | MVSD_ERR_DATA_ENDBIT))
		data->error = -EILSEQ;
	else if (err_status & MVSD_ERR_XFER_SIZE)
		data->error = -EBADE;
	err_status &= ~(MVSD_ERR_DATA_TIMEOUT | MVSD_ERR_DATA_CRC |
			MVSD_ERR_DATA_ENDBIT | MVSD_ERR_XFER_SIZE);

	dev_dbg(host->dev, "data done: blocks_left=%d, bytes_left=%d\n",
		mvsd_read(MVSD_CURR_BLK_LEFT), mvsd_read(MVSD_CURR_BYTE_LEFT));
	data->bytes_xfered =
		(data->blocks - mvsd_read(MVSD_CURR_BLK_LEFT)) * data->blksz;
	/* We can't be sure about the last block when errors are detected */
	if (data->bytes_xfered && data->error)
		data->bytes_xfered -= data->blksz;

	/* Handle Auto cmd 12 response */
	if (data->stop) {
		unsigned int response[3], i;
		for (i = 0; i < 3; i++)
			response[i] = mvsd_read(MVSD_AUTO_RSP(i));
		data->stop->resp[0] =	((response[2] & 0x003f) << (8 - 8)) |
					((response[1] & 0xffff) << (14 - 8)) |
					((response[0] & 0x03ff) << (30 - 8));
		data->stop->resp[1] =	((response[0] & 0xfc00) >> 10);
		data->stop->resp[2] = 0;
		data->stop->resp[3] = 0;

		if (err_status & MVSD_ERR_AUTOCMD12) {
			u32 err_cmd12 = mvsd_read(MVSD_AUTOCMD12_ERR_STATUS);
			dev_dbg(host->dev, "c12err 0x%04x\n", err_cmd12);
			if (err_cmd12 & MVSD_AUTOCMD12_ERR_NOTEXE)
				data->stop->error = -ENOEXEC;
			else if (err_cmd12 & MVSD_AUTOCMD12_ERR_TIMEOUT)
				data->stop->error = -ETIMEDOUT;
			else if (err_cmd12)
				data->stop->error = -EILSEQ;
			err_status &= ~MVSD_ERR_AUTOCMD12;
		}
	}

	return err_status;
}

static irqreturn_t mvsd_irq(int irq, void *dev)
{
	struct mvsd_host *host = dev;
	void __iomem *iobase = host->base;
	u32 intr_status, intr_done_mask;
	int irq_handled = 0;

	intr_status = mvsd_read(MVSD_NOR_INTR_STATUS);
	dev_dbg(host->dev, "intr 0x%04x intr_en 0x%04x hw_state 0x%04x\n",
		intr_status, mvsd_read(MVSD_NOR_INTR_EN),
		mvsd_read(MVSD_HW_STATE));

	/*
	 * It looks like, SDIO IP can issue one late, spurious irq
	 * although all irqs should be disabled. To work around this,
	 * bail out early, if we didn't expect any irqs to occur.
	 */
	if (!mvsd_read(MVSD_NOR_INTR_EN) && !mvsd_read(MVSD_ERR_INTR_EN)) {
		dev_dbg(host->dev, "spurious irq detected intr 0x%04x intr_en 0x%04x erri 0x%04x erri_en 0x%04x\n",
			mvsd_read(MVSD_NOR_INTR_STATUS),
			mvsd_read(MVSD_NOR_INTR_EN),
			mvsd_read(MVSD_ERR_INTR_STATUS),
			mvsd_read(MVSD_ERR_INTR_EN));
		return IRQ_HANDLED;
	}

	spin_lock(&host->lock);

	/* PIO handling, if needed. Messy business... */
	if (host->pio_size &&
	    (intr_status & host->intr_en &
	     (MVSD_NOR_RX_READY | MVSD_NOR_RX_FIFO_8W))) {
		u16 *p = host->pio_ptr;
		int s = host->pio_size;
		while (s >= 32 && (intr_status & MVSD_NOR_RX_FIFO_8W)) {
			readsw(iobase + MVSD_FIFO, p, 16);
			p += 16;
			s -= 32;
			intr_status = mvsd_read(MVSD_NOR_INTR_STATUS);
		}
		/*
		 * Normally we'd use < 32 here, but the RX_FIFO_8W bit
		 * doesn't appear to assert when there is exactly 32 bytes
		 * (8 words) left to fetch in a transfer.
		 */
		if (s <= 32) {
			while (s >= 4 && (intr_status & MVSD_NOR_RX_READY)) {
				put_unaligned(mvsd_read(MVSD_FIFO), p++);
				put_unaligned(mvsd_read(MVSD_FIFO), p++);
				s -= 4;
				intr_status = mvsd_read(MVSD_NOR_INTR_STATUS);
			}
			if (s && s < 4 && (intr_status & MVSD_NOR_RX_READY)) {
				u16 val[2] = {0, 0};
				val[0] = mvsd_read(MVSD_FIFO);
				val[1] = mvsd_read(MVSD_FIFO);
				memcpy(p, ((void *)&val) + 4 - s, s);
				s = 0;
				intr_status = mvsd_read(MVSD_NOR_INTR_STATUS);
			}
			if (s == 0) {
				host->intr_en &=
				     ~(MVSD_NOR_RX_READY | MVSD_NOR_RX_FIFO_8W);
				mvsd_write(MVSD_NOR_INTR_EN, host->intr_en);
			} else if (host->intr_en & MVSD_NOR_RX_FIFO_8W) {
				host->intr_en &= ~MVSD_NOR_RX_FIFO_8W;
				host->intr_en |= MVSD_NOR_RX_READY;
				mvsd_write(MVSD_NOR_INTR_EN, host->intr_en);
			}
		}
		dev_dbg(host->dev, "pio %d intr 0x%04x hw_state 0x%04x\n",
			s, intr_status, mvsd_read(MVSD_HW_STATE));
		host->pio_ptr = p;
		host->pio_size = s;
		irq_handled = 1;
	} else if (host->pio_size &&
		   (intr_status & host->intr_en &
		    (MVSD_NOR_TX_AVAIL | MVSD_NOR_TX_FIFO_8W))) {
		u16 *p = host->pio_ptr;
		int s = host->pio_size;
		/*
		 * The TX_FIFO_8W bit is unreliable. When set, bursting
		 * 16 halfwords all at once in the FIFO drops data. Actually
		 * TX_AVAIL does go off after only one word is pushed even if
		 * TX_FIFO_8W remains set.
		 */
		while (s >= 4 && (intr_status & MVSD_NOR_TX_AVAIL)) {
			mvsd_write(MVSD_FIFO, get_unaligned(p++));
			mvsd_write(MVSD_FIFO, get_unaligned(p++));
			s -= 4;
			intr_status = mvsd_read(MVSD_NOR_INTR_STATUS);
		}
		if (s < 4) {
			if (s && (intr_status & MVSD_NOR_TX_AVAIL)) {
				u16 val[2] = {0, 0};
				memcpy(((void *)&val) + 4 - s, p, s);
				mvsd_write(MVSD_FIFO, val[0]);
				mvsd_write(MVSD_FIFO, val[1]);
				s = 0;
				intr_status = mvsd_read(MVSD_NOR_INTR_STATUS);
			}
			if (s == 0) {
				host->intr_en &=
				     ~(MVSD_NOR_TX_AVAIL | MVSD_NOR_TX_FIFO_8W);
				mvsd_write(MVSD_NOR_INTR_EN, host->intr_en);
			}
		}
		dev_dbg(host->dev, "pio %d intr 0x%04x hw_state 0x%04x\n",
			s, intr_status, mvsd_read(MVSD_HW_STATE));
		host->pio_ptr = p;
		host->pio_size = s;
		irq_handled = 1;
	}

	mvsd_write(MVSD_NOR_INTR_STATUS, intr_status);

	intr_done_mask = MVSD_NOR_CARD_INT | MVSD_NOR_RX_READY |
			 MVSD_NOR_RX_FIFO_8W | MVSD_NOR_TX_FIFO_8W;
	if (intr_status & host->intr_en & ~intr_done_mask) {
		struct mmc_request *mrq = host->mrq;
		struct mmc_command *cmd = mrq->cmd;
		u32 err_status = 0;

		del_timer(&host->timer);
		host->mrq = NULL;

		host->intr_en &= MVSD_NOR_CARD_INT;
		mvsd_write(MVSD_NOR_INTR_EN, host->intr_en);
		mvsd_write(MVSD_ERR_INTR_EN, 0);

		spin_unlock(&host->lock);

		if (intr_status & MVSD_NOR_UNEXP_RSP) {
			cmd->error = -EPROTO;
		} else if (intr_status & MVSD_NOR_ERROR) {
			err_status = mvsd_read(MVSD_ERR_INTR_STATUS);
			dev_dbg(host->dev, "err 0x%04x\n", err_status);
		}

		err_status = mvsd_finish_cmd(host, cmd, err_status);
		if (mrq->data)
			err_status = mvsd_finish_data(host, mrq->data, err_status);
		if (err_status) {
			dev_err(host->dev, "unhandled error status %#04x\n",
				err_status);
			cmd->error = -ENOMSG;
		}

		mmc_request_done(host->mmc, mrq);
		irq_handled = 1;
	} else
		spin_unlock(&host->lock);

	if (intr_status & MVSD_NOR_CARD_INT) {
		mmc_signal_sdio_irq(host->mmc);
		irq_handled = 1;
	}

	if (irq_handled)
		return IRQ_HANDLED;

	dev_err(host->dev, "unhandled interrupt status=0x%04x en=0x%04x pio=%d\n",
		intr_status, host->intr_en, host->pio_size);
	return IRQ_NONE;
}

static void mvsd_timeout_timer(unsigned long data)
{
	struct mvsd_host *host = (struct mvsd_host *)data;
	void __iomem *iobase = host->base;
	struct mmc_request *mrq;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	mrq = host->mrq;
	if (mrq) {
		dev_err(host->dev, "Timeout waiting for hardware interrupt.\n");
		dev_err(host->dev, "hw_state=0x%04x, intr_status=0x%04x intr_en=0x%04x\n",
			mvsd_read(MVSD_HW_STATE),
			mvsd_read(MVSD_NOR_INTR_STATUS),
			mvsd_read(MVSD_NOR_INTR_EN));

		host->mrq = NULL;

		mvsd_write(MVSD_SW_RESET, MVSD_SW_RESET_NOW);

		host->xfer_mode &= MVSD_XFER_MODE_INT_CHK_EN;
		mvsd_write(MVSD_XFER_MODE, host->xfer_mode);

		host->intr_en &= MVSD_NOR_CARD_INT;
		mvsd_write(MVSD_NOR_INTR_EN, host->intr_en);
		mvsd_write(MVSD_ERR_INTR_EN, 0);
		mvsd_write(MVSD_ERR_INTR_STATUS, 0xffff);

		mrq->cmd->error = -ETIMEDOUT;
		mvsd_finish_cmd(host, mrq->cmd, 0);
		if (mrq->data) {
			mrq->data->error = -ETIMEDOUT;
			mvsd_finish_data(host, mrq->data, 0);
		}
	}
	spin_unlock_irqrestore(&host->lock, flags);

	if (mrq)
		mmc_request_done(host->mmc, mrq);
}

static void mvsd_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct mvsd_host *host = mmc_priv(mmc);
	void __iomem *iobase = host->base;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	if (enable) {
		host->xfer_mode |= MVSD_XFER_MODE_INT_CHK_EN;
		host->intr_en |= MVSD_NOR_CARD_INT;
	} else {
		host->xfer_mode &= ~MVSD_XFER_MODE_INT_CHK_EN;
		host->intr_en &= ~MVSD_NOR_CARD_INT;
	}
	mvsd_write(MVSD_XFER_MODE, host->xfer_mode);
	mvsd_write(MVSD_NOR_INTR_EN, host->intr_en);
	spin_unlock_irqrestore(&host->lock, flags);
}

static void mvsd_power_up(struct mvsd_host *host)
{
	void __iomem *iobase = host->base;
	dev_dbg(host->dev, "power up\n");
	mvsd_write(MVSD_NOR_INTR_EN, 0);
	mvsd_write(MVSD_ERR_INTR_EN, 0);
	mvsd_write(MVSD_SW_RESET, MVSD_SW_RESET_NOW);
	mvsd_write(MVSD_XFER_MODE, 0);
	mvsd_write(MVSD_NOR_STATUS_EN, 0xffff);
	mvsd_write(MVSD_ERR_STATUS_EN, 0xffff);
	mvsd_write(MVSD_NOR_INTR_STATUS, 0xffff);
	mvsd_write(MVSD_ERR_INTR_STATUS, 0xffff);
}

static void mvsd_power_down(struct mvsd_host *host)
{
	void __iomem *iobase = host->base;
	dev_dbg(host->dev, "power down\n");
	mvsd_write(MVSD_NOR_INTR_EN, 0);
	mvsd_write(MVSD_ERR_INTR_EN, 0);
	mvsd_write(MVSD_SW_RESET, MVSD_SW_RESET_NOW);
	mvsd_write(MVSD_XFER_MODE, MVSD_XFER_MODE_STOP_CLK);
	mvsd_write(MVSD_NOR_STATUS_EN, 0);
	mvsd_write(MVSD_ERR_STATUS_EN, 0);
	mvsd_write(MVSD_NOR_INTR_STATUS, 0xffff);
	mvsd_write(MVSD_ERR_INTR_STATUS, 0xffff);
}

static void mvsd_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mvsd_host *host = mmc_priv(mmc);
	void __iomem *iobase = host->base;
	u32 ctrl_reg = 0;

	if (ios->power_mode == MMC_POWER_UP)
		mvsd_power_up(host);

	if (ios->clock == 0) {
		mvsd_write(MVSD_XFER_MODE, MVSD_XFER_MODE_STOP_CLK);
		mvsd_write(MVSD_CLK_DIV, MVSD_BASE_DIV_MAX);
		host->clock = 0;
		dev_dbg(host->dev, "clock off\n");
	} else if (ios->clock != host->clock) {
		u32 m = DIV_ROUND_UP(host->base_clock, ios->clock) - 1;
		if (m > MVSD_BASE_DIV_MAX)
			m = MVSD_BASE_DIV_MAX;
		mvsd_write(MVSD_CLK_DIV, m);
		host->clock = ios->clock;
		host->ns_per_clk = 1000000000 / (host->base_clock / (m+1));
		dev_dbg(host->dev, "clock=%d (%d), div=0x%04x\n",
			ios->clock, host->base_clock / (m+1), m);
	}

	/* default transfer mode */
	ctrl_reg |= MVSD_HOST_CTRL_BIG_ENDIAN;
	ctrl_reg &= ~MVSD_HOST_CTRL_LSB_FIRST;

	/* default to maximum timeout */
	ctrl_reg |= MVSD_HOST_CTRL_TMOUT_MASK;
	ctrl_reg |= MVSD_HOST_CTRL_TMOUT_EN;

	if (ios->bus_mode == MMC_BUSMODE_PUSHPULL)
		ctrl_reg |= MVSD_HOST_CTRL_PUSH_PULL_EN;

	if (ios->bus_width == MMC_BUS_WIDTH_4)
		ctrl_reg |= MVSD_HOST_CTRL_DATA_WIDTH_4_BITS;

	/*
	 * The HI_SPEED_EN bit is causing trouble with many (but not all)
	 * high speed SD, SDHC and SDIO cards.  Not enabling that bit
	 * makes all cards work.  So let's just ignore that bit for now
	 * and revisit this issue if problems for not enabling this bit
	 * are ever reported.
	 */
#if 0
	if (ios->timing == MMC_TIMING_MMC_HS ||
	    ios->timing == MMC_TIMING_SD_HS)
		ctrl_reg |= MVSD_HOST_CTRL_HI_SPEED_EN;
#endif

	host->ctrl = ctrl_reg;
	mvsd_write(MVSD_HOST_CTRL, ctrl_reg);
	dev_dbg(host->dev, "ctrl 0x%04x: %s %s %s\n", ctrl_reg,
		(ctrl_reg & MVSD_HOST_CTRL_PUSH_PULL_EN) ?
			"push-pull" : "open-drain",
		(ctrl_reg & MVSD_HOST_CTRL_DATA_WIDTH_4_BITS) ?
			"4bit-width" : "1bit-width",
		(ctrl_reg & MVSD_HOST_CTRL_HI_SPEED_EN) ?
			"high-speed" : "");

	if (ios->power_mode == MMC_POWER_OFF)
		mvsd_power_down(host);
}

static const struct mmc_host_ops mvsd_ops = {
	.request		= mvsd_request,
	.get_ro			= mmc_gpio_get_ro,
	.set_ios		= mvsd_set_ios,
	.enable_sdio_irq	= mvsd_enable_sdio_irq,
};

static void
mv_conf_mbus_windows(struct mvsd_host *host,
		     const struct mbus_dram_target_info *dram)
{
	void __iomem *iobase = host->base;
	int i;

	for (i = 0; i < 4; i++) {
		writel(0, iobase + MVSD_WINDOW_CTRL(i));
		writel(0, iobase + MVSD_WINDOW_BASE(i));
	}

	for (i = 0; i < dram->num_cs; i++) {
		const struct mbus_dram_window *cs = dram->cs + i;
		writel(((cs->size - 1) & 0xffff0000) |
		       (cs->mbus_attr << 8) |
		       (dram->mbus_dram_target_id << 4) | 1,
		       iobase + MVSD_WINDOW_CTRL(i));
		writel(cs->base, iobase + MVSD_WINDOW_BASE(i));
	}
}

static int mvsd_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mmc_host *mmc = NULL;
	struct mvsd_host *host = NULL;
	const struct mbus_dram_target_info *dram;
	struct resource *r;
	int ret, irq;
	struct pinctrl *pinctrl;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!r || irq < 0)
		return -ENXIO;

	mmc = mmc_alloc_host(sizeof(struct mvsd_host), &pdev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto out;
	}

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->dev = &pdev->dev;

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl))
		dev_warn(&pdev->dev, "no pins associated\n");

	/*
	 * Some non-DT platforms do not pass a clock, and the clock
	 * frequency is passed through platform_data. On DT platforms,
	 * a clock must always be passed, even if there is no gatable
	 * clock associated to the SDIO interface (it can simply be a
	 * fixed rate clock).
	 */
	host->clk = devm_clk_get(&pdev->dev, NULL);
	if (!IS_ERR(host->clk))
		clk_prepare_enable(host->clk);

	mmc->ops = &mvsd_ops;

	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	mmc->f_min = DIV_ROUND_UP(host->base_clock, MVSD_BASE_DIV_MAX);
	mmc->f_max = MVSD_CLOCKRATE_MAX;

	mmc->max_blk_size = 2048;
	mmc->max_blk_count = 65535;

	mmc->max_segs = 1;
	mmc->max_seg_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;

	if (np) {
		if (IS_ERR(host->clk)) {
			dev_err(&pdev->dev, "DT platforms must have a clock associated\n");
			ret = -EINVAL;
			goto out;
		}

		host->base_clock = clk_get_rate(host->clk) / 2;
		ret = mmc_of_parse(mmc);
		if (ret < 0)
			goto out;
	} else {
		const struct mvsdio_platform_data *mvsd_data;

		mvsd_data = pdev->dev.platform_data;
		if (!mvsd_data) {
			ret = -ENXIO;
			goto out;
		}
		mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_SDIO_IRQ |
			    MMC_CAP_SD_HIGHSPEED | MMC_CAP_MMC_HIGHSPEED;
		host->base_clock = mvsd_data->clock / 2;
		/* GPIO 0 regarded as invalid for backward compatibility */
		if (mvsd_data->gpio_card_detect &&
		    gpio_is_valid(mvsd_data->gpio_card_detect)) {
			ret = mmc_gpio_request_cd(mmc,
						  mvsd_data->gpio_card_detect,
						  0);
			if (ret)
				goto out;
		} else {
			mmc->caps |= MMC_CAP_NEEDS_POLL;
		}

		if (mvsd_data->gpio_write_protect &&
		    gpio_is_valid(mvsd_data->gpio_write_protect))
			mmc_gpio_request_ro(mmc, mvsd_data->gpio_write_protect);
	}

	if (maxfreq)
		mmc->f_max = maxfreq;

	spin_lock_init(&host->lock);

	host->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(host->base)) {
		ret = PTR_ERR(host->base);
		goto out;
	}

	/* (Re-)program MBUS remapping windows if we are asked to. */
	dram = mv_mbus_dram_info();
	if (dram)
		mv_conf_mbus_windows(host, dram);

	mvsd_power_down(host);

	ret = devm_request_irq(&pdev->dev, irq, mvsd_irq, 0, DRIVER_NAME, host);
	if (ret) {
		dev_err(&pdev->dev, "cannot assign irq %d\n", irq);
		goto out;
	}

	setup_timer(&host->timer, mvsd_timeout_timer, (unsigned long)host);
	platform_set_drvdata(pdev, mmc);
	ret = mmc_add_host(mmc);
	if (ret)
		goto out;

	if (!(mmc->caps & MMC_CAP_NEEDS_POLL))
		dev_dbg(&pdev->dev, "using GPIO for card detection\n");
	else
		dev_dbg(&pdev->dev, "lacking card detect (fall back to polling)\n");

	return 0;

out:
	if (mmc) {
		mmc_gpio_free_cd(mmc);
		mmc_gpio_free_ro(mmc);
		if (!IS_ERR(host->clk))
			clk_disable_unprepare(host->clk);
		mmc_free_host(mmc);
	}

	return ret;
}

static int mvsd_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);

	struct mvsd_host *host = mmc_priv(mmc);

	mmc_gpio_free_cd(mmc);
	mmc_gpio_free_ro(mmc);
	mmc_remove_host(mmc);
	del_timer_sync(&host->timer);
	mvsd_power_down(host);

	if (!IS_ERR(host->clk))
		clk_disable_unprepare(host->clk);
	mmc_free_host(mmc);

	return 0;
}

static const struct of_device_id mvsdio_dt_ids[] = {
	{ .compatible = "marvell,orion-sdio" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mvsdio_dt_ids);

static struct platform_driver mvsd_driver = {
	.probe		= mvsd_probe,
	.remove		= mvsd_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table = mvsdio_dt_ids,
	},
};

module_platform_driver(mvsd_driver);

/* maximum card clock frequency (default 50MHz) */
module_param(maxfreq, int, 0);

/* force PIO transfers all the time */
module_param(nodma, int, 0);

MODULE_AUTHOR("Maen Suleiman, Nicolas Pitre");
MODULE_DESCRIPTION("Marvell MMC,SD,SDIO Host Controller driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mvsdio");
