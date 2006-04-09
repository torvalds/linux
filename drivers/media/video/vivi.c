/*
 * Virtual Video driver - This code emulates a real video device with v4l2 api
 *
 * Copyright (c) 2006 by:
 *      Mauro Carvalho Chehab <mchehab--a.t--infradead.org>
 *      Ted Walther <ted--a.t--enumera.com>
 *      John Sokol <sokol--a.t--videotechnology.com>
 *      http://v4l.videotechnology.com/
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
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#ifdef CONFIG_VIDEO_V4L1_COMPAT
/* Include V4L1 specific functions. Should be removed soon */
#include <linux/videodev.h>
#endif
#include <linux/interrupt.h>
#include <media/video-buf.h>
#include <media/v4l2-common.h>
#include <linux/kthread.h>
#include <linux/highmem.h>

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

/* These timers are for 1 fps - used only for testing */
//#define WAKE_DENOMINATOR 30 /* hack for testing purposes */
//#define BUFFER_TIMEOUT     msecs_to_jiffies(5000)  /* 5 seconds */

#include "font.h"

#ifndef kzalloc
#define kzalloc(size, flags)                            \
({                                                      \
	void *__ret = kmalloc(size, flags);             \
	if (__ret)                                      \
		memset(__ret, 0, size);                 \
	__ret;                                          \
})
#endif

MODULE_DESCRIPTION("Video Technology Magazine Virtual Video Capture Board");
MODULE_AUTHOR("Mauro Carvalho Chehab, Ted Walther and John Sokol");
MODULE_LICENSE("Dual BSD/GPL");

#define VIVI_MAJOR_VERSION 0
#define VIVI_MINOR_VERSION 4
#define VIVI_RELEASE 0
#define VIVI_VERSION KERNEL_VERSION(VIVI_MAJOR_VERSION, VIVI_MINOR_VERSION, VIVI_RELEASE)

static int video_nr = -1;        /* /dev/videoN, -1 for autodetect */
module_param(video_nr, int, 0);

static int debug = 0;
module_param(debug, int, 0);

static unsigned int vid_limit = 16;
module_param(vid_limit,int,0644);
MODULE_PARM_DESC(vid_limit,"capture memory limit in megabytes");

/* supported controls */
static struct v4l2_queryctrl vivi_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_VOLUME,
		.name          = "Volume",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 65535,
		.flags         = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_BRIGHTNESS,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Brightness",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 127,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_CONTRAST,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Contrast",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 0x1,
		.default_value = 0x10,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_SATURATION,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Saturation",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 0x1,
		.default_value = 127,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_HUE,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Hue",
		.minimum       = -128,
		.maximum       = 127,
		.step          = 0x1,
		.default_value = 0,
		.flags         = 0,
	}
};

static int qctl_regs[ARRAY_SIZE(vivi_qctrl)];

#define dprintk(level,fmt, arg...)			     \
	do { 						     \
		if (debug >= (level))			     \
			printk(KERN_DEBUG "vivi: " fmt , ## arg);    \
	} while (0)

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct vivi_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

static struct vivi_fmt format = {
	.name     = "4:2:2, packed, YUYV",
	.fourcc   = V4L2_PIX_FMT_YUYV,
	.depth    = 16,
};

struct sg_to_addr {
	int pos;
	struct scatterlist *sg;
};

/* buffer for one video frame */
struct vivi_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct vivi_fmt        *fmt;

	struct sg_to_addr      *to_addr;
};

struct vivi_dmaqueue {
	struct list_head       active;
	struct list_head       queued;
	struct timer_list      timeout;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

static LIST_HEAD(vivi_devlist);

struct vivi_dev {
	struct list_head           vivi_devlist;

	struct semaphore           lock;

	int                        users;

	/* various device info */
	unsigned int               resources;
	struct video_device        video_dev;

	struct vivi_dmaqueue       vidq;

	/* Several counters */
	int                        h,m,s,us,jiffies;
	char                       timestr[13];
};

struct vivi_fh {
	struct vivi_dev            *dev;

	/* video capture */
	struct vivi_fmt            *fmt;
	unsigned int               width,height;
	struct videobuf_queue      vb_vidq;

	enum v4l2_buf_type         type;
};

/* ------------------------------------------------------------------
	DMA and thread functions
   ------------------------------------------------------------------*/

/* Bars and Colors should match positions */

enum colors {
	WHITE,
	AMBAR,
	CYAN,
	GREEN,
	MAGENTA,
	RED,
	BLUE
};

static u8 bars[8][3] = {
	/* R   G   B */
	{204,204,204},	/* white */
	{208,208,  0},  /* ambar */
	{  0,206,206},  /* cyan */
	{  0,239,  0},  /* green */
	{239,  0,239},  /* magenta */
	{205,  0,  0},  /* red */
	{  0,  0,255},  /* blue */
	{  0,  0,  0}
};

#define TO_Y(r,g,b) (((16829*r +33039*g +6416*b  + 32768)>>16)+16)
/* RGB to  V(Cr) Color transform */
#define TO_V(r,g,b) (((28784*r -24103*g -4681*b  + 32768)>>16)+128)
/* RGB to  U(Cb) Color transform */
#define TO_U(r,g,b) (((-9714*r -19070*g +28784*b + 32768)>>16)+128)

#define TSTAMP_MIN_Y 24
#define TSTAMP_MAX_Y TSTAMP_MIN_Y+15
#define TSTAMP_MIN_X 64

void prep_to_addr(struct sg_to_addr to_addr[],struct videobuf_buffer *vb)
{
	int i, pos=0;

	for (i=0;i<vb->dma.nr_pages;i++) {
		to_addr[i].sg=&vb->dma.sglist[i];
		to_addr[i].pos=pos;
		pos += vb->dma.sglist[i].length;
	}
}

inline int get_addr_pos(int pos, int pages, struct sg_to_addr to_addr[])
{
	int p1=0,p2=pages-1,p3=pages/2;

	/* Sanity test */
	BUG_ON (pos>=to_addr[p2].pos+to_addr[p2].sg->length);

	while (p1+1<p2) {
		if (pos < to_addr[p3].pos) {
			p2=p3;
		} else {
			p1=p3;
		}
		p3=(p1+p2)/2;
	}
	if (pos >= to_addr[p2].pos)
		p1=p2;

	return (p1);
}

void gen_line(struct sg_to_addr to_addr[],int inipos,int pages,int wmax,
					int hmax, int line, char *timestr)
{
	int  w,i,j,pos=inipos,pgpos,oldpg,y;
	char *p,*s,*basep;
	struct page *pg;
	u8   chr,r,g,b,color;

	/* Get first addr pointed to pixel position */
	oldpg=get_addr_pos(pos,pages,to_addr);
	pg=pfn_to_page(to_addr[oldpg].sg->dma_address >> PAGE_SHIFT);
	basep = kmap_atomic(pg, KM_BOUNCE_READ)+to_addr[oldpg].sg->offset;

	/* We will just duplicate the second pixel at the packet */
	wmax/=2;

	/* Generate a standard color bar pattern */
	for (w=0;w<wmax;w++) {
		r=bars[w*7/wmax][0];
		g=bars[w*7/wmax][1];
		b=bars[w*7/wmax][2];

		for (color=0;color<4;color++) {
			pgpos=get_addr_pos(pos,pages,to_addr);
			if (pgpos!=oldpg) {
				pg=pfn_to_page(to_addr[pgpos].sg->dma_address >> PAGE_SHIFT);
				kunmap_atomic(basep, KM_BOUNCE_READ);
				basep= kmap_atomic(pg, KM_BOUNCE_READ)+to_addr[pgpos].sg->offset;
				oldpg=pgpos;
			}
			p=basep+pos-to_addr[pgpos].pos;

			switch (color) {
				case 0:
				case 2:
					*p=TO_Y(r,g,b);		/* Luminance */
					break;
				case 1:
					*p=TO_U(r,g,b);		/* Cb */
					break;
				case 3:
					*p=TO_V(r,g,b);		/* Cr */
					break;
			}
			pos++;
		}
	}

	/* Checks if it is possible to show timestamp */
	if (TSTAMP_MAX_Y>=hmax)
		goto end;
	if (TSTAMP_MIN_X+strlen(timestr)>=wmax)
		goto end;

	/* Print stream time */
	if (line>=TSTAMP_MIN_Y && line<=TSTAMP_MAX_Y) {
		j=TSTAMP_MIN_X;
		for (s=timestr;*s;s++) {
			chr=rom8x16_bits[(*s-0x30)*16+line-TSTAMP_MIN_Y];
			for (i=0;i<7;i++) {
				if (chr&1<<(7-i)) { /* Font color*/
					r=bars[BLUE][0];
					g=bars[BLUE][1];
					b=bars[BLUE][2];
					r=g=b=0;
					g=198;
				} else { /* Background color */
					r=bars[WHITE][0];
					g=bars[WHITE][1];
					b=bars[WHITE][2];
					r=g=b=0;
				}

				pos=inipos+j*2;
				for (color=0;color<4;color++) {
					pgpos=get_addr_pos(pos,pages,to_addr);
					if (pgpos!=oldpg) {
						pg=pfn_to_page(to_addr[pgpos].
								sg->dma_address
								>> PAGE_SHIFT);
						kunmap_atomic(basep,
								KM_BOUNCE_READ);
						basep= kmap_atomic(pg,
							KM_BOUNCE_READ)+
							to_addr[pgpos].sg->offset;
						oldpg=pgpos;
					}
					p=basep+pos-to_addr[pgpos].pos;

					y=TO_Y(r,g,b);

					switch (color) {
						case 0:
						case 2:
							*p=TO_Y(r,g,b);		/* Luminance */
							break;
						case 1:
							*p=TO_U(r,g,b);		/* Cb */
							break;
						case 3:
							*p=TO_V(r,g,b);		/* Cr */
							break;
					}
					pos++;
				}
				j++;
			}
		}
	}


end:
	kunmap_atomic(basep, KM_BOUNCE_READ);
}
static void vivi_fillbuff(struct vivi_dev *dev,struct vivi_buffer *buf)
{
	int h,pos=0;
	int hmax  = buf->vb.height;
	int wmax  = buf->vb.width;
	struct videobuf_buffer *vb=&buf->vb;
	struct sg_to_addr *to_addr=buf->to_addr;
	struct timeval ts;

	/* Test if DMA mapping is ready */
	if (!vb->dma.sglist[0].dma_address)
		return;

	prep_to_addr(to_addr,vb);

	/* Check if there is enough memory */
	BUG_ON(buf->vb.dma.nr_pages << PAGE_SHIFT < (buf->vb.width*buf->vb.height)*2);

	for (h=0;h<hmax;h++) {
		gen_line(to_addr,pos,vb->dma.nr_pages,wmax,hmax,h,dev->timestr);
		pos += wmax*2;
	}

	/* Updates stream time */

	dev->us+=jiffies_to_usecs(jiffies-dev->jiffies);
	dev->jiffies=jiffies;
	if (dev->us>=1000000) {
		dev->us-=1000000;
		dev->s++;
		if (dev->s>=60) {
			dev->s-=60;
			dev->m++;
			if (dev->m>60) {
				dev->m-=60;
				dev->h++;
				if (dev->h>24)
					dev->h-=24;
			}
		}
	}
	sprintf(dev->timestr,"%02d:%02d:%02d:%03d",
			dev->h,dev->m,dev->s,(dev->us+500)/1000);

	dprintk(2,"vivifill at %s: Buffer 0x%08lx size= %d\n",dev->timestr,
			(unsigned long)buf->vb.dma.vmalloc,pos);

	/* Advice that buffer was filled */
	buf->vb.state = STATE_DONE;
	buf->vb.field_count++;
	do_gettimeofday(&ts);
	buf->vb.ts = ts;

	list_del(&buf->vb.queue);
	wake_up(&buf->vb.done);
}

static int restart_video_queue(struct vivi_dmaqueue *dma_q);

static void vivi_thread_tick(struct vivi_dmaqueue  *dma_q)
{
	struct vivi_buffer    *buf;
	struct vivi_dev *dev= container_of(dma_q,struct vivi_dev,vidq);

	int bc;

	/* Announces videobuf that all went ok */
	for (bc = 0;; bc++) {
		if (list_empty(&dma_q->active)) {
			dprintk(1,"No active queue to serve\n");
			break;
		}

		buf = list_entry(dma_q->active.next,
				 struct vivi_buffer, vb.queue);

		/* Nobody is waiting something to be done, just return */
		if (!waitqueue_active(&buf->vb.done)) {
			mod_timer(&dma_q->timeout, jiffies+BUFFER_TIMEOUT);
			return;
		}

		do_gettimeofday(&buf->vb.ts);
		dprintk(2,"[%p/%d] wakeup\n",buf,buf->vb.i);

		/* Fill buffer */
		vivi_fillbuff(dev,buf);
	}
	if (list_empty(&dma_q->active)) {
		del_timer(&dma_q->timeout);
	} else {
		mod_timer(&dma_q->timeout, jiffies+BUFFER_TIMEOUT);
	}
	if (bc != 1)
		dprintk(1,"%s: %d buffers handled (should be 1)\n",__FUNCTION__,bc);
}

void vivi_sleep(struct vivi_dmaqueue  *dma_q)
{
	int timeout;
	DECLARE_WAITQUEUE(wait, current);

	dprintk(1,"%s dma_q=0x%08lx\n",__FUNCTION__,(unsigned long)dma_q);

	add_wait_queue(&dma_q->wq, &wait);
	if (!kthread_should_stop()) {
		dma_q->frame++;

		/* Calculate time to wake up */
		timeout=dma_q->ini_jiffies+msecs_to_jiffies((dma_q->frame*WAKE_NUMERATOR*1000)/WAKE_DENOMINATOR)-jiffies;

		if (timeout <= 0) {
			int old=dma_q->frame;
			dma_q->frame=(jiffies_to_msecs(jiffies-dma_q->ini_jiffies)*WAKE_DENOMINATOR)/(WAKE_NUMERATOR*1000)+1;

			timeout=dma_q->ini_jiffies+msecs_to_jiffies((dma_q->frame*WAKE_NUMERATOR*1000)/WAKE_DENOMINATOR)-jiffies;

			dprintk(1,"underrun, losed %d frames. "
				  "Now, frame is %d. Waking on %d jiffies\n",
					dma_q->frame-old,dma_q->frame,timeout);
		} else
			dprintk(1,"will sleep for %i jiffies\n",timeout);

		vivi_thread_tick(dma_q);

		schedule_timeout_interruptible (timeout);
	}

	remove_wait_queue(&dma_q->wq, &wait);
	try_to_freeze();
}

int vivi_thread(void *data)
{
	struct vivi_dmaqueue  *dma_q=data;

	dprintk(1,"thread started\n");

	for (;;) {
		vivi_sleep(dma_q);

		if (kthread_should_stop())
			break;
	}
	dprintk(1, "thread: exit\n");
	return 0;
}

int vivi_start_thread(struct vivi_dmaqueue  *dma_q)
{
	dma_q->frame=0;
	dma_q->ini_jiffies=jiffies;

	dprintk(1,"%s\n",__FUNCTION__);
	init_waitqueue_head(&dma_q->wq);

	dma_q->kthread = kthread_run(vivi_thread, dma_q, "vivi");

	if (dma_q->kthread == NULL) {
		printk(KERN_ERR "vivi: kernel_thread() failed\n");
		return -EINVAL;
	}
	dprintk(1,"returning from %s\n",__FUNCTION__);
	return 0;
}

void vivi_stop_thread(struct vivi_dmaqueue  *dma_q)
{
	dprintk(1,"%s\n",__FUNCTION__);
	/* shutdown control thread */
	if (dma_q->kthread) {
		kthread_stop(dma_q->kthread);
		dma_q->kthread=NULL;
	}
}

static int restart_video_queue(struct vivi_dmaqueue *dma_q)
{
	struct vivi_buffer *buf, *prev;
	struct list_head *item;

	dprintk(1,"%s dma_q=0x%08lx\n",__FUNCTION__,(unsigned long)dma_q);

	if (!list_empty(&dma_q->active)) {
		buf = list_entry(dma_q->active.next, struct vivi_buffer, vb.queue);
		dprintk(2,"restart_queue [%p/%d]: restart dma\n",
			buf, buf->vb.i);

		dprintk(1,"Restarting video dma\n");
		vivi_stop_thread(dma_q);
//		vivi_start_thread(dma_q);

		/* cancel all outstanding capture / vbi requests */
		list_for_each(item,&dma_q->active) {
			buf = list_entry(item, struct vivi_buffer, vb.queue);

			list_del(&buf->vb.queue);
			buf->vb.state = STATE_ERROR;
			wake_up(&buf->vb.done);
		}
		mod_timer(&dma_q->timeout, jiffies+BUFFER_TIMEOUT);

		return 0;
	}

	prev = NULL;
	for (;;) {
		if (list_empty(&dma_q->queued))
			return 0;
		buf = list_entry(dma_q->queued.next, struct vivi_buffer, vb.queue);
		if (NULL == prev) {
			list_del(&buf->vb.queue);
			list_add_tail(&buf->vb.queue,&dma_q->active);

			dprintk(1,"Restarting video dma\n");
			vivi_stop_thread(dma_q);
			vivi_start_thread(dma_q);

			buf->vb.state = STATE_ACTIVE;
			mod_timer(&dma_q->timeout, jiffies+BUFFER_TIMEOUT);
			dprintk(2,"[%p/%d] restart_queue - first active\n",
				buf,buf->vb.i);

		} else if (prev->vb.width  == buf->vb.width  &&
			   prev->vb.height == buf->vb.height &&
			   prev->fmt       == buf->fmt) {
			list_del(&buf->vb.queue);
			list_add_tail(&buf->vb.queue,&dma_q->active);
			buf->vb.state = STATE_ACTIVE;
			dprintk(2,"[%p/%d] restart_queue - move to active\n",
				buf,buf->vb.i);
		} else {
			return 0;
		}
		prev = buf;
	}
}

static void vivi_vid_timeout(unsigned long data)
{
	struct vivi_dev      *dev  = (struct vivi_dev*)data;
	struct vivi_dmaqueue *vidq = &dev->vidq;
	struct vivi_buffer   *buf;

	while (!list_empty(&vidq->active)) {
		buf = list_entry(vidq->active.next, struct vivi_buffer, vb.queue);
		list_del(&buf->vb.queue);
		buf->vb.state = STATE_ERROR;
		wake_up(&buf->vb.done);
		printk("vivi/0: [%p/%d] timeout\n", buf, buf->vb.i);
	}

	restart_video_queue(vidq);
}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct vivi_fh *fh = vq->priv_data;

	*size = fh->width*fh->height*2;

	if (0 == *count)
		*count = 32;
	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;
	return 0;
}

void
free_buffer(struct videobuf_queue *vq, struct vivi_buffer *buf)
{
	dprintk(1,"%s\n",__FUNCTION__);

	if (in_interrupt())
		BUG();

	/*FIXME: Maybe a spinlock is required here */
	kfree(buf->to_addr);
	buf->to_addr=NULL;

	videobuf_waiton(&buf->vb,0,0);
	videobuf_dma_unmap(vq, &buf->vb.dma);
	videobuf_dma_free(&buf->vb.dma);
	buf->vb.state = STATE_NEEDS_INIT;
}

#define norm_maxw() 1024
#define norm_maxh() 768
static int
buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct vivi_fh     *fh  = vq->priv_data;
	struct vivi_buffer *buf = container_of(vb,struct vivi_buffer,vb);
	int rc, init_buffer = 0;

//	dprintk(1,"%s, field=%d\n",__FUNCTION__,field);

	BUG_ON(NULL == fh->fmt);
	if (fh->width  < 48 || fh->width  > norm_maxw() ||
	    fh->height < 32 || fh->height > norm_maxh())
		return -EINVAL;
	buf->vb.size = fh->width*fh->height*2;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	if (buf->fmt       != fh->fmt    ||
	    buf->vb.width  != fh->width  ||
	    buf->vb.height != fh->height ||
	buf->vb.field  != field) {
		buf->fmt       = fh->fmt;
		buf->vb.width  = fh->width;
		buf->vb.height = fh->height;
		buf->vb.field  = field;
		init_buffer = 1;
	}

	if (STATE_NEEDS_INIT == buf->vb.state) {
		if (0 != (rc = videobuf_iolock(vq,&buf->vb,NULL)))
			goto fail;
	}

	buf->vb.state = STATE_PREPARED;

	if (NULL == (buf->to_addr = kmalloc(sizeof(*buf->to_addr) * vb->dma.nr_pages,GFP_KERNEL))) {
		rc=-ENOMEM;
		goto fail;
	}

	return 0;

fail:
	free_buffer(vq,buf);
	return rc;
}

static void
buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct vivi_buffer    *buf     = container_of(vb,struct vivi_buffer,vb);
	struct vivi_fh        *fh      = vq->priv_data;
	struct vivi_dev       *dev     = fh->dev;
	struct vivi_dmaqueue  *vidq    = &dev->vidq;
	struct vivi_buffer    *prev;

	if (!list_empty(&vidq->queued)) {
		dprintk(1,"adding vb queue=0x%08lx\n",(unsigned long)&buf->vb.queue);
		list_add_tail(&buf->vb.queue,&vidq->queued);
		buf->vb.state = STATE_QUEUED;
		dprintk(2,"[%p/%d] buffer_queue - append to queued\n",
			buf, buf->vb.i);
	} else if (list_empty(&vidq->active)) {
		list_add_tail(&buf->vb.queue,&vidq->active);

		buf->vb.state = STATE_ACTIVE;
		mod_timer(&vidq->timeout, jiffies+BUFFER_TIMEOUT);
		dprintk(2,"[%p/%d] buffer_queue - first active\n",
			buf, buf->vb.i);

		vivi_start_thread(vidq);
	} else {
		prev = list_entry(vidq->active.prev, struct vivi_buffer, vb.queue);
		if (prev->vb.width  == buf->vb.width  &&
		    prev->vb.height == buf->vb.height &&
		    prev->fmt       == buf->fmt) {
			list_add_tail(&buf->vb.queue,&vidq->active);
			buf->vb.state = STATE_ACTIVE;
			dprintk(2,"[%p/%d] buffer_queue - append to active\n",
				buf, buf->vb.i);

		} else {
			list_add_tail(&buf->vb.queue,&vidq->queued);
			buf->vb.state = STATE_QUEUED;
			dprintk(2,"[%p/%d] buffer_queue - first queued\n",
				buf, buf->vb.i);
		}
	}
}

static void buffer_release(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct vivi_buffer   *buf  = container_of(vb,struct vivi_buffer,vb);
	struct vivi_fh       *fh   = vq->priv_data;
	struct vivi_dev      *dev  = (struct vivi_dev*)fh->dev;
	struct vivi_dmaqueue *vidq = &dev->vidq;

	dprintk(1,"%s\n",__FUNCTION__);

	vivi_stop_thread(vidq);

	free_buffer(vq,buf);
}

int vivi_map_sg (void *dev, struct scatterlist *sg, int nents,
	   int direction)
{
	int i;

	dprintk(1,"%s, number of pages=%d\n",__FUNCTION__,nents);
	BUG_ON(direction == DMA_NONE);

	for (i = 0; i < nents; i++ ) {
		BUG_ON(!sg[i].page);

		sg[i].dma_address = page_to_phys(sg[i].page) + sg[i].offset;
	}

	return nents;
}

int vivi_unmap_sg(void *dev,struct scatterlist *sglist,int nr_pages,
					int direction)
{
	dprintk(1,"%s\n",__FUNCTION__);
	return 0;
}

int vivi_dma_sync_sg(void *dev,struct scatterlist *sglist,int nr_pages,
					int direction)
{
//	dprintk(1,"%s\n",__FUNCTION__);

//	flush_write_buffers();
	return 0;
}

static struct videobuf_queue_ops vivi_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,

	/* Non-pci handling routines */
	.vb_map_sg      = vivi_map_sg,
	.vb_dma_sync_sg = vivi_dma_sync_sg,
	.vb_unmap_sg    = vivi_unmap_sg,
};

/* ------------------------------------------------------------------
	IOCTL handling
   ------------------------------------------------------------------*/

static int vivi_try_fmt(struct vivi_dev *dev, struct vivi_fh *fh,
			struct v4l2_format *f)
{
	struct vivi_fmt *fmt;
	enum v4l2_field field;
	unsigned int maxw, maxh;

	if (format.fourcc != f->fmt.pix.pixelformat) {
		dprintk(1,"Fourcc format invalid.\n");
		return -EINVAL;
	}
	fmt=&format;

	field = f->fmt.pix.field;

	if (field == V4L2_FIELD_ANY) {
//		field=V4L2_FIELD_INTERLACED;
		field=V4L2_FIELD_SEQ_TB;
	} else if (V4L2_FIELD_INTERLACED != field) {
		dprintk(1,"Field type invalid.\n");
		return -EINVAL;
	}

	maxw  = norm_maxw();
	maxh  = norm_maxh();

	f->fmt.pix.field = field;
	if (f->fmt.pix.height < 32)
		f->fmt.pix.height = 32;
	if (f->fmt.pix.height > maxh)
		f->fmt.pix.height = maxh;
	if (f->fmt.pix.width < 48)
		f->fmt.pix.width = 48;
	if (f->fmt.pix.width > maxw)
		f->fmt.pix.width = maxw;
	f->fmt.pix.width &= ~0x03;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

static int res_get(struct vivi_dev *dev, struct vivi_fh *fh)
{
	/* is it free? */
	down(&dev->lock);
	if (dev->resources) {
		/* no, someone else uses it */
		up(&dev->lock);
		return 0;
	}
	/* it's free, grab it */
	dev->resources =1;
	dprintk(1,"res: get\n");
	up(&dev->lock);
	return 1;
}

static inline int res_locked(struct vivi_dev *dev)
{
	return (dev->resources);
}

static void res_free(struct vivi_dev *dev, struct vivi_fh *fh)
{
	down(&dev->lock);
	dev->resources = 0;
	dprintk(1,"res: put\n");
	up(&dev->lock);
}

static int vivi_do_ioctl(struct inode *inode, struct file *file, unsigned int cmd, void *arg)
{
	struct vivi_fh  *fh     = file->private_data;
	struct vivi_dev *dev    = fh->dev;
	int ret=0;

	if (debug) {
		if (_IOC_DIR(cmd) & _IOC_WRITE)
			v4l_printk_ioctl_arg("vivi(w)",cmd, arg);
		else if (!_IOC_DIR(cmd) & _IOC_READ) {
			v4l_print_ioctl("vivi", cmd);
		}
	}

	switch(cmd) {
	/* --- capabilities ------------------------------------------ */
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = (struct v4l2_capability*)arg;

		memset(cap, 0, sizeof(*cap));

		strcpy(cap->driver, "vivi");
		strcpy(cap->card, "vivi");
		cap->version = VIVI_VERSION;
		cap->capabilities =
					V4L2_CAP_VIDEO_CAPTURE |
					V4L2_CAP_STREAMING     |
					V4L2_CAP_READWRITE;
		break;
	}
	/* --- capture ioctls ---------------------------------------- */
	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *f = arg;
		enum v4l2_buf_type type;
		unsigned int index;

		index = f->index;
		type  = f->type;

		if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			ret=-EINVAL;
			break;
		}

		switch (type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
			if (index > 0){
				ret=-EINVAL;
				break;
			}
			memset(f,0,sizeof(*f));

			f->index = index;
			f->type  = type;
			strlcpy(f->description,format.name,sizeof(f->description));
			f->pixelformat = format.fourcc;
			break;
		default:
			ret=-EINVAL;
		}
		break;
	}
	case VIDIOC_G_FMT:
	{
		struct v4l2_format *f = (struct v4l2_format *)arg;

		if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			ret=-EINVAL;
			break;
		}

		memset(&f->fmt.pix,0,sizeof(f->fmt.pix));
		f->fmt.pix.width        = fh->width;
		f->fmt.pix.height       = fh->height;
		f->fmt.pix.field        = fh->vb_vidq.field;
		f->fmt.pix.pixelformat  = fh->fmt->fourcc;
		f->fmt.pix.bytesperline =
			(f->fmt.pix.width * fh->fmt->depth) >> 3;
		f->fmt.pix.sizeimage =
			f->fmt.pix.height * f->fmt.pix.bytesperline;
		break;
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = arg;

		if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			dprintk(1,"Only capture supported.\n");
			ret=-EINVAL;
			break;
		}

		ret = vivi_try_fmt(dev,fh,f);
		if (ret < 0)
			break;

		fh->fmt           = &format;
		fh->width         = f->fmt.pix.width;
		fh->height        = f->fmt.pix.height;
		fh->vb_vidq.field = f->fmt.pix.field;
		fh->type          = f->type;

		break;
	}
	case VIDIOC_TRY_FMT:
	{
		struct v4l2_format *f = arg;
		if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			ret=-EINVAL;
			break;
		}

		ret=vivi_try_fmt(dev,fh,f);
		break;
	}
	case VIDIOC_REQBUFS:
		if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			ret=-EINVAL;
			break;
		}
		ret=videobuf_reqbufs(&fh->vb_vidq, arg);
		break;
	case VIDIOC_QUERYBUF:
		if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			ret=-EINVAL;
			break;
		}
		ret=videobuf_querybuf(&fh->vb_vidq, arg);
		break;
	case VIDIOC_QBUF:
		if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			ret=-EINVAL;
			break;
		}
		ret=videobuf_qbuf(&fh->vb_vidq, arg);
		break;
	case VIDIOC_DQBUF:
		if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			ret=-EINVAL;
			break;
		}
		ret=videobuf_dqbuf(&fh->vb_vidq, arg,
					file->f_flags & O_NONBLOCK);
		break;
#ifdef HAVE_V4L1
	/* --- streaming capture ------------------------------------- */
	case VIDIOCGMBUF:
	{
		struct video_mbuf *mbuf = arg;
		struct videobuf_queue *q=&fh->vb_vidq;
		struct v4l2_requestbuffers req;
		unsigned int i;

		memset(&req,0,sizeof(req));
		req.type   = q->type;
		req.count  = 8;
		req.memory = V4L2_MEMORY_MMAP;
		ret = videobuf_reqbufs(q,&req);
		if (ret < 0)
			break;
		memset(mbuf,0,sizeof(*mbuf));
		mbuf->frames = req.count;
		mbuf->size   = 0;
		for (i = 0; i < mbuf->frames; i++) {
			mbuf->offsets[i]  = q->bufs[i]->boff;
			mbuf->size       += q->bufs[i]->bsize;
		}
		break;
	}
#endif
	case VIDIOC_STREAMON:
	{
		if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		if (!res_get(dev,fh))
			return -EBUSY;
		ret=videobuf_streamon(&fh->vb_vidq);
		break;
	}
	case VIDIOC_STREAMOFF:
	{
		if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			ret=-EINVAL;
			break;
		}
		ret = videobuf_streamoff(&fh->vb_vidq);
		if (ret < 0)
			break;
		res_free(dev,fh);
		break;
	}
	/* ---------- tv norms ---------- */
	case VIDIOC_ENUMSTD:
	{
		struct v4l2_standard *e = arg;

		if (e->index>0) {
			ret=-EINVAL;
			break;
		}
		ret = v4l2_video_std_construct(e, V4L2_STD_NTSC_M, "NTSC-M");

		/* Allows vivi to use different fps from video std */
		e->frameperiod.numerator = WAKE_NUMERATOR;
		e->frameperiod.denominator = WAKE_DENOMINATOR;

		break;
	}
	case VIDIOC_G_STD:
	{
		v4l2_std_id *id = arg;

		*id = V4L2_STD_NTSC_M;
		break;
	}
	case VIDIOC_S_STD:
	{
		break;
	}
	/* ------ input switching ---------- */
	case VIDIOC_ENUMINPUT:
	{ /* only one input in this sample driver */
		struct v4l2_input *inp = arg;

		if (inp->index != 0) {
			ret=-EINVAL;
			break;
		}
		memset(inp, 0, sizeof(*inp));

		inp->index = 0;
		inp->type = V4L2_INPUT_TYPE_CAMERA;
		inp->std = V4L2_STD_NTSC_M;
		strcpy(inp->name,"Camera");
		break;
	}
	case VIDIOC_G_INPUT:
	{
		unsigned int *i = arg;

		*i = 0;
		break;
	}
	case VIDIOC_S_INPUT:
	{
		unsigned int *i = arg;

		if (*i > 0)
			ret=-EINVAL;
		break;
	}

	/* --- controls ---------------------------------------------- */
	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *qc = arg;
		int i;

		for (i = 0; i < ARRAY_SIZE(vivi_qctrl); i++)
			if (qc->id && qc->id == vivi_qctrl[i].id) {
				memcpy(qc, &(vivi_qctrl[i]),
					sizeof(*qc));
				break;
			}

		ret=-EINVAL;
		break;
	}
	case VIDIOC_G_CTRL:
	{
		struct v4l2_control *ctrl = arg;
		int i;

		for (i = 0; i < ARRAY_SIZE(vivi_qctrl); i++)
			if (ctrl->id == vivi_qctrl[i].id) {
				ctrl->value=qctl_regs[i];
				break;
			}

		ret=-EINVAL;
		break;
	}
	case VIDIOC_S_CTRL:
	{
		struct v4l2_control *ctrl = arg;
		int i;
		for (i = 0; i < ARRAY_SIZE(vivi_qctrl); i++)
			if (ctrl->id == vivi_qctrl[i].id) {
				if (ctrl->value <
					vivi_qctrl[i].minimum
					|| ctrl->value >
					vivi_qctrl[i].maximum) {
						ret=-ERANGE;
						break;
					}
				qctl_regs[i]=ctrl->value;
				break;
			}
		ret=-EINVAL;
		break;
	}
	default:
		ret=v4l_compat_translate_ioctl(inode,file,cmd,arg,vivi_do_ioctl);
	}

	if (debug) {
		if (ret<0) {
			v4l_print_ioctl("vivi(err)", cmd);
			dprintk(1,"errcode=%d\n",ret);
		} else if (_IOC_DIR(cmd) & _IOC_READ)
			v4l_printk_ioctl_arg("vivi(r)",cmd, arg);
	}

	return ret;
}

static int vivi_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, vivi_do_ioctl);
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

#define line_buf_size(norm) (norm_maxw(norm)*(format.depth+7)/8)

static int vivi_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct vivi_dev *h,*dev = NULL;
	struct vivi_fh *fh;
	struct list_head *list;
	enum v4l2_buf_type type = 0;
	int i;

	printk(KERN_DEBUG "vivi: open called (minor=%d)\n",minor);

	list_for_each(list,&vivi_devlist) {
		h = list_entry(list, struct vivi_dev, vivi_devlist);
		if (h->video_dev.minor == minor) {
			dev  = h;
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		}
	}
	if (NULL == dev)
		return -ENODEV;


	/* If more than one user, mutex should be added */
	dev->users++;

	dprintk(1,"open minor=%d type=%s users=%d\n",
				minor,v4l2_type_names[type],dev->users);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh),GFP_KERNEL);
	if (NULL == fh) {
		dev->users--;
		return -ENOMEM;
	}

	file->private_data = fh;
	fh->dev      = dev;
	fh->type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fh->fmt      = &format;
	fh->width    = 640;
	fh->height   = 480;

	/* Put all controls at a sane state */
	for (i = 0; i < ARRAY_SIZE(vivi_qctrl); i++)
		qctl_regs[i] =vivi_qctrl[i].default_value;

	dprintk(1,"Open: fh=0x%08lx, dev=0x%08lx, dev->vidq=0x%08lx\n",
		(unsigned long)fh,(unsigned long)dev,(unsigned long)&dev->vidq);
	dprintk(1,"Open: list_empty queued=%d\n",list_empty(&dev->vidq.queued));
	dprintk(1,"Open: list_empty active=%d\n",list_empty(&dev->vidq.active));

	/* Resets frame counters */
	dev->h=0;
	dev->m=0;
	dev->s=0;
	dev->us=0;
	dev->jiffies=jiffies;
	sprintf(dev->timestr,"%02d:%02d:%02d:%03d",
			dev->h,dev->m,dev->s,(dev->us+500)/1000);

	videobuf_queue_init(&fh->vb_vidq, &vivi_video_qops,
			NULL, NULL,
			fh->type,
			V4L2_FIELD_INTERLACED,
			sizeof(struct vivi_buffer),fh);

	return 0;
}

static ssize_t
vivi_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct vivi_fh *fh = file->private_data;

	if (fh->type==V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		if (res_locked(fh->dev))
			return -EBUSY;
		return videobuf_read_one(&fh->vb_vidq, data, count, ppos,
					file->f_flags & O_NONBLOCK);
	}
	return 0;
}

static unsigned int
vivi_poll(struct file *file, struct poll_table_struct *wait)
{
	struct vivi_fh *fh = file->private_data;
	struct vivi_buffer *buf;

	dprintk(1,"%s\n",__FUNCTION__);

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
		return POLLERR;

	if (res_get(fh->dev,fh)) {
		dprintk(1,"poll: mmap interface\n");
		/* streaming capture */
		if (list_empty(&fh->vb_vidq.stream))
			return POLLERR;
		buf = list_entry(fh->vb_vidq.stream.next,struct vivi_buffer,vb.stream);
	} else {
		dprintk(1,"poll: read() interface\n");
		/* read() capture */
		buf = (struct vivi_buffer*)fh->vb_vidq.read_buf;
		if (NULL == buf)
			return POLLERR;
	}
	poll_wait(file, &buf->vb.done, wait);
	if (buf->vb.state == STATE_DONE ||
	    buf->vb.state == STATE_ERROR)
		return POLLIN|POLLRDNORM;
	return 0;
}

static int vivi_release(struct inode *inode, struct file *file)
{
	struct vivi_fh  *fh     = file->private_data;
	struct vivi_dev *dev    = fh->dev;
	struct vivi_dmaqueue *vidq = &dev->vidq;

	int minor = iminor(inode);

	vivi_stop_thread(vidq);
	videobuf_mmap_free(&fh->vb_vidq);

	kfree (fh);

	dev->users--;

	printk(KERN_DEBUG "vivi: close called (minor=%d, users=%d)\n",minor,dev->users);

	return 0;
}

static int
vivi_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct vivi_fh *fh = file->private_data;
	int ret;

	dprintk (1,"mmap called, vma=0x%08lx\n",(unsigned long)vma);

	ret=videobuf_mmap_mapper(&fh->vb_vidq, vma);

	dprintk (1,"vma start=0x%08lx, size=%ld, ret=%d\n",
		(unsigned long)vma->vm_start,
		(unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
		ret);

	return ret;
}

static struct file_operations vivi_fops = {
	.owner		= THIS_MODULE,
	.open           = vivi_open,
	.release        = vivi_release,
	.read           = vivi_read,
	.poll		= vivi_poll,
	.ioctl          = vivi_ioctl,
	.mmap		= vivi_mmap,
	.llseek         = no_llseek,
};

static struct video_device vivi = {
	.name		= "VTM Virtual Video Capture Board",
	.type		= VID_TYPE_CAPTURE,
	.hardware	= 0,
	.fops           = &vivi_fops,
	.minor		= -1,
//	.release	= video_device_release,
};
/* ------------------------------------------------------------------
	Initialization and module stuff
   ------------------------------------------------------------------*/

static int __init vivi_init(void)
{
	int ret;
	struct vivi_dev *dev;

	dev = kzalloc(sizeof(*dev),GFP_KERNEL);
	if (NULL == dev)
		return -ENOMEM;
	list_add_tail(&dev->vivi_devlist,&vivi_devlist);

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	INIT_LIST_HEAD(&dev->vidq.queued);

	/* initialize locks */
	init_MUTEX(&dev->lock);

	dev->vidq.timeout.function = vivi_vid_timeout;
	dev->vidq.timeout.data     = (unsigned long)dev;
	init_timer(&dev->vidq.timeout);

	ret = video_register_device(&vivi, VFL_TYPE_GRABBER, video_nr);
	printk(KERN_INFO "Video Technology Magazine Virtual Video Capture Board (Load status: %d)\n", ret);
	return ret;
}

static void __exit vivi_exit(void)
{
	struct vivi_dev *h;
	struct list_head *list;

	list_for_each(list,&vivi_devlist) {
		h = list_entry(list, struct vivi_dev, vivi_devlist);
		kfree (h);
	}
	video_unregister_device(&vivi);
}

module_init(vivi_init);
module_exit(vivi_exit);
