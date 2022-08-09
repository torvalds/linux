// SPDX-License-Identifier: GPL-2.0+
/*
 * MIPI CSI-2 Receiver Subdev for Freescale i.MX6 SOC.
 *
 * Copyright (c) 2012-2017 Mentor Graphics Inc.
 */
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include "imx-media.h"

/*
 * there must be 5 pads: 1 input pad from sensor, and
 * the 4 virtual channel output pads
 */
#define CSI2_SINK_PAD       0
#define CSI2_NUM_SINK_PADS  1
#define CSI2_NUM_SRC_PADS   4
#define CSI2_NUM_PADS       5

/*
 * The default maximum bit-rate per lane in Mbps, if the
 * source subdev does not provide V4L2_CID_LINK_FREQ.
 */
#define CSI2_DEFAULT_MAX_MBPS 849

struct csi2_dev {
	struct device          *dev;
	struct v4l2_subdev      sd;
	struct v4l2_async_notifier notifier;
	struct media_pad       pad[CSI2_NUM_PADS];
	struct clk             *dphy_clk;
	struct clk             *pllref_clk;
	struct clk             *pix_clk; /* what is this? */
	void __iomem           *base;

	struct v4l2_subdev	*remote;
	unsigned int		remote_pad;
	unsigned short		data_lanes;

	/* lock to protect all members below */
	struct mutex lock;

	struct v4l2_mbus_framefmt format_mbus;

	int                     stream_count;
	struct v4l2_subdev      *src_sd;
	bool                    sink_linked[CSI2_NUM_SRC_PADS];
};

#define DEVICE_NAME "imx6-mipi-csi2"

/* Register offsets */
#define CSI2_VERSION            0x000
#define CSI2_N_LANES            0x004
#define CSI2_PHY_SHUTDOWNZ      0x008
#define CSI2_DPHY_RSTZ          0x00c
#define CSI2_RESETN             0x010
#define CSI2_PHY_STATE          0x014
#define PHY_STOPSTATEDATA_BIT   4
#define PHY_STOPSTATEDATA(n)    BIT(PHY_STOPSTATEDATA_BIT + (n))
#define PHY_RXCLKACTIVEHS       BIT(8)
#define PHY_RXULPSCLKNOT        BIT(9)
#define PHY_STOPSTATECLK        BIT(10)
#define CSI2_DATA_IDS_1         0x018
#define CSI2_DATA_IDS_2         0x01c
#define CSI2_ERR1               0x020
#define CSI2_ERR2               0x024
#define CSI2_MSK1               0x028
#define CSI2_MSK2               0x02c
#define CSI2_PHY_TST_CTRL0      0x030
#define PHY_TESTCLR		BIT(0)
#define PHY_TESTCLK		BIT(1)
#define CSI2_PHY_TST_CTRL1      0x034
#define PHY_TESTEN		BIT(16)
/*
 * i.MX CSI2IPU Gasket registers follow. The CSI2IPU gasket is
 * not part of the MIPI CSI-2 core, but its registers fall in the
 * same register map range.
 */
#define CSI2IPU_GASKET		0xf00
#define CSI2IPU_YUV422_YUYV	BIT(2)

static inline struct csi2_dev *sd_to_dev(struct v4l2_subdev *sdev)
{
	return container_of(sdev, struct csi2_dev, sd);
}

static inline struct csi2_dev *notifier_to_dev(struct v4l2_async_notifier *n)
{
	return container_of(n, struct csi2_dev, notifier);
}

/*
 * The required sequence of MIPI CSI-2 startup as specified in the i.MX6
 * reference manual is as follows:
 *
 * 1. Deassert presetn signal (global reset).
 *        It's not clear what this "global reset" signal is (maybe APB
 *        global reset), but in any case this step would be probably
 *        be carried out during driver load in csi2_probe().
 *
 * 2. Configure MIPI Camera Sensor to put all Tx lanes in LP-11 state.
 *        This must be carried out by the MIPI sensor's s_power(ON) subdev
 *        op.
 *
 * 3. D-PHY initialization.
 * 4. CSI2 Controller programming (Set N_LANES, deassert PHY_SHUTDOWNZ,
 *    deassert PHY_RSTZ, deassert CSI2_RESETN).
 * 5. Read the PHY status register (PHY_STATE) to confirm that all data and
 *    clock lanes of the D-PHY are in LP-11 state.
 * 6. Configure the MIPI Camera Sensor to start transmitting a clock on the
 *    D-PHY clock lane.
 * 7. CSI2 Controller programming - Read the PHY status register (PHY_STATE)
 *    to confirm that the D-PHY is receiving a clock on the D-PHY clock lane.
 *
 * All steps 3 through 7 are carried out by csi2_s_stream(ON) here. Step
 * 6 is accomplished by calling the source subdev's s_stream(ON) between
 * steps 5 and 7.
 */

static void csi2_enable(struct csi2_dev *csi2, bool enable)
{
	if (enable) {
		writel(0x1, csi2->base + CSI2_PHY_SHUTDOWNZ);
		writel(0x1, csi2->base + CSI2_DPHY_RSTZ);
		writel(0x1, csi2->base + CSI2_RESETN);
	} else {
		writel(0x0, csi2->base + CSI2_PHY_SHUTDOWNZ);
		writel(0x0, csi2->base + CSI2_DPHY_RSTZ);
		writel(0x0, csi2->base + CSI2_RESETN);
	}
}

static void csi2_set_lanes(struct csi2_dev *csi2, unsigned int lanes)
{
	writel(lanes - 1, csi2->base + CSI2_N_LANES);
}

static void dw_mipi_csi2_phy_write(struct csi2_dev *csi2,
				   u32 test_code, u32 test_data)
{
	/* Clear PHY test interface */
	writel(PHY_TESTCLR, csi2->base + CSI2_PHY_TST_CTRL0);
	writel(0x0, csi2->base + CSI2_PHY_TST_CTRL1);
	writel(0x0, csi2->base + CSI2_PHY_TST_CTRL0);

	/* Raise test interface strobe signal */
	writel(PHY_TESTCLK, csi2->base + CSI2_PHY_TST_CTRL0);

	/* Configure address write on falling edge and lower strobe signal */
	writel(PHY_TESTEN | test_code, csi2->base + CSI2_PHY_TST_CTRL1);
	writel(0x0, csi2->base + CSI2_PHY_TST_CTRL0);

	/* Configure data write on rising edge and raise strobe signal */
	writel(test_data, csi2->base + CSI2_PHY_TST_CTRL1);
	writel(PHY_TESTCLK, csi2->base + CSI2_PHY_TST_CTRL0);

	/* Clear strobe signal */
	writel(0x0, csi2->base + CSI2_PHY_TST_CTRL0);
}

/*
 * This table is based on the table documented at
 * https://community.nxp.com/docs/DOC-94312. It assumes
 * a 27MHz D-PHY pll reference clock.
 */
static const struct {
	u32 max_mbps;
	u32 hsfreqrange_sel;
} hsfreq_map[] = {
	{ 90, 0x00}, {100, 0x20}, {110, 0x40}, {125, 0x02},
	{140, 0x22}, {150, 0x42}, {160, 0x04}, {180, 0x24},
	{200, 0x44}, {210, 0x06}, {240, 0x26}, {250, 0x46},
	{270, 0x08}, {300, 0x28}, {330, 0x48}, {360, 0x2a},
	{400, 0x4a}, {450, 0x0c}, {500, 0x2c}, {550, 0x0e},
	{600, 0x2e}, {650, 0x10}, {700, 0x30}, {750, 0x12},
	{800, 0x32}, {850, 0x14}, {900, 0x34}, {950, 0x54},
	{1000, 0x74},
};

static int max_mbps_to_hsfreqrange_sel(u32 max_mbps)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hsfreq_map); i++)
		if (hsfreq_map[i].max_mbps > max_mbps)
			return hsfreq_map[i].hsfreqrange_sel;

	return -EINVAL;
}

static int csi2_dphy_init(struct csi2_dev *csi2)
{
	struct v4l2_ctrl *ctrl;
	u32 mbps_per_lane;
	int sel;

	ctrl = v4l2_ctrl_find(csi2->src_sd->ctrl_handler,
			      V4L2_CID_LINK_FREQ);
	if (!ctrl)
		mbps_per_lane = CSI2_DEFAULT_MAX_MBPS;
	else
		mbps_per_lane = DIV_ROUND_UP_ULL(2 * ctrl->qmenu_int[ctrl->val],
						 USEC_PER_SEC);

	sel = max_mbps_to_hsfreqrange_sel(mbps_per_lane);
	if (sel < 0)
		return sel;

	dw_mipi_csi2_phy_write(csi2, 0x44, sel);

	return 0;
}

/*
 * Waits for ultra-low-power state on D-PHY clock lane. This is currently
 * unused and may not be needed at all, but keep around just in case.
 */
static int __maybe_unused csi2_dphy_wait_ulp(struct csi2_dev *csi2)
{
	u32 reg;
	int ret;

	/* wait for ULP on clock lane */
	ret = readl_poll_timeout(csi2->base + CSI2_PHY_STATE, reg,
				 !(reg & PHY_RXULPSCLKNOT), 0, 500000);
	if (ret) {
		v4l2_err(&csi2->sd, "ULP timeout, phy_state = 0x%08x\n", reg);
		return ret;
	}

	/* wait until no errors on bus */
	ret = readl_poll_timeout(csi2->base + CSI2_ERR1, reg,
				 reg == 0x0, 0, 500000);
	if (ret) {
		v4l2_err(&csi2->sd, "stable bus timeout, err1 = 0x%08x\n", reg);
		return ret;
	}

	return 0;
}

/* Waits for low-power LP-11 state on data and clock lanes. */
static void csi2_dphy_wait_stopstate(struct csi2_dev *csi2, unsigned int lanes)
{
	u32 mask, reg;
	int ret;

	mask = PHY_STOPSTATECLK | (((1 << lanes) - 1) << PHY_STOPSTATEDATA_BIT);

	ret = readl_poll_timeout(csi2->base + CSI2_PHY_STATE, reg,
				 (reg & mask) == mask, 0, 500000);
	if (ret) {
		v4l2_warn(&csi2->sd, "LP-11 wait timeout, likely a sensor driver bug, expect capture failures.\n");
		v4l2_warn(&csi2->sd, "phy_state = 0x%08x\n", reg);
	}
}

/* Wait for active clock on the clock lane. */
static int csi2_dphy_wait_clock_lane(struct csi2_dev *csi2)
{
	u32 reg;
	int ret;

	ret = readl_poll_timeout(csi2->base + CSI2_PHY_STATE, reg,
				 (reg & PHY_RXCLKACTIVEHS), 0, 500000);
	if (ret) {
		v4l2_err(&csi2->sd, "clock lane timeout, phy_state = 0x%08x\n",
			 reg);
		return ret;
	}

	return 0;
}

/* Setup the i.MX CSI2IPU Gasket */
static void csi2ipu_gasket_init(struct csi2_dev *csi2)
{
	u32 reg = 0;

	switch (csi2->format_mbus.code) {
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YUYV8_1X16:
		reg = CSI2IPU_YUV422_YUYV;
		break;
	default:
		break;
	}

	writel(reg, csi2->base + CSI2IPU_GASKET);
}

static int csi2_get_active_lanes(struct csi2_dev *csi2, unsigned int *lanes)
{
	struct v4l2_mbus_config mbus_config = { 0 };
	unsigned int num_lanes = UINT_MAX;
	int ret;

	*lanes = csi2->data_lanes;

	ret = v4l2_subdev_call(csi2->remote, pad, get_mbus_config,
			       csi2->remote_pad, &mbus_config);
	if (ret == -ENOIOCTLCMD) {
		dev_dbg(csi2->dev, "No remote mbus configuration available\n");
		return 0;
	}

	if (ret) {
		dev_err(csi2->dev, "Failed to get remote mbus configuration\n");
		return ret;
	}

	if (mbus_config.type != V4L2_MBUS_CSI2_DPHY) {
		dev_err(csi2->dev, "Unsupported media bus type %u\n",
			mbus_config.type);
		return -EINVAL;
	}

	switch (mbus_config.flags & V4L2_MBUS_CSI2_LANES) {
	case V4L2_MBUS_CSI2_1_LANE:
		num_lanes = 1;
		break;
	case V4L2_MBUS_CSI2_2_LANE:
		num_lanes = 2;
		break;
	case V4L2_MBUS_CSI2_3_LANE:
		num_lanes = 3;
		break;
	case V4L2_MBUS_CSI2_4_LANE:
		num_lanes = 4;
		break;
	default:
		num_lanes = csi2->data_lanes;
		break;
	}

	if (num_lanes > csi2->data_lanes) {
		dev_err(csi2->dev,
			"Unsupported mbus config: too many data lanes %u\n",
			num_lanes);
		return -EINVAL;
	}

	*lanes = num_lanes;

	return 0;
}

static int csi2_start(struct csi2_dev *csi2)
{
	unsigned int lanes;
	int ret;

	ret = clk_prepare_enable(csi2->pix_clk);
	if (ret)
		return ret;

	/* setup the gasket */
	csi2ipu_gasket_init(csi2);

	/* Step 3 */
	ret = csi2_dphy_init(csi2);
	if (ret)
		goto err_disable_clk;

	ret = csi2_get_active_lanes(csi2, &lanes);
	if (ret)
		goto err_disable_clk;

	/* Step 4 */
	csi2_set_lanes(csi2, lanes);
	csi2_enable(csi2, true);

	/* Step 5 */
	csi2_dphy_wait_stopstate(csi2, lanes);

	/* Step 6 */
	ret = v4l2_subdev_call(csi2->src_sd, video, s_stream, 1);
	ret = (ret && ret != -ENOIOCTLCMD) ? ret : 0;
	if (ret)
		goto err_assert_reset;

	/* Step 7 */
	ret = csi2_dphy_wait_clock_lane(csi2);
	if (ret)
		goto err_stop_upstream;

	return 0;

err_stop_upstream:
	v4l2_subdev_call(csi2->src_sd, video, s_stream, 0);
err_assert_reset:
	csi2_enable(csi2, false);
err_disable_clk:
	clk_disable_unprepare(csi2->pix_clk);
	return ret;
}

static void csi2_stop(struct csi2_dev *csi2)
{
	/* stop upstream */
	v4l2_subdev_call(csi2->src_sd, video, s_stream, 0);

	csi2_enable(csi2, false);
	clk_disable_unprepare(csi2->pix_clk);
}

/*
 * V4L2 subdev operations.
 */

static int csi2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	int i, ret = 0;

	mutex_lock(&csi2->lock);

	if (!csi2->src_sd) {
		ret = -EPIPE;
		goto out;
	}

	for (i = 0; i < CSI2_NUM_SRC_PADS; i++) {
		if (csi2->sink_linked[i])
			break;
	}
	if (i >= CSI2_NUM_SRC_PADS) {
		ret = -EPIPE;
		goto out;
	}

	/*
	 * enable/disable streaming only if stream_count is
	 * going from 0 to 1 / 1 to 0.
	 */
	if (csi2->stream_count != !enable)
		goto update_count;

	dev_dbg(csi2->dev, "stream %s\n", enable ? "ON" : "OFF");
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

	dev_dbg(csi2->dev, "link setup %s -> %s", remote->entity->name,
		local->entity->name);

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

static struct v4l2_mbus_framefmt *
__csi2_get_fmt(struct csi2_dev *csi2, struct v4l2_subdev_state *sd_state,
	       unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&csi2->sd, sd_state, pad);
	else
		return &csi2->format_mbus;
}

static int csi2_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *sd_state,
			struct v4l2_subdev_format *sdformat)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	struct v4l2_mbus_framefmt *fmt;

	mutex_lock(&csi2->lock);

	fmt = __csi2_get_fmt(csi2, sd_state, sdformat->pad, sdformat->which);

	sdformat->format = *fmt;

	mutex_unlock(&csi2->lock);

	return 0;
}

static int csi2_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *sd_state,
			struct v4l2_subdev_format *sdformat)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	struct v4l2_mbus_framefmt *fmt;
	int ret = 0;

	if (sdformat->pad >= CSI2_NUM_PADS)
		return -EINVAL;

	mutex_lock(&csi2->lock);

	if (csi2->stream_count > 0) {
		ret = -EBUSY;
		goto out;
	}

	/* Output pads mirror active input pad, no limits on input pads */
	if (sdformat->pad != CSI2_SINK_PAD)
		sdformat->format = csi2->format_mbus;

	fmt = __csi2_get_fmt(csi2, sd_state, sdformat->pad, sdformat->which);

	*fmt = sdformat->format;
out:
	mutex_unlock(&csi2->lock);
	return ret;
}

static int csi2_registered(struct v4l2_subdev *sd)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);

	/* set a default mbus format  */
	return imx_media_init_mbus_fmt(&csi2->format_mbus,
				      IMX_MEDIA_DEF_PIX_WIDTH,
				      IMX_MEDIA_DEF_PIX_HEIGHT, 0,
				      V4L2_FIELD_NONE, NULL);
}

static const struct media_entity_operations csi2_entity_ops = {
	.link_setup = csi2_link_setup,
	.link_validate = v4l2_subdev_link_validate,
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,
};

static const struct v4l2_subdev_video_ops csi2_video_ops = {
	.s_stream = csi2_s_stream,
};

static const struct v4l2_subdev_pad_ops csi2_pad_ops = {
	.init_cfg = imx_media_init_cfg,
	.get_fmt = csi2_get_fmt,
	.set_fmt = csi2_set_fmt,
};

static const struct v4l2_subdev_ops csi2_subdev_ops = {
	.video = &csi2_video_ops,
	.pad = &csi2_pad_ops,
};

static const struct v4l2_subdev_internal_ops csi2_internal_ops = {
	.registered = csi2_registered,
};

static int csi2_notify_bound(struct v4l2_async_notifier *notifier,
			     struct v4l2_subdev *sd,
			     struct v4l2_async_subdev *asd)
{
	struct csi2_dev *csi2 = notifier_to_dev(notifier);
	struct media_pad *sink = &csi2->sd.entity.pads[CSI2_SINK_PAD];
	int pad;

	pad = media_entity_get_fwnode_pad(&sd->entity, asd->match.fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (pad < 0) {
		dev_err(csi2->dev, "Failed to find pad for %s\n", sd->name);
		return pad;
	}

	csi2->remote = sd;
	csi2->remote_pad = pad;

	dev_dbg(csi2->dev, "Bound %s pad: %d\n", sd->name, pad);

	return v4l2_create_fwnode_links_to_pad(sd, sink, 0);
}

static void csi2_notify_unbind(struct v4l2_async_notifier *notifier,
			       struct v4l2_subdev *sd,
			       struct v4l2_async_subdev *asd)
{
	struct csi2_dev *csi2 = notifier_to_dev(notifier);

	csi2->remote = NULL;
}

static const struct v4l2_async_notifier_operations csi2_notify_ops = {
	.bound = csi2_notify_bound,
	.unbind = csi2_notify_unbind,
};

static int csi2_async_register(struct csi2_dev *csi2)
{
	struct v4l2_fwnode_endpoint vep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *ep;
	int ret;

	v4l2_async_nf_init(&csi2->notifier);

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(csi2->dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep)
		return -ENOTCONN;

	ret = v4l2_fwnode_endpoint_parse(ep, &vep);
	if (ret)
		goto err_parse;

	csi2->data_lanes = vep.bus.mipi_csi2.num_data_lanes;

	dev_dbg(csi2->dev, "data lanes: %d\n", vep.bus.mipi_csi2.num_data_lanes);
	dev_dbg(csi2->dev, "flags: 0x%08x\n", vep.bus.mipi_csi2.flags);

	asd = v4l2_async_nf_add_fwnode_remote(&csi2->notifier, ep,
					      struct v4l2_async_subdev);
	fwnode_handle_put(ep);

	if (IS_ERR(asd))
		return PTR_ERR(asd);

	csi2->notifier.ops = &csi2_notify_ops;

	ret = v4l2_async_subdev_nf_register(&csi2->sd, &csi2->notifier);
	if (ret)
		return ret;

	return v4l2_async_register_subdev(&csi2->sd);

err_parse:
	fwnode_handle_put(ep);
	return ret;
}

static int csi2_probe(struct platform_device *pdev)
{
	struct csi2_dev *csi2;
	struct resource *res;
	int i, ret;

	csi2 = devm_kzalloc(&pdev->dev, sizeof(*csi2), GFP_KERNEL);
	if (!csi2)
		return -ENOMEM;

	csi2->dev = &pdev->dev;

	v4l2_subdev_init(&csi2->sd, &csi2_subdev_ops);
	v4l2_set_subdevdata(&csi2->sd, &pdev->dev);
	csi2->sd.internal_ops = &csi2_internal_ops;
	csi2->sd.entity.ops = &csi2_entity_ops;
	csi2->sd.dev = &pdev->dev;
	csi2->sd.owner = THIS_MODULE;
	csi2->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	strscpy(csi2->sd.name, DEVICE_NAME, sizeof(csi2->sd.name));
	csi2->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	csi2->sd.grp_id = IMX_MEDIA_GRP_ID_CSI2;

	for (i = 0; i < CSI2_NUM_PADS; i++) {
		csi2->pad[i].flags = (i == CSI2_SINK_PAD) ?
		MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;
	}

	ret = media_entity_pads_init(&csi2->sd.entity, CSI2_NUM_PADS,
				     csi2->pad);
	if (ret)
		return ret;

	csi2->pllref_clk = devm_clk_get(&pdev->dev, "ref");
	if (IS_ERR(csi2->pllref_clk)) {
		v4l2_err(&csi2->sd, "failed to get pll reference clock\n");
		return PTR_ERR(csi2->pllref_clk);
	}

	csi2->dphy_clk = devm_clk_get(&pdev->dev, "dphy");
	if (IS_ERR(csi2->dphy_clk)) {
		v4l2_err(&csi2->sd, "failed to get dphy clock\n");
		return PTR_ERR(csi2->dphy_clk);
	}

	csi2->pix_clk = devm_clk_get(&pdev->dev, "pix");
	if (IS_ERR(csi2->pix_clk)) {
		v4l2_err(&csi2->sd, "failed to get pixel clock\n");
		return PTR_ERR(csi2->pix_clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		v4l2_err(&csi2->sd, "failed to get platform resources\n");
		return -ENODEV;
	}

	csi2->base = devm_ioremap(&pdev->dev, res->start, PAGE_SIZE);
	if (!csi2->base)
		return -ENOMEM;

	mutex_init(&csi2->lock);

	ret = clk_prepare_enable(csi2->pllref_clk);
	if (ret) {
		v4l2_err(&csi2->sd, "failed to enable pllref_clk\n");
		goto rmmutex;
	}

	ret = clk_prepare_enable(csi2->dphy_clk);
	if (ret) {
		v4l2_err(&csi2->sd, "failed to enable dphy_clk\n");
		goto pllref_off;
	}

	platform_set_drvdata(pdev, &csi2->sd);

	ret = csi2_async_register(csi2);
	if (ret)
		goto clean_notifier;

	return 0;

clean_notifier:
	v4l2_async_nf_unregister(&csi2->notifier);
	v4l2_async_nf_cleanup(&csi2->notifier);
	clk_disable_unprepare(csi2->dphy_clk);
pllref_off:
	clk_disable_unprepare(csi2->pllref_clk);
rmmutex:
	mutex_destroy(&csi2->lock);
	return ret;
}

static int csi2_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct csi2_dev *csi2 = sd_to_dev(sd);

	v4l2_async_nf_unregister(&csi2->notifier);
	v4l2_async_nf_cleanup(&csi2->notifier);
	v4l2_async_unregister_subdev(sd);
	clk_disable_unprepare(csi2->dphy_clk);
	clk_disable_unprepare(csi2->pllref_clk);
	mutex_destroy(&csi2->lock);
	media_entity_cleanup(&sd->entity);

	return 0;
}

static const struct of_device_id csi2_dt_ids[] = {
	{ .compatible = "fsl,imx6-mipi-csi2", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, csi2_dt_ids);

static struct platform_driver csi2_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = csi2_dt_ids,
	},
	.probe = csi2_probe,
	.remove = csi2_remove,
};

module_platform_driver(csi2_driver);

MODULE_DESCRIPTION("i.MX5/6 MIPI CSI-2 Receiver driver");
MODULE_AUTHOR("Steve Longerbeam <steve_longerbeam@mentor.com>");
MODULE_LICENSE("GPL");
