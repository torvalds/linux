/*
    init/start/stop/exit stream functions
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2004  Chris Kennedy <c@groovy.org>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* License: GPL
 * Author: Kevin Thayer <nufan_wfk at yahoo dot com>
 *
 * This file will hold API related functions, both internal (firmware api)
 * and external (v4l2, etc)
 *
 * -----
 * MPG600/MPG160 support by  T.Adachi <tadachi@tadachi-net.com>
 *                      and Takeru KOMORIYA<komoriya@paken.org>
 *
 * AVerMedia M179 GPIO info by Chris Pinkham <cpinkham@bc2va.org>
 *                using information provided by Jiun-Kuei Jung @ AVerMedia.
 */

#include "ivtv-driver.h"
#include "ivtv-fileops.h"
#include "ivtv-queue.h"
#include "ivtv-mailbox.h"
#include "ivtv-ioctl.h"
#include "ivtv-irq.h"
#include "ivtv-yuv.h"
#include "ivtv-cards.h"
#include "ivtv-streams.h"

static const struct v4l2_file_operations ivtv_v4l2_enc_fops = {
	.owner = THIS_MODULE,
	.read = ivtv_v4l2_read,
	.write = ivtv_v4l2_write,
	.open = ivtv_v4l2_open,
	.unlocked_ioctl = ivtv_v4l2_ioctl,
	.release = ivtv_v4l2_close,
	.poll = ivtv_v4l2_enc_poll,
};

static const struct v4l2_file_operations ivtv_v4l2_dec_fops = {
	.owner = THIS_MODULE,
	.read = ivtv_v4l2_read,
	.write = ivtv_v4l2_write,
	.open = ivtv_v4l2_open,
	.unlocked_ioctl = ivtv_v4l2_ioctl,
	.release = ivtv_v4l2_close,
	.poll = ivtv_v4l2_dec_poll,
};

#define IVTV_V4L2_DEC_MPG_OFFSET  16	/* offset from 0 to register decoder mpg v4l2 minors on */
#define IVTV_V4L2_ENC_PCM_OFFSET  24	/* offset from 0 to register pcm v4l2 minors on */
#define IVTV_V4L2_ENC_YUV_OFFSET  32	/* offset from 0 to register yuv v4l2 minors on */
#define IVTV_V4L2_DEC_YUV_OFFSET  48	/* offset from 0 to register decoder yuv v4l2 minors on */
#define IVTV_V4L2_DEC_VBI_OFFSET   8	/* offset from 0 to register decoder vbi input v4l2 minors on */
#define IVTV_V4L2_DEC_VOUT_OFFSET 16	/* offset from 0 to register vbi output v4l2 minors on */

static struct {
	const char *name;
	int vfl_type;
	int num_offset;
	int dma, pio;
	enum v4l2_buf_type buf_type;
	const struct v4l2_file_operations *fops;
} ivtv_stream_info[] = {
	{	/* IVTV_ENC_STREAM_TYPE_MPG */
		"encoder MPG",
		VFL_TYPE_GRABBER, 0,
		PCI_DMA_FROMDEVICE, 0, V4L2_BUF_TYPE_VIDEO_CAPTURE,
		&ivtv_v4l2_enc_fops
	},
	{	/* IVTV_ENC_STREAM_TYPE_YUV */
		"encoder YUV",
		VFL_TYPE_GRABBER, IVTV_V4L2_ENC_YUV_OFFSET,
		PCI_DMA_FROMDEVICE, 0, V4L2_BUF_TYPE_VIDEO_CAPTURE,
		&ivtv_v4l2_enc_fops
	},
	{	/* IVTV_ENC_STREAM_TYPE_VBI */
		"encoder VBI",
		VFL_TYPE_VBI, 0,
		PCI_DMA_FROMDEVICE, 0, V4L2_BUF_TYPE_VBI_CAPTURE,
		&ivtv_v4l2_enc_fops
	},
	{	/* IVTV_ENC_STREAM_TYPE_PCM */
		"encoder PCM",
		VFL_TYPE_GRABBER, IVTV_V4L2_ENC_PCM_OFFSET,
		PCI_DMA_FROMDEVICE, 0, V4L2_BUF_TYPE_PRIVATE,
		&ivtv_v4l2_enc_fops
	},
	{	/* IVTV_ENC_STREAM_TYPE_RAD */
		"encoder radio",
		VFL_TYPE_RADIO, 0,
		PCI_DMA_NONE, 1, V4L2_BUF_TYPE_PRIVATE,
		&ivtv_v4l2_enc_fops
	},
	{	/* IVTV_DEC_STREAM_TYPE_MPG */
		"decoder MPG",
		VFL_TYPE_GRABBER, IVTV_V4L2_DEC_MPG_OFFSET,
		PCI_DMA_TODEVICE, 0, V4L2_BUF_TYPE_VIDEO_OUTPUT,
		&ivtv_v4l2_dec_fops
	},
	{	/* IVTV_DEC_STREAM_TYPE_VBI */
		"decoder VBI",
		VFL_TYPE_VBI, IVTV_V4L2_DEC_VBI_OFFSET,
		PCI_DMA_NONE, 1, V4L2_BUF_TYPE_VBI_CAPTURE,
		&ivtv_v4l2_enc_fops
	},
	{	/* IVTV_DEC_STREAM_TYPE_VOUT */
		"decoder VOUT",
		VFL_TYPE_VBI, IVTV_V4L2_DEC_VOUT_OFFSET,
		PCI_DMA_NONE, 1, V4L2_BUF_TYPE_VBI_OUTPUT,
		&ivtv_v4l2_dec_fops
	},
	{	/* IVTV_DEC_STREAM_TYPE_YUV */
		"decoder YUV",
		VFL_TYPE_GRABBER, IVTV_V4L2_DEC_YUV_OFFSET,
		PCI_DMA_TODEVICE, 0, V4L2_BUF_TYPE_VIDEO_OUTPUT,
		&ivtv_v4l2_dec_fops
	}
};

static void ivtv_stream_init(struct ivtv *itv, int type)
{
	struct ivtv_stream *s = &itv->streams[type];
	struct video_device *vdev = s->vdev;

	/* we need to keep vdev, so restore it afterwards */
	memset(s, 0, sizeof(*s));
	s->vdev = vdev;

	/* initialize ivtv_stream fields */
	s->itv = itv;
	s->type = type;
	s->name = ivtv_stream_info[type].name;

	if (ivtv_stream_info[type].pio)
		s->dma = PCI_DMA_NONE;
	else
		s->dma = ivtv_stream_info[type].dma;
	s->buf_size = itv->stream_buf_size[type];
	if (s->buf_size)
		s->buffers = (itv->options.kilobytes[type] * 1024 + s->buf_size - 1) / s->buf_size;
	spin_lock_init(&s->qlock);
	init_waitqueue_head(&s->waitq);
	s->id = -1;
	s->sg_handle = IVTV_DMA_UNMAPPED;
	ivtv_queue_init(&s->q_free);
	ivtv_queue_init(&s->q_full);
	ivtv_queue_init(&s->q_dma);
	ivtv_queue_init(&s->q_predma);
	ivtv_queue_init(&s->q_io);
}

static int ivtv_prep_dev(struct ivtv *itv, int type)
{
	struct ivtv_stream *s = &itv->streams[type];
	int num_offset = ivtv_stream_info[type].num_offset;
	int num = itv->instance + ivtv_first_minor + num_offset;

	/* These four fields are always initialized. If vdev == NULL, then
	   this stream is not in use. In that case no other fields but these
	   four can be used. */
	s->vdev = NULL;
	s->itv = itv;
	s->type = type;
	s->name = ivtv_stream_info[type].name;

	/* Check whether the radio is supported */
	if (type == IVTV_ENC_STREAM_TYPE_RAD && !(itv->v4l2_cap & V4L2_CAP_RADIO))
		return 0;
	if (type >= IVTV_DEC_STREAM_TYPE_MPG && !(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
		return 0;

	/* User explicitly selected 0 buffers for these streams, so don't
	   create them. */
	if (ivtv_stream_info[type].dma != PCI_DMA_NONE &&
	    itv->options.kilobytes[type] == 0) {
		IVTV_INFO("Disabled %s device\n", ivtv_stream_info[type].name);
		return 0;
	}

	ivtv_stream_init(itv, type);

	/* allocate and initialize the v4l2 video device structure */
	s->vdev = video_device_alloc();
	if (s->vdev == NULL) {
		IVTV_ERR("Couldn't allocate v4l2 video_device for %s\n", s->name);
		return -ENOMEM;
	}

	snprintf(s->vdev->name, sizeof(s->vdev->name), "%s %s",
			itv->v4l2_dev.name, s->name);

	s->vdev->num = num;
	s->vdev->v4l2_dev = &itv->v4l2_dev;
	s->vdev->fops = ivtv_stream_info[type].fops;
	s->vdev->release = video_device_release;
	s->vdev->tvnorms = V4L2_STD_ALL;
	ivtv_set_funcs(s->vdev);
	return 0;
}

/* Initialize v4l2 variables and prepare v4l2 devices */
int ivtv_streams_setup(struct ivtv *itv)
{
	int type;

	/* Setup V4L2 Devices */
	for (type = 0; type < IVTV_MAX_STREAMS; type++) {
		/* Prepare device */
		if (ivtv_prep_dev(itv, type))
			break;

		if (itv->streams[type].vdev == NULL)
			continue;

		/* Allocate Stream */
		if (ivtv_stream_alloc(&itv->streams[type]))
			break;
	}
	if (type == IVTV_MAX_STREAMS)
		return 0;

	/* One or more streams could not be initialized. Clean 'em all up. */
	ivtv_streams_cleanup(itv, 0);
	return -ENOMEM;
}

static int ivtv_reg_dev(struct ivtv *itv, int type)
{
	struct ivtv_stream *s = &itv->streams[type];
	int vfl_type = ivtv_stream_info[type].vfl_type;
	const char *name;
	int num;

	if (s->vdev == NULL)
		return 0;

	num = s->vdev->num;
	/* card number + user defined offset + device offset */
	if (type != IVTV_ENC_STREAM_TYPE_MPG) {
		struct ivtv_stream *s_mpg = &itv->streams[IVTV_ENC_STREAM_TYPE_MPG];

		if (s_mpg->vdev)
			num = s_mpg->vdev->num + ivtv_stream_info[type].num_offset;
	}
	video_set_drvdata(s->vdev, s);

	/* Register device. First try the desired minor, then any free one. */
	if (video_register_device_no_warn(s->vdev, vfl_type, num)) {
		IVTV_ERR("Couldn't register v4l2 device for %s (device node number %d)\n",
				s->name, num);
		video_device_release(s->vdev);
		s->vdev = NULL;
		return -ENOMEM;
	}
	name = video_device_node_name(s->vdev);

	switch (vfl_type) {
	case VFL_TYPE_GRABBER:
		IVTV_INFO("Registered device %s for %s (%d kB)\n",
			name, s->name, itv->options.kilobytes[type]);
		break;
	case VFL_TYPE_RADIO:
		IVTV_INFO("Registered device %s for %s\n",
			name, s->name);
		break;
	case VFL_TYPE_VBI:
		if (itv->options.kilobytes[type])
			IVTV_INFO("Registered device %s for %s (%d kB)\n",
				name, s->name, itv->options.kilobytes[type]);
		else
			IVTV_INFO("Registered device %s for %s\n",
				name, s->name);
		break;
	}
	return 0;
}

/* Register v4l2 devices */
int ivtv_streams_register(struct ivtv *itv)
{
	int type;
	int err = 0;

	/* Register V4L2 devices */
	for (type = 0; type < IVTV_MAX_STREAMS; type++)
		err |= ivtv_reg_dev(itv, type);

	if (err == 0)
		return 0;

	/* One or more streams could not be initialized. Clean 'em all up. */
	ivtv_streams_cleanup(itv, 1);
	return -ENOMEM;
}

/* Unregister v4l2 devices */
void ivtv_streams_cleanup(struct ivtv *itv, int unregister)
{
	int type;

	/* Teardown all streams */
	for (type = 0; type < IVTV_MAX_STREAMS; type++) {
		struct video_device *vdev = itv->streams[type].vdev;

		itv->streams[type].vdev = NULL;
		if (vdev == NULL)
			continue;

		ivtv_stream_free(&itv->streams[type]);
		/* Unregister or release device */
		if (unregister)
			video_unregister_device(vdev);
		else
			video_device_release(vdev);
	}
}

static void ivtv_vbi_setup(struct ivtv *itv)
{
	int raw = ivtv_raw_vbi(itv);
	u32 data[CX2341X_MBOX_MAX_DATA];
	int lines;
	int i;

	/* Reset VBI */
	ivtv_vapi(itv, CX2341X_ENC_SET_VBI_LINE, 5, 0xffff , 0, 0, 0, 0);

	/* setup VBI registers */
	v4l2_subdev_call(itv->sd_video, video, s_fmt, &itv->vbi.in);

	/* determine number of lines and total number of VBI bytes.
	   A raw line takes 1443 bytes: 2 * 720 + 4 byte frame header - 1
	   The '- 1' byte is probably an unused U or V byte. Or something...
	   A sliced line takes 51 bytes: 4 byte frame header, 4 byte internal
	   header, 42 data bytes + checksum (to be confirmed) */
	if (raw) {
		lines = itv->vbi.count * 2;
	} else {
		lines = itv->is_60hz ? 24 : 38;
		if (itv->is_60hz && (itv->hw_flags & IVTV_HW_CX25840))
			lines += 2;
	}

	itv->vbi.enc_size = lines * (raw ? itv->vbi.raw_size : itv->vbi.sliced_size);

	/* Note: sliced vs raw flag doesn't seem to have any effect
	   TODO: check mode (0x02) value with older ivtv versions. */
	data[0] = raw | 0x02 | (0xbd << 8);

	/* Every X number of frames a VBI interrupt arrives (frames as in 25 or 30 fps) */
	data[1] = 1;
	/* The VBI frames are stored in a ringbuffer with this size (with a VBI frame as unit) */
	data[2] = raw ? 4 : 4 * (itv->vbi.raw_size / itv->vbi.enc_size);
	/* The start/stop codes determine which VBI lines end up in the raw VBI data area.
	   The codes are from table 24 in the saa7115 datasheet. Each raw/sliced/video line
	   is framed with codes FF0000XX where XX is the SAV/EAV (Start/End of Active Video)
	   code. These values for raw VBI are obtained from a driver disassembly. The sliced
	   start/stop codes was deduced from this, but they do not appear in the driver.
	   Other code pairs that I found are: 0x250E6249/0x13545454 and 0x25256262/0x38137F54.
	   However, I have no idea what these values are for. */
	if (itv->hw_flags & IVTV_HW_CX25840) {
		/* Setup VBI for the cx25840 digitizer */
		if (raw) {
			data[3] = 0x20602060;
			data[4] = 0x30703070;
		} else {
			data[3] = 0xB0F0B0F0;
			data[4] = 0xA0E0A0E0;
		}
		/* Lines per frame */
		data[5] = lines;
		/* bytes per line */
		data[6] = (raw ? itv->vbi.raw_size : itv->vbi.sliced_size);
	} else {
		/* Setup VBI for the saa7115 digitizer */
		if (raw) {
			data[3] = 0x25256262;
			data[4] = 0x387F7F7F;
		} else {
			data[3] = 0xABABECEC;
			data[4] = 0xB6F1F1F1;
		}
		/* Lines per frame */
		data[5] = lines;
		/* bytes per line */
		data[6] = itv->vbi.enc_size / lines;
	}

	IVTV_DEBUG_INFO(
		"Setup VBI API header 0x%08x pkts %d buffs %d ln %d sz %d\n",
			data[0], data[1], data[2], data[5], data[6]);

	ivtv_api(itv, CX2341X_ENC_SET_VBI_CONFIG, 7, data);

	/* returns the VBI encoder memory area. */
	itv->vbi.enc_start = data[2];
	itv->vbi.fpi = data[0];
	if (!itv->vbi.fpi)
		itv->vbi.fpi = 1;

	IVTV_DEBUG_INFO("Setup VBI start 0x%08x frames %d fpi %d\n",
		itv->vbi.enc_start, data[1], itv->vbi.fpi);

	/* select VBI lines.
	   Note that the sliced argument seems to have no effect. */
	for (i = 2; i <= 24; i++) {
		int valid;

		if (itv->is_60hz) {
			valid = i >= 10 && i < 22;
		} else {
			valid = i >= 6 && i < 24;
		}
		ivtv_vapi(itv, CX2341X_ENC_SET_VBI_LINE, 5, i - 1,
				valid, 0 , 0, 0);
		ivtv_vapi(itv, CX2341X_ENC_SET_VBI_LINE, 5, (i - 1) | 0x80000000,
				valid, 0, 0, 0);
	}

	/* Remaining VBI questions:
	   - Is it possible to select particular VBI lines only for inclusion in the MPEG
	   stream? Currently you can only get the first X lines.
	   - Is mixed raw and sliced VBI possible?
	   - What's the meaning of the raw/sliced flag?
	   - What's the meaning of params 2, 3 & 4 of the Select VBI command? */
}

int ivtv_start_v4l2_encode_stream(struct ivtv_stream *s)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	struct ivtv *itv = s->itv;
	struct cx2341x_mpeg_params *p = &itv->params;
	int captype = 0, subtype = 0;
	int enable_passthrough = 0;

	if (s->vdev == NULL)
		return -EINVAL;

	IVTV_DEBUG_INFO("Start encoder stream %s\n", s->name);

	switch (s->type) {
	case IVTV_ENC_STREAM_TYPE_MPG:
		captype = 0;
		subtype = 3;

		/* Stop Passthrough */
		if (itv->output_mode == OUT_PASSTHROUGH) {
			ivtv_passthrough_mode(itv, 0);
			enable_passthrough = 1;
		}
		itv->mpg_data_received = itv->vbi_data_inserted = 0;
		itv->dualwatch_jiffies = jiffies;
		itv->dualwatch_stereo_mode = p->audio_properties & 0x0300;
		itv->search_pack_header = 0;
		break;

	case IVTV_ENC_STREAM_TYPE_YUV:
		if (itv->output_mode == OUT_PASSTHROUGH) {
			captype = 2;
			subtype = 11;	/* video+audio+decoder */
			break;
		}
		captype = 1;
		subtype = 1;
		break;
	case IVTV_ENC_STREAM_TYPE_PCM:
		captype = 1;
		subtype = 2;
		break;
	case IVTV_ENC_STREAM_TYPE_VBI:
		captype = 1;
		subtype = 4;

		itv->vbi.frame = 0;
		itv->vbi.inserted_frame = 0;
		memset(itv->vbi.sliced_mpeg_size,
			0, sizeof(itv->vbi.sliced_mpeg_size));
		break;
	default:
		return -EINVAL;
	}
	s->subtype = subtype;
	s->buffers_stolen = 0;

	/* Clear Streamoff flags in case left from last capture */
	clear_bit(IVTV_F_S_STREAMOFF, &s->s_flags);

	if (atomic_read(&itv->capturing) == 0) {
		int digitizer;

		/* Always use frame based mode. Experiments have demonstrated that byte
		   stream based mode results in dropped frames and corruption. Not often,
		   but occasionally. Many thanks go to Leonard Orb who spent a lot of
		   effort and time trying to trace the cause of the drop outs. */
		/* 1 frame per DMA */
		/*ivtv_vapi(itv, CX2341X_ENC_SET_DMA_BLOCK_SIZE, 2, 128, 0); */
		ivtv_vapi(itv, CX2341X_ENC_SET_DMA_BLOCK_SIZE, 2, 1, 1);

		/* Stuff from Windows, we don't know what it is */
		ivtv_vapi(itv, CX2341X_ENC_SET_VERT_CROP_LINE, 1, 0);
		/* According to the docs, this should be correct. However, this is
		   untested. I don't dare enable this without having tested it.
		   Only very few old cards actually have this hardware combination.
		ivtv_vapi(itv, CX2341X_ENC_SET_VERT_CROP_LINE, 1,
			((itv->hw_flags & IVTV_HW_SAA7114) && itv->is_60hz) ? 10001 : 0);
		*/
		ivtv_vapi(itv, CX2341X_ENC_MISC, 2, 3, !itv->has_cx23415);
		ivtv_vapi(itv, CX2341X_ENC_MISC, 2, 8, 0);
		ivtv_vapi(itv, CX2341X_ENC_MISC, 2, 4, 1);
		ivtv_vapi(itv, CX2341X_ENC_MISC, 1, 12);

		/* assign placeholder */
		ivtv_vapi(itv, CX2341X_ENC_SET_PLACEHOLDER, 12,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

		if (itv->card->hw_all & (IVTV_HW_SAA7115 | IVTV_HW_SAA717X))
		    digitizer = 0xF1;
		else if (itv->card->hw_all & IVTV_HW_SAA7114)
		    digitizer = 0xEF;
		else /* cx25840 */
		    digitizer = 0x140;

		ivtv_vapi(itv, CX2341X_ENC_SET_NUM_VSYNC_LINES, 2, digitizer, digitizer);

		/* Setup VBI */
		if (itv->v4l2_cap & V4L2_CAP_VBI_CAPTURE) {
			ivtv_vbi_setup(itv);
		}

		/* assign program index info. Mask 7: select I/P/B, Num_req: 400 max */
		ivtv_vapi_result(itv, data, CX2341X_ENC_SET_PGM_INDEX_INFO, 2, 7, 400);
		itv->pgm_info_offset = data[0];
		itv->pgm_info_num = data[1];
		itv->pgm_info_write_idx = 0;
		itv->pgm_info_read_idx = 0;

		IVTV_DEBUG_INFO("PGM Index at 0x%08x with %d elements\n",
				itv->pgm_info_offset, itv->pgm_info_num);

		/* Setup API for Stream */
		cx2341x_update(itv, ivtv_api_func, NULL, p);

		/* mute if capturing radio */
		if (test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags))
			ivtv_vapi(itv, CX2341X_ENC_MUTE_VIDEO, 1,
				1 | (p->video_mute_yuv << 8));
	}

	/* Vsync Setup */
	if (itv->has_cx23415 && !test_and_set_bit(IVTV_F_I_DIG_RST, &itv->i_flags)) {
		/* event notification (on) */
		ivtv_vapi(itv, CX2341X_ENC_SET_EVENT_NOTIFICATION, 4, 0, 1, IVTV_IRQ_ENC_VIM_RST, -1);
		ivtv_clear_irq_mask(itv, IVTV_IRQ_ENC_VIM_RST);
	}

	if (atomic_read(&itv->capturing) == 0) {
		/* Clear all Pending Interrupts */
		ivtv_set_irq_mask(itv, IVTV_IRQ_MASK_CAPTURE);

		clear_bit(IVTV_F_I_EOS, &itv->i_flags);

		/* Initialize Digitizer for Capture */
		/* Avoid tinny audio problem - ensure audio clocks are going */
		v4l2_subdev_call(itv->sd_audio, audio, s_stream, 1);
		/* Avoid unpredictable PCI bus hang - disable video clocks */
		v4l2_subdev_call(itv->sd_video, video, s_stream, 0);
		ivtv_msleep_timeout(150, 1);
		ivtv_vapi(itv, CX2341X_ENC_INITIALIZE_INPUT, 0);
		v4l2_subdev_call(itv->sd_video, video, s_stream, 1);
		ivtv_msleep_timeout(150, 1);
	}

	/* begin_capture */
	if (ivtv_vapi(itv, CX2341X_ENC_START_CAPTURE, 2, captype, subtype))
	{
		IVTV_DEBUG_WARN( "Error starting capture!\n");
		return -EINVAL;
	}

	/* Start Passthrough */
	if (enable_passthrough) {
		ivtv_passthrough_mode(itv, 1);
	}

	if (s->type == IVTV_ENC_STREAM_TYPE_VBI)
		ivtv_clear_irq_mask(itv, IVTV_IRQ_ENC_VBI_CAP);
	else
		ivtv_clear_irq_mask(itv, IVTV_IRQ_MASK_CAPTURE);

	/* you're live! sit back and await interrupts :) */
	atomic_inc(&itv->capturing);
	return 0;
}

static int ivtv_setup_v4l2_decode_stream(struct ivtv_stream *s)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	struct ivtv *itv = s->itv;
	struct cx2341x_mpeg_params *p = &itv->params;
	int datatype;

	if (s->vdev == NULL)
		return -EINVAL;

	IVTV_DEBUG_INFO("Setting some initial decoder settings\n");

	/* set audio mode to left/stereo  for dual/stereo mode. */
	ivtv_vapi(itv, CX2341X_DEC_SET_AUDIO_MODE, 2, itv->audio_bilingual_mode, itv->audio_stereo_mode);

	/* set number of internal decoder buffers */
	ivtv_vapi(itv, CX2341X_DEC_SET_DISPLAY_BUFFERS, 1, 0);

	/* prebuffering */
	ivtv_vapi(itv, CX2341X_DEC_SET_PREBUFFERING, 1, 1);

	/* extract from user packets */
	ivtv_vapi_result(itv, data, CX2341X_DEC_EXTRACT_VBI, 1, 1);
	itv->vbi.dec_start = data[0];

	IVTV_DEBUG_INFO("Decoder VBI RE-Insert start 0x%08x size 0x%08x\n",
		itv->vbi.dec_start, data[1]);

	/* set decoder source settings */
	/* Data type: 0 = mpeg from host,
	   1 = yuv from encoder,
	   2 = yuv_from_host */
	switch (s->type) {
	case IVTV_DEC_STREAM_TYPE_YUV:
		datatype = itv->output_mode == OUT_PASSTHROUGH ? 1 : 2;
		IVTV_DEBUG_INFO("Setup DEC YUV Stream data[0] = %d\n", datatype);
		break;
	case IVTV_DEC_STREAM_TYPE_MPG:
	default:
		datatype = 0;
		break;
	}
	if (ivtv_vapi(itv, CX2341X_DEC_SET_DECODER_SOURCE, 4, datatype,
			p->width, p->height, p->audio_properties)) {
		IVTV_DEBUG_WARN("Couldn't initialize decoder source\n");
	}
	return 0;
}

int ivtv_start_v4l2_decode_stream(struct ivtv_stream *s, int gop_offset)
{
	struct ivtv *itv = s->itv;

	if (s->vdev == NULL)
		return -EINVAL;

	if (test_and_set_bit(IVTV_F_S_STREAMING, &s->s_flags))
		return 0;	/* already started */

	IVTV_DEBUG_INFO("Starting decode stream %s (gop_offset %d)\n", s->name, gop_offset);

	ivtv_setup_v4l2_decode_stream(s);

	/* set dma size to 65536 bytes */
	ivtv_vapi(itv, CX2341X_DEC_SET_DMA_BLOCK_SIZE, 1, 65536);

	/* Clear Streamoff */
	clear_bit(IVTV_F_S_STREAMOFF, &s->s_flags);

	/* Zero out decoder counters */
	writel(0, &itv->dec_mbox.mbox[IVTV_MBOX_DMA_END].data[0]);
	writel(0, &itv->dec_mbox.mbox[IVTV_MBOX_DMA_END].data[1]);
	writel(0, &itv->dec_mbox.mbox[IVTV_MBOX_DMA_END].data[2]);
	writel(0, &itv->dec_mbox.mbox[IVTV_MBOX_DMA_END].data[3]);
	writel(0, &itv->dec_mbox.mbox[IVTV_MBOX_DMA].data[0]);
	writel(0, &itv->dec_mbox.mbox[IVTV_MBOX_DMA].data[1]);
	writel(0, &itv->dec_mbox.mbox[IVTV_MBOX_DMA].data[2]);
	writel(0, &itv->dec_mbox.mbox[IVTV_MBOX_DMA].data[3]);

	/* turn on notification of dual/stereo mode change */
	ivtv_vapi(itv, CX2341X_DEC_SET_EVENT_NOTIFICATION, 4, 0, 1, IVTV_IRQ_DEC_AUD_MODE_CHG, -1);

	/* start playback */
	ivtv_vapi(itv, CX2341X_DEC_START_PLAYBACK, 2, gop_offset, 0);

	/* Clear the following Interrupt mask bits for decoding */
	ivtv_clear_irq_mask(itv, IVTV_IRQ_MASK_DECODE);
	IVTV_DEBUG_IRQ("IRQ Mask is now: 0x%08x\n", itv->irqmask);

	/* you're live! sit back and await interrupts :) */
	atomic_inc(&itv->decoding);
	return 0;
}

void ivtv_stop_all_captures(struct ivtv *itv)
{
	int i;

	for (i = IVTV_MAX_STREAMS - 1; i >= 0; i--) {
		struct ivtv_stream *s = &itv->streams[i];

		if (s->vdev == NULL)
			continue;
		if (test_bit(IVTV_F_S_STREAMING, &s->s_flags)) {
			ivtv_stop_v4l2_encode_stream(s, 0);
		}
	}
}

int ivtv_stop_v4l2_encode_stream(struct ivtv_stream *s, int gop_end)
{
	struct ivtv *itv = s->itv;
	DECLARE_WAITQUEUE(wait, current);
	int cap_type;
	int stopmode;

	if (s->vdev == NULL)
		return -EINVAL;

	/* This function assumes that you are allowed to stop the capture
	   and that we are actually capturing */

	IVTV_DEBUG_INFO("Stop Capture\n");

	if (s->type == IVTV_DEC_STREAM_TYPE_VOUT)
		return 0;
	if (atomic_read(&itv->capturing) == 0)
		return 0;

	switch (s->type) {
	case IVTV_ENC_STREAM_TYPE_YUV:
		cap_type = 1;
		break;
	case IVTV_ENC_STREAM_TYPE_PCM:
		cap_type = 1;
		break;
	case IVTV_ENC_STREAM_TYPE_VBI:
		cap_type = 1;
		break;
	case IVTV_ENC_STREAM_TYPE_MPG:
	default:
		cap_type = 0;
		break;
	}

	/* Stop Capture Mode */
	if (s->type == IVTV_ENC_STREAM_TYPE_MPG && gop_end) {
		stopmode = 0;
	} else {
		stopmode = 1;
	}

	/* end_capture */
	/* when: 0 =  end of GOP  1 = NOW!, type: 0 = mpeg, subtype: 3 = video+audio */
	ivtv_vapi(itv, CX2341X_ENC_STOP_CAPTURE, 3, stopmode, cap_type, s->subtype);

	if (!test_bit(IVTV_F_S_PASSTHROUGH, &s->s_flags)) {
		if (s->type == IVTV_ENC_STREAM_TYPE_MPG && gop_end) {
			/* only run these if we're shutting down the last cap */
			unsigned long duration;
			unsigned long then = jiffies;

			add_wait_queue(&itv->eos_waitq, &wait);

			set_current_state(TASK_INTERRUPTIBLE);

			/* wait 2s for EOS interrupt */
			while (!test_bit(IVTV_F_I_EOS, &itv->i_flags) &&
				time_before(jiffies,
					    then + msecs_to_jiffies(2000))) {
				schedule_timeout(msecs_to_jiffies(10));
			}

			/* To convert jiffies to ms, we must multiply by 1000
			 * and divide by HZ.  To avoid runtime division, we
			 * convert this to multiplication by 1000/HZ.
			 * Since integer division truncates, we get the best
			 * accuracy if we do a rounding calculation of the constant.
			 * Think of the case where HZ is 1024.
			 */
			duration = ((1000 + HZ / 2) / HZ) * (jiffies - then);

			if (!test_bit(IVTV_F_I_EOS, &itv->i_flags)) {
				IVTV_DEBUG_WARN("%s: EOS interrupt not received! stopping anyway.\n", s->name);
				IVTV_DEBUG_WARN("%s: waited %lu ms.\n", s->name, duration);
			} else {
				IVTV_DEBUG_INFO("%s: EOS took %lu ms to occur.\n", s->name, duration);
			}
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&itv->eos_waitq, &wait);
			set_bit(IVTV_F_S_STREAMOFF, &s->s_flags);
		}

		/* Handle any pending interrupts */
		ivtv_msleep_timeout(100, 1);
	}

	atomic_dec(&itv->capturing);

	/* Clear capture and no-read bits */
	clear_bit(IVTV_F_S_STREAMING, &s->s_flags);

	if (s->type == IVTV_ENC_STREAM_TYPE_VBI)
		ivtv_set_irq_mask(itv, IVTV_IRQ_ENC_VBI_CAP);

	if (atomic_read(&itv->capturing) > 0) {
		return 0;
	}

	/* Set the following Interrupt mask bits for capture */
	ivtv_set_irq_mask(itv, IVTV_IRQ_MASK_CAPTURE);
	del_timer(&itv->dma_timer);

	/* event notification (off) */
	if (test_and_clear_bit(IVTV_F_I_DIG_RST, &itv->i_flags)) {
		/* type: 0 = refresh */
		/* on/off: 0 = off, intr: 0x10000000, mbox_id: -1: none */
		ivtv_vapi(itv, CX2341X_ENC_SET_EVENT_NOTIFICATION, 4, 0, 0, IVTV_IRQ_ENC_VIM_RST, -1);
		ivtv_set_irq_mask(itv, IVTV_IRQ_ENC_VIM_RST);
	}

	wake_up(&s->waitq);

	return 0;
}

int ivtv_stop_v4l2_decode_stream(struct ivtv_stream *s, int flags, u64 pts)
{
	struct ivtv *itv = s->itv;

	if (s->vdev == NULL)
		return -EINVAL;

	if (s->type != IVTV_DEC_STREAM_TYPE_YUV && s->type != IVTV_DEC_STREAM_TYPE_MPG)
		return -EINVAL;

	if (!test_bit(IVTV_F_S_STREAMING, &s->s_flags))
		return 0;

	IVTV_DEBUG_INFO("Stop Decode at %llu, flags: %x\n", (unsigned long long)pts, flags);

	/* Stop Decoder */
	if (!(flags & VIDEO_CMD_STOP_IMMEDIATELY) || pts) {
		u32 tmp = 0;

		/* Wait until the decoder is no longer running */
		if (pts) {
			ivtv_vapi(itv, CX2341X_DEC_STOP_PLAYBACK, 3,
				0, (u32)(pts & 0xffffffff), (u32)(pts >> 32));
		}
		while (1) {
			u32 data[CX2341X_MBOX_MAX_DATA];
			ivtv_vapi_result(itv, data, CX2341X_DEC_GET_XFER_INFO, 0);
			if (s->q_full.buffers + s->q_dma.buffers == 0) {
				if (tmp == data[3])
					break;
				tmp = data[3];
			}
			if (ivtv_msleep_timeout(100, 1))
				break;
		}
	}
	ivtv_vapi(itv, CX2341X_DEC_STOP_PLAYBACK, 3, flags & VIDEO_CMD_STOP_TO_BLACK, 0, 0);

	/* turn off notification of dual/stereo mode change */
	ivtv_vapi(itv, CX2341X_DEC_SET_EVENT_NOTIFICATION, 4, 0, 0, IVTV_IRQ_DEC_AUD_MODE_CHG, -1);

	ivtv_set_irq_mask(itv, IVTV_IRQ_MASK_DECODE);
	del_timer(&itv->dma_timer);

	clear_bit(IVTV_F_S_NEEDS_DATA, &s->s_flags);
	clear_bit(IVTV_F_S_STREAMING, &s->s_flags);
	ivtv_flush_queues(s);

	/* decrement decoding */
	atomic_dec(&itv->decoding);

	set_bit(IVTV_F_I_EV_DEC_STOPPED, &itv->i_flags);
	wake_up(&itv->event_waitq);

	/* wake up wait queues */
	wake_up(&s->waitq);

	return 0;
}

int ivtv_passthrough_mode(struct ivtv *itv, int enable)
{
	struct ivtv_stream *yuv_stream = &itv->streams[IVTV_ENC_STREAM_TYPE_YUV];
	struct ivtv_stream *dec_stream = &itv->streams[IVTV_DEC_STREAM_TYPE_YUV];

	if (yuv_stream->vdev == NULL || dec_stream->vdev == NULL)
		return -EINVAL;

	IVTV_DEBUG_INFO("ivtv ioctl: Select passthrough mode\n");

	/* Prevent others from starting/stopping streams while we
	   initiate/terminate passthrough mode */
	if (enable) {
		if (itv->output_mode == OUT_PASSTHROUGH) {
			return 0;
		}
		if (ivtv_set_output_mode(itv, OUT_PASSTHROUGH) != OUT_PASSTHROUGH)
			return -EBUSY;

		/* Fully initialize stream, and then unflag init */
		set_bit(IVTV_F_S_PASSTHROUGH, &dec_stream->s_flags);
		set_bit(IVTV_F_S_STREAMING, &dec_stream->s_flags);

		/* Setup YUV Decoder */
		ivtv_setup_v4l2_decode_stream(dec_stream);

		/* Start Decoder */
		ivtv_vapi(itv, CX2341X_DEC_START_PLAYBACK, 2, 0, 1);
		atomic_inc(&itv->decoding);

		/* Setup capture if not already done */
		if (atomic_read(&itv->capturing) == 0) {
			cx2341x_update(itv, ivtv_api_func, NULL, &itv->params);
		}

		/* Start Passthrough Mode */
		ivtv_vapi(itv, CX2341X_ENC_START_CAPTURE, 2, 2, 11);
		atomic_inc(&itv->capturing);
		return 0;
	}

	if (itv->output_mode != OUT_PASSTHROUGH)
		return 0;

	/* Stop Passthrough Mode */
	ivtv_vapi(itv, CX2341X_ENC_STOP_CAPTURE, 3, 1, 2, 11);
	ivtv_vapi(itv, CX2341X_DEC_STOP_PLAYBACK, 3, 1, 0, 0);

	atomic_dec(&itv->capturing);
	atomic_dec(&itv->decoding);
	clear_bit(IVTV_F_S_PASSTHROUGH, &dec_stream->s_flags);
	clear_bit(IVTV_F_S_STREAMING, &dec_stream->s_flags);
	itv->output_mode = OUT_NONE;

	return 0;
}
