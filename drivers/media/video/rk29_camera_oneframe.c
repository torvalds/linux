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
#ifdef CONFIG_ARCH_RK29
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
#include <media/soc_mediabus.h>
#include <mach/rk29-ipp.h>


static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR|S_IWGRP);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING"rk29xx_camera: " fmt , ## arg); } while (0)

#define RK29CAMERA_TR(format, ...) printk(KERN_ERR format, ## __VA_ARGS__)
#define RK29CAMERA_DG(format, ...) dprintk(1, format, ## __VA_ARGS__)

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

#ifdef CONFIG_VIDEO_RK29_DIGITALZOOM_IPP_OFF
#define CAM_WORKQUEUE_IS_EN()   ((pcdev->host_width != pcdev->icd->user_width) || (pcdev->host_height != pcdev->icd->user_height)\
                                  || (pcdev->icd_cb.sensor_cb))
#define CAM_IPPWORK_IS_EN()     ((pcdev->host_width != pcdev->icd->user_width) || (pcdev->host_height != pcdev->icd->user_height))                                  
#else
#define CAM_WORKQUEUE_IS_EN()   (true)
#define CAM_IPPWORK_IS_EN()     ((pcdev->zoominfo.a.c.width != pcdev->icd->user_width) || (pcdev->zoominfo.a.c.height != pcdev->icd->user_height))
#endif
//Configure Macro
/*
*            Driver Version Note
*
*v0.0.x : this driver is 2.6.32 kernel driver;
*v0.1.x : this driver is 3.0.8 kernel driver;
*
*v0.x.1 : this driver first support rk2918;
*v0.x.2 : fix this driver support v4l2 format is V4L2_PIX_FMT_NV12 and V4L2_PIX_FMT_NV16,is not V4L2_PIX_FMT_YUV420 
*         and V4L2_PIX_FMT_YUV422P;
*v0.x.3 : this driver support VIDIOC_ENUM_FRAMEINTERVALS;
*v0.x.4 : this driver support digital zoom;
*v0.x.5 : this driver support test framerate and query framerate from board file configuration;
*v0.x.6 : this driver improve test framerate method;
*v0.x.7 : this driver product resolution by IPP crop and scale, which user request but sensor can't support;
*         note: this version is only provide yifang client, which is not official version;
*v0.x.8 : this driver and rk29_camera.c support upto 3 back-sensors and upto 3 front-sensors;
*v0.x.9 : camera io code is compatible for rk29xx and rk30xx
*v0.x.a : fix error when calculate crop left-top point coordinate;
*         note: this version provided as patch camera_patch_v1.1
*v0.x.b : fix sensor autofocus thread may in running when reinit sensor if sensor haven't stream on in first init;
*v0.x.c : fix work queue havn't been finished after close device;
*v0.x.d : fix error when calculate crop left-top point coordinate;
*/
#define RK29_CAM_VERSION_CODE KERNEL_VERSION(0, 1, 0xd)

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
#define RK29_CAM_FRAME_INVAL_DC 3          /* ddl@rock-chips.com :  */
#define RK29_CAM_FRAME_MEASURE  5

#define RK29_CAM_AXI   0
#define RK29_CAM_AHB   1
#define CONFIG_RK29_CAM_WORK_BUS    RK29_CAM_AXI

extern void videobuf_dma_contig_free(struct videobuf_queue *q, struct videobuf_buffer *buf);
extern dma_addr_t videobuf_to_dma_contig(struct videobuf_buffer *buf);

/* buffer for one video frame */
struct rk29_buffer
{
    /* common v4l buffer stuff -- must be first */
    struct videobuf_buffer vb;
    enum v4l2_mbus_pixelcode	code;
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
struct rk29_camera_work
{
	struct videobuf_buffer *vb;
	struct rk29_camera_dev *pcdev;
	struct work_struct work;
};
struct rk29_camera_frmivalenum
{
    struct v4l2_frmivalenum fival;
    struct rk29_camera_frmivalenum *nxt;
};
struct rk29_camera_frmivalinfo
{
    struct soc_camera_device *icd;
    struct rk29_camera_frmivalenum *fival_list;
};
struct rk29_camera_zoominfo
{
    struct semaphore sem;
    struct v4l2_crop a;
    int zoom_rate;
};
struct rk29_camera_dev
{
    struct soc_camera_host	soc_host;
    struct device		*dev;
    /* RK2827x is only supposed to handle one camera on its Quick Capture
     * interface. If anyone ever builds hardware to enable more than
     * one camera, they will have to modify this driver too */
    struct soc_camera_device *icd;

	struct clk *aclk_ddr_lcdc;
	struct clk *aclk_disp_matrix;

	struct clk *hclk_cpu_display;
	struct clk *vip_slave;

	struct clk *vip_out;
	struct clk *vip_input;
	struct clk *vip_bus;

	struct clk *hclk_disp_matrix;
	struct clk *vip_matrix;

	struct clk *pd_display;

    void __iomem *base;
	void __iomem *grf_base;
    int frame_inval;           /* ddl@rock-chips.com : The first frames is invalidate  */
    unsigned int irq;
	unsigned int fps;
    unsigned long frame_interval;
	unsigned int pixfmt;
	unsigned int vipmem_phybase;
	unsigned int vipmem_size;
	unsigned int vipmem_bsize;
	int host_width;
	int host_height;
    int icd_width;
    int icd_height;

    struct rk29camera_platform_data *pdata;
    struct resource		*res;

    struct list_head	capture;
    struct rk29_camera_zoominfo zoominfo;
    
    spinlock_t		lock;

    struct videobuf_buffer	*active;
	struct rk29_camera_reg reginfo_suspend;
	struct workqueue_struct *camera_wq;
	struct rk29_camera_work *camera_work;
	unsigned int camera_work_count;
	struct hrtimer fps_timer;
	struct work_struct camera_reinit_work;
    int icd_init;
    rk29_camera_sensor_cb_s icd_cb;
    struct rk29_camera_frmivalinfo icd_frmival[2];
};

static const struct v4l2_queryctrl rk29_camera_controls[] =
{
	#ifdef CONFIG_VIDEO_RK29_DIGITALZOOM_IPP_ON
    {
        .id		= V4L2_CID_ZOOM_ABSOLUTE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "DigitalZoom Control",
        .minimum	= 100,
        .maximum	= 300,
        .step		= 5,
        .default_value = 100,
    },
    #endif
};

static DEFINE_MUTEX(camera_lock);
static const char *rk29_cam_driver_description = "RK29_Camera";
static struct rk29_camera_dev *rk29_camdev_info_ptr;

static int rk29_camera_s_stream(struct soc_camera_device *icd, int enable);


/*
 *  Videobuf operations
 */
static int rk29_videobuf_setup(struct videobuf_queue *vq, unsigned int *count,
                               unsigned int *size)
{
    struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
    int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);
    int bytes_per_line_host = soc_mbus_bytes_per_line(pcdev->host_width,
						icd->current_fmt->host_fmt);

    dev_dbg(&icd->dev, "count=%d, size=%d\n", *count, *size);

	if (bytes_per_line < 0)
		return bytes_per_line;

	/* planar capture requires Y, U and V buffers to be page aligned */
	*size = PAGE_ALIGN(bytes_per_line*icd->user_height);       /* Y pages UV pages, yuv422*/
	pcdev->vipmem_bsize = PAGE_ALIGN(bytes_per_line_host * pcdev->host_height);


	if (CAM_WORKQUEUE_IS_EN()) {
        #ifdef CONFIG_VIDEO_RK29_DIGITALZOOM_IPP_OFF
        if (CAM_IPPWORK_IS_EN()) 
        #endif
        {
            BUG_ON(pcdev->vipmem_size<pcdev->vipmem_bsize);
    		if (*count > pcdev->vipmem_size/pcdev->vipmem_bsize) {    /* Buffers must be limited, when this resolution is genered by IPP */
    			*count = pcdev->vipmem_size/pcdev->vipmem_bsize;
    		}
        }
		if ((pcdev->camera_work_count != *count) && pcdev->camera_work) {
			kfree(pcdev->camera_work);
			pcdev->camera_work = NULL;
			pcdev->camera_work_count = 0;
		}

		if (pcdev->camera_work == NULL) {
			pcdev->camera_work = kmalloc(sizeof(struct rk29_camera_work)*(*count), GFP_KERNEL);
			if (pcdev->camera_work == NULL) {
				RK29CAMERA_TR("\n %s kmalloc fail\n", __FUNCTION__);
				BUG();
			}
			pcdev->camera_work_count = *count;
		}
	}

    RK29CAMERA_DG("%s..%d.. videobuf size:%d, vipmem_buf size:%d, count:%d \n",__FUNCTION__,__LINE__, *size,pcdev->vipmem_size, *count);

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
    /*
	 * This waits until this buffer is out of danger, i.e., until it is no
	 * longer in STATE_QUEUED or STATE_ACTIVE
	 */
	//videobuf_waiton(vq, &buf->vb, 0, 0);
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
    int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);
	if (bytes_per_line < 0)
		return bytes_per_line;

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

    if (buf->code    != icd->current_fmt->code ||
            vb->width   != icd->user_width ||
            vb->height  != icd->user_height ||
             vb->field   != field) {
        buf->code    = icd->current_fmt->code;
        vb->width   = icd->user_width;
        vb->height  = icd->user_height;
        vb->field   = field;
        vb->state   = VIDEOBUF_NEEDS_INIT;
    }

    vb->size = bytes_per_line*vb->height;          /* ddl@rock-chips.com : fmt->depth is coorect */
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
    //RK29CAMERA_TR("\n%s..%d.. \n",__FUNCTION__,__LINE__);
    return 0;
fail:
    rk29_videobuf_free(vq, buf);
out:
    return ret;
}

static inline void rk29_videobuf_capture(struct videobuf_buffer *vb)
{
	unsigned int y_addr,uv_addr;
	struct rk29_camera_dev *pcdev = rk29_camdev_info_ptr;

    if (vb) {
		if (CAM_WORKQUEUE_IS_EN()) {
			y_addr = pcdev->vipmem_phybase + vb->i*pcdev->vipmem_bsize;
			uv_addr = y_addr + pcdev->host_width*pcdev->host_height;

			if (y_addr > (pcdev->vipmem_phybase + pcdev->vipmem_size - pcdev->vipmem_bsize)) {
				RK29CAMERA_TR("vipmem for IPP is overflow! %dx%d -> %dx%d vb_index:%d\n",pcdev->host_width,pcdev->host_height,
					          pcdev->icd->user_width,pcdev->icd->user_height, vb->i);
				BUG();
			}
		} else {
			y_addr = vb->boff;
			uv_addr = y_addr + vb->width * vb->height;
		}
        write_vip_reg(RK29_VIP_CAPTURE_F1SA_Y, y_addr);
        write_vip_reg(RK29_VIP_CAPTURE_F1SA_UV, uv_addr);
        write_vip_reg(RK29_VIP_CAPTURE_F2SA_Y, y_addr);
        write_vip_reg(RK29_VIP_CAPTURE_F2SA_UV, uv_addr);
        write_vip_reg(RK29_VIP_FB_SR,  0x00000002);//frame1 has been ready to receive data,frame 2 is not used
    }
}
/* Locking: Caller holds q->irqlock */
static void rk29_videobuf_queue(struct videobuf_queue *vq,
                                struct videobuf_buffer *vb)
{
    struct soc_camera_device *icd = vq->priv_data;
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;

    dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
            vb, vb->baddr, vb->bsize);

    vb->state = VIDEOBUF_QUEUED;

	if (list_empty(&pcdev->capture)) {
		list_add_tail(&vb->queue, &pcdev->capture);
	} else {
		if (list_entry(pcdev->capture.next, struct videobuf_buffer, queue) != vb)
			list_add_tail(&vb->queue, &pcdev->capture);
		else
			BUG();    /* ddl@rock-chips.com : The same videobuffer queue again */
	}
    if (!pcdev->active) {
        pcdev->active = vb;
        rk29_videobuf_capture(vb);
    }
}
static int rk29_pixfmt2ippfmt(unsigned int pixfmt, int *ippfmt)
{
	switch (pixfmt)
	{
		case V4L2_PIX_FMT_NV16:
        case V4L2_PIX_FMT_YUV422P:
		{
			*ippfmt = IPP_Y_CBCR_H2V1;
			break;
		}
		case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_YUV420:
		{
			*ippfmt = IPP_Y_CBCR_H2V2;
			break;
		}
		default:
			goto rk29_pixfmt2ippfmt_err;
	}

	return 0;
rk29_pixfmt2ippfmt_err:
	return -1;
}
static void rk29_camera_capture_process(struct work_struct *work)
{
	struct rk29_camera_work *camera_work = container_of(work, struct rk29_camera_work, work);
	struct videobuf_buffer *vb = camera_work->vb;
	struct rk29_camera_dev *pcdev = camera_work->pcdev;
	struct rk29_ipp_req ipp_req;
	unsigned long int flags;
    int src_y_offset,src_uv_offset,dst_y_offset,dst_uv_offset,src_y_size,dst_y_size;
    int scale_times,w,h,vipdata_base;
	
    /*
    *ddl@rock-chips.com: 
    * IPP Dest image resolution is 2047x1088, so scale operation break up some times
    */
    if ((pcdev->icd->user_width > 0x7f0) || (pcdev->icd->user_height > 0x430)) {
        scale_times = MAX((pcdev->icd->user_width/0x7f0),(pcdev->icd->user_height/0x430));        
        scale_times++;
    } else {
        scale_times = 1;
    }
    
    memset(&ipp_req, 0, sizeof(struct rk29_ipp_req));
    
    down(&pcdev->zoominfo.sem);
    
    ipp_req.timeout = 100;
    ipp_req.flag = IPP_ROT_0;    
    ipp_req.src0.w = pcdev->zoominfo.a.c.width/scale_times;
    ipp_req.src0.h = pcdev->zoominfo.a.c.height/scale_times;
    ipp_req.src_vir_w = pcdev->host_width;
    rk29_pixfmt2ippfmt(pcdev->pixfmt, &ipp_req.src0.fmt);
    ipp_req.dst0.w = pcdev->icd->user_width/scale_times;
    ipp_req.dst0.h = pcdev->icd->user_height/scale_times;
    ipp_req.dst_vir_w = pcdev->icd->user_width;        
    rk29_pixfmt2ippfmt(pcdev->pixfmt, &ipp_req.dst0.fmt);

    vipdata_base = pcdev->vipmem_phybase + vb->i*pcdev->vipmem_bsize;
    src_y_size = pcdev->host_width*pcdev->host_height;
    dst_y_size = pcdev->icd->user_width*pcdev->icd->user_height;
    
    for (h=0; h<scale_times; h++) {
        for (w=0; w<scale_times; w++) {
            
            src_y_offset = (pcdev->zoominfo.a.c.top + h*pcdev->zoominfo.a.c.height/scale_times)* pcdev->host_width 
                        + pcdev->zoominfo.a.c.left + w*pcdev->zoominfo.a.c.width/scale_times;
		    src_uv_offset = (pcdev->zoominfo.a.c.top + h*pcdev->zoominfo.a.c.height/scale_times)* pcdev->host_width/2
                        + pcdev->zoominfo.a.c.left + w*pcdev->zoominfo.a.c.width/scale_times;

            dst_y_offset = pcdev->icd->user_width*pcdev->icd->user_height*h/scale_times + pcdev->icd->user_width*w/scale_times;
            dst_uv_offset = pcdev->icd->user_width*pcdev->icd->user_height*h/scale_times/2 + pcdev->icd->user_width*w/scale_times;

    		ipp_req.src0.YrgbMst = vipdata_base + src_y_offset;
    		ipp_req.src0.CbrMst = vipdata_base + src_y_size + src_uv_offset;
    		ipp_req.dst0.YrgbMst = vb->boff + dst_y_offset;
    		ipp_req.dst0.CbrMst = vb->boff + dst_y_size + dst_uv_offset;

    		if (ipp_blit_sync(&ipp_req)) {
    			spin_lock_irqsave(&pcdev->lock, flags);
    			vb->state = VIDEOBUF_ERROR;
    			spin_unlock_irqrestore(&pcdev->lock, flags);

                RK29CAMERA_TR("Capture image(vb->i:0x%x) which IPP operated is error:\n",vb->i);
                RK29CAMERA_TR("widx:%d hidx:%d ",w,h);
                RK29CAMERA_TR("%dx%d@(%d,%d)->%dx%d\n",pcdev->zoominfo.a.c.width,pcdev->zoominfo.a.c.height,pcdev->zoominfo.a.c.left,pcdev->zoominfo.a.c.top,pcdev->icd->user_width,pcdev->icd->user_height);
            	RK29CAMERA_TR("ipp_req.src0.YrgbMst:0x%x ipp_req.src0.CbrMst:0x%x \n", ipp_req.src0.YrgbMst,ipp_req.src0.CbrMst);
            	RK29CAMERA_TR("ipp_req.src0.w:0x%x ipp_req.src0.h:0x%x \n",ipp_req.src0.w,ipp_req.src0.h);
            	RK29CAMERA_TR("ipp_req.src0.fmt:0x%x\n",ipp_req.src0.fmt);
            	RK29CAMERA_TR("ipp_req.dst0.YrgbMst:0x%x ipp_req.dst0.CbrMst:0x%x \n",ipp_req.dst0.YrgbMst,ipp_req.dst0.CbrMst);
            	RK29CAMERA_TR("ipp_req.dst0.w:0x%x ipp_req.dst0.h:0x%x \n",ipp_req.dst0.w ,ipp_req.dst0.h);
            	RK29CAMERA_TR("ipp_req.dst0.fmt:0x%x\n",ipp_req.dst0.fmt);
            	RK29CAMERA_TR("ipp_req.src_vir_w:0x%x ipp_req.dst_vir_w :0x%x\n",ipp_req.src_vir_w ,ipp_req.dst_vir_w);
            	RK29CAMERA_TR("ipp_req.timeout:0x%x ipp_req.flag :0x%x\n",ipp_req.timeout,ipp_req.flag);
                
    			goto do_ipp_err;
    		}
        }
    }

    if (pcdev->icd_cb.sensor_cb)
        (pcdev->icd_cb.sensor_cb)(vb);
	
do_ipp_err:
    up(&pcdev->zoominfo.sem);
    wake_up(&(camera_work->vb->done)); 
	return;
}
static irqreturn_t rk29_camera_irq(int irq, void *data)
{
    struct rk29_camera_dev *pcdev = data;
    struct videobuf_buffer *vb;
	struct rk29_camera_work *wk;
    static struct timeval first_tv;
    struct timeval tv;

    read_vip_reg(RK29_VIP_INT_STS);    /* clear vip interrupte single  */
    /* ddl@rock-chps.com : Current VIP is run in One Frame Mode, Frame 1 is validate */
    if (read_vip_reg(RK29_VIP_FB_SR) & 0x01) {
        if (!pcdev->fps) {
            do_gettimeofday(&first_tv);            
        }
		pcdev->fps++;
		if (!pcdev->active)
			goto RK29_CAMERA_IRQ_END;

        if (pcdev->frame_inval>0) {            
            pcdev->frame_inval--;
            rk29_videobuf_capture(pcdev->active);
            goto RK29_CAMERA_IRQ_END;
        } else if (pcdev->frame_inval) {
        	RK29CAMERA_TR("frame_inval : %0x",pcdev->frame_inval);
            pcdev->frame_inval = 0;
        }

        if(pcdev->fps == RK29_CAM_FRAME_MEASURE) {
            do_gettimeofday(&tv);            
            pcdev->frame_interval = ((tv.tv_sec*1000000 + tv.tv_usec) - (first_tv.tv_sec*1000000 + first_tv.tv_usec))
                                    /(RK29_CAM_FRAME_MEASURE-1);
        }

        vb = pcdev->active;
		/* ddl@rock-chips.com : this vb may be deleted from queue */
		if ((vb->state == VIDEOBUF_QUEUED) || (vb->state == VIDEOBUF_ACTIVE)) {
        	list_del_init(&vb->queue);
		}

        pcdev->active = NULL;
        if (!list_empty(&pcdev->capture)) {
            pcdev->active = list_entry(pcdev->capture.next, struct videobuf_buffer, queue);
			if (pcdev->active) {
				rk29_videobuf_capture(pcdev->active);
			}
        }

        if (pcdev->active == NULL) {
			RK29CAMERA_DG("%s video_buf queue is empty!\n",__FUNCTION__);
        }

		if ((vb->state == VIDEOBUF_QUEUED) || (vb->state == VIDEOBUF_ACTIVE)) {
	        vb->state = VIDEOBUF_DONE;
	        do_gettimeofday(&vb->ts);
	        vb->field_count++;
		}

		if (CAM_WORKQUEUE_IS_EN()) {
			wk = pcdev->camera_work + vb->i;
			INIT_WORK(&(wk->work), rk29_camera_capture_process);
			wk->vb = vb;
			wk->pcdev = pcdev;
			queue_work(pcdev->camera_wq, &(wk->work));
		} else {		    
			wake_up(&vb->done);
		}
    }

RK29_CAMERA_IRQ_END:
    return IRQ_HANDLED;
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
    if(vb->i == 0){
        flush_workqueue(pcdev->camera_wq);
    }

	if (vb == pcdev->active) {
		RK29CAMERA_DG("%s Wait for this video buf(0x%x) write finished!\n ",__FUNCTION__,(unsigned int)vb);
		interruptible_sleep_on_timeout(&vb->done, msecs_to_jiffies(500));
        flush_workqueue(pcdev->camera_wq);
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
                                   icd,&icd->video_lock);
}
static int rk29_camera_activate(struct rk29_camera_dev *pcdev, struct soc_camera_device *icd)
{
    unsigned long sensor_bus_flags = SOCAM_MCLK_24MHZ;
    struct clk *parent;

    RK29CAMERA_DG("%s..%d.. \n",__FUNCTION__,__LINE__);
    if (!pcdev->aclk_ddr_lcdc || !pcdev->aclk_disp_matrix ||  !pcdev->hclk_cpu_display ||
		!pcdev->vip_slave || !pcdev->vip_out || !pcdev->vip_input || !pcdev->vip_bus || !pcdev->pd_display ||
		IS_ERR(pcdev->aclk_ddr_lcdc) || IS_ERR(pcdev->aclk_disp_matrix) ||  IS_ERR(pcdev->hclk_cpu_display) || IS_ERR(pcdev->pd_display) ||
		IS_ERR(pcdev->vip_slave) || IS_ERR(pcdev->vip_out) || IS_ERR(pcdev->vip_input) || IS_ERR(pcdev->vip_bus))  {

        RK29CAMERA_TR(KERN_ERR "failed to get vip_clk(axi) source\n");
        goto RK29_CAMERA_ACTIVE_ERR;
    }

	if (!pcdev->hclk_disp_matrix || !pcdev->vip_matrix ||
		IS_ERR(pcdev->hclk_disp_matrix) || IS_ERR(pcdev->vip_matrix))  {

        RK29CAMERA_TR(KERN_ERR "failed to get vip_clk(ahb) source\n");
        goto RK29_CAMERA_ACTIVE_ERR;
    }

	clk_enable(pcdev->pd_display);

	clk_enable(pcdev->aclk_ddr_lcdc);
	clk_enable(pcdev->aclk_disp_matrix);

	clk_enable(pcdev->hclk_cpu_display);
	clk_enable(pcdev->vip_slave);

#if (CONFIG_RK29_CAM_WORK_BUS==RK29_CAM_AHB)
	clk_enable(pcdev->hclk_disp_matrix);
	clk_enable(pcdev->vip_matrix);
#endif

	clk_enable(pcdev->vip_input);
	clk_enable(pcdev->vip_bus);

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

    clk_set_parent(pcdev->vip_out, parent);

    clk_enable(pcdev->vip_out);
    rk29_mux_api_set(GPIO1B4_VIPCLKOUT_NAME, GPIO1L_VIP_CLKOUT);
    ndelay(10);

#if (CONFIG_RK29_CAM_WORK_BUS==RK29_CAM_AHB)
	write_grf_reg(GRF_SOC_CON0_Reg, read_grf_reg(GRF_SOC_CON0_Reg)|VIP_AHBMASTER);  //VIP Config to AHB
	write_grf_reg(GRF_OS_REG0, read_grf_reg(GRF_OS_REG0)&(~VIP_ACLK_DIV_HCLK_2));   //aclk:hclk = 1:1
#else
	write_grf_reg(GRF_SOC_CON0_Reg, read_grf_reg(GRF_SOC_CON0_Reg)&(~VIP_AHBMASTER));  //VIP Config to AXI
    write_grf_reg(GRF_OS_REG0, read_grf_reg(GRF_OS_REG0)|VIP_ACLK_DIV_HCLK_2);   //aclk:hclk = 2:1
#endif
	ndelay(10);

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
    //pcdev->active = NULL;

    write_vip_reg(RK29_VIP_CTRL, 0);
    read_vip_reg(RK29_VIP_INT_STS);             //clear vip interrupte single

    rk29_mux_api_set(GPIO1B4_VIPCLKOUT_NAME, GPIO1L_GPIO1B4);
    clk_disable(pcdev->vip_out);

	clk_disable(pcdev->vip_input);
	clk_disable(pcdev->vip_bus);

#if (CONFIG_RK29_CAM_WORK_BUS==RK29_CAM_AHB)
	clk_disable(pcdev->hclk_disp_matrix);
	clk_disable(pcdev->vip_matrix);
#endif

	clk_disable(pcdev->hclk_cpu_display);
	clk_disable(pcdev->vip_slave);

	clk_disable(pcdev->aclk_ddr_lcdc);
	clk_disable(pcdev->aclk_disp_matrix);

	clk_disable(pcdev->pd_display);
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
    int ret,i,icd_catch;
    struct rk29_camera_frmivalenum *fival_list,*fival_nxt;
    
    mutex_lock(&camera_lock);

    if (pcdev->icd) {
        ret = -EBUSY;
        goto ebusy;
    }

    RK29CAMERA_DG("RK29 Camera driver attached to %s\n",dev_name(icd->pdev));
    
	pcdev->frame_inval = RK29_CAM_FRAME_INVAL_INIT;
    pcdev->active = NULL;
    pcdev->icd = NULL;
	pcdev->reginfo_suspend.Inval = Reg_Invalidate;
    pcdev->zoominfo.zoom_rate = 100;
        
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
		v4l2_subdev_call(sd, core, ioctl, RK29_CAM_SUBDEV_IOREQUEST,(void*)pcdev->pdata);
        #if 0
        ret = v4l2_subdev_call(sd,core, init, 0);
        if (ret)
            goto ebusy;
        #endif
        v4l2_subdev_call(sd, core, ioctl, RK29_CAM_SUBDEV_CB_REGISTER,(void*)(&pcdev->icd_cb));
    }
    
    pcdev->icd = icd;
    pcdev->icd_init = 0;

    icd_catch = 0;
    for (i=0; i<2; i++) {
        if (pcdev->icd_frmival[i].icd == icd)
            icd_catch = 1;
        if (pcdev->icd_frmival[i].icd == NULL) {
            pcdev->icd_frmival[i].icd = icd;
            icd_catch = 1;
        }
    }

    if (icd_catch == 0) {
        fival_list = pcdev->icd_frmival[0].fival_list;
        fival_nxt = fival_list;
        while(fival_nxt != NULL) {
            fival_nxt = fival_list->nxt;
            kfree(fival_list);
            fival_list = fival_nxt;
        }
        pcdev->icd_frmival[0].icd = icd;
        pcdev->icd_frmival[0].fival_list = kzalloc(sizeof(struct rk29_camera_frmivalenum),GFP_KERNEL);
    }
ebusy:
    mutex_unlock(&camera_lock);

    return ret;
}
static void rk29_camera_remove_device(struct soc_camera_device *icd)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);

	mutex_lock(&camera_lock);
    BUG_ON(icd != pcdev->icd);

    RK29CAMERA_DG("RK29 Camera driver detached from %s\n",dev_name(icd->pdev));

	/* ddl@rock-chips.com: Application will call VIDIOC_STREAMOFF before close device, but
	   stream may be turn on again before close device, if suspend and resume happened. */
	if (read_vip_reg(RK29_VIP_CTRL) & ENABLE_CAPTURE) {
		rk29_camera_s_stream(icd,0);
	}

    v4l2_subdev_call(sd, core, ioctl, RK29_CAM_SUBDEV_DEACTIVATE,NULL);
	rk29_camera_deactivate(pcdev);

	if (pcdev->camera_work) {
		kfree(pcdev->camera_work);
		pcdev->camera_work = NULL;
		pcdev->camera_work_count = 0;
	}

	pcdev->active = NULL;
    pcdev->icd = NULL;
    pcdev->icd_cb.sensor_cb = NULL;
	pcdev->reginfo_suspend.Inval = Reg_Invalidate;
	/* ddl@rock-chips.com: capture list must be reset, because this list may be not empty,
     * if app havn't dequeue all videobuf before close camera device;
	*/
    INIT_LIST_HEAD(&pcdev->capture);

	mutex_unlock(&camera_lock);
	RK29CAMERA_DG("%s exit\n",__FUNCTION__);

	return;
}
static int rk29_camera_set_bus_param(struct soc_camera_device *icd, __u32 pixfmt)
{
    unsigned long bus_flags, camera_flags, common_flags;
    unsigned int vip_ctrl_val = 0;
	const struct soc_mbus_pixelfmt *fmt;
	int ret = 0;

    RK29CAMERA_DG("%s..%d..\n",__FUNCTION__,__LINE__);

	fmt = soc_mbus_get_fmtdesc(icd->current_fmt->code);
	if (!fmt)
		return -EINVAL;

    bus_flags = RK29_CAM_BUS_PARAM;
	/* If requested data width is supported by the platform, use it */
	switch (fmt->bits_per_sample) {
    	case 10:
    		if (!(bus_flags & SOCAM_DATAWIDTH_10))
    			return -EINVAL;    		
    		break;
    	case 9:
    		if (!(bus_flags & SOCAM_DATAWIDTH_9))
    			return -EINVAL;    		
    		break;
    	case 8:
    		if (!(bus_flags & SOCAM_DATAWIDTH_8))
    			return -EINVAL;    		
    		break;
    	default:
    		return -EINVAL;
	}
    
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
    RK29CAMERA_DG("%s..ctrl:0x%x CtrReg=%x AXI_AHB:0x%x aclk_hclk:0x%x \n",__FUNCTION__,vip_ctrl_val,read_vip_reg(RK29_VIP_CTRL),
		read_grf_reg(GRF_SOC_CON0_Reg)&VIP_AHBMASTER, read_grf_reg(GRF_OS_REG0)&VIP_ACLK_DIV_HCLK_2);

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

static const struct soc_mbus_pixelfmt rk29_camera_formats[] = {
   {
		.fourcc			= V4L2_PIX_FMT_NV12,
		.name			= "YUV420 NV12",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_1_5X8,
		.order			= SOC_MBUS_ORDER_LE,
	},{
		.fourcc			= V4L2_PIX_FMT_NV16,
		.name			= "YUV422 NV16",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	},{
		.fourcc			= V4L2_PIX_FMT_YUV420,
		.name			= "NV12(v0.0.1)",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_1_5X8,
		.order			= SOC_MBUS_ORDER_LE,
	},{
		.fourcc			= V4L2_PIX_FMT_YUV422P,
		.name			= "NV16(v0.0.1)",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	}
};

static void rk29_camera_setup_format(struct soc_camera_device *icd, __u32 host_pixfmt, enum v4l2_mbus_pixelcode icd_code, struct v4l2_rect *rect)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
    unsigned int vip_fs = 0,vip_crop = 0;
    unsigned int vip_ctrl_val = VIP_SENSOR|ONEFRAME|DISABLE_CAPTURE;

    switch (host_pixfmt)
    {
        case V4L2_PIX_FMT_NV16:
        case V4L2_PIX_FMT_YUV422P:  /* ddl@rock-chips.com: V4L2_PIX_FMT_YUV422P is V4L2_PIX_FMT_NV16 actually in 0.0.1 driver */
            vip_ctrl_val |= VIPREGYUV422;
			pcdev->frame_inval = RK29_CAM_FRAME_INVAL_DC;
			pcdev->pixfmt = host_pixfmt;
            break;
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_YUV420:   /* ddl@rock-chips.com: V4L2_PIX_FMT_YUV420 is V4L2_PIX_FMT_NV12 actually in 0.0.1 driver */
            vip_ctrl_val |= VIPREGYUV420;
			if (pcdev->frame_inval != RK29_CAM_FRAME_INVAL_INIT)
				pcdev->frame_inval = RK29_CAM_FRAME_INVAL_INIT;
			pcdev->pixfmt = host_pixfmt;
            break;
        default:                                                                                /* ddl@rock-chips.com : vip output format is hold when pixfmt is invalidate */
            vip_ctrl_val |= (read_vip_reg(RK29_VIP_CTRL) & VIPREGYUV422);
            break;
    }

    switch (icd_code)
    {
        case V4L2_MBUS_FMT_UYVY8_2X8:
            vip_ctrl_val |= SENSOR_UYVY;
            break;
        case V4L2_MBUS_FMT_YUYV8_2X8:
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
        BUG();
    }

    write_vip_reg(RK29_VIP_CROP, vip_crop);
    write_vip_reg(RK29_VIP_FS, vip_fs);

    write_vip_reg(RK29_VIP_FB_SR,  0x00000003);

	pcdev->host_width = rect->width;
	pcdev->host_height = rect->height;

    RK29CAMERA_DG("%s.. crop:0x%x fs:0x%x ctrl:0x%x CtrlReg:0x%x\n",__FUNCTION__,vip_crop,vip_fs,vip_ctrl_val,read_vip_reg(RK29_VIP_CTRL));
	return;
}

static int rk29_camera_get_formats(struct soc_camera_device *icd, unsigned int idx,
				  struct soc_camera_format_xlate *xlate)
{
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
    struct device *dev = icd->dev.parent;
    int formats = 0, ret;
	enum v4l2_mbus_pixelcode code;
	const struct soc_mbus_pixelfmt *fmt;

	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, idx, &code);
	if (ret < 0)
		/* No more formats */
		return 0;

	fmt = soc_mbus_get_fmtdesc(code);
	if (!fmt) {
		dev_err(dev, "Invalid format code #%u: %d\n", idx, code);
		return 0;
	}

    ret = rk29_camera_try_bus_param(icd, fmt->bits_per_sample);
    if (ret < 0)
        return 0;

    switch (code) {
        case V4L2_MBUS_FMT_UYVY8_2X8:
        case V4L2_MBUS_FMT_YUYV8_2X8:
            formats++;
            if (xlate) {
                xlate->host_fmt = &rk29_camera_formats[0];
                xlate->code	= code;
                xlate++;
                dev_dbg(dev, "Providing format %s using code %d\n",
                	rk29_camera_formats[0].name,code);
            }

            formats++;
            if (xlate) {
                xlate->host_fmt = &rk29_camera_formats[1];
                xlate->code	= code;
                xlate++;
                dev_dbg(dev, "Providing format %s using code %d\n",
                	rk29_camera_formats[1].name,code);
            }

            formats++;
            if (xlate) {
                xlate->host_fmt = &rk29_camera_formats[2];
                xlate->code	= code;
                xlate++;
                dev_dbg(dev, "Providing format %s using code %d\n",
                	rk29_camera_formats[2].name,code);
            } 

            formats++;
            if (xlate) {
                xlate->host_fmt = &rk29_camera_formats[3];
                xlate->code	= code;
                xlate++;
                dev_dbg(dev, "Providing format %s using code %d\n",
                	rk29_camera_formats[3].name,code);;
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
	struct v4l2_mbus_framefmt mf;
	u32 fourcc = icd->current_fmt->host_fmt->fourcc;
    int ret;

    ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
    if (ret < 0)
        return ret;

    if ((mf.width < (a->c.left + a->c.width)) || (mf.height < (a->c.top + a->c.height)))  {

        mf.width = a->c.left + a->c.width;
        mf.height = a->c.top + a->c.height;

        v4l_bound_align_image(&mf.width, RK29_CAM_W_MIN, RK29_CAM_W_MAX, 1,
            &mf.height, RK29_CAM_H_MIN, RK29_CAM_H_MAX, 0,
            fourcc == V4L2_PIX_FMT_NV16 ?4 : 0);

        ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
        if (ret < 0)
            return ret;
    }

    rk29_camera_setup_format(icd, fourcc, mf.code, &a->c);

    icd->user_width = mf.width;
    icd->user_height = mf.height;

    return 0;
}

static int rk29_camera_set_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
    struct device *dev = icd->dev.parent;
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
    const struct soc_camera_format_xlate *xlate = NULL;
	struct soc_camera_host *ici =to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
    struct v4l2_pix_format *pix = &f->fmt.pix;
    struct v4l2_mbus_framefmt mf;
    struct v4l2_rect rect;
    int ret,usr_w,usr_h,icd_width,icd_height;
    int stream_on = 0;

	usr_w = pix->width;
	usr_h = pix->height;
    RK29CAMERA_DG("%s enter width:%d  height:%d\n",__FUNCTION__,usr_w,usr_h);

    xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
    if (!xlate) {
        dev_err(dev, "Format %x not found\n", pix->pixelformat);
        ret = -EINVAL;
        goto RK29_CAMERA_SET_FMT_END;
    }
    
    /* ddl@rock-chips.com: sensor init code transmit in here after open */
    if (pcdev->icd_init == 0) {
        v4l2_subdev_call(sd,core, init, 0);        
        pcdev->icd_init = 1;
    }

    stream_on = read_vip_reg(RK29_VIP_CTRL);
    if (stream_on & ENABLE_CAPTURE)
        write_vip_reg(RK29_VIP_CTRL, (stream_on & (~ENABLE_CAPTURE)));
    
	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);

	if (mf.code != xlate->code)
		return -EINVAL;
    
    icd_width = mf.width;
    icd_height = mf.height;
	#ifdef CONFIG_VIDEO_RK29_WORK_IPP
	if ((mf.width != usr_w) || (mf.height != usr_h)) {
        if (unlikely((mf.width <16) || (mf.width > 8190) || (mf.height < 16) || (mf.height > 8190))) {
    		RK29CAMERA_TR("Senor and IPP both invalid source resolution(%dx%d)\n",mf.width,mf.height);
    		ret = -EINVAL;
    		goto RK29_CAMERA_SET_FMT_END;
    	}    	
    	if (unlikely((usr_w <16)||(usr_h < 16))) {
    		RK29CAMERA_TR("Senor and IPP both invalid destination resolution(%dx%d)\n",usr_w,usr_h);
    		ret = -EINVAL;
            goto RK29_CAMERA_SET_FMT_END;
    	}
	}
	#endif
    icd->sense = NULL;

    if (!ret) {

        if (mf.width*usr_h == mf.height*usr_w) {
            rect.width = mf.width;
            rect.height = mf.height;
        } else {
            int ratio;
            if (usr_w > usr_h) {
                if (mf.width > usr_w) {
                    ratio = MIN((mf.width*10/usr_w),(mf.height*10/usr_h))-1;
                    rect.width = usr_w*ratio/10;
                    rect.height = usr_h*ratio/10;                    
                } else {
                    ratio = MAX((usr_w*10/mf.width),(usr_h*10/mf.height))+1;
                    rect.width = usr_w*10/ratio;
                    rect.height = usr_h*10/ratio;
                }                
            } else {
                if (mf.height > usr_h) {
                    ratio = MIN((mf.width*10/usr_w),(mf.height*10/usr_h))-1;
                    rect.width = usr_w*ratio/10;
                    rect.height = usr_h*ratio/10;                     
                } else {
                    ratio = MAX((usr_w*10/mf.width),(usr_h*10/mf.height))+1;
                    rect.width = usr_w*10/ratio;
                    rect.height = usr_h*10/ratio;
                }
            }
        }

        rect.left = (mf.width - rect.width)/2;
        rect.top = (mf.height - rect.height)/2;

        down(&pcdev->zoominfo.sem);        
        pcdev->zoominfo.a.c.width = rect.width*100/pcdev->zoominfo.zoom_rate;
		pcdev->zoominfo.a.c.width &= ~0x03;
		pcdev->zoominfo.a.c.height = rect.height*100/pcdev->zoominfo.zoom_rate;
		pcdev->zoominfo.a.c.height &= ~0x03;
		pcdev->zoominfo.a.c.left = ((rect.width - pcdev->zoominfo.a.c.width)/2 + rect.left)&(~0x01);
		pcdev->zoominfo.a.c.top = ((rect.height - pcdev->zoominfo.a.c.height)/2 + rect.top)&(~0x01);
        up(&pcdev->zoominfo.sem);

        /* ddl@rock-chips.com: IPP work limit check */
        if ((pcdev->zoominfo.a.c.width != usr_w) || (pcdev->zoominfo.a.c.height != usr_h)) {
            if (usr_w > 0x7f0) {
                if (((usr_w>>1)&0x3f) && (((usr_w>>1)&0x3f) <= 8)) {
                    RK29CAMERA_TR("IPP Destination resolution(%dx%d, ((%d div 1) mod 64)=%d is <= 8)",usr_w,usr_h, usr_w, (int)((usr_w>>1)&0x3f));
                    ret = -EINVAL;
                    goto RK29_CAMERA_SET_FMT_END;
                }
            } else {
                if ((usr_w&0x3f) && ((usr_w&0x3f) <= 8)) {
                    RK29CAMERA_TR("IPP Destination resolution(%dx%d, %d mod 64=%d is <= 8)",usr_w,usr_h, usr_w, (int)(usr_w&0x3f));
                    ret = -EINVAL;
                    goto RK29_CAMERA_SET_FMT_END;
                }
            }
        }
        
        /* ddl@rock-chips.com: Crop is doing by IPP, not by VIP in rk2918 */
        rect.left = 0;
        rect.top = 0;
        rect.width = mf.width;
        rect.height = mf.height;
        
        RK29CAMERA_DG("%s..%s Sensor output:%dx%d  VIP output:%dx%d (zoom: %dx%d@(%d,%d)->%dx%d)\n",__FUNCTION__,xlate->host_fmt->name,
			           mf.width, mf.height,rect.width,rect.height, pcdev->zoominfo.a.c.width,pcdev->zoominfo.a.c.height, pcdev->zoominfo.a.c.left,pcdev->zoominfo.a.c.top,
			           pix->width, pix->height);

                
        rk29_camera_setup_format(icd, pix->pixelformat, mf.code, &rect); 
        
		if (CAM_IPPWORK_IS_EN()) {
			BUG_ON(pcdev->vipmem_phybase == 0);
		}
        pcdev->icd_width = icd_width;
        pcdev->icd_height = icd_height;

        pix->width = usr_w;
    	pix->height = usr_h;
    	pix->field = mf.field;
    	pix->colorspace = mf.colorspace;
    	icd->current_fmt = xlate;        
    }

RK29_CAMERA_SET_FMT_END:
    if (stream_on & ENABLE_CAPTURE)
        write_vip_reg(RK29_VIP_CTRL, (read_vip_reg(RK29_VIP_CTRL) | ENABLE_CAPTURE));
	if (ret)
    	RK29CAMERA_TR("\n%s: Driver isn't support %dx%d resolution which user request!\n",__FUNCTION__,usr_w,usr_h);
    return ret;
}
static bool rk29_camera_fmt_capturechk(struct v4l2_format *f)
{
    bool ret = false;

	if ((f->fmt.pix.width == 1024) && (f->fmt.pix.height == 768)) {
		ret = true;
	} else if ((f->fmt.pix.width == 1280) && (f->fmt.pix.height == 1024)) {
		ret = true;
	} else if ((f->fmt.pix.width == 1600) && (f->fmt.pix.height == 1200)) {
		ret = true;
	} else if ((f->fmt.pix.width == 2048) && (f->fmt.pix.height == 1536)) {
		ret = true;
	} else if ((f->fmt.pix.width == 2592) && (f->fmt.pix.height == 1944)) {
		ret = true;
	}

	if (ret == true)
		RK29CAMERA_DG("%s %dx%d is capture format\n", __FUNCTION__, f->fmt.pix.width, f->fmt.pix.height);
	return ret;
}
static int rk29_camera_try_fmt(struct soc_camera_device *icd,
                                   struct v4l2_format *f)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct rk29_camera_dev *pcdev = ici->priv;
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
    const struct soc_camera_format_xlate *xlate;
    struct v4l2_pix_format *pix = &f->fmt.pix;
    __u32 pixfmt = pix->pixelformat;
    int ret,usr_w,usr_h,i;
	bool is_capture = rk29_camera_fmt_capturechk(f);
	bool vipmem_is_overflow = false;
    struct v4l2_mbus_framefmt mf;
    int bytes_per_line_host;
    
	usr_w = pix->width;
	usr_h = pix->height;
	RK29CAMERA_DG("%s enter width:%d  height:%d\n",__FUNCTION__,usr_w,usr_h);

    xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
    if (!xlate) {
        dev_err(icd->dev.parent, "Format (%c%c%c%c) not found\n", pixfmt & 0xFF, (pixfmt >> 8) & 0xFF,
			(pixfmt >> 16) & 0xFF, (pixfmt >> 24) & 0xFF);
        ret = -EINVAL;
        RK29CAMERA_TR("%s(version:%c%c%c) support format:\n",rk29_cam_driver_description,(RK29_CAM_VERSION_CODE&0xff0000)>>16,
            (RK29_CAM_VERSION_CODE&0xff00)>>8,(RK29_CAM_VERSION_CODE&0xff));
        for (i = 0; i < icd->num_user_formats; i++)
		    RK29CAMERA_TR("(%c%c%c%c)-%s\n",
		    icd->user_formats[i].host_fmt->fourcc & 0xFF, (icd->user_formats[i].host_fmt->fourcc >> 8) & 0xFF,
			(icd->user_formats[i].host_fmt->fourcc >> 16) & 0xFF, (icd->user_formats[i].host_fmt->fourcc >> 24) & 0xFF,
			icd->user_formats[i].host_fmt->name);
        goto RK29_CAMERA_TRY_FMT_END;
    }
   /* limit to rk29 hardware capabilities */
    v4l_bound_align_image(&pix->width, RK29_CAM_W_MIN, RK29_CAM_W_MAX, 1,
    	      &pix->height, RK29_CAM_H_MIN, RK29_CAM_H_MAX, 0,
    	      pixfmt == V4L2_PIX_FMT_NV16 ? 4 : 0);

    pix->bytesperline = soc_mbus_bytes_per_line(pix->width,
						    xlate->host_fmt);
	if (pix->bytesperline < 0)
		return pix->bytesperline;

    /* limit to sensor capabilities */
	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	ret = v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
	if (ret < 0)
		goto RK29_CAMERA_TRY_FMT_END;
    RK29CAMERA_DG("%s mf.width:%d  mf.height:%d\n",__FUNCTION__,mf.width,mf.height);
	#ifdef CONFIG_VIDEO_RK29_WORK_IPP       
	if ((mf.width != usr_w) || (mf.height != usr_h)) {
        bytes_per_line_host = soc_mbus_bytes_per_line(mf.width,icd->current_fmt->host_fmt); 
		if (is_capture) {
			vipmem_is_overflow = (PAGE_ALIGN(bytes_per_line_host*mf.height) > pcdev->vipmem_size);
		} else {
			/* Assume preview buffer minimum is 4 */
			vipmem_is_overflow = (PAGE_ALIGN(bytes_per_line_host*mf.height)*4 > pcdev->vipmem_size);
		}        
		if (vipmem_is_overflow == false) {
			pix->width = usr_w;
			pix->height = usr_h;
		} else {
			RK29CAMERA_TR("vipmem for IPP is overflow, This resolution(%dx%d -> %dx%d) is invalidate!\n",mf.width,mf.height,usr_w,usr_h);
            pix->width = mf.width;
            pix->height = mf.height;            
		}
        
        if ((mf.width < usr_w) || (mf.height < usr_h)) {
            if (((usr_w>>1) > mf.width) || ((usr_h>>1) > mf.height)) {
                RK29CAMERA_TR("The aspect ratio(%dx%d/%dx%d) is bigger than 2 !\n",mf.width,mf.height,usr_w,usr_h);
                pix->width = mf.width;
                pix->height = mf.height;
            }
        }        
	}
	#else
    pix->width	= mf.width;
	pix->height	= mf.height;	
    #endif
    pix->colorspace	= mf.colorspace;    

    switch (mf.field) {
    	case V4L2_FIELD_ANY:
    	case V4L2_FIELD_NONE:
    		pix->field	= V4L2_FIELD_NONE;
    		break;
    	default:
    		/* TODO: support interlaced at least in pass-through mode */
    		dev_err(icd->dev.parent, "Field type %d unsupported.\n",
    			mf.field);
    		goto RK29_CAMERA_TRY_FMT_END;
	}

RK29_CAMERA_TRY_FMT_END:
	if (ret)
    	RK29CAMERA_TR("\n%s..%d.. ret = %d  \n",__FUNCTION__,__LINE__, ret);
    return ret;
}

static int rk29_camera_reqbufs(struct soc_camera_device *icd,
                               struct v4l2_requestbuffers *p)
{
    int i;

    /* This is for locking debugging only. I removed spinlocks and now I
     * check whether .prepare is ever called on a linked buffer, or whether
     * a dma IRQ can occur for an in-work or unlinked buffer. Until now
     * it hadn't triggered */
    for (i = 0; i < p->count; i++) {
        struct rk29_buffer *buf = container_of(icd->vb_vidq.bufs[i],
                                                           struct rk29_buffer, vb);
        buf->inwork = 0;
        INIT_LIST_HEAD(&buf->vb.queue);
    }

    return 0;
}

static unsigned int rk29_camera_poll(struct file *file, poll_table *pt)
{
    struct soc_camera_device *icd = file->private_data;
    struct rk29_buffer *buf;

    buf = list_entry(icd->vb_vidq.stream.next, struct rk29_buffer,
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
    struct rk29_camera_dev *pcdev = ici->priv;
    char orientation[5];
    int i;

    strlcpy(cap->card, dev_name(pcdev->icd->pdev), sizeof(cap->card));

    memset(orientation,0x00,sizeof(orientation));
    for (i=0; i<RK29_CAM_SUPPORT_NUMS;i++) {
        if ((pcdev->pdata->info[i].dev_name!=NULL) && (strcmp(dev_name(pcdev->icd->pdev), pcdev->pdata->info[i].dev_name) == 0)) {
            sprintf(orientation,"-%d",pcdev->pdata->info[i].orientation);
        }
    }
    
    if (orientation[0] != '-') {
        RK29CAMERA_TR("%s: %s is not registered in rk29_camera_platform_data, orientation apply default value",__FUNCTION__,dev_name(pcdev->icd->pdev));
        if (strstr(dev_name(pcdev->icd->pdev),"front")) 
            strcat(cap->card,"-270");
        else 
            strcat(cap->card,"-90");
    } else {
        strcat(cap->card,orientation); 
    }
    
    cap->version = RK29_CAM_VERSION_CODE;
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

    return 0;
}

static int rk29_camera_suspend(struct soc_camera_device *icd, pm_message_t state)
{
    struct soc_camera_host *ici =
                    to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd;
    int ret = 0,tmp;

	mutex_lock(&camera_lock);
	if ((pcdev->icd == icd) && (icd->ops->suspend)) {
		rk29_camera_s_stream(icd, 0);
		sd = soc_camera_to_subdev(icd);
		v4l2_subdev_call(sd, video, s_stream, 0);
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
	mutex_unlock(&camera_lock);
    return ret;
}

static int rk29_camera_resume(struct soc_camera_device *icd)
{
    struct soc_camera_host *ici =
                    to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd;
    int ret = 0;

	mutex_lock(&camera_lock);
	if ((pcdev->icd == icd) && (icd->ops->resume)) {
		if (pcdev->reginfo_suspend.Inval == Reg_Validate) {
			rk29_camera_activate(pcdev, icd);
			write_vip_reg(RK29_VIP_INT_MASK, pcdev->reginfo_suspend.VipIntMsk);
			write_vip_reg(RK29_VIP_CRM, pcdev->reginfo_suspend.VipCrm);
			write_vip_reg(RK29_VIP_CTRL, pcdev->reginfo_suspend.VipCtrl&~ENABLE_CAPTURE);
			write_vip_reg(RK29_VIP_CROP, pcdev->reginfo_suspend.VipCrop);
			write_vip_reg(RK29_VIP_FS, pcdev->reginfo_suspend.VipFs);

			rk29_videobuf_capture(pcdev->active);
			rk29_camera_s_stream(icd, 1);
			pcdev->reginfo_suspend.Inval = Reg_Invalidate;
		} else {
			RK29CAMERA_TR("Resume fail, vip register recored is invalidate!!\n");
			goto rk29_camera_resume_end;
		}

		ret = icd->ops->resume(icd);
		sd = soc_camera_to_subdev(icd);
		v4l2_subdev_call(sd, video, s_stream, 1);

		RK29CAMERA_DG("%s Enter success\n",__FUNCTION__);
	} else {
		RK29CAMERA_DG("%s icd has been deattach, don't need enter resume\n", __FUNCTION__);
	}

rk29_camera_resume_end:
	mutex_unlock(&camera_lock);
    return ret;
}

static void rk29_camera_reinit_work(struct work_struct *work)
{
	struct device *control;
    struct v4l2_subdev *sd;
	struct v4l2_mbus_framefmt mf;
	const struct soc_camera_format_xlate *xlate;
	int ret;

	write_vip_reg(RK29_VIP_CTRL, (read_vip_reg(RK29_VIP_CTRL)&(~ENABLE_CAPTURE)));

	control = to_soc_camera_control(rk29_camdev_info_ptr->icd);
	sd = dev_get_drvdata(control);
    v4l2_subdev_call(sd, video, s_stream, 0);  /* ddl@rock-chips.com: Avoid sensor autofocus thread is running */
	ret = v4l2_subdev_call(sd,core, init, 1);

	mf.width = rk29_camdev_info_ptr->icd->user_width;
	mf.height = rk29_camdev_info_ptr->icd->user_height;
	xlate = soc_camera_xlate_by_fourcc(rk29_camdev_info_ptr->icd, rk29_camdev_info_ptr->icd->current_fmt->host_fmt->fourcc);	
	mf.code = xlate->code;

	ret |= v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
    v4l2_subdev_call(sd, video, s_stream, 1);
	write_vip_reg(RK29_VIP_CTRL, (read_vip_reg(RK29_VIP_CTRL)|ENABLE_CAPTURE));

	RK29CAMERA_TR("Camera host haven't recevie data from sensor,Reinit sensor now! ret:0x%x\n",ret);
}
static enum hrtimer_restart rk29_camera_fps_func(struct hrtimer *timer)
{
    struct rk29_camera_frmivalenum *fival_nxt=NULL,*fival_pre=NULL, *fival_rec=NULL;
    int rec_flag,i;
    
	RK29CAMERA_DG("rk29_camera_fps_func fps:0x%x\n",rk29_camdev_info_ptr->fps);
	if (rk29_camdev_info_ptr->fps < 2) {
		RK29CAMERA_TR("Camera host haven't recevie data from sensor,Reinit sensor delay!\n");
		INIT_WORK(&rk29_camdev_info_ptr->camera_reinit_work, rk29_camera_reinit_work);
		queue_work(rk29_camdev_info_ptr->camera_wq,&(rk29_camdev_info_ptr->camera_reinit_work));
	} else {
	    for (i=0; i<2; i++) {
            if (rk29_camdev_info_ptr->icd == rk29_camdev_info_ptr->icd_frmival[i].icd) {
                fival_nxt = rk29_camdev_info_ptr->icd_frmival[i].fival_list;                
            }
        }
        
        rec_flag = 0;
        fival_pre = fival_nxt;
        while (fival_nxt != NULL) {

            RK29CAMERA_DG("%s %c%c%c%c %dx%d framerate : %d/%d\n", dev_name(&rk29_camdev_info_ptr->icd->dev), 
                fival_nxt->fival.pixel_format & 0xFF, (fival_nxt->fival.pixel_format >> 8) & 0xFF,
			    (fival_nxt->fival.pixel_format >> 16) & 0xFF, (fival_nxt->fival.pixel_format >> 24),
			    fival_nxt->fival.width, fival_nxt->fival.height, fival_nxt->fival.discrete.denominator,
			    fival_nxt->fival.discrete.numerator);
            
            if (((fival_nxt->fival.pixel_format == rk29_camdev_info_ptr->pixfmt) 
                && (fival_nxt->fival.height == rk29_camdev_info_ptr->icd->user_height)
                && (fival_nxt->fival.width == rk29_camdev_info_ptr->icd->user_width))
                || (fival_nxt->fival.discrete.denominator == 0)) {
                
                fival_nxt->fival.index = 0;
                fival_nxt->fival.width = rk29_camdev_info_ptr->icd->user_width;
                fival_nxt->fival.height= rk29_camdev_info_ptr->icd->user_height;
                fival_nxt->fival.pixel_format = rk29_camdev_info_ptr->pixfmt;
                fival_nxt->fival.discrete.denominator = rk29_camdev_info_ptr->frame_interval;                
                fival_nxt->fival.reserved[1] = (rk29_camdev_info_ptr->icd_width<<16)
                                                    |(rk29_camdev_info_ptr->icd_height);
                fival_nxt->fival.discrete.numerator = 1000000;
                fival_nxt->fival.type = V4L2_FRMIVAL_TYPE_DISCRETE;
                
                rec_flag = 1;
                fival_rec = fival_nxt;
            }
            fival_pre = fival_nxt;
            fival_nxt = fival_nxt->nxt;            
        }

        if ((rec_flag == 0) && fival_pre) {
            fival_pre->nxt = kzalloc(sizeof(struct rk29_camera_frmivalenum), GFP_ATOMIC);
            if (fival_pre->nxt != NULL) {
                fival_pre->nxt->fival.index = fival_pre->fival.index++;
                fival_pre->nxt->fival.width = rk29_camdev_info_ptr->icd->user_width;
                fival_pre->nxt->fival.height= rk29_camdev_info_ptr->icd->user_height;
                fival_pre->nxt->fival.pixel_format = rk29_camdev_info_ptr->pixfmt;

                fival_pre->nxt->fival.discrete.denominator = rk29_camdev_info_ptr->frame_interval;
                
                fival_pre->nxt->fival.reserved[1] = (rk29_camdev_info_ptr->icd_width<<16)
                                                    |(rk29_camdev_info_ptr->icd_height);
                
                fival_pre->nxt->fival.discrete.numerator = 1000000;
                fival_pre->nxt->fival.type = V4L2_FRMIVAL_TYPE_DISCRETE;
                rec_flag = 1;
                fival_rec = fival_pre->nxt;
            }
        }
	}

	return HRTIMER_NORESTART;
}
static int rk29_camera_s_stream(struct soc_camera_device *icd, int enable)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
    int vip_ctrl_val;
	int ret;

	WARN_ON(pcdev->icd != icd);
    pcdev->frame_interval = 0;
	vip_ctrl_val = read_vip_reg(RK29_VIP_CTRL);
	if (enable) {
		pcdev->fps = 0;
		hrtimer_cancel(&pcdev->fps_timer);
		hrtimer_start(&pcdev->fps_timer,ktime_set(1, 0),HRTIMER_MODE_REL);
		vip_ctrl_val |= ENABLE_CAPTURE;
	} else {
        vip_ctrl_val &= ~ENABLE_CAPTURE;
		ret = hrtimer_cancel(&pcdev->fps_timer);
		ret |= flush_work(&rk29_camdev_info_ptr->camera_reinit_work);
		RK29CAMERA_DG("STREAM_OFF cancel timer and flush work:0x%x \n", ret);
	}
	write_vip_reg(RK29_VIP_CTRL, vip_ctrl_val);

	RK29CAMERA_DG("%s.. enable : 0x%x \n", __FUNCTION__, enable);
	return 0;
}
int rk29_camera_enum_frameintervals(struct soc_camera_device *icd, struct v4l2_frmivalenum *fival)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk29_camera_dev *pcdev = ici->priv;
    struct rk29_camera_frmivalenum *fival_list = NULL;
    struct v4l2_frmivalenum *fival_head=NULL;
    int i,ret = 0,index;
    
    index = fival->index & 0x00ffffff;
    if ((fival->index & 0xff000000) == 0xff000000) {   /* ddl@rock-chips.com: detect framerate */ 
        for (i=0; i<2; i++) {
            if (pcdev->icd_frmival[i].icd == icd) {
                fival_list = pcdev->icd_frmival[i].fival_list;            
            }
        }
        
        if (fival_list != NULL) {
            i = 0;
            while (fival_list != NULL) {
                if ((fival->pixel_format == fival_list->fival.pixel_format)
                    && (fival->height == fival_list->fival.height) 
                    && (fival->width == fival_list->fival.width)) {
                    if (i == index)
                        break;
                    i++;
                }                
                fival_list = fival_list->nxt;                
            }
            
            if ((i==index) && (fival_list != NULL)) {
                memcpy(fival, &fival_list->fival, sizeof(struct v4l2_frmivalenum));
            } else {
                ret = -EINVAL;
            }
        } else {
            RK29CAMERA_TR("%s: fival_list is NULL\n",__FUNCTION__);
            ret = -EINVAL;
        }
    } else {  

        for (i=0; i<RK29_CAM_SUPPORT_NUMS; i++) {
            if (pcdev->pdata->info[i].dev_name && (strcmp(dev_name(pcdev->icd->pdev),pcdev->pdata->info[i].dev_name) == 0)) {
                fival_head = pcdev->pdata->info[i].fival;
            }
        }
        
        if (fival_head == NULL) {
            RK29CAMERA_TR("%s: %s is not registered in rk29_camera_platform_data!!",__FUNCTION__,dev_name(pcdev->icd->pdev));
            ret = -EINVAL;
            goto rk29_camera_enum_frameintervals_end;
        }
        
        i = 0;
        while (fival_head->width && fival_head->height) {
            if ((fival->pixel_format == fival_head->pixel_format)
                && (fival->height == fival_head->height) 
                && (fival->width == fival_head->width)) {
                if (i == index) {
                    break;
                }
                i++;
            }
            fival_head++;  
        }

        if ((i == index) && (fival->height == fival_head->height) && (fival->width == fival_head->width)) {
            memcpy(fival, fival_head, sizeof(struct v4l2_frmivalenum));
            RK29CAMERA_DG("%s %dx%d@%c%c%c%c framerate : %d/%d\n", dev_name(rk29_camdev_info_ptr->icd->pdev),
                fival->width, fival->height,
                fival->pixel_format & 0xFF, (fival->pixel_format >> 8) & 0xFF,
			    (fival->pixel_format >> 16) & 0xFF, (fival->pixel_format >> 24),
			     fival->discrete.denominator,fival->discrete.numerator);			    
        } else {
            if (index == 0)
                RK29CAMERA_TR("%s have not catch %d%d@%c%c%c%c index(%d) framerate\n",dev_name(rk29_camdev_info_ptr->icd->pdev),
                    fival->width,fival->height, 
                    fival->pixel_format & 0xFF, (fival->pixel_format >> 8) & 0xFF,
    			    (fival->pixel_format >> 16) & 0xFF, (fival->pixel_format >> 24),
    			    index);
            else
                RK29CAMERA_DG("%s have not catch %d%d@%c%c%c%c index(%d) framerate\n",dev_name(rk29_camdev_info_ptr->icd->pdev),
                    fival->width,fival->height, 
                    fival->pixel_format & 0xFF, (fival->pixel_format >> 8) & 0xFF,
    			    (fival->pixel_format >> 16) & 0xFF, (fival->pixel_format >> 24),
    			    index);
            ret = -EINVAL;
        }
    }
rk29_camera_enum_frameintervals_end:
    return ret;
}

#ifdef CONFIG_VIDEO_RK29_DIGITALZOOM_IPP_ON
static int rk29_camera_set_digit_zoom(struct soc_camera_device *icd,
								const struct v4l2_queryctrl *qctrl, int zoom_rate)
{
	struct v4l2_crop a;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct rk29_camera_dev *pcdev = ici->priv;
	
/* ddl@rock-chips.com : The largest resolution is 2047x1088, so larger resolution must be operated some times
   (Assume operate times is 4),but resolution which ipp can operate ,it is width and height must be even. */
	a.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a.c.width = pcdev->host_width*100/zoom_rate;
	a.c.width &= ~0x03;    
	a.c.height = pcdev->host_height*100/zoom_rate;
	a.c.height &= ~0x03;
	
	a.c.left = ((pcdev->host_width - a.c.width)>>1)&(~0x01);
	a.c.top = ((pcdev->host_height - a.c.height)>>1)&(~0x01);

    down(&pcdev->zoominfo.sem);
	pcdev->zoominfo.a.c.height = a.c.height;
	pcdev->zoominfo.a.c.width = a.c.width;
	pcdev->zoominfo.a.c.top = a.c.top;
	pcdev->zoominfo.a.c.left = a.c.left;
    up(&pcdev->zoominfo.sem);
    
	RK29CAMERA_DG("%s..zoom_rate:%d (%dx%d at (%d,%d)-> %dx%d)\n",__FUNCTION__, zoom_rate,a.c.width, a.c.height, a.c.left, a.c.top, pcdev->host_width, pcdev->host_height );

	return 0;
}
#endif
static inline struct v4l2_queryctrl const *rk29_camera_soc_camera_find_qctrl(
	struct soc_camera_host_ops *ops, int id)
{
	int i;

	for (i = 0; i < ops->num_controls; i++)
		if (ops->controls[i].id == id)
			return &ops->controls[i];

	return NULL;
}


static int rk29_camera_set_ctrl(struct soc_camera_device *icd,
								struct v4l2_control *sctrl)
{

	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	const struct v4l2_queryctrl *qctrl;
    struct rk29_camera_dev *pcdev = ici->priv;
    int ret = 0;

	qctrl = rk29_camera_soc_camera_find_qctrl(ici->ops, sctrl->id);
	if (!qctrl) {
		ret = -ENOIOCTLCMD;
        goto rk29_camera_set_ctrl_end;
	}

	switch (sctrl->id)
	{
	#ifdef CONFIG_VIDEO_RK29_DIGITALZOOM_IPP_ON
		case V4L2_CID_ZOOM_ABSOLUTE:
		{
			if ((sctrl->value < qctrl->minimum) || (sctrl->value > qctrl->maximum)){
        		ret = -EINVAL;
                goto rk29_camera_set_ctrl_end;
        	}
            ret = rk29_camera_set_digit_zoom(icd, qctrl, sctrl->value);
			if (ret == 0) {
				pcdev->zoominfo.zoom_rate = sctrl->value;
            } else { 
                goto rk29_camera_set_ctrl_end;
            }
			break;
		}
    #endif
		default:
			ret = -ENOIOCTLCMD;
			break;
	}
rk29_camera_set_ctrl_end:
	return ret;
}

static struct soc_camera_host_ops rk29_soc_camera_host_ops =
{
    .owner		= THIS_MODULE,
    .add		= rk29_camera_add_device,
    .remove		= rk29_camera_remove_device,
    .suspend	= rk29_camera_suspend,
    .resume		= rk29_camera_resume,
    .enum_frameinervals = rk29_camera_enum_frameintervals,
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
    .s_stream = rk29_camera_s_stream,   /* ddl@rock-chips.com : Add stream control for host */
    .set_ctrl = rk29_camera_set_ctrl,
    .controls = rk29_camera_controls,
    .num_controls = ARRAY_SIZE(rk29_camera_controls)
    
};
static int rk29_camera_probe(struct platform_device *pdev)
{
    struct rk29_camera_dev *pcdev;
    struct resource *res;
    struct rk29_camera_frmivalenum *fival_list,*fival_nxt;
    int irq,i;
    int err = 0;

    RK29CAMERA_TR("RK29 Camera driver version: v%d.%d.%d\n",(RK29_CAM_VERSION_CODE&0xff0000)>>16,
        (RK29_CAM_VERSION_CODE&0xff00)>>8,RK29_CAM_VERSION_CODE&0xff);
    
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
	pcdev->aclk_ddr_lcdc = clk_get(&pdev->dev, "aclk_ddr_lcdc");
	pcdev->aclk_disp_matrix = clk_get(&pdev->dev, "aclk_disp_matrix");

	pcdev->hclk_cpu_display = clk_get(&pdev->dev, "hclk_cpu_display");
	pcdev->vip_slave = clk_get(&pdev->dev, "vip_slave");
	pcdev->vip_out = clk_get(&pdev->dev,"vip_out");
	pcdev->vip_input = clk_get(&pdev->dev,"vip_input");
	pcdev->vip_bus = clk_get(&pdev->dev, "vip_bus");

	pcdev->hclk_disp_matrix = clk_get(&pdev->dev,"hclk_disp_matrix");
	pcdev->vip_matrix = clk_get(&pdev->dev,"vip_matrix");

	pcdev->pd_display = clk_get(&pdev->dev,"pd_display");

	pcdev->zoominfo.zoom_rate = 100;

    if (!pcdev->aclk_ddr_lcdc || !pcdev->aclk_disp_matrix ||  !pcdev->hclk_cpu_display ||
		!pcdev->vip_slave || !pcdev->vip_out || !pcdev->vip_input || !pcdev->vip_bus || !pcdev->pd_display ||
		IS_ERR(pcdev->aclk_ddr_lcdc) || IS_ERR(pcdev->aclk_disp_matrix) ||  IS_ERR(pcdev->hclk_cpu_display) || IS_ERR(pcdev->pd_display) ||
		IS_ERR(pcdev->vip_slave) || IS_ERR(pcdev->vip_out) || IS_ERR(pcdev->vip_input) || IS_ERR(pcdev->vip_bus))  {

        RK29CAMERA_TR(KERN_ERR "failed to get vip_clk(axi) source\n");
        err = -ENOENT;
        goto exit_reqmem;
    }

	if (!pcdev->hclk_disp_matrix || !pcdev->vip_matrix ||
		IS_ERR(pcdev->hclk_disp_matrix) || IS_ERR(pcdev->vip_matrix))  {

        RK29CAMERA_TR(KERN_ERR "failed to get vip_clk(ahb) source\n");
        err = -ENOENT;
        goto exit_reqmem;
    }

    dev_set_drvdata(&pdev->dev, pcdev);
    pcdev->res = res;

    pcdev->pdata = pdev->dev.platform_data;             /* ddl@rock-chips.com : Request IO in init function */
    if (pcdev->pdata && pcdev->pdata->io_init) {
        pcdev->pdata->io_init();
    }
	#ifdef CONFIG_VIDEO_RK29_WORK_IPP
	if (pcdev->pdata && (strcmp(pcdev->pdata->meminfo.name,"camera_ipp_mem")==0)) {
		pcdev->vipmem_phybase = pcdev->pdata->meminfo.start;
		pcdev->vipmem_size = pcdev->pdata->meminfo.size;
		RK29CAMERA_DG("\n%s Memory(start:0x%x size:0x%x) for IPP obtain \n",__FUNCTION__, pcdev->pdata->meminfo.start,pcdev->pdata->meminfo.size);
	} else {
		RK29CAMERA_TR("\n%s Memory for IPP have not obtain! IPP Function is fail\n",__FUNCTION__);
		pcdev->vipmem_phybase = 0;
		pcdev->vipmem_size = 0;
	}
	#endif
    INIT_LIST_HEAD(&pcdev->capture);
    spin_lock_init(&pcdev->lock);
    sema_init(&pcdev->zoominfo.sem,1);

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

	pcdev->camera_wq = create_workqueue("rk_camera_workqueue");
	if (pcdev->camera_wq == NULL)
		goto exit_free_irq;
	INIT_WORK(&pcdev->camera_reinit_work, rk29_camera_reinit_work);

    for (i=0; i<2; i++) {
        pcdev->icd_frmival[i].icd = NULL;
        pcdev->icd_frmival[i].fival_list = kzalloc(sizeof(struct rk29_camera_frmivalenum),GFP_KERNEL);
        
    }

    pcdev->soc_host.drv_name	= RK29_CAM_DRV_NAME;
    pcdev->soc_host.ops		= &rk29_soc_camera_host_ops;
    pcdev->soc_host.priv		= pcdev;
    pcdev->soc_host.v4l2_dev.dev	= &pdev->dev;
    pcdev->soc_host.nr		= pdev->id;

    err = soc_camera_host_register(&pcdev->soc_host);
    if (err)
        goto exit_free_irq;

	hrtimer_init(&pcdev->fps_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pcdev->fps_timer.function = rk29_camera_fps_func;
    pcdev->icd_cb.sensor_cb = NULL;

    RK29CAMERA_DG("%s..%s..%d  \n",__FUNCTION__,__FILE__,__LINE__);
    return 0;

exit_free_irq:
    
    for (i=0; i<2; i++) {
        fival_list = pcdev->icd_frmival[i].fival_list;
        fival_nxt = fival_list;
        while(fival_nxt != NULL) {
            fival_nxt = fival_list->nxt;
            kfree(fival_list);
            fival_list = fival_nxt;
        }
    }
    
    free_irq(pcdev->irq, pcdev);
	if (pcdev->camera_wq) {
		destroy_workqueue(pcdev->camera_wq);
		pcdev->camera_wq = NULL;
	}
exit_reqirq:
    iounmap(pcdev->base);
exit_ioremap:
    release_mem_region(res->start, res->end - res->start + 1);
exit_reqmem:
    if (pcdev->aclk_ddr_lcdc) {
		clk_put(pcdev->aclk_ddr_lcdc);
		pcdev->aclk_ddr_lcdc = NULL;
    }
	if (pcdev->aclk_disp_matrix) {
		clk_put(pcdev->aclk_disp_matrix);
		pcdev->aclk_disp_matrix = NULL;
    }
	if (pcdev->hclk_cpu_display) {
		clk_put(pcdev->hclk_cpu_display);
		pcdev->hclk_cpu_display = NULL;
    }
	if (pcdev->vip_slave) {
		clk_put(pcdev->vip_slave);
		pcdev->vip_slave = NULL;
    }
	if (pcdev->vip_out) {
		clk_put(pcdev->vip_out);
		pcdev->vip_out = NULL;
    }
	if (pcdev->vip_input) {
		clk_put(pcdev->vip_input);
		pcdev->vip_input = NULL;
    }
	if (pcdev->vip_bus) {
		clk_put(pcdev->vip_bus);
		pcdev->vip_bus = NULL;
    }
    if (pcdev->hclk_disp_matrix) {
		clk_put(pcdev->hclk_disp_matrix);
		pcdev->hclk_disp_matrix = NULL;
    }
	if (pcdev->vip_matrix) {
		clk_put(pcdev->vip_matrix);
		pcdev->vip_matrix = NULL;
    }
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
    struct rk29_camera_frmivalenum *fival_list,*fival_nxt;
    int i;
    
    free_irq(pcdev->irq, pcdev);

	if (pcdev->camera_wq) {
		destroy_workqueue(pcdev->camera_wq);
		pcdev->camera_wq = NULL;
	}

    for (i=0; i<2; i++) {
        fival_list = pcdev->icd_frmival[i].fival_list;
        fival_nxt = fival_list;
        while(fival_nxt != NULL) {
            fival_nxt = fival_list->nxt;
            kfree(fival_list);
            fival_list = fival_nxt;
        }
    }

    soc_camera_host_unregister(&pcdev->soc_host);

    res = pcdev->res;
    release_mem_region(res->start, res->end - res->start + 1);

    if (pcdev->pdata && pcdev->pdata->io_deinit) {         /* ddl@rock-chips.com : Free IO in deinit function */
        pcdev->pdata->io_deinit(0);
		pcdev->pdata->io_deinit(1);
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
    RK29CAMERA_DG("%s..%s..%d  \n",__FUNCTION__,__FILE__,__LINE__);
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
#endif
