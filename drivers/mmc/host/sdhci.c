/*
 *  linux/drivers/mmc/host/sdhci.c - Secure Digital Host Controller Interface driver
 *
 *  Copyright (C) 2005-2008 Pierre Ossman, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * Thanks to the following companies for their support:
 *
 *     - JMicron (hardware and technical support)
 */

#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>

#include <linux/leds.h>

#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>

#include "sdhci.h"

#define DRIVER_NAME "sdhci"

#define DBG(f, x...) \
	pr_debug("%s: " DRIVER_NAME ": " f, mmc_hostname(host->mmc), ## x)

#define SDHCI_DUMP(f, x...) \
	pr_err("%s: " DRIVER_NAME ": " f, mmc_hostname(host->mmc), ## x)

#define MAX_TUNING_LOOP 40

static unsigned int debug_quirks = 0;
static unsigned int debug_quirks2;

static void sdhci_finish_data(struct sdhci_host *);

static void sdhci_enable_preset_value(struct sdhci_host *host, bool enable);

void sdhci_dumpregs(struct sdhci_host *host)
{
	SDHCI_DUMP("============ SDHCI REGISTER DUMP ===========\n");

	SDHCI_DUMP("Sys addr:  0x%08x | Version:  0x%08x\n",
		   sdhci_readl(host, SDHCI_DMA_ADDRESS),
		   sdhci_readw(host, SDHCI_HOST_VERSION));
	SDHCI_DUMP("Blk size:  0x%08x | Blk cnt:  0x%08x\n",
		   sdhci_readw(host, SDHCI_BLOCK_SIZE),
		   sdhci_readw(host, SDHCI_BLOCK_COUNT));
	SDHCI_DUMP("Argument:  0x%08x | Trn mode: 0x%08x\n",
		   sdhci_readl(host, SDHCI_ARGUMENT),
		   sdhci_readw(host, SDHCI_TRANSFER_MODE));
	SDHCI_DUMP("Present:   0x%08x | Host ctl: 0x%08x\n",
		   sdhci_readl(host, SDHCI_PRESENT_STATE),
		   sdhci_readb(host, SDHCI_HOST_CONTROL));
	SDHCI_DUMP("Power:     0x%08x | Blk gap:  0x%08x\n",
		   sdhci_readb(host, SDHCI_POWER_CONTROL),
		   sdhci_readb(host, SDHCI_BLOCK_GAP_CONTROL));
	SDHCI_DUMP("Wake-up:   0x%08x | Clock:    0x%08x\n",
		   sdhci_readb(host, SDHCI_WAKE_UP_CONTROL),
		   sdhci_readw(host, SDHCI_CLOCK_CONTROL));
	SDHCI_DUMP("Timeout:   0x%08x | Int stat: 0x%08x\n",
		   sdhci_readb(host, SDHCI_TIMEOUT_CONTROL),
		   sdhci_readl(host, SDHCI_INT_STATUS));
	SDHCI_DUMP("Int enab:  0x%08x | Sig enab: 0x%08x\n",
		   sdhci_readl(host, SDHCI_INT_ENABLE),
		   sdhci_readl(host, SDHCI_SIGNAL_ENABLE));
	SDHCI_DUMP("AC12 err:  0x%08x | Slot int: 0x%08x\n",
		   sdhci_readw(host, SDHCI_ACMD12_ERR),
		   sdhci_readw(host, SDHCI_SLOT_INT_STATUS));
	SDHCI_DUMP("Caps:      0x%08x | Caps_1:   0x%08x\n",
		   sdhci_readl(host, SDHCI_CAPABILITIES),
		   sdhci_readl(host, SDHCI_CAPABILITIES_1));
	SDHCI_DUMP("Cmd:       0x%08x | Max curr: 0x%08x\n",
		   sdhci_readw(host, SDHCI_COMMAND),
		   sdhci_readl(host, SDHCI_MAX_CURRENT));
	SDHCI_DUMP("Resp[0]:   0x%08x | Resp[1]:  0x%08x\n",
		   sdhci_readl(host, SDHCI_RESPONSE),
		   sdhci_readl(host, SDHCI_RESPONSE + 4));
	SDHCI_DUMP("Resp[2]:   0x%08x | Resp[3]:  0x%08x\n",
		   sdhci_readl(host, SDHCI_RESPONSE + 8),
		   sdhci_readl(host, SDHCI_RESPONSE + 12));
	SDHCI_DUMP("Host ctl2: 0x%08x\n",
		   sdhci_readw(host, SDHCI_HOST_CONTROL2));

	if (host->flags & SDHCI_USE_ADMA) {
		if (host->flags & SDHCI_USE_64_BIT_DMA) {
			SDHCI_DUMP("ADMA Err:  0x%08x | ADMA Ptr: 0x%08x%08x\n",
				   sdhci_readl(host, SDHCI_ADMA_ERROR),
				   sdhci_readl(host, SDHCI_ADMA_ADDRESS_HI),
				   sdhci_readl(host, SDHCI_ADMA_ADDRESS));
		} else {
			SDHCI_DUMP("ADMA Err:  0x%08x | ADMA Ptr: 0x%08x\n",
				   sdhci_readl(host, SDHCI_ADMA_ERROR),
				   sdhci_readl(host, SDHCI_ADMA_ADDRESS));
		}
	}

	SDHCI_DUMP("============================================\n");
}
EXPORT_SYMBOL_GPL(sdhci_dumpregs);

/*****************************************************************************\
 *                                                                           *
 * Low level functions                                                       *
 *                                                                           *
\*****************************************************************************/

static inline bool sdhci_data_line_cmd(struct mmc_command *cmd)
{
	return cmd->data || cmd->flags & MMC_RSP_BUSY;
}

static void sdhci_set_card_detection(struct sdhci_host *host, bool enable)
{
	u32 present;

	if ((host->quirks & SDHCI_QUIRK_BROKEN_CARD_DETECTION) ||
	    !mmc_card_is_removable(host->mmc))
		return;

	if (enable) {
		present = sdhci_readl(host, SDHCI_PRESENT_STATE) &
				      SDHCI_CARD_PRESENT;

		host->ier |= present ? SDHCI_INT_CARD_REMOVE :
				       SDHCI_INT_CARD_INSERT;
	} else {
		host->ier &= ~(SDHCI_INT_CARD_REMOVE | SDHCI_INT_CARD_INSERT);
	}

	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
}

static void sdhci_enable_card_detection(struct sdhci_host *host)
{
	sdhci_set_card_detection(host, true);
}

static void sdhci_disable_card_detection(struct sdhci_host *host)
{
	sdhci_set_card_detection(host, false);
}

static void sdhci_runtime_pm_bus_on(struct sdhci_host *host)
{
	if (host->bus_on)
		return;
	host->bus_on = true;
	pm_runtime_get_noresume(host->mmc->parent);
}

static void sdhci_runtime_pm_bus_off(struct sdhci_host *host)
{
	if (!host->bus_on)
		return;
	host->bus_on = false;
	pm_runtime_put_noidle(host->mmc->parent);
}

void sdhci_reset(struct sdhci_host *host, u8 mask)
{
	ktime_t timeout;

	sdhci_writeb(host, mask, SDHCI_SOFTWARE_RESET);

	if (mask & SDHCI_RESET_ALL) {
		host->clock = 0;
		/* Reset-all turns off SD Bus Power */
		if (host->quirks2 & SDHCI_QUIRK2_CARD_ON_NEEDS_BUS_ON)
			sdhci_runtime_pm_bus_off(host);
	}

	/* Wait max 100 ms */
	timeout = ktime_add_ms(ktime_get(), 100);

	/* hw clears the bit when it's done */
	while (sdhci_readb(host, SDHCI_SOFTWARE_RESET) & mask) {
		if (ktime_after(ktime_get(), timeout)) {
			pr_err("%s: Reset 0x%x never completed.\n",
				mmc_hostname(host->mmc), (int)mask);
			sdhci_dumpregs(host);
			return;
		}
		udelay(10);
	}
}
EXPORT_SYMBOL_GPL(sdhci_reset);

static void sdhci_do_reset(struct sdhci_host *host, u8 mask)
{
	if (host->quirks & SDHCI_QUIRK_NO_CARD_NO_RESET) {
		struct mmc_host *mmc = host->mmc;

		if (!mmc->ops->get_cd(mmc))
			return;
	}

	host->ops->reset(host, mask);

	if (mask & SDHCI_RESET_ALL) {
		if (host->flags & (SDHCI_USE_SDMA | SDHCI_USE_ADMA)) {
			if (host->ops->enable_dma)
				host->ops->enable_dma(host);
		}

		/* Resetting the controller clears many */
		host->preset_enabled = false;
	}
}

static void sdhci_set_default_irqs(struct sdhci_host *host)
{
	host->ier = SDHCI_INT_BUS_POWER | SDHCI_INT_DATA_END_BIT |
		    SDHCI_INT_DATA_CRC | SDHCI_INT_DATA_TIMEOUT |
		    SDHCI_INT_INDEX | SDHCI_INT_END_BIT | SDHCI_INT_CRC |
		    SDHCI_INT_TIMEOUT | SDHCI_INT_DATA_END |
		    SDHCI_INT_RESPONSE;

	if (host->tuning_mode == SDHCI_TUNING_MODE_2 ||
	    host->tuning_mode == SDHCI_TUNING_MODE_3)
		host->ier |= SDHCI_INT_RETUNE;

	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
}

static void sdhci_init(struct sdhci_host *host, int soft)
{
	struct mmc_host *mmc = host->mmc;

	if (soft)
		sdhci_do_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
	else
		sdhci_do_reset(host, SDHCI_RESET_ALL);

	sdhci_set_default_irqs(host);

	host->cqe_on = false;

	if (soft) {
		/* force clock reconfiguration */
		host->clock = 0;
		mmc->ops->set_ios(mmc, &mmc->ios);
	}
}

static void sdhci_reinit(struct sdhci_host *host)
{
	sdhci_init(host, 0);
	sdhci_enable_card_detection(host);
}

static void __sdhci_led_activate(struct sdhci_host *host)
{
	u8 ctrl;

	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
	ctrl |= SDHCI_CTRL_LED;
	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
}

static void __sdhci_led_deactivate(struct sdhci_host *host)
{
	u8 ctrl;

	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
	ctrl &= ~SDHCI_CTRL_LED;
	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
}

#if IS_REACHABLE(CONFIG_LEDS_CLASS)
static void sdhci_led_control(struct led_classdev *led,
			      enum led_brightness brightness)
{
	struct sdhci_host *host = container_of(led, struct sdhci_host, led);
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	if (host->runtime_suspended)
		goto out;

	if (brightness == LED_OFF)
		__sdhci_led_deactivate(host);
	else
		__sdhci_led_activate(host);
out:
	spin_unlock_irqrestore(&host->lock, flags);
}

static int sdhci_led_register(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;

	snprintf(host->led_name, sizeof(host->led_name),
		 "%s::", mmc_hostname(mmc));

	host->led.name = host->led_name;
	host->led.brightness = LED_OFF;
	host->led.default_trigger = mmc_hostname(mmc);
	host->led.brightness_set = sdhci_led_control;

	return led_classdev_register(mmc_dev(mmc), &host->led);
}

static void sdhci_led_unregister(struct sdhci_host *host)
{
	led_classdev_unregister(&host->led);
}

static inline void sdhci_led_activate(struct sdhci_host *host)
{
}

static inline void sdhci_led_deactivate(struct sdhci_host *host)
{
}

#else

static inline int sdhci_led_register(struct sdhci_host *host)
{
	return 0;
}

static inline void sdhci_led_unregister(struct sdhci_host *host)
{
}

static inline void sdhci_led_activate(struct sdhci_host *host)
{
	__sdhci_led_activate(host);
}

static inline void sdhci_led_deactivate(struct sdhci_host *host)
{
	__sdhci_led_deactivate(host);
}

#endif

/*****************************************************************************\
 *                                                                           *
 * Core functions                                                            *
 *                                                                           *
\*****************************************************************************/

static void sdhci_read_block_pio(struct sdhci_host *host)
{
	unsigned long flags;
	size_t blksize, len, chunk;
	u32 uninitialized_var(scratch);
	u8 *buf;

	DBG("PIO reading\n");

	blksize = host->data->blksz;
	chunk = 0;

	local_irq_save(flags);

	while (blksize) {
		BUG_ON(!sg_miter_next(&host->sg_miter));

		len = min(host->sg_miter.length, blksize);

		blksize -= len;
		host->sg_miter.consumed = len;

		buf = host->sg_miter.addr;

		while (len) {
			if (chunk == 0) {
				scratch = sdhci_readl(host, SDHCI_BUFFER);
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

static void sdhci_write_block_pio(struct sdhci_host *host)
{
	unsigned long flags;
	size_t blksize, len, chunk;
	u32 scratch;
	u8 *buf;

	DBG("PIO writing\n");

	blksize = host->data->blksz;
	chunk = 0;
	scratch = 0;

	local_irq_save(flags);

	while (blksize) {
		BUG_ON(!sg_miter_next(&host->sg_miter));

		len = min(host->sg_miter.length, blksize);

		blksize -= len;
		host->sg_miter.consumed = len;

		buf = host->sg_miter.addr;

		while (len) {
			scratch |= (u32)*buf << (chunk * 8);

			buf++;
			chunk++;
			len--;

			if ((chunk == 4) || ((len == 0) && (blksize == 0))) {
				sdhci_writel(host, scratch, SDHCI_BUFFER);
				chunk = 0;
				scratch = 0;
			}
		}
	}

	sg_miter_stop(&host->sg_miter);

	local_irq_restore(flags);
}

static void sdhci_transfer_pio(struct sdhci_host *host)
{
	u32 mask;

	if (host->blocks == 0)
		return;

	if (host->data->flags & MMC_DATA_READ)
		mask = SDHCI_DATA_AVAILABLE;
	else
		mask = SDHCI_SPACE_AVAILABLE;

	/*
	 * Some controllers (JMicron JMB38x) mess up the buffer bits
	 * for transfers < 4 bytes. As long as it is just one block,
	 * we can ignore the bits.
	 */
	if ((host->quirks & SDHCI_QUIRK_BROKEN_SMALL_PIO) &&
		(host->data->blocks == 1))
		mask = ~0;

	while (sdhci_readl(host, SDHCI_PRESENT_STATE) & mask) {
		if (host->quirks & SDHCI_QUIRK_PIO_NEEDS_DELAY)
			udelay(100);

		if (host->data->flags & MMC_DATA_READ)
			sdhci_read_block_pio(host);
		else
			sdhci_write_block_pio(host);

		host->blocks--;
		if (host->blocks == 0)
			break;
	}

	DBG("PIO transfer complete.\n");
}

static int sdhci_pre_dma_transfer(struct sdhci_host *host,
				  struct mmc_data *data, int cookie)
{
	int sg_count;

	/*
	 * If the data buffers are already mapped, return the previous
	 * dma_map_sg() result.
	 */
	if (data->host_cookie == COOKIE_PRE_MAPPED)
		return data->sg_count;

	sg_count = dma_map_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
			      mmc_get_dma_dir(data));

	if (sg_count == 0)
		return -ENOSPC;

	data->sg_count = sg_count;
	data->host_cookie = cookie;

	return sg_count;
}

static char *sdhci_kmap_atomic(struct scatterlist *sg, unsigned long *flags)
{
	local_irq_save(*flags);
	return kmap_atomic(sg_page(sg)) + sg->offset;
}

static void sdhci_kunmap_atomic(void *buffer, unsigned long *flags)
{
	kunmap_atomic(buffer);
	local_irq_restore(*flags);
}

static void sdhci_adma_write_desc(struct sdhci_host *host, void *desc,
				  dma_addr_t addr, int len, unsigned cmd)
{
	struct sdhci_adma2_64_desc *dma_desc = desc;

	/* 32-bit and 64-bit descriptors have these members in same position */
	dma_desc->cmd = cpu_to_le16(cmd);
	dma_desc->len = cpu_to_le16(len);
	dma_desc->addr_lo = cpu_to_le32((u32)addr);

	if (host->flags & SDHCI_USE_64_BIT_DMA)
		dma_desc->addr_hi = cpu_to_le32((u64)addr >> 32);
}

static void sdhci_adma_mark_end(void *desc)
{
	struct sdhci_adma2_64_desc *dma_desc = desc;

	/* 32-bit and 64-bit descriptors have 'cmd' in same position */
	dma_desc->cmd |= cpu_to_le16(ADMA2_END);
}

static void sdhci_adma_table_pre(struct sdhci_host *host,
	struct mmc_data *data, int sg_count)
{
	struct scatterlist *sg;
	unsigned long flags;
	dma_addr_t addr, align_addr;
	void *desc, *align;
	char *buffer;
	int len, offset, i;

	/*
	 * The spec does not specify endianness of descriptor table.
	 * We currently guess that it is LE.
	 */

	host->sg_count = sg_count;

	desc = host->adma_table;
	align = host->align_buffer;

	align_addr = host->align_addr;

	for_each_sg(data->sg, sg, host->sg_count, i) {
		addr = sg_dma_address(sg);
		len = sg_dma_len(sg);

		/*
		 * The SDHCI specification states that ADMA addresses must
		 * be 32-bit aligned. If they aren't, then we use a bounce
		 * buffer for the (up to three) bytes that screw up the
		 * alignment.
		 */
		offset = (SDHCI_ADMA2_ALIGN - (addr & SDHCI_ADMA2_MASK)) &
			 SDHCI_ADMA2_MASK;
		if (offset) {
			if (data->flags & MMC_DATA_WRITE) {
				buffer = sdhci_kmap_atomic(sg, &flags);
				memcpy(align, buffer, offset);
				sdhci_kunmap_atomic(buffer, &flags);
			}

			/* tran, valid */
			sdhci_adma_write_desc(host, desc, align_addr, offset,
					      ADMA2_TRAN_VALID);

			BUG_ON(offset > 65536);

			align += SDHCI_ADMA2_ALIGN;
			align_addr += SDHCI_ADMA2_ALIGN;

			desc += host->desc_sz;

			addr += offset;
			len -= offset;
		}

		BUG_ON(len > 65536);

		if (len) {
			/* tran, valid */
			sdhci_adma_write_desc(host, desc, addr, len,
					      ADMA2_TRAN_VALID);
			desc += host->desc_sz;
		}

		/*
		 * If this triggers then we have a calculation bug
		 * somewhere. :/
		 */
		WARN_ON((desc - host->adma_table) >= host->adma_table_sz);
	}

	if (host->quirks & SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC) {
		/* Mark the last descriptor as the terminating descriptor */
		if (desc != host->adma_table) {
			desc -= host->desc_sz;
			sdhci_adma_mark_end(desc);
		}
	} else {
		/* Add a terminating entry - nop, end, valid */
		sdhci_adma_write_desc(host, desc, 0, 0, ADMA2_NOP_END_VALID);
	}
}

static void sdhci_adma_table_post(struct sdhci_host *host,
	struct mmc_data *data)
{
	struct scatterlist *sg;
	int i, size;
	void *align;
	char *buffer;
	unsigned long flags;

	if (data->flags & MMC_DATA_READ) {
		bool has_unaligned = false;

		/* Do a quick scan of the SG list for any unaligned mappings */
		for_each_sg(data->sg, sg, host->sg_count, i)
			if (sg_dma_address(sg) & SDHCI_ADMA2_MASK) {
				has_unaligned = true;
				break;
			}

		if (has_unaligned) {
			dma_sync_sg_for_cpu(mmc_dev(host->mmc), data->sg,
					    data->sg_len, DMA_FROM_DEVICE);

			align = host->align_buffer;

			for_each_sg(data->sg, sg, host->sg_count, i) {
				if (sg_dma_address(sg) & SDHCI_ADMA2_MASK) {
					size = SDHCI_ADMA2_ALIGN -
					       (sg_dma_address(sg) & SDHCI_ADMA2_MASK);

					buffer = sdhci_kmap_atomic(sg, &flags);
					memcpy(buffer, align, size);
					sdhci_kunmap_atomic(buffer, &flags);

					align += SDHCI_ADMA2_ALIGN;
				}
			}
		}
	}
}

static u8 sdhci_calc_timeout(struct sdhci_host *host, struct mmc_command *cmd)
{
	u8 count;
	struct mmc_data *data = cmd->data;
	unsigned target_timeout, current_timeout;

	/*
	 * If the host controller provides us with an incorrect timeout
	 * value, just skip the check and use 0xE.  The hardware may take
	 * longer to time out, but that's much better than having a too-short
	 * timeout value.
	 */
	if (host->quirks & SDHCI_QUIRK_BROKEN_TIMEOUT_VAL)
		return 0xE;

	/* Unspecified timeout, assume max */
	if (!data && !cmd->busy_timeout)
		return 0xE;

	/* timeout in us */
	if (!data)
		target_timeout = cmd->busy_timeout * 1000;
	else {
		target_timeout = DIV_ROUND_UP(data->timeout_ns, 1000);
		if (host->clock && data->timeout_clks) {
			unsigned long long val;

			/*
			 * data->timeout_clks is in units of clock cycles.
			 * host->clock is in Hz.  target_timeout is in us.
			 * Hence, us = 1000000 * cycles / Hz.  Round up.
			 */
			val = 1000000ULL * data->timeout_clks;
			if (do_div(val, host->clock))
				target_timeout++;
			target_timeout += val;
		}
	}

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
		DBG("Too large timeout 0x%x requested for CMD%d!\n",
		    count, cmd->opcode);
		count = 0xE;
	}

	return count;
}

static void sdhci_set_transfer_irqs(struct sdhci_host *host)
{
	u32 pio_irqs = SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL;
	u32 dma_irqs = SDHCI_INT_DMA_END | SDHCI_INT_ADMA_ERROR;

	if (host->flags & SDHCI_REQ_USE_DMA)
		host->ier = (host->ier & ~pio_irqs) | dma_irqs;
	else
		host->ier = (host->ier & ~dma_irqs) | pio_irqs;

	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
}

static void sdhci_set_timeout(struct sdhci_host *host, struct mmc_command *cmd)
{
	u8 count;

	if (host->ops->set_timeout) {
		host->ops->set_timeout(host, cmd);
	} else {
		count = sdhci_calc_timeout(host, cmd);
		sdhci_writeb(host, count, SDHCI_TIMEOUT_CONTROL);
	}
}

static void sdhci_prepare_data(struct sdhci_host *host, struct mmc_command *cmd)
{
	u8 ctrl;
	struct mmc_data *data = cmd->data;

	if (sdhci_data_line_cmd(cmd))
		sdhci_set_timeout(host, cmd);

	if (!data)
		return;

	WARN_ON(host->data);

	/* Sanity checks */
	BUG_ON(data->blksz * data->blocks > 524288);
	BUG_ON(data->blksz > host->mmc->max_blk_size);
	BUG_ON(data->blocks > 65535);

	host->data = data;
	host->data_early = 0;
	host->data->bytes_xfered = 0;

	if (host->flags & (SDHCI_USE_SDMA | SDHCI_USE_ADMA)) {
		struct scatterlist *sg;
		unsigned int length_mask, offset_mask;
		int i;

		host->flags |= SDHCI_REQ_USE_DMA;

		/*
		 * FIXME: This doesn't account for merging when mapping the
		 * scatterlist.
		 *
		 * The assumption here being that alignment and lengths are
		 * the same after DMA mapping to device address space.
		 */
		length_mask = 0;
		offset_mask = 0;
		if (host->flags & SDHCI_USE_ADMA) {
			if (host->quirks & SDHCI_QUIRK_32BIT_ADMA_SIZE) {
				length_mask = 3;
				/*
				 * As we use up to 3 byte chunks to work
				 * around alignment problems, we need to
				 * check the offset as well.
				 */
				offset_mask = 3;
			}
		} else {
			if (host->quirks & SDHCI_QUIRK_32BIT_DMA_SIZE)
				length_mask = 3;
			if (host->quirks & SDHCI_QUIRK_32BIT_DMA_ADDR)
				offset_mask = 3;
		}

		if (unlikely(length_mask | offset_mask)) {
			for_each_sg(data->sg, sg, data->sg_len, i) {
				if (sg->length & length_mask) {
					DBG("Reverting to PIO because of transfer size (%d)\n",
					    sg->length);
					host->flags &= ~SDHCI_REQ_USE_DMA;
					break;
				}
				if (sg->offset & offset_mask) {
					DBG("Reverting to PIO because of bad alignment\n");
					host->flags &= ~SDHCI_REQ_USE_DMA;
					break;
				}
			}
		}
	}

	if (host->flags & SDHCI_REQ_USE_DMA) {
		int sg_cnt = sdhci_pre_dma_transfer(host, data, COOKIE_MAPPED);

		if (sg_cnt <= 0) {
			/*
			 * This only happens when someone fed
			 * us an invalid request.
			 */
			WARN_ON(1);
			host->flags &= ~SDHCI_REQ_USE_DMA;
		} else if (host->flags & SDHCI_USE_ADMA) {
			sdhci_adma_table_pre(host, data, sg_cnt);

			sdhci_writel(host, host->adma_addr, SDHCI_ADMA_ADDRESS);
			if (host->flags & SDHCI_USE_64_BIT_DMA)
				sdhci_writel(host,
					     (u64)host->adma_addr >> 32,
					     SDHCI_ADMA_ADDRESS_HI);
		} else {
			WARN_ON(sg_cnt != 1);
			sdhci_writel(host, sg_dma_address(data->sg),
				SDHCI_DMA_ADDRESS);
		}
	}

	/*
	 * Always adjust the DMA selection as some controllers
	 * (e.g. JMicron) can't do PIO properly when the selection
	 * is ADMA.
	 */
	if (host->version >= SDHCI_SPEC_200) {
		ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
		ctrl &= ~SDHCI_CTRL_DMA_MASK;
		if ((host->flags & SDHCI_REQ_USE_DMA) &&
			(host->flags & SDHCI_USE_ADMA)) {
			if (host->flags & SDHCI_USE_64_BIT_DMA)
				ctrl |= SDHCI_CTRL_ADMA64;
			else
				ctrl |= SDHCI_CTRL_ADMA32;
		} else {
			ctrl |= SDHCI_CTRL_SDMA;
		}
		sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
	}

	if (!(host->flags & SDHCI_REQ_USE_DMA)) {
		int flags;

		flags = SG_MITER_ATOMIC;
		if (host->data->flags & MMC_DATA_READ)
			flags |= SG_MITER_TO_SG;
		else
			flags |= SG_MITER_FROM_SG;
		sg_miter_start(&host->sg_miter, data->sg, data->sg_len, flags);
		host->blocks = data->blocks;
	}

	sdhci_set_transfer_irqs(host);

	/* Set the DMA boundary value and block size */
	sdhci_writew(host, SDHCI_MAKE_BLKSZ(host->sdma_boundary, data->blksz),
		     SDHCI_BLOCK_SIZE);
	sdhci_writew(host, data->blocks, SDHCI_BLOCK_COUNT);
}

static inline bool sdhci_auto_cmd12(struct sdhci_host *host,
				    struct mmc_request *mrq)
{
	return !mrq->sbc && (host->flags & SDHCI_AUTO_CMD12) &&
	       !mrq->cap_cmd_during_tfr;
}

static void sdhci_set_transfer_mode(struct sdhci_host *host,
	struct mmc_command *cmd)
{
	u16 mode = 0;
	struct mmc_data *data = cmd->data;

	if (data == NULL) {
		if (host->quirks2 &
			SDHCI_QUIRK2_CLEAR_TRANSFERMODE_REG_BEFORE_CMD) {
			sdhci_writew(host, 0x0, SDHCI_TRANSFER_MODE);
		} else {
		/* clear Auto CMD settings for no data CMDs */
			mode = sdhci_readw(host, SDHCI_TRANSFER_MODE);
			sdhci_writew(host, mode & ~(SDHCI_TRNS_AUTO_CMD12 |
				SDHCI_TRNS_AUTO_CMD23), SDHCI_TRANSFER_MODE);
		}
		return;
	}

	WARN_ON(!host->data);

	if (!(host->quirks2 & SDHCI_QUIRK2_SUPPORT_SINGLE))
		mode = SDHCI_TRNS_BLK_CNT_EN;

	if (mmc_op_multi(cmd->opcode) || data->blocks > 1) {
		mode = SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_MULTI;
		/*
		 * If we are sending CMD23, CMD12 never gets sent
		 * on successful completion (so no Auto-CMD12).
		 */
		if (sdhci_auto_cmd12(host, cmd->mrq) &&
		    (cmd->opcode != SD_IO_RW_EXTENDED))
			mode |= SDHCI_TRNS_AUTO_CMD12;
		else if (cmd->mrq->sbc && (host->flags & SDHCI_AUTO_CMD23)) {
			mode |= SDHCI_TRNS_AUTO_CMD23;
			sdhci_writel(host, cmd->mrq->sbc->arg, SDHCI_ARGUMENT2);
		}
	}

	if (data->flags & MMC_DATA_READ)
		mode |= SDHCI_TRNS_READ;
	if (host->flags & SDHCI_REQ_USE_DMA)
		mode |= SDHCI_TRNS_DMA;

	sdhci_writew(host, mode, SDHCI_TRANSFER_MODE);
}

static bool sdhci_needs_reset(struct sdhci_host *host, struct mmc_request *mrq)
{
	return (!(host->flags & SDHCI_DEVICE_DEAD) &&
		((mrq->cmd && mrq->cmd->error) ||
		 (mrq->sbc && mrq->sbc->error) ||
		 (mrq->data && ((mrq->data->error && !mrq->data->stop) ||
				(mrq->data->stop && mrq->data->stop->error))) ||
		 (host->quirks & SDHCI_QUIRK_RESET_AFTER_REQUEST)));
}

static void __sdhci_finish_mrq(struct sdhci_host *host, struct mmc_request *mrq)
{
	int i;

	for (i = 0; i < SDHCI_MAX_MRQS; i++) {
		if (host->mrqs_done[i] == mrq) {
			WARN_ON(1);
			return;
		}
	}

	for (i = 0; i < SDHCI_MAX_MRQS; i++) {
		if (!host->mrqs_done[i]) {
			host->mrqs_done[i] = mrq;
			break;
		}
	}

	WARN_ON(i >= SDHCI_MAX_MRQS);

	tasklet_schedule(&host->finish_tasklet);
}

static void sdhci_finish_mrq(struct sdhci_host *host, struct mmc_request *mrq)
{
	if (host->cmd && host->cmd->mrq == mrq)
		host->cmd = NULL;

	if (host->data_cmd && host->data_cmd->mrq == mrq)
		host->data_cmd = NULL;

	if (host->data && host->data->mrq == mrq)
		host->data = NULL;

	if (sdhci_needs_reset(host, mrq))
		host->pending_reset = true;

	__sdhci_finish_mrq(host, mrq);
}

static void sdhci_finish_data(struct sdhci_host *host)
{
	struct mmc_command *data_cmd = host->data_cmd;
	struct mmc_data *data = host->data;

	host->data = NULL;
	host->data_cmd = NULL;

	if ((host->flags & (SDHCI_REQ_USE_DMA | SDHCI_USE_ADMA)) ==
	    (SDHCI_REQ_USE_DMA | SDHCI_USE_ADMA))
		sdhci_adma_table_post(host, data);

	/*
	 * The specification states that the block count register must
	 * be updated, but it does not specify at what point in the
	 * data flow. That makes the register entirely useless to read
	 * back so we have to assume that nothing made it to the card
	 * in the event of an error.
	 */
	if (data->error)
		data->bytes_xfered = 0;
	else
		data->bytes_xfered = data->blksz * data->blocks;

	/*
	 * Need to send CMD12 if -
	 * a) open-ended multiblock transfer (no CMD23)
	 * b) error in multiblock transfer
	 */
	if (data->stop &&
	    (data->error ||
	     !data->mrq->sbc)) {

		/*
		 * The controller needs a reset of internal state machines
		 * upon error conditions.
		 */
		if (data->error) {
			if (!host->cmd || host->cmd == data_cmd)
				sdhci_do_reset(host, SDHCI_RESET_CMD);
			sdhci_do_reset(host, SDHCI_RESET_DATA);
		}

		/*
		 * 'cap_cmd_during_tfr' request must not use the command line
		 * after mmc_command_done() has been called. It is upper layer's
		 * responsibility to send the stop command if required.
		 */
		if (data->mrq->cap_cmd_during_tfr) {
			sdhci_finish_mrq(host, data->mrq);
		} else {
			/* Avoid triggering warning in sdhci_send_command() */
			host->cmd = NULL;
			sdhci_send_command(host, data->stop);
		}
	} else {
		sdhci_finish_mrq(host, data->mrq);
	}
}

static void sdhci_mod_timer(struct sdhci_host *host, struct mmc_request *mrq,
			    unsigned long timeout)
{
	if (sdhci_data_line_cmd(mrq->cmd))
		mod_timer(&host->data_timer, timeout);
	else
		mod_timer(&host->timer, timeout);
}

static void sdhci_del_timer(struct sdhci_host *host, struct mmc_request *mrq)
{
	if (sdhci_data_line_cmd(mrq->cmd))
		del_timer(&host->data_timer);
	else
		del_timer(&host->timer);
}

void sdhci_send_command(struct sdhci_host *host, struct mmc_command *cmd)
{
	int flags;
	u32 mask;
	unsigned long timeout;

	WARN_ON(host->cmd);

	/* Initially, a command has no error */
	cmd->error = 0;

	if ((host->quirks2 & SDHCI_QUIRK2_STOP_WITH_TC) &&
	    cmd->opcode == MMC_STOP_TRANSMISSION)
		cmd->flags |= MMC_RSP_BUSY;

	/* Wait max 10 ms */
	timeout = 10;

	mask = SDHCI_CMD_INHIBIT;
	if (sdhci_data_line_cmd(cmd))
		mask |= SDHCI_DATA_INHIBIT;

	/* We shouldn't wait for data inihibit for stop commands, even
	   though they might use busy signaling */
	if (cmd->mrq->data && (cmd == cmd->mrq->data->stop))
		mask &= ~SDHCI_DATA_INHIBIT;

	while (sdhci_readl(host, SDHCI_PRESENT_STATE) & mask) {
		if (timeout == 0) {
			pr_err("%s: Controller never released inhibit bit(s).\n",
			       mmc_hostname(host->mmc));
			sdhci_dumpregs(host);
			cmd->error = -EIO;
			sdhci_finish_mrq(host, cmd->mrq);
			return;
		}
		timeout--;
		mdelay(1);
	}

	timeout = jiffies;
	if (!cmd->data && cmd->busy_timeout > 9000)
		timeout += DIV_ROUND_UP(cmd->busy_timeout, 1000) * HZ + HZ;
	else
		timeout += 10 * HZ;
	sdhci_mod_timer(host, cmd->mrq, timeout);

	host->cmd = cmd;
	if (sdhci_data_line_cmd(cmd)) {
		WARN_ON(host->data_cmd);
		host->data_cmd = cmd;
	}

	sdhci_prepare_data(host, cmd);

	sdhci_writel(host, cmd->arg, SDHCI_ARGUMENT);

	sdhci_set_transfer_mode(host, cmd);

	if ((cmd->flags & MMC_RSP_136) && (cmd->flags & MMC_RSP_BUSY)) {
		pr_err("%s: Unsupported response type!\n",
			mmc_hostname(host->mmc));
		cmd->error = -EINVAL;
		sdhci_finish_mrq(host, cmd->mrq);
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

	/* CMD19 is special in that the Data Present Select should be set */
	if (cmd->data || cmd->opcode == MMC_SEND_TUNING_BLOCK ||
	    cmd->opcode == MMC_SEND_TUNING_BLOCK_HS200)
		flags |= SDHCI_CMD_DATA;

	sdhci_writew(host, SDHCI_MAKE_CMD(cmd->opcode, flags), SDHCI_COMMAND);
}
EXPORT_SYMBOL_GPL(sdhci_send_command);

static void sdhci_read_rsp_136(struct sdhci_host *host, struct mmc_command *cmd)
{
	int i, reg;

	for (i = 0; i < 4; i++) {
		reg = SDHCI_RESPONSE + (3 - i) * 4;
		cmd->resp[i] = sdhci_readl(host, reg);
	}

	if (host->quirks2 & SDHCI_QUIRK2_RSP_136_HAS_CRC)
		return;

	/* CRC is stripped so we need to do some shifting */
	for (i = 0; i < 4; i++) {
		cmd->resp[i] <<= 8;
		if (i != 3)
			cmd->resp[i] |= cmd->resp[i + 1] >> 24;
	}
}

static void sdhci_finish_command(struct sdhci_host *host)
{
	struct mmc_command *cmd = host->cmd;

	host->cmd = NULL;

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			sdhci_read_rsp_136(host, cmd);
		} else {
			cmd->resp[0] = sdhci_readl(host, SDHCI_RESPONSE);
		}
	}

	if (cmd->mrq->cap_cmd_during_tfr && cmd == cmd->mrq->cmd)
		mmc_command_done(host->mmc, cmd->mrq);

	/*
	 * The host can send and interrupt when the busy state has
	 * ended, allowing us to wait without wasting CPU cycles.
	 * The busy signal uses DAT0 so this is similar to waiting
	 * for data to complete.
	 *
	 * Note: The 1.0 specification is a bit ambiguous about this
	 *       feature so there might be some problems with older
	 *       controllers.
	 */
	if (cmd->flags & MMC_RSP_BUSY) {
		if (cmd->data) {
			DBG("Cannot wait for busy signal when also doing a data transfer");
		} else if (!(host->quirks & SDHCI_QUIRK_NO_BUSY_IRQ) &&
			   cmd == host->data_cmd) {
			/* Command complete before busy is ended */
			return;
		}
	}

	/* Finished CMD23, now send actual command. */
	if (cmd == cmd->mrq->sbc) {
		sdhci_send_command(host, cmd->mrq->cmd);
	} else {

		/* Processed actual command. */
		if (host->data && host->data_early)
			sdhci_finish_data(host);

		if (!cmd->data)
			sdhci_finish_mrq(host, cmd->mrq);
	}
}

static u16 sdhci_get_preset_value(struct sdhci_host *host)
{
	u16 preset = 0;

	switch (host->timing) {
	case MMC_TIMING_UHS_SDR12:
		preset = sdhci_readw(host, SDHCI_PRESET_FOR_SDR12);
		break;
	case MMC_TIMING_UHS_SDR25:
		preset = sdhci_readw(host, SDHCI_PRESET_FOR_SDR25);
		break;
	case MMC_TIMING_UHS_SDR50:
		preset = sdhci_readw(host, SDHCI_PRESET_FOR_SDR50);
		break;
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_MMC_HS200:
		preset = sdhci_readw(host, SDHCI_PRESET_FOR_SDR104);
		break;
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		preset = sdhci_readw(host, SDHCI_PRESET_FOR_DDR50);
		break;
	case MMC_TIMING_MMC_HS400:
		preset = sdhci_readw(host, SDHCI_PRESET_FOR_HS400);
		break;
	default:
		pr_warn("%s: Invalid UHS-I mode selected\n",
			mmc_hostname(host->mmc));
		preset = sdhci_readw(host, SDHCI_PRESET_FOR_SDR12);
		break;
	}
	return preset;
}

u16 sdhci_calc_clk(struct sdhci_host *host, unsigned int clock,
		   unsigned int *actual_clock)
{
	int div = 0; /* Initialized for compiler warning */
	int real_div = div, clk_mul = 1;
	u16 clk = 0;
	bool switch_base_clk = false;

	if (host->version >= SDHCI_SPEC_300) {
		if (host->preset_enabled) {
			u16 pre_val;

			clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
			pre_val = sdhci_get_preset_value(host);
			div = (pre_val & SDHCI_PRESET_SDCLK_FREQ_MASK)
				>> SDHCI_PRESET_SDCLK_FREQ_SHIFT;
			if (host->clk_mul &&
				(pre_val & SDHCI_PRESET_CLKGEN_SEL_MASK)) {
				clk = SDHCI_PROG_CLOCK_MODE;
				real_div = div + 1;
				clk_mul = host->clk_mul;
			} else {
				real_div = max_t(int, 1, div << 1);
			}
			goto clock_set;
		}

		/*
		 * Check if the Host Controller supports Programmable Clock
		 * Mode.
		 */
		if (host->clk_mul) {
			for (div = 1; div <= 1024; div++) {
				if ((host->max_clk * host->clk_mul / div)
					<= clock)
					break;
			}
			if ((host->max_clk * host->clk_mul / div) <= clock) {
				/*
				 * Set Programmable Clock Mode in the Clock
				 * Control register.
				 */
				clk = SDHCI_PROG_CLOCK_MODE;
				real_div = div;
				clk_mul = host->clk_mul;
				div--;
			} else {
				/*
				 * Divisor can be too small to reach clock
				 * speed requirement. Then use the base clock.
				 */
				switch_base_clk = true;
			}
		}

		if (!host->clk_mul || switch_base_clk) {
			/* Version 3.00 divisors must be a multiple of 2. */
			if (host->max_clk <= clock)
				div = 1;
			else {
				for (div = 2; div < SDHCI_MAX_DIV_SPEC_300;
				     div += 2) {
					if ((host->max_clk / div) <= clock)
						break;
				}
			}
			real_div = div;
			div >>= 1;
			if ((host->quirks2 & SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN)
				&& !div && host->max_clk <= 25000000)
				div = 1;
		}
	} else {
		/* Version 2.00 divisors must be a power of 2. */
		for (div = 1; div < SDHCI_MAX_DIV_SPEC_200; div *= 2) {
			if ((host->max_clk / div) <= clock)
				break;
		}
		real_div = div;
		div >>= 1;
	}

clock_set:
	if (real_div)
		*actual_clock = (host->max_clk * clk_mul) / real_div;
	clk |= (div & SDHCI_DIV_MASK) << SDHCI_DIVIDER_SHIFT;
	clk |= ((div & SDHCI_DIV_HI_MASK) >> SDHCI_DIV_MASK_LEN)
		<< SDHCI_DIVIDER_HI_SHIFT;

	return clk;
}
EXPORT_SYMBOL_GPL(sdhci_calc_clk);

void sdhci_enable_clk(struct sdhci_host *host, u16 clk)
{
	ktime_t timeout;

	clk |= SDHCI_CLOCK_INT_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	/* Wait max 20 ms */
	timeout = ktime_add_ms(ktime_get(), 20);
	while (!((clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL))
		& SDHCI_CLOCK_INT_STABLE)) {
		if (ktime_after(ktime_get(), timeout)) {
			pr_err("%s: Internal clock never stabilised.\n",
			       mmc_hostname(host->mmc));
			sdhci_dumpregs(host);
			return;
		}
		udelay(10);
	}

	clk |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
}
EXPORT_SYMBOL_GPL(sdhci_enable_clk);

void sdhci_set_clock(struct sdhci_host *host, unsigned int clock)
{
	u16 clk;

	host->mmc->actual_clock = 0;

	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		return;

	clk = sdhci_calc_clk(host, clock, &host->mmc->actual_clock);
	sdhci_enable_clk(host, clk);
}
EXPORT_SYMBOL_GPL(sdhci_set_clock);

static void sdhci_set_power_reg(struct sdhci_host *host, unsigned char mode,
				unsigned short vdd)
{
	struct mmc_host *mmc = host->mmc;

	mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, vdd);

	if (mode != MMC_POWER_OFF)
		sdhci_writeb(host, SDHCI_POWER_ON, SDHCI_POWER_CONTROL);
	else
		sdhci_writeb(host, 0, SDHCI_POWER_CONTROL);
}

void sdhci_set_power_noreg(struct sdhci_host *host, unsigned char mode,
			   unsigned short vdd)
{
	u8 pwr = 0;

	if (mode != MMC_POWER_OFF) {
		switch (1 << vdd) {
		case MMC_VDD_165_195:
			pwr = SDHCI_POWER_180;
			break;
		case MMC_VDD_29_30:
		case MMC_VDD_30_31:
			pwr = SDHCI_POWER_300;
			break;
		case MMC_VDD_32_33:
		case MMC_VDD_33_34:
			pwr = SDHCI_POWER_330;
			break;
		default:
			WARN(1, "%s: Invalid vdd %#x\n",
			     mmc_hostname(host->mmc), vdd);
			break;
		}
	}

	if (host->pwr == pwr)
		return;

	host->pwr = pwr;

	if (pwr == 0) {
		sdhci_writeb(host, 0, SDHCI_POWER_CONTROL);
		if (host->quirks2 & SDHCI_QUIRK2_CARD_ON_NEEDS_BUS_ON)
			sdhci_runtime_pm_bus_off(host);
	} else {
		/*
		 * Spec says that we should clear the power reg before setting
		 * a new value. Some controllers don't seem to like this though.
		 */
		if (!(host->quirks & SDHCI_QUIRK_SINGLE_POWER_WRITE))
			sdhci_writeb(host, 0, SDHCI_POWER_CONTROL);

		/*
		 * At least the Marvell CaFe chip gets confused if we set the
		 * voltage and set turn on power at the same time, so set the
		 * voltage first.
		 */
		if (host->quirks & SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER)
			sdhci_writeb(host, pwr, SDHCI_POWER_CONTROL);

		pwr |= SDHCI_POWER_ON;

		sdhci_writeb(host, pwr, SDHCI_POWER_CONTROL);

		if (host->quirks2 & SDHCI_QUIRK2_CARD_ON_NEEDS_BUS_ON)
			sdhci_runtime_pm_bus_on(host);

		/*
		 * Some controllers need an extra 10ms delay of 10ms before
		 * they can apply clock after applying power
		 */
		if (host->quirks & SDHCI_QUIRK_DELAY_AFTER_POWER)
			mdelay(10);
	}
}
EXPORT_SYMBOL_GPL(sdhci_set_power_noreg);

void sdhci_set_power(struct sdhci_host *host, unsigned char mode,
		     unsigned short vdd)
{
	if (IS_ERR(host->mmc->supply.vmmc))
		sdhci_set_power_noreg(host, mode, vdd);
	else
		sdhci_set_power_reg(host, mode, vdd);
}
EXPORT_SYMBOL_GPL(sdhci_set_power);

/*****************************************************************************\
 *                                                                           *
 * MMC callbacks                                                             *
 *                                                                           *
\*****************************************************************************/

static void sdhci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sdhci_host *host;
	int present;
	unsigned long flags;

	host = mmc_priv(mmc);

	/* Firstly check card presence */
	present = mmc->ops->get_cd(mmc);

	spin_lock_irqsave(&host->lock, flags);

	sdhci_led_activate(host);

	/*
	 * Ensure we don't send the STOP for non-SET_BLOCK_COUNTED
	 * requests if Auto-CMD12 is enabled.
	 */
	if (sdhci_auto_cmd12(host, mrq)) {
		if (mrq->stop) {
			mrq->data->stop = NULL;
			mrq->stop = NULL;
		}
	}

	if (!present || host->flags & SDHCI_DEVICE_DEAD) {
		mrq->cmd->error = -ENOMEDIUM;
		sdhci_finish_mrq(host, mrq);
	} else {
		if (mrq->sbc && !(host->flags & SDHCI_AUTO_CMD23))
			sdhci_send_command(host, mrq->sbc);
		else
			sdhci_send_command(host, mrq->cmd);
	}

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}

void sdhci_set_bus_width(struct sdhci_host *host, int width)
{
	u8 ctrl;

	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
	if (width == MMC_BUS_WIDTH_8) {
		ctrl &= ~SDHCI_CTRL_4BITBUS;
		ctrl |= SDHCI_CTRL_8BITBUS;
	} else {
		if (host->mmc->caps & MMC_CAP_8_BIT_DATA)
			ctrl &= ~SDHCI_CTRL_8BITBUS;
		if (width == MMC_BUS_WIDTH_4)
			ctrl |= SDHCI_CTRL_4BITBUS;
		else
			ctrl &= ~SDHCI_CTRL_4BITBUS;
	}
	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
}
EXPORT_SYMBOL_GPL(sdhci_set_bus_width);

void sdhci_set_uhs_signaling(struct sdhci_host *host, unsigned timing)
{
	u16 ctrl_2;

	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	/* Select Bus Speed Mode for host */
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	if ((timing == MMC_TIMING_MMC_HS200) ||
	    (timing == MMC_TIMING_UHS_SDR104))
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104;
	else if (timing == MMC_TIMING_UHS_SDR12)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR12;
	else if (timing == MMC_TIMING_UHS_SDR25)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25;
	else if (timing == MMC_TIMING_UHS_SDR50)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR50;
	else if ((timing == MMC_TIMING_UHS_DDR50) ||
		 (timing == MMC_TIMING_MMC_DDR52))
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50;
	else if (timing == MMC_TIMING_MMC_HS400)
		ctrl_2 |= SDHCI_CTRL_HS400; /* Non-standard */
	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
}
EXPORT_SYMBOL_GPL(sdhci_set_uhs_signaling);

void sdhci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u8 ctrl;

	if (ios->power_mode == MMC_POWER_UNDEFINED)
		return;

	if (host->flags & SDHCI_DEVICE_DEAD) {
		if (!IS_ERR(mmc->supply.vmmc) &&
		    ios->power_mode == MMC_POWER_OFF)
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, 0);
		return;
	}

	/*
	 * Reset the chip on each power off.
	 * Should clear out any weird states.
	 */
	if (ios->power_mode == MMC_POWER_OFF) {
		sdhci_writel(host, 0, SDHCI_SIGNAL_ENABLE);
		sdhci_reinit(host);
	}

	if (host->version >= SDHCI_SPEC_300 &&
		(ios->power_mode == MMC_POWER_UP) &&
		!(host->quirks2 & SDHCI_QUIRK2_PRESET_VALUE_BROKEN))
		sdhci_enable_preset_value(host, false);

	if (!ios->clock || ios->clock != host->clock) {
		host->ops->set_clock(host, ios->clock);
		host->clock = ios->clock;

		if (host->quirks & SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK &&
		    host->clock) {
			host->timeout_clk = host->mmc->actual_clock ?
						host->mmc->actual_clock / 1000 :
						host->clock / 1000;
			host->mmc->max_busy_timeout =
				host->ops->get_max_timeout_count ?
				host->ops->get_max_timeout_count(host) :
				1 << 27;
			host->mmc->max_busy_timeout /= host->timeout_clk;
		}
	}

	if (host->ops->set_power)
		host->ops->set_power(host, ios->power_mode, ios->vdd);
	else
		sdhci_set_power(host, ios->power_mode, ios->vdd);

	if (host->ops->platform_send_init_74_clocks)
		host->ops->platform_send_init_74_clocks(host, ios->power_mode);

	host->ops->set_bus_width(host, ios->bus_width);

	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);

	if (!(host->quirks & SDHCI_QUIRK_NO_HISPD_BIT)) {
		if (ios->timing == MMC_TIMING_SD_HS ||
		     ios->timing == MMC_TIMING_MMC_HS ||
		     ios->timing == MMC_TIMING_MMC_HS400 ||
		     ios->timing == MMC_TIMING_MMC_HS200 ||
		     ios->timing == MMC_TIMING_MMC_DDR52 ||
		     ios->timing == MMC_TIMING_UHS_SDR50 ||
		     ios->timing == MMC_TIMING_UHS_SDR104 ||
		     ios->timing == MMC_TIMING_UHS_DDR50 ||
		     ios->timing == MMC_TIMING_UHS_SDR25)
			ctrl |= SDHCI_CTRL_HISPD;
		else
			ctrl &= ~SDHCI_CTRL_HISPD;
	}

	if (host->version >= SDHCI_SPEC_300) {
		u16 clk, ctrl_2;

		if (!host->preset_enabled) {
			sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
			/*
			 * We only need to set Driver Strength if the
			 * preset value enable is not set.
			 */
			ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
			ctrl_2 &= ~SDHCI_CTRL_DRV_TYPE_MASK;
			if (ios->drv_type == MMC_SET_DRIVER_TYPE_A)
				ctrl_2 |= SDHCI_CTRL_DRV_TYPE_A;
			else if (ios->drv_type == MMC_SET_DRIVER_TYPE_B)
				ctrl_2 |= SDHCI_CTRL_DRV_TYPE_B;
			else if (ios->drv_type == MMC_SET_DRIVER_TYPE_C)
				ctrl_2 |= SDHCI_CTRL_DRV_TYPE_C;
			else if (ios->drv_type == MMC_SET_DRIVER_TYPE_D)
				ctrl_2 |= SDHCI_CTRL_DRV_TYPE_D;
			else {
				pr_warn("%s: invalid driver type, default to driver type B\n",
					mmc_hostname(mmc));
				ctrl_2 |= SDHCI_CTRL_DRV_TYPE_B;
			}

			sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
		} else {
			/*
			 * According to SDHC Spec v3.00, if the Preset Value
			 * Enable in the Host Control 2 register is set, we
			 * need to reset SD Clock Enable before changing High
			 * Speed Enable to avoid generating clock gliches.
			 */

			/* Reset SD Clock Enable */
			clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
			clk &= ~SDHCI_CLOCK_CARD_EN;
			sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

			sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);

			/* Re-enable SD Clock */
			host->ops->set_clock(host, host->clock);
		}

		/* Reset SD Clock Enable */
		clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
		clk &= ~SDHCI_CLOCK_CARD_EN;
		sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

		host->ops->set_uhs_signaling(host, ios->timing);
		host->timing = ios->timing;

		if (!(host->quirks2 & SDHCI_QUIRK2_PRESET_VALUE_BROKEN) &&
				((ios->timing == MMC_TIMING_UHS_SDR12) ||
				 (ios->timing == MMC_TIMING_UHS_SDR25) ||
				 (ios->timing == MMC_TIMING_UHS_SDR50) ||
				 (ios->timing == MMC_TIMING_UHS_SDR104) ||
				 (ios->timing == MMC_TIMING_UHS_DDR50) ||
				 (ios->timing == MMC_TIMING_MMC_DDR52))) {
			u16 preset;

			sdhci_enable_preset_value(host, true);
			preset = sdhci_get_preset_value(host);
			ios->drv_type = (preset & SDHCI_PRESET_DRV_MASK)
				>> SDHCI_PRESET_DRV_SHIFT;
		}

		/* Re-enable SD Clock */
		host->ops->set_clock(host, host->clock);
	} else
		sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);

	/*
	 * Some (ENE) controllers go apeshit on some ios operation,
	 * signalling timeout and CRC errors even on CMD0. Resetting
	 * it on each ios seems to solve the problem.
	 */
	if (host->quirks & SDHCI_QUIRK_RESET_CMD_DATA_ON_IOS)
		sdhci_do_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);

	mmiowb();
}
EXPORT_SYMBOL_GPL(sdhci_set_ios);

static int sdhci_get_cd(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	int gpio_cd = mmc_gpio_get_cd(mmc);

	if (host->flags & SDHCI_DEVICE_DEAD)
		return 0;

	/* If nonremovable, assume that the card is always present. */
	if (!mmc_card_is_removable(host->mmc))
		return 1;

	/*
	 * Try slot gpio detect, if defined it take precedence
	 * over build in controller functionality
	 */
	if (gpio_cd >= 0)
		return !!gpio_cd;

	/* If polling, assume that the card is always present. */
	if (host->quirks & SDHCI_QUIRK_BROKEN_CARD_DETECTION)
		return 1;

	/* Host native card detect */
	return !!(sdhci_readl(host, SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT);
}

static int sdhci_check_ro(struct sdhci_host *host)
{
	unsigned long flags;
	int is_readonly;

	spin_lock_irqsave(&host->lock, flags);

	if (host->flags & SDHCI_DEVICE_DEAD)
		is_readonly = 0;
	else if (host->ops->get_ro)
		is_readonly = host->ops->get_ro(host);
	else
		is_readonly = !(sdhci_readl(host, SDHCI_PRESENT_STATE)
				& SDHCI_WRITE_PROTECT);

	spin_unlock_irqrestore(&host->lock, flags);

	/* This quirk needs to be replaced by a callback-function later */
	return host->quirks & SDHCI_QUIRK_INVERTED_WRITE_PROTECT ?
		!is_readonly : is_readonly;
}

#define SAMPLE_COUNT	5

static int sdhci_get_ro(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	int i, ro_count;

	if (!(host->quirks & SDHCI_QUIRK_UNSTABLE_RO_DETECT))
		return sdhci_check_ro(host);

	ro_count = 0;
	for (i = 0; i < SAMPLE_COUNT; i++) {
		if (sdhci_check_ro(host)) {
			if (++ro_count > SAMPLE_COUNT / 2)
				return 1;
		}
		msleep(30);
	}
	return 0;
}

static void sdhci_hw_reset(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);

	if (host->ops && host->ops->hw_reset)
		host->ops->hw_reset(host);
}

static void sdhci_enable_sdio_irq_nolock(struct sdhci_host *host, int enable)
{
	if (!(host->flags & SDHCI_DEVICE_DEAD)) {
		if (enable)
			host->ier |= SDHCI_INT_CARD_INT;
		else
			host->ier &= ~SDHCI_INT_CARD_INT;

		sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
		sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
		mmiowb();
	}
}

void sdhci_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct sdhci_host *host = mmc_priv(mmc);
	unsigned long flags;

	if (enable)
		pm_runtime_get_noresume(host->mmc->parent);

	spin_lock_irqsave(&host->lock, flags);
	if (enable)
		host->flags |= SDHCI_SDIO_IRQ_ENABLED;
	else
		host->flags &= ~SDHCI_SDIO_IRQ_ENABLED;

	sdhci_enable_sdio_irq_nolock(host, enable);
	spin_unlock_irqrestore(&host->lock, flags);

	if (!enable)
		pm_runtime_put_noidle(host->mmc->parent);
}
EXPORT_SYMBOL_GPL(sdhci_enable_sdio_irq);

int sdhci_start_signal_voltage_switch(struct mmc_host *mmc,
				      struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u16 ctrl;
	int ret;

	/*
	 * Signal Voltage Switching is only applicable for Host Controllers
	 * v3.00 and above.
	 */
	if (host->version < SDHCI_SPEC_300)
		return 0;

	ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		if (!(host->flags & SDHCI_SIGNALING_330))
			return -EINVAL;
		/* Set 1.8V Signal Enable in the Host Control2 register to 0 */
		ctrl &= ~SDHCI_CTRL_VDD_180;
		sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);

		if (!IS_ERR(mmc->supply.vqmmc)) {
			ret = mmc_regulator_set_vqmmc(mmc, ios);
			if (ret) {
				pr_warn("%s: Switching to 3.3V signalling voltage failed\n",
					mmc_hostname(mmc));
				return -EIO;
			}
		}
		/* Wait for 5ms */
		usleep_range(5000, 5500);

		/* 3.3V regulator output should be stable within 5 ms */
		ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		if (!(ctrl & SDHCI_CTRL_VDD_180))
			return 0;

		pr_warn("%s: 3.3V regulator output did not became stable\n",
			mmc_hostname(mmc));

		return -EAGAIN;
	case MMC_SIGNAL_VOLTAGE_180:
		if (!(host->flags & SDHCI_SIGNALING_180))
			return -EINVAL;
		if (!IS_ERR(mmc->supply.vqmmc)) {
			ret = mmc_regulator_set_vqmmc(mmc, ios);
			if (ret) {
				pr_warn("%s: Switching to 1.8V signalling voltage failed\n",
					mmc_hostname(mmc));
				return -EIO;
			}
		}

		/*
		 * Enable 1.8V Signal Enable in the Host Control2
		 * register
		 */
		ctrl |= SDHCI_CTRL_VDD_180;
		sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);

		/* Some controller need to do more when switching */
		if (host->ops->voltage_switch)
			host->ops->voltage_switch(host);

		/* 1.8V regulator output should be stable within 5 ms */
		ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		if (ctrl & SDHCI_CTRL_VDD_180)
			return 0;

		pr_warn("%s: 1.8V regulator output did not became stable\n",
			mmc_hostname(mmc));

		return -EAGAIN;
	case MMC_SIGNAL_VOLTAGE_120:
		if (!(host->flags & SDHCI_SIGNALING_120))
			return -EINVAL;
		if (!IS_ERR(mmc->supply.vqmmc)) {
			ret = mmc_regulator_set_vqmmc(mmc, ios);
			if (ret) {
				pr_warn("%s: Switching to 1.2V signalling voltage failed\n",
					mmc_hostname(mmc));
				return -EIO;
			}
		}
		return 0;
	default:
		/* No signal voltage switch required */
		return 0;
	}
}
EXPORT_SYMBOL_GPL(sdhci_start_signal_voltage_switch);

static int sdhci_card_busy(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u32 present_state;

	/* Check whether DAT[0] is 0 */
	present_state = sdhci_readl(host, SDHCI_PRESENT_STATE);

	return !(present_state & SDHCI_DATA_0_LVL_MASK);
}

static int sdhci_prepare_hs400_tuning(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	host->flags |= SDHCI_HS400_TUNING;
	spin_unlock_irqrestore(&host->lock, flags);

	return 0;
}

static void sdhci_start_tuning(struct sdhci_host *host)
{
	u16 ctrl;

	ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl |= SDHCI_CTRL_EXEC_TUNING;
	if (host->quirks2 & SDHCI_QUIRK2_TUNING_WORK_AROUND)
		ctrl |= SDHCI_CTRL_TUNED_CLK;
	sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);

	/*
	 * As per the Host Controller spec v3.00, tuning command
	 * generates Buffer Read Ready interrupt, so enable that.
	 *
	 * Note: The spec clearly says that when tuning sequence
	 * is being performed, the controller does not generate
	 * interrupts other than Buffer Read Ready interrupt. But
	 * to make sure we don't hit a controller bug, we _only_
	 * enable Buffer Read Ready interrupt here.
	 */
	sdhci_writel(host, SDHCI_INT_DATA_AVAIL, SDHCI_INT_ENABLE);
	sdhci_writel(host, SDHCI_INT_DATA_AVAIL, SDHCI_SIGNAL_ENABLE);
}

static void sdhci_end_tuning(struct sdhci_host *host)
{
	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
}

static void sdhci_reset_tuning(struct sdhci_host *host)
{
	u16 ctrl;

	ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl &= ~SDHCI_CTRL_TUNED_CLK;
	ctrl &= ~SDHCI_CTRL_EXEC_TUNING;
	sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
}

static void sdhci_abort_tuning(struct sdhci_host *host, u32 opcode)
{
	sdhci_reset_tuning(host);

	sdhci_do_reset(host, SDHCI_RESET_CMD);
	sdhci_do_reset(host, SDHCI_RESET_DATA);

	sdhci_end_tuning(host);

	mmc_abort_tuning(host->mmc, opcode);
}

/*
 * We use sdhci_send_tuning() because mmc_send_tuning() is not a good fit. SDHCI
 * tuning command does not have a data payload (or rather the hardware does it
 * automatically) so mmc_send_tuning() will return -EIO. Also the tuning command
 * interrupt setup is different to other commands and there is no timeout
 * interrupt so special handling is needed.
 */
static void sdhci_send_tuning(struct sdhci_host *host, u32 opcode)
{
	struct mmc_host *mmc = host->mmc;
	struct mmc_command cmd = {};
	struct mmc_request mrq = {};
	unsigned long flags;
	u32 b = host->sdma_boundary;

	spin_lock_irqsave(&host->lock, flags);

	cmd.opcode = opcode;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
	cmd.mrq = &mrq;

	mrq.cmd = &cmd;
	/*
	 * In response to CMD19, the card sends 64 bytes of tuning
	 * block to the Host Controller. So we set the block size
	 * to 64 here.
	 */
	if (cmd.opcode == MMC_SEND_TUNING_BLOCK_HS200 &&
	    mmc->ios.bus_width == MMC_BUS_WIDTH_8)
		sdhci_writew(host, SDHCI_MAKE_BLKSZ(b, 128), SDHCI_BLOCK_SIZE);
	else
		sdhci_writew(host, SDHCI_MAKE_BLKSZ(b, 64), SDHCI_BLOCK_SIZE);

	/*
	 * The tuning block is sent by the card to the host controller.
	 * So we set the TRNS_READ bit in the Transfer Mode register.
	 * This also takes care of setting DMA Enable and Multi Block
	 * Select in the same register to 0.
	 */
	sdhci_writew(host, SDHCI_TRNS_READ, SDHCI_TRANSFER_MODE);

	sdhci_send_command(host, &cmd);

	host->cmd = NULL;

	sdhci_del_timer(host, &mrq);

	host->tuning_done = 0;

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);

	/* Wait for Buffer Read Ready interrupt */
	wait_event_timeout(host->buf_ready_int, (host->tuning_done == 1),
			   msecs_to_jiffies(50));

}

static void __sdhci_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	int i;

	/*
	 * Issue opcode repeatedly till Execute Tuning is set to 0 or the number
	 * of loops reaches 40 times.
	 */
	for (i = 0; i < MAX_TUNING_LOOP; i++) {
		u16 ctrl;

		sdhci_send_tuning(host, opcode);

		if (!host->tuning_done) {
			pr_info("%s: Tuning timeout, falling back to fixed sampling clock\n",
				mmc_hostname(host->mmc));
			sdhci_abort_tuning(host, opcode);
			return;
		}

		ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		if (!(ctrl & SDHCI_CTRL_EXEC_TUNING)) {
			if (ctrl & SDHCI_CTRL_TUNED_CLK)
				return; /* Success! */
			break;
		}

		/* Spec does not require a delay between tuning cycles */
		if (host->tuning_delay > 0)
			mdelay(host->tuning_delay);
	}

	pr_info("%s: Tuning failed, falling back to fixed sampling clock\n",
		mmc_hostname(host->mmc));
	sdhci_reset_tuning(host);
}

int sdhci_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	int err = 0;
	unsigned int tuning_count = 0;
	bool hs400_tuning;

	hs400_tuning = host->flags & SDHCI_HS400_TUNING;

	if (host->tuning_mode == SDHCI_TUNING_MODE_1)
		tuning_count = host->tuning_count;

	/*
	 * The Host Controller needs tuning in case of SDR104 and DDR50
	 * mode, and for SDR50 mode when Use Tuning for SDR50 is set in
	 * the Capabilities register.
	 * If the Host Controller supports the HS200 mode then the
	 * tuning function has to be executed.
	 */
	switch (host->timing) {
	/* HS400 tuning is done in HS200 mode */
	case MMC_TIMING_MMC_HS400:
		err = -EINVAL;
		goto out;

	case MMC_TIMING_MMC_HS200:
		/*
		 * Periodic re-tuning for HS400 is not expected to be needed, so
		 * disable it here.
		 */
		if (hs400_tuning)
			tuning_count = 0;
		break;

	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_UHS_DDR50:
		break;

	case MMC_TIMING_UHS_SDR50:
		if (host->flags & SDHCI_SDR50_NEEDS_TUNING)
			break;
		/* FALLTHROUGH */

	default:
		goto out;
	}

	if (host->ops->platform_execute_tuning) {
		err = host->ops->platform_execute_tuning(host, opcode);
		goto out;
	}

	host->mmc->retune_period = tuning_count;

	if (host->tuning_delay < 0)
		host->tuning_delay = opcode == MMC_SEND_TUNING_BLOCK;

	sdhci_start_tuning(host);

	__sdhci_execute_tuning(host, opcode);

	sdhci_end_tuning(host);
out:
	host->flags &= ~SDHCI_HS400_TUNING;

	return err;
}
EXPORT_SYMBOL_GPL(sdhci_execute_tuning);

static void sdhci_enable_preset_value(struct sdhci_host *host, bool enable)
{
	/* Host Controller v3.00 defines preset value registers */
	if (host->version < SDHCI_SPEC_300)
		return;

	/*
	 * We only enable or disable Preset Value if they are not already
	 * enabled or disabled respectively. Otherwise, we bail out.
	 */
	if (host->preset_enabled != enable) {
		u16 ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);

		if (enable)
			ctrl |= SDHCI_CTRL_PRESET_VAL_ENABLE;
		else
			ctrl &= ~SDHCI_CTRL_PRESET_VAL_ENABLE;

		sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);

		if (enable)
			host->flags |= SDHCI_PV_ENABLED;
		else
			host->flags &= ~SDHCI_PV_ENABLED;

		host->preset_enabled = enable;
	}
}

static void sdhci_post_req(struct mmc_host *mmc, struct mmc_request *mrq,
				int err)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if (data->host_cookie != COOKIE_UNMAPPED)
		dma_unmap_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
			     mmc_get_dma_dir(data));

	data->host_cookie = COOKIE_UNMAPPED;
}

static void sdhci_pre_req(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sdhci_host *host = mmc_priv(mmc);

	mrq->data->host_cookie = COOKIE_UNMAPPED;

	if (host->flags & SDHCI_REQ_USE_DMA)
		sdhci_pre_dma_transfer(host, mrq->data, COOKIE_PRE_MAPPED);
}

static inline bool sdhci_has_requests(struct sdhci_host *host)
{
	return host->cmd || host->data_cmd;
}

static void sdhci_error_out_mrqs(struct sdhci_host *host, int err)
{
	if (host->data_cmd) {
		host->data_cmd->error = err;
		sdhci_finish_mrq(host, host->data_cmd->mrq);
	}

	if (host->cmd) {
		host->cmd->error = err;
		sdhci_finish_mrq(host, host->cmd->mrq);
	}
}

static void sdhci_card_event(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	unsigned long flags;
	int present;

	/* First check if client has provided their own card event */
	if (host->ops->card_event)
		host->ops->card_event(host);

	present = mmc->ops->get_cd(mmc);

	spin_lock_irqsave(&host->lock, flags);

	/* Check sdhci_has_requests() first in case we are runtime suspended */
	if (sdhci_has_requests(host) && !present) {
		pr_err("%s: Card removed during transfer!\n",
			mmc_hostname(host->mmc));
		pr_err("%s: Resetting controller.\n",
			mmc_hostname(host->mmc));

		sdhci_do_reset(host, SDHCI_RESET_CMD);
		sdhci_do_reset(host, SDHCI_RESET_DATA);

		sdhci_error_out_mrqs(host, -ENOMEDIUM);
	}

	spin_unlock_irqrestore(&host->lock, flags);
}

static const struct mmc_host_ops sdhci_ops = {
	.request	= sdhci_request,
	.post_req	= sdhci_post_req,
	.pre_req	= sdhci_pre_req,
	.set_ios	= sdhci_set_ios,
	.get_cd		= sdhci_get_cd,
	.get_ro		= sdhci_get_ro,
	.hw_reset	= sdhci_hw_reset,
	.enable_sdio_irq = sdhci_enable_sdio_irq,
	.start_signal_voltage_switch	= sdhci_start_signal_voltage_switch,
	.prepare_hs400_tuning		= sdhci_prepare_hs400_tuning,
	.execute_tuning			= sdhci_execute_tuning,
	.card_event			= sdhci_card_event,
	.card_busy	= sdhci_card_busy,
};

/*****************************************************************************\
 *                                                                           *
 * Tasklets                                                                  *
 *                                                                           *
\*****************************************************************************/

static bool sdhci_request_done(struct sdhci_host *host)
{
	unsigned long flags;
	struct mmc_request *mrq;
	int i;

	spin_lock_irqsave(&host->lock, flags);

	for (i = 0; i < SDHCI_MAX_MRQS; i++) {
		mrq = host->mrqs_done[i];
		if (mrq)
			break;
	}

	if (!mrq) {
		spin_unlock_irqrestore(&host->lock, flags);
		return true;
	}

	sdhci_del_timer(host, mrq);

	/*
	 * Always unmap the data buffers if they were mapped by
	 * sdhci_prepare_data() whenever we finish with a request.
	 * This avoids leaking DMA mappings on error.
	 */
	if (host->flags & SDHCI_REQ_USE_DMA) {
		struct mmc_data *data = mrq->data;

		if (data && data->host_cookie == COOKIE_MAPPED) {
			dma_unmap_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
				     mmc_get_dma_dir(data));
			data->host_cookie = COOKIE_UNMAPPED;
		}
	}

	/*
	 * The controller needs a reset of internal state machines
	 * upon error conditions.
	 */
	if (sdhci_needs_reset(host, mrq)) {
		/*
		 * Do not finish until command and data lines are available for
		 * reset. Note there can only be one other mrq, so it cannot
		 * also be in mrqs_done, otherwise host->cmd and host->data_cmd
		 * would both be null.
		 */
		if (host->cmd || host->data_cmd) {
			spin_unlock_irqrestore(&host->lock, flags);
			return true;
		}

		/* Some controllers need this kick or reset won't work here */
		if (host->quirks & SDHCI_QUIRK_CLOCK_BEFORE_RESET)
			/* This is to force an update */
			host->ops->set_clock(host, host->clock);

		/* Spec says we should do both at the same time, but Ricoh
		   controllers do not like that. */
		sdhci_do_reset(host, SDHCI_RESET_CMD);
		sdhci_do_reset(host, SDHCI_RESET_DATA);

		host->pending_reset = false;
	}

	if (!sdhci_has_requests(host))
		sdhci_led_deactivate(host);

	host->mrqs_done[i] = NULL;

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);

	mmc_request_done(host->mmc, mrq);

	return false;
}

static void sdhci_tasklet_finish(unsigned long param)
{
	struct sdhci_host *host = (struct sdhci_host *)param;

	while (!sdhci_request_done(host))
		;
}

static void sdhci_timeout_timer(unsigned long data)
{
	struct sdhci_host *host;
	unsigned long flags;

	host = (struct sdhci_host*)data;

	spin_lock_irqsave(&host->lock, flags);

	if (host->cmd && !sdhci_data_line_cmd(host->cmd)) {
		pr_err("%s: Timeout waiting for hardware cmd interrupt.\n",
		       mmc_hostname(host->mmc));
		sdhci_dumpregs(host);

		host->cmd->error = -ETIMEDOUT;
		sdhci_finish_mrq(host, host->cmd->mrq);
	}

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}

static void sdhci_timeout_data_timer(unsigned long data)
{
	struct sdhci_host *host;
	unsigned long flags;

	host = (struct sdhci_host *)data;

	spin_lock_irqsave(&host->lock, flags);

	if (host->data || host->data_cmd ||
	    (host->cmd && sdhci_data_line_cmd(host->cmd))) {
		pr_err("%s: Timeout waiting for hardware interrupt.\n",
		       mmc_hostname(host->mmc));
		sdhci_dumpregs(host);

		if (host->data) {
			host->data->error = -ETIMEDOUT;
			sdhci_finish_data(host);
		} else if (host->data_cmd) {
			host->data_cmd->error = -ETIMEDOUT;
			sdhci_finish_mrq(host, host->data_cmd->mrq);
		} else {
			host->cmd->error = -ETIMEDOUT;
			sdhci_finish_mrq(host, host->cmd->mrq);
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
	if (!host->cmd) {
		/*
		 * SDHCI recovers from errors by resetting the cmd and data
		 * circuits.  Until that is done, there very well might be more
		 * interrupts, so ignore them in that case.
		 */
		if (host->pending_reset)
			return;
		pr_err("%s: Got command interrupt 0x%08x even though no command operation was in progress.\n",
		       mmc_hostname(host->mmc), (unsigned)intmask);
		sdhci_dumpregs(host);
		return;
	}

	if (intmask & (SDHCI_INT_TIMEOUT | SDHCI_INT_CRC |
		       SDHCI_INT_END_BIT | SDHCI_INT_INDEX)) {
		if (intmask & SDHCI_INT_TIMEOUT)
			host->cmd->error = -ETIMEDOUT;
		else
			host->cmd->error = -EILSEQ;

		/*
		 * If this command initiates a data phase and a response
		 * CRC error is signalled, the card can start transferring
		 * data - the card may have received the command without
		 * error.  We must not terminate the mmc_request early.
		 *
		 * If the card did not receive the command or returned an
		 * error which prevented it sending data, the data phase
		 * will time out.
		 */
		if (host->cmd->data &&
		    (intmask & (SDHCI_INT_CRC | SDHCI_INT_TIMEOUT)) ==
		     SDHCI_INT_CRC) {
			host->cmd = NULL;
			return;
		}

		sdhci_finish_mrq(host, host->cmd->mrq);
		return;
	}

	if (intmask & SDHCI_INT_RESPONSE)
		sdhci_finish_command(host);
}

static void sdhci_adma_show_error(struct sdhci_host *host)
{
	void *desc = host->adma_table;

	sdhci_dumpregs(host);

	while (true) {
		struct sdhci_adma2_64_desc *dma_desc = desc;

		if (host->flags & SDHCI_USE_64_BIT_DMA)
			DBG("%p: DMA 0x%08x%08x, LEN 0x%04x, Attr=0x%02x\n",
			    desc, le32_to_cpu(dma_desc->addr_hi),
			    le32_to_cpu(dma_desc->addr_lo),
			    le16_to_cpu(dma_desc->len),
			    le16_to_cpu(dma_desc->cmd));
		else
			DBG("%p: DMA 0x%08x, LEN 0x%04x, Attr=0x%02x\n",
			    desc, le32_to_cpu(dma_desc->addr_lo),
			    le16_to_cpu(dma_desc->len),
			    le16_to_cpu(dma_desc->cmd));

		desc += host->desc_sz;

		if (dma_desc->cmd & cpu_to_le16(ADMA2_END))
			break;
	}
}

static void sdhci_data_irq(struct sdhci_host *host, u32 intmask)
{
	u32 command;

	/* CMD19 generates _only_ Buffer Read Ready interrupt */
	if (intmask & SDHCI_INT_DATA_AVAIL) {
		command = SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND));
		if (command == MMC_SEND_TUNING_BLOCK ||
		    command == MMC_SEND_TUNING_BLOCK_HS200) {
			host->tuning_done = 1;
			wake_up(&host->buf_ready_int);
			return;
		}
	}

	if (!host->data) {
		struct mmc_command *data_cmd = host->data_cmd;

		/*
		 * The "data complete" interrupt is also used to
		 * indicate that a busy state has ended. See comment
		 * above in sdhci_cmd_irq().
		 */
		if (data_cmd && (data_cmd->flags & MMC_RSP_BUSY)) {
			if (intmask & SDHCI_INT_DATA_TIMEOUT) {
				host->data_cmd = NULL;
				data_cmd->error = -ETIMEDOUT;
				sdhci_finish_mrq(host, data_cmd->mrq);
				return;
			}
			if (intmask & SDHCI_INT_DATA_END) {
				host->data_cmd = NULL;
				/*
				 * Some cards handle busy-end interrupt
				 * before the command completed, so make
				 * sure we do things in the proper order.
				 */
				if (host->cmd == data_cmd)
					return;

				sdhci_finish_mrq(host, data_cmd->mrq);
				return;
			}
		}

		/*
		 * SDHCI recovers from errors by resetting the cmd and data
		 * circuits. Until that is done, there very well might be more
		 * interrupts, so ignore them in that case.
		 */
		if (host->pending_reset)
			return;

		pr_err("%s: Got data interrupt 0x%08x even though no data operation was in progress.\n",
		       mmc_hostname(host->mmc), (unsigned)intmask);
		sdhci_dumpregs(host);

		return;
	}

	if (intmask & SDHCI_INT_DATA_TIMEOUT)
		host->data->error = -ETIMEDOUT;
	else if (intmask & SDHCI_INT_DATA_END_BIT)
		host->data->error = -EILSEQ;
	else if ((intmask & SDHCI_INT_DATA_CRC) &&
		SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND))
			!= MMC_BUS_TEST_R)
		host->data->error = -EILSEQ;
	else if (intmask & SDHCI_INT_ADMA_ERROR) {
		pr_err("%s: ADMA error\n", mmc_hostname(host->mmc));
		sdhci_adma_show_error(host);
		host->data->error = -EIO;
		if (host->ops->adma_workaround)
			host->ops->adma_workaround(host, intmask);
	}

	if (host->data->error)
		sdhci_finish_data(host);
	else {
		if (intmask & (SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL))
			sdhci_transfer_pio(host);

		/*
		 * We currently don't do anything fancy with DMA
		 * boundaries, but as we can't disable the feature
		 * we need to at least restart the transfer.
		 *
		 * According to the spec sdhci_readl(host, SDHCI_DMA_ADDRESS)
		 * should return a valid address to continue from, but as
		 * some controllers are faulty, don't trust them.
		 */
		if (intmask & SDHCI_INT_DMA_END) {
			u32 dmastart, dmanow;
			dmastart = sg_dma_address(host->data->sg);
			dmanow = dmastart + host->data->bytes_xfered;
			/*
			 * Force update to the next DMA block boundary.
			 */
			dmanow = (dmanow &
				~(SDHCI_DEFAULT_BOUNDARY_SIZE - 1)) +
				SDHCI_DEFAULT_BOUNDARY_SIZE;
			host->data->bytes_xfered = dmanow - dmastart;
			DBG("DMA base 0x%08x, transferred 0x%06x bytes, next 0x%08x\n",
			    dmastart, host->data->bytes_xfered, dmanow);
			sdhci_writel(host, dmanow, SDHCI_DMA_ADDRESS);
		}

		if (intmask & SDHCI_INT_DATA_END) {
			if (host->cmd == host->data_cmd) {
				/*
				 * Data managed to finish before the
				 * command completed. Make sure we do
				 * things in the proper order.
				 */
				host->data_early = 1;
			} else {
				sdhci_finish_data(host);
			}
		}
	}
}

static irqreturn_t sdhci_irq(int irq, void *dev_id)
{
	irqreturn_t result = IRQ_NONE;
	struct sdhci_host *host = dev_id;
	u32 intmask, mask, unexpected = 0;
	int max_loops = 16;

	spin_lock(&host->lock);

	if (host->runtime_suspended && !sdhci_sdio_irq_enabled(host)) {
		spin_unlock(&host->lock);
		return IRQ_NONE;
	}

	intmask = sdhci_readl(host, SDHCI_INT_STATUS);
	if (!intmask || intmask == 0xffffffff) {
		result = IRQ_NONE;
		goto out;
	}

	do {
		DBG("IRQ status 0x%08x\n", intmask);

		if (host->ops->irq) {
			intmask = host->ops->irq(host, intmask);
			if (!intmask)
				goto cont;
		}

		/* Clear selected interrupts. */
		mask = intmask & (SDHCI_INT_CMD_MASK | SDHCI_INT_DATA_MASK |
				  SDHCI_INT_BUS_POWER);
		sdhci_writel(host, mask, SDHCI_INT_STATUS);

		if (intmask & (SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE)) {
			u32 present = sdhci_readl(host, SDHCI_PRESENT_STATE) &
				      SDHCI_CARD_PRESENT;

			/*
			 * There is a observation on i.mx esdhc.  INSERT
			 * bit will be immediately set again when it gets
			 * cleared, if a card is inserted.  We have to mask
			 * the irq to prevent interrupt storm which will
			 * freeze the system.  And the REMOVE gets the
			 * same situation.
			 *
			 * More testing are needed here to ensure it works
			 * for other platforms though.
			 */
			host->ier &= ~(SDHCI_INT_CARD_INSERT |
				       SDHCI_INT_CARD_REMOVE);
			host->ier |= present ? SDHCI_INT_CARD_REMOVE :
					       SDHCI_INT_CARD_INSERT;
			sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
			sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);

			sdhci_writel(host, intmask & (SDHCI_INT_CARD_INSERT |
				     SDHCI_INT_CARD_REMOVE), SDHCI_INT_STATUS);

			host->thread_isr |= intmask & (SDHCI_INT_CARD_INSERT |
						       SDHCI_INT_CARD_REMOVE);
			result = IRQ_WAKE_THREAD;
		}

		if (intmask & SDHCI_INT_CMD_MASK)
			sdhci_cmd_irq(host, intmask & SDHCI_INT_CMD_MASK);

		if (intmask & SDHCI_INT_DATA_MASK)
			sdhci_data_irq(host, intmask & SDHCI_INT_DATA_MASK);

		if (intmask & SDHCI_INT_BUS_POWER)
			pr_err("%s: Card is consuming too much power!\n",
				mmc_hostname(host->mmc));

		if (intmask & SDHCI_INT_RETUNE)
			mmc_retune_needed(host->mmc);

		if ((intmask & SDHCI_INT_CARD_INT) &&
		    (host->ier & SDHCI_INT_CARD_INT)) {
			sdhci_enable_sdio_irq_nolock(host, false);
			host->thread_isr |= SDHCI_INT_CARD_INT;
			result = IRQ_WAKE_THREAD;
		}

		intmask &= ~(SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE |
			     SDHCI_INT_CMD_MASK | SDHCI_INT_DATA_MASK |
			     SDHCI_INT_ERROR | SDHCI_INT_BUS_POWER |
			     SDHCI_INT_RETUNE | SDHCI_INT_CARD_INT);

		if (intmask) {
			unexpected |= intmask;
			sdhci_writel(host, intmask, SDHCI_INT_STATUS);
		}
cont:
		if (result == IRQ_NONE)
			result = IRQ_HANDLED;

		intmask = sdhci_readl(host, SDHCI_INT_STATUS);
	} while (intmask && --max_loops);
out:
	spin_unlock(&host->lock);

	if (unexpected) {
		pr_err("%s: Unexpected interrupt 0x%08x.\n",
			   mmc_hostname(host->mmc), unexpected);
		sdhci_dumpregs(host);
	}

	return result;
}

static irqreturn_t sdhci_thread_irq(int irq, void *dev_id)
{
	struct sdhci_host *host = dev_id;
	unsigned long flags;
	u32 isr;

	spin_lock_irqsave(&host->lock, flags);
	isr = host->thread_isr;
	host->thread_isr = 0;
	spin_unlock_irqrestore(&host->lock, flags);

	if (isr & (SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE)) {
		struct mmc_host *mmc = host->mmc;

		mmc->ops->card_event(mmc);
		mmc_detect_change(mmc, msecs_to_jiffies(200));
	}

	if (isr & SDHCI_INT_CARD_INT) {
		sdio_run_irqs(host->mmc);

		spin_lock_irqsave(&host->lock, flags);
		if (host->flags & SDHCI_SDIO_IRQ_ENABLED)
			sdhci_enable_sdio_irq_nolock(host, true);
		spin_unlock_irqrestore(&host->lock, flags);
	}

	return isr ? IRQ_HANDLED : IRQ_NONE;
}

/*****************************************************************************\
 *                                                                           *
 * Suspend/resume                                                            *
 *                                                                           *
\*****************************************************************************/

#ifdef CONFIG_PM
/*
 * To enable wakeup events, the corresponding events have to be enabled in
 * the Interrupt Status Enable register too. See 'Table 1-6: Wakeup Signal
 * Table' in the SD Host Controller Standard Specification.
 * It is useless to restore SDHCI_INT_ENABLE state in
 * sdhci_disable_irq_wakeups() since it will be set by
 * sdhci_enable_card_detection() or sdhci_init().
 */
void sdhci_enable_irq_wakeups(struct sdhci_host *host)
{
	u8 val;
	u8 mask = SDHCI_WAKE_ON_INSERT | SDHCI_WAKE_ON_REMOVE
			| SDHCI_WAKE_ON_INT;
	u32 irq_val = SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE |
		      SDHCI_INT_CARD_INT;

	val = sdhci_readb(host, SDHCI_WAKE_UP_CONTROL);
	val |= mask ;
	/* Avoid fake wake up */
	if (host->quirks & SDHCI_QUIRK_BROKEN_CARD_DETECTION) {
		val &= ~(SDHCI_WAKE_ON_INSERT | SDHCI_WAKE_ON_REMOVE);
		irq_val &= ~(SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE);
	}
	sdhci_writeb(host, val, SDHCI_WAKE_UP_CONTROL);
	sdhci_writel(host, irq_val, SDHCI_INT_ENABLE);
}
EXPORT_SYMBOL_GPL(sdhci_enable_irq_wakeups);

static void sdhci_disable_irq_wakeups(struct sdhci_host *host)
{
	u8 val;
	u8 mask = SDHCI_WAKE_ON_INSERT | SDHCI_WAKE_ON_REMOVE
			| SDHCI_WAKE_ON_INT;

	val = sdhci_readb(host, SDHCI_WAKE_UP_CONTROL);
	val &= ~mask;
	sdhci_writeb(host, val, SDHCI_WAKE_UP_CONTROL);
}

int sdhci_suspend_host(struct sdhci_host *host)
{
	sdhci_disable_card_detection(host);

	mmc_retune_timer_stop(host->mmc);

	if (!device_may_wakeup(mmc_dev(host->mmc))) {
		host->ier = 0;
		sdhci_writel(host, 0, SDHCI_INT_ENABLE);
		sdhci_writel(host, 0, SDHCI_SIGNAL_ENABLE);
		free_irq(host->irq, host);
	} else {
		sdhci_enable_irq_wakeups(host);
		enable_irq_wake(host->irq);
	}
	return 0;
}

EXPORT_SYMBOL_GPL(sdhci_suspend_host);

int sdhci_resume_host(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	int ret = 0;

	if (host->flags & (SDHCI_USE_SDMA | SDHCI_USE_ADMA)) {
		if (host->ops->enable_dma)
			host->ops->enable_dma(host);
	}

	if ((host->mmc->pm_flags & MMC_PM_KEEP_POWER) &&
	    (host->quirks2 & SDHCI_QUIRK2_HOST_OFF_CARD_ON)) {
		/* Card keeps power but host controller does not */
		sdhci_init(host, 0);
		host->pwr = 0;
		host->clock = 0;
		mmc->ops->set_ios(mmc, &mmc->ios);
	} else {
		sdhci_init(host, (host->mmc->pm_flags & MMC_PM_KEEP_POWER));
		mmiowb();
	}

	if (!device_may_wakeup(mmc_dev(host->mmc))) {
		ret = request_threaded_irq(host->irq, sdhci_irq,
					   sdhci_thread_irq, IRQF_SHARED,
					   mmc_hostname(host->mmc), host);
		if (ret)
			return ret;
	} else {
		sdhci_disable_irq_wakeups(host);
		disable_irq_wake(host->irq);
	}

	sdhci_enable_card_detection(host);

	return ret;
}

EXPORT_SYMBOL_GPL(sdhci_resume_host);

int sdhci_runtime_suspend_host(struct sdhci_host *host)
{
	unsigned long flags;

	mmc_retune_timer_stop(host->mmc);

	spin_lock_irqsave(&host->lock, flags);
	host->ier &= SDHCI_INT_CARD_INT;
	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
	spin_unlock_irqrestore(&host->lock, flags);

	synchronize_hardirq(host->irq);

	spin_lock_irqsave(&host->lock, flags);
	host->runtime_suspended = true;
	spin_unlock_irqrestore(&host->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(sdhci_runtime_suspend_host);

int sdhci_runtime_resume_host(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	unsigned long flags;
	int host_flags = host->flags;

	if (host_flags & (SDHCI_USE_SDMA | SDHCI_USE_ADMA)) {
		if (host->ops->enable_dma)
			host->ops->enable_dma(host);
	}

	sdhci_init(host, 0);

	if (mmc->ios.power_mode != MMC_POWER_UNDEFINED &&
	    mmc->ios.power_mode != MMC_POWER_OFF) {
		/* Force clock and power re-program */
		host->pwr = 0;
		host->clock = 0;
		mmc->ops->start_signal_voltage_switch(mmc, &mmc->ios);
		mmc->ops->set_ios(mmc, &mmc->ios);

		if ((host_flags & SDHCI_PV_ENABLED) &&
		    !(host->quirks2 & SDHCI_QUIRK2_PRESET_VALUE_BROKEN)) {
			spin_lock_irqsave(&host->lock, flags);
			sdhci_enable_preset_value(host, true);
			spin_unlock_irqrestore(&host->lock, flags);
		}

		if ((mmc->caps2 & MMC_CAP2_HS400_ES) &&
		    mmc->ops->hs400_enhanced_strobe)
			mmc->ops->hs400_enhanced_strobe(mmc, &mmc->ios);
	}

	spin_lock_irqsave(&host->lock, flags);

	host->runtime_suspended = false;

	/* Enable SDIO IRQ */
	if (host->flags & SDHCI_SDIO_IRQ_ENABLED)
		sdhci_enable_sdio_irq_nolock(host, true);

	/* Enable Card Detection */
	sdhci_enable_card_detection(host);

	spin_unlock_irqrestore(&host->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(sdhci_runtime_resume_host);

#endif /* CONFIG_PM */

/*****************************************************************************\
 *                                                                           *
 * Command Queue Engine (CQE) helpers                                        *
 *                                                                           *
\*****************************************************************************/

void sdhci_cqe_enable(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	unsigned long flags;
	u8 ctrl;

	spin_lock_irqsave(&host->lock, flags);

	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
	ctrl &= ~SDHCI_CTRL_DMA_MASK;
	if (host->flags & SDHCI_USE_64_BIT_DMA)
		ctrl |= SDHCI_CTRL_ADMA64;
	else
		ctrl |= SDHCI_CTRL_ADMA32;
	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);

	sdhci_writew(host, SDHCI_MAKE_BLKSZ(host->sdma_boundary, 512),
		     SDHCI_BLOCK_SIZE);

	/* Set maximum timeout */
	sdhci_writeb(host, 0xE, SDHCI_TIMEOUT_CONTROL);

	host->ier = host->cqe_ier;

	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);

	host->cqe_on = true;

	pr_debug("%s: sdhci: CQE on, IRQ mask %#x, IRQ status %#x\n",
		 mmc_hostname(mmc), host->ier,
		 sdhci_readl(host, SDHCI_INT_STATUS));

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}
EXPORT_SYMBOL_GPL(sdhci_cqe_enable);

void sdhci_cqe_disable(struct mmc_host *mmc, bool recovery)
{
	struct sdhci_host *host = mmc_priv(mmc);
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	sdhci_set_default_irqs(host);

	host->cqe_on = false;

	if (recovery) {
		sdhci_do_reset(host, SDHCI_RESET_CMD);
		sdhci_do_reset(host, SDHCI_RESET_DATA);
	}

	pr_debug("%s: sdhci: CQE off, IRQ mask %#x, IRQ status %#x\n",
		 mmc_hostname(mmc), host->ier,
		 sdhci_readl(host, SDHCI_INT_STATUS));

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}
EXPORT_SYMBOL_GPL(sdhci_cqe_disable);

bool sdhci_cqe_irq(struct sdhci_host *host, u32 intmask, int *cmd_error,
		   int *data_error)
{
	u32 mask;

	if (!host->cqe_on)
		return false;

	if (intmask & (SDHCI_INT_INDEX | SDHCI_INT_END_BIT | SDHCI_INT_CRC))
		*cmd_error = -EILSEQ;
	else if (intmask & SDHCI_INT_TIMEOUT)
		*cmd_error = -ETIMEDOUT;
	else
		*cmd_error = 0;

	if (intmask & (SDHCI_INT_DATA_END_BIT | SDHCI_INT_DATA_CRC))
		*data_error = -EILSEQ;
	else if (intmask & SDHCI_INT_DATA_TIMEOUT)
		*data_error = -ETIMEDOUT;
	else if (intmask & SDHCI_INT_ADMA_ERROR)
		*data_error = -EIO;
	else
		*data_error = 0;

	/* Clear selected interrupts. */
	mask = intmask & host->cqe_ier;
	sdhci_writel(host, mask, SDHCI_INT_STATUS);

	if (intmask & SDHCI_INT_BUS_POWER)
		pr_err("%s: Card is consuming too much power!\n",
		       mmc_hostname(host->mmc));

	intmask &= ~(host->cqe_ier | SDHCI_INT_ERROR);
	if (intmask) {
		sdhci_writel(host, intmask, SDHCI_INT_STATUS);
		pr_err("%s: CQE: Unexpected interrupt 0x%08x.\n",
		       mmc_hostname(host->mmc), intmask);
		sdhci_dumpregs(host);
	}

	return true;
}
EXPORT_SYMBOL_GPL(sdhci_cqe_irq);

/*****************************************************************************\
 *                                                                           *
 * Device allocation/registration                                            *
 *                                                                           *
\*****************************************************************************/

struct sdhci_host *sdhci_alloc_host(struct device *dev,
	size_t priv_size)
{
	struct mmc_host *mmc;
	struct sdhci_host *host;

	WARN_ON(dev == NULL);

	mmc = mmc_alloc_host(sizeof(struct sdhci_host) + priv_size, dev);
	if (!mmc)
		return ERR_PTR(-ENOMEM);

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->mmc_host_ops = sdhci_ops;
	mmc->ops = &host->mmc_host_ops;

	host->flags = SDHCI_SIGNALING_330;

	host->cqe_ier     = SDHCI_CQE_INT_MASK;
	host->cqe_err_ier = SDHCI_CQE_INT_ERR_MASK;

	host->tuning_delay = -1;

	host->sdma_boundary = SDHCI_DEFAULT_BOUNDARY_ARG;

	return host;
}

EXPORT_SYMBOL_GPL(sdhci_alloc_host);

static int sdhci_set_dma_mask(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	struct device *dev = mmc_dev(mmc);
	int ret = -EINVAL;

	if (host->quirks2 & SDHCI_QUIRK2_BROKEN_64_BIT_DMA)
		host->flags &= ~SDHCI_USE_64_BIT_DMA;

	/* Try 64-bit mask if hardware is capable  of it */
	if (host->flags & SDHCI_USE_64_BIT_DMA) {
		ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
		if (ret) {
			pr_warn("%s: Failed to set 64-bit DMA mask.\n",
				mmc_hostname(mmc));
			host->flags &= ~SDHCI_USE_64_BIT_DMA;
		}
	}

	/* 32-bit mask as default & fallback */
	if (ret) {
		ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret)
			pr_warn("%s: Failed to set 32-bit DMA mask.\n",
				mmc_hostname(mmc));
	}

	return ret;
}

void __sdhci_read_caps(struct sdhci_host *host, u16 *ver, u32 *caps, u32 *caps1)
{
	u16 v;
	u64 dt_caps_mask = 0;
	u64 dt_caps = 0;

	if (host->read_caps)
		return;

	host->read_caps = true;

	if (debug_quirks)
		host->quirks = debug_quirks;

	if (debug_quirks2)
		host->quirks2 = debug_quirks2;

	sdhci_do_reset(host, SDHCI_RESET_ALL);

	of_property_read_u64(mmc_dev(host->mmc)->of_node,
			     "sdhci-caps-mask", &dt_caps_mask);
	of_property_read_u64(mmc_dev(host->mmc)->of_node,
			     "sdhci-caps", &dt_caps);

	v = ver ? *ver : sdhci_readw(host, SDHCI_HOST_VERSION);
	host->version = (v & SDHCI_SPEC_VER_MASK) >> SDHCI_SPEC_VER_SHIFT;

	if (host->quirks & SDHCI_QUIRK_MISSING_CAPS)
		return;

	if (caps) {
		host->caps = *caps;
	} else {
		host->caps = sdhci_readl(host, SDHCI_CAPABILITIES);
		host->caps &= ~lower_32_bits(dt_caps_mask);
		host->caps |= lower_32_bits(dt_caps);
	}

	if (host->version < SDHCI_SPEC_300)
		return;

	if (caps1) {
		host->caps1 = *caps1;
	} else {
		host->caps1 = sdhci_readl(host, SDHCI_CAPABILITIES_1);
		host->caps1 &= ~upper_32_bits(dt_caps_mask);
		host->caps1 |= upper_32_bits(dt_caps);
	}
}
EXPORT_SYMBOL_GPL(__sdhci_read_caps);

int sdhci_setup_host(struct sdhci_host *host)
{
	struct mmc_host *mmc;
	u32 max_current_caps;
	unsigned int ocr_avail;
	unsigned int override_timeout_clk;
	u32 max_clk;
	int ret;

	WARN_ON(host == NULL);
	if (host == NULL)
		return -EINVAL;

	mmc = host->mmc;

	/*
	 * If there are external regulators, get them. Note this must be done
	 * early before resetting the host and reading the capabilities so that
	 * the host can take the appropriate action if regulators are not
	 * available.
	 */
	ret = mmc_regulator_get_supply(mmc);
	if (ret == -EPROBE_DEFER)
		return ret;

	DBG("Version:   0x%08x | Present:  0x%08x\n",
	    sdhci_readw(host, SDHCI_HOST_VERSION),
	    sdhci_readl(host, SDHCI_PRESENT_STATE));
	DBG("Caps:      0x%08x | Caps_1:   0x%08x\n",
	    sdhci_readl(host, SDHCI_CAPABILITIES),
	    sdhci_readl(host, SDHCI_CAPABILITIES_1));

	sdhci_read_caps(host);

	override_timeout_clk = host->timeout_clk;

	if (host->version > SDHCI_SPEC_300) {
		pr_err("%s: Unknown controller version (%d). You may experience problems.\n",
		       mmc_hostname(mmc), host->version);
	}

	if (host->quirks & SDHCI_QUIRK_FORCE_DMA)
		host->flags |= SDHCI_USE_SDMA;
	else if (!(host->caps & SDHCI_CAN_DO_SDMA))
		DBG("Controller doesn't have SDMA capability\n");
	else
		host->flags |= SDHCI_USE_SDMA;

	if ((host->quirks & SDHCI_QUIRK_BROKEN_DMA) &&
		(host->flags & SDHCI_USE_SDMA)) {
		DBG("Disabling DMA as it is marked broken\n");
		host->flags &= ~SDHCI_USE_SDMA;
	}

	if ((host->version >= SDHCI_SPEC_200) &&
		(host->caps & SDHCI_CAN_DO_ADMA2))
		host->flags |= SDHCI_USE_ADMA;

	if ((host->quirks & SDHCI_QUIRK_BROKEN_ADMA) &&
		(host->flags & SDHCI_USE_ADMA)) {
		DBG("Disabling ADMA as it is marked broken\n");
		host->flags &= ~SDHCI_USE_ADMA;
	}

	/*
	 * It is assumed that a 64-bit capable device has set a 64-bit DMA mask
	 * and *must* do 64-bit DMA.  A driver has the opportunity to change
	 * that during the first call to ->enable_dma().  Similarly
	 * SDHCI_QUIRK2_BROKEN_64_BIT_DMA must be left to the drivers to
	 * implement.
	 */
	if (host->caps & SDHCI_CAN_64BIT)
		host->flags |= SDHCI_USE_64_BIT_DMA;

	if (host->flags & (SDHCI_USE_SDMA | SDHCI_USE_ADMA)) {
		ret = sdhci_set_dma_mask(host);

		if (!ret && host->ops->enable_dma)
			ret = host->ops->enable_dma(host);

		if (ret) {
			pr_warn("%s: No suitable DMA available - falling back to PIO\n",
				mmc_hostname(mmc));
			host->flags &= ~(SDHCI_USE_SDMA | SDHCI_USE_ADMA);

			ret = 0;
		}
	}

	/* SDMA does not support 64-bit DMA */
	if (host->flags & SDHCI_USE_64_BIT_DMA)
		host->flags &= ~SDHCI_USE_SDMA;

	if (host->flags & SDHCI_USE_ADMA) {
		dma_addr_t dma;
		void *buf;

		/*
		 * The DMA descriptor table size is calculated as the maximum
		 * number of segments times 2, to allow for an alignment
		 * descriptor for each segment, plus 1 for a nop end descriptor,
		 * all multipled by the descriptor size.
		 */
		if (host->flags & SDHCI_USE_64_BIT_DMA) {
			host->adma_table_sz = (SDHCI_MAX_SEGS * 2 + 1) *
					      SDHCI_ADMA2_64_DESC_SZ;
			host->desc_sz = SDHCI_ADMA2_64_DESC_SZ;
		} else {
			host->adma_table_sz = (SDHCI_MAX_SEGS * 2 + 1) *
					      SDHCI_ADMA2_32_DESC_SZ;
			host->desc_sz = SDHCI_ADMA2_32_DESC_SZ;
		}

		host->align_buffer_sz = SDHCI_MAX_SEGS * SDHCI_ADMA2_ALIGN;
		buf = dma_alloc_coherent(mmc_dev(mmc), host->align_buffer_sz +
					 host->adma_table_sz, &dma, GFP_KERNEL);
		if (!buf) {
			pr_warn("%s: Unable to allocate ADMA buffers - falling back to standard DMA\n",
				mmc_hostname(mmc));
			host->flags &= ~SDHCI_USE_ADMA;
		} else if ((dma + host->align_buffer_sz) &
			   (SDHCI_ADMA2_DESC_ALIGN - 1)) {
			pr_warn("%s: unable to allocate aligned ADMA descriptor\n",
				mmc_hostname(mmc));
			host->flags &= ~SDHCI_USE_ADMA;
			dma_free_coherent(mmc_dev(mmc), host->align_buffer_sz +
					  host->adma_table_sz, buf, dma);
		} else {
			host->align_buffer = buf;
			host->align_addr = dma;

			host->adma_table = buf + host->align_buffer_sz;
			host->adma_addr = dma + host->align_buffer_sz;
		}
	}

	/*
	 * If we use DMA, then it's up to the caller to set the DMA
	 * mask, but PIO does not need the hw shim so we set a new
	 * mask here in that case.
	 */
	if (!(host->flags & (SDHCI_USE_SDMA | SDHCI_USE_ADMA))) {
		host->dma_mask = DMA_BIT_MASK(64);
		mmc_dev(mmc)->dma_mask = &host->dma_mask;
	}

	if (host->version >= SDHCI_SPEC_300)
		host->max_clk = (host->caps & SDHCI_CLOCK_V3_BASE_MASK)
			>> SDHCI_CLOCK_BASE_SHIFT;
	else
		host->max_clk = (host->caps & SDHCI_CLOCK_BASE_MASK)
			>> SDHCI_CLOCK_BASE_SHIFT;

	host->max_clk *= 1000000;
	if (host->max_clk == 0 || host->quirks &
			SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN) {
		if (!host->ops->get_max_clock) {
			pr_err("%s: Hardware doesn't specify base clock frequency.\n",
			       mmc_hostname(mmc));
			ret = -ENODEV;
			goto undma;
		}
		host->max_clk = host->ops->get_max_clock(host);
	}

	/*
	 * In case of Host Controller v3.00, find out whether clock
	 * multiplier is supported.
	 */
	host->clk_mul = (host->caps1 & SDHCI_CLOCK_MUL_MASK) >>
			SDHCI_CLOCK_MUL_SHIFT;

	/*
	 * In case the value in Clock Multiplier is 0, then programmable
	 * clock mode is not supported, otherwise the actual clock
	 * multiplier is one more than the value of Clock Multiplier
	 * in the Capabilities Register.
	 */
	if (host->clk_mul)
		host->clk_mul += 1;

	/*
	 * Set host parameters.
	 */
	max_clk = host->max_clk;

	if (host->ops->get_min_clock)
		mmc->f_min = host->ops->get_min_clock(host);
	else if (host->version >= SDHCI_SPEC_300) {
		if (host->clk_mul) {
			mmc->f_min = (host->max_clk * host->clk_mul) / 1024;
			max_clk = host->max_clk * host->clk_mul;
		} else
			mmc->f_min = host->max_clk / SDHCI_MAX_DIV_SPEC_300;
	} else
		mmc->f_min = host->max_clk / SDHCI_MAX_DIV_SPEC_200;

	if (!mmc->f_max || mmc->f_max > max_clk)
		mmc->f_max = max_clk;

	if (!(host->quirks & SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK)) {
		host->timeout_clk = (host->caps & SDHCI_TIMEOUT_CLK_MASK) >>
					SDHCI_TIMEOUT_CLK_SHIFT;

		if (host->caps & SDHCI_TIMEOUT_CLK_UNIT)
			host->timeout_clk *= 1000;

		if (host->timeout_clk == 0) {
			if (!host->ops->get_timeout_clock) {
				pr_err("%s: Hardware doesn't specify timeout clock frequency.\n",
					mmc_hostname(mmc));
				ret = -ENODEV;
				goto undma;
			}

			host->timeout_clk =
				DIV_ROUND_UP(host->ops->get_timeout_clock(host),
					     1000);
		}

		if (override_timeout_clk)
			host->timeout_clk = override_timeout_clk;

		mmc->max_busy_timeout = host->ops->get_max_timeout_count ?
			host->ops->get_max_timeout_count(host) : 1 << 27;
		mmc->max_busy_timeout /= host->timeout_clk;
	}

	mmc->caps |= MMC_CAP_SDIO_IRQ | MMC_CAP_ERASE | MMC_CAP_CMD23;
	mmc->caps2 |= MMC_CAP2_SDIO_IRQ_NOTHREAD;

	if (host->quirks & SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12)
		host->flags |= SDHCI_AUTO_CMD12;

	/* Auto-CMD23 stuff only works in ADMA or PIO. */
	if ((host->version >= SDHCI_SPEC_300) &&
	    ((host->flags & SDHCI_USE_ADMA) ||
	     !(host->flags & SDHCI_USE_SDMA)) &&
	     !(host->quirks2 & SDHCI_QUIRK2_ACMD23_BROKEN)) {
		host->flags |= SDHCI_AUTO_CMD23;
		DBG("Auto-CMD23 available\n");
	} else {
		DBG("Auto-CMD23 unavailable\n");
	}

	/*
	 * A controller may support 8-bit width, but the board itself
	 * might not have the pins brought out.  Boards that support
	 * 8-bit width must set "mmc->caps |= MMC_CAP_8_BIT_DATA;" in
	 * their platform code before calling sdhci_add_host(), and we
	 * won't assume 8-bit width for hosts without that CAP.
	 */
	if (!(host->quirks & SDHCI_QUIRK_FORCE_1_BIT_DATA))
		mmc->caps |= MMC_CAP_4_BIT_DATA;

	if (host->quirks2 & SDHCI_QUIRK2_HOST_NO_CMD23)
		mmc->caps &= ~MMC_CAP_CMD23;

	if (host->caps & SDHCI_CAN_DO_HISPD)
		mmc->caps |= MMC_CAP_SD_HIGHSPEED | MMC_CAP_MMC_HIGHSPEED;

	if ((host->quirks & SDHCI_QUIRK_BROKEN_CARD_DETECTION) &&
	    mmc_card_is_removable(mmc) &&
	    mmc_gpio_get_cd(host->mmc) < 0)
		mmc->caps |= MMC_CAP_NEEDS_POLL;

	/* If vqmmc regulator and no 1.8V signalling, then there's no UHS */
	if (!IS_ERR(mmc->supply.vqmmc)) {
		ret = regulator_enable(mmc->supply.vqmmc);
		if (!regulator_is_supported_voltage(mmc->supply.vqmmc, 1700000,
						    1950000))
			host->caps1 &= ~(SDHCI_SUPPORT_SDR104 |
					 SDHCI_SUPPORT_SDR50 |
					 SDHCI_SUPPORT_DDR50);
		if (ret) {
			pr_warn("%s: Failed to enable vqmmc regulator: %d\n",
				mmc_hostname(mmc), ret);
			mmc->supply.vqmmc = ERR_PTR(-EINVAL);
		}
	}

	if (host->quirks2 & SDHCI_QUIRK2_NO_1_8_V) {
		host->caps1 &= ~(SDHCI_SUPPORT_SDR104 | SDHCI_SUPPORT_SDR50 |
				 SDHCI_SUPPORT_DDR50);
	}

	/* Any UHS-I mode in caps implies SDR12 and SDR25 support. */
	if (host->caps1 & (SDHCI_SUPPORT_SDR104 | SDHCI_SUPPORT_SDR50 |
			   SDHCI_SUPPORT_DDR50))
		mmc->caps |= MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25;

	/* SDR104 supports also implies SDR50 support */
	if (host->caps1 & SDHCI_SUPPORT_SDR104) {
		mmc->caps |= MMC_CAP_UHS_SDR104 | MMC_CAP_UHS_SDR50;
		/* SD3.0: SDR104 is supported so (for eMMC) the caps2
		 * field can be promoted to support HS200.
		 */
		if (!(host->quirks2 & SDHCI_QUIRK2_BROKEN_HS200))
			mmc->caps2 |= MMC_CAP2_HS200;
	} else if (host->caps1 & SDHCI_SUPPORT_SDR50) {
		mmc->caps |= MMC_CAP_UHS_SDR50;
	}

	if (host->quirks2 & SDHCI_QUIRK2_CAPS_BIT63_FOR_HS400 &&
	    (host->caps1 & SDHCI_SUPPORT_HS400))
		mmc->caps2 |= MMC_CAP2_HS400;

	if ((mmc->caps2 & MMC_CAP2_HSX00_1_2V) &&
	    (IS_ERR(mmc->supply.vqmmc) ||
	     !regulator_is_supported_voltage(mmc->supply.vqmmc, 1100000,
					     1300000)))
		mmc->caps2 &= ~MMC_CAP2_HSX00_1_2V;

	if ((host->caps1 & SDHCI_SUPPORT_DDR50) &&
	    !(host->quirks2 & SDHCI_QUIRK2_BROKEN_DDR50))
		mmc->caps |= MMC_CAP_UHS_DDR50;

	/* Does the host need tuning for SDR50? */
	if (host->caps1 & SDHCI_USE_SDR50_TUNING)
		host->flags |= SDHCI_SDR50_NEEDS_TUNING;

	/* Driver Type(s) (A, C, D) supported by the host */
	if (host->caps1 & SDHCI_DRIVER_TYPE_A)
		mmc->caps |= MMC_CAP_DRIVER_TYPE_A;
	if (host->caps1 & SDHCI_DRIVER_TYPE_C)
		mmc->caps |= MMC_CAP_DRIVER_TYPE_C;
	if (host->caps1 & SDHCI_DRIVER_TYPE_D)
		mmc->caps |= MMC_CAP_DRIVER_TYPE_D;

	/* Initial value for re-tuning timer count */
	host->tuning_count = (host->caps1 & SDHCI_RETUNING_TIMER_COUNT_MASK) >>
			     SDHCI_RETUNING_TIMER_COUNT_SHIFT;

	/*
	 * In case Re-tuning Timer is not disabled, the actual value of
	 * re-tuning timer will be 2 ^ (n - 1).
	 */
	if (host->tuning_count)
		host->tuning_count = 1 << (host->tuning_count - 1);

	/* Re-tuning mode supported by the Host Controller */
	host->tuning_mode = (host->caps1 & SDHCI_RETUNING_MODE_MASK) >>
			     SDHCI_RETUNING_MODE_SHIFT;

	ocr_avail = 0;

	/*
	 * According to SD Host Controller spec v3.00, if the Host System
	 * can afford more than 150mA, Host Driver should set XPC to 1. Also
	 * the value is meaningful only if Voltage Support in the Capabilities
	 * register is set. The actual current value is 4 times the register
	 * value.
	 */
	max_current_caps = sdhci_readl(host, SDHCI_MAX_CURRENT);
	if (!max_current_caps && !IS_ERR(mmc->supply.vmmc)) {
		int curr = regulator_get_current_limit(mmc->supply.vmmc);
		if (curr > 0) {

			/* convert to SDHCI_MAX_CURRENT format */
			curr = curr/1000;  /* convert to mA */
			curr = curr/SDHCI_MAX_CURRENT_MULTIPLIER;

			curr = min_t(u32, curr, SDHCI_MAX_CURRENT_LIMIT);
			max_current_caps =
				(curr << SDHCI_MAX_CURRENT_330_SHIFT) |
				(curr << SDHCI_MAX_CURRENT_300_SHIFT) |
				(curr << SDHCI_MAX_CURRENT_180_SHIFT);
		}
	}

	if (host->caps & SDHCI_CAN_VDD_330) {
		ocr_avail |= MMC_VDD_32_33 | MMC_VDD_33_34;

		mmc->max_current_330 = ((max_current_caps &
				   SDHCI_MAX_CURRENT_330_MASK) >>
				   SDHCI_MAX_CURRENT_330_SHIFT) *
				   SDHCI_MAX_CURRENT_MULTIPLIER;
	}
	if (host->caps & SDHCI_CAN_VDD_300) {
		ocr_avail |= MMC_VDD_29_30 | MMC_VDD_30_31;

		mmc->max_current_300 = ((max_current_caps &
				   SDHCI_MAX_CURRENT_300_MASK) >>
				   SDHCI_MAX_CURRENT_300_SHIFT) *
				   SDHCI_MAX_CURRENT_MULTIPLIER;
	}
	if (host->caps & SDHCI_CAN_VDD_180) {
		ocr_avail |= MMC_VDD_165_195;

		mmc->max_current_180 = ((max_current_caps &
				   SDHCI_MAX_CURRENT_180_MASK) >>
				   SDHCI_MAX_CURRENT_180_SHIFT) *
				   SDHCI_MAX_CURRENT_MULTIPLIER;
	}

	/* If OCR set by host, use it instead. */
	if (host->ocr_mask)
		ocr_avail = host->ocr_mask;

	/* If OCR set by external regulators, give it highest prio. */
	if (mmc->ocr_avail)
		ocr_avail = mmc->ocr_avail;

	mmc->ocr_avail = ocr_avail;
	mmc->ocr_avail_sdio = ocr_avail;
	if (host->ocr_avail_sdio)
		mmc->ocr_avail_sdio &= host->ocr_avail_sdio;
	mmc->ocr_avail_sd = ocr_avail;
	if (host->ocr_avail_sd)
		mmc->ocr_avail_sd &= host->ocr_avail_sd;
	else /* normal SD controllers don't support 1.8V */
		mmc->ocr_avail_sd &= ~MMC_VDD_165_195;
	mmc->ocr_avail_mmc = ocr_avail;
	if (host->ocr_avail_mmc)
		mmc->ocr_avail_mmc &= host->ocr_avail_mmc;

	if (mmc->ocr_avail == 0) {
		pr_err("%s: Hardware doesn't report any support voltages.\n",
		       mmc_hostname(mmc));
		ret = -ENODEV;
		goto unreg;
	}

	if ((mmc->caps & (MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
			  MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR104 |
			  MMC_CAP_UHS_DDR50 | MMC_CAP_1_8V_DDR)) ||
	    (mmc->caps2 & (MMC_CAP2_HS200_1_8V_SDR | MMC_CAP2_HS400_1_8V)))
		host->flags |= SDHCI_SIGNALING_180;

	if (mmc->caps2 & MMC_CAP2_HSX00_1_2V)
		host->flags |= SDHCI_SIGNALING_120;

	spin_lock_init(&host->lock);

	/*
	 * Maximum number of segments. Depends on if the hardware
	 * can do scatter/gather or not.
	 */
	if (host->flags & SDHCI_USE_ADMA)
		mmc->max_segs = SDHCI_MAX_SEGS;
	else if (host->flags & SDHCI_USE_SDMA)
		mmc->max_segs = 1;
	else /* PIO */
		mmc->max_segs = SDHCI_MAX_SEGS;

	/*
	 * Maximum number of sectors in one transfer. Limited by SDMA boundary
	 * size (512KiB). Note some tuning modes impose a 4MiB limit, but this
	 * is less anyway.
	 */
	mmc->max_req_size = 524288;

	/*
	 * Maximum segment size. Could be one segment with the maximum number
	 * of bytes. When doing hardware scatter/gather, each entry cannot
	 * be larger than 64 KiB though.
	 */
	if (host->flags & SDHCI_USE_ADMA) {
		if (host->quirks & SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC)
			mmc->max_seg_size = 65535;
		else
			mmc->max_seg_size = 65536;
	} else {
		mmc->max_seg_size = mmc->max_req_size;
	}

	/*
	 * Maximum block size. This varies from controller to controller and
	 * is specified in the capabilities register.
	 */
	if (host->quirks & SDHCI_QUIRK_FORCE_BLK_SZ_2048) {
		mmc->max_blk_size = 2;
	} else {
		mmc->max_blk_size = (host->caps & SDHCI_MAX_BLOCK_MASK) >>
				SDHCI_MAX_BLOCK_SHIFT;
		if (mmc->max_blk_size >= 3) {
			pr_warn("%s: Invalid maximum block size, assuming 512 bytes\n",
				mmc_hostname(mmc));
			mmc->max_blk_size = 0;
		}
	}

	mmc->max_blk_size = 512 << mmc->max_blk_size;

	/*
	 * Maximum block count.
	 */
	mmc->max_blk_count = (host->quirks & SDHCI_QUIRK_NO_MULTIBLOCK) ? 1 : 65535;

	return 0;

unreg:
	if (!IS_ERR(mmc->supply.vqmmc))
		regulator_disable(mmc->supply.vqmmc);
undma:
	if (host->align_buffer)
		dma_free_coherent(mmc_dev(mmc), host->align_buffer_sz +
				  host->adma_table_sz, host->align_buffer,
				  host->align_addr);
	host->adma_table = NULL;
	host->align_buffer = NULL;

	return ret;
}
EXPORT_SYMBOL_GPL(sdhci_setup_host);

void sdhci_cleanup_host(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;

	if (!IS_ERR(mmc->supply.vqmmc))
		regulator_disable(mmc->supply.vqmmc);

	if (host->align_buffer)
		dma_free_coherent(mmc_dev(mmc), host->align_buffer_sz +
				  host->adma_table_sz, host->align_buffer,
				  host->align_addr);
	host->adma_table = NULL;
	host->align_buffer = NULL;
}
EXPORT_SYMBOL_GPL(sdhci_cleanup_host);

int __sdhci_add_host(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	int ret;

	/*
	 * Init tasklets.
	 */
	tasklet_init(&host->finish_tasklet,
		sdhci_tasklet_finish, (unsigned long)host);

	setup_timer(&host->timer, sdhci_timeout_timer, (unsigned long)host);
	setup_timer(&host->data_timer, sdhci_timeout_data_timer,
		    (unsigned long)host);

	init_waitqueue_head(&host->buf_ready_int);

	sdhci_init(host, 0);

	ret = request_threaded_irq(host->irq, sdhci_irq, sdhci_thread_irq,
				   IRQF_SHARED,	mmc_hostname(mmc), host);
	if (ret) {
		pr_err("%s: Failed to request IRQ %d: %d\n",
		       mmc_hostname(mmc), host->irq, ret);
		goto untasklet;
	}

	ret = sdhci_led_register(host);
	if (ret) {
		pr_err("%s: Failed to register LED device: %d\n",
		       mmc_hostname(mmc), ret);
		goto unirq;
	}

	mmiowb();

	ret = mmc_add_host(mmc);
	if (ret)
		goto unled;

	pr_info("%s: SDHCI controller on %s [%s] using %s\n",
		mmc_hostname(mmc), host->hw_name, dev_name(mmc_dev(mmc)),
		(host->flags & SDHCI_USE_ADMA) ?
		(host->flags & SDHCI_USE_64_BIT_DMA) ? "ADMA 64-bit" : "ADMA" :
		(host->flags & SDHCI_USE_SDMA) ? "DMA" : "PIO");

	sdhci_enable_card_detection(host);

	return 0;

unled:
	sdhci_led_unregister(host);
unirq:
	sdhci_do_reset(host, SDHCI_RESET_ALL);
	sdhci_writel(host, 0, SDHCI_INT_ENABLE);
	sdhci_writel(host, 0, SDHCI_SIGNAL_ENABLE);
	free_irq(host->irq, host);
untasklet:
	tasklet_kill(&host->finish_tasklet);

	return ret;
}
EXPORT_SYMBOL_GPL(__sdhci_add_host);

int sdhci_add_host(struct sdhci_host *host)
{
	int ret;

	ret = sdhci_setup_host(host);
	if (ret)
		return ret;

	ret = __sdhci_add_host(host);
	if (ret)
		goto cleanup;

	return 0;

cleanup:
	sdhci_cleanup_host(host);

	return ret;
}
EXPORT_SYMBOL_GPL(sdhci_add_host);

void sdhci_remove_host(struct sdhci_host *host, int dead)
{
	struct mmc_host *mmc = host->mmc;
	unsigned long flags;

	if (dead) {
		spin_lock_irqsave(&host->lock, flags);

		host->flags |= SDHCI_DEVICE_DEAD;

		if (sdhci_has_requests(host)) {
			pr_err("%s: Controller removed during "
				" transfer!\n", mmc_hostname(mmc));
			sdhci_error_out_mrqs(host, -ENOMEDIUM);
		}

		spin_unlock_irqrestore(&host->lock, flags);
	}

	sdhci_disable_card_detection(host);

	mmc_remove_host(mmc);

	sdhci_led_unregister(host);

	if (!dead)
		sdhci_do_reset(host, SDHCI_RESET_ALL);

	sdhci_writel(host, 0, SDHCI_INT_ENABLE);
	sdhci_writel(host, 0, SDHCI_SIGNAL_ENABLE);
	free_irq(host->irq, host);

	del_timer_sync(&host->timer);
	del_timer_sync(&host->data_timer);

	tasklet_kill(&host->finish_tasklet);

	if (!IS_ERR(mmc->supply.vqmmc))
		regulator_disable(mmc->supply.vqmmc);

	if (host->align_buffer)
		dma_free_coherent(mmc_dev(mmc), host->align_buffer_sz +
				  host->adma_table_sz, host->align_buffer,
				  host->align_addr);

	host->adma_table = NULL;
	host->align_buffer = NULL;
}

EXPORT_SYMBOL_GPL(sdhci_remove_host);

void sdhci_free_host(struct sdhci_host *host)
{
	mmc_free_host(host->mmc);
}

EXPORT_SYMBOL_GPL(sdhci_free_host);

/*****************************************************************************\
 *                                                                           *
 * Driver init/exit                                                          *
 *                                                                           *
\*****************************************************************************/

static int __init sdhci_drv_init(void)
{
	pr_info(DRIVER_NAME
		": Secure Digital Host Controller Interface driver\n");
	pr_info(DRIVER_NAME ": Copyright(c) Pierre Ossman\n");

	return 0;
}

static void __exit sdhci_drv_exit(void)
{
}

module_init(sdhci_drv_init);
module_exit(sdhci_drv_exit);

module_param(debug_quirks, uint, 0444);
module_param(debug_quirks2, uint, 0444);

MODULE_AUTHOR("Pierre Ossman <pierre@ossman.eu>");
MODULE_DESCRIPTION("Secure Digital Host Controller Interface core driver");
MODULE_LICENSE("GPL");

MODULE_PARM_DESC(debug_quirks, "Force certain quirks.");
MODULE_PARM_DESC(debug_quirks2, "Force certain other quirks.");
