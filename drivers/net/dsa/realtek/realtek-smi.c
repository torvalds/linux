// SPDX-License-Identifier: GPL-2.0+
/* Realtek Simple Management Interface (SMI) driver
 * It can be discussed how "simple" this interface is.
 *
 * The SMI protocol piggy-backs the MDIO MDC and MDIO signals levels
 * but the protocol is not MDIO at all. Instead it is a Realtek
 * pecularity that need to bit-bang the lines in a special way to
 * communicate with the switch.
 *
 * ASICs we intend to support with this driver:
 *
 * RTL8366   - The original version, apparently
 * RTL8369   - Similar enough to have the same datsheet as RTL8366
 * RTL8366RB - Probably reads out "RTL8366 revision B", has a quite
 *             different register layout from the other two
 * RTL8366S  - Is this "RTL8366 super"?
 * RTL8367   - Has an OpenWRT driver as well
 * RTL8368S  - Seems to be an alternative name for RTL8366RB
 * RTL8370   - Also uses SMI
 *
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 * Copyright (C) 2010 Antti Seppälä <a.seppala@gmail.com>
 * Copyright (C) 2010 Roman Yeryomin <roman@advem.lv>
 * Copyright (C) 2011 Colin Leitner <colin.leitner@googlemail.com>
 * Copyright (C) 2009-2010 Gabor Juhos <juhosg@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/bitops.h>
#include <linux/if_bridge.h>

#include "realtek.h"
#include "realtek-smi.h"
#include "rtl83xx.h"

#define REALTEK_SMI_ACK_RETRY_COUNT		5

static inline void realtek_smi_clk_delay(struct realtek_priv *priv)
{
	ndelay(priv->variant->clk_delay);
}

static void realtek_smi_start(struct realtek_priv *priv)
{
	/* Set GPIO pins to output mode, with initial state:
	 * SCK = 0, SDA = 1
	 */
	gpiod_direction_output(priv->mdc, 0);
	gpiod_direction_output(priv->mdio, 1);
	realtek_smi_clk_delay(priv);

	/* CLK 1: 0 -> 1, 1 -> 0 */
	gpiod_set_value(priv->mdc, 1);
	realtek_smi_clk_delay(priv);
	gpiod_set_value(priv->mdc, 0);
	realtek_smi_clk_delay(priv);

	/* CLK 2: */
	gpiod_set_value(priv->mdc, 1);
	realtek_smi_clk_delay(priv);
	gpiod_set_value(priv->mdio, 0);
	realtek_smi_clk_delay(priv);
	gpiod_set_value(priv->mdc, 0);
	realtek_smi_clk_delay(priv);
	gpiod_set_value(priv->mdio, 1);
}

static void realtek_smi_stop(struct realtek_priv *priv)
{
	realtek_smi_clk_delay(priv);
	gpiod_set_value(priv->mdio, 0);
	gpiod_set_value(priv->mdc, 1);
	realtek_smi_clk_delay(priv);
	gpiod_set_value(priv->mdio, 1);
	realtek_smi_clk_delay(priv);
	gpiod_set_value(priv->mdc, 1);
	realtek_smi_clk_delay(priv);
	gpiod_set_value(priv->mdc, 0);
	realtek_smi_clk_delay(priv);
	gpiod_set_value(priv->mdc, 1);

	/* Add a click */
	realtek_smi_clk_delay(priv);
	gpiod_set_value(priv->mdc, 0);
	realtek_smi_clk_delay(priv);
	gpiod_set_value(priv->mdc, 1);

	/* Set GPIO pins to input mode */
	gpiod_direction_input(priv->mdio);
	gpiod_direction_input(priv->mdc);
}

static void realtek_smi_write_bits(struct realtek_priv *priv, u32 data, u32 len)
{
	for (; len > 0; len--) {
		realtek_smi_clk_delay(priv);

		/* Prepare data */
		gpiod_set_value(priv->mdio, !!(data & (1 << (len - 1))));
		realtek_smi_clk_delay(priv);

		/* Clocking */
		gpiod_set_value(priv->mdc, 1);
		realtek_smi_clk_delay(priv);
		gpiod_set_value(priv->mdc, 0);
	}
}

static void realtek_smi_read_bits(struct realtek_priv *priv, u32 len, u32 *data)
{
	gpiod_direction_input(priv->mdio);

	for (*data = 0; len > 0; len--) {
		u32 u;

		realtek_smi_clk_delay(priv);

		/* Clocking */
		gpiod_set_value(priv->mdc, 1);
		realtek_smi_clk_delay(priv);
		u = !!gpiod_get_value(priv->mdio);
		gpiod_set_value(priv->mdc, 0);

		*data |= (u << (len - 1));
	}

	gpiod_direction_output(priv->mdio, 0);
}

static int realtek_smi_wait_for_ack(struct realtek_priv *priv)
{
	int retry_cnt;

	retry_cnt = 0;
	do {
		u32 ack;

		realtek_smi_read_bits(priv, 1, &ack);
		if (ack == 0)
			break;

		if (++retry_cnt > REALTEK_SMI_ACK_RETRY_COUNT) {
			dev_err(priv->dev, "ACK timeout\n");
			return -ETIMEDOUT;
		}
	} while (1);

	return 0;
}

static int realtek_smi_write_byte(struct realtek_priv *priv, u8 data)
{
	realtek_smi_write_bits(priv, data, 8);
	return realtek_smi_wait_for_ack(priv);
}

static int realtek_smi_write_byte_noack(struct realtek_priv *priv, u8 data)
{
	realtek_smi_write_bits(priv, data, 8);
	return 0;
}

static int realtek_smi_read_byte0(struct realtek_priv *priv, u8 *data)
{
	u32 t;

	/* Read data */
	realtek_smi_read_bits(priv, 8, &t);
	*data = (t & 0xff);

	/* Send an ACK */
	realtek_smi_write_bits(priv, 0x00, 1);

	return 0;
}

static int realtek_smi_read_byte1(struct realtek_priv *priv, u8 *data)
{
	u32 t;

	/* Read data */
	realtek_smi_read_bits(priv, 8, &t);
	*data = (t & 0xff);

	/* Send an ACK */
	realtek_smi_write_bits(priv, 0x01, 1);

	return 0;
}

static int realtek_smi_read_reg(struct realtek_priv *priv, u32 addr, u32 *data)
{
	unsigned long flags;
	u8 lo = 0;
	u8 hi = 0;
	int ret;

	spin_lock_irqsave(&priv->lock, flags);

	realtek_smi_start(priv);

	/* Send READ command */
	ret = realtek_smi_write_byte(priv, priv->variant->cmd_read);
	if (ret)
		goto out;

	/* Set ADDR[7:0] */
	ret = realtek_smi_write_byte(priv, addr & 0xff);
	if (ret)
		goto out;

	/* Set ADDR[15:8] */
	ret = realtek_smi_write_byte(priv, addr >> 8);
	if (ret)
		goto out;

	/* Read DATA[7:0] */
	realtek_smi_read_byte0(priv, &lo);
	/* Read DATA[15:8] */
	realtek_smi_read_byte1(priv, &hi);

	*data = ((u32)lo) | (((u32)hi) << 8);

	ret = 0;

 out:
	realtek_smi_stop(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

static int realtek_smi_write_reg(struct realtek_priv *priv,
				 u32 addr, u32 data, bool ack)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&priv->lock, flags);

	realtek_smi_start(priv);

	/* Send WRITE command */
	ret = realtek_smi_write_byte(priv, priv->variant->cmd_write);
	if (ret)
		goto out;

	/* Set ADDR[7:0] */
	ret = realtek_smi_write_byte(priv, addr & 0xff);
	if (ret)
		goto out;

	/* Set ADDR[15:8] */
	ret = realtek_smi_write_byte(priv, addr >> 8);
	if (ret)
		goto out;

	/* Write DATA[7:0] */
	ret = realtek_smi_write_byte(priv, data & 0xff);
	if (ret)
		goto out;

	/* Write DATA[15:8] */
	if (ack)
		ret = realtek_smi_write_byte(priv, data >> 8);
	else
		ret = realtek_smi_write_byte_noack(priv, data >> 8);
	if (ret)
		goto out;

	ret = 0;

 out:
	realtek_smi_stop(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

/* There is one single case when we need to use this accessor and that
 * is when issueing soft reset. Since the device reset as soon as we write
 * that bit, no ACK will come back for natural reasons.
 */
static int realtek_smi_write_reg_noack(void *ctx, u32 reg, u32 val)
{
	return realtek_smi_write_reg(ctx, reg, val, false);
}

/* Regmap accessors */

static int realtek_smi_write(void *ctx, u32 reg, u32 val)
{
	struct realtek_priv *priv = ctx;

	return realtek_smi_write_reg(priv, reg, val, true);
}

static int realtek_smi_read(void *ctx, u32 reg, u32 *val)
{
	struct realtek_priv *priv = ctx;

	return realtek_smi_read_reg(priv, reg, val);
}

static const struct realtek_interface_info realtek_smi_info = {
	.reg_read = realtek_smi_read,
	.reg_write = realtek_smi_write,
};

/**
 * realtek_smi_probe() - Probe a platform device for an SMI-connected switch
 * @pdev: platform_device to probe on.
 *
 * This function should be used as the .probe in a platform_driver. After
 * calling the common probe function for both interfaces, it initializes the
 * values specific for SMI-connected devices. Finally, it calls a common
 * function to register the DSA switch.
 *
 * Context: Can sleep. Takes and releases priv->map_lock.
 * Return: Returns 0 on success, a negative error on failure.
 */
int realtek_smi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct realtek_priv *priv;
	int ret;

	priv = rtl83xx_probe(dev, &realtek_smi_info);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	/* Fetch MDIO pins */
	priv->mdc = devm_gpiod_get_optional(dev, "mdc", GPIOD_OUT_LOW);
	if (IS_ERR(priv->mdc)) {
		rtl83xx_remove(priv);
		return PTR_ERR(priv->mdc);
	}

	priv->mdio = devm_gpiod_get_optional(dev, "mdio", GPIOD_OUT_LOW);
	if (IS_ERR(priv->mdio)) {
		rtl83xx_remove(priv);
		return PTR_ERR(priv->mdio);
	}

	priv->write_reg_noack = realtek_smi_write_reg_noack;

	ret = rtl83xx_register_switch(priv);
	if (ret) {
		rtl83xx_remove(priv);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(realtek_smi_probe, "REALTEK_DSA");

/**
 * realtek_smi_remove() - Remove the driver of a SMI-connected switch
 * @pdev: platform_device to be removed.
 *
 * This function should be used as the .remove in a platform_driver. First
 * it unregisters the DSA switch and then it calls the common remove function.
 *
 * Context: Can sleep.
 * Return: Nothing.
 */
void realtek_smi_remove(struct platform_device *pdev)
{
	struct realtek_priv *priv = platform_get_drvdata(pdev);

	if (!priv)
		return;

	rtl83xx_unregister_switch(priv);

	rtl83xx_remove(priv);
}
EXPORT_SYMBOL_NS_GPL(realtek_smi_remove, "REALTEK_DSA");

/**
 * realtek_smi_shutdown() - Shutdown the driver of a SMI-connected switch
 * @pdev: platform_device shutting down.
 *
 * This function should be used as the .shutdown in a platform_driver. It calls
 * the common shutdown function.
 *
 * Context: Can sleep.
 * Return: Nothing.
 */
void realtek_smi_shutdown(struct platform_device *pdev)
{
	struct realtek_priv *priv = platform_get_drvdata(pdev);

	if (!priv)
		return;

	rtl83xx_shutdown(priv);
}
EXPORT_SYMBOL_NS_GPL(realtek_smi_shutdown, "REALTEK_DSA");
