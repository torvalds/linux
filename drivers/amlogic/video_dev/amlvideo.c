/*
 * pass a frame of amlogic video codec device  to user in style of v4l2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD Licence, GNU General Public License
 * as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/wait.h>
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
#include <media/videobuf-res.h>
#include <media/videobuf2-core.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <linux/types.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <linux/amlogic/ge2d/ge2d.h>
#include <linux/amlogic/amports/timestamp.h>
#include <linux/kernel.h>
#include <linux/amlogic/amports/tsync.h>
#include <linux/amlogic/amports/vfp.h>

#define AVMLVIDEO_MODULE_NAME "amlvideo"

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001

#define AMLVIDEO_MAJOR_VERSION 0
#define AMLVIDEO_MINOR_VERSION 7
#define AMLVIDEO_RELEASE 1
#define AMLVIDEO_VERSION \
	KERNEL_VERSION(AMLVIDEO_MAJOR_VERSION, AMLVIDEO_MINOR_VERSION, AMLVIDEO_RELEASE)
#define MAGIC_RE_MEM 0x123039dc

#define RECEIVER_NAME "amlvideo"
#define PROVIDER_NAME "amlvideo"

#define AMLVIDEO_POOL_SIZE 16
static vfq_t q_ready;
extern bool omx_secret_mode;
static u8 first_frame;
static u32 vpts_last;


#define DUR2PTS(x) ((x) - ((x) >> 4))
#define DUR2PTS_RM(x) ((x) & 0xf)

MODULE_DESCRIPTION("pass a frame of amlogic video codec device  to user in style of v4l2");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL");
//static u32 vpts_remainder;
static unsigned video_nr = 10;
//module_param(video_nr, uint, 0644);
//MODULE_PARM_DESC(video_nr, "videoX start number, 10 is defaut");

static unsigned n_devs = 1;

static unsigned debug = 0;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
module_param(vid_limit, uint, 0644);
MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static unsigned int freerun_mode = 0;
module_param(freerun_mode, uint, 0664);
MODULE_PARM_DESC(freerun_mode, "av synchronization int kernel");

vframe_t* ppmgrvf = NULL;
static int video_receiver_event_fun(int type, void* data, void*);

/* supported controls */
static struct v4l2_queryctrl vivi_qctrl[] = { };

#define dprintk(dev, level, fmt, arg...) \
	v4l2_dbg(level, debug, &dev->v4l2_dev, fmt, ## arg)

/* ------------------------------------------------------------------
 Basic structures
 ------------------------------------------------------------------*/

struct vivi_fmt {
    char *name;
    u32 fourcc; /* v4l2 format id */
    int depth;
};

static struct vivi_fmt formats[] = {

    {
                .name = "RGB888 (24)",
                .fourcc = V4L2_PIX_FMT_RGB24, /* 24  RGB-8-8-8 */
                .depth = 24,
        },
    {
                .name = "RGBA8888 (32)",
                .fourcc = V4L2_PIX_FMT_RGB32, /* 24  RGBA-8-8-8-8 */
                .depth = 32,
    },
    {           .name = "12  Y/CbCr 4:2:0",
                .fourcc = V4L2_PIX_FMT_NV12,
                .depth = 12,
    },
    {           .name = "21  Y/CbCr 4:2:0",
                .fourcc = V4L2_PIX_FMT_NV21,
                .depth = 12,
    },
    {
                .name = "RGB565 (BE)",
                .fourcc = V4L2_PIX_FMT_RGB565X, /* rrrrrggg gggbbbbb */
                .depth = 16,
    },
#if 0
        {
            .name = "BGRA8888 (32)",
            .fourcc = V4L2_PIX_FMT_BGR32, /* 24  RGBA-8-8-8-8 */
            .depth = 32,
        },
        {
            .name = "BGR888 (24)",
            .fourcc = V4L2_PIX_FMT_BGR24, /* 24  BGR-8-8-8 */
            .depth = 24,
        },
        {
            .name = "YUV420P",
            .fourcc = V4L2_PIX_FMT_YUV420,
            .depth = 12,
        },
#endif
    };

struct vframe_s *amlvideo_pool_ready[AMLVIDEO_POOL_SIZE+1];
/* ------------------------------------------------------------------
 *           provider operations
 *-----------------------------------------------------------------*/
static vframe_t *amlvideo_vf_peek(void *op_arg) {
    return vfq_peek(&q_ready);
}

static vframe_t *amlvideo_vf_get(void *op_arg) {
    return vfq_pop(&q_ready);
}

static void amlvideo_vf_put(vframe_t *vf, void *op_arg) {
    vf_put(vf, RECEIVER_NAME);
}

static int amlvideo_event_cb(int type, void *data, void *private_data)
{
    //printk("ionvideo_event_cb_type=%d\n",type);
    if (type & VFRAME_EVENT_RECEIVER_PUT) {
        //printk("video put, avail=%d\n", vfq_level(&q_ready) );
    }else if (type & VFRAME_EVENT_RECEIVER_GET) {
        //    printk("video get, avail=%d\n", vfq_level(&q_ready) );
    }else if(type & VFRAME_EVENT_RECEIVER_FRAME_WAIT){
        // up(&thread_sem);
        //   printk("receiver is waiting\n");
    }else if(type & VFRAME_EVENT_RECEIVER_FRAME_WAIT){
        //     printk("frame wait\n");
    }
    return 0;
}

static int amlvideo_vf_states(vframe_states_t *states, void *op_arg) {
    //unsigned long flags;
    //spin_lock_irqsave(&lock, flags);
    states->vf_pool_size    = AMLVIDEO_POOL_SIZE;
    states->buf_recycle_num = 0;
    states->buf_free_num    =  AMLVIDEO_POOL_SIZE - vfq_level(&q_ready);
    states->buf_avail_num   = vfq_level(&q_ready);
    //spin_unlock_irqrestore(&lock, flags);
    return 0;
}

static const struct vframe_operations_s amlvideo_vf_provider = {
    .peek      = amlvideo_vf_peek,
    .get       = amlvideo_vf_get,
    .put       = amlvideo_vf_put,
    .event_cb  = amlvideo_event_cb,
    .vf_states = amlvideo_vf_states,
};

static struct vframe_provider_s amlvideo_vf_prov;


static struct vframe_receiver_s video_vf_recv;

static struct vivi_fmt *get_format(struct v4l2_format *f) {
    struct vivi_fmt *fmt;
    unsigned int k;

    for (k = 0; k < ARRAY_SIZE(formats); k++) {
        fmt = &formats[k];
        if (fmt->fourcc == f->fmt.pix.pixelformat)
            break;
    }

    if (k == ARRAY_SIZE(formats))
        return NULL ;

    return &formats[k];
}

struct sg_to_addr {
    int pos;
    struct scatterlist *sg;
};

/* buffer for one video frame */
struct vivi_buffer {
    /* common v4l buffer stuff -- must be first */
    struct videobuf_buffer vb;

    struct vivi_fmt *fmt;
};

struct vivi_dmaqueue {
    struct list_head active;

    /* thread for generating video stream*/
    struct task_struct *kthread;
    wait_queue_head_t wq;
};

static LIST_HEAD(vivi_devlist);

struct vivi_dev {
    struct list_head vivi_devlist;
    struct v4l2_device v4l2_dev;

    spinlock_t slock;
    struct mutex mutex;
    int users;

    /* various device info */
    struct video_device *vfd;

    struct vivi_dmaqueue vidq;

    /* Control 'registers' */
    int qctl_regs[ARRAY_SIZE(vivi_qctrl)];
    struct videobuf_res_privdata* res;
};

struct vivi_fh {
    struct vivi_dev *dev;

    /* video capture */
    struct vivi_fmt *fmt;
    unsigned int width, height;
    struct videobuf_queue vb_vidq;
    unsigned int is_streamed_on;

    enum v4l2_buf_type type;
};

static int index;
static int unregFlag;
static int startFlag;
unsigned eventparam[4];
struct mutex vfpMutex;
static int video_receiver_event_fun(int type, void* data, void* private_data) {
    if (type == VFRAME_EVENT_PROVIDER_UNREG) {
        unregFlag = 1;
        if (index != 8)
            mutex_lock(&vfpMutex);
        printk("AML:VFRAME_EVENT_PROVIDER_UNREG\n");
        if (vf_get_receiver(PROVIDER_NAME)) {
            printk("unreg:amlvideo\n");
            vf_unreg_provider(&amlvideo_vf_prov);
            omx_secret_mode = false;
        }
        first_frame = 0;
    }
    if (type == VFRAME_EVENT_PROVIDER_REG) {
        printk("AML:VFRAME_EVENT_PROVIDER_REG\n");

        if (unregFlag == 0)
            vf_notify_provider(RECEIVER_NAME, VFRAME_EVENT_RECEIVER_PARAM_SET, (void*) eventparam);
        ppmgrvf = NULL;
        first_frame = 0;
        mutex_unlock(&vfpMutex);
    }else if (type == VFRAME_EVENT_PROVIDER_QUREY_STATE) {
        return RECEIVER_ACTIVE ;
    }else if (type == VFRAME_EVENT_PROVIDER_START) {
        printk("AML:VFRAME_EVENT_PROVIDER_START\n");
        if (vf_get_receiver(PROVIDER_NAME)) {
            struct vframe_receiver_s * aaa = vf_get_receiver(PROVIDER_NAME);
            printk("aaa->name=%s",aaa->name);
            omx_secret_mode = true;
            vf_provider_init(&amlvideo_vf_prov, PROVIDER_NAME ,&amlvideo_vf_provider, NULL);
            vf_reg_provider(&amlvideo_vf_prov);
            vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_START,NULL);
            vfq_init(&q_ready, AMLVIDEO_POOL_SIZE+1, &amlvideo_pool_ready[0]);
        }
    }
    return 0;
}

static const struct vframe_receiver_op_s video_vf_receiver = { .event_cb = video_receiver_event_fun };

/* ------------------------------------------------------------------
 Videobuf operations
 ------------------------------------------------------------------*/
static int buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size) {
    struct videobuf_res_privdata* res = (struct videobuf_res_privdata*) vq->priv_data;
    struct vivi_fh *fh = (struct vivi_fh *) res->priv;
    struct vivi_dev *dev = fh->dev;
    *size = (fh->width * fh->height * fh->fmt->depth) >> 3;
    if (0 == *count)
        *count = 32;

    while (*size * *count > vid_limit * 1024 * 1024)
        (*count)--;

    dprintk(dev, 1, "%s, count=%d, size=%d\n", __func__, *count, *size);

    return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct vivi_buffer *buf) {
    struct videobuf_res_privdata* res = (struct videobuf_res_privdata*) vq->priv_data;
    struct vivi_fh *fh = (struct vivi_fh *) res->priv;
    struct vivi_dev *dev = fh->dev;

    dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);
    videobuf_waiton(vq, &buf->vb, 0, 0);
    if (in_interrupt())
        BUG();
    videobuf_res_free(vq, &buf->vb);
    dprintk(dev, 1, "free_buffer: freed\n");
    buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define NORM_MAXW 2000
#define NORM_MAXH 1600
static int buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb, enum v4l2_field field) {
    struct videobuf_res_privdata* res = (struct videobuf_res_privdata*) vq->priv_data;
    struct vivi_fh *fh = (struct vivi_fh *) res->priv;
    struct vivi_dev *dev = fh->dev;
    struct vivi_buffer
    *buf = container_of(vb, struct vivi_buffer, vb);
    int rc;
    dprintk(dev, 1, "%s, field=%d\n", __func__, field);

    BUG_ON(NULL == fh->fmt);

    if (fh->width < 48 || fh->width > NORM_MAXW || fh->height < 32 || fh->height > NORM_MAXH)
        return -EINVAL;

    buf->vb.size = (fh->width * fh->height * fh->fmt->depth) >> 3;
    if (0 != buf->vb.baddr && buf->vb.bsize < buf->vb.size)
        return -EINVAL;
    /* These properties only change when queue is idle, see s_fmt */
    buf->fmt = fh->fmt;
    buf->vb.width = fh->width;
    buf->vb.height = fh->height;
    buf->vb.field = field;
    if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
        rc = videobuf_iolock(vq, &buf->vb, NULL );
        if (rc < 0)
            goto fail;
    }
    buf->vb.state = VIDEOBUF_PREPARED;
    return 0;

    fail: free_buffer(vq, buf);
    return rc;
}

static void buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb) {
    struct vivi_buffer * buf = container_of(vb, struct vivi_buffer, vb);
    struct videobuf_res_privdata* res = (struct videobuf_res_privdata*) vq->priv_data;
    struct vivi_fh *fh = (struct vivi_fh *) res->priv;
    struct vivi_dev *dev = fh->dev;
    struct vivi_dmaqueue *vidq = &dev->vidq;
    dprintk(dev, 1, "%s\n", __func__);
    buf->vb.state = VIDEOBUF_QUEUED;
    list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq, struct videobuf_buffer *vb) {
    struct vivi_buffer
    *buf = container_of(vb, struct vivi_buffer, vb);
    free_buffer(vq, buf);
}

static struct videobuf_queue_ops vivi_video_qops = { .buf_setup = buffer_setup, .buf_prepare = buffer_prepare, .buf_queue = buffer_queue, .buf_release = buffer_release, };

/* ------------------------------------------------------------------
 IOCTL vidioc handling
 ------------------------------------------------------------------*/
static int vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap) {
    struct vivi_fh *fh = priv;
    struct vivi_dev *dev = fh->dev;

    strcpy(cap->driver, "amlvideo");
    strcpy(cap->card, "amlvideo");
    strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
    cap->version = AMLVIDEO_VERSION;
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
    return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *f) {
    struct vivi_fmt *fmt;

    if (f->index >= ARRAY_SIZE(formats))
        return -EINVAL;

    fmt = &formats[f->index];

    strlcpy(f->description, fmt->name, sizeof(f->description));
    f->pixelformat = fmt->fourcc;
    return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f) {
    struct vivi_fh *fh = priv;
    f->fmt.pix.width = fh->width;
    f->fmt.pix.height = fh->height;
    f->fmt.pix.field = fh->vb_vidq.field;
    f->fmt.pix.pixelformat = fh->fmt->fourcc;
    f->fmt.pix.bytesperline = (f->fmt.pix.width * fh->fmt->depth) >> 3;
    f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;

    return (0);
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f) {
    struct vivi_fh *fh = priv;
    int ret = 0;

    fh->fmt = get_format(f);
    fh->width = f->fmt.pix.width;
    fh->height = f->fmt.pix.height;
    fh->vb_vidq.field = f->fmt.pix.field;
    fh->type = f->type;

    eventparam[0] = f->fmt.pix.width;
    eventparam[1] = f->fmt.pix.height;
    if (fh->fmt->fourcc == V4L2_PIX_FMT_RGB24)
        eventparam[2] = (GE2D_FORMAT_S24_BGR | GE2D_LITTLE_ENDIAN);
    else if (fh->fmt->fourcc == V4L2_PIX_FMT_RGB32)
        eventparam[2] = (GE2D_FORMAT_S32_ABGR | GE2D_LITTLE_ENDIAN);
    else if (fh->fmt->fourcc == V4L2_PIX_FMT_RGB565X)
        eventparam[2] = (GE2D_FORMAT_S16_RGB_565 | GE2D_LITTLE_ENDIAN);
    else
        eventparam[2] = (GE2D_FORMAT_M24_NV21 | GE2D_LITTLE_ENDIAN);
    vf_notify_provider(RECEIVER_NAME, VFRAME_EVENT_RECEIVER_PARAM_SET, (void*) eventparam);
    return ret;
}

/*FIXME: This seems to be generic enough to be at videodev2 */
static int vidioc_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f) {
    struct vivi_fh *fh = priv;
    int ret = 0;

    fh->fmt = get_format(f);
    fh->width = f->fmt.pix.width;
    fh->height = f->fmt.pix.height;
    fh->vb_vidq.field = f->fmt.pix.field;
    fh->type = f->type;
    eventparam[0] = f->fmt.pix.width;
    eventparam[1] = f->fmt.pix.height;
    if (fh->fmt->fourcc == V4L2_PIX_FMT_RGB24)
        eventparam[2] = (GE2D_FORMAT_S24_BGR | GE2D_LITTLE_ENDIAN);
    else if (fh->fmt->fourcc == V4L2_PIX_FMT_RGB32)
        eventparam[2] = (GE2D_FORMAT_S32_ABGR | GE2D_LITTLE_ENDIAN);
    else if (fh->fmt->fourcc == V4L2_PIX_FMT_RGB565X)
        eventparam[2] = (GE2D_FORMAT_S16_RGB_565 | GE2D_LITTLE_ENDIAN);
    else if (fh->fmt->fourcc == V4L2_PIX_FMT_NV12)
        eventparam[2] = (GE2D_FORMAT_M24_NV12 | GE2D_LITTLE_ENDIAN);
    else if (fh->fmt->fourcc == V4L2_PIX_FMT_NV21)
        eventparam[2] = (GE2D_FORMAT_M24_NV21 | GE2D_LITTLE_ENDIAN);
    vf_notify_provider(RECEIVER_NAME, VFRAME_EVENT_RECEIVER_PARAM_SET, (void*) eventparam);
    return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv, struct v4l2_requestbuffers *p) {
    struct vivi_fh *fh = priv;

    return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p) {
    struct vivi_fh *fh = priv;

    return (videobuf_querybuf(&fh->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p) {
    int ret = 0;
    if (omx_secret_mode == true) {
        return ret;
    }
    if (ppmgrvf) {
        vf_put(ppmgrvf, RECEIVER_NAME);
        vf_notify_provider(RECEIVER_NAME, VFRAME_EVENT_RECEIVER_PUT, NULL );
    }
    mutex_unlock(&vfpMutex);
    return ret;
}

static u32 current_pts = 0;
static int freerun_start = 0;
static int freerun_cleancache_dqbuf(struct v4l2_buffer *p) {
    int ret = 0;

    if (!vf_peek(RECEIVER_NAME)) {
        msleep(10);
        return -EAGAIN;
    }
    mutex_lock(&vfpMutex);
    ppmgrvf = vf_get(RECEIVER_NAME);
    if (ppmgrvf == NULL) {
        mutex_unlock(&vfpMutex);
        return -EAGAIN;
    }
    if (startFlag) {
        current_pts = 0;
        while (vf_peek(RECEIVER_NAME)) {
            vf_put(ppmgrvf, RECEIVER_NAME);
            vf_notify_provider(RECEIVER_NAME, VFRAME_EVENT_RECEIVER_PUT, NULL );
            ppmgrvf = vf_get(RECEIVER_NAME);
            if (!ppmgrvf) {
                mutex_unlock(&vfpMutex);
                return -EAGAIN;
            }
        }
        freerun_start = 1;
        startFlag = 0;
    }
    if (freerun_start) {
        current_pts += DUR2PTS(ppmgrvf->duration);
    }
    p->index = (ppmgrvf->canvas0Addr&0xff) - PPMGR_CANVAS_INDEX;
    p->timestamp.tv_sec = current_pts / 90000;
    p->timestamp.tv_usec = (current_pts % 90000) * 100 / 9;
    return ret;
}

static int freerun_dqbuf(struct v4l2_buffer *p) {
    int ret = 0;
    if (omx_secret_mode == true) {
        if (vfq_level(&q_ready)>AMLVIDEO_POOL_SIZE-1) {
            msleep(10);
            return -EAGAIN;
        }
    }
    if (!vf_peek(RECEIVER_NAME)) {
        msleep(10);
        return -EAGAIN;
    }
    if (omx_secret_mode != true) {
        mutex_lock(&vfpMutex);
    }
    ppmgrvf = vf_get(RECEIVER_NAME);
    if (!ppmgrvf) {
        mutex_unlock(&vfpMutex);
        return -EAGAIN;
    }
    if (omx_secret_mode == true) {
        vfq_push(&q_ready, ppmgrvf);
        p->index = 0;
        p->timestamp.tv_sec = 0;

        if (ppmgrvf->pts) {
            first_frame = 1;
            p->timestamp.tv_usec = ppmgrvf->pts;
        } else if (first_frame == 0){
            first_frame = 1;
            p->timestamp.tv_usec = 0;
        } else {
            p->timestamp.tv_usec = vpts_last + (DUR2PTS(ppmgrvf->duration));
        }
        vpts_last = p->timestamp.tv_usec;
        //printk("p->timestamp.tv_usec=%d\n",p->timestamp.tv_usec);
        return ret;
    }   
    if (ppmgrvf->pts != 0) {
        timestamp_vpts_set(ppmgrvf->pts);
    } else{
        timestamp_vpts_inc(DUR2PTS(ppmgrvf->duration));
		ppmgrvf->pts=timestamp_vpts_get();
    }

	if(!ppmgrvf->pts)
        ppmgrvf->pts_us64=ppmgrvf->pts*100/9;
	
    if (unregFlag || startFlag) {
        if (ppmgrvf->pts == 0)
            timestamp_vpts_set(timestamp_pcrscr_get());
        startFlag = 0;
        unregFlag = 0;
    }
    if (!ppmgrvf) {
        mutex_unlock(&vfpMutex);
        return -EAGAIN;
    }
    p->index = (ppmgrvf->canvas0Addr&0xff) - PPMGR_CANVAS_INDEX;
    p->timestamp.tv_sec = 0;
    p->timestamp.tv_usec = ppmgrvf->pts_us64;
    return ret;
}

static int normal_dqbuf(struct v4l2_buffer *p) {
    static int last_pcrscr = 0;
    int ret = 0;
    int diff = 0;
    int a = 0;
    int b = timestamp_pcrscr_get();
    if ((!vf_peek(RECEIVER_NAME)) || (last_pcrscr == b)) {
        msleep(10);
        return -EAGAIN;
    }
    last_pcrscr = b;
    mutex_lock(&vfpMutex);
    ppmgrvf = vf_get(RECEIVER_NAME);
    if (!ppmgrvf) {
        mutex_unlock(&vfpMutex);
        return -EAGAIN;
    }
    if (ppmgrvf->pts != 0) {
        if (abs(timestamp_pcrscr_get() - ppmgrvf->pts) > tsync_vpts_discontinuity_margin()) {
            tsync_avevent_locked(VIDEO_TSTAMP_DISCONTINUITY, ppmgrvf->pts);
        } else {
            timestamp_vpts_set(ppmgrvf->pts);
        }
    } else
        timestamp_vpts_inc(DUR2PTS(ppmgrvf->duration));
    a = timestamp_vpts_get();
    diff = a - b;
    if (unregFlag || startFlag) {
        if (ppmgrvf->pts == 0)
            timestamp_vpts_set(timestamp_pcrscr_get());
        tsync_avevent_locked(VIDEO_START, timestamp_vpts_get());
        startFlag = 0;
        unregFlag = 0;
        diff = 0;
    } else if (!freerun_mode) {
        if (diff > 3600 && diff < 450000) {
            msleep(diff / 90);
        } else if (diff < -11520) {
            int count = (-diff) >> 13;
            while (count--) {
                if (!vf_peek(RECEIVER_NAME)) {
                    break;
                } else {
                    if (ppmgrvf) {
                        vf_put(ppmgrvf, RECEIVER_NAME);
                        vf_notify_provider(RECEIVER_NAME, VFRAME_EVENT_RECEIVER_PUT, NULL );
                    }
                    ppmgrvf = vf_get(RECEIVER_NAME);
                    if (ppmgrvf->pts != 0) {
                        if (abs(timestamp_pcrscr_get() - ppmgrvf->pts) > tsync_vpts_discontinuity_margin()) {
                            tsync_avevent_locked(VIDEO_TSTAMP_DISCONTINUITY, ppmgrvf->pts);
                        } else {
                            timestamp_vpts_set(ppmgrvf->pts);
                        }
                    } else {
                        timestamp_vpts_inc(DUR2PTS(ppmgrvf->duration));
                    }
                }
            }
        }
    }
    if (!ppmgrvf) {
        mutex_unlock(&vfpMutex);
        return -EAGAIN;
    }
    p->index = (ppmgrvf->canvas0Addr&0xff) - PPMGR_CANVAS_INDEX;
    p->timestamp.tv_sec = 0;
    p->timestamp.tv_usec = ppmgrvf->duration;

    return ret;
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p) {
    int ret = 0;

    if (freerun_mode == 1) {
        ret = freerun_dqbuf(p);
    }else if (freerun_mode == 2) {
        ret = freerun_cleancache_dqbuf(p);
    } else {
        ret = normal_dqbuf(p);
    }
    return ret;
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
    struct vivi_fh *fh = priv;

    return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i) {
    struct vivi_fh *fh = priv;
    int ret;
    if ((fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) || (i != fh->type))
        return -EINVAL;
    ret = videobuf_streamon(&fh->vb_vidq);
    if (ret == 0)
        fh->is_streamed_on = 1;
    return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i) {
    struct vivi_fh *fh = priv;
    int ret;
    if ((fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) || (i != fh->type))
        return -EINVAL;
    ret = videobuf_streamoff(&fh->vb_vidq);
    if (ret == 0)
        fh->is_streamed_on = 0;
    return ret;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id i) {
    return 0;
}

/* --- controls ---------------------------------------------- */
static int vidioc_queryctrl(struct file *file, void *priv, struct v4l2_queryctrl *qc) {
    int i;

    for (i = 0; i < ARRAY_SIZE(vivi_qctrl); i++)
        if (qc->id && qc->id == vivi_qctrl[i].id) {
            memcpy(qc, &(vivi_qctrl[i]), sizeof(*qc));
            return (0);
        }

    return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv, struct v4l2_control *ctrl) {
    struct vivi_fh *fh = priv;
    struct vivi_dev *dev = fh->dev;
    int i;

    for (i = 0; i < ARRAY_SIZE(vivi_qctrl); i++)
        if (ctrl->id == vivi_qctrl[i].id) {
            ctrl->value = dev->qctl_regs[i];
            return 0;
        }

    return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv, struct v4l2_control *ctrl) {
    struct vivi_fh *fh = priv;

    struct vivi_dev *dev = fh->dev;
    int i;

    for (i = 0; i < ARRAY_SIZE(vivi_qctrl); i++)
        if (ctrl->id == vivi_qctrl[i].id) {
            if (ctrl->value < vivi_qctrl[i].minimum || ctrl->value > vivi_qctrl[i].maximum) {
                return -ERANGE;
            }
            dev->qctl_regs[i] = ctrl->value;
            return 0;
        }
    return -EINVAL;
}

/* ------------------------------------------------------------------
 File operations for the device
 ------------------------------------------------------------------*/
extern void get_ppmgr_buf_info(char** start, unsigned int* size);
static int amlvideo_open(struct file *file) {
    struct vivi_dev *dev = video_drvdata(file);
    struct vivi_fh *fh = NULL;
    int retval = 0;
    struct videobuf_res_privdata* res = NULL;
    char *bstart = NULL;
    unsigned int bsize = 0;
    ppmgrvf = NULL;
    index = 0;
    unregFlag = 0;
    startFlag = 1;
    freerun_start = 0;
    mutex_unlock(&vfpMutex);
    mutex_lock(&dev->mutex);
    dev->users++;
    if (dev->users > 1) {
        dev->users--;
        mutex_unlock(&dev->mutex);
        return -EBUSY;
    }
    res = kzalloc(sizeof(*res), GFP_KERNEL);
    if ((NULL == res) || (dev->res != NULL )) {
        dev->users--;
        mutex_unlock(&dev->mutex);
        return -ENOMEM;
    } else {
        fh = kzalloc(sizeof(*fh), GFP_KERNEL);
        if (NULL == fh) {
            kfree(res);
            dev->users--;
            mutex_unlock(&dev->mutex);
            retval = -ENOMEM;
        }
    }
    mutex_unlock(&dev->mutex);

    file->private_data = fh;
    fh->dev = dev;

    fh->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fh->fmt = &formats[0];
    fh->width = 1920;
    fh->height = 1080;

    res->priv = (void*) fh;
    dev->res = res;
    get_ppmgr_buf_info(&bstart, &bsize);
    res->start = (resource_size_t) bstart;
    res->end = (resource_size_t)(bstart + bsize - 1);
    res->magic = MAGIC_RE_MEM;
    videobuf_queue_res_init(&fh->vb_vidq, &vivi_video_qops, NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED, sizeof(struct vivi_buffer), (void*) res, NULL );
    printk("amlvideo open");
    return 0;
}

static ssize_t amlvideo_read(struct file *file, char __user *data, size_t count, loff_t *ppos) {
    struct vivi_fh *fh = file->private_data;

    if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
        return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0, file->f_flags & O_NONBLOCK);
    }
    return 0;
}

static unsigned int amlvideo_poll(struct file *file, struct poll_table_struct *wait) {
    struct vivi_fh *fh = file->private_data;
    struct vivi_dev *dev = fh->dev;
    struct videobuf_queue *q = &fh->vb_vidq;

    dprintk(dev, 1, "%s\n", __func__);

    if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
        return POLLERR;

    return videobuf_poll_stream(file, q, wait);
}

static int amlvideo_close(struct file *file) {
    struct vivi_fh *fh = file->private_data;
    struct vivi_dev *dev = fh->dev;
    videobuf_stop(&fh->vb_vidq);
    videobuf_mmap_free(&fh->vb_vidq);
    kfree(fh);
    index = 8;
    startFlag = 0;
    mutex_unlock(&vfpMutex);
    if (dev->res) {
        kfree(dev->res);
        dev->res = NULL;
    }
    mutex_lock(&dev->mutex);
    dev->users--;
    mutex_unlock(&dev->mutex);
    printk("amlvideo close");
    return 0;
}

static int amlvideo_mmap(struct file *file, struct vm_area_struct *vma) {
    struct vivi_fh *fh = file->private_data;

    struct vivi_dev *dev = fh->dev;
    int ret;

    dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

    dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n", (unsigned long)vma->vm_start, (unsigned long)vma->vm_end-(unsigned long)vma->vm_start, ret);

    return ret;
}

static const struct v4l2_file_operations amlvideo_fops = { .owner = THIS_MODULE, .open = amlvideo_open, .release = amlvideo_close, .read = amlvideo_read, .poll = amlvideo_poll, .ioctl = video_ioctl2, /* V4L2 ioctl handler */
.mmap = amlvideo_mmap, };

static const struct v4l2_ioctl_ops amlvideo_ioctl_ops = {
        .vidioc_querycap = vidioc_querycap,
        .vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
        .vidioc_g_fmt_vid_cap = vidioc_g_fmt_vid_cap,
        .vidioc_try_fmt_vid_cap = vidioc_try_fmt_vid_cap,
        .vidioc_s_fmt_vid_cap = vidioc_s_fmt_vid_cap,
        .vidioc_reqbufs = vidioc_reqbufs,
        .vidioc_querybuf = vidioc_querybuf,
        .vidioc_qbuf = vidioc_qbuf,
        .vidioc_dqbuf = vidioc_dqbuf,
        .vidioc_s_std = vidioc_s_std,
        .vidioc_queryctrl = vidioc_queryctrl,
        .vidioc_g_ctrl = vidioc_g_ctrl,
        .vidioc_s_ctrl = vidioc_s_ctrl,
        .vidioc_streamon = vidioc_streamon,
        .vidioc_streamoff = vidioc_streamoff,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
        .vidiocgmbuf = vidiocgmbuf,
#endif
    };

static struct video_device amlvideo_template = { .name = "amlvideo", .fops = &amlvideo_fops, .ioctl_ops = &amlvideo_ioctl_ops, .release = video_device_release,

.tvnorms = V4L2_STD_525_60, .current_norm = V4L2_STD_NTSC_M , };

/* -----------------------------------------------------------------
 Initialization and module stuff
 ------------------------------------------------------------------*/

static int amlvideo_release(void) {
    struct vivi_dev *dev;
    struct list_head *list;

    while (!list_empty(&vivi_devlist)) {
        list = vivi_devlist.next;
        list_del(list);
        dev = list_entry(list, struct vivi_dev, vivi_devlist);

        v4l2_info(&dev->v4l2_dev, "unregistering %s\n",
                video_device_node_name(dev->vfd));
        video_unregister_device(dev->vfd);
        v4l2_device_unregister(&dev->v4l2_dev);
        kfree(dev);
    }

    return 0;
}

static int __init amlvideo_create_instance(int inst) {
    struct vivi_dev *dev;
    struct video_device *vfd;
    int ret, i;
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    snprintf(dev->v4l2_dev.name, sizeof(dev->v4l2_dev.name), "%s-%03d", AVMLVIDEO_MODULE_NAME, inst);
    ret = v4l2_device_register(NULL, &dev->v4l2_dev);
    if (ret)
        goto free_dev;
    /* init video dma queues */
    INIT_LIST_HEAD(&dev->vidq.active);
    init_waitqueue_head(&dev->vidq.wq);

    /* initialize locks */
    spin_lock_init(&dev->slock);
    mutex_init(&dev->mutex);

    ret = -ENOMEM;
    vfd = video_device_alloc();
    if (!vfd)
        goto unreg_dev;

    *vfd = amlvideo_template;
    vfd->debug = debug;
    ret = video_register_device(vfd, VFL_TYPE_GRABBER, video_nr);
    if (ret < 0)
        goto rel_vdev;

    video_set_drvdata(vfd, dev);

    /* Set all controls to their default value. */
    for (i = 0; i < ARRAY_SIZE(vivi_qctrl); i++)
        dev->qctl_regs[i] = vivi_qctrl[i].default_value;

    /* Now that everything is fine, let's add it to device list */
    list_add_tail(&dev->vivi_devlist, &vivi_devlist);

    if (video_nr != -1)
        video_nr++;

    dev->vfd = vfd;
    v4l2_info(&dev->v4l2_dev, "V4L2 device registered as %s\n", video_device_node_name(vfd));
    return 0;

    rel_vdev: video_device_release(vfd);
    unreg_dev: v4l2_device_unregister(&dev->v4l2_dev);
    free_dev: kfree(dev);
    dev->res = NULL;
    return ret;
}

#undef NORM_MAXW
#undef NORM_MAXH
//#define __init
/* This routine allocates from 1 to n_devs virtual drivers.

 The real maximum number of virtual drivers will depend on how many drivers
 will succeed. This is limited to the maximum number of devices that
 videodev supports, which is equal to VIDEO_NUM_DEVICES.
 */
static int __init amlvideo_init(void) {
    int ret = 0, i;

    if (n_devs <= 0)
        n_devs = 1;
    for (i = 0; i < n_devs; i++) {
        ret = amlvideo_create_instance(i);
        if (ret) {
            /* If some instantiations succeeded, keep driver */
            if (i)
                ret = 0;
            break;
        }
    }

    if (ret < 0) {
        //printk(KERN_INFO "Error %d while loading vivi driver\n", ret);
        return ret;
    }

    //printk(KERN_INFO "Video Technology Magazine Virtual Video "
    //		"Capture Board ver %u.%u.%u successfully loaded.\n",
    //		(AMLVIDEO_VERSION >> 16) & 0xFF, (AMLVIDEO_VERSION >> 8) & 0xFF,
    //		AMLVIDEO_VERSION & 0xFF);

    /* n_devs will reflect the actual number of allocated devices */
    n_devs = i;
    mutex_init(&vfpMutex);
    vf_receiver_init(&video_vf_recv, RECEIVER_NAME, &video_vf_receiver, NULL );
    vf_reg_receiver(&video_vf_recv);
    return ret;
}

static void __exit amlvideo_exit(void) {
    vf_unreg_receiver(&video_vf_recv);
    amlvideo_release();
}

module_init(amlvideo_init);
module_exit(amlvideo_exit);
