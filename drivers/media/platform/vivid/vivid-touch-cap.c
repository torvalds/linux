// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-touch-cap.c - touch support functions.
 */

#include "vivid-core.h"
#include "vivid-kthread-touch.h"
#include "vivid-vid-common.h"
#include "vivid-touch-cap.h"

static int touch_cap_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
				 unsigned int *nplanes, unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *f = &dev->tch_format;
	unsigned int size = f->sizeimage;

	if (*nplanes) {
		if (sizes[0] < size)
			return -EINVAL;
	} else {
		sizes[0] = size;
	}

	if (vq->num_buffers + *nbuffers < 2)
		*nbuffers = 2 - vq->num_buffers;

	*nplanes = 1;
	return 0;
}

static int touch_cap_buf_prepare(struct vb2_buffer *vb)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_pix_format *f = &dev->tch_format;
	unsigned int size = f->sizeimage;

	if (dev->buf_prepare_error) {
		/*
		 * Error injection: test what happens if buf_prepare() returns
		 * an error.
		 */
		dev->buf_prepare_error = false;
		return -EINVAL;
	}
	if (vb2_plane_size(vb, 0) < size) {
		dprintk(dev, 1, "%s data will not fit into plane (%lu < %u)\n",
			__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}
	vb2_set_plane_payload(vb, 0, size);

	return 0;
}

static void touch_cap_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct vivid_buffer *buf = container_of(vbuf, struct vivid_buffer, vb);

	vbuf->field = V4L2_FIELD_NONE;
	spin_lock(&dev->slock);
	list_add_tail(&buf->list, &dev->touch_cap_active);
	spin_unlock(&dev->slock);
}

static int touch_cap_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);
	int err;

	dev->touch_cap_seq_count = 0;
	if (dev->start_streaming_error) {
		dev->start_streaming_error = false;
		err = -EINVAL;
	} else {
		err = vivid_start_generating_touch_cap(dev);
	}
	if (err) {
		struct vivid_buffer *buf, *tmp;

		list_for_each_entry_safe(buf, tmp,
					 &dev->touch_cap_active, list) {
			list_del(&buf->list);
			vb2_buffer_done(&buf->vb.vb2_buf,
					VB2_BUF_STATE_QUEUED);
		}
	}
	return err;
}

/* abort streaming and wait for last buffer */
static void touch_cap_stop_streaming(struct vb2_queue *vq)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);

	vivid_stop_generating_touch_cap(dev);
}

static void touch_cap_buf_request_complete(struct vb2_buffer *vb)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &dev->ctrl_hdl_touch_cap);
}

const struct vb2_ops vivid_touch_cap_qops = {
	.queue_setup		= touch_cap_queue_setup,
	.buf_prepare		= touch_cap_buf_prepare,
	.buf_queue		= touch_cap_buf_queue,
	.start_streaming	= touch_cap_start_streaming,
	.stop_streaming		= touch_cap_stop_streaming,
	.buf_request_complete	= touch_cap_buf_request_complete,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

int vivid_enum_fmt_tch(struct file *file, void  *priv, struct v4l2_fmtdesc *f)
{
	if (f->index)
		return -EINVAL;

	f->pixelformat = V4L2_TCH_FMT_DELTA_TD16;
	return 0;
}

int vivid_g_fmt_tch(struct file *file, void *priv, struct v4l2_format *f)
{
	struct vivid_dev *dev = video_drvdata(file);

	if (dev->multiplanar)
		return -ENOTTY;
	f->fmt.pix = dev->tch_format;
	return 0;
}

int vivid_g_fmt_tch_mplane(struct file *file, void *priv, struct v4l2_format *f)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct v4l2_format sp_fmt;

	if (!dev->multiplanar)
		return -ENOTTY;
	sp_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	sp_fmt.fmt.pix = dev->tch_format;
	fmt_sp2mp(&sp_fmt, f);
	return 0;
}

int vivid_g_parm_tch(struct file *file, void *priv,
		     struct v4l2_streamparm *parm)
{
	struct vivid_dev *dev = video_drvdata(file);

	if (parm->type != (dev->multiplanar ?
			   V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE :
			   V4L2_BUF_TYPE_VIDEO_CAPTURE))
		return -EINVAL;

	parm->parm.capture.capability   = V4L2_CAP_TIMEPERFRAME;
	parm->parm.capture.timeperframe = dev->timeperframe_tch_cap;
	parm->parm.capture.readbuffers  = 1;
	return 0;
}

int vivid_enum_input_tch(struct file *file, void *priv, struct v4l2_input *inp)
{
	if (inp->index)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_TOUCH;
	strscpy(inp->name, "Vivid Touch", sizeof(inp->name));
	inp->capabilities = 0;
	return 0;
}

int vivid_g_input_tch(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

int vivid_set_touch(struct vivid_dev *dev, unsigned int i)
{
	struct v4l2_pix_format *f = &dev->tch_format;

	if (i)
		return -EINVAL;

	f->pixelformat = V4L2_TCH_FMT_DELTA_TD16;
	f->width =  VIVID_TCH_WIDTH;
	f->height = VIVID_TCH_HEIGHT;
	f->field = V4L2_FIELD_NONE;
	f->colorspace = V4L2_COLORSPACE_RAW;
	f->bytesperline = f->width * sizeof(s16);
	f->sizeimage = f->width * f->height * sizeof(s16);
	return 0;
}

int vivid_s_input_tch(struct file *file, void *priv, unsigned int i)
{
	return vivid_set_touch(video_drvdata(file), i);
}

static void vivid_fill_buff_noise(__s16 *tch_buf, int size)
{
	int i;

	/* Fill 10% of the values within range -3 and 3, zero the others */
	for (i = 0; i < size; i++) {
		unsigned int rand = get_random_int();

		if (rand % 10)
			tch_buf[i] = 0;
		else
			tch_buf[i] = (rand / 10) % 7 - 3;
	}
}

static inline int get_random_pressure(void)
{
	return get_random_int() % VIVID_PRESSURE_LIMIT;
}

static void vivid_tch_buf_set(struct v4l2_pix_format *f,
			      __s16 *tch_buf,
			      int index)
{
	unsigned int x = index % f->width;
	unsigned int y = index / f->width;
	unsigned int offset = VIVID_MIN_PRESSURE;

	tch_buf[index] = offset + get_random_pressure();
	offset /= 2;
	if (x)
		tch_buf[index - 1] = offset + get_random_pressure();
	if (x < f->width - 1)
		tch_buf[index + 1] = offset + get_random_pressure();
	if (y)
		tch_buf[index - f->width] = offset + get_random_pressure();
	if (y < f->height - 1)
		tch_buf[index + f->width] = offset + get_random_pressure();
	offset /= 2;
	if (x && y)
		tch_buf[index - 1 - f->width] = offset + get_random_pressure();
	if (x < f->width - 1 && y)
		tch_buf[index + 1 - f->width] = offset + get_random_pressure();
	if (x && y < f->height - 1)
		tch_buf[index - 1 + f->width] = offset + get_random_pressure();
	if (x < f->width - 1 && y < f->height - 1)
		tch_buf[index + 1 + f->width] = offset + get_random_pressure();
}

void vivid_fillbuff_tch(struct vivid_dev *dev, struct vivid_buffer *buf)
{
	struct v4l2_pix_format *f = &dev->tch_format;
	int size = f->width * f->height;
	int x, y, xstart, ystart, offset_x, offset_y;
	unsigned int test_pattern, test_pat_idx, rand;

	__s16 *tch_buf = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);

	buf->vb.sequence = dev->touch_cap_seq_count;
	test_pattern = (buf->vb.sequence / TCH_SEQ_COUNT) % TEST_CASE_MAX;
	test_pat_idx = buf->vb.sequence % TCH_SEQ_COUNT;

	vivid_fill_buff_noise(tch_buf, size);

	if (test_pat_idx >= TCH_PATTERN_COUNT)
		return;

	if (test_pat_idx == 0)
		dev->tch_pat_random = get_random_int();
	rand = dev->tch_pat_random;

	switch (test_pattern) {
	case SINGLE_TAP:
		if (test_pat_idx == 2)
			vivid_tch_buf_set(f, tch_buf, rand % size);
		break;
	case DOUBLE_TAP:
		if (test_pat_idx == 2 || test_pat_idx == 4)
			vivid_tch_buf_set(f, tch_buf, rand % size);
		break;
	case TRIPLE_TAP:
		if (test_pat_idx == 2 || test_pat_idx == 4 || test_pat_idx == 6)
			vivid_tch_buf_set(f, tch_buf, rand % size);
		break;
	case MOVE_LEFT_TO_RIGHT:
		vivid_tch_buf_set(f, tch_buf,
				  (rand % f->height) * f->width +
				  test_pat_idx *
				  (f->width / TCH_PATTERN_COUNT));
		break;
	case ZOOM_IN:
		x = f->width / 2;
		y = f->height / 2;
		offset_x = ((TCH_PATTERN_COUNT - 1 - test_pat_idx) * x) /
				TCH_PATTERN_COUNT;
		offset_y = ((TCH_PATTERN_COUNT - 1 - test_pat_idx) * y) /
				TCH_PATTERN_COUNT;
		vivid_tch_buf_set(f, tch_buf,
				  (x - offset_x) + f->width * (y - offset_y));
		vivid_tch_buf_set(f, tch_buf,
				  (x + offset_x) + f->width * (y + offset_y));
		break;
	case ZOOM_OUT:
		x = f->width / 2;
		y = f->height / 2;
		offset_x = (test_pat_idx * x) / TCH_PATTERN_COUNT;
		offset_y = (test_pat_idx * y) / TCH_PATTERN_COUNT;
		vivid_tch_buf_set(f, tch_buf,
				  (x - offset_x) + f->width * (y - offset_y));
		vivid_tch_buf_set(f, tch_buf,
				  (x + offset_x) + f->width * (y + offset_y));
		break;
	case PALM_PRESS:
		for (x = 0; x < f->width; x++)
			for (y = f->height / 2; y < f->height; y++)
				tch_buf[x + f->width * y] = VIVID_MIN_PRESSURE +
							get_random_pressure();
		break;
	case MULTIPLE_PRESS:
		/* 16 pressure points */
		for (y = 0; y < 4; y++) {
			for (x = 0; x < 4; x++) {
				ystart = (y * f->height) / 4 + f->height / 8;
				xstart = (x * f->width) / 4 + f->width / 8;
				vivid_tch_buf_set(f, tch_buf,
						  ystart * f->width + xstart);
			}
		}
		break;
	}
#ifdef __BIG_ENDIAN__
	for (x = 0; x < size; x++)
		tch_buf[x] = (__force s16)__cpu_to_le16((u16)tch_buf[x]);
#endif
}
