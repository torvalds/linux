/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD Licence, GNU General Public License
 * as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/freezer.h>
#include <media/videobuf-dma-contig.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>


#define VOUT_NR			1
#define VOUT_NAME		"rk29_vout"

#define VOUT_WIDTH		1024
#define VOUT_HEIGHT		768

#define norm_maxw() 	1920
#define norm_maxh() 	1080

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define frames_to_ms(frames)					\
	((frames * WAKE_NUMERATOR * 1000) / WAKE_DENOMINATOR)


#if 1
#define rk29_vout_dbg(dev, format, arg...)		\
	dev_printk(KERN_INFO , dev , format , ## arg)
#else
#define rk29_vout_dbg(dev, format, arg...)	

#endif


static unsigned int vid_limit = 16; //16M

struct rk29_vid_device {
	struct rk29_vout_device *vouts[VOUT_NR];
	struct v4l2_device 		v4l2_dev;

	struct device 			*dev;
};
struct rk29_vout_device {
	int 					vid;
	int						opened;
	int						first_int;

	struct rk29_vid_device 	*vid_dev;
	enum v4l2_buf_type 		type;
	enum v4l2_memory 		memory;

	struct video_device 	*vfd;
	struct videobuf_queue 	vq;

	spinlock_t				lock;
	struct mutex		   	mutex;

	struct list_head		active;
	struct task_struct		*kthread;

	struct videobuf_buffer *cur_frm, next_frm;

	struct v4l2_pix_format 	pix;
	struct v4l2_rect 		crop;
	struct v4l2_rect 		win;
	struct v4l2_control 	ctrl;
	struct rk29_vaddr 		vaddr;
};

struct rk29_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct rk29_fmt formats[] = {
	{
		.name     = "4:2:0, Y/CbCr",
		.fourcc   = V4L2_PIX_FMT_NV12,
		.depth    = 12,
	},
};

static struct rk29_fmt *get_format(struct v4l2_format *f)
{
	struct rk29_fmt *fmt;
	unsigned int k;

	for (k = 0; k < ARRAY_SIZE(formats); k++) {
		fmt = &formats[k];
		if (fmt->fourcc == f->fmt.pix.pixelformat)
			break;
	}

	if (k == ARRAY_SIZE(formats))
		return NULL;

	return &formats[k];
}
/*  open lcd controler */
static int lcd_open(struct rk29_vout_device *vout)
{
	rk29_vout_dbg(vout->vid_dev->dev, "%s:\n", __func__);
	rk29_vout_dbg(vout->vid_dev->dev, "pix.width = %u, pix.height = %u,pix.bytesperline = %u, pix.sizeimage = %u\n", 
		vout->pix.width, vout->pix.height,vout->pix.bytesperline, vout->pix.sizeimage);
	rk29_vout_dbg(vout->vid_dev->dev, "crop.width = %u, crop.height = %u,crop.left = %u, crop.top = %u\n", 
		vout->crop.width, vout->crop.height,vout->crop.left, vout->crop.top);
	rk29_vout_dbg(vout->vid_dev->dev, "win.width = %u, win.height = %u,win.left = %u, win.top = %u\n\n", 
		vout->win.width, vout->win.height,vout->win.left, vout->win.top);
	return 0;
}
/* close lcd_controler */
static int lcd_close(struct rk29_vout_device *vout)
{
	rk29_vout_dbg(vout->vid_dev->dev, "%s.\n", __func__);
	return 0;
}
/* set lcd_controler var */
static int lcd_set_var(struct rk29_vout_device *vout)
{
	/*
	rk29_vout_dbg(vout->vid_dev->dev, "%s.\n", __func__);
	rk29_vout_dbg(vout->vid_dev->dev, "pix.width = %u, pix.height = %u,pix.bytesperline = %u, pix.sizeimage = %u\n", 
		vout->pix.width, vout->pix.height,vout->pix.bytesperline, vout->pix.sizeimage);
	rk29_vout_dbg(vout->vid_dev->dev, "crop.width = %u, crop.height = %u,crop.left = %u, crop.top = %u\n", 
		vout->crop.width, vout->crop.height,vout->crop.left, vout->crop.top);
	rk29_vout_dbg(vout->vid_dev->dev, "win.width = %u, win.height = %u,win.left = %u, win.top = %u\n\n", 
		vout->win.width, vout->win.height,vout->win.left, vout->win.top);
	*/
	return 0;
}
/* set lcd_controler addr */
static int lcd_set_addr(struct rk29_vout_device *vout)
{
	//rk29_vout_dbg(vout->vid_dev->dev, "%s: set addr 0x%08x\n", __func__, vout->vaddr.base[0]);

	mdelay(10);
	return 0;
}

static void rk29_thread_dequeue_buff(struct rk29_vout_device *vout)
{
	unsigned long flags = 0;
	
	spin_lock_irqsave(&vout->lock, flags);

	if (list_empty(&vout->active)) {
		rk29_vout_dbg(vout->vid_dev->dev, "No active queue to serve\n");
		goto unlock;
	}
	if(vout->first_int){
		vout->first_int = 0;
	}else {
		vout->cur_frm->state = VIDEOBUF_DONE;
		wake_up(&vout->cur_frm->done);
	}
	vout->cur_frm = list_entry(vout->active.next, struct videobuf_buffer, queue);
	list_del(&vout->cur_frm->queue);
	vout->vaddr = vout->cur_frm->vaddr;
	rk29_vout_dbg(vout->vid_dev->dev, "lcd_set_addr: index = %d, addr = 0x%08x\n",
		vout->cur_frm->i,vout->vaddr.base[0]);
	lcd_set_addr(vout);
	//vout->cur_frm->state = VIDEOBUF_ACTIVE;
	
unlock:
	spin_unlock_irqrestore(&vout->lock, flags);
}
static int rk29_vout_thread(void *data)
{
	struct rk29_vout_device *vout = data;

	//set_freezable();

	for (;;) {
		rk29_thread_dequeue_buff(vout);

		if (kthread_should_stop())
			break;
	}

	return 0;
}

static int rk29_start_thread(struct rk29_vout_device *vout)
{
	rk29_vout_dbg(vout->vid_dev->dev, "%s\n", __func__);

	vout->kthread = kthread_run(rk29_vout_thread, vout, "rk29_vout");
	if (IS_ERR(vout->kthread)) {
		dev_err(vout->vid_dev->dev, "kernel_thread() failed\n");
		return PTR_ERR(vout->kthread);
	}
	return 0;
}
static void rk29_stop_thread(struct rk29_vout_device *vout)
{
	rk29_vout_dbg(vout->vid_dev->dev, "%s\n", __func__);
	if (vout->kthread) {
		kthread_stop(vout->kthread);
		vout->kthread = NULL;
	}
}



static int vidioc_g_fmt_vid_out(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct rk29_vout_device *vout = priv;

	f->fmt.pix = vout->pix;
	return 0;
}
static int vidioc_try_fmt_vid_out(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct rk29_vout_device *vout = priv;
	struct rk29_fmt *fmt;
	unsigned int maxw, maxh;

	fmt = get_format(f);
	if (!fmt) {
		rk29_vout_dbg(vout->vid_dev->dev, "Fourcc format (0x%08x) invalid.\n",
			f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	maxw  = norm_maxw();
	maxh  = norm_maxh();

	v4l_bound_align_image(&f->fmt.pix.width, 48, maxw, 2,
			      &f->fmt.pix.height, 32, maxh, 0, 0);
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}
static int vidioc_s_fmt_vid_out(struct file *file, void *priv,
					struct v4l2_format *f)
{
	int ret = 0;
	struct rk29_vout_device *vout = priv;

	ret = vidioc_try_fmt_vid_out(file, vout, f);
	if (ret < 0)
		return ret;

	mutex_lock(&vout->mutex);

	if (videobuf_queue_is_busy(&vout->vq)) {
		rk29_vout_dbg(vout->vid_dev->dev, "%s queue busy\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	vout->pix 			= f->fmt.pix;
	vout->vq.field 	= f->fmt.pix.field;
	vout->type          = f->type;
	lcd_set_var(vout);
	ret = 0;

out:
	mutex_unlock(&vout->mutex);

	return ret;
}
static int vidioc_g_fmt_vid_overlay(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct rk29_vout_device *vout = priv;
	
	f->fmt.win.w.width = vout->win.width;
	f->fmt.win.w.height = vout->win.height;
		return 0;
}
static int vidioc_try_fmt_vid_overlay(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct rk29_vout_device *vout = priv;

	if (f->fmt.win.w.left < 0) {
		f->fmt.win.w.width += f->fmt.win.w.left;
		f->fmt.win.w.left = 0;
	}
	if (f->fmt.win.w.top < 0) {
		f->fmt.win.w.height += f->fmt.win.w.top;
		f->fmt.win.w.top = 0;
	}
	f->fmt.win.w.width = (f->fmt.win.w.width < vout->win.width) ?
		f->fmt.win.w.width : vout->win.width;
	f->fmt.win.w.height = (f->fmt.win.w.height < vout->win.height) ?
		f->fmt.win.w.height : vout->win.height;
	if (f->fmt.win.w.left + f->fmt.win.w.width > vout->win.width)
		f->fmt.win.w.width = vout->win.width - f->fmt.win.w.left;
	if (f->fmt.win.w.top + f->fmt.win.w.height > vout->win.height)
		f->fmt.win.w.height = vout->win.height - f->fmt.win.w.top;
	f->fmt.win.w.width &= ~1;
	f->fmt.win.w.height &= ~1;

	if (f->fmt.win.w.width <= 0 || f->fmt.win.w.height <= 0)
		return -EINVAL;

	f->fmt.win.field = V4L2_FIELD_NONE;

	return 0;
}
static int vidioc_s_fmt_vid_overlay(struct file *file, void *priv,
					struct v4l2_format *f)
{
	int ret = 0;
	struct rk29_vout_device *vout = priv;

	ret = vidioc_try_fmt_vid_overlay(file, vout, f);
	if (ret < 0)
		return ret;

	mutex_lock(&vout->mutex);
	vout->win = f->fmt.win.w;
	lcd_set_var(vout);
	mutex_unlock(&vout->mutex);

	return ret;
}


static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct rk29_vout_device *vout = priv;

	if ((p->type != V4L2_BUF_TYPE_VIDEO_OUTPUT) || (p->count < 0))
		return -EINVAL;

	if (V4L2_MEMORY_USERPTR != p->memory)
		return -EINVAL;
	
	return videobuf_reqbufs(&vout->vq, p);
}
static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct rk29_vout_device *vout = priv;

	if (p->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;
	if (V4L2_MEMORY_USERPTR != p->memory)
		return -EINVAL;
	rk29_vout_dbg(vout->vid_dev->dev, "qbuf: index = %d, addr = 0x%08x\n",
		p->index, ((struct rk29_vaddr *)p->m.userptr)->base[0]);

	vout->vaddr = *((struct rk29_vaddr *)(p->m.userptr));
	return (videobuf_qbuf(&vout->vq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	int ret = 0;
	struct rk29_vout_device *vout = priv;

	ret = (videobuf_dqbuf(&vout->vq, p, file->f_flags & O_NONBLOCK));

	*((struct rk29_vaddr *)p->m.userptr) = vout->vq.bufs[p->index]->vaddr;
	rk29_vout_dbg(vout->vid_dev->dev, "dqbuf: index = %d,addr = 0x%08x\n", 
			p->index, ((struct rk29_vaddr *)p->m.userptr)->base[0]);

	return ret;
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	int ret = 0;
	struct rk29_vout_device *vout = priv;

	if (vout->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;
	if (i != vout->type)
		return -EINVAL;

	mutex_lock(&vout->mutex);
	
	ret = videobuf_streamon(&vout->vq);
	if(ret < 0)
		goto streamon_err;

	if(lcd_open(vout) < 0) {
		dev_err(vout->vid_dev->dev,"lcd_open error\n");
		ret = -EIO;
		goto streamon_err1;
	}

	vout->first_int = 1;
	INIT_LIST_HEAD(&vout->active);
	if(rk29_start_thread(vout) < 0) {
		dev_err(vout->vid_dev->dev,"rk29_start_thread error\n");
		ret = -EINVAL;
		goto streamon_err2;
	}
	mutex_unlock(&vout->mutex);
	return 0;
streamon_err2:
	lcd_close(vout);
streamon_err1:
	ret = videobuf_streamoff(&vout->vq);
streamon_err:
	mutex_unlock(&vout->mutex);
	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct rk29_vout_device *vout = priv;

	if (vout->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;
	if (i != vout->type)
		return -EINVAL;
	
	if(lcd_close(vout) < 0)
		return -EINVAL;

	rk29_stop_thread(vout);
	
	INIT_LIST_HEAD(&vout->active);
	videobuf_mmap_free(&vout->vq);
	return videobuf_streamoff(&vout->vq);
}
static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct rk29_vout_device *vout = priv;

	*ctrl = vout->ctrl;

	return 0;
}
static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctrl)
{
	struct rk29_vout_device *vout = priv;

	vout->ctrl = *ctrl;
	lcd_set_var(vout);
	return 0;
}
static int vidioc_g_crop(struct file *file, void *priv,
			 struct v4l2_crop *crop)
{
	struct rk29_vout_device *vout = priv;

	if (crop->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;
	crop->c = vout->crop;
	
	return 0;
}
static int vidioc_s_crop(struct file *file, void *priv,
				struct v4l2_crop *crop)
{
	struct rk29_vout_device *vout = priv;

	if (crop->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	if (vout->vq.streaming) {
		rk29_vout_dbg(vout->vid_dev->dev, "vedio is running\n");
		return -EBUSY;
	}
	if ((crop->c.width < 0) || (crop->c.height < 0)) {
		rk29_vout_dbg(vout->vid_dev->dev, "The crop rect must be bigger than 0\n");
		return -EINVAL;
	}

	if ((crop->c.width > vout->pix.width) || (crop->c.height > vout->pix.height)) {
		rk29_vout_dbg(vout->vid_dev->dev, "The crop width/height must be smaller than "
			"%d and %d\n", vout->pix.width, vout->pix.height);
		return -EINVAL;
	}

	if ((crop->c.left < 0) || (crop->c.top < 0)) {
		rk29_vout_dbg(vout->vid_dev->dev, "The crop left, top must be  bigger than 0\n");
		return -EINVAL;
	}

	if ((crop->c.left > vout->pix.width) || (crop->c.top > vout->pix.height)) {
		rk29_vout_dbg(vout->vid_dev->dev, "The crop left, top must be smaller than %d, %d\n",
			vout->pix.width, vout->pix.height);
		return -EINVAL;
	}

	if ((crop->c.left + crop->c.width) > vout->pix.width) {
		rk29_vout_dbg(vout->vid_dev->dev, "The crop rect must be in bound rect\n");
		return -EINVAL;
	}

	if ((crop->c.top + crop->c.height) > vout->pix.height) {
		rk29_vout_dbg(vout->vid_dev->dev, "The crop rect must be in bound rect\n");
		return -EINVAL;
	}

	vout->crop = crop->c;
	lcd_set_var(vout);
	return 0;
}

static int buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct rk29_vout_device *vout  = vq->priv_data;

	*size = norm_maxw() * norm_maxh() * 2;

	if (0 == *count)
		*count = 32;

	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;

	rk29_vout_dbg(vout->vid_dev->dev, "buffer_setup: count = %d, size = %d\n",
			*count, *size);
	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	if (in_interrupt())
		BUG();
	videobuf_dma_contig_free(vq, vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static int buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	//struct rk29_vout_device *vout  = vq->priv_data;

	vb->state = VIDEOBUF_PREPARED;

	return 0;
}
static void buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct rk29_vout_device *vout  = vq->priv_data;

	vb->vaddr = vout->vaddr;
	list_add_tail(&vb->queue, &vout->active);
	vb->state = VIDEOBUF_QUEUED;
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	//struct rk29_vout_device *vout  = vq->priv_data;
	
	free_buffer(vq, vb);
	
	vb->state = VIDEOBUF_NEEDS_INIT;
	return;
}

static struct videobuf_queue_ops rk29_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

static int rk29_vout_open(struct file *file)
{
	struct rk29_vout_device *vout = video_drvdata(file);

	rk29_vout_dbg(vout->vid_dev->dev, "Entering %s\n", __func__);

	if (vout == NULL)
		return -ENODEV;
	
	mutex_lock(&vout->mutex);
	if (vout->opened) {
		mutex_unlock(&vout->mutex);
		return -EBUSY;
	}
	vout->opened += 1;
	mutex_unlock(&vout->mutex);

	file->private_data = vout;

	vout->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	INIT_LIST_HEAD(&vout->active);

	spin_lock_init(&vout->lock);

	videobuf_queue_dma_contig_init(&vout->vq, &rk29_video_qops,
			vout->vq.dev, &vout->lock, vout->type, V4L2_FIELD_NONE,
			sizeof(struct videobuf_buffer), vout);

	rk29_vout_dbg(vout->vid_dev->dev, "Exiting %s\n", __func__);

	return 0;
}
static int rk29_vout_release(struct file *file)
{
	struct rk29_vout_device *vout = file->private_data;
	
	rk29_vout_dbg(vout->vid_dev->dev, "Entering %s\n", __func__);
	
	if (!vout)
		return 0;

	rk29_stop_thread(vout);
	lcd_close(vout);
	INIT_LIST_HEAD(&vout->active);
	videobuf_mmap_free(&vout->vq);
	videobuf_stop(&vout->vq);
	
	mutex_lock(&vout->mutex);
	vout->opened -= 1;
	mutex_unlock(&vout->mutex);
	file->private_data = NULL;
	rk29_vout_dbg(vout->vid_dev->dev, "Exiting %s\n", __func__);
	
	return 0;
}

static const struct v4l2_file_operations rk29_vout_fops = {
	.owner		= THIS_MODULE,
	.open		= rk29_vout_open,
	.release	= rk29_vout_release,
	.ioctl		= video_ioctl2,
};

static const struct v4l2_ioctl_ops rk29_ioctl_ops = {
	.vidioc_g_fmt_vid_out     	= vidioc_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out   	= vidioc_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out     	= vidioc_s_fmt_vid_out,
	.vidioc_g_fmt_vid_overlay	= vidioc_g_fmt_vid_overlay,
	.vidioc_try_fmt_vid_overlay = vidioc_try_fmt_vid_overlay,
	.vidioc_s_fmt_vid_overlay	= vidioc_s_fmt_vid_overlay,
	.vidioc_reqbufs       		= vidioc_reqbufs,
	.vidioc_qbuf          		= vidioc_qbuf,
	.vidioc_dqbuf         		= vidioc_dqbuf,
	.vidioc_g_ctrl        		= vidioc_g_ctrl,
	.vidioc_s_ctrl        		= vidioc_s_ctrl,
	.vidioc_g_crop        		= vidioc_g_crop,
	.vidioc_s_crop        		= vidioc_s_crop,
	.vidioc_streamon      		= vidioc_streamon,
	.vidioc_streamoff     		= vidioc_streamoff,
};

static int rk29_vout_create_video_devices(struct platform_device *pdev)
{
	int ret = 0, k;
	struct rk29_vout_device *vout;
	struct video_device *vfd = NULL;
	struct v4l2_device *v4l2_dev = platform_get_drvdata(pdev);
	struct rk29_vid_device *vid_dev = container_of(v4l2_dev,
			struct rk29_vid_device, v4l2_dev);

	for (k = 0; k < VOUT_NR; k++) {

		vout = kzalloc(sizeof(struct rk29_vout_device), GFP_KERNEL);
		if (!vout) {
			dev_err(&pdev->dev, ": could not allocate memory\n");
			return -ENOMEM;
		}

		vout->vid = k;
		vid_dev->vouts[k] = vout;
		vout->vid_dev = vid_dev;

		vout->pix.width = norm_maxw();
		vout->pix.height = norm_maxh();
		
		vout->pix.pixelformat = formats[0].fourcc;
		vout->pix.field = V4L2_FIELD_NONE;
		vout->pix.bytesperline = (vout->pix.width * formats[0].depth) >> 3;
		vout->pix.sizeimage = vout->pix.height * vout->pix.bytesperline;
		vout->pix.colorspace = V4L2_COLORSPACE_JPEG;
		vout->pix.priv = 0;

		vout->win.width = VOUT_WIDTH;
		vout->win.height = VOUT_HEIGHT;
		vout->win.left = 0;
		vout->win.top = 0;

		vfd = vout->vfd = video_device_alloc();
		if (!vfd){
			dev_err(&pdev->dev, "could not allocate video device struct\n");
			ret = -ENOMEM;
			goto error0;
		}
		vfd->release = video_device_release;
		vfd->ioctl_ops = &rk29_ioctl_ops;
		strlcpy(vfd->name, VOUT_NAME, sizeof(vfd->name));
		vfd->fops = &rk29_vout_fops;
		vfd->v4l2_dev = &vout->vid_dev->v4l2_dev;
		vfd->minor = -1;
		mutex_init(&vout->mutex);
		
		if (video_register_device(vfd, VFL_TYPE_GRABBER, k + 1) < 0) {
			dev_err(&pdev->dev, ": Could not register "
					"Video for Linux device\n");
			vfd->minor = -1;
			ret = -ENODEV;
			goto error1;
		}
		video_set_drvdata(vfd, vout);
		goto success;
error1:
		video_device_release(vfd);
error0:
		kfree(vout);
		return ret;

success:
		dev_info(&pdev->dev, "registered and initialized"
				" video device %d\n", vfd->minor);
	}

	return 0;
}

static int __init rk29_vout_probe(struct platform_device *pdev)
{
	struct rk29_vid_device *vid_dev = NULL;
	int ret = 0;

	vid_dev = kzalloc(sizeof(struct rk29_vid_device), GFP_KERNEL);
	if (!vid_dev)
		return -ENOMEM;

	vid_dev->dev = &pdev->dev;

	if ((ret = v4l2_device_register(&pdev->dev, &vid_dev->v4l2_dev)) < 0){
		dev_err(&pdev->dev, "v4l2_device_register failed\n");
		goto probe_err0;
	}

	if((ret = rk29_vout_create_video_devices(pdev)) < 0)
		goto probe_err1;

	return 0;

probe_err1:
	v4l2_device_unregister(&vid_dev->v4l2_dev);
probe_err0:
	kfree(vid_dev);
	return ret;
}
static int rk29_vout_remove(struct platform_device *pdev)
{
	int k;
	struct video_device *vfd = NULL;
	struct rk29_vout_device *vout;
	struct v4l2_device *v4l2_dev = platform_get_drvdata(pdev);
	struct rk29_vid_device *vid_dev = container_of(v4l2_dev, struct
			rk29_vid_device, v4l2_dev);

	v4l2_device_unregister(v4l2_dev);
	for (k = 0; k < VOUT_NR; k++) {
		vout = vid_dev->vouts[k];
		if (!vout)
			break;
		vfd = vout->vfd;
		if (vfd)
			video_device_release(vfd);
	}

	kfree(vid_dev);
	
	return 0;
}

static struct platform_driver rk29_vout_driver = {
	.driver = {
		.name = VOUT_NAME,
	},
	.probe = rk29_vout_probe,
	.remove = rk29_vout_remove,
};

static int __init rk29_vout_init(void)
{
	if (platform_driver_register(&rk29_vout_driver) != 0) {
		printk(KERN_ERR VOUT_NAME ":Could not register Video driver\n");
		return -EINVAL;
	}
	return 0;
}

static void rk29_vout_exit(void)
{
	platform_driver_unregister(&rk29_vout_driver);
}

module_init(rk29_vout_init);
module_exit(rk29_vout_exit);

