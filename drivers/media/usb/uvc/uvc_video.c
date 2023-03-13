// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      uvc_video.c  --  USB Video Class driver - Video handling
 *
 *      Copyright (C) 2005-2010
 *          Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <asm/unaligned.h>

#include <media/v4l2-common.h>

#include "uvcvideo.h"

/* ------------------------------------------------------------------------
 * UVC Controls
 */

static int __uvc_query_ctrl(struct uvc_device *dev, u8 query, u8 unit,
			u8 intfnum, u8 cs, void *data, u16 size,
			int timeout)
{
	u8 type = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	unsigned int pipe;

	pipe = (query & 0x80) ? usb_rcvctrlpipe(dev->udev, 0)
			      : usb_sndctrlpipe(dev->udev, 0);
	type |= (query & 0x80) ? USB_DIR_IN : USB_DIR_OUT;

	return usb_control_msg(dev->udev, pipe, query, type, cs << 8,
			unit << 8 | intfnum, data, size, timeout);
}

static const char *uvc_query_name(u8 query)
{
	switch (query) {
	case UVC_SET_CUR:
		return "SET_CUR";
	case UVC_GET_CUR:
		return "GET_CUR";
	case UVC_GET_MIN:
		return "GET_MIN";
	case UVC_GET_MAX:
		return "GET_MAX";
	case UVC_GET_RES:
		return "GET_RES";
	case UVC_GET_LEN:
		return "GET_LEN";
	case UVC_GET_INFO:
		return "GET_INFO";
	case UVC_GET_DEF:
		return "GET_DEF";
	default:
		return "<invalid>";
	}
}

int uvc_query_ctrl(struct uvc_device *dev, u8 query, u8 unit,
			u8 intfnum, u8 cs, void *data, u16 size)
{
	int ret;
	u8 error;
	u8 tmp;

	ret = __uvc_query_ctrl(dev, query, unit, intfnum, cs, data, size,
				UVC_CTRL_CONTROL_TIMEOUT);
	if (likely(ret == size))
		return 0;

	if (ret != -EPIPE) {
		dev_err(&dev->udev->dev,
			"Failed to query (%s) UVC control %u on unit %u: %d (exp. %u).\n",
			uvc_query_name(query), cs, unit, ret, size);
		return ret < 0 ? ret : -EPIPE;
	}

	/* Reuse data[0] to request the error code. */
	tmp = *(u8 *)data;

	ret = __uvc_query_ctrl(dev, UVC_GET_CUR, 0, intfnum,
			       UVC_VC_REQUEST_ERROR_CODE_CONTROL, data, 1,
			       UVC_CTRL_CONTROL_TIMEOUT);

	error = *(u8 *)data;
	*(u8 *)data = tmp;

	if (ret != 1)
		return ret < 0 ? ret : -EPIPE;

	uvc_dbg(dev, CONTROL, "Control error %u\n", error);

	switch (error) {
	case 0:
		/* Cannot happen - we received a STALL */
		return -EPIPE;
	case 1: /* Not ready */
		return -EBUSY;
	case 2: /* Wrong state */
		return -EACCES;
	case 3: /* Power */
		return -EREMOTE;
	case 4: /* Out of range */
		return -ERANGE;
	case 5: /* Invalid unit */
	case 6: /* Invalid control */
	case 7: /* Invalid Request */
		/*
		 * The firmware has not properly implemented
		 * the control or there has been a HW error.
		 */
		return -EIO;
	case 8: /* Invalid value within range */
		return -EINVAL;
	default: /* reserved or unknown */
		break;
	}

	return -EPIPE;
}

static const struct usb_device_id elgato_cam_link_4k = {
	USB_DEVICE(0x0fd9, 0x0066)
};

static void uvc_fixup_video_ctrl(struct uvc_streaming *stream,
	struct uvc_streaming_control *ctrl)
{
	struct uvc_format *format = NULL;
	struct uvc_frame *frame = NULL;
	unsigned int i;

	/*
	 * The response of the Elgato Cam Link 4K is incorrect: The second byte
	 * contains bFormatIndex (instead of being the second byte of bmHint).
	 * The first byte is always zero. The third byte is always 1.
	 *
	 * The UVC 1.5 class specification defines the first five bits in the
	 * bmHint bitfield. The remaining bits are reserved and should be zero.
	 * Therefore a valid bmHint will be less than 32.
	 *
	 * Latest Elgato Cam Link 4K firmware as of 2021-03-23 needs this fix.
	 * MCU: 20.02.19, FPGA: 67
	 */
	if (usb_match_one_id(stream->dev->intf, &elgato_cam_link_4k) &&
	    ctrl->bmHint > 255) {
		u8 corrected_format_index = ctrl->bmHint >> 8;

		uvc_dbg(stream->dev, VIDEO,
			"Correct USB video probe response from {bmHint: 0x%04x, bFormatIndex: %u} to {bmHint: 0x%04x, bFormatIndex: %u}\n",
			ctrl->bmHint, ctrl->bFormatIndex,
			1, corrected_format_index);
		ctrl->bmHint = 1;
		ctrl->bFormatIndex = corrected_format_index;
	}

	for (i = 0; i < stream->nformats; ++i) {
		if (stream->format[i].index == ctrl->bFormatIndex) {
			format = &stream->format[i];
			break;
		}
	}

	if (format == NULL)
		return;

	for (i = 0; i < format->nframes; ++i) {
		if (format->frame[i].bFrameIndex == ctrl->bFrameIndex) {
			frame = &format->frame[i];
			break;
		}
	}

	if (frame == NULL)
		return;

	if (!(format->flags & UVC_FMT_FLAG_COMPRESSED) ||
	     (ctrl->dwMaxVideoFrameSize == 0 &&
	      stream->dev->uvc_version < 0x0110))
		ctrl->dwMaxVideoFrameSize =
			frame->dwMaxVideoFrameBufferSize;

	/*
	 * The "TOSHIBA Web Camera - 5M" Chicony device (04f2:b50b) seems to
	 * compute the bandwidth on 16 bits and erroneously sign-extend it to
	 * 32 bits, resulting in a huge bandwidth value. Detect and fix that
	 * condition by setting the 16 MSBs to 0 when they're all equal to 1.
	 */
	if ((ctrl->dwMaxPayloadTransferSize & 0xffff0000) == 0xffff0000)
		ctrl->dwMaxPayloadTransferSize &= ~0xffff0000;

	if (!(format->flags & UVC_FMT_FLAG_COMPRESSED) &&
	    stream->dev->quirks & UVC_QUIRK_FIX_BANDWIDTH &&
	    stream->intf->num_altsetting > 1) {
		u32 interval;
		u32 bandwidth;

		interval = (ctrl->dwFrameInterval > 100000)
			 ? ctrl->dwFrameInterval
			 : frame->dwFrameInterval[0];

		/*
		 * Compute a bandwidth estimation by multiplying the frame
		 * size by the number of video frames per second, divide the
		 * result by the number of USB frames (or micro-frames for
		 * high-speed devices) per second and add the UVC header size
		 * (assumed to be 12 bytes long).
		 */
		bandwidth = frame->wWidth * frame->wHeight / 8 * format->bpp;
		bandwidth *= 10000000 / interval + 1;
		bandwidth /= 1000;
		if (stream->dev->udev->speed == USB_SPEED_HIGH)
			bandwidth /= 8;
		bandwidth += 12;

		/*
		 * The bandwidth estimate is too low for many cameras. Don't use
		 * maximum packet sizes lower than 1024 bytes to try and work
		 * around the problem. According to measurements done on two
		 * different camera models, the value is high enough to get most
		 * resolutions working while not preventing two simultaneous
		 * VGA streams at 15 fps.
		 */
		bandwidth = max_t(u32, bandwidth, 1024);

		ctrl->dwMaxPayloadTransferSize = bandwidth;
	}
}

static size_t uvc_video_ctrl_size(struct uvc_streaming *stream)
{
	/*
	 * Return the size of the video probe and commit controls, which depends
	 * on the protocol version.
	 */
	if (stream->dev->uvc_version < 0x0110)
		return 26;
	else if (stream->dev->uvc_version < 0x0150)
		return 34;
	else
		return 48;
}

static int uvc_get_video_ctrl(struct uvc_streaming *stream,
	struct uvc_streaming_control *ctrl, int probe, u8 query)
{
	u16 size = uvc_video_ctrl_size(stream);
	u8 *data;
	int ret;

	if ((stream->dev->quirks & UVC_QUIRK_PROBE_DEF) &&
			query == UVC_GET_DEF)
		return -EIO;

	data = kmalloc(size, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	ret = __uvc_query_ctrl(stream->dev, query, 0, stream->intfnum,
		probe ? UVC_VS_PROBE_CONTROL : UVC_VS_COMMIT_CONTROL, data,
		size, uvc_timeout_param);

	if ((query == UVC_GET_MIN || query == UVC_GET_MAX) && ret == 2) {
		/*
		 * Some cameras, mostly based on Bison Electronics chipsets,
		 * answer a GET_MIN or GET_MAX request with the wCompQuality
		 * field only.
		 */
		uvc_warn_once(stream->dev, UVC_WARN_MINMAX, "UVC non "
			"compliance - GET_MIN/MAX(PROBE) incorrectly "
			"supported. Enabling workaround.\n");
		memset(ctrl, 0, sizeof(*ctrl));
		ctrl->wCompQuality = le16_to_cpup((__le16 *)data);
		ret = 0;
		goto out;
	} else if (query == UVC_GET_DEF && probe == 1 && ret != size) {
		/*
		 * Many cameras don't support the GET_DEF request on their
		 * video probe control. Warn once and return, the caller will
		 * fall back to GET_CUR.
		 */
		uvc_warn_once(stream->dev, UVC_WARN_PROBE_DEF, "UVC non "
			"compliance - GET_DEF(PROBE) not supported. "
			"Enabling workaround.\n");
		ret = -EIO;
		goto out;
	} else if (ret != size) {
		dev_err(&stream->intf->dev,
			"Failed to query (%u) UVC %s control : %d (exp. %u).\n",
			query, probe ? "probe" : "commit", ret, size);
		ret = (ret == -EPROTO) ? -EPROTO : -EIO;
		goto out;
	}

	ctrl->bmHint = le16_to_cpup((__le16 *)&data[0]);
	ctrl->bFormatIndex = data[2];
	ctrl->bFrameIndex = data[3];
	ctrl->dwFrameInterval = le32_to_cpup((__le32 *)&data[4]);
	ctrl->wKeyFrameRate = le16_to_cpup((__le16 *)&data[8]);
	ctrl->wPFrameRate = le16_to_cpup((__le16 *)&data[10]);
	ctrl->wCompQuality = le16_to_cpup((__le16 *)&data[12]);
	ctrl->wCompWindowSize = le16_to_cpup((__le16 *)&data[14]);
	ctrl->wDelay = le16_to_cpup((__le16 *)&data[16]);
	ctrl->dwMaxVideoFrameSize = get_unaligned_le32(&data[18]);
	ctrl->dwMaxPayloadTransferSize = get_unaligned_le32(&data[22]);

	if (size >= 34) {
		ctrl->dwClockFrequency = get_unaligned_le32(&data[26]);
		ctrl->bmFramingInfo = data[30];
		ctrl->bPreferedVersion = data[31];
		ctrl->bMinVersion = data[32];
		ctrl->bMaxVersion = data[33];
	} else {
		ctrl->dwClockFrequency = stream->dev->clock_frequency;
		ctrl->bmFramingInfo = 0;
		ctrl->bPreferedVersion = 0;
		ctrl->bMinVersion = 0;
		ctrl->bMaxVersion = 0;
	}

	/*
	 * Some broken devices return null or wrong dwMaxVideoFrameSize and
	 * dwMaxPayloadTransferSize fields. Try to get the value from the
	 * format and frame descriptors.
	 */
	uvc_fixup_video_ctrl(stream, ctrl);
	ret = 0;

out:
	kfree(data);
	return ret;
}

static int uvc_set_video_ctrl(struct uvc_streaming *stream,
	struct uvc_streaming_control *ctrl, int probe)
{
	u16 size = uvc_video_ctrl_size(stream);
	u8 *data;
	int ret;

	data = kzalloc(size, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	*(__le16 *)&data[0] = cpu_to_le16(ctrl->bmHint);
	data[2] = ctrl->bFormatIndex;
	data[3] = ctrl->bFrameIndex;
	*(__le32 *)&data[4] = cpu_to_le32(ctrl->dwFrameInterval);
	*(__le16 *)&data[8] = cpu_to_le16(ctrl->wKeyFrameRate);
	*(__le16 *)&data[10] = cpu_to_le16(ctrl->wPFrameRate);
	*(__le16 *)&data[12] = cpu_to_le16(ctrl->wCompQuality);
	*(__le16 *)&data[14] = cpu_to_le16(ctrl->wCompWindowSize);
	*(__le16 *)&data[16] = cpu_to_le16(ctrl->wDelay);
	put_unaligned_le32(ctrl->dwMaxVideoFrameSize, &data[18]);
	put_unaligned_le32(ctrl->dwMaxPayloadTransferSize, &data[22]);

	if (size >= 34) {
		put_unaligned_le32(ctrl->dwClockFrequency, &data[26]);
		data[30] = ctrl->bmFramingInfo;
		data[31] = ctrl->bPreferedVersion;
		data[32] = ctrl->bMinVersion;
		data[33] = ctrl->bMaxVersion;
	}

	ret = __uvc_query_ctrl(stream->dev, UVC_SET_CUR, 0, stream->intfnum,
		probe ? UVC_VS_PROBE_CONTROL : UVC_VS_COMMIT_CONTROL, data,
		size, uvc_timeout_param);
	if (ret != size) {
		dev_err(&stream->intf->dev,
			"Failed to set UVC %s control : %d (exp. %u).\n",
			probe ? "probe" : "commit", ret, size);
		ret = -EIO;
	}

	kfree(data);
	return ret;
}

int uvc_probe_video(struct uvc_streaming *stream,
	struct uvc_streaming_control *probe)
{
	struct uvc_streaming_control probe_min, probe_max;
	unsigned int i;
	int ret;

	/*
	 * Perform probing. The device should adjust the requested values
	 * according to its capabilities. However, some devices, namely the
	 * first generation UVC Logitech webcams, don't implement the Video
	 * Probe control properly, and just return the needed bandwidth. For
	 * that reason, if the needed bandwidth exceeds the maximum available
	 * bandwidth, try to lower the quality.
	 */
	ret = uvc_set_video_ctrl(stream, probe, 1);
	if (ret < 0)
		goto done;

	/* Get the minimum and maximum values for compression settings. */
	if (!(stream->dev->quirks & UVC_QUIRK_PROBE_MINMAX)) {
		ret = uvc_get_video_ctrl(stream, &probe_min, 1, UVC_GET_MIN);
		if (ret < 0)
			goto done;
		ret = uvc_get_video_ctrl(stream, &probe_max, 1, UVC_GET_MAX);
		if (ret < 0)
			goto done;

		probe->wCompQuality = probe_max.wCompQuality;
	}

	for (i = 0; i < 2; ++i) {
		ret = uvc_set_video_ctrl(stream, probe, 1);
		if (ret < 0)
			goto done;
		ret = uvc_get_video_ctrl(stream, probe, 1, UVC_GET_CUR);
		if (ret < 0)
			goto done;

		if (stream->intf->num_altsetting == 1)
			break;

		if (probe->dwMaxPayloadTransferSize <= stream->maxpsize)
			break;

		if (stream->dev->quirks & UVC_QUIRK_PROBE_MINMAX) {
			ret = -ENOSPC;
			goto done;
		}

		/* TODO: negotiate compression parameters */
		probe->wKeyFrameRate = probe_min.wKeyFrameRate;
		probe->wPFrameRate = probe_min.wPFrameRate;
		probe->wCompQuality = probe_max.wCompQuality;
		probe->wCompWindowSize = probe_min.wCompWindowSize;
	}

done:
	return ret;
}

static int uvc_commit_video(struct uvc_streaming *stream,
			    struct uvc_streaming_control *probe)
{
	return uvc_set_video_ctrl(stream, probe, 0);
}

/* -----------------------------------------------------------------------------
 * Clocks and timestamps
 */

static inline ktime_t uvc_video_get_time(void)
{
	if (uvc_clock_param == CLOCK_MONOTONIC)
		return ktime_get();
	else
		return ktime_get_real();
}

static void
uvc_video_clock_decode(struct uvc_streaming *stream, struct uvc_buffer *buf,
		       const u8 *data, int len)
{
	struct uvc_clock_sample *sample;
	unsigned int header_size;
	bool has_pts = false;
	bool has_scr = false;
	unsigned long flags;
	ktime_t time;
	u16 host_sof;
	u16 dev_sof;

	switch (data[1] & (UVC_STREAM_PTS | UVC_STREAM_SCR)) {
	case UVC_STREAM_PTS | UVC_STREAM_SCR:
		header_size = 12;
		has_pts = true;
		has_scr = true;
		break;
	case UVC_STREAM_PTS:
		header_size = 6;
		has_pts = true;
		break;
	case UVC_STREAM_SCR:
		header_size = 8;
		has_scr = true;
		break;
	default:
		header_size = 2;
		break;
	}

	/* Check for invalid headers. */
	if (len < header_size)
		return;

	/*
	 * Extract the timestamps:
	 *
	 * - store the frame PTS in the buffer structure
	 * - if the SCR field is present, retrieve the host SOF counter and
	 *   kernel timestamps and store them with the SCR STC and SOF fields
	 *   in the ring buffer
	 */
	if (has_pts && buf != NULL)
		buf->pts = get_unaligned_le32(&data[2]);

	if (!has_scr)
		return;

	/*
	 * To limit the amount of data, drop SCRs with an SOF identical to the
	 * previous one. This filtering is also needed to support UVC 1.5, where
	 * all the data packets of the same frame contains the same SOF. In that
	 * case only the first one will match the host_sof.
	 */
	dev_sof = get_unaligned_le16(&data[header_size - 2]);
	if (dev_sof == stream->clock.last_sof)
		return;

	stream->clock.last_sof = dev_sof;

	host_sof = usb_get_current_frame_number(stream->dev->udev);
	time = uvc_video_get_time();

	/*
	 * The UVC specification allows device implementations that can't obtain
	 * the USB frame number to keep their own frame counters as long as they
	 * match the size and frequency of the frame number associated with USB
	 * SOF tokens. The SOF values sent by such devices differ from the USB
	 * SOF tokens by a fixed offset that needs to be estimated and accounted
	 * for to make timestamp recovery as accurate as possible.
	 *
	 * The offset is estimated the first time a device SOF value is received
	 * as the difference between the host and device SOF values. As the two
	 * SOF values can differ slightly due to transmission delays, consider
	 * that the offset is null if the difference is not higher than 10 ms
	 * (negative differences can not happen and are thus considered as an
	 * offset). The video commit control wDelay field should be used to
	 * compute a dynamic threshold instead of using a fixed 10 ms value, but
	 * devices don't report reliable wDelay values.
	 *
	 * See uvc_video_clock_host_sof() for an explanation regarding why only
	 * the 8 LSBs of the delta are kept.
	 */
	if (stream->clock.sof_offset == (u16)-1) {
		u16 delta_sof = (host_sof - dev_sof) & 255;
		if (delta_sof >= 10)
			stream->clock.sof_offset = delta_sof;
		else
			stream->clock.sof_offset = 0;
	}

	dev_sof = (dev_sof + stream->clock.sof_offset) & 2047;

	spin_lock_irqsave(&stream->clock.lock, flags);

	sample = &stream->clock.samples[stream->clock.head];
	sample->dev_stc = get_unaligned_le32(&data[header_size - 6]);
	sample->dev_sof = dev_sof;
	sample->host_sof = host_sof;
	sample->host_time = time;

	/* Update the sliding window head and count. */
	stream->clock.head = (stream->clock.head + 1) % stream->clock.size;

	if (stream->clock.count < stream->clock.size)
		stream->clock.count++;

	spin_unlock_irqrestore(&stream->clock.lock, flags);
}

static void uvc_video_clock_reset(struct uvc_streaming *stream)
{
	struct uvc_clock *clock = &stream->clock;

	clock->head = 0;
	clock->count = 0;
	clock->last_sof = -1;
	clock->sof_offset = -1;
}

static int uvc_video_clock_init(struct uvc_streaming *stream)
{
	struct uvc_clock *clock = &stream->clock;

	spin_lock_init(&clock->lock);
	clock->size = 32;

	clock->samples = kmalloc_array(clock->size, sizeof(*clock->samples),
				       GFP_KERNEL);
	if (clock->samples == NULL)
		return -ENOMEM;

	uvc_video_clock_reset(stream);

	return 0;
}

static void uvc_video_clock_cleanup(struct uvc_streaming *stream)
{
	kfree(stream->clock.samples);
	stream->clock.samples = NULL;
}

/*
 * uvc_video_clock_host_sof - Return the host SOF value for a clock sample
 *
 * Host SOF counters reported by usb_get_current_frame_number() usually don't
 * cover the whole 11-bits SOF range (0-2047) but are limited to the HCI frame
 * schedule window. They can be limited to 8, 9 or 10 bits depending on the host
 * controller and its configuration.
 *
 * We thus need to recover the SOF value corresponding to the host frame number.
 * As the device and host frame numbers are sampled in a short interval, the
 * difference between their values should be equal to a small delta plus an
 * integer multiple of 256 caused by the host frame number limited precision.
 *
 * To obtain the recovered host SOF value, compute the small delta by masking
 * the high bits of the host frame counter and device SOF difference and add it
 * to the device SOF value.
 */
static u16 uvc_video_clock_host_sof(const struct uvc_clock_sample *sample)
{
	/* The delta value can be negative. */
	s8 delta_sof;

	delta_sof = (sample->host_sof - sample->dev_sof) & 255;

	return (sample->dev_sof + delta_sof) & 2047;
}

/*
 * uvc_video_clock_update - Update the buffer timestamp
 *
 * This function converts the buffer PTS timestamp to the host clock domain by
 * going through the USB SOF clock domain and stores the result in the V4L2
 * buffer timestamp field.
 *
 * The relationship between the device clock and the host clock isn't known.
 * However, the device and the host share the common USB SOF clock which can be
 * used to recover that relationship.
 *
 * The relationship between the device clock and the USB SOF clock is considered
 * to be linear over the clock samples sliding window and is given by
 *
 * SOF = m * PTS + p
 *
 * Several methods to compute the slope (m) and intercept (p) can be used. As
 * the clock drift should be small compared to the sliding window size, we
 * assume that the line that goes through the points at both ends of the window
 * is a good approximation. Naming those points P1 and P2, we get
 *
 * SOF = (SOF2 - SOF1) / (STC2 - STC1) * PTS
 *     + (SOF1 * STC2 - SOF2 * STC1) / (STC2 - STC1)
 *
 * or
 *
 * SOF = ((SOF2 - SOF1) * PTS + SOF1 * STC2 - SOF2 * STC1) / (STC2 - STC1)   (1)
 *
 * to avoid losing precision in the division. Similarly, the host timestamp is
 * computed with
 *
 * TS = ((TS2 - TS1) * SOF + TS1 * SOF2 - TS2 * SOF1) / (SOF2 - SOF1)	     (2)
 *
 * SOF values are coded on 11 bits by USB. We extend their precision with 16
 * decimal bits, leading to a 11.16 coding.
 *
 * TODO: To avoid surprises with device clock values, PTS/STC timestamps should
 * be normalized using the nominal device clock frequency reported through the
 * UVC descriptors.
 *
 * Both the PTS/STC and SOF counters roll over, after a fixed but device
 * specific amount of time for PTS/STC and after 2048ms for SOF. As long as the
 * sliding window size is smaller than the rollover period, differences computed
 * on unsigned integers will produce the correct result. However, the p term in
 * the linear relations will be miscomputed.
 *
 * To fix the issue, we subtract a constant from the PTS and STC values to bring
 * PTS to half the 32 bit STC range. The sliding window STC values then fit into
 * the 32 bit range without any rollover.
 *
 * Similarly, we add 2048 to the device SOF values to make sure that the SOF
 * computed by (1) will never be smaller than 0. This offset is then compensated
 * by adding 2048 to the SOF values used in (2). However, this doesn't prevent
 * rollovers between (1) and (2): the SOF value computed by (1) can be slightly
 * lower than 4096, and the host SOF counters can have rolled over to 2048. This
 * case is handled by subtracting 2048 from the SOF value if it exceeds the host
 * SOF value at the end of the sliding window.
 *
 * Finally we subtract a constant from the host timestamps to bring the first
 * timestamp of the sliding window to 1s.
 */
void uvc_video_clock_update(struct uvc_streaming *stream,
			    struct vb2_v4l2_buffer *vbuf,
			    struct uvc_buffer *buf)
{
	struct uvc_clock *clock = &stream->clock;
	struct uvc_clock_sample *first;
	struct uvc_clock_sample *last;
	unsigned long flags;
	u64 timestamp;
	u32 delta_stc;
	u32 y1, y2;
	u32 x1, x2;
	u32 mean;
	u32 sof;
	u64 y;

	if (!uvc_hw_timestamps_param)
		return;

	/*
	 * We will get called from __vb2_queue_cancel() if there are buffers
	 * done but not dequeued by the user, but the sample array has already
	 * been released at that time. Just bail out in that case.
	 */
	if (!clock->samples)
		return;

	spin_lock_irqsave(&clock->lock, flags);

	if (clock->count < clock->size)
		goto done;

	first = &clock->samples[clock->head];
	last = &clock->samples[(clock->head - 1) % clock->size];

	/* First step, PTS to SOF conversion. */
	delta_stc = buf->pts - (1UL << 31);
	x1 = first->dev_stc - delta_stc;
	x2 = last->dev_stc - delta_stc;
	if (x1 == x2)
		goto done;

	y1 = (first->dev_sof + 2048) << 16;
	y2 = (last->dev_sof + 2048) << 16;
	if (y2 < y1)
		y2 += 2048 << 16;

	y = (u64)(y2 - y1) * (1ULL << 31) + (u64)y1 * (u64)x2
	  - (u64)y2 * (u64)x1;
	y = div_u64(y, x2 - x1);

	sof = y;

	uvc_dbg(stream->dev, CLOCK,
		"%s: PTS %u y %llu.%06llu SOF %u.%06llu (x1 %u x2 %u y1 %u y2 %u SOF offset %u)\n",
		stream->dev->name, buf->pts,
		y >> 16, div_u64((y & 0xffff) * 1000000, 65536),
		sof >> 16, div_u64(((u64)sof & 0xffff) * 1000000LLU, 65536),
		x1, x2, y1, y2, clock->sof_offset);

	/* Second step, SOF to host clock conversion. */
	x1 = (uvc_video_clock_host_sof(first) + 2048) << 16;
	x2 = (uvc_video_clock_host_sof(last) + 2048) << 16;
	if (x2 < x1)
		x2 += 2048 << 16;
	if (x1 == x2)
		goto done;

	y1 = NSEC_PER_SEC;
	y2 = (u32)ktime_to_ns(ktime_sub(last->host_time, first->host_time)) + y1;

	/*
	 * Interpolated and host SOF timestamps can wrap around at slightly
	 * different times. Handle this by adding or removing 2048 to or from
	 * the computed SOF value to keep it close to the SOF samples mean
	 * value.
	 */
	mean = (x1 + x2) / 2;
	if (mean - (1024 << 16) > sof)
		sof += 2048 << 16;
	else if (sof > mean + (1024 << 16))
		sof -= 2048 << 16;

	y = (u64)(y2 - y1) * (u64)sof + (u64)y1 * (u64)x2
	  - (u64)y2 * (u64)x1;
	y = div_u64(y, x2 - x1);

	timestamp = ktime_to_ns(first->host_time) + y - y1;

	uvc_dbg(stream->dev, CLOCK,
		"%s: SOF %u.%06llu y %llu ts %llu buf ts %llu (x1 %u/%u/%u x2 %u/%u/%u y1 %u y2 %u)\n",
		stream->dev->name,
		sof >> 16, div_u64(((u64)sof & 0xffff) * 1000000LLU, 65536),
		y, timestamp, vbuf->vb2_buf.timestamp,
		x1, first->host_sof, first->dev_sof,
		x2, last->host_sof, last->dev_sof, y1, y2);

	/* Update the V4L2 buffer. */
	vbuf->vb2_buf.timestamp = timestamp;

done:
	spin_unlock_irqrestore(&clock->lock, flags);
}

/* ------------------------------------------------------------------------
 * Stream statistics
 */

static void uvc_video_stats_decode(struct uvc_streaming *stream,
		const u8 *data, int len)
{
	unsigned int header_size;
	bool has_pts = false;
	bool has_scr = false;
	u16 scr_sof;
	u32 scr_stc;
	u32 pts;

	if (stream->stats.stream.nb_frames == 0 &&
	    stream->stats.frame.nb_packets == 0)
		stream->stats.stream.start_ts = ktime_get();

	switch (data[1] & (UVC_STREAM_PTS | UVC_STREAM_SCR)) {
	case UVC_STREAM_PTS | UVC_STREAM_SCR:
		header_size = 12;
		has_pts = true;
		has_scr = true;
		break;
	case UVC_STREAM_PTS:
		header_size = 6;
		has_pts = true;
		break;
	case UVC_STREAM_SCR:
		header_size = 8;
		has_scr = true;
		break;
	default:
		header_size = 2;
		break;
	}

	/* Check for invalid headers. */
	if (len < header_size || data[0] < header_size) {
		stream->stats.frame.nb_invalid++;
		return;
	}

	/* Extract the timestamps. */
	if (has_pts)
		pts = get_unaligned_le32(&data[2]);

	if (has_scr) {
		scr_stc = get_unaligned_le32(&data[header_size - 6]);
		scr_sof = get_unaligned_le16(&data[header_size - 2]);
	}

	/* Is PTS constant through the whole frame ? */
	if (has_pts && stream->stats.frame.nb_pts) {
		if (stream->stats.frame.pts != pts) {
			stream->stats.frame.nb_pts_diffs++;
			stream->stats.frame.last_pts_diff =
				stream->stats.frame.nb_packets;
		}
	}

	if (has_pts) {
		stream->stats.frame.nb_pts++;
		stream->stats.frame.pts = pts;
	}

	/*
	 * Do all frames have a PTS in their first non-empty packet, or before
	 * their first empty packet ?
	 */
	if (stream->stats.frame.size == 0) {
		if (len > header_size)
			stream->stats.frame.has_initial_pts = has_pts;
		if (len == header_size && has_pts)
			stream->stats.frame.has_early_pts = true;
	}

	/* Do the SCR.STC and SCR.SOF fields vary through the frame ? */
	if (has_scr && stream->stats.frame.nb_scr) {
		if (stream->stats.frame.scr_stc != scr_stc)
			stream->stats.frame.nb_scr_diffs++;
	}

	if (has_scr) {
		/* Expand the SOF counter to 32 bits and store its value. */
		if (stream->stats.stream.nb_frames > 0 ||
		    stream->stats.frame.nb_scr > 0)
			stream->stats.stream.scr_sof_count +=
				(scr_sof - stream->stats.stream.scr_sof) % 2048;
		stream->stats.stream.scr_sof = scr_sof;

		stream->stats.frame.nb_scr++;
		stream->stats.frame.scr_stc = scr_stc;
		stream->stats.frame.scr_sof = scr_sof;

		if (scr_sof < stream->stats.stream.min_sof)
			stream->stats.stream.min_sof = scr_sof;
		if (scr_sof > stream->stats.stream.max_sof)
			stream->stats.stream.max_sof = scr_sof;
	}

	/* Record the first non-empty packet number. */
	if (stream->stats.frame.size == 0 && len > header_size)
		stream->stats.frame.first_data = stream->stats.frame.nb_packets;

	/* Update the frame size. */
	stream->stats.frame.size += len - header_size;

	/* Update the packets counters. */
	stream->stats.frame.nb_packets++;
	if (len <= header_size)
		stream->stats.frame.nb_empty++;

	if (data[1] & UVC_STREAM_ERR)
		stream->stats.frame.nb_errors++;
}

static void uvc_video_stats_update(struct uvc_streaming *stream)
{
	struct uvc_stats_frame *frame = &stream->stats.frame;

	uvc_dbg(stream->dev, STATS,
		"frame %u stats: %u/%u/%u packets, %u/%u/%u pts (%searly %sinitial), %u/%u scr, last pts/stc/sof %u/%u/%u\n",
		stream->sequence, frame->first_data,
		frame->nb_packets - frame->nb_empty, frame->nb_packets,
		frame->nb_pts_diffs, frame->last_pts_diff, frame->nb_pts,
		frame->has_early_pts ? "" : "!",
		frame->has_initial_pts ? "" : "!",
		frame->nb_scr_diffs, frame->nb_scr,
		frame->pts, frame->scr_stc, frame->scr_sof);

	stream->stats.stream.nb_frames++;
	stream->stats.stream.nb_packets += stream->stats.frame.nb_packets;
	stream->stats.stream.nb_empty += stream->stats.frame.nb_empty;
	stream->stats.stream.nb_errors += stream->stats.frame.nb_errors;
	stream->stats.stream.nb_invalid += stream->stats.frame.nb_invalid;

	if (frame->has_early_pts)
		stream->stats.stream.nb_pts_early++;
	if (frame->has_initial_pts)
		stream->stats.stream.nb_pts_initial++;
	if (frame->last_pts_diff <= frame->first_data)
		stream->stats.stream.nb_pts_constant++;
	if (frame->nb_scr >= frame->nb_packets - frame->nb_empty)
		stream->stats.stream.nb_scr_count_ok++;
	if (frame->nb_scr_diffs + 1 == frame->nb_scr)
		stream->stats.stream.nb_scr_diffs_ok++;

	memset(&stream->stats.frame, 0, sizeof(stream->stats.frame));
}

size_t uvc_video_stats_dump(struct uvc_streaming *stream, char *buf,
			    size_t size)
{
	unsigned int scr_sof_freq;
	unsigned int duration;
	size_t count = 0;

	/*
	 * Compute the SCR.SOF frequency estimate. At the nominal 1kHz SOF
	 * frequency this will not overflow before more than 1h.
	 */
	duration = ktime_ms_delta(stream->stats.stream.stop_ts,
				  stream->stats.stream.start_ts);
	if (duration != 0)
		scr_sof_freq = stream->stats.stream.scr_sof_count * 1000
			     / duration;
	else
		scr_sof_freq = 0;

	count += scnprintf(buf + count, size - count,
			   "frames:  %u\npackets: %u\nempty:   %u\n"
			   "errors:  %u\ninvalid: %u\n",
			   stream->stats.stream.nb_frames,
			   stream->stats.stream.nb_packets,
			   stream->stats.stream.nb_empty,
			   stream->stats.stream.nb_errors,
			   stream->stats.stream.nb_invalid);
	count += scnprintf(buf + count, size - count,
			   "pts: %u early, %u initial, %u ok\n",
			   stream->stats.stream.nb_pts_early,
			   stream->stats.stream.nb_pts_initial,
			   stream->stats.stream.nb_pts_constant);
	count += scnprintf(buf + count, size - count,
			   "scr: %u count ok, %u diff ok\n",
			   stream->stats.stream.nb_scr_count_ok,
			   stream->stats.stream.nb_scr_diffs_ok);
	count += scnprintf(buf + count, size - count,
			   "sof: %u <= sof <= %u, freq %u.%03u kHz\n",
			   stream->stats.stream.min_sof,
			   stream->stats.stream.max_sof,
			   scr_sof_freq / 1000, scr_sof_freq % 1000);

	return count;
}

static void uvc_video_stats_start(struct uvc_streaming *stream)
{
	memset(&stream->stats, 0, sizeof(stream->stats));
	stream->stats.stream.min_sof = 2048;
}

static void uvc_video_stats_stop(struct uvc_streaming *stream)
{
	stream->stats.stream.stop_ts = ktime_get();
}

/* ------------------------------------------------------------------------
 * Video codecs
 */

/*
 * Video payload decoding is handled by uvc_video_decode_start(),
 * uvc_video_decode_data() and uvc_video_decode_end().
 *
 * uvc_video_decode_start is called with URB data at the start of a bulk or
 * isochronous payload. It processes header data and returns the header size
 * in bytes if successful. If an error occurs, it returns a negative error
 * code. The following error codes have special meanings.
 *
 * - EAGAIN informs the caller that the current video buffer should be marked
 *   as done, and that the function should be called again with the same data
 *   and a new video buffer. This is used when end of frame conditions can be
 *   reliably detected at the beginning of the next frame only.
 *
 * If an error other than -EAGAIN is returned, the caller will drop the current
 * payload. No call to uvc_video_decode_data and uvc_video_decode_end will be
 * made until the next payload. -ENODATA can be used to drop the current
 * payload if no other error code is appropriate.
 *
 * uvc_video_decode_data is called for every URB with URB data. It copies the
 * data to the video buffer.
 *
 * uvc_video_decode_end is called with header data at the end of a bulk or
 * isochronous payload. It performs any additional header data processing and
 * returns 0 or a negative error code if an error occurred. As header data have
 * already been processed by uvc_video_decode_start, this functions isn't
 * required to perform sanity checks a second time.
 *
 * For isochronous transfers where a payload is always transferred in a single
 * URB, the three functions will be called in a row.
 *
 * To let the decoder process header data and update its internal state even
 * when no video buffer is available, uvc_video_decode_start must be prepared
 * to be called with a NULL buf parameter. uvc_video_decode_data and
 * uvc_video_decode_end will never be called with a NULL buffer.
 */
static int uvc_video_decode_start(struct uvc_streaming *stream,
		struct uvc_buffer *buf, const u8 *data, int len)
{
	u8 fid;

	/*
	 * Sanity checks:
	 * - packet must be at least 2 bytes long
	 * - bHeaderLength value must be at least 2 bytes (see above)
	 * - bHeaderLength value can't be larger than the packet size.
	 */
	if (len < 2 || data[0] < 2 || data[0] > len) {
		stream->stats.frame.nb_invalid++;
		return -EINVAL;
	}

	fid = data[1] & UVC_STREAM_FID;

	/*
	 * Increase the sequence number regardless of any buffer states, so
	 * that discontinuous sequence numbers always indicate lost frames.
	 */
	if (stream->last_fid != fid) {
		stream->sequence++;
		if (stream->sequence)
			uvc_video_stats_update(stream);
	}

	uvc_video_clock_decode(stream, buf, data, len);
	uvc_video_stats_decode(stream, data, len);

	/*
	 * Store the payload FID bit and return immediately when the buffer is
	 * NULL.
	 */
	if (buf == NULL) {
		stream->last_fid = fid;
		return -ENODATA;
	}

	/* Mark the buffer as bad if the error bit is set. */
	if (data[1] & UVC_STREAM_ERR) {
		uvc_dbg(stream->dev, FRAME,
			"Marking buffer as bad (error bit set)\n");
		buf->error = 1;
	}

	/*
	 * Synchronize to the input stream by waiting for the FID bit to be
	 * toggled when the buffer state is not UVC_BUF_STATE_ACTIVE.
	 * stream->last_fid is initialized to -1, so the first isochronous
	 * frame will always be in sync.
	 *
	 * If the device doesn't toggle the FID bit, invert stream->last_fid
	 * when the EOF bit is set to force synchronisation on the next packet.
	 */
	if (buf->state != UVC_BUF_STATE_ACTIVE) {
		if (fid == stream->last_fid) {
			uvc_dbg(stream->dev, FRAME,
				"Dropping payload (out of sync)\n");
			if ((stream->dev->quirks & UVC_QUIRK_STREAM_NO_FID) &&
			    (data[1] & UVC_STREAM_EOF))
				stream->last_fid ^= UVC_STREAM_FID;
			return -ENODATA;
		}

		buf->buf.field = V4L2_FIELD_NONE;
		buf->buf.sequence = stream->sequence;
		buf->buf.vb2_buf.timestamp = ktime_to_ns(uvc_video_get_time());

		/* TODO: Handle PTS and SCR. */
		buf->state = UVC_BUF_STATE_ACTIVE;
	}

	/*
	 * Mark the buffer as done if we're at the beginning of a new frame.
	 * End of frame detection is better implemented by checking the EOF
	 * bit (FID bit toggling is delayed by one frame compared to the EOF
	 * bit), but some devices don't set the bit at end of frame (and the
	 * last payload can be lost anyway). We thus must check if the FID has
	 * been toggled.
	 *
	 * stream->last_fid is initialized to -1, so the first isochronous
	 * frame will never trigger an end of frame detection.
	 *
	 * Empty buffers (bytesused == 0) don't trigger end of frame detection
	 * as it doesn't make sense to return an empty buffer. This also
	 * avoids detecting end of frame conditions at FID toggling if the
	 * previous payload had the EOF bit set.
	 */
	if (fid != stream->last_fid && buf->bytesused != 0) {
		uvc_dbg(stream->dev, FRAME,
			"Frame complete (FID bit toggled)\n");
		buf->state = UVC_BUF_STATE_READY;
		return -EAGAIN;
	}

	stream->last_fid = fid;

	return data[0];
}

static inline enum dma_data_direction uvc_stream_dir(
				struct uvc_streaming *stream)
{
	if (stream->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return DMA_FROM_DEVICE;
	else
		return DMA_TO_DEVICE;
}

static inline struct device *uvc_stream_to_dmadev(struct uvc_streaming *stream)
{
	return bus_to_hcd(stream->dev->udev->bus)->self.sysdev;
}

static int uvc_submit_urb(struct uvc_urb *uvc_urb, gfp_t mem_flags)
{
	/* Sync DMA. */
	dma_sync_sgtable_for_device(uvc_stream_to_dmadev(uvc_urb->stream),
				    uvc_urb->sgt,
				    uvc_stream_dir(uvc_urb->stream));
	return usb_submit_urb(uvc_urb->urb, mem_flags);
}

/*
 * uvc_video_decode_data_work: Asynchronous memcpy processing
 *
 * Copy URB data to video buffers in process context, releasing buffer
 * references and requeuing the URB when done.
 */
static void uvc_video_copy_data_work(struct work_struct *work)
{
	struct uvc_urb *uvc_urb = container_of(work, struct uvc_urb, work);
	unsigned int i;
	int ret;

	for (i = 0; i < uvc_urb->async_operations; i++) {
		struct uvc_copy_op *op = &uvc_urb->copy_operations[i];

		memcpy(op->dst, op->src, op->len);

		/* Release reference taken on this buffer. */
		uvc_queue_buffer_release(op->buf);
	}

	ret = uvc_submit_urb(uvc_urb, GFP_KERNEL);
	if (ret < 0)
		dev_err(&uvc_urb->stream->intf->dev,
			"Failed to resubmit video URB (%d).\n", ret);
}

static void uvc_video_decode_data(struct uvc_urb *uvc_urb,
		struct uvc_buffer *buf, const u8 *data, int len)
{
	unsigned int active_op = uvc_urb->async_operations;
	struct uvc_copy_op *op = &uvc_urb->copy_operations[active_op];
	unsigned int maxlen;

	if (len <= 0)
		return;

	maxlen = buf->length - buf->bytesused;

	/* Take a buffer reference for async work. */
	kref_get(&buf->ref);

	op->buf = buf;
	op->src = data;
	op->dst = buf->mem + buf->bytesused;
	op->len = min_t(unsigned int, len, maxlen);

	buf->bytesused += op->len;

	/* Complete the current frame if the buffer size was exceeded. */
	if (len > maxlen) {
		uvc_dbg(uvc_urb->stream->dev, FRAME,
			"Frame complete (overflow)\n");
		buf->error = 1;
		buf->state = UVC_BUF_STATE_READY;
	}

	uvc_urb->async_operations++;
}

static void uvc_video_decode_end(struct uvc_streaming *stream,
		struct uvc_buffer *buf, const u8 *data, int len)
{
	/* Mark the buffer as done if the EOF marker is set. */
	if (data[1] & UVC_STREAM_EOF && buf->bytesused != 0) {
		uvc_dbg(stream->dev, FRAME, "Frame complete (EOF found)\n");
		if (data[0] == len)
			uvc_dbg(stream->dev, FRAME, "EOF in empty payload\n");
		buf->state = UVC_BUF_STATE_READY;
		if (stream->dev->quirks & UVC_QUIRK_STREAM_NO_FID)
			stream->last_fid ^= UVC_STREAM_FID;
	}
}

/*
 * Video payload encoding is handled by uvc_video_encode_header() and
 * uvc_video_encode_data(). Only bulk transfers are currently supported.
 *
 * uvc_video_encode_header is called at the start of a payload. It adds header
 * data to the transfer buffer and returns the header size. As the only known
 * UVC output device transfers a whole frame in a single payload, the EOF bit
 * is always set in the header.
 *
 * uvc_video_encode_data is called for every URB and copies the data from the
 * video buffer to the transfer buffer.
 */
static int uvc_video_encode_header(struct uvc_streaming *stream,
		struct uvc_buffer *buf, u8 *data, int len)
{
	data[0] = 2;	/* Header length */
	data[1] = UVC_STREAM_EOH | UVC_STREAM_EOF
		| (stream->last_fid & UVC_STREAM_FID);
	return 2;
}

static int uvc_video_encode_data(struct uvc_streaming *stream,
		struct uvc_buffer *buf, u8 *data, int len)
{
	struct uvc_video_queue *queue = &stream->queue;
	unsigned int nbytes;
	void *mem;

	/* Copy video data to the URB buffer. */
	mem = buf->mem + queue->buf_used;
	nbytes = min((unsigned int)len, buf->bytesused - queue->buf_used);
	nbytes = min(stream->bulk.max_payload_size - stream->bulk.payload_size,
			nbytes);
	memcpy(data, mem, nbytes);

	queue->buf_used += nbytes;

	return nbytes;
}

/* ------------------------------------------------------------------------
 * Metadata
 */

/*
 * Additionally to the payload headers we also want to provide the user with USB
 * Frame Numbers and system time values. The resulting buffer is thus composed
 * of blocks, containing a 64-bit timestamp in  nanoseconds, a 16-bit USB Frame
 * Number, and a copy of the payload header.
 *
 * Ideally we want to capture all payload headers for each frame. However, their
 * number is unknown and unbound. We thus drop headers that contain no vendor
 * data and that either contain no SCR value or an SCR value identical to the
 * previous header.
 */
static void uvc_video_decode_meta(struct uvc_streaming *stream,
				  struct uvc_buffer *meta_buf,
				  const u8 *mem, unsigned int length)
{
	struct uvc_meta_buf *meta;
	size_t len_std = 2;
	bool has_pts, has_scr;
	unsigned long flags;
	unsigned int sof;
	ktime_t time;
	const u8 *scr;

	if (!meta_buf || length == 2)
		return;

	if (meta_buf->length - meta_buf->bytesused <
	    length + sizeof(meta->ns) + sizeof(meta->sof)) {
		meta_buf->error = 1;
		return;
	}

	has_pts = mem[1] & UVC_STREAM_PTS;
	has_scr = mem[1] & UVC_STREAM_SCR;

	if (has_pts) {
		len_std += 4;
		scr = mem + 6;
	} else {
		scr = mem + 2;
	}

	if (has_scr)
		len_std += 6;

	if (stream->meta.format == V4L2_META_FMT_UVC)
		length = len_std;

	if (length == len_std && (!has_scr ||
				  !memcmp(scr, stream->clock.last_scr, 6)))
		return;

	meta = (struct uvc_meta_buf *)((u8 *)meta_buf->mem + meta_buf->bytesused);
	local_irq_save(flags);
	time = uvc_video_get_time();
	sof = usb_get_current_frame_number(stream->dev->udev);
	local_irq_restore(flags);
	put_unaligned(ktime_to_ns(time), &meta->ns);
	put_unaligned(sof, &meta->sof);

	if (has_scr)
		memcpy(stream->clock.last_scr, scr, 6);

	meta->length = mem[0];
	meta->flags  = mem[1];
	memcpy(meta->buf, &mem[2], length - 2);
	meta_buf->bytesused += length + sizeof(meta->ns) + sizeof(meta->sof);

	uvc_dbg(stream->dev, FRAME,
		"%s(): t-sys %lluns, SOF %u, len %u, flags 0x%x, PTS %u, STC %u frame SOF %u\n",
		__func__, ktime_to_ns(time), meta->sof, meta->length,
		meta->flags,
		has_pts ? *(u32 *)meta->buf : 0,
		has_scr ? *(u32 *)scr : 0,
		has_scr ? *(u32 *)(scr + 4) & 0x7ff : 0);
}

/* ------------------------------------------------------------------------
 * URB handling
 */

/*
 * Set error flag for incomplete buffer.
 */
static void uvc_video_validate_buffer(const struct uvc_streaming *stream,
				      struct uvc_buffer *buf)
{
	if (stream->ctrl.dwMaxVideoFrameSize != buf->bytesused &&
	    !(stream->cur_format->flags & UVC_FMT_FLAG_COMPRESSED))
		buf->error = 1;
}

/*
 * Completion handler for video URBs.
 */

static void uvc_video_next_buffers(struct uvc_streaming *stream,
		struct uvc_buffer **video_buf, struct uvc_buffer **meta_buf)
{
	uvc_video_validate_buffer(stream, *video_buf);

	if (*meta_buf) {
		struct vb2_v4l2_buffer *vb2_meta = &(*meta_buf)->buf;
		const struct vb2_v4l2_buffer *vb2_video = &(*video_buf)->buf;

		vb2_meta->sequence = vb2_video->sequence;
		vb2_meta->field = vb2_video->field;
		vb2_meta->vb2_buf.timestamp = vb2_video->vb2_buf.timestamp;

		(*meta_buf)->state = UVC_BUF_STATE_READY;
		if (!(*meta_buf)->error)
			(*meta_buf)->error = (*video_buf)->error;
		*meta_buf = uvc_queue_next_buffer(&stream->meta.queue,
						  *meta_buf);
	}
	*video_buf = uvc_queue_next_buffer(&stream->queue, *video_buf);
}

static void uvc_video_decode_isoc(struct uvc_urb *uvc_urb,
			struct uvc_buffer *buf, struct uvc_buffer *meta_buf)
{
	struct urb *urb = uvc_urb->urb;
	struct uvc_streaming *stream = uvc_urb->stream;
	u8 *mem;
	int ret, i;

	for (i = 0; i < urb->number_of_packets; ++i) {
		if (urb->iso_frame_desc[i].status < 0) {
			uvc_dbg(stream->dev, FRAME,
				"USB isochronous frame lost (%d)\n",
				urb->iso_frame_desc[i].status);
			/* Mark the buffer as faulty. */
			if (buf != NULL)
				buf->error = 1;
			continue;
		}

		/* Decode the payload header. */
		mem = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		do {
			ret = uvc_video_decode_start(stream, buf, mem,
				urb->iso_frame_desc[i].actual_length);
			if (ret == -EAGAIN)
				uvc_video_next_buffers(stream, &buf, &meta_buf);
		} while (ret == -EAGAIN);

		if (ret < 0)
			continue;

		uvc_video_decode_meta(stream, meta_buf, mem, ret);

		/* Decode the payload data. */
		uvc_video_decode_data(uvc_urb, buf, mem + ret,
			urb->iso_frame_desc[i].actual_length - ret);

		/* Process the header again. */
		uvc_video_decode_end(stream, buf, mem,
			urb->iso_frame_desc[i].actual_length);

		if (buf->state == UVC_BUF_STATE_READY)
			uvc_video_next_buffers(stream, &buf, &meta_buf);
	}
}

static void uvc_video_decode_bulk(struct uvc_urb *uvc_urb,
			struct uvc_buffer *buf, struct uvc_buffer *meta_buf)
{
	struct urb *urb = uvc_urb->urb;
	struct uvc_streaming *stream = uvc_urb->stream;
	u8 *mem;
	int len, ret;

	/*
	 * Ignore ZLPs if they're not part of a frame, otherwise process them
	 * to trigger the end of payload detection.
	 */
	if (urb->actual_length == 0 && stream->bulk.header_size == 0)
		return;

	mem = urb->transfer_buffer;
	len = urb->actual_length;
	stream->bulk.payload_size += len;

	/*
	 * If the URB is the first of its payload, decode and save the
	 * header.
	 */
	if (stream->bulk.header_size == 0 && !stream->bulk.skip_payload) {
		do {
			ret = uvc_video_decode_start(stream, buf, mem, len);
			if (ret == -EAGAIN)
				uvc_video_next_buffers(stream, &buf, &meta_buf);
		} while (ret == -EAGAIN);

		/* If an error occurred skip the rest of the payload. */
		if (ret < 0 || buf == NULL) {
			stream->bulk.skip_payload = 1;
		} else {
			memcpy(stream->bulk.header, mem, ret);
			stream->bulk.header_size = ret;

			uvc_video_decode_meta(stream, meta_buf, mem, ret);

			mem += ret;
			len -= ret;
		}
	}

	/*
	 * The buffer queue might have been cancelled while a bulk transfer
	 * was in progress, so we can reach here with buf equal to NULL. Make
	 * sure buf is never dereferenced if NULL.
	 */

	/* Prepare video data for processing. */
	if (!stream->bulk.skip_payload && buf != NULL)
		uvc_video_decode_data(uvc_urb, buf, mem, len);

	/*
	 * Detect the payload end by a URB smaller than the maximum size (or
	 * a payload size equal to the maximum) and process the header again.
	 */
	if (urb->actual_length < urb->transfer_buffer_length ||
	    stream->bulk.payload_size >= stream->bulk.max_payload_size) {
		if (!stream->bulk.skip_payload && buf != NULL) {
			uvc_video_decode_end(stream, buf, stream->bulk.header,
				stream->bulk.payload_size);
			if (buf->state == UVC_BUF_STATE_READY)
				uvc_video_next_buffers(stream, &buf, &meta_buf);
		}

		stream->bulk.header_size = 0;
		stream->bulk.skip_payload = 0;
		stream->bulk.payload_size = 0;
	}
}

static void uvc_video_encode_bulk(struct uvc_urb *uvc_urb,
	struct uvc_buffer *buf, struct uvc_buffer *meta_buf)
{
	struct urb *urb = uvc_urb->urb;
	struct uvc_streaming *stream = uvc_urb->stream;

	u8 *mem = urb->transfer_buffer;
	int len = stream->urb_size, ret;

	if (buf == NULL) {
		urb->transfer_buffer_length = 0;
		return;
	}

	/* If the URB is the first of its payload, add the header. */
	if (stream->bulk.header_size == 0) {
		ret = uvc_video_encode_header(stream, buf, mem, len);
		stream->bulk.header_size = ret;
		stream->bulk.payload_size += ret;
		mem += ret;
		len -= ret;
	}

	/* Process video data. */
	ret = uvc_video_encode_data(stream, buf, mem, len);

	stream->bulk.payload_size += ret;
	len -= ret;

	if (buf->bytesused == stream->queue.buf_used ||
	    stream->bulk.payload_size == stream->bulk.max_payload_size) {
		if (buf->bytesused == stream->queue.buf_used) {
			stream->queue.buf_used = 0;
			buf->state = UVC_BUF_STATE_READY;
			buf->buf.sequence = ++stream->sequence;
			uvc_queue_next_buffer(&stream->queue, buf);
			stream->last_fid ^= UVC_STREAM_FID;
		}

		stream->bulk.header_size = 0;
		stream->bulk.payload_size = 0;
	}

	urb->transfer_buffer_length = stream->urb_size - len;
}

static void uvc_video_complete(struct urb *urb)
{
	struct uvc_urb *uvc_urb = urb->context;
	struct uvc_streaming *stream = uvc_urb->stream;
	struct uvc_video_queue *queue = &stream->queue;
	struct uvc_video_queue *qmeta = &stream->meta.queue;
	struct vb2_queue *vb2_qmeta = stream->meta.vdev.queue;
	struct uvc_buffer *buf = NULL;
	struct uvc_buffer *buf_meta = NULL;
	unsigned long flags;
	int ret;

	switch (urb->status) {
	case 0:
		break;

	default:
		dev_warn(&stream->intf->dev,
			 "Non-zero status (%d) in video completion handler.\n",
			 urb->status);
		fallthrough;
	case -ENOENT:		/* usb_poison_urb() called. */
		if (stream->frozen)
			return;
		fallthrough;
	case -ECONNRESET:	/* usb_unlink_urb() called. */
	case -ESHUTDOWN:	/* The endpoint is being disabled. */
		uvc_queue_cancel(queue, urb->status == -ESHUTDOWN);
		if (vb2_qmeta)
			uvc_queue_cancel(qmeta, urb->status == -ESHUTDOWN);
		return;
	}

	buf = uvc_queue_get_current_buffer(queue);

	if (vb2_qmeta) {
		spin_lock_irqsave(&qmeta->irqlock, flags);
		if (!list_empty(&qmeta->irqqueue))
			buf_meta = list_first_entry(&qmeta->irqqueue,
						    struct uvc_buffer, queue);
		spin_unlock_irqrestore(&qmeta->irqlock, flags);
	}

	/* Re-initialise the URB async work. */
	uvc_urb->async_operations = 0;

	/* Sync DMA and invalidate vmap range. */
	dma_sync_sgtable_for_cpu(uvc_stream_to_dmadev(uvc_urb->stream),
				 uvc_urb->sgt, uvc_stream_dir(stream));
	invalidate_kernel_vmap_range(uvc_urb->buffer,
				     uvc_urb->stream->urb_size);

	/*
	 * Process the URB headers, and optionally queue expensive memcpy tasks
	 * to be deferred to a work queue.
	 */
	stream->decode(uvc_urb, buf, buf_meta);

	/* If no async work is needed, resubmit the URB immediately. */
	if (!uvc_urb->async_operations) {
		ret = uvc_submit_urb(uvc_urb, GFP_ATOMIC);
		if (ret < 0)
			dev_err(&stream->intf->dev,
				"Failed to resubmit video URB (%d).\n", ret);
		return;
	}

	queue_work(stream->async_wq, &uvc_urb->work);
}

/*
 * Free transfer buffers.
 */
static void uvc_free_urb_buffers(struct uvc_streaming *stream)
{
	struct device *dma_dev = uvc_stream_to_dmadev(stream);
	struct uvc_urb *uvc_urb;

	for_each_uvc_urb(uvc_urb, stream) {
		if (!uvc_urb->buffer)
			continue;

		dma_vunmap_noncontiguous(dma_dev, uvc_urb->buffer);
		dma_free_noncontiguous(dma_dev, stream->urb_size, uvc_urb->sgt,
				       uvc_stream_dir(stream));

		uvc_urb->buffer = NULL;
		uvc_urb->sgt = NULL;
	}

	stream->urb_size = 0;
}

static bool uvc_alloc_urb_buffer(struct uvc_streaming *stream,
				 struct uvc_urb *uvc_urb, gfp_t gfp_flags)
{
	struct device *dma_dev = uvc_stream_to_dmadev(stream);

	uvc_urb->sgt = dma_alloc_noncontiguous(dma_dev, stream->urb_size,
					       uvc_stream_dir(stream),
					       gfp_flags, 0);
	if (!uvc_urb->sgt)
		return false;
	uvc_urb->dma = uvc_urb->sgt->sgl->dma_address;

	uvc_urb->buffer = dma_vmap_noncontiguous(dma_dev, stream->urb_size,
						 uvc_urb->sgt);
	if (!uvc_urb->buffer) {
		dma_free_noncontiguous(dma_dev, stream->urb_size,
				       uvc_urb->sgt,
				       uvc_stream_dir(stream));
		uvc_urb->sgt = NULL;
		return false;
	}

	return true;
}

/*
 * Allocate transfer buffers. This function can be called with buffers
 * already allocated when resuming from suspend, in which case it will
 * return without touching the buffers.
 *
 * Limit the buffer size to UVC_MAX_PACKETS bulk/isochronous packets. If the
 * system is too low on memory try successively smaller numbers of packets
 * until allocation succeeds.
 *
 * Return the number of allocated packets on success or 0 when out of memory.
 */
static int uvc_alloc_urb_buffers(struct uvc_streaming *stream,
	unsigned int size, unsigned int psize, gfp_t gfp_flags)
{
	unsigned int npackets;
	unsigned int i;

	/* Buffers are already allocated, bail out. */
	if (stream->urb_size)
		return stream->urb_size / psize;

	/*
	 * Compute the number of packets. Bulk endpoints might transfer UVC
	 * payloads across multiple URBs.
	 */
	npackets = DIV_ROUND_UP(size, psize);
	if (npackets > UVC_MAX_PACKETS)
		npackets = UVC_MAX_PACKETS;

	/* Retry allocations until one succeed. */
	for (; npackets > 1; npackets /= 2) {
		stream->urb_size = psize * npackets;

		for (i = 0; i < UVC_URBS; ++i) {
			struct uvc_urb *uvc_urb = &stream->uvc_urb[i];

			if (!uvc_alloc_urb_buffer(stream, uvc_urb, gfp_flags)) {
				uvc_free_urb_buffers(stream);
				break;
			}

			uvc_urb->stream = stream;
		}

		if (i == UVC_URBS) {
			uvc_dbg(stream->dev, VIDEO,
				"Allocated %u URB buffers of %ux%u bytes each\n",
				UVC_URBS, npackets, psize);
			return npackets;
		}
	}

	uvc_dbg(stream->dev, VIDEO,
		"Failed to allocate URB buffers (%u bytes per packet)\n",
		psize);
	return 0;
}

/*
 * Uninitialize isochronous/bulk URBs and free transfer buffers.
 */
static void uvc_video_stop_transfer(struct uvc_streaming *stream,
				    int free_buffers)
{
	struct uvc_urb *uvc_urb;

	uvc_video_stats_stop(stream);

	/*
	 * We must poison the URBs rather than kill them to ensure that even
	 * after the completion handler returns, any asynchronous workqueues
	 * will be prevented from resubmitting the URBs.
	 */
	for_each_uvc_urb(uvc_urb, stream)
		usb_poison_urb(uvc_urb->urb);

	flush_workqueue(stream->async_wq);

	for_each_uvc_urb(uvc_urb, stream) {
		usb_free_urb(uvc_urb->urb);
		uvc_urb->urb = NULL;
	}

	if (free_buffers)
		uvc_free_urb_buffers(stream);
}

/*
 * Compute the maximum number of bytes per interval for an endpoint.
 */
u16 uvc_endpoint_max_bpi(struct usb_device *dev, struct usb_host_endpoint *ep)
{
	u16 psize;

	switch (dev->speed) {
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
		return le16_to_cpu(ep->ss_ep_comp.wBytesPerInterval);
	default:
		psize = usb_endpoint_maxp(&ep->desc);
		psize *= usb_endpoint_maxp_mult(&ep->desc);
		return psize;
	}
}

/*
 * Initialize isochronous URBs and allocate transfer buffers. The packet size
 * is given by the endpoint.
 */
static int uvc_init_video_isoc(struct uvc_streaming *stream,
	struct usb_host_endpoint *ep, gfp_t gfp_flags)
{
	struct urb *urb;
	struct uvc_urb *uvc_urb;
	unsigned int npackets, i;
	u16 psize;
	u32 size;

	psize = uvc_endpoint_max_bpi(stream->dev->udev, ep);
	size = stream->ctrl.dwMaxVideoFrameSize;

	npackets = uvc_alloc_urb_buffers(stream, size, psize, gfp_flags);
	if (npackets == 0)
		return -ENOMEM;

	size = npackets * psize;

	for_each_uvc_urb(uvc_urb, stream) {
		urb = usb_alloc_urb(npackets, gfp_flags);
		if (urb == NULL) {
			uvc_video_stop_transfer(stream, 1);
			return -ENOMEM;
		}

		urb->dev = stream->dev->udev;
		urb->context = uvc_urb;
		urb->pipe = usb_rcvisocpipe(stream->dev->udev,
				ep->desc.bEndpointAddress);
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->transfer_dma = uvc_urb->dma;
		urb->interval = ep->desc.bInterval;
		urb->transfer_buffer = uvc_urb->buffer;
		urb->complete = uvc_video_complete;
		urb->number_of_packets = npackets;
		urb->transfer_buffer_length = size;

		for (i = 0; i < npackets; ++i) {
			urb->iso_frame_desc[i].offset = i * psize;
			urb->iso_frame_desc[i].length = psize;
		}

		uvc_urb->urb = urb;
	}

	return 0;
}

/*
 * Initialize bulk URBs and allocate transfer buffers. The packet size is
 * given by the endpoint.
 */
static int uvc_init_video_bulk(struct uvc_streaming *stream,
	struct usb_host_endpoint *ep, gfp_t gfp_flags)
{
	struct urb *urb;
	struct uvc_urb *uvc_urb;
	unsigned int npackets, pipe;
	u16 psize;
	u32 size;

	psize = usb_endpoint_maxp(&ep->desc);
	size = stream->ctrl.dwMaxPayloadTransferSize;
	stream->bulk.max_payload_size = size;

	npackets = uvc_alloc_urb_buffers(stream, size, psize, gfp_flags);
	if (npackets == 0)
		return -ENOMEM;

	size = npackets * psize;

	if (usb_endpoint_dir_in(&ep->desc))
		pipe = usb_rcvbulkpipe(stream->dev->udev,
				       ep->desc.bEndpointAddress);
	else
		pipe = usb_sndbulkpipe(stream->dev->udev,
				       ep->desc.bEndpointAddress);

	if (stream->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		size = 0;

	for_each_uvc_urb(uvc_urb, stream) {
		urb = usb_alloc_urb(0, gfp_flags);
		if (urb == NULL) {
			uvc_video_stop_transfer(stream, 1);
			return -ENOMEM;
		}

		usb_fill_bulk_urb(urb, stream->dev->udev, pipe,	uvc_urb->buffer,
				  size, uvc_video_complete, uvc_urb);
		urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		urb->transfer_dma = uvc_urb->dma;

		uvc_urb->urb = urb;
	}

	return 0;
}

/*
 * Initialize isochronous/bulk URBs and allocate transfer buffers.
 */
static int uvc_video_start_transfer(struct uvc_streaming *stream,
				    gfp_t gfp_flags)
{
	struct usb_interface *intf = stream->intf;
	struct usb_host_endpoint *ep;
	struct uvc_urb *uvc_urb;
	unsigned int i;
	int ret;

	stream->sequence = -1;
	stream->last_fid = -1;
	stream->bulk.header_size = 0;
	stream->bulk.skip_payload = 0;
	stream->bulk.payload_size = 0;

	uvc_video_stats_start(stream);

	if (intf->num_altsetting > 1) {
		struct usb_host_endpoint *best_ep = NULL;
		unsigned int best_psize = UINT_MAX;
		unsigned int bandwidth;
		unsigned int altsetting;
		int intfnum = stream->intfnum;

		/* Isochronous endpoint, select the alternate setting. */
		bandwidth = stream->ctrl.dwMaxPayloadTransferSize;

		if (bandwidth == 0) {
			uvc_dbg(stream->dev, VIDEO,
				"Device requested null bandwidth, defaulting to lowest\n");
			bandwidth = 1;
		} else {
			uvc_dbg(stream->dev, VIDEO,
				"Device requested %u B/frame bandwidth\n",
				bandwidth);
		}

		for (i = 0; i < intf->num_altsetting; ++i) {
			struct usb_host_interface *alts;
			unsigned int psize;

			alts = &intf->altsetting[i];
			ep = uvc_find_endpoint(alts,
				stream->header.bEndpointAddress);
			if (ep == NULL)
				continue;

			/* Check if the bandwidth is high enough. */
			psize = uvc_endpoint_max_bpi(stream->dev->udev, ep);
			if (psize >= bandwidth && psize <= best_psize) {
				altsetting = alts->desc.bAlternateSetting;
				best_psize = psize;
				best_ep = ep;
			}
		}

		if (best_ep == NULL) {
			uvc_dbg(stream->dev, VIDEO,
				"No fast enough alt setting for requested bandwidth\n");
			return -EIO;
		}

		uvc_dbg(stream->dev, VIDEO,
			"Selecting alternate setting %u (%u B/frame bandwidth)\n",
			altsetting, best_psize);

		/*
		 * Some devices, namely the Logitech C910 and B910, are unable
		 * to recover from a USB autosuspend, unless the alternate
		 * setting of the streaming interface is toggled.
		 */
		if (stream->dev->quirks & UVC_QUIRK_WAKE_AUTOSUSPEND) {
			usb_set_interface(stream->dev->udev, intfnum,
					  altsetting);
			usb_set_interface(stream->dev->udev, intfnum, 0);
		}

		ret = usb_set_interface(stream->dev->udev, intfnum, altsetting);
		if (ret < 0)
			return ret;

		ret = uvc_init_video_isoc(stream, best_ep, gfp_flags);
	} else {
		/* Bulk endpoint, proceed to URB initialization. */
		ep = uvc_find_endpoint(&intf->altsetting[0],
				stream->header.bEndpointAddress);
		if (ep == NULL)
			return -EIO;

		/* Reject broken descriptors. */
		if (usb_endpoint_maxp(&ep->desc) == 0)
			return -EIO;

		ret = uvc_init_video_bulk(stream, ep, gfp_flags);
	}

	if (ret < 0)
		return ret;

	/* Submit the URBs. */
	for_each_uvc_urb(uvc_urb, stream) {
		ret = uvc_submit_urb(uvc_urb, gfp_flags);
		if (ret < 0) {
			dev_err(&stream->intf->dev,
				"Failed to submit URB %u (%d).\n",
				uvc_urb_index(uvc_urb), ret);
			uvc_video_stop_transfer(stream, 1);
			return ret;
		}
	}

	/*
	 * The Logitech C920 temporarily forgets that it should not be adjusting
	 * Exposure Absolute during init so restore controls to stored values.
	 */
	if (stream->dev->quirks & UVC_QUIRK_RESTORE_CTRLS_ON_INIT)
		uvc_ctrl_restore_values(stream->dev);

	return 0;
}

/* --------------------------------------------------------------------------
 * Suspend/resume
 */

/*
 * Stop streaming without disabling the video queue.
 *
 * To let userspace applications resume without trouble, we must not touch the
 * video buffers in any way. We mark the device as frozen to make sure the URB
 * completion handler won't try to cancel the queue when we kill the URBs.
 */
int uvc_video_suspend(struct uvc_streaming *stream)
{
	if (!uvc_queue_streaming(&stream->queue))
		return 0;

	stream->frozen = 1;
	uvc_video_stop_transfer(stream, 0);
	usb_set_interface(stream->dev->udev, stream->intfnum, 0);
	return 0;
}

/*
 * Reconfigure the video interface and restart streaming if it was enabled
 * before suspend.
 *
 * If an error occurs, disable the video queue. This will wake all pending
 * buffers, making sure userspace applications are notified of the problem
 * instead of waiting forever.
 */
int uvc_video_resume(struct uvc_streaming *stream, int reset)
{
	int ret;

	/*
	 * If the bus has been reset on resume, set the alternate setting to 0.
	 * This should be the default value, but some devices crash or otherwise
	 * misbehave if they don't receive a SET_INTERFACE request before any
	 * other video control request.
	 */
	if (reset)
		usb_set_interface(stream->dev->udev, stream->intfnum, 0);

	stream->frozen = 0;

	uvc_video_clock_reset(stream);

	if (!uvc_queue_streaming(&stream->queue))
		return 0;

	ret = uvc_commit_video(stream, &stream->ctrl);
	if (ret < 0)
		return ret;

	return uvc_video_start_transfer(stream, GFP_NOIO);
}

/* ------------------------------------------------------------------------
 * Video device
 */

/*
 * Initialize the UVC video device by switching to alternate setting 0 and
 * retrieve the default format.
 *
 * Some cameras (namely the Fuji Finepix) set the format and frame
 * indexes to zero. The UVC standard doesn't clearly make this a spec
 * violation, so try to silently fix the values if possible.
 *
 * This function is called before registering the device with V4L.
 */
int uvc_video_init(struct uvc_streaming *stream)
{
	struct uvc_streaming_control *probe = &stream->ctrl;
	struct uvc_format *format = NULL;
	struct uvc_frame *frame = NULL;
	struct uvc_urb *uvc_urb;
	unsigned int i;
	int ret;

	if (stream->nformats == 0) {
		dev_info(&stream->intf->dev,
			 "No supported video formats found.\n");
		return -EINVAL;
	}

	atomic_set(&stream->active, 0);

	/*
	 * Alternate setting 0 should be the default, yet the XBox Live Vision
	 * Cam (and possibly other devices) crash or otherwise misbehave if
	 * they don't receive a SET_INTERFACE request before any other video
	 * control request.
	 */
	usb_set_interface(stream->dev->udev, stream->intfnum, 0);

	/*
	 * Set the streaming probe control with default streaming parameters
	 * retrieved from the device. Webcams that don't support GET_DEF
	 * requests on the probe control will just keep their current streaming
	 * parameters.
	 */
	if (uvc_get_video_ctrl(stream, probe, 1, UVC_GET_DEF) == 0)
		uvc_set_video_ctrl(stream, probe, 1);

	/*
	 * Initialize the streaming parameters with the probe control current
	 * value. This makes sure SET_CUR requests on the streaming commit
	 * control will always use values retrieved from a successful GET_CUR
	 * request on the probe control, as required by the UVC specification.
	 */
	ret = uvc_get_video_ctrl(stream, probe, 1, UVC_GET_CUR);

	/*
	 * Elgato Cam Link 4k can be in a stalled state if the resolution of
	 * the external source has changed while the firmware initializes.
	 * Once in this state, the device is useless until it receives a
	 * USB reset. It has even been observed that the stalled state will
	 * continue even after unplugging the device.
	 */
	if (ret == -EPROTO &&
	    usb_match_one_id(stream->dev->intf, &elgato_cam_link_4k)) {
		dev_err(&stream->intf->dev, "Elgato Cam Link 4K firmware crash detected\n");
		dev_err(&stream->intf->dev, "Resetting the device, unplug and replug to recover\n");
		usb_reset_device(stream->dev->udev);
	}

	if (ret < 0)
		return ret;

	/*
	 * Check if the default format descriptor exists. Use the first
	 * available format otherwise.
	 */
	for (i = stream->nformats; i > 0; --i) {
		format = &stream->format[i-1];
		if (format->index == probe->bFormatIndex)
			break;
	}

	if (format->nframes == 0) {
		dev_info(&stream->intf->dev,
			 "No frame descriptor found for the default format.\n");
		return -EINVAL;
	}

	/*
	 * Zero bFrameIndex might be correct. Stream-based formats (including
	 * MPEG-2 TS and DV) do not support frames but have a dummy frame
	 * descriptor with bFrameIndex set to zero. If the default frame
	 * descriptor is not found, use the first available frame.
	 */
	for (i = format->nframes; i > 0; --i) {
		frame = &format->frame[i-1];
		if (frame->bFrameIndex == probe->bFrameIndex)
			break;
	}

	probe->bFormatIndex = format->index;
	probe->bFrameIndex = frame->bFrameIndex;

	stream->def_format = format;
	stream->cur_format = format;
	stream->cur_frame = frame;

	/* Select the video decoding function */
	if (stream->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		if (stream->dev->quirks & UVC_QUIRK_BUILTIN_ISIGHT)
			stream->decode = uvc_video_decode_isight;
		else if (stream->intf->num_altsetting > 1)
			stream->decode = uvc_video_decode_isoc;
		else
			stream->decode = uvc_video_decode_bulk;
	} else {
		if (stream->intf->num_altsetting == 1)
			stream->decode = uvc_video_encode_bulk;
		else {
			dev_info(&stream->intf->dev,
				 "Isochronous endpoints are not supported for video output devices.\n");
			return -EINVAL;
		}
	}

	/* Prepare asynchronous work items. */
	for_each_uvc_urb(uvc_urb, stream)
		INIT_WORK(&uvc_urb->work, uvc_video_copy_data_work);

	return 0;
}

int uvc_video_start_streaming(struct uvc_streaming *stream)
{
	int ret;

	ret = uvc_video_clock_init(stream);
	if (ret < 0)
		return ret;

	/* Commit the streaming parameters. */
	ret = uvc_commit_video(stream, &stream->ctrl);
	if (ret < 0)
		goto error_commit;

	ret = uvc_video_start_transfer(stream, GFP_KERNEL);
	if (ret < 0)
		goto error_video;

	return 0;

error_video:
	usb_set_interface(stream->dev->udev, stream->intfnum, 0);
error_commit:
	uvc_video_clock_cleanup(stream);

	return ret;
}

void uvc_video_stop_streaming(struct uvc_streaming *stream)
{
	uvc_video_stop_transfer(stream, 1);

	if (stream->intf->num_altsetting > 1) {
		usb_set_interface(stream->dev->udev, stream->intfnum, 0);
	} else {
		/*
		 * UVC doesn't specify how to inform a bulk-based device
		 * when the video stream is stopped. Windows sends a
		 * CLEAR_FEATURE(HALT) request to the video streaming
		 * bulk endpoint, mimic the same behaviour.
		 */
		unsigned int epnum = stream->header.bEndpointAddress
				   & USB_ENDPOINT_NUMBER_MASK;
		unsigned int dir = stream->header.bEndpointAddress
				 & USB_ENDPOINT_DIR_MASK;
		unsigned int pipe;

		pipe = usb_sndbulkpipe(stream->dev->udev, epnum) | dir;
		usb_clear_halt(stream->dev->udev, pipe);
	}

	uvc_video_clock_cleanup(stream);
}
