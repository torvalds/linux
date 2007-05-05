/*
 *  linux/drivers/mmc/sdhci.c - Secure Digital Host Controller Interface driver
 *
 *  Copyright (C) 2005-2007 Pierre Ossman, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>

#include <linux/mmc/host.h>

#include <asm/scatterlist.h>

#include "sdhci.h"

#define DRIVER_NAME "sdhci"

#define DBG(f, x...) \
	pr_debug(DRIVER_NAME " [%s()]: " f, __func__,## x)

static unsigned int debug_nodma = 0;
static unsigned int debug_forcedma = 0;
static unsigned int debug_quirks = 0;

#define SDHCI_QUIRK_CLOCK_BEFORE_RESET			(1<<0)
#define SDHCI_QUIRK_FORCE_DMA				(1<<1)
/* Controller doesn't like some resets when there is no card inserted. */
#define SDHCI_QUIRK_NO_CARD_NO_RESET			(1<<2)
#define SDHCI_QUIRK_SINGLE_POWER_WRITE			(1<<3)

static const struct pci_device_id pci_ids[] __devinitdata = {
	{
		.vendor		= PCI_VENDOR_ID_RICOH,
		.device		= PCI_DEVICE_ID_RICOH_R5C822,
		.subvendor	= PCI_VENDOR_ID_IBM,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= SDHCI_QUIRK_CLOCK_BEFORE_RESET |
				  SDHCI_QUIRK_FORCE_DMA,
	},

	{
		.vendor		= PCI_VENDOR_ID_RICOH,
		.device		= PCI_DEVICE_ID_RICOH_R5C822,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= SDHCI_QUIRK_FORCE_DMA |
				  SDHCI_QUIRK_NO_CARD_NO_RESET,
	},

	{
		.vendor		= PCI_VENDOR_ID_TI,
		.device		= PCI_DEVICE_ID_TI_XX21_XX11_SD,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= SDHCI_QUIRK_FORCE_DMA,
	},

	{
		.vendor		= PCI_VENDOR_ID_ENE,
		.device		= PCI_DEVICE_ID_ENE_CB712_SD,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= SDHCI_QUIRK_SINGLE_POWER_WRITE,
	},

	{	/* Generic SD host controller */
		PCI_DEVICE_CLASS((PCI_CLASS_SYSTEM_SDHCI << 8), 0xFFFF00)
	},

	{ /* end: all zeroes */ },
};

MODULE_DEVICE_TABLE(pci, pci_ids);

static void sdhci_prepare_data(struct sdhci_host *, struct mmc_data *);
static void sdhci_finish_data(struct sdhci_host *);

static void sdhci_send_command(struct sdhci_host *, struct mmc_command *);
static void sdhci_finish_command(struct sdhci_host *);

static void sdhci_dumpregs(struct sdhci_host *host)
{
	printk(KERN_DEBUG DRIVER_NAME ": ============== REGISTER DUMP ==============\n");

	printk(KERN_DEBUG DRIVER_NAME ": Sys addr: 0x%08x | Version:  0x%08x\n",
		readl(host->ioaddr + SDHCI_DMA_ADDRESS),
		readw(host->ioaddr + SDHCI_HOST_VERSION));
	printk(KERN_DEBUG DRIVER_NAME ": Blk size: 0x%08x | Blk cnt:  0x%08x\n",
		readw(host->ioaddr + SDHCI_BLOCK_SIZE),
		readw(host->ioaddr + SDHCI_BLOCK_COUNT));
	printk(KERN_DEBUG DRIVER_NAME ": Argument: 0x%08x | Trn mode: 0x%08x\n",
		readl(host->ioaddr + SDHCI_ARGUMENT),
		readw(host->ioaddr + SDHCI_TRANSFER_MODE));
	printk(KERN_DEBUG DRIVER_NAME ": Present:  0x%08x | Host ctl: 0x%08x\n",
		readl(host->ioaddr + SDHCI_PRESENT_STATE),
		readb(host->ioaddr + SDHCI_HOST_CONTROL));
	printk(KERN_DEBUG DRIVER_NAME ": Power:    0x%08x | Blk gap:  0x%08x\n",
		readb(host->ioaddr + SDHCI_POWER_CONTROL),
		readb(host->ioaddr + SDHCI_BLOCK_GAP_CONTROL));
	printk(KERN_DEBUG DRIVER_NAME ": Wake-up:  0x%08x | Clock:    0x%08x\n",
		readb(host->ioaddr + SDHCI_WALK_UP_CONTROL),
		readw(host->ioaddr + SDHCI_CLOCK_CONTROL));
	printk(KERN_DEBUG DRIVER_NAME ": Timeout:  0x%08x | Int stat: 0x%08x\n",
		readb(host->ioaddr + SDHCI_TIMEOUT_CONTROL),
		readl(host->ioaddr + SDHCI_INT_STATUS));
	printk(KERN_DEBUG DRIVER_NAME ": Int enab: 0x%08x | Sig enab: 0x%08x\n",
		readl(host->ioaddr + SDHCI_INT_ENABLE),
		readl(host->ioaddr + SDHCI_SIGNAL_ENABLE));
	printk(KERN_DEBUG DRIVER_NAME ": AC12 err: 0x%08x | Slot int: 0x%08x\n",
		readw(host->ioaddr + SDHCI_ACMD12_ERR),
		readw(host->ioaddr + SDHCI_SLOT_INT_STATUS));
	printk(KERN_DEBUG DRIVER_NAME ": Caps:     0x%08x | Max curr: 0x%08x\n",
		readl(host->ioaddr + SDHCI_CAPABILITIES),
		readl(host->ioaddr + SDHCI_MAX_CURRENT));

	printk(KERN_DEBUG DRIVER_NAME ": ===========================================\n");
}

/*****************************************************************************\
 *                                                                           *
 * Low level functions                                                       *
 *                                                                           *
\*****************************************************************************/

static void sdhci_reset(struct sdhci_host *host, u8 mask)
{
	unsigned long timeout;

	if (host->chip->quirks & SDHCI_QUIRK_NO_CARD_NO_RESET) {
		if (!(readl(host->ioaddr + SDHCI_PRESENT_STATE) &
			SDHCI_CARD_PRESENT))
			return;
	}

	writeb(mask, host->ioaddr + SDHCI_SOFTWARE_RESET);

	if (mask & SDHCI_RESET_ALL)
		host->clock = 0;

	/* Wait max 100 ms */
	timeout = 100;

	/* hw clears the bit when it's done */
	while (readb(host->ioaddr + SDHCI_SOFTWARE_RESET) & mask) {
		if (timeout == 0) {
			printk(KERN_ERR "%s: Reset 0x%x never completed.\n",
				mmc_hostname(host->mmc), (int)mask);
			sdhci_dumpregs(host);
			return;
		}
		timeout--;
		mdelay(1);
	}
}

static void sdhci_init(struct sdhci_host *host)
{
	u32 intmask;

	sdhci_reset(host, SDHCI_RESET_ALL);

	intmask = SDHCI_INT_BUS_POWER | SDHCI_INT_DATA_END_BIT |
		SDHCI_INT_DATA_CRC | SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_INDEX |
		SDHCI_INT_END_BIT | SDHCI_INT_CRC | SDHCI_INT_TIMEOUT |
		SDHCI_INT_CARD_REMOVE | SDHCI_INT_CARD_INSERT |
		SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL |
		SDHCI_INT_DMA_END | SDHCI_INT_DATA_END | SDHCI_INT_RESPONSE;

	writel(intmask, host->ioaddr + SDHCI_INT_ENABLE);
	writel(intmask, host->ioaddr + SDHCI_SIGNAL_ENABLE);
}

static void sdhci_activate_led(struct sdhci_host *host)
{
	u8 ctrl;

	ctrl = readb(host->ioaddr + SDHCI_HOST_CONTROL);
	ctrl |= SDHCI_CTRL_LED;
	writeb(ctrl, host->ioaddr + SDHCI_HOST_CONTROL);
}

static void sdhci_deactivate_led(struct sdhci_host *host)
{
	u8 ctrl;

	ctrl = readb(host->ioaddr + SDHCI_HOST_CONTROL);
	ctrl &= ~SDHCI_CTRL_LED;
	writeb(ctrl, host->ioaddr + SDHCI_HOST_CONTROL);
}

/*****************************************************************************\
 *                                                                           *
 * Core functions                                                            *
 *                                                                           *
\*****************************************************************************/

static inline char* sdhci_sg_to_buffer(struct sdhci_host* host)
{
	return page_address(host->cur_sg->page) + host->cur_sg->offset;
}

static inline int sdhci_next_sg(struct sdhci_host* host)
{
	/*
	 * Skip to next SG entry.
	 */
	host->cur_sg++;
	host->num_sg--;

	/*
	 * Any entries left?
	 */
	if (host->num_sg > 0) {
		host->offset = 0;
		host->remain = host->cur_sg->length;
	}

	return host->num_sg;
}

static void sdhci_read_block_pio(struct sdhci_host *host)
{
	int blksize, chunk_remain;
	u32 data;
	char *buffer;
	int size;

	DBG("PIO reading\n");

	blksize = host->data->blksz;
	chunk_remain = 0;
	data = 0;

	buffer = sdhci_sg_to_buffer(host) + host->offset;

	while (blksize) {
		if (chunk_remain == 0) {
			data = readl(host->ioaddr + SDHCI_BUFFER);
			chunk_remain = min(blksize, 4);
		}

		size = min(host->remain, chunk_remain);

		chunk_remain -= size;
		blksize -= size;
		host->offset += size;
		host->remain -= size;

		while (size) {
			*buffer = data & 0xFF;
			buffer++;
			data >>= 8;
			size--;
		}

		if (host->remain == 0) {
			if (sdhci_next_sg(host) == 0) {
				BUG_ON(blksize != 0);
				return;
			}
			buffer = sdhci_sg_to_buffer(host);
		}
	}
}

static void sdhci_write_block_pio(struct sdhci_host *host)
{
	int blksize, chunk_remain;
	u32 data;
	char *buffer;
	int bytes, size;

	DBG("PIO writing\n");

	blksize = host->data->blksz;
	chunk_remain = 4;
	data = 0;

	bytes = 0;
	buffer = sdhci_sg_to_buffer(host) + host->offset;

	while (blksize) {
		size = min(host->remain, chunk_remain);

		chunk_remain -= size;
		blksize -= size;
		host->offset += size;
		host->remain -= size;

		while (size) {
			data >>= 8;
			data |= (u32)*buffer << 24;
			buffer++;
			size--;
		}

		if (chunk_remain == 0) {
			writel(data, host->ioaddr + SDHCI_BUFFER);
			chunk_remain = min(blksize, 4);
		}

		if (host->remain == 0) {
			if (sdhci_next_sg(host) == 0) {
				BUG_ON(blksize != 0);
				return;
			}
			buffer = sdhci_sg_to_buffer(host);
		}
	}
}

static void sdhci_transfer_pio(struct sdhci_host *host)
{
	u32 mask;

	BUG_ON(!host->data);

	if (host->num_sg == 0)
		return;

	if (host->data->flags & MMC_DATA_READ)
		mask = SDHCI_DATA_AVAILABLE;
	else
		mask = SDHCI_SPACE_AVAILABLE;

	while (readl(host->ioaddr + SDHCI_PRESENT_STATE) & mask) {
		if (host->data->flags & MMC_DATA_READ)
			sdhci_read_block_pio(host);
		else
			sdhci_write_block_pio(host);

		if (host->num_sg == 0)
			break;
	}

	DBG("PIO transfer complete.\n");
}

static void sdhci_prepare_data(struct sdhci_host *host, struct mmc_data *data)
{
	u8 count;
	unsigned target_timeout, current_timeout;

	WARN_ON(host->data);

	if (data == NULL)
		return;

	DBG("blksz %04x blks %04x flags %08x\n",
		data->blksz, data->blocks, data->flags);
	DBG("tsac %d ms nsac %d clk\n",
		data->timeout_ns / 1000000, data->timeout_clks);

	/* Sanity checks */
	BUG_ON(data->blksz * data->blocks > 524288);
	BUG_ON(data->blksz > host->mmc->max_blk_size);
	BUG_ON(data->blocks > 65535);

	/* timeout in us */
	target_timeout = data->timeout_ns / 1000 +
		data->timeout_clks / host->clock;

	/*
	 * Figure out needed cycles.
	 * We do this in steps in order to fit inside a 32 bit int.
	 * The first step is the minimum timeout, which will have a
	 * minimum resolution of 6 bits:
	 * (1) 2^13*1000 > 2^22,
	 * (2) host->timeout_clk < 2^16
	 *     =>
	 *     (1) / (2) > 2^6
	 */
	count = 0;
	current_timeout = (1 << 13) * 1000 / host->timeout_clk;
	while (current_timeout < target_timeout) {
		count++;
		current_timeout <<= 1;
		if (count >= 0xF)
			break;
	}

	if (count >= 0xF) {
		printk(KERN_WARNING "%s: Too large timeout requested!\n",
			mmc_hostname(host->mmc));
		count = 0xE;
	}

	writeb(count, host->ioaddr + SDHCI_TIMEOUT_CONTROL);

	if (host->flags & SDHCI_USE_DMA) {
		int count;

		count = pci_map_sg(host->chip->pdev, data->sg, data->sg_len,
			(data->flags & MMC_DATA_READ)?PCI_DMA_FROMDEVICE:PCI_DMA_TODEVICE);
		BUG_ON(count != 1);

		writel(sg_dma_address(data->sg), host->ioaddr + SDHCI_DMA_ADDRESS);
	} else {
		host->cur_sg = data->sg;
		host->num_sg = data->sg_len;

		host->offset = 0;
		host->remain = host->cur_sg->length;
	}

	/* We do not handle DMA boundaries, so set it to max (512 KiB) */
	writew(SDHCI_MAKE_BLKSZ(7, data->blksz),
		host->ioaddr + SDHCI_BLOCK_SIZE);
	writew(data->blocks, host->ioaddr + SDHCI_BLOCK_COUNT);
}

static void sdhci_set_transfer_mode(struct sdhci_host *host,
	struct mmc_data *data)
{
	u16 mode;

	WARN_ON(host->data);

	if (data == NULL)
		return;

	mode = SDHCI_TRNS_BLK_CNT_EN;
	if (data->blocks > 1)
		mode |= SDHCI_TRNS_MULTI;
	if (data->flags & MMC_DATA_READ)
		mode |= SDHCI_TRNS_READ;
	if (host->flags & SDHCI_USE_DMA)
		mode |= SDHCI_TRNS_DMA;

	writew(mode, host->ioaddr + SDHCI_TRANSFER_MODE);
}

static void sdhci_finish_data(struct sdhci_host *host)
{
	struct mmc_data *data;
	u16 blocks;

	BUG_ON(!host->data);

	data = host->data;
	host->data = NULL;

	if (host->flags & SDHCI_USE_DMA) {
		pci_unmap_sg(host->chip->pdev, data->sg, data->sg_len,
			(data->flags & MMC_DATA_READ)?PCI_DMA_FROMDEVICE:PCI_DMA_TODEVICE);
	}

	/*
	 * Controller doesn't count down when in single block mode.
	 */
	if ((data->blocks == 1) && (data->error == MMC_ERR_NONE))
		blocks = 0;
	else
		blocks = readw(host->ioaddr + SDHCI_BLOCK_COUNT);
	data->bytes_xfered = data->blksz * (data->blocks - blocks);

	if ((data->error == MMC_ERR_NONE) && blocks) {
		printk(KERN_ERR "%s: Controller signalled completion even "
			"though there were blocks left.\n",
			mmc_hostname(host->mmc));
		data->error = MMC_ERR_FAILED;
	}

	DBG("Ending data transfer (%d bytes)\n", data->bytes_xfered);

	if (data->stop) {
		/*
		 * The controller needs a reset of internal state machines
		 * upon error conditions.
		 */
		if (data->error != MMC_ERR_NONE) {
			sdhci_reset(host, SDHCI_RESET_CMD);
			sdhci_reset(host, SDHCI_RESET_DATA);
		}

		sdhci_send_command(host, data->stop);
	} else
		tasklet_schedule(&host->finish_tasklet);
}

static void sdhci_send_command(struct sdhci_host *host, struct mmc_command *cmd)
{
	int flags;
	u32 mask;
	unsigned long timeout;

	WARN_ON(host->cmd);

	DBG("Sending cmd (%x)\n", cmd->opcode);

	/* Wait max 10 ms */
	timeout = 10;

	mask = SDHCI_CMD_INHIBIT;
	if ((cmd->data != NULL) || (cmd->flags & MMC_RSP_BUSY))
		mask |= SDHCI_DATA_INHIBIT;

	/* We shouldn't wait for data inihibit for stop commands, even
	   though they might use busy signaling */
	if (host->mrq->data && (cmd == host->mrq->data->stop))
		mask &= ~SDHCI_DATA_INHIBIT;

	while (readl(host->ioaddr + SDHCI_PRESENT_STATE) & mask) {
		if (timeout == 0) {
			printk(KERN_ERR "%s: Controller never released "
				"inhibit bit(s).\n", mmc_hostname(host->mmc));
			sdhci_dumpregs(host);
			cmd->error = MMC_ERR_FAILED;
			tasklet_schedule(&host->finish_tasklet);
			return;
		}
		timeout--;
		mdelay(1);
	}

	mod_timer(&host->timer, jiffies + 10 * HZ);

	host->cmd = cmd;

	sdhci_prepare_data(host, cmd->data);

	writel(cmd->arg, host->ioaddr + SDHCI_ARGUMENT);

	sdhci_set_transfer_mode(host, cmd->data);

	if ((cmd->flags & MMC_RSP_136) && (cmd->flags & MMC_RSP_BUSY)) {
		printk(KERN_ERR "%s: Unsupported response type!\n",
			mmc_hostname(host->mmc));
		cmd->error = MMC_ERR_INVALID;
		tasklet_schedule(&host->finish_tasklet);
		return;
	}

	if (!(cmd->flags & MMC_RSP_PRESENT))
		flags = SDHCI_CMD_RESP_NONE;
	else if (cmd->flags & MMC_RSP_136)
		flags = SDHCI_CMD_RESP_LONG;
	else if (cmd->flags & MMC_RSP_BUSY)
		flags = SDHCI_CMD_RESP_SHORT_BUSY;
	else
		flags = SDHCI_CMD_RESP_SHORT;

	if (cmd->flags & MMC_RSP_CRC)
		flags |= SDHCI_CMD_CRC;
	if (cmd->flags & MMC_RSP_OPCODE)
		flags |= SDHCI_CMD_INDEX;
	if (cmd->data)
		flags |= SDHCI_CMD_DATA;

	writew(SDHCI_MAKE_CMD(cmd->opcode, flags),
		host->ioaddr + SDHCI_COMMAND);
}

static void sdhci_finish_command(struct sdhci_host *host)
{
	int i;

	BUG_ON(host->cmd == NULL);

	if (host->cmd->flags & MMC_RSP_PRESENT) {
		if (host->cmd->flags & MMC_RSP_136) {
			/* CRC is stripped so we need to do some shifting. */
			for (i = 0;i < 4;i++) {
				host->cmd->resp[i] = readl(host->ioaddr +
					SDHCI_RESPONSE + (3-i)*4) << 8;
				if (i != 3)
					host->cmd->resp[i] |=
						readb(host->ioaddr +
						SDHCI_RESPONSE + (3-i)*4-1);
			}
		} else {
			host->cmd->resp[0] = readl(host->ioaddr + SDHCI_RESPONSE);
		}
	}

	host->cmd->error = MMC_ERR_NONE;

	DBG("Ending cmd (%x)\n", host->cmd->opcode);

	if (host->cmd->data)
		host->data = host->cmd->data;
	else
		tasklet_schedule(&host->finish_tasklet);

	host->cmd = NULL;
}

static void sdhci_set_clock(struct sdhci_host *host, unsigned int clock)
{
	int div;
	u16 clk;
	unsigned long timeout;

	if (clock == host->clock)
		return;

	writew(0, host->ioaddr + SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		goto out;

	for (div = 1;div < 256;div *= 2) {
		if ((host->max_clk / div) <= clock)
			break;
	}
	div >>= 1;

	clk = div << SDHCI_DIVIDER_SHIFT;
	clk |= SDHCI_CLOCK_INT_EN;
	writew(clk, host->ioaddr + SDHCI_CLOCK_CONTROL);

	/* Wait max 10 ms */
	timeout = 10;
	while (!((clk = readw(host->ioaddr + SDHCI_CLOCK_CONTROL))
		& SDHCI_CLOCK_INT_STABLE)) {
		if (timeout == 0) {
			printk(KERN_ERR "%s: Internal clock never "
				"stabilised.\n", mmc_hostname(host->mmc));
			sdhci_dumpregs(host);
			return;
		}
		timeout--;
		mdelay(1);
	}

	clk |= SDHCI_CLOCK_CARD_EN;
	writew(clk, host->ioaddr + SDHCI_CLOCK_CONTROL);

out:
	host->clock = clock;
}

static void sdhci_set_power(struct sdhci_host *host, unsigned short power)
{
	u8 pwr;

	if (host->power == power)
		return;

	if (power == (unsigned short)-1) {
		writeb(0, host->ioaddr + SDHCI_POWER_CONTROL);
		goto out;
	}

	/*
	 * Spec says that we should clear the power reg before setting
	 * a new value. Some controllers don't seem to like this though.
	 */
	if (!(host->chip->quirks & SDHCI_QUIRK_SINGLE_POWER_WRITE))
		writeb(0, host->ioaddr + SDHCI_POWER_CONTROL);

	pwr = SDHCI_POWER_ON;

	switch (1 << power) {
	case MMC_VDD_165_195:
		pwr |= SDHCI_POWER_180;
		break;
	case MMC_VDD_29_30:
	case MMC_VDD_30_31:
		pwr |= SDHCI_POWER_300;
		break;
	case MMC_VDD_32_33:
	case MMC_VDD_33_34:
		pwr |= SDHCI_POWER_330;
		break;
	default:
		BUG();
	}

	writeb(pwr, host->ioaddr + SDHCI_POWER_CONTROL);

out:
	host->power = power;
}

/*****************************************************************************\
 *                                                                           *
 * MMC callbacks                                                             *
 *                                                                           *
\*****************************************************************************/

static void sdhci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sdhci_host *host;
	unsigned long flags;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);

	WARN_ON(host->mrq != NULL);

	sdhci_activate_led(host);

	host->mrq = mrq;

	if (!(readl(host->ioaddr + SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT)) {
		host->mrq->cmd->error = MMC_ERR_TIMEOUT;
		tasklet_schedule(&host->finish_tasklet);
	} else
		sdhci_send_command(host, mrq->cmd);

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}

static void sdhci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host;
	unsigned long flags;
	u8 ctrl;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);

	/*
	 * Reset the chip on each power off.
	 * Should clear out any weird states.
	 */
	if (ios->power_mode == MMC_POWER_OFF) {
		writel(0, host->ioaddr + SDHCI_SIGNAL_ENABLE);
		sdhci_init(host);
	}

	sdhci_set_clock(host, ios->clock);

	if (ios->power_mode == MMC_POWER_OFF)
		sdhci_set_power(host, -1);
	else
		sdhci_set_power(host, ios->vdd);

	ctrl = readb(host->ioaddr + SDHCI_HOST_CONTROL);

	if (ios->bus_width == MMC_BUS_WIDTH_4)
		ctrl |= SDHCI_CTRL_4BITBUS;
	else
		ctrl &= ~SDHCI_CTRL_4BITBUS;

	if (ios->timing == MMC_TIMING_SD_HS)
		ctrl |= SDHCI_CTRL_HISPD;
	else
		ctrl &= ~SDHCI_CTRL_HISPD;

	writeb(ctrl, host->ioaddr + SDHCI_HOST_CONTROL);

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}

static int sdhci_get_ro(struct mmc_host *mmc)
{
	struct sdhci_host *host;
	unsigned long flags;
	int present;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);

	present = readl(host->ioaddr + SDHCI_PRESENT_STATE);

	spin_unlock_irqrestore(&host->lock, flags);

	return !(present & SDHCI_WRITE_PROTECT);
}

static const struct mmc_host_ops sdhci_ops = {
	.request	= sdhci_request,
	.set_ios	= sdhci_set_ios,
	.get_ro		= sdhci_get_ro,
};

/*****************************************************************************\
 *                                                                           *
 * Tasklets                                                                  *
 *                                                                           *
\*****************************************************************************/

static void sdhci_tasklet_card(unsigned long param)
{
	struct sdhci_host *host;
	unsigned long flags;

	host = (struct sdhci_host*)param;

	spin_lock_irqsave(&host->lock, flags);

	if (!(readl(host->ioaddr + SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT)) {
		if (host->mrq) {
			printk(KERN_ERR "%s: Card removed during transfer!\n",
				mmc_hostname(host->mmc));
			printk(KERN_ERR "%s: Resetting controller.\n",
				mmc_hostname(host->mmc));

			sdhci_reset(host, SDHCI_RESET_CMD);
			sdhci_reset(host, SDHCI_RESET_DATA);

			host->mrq->cmd->error = MMC_ERR_FAILED;
			tasklet_schedule(&host->finish_tasklet);
		}
	}

	spin_unlock_irqrestore(&host->lock, flags);

	mmc_detect_change(host->mmc, msecs_to_jiffies(500));
}

static void sdhci_tasklet_finish(unsigned long param)
{
	struct sdhci_host *host;
	unsigned long flags;
	struct mmc_request *mrq;

	host = (struct sdhci_host*)param;

	spin_lock_irqsave(&host->lock, flags);

	del_timer(&host->timer);

	mrq = host->mrq;

	DBG("Ending request, cmd (%x)\n", mrq->cmd->opcode);

	/*
	 * The controller needs a reset of internal state machines
	 * upon error conditions.
	 */
	if ((mrq->cmd->error != MMC_ERR_NONE) ||
		(mrq->data && ((mrq->data->error != MMC_ERR_NONE) ||
		(mrq->data->stop && (mrq->data->stop->error != MMC_ERR_NONE))))) {

		/* Some controllers need this kick or reset won't work here */
		if (host->chip->quirks & SDHCI_QUIRK_CLOCK_BEFORE_RESET) {
			unsigned int clock;

			/* This is to force an update */
			clock = host->clock;
			host->clock = 0;
			sdhci_set_clock(host, clock);
		}

		/* Spec says we should do both at the same time, but Ricoh
		   controllers do not like that. */
		sdhci_reset(host, SDHCI_RESET_CMD);
		sdhci_reset(host, SDHCI_RESET_DATA);
	}

	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;

	sdhci_deactivate_led(host);

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);

	mmc_request_done(host->mmc, mrq);
}

static void sdhci_timeout_timer(unsigned long data)
{
	struct sdhci_host *host;
	unsigned long flags;

	host = (struct sdhci_host*)data;

	spin_lock_irqsave(&host->lock, flags);

	if (host->mrq) {
		printk(KERN_ERR "%s: Timeout waiting for hardware "
			"interrupt.\n", mmc_hostname(host->mmc));
		sdhci_dumpregs(host);

		if (host->data) {
			host->data->error = MMC_ERR_TIMEOUT;
			sdhci_finish_data(host);
		} else {
			if (host->cmd)
				host->cmd->error = MMC_ERR_TIMEOUT;
			else
				host->mrq->cmd->error = MMC_ERR_TIMEOUT;

			tasklet_schedule(&host->finish_tasklet);
		}
	}

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}

/*****************************************************************************\
 *                                                                           *
 * Interrupt handling                                                        *
 *                                                                           *
\*****************************************************************************/

static void sdhci_cmd_irq(struct sdhci_host *host, u32 intmask)
{
	BUG_ON(intmask == 0);

	if (!host->cmd) {
		printk(KERN_ERR "%s: Got command interrupt even though no "
			"command operation was in progress.\n",
			mmc_hostname(host->mmc));
		sdhci_dumpregs(host);
		return;
	}

	if (intmask & SDHCI_INT_RESPONSE)
		sdhci_finish_command(host);
	else {
		if (intmask & SDHCI_INT_TIMEOUT)
			host->cmd->error = MMC_ERR_TIMEOUT;
		else if (intmask & SDHCI_INT_CRC)
			host->cmd->error = MMC_ERR_BADCRC;
		else if (intmask & (SDHCI_INT_END_BIT | SDHCI_INT_INDEX))
			host->cmd->error = MMC_ERR_FAILED;
		else
			host->cmd->error = MMC_ERR_INVALID;

		tasklet_schedule(&host->finish_tasklet);
	}
}

static void sdhci_data_irq(struct sdhci_host *host, u32 intmask)
{
	BUG_ON(intmask == 0);

	if (!host->data) {
		/*
		 * A data end interrupt is sent together with the response
		 * for the stop command.
		 */
		if (intmask & SDHCI_INT_DATA_END)
			return;

		printk(KERN_ERR "%s: Got data interrupt even though no "
			"data operation was in progress.\n",
			mmc_hostname(host->mmc));
		sdhci_dumpregs(host);

		return;
	}

	if (intmask & SDHCI_INT_DATA_TIMEOUT)
		host->data->error = MMC_ERR_TIMEOUT;
	else if (intmask & SDHCI_INT_DATA_CRC)
		host->data->error = MMC_ERR_BADCRC;
	else if (intmask & SDHCI_INT_DATA_END_BIT)
		host->data->error = MMC_ERR_FAILED;

	if (host->data->error != MMC_ERR_NONE)
		sdhci_finish_data(host);
	else {
		if (intmask & (SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL))
			sdhci_transfer_pio(host);

		if (intmask & SDHCI_INT_DATA_END)
			sdhci_finish_data(host);
	}
}

static irqreturn_t sdhci_irq(int irq, void *dev_id)
{
	irqreturn_t result;
	struct sdhci_host* host = dev_id;
	u32 intmask;

	spin_lock(&host->lock);

	intmask = readl(host->ioaddr + SDHCI_INT_STATUS);

	if (!intmask || intmask == 0xffffffff) {
		result = IRQ_NONE;
		goto out;
	}

	DBG("*** %s got interrupt: 0x%08x\n", host->slot_descr, intmask);

	if (intmask & (SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE)) {
		writel(intmask & (SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE),
			host->ioaddr + SDHCI_INT_STATUS);
		tasklet_schedule(&host->card_tasklet);
	}

	intmask &= ~(SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE);

	if (intmask & SDHCI_INT_CMD_MASK) {
		writel(intmask & SDHCI_INT_CMD_MASK,
			host->ioaddr + SDHCI_INT_STATUS);
		sdhci_cmd_irq(host, intmask & SDHCI_INT_CMD_MASK);
	}

	if (intmask & SDHCI_INT_DATA_MASK) {
		writel(intmask & SDHCI_INT_DATA_MASK,
			host->ioaddr + SDHCI_INT_STATUS);
		sdhci_data_irq(host, intmask & SDHCI_INT_DATA_MASK);
	}

	intmask &= ~(SDHCI_INT_CMD_MASK | SDHCI_INT_DATA_MASK);

	if (intmask & SDHCI_INT_BUS_POWER) {
		printk(KERN_ERR "%s: Card is consuming too much power!\n",
			mmc_hostname(host->mmc));
		writel(SDHCI_INT_BUS_POWER, host->ioaddr + SDHCI_INT_STATUS);
	}

	intmask &= SDHCI_INT_BUS_POWER;

	if (intmask) {
		printk(KERN_ERR "%s: Unexpected interrupt 0x%08x.\n",
			mmc_hostname(host->mmc), intmask);
		sdhci_dumpregs(host);

		writel(intmask, host->ioaddr + SDHCI_INT_STATUS);
	}

	result = IRQ_HANDLED;

	mmiowb();
out:
	spin_unlock(&host->lock);

	return result;
}

/*****************************************************************************\
 *                                                                           *
 * Suspend/resume                                                            *
 *                                                                           *
\*****************************************************************************/

#ifdef CONFIG_PM

static int sdhci_suspend (struct pci_dev *pdev, pm_message_t state)
{
	struct sdhci_chip *chip;
	int i, ret;

	chip = pci_get_drvdata(pdev);
	if (!chip)
		return 0;

	DBG("Suspending...\n");

	for (i = 0;i < chip->num_slots;i++) {
		if (!chip->hosts[i])
			continue;
		ret = mmc_suspend_host(chip->hosts[i]->mmc, state);
		if (ret) {
			for (i--;i >= 0;i--)
				mmc_resume_host(chip->hosts[i]->mmc);
			return ret;
		}
	}

	pci_save_state(pdev);
	pci_enable_wake(pdev, pci_choose_state(pdev, state), 0);

	for (i = 0;i < chip->num_slots;i++) {
		if (!chip->hosts[i])
			continue;
		free_irq(chip->hosts[i]->irq, chip->hosts[i]);
	}

	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int sdhci_resume (struct pci_dev *pdev)
{
	struct sdhci_chip *chip;
	int i, ret;

	chip = pci_get_drvdata(pdev);
	if (!chip)
		return 0;

	DBG("Resuming...\n");

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	for (i = 0;i < chip->num_slots;i++) {
		if (!chip->hosts[i])
			continue;
		if (chip->hosts[i]->flags & SDHCI_USE_DMA)
			pci_set_master(pdev);
		ret = request_irq(chip->hosts[i]->irq, sdhci_irq,
			IRQF_SHARED, chip->hosts[i]->slot_descr,
			chip->hosts[i]);
		if (ret)
			return ret;
		sdhci_init(chip->hosts[i]);
		mmiowb();
		ret = mmc_resume_host(chip->hosts[i]->mmc);
		if (ret)
			return ret;
	}

	return 0;
}

#else /* CONFIG_PM */

#define sdhci_suspend NULL
#define sdhci_resume NULL

#endif /* CONFIG_PM */

/*****************************************************************************\
 *                                                                           *
 * Device probing/removal                                                    *
 *                                                                           *
\*****************************************************************************/

static int __devinit sdhci_probe_slot(struct pci_dev *pdev, int slot)
{
	int ret;
	unsigned int version;
	struct sdhci_chip *chip;
	struct mmc_host *mmc;
	struct sdhci_host *host;

	u8 first_bar;
	unsigned int caps;

	chip = pci_get_drvdata(pdev);
	BUG_ON(!chip);

	ret = pci_read_config_byte(pdev, PCI_SLOT_INFO, &first_bar);
	if (ret)
		return ret;

	first_bar &= PCI_SLOT_INFO_FIRST_BAR_MASK;

	if (first_bar > 5) {
		printk(KERN_ERR DRIVER_NAME ": Invalid first BAR. Aborting.\n");
		return -ENODEV;
	}

	if (!(pci_resource_flags(pdev, first_bar + slot) & IORESOURCE_MEM)) {
		printk(KERN_ERR DRIVER_NAME ": BAR is not iomem. Aborting.\n");
		return -ENODEV;
	}

	if (pci_resource_len(pdev, first_bar + slot) != 0x100) {
		printk(KERN_ERR DRIVER_NAME ": Invalid iomem size. "
			"You may experience problems.\n");
	}

	if ((pdev->class & 0x0000FF) == PCI_SDHCI_IFVENDOR) {
		printk(KERN_ERR DRIVER_NAME ": Vendor specific interface. Aborting.\n");
		return -ENODEV;
	}

	if ((pdev->class & 0x0000FF) > PCI_SDHCI_IFVENDOR) {
		printk(KERN_ERR DRIVER_NAME ": Unknown interface. Aborting.\n");
		return -ENODEV;
	}

	mmc = mmc_alloc_host(sizeof(struct sdhci_host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->mmc = mmc;

	host->chip = chip;
	chip->hosts[slot] = host;

	host->bar = first_bar + slot;

	host->addr = pci_resource_start(pdev, host->bar);
	host->irq = pdev->irq;

	DBG("slot %d at 0x%08lx, irq %d\n", slot, host->addr, host->irq);

	snprintf(host->slot_descr, 20, "sdhci:slot%d", slot);

	ret = pci_request_region(pdev, host->bar, host->slot_descr);
	if (ret)
		goto free;

	host->ioaddr = ioremap_nocache(host->addr,
		pci_resource_len(pdev, host->bar));
	if (!host->ioaddr) {
		ret = -ENOMEM;
		goto release;
	}

	sdhci_reset(host, SDHCI_RESET_ALL);

	version = readw(host->ioaddr + SDHCI_HOST_VERSION);
	version = (version & SDHCI_SPEC_VER_MASK) >> SDHCI_SPEC_VER_SHIFT;
	if (version != 0) {
		printk(KERN_ERR "%s: Unknown controller version (%d). "
			"You may experience problems.\n", host->slot_descr,
			version);
	}

	caps = readl(host->ioaddr + SDHCI_CAPABILITIES);

	if (debug_nodma)
		DBG("DMA forced off\n");
	else if (debug_forcedma) {
		DBG("DMA forced on\n");
		host->flags |= SDHCI_USE_DMA;
	} else if (chip->quirks & SDHCI_QUIRK_FORCE_DMA)
		host->flags |= SDHCI_USE_DMA;
	else if ((pdev->class & 0x0000FF) != PCI_SDHCI_IFDMA)
		DBG("Controller doesn't have DMA interface\n");
	else if (!(caps & SDHCI_CAN_DO_DMA))
		DBG("Controller doesn't have DMA capability\n");
	else
		host->flags |= SDHCI_USE_DMA;

	if (host->flags & SDHCI_USE_DMA) {
		if (pci_set_dma_mask(pdev, DMA_32BIT_MASK)) {
			printk(KERN_WARNING "%s: No suitable DMA available. "
				"Falling back to PIO.\n", host->slot_descr);
			host->flags &= ~SDHCI_USE_DMA;
		}
	}

	if (host->flags & SDHCI_USE_DMA)
		pci_set_master(pdev);
	else /* XXX: Hack to get MMC layer to avoid highmem */
		pdev->dma_mask = 0;

	host->max_clk =
		(caps & SDHCI_CLOCK_BASE_MASK) >> SDHCI_CLOCK_BASE_SHIFT;
	if (host->max_clk == 0) {
		printk(KERN_ERR "%s: Hardware doesn't specify base clock "
			"frequency.\n", host->slot_descr);
		ret = -ENODEV;
		goto unmap;
	}
	host->max_clk *= 1000000;

	host->timeout_clk =
		(caps & SDHCI_TIMEOUT_CLK_MASK) >> SDHCI_TIMEOUT_CLK_SHIFT;
	if (host->timeout_clk == 0) {
		printk(KERN_ERR "%s: Hardware doesn't specify timeout clock "
			"frequency.\n", host->slot_descr);
		ret = -ENODEV;
		goto unmap;
	}
	if (caps & SDHCI_TIMEOUT_CLK_UNIT)
		host->timeout_clk *= 1000;

	/*
	 * Set host parameters.
	 */
	mmc->ops = &sdhci_ops;
	mmc->f_min = host->max_clk / 256;
	mmc->f_max = host->max_clk;
	mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_MULTIWRITE | MMC_CAP_BYTEBLOCK;

	if (caps & SDHCI_CAN_DO_HISPD)
		mmc->caps |= MMC_CAP_SD_HIGHSPEED;

	mmc->ocr_avail = 0;
	if (caps & SDHCI_CAN_VDD_330)
		mmc->ocr_avail |= MMC_VDD_32_33|MMC_VDD_33_34;
	if (caps & SDHCI_CAN_VDD_300)
		mmc->ocr_avail |= MMC_VDD_29_30|MMC_VDD_30_31;
	if (caps & SDHCI_CAN_VDD_180)
		mmc->ocr_avail |= MMC_VDD_165_195;

	if (mmc->ocr_avail == 0) {
		printk(KERN_ERR "%s: Hardware doesn't report any "
			"support voltages.\n", host->slot_descr);
		ret = -ENODEV;
		goto unmap;
	}

	spin_lock_init(&host->lock);

	/*
	 * Maximum number of segments. Hardware cannot do scatter lists.
	 */
	if (host->flags & SDHCI_USE_DMA)
		mmc->max_hw_segs = 1;
	else
		mmc->max_hw_segs = 16;
	mmc->max_phys_segs = 16;

	/*
	 * Maximum number of sectors in one transfer. Limited by DMA boundary
	 * size (512KiB).
	 */
	mmc->max_req_size = 524288;

	/*
	 * Maximum segment size. Could be one segment with the maximum number
	 * of bytes.
	 */
	mmc->max_seg_size = mmc->max_req_size;

	/*
	 * Maximum block size. This varies from controller to controller and
	 * is specified in the capabilities register.
	 */
	mmc->max_blk_size = (caps & SDHCI_MAX_BLOCK_MASK) >> SDHCI_MAX_BLOCK_SHIFT;
	if (mmc->max_blk_size >= 3) {
		printk(KERN_ERR "%s: Invalid maximum block size.\n",
			host->slot_descr);
		ret = -ENODEV;
		goto unmap;
	}
	mmc->max_blk_size = 512 << mmc->max_blk_size;

	/*
	 * Maximum block count.
	 */
	mmc->max_blk_count = 65535;

	/*
	 * Init tasklets.
	 */
	tasklet_init(&host->card_tasklet,
		sdhci_tasklet_card, (unsigned long)host);
	tasklet_init(&host->finish_tasklet,
		sdhci_tasklet_finish, (unsigned long)host);

	setup_timer(&host->timer, sdhci_timeout_timer, (unsigned long)host);

	ret = request_irq(host->irq, sdhci_irq, IRQF_SHARED,
		host->slot_descr, host);
	if (ret)
		goto untasklet;

	sdhci_init(host);

#ifdef CONFIG_MMC_DEBUG
	sdhci_dumpregs(host);
#endif

	mmiowb();

	mmc_add_host(mmc);

	printk(KERN_INFO "%s: SDHCI at 0x%08lx irq %d %s\n", mmc_hostname(mmc),
		host->addr, host->irq,
		(host->flags & SDHCI_USE_DMA)?"DMA":"PIO");

	return 0;

untasklet:
	tasklet_kill(&host->card_tasklet);
	tasklet_kill(&host->finish_tasklet);
unmap:
	iounmap(host->ioaddr);
release:
	pci_release_region(pdev, host->bar);
free:
	mmc_free_host(mmc);

	return ret;
}

static void sdhci_remove_slot(struct pci_dev *pdev, int slot)
{
	struct sdhci_chip *chip;
	struct mmc_host *mmc;
	struct sdhci_host *host;

	chip = pci_get_drvdata(pdev);
	host = chip->hosts[slot];
	mmc = host->mmc;

	chip->hosts[slot] = NULL;

	mmc_remove_host(mmc);

	sdhci_reset(host, SDHCI_RESET_ALL);

	free_irq(host->irq, host);

	del_timer_sync(&host->timer);

	tasklet_kill(&host->card_tasklet);
	tasklet_kill(&host->finish_tasklet);

	iounmap(host->ioaddr);

	pci_release_region(pdev, host->bar);

	mmc_free_host(mmc);
}

static int __devinit sdhci_probe(struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	int ret, i;
	u8 slots, rev;
	struct sdhci_chip *chip;

	BUG_ON(pdev == NULL);
	BUG_ON(ent == NULL);

	pci_read_config_byte(pdev, PCI_CLASS_REVISION, &rev);

	printk(KERN_INFO DRIVER_NAME
		": SDHCI controller found at %s [%04x:%04x] (rev %x)\n",
		pci_name(pdev), (int)pdev->vendor, (int)pdev->device,
		(int)rev);

	ret = pci_read_config_byte(pdev, PCI_SLOT_INFO, &slots);
	if (ret)
		return ret;

	slots = PCI_SLOT_INFO_SLOTS(slots) + 1;
	DBG("found %d slot(s)\n", slots);
	if (slots == 0)
		return -ENODEV;

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	chip = kzalloc(sizeof(struct sdhci_chip) +
		sizeof(struct sdhci_host*) * slots, GFP_KERNEL);
	if (!chip) {
		ret = -ENOMEM;
		goto err;
	}

	chip->pdev = pdev;
	chip->quirks = ent->driver_data;

	if (debug_quirks)
		chip->quirks = debug_quirks;

	chip->num_slots = slots;
	pci_set_drvdata(pdev, chip);

	for (i = 0;i < slots;i++) {
		ret = sdhci_probe_slot(pdev, i);
		if (ret) {
			for (i--;i >= 0;i--)
				sdhci_remove_slot(pdev, i);
			goto free;
		}
	}

	return 0;

free:
	pci_set_drvdata(pdev, NULL);
	kfree(chip);

err:
	pci_disable_device(pdev);
	return ret;
}

static void __devexit sdhci_remove(struct pci_dev *pdev)
{
	int i;
	struct sdhci_chip *chip;

	chip = pci_get_drvdata(pdev);

	if (chip) {
		for (i = 0;i < chip->num_slots;i++)
			sdhci_remove_slot(pdev, i);

		pci_set_drvdata(pdev, NULL);

		kfree(chip);
	}

	pci_disable_device(pdev);
}

static struct pci_driver sdhci_driver = {
	.name = 	DRIVER_NAME,
	.id_table =	pci_ids,
	.probe = 	sdhci_probe,
	.remove =	__devexit_p(sdhci_remove),
	.suspend =	sdhci_suspend,
	.resume	=	sdhci_resume,
};

/*****************************************************************************\
 *                                                                           *
 * Driver init/exit                                                          *
 *                                                                           *
\*****************************************************************************/

static int __init sdhci_drv_init(void)
{
	printk(KERN_INFO DRIVER_NAME
		": Secure Digital Host Controller Interface driver\n");
	printk(KERN_INFO DRIVER_NAME ": Copyright(c) Pierre Ossman\n");

	return pci_register_driver(&sdhci_driver);
}

static void __exit sdhci_drv_exit(void)
{
	DBG("Exiting\n");

	pci_unregister_driver(&sdhci_driver);
}

module_init(sdhci_drv_init);
module_exit(sdhci_drv_exit);

module_param(debug_nodma, uint, 0444);
module_param(debug_forcedma, uint, 0444);
module_param(debug_quirks, uint, 0444);

MODULE_AUTHOR("Pierre Ossman <drzeus@drzeus.cx>");
MODULE_DESCRIPTION("Secure Digital Host Controller Interface driver");
MODULE_LICENSE("GPL");

MODULE_PARM_DESC(debug_nodma, "Forcefully disable DMA transfers. (default 0)");
MODULE_PARM_DESC(debug_forcedma, "Forcefully enable DMA transfers. (default 0)");
MODULE_PARM_DESC(debug_quirks, "Force certain quirks.");
