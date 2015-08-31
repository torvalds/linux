/*
 * Copyright (C) 2014-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/media-bus-format.h>
#include <media/v4l2-ioctl.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include "mxc_vadc.h"

/* Resource names for the VADC driver. */
#define VAFE_REGS_ADDR_RES_NAME "vadc-vafe"
#define VDEC_REGS_ADDR_RES_NAME "vadc-vdec"

#define reg32_write(addr, val) __raw_writel(val, addr)
#define reg32_read(addr)       __raw_readl(addr)
#define reg32setbit(addr, bitpos) \
	reg32_write((addr), (reg32_read((addr)) | (1<<(bitpos))))

#define reg32clrbit(addr, bitpos) \
	reg32_write((addr), (reg32_read((addr)) & (0xFFFFFFFF ^ (1<<(bitpos)))))

#define GPC_CNTR		0x00
#define IMX6SX_GPC_CNTR_VADC_ANALOG_OFF_MASK	BIT(17)
#define IMX6SX_GPC_CNTR_VADC_POWER_DOWN_MASK	BIT(18)

void __iomem *vafe_regbase;
void __iomem *vdec_regbase;


/* List of input video formats supported. The video formats is corresponding
 * with v4l2 id in video_fmt
 */
enum video_fmt_idx {
	VADC_NTSC = 0,	/* Locked on (M) NTSC video signal. */
	VADC_PAL,		/* (B, G, H, I, N)PAL video signal. */
};

/* Number of video standards supported (including 'not locked' signal). */
#define VADC_STD_MAX		(VADC_PAL + 1)

/* Video format structure. */
struct video_fmt{
	v4l2_std_id  v4l2_std;	/* Video for linux ID. */
	char name[16];		/* Name (e.g., "NTSC", "PAL", etc.) */
	u16 raw_width;		/* Raw width. */
	u16 raw_height;		/* Raw height. */
	u16 active_width;	/* Active width. */
	u16 active_height;	/* Active height. */
	u16 framerates;
};

/*
 * Maintains the information on the current state of the sensor.
 */
struct vadc_state {
	struct v4l2_device v4l2_dev;
	struct v4l2_subdev sd;
	struct video_fmt *fmt;

	struct clk *vadc_clk;
	struct clk *csi_clk;
	struct regmap *gpr;
	void __iomem *gpc_reg;

	u32 vadc_in;
	u32 csi_id;
};

static int vadc_querystd(struct v4l2_subdev *sd, v4l2_std_id *std);

/* Description of video formats supported.
 *
 *  PAL: raw=720x625, active=720x576.
 *  NTSC: raw=720x525, active=720x480.
 */
static struct video_fmt video_fmts[] = {
	/* NTSC */
	{
	 .v4l2_std = V4L2_STD_NTSC,
	 .name = "NTSC",
	 .raw_width = 720,
	 .raw_height = 525,
	 .active_width = 720,
	 .active_height = 480,
	 .framerates = 30,
	 },
	/* (B, G, H, I, N) PAL */
	{
	 .v4l2_std = V4L2_STD_PAL,
	 .name = "PAL",
	 .raw_width = 720,
	 .raw_height = 625,
	 .active_width = 720,
	 .active_height = 576,
	 .framerates = 25,
	 },
};

static void afe_voltage_clampingmode(void)
{
	reg32_write(AFE_CLAMP, 0x07);
	reg32_write(AFE_CLMPAMP, 0x60);
	reg32_write(AFE_CLMPDAT, 0xF0);
}

static void afe_alwayson_clampingmode(void)
{
	reg32_write(AFE_CLAMP, 0x15);
	reg32_write(AFE_CLMPDAT, 0x08);
	reg32_write(AFE_CLMPAMP, 0x00);
}

static void afe_init(void)
{
	pr_debug("%s\n", __func__);

	reg32_write(AFE_PDBUF, 0x1f);
	reg32_write(AFE_PDADC, 0x0f);
	reg32_write(AFE_PDSARH, 0x01);
	reg32_write(AFE_PDSARL, 0xff);
	reg32_write(AFE_PDADCRFH, 0x01);
	reg32_write(AFE_PDADCRFL, 0xff);
	reg32_write(AFE_ICTRL, 0x3a);
	reg32_write(AFE_ICTLSTG, 0x1e);

	reg32_write(AFE_RCTRLSTG, 0x1e);
	reg32_write(AFE_INPBUF, 0x035);
	reg32_write(AFE_INPFLT, 0x02);
	reg32_write(AFE_ADCDGN, 0x40);
	reg32_write(AFE_TSTSEL, 0x10);

	reg32_write(AFE_ACCTST, 0x07);

	reg32_write(AFE_BGREG, 0x08);

	reg32_write(AFE_ADCGN, 0x09);

	/* set current controlled clamping
	* always on, low current */
	reg32_write(AFE_CLAMP, 0x11);
	reg32_write(AFE_CLMPAMP, 0x08);
}

static void vdec_mode_timing_init(int std)
{
	if (std == V4L2_STD_NTSC) {
		/* NTSC 720x480 */
		reg32_write(VDEC_HACTS, 0x66);
		reg32_write(VDEC_HACTE, 0x24);

		reg32_write(VDEC_VACTS, 0x29);
		reg32_write(VDEC_VACTE, 0x04);

		/* set V Position */
		reg32_write(VDEC_VRTPOS, 0x2);
	} else if (std == V4L2_STD_PAL) {
		/* PAL 720x576 */
		reg32_write(VDEC_HACTS, 0x66);
		reg32_write(VDEC_HACTE, 0x24);

		reg32_write(VDEC_VACTS, 0x29);
		reg32_write(VDEC_VACTE, 0x04);

		/* set V Position */
		reg32_write(VDEC_VRTPOS, 0x6);
	} else
		pr_debug("Error not support video mode\n");

	/* set H Position */
	reg32_write(VDEC_HZPOS, 0x60);

	/* set H ignore start */
	reg32_write(VDEC_HSIGS, 0xf8);

	/* set H ignore end */
	reg32_write(VDEC_HSIGE, 0x18);
}

/*
* vdec_init()
* Initialises the VDEC registers
* Returns: nothing
*/
static void vdec_init(struct vadc_state *vadc)
{
	v4l2_std_id std;

	pr_debug("%s\n", __func__);

	/* Get work mode PAL or NTSC */
	vadc_querystd(&vadc->sd, &std);

	vdec_mode_timing_init(std);

	/* vcr detect threshold high, automatic detections */
	reg32_write(VDEC_VSCON2, 0);

	reg32_write(VDEC_BASE + 0x110, 0x01);

	/* set the noramp mode on the Hloop PLL. */
	reg32_write(VDEC_BASE+(0x14*4), 0x10);

	/* set the YC relative delay.*/
	reg32_write(VDEC_YCDEL, 0x90);

	/* setup the Hpll */
	reg32_write(VDEC_BASE+(0x13*4), 0x13);

	/* setup the 2d comb */
	/* set the gain of the Hdetail output to 3
	 * set the notch alpha gain to 1 */
	reg32_write(VDEC_CFC2, 0x34);

	/* setup various 2d comb bits.*/
	reg32_write(VDEC_BASE+(0x02*4), 0x01);
	reg32_write(VDEC_BASE+(0x03*4), 0x18);
	reg32_write(VDEC_BASE+(0x04*4), 0x34);

	/* set the start of the burst gate */
	reg32_write(VDEC_BRSTGT, 0x30);

	/* set 1f motion gain */
	reg32_write(VDEC_BASE+(0x0f*4), 0x20);

	/* set the 1F chroma motion detector thresh
	 * for colour reverse detection */
	reg32_write(VDEC_THSH1, 0x02);
	reg32_write(VDEC_BASE+(0x4a*4), 0x20);
	reg32_write(VDEC_BASE+(0x4b*4), 0x08);

	reg32_write(VDEC_BASE+(0x4c*4), 0x08);

	/* set the threshold for the narrow/wide adaptive chroma BW */
	reg32_write(VDEC_BASE+(0x20*4), 0x20);

	/* turn up the colour with the new colour gain reg */
	/* hue: */
	reg32_write(VDEC_HUE, 0x00);

	/* cbgain: 22 B4 */
	reg32_write(VDEC_CBGN, 0xb4);
	/* cr gain 80 */
	reg32_write(VDEC_CRGN, 0x80);
	/* luma gain (contrast) */
	reg32_write(VDEC_CNTR, 0x80);

	/* setup the signed black level register, brightness */
	reg32_write(VDEC_BRT, 0x00);

	/* filter the standard detection
	 * enable the comb for the ntsc443 */
	reg32_write(VDEC_STDDBG, 0x20);

	/* setup chroma kill thresh for no chroma */
	reg32_write(VDEC_CHBTH, 0x0);

	/* set chroma loop to wider BW
	 * no set it to normal BW. i fixed the bw problem.*/
	reg32_write(VDEC_YCDEL, 0x00);

	/* set the compensation in the chroma loop for the Hloop
	 * set the ratio for the nonarithmetic 3d comb modes.*/
	reg32_write(VDEC_BASE + (0x1d*4), 0x90);

	/* set the threshold for the nonarithmetic mode for the 2d comb
	 * the higher the value the more Fc Fh offset
	 * we will tolerate before turning off the comb. */
	reg32_write(VDEC_BASE + (0x33*4), 0xa0);

	/* setup the bluescreen output colour */
	reg32_write(VDEC_BASE + (0x3d*4), 35);
	reg32_write(VDEC_BLSCRCR, 114);
	reg32_write(VDEC_BLSCRCB, 212);

	/* disable the active blanking */
	reg32_write(VDEC_BASE + (0x15*4), 0x02);

	/* setup the luma agc for automatic gain. */
	reg32_write(VDEC_LMAGC2, 0x5e);
	reg32_write(VDEC_LMAGC1, 0x81);

	/* setup chroma agc */
	reg32_write(VDEC_CHAGC2, 0xa0);
	reg32_write(VDEC_CHAGC1, 0x01);

	/* setup the MV thresh lower nibble
	 * setup the sync top cap, upper nibble */
	reg32_write(VDEC_BASE + (0x3a*4), 0x80);
	reg32_write(VDEC_SHPIMP, 0x00);

	/* setup the vsync block */
	reg32_write(VDEC_VSCON1, 0x87);

	/* set the nosignal threshold
	 * set the vsync threshold */
	reg32_write(VDEC_VSSGTH, 0x35);

	/* set length for min hphase filter
	 * (or saturate limit if saturate is chosen) */
	reg32_write(VDEC_BASE + (0x45*4), 0x40);

	/* enable the internal resampler,
	 * select min filter not saturate for
	 * hphase noise filter for vcr detect.
	 * enable vcr pause mode different field lengths */
	reg32_write(VDEC_BASE + (0x46*4), 0x90);

	/* disable VCR detection, lock to the Hsync rather than the Vsync */
	reg32_write(VDEC_VSCON2, 0x04);

	/* set tiplevel goal for dc clamp. */
	reg32_write(VDEC_BASE + (0x3c*4), 0xB0);

	/* override SECAM detection and force SECAM off */
	reg32_write(VDEC_BASE + (0x2f*4), 0x20);

	/* Set r3d_hardblend in 3D control2 reg */
	reg32_write(VDEC_BASE + (0x0c*4), 0x04);
}

/* set Input selector & input pull-downs */
static void vadc_s_routing(int vadc_in)
{
	switch (vadc_in) {
	case 0:
		reg32_write(AFE_INPFLT, 0x02);
		reg32_write(AFE_OFFDRV, 0x00);
		reg32_write(AFE_INPCONFIG, 0x1e);
		break;
	case 1:
		reg32_write(AFE_INPFLT, 0x02);
		reg32_write(AFE_OFFDRV, 0x00);
		reg32_write(AFE_INPCONFIG, 0x2d);
		break;
	case 2:
		reg32_write(AFE_INPFLT, 0x02);
		reg32_write(AFE_OFFDRV, 0x00);
		reg32_write(AFE_INPCONFIG, 0x4b);
		break;
	case 3:
		reg32_write(AFE_INPFLT, 0x02);
		reg32_write(AFE_OFFDRV, 0x00);
		reg32_write(AFE_INPCONFIG, 0x87);
		break;
	default:
		pr_debug("error video input %d\n", vadc_in);
	}
}

static void vadc_power_up(struct vadc_state *state)
{
	/* Power on vadc analog */
	reg32clrbit(state->gpc_reg + GPC_CNTR, 17);

	/* Power down vadc ext power */
	reg32clrbit(state->gpc_reg + GPC_CNTR, 18);

	/* software reset afe  */
	regmap_update_bits(state->gpr, IOMUXC_GPR1,
			IMX6SX_GPR1_VADC_SW_RST_MASK,
			IMX6SX_GPR1_VADC_SW_RST_RESET);

	msleep(10);

	/* clock config for vadc */
	reg32_write(VDEC_BASE + 0x320, 0xe3);
	reg32_write(VDEC_BASE + 0x324, 0x38);
	reg32_write(VDEC_BASE + 0x328, 0x8e);
	reg32_write(VDEC_BASE + 0x32c, 0x23);

	/* Release reset bit  */
	regmap_update_bits(state->gpr, IOMUXC_GPR1,
			IMX6SX_GPR1_VADC_SW_RST_MASK,
			IMX6SX_GPR1_VADC_SW_RST_RELEASE);

	/* Power on vadc ext power */
	reg32setbit(state->gpc_reg + GPC_CNTR, 18);
}

static void vadc_power_down(struct vadc_state *state)
{
	/* Power down vadc analog */
	reg32setbit(state->gpc_reg + GPC_CNTR, 17);

	/* Power down vadc ext power */
	reg32clrbit(state->gpc_reg + GPC_CNTR, 18);

}
static void vadc_init(struct vadc_state *vadc)
{
	pr_debug("%s\n", __func__);

	vadc_power_up(vadc);

	afe_init();

	/* select Video Input 0-3 */
	vadc_s_routing(vadc->vadc_in);

	afe_voltage_clampingmode();

	vdec_init(vadc);

	/*
	* current control loop will move sinewave input off below
	* the bottom of the signal range visible
	* when the testbus is viewed as magnitude,
	* so have to break before this point while capturing ENOB data:
	*/
	afe_alwayson_clampingmode();
}

static inline struct vadc_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct vadc_state, sd);
}

static int vadc_g_std(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct vadc_state *state = to_state(sd);

	*std = state->fmt->v4l2_std;
	return 0;
}

/*!
 * Return attributes of current video standard.
 * Since this device autodetects the current standard, this function also
 * sets the values that need to be changed if the standard changes.
 * There is no set std equivalent function.
 *
 *  @return		None.
 */
static int vadc_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct vadc_state *state = to_state(sd);
	int mod;
	int idx;
	int i;

	/* Read auto mode detected result */
	printk(KERN_INFO"wait vadc auto detect video mode....\n");
	for (i = 0; i < 10; i++) {
		msleep(200);
		mod = reg32_read(VDEC_VIDMOD);
		/* Check video signal states */
		if ((mod & VDEC_VIDMOD_SIGNAL_MASK)
				== VDEC_VIDMOD_SIGNAL_DETECT)
			break;
	}
	if (i == 10)
		printk(KERN_INFO"Timeout detect video signal mod=0x%x\n", mod);

	if ((mod & VDEC_VIDMOD_PAL_MASK) || (mod & VDEC_VIDMOD_M625_MASK))
		idx = VADC_PAL;
	else
		idx = VADC_NTSC;

	*std = video_fmts[idx].v4l2_std;
	state->fmt = &video_fmts[idx];

	printk(KERN_INFO"video mode %s\n", video_fmts[idx].name);
	return 0;
}

static int vadc_enum_mbus_fmt(struct v4l2_subdev *sd,
				unsigned index,	u32 *code)
{
	/* support only one format  */
	if (index >= 1)
		return -EINVAL;

	*code = MEDIA_BUS_FMT_AYUV8_1X32;
	return 0;
}

static int vadc_mbus_fmt(struct v4l2_subdev *sd,
			    struct v4l2_mbus_framefmt *fmt)
{
	struct vadc_state *state = to_state(sd);

	fmt->code = MEDIA_BUS_FMT_AYUV8_1X32;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	fmt->field = V4L2_FIELD_INTERLACED;
	fmt->width = 720;
	fmt->height = state->fmt->v4l2_std & V4L2_STD_NTSC ? 480 : 576;

	return 0;
}

static int vadc_enum_framesizes(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	struct vadc_state *state = to_state(sd);
	if (fse->index >= 1)
		return -EINVAL;

	fse->min_width = state->fmt->active_width;
	fse->max_width = state->fmt->active_width;
	fse->min_height  = state->fmt->active_height;
	fse->max_height  = state->fmt->active_height;

	return 0;
}
static int vadc_enum_frameintervals(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_frame_interval_enum *fie)
{
	struct vadc_state *state = to_state(sd);

	if (fie->index < 0 || fie->index >= 1)
		return -EINVAL;

	fie->interval.numerator = 1;

	fie->interval.denominator = state->fmt->framerates;

	return 0;
}

static int vadc_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct vadc_state *state = to_state(sd);

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (parms->parm.capture.timeperframe.denominator
				!= state->fmt->framerates)
		parms->parm.capture.timeperframe.denominator
				= state->fmt->framerates;

	return 0;
}

static const struct v4l2_subdev_video_ops vadc_video_ops = {
	.querystd              = vadc_querystd,
	.enum_mbus_fmt         = vadc_enum_mbus_fmt,
	.try_mbus_fmt          = vadc_mbus_fmt,
	.g_mbus_fmt            = vadc_mbus_fmt,
	.s_mbus_fmt            = vadc_mbus_fmt,
	.s_parm                = vadc_s_parm,
	.g_std                 = vadc_g_std,
};

static const struct v4l2_subdev_pad_ops vadc_pad_ops = {
	.enum_frame_size       = vadc_enum_framesizes,
	.enum_frame_interval   = vadc_enum_frameintervals,
};

static const struct v4l2_subdev_ops vadc_ops = {
	.video = &vadc_video_ops,
	.pad = &vadc_pad_ops,
};

static const struct of_device_id fsl_vadc_dt_ids[] = {
	{ .compatible = "fsl,imx6sx-vadc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_vadc_dt_ids);

static int vadc_of_init(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *gpc_np;
	struct vadc_state *state = platform_get_drvdata(pdev);
	int csi_id;
	int ret;

	/* Get csi_id to setting vadc to csi mux in gpr */
	ret = of_property_read_u32(np, "csi_id", &csi_id);
	if (ret) {
		dev_err(&pdev->dev, "failed to read of property csi_id\n");
		return ret;
	}

	state->csi_id = csi_id;

	/* remap GPR register */
	state->gpr = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
			"gpr");
	if (IS_ERR(state->gpr)) {
		dev_dbg(&pdev->dev, "can not get gpr\n");
		return -ENOMEM;
	}

	/* Configuration vadc-to-csi 0 or 1 */
	if (csi_id) {
		regmap_update_bits(state->gpr, IOMUXC_GPR5,
				IMX6SX_GPR5_CSI2_MUX_CTRL_MASK,
				IMX6SX_GPR5_CSI2_MUX_CTRL_CVD);
	} else {
		regmap_update_bits(state->gpr, IOMUXC_GPR5,
				IMX6SX_GPR5_CSI1_MUX_CTRL_MASK,
				IMX6SX_GPR5_CSI1_MUX_CTRL_CVD);
	}

	/* Get default vadc_in number  */
	ret = of_property_read_u32(np, "vadc_in", &state->vadc_in);
	if (ret) {
		dev_err(&pdev->dev, "failed to read of property vadc_in\n");
		return ret;
	}

	/* map GPC register  */
	gpc_np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-gpc");
	state->gpc_reg = of_iomap(gpc_np, 0);
	if (!state->gpc_reg) {
		dev_err(&pdev->dev, "ioremap failed with gpc base\n");
		goto error;
	}

	return ret;

error:
	iounmap(state->gpc_reg);
	return ret;
}

static void vadc_v4l2_subdev_init(struct v4l2_subdev *sd,
		struct platform_device *pdev,
		const struct v4l2_subdev_ops *ops)
{
	struct vadc_state *state = platform_get_drvdata(pdev);
	int ret = 0;

	v4l2_subdev_init(sd, ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->owner = pdev->dev.driver->owner;
	sd->dev = &pdev->dev;

	/* initialize name */
	snprintf(sd->name, sizeof(sd->name), "%s",
		pdev->dev.driver->name);

	v4l2_set_subdevdata(sd, state);

	ret = v4l2_async_register_subdev(sd);
	if (ret < 0)
		dev_err(&pdev->dev, "%s--Async register faialed, ret=%d\n", __func__, ret);
}

static int vadc_probe(struct platform_device *pdev)
{
	struct vadc_state *state;
	struct v4l2_subdev *sd;
	struct resource *res;
	int ret = 0;

	state = devm_kzalloc(&pdev->dev, sizeof(struct vadc_state), GFP_KERNEL);
	if (!state) {
		dev_err(&pdev->dev, "Cannot allocate device data\n");
		return -ENOMEM;
	}

	/* Set initial values for the sensor struct. */
	state->fmt = &video_fmts[VADC_NTSC];

	sd = &state->sd;

	/* map vafe address  */
	res = platform_get_resource_byname(pdev,
				IORESOURCE_MEM, VAFE_REGS_ADDR_RES_NAME);
	if (!res) {
		dev_err(&pdev->dev, "No vafe base address found.\n");
		return -ENOMEM;
	}
	vafe_regbase = devm_ioremap_resource(&pdev->dev, res);
	if (!vafe_regbase) {
		dev_err(&pdev->dev, "ioremap failed with vafe base\n");
		return -ENOMEM;
	}

	/* map vdec address  */
	res = platform_get_resource_byname(pdev,
				IORESOURCE_MEM, VDEC_REGS_ADDR_RES_NAME);
	if (!res) {
		dev_err(&pdev->dev, "No vdec base address found.\n");
		return -ENODEV;
	}
	vdec_regbase = devm_ioremap_resource(&pdev->dev, res);
	if (!vdec_regbase) {
		dev_err(&pdev->dev, "ioremap failed with vdec base\n");
		return -ENOMEM;
	}

	/* Get clock */
	state->vadc_clk = devm_clk_get(&pdev->dev, "vadc");
	if (IS_ERR(state->vadc_clk)) {
		ret = PTR_ERR(state->vadc_clk);
		return ret;
	}

	state->csi_clk = devm_clk_get(&pdev->dev, "csi");
	if (IS_ERR(state->csi_clk)) {
		ret = PTR_ERR(state->csi_clk);
		return ret;
	}

	/* clock  */
	clk_prepare_enable(state->csi_clk);
	clk_prepare_enable(state->vadc_clk);

	platform_set_drvdata(pdev, state);

	vadc_v4l2_subdev_init(sd, pdev, &vadc_ops);

	pm_runtime_enable(&pdev->dev);

	pm_runtime_get_sync(&pdev->dev);
	/* Init VADC */
	ret = vadc_of_init(pdev);
	if (ret < 0)
		goto err;
	vadc_init(state);

	pr_info("vadc driver loaded\n");

	return 0;
err:
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	v4l2_async_unregister_subdev(&state->sd);
	clk_disable_unprepare(state->csi_clk);
	clk_disable_unprepare(state->vadc_clk);
	return ret;
}

static int vadc_remove(struct platform_device *pdev)
{
	struct vadc_state *state = platform_get_drvdata(pdev);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	v4l2_async_unregister_subdev(&state->sd);
	clk_disable_unprepare(state->csi_clk);
	clk_disable_unprepare(state->vadc_clk);

	vadc_power_down(state);
	return true;
}

static int vadc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct vadc_state *state = platform_get_drvdata(pdev);

	clk_disable(state->csi_clk);
	clk_disable(state->vadc_clk);

	vadc_power_down(state);

	return 0;
}

static int vadc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct vadc_state *state = platform_get_drvdata(pdev);

	clk_enable(state->csi_clk);
	clk_enable(state->vadc_clk);

	vadc_init(state);
	return 0;
}

static const struct dev_pm_ops vadc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(vadc_suspend, vadc_resume)
};

static struct platform_driver vadc_driver = {
	.driver = {
		.name = "fsl_vadc",
		.of_match_table = of_match_ptr(fsl_vadc_dt_ids),
		.pm	= &vadc_pm_ops,
	},
	.probe = vadc_probe,
	.remove = vadc_remove,
};

module_platform_driver(vadc_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("fsl VADC/VDEC driver");
MODULE_LICENSE("GPL");
