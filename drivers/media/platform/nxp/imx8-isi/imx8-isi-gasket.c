// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019-2023 NXP
 */

#include <linux/regmap.h>

#include <media/mipi-csi2.h>

#include "imx8-isi-core.h"

/* -----------------------------------------------------------------------------
 * i.MX8MN and i.MX8MP gasket
 */

#define GASKET_BASE(n)				(0x0060 + (n) * 0x30)

#define GASKET_CTRL				0x0000
#define GASKET_CTRL_DATA_TYPE(dt)		((dt) << 8)
#define GASKET_CTRL_DATA_TYPE_MASK		(0x3f << 8)
#define GASKET_CTRL_DUAL_COMP_ENABLE		BIT(1)
#define GASKET_CTRL_ENABLE			BIT(0)

#define GASKET_HSIZE				0x0004
#define GASKET_VSIZE				0x0008

static void mxc_imx8_gasket_enable(struct mxc_isi_dev *isi,
				   const struct v4l2_mbus_frame_desc *fd,
				   const struct v4l2_mbus_framefmt *fmt,
				   const unsigned int port)
{
	u32 val;

	regmap_write(isi->gasket, GASKET_BASE(port) + GASKET_HSIZE, fmt->width);
	regmap_write(isi->gasket, GASKET_BASE(port) + GASKET_VSIZE, fmt->height);

	val = GASKET_CTRL_DATA_TYPE(fd->entry[0].bus.csi2.dt);
	if (fd->entry[0].bus.csi2.dt == MIPI_CSI2_DT_YUV422_8B)
		val |= GASKET_CTRL_DUAL_COMP_ENABLE;

	val |= GASKET_CTRL_ENABLE;
	regmap_write(isi->gasket, GASKET_BASE(port) + GASKET_CTRL, val);
}

static void mxc_imx8_gasket_disable(struct mxc_isi_dev *isi,
				    const unsigned int port)
{
	regmap_write(isi->gasket, GASKET_BASE(port) + GASKET_CTRL, 0);
}

const struct mxc_gasket_ops mxc_imx8_gasket_ops = {
	.enable = mxc_imx8_gasket_enable,
	.disable = mxc_imx8_gasket_disable,
};

/* -----------------------------------------------------------------------------
 * i.MX93 gasket
 */

#define DISP_MIX_CAMERA_MUX                     0x30
#define DISP_MIX_CAMERA_MUX_DATA_TYPE(x)        (((x) & 0x3f) << 3)
#define DISP_MIX_CAMERA_MUX_GASKET_ENABLE       BIT(16)

static void mxc_imx93_gasket_enable(struct mxc_isi_dev *isi,
				    const struct v4l2_mbus_frame_desc *fd,
				    const struct v4l2_mbus_framefmt *fmt,
				    const unsigned int port)
{
	u32 val;

	val = DISP_MIX_CAMERA_MUX_DATA_TYPE(fd->entry[0].bus.csi2.dt);
	val |= DISP_MIX_CAMERA_MUX_GASKET_ENABLE;
	regmap_write(isi->gasket, DISP_MIX_CAMERA_MUX, val);
}

static void mxc_imx93_gasket_disable(struct mxc_isi_dev *isi,
				     unsigned int port)
{
	regmap_write(isi->gasket, DISP_MIX_CAMERA_MUX, 0);
}

const struct mxc_gasket_ops mxc_imx93_gasket_ops = {
	.enable = mxc_imx93_gasket_enable,
	.disable = mxc_imx93_gasket_disable,
};
