/*
 *
 * Copyright (C) 2008-2009 QUALCOMM Incorporated.
 *
 */

#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/ioctl.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>
#include <linux/proc_fs.h>
#include <media/v4l2-dev.h>
#include <media/msm_camera.h>
#include <mach/camera.h>
#include <media/v4l2-ioctl.h>
/*#include <linux/platform_device.h>*/

#define MSM_V4L2_START_SNAPSHOT _IOWR('V', BASE_VIDIOC_PRIVATE+1, \
      struct v4l2_buffer)

#define MSM_V4L2_GET_PICTURE    _IOWR('V', BASE_VIDIOC_PRIVATE+2, \
      struct v4l2_buffer)

#define MSM_V4L2_DEVICE_NAME       "msm_v4l2"

#define MSM_V4L2_PROC_NAME         "msm_v4l2"

#define MSM_V4L2_DEVNUM_MPEG2       0
#define MSM_V4L2_DEVNUM_YUV         20

/* HVGA-P (portrait) and HVGA-L (landscape) */
#define MSM_V4L2_WIDTH              480
#define MSM_V4L2_HEIGHT             320

#if 1
#define D(fmt, args...) printk(KERN_INFO "msm_v4l2: " fmt, ##args)
#else
#define D(fmt, args...) do {} while (0)
#endif

#define PREVIEW_FRAMES_NUM 4

struct msm_v4l2_device {
	struct list_head read_queue;
	struct v4l2_format current_cap_format;
	struct v4l2_format current_pix_format;
	struct video_device *pvdev;
	struct msm_v4l2_driver   *drv;
	uint8_t opencnt;

	spinlock_t read_queue_lock;
};

static struct msm_v4l2_device *g_pmsm_v4l2_dev;


static DEFINE_MUTEX(msm_v4l2_opencnt_lock);

static int msm_v4l2_open(struct file *f)
{
	int rc = 0;
	D("%s\n", __func__);
	mutex_lock(&msm_v4l2_opencnt_lock);
	if (!g_pmsm_v4l2_dev->opencnt) {
		rc = g_pmsm_v4l2_dev->drv->open(
				g_pmsm_v4l2_dev->drv->sync,
				MSM_APPS_ID_V4L2);
	}
	g_pmsm_v4l2_dev->opencnt++;
	mutex_unlock(&msm_v4l2_opencnt_lock);
	return rc;
}

static int msm_v4l2_release(struct file *f)
{
	int rc = 0;
	D("%s\n", __func__);
	mutex_lock(&msm_v4l2_opencnt_lock);
	if (!g_pmsm_v4l2_dev->opencnt) {
		g_pmsm_v4l2_dev->opencnt--;
		if (!g_pmsm_v4l2_dev->opencnt) {
			rc = g_pmsm_v4l2_dev->drv->release(
					g_pmsm_v4l2_dev->drv->sync);
		}
	}
	mutex_unlock(&msm_v4l2_opencnt_lock);
	return rc;
}

static unsigned int msm_v4l2_poll(struct file *f, struct poll_table_struct *w)
{
	return g_pmsm_v4l2_dev->drv->drv_poll(g_pmsm_v4l2_dev->drv->sync, f, w);
}

static long msm_v4l2_ioctl(struct file *filep,
			   unsigned int cmd, unsigned long arg)
{
	struct msm_ctrl_cmd *ctrlcmd;

	D("msm_v4l2_ioctl, cmd = %d, %d\n", cmd, __LINE__);

	switch (cmd) {
	case MSM_V4L2_START_SNAPSHOT:

		ctrlcmd = kmalloc(sizeof(struct msm_ctrl_cmd), GFP_ATOMIC);
		if (!ctrlcmd) {
			CDBG("msm_v4l2_ioctl: cannot allocate buffer\n");
			return -ENOMEM;
		}

		ctrlcmd->length     = 0;
		ctrlcmd->value      = NULL;
		ctrlcmd->timeout_ms = 10000;

		D("msm_v4l2_ioctl,  MSM_V4L2_START_SNAPSHOT v4l2 ioctl %d\n",
		cmd);
		ctrlcmd->type = MSM_V4L2_SNAPSHOT;
		return g_pmsm_v4l2_dev->drv->ctrl(g_pmsm_v4l2_dev->drv->sync,
							ctrlcmd);

	case MSM_V4L2_GET_PICTURE:
		D("msm_v4l2_ioctl,  MSM_V4L2_GET_PICTURE v4l2 ioctl %d\n", cmd);
		ctrlcmd = (struct msm_ctrl_cmd *)arg;
		return g_pmsm_v4l2_dev->drv->get_pict(
				g_pmsm_v4l2_dev->drv->sync, ctrlcmd);

	default:
		D("msm_v4l2_ioctl, standard v4l2 ioctl %d\n", cmd);
		return video_ioctl2(filep, cmd, arg);
	}
}

static void msm_v4l2_release_dev(struct video_device *d)
{
	D("%s\n", __func__);
}

static int msm_v4l2_querycap(struct file *f,
			     void *pctx, struct v4l2_capability *pcaps)
{
	D("%s\n", __func__);
	strncpy(pcaps->driver, MSM_APPS_ID_V4L2, sizeof(pcaps->driver));
	strncpy(pcaps->card,
		MSM_V4L2_DEVICE_NAME, sizeof(pcaps->card));
	pcaps->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;
}

static int msm_v4l2_s_std(struct file *f, void *pctx, v4l2_std_id *pnorm)
{
	D("%s\n", __func__);
	return 0;
}

static int msm_v4l2_queryctrl(struct file *f,
				void *pctx, struct v4l2_queryctrl *pqctrl)
{
  int rc = 0;
  struct msm_ctrl_cmd *ctrlcmd;

	D("%s\n", __func__);

	ctrlcmd = kmalloc(sizeof(struct msm_ctrl_cmd), GFP_ATOMIC);
	if (!ctrlcmd) {
		CDBG("msm_v4l2_queryctrl: cannot allocate buffer\n");
		return -ENOMEM;
	}

	ctrlcmd->type       = MSM_V4L2_QUERY_CTRL;
	ctrlcmd->length     = sizeof(struct v4l2_queryctrl);
	ctrlcmd->value      = pqctrl;
	ctrlcmd->timeout_ms = 10000;

	rc = g_pmsm_v4l2_dev->drv->ctrl(g_pmsm_v4l2_dev->drv->sync, ctrlcmd);
	if (rc < 0)
		return -1;

	return ctrlcmd->status;
}

static int msm_v4l2_g_ctrl(struct file *f, void *pctx, struct v4l2_control *c)
{
	int rc = 0;
	struct msm_ctrl_cmd *ctrlcmd;

	D("%s\n", __func__);

	ctrlcmd = kmalloc(sizeof(struct msm_ctrl_cmd), GFP_ATOMIC);
	if (!ctrlcmd) {
		CDBG("msm_v4l2_g_ctrl: cannot allocate buffer\n");
		return -ENOMEM;
	}

	ctrlcmd->type       = MSM_V4L2_GET_CTRL;
	ctrlcmd->length     = sizeof(struct v4l2_control);
	ctrlcmd->value      = c;
	ctrlcmd->timeout_ms = 10000;

	rc = g_pmsm_v4l2_dev->drv->ctrl(g_pmsm_v4l2_dev->drv->sync, ctrlcmd);
	if (rc < 0)
		return -1;

	return ctrlcmd->status;
}

static int msm_v4l2_s_ctrl(struct file *f, void *pctx, struct v4l2_control *c)
{
	int rc = 0;
	struct msm_ctrl_cmd *ctrlcmd;

	ctrlcmd = kmalloc(sizeof(struct msm_ctrl_cmd), GFP_ATOMIC);
	if (!ctrlcmd) {
		CDBG("msm_v4l2_s_ctrl: cannot allocate buffer\n");
		return -ENOMEM;
	}

	ctrlcmd->type       = MSM_V4L2_SET_CTRL;
	ctrlcmd->length     = sizeof(struct v4l2_control);
	ctrlcmd->value      = c;
	ctrlcmd->timeout_ms = 10000;

	D("%s\n", __func__);

	rc = g_pmsm_v4l2_dev->drv->ctrl(g_pmsm_v4l2_dev->drv->sync, ctrlcmd);
	if (rc < 0)
		return -1;

	return ctrlcmd->status;
}

static int msm_v4l2_reqbufs(struct file *f,
			    void *pctx, struct v4l2_requestbuffers *b)
{
	D("%s\n", __func__);
	return 0;
}

static int msm_v4l2_querybuf(struct file *f, void *pctx, struct v4l2_buffer *pb)
{
	struct msm_pmem_info pmem_buf;
#if 0
	__u32 width = 0;
	__u32 height = 0;
	__u32 y_size = 0;
	__u32 y_pad = 0;

	/* FIXME: g_pmsm_v4l2_dev->current_pix_format.fmt.pix.width; */
	width = 640;
	/* FIXME: g_pmsm_v4l2_dev->current_pix_format.fmt.pix.height; */
	height = 480;

	D("%s: width = %d, height = %d\n", __func__, width, height);

	y_size = width * height;
	y_pad = y_size % 4;
#endif

    __u32 y_pad = pb->bytesused % 4;

	/* V4L2 videodev will do the copy_from_user. */

	memset(&pmem_buf, 0, sizeof(struct msm_pmem_info));
	pmem_buf.type = MSM_PMEM_OUTPUT2;
	pmem_buf.vaddr = (void *)pb->m.userptr;
	pmem_buf.y_off = 0;
	pmem_buf.fd = (int)pb->reserved;
	/* pmem_buf.cbcr_off = (y_size + y_pad); */
    pmem_buf.cbcr_off = (pb->bytesused + y_pad);

	g_pmsm_v4l2_dev->drv->reg_pmem(g_pmsm_v4l2_dev->drv->sync, &pmem_buf);

	return 0;
}

static int msm_v4l2_qbuf(struct file *f, void *pctx, struct v4l2_buffer *pb)
{
    /*
	__u32 y_size = 0;
	__u32 y_pad = 0;
	__u32 width = 0;
	__u32 height = 0;
    */

	__u32 y_pad = 0;

	struct msm_pmem_info meminfo;
	struct msm_frame frame;
	static int cnt;

	if ((pb->flags >> 16) & 0x0001) {
		/* this is for previwe */
#if 0
		width = 640;
		height = 480;

		/* V4L2 videodev will do the copy_from_user. */
		D("%s: width = %d, height = %d\n", __func__, width, height);
		y_size = width * height;
		y_pad = y_size % 4;
#endif

		y_pad = pb->bytesused % 4;

		if (pb->type == V4L2_BUF_TYPE_PRIVATE) {
			/* this qbuf is actually for releasing */

			frame.buffer           = pb->m.userptr;
			frame.y_off            = 0;
			/* frame.cbcr_off = (y_size + y_pad); */
			frame.cbcr_off         = (pb->bytesused + y_pad);
			frame.fd               = pb->reserved;

			D("V4L2_BUF_TYPE_PRIVATE: pb->bytesused = %d \n",
			pb->bytesused);

			g_pmsm_v4l2_dev->drv->put_frame(
				g_pmsm_v4l2_dev->drv->sync,
				&frame);

			return 0;
		}

		D("V4L2_BUF_TYPE_VIDEO_CAPTURE: pb->bytesused = %d \n",
		pb->bytesused);

		meminfo.type             = MSM_PMEM_OUTPUT2;
		meminfo.fd               = (int)pb->reserved;
		meminfo.vaddr            = (void *)pb->m.userptr;
		meminfo.y_off            = 0;
		/* meminfo.cbcr_off = (y_size + y_pad); */
		meminfo.cbcr_off         = (pb->bytesused + y_pad);
		if (cnt == PREVIEW_FRAMES_NUM - 1)
			meminfo.active = 0;
		else
			meminfo.active = 1;
		cnt++;
		g_pmsm_v4l2_dev->drv->reg_pmem(g_pmsm_v4l2_dev->drv->sync,
				&meminfo);
	} else if ((pb->flags) & 0x0001) {
		/* this is for snapshot */

	__u32 y_size = 0;

	if ((pb->flags >> 8) & 0x01) {

		y_size = pb->bytesused;

		meminfo.type = MSM_PMEM_THUMBAIL;
	} else if ((pb->flags >> 9) & 0x01) {

		y_size = pb->bytesused;

		meminfo.type = MSM_PMEM_MAINIMG;
	}

	y_pad = y_size % 4;

	meminfo.fd         = (int)pb->reserved;
	meminfo.vaddr      = (void *)pb->m.userptr;
	meminfo.y_off      = 0;
	/* meminfo.cbcr_off = (y_size + y_pad); */
	meminfo.cbcr_off   = (y_size + y_pad);
	meminfo.active 	   = 1;
	g_pmsm_v4l2_dev->drv->reg_pmem(g_pmsm_v4l2_dev->drv->sync,
					&meminfo);
	}

	return 0;
}

static int msm_v4l2_dqbuf(struct file *f, void *pctx, struct v4l2_buffer *pb)
{
	struct msm_frame frame;
	D("%s\n", __func__);

	/* V4L2 videodev will do the copy_to_user. */
	if (pb->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {

		D("%s, %d\n", __func__, __LINE__);

		g_pmsm_v4l2_dev->drv->get_frame(
			g_pmsm_v4l2_dev->drv->sync,
			&frame);

		pb->type       = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		pb->m.userptr  = (unsigned long)frame.buffer;  /* FIXME */
		pb->reserved   = (int)frame.fd;
		/* pb->length     = (int)frame.cbcr_off; */

		pb->bytesused  = frame.cbcr_off;

	} else if (pb->type == V4L2_BUF_TYPE_PRIVATE) {
		__u32 y_pad     = pb->bytesused % 4;

		frame.buffer   = pb->m.userptr;
		frame.y_off    = 0;
		/* frame.cbcr_off = (y_size + y_pad); */
		frame.cbcr_off = (pb->bytesused + y_pad);
		frame.fd       = pb->reserved;

		g_pmsm_v4l2_dev->drv->put_frame(
			g_pmsm_v4l2_dev->drv->sync,
			&frame);
	}

	return 0;
}

static int msm_v4l2_streamon(struct file *f, void *pctx, enum v4l2_buf_type i)
{
  struct msm_ctrl_cmd *ctrlcmd;

	ctrlcmd = kmalloc(sizeof(struct msm_ctrl_cmd), GFP_ATOMIC);
	if (!ctrlcmd) {
		CDBG("msm_v4l2_s_fmt_cap: cannot allocate buffer\n");
		return -ENOMEM;
	}

	ctrlcmd->type       = MSM_V4L2_STREAM_ON;
	ctrlcmd->timeout_ms = 10000;
	ctrlcmd->length     = 0;
	ctrlcmd->value      = NULL;

	D("%s\n", __func__);

	g_pmsm_v4l2_dev->drv->ctrl(
		g_pmsm_v4l2_dev->drv->sync,
		ctrlcmd);

	D("%s after drv->ctrl \n", __func__);

	return 0;
}

static int msm_v4l2_streamoff(struct file *f, void *pctx, enum v4l2_buf_type i)
{
  struct msm_ctrl_cmd *ctrlcmd;

	ctrlcmd = kmalloc(sizeof(struct msm_ctrl_cmd), GFP_ATOMIC);
	if (!ctrlcmd) {
		CDBG("msm_v4l2_s_fmt_cap: cannot allocate buffer\n");
		return -ENOMEM;
	}

	ctrlcmd->type       = MSM_V4L2_STREAM_OFF;
	ctrlcmd->timeout_ms = 10000;
	ctrlcmd->length     = 0;
	ctrlcmd->value      = NULL;


	D("%s\n", __func__);

	g_pmsm_v4l2_dev->drv->ctrl(
		g_pmsm_v4l2_dev->drv->sync,
		ctrlcmd);

	return 0;
}

static int msm_v4l2_enum_fmt_overlay(struct file *f,
				     void *pctx, struct v4l2_fmtdesc *pfmtdesc)
{
	D("%s\n", __func__);
	return 0;
}

static int msm_v4l2_enum_fmt_cap(struct file *f,
				 void *pctx, struct v4l2_fmtdesc *pfmtdesc)
{
	D("%s\n", __func__);

	switch (pfmtdesc->index) {
	case 0:
		pfmtdesc->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		pfmtdesc->flags = 0;
		strncpy(pfmtdesc->description, "YUV 4:2:0",
			sizeof(pfmtdesc->description));
		pfmtdesc->pixelformat = V4L2_PIX_FMT_YVU420;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int msm_v4l2_g_fmt_cap(struct file *f,
			      void *pctx, struct v4l2_format *pfmt)
{
	D("%s\n", __func__);
	pfmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	pfmt->fmt.pix.width = MSM_V4L2_WIDTH;
	pfmt->fmt.pix.height = MSM_V4L2_HEIGHT;
	pfmt->fmt.pix.pixelformat = V4L2_PIX_FMT_YVU420;
	pfmt->fmt.pix.field = V4L2_FIELD_ANY;
	pfmt->fmt.pix.bytesperline = 0;
	pfmt->fmt.pix.sizeimage = 0;
	pfmt->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	pfmt->fmt.pix.priv = 0;
	return 0;
}

static int msm_v4l2_s_fmt_cap(struct file *f,
			      void *pctx, struct v4l2_format *pfmt)
{
  struct msm_ctrl_cmd *ctrlcmd;

	D("%s\n", __func__);

	ctrlcmd = kmalloc(sizeof(struct msm_ctrl_cmd), GFP_ATOMIC);
	if (!ctrlcmd) {
		CDBG("msm_v4l2_s_fmt_cap: cannot allocate buffer\n");
		return -ENOMEM;
	}

  ctrlcmd->type       = MSM_V4L2_VID_CAP_TYPE;
  ctrlcmd->length     = sizeof(struct v4l2_format);
  ctrlcmd->value      = pfmt;
  ctrlcmd->timeout_ms = 10000;

	if (pfmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		kfree(ctrlcmd);
		return -1;
	}

#if 0
	/* FIXEME */
	if (pfmt->fmt.pix.pixelformat != V4L2_PIX_FMT_YVU420) {
		kfree(ctrlcmd);
		return -EINVAL;
	}
#endif

	/* Ok, but check other params, too. */

#if 0
	memcpy(&g_pmsm_v4l2_dev->current_pix_format.fmt.pix, pfmt,
	       sizeof(struct v4l2_format));
#endif

	g_pmsm_v4l2_dev->drv->ctrl(g_pmsm_v4l2_dev->drv->sync, ctrlcmd);

	return 0;
}

static int msm_v4l2_g_fmt_overlay(struct file *f,
				  void *pctx, struct v4l2_format *pfmt)
{
	D("%s\n", __func__);
	pfmt->type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
	pfmt->fmt.pix.width = MSM_V4L2_WIDTH;
	pfmt->fmt.pix.height = MSM_V4L2_HEIGHT;
	pfmt->fmt.pix.pixelformat = V4L2_PIX_FMT_YVU420;
	pfmt->fmt.pix.field = V4L2_FIELD_ANY;
	pfmt->fmt.pix.bytesperline = 0;
	pfmt->fmt.pix.sizeimage = 0;
	pfmt->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	pfmt->fmt.pix.priv = 0;
	return 0;
}

static int msm_v4l2_s_fmt_overlay(struct file *f,
				  void *pctx, struct v4l2_format *pfmt)
{
	D("%s\n", __func__);
	return 0;
}

static int msm_v4l2_overlay(struct file *f, void *pctx, unsigned int i)
{
	D("%s\n", __func__);
	return 0;
}

static int msm_v4l2_g_jpegcomp(struct file *f,
			       void *pctx, struct v4l2_jpegcompression *pcomp)
{
	D("%s\n", __func__);
	return 0;
}

static int msm_v4l2_s_jpegcomp(struct file *f,
			       void *pctx, struct v4l2_jpegcompression *pcomp)
{
	D("%s\n", __func__);
	return 0;
}

#ifdef CONFIG_PROC_FS
int msm_v4l2_read_proc(char *pbuf, char **start, off_t offset,
		       int count, int *eof, void *data)
{
	int len = 0;
	len += snprintf(pbuf, strlen("stats\n") + 1, "stats\n");

	if (g_pmsm_v4l2_dev) {
		len += snprintf(pbuf, strlen("mode: ") + 1, "mode: ");

		if (g_pmsm_v4l2_dev->current_cap_format.type
		    == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			len += snprintf(pbuf, strlen("capture\n") + 1,
					"capture\n");
		else
			len += snprintf(pbuf, strlen("unknown\n") + 1,
					"unknown\n");

		len += snprintf(pbuf, 21, "resolution: %dx%d\n",
				g_pmsm_v4l2_dev->current_cap_format.fmt.pix.
				width,
				g_pmsm_v4l2_dev->current_cap_format.fmt.pix.
				height);

		len += snprintf(pbuf,
				strlen("pixel format: ") + 1, "pixel format: ");
		if (g_pmsm_v4l2_dev->current_cap_format.fmt.pix.pixelformat
		    == V4L2_PIX_FMT_YVU420)
			len += snprintf(pbuf, strlen("yvu420\n") + 1,
					"yvu420\n");
		else
			len += snprintf(pbuf, strlen("unknown\n") + 1,
					"unknown\n");

		len += snprintf(pbuf, strlen("colorspace: ") + 1,
				"colorspace: ");
		if (g_pmsm_v4l2_dev->current_cap_format.fmt.pix.colorspace
		    == V4L2_COLORSPACE_JPEG)
			len += snprintf(pbuf, strlen("jpeg\n") + 1, "jpeg\n");
		else
			len += snprintf(pbuf, strlen("unknown\n") + 1,
					"unknown\n");
	}

	*eof = 1;
	return len;
}
#endif

static const struct v4l2_file_operations msm_v4l2_fops = {
	.owner = THIS_MODULE,
	.open = msm_v4l2_open,
	.poll = msm_v4l2_poll,
	.release = msm_v4l2_release,
	.ioctl = msm_v4l2_ioctl,
};

static void msm_v4l2_dev_init(struct msm_v4l2_device *pmsm_v4l2_dev)
{
	pmsm_v4l2_dev->read_queue_lock =
	    __SPIN_LOCK_UNLOCKED(pmsm_v4l2_dev->read_queue_lock);
	INIT_LIST_HEAD(&pmsm_v4l2_dev->read_queue);
}

static int msm_v4l2_try_fmt_cap(struct file *file,
				 void *fh, struct v4l2_format *f)
{
	/* FIXME */
	return 0;
}

static int mm_v4l2_try_fmt_type_private(struct file *file,
					 void *fh, struct v4l2_format *f)
{
	/* FIXME */
	return 0;
}

/*
 * should the following structure be used instead of the code in the function?
 * static const struct v4l2_ioctl_ops msm_v4l2_ioctl_ops = {
 *     .vidioc_querycap = ....
 * }
 */
static const struct v4l2_ioctl_ops msm_ioctl_ops = {
	.vidioc_querycap = msm_v4l2_querycap,
	.vidioc_s_std = msm_v4l2_s_std,

	.vidioc_queryctrl = msm_v4l2_queryctrl,
	.vidioc_g_ctrl = msm_v4l2_g_ctrl,
	.vidioc_s_ctrl = msm_v4l2_s_ctrl,

	.vidioc_reqbufs = msm_v4l2_reqbufs,
	.vidioc_querybuf = msm_v4l2_querybuf,
	.vidioc_qbuf = msm_v4l2_qbuf,
	.vidioc_dqbuf = msm_v4l2_dqbuf,

	.vidioc_streamon = msm_v4l2_streamon,
	.vidioc_streamoff = msm_v4l2_streamoff,

	.vidioc_enum_fmt_vid_overlay = msm_v4l2_enum_fmt_overlay,
	.vidioc_enum_fmt_vid_cap = msm_v4l2_enum_fmt_cap,

	.vidioc_try_fmt_vid_cap = msm_v4l2_try_fmt_cap,
	.vidioc_try_fmt_type_private = mm_v4l2_try_fmt_type_private,

	.vidioc_g_fmt_vid_cap = msm_v4l2_g_fmt_cap,
	.vidioc_s_fmt_vid_cap = msm_v4l2_s_fmt_cap,
	.vidioc_g_fmt_vid_overlay = msm_v4l2_g_fmt_overlay,
	.vidioc_s_fmt_vid_overlay = msm_v4l2_s_fmt_overlay,
	.vidioc_overlay = msm_v4l2_overlay,

	.vidioc_g_jpegcomp = msm_v4l2_g_jpegcomp,
	.vidioc_s_jpegcomp = msm_v4l2_s_jpegcomp,
};

static int msm_v4l2_video_dev_init(struct video_device *pvd)
{
	strncpy(pvd->name, MSM_APPS_ID_V4L2, sizeof(pvd->name));
	pvd->vfl_type = 1;
	pvd->fops = &msm_v4l2_fops;
	pvd->release = msm_v4l2_release_dev;
	pvd->minor = -1;
	pvd->ioctl_ops = &msm_ioctl_ops;
	return msm_v4l2_register(g_pmsm_v4l2_dev->drv);
}

static int __init msm_v4l2_init(void)
{
	int rc = -ENOMEM;
	struct video_device *pvdev = NULL;
	struct msm_v4l2_device *pmsm_v4l2_dev = NULL;
	D("%s\n", __func__);

	pvdev = video_device_alloc();
	if (pvdev == NULL)
		return rc;

	pmsm_v4l2_dev =
		kzalloc(sizeof(struct msm_v4l2_device), GFP_KERNEL);
	if (pmsm_v4l2_dev == NULL) {
		video_device_release(pvdev);
		return rc;
	}

	msm_v4l2_dev_init(pmsm_v4l2_dev);

	g_pmsm_v4l2_dev = pmsm_v4l2_dev;
	g_pmsm_v4l2_dev->pvdev = pvdev;

	g_pmsm_v4l2_dev->drv =
		kzalloc(sizeof(struct msm_v4l2_driver), GFP_KERNEL);
	if (!g_pmsm_v4l2_dev->drv) {
		video_device_release(pvdev);
		kfree(pmsm_v4l2_dev);
		return rc;
	}

	rc = msm_v4l2_video_dev_init(pvdev);
	if (rc < 0) {
		video_device_release(pvdev);
		kfree(g_pmsm_v4l2_dev->drv);
		kfree(pmsm_v4l2_dev);
		return rc;
	}

	if (video_register_device(pvdev, VFL_TYPE_GRABBER,
	    MSM_V4L2_DEVNUM_YUV)) {
		D("failed to register device\n");
		video_device_release(pvdev);
		kfree(g_pmsm_v4l2_dev);
		g_pmsm_v4l2_dev = NULL;
		return -ENOENT;
	}
#ifdef CONFIG_PROC_FS
	create_proc_read_entry(MSM_V4L2_PROC_NAME,
			       0, NULL, msm_v4l2_read_proc, NULL);
#endif

	return 0;
}

static void __exit msm_v4l2_exit(void)
{
	struct video_device *pvdev = g_pmsm_v4l2_dev->pvdev;
	D("%s\n", __func__);
#ifdef CONFIG_PROC_FS
	remove_proc_entry(MSM_V4L2_PROC_NAME, NULL);
#endif
	video_unregister_device(pvdev);
	video_device_release(pvdev);

	msm_v4l2_unregister(g_pmsm_v4l2_dev->drv);

	kfree(g_pmsm_v4l2_dev->drv);
	g_pmsm_v4l2_dev->drv = NULL;

	kfree(g_pmsm_v4l2_dev);
	g_pmsm_v4l2_dev = NULL;
}

module_init(msm_v4l2_init);
module_exit(msm_v4l2_exit);

MODULE_DESCRIPTION("MSM V4L2 driver");
MODULE_LICENSE("GPL v2");
