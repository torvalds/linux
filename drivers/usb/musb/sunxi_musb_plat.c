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
	DEFINE_RES_MEM_NAMED(SW_PA_USB0_IO_BASE, 0x1000, "sunxi_musb0-mem"),
	DEFINE_RES_IRQ_NAMED(SW_INT_IRQNO_USB0, "mc")
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

/* For testing peripheral mode, set this '1'. */
#define MUSB_SUNXI_FORCE_PERIPHERAL 0

static struct musb_hdrc_platform_data sunxi_musb_plat = {
#if (defined(CONFIG_USB_GADGET_MUSB_HDRC) || \
	defined(CONFIG_USB_GADGET_MUSB_HDRC_MODULE)) && \
		MUSB_SUNXI_FORCE_PERIPHERAL
	.mode		= MUSB_PERIPHERAL,
#else
	.mode		= MUSB_HOST,
#endif
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

#if defined(CONFIG_USB_GADGET_MUSB_HDRC) || \
	defined(CONFIG_USB_GADGET_MUSB_HDRC_MODULE)
static void do_fex_setup(struct musb_hdrc_platform_data *plat)
{
	int ret;
	int enabled = 1;
	int usb_port_type = 1;
	int usb_detect_type = 0;

	/* usbc enabled */
	ret = script_parser_fetch("usbc0", "usb_used", &enabled, 64);
	if (ret != 0) {
		pr_debug("couldn't fetch config '%s':'%s'.\n",
			 "usbc0", "usb_used");
		enabled = 1;
	}

	/* usbc port type */
	ret = script_parser_fetch("usbc0", "usb_port_type", &usb_port_type, 64);
	if (ret != 0) {
		pr_debug("couldn't fetch config '%s':'%s'.\n",
			 "usbc0", "usb_port_type");
		usb_port_type = 1;
	}

	/* usbc detect type */
	ret = script_parser_fetch("usbc0", "usb_detect_type", &usb_detect_type,
				  64);
	if (ret != 0) {
		pr_debug("couldn't fetch config '%s':'%s'.\n",
			 "usbc0", "usb_detect_type");
		usb_detect_type = 0;
	}

	pr_debug("usbc0 config: enabled=%d, port_type=%d, detect_type=%d\n",
		enabled, usb_port_type, usb_detect_type);

#if MUSB_SUNXI_FORCE_PERIPHERAL
	plat->mode = MUSB_PERIPHERAL;
#else
	plat->mode = MUSB_HOST;
	if (usb_port_type == 2 && usb_detect_type == 1) {
		/* OTG is not yet supported */
		plat->mode = /*MUSB_OTG*/ MUSB_PERIPHERAL;
	} else if (usb_port_type == 0) {
		plat->mode = MUSB_PERIPHERAL;
	}
#endif
}
#else
static void do_fex_setup(struct musb_hdrc_platform_data *plat)
{
}
#endif

int register_musb_device(void)
{
	do_fex_setup(&sunxi_musb_plat);

	return platform_device_register(&sunxi_musb_device);
}

void unregister_musb_device(void)
{
	platform_device_unregister(&sunxi_musb_device);
}
