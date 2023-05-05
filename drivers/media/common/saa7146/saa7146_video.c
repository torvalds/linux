#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <media/drv-intf/saa7146_vv.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ctrls.h>
#include <linux/module.h>
#include <linux/kernel.h>

/* format descriptions for capture and preview */
static struct saa7146_format formats[] = {
	{
		.pixelformat	= V4L2_PIX_FMT_RGB332,
		.trans		= RGB08_COMPOSED,
		.depth		= 8,
		.flags		= 0,
	}, {
		.pixelformat	= V4L2_PIX_FMT_RGB565,
		.trans		= RGB16_COMPOSED,
		.depth		= 16,
		.flags		= 0,
	}, {
		.pixelformat	= V4L2_PIX_FMT_BGR24,
		.trans		= RGB24_COMPOSED,
		.depth		= 24,
		.flags		= 0,
	}, {
		.pixelformat	= V4L2_PIX_FMT_BGR32,
		.trans		= RGB32_COMPOSED,
		.depth		= 32,
		.flags		= 0,
	}, {
		.pixelformat	= V4L2_PIX_FMT_RGB32,
		.trans		= RGB32_COMPOSED,
		.depth		= 32,
		.flags		= 0,
		.swap		= 0x2,
	}, {
		.pixelformat	= V4L2_PIX_FMT_GREY,
		.trans		= Y8,
		.depth		= 8,
		.flags		= 0,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YUV422P,
		.trans		= YUV422_DECOMPOSED,
		.depth		= 16,
		.flags		= FORMAT_IS_PLANAR,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YVU420,
		.trans		= YUV420_DECOMPOSED,
		.depth		= 12,
		.flags		= FORMAT_BYTE_SWAP|FORMAT_IS_PLANAR,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YUV420,
		.trans		= YUV420_DECOMPOSED,
		.depth		= 12,
		.flags		= FORMAT_IS_PLANAR,
	}, {
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.trans		= YUV422_COMPOSED,
		.depth		= 16,
		.flags		= 0,
	}
};

/* unfortunately, the saa7146 contains a bug which prevents it from doing on-the-fly byte swaps.
   due to this, it's impossible to provide additional *packed* formats, which are simply byte swapped
   (like V4L2_PIX_FMT_YUYV) ... 8-( */

struct saa7146_format* saa7146_format_by_fourcc(struct saa7146_dev *dev, int fourcc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i].pixelformat == fourcc) {
			return formats+i;
		}
	}

	DEB_D("unknown pixelformat:'%4.4s'\n", (char *)&fourcc);
	return NULL;
}

/********************************************************************************/
/* common pagetable functions */

static int saa7146_pgtable_build(struct saa7146_dev *dev, struct saa7146_buf *buf)
{
	struct saa7146_vv *vv = dev->vv_data;
	struct pci_dev *pci = dev->pci;
	struct sg_table *sgt = vb2_dma_sg_plane_desc(&buf->vb.vb2_buf, 0);
	struct scatterlist *list = sgt->sgl;
	int length = sgt->nents;
	struct v4l2_pix_format *pix = &vv->video_fmt;
	struct saa7146_format *sfmt = saa7146_format_by_fourcc(dev, pix->pixelformat);

	DEB_EE("dev:%p, buf:%p, sg_len:%d\n", dev, buf, length);

	if( 0 != IS_PLANAR(sfmt->trans)) {
		struct saa7146_pgtable *pt1 = &buf->pt[0];
		struct saa7146_pgtable *pt2 = &buf->pt[1];
		struct saa7146_pgtable *pt3 = &buf->pt[2];
		struct sg_dma_page_iter dma_iter;
		__le32  *ptr1, *ptr2, *ptr3;
		__le32 fill;

		int size = pix->width * pix->height;
		int i, m1, m2, m3, o1, o2;

		switch( sfmt->depth ) {
			case 12: {
				/* create some offsets inside the page table */
				m1 = ((size + PAGE_SIZE) / PAGE_SIZE) - 1;
				m2 = ((size + (size / 4) + PAGE_SIZE) / PAGE_SIZE) - 1;
				m3 = ((size + (size / 2) + PAGE_SIZE) / PAGE_SIZE) - 1;
				o1 = size % PAGE_SIZE;
				o2 = (size + (size / 4)) % PAGE_SIZE;
				DEB_CAP("size:%d, m1:%d, m2:%d, m3:%d, o1:%d, o2:%d\n",
					size, m1, m2, m3, o1, o2);
				break;
			}
			case 16: {
				/* create some offsets inside the page table */
				m1 = ((size + PAGE_SIZE) / PAGE_SIZE) - 1;
				m2 = ((size + (size / 2) + PAGE_SIZE) / PAGE_SIZE) - 1;
				m3 = ((2 * size + PAGE_SIZE) / PAGE_SIZE) - 1;
				o1 = size % PAGE_SIZE;
				o2 = (size + (size / 2)) % PAGE_SIZE;
				DEB_CAP("size:%d, m1:%d, m2:%d, m3:%d, o1:%d, o2:%d\n",
					size, m1, m2, m3, o1, o2);
				break;
			}
			default: {
				return -1;
			}
		}

		ptr1 = pt1->cpu;
		ptr2 = pt2->cpu;
		ptr3 = pt3->cpu;

		for_each_sg_dma_page(list, &dma_iter, length, 0)
			*ptr1++ = cpu_to_le32(sg_page_iter_dma_address(&dma_iter) - list->offset);

		/* if we have a user buffer, the first page may not be
		   aligned to a page boundary. */
		pt1->offset = sgt->sgl->offset;
		pt2->offset = pt1->offset + o1;
		pt3->offset = pt1->offset + o2;

		/* create video-dma2 page table */
		ptr1 = pt1->cpu;
		for (i = m1; i <= m2; i++, ptr2++)
			*ptr2 = ptr1[i];
		fill = *(ptr2 - 1);
		for (; i < 1024; i++, ptr2++)
			*ptr2 = fill;
		/* create video-dma3 page table */
		ptr1 = pt1->cpu;
		for (i = m2; i <= m3; i++, ptr3++)
			*ptr3 = ptr1[i];
		fill = *(ptr3 - 1);
		for (; i < 1024; i++, ptr3++)
			*ptr3 = fill;
		/* finally: finish up video-dma1 page table */
		ptr1 = pt1->cpu + m1;
		fill = pt1->cpu[m1];
		for (i = m1; i < 1024; i++, ptr1++)
			*ptr1 = fill;
	} else {
		struct saa7146_pgtable *pt = &buf->pt[0];

		return saa7146_pgtable_build_single(pci, pt, list, length);
	}

	return 0;
}


/********************************************************************************/
/* file operations */

static int video_begin(struct saa7146_dev *dev)
{
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_format *fmt = NULL;
	unsigned int resource;
	int ret = 0;

	DEB_EE("dev:%p\n", dev);

	fmt = saa7146_format_by_fourcc(dev, vv->video_fmt.pixelformat);
	/* we need to have a valid format set here */
	if (!fmt)
		return -EINVAL;

	if (0 != (fmt->flags & FORMAT_IS_PLANAR)) {
		resource = RESOURCE_DMA1_HPS|RESOURCE_DMA2_CLP|RESOURCE_DMA3_BRS;
	} else {
		resource = RESOURCE_DMA1_HPS;
	}

	ret = saa7146_res_get(dev, resource);
	if (0 == ret) {
		DEB_S("cannot get capture resource %d\n", resource);
		return -EBUSY;
	}

	/* clear out beginning of streaming bit (rps register 0)*/
	saa7146_write(dev, MC2, MASK_27 );

	/* enable rps0 irqs */
	SAA7146_IER_ENABLE(dev, MASK_27);

	return 0;
}

static void video_end(struct saa7146_dev *dev)
{
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_format *fmt = NULL;
	unsigned long flags;
	unsigned int resource;
	u32 dmas = 0;
	DEB_EE("dev:%p\n", dev);

	fmt = saa7146_format_by_fourcc(dev, vv->video_fmt.pixelformat);
	/* we need to have a valid format set here */
	if (!fmt)
		return;

	if (0 != (fmt->flags & FORMAT_IS_PLANAR)) {
		resource = RESOURCE_DMA1_HPS|RESOURCE_DMA2_CLP|RESOURCE_DMA3_BRS;
		dmas = MASK_22 | MASK_21 | MASK_20;
	} else {
		resource = RESOURCE_DMA1_HPS;
		dmas = MASK_22;
	}
	spin_lock_irqsave(&dev->slock,flags);

	/* disable rps0  */
	saa7146_write(dev, MC1, MASK_28);

	/* disable rps0 irqs */
	SAA7146_IER_DISABLE(dev, MASK_27);

	/* shut down all used video dma transfers */
	saa7146_write(dev, MC1, dmas);

	spin_unlock_irqrestore(&dev->slock, flags);

	saa7146_res_free(dev, resource);
}

static int vidioc_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct saa7146_dev *dev = video_drvdata(file);

	strscpy((char *)cap->driver, "saa7146 v4l2", sizeof(cap->driver));
	strscpy((char *)cap->card, dev->ext->name, sizeof(cap->card));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE |
			    V4L2_CAP_READWRITE | V4L2_CAP_STREAMING |
			    V4L2_CAP_DEVICE_CAPS;
	cap->capabilities |= dev->ext_vv_data->capabilities;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(formats))
		return -EINVAL;
	f->pixelformat = formats[f->index].pixelformat;
	return 0;
}

int saa7146_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct saa7146_dev *dev = container_of(ctrl->handler,
				struct saa7146_dev, ctrl_handler);
	struct saa7146_vv *vv = dev->vv_data;
	u32 val;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		val = saa7146_read(dev, BCS_CTRL);
		val &= 0x00ffffff;
		val |= (ctrl->val << 24);
		saa7146_write(dev, BCS_CTRL, val);
		saa7146_write(dev, MC2, MASK_22 | MASK_06);
		break;

	case V4L2_CID_CONTRAST:
		val = saa7146_read(dev, BCS_CTRL);
		val &= 0xff00ffff;
		val |= (ctrl->val << 16);
		saa7146_write(dev, BCS_CTRL, val);
		saa7146_write(dev, MC2, MASK_22 | MASK_06);
		break;

	case V4L2_CID_SATURATION:
		val = saa7146_read(dev, BCS_CTRL);
		val &= 0xffffff00;
		val |= (ctrl->val << 0);
		saa7146_write(dev, BCS_CTRL, val);
		saa7146_write(dev, MC2, MASK_22 | MASK_06);
		break;

	case V4L2_CID_HFLIP:
		/* fixme: we can support changing VFLIP and HFLIP here... */
		if (vb2_is_busy(&vv->video_dmaq.q))
			return -EBUSY;
		vv->hflip = ctrl->val;
		break;

	case V4L2_CID_VFLIP:
		if (vb2_is_busy(&vv->video_dmaq.q))
			return -EBUSY;
		vv->vflip = ctrl->val;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_g_parm(struct file *file, void *fh,
		struct v4l2_streamparm *parm)
{
	struct saa7146_dev *dev = video_drvdata(file);
	struct saa7146_vv *vv = dev->vv_data;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	parm->parm.capture.readbuffers = 1;
	v4l2_video_std_frame_period(vv->standard->id,
				    &parm->parm.capture.timeperframe);
	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct saa7146_dev *dev = video_drvdata(file);
	struct saa7146_vv *vv = dev->vv_data;

	f->fmt.pix = vv->video_fmt;
	return 0;
}

static int vidioc_g_fmt_vbi_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct saa7146_dev *dev = video_drvdata(file);
	struct saa7146_vv *vv = dev->vv_data;

	f->fmt.vbi = vv->vbi_fmt;
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct saa7146_dev *dev = video_drvdata(file);
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_format *fmt;
	enum v4l2_field field;
	int maxw, maxh;
	int calc_bpl;

	DEB_EE("V4L2_BUF_TYPE_VIDEO_CAPTURE: dev:%p, fh:%p\n", dev, fh);

	fmt = saa7146_format_by_fourcc(dev, f->fmt.pix.pixelformat);
	if (NULL == fmt)
		return -EINVAL;

	field = f->fmt.pix.field;
	maxw  = vv->standard->h_max_out;
	maxh  = vv->standard->v_max_out;

	if (V4L2_FIELD_ANY == field) {
		field = (f->fmt.pix.height > maxh / 2)
			? V4L2_FIELD_INTERLACED
			: V4L2_FIELD_BOTTOM;
	}
	switch (field) {
	case V4L2_FIELD_ALTERNATE:
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
		maxh = maxh / 2;
		break;
	default:
		field = V4L2_FIELD_INTERLACED;
		break;
	}

	f->fmt.pix.field = field;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	if (f->fmt.pix.width < 48)
		f->fmt.pix.width = 48;
	if (f->fmt.pix.height < 32)
		f->fmt.pix.height = 32;
	if (f->fmt.pix.width > maxw)
		f->fmt.pix.width = maxw;
	if (f->fmt.pix.height > maxh)
		f->fmt.pix.height = maxh;

	calc_bpl = (f->fmt.pix.width * fmt->depth) / 8;

	if (f->fmt.pix.bytesperline < calc_bpl)
		f->fmt.pix.bytesperline = calc_bpl;

	if (f->fmt.pix.bytesperline > (2 * PAGE_SIZE * fmt->depth) / 8) /* arbitrary constraint */
		f->fmt.pix.bytesperline = calc_bpl;

	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;
	DEB_D("w:%d, h:%d, bytesperline:%d, sizeimage:%d\n",
	      f->fmt.pix.width, f->fmt.pix.height,
	      f->fmt.pix.bytesperline, f->fmt.pix.sizeimage);

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct saa7146_dev *dev = video_drvdata(file);
	struct saa7146_vv *vv = dev->vv_data;
	int err;

	DEB_EE("V4L2_BUF_TYPE_VIDEO_CAPTURE: dev:%p\n", dev);
	if (vb2_is_busy(&vv->video_dmaq.q)) {
		DEB_EE("streaming capture is active\n");
		return -EBUSY;
	}
	err = vidioc_try_fmt_vid_cap(file, fh, f);
	if (0 != err)
		return err;
	switch (f->fmt.pix.field) {
	case V4L2_FIELD_ALTERNATE:
		vv->last_field = V4L2_FIELD_TOP;
		break;
	default:
		vv->last_field = V4L2_FIELD_INTERLACED;
		break;
	}
	vv->video_fmt = f->fmt.pix;
	DEB_EE("set to pixelformat '%4.4s'\n",
	       (char *)&vv->video_fmt.pixelformat);
	return 0;
}

static int vidioc_g_std(struct file *file, void *fh, v4l2_std_id *norm)
{
	struct saa7146_dev *dev = video_drvdata(file);
	struct saa7146_vv *vv = dev->vv_data;

	*norm = vv->standard->id;
	return 0;
}

static int vidioc_s_std(struct file *file, void *fh, v4l2_std_id id)
{
	struct saa7146_dev *dev = video_drvdata(file);
	struct saa7146_vv *vv = dev->vv_data;
	int found = 0;
	int i;

	DEB_EE("VIDIOC_S_STD\n");

	for (i = 0; i < dev->ext_vv_data->num_stds; i++)
		if (id & dev->ext_vv_data->stds[i].id)
			break;

	if (i != dev->ext_vv_data->num_stds &&
	    vv->standard == &dev->ext_vv_data->stds[i])
		return 0;

	if (vb2_is_busy(&vv->video_dmaq.q) || vb2_is_busy(&vv->vbi_dmaq.q)) {
		DEB_D("cannot change video standard while streaming capture is active\n");
		return -EBUSY;
	}

	if (i != dev->ext_vv_data->num_stds) {
		vv->standard = &dev->ext_vv_data->stds[i];
		if (NULL != dev->ext_vv_data->std_callback)
			dev->ext_vv_data->std_callback(dev, vv->standard);
		found = 1;
	}

	if (!found) {
		DEB_EE("VIDIOC_S_STD: standard not found\n");
		return -EINVAL;
	}

	DEB_EE("VIDIOC_S_STD: set to standard to '%s'\n", vv->standard->name);
	return 0;
}

const struct v4l2_ioctl_ops saa7146_video_ioctl_ops = {
	.vidioc_querycap             = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap     = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap        = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap      = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap        = vidioc_s_fmt_vid_cap,
	.vidioc_g_std                = vidioc_g_std,
	.vidioc_s_std                = vidioc_s_std,
	.vidioc_g_parm		     = vidioc_g_parm,
	.vidioc_reqbufs		     = vb2_ioctl_reqbufs,
	.vidioc_create_bufs	     = vb2_ioctl_create_bufs,
	.vidioc_querybuf	     = vb2_ioctl_querybuf,
	.vidioc_qbuf		     = vb2_ioctl_qbuf,
	.vidioc_dqbuf		     = vb2_ioctl_dqbuf,
	.vidioc_streamon	     = vb2_ioctl_streamon,
	.vidioc_streamoff	     = vb2_ioctl_streamoff,
	.vidioc_subscribe_event      = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event    = v4l2_event_unsubscribe,
};

const struct v4l2_ioctl_ops saa7146_vbi_ioctl_ops = {
	.vidioc_querycap             = vidioc_querycap,
	.vidioc_g_fmt_vbi_cap        = vidioc_g_fmt_vbi_cap,
	.vidioc_try_fmt_vbi_cap      = vidioc_g_fmt_vbi_cap,
	.vidioc_s_fmt_vbi_cap        = vidioc_g_fmt_vbi_cap,
	.vidioc_g_std                = vidioc_g_std,
	.vidioc_s_std                = vidioc_s_std,
	.vidioc_g_parm		     = vidioc_g_parm,
	.vidioc_reqbufs		     = vb2_ioctl_reqbufs,
	.vidioc_create_bufs	     = vb2_ioctl_create_bufs,
	.vidioc_querybuf	     = vb2_ioctl_querybuf,
	.vidioc_qbuf		     = vb2_ioctl_qbuf,
	.vidioc_dqbuf		     = vb2_ioctl_dqbuf,
	.vidioc_streamon	     = vb2_ioctl_streamon,
	.vidioc_streamoff	     = vb2_ioctl_streamoff,
	.vidioc_subscribe_event      = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event    = v4l2_event_unsubscribe,
};

/*********************************************************************************/
/* buffer handling functions                                                  */

static int buffer_activate (struct saa7146_dev *dev,
		     struct saa7146_buf *buf,
		     struct saa7146_buf *next)
{
	struct saa7146_vv *vv = dev->vv_data;

	saa7146_set_capture(dev,buf,next);

	mod_timer(&vv->video_dmaq.timeout, jiffies+BUFFER_TIMEOUT);
	return 0;
}

static void release_all_pagetables(struct saa7146_dev *dev, struct saa7146_buf *buf)
{
	saa7146_pgtable_free(dev->pci, &buf->pt[0]);
	saa7146_pgtable_free(dev->pci, &buf->pt[1]);
	saa7146_pgtable_free(dev->pci, &buf->pt[2]);
}

static int queue_setup(struct vb2_queue *q,
		       unsigned int *num_buffers, unsigned int *num_planes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	struct saa7146_dev *dev = vb2_get_drv_priv(q);
	unsigned int size = dev->vv_data->video_fmt.sizeimage;

	if (*num_planes)
		return sizes[0] < size ? -EINVAL : 0;
	*num_planes = 1;
	sizes[0] = size;

	return 0;
}

static void buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct saa7146_dev *dev = vb2_get_drv_priv(vq);
	struct saa7146_buf *buf = container_of(vbuf, struct saa7146_buf, vb);
	unsigned long flags;

	spin_lock_irqsave(&dev->slock, flags);

	saa7146_buffer_queue(dev, &dev->vv_data->video_dmaq, buf);
	spin_unlock_irqrestore(&dev->slock, flags);
}

static int buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct saa7146_buf *buf = container_of(vbuf, struct saa7146_buf, vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct saa7146_dev *dev = vb2_get_drv_priv(vq);
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_format *sfmt;
	int ret;

	buf->activate = buffer_activate;
	sfmt = saa7146_format_by_fourcc(dev, vv->video_fmt.pixelformat);

	if (IS_PLANAR(sfmt->trans)) {
		saa7146_pgtable_alloc(dev->pci, &buf->pt[0]);
		saa7146_pgtable_alloc(dev->pci, &buf->pt[1]);
		saa7146_pgtable_alloc(dev->pci, &buf->pt[2]);
	} else {
		saa7146_pgtable_alloc(dev->pci, &buf->pt[0]);
	}

	ret = saa7146_pgtable_build(dev, buf);
	if (ret)
		release_all_pagetables(dev, buf);
	return ret;
}

static int buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct saa7146_dev *dev = vb2_get_drv_priv(vq);
	struct saa7146_vv *vv = dev->vv_data;
	unsigned int size = vv->video_fmt.sizeimage;

	if (vb2_plane_size(vb, 0) < size)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, size);
	return 0;
}

static void buf_cleanup(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct saa7146_buf *buf = container_of(vbuf, struct saa7146_buf, vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct saa7146_dev *dev = vb2_get_drv_priv(vq);

	release_all_pagetables(dev, buf);
}

static void return_buffers(struct vb2_queue *q, int state)
{
	struct saa7146_dev *dev = vb2_get_drv_priv(q);
	struct saa7146_dmaqueue *dq = &dev->vv_data->video_dmaq;
	struct saa7146_buf *buf;

	if (dq->curr) {
		buf = dq->curr;
		dq->curr = NULL;
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	while (!list_empty(&dq->queue)) {
		buf = list_entry(dq->queue.next, struct saa7146_buf, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
}

static int start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct saa7146_dev *dev = vb2_get_drv_priv(q);
	int ret;

	if (!vb2_is_streaming(&dev->vv_data->video_dmaq.q))
		dev->vv_data->seqnr = 0;
	ret = video_begin(dev);
	if (ret)
		return_buffers(q, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void stop_streaming(struct vb2_queue *q)
{
	struct saa7146_dev *dev = vb2_get_drv_priv(q);
	struct saa7146_dmaqueue *dq = &dev->vv_data->video_dmaq;

	del_timer(&dq->timeout);
	video_end(dev);
	return_buffers(q, VB2_BUF_STATE_ERROR);
}

const struct vb2_ops video_qops = {
	.queue_setup	= queue_setup,
	.buf_queue	= buf_queue,
	.buf_init	= buf_init,
	.buf_prepare	= buf_prepare,
	.buf_cleanup	= buf_cleanup,
	.start_streaming = start_streaming,
	.stop_streaming = stop_streaming,
	.wait_prepare	= vb2_ops_wait_prepare,
	.wait_finish	= vb2_ops_wait_finish,
};

/********************************************************************************/
/* file operations */

static void video_init(struct saa7146_dev *dev, struct saa7146_vv *vv)
{
	INIT_LIST_HEAD(&vv->video_dmaq.queue);

	timer_setup(&vv->video_dmaq.timeout, saa7146_buffer_timeout, 0);
	vv->video_dmaq.dev              = dev;

	/* set some default values */
	vv->standard = &dev->ext_vv_data->stds[0];

	/* FIXME: what's this? */
	vv->current_hps_source = SAA7146_HPS_SOURCE_PORT_A;
	vv->current_hps_sync = SAA7146_HPS_SYNC_PORT_A;
}

static void video_irq_done(struct saa7146_dev *dev, unsigned long st)
{
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_dmaqueue *q = &vv->video_dmaq;

	spin_lock(&dev->slock);
	DEB_CAP("called\n");

	/* only finish the buffer if we have one... */
	if (q->curr)
		saa7146_buffer_finish(dev, q, VB2_BUF_STATE_DONE);
	saa7146_buffer_next(dev,q,0);

	spin_unlock(&dev->slock);
}

const struct saa7146_use_ops saa7146_video_uops = {
	.init = video_init,
	.irq_done = video_irq_done,
};
