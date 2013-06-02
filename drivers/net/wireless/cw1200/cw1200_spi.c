/*
 * Mac80211 SPI driver for ST-Ericsson CW1200 device
 *
 * Copyright (c) 2011, Sagrad Inc.
 * Author:  Solomon Peachy <speachy@sagrad.com>
 *
 * Based on cw1200_sdio.c
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <net/mac80211.h>

#include <linux/spi/spi.h>
#include <linux/device.h>

#include "cw1200.h"
#include "hwbus.h"
#include <linux/platform_data/net-cw1200.h>
#include "hwio.h"

MODULE_AUTHOR("Solomon Peachy <speachy@sagrad.com>");
MODULE_DESCRIPTION("mac80211 ST-Ericsson CW1200 SPI driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:cw1200_wlan_spi");

/* #define SPI_DEBUG */

struct hwbus_priv {
	struct spi_device	*func;
	struct cw1200_common	*core;
	const struct cw1200_platform_data_spi *pdata;
	spinlock_t		lock; /* Serialize all bus operations */
	int claimed;
};

#define SDIO_TO_SPI_ADDR(addr) ((addr & 0x1f)>>2)
#define SET_WRITE 0x7FFF /* usage: and operation */
#define SET_READ 0x8000  /* usage: or operation */

/*
   Notes on byte ordering:
   LE:  B0 B1 B2 B3
   BE:  B3 B2 B1 B0

   Hardware expects 32-bit data to be written as 16-bit BE words:

   B1 B0 B3 B2

*/

static int cw1200_spi_memcpy_fromio(struct hwbus_priv *self,
				     unsigned int addr,
				     void *dst, int count)
{
	int ret, i;
	uint16_t regaddr;
	struct spi_message      m;

	struct spi_transfer     t_addr = {
		.tx_buf         = &regaddr,
		.len            = sizeof(regaddr),
	};
	struct spi_transfer     t_msg = {
		.rx_buf         = dst,
		.len            = count,
	};

	regaddr = (SDIO_TO_SPI_ADDR(addr))<<12;
	regaddr |= SET_READ;
	regaddr |= (count>>1);
	regaddr = cpu_to_le16(regaddr);

#ifdef SPI_DEBUG
	pr_info("READ : %04d from 0x%02x (%04x)\n", count, addr,
		le16_to_cpu(regaddr));
#endif

#if defined(__LITTLE_ENDIAN)
	/* We have to byteswap if the SPI bus is limited to 8b operation */
	if (self->func->bits_per_word == 8)
#endif
		regaddr = swab16(regaddr);

	spi_message_init(&m);
	spi_message_add_tail(&t_addr, &m);
	spi_message_add_tail(&t_msg, &m);
	ret = spi_sync(self->func, &m);

#ifdef SPI_DEBUG
	pr_info("READ : ");
	for (i = 0; i < t_addr.len; i++)
		printk("%02x ", ((u8 *)t_addr.tx_buf)[i]);
	printk(" : ");
	for (i = 0; i < t_msg.len; i++)
		printk("%02x ", ((u8 *)t_msg.rx_buf)[i]);
	printk("\n");
#endif

#if defined(__LITTLE_ENDIAN)
	/* We have to byteswap if the SPI bus is limited to 8b operation */
	if (self->func->bits_per_word == 8)
#endif
	{
		uint16_t *buf = (uint16_t *)dst;
		for (i = 0; i < ((count + 1) >> 1); i++)
			buf[i] = swab16(buf[i]);
	}

	return ret;
}

static int cw1200_spi_memcpy_toio(struct hwbus_priv *self,
				   unsigned int addr,
				   const void *src, int count)
{
	int rval, i;
	uint16_t regaddr;
	struct spi_transfer     t_addr = {
		.tx_buf         = &regaddr,
		.len            = sizeof(regaddr),
	};
	struct spi_transfer     t_msg = {
		.tx_buf         = src,
		.len            = count,
	};
	struct spi_message      m;

	regaddr = (SDIO_TO_SPI_ADDR(addr))<<12;
	regaddr &= SET_WRITE;
	regaddr |= (count>>1);
	regaddr = cpu_to_le16(regaddr);

#ifdef SPI_DEBUG
	pr_info("WRITE: %04d  to  0x%02x (%04x)\n", count, addr,
		le16_to_cpu(regaddr));
#endif

#if defined(__LITTLE_ENDIAN)
	/* We have to byteswap if the SPI bus is limited to 8b operation */
	if (self->func->bits_per_word == 8)
#endif
	{
		uint16_t *buf = (uint16_t *)src;
		regaddr = swab16(regaddr);
		for (i = 0; i < ((count + 1) >> 1); i++)
			buf[i] = swab16(buf[i]);
	}

#ifdef SPI_DEBUG
	pr_info("WRITE: ");
	for (i = 0; i < t_addr.len; i++)
		printk("%02x ", ((u8 *)t_addr.tx_buf)[i]);
	printk(" : ");
	for (i = 0; i < t_msg.len; i++)
		printk("%02x ", ((u8 *)t_msg.tx_buf)[i]);
	printk("\n");
#endif

	spi_message_init(&m);
	spi_message_add_tail(&t_addr, &m);
	spi_message_add_tail(&t_msg, &m);
	rval = spi_sync(self->func, &m);

#ifdef SPI_DEBUG
	pr_info("WROTE: %d\n", m.actual_length);
#endif

#if defined(__LITTLE_ENDIAN)
	/* We have to byteswap if the SPI bus is limited to 8b operation */
	if (self->func->bits_per_word == 8)
#endif
	{
		uint16_t *buf = (uint16_t *)src;
		for (i = 0; i < ((count + 1) >> 1); i++)
			buf[i] = swab16(buf[i]);
	}
	return rval;
}

static void cw1200_spi_lock(struct hwbus_priv *self)
{
	unsigned long flags;

	might_sleep();

	spin_lock_irqsave(&self->lock, flags);
	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (!self->claimed)
			break;
		spin_unlock_irqrestore(&self->lock, flags);
		schedule();
		spin_lock_irqsave(&self->lock, flags);
	}
	set_current_state(TASK_RUNNING);
	self->claimed = 1;
	spin_unlock_irqrestore(&self->lock, flags);

	return;
}

static void cw1200_spi_unlock(struct hwbus_priv *self)
{
	unsigned long flags;

	spin_lock_irqsave(&self->lock, flags);
	self->claimed = 0;
	spin_unlock_irqrestore(&self->lock, flags);
	return;
}

static irqreturn_t cw1200_spi_irq_handler(int irq, void *dev_id)
{
	struct hwbus_priv *self = dev_id;

	if (self->core) {
		cw1200_irq_handler(self->core);
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static int cw1200_spi_irq_subscribe(struct hwbus_priv *self)
{
	int ret;

	pr_debug("SW IRQ subscribe\n");

	ret = request_any_context_irq(self->func->irq, cw1200_spi_irq_handler,
				      IRQF_TRIGGER_HIGH,
				      "cw1200_wlan_irq", self);
	if (WARN_ON(ret < 0))
		goto exit;

	ret = enable_irq_wake(self->func->irq);
	if (WARN_ON(ret))
		goto free_irq;

	return 0;

free_irq:
	free_irq(self->func->irq, self);
exit:
	return ret;
}

static int cw1200_spi_irq_unsubscribe(struct hwbus_priv *self)
{
	int ret = 0;

	pr_debug("SW IRQ unsubscribe\n");
	disable_irq_wake(self->func->irq);
	free_irq(self->func->irq, self);

	return ret;
}

static int cw1200_spi_off(const struct cw1200_platform_data_spi *pdata)
{
	if (pdata->reset) {
		gpio_set_value(pdata->reset, 0);
		msleep(30); /* Min is 2 * CLK32K cycles */
		gpio_free(pdata->reset);
	}

	if (pdata->power_ctrl)
		pdata->power_ctrl(pdata, false);
	if (pdata->clk_ctrl)
		pdata->clk_ctrl(pdata, false);

	return 0;
}

static int cw1200_spi_on(const struct cw1200_platform_data_spi *pdata)
{
	/* Ensure I/Os are pulled low */
	if (pdata->reset) {
		gpio_request(pdata->reset, "cw1200_wlan_reset");
		gpio_direction_output(pdata->reset, 0);
	}
	if (pdata->powerup) {
		gpio_request(pdata->powerup, "cw1200_wlan_powerup");
		gpio_direction_output(pdata->powerup, 0);
	}
	if (pdata->reset || pdata->powerup)
		msleep(10); /* Settle time? */

	/* Enable 3v3 and 1v8 to hardware */
	if (pdata->power_ctrl) {
		if (pdata->power_ctrl(pdata, true)) {
			pr_err("power_ctrl() failed!\n");
			return -1;
		}
	}

	/* Enable CLK32K */
	if (pdata->clk_ctrl) {
		if (pdata->clk_ctrl(pdata, true)) {
			pr_err("clk_ctrl() failed!\n");
			return -1;
		}
		msleep(10); /* Delay until clock is stable for 2 cycles */
	}

	/* Enable POWERUP signal */
	if (pdata->powerup) {
		gpio_set_value(pdata->powerup, 1);
		msleep(250); /* or more..? */
	}
	/* Enable RSTn signal */
	if (pdata->reset) {
		gpio_set_value(pdata->reset, 1);
		msleep(50); /* Or more..? */
	}
	return 0;
}

static size_t cw1200_spi_align_size(struct hwbus_priv *self, size_t size)
{
	return size & 1 ? size + 1 : size;
}

static int cw1200_spi_pm(struct hwbus_priv *self, bool suspend)
{
	return irq_set_irq_wake(self->func->irq, suspend);
}

static struct hwbus_ops cw1200_spi_hwbus_ops = {
	.hwbus_memcpy_fromio	= cw1200_spi_memcpy_fromio,
	.hwbus_memcpy_toio	= cw1200_spi_memcpy_toio,
	.lock			= cw1200_spi_lock,
	.unlock			= cw1200_spi_unlock,
	.align_size		= cw1200_spi_align_size,
	.power_mgmt		= cw1200_spi_pm,
};

/* Probe Function to be called by SPI stack when device is discovered */
static int cw1200_spi_probe(struct spi_device *func)
{
	const struct cw1200_platform_data_spi *plat_data =
		func->dev.platform_data;
	struct hwbus_priv *self;
	int status;

	/* Sanity check speed */
	if (func->max_speed_hz > 52000000)
		func->max_speed_hz = 52000000;
	if (func->max_speed_hz < 1000000)
		func->max_speed_hz = 1000000;

	/* Fix up transfer size */
	if (plat_data->spi_bits_per_word)
		func->bits_per_word = plat_data->spi_bits_per_word;
	if (!func->bits_per_word)
		func->bits_per_word = 16;

	/* And finally.. */
	func->mode = SPI_MODE_0;

	pr_info("cw1200_wlan_spi: Probe called (CS %d M %d BPW %d CLK %d)\n",
		func->chip_select, func->mode, func->bits_per_word,
		func->max_speed_hz);

	if (cw1200_spi_on(plat_data)) {
		pr_err("spi_on() failed!\n");
		return -1;
	}

	if (spi_setup(func)) {
		pr_err("spi_setup() failed!\n");
		return -1;
	}

	self = kzalloc(sizeof(*self), GFP_KERNEL);
	if (!self) {
		pr_err("Can't allocate SPI hwbus_priv.");
		return -ENOMEM;
	}

	self->pdata = plat_data;
	self->func = func;
	spin_lock_init(&self->lock);

	spi_set_drvdata(func, self);

	status = cw1200_spi_irq_subscribe(self);

	status = cw1200_core_probe(&cw1200_spi_hwbus_ops,
				   self, &func->dev, &self->core,
				   self->pdata->ref_clk,
				   self->pdata->macaddr,
				   self->pdata->sdd_file,
				   self->pdata->have_5ghz);

	if (status) {
		cw1200_spi_irq_unsubscribe(self);
		cw1200_spi_off(plat_data);
		kfree(self);
	}

	return status;
}

/* Disconnect Function to be called by SPI stack when device is disconnected */
static int cw1200_spi_disconnect(struct spi_device *func)
{
	struct hwbus_priv *self = spi_get_drvdata(func);

	if (self) {
		cw1200_spi_irq_unsubscribe(self);
		if (self->core) {
			cw1200_core_release(self->core);
			self->core = NULL;
		}
		kfree(self);
	}
	cw1200_spi_off(func->dev.platform_data);

	return 0;
}

#ifdef CONFIG_PM
static int cw1200_spi_suspend(struct device *dev, pm_message_t state)
{
	struct hwbus_priv *self = spi_get_drvdata(to_spi_device(dev));

	if (!cw1200_can_suspend(self->core))
		return -EAGAIN;

	/* XXX notify host that we have to keep CW1200 powered on? */
	return 0;
}

static int cw1200_spi_resume(struct device *dev)
{
	return 0;
}
#endif

static struct spi_driver spi_driver = {
	.probe		= cw1200_spi_probe,
	.remove		= cw1200_spi_disconnect,
	.driver = {
		.name		= "cw1200_wlan_spi",
		.bus            = &spi_bus_type,
		.owner          = THIS_MODULE,
#ifdef CONFIG_PM
		.suspend        = cw1200_spi_suspend,
		.resume         = cw1200_spi_resume,
#endif
	},
};

module_spi_driver(spi_driver);
