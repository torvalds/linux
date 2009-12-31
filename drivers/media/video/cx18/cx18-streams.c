/*
 *  cx18 init/start/stop/exit stream functions
 *
 *  Derived from ivtv-streams.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@radix.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include "cx18-driver.h"
#include "cx18-io.h"
#include "cx18-fileops.h"
#include "cx18-mailbox.h"
#include "cx18-i2c.h"
#include "cx18-queue.h"
#include "cx18-ioctl.h"
#include "cx18-streams.h"
#include "cx18-cards.h"
#include "cx18-scb.h"
#include "cx18-dvb.h"

#define CX18_DSP0_INTERRUPT_MASK     	0xd0004C

static struct v4l2_file_operations cx18_v4l2_enc_fops = {
	.owner = THIS_MODULE,
	.read = cx18_v4l2_read,
	.open = cx18_v4l2_open,
	/* FIXME change to video_ioctl2 if serialization lock can be removed */
	.ioctl = cx18_v4l2_ioctl,
	.release = cx18_v4l2_close,
	.poll = cx18_v4l2_enc_poll,
};

/* offset from 0 to register ts v4l2 minors on */
#define CX18_V4L2_ENC_TS_OFFSET   16
/* offset from 0 to register pcm v4l2 minors on */
#define CX18_V4L2_ENC_PCM_OFFSET  24
/* offset from 0 to register yuv v4l2 minors on */
#define CX18_V4L2_ENC_YUV_OFFSET  32

static struct {
	const char *name;
	int vfl_type;
	int num_offset;
	int dma;
	enum v4l2_buf_type buf_type;
} cx18_stream_info[] = {
	{	/* CX18_ENC_STREAM_TYPE_MPG */
		"encoder MPEG",
		VFL_TYPE_GRABBER, 0,
		PCI_DMA_FROMDEVICE, V4L2_BUF_TYPE_VIDEO_CAPTURE,
	},
	{	/* CX18_ENC_STREAM_TYPE_TS */
		"TS",
		VFL_TYPE_GRABBER, -1,
		PCI_DMA_FROMDEVICE, V4L2_BUF_TYPE_VIDEO_CAPTURE,
	},
	{	/* CX18_ENC_STREAM_TYPE_YUV */
		"encoder YUV",
		VFL_TYPE_GRABBER, CX18_V4L2_ENC_YUV_OFFSET,
		PCI_DMA_FROMDEVICE, V4L2_BUF_TYPE_VIDEO_CAPTURE,
	},
	{	/* CX18_ENC_STREAM_TYPE_VBI */
		"encoder VBI",
		VFL_TYPE_VBI, 0,
		PCI_DMA_FROMDEVICE, V4L2_BUF_TYPE_VBI_CAPTURE,
	},
	{	/* CX18_ENC_STREAM_TYPE_PCM */
		"encoder PCM audio",
		VFL_TYPE_GRABBER, CX18_V4L2_ENC_PCM_OFFSET,
		PCI_DMA_FROMDEVICE, V4L2_BUF_TYPE_PRIVATE,
	},
	{	/* CX18_ENC_STREAM_TYPE_IDX */
		"encoder IDX",
		VFL_TYPE_GRABBER, -1,
		PCI_DMA_FROMDEVICE, V4L2_BUF_TYPE_VIDEO_CAPTURE,
	},
	{	/* CX18_ENC_STREAM_TYPE_RAD */
		"encoder radio",
		VFL_TYPE_RADIO, 0,
		PCI_DMA_NONE, V4L2_BUF_TYPE_PRIVATE,
	},
};

static void cx18_stream_init(struct cx18 *cx, int type)
{
	struct cx18_stream *s = &cx->streams[type];
	struct video_device *video_dev = s->video_dev;

	/* we need to keep video_dev, so restore it afterwards */
	memset(s, 0, sizeof(*s));
	s->video_dev = video_dev;

	/* initialize cx18_stream fields */
	s->cx = cx;
	s->type = type;
	s->name = cx18_stream_info[type].name;
	s->handle = CX18_INVALID_TASK_HANDLE;

	s->dma = cx18_stream_info[type].dma;
	s->buffers = cx->stream_buffers[type];
	s->buf_size = cx->stream_buf_size[type];
	INIT_LIST_HEAD(&s->buf_pool);
	s->bufs_per_mdl = 1;
	s->mdl_size = s->buf_size * s->bufs_per_mdl;

	init_waitqueue_head(&s->waitq);
	s->id = -1;
	spin_lock_init(&s->q_free.lock);
	cx18_queue_init(&s->q_free);
	spin_lock_init(&s->q_busy.lock);
	cx18_queue_init(&s->q_busy);
	spin_lock_init(&s->q_full.lock);
	cx18_queue_init(&s->q_full);
	spin_lock_init(&s->q_idle.lock);
	cx18_queue_init(&s->q_idle);

	INIT_WORK(&s->out_work_order, cx18_out_work_handler);
}

static int cx18_prep_dev(struct cx18 *cx, int type)
{
	struct cx18_stream *s = &cx->streams[type];
	u32 cap = cx->v4l2_cap;
	int num_offset = cx18_stream_info[type].num_offset;
	int num = cx->instance + cx18_first_minor + num_offset;

	/* These four fields are always initialized. If video_dev == NULL, then
	   this stream is not in use. In that case no other fields but these
	   four can be used. */
	s->video_dev = NULL;
	s->cx = cx;
	s->type = type;
	s->name = cx18_stream_info[type].name;

	/* Check whether the radio is supported */
	if (type == CX18_ENC_STREAM_TYPE_RAD && !(cap & V4L2_CAP_RADIO))
		return 0;

	/* Check whether VBI is supported */
	if (type == CX18_ENC_STREAM_TYPE_VBI &&
	    !(cap & (V4L2_CAP_VBI_CAPTURE | V4L2_CAP_SLICED_VBI_CAPTURE)))
		return 0;

	/* User explicitly selected 0 buffers for these streams, so don't
	   create them. */
	if (cx18_stream_info[type].dma != PCI_DMA_NONE &&
	    cx->stream_buffers[type] == 0) {
		CX18_INFO("Disabled %s device\n", cx18_stream_info[type].name);
		return 0;
	}

	cx18_stream_init(cx, type);

	if (num_offset == -1)
		return 0;

	/* allocate and initialize the v4l2 video device structure */
	s->video_dev = video_device_alloc();
	if (s->video_dev == NULL) {
		CX18_ERR("Couldn't allocate v4l2 video_device for %s\n",
				s->name);
		return -ENOMEM;
	}

	snprintf(s->video_dev->name, sizeof(s->video_dev->name), "%s %s",
		 cx->v4l2_dev.name, s->name);

	s->video_dev->num = num;
	s->video_dev->v4l2_dev = &cx->v4l2_dev;
	s->video_dev->fops = &cx18_v4l2_enc_fops;
	s->video_dev->release = video_device_release;
	s->video_dev->tvnorms = V4L2_STD_ALL;
	cx18_set_funcs(s->video_dev);
	return 0;
}

/* Initialize v4l2 variables and register v4l2 devices */
int cx18_streams_setup(struct cx18 *cx)
{
	int type, ret;

	/* Setup V4L2 Devices */
	for (type = 0; type < CX18_MAX_STREAMS; type++) {
		/* Prepare device */
		ret = cx18_prep_dev(cx, type);
		if (ret < 0)
			break;

		/* Allocate Stream */
		ret = cx18_stream_alloc(&cx->streams[type]);
		if (ret < 0)
			break;
	}
	if (type == CX18_MAX_STREAMS)
		return 0;

	/* One or more streams could not be initialized. Clean 'em all up. */
	cx18_streams_cleanup(cx, 0);
	return ret;
}

static int cx18_reg_dev(struct cx18 *cx, int type)
{
	struct cx18_stream *s = &cx->streams[type];
	int vfl_type = cx18_stream_info[type].vfl_type;
	const char *name;
	int num, ret;

	/* TODO: Shouldn't this be a VFL_TYPE_TRANSPORT or something?
	 * We need a VFL_TYPE_TS defined.
	 */
	if (strcmp("TS", s->name) == 0) {
		/* just return if no DVB is supported */
		if ((cx->card->hw_all & CX18_HW_DVB) == 0)
			return 0;
		ret = cx18_dvb_register(s);
		if (ret < 0) {
			CX18_ERR("DVB failed to register\n");
			return ret;
		}
	}

	if (s->video_dev == NULL)
		return 0;

	num = s->video_dev->num;
	/* card number + user defined offset + device offset */
	if (type != CX18_ENC_STREAM_TYPE_MPG) {
		struct cx18_stream *s_mpg = &cx->streams[CX18_ENC_STREAM_TYPE_MPG];

		if (s_mpg->video_dev)
			num = s_mpg->video_dev->num
			    + cx18_stream_info[type].num_offset;
	}
	video_set_drvdata(s->video_dev, s);

	/* Register device. First try the desired minor, then any free one. */
	ret = video_register_device_no_warn(s->video_dev, vfl_type, num);
	if (ret < 0) {
		CX18_ERR("Couldn't register v4l2 device for %s (device node number %d)\n",
			s->name, num);
		video_device_release(s->video_dev);
		s->video_dev = NULL;
		return ret;
	}

	name = video_device_node_name(s->video_dev);

	switch (vfl_type) {
	case VFL_TYPE_GRABBER:
		CX18_INFO("Registered device %s for %s (%d x %d.%02d kB)\n",
			  name, s->name, cx->stream_buffers[type],
			  cx->stream_buf_size[type] / 1024,
			  (cx->stream_buf_size[type] * 100 / 1024) % 100);
		break;

	case VFL_TYPE_RADIO:
		CX18_INFO("Registered device %s for %s\n", name, s->name);
		break;

	case VFL_TYPE_VBI:
		if (cx->stream_buffers[type])
			CX18_INFO("Registered device %s for %s "
				  "(%d x %d bytes)\n",
				  name, s->name, cx->stream_buffers[type],
				  cx->stream_buf_size[type]);
		else
			CX18_INFO("Registered device %s for %s\n",
				name, s->name);
		break;
	}

	return 0;
}

/* Register v4l2 devices */
int cx18_streams_register(struct cx18 *cx)
{
	int type;
	int err;
	int ret = 0;

	/* Register V4L2 devices */
	for (type = 0; type < CX18_MAX_STREAMS; type++) {
		err = cx18_reg_dev(cx, type);
		if (err && ret == 0)
			ret = err;
	}

	if (ret == 0)
		return 0;

	/* One or more streams could not be initialized. Clean 'em all up. */
	cx18_streams_cleanup(cx, 1);
	return ret;
}

/* Unregister v4l2 devices */
void cx18_streams_cleanup(struct cx18 *cx, int unregister)
{
	struct video_device *vdev;
	int type;

	/* Teardown all streams */
	for (type = 0; type < CX18_MAX_STREAMS; type++) {

		/* No struct video_device, but can have buffers allocated */
		if (type == CX18_ENC_STREAM_TYPE_TS) {
			if (cx->streams[type].dvb.enabled) {
				cx18_dvb_unregister(&cx->streams[type]);
				cx->streams[type].dvb.enabled = false;
				cx18_stream_free(&cx->streams[type]);
			}
			continue;
		}

		/* No struct video_device, but can have buffers allocated */
		if (type == CX18_ENC_STREAM_TYPE_IDX) {
			if (cx->stream_buffers[type] != 0) {
				cx->stream_buffers[type] = 0;
				cx18_stream_free(&cx->streams[type]);
			}
			continue;
		}

		/* If struct video_device exists, can have buffers allocated */
		vdev = cx->streams[type].video_dev;

		cx->streams[type].video_dev = NULL;
		if (vdev == NULL)
			continue;

		cx18_stream_free(&cx->streams[type]);

		/* Unregister or release device */
		if (unregister)
			video_unregister_device(vdev);
		else
			video_device_release(vdev);
	}
}

static inline bool cx18_stream_enabled(struct cx18_stream *s)
{
	return s->video_dev || s->dvb.enabled;
}

static void cx18_vbi_setup(struct cx18_stream *s)
{
	struct cx18 *cx = s->cx;
	int raw = cx18_raw_vbi(cx);
	u32 data[CX2341X_MBOX_MAX_DATA];
	int lines;

	if (cx->is_60hz) {
		cx->vbi.count = 12;
		cx->vbi.start[0] = 10;
		cx->vbi.start[1] = 273;
	} else {        /* PAL/SECAM */
		cx->vbi.count = 18;
		cx->vbi.start[0] = 6;
		cx->vbi.start[1] = 318;
	}

	/* setup VBI registers */
	v4l2_subdev_call(cx->sd_av, video, s_fmt, &cx->vbi.in);

	/*
	 * Send the CX18_CPU_SET_RAW_VBI_PARAM API command to setup Encoder Raw
	 * VBI when the first analog capture channel starts, as once it starts
	 * (e.g. MPEG), we can't effect any change in the Encoder Raw VBI setup
	 * (i.e. for the VBI capture channels).  We also send it for each
	 * analog capture channel anyway just to make sure we get the proper
	 * behavior
	 */
	if (raw) {
		lines = cx->vbi.count * 2;
	} else {
		/*
		 * For 525/60 systems, according to the VIP 2 & BT.656 std:
		 * The EAV RP code's Field bit toggles on line 4, a few lines
		 * after the Vertcal Blank bit has already toggled.
		 * Tell the encoder to capture 21-4+1=18 lines per field,
		 * since we want lines 10 through 21.
		 *
		 * For 625/50 systems, according to the VIP 2 & BT.656 std:
		 * The EAV RP code's Field bit toggles on line 1, a few lines
		 * after the Vertcal Blank bit has already toggled.
		 * (We've actually set the digitizer so that the Field bit
		 * toggles on line 2.) Tell the encoder to capture 23-2+1=22
		 * lines per field, since we want lines 6 through 23.
		 */
		lines = cx->is_60hz ? (21 - 4 + 1) * 2 : (23 - 2 + 1) * 2;
	}

	data[0] = s->handle;
	/* Lines per field */
	data[1] = (lines / 2) | ((lines / 2) << 16);
	/* bytes per line */
	data[2] = (raw ? vbi_active_samples
		       : (cx->is_60hz ? vbi_hblank_samples_60Hz
				      : vbi_hblank_samples_50Hz));
	/* Every X number of frames a VBI interrupt arrives
	   (frames as in 25 or 30 fps) */
	data[3] = 1;
	/*
	 * Set the SAV/EAV RP codes to look for as start/stop points
	 * when in VIP-1.1 mode
	 */
	if (raw) {
		/*
		 * Start codes for beginning of "active" line in vertical blank
		 * 0x20 (               VerticalBlank                )
		 * 0x60 (     EvenField VerticalBlank                )
		 */
		data[4] = 0x20602060;
		/*
		 * End codes for end of "active" raw lines and regular lines
		 * 0x30 (               VerticalBlank HorizontalBlank)
		 * 0x70 (     EvenField VerticalBlank HorizontalBlank)
		 * 0x90 (Task                         HorizontalBlank)
		 * 0xd0 (Task EvenField               HorizontalBlank)
		 */
		data[5] = 0x307090d0;
	} else {
		/*
		 * End codes for active video, we want data in the hblank region
		 * 0xb0 (Task         0 VerticalBlank HorizontalBlank)
		 * 0xf0 (Task EvenField VerticalBlank HorizontalBlank)
		 *
		 * Since the V bit is only allowed to toggle in the EAV RP code,
		 * just before the first active region line, these two
		 * are problematic:
		 * 0x90 (Task                         HorizontalBlank)
		 * 0xd0 (Task EvenField               HorizontalBlank)
		 *
		 * We have set the digitzer such that we don't have to worry
		 * about these problem codes.
		 */
		data[4] = 0xB0F0B0F0;
		/*
		 * Start codes for beginning of active line in vertical blank
		 * 0xa0 (Task           VerticalBlank                )
		 * 0xe0 (Task EvenField VerticalBlank                )
		 */
		data[5] = 0xA0E0A0E0;
	}

	CX18_DEBUG_INFO("Setup VBI h: %d lines %x bpl %d fr %d %x %x\n",
			data[0], data[1], data[2], data[3], data[4], data[5]);

	cx18_api(cx, CX18_CPU_SET_RAW_VBI_PARAM, 6, data);
}

static
struct cx18_queue *_cx18_stream_put_mdl_fw(struct cx18_stream *s,
					   struct cx18_mdl *mdl)
{
	struct cx18 *cx = s->cx;
	struct cx18_queue *q;

	/* Don't give it to the firmware, if we're not running a capture */
	if (s->handle == CX18_INVALID_TASK_HANDLE ||
	    test_bit(CX18_F_S_STOPPING, &s->s_flags) ||
	    !test_bit(CX18_F_S_STREAMING, &s->s_flags))
		return cx18_enqueue(s, mdl, &s->q_free);

	q = cx18_enqueue(s, mdl, &s->q_busy);
	if (q != &s->q_busy)
		return q; /* The firmware has the max MDLs it can handle */

	cx18_mdl_sync_for_device(s, mdl);
	cx18_vapi(cx, CX18_CPU_DE_SET_MDL, 5, s->handle,
		  (void __iomem *) &cx->scb->cpu_mdl[mdl->id] - cx->enc_mem,
		  s->bufs_per_mdl, mdl->id, s->mdl_size);
	return q;
}

static
void _cx18_stream_load_fw_queue(struct cx18_stream *s)
{
	struct cx18_queue *q;
	struct cx18_mdl *mdl;

	if (atomic_read(&s->q_free.depth) == 0 ||
	    atomic_read(&s->q_busy.depth) >= CX18_MAX_FW_MDLS_PER_STREAM)
		return;

	/* Move from q_free to q_busy notifying the firmware, until the limit */
	do {
		mdl = cx18_dequeue(s, &s->q_free);
		if (mdl == NULL)
			break;
		q = _cx18_stream_put_mdl_fw(s, mdl);
	} while (atomic_read(&s->q_busy.depth) < CX18_MAX_FW_MDLS_PER_STREAM
		 && q == &s->q_busy);
}

void cx18_out_work_handler(struct work_struct *work)
{
	struct cx18_stream *s =
			 container_of(work, struct cx18_stream, out_work_order);

	_cx18_stream_load_fw_queue(s);
}

static void cx18_stream_configure_mdls(struct cx18_stream *s)
{
	cx18_unload_queues(s);

	switch (s->type) {
	case CX18_ENC_STREAM_TYPE_YUV:
		/*
		 * Height should be a multiple of 32 lines.
		 * Set the MDL size to the exact size needed for one frame.
		 * Use enough buffers per MDL to cover the MDL size
		 */
		s->mdl_size = 720 * s->cx->params.height * 3 / 2;
		s->bufs_per_mdl = s->mdl_size / s->buf_size;
		if (s->mdl_size % s->buf_size)
			s->bufs_per_mdl++;
		break;
	case CX18_ENC_STREAM_TYPE_VBI:
		s->bufs_per_mdl = 1;
		if  (cx18_raw_vbi(s->cx)) {
			s->mdl_size = (s->cx->is_60hz ? 12 : 18)
						       * 2 * vbi_active_samples;
		} else {
			/*
			 * See comment in cx18_vbi_setup() below about the
			 * extra lines we capture in sliced VBI mode due to
			 * the lines on which EAV RP codes toggle.
			*/
			s->mdl_size = s->cx->is_60hz
				   ? (21 - 4 + 1) * 2 * vbi_hblank_samples_60Hz
				   : (23 - 2 + 1) * 2 * vbi_hblank_samples_50Hz;
		}
		break;
	default:
		s->bufs_per_mdl = 1;
		s->mdl_size = s->buf_size * s->bufs_per_mdl;
		break;
	}

	cx18_load_queues(s);
}

int cx18_start_v4l2_encode_stream(struct cx18_stream *s)
{
	u32 data[MAX_MB_ARGUMENTS];
	struct cx18 *cx = s->cx;
	int captype = 0;
	struct cx18_api_func_private priv;

	if (!cx18_stream_enabled(s))
		return -EINVAL;

	CX18_DEBUG_INFO("Start encoder stream %s\n", s->name);

	switch (s->type) {
	case CX18_ENC_STREAM_TYPE_MPG:
		captype = CAPTURE_CHANNEL_TYPE_MPEG;
		cx->mpg_data_received = cx->vbi_data_inserted = 0;
		cx->dualwatch_jiffies = jiffies;
		cx->dualwatch_stereo_mode = cx->params.audio_properties & 0x300;
		cx->search_pack_header = 0;
		break;

	case CX18_ENC_STREAM_TYPE_TS:
		captype = CAPTURE_CHANNEL_TYPE_TS;
		break;
	case CX18_ENC_STREAM_TYPE_YUV:
		captype = CAPTURE_CHANNEL_TYPE_YUV;
		break;
	case CX18_ENC_STREAM_TYPE_PCM:
		captype = CAPTURE_CHANNEL_TYPE_PCM;
		break;
	case CX18_ENC_STREAM_TYPE_VBI:
#ifdef CX18_ENCODER_PARSES_SLICED
		captype = cx18_raw_vbi(cx) ?
		     CAPTURE_CHANNEL_TYPE_VBI : CAPTURE_CHANNEL_TYPE_SLICED_VBI;
#else
		/*
		 * Currently we set things up so that Sliced VBI from the
		 * digitizer is handled as Raw VBI by the encoder
		 */
		captype = CAPTURE_CHANNEL_TYPE_VBI;
#endif
		cx->vbi.frame = 0;
		cx->vbi.inserted_frame = 0;
		memset(cx->vbi.sliced_mpeg_size,
			0, sizeof(cx->vbi.sliced_mpeg_size));
		break;
	default:
		return -EINVAL;
	}

	/* Clear Streamoff flags in case left from last capture */
	clear_bit(CX18_F_S_STREAMOFF, &s->s_flags);

	cx18_vapi_result(cx, data, CX18_CREATE_TASK, 1, CPU_CMD_MASK_CAPTURE);
	s->handle = data[0];
	cx18_vapi(cx, CX18_CPU_SET_CHANNEL_TYPE, 2, s->handle, captype);

	/*
	 * For everything but CAPTURE_CHANNEL_TYPE_TS, play it safe and
	 * set up all the parameters, as it is not obvious which parameters the
	 * firmware shares across capture channel types and which it does not.
	 *
	 * Some of the cx18_vapi() calls below apply to only certain capture
	 * channel types.  We're hoping there's no harm in calling most of them
	 * anyway, as long as the values are all consistent.  Setting some
	 * shared parameters will have no effect once an analog capture channel
	 * has started streaming.
	 */
	if (captype != CAPTURE_CHANNEL_TYPE_TS) {
		cx18_vapi(cx, CX18_CPU_SET_VER_CROP_LINE, 2, s->handle, 0);
		cx18_vapi(cx, CX18_CPU_SET_MISC_PARAMETERS, 3, s->handle, 3, 1);
		cx18_vapi(cx, CX18_CPU_SET_MISC_PARAMETERS, 3, s->handle, 8, 0);
		cx18_vapi(cx, CX18_CPU_SET_MISC_PARAMETERS, 3, s->handle, 4, 1);

		/*
		 * Audio related reset according to
		 * Documentation/video4linux/cx2341x/fw-encoder-api.txt
		 */
		if (atomic_read(&cx->ana_capturing) == 0)
			cx18_vapi(cx, CX18_CPU_SET_MISC_PARAMETERS, 2,
				  s->handle, 12);

		/*
		 * Number of lines for Field 1 & Field 2 according to
		 * Documentation/video4linux/cx2341x/fw-encoder-api.txt
		 * Field 1 is 312 for 625 line systems in BT.656
		 * Field 2 is 313 for 625 line systems in BT.656
		 */
		cx18_vapi(cx, CX18_CPU_SET_CAPTURE_LINE_NO, 3,
			  s->handle, 312, 313);

		if (cx->v4l2_cap & V4L2_CAP_VBI_CAPTURE)
			cx18_vbi_setup(s);

		/*
		 * assign program index info.
		 * Mask 7: select I/P/B, Num_req: 400 max
		 * FIXME - currently we have this hardcoded as disabled
		 */
		cx18_vapi_result(cx, data, CX18_CPU_SET_INDEXTABLE, 1, 0);

		/* Call out to the common CX2341x API setup for user controls */
		priv.cx = cx;
		priv.s = s;
		cx2341x_update(&priv, cx18_api_func, NULL, &cx->params);

		/*
		 * When starting a capture and we're set for radio,
		 * ensure the video is muted, despite the user control.
		 */
		if (!cx->params.video_mute &&
		    test_bit(CX18_F_I_RADIO_USER, &cx->i_flags))
			cx18_vapi(cx, CX18_CPU_SET_VIDEO_MUTE, 2, s->handle,
				  (cx->params.video_mute_yuv << 8) | 1);
	}

	if (atomic_read(&cx->tot_capturing) == 0) {
		clear_bit(CX18_F_I_EOS, &cx->i_flags);
		cx18_write_reg(cx, 7, CX18_DSP0_INTERRUPT_MASK);
	}

	cx18_vapi(cx, CX18_CPU_DE_SET_MDL_ACK, 3, s->handle,
		(void __iomem *)&cx->scb->cpu_mdl_ack[s->type][0] - cx->enc_mem,
		(void __iomem *)&cx->scb->cpu_mdl_ack[s->type][1] - cx->enc_mem);

	/* Init all the cpu_mdls for this stream */
	cx18_stream_configure_mdls(s);
	_cx18_stream_load_fw_queue(s);

	/* begin_capture */
	if (cx18_vapi(cx, CX18_CPU_CAPTURE_START, 1, s->handle)) {
		CX18_DEBUG_WARN("Error starting capture!\n");
		/* Ensure we're really not capturing before releasing MDLs */
		set_bit(CX18_F_S_STOPPING, &s->s_flags);
		if (s->type == CX18_ENC_STREAM_TYPE_MPG)
			cx18_vapi(cx, CX18_CPU_CAPTURE_STOP, 2, s->handle, 1);
		else
			cx18_vapi(cx, CX18_CPU_CAPTURE_STOP, 1, s->handle);
		clear_bit(CX18_F_S_STREAMING, &s->s_flags);
		/* FIXME - CX18_F_S_STREAMOFF as well? */
		cx18_vapi(cx, CX18_CPU_DE_RELEASE_MDL, 1, s->handle);
		cx18_vapi(cx, CX18_DESTROY_TASK, 1, s->handle);
		s->handle = CX18_INVALID_TASK_HANDLE;
		clear_bit(CX18_F_S_STOPPING, &s->s_flags);
		if (atomic_read(&cx->tot_capturing) == 0) {
			set_bit(CX18_F_I_EOS, &cx->i_flags);
			cx18_write_reg(cx, 5, CX18_DSP0_INTERRUPT_MASK);
		}
		return -EINVAL;
	}

	/* you're live! sit back and await interrupts :) */
	if (captype != CAPTURE_CHANNEL_TYPE_TS)
		atomic_inc(&cx->ana_capturing);
	atomic_inc(&cx->tot_capturing);
	return 0;
}

void cx18_stop_all_captures(struct cx18 *cx)
{
	int i;

	for (i = CX18_MAX_STREAMS - 1; i >= 0; i--) {
		struct cx18_stream *s = &cx->streams[i];

		if (!cx18_stream_enabled(s))
			continue;
		if (test_bit(CX18_F_S_STREAMING, &s->s_flags))
			cx18_stop_v4l2_encode_stream(s, 0);
	}
}

int cx18_stop_v4l2_encode_stream(struct cx18_stream *s, int gop_end)
{
	struct cx18 *cx = s->cx;
	unsigned long then;

	if (!cx18_stream_enabled(s))
		return -EINVAL;

	/* This function assumes that you are allowed to stop the capture
	   and that we are actually capturing */

	CX18_DEBUG_INFO("Stop Capture\n");

	if (atomic_read(&cx->tot_capturing) == 0)
		return 0;

	set_bit(CX18_F_S_STOPPING, &s->s_flags);
	if (s->type == CX18_ENC_STREAM_TYPE_MPG)
		cx18_vapi(cx, CX18_CPU_CAPTURE_STOP, 2, s->handle, !gop_end);
	else
		cx18_vapi(cx, CX18_CPU_CAPTURE_STOP, 1, s->handle);

	then = jiffies;

	if (s->type == CX18_ENC_STREAM_TYPE_MPG && gop_end) {
		CX18_INFO("ignoring gop_end: not (yet?) supported by the firmware\n");
	}

	if (s->type != CX18_ENC_STREAM_TYPE_TS)
		atomic_dec(&cx->ana_capturing);
	atomic_dec(&cx->tot_capturing);

	/* Clear capture and no-read bits */
	clear_bit(CX18_F_S_STREAMING, &s->s_flags);

	/* Tell the CX23418 it can't use our buffers anymore */
	cx18_vapi(cx, CX18_CPU_DE_RELEASE_MDL, 1, s->handle);

	cx18_vapi(cx, CX18_DESTROY_TASK, 1, s->handle);
	s->handle = CX18_INVALID_TASK_HANDLE;
	clear_bit(CX18_F_S_STOPPING, &s->s_flags);

	if (atomic_read(&cx->tot_capturing) > 0)
		return 0;

	cx18_write_reg(cx, 5, CX18_DSP0_INTERRUPT_MASK);
	wake_up(&s->waitq);

	return 0;
}

u32 cx18_find_handle(struct cx18 *cx)
{
	int i;

	/* find first available handle to be used for global settings */
	for (i = 0; i < CX18_MAX_STREAMS; i++) {
		struct cx18_stream *s = &cx->streams[i];

		if (s->video_dev && (s->handle != CX18_INVALID_TASK_HANDLE))
			return s->handle;
	}
	return CX18_INVALID_TASK_HANDLE;
}

struct cx18_stream *cx18_handle_to_stream(struct cx18 *cx, u32 handle)
{
	int i;
	struct cx18_stream *s;

	if (handle == CX18_INVALID_TASK_HANDLE)
		return NULL;

	for (i = 0; i < CX18_MAX_STREAMS; i++) {
		s = &cx->streams[i];
		if (s->handle != handle)
			continue;
		if (cx18_stream_enabled(s))
			return s;
	}
	return NULL;
}
