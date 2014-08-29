/*
 * Ion Video driver - This code emulates a real video device with v4l2 api,
 * used for surface video display.
 *
 *Author: Shuai Cao <shuai.cao@amlogic.com>
 *
 */
#include "ionvideo.h"

#define IONVIDEO_MODULE_NAME "ionvideo"

#define IONVIDEO_VERSION "1.0"
#define RECEIVER_NAME "ionvideo"

static int is_actived = 0;

static unsigned video_nr = 13;

static u64 last_pts_us64 = 0;

module_param(video_nr, uint, 0644);
MODULE_PARM_DESC(video_nr, "videoX start number, 13 is autodetect");

static unsigned n_devs = 1;
module_param(n_devs, uint, 0644);
MODULE_PARM_DESC(n_devs, "number of video devices to create");

static unsigned debug = 0;
module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
module_param(vid_limit, uint, 0644);
MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static unsigned int freerun_mode = 1;
module_param(freerun_mode, uint, 0664);
MODULE_PARM_DESC(freerun_mode, "av synchronization");

static unsigned int skip_frames = 0;
module_param(skip_frames, uint, 0664);
MODULE_PARM_DESC(skip_frames, "skip frames");


static const struct ionvideo_fmt formats[] = {
    {
        .name = "RGB32 (LE)",
        .fourcc = V4L2_PIX_FMT_RGB32, /* argb */
        .depth = 32,
    },
    {
        .name = "RGB565 (LE)",
        .fourcc = V4L2_PIX_FMT_RGB565, /* gggbbbbb rrrrrggg */
        .depth = 16,
    },
    {
        .name = "RGB24 (LE)",
        .fourcc = V4L2_PIX_FMT_RGB24, /* rgb */
        .depth = 24,
    },
    {
        .name = "RGB24 (BE)",
        .fourcc = V4L2_PIX_FMT_BGR24, /* bgr */
        .depth = 24,
    },
    {
        .name = "12  Y/CbCr 4:2:0",
        .fourcc   = V4L2_PIX_FMT_NV12,
        .depth    = 12,
    },
    {
        .name     = "12  Y/CrCb 4:2:0",
        .fourcc   = V4L2_PIX_FMT_NV21,
        .depth    = 12,
    },
    {
        .name     = "YUV420P",
        .fourcc   = V4L2_PIX_FMT_YUV420,
        .depth    = 12,
    },
    {
        .name     = "YVU420P",
        .fourcc   = V4L2_PIX_FMT_YVU420,
        .depth    = 12,
    }
};

static const struct ionvideo_fmt *__get_format(u32 pixelformat)
{
    const struct ionvideo_fmt *fmt;
    unsigned int k;

    for (k = 0; k < ARRAY_SIZE(formats); k++) {
        fmt = &formats[k];
        if (fmt->fourcc == pixelformat)
            break;
    }

    if (k == ARRAY_SIZE(formats))
        return NULL;

    return &formats[k];
}

static const struct ionvideo_fmt *get_format(struct v4l2_format *f)
{
    return __get_format(f->fmt.pix.pixelformat);
}

static LIST_HEAD (ionvideo_devlist);

static DEFINE_SPINLOCK(ion_states_lock);
static int  ionvideo_vf_get_states(vframe_states_t *states)
{
    int ret = -1;
    unsigned long flags;
    struct vframe_provider_s *vfp;
    vfp = vf_get_provider(RECEIVER_NAME);
    spin_lock_irqsave(&ion_states_lock, flags);
    if (vfp && vfp->ops && vfp->ops->vf_states) {
        ret=vfp->ops->vf_states(states, vfp->op_arg);
    }
    spin_unlock_irqrestore(&ion_states_lock, flags);
    return ret;
}



/* ------------------------------------------------------------------
 DMA and thread functions
 ------------------------------------------------------------------*/
unsigned get_ionvideo_debug(void) {
    return debug;
}
EXPORT_SYMBOL(get_ionvideo_debug);

int is_ionvideo_active(void) {
    return is_actived;
}
EXPORT_SYMBOL(is_ionvideo_active);

static void videoc_omx_compute_pts(struct ionvideo_dev *dev, struct vframe_s* vf) {
    if (dev->pts == 0) {
        if (dev->receiver_register == 0) {
            dev->pts = last_pts_us64 + (DUR2PTS(vf->duration)*100/9);
        }
    }
    if (dev->receiver_register) {    
        dev->receiver_register = 0;
    }
    last_pts_us64 = dev->pts;  
}

static int ionvideo_fillbuff(struct ionvideo_dev *dev, struct ionvideo_buffer *buf) {

    struct vframe_s* vf;
    struct vb2_buffer *vb = &(buf->vb);
    int ret = 0;
//-------------------------------------------------------
    vf = vf_get(RECEIVER_NAME);
    if (!vf) {
        return -EAGAIN;
    }
    if (vf && dev->once_record == 1) {
    	dev->once_record = 0;
    	if ((vf->type & VIDTYPE_INTERLACE_BOTTOM) == 0x3) {
    		dev->ppmgr2_dev.bottom_first = 1;
    	} else {
    		dev->ppmgr2_dev.bottom_first = 0;
    	}
    }
    if (freerun_mode == 0) {
        if ((vf->type & 0x1) == VIDTYPE_INTERLACE) {
            if ((dev->ppmgr2_dev.bottom_first && (vf->type & 0x2)) || (dev->ppmgr2_dev.bottom_first == 0 && ((vf->type & 0x2) == 0))) {
                buf->pts = vf->pts;
                buf->duration = vf->duration;
            }
        } else {
            buf->pts = vf->pts;
            buf->duration = vf->duration;
        }
        ret = ppmgr2_process(vf, &dev->ppmgr2_dev, vb->v4l2_buf.index);
        if (ret) {
            vf_put(vf, RECEIVER_NAME);
            return ret;
        }
        vf_put(vf, RECEIVER_NAME);
    } else {
        if ((vf->type & 0x1) == VIDTYPE_INTERLACE) {
            if ((dev->ppmgr2_dev.bottom_first && (vf->type & 0x2)) || (dev->ppmgr2_dev.bottom_first == 0 && ((vf->type & 0x2) == 0)))
                dev->pts = vf->pts_us64;
        } else
            dev->pts = vf->pts_us64;
        ret = ppmgr2_process(vf, &dev->ppmgr2_dev, vb->v4l2_buf.index);
        if (ret) {
            vf_put(vf, RECEIVER_NAME);
            return ret;
        }
        videoc_omx_compute_pts(dev, vf);
        vf_put(vf, RECEIVER_NAME);
        buf->vb.v4l2_buf.timestamp.tv_sec = dev->pts >> 32;
        buf->vb.v4l2_buf.timestamp.tv_usec = dev->pts & 0xFFFFFFFF;
    }
//-------------------------------------------------------
    return 0;
}

static int ionvideo_size_changed(struct ionvideo_dev *dev, struct vframe_s* vf) {
    int aw = vf->width;
    int ah = vf->height;

    v4l_bound_align_image(&aw, 48, MAX_WIDTH, 5, &ah, 32, MAX_HEIGHT, 0, 0);
    dev->c_width = aw;
    dev->c_height = ah;
    if (aw != dev->width || ah != dev->height) {
        dprintk(dev, 2, "Video frame size changed w:%d h:%d\n", aw, ah);
        return -EAGAIN;
    }
    return 0;
}

static void ionvideo_thread_tick(struct ionvideo_dev *dev) {
    struct ionvideo_dmaqueue *dma_q = &dev->vidq;
    struct ionvideo_buffer *buf;
    unsigned long flags = 0;
    struct vframe_s* vf;

    dprintk(dev, 4, "Thread tick\n");
    /* video seekTo clear list */

    vf = vf_peek(RECEIVER_NAME);
    if (!vf) {
        msleep(5);
        return;
    }
    if (freerun_mode == 0 && ionvideo_size_changed(dev, vf)) {
        msleep(10);
        return;
    }
    spin_lock_irqsave(&dev->slock, flags);
    if (list_empty(&dma_q->active)) {
        dprintk(dev, 3, "No active queue to serve\n");
        spin_unlock_irqrestore(&dev->slock, flags);
        msleep(5);
        return;
    }
    buf = list_entry(dma_q->active.next, struct ionvideo_buffer, list);
    spin_unlock_irqrestore(&dev->slock, flags);
    /* Fill buffer */
    if (ionvideo_fillbuff(dev, buf)) {
        return;
    }
    spin_lock_irqsave(&dev->slock, flags);
    list_del(&buf->list);
    spin_unlock_irqrestore(&dev->slock, flags);
    vb2_buffer_done(&buf->vb, VB2_BUF_STATE_DONE);
    dprintk(dev, 4, "[%p/%d] done\n", buf, buf->vb.v4l2_buf.index);
}

#define frames_to_ms(frames)					\
    ((frames * WAKE_NUMERATOR * 1000) / WAKE_DENOMINATOR)

static void ionvideo_sleep(struct ionvideo_dev *dev) {
    struct ionvideo_dmaqueue *dma_q = &dev->vidq;
    //int timeout;
    DECLARE_WAITQUEUE(wait, current);

    dprintk(dev, 4, "%s dma_q=0x%08lx\n", __func__, (unsigned long)dma_q);

    add_wait_queue(&dma_q->wq, &wait);
    if (kthread_should_stop())
        goto stop_task;

    /* Calculate time to wake up */
    //timeout = msecs_to_jiffies(frames_to_ms(1));

    ionvideo_thread_tick(dev);

    //schedule_timeout_interruptible(timeout);

stop_task:
    remove_wait_queue(&dma_q->wq, &wait);
    try_to_freeze();
}

static int ionvideo_thread(void *data) {
    struct ionvideo_dev *dev = data;

    dprintk(dev, 2, "thread started\n");

    set_freezable();

    for (;;) {
        ionvideo_sleep(dev);

        if (kthread_should_stop())
            break;
    }
    dprintk(dev, 2, "thread: exit\n");
    return 0;
}

static int ionvideo_start_generating(struct ionvideo_dev *dev) {
    struct ionvideo_dmaqueue *dma_q = &dev->vidq;

    dprintk(dev, 2, "%s\n", __func__);

    /* Resets frame counters */
    dev->ms = 0;
    //dev->jiffies = jiffies;

    dma_q->frame = 0;
    //dma_q->ini_jiffies = jiffies;
    dma_q->kthread = kthread_run(ionvideo_thread, dev, dev->v4l2_dev.name);

    if (IS_ERR(dma_q->kthread)) {
        v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
        return PTR_ERR(dma_q->kthread);
    }
    /* Wakes thread */
    wake_up_interruptible(&dma_q->wq);

    dprintk(dev, 2, "returning from %s\n", __func__);
    return 0;
}

static void ionvideo_stop_generating(struct ionvideo_dev *dev) {
    struct ionvideo_dmaqueue *dma_q = &dev->vidq;

    dprintk(dev, 2, "%s\n", __func__);

    /* shutdown control thread */
    if (dma_q->kthread) {
        kthread_stop(dma_q->kthread);
        dma_q->kthread = NULL;
    }

    /*
     * Typical driver might need to wait here until dma engine stops.
     * In this case we can abort imiedetly, so it's just a noop.
     */

    /* Release all active buffers */
    while (!list_empty(&dma_q->active)) {
        struct ionvideo_buffer *buf;
        buf = list_entry(dma_q->active.next, struct ionvideo_buffer, list);
        list_del(&buf->list);
        vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
        dprintk(dev, 2, "[%p/%d] done\n", buf, buf->vb.v4l2_buf.index);
    }
}
/* ------------------------------------------------------------------
 Videobuf operations
 ------------------------------------------------------------------*/
static int queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt, unsigned int *nbuffers, unsigned int *nplanes, unsigned int sizes[], void *alloc_ctxs[]) {
    struct ionvideo_dev *dev = vb2_get_drv_priv(vq);
    unsigned long size;

    if (fmt)
        size = fmt->fmt.pix.sizeimage;
    else
        size = (dev->width * dev->height * dev->pixelsize) >> 3;

    if (size == 0)
        return -EINVAL;

    if (0 == *nbuffers)
        *nbuffers = 32;

    while (size * *nbuffers > vid_limit * MAX_WIDTH * MAX_HEIGHT)
        (*nbuffers)--;

    *nplanes = 1;

    sizes[0] = size;

    /*
     * videobuf2-vmalloc allocator is context-less so no need to set
     * alloc_ctxs array.
     */

    dprintk(dev, 2, "%s, count=%d, size=%ld\n", __func__, *nbuffers, size);

    return 0;
}

static int buffer_prepare(struct vb2_buffer *vb) {
    struct ionvideo_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
    struct ionvideo_buffer *buf = container_of(vb, struct ionvideo_buffer, vb);
    unsigned long size;

    dprintk(dev, 2, "%s, field=%d\n", __func__, vb->v4l2_buf.field);

    BUG_ON(NULL == dev->fmt);

    /*
     * Theses properties only change when queue is idle, see s_fmt.
     * The below checks should not be performed here, on each
     * buffer_prepare (i.e. on each qbuf). Most of the code in this function
     * should thus be moved to buffer_init and s_fmt.
     */
    if (dev->width < 48 || dev->width > MAX_WIDTH || dev->height < 32 || dev->height > MAX_HEIGHT)
        return -EINVAL;

    size = (dev->width * dev->height * dev->pixelsize) >> 3;
    if (vb2_plane_size(vb, 0) < size) {
        dprintk(dev, 1, "%s data will not fit into plane (%lu < %lu)\n", __func__, vb2_plane_size(vb, 0), size);
        return -EINVAL;
    }

    vb2_set_plane_payload(&buf->vb, 0, size);

    buf->fmt = dev->fmt;

    return 0;
}

static void buffer_queue(struct vb2_buffer *vb) {
    struct ionvideo_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
    struct ionvideo_buffer *buf = container_of(vb, struct ionvideo_buffer, vb);
    struct ionvideo_dmaqueue *vidq = &dev->vidq;
    unsigned long flags = 0;

    dprintk(dev, 2, "%s\n", __func__);

    spin_lock_irqsave(&dev->slock, flags);
    list_add_tail(&buf->list, &vidq->active);
    spin_unlock_irqrestore(&dev->slock, flags);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count) {
    struct ionvideo_dev *dev = vb2_get_drv_priv(vq);
    is_actived = 1;
    dprintk(dev, 2, "%s\n", __func__);
    return ionvideo_start_generating(dev);
}

/* abort streaming and wait for last buffer */
static int stop_streaming(struct vb2_queue *vq) {
    struct ionvideo_dev *dev = vb2_get_drv_priv(vq);
    is_actived = 0;
    dprintk(dev, 2, "%s\n", __func__);
    ionvideo_stop_generating(dev);
    return 0;
}

static void ionvideo_lock(struct vb2_queue *vq) {
    struct ionvideo_dev *dev = vb2_get_drv_priv(vq);
    mutex_lock(&dev->mutex);
}

static void ionvideo_unlock(struct vb2_queue *vq) {
    struct ionvideo_dev *dev = vb2_get_drv_priv(vq);
    mutex_unlock(&dev->mutex);
}

static const struct vb2_ops ionvideo_video_qops = {
    .queue_setup = queue_setup,
    .buf_prepare = buffer_prepare,
    .buf_queue = buffer_queue,
    .start_streaming = start_streaming,
    .stop_streaming = stop_streaming,
    .wait_prepare = ionvideo_unlock,
    .wait_finish = ionvideo_lock,
};

/* ------------------------------------------------------------------
 IOCTL vidioc handling
 ------------------------------------------------------------------*/
static int vidioc_open(struct file *file) {
    struct ionvideo_dev *dev = video_drvdata(file);
    if (dev->fd_num > 0 || ppmgr2_init(&(dev->ppmgr2_dev)) < 0) {
        return -EBUSY;
    }
    dev->fd_num++;
    dev->pts = 0;
    dev->c_width = 0;
    dev->c_height = 0;
    dev->once_record = 1;
    dev->ppmgr2_dev.bottom_first = 0;
    skip_frames = 0;
    dprintk(dev, 2, "vidioc_open\n");
    printk("ionvideo open\n");
    return v4l2_fh_open(file);
}

static int vidioc_release(struct file *file) {
    struct ionvideo_dev *dev = video_drvdata(file);
    ionvideo_stop_generating(dev);
    printk("ionvideo_stop_generating!!!!\n");
    ppmgr2_release(&(dev->ppmgr2_dev));
    dprintk(dev, 2, "vidioc_release\n");
    printk("ionvideo release\n");
    if (dev->fd_num > 0) {
        dev->fd_num--;
    }
    dev->once_record = 0;
    return vb2_fop_release(file);
}

static int vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap) {
    struct ionvideo_dev *dev = video_drvdata(file);

    strcpy(cap->driver, "ionvideo");
    strcpy(cap->card, "ionvideo");
    snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s", dev->v4l2_dev.name);
    cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
    cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
    return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *f) {
    const struct ionvideo_fmt *fmt;

    if (f->index >= ARRAY_SIZE(formats))
        return -EINVAL;

    fmt = &formats[f->index];

    strlcpy(f->description, fmt->name, sizeof(f->description));
    f->pixelformat = fmt->fourcc;
    return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f) {
    struct ionvideo_dev *dev = video_drvdata(file);
    struct vb2_queue *q = &dev->vb_vidq;
    int ret = 0;
    unsigned long flags;

    if (freerun_mode == 0) {
        if (dev->c_width == 0 || dev->c_height == 0) {
            return -EINVAL;
        }
        f->fmt.pix.width = dev->c_width;
        f->fmt.pix.height = dev->c_height;
        spin_lock_irqsave(&q->done_lock, flags);
        ret = list_empty(&q->done_list);
        spin_unlock_irqrestore(&q->done_lock, flags);
        if (!ret) {
            return -EAGAIN;
        }
    } else {
        f->fmt.pix.width = dev->width;
        f->fmt.pix.height = dev->height;
    }
    f->fmt.pix.field = V4L2_FIELD_INTERLACED;
    f->fmt.pix.pixelformat = dev->fmt->fourcc;
    f->fmt.pix.bytesperline = (f->fmt.pix.width * dev->fmt->depth) >> 3;
    f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
    if (dev->fmt->is_yuv)
        f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
    else
        f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

    return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f) {
    struct ionvideo_dev *dev = video_drvdata(file);
    const struct ionvideo_fmt *fmt;

    fmt = get_format(f);
    if (!fmt) {
        dprintk(dev, 1, "Fourcc format (0x%08x) unknown.\n", f->fmt.pix.pixelformat);
        return -EINVAL;
    }

    f->fmt.pix.field = V4L2_FIELD_INTERLACED;
    v4l_bound_align_image(&f->fmt.pix.width, 48, MAX_WIDTH, 4, &f->fmt.pix.height, 32, MAX_HEIGHT, 0, 0);
    f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth) >> 3;
    f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
    if (fmt->is_yuv)
        f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
    else
        f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    f->fmt.pix.priv = 0;
    return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f) {
    struct ionvideo_dev *dev = video_drvdata(file);
    struct vb2_queue *q = &dev->vb_vidq;

    int ret = vidioc_try_fmt_vid_cap(file, priv, f);
    if (ret < 0)
        return ret;

    if (vb2_is_busy(q)) {
        dprintk(dev, 1, "%s device busy\n", __func__);
        return -EBUSY;
    }
    dev->fmt = get_format(f);
    dev->pixelsize = dev->fmt->depth;
    dev->width = f->fmt.pix.width;
    dev->height = f->fmt.pix.height;

    return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *fh, struct v4l2_frmsizeenum *fsize) {
    static const struct v4l2_frmsize_stepwise sizes = { 48, MAX_WIDTH, 4, 32, MAX_HEIGHT, 1 };
    int i;

    if (fsize->index)
        return -EINVAL;
    for (i = 0; i < ARRAY_SIZE(formats); i++)
        if (formats[i].fourcc == fsize->pixel_format)
            break;
    if (i == ARRAY_SIZE(formats))
        return -EINVAL;
    fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
    fsize->stepwise = sizes;
    return 0;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p) {
    struct ionvideo_dev *dev = video_drvdata(file);
    struct ppmgr2_device* ppmgr2_dev = &(dev->ppmgr2_dev);
    int ret = 0;

    ret = vb2_ioctl_qbuf(file, priv, p);
    if (ret != 0) { return ret; }

    if (!ppmgr2_dev->phy_addr[p->index]){
        struct vb2_buffer *vb;
        struct vb2_queue *q;
        void* phy_addr = NULL;
        q = dev->vdev.queue;
        vb = q->bufs[p->index];
        phy_addr = vb2_plane_cookie(vb, 0);
        if (phy_addr) {
            ret = ppmgr2_canvas_config(ppmgr2_dev, dev->width, dev->height, dev->fmt->fourcc, phy_addr, p->index);
        } else {
            return -ENOMEM;
        }
    }

    return ret;
}

static int vidioc_synchronization_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p) {
    struct vb2_buffer *vb = NULL;
    struct vb2_queue *q;
    struct ionvideo_dev *dev = video_drvdata(file);
    struct ionvideo_buffer *buf;
    int ret = 0;
    int d = 0;
    unsigned long flags;

    q = dev->vdev.queue;
    if (dev->receiver_register) {  	 
    	// clear the frame buffer queue  
    	while(!list_empty(&q->done_list)) {
		ret = vb2_ioctl_dqbuf(file, priv, p);
		if (ret) { return ret;}
		ret = vb2_ioctl_qbuf(file, priv, p);
		if (ret) { return ret;}
 	}
    	printk("init to clear the done list buffer.done\n");
    	dev->receiver_register = 0;
    	dev->is_video_started = 0;
	return -EAGAIN;
    } else{
    	spin_lock_irqsave(&q->done_lock, flags);
    	if (list_empty(&q->done_list)) {
       	spin_unlock_irqrestore(&q->done_lock, flags);
        	return -EAGAIN;
    	}
    	vb = list_first_entry(&q->done_list, struct vb2_buffer, done_entry);
    	spin_unlock_irqrestore(&q->done_lock, flags);

    	buf = container_of(vb, struct ionvideo_buffer, vb);
    	if(dev->is_video_started == 0){
		printk("Execute the VIDEO_START cmd. pts=%llx\n", buf->pts);
        	tsync_avevent_locked(VIDEO_START, buf->pts ? buf->pts : timestamp_vpts_get());        
        	d = 0;
        	dev->is_video_started=1;
    	}else{ 
	    	if (buf->pts  == 0) {
	       	buf->pts = timestamp_vpts_get() + DUR2PTS(buf->duration);
	    	}      

		if (abs(timestamp_pcrscr_get() - buf->pts ) > tsync_vpts_discontinuity_margin()) {
	        	tsync_avevent_locked(VIDEO_TSTAMP_DISCONTINUITY, buf->pts );
	    	} 
		else{
			timestamp_vpts_set(buf->pts);
		}
	    	d = (buf->pts - timestamp_pcrscr_get());
    	}
    }

    if (d > 450) {
        return -EAGAIN;
    } else if (d < -11520) {
        int s = 3;
        while (s--) {
            ret = vb2_ioctl_dqbuf(file, priv, p);
            if (ret) {  return ret; }

	     if (buf->pts  == 0) {
		buf->pts = timestamp_vpts_get() + DUR2PTS(buf->duration);
	     }      

	     if (abs(timestamp_pcrscr_get() - buf->pts ) > tsync_vpts_discontinuity_margin()) {
		tsync_avevent_locked(VIDEO_TSTAMP_DISCONTINUITY, buf->pts );
	     } 
	     else{
		timestamp_vpts_set(buf->pts);
	     }
            
            if(list_empty(&q->done_list)) {
                break;
            } else {
                ret = vb2_ioctl_qbuf(file, priv, p);
                if (ret) { return ret; }
		skip_frames++;
            }
        }
        dprintk(dev, 1, "s:%u\n", skip_frames);
    } else {	 
        ret = vb2_ioctl_dqbuf(file, priv, p);
        if (ret) {
            return ret;
        }
    }
    p->timestamp.tv_sec = 0;
    p->timestamp.tv_usec = timestamp_vpts_get();

    return 0;
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p){
    if (freerun_mode == 0) {
        return vidioc_synchronization_dqbuf(file, priv, p);
    }
    return vb2_ioctl_dqbuf(file, priv, p);
}

#define NUM_INPUTS 10
/* only one input in this sample driver */
static int vidioc_enum_input(struct file *file, void *priv, struct v4l2_input *inp) {
    if (inp->index >= NUM_INPUTS)
        return -EINVAL;

    inp->type = V4L2_INPUT_TYPE_CAMERA;
    sprintf(inp->name, "Camera %u", inp->index);
    return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i) {
    struct ionvideo_dev *dev = video_drvdata(file);

    *i = dev->input;
    return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i) {
    struct ionvideo_dev *dev = video_drvdata(file);

    if (i >= NUM_INPUTS)
        return -EINVAL;

    if (i == dev->input)
        return 0;

    dev->input = i;
    return 0;
}

/* ------------------------------------------------------------------
 File operations for the device
 ------------------------------------------------------------------*/
static const struct v4l2_file_operations ionvideo_fops = {
    .owner = THIS_MODULE,
    .open = vidioc_open,
    .release = vidioc_release,
    .read = vb2_fop_read,
    .poll = vb2_fop_poll,
    .unlocked_ioctl = video_ioctl2, /* V4L2 ioctl handler */
    .mmap = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops ionvideo_ioctl_ops = {
    .vidioc_querycap = vidioc_querycap,
    .vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap = vidioc_g_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap = vidioc_try_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap = vidioc_s_fmt_vid_cap,
    .vidioc_enum_framesizes = vidioc_enum_framesizes,
    .vidioc_reqbufs = vb2_ioctl_reqbufs,
    .vidioc_create_bufs = vb2_ioctl_create_bufs,
    .vidioc_prepare_buf = vb2_ioctl_prepare_buf,
    .vidioc_querybuf = vb2_ioctl_querybuf,
    .vidioc_qbuf = vidioc_qbuf,
    .vidioc_dqbuf = vidioc_dqbuf,
    .vidioc_enum_input = vidioc_enum_input,
    .vidioc_g_input = vidioc_g_input,
    .vidioc_s_input = vidioc_s_input,
    .vidioc_streamon = vb2_ioctl_streamon,
    .vidioc_streamoff = vb2_ioctl_streamoff,
    .vidioc_log_status = v4l2_ctrl_log_status,
    .vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
    .vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct video_device ionvideo_template = {
    .name = "ionvideo",
    .fops = &ionvideo_fops,
    .ioctl_ops = &ionvideo_ioctl_ops,
    .release = video_device_release_empty,
};

/* -----------------------------------------------------------------
 Initialization and module stuff
 ------------------------------------------------------------------*/
//struct vb2_dc_conf * ionvideo_dma_ctx = NULL;
static int ionvideo_release(void) {
    struct ionvideo_dev *dev;
    struct list_head *list;

    while (!list_empty(&ionvideo_devlist)) {
        list = ionvideo_devlist.next;
        list_del(list);
        dev = list_entry(list, struct ionvideo_dev, ionvideo_devlist);

        v4l2_info(&dev->v4l2_dev, "unregistering %s\n", video_device_node_name(&dev->vdev));
        video_unregister_device(&dev->vdev);
        v4l2_device_unregister(&dev->v4l2_dev);
        kfree(dev);
    }
    //vb2_dma_contig_cleanup_ctx(ionvideo_dma_ctx);

    return 0;
}

static int video_receiver_event_fun(int type, void* data, void* private_data) {

    struct ionvideo_dev *dev = (struct ionvideo_dev *)private_data;

    if (type == VFRAME_EVENT_PROVIDER_UNREG) {
        dev->receiver_register = 0;
        tsync_avevent(VIDEO_STOP, 0);
        printk("unreg:ionvideo\n");
    }else if (type == VFRAME_EVENT_PROVIDER_REG) {
        dev->receiver_register = 1;
        dev->ppmgr2_dev.interlaced_num = 0;
        printk("reg:ionvideo\n");
    }else if (type == VFRAME_EVENT_PROVIDER_QUREY_STATE) {
        return RECEIVER_ACTIVE ;
    }
    return 0;
}

static const struct vframe_receiver_op_s video_vf_receiver = { .event_cb = video_receiver_event_fun };

static int __init ionvideo_create_instance(int inst)
{
    struct ionvideo_dev *dev;
    struct video_device *vfd;
    struct vb2_queue *q;
    int ret;

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
    return -ENOMEM;

    snprintf(dev->v4l2_dev.name, sizeof(dev->v4l2_dev.name),
            "%s-%03d", IONVIDEO_MODULE_NAME, inst);
    ret = v4l2_device_register(NULL, &dev->v4l2_dev);
    if (ret)
    goto free_dev;

    dev->fmt = &formats[0];
    dev->width = 640;
    dev->height = 480;
    dev->pixelsize = dev->fmt->depth;
    dev->fd_num = 0;

    /* initialize locks */
    spin_lock_init(&dev->slock);

    /* initialize queue */
    q = &dev->vb_vidq;
    q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ;
    q->drv_priv = dev;
    q->buf_struct_size = sizeof(struct ionvideo_buffer);
    q->ops = &ionvideo_video_qops;
    q->mem_ops = &vb2_ion_memops;
    q->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

    ret = vb2_queue_init(q);
    if (ret)
    goto unreg_dev;

    mutex_init(&dev->mutex);

    /* init video dma queues */
    INIT_LIST_HEAD(&dev->vidq.active);
    init_waitqueue_head(&dev->vidq.wq);

    vfd = &dev->vdev;
    *vfd = ionvideo_template;
    vfd->debug = debug;
    vfd->v4l2_dev = &dev->v4l2_dev;
    vfd->queue = q;
    set_bit(V4L2_FL_USE_FH_PRIO, &vfd->flags);

    /*
     * Provide a mutex to v4l2 core. It will be used to protect
     * all fops and v4l2 ioctls.
     */
    vfd->lock = &dev->mutex;
    video_set_drvdata(vfd, dev);

    ret = video_register_device(vfd, VFL_TYPE_GRABBER, video_nr);
    if (ret < 0)
        goto unreg_dev;

    /* Now that everything is fine, let's add it to device list */
    list_add_tail(&dev->ionvideo_devlist, &ionvideo_devlist);
    vf_receiver_init(&dev->video_vf_receiver, RECEIVER_NAME, &video_vf_receiver, dev);
    vf_reg_receiver(&dev->video_vf_receiver);
    v4l2_info(&dev->v4l2_dev, "V4L2 device registered as %s\n",
            video_device_node_name(vfd));
    return 0;

unreg_dev:
    v4l2_device_unregister(&dev->v4l2_dev);
free_dev:
    kfree(dev);
    return ret;
}

static ssize_t vframe_states_show(struct class *class, struct class_attribute* attr, char* buf)
{
    int ret = 0;
    vframe_states_t states;
//    unsigned long flags;
	
    if (ionvideo_vf_get_states(&states) == 0) {
        ret += sprintf(buf + ret, "vframe_pool_size=%d\n", states.vf_pool_size);
        ret += sprintf(buf + ret, "vframe buf_free_num=%d\n", states.buf_free_num);
        ret += sprintf(buf + ret, "vframe buf_recycle_num=%d\n", states.buf_recycle_num);
        ret += sprintf(buf + ret, "vframe buf_avail_num=%d\n", states.buf_avail_num);
    } else {
        ret += sprintf(buf + ret, "vframe no states\n");
    }

    return ret;
}

static struct class_attribute ion_video_class_attrs[] = {
	__ATTR_RO(vframe_states),
    __ATTR_NULL
};
static struct class ionvideo_class = {
        .name = "ionvideo",
        .class_attrs = ion_video_class_attrs,
};

/* This routine allocates from 1 to n_devs virtual drivers.

 The real maximum number of virtual drivers will depend on how many drivers
 will succeed. This is limited to the maximum number of devices that
 videodev supports, which is equal to VIDEO_NUM_DEVICES.
 */
static int __init ionvideo_init(void)
{
    int ret = 0, i;
    ret = class_register(&ionvideo_class);
    if(ret<0)
        return ret;
    if (n_devs <= 0)
    n_devs = 1;

    for (i = 0; i < n_devs; i++) {
        ret = ionvideo_create_instance(i);
        if (ret) {
            /* If some instantiations succeeded, keep driver */
            if (i)
            ret = 0;
            break;
        }
    }

    if (ret < 0) {
        printk(KERN_ERR "ionvideo: error %d while loading driver\n", ret);
        return ret;
    }

    printk(KERN_INFO "Video Technology Magazine Ion Video "
            "Capture Board ver %s successfully loaded.\n",
            IONVIDEO_VERSION);

    /* n_devs will reflect the actual number of allocated devices */
    n_devs = i;
    
    return ret;
}

static void __exit ionvideo_exit(void)
{
    ionvideo_release();
	class_unregister(&ionvideo_class);
}

MODULE_DESCRIPTION("Video Technology Magazine Ion Video Capture Board");
MODULE_AUTHOR("Amlogic, Shuai Cao<shuai.cao@amlogic.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(IONVIDEO_VERSION);

module_init (ionvideo_init);
module_exit (ionvideo_exit);
