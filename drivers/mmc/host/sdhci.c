// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  linux/drivers/mmc/host/sdhci.c - Secure Digital Host Controller Interface driver
 *
 *  Copyright (C) 2005-2008 Pierre Ossman, All Rights Reserved.
 *
 * Thanks to the following companies for their support:
 *
 *     - JMicron (hardware and technical support)
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/ktime.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/sizes.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/bug.h>
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

static bool sdhci_send_command(struct sdhci_host *host, struct mmc_command *cmd);

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
	SDHCI_DUMP("ACmd stat: 0x%08x | Slot int: 0x%08x\n",
		   sdhci_readw(host, SDHCI_AUTO_CMD_STATUS),
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

	if (host->ops->dump_uhs2_regs)
		host->ops->dump_uhs2_regs(host);

	if (host->ops->dump_vendor_regs)
		host->ops->dump_vendor_regs(host);

	SDHCI_DUMP("============================================\n");
}
EXPORT_SYMBOL_GPL(sdhci_dumpregs);

/*****************************************************************************\
 *                                                                           *
 * Low level functions                                                       *
 *                                                                           *
\*****************************************************************************/

static void sdhci_do_enable_v4_mode(struct sdhci_host *host)
{
	u16 ctrl2;

	ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	if (ctrl2 & SDHCI_CTRL_V4_MODE)
		return;

	ctrl2 |= SDHCI_CTRL_V4_MODE;
	sdhci_writew(host, ctrl2, SDHCI_HOST_CONTROL2);
}

/*
 * This can be called before sdhci_add_host() by Vendor's host controller
 * driver to enable v4 mode if supported.
 */
void sdhci_enable_v4_mode(struct sdhci_host *host)
{
	host->v4_mode = true;
	sdhci_do_enable_v4_mode(host);
}
EXPORT_SYMBOL_GPL(sdhci_enable_v4_mode);

bool sdhci_data_line_cmd(struct mmc_command *cmd)
{
	return cmd->data || cmd->flags & MMC_RSP_BUSY;
}
EXPORT_SYMBOL_GPL(sdhci_data_line_cmd);

static void sdhci_set_card_detection(struct sdhci_host *host, bool enable)
{
	u32 present;

	if ((host->quirks & SDHCI_QUIRK_BROKEN_CARD_DETECTION) ||
	    !mmc_card_is_removable(host->mmc) || mmc_host_can_gpio_cd(host->mmc))
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
	pm_runtime_get_noresume(mmc_dev(host->mmc));
}

static void sdhci_runtime_pm_bus_off(struct sdhci_host *host)
{
	if (!host->bus_on)
		return;
	host->bus_on = false;
	pm_runtime_put_noidle(mmc_dev(host->mmc));
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
	while (1) {
		bool timedout = ktime_after(ktime_get(), timeout);

		if (!(sdhci_readb(host, SDHCI_SOFTWARE_RESET) & mask))
			break;
		if (timedout) {
			pr_err("%s: Reset 0x%x never completed.\n",
				mmc_hostname(host->mmc), (int)mask);
			sdhci_err_stats_inc(host, CTRL_TIMEOUT);
			sdhci_dumpregs(host);
			return;
		}
		udelay(10);
	}
}
EXPORT_SYMBOL_GPL(sdhci_reset);

bool sdhci_do_reset(struct sdhci_host *host, u8 mask)
{
	if (host->quirks & SDHCI_QUIRK_NO_CARD_NO_RESET) {
		struct mmc_host *mmc = host->mmc;

		if (!mmc->ops->get_cd(mmc))
			return false;
	}

	host->ops->reset(host, mask);

	return true;
}
EXPORT_SYMBOL_GPL(sdhci_do_reset);

static void sdhci_reset_for_all(struct sdhci_host *host)
{
	if (sdhci_do_reset(host, SDHCI_RESET_ALL)) {
		if (host->flags & (SDHCI_USE_SDMA | SDHCI_USE_ADMA)) {
			if (host->ops->enable_dma)
				host->ops->enable_dma(host);
		}
		/* Resetting the controller clears many */
		host->preset_enabled = false;
	}
}

enum sdhci_reset_reason {
	SDHCI_RESET_FOR_INIT,
	SDHCI_RESET_FOR_REQUEST_ERROR,
	SDHCI_RESET_FOR_REQUEST_ERROR_DATA_ONLY,
	SDHCI_RESET_FOR_TUNING_ABORT,
	SDHCI_RESET_FOR_CARD_REMOVED,
	SDHCI_RESET_FOR_CQE_RECOVERY,
};

static void sdhci_reset_for_reason(struct sdhci_host *host, enum sdhci_reset_reason reason)
{
	if (host->quirks2 & SDHCI_QUIRK2_ISSUE_CMD_DAT_RESET_TOGETHER) {
		sdhci_do_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
		return;
	}

	switch (reason) {
	case SDHCI_RESET_FOR_INIT:
		sdhci_do_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
		break;
	case SDHCI_RESET_FOR_REQUEST_ERROR:
	case SDHCI_RESET_FOR_TUNING_ABORT:
	case SDHCI_RESET_FOR_CARD_REMOVED:
	case SDHCI_RESET_FOR_CQE_RECOVERY:
		sdhci_do_reset(host, SDHCI_RESET_CMD);
		sdhci_do_reset(host, SDHCI_RESET_DATA);
		break;
	case SDHCI_RESET_FOR_REQUEST_ERROR_DATA_ONLY:
		sdhci_do_reset(host, SDHCI_RESET_DATA);
		break;
	}
}

#define sdhci_reset_for(h, r) sdhci_reset_for_reason((h), SDHCI_RESET_FOR_##r)

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

static void sdhci_config_dma(struct sdhci_host *host)
{
	u8 ctrl;
	u16 ctrl2;

	if (host->version < SDHCI_SPEC_200)
		return;

	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);

	/*
	 * Always adjust the DMA selection as some controllers
	 * (e.g. JMicron) can't do PIO properly when the selection
	 * is ADMA.
	 */
	ctrl &= ~SDHCI_CTRL_DMA_MASK;
	if (!(host->flags & SDHCI_REQ_USE_DMA))
		goto out;

	/* Note if DMA Select is zero then SDMA is selected */
	if (host->flags & SDHCI_USE_ADMA)
		ctrl |= SDHCI_CTRL_ADMA32;

	if (host->flags & SDHCI_USE_64_BIT_DMA) {
		/*
		 * If v4 mode, all supported DMA can be 64-bit addressing if
		 * controller supports 64-bit system address, otherwise only
		 * ADMA can support 64-bit addressing.
		 */
		if (host->v4_mode) {
			ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
			ctrl2 |= SDHCI_CTRL_64BIT_ADDR;
			sdhci_writew(host, ctrl2, SDHCI_HOST_CONTROL2);
		} else if (host->flags & SDHCI_USE_ADMA) {
			/*
			 * Don't need to undo SDHCI_CTRL_ADMA32 in order to
			 * set SDHCI_CTRL_ADMA64.
			 */
			ctrl |= SDHCI_CTRL_ADMA64;
		}
	}

out:
	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
}

static void sdhci_init(struct sdhci_host *host, int soft)
{
	struct mmc_host *mmc = host->mmc;
	unsigned long flags;

	if (soft)
		sdhci_reset_for(host, INIT);
	else
		sdhci_reset_for_all(host);

	if (host->v4_mode)
		sdhci_do_enable_v4_mode(host);

	spin_lock_irqsave(&host->lock, flags);
	sdhci_set_default_irqs(host);
	spin_unlock_irqrestore(&host->lock, flags);

	host->cqe_on = false;

	if (soft) {
		/* force clock reconfiguration */
		host->clock = 0;
		host->reinit_uhs = true;
		mmc->ops->set_ios(mmc, &mmc->ios);
	}
}

static void sdhci_reinit(struct sdhci_host *host)
{
	u32 cd = host->ier & (SDHCI_INT_CARD_REMOVE | SDHCI_INT_CARD_INSERT);

	sdhci_init(host, 0);
	sdhci_enable_card_detection(host);

	/*
	 * A change to the card detect bits indicates a change in present state,
	 * refer sdhci_set_card_detection(). A card detect interrupt might have
	 * been missed while the host controller was being reset, so trigger a
	 * rescan to check.
	 */
	if (cd != (host->ier & (SDHCI_INT_CARD_REMOVE | SDHCI_INT_CARD_INSERT)))
		mmc_detect_change(host->mmc, msecs_to_jiffies(200));
}

static void __sdhci_led_activate(struct sdhci_host *host)
{
	u8 ctrl;

	if (host->quirks & SDHCI_QUIRK_NO_LED)
		return;

	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
	ctrl |= SDHCI_CTRL_LED;
	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
}

static void __sdhci_led_deactivate(struct sdhci_host *host)
{
	u8 ctrl;

	if (host->quirks & SDHCI_QUIRK_NO_LED)
		return;

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

	if (host->quirks & SDHCI_QUIRK_NO_LED)
		return 0;

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
	if (host->quirks & SDHCI_QUIRK_NO_LED)
		return;

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

void sdhci_mod_timer(struct sdhci_host *host, struct mmc_request *mrq,
		     unsigned long timeout)
{
	if (sdhci_data_line_cmd(mrq->cmd))
		mod_timer(&host->data_timer, timeout);
	else
		mod_timer(&host->timer, timeout);
}
EXPORT_SYMBOL_GPL(sdhci_mod_timer);

static void sdhci_del_timer(struct sdhci_host *host, struct mmc_request *mrq)
{
	if (sdhci_data_line_cmd(mrq->cmd))
		timer_delete(&host->data_timer);
	else
		timer_delete(&host->timer);
}

static inline bool sdhci_has_requests(struct sdhci_host *host)
{
	return host->cmd || host->data_cmd;
}

/*****************************************************************************\
 *                                                                           *
 * Core functions                                                            *
 *                                                                           *
\*****************************************************************************/

static void sdhci_read_block_pio(struct sdhci_host *host)
{
	size_t blksize, len, chunk;
	u32 scratch;
	u8 *buf;

	DBG("PIO reading\n");

	blksize = host->data->blksz;
	chunk = 0;

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
}

static void sdhci_write_block_pio(struct sdhci_host *host)
{
	size_t blksize, len, chunk;
	u32 scratch;
	u8 *buf;

	DBG("PIO writing\n");

	blksize = host->data->blksz;
	chunk = 0;
	scratch = 0;

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

	/* Bounce write requests to the bounce buffer */
	if (host->bounce_buffer) {
		unsigned int length = data->blksz * data->blocks;

		if (length > host->bounce_buffer_size) {
			pr_err("%s: asked for transfer of %u bytes exceeds bounce buffer %u bytes\n",
			       mmc_hostname(host->mmc), length,
			       host->bounce_buffer_size);
			return -EIO;
		}
		if (mmc_get_dma_dir(data) == DMA_TO_DEVICE) {
			/* Copy the data to the bounce buffer */
			if (host->ops->copy_to_bounce_buffer) {
				host->ops->copy_to_bounce_buffer(host,
								 data, length);
			} else {
				sg_copy_to_buffer(data->sg, data->sg_len,
						  host->bounce_buffer, length);
			}
		}
		/* Switch ownership to the DMA */
		dma_sync_single_for_device(mmc_dev(host->mmc),
					   host->bounce_addr,
					   host->bounce_buffer_size,
					   mmc_get_dma_dir(data));
		/* Just a dummy value */
		sg_count = 1;
	} else {
		/* Just access the data directly from memory */
		sg_count = dma_map_sg(mmc_dev(host->mmc),
				      data->sg, data->sg_len,
				      mmc_get_dma_dir(data));
	}

	if (sg_count == 0)
		return -ENOSPC;

	data->sg_count = sg_count;
	data->host_cookie = cookie;

	return sg_count;
}

static char *sdhci_kmap_atomic(struct scatterlist *sg)
{
	return kmap_local_page(sg_page(sg)) + sg->offset;
}

static void sdhci_kunmap_atomic(void *buffer)
{
	kunmap_local(buffer);
}

void sdhci_adma_write_desc(struct sdhci_host *host, void **desc,
			   dma_addr_t addr, int len, unsigned int cmd)
{
	struct sdhci_adma2_64_desc *dma_desc = *desc;

	/* 32-bit and 64-bit descriptors have these members in same position */
	dma_desc->cmd = cpu_to_le16(cmd);
	dma_desc->len = cpu_to_le16(len);
	dma_desc->addr_lo = cpu_to_le32(lower_32_bits(addr));

	if (host->flags & SDHCI_USE_64_BIT_DMA)
		dma_desc->addr_hi = cpu_to_le32(upper_32_bits(addr));

	*desc += host->desc_sz;
}
EXPORT_SYMBOL_GPL(sdhci_adma_write_desc);

static inline void __sdhci_adma_write_desc(struct sdhci_host *host,
					   void **desc, dma_addr_t addr,
					   int len, unsigned int cmd)
{
	if (host->ops->adma_write_desc)
		host->ops->adma_write_desc(host, desc, addr, len, cmd);
	else
		sdhci_adma_write_desc(host, desc, addr, len, cmd);
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
				buffer = sdhci_kmap_atomic(sg);
				memcpy(align, buffer, offset);
				sdhci_kunmap_atomic(buffer);
			}

			/* tran, valid */
			__sdhci_adma_write_desc(host, &desc, align_addr,
						offset, ADMA2_TRAN_VALID);

			BUG_ON(offset > 65536);

			align += SDHCI_ADMA2_ALIGN;
			align_addr += SDHCI_ADMA2_ALIGN;

			addr += offset;
			len -= offset;
		}

		/*
		 * The block layer forces a minimum segment size of PAGE_SIZE,
		 * so 'len' can be too big here if PAGE_SIZE >= 64KiB. Write
		 * multiple descriptors, noting that the ADMA table is sized
		 * for 4KiB chunks anyway, so it will be big enough.
		 */
		while (len > host->max_adma) {
			int n = 32 * 1024; /* 32KiB*/

			__sdhci_adma_write_desc(host, &desc, addr, n, ADMA2_TRAN_VALID);
			addr += n;
			len -= n;
		}

		/* tran, valid */
		if (len)
			__sdhci_adma_write_desc(host, &desc, addr, len,
						ADMA2_TRAN_VALID);

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
		__sdhci_adma_write_desc(host, &desc, 0, 0, ADMA2_NOP_END_VALID);
	}
}

static void sdhci_adma_table_post(struct sdhci_host *host,
	struct mmc_data *data)
{
	struct scatterlist *sg;
	int i, size;
	void *align;
	char *buffer;

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

					buffer = sdhci_kmap_atomic(sg);
					memcpy(buffer, align, size);
					sdhci_kunmap_atomic(buffer);

					align += SDHCI_ADMA2_ALIGN;
				}
			}
		}
	}
}

static void sdhci_set_adma_addr(struct sdhci_host *host, dma_addr_t addr)
{
	sdhci_writel(host, lower_32_bits(addr), SDHCI_ADMA_ADDRESS);
	if (host->flags & SDHCI_USE_64_BIT_DMA)
		sdhci_writel(host, upper_32_bits(addr), SDHCI_ADMA_ADDRESS_HI);
}

static dma_addr_t sdhci_sdma_address(struct sdhci_host *host)
{
	if (host->bounce_buffer)
		return host->bounce_addr;
	else
		return sg_dma_address(host->data->sg);
}

static void sdhci_set_sdma_addr(struct sdhci_host *host, dma_addr_t addr)
{
	if (host->v4_mode)
		sdhci_set_adma_addr(host, addr);
	else
		sdhci_writel(host, addr, SDHCI_DMA_ADDRESS);
}

static unsigned int sdhci_target_timeout(struct sdhci_host *host,
					 struct mmc_command *cmd,
					 struct mmc_data *data)
{
	unsigned int target_timeout;

	/* timeout in us */
	if (!data) {
		target_timeout = cmd->busy_timeout * 1000;
	} else {
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

	return target_timeout;
}

static void sdhci_calc_sw_timeout(struct sdhci_host *host,
				  struct mmc_command *cmd)
{
	struct mmc_data *data = cmd->data;
	struct mmc_host *mmc = host->mmc;
	struct mmc_ios *ios = &mmc->ios;
	unsigned char bus_width = 1 << ios->bus_width;
	unsigned int blksz;
	unsigned int freq;
	u64 target_timeout;
	u64 transfer_time;

	target_timeout = sdhci_target_timeout(host, cmd, data);
	target_timeout *= NSEC_PER_USEC;

	if (data) {
		blksz = data->blksz;
		freq = mmc->actual_clock ? : host->clock;
		transfer_time = (u64)blksz * NSEC_PER_SEC * (8 / bus_width);
		do_div(transfer_time, freq);
		/* multiply by '2' to account for any unknowns */
		transfer_time = transfer_time * 2;
		/* calculate timeout for the entire data */
		host->data_timeout = data->blocks * target_timeout +
				     transfer_time;
	} else {
		host->data_timeout = target_timeout;
	}

	if (host->data_timeout)
		host->data_timeout += MMC_CMD_TRANSFER_TIME;
}

static u8 sdhci_calc_timeout(struct sdhci_host *host, struct mmc_command *cmd,
			     bool *too_big)
{
	u8 count;
	struct mmc_data *data;
	unsigned target_timeout, current_timeout;

	*too_big = false;

	/*
	 * If the host controller provides us with an incorrect timeout
	 * value, just skip the check and use the maximum. The hardware may take
	 * longer to time out, but that's much better than having a too-short
	 * timeout value.
	 */
	if (host->quirks & SDHCI_QUIRK_BROKEN_TIMEOUT_VAL)
		return host->max_timeout_count;

	/* Unspecified command, assume max */
	if (cmd == NULL)
		return host->max_timeout_count;

	data = cmd->data;
	/* Unspecified timeout, assume max */
	if (!data && !cmd->busy_timeout)
		return host->max_timeout_count;

	/* timeout in us */
	target_timeout = sdhci_target_timeout(host, cmd, data);

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
		if (count > host->max_timeout_count) {
			if (!(host->quirks2 & SDHCI_QUIRK2_DISABLE_HW_TIMEOUT))
				DBG("Too large timeout 0x%x requested for CMD%d!\n",
				    count, cmd->opcode);
			count = host->max_timeout_count;
			*too_big = true;
			break;
		}
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

	if (host->flags & (SDHCI_AUTO_CMD23 | SDHCI_AUTO_CMD12))
		host->ier |= SDHCI_INT_AUTO_CMD_ERR;
	else
		host->ier &= ~SDHCI_INT_AUTO_CMD_ERR;

	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
}

void sdhci_set_data_timeout_irq(struct sdhci_host *host, bool enable)
{
	if (enable)
		host->ier |= SDHCI_INT_DATA_TIMEOUT;
	else
		host->ier &= ~SDHCI_INT_DATA_TIMEOUT;
	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
}
EXPORT_SYMBOL_GPL(sdhci_set_data_timeout_irq);

void __sdhci_set_timeout(struct sdhci_host *host, struct mmc_command *cmd)
{
	bool too_big = false;
	u8 count = sdhci_calc_timeout(host, cmd, &too_big);

	if (too_big &&
	    host->quirks2 & SDHCI_QUIRK2_DISABLE_HW_TIMEOUT) {
		sdhci_calc_sw_timeout(host, cmd);
		sdhci_set_data_timeout_irq(host, false);
	} else if (!(host->ier & SDHCI_INT_DATA_TIMEOUT)) {
		sdhci_set_data_timeout_irq(host, true);
	}

	sdhci_writeb(host, count, SDHCI_TIMEOUT_CONTROL);
}
EXPORT_SYMBOL_GPL(__sdhci_set_timeout);

static void sdhci_set_timeout(struct sdhci_host *host, struct mmc_command *cmd)
{
	if (host->ops->set_timeout)
		host->ops->set_timeout(host, cmd);
	else
		__sdhci_set_timeout(host, cmd);
}

void sdhci_initialize_data(struct sdhci_host *host, struct mmc_data *data)
{
	WARN_ON(host->data);

	/* Sanity checks */
	BUG_ON(data->blksz * data->blocks > 524288);
	BUG_ON(data->blksz > host->mmc->max_blk_size);
	BUG_ON(data->blocks > 65535);

	host->data = data;
	host->data_early = 0;
	host->data->bytes_xfered = 0;
}
EXPORT_SYMBOL_GPL(sdhci_initialize_data);

static inline void sdhci_set_block_info(struct sdhci_host *host,
					struct mmc_data *data)
{
	/* Set the DMA boundary value and block size */
	sdhci_writew(host,
		     SDHCI_MAKE_BLKSZ(host->sdma_boundary, data->blksz),
		     SDHCI_BLOCK_SIZE);
	/*
	 * For Version 4.10 onwards, if v4 mode is enabled, 32-bit Block Count
	 * can be supported, in that case 16-bit block count register must be 0.
	 */
	if (host->version >= SDHCI_SPEC_410 && host->v4_mode &&
	    (host->quirks2 & SDHCI_QUIRK2_USE_32BIT_BLK_CNT)) {
		if (sdhci_readw(host, SDHCI_BLOCK_COUNT))
			sdhci_writew(host, 0, SDHCI_BLOCK_COUNT);
		sdhci_writew(host, data->blocks, SDHCI_32BIT_BLK_CNT);
	} else {
		sdhci_writew(host, data->blocks, SDHCI_BLOCK_COUNT);
	}
}

void sdhci_prepare_dma(struct sdhci_host *host, struct mmc_data *data)
{
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

	sdhci_config_dma(host);

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
			sdhci_set_adma_addr(host, host->adma_addr);
		} else {
			WARN_ON(sg_cnt != 1);
			sdhci_set_sdma_addr(host, sdhci_sdma_address(host));
		}
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
}
EXPORT_SYMBOL_GPL(sdhci_prepare_dma);

static void sdhci_prepare_data(struct sdhci_host *host, struct mmc_command *cmd)
{
	struct mmc_data *data = cmd->data;

	sdhci_initialize_data(host, data);

	sdhci_prepare_dma(host, data);

	sdhci_set_block_info(host, data);
}

#if IS_ENABLED(CONFIG_MMC_SDHCI_EXTERNAL_DMA)

static int sdhci_external_dma_init(struct sdhci_host *host)
{
	int ret = 0;
	struct mmc_host *mmc = host->mmc;

	host->tx_chan = dma_request_chan(mmc_dev(mmc), "tx");
	if (IS_ERR(host->tx_chan)) {
		ret = PTR_ERR(host->tx_chan);
		if (ret != -EPROBE_DEFER)
			pr_warn("Failed to request TX DMA channel.\n");
		host->tx_chan = NULL;
		return ret;
	}

	host->rx_chan = dma_request_chan(mmc_dev(mmc), "rx");
	if (IS_ERR(host->rx_chan)) {
		if (host->tx_chan) {
			dma_release_channel(host->tx_chan);
			host->tx_chan = NULL;
		}

		ret = PTR_ERR(host->rx_chan);
		if (ret != -EPROBE_DEFER)
			pr_warn("Failed to request RX DMA channel.\n");
		host->rx_chan = NULL;
	}

	return ret;
}

static struct dma_chan *sdhci_external_dma_channel(struct sdhci_host *host,
						   struct mmc_data *data)
{
	return data->flags & MMC_DATA_WRITE ? host->tx_chan : host->rx_chan;
}

static int sdhci_external_dma_setup(struct sdhci_host *host,
				    struct mmc_command *cmd)
{
	int ret, i;
	enum dma_transfer_direction dir;
	struct dma_async_tx_descriptor *desc;
	struct mmc_data *data = cmd->data;
	struct dma_chan *chan;
	struct dma_slave_config cfg;
	dma_cookie_t cookie;
	int sg_cnt;

	if (!host->mapbase)
		return -EINVAL;

	memset(&cfg, 0, sizeof(cfg));
	cfg.src_addr = host->mapbase + SDHCI_BUFFER;
	cfg.dst_addr = host->mapbase + SDHCI_BUFFER;
	cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.src_maxburst = data->blksz / 4;
	cfg.dst_maxburst = data->blksz / 4;

	/* Sanity check: all the SG entries must be aligned by block size. */
	for (i = 0; i < data->sg_len; i++) {
		if ((data->sg + i)->length % data->blksz)
			return -EINVAL;
	}

	chan = sdhci_external_dma_channel(host, data);

	ret = dmaengine_slave_config(chan, &cfg);
	if (ret)
		return ret;

	sg_cnt = sdhci_pre_dma_transfer(host, data, COOKIE_MAPPED);
	if (sg_cnt <= 0)
		return -EINVAL;

	dir = data->flags & MMC_DATA_WRITE ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM;
	desc = dmaengine_prep_slave_sg(chan, data->sg, data->sg_len, dir,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc)
		return -EINVAL;

	desc->callback = NULL;
	desc->callback_param = NULL;

	cookie = dmaengine_submit(desc);
	if (dma_submit_error(cookie))
		ret = cookie;

	return ret;
}

static void sdhci_external_dma_release(struct sdhci_host *host)
{
	if (host->tx_chan) {
		dma_release_channel(host->tx_chan);
		host->tx_chan = NULL;
	}

	if (host->rx_chan) {
		dma_release_channel(host->rx_chan);
		host->rx_chan = NULL;
	}

	sdhci_switch_external_dma(host, false);
}

static void __sdhci_external_dma_prepare_data(struct sdhci_host *host,
					      struct mmc_command *cmd)
{
	struct mmc_data *data = cmd->data;

	sdhci_initialize_data(host, data);

	host->flags |= SDHCI_REQ_USE_DMA;
	sdhci_set_transfer_irqs(host);

	sdhci_set_block_info(host, data);
}

static void sdhci_external_dma_prepare_data(struct sdhci_host *host,
					    struct mmc_command *cmd)
{
	if (!sdhci_external_dma_setup(host, cmd)) {
		__sdhci_external_dma_prepare_data(host, cmd);
	} else {
		sdhci_external_dma_release(host);
		pr_err("%s: Cannot use external DMA, switch to the DMA/PIO which standard SDHCI provides.\n",
		       mmc_hostname(host->mmc));
		sdhci_prepare_data(host, cmd);
	}
}

static void sdhci_external_dma_pre_transfer(struct sdhci_host *host,
					    struct mmc_command *cmd)
{
	struct dma_chan *chan;

	if (!cmd->data)
		return;

	chan = sdhci_external_dma_channel(host, cmd->data);
	if (chan)
		dma_async_issue_pending(chan);
}

#else

static inline int sdhci_external_dma_init(struct sdhci_host *host)
{
	return -EOPNOTSUPP;
}

static inline void sdhci_external_dma_release(struct sdhci_host *host)
{
}

static inline void sdhci_external_dma_prepare_data(struct sdhci_host *host,
						   struct mmc_command *cmd)
{
	/* This should never happen */
	WARN_ON_ONCE(1);
}

static inline void sdhci_external_dma_pre_transfer(struct sdhci_host *host,
						   struct mmc_command *cmd)
{
}

static inline struct dma_chan *sdhci_external_dma_channel(struct sdhci_host *host,
							  struct mmc_data *data)
{
	return NULL;
}

#endif

void sdhci_switch_external_dma(struct sdhci_host *host, bool en)
{
	host->use_external_dma = en;
}
EXPORT_SYMBOL_GPL(sdhci_switch_external_dma);

static inline bool sdhci_auto_cmd12(struct sdhci_host *host,
				    struct mmc_request *mrq)
{
	return !mrq->sbc && (host->flags & SDHCI_AUTO_CMD12) &&
	       !mrq->cap_cmd_during_tfr;
}

static inline bool sdhci_auto_cmd23(struct sdhci_host *host,
				    struct mmc_request *mrq)
{
	return mrq->sbc && (host->flags & SDHCI_AUTO_CMD23);
}

static inline bool sdhci_manual_cmd23(struct sdhci_host *host,
				      struct mmc_request *mrq)
{
	return mrq->sbc && !(host->flags & SDHCI_AUTO_CMD23);
}

static inline void sdhci_auto_cmd_select(struct sdhci_host *host,
					 struct mmc_command *cmd,
					 u16 *mode)
{
	bool use_cmd12 = sdhci_auto_cmd12(host, cmd->mrq) &&
			 (cmd->opcode != SD_IO_RW_EXTENDED);
	bool use_cmd23 = sdhci_auto_cmd23(host, cmd->mrq);
	u16 ctrl2;

	/*
	 * In case of Version 4.10 or later, use of 'Auto CMD Auto
	 * Select' is recommended rather than use of 'Auto CMD12
	 * Enable' or 'Auto CMD23 Enable'. We require Version 4 Mode
	 * here because some controllers (e.g sdhci-of-dwmshc) expect it.
	 */
	if (host->version >= SDHCI_SPEC_410 && host->v4_mode &&
	    (use_cmd12 || use_cmd23)) {
		*mode |= SDHCI_TRNS_AUTO_SEL;

		ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		if (use_cmd23)
			ctrl2 |= SDHCI_CMD23_ENABLE;
		else
			ctrl2 &= ~SDHCI_CMD23_ENABLE;
		sdhci_writew(host, ctrl2, SDHCI_HOST_CONTROL2);

		return;
	}

	/*
	 * If we are sending CMD23, CMD12 never gets sent
	 * on successful completion (so no Auto-CMD12).
	 */
	if (use_cmd12)
		*mode |= SDHCI_TRNS_AUTO_CMD12;
	else if (use_cmd23)
		*mode |= SDHCI_TRNS_AUTO_CMD23;
}

static void sdhci_set_transfer_mode(struct sdhci_host *host,
	struct mmc_command *cmd)
{
	u16 mode = 0;
	struct mmc_data *data = cmd->data;

	if (data == NULL) {
		if (host->quirks2 &
			SDHCI_QUIRK2_CLEAR_TRANSFERMODE_REG_BEFORE_CMD) {
			/* must not clear SDHCI_TRANSFER_MODE when tuning */
			if (!mmc_op_tuning(cmd->opcode))
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
		sdhci_auto_cmd_select(host, cmd, &mode);
		if (sdhci_auto_cmd23(host, cmd->mrq))
			sdhci_writel(host, cmd->mrq->sbc->arg, SDHCI_ARGUMENT2);
	}

	if (data->flags & MMC_DATA_READ)
		mode |= SDHCI_TRNS_READ;
	if (host->flags & SDHCI_REQ_USE_DMA)
		mode |= SDHCI_TRNS_DMA;

	sdhci_writew(host, mode, SDHCI_TRANSFER_MODE);
}

bool sdhci_needs_reset(struct sdhci_host *host, struct mmc_request *mrq)
{
	return (!(host->flags & SDHCI_DEVICE_DEAD) &&
		((mrq->cmd && mrq->cmd->error) ||
		 (mrq->sbc && mrq->sbc->error) ||
		 (mrq->data && mrq->data->stop && mrq->data->stop->error) ||
		 (host->quirks & SDHCI_QUIRK_RESET_AFTER_REQUEST)));
}
EXPORT_SYMBOL_GPL(sdhci_needs_reset);

static void sdhci_set_mrq_done(struct sdhci_host *host, struct mmc_request *mrq)
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
}

void __sdhci_finish_mrq(struct sdhci_host *host, struct mmc_request *mrq)
{
	if (host->cmd && host->cmd->mrq == mrq)
		host->cmd = NULL;

	if (host->data_cmd && host->data_cmd->mrq == mrq)
		host->data_cmd = NULL;

	if (host->deferred_cmd && host->deferred_cmd->mrq == mrq)
		host->deferred_cmd = NULL;

	if (host->data && host->data->mrq == mrq)
		host->data = NULL;

	if (sdhci_needs_reset(host, mrq))
		host->pending_reset = true;

	sdhci_set_mrq_done(host, mrq);

	sdhci_del_timer(host, mrq);

	if (!sdhci_has_requests(host))
		sdhci_led_deactivate(host);
}
EXPORT_SYMBOL_GPL(__sdhci_finish_mrq);

void sdhci_finish_mrq(struct sdhci_host *host, struct mmc_request *mrq)
{
	__sdhci_finish_mrq(host, mrq);

	queue_work(host->complete_wq, &host->complete_work);
}
EXPORT_SYMBOL_GPL(sdhci_finish_mrq);

void __sdhci_finish_data_common(struct sdhci_host *host, bool defer_reset)
{
	struct mmc_command *data_cmd = host->data_cmd;
	struct mmc_data *data = host->data;

	host->data = NULL;
	host->data_cmd = NULL;

	/*
	 * The controller needs a reset of internal state machines upon error
	 * conditions.
	 */
	if (data->error) {
		if (defer_reset)
			host->pending_reset = true;
		else if (!host->cmd || host->cmd == data_cmd)
			sdhci_reset_for(host, REQUEST_ERROR);
		else
			sdhci_reset_for(host, REQUEST_ERROR_DATA_ONLY);
	}

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
}
EXPORT_SYMBOL_GPL(__sdhci_finish_data_common);

static void __sdhci_finish_data(struct sdhci_host *host, bool sw_data_timeout)
{
	struct mmc_data *data = host->data;

	__sdhci_finish_data_common(host, false);

	/*
	 * Need to send CMD12 if -
	 * a) open-ended multiblock transfer not using auto CMD12 (no CMD23)
	 * b) error in multiblock transfer
	 */
	if (data->stop &&
	    ((!data->mrq->sbc && !sdhci_auto_cmd12(host, data->mrq)) ||
	     data->error)) {
		/*
		 * 'cap_cmd_during_tfr' request must not use the command line
		 * after mmc_command_done() has been called. It is upper layer's
		 * responsibility to send the stop command if required.
		 */
		if (data->mrq->cap_cmd_during_tfr) {
			__sdhci_finish_mrq(host, data->mrq);
		} else {
			/* Avoid triggering warning in sdhci_send_command() */
			host->cmd = NULL;
			if (!sdhci_send_command(host, data->stop)) {
				if (sw_data_timeout) {
					/*
					 * This is anyway a sw data timeout, so
					 * give up now.
					 */
					data->stop->error = -EIO;
					__sdhci_finish_mrq(host, data->mrq);
				} else {
					WARN_ON(host->deferred_cmd);
					host->deferred_cmd = data->stop;
				}
			}
		}
	} else {
		__sdhci_finish_mrq(host, data->mrq);
	}
}

static void sdhci_finish_data(struct sdhci_host *host)
{
	__sdhci_finish_data(host, false);
}

static bool sdhci_send_command(struct sdhci_host *host, struct mmc_command *cmd)
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

	mask = SDHCI_CMD_INHIBIT;
	if (sdhci_data_line_cmd(cmd))
		mask |= SDHCI_DATA_INHIBIT;

	/* We shouldn't wait for data inihibit for stop commands, even
	   though they might use busy signaling */
	if (cmd->mrq->data && (cmd == cmd->mrq->data->stop))
		mask &= ~SDHCI_DATA_INHIBIT;

	if (sdhci_readl(host, SDHCI_PRESENT_STATE) & mask)
		return false;

	host->cmd = cmd;
	host->data_timeout = 0;
	if (sdhci_data_line_cmd(cmd)) {
		WARN_ON(host->data_cmd);
		host->data_cmd = cmd;
		sdhci_set_timeout(host, cmd);
	}

	if (cmd->data) {
		if (host->use_external_dma)
			sdhci_external_dma_prepare_data(host, cmd);
		else
			sdhci_prepare_data(host, cmd);
	}

	sdhci_writel(host, cmd->arg, SDHCI_ARGUMENT);

	sdhci_set_transfer_mode(host, cmd);

	if ((cmd->flags & MMC_RSP_136) && (cmd->flags & MMC_RSP_BUSY)) {
		WARN_ONCE(1, "Unsupported response type!\n");
		/*
		 * This does not happen in practice because 136-bit response
		 * commands never have busy waiting, so rather than complicate
		 * the error path, just remove busy waiting and continue.
		 */
		cmd->flags &= ~MMC_RSP_BUSY;
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
	if (cmd->data || mmc_op_tuning(cmd->opcode))
		flags |= SDHCI_CMD_DATA;

	timeout = jiffies;
	if (host->data_timeout)
		timeout += nsecs_to_jiffies(host->data_timeout);
	else if (!cmd->data && cmd->busy_timeout > 9000)
		timeout += DIV_ROUND_UP(cmd->busy_timeout, 1000) * HZ + HZ;
	else
		timeout += 10 * HZ;
	sdhci_mod_timer(host, cmd->mrq, timeout);

	if (host->use_external_dma)
		sdhci_external_dma_pre_transfer(host, cmd);

	sdhci_writew(host, SDHCI_MAKE_CMD(cmd->opcode, flags), SDHCI_COMMAND);

	return true;
}

bool sdhci_present_error(struct sdhci_host *host,
			 struct mmc_command *cmd, bool present)
{
	if (!present || host->flags & SDHCI_DEVICE_DEAD) {
		cmd->error = -ENOMEDIUM;
		return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(sdhci_present_error);

static bool sdhci_send_command_retry(struct sdhci_host *host,
				     struct mmc_command *cmd,
				     unsigned long flags)
	__releases(host->lock)
	__acquires(host->lock)
{
	struct mmc_command *deferred_cmd = host->deferred_cmd;
	int timeout = 10; /* Approx. 10 ms */
	bool present;

	while (!sdhci_send_command(host, cmd)) {
		if (!timeout--) {
			pr_err("%s: Controller never released inhibit bit(s).\n",
			       mmc_hostname(host->mmc));
			sdhci_err_stats_inc(host, CTRL_TIMEOUT);
			sdhci_dumpregs(host);
			cmd->error = -EIO;
			return false;
		}

		spin_unlock_irqrestore(&host->lock, flags);

		usleep_range(1000, 1250);

		present = host->mmc->ops->get_cd(host->mmc);

		spin_lock_irqsave(&host->lock, flags);

		/* A deferred command might disappear, handle that */
		if (cmd == deferred_cmd && cmd != host->deferred_cmd)
			return true;

		if (sdhci_present_error(host, cmd, present))
			return false;
	}

	if (cmd == host->deferred_cmd)
		host->deferred_cmd = NULL;

	return true;
}

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
		if (!sdhci_send_command(host, cmd->mrq->cmd)) {
			WARN_ON(host->deferred_cmd);
			host->deferred_cmd = cmd->mrq->cmd;
		}
	} else {

		/* Processed actual command. */
		if (host->data && host->data_early)
			sdhci_finish_data(host);

		if (!cmd->data)
			__sdhci_finish_mrq(host, cmd->mrq);
	}
}

static u16 sdhci_get_preset_value(struct sdhci_host *host)
{
	u16 preset = 0;

	switch (host->timing) {
	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_SD_HS:
		preset = sdhci_readw(host, SDHCI_PRESET_FOR_HIGH_SPEED);
		break;
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
	case MMC_TIMING_UHS2_SPEED_A:
	case MMC_TIMING_UHS2_SPEED_A_HD:
	case MMC_TIMING_UHS2_SPEED_B:
	case MMC_TIMING_UHS2_SPEED_B_HD:
		preset = sdhci_readw(host, SDHCI_PRESET_FOR_UHS2);
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
			div = FIELD_GET(SDHCI_PRESET_SDCLK_FREQ_MASK, pre_val);
			if (host->clk_mul &&
				(pre_val & SDHCI_PRESET_CLKGEN_SEL)) {
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

	/* Wait max 150 ms */
	timeout = ktime_add_ms(ktime_get(), 150);
	while (1) {
		bool timedout = ktime_after(ktime_get(), timeout);

		clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
		if (clk & SDHCI_CLOCK_INT_STABLE)
			break;
		if (timedout) {
			pr_err("%s: Internal clock never stabilised.\n",
			       mmc_hostname(host->mmc));
			sdhci_err_stats_inc(host, CTRL_TIMEOUT);
			sdhci_dumpregs(host);
			return;
		}
		udelay(10);
	}

	if (host->version >= SDHCI_SPEC_410 && host->v4_mode) {
		clk |= SDHCI_CLOCK_PLL_EN;
		clk &= ~SDHCI_CLOCK_INT_STABLE;
		sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

		/* Wait max 150 ms */
		timeout = ktime_add_ms(ktime_get(), 150);
		while (1) {
			bool timedout = ktime_after(ktime_get(), timeout);

			clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
			if (clk & SDHCI_CLOCK_INT_STABLE)
				break;
			if (timedout) {
				pr_err("%s: PLL clock never stabilised.\n",
				       mmc_hostname(host->mmc));
				sdhci_err_stats_inc(host, CTRL_TIMEOUT);
				sdhci_dumpregs(host);
				return;
			}
			udelay(10);
		}
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

unsigned short sdhci_get_vdd_value(unsigned short vdd)
{
	switch (1 << vdd) {
	case MMC_VDD_165_195:
	/*
	 * Without a regulator, SDHCI does not support 2.0v
	 * so we only get here if the driver deliberately
	 * added the 2.0v range to ocr_avail. Map it to 1.8v
	 * for the purpose of turning on the power.
	 */
	case MMC_VDD_20_21:
		return SDHCI_POWER_180;
	case MMC_VDD_29_30:
	case MMC_VDD_30_31:
		return SDHCI_POWER_300;
	case MMC_VDD_32_33:
	case MMC_VDD_33_34:
	/*
	 * 3.4V ~ 3.6V are valid only for those platforms where it's
	 * known that the voltage range is supported by hardware.
	 */
	case MMC_VDD_34_35:
	case MMC_VDD_35_36:
		return SDHCI_POWER_330;
	default:
		return 0;
	}
}
EXPORT_SYMBOL_GPL(sdhci_get_vdd_value);

void sdhci_set_power_noreg(struct sdhci_host *host, unsigned char mode,
			   unsigned short vdd)
{
	u8 pwr = 0;

	if (mode != MMC_POWER_OFF) {
		pwr = sdhci_get_vdd_value(vdd);
		if (!pwr) {
			WARN(1, "%s: Invalid vdd %#x\n",
			     mmc_hostname(host->mmc), vdd);
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

/*
 * Some controllers need to configure a valid bus voltage on their power
 * register regardless of whether an external regulator is taking care of power
 * supply. This helper function takes care of it if set as the controller's
 * sdhci_ops.set_power callback.
 */
void sdhci_set_power_and_bus_voltage(struct sdhci_host *host,
				     unsigned char mode,
				     unsigned short vdd)
{
	if (!IS_ERR(host->mmc->supply.vmmc)) {
		struct mmc_host *mmc = host->mmc;

		mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, vdd);
	}
	sdhci_set_power_noreg(host, mode, vdd);
}
EXPORT_SYMBOL_GPL(sdhci_set_power_and_bus_voltage);

/*****************************************************************************\
 *                                                                           *
 * MMC callbacks                                                             *
 *                                                                           *
\*****************************************************************************/

void sdhci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct mmc_command *cmd;
	unsigned long flags;
	bool present;

	/* Firstly check card presence */
	present = mmc->ops->get_cd(mmc);

	spin_lock_irqsave(&host->lock, flags);

	sdhci_led_activate(host);

	if (sdhci_present_error(host, mrq->cmd, present))
		goto out_finish;

	cmd = sdhci_manual_cmd23(host, mrq) ? mrq->sbc : mrq->cmd;

	if (!sdhci_send_command_retry(host, cmd, flags))
		goto out_finish;

	spin_unlock_irqrestore(&host->lock, flags);

	return;

out_finish:
	sdhci_finish_mrq(host, mrq);
	spin_unlock_irqrestore(&host->lock, flags);
}
EXPORT_SYMBOL_GPL(sdhci_request);

int sdhci_request_atomic(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct mmc_command *cmd;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&host->lock, flags);

	if (sdhci_present_error(host, mrq->cmd, true)) {
		sdhci_finish_mrq(host, mrq);
		goto out_finish;
	}

	cmd = sdhci_manual_cmd23(host, mrq) ? mrq->sbc : mrq->cmd;

	/*
	 * The HSQ may send a command in interrupt context without polling
	 * the busy signaling, which means we should return BUSY if controller
	 * has not released inhibit bits to allow HSQ trying to send request
	 * again in non-atomic context. So we should not finish this request
	 * here.
	 */
	if (!sdhci_send_command(host, cmd))
		ret = -EBUSY;
	else
		sdhci_led_activate(host);

out_finish:
	spin_unlock_irqrestore(&host->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(sdhci_request_atomic);

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

static bool sdhci_timing_has_preset(unsigned char timing)
{
	switch (timing) {
	case MMC_TIMING_UHS_SDR12:
	case MMC_TIMING_UHS_SDR25:
	case MMC_TIMING_UHS_SDR50:
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		return true;
	}
	return false;
}

static bool sdhci_preset_needed(struct sdhci_host *host, unsigned char timing)
{
	return !(host->quirks2 & SDHCI_QUIRK2_PRESET_VALUE_BROKEN) &&
	       sdhci_timing_has_preset(timing);
}

static bool sdhci_presetable_values_change(struct sdhci_host *host, struct mmc_ios *ios)
{
	/*
	 * Preset Values are: Driver Strength, Clock Generator and SDCLK/RCLK
	 * Frequency. Check if preset values need to be enabled, or the Driver
	 * Strength needs updating. Note, clock changes are handled separately.
	 */
	return !host->preset_enabled &&
	       (sdhci_preset_needed(host, ios->timing) || host->drv_type != ios->drv_type);
}

void sdhci_set_ios_common(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);

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
			host->timeout_clk = mmc->actual_clock ?
						mmc->actual_clock / 1000 :
						host->clock / 1000;
			mmc->max_busy_timeout =
				host->ops->get_max_timeout_count ?
				host->ops->get_max_timeout_count(host) :
				1 << 27;
			mmc->max_busy_timeout /= host->timeout_clk;
		}
	}
}
EXPORT_SYMBOL_GPL(sdhci_set_ios_common);

void sdhci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	bool reinit_uhs = host->reinit_uhs;
	bool turning_on_clk;
	u8 ctrl;

	host->reinit_uhs = false;

	if (ios->power_mode == MMC_POWER_UNDEFINED)
		return;

	if (host->flags & SDHCI_DEVICE_DEAD) {
		if (!IS_ERR(mmc->supply.vmmc) &&
		    ios->power_mode == MMC_POWER_OFF)
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, 0);
		return;
	}

	turning_on_clk = ios->clock != host->clock && ios->clock && !host->clock;

	sdhci_set_ios_common(mmc, ios);

	if (host->ops->set_power)
		host->ops->set_power(host, ios->power_mode, ios->vdd);
	else
		sdhci_set_power(host, ios->power_mode, ios->vdd);

	if (host->ops->platform_send_init_74_clocks)
		host->ops->platform_send_init_74_clocks(host, ios->power_mode);

	host->ops->set_bus_width(host, ios->bus_width);

	/*
	 * Special case to avoid multiple clock changes during voltage
	 * switching.
	 */
	if (!reinit_uhs &&
	    turning_on_clk &&
	    host->timing == ios->timing &&
	    host->version >= SDHCI_SPEC_300 &&
	    !sdhci_presetable_values_change(host, ios))
		return;

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

		/*
		 * According to SDHCI Spec v3.00, if the Preset Value
		 * Enable in the Host Control 2 register is set, we
		 * need to reset SD Clock Enable before changing High
		 * Speed Enable to avoid generating clock glitches.
		 */
		clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
		if (clk & SDHCI_CLOCK_CARD_EN) {
			clk &= ~SDHCI_CLOCK_CARD_EN;
			sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
		}

		sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);

		if (!host->preset_enabled) {
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
			host->drv_type = ios->drv_type;
		}

		host->ops->set_uhs_signaling(host, ios->timing);
		host->timing = ios->timing;

		if (sdhci_preset_needed(host, ios->timing)) {
			u16 preset;

			sdhci_enable_preset_value(host, true);
			preset = sdhci_get_preset_value(host);
			ios->drv_type = FIELD_GET(SDHCI_PRESET_DRV_MASK,
						  preset);
			host->drv_type = ios->drv_type;
		}

		/* Re-enable SD Clock */
		host->ops->set_clock(host, host->clock);
	} else
		sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
}
EXPORT_SYMBOL_GPL(sdhci_set_ios);

static int sdhci_get_cd(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	int gpio_cd = mmc_gpio_get_cd(mmc);

	if (host->flags & SDHCI_DEVICE_DEAD)
		return 0;

	/* If nonremovable, assume that the card is always present. */
	if (!mmc_card_is_removable(mmc))
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

int sdhci_get_cd_nogpio(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&host->lock, flags);

	if (host->flags & SDHCI_DEVICE_DEAD)
		goto out;

	ret = !!(sdhci_readl(host, SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT);
out:
	spin_unlock_irqrestore(&host->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(sdhci_get_cd_nogpio);

int sdhci_get_ro(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	bool allow_invert = false;
	int is_readonly;

	if (host->flags & SDHCI_DEVICE_DEAD) {
		is_readonly = 0;
	} else if (host->ops->get_ro) {
		is_readonly = host->ops->get_ro(host);
	} else if (mmc_host_can_gpio_ro(mmc)) {
		is_readonly = mmc_gpio_get_ro(mmc);
		/* Do not invert twice */
		allow_invert = !(mmc->caps2 & MMC_CAP2_RO_ACTIVE_HIGH);
	} else {
		is_readonly = !(sdhci_readl(host, SDHCI_PRESENT_STATE)
				& SDHCI_WRITE_PROTECT);
		allow_invert = true;
	}

	if (is_readonly >= 0 &&
	    allow_invert &&
	    (host->quirks & SDHCI_QUIRK_INVERTED_WRITE_PROTECT))
		is_readonly = !is_readonly;

	return is_readonly;
}
EXPORT_SYMBOL_GPL(sdhci_get_ro);

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
	}
}

void sdhci_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct sdhci_host *host = mmc_priv(mmc);
	unsigned long flags;

	if (enable)
		pm_runtime_get_noresume(mmc_dev(mmc));

	spin_lock_irqsave(&host->lock, flags);
	sdhci_enable_sdio_irq_nolock(host, enable);
	spin_unlock_irqrestore(&host->lock, flags);

	if (!enable)
		pm_runtime_put_noidle(mmc_dev(mmc));
}
EXPORT_SYMBOL_GPL(sdhci_enable_sdio_irq);

static void sdhci_ack_sdio_irq(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	sdhci_enable_sdio_irq_nolock(host, true);
	spin_unlock_irqrestore(&host->lock, flags);
}

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
			if (ret < 0) {
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

		pr_warn("%s: 3.3V regulator output did not become stable\n",
			mmc_hostname(mmc));

		return -EAGAIN;
	case MMC_SIGNAL_VOLTAGE_180:
		if (!(host->flags & SDHCI_SIGNALING_180))
			return -EINVAL;
		if (!IS_ERR(mmc->supply.vqmmc)) {
			ret = mmc_regulator_set_vqmmc(mmc, ios);
			if (ret < 0) {
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

		pr_warn("%s: 1.8V regulator output did not become stable\n",
			mmc_hostname(mmc));

		return -EAGAIN;
	case MMC_SIGNAL_VOLTAGE_120:
		if (!(host->flags & SDHCI_SIGNALING_120))
			return -EINVAL;
		if (!IS_ERR(mmc->supply.vqmmc)) {
			ret = mmc_regulator_set_vqmmc(mmc, ios);
			if (ret < 0) {
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

void sdhci_start_tuning(struct sdhci_host *host)
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
EXPORT_SYMBOL_GPL(sdhci_start_tuning);

void sdhci_end_tuning(struct sdhci_host *host)
{
	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
}
EXPORT_SYMBOL_GPL(sdhci_end_tuning);

void sdhci_reset_tuning(struct sdhci_host *host)
{
	u16 ctrl;

	ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl &= ~SDHCI_CTRL_TUNED_CLK;
	ctrl &= ~SDHCI_CTRL_EXEC_TUNING;
	sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
}
EXPORT_SYMBOL_GPL(sdhci_reset_tuning);

void sdhci_abort_tuning(struct sdhci_host *host, u32 opcode)
{
	sdhci_reset_tuning(host);

	sdhci_reset_for(host, TUNING_ABORT);

	sdhci_end_tuning(host);

	mmc_send_abort_tuning(host->mmc, opcode);
}
EXPORT_SYMBOL_GPL(sdhci_abort_tuning);

/*
 * We use sdhci_send_tuning() because mmc_send_tuning() is not a good fit. SDHCI
 * tuning command does not have a data payload (or rather the hardware does it
 * automatically) so mmc_send_tuning() will return -EIO. Also the tuning command
 * interrupt setup is different to other commands and there is no timeout
 * interrupt so special handling is needed.
 */
void sdhci_send_tuning(struct sdhci_host *host, u32 opcode)
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

	if (!sdhci_send_command_retry(host, &cmd, flags)) {
		spin_unlock_irqrestore(&host->lock, flags);
		host->tuning_done = 0;
		return;
	}

	host->cmd = NULL;

	sdhci_del_timer(host, &mrq);

	host->tuning_done = 0;

	spin_unlock_irqrestore(&host->lock, flags);

	/* Wait for Buffer Read Ready interrupt */
	wait_event_timeout(host->buf_ready_int, (host->tuning_done == 1),
			   msecs_to_jiffies(50));

}
EXPORT_SYMBOL_GPL(sdhci_send_tuning);

int __sdhci_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	int i;

	/*
	 * Issue opcode repeatedly till Execute Tuning is set to 0 or the number
	 * of loops reaches tuning loop count.
	 */
	for (i = 0; i < host->tuning_loop_count; i++) {
		u16 ctrl;

		sdhci_send_tuning(host, opcode);

		if (!host->tuning_done) {
			pr_debug("%s: Tuning timeout, falling back to fixed sampling clock\n",
				 mmc_hostname(host->mmc));
			sdhci_abort_tuning(host, opcode);
			return -ETIMEDOUT;
		}

		/* Spec does not require a delay between tuning cycles */
		if (host->tuning_delay > 0)
			mdelay(host->tuning_delay);

		ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		if (!(ctrl & SDHCI_CTRL_EXEC_TUNING)) {
			if (ctrl & SDHCI_CTRL_TUNED_CLK)
				return 0; /* Success! */
			break;
		}

	}

	pr_info("%s: Tuning failed, falling back to fixed sampling clock\n",
		mmc_hostname(host->mmc));
	sdhci_reset_tuning(host);
	return -EAGAIN;
}
EXPORT_SYMBOL_GPL(__sdhci_execute_tuning);

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
		fallthrough;

	default:
		goto out;
	}

	if (host->ops->platform_execute_tuning) {
		err = host->ops->platform_execute_tuning(host, opcode);
		goto out;
	}

	mmc->retune_period = tuning_count;

	if (host->tuning_delay < 0)
		host->tuning_delay = opcode == MMC_SEND_TUNING_BLOCK;

	sdhci_start_tuning(host);

	host->tuning_err = __sdhci_execute_tuning(host, opcode);

	sdhci_end_tuning(host);
out:
	host->flags &= ~SDHCI_HS400_TUNING;

	return err;
}
EXPORT_SYMBOL_GPL(sdhci_execute_tuning);

void sdhci_enable_preset_value(struct sdhci_host *host, bool enable)
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
EXPORT_SYMBOL_GPL(sdhci_enable_preset_value);

static void sdhci_post_req(struct mmc_host *mmc, struct mmc_request *mrq,
				int err)
{
	struct mmc_data *data = mrq->data;

	if (data->host_cookie != COOKIE_UNMAPPED)
		dma_unmap_sg(mmc_dev(mmc), data->sg, data->sg_len,
			     mmc_get_dma_dir(data));

	data->host_cookie = COOKIE_UNMAPPED;
}

static void sdhci_pre_req(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sdhci_host *host = mmc_priv(mmc);

	mrq->data->host_cookie = COOKIE_UNMAPPED;

	/*
	 * No pre-mapping in the pre hook if we're using the bounce buffer,
	 * for that we would need two bounce buffers since one buffer is
	 * in flight when this is getting called.
	 */
	if (host->flags & SDHCI_REQ_USE_DMA && !host->bounce_buffer)
		sdhci_pre_dma_transfer(host, mrq->data, COOKIE_PRE_MAPPED);
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
			mmc_hostname(mmc));
		pr_err("%s: Resetting controller.\n",
			mmc_hostname(mmc));

		sdhci_reset_for(host, CARD_REMOVED);

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
	.card_hw_reset	= sdhci_hw_reset,
	.enable_sdio_irq = sdhci_enable_sdio_irq,
	.ack_sdio_irq    = sdhci_ack_sdio_irq,
	.start_signal_voltage_switch	= sdhci_start_signal_voltage_switch,
	.prepare_hs400_tuning		= sdhci_prepare_hs400_tuning,
	.execute_tuning			= sdhci_execute_tuning,
	.card_event			= sdhci_card_event,
	.card_busy	= sdhci_card_busy,
};

/*****************************************************************************\
 *                                                                           *
 * Request done                                                              *
 *                                                                           *
\*****************************************************************************/

void sdhci_request_done_dma(struct sdhci_host *host, struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;

	if (data && data->host_cookie == COOKIE_MAPPED) {
		if (host->bounce_buffer) {
			/*
			 * On reads, copy the bounced data into the
			 * sglist
			 */
			if (mmc_get_dma_dir(data) == DMA_FROM_DEVICE) {
				unsigned int length = data->bytes_xfered;

				if (length > host->bounce_buffer_size) {
					pr_err("%s: bounce buffer is %u bytes but DMA claims to have transferred %u bytes\n",
					       mmc_hostname(host->mmc),
					       host->bounce_buffer_size,
					       data->bytes_xfered);
					/* Cap it down and continue */
					length = host->bounce_buffer_size;
				}
				dma_sync_single_for_cpu(mmc_dev(host->mmc),
							host->bounce_addr,
							host->bounce_buffer_size,
							DMA_FROM_DEVICE);
				sg_copy_from_buffer(data->sg,
						    data->sg_len,
						    host->bounce_buffer,
						    length);
			} else {
				/* No copying, just switch ownership */
				dma_sync_single_for_cpu(mmc_dev(host->mmc),
							host->bounce_addr,
							host->bounce_buffer_size,
							mmc_get_dma_dir(data));
			}
		} else {
			/* Unmap the raw data */
			dma_unmap_sg(mmc_dev(host->mmc), data->sg,
				     data->sg_len,
				     mmc_get_dma_dir(data));
		}
		data->host_cookie = COOKIE_UNMAPPED;
	}
}
EXPORT_SYMBOL_GPL(sdhci_request_done_dma);

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

		sdhci_reset_for(host, REQUEST_ERROR);

		host->pending_reset = false;
	}

	/*
	 * Always unmap the data buffers if they were mapped by
	 * sdhci_prepare_data() whenever we finish with a request.
	 * This avoids leaking DMA mappings on error.
	 */
	if (host->flags & SDHCI_REQ_USE_DMA) {
		struct mmc_data *data = mrq->data;

		if (host->use_external_dma && data &&
		    (mrq->cmd->error || data->error)) {
			struct dma_chan *chan = sdhci_external_dma_channel(host, data);

			host->mrqs_done[i] = NULL;
			spin_unlock_irqrestore(&host->lock, flags);
			dmaengine_terminate_sync(chan);
			spin_lock_irqsave(&host->lock, flags);
			sdhci_set_mrq_done(host, mrq);
		}

		sdhci_request_done_dma(host, mrq);
	}

	host->mrqs_done[i] = NULL;

	spin_unlock_irqrestore(&host->lock, flags);

	if (host->ops->request_done)
		host->ops->request_done(host, mrq);
	else
		mmc_request_done(host->mmc, mrq);

	return false;
}

void sdhci_complete_work(struct work_struct *work)
{
	struct sdhci_host *host = container_of(work, struct sdhci_host,
					       complete_work);

	while (!sdhci_request_done(host))
		;
}
EXPORT_SYMBOL_GPL(sdhci_complete_work);

static void sdhci_timeout_timer(struct timer_list *t)
{
	struct sdhci_host *host;
	unsigned long flags;

	host = timer_container_of(host, t, timer);

	spin_lock_irqsave(&host->lock, flags);

	if (host->cmd && !sdhci_data_line_cmd(host->cmd)) {
		pr_err("%s: Timeout waiting for hardware cmd interrupt.\n",
		       mmc_hostname(host->mmc));
		sdhci_err_stats_inc(host, REQ_TIMEOUT);
		sdhci_dumpregs(host);

		host->cmd->error = -ETIMEDOUT;
		sdhci_finish_mrq(host, host->cmd->mrq);
	}

	spin_unlock_irqrestore(&host->lock, flags);
}

static void sdhci_timeout_data_timer(struct timer_list *t)
{
	struct sdhci_host *host;
	unsigned long flags;

	host = timer_container_of(host, t, data_timer);

	spin_lock_irqsave(&host->lock, flags);

	if (host->data || host->data_cmd ||
	    (host->cmd && sdhci_data_line_cmd(host->cmd))) {
		pr_err("%s: Timeout waiting for hardware interrupt.\n",
		       mmc_hostname(host->mmc));
		sdhci_err_stats_inc(host, REQ_TIMEOUT);
		sdhci_dumpregs(host);

		if (host->data) {
			host->data->error = -ETIMEDOUT;
			__sdhci_finish_data(host, true);
			queue_work(host->complete_wq, &host->complete_work);
		} else if (host->data_cmd) {
			host->data_cmd->error = -ETIMEDOUT;
			sdhci_finish_mrq(host, host->data_cmd->mrq);
		} else {
			host->cmd->error = -ETIMEDOUT;
			sdhci_finish_mrq(host, host->cmd->mrq);
		}
	}

	spin_unlock_irqrestore(&host->lock, flags);
}

/*****************************************************************************\
 *                                                                           *
 * Interrupt handling                                                        *
 *                                                                           *
\*****************************************************************************/

static void sdhci_cmd_irq(struct sdhci_host *host, u32 intmask, u32 *intmask_p)
{
	/* Handle auto-CMD12 error */
	if (intmask & SDHCI_INT_AUTO_CMD_ERR && host->data_cmd) {
		struct mmc_request *mrq = host->data_cmd->mrq;
		u16 auto_cmd_status = sdhci_readw(host, SDHCI_AUTO_CMD_STATUS);
		int data_err_bit = (auto_cmd_status & SDHCI_AUTO_CMD_TIMEOUT) ?
				   SDHCI_INT_DATA_TIMEOUT :
				   SDHCI_INT_DATA_CRC;

		/* Treat auto-CMD12 error the same as data error */
		if (!mrq->sbc && (host->flags & SDHCI_AUTO_CMD12)) {
			*intmask_p |= data_err_bit;
			return;
		}
	}

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
		sdhci_err_stats_inc(host, UNEXPECTED_IRQ);
		sdhci_dumpregs(host);
		return;
	}

	if (intmask & (SDHCI_INT_TIMEOUT | SDHCI_INT_CRC |
		       SDHCI_INT_END_BIT | SDHCI_INT_INDEX)) {
		if (intmask & SDHCI_INT_TIMEOUT) {
			host->cmd->error = -ETIMEDOUT;
			sdhci_err_stats_inc(host, CMD_TIMEOUT);
		} else {
			host->cmd->error = -EILSEQ;
			if (!mmc_op_tuning(host->cmd->opcode))
				sdhci_err_stats_inc(host, CMD_CRC);
		}
		/* Treat data command CRC error the same as data CRC error */
		if (host->cmd->data &&
		    (intmask & (SDHCI_INT_CRC | SDHCI_INT_TIMEOUT)) ==
		     SDHCI_INT_CRC) {
			host->cmd = NULL;
			*intmask_p |= SDHCI_INT_DATA_CRC;
			return;
		}

		__sdhci_finish_mrq(host, host->cmd->mrq);
		return;
	}

	/* Handle auto-CMD23 error */
	if (intmask & SDHCI_INT_AUTO_CMD_ERR) {
		struct mmc_request *mrq = host->cmd->mrq;
		u16 auto_cmd_status = sdhci_readw(host, SDHCI_AUTO_CMD_STATUS);
		int err = (auto_cmd_status & SDHCI_AUTO_CMD_TIMEOUT) ?
			  -ETIMEDOUT :
			  -EILSEQ;

		sdhci_err_stats_inc(host, AUTO_CMD);

		if (sdhci_auto_cmd23(host, mrq)) {
			mrq->sbc->error = err;
			__sdhci_finish_mrq(host, mrq);
			return;
		}
	}

	if (intmask & SDHCI_INT_RESPONSE)
		sdhci_finish_command(host);
}

static void sdhci_adma_show_error(struct sdhci_host *host)
{
	void *desc = host->adma_table;
	dma_addr_t dma = host->adma_addr;

	sdhci_dumpregs(host);

	while (true) {
		struct sdhci_adma2_64_desc *dma_desc = desc;

		if (host->flags & SDHCI_USE_64_BIT_DMA)
			SDHCI_DUMP("%08llx: DMA 0x%08x%08x, LEN 0x%04x, Attr=0x%02x\n",
			    (unsigned long long)dma,
			    le32_to_cpu(dma_desc->addr_hi),
			    le32_to_cpu(dma_desc->addr_lo),
			    le16_to_cpu(dma_desc->len),
			    le16_to_cpu(dma_desc->cmd));
		else
			SDHCI_DUMP("%08llx: DMA 0x%08x, LEN 0x%04x, Attr=0x%02x\n",
			    (unsigned long long)dma,
			    le32_to_cpu(dma_desc->addr_lo),
			    le16_to_cpu(dma_desc->len),
			    le16_to_cpu(dma_desc->cmd));

		desc += host->desc_sz;
		dma += host->desc_sz;

		if (dma_desc->cmd & cpu_to_le16(ADMA2_END))
			break;
	}
}

static void sdhci_data_irq(struct sdhci_host *host, u32 intmask)
{
	/*
	 * CMD19 generates _only_ Buffer Read Ready interrupt if
	 * use sdhci_send_tuning.
	 * Need to exclude this case: PIO mode and use mmc_send_tuning,
	 * If not, sdhci_transfer_pio will never be called, make the
	 * SDHCI_INT_DATA_AVAIL always there, stuck in irq storm.
	 */
	if (intmask & SDHCI_INT_DATA_AVAIL && !host->data) {
		if (mmc_op_tuning(SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND)))) {
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
				sdhci_err_stats_inc(host, CMD_TIMEOUT);
				__sdhci_finish_mrq(host, data_cmd->mrq);
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

				__sdhci_finish_mrq(host, data_cmd->mrq);
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
		sdhci_err_stats_inc(host, UNEXPECTED_IRQ);
		sdhci_dumpregs(host);

		return;
	}

	if (intmask & SDHCI_INT_DATA_TIMEOUT) {
		host->data->error = -ETIMEDOUT;
		sdhci_err_stats_inc(host, DAT_TIMEOUT);
	} else if (intmask & SDHCI_INT_DATA_END_BIT) {
		host->data->error = -EILSEQ;
		if (!mmc_op_tuning(SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND))))
			sdhci_err_stats_inc(host, DAT_CRC);
	} else if ((intmask & (SDHCI_INT_DATA_CRC | SDHCI_INT_TUNING_ERROR)) &&
		SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND))
			!= MMC_BUS_TEST_R) {
		host->data->error = -EILSEQ;
		if (!mmc_op_tuning(SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND))))
			sdhci_err_stats_inc(host, DAT_CRC);
		if (intmask & SDHCI_INT_TUNING_ERROR) {
			u16 ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);

			ctrl2 &= ~SDHCI_CTRL_TUNED_CLK;
			sdhci_writew(host, ctrl2, SDHCI_HOST_CONTROL2);
		}
	} else if (intmask & SDHCI_INT_ADMA_ERROR) {
		pr_err("%s: ADMA error: 0x%08x\n", mmc_hostname(host->mmc),
		       intmask);
		sdhci_adma_show_error(host);
		sdhci_err_stats_inc(host, ADMA);
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
			dma_addr_t dmastart, dmanow;

			dmastart = sdhci_sdma_address(host);
			dmanow = dmastart + host->data->bytes_xfered;
			/*
			 * Force update to the next DMA block boundary.
			 */
			dmanow = (dmanow &
				~((dma_addr_t)SDHCI_DEFAULT_BOUNDARY_SIZE - 1)) +
				SDHCI_DEFAULT_BOUNDARY_SIZE;
			host->data->bytes_xfered = dmanow - dmastart;
			DBG("DMA base %pad, transferred 0x%06x bytes, next %pad\n",
			    &dmastart, host->data->bytes_xfered, &dmanow);
			sdhci_set_sdma_addr(host, dmanow);
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

static inline bool sdhci_defer_done(struct sdhci_host *host,
				    struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;

	return host->pending_reset || host->always_defer_done ||
	       ((host->flags & SDHCI_REQ_USE_DMA) && data &&
		data->host_cookie == COOKIE_MAPPED);
}

static irqreturn_t sdhci_irq(int irq, void *dev_id)
{
	struct mmc_request *mrqs_done[SDHCI_MAX_MRQS] = {0};
	irqreturn_t result = IRQ_NONE;
	struct sdhci_host *host = dev_id;
	u32 intmask, mask, unexpected = 0;
	int max_loops = 16;
	int i;

	spin_lock(&host->lock);

	if (host->runtime_suspended) {
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
			sdhci_cmd_irq(host, intmask & SDHCI_INT_CMD_MASK, &intmask);

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
			sdio_signal_irq(host->mmc);
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

	/* Determine if mrqs can be completed immediately */
	for (i = 0; i < SDHCI_MAX_MRQS; i++) {
		struct mmc_request *mrq = host->mrqs_done[i];

		if (!mrq)
			continue;

		if (sdhci_defer_done(host, mrq)) {
			result = IRQ_WAKE_THREAD;
		} else {
			mrqs_done[i] = mrq;
			host->mrqs_done[i] = NULL;
		}
	}
out:
	if (host->deferred_cmd)
		result = IRQ_WAKE_THREAD;

	spin_unlock(&host->lock);

	/* Process mrqs ready for immediate completion */
	for (i = 0; i < SDHCI_MAX_MRQS; i++) {
		if (!mrqs_done[i])
			continue;

		if (host->ops->request_done)
			host->ops->request_done(host, mrqs_done[i]);
		else
			mmc_request_done(host->mmc, mrqs_done[i]);
	}

	if (unexpected) {
		pr_err("%s: Unexpected interrupt 0x%08x.\n",
			   mmc_hostname(host->mmc), unexpected);
		sdhci_err_stats_inc(host, UNEXPECTED_IRQ);
		sdhci_dumpregs(host);
	}

	return result;
}

irqreturn_t sdhci_thread_irq(int irq, void *dev_id)
{
	struct sdhci_host *host = dev_id;
	struct mmc_command *cmd;
	unsigned long flags;
	u32 isr;

	while (!sdhci_request_done(host))
		;

	spin_lock_irqsave(&host->lock, flags);

	isr = host->thread_isr;
	host->thread_isr = 0;

	cmd = host->deferred_cmd;
	if (cmd && !sdhci_send_command_retry(host, cmd, flags))
		sdhci_finish_mrq(host, cmd->mrq);

	spin_unlock_irqrestore(&host->lock, flags);

	if (isr & (SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE)) {
		struct mmc_host *mmc = host->mmc;

		mmc->ops->card_event(mmc);
		mmc_detect_change(mmc, msecs_to_jiffies(200));
	}

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(sdhci_thread_irq);

/*****************************************************************************\
 *                                                                           *
 * Suspend/resume                                                            *
 *                                                                           *
\*****************************************************************************/

#ifdef CONFIG_PM

static bool sdhci_cd_irq_can_wakeup(struct sdhci_host *host)
{
	return mmc_card_is_removable(host->mmc) &&
	       !(host->quirks & SDHCI_QUIRK_BROKEN_CARD_DETECTION) &&
	       !mmc_host_can_gpio_cd(host->mmc);
}

/*
 * To enable wakeup events, the corresponding events have to be enabled in
 * the Interrupt Status Enable register too. See 'Table 1-6: Wakeup Signal
 * Table' in the SD Host Controller Standard Specification.
 * It is useless to restore SDHCI_INT_ENABLE state in
 * sdhci_disable_irq_wakeups() since it will be set by
 * sdhci_enable_card_detection() or sdhci_init().
 */
bool sdhci_enable_irq_wakeups(struct sdhci_host *host)
{
	u8 mask = SDHCI_WAKE_ON_INSERT | SDHCI_WAKE_ON_REMOVE |
		  SDHCI_WAKE_ON_INT;
	u32 irq_val = 0;
	u8 wake_val = 0;
	u8 val;

	if (sdhci_cd_irq_can_wakeup(host)) {
		wake_val |= SDHCI_WAKE_ON_INSERT | SDHCI_WAKE_ON_REMOVE;
		irq_val |= SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE;
	}

	if (mmc_card_wake_sdio_irq(host->mmc)) {
		wake_val |= SDHCI_WAKE_ON_INT;
		irq_val |= SDHCI_INT_CARD_INT;
	}

	if (!irq_val)
		return false;

	val = sdhci_readb(host, SDHCI_WAKE_UP_CONTROL);
	val &= ~mask;
	val |= wake_val;
	sdhci_writeb(host, val, SDHCI_WAKE_UP_CONTROL);

	sdhci_writel(host, irq_val, SDHCI_INT_ENABLE);

	host->irq_wake_enabled = !enable_irq_wake(host->irq);

	return host->irq_wake_enabled;
}
EXPORT_SYMBOL_GPL(sdhci_enable_irq_wakeups);

void sdhci_disable_irq_wakeups(struct sdhci_host *host)
{
	u8 val;
	u8 mask = SDHCI_WAKE_ON_INSERT | SDHCI_WAKE_ON_REMOVE
			| SDHCI_WAKE_ON_INT;

	val = sdhci_readb(host, SDHCI_WAKE_UP_CONTROL);
	val &= ~mask;
	sdhci_writeb(host, val, SDHCI_WAKE_UP_CONTROL);

	disable_irq_wake(host->irq);

	host->irq_wake_enabled = false;
}
EXPORT_SYMBOL_GPL(sdhci_disable_irq_wakeups);

int sdhci_suspend_host(struct sdhci_host *host)
{
	sdhci_disable_card_detection(host);

	mmc_retune_timer_stop(host->mmc);

	if (!device_may_wakeup(mmc_dev(host->mmc)) ||
	    !sdhci_enable_irq_wakeups(host)) {
		host->ier = 0;
		sdhci_writel(host, 0, SDHCI_INT_ENABLE);
		sdhci_writel(host, 0, SDHCI_SIGNAL_ENABLE);
		free_irq(host->irq, host);
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

	if ((mmc->pm_flags & MMC_PM_KEEP_POWER) &&
	    (host->quirks2 & SDHCI_QUIRK2_HOST_OFF_CARD_ON)) {
		/* Card keeps power but host controller does not */
		sdhci_init(host, 0);
		host->pwr = 0;
		host->clock = 0;
		host->reinit_uhs = true;
		mmc->ops->set_ios(mmc, &mmc->ios);
	} else {
		sdhci_init(host, (mmc->pm_flags & MMC_PM_KEEP_POWER));
	}

	if (host->irq_wake_enabled) {
		sdhci_disable_irq_wakeups(host);
	} else {
		ret = request_threaded_irq(host->irq, sdhci_irq,
					   sdhci_thread_irq, IRQF_SHARED,
					   mmc_hostname(mmc), host);
		if (ret)
			return ret;
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

int sdhci_runtime_resume_host(struct sdhci_host *host, int soft_reset)
{
	struct mmc_host *mmc = host->mmc;
	unsigned long flags;
	int host_flags = host->flags;

	if (host_flags & (SDHCI_USE_SDMA | SDHCI_USE_ADMA)) {
		if (host->ops->enable_dma)
			host->ops->enable_dma(host);
	}

	sdhci_init(host, soft_reset);

	if (mmc->ios.power_mode != MMC_POWER_UNDEFINED &&
	    mmc->ios.power_mode != MMC_POWER_OFF) {
		/* Force clock and power re-program */
		host->pwr = 0;
		host->clock = 0;
		host->reinit_uhs = true;
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
	if (sdio_irq_claimed(mmc))
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
	/*
	 * Host from V4.10 supports ADMA3 DMA type.
	 * ADMA3 performs integrated descriptor which is more suitable
	 * for cmd queuing to fetch both command and transfer descriptors.
	 */
	if (host->v4_mode && (host->caps1 & SDHCI_CAN_DO_ADMA3))
		ctrl |= SDHCI_CTRL_ADMA3;
	else if (host->flags & SDHCI_USE_64_BIT_DMA)
		ctrl |= SDHCI_CTRL_ADMA64;
	else
		ctrl |= SDHCI_CTRL_ADMA32;
	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);

	sdhci_writew(host, SDHCI_MAKE_BLKSZ(host->sdma_boundary, 512),
		     SDHCI_BLOCK_SIZE);

	/* Set maximum timeout */
	sdhci_set_timeout(host, NULL);

	host->ier = host->cqe_ier;

	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);

	host->cqe_on = true;

	pr_debug("%s: sdhci: CQE on, IRQ mask %#x, IRQ status %#x\n",
		 mmc_hostname(mmc), host->ier,
		 sdhci_readl(host, SDHCI_INT_STATUS));

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

	if (recovery)
		sdhci_reset_for(host, CQE_RECOVERY);

	pr_debug("%s: sdhci: CQE off, IRQ mask %#x, IRQ status %#x\n",
		 mmc_hostname(mmc), host->ier,
		 sdhci_readl(host, SDHCI_INT_STATUS));

	spin_unlock_irqrestore(&host->lock, flags);
}
EXPORT_SYMBOL_GPL(sdhci_cqe_disable);

bool sdhci_cqe_irq(struct sdhci_host *host, u32 intmask, int *cmd_error,
		   int *data_error)
{
	u32 mask;

	if (!host->cqe_on)
		return false;

	if (intmask & (SDHCI_INT_INDEX | SDHCI_INT_END_BIT | SDHCI_INT_CRC)) {
		*cmd_error = -EILSEQ;
		if (!mmc_op_tuning(SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND))))
			sdhci_err_stats_inc(host, CMD_CRC);
	} else if (intmask & SDHCI_INT_TIMEOUT) {
		*cmd_error = -ETIMEDOUT;
		sdhci_err_stats_inc(host, CMD_TIMEOUT);
	} else
		*cmd_error = 0;

	if (intmask & (SDHCI_INT_DATA_END_BIT | SDHCI_INT_DATA_CRC | SDHCI_INT_TUNING_ERROR)) {
		*data_error = -EILSEQ;
		if (!mmc_op_tuning(SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND))))
			sdhci_err_stats_inc(host, DAT_CRC);
	} else if (intmask & SDHCI_INT_DATA_TIMEOUT) {
		*data_error = -ETIMEDOUT;
		sdhci_err_stats_inc(host, DAT_TIMEOUT);
	} else if (intmask & SDHCI_INT_ADMA_ERROR) {
		*data_error = -EIO;
		sdhci_err_stats_inc(host, ADMA);
	} else
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
		sdhci_err_stats_inc(host, UNEXPECTED_IRQ);
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
	host->tuning_loop_count = MAX_TUNING_LOOP;

	host->sdma_boundary = SDHCI_DEFAULT_BOUNDARY_ARG;

	/*
	 * The DMA table descriptor count is calculated as the maximum
	 * number of segments times 2, to allow for an alignment
	 * descriptor for each segment, plus 1 for a nop end descriptor.
	 */
	host->adma_table_cnt = SDHCI_MAX_SEGS * 2 + 1;
	host->max_adma = 65536;

	host->max_timeout_count = 0xE;

	host->complete_work_fn = sdhci_complete_work;
	host->thread_irq_fn    = sdhci_thread_irq;

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

void __sdhci_read_caps(struct sdhci_host *host, const u16 *ver,
		       const u32 *caps, const u32 *caps1)
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

	sdhci_reset_for_all(host);

	if (host->v4_mode)
		sdhci_do_enable_v4_mode(host);

	device_property_read_u64(mmc_dev(host->mmc),
				 "sdhci-caps-mask", &dt_caps_mask);
	device_property_read_u64(mmc_dev(host->mmc),
				 "sdhci-caps", &dt_caps);

	v = ver ? *ver : sdhci_readw(host, SDHCI_HOST_VERSION);
	host->version = (v & SDHCI_SPEC_VER_MASK) >> SDHCI_SPEC_VER_SHIFT;

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

static void sdhci_allocate_bounce_buffer(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	unsigned int max_blocks;
	unsigned int bounce_size;
	int ret;

	/*
	 * Cap the bounce buffer at 64KB. Using a bigger bounce buffer
	 * has diminishing returns, this is probably because SD/MMC
	 * cards are usually optimized to handle this size of requests.
	 */
	bounce_size = SZ_64K;
	/*
	 * Adjust downwards to maximum request size if this is less
	 * than our segment size, else hammer down the maximum
	 * request size to the maximum buffer size.
	 */
	if (mmc->max_req_size < bounce_size)
		bounce_size = mmc->max_req_size;
	max_blocks = bounce_size / 512;

	/*
	 * When we just support one segment, we can get significant
	 * speedups by the help of a bounce buffer to group scattered
	 * reads/writes together.
	 */
	host->bounce_buffer = devm_kmalloc(mmc_dev(mmc),
					   bounce_size,
					   GFP_KERNEL);
	if (!host->bounce_buffer) {
		pr_err("%s: failed to allocate %u bytes for bounce buffer, falling back to single segments\n",
		       mmc_hostname(mmc),
		       bounce_size);
		/*
		 * Exiting with zero here makes sure we proceed with
		 * mmc->max_segs == 1.
		 */
		return;
	}

	host->bounce_addr = dma_map_single(mmc_dev(mmc),
					   host->bounce_buffer,
					   bounce_size,
					   DMA_BIDIRECTIONAL);
	ret = dma_mapping_error(mmc_dev(mmc), host->bounce_addr);
	if (ret) {
		devm_kfree(mmc_dev(mmc), host->bounce_buffer);
		host->bounce_buffer = NULL;
		/* Again fall back to max_segs == 1 */
		return;
	}

	host->bounce_buffer_size = bounce_size;

	/* Lie about this since we're bouncing */
	mmc->max_segs = max_blocks;
	mmc->max_seg_size = bounce_size;
	mmc->max_req_size = bounce_size;

	pr_info("%s bounce up to %u segments into one, max segment size %u bytes\n",
		mmc_hostname(mmc), max_blocks, bounce_size);
}

static inline bool sdhci_can_64bit_dma(struct sdhci_host *host)
{
	/*
	 * According to SD Host Controller spec v4.10, bit[27] added from
	 * version 4.10 in Capabilities Register is used as 64-bit System
	 * Address support for V4 mode.
	 */
	if (host->version >= SDHCI_SPEC_410 && host->v4_mode)
		return host->caps & SDHCI_CAN_64BIT_V4;

	return host->caps & SDHCI_CAN_64BIT;
}

int sdhci_setup_host(struct sdhci_host *host)
{
	struct mmc_host *mmc;
	u32 max_current_caps;
	unsigned int ocr_avail;
	unsigned int override_timeout_clk;
	u32 max_clk;
	int ret = 0;
	bool enable_vqmmc = false;

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
	if (!mmc->supply.vqmmc) {
		ret = mmc_regulator_get_supply(mmc);
		if (ret)
			return ret;
		enable_vqmmc  = true;
	}

	DBG("Version:   0x%08x | Present:  0x%08x\n",
	    sdhci_readw(host, SDHCI_HOST_VERSION),
	    sdhci_readl(host, SDHCI_PRESENT_STATE));
	DBG("Caps:      0x%08x | Caps_1:   0x%08x\n",
	    sdhci_readl(host, SDHCI_CAPABILITIES),
	    sdhci_readl(host, SDHCI_CAPABILITIES_1));

	sdhci_read_caps(host);

	override_timeout_clk = host->timeout_clk;

	if (host->version > SDHCI_SPEC_420) {
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

	if (sdhci_can_64bit_dma(host))
		host->flags |= SDHCI_USE_64_BIT_DMA;

	if (host->use_external_dma) {
		ret = sdhci_external_dma_init(host);
		if (ret == -EPROBE_DEFER)
			goto unreg;
		/*
		 * Fall back to use the DMA/PIO integrated in standard SDHCI
		 * instead of external DMA devices.
		 */
		else if (ret)
			sdhci_switch_external_dma(host, false);
		/* Disable internal DMA sources */
		else
			host->flags &= ~(SDHCI_USE_SDMA | SDHCI_USE_ADMA);
	}

	if (host->flags & (SDHCI_USE_SDMA | SDHCI_USE_ADMA)) {
		if (host->ops->set_dma_mask)
			ret = host->ops->set_dma_mask(host);
		else
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

	/* SDMA does not support 64-bit DMA if v4 mode not set */
	if ((host->flags & SDHCI_USE_64_BIT_DMA) && !host->v4_mode)
		host->flags &= ~SDHCI_USE_SDMA;

	if (host->flags & SDHCI_USE_ADMA) {
		dma_addr_t dma;
		void *buf;

		if (!(host->flags & SDHCI_USE_64_BIT_DMA))
			host->alloc_desc_sz = SDHCI_ADMA2_32_DESC_SZ;
		else if (!host->alloc_desc_sz)
			host->alloc_desc_sz = SDHCI_ADMA2_64_DESC_SZ(host);

		host->desc_sz = host->alloc_desc_sz;
		host->adma_table_sz = host->adma_table_cnt * host->desc_sz;

		host->align_buffer_sz = SDHCI_MAX_SEGS * SDHCI_ADMA2_ALIGN;
		/*
		 * Use zalloc to zero the reserved high 32-bits of 128-bit
		 * descriptors so that they never need to be written.
		 */
		buf = dma_alloc_coherent(mmc_dev(mmc),
					 host->align_buffer_sz + host->adma_table_sz,
					 &dma, GFP_KERNEL);
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
		host->max_clk = FIELD_GET(SDHCI_CLOCK_V3_BASE_MASK, host->caps);
	else
		host->max_clk = FIELD_GET(SDHCI_CLOCK_BASE_MASK, host->caps);

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
	host->clk_mul = FIELD_GET(SDHCI_CLOCK_MUL_MASK, host->caps1);

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
		if (host->clk_mul)
			max_clk = host->max_clk * host->clk_mul;
		/*
		 * Divided Clock Mode minimum clock rate is always less than
		 * Programmable Clock Mode minimum clock rate.
		 */
		mmc->f_min = host->max_clk / SDHCI_MAX_DIV_SPEC_300;
	} else
		mmc->f_min = host->max_clk / SDHCI_MAX_DIV_SPEC_200;

	if (!mmc->f_max || mmc->f_max > max_clk)
		mmc->f_max = max_clk;

	if (!(host->quirks & SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK)) {
		host->timeout_clk = FIELD_GET(SDHCI_TIMEOUT_CLK_MASK, host->caps);

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

	if (host->quirks2 & SDHCI_QUIRK2_DISABLE_HW_TIMEOUT &&
	    !host->ops->get_max_timeout_count)
		mmc->max_busy_timeout = 0;

	mmc->caps |= MMC_CAP_SDIO_IRQ | MMC_CAP_CMD23;
	mmc->caps2 |= MMC_CAP2_SDIO_IRQ_NOTHREAD;

	if (host->quirks & SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12)
		host->flags |= SDHCI_AUTO_CMD12;

	/*
	 * For v3 mode, Auto-CMD23 stuff only works in ADMA or PIO.
	 * For v4 mode, SDMA may use Auto-CMD23 as well.
	 */
	if ((host->version >= SDHCI_SPEC_300) &&
	    ((host->flags & SDHCI_USE_ADMA) ||
	     !(host->flags & SDHCI_USE_SDMA) || host->v4_mode) &&
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
	    mmc_gpio_get_cd(mmc) < 0)
		mmc->caps |= MMC_CAP_NEEDS_POLL;

	if (!IS_ERR(mmc->supply.vqmmc)) {
		if (enable_vqmmc) {
			ret = regulator_enable(mmc->supply.vqmmc);
			host->sdhci_core_to_disable_vqmmc = !ret;
		}

		/* If vqmmc provides no 1.8V signalling, then there's no UHS */
		if (!regulator_is_supported_voltage(mmc->supply.vqmmc, 1700000,
						    1950000))
			host->caps1 &= ~(SDHCI_SUPPORT_SDR104 |
					 SDHCI_SUPPORT_SDR50 |
					 SDHCI_SUPPORT_DDR50);

		/* In eMMC case vqmmc might be a fixed 1.8V regulator */
		if (!regulator_is_supported_voltage(mmc->supply.vqmmc, 2700000,
						    3600000))
			host->flags &= ~SDHCI_SIGNALING_330;

		if (ret) {
			pr_warn("%s: Failed to enable vqmmc regulator: %d\n",
				mmc_hostname(mmc), ret);
			mmc->supply.vqmmc = ERR_PTR(-EINVAL);
		}

	}

	if (host->quirks2 & SDHCI_QUIRK2_NO_1_8_V) {
		host->caps1 &= ~(SDHCI_SUPPORT_SDR104 | SDHCI_SUPPORT_SDR50 |
				 SDHCI_SUPPORT_DDR50);
		/*
		 * The SDHCI controller in a SoC might support HS200/HS400
		 * (indicated using mmc-hs200-1_8v/mmc-hs400-1_8v dt property),
		 * but if the board is modeled such that the IO lines are not
		 * connected to 1.8v then HS200/HS400 cannot be supported.
		 * Disable HS200/HS400 if the board does not have 1.8v connected
		 * to the IO lines. (Applicable for other modes in 1.8v)
		 */
		mmc->caps2 &= ~(MMC_CAP2_HSX00_1_8V | MMC_CAP2_HS400_ES);
		mmc->caps &= ~(MMC_CAP_1_8V_DDR | MMC_CAP_UHS);
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
	host->tuning_count = FIELD_GET(SDHCI_RETUNING_TIMER_COUNT_MASK,
				       host->caps1);

	/*
	 * In case Re-tuning Timer is not disabled, the actual value of
	 * re-tuning timer will be 2 ^ (n - 1).
	 */
	if (host->tuning_count)
		host->tuning_count = 1 << (host->tuning_count - 1);

	/* Re-tuning mode supported by the Host Controller */
	host->tuning_mode = FIELD_GET(SDHCI_RETUNING_MODE_MASK, host->caps1);

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
				FIELD_PREP(SDHCI_MAX_CURRENT_330_MASK, curr) |
				FIELD_PREP(SDHCI_MAX_CURRENT_300_MASK, curr) |
				FIELD_PREP(SDHCI_MAX_CURRENT_180_MASK, curr);
		}
	}

	if (host->caps & SDHCI_CAN_VDD_330) {
		ocr_avail |= MMC_VDD_32_33 | MMC_VDD_33_34;

		mmc->max_current_330 = FIELD_GET(SDHCI_MAX_CURRENT_330_MASK,
						 max_current_caps) *
						SDHCI_MAX_CURRENT_MULTIPLIER;
	}
	if (host->caps & SDHCI_CAN_VDD_300) {
		ocr_avail |= MMC_VDD_29_30 | MMC_VDD_30_31;

		mmc->max_current_300 = FIELD_GET(SDHCI_MAX_CURRENT_300_MASK,
						 max_current_caps) *
						SDHCI_MAX_CURRENT_MULTIPLIER;
	}
	if (host->caps & SDHCI_CAN_VDD_180) {
		ocr_avail |= MMC_VDD_165_195;

		mmc->max_current_180 = FIELD_GET(SDHCI_MAX_CURRENT_180_MASK,
						 max_current_caps) *
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
	 * Maximum number of sectors in one transfer. Limited by SDMA boundary
	 * size (512KiB). Note some tuning modes impose a 4MiB limit, but this
	 * is less anyway.
	 */
	mmc->max_req_size = 524288;

	/*
	 * Maximum number of segments. Depends on if the hardware
	 * can do scatter/gather or not.
	 */
	if (host->flags & SDHCI_USE_ADMA) {
		mmc->max_segs = SDHCI_MAX_SEGS;
	} else if (host->flags & SDHCI_USE_SDMA) {
		mmc->max_segs = 1;
		mmc->max_req_size = min_t(size_t, mmc->max_req_size,
					  dma_max_mapping_size(mmc_dev(mmc)));
	} else { /* PIO */
		mmc->max_segs = SDHCI_MAX_SEGS;
	}

	/*
	 * Maximum segment size. Could be one segment with the maximum number
	 * of bytes. When doing hardware scatter/gather, each entry cannot
	 * be larger than 64 KiB though.
	 */
	if (host->flags & SDHCI_USE_ADMA) {
		if (host->quirks & SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC) {
			host->max_adma = 65532; /* 32-bit alignment */
			mmc->max_seg_size = 65535;
			/*
			 * sdhci_adma_table_pre() expects to define 1 DMA
			 * descriptor per segment, so the maximum segment size
			 * is set accordingly. SDHCI allows up to 64KiB per DMA
			 * descriptor (16-bit field), but some controllers do
			 * not support "zero means 65536" reducing the maximum
			 * for them to 65535. That is a problem if PAGE_SIZE is
			 * 64KiB because the block layer does not support
			 * max_seg_size < PAGE_SIZE, however
			 * sdhci_adma_table_pre() has a workaround to handle
			 * that case, and split the descriptor. Refer also
			 * comment in sdhci_adma_table_pre().
			 */
			if (mmc->max_seg_size < PAGE_SIZE)
				mmc->max_seg_size = PAGE_SIZE;
		} else {
			mmc->max_seg_size = 65536;
		}
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

	if (mmc->max_segs == 1)
		/* This may alter mmc->*_blk_* parameters */
		sdhci_allocate_bounce_buffer(host);

	return 0;

unreg:
	if (host->sdhci_core_to_disable_vqmmc)
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

	if (host->sdhci_core_to_disable_vqmmc)
		regulator_disable(mmc->supply.vqmmc);

	if (host->align_buffer)
		dma_free_coherent(mmc_dev(mmc), host->align_buffer_sz +
				  host->adma_table_sz, host->align_buffer,
				  host->align_addr);

	if (host->use_external_dma)
		sdhci_external_dma_release(host);

	host->adma_table = NULL;
	host->align_buffer = NULL;
}
EXPORT_SYMBOL_GPL(sdhci_cleanup_host);

int __sdhci_add_host(struct sdhci_host *host)
{
	unsigned int flags = WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_HIGHPRI;
	struct mmc_host *mmc = host->mmc;
	int ret;

	if ((mmc->caps2 & MMC_CAP2_CQE) &&
	    (host->quirks & SDHCI_QUIRK_BROKEN_CQE)) {
		mmc->caps2 &= ~MMC_CAP2_CQE;
		mmc->cqe_ops = NULL;
	}

	host->complete_wq = alloc_workqueue("sdhci", flags, 0);
	if (!host->complete_wq)
		return -ENOMEM;

	INIT_WORK(&host->complete_work, host->complete_work_fn);

	timer_setup(&host->timer, sdhci_timeout_timer, 0);
	timer_setup(&host->data_timer, sdhci_timeout_data_timer, 0);

	init_waitqueue_head(&host->buf_ready_int);

	sdhci_init(host, 0);

	ret = request_threaded_irq(host->irq, sdhci_irq, host->thread_irq_fn,
				   IRQF_SHARED,	mmc_hostname(mmc), host);
	if (ret) {
		pr_err("%s: Failed to request IRQ %d: %d\n",
		       mmc_hostname(mmc), host->irq, ret);
		goto unwq;
	}

	ret = sdhci_led_register(host);
	if (ret) {
		pr_err("%s: Failed to register LED device: %d\n",
		       mmc_hostname(mmc), ret);
		goto unirq;
	}

	ret = mmc_add_host(mmc);
	if (ret)
		goto unled;

	pr_info("%s: SDHCI controller on %s [%s] using %s\n",
		mmc_hostname(mmc), host->hw_name, dev_name(mmc_dev(mmc)),
		host->use_external_dma ? "External DMA" :
		(host->flags & SDHCI_USE_ADMA) ?
		(host->flags & SDHCI_USE_64_BIT_DMA) ? "ADMA 64-bit" : "ADMA" :
		(host->flags & SDHCI_USE_SDMA) ? "DMA" : "PIO");

	sdhci_enable_card_detection(host);

	return 0;

unled:
	sdhci_led_unregister(host);
unirq:
	sdhci_reset_for_all(host);
	sdhci_writel(host, 0, SDHCI_INT_ENABLE);
	sdhci_writel(host, 0, SDHCI_SIGNAL_ENABLE);
	free_irq(host->irq, host);
unwq:
	destroy_workqueue(host->complete_wq);

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
		sdhci_reset_for_all(host);

	sdhci_writel(host, 0, SDHCI_INT_ENABLE);
	sdhci_writel(host, 0, SDHCI_SIGNAL_ENABLE);
	free_irq(host->irq, host);

	timer_delete_sync(&host->timer);
	timer_delete_sync(&host->data_timer);

	destroy_workqueue(host->complete_wq);

	if (host->sdhci_core_to_disable_vqmmc)
		regulator_disable(mmc->supply.vqmmc);

	if (host->align_buffer)
		dma_free_coherent(mmc_dev(mmc), host->align_buffer_sz +
				  host->adma_table_sz, host->align_buffer,
				  host->align_addr);

	if (host->use_external_dma)
		sdhci_external_dma_release(host);

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
