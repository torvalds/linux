/*
* linux/drivers/mmc/host/mshci.c
* Mobile Storage Host Controller Interface driver
*
* Copyright (c) 2011 Samsung Electronics Co., Ltd.
*		http://www.samsung.com
*
* Based on linux/drivers/mmc/host/sdhci.c
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or (at
* your option) any later version.
*
*/

#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>

#include <linux/leds.h>

#include <linux/mmc/host.h>

#include <plat/cpu.h>

#include "mshci.h"

#define DRIVER_NAME "mshci"

#define DBG(f, x...) \
	pr_debug(DRIVER_NAME " [%s()]: " f, __func__, ## x)

#define SDHC_CLK_ON 1
#define SDHC_CLK_OFF 0

static unsigned int debug_quirks;

static void mshci_prepare_data(struct mshci_host *, struct mmc_data *);
static void mshci_finish_data(struct mshci_host *);

static void mshci_send_command(struct mshci_host *, struct mmc_command *);
static void mshci_finish_command(struct mshci_host *);
static void mshci_fifo_init(struct mshci_host *host);

static void mshci_set_clock(struct mshci_host *host,
				unsigned int clock, u32 bus_width);

#define MSHCI_MAX_DMA_SINGLE_TRANS_SIZE	(0x1000)
#define MSHCI_MAX_DMA_TRANS_SIZE	(0x400000)
#define MSHCI_MAX_DMA_LIST		(MSHCI_MAX_DMA_TRANS_SIZE / \
					 MSHCI_MAX_DMA_SINGLE_TRANS_SIZE)

static void mshci_dumpregs(struct mshci_host *host)
{
	printk(KERN_DEBUG DRIVER_NAME ": ============== REGISTER DUMP ==============\n");
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_CTRL:      0x%08x\n",
		mshci_readl(host, MSHCI_CTRL));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_PWREN:     0x%08x\n",
		mshci_readl(host, MSHCI_PWREN));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_CLKDIV:    0x%08x\n",
		mshci_readl(host, MSHCI_CLKDIV));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_CLKSRC:    0x%08x\n",
		mshci_readl(host, MSHCI_CLKSRC));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_CLKENA:    0x%08x\n",
		mshci_readl(host, MSHCI_CLKENA));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_TMOUT:     0x%08x\n",
		mshci_readl(host, MSHCI_TMOUT));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_CTYPE:     0x%08x\n",
		mshci_readl(host, MSHCI_CTYPE));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_BLKSIZ:    0x%08x\n",
		mshci_readl(host, MSHCI_BLKSIZ));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_BYTCNT:    0x%08x\n",
		mshci_readl(host, MSHCI_BYTCNT));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_INTMSK:    0x%08x\n",
		mshci_readl(host, MSHCI_INTMSK));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_CMDARG:    0x%08x\n",
		mshci_readl(host, MSHCI_CMDARG));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_CMD:       0x%08x\n",
		mshci_readl(host, MSHCI_CMD));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_MINTSTS:   0x%08x\n",
		mshci_readl(host, MSHCI_MINTSTS));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_RINTSTS:   0x%08x\n",
		mshci_readl(host, MSHCI_RINTSTS));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_STATUS:    0x%08x\n",
		mshci_readl(host, MSHCI_STATUS));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_FIFOTH:    0x%08x\n",
		mshci_readl(host, MSHCI_FIFOTH));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_CDETECT:   0x%08x\n",
		mshci_readl(host, MSHCI_CDETECT));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_WRTPRT:    0x%08x\n",
		mshci_readl(host, MSHCI_WRTPRT));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_GPIO:      0x%08x\n",
		mshci_readl(host, MSHCI_GPIO));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_TCBCNT:    0x%08x\n",
		mshci_readl(host, MSHCI_TCBCNT));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_TBBCNT:    0x%08x\n",
		mshci_readl(host, MSHCI_TBBCNT));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_DEBNCE:    0x%08x\n",
		mshci_readl(host, MSHCI_DEBNCE));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_USRID:     0x%08x\n",
		mshci_readl(host, MSHCI_USRID));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_VERID:     0x%08x\n",
		mshci_readl(host, MSHCI_VERID));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_HCON:      0x%08x\n",
		mshci_readl(host, MSHCI_HCON));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_UHS_REG:   0x%08x\n",
		mshci_readl(host, MSHCI_UHS_REG));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_BMOD:      0x%08x\n",
		mshci_readl(host, MSHCI_BMOD));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_PLDMND:   0x%08x\n",
		mshci_readl(host, MSHCI_PLDMND));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_DBADDR:    0x%08x\n",
		mshci_readl(host, MSHCI_DBADDR));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_IDSTS:     0x%08x\n",
		mshci_readl(host, MSHCI_IDSTS));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_IDINTEN:   0x%08x\n",
		mshci_readl(host, MSHCI_IDINTEN));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_DSCADDR:   0x%08x\n",
		mshci_readl(host, MSHCI_DSCADDR));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_BUFADDR:   0x%08x\n",
		mshci_readl(host, MSHCI_BUFADDR));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_WAKEUPCON: 0x%08x\n",
		mshci_readl(host, MSHCI_WAKEUPCON));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_CLOCKCON:  0x%08x\n",
		mshci_readl(host, MSHCI_CLOCKCON));
	printk(KERN_DEBUG DRIVER_NAME ": MSHCI_FIFODAT:   0x%08x\n",
		mshci_readl(host, MSHCI_FIFODAT + host->data_addr));
	printk(KERN_DEBUG DRIVER_NAME ": ===========================================\n");
}


/*****************************************************************************\
 *                                                                           *
 * Low level functions                                                       *
 *                                                                           *
\*****************************************************************************/

static void mshci_clear_set_irqs(struct mshci_host *host, u32 clear, u32 set)
{
	u32 ier;

	ier = mshci_readl(host, MSHCI_INTMSK);
	ier &= ~clear;
	ier |= set;
	mshci_writel(host, ier, MSHCI_INTMSK);
}

static void mshci_unmask_irqs(struct mshci_host *host, u32 irqs)
{
	mshci_clear_set_irqs(host, 0, irqs);
}

static void mshci_mask_irqs(struct mshci_host *host, u32 irqs)
{
	mshci_clear_set_irqs(host, irqs, 0);
}

static void mshci_set_card_detection(struct mshci_host *host, bool enable)
{
	u32 irqs = INTMSK_CDETECT;

	/* it can makes a problme if enable CD_DETECT interrupt,
	 * when CD pin dose not exist. */
	if (host->quirks & MSHCI_QUIRK_BROKEN_CARD_DETECTION ||
			host->quirks & MSHCI_QUIRK_BROKEN_PRESENT_BIT) {
		mshci_mask_irqs(host, irqs);
	} else if (enable) {
		mshci_unmask_irqs(host, irqs);
	} else {
		mshci_mask_irqs(host, irqs);
	}
}

static void mshci_enable_card_detection(struct mshci_host *host)
{
	mshci_set_card_detection(host, true);
}

static void mshci_disable_card_detection(struct mshci_host *host)
{
	mshci_set_card_detection(host, false);
}

static void mshci_reset_ciu(struct mshci_host *host)
{
	u32 timeout = 100;
	u32 ier;

	ier = mshci_readl(host, MSHCI_CTRL);
	ier |= CTRL_RESET;

	mshci_writel(host, ier, MSHCI_CTRL);
	while (mshci_readl(host, MSHCI_CTRL) & CTRL_RESET) {
		if (timeout == 0) {
			printk(KERN_ERR "%s: Reset CTRL never completed.\n",
				mmc_hostname(host->mmc));
			mshci_dumpregs(host);
			return;
		}
		timeout--;
		mdelay(1);
	}
}

static void mshci_reset_fifo(struct mshci_host *host)
{
	u32 timeout = 100;
	u32 ier;

	ier = mshci_readl(host, MSHCI_CTRL);
	ier |= FIFO_RESET;

	mshci_writel(host, ier, MSHCI_CTRL);
	while (mshci_readl(host, MSHCI_CTRL) & FIFO_RESET) {
		if (timeout == 0) {
			printk(KERN_ERR "%s: Reset FIFO never completed.\n",
				mmc_hostname(host->mmc));
			mshci_dumpregs(host);
			return;
		}
		timeout--;
		mdelay(1);
	}
}

static void mshci_reset_dma(struct mshci_host *host)
{
	u32 timeout = 100;
	u32 ier;

	ier = mshci_readl(host, MSHCI_CTRL);
	ier |= DMA_RESET;

	mshci_writel(host, ier, MSHCI_CTRL);
	while (mshci_readl(host, MSHCI_CTRL) & DMA_RESET) {
		if (timeout == 0) {
			printk(KERN_ERR "%s: Reset DMA never completed.\n",
				mmc_hostname(host->mmc));
			mshci_dumpregs(host);
			return;
		}
		timeout--;
		mdelay(1);
	}
}

static void mshci_reset_all(struct mshci_host *host)
{
	int count, err = 0;

	/* Wait max 100 ms */
	count = 10000;

	/* before reset ciu, it should check DATA0. if when DATA0 is low and
	   it resets ciu, it might make a problem */
	do {
		if (!(mshci_readl(host, MSHCI_STATUS) & (1<<9))) {
			udelay(100);
			if (!(mshci_readl(host, MSHCI_STATUS) & (1<<9))) {
				udelay(100);
				if (!(mshci_readl(host, MSHCI_STATUS) & (1<<9)))
					break;
			}
		}
		if (count == 0) {
			printk(KERN_ERR "%s: Controller never released "
				"data0 before reset ciu.\n",
				mmc_hostname(host->mmc));
			mshci_dumpregs(host);
			err = 1;
			break;
		}
		count--;
		udelay(10);
	} while (1);

	if (err && host->ops->init_card) {
		printk(KERN_ERR "%s: eMMC's data lines get low.\n"
			"Reset eMMC.\n", mmc_hostname(host->mmc));
		host->ops->init_card(host);
	}

	mshci_reset_ciu(host);
	udelay(1);
	mshci_reset_fifo(host);
	udelay(1);
	mshci_reset_dma(host);
	udelay(1);
}

static void mshci_init(struct mshci_host *host)
{
	mshci_reset_all(host);

	/* clear interrupt status */
	mshci_writel(host, INTMSK_ALL, MSHCI_RINTSTS);

	mshci_clear_set_irqs(host, INTMSK_ALL,
		INTMSK_CDETECT | INTMSK_RE |
		INTMSK_CDONE | INTMSK_DTO | INTMSK_TXDR | INTMSK_RXDR |
		INTMSK_RCRC | INTMSK_DCRC | INTMSK_RTO | INTMSK_DRTO |
		INTMSK_HTO | INTMSK_FRUN | INTMSK_HLE | INTMSK_SBE |
		INTMSK_EBE);
}

static void mshci_reinit(struct mshci_host *host)
{
	mshci_init(host);
	mshci_enable_card_detection(host);
}

/*****************************************************************************\
 *                                                                           *
 * Core functions                                                            *
 *                                                                           *
\*****************************************************************************/

static void mshci_read_block_pio(struct mshci_host *host)
{
	unsigned long flags;
	size_t fifo_cnt, len, chunk;
	u32 uninitialized_var(scratch);
	u8 *buf;

	DBG("PIO reading\n");

	fifo_cnt = (mshci_readl(host, MSHCI_STATUS)&FIFO_COUNT)>>17;
	fifo_cnt *= FIFO_WIDTH;
	chunk = 0;

	local_irq_save(flags);

	while (fifo_cnt) {
		if (!sg_miter_next(&host->sg_miter))
			BUG();

		len = min(host->sg_miter.length, fifo_cnt);

		fifo_cnt -= len;
		host->sg_miter.consumed = len;

		buf = host->sg_miter.addr;

		while (len) {
			if (chunk == 0) {
				scratch = mshci_readl(host,
					MSHCI_FIFODAT + host->data_addr);
				chunk = 4;
			}

			*buf = scratch & 0xFF;

			buf++;
			scratch >>= 8;
			chunk--;
			len--;
		}
	}

	sg_miter_stop(&host->sg_miter);

	local_irq_restore(flags);
}

static void mshci_write_block_pio(struct mshci_host *host)
{
	unsigned long flags;
	size_t fifo_cnt, len, chunk;
	u32 scratch;
	u8 *buf;

	DBG("PIO writing\n");

	fifo_cnt = 8;

	fifo_cnt *= FIFO_WIDTH;
	chunk = 0;
	scratch = 0;

	local_irq_save(flags);

	while (fifo_cnt) {
		if (!sg_miter_next(&host->sg_miter)) {

			/* Even though transfer is complete,
			 * TXDR interrupt occurs again.
			 * So, it has to check that it has really
			 * no next sg buffer or just DTO interrupt
			 * has not occured yet.
			 */

			if ((host->data->blocks * host->data->blksz) ==
					host->data_transfered)
				break; /* transfer done but DTO not yet */
			BUG();
		}
		len = min(host->sg_miter.length, fifo_cnt);

		fifo_cnt -= len;
		host->sg_miter.consumed = len;
		host->data_transfered += len;

		buf = (host->sg_miter.addr);

		while (len) {
			scratch |= (u32)*buf << (chunk * 8);

			buf++;
			chunk++;
			len--;

			if ((chunk == 4) || ((len == 0) && (fifo_cnt == 0))) {
				mshci_writel(host, scratch,
				MSHCI_FIFODAT + host->data_addr);
				chunk = 0;
				scratch = 0;
			}
		}
	}

	sg_miter_stop(&host->sg_miter);

	local_irq_restore(flags);
}

static void mshci_transfer_pio(struct mshci_host *host)
{
	BUG_ON(!host->data);

	if (host->blocks == 0)
		return;

	if (host->data->flags & MMC_DATA_READ)
		mshci_read_block_pio(host);
	else
		mshci_write_block_pio(host);

	DBG("PIO transfer complete.\n");
}

static void mshci_set_mdma_desc(u8 *desc_vir, u8 *desc_phy,
				u32 des0, u32 des1, u32 des2)
{
	((struct mshci_idmac *)(desc_vir))->des0 = des0;
	((struct mshci_idmac *)(desc_vir))->des1 = des1;
	((struct mshci_idmac *)(desc_vir))->des2 = des2;
	((struct mshci_idmac *)(desc_vir))->des3 = (u32)desc_phy +
					sizeof(struct mshci_idmac);
}

static int mshci_mdma_table_pre(struct mshci_host *host,
	struct mmc_data *data)
{
	int direction;

	u8 *desc_vir, *desc_phy;
	dma_addr_t addr;
	int len;

	struct scatterlist *sg;
	int i;
	u32 des_flag;
	u32 size_idmac = sizeof(struct mshci_idmac);

	if (data->flags & MMC_DATA_READ)
		direction = DMA_FROM_DEVICE;
	else
		direction = DMA_TO_DEVICE;

	if (!data->host_cookie) {
		if (host->ops->dma_map_sg && data->blocks >= 2048) {
			/* if transfer size is bigger than 1MiB */
			host->sg_count = host->ops->dma_map_sg(host,
				mmc_dev(host->mmc),
				data->sg, data->sg_len, direction, 2);
		} else if (host->ops->dma_map_sg && data->blocks >= 128) {
			/* if transfer size is bigger than 64KiB */
			host->sg_count = host->ops->dma_map_sg(host,
				mmc_dev(host->mmc),
				data->sg, data->sg_len, direction, 1);
		} else {
			host->sg_count = dma_map_sg(mmc_dev(host->mmc),
				data->sg, data->sg_len, direction);
		}

		if (host->sg_count == 0)
			goto fail;
	} else
		host->sg_count = data->host_cookie;

	desc_vir = host->idma_desc;

	/* to know phy address */
	host->idma_addr = dma_map_single(mmc_dev(host->mmc),
				host->idma_desc,
				/* cache flush for only transfer size */
				(host->sg_count+1) * 16,
				DMA_TO_DEVICE);
	if (dma_mapping_error(mmc_dev(host->mmc), host->idma_addr))
		goto unmap_entries;
	BUG_ON(host->idma_addr & 0x3);

	desc_phy = (u8 *)host->idma_addr;

	for_each_sg(data->sg, sg, host->sg_count, i) {
		addr = sg_dma_address(sg);
		len = sg_dma_len(sg);

		/* tran, valid */
		des_flag = (MSHCI_IDMAC_OWN|MSHCI_IDMAC_CH);
		des_flag |= (i == 0) ? MSHCI_IDMAC_FS : 0;

		mshci_set_mdma_desc(desc_vir, desc_phy, des_flag, len, addr);
		desc_vir += size_idmac;
		desc_phy += size_idmac;

		/*
		 * If this triggers then we have a calculation bug
		 * somewhere. :/
		 */
		WARN_ON((desc_vir - host->idma_desc) > MSHCI_MAX_DMA_LIST * \
				size_idmac);
	}

	/*
	* Add a terminating flag.
	 */
	((struct mshci_idmac *)(desc_vir-size_idmac))->des0 |= MSHCI_IDMAC_LD;

	/* it has to dma map again to resync vir data to phy data  */
	host->idma_addr = dma_map_single(mmc_dev(host->mmc),
				host->idma_desc,
				/* cache flush for only transfer size */
				(host->sg_count+1) * 16,
				DMA_TO_DEVICE);
	if (dma_mapping_error(mmc_dev(host->mmc), host->idma_addr))
		goto unmap_entries;
	BUG_ON(host->idma_addr & 0x3);

	return 0;

unmap_entries:
	if (host->ops->dma_unmap_sg && data->blocks >= 2048) {
		/* if transfer size is bigger than 1MiB */
		host->ops->dma_unmap_sg(host, mmc_dev(host->mmc),
			data->sg, data->sg_len, direction, 2);
	} else if (host->ops->dma_unmap_sg && data->blocks >= 128) {
		/* if transfer size is bigger than 64KiB */
		host->ops->dma_unmap_sg(host, mmc_dev(host->mmc),
			data->sg, data->sg_len, direction, 1);
	} else {
		dma_unmap_sg(mmc_dev(host->mmc),
			data->sg, data->sg_len, direction);
	}
fail:
	return -EINVAL;
}

static void mshci_idma_table_post(struct mshci_host *host,
	struct mmc_data *data)
{
	int direction;

	if (data->flags & MMC_DATA_READ)
		direction = DMA_FROM_DEVICE;
	else
		direction = DMA_TO_DEVICE;

	dma_unmap_single(mmc_dev(host->mmc), host->idma_addr,
				/* cache flush for only transfer size */
				(host->sg_count+1) * 16,
				DMA_TO_DEVICE);

	if (!host->mmc->ops->post_req || !data->host_cookie) {
		if (host->ops->dma_unmap_sg && data->blocks >= 2048) {
			/* if transfer size is bigger than 1MiB */
			host->ops->dma_unmap_sg(host, mmc_dev(host->mmc),
				data->sg, data->sg_len, direction, 2);
		} else if (host->ops->dma_unmap_sg && data->blocks >= 128) {
			/* if transfer size is bigger than 64KiB */
			host->ops->dma_unmap_sg(host, mmc_dev(host->mmc),
				data->sg, data->sg_len, direction, 1);
		} else {
			dma_unmap_sg(mmc_dev(host->mmc),
				data->sg, data->sg_len, direction);
		}
	}
}

static u32 mshci_calc_timeout(struct mshci_host *host, struct mmc_data *data)
{
	return 0xffffffff; /* this value SHOULD be optimized */
}

static void mshci_set_transfer_irqs(struct mshci_host *host)
{
	u32 dma_irqs = INTMSK_DMA;
	u32 pio_irqs = INTMSK_TXDR | INTMSK_RXDR;

	if (host->flags & MSHCI_REQ_USE_DMA)
		mshci_clear_set_irqs(host, dma_irqs, 0);
	else
		mshci_clear_set_irqs(host, 0, pio_irqs);
}

static void mshci_prepare_data(struct mshci_host *host, struct mmc_data *data)
{
	u32 count;
	u32 ret;

	WARN_ON(host->data);

	if (data == NULL)
		return;

	BUG_ON(data->blksz > host->mmc->max_blk_size);
	BUG_ON(data->blocks > host->mmc->max_blk_count);

	host->data = data;
	host->data_early = 0;

	count = mshci_calc_timeout(host, data);
	mshci_writel(host, count, MSHCI_TMOUT);

	mshci_reset_fifo(host);

	if (host->flags & (MSHCI_USE_IDMA))
		host->flags |= MSHCI_REQ_USE_DMA;

	if (data->host_cookie)
		goto check_done;
	/*
	 * FIXME: This doesn't account for merging when mapping the
	 * scatterlist.
	 */
	if (host->flags & MSHCI_REQ_USE_DMA) {
		/* mshc's IDMAC can't transfer data that is not aligned
		 * or has length not divided by 4 byte. */
		int i;
		struct scatterlist *sg;

		for_each_sg(data->sg, sg, data->sg_len, i) {
			if (sg->length & 0x3) {
				DBG("Reverting to PIO because of "
					"transfer size (%d)\n",
					sg->length);
				host->flags &= ~MSHCI_REQ_USE_DMA;
				break;
			} else if (sg->offset & 0x3) {
				DBG("Reverting to PIO because of "
					"bad alignment\n");
				host->flags &= ~MSHCI_REQ_USE_DMA;
				break;
			}
		}
	}
check_done:

	if (host->flags & MSHCI_REQ_USE_DMA) {
		ret = mshci_mdma_table_pre(host, data);
		if (ret) {
			/*
			 * This only happens when someone fed
			 * us an invalid request.
			 */
			WARN_ON(1);
			host->flags &= ~MSHCI_REQ_USE_DMA;
		} else {
			mshci_writel(host, host->idma_addr,
				MSHCI_DBADDR);
		}
	}

	if (host->flags & MSHCI_REQ_USE_DMA) {
		/* enable DMA, IDMA interrupts and IDMAC */
		mshci_writel(host, (mshci_readl(host, MSHCI_CTRL) |
					ENABLE_IDMAC|DMA_ENABLE), MSHCI_CTRL);
		mshci_writel(host, (mshci_readl(host, MSHCI_BMOD) |
					(BMOD_IDMAC_ENABLE|BMOD_IDMAC_FB)),
					MSHCI_BMOD);
		mshci_writel(host, INTMSK_IDMAC_ERROR, MSHCI_IDINTEN);
	}

	if (!(host->flags & MSHCI_REQ_USE_DMA)) {
		int flags;

		flags = SG_MITER_ATOMIC;
		if (host->data->flags & MMC_DATA_READ)
			flags |= SG_MITER_TO_SG;
		else
			flags |= SG_MITER_FROM_SG;

		sg_miter_start(&host->sg_miter, data->sg, data->sg_len, flags);
		host->blocks = data->blocks;

		printk(KERN_ERR "it starts transfer on PIO\n");
	}

	/* set transfered data as 0. this value only uses for PIO write */
	host->data_transfered = 0;
	mshci_set_transfer_irqs(host);

	mshci_writel(host, data->blksz, MSHCI_BLKSIZ);
	mshci_writel(host, (data->blocks * data->blksz), MSHCI_BYTCNT);
}

static u32 mshci_set_transfer_mode(struct mshci_host *host,
	struct mmc_data *data)
{
	u32 ret = 0;

	if (data == NULL)
		return ret;

	WARN_ON(!host->data);

	/* this cmd has data to transmit */
	ret |= CMD_DATA_EXP_BIT;

	if (data->flags & MMC_DATA_WRITE)
		ret |= CMD_RW_BIT;
	if (data->flags & MMC_DATA_STREAM)
		ret |= CMD_TRANSMODE_BIT;

	return ret;
}

static void mshci_finish_data(struct mshci_host *host)
{
	struct mmc_data *data;

	BUG_ON(!host->data);

	data = host->data;
	host->data = NULL;

	if (host->flags & MSHCI_REQ_USE_DMA) {
		mshci_idma_table_post(host, data);
		/* disable IDMAC and DMA interrupt */
		mshci_writel(host, (mshci_readl(host, MSHCI_CTRL) &
				~(DMA_ENABLE|ENABLE_IDMAC)), MSHCI_CTRL);
		/* mask all interrupt source of IDMAC */
		mshci_writel(host, 0x0, MSHCI_IDINTEN);
	}

	if (data->error) {
		/* to go to idle state */
		mshci_reset_ciu(host);
		/* to clear fifo */
		mshci_reset_fifo(host);
		/* to reset dma */
		mshci_reset_dma(host);
		data->bytes_xfered = 0;
	} else
		data->bytes_xfered = data->blksz * data->blocks;

	/*
	 * Need to send CMD12 if -
	 * a) open-ended multiblock transfer (no CMD23)
	 * b) error in multiblock transfer
	 */
	if (data->stop && ((data->error) ||
	 !(host->mmc->caps & MMC_CAP_CMD23) ||
	 ((host->mmc->caps & MMC_CAP_CMD23) &&
	 !host->mrq->sbc))) /* packed cmd case */
		mshci_send_command(host, data->stop);
	else
		tasklet_schedule(&host->finish_tasklet);
}

static void mshci_wait_release_start_bit(struct mshci_host *host)
{
	u32 loop_count = 1000000;

	ktime_t expires;
	u64 add_time = 100000; /* 100us */

	/* before off clock, make sure data busy is released. */
	while (mshci_readl(host, MSHCI_STATUS) & (1<<9) && --loop_count) {
		spin_unlock_irqrestore(&host->lock, host->sl_flags);
		expires = ktime_add_ns(ktime_get(), add_time);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_hrtimeout(&expires, HRTIMER_MODE_ABS);
		spin_lock_irqsave(&host->lock, host->sl_flags);
	}
	if (loop_count == 0)
		printk(KERN_ERR "%s: cmd_strt_bit not released for 11sec\n",
				mmc_hostname(host->mmc));

	loop_count = 1000000;
	do {
		if (!(mshci_readl(host, MSHCI_CMD) & CMD_STRT_BIT))
			break;
		loop_count--;
		udelay(1);
	} while (loop_count);
	if (loop_count == 0)
		printk(KERN_ERR "%s: cmd_strt_bit not released for 1sec\n",
				mmc_hostname(host->mmc));
}

static void mshci_clock_onoff(struct mshci_host *host, bool val)
{
	mshci_wait_release_start_bit(host);

	if (val) {
		mshci_writel(host, (0x1<<0), MSHCI_CLKENA);
		mshci_writel(host, 0, MSHCI_CMD);
		mshci_writel(host, CMD_ONLY_CLK, MSHCI_CMD);
	} else {
		mshci_writel(host, (0x0<<0), MSHCI_CLKENA);
		mshci_writel(host, 0, MSHCI_CMD);
		mshci_writel(host, CMD_ONLY_CLK, MSHCI_CMD);
	}
}

static void mshci_send_command(struct mshci_host *host, struct mmc_command *cmd)
{
	int flags, ret;

	WARN_ON(host->cmd);

	/* clear error_state */
	if (cmd->opcode != 12)
		host->error_state = 0;

	/* disable interrupt before issuing cmd to the card. */
	mshci_writel(host, (mshci_readl(host, MSHCI_CTRL) & ~INT_ENABLE),
					MSHCI_CTRL);

	mod_timer(&host->timer, jiffies + 10 * HZ);

	host->cmd = cmd;

	mshci_prepare_data(host, cmd->data);

	mshci_writel(host, cmd->arg, MSHCI_CMDARG);

	flags = mshci_set_transfer_mode(host, cmd->data);

	if ((cmd->flags & MMC_RSP_136) && (cmd->flags & MMC_RSP_BUSY)) {
		printk(KERN_ERR "%s: Unsupported response type!\n",
			mmc_hostname(host->mmc));
		cmd->error = -EINVAL;
		tasklet_schedule(&host->finish_tasklet);
		return;
	}

	if (cmd->flags & MMC_RSP_PRESENT) {
		flags |= CMD_RESP_EXP_BIT;
		if (cmd->flags & MMC_RSP_136)
			flags |= CMD_RESP_LENGTH_BIT;
	}
	if (cmd->flags & MMC_RSP_CRC)
		flags |= CMD_CHECK_CRC_BIT;

	flags |= (cmd->opcode | CMD_STRT_BIT | host->hold_bit |
				CMD_WAIT_PRV_DAT_BIT);

	ret = mshci_readl(host, MSHCI_CMD);
	if (ret & CMD_STRT_BIT)
		printk(KERN_ERR "CMD busy. current cmd %d. last cmd reg 0x%x\n",
			cmd->opcode, ret);

	mshci_writel(host, flags, MSHCI_CMD);

	/* enable interrupt upon it sends a command to the card. */
	mshci_writel(host, (mshci_readl(host, MSHCI_CTRL) | INT_ENABLE),
					MSHCI_CTRL);
}

static void mshci_finish_command(struct mshci_host *host)
{
	int i;

	BUG_ON(host->cmd == NULL);

	if (host->cmd->flags & MMC_RSP_PRESENT) {
		if (host->cmd->flags & MMC_RSP_136) {
			/*
			 * response data are overturned.
			 */
			for (i = 0; i < 4; i++) {
				host->cmd->resp[0] = mshci_readl(host,
								MSHCI_RESP3);
				host->cmd->resp[1] = mshci_readl(host,
								MSHCI_RESP2);
				host->cmd->resp[2] = mshci_readl(host,
								MSHCI_RESP1);
				host->cmd->resp[3] = mshci_readl(host,
								MSHCI_RESP0);
			}
		} else {
			host->cmd->resp[0] = mshci_readl(host, MSHCI_RESP0);
		}
	}

	host->cmd->error = 0;

	if (host->data && host->data_early)
		mshci_finish_data(host);

	if (!host->cmd->data)
		tasklet_schedule(&host->finish_tasklet);

	host->cmd = NULL;
}

static void mshci_set_clock(struct mshci_host *host,
				unsigned int clock, u32 ddr)
{
	int div;

	/* befor changing clock. clock needs to be off. */
	mshci_clock_onoff(host, CLK_DISABLE);

	if (clock == 0)
		goto out;

	if (clock >= host->max_clk) {
		div = 0;
	} else {
		for (div = 1; div <= 0xff; div++) {
			/* div value should not be greater than 0xff */
			if ((host->max_clk / (div<<1)) <= clock)
				break;
		}
	}

	mshci_wait_release_start_bit(host);

	mshci_writel(host, div, MSHCI_CLKDIV);

	mshci_writel(host, 0, MSHCI_CMD);
	mshci_writel(host, CMD_ONLY_CLK, MSHCI_CMD);
	mshci_writel(host, mshci_readl(host, MSHCI_CMD)&(~CMD_SEND_CLK_ONLY),
					MSHCI_CMD);

	mshci_clock_onoff(host, CLK_ENABLE);

out:
	host->clock = clock;
}

static void mshci_set_power(struct mshci_host *host, unsigned short power)
{
	u8 pwr = power;

	if (power == (unsigned short)-1)
		pwr = 0;

	if (host->pwr == pwr)
		return;

	host->pwr = pwr;

	if (pwr == 0)
		mshci_writel(host, 0, MSHCI_PWREN);
	else
		mshci_writel(host, 0x1, MSHCI_PWREN);
}

#ifdef CONFIG_MMC_POLLING_WAIT_CMD23
static void mshci_check_sbc_status(struct mshci_host *host, int intmask)
{
	int timeout, int_status;;

	/* wait for command done or error by polling */
	timeout = 0x100000; /* it is bigger than 1ms */
	do {
		int_status = mshci_readl(host, MSHCI_RINTSTS);
		if (int_status & CMD_STATUS)
			break;
		timeout--;
	} while (timeout);

	/* clear pending interupt bit */
	mshci_writel(host, int_status, MSHCI_RINTSTS);

	/* check whether command error has been occured or not. */
	if (int_status & INTMSK_HTO) {
		printk(KERN_ERR "%s: %s Host timeout error\n",
					mmc_hostname(host->mmc),
					__func__);
		host->mrq->sbc->error = -ETIMEDOUT;
	} else if (int_status & INTMSK_DRTO) {
		printk(KERN_ERR "%s: %s Data read timeout error\n",
					mmc_hostname(host->mmc),
					__func__);
		host->mrq->sbc->error = -ETIMEDOUT;
	} else if (int_status & INTMSK_SBE) {
		printk(KERN_ERR "%s: %s FIFO Start bit error\n",
					mmc_hostname(host->mmc),
					__func__);
		host->mrq->sbc->error = -EIO;
	} else if (int_status & INTMSK_EBE) {
		printk(KERN_ERR "%s: %s FIFO Endbit/Write no CRC error\n",
					mmc_hostname(host->mmc),
					__func__);
		host->mrq->sbc->error = -EIO;
	} else if (int_status & INTMSK_DCRC) {
		printk(KERN_ERR "%s: %s Data CRC error\n",
					mmc_hostname(host->mmc),
					__func__);
		host->mrq->sbc->error = -EIO;
	} else if (int_status & INTMSK_FRUN) {
		printk(KERN_ERR "%s: %s FIFO underrun/overrun error\n",
					mmc_hostname(host->mmc),
					__func__);
		host->mrq->sbc->error = -EIO;
	} else if (int_status & CMD_ERROR) {
		printk(KERN_ERR "%s: %s cmd %s error\n",
					mmc_hostname(host->mmc),
					__func__, (intmask & INTMSK_RCRC) ?
					"response crc" :
					(intmask & INTMSK_RE) ? "response" :
					"response timeout");
		host->mrq->sbc->error = -ETIMEDOUT;
	}

	if (host->mrq->sbc->error) {
		/* restore interrupt mask bit */
		mshci_writel(host, intmask, MSHCI_INTMSK);
		return;
	}

	if (!timeout) {
		printk(KERN_ERR "%s: %s no interrupt occured\n",
			mmc_hostname(host->mmc), __func__);
		host->mrq->sbc->error = -ETIMEDOUT;
		/* restore interrupt mask bit */
		mshci_writel(host, intmask, MSHCI_INTMSK);
		return;
	}

	/* command done interrupt has been occured with no errors.
	   nothing to do. just return to the previous function */
	if ((int_status & INTMSK_CDONE) && !(int_status & CMD_ERROR)) {
		/* restore interrupt mask bit */
		mshci_writel(host, intmask, MSHCI_INTMSK);
		return;
	}

	/* should not be here */
	printk(KERN_ERR "%s: an error that has not to be occured was"
			" occured 0x%x\n",mmc_hostname(host->mmc),int_status);
}

static void mshci_send_sbc(struct mshci_host *host, struct mmc_command *cmd)
{
	int flags = 0, ret, intmask;

	WARN_ON(host->cmd);

	/* disable interrupt before issuing cmd to the card. */
	mshci_writel(host, (mshci_readl(host, MSHCI_CTRL) & ~INT_ENABLE),
					MSHCI_CTRL);

	host->cmd = cmd;

	mod_timer(&host->timer, jiffies + 10 * HZ);

	mshci_writel(host, cmd->arg, MSHCI_CMDARG);

	if ((cmd->flags & MMC_RSP_136) && (cmd->flags & MMC_RSP_BUSY)) {
		printk(KERN_ERR "%s: Unsupported response type!\n",
			mmc_hostname(host->mmc));
		cmd->error = -EINVAL;
		tasklet_schedule(&host->finish_tasklet);
		return;
	}

	if (cmd->flags & MMC_RSP_PRESENT) {
		flags |= CMD_RESP_EXP_BIT;
		if (cmd->flags & MMC_RSP_136)
			flags |= CMD_RESP_LENGTH_BIT;
	}
	if (cmd->flags & MMC_RSP_CRC)
		flags |= CMD_CHECK_CRC_BIT;

	flags |= (cmd->opcode | CMD_STRT_BIT | host->hold_bit |
				CMD_WAIT_PRV_DAT_BIT);

	ret = mshci_readl(host, MSHCI_CMD);
	if (ret & CMD_STRT_BIT)
		printk(KERN_ERR "CMD busy. current cmd %d. last cmd reg 0x%x\n",
			cmd->opcode, ret);

	/* backup interrupt mask bit */
	intmask = mshci_readl(host, MSHCI_INTMSK);

	/* disable interrupts for sbc command. it will wait for command done
	   by polling. it expects a faster repsonse */
	mshci_clear_set_irqs(host, INTMSK_ALL, 0);

	/* send command */
	mshci_writel(host, flags, MSHCI_CMD);

	/* enable interrupt upon it sends a command to the card. */
	mshci_writel(host, (mshci_readl(host, MSHCI_CTRL) | INT_ENABLE),
					MSHCI_CTRL);

	/* check the interrupt by polling */
	mshci_check_sbc_status(host,intmask);
}
#endif

/*****************************************************************************\
 *                                                                           *
 * MMC callbacks                                                             *
 *                                                                           *
\*****************************************************************************/

static void mshci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mshci_host *host;
	bool present;
	int timeout;
	ktime_t expires;
	u64 add_time = 50000; /* 50us */

	host = mmc_priv(mmc);

	WARN_ON(host->mrq != NULL);

	host->mrq = mrq;

	/* Wait max 1 sec */
	timeout = 100000;

	/* We shouldn't wait for data inihibit for stop commands, even
	   though they might use busy signaling */
	if (mrq->cmd->opcode == 12) {
		/* nothing to do */
	} else {
		for (;;) {
			spin_lock_irqsave(&host->lock, host->sl_flags);
			if (mshci_readl(host, MSHCI_STATUS) & (1<<9)) {
				if (timeout == 0) {
					printk(KERN_ERR "%s: Controller never"
						" released  data0.\n",
						mmc_hostname(host->mmc));
					mshci_dumpregs(host);

					mrq->cmd->error = -ENOTRECOVERABLE;
					host->error_state = 1;

					tasklet_schedule \
						(&host->finish_tasklet);
					spin_unlock_irqrestore \
						(&host->lock, host->sl_flags);
					return;
				}
				timeout--;

				/* if previous command made an error,
				* this function might be called by tasklet.
				* So, it SHOULD NOT use schedule_hrtimeout */
				if (host->error_state == 1) {
					spin_unlock_irqrestore
						(&host->lock, host->sl_flags);
					udelay(10);
				} else {
					spin_unlock_irqrestore
						(&host->lock, host->sl_flags);
					expires = ktime_add_ns
						(ktime_get(), add_time);
					set_current_state
						(TASK_UNINTERRUPTIBLE);
					schedule_hrtimeout
						(&expires, HRTIMER_MODE_ABS);
				}
			} else {
				spin_unlock_irqrestore(&host->lock,
						host->sl_flags);
				break;
			}
		}
	}
	spin_lock_irqsave(&host->lock, host->sl_flags);
	/* If polling, assume that the card is always present. */
	if (host->quirks & MSHCI_QUIRK_BROKEN_CARD_DETECTION ||
			host->quirks & MSHCI_QUIRK_BROKEN_PRESENT_BIT)
		present = true;
	else
		present = !(mshci_readl(host, MSHCI_CDETECT) & CARD_PRESENT);

	if (!present || host->flags & MSHCI_DEVICE_DEAD) {
		host->mrq->cmd->error = -ENOMEDIUM;
		tasklet_schedule(&host->finish_tasklet);
	} else {
#ifdef CONFIG_MMC_POLLING_WAIT_CMD23
		if (mrq->sbc) {
			mshci_send_sbc(host, mrq->sbc);
			if (mrq->sbc->error) {
				tasklet_schedule(&host->finish_tasklet);
			} else {
				if (host->cmd)
					host->cmd = NULL;
				mshci_send_command(host, mrq->cmd);
			}
		} else
#endif
			mshci_send_command(host, mrq->cmd);
	}

	mmiowb();
	spin_unlock_irqrestore(&host->lock, host->sl_flags);
}

static void mshci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mshci_host *host;
	u32 regs;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, host->sl_flags);

	if (host->flags & MSHCI_DEVICE_DEAD)
		goto out;

	if (ios->power_mode == MMC_POWER_OFF)
		mshci_reinit(host);

#ifdef CONFIG_MMC_CLKGATE
	/* gating the clock and out */
	if (mmc->clk_gated) {
		WARN_ON(ios->clock != 0);
		if (host->clock != 0)
			mshci_set_clock(host, ios->clock, ios->ddr);
		goto out;
	}
#endif

	if (host->ops->set_ios)
		host->ops->set_ios(host, ios);

	mshci_set_clock(host, ios->clock, ios->ddr);

	if (ios->power_mode == MMC_POWER_OFF)
		mshci_set_power(host, -1);
	else
		mshci_set_power(host, ios->vdd);

	regs = mshci_readl(host, MSHCI_UHS_REG);

	if (ios->bus_width == MMC_BUS_WIDTH_8) {
		mshci_writel(host, (0x1<<16), MSHCI_CTYPE);
		if (ios->timing == MMC_TIMING_UHS_DDR50) {
			regs |= (0x1 << 16);
			mshci_writel(host, regs, MSHCI_UHS_REG);
			/* if exynos4412 EVT1 or the latest one */
			if (soc_is_exynos4412() &&
				samsung_rev() >= EXYNOS4412_REV_1_0) {
				if ((host->max_clk/2) < 46300000) {
					mshci_writel(host, (0x00010001),
						MSHCI_CLKSEL);
				} else {
					mshci_writel(host, (0x00020002),
						MSHCI_CLKSEL);
				}
			} else {
				if ((host->max_clk/2) < 40000000)
					mshci_writel(host, (0x00010001),
						MSHCI_CLKSEL);
				else
					mshci_writel(host, (0x00020002),
						MSHCI_CLKSEL);
			}
		} else {
			regs &= ~(0x1 << 16);
			mshci_writel(host, regs|(0x0<<0), MSHCI_UHS_REG);
			mshci_writel(host, (0x00010001), MSHCI_CLKSEL);
		}
	} else if (ios->bus_width == MMC_BUS_WIDTH_4) {
		mshci_writel(host, (0x1<<0), MSHCI_CTYPE);
		if (ios->timing == MMC_TIMING_UHS_DDR50) {
			regs |= (0x1 << 16);
			mshci_writel(host, regs, MSHCI_UHS_REG);
			mshci_writel(host, (0x00010001), MSHCI_CLKSEL);
		} else {
			regs &= ~(0x1 << 16);
			mshci_writel(host, regs|(0x0<<0), MSHCI_UHS_REG);
			mshci_writel(host, (0x00010001), MSHCI_CLKSEL);
		}
	} else {
		regs &= ~(0x1 << 16);
		mshci_writel(host, regs|0, MSHCI_UHS_REG);
		mshci_writel(host, (0x0<<0), MSHCI_CTYPE);
		mshci_writel(host, (0x00010001), MSHCI_CLKSEL);
	}
out:
	mmiowb();
	spin_unlock_irqrestore(&host->lock, host->sl_flags);
}

static int mshci_get_ro(struct mmc_host *mmc)
{
	struct mshci_host *host;
	int wrtprt;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, host->sl_flags);

	if (host->quirks & MSHCI_QUIRK_NO_WP_BIT)
		wrtprt = host->ops->get_ro(mmc) ? 0 : WRTPRT_ON;
	else if (host->flags & MSHCI_DEVICE_DEAD)
		wrtprt = 0;
	else
		wrtprt = mshci_readl(host, MSHCI_WRTPRT);

	spin_unlock_irqrestore(&host->lock, host->sl_flags);

	return wrtprt & WRTPRT_ON;
}

static void mshci_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct mshci_host *host;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, host->sl_flags);

	if (host->flags & MSHCI_DEVICE_DEAD)
		goto out;

	if (enable)
		mshci_unmask_irqs(host, SDIO_INT_ENABLE);
	else
		mshci_mask_irqs(host, SDIO_INT_ENABLE);
out:
	mmiowb();

	spin_unlock_irqrestore(&host->lock, host->sl_flags);
}

static void mshci_init_card(struct mmc_host *mmc, struct mmc_card *card)
{
	struct mshci_host *host;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, host->sl_flags);

	if (host->flags & MSHCI_DEVICE_DEAD)
		goto out;

	if (host->ops->init_card)
		host->ops->init_card(host);
out:
	mmiowb();

	spin_unlock_irqrestore(&host->lock, host->sl_flags);
}

static void mshci_pre_req(struct mmc_host *mmc, struct mmc_request *mrq,
							bool is_first_req)
{
	struct mshci_host *host;
	struct mmc_data *data = mrq->data;
	int sg_count, direction;

	host = mmc_priv(mmc);
	spin_lock_irqsave(&host->lock, host->sl_flags);

	if (!data)
		goto out;

	if (data->host_cookie) {
		data->host_cookie = 0;
		goto out;
	}

	if (host->flags & MSHCI_USE_IDMA) {
		/* mshc's IDMAC can't transfer data that is not aligned
		 * or has length not divided by 4 byte. */
		int i;
		struct scatterlist *sg;

		for_each_sg(data->sg, sg, data->sg_len, i) {
			if (sg->length & 0x3) {
				DBG("Reverting to PIO because of "
					"transfer size (%d)\n",
					sg->length);
				data->host_cookie = 0;
				goto out;
			} else if (sg->offset & 0x3) {
				DBG("Reverting to PIO because of "
					"bad alignment\n");
				host->flags &= ~MSHCI_REQ_USE_DMA;
				data->host_cookie = 0;
				goto out;
			}
		}
	}

	if (data->flags & MMC_DATA_READ)
		direction = DMA_FROM_DEVICE;
	else
		direction = DMA_TO_DEVICE;

	if (host->ops->dma_map_sg && data->blocks >= 2048) {
		/* if transfer size is bigger than 1MiB */
		sg_count = host->ops->dma_map_sg(host,
			mmc_dev(host->mmc),
			data->sg, data->sg_len, direction, 2);
	} else if (host->ops->dma_map_sg && data->blocks >= 128) {
		/* if transfer size is bigger than 64KiB */
		sg_count = host->ops->dma_map_sg(host,
			mmc_dev(host->mmc),
			data->sg, data->sg_len, direction, 1);
	} else {
		sg_count = dma_map_sg(mmc_dev(host->mmc),
			data->sg, data->sg_len, direction);
	}

	if (sg_count == 0)
		data->host_cookie = 0;
	else
		data->host_cookie = sg_count;
out:
	spin_unlock_irqrestore(&host->lock, host->sl_flags);
	return;
}

static void mshci_post_req(struct mmc_host *mmc, struct mmc_request *mrq,
							int err)
{
	struct mshci_host *host;
	struct mmc_data *data = mrq->data;
	int direction;

	host = mmc_priv(mmc);
	spin_lock_irqsave(&host->lock, host->sl_flags);

	if (!data)
		goto out;

	if (data->flags & MMC_DATA_READ)
		direction = DMA_FROM_DEVICE;
	else
		direction = DMA_TO_DEVICE;

	if ((host->ops->dma_unmap_sg && data->blocks >= 2048 &&
		data->host_cookie)) {
		/* if transfer size is bigger than 1MiB */
		host->ops->dma_unmap_sg(host, mmc_dev(host->mmc),
			data->sg, data->sg_len, direction, 2);
	} else if ((host->ops->dma_unmap_sg && data->blocks >= 128 &&
		data->host_cookie)) {
		/* if transfer size is bigger than 64KiB */
		host->ops->dma_unmap_sg(host, mmc_dev(host->mmc),
			data->sg, data->sg_len, direction, 1);
	} else if (data->host_cookie) {
		dma_unmap_sg(mmc_dev(host->mmc),
			data->sg, data->sg_len, direction);
	}
out:
	spin_unlock_irqrestore(&host->lock, host->sl_flags);
	return;
}

static struct mmc_host_ops mshci_ops = {
	.request	= mshci_request,
	.set_ios	= mshci_set_ios,
	.get_ro		= mshci_get_ro,
	.enable_sdio_irq = mshci_enable_sdio_irq,
	.init_card	= mshci_init_card,
#ifdef CONFIG_MMC_MSHCI_ASYNC_OPS
	.pre_req	= mshci_pre_req,
	.post_req	= mshci_post_req,
#endif
};

/*****************************************************************************\
 *                                                                           *
 * Tasklets                                                                  *
 *                                                                           *
\*****************************************************************************/

static void mshci_tasklet_card(unsigned long param)
{
	struct mshci_host *host;

	host = (struct mshci_host *)param;

	spin_lock_irqsave(&host->lock, host->sl_flags);

	if ((host->quirks & MSHCI_QUIRK_BROKEN_CARD_DETECTION) ||
			(host->quirks & MSHCI_QUIRK_BROKEN_PRESENT_BIT) ||
			(mshci_readl(host, MSHCI_CDETECT) & CARD_PRESENT)) {
		if (host->mrq) {
			printk(KERN_ERR "%s: Card removed during transfer!\n",
				mmc_hostname(host->mmc));
			printk(KERN_ERR "%s: Resetting controller.\n",
				mmc_hostname(host->mmc));

			host->mrq->cmd->error = -ENOMEDIUM;
			tasklet_schedule(&host->finish_tasklet);
		}
	}

	spin_unlock_irqrestore(&host->lock, host->sl_flags);

	mmc_detect_change(host->mmc, msecs_to_jiffies(200));
}

static void mshci_tasklet_finish(unsigned long param)
{
	struct mshci_host *host;
	struct mmc_request *mrq;

	host = (struct mshci_host *)param;

	if (host == NULL)
		return;

	spin_lock_irqsave(&host->lock, host->sl_flags);

	del_timer(&host->timer);

	mrq = host->mrq;

	if (mrq == NULL || mrq->cmd == NULL)
		goto out;

	/*
	 * The controller needs a reset of internal state machines
	 * upon error conditions.
	 */
	if (!(host->flags & MSHCI_DEVICE_DEAD) &&
		(mrq->cmd->error ||
#ifdef CONFIG_MMC_POLLING_WAIT_CMD23
		(mrq->sbc && mrq->sbc->error) ||
#endif
		 (mrq->data && (mrq->data->error ||
		  (mrq->data->stop && mrq->data->stop->error))))) {
		mshci_reset_fifo(host);
	}

out:
	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;

	mmiowb();
	spin_unlock_irqrestore(&host->lock, host->sl_flags);

	if (mrq)
		mmc_request_done(host->mmc, mrq);
}

static void mshci_timeout_timer(unsigned long data)
{
	struct mshci_host *host;

	host = (struct mshci_host *)data;

	spin_lock_irqsave(&host->lock, host->sl_flags);

	if (host->mrq) {
		printk(KERN_ERR "%s: Timeout waiting for hardware "
			"interrupt.\n", mmc_hostname(host->mmc));
		mshci_dumpregs(host);

		if (host->data) {
			host->data->error = -ETIMEDOUT;
			mshci_finish_data(host);
		} else {
			if (host->cmd)
				host->cmd->error = -ETIMEDOUT;
			else
				host->mrq->cmd->error = -ETIMEDOUT;

			tasklet_schedule(&host->finish_tasklet);
		}
	}

	mmiowb();
	spin_unlock_irqrestore(&host->lock, host->sl_flags);
}

/*****************************************************************************\
 *                                                                           *
 * Interrupt handling                                                        *
 *                                                                           *
\*****************************************************************************/

static void mshci_cmd_irq(struct mshci_host *host, u32 intmask)
{
	BUG_ON(intmask == 0);

	if (!host->cmd) {
		printk(KERN_ERR "%s: Got command interrupt 0x%08x even "
			"though no command operation was in progress.\n",
			mmc_hostname(host->mmc), (unsigned)intmask);
		mshci_dumpregs(host);
		return;
	}

	if (intmask & INTMSK_RTO) {
		host->cmd->error = -ETIMEDOUT;
		printk(KERN_ERR "%s: cmd %d response timeout error\n",
				mmc_hostname(host->mmc), host->cmd->opcode);
	} else if (intmask & (INTMSK_RCRC | INTMSK_RE)) {
		host->cmd->error = -EILSEQ;
		printk(KERN_ERR "%s: cmd %d repsonse %s error\n",
				mmc_hostname(host->mmc), host->cmd->opcode,
				(intmask & INTMSK_RCRC) ? "crc" : "RE");
	}
	if (host->cmd->error) {
		/* to notify an error happend */
		host->error_state = 1;
#if defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_P4NOTE) || \
		defined(CONFIG_MACH_C1_USA_ATT)
		/* dh0421.hwang */
		if (host->mmc && host->mmc->card)
			mshci_dumpregs(host);
#endif
		tasklet_schedule(&host->finish_tasklet);
		return;
	}

	if (intmask & INTMSK_CDONE)
		mshci_finish_command(host);
}

static void mshci_data_irq(struct mshci_host *host, u32 intmask, u8 intr_src)
{
	BUG_ON(intmask == 0);

	if (!host->data) {
		/*
		 * The "data complete" interrupt is also used to
		 * indicate that a busy state has ended. See comment
		 * above in mshci_cmd_irq().
		 */
		if (host->cmd && (host->cmd->flags & MMC_RSP_BUSY)) {
			if (intmask & INTMSK_DTO) {
				mshci_finish_command(host);
				return;
			}
		}

		printk(KERN_ERR "%s: Got data interrupt 0x%08x from %s "
			"even though no data operation was in progress.\n",
			mmc_hostname(host->mmc), (unsigned)intmask,
			intr_src ? "MINT" : "IDMAC");
		mshci_dumpregs(host);

		return;
	}
	if (intr_src == INT_SRC_MINT) {
		if (intmask & INTMSK_HTO) {
			printk(KERN_ERR "%s: Host timeout error\n",
						mmc_hostname(host->mmc));
			host->data->error = -ETIMEDOUT;
#if 1	/* debugging for Host timeout error */
			mshci_dumpregs(host);
			panic("[TEST] %s: HTO error interrupt occured\n",
					mmc_hostname(host->mmc));
#endif
		} else if (intmask & INTMSK_DRTO) {
			printk(KERN_ERR "%s: Data read timeout error\n",
						mmc_hostname(host->mmc));
			host->data->error = -ETIMEDOUT;
		} else if (intmask & INTMSK_SBE) {
			printk(KERN_ERR "%s: FIFO Start bit error\n",
						mmc_hostname(host->mmc));
			host->data->error = -EIO;
		} else if (intmask & INTMSK_EBE) {
			printk(KERN_ERR "%s: FIFO Endbit/Write no CRC error\n",
						mmc_hostname(host->mmc));
			host->data->error = -EIO;
		} else if (intmask & INTMSK_DCRC) {
			printk(KERN_ERR "%s: Data CRC error\n",
						mmc_hostname(host->mmc));
			host->data->error = -EIO;
		} else if (intmask & INTMSK_FRUN) {
			printk(KERN_ERR "%s: FIFO underrun/overrun error\n",
						mmc_hostname(host->mmc));
			host->data->error = -EIO;
		}
	} else {
		if (intmask & IDSTS_FBE) {
			printk(KERN_ERR "%s: Fatal Bus error on DMA\n",
					mmc_hostname(host->mmc));
			host->data->error = -EIO;
		} else if (intmask & IDSTS_CES) {
			printk(KERN_ERR "%s: Card error on DMA\n",
					mmc_hostname(host->mmc));
			host->data->error = -EIO;
		} else if (intmask & IDSTS_DU) {
			printk(KERN_ERR "%s: Description error on DMA\n",
					mmc_hostname(host->mmc));
			host->data->error = -EIO;
		}
	}

	if (host->data->error) {
		/* to notify an error happend */
		host->error_state = 1;
#if defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_P4NOTE) || \
		defined(CONFIG_MACH_C1_USA_ATT)
		/* dh0421.hwang */
		if (host->mmc && host->mmc->card)
			mshci_dumpregs(host);
#endif
		mshci_finish_data(host);
	} else {
		if (!(host->flags & MSHCI_REQ_USE_DMA) &&
				(((host->data->flags & MMC_DATA_READ) &&
				(intmask & (INTMSK_RXDR | INTMSK_DTO))) ||
				((host->data->flags & MMC_DATA_WRITE) &&
					(intmask & (INTMSK_TXDR)))))
			mshci_transfer_pio(host);

		if (intmask & INTMSK_DTO) {
			if (host->cmd) {
				/*
				 * Data managed to finish before the
				 * command completed. Make sure we do
				 * things in the proper order.
				 */
				host->data_early = 1;
			} else {
				mshci_finish_data(host);
			}
		}
	}
}

static irqreturn_t mshci_irq(int irq, void *dev_id)
{
	irqreturn_t result;
	struct mshci_host *host = dev_id;
	u32 intmask;
	int cardint = 0;
	int timeout = 0x10000;

	spin_lock(&host->lock);

	intmask = mshci_readl(host, MSHCI_MINTSTS);

	if (!intmask || intmask == 0xffffffff) {
		/* check if there is a interrupt for IDMAC  */
		intmask = mshci_readl(host, MSHCI_IDSTS);
		if (intmask) {
			mshci_writel(host, intmask, MSHCI_IDSTS);
			mshci_data_irq(host, intmask, INT_SRC_IDMAC);
			result = IRQ_HANDLED;
			goto out;
			}
		result = IRQ_NONE;
		goto out;
	}
	DBG("*** %s got interrupt: 0x%08x\n",
		mmc_hostname(host->mmc), intmask);

	mshci_writel(host, intmask, MSHCI_RINTSTS);

	if (intmask & (INTMSK_CDETECT)) {
		if (!(host->mmc->caps & MMC_CAP_NONREMOVABLE))
			tasklet_schedule(&host->card_tasklet);
	}
	intmask &= ~INTMSK_CDETECT;

	if (intmask & CMD_STATUS) {
		if (!(intmask & INTMSK_CDONE) && (intmask & INTMSK_RTO)) {
			/*
			 * when a error about command timeout occurs,
			 * cmd done intr comes together.
			 * cmd done intr comes later than error intr.
			 * so, it has to wait for cmd done intr.
			 */
			while (--timeout && !(mshci_readl(host, MSHCI_MINTSTS)
				& INTMSK_CDONE))
				; /* Nothing to do */
			if (!timeout)
				printk(KERN_ERR"*** %s time out for	CDONE intr\n",
					mmc_hostname(host->mmc));
			else
				mshci_writel(host, INTMSK_CDONE,
					MSHCI_RINTSTS);
			mshci_cmd_irq(host, intmask & CMD_STATUS);
		} else {
			mshci_cmd_irq(host, intmask & CMD_STATUS);
		}
	}

	if (intmask & DATA_STATUS) {
		if (!(intmask & INTMSK_DTO) && (intmask & INTMSK_DRTO)) {
			/*
			 * when a error about data timout occurs,
			 * DTO intr comes together.
			 * DTO intr comes later than error intr.
			 * so, it has to wait for DTO intr.
			 */
			while (--timeout && !(mshci_readl(host, MSHCI_MINTSTS)
				& INTMSK_DTO))
				; /* Nothing to do */
			if (!timeout)
				printk(KERN_ERR"*** %s time out for	DTO intr\n",
					mmc_hostname(host->mmc));
			else
				mshci_writel(host, INTMSK_DTO,
					MSHCI_RINTSTS);
			mshci_data_irq(host, intmask & DATA_STATUS,
							INT_SRC_MINT);
		} else {
			mshci_data_irq(host, intmask & DATA_STATUS,
							INT_SRC_MINT);
		}
	}

	intmask &= ~(CMD_STATUS | DATA_STATUS);

	if (intmask & SDIO_INT_ENABLE)
		cardint = 1;

	intmask &= ~SDIO_INT_ENABLE;

	if (intmask) {
		printk(KERN_ERR "%s: Unexpected interrupt 0x%08x.\n",
			mmc_hostname(host->mmc), intmask);
		mshci_dumpregs(host);
	}

	result = IRQ_HANDLED;

	mmiowb();
out:
	spin_unlock(&host->lock);

	/*
	 * We have to delay this as it calls back into the driver.
	 */
	if (cardint)
		mmc_signal_sdio_irq(host->mmc);

	return result;
}

/*****************************************************************************\
 *                                                                           *
 * Suspend/resume                                                            *
 *                                                                           *
\*****************************************************************************/

#ifdef CONFIG_PM

int mshci_suspend_host(struct mshci_host *host, pm_message_t state)
{
	int ret;

	mshci_disable_card_detection(host);

	ret = mmc_suspend_host(host->mmc);
	if (ret)
		return ret;

	free_irq(host->irq, host);

	return 0;
}
EXPORT_SYMBOL_GPL(mshci_suspend_host);

int mshci_resume_host(struct mshci_host *host)
{
	int ret;
	int count;

	if (host->flags & (MSHCI_USE_IDMA)) {
		if (host->ops->enable_dma)
			host->ops->enable_dma(host);
	}

	mshci_init(host);

	ret = request_irq(host->irq, mshci_irq, IRQF_SHARED,
			  mmc_hostname(host->mmc), host);
	if (ret)
		return ret;

	mmiowb();

	mshci_fifo_init(host);

	/* set debounce filter value*/
	mshci_writel(host, 0xfffff, MSHCI_DEBNCE);

	/* clear card type. set 1bit mode */
	mshci_writel(host, 0x0, MSHCI_CTYPE);

	/* set bus mode register for IDMAC */
	if (host->flags & MSHCI_USE_IDMA) {
		mshci_writel(host, BMOD_IDMAC_RESET, MSHCI_BMOD);
		count = 100;
		while ((mshci_readl(host, MSHCI_BMOD) & BMOD_IDMAC_RESET)
				&& --count)
			; /* nothing to do */

		mshci_writel(host, (mshci_readl(host, MSHCI_BMOD) |
				(BMOD_IDMAC_ENABLE|BMOD_IDMAC_FB)), MSHCI_BMOD);
	}

	ret = mmc_resume_host(host->mmc);
	if (ret)
		return ret;

	mshci_enable_card_detection(host);

	return 0;
}
EXPORT_SYMBOL_GPL(mshci_resume_host);

#endif /* CONFIG_PM */

/*****************************************************************************\
 *                                                                           *
 * Device allocation/registration                                            *
 *                                                                           *
\*****************************************************************************/

struct mshci_host *mshci_alloc_host(struct device *dev,
	size_t priv_size)
{
	struct mmc_host *mmc;
	struct mshci_host *host;

	WARN_ON(dev == NULL);

	mmc = mmc_alloc_host(sizeof(struct mshci_host) + priv_size, dev);
	if (!mmc)
		return ERR_PTR(-ENOMEM);

	host = mmc_priv(mmc);
	host->mmc = mmc;

	return host;
}

static void mshci_fifo_init(struct mshci_host *host)
{
	int fifo_val, fifo_depth, fifo_threshold;

	fifo_val = mshci_readl(host, MSHCI_FIFOTH);
	fifo_depth = host->ops->get_fifo_depth(host);
	fifo_threshold = fifo_depth/2;
	host->fifo_threshold = fifo_threshold;
	host->fifo_depth = fifo_threshold*2;

	printk(KERN_INFO "%s: FIFO WMARK FOR RX 0x%x WX 0x%x. ###########\n",
		mmc_hostname(host->mmc), fifo_depth,
		    fifo_threshold);

	fifo_val &= ~(RX_WMARK | TX_WMARK | MSIZE_MASK);

	fifo_val |= (fifo_threshold | ((fifo_threshold-1)<<16));
	if (fifo_threshold >= 0x40)
		fifo_val |= MSIZE_64;
	else if (fifo_threshold >= 0x20)
		fifo_val |= MSIZE_32;
	else if (fifo_threshold >= 0x10)
		fifo_val |= MSIZE_16;
	else if (fifo_threshold >= 0x8)
		fifo_val |= MSIZE_8;
	else
		fifo_val |= MSIZE_1;

	mshci_writel(host, fifo_val, MSHCI_FIFOTH);
}
EXPORT_SYMBOL_GPL(mshci_alloc_host);

int mshci_add_host(struct mshci_host *host)
{
	struct mmc_host *mmc;
	int ret, count;

	WARN_ON(host == NULL);
	if (host == NULL)
		return -EINVAL;

	mmc = host->mmc;

	if (debug_quirks)
		host->quirks = debug_quirks;

	mshci_reset_all(host);

	host->version = mshci_readl(host, MSHCI_VERID);

	/* there are no reasons not to use DMA */
	host->flags |= MSHCI_USE_IDMA;

	if (host->flags & MSHCI_USE_IDMA) {
		/* We need to allocate descriptors for all sg entries
		 * MSHCI_MAX_DMA_LIST transfer for each of those entries. */
		host->idma_desc = kmalloc(MSHCI_MAX_DMA_LIST * \
					sizeof(struct mshci_idmac), GFP_KERNEL);
		if (!host->idma_desc) {
			kfree(host->idma_desc);
			printk(KERN_WARNING "%s: Unable to allocate IDMA "
				"buffers. Falling back to standard DMA.\n",
				mmc_hostname(mmc));
			host->flags &= ~MSHCI_USE_IDMA;
		}
	}

	/*
	 * If we use DMA, then it's up to the caller to set the DMA
	 * mask, but PIO does not need the hw shim so we set a new
	 * mask here in that case.
	 */
	if (!(host->flags & (MSHCI_USE_IDMA))) {
		host->dma_mask = DMA_BIT_MASK(64);
		mmc_dev(host->mmc)->dma_mask = &host->dma_mask;
	}

	printk(KERN_ERR "%s: Version ID 0x%x.\n",
		mmc_hostname(host->mmc), host->version);

	host->max_clk = 0;

	if (host->max_clk == 0) {
		if (!host->ops->get_max_clock) {
			printk(KERN_ERR
			       "%s: Hardware doesn't specify base clock "
			       "frequency.\n", mmc_hostname(mmc));
			return -ENODEV;
		}
		host->max_clk = host->ops->get_max_clock(host);
	}

	/*
	 * Set host parameters.
	 */
	if (host->ops->get_ro)
		mshci_ops.get_ro = host->ops->get_ro;

	mmc->ops = &mshci_ops;
	mmc->f_min = 400000;
	mmc->f_max = host->max_clk;
	mmc->caps |= MMC_CAP_SDIO_IRQ;

	mmc->caps |= MMC_CAP_4_BIT_DATA;

	mmc->ocr_avail = 0;
	mmc->ocr_avail |= MMC_VDD_32_33|MMC_VDD_33_34;
	mmc->ocr_avail |= MMC_VDD_29_30|MMC_VDD_30_31;


	if (mmc->ocr_avail == 0) {
		printk(KERN_ERR "%s: Hardware doesn't report any "
			"support voltages.\n", mmc_hostname(mmc));
		return -ENODEV;
	}

	spin_lock_init(&host->lock);

	/*
	 * Maximum number of segments. Depends on if the hardware
	 * can do scatter/gather or not.
	 */
	if (host->flags & MSHCI_USE_IDMA)
		mmc->max_segs = MSHCI_MAX_DMA_LIST;
	else /* PIO */
		mmc->max_segs = MSHCI_MAX_DMA_LIST;

	mmc->max_segs = MSHCI_MAX_DMA_LIST;

	/*
	 * Maximum number of sectors in one transfer. Limited by DMA boundary
	 * size (4KiB).
	 * Limited by CPU I/O boundry size (0xfffff000 KiB)
	 */

	/* to prevent starvation of a process that want to access SD device
	 * it should limit size that transfer at one time. */
	mmc->max_req_size = MSHCI_MAX_DMA_TRANS_SIZE;

	/*
	 * Maximum segment size. Could be one segment with the maximum number
	 * of bytes. When doing hardware scatter/gather, each entry cannot
	 * be larger than 4 KiB though.
	 */
	if (host->flags & MSHCI_USE_IDMA)
		mmc->max_seg_size = 0x1000;
	else
		mmc->max_seg_size = mmc->max_req_size;

		/* from SD spec 2.0 and MMC spec 4.2, block size has been
	 * fixed to 512 byte */
	mmc->max_blk_size = 0;

	mmc->max_blk_size = 512 << mmc->max_blk_size;

	/*
	 * Maximum block count.
	 */
	mmc->max_blk_count = MSHCI_MAX_DMA_TRANS_SIZE / mmc->max_blk_size ;

	/*
	 * Init tasklets.
	 */
	tasklet_init(&host->card_tasklet,
		mshci_tasklet_card, (unsigned long)host);
	tasklet_init(&host->finish_tasklet,
		mshci_tasklet_finish, (unsigned long)host);

	setup_timer(&host->timer, mshci_timeout_timer, (unsigned long)host);

	ret = request_irq(host->irq, mshci_irq, IRQF_SHARED,
		mmc_hostname(mmc), host);
	if (ret)
		goto untasklet;

	mshci_init(host);

	mshci_writel(host, (mshci_readl(host, MSHCI_CTRL) | INT_ENABLE),
					MSHCI_CTRL);

	mshci_fifo_init(host);

	/* set debounce filter value*/
	mshci_writel(host, 0xfffff, MSHCI_DEBNCE);

	/* clear card type. set 1bit mode */
	mshci_writel(host, 0x0, MSHCI_CTYPE);

	/* set bus mode register for IDMAC */
	if (host->flags & MSHCI_USE_IDMA) {
		mshci_writel(host, BMOD_IDMAC_RESET, MSHCI_BMOD);
		count = 100;
		while ((mshci_readl(host, MSHCI_BMOD) & BMOD_IDMAC_RESET)
			&& --count)
			; /* nothing to do */

		mshci_writel(host, (mshci_readl(host, MSHCI_BMOD) |
				(BMOD_IDMAC_ENABLE|BMOD_IDMAC_FB)), MSHCI_BMOD);
	}
#ifdef CONFIG_MMC_DEBUG
	mshci_dumpregs(host);
#endif

	mmiowb();

	mmc_add_host(mmc);

	printk(KERN_INFO "%s: MSHCI controller on %s [%s] using %s\n",
		mmc_hostname(mmc), host->hw_name, dev_name(mmc_dev(mmc)),
		(host->flags & MSHCI_USE_IDMA) ? "IDMA" : "PIO");

	mshci_enable_card_detection(host);

	return 0;

untasklet:
	tasklet_kill(&host->card_tasklet);
	tasklet_kill(&host->finish_tasklet);

	return ret;
}
EXPORT_SYMBOL_GPL(mshci_add_host);

void mshci_remove_host(struct mshci_host *host, int dead)
{
	if (dead) {
		spin_lock_irqsave(&host->lock, host->sl_flags);

		host->flags |= MSHCI_DEVICE_DEAD;

		if (host->mrq) {
			printk(KERN_ERR "%s: Controller removed during "
				" transfer!\n", mmc_hostname(host->mmc));

			host->mrq->cmd->error = -ENOMEDIUM;
			tasklet_schedule(&host->finish_tasklet);
		}

		spin_unlock_irqrestore(&host->lock, host->sl_flags);
	}

	mshci_disable_card_detection(host);

	mmc_remove_host(host->mmc);

	if (!dead)
		mshci_reset_all(host);

	free_irq(host->irq, host);

	del_timer_sync(&host->timer);

	tasklet_kill(&host->card_tasklet);
	tasklet_kill(&host->finish_tasklet);

	kfree(host->idma_desc);

	host->idma_desc = NULL;
	host->align_buffer = NULL;
}
EXPORT_SYMBOL_GPL(mshci_remove_host);

void mshci_free_host(struct mshci_host *host)
{
	mmc_free_host(host->mmc);
}
EXPORT_SYMBOL_GPL(mshci_free_host);

/*****************************************************************************\
 *                                                                           *
 * Driver init/exit                                                          *
 *                                                                           *
\*****************************************************************************/

static int __init mshci_drv_init(void)
{
	int ret = 0;
	printk(KERN_INFO DRIVER_NAME
		": Mobile Storage Host Controller Interface driver\n");
	printk(KERN_INFO DRIVER_NAME
		": Copyright (c) 2011 Samsung Electronics Co., Ltd\n");

	return ret;
}

static void __exit mshci_drv_exit(void)
{
}

module_init(mshci_drv_init);
module_exit(mshci_drv_exit);

module_param(debug_quirks, uint, 0444);

MODULE_AUTHOR("Hyunsung Jang, <hs79.jang@samsung.com>");
MODULE_DESCRIPTION("Mobile Storage Host Controller Interface core driver");
MODULE_LICENSE("GPL");

MODULE_PARM_DESC(debug_quirks, "Force certain quirks.");
