/*
 * Driver for the SH-Mobile MIPI CSI-2 unit
 *
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/sh_mobile_csi2.h>
#include <media/soc_camera.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#define SH_CSI2_TREF	0x00
#define SH_CSI2_SRST	0x04
#define SH_CSI2_PHYCNT	0x08
#define SH_CSI2_CHKSUM	0x0C
#define SH_CSI2_VCDT	0x10

struct sh_csi2 {
	struct v4l2_subdev		subdev;
	struct list_head		list;
	struct notifier_block		notifier;
	unsigned int			irq;
	void __iomem			*base;
	struct platform_device		*pdev;
	struct sh_csi2_client_config	*client;
	unsigned long (*query_bus_param)(struct soc_camera_device *);
	int (*set_bus_param)(struct soc_camera_device *, unsigned long);
};

static int sh_csi2_try_fmt(struct v4l2_subdev *sd,
			   struct v4l2_mbus_framefmt *mf)
{
	struct sh_csi2 *priv = container_of(sd, struct sh_csi2, subdev);
	struct sh_csi2_pdata *pdata = priv->pdev->dev.platform_data;

	if (mf->width > 8188)
		mf->width = 8188;
	else if (mf->width & 1)
		mf->width &= ~1;

	switch (pdata->type) {
	case SH_CSI2C:
		switch (mf->code) {
		case V4L2_MBUS_FMT_UYVY8_2X8:		/* YUV422 */
		case V4L2_MBUS_FMT_YUYV8_1_5X8:		/* YUV420 */
		case V4L2_MBUS_FMT_Y8_1X8:		/* RAW8 */
		case V4L2_MBUS_FMT_SBGGR8_1X8:
		case V4L2_MBUS_FMT_SGRBG8_1X8:
			break;
		default:
			/* All MIPI CSI-2 devices must support one of primary formats */
			mf->code = V4L2_MBUS_FMT_YUYV8_2X8;
		}
		break;
	case SH_CSI2I:
		switch (mf->code) {
		case V4L2_MBUS_FMT_Y8_1X8:		/* RAW8 */
		case V4L2_MBUS_FMT_SBGGR8_1X8:
		case V4L2_MBUS_FMT_SGRBG8_1X8:
		case V4L2_MBUS_FMT_SBGGR10_1X10:	/* RAW10 */
		case V4L2_MBUS_FMT_SBGGR12_1X12:	/* RAW12 */
			break;
		default:
			/* All MIPI CSI-2 devices must support one of primary formats */
			mf->code = V4L2_MBUS_FMT_SBGGR8_1X8;
		}
		break;
	}

	return 0;
}

/*
 * We have done our best in try_fmt to try and tell the sensor, which formats
 * we support. If now the configuration is unsuitable for us we can only
 * error out.
 */
static int sh_csi2_s_fmt(struct v4l2_subdev *sd,
			 struct v4l2_mbus_framefmt *mf)
{
	struct sh_csi2 *priv = container_of(sd, struct sh_csi2, subdev);
	u32 tmp = (priv->client->channel & 3) << 8;

	dev_dbg(sd->v4l2_dev->dev, "%s(%u)\n", __func__, mf->code);
	if (mf->width > 8188 || mf->width & 1)
		return -EINVAL;

	switch (mf->code) {
	case V4L2_MBUS_FMT_UYVY8_2X8:
		tmp |= 0x1e;	/* YUV422 8 bit */
		break;
	case V4L2_MBUS_FMT_YUYV8_1_5X8:
		tmp |= 0x18;	/* YUV420 8 bit */
		break;
	case V4L2_MBUS_FMT_RGB555_2X8_PADHI_BE:
		tmp |= 0x21;	/* RGB555 */
		break;
	case V4L2_MBUS_FMT_RGB565_2X8_BE:
		tmp |= 0x22;	/* RGB565 */
		break;
	case V4L2_MBUS_FMT_Y8_1X8:
	case V4L2_MBUS_FMT_SBGGR8_1X8:
	case V4L2_MBUS_FMT_SGRBG8_1X8:
		tmp |= 0x2a;	/* RAW8 */
		break;
	default:
		return -EINVAL;
	}

	iowrite32(tmp, priv->base + SH_CSI2_VCDT);

	return 0;
}

static struct v4l2_subdev_video_ops sh_csi2_subdev_video_ops = {
	.s_mbus_fmt	= sh_csi2_s_fmt,
	.try_mbus_fmt	= sh_csi2_try_fmt,
};

static struct v4l2_subdev_core_ops sh_csi2_subdev_core_ops;

static struct v4l2_subdev_ops sh_csi2_subdev_ops = {
	.core	= &sh_csi2_subdev_core_ops,
	.video	= &sh_csi2_subdev_video_ops,
};

static void sh_csi2_hwinit(struct sh_csi2 *priv)
{
	struct sh_csi2_pdata *pdata = priv->pdev->dev.platform_data;
	__u32 tmp = 0x10; /* Enable MIPI CSI clock lane */

	/* Reflect registers immediately */
	iowrite32(0x00000001, priv->base + SH_CSI2_TREF);
	/* reset CSI2 harware */
	iowrite32(0x00000001, priv->base + SH_CSI2_SRST);
	udelay(5);
	iowrite32(0x00000000, priv->base + SH_CSI2_SRST);

	if (priv->client->lanes & 3)
		tmp |= priv->client->lanes & 3;
	else
		/* Default - both lanes */
		tmp |= 3;

	if (priv->client->phy == SH_CSI2_PHY_MAIN)
		tmp |= 0x8000;

	iowrite32(tmp, priv->base + SH_CSI2_PHYCNT);

	tmp = 0;
	if (pdata->flags & SH_CSI2_ECC)
		tmp |= 2;
	if (pdata->flags & SH_CSI2_CRC)
		tmp |= 1;
	iowrite32(tmp, priv->base + SH_CSI2_CHKSUM);
}

static int sh_csi2_set_bus_param(struct soc_camera_device *icd,
				 unsigned long flags)
{
	return 0;
}

static unsigned long sh_csi2_query_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	const unsigned long flags = SOCAM_PCLK_SAMPLE_RISING |
		SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_HIGH |
		SOCAM_MASTER | SOCAM_DATAWIDTH_8 | SOCAM_DATA_ACTIVE_HIGH;

	return soc_camera_apply_sensor_flags(icl, flags);
}

static int sh_csi2_notify(struct notifier_block *nb,
			  unsigned long action, void *data)
{
	struct device *dev = data;
	struct soc_camera_device *icd = to_soc_camera_dev(dev);
	struct v4l2_device *v4l2_dev = dev_get_drvdata(dev->parent);
	struct sh_csi2 *priv =
		container_of(nb, struct sh_csi2, notifier);
	struct sh_csi2_pdata *pdata = priv->pdev->dev.platform_data;
	int ret, i;

	for (i = 0; i < pdata->num_clients; i++)
		if (&pdata->clients[i].pdev->dev == icd->pdev)
			break;

	dev_dbg(dev, "%s(%p): action = %lu, found #%d\n", __func__, dev, action, i);

	if (i == pdata->num_clients)
		return NOTIFY_DONE;

	switch (action) {
	case BUS_NOTIFY_BOUND_DRIVER:
		snprintf(priv->subdev.name, V4L2_SUBDEV_NAME_SIZE, "%s%s",
			 dev_name(v4l2_dev->dev), ".mipi-csi");
		priv->subdev.grp_id = (long)icd;
		ret = v4l2_device_register_subdev(v4l2_dev, &priv->subdev);
		dev_dbg(dev, "%s(%p): ret(register_subdev) = %d\n", __func__, priv, ret);
		if (ret < 0)
			return NOTIFY_DONE;

		priv->client = pdata->clients + i;

		priv->set_bus_param		= icd->ops->set_bus_param;
		priv->query_bus_param		= icd->ops->query_bus_param;
		icd->ops->set_bus_param		= sh_csi2_set_bus_param;
		icd->ops->query_bus_param	= sh_csi2_query_bus_param;

		pm_runtime_get_sync(v4l2_get_subdevdata(&priv->subdev));

		sh_csi2_hwinit(priv);
		break;
	case BUS_NOTIFY_UNBIND_DRIVER:
		priv->client = NULL;

		/* Driver is about to be unbound */
		icd->ops->set_bus_param		= priv->set_bus_param;
		icd->ops->query_bus_param	= priv->query_bus_param;
		priv->set_bus_param		= NULL;
		priv->query_bus_param		= NULL;

		v4l2_device_unregister_subdev(&priv->subdev);

		pm_runtime_put(v4l2_get_subdevdata(&priv->subdev));
		break;
	}

	return NOTIFY_OK;
}

static __devinit int sh_csi2_probe(struct platform_device *pdev)
{
	struct resource *res;
	unsigned int irq;
	int ret;
	struct sh_csi2 *priv;
	/* Platform data specify the PHY, lanes, ECC, CRC */
	struct sh_csi2_pdata *pdata = pdev->dev.platform_data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	/* Interrupt unused so far */
	irq = platform_get_irq(pdev, 0);

	if (!res || (int)irq <= 0 || !pdata) {
		dev_err(&pdev->dev, "Not enough CSI2 platform resources.\n");
		return -ENODEV;
	}

	/* TODO: Add support for CSI2I. Careful: different register layout! */
	if (pdata->type != SH_CSI2C) {
		dev_err(&pdev->dev, "Only CSI2C supported ATM.\n");
		return -EINVAL;
	}

	priv = kzalloc(sizeof(struct sh_csi2), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->irq = irq;
	priv->notifier.notifier_call = sh_csi2_notify;

	/* We MUST attach after the MIPI sensor */
	ret = bus_register_notifier(&soc_camera_bus_type, &priv->notifier);
	if (ret < 0) {
		dev_err(&pdev->dev, "CSI2 cannot register notifier\n");
		goto ernotify;
	}

	if (!request_mem_region(res->start, resource_size(res), pdev->name)) {
		dev_err(&pdev->dev, "CSI2 register region already claimed\n");
		ret = -EBUSY;
		goto ereqreg;
	}

	priv->base = ioremap(res->start, resource_size(res));
	if (!priv->base) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "Unable to ioremap CSI2 registers.\n");
		goto eremap;
	}

	priv->pdev = pdev;

	v4l2_subdev_init(&priv->subdev, &sh_csi2_subdev_ops);
	v4l2_set_subdevdata(&priv->subdev, &pdev->dev);

	platform_set_drvdata(pdev, priv);

	pm_runtime_enable(&pdev->dev);

	dev_dbg(&pdev->dev, "CSI2 probed.\n");

	return 0;

eremap:
	release_mem_region(res->start, resource_size(res));
ereqreg:
	bus_unregister_notifier(&soc_camera_bus_type, &priv->notifier);
ernotify:
	kfree(priv);

	return ret;
}

static __devexit int sh_csi2_remove(struct platform_device *pdev)
{
	struct sh_csi2 *priv = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	bus_unregister_notifier(&soc_camera_bus_type, &priv->notifier);
	pm_runtime_disable(&pdev->dev);
	iounmap(priv->base);
	release_mem_region(res->start, resource_size(res));
	platform_set_drvdata(pdev, NULL);
	kfree(priv);

	return 0;
}

static struct platform_driver __refdata sh_csi2_pdrv = {
	.remove  = __devexit_p(sh_csi2_remove),
	.driver  = {
		.name	= "sh-mobile-csi2",
		.owner	= THIS_MODULE,
	},
};

static int __init sh_csi2_init(void)
{
	return platform_driver_probe(&sh_csi2_pdrv, sh_csi2_probe);
}

static void __exit sh_csi2_exit(void)
{
	platform_driver_unregister(&sh_csi2_pdrv);
}

module_init(sh_csi2_init);
module_exit(sh_csi2_exit);

MODULE_DESCRIPTION("SH-Mobile MIPI CSI-2 driver");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sh-mobile-csi2");
