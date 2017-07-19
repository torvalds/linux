/*
 * Renesas USB driver
 *
 * Copyright (C) 2011 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include "common.h"
#include "rcar2.h"
#include "rcar3.h"

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


#define USBHSF_RUNTIME_PWCTRL	(1 << 0)

/* status */
#define usbhsc_flags_init(p)   do {(p)->flags = 0; } while (0)
#define usbhsc_flags_set(p, b) ((p)->flags |=  (b))
#define usbhsc_flags_clr(p, b) ((p)->flags &= ~(b))
#define usbhsc_flags_has(p, b) ((p)->flags &   (b))

/*
 * platform call back
 *
 * renesas usb support platform callback function.
 * Below macro call it.
 * if platform doesn't have callback, it return 0 (no error)
 */
#define usbhs_platform_call(priv, func, args...)\
	(!(priv) ? -ENODEV :			\
	 !((priv)->pfunc.func) ? 0 :		\
	 (priv)->pfunc.func(args))

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
	int has_otg = usbhs_get_dparam(priv, has_otg);

	if (has_otg)
		usbhs_bset(priv, DVSTCTR, (EXTLP | PWEN), (EXTLP | PWEN));

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

	req->wValue	= usbhs_read(priv, USBVAL);
	req->wIndex	= usbhs_read(priv, USBINDX);
	req->wLength	= usbhs_read(priv, USBLENG);
}

void usbhs_usbreq_set_val(struct usbhs_priv *priv, struct usb_ctrlrequest *req)
{
	usbhs_write(priv, USBREQ,  (req->bRequest << 8) | req->bRequestType);
	usbhs_write(priv, USBVAL,  req->wValue);
	usbhs_write(priv, USBINDX, req->wIndex);
	usbhs_write(priv, USBLENG, req->wLength);

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

/*
 *		platform default param
 */

/* commonly used on old SH-Mobile SoCs */
static struct renesas_usbhs_driver_pipe_config usbhsc_default_pipe[] = {
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_CONTROL, 64, 0x00, false),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_ISOC, 1024, 0x08, false),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_ISOC, 1024, 0x18, false),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0x28, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0x38, true),
	RENESAS_USBHS_PIPE(USB_ENDPOINT_XFER_BULK, 512, 0x48, true),
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

		/* enable platform power */
		usbhs_platform_call(priv, power_ctrl, pdev, priv->base, enable);

		/* USB on */
		usbhs_sys_clock_ctrl(priv, enable);
	} else {
		/* USB off */
		usbhs_sys_clock_ctrl(priv, enable);

		/* disable platform power */
		usbhs_platform_call(priv, power_ctrl, pdev, priv->base, enable);

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
	enable = usbhs_platform_call(priv, get_vbus, pdev);

	/*
	 * get id from platform
	 */
	id = usbhs_platform_call(priv, get_id, pdev);

	if (enable && !mod) {
		if (priv->edev) {
			cable = extcon_get_cable_state_(priv->edev, EXTCON_USB_HOST);
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
		if (usbhsc_flags_has(priv, USBHSF_RUNTIME_PWCTRL))
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
		if (usbhsc_flags_has(priv, USBHSF_RUNTIME_PWCTRL))
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

static int usbhsc_drvcllbck_notify_hotplug(struct platform_device *pdev)
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
		.compatible = "renesas,usbhs-r8a7790",
		.data = (void *)USBHS_TYPE_RCAR_GEN2,
	},
	{
		.compatible = "renesas,usbhs-r8a7791",
		.data = (void *)USBHS_TYPE_RCAR_GEN2,
	},
	{
		.compatible = "renesas,usbhs-r8a7794",
		.data = (void *)USBHS_TYPE_RCAR_GEN2,
	},
	{
		.compatible = "renesas,usbhs-r8a7795",
		.data = (void *)USBHS_TYPE_RCAR_GEN3,
	},
	{
		.compatible = "renesas,usbhs-r8a7796",
		.data = (void *)USBHS_TYPE_RCAR_GEN3,
	},
	{
		.compatible = "renesas,rcar-gen2-usbhs",
		.data = (void *)USBHS_TYPE_RCAR_GEN2,
	},
	{
		.compatible = "renesas,rcar-gen3-usbhs",
		.data = (void *)USBHS_TYPE_RCAR_GEN3,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, usbhs_of_match);

static struct renesas_usbhs_platform_info *usbhs_parse_dt(struct device *dev)
{
	struct renesas_usbhs_platform_info *info;
	struct renesas_usbhs_driver_param *dparam;
	const struct of_device_id *of_id = of_match_device(usbhs_of_match, dev);
	u32 tmp;
	int gpio;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return NULL;

	dparam = &info->driver_param;
	dparam->type = of_id ? (uintptr_t)of_id->data : 0;
	if (!of_property_read_u32(dev->of_node, "renesas,buswait", &tmp))
		dparam->buswait_bwait = tmp;
	gpio = of_get_named_gpio_flags(dev->of_node, "renesas,enable-gpio", 0,
				       NULL);
	if (gpio > 0)
		dparam->enable_gpio = gpio;

	if (dparam->type == USBHS_TYPE_RCAR_GEN2 ||
	    dparam->type == USBHS_TYPE_RCAR_GEN3)
		dparam->has_usb_dmac = 1;

	return info;
}

static int usbhs_probe(struct platform_device *pdev)
{
	struct renesas_usbhs_platform_info *info = dev_get_platdata(&pdev->dev);
	struct renesas_usbhs_driver_callback *dfunc;
	struct usbhs_priv *priv;
	struct resource *res, *irq_res;
	int ret;

	/* check device node */
	if (pdev->dev.of_node)
		info = pdev->dev.platform_data = usbhs_parse_dt(&pdev->dev);

	/* check platform information */
	if (!info) {
		dev_err(&pdev->dev, "no platform information\n");
		return -EINVAL;
	}

	/* platform data */
	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq_res) {
		dev_err(&pdev->dev, "Not enough Renesas USB platform resources.\n");
		return -ENODEV;
	}

	/* usb private data */
	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	if (of_property_read_bool(pdev->dev.of_node, "extcon")) {
		priv->edev = extcon_get_edev_by_phandle(&pdev->dev, 0);
		if (IS_ERR(priv->edev))
			return PTR_ERR(priv->edev);
	}

	/*
	 * care platform info
	 */

	memcpy(&priv->dparam,
	       &info->driver_param,
	       sizeof(struct renesas_usbhs_driver_param));

	switch (priv->dparam.type) {
	case USBHS_TYPE_RCAR_GEN2:
		priv->pfunc = usbhs_rcar2_ops;
		if (!priv->dparam.pipe_configs) {
			priv->dparam.pipe_configs = usbhsc_new_pipe;
			priv->dparam.pipe_size = ARRAY_SIZE(usbhsc_new_pipe);
		}
		break;
	case USBHS_TYPE_RCAR_GEN3:
		priv->pfunc = usbhs_rcar3_ops;
		if (!priv->dparam.pipe_configs) {
			priv->dparam.pipe_configs = usbhsc_new_pipe;
			priv->dparam.pipe_size = ARRAY_SIZE(usbhsc_new_pipe);
		}
		break;
	default:
		if (!info->platform_callback.get_id) {
			dev_err(&pdev->dev, "no platform callbacks");
			return -EINVAL;
		}
		memcpy(&priv->pfunc,
		       &info->platform_callback,
		       sizeof(struct renesas_usbhs_platform_callback));
		break;
	}

	/* set driver callback functions for platform */
	dfunc			= &info->driver_callback;
	dfunc->notify_hotplug	= usbhsc_drvcllbck_notify_hotplug;

	/* set default param if platform doesn't have */
	if (!priv->dparam.pipe_configs) {
		priv->dparam.pipe_configs = usbhsc_default_pipe;
		priv->dparam.pipe_size = ARRAY_SIZE(usbhsc_default_pipe);
	}
	if (!priv->dparam.pio_dma_border)
		priv->dparam.pio_dma_border = 64; /* 64byte */

	/* FIXME */
	/* runtime power control ? */
	if (priv->pfunc.get_vbus)
		usbhsc_flags_set(priv, USBHSF_RUNTIME_PWCTRL);

	/*
	 * priv settings
	 */
	priv->irq	= irq_res->start;
	if (irq_res->flags & IORESOURCE_IRQ_SHAREABLE)
		priv->irqflags = IRQF_SHARED;
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

	/*
	 * deviece reset here because
	 * USB device might be used in boot loader.
	 */
	usbhs_sys_clock_ctrl(priv, 0);

	/* check GPIO determining if USB function should be enabled */
	if (priv->dparam.enable_gpio) {
		gpio_request_one(priv->dparam.enable_gpio, GPIOF_IN, NULL);
		ret = !gpio_get_value(priv->dparam.enable_gpio);
		gpio_free(priv->dparam.enable_gpio);
		if (ret) {
			dev_warn(&pdev->dev,
				 "USB function not selected (GPIO %d)\n",
				 priv->dparam.enable_gpio);
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
		dev_err(&pdev->dev, "platform init failed.\n");
		goto probe_end_mod_exit;
	}

	/* reset phy for connection */
	usbhs_platform_call(priv, phy_reset, pdev);

	/* power control */
	pm_runtime_enable(&pdev->dev);
	if (!usbhsc_flags_has(priv, USBHSF_RUNTIME_PWCTRL)) {
		usbhsc_power_ctrl(priv, 1);
		usbhs_mod_autonomy_mode(priv);
	}

	/*
	 * manual call notify_hotplug for cold plug
	 */
	usbhsc_drvcllbck_notify_hotplug(pdev);

	dev_info(&pdev->dev, "probed\n");

	return ret;

probe_end_mod_exit:
	usbhs_mod_remove(priv);
probe_end_fifo_exit:
	usbhs_fifo_remove(priv);
probe_end_pipe_exit:
	usbhs_pipe_remove(priv);

	dev_info(&pdev->dev, "probe failed (%d)\n", ret);

	return ret;
}

static int usbhs_remove(struct platform_device *pdev)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);
	struct renesas_usbhs_platform_info *info = dev_get_platdata(&pdev->dev);
	struct renesas_usbhs_driver_callback *dfunc = &info->driver_callback;

	dev_dbg(&pdev->dev, "usb remove\n");

	dfunc->notify_hotplug = NULL;

	/* power off */
	if (!usbhsc_flags_has(priv, USBHSF_RUNTIME_PWCTRL))
		usbhsc_power_ctrl(priv, 0);

	pm_runtime_disable(&pdev->dev);

	usbhs_platform_call(priv, hardware_exit, pdev);
	usbhs_mod_remove(priv);
	usbhs_fifo_remove(priv);
	usbhs_pipe_remove(priv);

	return 0;
}

static int usbhsc_suspend(struct device *dev)
{
	struct usbhs_priv *priv = dev_get_drvdata(dev);
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);

	if (mod) {
		usbhs_mod_call(priv, stop, priv);
		usbhs_mod_change(priv, -1);
	}

	if (mod || !usbhsc_flags_has(priv, USBHSF_RUNTIME_PWCTRL))
		usbhsc_power_ctrl(priv, 0);

	return 0;
}

static int usbhsc_resume(struct device *dev)
{
	struct usbhs_priv *priv = dev_get_drvdata(dev);
	struct platform_device *pdev = usbhs_priv_to_pdev(priv);

	if (!usbhsc_flags_has(priv, USBHSF_RUNTIME_PWCTRL)) {
		usbhsc_power_ctrl(priv, 1);
		usbhs_mod_autonomy_mode(priv);
	}

	usbhs_platform_call(priv, phy_reset, pdev);

	usbhsc_drvcllbck_notify_hotplug(pdev);

	return 0;
}

static int usbhsc_runtime_nop(struct device *dev)
{
	/* Runtime PM callback shared between ->runtime_suspend()
	 * and ->runtime_resume(). Simply returns success.
	 *
	 * This driver re-initializes all registers after
	 * pm_runtime_get_sync() anyway so there is no need
	 * to save and restore registers here.
	 */
	return 0;
}

static const struct dev_pm_ops usbhsc_pm_ops = {
	.suspend		= usbhsc_suspend,
	.resume			= usbhsc_resume,
	.runtime_suspend	= usbhsc_runtime_nop,
	.runtime_resume		= usbhsc_runtime_nop,
};

static struct platform_driver renesas_usbhs_driver = {
	.driver		= {
		.name	= "renesas_usbhs",
		.pm	= &usbhsc_pm_ops,
		.of_match_table = of_match_ptr(usbhs_of_match),
	},
	.probe		= usbhs_probe,
	.remove		= usbhs_remove,
};

module_platform_driver(renesas_usbhs_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas USB driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
