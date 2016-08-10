/*
 * SoC-camera host driver for Renesas R-Car VIN unit
 *
 * Copyright (C) 2011-2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Cogent Embedded, Inc., <source@cogentembedded.com>
 *
 * Based on V4L2 Driver for SuperH Mobile CEU interface "sh_mobile_ceu_camera.c"
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/soc_camera.h>
#include <media/drv-intf/soc_mediabus.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-of.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include "soc_scale_crop.h"

#define DRV_NAME "rcar_vin"

/* Register offsets for R-Car VIN */
#define VNMC_REG	0x00	/* Video n Main Control Register */
#define VNMS_REG	0x04	/* Video n Module Status Register */
#define VNFC_REG	0x08	/* Video n Frame Capture Register */
#define VNSLPRC_REG	0x0C	/* Video n Start Line Pre-Clip Register */
#define VNELPRC_REG	0x10	/* Video n End Line Pre-Clip Register */
#define VNSPPRC_REG	0x14	/* Video n Start Pixel Pre-Clip Register */
#define VNEPPRC_REG	0x18	/* Video n End Pixel Pre-Clip Register */
#define VNSLPOC_REG	0x1C	/* Video n Start Line Post-Clip Register */
#define VNELPOC_REG	0x20	/* Video n End Line Post-Clip Register */
#define VNSPPOC_REG	0x24	/* Video n Start Pixel Post-Clip Register */
#define VNEPPOC_REG	0x28	/* Video n End Pixel Post-Clip Register */
#define VNIS_REG	0x2C	/* Video n Image Stride Register */
#define VNMB_REG(m)	(0x30 + ((m) << 2)) /* Video n Memory Base m Register */
#define VNIE_REG	0x40	/* Video n Interrupt Enable Register */
#define VNINTS_REG	0x44	/* Video n Interrupt Status Register */
#define VNSI_REG	0x48	/* Video n Scanline Interrupt Register */
#define VNMTC_REG	0x4C	/* Video n Memory Transfer Control Register */
#define VNYS_REG	0x50	/* Video n Y Scale Register */
#define VNXS_REG	0x54	/* Video n X Scale Register */
#define VNDMR_REG	0x58	/* Video n Data Mode Register */
#define VNDMR2_REG	0x5C	/* Video n Data Mode Register 2 */
#define VNUVAOF_REG	0x60	/* Video n UV Address Offset Register */
#define VNC1A_REG	0x80	/* Video n Coefficient Set C1A Register */
#define VNC1B_REG	0x84	/* Video n Coefficient Set C1B Register */
#define VNC1C_REG	0x88	/* Video n Coefficient Set C1C Register */
#define VNC2A_REG	0x90	/* Video n Coefficient Set C2A Register */
#define VNC2B_REG	0x94	/* Video n Coefficient Set C2B Register */
#define VNC2C_REG	0x98	/* Video n Coefficient Set C2C Register */
#define VNC3A_REG	0xA0	/* Video n Coefficient Set C3A Register */
#define VNC3B_REG	0xA4	/* Video n Coefficient Set C3B Register */
#define VNC3C_REG	0xA8	/* Video n Coefficient Set C3C Register */
#define VNC4A_REG	0xB0	/* Video n Coefficient Set C4A Register */
#define VNC4B_REG	0xB4	/* Video n Coefficient Set C4B Register */
#define VNC4C_REG	0xB8	/* Video n Coefficient Set C4C Register */
#define VNC5A_REG	0xC0	/* Video n Coefficient Set C5A Register */
#define VNC5B_REG	0xC4	/* Video n Coefficient Set C5B Register */
#define VNC5C_REG	0xC8	/* Video n Coefficient Set C5C Register */
#define VNC6A_REG	0xD0	/* Video n Coefficient Set C6A Register */
#define VNC6B_REG	0xD4	/* Video n Coefficient Set C6B Register */
#define VNC6C_REG	0xD8	/* Video n Coefficient Set C6C Register */
#define VNC7A_REG	0xE0	/* Video n Coefficient Set C7A Register */
#define VNC7B_REG	0xE4	/* Video n Coefficient Set C7B Register */
#define VNC7C_REG	0xE8	/* Video n Coefficient Set C7C Register */
#define VNC8A_REG	0xF0	/* Video n Coefficient Set C8A Register */
#define VNC8B_REG	0xF4	/* Video n Coefficient Set C8B Register */
#define VNC8C_REG	0xF8	/* Video n Coefficient Set C8C Register */

/* Register bit fields for R-Car VIN */
/* Video n Main Control Register bits */
#define VNMC_FOC		(1 << 21)
#define VNMC_YCAL		(1 << 19)
#define VNMC_INF_YUV8_BT656	(0 << 16)
#define VNMC_INF_YUV8_BT601	(1 << 16)
#define VNMC_INF_YUV10_BT656	(2 << 16)
#define VNMC_INF_YUV10_BT601	(3 << 16)
#define VNMC_INF_YUV16		(5 << 16)
#define VNMC_INF_RGB888		(6 << 16)
#define VNMC_VUP		(1 << 10)
#define VNMC_IM_ODD		(0 << 3)
#define VNMC_IM_ODD_EVEN	(1 << 3)
#define VNMC_IM_EVEN		(2 << 3)
#define VNMC_IM_FULL		(3 << 3)
#define VNMC_BPS		(1 << 1)
#define VNMC_ME			(1 << 0)

/* Video n Module Status Register bits */
#define VNMS_FBS_MASK		(3 << 3)
#define VNMS_FBS_SHIFT		3
#define VNMS_AV			(1 << 1)
#define VNMS_CA			(1 << 0)

/* Video n Frame Capture Register bits */
#define VNFC_C_FRAME		(1 << 1)
#define VNFC_S_FRAME		(1 << 0)

/* Video n Interrupt Enable Register bits */
#define VNIE_FIE		(1 << 4)
#define VNIE_EFE		(1 << 1)

/* Video n Data Mode Register bits */
#define VNDMR_EXRGB		(1 << 8)
#define VNDMR_BPSM		(1 << 4)
#define VNDMR_DTMD_YCSEP	(1 << 1)
#define VNDMR_DTMD_ARGB		(1 << 0)

/* Video n Data Mode Register 2 bits */
#define VNDMR2_VPS		(1 << 30)
#define VNDMR2_HPS		(1 << 29)
#define VNDMR2_FTEV		(1 << 17)
#define VNDMR2_VLV(n)		((n & 0xf) << 12)

#define VIN_MAX_WIDTH		2048
#define VIN_MAX_HEIGHT		2048

#define TIMEOUT_MS		100

#define RCAR_VIN_HSYNC_ACTIVE_LOW	(1 << 0)
#define RCAR_VIN_VSYNC_ACTIVE_LOW	(1 << 1)
#define RCAR_VIN_BT601			(1 << 2)
#define RCAR_VIN_BT656			(1 << 3)

enum chip_id {
	RCAR_GEN3,
	RCAR_GEN2,
	RCAR_H1,
	RCAR_M1,
	RCAR_E1,
};

struct vin_coeff {
	unsigned short xs_value;
	u32 coeff_set[24];
};

static const struct vin_coeff vin_coeff_set[] = {
	{ 0x0000, {
		0x00000000,		0x00000000,		0x00000000,
		0x00000000,		0x00000000,		0x00000000,
		0x00000000,		0x00000000,		0x00000000,
		0x00000000,		0x00000000,		0x00000000,
		0x00000000,		0x00000000,		0x00000000,
		0x00000000,		0x00000000,		0x00000000,
		0x00000000,		0x00000000,		0x00000000,
		0x00000000,		0x00000000,		0x00000000 },
	},
	{ 0x1000, {
		0x000fa400,		0x000fa400,		0x09625902,
		0x000003f8,		0x00000403,		0x3de0d9f0,
		0x001fffed,		0x00000804,		0x3cc1f9c3,
		0x001003de,		0x00000c01,		0x3cb34d7f,
		0x002003d2,		0x00000c00,		0x3d24a92d,
		0x00200bca,		0x00000bff,		0x3df600d2,
		0x002013cc,		0x000007ff,		0x3ed70c7e,
		0x00100fde,		0x00000000,		0x3f87c036 },
	},
	{ 0x1200, {
		0x002ffff1,		0x002ffff1,		0x02a0a9c8,
		0x002003e7,		0x001ffffa,		0x000185bc,
		0x002007dc,		0x000003ff,		0x3e52859c,
		0x00200bd4,		0x00000002,		0x3d53996b,
		0x00100fd0,		0x00000403,		0x3d04ad2d,
		0x00000bd5,		0x00000403,		0x3d35ace7,
		0x3ff003e4,		0x00000801,		0x3dc674a1,
		0x3fffe800,		0x00000800,		0x3e76f461 },
	},
	{ 0x1400, {
		0x00100be3,		0x00100be3,		0x04d1359a,
		0x00000fdb,		0x002003ed,		0x0211fd93,
		0x00000fd6,		0x002003f4,		0x0002d97b,
		0x000007d6,		0x002ffffb,		0x3e93b956,
		0x3ff003da,		0x001003ff,		0x3db49926,
		0x3fffefe9,		0x00100001,		0x3d655cee,
		0x3fffd400,		0x00000003,		0x3d65f4b6,
		0x000fb421,		0x00000402,		0x3dc6547e },
	},
	{ 0x1600, {
		0x00000bdd,		0x00000bdd,		0x06519578,
		0x3ff007da,		0x00000be3,		0x03c24973,
		0x3ff003d9,		0x00000be9,		0x01b30d5f,
		0x3ffff7df,		0x001003f1,		0x0003c542,
		0x000fdfec,		0x001003f7,		0x3ec4711d,
		0x000fc400,		0x002ffffd,		0x3df504f1,
		0x001fa81a,		0x002ffc00,		0x3d957cc2,
		0x002f8c3c,		0x00100000,		0x3db5c891 },
	},
	{ 0x1800, {
		0x3ff003dc,		0x3ff003dc,		0x0791e558,
		0x000ff7dd,		0x3ff007de,		0x05328554,
		0x000fe7e3,		0x3ff00be2,		0x03232546,
		0x000fd7ee,		0x000007e9,		0x0143bd30,
		0x001fb800,		0x000007ee,		0x00044511,
		0x002fa015,		0x000007f4,		0x3ef4bcee,
		0x002f8832,		0x001003f9,		0x3e4514c7,
		0x001f7853,		0x001003fd,		0x3de54c9f },
	},
	{ 0x1a00, {
		0x000fefe0,		0x000fefe0,		0x08721d3c,
		0x001fdbe7,		0x000ffbde,		0x0652a139,
		0x001fcbf0,		0x000003df,		0x0463292e,
		0x002fb3ff,		0x3ff007e3,		0x0293a91d,
		0x002f9c12,		0x3ff00be7,		0x01241905,
		0x001f8c29,		0x000007ed,		0x3fe470eb,
		0x000f7c46,		0x000007f2,		0x3f04b8ca,
		0x3fef7865,		0x000007f6,		0x3e74e4a8 },
	},
	{ 0x1c00, {
		0x001fd3e9,		0x001fd3e9,		0x08f23d26,
		0x002fbff3,		0x001fe3e4,		0x0712ad23,
		0x002fa800,		0x000ff3e0,		0x05631d1b,
		0x001f9810,		0x000ffbe1,		0x03b3890d,
		0x000f8c23,		0x000003e3,		0x0233e8fa,
		0x3fef843b,		0x000003e7,		0x00f430e4,
		0x3fbf8456,		0x3ff00bea,		0x00046cc8,
		0x3f8f8c72,		0x3ff00bef,		0x3f3490ac },
	},
	{ 0x1e00, {
		0x001fbbf4,		0x001fbbf4,		0x09425112,
		0x001fa800,		0x002fc7ed,		0x0792b110,
		0x000f980e,		0x001fdbe6,		0x0613110a,
		0x3fff8c20,		0x001fe7e3,		0x04a368fd,
		0x3fcf8c33,		0x000ff7e2,		0x0343b8ed,
		0x3f9f8c4a,		0x000fffe3,		0x0203f8da,
		0x3f5f9c61,		0x000003e6,		0x00e428c5,
		0x3f1fb07b,		0x000003eb,		0x3fe440af },
	},
	{ 0x2000, {
		0x000fa400,		0x000fa400,		0x09625902,
		0x3fff980c,		0x001fb7f5,		0x0812b0ff,
		0x3fdf901c,		0x001fc7ed,		0x06b2fcfa,
		0x3faf902d,		0x001fd3e8,		0x055348f1,
		0x3f7f983f,		0x001fe3e5,		0x04038ce3,
		0x3f3fa454,		0x001fefe3,		0x02e3c8d1,
		0x3f0fb86a,		0x001ff7e4,		0x01c3e8c0,
		0x3ecfd880,		0x000fffe6,		0x00c404ac },
	},
	{ 0x2200, {
		0x3fdf9c0b,		0x3fdf9c0b,		0x09725cf4,
		0x3fbf9818,		0x3fffa400,		0x0842a8f1,
		0x3f8f9827,		0x000fb3f7,		0x0702f0ec,
		0x3f5fa037,		0x000fc3ef,		0x05d330e4,
		0x3f2fac49,		0x001fcfea,		0x04a364d9,
		0x3effc05c,		0x001fdbe7,		0x038394ca,
		0x3ecfdc6f,		0x001fe7e6,		0x0273b0bb,
		0x3ea00083,		0x001fefe6,		0x0183c0a9 },
	},
	{ 0x2400, {
		0x3f9fa014,		0x3f9fa014,		0x098260e6,
		0x3f7f9c23,		0x3fcf9c0a,		0x08629ce5,
		0x3f4fa431,		0x3fefa400,		0x0742d8e1,
		0x3f1fb440,		0x3fffb3f8,		0x062310d9,
		0x3eefc850,		0x000fbbf2,		0x050340d0,
		0x3ecfe062,		0x000fcbec,		0x041364c2,
		0x3ea00073,		0x001fd3ea,		0x03037cb5,
		0x3e902086,		0x001fdfe8,		0x022388a5 },
	},
	{ 0x2600, {
		0x3f5fa81e,		0x3f5fa81e,		0x096258da,
		0x3f3fac2b,		0x3f8fa412,		0x088290d8,
		0x3f0fbc38,		0x3fafa408,		0x0772c8d5,
		0x3eefcc47,		0x3fcfa800,		0x0672f4ce,
		0x3ecfe456,		0x3fefaffa,		0x05531cc6,
		0x3eb00066,		0x3fffbbf3,		0x047334bb,
		0x3ea01c77,		0x000fc7ee,		0x039348ae,
		0x3ea04486,		0x000fd3eb,		0x02b350a1 },
	},
	{ 0x2800, {
		0x3f2fb426,		0x3f2fb426,		0x094250ce,
		0x3f0fc032,		0x3f4fac1b,		0x086284cd,
		0x3eefd040,		0x3f7fa811,		0x0782acc9,
		0x3ecfe84c,		0x3f9fa807,		0x06a2d8c4,
		0x3eb0005b,		0x3fbfac00,		0x05b2f4bc,
		0x3eb0186a,		0x3fdfb3fa,		0x04c308b4,
		0x3eb04077,		0x3fefbbf4,		0x03f31ca8,
		0x3ec06884,		0x000fbff2,		0x03031c9e },
	},
	{ 0x2a00, {
		0x3f0fc42d,		0x3f0fc42d,		0x090240c4,
		0x3eefd439,		0x3f2fb822,		0x08526cc2,
		0x3edfe845,		0x3f4fb018,		0x078294bf,
		0x3ec00051,		0x3f6fac0f,		0x06b2b4bb,
		0x3ec0185f,		0x3f8fac07,		0x05e2ccb4,
		0x3ec0386b,		0x3fafac00,		0x0502e8ac,
		0x3ed05c77,		0x3fcfb3fb,		0x0432f0a3,
		0x3ef08482,		0x3fdfbbf6,		0x0372f898 },
	},
	{ 0x2c00, {
		0x3eefdc31,		0x3eefdc31,		0x08e238b8,
		0x3edfec3d,		0x3f0fc828,		0x082258b9,
		0x3ed00049,		0x3f1fc01e,		0x077278b6,
		0x3ed01455,		0x3f3fb815,		0x06c294b2,
		0x3ed03460,		0x3f5fb40d,		0x0602acac,
		0x3ef0506c,		0x3f7fb006,		0x0542c0a4,
		0x3f107476,		0x3f9fb400,		0x0472c89d,
		0x3f309c80,		0x3fbfb7fc,		0x03b2cc94 },
	},
	{ 0x2e00, {
		0x3eefec37,		0x3eefec37,		0x088220b0,
		0x3ee00041,		0x3effdc2d,		0x07f244ae,
		0x3ee0144c,		0x3f0fd023,		0x07625cad,
		0x3ef02c57,		0x3f1fc81a,		0x06c274a9,
		0x3f004861,		0x3f3fbc13,		0x060288a6,
		0x3f20686b,		0x3f5fb80c,		0x05529c9e,
		0x3f408c74,		0x3f6fb805,		0x04b2ac96,
		0x3f80ac7e,		0x3f8fb800,		0x0402ac8e },
	},
	{ 0x3000, {
		0x3ef0003a,		0x3ef0003a,		0x084210a6,
		0x3ef01045,		0x3effec32,		0x07b228a7,
		0x3f00284e,		0x3f0fdc29,		0x073244a4,
		0x3f104058,		0x3f0fd420,		0x06a258a2,
		0x3f305c62,		0x3f2fc818,		0x0612689d,
		0x3f508069,		0x3f3fc011,		0x05728496,
		0x3f80a072,		0x3f4fc00a,		0x04d28c90,
		0x3fc0c07b,		0x3f6fbc04,		0x04429088 },
	},
	{ 0x3200, {
		0x3f00103e,		0x3f00103e,		0x07f1fc9e,
		0x3f102447,		0x3f000035,		0x0782149d,
		0x3f203c4f,		0x3f0ff02c,		0x07122c9c,
		0x3f405458,		0x3f0fe424,		0x06924099,
		0x3f607061,		0x3f1fd41d,		0x06024c97,
		0x3f909068,		0x3f2fcc16,		0x05726490,
		0x3fc0b070,		0x3f3fc80f,		0x04f26c8a,
		0x0000d077,		0x3f4fc409,		0x04627484 },
	},
	{ 0x3400, {
		0x3f202040,		0x3f202040,		0x07a1e898,
		0x3f303449,		0x3f100c38,		0x0741fc98,
		0x3f504c50,		0x3f10002f,		0x06e21495,
		0x3f706459,		0x3f1ff028,		0x06722492,
		0x3fa08060,		0x3f1fe421,		0x05f2348f,
		0x3fd09c67,		0x3f1fdc19,		0x05824c89,
		0x0000bc6e,		0x3f2fd014,		0x04f25086,
		0x0040dc74,		0x3f3fcc0d,		0x04825c7f },
	},
	{ 0x3600, {
		0x3f403042,		0x3f403042,		0x0761d890,
		0x3f504848,		0x3f301c3b,		0x0701f090,
		0x3f805c50,		0x3f200c33,		0x06a2008f,
		0x3fa07458,		0x3f10002b,		0x06520c8d,
		0x3fd0905e,		0x3f1ff424,		0x05e22089,
		0x0000ac65,		0x3f1fe81d,		0x05823483,
		0x0030cc6a,		0x3f2fdc18,		0x04f23c81,
		0x0080e871,		0x3f2fd412,		0x0482407c },
	},
	{ 0x3800, {
		0x3f604043,		0x3f604043,		0x0721c88a,
		0x3f80544a,		0x3f502c3c,		0x06d1d88a,
		0x3fb06851,		0x3f301c35,		0x0681e889,
		0x3fd08456,		0x3f30082f,		0x0611fc88,
		0x00009c5d,		0x3f200027,		0x05d20884,
		0x0030b863,		0x3f2ff421,		0x05621880,
		0x0070d468,		0x3f2fe81b,		0x0502247c,
		0x00c0ec6f,		0x3f2fe015,		0x04a22877 },
	},
	{ 0x3a00, {
		0x3f904c44,		0x3f904c44,		0x06e1b884,
		0x3fb0604a,		0x3f70383e,		0x0691c885,
		0x3fe07451,		0x3f502c36,		0x0661d483,
		0x00009055,		0x3f401831,		0x0601ec81,
		0x0030a85b,		0x3f300c2a,		0x05b1f480,
		0x0070c061,		0x3f300024,		0x0562047a,
		0x00b0d867,		0x3f3ff41e,		0x05020c77,
		0x00f0f46b,		0x3f2fec19,		0x04a21474 },
	},
	{ 0x3c00, {
		0x3fb05c43,		0x3fb05c43,		0x06c1b07e,
		0x3fe06c4b,		0x3f902c3f,		0x0681c081,
		0x0000844f,		0x3f703838,		0x0631cc7d,
		0x00309855,		0x3f602433,		0x05d1d47e,
		0x0060b459,		0x3f50142e,		0x0581e47b,
		0x00a0c85f,		0x3f400828,		0x0531f078,
		0x00e0e064,		0x3f300021,		0x0501fc73,
		0x00b0fc6a,		0x3f3ff41d,		0x04a20873 },
	},
	{ 0x3e00, {
		0x3fe06444,		0x3fe06444,		0x0681a07a,
		0x00007849,		0x3fc0503f,		0x0641b07a,
		0x0020904d,		0x3fa0403a,		0x05f1c07a,
		0x0060a453,		0x3f803034,		0x05c1c878,
		0x0090b858,		0x3f70202f,		0x0571d477,
		0x00d0d05d,		0x3f501829,		0x0531e073,
		0x0110e462,		0x3f500825,		0x04e1e471,
		0x01510065,		0x3f40001f,		0x04a1f06d },
	},
	{ 0x4000, {
		0x00007044,		0x00007044,		0x06519476,
		0x00208448,		0x3fe05c3f,		0x0621a476,
		0x0050984d,		0x3fc04c3a,		0x05e1b075,
		0x0080ac52,		0x3fa03c35,		0x05a1b875,
		0x00c0c056,		0x3f803030,		0x0561c473,
		0x0100d45b,		0x3f70202b,		0x0521d46f,
		0x0140e860,		0x3f601427,		0x04d1d46e,
		0x01810064,		0x3f500822,		0x0491dc6b },
	},
	{ 0x5000, {
		0x0110a442,		0x0110a442,		0x0551545e,
		0x0140b045,		0x00e0983f,		0x0531585f,
		0x0160c047,		0x00c08c3c,		0x0511645e,
		0x0190cc4a,		0x00908039,		0x04f1685f,
		0x01c0dc4c,		0x00707436,		0x04d1705e,
		0x0200e850,		0x00506833,		0x04b1785b,
		0x0230f453,		0x00305c30,		0x0491805a,
		0x02710056,		0x0010542d,		0x04718059 },
	},
	{ 0x6000, {
		0x01c0bc40,		0x01c0bc40,		0x04c13052,
		0x01e0c841,		0x01a0b43d,		0x04c13851,
		0x0210cc44,		0x0180a83c,		0x04a13453,
		0x0230d845,		0x0160a03a,		0x04913c52,
		0x0260e047,		0x01409838,		0x04714052,
		0x0280ec49,		0x01208c37,		0x04514c50,
		0x02b0f44b,		0x01008435,		0x04414c50,
		0x02d1004c,		0x00e07c33,		0x0431544f },
	},
	{ 0x7000, {
		0x0230c83e,		0x0230c83e,		0x04711c4c,
		0x0250d03f,		0x0210c43c,		0x0471204b,
		0x0270d840,		0x0200b83c,		0x0451244b,
		0x0290dc42,		0x01e0b43a,		0x0441244c,
		0x02b0e443,		0x01c0b038,		0x0441284b,
		0x02d0ec44,		0x01b0a438,		0x0421304a,
		0x02f0f445,		0x0190a036,		0x04213449,
		0x0310f847,		0x01709c34,		0x04213848 },
	},
	{ 0x8000, {
		0x0280d03d,		0x0280d03d,		0x04310c48,
		0x02a0d43e,		0x0270c83c,		0x04311047,
		0x02b0dc3e,		0x0250c83a,		0x04311447,
		0x02d0e040,		0x0240c03a,		0x04211446,
		0x02e0e840,		0x0220bc39,		0x04111847,
		0x0300e842,		0x0210b438,		0x04012445,
		0x0310f043,		0x0200b037,		0x04012045,
		0x0330f444,		0x01e0ac36,		0x03f12445 },
	},
	{ 0xefff, {
		0x0340dc3a,		0x0340dc3a,		0x03b0ec40,
		0x0340e03a,		0x0330e039,		0x03c0f03e,
		0x0350e03b,		0x0330dc39,		0x03c0ec3e,
		0x0350e43a,		0x0320dc38,		0x03c0f43e,
		0x0360e43b,		0x0320d839,		0x03b0f03e,
		0x0360e83b,		0x0310d838,		0x03c0fc3b,
		0x0370e83b,		0x0310d439,		0x03a0f83d,
		0x0370e83c,		0x0300d438,		0x03b0fc3c },
	}
};

enum rcar_vin_state {
	STOPPED = 0,
	RUNNING,
	STOPPING,
};

struct rcar_vin_priv {
	void __iomem			*base;
	spinlock_t			lock;
	int				sequence;
	/* State of the VIN module in capturing mode */
	enum rcar_vin_state		state;
	struct soc_camera_host		ici;
	struct list_head		capture;
#define MAX_BUFFER_NUM			3
	struct vb2_v4l2_buffer		*queue_buf[MAX_BUFFER_NUM];
	enum v4l2_field			field;
	unsigned int			pdata_flags;
	unsigned int			vb_count;
	unsigned int			nr_hw_slots;
	bool				request_to_stop;
	struct completion		capture_stop;
	enum chip_id			chip;
};

#define is_continuous_transfer(priv)	(priv->vb_count > MAX_BUFFER_NUM)

struct rcar_vin_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head		list;
};

#define to_buf_list(vb2_buffer)	(&container_of(vb2_buffer, \
						       struct rcar_vin_buffer, \
						       vb)->list)

struct rcar_vin_cam {
	/* VIN offsets within the camera output, before the VIN scaler */
	unsigned int			vin_left;
	unsigned int			vin_top;
	/* Client output, as seen by the VIN */
	unsigned int			width;
	unsigned int			height;
	/* User window from S_FMT */
	unsigned int out_width;
	unsigned int out_height;
	/*
	 * User window from S_CROP / G_CROP, produced by client cropping and
	 * scaling, VIN scaling and VIN cropping, mapped back onto the client
	 * input window
	 */
	struct v4l2_rect		subrect;
	/* Camera cropping rectangle */
	struct v4l2_rect		rect;
	const struct soc_mbus_pixelfmt	*extra_fmt;
};

/*
 * .queue_setup() is called to check whether the driver can accept the requested
 * number of buffers and to fill in plane sizes for the current frame format if
 * required
 */
static int rcar_vin_videobuf_setup(struct vb2_queue *vq,
				   unsigned int *count,
				   unsigned int *num_planes,
				   unsigned int sizes[], struct device *alloc_devs[])
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;

	if (!vq->num_buffers)
		priv->sequence = 0;

	if (!*count)
		*count = 2;
	priv->vb_count = *count;

	/* Number of hardware slots */
	if (is_continuous_transfer(priv))
		priv->nr_hw_slots = MAX_BUFFER_NUM;
	else
		priv->nr_hw_slots = 1;

	if (*num_planes)
		return sizes[0] < icd->sizeimage ? -EINVAL : 0;

	sizes[0] = icd->sizeimage;
	*num_planes = 1;

	dev_dbg(icd->parent, "count=%d, size=%u\n", *count, sizes[0]);

	return 0;
}

static int rcar_vin_setup(struct rcar_vin_priv *priv)
{
	struct soc_camera_device *icd = priv->ici.icd;
	struct rcar_vin_cam *cam = icd->host_priv;
	u32 vnmc, dmr, interrupts;
	bool progressive = false, output_is_yuv = false, input_is_yuv = false;

	switch (priv->field) {
	case V4L2_FIELD_TOP:
		vnmc = VNMC_IM_ODD;
		break;
	case V4L2_FIELD_BOTTOM:
		vnmc = VNMC_IM_EVEN;
		break;
	case V4L2_FIELD_INTERLACED:
	case V4L2_FIELD_INTERLACED_TB:
		vnmc = VNMC_IM_FULL;
		break;
	case V4L2_FIELD_INTERLACED_BT:
		vnmc = VNMC_IM_FULL | VNMC_FOC;
		break;
	case V4L2_FIELD_NONE:
		if (is_continuous_transfer(priv)) {
			vnmc = VNMC_IM_ODD_EVEN;
			progressive = true;
		} else {
			vnmc = VNMC_IM_ODD;
		}
		break;
	default:
		vnmc = VNMC_IM_ODD;
		break;
	}

	/* input interface */
	switch (icd->current_fmt->code) {
	case MEDIA_BUS_FMT_YUYV8_1X16:
		/* BT.601/BT.1358 16bit YCbCr422 */
		vnmc |= VNMC_INF_YUV16;
		input_is_yuv = true;
		break;
	case MEDIA_BUS_FMT_YUYV8_2X8:
		/* BT.656 8bit YCbCr422 or BT.601 8bit YCbCr422 */
		vnmc |= priv->pdata_flags & RCAR_VIN_BT656 ?
			VNMC_INF_YUV8_BT656 : VNMC_INF_YUV8_BT601;
		input_is_yuv = true;
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
		vnmc |= VNMC_INF_RGB888;
		break;
	case MEDIA_BUS_FMT_YUYV10_2X10:
		/* BT.656 10bit YCbCr422 or BT.601 10bit YCbCr422 */
		vnmc |= priv->pdata_flags & RCAR_VIN_BT656 ?
			VNMC_INF_YUV10_BT656 : VNMC_INF_YUV10_BT601;
		input_is_yuv = true;
		break;
	default:
		break;
	}

	/* output format */
	switch (icd->current_fmt->host_fmt->fourcc) {
	case V4L2_PIX_FMT_NV16:
		iowrite32(ALIGN(cam->width * cam->height, 0x80),
			  priv->base + VNUVAOF_REG);
		dmr = VNDMR_DTMD_YCSEP;
		output_is_yuv = true;
		break;
	case V4L2_PIX_FMT_YUYV:
		dmr = VNDMR_BPSM;
		output_is_yuv = true;
		break;
	case V4L2_PIX_FMT_UYVY:
		dmr = 0;
		output_is_yuv = true;
		break;
	case V4L2_PIX_FMT_RGB555X:
		dmr = VNDMR_DTMD_ARGB;
		break;
	case V4L2_PIX_FMT_RGB565:
		dmr = 0;
		break;
	case V4L2_PIX_FMT_RGB32:
		if (priv->chip != RCAR_GEN2 && priv->chip != RCAR_H1 &&
		    priv->chip != RCAR_E1)
			goto e_format;

		dmr = VNDMR_EXRGB;
		break;
	case V4L2_PIX_FMT_ARGB32:
		if (priv->chip != RCAR_GEN3)
			goto e_format;

		dmr = VNDMR_EXRGB | VNDMR_DTMD_ARGB;
		break;
	default:
		goto e_format;
	}

	/* Always update on field change */
	vnmc |= VNMC_VUP;

	/* If input and output use the same colorspace, use bypass mode */
	if (input_is_yuv == output_is_yuv)
		vnmc |= VNMC_BPS;

	/* progressive or interlaced mode */
	interrupts = progressive ? VNIE_FIE : VNIE_EFE;

	/* ack interrupts */
	iowrite32(interrupts, priv->base + VNINTS_REG);
	/* enable interrupts */
	iowrite32(interrupts, priv->base + VNIE_REG);
	/* start capturing */
	iowrite32(dmr, priv->base + VNDMR_REG);
	iowrite32(vnmc | VNMC_ME, priv->base + VNMC_REG);

	return 0;

e_format:
	dev_warn(icd->parent, "Invalid fourcc format (0x%x)\n",
		 icd->current_fmt->host_fmt->fourcc);
	return -EINVAL;
}

static void rcar_vin_capture(struct rcar_vin_priv *priv)
{
	if (is_continuous_transfer(priv))
		/* Continuous Frame Capture Mode */
		iowrite32(VNFC_C_FRAME, priv->base + VNFC_REG);
	else
		/* Single Frame Capture Mode */
		iowrite32(VNFC_S_FRAME, priv->base + VNFC_REG);
}

static void rcar_vin_request_capture_stop(struct rcar_vin_priv *priv)
{
	priv->state = STOPPING;

	/* set continuous & single transfer off */
	iowrite32(0, priv->base + VNFC_REG);
	/* disable capture (release DMA buffer), reset */
	iowrite32(ioread32(priv->base + VNMC_REG) & ~VNMC_ME,
		  priv->base + VNMC_REG);

	/* update the status if stopped already */
	if (!(ioread32(priv->base + VNMS_REG) & VNMS_CA))
		priv->state = STOPPED;
}

static int rcar_vin_get_free_hw_slot(struct rcar_vin_priv *priv)
{
	int slot;

	for (slot = 0; slot < priv->nr_hw_slots; slot++)
		if (priv->queue_buf[slot] == NULL)
			return slot;

	return -1;
}

static int rcar_vin_hw_ready(struct rcar_vin_priv *priv)
{
	/* Ensure all HW slots are filled */
	return rcar_vin_get_free_hw_slot(priv) < 0 ? 1 : 0;
}

/* Moves a buffer from the queue to the HW slots */
static int rcar_vin_fill_hw_slot(struct rcar_vin_priv *priv)
{
	struct vb2_v4l2_buffer *vbuf;
	dma_addr_t phys_addr_top;
	int slot;

	if (list_empty(&priv->capture))
		return 0;

	/* Find a free HW slot */
	slot = rcar_vin_get_free_hw_slot(priv);
	if (slot < 0)
		return 0;

	vbuf = &list_entry(priv->capture.next,
			struct rcar_vin_buffer, list)->vb;
	list_del_init(to_buf_list(vbuf));
	priv->queue_buf[slot] = vbuf;
	phys_addr_top = vb2_dma_contig_plane_dma_addr(&vbuf->vb2_buf, 0);
	iowrite32(phys_addr_top, priv->base + VNMB_REG(slot));

	return 1;
}

static void rcar_vin_videobuf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct soc_camera_device *icd = soc_camera_from_vb2q(vb->vb2_queue);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	unsigned long size;

	size = icd->sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(icd->parent, "Buffer #%d too small (%lu < %lu)\n",
			vb->index, vb2_plane_size(vb, 0), size);
		goto error;
	}

	vb2_set_plane_payload(vb, 0, size);

	dev_dbg(icd->parent, "%s (vb=0x%p) 0x%p %lu\n", __func__,
		vb, vb2_plane_vaddr(vb, 0), vb2_get_plane_payload(vb, 0));

	spin_lock_irq(&priv->lock);

	list_add_tail(to_buf_list(vbuf), &priv->capture);
	rcar_vin_fill_hw_slot(priv);

	/* If we weren't running, and have enough buffers, start capturing! */
	if (priv->state != RUNNING && rcar_vin_hw_ready(priv)) {
		if (rcar_vin_setup(priv)) {
			/* Submit error */
			list_del_init(to_buf_list(vbuf));
			spin_unlock_irq(&priv->lock);
			goto error;
		}
		priv->request_to_stop = false;
		init_completion(&priv->capture_stop);
		priv->state = RUNNING;
		rcar_vin_capture(priv);
	}

	spin_unlock_irq(&priv->lock);

	return;

error:
	vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
}

/*
 * Wait for capture to stop and all in-flight buffers to be finished with by
 * the video hardware. This must be called under &priv->lock
 *
 */
static void rcar_vin_wait_stop_streaming(struct rcar_vin_priv *priv)
{
	while (priv->state != STOPPED) {
		/* issue stop if running */
		if (priv->state == RUNNING)
			rcar_vin_request_capture_stop(priv);

		/* wait until capturing has been stopped */
		if (priv->state == STOPPING) {
			priv->request_to_stop = true;
			spin_unlock_irq(&priv->lock);
			if (!wait_for_completion_timeout(
					&priv->capture_stop,
					msecs_to_jiffies(TIMEOUT_MS)))
				priv->state = STOPPED;
			spin_lock_irq(&priv->lock);
		}
	}
}

static void rcar_vin_stop_streaming(struct vb2_queue *vq)
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	struct list_head *buf_head, *tmp;
	int i;

	spin_lock_irq(&priv->lock);
	rcar_vin_wait_stop_streaming(priv);

	for (i = 0; i < MAX_BUFFER_NUM; i++) {
		if (priv->queue_buf[i]) {
			vb2_buffer_done(&priv->queue_buf[i]->vb2_buf,
					VB2_BUF_STATE_ERROR);
			priv->queue_buf[i] = NULL;
		}
	}

	list_for_each_safe(buf_head, tmp, &priv->capture) {
		vb2_buffer_done(&list_entry(buf_head,
				struct rcar_vin_buffer, list)->vb.vb2_buf,
				VB2_BUF_STATE_ERROR);
		list_del_init(buf_head);
	}
	spin_unlock_irq(&priv->lock);
}

static struct vb2_ops rcar_vin_vb2_ops = {
	.queue_setup	= rcar_vin_videobuf_setup,
	.buf_queue	= rcar_vin_videobuf_queue,
	.stop_streaming	= rcar_vin_stop_streaming,
	.wait_prepare	= vb2_ops_wait_prepare,
	.wait_finish	= vb2_ops_wait_finish,
};

static irqreturn_t rcar_vin_irq(int irq, void *data)
{
	struct rcar_vin_priv *priv = data;
	u32 int_status;
	bool can_run = false, hw_stopped;
	int slot;
	unsigned int handled = 0;

	spin_lock(&priv->lock);

	int_status = ioread32(priv->base + VNINTS_REG);
	if (!int_status)
		goto done;
	/* ack interrupts */
	iowrite32(int_status, priv->base + VNINTS_REG);
	handled = 1;

	/* nothing to do if capture status is 'STOPPED' */
	if (priv->state == STOPPED)
		goto done;

	hw_stopped = !(ioread32(priv->base + VNMS_REG) & VNMS_CA);

	if (!priv->request_to_stop) {
		if (is_continuous_transfer(priv))
			slot = (ioread32(priv->base + VNMS_REG) &
				VNMS_FBS_MASK) >> VNMS_FBS_SHIFT;
		else
			slot = 0;

		priv->queue_buf[slot]->field = priv->field;
		priv->queue_buf[slot]->sequence = priv->sequence++;
		priv->queue_buf[slot]->vb2_buf.timestamp = ktime_get_ns();
		vb2_buffer_done(&priv->queue_buf[slot]->vb2_buf,
				VB2_BUF_STATE_DONE);
		priv->queue_buf[slot] = NULL;

		if (priv->state != STOPPING)
			can_run = rcar_vin_fill_hw_slot(priv);

		if (hw_stopped || !can_run) {
			priv->state = STOPPED;
		} else if (is_continuous_transfer(priv) &&
			   list_empty(&priv->capture) &&
			   priv->state == RUNNING) {
			/*
			 * The continuous capturing requires an explicit stop
			 * operation when there is no buffer to be set into
			 * the VnMBm registers.
			 */
			rcar_vin_request_capture_stop(priv);
		} else {
			rcar_vin_capture(priv);
		}

	} else if (hw_stopped) {
		priv->state = STOPPED;
		priv->request_to_stop = false;
		complete(&priv->capture_stop);
	}

done:
	spin_unlock(&priv->lock);

	return IRQ_RETVAL(handled);
}

static int rcar_vin_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	int i;

	for (i = 0; i < MAX_BUFFER_NUM; i++)
		priv->queue_buf[i] = NULL;

	pm_runtime_get_sync(ici->v4l2_dev.dev);

	dev_dbg(icd->parent, "R-Car VIN driver attached to camera %d\n",
		icd->devnum);

	return 0;
}

static void rcar_vin_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	struct vb2_v4l2_buffer *vbuf;
	int i;

	/* disable capture, disable interrupts */
	iowrite32(ioread32(priv->base + VNMC_REG) & ~VNMC_ME,
		  priv->base + VNMC_REG);
	iowrite32(0, priv->base + VNIE_REG);

	priv->state = STOPPED;
	priv->request_to_stop = false;

	/* make sure active buffer is cancelled */
	spin_lock_irq(&priv->lock);
	for (i = 0; i < MAX_BUFFER_NUM; i++) {
		vbuf = priv->queue_buf[i];
		if (vbuf) {
			list_del_init(to_buf_list(vbuf));
			vb2_buffer_done(&vbuf->vb2_buf, VB2_BUF_STATE_ERROR);
		}
	}
	spin_unlock_irq(&priv->lock);

	pm_runtime_put(ici->v4l2_dev.dev);

	dev_dbg(icd->parent, "R-Car VIN driver detached from camera %d\n",
		icd->devnum);
}

static void set_coeff(struct rcar_vin_priv *priv, unsigned short xs)
{
	int i;
	const struct vin_coeff *p_prev_set = NULL;
	const struct vin_coeff *p_set = NULL;

	/* Look for suitable coefficient values */
	for (i = 0; i < ARRAY_SIZE(vin_coeff_set); i++) {
		p_prev_set = p_set;
		p_set = &vin_coeff_set[i];

		if (xs < p_set->xs_value)
			break;
	}

	/* Use previous value if its XS value is closer */
	if (p_prev_set && p_set &&
	    xs - p_prev_set->xs_value < p_set->xs_value - xs)
		p_set = p_prev_set;

	/* Set coefficient registers */
	iowrite32(p_set->coeff_set[0], priv->base + VNC1A_REG);
	iowrite32(p_set->coeff_set[1], priv->base + VNC1B_REG);
	iowrite32(p_set->coeff_set[2], priv->base + VNC1C_REG);

	iowrite32(p_set->coeff_set[3], priv->base + VNC2A_REG);
	iowrite32(p_set->coeff_set[4], priv->base + VNC2B_REG);
	iowrite32(p_set->coeff_set[5], priv->base + VNC2C_REG);

	iowrite32(p_set->coeff_set[6], priv->base + VNC3A_REG);
	iowrite32(p_set->coeff_set[7], priv->base + VNC3B_REG);
	iowrite32(p_set->coeff_set[8], priv->base + VNC3C_REG);

	iowrite32(p_set->coeff_set[9], priv->base + VNC4A_REG);
	iowrite32(p_set->coeff_set[10], priv->base + VNC4B_REG);
	iowrite32(p_set->coeff_set[11], priv->base + VNC4C_REG);

	iowrite32(p_set->coeff_set[12], priv->base + VNC5A_REG);
	iowrite32(p_set->coeff_set[13], priv->base + VNC5B_REG);
	iowrite32(p_set->coeff_set[14], priv->base + VNC5C_REG);

	iowrite32(p_set->coeff_set[15], priv->base + VNC6A_REG);
	iowrite32(p_set->coeff_set[16], priv->base + VNC6B_REG);
	iowrite32(p_set->coeff_set[17], priv->base + VNC6C_REG);

	iowrite32(p_set->coeff_set[18], priv->base + VNC7A_REG);
	iowrite32(p_set->coeff_set[19], priv->base + VNC7B_REG);
	iowrite32(p_set->coeff_set[20], priv->base + VNC7C_REG);

	iowrite32(p_set->coeff_set[21], priv->base + VNC8A_REG);
	iowrite32(p_set->coeff_set[22], priv->base + VNC8B_REG);
	iowrite32(p_set->coeff_set[23], priv->base + VNC8C_REG);
}

/* rect is guaranteed to not exceed the scaled camera rectangle */
static int rcar_vin_set_rect(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_cam *cam = icd->host_priv;
	struct rcar_vin_priv *priv = ici->priv;
	unsigned int left_offset, top_offset;
	unsigned char dsize = 0;
	struct v4l2_rect *cam_subrect = &cam->subrect;
	u32 value;

	dev_dbg(icd->parent, "Crop %ux%u@%u:%u\n",
		icd->user_width, icd->user_height, cam->vin_left, cam->vin_top);

	left_offset = cam->vin_left;
	top_offset = cam->vin_top;

	if (icd->current_fmt->host_fmt->fourcc == V4L2_PIX_FMT_RGB32 &&
	    priv->chip == RCAR_E1)
		dsize = 1;

	dev_dbg(icd->parent, "Cam %ux%u@%u:%u\n",
		cam->width, cam->height, cam->vin_left, cam->vin_top);
	dev_dbg(icd->parent, "Cam subrect %ux%u@%u:%u\n",
		cam_subrect->width, cam_subrect->height,
		cam_subrect->left, cam_subrect->top);

	/* Set Start/End Pixel/Line Pre-Clip */
	iowrite32(left_offset << dsize, priv->base + VNSPPRC_REG);
	iowrite32((left_offset + cam_subrect->width - 1) << dsize,
		  priv->base + VNEPPRC_REG);
	switch (priv->field) {
	case V4L2_FIELD_INTERLACED:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
		iowrite32(top_offset / 2, priv->base + VNSLPRC_REG);
		iowrite32((top_offset + cam_subrect->height) / 2 - 1,
			  priv->base + VNELPRC_REG);
		break;
	default:
		iowrite32(top_offset, priv->base + VNSLPRC_REG);
		iowrite32(top_offset + cam_subrect->height - 1,
			  priv->base + VNELPRC_REG);
		break;
	}

	/* Set scaling coefficient */
	value = 0;
	if (cam_subrect->height != cam->out_height)
		value = (4096 * cam_subrect->height) / cam->out_height;
	dev_dbg(icd->parent, "YS Value: %x\n", value);
	iowrite32(value, priv->base + VNYS_REG);

	value = 0;
	if (cam_subrect->width != cam->out_width)
		value = (4096 * cam_subrect->width) / cam->out_width;

	/* Horizontal upscaling is up to double size */
	if (0 < value && value < 2048)
		value = 2048;

	dev_dbg(icd->parent, "XS Value: %x\n", value);
	iowrite32(value, priv->base + VNXS_REG);

	/* Horizontal upscaling is carried out by scaling down from double size */
	if (value < 4096)
		value *= 2;

	set_coeff(priv, value);

	/* Set Start/End Pixel/Line Post-Clip */
	iowrite32(0, priv->base + VNSPPOC_REG);
	iowrite32(0, priv->base + VNSLPOC_REG);
	iowrite32((cam->out_width - 1) << dsize, priv->base + VNEPPOC_REG);
	switch (priv->field) {
	case V4L2_FIELD_INTERLACED:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
		iowrite32(cam->out_height / 2 - 1,
			  priv->base + VNELPOC_REG);
		break;
	default:
		iowrite32(cam->out_height - 1, priv->base + VNELPOC_REG);
		break;
	}

	iowrite32(ALIGN(cam->out_width, 0x10), priv->base + VNIS_REG);

	return 0;
}

static void capture_stop_preserve(struct rcar_vin_priv *priv, u32 *vnmc)
{
	*vnmc = ioread32(priv->base + VNMC_REG);
	/* module disable */
	iowrite32(*vnmc & ~VNMC_ME, priv->base + VNMC_REG);
}

static void capture_restore(struct rcar_vin_priv *priv, u32 vnmc)
{
	unsigned long timeout = jiffies + 10 * HZ;

	/*
	 * Wait until the end of the current frame. It can take a long time,
	 * but if it has been aborted by a MRST1 reset, it should exit sooner.
	 */
	while ((ioread32(priv->base + VNMS_REG) & VNMS_AV) &&
		time_before(jiffies, timeout))
		msleep(1);

	if (time_after(jiffies, timeout)) {
		dev_err(priv->ici.v4l2_dev.dev,
			"Timeout waiting for frame end! Interface problem?\n");
		return;
	}

	iowrite32(vnmc, priv->base + VNMC_REG);
}

#define VIN_MBUS_FLAGS	(V4L2_MBUS_MASTER |		\
			 V4L2_MBUS_PCLK_SAMPLE_RISING |	\
			 V4L2_MBUS_HSYNC_ACTIVE_HIGH |	\
			 V4L2_MBUS_HSYNC_ACTIVE_LOW |	\
			 V4L2_MBUS_VSYNC_ACTIVE_HIGH |	\
			 V4L2_MBUS_VSYNC_ACTIVE_LOW |	\
			 V4L2_MBUS_DATA_ACTIVE_HIGH)

static int rcar_vin_set_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_config cfg;
	unsigned long common_flags;
	u32 vnmc;
	u32 val;
	int ret;

	capture_stop_preserve(priv, &vnmc);

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (!ret) {
		common_flags = soc_mbus_config_compatible(&cfg, VIN_MBUS_FLAGS);
		if (!common_flags) {
			dev_warn(icd->parent,
				 "MBUS flags incompatible: camera 0x%x, host 0x%x\n",
				 cfg.flags, VIN_MBUS_FLAGS);
			return -EINVAL;
		}
	} else if (ret != -ENOIOCTLCMD) {
		return ret;
	} else {
		common_flags = VIN_MBUS_FLAGS;
	}

	/* Make choises, based on platform preferences */
	if ((common_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH) &&
	    (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)) {
		if (priv->pdata_flags & RCAR_VIN_HSYNC_ACTIVE_LOW)
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_HIGH;
		else
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_LOW;
	}

	if ((common_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH) &&
	    (common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)) {
		if (priv->pdata_flags & RCAR_VIN_VSYNC_ACTIVE_LOW)
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_HIGH;
		else
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_LOW;
	}

	cfg.flags = common_flags;
	ret = v4l2_subdev_call(sd, video, s_mbus_config, &cfg);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	val = VNDMR2_FTEV | VNDMR2_VLV(1);
	if (!(common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW))
		val |= VNDMR2_VPS;
	if (!(common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW))
		val |= VNDMR2_HPS;
	iowrite32(val, priv->base + VNDMR2_REG);

	ret = rcar_vin_set_rect(icd);
	if (ret < 0)
		return ret;

	capture_restore(priv, vnmc);

	return 0;
}

static int rcar_vin_try_bus_param(struct soc_camera_device *icd,
				  unsigned char buswidth)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_config cfg;
	int ret;

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (ret == -ENOIOCTLCMD)
		return 0;
	else if (ret)
		return ret;

	if (buswidth > 24)
		return -EINVAL;

	/* check is there common mbus flags */
	ret = soc_mbus_config_compatible(&cfg, VIN_MBUS_FLAGS);
	if (ret)
		return 0;

	dev_warn(icd->parent,
		"MBUS flags incompatible: camera 0x%x, host 0x%x\n",
		 cfg.flags, VIN_MBUS_FLAGS);

	return -EINVAL;
}

static bool rcar_vin_packing_supported(const struct soc_mbus_pixelfmt *fmt)
{
	return	fmt->packing == SOC_MBUS_PACKING_NONE ||
		(fmt->bits_per_sample > 8 &&
		 fmt->packing == SOC_MBUS_PACKING_EXTEND16);
}

static const struct soc_mbus_pixelfmt rcar_vin_formats[] = {
	{
		.fourcc			= V4L2_PIX_FMT_NV16,
		.name			= "NV16",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PLANAR_Y_C,
	},
	{
		.fourcc			= V4L2_PIX_FMT_YUYV,
		.name			= "YUYV",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
	{
		.fourcc			= V4L2_PIX_FMT_UYVY,
		.name			= "UYVY",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
	{
		.fourcc			= V4L2_PIX_FMT_RGB565,
		.name			= "RGB565",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
	{
		.fourcc			= V4L2_PIX_FMT_RGB555X,
		.name			= "ARGB1555",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
	{
		.fourcc			= V4L2_PIX_FMT_RGB32,
		.name			= "RGB888",
		.bits_per_sample	= 32,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
	{
		.fourcc			= V4L2_PIX_FMT_ARGB32,
		.name			= "ARGB8888",
		.bits_per_sample	= 32,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
};

static int rcar_vin_get_formats(struct soc_camera_device *icd, unsigned int idx,
				struct soc_camera_format_xlate *xlate)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;
	int ret, k, n;
	int formats = 0;
	struct rcar_vin_cam *cam;
	struct v4l2_subdev_mbus_code_enum code = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.index = idx,
	};
	const struct soc_mbus_pixelfmt *fmt;

	ret = v4l2_subdev_call(sd, pad, enum_mbus_code, NULL, &code);
	if (ret < 0)
		return 0;

	fmt = soc_mbus_get_fmtdesc(code.code);
	if (!fmt) {
		dev_warn(dev, "unsupported format code #%u: %d\n", idx, code.code);
		return 0;
	}

	ret = rcar_vin_try_bus_param(icd, fmt->bits_per_sample);
	if (ret < 0)
		return 0;

	if (!icd->host_priv) {
		struct v4l2_subdev_format fmt = {
			.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		};
		struct v4l2_mbus_framefmt *mf = &fmt.format;
		struct v4l2_rect rect;
		struct device *dev = icd->parent;
		int shift;

		ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &fmt);
		if (ret < 0)
			return ret;

		/* Cache current client geometry */
		ret = soc_camera_client_g_rect(sd, &rect);
		if (ret == -ENOIOCTLCMD) {
			/* Sensor driver doesn't support cropping */
			rect.left = 0;
			rect.top = 0;
			rect.width = mf->width;
			rect.height = mf->height;
		} else if (ret < 0) {
			return ret;
		}

		/*
		 * If sensor proposes too large format then try smaller ones:
		 * 1280x960, 640x480, 320x240
		 */
		for (shift = 0; shift < 3; shift++) {
			if (mf->width <= VIN_MAX_WIDTH &&
			    mf->height <= VIN_MAX_HEIGHT)
				break;

			mf->width = 1280 >> shift;
			mf->height = 960 >> shift;
			ret = v4l2_device_call_until_err(sd->v4l2_dev,
							 soc_camera_grp_id(icd),
							 pad, set_fmt, NULL,
							 &fmt);
			if (ret < 0)
				return ret;
		}

		if (shift == 3) {
			dev_err(dev,
				"Failed to configure the client below %ux%u\n",
				mf->width, mf->height);
			return -EIO;
		}

		dev_dbg(dev, "camera fmt %ux%u\n", mf->width, mf->height);

		cam = kzalloc(sizeof(*cam), GFP_KERNEL);
		if (!cam)
			return -ENOMEM;
		/*
		 * We are called with current camera crop,
		 * initialise subrect with it
		 */
		cam->rect = rect;
		cam->subrect = rect;
		cam->width = mf->width;
		cam->height = mf->height;
		cam->out_width	= mf->width;
		cam->out_height	= mf->height;

		icd->host_priv = cam;
	} else {
		cam = icd->host_priv;
	}

	/* Beginning of a pass */
	if (!idx)
		cam->extra_fmt = NULL;

	switch (code.code) {
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YUYV10_2X10:
	case MEDIA_BUS_FMT_RGB888_1X24:
		if (cam->extra_fmt)
			break;

		/* Add all our formats that can be generated by VIN */
		cam->extra_fmt = rcar_vin_formats;

		n = ARRAY_SIZE(rcar_vin_formats);
		formats += n;
		for (k = 0; xlate && k < n; k++, xlate++) {
			xlate->host_fmt = &rcar_vin_formats[k];
			xlate->code = code.code;
			dev_dbg(dev, "Providing format %s using code %d\n",
				rcar_vin_formats[k].name, code.code);
		}
		break;
	default:
		if (!rcar_vin_packing_supported(fmt))
			return 0;

		dev_dbg(dev, "Providing format %s in pass-through mode\n",
			fmt->name);
		break;
	}

	/* Generic pass-through */
	formats++;
	if (xlate) {
		xlate->host_fmt = fmt;
		xlate->code = code.code;
		xlate++;
	}

	return formats;
}

static void rcar_vin_put_formats(struct soc_camera_device *icd)
{
	kfree(icd->host_priv);
	icd->host_priv = NULL;
}

static int rcar_vin_set_crop(struct soc_camera_device *icd,
			     const struct v4l2_crop *a)
{
	struct v4l2_crop a_writable = *a;
	const struct v4l2_rect *rect = &a_writable.c;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	struct v4l2_crop cam_crop;
	struct rcar_vin_cam *cam = icd->host_priv;
	struct v4l2_rect *cam_rect = &cam_crop.c;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct v4l2_mbus_framefmt *mf = &fmt.format;
	u32 vnmc;
	int ret, i;

	dev_dbg(dev, "S_CROP(%ux%u@%u:%u)\n", rect->width, rect->height,
		rect->left, rect->top);

	/* During camera cropping its output window can change too, stop VIN */
	capture_stop_preserve(priv, &vnmc);
	dev_dbg(dev, "VNMC_REG 0x%x\n", vnmc);

	/* Apply iterative camera S_CROP for new input window. */
	ret = soc_camera_client_s_crop(sd, &a_writable, &cam_crop,
				       &cam->rect, &cam->subrect);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "camera cropped to %ux%u@%u:%u\n",
		cam_rect->width, cam_rect->height,
		cam_rect->left, cam_rect->top);

	/* On success cam_crop contains current camera crop */

	/* Retrieve camera output window */
	ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &fmt);
	if (ret < 0)
		return ret;

	if (mf->width > VIN_MAX_WIDTH || mf->height > VIN_MAX_HEIGHT)
		return -EINVAL;

	/* Cache camera output window */
	cam->width = mf->width;
	cam->height = mf->height;

	icd->user_width  = cam->width;
	icd->user_height = cam->height;

	cam->vin_left = rect->left & ~1;
	cam->vin_top = rect->top & ~1;

	/* Use VIN cropping to crop to the new window. */
	ret = rcar_vin_set_rect(icd);
	if (ret < 0)
		return ret;

	cam->subrect = *rect;

	dev_dbg(dev, "VIN cropped to %ux%u@%u:%u\n",
		icd->user_width, icd->user_height,
		cam->vin_left, cam->vin_top);

	/* Restore capture */
	for (i = 0; i < MAX_BUFFER_NUM; i++) {
		if (priv->queue_buf[i] && priv->state == STOPPED) {
			vnmc |= VNMC_ME;
			break;
		}
	}
	capture_restore(priv, vnmc);

	/* Even if only camera cropping succeeded */
	return ret;
}

static int rcar_vin_get_crop(struct soc_camera_device *icd,
			     struct v4l2_crop *a)
{
	struct rcar_vin_cam *cam = icd->host_priv;

	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->c = cam->subrect;

	return 0;
}

/* Similar to set_crop multistage iterative algorithm */
static int rcar_vin_set_fmt(struct soc_camera_device *icd,
			    struct v4l2_format *f)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct rcar_vin_cam *cam = icd->host_priv;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	struct device *dev = icd->parent;
	__u32 pixfmt = pix->pixelformat;
	const struct soc_camera_format_xlate *xlate;
	unsigned int vin_sub_width = 0, vin_sub_height = 0;
	int ret;
	bool can_scale;
	enum v4l2_field field;
	v4l2_std_id std;

	dev_dbg(dev, "S_FMT(pix=0x%x, %ux%u)\n",
		pixfmt, pix->width, pix->height);

	switch (pix->field) {
	default:
		pix->field = V4L2_FIELD_NONE;
		/* fall-through */
	case V4L2_FIELD_NONE:
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
		field = pix->field;
		break;
	case V4L2_FIELD_INTERLACED:
		/* Query for standard if not explicitly mentioned _TB/_BT */
		ret = v4l2_subdev_call(sd, video, querystd, &std);
		if (ret == -ENOIOCTLCMD) {
			field = V4L2_FIELD_NONE;
		} else if (ret < 0) {
			return ret;
		} else {
			field = std & V4L2_STD_625_50 ?
				V4L2_FIELD_INTERLACED_TB :
				V4L2_FIELD_INTERLACED_BT;
		}
		break;
	}

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(dev, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}
	/* Calculate client output geometry */
	soc_camera_calc_client_output(icd, &cam->rect, &cam->subrect, pix, &mf,
				      12);
	mf.field = pix->field;
	mf.colorspace = pix->colorspace;
	mf.code	 = xlate->code;

	switch (pixfmt) {
	case V4L2_PIX_FMT_RGB32:
		can_scale = priv->chip != RCAR_E1;
		break;
	case V4L2_PIX_FMT_ARGB32:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB555X:
		can_scale = true;
		break;
	default:
		can_scale = false;
		break;
	}

	dev_dbg(dev, "request camera output %ux%u\n", mf.width, mf.height);

	ret = soc_camera_client_scale(icd, &cam->rect, &cam->subrect,
				      &mf, &vin_sub_width, &vin_sub_height,
				      can_scale, 12);

	/* Done with the camera. Now see if we can improve the result */
	dev_dbg(dev, "Camera %d fmt %ux%u, requested %ux%u\n",
		ret, mf.width, mf.height, pix->width, pix->height);

	if (ret == -ENOIOCTLCMD)
		dev_dbg(dev, "Sensor doesn't support scaling\n");
	else if (ret < 0)
		return ret;

	if (mf.code != xlate->code)
		return -EINVAL;

	/* Prepare VIN crop */
	cam->width = mf.width;
	cam->height = mf.height;

	/* Use VIN scaling to scale to the requested user window. */

	/* We cannot scale up */
	if (pix->width > vin_sub_width)
		vin_sub_width = pix->width;

	if (pix->height > vin_sub_height)
		vin_sub_height = pix->height;

	pix->colorspace = mf.colorspace;

	if (!can_scale) {
		pix->width = vin_sub_width;
		pix->height = vin_sub_height;
	}

	/*
	 * We have calculated CFLCR, the actual configuration will be performed
	 * in rcar_vin_set_bus_param()
	 */

	dev_dbg(dev, "W: %u : %u, H: %u : %u\n",
		vin_sub_width, pix->width, vin_sub_height, pix->height);

	cam->out_width = pix->width;
	cam->out_height = pix->height;

	icd->current_fmt = xlate;

	priv->field = field;

	return 0;
}

static int rcar_vin_try_fmt(struct soc_camera_device *icd,
			    struct v4l2_format *f)
{
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_subdev_pad_config pad_cfg;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
	};
	struct v4l2_mbus_framefmt *mf = &format.format;
	__u32 pixfmt = pix->pixelformat;
	int width, height;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		xlate = icd->current_fmt;
		dev_dbg(icd->parent, "Format %x not found, keeping %x\n",
			pixfmt, xlate->host_fmt->fourcc);
		pixfmt = xlate->host_fmt->fourcc;
		pix->pixelformat = pixfmt;
		pix->colorspace = icd->colorspace;
	}

	/* FIXME: calculate using depth and bus width */
	v4l_bound_align_image(&pix->width, 2, VIN_MAX_WIDTH, 1,
			      &pix->height, 4, VIN_MAX_HEIGHT, 2, 0);

	width = pix->width;
	height = pix->height;

	/* let soc-camera calculate these values */
	pix->bytesperline = 0;
	pix->sizeimage = 0;

	/* limit to sensor capabilities */
	mf->width = pix->width;
	mf->height = pix->height;
	mf->field = pix->field;
	mf->code = xlate->code;
	mf->colorspace = pix->colorspace;

	ret = v4l2_device_call_until_err(sd->v4l2_dev, soc_camera_grp_id(icd),
					 pad, set_fmt, &pad_cfg, &format);
	if (ret < 0)
		return ret;

	/* Adjust only if VIN cannot scale */
	if (pix->width > mf->width * 2)
		pix->width = mf->width * 2;
	if (pix->height > mf->height * 3)
		pix->height = mf->height * 3;

	pix->field = mf->field;
	pix->colorspace = mf->colorspace;

	if (pixfmt == V4L2_PIX_FMT_NV16) {
		/* FIXME: check against rect_max after converting soc-camera */
		/* We can scale precisely, need a bigger image from camera */
		if (pix->width < width || pix->height < height) {
			/*
			 * We presume, the sensor behaves sanely, i.e. if
			 * requested a bigger rectangle, it will not return a
			 * smaller one.
			 */
			mf->width = VIN_MAX_WIDTH;
			mf->height = VIN_MAX_HEIGHT;
			ret = v4l2_device_call_until_err(sd->v4l2_dev,
							 soc_camera_grp_id(icd),
							 pad, set_fmt, &pad_cfg,
							 &format);
			if (ret < 0) {
				dev_err(icd->parent,
					"client try_fmt() = %d\n", ret);
				return ret;
			}
		}
		/* We will scale exactly */
		if (mf->width > width)
			pix->width = width;
		if (mf->height > height)
			pix->height = height;
	}

	return ret;
}

static unsigned int rcar_vin_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;

	return vb2_poll(&icd->vb2_vidq, file, pt);
}

static int rcar_vin_querycap(struct soc_camera_host *ici,
			     struct v4l2_capability *cap)
{
	strlcpy(cap->card, "R_Car_VIN", sizeof(cap->card));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s%d", DRV_NAME, ici->nr);

	return 0;
}

static int rcar_vin_init_videobuf2(struct vb2_queue *vq,
				   struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);

	vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vq->io_modes = VB2_MMAP | VB2_USERPTR;
	vq->drv_priv = icd;
	vq->ops = &rcar_vin_vb2_ops;
	vq->mem_ops = &vb2_dma_contig_memops;
	vq->buf_struct_size = sizeof(struct rcar_vin_buffer);
	vq->timestamp_flags  = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vq->lock = &ici->host_lock;
	vq->dev = ici->v4l2_dev.dev;

	return vb2_queue_init(vq);
}

static struct soc_camera_host_ops rcar_vin_host_ops = {
	.owner		= THIS_MODULE,
	.add		= rcar_vin_add_device,
	.remove		= rcar_vin_remove_device,
	.get_formats	= rcar_vin_get_formats,
	.put_formats	= rcar_vin_put_formats,
	.get_crop	= rcar_vin_get_crop,
	.set_crop	= rcar_vin_set_crop,
	.try_fmt	= rcar_vin_try_fmt,
	.set_fmt	= rcar_vin_set_fmt,
	.poll		= rcar_vin_poll,
	.querycap	= rcar_vin_querycap,
	.set_bus_param	= rcar_vin_set_bus_param,
	.init_videobuf2	= rcar_vin_init_videobuf2,
};

#ifdef CONFIG_OF
static const struct of_device_id rcar_vin_of_table[] = {
	{ .compatible = "renesas,vin-r8a7795", .data = (void *)RCAR_GEN3 },
	{ .compatible = "renesas,vin-r8a7794", .data = (void *)RCAR_GEN2 },
	{ .compatible = "renesas,vin-r8a7793", .data = (void *)RCAR_GEN2 },
	{ .compatible = "renesas,vin-r8a7791", .data = (void *)RCAR_GEN2 },
	{ .compatible = "renesas,vin-r8a7790", .data = (void *)RCAR_GEN2 },
	{ .compatible = "renesas,vin-r8a7779", .data = (void *)RCAR_H1 },
	{ .compatible = "renesas,vin-r8a7778", .data = (void *)RCAR_M1 },
	{ .compatible = "renesas,rcar-gen3-vin", .data = (void *)RCAR_GEN3 },
	{ .compatible = "renesas,rcar-gen2-vin", .data = (void *)RCAR_GEN2 },
	{ },
};
MODULE_DEVICE_TABLE(of, rcar_vin_of_table);
#endif

static int rcar_vin_probe(struct platform_device *pdev)
{
	const struct of_device_id *match = NULL;
	struct rcar_vin_priv *priv;
	struct v4l2_of_endpoint ep;
	struct device_node *np;
	struct resource *mem;
	unsigned int pdata_flags;
	int irq, ret;

	match = of_match_device(of_match_ptr(rcar_vin_of_table), &pdev->dev);

	np = of_graph_get_next_endpoint(pdev->dev.of_node, NULL);
	if (!np) {
		dev_err(&pdev->dev, "could not find endpoint\n");
		return -EINVAL;
	}

	ret = v4l2_of_parse_endpoint(np, &ep);
	if (ret) {
		dev_err(&pdev->dev, "could not parse endpoint\n");
		return ret;
	}

	if (ep.bus_type == V4L2_MBUS_BT656)
		pdata_flags = RCAR_VIN_BT656;
	else {
		pdata_flags = 0;
		if (ep.bus.parallel.flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			pdata_flags |= RCAR_VIN_HSYNC_ACTIVE_LOW;
		if (ep.bus.parallel.flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			pdata_flags |= RCAR_VIN_VSYNC_ACTIVE_LOW;
	}

	of_node_put(np);

	dev_dbg(&pdev->dev, "pdata_flags = %08x\n", pdata_flags);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem == NULL)
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return -EINVAL;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct rcar_vin_priv),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	ret = devm_request_irq(&pdev->dev, irq, rcar_vin_irq, IRQF_SHARED,
			       dev_name(&pdev->dev), priv);
	if (ret)
		return ret;

	priv->ici.priv = priv;
	priv->ici.v4l2_dev.dev = &pdev->dev;
	priv->ici.drv_name = dev_name(&pdev->dev);
	priv->ici.ops = &rcar_vin_host_ops;

	priv->pdata_flags = pdata_flags;
	if (!match) {
		priv->ici.nr = pdev->id;
		priv->chip = pdev->id_entry->driver_data;
	} else {
		priv->ici.nr = of_alias_get_id(pdev->dev.of_node, "vin");
		priv->chip = (enum chip_id)match->data;
	}

	spin_lock_init(&priv->lock);
	INIT_LIST_HEAD(&priv->capture);

	priv->state = STOPPED;

	pm_suspend_ignore_children(&pdev->dev, true);
	pm_runtime_enable(&pdev->dev);

	ret = soc_camera_host_register(&priv->ici);
	if (ret)
		goto cleanup;

	return 0;

cleanup:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int rcar_vin_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);

	soc_camera_host_unregister(soc_host);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver rcar_vin_driver = {
	.probe		= rcar_vin_probe,
	.remove		= rcar_vin_remove,
	.driver		= {
		.name		= DRV_NAME,
		.of_match_table	= of_match_ptr(rcar_vin_of_table),
	},
};

module_platform_driver(rcar_vin_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rcar_vin");
MODULE_DESCRIPTION("Renesas R-Car VIN camera host driver");
