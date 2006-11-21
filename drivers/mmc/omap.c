/*
 *  linux/drivers/media/mmc/omap.c
 *
 *  Copyright (C) 2004 Nokia Corporation
 *  Written by Tuukka Tikkanen and Juha Yrjölä<juha.yrjola@nokia.com>
 *  Misc hacks here and there by Tony Lindgren <tony@atomide.com>
 *  Other hacks (DMA, SD, etc) by David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/mmc/host.h>
#include <linux/mmc/protocol.h>
#include <linux/mmc/card.h>
#include <linux/clk.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/scatterlist.h>
#include <asm/mach-types.h>

#include <asm/arch/board.h>
#include <asm/arch/gpio.h>
#include <asm/arch/dma.h>
#include <asm/arch/mux.h>
#include <asm/arch/fpga.h>
#include <asm/arch/tps65010.h>

#define	OMAP_MMC_REG_CMD	0x00
#define	OMAP_MMC_REG_ARGL	0x04
#define	OMAP_MMC_REG_ARGH	0x08
#define	OMAP_MMC_REG_CON	0x0c
#define	OMAP_MMC_REG_STAT	0x10
#define	OMAP_MMC_REG_IE		0x14
#define	OMAP_MMC_REG_CTO	0x18
#define	OMAP_MMC_REG_DTO	0x1c
#define	OMAP_MMC_REG_DATA	0x20
#define	OMAP_MMC_REG_BLEN	0x24
#define	OMAP_MMC_REG_NBLK	0x28
#define	OMAP_MMC_REG_BUF	0x2c
#define OMAP_MMC_REG_SDIO	0x34
#define	OMAP_MMC_REG_REV	0x3c
#define	OMAP_MMC_REG_RSP0	0x40
#define	OMAP_MMC_REG_RSP1	0x44
#define	OMAP_MMC_REG_RSP2	0x48
#define	OMAP_MMC_REG_RSP3	0x4c
#define	OMAP_MMC_REG_RSP4	0x50
#define	OMAP_MMC_REG_RSP5	0x54
#define	OMAP_MMC_REG_RSP6	0x58
#define	OMAP_MMC_REG_RSP7	0x5c
#define	OMAP_MMC_REG_IOSR	0x60
#define	OMAP_MMC_REG_SYSC	0x64
#define	OMAP_MMC_REG_SYSS	0x68

#define	OMAP_MMC_STAT_CARD_ERR		(1 << 14)
#define	OMAP_MMC_STAT_CARD_IRQ		(1 << 13)
#define	OMAP_MMC_STAT_OCR_BUSY		(1 << 12)
#define	OMAP_MMC_STAT_A_EMPTY		(1 << 11)
#define	OMAP_MMC_STAT_A_FULL		(1 << 10)
#define	OMAP_MMC_STAT_CMD_CRC		(1 <<  8)
#define	OMAP_MMC_STAT_CMD_TOUT		(1 <<  7)
#define	OMAP_MMC_STAT_DATA_CRC		(1 <<  6)
#define	OMAP_MMC_STAT_DATA_TOUT		(1 <<  5)
#define	OMAP_MMC_STAT_END_BUSY		(1 <<  4)
#define	OMAP_MMC_STAT_END_OF_DATA	(1 <<  3)
#define	OMAP_MMC_STAT_CARD_BUSY		(1 <<  2)
#define	OMAP_MMC_STAT_END_OF_CMD	(1 <<  0)

#define OMAP_MMC_READ(host, reg)	__raw_readw((host)->virt_base + OMAP_MMC_REG_##reg)
#define OMAP_MMC_WRITE(host, reg, val)	__raw_writew((val), (host)->virt_base + OMAP_MMC_REG_##reg)

/*
 * Command types
 */
#define OMAP_MMC_CMDTYPE_BC	0
#define OMAP_MMC_CMDTYPE_BCR	1
#define OMAP_MMC_CMDTYPE_AC	2
#define OMAP_MMC_CMDTYPE_ADTC	3


#define DRIVER_NAME "mmci-omap"

/* Specifies how often in millisecs to poll for card status changes
 * when the cover switch is open */
#define OMAP_MMC_SWITCH_POLL_DELAY	500

static int mmc_omap_enable_poll = 1;

struct mmc_omap_host {
	int			initialized;
	int			suspended;
	struct mmc_request *	mrq;
	struct mmc_command *	cmd;
	struct mmc_data *	data;
	struct mmc_host *	mmc;
	struct device *		dev;
	unsigned char		id; /* 16xx chips have 2 MMC blocks */
	struct clk *		iclk;
	struct clk *		fclk;
	struct resource		*mem_res;
	void __iomem		*virt_base;
	unsigned int		phys_base;
	int			irq;
	unsigned char		bus_mode;
	unsigned char		hw_bus_mode;

	unsigned int		sg_len;
	int			sg_idx;
	u16 *			buffer;
	u32			buffer_bytes_left;
	u32			total_bytes_left;

	unsigned		use_dma:1;
	unsigned		brs_received:1, dma_done:1;
	unsigned		dma_is_read:1;
	unsigned		dma_in_use:1;
	int			dma_ch;
	spinlock_t		dma_lock;
	struct timer_list	dma_timer;
	unsigned		dma_len;

	short			power_pin;
	short			wp_pin;

	int			switch_pin;
	struct work_struct	switch_work;
	struct timer_list	switch_timer;
	int			switch_last_state;
};

static inline int
mmc_omap_cover_is_open(struct mmc_omap_host *host)
{
	if (host->switch_pin < 0)
		return 0;
	return omap_get_gpio_datain(host->switch_pin);
}

static ssize_t
mmc_omap_show_cover_switch(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mmc_omap_host *host = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", mmc_omap_cover_is_open(host) ? "open" :
			"closed");
}

static DEVICE_ATTR(cover_switch, S_IRUGO, mmc_omap_show_cover_switch, NULL);

static ssize_t
mmc_omap_show_enable_poll(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", mmc_omap_enable_poll);
}

static ssize_t
mmc_omap_store_enable_poll(struct device *dev,
	struct device_attribute *attr, const char *buf,
	size_t size)
{
	int enable_poll;

	if (sscanf(buf, "%10d", &enable_poll) != 1)
		return -EINVAL;

	if (enable_poll != mmc_omap_enable_poll) {
		struct mmc_omap_host *host = dev_get_drvdata(dev);

		mmc_omap_enable_poll = enable_poll;
		if (enable_poll && host->switch_pin >= 0)
			schedule_work(&host->switch_work);
	}
	return size;
}

static DEVICE_ATTR(enable_poll, 0664,
		   mmc_omap_show_enable_poll, mmc_omap_store_enable_poll);

static void
mmc_omap_start_command(struct mmc_omap_host *host, struct mmc_command *cmd)
{
	u32 cmdreg;
	u32 resptype;
	u32 cmdtype;

	host->cmd = cmd;

	resptype = 0;
	cmdtype = 0;

	/* Our hardware needs to know exact type */
	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		break;
	case MMC_RSP_R1:
	case MMC_RSP_R1B:
		/* resp 1, 1b, 6, 7 */
		resptype = 1;
		break;
	case MMC_RSP_R2:
		resptype = 2;
		break;
	case MMC_RSP_R3:
		resptype = 3;
		break;
	default:
		dev_err(mmc_dev(host->mmc), "Invalid response type: %04x\n", mmc_resp_type(cmd));
		break;
	}

	if (mmc_cmd_type(cmd) == MMC_CMD_ADTC) {
		cmdtype = OMAP_MMC_CMDTYPE_ADTC;
	} else if (mmc_cmd_type(cmd) == MMC_CMD_BC) {
		cmdtype = OMAP_MMC_CMDTYPE_BC;
	} else if (mmc_cmd_type(cmd) == MMC_CMD_BCR) {
		cmdtype = OMAP_MMC_CMDTYPE_BCR;
	} else {
		cmdtype = OMAP_MMC_CMDTYPE_AC;
	}

	cmdreg = cmd->opcode | (resptype << 8) | (cmdtype << 12);

	if (host->bus_mode == MMC_BUSMODE_OPENDRAIN)
		cmdreg |= 1 << 6;

	if (cmd->flags & MMC_RSP_BUSY)
		cmdreg |= 1 << 11;

	if (host->data && !(host->data->flags & MMC_DATA_WRITE))
		cmdreg |= 1 << 15;

	clk_enable(host->fclk);

	OMAP_MMC_WRITE(host, CTO, 200);
	OMAP_MMC_WRITE(host, ARGL, cmd->arg & 0xffff);
	OMAP_MMC_WRITE(host, ARGH, cmd->arg >> 16);
	OMAP_MMC_WRITE(host, IE,
		       OMAP_MMC_STAT_A_EMPTY    | OMAP_MMC_STAT_A_FULL    |
		       OMAP_MMC_STAT_CMD_CRC    | OMAP_MMC_STAT_CMD_TOUT  |
		       OMAP_MMC_STAT_DATA_CRC   | OMAP_MMC_STAT_DATA_TOUT |
		       OMAP_MMC_STAT_END_OF_CMD | OMAP_MMC_STAT_CARD_ERR  |
		       OMAP_MMC_STAT_END_OF_DATA);
	OMAP_MMC_WRITE(host, CMD, cmdreg);
}

static void
mmc_omap_xfer_done(struct mmc_omap_host *host, struct mmc_data *data)
{
	if (host->dma_in_use) {
		enum dma_data_direction dma_data_dir;

		BUG_ON(host->dma_ch < 0);
		if (data->error != MMC_ERR_NONE)
			omap_stop_dma(host->dma_ch);
		/* Release DMA channel lazily */
		mod_timer(&host->dma_timer, jiffies + HZ);
		if (data->flags & MMC_DATA_WRITE)
			dma_data_dir = DMA_TO_DEVICE;
		else
			dma_data_dir = DMA_FROM_DEVICE;
		dma_unmap_sg(mmc_dev(host->mmc), data->sg, host->sg_len,
			     dma_data_dir);
	}
	host->data = NULL;
	host->sg_len = 0;
	clk_disable(host->fclk);

	/* NOTE:  MMC layer will sometimes poll-wait CMD13 next, issuing
	 * dozens of requests until the card finishes writing data.
	 * It'd be cheaper to just wait till an EOFB interrupt arrives...
	 */

	if (!data->stop) {
		host->mrq = NULL;
		mmc_request_done(host->mmc, data->mrq);
		return;
	}

	mmc_omap_start_command(host, data->stop);
}

static void
mmc_omap_end_of_data(struct mmc_omap_host *host, struct mmc_data *data)
{
	unsigned long flags;
	int done;

	if (!host->dma_in_use) {
		mmc_omap_xfer_done(host, data);
		return;
	}
	done = 0;
	spin_lock_irqsave(&host->dma_lock, flags);
	if (host->dma_done)
		done = 1;
	else
		host->brs_received = 1;
	spin_unlock_irqrestore(&host->dma_lock, flags);
	if (done)
		mmc_omap_xfer_done(host, data);
}

static void
mmc_omap_dma_timer(unsigned long data)
{
	struct mmc_omap_host *host = (struct mmc_omap_host *) data;

	BUG_ON(host->dma_ch < 0);
	omap_free_dma(host->dma_ch);
	host->dma_ch = -1;
}

static void
mmc_omap_dma_done(struct mmc_omap_host *host, struct mmc_data *data)
{
	unsigned long flags;
	int done;

	done = 0;
	spin_lock_irqsave(&host->dma_lock, flags);
	if (host->brs_received)
		done = 1;
	else
		host->dma_done = 1;
	spin_unlock_irqrestore(&host->dma_lock, flags);
	if (done)
		mmc_omap_xfer_done(host, data);
}

static void
mmc_omap_cmd_done(struct mmc_omap_host *host, struct mmc_command *cmd)
{
	host->cmd = NULL;

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			/* response type 2 */
			cmd->resp[3] =
				OMAP_MMC_READ(host, RSP0) |
				(OMAP_MMC_READ(host, RSP1) << 16);
			cmd->resp[2] =
				OMAP_MMC_READ(host, RSP2) |
				(OMAP_MMC_READ(host, RSP3) << 16);
			cmd->resp[1] =
				OMAP_MMC_READ(host, RSP4) |
				(OMAP_MMC_READ(host, RSP5) << 16);
			cmd->resp[0] =
				OMAP_MMC_READ(host, RSP6) |
				(OMAP_MMC_READ(host, RSP7) << 16);
		} else {
			/* response types 1, 1b, 3, 4, 5, 6 */
			cmd->resp[0] =
				OMAP_MMC_READ(host, RSP6) |
				(OMAP_MMC_READ(host, RSP7) << 16);
		}
	}

	if (host->data == NULL || cmd->error != MMC_ERR_NONE) {
		host->mrq = NULL;
		clk_disable(host->fclk);
		mmc_request_done(host->mmc, cmd->mrq);
	}
}

/* PIO only */
static void
mmc_omap_sg_to_buf(struct mmc_omap_host *host)
{
	struct scatterlist *sg;

	sg = host->data->sg + host->sg_idx;
	host->buffer_bytes_left = sg->length;
	host->buffer = page_address(sg->page) + sg->offset;
	if (host->buffer_bytes_left > host->total_bytes_left)
		host->buffer_bytes_left = host->total_bytes_left;
}

/* PIO only */
static void
mmc_omap_xfer_data(struct mmc_omap_host *host, int write)
{
	int n;

	if (host->buffer_bytes_left == 0) {
		host->sg_idx++;
		BUG_ON(host->sg_idx == host->sg_len);
		mmc_omap_sg_to_buf(host);
	}
	n = 64;
	if (n > host->buffer_bytes_left)
		n = host->buffer_bytes_left;
	host->buffer_bytes_left -= n;
	host->total_bytes_left -= n;
	host->data->bytes_xfered += n;

	if (write) {
		__raw_writesw(host->virt_base + OMAP_MMC_REG_DATA, host->buffer, n);
	} else {
		__raw_readsw(host->virt_base + OMAP_MMC_REG_DATA, host->buffer, n);
	}
}

static inline void mmc_omap_report_irq(u16 status)
{
	static const char *mmc_omap_status_bits[] = {
		"EOC", "CD", "CB", "BRS", "EOFB", "DTO", "DCRC", "CTO",
		"CCRC", "CRW", "AF", "AE", "OCRB", "CIRQ", "CERR"
	};
	int i, c = 0;

	for (i = 0; i < ARRAY_SIZE(mmc_omap_status_bits); i++)
		if (status & (1 << i)) {
			if (c)
				printk(" ");
			printk("%s", mmc_omap_status_bits[i]);
			c++;
		}
}

static irqreturn_t mmc_omap_irq(int irq, void *dev_id)
{
	struct mmc_omap_host * host = (struct mmc_omap_host *)dev_id;
	u16 status;
	int end_command;
	int end_transfer;
	int transfer_error;

	if (host->cmd == NULL && host->data == NULL) {
		status = OMAP_MMC_READ(host, STAT);
		dev_info(mmc_dev(host->mmc),"spurious irq 0x%04x\n", status);
		if (status != 0) {
			OMAP_MMC_WRITE(host, STAT, status);
			OMAP_MMC_WRITE(host, IE, 0);
		}
		return IRQ_HANDLED;
	}

	end_command = 0;
	end_transfer = 0;
	transfer_error = 0;

	while ((status = OMAP_MMC_READ(host, STAT)) != 0) {
		OMAP_MMC_WRITE(host, STAT, status);
#ifdef CONFIG_MMC_DEBUG
		dev_dbg(mmc_dev(host->mmc), "MMC IRQ %04x (CMD %d): ",
			status, host->cmd != NULL ? host->cmd->opcode : -1);
		mmc_omap_report_irq(status);
		printk("\n");
#endif
		if (host->total_bytes_left) {
			if ((status & OMAP_MMC_STAT_A_FULL) ||
			    (status & OMAP_MMC_STAT_END_OF_DATA))
				mmc_omap_xfer_data(host, 0);
			if (status & OMAP_MMC_STAT_A_EMPTY)
				mmc_omap_xfer_data(host, 1);
		}

		if (status & OMAP_MMC_STAT_END_OF_DATA) {
			end_transfer = 1;
		}

		if (status & OMAP_MMC_STAT_DATA_TOUT) {
			dev_dbg(mmc_dev(host->mmc), "data timeout\n");
			if (host->data) {
				host->data->error |= MMC_ERR_TIMEOUT;
				transfer_error = 1;
			}
		}

		if (status & OMAP_MMC_STAT_DATA_CRC) {
			if (host->data) {
				host->data->error |= MMC_ERR_BADCRC;
				dev_dbg(mmc_dev(host->mmc),
					 "data CRC error, bytes left %d\n",
					host->total_bytes_left);
				transfer_error = 1;
			} else {
				dev_dbg(mmc_dev(host->mmc), "data CRC error\n");
			}
		}

		if (status & OMAP_MMC_STAT_CMD_TOUT) {
			/* Timeouts are routine with some commands */
			if (host->cmd) {
				if (host->cmd->opcode != MMC_ALL_SEND_CID &&
						host->cmd->opcode !=
						MMC_SEND_OP_COND &&
						host->cmd->opcode !=
						MMC_APP_CMD &&
						!mmc_omap_cover_is_open(host))
					dev_err(mmc_dev(host->mmc),
						"command timeout, CMD %d\n",
						host->cmd->opcode);
				host->cmd->error = MMC_ERR_TIMEOUT;
				end_command = 1;
			}
		}

		if (status & OMAP_MMC_STAT_CMD_CRC) {
			if (host->cmd) {
				dev_err(mmc_dev(host->mmc),
					"command CRC error (CMD%d, arg 0x%08x)\n",
					host->cmd->opcode, host->cmd->arg);
				host->cmd->error = MMC_ERR_BADCRC;
				end_command = 1;
			} else
				dev_err(mmc_dev(host->mmc),
					"command CRC error without cmd?\n");
		}

		if (status & OMAP_MMC_STAT_CARD_ERR) {
			if (host->cmd && host->cmd->opcode == MMC_STOP_TRANSMISSION) {
				u32 response = OMAP_MMC_READ(host, RSP6)
					| (OMAP_MMC_READ(host, RSP7) << 16);
				/* STOP sometimes sets must-ignore bits */
				if (!(response & (R1_CC_ERROR
								| R1_ILLEGAL_COMMAND
								| R1_COM_CRC_ERROR))) {
					end_command = 1;
					continue;
				}
			}

			dev_dbg(mmc_dev(host->mmc), "card status error (CMD%d)\n",
				host->cmd->opcode);
			if (host->cmd) {
				host->cmd->error = MMC_ERR_FAILED;
				end_command = 1;
			}
			if (host->data) {
				host->data->error = MMC_ERR_FAILED;
				transfer_error = 1;
			}
		}

		/*
		 * NOTE: On 1610 the END_OF_CMD may come too early when
		 * starting a write 
		 */
		if ((status & OMAP_MMC_STAT_END_OF_CMD) &&
		    (!(status & OMAP_MMC_STAT_A_EMPTY))) {
			end_command = 1;
		}
	}

	if (end_command) {
		mmc_omap_cmd_done(host, host->cmd);
	}
	if (transfer_error)
		mmc_omap_xfer_done(host, host->data);
	else if (end_transfer)
		mmc_omap_end_of_data(host, host->data);

	return IRQ_HANDLED;
}

static irqreturn_t mmc_omap_switch_irq(int irq, void *dev_id)
{
	struct mmc_omap_host *host = (struct mmc_omap_host *) dev_id;

	schedule_work(&host->switch_work);

	return IRQ_HANDLED;
}

static void mmc_omap_switch_timer(unsigned long arg)
{
	struct mmc_omap_host *host = (struct mmc_omap_host *) arg;

	schedule_work(&host->switch_work);
}

static void mmc_omap_switch_handler(struct work_struct *work)
{
	struct mmc_omap_host *host = container_of(work, struct mmc_omap_host, switch_work);
	struct mmc_card *card;
	static int complained = 0;
	int cards = 0, cover_open;

	if (host->switch_pin == -1)
		return;
	cover_open = mmc_omap_cover_is_open(host);
	if (cover_open != host->switch_last_state) {
		kobject_uevent(&host->dev->kobj, KOBJ_CHANGE);
		host->switch_last_state = cover_open;
	}
	mmc_detect_change(host->mmc, 0);
	list_for_each_entry(card, &host->mmc->cards, node) {
		if (mmc_card_present(card))
			cards++;
	}
	if (mmc_omap_cover_is_open(host)) {
		if (!complained) {
			dev_info(mmc_dev(host->mmc), "cover is open");
			complained = 1;
		}
		if (mmc_omap_enable_poll)
			mod_timer(&host->switch_timer, jiffies +
				msecs_to_jiffies(OMAP_MMC_SWITCH_POLL_DELAY));
	} else {
		complained = 0;
	}
}

/* Prepare to transfer the next segment of a scatterlist */
static void
mmc_omap_prepare_dma(struct mmc_omap_host *host, struct mmc_data *data)
{
	int dma_ch = host->dma_ch;
	unsigned long data_addr;
	u16 buf, frame;
	u32 count;
	struct scatterlist *sg = &data->sg[host->sg_idx];
	int src_port = 0;
	int dst_port = 0;
	int sync_dev = 0;

	data_addr = host->phys_base + OMAP_MMC_REG_DATA;
	frame = data->blksz;
	count = sg_dma_len(sg);

	if ((data->blocks == 1) && (count > data->blksz))
		count = frame;

	host->dma_len = count;

	/* FIFO is 16x2 bytes on 15xx, and 32x2 bytes on 16xx and 24xx.
	 * Use 16 or 32 word frames when the blocksize is at least that large.
	 * Blocksize is usually 512 bytes; but not for some SD reads.
	 */
	if (cpu_is_omap15xx() && frame > 32)
		frame = 32;
	else if (frame > 64)
		frame = 64;
	count /= frame;
	frame >>= 1;

	if (!(data->flags & MMC_DATA_WRITE)) {
		buf = 0x800f | ((frame - 1) << 8);

		if (cpu_class_is_omap1()) {
			src_port = OMAP_DMA_PORT_TIPB;
			dst_port = OMAP_DMA_PORT_EMIFF;
		}
		if (cpu_is_omap24xx())
			sync_dev = OMAP24XX_DMA_MMC1_RX;

		omap_set_dma_src_params(dma_ch, src_port,
					OMAP_DMA_AMODE_CONSTANT,
					data_addr, 0, 0);
		omap_set_dma_dest_params(dma_ch, dst_port,
					 OMAP_DMA_AMODE_POST_INC,
					 sg_dma_address(sg), 0, 0);
		omap_set_dma_dest_data_pack(dma_ch, 1);
		omap_set_dma_dest_burst_mode(dma_ch, OMAP_DMA_DATA_BURST_4);
	} else {
		buf = 0x0f80 | ((frame - 1) << 0);

		if (cpu_class_is_omap1()) {
			src_port = OMAP_DMA_PORT_EMIFF;
			dst_port = OMAP_DMA_PORT_TIPB;
		}
		if (cpu_is_omap24xx())
			sync_dev = OMAP24XX_DMA_MMC1_TX;

		omap_set_dma_dest_params(dma_ch, dst_port,
					 OMAP_DMA_AMODE_CONSTANT,
					 data_addr, 0, 0);
		omap_set_dma_src_params(dma_ch, src_port,
					OMAP_DMA_AMODE_POST_INC,
					sg_dma_address(sg), 0, 0);
		omap_set_dma_src_data_pack(dma_ch, 1);
		omap_set_dma_src_burst_mode(dma_ch, OMAP_DMA_DATA_BURST_4);
	}

	/* Max limit for DMA frame count is 0xffff */
	BUG_ON(count > 0xffff);

	OMAP_MMC_WRITE(host, BUF, buf);
	omap_set_dma_transfer_params(dma_ch, OMAP_DMA_DATA_TYPE_S16,
				     frame, count, OMAP_DMA_SYNC_FRAME,
				     sync_dev, 0);
}

/* A scatterlist segment completed */
static void mmc_omap_dma_cb(int lch, u16 ch_status, void *data)
{
	struct mmc_omap_host *host = (struct mmc_omap_host *) data;
	struct mmc_data *mmcdat = host->data;

	if (unlikely(host->dma_ch < 0)) {
		dev_err(mmc_dev(host->mmc),
			"DMA callback while DMA not enabled\n");
		return;
	}
	/* FIXME: We really should do something to _handle_ the errors */
	if (ch_status & OMAP1_DMA_TOUT_IRQ) {
		dev_err(mmc_dev(host->mmc),"DMA timeout\n");
		return;
	}
	if (ch_status & OMAP_DMA_DROP_IRQ) {
		dev_err(mmc_dev(host->mmc), "DMA sync error\n");
		return;
	}
	if (!(ch_status & OMAP_DMA_BLOCK_IRQ)) {
		return;
	}
	mmcdat->bytes_xfered += host->dma_len;
	host->sg_idx++;
	if (host->sg_idx < host->sg_len) {
		mmc_omap_prepare_dma(host, host->data);
		omap_start_dma(host->dma_ch);
	} else
		mmc_omap_dma_done(host, host->data);
}

static int mmc_omap_get_dma_channel(struct mmc_omap_host *host, struct mmc_data *data)
{
	const char *dev_name;
	int sync_dev, dma_ch, is_read, r;

	is_read = !(data->flags & MMC_DATA_WRITE);
	del_timer_sync(&host->dma_timer);
	if (host->dma_ch >= 0) {
		if (is_read == host->dma_is_read)
			return 0;
		omap_free_dma(host->dma_ch);
		host->dma_ch = -1;
	}

	if (is_read) {
		if (host->id == 1) {
			sync_dev = OMAP_DMA_MMC_RX;
			dev_name = "MMC1 read";
		} else {
			sync_dev = OMAP_DMA_MMC2_RX;
			dev_name = "MMC2 read";
		}
	} else {
		if (host->id == 1) {
			sync_dev = OMAP_DMA_MMC_TX;
			dev_name = "MMC1 write";
		} else {
			sync_dev = OMAP_DMA_MMC2_TX;
			dev_name = "MMC2 write";
		}
	}
	r = omap_request_dma(sync_dev, dev_name, mmc_omap_dma_cb,
			     host, &dma_ch);
	if (r != 0) {
		dev_dbg(mmc_dev(host->mmc), "omap_request_dma() failed with %d\n", r);
		return r;
	}
	host->dma_ch = dma_ch;
	host->dma_is_read = is_read;

	return 0;
}

static inline void set_cmd_timeout(struct mmc_omap_host *host, struct mmc_request *req)
{
	u16 reg;

	reg = OMAP_MMC_READ(host, SDIO);
	reg &= ~(1 << 5);
	OMAP_MMC_WRITE(host, SDIO, reg);
	/* Set maximum timeout */
	OMAP_MMC_WRITE(host, CTO, 0xff);
}

static inline void set_data_timeout(struct mmc_omap_host *host, struct mmc_request *req)
{
	int timeout;
	u16 reg;

	/* Convert ns to clock cycles by assuming 20MHz frequency
	 * 1 cycle at 20MHz = 500 ns
	 */
	timeout = req->data->timeout_clks + req->data->timeout_ns / 500;

	/* Check if we need to use timeout multiplier register */
	reg = OMAP_MMC_READ(host, SDIO);
	if (timeout > 0xffff) {
		reg |= (1 << 5);
		timeout /= 1024;
	} else
		reg &= ~(1 << 5);
	OMAP_MMC_WRITE(host, SDIO, reg);
	OMAP_MMC_WRITE(host, DTO, timeout);
}

static void
mmc_omap_prepare_data(struct mmc_omap_host *host, struct mmc_request *req)
{
	struct mmc_data *data = req->data;
	int i, use_dma, block_size;
	unsigned sg_len;

	host->data = data;
	if (data == NULL) {
		OMAP_MMC_WRITE(host, BLEN, 0);
		OMAP_MMC_WRITE(host, NBLK, 0);
		OMAP_MMC_WRITE(host, BUF, 0);
		host->dma_in_use = 0;
		set_cmd_timeout(host, req);
		return;
	}

	block_size = data->blksz;

	OMAP_MMC_WRITE(host, NBLK, data->blocks - 1);
	OMAP_MMC_WRITE(host, BLEN, block_size - 1);
	set_data_timeout(host, req);

	/* cope with calling layer confusion; it issues "single
	 * block" writes using multi-block scatterlists.
	 */
	sg_len = (data->blocks == 1) ? 1 : data->sg_len;

	/* Only do DMA for entire blocks */
	use_dma = host->use_dma;
	if (use_dma) {
		for (i = 0; i < sg_len; i++) {
			if ((data->sg[i].length % block_size) != 0) {
				use_dma = 0;
				break;
			}
		}
	}

	host->sg_idx = 0;
	if (use_dma) {
		if (mmc_omap_get_dma_channel(host, data) == 0) {
			enum dma_data_direction dma_data_dir;

			if (data->flags & MMC_DATA_WRITE)
				dma_data_dir = DMA_TO_DEVICE;
			else
				dma_data_dir = DMA_FROM_DEVICE;

			host->sg_len = dma_map_sg(mmc_dev(host->mmc), data->sg,
						sg_len, dma_data_dir);
			host->total_bytes_left = 0;
			mmc_omap_prepare_dma(host, req->data);
			host->brs_received = 0;
			host->dma_done = 0;
			host->dma_in_use = 1;
		} else
			use_dma = 0;
	}

	/* Revert to PIO? */
	if (!use_dma) {
		OMAP_MMC_WRITE(host, BUF, 0x1f1f);
		host->total_bytes_left = data->blocks * block_size;
		host->sg_len = sg_len;
		mmc_omap_sg_to_buf(host);
		host->dma_in_use = 0;
	}
}

static void mmc_omap_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct mmc_omap_host *host = mmc_priv(mmc);

	WARN_ON(host->mrq != NULL);

	host->mrq = req;

	/* only touch fifo AFTER the controller readies it */
	mmc_omap_prepare_data(host, req);
	mmc_omap_start_command(host, req->cmd);
	if (host->dma_in_use)
		omap_start_dma(host->dma_ch);
}

static void innovator_fpga_socket_power(int on)
{
#if defined(CONFIG_MACH_OMAP_INNOVATOR) && defined(CONFIG_ARCH_OMAP15XX)
	if (on) {
		fpga_write(fpga_read(OMAP1510_FPGA_POWER) | (1 << 3),
		     OMAP1510_FPGA_POWER);
	} else {
		fpga_write(fpga_read(OMAP1510_FPGA_POWER) & ~(1 << 3),
		     OMAP1510_FPGA_POWER);
	}
#endif
}

/*
 * Turn the socket power on/off. Innovator uses FPGA, most boards
 * probably use GPIO.
 */
static void mmc_omap_power(struct mmc_omap_host *host, int on)
{
	if (on) {
		if (machine_is_omap_innovator())
			innovator_fpga_socket_power(1);
		else if (machine_is_omap_h2())
			tps65010_set_gpio_out_value(GPIO3, HIGH);
		else if (machine_is_omap_h3())
			/* GPIO 4 of TPS65010 sends SD_EN signal */
			tps65010_set_gpio_out_value(GPIO4, HIGH);
		else if (cpu_is_omap24xx()) {
			u16 reg = OMAP_MMC_READ(host, CON);
			OMAP_MMC_WRITE(host, CON, reg | (1 << 11));
		} else
			if (host->power_pin >= 0)
				omap_set_gpio_dataout(host->power_pin, 1);
	} else {
		if (machine_is_omap_innovator())
			innovator_fpga_socket_power(0);
		else if (machine_is_omap_h2())
			tps65010_set_gpio_out_value(GPIO3, LOW);
		else if (machine_is_omap_h3())
			tps65010_set_gpio_out_value(GPIO4, LOW);
		else if (cpu_is_omap24xx()) {
			u16 reg = OMAP_MMC_READ(host, CON);
			OMAP_MMC_WRITE(host, CON, reg & ~(1 << 11));
		} else
			if (host->power_pin >= 0)
				omap_set_gpio_dataout(host->power_pin, 0);
	}
}

static void mmc_omap_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mmc_omap_host *host = mmc_priv(mmc);
	int dsor;
	int realclock, i;

	realclock = ios->clock;

	if (ios->clock == 0)
		dsor = 0;
	else {
		int func_clk_rate = clk_get_rate(host->fclk);

		dsor = func_clk_rate / realclock;
		if (dsor < 1)
			dsor = 1;

		if (func_clk_rate / dsor > realclock)
			dsor++;

		if (dsor > 250)
			dsor = 250;
		dsor++;

		if (ios->bus_width == MMC_BUS_WIDTH_4)
			dsor |= 1 << 15;
	}

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		mmc_omap_power(host, 0);
		break;
	case MMC_POWER_UP:
	case MMC_POWER_ON:
		mmc_omap_power(host, 1);
		dsor |= 1 << 11;
		break;
	}

	host->bus_mode = ios->bus_mode;
	host->hw_bus_mode = host->bus_mode;

	clk_enable(host->fclk);

	/* On insanely high arm_per frequencies something sometimes
	 * goes somehow out of sync, and the POW bit is not being set,
	 * which results in the while loop below getting stuck.
	 * Writing to the CON register twice seems to do the trick. */
	for (i = 0; i < 2; i++)
		OMAP_MMC_WRITE(host, CON, dsor);
	if (ios->power_mode == MMC_POWER_UP) {
		/* Send clock cycles, poll completion */
		OMAP_MMC_WRITE(host, IE, 0);
		OMAP_MMC_WRITE(host, STAT, 0xffff);
		OMAP_MMC_WRITE(host, CMD, 1 << 7);
		while ((OMAP_MMC_READ(host, STAT) & 1) == 0);
		OMAP_MMC_WRITE(host, STAT, 1);
	}
	clk_disable(host->fclk);
}

static int mmc_omap_get_ro(struct mmc_host *mmc)
{
	struct mmc_omap_host *host = mmc_priv(mmc);

	return host->wp_pin && omap_get_gpio_datain(host->wp_pin);
}

static const struct mmc_host_ops mmc_omap_ops = {
	.request	= mmc_omap_request,
	.set_ios	= mmc_omap_set_ios,
	.get_ro		= mmc_omap_get_ro,
};

static int __init mmc_omap_probe(struct platform_device *pdev)
{
	struct omap_mmc_conf *minfo = pdev->dev.platform_data;
	struct mmc_host *mmc;
	struct mmc_omap_host *host = NULL;
	struct resource *res;
	int ret = 0;
	int irq;

	if (minfo == NULL) {
		dev_err(&pdev->dev, "platform data missing\n");
		return -ENXIO;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (res == NULL || irq < 0)
		return -ENXIO;

	res = request_mem_region(res->start, res->end - res->start + 1,
			         pdev->name);
	if (res == NULL)
		return -EBUSY;

	mmc = mmc_alloc_host(sizeof(struct mmc_omap_host), &pdev->dev);
	if (mmc == NULL) {
		ret = -ENOMEM;
		goto err_free_mem_region;
	}

	host = mmc_priv(mmc);
	host->mmc = mmc;

	spin_lock_init(&host->dma_lock);
	init_timer(&host->dma_timer);
	host->dma_timer.function = mmc_omap_dma_timer;
	host->dma_timer.data = (unsigned long) host;

	host->id = pdev->id;
	host->mem_res = res;
	host->irq = irq;

	if (cpu_is_omap24xx()) {
		host->iclk = clk_get(&pdev->dev, "mmc_ick");
		if (IS_ERR(host->iclk))
			goto err_free_mmc_host;
		clk_enable(host->iclk);
	}

	if (!cpu_is_omap24xx())
		host->fclk = clk_get(&pdev->dev, "mmc_ck");
	else
		host->fclk = clk_get(&pdev->dev, "mmc_fck");

	if (IS_ERR(host->fclk)) {
		ret = PTR_ERR(host->fclk);
		goto err_free_iclk;
	}

	/* REVISIT:
	 * Also, use minfo->cover to decide how to manage
	 * the card detect sensing.
	 */
	host->power_pin = minfo->power_pin;
	host->switch_pin = minfo->switch_pin;
	host->wp_pin = minfo->wp_pin;
	host->use_dma = 1;
	host->dma_ch = -1;

	host->irq = irq;
	host->phys_base = host->mem_res->start;
	host->virt_base = (void __iomem *) IO_ADDRESS(host->phys_base);

	mmc->ops = &mmc_omap_ops;
	mmc->f_min = 400000;
	mmc->f_max = 24000000;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->caps = MMC_CAP_MULTIWRITE | MMC_CAP_BYTEBLOCK;

	if (minfo->wire4)
		 mmc->caps |= MMC_CAP_4_BIT_DATA;

	/* Use scatterlist DMA to reduce per-transfer costs.
	 * NOTE max_seg_size assumption that small blocks aren't
	 * normally used (except e.g. for reading SD registers).
	 */
	mmc->max_phys_segs = 32;
	mmc->max_hw_segs = 32;
	mmc->max_blk_size = 2048;	/* BLEN is 11 bits (+1) */
	mmc->max_blk_count = 2048;	/* NBLK is 11 bits (+1) */
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_seg_size = mmc->max_req_size;

	if (host->power_pin >= 0) {
		if ((ret = omap_request_gpio(host->power_pin)) != 0) {
			dev_err(mmc_dev(host->mmc),
				"Unable to get GPIO pin for MMC power\n");
			goto err_free_fclk;
		}
		omap_set_gpio_direction(host->power_pin, 0);
	}

	ret = request_irq(host->irq, mmc_omap_irq, 0, DRIVER_NAME, host);
	if (ret)
		goto err_free_power_gpio;

	host->dev = &pdev->dev;
	platform_set_drvdata(pdev, host);

	if (host->switch_pin >= 0) {
		INIT_WORK(&host->switch_work, mmc_omap_switch_handler);
		init_timer(&host->switch_timer);
		host->switch_timer.function = mmc_omap_switch_timer;
		host->switch_timer.data = (unsigned long) host;
		if (omap_request_gpio(host->switch_pin) != 0) {
			dev_warn(mmc_dev(host->mmc), "Unable to get GPIO pin for MMC cover switch\n");
			host->switch_pin = -1;
			goto no_switch;
		}

		omap_set_gpio_direction(host->switch_pin, 1);
		ret = request_irq(OMAP_GPIO_IRQ(host->switch_pin),
				  mmc_omap_switch_irq, IRQF_TRIGGER_RISING, DRIVER_NAME, host);
		if (ret) {
			dev_warn(mmc_dev(host->mmc), "Unable to get IRQ for MMC cover switch\n");
			omap_free_gpio(host->switch_pin);
			host->switch_pin = -1;
			goto no_switch;
		}
		ret = device_create_file(&pdev->dev, &dev_attr_cover_switch);
		if (ret == 0) {
			ret = device_create_file(&pdev->dev, &dev_attr_enable_poll);
			if (ret != 0)
				device_remove_file(&pdev->dev, &dev_attr_cover_switch);
		}
		if (ret) {
			dev_warn(mmc_dev(host->mmc), "Unable to create sysfs attributes\n");
			free_irq(OMAP_GPIO_IRQ(host->switch_pin), host);
			omap_free_gpio(host->switch_pin);
			host->switch_pin = -1;
			goto no_switch;
		}
		if (mmc_omap_enable_poll && mmc_omap_cover_is_open(host))
			schedule_work(&host->switch_work);
	}

	mmc_add_host(mmc);

	return 0;

no_switch:
	/* FIXME: Free other resources too. */
	if (host) {
		if (host->iclk && !IS_ERR(host->iclk))
			clk_put(host->iclk);
		if (host->fclk && !IS_ERR(host->fclk))
			clk_put(host->fclk);
		mmc_free_host(host->mmc);
	}
err_free_power_gpio:
	if (host->power_pin >= 0)
		omap_free_gpio(host->power_pin);
err_free_fclk:
	clk_put(host->fclk);
err_free_iclk:
	if (host->iclk != NULL) {
		clk_disable(host->iclk);
		clk_put(host->iclk);
	}
err_free_mmc_host:
	mmc_free_host(host->mmc);
err_free_mem_region:
	release_mem_region(res->start, res->end - res->start + 1);
	return ret;
}

static int mmc_omap_remove(struct platform_device *pdev)
{
	struct mmc_omap_host *host = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	BUG_ON(host == NULL);

	mmc_remove_host(host->mmc);
	free_irq(host->irq, host);

	if (host->power_pin >= 0)
		omap_free_gpio(host->power_pin);
	if (host->switch_pin >= 0) {
		device_remove_file(&pdev->dev, &dev_attr_enable_poll);
		device_remove_file(&pdev->dev, &dev_attr_cover_switch);
		free_irq(OMAP_GPIO_IRQ(host->switch_pin), host);
		omap_free_gpio(host->switch_pin);
		host->switch_pin = -1;
		del_timer_sync(&host->switch_timer);
		flush_scheduled_work();
	}
	if (host->iclk && !IS_ERR(host->iclk))
		clk_put(host->iclk);
	if (host->fclk && !IS_ERR(host->fclk))
		clk_put(host->fclk);

	release_mem_region(pdev->resource[0].start,
			   pdev->resource[0].end - pdev->resource[0].start + 1);

	mmc_free_host(host->mmc);

	return 0;
}

#ifdef CONFIG_PM
static int mmc_omap_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	int ret = 0;
	struct mmc_omap_host *host = platform_get_drvdata(pdev);

	if (host && host->suspended)
		return 0;

	if (host) {
		ret = mmc_suspend_host(host->mmc, mesg);
		if (ret == 0)
			host->suspended = 1;
	}
	return ret;
}

static int mmc_omap_resume(struct platform_device *pdev)
{
	int ret = 0;
	struct mmc_omap_host *host = platform_get_drvdata(pdev);

	if (host && !host->suspended)
		return 0;

	if (host) {
		ret = mmc_resume_host(host->mmc);
		if (ret == 0)
			host->suspended = 0;
	}

	return ret;
}
#else
#define mmc_omap_suspend	NULL
#define mmc_omap_resume		NULL
#endif

static struct platform_driver mmc_omap_driver = {
	.probe		= mmc_omap_probe,
	.remove		= mmc_omap_remove,
	.suspend	= mmc_omap_suspend,
	.resume		= mmc_omap_resume,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

static int __init mmc_omap_init(void)
{
	return platform_driver_register(&mmc_omap_driver);
}

static void __exit mmc_omap_exit(void)
{
	platform_driver_unregister(&mmc_omap_driver);
}

module_init(mmc_omap_init);
module_exit(mmc_omap_exit);

MODULE_DESCRIPTION("OMAP Multimedia Card driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS(DRIVER_NAME);
MODULE_AUTHOR("Juha Yrjölä");
