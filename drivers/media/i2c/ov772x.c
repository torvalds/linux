// SPDX-License-Identifier: GPL-2.0
/*
 * ov772x Camera Driver
 *
 * Copyright (C) 2017 Jacopo Mondi <jacopo+renesas@jmondi.org>
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * Based on ov7670 and soc_camera_platform driver,
 *
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
 * Copyright (C) 2008 Magnus Damm
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>

#include <media/i2c/ov772x.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-subdev.h>

/*
 * register offset
 */
#define GAIN        0x00 /* AGC - Gain control gain setting */
#define BLUE        0x01 /* AWB - Blue channel gain setting */
#define RED         0x02 /* AWB - Red   channel gain setting */
#define GREEN       0x03 /* AWB - Green channel gain setting */
#define COM1        0x04 /* Common control 1 */
#define BAVG        0x05 /* U/B Average Level */
#define GAVG        0x06 /* Y/Gb Average Level */
#define RAVG        0x07 /* V/R Average Level */
#define AECH        0x08 /* Exposure Value - AEC MSBs */
#define COM2        0x09 /* Common control 2 */
#define PID         0x0A /* Product ID Number MSB */
#define VER         0x0B /* Product ID Number LSB */
#define COM3        0x0C /* Common control 3 */
#define COM4        0x0D /* Common control 4 */
#define COM5        0x0E /* Common control 5 */
#define COM6        0x0F /* Common control 6 */
#define AEC         0x10 /* Exposure Value */
#define CLKRC       0x11 /* Internal clock */
#define COM7        0x12 /* Common control 7 */
#define COM8        0x13 /* Common control 8 */
#define COM9        0x14 /* Common control 9 */
#define COM10       0x15 /* Common control 10 */
#define REG16       0x16 /* Register 16 */
#define HSTART      0x17 /* Horizontal sensor size */
#define HSIZE       0x18 /* Horizontal frame (HREF column) end high 8-bit */
#define VSTART      0x19 /* Vertical frame (row) start high 8-bit */
#define VSIZE       0x1A /* Vertical sensor size */
#define PSHFT       0x1B /* Data format - pixel delay select */
#define MIDH        0x1C /* Manufacturer ID byte - high */
#define MIDL        0x1D /* Manufacturer ID byte - low  */
#define LAEC        0x1F /* Fine AEC value */
#define COM11       0x20 /* Common control 11 */
#define BDBASE      0x22 /* Banding filter Minimum AEC value */
#define DBSTEP      0x23 /* Banding filter Maximum Setp */
#define AEW         0x24 /* AGC/AEC - Stable operating region (upper limit) */
#define AEB         0x25 /* AGC/AEC - Stable operating region (lower limit) */
#define VPT         0x26 /* AGC/AEC Fast mode operating region */
#define REG28       0x28 /* Register 28 */
#define HOUTSIZE    0x29 /* Horizontal data output size MSBs */
#define EXHCH       0x2A /* Dummy pixel insert MSB */
#define EXHCL       0x2B /* Dummy pixel insert LSB */
#define VOUTSIZE    0x2C /* Vertical data output size MSBs */
#define ADVFL       0x2D /* LSB of insert dummy lines in Vertical direction */
#define ADVFH       0x2E /* MSG of insert dummy lines in Vertical direction */
#define YAVE        0x2F /* Y/G Channel Average value */
#define LUMHTH      0x30 /* Histogram AEC/AGC Luminance high level threshold */
#define LUMLTH      0x31 /* Histogram AEC/AGC Luminance low  level threshold */
#define HREF        0x32 /* Image start and size control */
#define DM_LNL      0x33 /* Dummy line low  8 bits */
#define DM_LNH      0x34 /* Dummy line high 8 bits */
#define ADOFF_B     0x35 /* AD offset compensation value for B  channel */
#define ADOFF_R     0x36 /* AD offset compensation value for R  channel */
#define ADOFF_GB    0x37 /* AD offset compensation value for Gb channel */
#define ADOFF_GR    0x38 /* AD offset compensation value for Gr channel */
#define OFF_B       0x39 /* Analog process B  channel offset value */
#define OFF_R       0x3A /* Analog process R  channel offset value */
#define OFF_GB      0x3B /* Analog process Gb channel offset value */
#define OFF_GR      0x3C /* Analog process Gr channel offset value */
#define COM12       0x3D /* Common control 12 */
#define COM13       0x3E /* Common control 13 */
#define COM14       0x3F /* Common control 14 */
#define COM15       0x40 /* Common control 15*/
#define COM16       0x41 /* Common control 16 */
#define TGT_B       0x42 /* BLC blue channel target value */
#define TGT_R       0x43 /* BLC red  channel target value */
#define TGT_GB      0x44 /* BLC Gb   channel target value */
#define TGT_GR      0x45 /* BLC Gr   channel target value */
/* for ov7720 */
#define LCC0        0x46 /* Lens correction control 0 */
#define LCC1        0x47 /* Lens correction option 1 - X coordinate */
#define LCC2        0x48 /* Lens correction option 2 - Y coordinate */
#define LCC3        0x49 /* Lens correction option 3 */
#define LCC4        0x4A /* Lens correction option 4 - radius of the circular */
#define LCC5        0x4B /* Lens correction option 5 */
#define LCC6        0x4C /* Lens correction option 6 */
/* for ov7725 */
#define LC_CTR      0x46 /* Lens correction control */
#define LC_XC       0x47 /* X coordinate of lens correction center relative */
#define LC_YC       0x48 /* Y coordinate of lens correction center relative */
#define LC_COEF     0x49 /* Lens correction coefficient */
#define LC_RADI     0x4A /* Lens correction radius */
#define LC_COEFB    0x4B /* Lens B channel compensation coefficient */
#define LC_COEFR    0x4C /* Lens R channel compensation coefficient */

#define FIXGAIN     0x4D /* Analog fix gain amplifer */
#define AREF0       0x4E /* Sensor reference control */
#define AREF1       0x4F /* Sensor reference current control */
#define AREF2       0x50 /* Analog reference control */
#define AREF3       0x51 /* ADC    reference control */
#define AREF4       0x52 /* ADC    reference control */
#define AREF5       0x53 /* ADC    reference control */
#define AREF6       0x54 /* Analog reference control */
#define AREF7       0x55 /* Analog reference control */
#define UFIX        0x60 /* U channel fixed value output */
#define VFIX        0x61 /* V channel fixed value output */
#define AWBB_BLK    0x62 /* AWB option for advanced AWB */
#define AWB_CTRL0   0x63 /* AWB control byte 0 */
#define DSP_CTRL1   0x64 /* DSP control byte 1 */
#define DSP_CTRL2   0x65 /* DSP control byte 2 */
#define DSP_CTRL3   0x66 /* DSP control byte 3 */
#define DSP_CTRL4   0x67 /* DSP control byte 4 */
#define AWB_BIAS    0x68 /* AWB BLC level clip */
#define AWB_CTRL1   0x69 /* AWB control  1 */
#define AWB_CTRL2   0x6A /* AWB control  2 */
#define AWB_CTRL3   0x6B /* AWB control  3 */
#define AWB_CTRL4   0x6C /* AWB control  4 */
#define AWB_CTRL5   0x6D /* AWB control  5 */
#define AWB_CTRL6   0x6E /* AWB control  6 */
#define AWB_CTRL7   0x6F /* AWB control  7 */
#define AWB_CTRL8   0x70 /* AWB control  8 */
#define AWB_CTRL9   0x71 /* AWB control  9 */
#define AWB_CTRL10  0x72 /* AWB control 10 */
#define AWB_CTRL11  0x73 /* AWB control 11 */
#define AWB_CTRL12  0x74 /* AWB control 12 */
#define AWB_CTRL13  0x75 /* AWB control 13 */
#define AWB_CTRL14  0x76 /* AWB control 14 */
#define AWB_CTRL15  0x77 /* AWB control 15 */
#define AWB_CTRL16  0x78 /* AWB control 16 */
#define AWB_CTRL17  0x79 /* AWB control 17 */
#define AWB_CTRL18  0x7A /* AWB control 18 */
#define AWB_CTRL19  0x7B /* AWB control 19 */
#define AWB_CTRL20  0x7C /* AWB control 20 */
#define AWB_CTRL21  0x7D /* AWB control 21 */
#define GAM1        0x7E /* Gamma Curve  1st segment input end point */
#define GAM2        0x7F /* Gamma Curve  2nd segment input end point */
#define GAM3        0x80 /* Gamma Curve  3rd segment input end point */
#define GAM4        0x81 /* Gamma Curve  4th segment input end point */
#define GAM5        0x82 /* Gamma Curve  5th segment input end point */
#define GAM6        0x83 /* Gamma Curve  6th segment input end point */
#define GAM7        0x84 /* Gamma Curve  7th segment input end point */
#define GAM8        0x85 /* Gamma Curve  8th segment input end point */
#define GAM9        0x86 /* Gamma Curve  9th segment input end point */
#define GAM10       0x87 /* Gamma Curve 10th segment input end point */
#define GAM11       0x88 /* Gamma Curve 11th segment input end point */
#define GAM12       0x89 /* Gamma Curve 12th segment input end point */
#define GAM13       0x8A /* Gamma Curve 13th segment input end point */
#define GAM14       0x8B /* Gamma Curve 14th segment input end point */
#define GAM15       0x8C /* Gamma Curve 15th segment input end point */
#define SLOP        0x8D /* Gamma curve highest segment slope */
#define DNSTH       0x8E /* De-noise threshold */
#define EDGE_STRNGT 0x8F /* Edge strength  control when manual mode */
#define EDGE_TRSHLD 0x90 /* Edge threshold control when manual mode */
#define DNSOFF      0x91 /* Auto De-noise threshold control */
#define EDGE_UPPER  0x92 /* Edge strength upper limit when Auto mode */
#define EDGE_LOWER  0x93 /* Edge strength lower limit when Auto mode */
#define MTX1        0x94 /* Matrix coefficient 1 */
#define MTX2        0x95 /* Matrix coefficient 2 */
#define MTX3        0x96 /* Matrix coefficient 3 */
#define MTX4        0x97 /* Matrix coefficient 4 */
#define MTX5        0x98 /* Matrix coefficient 5 */
#define MTX6        0x99 /* Matrix coefficient 6 */
#define MTX_CTRL    0x9A /* Matrix control */
#define BRIGHT      0x9B /* Brightness control */
#define CNTRST      0x9C /* Contrast contrast */
#define CNTRST_CTRL 0x9D /* Contrast contrast center */
#define UVAD_J0     0x9E /* Auto UV adjust contrast 0 */
#define UVAD_J1     0x9F /* Auto UV adjust contrast 1 */
#define SCAL0       0xA0 /* Scaling control 0 */
#define SCAL1       0xA1 /* Scaling control 1 */
#define SCAL2       0xA2 /* Scaling control 2 */
#define FIFODLYM    0xA3 /* FIFO manual mode delay control */
#define FIFODLYA    0xA4 /* FIFO auto   mode delay control */
#define SDE         0xA6 /* Special digital effect control */
#define USAT        0xA7 /* U component saturation control */
#define VSAT        0xA8 /* V component saturation control */
/* for ov7720 */
#define HUE0        0xA9 /* Hue control 0 */
#define HUE1        0xAA /* Hue control 1 */
/* for ov7725 */
#define HUECOS      0xA9 /* Cosine value */
#define HUESIN      0xAA /* Sine value */

#define SIGN        0xAB /* Sign bit for Hue and contrast */
#define DSPAUTO     0xAC /* DSP auto function ON/OFF control */

/*
 * register detail
 */

/* COM2 */
#define SOFT_SLEEP_MODE 0x10	/* Soft sleep mode */
				/* Output drive capability */
#define OCAP_1x         0x00	/* 1x */
#define OCAP_2x         0x01	/* 2x */
#define OCAP_3x         0x02	/* 3x */
#define OCAP_4x         0x03	/* 4x */

/* COM3 */
#define SWAP_MASK       (SWAP_RGB | SWAP_YUV | SWAP_ML)
#define IMG_MASK        (VFLIP_IMG | HFLIP_IMG | SCOLOR_TEST)

#define VFLIP_IMG       0x80	/* Vertical flip image ON/OFF selection */
#define HFLIP_IMG       0x40	/* Horizontal mirror image ON/OFF selection */
#define SWAP_RGB        0x20	/* Swap B/R  output sequence in RGB mode */
#define SWAP_YUV        0x10	/* Swap Y/UV output sequence in YUV mode */
#define SWAP_ML         0x08	/* Swap output MSB/LSB */
				/* Tri-state option for output clock */
#define NOTRI_CLOCK     0x04	/*   0: Tri-state    at this period */
				/*   1: No tri-state at this period */
				/* Tri-state option for output data */
#define NOTRI_DATA      0x02	/*   0: Tri-state    at this period */
				/*   1: No tri-state at this period */
#define SCOLOR_TEST     0x01	/* Sensor color bar test pattern */

/* COM4 */
				/* PLL frequency control */
#define PLL_BYPASS      0x00	/*  00: Bypass PLL */
#define PLL_4x          0x40	/*  01: PLL 4x */
#define PLL_6x          0x80	/*  10: PLL 6x */
#define PLL_8x          0xc0	/*  11: PLL 8x */
				/* AEC evaluate window */
#define AEC_FULL        0x00	/*  00: Full window */
#define AEC_1p2         0x10	/*  01: 1/2  window */
#define AEC_1p4         0x20	/*  10: 1/4  window */
#define AEC_2p3         0x30	/*  11: Low 2/3 window */
#define COM4_RESERVED   0x01	/* Reserved bit */

/* COM5 */
#define AFR_ON_OFF      0x80	/* Auto frame rate control ON/OFF selection */
#define AFR_SPPED       0x40	/* Auto frame rate control speed selection */
				/* Auto frame rate max rate control */
#define AFR_NO_RATE     0x00	/*     No  reduction of frame rate */
#define AFR_1p2         0x10	/*     Max reduction to 1/2 frame rate */
#define AFR_1p4         0x20	/*     Max reduction to 1/4 frame rate */
#define AFR_1p8         0x30	/* Max reduction to 1/8 frame rate */
				/* Auto frame rate active point control */
#define AF_2x           0x00	/*     Add frame when AGC reaches  2x gain */
#define AF_4x           0x04	/*     Add frame when AGC reaches  4x gain */
#define AF_8x           0x08	/*     Add frame when AGC reaches  8x gain */
#define AF_16x          0x0c	/* Add frame when AGC reaches 16x gain */
				/* AEC max step control */
#define AEC_NO_LIMIT    0x01	/*   0 : AEC incease step has limit */
				/*   1 : No limit to AEC increase step */
/* CLKRC */
				/* Input clock divider register */
#define CLKRC_RESERVED  0x80	/* Reserved bit */
#define CLKRC_DIV(n)    ((n) - 1)

/* COM7 */
				/* SCCB Register Reset */
#define SCCB_RESET      0x80	/*   0 : No change */
				/*   1 : Resets all registers to default */
				/* Resolution selection */
#define SLCT_MASK       0x40	/*   Mask of VGA or QVGA */
#define SLCT_VGA        0x00	/*   0 : VGA */
#define SLCT_QVGA       0x40	/*   1 : QVGA */
#define ITU656_ON_OFF   0x20	/* ITU656 protocol ON/OFF selection */
#define SENSOR_RAW	0x10	/* Sensor RAW */
				/* RGB output format control */
#define FMT_MASK        0x0c	/*      Mask of color format */
#define FMT_GBR422      0x00	/*      00 : GBR 4:2:2 */
#define FMT_RGB565      0x04	/*      01 : RGB 565 */
#define FMT_RGB555      0x08	/*      10 : RGB 555 */
#define FMT_RGB444      0x0c	/* 11 : RGB 444 */
				/* Output format control */
#define OFMT_MASK       0x03    /*      Mask of output format */
#define OFMT_YUV        0x00	/*      00 : YUV */
#define OFMT_P_BRAW     0x01	/*      01 : Processed Bayer RAW */
#define OFMT_RGB        0x02	/*      10 : RGB */
#define OFMT_BRAW       0x03	/* 11 : Bayer RAW */

/* COM8 */
#define FAST_ALGO       0x80	/* Enable fast AGC/AEC algorithm */
				/* AEC Setp size limit */
#define UNLMT_STEP      0x40	/*   0 : Step size is limited */
				/*   1 : Unlimited step size */
#define BNDF_ON_OFF     0x20	/* Banding filter ON/OFF */
#define AEC_BND         0x10	/* Enable AEC below banding value */
#define AEC_ON_OFF      0x08	/* Fine AEC ON/OFF control */
#define AGC_ON          0x04	/* AGC Enable */
#define AWB_ON          0x02	/* AWB Enable */
#define AEC_ON          0x01	/* AEC Enable */

/* COM9 */
#define BASE_AECAGC     0x80	/* Histogram or average based AEC/AGC */
				/* Automatic gain ceiling - maximum AGC value */
#define GAIN_2x         0x00	/*    000 :   2x */
#define GAIN_4x         0x10	/*    001 :   4x */
#define GAIN_8x         0x20	/*    010 :   8x */
#define GAIN_16x        0x30	/*    011 :  16x */
#define GAIN_32x        0x40	/*    100 :  32x */
#define GAIN_64x        0x50	/* 101 :  64x */
#define GAIN_128x       0x60	/* 110 : 128x */
#define DROP_VSYNC      0x04	/* Drop VSYNC output of corrupt frame */
#define DROP_HREF       0x02	/* Drop HREF  output of corrupt frame */

/* COM11 */
#define SGLF_ON_OFF     0x02	/* Single frame ON/OFF selection */
#define SGLF_TRIG       0x01	/* Single frame transfer trigger */

/* HREF */
#define HREF_VSTART_SHIFT	6	/* VSTART LSB */
#define HREF_HSTART_SHIFT	4	/* HSTART 2 LSBs */
#define HREF_VSIZE_SHIFT	2	/* VSIZE LSB */
#define HREF_HSIZE_SHIFT	0	/* HSIZE 2 LSBs */

/* EXHCH */
#define EXHCH_VSIZE_SHIFT	2	/* VOUTSIZE LSB */
#define EXHCH_HSIZE_SHIFT	0	/* HOUTSIZE 2 LSBs */

/* DSP_CTRL1 */
#define FIFO_ON         0x80	/* FIFO enable/disable selection */
#define UV_ON_OFF       0x40	/* UV adjust function ON/OFF selection */
#define YUV444_2_422    0x20	/* YUV444 to 422 UV channel option selection */
#define CLR_MTRX_ON_OFF 0x10	/* Color matrix ON/OFF selection */
#define INTPLT_ON_OFF   0x08	/* Interpolation ON/OFF selection */
#define GMM_ON_OFF      0x04	/* Gamma function ON/OFF selection */
#define AUTO_BLK_ON_OFF 0x02	/* Black defect auto correction ON/OFF */
#define AUTO_WHT_ON_OFF 0x01	/* White define auto correction ON/OFF */

/* DSP_CTRL3 */
#define UV_MASK         0x80	/* UV output sequence option */
#define UV_ON           0x80	/*   ON */
#define UV_OFF          0x00	/*   OFF */
#define CBAR_MASK       0x20	/* DSP Color bar mask */
#define CBAR_ON         0x20	/*   ON */
#define CBAR_OFF        0x00	/*   OFF */

/* DSP_CTRL4 */
#define DSP_OFMT_YUV	0x00
#define DSP_OFMT_RGB	0x00
#define DSP_OFMT_RAW8	0x02
#define DSP_OFMT_RAW10	0x03

/* DSPAUTO (DSP Auto Function ON/OFF Control) */
#define AWB_ACTRL       0x80 /* AWB auto threshold control */
#define DENOISE_ACTRL   0x40 /* De-noise auto threshold control */
#define EDGE_ACTRL      0x20 /* Edge enhancement auto strength control */
#define UV_ACTRL        0x10 /* UV adjust auto slope control */
#define SCAL0_ACTRL     0x08 /* Auto scaling factor control */
#define SCAL1_2_ACTRL   0x04 /* Auto scaling factor control */

#define OV772X_MAX_WIDTH	VGA_WIDTH
#define OV772X_MAX_HEIGHT	VGA_HEIGHT

/*
 * ID
 */
#define OV7720  0x7720
#define OV7725  0x7721
#define VERSION(pid, ver) ((pid << 8) | (ver & 0xFF))

/*
 * PLL multipliers
 */
static struct {
	unsigned int mult;
	u8 com4;
} ov772x_pll[] = {
	{ 1, PLL_BYPASS, },
	{ 4, PLL_4x, },
	{ 6, PLL_6x, },
	{ 8, PLL_8x, },
};

/*
 * struct
 */

struct ov772x_color_format {
	u32 code;
	enum v4l2_colorspace colorspace;
	u8 dsp3;
	u8 dsp4;
	u8 com3;
	u8 com7;
};

struct ov772x_win_size {
	char                     *name;
	unsigned char             com7_bit;
	unsigned int		  sizeimage;
	struct v4l2_rect	  rect;
};

struct ov772x_priv {
	struct v4l2_subdev                subdev;
	struct v4l2_ctrl_handler	  hdl;
	struct clk			 *clk;
	struct regmap			 *regmap;
	struct ov772x_camera_info        *info;
	struct gpio_desc		 *pwdn_gpio;
	struct gpio_desc		 *rstb_gpio;
	const struct ov772x_color_format *cfmt;
	const struct ov772x_win_size     *win;
	struct v4l2_ctrl		 *vflip_ctrl;
	struct v4l2_ctrl		 *hflip_ctrl;
	unsigned int			  test_pattern;
	/* band_filter = COM8[5] ? 256 - BDBASE : 0 */
	struct v4l2_ctrl		 *band_filter_ctrl;
	unsigned int			  fps;
	/* lock to protect power_count and streaming */
	struct mutex			  lock;
	int				  power_count;
	int				  streaming;
	struct media_pad pad;
	enum v4l2_mbus_type		  bus_type;
};

/*
 * supported color format list
 */
static const struct ov772x_color_format ov772x_cfmts[] = {
	{
		.code		= MEDIA_BUS_FMT_YUYV8_2X8,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.dsp3		= 0x0,
		.dsp4		= DSP_OFMT_YUV,
		.com3		= SWAP_YUV,
		.com7		= OFMT_YUV,
	},
	{
		.code		= MEDIA_BUS_FMT_YVYU8_2X8,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.dsp3		= UV_ON,
		.dsp4		= DSP_OFMT_YUV,
		.com3		= SWAP_YUV,
		.com7		= OFMT_YUV,
	},
	{
		.code		= MEDIA_BUS_FMT_UYVY8_2X8,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.dsp3		= 0x0,
		.dsp4		= DSP_OFMT_YUV,
		.com3		= 0x0,
		.com7		= OFMT_YUV,
	},
	{
		.code		= MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.dsp3		= 0x0,
		.dsp4		= DSP_OFMT_YUV,
		.com3		= SWAP_RGB,
		.com7		= FMT_RGB555 | OFMT_RGB,
	},
	{
		.code		= MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.dsp3		= 0x0,
		.dsp4		= DSP_OFMT_YUV,
		.com3		= 0x0,
		.com7		= FMT_RGB555 | OFMT_RGB,
	},
	{
		.code		= MEDIA_BUS_FMT_RGB565_2X8_LE,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.dsp3		= 0x0,
		.dsp4		= DSP_OFMT_YUV,
		.com3		= SWAP_RGB,
		.com7		= FMT_RGB565 | OFMT_RGB,
	},
	{
		.code		= MEDIA_BUS_FMT_RGB565_2X8_BE,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.dsp3		= 0x0,
		.dsp4		= DSP_OFMT_YUV,
		.com3		= 0x0,
		.com7		= FMT_RGB565 | OFMT_RGB,
	},
	{
		/* Setting DSP4 to DSP_OFMT_RAW8 still gives 10-bit output,
		 * regardless of the COM7 value. We can thus only support 10-bit
		 * Bayer until someone figures it out.
		 */
		.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.dsp3		= 0x0,
		.dsp4		= DSP_OFMT_RAW10,
		.com3		= 0x0,
		.com7		= SENSOR_RAW | OFMT_BRAW,
	},
};

/*
 * window size list
 */

static const struct ov772x_win_size ov772x_win_sizes[] = {
	{
		.name		= "VGA",
		.com7_bit	= SLCT_VGA,
		.sizeimage	= 510 * 748,
		.rect = {
			.left	= 140,
			.top	= 14,
			.width	= VGA_WIDTH,
			.height	= VGA_HEIGHT,
		},
	}, {
		.name		= "QVGA",
		.com7_bit	= SLCT_QVGA,
		.sizeimage	= 278 * 576,
		.rect = {
			.left	= 252,
			.top	= 6,
			.width	= QVGA_WIDTH,
			.height	= QVGA_HEIGHT,
		},
	},
};

static const char * const ov772x_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
};

/*
 * frame rate settings lists
 */
static const unsigned int ov772x_frame_intervals[] = { 5, 10, 15, 20, 30, 60 };

/*
 * general function
 */

static struct ov772x_priv *to_ov772x(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov772x_priv, subdev);
}

static int ov772x_reset(struct ov772x_priv *priv)
{
	int ret;

	ret = regmap_write(priv->regmap, COM7, SCCB_RESET);
	if (ret < 0)
		return ret;

	usleep_range(1000, 5000);

	return regmap_update_bits(priv->regmap, COM2, SOFT_SLEEP_MODE,
				  SOFT_SLEEP_MODE);
}

/*
 * subdev ops
 */

static int ov772x_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov772x_priv *priv = to_ov772x(sd);
	int ret = 0;

	mutex_lock(&priv->lock);

	if (priv->streaming == enable)
		goto done;

	if (priv->bus_type == V4L2_MBUS_BT656) {
		ret = regmap_update_bits(priv->regmap, COM7, ITU656_ON_OFF,
					 enable ?
					 ITU656_ON_OFF : ~ITU656_ON_OFF);
		if (ret)
			goto done;
	}

	ret = regmap_update_bits(priv->regmap, COM2, SOFT_SLEEP_MODE,
				 enable ? 0 : SOFT_SLEEP_MODE);
	if (ret)
		goto done;

	if (enable) {
		dev_dbg(&client->dev, "format %d, win %s\n",
			priv->cfmt->code, priv->win->name);
	}
	priv->streaming = enable;

done:
	mutex_unlock(&priv->lock);

	return ret;
}

static unsigned int ov772x_select_fps(struct ov772x_priv *priv,
				      struct v4l2_fract *tpf)
{
	unsigned int fps = tpf->numerator ?
			   tpf->denominator / tpf->numerator :
			   tpf->denominator;
	unsigned int best_diff;
	unsigned int diff;
	unsigned int idx;
	unsigned int i;

	/* Approximate to the closest supported frame interval. */
	best_diff = ~0L;
	for (i = 0, idx = 0; i < ARRAY_SIZE(ov772x_frame_intervals); i++) {
		diff = abs(fps - ov772x_frame_intervals[i]);
		if (diff < best_diff) {
			idx = i;
			best_diff = diff;
		}
	}

	return ov772x_frame_intervals[idx];
}

static int ov772x_set_frame_rate(struct ov772x_priv *priv,
				 unsigned int fps,
				 const struct ov772x_color_format *cfmt,
				 const struct ov772x_win_size *win)
{
	unsigned long fin = clk_get_rate(priv->clk);
	unsigned int best_diff;
	unsigned int fsize;
	unsigned int pclk;
	unsigned int diff;
	unsigned int i;
	u8 clkrc = 0;
	u8 com4 = 0;
	int ret;

	/* Use image size (with blankings) to calculate desired pixel clock. */
	switch (cfmt->com7 & OFMT_MASK) {
	case OFMT_BRAW:
		fsize = win->sizeimage;
		break;
	case OFMT_RGB:
	case OFMT_YUV:
	default:
		fsize = win->sizeimage * 2;
		break;
	}

	pclk = fps * fsize;

	/*
	 * Pixel clock generation circuit is pretty simple:
	 *
	 * Fin -> [ / CLKRC_div] -> [ * PLL_mult] -> pclk
	 *
	 * Try to approximate the desired pixel clock testing all available
	 * PLL multipliers (1x, 4x, 6x, 8x) and calculate corresponding
	 * divisor with:
	 *
	 * div = PLL_mult * Fin / pclk
	 *
	 * and re-calculate the pixel clock using it:
	 *
	 * pclk = Fin * PLL_mult / CLKRC_div
	 *
	 * Choose the PLL_mult and CLKRC_div pair that gives a pixel clock
	 * closer to the desired one.
	 *
	 * The desired pixel clock is calculated using a known frame size
	 * (blanking included) and FPS.
	 */
	best_diff = ~0L;
	for (i = 0; i < ARRAY_SIZE(ov772x_pll); i++) {
		unsigned int pll_mult = ov772x_pll[i].mult;
		unsigned int pll_out = pll_mult * fin;
		unsigned int t_pclk;
		unsigned int div;

		if (pll_out < pclk)
			continue;

		div = DIV_ROUND_CLOSEST(pll_out, pclk);
		t_pclk = DIV_ROUND_CLOSEST(fin * pll_mult, div);
		diff = abs(pclk - t_pclk);
		if (diff < best_diff) {
			best_diff = diff;
			clkrc = CLKRC_DIV(div);
			com4 = ov772x_pll[i].com4;
		}
	}

	ret = regmap_write(priv->regmap, COM4, com4 | COM4_RESERVED);
	if (ret < 0)
		return ret;

	ret = regmap_write(priv->regmap, CLKRC, clkrc | CLKRC_RESERVED);
	if (ret < 0)
		return ret;

	return 0;
}

static int ov772x_get_frame_interval(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_frame_interval *ival)
{
	struct ov772x_priv *priv = to_ov772x(sd);
	struct v4l2_fract *tpf = &ival->interval;

	/*
	 * FIXME: Implement support for V4L2_SUBDEV_FORMAT_TRY, using the V4L2
	 * subdev active state API.
	 */
	if (ival->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	tpf->numerator = 1;
	tpf->denominator = priv->fps;

	return 0;
}

static int ov772x_set_frame_interval(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_frame_interval *ival)
{
	struct ov772x_priv *priv = to_ov772x(sd);
	struct v4l2_fract *tpf = &ival->interval;
	unsigned int fps;
	int ret = 0;

	/*
	 * FIXME: Implement support for V4L2_SUBDEV_FORMAT_TRY, using the V4L2
	 * subdev active state API.
	 */
	if (ival->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	mutex_lock(&priv->lock);

	if (priv->streaming) {
		ret = -EBUSY;
		goto error;
	}

	fps = ov772x_select_fps(priv, tpf);

	/*
	 * If the device is not powered up by the host driver do
	 * not apply any changes to H/W at this time. Instead
	 * the frame rate will be restored right after power-up.
	 */
	if (priv->power_count > 0) {
		ret = ov772x_set_frame_rate(priv, fps, priv->cfmt, priv->win);
		if (ret)
			goto error;
	}

	tpf->numerator = 1;
	tpf->denominator = fps;
	priv->fps = fps;

error:
	mutex_unlock(&priv->lock);

	return ret;
}

static int ov772x_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov772x_priv *priv = container_of(ctrl->handler,
						struct ov772x_priv, hdl);
	struct regmap *regmap = priv->regmap;
	int ret = 0;
	u8 val;

	/* v4l2_ctrl_lock() locks our own mutex */

	/*
	 * If the device is not powered up by the host driver do
	 * not apply any controls to H/W at this time. Instead
	 * the controls will be restored right after power-up.
	 */
	if (priv->power_count == 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		val = ctrl->val ? VFLIP_IMG : 0x00;
		if (priv->info && (priv->info->flags & OV772X_FLAG_VFLIP))
			val ^= VFLIP_IMG;
		return regmap_update_bits(regmap, COM3, VFLIP_IMG, val);
	case V4L2_CID_HFLIP:
		val = ctrl->val ? HFLIP_IMG : 0x00;
		if (priv->info && (priv->info->flags & OV772X_FLAG_HFLIP))
			val ^= HFLIP_IMG;
		return regmap_update_bits(regmap, COM3, HFLIP_IMG, val);
	case V4L2_CID_BAND_STOP_FILTER:
		if (!ctrl->val) {
			/* Switch the filter off, it is on now */
			ret = regmap_update_bits(regmap, BDBASE, 0xff, 0xff);
			if (!ret)
				ret = regmap_update_bits(regmap, COM8,
							 BNDF_ON_OFF, 0);
		} else {
			/* Switch the filter on, set AEC low limit */
			val = 256 - ctrl->val;
			ret = regmap_update_bits(regmap, COM8,
						 BNDF_ON_OFF, BNDF_ON_OFF);
			if (!ret)
				ret = regmap_update_bits(regmap, BDBASE,
							 0xff, val);
		}

		return ret;
	case V4L2_CID_TEST_PATTERN:
		priv->test_pattern = ctrl->val;
		return 0;
	}

	return -EINVAL;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov772x_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct ov772x_priv *priv = to_ov772x(sd);
	int ret;
	unsigned int val;

	reg->size = 1;
	if (reg->reg > 0xff)
		return -EINVAL;

	ret = regmap_read(priv->regmap, reg->reg, &val);
	if (ret < 0)
		return ret;

	reg->val = (__u64)val;

	return 0;
}

static int ov772x_s_register(struct v4l2_subdev *sd,
			     const struct v4l2_dbg_register *reg)
{
	struct ov772x_priv *priv = to_ov772x(sd);

	if (reg->reg > 0xff ||
	    reg->val > 0xff)
		return -EINVAL;

	return regmap_write(priv->regmap, reg->reg, reg->val);
}
#endif

static int ov772x_power_on(struct ov772x_priv *priv)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->subdev);
	int ret;

	if (priv->clk) {
		ret = clk_prepare_enable(priv->clk);
		if (ret)
			return ret;
	}

	if (priv->pwdn_gpio) {
		gpiod_set_value(priv->pwdn_gpio, 1);
		usleep_range(500, 1000);
	}

	/*
	 * FIXME: The reset signal is connected to a shared GPIO on some
	 * platforms (namely the SuperH Migo-R). Until a framework becomes
	 * available to handle this cleanly, request the GPIO temporarily
	 * to avoid conflicts.
	 */
	priv->rstb_gpio = gpiod_get_optional(&client->dev, "reset",
					     GPIOD_OUT_LOW);
	if (IS_ERR(priv->rstb_gpio)) {
		dev_info(&client->dev, "Unable to get GPIO \"reset\"");
		clk_disable_unprepare(priv->clk);
		return PTR_ERR(priv->rstb_gpio);
	}

	if (priv->rstb_gpio) {
		gpiod_set_value(priv->rstb_gpio, 1);
		usleep_range(500, 1000);
		gpiod_set_value(priv->rstb_gpio, 0);
		usleep_range(500, 1000);

		gpiod_put(priv->rstb_gpio);
	}

	return 0;
}

static int ov772x_power_off(struct ov772x_priv *priv)
{
	clk_disable_unprepare(priv->clk);

	if (priv->pwdn_gpio) {
		gpiod_set_value(priv->pwdn_gpio, 0);
		usleep_range(500, 1000);
	}

	return 0;
}

static int ov772x_set_params(struct ov772x_priv *priv,
			     const struct ov772x_color_format *cfmt,
			     const struct ov772x_win_size *win);

static int ov772x_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov772x_priv *priv = to_ov772x(sd);
	int ret = 0;

	mutex_lock(&priv->lock);

	/* If the power count is modified from 0 to != 0 or from != 0 to 0,
	 * update the power state.
	 */
	if (priv->power_count == !on) {
		if (on) {
			ret = ov772x_power_on(priv);
			/*
			 * Restore the format, the frame rate, and
			 * the controls
			 */
			if (!ret)
				ret = ov772x_set_params(priv, priv->cfmt,
							priv->win);
		} else {
			ret = ov772x_power_off(priv);
		}
	}

	if (!ret) {
		/* Update the power count. */
		priv->power_count += on ? 1 : -1;
		WARN(priv->power_count < 0, "Unbalanced power count\n");
		WARN(priv->power_count > 1, "Duplicated s_power call\n");
	}

	mutex_unlock(&priv->lock);

	return ret;
}

static const struct ov772x_win_size *ov772x_select_win(u32 width, u32 height)
{
	const struct ov772x_win_size *win = &ov772x_win_sizes[0];
	u32 best_diff = UINT_MAX;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ov772x_win_sizes); ++i) {
		u32 diff = abs(width - ov772x_win_sizes[i].rect.width)
			 + abs(height - ov772x_win_sizes[i].rect.height);
		if (diff < best_diff) {
			best_diff = diff;
			win = &ov772x_win_sizes[i];
		}
	}

	return win;
}

static void ov772x_select_params(const struct v4l2_mbus_framefmt *mf,
				 const struct ov772x_color_format **cfmt,
				 const struct ov772x_win_size **win)
{
	unsigned int i;

	/* Select a format. */
	*cfmt = &ov772x_cfmts[0];

	for (i = 0; i < ARRAY_SIZE(ov772x_cfmts); i++) {
		if (mf->code == ov772x_cfmts[i].code) {
			*cfmt = &ov772x_cfmts[i];
			break;
		}
	}

	/* Select a window size. */
	*win = ov772x_select_win(mf->width, mf->height);
}

static int ov772x_edgectrl(struct ov772x_priv *priv)
{
	struct regmap *regmap = priv->regmap;
	int ret;

	if (!priv->info)
		return 0;

	if (priv->info->edgectrl.strength & OV772X_MANUAL_EDGE_CTRL) {
		/*
		 * Manual Edge Control Mode.
		 *
		 * Edge auto strength bit is set by default.
		 * Remove it when manual mode.
		 */

		ret = regmap_update_bits(regmap, DSPAUTO, EDGE_ACTRL, 0x00);
		if (ret < 0)
			return ret;

		ret = regmap_update_bits(regmap, EDGE_TRSHLD,
					 OV772X_EDGE_THRESHOLD_MASK,
					 priv->info->edgectrl.threshold);
		if (ret < 0)
			return ret;

		ret = regmap_update_bits(regmap, EDGE_STRNGT,
					 OV772X_EDGE_STRENGTH_MASK,
					 priv->info->edgectrl.strength);
		if (ret < 0)
			return ret;

	} else if (priv->info->edgectrl.upper > priv->info->edgectrl.lower) {
		/*
		 * Auto Edge Control Mode.
		 *
		 * Set upper and lower limit.
		 */
		ret = regmap_update_bits(regmap, EDGE_UPPER,
					 OV772X_EDGE_UPPER_MASK,
					 priv->info->edgectrl.upper);
		if (ret < 0)
			return ret;

		ret = regmap_update_bits(regmap, EDGE_LOWER,
					 OV772X_EDGE_LOWER_MASK,
					 priv->info->edgectrl.lower);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int ov772x_set_params(struct ov772x_priv *priv,
			     const struct ov772x_color_format *cfmt,
			     const struct ov772x_win_size *win)
{
	int ret;
	u8  val;

	/* Reset hardware. */
	ov772x_reset(priv);

	/* Edge Ctrl. */
	ret = ov772x_edgectrl(priv);
	if (ret < 0)
		return ret;

	/* Format and window size. */
	ret = regmap_write(priv->regmap, HSTART, win->rect.left >> 2);
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = regmap_write(priv->regmap, HSIZE, win->rect.width >> 2);
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = regmap_write(priv->regmap, VSTART, win->rect.top >> 1);
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = regmap_write(priv->regmap, VSIZE, win->rect.height >> 1);
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = regmap_write(priv->regmap, HOUTSIZE, win->rect.width >> 2);
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = regmap_write(priv->regmap, VOUTSIZE, win->rect.height >> 1);
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = regmap_write(priv->regmap, HREF,
			   ((win->rect.top & 1) << HREF_VSTART_SHIFT) |
			   ((win->rect.left & 3) << HREF_HSTART_SHIFT) |
			   ((win->rect.height & 1) << HREF_VSIZE_SHIFT) |
			   ((win->rect.width & 3) << HREF_HSIZE_SHIFT));
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = regmap_write(priv->regmap, EXHCH,
			   ((win->rect.height & 1) << EXHCH_VSIZE_SHIFT) |
			   ((win->rect.width & 3) << EXHCH_HSIZE_SHIFT));
	if (ret < 0)
		goto ov772x_set_fmt_error;

	/* Set DSP_CTRL3. */
	val = cfmt->dsp3;
	if (val) {
		ret = regmap_update_bits(priv->regmap, DSP_CTRL3, UV_MASK, val);
		if (ret < 0)
			goto ov772x_set_fmt_error;
	}

	/* DSP_CTRL4: AEC reference point and DSP output format. */
	if (cfmt->dsp4) {
		ret = regmap_write(priv->regmap, DSP_CTRL4, cfmt->dsp4);
		if (ret < 0)
			goto ov772x_set_fmt_error;
	}

	/* Set COM3. */
	val = cfmt->com3;
	if (priv->info && (priv->info->flags & OV772X_FLAG_VFLIP))
		val |= VFLIP_IMG;
	if (priv->info && (priv->info->flags & OV772X_FLAG_HFLIP))
		val |= HFLIP_IMG;
	if (priv->vflip_ctrl->val)
		val ^= VFLIP_IMG;
	if (priv->hflip_ctrl->val)
		val ^= HFLIP_IMG;
	if (priv->test_pattern)
		val |= SCOLOR_TEST;

	ret = regmap_update_bits(priv->regmap, COM3, SWAP_MASK | IMG_MASK, val);
	if (ret < 0)
		goto ov772x_set_fmt_error;

	/* COM7: Sensor resolution and output format control. */
	ret = regmap_write(priv->regmap, COM7, win->com7_bit | cfmt->com7);
	if (ret < 0)
		goto ov772x_set_fmt_error;

	/* COM4, CLKRC: Set pixel clock and framerate. */
	ret = ov772x_set_frame_rate(priv, priv->fps, cfmt, win);
	if (ret < 0)
		goto ov772x_set_fmt_error;

	/* Set COM8. */
	if (priv->band_filter_ctrl->val) {
		unsigned short band_filter = priv->band_filter_ctrl->val;

		ret = regmap_update_bits(priv->regmap, COM8,
					 BNDF_ON_OFF, BNDF_ON_OFF);
		if (!ret)
			ret = regmap_update_bits(priv->regmap, BDBASE,
						 0xff, 256 - band_filter);
		if (ret < 0)
			goto ov772x_set_fmt_error;
	}

	return ret;

ov772x_set_fmt_error:

	ov772x_reset(priv);

	return ret;
}

static int ov772x_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct ov772x_priv *priv = to_ov772x(sd);

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	sel->r.left = 0;
	sel->r.top = 0;
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP:
		sel->r.width = priv->win->rect.width;
		sel->r.height = priv->win->rect.height;
		return 0;
	default:
		return -EINVAL;
	}
}

static int ov772x_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct ov772x_priv *priv = to_ov772x(sd);

	if (format->pad)
		return -EINVAL;

	mf->width	= priv->win->rect.width;
	mf->height	= priv->win->rect.height;
	mf->code	= priv->cfmt->code;
	mf->colorspace	= priv->cfmt->colorspace;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int ov772x_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct ov772x_priv *priv = to_ov772x(sd);
	struct v4l2_mbus_framefmt *mf = &format->format;
	const struct ov772x_color_format *cfmt;
	const struct ov772x_win_size *win;
	int ret = 0;

	if (format->pad)
		return -EINVAL;

	ov772x_select_params(mf, &cfmt, &win);

	mf->code = cfmt->code;
	mf->width = win->rect.width;
	mf->height = win->rect.height;
	mf->field = V4L2_FIELD_NONE;
	mf->colorspace = cfmt->colorspace;
	mf->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	mf->quantization = V4L2_QUANTIZATION_DEFAULT;
	mf->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_state_get_format(sd_state, 0) = *mf;
		return 0;
	}

	mutex_lock(&priv->lock);

	if (priv->streaming) {
		ret = -EBUSY;
		goto error;
	}

	/*
	 * If the device is not powered up by the host driver do
	 * not apply any changes to H/W at this time. Instead
	 * the format will be restored right after power-up.
	 */
	if (priv->power_count > 0) {
		ret = ov772x_set_params(priv, cfmt, win);
		if (ret < 0)
			goto error;
	}
	priv->win = win;
	priv->cfmt = cfmt;

error:
	mutex_unlock(&priv->lock);

	return ret;
}

static int ov772x_video_probe(struct ov772x_priv *priv)
{
	struct i2c_client  *client = v4l2_get_subdevdata(&priv->subdev);
	int		    pid, ver, midh, midl;
	const char         *devname;
	int		    ret;

	ret = ov772x_power_on(priv);
	if (ret < 0)
		return ret;

	/* Check and show product ID and manufacturer ID. */
	ret = regmap_read(priv->regmap, PID, &pid);
	if (ret < 0)
		return ret;
	ret = regmap_read(priv->regmap, VER, &ver);
	if (ret < 0)
		return ret;

	switch (VERSION(pid, ver)) {
	case OV7720:
		devname     = "ov7720";
		break;
	case OV7725:
		devname     = "ov7725";
		break;
	default:
		dev_err(&client->dev,
			"Product ID error %x:%x\n", pid, ver);
		ret = -ENODEV;
		goto done;
	}

	ret = regmap_read(priv->regmap, MIDH, &midh);
	if (ret < 0)
		return ret;
	ret = regmap_read(priv->regmap, MIDL, &midl);
	if (ret < 0)
		return ret;

	dev_info(&client->dev,
		 "%s Product ID %0x:%0x Manufacturer ID %x:%x\n",
		 devname, pid, ver, midh, midl);

	ret = v4l2_ctrl_handler_setup(&priv->hdl);

done:
	ov772x_power_off(priv);

	return ret;
}

static const struct v4l2_ctrl_ops ov772x_ctrl_ops = {
	.s_ctrl = ov772x_s_ctrl,
};

static const struct v4l2_subdev_core_ops ov772x_subdev_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= ov772x_g_register,
	.s_register	= ov772x_s_register,
#endif
	.s_power	= ov772x_s_power,
};

static int ov772x_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *sd_state,
				      struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->pad || fie->index >= ARRAY_SIZE(ov772x_frame_intervals))
		return -EINVAL;

	if (fie->width != VGA_WIDTH && fie->width != QVGA_WIDTH)
		return -EINVAL;
	if (fie->height != VGA_HEIGHT && fie->height != QVGA_HEIGHT)
		return -EINVAL;

	fie->interval.numerator = 1;
	fie->interval.denominator = ov772x_frame_intervals[fie->index];

	return 0;
}

static int ov772x_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(ov772x_cfmts))
		return -EINVAL;

	code->code = ov772x_cfmts[code->index].code;

	return 0;
}

static const struct v4l2_subdev_video_ops ov772x_subdev_video_ops = {
	.s_stream		= ov772x_s_stream,
};

static const struct v4l2_subdev_pad_ops ov772x_subdev_pad_ops = {
	.enum_frame_interval	= ov772x_enum_frame_interval,
	.enum_mbus_code		= ov772x_enum_mbus_code,
	.get_selection		= ov772x_get_selection,
	.get_fmt		= ov772x_get_fmt,
	.set_fmt		= ov772x_set_fmt,
	.get_frame_interval	= ov772x_get_frame_interval,
	.set_frame_interval	= ov772x_set_frame_interval,
};

static const struct v4l2_subdev_ops ov772x_subdev_ops = {
	.core	= &ov772x_subdev_core_ops,
	.video	= &ov772x_subdev_video_ops,
	.pad	= &ov772x_subdev_pad_ops,
};

static int ov772x_parse_dt(struct i2c_client *client,
			   struct ov772x_priv *priv)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_PARALLEL
	};
	struct fwnode_handle *ep;
	int ret;

	ep = fwnode_graph_get_next_endpoint(dev_fwnode(&client->dev), NULL);
	if (!ep) {
		dev_err(&client->dev, "Endpoint node not found\n");
		return -EINVAL;
	}

	/*
	 * For backward compatibility with older DTS where the
	 * bus-type property was not mandatory, assume
	 * V4L2_MBUS_PARALLEL as it was the only supported bus at the
	 * time. v4l2_fwnode_endpoint_alloc_parse() will not fail if
	 * 'bus-type' is not specified.
	 */
	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	if (ret) {
		bus_cfg = (struct v4l2_fwnode_endpoint)
			  { .bus_type = V4L2_MBUS_BT656 };
		ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
		if (ret)
			goto error_fwnode_put;
	}

	priv->bus_type = bus_cfg.bus_type;
	v4l2_fwnode_endpoint_free(&bus_cfg);

error_fwnode_put:
	fwnode_handle_put(ep);

	return ret;
}

/*
 * i2c_driver function
 */

static int ov772x_probe(struct i2c_client *client)
{
	struct ov772x_priv	*priv;
	int			ret;
	static const struct regmap_config ov772x_regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = DSPAUTO,
	};

	if (!client->dev.of_node && !client->dev.platform_data) {
		dev_err(&client->dev,
			"Missing ov772x platform data for non-DT device\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = devm_regmap_init_sccb(client, &ov772x_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev, "Failed to allocate register map\n");
		return PTR_ERR(priv->regmap);
	}

	priv->info = client->dev.platform_data;
	mutex_init(&priv->lock);

	v4l2_i2c_subdev_init(&priv->subdev, client, &ov772x_subdev_ops);
	priv->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			      V4L2_SUBDEV_FL_HAS_EVENTS;
	v4l2_ctrl_handler_init(&priv->hdl, 3);
	/* Use our mutex for the controls */
	priv->hdl.lock = &priv->lock;
	priv->vflip_ctrl = v4l2_ctrl_new_std(&priv->hdl, &ov772x_ctrl_ops,
					     V4L2_CID_VFLIP, 0, 1, 1, 0);
	priv->hflip_ctrl = v4l2_ctrl_new_std(&priv->hdl, &ov772x_ctrl_ops,
					     V4L2_CID_HFLIP, 0, 1, 1, 0);
	priv->band_filter_ctrl = v4l2_ctrl_new_std(&priv->hdl, &ov772x_ctrl_ops,
						   V4L2_CID_BAND_STOP_FILTER,
						   0, 256, 1, 0);
	v4l2_ctrl_new_std_menu_items(&priv->hdl, &ov772x_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov772x_test_pattern_menu) - 1,
				     0, 0, ov772x_test_pattern_menu);
	priv->subdev.ctrl_handler = &priv->hdl;
	if (priv->hdl.error) {
		ret = priv->hdl.error;
		goto error_ctrl_free;
	}

	priv->clk = clk_get(&client->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&client->dev, "Unable to get xclk clock\n");
		ret = PTR_ERR(priv->clk);
		goto error_ctrl_free;
	}

	priv->pwdn_gpio = gpiod_get_optional(&client->dev, "powerdown",
					     GPIOD_OUT_LOW);
	if (IS_ERR(priv->pwdn_gpio)) {
		dev_info(&client->dev, "Unable to get GPIO \"powerdown\"");
		ret = PTR_ERR(priv->pwdn_gpio);
		goto error_clk_put;
	}

	ret = ov772x_parse_dt(client, priv);
	if (ret)
		goto error_clk_put;

	ret = ov772x_video_probe(priv);
	if (ret < 0)
		goto error_gpio_put;

	priv->pad.flags = MEDIA_PAD_FL_SOURCE;
	priv->subdev.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&priv->subdev.entity, 1, &priv->pad);
	if (ret < 0)
		goto error_gpio_put;

	priv->cfmt = &ov772x_cfmts[0];
	priv->win = &ov772x_win_sizes[0];
	priv->fps = 15;

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret)
		goto error_entity_cleanup;

	return 0;

error_entity_cleanup:
	media_entity_cleanup(&priv->subdev.entity);
error_gpio_put:
	if (priv->pwdn_gpio)
		gpiod_put(priv->pwdn_gpio);
error_clk_put:
	clk_put(priv->clk);
error_ctrl_free:
	v4l2_ctrl_handler_free(&priv->hdl);
	mutex_destroy(&priv->lock);

	return ret;
}

static void ov772x_remove(struct i2c_client *client)
{
	struct ov772x_priv *priv = to_ov772x(i2c_get_clientdata(client));

	media_entity_cleanup(&priv->subdev.entity);
	clk_put(priv->clk);
	if (priv->pwdn_gpio)
		gpiod_put(priv->pwdn_gpio);
	v4l2_async_unregister_subdev(&priv->subdev);
	v4l2_ctrl_handler_free(&priv->hdl);
	mutex_destroy(&priv->lock);
}

static const struct i2c_device_id ov772x_id[] = {
	{ "ov772x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov772x_id);

static const struct of_device_id ov772x_of_match[] = {
	{ .compatible = "ovti,ov7725", },
	{ .compatible = "ovti,ov7720", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ov772x_of_match);

static struct i2c_driver ov772x_i2c_driver = {
	.driver = {
		.name = "ov772x",
		.of_match_table = ov772x_of_match,
	},
	.probe    = ov772x_probe,
	.remove   = ov772x_remove,
	.id_table = ov772x_id,
};

module_i2c_driver(ov772x_i2c_driver);

MODULE_DESCRIPTION("V4L2 driver for OV772x image sensor");
MODULE_AUTHOR("Kuninori Morimoto");
MODULE_LICENSE("GPL v2");
