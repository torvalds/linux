/*
 * Allwinner SUNXI MUSB platform setup
 *
 * Copyright © 2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#define DEBUG 1

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/usb/musb.h>

#include <plat/sys_config.h>
#include <mach/platform.h>
#include <mach/clock.h>
#include <mach/irqs.h>

#include "../../power/axp_power/axp-gpio.h"
#include "sunxi_musb_plat.h"

struct sunxi_musb_gpio {
	unsigned int	Drv_vbus_Handle;
	user_gpio_set_t	drv_vbus_gpio_set;
};

struct sunxi_musb_board_priv {
	struct device *dev;
	struct sunxi_musb_gpio gpio;
};

static s32 pin_init(struct sunxi_musb_board_priv *priv)
{
	struct sunxi_musb_gpio *sw_hcd_io = &priv->gpio;
	s32 ret = 0;

	pr_debug("%s():\n", __func__);

	/* request gpio */
	ret = script_parser_fetch("usbc0", "usb_drv_vbus_gpio",
				  (int *)&sw_hcd_io->drv_vbus_gpio_set, 64);
	if (ret != 0)
		dev_warn(priv->dev, "get usbc0(drv vbus) id failed\n");

	if (!sw_hcd_io->drv_vbus_gpio_set.port) {
		dev_err(priv->dev, "usbc0(drv vbus) is invalid\n");
		sw_hcd_io->Drv_vbus_Handle = 0;
		return 0;
	}

	if (sw_hcd_io->drv_vbus_gpio_set.port == 0xffff) { /* power */
		if (sw_hcd_io->drv_vbus_gpio_set.mul_sel == 0 ||
				sw_hcd_io->drv_vbus_gpio_set.mul_sel == 1) {
			axp_gpio_set_io(sw_hcd_io->drv_vbus_gpio_set.port_num,
					sw_hcd_io->drv_vbus_gpio_set.mul_sel);
			axp_gpio_set_value(
					sw_hcd_io->drv_vbus_gpio_set.port_num,
					!sw_hcd_io->drv_vbus_gpio_set.data);

			return 100 + sw_hcd_io->drv_vbus_gpio_set.port_num;
		} else {
			dev_err(priv->dev, "unknown gpio mul_sel(%d)\n",
				sw_hcd_io->drv_vbus_gpio_set.mul_sel);
			return 0;
		}
	} else {  /* axp */
		sw_hcd_io->Drv_vbus_Handle = sunxi_gpio_request_array(
				&sw_hcd_io->drv_vbus_gpio_set, 1);
		if (sw_hcd_io->Drv_vbus_Handle == 0) {
			dev_err(priv->dev, "gpio_request failed\n");
			return -1;
		}

		/* set config, ouput */
		gpio_set_one_pin_io_status(sw_hcd_io->Drv_vbus_Handle,
					   !sw_hcd_io->drv_vbus_gpio_set.data,
					   NULL);

		/* reserved is pull down */
		gpio_set_one_pin_pull(sw_hcd_io->Drv_vbus_Handle, 2, NULL);
	}

	return 0;
}

static s32 pin_exit(struct sunxi_musb_board_priv *priv)
{
	struct sunxi_musb_gpio *sw_hcd_io = &priv->gpio;

	pr_debug("%s():\n", __func__);

	if (sw_hcd_io->Drv_vbus_Handle) {
		if (sw_hcd_io->drv_vbus_gpio_set.port == 0xffff) { /* power */
			axp_gpio_set_io(sw_hcd_io->drv_vbus_gpio_set.port_num,
					sw_hcd_io->drv_vbus_gpio_set.mul_sel);
			axp_gpio_set_value(
					sw_hcd_io->drv_vbus_gpio_set.port_num,
					sw_hcd_io->drv_vbus_gpio_set.data);
		} else {
			gpio_release(sw_hcd_io->Drv_vbus_Handle, 0);
		}
	}

	sw_hcd_io->Drv_vbus_Handle = 0;

	return 0;
}

static int sunxi_musb_board_priv_set_phy_power(
				struct sunxi_musb_board_priv *priv, int is_on)
{
	struct sunxi_musb_gpio *sw_hcd_io = &priv->gpio;
	int on_off;

	pr_debug("%s():\n", __func__);

	if (sw_hcd_io->Drv_vbus_Handle == 0) {
		dev_info(priv->dev, "wrn: sw_hcd_io->drv_vbus_Handle is null\n");
		return -EIO;
	}

	/* set power */
	on_off = !!is_on;
	if (sw_hcd_io->drv_vbus_gpio_set.data != 0)
		on_off = !on_off; /* inverse */

	if (sw_hcd_io->drv_vbus_gpio_set.port == 0xffff)
		axp_gpio_set_value(sw_hcd_io->drv_vbus_gpio_set.port_num,
				   on_off);
	else
		gpio_write_one_pin_value(sw_hcd_io->Drv_vbus_Handle, on_off,
					 NULL);

	return 0;
}

static void USBC_ConfigFIFO_Base(void)
{
	static DEFINE_SPINLOCK(lock);
	unsigned long flags = 0;
	u32 reg_value;

	/* config usb fifo, 8kb mode */
	spin_lock_irqsave(&lock, flags);

	reg_value = __raw_readl((void __iomem *)SW_VA_SRAM_IO_BASE + 0x04);
	reg_value &= ~(0x03 << 0);
	reg_value |= (1 << 0);
	__raw_writel(reg_value, (void __iomem *)SW_VA_SRAM_IO_BASE + 0x04);

	spin_unlock_irqrestore(&lock, flags);
}

static struct sunxi_musb_board_priv *sunxi_musb_board_priv_init(
							struct device *dev)
{
	struct sunxi_musb_board_priv *priv;
	int ret;

	pr_debug("%s():\n", __func__);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->dev = dev;

	USBC_ConfigFIFO_Base();

	ret = pin_init(priv);
	if (ret < 0) {
		dev_err(priv->dev, "pin_init failed\n");
		kfree(priv);
		return ERR_PTR(-ENOMEM);
	}

	return priv;
}

static void sunxi_musb_board_priv_exit(struct sunxi_musb_board_priv *priv)
{
	pr_debug("%s():\n", __func__);

	pin_exit(priv);
	kfree(priv);
}

static struct resource sunxi_musb_resources[] = {
	[0] = {
		.start = SW_PA_USB0_IO_BASE,
		.end = SW_PA_USB0_IO_BASE + 0xfff,
		.flags = IORESOURCE_MEM,
		.name = "sunxi_musb0-mem",
	},
	[1] = {
		.start = SW_INT_IRQNO_USB0,
		.end = SW_INT_IRQNO_USB0,
		.flags = IORESOURCE_IRQ,
		.name = "mc", /* hardcoded in musb */
	},
};

/* Can support a maximum ep number, ep0 ~ 5 */
#define USBC_MAX_EP_NUM		6

static struct musb_fifo_cfg sunxi_musb_mode_cfg[] = {
	{ .hw_ep_num =  1, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  1, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  2, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  2, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  3, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  3, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  4, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  4, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  5, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  5, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
};

static struct musb_hdrc_config sunxi_musb_config = {
	.multipoint	= 1,
	.dyn_fifo	= 1,
	.soft_con	= 1,
	.dma		= 0,

	.num_eps	= USBC_MAX_EP_NUM,
	.ram_bits	= 11,

	.fifo_cfg	= sunxi_musb_mode_cfg,
	.fifo_cfg_size	= ARRAY_SIZE(sunxi_musb_mode_cfg),
};

static struct sunxi_musb_board_data sunxi_musb_board_data = {
	.init		= sunxi_musb_board_priv_init,
	.exit		= sunxi_musb_board_priv_exit,
	.set_phy_power	= sunxi_musb_board_priv_set_phy_power,
};

static struct musb_hdrc_platform_data sunxi_musb_plat = {
	.mode		= MUSB_HOST,
	.config		= &sunxi_musb_config,
	.board_data	= &sunxi_musb_board_data,
};

static struct platform_device sunxi_musb_device = {
	.name	= "sunxi_musb",
	.id	= -1,

	.dev = {
		.platform_data = &sunxi_musb_plat,
	},

	.resource = sunxi_musb_resources,
	.num_resources = ARRAY_SIZE(sunxi_musb_resources),
};

int register_musb_device(void)
{
	return platform_device_register(&sunxi_musb_device);
}

void unregister_musb_device(void)
{
	platform_device_unregister(&sunxi_musb_device);
}
