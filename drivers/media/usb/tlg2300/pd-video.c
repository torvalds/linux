#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/videodev2.h>
#include <linux/usb.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <media/v4l2-ioctl.h>
#include <media/v4l2-dev.h>

#include "pd-common.h"
#include "vendorcmds.h"

#ifdef CONFIG_PM
static int pm_video_suspend(struct poseidon *pd);
static int pm_video_resume(struct poseidon *pd);
#endif
static void iso_bubble_handler(struct work_struct *w);

static int usb_transfer_mode;
module_param(usb_transfer_mode, int, 0644);
MODULE_PARM_DESC(usb_transfer_mode, "0 = Bulk, 1 = Isochronous");

static const struct poseidon_format poseidon_formats[] = {
	{ "YUV 422", V4L2_PIX_FMT_YUYV, 16, 0},
	{ "RGB565", V4L2_PIX_FMT_RGB565, 16, 0},
};

static const struct poseidon_tvnorm poseidon_tvnorms[] = {
	{ V4L2_STD_PAL_D, "PAL-D",  TLG_TUNE_VSTD_PAL_D },
	{ V4L2_STD_PAL_B, "PAL-B",  TLG_TUNE_VSTD_PAL_B },
	{ V4L2_STD_PAL_G, "PAL-G",  TLG_TUNE_VSTD_PAL_G },
	{ V4L2_STD_PAL_H, "PAL-H",  TLG_TUNE_VSTD_PAL_H },
	{ V4L2_STD_PAL_I, "PAL-I",  TLG_TUNE_VSTD_PAL_I },
	{ V4L2_STD_PAL_M, "PAL-M",  TLG_TUNE_VSTD_PAL_M },
	{ V4L2_STD_PAL_N, "PAL-N",  TLG_TUNE_VSTD_PAL_N_COMBO },
	{ V4L2_STD_PAL_Nc, "PAL-Nc", TLG_TUNE_VSTD_PAL_N_COMBO },
	{ V4L2_STD_NTSC_M, "NTSC-M", TLG_TUNE_VSTD_NTSC_M },
	{ V4L2_STD_NTSC_M_JP, "NTSC-JP", TLG_TUNE_VSTD_NTSC_M_J },
	{ V4L2_STD_SECAM_B, "SECAM-B", TLG_TUNE_VSTD_SECAM_B },
	{ V4L2_STD_SECAM_D, "SECAM-D", TLG_TUNE_VSTD_SECAM_D },
	{ V4L2_STD_SECAM_G, "SECAM-G", TLG_TUNE_VSTD_SECAM_G },
	{ V4L2_STD_SECAM_H, "SECAM-H", TLG_TUNE_VSTD_SECAM_H },
	{ V4L2_STD_SECAM_K, "SECAM-K", TLG_TUNE_VSTD_SECAM_K },
	{ V4L2_STD_SECAM_K1, "SECAM-K1", TLG_TUNE_VSTD_SECAM_K1 },
	{ V4L2_STD_SECAM_L, "SECAM-L", TLG_TUNE_VSTD_SECAM_L },
	{ V4L2_STD_SECAM_LC, "SECAM-LC", TLG_TUNE_VSTD_SECAM_L1 },
};
static const unsigned int POSEIDON_TVNORMS = ARRAY_SIZE(poseidon_tvnorms);

struct pd_audio_mode {
	u32 tlg_audio_mode;
	u32 v4l2_audio_sub;
	u32 v4l2_audio_mode;
};

static const struct pd_audio_mode pd_audio_modes[] = {
	{ TLG_TUNE_TVAUDIO_MODE_MONO, V4L2_TUNER_SUB_MONO,
		V4L2_TUNER_MODE_MONO },
	{ TLG_TUNE_TVAUDIO_MODE_STEREO, V4L2_TUNER_SUB_STEREO,
		V4L2_TUNER_MODE_STEREO },
	{ TLG_TUNE_TVAUDIO_MODE_LANG_A, V4L2_TUNER_SUB_LANG1,
		V4L2_TUNER_MODE_LANG1 },
	{ TLG_TUNE_TVAUDIO_MODE_LANG_B, V4L2_TUNER_SUB_LANG2,
		V4L2_TUNER_MODE_LANG2 },
	{ TLG_TUNE_TVAUDIO_MODE_LANG_C, V4L2_TUNER_SUB_LANG1,
		V4L2_TUNER_MODE_LANG1_LANG2 }
};
static const unsigned int POSEIDON_AUDIOMODS = ARRAY_SIZE(pd_audio_modes);

struct pd_input {
	char *name;
	uint32_t tlg_src;
};

static const struct pd_input pd_inputs[] = {
	{ "TV Antenna", TLG_SIG_SRC_ANTENNA },
	{ "TV Cable", TLG_SIG_SRC_CABLE },
	{ "TV SVideo", TLG_SIG_SRC_SVIDEO },
	{ "TV Composite", TLG_SIG_SRC_COMPOSITE }
};
static const unsigned int POSEIDON_INPUTS = ARRAY_SIZE(pd_inputs);

struct poseidon_control {
	struct v4l2_queryctrl v4l2_ctrl;
	enum cmd_custom_param_id vc_id;
};

static struct poseidon_control controls[] = {
	{
		{ V4L2_CID_BRIGHTNESS, V4L2_CTRL_TYPE_INTEGER,
			"brightness", 0, 10000, 1, 100, 0, },
		CUST_PARM_ID_BRIGHTNESS_CTRL
	}, {
		{ V4L2_CID_CONTRAST, V4L2_CTRL_TYPE_INTEGER,
			"contrast", 0, 10000, 1, 100, 0, },
		CUST_PARM_ID_CONTRAST_CTRL,
	}, {
		{ V4L2_CID_HUE, V4L2_CTRL_TYPE_INTEGER,
			"hue", 0, 10000, 1, 100, 0, },
		CUST_PARM_ID_HUE_CTRL,
	}, {
		{ V4L2_CID_SATURATION, V4L2_CTRL_TYPE_INTEGER,
			"saturation", 0, 10000, 1, 100, 0, },
		CUST_PARM_ID_SATURATION_CTRL,
	},
};

struct video_std_to_audio_std {
	v4l2_std_id	video_std;
	int 		audio_std;
};

static const struct video_std_to_audio_std video_to_audio_map[] = {
	/* country : { 27, 32, 33, 34, 36, 44, 45, 46, 47, 48, 64,
			65, 86, 351, 352, 353, 354, 358, 372, 852, 972 } */
	{ (V4L2_STD_PAL_I | V4L2_STD_PAL_B | V4L2_STD_PAL_D |
		V4L2_STD_SECAM_L | V4L2_STD_SECAM_D), TLG_TUNE_ASTD_NICAM },

	/* country : { 1, 52, 54, 55, 886 } */
	{V4L2_STD_NTSC_M | V4L2_STD_PAL_N | V4L2_STD_PAL_M, TLG_TUNE_ASTD_BTSC},

	/* country : { 81 } */
	{ V4L2_STD_NTSC_M_JP, TLG_TUNE_ASTD_EIAJ },

	/* other country : TLG_TUNE_ASTD_A2 */
};
static const unsigned int map_size = ARRAY_SIZE(video_to_audio_map);

static int get_audio_std(v4l2_std_id v4l2_std)
{
	int i = 0;

	for (; i < map_size; i++) {
		if (v4l2_std & video_to_audio_map[i].video_std)
			return video_to_audio_map[i].audio_std;
	}
	return TLG_TUNE_ASTD_A2;
}

static int vidioc_querycap(struct file *file, void *fh,
			struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct poseidon *p = video_get_drvdata(vdev);
	struct front_face *front = fh;

	logs(front);

	strcpy(cap->driver, "tele-video");
	strcpy(cap->card, "Telegent Poseidon");
	usb_make_path(p->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_TUNER | V4L2_CAP_AUDIO |
			V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
	if (vdev->vfl_type == VFL_TYPE_VBI)
		cap->device_caps |= V4L2_CAP_VBI_CAPTURE;
	else
		cap->device_caps |= V4L2_CAP_VIDEO_CAPTURE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS |
		V4L2_CAP_RADIO | V4L2_CAP_VBI_CAPTURE | V4L2_CAP_VIDEO_CAPTURE;
	return 0;
}

/*====================================================================*/
static void init_copy(struct video_data *video, bool index)
{
	struct front_face *front = video->front;

	video->field_count	= index;
	video->lines_copied	= 0;
	video->prev_left	= 0 ;
	video->dst 		= (char *)videobuf_to_vmalloc(front->curr_frame)
					+ index * video->lines_size;
	video->vbi->copied 	= 0; /* set it here */
}

static bool get_frame(struct front_face *front, int *need_init)
{
	struct videobuf_buffer *vb = front->curr_frame;

	if (vb)
		return true;

	spin_lock(&front->queue_lock);
	if (!list_empty(&front->active)) {
		vb = list_entry(front->active.next,
			       struct videobuf_buffer, queue);
		if (need_init)
			*need_init = 1;
		front->curr_frame = vb;
		list_del_init(&vb->queue);
	}
	spin_unlock(&front->queue_lock);

	return !!vb;
}

/* check if the video's buffer is ready */
static bool get_video_frame(struct front_face *front, struct video_data *video)
{
	int need_init = 0;
	bool ret = true;

	ret = get_frame(front, &need_init);
	if (ret && need_init)
		init_copy(video, 0);
	return ret;
}

static void submit_frame(struct front_face *front)
{
	struct videobuf_buffer *vb = front->curr_frame;

	if (vb == NULL)
		return;

	front->curr_frame	= NULL;
	vb->state		= VIDEOBUF_DONE;
	vb->field_count++;
	v4l2_get_timestamp(&vb->ts);

	wake_up(&vb->done);
}

/*
 * A frame is composed of two fields. If we receive all the two fields,
 * call the  submit_frame() to submit the whole frame to applications.
 */
static void end_field(struct video_data *video)
{
	/* logs(video->front); */
	if (1 == video->field_count)
		submit_frame(video->front);
	else
		init_copy(video, 1);
}

static void copy_video_data(struct video_data *video, char *src,
				unsigned int count)
{
#define copy_data(len)  \
	do { \
		if (++video->lines_copied > video->lines_per_field) \
			goto overflow; \
		memcpy(video->dst, src, len);\
		video->dst += len + video->lines_size; \
		src += len; \
		count -= len; \
	 } while (0)

	while (count && count >= video->lines_size) {
		if (video->prev_left) {
			copy_data(video->prev_left);
			video->prev_left = 0;
			continue;
		}
		copy_data(video->lines_size);
	}
	if (count && count < video->lines_size) {
		memcpy(video->dst, src, count);

		video->prev_left = video->lines_size - count;
		video->dst += count;
	}
	return;

overflow:
	end_field(video);
}

static void check_trailer(struct video_data *video, char *src, int count)
{
	struct vbi_data *vbi = video->vbi;
	int offset; /* trailer's offset */
	char *buf;

	offset = (video->context.pix.sizeimage / 2 + vbi->vbi_size / 2)
		- (vbi->copied + video->lines_size * video->lines_copied);
	if (video->prev_left)
		offset -= (video->lines_size - video->prev_left);

	if (offset > count || offset <= 0)
		goto short_package;

	buf = src + offset;

	/* trailer : (VFHS) + U32 + U32 + field_num */
	if (!strncmp(buf, "VFHS", 4)) {
		int field_num = *((u32 *)(buf + 12));

		if ((field_num & 1) ^ video->field_count) {
			init_copy(video, video->field_count);
			return;
		}
		copy_video_data(video, src, offset);
	}
short_package:
	end_field(video);
}

/* ==========  Check this more carefully! =========== */
static inline void copy_vbi_data(struct vbi_data *vbi,
				char *src, unsigned int count)
{
	struct front_face *front = vbi->front;

	if (front && get_frame(front, NULL)) {
		char *buf = videobuf_to_vmalloc(front->curr_frame);

		if (vbi->video->field_count)
			buf += (vbi->vbi_size / 2);
		memcpy(buf + vbi->copied, src, count);
	}
	vbi->copied += count;
}

/*
 * Copy the normal data (VBI or VIDEO) without the trailer.
 * VBI is not interlaced, while VIDEO is interlaced.
 */
static inline void copy_vbi_video_data(struct video_data *video,
				char *src, unsigned int count)
{
	struct vbi_data *vbi = video->vbi;
	unsigned int vbi_delta = (vbi->vbi_size / 2) - vbi->copied;

	if (vbi_delta >= count) {
		copy_vbi_data(vbi, src, count);
	} else {
		if (vbi_delta) {
			copy_vbi_data(vbi, src, vbi_delta);

			/* we receive the two fields of the VBI*/
			if (vbi->front && video->field_count)
				submit_frame(vbi->front);
		}
		copy_video_data(video, src + vbi_delta, count - vbi_delta);
	}
}

static void urb_complete_bulk(struct urb *urb)
{
	struct front_face *front = urb->context;
	struct video_data *video = &front->pd->video_data;
	char *src = (char *)urb->transfer_buffer;
	int count = urb->actual_length;
	int ret = 0;

	if (!video->is_streaming || urb->status) {
		if (urb->status == -EPROTO)
			goto resend_it;
		return;
	}
	if (!get_video_frame(front, video))
		goto resend_it;

	if (count == urb->transfer_buffer_length)
		copy_vbi_video_data(video, src, count);
	else
		check_trailer(video, src, count);

resend_it:
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret)
		log(" submit failed: error %d", ret);
}

/************************* for ISO *********************/
#define GET_SUCCESS		(0)
#define GET_TRAILER		(1)
#define GET_TOO_MUCH_BUBBLE	(2)
#define GET_NONE		(3)
static int get_chunk(int start, struct urb *urb,
			int *head, int *tail, int *bubble_err)
{
	struct usb_iso_packet_descriptor *pkt = NULL;
	int ret = GET_SUCCESS;

	for (*head = *tail = -1; start < urb->number_of_packets; start++) {
		pkt = &urb->iso_frame_desc[start];

		/* handle the bubble of the Hub */
		if (-EOVERFLOW == pkt->status) {
			if (++*bubble_err > urb->number_of_packets / 3)
				return GET_TOO_MUCH_BUBBLE;
			continue;
		}

		/* This is the gap */
		if (pkt->status || pkt->actual_length <= 0
				|| pkt->actual_length > ISO_PKT_SIZE) {
			if (*head != -1)
				break;
			continue;
		}

		/* a good isochronous packet */
		if (pkt->actual_length == ISO_PKT_SIZE) {
			if (*head == -1)
				*head = start;
			*tail = start;
			continue;
		}

		/* trailer is here */
		if (pkt->actual_length < ISO_PKT_SIZE) {
			if (*head == -1) {
				*head = start;
				*tail = start;
				return GET_TRAILER;
			}
			break;
		}
	}

	if (*head == -1 && *tail == -1)
		ret = GET_NONE;
	return ret;
}

/*
 * |__|------|___|-----|_______|
 *       ^          ^
 *       |          |
 *      gap        gap
 */
static void urb_complete_iso(struct urb *urb)
{
	struct front_face *front = urb->context;
	struct video_data *video = &front->pd->video_data;
	int bubble_err = 0, head = 0, tail = 0;
	char *src = (char *)urb->transfer_buffer;
	int ret = 0;

	if (!video->is_streaming)
		return;

	do {
		if (!get_video_frame(front, video))
			goto out;

		switch (get_chunk(head, urb, &head, &tail, &bubble_err)) {
		case GET_SUCCESS:
			copy_vbi_video_data(video, src + (head * ISO_PKT_SIZE),
					(tail - head + 1) * ISO_PKT_SIZE);
			break;
		case GET_TRAILER:
			check_trailer(video, src + (head * ISO_PKT_SIZE),
					ISO_PKT_SIZE);
			break;
		case GET_NONE:
			goto out;
		case GET_TOO_MUCH_BUBBLE:
			log("\t We got too much bubble");
			schedule_work(&video->bubble_work);
			return;
		}
	} while (head = tail + 1, head < urb->number_of_packets);

out:
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret)
		log("usb_submit_urb err : %d", ret);
}
/*============================= [  end  ] =====================*/

static int prepare_iso_urb(struct video_data *video)
{
	struct usb_device *udev = video->pd->udev;
	int i;

	if (video->urb_array[0])
		return 0;

	for (i = 0; i < SBUF_NUM; i++) {
		struct urb *urb;
		void *mem;
		int j;

		urb = usb_alloc_urb(PK_PER_URB, GFP_KERNEL);
		if (urb == NULL)
			goto out;

		video->urb_array[i] = urb;
		mem = usb_alloc_coherent(udev,
					 ISO_PKT_SIZE * PK_PER_URB,
					 GFP_KERNEL,
					 &urb->transfer_dma);

		urb->complete	= urb_complete_iso;	/* handler */
		urb->dev	= udev;
		urb->context	= video->front;
		urb->pipe	= usb_rcvisocpipe(udev,
						video->endpoint_addr);
		urb->interval	= 1;
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->number_of_packets	= PK_PER_URB;
		urb->transfer_buffer	= mem;
		urb->transfer_buffer_length = PK_PER_URB * ISO_PKT_SIZE;

		for (j = 0; j < PK_PER_URB; j++) {
			urb->iso_frame_desc[j].offset = ISO_PKT_SIZE * j;
			urb->iso_frame_desc[j].length = ISO_PKT_SIZE;
		}
	}
	return 0;
out:
	for (; i > 0; i--)
		;
	return -ENOMEM;
}

/* return the succeeded number of the allocation */
int alloc_bulk_urbs_generic(struct urb **urb_array, int num,
			struct usb_device *udev, u8 ep_addr,
			int buf_size, gfp_t gfp_flags,
			usb_complete_t complete_fn, void *context)
{
	int i = 0;

	for (; i < num; i++) {
		void *mem;
		struct urb *urb = usb_alloc_urb(0, gfp_flags);
		if (urb == NULL)
			return i;

		mem = usb_alloc_coherent(udev, buf_size, gfp_flags,
					 &urb->transfer_dma);
		if (mem == NULL) {
			usb_free_urb(urb);
			return i;
		}

		usb_fill_bulk_urb(urb, udev, usb_rcvbulkpipe(udev, ep_addr),
				mem, buf_size, complete_fn, context);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		urb_array[i] = urb;
	}
	return i;
}

void free_all_urb_generic(struct urb **urb_array, int num)
{
	int i;
	struct urb *urb;

	for (i = 0; i < num; i++) {
		urb = urb_array[i];
		if (urb) {
			usb_free_coherent(urb->dev,
					urb->transfer_buffer_length,
					urb->transfer_buffer,
					urb->transfer_dma);
			usb_free_urb(urb);
			urb_array[i] = NULL;
		}
	}
}

static int prepare_bulk_urb(struct video_data *video)
{
	if (video->urb_array[0])
		return 0;

	alloc_bulk_urbs_generic(video->urb_array, SBUF_NUM,
			video->pd->udev, video->endpoint_addr,
			0x2000, GFP_KERNEL,
			urb_complete_bulk, video->front);
	return 0;
}

/* free the URBs */
static void free_all_urb(struct video_data *video)
{
	free_all_urb_generic(video->urb_array, SBUF_NUM);
}

static void pd_buf_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	videobuf_vmalloc_free(vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static void pd_buf_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct front_face *front = q->priv_data;
	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &front->active);
}

static int pd_buf_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
			   enum v4l2_field field)
{
	struct front_face *front = q->priv_data;
	int rc;

	switch (front->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (VIDEOBUF_NEEDS_INIT == vb->state) {
			struct v4l2_pix_format *pix;

			pix = &front->pd->video_data.context.pix;
			vb->size	= pix->sizeimage; /* real frame size */
			vb->width	= pix->width;
			vb->height	= pix->height;
			rc = videobuf_iolock(q, vb, NULL);
			if (rc < 0)
				return rc;
		}
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (VIDEOBUF_NEEDS_INIT == vb->state) {
			vb->size	= front->pd->vbi_data.vbi_size;
			rc = videobuf_iolock(q, vb, NULL);
			if (rc < 0)
				return rc;
		}
		break;
	default:
		return -EINVAL;
	}
	vb->field = field;
	vb->state = VIDEOBUF_PREPARED;
	return 0;
}

static int fire_all_urb(struct video_data *video)
{
	int i, ret;

	video->is_streaming = 1;

	for (i = 0; i < SBUF_NUM; i++) {
		ret = usb_submit_urb(video->urb_array[i], GFP_KERNEL);
		if (ret)
			log("(%d) failed: error %d", i, ret);
	}
	return ret;
}

static int start_video_stream(struct poseidon *pd)
{
	struct video_data *video = &pd->video_data;
	s32 cmd_status;

	send_set_req(pd, TAKE_REQUEST, 0, &cmd_status);
	send_set_req(pd, PLAY_SERVICE, TLG_TUNE_PLAY_SVC_START, &cmd_status);

	if (pd->cur_transfer_mode) {
		prepare_iso_urb(video);
		INIT_WORK(&video->bubble_work, iso_bubble_handler);
	} else {
		/* The bulk mode does not need a bubble handler */
		prepare_bulk_urb(video);
	}
	fire_all_urb(video);
	return 0;
}

static int pd_buf_setup(struct videobuf_queue *q, unsigned int *count,
		       unsigned int *size)
{
	struct front_face *front = q->priv_data;
	struct poseidon *pd	= front->pd;

	switch (front->type) {
	default:
		return -EINVAL;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE: {
		struct video_data *video = &pd->video_data;
		struct v4l2_pix_format *pix = &video->context.pix;

		*size = PAGE_ALIGN(pix->sizeimage);/* page aligned frame size */
		if (*count < 4)
			*count = 4;
		if (1) {
			/* same in different altersetting */
			video->endpoint_addr	= 0x82;
			video->vbi		= &pd->vbi_data;
			video->vbi->video	= video;
			video->pd		= pd;
			video->lines_per_field	= pix->height / 2;
			video->lines_size	= pix->width * 2;
			video->front 		= front;
		}
		return start_video_stream(pd);
	}

	case V4L2_BUF_TYPE_VBI_CAPTURE: {
		struct vbi_data *vbi = &pd->vbi_data;

		*size = PAGE_ALIGN(vbi->vbi_size);
		log("size : %d", *size);
		if (*count == 0)
			*count = 4;
	}
		break;
	}
	return 0;
}

static struct videobuf_queue_ops pd_video_qops = {
	.buf_setup      = pd_buf_setup,
	.buf_prepare    = pd_buf_prepare,
	.buf_queue      = pd_buf_queue,
	.buf_release    = pd_buf_release,
};

static int vidioc_enum_fmt(struct file *file, void *fh,
				struct v4l2_fmtdesc *f)
{
	if (ARRAY_SIZE(poseidon_formats) <= f->index)
		return -EINVAL;
	f->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f->flags	= 0;
	f->pixelformat	= poseidon_formats[f->index].fourcc;
	strcpy(f->description, poseidon_formats[f->index].name);
	return 0;
}

static int vidioc_g_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct front_face *front = fh;
	struct poseidon *pd = front->pd;

	logs(front);
	f->fmt.pix = pd->video_data.context.pix;
	return 0;
}

static int vidioc_try_fmt(struct file *file, void *fh,
		struct v4l2_format *f)
{
	return 0;
}

/*
 * VLC calls VIDIOC_S_STD before VIDIOC_S_FMT, while
 * Mplayer calls them in the reverse order.
 */
static int pd_vidioc_s_fmt(struct poseidon *pd, struct v4l2_pix_format *pix)
{
	struct video_data *video	= &pd->video_data;
	struct running_context *context = &video->context;
	struct v4l2_pix_format *pix_def	= &context->pix;
	s32 ret = 0, cmd_status = 0, vid_resol;

	/* set the pixel format to firmware */
	if (pix->pixelformat == V4L2_PIX_FMT_RGB565) {
		vid_resol = TLG_TUNER_VID_FORMAT_RGB_565;
	} else {
		pix->pixelformat = V4L2_PIX_FMT_YUYV;
		vid_resol = TLG_TUNER_VID_FORMAT_YUV;
	}
	ret = send_set_req(pd, VIDEO_STREAM_FMT_SEL,
				vid_resol, &cmd_status);

	/* set the resolution to firmware */
	vid_resol = TLG_TUNE_VID_RES_720;
	switch (pix->width) {
	case 704:
		vid_resol = TLG_TUNE_VID_RES_704;
		break;
	default:
		pix->width = 720;
	case 720:
		break;
	}
	ret |= send_set_req(pd, VIDEO_ROSOLU_SEL,
				vid_resol, &cmd_status);
	if (ret || cmd_status)
		return -EBUSY;

	pix_def->pixelformat = pix->pixelformat; /* save it */
	pix->height = (context->tvnormid & V4L2_STD_525_60) ?  480 : 576;

	/* Compare with the default setting */
	if ((pix_def->width != pix->width)
		|| (pix_def->height != pix->height)) {
		pix_def->width		= pix->width;
		pix_def->height		= pix->height;
		pix_def->bytesperline	= pix->width * 2;
		pix_def->sizeimage 	= pix->width * pix->height * 2;
	}
	*pix = *pix_def;

	return 0;
}

static int vidioc_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct front_face *front	= fh;
	struct poseidon *pd		= front->pd;

	logs(front);
	/* stop VBI here */
	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != f->type)
		return -EINVAL;

	mutex_lock(&pd->lock);
	if (pd->file_for_stream == NULL)
		pd->file_for_stream = file;
	else if (file != pd->file_for_stream) {
		mutex_unlock(&pd->lock);
		return -EINVAL;
	}

	pd_vidioc_s_fmt(pd, &f->fmt.pix);
	mutex_unlock(&pd->lock);
	return 0;
}

static int vidioc_g_fmt_vbi(struct file *file, void *fh,
			       struct v4l2_format *v4l2_f)
{
	struct front_face *front	= fh;
	struct poseidon *pd		= front->pd;
	struct v4l2_vbi_format *vbi_fmt	= &v4l2_f->fmt.vbi;

	vbi_fmt->samples_per_line	= 720 * 2;
	vbi_fmt->sampling_rate		= 6750000 * 4;
	vbi_fmt->sample_format		= V4L2_PIX_FMT_GREY;
	vbi_fmt->offset			= 64 * 4;  /*FIXME: why offset */
	if (pd->video_data.context.tvnormid & V4L2_STD_525_60) {
		vbi_fmt->start[0] = 10;
		vbi_fmt->start[1] = 264;
		vbi_fmt->count[0] = V4L_NTSC_VBI_LINES;
		vbi_fmt->count[1] = V4L_NTSC_VBI_LINES;
	} else {
		vbi_fmt->start[0] = 6;
		vbi_fmt->start[1] = 314;
		vbi_fmt->count[0] = V4L_PAL_VBI_LINES;
		vbi_fmt->count[1] = V4L_PAL_VBI_LINES;
	}
	vbi_fmt->flags = V4L2_VBI_UNSYNC;
	logs(front);
	return 0;
}

static int set_std(struct poseidon *pd, v4l2_std_id *norm)
{
	struct video_data *video = &pd->video_data;
	struct vbi_data *vbi	= &pd->vbi_data;
	struct running_context *context;
	struct v4l2_pix_format *pix;
	s32 i, ret = 0, cmd_status, param;
	int height;

	for (i = 0; i < POSEIDON_TVNORMS; i++) {
		if (*norm & poseidon_tvnorms[i].v4l2_id) {
			param = poseidon_tvnorms[i].tlg_tvnorm;
			log("name : %s", poseidon_tvnorms[i].name);
			goto found;
		}
	}
	return -EINVAL;
found:
	mutex_lock(&pd->lock);
	ret = send_set_req(pd, VIDEO_STD_SEL, param, &cmd_status);
	if (ret || cmd_status)
		goto out;

	/* Set vbi size and check the height of the frame */
	context = &video->context;
	context->tvnormid = poseidon_tvnorms[i].v4l2_id;
	if (context->tvnormid & V4L2_STD_525_60) {
		vbi->vbi_size = V4L_NTSC_VBI_FRAMESIZE;
		height = 480;
	} else {
		vbi->vbi_size = V4L_PAL_VBI_FRAMESIZE;
		height = 576;
	}

	pix = &context->pix;
	if (pix->height != height) {
		pix->height	= height;
		pix->sizeimage 	= pix->width * pix->height * 2;
	}

out:
	mutex_unlock(&pd->lock);
	return ret;
}

static int vidioc_s_std(struct file *file, void *fh, v4l2_std_id *norm)
{
	struct front_face *front = fh;
	logs(front);
	return set_std(front->pd, norm);
}

static int vidioc_enum_input(struct file *file, void *fh, struct v4l2_input *in)
{
	struct front_face *front = fh;

	if (in->index >= POSEIDON_INPUTS)
		return -EINVAL;
	strcpy(in->name, pd_inputs[in->index].name);
	in->type  = V4L2_INPUT_TYPE_TUNER;

	/*
	 * the audio input index mixed with this video input,
	 * Poseidon only have one audio/video, set to "0"
	 */
	in->audioset	= 1;
	in->tuner	= 0;
	in->std		= V4L2_STD_ALL;
	in->status	= 0;
	logs(front);
	return 0;
}

static int vidioc_g_input(struct file *file, void *fh, unsigned int *i)
{
	struct front_face *front = fh;
	struct poseidon *pd = front->pd;
	struct running_context *context = &pd->video_data.context;

	logs(front);
	*i = context->sig_index;
	return 0;
}

/* We can support several inputs */
static int vidioc_s_input(struct file *file, void *fh, unsigned int i)
{
	struct front_face *front = fh;
	struct poseidon *pd = front->pd;
	s32 ret, cmd_status;

	if (i >= POSEIDON_INPUTS)
		return -EINVAL;
	ret = send_set_req(pd, SGNL_SRC_SEL,
			pd_inputs[i].tlg_src, &cmd_status);
	if (ret)
		return ret;

	pd->video_data.context.sig_index = i;
	return 0;
}

static struct poseidon_control *check_control_id(u32 id)
{
	struct poseidon_control *control = &controls[0];
	int array_size = ARRAY_SIZE(controls);

	for (; control < &controls[array_size]; control++)
		if (control->v4l2_ctrl.id  == id)
			return control;
	return NULL;
}

static int vidioc_queryctrl(struct file *file, void *fh,
			struct v4l2_queryctrl *a)
{
	struct poseidon_control *control = NULL;

	control = check_control_id(a->id);
	if (!control)
		return -EINVAL;

	*a = control->v4l2_ctrl;
	return 0;
}

static int vidioc_g_ctrl(struct file *file, void *fh, struct v4l2_control *ctrl)
{
	struct front_face *front = fh;
	struct poseidon *pd = front->pd;
	struct poseidon_control *control = NULL;
	struct tuner_custom_parameter_s tuner_param;
	s32 ret = 0, cmd_status;

	control = check_control_id(ctrl->id);
	if (!control)
		return -EINVAL;

	mutex_lock(&pd->lock);
	ret = send_get_req(pd, TUNER_CUSTOM_PARAMETER, control->vc_id,
			&tuner_param, &cmd_status, sizeof(tuner_param));
	mutex_unlock(&pd->lock);

	if (ret || cmd_status)
		return -1;

	ctrl->value = tuner_param.param_value;
	return 0;
}

static int vidioc_s_ctrl(struct file *file, void *fh, struct v4l2_control *a)
{
	struct tuner_custom_parameter_s param = {0};
	struct poseidon_control *control = NULL;
	struct front_face *front	= fh;
	struct poseidon *pd		= front->pd;
	s32 ret = 0, cmd_status, params;

	control = check_control_id(a->id);
	if (!control)
		return -EINVAL;

	param.param_value = a->value;
	param.param_id	= control->vc_id;
	params = *(s32 *)&param; /* temp code */

	mutex_lock(&pd->lock);
	ret = send_set_req(pd, TUNER_CUSTOM_PARAMETER, params, &cmd_status);
	ret = send_set_req(pd, TAKE_REQUEST, 0, &cmd_status);
	mutex_unlock(&pd->lock);

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ/4);
	return ret;
}

/* Audio ioctls */
static int vidioc_enumaudio(struct file *file, void *fh, struct v4l2_audio *a)
{
	if (0 != a->index)
		return -EINVAL;
	a->capability = V4L2_AUDCAP_STEREO;
	strcpy(a->name, "USB audio in");
	/*Poseidon have no AVL function.*/
	a->mode = 0;
	return 0;
}

static int vidioc_g_audio(struct file *file, void *fh, struct v4l2_audio *a)
{
	a->index = 0;
	a->capability = V4L2_AUDCAP_STEREO;
	strcpy(a->name, "USB audio in");
	a->mode = 0;
	return 0;
}

static int vidioc_s_audio(struct file *file, void *fh, const struct v4l2_audio *a)
{
	return (0 == a->index) ? 0 : -EINVAL;
}

/* Tuner ioctls */
static int vidioc_g_tuner(struct file *file, void *fh, struct v4l2_tuner *tuner)
{
	struct front_face *front	= fh;
	struct poseidon *pd		= front->pd;
	struct tuner_atv_sig_stat_s atv_stat;
	s32 count = 5, ret, cmd_status;
	int index;

	if (0 != tuner->index)
		return -EINVAL;

	mutex_lock(&pd->lock);
	ret = send_get_req(pd, TUNER_STATUS, TLG_MODE_ANALOG_TV,
				&atv_stat, &cmd_status, sizeof(atv_stat));

	while (atv_stat.sig_lock_busy && count-- && !ret) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);

		ret = send_get_req(pd, TUNER_STATUS, TLG_MODE_ANALOG_TV,
				&atv_stat, &cmd_status, sizeof(atv_stat));
	}
	mutex_unlock(&pd->lock);

	if (debug_mode)
		log("P:%d,S:%d", atv_stat.sig_present, atv_stat.sig_strength);

	if (ret || cmd_status)
		tuner->signal = 0;
	else if (atv_stat.sig_present && !atv_stat.sig_strength)
		tuner->signal = 0xFFFF;
	else
		tuner->signal = (atv_stat.sig_strength * 255 / 10) << 8;

	strcpy(tuner->name, "Telegent Systems");
	tuner->type = V4L2_TUNER_ANALOG_TV;
	tuner->rangelow = TUNER_FREQ_MIN / 62500;
	tuner->rangehigh = TUNER_FREQ_MAX / 62500;
	tuner->capability = V4L2_TUNER_CAP_NORM | V4L2_TUNER_CAP_STEREO |
				V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2;
	index = pd->video_data.context.audio_idx;
	tuner->rxsubchans = pd_audio_modes[index].v4l2_audio_sub;
	tuner->audmode = pd_audio_modes[index].v4l2_audio_mode;
	tuner->afc = 0;
	logs(front);
	return 0;
}

static int pd_vidioc_s_tuner(struct poseidon *pd, int index)
{
	s32 ret = 0, cmd_status, param, audiomode;

	mutex_lock(&pd->lock);
	param = pd_audio_modes[index].tlg_audio_mode;
	ret = send_set_req(pd, TUNER_AUD_MODE, param, &cmd_status);
	audiomode = get_audio_std(pd->video_data.context.tvnormid);
	ret |= send_set_req(pd, TUNER_AUD_ANA_STD, audiomode,
				&cmd_status);
	if (!ret)
		pd->video_data.context.audio_idx = index;
	mutex_unlock(&pd->lock);
	return ret;
}

static int vidioc_s_tuner(struct file *file, void *fh, struct v4l2_tuner *a)
{
	struct front_face *front	= fh;
	struct poseidon *pd		= front->pd;
	int index;

	if (0 != a->index)
		return -EINVAL;
	logs(front);
	for (index = 0; index < POSEIDON_AUDIOMODS; index++)
		if (a->audmode == pd_audio_modes[index].v4l2_audio_mode)
			return pd_vidioc_s_tuner(pd, index);
	return -EINVAL;
}

static int vidioc_g_frequency(struct file *file, void *fh,
			struct v4l2_frequency *freq)
{
	struct front_face *front = fh;
	struct poseidon *pd = front->pd;
	struct running_context *context = &pd->video_data.context;

	if (0 != freq->tuner)
		return -EINVAL;
	freq->frequency = context->freq;
	freq->type = V4L2_TUNER_ANALOG_TV;
	return 0;
}

static int set_frequency(struct poseidon *pd, u32 *frequency)
{
	s32 ret = 0, param, cmd_status;
	struct running_context *context = &pd->video_data.context;

	*frequency = clamp(*frequency,
			TUNER_FREQ_MIN / 62500, TUNER_FREQ_MAX / 62500);
	param = (*frequency) * 62500 / 1000;

	mutex_lock(&pd->lock);
	ret = send_set_req(pd, TUNE_FREQ_SELECT, param, &cmd_status);
	ret = send_set_req(pd, TAKE_REQUEST, 0, &cmd_status);

	msleep(250); /* wait for a while until the hardware is ready. */
	context->freq = *frequency;
	mutex_unlock(&pd->lock);
	return ret;
}

static int vidioc_s_frequency(struct file *file, void *fh,
				struct v4l2_frequency *freq)
{
	struct front_face *front = fh;
	struct poseidon *pd = front->pd;

	if (freq->tuner)
		return -EINVAL;
	logs(front);
#ifdef CONFIG_PM
	pd->pm_suspend = pm_video_suspend;
	pd->pm_resume = pm_video_resume;
#endif
	return set_frequency(pd, &freq->frequency);
}

static int vidioc_reqbufs(struct file *file, void *fh,
				struct v4l2_requestbuffers *b)
{
	struct front_face *front = file->private_data;
	logs(front);
	return videobuf_reqbufs(&front->q, b);
}

static int vidioc_querybuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct front_face *front = file->private_data;
	logs(front);
	return videobuf_querybuf(&front->q, b);
}

static int vidioc_qbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct front_face *front = file->private_data;
	return videobuf_qbuf(&front->q, b);
}

static int vidioc_dqbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct front_face *front = file->private_data;
	return videobuf_dqbuf(&front->q, b, file->f_flags & O_NONBLOCK);
}

/* Just stop the URBs, do not free the URBs */
static int usb_transfer_stop(struct video_data *video)
{
	if (video->is_streaming) {
		int i;
		s32 cmd_status;
		struct poseidon *pd = video->pd;

		video->is_streaming = 0;
		for (i = 0; i < SBUF_NUM; ++i) {
			if (video->urb_array[i])
				usb_kill_urb(video->urb_array[i]);
		}

		send_set_req(pd, PLAY_SERVICE, TLG_TUNE_PLAY_SVC_STOP,
			       &cmd_status);
	}
	return 0;
}

int stop_all_video_stream(struct poseidon *pd)
{
	struct video_data *video = &pd->video_data;
	struct vbi_data *vbi	= &pd->vbi_data;

	mutex_lock(&pd->lock);
	if (video->is_streaming) {
		struct front_face *front = video->front;

		/* stop the URBs */
		usb_transfer_stop(video);
		free_all_urb(video);

		/* stop the host side of VIDEO */
		videobuf_stop(&front->q);
		videobuf_mmap_free(&front->q);

		/* stop the host side of VBI */
		front = vbi->front;
		if (front) {
			videobuf_stop(&front->q);
			videobuf_mmap_free(&front->q);
		}
	}
	mutex_unlock(&pd->lock);
	return 0;
}

/*
 * The bubbles can seriously damage the video's quality,
 * though it occurs in very rare situation.
 */
static void iso_bubble_handler(struct work_struct *w)
{
	struct video_data *video;
	struct poseidon *pd;

	video = container_of(w, struct video_data, bubble_work);
	pd = video->pd;

	mutex_lock(&pd->lock);
	usb_transfer_stop(video);
	msleep(500);
	start_video_stream(pd);
	mutex_unlock(&pd->lock);
}


static int vidioc_streamon(struct file *file, void *fh,
				enum v4l2_buf_type type)
{
	struct front_face *front = fh;

	logs(front);
	if (unlikely(type != front->type))
		return -EINVAL;
	return videobuf_streamon(&front->q);
}

static int vidioc_streamoff(struct file *file, void *fh,
				enum v4l2_buf_type type)
{
	struct front_face *front = file->private_data;

	logs(front);
	if (unlikely(type != front->type))
		return -EINVAL;
	return videobuf_streamoff(&front->q);
}

/* Set the firmware's default values : need altersetting */
static int pd_video_checkmode(struct poseidon *pd)
{
	s32 ret = 0, cmd_status, audiomode;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ/2);

	/* choose the altersetting */
	ret = usb_set_interface(pd->udev, 0,
					(pd->cur_transfer_mode ?
					 ISO_3K_BULK_ALTERNATE_IFACE :
					 BULK_ALTERNATE_IFACE));
	if (ret < 0)
		goto error;

	/* set default parameters for PAL-D , with the VBI enabled*/
	ret = set_tuner_mode(pd, TLG_MODE_ANALOG_TV);
	ret |= send_set_req(pd, SGNL_SRC_SEL,
				TLG_SIG_SRC_ANTENNA, &cmd_status);
	ret |= send_set_req(pd, VIDEO_STD_SEL,
				TLG_TUNE_VSTD_PAL_D, &cmd_status);
	ret |= send_set_req(pd, VIDEO_STREAM_FMT_SEL,
				TLG_TUNER_VID_FORMAT_YUV, &cmd_status);
	ret |= send_set_req(pd, VIDEO_ROSOLU_SEL,
				TLG_TUNE_VID_RES_720, &cmd_status);
	ret |= send_set_req(pd, TUNE_FREQ_SELECT, TUNER_FREQ_MIN, &cmd_status);
	ret |= send_set_req(pd, VBI_DATA_SEL, 1, &cmd_status);/* enable vbi */

	/* set the audio */
	audiomode = get_audio_std(pd->video_data.context.tvnormid);
	ret |= send_set_req(pd, TUNER_AUD_ANA_STD, audiomode, &cmd_status);
	ret |= send_set_req(pd, TUNER_AUD_MODE,
				TLG_TUNE_TVAUDIO_MODE_STEREO, &cmd_status);
	ret |= send_set_req(pd, AUDIO_SAMPLE_RATE_SEL,
				ATV_AUDIO_RATE_48K, &cmd_status);
error:
	return ret;
}

#ifdef CONFIG_PM
static int pm_video_suspend(struct poseidon *pd)
{
	/* stop audio */
	pm_alsa_suspend(pd);

	/* stop and free all the URBs */
	usb_transfer_stop(&pd->video_data);
	free_all_urb(&pd->video_data);

	/* reset the interface */
	usb_set_interface(pd->udev, 0, 0);
	msleep(300);
	return 0;
}

static int restore_v4l2_context(struct poseidon *pd,
				struct running_context *context)
{
	struct front_face *front = pd->video_data.front;

	pd_video_checkmode(pd);

	set_std(pd, &context->tvnormid);
	vidioc_s_input(NULL, front, context->sig_index);
	pd_vidioc_s_tuner(pd, context->audio_idx);
	pd_vidioc_s_fmt(pd, &context->pix);
	set_frequency(pd, &context->freq);
	return 0;
}

static int pm_video_resume(struct poseidon *pd)
{
	struct video_data *video = &pd->video_data;

	/* resume the video */
	/* [1] restore the origin V4L2 parameters */
	restore_v4l2_context(pd, &video->context);

	/* [2] initiate video copy variables */
	if (video->front->curr_frame)
		init_copy(video, 0);

	/* [3] fire urbs	*/
	start_video_stream(pd);

	/* resume the audio */
	pm_alsa_resume(pd);
	return 0;
}
#endif

void set_debug_mode(struct video_device *vfd, int debug_mode)
{
	vfd->debug = 0;
	if (debug_mode & 0x1)
		vfd->debug = V4L2_DEBUG_IOCTL;
	if (debug_mode & 0x2)
		vfd->debug = V4L2_DEBUG_IOCTL | V4L2_DEBUG_IOCTL_ARG;
}

static void init_video_context(struct running_context *context)
{
	context->sig_index	= 0;
	context->audio_idx	= 1; /* stereo */
	context->tvnormid  	= V4L2_STD_PAL_D;
	context->pix = (struct v4l2_pix_format) {
				.width		= 720,
				.height		= 576,
				.pixelformat	= V4L2_PIX_FMT_YUYV,
				.field		= V4L2_FIELD_INTERLACED,
				.bytesperline	= 720 * 2,
				.sizeimage	= 720 * 576 * 2,
				.colorspace	= V4L2_COLORSPACE_SMPTE170M,
				.priv		= 0
			};
}

static int pd_video_open(struct file *file)
{
	struct video_device *vfd = video_devdata(file);
	struct poseidon *pd = video_get_drvdata(vfd);
	struct front_face *front = NULL;
	int ret = -ENOMEM;

	mutex_lock(&pd->lock);
	usb_autopm_get_interface(pd->interface);

	if (vfd->vfl_type == VFL_TYPE_GRABBER
		&& !(pd->state & POSEIDON_STATE_ANALOG)) {
		front = kzalloc(sizeof(struct front_face), GFP_KERNEL);
		if (!front)
			goto out;

		pd->cur_transfer_mode	= usb_transfer_mode;/* bulk or iso */
		init_video_context(&pd->video_data.context);

		ret = pd_video_checkmode(pd);
		if (ret < 0) {
			kfree(front);
			ret = -1;
			goto out;
		}

		pd->state		|= POSEIDON_STATE_ANALOG;
		front->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
		pd->video_data.users++;
		set_debug_mode(vfd, debug_mode);

		videobuf_queue_vmalloc_init(&front->q, &pd_video_qops,
				NULL, &front->queue_lock,
				V4L2_BUF_TYPE_VIDEO_CAPTURE,
				V4L2_FIELD_INTERLACED,/* video is interlacd */
				sizeof(struct videobuf_buffer),/*it's enough*/
				front, NULL);
	} else if (vfd->vfl_type == VFL_TYPE_VBI
		&& !(pd->state & POSEIDON_STATE_VBI)) {
		front = kzalloc(sizeof(struct front_face), GFP_KERNEL);
		if (!front)
			goto out;

		pd->state	|= POSEIDON_STATE_VBI;
		front->type	= V4L2_BUF_TYPE_VBI_CAPTURE;
		pd->vbi_data.front = front;
		pd->vbi_data.users++;

		videobuf_queue_vmalloc_init(&front->q, &pd_video_qops,
				NULL, &front->queue_lock,
				V4L2_BUF_TYPE_VBI_CAPTURE,
				V4L2_FIELD_NONE, /* vbi is NONE mode */
				sizeof(struct videobuf_buffer),
				front, NULL);
	} else {
		/* maybe add FM support here */
		log("other ");
		ret = -EINVAL;
		goto out;
	}

	front->pd		= pd;
	front->curr_frame	= NULL;
	INIT_LIST_HEAD(&front->active);
	spin_lock_init(&front->queue_lock);

	file->private_data	= front;
	kref_get(&pd->kref);

	mutex_unlock(&pd->lock);
	return 0;
out:
	usb_autopm_put_interface(pd->interface);
	mutex_unlock(&pd->lock);
	return ret;
}

static int pd_video_release(struct file *file)
{
	struct front_face *front = file->private_data;
	struct poseidon *pd = front->pd;
	s32 cmd_status = 0;

	logs(front);
	mutex_lock(&pd->lock);

	if (front->type	== V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		pd->state &= ~POSEIDON_STATE_ANALOG;

		/* stop the device, and free the URBs */
		usb_transfer_stop(&pd->video_data);
		free_all_urb(&pd->video_data);

		/* stop the firmware */
		send_set_req(pd, PLAY_SERVICE, TLG_TUNE_PLAY_SVC_STOP,
			       &cmd_status);

		pd->file_for_stream = NULL;
		pd->video_data.users--;
	} else if (front->type	== V4L2_BUF_TYPE_VBI_CAPTURE) {
		pd->state &= ~POSEIDON_STATE_VBI;
		pd->vbi_data.front = NULL;
		pd->vbi_data.users--;
	}
	videobuf_stop(&front->q);
	videobuf_mmap_free(&front->q);

	usb_autopm_put_interface(pd->interface);
	mutex_unlock(&pd->lock);

	kfree(front);
	file->private_data = NULL;
	kref_put(&pd->kref, poseidon_delete);
	return 0;
}

static int pd_video_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct front_face *front = file->private_data;
	return  videobuf_mmap_mapper(&front->q, vma);
}

static unsigned int pd_video_poll(struct file *file, poll_table *table)
{
	struct front_face *front = file->private_data;
	return videobuf_poll_stream(file, &front->q, table);
}

static ssize_t pd_video_read(struct file *file, char __user *buffer,
			size_t count, loff_t *ppos)
{
	struct front_face *front = file->private_data;
	return videobuf_read_stream(&front->q, buffer, count, ppos,
				0, file->f_flags & O_NONBLOCK);
}

/* This struct works for both VIDEO and VBI */
static const struct v4l2_file_operations pd_video_fops = {
	.owner		= THIS_MODULE,
	.open		= pd_video_open,
	.release	= pd_video_release,
	.read		= pd_video_read,
	.poll		= pd_video_poll,
	.mmap		= pd_video_mmap,
	.ioctl		= video_ioctl2, /* maybe changed in future */
};

static const struct v4l2_ioctl_ops pd_video_ioctl_ops = {
	.vidioc_querycap	= vidioc_querycap,

	/* Video format */
	.vidioc_g_fmt_vid_cap	= vidioc_g_fmt,
	.vidioc_enum_fmt_vid_cap	= vidioc_enum_fmt,
	.vidioc_s_fmt_vid_cap	= vidioc_s_fmt,
	.vidioc_g_fmt_vbi_cap	= vidioc_g_fmt_vbi, /* VBI */
	.vidioc_try_fmt_vid_cap = vidioc_try_fmt,

	/* Input */
	.vidioc_g_input		= vidioc_g_input,
	.vidioc_s_input		= vidioc_s_input,
	.vidioc_enum_input	= vidioc_enum_input,

	/* Audio ioctls */
	.vidioc_enumaudio	= vidioc_enumaudio,
	.vidioc_g_audio		= vidioc_g_audio,
	.vidioc_s_audio		= vidioc_s_audio,

	/* Tuner ioctls */
	.vidioc_g_tuner		= vidioc_g_tuner,
	.vidioc_s_tuner		= vidioc_s_tuner,
	.vidioc_s_std		= vidioc_s_std,
	.vidioc_g_frequency	= vidioc_g_frequency,
	.vidioc_s_frequency	= vidioc_s_frequency,

	/* Buffer handlers */
	.vidioc_reqbufs		= vidioc_reqbufs,
	.vidioc_querybuf	= vidioc_querybuf,
	.vidioc_qbuf		= vidioc_qbuf,
	.vidioc_dqbuf		= vidioc_dqbuf,

	/* Stream on/off */
	.vidioc_streamon	= vidioc_streamon,
	.vidioc_streamoff	= vidioc_streamoff,

	/* Control handling */
	.vidioc_queryctrl	= vidioc_queryctrl,
	.vidioc_g_ctrl		= vidioc_g_ctrl,
	.vidioc_s_ctrl		= vidioc_s_ctrl,
};

static struct video_device pd_video_template = {
	.name = "Telegent-Video",
	.fops = &pd_video_fops,
	.minor = -1,
	.release = video_device_release_empty,
	.tvnorms = V4L2_STD_ALL,
	.ioctl_ops = &pd_video_ioctl_ops,
};

void pd_video_exit(struct poseidon *pd)
{
	struct video_data *video = &pd->video_data;
	struct vbi_data *vbi = &pd->vbi_data;

	video_unregister_device(&video->v_dev);
	video_unregister_device(&vbi->v_dev);
	log();
}

int pd_video_init(struct poseidon *pd)
{
	struct video_data *video = &pd->video_data;
	struct vbi_data *vbi	= &pd->vbi_data;
	u32 freq = TUNER_FREQ_MIN / 62500;
	int ret = -ENOMEM;

	set_frequency(pd, &freq);
	video->v_dev = pd_video_template;
	video->v_dev.v4l2_dev = &pd->v4l2_dev;
	video_set_drvdata(&video->v_dev, pd);

	ret = video_register_device(&video->v_dev, VFL_TYPE_GRABBER, -1);
	if (ret != 0)
		goto out;

	/* VBI uses the same template as video */
	vbi->v_dev = pd_video_template;
	vbi->v_dev.v4l2_dev = &pd->v4l2_dev;
	video_set_drvdata(&vbi->v_dev, pd);
	ret = video_register_device(&vbi->v_dev, VFL_TYPE_VBI, -1);
	if (ret != 0)
		goto out;
	log("register VIDEO/VBI devices");
	return 0;
out:
	log("VIDEO/VBI devices register failed, : %d", ret);
	pd_video_exit(pd);
	return ret;
}
