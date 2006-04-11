#include <media/saa7146_vv.h>

static int max_memory = 32;

module_param(max_memory, int, 0644);
MODULE_PARM_DESC(max_memory, "maximum memory usage for capture buffers (default: 32Mb)");

#define IS_CAPTURE_ACTIVE(fh) \
	(((vv->video_status & STATUS_CAPTURE) != 0) && (vv->video_fh == fh))

#define IS_OVERLAY_ACTIVE(fh) \
	(((vv->video_status & STATUS_OVERLAY) != 0) && (vv->video_fh == fh))

/* format descriptions for capture and preview */
static struct saa7146_format formats[] = {
	{
		.name		= "RGB-8 (3-3-2)",
		.pixelformat	= V4L2_PIX_FMT_RGB332,
		.trans		= RGB08_COMPOSED,
		.depth		= 8,
		.flags		= 0,
	}, {
		.name		= "RGB-16 (5/B-6/G-5/R)",
		.pixelformat	= V4L2_PIX_FMT_RGB565,
		.trans		= RGB16_COMPOSED,
		.depth		= 16,
		.flags		= 0,
	}, {
		.name		= "RGB-24 (B-G-R)",
		.pixelformat	= V4L2_PIX_FMT_BGR24,
		.trans		= RGB24_COMPOSED,
		.depth		= 24,
		.flags		= 0,
	}, {
		.name		= "RGB-32 (B-G-R)",
		.pixelformat	= V4L2_PIX_FMT_BGR32,
		.trans		= RGB32_COMPOSED,
		.depth		= 32,
		.flags		= 0,
	}, {
		.name		= "RGB-32 (R-G-B)",
		.pixelformat	= V4L2_PIX_FMT_RGB32,
		.trans		= RGB32_COMPOSED,
		.depth		= 32,
		.flags		= 0,
		.swap		= 0x2,
	}, {
		.name		= "Greyscale-8",
		.pixelformat	= V4L2_PIX_FMT_GREY,
		.trans		= Y8,
		.depth		= 8,
		.flags		= 0,
	}, {
		.name		= "YUV 4:2:2 planar (Y-Cb-Cr)",
		.pixelformat	= V4L2_PIX_FMT_YUV422P,
		.trans		= YUV422_DECOMPOSED,
		.depth		= 16,
		.flags		= FORMAT_BYTE_SWAP|FORMAT_IS_PLANAR,
	}, {
		.name		= "YVU 4:2:0 planar (Y-Cb-Cr)",
		.pixelformat	= V4L2_PIX_FMT_YVU420,
		.trans		= YUV420_DECOMPOSED,
		.depth		= 12,
		.flags		= FORMAT_BYTE_SWAP|FORMAT_IS_PLANAR,
	}, {
		.name		= "YUV 4:2:0 planar (Y-Cb-Cr)",
		.pixelformat	= V4L2_PIX_FMT_YUV420,
		.trans		= YUV420_DECOMPOSED,
		.depth		= 12,
		.flags		= FORMAT_IS_PLANAR,
	}, {
		.name		= "YUV 4:2:2 (U-Y-V-Y)",
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.trans		= YUV422_COMPOSED,
		.depth		= 16,
		.flags		= 0,
	}
};

/* unfortunately, the saa7146 contains a bug which prevents it from doing on-the-fly byte swaps.
   due to this, it's impossible to provide additional *packed* formats, which are simply byte swapped
   (like V4L2_PIX_FMT_YUYV) ... 8-( */

static int NUM_FORMATS = sizeof(formats)/sizeof(struct saa7146_format);

struct saa7146_format* format_by_fourcc(struct saa7146_dev *dev, int fourcc)
{
	int i, j = NUM_FORMATS;

	for (i = 0; i < j; i++) {
		if (formats[i].pixelformat == fourcc) {
			return formats+i;
		}
	}

	DEB_D(("unknown pixelformat:'%4.4s'\n",(char *)&fourcc));
	return NULL;
}

static int g_fmt(struct saa7146_fh *fh, struct v4l2_format *f)
{
	struct saa7146_dev *dev = fh->dev;
	DEB_EE(("dev:%p, fh:%p\n",dev,fh));

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		f->fmt.pix = fh->video_fmt;
		return 0;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		f->fmt.win = fh->ov.win;
		return 0;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	{
		f->fmt.vbi = fh->vbi_fmt;
		return 0;
	}
	default:
		DEB_D(("invalid format type '%d'.\n",f->type));
		return -EINVAL;
	}
}

static int try_win(struct saa7146_dev *dev, struct v4l2_window *win)
{
	struct saa7146_vv *vv = dev->vv_data;
	enum v4l2_field field;
	int maxw, maxh;

	DEB_EE(("dev:%p\n",dev));

	if (NULL == vv->ov_fb.base) {
		DEB_D(("no fb base set.\n"));
		return -EINVAL;
	}
	if (NULL == vv->ov_fmt) {
		DEB_D(("no fb fmt set.\n"));
		return -EINVAL;
	}
	if (win->w.width < 48 || win->w.height <  32) {
		DEB_D(("min width/height. (%d,%d)\n",win->w.width,win->w.height));
		return -EINVAL;
	}
	if (win->clipcount > 16) {
		DEB_D(("clipcount too big.\n"));
		return -EINVAL;
	}

	field = win->field;
	maxw  = vv->standard->h_max_out;
	maxh  = vv->standard->v_max_out;

	if (V4L2_FIELD_ANY == field) {
		field = (win->w.height > maxh/2)
			? V4L2_FIELD_INTERLACED
			: V4L2_FIELD_TOP;
		}
	switch (field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
	case V4L2_FIELD_ALTERNATE:
		maxh = maxh / 2;
		break;
	case V4L2_FIELD_INTERLACED:
		break;
	default: {
		DEB_D(("no known field mode '%d'.\n",field));
		return -EINVAL;
	}
	}

	win->field = field;
	if (win->w.width > maxw)
		win->w.width = maxw;
	if (win->w.height > maxh)
		win->w.height = maxh;

	return 0;
}

static int try_fmt(struct saa7146_fh *fh, struct v4l2_format *f)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	int err;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	{
		struct saa7146_format *fmt;
		enum v4l2_field field;
		int maxw, maxh;
		int calc_bpl;

		DEB_EE(("V4L2_BUF_TYPE_VIDEO_CAPTURE: dev:%p, fh:%p\n",dev,fh));

		fmt = format_by_fourcc(dev,f->fmt.pix.pixelformat);
		if (NULL == fmt) {
			return -EINVAL;
		}

		field = f->fmt.pix.field;
		maxw  = vv->standard->h_max_out;
		maxh  = vv->standard->v_max_out;

		if (V4L2_FIELD_ANY == field) {
			field = (f->fmt.pix.height > maxh/2)
				? V4L2_FIELD_INTERLACED
				: V4L2_FIELD_BOTTOM;
		}
		switch (field) {
		case V4L2_FIELD_ALTERNATE: {
			vv->last_field = V4L2_FIELD_TOP;
			maxh = maxh / 2;
			break;
		}
		case V4L2_FIELD_TOP:
		case V4L2_FIELD_BOTTOM:
			vv->last_field = V4L2_FIELD_INTERLACED;
			maxh = maxh / 2;
			break;
		case V4L2_FIELD_INTERLACED:
			vv->last_field = V4L2_FIELD_INTERLACED;
			break;
		default: {
			DEB_D(("no known field mode '%d'.\n",field));
			return -EINVAL;
		}
		}

		f->fmt.pix.field = field;
		if (f->fmt.pix.width > maxw)
			f->fmt.pix.width = maxw;
		if (f->fmt.pix.height > maxh)
			f->fmt.pix.height = maxh;

		calc_bpl = (f->fmt.pix.width * fmt->depth)/8;

		if (f->fmt.pix.bytesperline < calc_bpl)
			f->fmt.pix.bytesperline = calc_bpl;

		if (f->fmt.pix.bytesperline > (2*PAGE_SIZE * fmt->depth)/8) /* arbitrary constraint */
			f->fmt.pix.bytesperline = calc_bpl;

		f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;
		DEB_D(("w:%d, h:%d, bytesperline:%d, sizeimage:%d\n",f->fmt.pix.width,f->fmt.pix.height,f->fmt.pix.bytesperline,f->fmt.pix.sizeimage));

		return 0;
	}
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		DEB_EE(("V4L2_BUF_TYPE_VIDEO_OVERLAY: dev:%p, fh:%p\n",dev,fh));
		err = try_win(dev,&f->fmt.win);
		if (0 != err) {
			return err;
		}
		return 0;
	default:
		DEB_EE(("unknown format type '%d'\n",f->type));
		return -EINVAL;
	}
}

int saa7146_start_preview(struct saa7146_fh *fh)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	int ret = 0, err = 0;

	DEB_EE(("dev:%p, fh:%p\n",dev,fh));

	/* check if we have overlay informations */
	if( NULL == fh->ov.fh ) {
		DEB_D(("no overlay data available. try S_FMT first.\n"));
		return -EAGAIN;
	}

	/* check if streaming capture is running */
	if (IS_CAPTURE_ACTIVE(fh) != 0) {
		DEB_D(("streaming capture is active.\n"));
		return -EBUSY;
	}

	/* check if overlay is running */
	if (IS_OVERLAY_ACTIVE(fh) != 0) {
		if (vv->video_fh == fh) {
			DEB_D(("overlay is already active.\n"));
			return 0;
		}
		DEB_D(("overlay is already active in another open.\n"));
		return -EBUSY;
	}

	if (0 == saa7146_res_get(fh, RESOURCE_DMA1_HPS|RESOURCE_DMA2_CLP)) {
		DEB_D(("cannot get necessary overlay resources\n"));
		return -EBUSY;
	}

	err = try_win(dev,&fh->ov.win);
	if (0 != err) {
		saa7146_res_free(vv->video_fh, RESOURCE_DMA1_HPS|RESOURCE_DMA2_CLP);
		return -EBUSY;
	}

	vv->ov_data = &fh->ov;

	DEB_D(("%dx%d+%d+%d %s field=%s\n",
		fh->ov.win.w.width,fh->ov.win.w.height,
		fh->ov.win.w.left,fh->ov.win.w.top,
		vv->ov_fmt->name,v4l2_field_names[fh->ov.win.field]));

	if (0 != (ret = saa7146_enable_overlay(fh))) {
		DEB_D(("enabling overlay failed: %d\n",ret));
		saa7146_res_free(vv->video_fh, RESOURCE_DMA1_HPS|RESOURCE_DMA2_CLP);
		return ret;
	}

	vv->video_status = STATUS_OVERLAY;
	vv->video_fh = fh;

	return 0;
}
EXPORT_SYMBOL_GPL(saa7146_start_preview);

int saa7146_stop_preview(struct saa7146_fh *fh)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;

	DEB_EE(("dev:%p, fh:%p\n",dev,fh));

	/* check if streaming capture is running */
	if (IS_CAPTURE_ACTIVE(fh) != 0) {
		DEB_D(("streaming capture is active.\n"));
		return -EBUSY;
	}

	/* check if overlay is running at all */
	if ((vv->video_status & STATUS_OVERLAY) == 0) {
		DEB_D(("no active overlay.\n"));
		return 0;
	}

	if (vv->video_fh != fh) {
		DEB_D(("overlay is active, but in another open.\n"));
		return -EBUSY;
	}

	vv->video_status = 0;
	vv->video_fh = NULL;

	saa7146_disable_overlay(fh);

	saa7146_res_free(fh, RESOURCE_DMA1_HPS|RESOURCE_DMA2_CLP);

	return 0;
}
EXPORT_SYMBOL_GPL(saa7146_stop_preview);

static int s_fmt(struct saa7146_fh *fh, struct v4l2_format *f)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;

	int err;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		DEB_EE(("V4L2_BUF_TYPE_VIDEO_CAPTURE: dev:%p, fh:%p\n",dev,fh));
		if (IS_CAPTURE_ACTIVE(fh) != 0) {
			DEB_EE(("streaming capture is active\n"));
			return -EBUSY;
		}
		err = try_fmt(fh,f);
		if (0 != err)
			return err;
		fh->video_fmt = f->fmt.pix;
		DEB_EE(("set to pixelformat '%4.4s'\n",(char *)&fh->video_fmt.pixelformat));
		return 0;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		DEB_EE(("V4L2_BUF_TYPE_VIDEO_OVERLAY: dev:%p, fh:%p\n",dev,fh));
		err = try_win(dev,&f->fmt.win);
		if (0 != err)
			return err;
		mutex_lock(&dev->lock);
		fh->ov.win    = f->fmt.win;
		fh->ov.nclips = f->fmt.win.clipcount;
		if (fh->ov.nclips > 16)
			fh->ov.nclips = 16;
		if (copy_from_user(fh->ov.clips,f->fmt.win.clips,sizeof(struct v4l2_clip)*fh->ov.nclips)) {
			mutex_unlock(&dev->lock);
			return -EFAULT;
		}

		/* fh->ov.fh is used to indicate that we have valid overlay informations, too */
		fh->ov.fh = fh;

		mutex_unlock(&dev->lock);

		/* check if our current overlay is active */
		if (IS_OVERLAY_ACTIVE(fh) != 0) {
			saa7146_stop_preview(fh);
			saa7146_start_preview(fh);
		}
		return 0;
	default:
		DEB_D(("unknown format type '%d'\n",f->type));
		return -EINVAL;
	}
}

/********************************************************************************/
/* device controls */

static struct v4l2_queryctrl controls[] = {
	{
		.id		= V4L2_CID_BRIGHTNESS,
		.name		= "Brightness",
		.minimum	= 0,
		.maximum	= 255,
		.step		= 1,
		.default_value	= 128,
		.type		= V4L2_CTRL_TYPE_INTEGER,
	},{
		.id		= V4L2_CID_CONTRAST,
		.name		= "Contrast",
		.minimum	= 0,
		.maximum	= 127,
		.step		= 1,
		.default_value	= 64,
		.type		= V4L2_CTRL_TYPE_INTEGER,
	},{
		.id		= V4L2_CID_SATURATION,
		.name		= "Saturation",
		.minimum	= 0,
		.maximum	= 127,
		.step		= 1,
		.default_value	= 64,
		.type		= V4L2_CTRL_TYPE_INTEGER,
	},{
		.id		= V4L2_CID_VFLIP,
		.name		= "Vertical flip",
		.minimum	= 0,
		.maximum	= 1,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id		= V4L2_CID_HFLIP,
		.name		= "Horizontal flip",
		.minimum	= 0,
		.maximum	= 1,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
	},
};
static int NUM_CONTROLS = sizeof(controls)/sizeof(struct v4l2_queryctrl);

#define V4L2_CID_PRIVATE_LASTP1      (V4L2_CID_PRIVATE_BASE + 0)

static struct v4l2_queryctrl* ctrl_by_id(int id)
{
	int i;

	for (i = 0; i < NUM_CONTROLS; i++)
		if (controls[i].id == id)
			return controls+i;
	return NULL;
}

static int get_control(struct saa7146_fh *fh, struct v4l2_control *c)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;

	const struct v4l2_queryctrl* ctrl;
	u32 value = 0;

	ctrl = ctrl_by_id(c->id);
	if (NULL == ctrl)
		return -EINVAL;
	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		value = saa7146_read(dev, BCS_CTRL);
		c->value = 0xff & (value >> 24);
		DEB_D(("V4L2_CID_BRIGHTNESS: %d\n",c->value));
		break;
	case V4L2_CID_CONTRAST:
		value = saa7146_read(dev, BCS_CTRL);
		c->value = 0x7f & (value >> 16);
		DEB_D(("V4L2_CID_CONTRAST: %d\n",c->value));
		break;
	case V4L2_CID_SATURATION:
		value = saa7146_read(dev, BCS_CTRL);
		c->value = 0x7f & (value >> 0);
		DEB_D(("V4L2_CID_SATURATION: %d\n",c->value));
		break;
	case V4L2_CID_VFLIP:
		c->value = vv->vflip;
		DEB_D(("V4L2_CID_VFLIP: %d\n",c->value));
		break;
	case V4L2_CID_HFLIP:
		c->value = vv->hflip;
		DEB_D(("V4L2_CID_HFLIP: %d\n",c->value));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int set_control(struct saa7146_fh *fh, struct v4l2_control *c)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;

	const struct v4l2_queryctrl* ctrl;

	ctrl = ctrl_by_id(c->id);
	if (NULL == ctrl) {
		DEB_D(("unknown control %d\n",c->id));
		return -EINVAL;
	}

	mutex_lock(&dev->lock);

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_BOOLEAN:
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER:
		if (c->value < ctrl->minimum)
			c->value = ctrl->minimum;
		if (c->value > ctrl->maximum)
			c->value = ctrl->maximum;
		break;
	default:
		/* nothing */;
	};

	switch (c->id) {
	case V4L2_CID_BRIGHTNESS: {
		u32 value = saa7146_read(dev, BCS_CTRL);
		value &= 0x00ffffff;
		value |= (c->value << 24);
		saa7146_write(dev, BCS_CTRL, value);
		saa7146_write(dev, MC2, MASK_22 | MASK_06 );
		break;
	}
	case V4L2_CID_CONTRAST: {
		u32 value = saa7146_read(dev, BCS_CTRL);
		value &= 0xff00ffff;
		value |= (c->value << 16);
		saa7146_write(dev, BCS_CTRL, value);
		saa7146_write(dev, MC2, MASK_22 | MASK_06 );
		break;
	}
	case V4L2_CID_SATURATION: {
		u32 value = saa7146_read(dev, BCS_CTRL);
		value &= 0xffffff00;
		value |= (c->value << 0);
		saa7146_write(dev, BCS_CTRL, value);
		saa7146_write(dev, MC2, MASK_22 | MASK_06 );
		break;
	}
	case V4L2_CID_HFLIP:
		/* fixme: we can support changing VFLIP and HFLIP here... */
		if (IS_CAPTURE_ACTIVE(fh) != 0) {
			DEB_D(("V4L2_CID_HFLIP while active capture.\n"));
			mutex_unlock(&dev->lock);
			return -EINVAL;
		}
		vv->hflip = c->value;
		break;
	case V4L2_CID_VFLIP:
		if (IS_CAPTURE_ACTIVE(fh) != 0) {
			DEB_D(("V4L2_CID_VFLIP while active capture.\n"));
			mutex_unlock(&dev->lock);
			return -EINVAL;
		}
		vv->vflip = c->value;
		break;
	default: {
		return -EINVAL;
	}
	}
	mutex_unlock(&dev->lock);

	if (IS_OVERLAY_ACTIVE(fh) != 0) {
		saa7146_stop_preview(fh);
		saa7146_start_preview(fh);
	}
	return 0;
}

/********************************************************************************/
/* common pagetable functions */

static int saa7146_pgtable_build(struct saa7146_dev *dev, struct saa7146_buf *buf)
{
	struct pci_dev *pci = dev->pci;
	struct scatterlist *list = buf->vb.dma.sglist;
	int length = buf->vb.dma.sglen;
	struct saa7146_format *sfmt = format_by_fourcc(dev,buf->fmt->pixelformat);

	DEB_EE(("dev:%p, buf:%p, sg_len:%d\n",dev,buf,length));

	if( 0 != IS_PLANAR(sfmt->trans)) {
		struct saa7146_pgtable *pt1 = &buf->pt[0];
		struct saa7146_pgtable *pt2 = &buf->pt[1];
		struct saa7146_pgtable *pt3 = &buf->pt[2];
		u32  *ptr1, *ptr2, *ptr3;
		u32 fill;

		int size = buf->fmt->width*buf->fmt->height;
		int i,p,m1,m2,m3,o1,o2;

		switch( sfmt->depth ) {
			case 12: {
				/* create some offsets inside the page table */
				m1 = ((size+PAGE_SIZE)/PAGE_SIZE)-1;
				m2 = ((size+(size/4)+PAGE_SIZE)/PAGE_SIZE)-1;
				m3 = ((size+(size/2)+PAGE_SIZE)/PAGE_SIZE)-1;
				o1 = size%PAGE_SIZE;
				o2 = (size+(size/4))%PAGE_SIZE;
				DEB_CAP(("size:%d, m1:%d, m2:%d, m3:%d, o1:%d, o2:%d\n",size,m1,m2,m3,o1,o2));
				break;
			}
			case 16: {
				/* create some offsets inside the page table */
				m1 = ((size+PAGE_SIZE)/PAGE_SIZE)-1;
				m2 = ((size+(size/2)+PAGE_SIZE)/PAGE_SIZE)-1;
				m3 = ((2*size+PAGE_SIZE)/PAGE_SIZE)-1;
				o1 = size%PAGE_SIZE;
				o2 = (size+(size/2))%PAGE_SIZE;
				DEB_CAP(("size:%d, m1:%d, m2:%d, m3:%d, o1:%d, o2:%d\n",size,m1,m2,m3,o1,o2));
				break;
			}
			default: {
				return -1;
			}
		}

		ptr1 = pt1->cpu;
		ptr2 = pt2->cpu;
		ptr3 = pt3->cpu;

		/* walk all pages, copy all page addresses to ptr1 */
		for (i = 0; i < length; i++, list++) {
			for (p = 0; p * 4096 < list->length; p++, ptr1++) {
				*ptr1 = cpu_to_le32(sg_dma_address(list) - list->offset);
			}
		}
/*
		ptr1 = pt1->cpu;
		for(j=0;j<40;j++) {
			printk("ptr1 %d: 0x%08x\n",j,ptr1[j]);
		}
*/

		/* if we have a user buffer, the first page may not be
		   aligned to a page boundary. */
		pt1->offset = buf->vb.dma.sglist->offset;
		pt2->offset = pt1->offset+o1;
		pt3->offset = pt1->offset+o2;

		/* create video-dma2 page table */
		ptr1 = pt1->cpu;
		for(i = m1; i <= m2 ; i++, ptr2++) {
			*ptr2 = ptr1[i];
		}
		fill = *(ptr2-1);
		for(;i<1024;i++,ptr2++) {
			*ptr2 = fill;
		}
		/* create video-dma3 page table */
		ptr1 = pt1->cpu;
		for(i = m2; i <= m3; i++,ptr3++) {
			*ptr3 = ptr1[i];
		}
		fill = *(ptr3-1);
		for(;i<1024;i++,ptr3++) {
			*ptr3 = fill;
		}
		/* finally: finish up video-dma1 page table */
		ptr1 = pt1->cpu+m1;
		fill = pt1->cpu[m1];
		for(i=m1;i<1024;i++,ptr1++) {
			*ptr1 = fill;
		}
/*
		ptr1 = pt1->cpu;
		ptr2 = pt2->cpu;
		ptr3 = pt3->cpu;
		for(j=0;j<40;j++) {
			printk("ptr1 %d: 0x%08x\n",j,ptr1[j]);
		}
		for(j=0;j<40;j++) {
			printk("ptr2 %d: 0x%08x\n",j,ptr2[j]);
		}
		for(j=0;j<40;j++) {
			printk("ptr3 %d: 0x%08x\n",j,ptr3[j]);
		}
*/
	} else {
		struct saa7146_pgtable *pt = &buf->pt[0];
		return saa7146_pgtable_build_single(pci, pt, list, length);
	}

	return 0;
}


/********************************************************************************/
/* file operations */

static int video_begin(struct saa7146_fh *fh)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_format *fmt = NULL;
	unsigned int resource;
	int ret = 0, err = 0;

	DEB_EE(("dev:%p, fh:%p\n",dev,fh));

	if ((vv->video_status & STATUS_CAPTURE) != 0) {
		if (vv->video_fh == fh) {
			DEB_S(("already capturing.\n"));
			return 0;
		}
		DEB_S(("already capturing in another open.\n"));
		return -EBUSY;
	}

	if ((vv->video_status & STATUS_OVERLAY) != 0) {
		DEB_S(("warning: suspending overlay video for streaming capture.\n"));
		vv->ov_suspend = vv->video_fh;
		err = saa7146_stop_preview(vv->video_fh); /* side effect: video_status is now 0, video_fh is NULL */
		if (0 != err) {
			DEB_D(("suspending video failed. aborting\n"));
			return err;
		}
	}

	fmt = format_by_fourcc(dev,fh->video_fmt.pixelformat);
	/* we need to have a valid format set here */
	BUG_ON(NULL == fmt);

	if (0 != (fmt->flags & FORMAT_IS_PLANAR)) {
		resource = RESOURCE_DMA1_HPS|RESOURCE_DMA2_CLP|RESOURCE_DMA3_BRS;
	} else {
		resource = RESOURCE_DMA1_HPS;
	}

	ret = saa7146_res_get(fh, resource);
	if (0 == ret) {
		DEB_S(("cannot get capture resource %d\n",resource));
		if (vv->ov_suspend != NULL) {
			saa7146_start_preview(vv->ov_suspend);
			vv->ov_suspend = NULL;
		}
		return -EBUSY;
	}

	/* clear out beginning of streaming bit (rps register 0)*/
	saa7146_write(dev, MC2, MASK_27 );

	/* enable rps0 irqs */
	SAA7146_IER_ENABLE(dev, MASK_27);

	vv->video_fh = fh;
	vv->video_status = STATUS_CAPTURE;

	return 0;
}

static int video_end(struct saa7146_fh *fh, struct file *file)
{
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_format *fmt = NULL;
	unsigned long flags;
	unsigned int resource;
	u32 dmas = 0;
	DEB_EE(("dev:%p, fh:%p\n",dev,fh));

	if ((vv->video_status & STATUS_CAPTURE) != STATUS_CAPTURE) {
		DEB_S(("not capturing.\n"));
		return 0;
	}

	if (vv->video_fh != fh) {
		DEB_S(("capturing, but in another open.\n"));
		return -EBUSY;
	}

	fmt = format_by_fourcc(dev,fh->video_fmt.pixelformat);
	/* we need to have a valid format set here */
	BUG_ON(NULL == fmt);

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

	vv->video_fh = NULL;
	vv->video_status = 0;

	saa7146_res_free(fh, resource);

	if (vv->ov_suspend != NULL) {
		saa7146_start_preview(vv->ov_suspend);
		vv->ov_suspend = NULL;
	}

	return 0;
}

/*
 * This function is _not_ called directly, but from
 * video_generic_ioctl (and maybe others).  userspace
 * copying is done already, arg is a kernel pointer.
 */

int saa7146_video_do_ioctl(struct inode *inode, struct file *file, unsigned int cmd, void *arg)
{
	struct saa7146_fh *fh  = file->private_data;
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;

	int err = 0, result = 0, ee = 0;

	struct saa7146_use_ops *ops;
	struct videobuf_queue *q;

	/* check if extension handles the command */
	for(ee = 0; dev->ext_vv_data->ioctls[ee].flags != 0; ee++) {
		if( cmd == dev->ext_vv_data->ioctls[ee].cmd )
			break;
	}

	if( 0 != (dev->ext_vv_data->ioctls[ee].flags & SAA7146_EXCLUSIVE) ) {
		DEB_D(("extension handles ioctl exclusive.\n"));
		result = dev->ext_vv_data->ioctl(fh, cmd, arg);
		return result;
	}
	if( 0 != (dev->ext_vv_data->ioctls[ee].flags & SAA7146_BEFORE) ) {
		DEB_D(("extension handles ioctl before.\n"));
		result = dev->ext_vv_data->ioctl(fh, cmd, arg);
		if( -EAGAIN != result ) {
			return result;
		}
	}

	/* fixme: add handle "after" case (is it still needed?) */

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE: {
		ops = &saa7146_video_uops;
		q = &fh->video_q;
		break;
		}
	case V4L2_BUF_TYPE_VBI_CAPTURE: {
		ops = &saa7146_vbi_uops;
		q = &fh->vbi_q;
		break;
		}
	default:
		BUG();
		return 0;
	}

	switch (cmd) {
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;
		memset(cap,0,sizeof(*cap));

		DEB_EE(("VIDIOC_QUERYCAP\n"));

		strcpy(cap->driver, "saa7146 v4l2");
		strlcpy(cap->card, dev->ext->name, sizeof(cap->card));
		sprintf(cap->bus_info,"PCI:%s", pci_name(dev->pci));
		cap->version = SAA7146_VERSION_CODE;
		cap->capabilities =
			V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_VIDEO_OVERLAY |
			V4L2_CAP_READWRITE |
			V4L2_CAP_STREAMING;
		cap->capabilities |= dev->ext_vv_data->capabilities;
		return 0;
	}
	case VIDIOC_G_FBUF:
	{
		struct v4l2_framebuffer *fb = arg;

		DEB_EE(("VIDIOC_G_FBUF\n"));

		*fb = vv->ov_fb;
		fb->capability = V4L2_FBUF_CAP_LIST_CLIPPING;
		return 0;
	}
	case VIDIOC_S_FBUF:
	{
		struct v4l2_framebuffer *fb = arg;
		struct saa7146_format *fmt;

		DEB_EE(("VIDIOC_S_FBUF\n"));

		if(!capable(CAP_SYS_ADMIN) &&
		   !capable(CAP_SYS_RAWIO))
			return -EPERM;

		/* check args */
		fmt = format_by_fourcc(dev,fb->fmt.pixelformat);
		if (NULL == fmt) {
			return -EINVAL;
		}

		/* planar formats are not allowed for overlay video, clipping and video dma would clash */
		if (0 != (fmt->flags & FORMAT_IS_PLANAR)) {
			DEB_S(("planar pixelformat '%4.4s' not allowed for overlay\n",(char *)&fmt->pixelformat));
		}

		/* check if overlay is running */
		if (IS_OVERLAY_ACTIVE(fh) != 0) {
			if (vv->video_fh != fh) {
				DEB_D(("refusing to change framebuffer informations while overlay is active in another open.\n"));
				return -EBUSY;
			}
		}

		mutex_lock(&dev->lock);

		/* ok, accept it */
		vv->ov_fb = *fb;
		vv->ov_fmt = fmt;
		if (0 == vv->ov_fb.fmt.bytesperline)
			vv->ov_fb.fmt.bytesperline =
				vv->ov_fb.fmt.width*fmt->depth/8;

		mutex_unlock(&dev->lock);

		return 0;
	}
	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *f = arg;
		int index;

		switch (f->type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		case V4L2_BUF_TYPE_VIDEO_OVERLAY: {
			index = f->index;
			if (index < 0 || index >= NUM_FORMATS) {
				return -EINVAL;
			}
			memset(f,0,sizeof(*f));
			f->index = index;
			strlcpy(f->description,formats[index].name,sizeof(f->description));
			f->pixelformat = formats[index].pixelformat;
			break;
		}
		default:
			return -EINVAL;
		}

		DEB_EE(("VIDIOC_ENUM_FMT: type:%d, index:%d\n",f->type,f->index));
		return 0;
	}
	case VIDIOC_QUERYCTRL:
	{
		const struct v4l2_queryctrl *ctrl;
		struct v4l2_queryctrl *c = arg;

		if ((c->id <  V4L2_CID_BASE ||
		     c->id >= V4L2_CID_LASTP1) &&
		    (c->id <  V4L2_CID_PRIVATE_BASE ||
		     c->id >= V4L2_CID_PRIVATE_LASTP1))
			return -EINVAL;

		ctrl = ctrl_by_id(c->id);
		if( NULL == ctrl ) {
			return -EINVAL;
/*
			c->flags = V4L2_CTRL_FLAG_DISABLED;
			return 0;
*/
		}

		DEB_EE(("VIDIOC_QUERYCTRL: id:%d\n",c->id));
		*c = *ctrl;
		return 0;
	}
	case VIDIOC_G_CTRL: {
		DEB_EE(("VIDIOC_G_CTRL\n"));
		return get_control(fh,arg);
	}
	case VIDIOC_S_CTRL:
	{
		DEB_EE(("VIDIOC_S_CTRL\n"));
		err = set_control(fh,arg);
		return err;
	}
	case VIDIOC_G_PARM:
	{
		struct v4l2_streamparm *parm = arg;
		if( parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ) {
			return -EINVAL;
		}
		memset(&parm->parm.capture,0,sizeof(struct v4l2_captureparm));
		parm->parm.capture.readbuffers = 1;
		// fixme: only for PAL!
		parm->parm.capture.timeperframe.numerator = 1;
		parm->parm.capture.timeperframe.denominator = 25;
		return 0;
	}
	case VIDIOC_G_FMT:
	{
		struct v4l2_format *f = arg;
		DEB_EE(("VIDIOC_G_FMT\n"));
		return g_fmt(fh,f);
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = arg;
		DEB_EE(("VIDIOC_S_FMT\n"));
		return s_fmt(fh,f);
	}
	case VIDIOC_TRY_FMT:
	{
		struct v4l2_format *f = arg;
		DEB_EE(("VIDIOC_TRY_FMT\n"));
		return try_fmt(fh,f);
	}
	case VIDIOC_G_STD:
	{
		v4l2_std_id *id = arg;
		DEB_EE(("VIDIOC_G_STD\n"));
		*id = vv->standard->id;
		return 0;
	}
	/* the saa7146 supfhrts (used in conjunction with the saa7111a for example)
	   PAL / NTSC / SECAM. if your hardware does not (or does more)
	   -- override this function in your extension */
	case VIDIOC_ENUMSTD:
	{
		struct v4l2_standard *e = arg;
		if (e->index < 0 )
			return -EINVAL;
		if( e->index < dev->ext_vv_data->num_stds ) {
			DEB_EE(("VIDIOC_ENUMSTD: index:%d\n",e->index));
			v4l2_video_std_construct(e, dev->ext_vv_data->stds[e->index].id, dev->ext_vv_data->stds[e->index].name);
			return 0;
		}
		return -EINVAL;
	}
	case VIDIOC_S_STD:
	{
		v4l2_std_id *id = arg;
		int found = 0;
		int i, err;

		DEB_EE(("VIDIOC_S_STD\n"));

		if ((vv->video_status & STATUS_CAPTURE) == STATUS_CAPTURE) {
			DEB_D(("cannot change video standard while streaming capture is active\n"));
			return -EBUSY;
		}

		if ((vv->video_status & STATUS_OVERLAY) != 0) {
			vv->ov_suspend = vv->video_fh;
			err = saa7146_stop_preview(vv->video_fh); /* side effect: video_status is now 0, video_fh is NULL */
			if (0 != err) {
				DEB_D(("suspending video failed. aborting\n"));
				return err;
			}
		}

		mutex_lock(&dev->lock);

		for(i = 0; i < dev->ext_vv_data->num_stds; i++)
			if (*id & dev->ext_vv_data->stds[i].id)
				break;
		if (i != dev->ext_vv_data->num_stds) {
			vv->standard = &dev->ext_vv_data->stds[i];
			if( NULL != dev->ext_vv_data->std_callback )
				dev->ext_vv_data->std_callback(dev, vv->standard);
			found = 1;
		}

		mutex_unlock(&dev->lock);

		if (vv->ov_suspend != NULL) {
			saa7146_start_preview(vv->ov_suspend);
			vv->ov_suspend = NULL;
		}

		if( 0 == found ) {
			DEB_EE(("VIDIOC_S_STD: standard not found.\n"));
			return -EINVAL;
		}

		DEB_EE(("VIDIOC_S_STD: set to standard to '%s'\n",vv->standard->name));
		return 0;
	}
	case VIDIOC_OVERLAY:
	{
		int on = *(int *)arg;
		int err = 0;

		DEB_D(("VIDIOC_OVERLAY on:%d\n",on));
		if (on != 0) {
			err = saa7146_start_preview(fh);
		} else {
			err = saa7146_stop_preview(fh);
		}
		return err;
	}
	case VIDIOC_REQBUFS: {
		struct v4l2_requestbuffers *req = arg;
		DEB_D(("VIDIOC_REQBUFS, type:%d\n",req->type));
		return videobuf_reqbufs(q,req);
	}
	case VIDIOC_QUERYBUF: {
		struct v4l2_buffer *buf = arg;
		DEB_D(("VIDIOC_QUERYBUF, type:%d, offset:%d\n",buf->type,buf->m.offset));
		return videobuf_querybuf(q,buf);
	}
	case VIDIOC_QBUF: {
		struct v4l2_buffer *buf = arg;
		int ret = 0;
		ret = videobuf_qbuf(q,buf);
		DEB_D(("VIDIOC_QBUF: ret:%d, index:%d\n",ret,buf->index));
		return ret;
	}
	case VIDIOC_DQBUF: {
		struct v4l2_buffer *buf = arg;
		int ret = 0;
		ret = videobuf_dqbuf(q,buf,file->f_flags & O_NONBLOCK);
		DEB_D(("VIDIOC_DQBUF: ret:%d, index:%d\n",ret,buf->index));
		return ret;
	}
	case VIDIOC_STREAMON: {
		int *type = arg;
		DEB_D(("VIDIOC_STREAMON, type:%d\n",*type));

		err = video_begin(fh);
		if( 0 != err) {
			return err;
		}
		err = videobuf_streamon(q);
		return err;
	}
	case VIDIOC_STREAMOFF: {
		int *type = arg;

		DEB_D(("VIDIOC_STREAMOFF, type:%d\n",*type));

		/* ugly: we need to copy some checks from video_end(),
		   because videobuf_streamoff() relies on the capture running.
		   check and fix this */
		if ((vv->video_status & STATUS_CAPTURE) != STATUS_CAPTURE) {
			DEB_S(("not capturing.\n"));
			return 0;
		}

		if (vv->video_fh != fh) {
			DEB_S(("capturing, but in another open.\n"));
			return -EBUSY;
		}

		err = videobuf_streamoff(q);
		if (0 != err) {
			DEB_D(("warning: videobuf_streamoff() failed.\n"));
			video_end(fh, file);
		} else {
			err = video_end(fh, file);
		}
		return err;
	}
	case VIDIOCGMBUF:
	{
		struct video_mbuf *mbuf = arg;
		struct videobuf_queue *q;
		int i;

		/* fixme: number of capture buffers and sizes for v4l apps */
		int gbuffers = 2;
		int gbufsize = 768*576*4;

		DEB_D(("VIDIOCGMBUF \n"));

		q = &fh->video_q;
		mutex_lock(&q->lock);
		err = videobuf_mmap_setup(q,gbuffers,gbufsize,
					  V4L2_MEMORY_MMAP);
		if (err < 0) {
			mutex_unlock(&q->lock);
			return err;
		}
		memset(mbuf,0,sizeof(*mbuf));
		mbuf->frames = gbuffers;
		mbuf->size   = gbuffers * gbufsize;
		for (i = 0; i < gbuffers; i++)
			mbuf->offsets[i] = i * gbufsize;
		mutex_unlock(&q->lock);
		return 0;
	}
	default:
		return v4l_compat_translate_ioctl(inode,file,cmd,arg,
						  saa7146_video_do_ioctl);
	}
	return 0;
}

/*********************************************************************************/
/* buffer handling functions                                                  */

static int buffer_activate (struct saa7146_dev *dev,
		     struct saa7146_buf *buf,
		     struct saa7146_buf *next)
{
	struct saa7146_vv *vv = dev->vv_data;

	buf->vb.state = STATE_ACTIVE;
	saa7146_set_capture(dev,buf,next);

	mod_timer(&vv->video_q.timeout, jiffies+BUFFER_TIMEOUT);
	return 0;
}

static int buffer_prepare(struct videobuf_queue *q,
			  struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct file *file = q->priv_data;
	struct saa7146_fh *fh = file->private_data;
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_buf *buf = (struct saa7146_buf *)vb;
	int size,err = 0;

	DEB_CAP(("vbuf:%p\n",vb));

	/* sanity checks */
	if (fh->video_fmt.width  < 48 ||
	    fh->video_fmt.height < 32 ||
	    fh->video_fmt.width  > vv->standard->h_max_out ||
	    fh->video_fmt.height > vv->standard->v_max_out) {
		DEB_D(("w (%d) / h (%d) out of bounds.\n",fh->video_fmt.width,fh->video_fmt.height));
		return -EINVAL;
	}

	size = fh->video_fmt.sizeimage;
	if (0 != buf->vb.baddr && buf->vb.bsize < size) {
		DEB_D(("size mismatch.\n"));
		return -EINVAL;
	}

	DEB_CAP(("buffer_prepare [size=%dx%d,bytes=%d,fields=%s]\n",
		fh->video_fmt.width,fh->video_fmt.height,size,v4l2_field_names[fh->video_fmt.field]));
	if (buf->vb.width  != fh->video_fmt.width  ||
	    buf->vb.bytesperline != fh->video_fmt.bytesperline ||
	    buf->vb.height != fh->video_fmt.height ||
	    buf->vb.size   != size ||
	    buf->vb.field  != field      ||
	    buf->vb.field  != fh->video_fmt.field  ||
	    buf->fmt       != &fh->video_fmt) {
		saa7146_dma_free(dev,q,buf);
	}

	if (STATE_NEEDS_INIT == buf->vb.state) {
		struct saa7146_format *sfmt;

		buf->vb.bytesperline  = fh->video_fmt.bytesperline;
		buf->vb.width  = fh->video_fmt.width;
		buf->vb.height = fh->video_fmt.height;
		buf->vb.size   = size;
		buf->vb.field  = field;
		buf->fmt       = &fh->video_fmt;
		buf->vb.field  = fh->video_fmt.field;

		sfmt = format_by_fourcc(dev,buf->fmt->pixelformat);

		if( 0 != IS_PLANAR(sfmt->trans)) {
			saa7146_pgtable_free(dev->pci, &buf->pt[0]);
			saa7146_pgtable_free(dev->pci, &buf->pt[1]);
			saa7146_pgtable_free(dev->pci, &buf->pt[2]);

			saa7146_pgtable_alloc(dev->pci, &buf->pt[0]);
			saa7146_pgtable_alloc(dev->pci, &buf->pt[1]);
			saa7146_pgtable_alloc(dev->pci, &buf->pt[2]);
		} else {
			saa7146_pgtable_free(dev->pci, &buf->pt[0]);
			saa7146_pgtable_alloc(dev->pci, &buf->pt[0]);
		}

		err = videobuf_iolock(q,&buf->vb, &vv->ov_fb);
		if (err)
			goto oops;
		err = saa7146_pgtable_build(dev,buf);
		if (err)
			goto oops;
	}
	buf->vb.state = STATE_PREPARED;
	buf->activate = buffer_activate;

	return 0;

 oops:
	DEB_D(("error out.\n"));
	saa7146_dma_free(dev,q,buf);

	return err;
}

static int buffer_setup(struct videobuf_queue *q, unsigned int *count, unsigned int *size)
{
	struct file *file = q->priv_data;
	struct saa7146_fh *fh = file->private_data;

	if (0 == *count || *count > MAX_SAA7146_CAPTURE_BUFFERS)
		*count = MAX_SAA7146_CAPTURE_BUFFERS;

	*size = fh->video_fmt.sizeimage;

	/* check if we exceed the "max_memory" parameter */
	if( (*count * *size) > (max_memory*1048576) ) {
		*count = (max_memory*1048576) / *size;
	}

	DEB_CAP(("%d buffers, %d bytes each.\n",*count,*size));

	return 0;
}

static void buffer_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct file *file = q->priv_data;
	struct saa7146_fh *fh = file->private_data;
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_buf *buf = (struct saa7146_buf *)vb;

	DEB_CAP(("vbuf:%p\n",vb));
	saa7146_buffer_queue(fh->dev,&vv->video_q,buf);
}

static void buffer_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct file *file = q->priv_data;
	struct saa7146_fh *fh = file->private_data;
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_buf *buf = (struct saa7146_buf *)vb;

	DEB_CAP(("vbuf:%p\n",vb));
	saa7146_dma_free(dev,q,buf);
}

static struct videobuf_queue_ops video_qops = {
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};

/********************************************************************************/
/* file operations */

static void video_init(struct saa7146_dev *dev, struct saa7146_vv *vv)
{
	INIT_LIST_HEAD(&vv->video_q.queue);

	init_timer(&vv->video_q.timeout);
	vv->video_q.timeout.function = saa7146_buffer_timeout;
	vv->video_q.timeout.data     = (unsigned long)(&vv->video_q);
	vv->video_q.dev              = dev;

	/* set some default values */
	vv->standard = &dev->ext_vv_data->stds[0];

	/* FIXME: what's this? */
	vv->current_hps_source = SAA7146_HPS_SOURCE_PORT_A;
	vv->current_hps_sync = SAA7146_HPS_SYNC_PORT_A;
}


static int video_open(struct saa7146_dev *dev, struct file *file)
{
	struct saa7146_fh *fh = (struct saa7146_fh *)file->private_data;
	struct saa7146_format *sfmt;

	fh->video_fmt.width = 384;
	fh->video_fmt.height = 288;
	fh->video_fmt.pixelformat = V4L2_PIX_FMT_BGR24;
	fh->video_fmt.bytesperline = 0;
	fh->video_fmt.field = V4L2_FIELD_ANY;
	sfmt = format_by_fourcc(dev,fh->video_fmt.pixelformat);
	fh->video_fmt.sizeimage = (fh->video_fmt.width * fh->video_fmt.height * sfmt->depth)/8;

	videobuf_queue_init(&fh->video_q, &video_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_INTERLACED,
			    sizeof(struct saa7146_buf),
			    file);

	mutex_init(&fh->video_q.lock);

	return 0;
}


static void video_close(struct saa7146_dev *dev, struct file *file)
{
	struct saa7146_fh *fh = (struct saa7146_fh *)file->private_data;
	struct saa7146_vv *vv = dev->vv_data;
	int err;

	if (IS_CAPTURE_ACTIVE(fh) != 0) {
		err = video_end(fh, file);
	} else if (IS_OVERLAY_ACTIVE(fh) != 0) {
		err = saa7146_stop_preview(fh);
	}

	/* hmm, why is this function declared void? */
	/* return err */
}


static void video_irq_done(struct saa7146_dev *dev, unsigned long st)
{
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_dmaqueue *q = &vv->video_q;

	spin_lock(&dev->slock);
	DEB_CAP(("called.\n"));

	/* only finish the buffer if we have one... */
	if( NULL != q->curr ) {
		saa7146_buffer_finish(dev,q,STATE_DONE);
	}
	saa7146_buffer_next(dev,q,0);

	spin_unlock(&dev->slock);
}

static ssize_t video_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct saa7146_fh *fh = file->private_data;
	struct saa7146_dev *dev = fh->dev;
	struct saa7146_vv *vv = dev->vv_data;
	ssize_t ret = 0;

	DEB_EE(("called.\n"));

	if ((vv->video_status & STATUS_CAPTURE) != 0) {
		/* fixme: should we allow read() captures while streaming capture? */
		if (vv->video_fh == fh) {
			DEB_S(("already capturing.\n"));
			return -EBUSY;
		}
		DEB_S(("already capturing in another open.\n"));
		return -EBUSY;
	}

	ret = video_begin(fh);
	if( 0 != ret) {
		goto out;
	}

	ret = videobuf_read_one(&fh->video_q , data, count, ppos,
				file->f_flags & O_NONBLOCK);
	if (ret != 0) {
		video_end(fh, file);
	} else {
		ret = video_end(fh, file);
	}
out:
	/* restart overlay if it was active before */
	if (vv->ov_suspend != NULL) {
		saa7146_start_preview(vv->ov_suspend);
		vv->ov_suspend = NULL;
	}

	return ret;
}

struct saa7146_use_ops saa7146_video_uops = {
	.init = video_init,
	.open = video_open,
	.release = video_close,
	.irq_done = video_irq_done,
	.read = video_read,
};
