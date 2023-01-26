// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/usb/uvc.h>
#include <linux/videodev2.h>

/* ------------------------------------------------------------------------
 * Video formats
 */

static struct uvc_format_desc uvc_fmts[] = {
	{
		.name		= "YUV 4:2:2 (YUYV)",
		.guid		= UVC_GUID_FORMAT_YUY2,
		.fcc		= V4L2_PIX_FMT_YUYV,
	},
	{
		.name		= "YUV 4:2:2 (YUYV)",
		.guid		= UVC_GUID_FORMAT_YUY2_ISIGHT,
		.fcc		= V4L2_PIX_FMT_YUYV,
	},
	{
		.name		= "YUV 4:2:0 (NV12)",
		.guid		= UVC_GUID_FORMAT_NV12,
		.fcc		= V4L2_PIX_FMT_NV12,
	},
	{
		.name		= "MJPEG",
		.guid		= UVC_GUID_FORMAT_MJPEG,
		.fcc		= V4L2_PIX_FMT_MJPEG,
	},
	{
		.name		= "YVU 4:2:0 (YV12)",
		.guid		= UVC_GUID_FORMAT_YV12,
		.fcc		= V4L2_PIX_FMT_YVU420,
	},
	{
		.name		= "YUV 4:2:0 (I420)",
		.guid		= UVC_GUID_FORMAT_I420,
		.fcc		= V4L2_PIX_FMT_YUV420,
	},
	{
		.name		= "YUV 4:2:0 (M420)",
		.guid		= UVC_GUID_FORMAT_M420,
		.fcc		= V4L2_PIX_FMT_M420,
	},
	{
		.name		= "YUV 4:2:2 (UYVY)",
		.guid		= UVC_GUID_FORMAT_UYVY,
		.fcc		= V4L2_PIX_FMT_UYVY,
	},
	{
		.name		= "Greyscale 8-bit (Y800)",
		.guid		= UVC_GUID_FORMAT_Y800,
		.fcc		= V4L2_PIX_FMT_GREY,
	},
	{
		.name		= "Greyscale 8-bit (Y8  )",
		.guid		= UVC_GUID_FORMAT_Y8,
		.fcc		= V4L2_PIX_FMT_GREY,
	},
	{
		.name		= "Greyscale 8-bit (D3DFMT_L8)",
		.guid		= UVC_GUID_FORMAT_D3DFMT_L8,
		.fcc		= V4L2_PIX_FMT_GREY,
	},
	{
		.name		= "IR 8-bit (L8_IR)",
		.guid		= UVC_GUID_FORMAT_KSMEDIA_L8_IR,
		.fcc		= V4L2_PIX_FMT_GREY,
	},
	{
		.name		= "Greyscale 10-bit (Y10 )",
		.guid		= UVC_GUID_FORMAT_Y10,
		.fcc		= V4L2_PIX_FMT_Y10,
	},
	{
		.name		= "Greyscale 12-bit (Y12 )",
		.guid		= UVC_GUID_FORMAT_Y12,
		.fcc		= V4L2_PIX_FMT_Y12,
	},
	{
		.name		= "Greyscale 16-bit (Y16 )",
		.guid		= UVC_GUID_FORMAT_Y16,
		.fcc		= V4L2_PIX_FMT_Y16,
	},
	{
		.name		= "BGGR Bayer (BY8 )",
		.guid		= UVC_GUID_FORMAT_BY8,
		.fcc		= V4L2_PIX_FMT_SBGGR8,
	},
	{
		.name		= "BGGR Bayer (BA81)",
		.guid		= UVC_GUID_FORMAT_BA81,
		.fcc		= V4L2_PIX_FMT_SBGGR8,
	},
	{
		.name		= "GBRG Bayer (GBRG)",
		.guid		= UVC_GUID_FORMAT_GBRG,
		.fcc		= V4L2_PIX_FMT_SGBRG8,
	},
	{
		.name		= "GRBG Bayer (GRBG)",
		.guid		= UVC_GUID_FORMAT_GRBG,
		.fcc		= V4L2_PIX_FMT_SGRBG8,
	},
	{
		.name		= "RGGB Bayer (RGGB)",
		.guid		= UVC_GUID_FORMAT_RGGB,
		.fcc		= V4L2_PIX_FMT_SRGGB8,
	},
	{
		.name		= "RGB565",
		.guid		= UVC_GUID_FORMAT_RGBP,
		.fcc		= V4L2_PIX_FMT_RGB565,
	},
	{
		.name		= "BGR 8:8:8 (BGR3)",
		.guid		= UVC_GUID_FORMAT_BGR3,
		.fcc		= V4L2_PIX_FMT_BGR24,
	},
	{
		.name		= "BGRA/X 8:8:8:8 (BGR4)",
		.guid		= UVC_GUID_FORMAT_BGR4,
		.fcc		= V4L2_PIX_FMT_XBGR32,
	},
	{
		.name		= "H.264",
		.guid		= UVC_GUID_FORMAT_H264,
		.fcc		= V4L2_PIX_FMT_H264,
	},
	{
		.name		= "H.265",
		.guid		= UVC_GUID_FORMAT_H265,
		.fcc		= V4L2_PIX_FMT_HEVC,
	},
	{
		.name		= "Greyscale 8 L/R (Y8I)",
		.guid		= UVC_GUID_FORMAT_Y8I,
		.fcc		= V4L2_PIX_FMT_Y8I,
	},
	{
		.name		= "Greyscale 12 L/R (Y12I)",
		.guid		= UVC_GUID_FORMAT_Y12I,
		.fcc		= V4L2_PIX_FMT_Y12I,
	},
	{
		.name		= "Depth data 16-bit (Z16)",
		.guid		= UVC_GUID_FORMAT_Z16,
		.fcc		= V4L2_PIX_FMT_Z16,
	},
	{
		.name		= "Bayer 10-bit (SRGGB10P)",
		.guid		= UVC_GUID_FORMAT_RW10,
		.fcc		= V4L2_PIX_FMT_SRGGB10P,
	},
	{
		.name		= "Bayer 16-bit (SBGGR16)",
		.guid		= UVC_GUID_FORMAT_BG16,
		.fcc		= V4L2_PIX_FMT_SBGGR16,
	},
	{
		.name		= "Bayer 16-bit (SGBRG16)",
		.guid		= UVC_GUID_FORMAT_GB16,
		.fcc		= V4L2_PIX_FMT_SGBRG16,
	},
	{
		.name		= "Bayer 16-bit (SRGGB16)",
		.guid		= UVC_GUID_FORMAT_RG16,
		.fcc		= V4L2_PIX_FMT_SRGGB16,
	},
	{
		.name		= "Bayer 16-bit (SGRBG16)",
		.guid		= UVC_GUID_FORMAT_GR16,
		.fcc		= V4L2_PIX_FMT_SGRBG16,
	},
	{
		.name		= "Depth data 16-bit (Z16)",
		.guid		= UVC_GUID_FORMAT_INVZ,
		.fcc		= V4L2_PIX_FMT_Z16,
	},
	{
		.name		= "Greyscale 10-bit (Y10 )",
		.guid		= UVC_GUID_FORMAT_INVI,
		.fcc		= V4L2_PIX_FMT_Y10,
	},
	{
		.name		= "IR:Depth 26-bit (INZI)",
		.guid		= UVC_GUID_FORMAT_INZI,
		.fcc		= V4L2_PIX_FMT_INZI,
	},
	{
		.name		= "4-bit Depth Confidence (Packed)",
		.guid		= UVC_GUID_FORMAT_CNF4,
		.fcc		= V4L2_PIX_FMT_CNF4,
	},
	{
		.name		= "HEVC",
		.guid		= UVC_GUID_FORMAT_HEVC,
		.fcc		= V4L2_PIX_FMT_HEVC,
	},
};

struct uvc_format_desc *uvc_format_by_guid(const u8 guid[16])
{
	unsigned int len = ARRAY_SIZE(uvc_fmts);
	unsigned int i;

	for (i = 0; i < len; ++i) {
		if (memcmp(guid, uvc_fmts[i].guid, 16) == 0)
			return &uvc_fmts[i];
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(uvc_format_by_guid);

MODULE_LICENSE("GPL");
