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

#include <mach/rk2818_camera.h>
#include <mach/rk2818_iomap.h>
#include <mach/iomux.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf-dma-contig.h>
#include <media/soc_camera.h>


#define RK28_VIP_AHBR_CTRL                0x00
#define RK28_VIP_INT_MASK                 0x04
#define RK28_VIP_INT_STS                  0x08
#define RK28_VIP_STS                      0x0c
#define RK28_VIP_CTRL                     0x10
#define RK28_VIP_CAPTURE_F1SA_Y           0x14
#define RK28_VIP_CAPTURE_F1SA_UV          0x18
#define RK28_VIP_CAPTURE_F1SA_Cr          0x1c
#define RK28_VIP_CAPTURE_F2SA_Y           0x20
#define RK28_VIP_CAPTURE_F2SA_UV          0x24
#define RK28_VIP_CAPTURE_F2SA_Cr          0x28
#define RK28_VIP_FB_SR                    0x2c
#define RK28_VIP_FS                       0x30
#define RK28_VIP_VIPRESERVED              0x34
#define RK28_VIP_CROP                     0x38
#define RK28_VIP_CRM                      0x3c
#define RK28_VIP_RESET                    0x40
#define RK28_VIP_L_SFT                    0x44

#define RK28_CPU_API_REG                  (RK2818_REGFILE_BASE+0x14)


//ctrl-------------------
#define  DISABLE_CAPTURE              0x00000000
#define  ENABLE_CAPTURE               0x00000001

#define  VSY_HIGH_ACTIVE              0x00000080
#define  VSY_LOW_ACTIVE               0x00000000

#define  HSY_HIGH_ACTIVE              0x00000000
#define  HSY_LOW_ACTIVE               0x00000002

#define  CCIR656                      0x00000000
#define  SENSOR                       0x00000004

#define  SENSOR_UYVY                  0x00000000
#define  SENSOR_YUYV                  0x00000008

#define  CON_OR_PIN                   0x00000000
#define  ONEFRAME                     0x00000020

#define  VIPREGYUV420                 0x00000000
#define  VIPREGYUV422                 0x00000040

#define  FIELD0_START                 0x00000000
#define  FIELD1_START                 0x00000080

#define  CONTINUOUS                   0x00000000
#define  PING_PONG                    0x00000100

#define  POSITIVE_EDGE                0x00000000
#define  NEGATIVE_EDGE                0x00000200

#define  VIPREGNTSC                   0x00000000
#define  VIPREGPAL                    0x00000400
//--------------------------
#define CONFIG_RK28CAMERA_TR      1
#define CONFIG_RK28CAMERA_DEBUG	  0
#if (CONFIG_RK28CAMERA_TR)
	#define RK28CAMERA_TR(format, ...)      printk(format, ## __VA_ARGS__)
	#if (CONFIG_RK28CAMERA_DEBUG)
	#define RK28CAMERA_DG(format, ...)      printk(format, ## __VA_ARGS__)
	#else
	#define RK28CAMERA_DG(format, ...)
	#endif
#else
	#define RK28CAMERA_TR(format, ...)
#endif

#define MIN(x,y)   ((x<y) ? x: y)
#define MAX(x,y)    ((x>y) ? x: y)
#define RK28_SENSOR_24MHZ      24           /* MHz */
#define RK28_SENSOR_48MHZ      48


#define RK28_CAM_VERSION_CODE KERNEL_VERSION(0, 0, 5)

/* limit to rk28 hardware capabilities */
#define RK28_CAM_BUS_PARAM   (SOCAM_MASTER |\
                SOCAM_HSYNC_ACTIVE_HIGH |\
                SOCAM_HSYNC_ACTIVE_LOW |\
                SOCAM_VSYNC_ACTIVE_HIGH |\
                SOCAM_VSYNC_ACTIVE_LOW |\
                SOCAM_PCLK_SAMPLE_RISING |\
                SOCAM_PCLK_SAMPLE_FALLING|\
                SOCAM_DATA_ACTIVE_HIGH |\
                SOCAM_DATA_ACTIVE_LOW|\
                SOCAM_DATAWIDTH_8 |SOCAM_MCLK_24MHZ |SOCAM_MCLK_48MHZ)

#define RK28_CAM_W_MIN      48
#define RK28_CAM_H_MIN       32
#define RK28_CAM_W_MAX      3856            /* ddl@rock-chips.com : 10M Pixel */
#define RK28_CAM_H_MAX      2764
#define RK28_CAM_FRAME_INVAL 3          /* ddl@rock-chips.com :  */

static DEFINE_MUTEX(camera_lock);


#define write_vip_reg(addr, val)  __raw_writel(val, addr+(rk28_camdev_info_ptr->base))
#define read_vip_reg(addr) __raw_readl(addr+(rk28_camdev_info_ptr->base))
#define mask_vip_reg addr, msk, val)    write_vip_reg(addr, (val)|((~(msk))&read_vip_reg(addr)))

#define set_vip_vsp(val)    __raw_writel(((val) | __raw_readl(RK28_CPU_API_REG)), RK28_CPU_API_REG)

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
struct rk28_buffer
{
    /* common v4l buffer stuff -- must be first */
    struct videobuf_buffer vb;
    const struct soc_camera_data_format        *fmt;
    int			inwork;
};

struct rk28_camera_dev
{
    struct soc_camera_host	soc_host;
    struct device		*dev;
    /* RK2827x is only supposed to handle one camera on its Quick Capture
     * interface. If anyone ever builds hardware to enable more than
     * one camera, they will have to modify this driver too */
    struct soc_camera_device *icd;
    struct clk *clk;
    void __iomem *base;
    unsigned int frame_inval;           /* ddl@rock-chips.com : The first frames is invalidate  */
    unsigned int irq;

    struct rk28camera_platform_data *pdata;
    struct resource		*res;

    struct list_head	capture;

    spinlock_t		lock;

    struct videobuf_buffer	*active;
	struct videobuf_queue *vb_vidq_ptr;
};

static const char *rk28_cam_driver_description = "RK28_Camera";
static struct rk28_camera_dev *rk28_camdev_info_ptr;
/*
 *  Videobuf operations
 */
static int rk28_videobuf_setup(struct videobuf_queue *vq, unsigned int *count,
                               unsigned int *size)
{
    struct soc_camera_device *icd = vq->priv_data;
    int bytes_per_pixel = (icd->current_fmt->depth + 7) >> 3;

    dev_dbg(&icd->dev, "count=%d, size=%d\n", *count, *size);

    /* planar capture requires Y, U and V buffers to be page aligned */
    *size = PAGE_ALIGN( icd->user_width * icd->user_height * bytes_per_pixel);                               /* Y pages UV pages, yuv422*/

    RK28CAMERA_DG("\n%s..%d.. size = %d\n",__FUNCTION__,__LINE__, *size);

    return 0;
}
static void rk28_videobuf_free(struct videobuf_queue *vq, struct rk28_buffer *buf)
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

}
static int rk28_videobuf_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb, enum v4l2_field field)
{
    struct soc_camera_device *icd = vq->priv_data;
    struct rk28_buffer *buf;
    int ret;

    buf = container_of(vb, struct rk28_buffer, vb);

    dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
            vb, vb->baddr, vb->bsize);

    //RK28CAMERA_TR("\n%s..%d..  \n",__FUNCTION__,__LINE__);

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
    //RK28CAMERA_TR("\n%s..%d.. \n",__FUNCTION__,__LINE__);
    return 0;
fail:
    rk28_videobuf_free(vq, buf);
out:
    return ret;
}

static inline void rk28_videobuf_capture(struct videobuf_buffer *vb)
{
    unsigned int size;

    if (vb) {
        size = vb->width * vb->height; /* Y pages UV pages, yuv422*/
        write_vip_reg(RK28_VIP_CAPTURE_F1SA_Y, vb->boff);
        write_vip_reg(RK28_VIP_CAPTURE_F1SA_UV, vb->boff + size);
        write_vip_reg(RK28_VIP_CAPTURE_F2SA_Y, vb->boff);
        write_vip_reg(RK28_VIP_CAPTURE_F2SA_UV, vb->boff + size);
        write_vip_reg(RK28_VIP_FB_SR,  0x00000002);//frame1 has been ready to receive data,frame 2 is not used
    }
}

static void rk28_videobuf_queue(struct videobuf_queue *vq,
                                struct videobuf_buffer *vb)
{
    struct soc_camera_device *icd = vq->priv_data;
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk28_camera_dev *pcdev = ici->priv;
    unsigned long flags;

    dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
            vb, vb->baddr, vb->bsize);

    vb->state = VIDEOBUF_ACTIVE;
    spin_lock_irqsave(&pcdev->lock, flags);
	if (!list_empty(&pcdev->capture)) {
		list_add_tail(&vb->queue, &pcdev->capture);
	} else {
		if (list_entry(pcdev->capture.next, struct videobuf_buffer, queue) != vb)
			list_add_tail(&vb->queue, &pcdev->capture);
		else
			BUG();    /* ddl@rock-chips.com : The same videobuffer queue again */
	}

    if (!pcdev->active) {
        pcdev->active = vb;
        rk28_videobuf_capture(vb);
    }
    spin_unlock_irqrestore(&pcdev->lock, flags);
}

static irqreturn_t rk28_camera_irq(int irq, void *data)
{
    struct rk28_camera_dev *pcdev = data;
    struct videobuf_buffer *vb;

    read_vip_reg(RK28_VIP_INT_STS);    /* clear vip interrupte single  */

    /* ddl@rock-chps.com : Current VIP is run in One Frame Mode, Frame 1 is validate */
    if (read_vip_reg(RK28_VIP_FB_SR) & 0x01) {

		if (!pcdev->active)
			goto RK28_CAMERA_IRQ_END;

        if ((pcdev->frame_inval>0) && (pcdev->frame_inval<=RK28_CAM_FRAME_INVAL)) {
            pcdev->frame_inval--;
            rk28_videobuf_capture(pcdev->active);
            goto RK28_CAMERA_IRQ_END;
        } else if (pcdev->frame_inval) {
        	printk("frame_inval : %0x",pcdev->frame_inval);
            pcdev->frame_inval = 0;
        }

        vb = pcdev->active;
        list_del_init(&vb->queue);

        if (!list_empty(&pcdev->capture)) {
              pcdev->active = list_entry(pcdev->capture.next, struct videobuf_buffer, queue);
        } else {
            pcdev->active = NULL;
        }

        rk28_videobuf_capture(pcdev->active);

        vb->state = VIDEOBUF_DONE;
        do_gettimeofday(&vb->ts);
        vb->field_count++;
        wake_up(&vb->done);
    }

RK28_CAMERA_IRQ_END:
    return IRQ_HANDLED;
}


static void rk28_videobuf_release(struct videobuf_queue *vq,
                                  struct videobuf_buffer *vb)
{
    struct rk28_buffer *buf = container_of(vb, struct rk28_buffer, vb);
#ifdef DEBUG
    struct soc_camera_device *icd = vq->priv_data;

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

    rk28_videobuf_free(vq, buf);
}

static struct videobuf_queue_ops rk28_videobuf_ops =
{
    .buf_setup      = rk28_videobuf_setup,
    .buf_prepare    = rk28_videobuf_prepare,
    .buf_queue      = rk28_videobuf_queue,
    .buf_release    = rk28_videobuf_release,
};

static void rk28_camera_init_videobuf(struct videobuf_queue *q,
                                      struct soc_camera_device *icd)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk28_camera_dev *pcdev = ici->priv;

    /* We must pass NULL as dev pointer, then all pci_* dma operations
     * transform to normal dma_* ones. */
    videobuf_queue_dma_contig_init(q,
                                   &rk28_videobuf_ops,
                                   ici->v4l2_dev.dev, &pcdev->lock,
                                   V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                   V4L2_FIELD_NONE,
                                   sizeof(struct rk28_buffer),
                                   icd);
	pcdev->vb_vidq_ptr = q;		/* ddl@rock-chips.com */
}
static int rk28_camera_activate(struct rk28_camera_dev *pcdev, struct soc_camera_device *icd)
{
    unsigned long sensor_bus_flags = SOCAM_MCLK_24MHZ;
    struct clk *parent;

    RK28CAMERA_DG("\n%s..%d.. \n",__FUNCTION__,__LINE__);
    if (!pcdev->clk || IS_ERR(pcdev->clk))
        RK28CAMERA_TR(KERN_ERR "failed to get vip_clk source\n");

    //if (icd->ops->query_bus_param)                                                  /* ddl@rock-chips.com : Query Sensor's xclk */
        //sensor_bus_flags = icd->ops->query_bus_param(icd);

    if (sensor_bus_flags & SOCAM_MCLK_48MHZ) {
        parent = clk_get(NULL, "clk48m");
        if (!parent || IS_ERR(parent))
             goto RK28_CAMERA_ACTIVE_ERR;
    } else if (sensor_bus_flags & SOCAM_MCLK_27MHZ) {
        parent = clk_get(NULL, "extclk");
        if (!parent || IS_ERR(parent))
             goto RK28_CAMERA_ACTIVE_ERR;
    } else {
        parent = clk_get(NULL, "xin24m");
        if (!parent || IS_ERR(parent))
             goto RK28_CAMERA_ACTIVE_ERR;
    }

    clk_set_parent(pcdev->clk, parent);

    clk_enable(pcdev->clk);
    rk2818_mux_api_set(GPIOF6_VIPCLK_SEL_NAME, IOMUXB_VIP_CLKOUT);
    ndelay(10);

    write_vip_reg(RK28_VIP_RESET, 0x76543210);  /* ddl@rock-chips.com : vip software reset */
    udelay(10);

    write_vip_reg(RK28_VIP_AHBR_CTRL, 0x07);   /* ddl@rock-chips.com : vip ahb burst 16 */
    write_vip_reg(RK28_VIP_INT_MASK, 0x01);                    //capture complete interrupt enable
    write_vip_reg(RK28_VIP_CRM,  0x00000000);               //Y/CB/CR color modification

    return 0;
RK28_CAMERA_ACTIVE_ERR:
    return -ENODEV;
}

static void rk28_camera_deactivate(struct rk28_camera_dev *pcdev)
{
    pcdev->active = NULL;

    write_vip_reg(RK28_VIP_CTRL, 0);
    read_vip_reg(RK28_VIP_INT_STS);             //clear vip interrupte single

    rk2818_mux_api_set(GPIOF6_VIPCLK_SEL_NAME, IOMUXB_GPIO1_B6);
    clk_disable(pcdev->clk);

    return;
}

/* The following two functions absolutely depend on the fact, that
 * there can be only one camera on RK28 quick capture interface */
static int rk28_camera_add_device(struct soc_camera_device *icd)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk28_camera_dev *pcdev = ici->priv;
    struct device *control = to_soc_camera_control(icd);
    struct v4l2_subdev *sd;
    int ret;

    mutex_lock(&camera_lock);

    if (pcdev->icd) {
        ret = -EBUSY;
        goto ebusy;
    }

    dev_info(&icd->dev, "RK28 Camera driver attached to camera %d\n",
             icd->devnum);

	pcdev->frame_inval = RK28_CAM_FRAME_INVAL;
    pcdev->active = NULL;
    pcdev->icd = NULL;
	/* ddl@rock-chips.com: capture list must be reset, because this list may be not empty,
     * if app havn't dequeue all videobuf before close camera device;
	*/
    INIT_LIST_HEAD(&pcdev->capture);

    ret = rk28_camera_activate(pcdev,icd);
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
static void rk28_camera_remove_device(struct soc_camera_device *icd)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk28_camera_dev *pcdev = ici->priv;

    BUG_ON(icd != pcdev->icd);

    dev_info(&icd->dev, "RK28 Camera driver detached from camera %d\n",
             icd->devnum);

	rk28_camera_deactivate(pcdev);

	/* ddl@rock-chips.com: Call videobuf_mmap_free here for free the struct video_buffer which malloc in videobuf_alloc */
	if (pcdev->vb_vidq_ptr) {
		videobuf_mmap_free(pcdev->vb_vidq_ptr);
		pcdev->vb_vidq_ptr = NULL;
	}

	pcdev->active = NULL;
    pcdev->icd = NULL;
	/* ddl@rock-chips.com: capture list must be reset, because this list may be not empty,
     * if app havn't dequeue all videobuf before close camera device;
	*/
    INIT_LIST_HEAD(&pcdev->capture);

	return;
}

static int rk28_camera_set_bus_param(struct soc_camera_device *icd, __u32 pixfmt)
{
    unsigned long bus_flags, camera_flags, common_flags;
    unsigned int vip_ctrl_val = 0;
    int ret = 0;

    RK28CAMERA_DG("\n%s..%d..\n",__FUNCTION__,__LINE__);

    bus_flags = RK28_CAM_BUS_PARAM;

    camera_flags = icd->ops->query_bus_param(icd);
    common_flags = soc_camera_bus_param_compatible(camera_flags, bus_flags);
    if (!common_flags) {
        ret = -EINVAL;
        goto RK28_CAMERA_SET_BUS_PARAM_END;
    }

    ret = icd->ops->set_bus_param(icd, common_flags);
    if (ret < 0)
        goto RK28_CAMERA_SET_BUS_PARAM_END;

    icd->buswidth = 8;

    vip_ctrl_val = read_vip_reg(RK28_VIP_CTRL);
    if (common_flags & SOCAM_PCLK_SAMPLE_FALLING)
        vip_ctrl_val |= NEGATIVE_EDGE;
    if (common_flags & SOCAM_HSYNC_ACTIVE_LOW)
        vip_ctrl_val |= HSY_LOW_ACTIVE;
    if (common_flags & SOCAM_VSYNC_ACTIVE_HIGH)
        set_vip_vsp(VSY_HIGH_ACTIVE);

    /* ddl@rock-chips.com : Don't enable capture here, enable in stream_on */
    //vip_ctrl_val |= ENABLE_CAPTURE;

    write_vip_reg(RK28_VIP_CTRL, vip_ctrl_val);
    RK28CAMERA_DG("\n%s..CtrReg=%x  \n",__FUNCTION__,read_vip_reg(RK28_VIP_CTRL));

RK28_CAMERA_SET_BUS_PARAM_END:
	if (ret)
    	RK28CAMERA_TR("\n%s..%d.. ret = %d \n",__FUNCTION__,__LINE__, ret);
    return ret;
}

static int rk28_camera_try_bus_param(struct soc_camera_device *icd, __u32 pixfmt)
{
    unsigned long bus_flags, camera_flags;
    int ret;

    bus_flags = RK28_CAM_BUS_PARAM;

    camera_flags = icd->ops->query_bus_param(icd);
    ret = soc_camera_bus_param_compatible(camera_flags, bus_flags) ;

    if (ret < 0)
        dev_warn(icd->dev.parent,
			 "Flags incompatible: camera %lx, host %lx\n",
			 camera_flags, bus_flags);
    return ret;
}
static const struct soc_camera_data_format rk28_camera_formats[] = {
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
	},
};

static void rk28_camera_setup_format(struct soc_camera_device *icd, __u32 host_pixfmt, __u32 cam_pixfmt, struct v4l2_rect *rect)
{
    unsigned int vip_fs,vip_crop;
    unsigned int vip_ctrl_val = SENSOR | ONEFRAME |DISABLE_CAPTURE;

    switch (host_pixfmt)
    {
        case V4L2_PIX_FMT_YUV422P:
            vip_ctrl_val |= VIPREGYUV422;
            break;
        case V4L2_PIX_FMT_YUV420:
            vip_ctrl_val |= VIPREGYUV420;
            break;
        default:                                                                                /* ddl@rock-chips.com : vip output format is hold when pixfmt is invalidate */
            vip_ctrl_val |= (read_vip_reg(RK28_VIP_CTRL) & VIPREGYUV422);
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
            vip_ctrl_val |= (read_vip_reg(RK28_VIP_CTRL) & SENSOR_YUYV);
            break;
    }

    write_vip_reg(RK28_VIP_CTRL, vip_ctrl_val);         /* ddl@rock-chips.com: VIP capture mode and capture format must be set before FS register set */

    read_vip_reg(RK28_VIP_INT_STS);                              /* clear vip interrupte single  */

    if (vip_ctrl_val & ONEFRAME)  {
        vip_crop = ((rect->left<<16) + rect->top);
        vip_fs  = (((rect->width + rect->left)<<16) + (rect->height+rect->top));
    } else if (vip_ctrl_val & PING_PONG) {
        WARN_ON(rect->left ||rect->top );
		RK28CAMERA_DG("\n %s..PingPang not support Crop \n",__FUNCTION__);
		return;
    }

    write_vip_reg(RK28_VIP_CROP, vip_crop);
    write_vip_reg(RK28_VIP_FS, vip_fs);

    write_vip_reg(RK28_VIP_FB_SR,  0x00000003);

    RK28CAMERA_DG("\n%s.. crop:%x .. fs : %x\n",__FUNCTION__,vip_crop,vip_fs);
}

static int rk28_camera_get_formats(struct soc_camera_device *icd, int idx,
				  struct soc_camera_format_xlate *xlate)
{
    struct device *dev = icd->dev.parent;
    int formats = 0, buswidth, ret;

    buswidth = 8;

    ret = rk28_camera_try_bus_param(icd, buswidth);
    if (ret < 0)
        return 0;

    switch (icd->formats[idx].fourcc) {
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_YUYV:
            formats++;
            if (xlate) {
                xlate->host_fmt = &rk28_camera_formats[0];
                xlate->cam_fmt = icd->formats + idx;
                xlate->buswidth = buswidth;
                xlate++;
                dev_dbg(dev, "Providing format %s using %s\n",
                	rk28_camera_formats[0].name,
                	icd->formats[idx].name);
            }

            formats++;
            if (xlate) {
                xlate->host_fmt = &rk28_camera_formats[1];
                xlate->cam_fmt = icd->formats + idx;
                xlate->buswidth = buswidth;
                xlate++;
                dev_dbg(dev, "Providing format %s using %s\n",
                	rk28_camera_formats[1].name,
                	icd->formats[idx].name);
            }
        default:
            break;
    }

    return formats;
}

static void rk28_camera_put_formats(struct soc_camera_device *icd)
{
	return;
}

static int rk28_camera_set_crop(struct soc_camera_device *icd,
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

        v4l_bound_align_image(&pix->width, 48, 2048, 1,
            &pix->height, 32, 2048, 0,
            icd->current_fmt->fourcc == V4L2_PIX_FMT_YUV422P ?4 : 0);

        ret = v4l2_subdev_call(sd, video, s_fmt, &f);
        if (ret < 0)
            return ret;
    }

    rk28_camera_setup_format(icd, icd->current_fmt->fourcc, pix->pixelformat, &a->c);

    icd->user_width = pix->width;
    icd->user_height = pix->height;

    return 0;
}

static int rk28_camera_set_fmt(struct soc_camera_device *icd,
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

    RK28CAMERA_DG("\n%s..%d..  \n",__FUNCTION__,__LINE__);

    xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
    if (!xlate) {
        dev_err(dev, "Format %x not found\n", pix->pixelformat);
        ret = -EINVAL;
        goto RK28_CAMERA_SET_FMT_END;
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

        RK28CAMERA_DG("\n%s..%s..%s \n",__FUNCTION__,xlate->host_fmt->name, cam_fmt->name);
        rk28_camera_setup_format(icd, pix->pixelformat, cam_fmt->fourcc, &rect);

        icd->buswidth = xlate->buswidth;
        icd->current_fmt = xlate->host_fmt;
    }

RK28_CAMERA_SET_FMT_END:
	if (ret)
    	RK28CAMERA_TR("\n%s..%d.. ret = %d  \n",__FUNCTION__,__LINE__, ret);
    return ret;
}

static int rk28_camera_try_fmt(struct soc_camera_device *icd,
                                   struct v4l2_format *f)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
    const struct soc_camera_format_xlate *xlate;
    struct v4l2_pix_format *pix = &f->fmt.pix;
    __u32 pixfmt = pix->pixelformat;
    enum v4l2_field field;
    int ret;

    RK28CAMERA_DG("\n%s..%d.. \n",__FUNCTION__,__LINE__);

    xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
    if (!xlate) {
        dev_err(ici->v4l2_dev.dev, "Format %x not found\n", pixfmt);
        ret = -EINVAL;
        goto RK28_CAMERA_TRY_FMT_END;
    }
   /* limit to rk28 hardware capabilities */
    v4l_bound_align_image(&pix->width, RK28_CAM_W_MIN, RK28_CAM_W_MAX, 1,
    	      &pix->height, RK28_CAM_H_MIN, RK28_CAM_H_MAX, 0,
    	      pixfmt == V4L2_PIX_FMT_YUV422P ? 4 : 0);

    pix->bytesperline = pix->width *
                                                DIV_ROUND_UP(xlate->host_fmt->depth, 8);
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
        goto RK28_CAMERA_TRY_FMT_END;
    }

RK28_CAMERA_TRY_FMT_END:
	if (ret)
    	RK28CAMERA_TR("\n%s..%d.. ret = %d  \n",__FUNCTION__,__LINE__, ret);
    return ret;
}

static int rk28_camera_reqbufs(struct soc_camera_file *icf,
                               struct v4l2_requestbuffers *p)
{
    int i;

    /* This is for locking debugging only. I removed spinlocks and now I
     * check whether .prepare is ever called on a linked buffer, or whether
     * a dma IRQ can occur for an in-work or unlinked buffer. Until now
     * it hadn't triggered */
    for (i = 0; i < p->count; i++) {
        struct rk28_buffer *buf = container_of(icf->vb_vidq.bufs[i],
                                                           struct rk28_buffer, vb);
        buf->inwork = 0;
        INIT_LIST_HEAD(&buf->vb.queue);
    }

    return 0;
}

static unsigned int rk28_camera_poll(struct file *file, poll_table *pt)
{
    struct soc_camera_file *icf = file->private_data;
    struct rk28_buffer *buf;

    buf = list_entry(icf->vb_vidq.stream.next, struct rk28_buffer,
                     vb.stream);

    poll_wait(file, &buf->vb.done, pt);

    if (buf->vb.state == VIDEOBUF_DONE ||
            buf->vb.state == VIDEOBUF_ERROR)
        return POLLIN|POLLRDNORM;

    return 0;
}

static int rk28_camera_querycap(struct soc_camera_host *ici,
                                struct v4l2_capability *cap)
{
    /* cap->name is set by the firendly caller:-> */
    strlcpy(cap->card, rk28_cam_driver_description, sizeof(cap->card));
    cap->version = RK28_CAM_VERSION_CODE;
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

    return 0;
}

static int rk28_camera_suspend(struct soc_camera_device *icd, pm_message_t state)
{
    struct soc_camera_host *ici =
                    to_soc_camera_host(icd->dev.parent);
    struct rk28_camera_dev *pcdev = ici->priv;
    int ret = 0;

    if ((pcdev->icd) && (pcdev->icd->ops->suspend))
        ret = pcdev->icd->ops->suspend(pcdev->icd, state);

    return ret;
}

static int rk28_camera_resume(struct soc_camera_device *icd)
{
    struct soc_camera_host *ici =
                    to_soc_camera_host(icd->dev.parent);
    struct rk28_camera_dev *pcdev = ici->priv;
    int ret = 0;

    if ((pcdev->icd) && (pcdev->icd->ops->resume))
        ret = pcdev->icd->ops->resume(pcdev->icd);

    return ret;
}

static int rk28_camera_s_stream(struct soc_camera_device *icd, int enable)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk28_camera_dev *pcdev = ici->priv;
    int vip_ctrl_val;

	WARN_ON(pcdev->icd != icd);

	vip_ctrl_val = read_vip_reg(RK28_VIP_CTRL);
	if (enable) {
		vip_ctrl_val |= ENABLE_CAPTURE;
	} else {
        vip_ctrl_val &= ~ENABLE_CAPTURE;
	}
	write_vip_reg(RK28_VIP_CTRL, vip_ctrl_val);

	RK28CAMERA_DG("%s.. enable : %d\n", __FUNCTION__, enable);
	return 0;
}

static struct soc_camera_host_ops rk28_soc_camera_host_ops =
{
    .owner		= THIS_MODULE,
    .add		= rk28_camera_add_device,
    .remove		= rk28_camera_remove_device,
    .suspend	= rk28_camera_suspend,
    .resume		= rk28_camera_resume,
    .set_crop	= rk28_camera_set_crop,
    .get_formats	= rk28_camera_get_formats,
    .put_formats	= rk28_camera_put_formats,
    .set_fmt	= rk28_camera_set_fmt,
    .try_fmt	= rk28_camera_try_fmt,
    .init_videobuf	= rk28_camera_init_videobuf,
    .reqbufs	= rk28_camera_reqbufs,
    .poll		= rk28_camera_poll,
    .querycap	= rk28_camera_querycap,
    .set_bus_param	= rk28_camera_set_bus_param,
    .s_stream = rk28_camera_s_stream   /* ddl@rock-chips.com : Add stream control for host */
};
static int rk28_camera_probe(struct platform_device *pdev)
{
    struct rk28_camera_dev *pcdev;
    struct resource *res;
    int irq;
    int err = 0;

    RK28CAMERA_DG("\n%s..%s..%d  \n",__FUNCTION__,__FILE__,__LINE__);
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
    rk28_camdev_info_ptr = pcdev;

    /*config output clk*/
    pcdev->clk = clk_get(&pdev->dev, "vip");
    if (!pcdev->clk || IS_ERR(pcdev->clk))  {
        RK28CAMERA_TR(KERN_ERR "failed to get vip_clk source\n");
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
                            RK28_CAM_DRV_NAME)) {
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
    err = request_irq(pcdev->irq, rk28_camera_irq, 0, RK28_CAM_DRV_NAME,
                      pcdev);
    if (err) {
        dev_err(pcdev->dev, "Camera interrupt register failed \n");
        goto exit_reqirq;
    }

    pcdev->soc_host.drv_name	= RK28_CAM_DRV_NAME;
    pcdev->soc_host.ops		= &rk28_soc_camera_host_ops;
    pcdev->soc_host.priv		= pcdev;
    pcdev->soc_host.v4l2_dev.dev	= &pdev->dev;
    pcdev->soc_host.nr		= pdev->id;

    err = soc_camera_host_register(&pcdev->soc_host);
    if (err)
        goto exit_free_irq;

    RK28CAMERA_DG("\n%s..%s..%d  \n",__FUNCTION__,__FILE__,__LINE__);
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
    rk28_camdev_info_ptr = NULL;
exit:
    return err;
}

static int __devexit rk28_camera_remove(struct platform_device *pdev)
{
    struct rk28_camera_dev *pcdev = platform_get_drvdata(pdev);
    struct resource *res;

    free_irq(pcdev->irq, pcdev);

    soc_camera_host_unregister(&pcdev->soc_host);

    res = pcdev->res;
    release_mem_region(res->start, res->end - res->start + 1);

    if (pcdev->pdata && pcdev->pdata->io_deinit) {         /* ddl@rock-chips.com : Free IO in deinit function */
        pcdev->pdata->io_deinit();
    }

    kfree(pcdev);
    rk28_camdev_info_ptr = NULL;
    dev_info(&pdev->dev, "RK28 Camera driver unloaded\n");

    return 0;
}

static struct platform_driver rk28_camera_driver =
{
    .driver 	= {
        .name	= RK28_CAM_DRV_NAME,
    },
    .probe		= rk28_camera_probe,
    .remove		= __devexit_p(rk28_camera_remove),
};


static int __devinit rk28_camera_init(void)
{
    RK28CAMERA_DG("\n%s..%s..%d  \n",__FUNCTION__,__FILE__,__LINE__);
    return platform_driver_register(&rk28_camera_driver);
}

static void __exit rk28_camera_exit(void)
{
    platform_driver_unregister(&rk28_camera_driver);
}

//module_init(rk28_camera_init);
device_initcall_sync(rk28_camera_init);
module_exit(rk28_camera_exit);

MODULE_DESCRIPTION("RK2818 Soc Camera Host driver");
MODULE_AUTHOR("ddl <ddl@rock-chips>");
MODULE_LICENSE("GPL");
