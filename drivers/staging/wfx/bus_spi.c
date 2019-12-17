// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI interface.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2011, Sagrad Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/of.h>

#include "bus.h"
#include "wfx.h"
#include "hwio.h"
#include "main.h"
#include "bh.h"

static int gpio_reset = -2;
module_param(gpio_reset, int, 0644);
MODULE_PARM_DESC(gpio_reset, "gpio number for reset. -1 for none.");

#define SET_WRITE 0x7FFF        /* usage: and operation */
#define SET_READ 0x8000         /* usage: or operation */

static const struct wfx_platform_data wfx_spi_pdata = {
	.file_fw = "wfm_wf200",
	.file_pds = "wf200.pds",
	.use_rising_clk = true,
};

struct wfx_spi_priv {
	struct spi_device *func;
	struct wfx_dev *core;
	struct gpio_desc *gpio_reset;
	struct work_struct request_rx;
	bool need_swab;
};

/*
 * WFx chip read data 16bits at time and place them directly into (little
 * endian) CPU register. So, chip expect byte order like "B1 B0 B3 B2" (while
 * LE is "B0 B1 B2 B3" and BE is "B3 B2 B1 B0")
 *
 * A little endian host with bits_per_word == 16 should do the right job
 * natively. The code below to support big endian host and commonly used SPI
 * 8bits.
 */
static int wfx_spi_copy_from_io(void *priv, unsigned int addr,
				void *dst, size_t count)
{
	struct wfx_spi_priv *bus = priv;
	u16 regaddr = (addr << 12) | (count / 2) | SET_READ;
	struct spi_message      m;
	struct spi_transfer     t_addr = {
		.tx_buf         = &regaddr,
		.len            = sizeof(regaddr),
	};
	struct spi_transfer     t_msg = {
		.rx_buf         = dst,
		.len            = count,
	};
	u16 *dst16 = dst;
	int ret, i;

	WARN(count % 2, "buffer size must be a multiple of 2");

	cpu_to_le16s(&regaddr);
	if (bus->need_swab)
		swab16s(&regaddr);

	spi_message_init(&m);
	spi_message_add_tail(&t_addr, &m);
	spi_message_add_tail(&t_msg, &m);
	ret = spi_sync(bus->func, &m);

	if (bus->need_swab && addr == WFX_REG_CONFIG)
		for (i = 0; i < count / 2; i++)
			swab16s(&dst16[i]);
	return ret;
}

static int wfx_spi_copy_to_io(void *priv, unsigned int addr,
			      const void *src, size_t count)
{
	struct wfx_spi_priv *bus = priv;
	u16 regaddr = (addr << 12) | (count / 2);
	// FIXME: use a bounce buffer
	u16 *src16 = (void *)src;
	int ret, i;
	struct spi_message      m;
	struct spi_transfer     t_addr = {
		.tx_buf         = &regaddr,
		.len            = sizeof(regaddr),
	};
	struct spi_transfer     t_msg = {
		.tx_buf         = src,
		.len            = count,
	};

	WARN(count % 2, "buffer size must be a multiple of 2");
	WARN(regaddr & SET_READ, "bad addr or size overflow");

	cpu_to_le16s(&regaddr);

	// Register address and CONFIG content always use 16bit big endian
	// ("BADC" order)
	if (bus->need_swab)
		swab16s(&regaddr);
	if (bus->need_swab && addr == WFX_REG_CONFIG)
		for (i = 0; i < count / 2; i++)
			swab16s(&src16[i]);

	spi_message_init(&m);
	spi_message_add_tail(&t_addr, &m);
	spi_message_add_tail(&t_msg, &m);
	ret = spi_sync(bus->func, &m);

	if (bus->need_swab && addr == WFX_REG_CONFIG)
		for (i = 0; i < count / 2; i++)
			swab16s(&src16[i]);
	return ret;
}

static void wfx_spi_lock(void *priv)
{
}

static void wfx_spi_unlock(void *priv)
{
}

static irqreturn_t wfx_spi_irq_handler(int irq, void *priv)
{
	struct wfx_spi_priv *bus = priv;

	if (!bus->core) {
		WARN(!bus->core, "race condition in driver init/deinit");
		return IRQ_NONE;
	}
	queue_work(system_highpri_wq, &bus->request_rx);
	return IRQ_HANDLED;
}

static void wfx_spi_request_rx(struct work_struct *work)
{
	struct wfx_spi_priv *bus =
		container_of(work, struct wfx_spi_priv, request_rx);

	wfx_bh_request_rx(bus->core);
}

static size_t wfx_spi_align_size(void *priv, size_t size)
{
	// Most of SPI controllers avoid DMA if buffer size is not 32bit aligned
	return ALIGN(size, 4);
}

static const struct hwbus_ops wfx_spi_hwbus_ops = {
	.copy_from_io = wfx_spi_copy_from_io,
	.copy_to_io = wfx_spi_copy_to_io,
	.lock			= wfx_spi_lock,
	.unlock			= wfx_spi_unlock,
	.align_size		= wfx_spi_align_size,
};

static int wfx_spi_probe(struct spi_device *func)
{
	struct wfx_spi_priv *bus;
	int ret;

	if (!func->bits_per_word)
		func->bits_per_word = 16;
	ret = spi_setup(func);
	if (ret)
		return ret;
	// Trace below is also displayed by spi_setup() if compiled with DEBUG
	dev_dbg(&func->dev, "SPI params: CS=%d, mode=%d bits/word=%d speed=%d\n",
		func->chip_select, func->mode, func->bits_per_word,
		func->max_speed_hz);
	if (func->bits_per_word != 16 && func->bits_per_word != 8)
		dev_warn(&func->dev, "unusual bits/word value: %d\n",
			 func->bits_per_word);
	if (func->max_speed_hz > 50000000)
		dev_warn(&func->dev, "%dHz is a very high speed\n",
			 func->max_speed_hz);

	bus = devm_kzalloc(&func->dev, sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return -ENOMEM;
	bus->func = func;
	if (func->bits_per_word == 8 || IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		bus->need_swab = true;
	spi_set_drvdata(func, bus);

	bus->gpio_reset = wfx_get_gpio(&func->dev, gpio_reset, "reset");
	if (!bus->gpio_reset) {
		dev_warn(&func->dev, "try to load firmware anyway\n");
	} else {
		gpiod_set_value(bus->gpio_reset, 0);
		udelay(100);
		gpiod_set_value(bus->gpio_reset, 1);
		udelay(2000);
	}

	ret = devm_request_irq(&func->dev, func->irq, wfx_spi_irq_handler,
			       IRQF_TRIGGER_RISING, "wfx", bus);
	if (ret)
		return ret;

	INIT_WORK(&bus->request_rx, wfx_spi_request_rx);
	bus->core = wfx_init_common(&func->dev, &wfx_spi_pdata,
				    &wfx_spi_hwbus_ops, bus);
	if (!bus->core)
		return -EIO;

	ret = wfx_probe(bus->core);
	if (ret)
		wfx_free_common(bus->core);

	return ret;
}

static int wfx_spi_remove(struct spi_device *func)
{
	struct wfx_spi_priv *bus = spi_get_drvdata(func);

	wfx_release(bus->core);
	wfx_free_common(bus->core);
	// A few IRQ will be sent during device release. Hopefully, no IRQ
	// should happen after wdev/wvif are released.
	devm_free_irq(&func->dev, func->irq, bus);
	flush_work(&bus->request_rx);
	return 0;
}

/*
 * For dynamic driver binding, kernel does not use OF to match driver. It only
 * use modalias and modalias is a copy of 'compatible' DT node with vendor
 * stripped.
 */
static const struct spi_device_id wfx_spi_id[] = {
	{ "wfx-spi", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, wfx_spi_id);

#ifdef CONFIG_OF
static const struct of_device_id wfx_spi_of_match[] = {
	{ .compatible = "silabs,wfx-spi" },
	{ },
};
MODULE_DEVICE_TABLE(of, wfx_spi_of_match);
#endif

struct spi_driver wfx_spi_driver = {
	.driver = {
		.name = "wfx-spi",
		.of_match_table = of_match_ptr(wfx_spi_of_match),
	},
	.id_table = wfx_spi_id,
	.probe = wfx_spi_probe,
	.remove = wfx_spi_remove,
};
