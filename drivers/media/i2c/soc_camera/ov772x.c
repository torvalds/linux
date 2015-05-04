/*
 * ov772x Camera Driver
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * Based on ov7670 and soc_camera_platform driver,
 *
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
 * Copyright (C) 2008 Magnus Damm
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>

#include <media/i2c/ov772x.h>
#include <media/soc_camera.h>
#include <media/v4l2-clk.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-image-sizes.h>

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
#define IMG_MASK        (VFLIP_IMG | HFLIP_IMG)

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
#define VERSION(pid, ver) ((pid<<8)|(ver&0xFF))

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
	struct v4l2_rect	  rect;
};

struct ov772x_priv {
	struct v4l2_subdev                subdev;
	struct v4l2_ctrl_handler	  hdl;
	struct v4l2_clk			 *clk;
	struct ov772x_camera_info        *info;
	const struct ov772x_color_format *cfmt;
	const struct ov772x_win_size     *win;
	unsigned short                    flag_vflip:1;
	unsigned short                    flag_hflip:1;
	/* band_filter = COM8[5] ? 256 - BDBASE : 0 */
	unsigned short                    band_filter;
};

/*
 * supported color format list
 */
static const struct ov772x_color_format ov772x_cfmts[] = {
	{
		.code		= MEDIA_BUS_FMT_YUYV8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.dsp3		= 0x0,
		.dsp4		= DSP_OFMT_YUV,
		.com3		= SWAP_YUV,
		.com7		= OFMT_YUV,
	},
	{
		.code		= MEDIA_BUS_FMT_YVYU8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
		.dsp3		= UV_ON,
		.dsp4		= DSP_OFMT_YUV,
		.com3		= SWAP_YUV,
		.com7		= OFMT_YUV,
	},
	{
		.code		= MEDIA_BUS_FMT_UYVY8_2X8,
		.colorspace	= V4L2_COLORSPACE_JPEG,
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
		.name     = "VGA",
		.com7_bit = SLCT_VGA,
		.rect = {
			.left = 140,
			.top = 14,
			.width = VGA_WIDTH,
			.height = VGA_HEIGHT,
		},
	}, {
		.name     = "QVGA",
		.com7_bit = SLCT_QVGA,
		.rect = {
			.left = 252,
			.top = 6,
			.width = QVGA_WIDTH,
			.height = QVGA_HEIGHT,
		},
	},
};

/*
 * general function
 */

static struct ov772x_priv *to_ov772x(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov772x_priv, subdev);
}

static inline int ov772x_read(struct i2c_client *client, u8 addr)
{
	return i2c_smbus_read_byte_data(client, addr);
}

static inline int ov772x_write(struct i2c_client *client, u8 addr, u8 value)
{
	return i2c_smbus_write_byte_data(client, addr, value);
}

static int ov772x_mask_set(struct i2c_client *client, u8  command, u8  mask,
			   u8  set)
{
	s32 val = ov772x_read(client, command);
	if (val < 0)
		return val;

	val &= ~mask;
	val |= set & mask;

	return ov772x_write(client, command, val);
}

static int ov772x_reset(struct i2c_client *client)
{
	int ret;

	ret = ov772x_write(client, COM7, SCCB_RESET);
	if (ret < 0)
		return ret;

	msleep(1);

	return ov772x_mask_set(client, COM2, SOFT_SLEEP_MODE, SOFT_SLEEP_MODE);
}

/*
 * soc_camera_ops function
 */

static int ov772x_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov772x_priv *priv = to_ov772x(sd);

	if (!enable) {
		ov772x_mask_set(client, COM2, SOFT_SLEEP_MODE, SOFT_SLEEP_MODE);
		return 0;
	}

	ov772x_mask_set(client, COM2, SOFT_SLEEP_MODE, 0);

	dev_dbg(&client->dev, "format %d, win %s\n",
		priv->cfmt->code, priv->win->name);

	return 0;
}

static int ov772x_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov772x_priv *priv = container_of(ctrl->handler,
						struct ov772x_priv, hdl);
	struct v4l2_subdev *sd = &priv->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;
	u8 val;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		val = ctrl->val ? VFLIP_IMG : 0x00;
		priv->flag_vflip = ctrl->val;
		if (priv->info->flags & OV772X_FLAG_VFLIP)
			val ^= VFLIP_IMG;
		return ov772x_mask_set(client, COM3, VFLIP_IMG, val);
	case V4L2_CID_HFLIP:
		val = ctrl->val ? HFLIP_IMG : 0x00;
		priv->flag_hflip = ctrl->val;
		if (priv->info->flags & OV772X_FLAG_HFLIP)
			val ^= HFLIP_IMG;
		return ov772x_mask_set(client, COM3, HFLIP_IMG, val);
	case V4L2_CID_BAND_STOP_FILTER:
		if (!ctrl->val) {
			/* Switch the filter off, it is on now */
			ret = ov772x_mask_set(client, BDBASE, 0xff, 0xff);
			if (!ret)
				ret = ov772x_mask_set(client, COM8,
						      BNDF_ON_OFF, 0);
		} else {
			/* Switch the filter on, set AEC low limit */
			val = 256 - ctrl->val;
			ret = ov772x_mask_set(client, COM8,
					      BNDF_ON_OFF, BNDF_ON_OFF);
			if (!ret)
				ret = ov772x_mask_set(client, BDBASE,
						      0xff, val);
		}
		if (!ret)
			priv->band_filter = ctrl->val;
		return ret;
	}

	return -EINVAL;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov772x_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	reg->size = 1;
	if (reg->reg > 0xff)
		return -EINVAL;

	ret = ov772x_read(client, reg->reg);
	if (ret < 0)
		return ret;

	reg->val = (__u64)ret;

	return 0;
}

static int ov772x_s_register(struct v4l2_subdev *sd,
			     const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg > 0xff ||
	    reg->val > 0xff)
		return -EINVAL;

	return ov772x_write(client, reg->reg, reg->val);
}
#endif

static int ov772x_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	struct ov772x_priv *priv = to_ov772x(sd);

	return soc_camera_set_power(&client->dev, ssdd, priv->clk, on);
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

static int ov772x_set_params(struct ov772x_priv *priv,
			     const struct ov772x_color_format *cfmt,
			     const struct ov772x_win_size *win)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->subdev);
	int ret;
	u8  val;

	/*
	 * reset hardware
	 */
	ov772x_reset(client);

	/*
	 * Edge Ctrl
	 */
	if (priv->info->edgectrl.strength & OV772X_MANUAL_EDGE_CTRL) {

		/*
		 * Manual Edge Control Mode
		 *
		 * Edge auto strength bit is set by default.
		 * Remove it when manual mode.
		 */

		ret = ov772x_mask_set(client, DSPAUTO, EDGE_ACTRL, 0x00);
		if (ret < 0)
			goto ov772x_set_fmt_error;

		ret = ov772x_mask_set(client,
				      EDGE_TRSHLD, OV772X_EDGE_THRESHOLD_MASK,
				      priv->info->edgectrl.threshold);
		if (ret < 0)
			goto ov772x_set_fmt_error;

		ret = ov772x_mask_set(client,
				      EDGE_STRNGT, OV772X_EDGE_STRENGTH_MASK,
				      priv->info->edgectrl.strength);
		if (ret < 0)
			goto ov772x_set_fmt_error;

	} else if (priv->info->edgectrl.upper > priv->info->edgectrl.lower) {
		/*
		 * Auto Edge Control Mode
		 *
		 * set upper and lower limit
		 */
		ret = ov772x_mask_set(client,
				      EDGE_UPPER, OV772X_EDGE_UPPER_MASK,
				      priv->info->edgectrl.upper);
		if (ret < 0)
			goto ov772x_set_fmt_error;

		ret = ov772x_mask_set(client,
				      EDGE_LOWER, OV772X_EDGE_LOWER_MASK,
				      priv->info->edgectrl.lower);
		if (ret < 0)
			goto ov772x_set_fmt_error;
	}

	/* Format and window size */
	ret = ov772x_write(client, HSTART, win->rect.left >> 2);
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = ov772x_write(client, HSIZE, win->rect.width >> 2);
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = ov772x_write(client, VSTART, win->rect.top >> 1);
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = ov772x_write(client, VSIZE, win->rect.height >> 1);
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = ov772x_write(client, HOUTSIZE, win->rect.width >> 2);
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = ov772x_write(client, VOUTSIZE, win->rect.height >> 1);
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = ov772x_write(client, HREF,
			   ((win->rect.top & 1) << HREF_VSTART_SHIFT) |
			   ((win->rect.left & 3) << HREF_HSTART_SHIFT) |
			   ((win->rect.height & 1) << HREF_VSIZE_SHIFT) |
			   ((win->rect.width & 3) << HREF_HSIZE_SHIFT));
	if (ret < 0)
		goto ov772x_set_fmt_error;
	ret = ov772x_write(client, EXHCH,
			   ((win->rect.height & 1) << EXHCH_VSIZE_SHIFT) |
			   ((win->rect.width & 3) << EXHCH_HSIZE_SHIFT));
	if (ret < 0)
		goto ov772x_set_fmt_error;

	/*
	 * set DSP_CTRL3
	 */
	val = cfmt->dsp3;
	if (val) {
		ret = ov772x_mask_set(client,
				      DSP_CTRL3, UV_MASK, val);
		if (ret < 0)
			goto ov772x_set_fmt_error;
	}

	/* DSP_CTRL4: AEC reference point and DSP output format. */
	if (cfmt->dsp4) {
		ret = ov772x_write(client, DSP_CTRL4, cfmt->dsp4);
		if (ret < 0)
			goto ov772x_set_fmt_error;
	}

	/*
	 * set COM3
	 */
	val = cfmt->com3;
	if (priv->info->flags & OV772X_FLAG_VFLIP)
		val |= VFLIP_IMG;
	if (priv->info->flags & OV772X_FLAG_HFLIP)
		val |= HFLIP_IMG;
	if (priv->flag_vflip)
		val ^= VFLIP_IMG;
	if (priv->flag_hflip)
		val ^= HFLIP_IMG;

	ret = ov772x_mask_set(client,
			      COM3, SWAP_MASK | IMG_MASK, val);
	if (ret < 0)
		goto ov772x_set_fmt_error;

	/* COM7: Sensor resolution and output format control. */
	ret = ov772x_write(client, COM7, win->com7_bit | cfmt->com7);
	if (ret < 0)
		goto ov772x_set_fmt_error;

	/*
	 * set COM8
	 */
	if (priv->band_filter) {
		ret = ov772x_mask_set(client, COM8, BNDF_ON_OFF, 1);
		if (!ret)
			ret = ov772x_mask_set(client, BDBASE,
					      0xff, 256 - priv->band_filter);
		if (ret < 0)
			goto ov772x_set_fmt_error;
	}

	return ret;

ov772x_set_fmt_error:

	ov772x_reset(client);

	return ret;
}

static int ov772x_get_selection(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_selection *sel)
{
	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	sel->r.left = 0;
	sel->r.top = 0;
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.width = OV772X_MAX_WIDTH;
		sel->r.height = OV772X_MAX_HEIGHT;
		return 0;
	case V4L2_SEL_TGT_CROP:
		sel->r.width = VGA_WIDTH;
		sel->r.height = VGA_HEIGHT;
		return 0;
	default:
		return -EINVAL;
	}
}

static int ov772x_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
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
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct ov772x_priv *priv = to_ov772x(sd);
	struct v4l2_mbus_framefmt *mf = &format->format;
	const struct ov772x_color_format *cfmt;
	const struct ov772x_win_size *win;
	int ret;

	if (format->pad)
		return -EINVAL;

	ov772x_select_params(mf, &cfmt, &win);

	mf->code = cfmt->code;
	mf->width = win->rect.width;
	mf->height = win->rect.height;
	mf->field = V4L2_FIELD_NONE;
	mf->colorspace = cfmt->colorspace;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		cfg->try_fmt = *mf;
		return 0;
	}

	ret = ov772x_set_params(priv, cfmt, win);
	if (ret < 0)
		return ret;

	priv->win = win;
	priv->cfmt = cfmt;
	return 0;
}

static int ov772x_video_probe(struct ov772x_priv *priv)
{
	struct i2c_client  *client = v4l2_get_subdevdata(&priv->subdev);
	u8                  pid, ver;
	const char         *devname;
	int		    ret;

	ret = ov772x_s_power(&priv->subdev, 1);
	if (ret < 0)
		return ret;

	/*
	 * check and show product ID and manufacturer ID
	 */
	pid = ov772x_read(client, PID);
	ver = ov772x_read(client, VER);

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

	dev_info(&client->dev,
		 "%s Product ID %0x:%0x Manufacturer ID %x:%x\n",
		 devname,
		 pid,
		 ver,
		 ov772x_read(client, MIDH),
		 ov772x_read(client, MIDL));
	ret = v4l2_ctrl_handler_setup(&priv->hdl);

done:
	ov772x_s_power(&priv->subdev, 0);
	return ret;
}

static const struct v4l2_ctrl_ops ov772x_ctrl_ops = {
	.s_ctrl = ov772x_s_ctrl,
};

static const struct v4l2_subdev_core_ops ov772x_subdev_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= ov772x_g_register,
	.s_register	= ov772x_s_register,
#endif
	.s_power	= ov772x_s_power,
};

static int ov772x_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(ov772x_cfmts))
		return -EINVAL;

	code->code = ov772x_cfmts[code->index].code;
	return 0;
}

static int ov772x_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);

	cfg->flags = V4L2_MBUS_PCLK_SAMPLE_RISING | V4L2_MBUS_MASTER |
		V4L2_MBUS_VSYNC_ACTIVE_HIGH | V4L2_MBUS_HSYNC_ACTIVE_HIGH |
		V4L2_MBUS_DATA_ACTIVE_HIGH;
	cfg->type = V4L2_MBUS_PARALLEL;
	cfg->flags = soc_camera_apply_board_flags(ssdd, cfg);

	return 0;
}

static const struct v4l2_subdev_video_ops ov772x_subdev_video_ops = {
	.s_stream	= ov772x_s_stream,
	.g_mbus_config	= ov772x_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops ov772x_subdev_pad_ops = {
	.enum_mbus_code = ov772x_enum_mbus_code,
	.get_selection	= ov772x_get_selection,
	.get_fmt	= ov772x_get_fmt,
	.set_fmt	= ov772x_set_fmt,
};

static const struct v4l2_subdev_ops ov772x_subdev_ops = {
	.core	= &ov772x_subdev_core_ops,
	.video	= &ov772x_subdev_video_ops,
	.pad	= &ov772x_subdev_pad_ops,
};

/*
 * i2c_driver function
 */

static int ov772x_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct ov772x_priv	*priv;
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int			ret;

	if (!ssdd || !ssdd->drv_priv) {
		dev_err(&client->dev, "OV772X: missing platform data!\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
			"I2C-Adapter doesn't support I2C_FUNC_SMBUS_BYTE_DATA\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->info = ssdd->drv_priv;

	v4l2_i2c_subdev_init(&priv->subdev, client, &ov772x_subdev_ops);
	v4l2_ctrl_handler_init(&priv->hdl, 3);
	v4l2_ctrl_new_std(&priv->hdl, &ov772x_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &ov772x_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &ov772x_ctrl_ops,
			V4L2_CID_BAND_STOP_FILTER, 0, 256, 1, 0);
	priv->subdev.ctrl_handler = &priv->hdl;
	if (priv->hdl.error)
		return priv->hdl.error;

	priv->clk = v4l2_clk_get(&client->dev, "mclk");
	if (IS_ERR(priv->clk)) {
		ret = PTR_ERR(priv->clk);
		goto eclkget;
	}

	ret = ov772x_video_probe(priv);
	if (ret < 0) {
		v4l2_clk_put(priv->clk);
eclkget:
		v4l2_ctrl_handler_free(&priv->hdl);
	} else {
		priv->cfmt = &ov772x_cfmts[0];
		priv->win = &ov772x_win_sizes[0];
	}

	return ret;
}

static int ov772x_remove(struct i2c_client *client)
{
	struct ov772x_priv *priv = to_ov772x(i2c_get_clientdata(client));

	v4l2_clk_put(priv->clk);
	v4l2_device_unregister_subdev(&priv->subdev);
	v4l2_ctrl_handler_free(&priv->hdl);
	return 0;
}

static const struct i2c_device_id ov772x_id[] = {
	{ "ov772x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov772x_id);

static struct i2c_driver ov772x_i2c_driver = {
	.driver = {
		.name = "ov772x",
	},
	.probe    = ov772x_probe,
	.remove   = ov772x_remove,
	.id_table = ov772x_id,
};

module_i2c_driver(ov772x_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for ov772x");
MODULE_AUTHOR("Kuninori Morimoto");
MODULE_LICENSE("GPL v2");
