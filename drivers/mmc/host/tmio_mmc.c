/*
 *  linux/drivers/mmc/tmio_mmc.c
 *
 *  Copyright (C) 2004 Ian Molton
 *  Copyright (C) 2007 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver for the MMC / SD / SDIO cell found in:
 *
 * TC6393XB TC6391XB TC6387XB T7L66XB ASIC3
 *
 * This driver draws mainly on scattered spec sheets, Reverse engineering
 * of the toshiba e800  SD driver and some parts of the 2.4 ASIC3 driver (4 bit
 * support). (Further 4 bit support from a later datasheet).
 *
 * TODO:
 *   Investigate using a workqueue for PIO transfers
 *   Eliminate FIXMEs
 *   SDIO support
 *   Better Power management
 *   Handle MMC errors better
 *   double buffer support
 *
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/scatterlist.h>

#define CTL_SD_CMD 0x00
#define CTL_ARG_REG 0x04
#define CTL_STOP_INTERNAL_ACTION 0x08
#define CTL_XFER_BLK_COUNT 0xa
#define CTL_RESPONSE 0x0c
#define CTL_STATUS 0x1c
#define CTL_IRQ_MASK 0x20
#define CTL_SD_CARD_CLK_CTL 0x24
#define CTL_SD_XFER_LEN 0x26
#define CTL_SD_MEM_CARD_OPT 0x28
#define CTL_SD_ERROR_DETAIL_STATUS 0x2c
#define CTL_SD_DATA_PORT 0x30
#define CTL_TRANSACTION_CTL 0x34
#define CTL_RESET_SD 0xe0
#define CTL_SDIO_REGS 0x100
#define CTL_CLK_AND_WAIT_CTL 0x138
#define CTL_RESET_SDIO 0x1e0

/* Definitions for values the CTRL_STATUS register can take. */
#define TMIO_STAT_CMDRESPEND    0x00000001
#define TMIO_STAT_DATAEND       0x00000004
#define TMIO_STAT_CARD_REMOVE   0x00000008
#define TMIO_STAT_CARD_INSERT   0x00000010
#define TMIO_STAT_SIGSTATE      0x00000020
#define TMIO_STAT_WRPROTECT     0x00000080
#define TMIO_STAT_CARD_REMOVE_A 0x00000100
#define TMIO_STAT_CARD_INSERT_A 0x00000200
#define TMIO_STAT_SIGSTATE_A    0x00000400
#define TMIO_STAT_CMD_IDX_ERR   0x00010000
#define TMIO_STAT_CRCFAIL       0x00020000
#define TMIO_STAT_STOPBIT_ERR   0x00040000
#define TMIO_STAT_DATATIMEOUT   0x00080000
#define TMIO_STAT_RXOVERFLOW    0x00100000
#define TMIO_STAT_TXUNDERRUN    0x00200000
#define TMIO_STAT_CMDTIMEOUT    0x00400000
#define TMIO_STAT_RXRDY         0x01000000
#define TMIO_STAT_TXRQ          0x02000000
#define TMIO_STAT_ILL_FUNC      0x20000000
#define TMIO_STAT_CMD_BUSY      0x40000000
#define TMIO_STAT_ILL_ACCESS    0x80000000

/* Define some IRQ masks */
/* This is the mask used at reset by the chip */
#define TMIO_MASK_ALL           0x837f031d
#define TMIO_MASK_READOP  (TMIO_STAT_RXRDY | TMIO_STAT_DATAEND)
#define TMIO_MASK_WRITEOP (TMIO_STAT_TXRQ | TMIO_STAT_DATAEND)
#define TMIO_MASK_CMD     (TMIO_STAT_CMDRESPEND | TMIO_STAT_CMDTIMEOUT | \
		TMIO_STAT_CARD_REMOVE | TMIO_STAT_CARD_INSERT)
#define TMIO_MASK_IRQ     (TMIO_MASK_READOP | TMIO_MASK_WRITEOP | TMIO_MASK_CMD)

#define enable_mmc_irqs(host, i) \
	do { \
		u32 mask;\
		mask  = sd_ctrl_read32((host), CTL_IRQ_MASK); \
		mask &= ~((i) & TMIO_MASK_IRQ); \
		sd_ctrl_write32((host), CTL_IRQ_MASK, mask); \
	} while (0)

#define disable_mmc_irqs(host, i) \
	do { \
		u32 mask;\
		mask  = sd_ctrl_read32((host), CTL_IRQ_MASK); \
		mask |= ((i) & TMIO_MASK_IRQ); \
		sd_ctrl_write32((host), CTL_IRQ_MASK, mask); \
	} while (0)

#define ack_mmc_irqs(host, i) \
	do { \
		sd_ctrl_write32((host), CTL_STATUS, ~(i)); \
	} while (0)


struct tmio_mmc_host {
	void __iomem *ctl;
	unsigned long bus_shift;
	struct mmc_command      *cmd;
	struct mmc_request      *mrq;
	struct mmc_data         *data;
	struct mmc_host         *mmc;
	int                     irq;

	/* Callbacks for clock / power control */
	void (*set_pwr)(struct platform_device *host, int state);
	void (*set_clk_div)(struct platform_device *host, int state);

	/* pio related stuff */
	struct scatterlist      *sg_ptr;
	unsigned int            sg_len;
	unsigned int            sg_off;

	struct platform_device *pdev;

	/* DMA support */
	struct dma_chan		*chan_rx;
	struct dma_chan		*chan_tx;
	struct tasklet_struct	dma_complete;
	struct tasklet_struct	dma_issue;
#ifdef CONFIG_TMIO_MMC_DMA
	unsigned int            dma_sglen;
#endif
};

static u16 sd_ctrl_read16(struct tmio_mmc_host *host, int addr)
{
	return readw(host->ctl + (addr << host->bus_shift));
}

static void sd_ctrl_read16_rep(struct tmio_mmc_host *host, int addr,
		u16 *buf, int count)
{
	readsw(host->ctl + (addr << host->bus_shift), buf, count);
}

static u32 sd_ctrl_read32(struct tmio_mmc_host *host, int addr)
{
	return readw(host->ctl + (addr << host->bus_shift)) |
	       readw(host->ctl + ((addr + 2) << host->bus_shift)) << 16;
}

static void sd_ctrl_write16(struct tmio_mmc_host *host, int addr, u16 val)
{
	writew(val, host->ctl + (addr << host->bus_shift));
}

static void sd_ctrl_write16_rep(struct tmio_mmc_host *host, int addr,
		u16 *buf, int count)
{
	writesw(host->ctl + (addr << host->bus_shift), buf, count);
}

static void sd_ctrl_write32(struct tmio_mmc_host *host, int addr, u32 val)
{
	writew(val, host->ctl + (addr << host->bus_shift));
	writew(val >> 16, host->ctl + ((addr + 2) << host->bus_shift));
}

static void tmio_mmc_init_sg(struct tmio_mmc_host *host, struct mmc_data *data)
{
	host->sg_len = data->sg_len;
	host->sg_ptr = data->sg;
	host->sg_off = 0;
}

static int tmio_mmc_next_sg(struct tmio_mmc_host *host)
{
	host->sg_ptr = sg_next(host->sg_ptr);
	host->sg_off = 0;
	return --host->sg_len;
}

static char *tmio_mmc_kmap_atomic(struct scatterlist *sg, unsigned long *flags)
{
	local_irq_save(*flags);
	return kmap_atomic(sg_page(sg), KM_BIO_SRC_IRQ) + sg->offset;
}

static void tmio_mmc_kunmap_atomic(void *virt, unsigned long *flags)
{
	kunmap_atomic(virt, KM_BIO_SRC_IRQ);
	local_irq_restore(*flags);
}

#ifdef CONFIG_MMC_DEBUG

#define STATUS_TO_TEXT(a) \
	do { \
		if (status & TMIO_STAT_##a) \
			printk(#a); \
	} while (0)

void pr_debug_status(u32 status)
{
	printk(KERN_DEBUG "status: %08x = ", status);
	STATUS_TO_TEXT(CARD_REMOVE);
	STATUS_TO_TEXT(CARD_INSERT);
	STATUS_TO_TEXT(SIGSTATE);
	STATUS_TO_TEXT(WRPROTECT);
	STATUS_TO_TEXT(CARD_REMOVE_A);
	STATUS_TO_TEXT(CARD_INSERT_A);
	STATUS_TO_TEXT(SIGSTATE_A);
	STATUS_TO_TEXT(CMD_IDX_ERR);
	STATUS_TO_TEXT(STOPBIT_ERR);
	STATUS_TO_TEXT(ILL_FUNC);
	STATUS_TO_TEXT(CMD_BUSY);
	STATUS_TO_TEXT(CMDRESPEND);
	STATUS_TO_TEXT(DATAEND);
	STATUS_TO_TEXT(CRCFAIL);
	STATUS_TO_TEXT(DATATIMEOUT);
	STATUS_TO_TEXT(CMDTIMEOUT);
	STATUS_TO_TEXT(RXOVERFLOW);
	STATUS_TO_TEXT(TXUNDERRUN);
	STATUS_TO_TEXT(RXRDY);
	STATUS_TO_TEXT(TXRQ);
	STATUS_TO_TEXT(ILL_ACCESS);
	printk("\n");
}

#else
#define pr_debug_status(s)  do { } while (0)
#endif

static void tmio_mmc_set_clock(struct tmio_mmc_host *host, int new_clock)
{
	u32 clk = 0, clock;

	if (new_clock) {
		for (clock = host->mmc->f_min, clk = 0x80000080;
			new_clock >= (clock<<1); clk >>= 1)
			clock <<= 1;
		clk |= 0x100;
	}

	if (host->set_clk_div)
		host->set_clk_div(host->pdev, (clk>>22) & 1);

	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, clk & 0x1ff);
}

static void tmio_mmc_clk_stop(struct tmio_mmc_host *host)
{
	sd_ctrl_write16(host, CTL_CLK_AND_WAIT_CTL, 0x0000);
	msleep(10);
	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, ~0x0100 &
		sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));
	msleep(10);
}

static void tmio_mmc_clk_start(struct tmio_mmc_host *host)
{
	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, 0x0100 |
		sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));
	msleep(10);
	sd_ctrl_write16(host, CTL_CLK_AND_WAIT_CTL, 0x0100);
	msleep(10);
}

static void reset(struct tmio_mmc_host *host)
{
	/* FIXME - should we set stop clock reg here */
	sd_ctrl_write16(host, CTL_RESET_SD, 0x0000);
	sd_ctrl_write16(host, CTL_RESET_SDIO, 0x0000);
	msleep(10);
	sd_ctrl_write16(host, CTL_RESET_SD, 0x0001);
	sd_ctrl_write16(host, CTL_RESET_SDIO, 0x0001);
	msleep(10);
}

static void
tmio_mmc_finish_request(struct tmio_mmc_host *host)
{
	struct mmc_request *mrq = host->mrq;

	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;

	mmc_request_done(host->mmc, mrq);
}

/* These are the bitmasks the tmio chip requires to implement the MMC response
 * types. Note that R1 and R6 are the same in this scheme. */
#define APP_CMD        0x0040
#define RESP_NONE      0x0300
#define RESP_R1        0x0400
#define RESP_R1B       0x0500
#define RESP_R2        0x0600
#define RESP_R3        0x0700
#define DATA_PRESENT   0x0800
#define TRANSFER_READ  0x1000
#define TRANSFER_MULTI 0x2000
#define SECURITY_CMD   0x4000

static int
tmio_mmc_start_command(struct tmio_mmc_host *host, struct mmc_command *cmd)
{
	struct mmc_data *data = host->data;
	int c = cmd->opcode;

	/* Command 12 is handled by hardware */
	if (cmd->opcode == 12 && !cmd->arg) {
		sd_ctrl_write16(host, CTL_STOP_INTERNAL_ACTION, 0x001);
		return 0;
	}

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE: c |= RESP_NONE; break;
	case MMC_RSP_R1:   c |= RESP_R1;   break;
	case MMC_RSP_R1B:  c |= RESP_R1B;  break;
	case MMC_RSP_R2:   c |= RESP_R2;   break;
	case MMC_RSP_R3:   c |= RESP_R3;   break;
	default:
		pr_debug("Unknown response type %d\n", mmc_resp_type(cmd));
		return -EINVAL;
	}

	host->cmd = cmd;

/* FIXME - this seems to be ok commented out but the spec suggest this bit
 *         should be set when issuing app commands.
 *	if(cmd->flags & MMC_FLAG_ACMD)
 *		c |= APP_CMD;
 */
	if (data) {
		c |= DATA_PRESENT;
		if (data->blocks > 1) {
			sd_ctrl_write16(host, CTL_STOP_INTERNAL_ACTION, 0x100);
			c |= TRANSFER_MULTI;
		}
		if (data->flags & MMC_DATA_READ)
			c |= TRANSFER_READ;
	}

	enable_mmc_irqs(host, TMIO_MASK_CMD);

	/* Fire off the command */
	sd_ctrl_write32(host, CTL_ARG_REG, cmd->arg);
	sd_ctrl_write16(host, CTL_SD_CMD, c);

	return 0;
}

/*
 * This chip always returns (at least?) as much data as you ask for.
 * I'm unsure what happens if you ask for less than a block. This should be
 * looked into to ensure that a funny length read doesnt hose the controller.
 */
static void tmio_mmc_pio_irq(struct tmio_mmc_host *host)
{
	struct mmc_data *data = host->data;
	void *sg_virt;
	unsigned short *buf;
	unsigned int count;
	unsigned long flags;

	if (!data) {
		pr_debug("Spurious PIO IRQ\n");
		return;
	}

	sg_virt = tmio_mmc_kmap_atomic(host->sg_ptr, &flags);
	buf = (unsigned short *)(sg_virt + host->sg_off);

	count = host->sg_ptr->length - host->sg_off;
	if (count > data->blksz)
		count = data->blksz;

	pr_debug("count: %08x offset: %08x flags %08x\n",
		 count, host->sg_off, data->flags);

	/* Transfer the data */
	if (data->flags & MMC_DATA_READ)
		sd_ctrl_read16_rep(host, CTL_SD_DATA_PORT, buf, count >> 1);
	else
		sd_ctrl_write16_rep(host, CTL_SD_DATA_PORT, buf, count >> 1);

	host->sg_off += count;

	tmio_mmc_kunmap_atomic(sg_virt, &flags);

	if (host->sg_off == host->sg_ptr->length)
		tmio_mmc_next_sg(host);

	return;
}

static void tmio_mmc_do_data_irq(struct tmio_mmc_host *host)
{
	struct mmc_data *data = host->data;
	struct mmc_command *stop;

	host->data = NULL;

	if (!data) {
		dev_warn(&host->pdev->dev, "Spurious data end IRQ\n");
		return;
	}
	stop = data->stop;

	/* FIXME - return correct transfer count on errors */
	if (!data->error)
		data->bytes_xfered = data->blocks * data->blksz;
	else
		data->bytes_xfered = 0;

	pr_debug("Completed data request\n");

	/*
	 * FIXME: other drivers allow an optional stop command of any given type
	 *        which we dont do, as the chip can auto generate them.
	 *        Perhaps we can be smarter about when to use auto CMD12 and
	 *        only issue the auto request when we know this is the desired
	 *        stop command, allowing fallback to the stop command the
	 *        upper layers expect. For now, we do what works.
	 */

	if (data->flags & MMC_DATA_READ) {
		if (!host->chan_rx)
			disable_mmc_irqs(host, TMIO_MASK_READOP);
		dev_dbg(&host->pdev->dev, "Complete Rx request %p\n",
			host->mrq);
	} else {
		if (!host->chan_tx)
			disable_mmc_irqs(host, TMIO_MASK_WRITEOP);
		dev_dbg(&host->pdev->dev, "Complete Tx request %p\n",
			host->mrq);
	}

	if (stop) {
		if (stop->opcode == 12 && !stop->arg)
			sd_ctrl_write16(host, CTL_STOP_INTERNAL_ACTION, 0x000);
		else
			BUG();
	}

	tmio_mmc_finish_request(host);
}

static void tmio_mmc_data_irq(struct tmio_mmc_host *host)
{
	struct mmc_data *data = host->data;

	if (!data)
		return;

	if (host->chan_tx && (data->flags & MMC_DATA_WRITE)) {
		/*
		 * Has all data been written out yet? Testing on SuperH showed,
		 * that in most cases the first interrupt comes already with the
		 * BUSY status bit clear, but on some operations, like mount or
		 * in the beginning of a write / sync / umount, there is one
		 * DATAEND interrupt with the BUSY bit set, in this cases
		 * waiting for one more interrupt fixes the problem.
		 */
		if (!(sd_ctrl_read32(host, CTL_STATUS) & TMIO_STAT_CMD_BUSY)) {
			disable_mmc_irqs(host, TMIO_STAT_DATAEND);
			tasklet_schedule(&host->dma_complete);
		}
	} else if (host->chan_rx && (data->flags & MMC_DATA_READ)) {
		disable_mmc_irqs(host, TMIO_STAT_DATAEND);
		tasklet_schedule(&host->dma_complete);
	} else {
		tmio_mmc_do_data_irq(host);
	}
}

static void tmio_mmc_cmd_irq(struct tmio_mmc_host *host,
	unsigned int stat)
{
	struct mmc_command *cmd = host->cmd;
	int i, addr;

	if (!host->cmd) {
		pr_debug("Spurious CMD irq\n");
		return;
	}

	host->cmd = NULL;

	/* This controller is sicker than the PXA one. Not only do we need to
	 * drop the top 8 bits of the first response word, we also need to
	 * modify the order of the response for short response command types.
	 */

	for (i = 3, addr = CTL_RESPONSE ; i >= 0 ; i--, addr += 4)
		cmd->resp[i] = sd_ctrl_read32(host, addr);

	if (cmd->flags &  MMC_RSP_136) {
		cmd->resp[0] = (cmd->resp[0] << 8) | (cmd->resp[1] >> 24);
		cmd->resp[1] = (cmd->resp[1] << 8) | (cmd->resp[2] >> 24);
		cmd->resp[2] = (cmd->resp[2] << 8) | (cmd->resp[3] >> 24);
		cmd->resp[3] <<= 8;
	} else if (cmd->flags & MMC_RSP_R3) {
		cmd->resp[0] = cmd->resp[3];
	}

	if (stat & TMIO_STAT_CMDTIMEOUT)
		cmd->error = -ETIMEDOUT;
	else if (stat & TMIO_STAT_CRCFAIL && cmd->flags & MMC_RSP_CRC)
		cmd->error = -EILSEQ;

	/* If there is data to handle we enable data IRQs here, and
	 * we will ultimatley finish the request in the data_end handler.
	 * If theres no data or we encountered an error, finish now.
	 */
	if (host->data && !cmd->error) {
		if (host->data->flags & MMC_DATA_READ) {
			if (!host->chan_rx)
				enable_mmc_irqs(host, TMIO_MASK_READOP);
		} else {
			struct dma_chan *chan = host->chan_tx;
			if (!chan)
				enable_mmc_irqs(host, TMIO_MASK_WRITEOP);
			else
				tasklet_schedule(&host->dma_issue);
		}
	} else {
		tmio_mmc_finish_request(host);
	}

	return;
}

static irqreturn_t tmio_mmc_irq(int irq, void *devid)
{
	struct tmio_mmc_host *host = devid;
	unsigned int ireg, irq_mask, status;

	pr_debug("MMC IRQ begin\n");

	status = sd_ctrl_read32(host, CTL_STATUS);
	irq_mask = sd_ctrl_read32(host, CTL_IRQ_MASK);
	ireg = status & TMIO_MASK_IRQ & ~irq_mask;

	pr_debug_status(status);
	pr_debug_status(ireg);

	if (!ireg) {
		disable_mmc_irqs(host, status & ~irq_mask);

		pr_warning("tmio_mmc: Spurious irq, disabling! "
			"0x%08x 0x%08x 0x%08x\n", status, irq_mask, ireg);
		pr_debug_status(status);

		goto out;
	}

	while (ireg) {
		/* Card insert / remove attempts */
		if (ireg & (TMIO_STAT_CARD_INSERT | TMIO_STAT_CARD_REMOVE)) {
			ack_mmc_irqs(host, TMIO_STAT_CARD_INSERT |
				TMIO_STAT_CARD_REMOVE);
			mmc_detect_change(host->mmc, msecs_to_jiffies(100));
		}

		/* CRC and other errors */
/*		if (ireg & TMIO_STAT_ERR_IRQ)
 *			handled |= tmio_error_irq(host, irq, stat);
 */

		/* Command completion */
		if (ireg & TMIO_MASK_CMD) {
			ack_mmc_irqs(host, TMIO_MASK_CMD);
			tmio_mmc_cmd_irq(host, status);
		}

		/* Data transfer */
		if (ireg & (TMIO_STAT_RXRDY | TMIO_STAT_TXRQ)) {
			ack_mmc_irqs(host, TMIO_STAT_RXRDY | TMIO_STAT_TXRQ);
			tmio_mmc_pio_irq(host);
		}

		/* Data transfer completion */
		if (ireg & TMIO_STAT_DATAEND) {
			ack_mmc_irqs(host, TMIO_STAT_DATAEND);
			tmio_mmc_data_irq(host);
		}

		/* Check status - keep going until we've handled it all */
		status = sd_ctrl_read32(host, CTL_STATUS);
		irq_mask = sd_ctrl_read32(host, CTL_IRQ_MASK);
		ireg = status & TMIO_MASK_IRQ & ~irq_mask;

		pr_debug("Status at end of loop: %08x\n", status);
		pr_debug_status(status);
	}
	pr_debug("MMC IRQ end\n");

out:
	return IRQ_HANDLED;
}

#ifdef CONFIG_TMIO_MMC_DMA
static void tmio_mmc_enable_dma(struct tmio_mmc_host *host, bool enable)
{
#if defined(CONFIG_SUPERH) || defined(CONFIG_ARCH_SHMOBILE)
	/* Switch DMA mode on or off - SuperH specific? */
	sd_ctrl_write16(host, 0xd8, enable ? 2 : 0);
#endif
}

static void tmio_dma_complete(void *arg)
{
	struct tmio_mmc_host *host = arg;

	dev_dbg(&host->pdev->dev, "Command completed\n");

	if (!host->data)
		dev_warn(&host->pdev->dev, "NULL data in DMA completion!\n");
	else
		enable_mmc_irqs(host, TMIO_STAT_DATAEND);
}

static void tmio_mmc_start_dma_rx(struct tmio_mmc_host *host)
{
	struct scatterlist *sg = host->sg_ptr;
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *chan = host->chan_rx;
	dma_cookie_t cookie;
	int ret;

	ret = dma_map_sg(&host->pdev->dev, sg, host->sg_len, DMA_FROM_DEVICE);
	if (ret > 0) {
		host->dma_sglen = ret;
		desc = chan->device->device_prep_slave_sg(chan, sg, ret,
			DMA_FROM_DEVICE, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	}

	if (desc) {
		desc->callback = tmio_dma_complete;
		desc->callback_param = host;
		cookie = desc->tx_submit(desc);
		if (cookie < 0) {
			desc = NULL;
			ret = cookie;
		} else {
			chan->device->device_issue_pending(chan);
		}
	}
	dev_dbg(&host->pdev->dev, "%s(): mapped %d -> %d, cookie %d, rq %p\n",
		__func__, host->sg_len, ret, cookie, host->mrq);

	if (!desc) {
		/* DMA failed, fall back to PIO */
		if (ret >= 0)
			ret = -EIO;
		host->chan_rx = NULL;
		dma_release_channel(chan);
		/* Free the Tx channel too */
		chan = host->chan_tx;
		if (chan) {
			host->chan_tx = NULL;
			dma_release_channel(chan);
		}
		dev_warn(&host->pdev->dev,
			 "DMA failed: %d, falling back to PIO\n", ret);
		tmio_mmc_enable_dma(host, false);
	}

	dev_dbg(&host->pdev->dev, "%s(): desc %p, cookie %d, sg[%d]\n", __func__,
		desc, cookie, host->sg_len);
}

static void tmio_mmc_start_dma_tx(struct tmio_mmc_host *host)
{
	struct scatterlist *sg = host->sg_ptr;
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *chan = host->chan_tx;
	dma_cookie_t cookie;
	int ret;

	ret = dma_map_sg(&host->pdev->dev, sg, host->sg_len, DMA_TO_DEVICE);
	if (ret > 0) {
		host->dma_sglen = ret;
		desc = chan->device->device_prep_slave_sg(chan, sg, ret,
			DMA_TO_DEVICE, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	}

	if (desc) {
		desc->callback = tmio_dma_complete;
		desc->callback_param = host;
		cookie = desc->tx_submit(desc);
		if (cookie < 0) {
			desc = NULL;
			ret = cookie;
		}
	}
	dev_dbg(&host->pdev->dev, "%s(): mapped %d -> %d, cookie %d, rq %p\n",
		__func__, host->sg_len, ret, cookie, host->mrq);

	if (!desc) {
		/* DMA failed, fall back to PIO */
		if (ret >= 0)
			ret = -EIO;
		host->chan_tx = NULL;
		dma_release_channel(chan);
		/* Free the Rx channel too */
		chan = host->chan_rx;
		if (chan) {
			host->chan_rx = NULL;
			dma_release_channel(chan);
		}
		dev_warn(&host->pdev->dev,
			 "DMA failed: %d, falling back to PIO\n", ret);
		tmio_mmc_enable_dma(host, false);
	}

	dev_dbg(&host->pdev->dev, "%s(): desc %p, cookie %d\n", __func__,
		desc, cookie);
}

static void tmio_mmc_start_dma(struct tmio_mmc_host *host,
			       struct mmc_data *data)
{
	if (data->flags & MMC_DATA_READ) {
		if (host->chan_rx)
			tmio_mmc_start_dma_rx(host);
	} else {
		if (host->chan_tx)
			tmio_mmc_start_dma_tx(host);
	}
}

static void tmio_issue_tasklet_fn(unsigned long priv)
{
	struct tmio_mmc_host *host = (struct tmio_mmc_host *)priv;
	struct dma_chan *chan = host->chan_tx;

	chan->device->device_issue_pending(chan);
}

static void tmio_tasklet_fn(unsigned long arg)
{
	struct tmio_mmc_host *host = (struct tmio_mmc_host *)arg;

	if (host->data->flags & MMC_DATA_READ)
		dma_unmap_sg(&host->pdev->dev, host->sg_ptr, host->dma_sglen,
			     DMA_FROM_DEVICE);
	else
		dma_unmap_sg(&host->pdev->dev, host->sg_ptr, host->dma_sglen,
			     DMA_TO_DEVICE);

	tmio_mmc_do_data_irq(host);
}

/* It might be necessary to make filter MFD specific */
static bool tmio_mmc_filter(struct dma_chan *chan, void *arg)
{
	dev_dbg(chan->device->dev, "%s: slave data %p\n", __func__, arg);
	chan->private = arg;
	return true;
}

static void tmio_mmc_request_dma(struct tmio_mmc_host *host,
				 struct tmio_mmc_data *pdata)
{
	/* We can only either use DMA for both Tx and Rx or not use it at all */
	if (pdata->dma) {
		dma_cap_mask_t mask;

		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);

		host->chan_tx = dma_request_channel(mask, tmio_mmc_filter,
						    pdata->dma->chan_priv_tx);
		dev_dbg(&host->pdev->dev, "%s: TX: got channel %p\n", __func__,
			host->chan_tx);

		if (!host->chan_tx)
			return;

		host->chan_rx = dma_request_channel(mask, tmio_mmc_filter,
						    pdata->dma->chan_priv_rx);
		dev_dbg(&host->pdev->dev, "%s: RX: got channel %p\n", __func__,
			host->chan_rx);

		if (!host->chan_rx) {
			dma_release_channel(host->chan_tx);
			host->chan_tx = NULL;
			return;
		}

		tasklet_init(&host->dma_complete, tmio_tasklet_fn, (unsigned long)host);
		tasklet_init(&host->dma_issue, tmio_issue_tasklet_fn, (unsigned long)host);

		tmio_mmc_enable_dma(host, true);
	}
}

static void tmio_mmc_release_dma(struct tmio_mmc_host *host)
{
	if (host->chan_tx) {
		struct dma_chan *chan = host->chan_tx;
		host->chan_tx = NULL;
		dma_release_channel(chan);
	}
	if (host->chan_rx) {
		struct dma_chan *chan = host->chan_rx;
		host->chan_rx = NULL;
		dma_release_channel(chan);
	}
}
#else
static void tmio_mmc_start_dma(struct tmio_mmc_host *host,
			       struct mmc_data *data)
{
}

static void tmio_mmc_request_dma(struct tmio_mmc_host *host,
				 struct tmio_mmc_data *pdata)
{
	host->chan_tx = NULL;
	host->chan_rx = NULL;
}

static void tmio_mmc_release_dma(struct tmio_mmc_host *host)
{
}
#endif

static int tmio_mmc_start_data(struct tmio_mmc_host *host,
	struct mmc_data *data)
{
	struct mfd_cell *cell = host->pdev->dev.platform_data;
	struct tmio_mmc_data *pdata = cell->driver_data;

	pr_debug("setup data transfer: blocksize %08x  nr_blocks %d\n",
		 data->blksz, data->blocks);

	/* Some hardware cannot perform 2 byte requests in 4 bit mode */
	if (host->mmc->ios.bus_width == MMC_BUS_WIDTH_4) {
		int blksz_2bytes = pdata->flags & TMIO_MMC_BLKSZ_2BYTES;

		if (data->blksz < 2 || (data->blksz < 4 && !blksz_2bytes)) {
			pr_err("%s: %d byte block unsupported in 4 bit mode\n",
			       mmc_hostname(host->mmc), data->blksz);
			return -EINVAL;
		}
	}

	tmio_mmc_init_sg(host, data);
	host->data = data;

	/* Set transfer length / blocksize */
	sd_ctrl_write16(host, CTL_SD_XFER_LEN, data->blksz);
	sd_ctrl_write16(host, CTL_XFER_BLK_COUNT, data->blocks);

	tmio_mmc_start_dma(host, data);

	return 0;
}

/* Process requests from the MMC layer */
static void tmio_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	int ret;

	if (host->mrq)
		pr_debug("request not null\n");

	host->mrq = mrq;

	if (mrq->data) {
		ret = tmio_mmc_start_data(host, mrq->data);
		if (ret)
			goto fail;
	}

	ret = tmio_mmc_start_command(host, mrq->cmd);
	if (!ret)
		return;

fail:
	mrq->cmd->error = ret;
	mmc_request_done(mmc, mrq);
}

/* Set MMC clock / power.
 * Note: This controller uses a simple divider scheme therefore it cannot
 * run a MMC card at full speed (20MHz). The max clock is 24MHz on SD, but as
 * MMC wont run that fast, it has to be clocked at 12MHz which is the next
 * slowest setting.
 */
static void tmio_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);

	if (ios->clock)
		tmio_mmc_set_clock(host, ios->clock);

	/* Power sequence - OFF -> ON -> UP */
	switch (ios->power_mode) {
	case MMC_POWER_OFF: /* power down SD bus */
		if (host->set_pwr)
			host->set_pwr(host->pdev, 0);
		tmio_mmc_clk_stop(host);
		break;
	case MMC_POWER_ON: /* power up SD bus */
		if (host->set_pwr)
			host->set_pwr(host->pdev, 1);
		break;
	case MMC_POWER_UP: /* start bus clock */
		tmio_mmc_clk_start(host);
		break;
	}

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		sd_ctrl_write16(host, CTL_SD_MEM_CARD_OPT, 0x80e0);
	break;
	case MMC_BUS_WIDTH_4:
		sd_ctrl_write16(host, CTL_SD_MEM_CARD_OPT, 0x00e0);
	break;
	}

	/* Let things settle. delay taken from winCE driver */
	udelay(140);
}

static int tmio_mmc_get_ro(struct mmc_host *mmc)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct mfd_cell	*cell = host->pdev->dev.platform_data;
	struct tmio_mmc_data *pdata = cell->driver_data;

	return ((pdata->flags & TMIO_MMC_WRPROTECT_DISABLE) ||
		(sd_ctrl_read32(host, CTL_STATUS) & TMIO_STAT_WRPROTECT)) ? 0 : 1;
}

static int tmio_mmc_get_cd(struct mmc_host *mmc)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct mfd_cell	*cell = host->pdev->dev.platform_data;
	struct tmio_mmc_data *pdata = cell->driver_data;

	if (!pdata->get_cd)
		return -ENOSYS;
	else
		return pdata->get_cd(host->pdev);
}

static const struct mmc_host_ops tmio_mmc_ops = {
	.request	= tmio_mmc_request,
	.set_ios	= tmio_mmc_set_ios,
	.get_ro         = tmio_mmc_get_ro,
	.get_cd		= tmio_mmc_get_cd,
};

#ifdef CONFIG_PM
static int tmio_mmc_suspend(struct platform_device *dev, pm_message_t state)
{
	struct mfd_cell	*cell = (struct mfd_cell *)dev->dev.platform_data;
	struct mmc_host *mmc = platform_get_drvdata(dev);
	int ret;

	ret = mmc_suspend_host(mmc);

	/* Tell MFD core it can disable us now.*/
	if (!ret && cell->disable)
		cell->disable(dev);

	return ret;
}

static int tmio_mmc_resume(struct platform_device *dev)
{
	struct mfd_cell	*cell = (struct mfd_cell *)dev->dev.platform_data;
	struct mmc_host *mmc = platform_get_drvdata(dev);
	int ret = 0;

	/* Tell the MFD core we are ready to be enabled */
	if (cell->resume) {
		ret = cell->resume(dev);
		if (ret)
			goto out;
	}

	mmc_resume_host(mmc);

out:
	return ret;
}
#else
#define tmio_mmc_suspend NULL
#define tmio_mmc_resume NULL
#endif

static int __devinit tmio_mmc_probe(struct platform_device *dev)
{
	struct mfd_cell	*cell = (struct mfd_cell *)dev->dev.platform_data;
	struct tmio_mmc_data *pdata;
	struct resource *res_ctl;
	struct tmio_mmc_host *host;
	struct mmc_host *mmc;
	int ret = -EINVAL;
	u32 irq_mask = TMIO_MASK_CMD;

	if (dev->num_resources != 2)
		goto out;

	res_ctl = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res_ctl)
		goto out;

	pdata = cell->driver_data;
	if (!pdata || !pdata->hclk)
		goto out;

	ret = -ENOMEM;

	mmc = mmc_alloc_host(sizeof(struct tmio_mmc_host), &dev->dev);
	if (!mmc)
		goto out;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->pdev = dev;
	platform_set_drvdata(dev, mmc);

	host->set_pwr = pdata->set_pwr;
	host->set_clk_div = pdata->set_clk_div;

	/* SD control register space size is 0x200, 0x400 for bus_shift=1 */
	host->bus_shift = resource_size(res_ctl) >> 10;

	host->ctl = ioremap(res_ctl->start, resource_size(res_ctl));
	if (!host->ctl)
		goto host_free;

	mmc->ops = &tmio_mmc_ops;
	mmc->caps = MMC_CAP_4_BIT_DATA | pdata->capabilities;
	mmc->f_max = pdata->hclk;
	mmc->f_min = mmc->f_max / 512;
	mmc->max_segs = 32;
	mmc->max_blk_size = 512;
	mmc->max_blk_count = (PAGE_CACHE_SIZE / mmc->max_blk_size) *
		mmc->max_segs;
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_seg_size = mmc->max_req_size;
	if (pdata->ocr_mask)
		mmc->ocr_avail = pdata->ocr_mask;
	else
		mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	/* Tell the MFD core we are ready to be enabled */
	if (cell->enable) {
		ret = cell->enable(dev);
		if (ret)
			goto unmap_ctl;
	}

	tmio_mmc_clk_stop(host);
	reset(host);

	ret = platform_get_irq(dev, 0);
	if (ret >= 0)
		host->irq = ret;
	else
		goto cell_disable;

	disable_mmc_irqs(host, TMIO_MASK_ALL);

	ret = request_irq(host->irq, tmio_mmc_irq, IRQF_DISABLED |
		IRQF_TRIGGER_FALLING, dev_name(&dev->dev), host);
	if (ret)
		goto cell_disable;

	/* See if we also get DMA */
	tmio_mmc_request_dma(host, pdata);

	mmc_add_host(mmc);

	pr_info("%s at 0x%08lx irq %d\n", mmc_hostname(host->mmc),
		(unsigned long)host->ctl, host->irq);

	/* Unmask the IRQs we want to know about */
	if (!host->chan_rx)
		irq_mask |= TMIO_MASK_READOP;
	if (!host->chan_tx)
		irq_mask |= TMIO_MASK_WRITEOP;
	enable_mmc_irqs(host, irq_mask);

	return 0;

cell_disable:
	if (cell->disable)
		cell->disable(dev);
unmap_ctl:
	iounmap(host->ctl);
host_free:
	mmc_free_host(mmc);
out:
	return ret;
}

static int __devexit tmio_mmc_remove(struct platform_device *dev)
{
	struct mfd_cell	*cell = (struct mfd_cell *)dev->dev.platform_data;
	struct mmc_host *mmc = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	if (mmc) {
		struct tmio_mmc_host *host = mmc_priv(mmc);
		mmc_remove_host(mmc);
		tmio_mmc_release_dma(host);
		free_irq(host->irq, host);
		if (cell->disable)
			cell->disable(dev);
		iounmap(host->ctl);
		mmc_free_host(mmc);
	}

	return 0;
}

/* ------------------- device registration ----------------------- */

static struct platform_driver tmio_mmc_driver = {
	.driver = {
		.name = "tmio-mmc",
		.owner = THIS_MODULE,
	},
	.probe = tmio_mmc_probe,
	.remove = __devexit_p(tmio_mmc_remove),
	.suspend = tmio_mmc_suspend,
	.resume = tmio_mmc_resume,
};


static int __init tmio_mmc_init(void)
{
	return platform_driver_register(&tmio_mmc_driver);
}

static void __exit tmio_mmc_exit(void)
{
	platform_driver_unregister(&tmio_mmc_driver);
}

module_init(tmio_mmc_init);
module_exit(tmio_mmc_exit);

MODULE_DESCRIPTION("Toshiba TMIO SD/MMC driver");
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:tmio-mmc");
