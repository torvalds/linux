// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 driver for the JPEG encoder/decoder from i.MX8QXP/i.MX8QM application
 * processors.
 *
 * The multi-planar buffers API is used.
 *
 * Baseline and extended sequential jpeg decoding is supported.
 * Progressive jpeg decoding is not supported by the IP.
 * Supports encode and decode of various formats:
 *     YUV444, YUV422, YUV420, BGR, ABGR, Gray
 * YUV420 is the only multi-planar format supported.
 * Minimum resolution is 64 x 64, maximum 8192 x 8192.
 * To achieve 8192 x 8192, modify in defconfig: CONFIG_CMA_SIZE_MBYTES=320
 * The alignment requirements for the resolution depend on the format,
 * multiple of 16 resolutions should work for all formats.
 * Special workarounds are made in the driver to support NV12 1080p.
 * When decoding, the driver detects image resolution and pixel format
 * from the jpeg stream, by parsing the jpeg markers.
 *
 * The IP has 4 slots available for context switching, but only slot 0
 * was fully tested to work. Context switching is not used by the driver.
 * Each driver instance (context) allocates a slot for itself, but this
 * is postponed until device_run, to allow unlimited opens.
 *
 * The driver submits jobs to the IP by setting up a descriptor for the
 * used slot, and then validating it. The encoder has an additional descriptor
 * for the configuration phase. The driver expects FRM_DONE interrupt from
 * IP to mark the job as finished.
 *
 * The decoder IP has some limitations regarding the component ID's,
 * but the driver works around this by replacing them in the jpeg stream.
 *
 * A module parameter is available for debug purpose (jpeg_tracing), to enable
 * it, enable dynamic debug for this module and:
 * echo 1 > /sys/module/mxc_jpeg_encdec/parameters/jpeg_tracing
 *
 * This is inspired by the drivers/media/platform/samsung/s5p-jpeg driver
 *
 * Copyright 2018-2019 NXP
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/string.h>

#include <media/v4l2-jpeg.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-dma-contig.h>

#include "mxc-jpeg-hw.h"
#include "mxc-jpeg.h"

static const struct mxc_jpeg_fmt mxc_formats[] = {
	{
		.name		= "JPEG",
		.fourcc		= V4L2_PIX_FMT_JPEG,
		.subsampling	= -1,
		.nc		= -1,
		.mem_planes	= 1,
		.comp_planes	= 1,
		.flags		= MXC_JPEG_FMT_TYPE_ENC,
	},
	{
		.name		= "BGR", /*BGR packed format*/
		.fourcc		= V4L2_PIX_FMT_BGR24,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
		.nc		= 3,
		.depth		= 24,
		.mem_planes	= 1,
		.comp_planes	= 1,
		.h_align	= 3,
		.v_align	= 3,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 8,
		.is_rgb		= 1,
	},
	{
		.name		= "BGR 12bit", /*12-bit BGR packed format*/
		.fourcc		= V4L2_PIX_FMT_BGR48_12,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
		.nc		= 3,
		.depth		= 36,
		.mem_planes	= 1,
		.comp_planes	= 1,
		.h_align	= 3,
		.v_align	= 3,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 12,
		.is_rgb		= 1,
	},
	{
		.name		= "ABGR", /* ABGR packed format */
		.fourcc		= V4L2_PIX_FMT_ABGR32,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
		.nc		= 4,
		.depth		= 32,
		.mem_planes	= 1,
		.comp_planes	= 1,
		.h_align	= 3,
		.v_align	= 3,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 8,
		.is_rgb		= 1,
	},
	{
		.name		= "ABGR 12bit", /* 12-bit ABGR packed format */
		.fourcc		= V4L2_PIX_FMT_ABGR64_12,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
		.nc		= 4,
		.depth		= 48,
		.mem_planes	= 1,
		.comp_planes	= 1,
		.h_align	= 3,
		.v_align	= 3,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 12,
		.is_rgb		= 1,
	},
	{
		.name		= "YUV420", /* 1st plane = Y, 2nd plane = UV */
		.fourcc		= V4L2_PIX_FMT_NV12M,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
		.nc		= 3,
		.depth		= 12, /* 6 bytes (4Y + UV) for 4 pixels */
		.mem_planes	= 2,
		.comp_planes	= 2, /* 1 plane Y, 1 plane UV interleaved */
		.h_align	= 4,
		.v_align	= 4,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 8,
	},
	{
		.name		= "YUV420", /* 1st plane = Y, 2nd plane = UV */
		.fourcc		= V4L2_PIX_FMT_NV12,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
		.nc		= 3,
		.depth		= 12, /* 6 bytes (4Y + UV) for 4 pixels */
		.mem_planes	= 1,
		.comp_planes	= 2, /* 1 plane Y, 1 plane UV interleaved */
		.h_align	= 4,
		.v_align	= 4,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 8,
	},
	{
		.name		= "YUV420 12bit", /* 1st plane = Y, 2nd plane = UV */
		.fourcc		= V4L2_PIX_FMT_P012M,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
		.nc		= 3,
		.depth		= 18, /* 6 x 12 bits (4Y + UV) for 4 pixels */
		.mem_planes	= 2,
		.comp_planes	= 2, /* 1 plane Y, 1 plane UV interleaved */
		.h_align	= 4,
		.v_align	= 4,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 12,
	},
	{
		.name		= "YUV420 12bit", /* 1st plane = Y, 2nd plane = UV */
		.fourcc		= V4L2_PIX_FMT_P012,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_420,
		.nc		= 3,
		.depth		= 18, /* 6 x 12 bits (4Y + UV) for 4 pixels */
		.mem_planes	= 1,
		.comp_planes	= 2, /* 1 plane Y, 1 plane UV interleaved */
		.h_align	= 4,
		.v_align	= 4,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 12,
	},
	{
		.name		= "YUV422", /* YUYV */
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_422,
		.nc		= 3,
		.depth		= 16,
		.mem_planes	= 1,
		.comp_planes	= 1,
		.h_align	= 4,
		.v_align	= 3,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 8,
	},
	{
		.name		= "YUV422 12bit", /* YUYV */
		.fourcc		= V4L2_PIX_FMT_Y212,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_422,
		.nc		= 3,
		.depth		= 24,
		.mem_planes	= 1,
		.comp_planes	= 1,
		.h_align	= 4,
		.v_align	= 3,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 12,
	},
	{
		.name		= "YUV444", /* YUVYUV */
		.fourcc		= V4L2_PIX_FMT_YUV24,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
		.nc		= 3,
		.depth		= 24,
		.mem_planes	= 1,
		.comp_planes	= 1,
		.h_align	= 3,
		.v_align	= 3,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 8,
	},
	{
		.name		= "YUV444 12bit", /* YUVYUV */
		.fourcc		= V4L2_PIX_FMT_YUV48_12,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_444,
		.nc		= 3,
		.depth		= 36,
		.mem_planes	= 1,
		.comp_planes	= 1,
		.h_align	= 3,
		.v_align	= 3,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 12,
	},
	{
		.name		= "Gray", /* Gray (Y8/Y12) or Single Comp */
		.fourcc		= V4L2_PIX_FMT_GREY,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY,
		.nc		= 1,
		.depth		= 8,
		.mem_planes	= 1,
		.comp_planes	= 1,
		.h_align	= 3,
		.v_align	= 3,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 8,
	},
	{
		.name		= "Gray 12bit", /* Gray (Y8/Y12) or Single Comp */
		.fourcc		= V4L2_PIX_FMT_Y012,
		.subsampling	= V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY,
		.nc		= 1,
		.depth		= 12,
		.mem_planes	= 1,
		.comp_planes	= 1,
		.h_align	= 3,
		.v_align	= 3,
		.flags		= MXC_JPEG_FMT_TYPE_RAW,
		.precision	= 12,
	},
};

#define MXC_JPEG_NUM_FORMATS ARRAY_SIZE(mxc_formats)

static const int mxc_decode_mode = MXC_JPEG_DECODE;
static const int mxc_encode_mode = MXC_JPEG_ENCODE;

static const struct of_device_id mxc_jpeg_match[] = {
	{
		.compatible = "nxp,imx8qxp-jpgdec",
		.data       = &mxc_decode_mode,
	},
	{
		.compatible = "nxp,imx8qxp-jpgenc",
		.data       = &mxc_encode_mode,
	},
	{ },
};

/*
 * default configuration stream, 64x64 yuv422
 * split by JPEG marker, so it's easier to modify & use
 */
static const unsigned char jpeg_soi[] = {
	0xFF, 0xD8
};

static const unsigned char jpeg_app0[] = {
	0xFF, 0xE0,
	0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00,
	0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01,
	0x00, 0x00
};

static const unsigned char jpeg_app14[] = {
	0xFF, 0xEE,
	0x00, 0x0E, 0x41, 0x64, 0x6F, 0x62, 0x65,
	0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const unsigned char jpeg_dqt[] = {
	0xFF, 0xDB,
	0x00, 0x84, 0x00, 0x10, 0x0B, 0x0C, 0x0E,
	0x0C, 0x0A, 0x10, 0x0E, 0x0D, 0x0E, 0x12,
	0x11, 0x10, 0x13, 0x18, 0x28, 0x1A, 0x18,
	0x16, 0x16, 0x18, 0x31, 0x23, 0x25, 0x1D,
	0x28, 0x3A, 0x33, 0x3D, 0x3C, 0x39, 0x33,
	0x38, 0x37, 0x40, 0x48, 0x5C, 0x4E, 0x40,
	0x44, 0x57, 0x45, 0x37, 0x38, 0x50, 0x6D,
	0x51, 0x57, 0x5F, 0x62, 0x67, 0x68, 0x67,
	0x3E, 0x4D, 0x71, 0x79, 0x70, 0x64, 0x78,
	0x5C, 0x65, 0x67, 0x63, 0x01, 0x11, 0x12,
	0x12, 0x18, 0x15, 0x18, 0x2F, 0x1A, 0x1A,
	0x2F, 0x63, 0x42, 0x38, 0x42, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
	0x63, 0x63, 0x63, 0x63, 0x63, 0x63
};

static const unsigned char jpeg_dqt_extseq[] = {
	0xFF, 0xDB,
	0x01, 0x04,
	0x10,
	0x00, 0x80, 0x00, 0x58, 0x00, 0x60, 0x00, 0x70,
	0x00, 0x60, 0x00, 0x50, 0x00, 0x80, 0x00, 0x70,
	0x00, 0x68, 0x00, 0x70, 0x00, 0x90, 0x00, 0x88,
	0x00, 0x80, 0x00, 0x98, 0x00, 0xC0, 0x01, 0x40,
	0x00, 0xD0, 0x00, 0xC0, 0x00, 0xB0, 0x00, 0xB0,
	0x00, 0xC0, 0x01, 0x88, 0x01, 0x18, 0x01, 0x28,
	0x00, 0xE8, 0x01, 0x40, 0x01, 0xD0, 0x01, 0x98,
	0x01, 0xE8, 0x01, 0xE0, 0x01, 0xC8, 0x01, 0x98,
	0x01, 0xC0, 0x01, 0xB8, 0x02, 0x00, 0x02, 0x40,
	0x02, 0xE0, 0x02, 0x70, 0x02, 0x00, 0x02, 0x20,
	0x02, 0xB8, 0x02, 0x28, 0x01, 0xB8, 0x01, 0xC0,
	0x02, 0x80, 0x03, 0x68, 0x02, 0x88, 0x02, 0xB8,
	0x02, 0xF8, 0x03, 0x10, 0x03, 0x38, 0x03, 0x40,
	0x03, 0x38, 0x01, 0xF0, 0x02, 0x68, 0x03, 0x88,
	0x03, 0xC8, 0x03, 0x80, 0x03, 0x20, 0x03, 0xC0,
	0x02, 0xE0, 0x03, 0x28, 0x03, 0x38, 0x03, 0x18,
	0x11,
	0x00, 0x88, 0x00, 0x90, 0x00, 0x90, 0x00, 0xC0,
	0x00, 0xA8, 0x00, 0xC0, 0x01, 0x78, 0x00, 0xD0,
	0x00, 0xD0, 0x01, 0x78, 0x03, 0x18, 0x02, 0x10,
	0x01, 0xC0, 0x02, 0x10, 0x03, 0x18, 0x03, 0x18,
	0x03, 0x18, 0x03, 0x18, 0x03, 0x18, 0x03, 0x18,
	0x03, 0x18, 0x03, 0x18, 0x03, 0x18, 0x03, 0x18,
	0x03, 0x18, 0x03, 0x18, 0x03, 0x18, 0x03, 0x18,
	0x03, 0x18, 0x03, 0x18, 0x03, 0x18, 0x03, 0x18,
	0x03, 0x18, 0x03, 0x18, 0x03, 0x18, 0x03, 0x18,
	0x03, 0x18, 0x03, 0x18, 0x03, 0x18, 0x03, 0x18,
	0x03, 0x18, 0x03, 0x18, 0x03, 0x18, 0x03, 0x18,
	0x03, 0x18, 0x03, 0x18, 0x03, 0x18, 0x03, 0x18,
	0x03, 0x18, 0x03, 0x18, 0x03, 0x18, 0x03, 0x18,
	0x03, 0x18, 0x03, 0x18, 0x03, 0x18, 0x03, 0x18,
	0x03, 0x18, 0x03, 0x18, 0x03, 0x18, 0x03, 0x18,
	0x03, 0x18, 0x03, 0x18, 0x03, 0x18, 0x03, 0x18,
};

static const unsigned char jpeg_sof_maximal[] = {
	0xFF, 0xC0,
	0x00, 0x14, 0x08, 0x00, 0x40, 0x00, 0x40,
	0x04, 0x01, 0x11, 0x00, 0x02, 0x11, 0x01,
	0x03, 0x11, 0x01, 0x04, 0x11, 0x01
};

static const unsigned char jpeg_sof_extseq[] = {
	0xFF, 0xC1,
	0x00, 0x14, 0x08, 0x00, 0x40, 0x00, 0x40,
	0x04, 0x01, 0x11, 0x00, 0x02, 0x11, 0x01,
	0x03, 0x11, 0x01, 0x04, 0x11, 0x01
};

static const unsigned char jpeg_dht[] = {
	0xFF, 0xC4,
	0x01, 0xA2, 0x00, 0x00, 0x01, 0x05, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x09, 0x0A, 0x0B, 0x10, 0x00, 0x02, 0x01,
	0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05,
	0x04, 0x04, 0x00, 0x00, 0x01, 0x7D, 0x01,
	0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
	0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61,
	0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91,
	0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15,
	0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72,
	0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19,
	0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A,
	0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A,
	0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76,
	0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85,
	0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93,
	0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A,
	0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,
	0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
	0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4,
	0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2,
	0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9,
	0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6,
	0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3,
	0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA,
	0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03,
	0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
	0x0B, 0x11, 0x00, 0x02, 0x01, 0x02, 0x04,
	0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04,
	0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02,
	0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06,
	0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13,
	0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
	0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52,
	0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16,
	0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18,
	0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A,
	0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43,
	0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,
	0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77,
	0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85,
	0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93,
	0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A,
	0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,
	0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
	0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4,
	0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2,
	0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9,
	0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
	0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5,
	0xF6, 0xF7, 0xF8, 0xF9, 0xFA
};

static const unsigned char jpeg_dht_extseq[] = {
	0xFF, 0xC4,
	0x02, 0x2a, 0x00, 0x00, 0x01, 0x05, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02,
	0x04, 0x03, 0x05, 0x05, 0x02, 0x03, 0x02,
	0x00, 0x00, 0xbf, 0x01, 0x02, 0x03, 0x00,
	0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41,
	0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
	0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23,
	0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
	0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a,
	0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26,
	0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36,
	0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
	0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54,
	0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63,
	0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
	0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
	0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
	0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
	0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4,
	0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2,
	0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9,
	0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
	0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5,
	0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
	0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
	0xf7, 0xf8, 0xf9, 0xfa, 0x0b, 0x0c, 0x0d,
	0x0e, 0x1b, 0x1c, 0x1d, 0x1e, 0x2b, 0x2c,
	0x2d, 0x2e, 0x3b, 0x3c, 0x3d, 0x3e, 0x4b,
	0x4c, 0x4d, 0x4e, 0x5b, 0x5c, 0x5d, 0x5e,
	0x6b, 0x6c, 0x6d, 0x6e, 0x7b, 0x7c, 0x7d,
	0x7e, 0x8b, 0x8c, 0x8d, 0x8e, 0x9b, 0x9c,
	0x9d, 0x9e, 0xab, 0xac, 0xad, 0xae, 0xbb,
	0xbc, 0xbd, 0xbe, 0xcb, 0xcc, 0xcd, 0xce,
	0xdb, 0xdc, 0xdd, 0xde, 0xeb, 0xec, 0xed,
	0xee, 0xfb, 0xfc, 0xfd, 0xfe, 0x01, 0x00,
	0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
	0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
	0x0d, 0x0e, 0x0f, 0x11, 0x00, 0x02, 0x01,
	0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05,
	0x02, 0x03, 0x02, 0x00, 0x00, 0xbf, 0x01,
	0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
	0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61,
	0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91,
	0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15,
	0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
	0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19,
	0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
	0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
	0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76,
	0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85,
	0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93,
	0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
	0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
	0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
	0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4,
	0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
	0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9,
	0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6,
	0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3,
	0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
	0x0b, 0x0c, 0x0d, 0x0e, 0x1b, 0x1c, 0x1d,
	0x1e, 0x2b, 0x2c, 0x2d, 0x2e, 0x3b, 0x3c,
	0x3d, 0x3e, 0x4b, 0x4c, 0x4d, 0x4e, 0x5b,
	0x5c, 0x5d, 0x5e, 0x6b, 0x6c, 0x6d, 0x6e,
	0x7b, 0x7c, 0x7d, 0x7e, 0x8b, 0x8c, 0x8d,
	0x8e, 0x9b, 0x9c, 0x9d, 0x9e, 0xab, 0xac,
	0xad, 0xae, 0xbb, 0xbc, 0xbd, 0xbe, 0xcb,
	0xcc, 0xcd, 0xce, 0xdb, 0xdc, 0xdd, 0xde,
	0xeb, 0xec, 0xed, 0xee, 0xfb, 0xfc, 0xfd,
	0xfe,
};

static const unsigned char jpeg_dri[] = {
	0xFF, 0xDD,
	0x00, 0x04, 0x00, 0x20
};

static const unsigned char jpeg_sos_maximal[] = {
	0xFF, 0xDA,
	0x00, 0x0C, 0x04, 0x01, 0x00, 0x02, 0x11, 0x03,
	0x11, 0x04, 0x11, 0x00, 0x3F, 0x00
};

static const unsigned char jpeg_image_red[] = {
	0xFC, 0x5F, 0xA2, 0xBF, 0xCA, 0x73, 0xFE, 0xFE,
	0x02, 0x8A, 0x00, 0x28, 0xA0, 0x02, 0x8A, 0x00,
	0x28, 0xA0, 0x02, 0x8A, 0x00, 0x28, 0xA0, 0x02,
	0x8A, 0x00, 0x28, 0xA0, 0x02, 0x8A, 0x00, 0x28,
	0xA0, 0x02, 0x8A, 0x00, 0x28, 0xA0, 0x02, 0x8A,
	0x00, 0x28, 0xA0, 0x02, 0x8A, 0x00, 0x28, 0xA0,
	0x02, 0x8A, 0x00, 0x28, 0xA0, 0x02, 0x8A, 0x00,
	0x28, 0xA0, 0x02, 0x8A, 0x00, 0x28, 0xA0, 0x02,
	0x8A, 0x00, 0x28, 0xA0, 0x02, 0x8A, 0x00, 0x28,
	0xA0, 0x02, 0x8A, 0x00, 0x28, 0xA0, 0x02, 0x8A,
	0x00, 0x28, 0xA0, 0x02, 0x8A, 0x00
};

static const unsigned char jpeg_eoi[] = {
	0xFF, 0xD9
};

struct mxc_jpeg_src_buf {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer	b;
	struct list_head	list;

	/* mxc-jpeg specific */
	bool			dht_needed;
	bool			jpeg_parse_error;
	const struct mxc_jpeg_fmt	*fmt;
	int			w;
	int			h;
};

static inline struct mxc_jpeg_src_buf *vb2_to_mxc_buf(struct vb2_buffer *vb)
{
	return container_of(to_vb2_v4l2_buffer(vb),
			    struct mxc_jpeg_src_buf, b);
}

static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-3)");

static unsigned int hw_timeout = 2000;
module_param(hw_timeout, int, 0644);
MODULE_PARM_DESC(hw_timeout, "MXC JPEG hw timeout, the number of milliseconds");

static void mxc_jpeg_bytesperline(struct mxc_jpeg_q_data *q, u32 precision);
static void mxc_jpeg_sizeimage(struct mxc_jpeg_q_data *q);

static void _bswap16(u16 *a)
{
	*a = ((*a & 0x00FF) << 8) | ((*a & 0xFF00) >> 8);
}

static void print_mxc_buf(struct mxc_jpeg_dev *jpeg, struct vb2_buffer *buf,
			  unsigned long len)
{
	unsigned int plane_no;
	u32 dma_addr;
	void *vaddr;
	unsigned long payload;

	if (debug < 3)
		return;

	for (plane_no = 0; plane_no < buf->num_planes; plane_no++) {
		payload = vb2_get_plane_payload(buf, plane_no);
		if (len == 0)
			len = payload;
		dma_addr = vb2_dma_contig_plane_dma_addr(buf, plane_no);
		vaddr = vb2_plane_vaddr(buf, plane_no);
		v4l2_dbg(3, debug, &jpeg->v4l2_dev,
			 "plane %d (vaddr=%p dma_addr=%x payload=%ld):",
			  plane_no, vaddr, dma_addr, payload);
		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1,
			       vaddr, len, false);
	}
}

static inline struct mxc_jpeg_ctx *mxc_jpeg_fh_to_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct mxc_jpeg_ctx, fh);
}

static int enum_fmt(const struct mxc_jpeg_fmt *mxc_formats, int n,
		    struct v4l2_fmtdesc *f, u32 type)
{
	int i, num = 0;

	for (i = 0; i < n; ++i) {
		if (mxc_formats[i].flags == type) {
			/* index-th format of searched type found ? */
			if (num == f->index)
				break;
			/* Correct type but haven't reached our index yet,
			 * just increment per-type index
			 */
			++num;
		}
	}

	/* Format not found */
	if (i >= n)
		return -EINVAL;

	f->pixelformat = mxc_formats[i].fourcc;

	return 0;
}

static const struct mxc_jpeg_fmt *mxc_jpeg_find_format(u32 pixelformat)
{
	unsigned int k;

	for (k = 0; k < MXC_JPEG_NUM_FORMATS; k++) {
		const struct mxc_jpeg_fmt *fmt = &mxc_formats[k];

		if (fmt->fourcc == pixelformat)
			return fmt;
	}
	return NULL;
}

static enum mxc_jpeg_image_format mxc_jpeg_fourcc_to_imgfmt(u32 fourcc)
{
	switch (fourcc) {
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_Y012:
		return MXC_JPEG_GRAY;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_Y212:
		return MXC_JPEG_YUV422;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_P012:
	case V4L2_PIX_FMT_P012M:
		return MXC_JPEG_YUV420;
	case V4L2_PIX_FMT_YUV24:
	case V4L2_PIX_FMT_YUV48_12:
		return MXC_JPEG_YUV444;
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_BGR48_12:
		return MXC_JPEG_BGR;
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_ABGR64_12:
		return MXC_JPEG_ABGR;
	default:
		return MXC_JPEG_INVALID;
	}
}

static struct mxc_jpeg_q_data *mxc_jpeg_get_q_data(struct mxc_jpeg_ctx *ctx,
						   enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return &ctx->out_q;
	return &ctx->cap_q;
}

static void mxc_jpeg_addrs(struct mxc_jpeg_desc *desc,
			   struct vb2_buffer *raw_buf,
			   struct vb2_buffer *jpeg_buf, int offset)
{
	int img_fmt = desc->stm_ctrl & STM_CTRL_IMAGE_FORMAT_MASK;
	struct mxc_jpeg_ctx *ctx = vb2_get_drv_priv(raw_buf->vb2_queue);
	struct mxc_jpeg_q_data *q_data;

	q_data = mxc_jpeg_get_q_data(ctx, raw_buf->type);
	desc->buf_base0 = vb2_dma_contig_plane_dma_addr(raw_buf, 0);
	desc->buf_base1 = 0;
	if (img_fmt == STM_CTRL_IMAGE_FORMAT(MXC_JPEG_YUV420)) {
		if (raw_buf->num_planes == 2)
			desc->buf_base1 = vb2_dma_contig_plane_dma_addr(raw_buf, 1);
		else
			desc->buf_base1 = desc->buf_base0 + q_data->sizeimage[0];
	}
	desc->stm_bufbase = vb2_dma_contig_plane_dma_addr(jpeg_buf, 0) +
		offset;
}

static bool mxc_jpeg_is_extended_sequential(const struct mxc_jpeg_fmt *fmt)
{
	if (!fmt || !(fmt->flags & MXC_JPEG_FMT_TYPE_RAW))
		return false;

	if (fmt->precision > 8)
		return true;

	return false;
}

static void notify_eos(struct mxc_jpeg_ctx *ctx)
{
	const struct v4l2_event ev = {
		.type = V4L2_EVENT_EOS
	};

	dev_dbg(ctx->mxc_jpeg->dev, "Notify app event EOS reached");
	v4l2_event_queue_fh(&ctx->fh, &ev);
}

static void notify_src_chg(struct mxc_jpeg_ctx *ctx)
{
	const struct v4l2_event ev = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	dev_dbg(ctx->mxc_jpeg->dev, "Notify app event SRC_CH_RESOLUTION");
	v4l2_event_queue_fh(&ctx->fh, &ev);
}

static int mxc_get_free_slot(struct mxc_jpeg_slot_data *slot_data)
{
	if (!slot_data->used)
		return slot_data->slot;
	return -1;
}

static bool mxc_jpeg_alloc_slot_data(struct mxc_jpeg_dev *jpeg)
{
	struct mxc_jpeg_desc *desc;
	struct mxc_jpeg_desc *cfg_desc;
	void *cfg_stm;

	if (jpeg->slot_data.desc)
		goto skip_alloc; /* already allocated, reuse it */

	/* allocate descriptor for decoding/encoding phase */
	desc = dma_alloc_coherent(jpeg->dev,
				  sizeof(struct mxc_jpeg_desc),
				  &jpeg->slot_data.desc_handle,
				  GFP_ATOMIC);
	if (!desc)
		goto err;
	jpeg->slot_data.desc = desc;

	/* allocate descriptor for configuration phase (encoder only) */
	cfg_desc = dma_alloc_coherent(jpeg->dev,
				      sizeof(struct mxc_jpeg_desc),
				      &jpeg->slot_data.cfg_desc_handle,
				      GFP_ATOMIC);
	if (!cfg_desc)
		goto err;
	jpeg->slot_data.cfg_desc = cfg_desc;

	/* allocate configuration stream */
	cfg_stm = dma_alloc_coherent(jpeg->dev,
				     MXC_JPEG_MAX_CFG_STREAM,
				     &jpeg->slot_data.cfg_stream_handle,
				     GFP_ATOMIC);
	if (!cfg_stm)
		goto err;
	jpeg->slot_data.cfg_stream_vaddr = cfg_stm;

skip_alloc:
	jpeg->slot_data.used = true;

	return true;
err:
	dev_err(jpeg->dev, "Could not allocate descriptors for slot %d", jpeg->slot_data.slot);

	return false;
}

static void mxc_jpeg_free_slot_data(struct mxc_jpeg_dev *jpeg)
{
	/* free descriptor for decoding/encoding phase */
	dma_free_coherent(jpeg->dev, sizeof(struct mxc_jpeg_desc),
			  jpeg->slot_data.desc,
			  jpeg->slot_data.desc_handle);

	/* free descriptor for encoder configuration phase / decoder DHT */
	dma_free_coherent(jpeg->dev, sizeof(struct mxc_jpeg_desc),
			  jpeg->slot_data.cfg_desc,
			  jpeg->slot_data.cfg_desc_handle);

	/* free configuration stream */
	dma_free_coherent(jpeg->dev, MXC_JPEG_MAX_CFG_STREAM,
			  jpeg->slot_data.cfg_stream_vaddr,
			  jpeg->slot_data.cfg_stream_handle);

	jpeg->slot_data.used = false;
}

static void mxc_jpeg_check_and_set_last_buffer(struct mxc_jpeg_ctx *ctx,
					       struct vb2_v4l2_buffer *src_buf,
					       struct vb2_v4l2_buffer *dst_buf)
{
	if (v4l2_m2m_is_last_draining_src_buf(ctx->fh.m2m_ctx, src_buf)) {
		dst_buf->flags |= V4L2_BUF_FLAG_LAST;
		v4l2_m2m_mark_stopped(ctx->fh.m2m_ctx);
		notify_eos(ctx);
		ctx->header_parsed = false;
	}
}

static void mxc_jpeg_job_finish(struct mxc_jpeg_ctx *ctx, enum vb2_buffer_state state, bool reset)
{
	struct mxc_jpeg_dev *jpeg = ctx->mxc_jpeg;
	void __iomem *reg = jpeg->base_reg;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;

	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	mxc_jpeg_check_and_set_last_buffer(ctx, src_buf, dst_buf);
	v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_buf_done(src_buf, state);
	v4l2_m2m_buf_done(dst_buf, state);

	mxc_jpeg_disable_irq(reg, ctx->slot);
	jpeg->slot_data.used = false;
	if (reset)
		mxc_jpeg_sw_reset(reg);
}

static u32 mxc_jpeg_get_plane_size(struct mxc_jpeg_q_data *q_data, u32 plane_no)
{
	const struct mxc_jpeg_fmt *fmt = q_data->fmt;
	u32 size;
	int i;

	if (plane_no >= fmt->mem_planes)
		return 0;

	if (fmt->mem_planes == fmt->comp_planes)
		return q_data->sizeimage[plane_no];

	if (plane_no < fmt->mem_planes - 1)
		return q_data->sizeimage[plane_no];

	size = q_data->sizeimage[fmt->mem_planes - 1];

	/* Should be impossible given mxc_formats. */
	if (WARN_ON_ONCE(fmt->comp_planes > ARRAY_SIZE(q_data->sizeimage)))
		return size;

	for (i = fmt->mem_planes; i < fmt->comp_planes; i++)
		size += q_data->sizeimage[i];

	return size;
}

static irqreturn_t mxc_jpeg_dec_irq(int irq, void *priv)
{
	struct mxc_jpeg_dev *jpeg = priv;
	struct mxc_jpeg_ctx *ctx;
	void __iomem *reg = jpeg->base_reg;
	struct device *dev = jpeg->dev;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct mxc_jpeg_src_buf *jpeg_src_buf;
	enum vb2_buffer_state buf_state;
	u32 dec_ret, com_status;
	unsigned long payload;
	struct mxc_jpeg_q_data *q_data;
	enum v4l2_buf_type cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	unsigned int slot;

	spin_lock(&jpeg->hw_lock);

	com_status = readl(reg + COM_STATUS);
	slot = COM_STATUS_CUR_SLOT(com_status);
	dev_dbg(dev, "Irq %d on slot %d.\n", irq, slot);

	ctx = v4l2_m2m_get_curr_priv(jpeg->m2m_dev);
	if (WARN_ON(!ctx))
		goto job_unlock;

	if (slot != ctx->slot) {
		/* TODO investigate when adding multi-instance support */
		dev_warn(dev, "IRQ slot %d != context slot %d.\n",
			 slot, ctx->slot);
		goto job_unlock;
	}

	if (!jpeg->slot_data.used)
		goto job_unlock;

	dec_ret = readl(reg + MXC_SLOT_OFFSET(slot, SLOT_STATUS));
	writel(dec_ret, reg + MXC_SLOT_OFFSET(slot, SLOT_STATUS)); /* w1c */

	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	if (!dst_buf || !src_buf) {
		dev_err(dev, "No source or destination buffer.\n");
		goto job_unlock;
	}
	jpeg_src_buf = vb2_to_mxc_buf(&src_buf->vb2_buf);

	if (dec_ret & SLOT_STATUS_ENC_CONFIG_ERR) {
		u32 ret = readl(reg + CAST_STATUS12);

		dev_err(dev, "Encoder/decoder error, dec_ret = 0x%08x, status=0x%08x",
			dec_ret, ret);
		mxc_jpeg_clr_desc(reg, slot);
		mxc_jpeg_sw_reset(reg);
		buf_state = VB2_BUF_STATE_ERROR;
		goto buffers_done;
	}

	if (!(dec_ret & SLOT_STATUS_FRMDONE))
		goto job_unlock;

	if (jpeg->mode == MXC_JPEG_ENCODE &&
	    ctx->enc_state == MXC_JPEG_ENC_CONF) {
		q_data = mxc_jpeg_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
		ctx->enc_state = MXC_JPEG_ENCODING;
		dev_dbg(dev, "Encoder config finished. Start encoding...\n");
		mxc_jpeg_enc_set_quality(dev, reg, ctx->jpeg_quality);
		mxc_jpeg_enc_mode_go(dev, reg, mxc_jpeg_is_extended_sequential(q_data->fmt));
		goto job_unlock;
	}
	if (jpeg->mode == MXC_JPEG_DECODE && jpeg_src_buf->dht_needed) {
		jpeg_src_buf->dht_needed = false;
		dev_dbg(dev, "Decoder DHT cfg finished. Start decoding...\n");
		goto job_unlock;
	}

	if (jpeg->mode == MXC_JPEG_ENCODE) {
		payload = readl(reg + MXC_SLOT_OFFSET(slot, SLOT_BUF_PTR));
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, payload);
		dev_dbg(dev, "Encoding finished, payload size: %ld\n",
			payload);
	} else {
		q_data = mxc_jpeg_get_q_data(ctx, cap_type);
		payload = mxc_jpeg_get_plane_size(q_data, 0);
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, payload);
		vb2_set_plane_payload(&dst_buf->vb2_buf, 1, 0);
		if (q_data->fmt->mem_planes == 2) {
			payload = mxc_jpeg_get_plane_size(q_data, 1);
			vb2_set_plane_payload(&dst_buf->vb2_buf, 1, payload);
		}
		dev_dbg(dev, "Decoding finished, payload size: %ld + %ld\n",
			vb2_get_plane_payload(&dst_buf->vb2_buf, 0),
			vb2_get_plane_payload(&dst_buf->vb2_buf, 1));
	}

	/* short preview of the results */
	dev_dbg(dev, "src_buf preview: ");
	print_mxc_buf(jpeg, &src_buf->vb2_buf, 32);
	dev_dbg(dev, "dst_buf preview: ");
	print_mxc_buf(jpeg, &dst_buf->vb2_buf, 32);
	buf_state = VB2_BUF_STATE_DONE;

buffers_done:
	mxc_jpeg_job_finish(ctx, buf_state, false);
	spin_unlock(&jpeg->hw_lock);
	cancel_delayed_work(&ctx->task_timer);
	v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
	return IRQ_HANDLED;
job_unlock:
	spin_unlock(&jpeg->hw_lock);
	return IRQ_HANDLED;
}

static int mxc_jpeg_fixup_sof(struct mxc_jpeg_sof *sof,
			      u32 fourcc,
			      u16 w, u16 h)
{
	int sof_length;
	const struct mxc_jpeg_fmt *fmt = mxc_jpeg_find_format(fourcc);

	if (fmt)
		sof->precision = fmt->precision;
	else
		sof->precision = 8; /* TODO allow 8/12 bit precision*/
	sof->height = h;
	_bswap16(&sof->height);
	sof->width = w;
	_bswap16(&sof->width);

	switch (fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_P012:
	case V4L2_PIX_FMT_P012M:
		sof->components_no = 3;
		sof->comp[0].v = 0x2;
		sof->comp[0].h = 0x2;
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_Y212:
		sof->components_no = 3;
		sof->comp[0].v = 0x1;
		sof->comp[0].h = 0x2;
		break;
	case V4L2_PIX_FMT_YUV24:
	case V4L2_PIX_FMT_YUV48_12:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_BGR48_12:
	default:
		sof->components_no = 3;
		break;
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_ABGR64_12:
		sof->components_no = 4;
		break;
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_Y012:
		sof->components_no = 1;
		break;
	}
	sof_length = 8 + 3 * sof->components_no;
	sof->length = sof_length;
	_bswap16(&sof->length);

	return sof_length; /* not swaped */
}

static int mxc_jpeg_fixup_sos(struct mxc_jpeg_sos *sos,
			      u32 fourcc)
{
	int sos_length;
	u8 *sof_u8 = (u8 *)sos;

	switch (fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_P012:
	case V4L2_PIX_FMT_P012M:
		sos->components_no = 3;
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_Y212:
		sos->components_no = 3;
		break;
	case V4L2_PIX_FMT_YUV24:
	case V4L2_PIX_FMT_YUV48_12:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_BGR48_12:
	default:
		sos->components_no = 3;
		break;
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_ABGR64_12:
		sos->components_no = 4;
		break;
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_Y012:
		sos->components_no = 1;
		break;
	}
	sos_length = 6 + 2 * sos->components_no;
	sos->length = sos_length;
	_bswap16(&sos->length);

	/* SOS ignorable bytes, not so ignorable after all */
	sof_u8[sos_length - 1] = 0x0;
	sof_u8[sos_length - 2] = 0x3f;
	sof_u8[sos_length - 3] = 0x0;

	return sos_length; /* not swaped */
}

static unsigned int mxc_jpeg_setup_cfg_stream(void *cfg_stream_vaddr,
					      u32 fourcc,
					      u16 w, u16 h)
{
	/*
	 * There is a hardware issue that first 128 bytes of configuration data
	 * can't be loaded correctly.
	 * To avoid this issue, we need to write the configuration from
	 * an offset which should be no less than 0x80 (128 bytes).
	 */
	unsigned int offset = 0x80;
	u8 *cfg = (u8 *)cfg_stream_vaddr;
	struct mxc_jpeg_sof *sof;
	struct mxc_jpeg_sos *sos;
	const struct mxc_jpeg_fmt *fmt = mxc_jpeg_find_format(fourcc);

	if (!fmt)
		return 0;

	memcpy(cfg + offset, jpeg_soi, ARRAY_SIZE(jpeg_soi));
	offset += ARRAY_SIZE(jpeg_soi);

	if (fmt->is_rgb) {
		memcpy(cfg + offset, jpeg_app14, sizeof(jpeg_app14));
		offset += sizeof(jpeg_app14);
	} else {
		memcpy(cfg + offset, jpeg_app0, sizeof(jpeg_app0));
		offset += sizeof(jpeg_app0);
	}

	if (mxc_jpeg_is_extended_sequential(fmt)) {
		memcpy(cfg + offset, jpeg_dqt_extseq, sizeof(jpeg_dqt_extseq));
		offset += sizeof(jpeg_dqt_extseq);

		memcpy(cfg + offset, jpeg_sof_extseq, sizeof(jpeg_sof_extseq));
	} else {
		memcpy(cfg + offset, jpeg_dqt, sizeof(jpeg_dqt));
		offset += sizeof(jpeg_dqt);

		memcpy(cfg + offset, jpeg_sof_maximal, sizeof(jpeg_sof_maximal));
	}
	offset += 2; /* skip marker ID */
	sof = (struct mxc_jpeg_sof *)(cfg + offset);
	offset += mxc_jpeg_fixup_sof(sof, fourcc, w, h);

	if (mxc_jpeg_is_extended_sequential(fmt)) {
		memcpy(cfg + offset, jpeg_dht_extseq, sizeof(jpeg_dht_extseq));
		offset += sizeof(jpeg_dht_extseq);
	} else {
		memcpy(cfg + offset, jpeg_dht, sizeof(jpeg_dht));
		offset += sizeof(jpeg_dht);
	}

	memcpy(cfg + offset, jpeg_dri, sizeof(jpeg_dri));
	offset += sizeof(jpeg_dri);

	memcpy(cfg + offset, jpeg_sos_maximal, sizeof(jpeg_sos_maximal));
	offset += 2; /* skip marker ID */
	sos = (struct mxc_jpeg_sos *)(cfg + offset);
	offset += mxc_jpeg_fixup_sos(sos, fourcc);

	memcpy(cfg + offset, jpeg_image_red, sizeof(jpeg_image_red));
	offset += sizeof(jpeg_image_red);

	memcpy(cfg + offset, jpeg_eoi, sizeof(jpeg_eoi));
	offset += sizeof(jpeg_eoi);

	return offset;
}

static void mxc_jpeg_config_dec_desc(struct vb2_buffer *out_buf,
				     struct mxc_jpeg_ctx *ctx,
				     struct vb2_buffer *src_buf,
				     struct vb2_buffer *dst_buf)
{
	enum v4l2_buf_type cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	struct mxc_jpeg_q_data *q_data_cap;
	enum mxc_jpeg_image_format img_fmt;
	struct mxc_jpeg_dev *jpeg = ctx->mxc_jpeg;
	void __iomem *reg = jpeg->base_reg;
	unsigned int slot = ctx->slot;
	struct mxc_jpeg_desc *desc = jpeg->slot_data.desc;
	struct mxc_jpeg_desc *cfg_desc = jpeg->slot_data.cfg_desc;
	dma_addr_t desc_handle = jpeg->slot_data.desc_handle;
	dma_addr_t cfg_desc_handle = jpeg->slot_data.cfg_desc_handle;
	dma_addr_t cfg_stream_handle = jpeg->slot_data.cfg_stream_handle;
	unsigned int *cfg_size = &jpeg->slot_data.cfg_stream_size;
	void *cfg_stream_vaddr = jpeg->slot_data.cfg_stream_vaddr;
	struct mxc_jpeg_src_buf *jpeg_src_buf;

	jpeg_src_buf = vb2_to_mxc_buf(src_buf);

	/* setup the decoding descriptor */
	desc->next_descpt_ptr = 0; /* end of chain */
	q_data_cap = mxc_jpeg_get_q_data(ctx, cap_type);
	desc->imgsize = q_data_cap->w_adjusted << 16 | q_data_cap->h_adjusted;
	img_fmt = mxc_jpeg_fourcc_to_imgfmt(q_data_cap->fmt->fourcc);
	desc->stm_ctrl &= ~STM_CTRL_IMAGE_FORMAT(0xF); /* clear image format */
	desc->stm_ctrl |= STM_CTRL_IMAGE_FORMAT(img_fmt);
	desc->stm_ctrl |= STM_CTRL_BITBUF_PTR_CLR(1);
	if (mxc_jpeg_is_extended_sequential(jpeg_src_buf->fmt))
		desc->stm_ctrl |= STM_CTRL_PIXEL_PRECISION;
	else
		desc->stm_ctrl &= ~STM_CTRL_PIXEL_PRECISION;
	desc->line_pitch = q_data_cap->bytesperline[0];
	mxc_jpeg_addrs(desc, dst_buf, src_buf, 0);
	mxc_jpeg_set_bufsize(desc, ALIGN(vb2_plane_size(src_buf, 0), 1024));
	print_descriptor_info(jpeg->dev, desc);

	if (!jpeg_src_buf->dht_needed) {
		/* validate the decoding descriptor */
		mxc_jpeg_set_desc(desc_handle, reg, slot);
		return;
	}

	/*
	 * if a default huffman table is needed, use the config descriptor to
	 * inject a DHT, by chaining it before the decoding descriptor
	 */
	*cfg_size = mxc_jpeg_setup_cfg_stream(cfg_stream_vaddr,
					      V4L2_PIX_FMT_YUYV,
					      MXC_JPEG_MIN_WIDTH,
					      MXC_JPEG_MIN_HEIGHT);
	cfg_desc->next_descpt_ptr = desc_handle | MXC_NXT_DESCPT_EN;
	cfg_desc->buf_base0 = vb2_dma_contig_plane_dma_addr(dst_buf, 0);
	cfg_desc->buf_base1 = 0;
	cfg_desc->imgsize = MXC_JPEG_MIN_WIDTH << 16;
	cfg_desc->imgsize |= MXC_JPEG_MIN_HEIGHT;
	cfg_desc->line_pitch = MXC_JPEG_MIN_WIDTH * 2;
	cfg_desc->stm_ctrl = STM_CTRL_IMAGE_FORMAT(MXC_JPEG_YUV422);
	cfg_desc->stm_ctrl |= STM_CTRL_BITBUF_PTR_CLR(1);
	cfg_desc->stm_bufbase = cfg_stream_handle;
	cfg_desc->stm_bufsize = ALIGN(*cfg_size, 1024);
	print_descriptor_info(jpeg->dev, cfg_desc);

	/* validate the configuration descriptor */
	mxc_jpeg_set_desc(cfg_desc_handle, reg, slot);
}

static void mxc_jpeg_config_enc_desc(struct vb2_buffer *out_buf,
				     struct mxc_jpeg_ctx *ctx,
				     struct vb2_buffer *src_buf,
				     struct vb2_buffer *dst_buf)
{
	struct mxc_jpeg_dev *jpeg = ctx->mxc_jpeg;
	void __iomem *reg = jpeg->base_reg;
	unsigned int slot = ctx->slot;
	struct mxc_jpeg_desc *desc = jpeg->slot_data.desc;
	struct mxc_jpeg_desc *cfg_desc = jpeg->slot_data.cfg_desc;
	dma_addr_t desc_handle = jpeg->slot_data.desc_handle;
	dma_addr_t cfg_desc_handle = jpeg->slot_data.cfg_desc_handle;
	void *cfg_stream_vaddr = jpeg->slot_data.cfg_stream_vaddr;
	struct mxc_jpeg_q_data *q_data;
	enum mxc_jpeg_image_format img_fmt;
	int w, h;

	q_data = mxc_jpeg_get_q_data(ctx, src_buf->vb2_queue->type);

	jpeg->slot_data.cfg_stream_size =
			mxc_jpeg_setup_cfg_stream(cfg_stream_vaddr,
						  q_data->fmt->fourcc,
						  q_data->crop.width,
						  q_data->crop.height);

	/* chain the config descriptor with the encoding descriptor */
	cfg_desc->next_descpt_ptr = desc_handle | MXC_NXT_DESCPT_EN;

	cfg_desc->buf_base0 = jpeg->slot_data.cfg_stream_handle;
	cfg_desc->buf_base1 = 0;
	cfg_desc->line_pitch = 0;
	cfg_desc->stm_bufbase = 0; /* no output expected */
	cfg_desc->stm_bufsize = 0x0;
	cfg_desc->imgsize = 0;
	cfg_desc->stm_ctrl = STM_CTRL_CONFIG_MOD(1);
	cfg_desc->stm_ctrl |= STM_CTRL_BITBUF_PTR_CLR(1);

	desc->next_descpt_ptr = 0; /* end of chain */

	/* use adjusted resolution for CAST IP job */
	w = q_data->crop.width;
	h = q_data->crop.height;
	v4l_bound_align_image(&w, w, MXC_JPEG_MAX_WIDTH, q_data->fmt->h_align,
			      &h, h, MXC_JPEG_MAX_HEIGHT, q_data->fmt->v_align, 0);
	mxc_jpeg_set_res(desc, w, h);
	mxc_jpeg_set_line_pitch(desc, q_data->bytesperline[0]);
	mxc_jpeg_set_bufsize(desc, ALIGN(vb2_plane_size(dst_buf, 0), 1024));
	img_fmt = mxc_jpeg_fourcc_to_imgfmt(q_data->fmt->fourcc);
	if (img_fmt == MXC_JPEG_INVALID)
		dev_err(jpeg->dev, "No valid image format detected\n");
	desc->stm_ctrl = STM_CTRL_CONFIG_MOD(0) |
			 STM_CTRL_IMAGE_FORMAT(img_fmt);
	desc->stm_ctrl |= STM_CTRL_BITBUF_PTR_CLR(1);
	if (mxc_jpeg_is_extended_sequential(q_data->fmt))
		desc->stm_ctrl |= STM_CTRL_PIXEL_PRECISION;
	else
		desc->stm_ctrl &= ~STM_CTRL_PIXEL_PRECISION;
	mxc_jpeg_addrs(desc, src_buf, dst_buf, 0);
	dev_dbg(jpeg->dev, "cfg_desc:\n");
	print_descriptor_info(jpeg->dev, cfg_desc);
	dev_dbg(jpeg->dev, "enc desc:\n");
	print_descriptor_info(jpeg->dev, desc);
	print_wrapper_info(jpeg->dev, reg);
	print_cast_status(jpeg->dev, reg, MXC_JPEG_ENCODE);

	/* validate the configuration descriptor */
	mxc_jpeg_set_desc(cfg_desc_handle, reg, slot);
}

static const struct mxc_jpeg_fmt *mxc_jpeg_get_sibling_format(const struct mxc_jpeg_fmt *fmt)
{
	int i;

	for (i = 0; i < MXC_JPEG_NUM_FORMATS; i++) {
		if (mxc_formats[i].subsampling == fmt->subsampling &&
		    mxc_formats[i].nc == fmt->nc &&
		    mxc_formats[i].precision == fmt->precision &&
		    mxc_formats[i].is_rgb == fmt->is_rgb &&
		    mxc_formats[i].fourcc != fmt->fourcc)
			return &mxc_formats[i];
	}

	return NULL;
}

static bool mxc_jpeg_compare_format(const struct mxc_jpeg_fmt *fmt1,
				    const struct mxc_jpeg_fmt *fmt2)
{
	if (fmt1 == fmt2)
		return true;
	if (mxc_jpeg_get_sibling_format(fmt1) == fmt2)
		return true;
	return false;
}

static void mxc_jpeg_set_last_buffer(struct mxc_jpeg_ctx *ctx)
{
	struct vb2_v4l2_buffer *next_dst_buf;

	next_dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	if (!next_dst_buf) {
		ctx->fh.m2m_ctx->is_draining = true;
		ctx->fh.m2m_ctx->next_buf_last = true;
		return;
	}

	v4l2_m2m_last_buffer_done(ctx->fh.m2m_ctx, next_dst_buf);
}

static bool mxc_jpeg_source_change(struct mxc_jpeg_ctx *ctx,
				   struct mxc_jpeg_src_buf *jpeg_src_buf)
{
	struct device *dev = ctx->mxc_jpeg->dev;
	struct mxc_jpeg_q_data *q_data_cap;

	if (!jpeg_src_buf->fmt)
		return false;

	q_data_cap = mxc_jpeg_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (mxc_jpeg_compare_format(q_data_cap->fmt, jpeg_src_buf->fmt))
		jpeg_src_buf->fmt = q_data_cap->fmt;
	if (ctx->need_initial_source_change_evt ||
	    q_data_cap->fmt != jpeg_src_buf->fmt ||
	    q_data_cap->w != jpeg_src_buf->w ||
	    q_data_cap->h != jpeg_src_buf->h) {
		dev_dbg(dev, "Detected jpeg res=(%dx%d)->(%dx%d), pixfmt=%c%c%c%c\n",
			q_data_cap->w, q_data_cap->h,
			jpeg_src_buf->w, jpeg_src_buf->h,
			(jpeg_src_buf->fmt->fourcc & 0xff),
			(jpeg_src_buf->fmt->fourcc >>  8) & 0xff,
			(jpeg_src_buf->fmt->fourcc >> 16) & 0xff,
			(jpeg_src_buf->fmt->fourcc >> 24) & 0xff);

		/*
		 * set-up the capture queue with the pixelformat and resolution
		 * detected from the jpeg output stream
		 */
		q_data_cap->w = jpeg_src_buf->w;
		q_data_cap->h = jpeg_src_buf->h;
		q_data_cap->fmt = jpeg_src_buf->fmt;
		q_data_cap->w_adjusted = q_data_cap->w;
		q_data_cap->h_adjusted = q_data_cap->h;
		q_data_cap->crop.left = 0;
		q_data_cap->crop.top = 0;
		q_data_cap->crop.width = jpeg_src_buf->w;
		q_data_cap->crop.height = jpeg_src_buf->h;
		q_data_cap->bytesperline[0] = 0;
		q_data_cap->bytesperline[1] = 0;

		/*
		 * align up the resolution for CAST IP,
		 * but leave the buffer resolution unchanged
		 */
		v4l_bound_align_image(&q_data_cap->w_adjusted,
				      q_data_cap->w_adjusted,  /* adjust up */
				      MXC_JPEG_MAX_WIDTH,
				      q_data_cap->fmt->h_align,
				      &q_data_cap->h_adjusted,
				      q_data_cap->h_adjusted, /* adjust up */
				      MXC_JPEG_MAX_HEIGHT,
				      q_data_cap->fmt->v_align,
				      0);

		/* setup bytesperline/sizeimage for capture queue */
		mxc_jpeg_bytesperline(q_data_cap, jpeg_src_buf->fmt->precision);
		mxc_jpeg_sizeimage(q_data_cap);
		notify_src_chg(ctx);
		ctx->source_change = 1;
		ctx->need_initial_source_change_evt = false;
		if (vb2_is_streaming(v4l2_m2m_get_dst_vq(ctx->fh.m2m_ctx)))
			mxc_jpeg_set_last_buffer(ctx);
	}

	return ctx->source_change ? true : false;
}

static int mxc_jpeg_job_ready(void *priv)
{
	struct mxc_jpeg_ctx *ctx = priv;

	return ctx->source_change ? 0 : 1;
}

static void mxc_jpeg_device_run_timeout(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mxc_jpeg_ctx *ctx = container_of(dwork, struct mxc_jpeg_ctx, task_timer);
	struct mxc_jpeg_dev *jpeg = ctx->mxc_jpeg;
	unsigned long flags;

	spin_lock_irqsave(&ctx->mxc_jpeg->hw_lock, flags);
	if (ctx->mxc_jpeg->slot_data.used) {
		dev_warn(jpeg->dev, "%s timeout, cancel it\n",
			 ctx->mxc_jpeg->mode == MXC_JPEG_DECODE ? "decode" : "encode");
		mxc_jpeg_job_finish(ctx, VB2_BUF_STATE_ERROR, true);
		v4l2_m2m_job_finish(ctx->mxc_jpeg->m2m_dev, ctx->fh.m2m_ctx);
	}
	spin_unlock_irqrestore(&ctx->mxc_jpeg->hw_lock, flags);
}

static void mxc_jpeg_device_run(void *priv)
{
	struct mxc_jpeg_ctx *ctx = priv;
	struct mxc_jpeg_dev *jpeg = ctx->mxc_jpeg;
	void __iomem *reg = jpeg->base_reg;
	struct device *dev = jpeg->dev;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	unsigned long flags;
	struct mxc_jpeg_q_data *q_data_cap, *q_data_out;
	struct mxc_jpeg_src_buf *jpeg_src_buf;

	spin_lock_irqsave(&ctx->mxc_jpeg->hw_lock, flags);
	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	if (!src_buf || !dst_buf) {
		dev_err(dev, "Null src or dst buf\n");
		goto end;
	}

	q_data_cap = mxc_jpeg_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (!q_data_cap)
		goto end;
	q_data_out = mxc_jpeg_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	if (!q_data_out)
		goto end;
	src_buf->sequence = q_data_out->sequence++;
	dst_buf->sequence = q_data_cap->sequence++;

	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, true);

	jpeg_src_buf = vb2_to_mxc_buf(&src_buf->vb2_buf);
	if (q_data_cap->fmt->mem_planes != dst_buf->vb2_buf.num_planes) {
		dev_err(dev, "Capture format %s has %d planes, but capture buffer has %d planes\n",
			q_data_cap->fmt->name, q_data_cap->fmt->mem_planes,
			dst_buf->vb2_buf.num_planes);
		jpeg_src_buf->jpeg_parse_error = true;
	}
	if (jpeg_src_buf->jpeg_parse_error) {
		mxc_jpeg_check_and_set_last_buffer(ctx, src_buf, dst_buf);
		v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
		spin_unlock_irqrestore(&ctx->mxc_jpeg->hw_lock, flags);
		v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);

		return;
	}
	if (ctx->mxc_jpeg->mode == MXC_JPEG_DECODE) {
		if (ctx->source_change || mxc_jpeg_source_change(ctx, jpeg_src_buf)) {
			spin_unlock_irqrestore(&ctx->mxc_jpeg->hw_lock, flags);
			v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
			return;
		}
	}

	mxc_jpeg_enable(reg);
	mxc_jpeg_set_l_endian(reg, 1);

	ctx->slot = mxc_get_free_slot(&jpeg->slot_data);
	if (ctx->slot < 0) {
		dev_err(dev, "No more free slots\n");
		goto end;
	}
	if (!mxc_jpeg_alloc_slot_data(jpeg)) {
		dev_err(dev, "Cannot allocate slot data\n");
		goto end;
	}

	mxc_jpeg_enable_slot(reg, ctx->slot);
	mxc_jpeg_enable_irq(reg, ctx->slot);

	if (jpeg->mode == MXC_JPEG_ENCODE) {
		dev_dbg(dev, "Encoding on slot %d\n", ctx->slot);
		ctx->enc_state = MXC_JPEG_ENC_CONF;
		mxc_jpeg_config_enc_desc(&dst_buf->vb2_buf, ctx,
					 &src_buf->vb2_buf, &dst_buf->vb2_buf);
		/* start config phase */
		mxc_jpeg_enc_mode_conf(dev, reg,
				       mxc_jpeg_is_extended_sequential(q_data_out->fmt));
	} else {
		dev_dbg(dev, "Decoding on slot %d\n", ctx->slot);
		print_mxc_buf(jpeg, &src_buf->vb2_buf, 0);
		mxc_jpeg_config_dec_desc(&dst_buf->vb2_buf, ctx,
					 &src_buf->vb2_buf, &dst_buf->vb2_buf);
		mxc_jpeg_dec_mode_go(dev, reg);
	}
	schedule_delayed_work(&ctx->task_timer, msecs_to_jiffies(hw_timeout));
end:
	spin_unlock_irqrestore(&ctx->mxc_jpeg->hw_lock, flags);
}

static int mxc_jpeg_decoder_cmd(struct file *file, void *priv,
				struct v4l2_decoder_cmd *cmd)
{
	struct v4l2_fh *fh = file->private_data;
	struct mxc_jpeg_ctx *ctx = mxc_jpeg_fh_to_ctx(fh);
	unsigned long flags;
	int ret;

	ret = v4l2_m2m_ioctl_try_decoder_cmd(file, fh, cmd);
	if (ret < 0)
		return ret;

	if (!vb2_is_streaming(v4l2_m2m_get_src_vq(fh->m2m_ctx)))
		return 0;

	spin_lock_irqsave(&ctx->mxc_jpeg->hw_lock, flags);
	ret = v4l2_m2m_ioctl_decoder_cmd(file, priv, cmd);
	spin_unlock_irqrestore(&ctx->mxc_jpeg->hw_lock, flags);
	if (ret < 0)
		return ret;

	if (cmd->cmd == V4L2_DEC_CMD_STOP &&
	    v4l2_m2m_has_stopped(fh->m2m_ctx)) {
		notify_eos(ctx);
		ctx->header_parsed = false;
	}

	if (cmd->cmd == V4L2_DEC_CMD_START &&
	    v4l2_m2m_has_stopped(fh->m2m_ctx))
		vb2_clear_last_buffer_dequeued(&fh->m2m_ctx->cap_q_ctx.q);
	return 0;
}

static int mxc_jpeg_encoder_cmd(struct file *file, void *priv,
				struct v4l2_encoder_cmd *cmd)
{
	struct v4l2_fh *fh = file->private_data;
	struct mxc_jpeg_ctx *ctx = mxc_jpeg_fh_to_ctx(fh);
	unsigned long flags;
	int ret;

	ret = v4l2_m2m_ioctl_try_encoder_cmd(file, fh, cmd);
	if (ret < 0)
		return ret;

	if (!vb2_is_streaming(v4l2_m2m_get_src_vq(fh->m2m_ctx)) ||
	    !vb2_is_streaming(v4l2_m2m_get_dst_vq(fh->m2m_ctx)))
		return 0;

	spin_lock_irqsave(&ctx->mxc_jpeg->hw_lock, flags);
	ret = v4l2_m2m_ioctl_encoder_cmd(file, fh, cmd);
	spin_unlock_irqrestore(&ctx->mxc_jpeg->hw_lock, flags);
	if (ret < 0)
		return 0;

	if (cmd->cmd == V4L2_ENC_CMD_STOP &&
	    v4l2_m2m_has_stopped(fh->m2m_ctx))
		notify_eos(ctx);

	if (cmd->cmd == V4L2_ENC_CMD_START &&
	    v4l2_m2m_has_stopped(fh->m2m_ctx))
		vb2_clear_last_buffer_dequeued(&fh->m2m_ctx->cap_q_ctx.q);

	return 0;
}

static int mxc_jpeg_queue_setup(struct vb2_queue *q,
				unsigned int *nbuffers,
				unsigned int *nplanes,
				unsigned int sizes[],
				struct device *alloc_ctxs[])
{
	struct mxc_jpeg_ctx *ctx = vb2_get_drv_priv(q);
	struct mxc_jpeg_q_data *q_data = NULL;
	int i;

	q_data = mxc_jpeg_get_q_data(ctx, q->type);
	if (!q_data)
		return -EINVAL;

	/* Handle CREATE_BUFS situation - *nplanes != 0 */
	if (*nplanes) {
		if (*nplanes != q_data->fmt->mem_planes)
			return -EINVAL;
		for (i = 0; i < *nplanes; i++) {
			if (sizes[i] < mxc_jpeg_get_plane_size(q_data, i))
				return -EINVAL;
		}
		return 0;
	}

	/* Handle REQBUFS situation */
	*nplanes = q_data->fmt->mem_planes;
	for (i = 0; i < *nplanes; i++)
		sizes[i] = mxc_jpeg_get_plane_size(q_data, i);

	if (V4L2_TYPE_IS_OUTPUT(q->type))
		ctx->need_initial_source_change_evt = true;

	return 0;
}

static int mxc_jpeg_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct mxc_jpeg_ctx *ctx = vb2_get_drv_priv(q);
	struct mxc_jpeg_q_data *q_data = mxc_jpeg_get_q_data(ctx, q->type);
	int ret;

	v4l2_m2m_update_start_streaming_state(ctx->fh.m2m_ctx, q);

	if (ctx->mxc_jpeg->mode == MXC_JPEG_DECODE && V4L2_TYPE_IS_CAPTURE(q->type))
		ctx->source_change = 0;
	dev_dbg(ctx->mxc_jpeg->dev, "Start streaming ctx=%p", ctx);
	q_data->sequence = 0;

	if (V4L2_TYPE_IS_CAPTURE(q->type))
		ctx->need_initial_source_change_evt = false;

	ret = pm_runtime_resume_and_get(ctx->mxc_jpeg->dev);
	if (ret < 0) {
		dev_err(ctx->mxc_jpeg->dev, "Failed to power up jpeg\n");
		return ret;
	}

	return 0;
}

static void mxc_jpeg_stop_streaming(struct vb2_queue *q)
{
	struct mxc_jpeg_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *vbuf;

	dev_dbg(ctx->mxc_jpeg->dev, "Stop streaming ctx=%p", ctx);

	/* Release all active buffers */
	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(q->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (!vbuf)
			break;
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
	}

	v4l2_m2m_update_stop_streaming_state(ctx->fh.m2m_ctx, q);
	/* if V4L2_DEC_CMD_STOP is sent before the source change triggered,
	 * restore the is_draining flag
	 */
	if (V4L2_TYPE_IS_CAPTURE(q->type) && ctx->source_change && ctx->fh.m2m_ctx->last_src_buf)
		ctx->fh.m2m_ctx->is_draining = true;

	if (V4L2_TYPE_IS_OUTPUT(q->type) &&
	    v4l2_m2m_has_stopped(ctx->fh.m2m_ctx)) {
		notify_eos(ctx);
		ctx->header_parsed = false;
	}

	pm_runtime_put_sync(&ctx->mxc_jpeg->pdev->dev);
}

static int mxc_jpeg_valid_comp_id(struct device *dev,
				  struct mxc_jpeg_sof *sof,
				  struct mxc_jpeg_sos *sos)
{
	int valid = 1;
	int i;

	/*
	 * there's a limitation in the IP that the component IDs must be
	 * between 0..4, if they are not, let's patch them
	 */
	for (i = 0; i < sof->components_no; i++)
		if (sof->comp[i].id > MXC_JPEG_MAX_COMPONENTS) {
			valid = 0;
			dev_err(dev, "Component %d has invalid ID: %d",
				i, sof->comp[i].id);
		}
	if (!valid)
		/* patch all comp IDs if at least one is invalid */
		for (i = 0; i < sof->components_no; i++) {
			dev_warn(dev, "Component %d ID patched to: %d",
				 i, i + 1);
			sof->comp[i].id = i + 1;
			sos->comp[i].id = i + 1;
		}

	return valid;
}

static bool mxc_jpeg_match_image_format(const struct mxc_jpeg_fmt *fmt,
					const struct v4l2_jpeg_header *header)
{
	if (fmt->subsampling != header->frame.subsampling ||
	    fmt->nc != header->frame.num_components ||
	    fmt->precision != header->frame.precision)
		return false;

	/*
	 * If the transform flag from APP14 marker is 0, images that are
	 * encoded with 3 components have RGB colorspace, see Recommendation
	 * ITU-T T.872 chapter 6.5.3 APP14 marker segment for colour encoding
	 */
	if (header->frame.subsampling == V4L2_JPEG_CHROMA_SUBSAMPLING_444) {
		u8 is_rgb = header->app14_tf == V4L2_JPEG_APP14_TF_CMYK_RGB ? 1 : 0;

		if (is_rgb != fmt->is_rgb)
			return false;
	}
	return true;
}

static u32 mxc_jpeg_get_image_format(struct device *dev,
				     const struct v4l2_jpeg_header *header)
{
	int i;
	u32 fourcc = 0;

	for (i = 0; i < MXC_JPEG_NUM_FORMATS; i++) {
		if (mxc_jpeg_match_image_format(&mxc_formats[i], header)) {
			fourcc = mxc_formats[i].fourcc;
			break;
		}
	}
	if (fourcc == 0) {
		dev_err(dev,
			"Could not identify image format nc=%d, subsampling=%d, precision=%d\n",
			header->frame.num_components,
			header->frame.subsampling,
			header->frame.precision);
		return fourcc;
	}

	return fourcc;
}

static void mxc_jpeg_bytesperline(struct mxc_jpeg_q_data *q, u32 precision)
{
	u32 bytesperline[2];

	bytesperline[0] = q->bytesperline[0];
	bytesperline[1] = q->bytesperline[0];	/*imx-jpeg only support the same line pitch*/
	v4l_bound_align_image(&bytesperline[0], 0, MXC_JPEG_MAX_LINE, 2,
			      &bytesperline[1], 0, MXC_JPEG_MAX_LINE, 2,
			      0);

	/* Bytes distance between the leftmost pixels in two adjacent lines */
	if (q->fmt->fourcc == V4L2_PIX_FMT_JPEG) {
		/* bytesperline unused for compressed formats */
		q->bytesperline[0] = 0;
		q->bytesperline[1] = 0;
	} else if (q->fmt->subsampling == V4L2_JPEG_CHROMA_SUBSAMPLING_420) {
		/* When the image format is planar the bytesperline value
		 * applies to the first plane and is divided by the same factor
		 * as the width field for the other planes
		 */
		q->bytesperline[0] = q->w_adjusted * DIV_ROUND_UP(precision, 8);
		q->bytesperline[1] = q->bytesperline[0];
	} else if (q->fmt->subsampling == V4L2_JPEG_CHROMA_SUBSAMPLING_422) {
		q->bytesperline[0] = q->w_adjusted * DIV_ROUND_UP(precision, 8) * 2;
		q->bytesperline[1] = 0;
	} else if (q->fmt->subsampling == V4L2_JPEG_CHROMA_SUBSAMPLING_444) {
		q->bytesperline[0] = q->w_adjusted * DIV_ROUND_UP(precision, 8) * q->fmt->nc;
		q->bytesperline[1] = 0;
	} else {
		/* grayscale */
		q->bytesperline[0] = q->w_adjusted * DIV_ROUND_UP(precision, 8);
		q->bytesperline[1] = 0;
	}

	if (q->fmt->fourcc != V4L2_PIX_FMT_JPEG) {
		q->bytesperline[0] = max(q->bytesperline[0], bytesperline[0]);
		if (q->fmt->mem_planes > 1)
			q->bytesperline[1] = max(q->bytesperline[1], bytesperline[1]);
	}
}

static void mxc_jpeg_sizeimage(struct mxc_jpeg_q_data *q)
{
	if (q->fmt->fourcc == V4L2_PIX_FMT_JPEG) {
		/* if no sizeimage from user, assume worst jpeg compression */
		if (!q->sizeimage[0])
			q->sizeimage[0] = 6 * q->w * q->h;
		q->sizeimage[1] = 0;

		if (q->sizeimage[0] > MXC_JPEG_MAX_SIZEIMAGE)
			q->sizeimage[0] = MXC_JPEG_MAX_SIZEIMAGE;

		/* jpeg stream size must be multiple of 1K */
		q->sizeimage[0] = ALIGN(q->sizeimage[0], 1024);
	} else {
		q->sizeimage[0] = q->bytesperline[0] * q->h_adjusted;
		q->sizeimage[1] = 0;
		if (q->fmt->subsampling == V4L2_JPEG_CHROMA_SUBSAMPLING_420)
			q->sizeimage[1] = q->sizeimage[0] / 2;
	}
}

static int mxc_jpeg_parse(struct mxc_jpeg_ctx *ctx, struct vb2_buffer *vb)
{
	struct device *dev = ctx->mxc_jpeg->dev;
	struct mxc_jpeg_q_data *q_data_out;
	struct mxc_jpeg_q_data *q_data_cap;
	u32 fourcc;
	struct v4l2_jpeg_header header;
	struct mxc_jpeg_sof *psof = NULL;
	struct mxc_jpeg_sos *psos = NULL;
	struct mxc_jpeg_src_buf *jpeg_src_buf = vb2_to_mxc_buf(vb);
	u8 *src_addr = (u8 *)vb2_plane_vaddr(vb, 0);
	u32 size = vb2_get_plane_payload(vb, 0);
	int ret;

	memset(&header, 0, sizeof(header));
	ret = v4l2_jpeg_parse_header((void *)src_addr, size, &header);
	if (ret < 0) {
		dev_err(dev, "Error parsing JPEG stream markers\n");
		return ret;
	}

	/* if DHT marker present, no need to inject default one */
	jpeg_src_buf->dht_needed = (header.num_dht == 0);

	q_data_out = mxc_jpeg_get_q_data(ctx,
					 V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	q_data_out->w = header.frame.width;
	q_data_out->h = header.frame.height;
	if (header.frame.width > MXC_JPEG_MAX_WIDTH ||
	    header.frame.height > MXC_JPEG_MAX_HEIGHT) {
		dev_err(dev, "JPEG width or height should be <= 8192: %dx%d\n",
			header.frame.width, header.frame.height);
		return -EINVAL;
	}
	if (header.frame.width < MXC_JPEG_MIN_WIDTH ||
	    header.frame.height < MXC_JPEG_MIN_HEIGHT) {
		dev_err(dev, "JPEG width or height should be > 64: %dx%d\n",
			header.frame.width, header.frame.height);
		return -EINVAL;
	}
	if (header.frame.num_components > V4L2_JPEG_MAX_COMPONENTS) {
		dev_err(dev, "JPEG number of components should be <=%d",
			V4L2_JPEG_MAX_COMPONENTS);
		return -EINVAL;
	}
	/* check and, if necessary, patch component IDs*/
	psof = (struct mxc_jpeg_sof *)header.sof.start;
	psos = (struct mxc_jpeg_sos *)header.sos.start;
	if (!mxc_jpeg_valid_comp_id(dev, psof, psos))
		dev_warn(dev, "JPEG component ids should be 0-3 or 1-4");

	q_data_cap = mxc_jpeg_get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (q_data_cap->fmt && mxc_jpeg_match_image_format(q_data_cap->fmt, &header))
		fourcc = q_data_cap->fmt->fourcc;
	else
		fourcc = mxc_jpeg_get_image_format(dev, &header);
	if (fourcc == 0)
		return -EINVAL;

	jpeg_src_buf->fmt = mxc_jpeg_find_format(fourcc);
	jpeg_src_buf->w = header.frame.width;
	jpeg_src_buf->h = header.frame.height;
	ctx->header_parsed = true;

	if (!v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx))
		mxc_jpeg_source_change(ctx, jpeg_src_buf);

	return 0;
}

static void mxc_jpeg_buf_queue(struct vb2_buffer *vb)
{
	int ret;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct mxc_jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct mxc_jpeg_src_buf *jpeg_src_buf;

	if (V4L2_TYPE_IS_CAPTURE(vb->vb2_queue->type) &&
	    vb2_is_streaming(vb->vb2_queue) &&
	    v4l2_m2m_dst_buf_is_last(ctx->fh.m2m_ctx)) {
		struct mxc_jpeg_q_data *q_data;

		q_data = mxc_jpeg_get_q_data(ctx, vb->vb2_queue->type);
		vbuf->field = V4L2_FIELD_NONE;
		vbuf->sequence = q_data->sequence++;
		v4l2_m2m_last_buffer_done(ctx->fh.m2m_ctx, vbuf);
		notify_eos(ctx);
		ctx->header_parsed = false;
		return;
	}

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		goto end;

	/* for V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE */
	if (ctx->mxc_jpeg->mode != MXC_JPEG_DECODE)
		goto end;

	jpeg_src_buf = vb2_to_mxc_buf(vb);
	jpeg_src_buf->jpeg_parse_error = false;
	ret = mxc_jpeg_parse(ctx, vb);
	if (ret)
		jpeg_src_buf->jpeg_parse_error = true;

end:
	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int mxc_jpeg_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;

	return 0;
}

static int mxc_jpeg_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct mxc_jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct mxc_jpeg_q_data *q_data = NULL;
	struct device *dev = ctx->mxc_jpeg->dev;
	unsigned long sizeimage;
	int i;

	vbuf->field = V4L2_FIELD_NONE;

	q_data = mxc_jpeg_get_q_data(ctx, vb->vb2_queue->type);
	if (!q_data)
		return -EINVAL;
	for (i = 0; i < q_data->fmt->mem_planes; i++) {
		sizeimage = mxc_jpeg_get_plane_size(q_data, i);
		if (!ctx->source_change && vb2_plane_size(vb, i) < sizeimage) {
			dev_err(dev, "plane %d too small (%lu < %lu)",
				i, vb2_plane_size(vb, i), sizeimage);
			return -EINVAL;
		}
	}
	if (V4L2_TYPE_IS_CAPTURE(vb->vb2_queue->type)) {
		vb2_set_plane_payload(vb, 0, 0);
		vb2_set_plane_payload(vb, 1, 0);
	}
	return 0;
}

static const struct vb2_ops mxc_jpeg_qops = {
	.queue_setup		= mxc_jpeg_queue_setup,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.buf_out_validate	= mxc_jpeg_buf_out_validate,
	.buf_prepare		= mxc_jpeg_buf_prepare,
	.start_streaming	= mxc_jpeg_start_streaming,
	.stop_streaming		= mxc_jpeg_stop_streaming,
	.buf_queue		= mxc_jpeg_buf_queue,
};

static int mxc_jpeg_queue_init(void *priv, struct vb2_queue *src_vq,
			       struct vb2_queue *dst_vq)
{
	struct mxc_jpeg_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct mxc_jpeg_src_buf);
	src_vq->ops = &mxc_jpeg_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->mxc_jpeg->lock;
	src_vq->dev = ctx->mxc_jpeg->dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &mxc_jpeg_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->mxc_jpeg->lock;
	dst_vq->dev = ctx->mxc_jpeg->dev;

	ret = vb2_queue_init(dst_vq);
	return ret;
}

static void mxc_jpeg_set_default_params(struct mxc_jpeg_ctx *ctx)
{
	struct mxc_jpeg_q_data *out_q = &ctx->out_q;
	struct mxc_jpeg_q_data *cap_q = &ctx->cap_q;
	struct mxc_jpeg_q_data *q[2] = {out_q, cap_q};
	int i;

	if (ctx->mxc_jpeg->mode == MXC_JPEG_ENCODE) {
		out_q->fmt = mxc_jpeg_find_format(MXC_JPEG_DEFAULT_PFMT);
		cap_q->fmt = mxc_jpeg_find_format(V4L2_PIX_FMT_JPEG);
	} else {
		out_q->fmt = mxc_jpeg_find_format(V4L2_PIX_FMT_JPEG);
		cap_q->fmt = mxc_jpeg_find_format(MXC_JPEG_DEFAULT_PFMT);
	}

	for (i = 0; i < 2; i++) {
		q[i]->w = MXC_JPEG_DEFAULT_WIDTH;
		q[i]->h = MXC_JPEG_DEFAULT_HEIGHT;
		q[i]->w_adjusted = MXC_JPEG_DEFAULT_WIDTH;
		q[i]->h_adjusted = MXC_JPEG_DEFAULT_HEIGHT;
		q[i]->crop.left = 0;
		q[i]->crop.top = 0;
		q[i]->crop.width = MXC_JPEG_DEFAULT_WIDTH;
		q[i]->crop.height = MXC_JPEG_DEFAULT_HEIGHT;
		mxc_jpeg_bytesperline(q[i], q[i]->fmt->precision);
		mxc_jpeg_sizeimage(q[i]);
	}
}

static int mxc_jpeg_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mxc_jpeg_ctx *ctx =
		container_of(ctrl->handler, struct mxc_jpeg_ctx, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		ctx->jpeg_quality = ctrl->val;
		break;
	default:
		dev_err(ctx->mxc_jpeg->dev, "Invalid control, id = %d, val = %d\n",
			ctrl->id, ctrl->val);
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops mxc_jpeg_ctrl_ops = {
	.s_ctrl = mxc_jpeg_s_ctrl,
};

static void mxc_jpeg_encode_ctrls(struct mxc_jpeg_ctx *ctx)
{
	v4l2_ctrl_new_std(&ctx->ctrl_handler, &mxc_jpeg_ctrl_ops,
			  V4L2_CID_JPEG_COMPRESSION_QUALITY, 1, 100, 1, 75);
}

static int mxc_jpeg_ctrls_setup(struct mxc_jpeg_ctx *ctx)
{
	int err;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, 2);

	if (ctx->mxc_jpeg->mode == MXC_JPEG_ENCODE)
		mxc_jpeg_encode_ctrls(ctx);

	if (ctx->ctrl_handler.error) {
		err = ctx->ctrl_handler.error;

		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		return err;
	}

	err = v4l2_ctrl_handler_setup(&ctx->ctrl_handler);
	if (err)
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	return err;
}

static int mxc_jpeg_open(struct file *file)
{
	struct mxc_jpeg_dev *mxc_jpeg = video_drvdata(file);
	struct video_device *mxc_vfd = video_devdata(file);
	struct device *dev = mxc_jpeg->dev;
	struct mxc_jpeg_ctx *ctx;
	int ret = 0;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (mutex_lock_interruptible(&mxc_jpeg->lock)) {
		ret = -ERESTARTSYS;
		goto free;
	}

	v4l2_fh_init(&ctx->fh, mxc_vfd);
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	ctx->mxc_jpeg = mxc_jpeg;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(mxc_jpeg->m2m_dev, ctx,
					    mxc_jpeg_queue_init);

	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto error;
	}

	ret = mxc_jpeg_ctrls_setup(ctx);
	if (ret) {
		dev_err(ctx->mxc_jpeg->dev, "failed to setup mxc jpeg controls\n");
		goto err_ctrls_setup;
	}
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	mxc_jpeg_set_default_params(ctx);
	ctx->slot = -1; /* slot not allocated yet */
	INIT_DELAYED_WORK(&ctx->task_timer, mxc_jpeg_device_run_timeout);

	if (mxc_jpeg->mode == MXC_JPEG_DECODE)
		dev_dbg(dev, "Opened JPEG decoder instance %p\n", ctx);
	else
		dev_dbg(dev, "Opened JPEG encoder instance %p\n", ctx);
	mutex_unlock(&mxc_jpeg->lock);

	return 0;

err_ctrls_setup:
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
error:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	mutex_unlock(&mxc_jpeg->lock);
free:
	kfree(ctx);
	return ret;
}

static int mxc_jpeg_querycap(struct file *file, void *priv,
			     struct v4l2_capability *cap)
{
	strscpy(cap->driver, MXC_JPEG_NAME " codec", sizeof(cap->driver));
	strscpy(cap->card, MXC_JPEG_NAME " codec", sizeof(cap->card));
	cap->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int mxc_jpeg_enum_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_fmtdesc *f)
{
	struct mxc_jpeg_ctx *ctx = mxc_jpeg_fh_to_ctx(priv);
	struct mxc_jpeg_q_data *q_data = mxc_jpeg_get_q_data(ctx, f->type);

	if (ctx->mxc_jpeg->mode == MXC_JPEG_ENCODE) {
		return enum_fmt(mxc_formats, MXC_JPEG_NUM_FORMATS, f,
			MXC_JPEG_FMT_TYPE_ENC);
	} else if (!ctx->header_parsed) {
		return enum_fmt(mxc_formats, MXC_JPEG_NUM_FORMATS, f,
			MXC_JPEG_FMT_TYPE_RAW);
	} else {
		/* For the decoder CAPTURE queue, only enumerate the raw formats
		 * supported for the format currently active on OUTPUT
		 * (more precisely what was propagated on capture queue
		 * after jpeg parse on the output buffer)
		 */
		int ret = -EINVAL;
		const struct mxc_jpeg_fmt *sibling;

		switch (f->index) {
		case 0:
			f->pixelformat = q_data->fmt->fourcc;
			ret = 0;
			break;
		case 1:
			sibling = mxc_jpeg_get_sibling_format(q_data->fmt);
			if (sibling) {
				f->pixelformat = sibling->fourcc;
				ret = 0;
			}
			break;
		default:
			break;
		}
		return ret;
	}
}

static int mxc_jpeg_enum_fmt_vid_out(struct file *file, void *priv,
				     struct v4l2_fmtdesc *f)
{
	struct mxc_jpeg_ctx *ctx = mxc_jpeg_fh_to_ctx(priv);
	u32 type = ctx->mxc_jpeg->mode == MXC_JPEG_DECODE ?  MXC_JPEG_FMT_TYPE_ENC :
							     MXC_JPEG_FMT_TYPE_RAW;
	int ret;

	ret = enum_fmt(mxc_formats, MXC_JPEG_NUM_FORMATS, f, type);
	if (ret)
		return ret;
	if (ctx->mxc_jpeg->mode == MXC_JPEG_DECODE)
		f->flags = V4L2_FMT_FLAG_DYN_RESOLUTION;
	return 0;
}

static u32 mxc_jpeg_get_fmt_type(struct mxc_jpeg_ctx *ctx, u32 type)
{
	if (ctx->mxc_jpeg->mode == MXC_JPEG_DECODE)
		return V4L2_TYPE_IS_OUTPUT(type) ? MXC_JPEG_FMT_TYPE_ENC : MXC_JPEG_FMT_TYPE_RAW;
	else
		return V4L2_TYPE_IS_CAPTURE(type) ? MXC_JPEG_FMT_TYPE_ENC : MXC_JPEG_FMT_TYPE_RAW;
}

static u32 mxc_jpeg_get_default_fourcc(struct mxc_jpeg_ctx *ctx, u32 type)
{
	if (ctx->mxc_jpeg->mode == MXC_JPEG_DECODE)
		return V4L2_TYPE_IS_OUTPUT(type) ? V4L2_PIX_FMT_JPEG : MXC_JPEG_DEFAULT_PFMT;
	else
		return V4L2_TYPE_IS_CAPTURE(type) ? V4L2_PIX_FMT_JPEG : MXC_JPEG_DEFAULT_PFMT;
}

static u32 mxc_jpeg_try_fourcc(struct mxc_jpeg_ctx *ctx, u32 fourcc)
{
	const struct mxc_jpeg_fmt *sibling;
	struct mxc_jpeg_q_data *q_data_cap;

	if (ctx->mxc_jpeg->mode != MXC_JPEG_DECODE)
		return fourcc;
	if (!ctx->header_parsed)
		return fourcc;

	q_data_cap = &ctx->cap_q;
	if (q_data_cap->fmt->fourcc == fourcc)
		return fourcc;

	sibling = mxc_jpeg_get_sibling_format(q_data_cap->fmt);
	if (sibling && sibling->fourcc == fourcc)
		return sibling->fourcc;

	return q_data_cap->fmt->fourcc;
}

static int mxc_jpeg_try_fmt(struct v4l2_format *f,
			    struct mxc_jpeg_ctx *ctx, struct mxc_jpeg_q_data *q_data)
{
	const struct mxc_jpeg_fmt *fmt;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct v4l2_plane_pix_format *pfmt;
	u32 fourcc = f->fmt.pix_mp.pixelformat;
	u32 w = (pix_mp->width < MXC_JPEG_MAX_WIDTH) ?
		 pix_mp->width : MXC_JPEG_MAX_WIDTH;
	u32 h = (pix_mp->height < MXC_JPEG_MAX_HEIGHT) ?
		 pix_mp->height : MXC_JPEG_MAX_HEIGHT;
	int i;

	fmt = mxc_jpeg_find_format(fourcc);
	if (!fmt || fmt->flags != mxc_jpeg_get_fmt_type(ctx, f->type)) {
		dev_warn(ctx->mxc_jpeg->dev, "Format not supported: %c%c%c%c, use the default.\n",
			 (fourcc & 0xff),
			 (fourcc >>  8) & 0xff,
			 (fourcc >> 16) & 0xff,
			 (fourcc >> 24) & 0xff);
		fourcc = mxc_jpeg_get_default_fourcc(ctx, f->type);
		fmt = mxc_jpeg_find_format(fourcc);
		if (!fmt)
			return -EINVAL;
		f->fmt.pix_mp.pixelformat = fourcc;
	}
	q_data->fmt = fmt;

	memset(pix_mp->reserved, 0, sizeof(pix_mp->reserved));
	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->num_planes = fmt->mem_planes;
	pix_mp->pixelformat = fmt->fourcc;

	q_data->w = w;
	q_data->h = h;
	q_data->w_adjusted = w;
	q_data->h_adjusted = h;
	v4l_bound_align_image(&q_data->w_adjusted,
			      w, /* adjust upwards*/
			      MXC_JPEG_MAX_WIDTH,
			      fmt->h_align,
			      &q_data->h_adjusted,
			      h, /* adjust upwards*/
			      MXC_JPEG_MAX_HEIGHT,
			      fmt->v_align,
			      0);
	for (i = 0; i < pix_mp->num_planes; i++) {
		pfmt = &pix_mp->plane_fmt[i];
		q_data->bytesperline[i] = pfmt->bytesperline;
		q_data->sizeimage[i] = pfmt->sizeimage;
	}

	/* calculate bytesperline & sizeimage */
	mxc_jpeg_bytesperline(q_data, fmt->precision);
	mxc_jpeg_sizeimage(q_data);

	/* adjust user format according to our calculations */
	for (i = 0; i < pix_mp->num_planes; i++) {
		pfmt = &pix_mp->plane_fmt[i];
		memset(pfmt->reserved, 0, sizeof(pfmt->reserved));
		pfmt->bytesperline = q_data->bytesperline[i];
		pfmt->sizeimage = mxc_jpeg_get_plane_size(q_data, i);
	}

	/* fix colorspace information to sRGB for both output & capture */
	pix_mp->colorspace = V4L2_COLORSPACE_SRGB;
	pix_mp->ycbcr_enc = V4L2_YCBCR_ENC_601;
	pix_mp->xfer_func = V4L2_XFER_FUNC_SRGB;
	/*
	 * this hardware does not change the range of the samples
	 * but since inside JPEG the YUV quantization is full-range,
	 * this driver will always use full-range for the raw frames, too
	 */
	pix_mp->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	if (fmt->flags == MXC_JPEG_FMT_TYPE_RAW) {
		q_data->crop.left = 0;
		q_data->crop.top = 0;
		q_data->crop.width = q_data->w;
		q_data->crop.height = q_data->h;
	}

	pix_mp->width = q_data->w_adjusted;
	pix_mp->height = q_data->h_adjusted;

	return 0;
}

static int mxc_jpeg_try_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct mxc_jpeg_ctx *ctx = mxc_jpeg_fh_to_ctx(priv);
	struct mxc_jpeg_dev *jpeg = ctx->mxc_jpeg;
	struct device *dev = jpeg->dev;
	struct mxc_jpeg_q_data tmp_q;

	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type)) {
		dev_err(dev, "TRY_FMT with Invalid type: %d\n", f->type);
		return -EINVAL;
	}

	if (ctx->mxc_jpeg->mode != MXC_JPEG_DECODE && V4L2_TYPE_IS_CAPTURE(f->type))
		f->fmt.pix_mp.pixelformat = mxc_jpeg_try_fourcc(ctx, f->fmt.pix_mp.pixelformat);

	return mxc_jpeg_try_fmt(f, ctx, &tmp_q);
}

static int mxc_jpeg_try_fmt_vid_out(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct mxc_jpeg_ctx *ctx = mxc_jpeg_fh_to_ctx(priv);
	struct mxc_jpeg_dev *jpeg = ctx->mxc_jpeg;
	struct device *dev = jpeg->dev;
	struct mxc_jpeg_q_data tmp_q;

	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type)) {
		dev_err(dev, "TRY_FMT with Invalid type: %d\n", f->type);
		return -EINVAL;
	}

	return mxc_jpeg_try_fmt(f, ctx, &tmp_q);
}

static void mxc_jpeg_s_parsed_fmt(struct mxc_jpeg_ctx *ctx, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct mxc_jpeg_q_data *q_data_cap;

	if (ctx->mxc_jpeg->mode != MXC_JPEG_DECODE || !V4L2_TYPE_IS_CAPTURE(f->type))
		return;
	if (!ctx->header_parsed)
		return;

	q_data_cap = mxc_jpeg_get_q_data(ctx, f->type);
	pix_mp->pixelformat = mxc_jpeg_try_fourcc(ctx, pix_mp->pixelformat);
	pix_mp->width = q_data_cap->w;
	pix_mp->height = q_data_cap->h;
}

static int mxc_jpeg_s_fmt(struct mxc_jpeg_ctx *ctx,
			  struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct mxc_jpeg_dev *jpeg = ctx->mxc_jpeg;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		v4l2_err(&jpeg->v4l2_dev, "queue busy\n");
		return -EBUSY;
	}

	mxc_jpeg_s_parsed_fmt(ctx, f);

	return mxc_jpeg_try_fmt(f, ctx, mxc_jpeg_get_q_data(ctx, f->type));
}

static int mxc_jpeg_s_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	return mxc_jpeg_s_fmt(mxc_jpeg_fh_to_ctx(priv), f);
}

static int mxc_jpeg_s_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	int ret;
	struct mxc_jpeg_ctx *ctx = mxc_jpeg_fh_to_ctx(priv);
	struct vb2_queue *dst_vq;
	struct mxc_jpeg_q_data *q_data_cap;
	enum v4l2_buf_type cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	struct v4l2_format fc;

	ret = mxc_jpeg_s_fmt(mxc_jpeg_fh_to_ctx(priv), f);
	if (ret)
		return ret;

	if (ctx->mxc_jpeg->mode != MXC_JPEG_DECODE)
		return 0;

	dst_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, cap_type);
	if (!dst_vq)
		return -EINVAL;

	if (vb2_is_busy(dst_vq))
		return 0;

	q_data_cap = mxc_jpeg_get_q_data(ctx, cap_type);
	if (q_data_cap->w == f->fmt.pix_mp.width && q_data_cap->h == f->fmt.pix_mp.height)
		return 0;
	memset(&fc, 0, sizeof(fc));
	fc.type = cap_type;
	fc.fmt.pix_mp.pixelformat = q_data_cap->fmt->fourcc;
	fc.fmt.pix_mp.width = f->fmt.pix_mp.width;
	fc.fmt.pix_mp.height = f->fmt.pix_mp.height;

	return mxc_jpeg_s_fmt_vid_cap(file, priv, &fc);
}

static int mxc_jpeg_g_fmt_vid(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct mxc_jpeg_ctx *ctx = mxc_jpeg_fh_to_ctx(priv);
	struct mxc_jpeg_dev *jpeg = ctx->mxc_jpeg;
	struct device *dev = jpeg->dev;
	struct v4l2_pix_format_mplane   *pix_mp = &f->fmt.pix_mp;
	struct mxc_jpeg_q_data *q_data = mxc_jpeg_get_q_data(ctx, f->type);
	int i;

	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type)) {
		dev_err(dev, "G_FMT with Invalid type: %d\n", f->type);
		return -EINVAL;
	}

	pix_mp->pixelformat = q_data->fmt->fourcc;
	pix_mp->width = q_data->w;
	pix_mp->height = q_data->h;
	pix_mp->field = V4L2_FIELD_NONE;
	if (q_data->fmt->flags == MXC_JPEG_FMT_TYPE_RAW) {
		pix_mp->width = q_data->w_adjusted;
		pix_mp->height = q_data->h_adjusted;
	}

	/* fix colorspace information to sRGB for both output & capture */
	pix_mp->colorspace = V4L2_COLORSPACE_SRGB;
	pix_mp->ycbcr_enc = V4L2_YCBCR_ENC_601;
	pix_mp->xfer_func = V4L2_XFER_FUNC_SRGB;
	pix_mp->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	pix_mp->num_planes = q_data->fmt->mem_planes;
	for (i = 0; i < pix_mp->num_planes; i++) {
		pix_mp->plane_fmt[i].bytesperline = q_data->bytesperline[i];
		pix_mp->plane_fmt[i].sizeimage = mxc_jpeg_get_plane_size(q_data, i);
	}

	return 0;
}

static int mxc_jpeg_dec_g_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct mxc_jpeg_ctx *ctx = mxc_jpeg_fh_to_ctx(fh);
	struct mxc_jpeg_q_data *q_data_cap;

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE && s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	q_data_cap = mxc_jpeg_get_q_data(ctx, s->type);

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		s->r = q_data_cap->crop;
		break;
	case V4L2_SEL_TGT_COMPOSE_PADDED:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = q_data_cap->w_adjusted;
		s->r.height = q_data_cap->h_adjusted;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mxc_jpeg_enc_g_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct mxc_jpeg_ctx *ctx = mxc_jpeg_fh_to_ctx(fh);
	struct mxc_jpeg_q_data *q_data_out;

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT && s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	q_data_out = mxc_jpeg_get_q_data(ctx, s->type);

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = q_data_out->w;
		s->r.height = q_data_out->h;
		break;
	case V4L2_SEL_TGT_CROP:
		s->r = q_data_out->crop;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mxc_jpeg_g_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct mxc_jpeg_ctx *ctx = mxc_jpeg_fh_to_ctx(fh);

	if (ctx->mxc_jpeg->mode == MXC_JPEG_DECODE)
		return mxc_jpeg_dec_g_selection(file, fh, s);
	else
		return mxc_jpeg_enc_g_selection(file, fh, s);
}

static int mxc_jpeg_s_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct mxc_jpeg_ctx *ctx = mxc_jpeg_fh_to_ctx(fh);
	struct mxc_jpeg_q_data *q_data_out;

	if (ctx->mxc_jpeg->mode != MXC_JPEG_ENCODE)
		return -ENOTTY;

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT && s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;
	if (s->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	q_data_out = mxc_jpeg_get_q_data(ctx, s->type);
	if (s->r.left || s->r.top)
		return -EINVAL;
	if (s->r.width > q_data_out->w || s->r.height > q_data_out->h)
		return -EINVAL;

	q_data_out->crop.left = 0;
	q_data_out->crop.top = 0;
	q_data_out->crop.width = s->r.width;
	q_data_out->crop.height = s->r.height;

	return 0;
}

static int mxc_jpeg_subscribe_event(struct v4l2_fh *fh,
				    const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ioctl_ops mxc_jpeg_ioctl_ops = {
	.vidioc_querycap		= mxc_jpeg_querycap,
	.vidioc_enum_fmt_vid_cap	= mxc_jpeg_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out	= mxc_jpeg_enum_fmt_vid_out,

	.vidioc_try_fmt_vid_cap_mplane	= mxc_jpeg_try_fmt_vid_cap,
	.vidioc_try_fmt_vid_out_mplane	= mxc_jpeg_try_fmt_vid_out,

	.vidioc_s_fmt_vid_cap_mplane	= mxc_jpeg_s_fmt_vid_cap,
	.vidioc_s_fmt_vid_out_mplane	= mxc_jpeg_s_fmt_vid_out,

	.vidioc_g_fmt_vid_cap_mplane	= mxc_jpeg_g_fmt_vid,
	.vidioc_g_fmt_vid_out_mplane	= mxc_jpeg_g_fmt_vid,

	.vidioc_g_selection		= mxc_jpeg_g_selection,
	.vidioc_s_selection		= mxc_jpeg_s_selection,

	.vidioc_subscribe_event		= mxc_jpeg_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,

	.vidioc_try_decoder_cmd		= v4l2_m2m_ioctl_try_decoder_cmd,
	.vidioc_decoder_cmd		= mxc_jpeg_decoder_cmd,
	.vidioc_try_encoder_cmd		= v4l2_m2m_ioctl_try_encoder_cmd,
	.vidioc_encoder_cmd		= mxc_jpeg_encoder_cmd,

	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,

	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,
	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,
};

static int mxc_jpeg_release(struct file *file)
{
	struct mxc_jpeg_dev *mxc_jpeg = video_drvdata(file);
	struct mxc_jpeg_ctx *ctx = mxc_jpeg_fh_to_ctx(file->private_data);
	struct device *dev = mxc_jpeg->dev;

	mutex_lock(&mxc_jpeg->lock);
	if (mxc_jpeg->mode == MXC_JPEG_DECODE)
		dev_dbg(dev, "Release JPEG decoder instance on slot %d.",
			ctx->slot);
	else
		dev_dbg(dev, "Release JPEG encoder instance on slot %d.",
			ctx->slot);
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	mutex_unlock(&mxc_jpeg->lock);

	return 0;
}

static const struct v4l2_file_operations mxc_jpeg_fops = {
	.owner		= THIS_MODULE,
	.open		= mxc_jpeg_open,
	.release	= mxc_jpeg_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static const struct v4l2_m2m_ops mxc_jpeg_m2m_ops = {
	.job_ready      = mxc_jpeg_job_ready,
	.device_run	= mxc_jpeg_device_run,
};

static void mxc_jpeg_detach_pm_domains(struct mxc_jpeg_dev *jpeg)
{
	int i;

	for (i = 0; i < jpeg->num_domains; i++) {
		if (jpeg->pd_link[i] && !IS_ERR(jpeg->pd_link[i]))
			device_link_del(jpeg->pd_link[i]);
		if (jpeg->pd_dev[i] && !IS_ERR(jpeg->pd_dev[i]))
			dev_pm_domain_detach(jpeg->pd_dev[i], true);
		jpeg->pd_dev[i] = NULL;
		jpeg->pd_link[i] = NULL;
	}
}

static int mxc_jpeg_attach_pm_domains(struct mxc_jpeg_dev *jpeg)
{
	struct device *dev = jpeg->dev;
	struct device_node *np = jpeg->pdev->dev.of_node;
	int i;
	int ret;

	jpeg->num_domains = of_count_phandle_with_args(np, "power-domains",
						       "#power-domain-cells");
	if (jpeg->num_domains < 0) {
		dev_err(dev, "No power domains defined for jpeg node\n");
		return jpeg->num_domains;
	}
	if (jpeg->num_domains == 1) {
		/* genpd_dev_pm_attach() attach automatically if power domains count is 1 */
		jpeg->num_domains = 0;
		return 0;
	}

	jpeg->pd_dev = devm_kmalloc_array(dev, jpeg->num_domains,
					  sizeof(*jpeg->pd_dev), GFP_KERNEL);
	if (!jpeg->pd_dev)
		return -ENOMEM;

	jpeg->pd_link = devm_kmalloc_array(dev, jpeg->num_domains,
					   sizeof(*jpeg->pd_link), GFP_KERNEL);
	if (!jpeg->pd_link)
		return -ENOMEM;

	for (i = 0; i < jpeg->num_domains; i++) {
		jpeg->pd_dev[i] = dev_pm_domain_attach_by_id(dev, i);
		if (IS_ERR(jpeg->pd_dev[i])) {
			ret = PTR_ERR(jpeg->pd_dev[i]);
			goto fail;
		}

		jpeg->pd_link[i] = device_link_add(dev, jpeg->pd_dev[i],
						   DL_FLAG_STATELESS |
						   DL_FLAG_PM_RUNTIME);
		if (!jpeg->pd_link[i]) {
			ret = -EINVAL;
			goto fail;
		}
	}

	return 0;
fail:
	mxc_jpeg_detach_pm_domains(jpeg);
	return ret;
}

static int mxc_jpeg_probe(struct platform_device *pdev)
{
	struct mxc_jpeg_dev *jpeg;
	struct device *dev = &pdev->dev;
	int dec_irq;
	int ret;
	int mode;
	const struct of_device_id *of_id;

	of_id = of_match_node(mxc_jpeg_match, dev->of_node);
	if (!of_id)
		return -ENODEV;
	mode = *(const int *)of_id->data;

	jpeg = devm_kzalloc(dev, sizeof(struct mxc_jpeg_dev), GFP_KERNEL);
	if (!jpeg)
		return -ENOMEM;

	mutex_init(&jpeg->lock);
	spin_lock_init(&jpeg->hw_lock);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "No suitable DMA available.\n");
		goto err_irq;
	}

	jpeg->base_reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(jpeg->base_reg))
		return PTR_ERR(jpeg->base_reg);

	ret = of_property_read_u32_index(pdev->dev.of_node, "slot", 0, &jpeg->slot_data.slot);
	if (ret)
		jpeg->slot_data.slot = 0;
	dev_info(&pdev->dev, "choose slot %d\n", jpeg->slot_data.slot);
	dec_irq = platform_get_irq(pdev, 0);
	if (dec_irq < 0) {
		ret = dec_irq;
		goto err_irq;
	}
	ret = devm_request_irq(&pdev->dev, dec_irq, mxc_jpeg_dec_irq,
			       0, pdev->name, jpeg);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq %d (%d)\n",
			dec_irq, ret);
		goto err_irq;
	}

	jpeg->pdev = pdev;
	jpeg->dev = dev;
	jpeg->mode = mode;

	/* Get clocks */
	ret = devm_clk_bulk_get_all(&pdev->dev, &jpeg->clks);
	if (ret < 0) {
		dev_err(dev, "failed to get clock\n");
		goto err_clk;
	}
	jpeg->num_clks = ret;

	ret = mxc_jpeg_attach_pm_domains(jpeg);
	if (ret < 0) {
		dev_err(dev, "failed to attach power domains %d\n", ret);
		goto err_clk;
	}

	/* v4l2 */
	ret = v4l2_device_register(dev, &jpeg->v4l2_dev);
	if (ret) {
		dev_err(dev, "failed to register v4l2 device\n");
		goto err_register;
	}
	jpeg->m2m_dev = v4l2_m2m_init(&mxc_jpeg_m2m_ops);
	if (IS_ERR(jpeg->m2m_dev)) {
		dev_err(dev, "failed to register v4l2 device\n");
		ret = PTR_ERR(jpeg->m2m_dev);
		goto err_m2m;
	}

	jpeg->dec_vdev = video_device_alloc();
	if (!jpeg->dec_vdev) {
		dev_err(dev, "failed to register v4l2 device\n");
		ret = -ENOMEM;
		goto err_vdev_alloc;
	}
	if (mode == MXC_JPEG_ENCODE)
		snprintf(jpeg->dec_vdev->name,
			 sizeof(jpeg->dec_vdev->name),
			 "%s-enc", MXC_JPEG_NAME);
	else
		snprintf(jpeg->dec_vdev->name,
			 sizeof(jpeg->dec_vdev->name),
			 "%s-dec", MXC_JPEG_NAME);

	jpeg->dec_vdev->fops = &mxc_jpeg_fops;
	jpeg->dec_vdev->ioctl_ops = &mxc_jpeg_ioctl_ops;
	jpeg->dec_vdev->minor = -1;
	jpeg->dec_vdev->release = video_device_release;
	jpeg->dec_vdev->lock = &jpeg->lock; /* lock for ioctl serialization */
	jpeg->dec_vdev->v4l2_dev = &jpeg->v4l2_dev;
	jpeg->dec_vdev->vfl_dir = VFL_DIR_M2M;
	jpeg->dec_vdev->device_caps = V4L2_CAP_STREAMING |
					V4L2_CAP_VIDEO_M2M_MPLANE;
	if (mode == MXC_JPEG_ENCODE) {
		v4l2_disable_ioctl(jpeg->dec_vdev, VIDIOC_DECODER_CMD);
		v4l2_disable_ioctl(jpeg->dec_vdev, VIDIOC_TRY_DECODER_CMD);
	} else {
		v4l2_disable_ioctl(jpeg->dec_vdev, VIDIOC_ENCODER_CMD);
		v4l2_disable_ioctl(jpeg->dec_vdev, VIDIOC_TRY_ENCODER_CMD);
	}
	ret = video_register_device(jpeg->dec_vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(dev, "failed to register video device\n");
		goto err_vdev_register;
	}
	video_set_drvdata(jpeg->dec_vdev, jpeg);
	if (mode == MXC_JPEG_ENCODE)
		v4l2_info(&jpeg->v4l2_dev,
			  "encoder device registered as /dev/video%d (%d,%d)\n",
			  jpeg->dec_vdev->num, VIDEO_MAJOR,
			  jpeg->dec_vdev->minor);
	else
		v4l2_info(&jpeg->v4l2_dev,
			  "decoder device registered as /dev/video%d (%d,%d)\n",
			  jpeg->dec_vdev->num, VIDEO_MAJOR,
			  jpeg->dec_vdev->minor);

	platform_set_drvdata(pdev, jpeg);
	pm_runtime_enable(dev);

	return 0;

err_vdev_register:
	video_device_release(jpeg->dec_vdev);

err_vdev_alloc:
	v4l2_m2m_release(jpeg->m2m_dev);

err_m2m:
	v4l2_device_unregister(&jpeg->v4l2_dev);

err_register:
	mxc_jpeg_detach_pm_domains(jpeg);

err_irq:
err_clk:
	return ret;
}

#ifdef CONFIG_PM
static int mxc_jpeg_runtime_resume(struct device *dev)
{
	struct mxc_jpeg_dev *jpeg = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(jpeg->num_clks, jpeg->clks);
	if (ret < 0) {
		dev_err(dev, "failed to enable clock\n");
		return ret;
	}

	return 0;
}

static int mxc_jpeg_runtime_suspend(struct device *dev)
{
	struct mxc_jpeg_dev *jpeg = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(jpeg->num_clks, jpeg->clks);

	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int mxc_jpeg_suspend(struct device *dev)
{
	struct mxc_jpeg_dev *jpeg = dev_get_drvdata(dev);

	v4l2_m2m_suspend(jpeg->m2m_dev);
	return pm_runtime_force_suspend(dev);
}

static int mxc_jpeg_resume(struct device *dev)
{
	struct mxc_jpeg_dev *jpeg = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		return ret;

	v4l2_m2m_resume(jpeg->m2m_dev);
	return ret;
}
#endif

static const struct dev_pm_ops	mxc_jpeg_pm_ops = {
	SET_RUNTIME_PM_OPS(mxc_jpeg_runtime_suspend,
			   mxc_jpeg_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(mxc_jpeg_suspend, mxc_jpeg_resume)
};

static void mxc_jpeg_remove(struct platform_device *pdev)
{
	struct mxc_jpeg_dev *jpeg = platform_get_drvdata(pdev);

	mxc_jpeg_free_slot_data(jpeg);

	pm_runtime_disable(&pdev->dev);
	video_unregister_device(jpeg->dec_vdev);
	v4l2_m2m_release(jpeg->m2m_dev);
	v4l2_device_unregister(&jpeg->v4l2_dev);
	mxc_jpeg_detach_pm_domains(jpeg);
}

MODULE_DEVICE_TABLE(of, mxc_jpeg_match);

static struct platform_driver mxc_jpeg_driver = {
	.probe = mxc_jpeg_probe,
	.remove_new = mxc_jpeg_remove,
	.driver = {
		.name = "mxc-jpeg",
		.of_match_table = mxc_jpeg_match,
		.pm = &mxc_jpeg_pm_ops,
	},
};
module_platform_driver(mxc_jpeg_driver);

MODULE_AUTHOR("Zhengyu Shen <zhengyu.shen_1@nxp.com>");
MODULE_AUTHOR("Mirela Rabulea <mirela.rabulea@nxp.com>");
MODULE_DESCRIPTION("V4L2 driver for i.MX8 QXP/QM JPEG encoder/decoder");
MODULE_LICENSE("GPL v2");
