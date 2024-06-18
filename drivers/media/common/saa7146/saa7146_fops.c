// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <media/drv-intf/saa7146_vv.h>
#include <linux/module.h>

/****************************************************************************/
/* resource management functions, shamelessly stolen from saa7134 driver */

int saa7146_res_get(struct saa7146_dev *dev, unsigned int bit)
{
	struct saa7146_vv *vv = dev->vv_data;

	if (vv->resources & bit) {
		DEB_D("already allocated! want: 0x%02x, cur:0x%02x\n",
		      bit, vv->resources);
		/* have it already allocated */
		return 1;
	}

	/* is it free? */
	if (vv->resources & bit) {
		DEB_D("locked! vv->resources:0x%02x, we want:0x%02x\n",
		      vv->resources, bit);
		/* no, someone else uses it */
		return 0;
	}
	/* it's free, grab it */
	vv->resources |= bit;
	DEB_D("res: get 0x%02x, cur:0x%02x\n", bit, vv->resources);
	return 1;
}

void saa7146_res_free(struct saa7146_dev *dev, unsigned int bits)
{
	struct saa7146_vv *vv = dev->vv_data;

	WARN_ON((vv->resources & bits) != bits);

	vv->resources &= ~bits;
	DEB_D("res: put 0x%02x, cur:0x%02x\n", bits, vv->resources);
}


/********************************************************************************/
/* common buffer functions */

int saa7146_buffer_queue(struct saa7146_dev *dev,
			 struct saa7146_dmaqueue *q,
			 struct saa7146_buf *buf)
{
	assert_spin_locked(&dev->slock);
	DEB_EE("dev:%p, dmaq:%p, buf:%p\n", dev, q, buf);

	if (WARN_ON(!q))
		return -EIO;

	if (NULL == q->curr) {
		q->curr = buf;
		DEB_D("immediately activating buffer %p\n", buf);
		buf->activate(dev,buf,NULL);
	} else {
		list_add_tail(&buf->list, &q->queue);
		DEB_D("adding buffer %p to queue. (active buffer present)\n",
		      buf);
	}
	return 0;
}

void saa7146_buffer_finish(struct saa7146_dev *dev,
			   struct saa7146_dmaqueue *q,
			   int state)
{
	struct saa7146_vv *vv = dev->vv_data;
	struct saa7146_buf *buf = q->curr;

	assert_spin_locked(&dev->slock);
	DEB_EE("dev:%p, dmaq:%p, state:%d\n", dev, q, state);
	DEB_EE("q->curr:%p\n", q->curr);

	/* finish current buffer */
	if (!buf) {
		DEB_D("aiii. no current buffer\n");
		return;
	}

	q->curr = NULL;
	buf->vb.vb2_buf.timestamp = ktime_get_ns();
	if (vv->video_fmt.field == V4L2_FIELD_ALTERNATE)
		buf->vb.field = vv->last_field;
	else if (vv->video_fmt.field == V4L2_FIELD_ANY)
		buf->vb.field = (vv->video_fmt.height > vv->standard->v_max_out / 2)
			? V4L2_FIELD_INTERLACED
			: V4L2_FIELD_BOTTOM;
	else
		buf->vb.field = vv->video_fmt.field;
	buf->vb.sequence = vv->seqnr++;
	vb2_buffer_done(&buf->vb.vb2_buf, state);
}

void saa7146_buffer_next(struct saa7146_dev *dev,
			 struct saa7146_dmaqueue *q, int vbi)
{
	struct saa7146_buf *buf,*next = NULL;

	if (WARN_ON(!q))
		return;

	DEB_INT("dev:%p, dmaq:%p, vbi:%d\n", dev, q, vbi);

	assert_spin_locked(&dev->slock);
	if (!list_empty(&q->queue)) {
		/* activate next one from queue */
		buf = list_entry(q->queue.next, struct saa7146_buf, list);
		list_del(&buf->list);
		if (!list_empty(&q->queue))
			next = list_entry(q->queue.next, struct saa7146_buf, list);
		q->curr = buf;
		DEB_INT("next buffer: buf:%p, prev:%p, next:%p\n",
			buf, q->queue.prev, q->queue.next);
		buf->activate(dev,buf,next);
	} else {
		DEB_INT("no next buffer. stopping.\n");
		if( 0 != vbi ) {
			/* turn off video-dma3 */
			saa7146_write(dev,MC1, MASK_20);
		} else {
			/* nothing to do -- just prevent next video-dma1 transfer
			   by lowering the protection address */

			// fixme: fix this for vflip != 0

			saa7146_write(dev, PROT_ADDR1, 0);
			saa7146_write(dev, MC2, (MASK_02|MASK_18));

			/* write the address of the rps-program */
			saa7146_write(dev, RPS_ADDR0, dev->d_rps0.dma_handle);
			/* turn on rps */
			saa7146_write(dev, MC1, (MASK_12 | MASK_28));

/*
			printk("vdma%d.base_even:     0x%08x\n", 1,saa7146_read(dev,BASE_EVEN1));
			printk("vdma%d.base_odd:      0x%08x\n", 1,saa7146_read(dev,BASE_ODD1));
			printk("vdma%d.prot_addr:     0x%08x\n", 1,saa7146_read(dev,PROT_ADDR1));
			printk("vdma%d.base_page:     0x%08x\n", 1,saa7146_read(dev,BASE_PAGE1));
			printk("vdma%d.pitch:         0x%08x\n", 1,saa7146_read(dev,PITCH1));
			printk("vdma%d.num_line_byte: 0x%08x\n", 1,saa7146_read(dev,NUM_LINE_BYTE1));
*/
		}
		del_timer(&q->timeout);
	}
}

void saa7146_buffer_timeout(struct timer_list *t)
{
	struct saa7146_dmaqueue *q = from_timer(q, t, timeout);
	struct saa7146_dev *dev = q->dev;
	unsigned long flags;

	DEB_EE("dev:%p, dmaq:%p\n", dev, q);

	spin_lock_irqsave(&dev->slock,flags);
	if (q->curr) {
		DEB_D("timeout on %p\n", q->curr);
		saa7146_buffer_finish(dev, q, VB2_BUF_STATE_ERROR);
	}

	/* we don't restart the transfer here like other drivers do. when
	   a streaming capture is disabled, the timeout function will be
	   called for the current buffer. if we activate the next buffer now,
	   we mess up our capture logic. if a timeout occurs on another buffer,
	   then something is seriously broken before, so no need to buffer the
	   next capture IMHO... */

	saa7146_buffer_next(dev, q, 0);

	spin_unlock_irqrestore(&dev->slock,flags);
}

/********************************************************************************/
/* file operations */

static ssize_t fops_write(struct file *file, const char __user *data, size_t count, loff_t *ppos)
{
	struct video_device *vdev = video_devdata(file);
	struct saa7146_dev *dev = video_drvdata(file);
	int ret;

	if (vdev->vfl_type != VFL_TYPE_VBI || !dev->ext_vv_data->vbi_fops.write)
		return -EINVAL;
	if (mutex_lock_interruptible(vdev->lock))
		return -ERESTARTSYS;
	ret = dev->ext_vv_data->vbi_fops.write(file, data, count, ppos);
	mutex_unlock(vdev->lock);
	return ret;
}

static const struct v4l2_file_operations video_fops =
{
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.read		= vb2_fop_read,
	.write		= fops_write,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
	.unlocked_ioctl	= video_ioctl2,
};

static void vv_callback(struct saa7146_dev *dev, unsigned long status)
{
	u32 isr = status;

	DEB_INT("dev:%p, isr:0x%08x\n", dev, (u32)status);

	if (0 != (isr & (MASK_27))) {
		DEB_INT("irq: RPS0 (0x%08x)\n", isr);
		saa7146_video_uops.irq_done(dev,isr);
	}

	if (0 != (isr & (MASK_28))) {
		u32 mc2 = saa7146_read(dev, MC2);
		if( 0 != (mc2 & MASK_15)) {
			DEB_INT("irq: RPS1 vbi workaround (0x%08x)\n", isr);
			wake_up(&dev->vv_data->vbi_wq);
			saa7146_write(dev,MC2, MASK_31);
			return;
		}
		DEB_INT("irq: RPS1 (0x%08x)\n", isr);
		saa7146_vbi_uops.irq_done(dev,isr);
	}
}

static const struct v4l2_ctrl_ops saa7146_ctrl_ops = {
	.s_ctrl = saa7146_s_ctrl,
};

int saa7146_vv_init(struct saa7146_dev* dev, struct saa7146_ext_vv *ext_vv)
{
	struct v4l2_ctrl_handler *hdl = &dev->ctrl_handler;
	struct v4l2_pix_format *fmt;
	struct v4l2_vbi_format *vbi;
	struct saa7146_vv *vv;
	int err;

	err = v4l2_device_register(&dev->pci->dev, &dev->v4l2_dev);
	if (err)
		return err;

	v4l2_ctrl_handler_init(hdl, 6);
	v4l2_ctrl_new_std(hdl, &saa7146_ctrl_ops,
		V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(hdl, &saa7146_ctrl_ops,
		V4L2_CID_CONTRAST, 0, 127, 1, 64);
	v4l2_ctrl_new_std(hdl, &saa7146_ctrl_ops,
		V4L2_CID_SATURATION, 0, 127, 1, 64);
	v4l2_ctrl_new_std(hdl, &saa7146_ctrl_ops,
		V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(hdl, &saa7146_ctrl_ops,
		V4L2_CID_HFLIP, 0, 1, 1, 0);
	if (hdl->error) {
		err = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		v4l2_device_unregister(&dev->v4l2_dev);
		return err;
	}
	dev->v4l2_dev.ctrl_handler = hdl;

	vv = kzalloc(sizeof(struct saa7146_vv), GFP_KERNEL);
	if (vv == NULL) {
		ERR("out of memory. aborting.\n");
		v4l2_ctrl_handler_free(hdl);
		v4l2_device_unregister(&dev->v4l2_dev);
		return -ENOMEM;
	}
	ext_vv->vid_ops = saa7146_video_ioctl_ops;
	ext_vv->vbi_ops = saa7146_vbi_ioctl_ops;
	ext_vv->core_ops = &saa7146_video_ioctl_ops;

	DEB_EE("dev:%p\n", dev);

	/* set default values for video parts of the saa7146 */
	saa7146_write(dev, BCS_CTRL, 0x80400040);

	/* enable video-port pins */
	saa7146_write(dev, MC1, (MASK_10 | MASK_26));

	/* save per-device extension data (one extension can
	   handle different devices that might need different
	   configuration data) */
	dev->ext_vv_data = ext_vv;

	saa7146_video_uops.init(dev,vv);
	if (dev->ext_vv_data->capabilities & V4L2_CAP_VBI_CAPTURE)
		saa7146_vbi_uops.init(dev,vv);

	fmt = &vv->video_fmt;
	fmt->width = 384;
	fmt->height = 288;
	fmt->pixelformat = V4L2_PIX_FMT_BGR24;
	fmt->field = V4L2_FIELD_INTERLACED;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	fmt->bytesperline = 3 * fmt->width;
	fmt->sizeimage = fmt->bytesperline * fmt->height;

	vbi = &vv->vbi_fmt;
	vbi->sampling_rate	= 27000000;
	vbi->offset		= 248; /* todo */
	vbi->samples_per_line	= 720 * 2;
	vbi->sample_format	= V4L2_PIX_FMT_GREY;

	/* fixme: this only works for PAL */
	vbi->start[0] = 5;
	vbi->count[0] = 16;
	vbi->start[1] = 312;
	vbi->count[1] = 16;

	timer_setup(&vv->vbi_read_timeout, NULL, 0);

	dev->vv_data = vv;
	dev->vv_callback = &vv_callback;

	return 0;
}
EXPORT_SYMBOL_GPL(saa7146_vv_init);

int saa7146_vv_release(struct saa7146_dev* dev)
{
	struct saa7146_vv *vv = dev->vv_data;

	DEB_EE("dev:%p\n", dev);

	v4l2_device_unregister(&dev->v4l2_dev);
	v4l2_ctrl_handler_free(&dev->ctrl_handler);
	kfree(vv);
	dev->vv_data = NULL;
	dev->vv_callback = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(saa7146_vv_release);

int saa7146_register_device(struct video_device *vfd, struct saa7146_dev *dev,
			    char *name, int type)
{
	struct vb2_queue *q;
	int err;
	int i;

	DEB_EE("dev:%p, name:'%s', type:%d\n", dev, name, type);

	vfd->fops = &video_fops;
	if (type == VFL_TYPE_VIDEO) {
		vfd->ioctl_ops = &dev->ext_vv_data->vid_ops;
		q = &dev->vv_data->video_dmaq.q;
	} else {
		vfd->ioctl_ops = &dev->ext_vv_data->vbi_ops;
		q = &dev->vv_data->vbi_dmaq.q;
	}
	vfd->release = video_device_release_empty;
	vfd->lock = &dev->v4l2_lock;
	vfd->v4l2_dev = &dev->v4l2_dev;
	vfd->tvnorms = 0;
	for (i = 0; i < dev->ext_vv_data->num_stds; i++)
		vfd->tvnorms |= dev->ext_vv_data->stds[i].id;
	strscpy(vfd->name, name, sizeof(vfd->name));
	vfd->device_caps = V4L2_CAP_VIDEO_CAPTURE |
			   V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	vfd->device_caps |= dev->ext_vv_data->capabilities;
	if (type == VFL_TYPE_VIDEO) {
		vfd->device_caps &=
			~(V4L2_CAP_VBI_CAPTURE | V4L2_CAP_SLICED_VBI_OUTPUT);
	} else if (vfd->device_caps & V4L2_CAP_SLICED_VBI_OUTPUT) {
		vfd->vfl_dir = VFL_DIR_TX;
		vfd->device_caps &= ~(V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
				      V4L2_CAP_AUDIO | V4L2_CAP_TUNER);
	} else {
		vfd->device_caps &= ~V4L2_CAP_VIDEO_CAPTURE;
	}

	q->type = type == VFL_TYPE_VIDEO ? V4L2_BUF_TYPE_VIDEO_CAPTURE : V4L2_BUF_TYPE_VBI_CAPTURE;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->io_modes = VB2_MMAP | VB2_READ | VB2_DMABUF;
	q->ops = type == VFL_TYPE_VIDEO ? &video_qops : &vbi_qops;
	q->mem_ops = &vb2_dma_sg_memops;
	q->drv_priv = dev;
	q->gfp_flags = __GFP_DMA32;
	q->buf_struct_size = sizeof(struct saa7146_buf);
	q->lock = &dev->v4l2_lock;
	q->min_queued_buffers = 2;
	q->dev = &dev->pci->dev;
	err = vb2_queue_init(q);
	if (err)
		return err;
	vfd->queue = q;

	video_set_drvdata(vfd, dev);

	err = video_register_device(vfd, type, -1);
	if (err < 0) {
		ERR("cannot register v4l2 device. skipping.\n");
		return err;
	}

	pr_info("%s: registered device %s [v4l2]\n",
		dev->name, video_device_node_name(vfd));
	return 0;
}
EXPORT_SYMBOL_GPL(saa7146_register_device);

int saa7146_unregister_device(struct video_device *vfd, struct saa7146_dev *dev)
{
	DEB_EE("dev:%p\n", dev);

	video_unregister_device(vfd);
	return 0;
}
EXPORT_SYMBOL_GPL(saa7146_unregister_device);

static int __init saa7146_vv_init_module(void)
{
	return 0;
}


static void __exit saa7146_vv_cleanup_module(void)
{
}

module_init(saa7146_vv_init_module);
module_exit(saa7146_vv_cleanup_module);

MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_DESCRIPTION("video4linux driver for saa7146-based hardware");
MODULE_LICENSE("GPL");
