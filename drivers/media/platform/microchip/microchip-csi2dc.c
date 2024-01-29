// SPDX-License-Identifier: GPL-2.0-only
/*
 * Microchip CSI2 Demux Controller (CSI2DC) driver
 *
 * Copyright (C) 2018 Microchip Technology, Inc.
 *
 * Author: Eugen Hristev <eugen.hristev@microchip.com>
 *
 */

#include <linux/clk.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>

#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* Global configuration register */
#define CSI2DC_GCFG			0x0

/* MIPI sensor pixel clock is free running */
#define CSI2DC_GCFG_MIPIFRN		BIT(0)
/* GPIO parallel interface selection */
#define CSI2DC_GCFG_GPIOSEL		BIT(1)
/* Output waveform inter-line minimum delay */
#define CSI2DC_GCFG_HLC(v)		((v) << 4)
#define CSI2DC_GCFG_HLC_MASK		GENMASK(7, 4)
/* SAMA7G5 requires a HLC delay of 15 */
#define SAMA7G5_HLC			(15)

/* Global control register */
#define CSI2DC_GCTLR			0x04
#define CSI2DC_GCTLR_SWRST		BIT(0)

/* Global status register */
#define CSI2DC_GS			0x08

/* SSP interrupt status register */
#define CSI2DC_SSPIS			0x28
/* Pipe update register */
#define CSI2DC_PU			0xc0
/* Video pipe attributes update */
#define CSI2DC_PU_VP			BIT(0)

/* Pipe update status register */
#define CSI2DC_PUS			0xc4

/* Video pipeline Interrupt Status Register */
#define CSI2DC_VPISR			0xf4

/* Video pipeline enable register */
#define CSI2DC_VPE			0xf8
#define CSI2DC_VPE_ENABLE		BIT(0)

/* Video pipeline configuration register */
#define CSI2DC_VPCFG			0xfc
/* Data type */
#define CSI2DC_VPCFG_DT(v)		((v) << 0)
#define CSI2DC_VPCFG_DT_MASK		GENMASK(5, 0)
/* Virtual channel identifier */
#define CSI2DC_VPCFG_VC(v)		((v) << 6)
#define CSI2DC_VPCFG_VC_MASK		GENMASK(7, 6)
/* Decompression enable */
#define CSI2DC_VPCFG_DE			BIT(8)
/* Decoder mode */
#define CSI2DC_VPCFG_DM(v)		((v) << 9)
#define CSI2DC_VPCFG_DM_DECODER8TO12	0
/* Decoder predictor 2 selection */
#define CSI2DC_VPCFG_DP2		BIT(12)
/* Recommended memory storage */
#define CSI2DC_VPCFG_RMS		BIT(13)
/* Post adjustment */
#define CSI2DC_VPCFG_PA			BIT(14)

/* Video pipeline column register */
#define CSI2DC_VPCOL			0x100
/* Column number */
#define CSI2DC_VPCOL_COL(v)		((v) << 0)
#define CSI2DC_VPCOL_COL_MASK		GENMASK(15, 0)

/* Video pipeline row register */
#define CSI2DC_VPROW			0x104
/* Row number */
#define CSI2DC_VPROW_ROW(v)		((v) << 0)
#define CSI2DC_VPROW_ROW_MASK		GENMASK(15, 0)

/* Version register */
#define CSI2DC_VERSION			0x1fc

/* register read/write helpers */
#define csi2dc_readl(st, reg)		readl_relaxed((st)->base + (reg))
#define csi2dc_writel(st, reg, val)	writel_relaxed((val), \
					(st)->base + (reg))

/* supported RAW data types */
#define CSI2DC_DT_RAW6			0x28
#define CSI2DC_DT_RAW7			0x29
#define CSI2DC_DT_RAW8			0x2a
#define CSI2DC_DT_RAW10			0x2b
#define CSI2DC_DT_RAW12			0x2c
#define CSI2DC_DT_RAW14			0x2d
/* YUV data types */
#define CSI2DC_DT_YUV422_8B		0x1e

/*
 * struct csi2dc_format - CSI2DC format type struct
 * @mbus_code:		Media bus code for the format
 * @dt:			Data type constant for this format
 */
struct csi2dc_format {
	u32				mbus_code;
	u32				dt;
};

static const struct csi2dc_format csi2dc_formats[] = {
	{
		.mbus_code =		MEDIA_BUS_FMT_SRGGB8_1X8,
		.dt =			CSI2DC_DT_RAW8,
	}, {
		.mbus_code =		MEDIA_BUS_FMT_SBGGR8_1X8,
		.dt =			CSI2DC_DT_RAW8,
	}, {
		.mbus_code =		MEDIA_BUS_FMT_SGRBG8_1X8,
		.dt =			CSI2DC_DT_RAW8,
	}, {
		.mbus_code =		MEDIA_BUS_FMT_SGBRG8_1X8,
		.dt =			CSI2DC_DT_RAW8,
	}, {
		.mbus_code =		MEDIA_BUS_FMT_SRGGB10_1X10,
		.dt =			CSI2DC_DT_RAW10,
	}, {
		.mbus_code =		MEDIA_BUS_FMT_SBGGR10_1X10,
		.dt =			CSI2DC_DT_RAW10,
	}, {
		.mbus_code =		MEDIA_BUS_FMT_SGRBG10_1X10,
		.dt =			CSI2DC_DT_RAW10,
	}, {
		.mbus_code =		MEDIA_BUS_FMT_SGBRG10_1X10,
		.dt =			CSI2DC_DT_RAW10,
	}, {
		.mbus_code =		MEDIA_BUS_FMT_YUYV8_2X8,
		.dt =			CSI2DC_DT_YUV422_8B,
	},
};

enum mipi_csi_pads {
	CSI2DC_PAD_SINK			= 0,
	CSI2DC_PAD_SOURCE		= 1,
	CSI2DC_PADS_NUM			= 2,
};

/*
 * struct csi2dc_device - CSI2DC device driver data/config struct
 * @base:		Register map base address
 * @csi2dc_sd:		v4l2 subdevice for the csi2dc device
 *			This is the subdevice that the csi2dc device itself
 *			registers in v4l2 subsystem
 * @dev:		struct device for this csi2dc device
 * @pclk:		Peripheral clock reference
 *			Input clock that clocks the hardware block internal
 *			logic
 * @scck:		Sensor Controller clock reference
 *			Input clock that is used to generate the pixel clock
 * @format:		Current saved format used in g/s fmt
 * @cur_fmt:		Current state format
 * @try_fmt:		Try format that is being tried
 * @pads:		Media entity pads for the csi2dc subdevice
 * @clk_gated:		Whether the clock is gated or free running
 * @video_pipe:		Whether video pipeline is configured
 * @parallel_mode:	The underlying subdevice is connected on a parallel bus
 * @vc:			Current set virtual channel
 * @notifier:		Async notifier that is used to bound the underlying
 *			subdevice to the csi2dc subdevice
 * @input_sd:		Reference to the underlying subdevice bound to the
 *			csi2dc subdevice
 * @remote_pad:		Pad number of the underlying subdevice that is linked
 *			to the csi2dc subdevice sink pad.
 */
struct csi2dc_device {
	void __iomem			*base;
	struct v4l2_subdev		csi2dc_sd;
	struct device			*dev;
	struct clk			*pclk;
	struct clk			*scck;

	struct v4l2_mbus_framefmt	 format;

	const struct csi2dc_format	*cur_fmt;
	const struct csi2dc_format	*try_fmt;

	struct media_pad		pads[CSI2DC_PADS_NUM];

	bool				clk_gated;
	bool				video_pipe;
	bool				parallel_mode;
	u32				vc;

	struct v4l2_async_notifier	notifier;

	struct v4l2_subdev		*input_sd;

	u32				remote_pad;
};

static inline struct csi2dc_device *
csi2dc_sd_to_csi2dc_device(struct v4l2_subdev *csi2dc_sd)
{
	return container_of(csi2dc_sd, struct csi2dc_device, csi2dc_sd);
}

static int csi2dc_enum_mbus_code(struct v4l2_subdev *csi2dc_sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(csi2dc_formats))
		return -EINVAL;

	code->code = csi2dc_formats[code->index].mbus_code;

	return 0;
}

static int csi2dc_get_fmt(struct v4l2_subdev *csi2dc_sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct csi2dc_device *csi2dc = csi2dc_sd_to_csi2dc_device(csi2dc_sd);
	struct v4l2_mbus_framefmt *v4l2_try_fmt;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		v4l2_try_fmt = v4l2_subdev_state_get_format(sd_state,
							    format->pad);
		format->format = *v4l2_try_fmt;

		return 0;
	}

	format->format = csi2dc->format;

	return 0;
}

static int csi2dc_set_fmt(struct v4l2_subdev *csi2dc_sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *req_fmt)
{
	struct csi2dc_device *csi2dc = csi2dc_sd_to_csi2dc_device(csi2dc_sd);
	const struct csi2dc_format *fmt, *try_fmt = NULL;
	struct v4l2_mbus_framefmt *v4l2_try_fmt;
	unsigned int i;

	/*
	 * Setting the source pad is disabled.
	 * The same format is being propagated from the sink to source.
	 */
	if (req_fmt->pad == CSI2DC_PAD_SOURCE)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(csi2dc_formats);  i++) {
		fmt = &csi2dc_formats[i];
		if (req_fmt->format.code == fmt->mbus_code)
			try_fmt = fmt;
		fmt++;
	}

	/* in case we could not find the desired format, default to something */
	if (!try_fmt) {
		try_fmt = &csi2dc_formats[0];

		dev_dbg(csi2dc->dev,
			"CSI2DC unsupported format 0x%x, defaulting to 0x%x\n",
			req_fmt->format.code, csi2dc_formats[0].mbus_code);
	}

	req_fmt->format.code = try_fmt->mbus_code;
	req_fmt->format.colorspace = V4L2_COLORSPACE_SRGB;
	req_fmt->format.field = V4L2_FIELD_NONE;

	if (req_fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		v4l2_try_fmt = v4l2_subdev_state_get_format(sd_state,
							    req_fmt->pad);
		*v4l2_try_fmt = req_fmt->format;
		/* Trying on the sink pad makes the source pad change too */
		v4l2_try_fmt = v4l2_subdev_state_get_format(sd_state,
							    CSI2DC_PAD_SOURCE);
		*v4l2_try_fmt = req_fmt->format;

		/* if we are just trying, we are done */
		return 0;
	}

	/* save the format for later requests */
	csi2dc->format = req_fmt->format;

	/* update config */
	csi2dc->cur_fmt = try_fmt;

	dev_dbg(csi2dc->dev, "new format set: 0x%x @%dx%d\n",
		csi2dc->format.code, csi2dc->format.width,
		csi2dc->format.height);

	return 0;
}

static int csi2dc_power(struct csi2dc_device *csi2dc, int on)
{
	int ret = 0;

	if (on) {
		ret = clk_prepare_enable(csi2dc->pclk);
		if (ret) {
			dev_err(csi2dc->dev, "failed to enable pclk:%d\n", ret);
			return ret;
		}

		ret = clk_prepare_enable(csi2dc->scck);
		if (ret) {
			dev_err(csi2dc->dev, "failed to enable scck:%d\n", ret);
			clk_disable_unprepare(csi2dc->pclk);
			return ret;
		}

		/* if powering up, deassert reset line */
		csi2dc_writel(csi2dc, CSI2DC_GCTLR, CSI2DC_GCTLR_SWRST);
	} else {
		/* if powering down, assert reset line */
		csi2dc_writel(csi2dc, CSI2DC_GCTLR, 0);

		clk_disable_unprepare(csi2dc->scck);
		clk_disable_unprepare(csi2dc->pclk);
	}

	return ret;
}

static int csi2dc_get_mbus_config(struct csi2dc_device *csi2dc)
{
	struct v4l2_mbus_config mbus_config = { 0 };
	int ret;

	ret = v4l2_subdev_call(csi2dc->input_sd, pad, get_mbus_config,
			       csi2dc->remote_pad, &mbus_config);
	if (ret == -ENOIOCTLCMD) {
		dev_dbg(csi2dc->dev,
			"no remote mbus configuration available\n");
		return 0;
	}

	if (ret) {
		dev_err(csi2dc->dev,
			"failed to get remote mbus configuration\n");
		return 0;
	}

	dev_dbg(csi2dc->dev, "subdev sending on channel %d\n", csi2dc->vc);

	csi2dc->clk_gated = mbus_config.bus.parallel.flags &
				V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;

	dev_dbg(csi2dc->dev, "mbus_config: %s clock\n",
		csi2dc->clk_gated ? "gated" : "free running");

	return 0;
}

static void csi2dc_vp_update(struct csi2dc_device *csi2dc)
{
	u32 vp, gcfg;

	if (!csi2dc->video_pipe) {
		dev_err(csi2dc->dev, "video pipeline unavailable\n");
		return;
	}

	if (csi2dc->parallel_mode) {
		/* In parallel mode, GPIO parallel interface must be selected */
		gcfg = csi2dc_readl(csi2dc, CSI2DC_GCFG);
		gcfg |= CSI2DC_GCFG_GPIOSEL;
		csi2dc_writel(csi2dc, CSI2DC_GCFG, gcfg);
		return;
	}

	/* serial video pipeline */

	csi2dc_writel(csi2dc, CSI2DC_GCFG,
		      (SAMA7G5_HLC & CSI2DC_GCFG_HLC_MASK) |
		      (csi2dc->clk_gated ? 0 : CSI2DC_GCFG_MIPIFRN));

	vp = CSI2DC_VPCFG_DT(csi2dc->cur_fmt->dt) & CSI2DC_VPCFG_DT_MASK;
	vp |= CSI2DC_VPCFG_VC(csi2dc->vc) & CSI2DC_VPCFG_VC_MASK;
	vp &= ~CSI2DC_VPCFG_DE;
	vp |= CSI2DC_VPCFG_DM(CSI2DC_VPCFG_DM_DECODER8TO12);
	vp &= ~CSI2DC_VPCFG_DP2;
	vp &= ~CSI2DC_VPCFG_RMS;
	vp |= CSI2DC_VPCFG_PA;

	csi2dc_writel(csi2dc, CSI2DC_VPCFG, vp);
	csi2dc_writel(csi2dc, CSI2DC_VPE, CSI2DC_VPE_ENABLE);
	csi2dc_writel(csi2dc, CSI2DC_PU, CSI2DC_PU_VP);
}

static int csi2dc_s_stream(struct v4l2_subdev *csi2dc_sd, int enable)
{
	struct csi2dc_device *csi2dc = csi2dc_sd_to_csi2dc_device(csi2dc_sd);
	int ret;

	if (enable) {
		ret = pm_runtime_resume_and_get(csi2dc->dev);
		if (ret < 0)
			return ret;

		csi2dc_get_mbus_config(csi2dc);

		csi2dc_vp_update(csi2dc);

		return v4l2_subdev_call(csi2dc->input_sd, video, s_stream,
					true);
	}

	dev_dbg(csi2dc->dev,
		"Last frame received: VPCOLR = %u, VPROWR= %u, VPISR = %x\n",
		csi2dc_readl(csi2dc, CSI2DC_VPCOL),
		csi2dc_readl(csi2dc, CSI2DC_VPROW),
		csi2dc_readl(csi2dc, CSI2DC_VPISR));

	/* stop streaming scenario */
	ret = v4l2_subdev_call(csi2dc->input_sd, video, s_stream, false);

	pm_runtime_put_sync(csi2dc->dev);

	return ret;
}

static int csi2dc_init_state(struct v4l2_subdev *csi2dc_sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct v4l2_mbus_framefmt *v4l2_try_fmt =
		v4l2_subdev_state_get_format(sd_state, 0);

	v4l2_try_fmt->height = 480;
	v4l2_try_fmt->width = 640;
	v4l2_try_fmt->code = csi2dc_formats[0].mbus_code;
	v4l2_try_fmt->colorspace = V4L2_COLORSPACE_SRGB;
	v4l2_try_fmt->field = V4L2_FIELD_NONE;
	v4l2_try_fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	v4l2_try_fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
	v4l2_try_fmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	return 0;
}

static const struct media_entity_operations csi2dc_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_pad_ops csi2dc_pad_ops = {
	.enum_mbus_code = csi2dc_enum_mbus_code,
	.set_fmt = csi2dc_set_fmt,
	.get_fmt = csi2dc_get_fmt,
};

static const struct v4l2_subdev_video_ops csi2dc_video_ops = {
	.s_stream = csi2dc_s_stream,
};

static const struct v4l2_subdev_ops csi2dc_subdev_ops = {
	.pad = &csi2dc_pad_ops,
	.video = &csi2dc_video_ops,
};

static const struct v4l2_subdev_internal_ops csi2dc_internal_ops = {
	.init_state = csi2dc_init_state,
};

static int csi2dc_async_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *subdev,
			      struct v4l2_async_connection *asd)
{
	struct csi2dc_device *csi2dc = container_of(notifier,
						struct csi2dc_device, notifier);
	int pad;
	int ret;

	csi2dc->input_sd = subdev;

	pad = media_entity_get_fwnode_pad(&subdev->entity, asd->match.fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (pad < 0) {
		dev_err(csi2dc->dev, "Failed to find pad for %s\n",
			subdev->name);
		return pad;
	}

	csi2dc->remote_pad = pad;

	ret = media_create_pad_link(&csi2dc->input_sd->entity,
				    csi2dc->remote_pad,
				    &csi2dc->csi2dc_sd.entity, 0,
				    MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(csi2dc->dev,
			"Failed to create pad link: %s to %s\n",
			csi2dc->input_sd->entity.name,
			csi2dc->csi2dc_sd.entity.name);
		return ret;
	}

	dev_dbg(csi2dc->dev, "link with %s pad: %d\n",
		csi2dc->input_sd->name, csi2dc->remote_pad);

	return ret;
}

static const struct v4l2_async_notifier_operations csi2dc_async_ops = {
	.bound = csi2dc_async_bound,
};

static int csi2dc_prepare_notifier(struct csi2dc_device *csi2dc,
				   struct fwnode_handle *input_fwnode)
{
	struct v4l2_async_connection *asd;
	int ret = 0;

	v4l2_async_subdev_nf_init(&csi2dc->notifier, &csi2dc->csi2dc_sd);

	asd = v4l2_async_nf_add_fwnode_remote(&csi2dc->notifier,
					      input_fwnode,
					      struct v4l2_async_connection);

	fwnode_handle_put(input_fwnode);

	if (IS_ERR(asd)) {
		ret = PTR_ERR(asd);
		dev_err(csi2dc->dev,
			"failed to add async notifier for node %pOF: %d\n",
			to_of_node(input_fwnode), ret);
		v4l2_async_nf_cleanup(&csi2dc->notifier);
		return ret;
	}

	csi2dc->notifier.ops = &csi2dc_async_ops;

	ret = v4l2_async_nf_register(&csi2dc->notifier);
	if (ret) {
		dev_err(csi2dc->dev, "fail to register async notifier: %d\n",
			ret);
		v4l2_async_nf_cleanup(&csi2dc->notifier);
	}

	return ret;
}

static int csi2dc_of_parse(struct csi2dc_device *csi2dc,
			   struct device_node *of_node)
{
	struct fwnode_handle *input_fwnode, *output_fwnode;
	struct v4l2_fwnode_endpoint input_endpoint = { 0 },
				    output_endpoint = { 0 };
	int ret;

	input_fwnode = fwnode_graph_get_next_endpoint(of_fwnode_handle(of_node),
						      NULL);
	if (!input_fwnode) {
		dev_err(csi2dc->dev,
			"missing port node at %pOF, input node is mandatory.\n",
			of_node);
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(input_fwnode, &input_endpoint);
	if (ret) {
		dev_err(csi2dc->dev, "endpoint not defined at %pOF\n", of_node);
		goto csi2dc_of_parse_err;
	}

	if (input_endpoint.bus_type == V4L2_MBUS_PARALLEL ||
	    input_endpoint.bus_type == V4L2_MBUS_BT656) {
		csi2dc->parallel_mode = true;
		dev_dbg(csi2dc->dev,
			"subdevice connected on parallel interface\n");
	}

	if (input_endpoint.bus_type == V4L2_MBUS_CSI2_DPHY) {
		csi2dc->clk_gated = input_endpoint.bus.mipi_csi2.flags &
					V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;
		dev_dbg(csi2dc->dev,
			"subdevice connected on serial interface\n");
		dev_dbg(csi2dc->dev, "DT: %s clock\n",
			csi2dc->clk_gated ? "gated" : "free running");
	}

	output_fwnode = fwnode_graph_get_next_endpoint
				(of_fwnode_handle(of_node), input_fwnode);

	if (output_fwnode)
		ret = v4l2_fwnode_endpoint_parse(output_fwnode,
						 &output_endpoint);

	fwnode_handle_put(output_fwnode);

	if (!output_fwnode || ret) {
		dev_info(csi2dc->dev,
			 "missing output node at %pOF, data pipe available only.\n",
			 of_node);
	} else {
		if (output_endpoint.bus_type != V4L2_MBUS_PARALLEL &&
		    output_endpoint.bus_type != V4L2_MBUS_BT656) {
			dev_err(csi2dc->dev,
				"output port must be parallel/bt656.\n");
			ret = -EINVAL;
			goto csi2dc_of_parse_err;
		}

		csi2dc->video_pipe = true;

		dev_dbg(csi2dc->dev,
			"block %pOF [%d.%d]->[%d.%d] video pipeline\n",
			of_node, input_endpoint.base.port,
			input_endpoint.base.id, output_endpoint.base.port,
			output_endpoint.base.id);
	}

	/* prepare async notifier for subdevice completion */
	return csi2dc_prepare_notifier(csi2dc, input_fwnode);

csi2dc_of_parse_err:
	fwnode_handle_put(input_fwnode);
	return ret;
}

static void csi2dc_default_format(struct csi2dc_device *csi2dc)
{
	csi2dc->cur_fmt = &csi2dc_formats[0];

	csi2dc->format.height = 480;
	csi2dc->format.width = 640;
	csi2dc->format.code = csi2dc_formats[0].mbus_code;
	csi2dc->format.colorspace = V4L2_COLORSPACE_SRGB;
	csi2dc->format.field = V4L2_FIELD_NONE;
	csi2dc->format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	csi2dc->format.quantization = V4L2_QUANTIZATION_DEFAULT;
	csi2dc->format.xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static int csi2dc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct csi2dc_device *csi2dc;
	int ret = 0;
	u32 ver;

	csi2dc = devm_kzalloc(dev, sizeof(*csi2dc), GFP_KERNEL);
	if (!csi2dc)
		return -ENOMEM;

	csi2dc->dev = dev;

	csi2dc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csi2dc->base)) {
		dev_err(dev, "base address not set\n");
		return PTR_ERR(csi2dc->base);
	}

	csi2dc->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(csi2dc->pclk)) {
		ret = PTR_ERR(csi2dc->pclk);
		dev_err(dev, "failed to get pclk: %d\n", ret);
		return ret;
	}

	csi2dc->scck = devm_clk_get(dev, "scck");
	if (IS_ERR(csi2dc->scck)) {
		ret = PTR_ERR(csi2dc->scck);
		dev_err(dev, "failed to get scck: %d\n", ret);
		return ret;
	}

	v4l2_subdev_init(&csi2dc->csi2dc_sd, &csi2dc_subdev_ops);
	csi2dc->csi2dc_sd.internal_ops = &csi2dc_internal_ops;

	csi2dc->csi2dc_sd.owner = THIS_MODULE;
	csi2dc->csi2dc_sd.dev = dev;
	snprintf(csi2dc->csi2dc_sd.name, sizeof(csi2dc->csi2dc_sd.name),
		 "csi2dc");

	csi2dc->csi2dc_sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	csi2dc->csi2dc_sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	csi2dc->csi2dc_sd.entity.ops = &csi2dc_entity_ops;

	platform_set_drvdata(pdev, csi2dc);

	ret = csi2dc_of_parse(csi2dc, dev->of_node);
	if (ret)
		goto csi2dc_probe_cleanup_entity;

	csi2dc->pads[CSI2DC_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	if (csi2dc->video_pipe)
		csi2dc->pads[CSI2DC_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&csi2dc->csi2dc_sd.entity,
				     csi2dc->video_pipe ? CSI2DC_PADS_NUM : 1,
				     csi2dc->pads);
	if (ret < 0) {
		dev_err(dev, "media entity init failed\n");
		goto csi2dc_probe_cleanup_notifier;
	}

	csi2dc_default_format(csi2dc);

	/* turn power on to validate capabilities */
	ret = csi2dc_power(csi2dc, true);
	if (ret < 0)
		goto csi2dc_probe_cleanup_notifier;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	ver = csi2dc_readl(csi2dc, CSI2DC_VERSION);

	/*
	 * we must register the subdev after PM runtime has been requested,
	 * otherwise we might bound immediately and request pm_runtime_resume
	 * before runtime_enable.
	 */
	ret = v4l2_async_register_subdev(&csi2dc->csi2dc_sd);
	if (ret) {
		dev_err(csi2dc->dev, "failed to register the subdevice\n");
		goto csi2dc_probe_cleanup_notifier;
	}

	dev_info(dev, "Microchip CSI2DC version %x\n", ver);

	return 0;

csi2dc_probe_cleanup_notifier:
	v4l2_async_nf_cleanup(&csi2dc->notifier);
csi2dc_probe_cleanup_entity:
	media_entity_cleanup(&csi2dc->csi2dc_sd.entity);

	return ret;
}

static void csi2dc_remove(struct platform_device *pdev)
{
	struct csi2dc_device *csi2dc = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	v4l2_async_unregister_subdev(&csi2dc->csi2dc_sd);
	v4l2_async_nf_unregister(&csi2dc->notifier);
	v4l2_async_nf_cleanup(&csi2dc->notifier);
	media_entity_cleanup(&csi2dc->csi2dc_sd.entity);
}

static int __maybe_unused csi2dc_runtime_suspend(struct device *dev)
{
	struct csi2dc_device *csi2dc = dev_get_drvdata(dev);

	return csi2dc_power(csi2dc, false);
}

static int __maybe_unused csi2dc_runtime_resume(struct device *dev)
{
	struct csi2dc_device *csi2dc = dev_get_drvdata(dev);

	return csi2dc_power(csi2dc, true);
}

static const struct dev_pm_ops csi2dc_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(csi2dc_runtime_suspend, csi2dc_runtime_resume, NULL)
};

static const struct of_device_id csi2dc_of_match[] = {
	{ .compatible = "microchip,sama7g5-csi2dc" },
	{ }
};

MODULE_DEVICE_TABLE(of, csi2dc_of_match);

static struct platform_driver csi2dc_driver = {
	.probe	= csi2dc_probe,
	.remove_new = csi2dc_remove,
	.driver = {
		.name =			"microchip-csi2dc",
		.pm =			&csi2dc_dev_pm_ops,
		.of_match_table =	of_match_ptr(csi2dc_of_match),
	},
};

module_platform_driver(csi2dc_driver);

MODULE_AUTHOR("Eugen Hristev <eugen.hristev@microchip.com>");
MODULE_DESCRIPTION("Microchip CSI2 Demux Controller driver");
MODULE_LICENSE("GPL v2");
