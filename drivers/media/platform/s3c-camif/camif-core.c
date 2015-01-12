/*
 * s3c24xx/s3c64xx SoC series Camera Interface (CAMIF) driver
 *
 * Copyright (C) 2012 Sylwester Nawrocki <sylvester.nawrocki@gmail.com>
 * Copyright (C) 2012 Tomasz Figa <tomasz.figa@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 */
#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/bug.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/version.h>

#include <media/media-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "camif-core.h"

static char *camif_clocks[CLK_MAX_NUM] = {
	/* HCLK CAMIF clock */
	[CLK_GATE]	= "camif",
	/* CAMIF / external camera sensor master clock */
	[CLK_CAM]	= "camera",
};

static const struct camif_fmt camif_formats[] = {
	{
		.name		= "YUV 4:2:2 planar, Y/Cb/Cr",
		.fourcc		= V4L2_PIX_FMT_YUV422P,
		.depth		= 16,
		.ybpp		= 1,
		.color		= IMG_FMT_YCBCR422P,
		.colplanes	= 3,
		.flags		= FMT_FL_S3C24XX_CODEC |
				  FMT_FL_S3C64XX,
	}, {
		.name		= "YUV 4:2:0 planar, Y/Cb/Cr",
		.fourcc		= V4L2_PIX_FMT_YUV420,
		.depth		= 12,
		.ybpp		= 1,
		.color		= IMG_FMT_YCBCR420,
		.colplanes	= 3,
		.flags		= FMT_FL_S3C24XX_CODEC |
				  FMT_FL_S3C64XX,
	}, {
		.name		= "YVU 4:2:0 planar, Y/Cr/Cb",
		.fourcc		= V4L2_PIX_FMT_YVU420,
		.depth		= 12,
		.ybpp		= 1,
		.color		= IMG_FMT_YCRCB420,
		.colplanes	= 3,
		.flags		= FMT_FL_S3C24XX_CODEC |
				  FMT_FL_S3C64XX,
	}, {
		.name		= "RGB565, 16 bpp",
		.fourcc		= V4L2_PIX_FMT_RGB565X,
		.depth		= 16,
		.ybpp		= 2,
		.color		= IMG_FMT_RGB565,
		.colplanes	= 1,
		.flags		= FMT_FL_S3C24XX_PREVIEW |
				  FMT_FL_S3C64XX,
	}, {
		.name		= "XRGB8888, 32 bpp",
		.fourcc		= V4L2_PIX_FMT_RGB32,
		.depth		= 32,
		.ybpp		= 4,
		.color		= IMG_FMT_XRGB8888,
		.colplanes	= 1,
		.flags		= FMT_FL_S3C24XX_PREVIEW |
				  FMT_FL_S3C64XX,
	}, {
		.name		= "BGR666",
		.fourcc		= V4L2_PIX_FMT_BGR666,
		.depth		= 32,
		.ybpp		= 4,
		.color		= IMG_FMT_RGB666,
		.colplanes	= 1,
		.flags		= FMT_FL_S3C64XX,
	}
};

/**
 * s3c_camif_find_format() - lookup camif color format by fourcc or an index
 * @pixelformat: fourcc to match, ignored if null
 * @index: index to the camif_formats array, ignored if negative
 */
const struct camif_fmt *s3c_camif_find_format(struct camif_vp *vp,
					      const u32 *pixelformat,
					      int index)
{
	const struct camif_fmt *fmt, *def_fmt = NULL;
	unsigned int i;
	int id = 0;

	if (index >= (int)ARRAY_SIZE(camif_formats))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(camif_formats); ++i) {
		fmt = &camif_formats[i];
		if (vp && !(vp->fmt_flags & fmt->flags))
			continue;
		if (pixelformat && fmt->fourcc == *pixelformat)
			return fmt;
		if (index == id)
			def_fmt = fmt;
		id++;
	}
	return def_fmt;
}

static int camif_get_scaler_factor(u32 src, u32 tar, u32 *ratio, u32 *shift)
{
	unsigned int sh = 6;

	if (src >= 64 * tar)
		return -EINVAL;

	while (sh--) {
		unsigned int tmp = 1 << sh;
		if (src >= tar * tmp) {
			*shift = sh, *ratio = tmp;
			return 0;
		}
	}
	*shift = 0, *ratio = 1;
	return 0;
}

int s3c_camif_get_scaler_config(struct camif_vp *vp,
				struct camif_scaler *scaler)
{
	struct v4l2_rect *camif_crop = &vp->camif->camif_crop;
	int source_x = camif_crop->width;
	int source_y = camif_crop->height;
	int target_x = vp->out_frame.rect.width;
	int target_y = vp->out_frame.rect.height;
	int ret;

	if (vp->rotation == 90 || vp->rotation == 270)
		swap(target_x, target_y);

	ret = camif_get_scaler_factor(source_x, target_x, &scaler->pre_h_ratio,
				      &scaler->h_shift);
	if (ret < 0)
		return ret;

	ret = camif_get_scaler_factor(source_y, target_y, &scaler->pre_v_ratio,
				      &scaler->v_shift);
	if (ret < 0)
		return ret;

	scaler->pre_dst_width = source_x / scaler->pre_h_ratio;
	scaler->pre_dst_height = source_y / scaler->pre_v_ratio;

	scaler->main_h_ratio = (source_x << 8) / (target_x << scaler->h_shift);
	scaler->main_v_ratio = (source_y << 8) / (target_y << scaler->v_shift);

	scaler->scaleup_h = (target_x >= source_x);
	scaler->scaleup_v = (target_y >= source_y);

	scaler->copy = 0;

	pr_debug("H: ratio: %u, shift: %u. V: ratio: %u, shift: %u.\n",
		 scaler->pre_h_ratio, scaler->h_shift,
		 scaler->pre_v_ratio, scaler->v_shift);

	pr_debug("Source: %dx%d, Target: %dx%d, scaleup_h/v: %d/%d\n",
		 source_x, source_y, target_x, target_y,
		 scaler->scaleup_h, scaler->scaleup_v);

	return 0;
}

static int camif_register_sensor(struct camif_dev *camif)
{
	struct s3c_camif_sensor_info *sensor = &camif->pdata.sensor;
	struct v4l2_device *v4l2_dev = &camif->v4l2_dev;
	struct i2c_adapter *adapter;
	struct v4l2_subdev_format format;
	struct v4l2_subdev *sd;
	int ret;

	camif->sensor.sd = NULL;

	if (sensor->i2c_board_info.addr == 0)
		return -EINVAL;

	adapter = i2c_get_adapter(sensor->i2c_bus_num);
	if (adapter == NULL) {
		v4l2_warn(v4l2_dev, "failed to get I2C adapter %d\n",
			  sensor->i2c_bus_num);
		return -EPROBE_DEFER;
	}

	sd = v4l2_i2c_new_subdev_board(v4l2_dev, adapter,
				       &sensor->i2c_board_info, NULL);
	if (sd == NULL) {
		i2c_put_adapter(adapter);
		v4l2_warn(v4l2_dev, "failed to acquire subdev %s\n",
			  sensor->i2c_board_info.type);
		return -EPROBE_DEFER;
	}
	camif->sensor.sd = sd;

	v4l2_info(v4l2_dev, "registered sensor subdevice %s\n", sd->name);

	/* Get initial pixel format and set it at the camif sink pad */
	format.pad = 0;
	format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &format);

	if (ret < 0)
		return 0;

	format.pad = CAMIF_SD_PAD_SINK;
	v4l2_subdev_call(&camif->subdev, pad, set_fmt, NULL, &format);

	v4l2_info(sd, "Initial format from sensor: %dx%d, %#x\n",
		  format.format.width, format.format.height,
		  format.format.code);
	return 0;
}

static void camif_unregister_sensor(struct camif_dev *camif)
{
	struct v4l2_subdev *sd = camif->sensor.sd;
	struct i2c_client *client = sd ? v4l2_get_subdevdata(sd) : NULL;
	struct i2c_adapter *adapter;

	if (client == NULL)
		return;

	adapter = client->adapter;
	v4l2_device_unregister_subdev(sd);
	camif->sensor.sd = NULL;
	i2c_unregister_device(client);
	i2c_put_adapter(adapter);
}

static int camif_create_media_links(struct camif_dev *camif)
{
	int i, ret;

	ret = media_entity_create_link(&camif->sensor.sd->entity, 0,
				&camif->subdev.entity, CAMIF_SD_PAD_SINK,
				MEDIA_LNK_FL_IMMUTABLE |
				MEDIA_LNK_FL_ENABLED);
	if (ret)
		return ret;

	for (i = 1; i < CAMIF_SD_PADS_NUM && !ret; i++) {
		ret = media_entity_create_link(&camif->subdev.entity, i,
				&camif->vp[i - 1].vdev.entity, 0,
				MEDIA_LNK_FL_IMMUTABLE |
				MEDIA_LNK_FL_ENABLED);
	}

	return ret;
}

static int camif_register_video_nodes(struct camif_dev *camif)
{
	int ret = s3c_camif_register_video_node(camif, VP_CODEC);
	if (ret < 0)
		return ret;

	return s3c_camif_register_video_node(camif, VP_PREVIEW);
}

static void camif_unregister_video_nodes(struct camif_dev *camif)
{
	s3c_camif_unregister_video_node(camif, VP_CODEC);
	s3c_camif_unregister_video_node(camif, VP_PREVIEW);
}

static void camif_unregister_media_entities(struct camif_dev *camif)
{
	camif_unregister_video_nodes(camif);
	camif_unregister_sensor(camif);
	s3c_camif_unregister_subdev(camif);
}

/*
 * Media device
 */
static int camif_media_dev_register(struct camif_dev *camif)
{
	struct media_device *md = &camif->media_dev;
	struct v4l2_device *v4l2_dev = &camif->v4l2_dev;
	unsigned int ip_rev = camif->variant->ip_revision;
	int ret;

	memset(md, 0, sizeof(*md));
	snprintf(md->model, sizeof(md->model), "SAMSUNG S3C%s CAMIF",
		 ip_rev == S3C6410_CAMIF_IP_REV ? "6410" : "244X");
	strlcpy(md->bus_info, "platform", sizeof(md->bus_info));
	md->hw_revision = ip_rev;
	md->driver_version = KERNEL_VERSION(1, 0, 0);

	md->dev = camif->dev;

	strlcpy(v4l2_dev->name, "s3c-camif", sizeof(v4l2_dev->name));
	v4l2_dev->mdev = md;

	ret = v4l2_device_register(camif->dev, v4l2_dev);
	if (ret < 0)
		return ret;

	ret = media_device_register(md);
	if (ret < 0)
		v4l2_device_unregister(v4l2_dev);

	return ret;
}

static void camif_clk_put(struct camif_dev *camif)
{
	int i;

	for (i = 0; i < CLK_MAX_NUM; i++) {
		if (IS_ERR(camif->clock[i]))
			continue;
		clk_unprepare(camif->clock[i]);
		clk_put(camif->clock[i]);
		camif->clock[i] = ERR_PTR(-EINVAL);
	}
}

static int camif_clk_get(struct camif_dev *camif)
{
	int ret, i;

	for (i = 1; i < CLK_MAX_NUM; i++)
		camif->clock[i] = ERR_PTR(-EINVAL);

	for (i = 0; i < CLK_MAX_NUM; i++) {
		camif->clock[i] = clk_get(camif->dev, camif_clocks[i]);
		if (IS_ERR(camif->clock[i])) {
			ret = PTR_ERR(camif->clock[i]);
			goto err;
		}
		ret = clk_prepare(camif->clock[i]);
		if (ret < 0) {
			clk_put(camif->clock[i]);
			camif->clock[i] = NULL;
			goto err;
		}
	}
	return 0;
err:
	camif_clk_put(camif);
	dev_err(camif->dev, "failed to get clock: %s\n",
		camif_clocks[i]);
	return ret;
}

/*
 * The CAMIF device has two relatively independent data processing paths
 * that can source data from memory or the common camera input frontend.
 * Register interrupts for each data processing path (camif_vp).
 */
static int camif_request_irqs(struct platform_device *pdev,
			      struct camif_dev *camif)
{
	int irq, ret, i;

	for (i = 0; i < CAMIF_VP_NUM; i++) {
		struct camif_vp *vp = &camif->vp[i];

		init_waitqueue_head(&vp->irq_queue);

		irq = platform_get_irq(pdev, i);
		if (irq <= 0) {
			dev_err(&pdev->dev, "failed to get IRQ %d\n", i);
			return -ENXIO;
		}

		ret = devm_request_irq(&pdev->dev, irq, s3c_camif_irq_handler,
				       0, dev_name(&pdev->dev), vp);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to install IRQ: %d\n", ret);
			break;
		}
	}

	return ret;
}

static int s3c_camif_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct s3c_camif_plat_data *pdata = dev->platform_data;
	struct s3c_camif_drvdata *drvdata;
	struct camif_dev *camif;
	struct resource *mres;
	int ret = 0;

	camif = devm_kzalloc(dev, sizeof(*camif), GFP_KERNEL);
	if (!camif)
		return -ENOMEM;

	spin_lock_init(&camif->slock);
	mutex_init(&camif->lock);

	camif->dev = dev;

	if (!pdata || !pdata->gpio_get || !pdata->gpio_put) {
		dev_err(dev, "wrong platform data\n");
		return -EINVAL;
	}

	camif->pdata = *pdata;
	drvdata = (void *)platform_get_device_id(pdev)->driver_data;
	camif->variant = drvdata->variant;

	mres = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	camif->io_base = devm_ioremap_resource(dev, mres);
	if (IS_ERR(camif->io_base))
		return PTR_ERR(camif->io_base);

	ret = camif_request_irqs(pdev, camif);
	if (ret < 0)
		return ret;

	ret = pdata->gpio_get();
	if (ret < 0)
		return ret;

	ret = s3c_camif_create_subdev(camif);
	if (ret < 0)
		goto err_sd;

	ret = camif_clk_get(camif);
	if (ret < 0)
		goto err_clk;

	platform_set_drvdata(pdev, camif);
	clk_set_rate(camif->clock[CLK_CAM],
			camif->pdata.sensor.clock_frequency);

	dev_info(dev, "sensor clock frequency: %lu\n",
		 clk_get_rate(camif->clock[CLK_CAM]));
	/*
	 * Set initial pixel format, resolution and crop rectangle.
	 * Must be done before a sensor subdev is registered as some
	 * settings are overrode with values from sensor subdev.
	 */
	s3c_camif_set_defaults(camif);

	pm_runtime_enable(dev);

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto err_pm;

	/* Initialize contiguous memory allocator */
	camif->alloc_ctx = vb2_dma_contig_init_ctx(dev);
	if (IS_ERR(camif->alloc_ctx)) {
		ret = PTR_ERR(camif->alloc_ctx);
		goto err_alloc;
	}

	ret = camif_media_dev_register(camif);
	if (ret < 0)
		goto err_mdev;

	ret = camif_register_sensor(camif);
	if (ret < 0)
		goto err_sens;

	ret = v4l2_device_register_subdev(&camif->v4l2_dev, &camif->subdev);
	if (ret < 0)
		goto err_sens;

	mutex_lock(&camif->media_dev.graph_mutex);

	ret = v4l2_device_register_subdev_nodes(&camif->v4l2_dev);
	if (ret < 0)
		goto err_unlock;

	ret = camif_register_video_nodes(camif);
	if (ret < 0)
		goto err_unlock;

	ret = camif_create_media_links(camif);
	if (ret < 0)
		goto err_unlock;

	mutex_unlock(&camif->media_dev.graph_mutex);
	pm_runtime_put(dev);
	return 0;

err_unlock:
	mutex_unlock(&camif->media_dev.graph_mutex);
err_sens:
	v4l2_device_unregister(&camif->v4l2_dev);
	media_device_unregister(&camif->media_dev);
	camif_unregister_media_entities(camif);
err_mdev:
	vb2_dma_contig_cleanup_ctx(camif->alloc_ctx);
err_alloc:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);
err_pm:
	camif_clk_put(camif);
err_clk:
	s3c_camif_unregister_subdev(camif);
err_sd:
	pdata->gpio_put();
	return ret;
}

static int s3c_camif_remove(struct platform_device *pdev)
{
	struct camif_dev *camif = platform_get_drvdata(pdev);
	struct s3c_camif_plat_data *pdata = &camif->pdata;

	media_device_unregister(&camif->media_dev);
	camif_unregister_media_entities(camif);
	v4l2_device_unregister(&camif->v4l2_dev);

	pm_runtime_disable(&pdev->dev);
	camif_clk_put(camif);
	pdata->gpio_put();

	return 0;
}

static int s3c_camif_runtime_resume(struct device *dev)
{
	struct camif_dev *camif = dev_get_drvdata(dev);

	clk_enable(camif->clock[CLK_GATE]);
	/* null op on s3c244x */
	clk_enable(camif->clock[CLK_CAM]);
	return 0;
}

static int s3c_camif_runtime_suspend(struct device *dev)
{
	struct camif_dev *camif = dev_get_drvdata(dev);

	/* null op on s3c244x */
	clk_disable(camif->clock[CLK_CAM]);

	clk_disable(camif->clock[CLK_GATE]);
	return 0;
}

static const struct s3c_camif_variant s3c244x_camif_variant = {
	.vp_pix_limits = {
		[VP_CODEC] = {
			.max_out_width		= 4096,
			.max_sc_out_width	= 2048,
			.out_width_align	= 16,
			.min_out_width		= 16,
			.max_height		= 4096,
		},
		[VP_PREVIEW] = {
			.max_out_width		= 640,
			.max_sc_out_width	= 640,
			.out_width_align	= 16,
			.min_out_width		= 16,
			.max_height		= 480,
		}
	},
	.pix_limits = {
		.win_hor_offset_align	= 8,
	},
	.ip_revision = S3C244X_CAMIF_IP_REV,
};

static struct s3c_camif_drvdata s3c244x_camif_drvdata = {
	.variant	= &s3c244x_camif_variant,
	.bus_clk_freq	= 24000000UL,
};

static const struct s3c_camif_variant s3c6410_camif_variant = {
	.vp_pix_limits = {
		[VP_CODEC] = {
			.max_out_width		= 4096,
			.max_sc_out_width	= 2048,
			.out_width_align	= 16,
			.min_out_width		= 16,
			.max_height		= 4096,
		},
		[VP_PREVIEW] = {
			.max_out_width		= 4096,
			.max_sc_out_width	= 720,
			.out_width_align	= 16,
			.min_out_width		= 16,
			.max_height		= 4096,
		}
	},
	.pix_limits = {
		.win_hor_offset_align	= 8,
	},
	.ip_revision = S3C6410_CAMIF_IP_REV,
	.has_img_effect = 1,
	.vp_offset = 0x20,
};

static struct s3c_camif_drvdata s3c6410_camif_drvdata = {
	.variant	= &s3c6410_camif_variant,
	.bus_clk_freq	= 133000000UL,
};

static struct platform_device_id s3c_camif_driver_ids[] = {
	{
		.name		= "s3c2440-camif",
		.driver_data	= (unsigned long)&s3c244x_camif_drvdata,
	}, {
		.name		= "s3c6410-camif",
		.driver_data	= (unsigned long)&s3c6410_camif_drvdata,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, s3c_camif_driver_ids);

static const struct dev_pm_ops s3c_camif_pm_ops = {
	.runtime_suspend	= s3c_camif_runtime_suspend,
	.runtime_resume		= s3c_camif_runtime_resume,
};

static struct platform_driver s3c_camif_driver = {
	.probe		= s3c_camif_probe,
	.remove		= s3c_camif_remove,
	.id_table	= s3c_camif_driver_ids,
	.driver = {
		.name	= S3C_CAMIF_DRIVER_NAME,
		.pm	= &s3c_camif_pm_ops,
	}
};

module_platform_driver(s3c_camif_driver);

MODULE_AUTHOR("Sylwester Nawrocki <sylvester.nawrocki@gmail.com>");
MODULE_AUTHOR("Tomasz Figa <tomasz.figa@gmail.com>");
MODULE_DESCRIPTION("S3C24XX/S3C64XX SoC camera interface driver");
MODULE_LICENSE("GPL");
