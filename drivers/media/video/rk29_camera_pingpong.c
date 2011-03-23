/*
 * V4L2 Driver for RK28 camera host
 *
 * Copyright (C) 2006, Sascha Hauer, Pengutronix
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/time.h>
#include <linux/clk.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>

#include <mach/rk29_camera.h>
#include <mach/rk29_iomap.h>
#include <mach/iomux.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf-dma-contig.h>
#include <media/soc_camera.h>

// VIP Reg Offset
#define RK29_VIP_AHBR_CTRL                0x00
#define RK29_VIP_INT_MASK                 0x04
#define RK29_VIP_INT_STS                  0x08
#define RK29_VIP_STS                      0x0c
#define RK29_VIP_CTRL                     0x10
#define RK29_VIP_CAPTURE_F1SA_Y           0x14
#define RK29_VIP_CAPTURE_F1SA_UV          0x18
#define RK29_VIP_CAPTURE_F1SA_Cr          0x1c
#define RK29_VIP_CAPTURE_F2SA_Y           0x20
#define RK29_VIP_CAPTURE_F2SA_UV          0x24
#define RK29_VIP_CAPTURE_F2SA_Cr          0x28
#define RK29_VIP_FB_SR                    0x2c
#define RK29_VIP_FS                       0x30
#define RK29_VIP_VIPRESERVED              0x34
#define RK29_VIP_CROP                     0x38
#define RK29_VIP_CRM                      0x3c
#define RK29_VIP_RESET                    0x40
#define RK29_VIP_L_SFT                    0x44

//The key register bit descrition
// VIP_CTRL Reg
#define  DISABLE_CAPTURE              (0x00<<0)
#define  ENABLE_CAPTURE               (0x01<<0)
#define  HSY_HIGH_ACTIVE              (0x00<<1)
#define  HSY_LOW_ACTIVE               (0x01<<1)
#define  VIP_CCIR656                  (0x00<<2)
#define  VIP_SENSOR                   (0x01<<2)
#define  SENSOR_UYVY                  (0x00<<3)
#define  SENSOR_YUYV                  (0x01<<3)
#define  VIP_YUV                      (0x00<<4)
#define  VIP_RAW                      (0x01<<4)
#define  CON_OR_PIN                   (0x00<<5)
#define  ONEFRAME                     (0x01<<5)
#define  VIPREGYUV420                 (0x00<<6)
#define  VIPREGYUV422                 (0x01<<6)
#define  FIELD0_START                 (0x00<<7)
#define  FIELD1_START                 (0x01<<7)
#define  CONTINUOUS                   (0x00<<8)
#define  PING_PONG                    (0x01<<8)
#define  POSITIVE_EDGE                (0x00<<9)
#define  NEGATIVE_EDGE                (0x01<<9)
#define  VIPREGNTSC                   (0x00<<10)
#define  VIPREGPAL                    (0x01<<10)
#define  VIP_DATA_LITTLEEND           (0x00<<11)
#define  VIP_DATA_BIGEND              (0x01<<11)
#define  VSY_LOW_ACTIVE               (0x00<<12)
#define  VSY_HIGH_ACTIVE              (0x01<<12)
#define  VIP_RAWINPUT_BYPASS          (0x00<<13)
#define  VIP_RAWINPUT_POSITIVE_EDGE   (0x01<<13)
#define  VIP_RAWINPUT_NEGATIVE_EDGE   (0x02<<13)

//VIP_FB_SR
#define  VIP_F1_NOREADY               (0x00<<0)
#define  VIP_F1_READY                 (0x01<<0)
#define  VIP_F2_NOREADY               (0x00<<1)
#define  VIP_F2_READY                 (0x01<<1)
#define  VIP_FRAME_NOLOSS             (0x00<<2)
#define  VIP_FRAME_LOSS               (0x01<<2)
#define  VIP_F1_IS_LATEST             (0x00<<3)
#define  VIP_F2_IS_LATEST             (0x01<<3)

// GRF_SOC_CON0 Reg
#define  GRF_SOC_CON0_Reg             0xbc
#define  VIP_AXIMASTER                (0x00<<0)
#define  VIP_AHBMASTER                (0x01<<2)

// GRF_OS_REG0
#define  GRF_OS_REG0                  0xd0
#define  VIP_ACLK_DIV_HCLK_1          (0x00<<0)
#define  VIP_ACLK_DIV_HCLK_2          (0x01<<0)


#define MIN(x,y)   ((x<y) ? x: y)
#define MAX(x,y)    ((x>y) ? x: y)
#define RK29_SENSOR_24MHZ      24           /* MHz */
#define RK29_SENSOR_48MHZ      48

#define write_vip_reg(addr, val)  __raw_writel(val, addr+(rk29_camdev_info_ptr->base))
#define read_vip_reg(addr) __raw_readl(addr+(rk29_camdev_info_ptr->base))
#define mask_vip_reg(addr, msk, val)    write_vip_reg(addr, (val)|((~(msk))&read_vip_reg(addr)))

#define write_grf_reg(addr, val)  __raw_writel(val, addr+RK29_GRF_BASE)
#define read_grf_reg(addr) __raw_readl(addr+RK29_GRF_BASE)
#define mask_grf_reg(addr, msk, val)    write_vip_reg(addr, (val)|((~(msk))&read_vip_reg(addr)))

//Configure Macro
#define CONFIG_RK29CAMERA_TR      1
#define CONFIG_RK29CAMERA_DEBUG	  1
#if (CONFIG_RK29CAMERA_TR)
	#define RK29CAMERA_TR(format, ...)      printk(format, ## __VA_ARGS__)
	#if (CONFIG_RK29CAMERA_DEBUG)
	#define RK29CAMERA_DG(format, ...)      printk(format, ## __VA_ARGS__)
	#else
	#define RK29CAMERA_DG(format, ...)
	#endif
#else
	#define RK29CAMERA_TR(format, ...)
	#define RK29CAMERA_DG(format, ...)
#endif

#define RK29_CAM_VERSION_CODE KERNEL_VERSION(0, 0, 1)

/* limit to rk29 hardware capabilities */
#define RK29_CAM_BUS_PARAM   (SOCAM_MASTER |\
                SOCAM_HSYNC_ACTIVE_HIGH |\
                SOCAM_HSYNC_ACTIVE_LOW |\
                SOCAM_VSYNC_ACTIVE_HIGH |\
                SOCAM_VSYNC_ACTIVE_LOW |\
                SOCAM_PCLK_SAMPLE_RISING |\
                SOCAM_PCLK_SAMPLE_FALLING|\
                SOCAM_DATA_ACTIVE_HIGH |\
                SOCAM_DATA_ACTIVE_LOW|\
                SOCAM_DATAWIDTH_8|SOCAM_DATAWIDTH_10|\
                SOCAM_MCLK_24MHZ |SOCAM_MCLK_48MHZ)

#define RK29_CAM_W_MIN        48
#define RK29_CAM_H_MIN        32
#define RK29_CAM_W_MAX        3856            /* ddl@rock-chips.com : 10M Pixel */
#define RK29_CAM_H_MAX        2764
#define RK29_CAM_FRAME_INVAL_INIT 3
#define RK29_CAM_FRAME_INVAL_DC 1             /* ddl@rock-chips.com :  */

// Variable and Function Declare
extern void videobuf_dma_contig_free(struct videobuf_queue *q, struct videobuf_buffer *buf);
extern dma_addr_t videobuf_to_dma_contig(struct videobuf_buffer *buf);
extern void videobuf_queue_dma_contig_init(struct videobuf_queue *q,
            struct videobuf_queue_ops *ops,
            struct device *dev,
            spinlock_t *irqlock,
            enum v4l2_buf_type type,
            enum v4l2_field field,
            unsigned int msize,
            void *priv);

/* buffer for one video frame */
struct rk29_buffer
{
    /* common v4l buffer stuff -- must be first */
    struct videobuf_buffer vb;
    const struct soc_camera_data_format        *fmt;
    int			inwork;
};
enum rk29_camera_reg_state
{
	Reg_Invalidate,
	Reg_Validate
};

struct rk29_camera_reg
{
	unsigned int VipCtrl;
	unsigned int VipCrop;
	unsigned int VipFs;
	unsigned int VipIntMsk;
	unsigned int VipCrm;
	enum rk29_camera_reg_state Inval;
};
struct rk29_camera_dev
{
    struct soc_camera_host	soc_host;
    struct device		*dev;
    /* RK2827x is only supposed to handle one camera on its Quick Capture
     * interface. If anyone ever builds hardware to enable more than
     * one camera, they will have to modify this driver too */
    struct soc_camera_device *icd;
    struct clk *clk;
    void __iomem *base;
    int frame_inval;           /* ddl@rock-chips.com : The first frames is invalidate  */
    unsigned int irq;

    struct rk29camera_platform_data *pdata;
    struct resource		*res;

    struct list_head	capture;

    spinlock_t		lock;

    struct videobuf_buffer	*active1;
	struct videobuf_buffer  *active2;
	struct videobuf_queue *vb_vidq_ptr;
	struct rk29_camera_reg reginfo_suspend;

	struct hrtimer frame_timer;
};
static DEFINE_MUTEX(camera_lock);
static const char *rk29_cam_driver_description = "RK29_Camera";
static struct rk29_camera_dev *rk29_camdev_info_ptr;
static int rk29_first;

static int rk29_camera_s_stream(struct soc_camera_device *icd, int enable);


/*
 *  Videobuf operations
 */
static int rk29_videobuf_setup(struct videobuf_queue *vq, unsigned int *count,
                               unsigned int *size)
{
    struct soc_camera_device *icd = vq->priv_data;
    int bytes_per_pixel = (icd->current_fmt->depth + 7) >> 3;

    dev_dbg(&icd->dev, "count=%d, size=%d\n", *count, *size);

    /* planar capture requires Y, U and V buffers to be page aligned */
    *size = PAGE_ALIGN( icd->user_width * icd->user_height * bytes_per_pixel);                               /* Y pages UV pages, yuv422*/

    RK29CAMERA_DG("\n%s..%d.. size = %d\n",__FUNCTION__,__LINE__, *size);

    return 0;
}
static void rk29_videobuf_free(struct videobuf_queue *vq, struct rk29_buffer *buf)
{
    struct soc_camera_device *icd = vq->priv_data;

    dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
            &buf->vb, buf->vb.baddr, buf->vb.bsize);

	/* ddl@rock-chips.com: buf_release called soc_camera_streamoff and soc_camera_close*/
	if (buf->vb.state == VIDEOBUF_NEEDS_INIT)
		return;

    if (in_interrupt())
        BUG();

    videobuf_dma_contig_free(vq, &buf->vb);
    dev_dbg(&icd->dev, "%s freed\n", __func__);
    buf->vb.state = VIDEOBUF_NEEDS_INIT;
	return;
}
static int rk29_videobuf_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb, enum v4l2_field field)
{
    struct soc_camera_device *icd = vq->priv_data;
    struct rk29_buffer *buf;
    int ret;

    buf = container_of(vb, struct rk29_buffer, vb);

    dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
            vb, vb->baddr, vb->bsize);

    //RK29CAMERA_TR("\n%s..%d..  \n",__FUNCTION__,__LINE__);

    /* Added list head initialization on alloc */
    WARN_ON(!list_empty(&vb->queue));

    /* This can be useful if you want to see if we actually fill
     * the buffer with something */
    //memset((void *)vb->baddr, 0xaa, vb->bsize);

    BUG_ON(NULL == icd->current_fmt);

    if (buf->fmt    != icd->current_fmt ||
            vb->width   != icd->user_width ||
            vb->height  != icd->user_height ||
             vb->field   != field) {
        buf->fmt    = icd->current_fmt;
        vb->width   = icd->user_width;
        vb->height  = icd->user_height;
        vb->field   = field;
        vb->state   = VIDEOBUF_NEEDS_INIT;
    }

    vb->size = vb->width * vb->height * ((buf->fmt->depth + 7) >> 3) ;          /* ddl@rock-chips.com : fmt->depth is coorect */
    if (0 != vb->baddr && vb->bsize < vb->size) {
        ret = -EINVAL;
        goto out;
    }

    if (vb->state == VIDEOBUF_NEEDS_INIT) {
        ret = videobuf_iolock(vq, vb, NULL);
        if (ret) {
            goto fail;
        }
        vb->state = VIDEOBUF_PREPARED;
    }
    return 0;
fail:
    rk29_videobuf_free(vq, buf);
out:
    return ret;
}

static inline void rk29_videobuf_capture(struct videobuf_buffer *vb)
{
    unsigned int size;
	int fb_sr;
	struct rk29_camera_dev *pcdev = rk29_camdev_info_ptr;

	if (vb) {
		size = vb->width * vb->height;    /* Y pages UV pages, yuv422*/
		fb_sr = read_vip_reg(RK29_VIP_FB_SR);
		if (fb_sr & VIP_F1_READY) {
			write_vip_reg(RK29_VIP_CAPTURE_F1SA_Y, vb->boff);
    		write_vip_reg(RK29_VIP_CAPTURE_F1SA_UV, vb->boff + size);
			write_vip_reg(RK29_VIP_FB_SR,  read_vip_reg(RK29_VIP_FB_SR)&(~VIP_F1_READY));
			pcdev->active1 = vb;
			RK29CAMERA_DG("f1 Start.. vb->boff:0x%x f2 vb->boff:0x%x FB_SR:0x%x, active1:0x%x, active2:0x%x\n",(unsigned int)vb->boff,read_vip_reg(RK29_VIP_CAPTURE_F2SA_Y), read_vip_reg(RK29_VIP_FB_SR)&0x03,
				   (unsigned int)pcdev->active1,(unsigned int)pcdev->active2);
		} else if (fb_sr & VIP_F2_READY) {
			write_vip_reg(RK29_VIP_CAPTURE_F2SA_Y, vb->boff);
    		write_vip_reg(RK29_VIP_CAPTURE_F2SA_UV, vb->boff + size);
			write_vip_reg(RK29_VIP_FB_SR,  read_vip_reg(RK29_VIP_FB_SR)&(~VIP_F2_READY));
			pcdev->active2 = vb;
			RK29CAMERA_DG("f2 Start..vb->boff:0x%x f1 vb->boff:0x%x FB_SR:0x%x, active1:0x%x, active2:0x%x\n",(unsigned int)vb->boff,read_vip_reg(RK29_VIP_CAPTURE_F1SA_Y), read_vip_reg(RK29_VIP_FB_SR)&0x03,
				  (unsigned int)pcdev->active1,(unsigned int)pcdev->active2);
        }
	}
}
/* Locking: Caller holds q->irqlock */
static void rk29_videobuf_queue(struct videobuf_queue *vq,
                                struct videobuf_buffer *vb)
{
    struct soc_camera_device *icd = vq->priv_data;
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
	unsigned int size;

    dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
            vb, vb->baddr, vb->bsize);

    vb->state = VIDEOBUF_QUEUED;

	if (!list_empty(&pcdev->capture)) {
		list_add_tail(&vb->queue, &pcdev->capture);
	} else {
		if (list_entry(pcdev->capture.next, struct videobuf_buffer, queue) != vb)
			list_add_tail(&vb->queue, &pcdev->capture);
		else
			BUG();    /* ddl@rock-chips.com : The same videobuffer queue again */
	}

    if (!pcdev->active1) {
        rk29_videobuf_capture(vb);
    }
}
static irqreturn_t rk29_camera_irq(int irq, void *data)
{
    struct rk29_camera_dev *pcdev = data;
    struct videobuf_buffer *vb=NULL,**nxt_active,**active;
	struct list_head *next_head;
	int fb_sr,vip_ctrl;

    read_vip_reg(RK29_VIP_INT_STS);    /* clear vip interrupte single  */

    /* ddl@rock-chps.com : Current VIP is run in One Frame Mode, Frame 1 is validate */
	fb_sr = read_vip_reg(RK29_VIP_FB_SR);
	vip_ctrl = read_vip_reg(RK29_VIP_CTRL);
    if (fb_sr & (VIP_F1_READY|VIP_F2_READY)) {
		#if 0
        if (pcdev->frame_inval>0) {
			if (((fb_sr & (VIP_F1_READY|VIP_F2_READY)) == (VIP_F1_READY|VIP_F2_READY)) && (vip_ctrl & PING_PONG)){
				write_vip_reg(RK29_VIP_FB_SR,  read_vip_reg(RK29_VIP_FB_SR)&(~(VIP_F1_READY|VIP_F2_READY)));
				pcdev->frame_inval -= 2;
	        } else if (fb_sr & VIP_F1_READY) {
				write_vip_reg(RK29_VIP_FB_SR,  read_vip_reg(RK29_VIP_FB_SR)&(~VIP_F1_READY));
				pcdev->frame_inval--;
			} else if ((fb_sr & VIP_F2_READY) && (vip_ctrl & PING_PONG)) {
				write_vip_reg(RK29_VIP_FB_SR,  read_vip_reg(RK29_VIP_FB_SR)&(~VIP_F2_READY));
				pcdev->frame_inval--;
	        }

			if (pcdev->frame_inval>=0)
            	goto RK29_CAMERA_IRQ_END;
        } else if (pcdev->frame_inval) {
        	RK29CAMERA_TR("frame_inval : %0x",pcdev->frame_inval);
            pcdev->frame_inval = 0;
        }


		if (((fb_sr & (VIP_F1_READY|VIP_F2_READY)) == (VIP_F1_READY|VIP_F2_READY)) && (vip_ctrl & PING_PONG)) {
			if ((read_vip_reg(RK29_VIP_FB_SR)&VIP_F2_IS_LATEST) == VIP_F2_IS_LATEST) {
				vb = pcdev->active2;
				write_vip_reg(RK29_VIP_FB_SR,  read_vip_reg(RK29_VIP_FB_SR)&(~VIP_F1_READY));
			} else {
				vb = pcdev->active1;
				write_vip_reg(RK29_VIP_FB_SR,  read_vip_reg(RK29_VIP_FB_SR)&(~VIP_F2_READY));
			}
        } else
		#endif

		if (fb_sr & VIP_F1_READY) {
			vb = pcdev->active1;
			if (rk29_first == 0) {
				rk29_first = 1;
				list_del_init(&vb->queue);
				write_vip_reg(RK29_VIP_FB_SR,  read_vip_reg(RK29_VIP_FB_SR)&(~VIP_F1_READY));
				goto RK29_CAMERA_IRQ_END1;
			}
		} else if ((fb_sr & VIP_F2_READY) && (vip_ctrl & PING_PONG)) {
			vb = pcdev->active2;
        }

		if (!vb) {
			goto RK29_CAMERA_IRQ_END;
		}

		/* ddl@rock-chips.com : this vb may be deleted from queue */
		if ((vb->state == VIDEOBUF_QUEUED) || (vb->state == VIDEOBUF_ACTIVE)) {
        	list_del_init(&vb->queue);

			vb->state = VIDEOBUF_DONE;
	        do_gettimeofday(&vb->ts);
	        vb->field_count++;
		}

        wake_up(&vb->done);
RK29_CAMERA_IRQ_END1:
		hrtimer_start(&pcdev->frame_timer,
						ktime_set(0, 10000),
						HRTIMER_MODE_REL);
    }

RK29_CAMERA_IRQ_END:
    return IRQ_HANDLED;
}


static enum hrtimer_restart rk29_camera_timer_func(struct hrtimer *timer)
{
	struct rk29_camera_dev *pcdev = container_of(timer, struct rk29_camera_dev, frame_timer);
	struct videobuf_buffer *vb=NULL,**nxt_active;
	int fb_sr;

	fb_sr = read_vip_reg(RK29_VIP_FB_SR);
	if (fb_sr & VIP_F1_READY) {
		nxt_active = &pcdev->active1;
	} else if (fb_sr & VIP_F2_READY)  {
		nxt_active = &pcdev->active2;
    }

	*nxt_active = NULL;
    if (!list_empty(&pcdev->capture)) {
        *nxt_active = list_entry(pcdev->capture.next, struct videobuf_buffer, queue);
		if (*nxt_active) {
			rk29_videobuf_capture(*nxt_active);
			list_del_init(&(*nxt_active)->queue);
		}
    }

    if (*nxt_active == NULL) {
		RK29CAMERA_DG("%s video_buf queue is empty!\n",__FUNCTION__);
    }

	return HRTIMER_NORESTART;
}

static void rk29_videobuf_release(struct videobuf_queue *vq,
                                  struct videobuf_buffer *vb)
{
    struct rk29_buffer *buf = container_of(vb, struct rk29_buffer, vb);
    struct soc_camera_device *icd = vq->priv_data;
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;

#ifdef DEBUG
    dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
            vb, vb->baddr, vb->bsize);

    switch (vb->state)
    {
        case VIDEOBUF_ACTIVE:
            dev_dbg(&icd->dev, "%s (active)\n", __func__);
            break;
        case VIDEOBUF_QUEUED:
            dev_dbg(&icd->dev, "%s (queued)\n", __func__);
            break;
        case VIDEOBUF_PREPARED:
            dev_dbg(&icd->dev, "%s (prepared)\n", __func__);
            break;
        default:
            dev_dbg(&icd->dev, "%s (unknown)\n", __func__);
            break;
    }
#endif
	if ((vb == pcdev->active1) || (vb == pcdev->active2)) {
		RK29CAMERA_DG("%s Wait for this video buf(0x%x) write finished!\n ",__FUNCTION__,(unsigned int)vb);
		interruptible_sleep_on_timeout(&vb->done, 100);
		RK29CAMERA_DG("%s This video buf(0x%x) write finished, release now!!\n",__FUNCTION__,(unsigned int)vb);
	}
    rk29_videobuf_free(vq, buf);
}

static struct videobuf_queue_ops rk29_videobuf_ops =
{
    .buf_setup      = rk29_videobuf_setup,
    .buf_prepare    = rk29_videobuf_prepare,
    .buf_queue      = rk29_videobuf_queue,
    .buf_release    = rk29_videobuf_release,
};

static void rk29_camera_init_videobuf(struct videobuf_queue *q,
                                      struct soc_camera_device *icd)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;

    /* We must pass NULL as dev pointer, then all pci_* dma operations
     * transform to normal dma_* ones. */
    videobuf_queue_dma_contig_init(q,
                                   &rk29_videobuf_ops,
                                   ici->v4l2_dev.dev, &pcdev->lock,
                                   V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                   V4L2_FIELD_NONE,
                                   sizeof(struct rk29_buffer),
                                   icd);
	pcdev->vb_vidq_ptr = q;		/* ddl@rock-chips.com */
}
static int rk29_camera_activate(struct rk29_camera_dev *pcdev, struct soc_camera_device *icd)
{
    unsigned long sensor_bus_flags = SOCAM_MCLK_24MHZ;
    struct clk *parent;

    RK29CAMERA_DG("\n%s..%d.. \n",__FUNCTION__,__LINE__);
    if (!pcdev->clk || IS_ERR(pcdev->clk))
        RK29CAMERA_TR(KERN_ERR "failed to get vip_clk source\n");

    //if (icd->ops->query_bus_param)                                                  /* ddl@rock-chips.com : Query Sensor's xclk */
        //sensor_bus_flags = icd->ops->query_bus_param(icd);

    if (sensor_bus_flags & SOCAM_MCLK_48MHZ) {
        parent = clk_get(NULL, "clk48m");
        if (!parent || IS_ERR(parent))
             goto RK29_CAMERA_ACTIVE_ERR;
    } else if (sensor_bus_flags & SOCAM_MCLK_27MHZ) {
        parent = clk_get(NULL, "extclk");
        if (!parent || IS_ERR(parent))
             goto RK29_CAMERA_ACTIVE_ERR;
    } else {
        parent = clk_get(NULL, "xin24m");
        if (!parent || IS_ERR(parent))
             goto RK29_CAMERA_ACTIVE_ERR;
    }

    clk_set_parent(pcdev->clk, parent);

    clk_enable(pcdev->clk);
    rk29_mux_api_set(GPIO1B4_VIPCLKOUT_NAME, GPIO1L_VIP_CLKOUT);
    ndelay(10);

	//write_grf_reg(GRF_SOC_CON0_Reg, read_grf_reg(GRF_SOC_CON0_Reg)|VIP_AHBMASTER);  //VIP Config to AHB
	write_grf_reg(GRF_SOC_CON0_Reg, read_grf_reg(GRF_SOC_CON0_Reg)&(~VIP_AHBMASTER));  //VIP Config to AXI
	write_grf_reg(GRF_OS_REG0, read_grf_reg(GRF_OS_REG0)&(~VIP_ACLK_DIV_HCLK_2));   //aclk:hclk = 1:1
	//write_grf_reg(GRF_OS_REG0, read_grf_reg(GRF_OS_REG0)|VIP_ACLK_DIV_HCLK_2);   //aclk:hclk = 2:1

    write_vip_reg(RK29_VIP_RESET, 0x76543210);  /* ddl@rock-chips.com : vip software reset */
    udelay(10);

    write_vip_reg(RK29_VIP_AHBR_CTRL, 0x07);   /* ddl@rock-chips.com : vip ahb burst 16 */
    write_vip_reg(RK29_VIP_INT_MASK, 0x01);    //capture complete interrupt enable
    write_vip_reg(RK29_VIP_CRM,  0x00000000);  //Y/CB/CR color modification

    return 0;
RK29_CAMERA_ACTIVE_ERR:
    return -ENODEV;
}

static void rk29_camera_deactivate(struct rk29_camera_dev *pcdev)
{
    pcdev->active1 = NULL;
	pcdev->active2 = NULL;

    write_vip_reg(RK29_VIP_CTRL, 0);
    read_vip_reg(RK29_VIP_INT_STS);             //clear vip interrupte single

    rk29_mux_api_set(GPIO1B4_VIPCLKOUT_NAME, GPIO1L_GPIO1B4);
    clk_disable(pcdev->clk);

    return;
}

/* The following two functions absolutely depend on the fact, that
 * there can be only one camera on RK28 quick capture interface */
static int rk29_camera_add_device(struct soc_camera_device *icd)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
    struct device *control = to_soc_camera_control(icd);
    struct v4l2_subdev *sd;
    int ret;

    mutex_lock(&camera_lock);

    if (pcdev->icd) {
        ret = -EBUSY;
        goto ebusy;
    }

    dev_info(&icd->dev, "RK29 Camera driver attached to camera %d\n",
             icd->devnum);

	pcdev->frame_inval = RK29_CAM_FRAME_INVAL_INIT;
    pcdev->active1 = NULL;
	pcdev->active2 = NULL;
    pcdev->icd = NULL;
	pcdev->reginfo_suspend.Inval = Reg_Invalidate;
	/* ddl@rock-chips.com: capture list must be reset, because this list may be not empty,
     * if app havn't dequeue all videobuf before close camera device;
	*/
    INIT_LIST_HEAD(&pcdev->capture);

    ret = rk29_camera_activate(pcdev,icd);
    if (ret)
        goto ebusy;

    /* ddl@rock-chips.com : v4l2_subdev is not created when ici->ops->add called in soc_camera_probe  */
    if (control) {
        sd = dev_get_drvdata(control);
        ret = v4l2_subdev_call(sd,core, init, 0);
        if (ret)
            goto ebusy;
    }

    pcdev->icd = icd;

ebusy:
    mutex_unlock(&camera_lock);

    return ret;
}
static void rk29_camera_remove_device(struct soc_camera_device *icd)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;

    BUG_ON(icd != pcdev->icd);

    dev_info(&icd->dev, "RK29 Camera driver detached from camera %d\n",
             icd->devnum);

	rk29_camera_deactivate(pcdev);

	/* ddl@rock-chips.com: Call videobuf_mmap_free here for free the struct video_buffer which malloc in videobuf_alloc */
	if (pcdev->vb_vidq_ptr) {
		videobuf_mmap_free(pcdev->vb_vidq_ptr);
		pcdev->vb_vidq_ptr = NULL;
	}

	pcdev->active1 = NULL;
	pcdev->active2 = NULL;
    pcdev->icd = NULL;
	pcdev->reginfo_suspend.Inval = Reg_Invalidate;
	/* ddl@rock-chips.com: capture list must be reset, because this list may be not empty,
     * if app havn't dequeue all videobuf before close camera device;
	*/
    INIT_LIST_HEAD(&pcdev->capture);

	return;
}

static int rk29_camera_set_bus_param(struct soc_camera_device *icd, __u32 pixfmt)
{
    unsigned long bus_flags, camera_flags, common_flags;
    unsigned int vip_ctrl_val = 0;
    int ret = 0;

    RK29CAMERA_DG("\n%s..%d..\n",__FUNCTION__,__LINE__);

    bus_flags = RK29_CAM_BUS_PARAM;
	if (icd->ops->query_bus_param)
    	camera_flags = icd->ops->query_bus_param(icd);
	else
		camera_flags = 0;

    common_flags = soc_camera_bus_param_compatible(camera_flags, bus_flags);
    if (!common_flags) {
        ret = -EINVAL;
        goto RK29_CAMERA_SET_BUS_PARAM_END;
    }

    ret = icd->ops->set_bus_param(icd, common_flags);
    if (ret < 0)
        goto RK29_CAMERA_SET_BUS_PARAM_END;

	if (common_flags & SOCAM_DATAWIDTH_8) {
        icd->buswidth = 8;
	} else if (common_flags & SOCAM_DATAWIDTH_10) {
	    icd->buswidth = 10;
	}

    vip_ctrl_val = read_vip_reg(RK29_VIP_CTRL);
    if (common_flags & SOCAM_PCLK_SAMPLE_FALLING) {
        vip_ctrl_val |= NEGATIVE_EDGE;
    } else {
		vip_ctrl_val &= ~NEGATIVE_EDGE;
    }
    if (common_flags & SOCAM_HSYNC_ACTIVE_LOW) {
        vip_ctrl_val |= HSY_LOW_ACTIVE;
    } else {
		vip_ctrl_val &= ~HSY_LOW_ACTIVE;
    }
    if (common_flags & SOCAM_VSYNC_ACTIVE_HIGH) {
        vip_ctrl_val |= VSY_HIGH_ACTIVE;
    } else {
		vip_ctrl_val &= ~VSY_HIGH_ACTIVE;
    }

    /* ddl@rock-chips.com : Don't enable capture here, enable in stream_on */
    //vip_ctrl_val |= ENABLE_CAPTURE;

    write_vip_reg(RK29_VIP_CTRL, vip_ctrl_val);
    RK29CAMERA_DG("\n%s..CtrReg=%x  \n",__FUNCTION__,read_vip_reg(RK29_VIP_CTRL));

RK29_CAMERA_SET_BUS_PARAM_END:
	if (ret)
    	RK29CAMERA_TR("\n%s..%d.. ret = %d \n",__FUNCTION__,__LINE__, ret);
    return ret;
}

static int rk29_camera_try_bus_param(struct soc_camera_device *icd, __u32 pixfmt)
{
    unsigned long bus_flags, camera_flags;
    int ret;

    bus_flags = RK29_CAM_BUS_PARAM;
	if (icd->ops->query_bus_param) {
        camera_flags = icd->ops->query_bus_param(icd);
	} else {
		camera_flags = 0;
	}
    ret = soc_camera_bus_param_compatible(camera_flags, bus_flags) ;

    if (ret < 0)
        dev_warn(icd->dev.parent,
			 "Flags incompatible: camera %lx, host %lx\n",
			 camera_flags, bus_flags);
    return ret;
}
static const struct soc_camera_data_format rk29_camera_formats[] = {
	{
		.name		= "Planar YUV420 12 bit",
		.depth		= 12,
		.fourcc		= V4L2_PIX_FMT_YUV420,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},{
		.name		= "Planar YUV422 16 bit",
		.depth		= 16,
		.fourcc		= V4L2_PIX_FMT_YUV422P,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},{
		.name		= "Raw Bayer RGB 10 bit",
		.depth		= 16,
		.fourcc		= V4L2_PIX_FMT_SGRBG10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
	}
};

static void rk29_camera_setup_format(struct soc_camera_device *icd, __u32 host_pixfmt, __u32 cam_pixfmt, struct v4l2_rect *rect)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
    unsigned int vip_fs = 0,vip_crop = 0;
    unsigned int vip_ctrl_val = VIP_SENSOR|PING_PONG|DISABLE_CAPTURE;

    switch (host_pixfmt)
    {
        case V4L2_PIX_FMT_YUV422P:
            vip_ctrl_val |= VIPREGYUV422;
			pcdev->frame_inval = RK29_CAM_FRAME_INVAL_DC;
            break;
        case V4L2_PIX_FMT_YUV420:
            vip_ctrl_val |= VIPREGYUV420;
			if (pcdev->frame_inval != RK29_CAM_FRAME_INVAL_INIT)
				pcdev->frame_inval = RK29_CAM_FRAME_INVAL_INIT;
            break;
		case V4L2_PIX_FMT_SGRBG10:
			vip_ctrl_val |= (VIP_RAW | VIP_SENSOR | VIP_DATA_LITTLEEND);
			pcdev->frame_inval = RK29_CAM_FRAME_INVAL_DC;
			break;
        default:                                                                                /* ddl@rock-chips.com : vip output format is hold when pixfmt is invalidate */
            vip_ctrl_val |= (read_vip_reg(RK29_VIP_CTRL) & VIPREGYUV422);
            break;
    }

    switch (cam_pixfmt)
    {
        case V4L2_PIX_FMT_UYVY:
            vip_ctrl_val |= SENSOR_UYVY;
            break;
        case V4L2_PIX_FMT_YUYV:
            vip_ctrl_val |= SENSOR_YUYV;
            break;
        default :
            vip_ctrl_val |= (read_vip_reg(RK29_VIP_CTRL) & SENSOR_YUYV);
            break;
    }

    write_vip_reg(RK29_VIP_CTRL, vip_ctrl_val);         /* ddl@rock-chips.com: VIP capture mode and capture format must be set before FS register set */

    read_vip_reg(RK29_VIP_INT_STS);                     /* clear vip interrupte single  */

    if (vip_ctrl_val & ONEFRAME)  {
        vip_crop = ((rect->left<<16) + rect->top);
        vip_fs  = (((rect->width + rect->left)<<16) + (rect->height+rect->top));
    } else if (vip_ctrl_val & PING_PONG) {
        if (rect->left ||rect->top ) {
			RK29CAMERA_DG("\n %s..PingPang not support Crop \n",__FUNCTION__);
			BUG();
        }
		vip_crop = 0;
        vip_fs  = (((rect->width + rect->left)<<16) + (rect->height+rect->top));
    }

    write_vip_reg(RK29_VIP_CROP, vip_crop);
    write_vip_reg(RK29_VIP_FS, vip_fs);

    write_vip_reg(RK29_VIP_FB_SR, (VIP_F2_READY|VIP_F1_READY));

    RK29CAMERA_DG("\n%s.. crop:0x%x fs:0x%x CtrlReg:0x%x\n",__FUNCTION__,vip_crop,vip_fs,read_vip_reg(RK29_VIP_CTRL));
	return;
}

static int rk29_camera_get_formats(struct soc_camera_device *icd, int idx,
				  struct soc_camera_format_xlate *xlate)
{
    struct device *dev = icd->dev.parent;
    int formats = 0, buswidth, ret;

    buswidth = 8;

    ret = rk29_camera_try_bus_param(icd, buswidth);
    if (ret < 0)
        return 0;

    switch (icd->formats[idx].fourcc) {
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_YUYV:
            formats++;
            if (xlate) {
                xlate->host_fmt = &rk29_camera_formats[0];
                xlate->cam_fmt = icd->formats + idx;
                xlate->buswidth = buswidth;
                xlate++;
                dev_dbg(dev, "Providing format %s using %s\n",
                	rk29_camera_formats[0].name,
                	icd->formats[idx].name);
            }

            formats++;
            if (xlate) {
                xlate->host_fmt = &rk29_camera_formats[1];
                xlate->cam_fmt = icd->formats + idx;
                xlate->buswidth = buswidth;
                xlate++;
                dev_dbg(dev, "Providing format %s using %s\n",
                	rk29_camera_formats[1].name,
                	icd->formats[idx].name);
            }
			break;
		case V4L2_PIX_FMT_SGRBG10:
			formats++;
            if (xlate) {
                xlate->host_fmt = &rk29_camera_formats[2];
                xlate->cam_fmt = icd->formats + idx;
                xlate->buswidth = 10;
                xlate++;
                dev_dbg(dev, "Providing format %s using %s\n",
                	rk29_camera_formats[2].name,
                	icd->formats[idx].name);
            }
			break;
        default:
            break;
    }

    return formats;
}

static void rk29_camera_put_formats(struct soc_camera_device *icd)
{
	return;
}

static int rk29_camera_set_crop(struct soc_camera_device *icd,
			       struct v4l2_crop *a)
{
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
    struct v4l2_format f;
    struct v4l2_pix_format *pix = &f.fmt.pix;
    int ret;

    f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = v4l2_subdev_call(sd, video, g_fmt, &f);
    if (ret < 0)
        return ret;

    if ((pix->width < (a->c.left + a->c.width)) || (pix->height < (a->c.top + a->c.height)))  {

        pix->width = a->c.left + a->c.width;
        pix->height = a->c.top + a->c.height;

        v4l_bound_align_image(&pix->width, RK29_CAM_W_MIN, RK29_CAM_W_MAX, 1,
            &pix->height, RK29_CAM_H_MIN, RK29_CAM_H_MAX, 0,
            icd->current_fmt->fourcc == V4L2_PIX_FMT_YUV422P ?4 : 0);

        ret = v4l2_subdev_call(sd, video, s_fmt, &f);
        if (ret < 0)
            return ret;
    }

    rk29_camera_setup_format(icd, icd->current_fmt->fourcc, pix->pixelformat, &a->c);

    icd->user_width = pix->width;
    icd->user_height = pix->height;

    return 0;
}

static int rk29_camera_set_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
    struct device *dev = icd->dev.parent;
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
    const struct soc_camera_data_format *cam_fmt = NULL;
    const struct soc_camera_format_xlate *xlate = NULL;
    struct v4l2_pix_format *pix = &f->fmt.pix;
    struct v4l2_format cam_f = *f;
    struct v4l2_rect rect;
    int ret;

    RK29CAMERA_DG("\n%s..%d..  \n",__FUNCTION__,__LINE__);

    xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
    if (!xlate) {
        dev_err(dev, "Format %x not found\n", pix->pixelformat);
        ret = -EINVAL;
        goto RK29_CAMERA_SET_FMT_END;
    }

    cam_fmt = xlate->cam_fmt;

    cam_f.fmt.pix.pixelformat = cam_fmt->fourcc;
    ret = v4l2_subdev_call(sd, video, s_fmt, &cam_f);
    cam_f.fmt.pix.pixelformat = pix->pixelformat;
    *pix = cam_f.fmt.pix;

    icd->sense = NULL;

    if (!ret) {
        rect.left = 0;
        rect.top = 0;
        rect.width = pix->width;
        rect.height = pix->height;

        RK29CAMERA_DG("\n%s..%s..%s \n",__FUNCTION__,xlate->host_fmt->name, cam_fmt->name);
        rk29_camera_setup_format(icd, pix->pixelformat, cam_fmt->fourcc, &rect);

        icd->buswidth = xlate->buswidth;
        icd->current_fmt = xlate->host_fmt;
    }

RK29_CAMERA_SET_FMT_END:
	if (ret)
    	RK29CAMERA_TR("\n%s..%d.. ret = %d  \n",__FUNCTION__,__LINE__, ret);
    return ret;
}

static int rk29_camera_try_fmt(struct soc_camera_device *icd,
                                   struct v4l2_format *f)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
    const struct soc_camera_format_xlate *xlate;
    struct v4l2_pix_format *pix = &f->fmt.pix;
    __u32 pixfmt = pix->pixelformat;
    enum v4l2_field field;
    int ret;

    RK29CAMERA_DG("\n%s..%d.. \n",__FUNCTION__,__LINE__);

    xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
    if (!xlate) {
        dev_err(ici->v4l2_dev.dev, "Format %x not found\n", pixfmt);
        ret = -EINVAL;
        goto RK29_CAMERA_TRY_FMT_END;
    }
   /* limit to rk29 hardware capabilities */
    v4l_bound_align_image(&pix->width, RK29_CAM_W_MIN, RK29_CAM_W_MAX, 1,
    	      &pix->height, RK29_CAM_H_MIN, RK29_CAM_H_MAX, 0,
    	      pixfmt == V4L2_PIX_FMT_YUV422P ? 4 : 0);

    pix->bytesperline = pix->width * DIV_ROUND_UP(xlate->host_fmt->depth, 8);
    pix->sizeimage = pix->height * pix->bytesperline;

    /* camera has to see its format, but the user the original one */
    pix->pixelformat = xlate->cam_fmt->fourcc;
    /* limit to sensor capabilities */
    ret = v4l2_subdev_call(sd, video, try_fmt, f);
    pix->pixelformat = pixfmt;

    field = pix->field;

    if (field == V4L2_FIELD_ANY) {
        pix->field = V4L2_FIELD_NONE;
    } else if (field != V4L2_FIELD_NONE) {
        dev_err(icd->dev.parent, "Field type %d unsupported.\n", field);
        ret = -EINVAL;
        goto RK29_CAMERA_TRY_FMT_END;
    }

RK29_CAMERA_TRY_FMT_END:
	if (ret)
    	RK29CAMERA_TR("\n%s..%d.. ret = %d  \n",__FUNCTION__,__LINE__, ret);
    return ret;
}

static int rk29_camera_reqbufs(struct soc_camera_file *icf,
                               struct v4l2_requestbuffers *p)
{
    int i;

    /* This is for locking debugging only. I removed spinlocks and now I
     * check whether .prepare is ever called on a linked buffer, or whether
     * a dma IRQ can occur for an in-work or unlinked buffer. Until now
     * it hadn't triggered */
    for (i = 0; i < p->count; i++) {
        struct rk29_buffer *buf = container_of(icf->vb_vidq.bufs[i],
                                                           struct rk29_buffer, vb);
        buf->inwork = 0;
        INIT_LIST_HEAD(&buf->vb.queue);
    }

    return 0;
}

static unsigned int rk29_camera_poll(struct file *file, poll_table *pt)
{
    struct soc_camera_file *icf = file->private_data;
    struct rk29_buffer *buf;

    buf = list_entry(icf->vb_vidq.stream.next, struct rk29_buffer,
                     vb.stream);

    poll_wait(file, &buf->vb.done, pt);

    if (buf->vb.state == VIDEOBUF_DONE ||
            buf->vb.state == VIDEOBUF_ERROR)
        return POLLIN|POLLRDNORM;

    return 0;
}

static int rk29_camera_querycap(struct soc_camera_host *ici,
                                struct v4l2_capability *cap)
{
    /* cap->name is set by the firendly caller:-> */
    strlcpy(cap->card, rk29_cam_driver_description, sizeof(cap->card));
    cap->version = RK29_CAM_VERSION_CODE;
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

    return 0;
}

static int rk29_camera_suspend(struct soc_camera_device *icd, pm_message_t state)
{
    struct soc_camera_host *ici =
                    to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
    int ret = 0,tmp;

	if ((pcdev->icd == icd) && (icd->ops->suspend)) {
		rk29_camera_s_stream(icd, 0);
		ret = icd->ops->suspend(icd, state);

		pcdev->reginfo_suspend.VipCtrl = read_vip_reg(RK29_VIP_CTRL);
		pcdev->reginfo_suspend.VipCrop = read_vip_reg(RK29_VIP_CROP);
		pcdev->reginfo_suspend.VipFs = read_vip_reg(RK29_VIP_FS);
		pcdev->reginfo_suspend.VipIntMsk = read_vip_reg(RK29_VIP_INT_MASK);
		pcdev->reginfo_suspend.VipCrm = read_vip_reg(RK29_VIP_CRM);

		tmp = pcdev->reginfo_suspend.VipFs>>16;		/* ddl@rock-chips.com */
		tmp += pcdev->reginfo_suspend.VipCrop>>16;
		pcdev->reginfo_suspend.VipFs = (pcdev->reginfo_suspend.VipFs & 0xffff) | (tmp<<16);

		pcdev->reginfo_suspend.Inval = Reg_Validate;
		rk29_camera_deactivate(pcdev);

		RK29CAMERA_DG("%s Enter Success...\n", __FUNCTION__);
	} else {
		RK29CAMERA_DG("%s icd has been deattach, don't need enter suspend\n", __FUNCTION__);
	}

    return ret;
}

static int rk29_camera_resume(struct soc_camera_device *icd)
{
    struct soc_camera_host *ici =
                    to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
    int ret = 0;

	if ((pcdev->icd == icd) && (icd->ops->resume)) {
		if (pcdev->reginfo_suspend.Inval == Reg_Validate) {
			rk29_camera_activate(pcdev, icd);
			write_vip_reg(RK29_VIP_INT_MASK, pcdev->reginfo_suspend.VipIntMsk);
			write_vip_reg(RK29_VIP_CRM, pcdev->reginfo_suspend.VipCrm);
			write_vip_reg(RK29_VIP_CTRL, pcdev->reginfo_suspend.VipCtrl&~ENABLE_CAPTURE);
			write_vip_reg(RK29_VIP_CROP, pcdev->reginfo_suspend.VipCrop);
			write_vip_reg(RK29_VIP_FS, pcdev->reginfo_suspend.VipFs);
			write_vip_reg(RK29_VIP_FB_SR, (VIP_F2_READY|VIP_F1_READY));

			if (pcdev->active1)
				rk29_videobuf_capture(pcdev->active1);

			if (pcdev->active2)
				rk29_videobuf_capture(pcdev->active2);

			rk29_camera_s_stream(icd, 1);
		}
		ret = icd->ops->resume(icd);

		RK29CAMERA_DG("%s Enter success\n",__FUNCTION__);
	} else {
		RK29CAMERA_DG("%s icd has been deattach, don't need enter resume\n", __FUNCTION__);
	}

    return ret;
}

static int rk29_camera_s_stream(struct soc_camera_device *icd, int enable)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
    int vip_ctrl_val;

	WARN_ON(pcdev->icd != icd);

	vip_ctrl_val = read_vip_reg(RK29_VIP_CTRL);
	if (enable) {
		vip_ctrl_val |= ENABLE_CAPTURE;
	} else {
        vip_ctrl_val &= ~ENABLE_CAPTURE;
	}
	write_vip_reg(RK29_VIP_CTRL, vip_ctrl_val);

	RK29CAMERA_DG("%s.. enable : %d\n", __FUNCTION__, enable);
	return 0;
}

static struct soc_camera_host_ops rk29_soc_camera_host_ops =
{
    .owner		= THIS_MODULE,
    .add		= rk29_camera_add_device,
    .remove		= rk29_camera_remove_device,
    .suspend	= rk29_camera_suspend,
    .resume		= rk29_camera_resume,
    .set_crop	= rk29_camera_set_crop,
    .get_formats	= rk29_camera_get_formats,
    .put_formats	= rk29_camera_put_formats,
    .set_fmt	= rk29_camera_set_fmt,
    .try_fmt	= rk29_camera_try_fmt,
    .init_videobuf	= rk29_camera_init_videobuf,
    .reqbufs	= rk29_camera_reqbufs,
    .poll		= rk29_camera_poll,
    .querycap	= rk29_camera_querycap,
    .set_bus_param	= rk29_camera_set_bus_param,
    .s_stream = rk29_camera_s_stream   /* ddl@rock-chips.com : Add stream control for host */
};
static int rk29_camera_probe(struct platform_device *pdev)
{
    struct rk29_camera_dev *pcdev;
    struct resource *res;
    int irq;
    int err = 0;

    RK29CAMERA_DG("\n%s..%s..%d  \n",__FUNCTION__,__FILE__,__LINE__);
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    irq = platform_get_irq(pdev, 0);
    if (!res || irq < 0) {
        err = -ENODEV;
        goto exit;
    }

    pcdev = kzalloc(sizeof(*pcdev), GFP_KERNEL);
    if (!pcdev) {
        dev_err(&pdev->dev, "Could not allocate pcdev\n");
        err = -ENOMEM;
        goto exit_alloc;
    }
    rk29_camdev_info_ptr = pcdev;

    /*config output clk*/
    pcdev->clk = clk_get(&pdev->dev, "vip_out");
    if (!pcdev->clk || IS_ERR(pcdev->clk))  {
        RK29CAMERA_TR(KERN_ERR "failed to get vip_clk source\n");
        err = -ENOENT;
        goto exit_eclkget;
    }

    dev_set_drvdata(&pdev->dev, pcdev);
    pcdev->res = res;

    pcdev->pdata = pdev->dev.platform_data;             /* ddl@rock-chips.com : Request IO in init function */
    if (pcdev->pdata && pcdev->pdata->io_init) {
        pcdev->pdata->io_init();
    }

    INIT_LIST_HEAD(&pcdev->capture);
    spin_lock_init(&pcdev->lock);

    /*
     * Request the regions.
     */
    if (!request_mem_region(res->start, res->end - res->start + 1,
                            RK29_CAM_DRV_NAME)) {
        err = -EBUSY;
        goto exit_reqmem;
    }

    pcdev->base = ioremap(res->start, res->end - res->start + 1);
    if (pcdev->base == NULL) {
        dev_err(pcdev->dev, "ioremap() of registers failed\n");
        err = -ENXIO;
        goto exit_ioremap;
    }

    pcdev->irq = irq;
    pcdev->dev = &pdev->dev;

    /* config buffer address */
    /* request irq */
    err = request_irq(pcdev->irq, rk29_camera_irq, 0, RK29_CAM_DRV_NAME,
                      pcdev);
    if (err) {
        dev_err(pcdev->dev, "Camera interrupt register failed \n");
        goto exit_reqirq;
    }

    pcdev->soc_host.drv_name	= RK29_CAM_DRV_NAME;
    pcdev->soc_host.ops		= &rk29_soc_camera_host_ops;
    pcdev->soc_host.priv		= pcdev;
    pcdev->soc_host.v4l2_dev.dev	= &pdev->dev;
    pcdev->soc_host.nr		= pdev->id;

    err = soc_camera_host_register(&pcdev->soc_host);
    if (err)
        goto exit_free_irq;

	hrtimer_init(&pcdev->frame_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    pcdev->frame_timer.function = rk29_camera_timer_func;

    RK29CAMERA_DG("\n%s..%s..%d  \n",__FUNCTION__,__FILE__,__LINE__);
    return 0;

exit_free_irq:
    free_irq(pcdev->irq, pcdev);
exit_reqirq:
    iounmap(pcdev->base);
exit_ioremap:
    release_mem_region(res->start, res->end - res->start + 1);
exit_reqmem:
    clk_put(pcdev->clk);
exit_eclkget:
    kfree(pcdev);
exit_alloc:
    rk29_camdev_info_ptr = NULL;
exit:
    return err;
}

static int __devexit rk29_camera_remove(struct platform_device *pdev)
{
    struct rk29_camera_dev *pcdev = platform_get_drvdata(pdev);
    struct resource *res;

    free_irq(pcdev->irq, pcdev);

    soc_camera_host_unregister(&pcdev->soc_host);

    res = pcdev->res;
    release_mem_region(res->start, res->end - res->start + 1);

    if (pcdev->pdata && pcdev->pdata->io_deinit) {         /* ddl@rock-chips.com : Free IO in deinit function */
        pcdev->pdata->io_deinit();
    }

    kfree(pcdev);
    rk29_camdev_info_ptr = NULL;
    dev_info(&pdev->dev, "RK28 Camera driver unloaded\n");

    return 0;
}

static struct platform_driver rk29_camera_driver =
{
    .driver 	= {
        .name	= RK29_CAM_DRV_NAME,
    },
    .probe		= rk29_camera_probe,
    .remove		= __devexit_p(rk29_camera_remove),
};


static int __devinit rk29_camera_init(void)
{
    RK29CAMERA_DG("\n%s..%s..%d  \n",__FUNCTION__,__FILE__,__LINE__);
    return platform_driver_register(&rk29_camera_driver);
}

static void __exit rk29_camera_exit(void)
{
    platform_driver_unregister(&rk29_camera_driver);
}

device_initcall_sync(rk29_camera_init);
module_exit(rk29_camera_exit);

MODULE_DESCRIPTION("RK29 Soc Camera Host driver");
MODULE_AUTHOR("ddl <ddl@rock-chips>");
MODULE_LICENSE("GPL");
