/*
 *  tifm_sd.c - TI FlashMedia driver
 *
 *  Copyright (C) 2006 Alex Dubov <oakad@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Special thanks to Brad Campbell for extensive testing of this driver.
 *
 */


#include <linux/tifm.h>
#include <linux/mmc/host.h>
#include <linux/highmem.h>
#include <linux/scatterlist.h>
#include <linux/log2.h>
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

#define TIFM_MMCSD_ERRMASK    0x01e0   /* set bits: CCRC, CTO, DCRC, DTO */
#define TIFM_MMCSD_EOC        0x0001   /* end of command phase  */
#define TIFM_MMCSD_CD         0x0002   /* card detect           */
#define TIFM_MMCSD_CB         0x0004   /* card enter busy state */
#define TIFM_MMCSD_BRS        0x0008   /* block received/sent   */
#define TIFM_MMCSD_EOFB       0x0010   /* card exit busy state  */
#define TIFM_MMCSD_DTO        0x0020   /* data time-out         */
#define TIFM_MMCSD_DCRC       0x0040   /* data crc error        */
#define TIFM_MMCSD_CTO        0x0080   /* command time-out      */
#define TIFM_MMCSD_CCRC       0x0100   /* command crc error     */
#define TIFM_MMCSD_AF         0x0400   /* fifo almost full      */
#define TIFM_MMCSD_AE         0x0800   /* fifo almost empty     */
#define TIFM_MMCSD_OCRB       0x1000   /* OCR busy              */
#define TIFM_MMCSD_CIRQ       0x2000   /* card irq (cmd40/sdio) */
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

#define TIFM_MMCSD_MAX_BLOCK_SIZE  0x0800UL

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
	struct tifm_dev       *dev;

	unsigned short        eject:1,
			      open_drain:1,
			      no_dma:1;
	unsigned short        cmd_flags;

	unsigned int          clk_freq;
	unsigned int          clk_div;
	unsigned long         timeout_jiffies;

	struct tasklet_struct finish_tasklet;
	struct timer_list     timer;
	struct mmc_request    *req;

	int                   sg_len;
	int                   sg_pos;
	unsigned int          block_pos;
	struct scatterlist    bounce_buf;
	unsigned char         bounce_buf_data[TIFM_MMCSD_MAX_BLOCK_SIZE];
};

/* for some reason, host won't respond correctly to readw/writew */
static void tifm_sd_read_fifo(struct tifm_sd *host, struct page *pg,
			      unsigned int off, unsigned int cnt)
{
	struct tifm_dev *sock = host->dev;
	unsigned char *buf;
	unsigned int pos = 0, val;

	buf = kmap_atomic(pg, KM_BIO_DST_IRQ) + off;
	if (host->cmd_flags & DATA_CARRY) {
		buf[pos++] = host->bounce_buf_data[0];
		host->cmd_flags &= ~DATA_CARRY;
	}

	while (pos < cnt) {
		val = readl(sock->addr + SOCK_MMCSD_DATA);
		buf[pos++] = val & 0xff;
		if (pos == cnt) {
			host->bounce_buf_data[0] = (val >> 8) & 0xff;
			host->cmd_flags |= DATA_CARRY;
			break;
		}
		buf[pos++] = (val >> 8) & 0xff;
	}
	kunmap_atomic(buf - off, KM_BIO_DST_IRQ);
}

static void tifm_sd_write_fifo(struct tifm_sd *host, struct page *pg,
			       unsigned int off, unsigned int cnt)
{
	struct tifm_dev *sock = host->dev;
	unsigned char *buf;
	unsigned int pos = 0, val;

	buf = kmap_atomic(pg, KM_BIO_SRC_IRQ) + off;
	if (host->cmd_flags & DATA_CARRY) {
		val = host->bounce_buf_data[0] | ((buf[pos++] << 8) & 0xff00);
		writel(val, sock->addr + SOCK_MMCSD_DATA);
		host->cmd_flags &= ~DATA_CARRY;
	}

	while (pos < cnt) {
		val = buf[pos++];
		if (pos == cnt) {
			host->bounce_buf_data[0] = val & 0xff;
			host->cmd_flags |= DATA_CARRY;
			break;
		}
		val |= (buf[pos++] << 8) & 0xff00;
		writel(val, sock->addr + SOCK_MMCSD_DATA);
	}
	kunmap_atomic(buf - off, KM_BIO_SRC_IRQ);
}

static void tifm_sd_transfer_data(struct tifm_sd *host)
{
	struct mmc_data *r_data = host->req->cmd->data;
	struct scatterlist *sg = r_data->sg;
	unsigned int off, cnt, t_size = TIFM_MMCSD_FIFO_SIZE * 2;
	unsigned int p_off, p_cnt;
	struct page *pg;

	if (host->sg_pos == host->sg_len)
		return;
	while (t_size) {
		cnt = sg[host->sg_pos].length - host->block_pos;
		if (!cnt) {
			host->block_pos = 0;
			host->sg_pos++;
			if (host->sg_pos == host->sg_len) {
				if ((r_data->flags & MMC_DATA_WRITE)
				    && DATA_CARRY)
					writel(host->bounce_buf_data[0],
					       host->dev->addr
					       + SOCK_MMCSD_DATA);

				return;
			}
			cnt = sg[host->sg_pos].length;
		}
		off = sg[host->sg_pos].offset + host->block_pos;

		pg = nth_page(sg[host->sg_pos].page, off >> PAGE_SHIFT);
		p_off = offset_in_page(off);
		p_cnt = PAGE_SIZE - p_off;
		p_cnt = min(p_cnt, cnt);
		p_cnt = min(p_cnt, t_size);

		if (r_data->flags & MMC_DATA_READ)
			tifm_sd_read_fifo(host, pg, p_off, p_cnt);
		else if (r_data->flags & MMC_DATA_WRITE)
			tifm_sd_write_fifo(host, pg, p_off, p_cnt);

		t_size -= p_cnt;
		host->block_pos += p_cnt;
	}
}

static void tifm_sd_copy_page(struct page *dst, unsigned int dst_off,
			      struct page *src, unsigned int src_off,
			      unsigned int count)
{
	unsigned char *src_buf = kmap_atomic(src, KM_BIO_SRC_IRQ) + src_off;
	unsigned char *dst_buf = kmap_atomic(dst, KM_BIO_DST_IRQ) + dst_off;

	memcpy(dst_buf, src_buf, count);

	kunmap_atomic(dst_buf - dst_off, KM_BIO_DST_IRQ);
	kunmap_atomic(src_buf - src_off, KM_BIO_SRC_IRQ);
}

static void tifm_sd_bounce_block(struct tifm_sd *host, struct mmc_data *r_data)
{
	struct scatterlist *sg = r_data->sg;
	unsigned int t_size = r_data->blksz;
	unsigned int off, cnt;
	unsigned int p_off, p_cnt;
	struct page *pg;

	dev_dbg(&host->dev->dev, "bouncing block\n");
	while (t_size) {
		cnt = sg[host->sg_pos].length - host->block_pos;
		if (!cnt) {
			host->block_pos = 0;
			host->sg_pos++;
			if (host->sg_pos == host->sg_len)
				return;
			cnt = sg[host->sg_pos].length;
		}
		off = sg[host->sg_pos].offset + host->block_pos;

		pg = nth_page(sg[host->sg_pos].page, off >> PAGE_SHIFT);
		p_off = offset_in_page(off);
		p_cnt = PAGE_SIZE - p_off;
		p_cnt = min(p_cnt, cnt);
		p_cnt = min(p_cnt, t_size);

		if (r_data->flags & MMC_DATA_WRITE)
			tifm_sd_copy_page(host->bounce_buf.page,
					  r_data->blksz - t_size,
					  pg, p_off, p_cnt);
		else if (r_data->flags & MMC_DATA_READ)
			tifm_sd_copy_page(pg, p_off, host->bounce_buf.page,
					  r_data->blksz - t_size, p_cnt);

		t_size -= p_cnt;
		host->block_pos += p_cnt;
	}
}

static int tifm_sd_set_dma_data(struct tifm_sd *host, struct mmc_data *r_data)
{
	struct tifm_dev *sock = host->dev;
	unsigned int t_size = TIFM_DMA_TSIZE * r_data->blksz;
	unsigned int dma_len, dma_blk_cnt, dma_off;
	struct scatterlist *sg = NULL;
	unsigned long flags;

	if (host->sg_pos == host->sg_len)
		return 1;

	if (host->cmd_flags & DATA_CARRY) {
		host->cmd_flags &= ~DATA_CARRY;
		local_irq_save(flags);
		tifm_sd_bounce_block(host, r_data);
		local_irq_restore(flags);
		if (host->sg_pos == host->sg_len)
			return 1;
	}

	dma_len = sg_dma_len(&r_data->sg[host->sg_pos]) - host->block_pos;
	if (!dma_len) {
		host->block_pos = 0;
		host->sg_pos++;
		if (host->sg_pos == host->sg_len)
			return 1;
		dma_len = sg_dma_len(&r_data->sg[host->sg_pos]);
	}

	if (dma_len < t_size) {
		dma_blk_cnt = dma_len / r_data->blksz;
		dma_off = host->block_pos;
		host->block_pos += dma_blk_cnt * r_data->blksz;
	} else {
		dma_blk_cnt = TIFM_DMA_TSIZE;
		dma_off = host->block_pos;
		host->block_pos += t_size;
	}

	if (dma_blk_cnt)
		sg = &r_data->sg[host->sg_pos];
	else if (dma_len) {
		if (r_data->flags & MMC_DATA_WRITE) {
			local_irq_save(flags);
			tifm_sd_bounce_block(host, r_data);
			local_irq_restore(flags);
		} else
			host->cmd_flags |= DATA_CARRY;

		sg = &host->bounce_buf;
		dma_off = 0;
		dma_blk_cnt = 1;
	} else
		return 1;

	dev_dbg(&sock->dev, "setting dma for %d blocks\n", dma_blk_cnt);
	writel(sg_dma_address(sg) + dma_off, sock->addr + SOCK_DMA_ADDRESS);
	if (r_data->flags & MMC_DATA_WRITE)
		writel((dma_blk_cnt << 8) | TIFM_DMA_TX | TIFM_DMA_EN,
		       sock->addr + SOCK_DMA_CONTROL);
	else
		writel((dma_blk_cnt << 8) | TIFM_DMA_EN,
		       sock->addr + SOCK_DMA_CONTROL);

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

	if (cmd->error)
		goto finish_request;

	if (!(host->cmd_flags & CMD_READY))
		return;

	if (cmd->data) {
		if (cmd->data->error) {
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
			if (tifm_sd_set_dma_data(host, r_data)) {
				host->cmd_flags |= FIFO_READY;
				tifm_sd_check_status(host);
			}
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
	int cmd_error = 0;
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
				cmd_error = -ETIMEDOUT;
			else if (host_status & TIFM_MMCSD_CCRC)
				cmd_error = -EILSEQ;

			if (cmd->data) {
				if (host_status & TIFM_MMCSD_DTO)
					cmd->data->error = -ETIMEDOUT;
				else if (host_status & TIFM_MMCSD_DCRC)
					cmd->data->error = -EILSEQ;
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
				tifm_sd_transfer_data(host);
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
	struct mmc_data *r_data = mrq->cmd->data;

	spin_lock_irqsave(&sock->lock, flags);
	if (host->eject) {
		mrq->cmd->error = -ENOMEDIUM;
		goto err_out;
	}

	if (host->req) {
		printk(KERN_ERR "%s : unfinished request detected\n",
		       sock->dev.bus_id);
		mrq->cmd->error = -ETIMEDOUT;
		goto err_out;
	}

	if (mrq->data && !is_power_of_2(mrq->data->blksz)) {
		printk(KERN_ERR "%s: Unsupported block size (%d bytes)\n",
			sock->dev.bus_id, mrq->data->blksz);
		mrq->cmd->error = -EINVAL;
		goto err_out;
	}

	host->cmd_flags = 0;
	host->block_pos = 0;
	host->sg_pos = 0;

	if (r_data) {
		tifm_sd_set_data_timeout(host, r_data);

		if ((r_data->flags & MMC_DATA_WRITE) && !mrq->stop)
			 writel(TIFM_MMCSD_EOFB
				| readl(sock->addr + SOCK_MMCSD_INT_ENABLE),
				sock->addr + SOCK_MMCSD_INT_ENABLE);

		if (host->no_dma) {
			writel(TIFM_MMCSD_BUFINT
			       | readl(sock->addr + SOCK_MMCSD_INT_ENABLE),
			       sock->addr + SOCK_MMCSD_INT_ENABLE);
			writel(((TIFM_MMCSD_FIFO_SIZE - 1) << 8)
			       | (TIFM_MMCSD_FIFO_SIZE - 1),
			       sock->addr + SOCK_MMCSD_BUFFER_CONFIG);

			host->sg_len = r_data->sg_len;
		} else {
			sg_init_one(&host->bounce_buf, host->bounce_buf_data,
				    r_data->blksz);

			if(1 != tifm_map_sg(sock, &host->bounce_buf, 1,
					    r_data->flags & MMC_DATA_WRITE
					    ? PCI_DMA_TODEVICE
					    : PCI_DMA_FROMDEVICE)) {
				printk(KERN_ERR "%s : scatterlist map failed\n",
				       sock->dev.bus_id);
				spin_unlock_irqrestore(&sock->lock, flags);
				goto err_out;
			}
			host->sg_len = tifm_map_sg(sock, r_data->sg,
						   r_data->sg_len,
						   r_data->flags
						   & MMC_DATA_WRITE
						   ? PCI_DMA_TODEVICE
						   : PCI_DMA_FROMDEVICE);
			if (host->sg_len < 1) {
				printk(KERN_ERR "%s : scatterlist map failed\n",
				       sock->dev.bus_id);
				tifm_unmap_sg(sock, &host->bounce_buf, 1,
					      r_data->flags & MMC_DATA_WRITE
					      ? PCI_DMA_TODEVICE
					      : PCI_DMA_FROMDEVICE);
				spin_unlock_irqrestore(&sock->lock, flags);
				goto err_out;
			}

			writel(TIFM_FIFO_INT_SETALL,
			       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_CLEAR);
			writel(ilog2(r_data->blksz) - 2,
			       sock->addr + SOCK_FIFO_PAGE_SIZE);
			writel(TIFM_FIFO_ENABLE,
			       sock->addr + SOCK_FIFO_CONTROL);
			writel(TIFM_FIFO_INTMASK,
			       sock->addr + SOCK_DMA_FIFO_INT_ENABLE_SET);

			if (r_data->flags & MMC_DATA_WRITE)
				writel(TIFM_MMCSD_TXDE,
				       sock->addr + SOCK_MMCSD_BUFFER_CONFIG);
			else
				writel(TIFM_MMCSD_RXDE,
				       sock->addr + SOCK_MMCSD_BUFFER_CONFIG);

			tifm_sd_set_dma_data(host, r_data);
		}

		writel(r_data->blocks - 1,
		       sock->addr + SOCK_MMCSD_NUM_BLOCKS);
		writel(r_data->blksz - 1,
		       sock->addr + SOCK_MMCSD_BLOCK_LEN);
	}

	host->req = mrq;
	mod_timer(&host->timer, jiffies + host->timeout_jiffies);
	writel(TIFM_CTRL_LED | readl(sock->addr + SOCK_CONTROL),
	       sock->addr + SOCK_CONTROL);
	tifm_sd_exec(host, mrq->cmd);
	spin_unlock_irqrestore(&sock->lock, flags);
	return;

err_out:
	spin_unlock_irqrestore(&sock->lock, flags);
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
		printk(KERN_ERR " %s : no request to complete?\n",
		       sock->dev.bus_id);
		spin_unlock_irqrestore(&sock->lock, flags);
		return;
	}

	r_data = mrq->cmd->data;
	if (r_data) {
		if (host->no_dma) {
			writel((~TIFM_MMCSD_BUFINT)
			       & readl(sock->addr + SOCK_MMCSD_INT_ENABLE),
			       sock->addr + SOCK_MMCSD_INT_ENABLE);
		} else {
			tifm_unmap_sg(sock, &host->bounce_buf, 1,
				      (r_data->flags & MMC_DATA_WRITE)
				      ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
			tifm_unmap_sg(sock, r_data->sg, r_data->sg_len,
				      (r_data->flags & MMC_DATA_WRITE)
				      ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
		}

		r_data->bytes_xfered = r_data->blocks
			- readl(sock->addr + SOCK_MMCSD_NUM_BLOCKS) - 1;
		r_data->bytes_xfered *= r_data->blksz;
		r_data->bytes_xfered += r_data->blksz
			- readl(sock->addr + SOCK_MMCSD_BLOCK_LEN) + 1;
	}

	writel((~TIFM_CTRL_LED) & readl(sock->addr + SOCK_CONTROL),
	       sock->addr + SOCK_CONTROL);

	spin_unlock_irqrestore(&sock->lock, flags);
	mmc_request_done(mmc, mrq);
}

static void tifm_sd_abort(unsigned long data)
{
	struct tifm_sd *host = (struct tifm_sd*)data;

	printk(KERN_ERR
	       "%s : card failed to respond for a long period of time "
	       "(%x, %x)\n",
	       host->dev->dev.bus_id, host->req->cmd->opcode, host->cmd_flags);

	tifm_eject(host->dev);
}

static void tifm_sd_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct tifm_sd *host = mmc_priv(mmc);
	struct tifm_dev *sock = host->dev;
	unsigned int clk_div1, clk_div2;
	unsigned long flags;

	spin_lock_irqsave(&sock->lock, flags);

	dev_dbg(&sock->dev, "ios: clock = %u, vdd = %x, bus_mode = %x, "
		"chip_select = %x, power_mode = %x, bus_width = %x\n",
		ios->clock, ios->vdd, ios->bus_mode, ios->chip_select,
		ios->power_mode, ios->bus_width);

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
	for (rc = 32; rc <= 256; rc <<= 1) {
		if (1 & readl(sock->addr + SOCK_MMCSD_SYSTEM_STATUS)) {
			rc = 0;
			break;
		}
		msleep(rc);
	}

	if (rc) {
		printk(KERN_ERR "%s : controller failed to reset\n",
		       sock->dev.bus_id);
		return -ENODEV;
	}

	writel(0, sock->addr + SOCK_MMCSD_NUM_BLOCKS);
	writel(host->clk_div | TIFM_MMCSD_POWER,
	       sock->addr + SOCK_MMCSD_CONFIG);
	writel(TIFM_MMCSD_RXDE, sock->addr + SOCK_MMCSD_BUFFER_CONFIG);

	// command timeout fixed to 64 clocks for now
	writel(64, sock->addr + SOCK_MMCSD_COMMAND_TO);
	writel(TIFM_MMCSD_INAB, sock->addr + SOCK_MMCSD_COMMAND);

	for (rc = 16; rc <= 64; rc <<= 1) {
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
		printk(KERN_ERR
		       "%s : card not ready - probe failed on initialization\n",
		       sock->dev.bus_id);
		return -ENODEV;
	}

	writel(TIFM_MMCSD_CERR | TIFM_MMCSD_BRS | TIFM_MMCSD_EOC
	       | TIFM_MMCSD_ERRMASK,
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
		printk(KERN_WARNING "%s : card gone, unexpectedly\n",
		       sock->dev.bus_id);
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

	mmc->max_blk_count = 2048;
	mmc->max_hw_segs = mmc->max_blk_count;
	mmc->max_blk_size = min(TIFM_MMCSD_MAX_BLOCK_SIZE, PAGE_SIZE);
	mmc->max_seg_size = mmc->max_blk_count * mmc->max_blk_size;
	mmc->max_req_size = mmc->max_seg_size;
	mmc->max_phys_segs = mmc->max_hw_segs;

	sock->card_event = tifm_sd_card_event;
	sock->data_event = tifm_sd_data_event;
	rc = tifm_sd_initialize_host(host);

	if (!rc)
		rc = mmc_add_host(mmc);
	if (!rc)
		return 0;

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
		host->req->cmd->error = -ENOMEDIUM;
		if (host->req->stop)
			host->req->stop->error = -ENOMEDIUM;
		tasklet_schedule(&host->finish_tasklet);
	}
	spin_unlock_irqrestore(&sock->lock, flags);
	mmc_remove_host(mmc);
	dev_dbg(&sock->dev, "after remove\n");

	mmc_free_host(mmc);
}

#ifdef CONFIG_PM

static int tifm_sd_suspend(struct tifm_dev *sock, pm_message_t state)
{
	return mmc_suspend_host(tifm_get_drvdata(sock), state);
}

static int tifm_sd_resume(struct tifm_dev *sock)
{
	struct mmc_host *mmc = tifm_get_drvdata(sock);
	struct tifm_sd *host = mmc_priv(mmc);
	int rc;

	rc = tifm_sd_initialize_host(host);
	dev_dbg(&sock->dev, "resume initialize %d\n", rc);

	if (rc)
		host->eject = 1;
	else
		rc = mmc_resume_host(mmc);

	return rc;
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
