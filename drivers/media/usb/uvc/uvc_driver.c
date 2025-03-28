// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      uvc_driver.c  --  USB Video Class driver
 *
 *      Copyright (C) 2005-2010
 *          Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/quirks.h>
#include <linux/usb/uvc.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/unaligned.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

#include "uvcvideo.h"

#define DRIVER_AUTHOR		"Laurent Pinchart " \
				"<laurent.pinchart@ideasonboard.com>"
#define DRIVER_DESC		"USB Video Class driver"

unsigned int uvc_clock_param = CLOCK_MONOTONIC;
unsigned int uvc_hw_timestamps_param;
unsigned int uvc_no_drop_param = 1;
static unsigned int uvc_quirks_param = -1;
unsigned int uvc_dbg_param;
unsigned int uvc_timeout_param = UVC_CTRL_STREAMING_TIMEOUT;

static struct usb_driver uvc_driver;

/* ------------------------------------------------------------------------
 * Utility functions
 */

struct usb_host_endpoint *uvc_find_endpoint(struct usb_host_interface *alts,
		u8 epaddr)
{
	struct usb_host_endpoint *ep;
	unsigned int i;

	for (i = 0; i < alts->desc.bNumEndpoints; ++i) {
		ep = &alts->endpoint[i];
		if (ep->desc.bEndpointAddress == epaddr)
			return ep;
	}

	return NULL;
}

static enum v4l2_colorspace uvc_colorspace(const u8 primaries)
{
	static const enum v4l2_colorspace colorprimaries[] = {
		V4L2_COLORSPACE_SRGB,  /* Unspecified */
		V4L2_COLORSPACE_SRGB,
		V4L2_COLORSPACE_470_SYSTEM_M,
		V4L2_COLORSPACE_470_SYSTEM_BG,
		V4L2_COLORSPACE_SMPTE170M,
		V4L2_COLORSPACE_SMPTE240M,
	};

	if (primaries < ARRAY_SIZE(colorprimaries))
		return colorprimaries[primaries];

	return V4L2_COLORSPACE_SRGB;  /* Reserved */
}

static enum v4l2_xfer_func uvc_xfer_func(const u8 transfer_characteristics)
{
	/*
	 * V4L2 does not currently have definitions for all possible values of
	 * UVC transfer characteristics. If v4l2_xfer_func is extended with new
	 * values, the mapping below should be updated.
	 *
	 * Substitutions are taken from the mapping given for
	 * V4L2_XFER_FUNC_DEFAULT documented in videodev2.h.
	 */
	static const enum v4l2_xfer_func xfer_funcs[] = {
		V4L2_XFER_FUNC_DEFAULT,    /* Unspecified */
		V4L2_XFER_FUNC_709,
		V4L2_XFER_FUNC_709,        /* Substitution for BT.470-2 M */
		V4L2_XFER_FUNC_709,        /* Substitution for BT.470-2 B, G */
		V4L2_XFER_FUNC_709,        /* Substitution for SMPTE 170M */
		V4L2_XFER_FUNC_SMPTE240M,
		V4L2_XFER_FUNC_NONE,
		V4L2_XFER_FUNC_SRGB,
	};

	if (transfer_characteristics < ARRAY_SIZE(xfer_funcs))
		return xfer_funcs[transfer_characteristics];

	return V4L2_XFER_FUNC_DEFAULT;  /* Reserved */
}

static enum v4l2_ycbcr_encoding uvc_ycbcr_enc(const u8 matrix_coefficients)
{
	/*
	 * V4L2 does not currently have definitions for all possible values of
	 * UVC matrix coefficients. If v4l2_ycbcr_encoding is extended with new
	 * values, the mapping below should be updated.
	 *
	 * Substitutions are taken from the mapping given for
	 * V4L2_YCBCR_ENC_DEFAULT documented in videodev2.h.
	 *
	 * FCC is assumed to be close enough to 601.
	 */
	static const enum v4l2_ycbcr_encoding ycbcr_encs[] = {
		V4L2_YCBCR_ENC_DEFAULT,  /* Unspecified */
		V4L2_YCBCR_ENC_709,
		V4L2_YCBCR_ENC_601,      /* Substitution for FCC */
		V4L2_YCBCR_ENC_601,      /* Substitution for BT.470-2 B, G */
		V4L2_YCBCR_ENC_601,
		V4L2_YCBCR_ENC_SMPTE240M,
	};

	if (matrix_coefficients < ARRAY_SIZE(ycbcr_encs))
		return ycbcr_encs[matrix_coefficients];

	return V4L2_YCBCR_ENC_DEFAULT;  /* Reserved */
}

/* ------------------------------------------------------------------------
 * Terminal and unit management
 */

struct uvc_entity *uvc_entity_by_id(struct uvc_device *dev, int id)
{
	struct uvc_entity *entity;

	list_for_each_entry(entity, &dev->entities, list) {
		if (entity->id == id)
			return entity;
	}

	return NULL;
}

static struct uvc_entity *uvc_entity_by_reference(struct uvc_device *dev,
	int id, struct uvc_entity *entity)
{
	unsigned int i;

	if (entity == NULL)
		entity = list_entry(&dev->entities, struct uvc_entity, list);

	list_for_each_entry_continue(entity, &dev->entities, list) {
		for (i = 0; i < entity->bNrInPins; ++i)
			if (entity->baSourceID[i] == id)
				return entity;
	}

	return NULL;
}

static struct uvc_streaming *uvc_stream_by_id(struct uvc_device *dev, int id)
{
	struct uvc_streaming *stream;

	list_for_each_entry(stream, &dev->streams, list) {
		if (stream->header.bTerminalLink == id)
			return stream;
	}

	return NULL;
}

/* ------------------------------------------------------------------------
 * Streaming Object Management
 */

static void uvc_stream_delete(struct uvc_streaming *stream)
{
	if (stream->async_wq)
		destroy_workqueue(stream->async_wq);

	mutex_destroy(&stream->mutex);

	usb_put_intf(stream->intf);

	kfree(stream->formats);
	kfree(stream->header.bmaControls);
	kfree(stream);
}

static struct uvc_streaming *uvc_stream_new(struct uvc_device *dev,
					    struct usb_interface *intf)
{
	struct uvc_streaming *stream;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (stream == NULL)
		return NULL;

	mutex_init(&stream->mutex);

	stream->dev = dev;
	stream->intf = usb_get_intf(intf);
	stream->intfnum = intf->cur_altsetting->desc.bInterfaceNumber;

	/* Allocate a stream specific work queue for asynchronous tasks. */
	stream->async_wq = alloc_workqueue("uvcvideo", WQ_UNBOUND | WQ_HIGHPRI,
					   0);
	if (!stream->async_wq) {
		uvc_stream_delete(stream);
		return NULL;
	}

	return stream;
}

/* ------------------------------------------------------------------------
 * Descriptors parsing
 */

static int uvc_parse_frame(struct uvc_device *dev,
			   struct uvc_streaming *streaming,
			   struct uvc_format *format, struct uvc_frame *frame,
			   u32 **intervals, u8 ftype, int width_multiplier,
			   const unsigned char *buffer, int buflen)
{
	struct usb_host_interface *alts = streaming->intf->cur_altsetting;
	unsigned int maxIntervalIndex;
	unsigned int interval;
	unsigned int i, n;

	if (ftype != UVC_VS_FRAME_FRAME_BASED)
		n = buflen > 25 ? buffer[25] : 0;
	else
		n = buflen > 21 ? buffer[21] : 0;

	n = n ? n : 3;

	if (buflen < 26 + 4 * n) {
		uvc_dbg(dev, DESCR,
			"device %d videostreaming interface %d FRAME error\n",
			dev->udev->devnum, alts->desc.bInterfaceNumber);
		return -EINVAL;
	}

	frame->bFrameIndex = buffer[3];
	frame->bmCapabilities = buffer[4];
	frame->wWidth = get_unaligned_le16(&buffer[5]) * width_multiplier;
	frame->wHeight = get_unaligned_le16(&buffer[7]);
	frame->dwMinBitRate = get_unaligned_le32(&buffer[9]);
	frame->dwMaxBitRate = get_unaligned_le32(&buffer[13]);
	if (ftype != UVC_VS_FRAME_FRAME_BASED) {
		frame->dwMaxVideoFrameBufferSize =
			get_unaligned_le32(&buffer[17]);
		frame->dwDefaultFrameInterval =
			get_unaligned_le32(&buffer[21]);
		frame->bFrameIntervalType = buffer[25];
	} else {
		frame->dwMaxVideoFrameBufferSize = 0;
		frame->dwDefaultFrameInterval =
			get_unaligned_le32(&buffer[17]);
		frame->bFrameIntervalType = buffer[21];
	}

	/*
	 * Copy the frame intervals.
	 *
	 * Some bogus devices report dwMinFrameInterval equal to
	 * dwMaxFrameInterval and have dwFrameIntervalStep set to zero. Setting
	 * all null intervals to 1 fixes the problem and some other divisions
	 * by zero that could happen.
	 */
	frame->dwFrameInterval = *intervals;

	for (i = 0; i < n; ++i) {
		interval = get_unaligned_le32(&buffer[26 + 4 * i]);
		(*intervals)[i] = interval ? interval : 1;
	}

	/*
	 * Apply more fixes, quirks and workarounds to handle incorrect or
	 * broken descriptors.
	 */

	/*
	 * Several UVC chipsets screw up dwMaxVideoFrameBufferSize completely.
	 * Observed behaviours range from setting the value to 1.1x the actual
	 * frame size to hardwiring the 16 low bits to 0. This results in a
	 * higher than necessary memory usage as well as a wrong image size
	 * information. For uncompressed formats this can be fixed by computing
	 * the value from the frame size.
	 */
	if (!(format->flags & UVC_FMT_FLAG_COMPRESSED))
		frame->dwMaxVideoFrameBufferSize = format->bpp * frame->wWidth
						 * frame->wHeight / 8;

	/*
	 * Clamp the default frame interval to the boundaries. A zero
	 * bFrameIntervalType value indicates a continuous frame interval
	 * range, with dwFrameInterval[0] storing the minimum value and
	 * dwFrameInterval[1] storing the maximum value.
	 */
	maxIntervalIndex = frame->bFrameIntervalType ? n - 1 : 1;
	frame->dwDefaultFrameInterval =
		clamp(frame->dwDefaultFrameInterval,
		      frame->dwFrameInterval[0],
		      frame->dwFrameInterval[maxIntervalIndex]);

	/*
	 * Some devices report frame intervals that are not functional. If the
	 * corresponding quirk is set, restrict operation to the first interval
	 * only.
	 */
	if (dev->quirks & UVC_QUIRK_RESTRICT_FRAME_RATE) {
		frame->bFrameIntervalType = 1;
		(*intervals)[0] = frame->dwDefaultFrameInterval;
	}

	uvc_dbg(dev, DESCR, "- %ux%u (%u.%u fps)\n",
		frame->wWidth, frame->wHeight,
		10000000 / frame->dwDefaultFrameInterval,
		(100000000 / frame->dwDefaultFrameInterval) % 10);

	*intervals += n;

	return buffer[0];
}

static int uvc_parse_format(struct uvc_device *dev,
	struct uvc_streaming *streaming, struct uvc_format *format,
	struct uvc_frame *frames, u32 **intervals, const unsigned char *buffer,
	int buflen)
{
	struct usb_host_interface *alts = streaming->intf->cur_altsetting;
	const struct uvc_format_desc *fmtdesc;
	struct uvc_frame *frame;
	const unsigned char *start = buffer;
	unsigned int width_multiplier = 1;
	unsigned int i, n;
	u8 ftype;
	int ret;

	format->type = buffer[2];
	format->index = buffer[3];
	format->frames = frames;

	switch (buffer[2]) {
	case UVC_VS_FORMAT_UNCOMPRESSED:
	case UVC_VS_FORMAT_FRAME_BASED:
		n = buffer[2] == UVC_VS_FORMAT_UNCOMPRESSED ? 27 : 28;
		if (buflen < n) {
			uvc_dbg(dev, DESCR,
				"device %d videostreaming interface %d FORMAT error\n",
				dev->udev->devnum,
				alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		/* Find the format descriptor from its GUID. */
		fmtdesc = uvc_format_by_guid(&buffer[5]);

		if (!fmtdesc) {
			/*
			 * Unknown video formats are not fatal errors, the
			 * caller will skip this descriptor.
			 */
			dev_info(&streaming->intf->dev,
				 "Unknown video format %pUl\n", &buffer[5]);
			return 0;
		}

		format->fcc = fmtdesc->fcc;
		format->bpp = buffer[21];

		/*
		 * Some devices report a format that doesn't match what they
		 * really send.
		 */
		if (dev->quirks & UVC_QUIRK_FORCE_Y8) {
			if (format->fcc == V4L2_PIX_FMT_YUYV) {
				format->fcc = V4L2_PIX_FMT_GREY;
				format->bpp = 8;
				width_multiplier = 2;
			}
		}

		/* Some devices report bpp that doesn't match the format. */
		if (dev->quirks & UVC_QUIRK_FORCE_BPP) {
			const struct v4l2_format_info *info =
				v4l2_format_info(format->fcc);

			if (info) {
				unsigned int div = info->hdiv * info->vdiv;

				n = info->bpp[0] * div;
				for (i = 1; i < info->comp_planes; i++)
					n += info->bpp[i];

				format->bpp = DIV_ROUND_UP(8 * n, div);
			}
		}

		if (buffer[2] == UVC_VS_FORMAT_UNCOMPRESSED) {
			ftype = UVC_VS_FRAME_UNCOMPRESSED;
		} else {
			ftype = UVC_VS_FRAME_FRAME_BASED;
			if (buffer[27])
				format->flags = UVC_FMT_FLAG_COMPRESSED;
		}
		break;

	case UVC_VS_FORMAT_MJPEG:
		if (buflen < 11) {
			uvc_dbg(dev, DESCR,
				"device %d videostreaming interface %d FORMAT error\n",
				dev->udev->devnum,
				alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		format->fcc = V4L2_PIX_FMT_MJPEG;
		format->flags = UVC_FMT_FLAG_COMPRESSED;
		format->bpp = 0;
		ftype = UVC_VS_FRAME_MJPEG;
		break;

	case UVC_VS_FORMAT_DV:
		if (buflen < 9) {
			uvc_dbg(dev, DESCR,
				"device %d videostreaming interface %d FORMAT error\n",
				dev->udev->devnum,
				alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		if ((buffer[8] & 0x7f) > 2) {
			uvc_dbg(dev, DESCR,
				"device %d videostreaming interface %d: unknown DV format %u\n",
				dev->udev->devnum,
				alts->desc.bInterfaceNumber, buffer[8]);
			return -EINVAL;
		}

		format->fcc = V4L2_PIX_FMT_DV;
		format->flags = UVC_FMT_FLAG_COMPRESSED | UVC_FMT_FLAG_STREAM;
		format->bpp = 0;
		ftype = 0;

		/* Create a dummy frame descriptor. */
		frame = &frames[0];
		memset(frame, 0, sizeof(*frame));
		frame->bFrameIntervalType = 1;
		frame->dwDefaultFrameInterval = 1;
		frame->dwFrameInterval = *intervals;
		*(*intervals)++ = 1;
		format->nframes = 1;
		break;

	case UVC_VS_FORMAT_MPEG2TS:
	case UVC_VS_FORMAT_STREAM_BASED:
		/* Not supported yet. */
	default:
		uvc_dbg(dev, DESCR,
			"device %d videostreaming interface %d unsupported format %u\n",
			dev->udev->devnum, alts->desc.bInterfaceNumber,
			buffer[2]);
		return -EINVAL;
	}

	uvc_dbg(dev, DESCR, "Found format %p4cc", &format->fcc);

	buflen -= buffer[0];
	buffer += buffer[0];

	/*
	 * Parse the frame descriptors. Only uncompressed, MJPEG and frame
	 * based formats have frame descriptors.
	 */
	if (ftype) {
		while (buflen > 2 && buffer[1] == USB_DT_CS_INTERFACE &&
		       buffer[2] == ftype) {
			frame = &frames[format->nframes];
			ret = uvc_parse_frame(dev, streaming, format, frame,
					      intervals, ftype, width_multiplier,
					      buffer, buflen);
			if (ret < 0)
				return ret;
			format->nframes++;
			buflen -= ret;
			buffer += ret;
		}
	}

	if (buflen > 2 && buffer[1] == USB_DT_CS_INTERFACE &&
	    buffer[2] == UVC_VS_STILL_IMAGE_FRAME) {
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (buflen > 2 && buffer[1] == USB_DT_CS_INTERFACE &&
	    buffer[2] == UVC_VS_COLORFORMAT) {
		if (buflen < 6) {
			uvc_dbg(dev, DESCR,
				"device %d videostreaming interface %d COLORFORMAT error\n",
				dev->udev->devnum,
				alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		format->colorspace = uvc_colorspace(buffer[3]);
		format->xfer_func = uvc_xfer_func(buffer[4]);
		format->ycbcr_enc = uvc_ycbcr_enc(buffer[5]);

		buflen -= buffer[0];
		buffer += buffer[0];
	} else {
		format->colorspace = V4L2_COLORSPACE_SRGB;
	}

	return buffer - start;
}

static int uvc_parse_streaming(struct uvc_device *dev,
	struct usb_interface *intf)
{
	struct uvc_streaming *streaming = NULL;
	struct uvc_format *format;
	struct uvc_frame *frame;
	struct usb_host_interface *alts = &intf->altsetting[0];
	const unsigned char *_buffer, *buffer = alts->extra;
	int _buflen, buflen = alts->extralen;
	unsigned int nformats = 0, nframes = 0, nintervals = 0;
	unsigned int size, i, n, p;
	u32 *interval;
	u16 psize;
	int ret = -EINVAL;

	if (intf->cur_altsetting->desc.bInterfaceSubClass
		!= UVC_SC_VIDEOSTREAMING) {
		uvc_dbg(dev, DESCR,
			"device %d interface %d isn't a video streaming interface\n",
			dev->udev->devnum,
			intf->altsetting[0].desc.bInterfaceNumber);
		return -EINVAL;
	}

	if (usb_driver_claim_interface(&uvc_driver, intf, dev)) {
		uvc_dbg(dev, DESCR,
			"device %d interface %d is already claimed\n",
			dev->udev->devnum,
			intf->altsetting[0].desc.bInterfaceNumber);
		return -EINVAL;
	}

	streaming = uvc_stream_new(dev, intf);
	if (streaming == NULL) {
		usb_driver_release_interface(&uvc_driver, intf);
		return -ENOMEM;
	}

	/*
	 * The Pico iMage webcam has its class-specific interface descriptors
	 * after the endpoint descriptors.
	 */
	if (buflen == 0) {
		for (i = 0; i < alts->desc.bNumEndpoints; ++i) {
			struct usb_host_endpoint *ep = &alts->endpoint[i];

			if (ep->extralen == 0)
				continue;

			if (ep->extralen > 2 &&
			    ep->extra[1] == USB_DT_CS_INTERFACE) {
				uvc_dbg(dev, DESCR,
					"trying extra data from endpoint %u\n",
					i);
				buffer = alts->endpoint[i].extra;
				buflen = alts->endpoint[i].extralen;
				break;
			}
		}
	}

	/* Skip the standard interface descriptors. */
	while (buflen > 2 && buffer[1] != USB_DT_CS_INTERFACE) {
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (buflen <= 2) {
		uvc_dbg(dev, DESCR,
			"no class-specific streaming interface descriptors found\n");
		goto error;
	}

	/* Parse the header descriptor. */
	switch (buffer[2]) {
	case UVC_VS_OUTPUT_HEADER:
		streaming->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		size = 9;
		break;

	case UVC_VS_INPUT_HEADER:
		streaming->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		size = 13;
		break;

	default:
		uvc_dbg(dev, DESCR,
			"device %d videostreaming interface %d HEADER descriptor not found\n",
			dev->udev->devnum, alts->desc.bInterfaceNumber);
		goto error;
	}

	p = buflen >= 4 ? buffer[3] : 0;
	n = buflen >= size ? buffer[size-1] : 0;

	if (buflen < size + p*n) {
		uvc_dbg(dev, DESCR,
			"device %d videostreaming interface %d HEADER descriptor is invalid\n",
			dev->udev->devnum, alts->desc.bInterfaceNumber);
		goto error;
	}

	streaming->header.bNumFormats = p;
	streaming->header.bEndpointAddress = buffer[6];
	if (buffer[2] == UVC_VS_INPUT_HEADER) {
		streaming->header.bmInfo = buffer[7];
		streaming->header.bTerminalLink = buffer[8];
		streaming->header.bStillCaptureMethod = buffer[9];
		streaming->header.bTriggerSupport = buffer[10];
		streaming->header.bTriggerUsage = buffer[11];
	} else {
		streaming->header.bTerminalLink = buffer[7];
	}
	streaming->header.bControlSize = n;

	streaming->header.bmaControls = kmemdup(&buffer[size], p * n,
						GFP_KERNEL);
	if (streaming->header.bmaControls == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	buflen -= buffer[0];
	buffer += buffer[0];

	_buffer = buffer;
	_buflen = buflen;

	/* Count the format and frame descriptors. */
	while (_buflen > 2 && _buffer[1] == USB_DT_CS_INTERFACE) {
		switch (_buffer[2]) {
		case UVC_VS_FORMAT_UNCOMPRESSED:
		case UVC_VS_FORMAT_MJPEG:
		case UVC_VS_FORMAT_FRAME_BASED:
			nformats++;
			break;

		case UVC_VS_FORMAT_DV:
			/*
			 * DV format has no frame descriptor. We will create a
			 * dummy frame descriptor with a dummy frame interval.
			 */
			nformats++;
			nframes++;
			nintervals++;
			break;

		case UVC_VS_FORMAT_MPEG2TS:
		case UVC_VS_FORMAT_STREAM_BASED:
			uvc_dbg(dev, DESCR,
				"device %d videostreaming interface %d FORMAT %u is not supported\n",
				dev->udev->devnum,
				alts->desc.bInterfaceNumber, _buffer[2]);
			break;

		case UVC_VS_FRAME_UNCOMPRESSED:
		case UVC_VS_FRAME_MJPEG:
			nframes++;
			if (_buflen > 25)
				nintervals += _buffer[25] ? _buffer[25] : 3;
			break;

		case UVC_VS_FRAME_FRAME_BASED:
			nframes++;
			if (_buflen > 21)
				nintervals += _buffer[21] ? _buffer[21] : 3;
			break;
		}

		_buflen -= _buffer[0];
		_buffer += _buffer[0];
	}

	if (nformats == 0) {
		uvc_dbg(dev, DESCR,
			"device %d videostreaming interface %d has no supported formats defined\n",
			dev->udev->devnum, alts->desc.bInterfaceNumber);
		goto error;
	}

	/*
	 * Allocate memory for the formats, the frames and the intervals,
	 * plus any required padding to guarantee that everything has the
	 * correct alignment.
	 */
	size = nformats * sizeof(*format);
	size = ALIGN(size, __alignof__(*frame)) + nframes * sizeof(*frame);
	size = ALIGN(size, __alignof__(*interval))
	     + nintervals * sizeof(*interval);

	format = kzalloc(size, GFP_KERNEL);
	if (!format) {
		ret = -ENOMEM;
		goto error;
	}

	frame = (void *)format + nformats * sizeof(*format);
	frame = PTR_ALIGN(frame, __alignof__(*frame));
	interval = (void *)frame + nframes * sizeof(*frame);
	interval = PTR_ALIGN(interval, __alignof__(*interval));

	streaming->formats = format;
	streaming->nformats = 0;

	/* Parse the format descriptors. */
	while (buflen > 2 && buffer[1] == USB_DT_CS_INTERFACE) {
		switch (buffer[2]) {
		case UVC_VS_FORMAT_UNCOMPRESSED:
		case UVC_VS_FORMAT_MJPEG:
		case UVC_VS_FORMAT_DV:
		case UVC_VS_FORMAT_FRAME_BASED:
			ret = uvc_parse_format(dev, streaming, format, frame,
				&interval, buffer, buflen);
			if (ret < 0)
				goto error;
			if (!ret)
				break;

			streaming->nformats++;
			frame += format->nframes;
			format++;

			buflen -= ret;
			buffer += ret;
			continue;

		default:
			break;
		}

		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (buflen)
		uvc_dbg(dev, DESCR,
			"device %d videostreaming interface %d has %u bytes of trailing descriptor garbage\n",
			dev->udev->devnum, alts->desc.bInterfaceNumber, buflen);

	/* Parse the alternate settings to find the maximum bandwidth. */
	for (i = 0; i < intf->num_altsetting; ++i) {
		struct usb_host_endpoint *ep;

		alts = &intf->altsetting[i];
		ep = uvc_find_endpoint(alts,
				streaming->header.bEndpointAddress);
		if (ep == NULL)
			continue;
		psize = uvc_endpoint_max_bpi(dev->udev, ep);
		if (psize > streaming->maxpsize)
			streaming->maxpsize = psize;
	}

	list_add_tail(&streaming->list, &dev->streams);
	return 0;

error:
	usb_driver_release_interface(&uvc_driver, intf);
	uvc_stream_delete(streaming);
	return ret;
}

static const u8 uvc_camera_guid[16] = UVC_GUID_UVC_CAMERA;
static const u8 uvc_gpio_guid[16] = UVC_GUID_EXT_GPIO_CONTROLLER;
static const u8 uvc_media_transport_input_guid[16] =
	UVC_GUID_UVC_MEDIA_TRANSPORT_INPUT;
static const u8 uvc_processing_guid[16] = UVC_GUID_UVC_PROCESSING;

static struct uvc_entity *uvc_alloc_entity(u16 type, u16 id,
		unsigned int num_pads, unsigned int extra_size)
{
	struct uvc_entity *entity;
	unsigned int num_inputs;
	unsigned int size;
	unsigned int i;

	extra_size = roundup(extra_size, sizeof(*entity->pads));
	if (num_pads)
		num_inputs = type & UVC_TERM_OUTPUT ? num_pads : num_pads - 1;
	else
		num_inputs = 0;
	size = sizeof(*entity) + extra_size + sizeof(*entity->pads) * num_pads
	     + num_inputs;
	entity = kzalloc(size, GFP_KERNEL);
	if (entity == NULL)
		return NULL;

	entity->id = id;
	entity->type = type;

	/*
	 * Set the GUID for standard entity types. For extension units, the GUID
	 * is initialized by the caller.
	 */
	switch (type) {
	case UVC_EXT_GPIO_UNIT:
		memcpy(entity->guid, uvc_gpio_guid, 16);
		break;
	case UVC_ITT_CAMERA:
		memcpy(entity->guid, uvc_camera_guid, 16);
		break;
	case UVC_ITT_MEDIA_TRANSPORT_INPUT:
		memcpy(entity->guid, uvc_media_transport_input_guid, 16);
		break;
	case UVC_VC_PROCESSING_UNIT:
		memcpy(entity->guid, uvc_processing_guid, 16);
		break;
	}

	entity->num_links = 0;
	entity->num_pads = num_pads;
	entity->pads = ((void *)(entity + 1)) + extra_size;

	for (i = 0; i < num_inputs; ++i)
		entity->pads[i].flags = MEDIA_PAD_FL_SINK;
	if (!UVC_ENTITY_IS_OTERM(entity) && num_pads)
		entity->pads[num_pads-1].flags = MEDIA_PAD_FL_SOURCE;

	entity->bNrInPins = num_inputs;
	entity->baSourceID = (u8 *)(&entity->pads[num_pads]);

	return entity;
}

static void uvc_entity_set_name(struct uvc_device *dev, struct uvc_entity *entity,
				const char *type_name, u8 string_id)
{
	int ret;

	/*
	 * First attempt to read the entity name from the device. If the entity
	 * has no associated string, or if reading the string fails (most
	 * likely due to a buggy firmware), fall back to default names based on
	 * the entity type.
	 */
	if (string_id) {
		ret = usb_string(dev->udev, string_id, entity->name,
				 sizeof(entity->name));
		if (!ret)
			return;
	}

	sprintf(entity->name, "%s %u", type_name, entity->id);
}

/* Parse vendor-specific extensions. */
static int uvc_parse_vendor_control(struct uvc_device *dev,
	const unsigned char *buffer, int buflen)
{
	struct usb_device *udev = dev->udev;
	struct usb_host_interface *alts = dev->intf->cur_altsetting;
	struct uvc_entity *unit;
	unsigned int n, p;
	int handled = 0;

	switch (le16_to_cpu(dev->udev->descriptor.idVendor)) {
	case 0x046d:		/* Logitech */
		if (buffer[1] != 0x41 || buffer[2] != 0x01)
			break;

		/*
		 * Logitech implements several vendor specific functions
		 * through vendor specific extension units (LXU).
		 *
		 * The LXU descriptors are similar to XU descriptors
		 * (see "USB Device Video Class for Video Devices", section
		 * 3.7.2.6 "Extension Unit Descriptor") with the following
		 * differences:
		 *
		 * ----------------------------------------------------------
		 * 0		bLength		1	 Number
		 *	Size of this descriptor, in bytes: 24+p+n*2
		 * ----------------------------------------------------------
		 * 23+p+n	bmControlsType	N	Bitmap
		 *	Individual bits in the set are defined:
		 *	0: Absolute
		 *	1: Relative
		 *
		 *	This bitset is mapped exactly the same as bmControls.
		 * ----------------------------------------------------------
		 * 23+p+n*2	bReserved	1	Boolean
		 * ----------------------------------------------------------
		 * 24+p+n*2	iExtension	1	Index
		 *	Index of a string descriptor that describes this
		 *	extension unit.
		 * ----------------------------------------------------------
		 */
		p = buflen >= 22 ? buffer[21] : 0;
		n = buflen >= 25 + p ? buffer[22+p] : 0;

		if (buflen < 25 + p + 2*n) {
			uvc_dbg(dev, DESCR,
				"device %d videocontrol interface %d EXTENSION_UNIT error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			break;
		}

		unit = uvc_alloc_entity(UVC_VC_EXTENSION_UNIT, buffer[3],
					p + 1, 2*n);
		if (unit == NULL)
			return -ENOMEM;

		memcpy(unit->guid, &buffer[4], 16);
		unit->extension.bNumControls = buffer[20];
		memcpy(unit->baSourceID, &buffer[22], p);
		unit->extension.bControlSize = buffer[22+p];
		unit->extension.bmControls = (u8 *)unit + sizeof(*unit);
		unit->extension.bmControlsType = (u8 *)unit + sizeof(*unit)
					       + n;
		memcpy(unit->extension.bmControls, &buffer[23+p], 2*n);

		uvc_entity_set_name(dev, unit, "Extension", buffer[24+p+2*n]);

		list_add_tail(&unit->list, &dev->entities);
		handled = 1;
		break;
	}

	return handled;
}

static int uvc_parse_standard_control(struct uvc_device *dev,
	const unsigned char *buffer, int buflen)
{
	struct usb_device *udev = dev->udev;
	struct uvc_entity *unit, *term;
	struct usb_interface *intf;
	struct usb_host_interface *alts = dev->intf->cur_altsetting;
	unsigned int i, n, p, len;
	const char *type_name;
	u16 type;

	switch (buffer[2]) {
	case UVC_VC_HEADER:
		n = buflen >= 12 ? buffer[11] : 0;

		if (buflen < 12 + n) {
			uvc_dbg(dev, DESCR,
				"device %d videocontrol interface %d HEADER error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		dev->uvc_version = get_unaligned_le16(&buffer[3]);
		dev->clock_frequency = get_unaligned_le32(&buffer[7]);

		/* Parse all USB Video Streaming interfaces. */
		for (i = 0; i < n; ++i) {
			intf = usb_ifnum_to_if(udev, buffer[12+i]);
			if (intf == NULL) {
				uvc_dbg(dev, DESCR,
					"device %d interface %d doesn't exists\n",
					udev->devnum, i);
				continue;
			}

			uvc_parse_streaming(dev, intf);
		}
		break;

	case UVC_VC_INPUT_TERMINAL:
		if (buflen < 8) {
			uvc_dbg(dev, DESCR,
				"device %d videocontrol interface %d INPUT_TERMINAL error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		/*
		 * Reject invalid terminal types that would cause issues:
		 *
		 * - The high byte must be non-zero, otherwise it would be
		 *   confused with a unit.
		 *
		 * - Bit 15 must be 0, as we use it internally as a terminal
		 *   direction flag.
		 *
		 * Other unknown types are accepted.
		 */
		type = get_unaligned_le16(&buffer[4]);
		if ((type & 0x7f00) == 0 || (type & 0x8000) != 0) {
			uvc_dbg(dev, DESCR,
				"device %d videocontrol interface %d INPUT_TERMINAL %d has invalid type 0x%04x, skipping\n",
				udev->devnum, alts->desc.bInterfaceNumber,
				buffer[3], type);
			return 0;
		}

		n = 0;
		p = 0;
		len = 8;

		if (type == UVC_ITT_CAMERA) {
			n = buflen >= 15 ? buffer[14] : 0;
			len = 15;

		} else if (type == UVC_ITT_MEDIA_TRANSPORT_INPUT) {
			n = buflen >= 9 ? buffer[8] : 0;
			p = buflen >= 10 + n ? buffer[9+n] : 0;
			len = 10;
		}

		if (buflen < len + n + p) {
			uvc_dbg(dev, DESCR,
				"device %d videocontrol interface %d INPUT_TERMINAL error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		term = uvc_alloc_entity(type | UVC_TERM_INPUT, buffer[3],
					1, n + p);
		if (term == NULL)
			return -ENOMEM;

		if (UVC_ENTITY_TYPE(term) == UVC_ITT_CAMERA) {
			term->camera.bControlSize = n;
			term->camera.bmControls = (u8 *)term + sizeof(*term);
			term->camera.wObjectiveFocalLengthMin =
				get_unaligned_le16(&buffer[8]);
			term->camera.wObjectiveFocalLengthMax =
				get_unaligned_le16(&buffer[10]);
			term->camera.wOcularFocalLength =
				get_unaligned_le16(&buffer[12]);
			memcpy(term->camera.bmControls, &buffer[15], n);
		} else if (UVC_ENTITY_TYPE(term) ==
			   UVC_ITT_MEDIA_TRANSPORT_INPUT) {
			term->media.bControlSize = n;
			term->media.bmControls = (u8 *)term + sizeof(*term);
			term->media.bTransportModeSize = p;
			term->media.bmTransportModes = (u8 *)term
						     + sizeof(*term) + n;
			memcpy(term->media.bmControls, &buffer[9], n);
			memcpy(term->media.bmTransportModes, &buffer[10+n], p);
		}

		if (UVC_ENTITY_TYPE(term) == UVC_ITT_CAMERA)
			type_name = "Camera";
		else if (UVC_ENTITY_TYPE(term) == UVC_ITT_MEDIA_TRANSPORT_INPUT)
			type_name = "Media";
		else
			type_name = "Input";

		uvc_entity_set_name(dev, term, type_name, buffer[7]);

		list_add_tail(&term->list, &dev->entities);
		break;

	case UVC_VC_OUTPUT_TERMINAL:
		if (buflen < 9) {
			uvc_dbg(dev, DESCR,
				"device %d videocontrol interface %d OUTPUT_TERMINAL error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		/*
		 * Make sure the terminal type MSB is not null, otherwise it
		 * could be confused with a unit.
		 */
		type = get_unaligned_le16(&buffer[4]);
		if ((type & 0xff00) == 0) {
			uvc_dbg(dev, DESCR,
				"device %d videocontrol interface %d OUTPUT_TERMINAL %d has invalid type 0x%04x, skipping\n",
				udev->devnum, alts->desc.bInterfaceNumber,
				buffer[3], type);
			return 0;
		}

		term = uvc_alloc_entity(type | UVC_TERM_OUTPUT, buffer[3],
					1, 0);
		if (term == NULL)
			return -ENOMEM;

		memcpy(term->baSourceID, &buffer[7], 1);

		uvc_entity_set_name(dev, term, "Output", buffer[8]);

		list_add_tail(&term->list, &dev->entities);
		break;

	case UVC_VC_SELECTOR_UNIT:
		p = buflen >= 5 ? buffer[4] : 0;

		if (buflen < 5 || buflen < 6 + p) {
			uvc_dbg(dev, DESCR,
				"device %d videocontrol interface %d SELECTOR_UNIT error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		unit = uvc_alloc_entity(buffer[2], buffer[3], p + 1, 0);
		if (unit == NULL)
			return -ENOMEM;

		memcpy(unit->baSourceID, &buffer[5], p);

		uvc_entity_set_name(dev, unit, "Selector", buffer[5+p]);

		list_add_tail(&unit->list, &dev->entities);
		break;

	case UVC_VC_PROCESSING_UNIT:
		n = buflen >= 8 ? buffer[7] : 0;
		p = dev->uvc_version >= 0x0110 ? 10 : 9;

		if (buflen < p + n) {
			uvc_dbg(dev, DESCR,
				"device %d videocontrol interface %d PROCESSING_UNIT error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		unit = uvc_alloc_entity(buffer[2], buffer[3], 2, n);
		if (unit == NULL)
			return -ENOMEM;

		memcpy(unit->baSourceID, &buffer[4], 1);
		unit->processing.wMaxMultiplier =
			get_unaligned_le16(&buffer[5]);
		unit->processing.bControlSize = buffer[7];
		unit->processing.bmControls = (u8 *)unit + sizeof(*unit);
		memcpy(unit->processing.bmControls, &buffer[8], n);
		if (dev->uvc_version >= 0x0110)
			unit->processing.bmVideoStandards = buffer[9+n];

		uvc_entity_set_name(dev, unit, "Processing", buffer[8+n]);

		list_add_tail(&unit->list, &dev->entities);
		break;

	case UVC_VC_EXTENSION_UNIT:
		p = buflen >= 22 ? buffer[21] : 0;
		n = buflen >= 24 + p ? buffer[22+p] : 0;

		if (buflen < 24 + p + n) {
			uvc_dbg(dev, DESCR,
				"device %d videocontrol interface %d EXTENSION_UNIT error\n",
				udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		unit = uvc_alloc_entity(buffer[2], buffer[3], p + 1, n);
		if (unit == NULL)
			return -ENOMEM;

		memcpy(unit->guid, &buffer[4], 16);
		unit->extension.bNumControls = buffer[20];
		memcpy(unit->baSourceID, &buffer[22], p);
		unit->extension.bControlSize = buffer[22+p];
		unit->extension.bmControls = (u8 *)unit + sizeof(*unit);
		memcpy(unit->extension.bmControls, &buffer[23+p], n);

		uvc_entity_set_name(dev, unit, "Extension", buffer[23+p+n]);

		list_add_tail(&unit->list, &dev->entities);
		break;

	default:
		uvc_dbg(dev, DESCR,
			"Found an unknown CS_INTERFACE descriptor (%u)\n",
			buffer[2]);
		break;
	}

	return 0;
}

static int uvc_parse_control(struct uvc_device *dev)
{
	struct usb_host_interface *alts = dev->intf->cur_altsetting;
	const unsigned char *buffer = alts->extra;
	int buflen = alts->extralen;
	int ret;

	/*
	 * Parse the default alternate setting only, as the UVC specification
	 * defines a single alternate setting, the default alternate setting
	 * zero.
	 */

	while (buflen > 2) {
		if (uvc_parse_vendor_control(dev, buffer, buflen) ||
		    buffer[1] != USB_DT_CS_INTERFACE)
			goto next_descriptor;

		ret = uvc_parse_standard_control(dev, buffer, buflen);
		if (ret < 0)
			return ret;

next_descriptor:
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	/*
	 * Check if the optional status endpoint is present. Built-in iSight
	 * webcams have an interrupt endpoint but spit proprietary data that
	 * don't conform to the UVC status endpoint messages. Don't try to
	 * handle the interrupt endpoint for those cameras.
	 */
	if (alts->desc.bNumEndpoints == 1 &&
	    !(dev->quirks & UVC_QUIRK_BUILTIN_ISIGHT)) {
		struct usb_host_endpoint *ep = &alts->endpoint[0];
		struct usb_endpoint_descriptor *desc = &ep->desc;

		if (usb_endpoint_is_int_in(desc) &&
		    le16_to_cpu(desc->wMaxPacketSize) >= 8 &&
		    desc->bInterval != 0) {
			uvc_dbg(dev, DESCR,
				"Found a Status endpoint (addr %02x)\n",
				desc->bEndpointAddress);
			dev->int_ep = ep;
		}
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Privacy GPIO
 */

static void uvc_gpio_event(struct uvc_device *dev)
{
	struct uvc_entity *unit = dev->gpio_unit;
	struct uvc_video_chain *chain;
	u8 new_val;

	if (!unit)
		return;

	new_val = gpiod_get_value_cansleep(unit->gpio.gpio_privacy);

	/* GPIO entities are always on the first chain. */
	chain = list_first_entry(&dev->chains, struct uvc_video_chain, list);
	uvc_ctrl_status_event(chain, unit->controls, &new_val);
}

static int uvc_gpio_get_cur(struct uvc_device *dev, struct uvc_entity *entity,
			    u8 cs, void *data, u16 size)
{
	if (cs != UVC_CT_PRIVACY_CONTROL || size < 1)
		return -EINVAL;

	*(u8 *)data = gpiod_get_value_cansleep(entity->gpio.gpio_privacy);

	return 0;
}

static int uvc_gpio_get_info(struct uvc_device *dev, struct uvc_entity *entity,
			     u8 cs, u8 *caps)
{
	if (cs != UVC_CT_PRIVACY_CONTROL)
		return -EINVAL;

	*caps = UVC_CONTROL_CAP_GET | UVC_CONTROL_CAP_AUTOUPDATE;
	return 0;
}

static irqreturn_t uvc_gpio_irq(int irq, void *data)
{
	struct uvc_device *dev = data;

	uvc_gpio_event(dev);
	return IRQ_HANDLED;
}

static int uvc_gpio_parse(struct uvc_device *dev)
{
	struct uvc_entity *unit;
	struct gpio_desc *gpio_privacy;
	int irq;

	gpio_privacy = devm_gpiod_get_optional(&dev->intf->dev, "privacy",
					       GPIOD_IN);
	if (IS_ERR_OR_NULL(gpio_privacy))
		return PTR_ERR_OR_ZERO(gpio_privacy);

	irq = gpiod_to_irq(gpio_privacy);
	if (irq < 0)
		return dev_err_probe(&dev->intf->dev, irq,
				     "No IRQ for privacy GPIO\n");

	unit = uvc_alloc_entity(UVC_EXT_GPIO_UNIT, UVC_EXT_GPIO_UNIT_ID, 0, 1);
	if (!unit)
		return -ENOMEM;

	unit->gpio.gpio_privacy = gpio_privacy;
	unit->gpio.irq = irq;
	unit->gpio.bControlSize = 1;
	unit->gpio.bmControls = (u8 *)unit + sizeof(*unit);
	unit->gpio.bmControls[0] = 1;
	unit->get_cur = uvc_gpio_get_cur;
	unit->get_info = uvc_gpio_get_info;
	strscpy(unit->name, "GPIO", sizeof(unit->name));

	list_add_tail(&unit->list, &dev->entities);

	dev->gpio_unit = unit;

	return 0;
}

static int uvc_gpio_init_irq(struct uvc_device *dev)
{
	struct uvc_entity *unit = dev->gpio_unit;
	int ret;

	if (!unit || unit->gpio.irq < 0)
		return 0;

	ret = request_threaded_irq(unit->gpio.irq, NULL, uvc_gpio_irq,
				   IRQF_ONESHOT | IRQF_TRIGGER_FALLING |
				   IRQF_TRIGGER_RISING,
				   "uvc_privacy_gpio", dev);

	unit->gpio.initialized = !ret;

	return ret;
}

static void uvc_gpio_deinit(struct uvc_device *dev)
{
	if (!dev->gpio_unit || !dev->gpio_unit->gpio.initialized)
		return;

	free_irq(dev->gpio_unit->gpio.irq, dev);
}

/* ------------------------------------------------------------------------
 * UVC device scan
 */

/*
 * Scan the UVC descriptors to locate a chain starting at an Output Terminal
 * and containing the following units:
 *
 * - one or more Output Terminals (USB Streaming or Display)
 * - zero or one Processing Unit
 * - zero, one or more single-input Selector Units
 * - zero or one multiple-input Selector Units, provided all inputs are
 *   connected to input terminals
 * - zero, one or mode single-input Extension Units
 * - one or more Input Terminals (Camera, External or USB Streaming)
 *
 * The terminal and units must match on of the following structures:
 *
 * ITT_*(0) -> +---------+    +---------+    +---------+ -> TT_STREAMING(0)
 * ...         | SU{0,1} | -> | PU{0,1} | -> | XU{0,n} |    ...
 * ITT_*(n) -> +---------+    +---------+    +---------+ -> TT_STREAMING(n)
 *
 *                 +---------+    +---------+ -> OTT_*(0)
 * TT_STREAMING -> | PU{0,1} | -> | XU{0,n} |    ...
 *                 +---------+    +---------+ -> OTT_*(n)
 *
 * The Processing Unit and Extension Units can be in any order. Additional
 * Extension Units connected to the main chain as single-unit branches are
 * also supported. Single-input Selector Units are ignored.
 */
static int uvc_scan_chain_entity(struct uvc_video_chain *chain,
	struct uvc_entity *entity)
{
	switch (UVC_ENTITY_TYPE(entity)) {
	case UVC_VC_EXTENSION_UNIT:
		uvc_dbg_cont(PROBE, " <- XU %d", entity->id);

		if (entity->bNrInPins != 1) {
			uvc_dbg(chain->dev, DESCR,
				"Extension unit %d has more than 1 input pin\n",
				entity->id);
			return -1;
		}

		break;

	case UVC_VC_PROCESSING_UNIT:
		uvc_dbg_cont(PROBE, " <- PU %d", entity->id);

		if (chain->processing != NULL) {
			uvc_dbg(chain->dev, DESCR,
				"Found multiple Processing Units in chain\n");
			return -1;
		}

		chain->processing = entity;
		break;

	case UVC_VC_SELECTOR_UNIT:
		uvc_dbg_cont(PROBE, " <- SU %d", entity->id);

		/* Single-input selector units are ignored. */
		if (entity->bNrInPins == 1)
			break;

		if (chain->selector != NULL) {
			uvc_dbg(chain->dev, DESCR,
				"Found multiple Selector Units in chain\n");
			return -1;
		}

		chain->selector = entity;
		break;

	case UVC_ITT_VENDOR_SPECIFIC:
	case UVC_ITT_CAMERA:
	case UVC_ITT_MEDIA_TRANSPORT_INPUT:
		uvc_dbg_cont(PROBE, " <- IT %d\n", entity->id);

		break;

	case UVC_OTT_VENDOR_SPECIFIC:
	case UVC_OTT_DISPLAY:
	case UVC_OTT_MEDIA_TRANSPORT_OUTPUT:
		uvc_dbg_cont(PROBE, " OT %d", entity->id);

		break;

	case UVC_TT_STREAMING:
		if (UVC_ENTITY_IS_ITERM(entity))
			uvc_dbg_cont(PROBE, " <- IT %d\n", entity->id);
		else
			uvc_dbg_cont(PROBE, " OT %d", entity->id);

		break;

	default:
		uvc_dbg(chain->dev, DESCR,
			"Unsupported entity type 0x%04x found in chain\n",
			UVC_ENTITY_TYPE(entity));
		return -1;
	}

	list_add_tail(&entity->chain, &chain->entities);
	return 0;
}

static int uvc_scan_chain_forward(struct uvc_video_chain *chain,
	struct uvc_entity *entity, struct uvc_entity *prev)
{
	struct uvc_entity *forward;
	int found;

	/* Forward scan */
	forward = NULL;
	found = 0;

	while (1) {
		forward = uvc_entity_by_reference(chain->dev, entity->id,
			forward);
		if (forward == NULL)
			break;
		if (forward == prev)
			continue;
		if (forward->chain.next || forward->chain.prev) {
			uvc_dbg(chain->dev, DESCR,
				"Found reference to entity %d already in chain\n",
				forward->id);
			return -EINVAL;
		}

		switch (UVC_ENTITY_TYPE(forward)) {
		case UVC_VC_EXTENSION_UNIT:
			if (forward->bNrInPins != 1) {
				uvc_dbg(chain->dev, DESCR,
					"Extension unit %d has more than 1 input pin\n",
					forward->id);
				return -EINVAL;
			}

			/*
			 * Some devices reference an output terminal as the
			 * source of extension units. This is incorrect, as
			 * output terminals only have an input pin, and thus
			 * can't be connected to any entity in the forward
			 * direction. The resulting topology would cause issues
			 * when registering the media controller graph. To
			 * avoid this problem, connect the extension unit to
			 * the source of the output terminal instead.
			 */
			if (UVC_ENTITY_IS_OTERM(entity)) {
				struct uvc_entity *source;

				source = uvc_entity_by_id(chain->dev,
							  entity->baSourceID[0]);
				if (!source) {
					uvc_dbg(chain->dev, DESCR,
						"Can't connect extension unit %u in chain\n",
						forward->id);
					break;
				}

				forward->baSourceID[0] = source->id;
			}

			list_add_tail(&forward->chain, &chain->entities);
			if (!found)
				uvc_dbg_cont(PROBE, " (->");

			uvc_dbg_cont(PROBE, " XU %d", forward->id);
			found = 1;
			break;

		case UVC_OTT_VENDOR_SPECIFIC:
		case UVC_OTT_DISPLAY:
		case UVC_OTT_MEDIA_TRANSPORT_OUTPUT:
		case UVC_TT_STREAMING:
			if (UVC_ENTITY_IS_ITERM(forward)) {
				uvc_dbg(chain->dev, DESCR,
					"Unsupported input terminal %u\n",
					forward->id);
				return -EINVAL;
			}

			if (UVC_ENTITY_IS_OTERM(entity)) {
				uvc_dbg(chain->dev, DESCR,
					"Unsupported connection between output terminals %u and %u\n",
					entity->id, forward->id);
				break;
			}

			list_add_tail(&forward->chain, &chain->entities);
			if (!found)
				uvc_dbg_cont(PROBE, " (->");

			uvc_dbg_cont(PROBE, " OT %d", forward->id);
			found = 1;
			break;
		}
	}
	if (found)
		uvc_dbg_cont(PROBE, ")");

	return 0;
}

static int uvc_scan_chain_backward(struct uvc_video_chain *chain,
	struct uvc_entity **_entity)
{
	struct uvc_entity *entity = *_entity;
	struct uvc_entity *term;
	int id = -EINVAL, i;

	switch (UVC_ENTITY_TYPE(entity)) {
	case UVC_VC_EXTENSION_UNIT:
	case UVC_VC_PROCESSING_UNIT:
		id = entity->baSourceID[0];
		break;

	case UVC_VC_SELECTOR_UNIT:
		/* Single-input selector units are ignored. */
		if (entity->bNrInPins == 1) {
			id = entity->baSourceID[0];
			break;
		}

		uvc_dbg_cont(PROBE, " <- IT");

		chain->selector = entity;
		for (i = 0; i < entity->bNrInPins; ++i) {
			id = entity->baSourceID[i];
			term = uvc_entity_by_id(chain->dev, id);
			if (term == NULL || !UVC_ENTITY_IS_ITERM(term)) {
				uvc_dbg(chain->dev, DESCR,
					"Selector unit %d input %d isn't connected to an input terminal\n",
					entity->id, i);
				return -1;
			}

			if (term->chain.next || term->chain.prev) {
				uvc_dbg(chain->dev, DESCR,
					"Found reference to entity %d already in chain\n",
					term->id);
				return -EINVAL;
			}

			uvc_dbg_cont(PROBE, " %d", term->id);

			list_add_tail(&term->chain, &chain->entities);
			uvc_scan_chain_forward(chain, term, entity);
		}

		uvc_dbg_cont(PROBE, "\n");

		id = 0;
		break;

	case UVC_ITT_VENDOR_SPECIFIC:
	case UVC_ITT_CAMERA:
	case UVC_ITT_MEDIA_TRANSPORT_INPUT:
	case UVC_OTT_VENDOR_SPECIFIC:
	case UVC_OTT_DISPLAY:
	case UVC_OTT_MEDIA_TRANSPORT_OUTPUT:
	case UVC_TT_STREAMING:
		id = UVC_ENTITY_IS_OTERM(entity) ? entity->baSourceID[0] : 0;
		break;
	}

	if (id <= 0) {
		*_entity = NULL;
		return id;
	}

	entity = uvc_entity_by_id(chain->dev, id);
	if (entity == NULL) {
		uvc_dbg(chain->dev, DESCR,
			"Found reference to unknown entity %d\n", id);
		return -EINVAL;
	}

	*_entity = entity;
	return 0;
}

static int uvc_scan_chain(struct uvc_video_chain *chain,
			  struct uvc_entity *term)
{
	struct uvc_entity *entity, *prev;

	uvc_dbg(chain->dev, PROBE, "Scanning UVC chain:");

	entity = term;
	prev = NULL;

	while (entity != NULL) {
		/* Entity must not be part of an existing chain */
		if (entity->chain.next || entity->chain.prev) {
			uvc_dbg(chain->dev, DESCR,
				"Found reference to entity %d already in chain\n",
				entity->id);
			return -EINVAL;
		}

		/* Process entity */
		if (uvc_scan_chain_entity(chain, entity) < 0)
			return -EINVAL;

		/* Forward scan */
		if (uvc_scan_chain_forward(chain, entity, prev) < 0)
			return -EINVAL;

		/* Backward scan */
		prev = entity;
		if (uvc_scan_chain_backward(chain, &entity) < 0)
			return -EINVAL;
	}

	return 0;
}

static unsigned int uvc_print_terms(struct list_head *terms, u16 dir,
		char *buffer)
{
	struct uvc_entity *term;
	unsigned int nterms = 0;
	char *p = buffer;

	list_for_each_entry(term, terms, chain) {
		if (!UVC_ENTITY_IS_TERM(term) ||
		    UVC_TERM_DIRECTION(term) != dir)
			continue;

		if (nterms)
			p += sprintf(p, ",");
		if (++nterms >= 4) {
			p += sprintf(p, "...");
			break;
		}
		p += sprintf(p, "%u", term->id);
	}

	return p - buffer;
}

static const char *uvc_print_chain(struct uvc_video_chain *chain)
{
	static char buffer[43];
	char *p = buffer;

	p += uvc_print_terms(&chain->entities, UVC_TERM_INPUT, p);
	p += sprintf(p, " -> ");
	uvc_print_terms(&chain->entities, UVC_TERM_OUTPUT, p);

	return buffer;
}

static struct uvc_video_chain *uvc_alloc_chain(struct uvc_device *dev)
{
	struct uvc_video_chain *chain;

	chain = kzalloc(sizeof(*chain), GFP_KERNEL);
	if (chain == NULL)
		return NULL;

	INIT_LIST_HEAD(&chain->entities);
	mutex_init(&chain->ctrl_mutex);
	chain->dev = dev;
	v4l2_prio_init(&chain->prio);

	return chain;
}

/*
 * Fallback heuristic for devices that don't connect units and terminals in a
 * valid chain.
 *
 * Some devices have invalid baSourceID references, causing uvc_scan_chain()
 * to fail, but if we just take the entities we can find and put them together
 * in the most sensible chain we can think of, turns out they do work anyway.
 * Note: This heuristic assumes there is a single chain.
 *
 * At the time of writing, devices known to have such a broken chain are
 *  - Acer Integrated Camera (5986:055a)
 *  - Realtek rtl157a7 (0bda:57a7)
 */
static int uvc_scan_fallback(struct uvc_device *dev)
{
	struct uvc_video_chain *chain;
	struct uvc_entity *iterm = NULL;
	struct uvc_entity *oterm = NULL;
	struct uvc_entity *entity;
	struct uvc_entity *prev;

	/*
	 * Start by locating the input and output terminals. We only support
	 * devices with exactly one of each for now.
	 */
	list_for_each_entry(entity, &dev->entities, list) {
		if (UVC_ENTITY_IS_ITERM(entity)) {
			if (iterm)
				return -EINVAL;
			iterm = entity;
		}

		if (UVC_ENTITY_IS_OTERM(entity)) {
			if (oterm)
				return -EINVAL;
			oterm = entity;
		}
	}

	if (iterm == NULL || oterm == NULL)
		return -EINVAL;

	/* Allocate the chain and fill it. */
	chain = uvc_alloc_chain(dev);
	if (chain == NULL)
		return -ENOMEM;

	if (uvc_scan_chain_entity(chain, oterm) < 0)
		goto error;

	prev = oterm;

	/*
	 * Add all Processing and Extension Units with two pads. The order
	 * doesn't matter much, use reverse list traversal to connect units in
	 * UVC descriptor order as we build the chain from output to input. This
	 * leads to units appearing in the order meant by the manufacturer for
	 * the cameras known to require this heuristic.
	 */
	list_for_each_entry_reverse(entity, &dev->entities, list) {
		if (entity->type != UVC_VC_PROCESSING_UNIT &&
		    entity->type != UVC_VC_EXTENSION_UNIT)
			continue;

		if (entity->num_pads != 2)
			continue;

		if (uvc_scan_chain_entity(chain, entity) < 0)
			goto error;

		prev->baSourceID[0] = entity->id;
		prev = entity;
	}

	if (uvc_scan_chain_entity(chain, iterm) < 0)
		goto error;

	prev->baSourceID[0] = iterm->id;

	list_add_tail(&chain->list, &dev->chains);

	uvc_dbg(dev, PROBE, "Found a video chain by fallback heuristic (%s)\n",
		uvc_print_chain(chain));

	return 0;

error:
	kfree(chain);
	return -EINVAL;
}

/*
 * Scan the device for video chains and register video devices.
 *
 * Chains are scanned starting at their output terminals and walked backwards.
 */
static int uvc_scan_device(struct uvc_device *dev)
{
	struct uvc_video_chain *chain;
	struct uvc_entity *term;

	list_for_each_entry(term, &dev->entities, list) {
		if (!UVC_ENTITY_IS_OTERM(term))
			continue;

		/*
		 * If the terminal is already included in a chain, skip it.
		 * This can happen for chains that have multiple output
		 * terminals, where all output terminals beside the first one
		 * will be inserted in the chain in forward scans.
		 */
		if (term->chain.next || term->chain.prev)
			continue;

		chain = uvc_alloc_chain(dev);
		if (chain == NULL)
			return -ENOMEM;

		term->flags |= UVC_ENTITY_FLAG_DEFAULT;

		if (uvc_scan_chain(chain, term) < 0) {
			kfree(chain);
			continue;
		}

		uvc_dbg(dev, PROBE, "Found a valid video chain (%s)\n",
			uvc_print_chain(chain));

		list_add_tail(&chain->list, &dev->chains);
	}

	if (list_empty(&dev->chains))
		uvc_scan_fallback(dev);

	if (list_empty(&dev->chains)) {
		dev_info(&dev->udev->dev, "No valid video chain found.\n");
		return -1;
	}

	/* Add GPIO entity to the first chain. */
	if (dev->gpio_unit) {
		chain = list_first_entry(&dev->chains,
					 struct uvc_video_chain, list);
		list_add_tail(&dev->gpio_unit->chain, &chain->entities);
	}

	return 0;
}

/* ------------------------------------------------------------------------
 * Video device registration and unregistration
 */

/*
 * Delete the UVC device.
 *
 * Called by the kernel when the last reference to the uvc_device structure
 * is released.
 *
 * As this function is called after or during disconnect(), all URBs have
 * already been cancelled by the USB core. There is no need to kill the
 * interrupt URB manually.
 */
static void uvc_delete(struct kref *kref)
{
	struct uvc_device *dev = container_of(kref, struct uvc_device, ref);
	struct list_head *p, *n;

	uvc_status_cleanup(dev);
	uvc_ctrl_cleanup_device(dev);

	usb_put_intf(dev->intf);
	usb_put_dev(dev->udev);

#ifdef CONFIG_MEDIA_CONTROLLER
	media_device_cleanup(&dev->mdev);
#endif

	list_for_each_safe(p, n, &dev->chains) {
		struct uvc_video_chain *chain;

		chain = list_entry(p, struct uvc_video_chain, list);
		kfree(chain);
	}

	list_for_each_safe(p, n, &dev->entities) {
		struct uvc_entity *entity;

		entity = list_entry(p, struct uvc_entity, list);
#ifdef CONFIG_MEDIA_CONTROLLER
		uvc_mc_cleanup_entity(entity);
#endif
		kfree(entity);
	}

	list_for_each_safe(p, n, &dev->streams) {
		struct uvc_streaming *streaming;

		streaming = list_entry(p, struct uvc_streaming, list);
		usb_driver_release_interface(&uvc_driver, streaming->intf);
		uvc_stream_delete(streaming);
	}

	kfree(dev);
}

static void uvc_release(struct video_device *vdev)
{
	struct uvc_streaming *stream = video_get_drvdata(vdev);
	struct uvc_device *dev = stream->dev;

	kref_put(&dev->ref, uvc_delete);
}

/*
 * Unregister the video devices.
 */
static void uvc_unregister_video(struct uvc_device *dev)
{
	struct uvc_streaming *stream;

	uvc_gpio_deinit(dev);

	list_for_each_entry(stream, &dev->streams, list) {
		/* Nothing to do here, continue. */
		if (!video_is_registered(&stream->vdev))
			continue;

		/*
		 * For stream->vdev we follow the same logic as:
		 * vb2_video_unregister_device().
		 */

		/* 1. Take a reference to vdev */
		get_device(&stream->vdev.dev);

		/* 2. Ensure that no new ioctls can be called. */
		video_unregister_device(&stream->vdev);

		/* 3. Wait for old ioctls to finish. */
		mutex_lock(&stream->mutex);

		/* 4. Stop streaming. */
		uvc_queue_release(&stream->queue);

		mutex_unlock(&stream->mutex);

		put_device(&stream->vdev.dev);

		/*
		 * For stream->meta.vdev we can directly call:
		 * vb2_video_unregister_device().
		 */
		vb2_video_unregister_device(&stream->meta.vdev);

		/*
		 * Now both vdevs are not streaming and all the ioctls will
		 * return -ENODEV.
		 */

		uvc_debugfs_cleanup_stream(stream);
	}

	uvc_status_unregister(dev);

	if (dev->vdev.dev)
		v4l2_device_unregister(&dev->vdev);
#ifdef CONFIG_MEDIA_CONTROLLER
	if (media_devnode_is_registered(dev->mdev.devnode))
		media_device_unregister(&dev->mdev);
#endif
}

int uvc_register_video_device(struct uvc_device *dev,
			      struct uvc_streaming *stream,
			      struct video_device *vdev,
			      struct uvc_video_queue *queue,
			      enum v4l2_buf_type type,
			      const struct v4l2_file_operations *fops,
			      const struct v4l2_ioctl_ops *ioctl_ops)
{
	int ret;

	/* Initialize the video buffers queue. */
	ret = uvc_queue_init(queue, type);
	if (ret)
		return ret;

	/* Register the device with V4L. */

	/*
	 * We already hold a reference to dev->udev. The video device will be
	 * unregistered before the reference is released, so we don't need to
	 * get another one.
	 */
	vdev->v4l2_dev = &dev->vdev;
	vdev->fops = fops;
	vdev->ioctl_ops = ioctl_ops;
	vdev->release = uvc_release;
	vdev->prio = &stream->chain->prio;
	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		vdev->vfl_dir = VFL_DIR_TX;
	else
		vdev->vfl_dir = VFL_DIR_RX;

	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	default:
		vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		vdev->device_caps = V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_STREAMING;
		break;
	case V4L2_BUF_TYPE_META_CAPTURE:
		vdev->device_caps = V4L2_CAP_META_CAPTURE | V4L2_CAP_STREAMING;
		break;
	}

	strscpy(vdev->name, dev->name, sizeof(vdev->name));

	/*
	 * Set the driver data before calling video_register_device, otherwise
	 * the file open() handler might race us.
	 */
	video_set_drvdata(vdev, stream);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		dev_err(&stream->intf->dev,
			"Failed to register %s device (%d).\n",
			v4l2_type_names[type], ret);
		return ret;
	}

	kref_get(&dev->ref);
	return 0;
}

static int uvc_register_video(struct uvc_device *dev,
		struct uvc_streaming *stream)
{
	int ret;

	/* Initialize the streaming interface with default parameters. */
	ret = uvc_video_init(stream);
	if (ret < 0) {
		dev_err(&stream->intf->dev,
			"Failed to initialize the device (%d).\n", ret);
		return ret;
	}

	if (stream->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		stream->chain->caps |= V4L2_CAP_VIDEO_CAPTURE
			| V4L2_CAP_META_CAPTURE;
	else
		stream->chain->caps |= V4L2_CAP_VIDEO_OUTPUT;

	uvc_debugfs_init_stream(stream);

	/* Register the device with V4L. */
	return uvc_register_video_device(dev, stream, &stream->vdev,
					 &stream->queue, stream->type,
					 &uvc_fops, &uvc_ioctl_ops);
}

/*
 * Register all video devices in all chains.
 */
static int uvc_register_terms(struct uvc_device *dev,
	struct uvc_video_chain *chain)
{
	struct uvc_streaming *stream;
	struct uvc_entity *term;
	int ret;

	list_for_each_entry(term, &chain->entities, chain) {
		if (UVC_ENTITY_TYPE(term) != UVC_TT_STREAMING)
			continue;

		stream = uvc_stream_by_id(dev, term->id);
		if (stream == NULL) {
			dev_info(&dev->udev->dev,
				 "No streaming interface found for terminal %u.",
				 term->id);
			continue;
		}

		stream->chain = chain;
		ret = uvc_register_video(dev, stream);
		if (ret < 0)
			return ret;

		/*
		 * Register a metadata node, but ignore a possible failure,
		 * complete registration of video nodes anyway.
		 */
		uvc_meta_register(stream);

		term->vdev = &stream->vdev;
	}

	return 0;
}

static int uvc_register_chains(struct uvc_device *dev)
{
	struct uvc_video_chain *chain;
	int ret;

	list_for_each_entry(chain, &dev->chains, list) {
		ret = uvc_register_terms(dev, chain);
		if (ret < 0)
			return ret;

#ifdef CONFIG_MEDIA_CONTROLLER
		ret = uvc_mc_register_entities(chain);
		if (ret < 0)
			dev_info(&dev->udev->dev,
				 "Failed to register entities (%d).\n", ret);
#endif
	}

	return 0;
}

/* ------------------------------------------------------------------------
 * USB probe, disconnect, suspend and resume
 */

static const struct uvc_device_info uvc_quirk_none = { 0 };

static int uvc_probe(struct usb_interface *intf,
		     const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct uvc_device *dev;
	const struct uvc_device_info *info =
		(const struct uvc_device_info *)id->driver_info;
	int function;
	int ret;

	/* Allocate memory for the device and initialize it. */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&dev->entities);
	INIT_LIST_HEAD(&dev->chains);
	INIT_LIST_HEAD(&dev->streams);
	kref_init(&dev->ref);
	atomic_set(&dev->nmappings, 0);

	dev->udev = usb_get_dev(udev);
	dev->intf = usb_get_intf(intf);
	dev->intfnum = intf->cur_altsetting->desc.bInterfaceNumber;
	dev->info = info ? info : &uvc_quirk_none;
	dev->quirks = uvc_quirks_param == -1
		    ? dev->info->quirks : uvc_quirks_param;

	if (id->idVendor && id->idProduct)
		uvc_dbg(dev, PROBE, "Probing known UVC device %s (%04x:%04x)\n",
			udev->devpath, id->idVendor, id->idProduct);
	else
		uvc_dbg(dev, PROBE, "Probing generic UVC device %s\n",
			udev->devpath);

	if (udev->product != NULL)
		strscpy(dev->name, udev->product, sizeof(dev->name));
	else
		snprintf(dev->name, sizeof(dev->name),
			 "UVC Camera (%04x:%04x)",
			 le16_to_cpu(udev->descriptor.idVendor),
			 le16_to_cpu(udev->descriptor.idProduct));

	/*
	 * Add iFunction or iInterface to names when available as additional
	 * distinguishers between interfaces. iFunction is prioritized over
	 * iInterface which matches Windows behavior at the point of writing.
	 */
	if (intf->intf_assoc && intf->intf_assoc->iFunction != 0)
		function = intf->intf_assoc->iFunction;
	else
		function = intf->cur_altsetting->desc.iInterface;
	if (function != 0) {
		size_t len;

		strlcat(dev->name, ": ", sizeof(dev->name));
		len = strlen(dev->name);
		usb_string(udev, function, dev->name + len,
			   sizeof(dev->name) - len);
	}

	/* Initialize the media device. */
#ifdef CONFIG_MEDIA_CONTROLLER
	dev->mdev.dev = &intf->dev;
	strscpy(dev->mdev.model, dev->name, sizeof(dev->mdev.model));
	if (udev->serial)
		strscpy(dev->mdev.serial, udev->serial,
			sizeof(dev->mdev.serial));
	usb_make_path(udev, dev->mdev.bus_info, sizeof(dev->mdev.bus_info));
	dev->mdev.hw_revision = le16_to_cpu(udev->descriptor.bcdDevice);
	media_device_init(&dev->mdev);

	dev->vdev.mdev = &dev->mdev;
#endif

	/* Parse the Video Class control descriptor. */
	if (uvc_parse_control(dev) < 0) {
		uvc_dbg(dev, PROBE, "Unable to parse UVC descriptors\n");
		goto error;
	}

	/* Parse the associated GPIOs. */
	if (uvc_gpio_parse(dev) < 0) {
		uvc_dbg(dev, PROBE, "Unable to parse UVC GPIOs\n");
		goto error;
	}

	dev_info(&dev->udev->dev, "Found UVC %u.%02x device %s (%04x:%04x)\n",
		 dev->uvc_version >> 8, dev->uvc_version & 0xff,
		 udev->product ? udev->product : "<unnamed>",
		 le16_to_cpu(udev->descriptor.idVendor),
		 le16_to_cpu(udev->descriptor.idProduct));

	if (dev->quirks != dev->info->quirks) {
		dev_info(&dev->udev->dev,
			 "Forcing device quirks to 0x%x by module parameter for testing purpose.\n",
			 dev->quirks);
		dev_info(&dev->udev->dev,
			 "Please report required quirks to the linux-media mailing list.\n");
	}

	if (dev->info->uvc_version) {
		dev->uvc_version = dev->info->uvc_version;
		dev_info(&dev->udev->dev, "Forcing UVC version to %u.%02x\n",
			 dev->uvc_version >> 8, dev->uvc_version & 0xff);
	}

	/* Register the V4L2 device. */
	if (v4l2_device_register(&intf->dev, &dev->vdev) < 0)
		goto error;

	/* Scan the device for video chains. */
	if (uvc_scan_device(dev) < 0)
		goto error;

	/* Initialize controls. */
	if (uvc_ctrl_init_device(dev) < 0)
		goto error;

	/* Register video device nodes. */
	if (uvc_register_chains(dev) < 0)
		goto error;

#ifdef CONFIG_MEDIA_CONTROLLER
	/* Register the media device node */
	if (media_device_register(&dev->mdev) < 0)
		goto error;
#endif
	/* Save our data pointer in the interface data. */
	usb_set_intfdata(intf, dev);

	/* Initialize the interrupt URB. */
	ret = uvc_status_init(dev);
	if (ret < 0) {
		dev_info(&dev->udev->dev,
			 "Unable to initialize the status endpoint (%d), status interrupt will not be supported.\n",
			 ret);
	}

	ret = uvc_gpio_init_irq(dev);
	if (ret < 0) {
		dev_err(&dev->udev->dev,
			"Unable to request privacy GPIO IRQ (%d)\n", ret);
		goto error;
	}

	if (dev->quirks & UVC_QUIRK_NO_RESET_RESUME)
		udev->quirks &= ~USB_QUIRK_RESET_RESUME;

	if (!(dev->quirks & UVC_QUIRK_DISABLE_AUTOSUSPEND))
		usb_enable_autosuspend(udev);

	uvc_dbg(dev, PROBE, "UVC device initialized\n");

	return 0;

error:
	uvc_unregister_video(dev);
	kref_put(&dev->ref, uvc_delete);
	return -ENODEV;
}

static void uvc_disconnect(struct usb_interface *intf)
{
	struct uvc_device *dev = usb_get_intfdata(intf);

	/*
	 * Set the USB interface data to NULL. This can be done outside the
	 * lock, as there's no other reader.
	 */
	usb_set_intfdata(intf, NULL);

	if (intf->cur_altsetting->desc.bInterfaceSubClass ==
	    UVC_SC_VIDEOSTREAMING)
		return;

	uvc_unregister_video(dev);
	kref_put(&dev->ref, uvc_delete);
}

static int uvc_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct uvc_device *dev = usb_get_intfdata(intf);
	struct uvc_streaming *stream;

	uvc_dbg(dev, SUSPEND, "Suspending interface %u\n",
		intf->cur_altsetting->desc.bInterfaceNumber);

	/* Controls are cached on the fly so they don't need to be saved. */
	if (intf->cur_altsetting->desc.bInterfaceSubClass ==
	    UVC_SC_VIDEOCONTROL) {
		uvc_status_suspend(dev);
		return 0;
	}

	list_for_each_entry(stream, &dev->streams, list) {
		if (stream->intf == intf)
			return uvc_video_suspend(stream);
	}

	uvc_dbg(dev, SUSPEND,
		"Suspend: video streaming USB interface mismatch\n");
	return -EINVAL;
}

static int __uvc_resume(struct usb_interface *intf, int reset)
{
	struct uvc_device *dev = usb_get_intfdata(intf);
	struct uvc_streaming *stream;
	int ret = 0;

	uvc_dbg(dev, SUSPEND, "Resuming interface %u\n",
		intf->cur_altsetting->desc.bInterfaceNumber);

	if (intf->cur_altsetting->desc.bInterfaceSubClass ==
	    UVC_SC_VIDEOCONTROL) {
		if (reset) {
			ret = uvc_ctrl_restore_values(dev);
			if (ret < 0)
				return ret;
		}

		return uvc_status_resume(dev);
	}

	list_for_each_entry(stream, &dev->streams, list) {
		if (stream->intf == intf) {
			ret = uvc_video_resume(stream, reset);
			if (ret < 0)
				uvc_queue_streamoff(&stream->queue,
						    stream->queue.queue.type);
			return ret;
		}
	}

	uvc_dbg(dev, SUSPEND,
		"Resume: video streaming USB interface mismatch\n");
	return -EINVAL;
}

static int uvc_resume(struct usb_interface *intf)
{
	return __uvc_resume(intf, 0);
}

static int uvc_reset_resume(struct usb_interface *intf)
{
	return __uvc_resume(intf, 1);
}

/* ------------------------------------------------------------------------
 * Module parameters
 */

static int uvc_clock_param_get(char *buffer, const struct kernel_param *kp)
{
	if (uvc_clock_param == CLOCK_MONOTONIC)
		return sprintf(buffer, "CLOCK_MONOTONIC");
	else
		return sprintf(buffer, "CLOCK_REALTIME");
}

static int uvc_clock_param_set(const char *val, const struct kernel_param *kp)
{
	if (strncasecmp(val, "clock_", strlen("clock_")) == 0)
		val += strlen("clock_");

	if (strcasecmp(val, "monotonic") == 0)
		uvc_clock_param = CLOCK_MONOTONIC;
	else if (strcasecmp(val, "realtime") == 0)
		uvc_clock_param = CLOCK_REALTIME;
	else
		return -EINVAL;

	return 0;
}

module_param_call(clock, uvc_clock_param_set, uvc_clock_param_get,
		  &uvc_clock_param, 0644);
MODULE_PARM_DESC(clock, "Video buffers timestamp clock");
module_param_named(hwtimestamps, uvc_hw_timestamps_param, uint, 0644);
MODULE_PARM_DESC(hwtimestamps, "Use hardware timestamps");

static int param_set_nodrop(const char *val, const struct kernel_param *kp)
{
	pr_warn_once("uvcvideo: "
		     DEPRECATED
		     "nodrop parameter will be eventually removed.\n");
	return param_set_bool(val, kp);
}

static const struct kernel_param_ops param_ops_nodrop = {
	.set = param_set_nodrop,
	.get = param_get_uint,
};

param_check_uint(nodrop, &uvc_no_drop_param);
module_param_cb(nodrop, &param_ops_nodrop, &uvc_no_drop_param, 0644);
__MODULE_PARM_TYPE(nodrop, "uint");
MODULE_PARM_DESC(nodrop, "Don't drop incomplete frames");

module_param_named(quirks, uvc_quirks_param, uint, 0644);
MODULE_PARM_DESC(quirks, "Forced device quirks");
module_param_named(trace, uvc_dbg_param, uint, 0644);
MODULE_PARM_DESC(trace, "Trace level bitmask");
module_param_named(timeout, uvc_timeout_param, uint, 0644);
MODULE_PARM_DESC(timeout, "Streaming control requests timeout");

/* ------------------------------------------------------------------------
 * Driver initialization and cleanup
 */

static const struct uvc_device_info uvc_quirk_probe_minmax = {
	.quirks = UVC_QUIRK_PROBE_MINMAX,
};

static const struct uvc_device_info uvc_quirk_fix_bandwidth = {
	.quirks = UVC_QUIRK_FIX_BANDWIDTH,
};

static const struct uvc_device_info uvc_quirk_probe_def = {
	.quirks = UVC_QUIRK_PROBE_DEF,
};

static const struct uvc_device_info uvc_quirk_stream_no_fid = {
	.quirks = UVC_QUIRK_STREAM_NO_FID,
};

static const struct uvc_device_info uvc_quirk_force_y8 = {
	.quirks = UVC_QUIRK_FORCE_Y8,
};

#define UVC_INFO_QUIRK(q) (kernel_ulong_t)&(struct uvc_device_info){.quirks = q}
#define UVC_INFO_META(m) (kernel_ulong_t)&(struct uvc_device_info) \
	{.meta_format = m}

/*
 * The Logitech cameras listed below have their interface class set to
 * VENDOR_SPEC because they don't announce themselves as UVC devices, even
 * though they are compliant.
 *
 * Sort these by vendor/product ID.
 */
static const struct usb_device_id uvc_ids[] = {
	/* Quanta ACER HD User Facing */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x0408,
	  .idProduct		= 0x4033,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= UVC_PC_PROTOCOL_15,
	  .driver_info		= (kernel_ulong_t)&(const struct uvc_device_info){
		.uvc_version = 0x010a,
	  } },
	/* Quanta ACER HD User Facing */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x0408,
	  .idProduct		= 0x4035,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= UVC_PC_PROTOCOL_15,
	  .driver_info		= (kernel_ulong_t)&(const struct uvc_device_info){
		.uvc_version = 0x010a,
	  } },
	/* LogiLink Wireless Webcam */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x0416,
	  .idProduct		= 0xa91a,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_minmax },
	/* Genius eFace 2025 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x0458,
	  .idProduct		= 0x706e,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_minmax },
	/* Microsoft Lifecam NX-6000 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x045e,
	  .idProduct		= 0x00f8,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_minmax },
	/* Microsoft Lifecam NX-3000 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x045e,
	  .idProduct		= 0x0721,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_def },
	/* Microsoft Lifecam VX-7000 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x045e,
	  .idProduct		= 0x0723,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_minmax },
	/* Logitech, Webcam C910 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x0821,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_WAKE_AUTOSUSPEND)},
	/* Logitech, Webcam B910 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x0823,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_WAKE_AUTOSUSPEND)},
	/* Logitech Quickcam Fusion */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x08c1,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Logitech Quickcam Orbit MP */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x08c2,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Logitech Quickcam Pro for Notebook */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x08c3,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Logitech Quickcam Pro 5000 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x08c5,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Logitech Quickcam OEM Dell Notebook */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x08c6,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Logitech Quickcam OEM Cisco VT Camera II */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x08c7,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Logitech HD Pro Webcam C920 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x082d,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_RESTORE_CTRLS_ON_INIT
					       | UVC_QUIRK_INVALID_DEVICE_SOF) },
	/* Logitech HD Pro Webcam C922 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x085c,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_INVALID_DEVICE_SOF) },
	/* Logitech Rally Bar Huddle */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x087c,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_NO_RESET_RESUME) },
	/* Logitech Rally Bar */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x089b,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_NO_RESET_RESUME) },
	/* Logitech Rally Bar Mini */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x046d,
	  .idProduct		= 0x08d3,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_NO_RESET_RESUME) },
	/* Chicony CNF7129 (Asus EEE 100HE) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x04f2,
	  .idProduct		= 0xb071,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_RESTRICT_FRAME_RATE) },
	/* Alcor Micro AU3820 (Future Boy PC USB Webcam) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x058f,
	  .idProduct		= 0x3820,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_minmax },
	/* Dell XPS m1530 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x05a9,
	  .idProduct		= 0x2640,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_def },
	/* Dell SP2008WFP Monitor */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x05a9,
	  .idProduct		= 0x2641,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_def },
	/* Dell Alienware X51 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x05a9,
	  .idProduct		= 0x2643,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_def },
	/* Dell Studio Hybrid 140g (OmniVision webcam) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x05a9,
	  .idProduct		= 0x264a,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_def },
	/* Dell XPS M1330 (OmniVision OV7670 webcam) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x05a9,
	  .idProduct		= 0x7670,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_def },
	/* Apple Built-In iSight */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x05ac,
	  .idProduct		= 0x8501,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_PROBE_MINMAX
					| UVC_QUIRK_BUILTIN_ISIGHT) },
	/* Apple FaceTime HD Camera (Built-In) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x05ac,
	  .idProduct		= 0x8514,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_def },
	/* Apple Built-In iSight via iBridge */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x05ac,
	  .idProduct		= 0x8600,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_def },
	/* Foxlink ("HP Webcam" on HP Mini 5103) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x05c8,
	  .idProduct		= 0x0403,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_fix_bandwidth },
	/* Genesys Logic USB 2.0 PC Camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x05e3,
	  .idProduct		= 0x0505,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_stream_no_fid },
	/* Hercules Classic Silver */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x06f8,
	  .idProduct		= 0x300c,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_fix_bandwidth },
	/* ViMicro Vega */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x0ac8,
	  .idProduct		= 0x332d,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_fix_bandwidth },
	/* ViMicro - Minoru3D */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x0ac8,
	  .idProduct		= 0x3410,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_fix_bandwidth },
	/* ViMicro Venus - Minoru3D */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x0ac8,
	  .idProduct		= 0x3420,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_fix_bandwidth },
	/* Ophir Optronics - SPCAM 620U */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x0bd3,
	  .idProduct		= 0x0555,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_minmax },
	/* Sonix Technology Co. Ltd. - 292A IPC AR0330 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x0c45,
	  .idProduct		= 0x6366,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_MJPEG_NO_EOF) },
	/* MT6227 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x0e8d,
	  .idProduct		= 0x0004,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_PROBE_MINMAX
					| UVC_QUIRK_PROBE_DEF) },
	/* IMC Networks (Medion Akoya) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x13d3,
	  .idProduct		= 0x5103,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_stream_no_fid },
	/* JMicron USB2.0 XGA WebCam */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x152d,
	  .idProduct		= 0x0310,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_minmax },
	/* Kurokesu C1 PRO */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x16d0,
	  .idProduct		= 0x0ed1,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_MJPEG_NO_EOF) },
	/* Syntek (HP Spartan) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x174f,
	  .idProduct		= 0x5212,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_stream_no_fid },
	/* Syntek (Samsung Q310) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x174f,
	  .idProduct		= 0x5931,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_stream_no_fid },
	/* Syntek (Packard Bell EasyNote MX52 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x174f,
	  .idProduct		= 0x8a12,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_stream_no_fid },
	/* Syntek (Asus F9SG) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x174f,
	  .idProduct		= 0x8a31,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_stream_no_fid },
	/* Syntek (Asus U3S) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x174f,
	  .idProduct		= 0x8a33,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_stream_no_fid },
	/* Syntek (JAOtech Smart Terminal) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x174f,
	  .idProduct		= 0x8a34,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_stream_no_fid },
	/* Miricle 307K */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x17dc,
	  .idProduct		= 0x0202,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_stream_no_fid },
	/* Lenovo Thinkpad SL400/SL500 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x17ef,
	  .idProduct		= 0x480b,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_stream_no_fid },
	/* Aveo Technology USB 2.0 Camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x1871,
	  .idProduct		= 0x0306,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_PROBE_MINMAX
					| UVC_QUIRK_PROBE_EXTRAFIELDS) },
	/* Aveo Technology USB 2.0 Camera (Tasco USB Microscope) */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x1871,
	  .idProduct		= 0x0516,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Ecamm Pico iMage */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x18cd,
	  .idProduct		= 0xcafe,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_PROBE_EXTRAFIELDS) },
	/* Manta MM-353 Plako */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x18ec,
	  .idProduct		= 0x3188,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_minmax },
	/* FSC WebCam V30S */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x18ec,
	  .idProduct		= 0x3288,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_minmax },
	/* Arkmicro unbranded */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x18ec,
	  .idProduct		= 0x3290,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_def },
	/* The Imaging Source USB CCD cameras */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x199e,
	  .idProduct		= 0x8102,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0 },
	/* Bodelin ProScopeHR */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_DEV_HI
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x19ab,
	  .idProduct		= 0x1000,
	  .bcdDevice_hi		= 0x0126,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_STATUS_INTERVAL) },
	/* MSI StarCam 370i */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x1b3b,
	  .idProduct		= 0x2951,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_minmax },
	/* Generalplus Technology Inc. 808 Camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x1b3f,
	  .idProduct		= 0x2002,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_minmax },
	/* Shenzhen Aoni Electronic Co.,Ltd 2K FHD camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x1bcf,
	  .idProduct		= 0x0b40,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&(const struct uvc_device_info){
		.uvc_version = 0x010a,
	  } },
	/* SiGma Micro USB Web Camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x1c4f,
	  .idProduct		= 0x3000,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_PROBE_MINMAX
					| UVC_QUIRK_IGNORE_SELECTOR_UNIT) },
	/* Actions Microelectronics Co. Display capture-UVC05 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x1de1,
	  .idProduct		= 0xf105,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_DISABLE_AUTOSUSPEND) },
	/* NXP Semiconductors IR VIDEO */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x1fc9,
	  .idProduct		= 0x009b,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_probe_minmax },
	/* Oculus VR Positional Tracker DK2 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x2833,
	  .idProduct		= 0x0201,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_force_y8 },
	/* Oculus VR Rift Sensor */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x2833,
	  .idProduct		= 0x0211,
	  .bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= (kernel_ulong_t)&uvc_quirk_force_y8 },
	/* GEO Semiconductor GC6500 */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x29fe,
	  .idProduct		= 0x4d53,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_FORCE_BPP) },
	/* Insta360 Link */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x2e1a,
	  .idProduct		= 0x4c01,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_QUIRK(UVC_QUIRK_DISABLE_AUTOSUSPEND) },
	/* Intel D410/ASR depth camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x8086,
	  .idProduct		= 0x0ad2,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_META(V4L2_META_FMT_D4XX) },
	/* Intel D415/ASRC depth camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x8086,
	  .idProduct		= 0x0ad3,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_META(V4L2_META_FMT_D4XX) },
	/* Intel D430/AWG depth camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x8086,
	  .idProduct		= 0x0ad4,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_META(V4L2_META_FMT_D4XX) },
	/* Intel RealSense D4M */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x8086,
	  .idProduct		= 0x0b03,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_META(V4L2_META_FMT_D4XX) },
	/* Intel D435/AWGC depth camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x8086,
	  .idProduct		= 0x0b07,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_META(V4L2_META_FMT_D4XX) },
	/* Intel D435i depth camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x8086,
	  .idProduct		= 0x0b3a,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_META(V4L2_META_FMT_D4XX) },
	/* Intel D405 Depth Camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x8086,
	  .idProduct		= 0x0b5b,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_META(V4L2_META_FMT_D4XX) },
	/* Intel D455 Depth Camera */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x8086,
	  .idProduct		= 0x0b5c,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_META(V4L2_META_FMT_D4XX) },
	/* Intel D421 Depth Module */
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE
				| USB_DEVICE_ID_MATCH_INT_INFO,
	  .idVendor		= 0x8086,
	  .idProduct		= 0x1155,
	  .bInterfaceClass	= USB_CLASS_VIDEO,
	  .bInterfaceSubClass	= 1,
	  .bInterfaceProtocol	= 0,
	  .driver_info		= UVC_INFO_META(V4L2_META_FMT_D4XX) },
	/* Generic USB Video Class */
	{ USB_INTERFACE_INFO(USB_CLASS_VIDEO, 1, UVC_PC_PROTOCOL_UNDEFINED) },
	{ USB_INTERFACE_INFO(USB_CLASS_VIDEO, 1, UVC_PC_PROTOCOL_15) },
	{}
};

MODULE_DEVICE_TABLE(usb, uvc_ids);

static struct usb_driver uvc_driver = {
	.name		= "uvcvideo",
	.probe		= uvc_probe,
	.disconnect	= uvc_disconnect,
	.suspend	= uvc_suspend,
	.resume		= uvc_resume,
	.reset_resume	= uvc_reset_resume,
	.id_table	= uvc_ids,
	.supports_autosuspend = 1,
};

static int __init uvc_init(void)
{
	int ret;

	uvc_debugfs_init();

	ret = usb_register(&uvc_driver);
	if (ret < 0) {
		uvc_debugfs_cleanup();
		return ret;
	}

	return 0;
}

static void __exit uvc_cleanup(void)
{
	usb_deregister(&uvc_driver);
	uvc_debugfs_cleanup();
}

module_init(uvc_init);
module_exit(uvc_cleanup);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

