/*
 *  tifm_sd.c - TI FlashMedia driver
 *
 *  Copyright (C) 2006 Alex Dubov <oakad@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */


#include <linux/tifm.h>
#include <linux/mmc/protocol.h>
#include <linux/mmc/host.h>
#include <linux/highmem.h>
#include <asm/io.h>

#define DRIVER_NAME "tifm_sd"
#define DRIVER_VERSION "0.8"

static int no_dma = 0;
static int fixed_timeout = 0;
module_param(no_dma, bool, 0644);
module_param(fixed_timeout, bool, 0644);

/* Constants here are mostly from OMAP5912 datasheet */
#define TIFM_MMCSD_RESET      0x0002
#define TIFM_MMCSD_CLKMASK    0x03ff
#define TIFM_MMCSD_POWER      0x0800
#define TIFM_MMCSD_4BBUS      0x8000
#define TIFM_MMCSD_RXDE       0x8000   /* rx dma enable */
#define TIFM_MMCSD_TXDE       0x0080   /* tx dma enable */
#define TIFM_MMCSD_BUFINT     0x0c00   /* set bits: AE, AF */
#define TIFM_MMCSD_DPE        0x0020   /* data timeout counted in kilocycles */
#define TIFM_MMCSD_INAB       0x0080   /* abort / initialize command */
#define TIFM_MMCSD_READ       0x8000

#define TIFM_MMCSD_DATAMASK   0x401d   /* set bits: CERR, EOFB, BRS, CB, EOC */
#define TIFM_MMCSD_ERRMASK    0x01e0   /* set bits: CCRC, CTO, DCRC, DTO */
#define TIFM_MMCSD_EOC        0x0001   /* end of command phase  */
#define TIFM_MMCSD_CB         0x0004   /* card enter busy state */
#define TIFM_MMCSD_BRS        0x0008   /* block received/sent   */
#define TIFM_MMCSD_EOFB       0x0010   /* card exit busy state  */
#define TIFM_MMCSD_DTO        0x0020   /* data time-out         */
#define TIFM_MMCSD_DCRC       0x0040   /* data crc error        */
#define TIFM_MMCSD_CTO        0x0080   /* command time-out      */
#define TIFM_MMCSD_CCRC       0x0100   /* command crc error     */
#define TIFM_MMCSD_AF         0x0400   /* fifo almost full      */
#define TIFM_MMCSD_AE         0x0800   /* fifo almost empty     */
#define TIFM_MMCSD_CERR       0x4000   /* card status error     */

#define TIFM_MMCSD_ODTO       0x0040   /* open drain / extended timeout */
#define TIFM_MMCSD_CARD_RO    0x0200   /* card is read-only     */

#define TIFM_MMCSD_FIFO_SIZE  0x0020

#define TIFM_MMCSD_RSP_R0     0x0000
#define TIFM_MMCSD_RSP_R1     0x0100
#define TIFM_MMCSD_RSP_R2     0x0200
#define TIFM_MMCSD_RSP_R3     0x0300
#define TIFM_MMCSD_RSP_R4     0x0400
#define TIFM_MMCSD_RSP_R5     0x0500
#define TIFM_MMCSD_RSP_R6     0x0600

#define TIFM_MMCSD_RSP_BUSY   0x0800

#define TIFM_MMCSD_CMD_BC     0x0000
#define TIFM_MMCSD_CMD_BCR    0x1000
#define TIFM_MMCSD_CMD_AC     0x2000
#define TIFM_MMCSD_CMD_ADTC   0x3000

enum {
	CMD_READY    = 0x0001,
	FIFO_READY   = 0x0002,
	BRS_READY    = 0x0004,
	SCMD_ACTIVE  = 0x0008,
	SCMD_READY   = 0x0010,
	CARD_BUSY    = 0x0020,
	DATA_CARRY   = 0x0040
};

struct tifm_sd {
	struct tifm_dev     *dev;

	unsigned short      eject:1,
			    open_drain:1,
			    no_dma:1;
	unsigned short      cmd_flags;

	unsigned int        clk_freq;
	unsigned int        clk_div;
	unsigned long       timeout_jiffies;

	struct tasklet_struct finish_tasklet;
	struct timer_list     timer;
	struct mmc_request    *req;

	size_t                written_blocks;
	size_t                buffer_size;
	size_t                buffer_pos;

};

static char* tifm_sd_data_buffer(struct mmc_data *data)
{
	return page_address(data->sg->page) + data->sg->offset;
}

static int tifm_sd_transfer_data(struct tifm_dev *sock, struct tifm_sd *host,
				 unsigned int host_status)
{
	struct mmc_command *cmd = host->req->cmd;
	unsigned int t_val = 0, cnt = 0;
	char *buffer;

	if (host_status & TIFM_MMCSD_BRS) {
		/* in non-dma rx mode BRS fires when fifo is still not empty */
		if (host->no_dma && (cmd->data->flags & MMC_DATA_READ)) {
			buffer = tifm_sd_data_buffer(host->req->data);
			while (host->buffer_size > host->buffer_pos) {
				t_val = readl(sock->addr + SOCK_MMCSD_DATA);
				buffer[host->buffer_pos++] = t_val & 0xff;
				buffer[host->buffer_pos++] =
							(t_val >> 8) & 0xff;
			}
		}
		return 1;
	} else if (host->no_dma) {
		buffer = tifm_sd_data_buffer(host->req->data);
		if ((cmd->data->flags & MMC_DATA_READ) &&
				(host_status & TIFM_MMCSD_AF)) {
			for (cnt = 0; cnt < TIFM_MMCSD_FIFO_SIZE; cnt++) {
				t_val = readl(sock->addr + SOCK_MMCSD_DATA);
				if (host->buffer_size > host->buffer_pos) {
					buffer[host->buffer_pos++] =
							t_val & 0xff;
					buffer[host->buffer_pos++] =
							(t_val >> 8) & 0xff;
				}
			}
		} else if ((cmd->data->flags & MMC_DATA_WRITE)
			   && (host_status & TIFM_MMCSD_AE)) {
			for (cnt = 0; cnt < TIFM_MMCSD_FIFO_SIZE; cnt++) {
				if (host->buffer_size > host->buffer_pos) {
					t_val = buffer[host->buffer_pos++]
						& 0x00ff;
					t_val |= ((buffer[host->buffer_pos++])
						  << 8) & 0xff00;
					writel(t_val,
					       sock->addr + SOCK_MMCSD_DATA);
				}
			}
		}
	}
	return 0;
}

static unsigned int tifm_sd_op_flags(struct mmc_command *cmd)
{
	unsigned int rc = 0;

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		rc |= TIFM_MMCSD_RSP_R0;
		break;
	case MMC_RSP_R1B:
		rc |= TIFM_MMCSD_RSP_BUSY; // deliberate fall-through
	case MMC_RSP_R1:
		rc |= TIFM_MMCSD_RSP_R1;
		break;
	case MMC_RSP_R2:
		rc |= TIFM_MMCSD_RSP_R2;
		break;
	case MMC_RSP_R3:
		rc |= TIFM_MMCSD_RSP_R3;
		break;
	default:
		BUG();
	}

	switch (mmc_cmd_type(cmd)) {
	case MMC_CMD_BC:
		rc |= TIFM_MMCSD_CMD_BC;
		break;
	case MMC_CMD_BCR:
		rc |= TIFM_MMCSD_CMD_BCR;
		break;
	case MMC_CMD_AC:
		rc |= TIFM_MMCSD_CMD_AC;
		break;
	case MMC_CMD_ADTC:
		rc |= TIFM_MMCSD_CMD_ADTC;
		break;
	default:
		BUG();
	}
	return rc;
}

static void tifm_sd_exec(struct tifm_sd *host, struct mmc_command *cmd)
{
	struct tifm_dev *sock = host->dev;
	unsigned int cmd_mask = tifm_sd_op_flags(cmd);

	if (host->open_drain)
		cmd_mask |= TIFM_MMCSD_ODTO;

	if (cmd->data && (cmd->data->flags & MMC_DATA_READ))
		cmd_mask |= TIFM_MMCSD_READ;

	dev_dbg(&sock->dev, "executing opcode 0x%x, arg: 0x%x, mask: 0x%x\n",
		cmd->opcode, cmd->arg, cmd_mask);

	writel((cmd->arg >> 16) & 0xffff, sock->addr + SOCK_MMCSD_ARG_HIGH);
	writel(cmd->arg & 0xffff, sock->addr + SOCK_MMCSD_ARG_LOW);
	writel(cmd->opcode | cmd_mask, sock->addr + SOCK_MMCSD_COMMAND);
}

static void tifm_sd_fetch_resp(struct mmc_command *cmd, struct tifm_dev *sock)
{
	cmd->resp[0] = (readl(sock->addr + SOCK_MMCSD_RESPONSE + 0x1c) << 16)
		       | readl(sock->addr + SOCK_MMCSD_RESPONSE + 0x18);
	cmd->resp[1] = (readl(sock->addr + SOCK_MMCSD_RESPONSE + 0x14) << 16)
		       | readl(sock->addr + SOCK_MMCSD_RESPONSE + 0x10);
	cmd->resp[2] = (readl(sock->addr + SOCK_MMCSD_RESPONSE + 0x0c) << 16)
		       | readl(sock->addr + SOCK_MMCSD_RESPONSE + 0x08);
	cmd->resp[3] = (readl(sock->addr + SOCK_MMCSD_RESPONSE + 0x04) << 16)
		       | readl(sock->addr + SOCK_MMCSD_RESPONSE + 0x00);
}

static void tifm_sd_check_status(struct tifm_sd *host)
{
	struct tifm_dev *sock = host->dev;
	struct mmc_command *cmd = host->req->cmd;

	if (cmd->error != MMC_ERR_NONE)
		goto finish_request;

	if (!(host->cmd_flags & CMD_READY))
		return;

	if (cmd->data) {
		if (cmd->data->error != MMC_ERR_NONE) {
			if ((host->cmd_flags & SCMD_ACTIVE)
			    && !(host->cmd_flags & SCMD_READY))
				return;

			goto finish_request;
		}

		if (!(host->cmd_flags & BRS_READY))
			return;

		if (!(host->no_dma || (host->cmd_flags & FIFO_READY)))
			return;

		if (cmd->data->flags & MMC_DATA_WRITE) {
			if (host->req->stop) {
				if (!(host->cmd_flags & SCMD_ACTIVE)) {
					host->cmd_flags |= SCMD_ACTIVE;
					writel(TIFM_MMCSD_EOFB
					       | readl(sock->addr
						       + SOCK_MMCSD_INT_ENABLE),
					       sock->addr
					       + SOCK_MMCSD_INT_ENABLE);
					tifm_sd_exec(host, host->req->stop);
					return;
				} else {
					if (!(host->cmd_flags & SCMD_READY)
					    || (host->cmd_flags & CARD_BUSY))
						return;
					writel((~TIFM_MMCSD_EOFB)
					       & readl(sock->addr
						       + SOCK_MMCSD_INT_ENABLE),
					       sock->addr
					       + SOCK_MMCSD_INT_ENABLE);
				}
			} else {
				if (host->cmd_flags & CARD_BUSY)
					return;
				writel((~TIFM_MMCSD_EOFB)
				       & readl(sock->addr
					       + SOCK_MMCSD_INT_ENABLE),
				       sock->addr + SOCK_MMCSD_INT_ENABLE);
			}
		} else {
			if (host->req->stop) {
				if (!(host->cmd_flags & SCMD_ACTIVE)) {
					host->cmd_flags |= SCMD_ACTIVE;
					tifm_sd_exec(host, host->req->stop);
					return;
				} else {
					if (!(host->cmd_flags & SCMD_READY))
						return;
				}
			}
		}
	}
finish_request:
	tasklet_schedule(&host->finish_tasklet);
}

/* Called from interrupt handler */
static void tifm_sd_data_event(struct tifm_dev *sock)
{
	struct tifm_sd *host;
	unsigned int fifo_status = 0;
	struct mmc_data *r_data = NULL;

	spin_lock(&sock->lock);
	host = mmc_priv((struct mmc_host*)tifm_get_drvdata(sock));
	fifo_status = readl(sock->addr + SOCK_DMA_FIFO_STATUS);
	dev_dbg(&sock->dev, "data event: fifo_status %x, flags %x\n",
		fifo_status, host->cmd_flags);

	if (host->req) {
		r_data = host->req->cmd->data;

		if (r_data && (fifo_status & TIFM_FIFO_READY)) {
			host->cmd_flags |= FIFO_READY;
			tifm_sd_check_status(host);
		}
	}

	writel(fifo_status, sock->addr + SOCK_DMA_FIFO_STATUS);
	spin_unlock(&sock->lock);
}

/* Called from interrupt handler */
static void tifm_sd_card_event(struct tifm_dev *sock)
{
	struct tifm_sd *host;
	unsigned int host_status = 0;
	int cmd_error = MMC_ERR_NONE;
	struct mmc_command *cmd = NULL;
	unsigned long flags;

	spin_lock(&sock->lock);
	host = mmc_priv((struct mmc_host*)tifm_get_drvdata(sock));
	host_status = readl(sock->addr + SOCK_MMCSD_STATUS);
	dev_dbg(&sock->dev, "host event: host_status %x, flags %x\n",
		host_status, host->cmd_flags);

	if (host->req) {
		cmd = host->req->cmd;

		if (host_status & TIFM_MMCSD_ERRMASK) {
			writel(host_status & TIFM_MMCSD_ERRMASK,
			       sock->addr + SOCK_MMCSD_STATUS);
			if (host_status & TIFM_MMCSD_CTO)
				cmd_error = MMC_ERR_TIMEOUT;
			else if (host_status & TIFM_MMCSD_CCRC)
				cmd_error = MMC_ERR_BADCRC;

			if (cmd->data) {
				if (host_status & TIFM_MMCSD_DTO)
					cmd->data->error = MMC_ERR_TIMEOUT;
				else if (host_status & TIFM_MMCSD_DCRC)
					cmd->data->error = MMC_ERR_BADCRC;
			}

			writel(TIFM_FIFO_INT_SETALL,
			       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_CLEAR);
			writel(TIFM_DMA_RESET, sock->addr + SOCK_DMA_CONTROL);

			if (host->req->stop) {
				if (host->cmd_flags & SCMD_ACTIVE) {
					host->req->stop->error = cmd_error;
					host->cmd_flags |= SCMD_READY;
				} else {
					cmd->error = cmd_error;
					host->cmd_flags |= SCMD_ACTIVE;
					tifm_sd_exec(host, host->req->stop);
					goto done;
				}
			} else
				cmd->error = cmd_error;
		} else {
			if (host_status & (TIFM_MMCSD_EOC | TIFM_MMCSD_CERR)) {
				if (!(host->cmd_flags & CMD_READY)) {
					host->cmd_flags |= CMD_READY;
					tifm_sd_fetch_resp(cmd, sock);
				} else if (host->cmd_flags & SCMD_ACTIVE) {
					host->cmd_flags |= SCMD_READY;
					tifm_sd_fetch_resp(host->req->stop,
							   sock);
				}
			}
			if (host_status & TIFM_MMCSD_BRS)
				host->cmd_flags |= BRS_READY;
		}

		if (host->no_dma && cmd->data) {
			if (host_status & TIFM_MMCSD_AE)
				writel(host_status & TIFM_MMCSD_AE,
				       sock->addr + SOCK_MMCSD_STATUS);

			if (host_status & (TIFM_MMCSD_AE | TIFM_MMCSD_AF
					   | TIFM_MMCSD_BRS)) {
				local_irq_save(flags);
				tifm_sd_transfer_data(sock, host, host_status);
				local_irq_restore(flags);
				host_status &= ~TIFM_MMCSD_AE;
			}
		}

		if (host_status & TIFM_MMCSD_EOFB)
			host->cmd_flags &= ~CARD_BUSY;
		else if (host_status & TIFM_MMCSD_CB)
			host->cmd_flags |= CARD_BUSY;

		tifm_sd_check_status(host);
	}
done:
	writel(host_status, sock->addr + SOCK_MMCSD_STATUS);
	spin_unlock(&sock->lock);
}

static void tifm_sd_prepare_data(struct tifm_sd *host, struct mmc_command *cmd)
{
	struct tifm_dev *sock = host->dev;
	unsigned int dest_cnt;

	/* DMA style IO */
	dev_dbg(&sock->dev, "setting dma for %d blocks\n",
		cmd->data->blocks);
	writel(TIFM_FIFO_INT_SETALL,
	       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_CLEAR);
	writel(ilog2(cmd->data->blksz) - 2,
	       sock->addr + SOCK_FIFO_PAGE_SIZE);
	writel(TIFM_FIFO_ENABLE, sock->addr + SOCK_FIFO_CONTROL);
	writel(TIFM_FIFO_INTMASK, sock->addr + SOCK_DMA_FIFO_INT_ENABLE_SET);

	dest_cnt = (cmd->data->blocks) << 8;

	writel(sg_dma_address(cmd->data->sg), sock->addr + SOCK_DMA_ADDRESS);

	writel(cmd->data->blocks - 1, sock->addr + SOCK_MMCSD_NUM_BLOCKS);
	writel(cmd->data->blksz - 1, sock->addr + SOCK_MMCSD_BLOCK_LEN);

	if (cmd->data->flags & MMC_DATA_WRITE) {
		writel(TIFM_MMCSD_TXDE, sock->addr + SOCK_MMCSD_BUFFER_CONFIG);
		writel(dest_cnt | TIFM_DMA_TX | TIFM_DMA_EN,
		       sock->addr + SOCK_DMA_CONTROL);
	} else {
		writel(TIFM_MMCSD_RXDE, sock->addr + SOCK_MMCSD_BUFFER_CONFIG);
		writel(dest_cnt | TIFM_DMA_EN, sock->addr + SOCK_DMA_CONTROL);
	}
}

static void tifm_sd_set_data_timeout(struct tifm_sd *host,
				     struct mmc_data *data)
{
	struct tifm_dev *sock = host->dev;
	unsigned int data_timeout = data->timeout_clks;

	if (fixed_timeout)
		return;

	data_timeout += data->timeout_ns /
			((1000000000UL / host->clk_freq) * host->clk_div);

	if (data_timeout < 0xffff) {
		writel(data_timeout, sock->addr + SOCK_MMCSD_DATA_TO);
		writel((~TIFM_MMCSD_DPE)
		       & readl(sock->addr + SOCK_MMCSD_SDIO_MODE_CONFIG),
		       sock->addr + SOCK_MMCSD_SDIO_MODE_CONFIG);
	} else {
		data_timeout = (data_timeout >> 10) + 1;
		if (data_timeout > 0xffff)
			data_timeout = 0;	/* set to unlimited */
		writel(data_timeout, sock->addr + SOCK_MMCSD_DATA_TO);
		writel(TIFM_MMCSD_DPE
		       | readl(sock->addr + SOCK_MMCSD_SDIO_MODE_CONFIG),
		       sock->addr + SOCK_MMCSD_SDIO_MODE_CONFIG);
	}
}

static void tifm_sd_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct tifm_sd *host = mmc_priv(mmc);
	struct tifm_dev *sock = host->dev;
	unsigned long flags;
	int sg_count = 0;
	struct mmc_data *r_data = mrq->cmd->data;

	spin_lock_irqsave(&sock->lock, flags);
	if (host->eject) {
		spin_unlock_irqrestore(&sock->lock, flags);
		goto err_out;
	}

	if (host->req) {
		printk(KERN_ERR DRIVER_NAME ": unfinished request detected\n");
		spin_unlock_irqrestore(&sock->lock, flags);
		goto err_out;
	}

	if (r_data) {
		tifm_sd_set_data_timeout(host, r_data);

		if (host->no_dma) {
			host->buffer_size = mrq->cmd->data->blocks
					    * mrq->cmd->data->blksz;

			writel(TIFM_MMCSD_BUFINT
			       | readl(sock->addr + SOCK_MMCSD_INT_ENABLE),
			       sock->addr + SOCK_MMCSD_INT_ENABLE);
			writel(((TIFM_MMCSD_FIFO_SIZE - 1) << 8)
			       | (TIFM_MMCSD_FIFO_SIZE - 1),
			       sock->addr + SOCK_MMCSD_BUFFER_CONFIG);

			host->written_blocks = 0;
			host->cmd_flags &= ~CARD_BUSY;
			host->buffer_pos = 0;
			writel(r_data->blocks - 1,
			       sock->addr + SOCK_MMCSD_NUM_BLOCKS);
			writel(r_data->blksz - 1,
			       sock->addr + SOCK_MMCSD_BLOCK_LEN);
		} else {
			sg_count = tifm_map_sg(sock, r_data->sg, r_data->sg_len,
					       mrq->cmd->flags & MMC_DATA_WRITE
					       ? PCI_DMA_TODEVICE
					       : PCI_DMA_FROMDEVICE);
			if (sg_count != 1) {
				printk(KERN_ERR DRIVER_NAME
					": scatterlist map failed\n");
				spin_unlock_irqrestore(&sock->lock, flags);
				goto err_out;
			}

			host->written_blocks = 0;
			host->cmd_flags &= ~CARD_BUSY;
			tifm_sd_prepare_data(host, mrq->cmd);
		}
	}

	host->req = mrq;
	mod_timer(&host->timer, jiffies + host->timeout_jiffies);
	host->cmd_flags = 0;
	writel(TIFM_CTRL_LED | readl(sock->addr + SOCK_CONTROL),
	       sock->addr + SOCK_CONTROL);
	tifm_sd_exec(host, mrq->cmd);
	spin_unlock_irqrestore(&sock->lock, flags);
	return;

err_out:
	if (sg_count > 0)
		tifm_unmap_sg(sock, r_data->sg, r_data->sg_len,
			      (r_data->flags & MMC_DATA_WRITE)
			      ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);

	mrq->cmd->error = MMC_ERR_TIMEOUT;
	mmc_request_done(mmc, mrq);
}

static void tifm_sd_end_cmd(unsigned long data)
{
	struct tifm_sd *host = (struct tifm_sd*)data;
	struct tifm_dev *sock = host->dev;
	struct mmc_host *mmc = tifm_get_drvdata(sock);
	struct mmc_request *mrq;
	struct mmc_data *r_data = NULL;
	unsigned long flags;

	spin_lock_irqsave(&sock->lock, flags);

	del_timer(&host->timer);
	mrq = host->req;
	host->req = NULL;

	if (!mrq) {
		printk(KERN_ERR DRIVER_NAME ": no request to complete?\n");
		spin_unlock_irqrestore(&sock->lock, flags);
		return;
	}

	r_data = mrq->cmd->data;
	if (r_data) {
		if (host->no_dma) {
			writel((~TIFM_MMCSD_BUFINT) &
				readl(sock->addr + SOCK_MMCSD_INT_ENABLE),
				sock->addr + SOCK_MMCSD_INT_ENABLE);

			if (r_data->flags & MMC_DATA_WRITE) {
				r_data->bytes_xfered = host->written_blocks
						       * r_data->blksz;
			} else {
				r_data->bytes_xfered = r_data->blocks -
					readl(sock->addr + SOCK_MMCSD_NUM_BLOCKS)
					- 1;
				r_data->bytes_xfered *= r_data->blksz;
				r_data->bytes_xfered += r_data->blksz
					- readl(sock->addr + SOCK_MMCSD_BLOCK_LEN)
					+ 1;
			}
			host->buffer_pos = 0;
			host->buffer_size = 0;
		} else {
			if (r_data->flags & MMC_DATA_WRITE) {
				r_data->bytes_xfered = host->written_blocks
						       * r_data->blksz;
			} else {
				r_data->bytes_xfered = r_data->blocks -
					readl(sock->addr + SOCK_MMCSD_NUM_BLOCKS) - 1;
				r_data->bytes_xfered *= r_data->blksz;
				r_data->bytes_xfered += r_data->blksz -
					readl(sock->addr + SOCK_MMCSD_BLOCK_LEN)
					+ 1;
			}
			tifm_unmap_sg(sock, r_data->sg, r_data->sg_len,
				      (r_data->flags & MMC_DATA_WRITE)
				      ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
		}
	}

	writel((~TIFM_CTRL_LED) & readl(sock->addr + SOCK_CONTROL),
	       sock->addr + SOCK_CONTROL);

	spin_unlock_irqrestore(&sock->lock, flags);
	mmc_request_done(mmc, mrq);
}

static void tifm_sd_abort(unsigned long data)
{
	struct tifm_sd *host = (struct tifm_sd*)data;

	printk(KERN_ERR DRIVER_NAME
	       ": card failed to respond for a long period of time\n");

	tifm_eject(host->dev);
}

static void tifm_sd_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct tifm_sd *host = mmc_priv(mmc);
	struct tifm_dev *sock = host->dev;
	unsigned int clk_div1, clk_div2;
	unsigned long flags;

	spin_lock_irqsave(&sock->lock, flags);

	dev_dbg(&sock->dev, "Setting bus width %d, power %d\n", ios->bus_width,
		ios->power_mode);
	if (ios->bus_width == MMC_BUS_WIDTH_4) {
		writel(TIFM_MMCSD_4BBUS | readl(sock->addr + SOCK_MMCSD_CONFIG),
		       sock->addr + SOCK_MMCSD_CONFIG);
	} else {
		writel((~TIFM_MMCSD_4BBUS)
		       & readl(sock->addr + SOCK_MMCSD_CONFIG),
		       sock->addr + SOCK_MMCSD_CONFIG);
	}

	if (ios->clock) {
		clk_div1 = 20000000 / ios->clock;
		if (!clk_div1)
			clk_div1 = 1;

		clk_div2 = 24000000 / ios->clock;
		if (!clk_div2)
			clk_div2 = 1;

		if ((20000000 / clk_div1) > ios->clock)
			clk_div1++;
		if ((24000000 / clk_div2) > ios->clock)
			clk_div2++;
		if ((20000000 / clk_div1) > (24000000 / clk_div2)) {
			host->clk_freq = 20000000;
			host->clk_div = clk_div1;
			writel((~TIFM_CTRL_FAST_CLK)
			       & readl(sock->addr + SOCK_CONTROL),
			       sock->addr + SOCK_CONTROL);
		} else {
			host->clk_freq = 24000000;
			host->clk_div = clk_div2;
			writel(TIFM_CTRL_FAST_CLK
			       | readl(sock->addr + SOCK_CONTROL),
			       sock->addr + SOCK_CONTROL);
		}
	} else {
		host->clk_div = 0;
	}
	host->clk_div &= TIFM_MMCSD_CLKMASK;
	writel(host->clk_div
	       | ((~TIFM_MMCSD_CLKMASK)
		  & readl(sock->addr + SOCK_MMCSD_CONFIG)),
	       sock->addr + SOCK_MMCSD_CONFIG);

	host->open_drain = (ios->bus_mode == MMC_BUSMODE_OPENDRAIN);

	/* chip_select : maybe later */
	//vdd
	//power is set before probe / after remove

	spin_unlock_irqrestore(&sock->lock, flags);
}

static int tifm_sd_ro(struct mmc_host *mmc)
{
	int rc = 0;
	struct tifm_sd *host = mmc_priv(mmc);
	struct tifm_dev *sock = host->dev;
	unsigned long flags;

	spin_lock_irqsave(&sock->lock, flags);
	if (TIFM_MMCSD_CARD_RO & readl(sock->addr + SOCK_PRESENT_STATE))
		rc = 1;
	spin_unlock_irqrestore(&sock->lock, flags);
	return rc;
}

static const struct mmc_host_ops tifm_sd_ops = {
	.request = tifm_sd_request,
	.set_ios = tifm_sd_ios,
	.get_ro  = tifm_sd_ro
};

static int tifm_sd_initialize_host(struct tifm_sd *host)
{
	int rc;
	unsigned int host_status = 0;
	struct tifm_dev *sock = host->dev;

	writel(0, sock->addr + SOCK_MMCSD_INT_ENABLE);
	mmiowb();
	host->clk_div = 61;
	host->clk_freq = 20000000;
	writel(TIFM_MMCSD_RESET, sock->addr + SOCK_MMCSD_SYSTEM_CONTROL);
	writel(host->clk_div | TIFM_MMCSD_POWER,
	       sock->addr + SOCK_MMCSD_CONFIG);

	/* wait up to 0.51 sec for reset */
	for (rc = 2; rc <= 256; rc <<= 1) {
		if (1 & readl(sock->addr + SOCK_MMCSD_SYSTEM_STATUS)) {
			rc = 0;
			break;
		}
		msleep(rc);
	}

	if (rc) {
		printk(KERN_ERR DRIVER_NAME
		       ": controller failed to reset\n");
		return -ENODEV;
	}

	writel(0, sock->addr + SOCK_MMCSD_NUM_BLOCKS);
	writel(host->clk_div | TIFM_MMCSD_POWER,
	       sock->addr + SOCK_MMCSD_CONFIG);
	writel(TIFM_MMCSD_RXDE, sock->addr + SOCK_MMCSD_BUFFER_CONFIG);

	// command timeout fixed to 64 clocks for now
	writel(64, sock->addr + SOCK_MMCSD_COMMAND_TO);
	writel(TIFM_MMCSD_INAB, sock->addr + SOCK_MMCSD_COMMAND);

	/* INAB should take much less than reset */
	for (rc = 1; rc <= 16; rc <<= 1) {
		host_status = readl(sock->addr + SOCK_MMCSD_STATUS);
		writel(host_status, sock->addr + SOCK_MMCSD_STATUS);
		if (!(host_status & TIFM_MMCSD_ERRMASK)
		    && (host_status & TIFM_MMCSD_EOC)) {
			rc = 0;
			break;
		}
		msleep(rc);
	}

	if (rc) {
		printk(KERN_ERR DRIVER_NAME
		       ": card not ready - probe failed on initialization\n");
		return -ENODEV;
	}

	writel(TIFM_MMCSD_DATAMASK | TIFM_MMCSD_ERRMASK,
	       sock->addr + SOCK_MMCSD_INT_ENABLE);
	mmiowb();

	return 0;
}

static int tifm_sd_probe(struct tifm_dev *sock)
{
	struct mmc_host *mmc;
	struct tifm_sd *host;
	int rc = -EIO;

	if (!(TIFM_SOCK_STATE_OCCUPIED
	      & readl(sock->addr + SOCK_PRESENT_STATE))) {
		printk(KERN_WARNING DRIVER_NAME ": card gone, unexpectedly\n");
		return rc;
	}

	mmc = mmc_alloc_host(sizeof(struct tifm_sd), &sock->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->no_dma = no_dma;
	tifm_set_drvdata(sock, mmc);
	host->dev = sock;
	host->timeout_jiffies = msecs_to_jiffies(1000);

	tasklet_init(&host->finish_tasklet, tifm_sd_end_cmd,
		     (unsigned long)host);
	setup_timer(&host->timer, tifm_sd_abort, (unsigned long)host);

	mmc->ops = &tifm_sd_ops;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_MULTIWRITE;
	mmc->f_min = 20000000 / 60;
	mmc->f_max = 24000000;
	mmc->max_hw_segs = 1;
	mmc->max_phys_segs = 1;
	// limited by DMA counter - it's safer to stick with
	// block counter has 11 bits though
	mmc->max_blk_count = 256;
	// 2k maximum hw block length
	mmc->max_blk_size = 2048;
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_seg_size = mmc->max_req_size;
	sock->card_event = tifm_sd_card_event;
	sock->data_event = tifm_sd_data_event;
	rc = tifm_sd_initialize_host(host);

	if (!rc)
		rc = mmc_add_host(mmc);
	if (rc)
		goto out_free_mmc;

	return 0;
out_free_mmc:
	mmc_free_host(mmc);
	return rc;
}

static void tifm_sd_remove(struct tifm_dev *sock)
{
	struct mmc_host *mmc = tifm_get_drvdata(sock);
	struct tifm_sd *host = mmc_priv(mmc);
	unsigned long flags;

	spin_lock_irqsave(&sock->lock, flags);
	host->eject = 1;
	writel(0, sock->addr + SOCK_MMCSD_INT_ENABLE);
	mmiowb();
	spin_unlock_irqrestore(&sock->lock, flags);

	tasklet_kill(&host->finish_tasklet);

	spin_lock_irqsave(&sock->lock, flags);
	if (host->req) {
		writel(TIFM_FIFO_INT_SETALL,
		       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_CLEAR);
		writel(0, sock->addr + SOCK_DMA_FIFO_INT_ENABLE_SET);
		host->req->cmd->error = MMC_ERR_TIMEOUT;
		if (host->req->stop)
			host->req->stop->error = MMC_ERR_TIMEOUT;
		tasklet_schedule(&host->finish_tasklet);
	}
	spin_unlock_irqrestore(&sock->lock, flags);
	mmc_remove_host(mmc);
	dev_dbg(&sock->dev, "after remove\n");

	/* The meaning of the bit majority in this constant is unknown. */
	writel(0xfff8 & readl(sock->addr + SOCK_CONTROL),
	       sock->addr + SOCK_CONTROL);

	mmc_free_host(mmc);
}

#ifdef CONFIG_PM

static int tifm_sd_suspend(struct tifm_dev *sock, pm_message_t state)
{
	struct mmc_host *mmc = tifm_get_drvdata(sock);
	int rc;

	rc = mmc_suspend_host(mmc, state);
	/* The meaning of the bit majority in this constant is unknown. */
	writel(0xfff8 & readl(sock->addr + SOCK_CONTROL),
	       sock->addr + SOCK_CONTROL);
	return rc;
}

static int tifm_sd_resume(struct tifm_dev *sock)
{
	struct mmc_host *mmc = tifm_get_drvdata(sock);
	struct tifm_sd *host = mmc_priv(mmc);

	if (sock->type != TIFM_TYPE_SD
	    || tifm_sd_initialize_host(host)) {
		tifm_eject(sock);
		return 0;
	} else {
		return mmc_resume_host(mmc);
	}
}

#else

#define tifm_sd_suspend NULL
#define tifm_sd_resume NULL

#endif /* CONFIG_PM */

static struct tifm_device_id tifm_sd_id_tbl[] = {
	{ TIFM_TYPE_SD }, { }
};

static struct tifm_driver tifm_sd_driver = {
	.driver = {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE
	},
	.id_table = tifm_sd_id_tbl,
	.probe    = tifm_sd_probe,
	.remove   = tifm_sd_remove,
	.suspend  = tifm_sd_suspend,
	.resume   = tifm_sd_resume
};

static int __init tifm_sd_init(void)
{
	return tifm_register_driver(&tifm_sd_driver);
}

static void __exit tifm_sd_exit(void)
{
	tifm_unregister_driver(&tifm_sd_driver);
}

MODULE_AUTHOR("Alex Dubov");
MODULE_DESCRIPTION("TI FlashMedia SD driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(tifm, tifm_sd_id_tbl);
MODULE_VERSION(DRIVER_VERSION);

module_init(tifm_sd_init);
module_exit(tifm_sd_exit);
