// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Cadence MIPI-CSI2 RX Controller v1.3
 *
 * Copyright (C) 2017 Cadence Design Systems Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define CSI2RX_DEVICE_CFG_REG			0x000

#define CSI2RX_SOFT_RESET_REG			0x004
#define CSI2RX_SOFT_RESET_PROTOCOL			BIT(1)
#define CSI2RX_SOFT_RESET_FRONT				BIT(0)

#define CSI2RX_STATIC_CFG_REG			0x008
#define CSI2RX_STATIC_CFG_DLANE_MAP(llane, plane)	((plane) << (16 + (llane) * 4))
#define CSI2RX_STATIC_CFG_LANES_MASK			GENMASK(11, 8)

#define CSI2RX_STREAM_BASE(n)		(((n) + 1) * 0x100)

#define CSI2RX_STREAM_CTRL_REG(n)		(CSI2RX_STREAM_BASE(n) + 0x000)
#define CSI2RX_STREAM_CTRL_START			BIT(0)

#define CSI2RX_STREAM_DATA_CFG_REG(n)		(CSI2RX_STREAM_BASE(n) + 0x008)
#define CSI2RX_STREAM_DATA_CFG_EN_VC_SELECT		BIT(31)
#define CSI2RX_STREAM_DATA_CFG_VC_SELECT(n)		BIT((n) + 16)

#define CSI2RX_STREAM_CFG_REG(n)		(CSI2RX_STREAM_BASE(n) + 0x00c)
#define CSI2RX_STREAM_CFG_FIFO_MODE_LARGE_BUF		(1 << 8)

#define CSI2RX_LANES_MAX	4
#define CSI2RX_STREAMS_MAX	4

enum csi2rx_pads {
	CSI2RX_PAD_SINK,
	CSI2RX_PAD_SOURCE_STREAM0,
	CSI2RX_PAD_SOURCE_STREAM1,
	CSI2RX_PAD_SOURCE_STREAM2,
	CSI2RX_PAD_SOURCE_STREAM3,
	CSI2RX_PAD_MAX,
};

struct csi2rx_priv {
	struct device			*dev;
	unsigned int			count;

	/*
	 * Used to prevent race conditions between multiple,
	 * concurrent calls to start and stop.
	 */
	struct mutex			lock;

	void __iomem			*base;
	struct clk			*sys_clk;
	struct clk			*p_clk;
	struct clk			*pixel_clk[CSI2RX_STREAMS_MAX];
	struct phy			*dphy;

	u8				lanes[CSI2RX_LANES_MAX];
	u8				num_lanes;
	u8				max_lanes;
	u8				max_streams;
	bool				has_internal_dphy;

	struct v4l2_subdev		subdev;
	struct v4l2_async_notifier	notifier;
	struct media_pad		pads[CSI2RX_PAD_MAX];

	/* Remote source */
	struct v4l2_subdev		*source_subdev;
	int				source_pad;
};

static inline
struct csi2rx_priv *v4l2_subdev_to_csi2rx(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct csi2rx_priv, subdev);
}

static void csi2rx_reset(struct csi2rx_priv *csi2rx)
{
	writel(CSI2RX_SOFT_RESET_PROTOCOL | CSI2RX_SOFT_RESET_FRONT,
	       csi2rx->base + CSI2RX_SOFT_RESET_REG);

	udelay(10);

	writel(0, csi2rx->base + CSI2RX_SOFT_RESET_REG);
}

static int csi2rx_start(struct csi2rx_priv *csi2rx)
{
	unsigned int i;
	unsigned long lanes_used = 0;
	u32 reg;
	int ret;

	ret = clk_prepare_enable(csi2rx->p_clk);
	if (ret)
		return ret;

	csi2rx_reset(csi2rx);

	reg = csi2rx->num_lanes << 8;
	for (i = 0; i < csi2rx->num_lanes; i++) {
		reg |= CSI2RX_STATIC_CFG_DLANE_MAP(i, csi2rx->lanes[i]);
		set_bit(csi2rx->lanes[i], &lanes_used);
	}

	/*
	 * Even the unused lanes need to be mapped. In order to avoid
	 * to map twice to the same physical lane, keep the lanes used
	 * in the previous loop, and only map unused physical lanes to
	 * the rest of our logical lanes.
	 */
	for (i = csi2rx->num_lanes; i < csi2rx->max_lanes; i++) {
		unsigned int idx = find_first_zero_bit(&lanes_used,
						       csi2rx->max_lanes);
		set_bit(idx, &lanes_used);
		reg |= CSI2RX_STATIC_CFG_DLANE_MAP(i, i + 1);
	}

	writel(reg, csi2rx->base + CSI2RX_STATIC_CFG_REG);

	ret = v4l2_subdev_call(csi2rx->source_subdev, video, s_stream, true);
	if (ret)
		goto err_disable_pclk;

	/*
	 * Create a static mapping between the CSI virtual channels
	 * and the output stream.
	 *
	 * This should be enhanced, but v4l2 lacks the support for
	 * changing that mapping dynamically.
	 *
	 * We also cannot enable and disable independent streams here,
	 * hence the reference counting.
	 */
	for (i = 0; i < csi2rx->max_streams; i++) {
		ret = clk_prepare_enable(csi2rx->pixel_clk[i]);
		if (ret)
			goto err_disable_pixclk;

		writel(CSI2RX_STREAM_CFG_FIFO_MODE_LARGE_BUF,
		       csi2rx->base + CSI2RX_STREAM_CFG_REG(i));

		writel(CSI2RX_STREAM_DATA_CFG_EN_VC_SELECT |
		       CSI2RX_STREAM_DATA_CFG_VC_SELECT(i),
		       csi2rx->base + CSI2RX_STREAM_DATA_CFG_REG(i));

		writel(CSI2RX_STREAM_CTRL_START,
		       csi2rx->base + CSI2RX_STREAM_CTRL_REG(i));
	}

	ret = clk_prepare_enable(csi2rx->sys_clk);
	if (ret)
		goto err_disable_pixclk;

	clk_disable_unprepare(csi2rx->p_clk);

	return 0;

err_disable_pixclk:
	for (; i > 0; i--)
		clk_disable_unprepare(csi2rx->pixel_clk[i - 1]);

err_disable_pclk:
	clk_disable_unprepare(csi2rx->p_clk);

	return ret;
}

static void csi2rx_stop(struct csi2rx_priv *csi2rx)
{
	unsigned int i;

	clk_prepare_enable(csi2rx->p_clk);
	clk_disable_unprepare(csi2rx->sys_clk);

	for (i = 0; i < csi2rx->max_streams; i++) {
		writel(0, csi2rx->base + CSI2RX_STREAM_CTRL_REG(i));

		clk_disable_unprepare(csi2rx->pixel_clk[i]);
	}

	clk_disable_unprepare(csi2rx->p_clk);

	if (v4l2_subdev_call(csi2rx->source_subdev, video, s_stream, false))
		dev_warn(csi2rx->dev, "Couldn't disable our subdev\n");
}

static int csi2rx_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct csi2rx_priv *csi2rx = v4l2_subdev_to_csi2rx(subdev);
	int ret = 0;

	mutex_lock(&csi2rx->lock);

	if (enable) {
		/*
		 * If we're not the first users, there's no need to
		 * enable the whole controller.
		 */
		if (!csi2rx->count) {
			ret = csi2rx_start(csi2rx);
			if (ret)
				goto out;
		}

		csi2rx->count++;
	} else {
		csi2rx->count--;

		/*
		 * Let the last user turn off the lights.
		 */
		if (!csi2rx->count)
			csi2rx_stop(csi2rx);
	}

out:
	mutex_unlock(&csi2rx->lock);
	return ret;
}

static const struct v4l2_subdev_video_ops csi2rx_video_ops = {
	.s_stream	= csi2rx_s_stream,
};

static const struct v4l2_subdev_ops csi2rx_subdev_ops = {
	.video		= &csi2rx_video_ops,
};

static int csi2rx_async_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *s_subdev,
			      struct v4l2_async_subdev *asd)
{
	struct v4l2_subdev *subdev = notifier->sd;
	struct csi2rx_priv *csi2rx = v4l2_subdev_to_csi2rx(subdev);

	csi2rx->source_pad = media_entity_get_fwnode_pad(&s_subdev->entity,
							 s_subdev->fwnode,
							 MEDIA_PAD_FL_SOURCE);
	if (csi2rx->source_pad < 0) {
		dev_err(csi2rx->dev, "Couldn't find output pad for subdev %s\n",
			s_subdev->name);
		return csi2rx->source_pad;
	}

	csi2rx->source_subdev = s_subdev;

	dev_dbg(csi2rx->dev, "Bound %s pad: %d\n", s_subdev->name,
		csi2rx->source_pad);

	return media_create_pad_link(&csi2rx->source_subdev->entity,
				     csi2rx->source_pad,
				     &csi2rx->subdev.entity, 0,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static const struct v4l2_async_notifier_operations csi2rx_notifier_ops = {
	.bound		= csi2rx_async_bound,
};

static int csi2rx_get_resources(struct csi2rx_priv *csi2rx,
				struct platform_device *pdev)
{
	struct resource *res;
	unsigned char i;
	u32 dev_cfg;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	csi2rx->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(csi2rx->base))
		return PTR_ERR(csi2rx->base);

	csi2rx->sys_clk = devm_clk_get(&pdev->dev, "sys_clk");
	if (IS_ERR(csi2rx->sys_clk)) {
		dev_err(&pdev->dev, "Couldn't get sys clock\n");
		return PTR_ERR(csi2rx->sys_clk);
	}

	csi2rx->p_clk = devm_clk_get(&pdev->dev, "p_clk");
	if (IS_ERR(csi2rx->p_clk)) {
		dev_err(&pdev->dev, "Couldn't get P clock\n");
		return PTR_ERR(csi2rx->p_clk);
	}

	csi2rx->dphy = devm_phy_optional_get(&pdev->dev, "dphy");
	if (IS_ERR(csi2rx->dphy)) {
		dev_err(&pdev->dev, "Couldn't get external D-PHY\n");
		return PTR_ERR(csi2rx->dphy);
	}

	/*
	 * FIXME: Once we'll have external D-PHY support, the check
	 * will need to be removed.
	 */
	if (csi2rx->dphy) {
		dev_err(&pdev->dev, "External D-PHY not supported yet\n");
		return -EINVAL;
	}

	clk_prepare_enable(csi2rx->p_clk);
	dev_cfg = readl(csi2rx->base + CSI2RX_DEVICE_CFG_REG);
	clk_disable_unprepare(csi2rx->p_clk);

	csi2rx->max_lanes = dev_cfg & 7;
	if (csi2rx->max_lanes > CSI2RX_LANES_MAX) {
		dev_err(&pdev->dev, "Invalid number of lanes: %u\n",
			csi2rx->max_lanes);
		return -EINVAL;
	}

	csi2rx->max_streams = (dev_cfg >> 4) & 7;
	if (csi2rx->max_streams > CSI2RX_STREAMS_MAX) {
		dev_err(&pdev->dev, "Invalid number of streams: %u\n",
			csi2rx->max_streams);
		return -EINVAL;
	}

	csi2rx->has_internal_dphy = dev_cfg & BIT(3) ? true : false;

	/*
	 * FIXME: Once we'll have internal D-PHY support, the check
	 * will need to be removed.
	 */
	if (csi2rx->has_internal_dphy) {
		dev_err(&pdev->dev, "Internal D-PHY not supported yet\n");
		return -EINVAL;
	}

	for (i = 0; i < csi2rx->max_streams; i++) {
		char clk_name[16];

		snprintf(clk_name, sizeof(clk_name), "pixel_if%u_clk", i);
		csi2rx->pixel_clk[i] = devm_clk_get(&pdev->dev, clk_name);
		if (IS_ERR(csi2rx->pixel_clk[i])) {
			dev_err(&pdev->dev, "Couldn't get clock %s\n", clk_name);
			return PTR_ERR(csi2rx->pixel_clk[i]);
		}
	}

	return 0;
}

static int csi2rx_parse_dt(struct csi2rx_priv *csi2rx)
{
	struct v4l2_fwnode_endpoint v4l2_ep = { .bus_type = 0 };
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *fwh;
	struct device_node *ep;
	int ret;

	ep = of_graph_get_endpoint_by_regs(csi2rx->dev->of_node, 0, 0);
	if (!ep)
		return -EINVAL;

	fwh = of_fwnode_handle(ep);
	ret = v4l2_fwnode_endpoint_parse(fwh, &v4l2_ep);
	if (ret) {
		dev_err(csi2rx->dev, "Could not parse v4l2 endpoint\n");
		of_node_put(ep);
		return ret;
	}

	if (v4l2_ep.bus_type != V4L2_MBUS_CSI2_DPHY) {
		dev_err(csi2rx->dev, "Unsupported media bus type: 0x%x\n",
			v4l2_ep.bus_type);
		of_node_put(ep);
		return -EINVAL;
	}

	memcpy(csi2rx->lanes, v4l2_ep.bus.mipi_csi2.data_lanes,
	       sizeof(csi2rx->lanes));
	csi2rx->num_lanes = v4l2_ep.bus.mipi_csi2.num_data_lanes;
	if (csi2rx->num_lanes > csi2rx->max_lanes) {
		dev_err(csi2rx->dev, "Unsupported number of data-lanes: %d\n",
			csi2rx->num_lanes);
		of_node_put(ep);
		return -EINVAL;
	}

	v4l2_async_notifier_init(&csi2rx->notifier);

	asd = v4l2_async_notifier_add_fwnode_remote_subdev(&csi2rx->notifier,
							   fwh,
							   struct v4l2_async_subdev);
	of_node_put(ep);
	if (IS_ERR(asd))
		return PTR_ERR(asd);

	csi2rx->notifier.ops = &csi2rx_notifier_ops;

	ret = v4l2_async_subdev_notifier_register(&csi2rx->subdev,
						  &csi2rx->notifier);
	if (ret)
		v4l2_async_notifier_cleanup(&csi2rx->notifier);

	return ret;
}

static int csi2rx_probe(struct platform_device *pdev)
{
	struct csi2rx_priv *csi2rx;
	unsigned int i;
	int ret;

	csi2rx = kzalloc(sizeof(*csi2rx), GFP_KERNEL);
	if (!csi2rx)
		return -ENOMEM;
	platform_set_drvdata(pdev, csi2rx);
	csi2rx->dev = &pdev->dev;
	mutex_init(&csi2rx->lock);

	ret = csi2rx_get_resources(csi2rx, pdev);
	if (ret)
		goto err_free_priv;

	ret = csi2rx_parse_dt(csi2rx);
	if (ret)
		goto err_free_priv;

	csi2rx->subdev.owner = THIS_MODULE;
	csi2rx->subdev.dev = &pdev->dev;
	v4l2_subdev_init(&csi2rx->subdev, &csi2rx_subdev_ops);
	v4l2_set_subdevdata(&csi2rx->subdev, &pdev->dev);
	snprintf(csi2rx->subdev.name, V4L2_SUBDEV_NAME_SIZE, "%s.%s",
		 KBUILD_MODNAME, dev_name(&pdev->dev));

	/* Create our media pads */
	csi2rx->subdev.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	csi2rx->pads[CSI2RX_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	for (i = CSI2RX_PAD_SOURCE_STREAM0; i < CSI2RX_PAD_MAX; i++)
		csi2rx->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&csi2rx->subdev.entity, CSI2RX_PAD_MAX,
				     csi2rx->pads);
	if (ret)
		goto err_cleanup;

	ret = v4l2_async_register_subdev(&csi2rx->subdev);
	if (ret < 0)
		goto err_cleanup;

	dev_info(&pdev->dev,
		 "Probed CSI2RX with %u/%u lanes, %u streams, %s D-PHY\n",
		 csi2rx->num_lanes, csi2rx->max_lanes, csi2rx->max_streams,
		 csi2rx->has_internal_dphy ? "internal" : "no");

	return 0;

err_cleanup:
	v4l2_async_notifier_cleanup(&csi2rx->notifier);
err_free_priv:
	kfree(csi2rx);
	return ret;
}

static int csi2rx_remove(struct platform_device *pdev)
{
	struct csi2rx_priv *csi2rx = platform_get_drvdata(pdev);

	v4l2_async_unregister_subdev(&csi2rx->subdev);
	kfree(csi2rx);

	return 0;
}

static const struct of_device_id csi2rx_of_table[] = {
	{ .compatible = "cdns,csi2rx" },
	{ },
};
MODULE_DEVICE_TABLE(of, csi2rx_of_table);

static struct platform_driver csi2rx_driver = {
	.probe	= csi2rx_probe,
	.remove	= csi2rx_remove,

	.driver	= {
		.name		= "cdns-csi2rx",
		.of_match_table	= csi2rx_of_table,
	},
};
module_platform_driver(csi2rx_driver);
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@bootlin.com>");
MODULE_DESCRIPTION("Cadence CSI2-RX controller");
MODULE_LICENSE("GPL");
