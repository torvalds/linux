/*
 * Copyright (C) 2010 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/slab.h>

#include <mach/hardware.h>
#include <mach/mux.h>
#include <linux/platform_data/i2c-davinci.h>

#include <linux/io.h>

#include <media/davinci/vpbe_types.h>
#include <media/davinci/vpbe_venc.h>
#include <media/davinci/vpss.h>
#include <media/v4l2-device.h>

#include "vpbe_venc_regs.h"

#define MODULE_NAME	"davinci-vpbe-venc"

static struct platform_device_id vpbe_venc_devtype[] = {
	{
		.name = DM644X_VPBE_VENC_SUBDEV_NAME,
		.driver_data = VPBE_VERSION_1,
	}, {
		.name = DM365_VPBE_VENC_SUBDEV_NAME,
		.driver_data = VPBE_VERSION_2,
	}, {
		.name = DM355_VPBE_VENC_SUBDEV_NAME,
		.driver_data = VPBE_VERSION_3,
	},
};

MODULE_DEVICE_TABLE(platform, vpbe_venc_devtype);

static int debug = 2;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level 0-2");

struct venc_state {
	struct v4l2_subdev sd;
	struct venc_callback *callback;
	struct venc_platform_data *pdata;
	struct device *pdev;
	u32 output;
	v4l2_std_id std;
	spinlock_t lock;
	void __iomem *venc_base;
	void __iomem *vdaccfg_reg;
	enum vpbe_version venc_type;
};

static inline struct venc_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct venc_state, sd);
}

static inline u32 venc_read(struct v4l2_subdev *sd, u32 offset)
{
	struct venc_state *venc = to_state(sd);

	return readl(venc->venc_base + offset);
}

static inline u32 venc_write(struct v4l2_subdev *sd, u32 offset, u32 val)
{
	struct venc_state *venc = to_state(sd);

	writel(val, (venc->venc_base + offset));

	return val;
}

static inline u32 venc_modify(struct v4l2_subdev *sd, u32 offset,
				 u32 val, u32 mask)
{
	u32 new_val = (venc_read(sd, offset) & ~mask) | (val & mask);

	venc_write(sd, offset, new_val);

	return new_val;
}

static inline u32 vdaccfg_write(struct v4l2_subdev *sd, u32 val)
{
	struct venc_state *venc = to_state(sd);

	writel(val, venc->vdaccfg_reg);

	val = readl(venc->vdaccfg_reg);

	return val;
}

#define VDAC_COMPONENT	0x543
#define VDAC_S_VIDEO	0x210
/* This function sets the dac of the VPBE for various outputs
 */
static int venc_set_dac(struct v4l2_subdev *sd, u32 out_index)
{
	switch (out_index) {
	case 0:
		v4l2_dbg(debug, 1, sd, "Setting output to Composite\n");
		venc_write(sd, VENC_DACSEL, 0);
		break;
	case 1:
		v4l2_dbg(debug, 1, sd, "Setting output to Component\n");
		venc_write(sd, VENC_DACSEL, VDAC_COMPONENT);
		break;
	case 2:
		v4l2_dbg(debug, 1, sd, "Setting output to S-video\n");
		venc_write(sd, VENC_DACSEL, VDAC_S_VIDEO);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void venc_enabledigitaloutput(struct v4l2_subdev *sd, int benable)
{
	struct venc_state *venc = to_state(sd);

	v4l2_dbg(debug, 2, sd, "venc_enabledigitaloutput\n");

	if (benable) {
		venc_write(sd, VENC_VMOD, 0);
		venc_write(sd, VENC_CVBS, 0);
		venc_write(sd, VENC_LCDOUT, 0);
		venc_write(sd, VENC_HSPLS, 0);
		venc_write(sd, VENC_HSTART, 0);
		venc_write(sd, VENC_HVALID, 0);
		venc_write(sd, VENC_HINT, 0);
		venc_write(sd, VENC_VSPLS, 0);
		venc_write(sd, VENC_VSTART, 0);
		venc_write(sd, VENC_VVALID, 0);
		venc_write(sd, VENC_VINT, 0);
		venc_write(sd, VENC_YCCCTL, 0);
		venc_write(sd, VENC_DACSEL, 0);

	} else {
		venc_write(sd, VENC_VMOD, 0);
		/* disable VCLK output pin enable */
		venc_write(sd, VENC_VIDCTL, 0x141);

		/* Disable output sync pins */
		venc_write(sd, VENC_SYNCCTL, 0);

		/* Disable DCLOCK */
		venc_write(sd, VENC_DCLKCTL, 0);
		venc_write(sd, VENC_DRGBX1, 0x0000057C);

		/* Disable LCD output control (accepting default polarity) */
		venc_write(sd, VENC_LCDOUT, 0);
		if (venc->venc_type != VPBE_VERSION_3)
			venc_write(sd, VENC_CMPNT, 0x100);
		venc_write(sd, VENC_HSPLS, 0);
		venc_write(sd, VENC_HINT, 0);
		venc_write(sd, VENC_HSTART, 0);
		venc_write(sd, VENC_HVALID, 0);

		venc_write(sd, VENC_VSPLS, 0);
		venc_write(sd, VENC_VINT, 0);
		venc_write(sd, VENC_VSTART, 0);
		venc_write(sd, VENC_VVALID, 0);

		venc_write(sd, VENC_HSDLY, 0);
		venc_write(sd, VENC_VSDLY, 0);

		venc_write(sd, VENC_YCCCTL, 0);
		venc_write(sd, VENC_VSTARTA, 0);

		/* Set OSD clock and OSD Sync Adavance registers */
		venc_write(sd, VENC_OSDCLK0, 1);
		venc_write(sd, VENC_OSDCLK1, 2);
	}
}

#define VDAC_CONFIG_SD_V3	0x0E21A6B6
#define VDAC_CONFIG_SD_V2	0x081141CF
/*
 * setting NTSC mode
 */
static int venc_set_ntsc(struct v4l2_subdev *sd)
{
	u32 val;
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	v4l2_dbg(debug, 2, sd, "venc_set_ntsc\n");

	/* Setup clock at VPSS & VENC for SD */
	vpss_enable_clock(VPSS_VENC_CLOCK_SEL, 1);
	if (pdata->setup_clock(VPBE_ENC_STD, V4L2_STD_525_60) < 0)
		return -EINVAL;

	venc_enabledigitaloutput(sd, 0);

	if (venc->venc_type == VPBE_VERSION_3) {
		venc_write(sd, VENC_CLKCTL, 0x01);
		venc_write(sd, VENC_VIDCTL, 0);
		val = vdaccfg_write(sd, VDAC_CONFIG_SD_V3);
	} else if (venc->venc_type == VPBE_VERSION_2) {
		venc_write(sd, VENC_CLKCTL, 0x01);
		venc_write(sd, VENC_VIDCTL, 0);
		vdaccfg_write(sd, VDAC_CONFIG_SD_V2);
	} else {
		/* to set VENC CLK DIV to 1 - final clock is 54 MHz */
		venc_modify(sd, VENC_VIDCTL, 0, 1 << 1);
		/* Set REC656 Mode */
		venc_write(sd, VENC_YCCCTL, 0x1);
		venc_modify(sd, VENC_VDPRO, 0, VENC_VDPRO_DAFRQ);
		venc_modify(sd, VENC_VDPRO, 0, VENC_VDPRO_DAUPS);
	}

	venc_write(sd, VENC_VMOD, 0);
	venc_modify(sd, VENC_VMOD, (1 << VENC_VMOD_VIE_SHIFT),
			VENC_VMOD_VIE);
	venc_modify(sd, VENC_VMOD, (0 << VENC_VMOD_VMD), VENC_VMOD_VMD);
	venc_modify(sd, VENC_VMOD, (0 << VENC_VMOD_TVTYP_SHIFT),
			VENC_VMOD_TVTYP);
	venc_write(sd, VENC_DACTST, 0x0);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_VENC, VENC_VMOD_VENC);

	return 0;
}

/*
 * setting PAL mode
 */
static int venc_set_pal(struct v4l2_subdev *sd)
{
	struct venc_state *venc = to_state(sd);

	v4l2_dbg(debug, 2, sd, "venc_set_pal\n");

	/* Setup clock at VPSS & VENC for SD */
	vpss_enable_clock(VPSS_VENC_CLOCK_SEL, 1);
	if (venc->pdata->setup_clock(VPBE_ENC_STD, V4L2_STD_625_50) < 0)
		return -EINVAL;

	venc_enabledigitaloutput(sd, 0);

	if (venc->venc_type == VPBE_VERSION_3) {
		venc_write(sd, VENC_CLKCTL, 0x1);
		venc_write(sd, VENC_VIDCTL, 0);
		vdaccfg_write(sd, VDAC_CONFIG_SD_V3);
	} else if (venc->venc_type == VPBE_VERSION_2) {
		venc_write(sd, VENC_CLKCTL, 0x1);
		venc_write(sd, VENC_VIDCTL, 0);
		vdaccfg_write(sd, VDAC_CONFIG_SD_V2);
	} else {
		/* to set VENC CLK DIV to 1 - final clock is 54 MHz */
		venc_modify(sd, VENC_VIDCTL, 0, 1 << 1);
		/* Set REC656 Mode */
		venc_write(sd, VENC_YCCCTL, 0x1);
	}

	venc_modify(sd, VENC_SYNCCTL, 1 << VENC_SYNCCTL_OVD_SHIFT,
			VENC_SYNCCTL_OVD);
	venc_write(sd, VENC_VMOD, 0);
	venc_modify(sd, VENC_VMOD,
			(1 << VENC_VMOD_VIE_SHIFT),
			VENC_VMOD_VIE);
	venc_modify(sd, VENC_VMOD,
			(0 << VENC_VMOD_VMD), VENC_VMOD_VMD);
	venc_modify(sd, VENC_VMOD,
			(1 << VENC_VMOD_TVTYP_SHIFT),
			VENC_VMOD_TVTYP);
	venc_write(sd, VENC_DACTST, 0x0);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_VENC, VENC_VMOD_VENC);

	return 0;
}

#define VDAC_CONFIG_HD_V2	0x081141EF
/*
 * venc_set_480p59_94
 *
 * This function configures the video encoder to EDTV(525p) component setting.
 */
static int venc_set_480p59_94(struct v4l2_subdev *sd)
{
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	v4l2_dbg(debug, 2, sd, "venc_set_480p59_94\n");
	if (venc->venc_type != VPBE_VERSION_1 &&
	    venc->venc_type != VPBE_VERSION_2)
		return -EINVAL;

	/* Setup clock at VPSS & VENC for SD */
	if (pdata->setup_clock(VPBE_ENC_CUSTOM_TIMINGS, 27000000) < 0)
		return -EINVAL;

	venc_enabledigitaloutput(sd, 0);

	if (venc->venc_type == VPBE_VERSION_2)
		vdaccfg_write(sd, VDAC_CONFIG_HD_V2);
	venc_write(sd, VENC_OSDCLK0, 0);
	venc_write(sd, VENC_OSDCLK1, 1);

	if (venc->venc_type == VPBE_VERSION_1) {
		venc_modify(sd, VENC_VDPRO, VENC_VDPRO_DAFRQ,
			    VENC_VDPRO_DAFRQ);
		venc_modify(sd, VENC_VDPRO, VENC_VDPRO_DAUPS,
			    VENC_VDPRO_DAUPS);
	}

	venc_write(sd, VENC_VMOD, 0);
	venc_modify(sd, VENC_VMOD, (1 << VENC_VMOD_VIE_SHIFT),
		    VENC_VMOD_VIE);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_HDMD, VENC_VMOD_HDMD);
	venc_modify(sd, VENC_VMOD, (HDTV_525P << VENC_VMOD_TVTYP_SHIFT),
		    VENC_VMOD_TVTYP);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_VDMD_YCBCR8 <<
		    VENC_VMOD_VDMD_SHIFT, VENC_VMOD_VDMD);

	venc_modify(sd, VENC_VMOD, VENC_VMOD_VENC, VENC_VMOD_VENC);

	return 0;
}

/*
 * venc_set_625p
 *
 * This function configures the video encoder to HDTV(625p) component setting
 */
static int venc_set_576p50(struct v4l2_subdev *sd)
{
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	v4l2_dbg(debug, 2, sd, "venc_set_576p50\n");

	if (venc->venc_type != VPBE_VERSION_1 &&
	    venc->venc_type != VPBE_VERSION_2)
		return -EINVAL;
	/* Setup clock at VPSS & VENC for SD */
	if (pdata->setup_clock(VPBE_ENC_CUSTOM_TIMINGS, 27000000) < 0)
		return -EINVAL;

	venc_enabledigitaloutput(sd, 0);

	if (venc->venc_type == VPBE_VERSION_2)
		vdaccfg_write(sd, VDAC_CONFIG_HD_V2);

	venc_write(sd, VENC_OSDCLK0, 0);
	venc_write(sd, VENC_OSDCLK1, 1);

	if (venc->venc_type == VPBE_VERSION_1) {
		venc_modify(sd, VENC_VDPRO, VENC_VDPRO_DAFRQ,
			    VENC_VDPRO_DAFRQ);
		venc_modify(sd, VENC_VDPRO, VENC_VDPRO_DAUPS,
			    VENC_VDPRO_DAUPS);
	}

	venc_write(sd, VENC_VMOD, 0);
	venc_modify(sd, VENC_VMOD, (1 << VENC_VMOD_VIE_SHIFT),
		    VENC_VMOD_VIE);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_HDMD, VENC_VMOD_HDMD);
	venc_modify(sd, VENC_VMOD, (HDTV_625P << VENC_VMOD_TVTYP_SHIFT),
		    VENC_VMOD_TVTYP);

	venc_modify(sd, VENC_VMOD, VENC_VMOD_VDMD_YCBCR8 <<
		    VENC_VMOD_VDMD_SHIFT, VENC_VMOD_VDMD);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_VENC, VENC_VMOD_VENC);

	return 0;
}

/*
 * venc_set_720p60_internal - Setup 720p60 in venc for dm365 only
 */
static int venc_set_720p60_internal(struct v4l2_subdev *sd)
{
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	if (pdata->setup_clock(VPBE_ENC_CUSTOM_TIMINGS, 74250000) < 0)
		return -EINVAL;

	venc_enabledigitaloutput(sd, 0);

	venc_write(sd, VENC_OSDCLK0, 0);
	venc_write(sd, VENC_OSDCLK1, 1);

	venc_write(sd, VENC_VMOD, 0);
	/* DM365 component HD mode */
	venc_modify(sd, VENC_VMOD, (1 << VENC_VMOD_VIE_SHIFT),
	    VENC_VMOD_VIE);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_HDMD, VENC_VMOD_HDMD);
	venc_modify(sd, VENC_VMOD, (HDTV_720P << VENC_VMOD_TVTYP_SHIFT),
		    VENC_VMOD_TVTYP);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_VENC, VENC_VMOD_VENC);
	venc_write(sd, VENC_XHINTVL, 0);
	return 0;
}

/*
 * venc_set_1080i30_internal - Setup 1080i30 in venc for dm365 only
 */
static int venc_set_1080i30_internal(struct v4l2_subdev *sd)
{
	struct venc_state *venc = to_state(sd);
	struct venc_platform_data *pdata = venc->pdata;

	if (pdata->setup_clock(VPBE_ENC_CUSTOM_TIMINGS, 74250000) < 0)
		return -EINVAL;

	venc_enabledigitaloutput(sd, 0);

	venc_write(sd, VENC_OSDCLK0, 0);
	venc_write(sd, VENC_OSDCLK1, 1);


	venc_write(sd, VENC_VMOD, 0);
	/* DM365 component HD mode */
	venc_modify(sd, VENC_VMOD, (1 << VENC_VMOD_VIE_SHIFT),
		    VENC_VMOD_VIE);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_HDMD, VENC_VMOD_HDMD);
	venc_modify(sd, VENC_VMOD, (HDTV_1080I << VENC_VMOD_TVTYP_SHIFT),
		    VENC_VMOD_TVTYP);
	venc_modify(sd, VENC_VMOD, VENC_VMOD_VENC, VENC_VMOD_VENC);
	venc_write(sd, VENC_XHINTVL, 0);
	return 0;
}

static int venc_s_std_output(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	v4l2_dbg(debug, 1, sd, "venc_s_std_output\n");

	if (norm & V4L2_STD_525_60)
		return venc_set_ntsc(sd);
	else if (norm & V4L2_STD_625_50)
		return venc_set_pal(sd);

	return -EINVAL;
}

static int venc_s_dv_timings(struct v4l2_subdev *sd,
			    struct v4l2_dv_timings *dv_timings)
{
	struct venc_state *venc = to_state(sd);
	u32 height = dv_timings->bt.height;
	int ret;

	v4l2_dbg(debug, 1, sd, "venc_s_dv_timings\n");

	if (height == 576)
		return venc_set_576p50(sd);
	else if (height == 480)
		return venc_set_480p59_94(sd);
	else if ((height == 720) &&
			(venc->venc_type == VPBE_VERSION_2)) {
		/* TBD setup internal 720p mode here */
		ret = venc_set_720p60_internal(sd);
		/* for DM365 VPBE, there is DAC inside */
		vdaccfg_write(sd, VDAC_CONFIG_HD_V2);
		return ret;
	} else if ((height == 1080) &&
		(venc->venc_type == VPBE_VERSION_2)) {
		/* TBD setup internal 1080i mode here */
		ret = venc_set_1080i30_internal(sd);
		/* for DM365 VPBE, there is DAC inside */
		vdaccfg_write(sd, VDAC_CONFIG_HD_V2);
		return ret;
	}
	return -EINVAL;
}

static int venc_s_routing(struct v4l2_subdev *sd, u32 input, u32 output,
			  u32 config)
{
	struct venc_state *venc = to_state(sd);
	int ret;

	v4l2_dbg(debug, 1, sd, "venc_s_routing\n");

	ret = venc_set_dac(sd, output);
	if (!ret)
		venc->output = output;

	return ret;
}

static long venc_ioctl(struct v4l2_subdev *sd,
			unsigned int cmd,
			void *arg)
{
	u32 val;

	switch (cmd) {
	case VENC_GET_FLD:
		val = venc_read(sd, VENC_VSTAT);
		*((int *)arg) = ((val & VENC_VSTAT_FIDST) ==
		VENC_VSTAT_FIDST);
		break;
	default:
		v4l2_err(sd, "Wrong IOCTL cmd\n");
		break;
	}

	return 0;
}

static const struct v4l2_subdev_core_ops venc_core_ops = {
	.ioctl      = venc_ioctl,
};

static const struct v4l2_subdev_video_ops venc_video_ops = {
	.s_routing = venc_s_routing,
	.s_std_output = venc_s_std_output,
	.s_dv_timings = venc_s_dv_timings,
};

static const struct v4l2_subdev_ops venc_ops = {
	.core = &venc_core_ops,
	.video = &venc_video_ops,
};

static int venc_initialize(struct v4l2_subdev *sd)
{
	struct venc_state *venc = to_state(sd);
	int ret;

	/* Set default to output to composite and std to NTSC */
	venc->output = 0;
	venc->std = V4L2_STD_525_60;

	ret = venc_s_routing(sd, 0, venc->output, 0);
	if (ret < 0) {
		v4l2_err(sd, "Error setting output during init\n");
		return -EINVAL;
	}

	ret = venc_s_std_output(sd, venc->std);
	if (ret < 0) {
		v4l2_err(sd, "Error setting std during init\n");
		return -EINVAL;
	}

	return ret;
}

static int venc_device_get(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct venc_state **venc = data;

	if (strstr(pdev->name, "vpbe-venc") != NULL)
		*venc = platform_get_drvdata(pdev);

	return 0;
}

struct v4l2_subdev *venc_sub_dev_init(struct v4l2_device *v4l2_dev,
		const char *venc_name)
{
	struct venc_state *venc;
	int err;

	err = bus_for_each_dev(&platform_bus_type, NULL, &venc,
			venc_device_get);
	if (venc == NULL)
		return NULL;

	v4l2_subdev_init(&venc->sd, &venc_ops);

	strcpy(venc->sd.name, venc_name);
	if (v4l2_device_register_subdev(v4l2_dev, &venc->sd) < 0) {
		v4l2_err(v4l2_dev,
			"vpbe unable to register venc sub device\n");
		return NULL;
	}
	if (venc_initialize(&venc->sd)) {
		v4l2_err(v4l2_dev,
			"vpbe venc initialization failed\n");
		return NULL;
	}

	return &venc->sd;
}
EXPORT_SYMBOL(venc_sub_dev_init);

static int venc_probe(struct platform_device *pdev)
{
	const struct platform_device_id *pdev_id;
	struct venc_state *venc;
	struct resource *res;
	int ret;

	venc = kzalloc(sizeof(struct venc_state), GFP_KERNEL);
	if (venc == NULL)
		return -ENOMEM;

	pdev_id = platform_get_device_id(pdev);
	if (!pdev_id) {
		ret = -EINVAL;
		goto free_mem;
	}
	venc->venc_type = pdev_id->driver_data;
	venc->pdev = &pdev->dev;
	venc->pdata = pdev->dev.platform_data;
	if (NULL == venc->pdata) {
		dev_err(venc->pdev, "Unable to get platform data for"
			" VENC sub device");
		ret = -ENOENT;
		goto free_mem;
	}
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(venc->pdev,
			"Unable to get VENC register address map\n");
		ret = -ENODEV;
		goto free_mem;
	}

	if (!request_mem_region(res->start, resource_size(res), "venc")) {
		dev_err(venc->pdev, "Unable to reserve VENC MMIO region\n");
		ret = -ENODEV;
		goto free_mem;
	}

	venc->venc_base = ioremap_nocache(res->start, resource_size(res));
	if (!venc->venc_base) {
		dev_err(venc->pdev, "Unable to map VENC IO space\n");
		ret = -ENODEV;
		goto release_venc_mem_region;
	}

	if (venc->venc_type != VPBE_VERSION_1) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (!res) {
			dev_err(venc->pdev,
				"Unable to get VDAC_CONFIG address map\n");
			ret = -ENODEV;
			goto unmap_venc_io;
		}

		if (!request_mem_region(res->start,
					resource_size(res), "venc")) {
			dev_err(venc->pdev,
				"Unable to reserve VDAC_CONFIG  MMIO region\n");
			ret = -ENODEV;
			goto unmap_venc_io;
		}

		venc->vdaccfg_reg = ioremap_nocache(res->start,
						    resource_size(res));
		if (!venc->vdaccfg_reg) {
			dev_err(venc->pdev,
				"Unable to map VDAC_CONFIG IO space\n");
			ret = -ENODEV;
			goto release_vdaccfg_mem_region;
		}
	}
	spin_lock_init(&venc->lock);
	platform_set_drvdata(pdev, venc);
	dev_notice(venc->pdev, "VENC sub device probe success\n");
	return 0;

release_vdaccfg_mem_region:
	release_mem_region(res->start, resource_size(res));
unmap_venc_io:
	iounmap(venc->venc_base);
release_venc_mem_region:
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));
free_mem:
	kfree(venc);
	return ret;
}

static int venc_remove(struct platform_device *pdev)
{
	struct venc_state *venc = platform_get_drvdata(pdev);
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	iounmap((void *)venc->venc_base);
	release_mem_region(res->start, resource_size(res));
	if (venc->venc_type != VPBE_VERSION_1) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		iounmap((void *)venc->vdaccfg_reg);
		release_mem_region(res->start, resource_size(res));
	}
	kfree(venc);

	return 0;
}

static struct platform_driver venc_driver = {
	.probe		= venc_probe,
	.remove		= venc_remove,
	.driver		= {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
	},
	.id_table	= vpbe_venc_devtype
};

module_platform_driver(venc_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VPBE VENC Driver");
MODULE_AUTHOR("Texas Instruments");
