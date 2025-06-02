// SPDX-License-Identifier: GPL-1.0+
/*
 * Renesas USB driver
 *
 * Copyright (C) 2011 Renesas Solutions Corp.
 * Copyright (C) 2019 Renesas Electronics Corporation
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include "common.h"
#include "rcar2.h"
#include "rcar3.h"
#include "rza.h"

/*
 *		image of renesas_usbhs
 *
 * ex) gadget case

 * mod.c
 * mod_gadget.c
 * mod_host.c		pipe.c		fifo.c
 *
 *			+-------+	+-----------+
 *			| pipe0 |------>| fifo pio  |
 * +------------+	+-------+	+-----------+
 * | mod_gadget |=====> | pipe1 |--+
 * +------------+	+-------+  |	+-----------+
 *			| pipe2 |  |  +-| fifo dma0 |
 * +------------+	+-------+  |  |	+-----------+
 * | mod_host   |	| pipe3 |<-|--+
 * +------------+	+-------+  |	+-----------+
 *			| ....  |  +--->| fifo dma1 |
 *			| ....  |	+-----------+
 */

/*
 * platform call back
 *
 * renesas usb support platform callback function.
 * Below macro call it.
 * if platform doesn't have callback, it return 0 (no error)
 */
#define usbhs_platform_call(priv, func, args...)\
	(!(priv) ? -ENODEV :			\
	 !((priv)->pfunc->func) ? 0 :		\
	 (priv)->pfunc->func(args))

/*
 *		common functions
 */
u16 usbhs_read(struct usbhs_priv *priv, u32 reg)
{
	return ioread16(priv->base + reg);
}

void usbhs_write(struct usbhs_priv *priv, u32 reg, u16 data)
{
	iowrite16(data, priv->base + reg);
}

void usbhs_bset(struct usbhs_priv *priv, u32 reg, u16 mask, u16 data)
{
	u16 val = usbhs_read(priv, reg);

	val &= ~mask;
	val |= data & mask;

	usbhs_write(priv, reg, val);
}

struct usbhs_priv *usbhs_pdev_to_priv(struct platform_device *pdev)
{
	return dev_get_drvdata(&pdev->dev);
}

int usbhs_get_id_as_gadget(struct platform_device *pdev)
{
	return USBHS_GADGET;
}

/*
 *		syscfg functions
 */
static void usbhs_sys_clock_ctrl(struct usbhs_priv *priv, int enable)
{
	usbhs_bset(priv, SYSCFG, SCKE, enable ? SCKE : 0);
}

void usbhs_sys_host_ctrl(struct usbhs_priv *priv, int enable)
{
	u16 mask = DCFM | DRPD | DPRPU | HSE | USBE;
	u16 val  = DCFM | DRPD | HSE | USBE;

	/*
	 * if enable
	 *
	 * - select Host mode
	 * - D+ Line/D- Line Pull-down
	 */
	usbhs_bset(priv, SYSCFG, mask, enable ? val : 0);
}

void usbhs_sys_function_ctrl(struct usbhs_priv *priv, int enable)
{
	u16 mask = DCFM | DRPD | DPRPU | HSE | USBE;
	u16 val  = HSE | USBE;

	/* CNEN bit is required for function operation */
	if (usbhs_get_dparam(priv, has_cnen)) {
		mask |= CNEN;
		val  |= CNEN;
	}

	/*
	 * if enable
	 *
	 * - select Function mode
	 * - D+ Line Pull-up is disabled
	 *      When D+ Line Pull-up is enabled,
	 *      calling usbhs_sys_function_pullup(,1)
	 */
	usbhs_bset(priv, SYSCFG, mask, enable ? val : 0);
}

void usbhs_sys_function_pullup(struct usbhs_priv *priv, int enable)
{
	usbhs_bset(priv, SYSCFG, DPRPU, enable ? DPRPU : 0);
}

void usbhs_sys_set_test_mode(struct usbhs_priv *priv, u16 mode)
{
	usbhs_write(priv, TESTMODE, mode);
}

/*
 *		frame functions
 */
int usbhs_frame_get_num(struct usbhs_priv *priv)
{
	return usbhs_read(priv, FRMNUM) & FRNM_MASK;
}

/*
 *		usb request functions
 */
void usbhs_usbreq_get_val(struct usbhs_priv *priv, struct usb_ctrlrequest *req)
{
	u16 val;

	val = usbhs_read(priv, USBREQ);
	req->bRequest		= (val >> 8) & 0xFF;
	req->bRequestType	= (val >> 0) & 0xFF;

	req->wValue	= cpu_to_le16(usbhs_read(priv, USBVAL));
	req->wIndex	= cpu_to_le16(usbhs_read(priv, USBINDX));
	req->wLength	= cpu_to_le16(usbhs_read(priv, USBLENG));
}

void usbhs_usbreq_set_val(struct usbhs_priv *priv, struct usb_ctrlrequest *req)
{
	usbhs_write(priv, USBREQ,  (req->bRequest << 8) | req->bRequestType);
	usbhs_write(priv, USBVAL,  le16_to_cpu(req->wValue));
	usbhs_write(priv, USBINDX, le16_to_cpu(req->wIndex));
	usbhs_write(priv, USBLENG, le16_to_cpu(req->wLength));

	usbhs_bset(priv, DCPCTR, SUREQ, SUREQ);
}

/*
 *		bus/vbus functions
 */
void usbhs_bus_send_sof_enable(struct usbhs_priv *priv)
{
	u16 status = usbhs_read(priv, DVSTCTR) & (USBRST | UACT);

	if (status != USBRST) {
		struct device *dev = usbhs_priv_to_dev(priv);
		dev_err(dev, "usbhs should be reset\n");
	}

	usbhs_bset(priv, DVSTCTR, (USBRST | UACT), UACT);
}

void usbhs_bus_send_reset(struct usbhs_priv *priv)
{
	usbhs_bset(priv, DVSTCTR, (USBRST | UACT), USBRST);
}

int usbhs_bus_get_speed(struct usbhs_priv *priv)
{
	u16 dvstctr = usbhs_read(priv, DVSTCTR);

	switch (RHST & dvstctr) {
	case RHST_LOW_SPEED:
		return USB_SPEED_LOW;
	case RHST_FULL_SPEED:
		return USB_SPEED_FULL;
	case RHST_HIGH_SPEED:
		return USB_SPEED_HIGH;
	}

	return USB_SPEED_UNKNOWN;
}

int usbhs_vbus_ctrl(struct usbhs_priv *priv, int enable)
{
	struct platform_device *pdev = usbhs_priv_to_pdev(priv);

	return usbhs_platform_call(priv, set_vbus, pdev, enable);
}

static void usbhsc_bus_init(struct usbhs_priv *priv)
{
	usbhs_write(priv, DVSTCTR, 0);

	usbhs_vbus_ctrl(priv, 0);
}

/*
 *		device configuration
 */
int usbhs_set_device_config(struct usbhs_priv *priv, int devnum,
			   u16 upphub, u16 hubport, u16 speed)
{
	struct device *dev = usbhs_priv_to_dev(priv);
	u16 usbspd = 0;
	u32 reg = DEVADD0 + (2 * devnum);

	if (devnum > 10) {
		dev_err(dev, "cannot set speed to unknown device %d\n", devnum);
		return -EIO;
	}

	if (upphub > 0xA) {
		dev_err(dev, "unsupported hub number %d\n", upphub);
		return -EIO;
	}

	switch (speed) {
	case USB_SPEED_LOW:
		usbspd = USBSPD_SPEED_LOW;
		break;
	case USB_SPEED_FULL:
		usbspd = USBSPD_SPEED_FULL;
		break;
	case USB_SPEED_HIGH:
		usbspd = USBSPD_SPEED_HIGH;
		break;
	default:
		dev_err(dev, "unsupported speed %d\n", speed);
		return -EIO;
	}

	usbhs_write(priv, reg,	UPPHUB(upphub)	|
				HUBPORT(hubport)|
				USBSPD(usbspd));

	return 0;
}

/*
 *		interrupt functions
 */
void usbhs_xxxsts_clear(struct usbhs_priv *priv, u16 sts_reg, u16 bit)
{
	u16 pipe_mask = (u16)GENMASK(usbhs_get_dparam(priv, pipe_size), 0);

	usbhs_write(priv, sts_reg, ~(1 << bit) & pipe_mask);
}

/*
 *		local functions
 */
static void usbhsc_set_buswait(struct usbhs_priv *priv)
{
	int wait = usbhs_get_dparam(priv, buswait_bwait);

	/* set bus wait if platform have */
	if (wait)
		usbhs_bset(priv, BUSWAIT, 0x000F, wait);
}

static bool usbhsc_is_multi_clks(struct usbhs_priv *priv)
{
	return priv->dparam.multi_clks;
}

static int usbhsc_clk_get(struct device *dev, struct usbhs_priv *priv)
{
	if (!usbhsc_is_multi_clks(priv))
		return 0;

	/* The first clock should exist */
	priv->clks[0] = of_clk_get(dev_of_node(dev), 0);
	if (IS_ERR(priv->clks[0]))
		return PTR_ERR(priv->clks[0]);

	/*
	 * To backward compatibility with old DT, this driver checks the return
	 * value if it's -ENOENT or not.
	 */
	priv->clks[1] = of_clk_get(dev_of_node(dev), 1);
	if (PTR_ERR(priv->clks[1]) == -ENOENT)
		priv->clks[1] = NULL;
	else if (IS_ERR(priv->clks[1])) {
		clk_put(priv->clks[0]);
		return PTR_ERR(priv->clks[1]);
	}

	return 0;
}

static void usbhsc_clk_put(struct usbhs_priv *priv)
{
	int i;

	if (!usbhsc_is_multi_clks(priv))
		return;

	for (i = 0; i < ARRAY_SIZE(priv->clks); i++)
		clk_put(priv->clks[i]);
}

static int usbhsc_clk_prepare_enable(struct usbhs_priv *priv)
{
	int i, ret;

	if (!usbhsc_is_multi_clks(priv))
		return 0;

	for (i = 0; i < ARRAY_SIZE(priv->clks); i++) {
		ret = clk_prepare_enable(priv->clks[i]);
		if (ret) {
			while (--i >= 0)
				clk_disable_unprepare(priv->clks[i]);
			return ret;
		}
	}

	return ret;
}

static void usbhsc_clk_disable_unprepare(struct usbhs_priv *priv)
{
	int i;

	if (!usbhsc_is_multi_clks(priv))
		return;

	for (i = 0; i < ARRAY_SIZE(priv->clks); i++)
		clk_disable_unprepare(priv->clks[i]);
}

/*
 *		platform default param
 */

/* commonly used on old SH-Mobile and RZ/G2L family SoCs */
static struct renesas_usbhs_driver_pipe_config usbhsc_default_pipe[] = {
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_CONTROL, 64, 0x00, false),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_ISOC, 1024, 0x08, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_ISOC, 1024, 0x28, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0x48, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0x58, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0x68, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_INT, 64, 0x04, false),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_INT, 64, 0x05, false),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_INT, 64, 0x06, false),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_INT, 64, 0x07, false),
};

/* commonly used on newer SH-Mobile and R-Car SoCs */
static struct renesas_usbhs_driver_pipe_config usbhsc_new_pipe[] = {
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_CONTROL, 64, 0x00, false),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_ISOC, 1024, 0x08, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_ISOC, 1024, 0x28, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0x48, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0x58, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0x68, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_INT, 64, 0x04, false),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_INT, 64, 0x05, false),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_INT, 64, 0x06, false),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0x78, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0x88, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0x98, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0xa8, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0xb8, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0xc8, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0xd8, true),
};

/*
 *		power control
 */
static void usbhsc_power_ctrl(struct usbhs_priv *priv, int enable)
{
	struct platform_device *pdev = usbhs_priv_to_pdev(priv);
	struct device *dev = usbhs_priv_to_dev(priv);

	if (enable) {
		/* enable PM */
		pm_runtime_get_sync(dev);

		/* enable clks */
		if (usbhsc_clk_prepare_enable(priv))
			return;

		/* enable platform power */
		usbhs_platform_call(priv, power_ctrl, pdev, priv->base, enable);

		/* USB on */
		usbhs_sys_clock_ctrl(priv, enable);
	} else {
		/* USB off */
		usbhs_sys_clock_ctrl(priv, enable);

		/* disable platform power */
		usbhs_platform_call(priv, power_ctrl, pdev, priv->base, enable);

		/* disable clks */
		usbhsc_clk_disable_unprepare(priv);

		/* disable PM */
		pm_runtime_put_sync(dev);
	}
}

/*
 *		hotplug
 */
static void usbhsc_hotplug(struct usbhs_priv *priv)
{
	struct platform_device *pdev = usbhs_priv_to_pdev(priv);
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);
	int id;
	int enable;
	int cable;
	int ret;

	/*
	 * get vbus status from platform
	 */
	enable = usbhs_mod_info_call(priv, get_vbus, pdev);

	/*
	 * get id from platform
	 */
	id = usbhs_platform_call(priv, get_id, pdev);

	if (enable && !mod) {
		if (priv->edev) {
			cable = extcon_get_state(priv->edev, EXTCON_USB_HOST);
			if ((cable > 0 && id != USBHS_HOST) ||
			    (!cable && id != USBHS_GADGET)) {
				dev_info(&pdev->dev,
					 "USB cable plugged in doesn't match the selected role!\n");
				return;
			}
		}

		ret = usbhs_mod_change(priv, id);
		if (ret < 0)
			return;

		dev_dbg(&pdev->dev, "%s enable\n", __func__);

		/* power on */
		if (usbhs_get_dparam(priv, runtime_pwctrl))
			usbhsc_power_ctrl(priv, enable);

		/* bus init */
		usbhsc_set_buswait(priv);
		usbhsc_bus_init(priv);

		/* module start */
		usbhs_mod_call(priv, start, priv);

	} else if (!enable && mod) {
		dev_dbg(&pdev->dev, "%s disable\n", __func__);

		/* module stop */
		usbhs_mod_call(priv, stop, priv);

		/* bus init */
		usbhsc_bus_init(priv);

		/* power off */
		if (usbhs_get_dparam(priv, runtime_pwctrl))
			usbhsc_power_ctrl(priv, enable);

		usbhs_mod_change(priv, -1);

		/* reset phy for next connection */
		usbhs_platform_call(priv, phy_reset, pdev);
	}
}

/*
 *		notify hotplug
 */
static void usbhsc_notify_hotplug(struct work_struct *work)
{
	struct usbhs_priv *priv = container_of(work,
					       struct usbhs_priv,
					       notify_hotplug_work.work);
	usbhsc_hotplug(priv);
}

int usbhsc_schedule_notify_hotplug(struct platform_device *pdev)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);
	int delay = usbhs_get_dparam(priv, detection_delay);

	/*
	 * This functions will be called in interrupt.
	 * To make sure safety context,
	 * use workqueue for usbhs_notify_hotplug
	 */
	schedule_delayed_work(&priv->notify_hotplug_work,
			      msecs_to_jiffies(delay));
	return 0;
}

/*
 *		platform functions
 */
static const struct of_device_id usbhs_of_match[] = {
	{
		.compatible = "renesas,usbhs-r8a774c0",
		.data = &usbhs_rcar_gen3_with_pll_plat_info,
	},
	{
		.compatible = "renesas,usbhs-r8a7790",
		.data = &usbhs_rcar_gen2_plat_info,
	},
	{
		.compatible = "renesas,usbhs-r8a7791",
		.data = &usbhs_rcar_gen2_plat_info,
	},
	{
		.compatible = "renesas,usbhs-r8a7794",
		.data = &usbhs_rcar_gen2_plat_info,
	},
	{
		.compatible = "renesas,usbhs-r8a7795",
		.data = &usbhs_rcar_gen3_plat_info,
	},
	{
		.compatible = "renesas,usbhs-r8a7796",
		.data = &usbhs_rcar_gen3_plat_info,
	},
	{
		.compatible = "renesas,usbhs-r8a77990",
		.data = &usbhs_rcar_gen3_with_pll_plat_info,
	},
	{
		.compatible = "renesas,usbhs-r8a77995",
		.data = &usbhs_rcar_gen3_with_pll_plat_info,
	},
	{
		.compatible = "renesas,usbhs-r9a07g043",
		.data = &usbhs_rzg2l_plat_info,
	},
	{
		.compatible = "renesas,usbhs-r9a07g044",
		.data = &usbhs_rzg2l_plat_info,
	},
	{
		.compatible = "renesas,usbhs-r9a07g054",
		.data = &usbhs_rzg2l_plat_info,
	},
	{
		.compatible = "renesas,rcar-gen2-usbhs",
		.data = &usbhs_rcar_gen2_plat_info,
	},
	{
		.compatible = "renesas,rcar-gen3-usbhs",
		.data = &usbhs_rcar_gen3_plat_info,
	},
	{
		.compatible = "renesas,rza1-usbhs",
		.data = &usbhs_rza1_plat_info,
	},
	{
		.compatible = "renesas,rza2-usbhs",
		.data = &usbhs_rza2_plat_info,
	},
	{
		.compatible = "renesas,rzg2l-usbhs",
		.data = &usbhs_rzg2l_plat_info,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, usbhs_of_match);

static int usbhs_probe(struct platform_device *pdev)
{
	const struct renesas_usbhs_platform_info *info;
	struct usbhs_priv *priv;
	struct device *dev = &pdev->dev;
	struct gpio_desc *gpiod;
	int ret;
	u32 tmp;
	int irq;

	info = of_device_get_match_data(dev);
	if (!info) {
		info = dev_get_platdata(dev);
		if (!info)
			return dev_err_probe(dev, -EINVAL, "no platform info\n");
	}

	/* platform data */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	/* usb private data */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	if (of_property_present(dev_of_node(dev), "extcon")) {
		priv->edev = extcon_get_edev_by_phandle(dev, 0);
		if (IS_ERR(priv->edev))
			return PTR_ERR(priv->edev);
	}

	priv->rsts = devm_reset_control_array_get_optional_shared(dev);
	if (IS_ERR(priv->rsts))
		return PTR_ERR(priv->rsts);

	/*
	 * care platform info
	 */

	priv->dparam = info->driver_param;

	if (!info->platform_callback.get_id) {
		dev_err(dev, "no platform callbacks\n");
		return -EINVAL;
	}
	priv->pfunc = &info->platform_callback;

	/* set default param if platform doesn't have */
	if (usbhs_get_dparam(priv, has_new_pipe_configs)) {
		priv->dparam.pipe_configs = usbhsc_new_pipe;
		priv->dparam.pipe_size = ARRAY_SIZE(usbhsc_new_pipe);
	} else if (!priv->dparam.pipe_configs) {
		priv->dparam.pipe_configs = usbhsc_default_pipe;
		priv->dparam.pipe_size = ARRAY_SIZE(usbhsc_default_pipe);
	}
	if (!priv->dparam.pio_dma_border)
		priv->dparam.pio_dma_border = 64; /* 64byte */
	if (!of_property_read_u32(dev_of_node(dev), "renesas,buswait", &tmp))
		priv->dparam.buswait_bwait = tmp;
	gpiod = devm_gpiod_get_optional(dev, "renesas,enable", GPIOD_IN);
	if (IS_ERR(gpiod))
		return PTR_ERR(gpiod);

	/* FIXME */
	/* runtime power control ? */
	if (priv->pfunc->get_vbus)
		usbhs_get_dparam(priv, runtime_pwctrl) = 1;

	/*
	 * priv settings
	 */
	priv->irq = irq;
	priv->pdev	= pdev;
	INIT_DELAYED_WORK(&priv->notify_hotplug_work, usbhsc_notify_hotplug);
	spin_lock_init(usbhs_priv_to_lock(priv));

	/* call pipe and module init */
	ret = usbhs_pipe_probe(priv);
	if (ret < 0)
		return ret;

	ret = usbhs_fifo_probe(priv);
	if (ret < 0)
		goto probe_end_pipe_exit;

	ret = usbhs_mod_probe(priv);
	if (ret < 0)
		goto probe_end_fifo_exit;

	/* dev_set_drvdata should be called after usbhs_mod_init */
	platform_set_drvdata(pdev, priv);

	ret = reset_control_deassert(priv->rsts);
	if (ret)
		goto probe_fail_rst;

	ret = usbhsc_clk_get(dev, priv);
	if (ret)
		goto probe_fail_clks;

	/*
	 * deviece reset here because
	 * USB device might be used in boot loader.
	 */
	usbhs_sys_clock_ctrl(priv, 0);

	/* check GPIO determining if USB function should be enabled */
	if (gpiod) {
		ret = !gpiod_get_value(gpiod);
		if (ret) {
			dev_warn(dev, "USB function not selected (GPIO)\n");
			ret = -ENOTSUPP;
			goto probe_end_mod_exit;
		}
	}

	/*
	 * platform call
	 *
	 * USB phy setup might depend on CPU/Board.
	 * If platform has its callback functions,
	 * call it here.
	 */
	ret = usbhs_platform_call(priv, hardware_init, pdev);
	if (ret < 0) {
		dev_err(dev, "platform init failed.\n");
		goto probe_end_mod_exit;
	}

	/* reset phy for connection */
	usbhs_platform_call(priv, phy_reset, pdev);

	/* power control */
	pm_runtime_enable(dev);
	if (!usbhs_get_dparam(priv, runtime_pwctrl)) {
		usbhsc_power_ctrl(priv, 1);
		usbhs_mod_autonomy_mode(priv);
	} else {
		usbhs_mod_non_autonomy_mode(priv);
	}

	/*
	 * manual call notify_hotplug for cold plug
	 */
	usbhsc_schedule_notify_hotplug(pdev);

	dev_info(dev, "probed\n");

	return ret;

probe_end_mod_exit:
	usbhsc_clk_put(priv);
probe_fail_clks:
	reset_control_assert(priv->rsts);
probe_fail_rst:
	usbhs_mod_remove(priv);
probe_end_fifo_exit:
	usbhs_fifo_remove(priv);
probe_end_pipe_exit:
	usbhs_pipe_remove(priv);

	dev_info(dev, "probe failed (%d)\n", ret);

	return ret;
}

static void usbhs_remove(struct platform_device *pdev)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);

	dev_dbg(&pdev->dev, "usb remove\n");

	flush_delayed_work(&priv->notify_hotplug_work);

	/* power off */
	if (!usbhs_get_dparam(priv, runtime_pwctrl))
		usbhsc_power_ctrl(priv, 0);

	pm_runtime_disable(&pdev->dev);

	usbhs_platform_call(priv, hardware_exit, pdev);
	usbhsc_clk_put(priv);
	reset_control_assert(priv->rsts);
	usbhs_mod_remove(priv);
	usbhs_fifo_remove(priv);
	usbhs_pipe_remove(priv);
}

static __maybe_unused int usbhsc_suspend(struct device *dev)
{
	struct usbhs_priv *priv = dev_get_drvdata(dev);
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);

	if (mod) {
		usbhs_mod_call(priv, stop, priv);
		usbhs_mod_change(priv, -1);
	}

	if (mod || !usbhs_get_dparam(priv, runtime_pwctrl))
		usbhsc_power_ctrl(priv, 0);

	return 0;
}

static __maybe_unused int usbhsc_resume(struct device *dev)
{
	struct usbhs_priv *priv = dev_get_drvdata(dev);
	struct platform_device *pdev = usbhs_priv_to_pdev(priv);

	if (!usbhs_get_dparam(priv, runtime_pwctrl)) {
		usbhsc_power_ctrl(priv, 1);
		usbhs_mod_autonomy_mode(priv);
	}

	usbhs_platform_call(priv, phy_reset, pdev);

	usbhsc_schedule_notify_hotplug(pdev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(usbhsc_pm_ops, usbhsc_suspend, usbhsc_resume);

static struct platform_driver renesas_usbhs_driver = {
	.driver		= {
		.name	= "renesas_usbhs",
		.pm	= &usbhsc_pm_ops,
		.of_match_table = usbhs_of_match,
	},
	.probe		= usbhs_probe,
	.remove		= usbhs_remove,
};

module_platform_driver(renesas_usbhs_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas USB driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
