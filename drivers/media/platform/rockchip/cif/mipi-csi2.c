// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip MIPI CSI2 Driver
 *
 * Copyright (C) 2019 Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/*
 * there must be 5 pads: 1 input pad from sensor, and
 * the 4 virtual channel output pads
 */
#define CSI2_SINK_PAD			0
#define CSI2_NUM_SINK_PADS		1
#define CSI2_NUM_SRC_PADS		4
#define CSI2_NUM_PADS			5
#define CSI2_NUM_PADS_SINGLE_LINK	2
#define MAX_CSI2_SENSORS		2

/*
 * The default maximum bit-rate per lane in Mbps, if the
 * source subdev does not provide V4L2_CID_LINK_FREQ.
 */
#define CSI2_DEFAULT_MAX_MBPS 849

#define IMX_MEDIA_GRP_ID_CSI2      BIT(8)
#define CSIHOST_MAX_ERRINT_COUNT	10

enum rkcsi2_chip_id {
	CHIP_PX30_CSI2,
	CHIP_RK1808_CSI2,
	CHIP_RK3128_CSI2,
	CHIP_RK3288_CSI2
};

enum csi2_pads {
	RK_CSI2_PAD_SINK = 0,
	RK_CSI2X_PAD_SOURCE0,
	RK_CSI2X_PAD_SOURCE1,
	RK_CSI2X_PAD_SOURCE2,
	RK_CSI2X_PAD_SOURCE3
};

enum host_type_t {
	RK_CSI_RXHOST,
	RK_DSI_RXHOST
};

struct csi2_match_data {
	int chip_id;
	int num_pads;
};

struct csi2_sensor {
	struct v4l2_subdev *sd;
	struct v4l2_mbus_config mbus;
	int lanes;
};

struct csi2_dev {
	struct device          *dev;
	struct v4l2_subdev      sd;
	struct media_pad       pad[CSI2_NUM_PADS];
	struct clk             *pix_clk; /* what is this? */
	void __iomem           *base;
	struct v4l2_async_notifier	notifier;
	struct v4l2_fwnode_bus_mipi_csi2 bus;

	/* lock to protect all members below */
	struct mutex lock;

	struct v4l2_mbus_framefmt format_mbus;

	int                     stream_count;
	struct v4l2_subdev      *src_sd;
	bool                    sink_linked[CSI2_NUM_SRC_PADS];
	struct csi2_sensor	sensors[MAX_CSI2_SENSORS];
	const struct csi2_match_data	*match_data;
	int num_sensors;
};

#define DEVICE_NAME "rockchip-mipi-csi2"

/* CSI Host Registers Define */
#define CSIHOST_N_LANES		0x04
#define CSIHOST_PHY_RSTZ	0x0c
#define CSIHOST_RESETN		0x10
#define CSIHOST_ERR1		0x20
#define CSIHOST_ERR2		0x24
#define CSIHOST_MSK1		0x28
#define CSIHOST_MSK2		0x2c
#define CSIHOST_CONTROL		0x40

#define SW_CPHY_EN(x)		((x) << 0)
#define SW_DSI_EN(x)		((x) << 4)
#define SW_DATATYPE_FS(x)	((x) << 8)
#define SW_DATATYPE_FE(x)	((x) << 14)
#define SW_DATATYPE_LS(x)	((x) << 20)
#define SW_DATATYPE_LE(x)	((x) << 26)

#define write_csihost_reg(base, addr, val)  writel(val, (addr) + (base))
#define read_csihost_reg(base, addr) readl((addr) + (base))

static inline struct csi2_dev *sd_to_dev(struct v4l2_subdev *sdev)
{
	return container_of(sdev, struct csi2_dev, sd);
}

static struct csi2_sensor *sd_to_sensor(struct csi2_dev *csi2,
					struct v4l2_subdev *sd)
{
	int i;

	for (i = 0; i < csi2->num_sensors; ++i)
		if (csi2->sensors[i].sd == sd)
			return &csi2->sensors[i];

	return NULL;
}

static struct v4l2_subdev *get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &sd->entity.pads[RK_CSI2_PAD_SINK];
	remote = media_entity_remote_pad(local);
	if (!remote) {
		v4l2_warn(sd, "No link between dphy and sensor\n");
		return NULL;
	}

	sensor_me = media_entity_remote_pad(local)->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

static void csi2_disable(struct csi2_dev *csi2)
{
	void __iomem *base = csi2->base;

	write_csihost_reg(base, CSIHOST_RESETN, 0);
	write_csihost_reg(base, CSIHOST_MSK1, 0xffffffff);
	write_csihost_reg(base, CSIHOST_MSK2, 0xffffffff);

	v4l2_info(&csi2->sd, "mipi csi host disable\n");
}

static void csi2_enable(struct csi2_dev *csi2,
			enum host_type_t host_type)
{
	void __iomem *base = csi2->base;
	int lanes = csi2->bus.num_data_lanes;

	write_csihost_reg(base, CSIHOST_N_LANES, lanes - 1);

	if (host_type == RK_DSI_RXHOST) {
		write_csihost_reg(base, CSIHOST_CONTROL,
				  SW_CPHY_EN(0) | SW_DSI_EN(1) |
				  SW_DATATYPE_FS(0x01) | SW_DATATYPE_FE(0x11) |
				  SW_DATATYPE_LS(0x21) | SW_DATATYPE_LE(0x31));
		/* Disable some error interrupt when HOST work on DSI RX mode */
		write_csihost_reg(base, CSIHOST_MSK1, 0xe00000f0);
		write_csihost_reg(base, CSIHOST_MSK2, 0xff00);
	} else {
		write_csihost_reg(base, CSIHOST_CONTROL,
				  SW_CPHY_EN(0) | SW_DSI_EN(0));
		write_csihost_reg(base, CSIHOST_MSK1, 0);
		write_csihost_reg(base, CSIHOST_MSK2, 0);
	}

	write_csihost_reg(base, CSIHOST_RESETN, 1);

	v4l2_info(&csi2->sd, "mipi csi host enable\n");
}

static int csi2_start(struct csi2_dev *csi2)
{
	enum host_type_t host_type;
	int ret;

	ret = clk_prepare_enable(csi2->pix_clk);
	if (ret)
		return ret;

	if (csi2->format_mbus.code == MEDIA_BUS_FMT_RGB888_1X24)
		host_type = RK_DSI_RXHOST;
	else
		host_type = RK_CSI_RXHOST;

	csi2_enable(csi2, host_type);

	pr_debug("stream sd: %s\n", csi2->src_sd->name);
	ret = v4l2_subdev_call(csi2->src_sd, video, s_stream, 1);
	ret = (ret && ret != -ENOIOCTLCMD) ? ret : 0;
	if (ret)
		goto err_assert_reset;

	return 0;

err_assert_reset:
	csi2_disable(csi2);
	clk_disable_unprepare(csi2->pix_clk);
	return ret;
}

static void csi2_stop(struct csi2_dev *csi2)
{
	/* stop upstream */
	v4l2_subdev_call(csi2->src_sd, video, s_stream, 0);

	csi2_disable(csi2);
	clk_disable_unprepare(csi2->pix_clk);
}

/*
 * V4L2 subdev operations.
 */

static int csi2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	int ret = 0;

	mutex_lock(&csi2->lock);

	dev_err(csi2->dev, "stream %s, src_sd: %p\n",
		enable ? "ON" : "OFF",
		csi2->src_sd);

	/*
	 * enable/disable streaming only if stream_count is
	 * going from 0 to 1 / 1 to 0.
	 */
	if (csi2->stream_count != !enable)
		goto update_count;

	dev_err(csi2->dev, "stream %s\n", enable ? "ON" : "OFF");

	if (enable)
		ret = csi2_start(csi2);
	else
		csi2_stop(csi2);
	if (ret)
		goto out;

update_count:
	csi2->stream_count += enable ? 1 : -1;
	if (csi2->stream_count < 0)
		csi2->stream_count = 0;
out:
	mutex_unlock(&csi2->lock);
	return ret;
}

static int csi2_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct csi2_dev *csi2 = sd_to_dev(sd);
	struct v4l2_subdev *remote_sd;
	int ret = 0;

	remote_sd = media_entity_to_v4l2_subdev(remote->entity);

	mutex_lock(&csi2->lock);

	if (local->flags & MEDIA_PAD_FL_SOURCE) {
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (csi2->sink_linked[local->index - 1]) {
				ret = -EBUSY;
				goto out;
			}
			csi2->sink_linked[local->index - 1] = true;
		} else {
			csi2->sink_linked[local->index - 1] = false;
		}
	} else {
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (csi2->src_sd) {
				ret = -EBUSY;
				goto out;
			}
			csi2->src_sd = remote_sd;
		} else {
			csi2->src_sd = NULL;
		}
	}

out:
	mutex_unlock(&csi2->lock);
	return ret;
}

static int csi2_media_init(struct v4l2_subdev *sd)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	int i = 0, num_pads = 0;

	num_pads = csi2->match_data->num_pads;

	for (i = 0; i < num_pads; i++) {
		csi2->pad[i].flags = (i == CSI2_SINK_PAD) ?
		MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;
	}

	csi2->pad[RK_CSI2X_PAD_SOURCE0].flags =
		MEDIA_PAD_FL_SOURCE | MEDIA_PAD_FL_MUST_CONNECT;
	csi2->pad[RK_CSI2_PAD_SINK].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;

	/* set a default mbus format  */
	csi2->format_mbus.code =  MEDIA_BUS_FMT_UYVY8_2X8;
	csi2->format_mbus.field = V4L2_FIELD_NONE;
	csi2->format_mbus.width = 1920;
	csi2->format_mbus.height = 1080;

	v4l2_err(&csi2->sd, "media entry init\n");
	return media_entity_init(&sd->entity, num_pads, csi2->pad, 0);
}

/* csi2 accepts all fmt/size from sensor */
static int csi2_get_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *fmt)
{
	struct v4l2_subdev *sensor = get_remote_sensor(sd);

	/*
	 * Do not allow format changes and just relay whatever
	 * set currently in the sensor.
	 */
	return v4l2_subdev_call(sensor, pad, get_fmt, NULL, fmt);
}

static int csi2_g_mbus_config(struct v4l2_subdev *sd,
			      struct v4l2_mbus_config *mbus)
{
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	int ret;

	ret = v4l2_subdev_call(sensor_sd, video, g_mbus_config, mbus);
	if (ret)
		return ret;

	return 0;
}

static const struct media_entity_operations csi2_entity_ops = {
	.link_setup = csi2_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_video_ops csi2_video_ops = {
	.g_mbus_config = csi2_g_mbus_config,
	.s_stream = csi2_s_stream,
};

static const struct v4l2_subdev_pad_ops csi2_pad_ops = {
	.get_fmt = csi2_get_set_fmt,
	.set_fmt = csi2_get_set_fmt,
};

static const struct v4l2_subdev_ops csi2_subdev_ops = {
	.video = &csi2_video_ops,
	.pad = &csi2_pad_ops,
};

static int csi2_parse_endpoint(struct device *dev,
			       struct v4l2_fwnode_endpoint *vep,
			       struct v4l2_async_subdev *asd)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct csi2_dev *csi2 = sd_to_dev(sd);

	if (vep->bus_type != V4L2_MBUS_CSI2) {
		v4l2_err(&csi2->sd,
			 "invalid bus type: %d, must be MIPI CSI2\n",
			 vep->bus_type);
		return -EINVAL;
	}

	csi2->bus = vep->bus.mipi_csi2;

	return 0;
}

/* The .bound() notifier callback when a match is found */
static int
csi2_notifier_bound(struct v4l2_async_notifier *notifier,
		    struct v4l2_subdev *sd,
		    struct v4l2_async_subdev *asd)
{
	struct csi2_dev *csi2 = container_of(notifier,
			struct csi2_dev,
			notifier);
	struct csi2_sensor *sensor;
	unsigned int pad, ret;

	if (csi2->num_sensors == ARRAY_SIZE(csi2->sensors))
		return -EBUSY;

	sensor = &csi2->sensors[csi2->num_sensors++];
	sensor->sd = sd;

	for (pad = 0; pad < sd->entity.num_pads; pad++)
		if (sensor->sd->entity.pads[pad].flags
					& MEDIA_PAD_FL_SOURCE)
			break;

	if (pad == sensor->sd->entity.num_pads) {
		dev_err(csi2->dev,
			"failed to find src pad for %s\n",
			sd->name);

		return -ENXIO;
	}

	ret = media_entity_create_link(&sensor->sd->entity, pad,
				       &csi2->sd.entity, RK_CSI2_PAD_SINK,
				       0/* csi2->num_sensors != 1 ? 0 : MEDIA_LNK_FL_ENABLED */);
	if (ret) {
		dev_err(csi2->dev,
			"failed to create link for %s\n",
			sd->name);
		return ret;
	}

	ret = media_entity_setup_link(csi2->sd.entity.links,
				      MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(csi2->dev,
			"failed to create link for %s\n",
			sensor->sd->name);
		return ret;
	}

	return 0;
}

/* The .unbind callback */
static void csi2_notifier_unbind(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *sd,
				 struct v4l2_async_subdev *asd)
{
	struct csi2_dev *csi2 = container_of(notifier,
						  struct csi2_dev,
						  notifier);
	struct csi2_sensor *sensor = sd_to_sensor(csi2, sd);

	sensor->sd = NULL;
}

static const struct
v4l2_async_notifier_operations csi2_async_ops = {
	.bound = csi2_notifier_bound,
	.unbind = csi2_notifier_unbind,
};

static irqreturn_t rk_csirx_irq1_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct csi2_dev *csi2 = sd_to_dev(dev_get_drvdata(dev));
	static int csi_err1_cnt;
	u32 val;

	val = read_csihost_reg(csi2->base, CSIHOST_ERR1);
	if (val) {
		write_csihost_reg(csi2->base,
				  CSIHOST_ERR1, 0x0);
		if (++csi_err1_cnt > CSIHOST_MAX_ERRINT_COUNT) {
			v4l2_err(&csi2->sd, "mask csi2 host msk1!\n");
			write_csihost_reg(csi2->base,
					  CSIHOST_MSK1, 0xffffffff);
			csi_err1_cnt = 0;
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t rk_csirx_irq2_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct csi2_dev *csi2 = sd_to_dev(dev_get_drvdata(dev));
	static int csi_err2_cnt;
	u32 val;

	val = read_csihost_reg(csi2->base, CSIHOST_ERR2);
	if (val) {
		if (++csi_err2_cnt > CSIHOST_MAX_ERRINT_COUNT) {
			v4l2_err(&csi2->sd, "mask csi2 host msk2!\n");
			write_csihost_reg(csi2->base,
					  CSIHOST_MSK2, 0xffffffff);
			csi_err2_cnt = 0;
		}
	}

	return IRQ_HANDLED;
}

static int csi2_notifier(struct csi2_dev *csi2)
{
	struct v4l2_async_notifier *ntf = &csi2->notifier;
	int ret;

	ret = v4l2_async_notifier_parse_fwnode_endpoints_by_port(
		csi2->dev, &csi2->notifier,
		sizeof(struct v4l2_async_subdev), 0,
		csi2_parse_endpoint);
	if (ret < 0)
		return ret;

	if (!ntf->num_subdevs)
		return -ENODEV;	/* no endpoint */

	csi2->sd.subdev_notifier = &csi2->notifier;
	csi2->notifier.ops = &csi2_async_ops;
	ret = v4l2_async_subdev_notifier_register(&csi2->sd, &csi2->notifier);
	if (ret) {
		v4l2_err(&csi2->sd,
			 "failed to register async notifier : %d\n",
			 ret);
		v4l2_async_notifier_cleanup(&csi2->notifier);
		return ret;
	}
	return v4l2_async_register_subdev(&csi2->sd);
}

static const struct csi2_match_data rk1808_csi2_match_data = {
	.chip_id = CHIP_RK1808_CSI2,
	.num_pads = CSI2_NUM_PADS
};

static const struct csi2_match_data rk3288_csi2_match_data = {
	.chip_id = CHIP_RK3288_CSI2,
	.num_pads = CSI2_NUM_PADS_SINGLE_LINK
};

static const struct of_device_id csi2_dt_ids[] = {
	{
		.compatible = "rockchip,rk1808-mipi-csi2",
		.data = &rk1808_csi2_match_data,
	},
	{
		.compatible = "rockchip,rk3288-mipi-csi2",
		.data = &rk3288_csi2_match_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, csi2_dt_ids);

static int csi2_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct csi2_dev *csi2 = NULL;
	struct resource *res;
	const struct csi2_match_data *data;
	int ret, irq;

	match = of_match_node(csi2_dt_ids, node);
	if (IS_ERR(match))
		return PTR_ERR(match);
	data = match->data;

	csi2 = devm_kzalloc(&pdev->dev, sizeof(*csi2), GFP_KERNEL);
	if (!csi2)
		return -ENOMEM;

	csi2->dev = &pdev->dev;
	csi2->match_data = data;

	v4l2_subdev_init(&csi2->sd, &csi2_subdev_ops);
	v4l2_set_subdevdata(&csi2->sd, &pdev->dev);
	csi2->sd.entity.ops = &csi2_entity_ops;
	csi2->sd.dev = &pdev->dev;
	csi2->sd.owner = THIS_MODULE;
	csi2->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ret = strscpy(csi2->sd.name, DEVICE_NAME, sizeof(csi2->sd.name));
	platform_set_drvdata(pdev, &csi2->sd);
	/* csi2->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE; */
	/* csi2->sd.grp_id = IMX_MEDIA_GRP_ID_CSI2; */

	csi2->pix_clk = devm_clk_get(&pdev->dev, "pclk_csi2host");
	if (IS_ERR(csi2->pix_clk)) {
		v4l2_err(&csi2->sd, "failed to get pixel clock\n");
		ret = PTR_ERR(csi2->pix_clk);
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	csi2->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(csi2->base)) {
		resource_size_t offset = res->start;
		resource_size_t size = resource_size(res);

		dev_warn(&pdev->dev, "avoid secondary mipi resource check!\n");

		csi2->base = devm_ioremap(&pdev->dev, offset, size);
		if (IS_ERR(csi2->base)) {
			dev_err(&pdev->dev, "Failed to ioremap resource\n");

			return PTR_ERR(csi2->base);
		}
	}

	irq = platform_get_irq_byname(pdev, "csi-intr1");
	if (irq > 0) {
		ret = devm_request_irq(&pdev->dev, irq,
				       rk_csirx_irq1_handler, 0,
				       dev_driver_string(&pdev->dev),
				       &pdev->dev);
		if (ret < 0)
			v4l2_err(&csi2->sd, "request csi-intr1 irq failed: %d\n",
				 ret);
	} else {
		v4l2_err(&csi2->sd, "No found irq csi-intr1\n");
	}

	irq = platform_get_irq_byname(pdev, "csi-intr2");
	if (irq > 0) {
		ret = devm_request_irq(&pdev->dev, irq,
				       rk_csirx_irq2_handler, 0,
				       dev_driver_string(&pdev->dev),
				       &pdev->dev);
		if (ret < 0)
			v4l2_err(&csi2->sd, "request csi-intr2 failed: %d\n",
				 ret);
	} else {
		v4l2_err(&csi2->sd, "No found irq csi-intr2\n");
	}

	mutex_init(&csi2->lock);

	ret = csi2_media_init(&csi2->sd);
	if (ret < 0)
		goto rmmutex;
	ret = csi2_notifier(csi2);
	if (ret)
		goto rmmutex;
	v4l2_info(&csi2->sd, "probe success!\n");

	return 0;

rmmutex:
	mutex_destroy(&csi2->lock);
	return ret;
}

static int csi2_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct csi2_dev *csi2 = sd_to_dev(sd);

	v4l2_async_unregister_subdev(sd);
	mutex_destroy(&csi2->lock);
	media_entity_cleanup(&sd->entity);

	return 0;
}

static struct platform_driver csi2_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = csi2_dt_ids,
	},
	.probe = csi2_probe,
	.remove = csi2_remove,
};

module_platform_driver(csi2_driver);

MODULE_DESCRIPTION("Rockchip MIPI CSI2 driver");
MODULE_AUTHOR("Macrofly.xu <xuhf@rock-chips.com>");
MODULE_LICENSE("GPL");
