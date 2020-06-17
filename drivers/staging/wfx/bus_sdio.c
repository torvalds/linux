// SPDX-License-Identifier: GPL-2.0-only
/*
 * SDIO interface.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/module.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/irq.h>

#include "bus.h"
#include "wfx.h"
#include "hwio.h"
#include "main.h"
#include "bh.h"

static const struct wfx_platform_data wfx_sdio_pdata = {
	.file_fw = "wfm_wf200",
	.file_pds = "wf200.pds",
};

struct wfx_sdio_priv {
	struct sdio_func *func;
	struct wfx_dev *core;
	u8 buf_id_tx;
	u8 buf_id_rx;
	int of_irq;
};

static int wfx_sdio_copy_from_io(void *priv, unsigned int reg_id,
				 void *dst, size_t count)
{
	struct wfx_sdio_priv *bus = priv;
	unsigned int sdio_addr = reg_id << 2;
	int ret;

	WARN(reg_id > 7, "chip only has 7 registers");
	WARN(((uintptr_t)dst) & 3, "unaligned buffer size");
	WARN(count & 3, "unaligned buffer address");

	/* Use queue mode buffers */
	if (reg_id == WFX_REG_IN_OUT_QUEUE)
		sdio_addr |= (bus->buf_id_rx + 1) << 7;
	ret = sdio_memcpy_fromio(bus->func, dst, sdio_addr, count);
	if (!ret && reg_id == WFX_REG_IN_OUT_QUEUE)
		bus->buf_id_rx = (bus->buf_id_rx + 1) % 4;

	return ret;
}

static int wfx_sdio_copy_to_io(void *priv, unsigned int reg_id,
			       const void *src, size_t count)
{
	struct wfx_sdio_priv *bus = priv;
	unsigned int sdio_addr = reg_id << 2;
	int ret;

	WARN(reg_id > 7, "chip only has 7 registers");
	WARN(((uintptr_t)src) & 3, "unaligned buffer size");
	WARN(count & 3, "unaligned buffer address");

	/* Use queue mode buffers */
	if (reg_id == WFX_REG_IN_OUT_QUEUE)
		sdio_addr |= bus->buf_id_tx << 7;
	// FIXME: discards 'const' qualifier for src
	ret = sdio_memcpy_toio(bus->func, sdio_addr, (void *)src, count);
	if (!ret && reg_id == WFX_REG_IN_OUT_QUEUE)
		bus->buf_id_tx = (bus->buf_id_tx + 1) % 32;

	return ret;
}

static void wfx_sdio_lock(void *priv)
{
	struct wfx_sdio_priv *bus = priv;

	sdio_claim_host(bus->func);
}

static void wfx_sdio_unlock(void *priv)
{
	struct wfx_sdio_priv *bus = priv;

	sdio_release_host(bus->func);
}

static void wfx_sdio_irq_handler(struct sdio_func *func)
{
	struct wfx_sdio_priv *bus = sdio_get_drvdata(func);

	wfx_bh_request_rx(bus->core);
}

static irqreturn_t wfx_sdio_irq_handler_ext(int irq, void *priv)
{
	struct wfx_sdio_priv *bus = priv;

	sdio_claim_host(bus->func);
	wfx_bh_request_rx(bus->core);
	sdio_release_host(bus->func);
	return IRQ_HANDLED;
}

static int wfx_sdio_irq_subscribe(void *priv)
{
	struct wfx_sdio_priv *bus = priv;
	u32 flags;
	int ret;
	u8 cccr;

	if (!bus->of_irq) {
		sdio_claim_host(bus->func);
		ret = sdio_claim_irq(bus->func, wfx_sdio_irq_handler);
		sdio_release_host(bus->func);
		return ret;
	}

	sdio_claim_host(bus->func);
	cccr = sdio_f0_readb(bus->func, SDIO_CCCR_IENx, NULL);
	cccr |= BIT(0);
	cccr |= BIT(bus->func->num);
	sdio_f0_writeb(bus->func, cccr, SDIO_CCCR_IENx, NULL);
	sdio_release_host(bus->func);
	flags = irq_get_trigger_type(bus->of_irq);
	if (!flags)
		flags = IRQF_TRIGGER_HIGH;
	flags |= IRQF_ONESHOT;
	return devm_request_threaded_irq(&bus->func->dev, bus->of_irq, NULL,
					 wfx_sdio_irq_handler_ext, flags,
					 "wfx", bus);
}

static int wfx_sdio_irq_unsubscribe(void *priv)
{
	struct wfx_sdio_priv *bus = priv;
	int ret;

	if (bus->of_irq)
		devm_free_irq(&bus->func->dev, bus->of_irq, bus);
	sdio_claim_host(bus->func);
	ret = sdio_release_irq(bus->func);
	sdio_release_host(bus->func);
	return ret;
}

static size_t wfx_sdio_align_size(void *priv, size_t size)
{
	struct wfx_sdio_priv *bus = priv;

	return sdio_align_size(bus->func, size);
}

static const struct hwbus_ops wfx_sdio_hwbus_ops = {
	.copy_from_io = wfx_sdio_copy_from_io,
	.copy_to_io = wfx_sdio_copy_to_io,
	.irq_subscribe = wfx_sdio_irq_subscribe,
	.irq_unsubscribe = wfx_sdio_irq_unsubscribe,
	.lock			= wfx_sdio_lock,
	.unlock			= wfx_sdio_unlock,
	.align_size		= wfx_sdio_align_size,
};

static const struct of_device_id wfx_sdio_of_match[] = {
	{ .compatible = "silabs,wfx-sdio" },
	{ .compatible = "silabs,wf200" },
	{ },
};
MODULE_DEVICE_TABLE(of, wfx_sdio_of_match);

static int wfx_sdio_probe(struct sdio_func *func,
			  const struct sdio_device_id *id)
{
	struct device_node *np = func->dev.of_node;
	struct wfx_sdio_priv *bus;
	int ret;

	if (func->num != 1) {
		dev_err(&func->dev, "SDIO function number is %d while it should always be 1 (unsupported chip?)\n",
			func->num);
		return -ENODEV;
	}

	bus = devm_kzalloc(&func->dev, sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return -ENOMEM;

	if (np) {
		if (!of_match_node(wfx_sdio_of_match, np)) {
			dev_warn(&func->dev, "no compatible device found in DT\n");
			return -ENODEV;
		}
		bus->of_irq = irq_of_parse_and_map(np, 0);
	} else {
		dev_warn(&func->dev,
			 "device is not declared in DT, features will be limited\n");
		// FIXME: ignore VID/PID and only rely on device tree
		// return -ENODEV;
	}

	bus->func = func;
	sdio_set_drvdata(func, bus);
	func->card->quirks |= MMC_QUIRK_LENIENT_FN0 |
			      MMC_QUIRK_BLKSZ_FOR_BYTE_MODE |
			      MMC_QUIRK_BROKEN_BYTE_MODE_512;

	sdio_claim_host(func);
	ret = sdio_enable_func(func);
	// Block of 64 bytes is more efficient than 512B for frame sizes < 4k
	sdio_set_block_size(func, 64);
	sdio_release_host(func);
	if (ret)
		goto err0;

	bus->core = wfx_init_common(&func->dev, &wfx_sdio_pdata,
				    &wfx_sdio_hwbus_ops, bus);
	if (!bus->core) {
		ret = -EIO;
		goto err1;
	}

	ret = wfx_probe(bus->core);
	if (ret)
		goto err1;

	return 0;

err1:
	sdio_claim_host(func);
	sdio_disable_func(func);
	sdio_release_host(func);
err0:
	return ret;
}

static void wfx_sdio_remove(struct sdio_func *func)
{
	struct wfx_sdio_priv *bus = sdio_get_drvdata(func);

	wfx_release(bus->core);
	sdio_claim_host(func);
	sdio_disable_func(func);
	sdio_release_host(func);
}

#define SDIO_VENDOR_ID_SILABS        0x0000
#define SDIO_DEVICE_ID_SILABS_WF200  0x1000
static const struct sdio_device_id wfx_sdio_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_SILABS, SDIO_DEVICE_ID_SILABS_WF200) },
	// FIXME: ignore VID/PID and only rely on device tree
	// { SDIO_DEVICE(SDIO_ANY_ID, SDIO_ANY_ID) },
	{ },
};
MODULE_DEVICE_TABLE(sdio, wfx_sdio_ids);

struct sdio_driver wfx_sdio_driver = {
	.name = "wfx-sdio",
	.id_table = wfx_sdio_ids,
	.probe = wfx_sdio_probe,
	.remove = wfx_sdio_remove,
	.drv = {
		.owner = THIS_MODULE,
		.of_match_table = wfx_sdio_of_match,
	}
};
