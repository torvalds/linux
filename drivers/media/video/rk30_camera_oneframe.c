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
#if CONFIG_ARCH_RK30
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
#include <plat/rk_camera.h>
#include <mach/iomux.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf-dma-contig.h>
#include <media/soc_camera.h>
#include <media/soc_mediabus.h>
#include <mach/io.h>
#include <plat/ipp.h>


static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING"rk_camera: " fmt , ## arg); } while (0)

#define RKCAMERA_TR(format, ...) printk(KERN_ERR format, ## __VA_ARGS__)
#define RKCAMERA_DG(format, ...) dprintk(1, format, ## __VA_ARGS__)

// CIF Reg Offset
#define  CIF_CIF_CTRL                0x00
#define  CIF_CIF_INTEN                 0x04
#define  CIF_CIF_INTSTAT                  0x08
#define  CIF_CIF_FOR                      0x0c
#define  CIF_CIF_LINE_NUM_ADDR                     0x10
#define  CIF_CIF_FRM0_ADDR_Y           0x14
#define  CIF_CIF_FRM0_ADDR_UV          0x18
#define  CIF_CIF_FRM1_ADDR_Y          0x1c
#define  CIF_CIF_FRM1_ADDR_UV           0x20
#define  CIF_CIF_VIR_LINE_WIDTH          0x24
#define  CIF_CIF_SET_SIZE          0x28
#define  CIF_CIF_SCM_ADDR_Y                    0x2c
#define  CIF_CIF_SCM_ADDR_U                       0x30
#define  CIF_CIF_SCM_ADDR_V              0x34
#define  CIF_CIF_WB_UP_FILTER                     0x38
#define  CIF_CIF_WB_LOW_FILTER                      0x3c
#define  CIF_CIF_WBC_CNT                    0x40
#define  CIF_CIF_CROP                   0x44
#define  CIF_CIF_SCL_CTRL		0x48
#define	CIF_CIF_SCL_DST		0x4c
#define	CIF_CIF_SCL_FCT		0x50
#define	CIF_CIF_SCL_VALID_NUM		0x54
#define	CIF_CIF_LINE_LOOP_CTR		0x58
#define	CIF_CIF_FRAME_STATUS		0x60
#define	CIF_CIF_CUR_DST			0x64
#define	CIF_CIF_LAST_LINE			0x68
#define	CIF_CIF_LAST_PIX			0x6c

//The key register bit descrition
// CIF_CTRL Reg , ignore SCM,WBC,ISP,
#define  DISABLE_CAPTURE              (0x00<<0)
#define  ENABLE_CAPTURE               (0x01<<0)
#define  MODE_ONEFRAME			(0x00<<1)
#define  	MODE_PINGPONG		(0x01<<1)
#define 	MODE_LINELOOP		(0x02<<1)
#define  AXI_BURST_16			(0x0F << 12)

//CIF_CIF_INTEN
#define 	FRAME_END_EN			(0x01<<1)
#define 	BUS_ERR_EN				(0x01<<6)
#define	SCL_ERR_EN				(0x01<<7)

//CIF_CIF_FOR
#define  VSY_HIGH_ACTIVE              (0x01<<0)
#define  VSY_LOW_ACTIVE               (0x00<<0)
#define  HSY_LOW_ACTIVE 			  (0x01<<1)
#define  HSY_HIGH_ACTIVE			  (0x00<<1)
#define  INPUT_MODE_YUV 			(0x00<<2)
#define  INPUT_MODE_PAL 			(0x02<<2)
#define  INPUT_MODE_NTSC 			(0x03<<2)
#define  INPUT_MODE_RAW 			(0x04<<2)
#define  INPUT_MODE_JPEG 			(0x05<<2)
#define  INPUT_MODE_MIPI			(0x06<<2)
#define	YUV_INPUT_ORDER_UYVY	(0x00<<5)
#define YUV_INPUT_ORDER_YVYU		(0x01<<5)
#define YUV_INPUT_ORDER_VYUY		(0x02<<5)
#define YUV_INPUT_ORDER_YUYV		(0x03<<5)
#define YUV_INPUT_422		(0x00<<7)
#define YUV_INPUT_420		(0x01<<7)
#define INPUT_420_ORDER_EVEN		(0x00<<8)
#define INPUT_420_ORDER_ODD		(0x01<<8)
#define CCIR_INPUT_ORDER_ODD		(0x00<<9)
#define CCIR_INPUT_ORDER_EVEN		(0x01<<9)
#define RAW_DATA_WIDTH_8			(0x00<<11)
#define RAW_DATA_WIDTH_10		(0x01<<11)
#define RAW_DATA_WIDTH_12		(0x02<<11)	
#define YUV_OUTPUT_422				(0x00<<16)
#define YUV_OUTPUT_420				(0x01<<16)
#define OUTPUT_420_ORDER_EVEN		(0x00<<17)
#define OUTPUT_420_ORDER_ODD 	(0x01<<17)
#define RAWD_DATA_LITTLE_ENDIAN	(0x00<<18)
#define RAWD_DATA_BIG_ENDIAN		(0x01<<18)
#define UV_STORAGE_ORDER_UVUV	(0x00<<19)
#define UV_STORAGE_ORDER_VUVU	(0x01<<19)

//CIF_CIF_SCL_CTRL
#define ENABLE_SCL_DOWN		(0x01<<0)		
#define DISABLE_SCL_DOWN 		(0x00<<0)
#define ENABLE_SCL_UP 	(0x01<<1)		
#define DISABLE_SCL_UP		(0x00<<1)
#define ENABLE_YUV_16BIT_BYPASS	(0x01<<4)
#define DISABLE_YUV_16BIT_BYPASS	(0x00<<4)
#define ENABLE_RAW_16BIT_BYPASS (0x01<<5)
#define DISABLE_RAW_16BIT_BYPASS	(0x00<<5)
#define ENABLE_32BIT_BYPASS (0x01<<6)
#define DISABLE_32BIT_BYPASS	(0x00<<6)

//CRU,PIXCLOCK
#define CRU_PCLK_REG30 		0xbc
#define ENANABLE_INVERT_PCLK_CIF0 		((0x1<<24)|(0x1<<8))
#define DISABLE_INVERT_PCLK_CIF0		((0x1<<24)|(0x0<<8))
#define ENANABLE_INVERT_PCLK_CIF1		((0x1<<28)|(0x1<<12))
#define DISABLE_INVERT_PCLK_CIF1		((0x1<<28)|(0x0<<12))

#define MIN(x,y)   ((x<y) ? x: y)
#define MAX(x,y)    ((x>y) ? x: y)
#define RK_SENSOR_24MHZ      24           /* MHz */
#define RK_SENSOR_48MHZ      48

#define write_cif_reg(base,addr, val)  __raw_writel(val, addr+(base))
#define read_cif_reg(base,addr) __raw_readl(addr+(base))
#define mask_cif_reg(addr, msk, val)    write_cif_reg(addr, (val)|((~(msk))&read_cif_reg(addr)))

#define write_cru_reg(addr, val)  __raw_writel(val, addr+RK30_CRU_BASE)
#define read_cru_reg(addr) __raw_readl(addr+RK30_CRU_BASE)
#define mask_cru_reg(addr, msk, val)	write_cif_reg(addr+RK30_CRU_BASE, (val)|((~(msk))&read_cif_reg(addr+RK30_CRU_BASE)))

#ifdef CONFIG_VIDEO_RK29_DIGITALZOOM_IPP_OFF
#define CAM_WORKQUEUE_IS_EN()   ((pcdev->host_width != pcdev->icd->user_width) || (pcdev->host_height != pcdev->icd->user_height)\
                                  || (pcdev->icd_cb.sensor_cb))
#define CAM_IPPWORK_IS_EN()     ((pcdev->host_width != pcdev->icd->user_width) || (pcdev->host_height != pcdev->icd->user_height))                                  
#else
#define CAM_WORKQUEUE_IS_EN()  (true)
#define CAM_IPPWORK_IS_EN()     ((pcdev->zoominfo.a.c.width != pcdev->icd->user_width) || (pcdev->zoominfo.a.c.height != pcdev->icd->user_height))
#endif

#define IS_CIF0()		(pcdev->hostid == RK_CAM_PLATFORM_DEV_ID_0)
//Configure Macro
/*
*			 Driver Version Note
*
*v0.0.x : this driver is 2.6.32 kernel driver;
*v0.1.x : this driver is 3.0.8 kernel driver;
*
*v0.x.1 : this driver first support rk2918;
*v0.x.2 : fix this driver support v4l2 format is V4L2_PIX_FMT_NV12 and V4L2_PIX_FMT_NV16,is not V4L2_PIX_FMT_YUV420 
*		  and V4L2_PIX_FMT_YUV422P;
*v0.x.3 : this driver support VIDIOC_ENUM_FRAMEINTERVALS;
*v0.x.4 : this driver support digital zoom;
*v0.x.5 : this driver support test framerate and query framerate from board file configuration;
*v0.x.6 : this driver improve test framerate method;
*/
#define RK_CAM_VERSION_CODE KERNEL_VERSION(0, 2, 6)

/* limit to rk29 hardware capabilities */
#define RK_CAM_BUS_PARAM   (SOCAM_MASTER |\
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

#define RK_CAM_W_MIN        48
#define RK_CAM_H_MIN        32
#define RK_CAM_W_MAX        3856            /* ddl@rock-chips.com : 10M Pixel */
#define RK_CAM_H_MAX        2764
#define RK_CAM_FRAME_INVAL_INIT 3
#define RK_CAM_FRAME_INVAL_DC 3          /* ddl@rock-chips.com :  */

extern void videobuf_dma_contig_free(struct videobuf_queue *q, struct videobuf_buffer *buf);
extern dma_addr_t videobuf_to_dma_contig(struct videobuf_buffer *buf);

/* buffer for one video frame */
struct rk_camera_buffer
{
    /* common v4l buffer stuff -- must be first */
    struct videobuf_buffer vb;
    enum v4l2_mbus_pixelcode	code;
    int			inwork;
};
enum rk_camera_reg_state
{
	Reg_Invalidate,
	Reg_Validate
};

struct rk_camera_reg
{
	unsigned int cifCtrl;
	unsigned int cifCrop;
	unsigned int cifFs;
	unsigned int cifIntEn;
	unsigned int cifFmt;
//	unsigned int VipCrm;
	enum rk_camera_reg_state Inval;
};
struct rk_camera_work
{
	struct videobuf_buffer *vb;
	struct rk_camera_dev *pcdev;
	struct work_struct work;
};
struct rk_camera_frmivalenum
{
    struct v4l2_frmivalenum fival;
    struct rk_camera_frmivalenum *nxt;
};
struct rk_camera_frmivalinfo
{
    struct soc_camera_device *icd;
    struct rk_camera_frmivalenum *fival_list;
};
struct rk_camera_zoominfo
{
    struct semaphore sem;
    struct v4l2_crop a;
    int zoom_rate;
};
struct rk_camera_timer{
	struct rk_camera_dev *pcdev;
	struct hrtimer timer;
	};
struct rk_camera_dev
{
	struct soc_camera_host	soc_host;
	struct device		*dev;
	/* RK2827x is only supposed to handle one camera on its Quick Capture
	 * interface. If anyone ever builds hardware to enable more than
	 * one camera, they will have to modify this driver too */
	struct soc_camera_device *icd;

	//************must modify start************/
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
	//************must modify end************/
	void __iomem *base;
	int frame_inval;           /* ddl@rock-chips.com : The first frames is invalidate  */
	unsigned int irq;
	unsigned int fps;
	unsigned int pixfmt;
	//for ipp	
	unsigned int vipmem_phybase;
	unsigned int vipmem_size;
	unsigned int vipmem_bsize;

	int host_width;	//croped size
	int host_height;
	int host_left;  //sensor output size ?
	int host_top;
	int hostid;

	struct rk29camera_platform_data *pdata;
	struct resource		*res;
	struct list_head	capture;
	struct rk_camera_zoominfo zoominfo;

	spinlock_t		lock;

	struct videobuf_buffer	*active;
	struct rk_camera_reg reginfo_suspend;
	struct workqueue_struct *camera_wq;
	struct rk_camera_work *camera_work;
	unsigned int camera_work_count;
	struct rk_camera_timer fps_timer;
	struct rk_camera_work camera_reinit_work;
	int icd_init;
	rk29_camera_sensor_cb_s icd_cb;
	struct rk_camera_frmivalinfo icd_frmival[2];
};

static const struct v4l2_queryctrl rk_camera_controls[] =
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
static const char *rk_cam_driver_description = "RK_Camera";

static int rk_camera_s_stream(struct soc_camera_device *icd, int enable);


/*
 *  Videobuf operations
 */
static int rk_videobuf_setup(struct videobuf_queue *vq, unsigned int *count,
                               unsigned int *size)
{
    struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk_camera_dev *pcdev = ici->priv;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);
	int bytes_per_line_host = soc_mbus_bytes_per_line(pcdev->host_width,
						icd->current_fmt->host_fmt);

    dev_dbg(&icd->dev, "count=%d, size=%d\n", *count, *size);

	if (bytes_per_line_host < 0)
		return bytes_per_line_host;

	/* planar capture requires Y, U and V buffers to be page aligned */
	//*size = PAGE_ALIGN(bytes_per_line*icd->user_height);       /* Y pages UV pages, yuv422*/
	*size = PAGE_ALIGN(bytes_per_line*icd->user_height);	   /* Y pages UV pages, yuv422*/
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
			pcdev->camera_work = kmalloc(sizeof(struct rk_camera_work)*(*count), GFP_KERNEL);
			if (pcdev->camera_work == NULL) {
				RKCAMERA_TR("\n %s kmalloc fail\n", __FUNCTION__);
				BUG();
			}
			pcdev->camera_work_count = *count;
		}
	}

    RKCAMERA_DG("%s..%d.. videobuf size:%d, vipmem_buf size:%d, count:%d \n",__FUNCTION__,__LINE__, *size,pcdev->vipmem_size, *count);

    return 0;
}
static void rk_videobuf_free(struct videobuf_queue *vq, struct rk_camera_buffer *buf)
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
static int rk_videobuf_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb, enum v4l2_field field)
{
    struct soc_camera_device *icd = vq->priv_data;
    struct rk_camera_buffer *buf;
    int ret;
    int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);
	if (bytes_per_line < 0)
		return bytes_per_line;

    buf = container_of(vb, struct rk_camera_buffer, vb);

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
    rk_videobuf_free(vq, buf);
out:
    return ret;
}

static inline void rk_videobuf_capture(struct videobuf_buffer *vb,struct rk_camera_dev *rk_pcdev)
{
	unsigned int y_addr,uv_addr;
	struct rk_camera_dev *pcdev = rk_pcdev;

    if (vb) {
		if (CAM_WORKQUEUE_IS_EN()) {
			y_addr = pcdev->vipmem_phybase + vb->i*pcdev->vipmem_bsize;
			uv_addr = y_addr + pcdev->host_width*pcdev->host_height;
			if (y_addr > (pcdev->vipmem_phybase + pcdev->vipmem_size - pcdev->vipmem_bsize)) {
				RKCAMERA_TR("vipmem for IPP is overflow! %dx%d -> %dx%d vb_index:%d\n",pcdev->host_width,pcdev->host_height,
					          pcdev->icd->user_width,pcdev->icd->user_height, vb->i);
				BUG();
			}
		} else {
			y_addr = vb->boff;
			uv_addr = y_addr + vb->width * vb->height;
		}
        write_cif_reg(pcdev->base,CIF_CIF_FRM0_ADDR_Y, y_addr);
        write_cif_reg(pcdev->base,CIF_CIF_FRM0_ADDR_UV, uv_addr);
	//printk("y_addr = 0x%x, uv_addr = 0x%x \n",read_cif_reg(pcdev->base, CIF_CIF_FRM0_ADDR_Y),read_cif_reg(pcdev->base, CIF_CIF_FRM0_ADDR_UV));		
        write_cif_reg(pcdev->base,CIF_CIF_FRM1_ADDR_Y, y_addr);
        write_cif_reg(pcdev->base,CIF_CIF_FRM1_ADDR_UV, uv_addr);
        write_cif_reg(pcdev->base,CIF_CIF_FRAME_STATUS,  0x00000002);//frame1 has been ready to receive data,frame 2 is not used
    }
}
/* Locking: Caller holds q->irqlock */
static void rk_videobuf_queue(struct videobuf_queue *vq,
                                struct videobuf_buffer *vb)
{
    struct soc_camera_device *icd = vq->priv_data;
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk_camera_dev *pcdev = ici->priv;

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
        rk_videobuf_capture(vb,pcdev);
    }
}
static int rk_pixfmt2ippfmt(unsigned int pixfmt, int *ippfmt)
{
	switch (pixfmt)
	{
		case V4L2_PIX_FMT_NV16:
		case V4L2_PIX_FMT_NV61:
		{
			*ippfmt = IPP_Y_CBCR_H2V1;
			break;
		}
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV21:
		{
			*ippfmt = IPP_Y_CBCR_H2V2;
			break;
		}
		default:
			goto rk_pixfmt2ippfmt_err;
	}

	return 0;
rk_pixfmt2ippfmt_err:
	return -1;
}
static void rk_camera_capture_process(struct work_struct *work)
{
	struct rk_camera_work *camera_work = container_of(work, struct rk_camera_work, work);
	struct videobuf_buffer *vb = camera_work->vb;
	struct rk_camera_dev *pcdev = camera_work->pcdev;
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
    ipp_req.src_vir_w = pcdev->zoominfo.a.c.width; 
    rk_pixfmt2ippfmt(pcdev->pixfmt, &ipp_req.src0.fmt);
    ipp_req.dst0.w = pcdev->icd->user_width/scale_times;
    ipp_req.dst0.h = pcdev->icd->user_height/scale_times;
    ipp_req.dst_vir_w = pcdev->icd->user_width;        
    rk_pixfmt2ippfmt(pcdev->pixfmt, &ipp_req.dst0.fmt);
    vipdata_base = pcdev->vipmem_phybase + vb->i*pcdev->vipmem_bsize;
    src_y_size = pcdev->zoominfo.a.c.width*pcdev->zoominfo.a.c.height;
    dst_y_size = pcdev->icd->user_width*pcdev->icd->user_height;
    for (h=0; h<scale_times; h++) {
        for (w=0; w<scale_times; w++) {
            
            src_y_offset = (pcdev->zoominfo.a.c.top + h*pcdev->zoominfo.a.c.height/scale_times)* pcdev->zoominfo.a.c.width 
                        + pcdev->zoominfo.a.c.left + w*pcdev->zoominfo.a.c.width/scale_times;
		    src_uv_offset = (pcdev->zoominfo.a.c.top + h*pcdev->zoominfo.a.c.height/scale_times)* pcdev->zoominfo.a.c.width/2
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

			RKCAMERA_TR("Capture image(vb->i:0x%x) which IPP operated is error:\n",vb->i);
			RKCAMERA_TR("widx:%d hidx:%d ",w,h);
			RKCAMERA_TR("%dx%d@(%d,%d)->%dx%d\n",pcdev->zoominfo.a.c.width,pcdev->zoominfo.a.c.height,pcdev->zoominfo.a.c.left,pcdev->zoominfo.a.c.top,pcdev->icd->user_width,pcdev->icd->user_height);
			RKCAMERA_TR("ipp_req.src0.YrgbMst:0x%x ipp_req.src0.CbrMst:0x%x \n", ipp_req.src0.YrgbMst,ipp_req.src0.CbrMst);
			RKCAMERA_TR("ipp_req.src0.w:0x%x ipp_req.src0.h:0x%x \n",ipp_req.src0.w,ipp_req.src0.h);
			RKCAMERA_TR("ipp_req.src0.fmt:0x%x\n",ipp_req.src0.fmt);
			RKCAMERA_TR("ipp_req.dst0.YrgbMst:0x%x ipp_req.dst0.CbrMst:0x%x \n",ipp_req.dst0.YrgbMst,ipp_req.dst0.CbrMst);
			RKCAMERA_TR("ipp_req.dst0.w:0x%x ipp_req.dst0.h:0x%x \n",ipp_req.dst0.w ,ipp_req.dst0.h);
			RKCAMERA_TR("ipp_req.dst0.fmt:0x%x\n",ipp_req.dst0.fmt);
			RKCAMERA_TR("ipp_req.src_vir_w:0x%x ipp_req.dst_vir_w :0x%x\n",ipp_req.src_vir_w ,ipp_req.dst_vir_w);
			RKCAMERA_TR("ipp_req.timeout:0x%x ipp_req.flag :0x%x\n",ipp_req.timeout,ipp_req.flag);
                
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
static irqreturn_t rk_camera_irq(int irq, void *data)
{
    struct rk_camera_dev *pcdev = data;
    struct videobuf_buffer *vb;
	struct rk_camera_work *wk;
	write_cif_reg(pcdev->base,CIF_CIF_INTSTAT,0xFFFFFFFF);  /* clear vip interrupte single  */
    /* ddl@rock-chps.com : Current VIP is run in One Frame Mode, Frame 1 is validate */
    if (read_cif_reg(pcdev->base,CIF_CIF_FRAME_STATUS) & 0x01) {
		pcdev->fps++;
		if (!pcdev->active)
			goto RK_CAMERA_IRQ_END;

        if (pcdev->frame_inval>0) {
            pcdev->frame_inval--;
            rk_videobuf_capture(pcdev->active,pcdev);
            goto RK_CAMERA_IRQ_END;
        } else if (pcdev->frame_inval) {
        	RKCAMERA_TR("frame_inval : %0x",pcdev->frame_inval);
            pcdev->frame_inval = 0;
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
				rk_videobuf_capture(pcdev->active,pcdev);
			}
        }

        if (pcdev->active == NULL) {
			RKCAMERA_DG("%s video_buf queue is empty!\n",__FUNCTION__);
		}

		if ((vb->state == VIDEOBUF_QUEUED) || (vb->state == VIDEOBUF_ACTIVE)) {
	        vb->state = VIDEOBUF_DONE;
	        do_gettimeofday(&vb->ts);
	        vb->field_count++;
		}
		
		if (CAM_WORKQUEUE_IS_EN()) {
			wk = pcdev->camera_work + vb->i;
			INIT_WORK(&(wk->work), rk_camera_capture_process);
			wk->vb = vb;
			wk->pcdev = pcdev;
			queue_work(pcdev->camera_wq, &(wk->work));
			
		} else {
			wake_up(&vb->done);
		}
		
    }

RK_CAMERA_IRQ_END:
    return IRQ_HANDLED;
}


static void rk_videobuf_release(struct videobuf_queue *vq,
                                  struct videobuf_buffer *vb)
{
    struct rk_camera_buffer *buf = container_of(vb, struct rk_camera_buffer, vb);
    struct soc_camera_device *icd = vq->priv_data;
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk_camera_dev *pcdev = ici->priv;
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
	if (vb == pcdev->active) {
		RKCAMERA_DG("%s Wait for this video buf(0x%x) write finished!\n ",__FUNCTION__,(unsigned int)vb);
		interruptible_sleep_on_timeout(&vb->done, 100);
		RKCAMERA_DG("%s This video buf(0x%x) write finished, release now!!\n",__FUNCTION__,(unsigned int)vb);
	}
    rk_videobuf_free(vq, buf);
}

static struct videobuf_queue_ops rk_videobuf_ops =
{
    .buf_setup      = rk_videobuf_setup,
    .buf_prepare    = rk_videobuf_prepare,
    .buf_queue      = rk_videobuf_queue,
    .buf_release    = rk_videobuf_release,
};

static void rk_camera_init_videobuf(struct videobuf_queue *q,
                                      struct soc_camera_device *icd)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk_camera_dev *pcdev = ici->priv;

    /* We must pass NULL as dev pointer, then all pci_* dma operations
     * transform to normal dma_* ones. */
    videobuf_queue_dma_contig_init(q,
                                   &rk_videobuf_ops,
                                   ici->v4l2_dev.dev, &pcdev->lock,
                                   V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                   V4L2_FIELD_NONE,
                                   sizeof(struct rk_camera_buffer),
                                   icd,&icd->video_lock);
}
static int rk_camera_activate(struct rk_camera_dev *pcdev, struct soc_camera_device *icd)
{
    unsigned long sensor_bus_flags = SOCAM_MCLK_24MHZ;
    struct clk *parent;

    RKCAMERA_TR("%s..%d.. \n",__FUNCTION__,__LINE__);
	#if 0
    if (!pcdev->aclk_ddr_lcdc || !pcdev->aclk_disp_matrix ||  !pcdev->hclk_cpu_display ||
		!pcdev->vip_slave || !pcdev->vip_out || !pcdev->vip_input || !pcdev->vip_bus || !pcdev->pd_display ||
		IS_ERR(pcdev->aclk_ddr_lcdc) || IS_ERR(pcdev->aclk_disp_matrix) ||  IS_ERR(pcdev->hclk_cpu_display) || IS_ERR(pcdev->pd_display) ||
		IS_ERR(pcdev->vip_slave) || IS_ERR(pcdev->vip_out) || IS_ERR(pcdev->vip_input) || IS_ERR(pcdev->vip_bus))  {

        RKCAMERA_TR(KERN_ERR "failed to get vip_clk(axi) source\n");
        goto RK_CAMERA_ACTIVE_ERR;
    }
	RKCAMERA_TR("%s..%d.. \n",__FUNCTION__,__LINE__);
	if (!pcdev->hclk_disp_matrix || !pcdev->vip_matrix ||
		IS_ERR(pcdev->hclk_disp_matrix) || IS_ERR(pcdev->vip_matrix))  {

        RKCAMERA_TR(KERN_ERR "failed to get vip_clk(ahb) source\n");
        goto RK_CAMERA_ACTIVE_ERR;
    }
	clk_enable(pcdev->pd_display);

	clk_enable(pcdev->aclk_ddr_lcdc);
	clk_enable(pcdev->aclk_disp_matrix);

	clk_enable(pcdev->hclk_cpu_display);
	clk_enable(pcdev->vip_slave);
	RK29CAMERA_TR("%s..%d.. \n",__FUNCTION__,__LINE__);

	clk_enable(pcdev->vip_input);
	clk_enable(pcdev->vip_bus);

    //if (icd->ops->query_bus_param)                                                  /* ddl@rock-chips.com : Query Sensor's xclk */
        //sensor_bus_flags = icd->ops->query_bus_param(icd);

    if (sensor_bus_flags & SOCAM_MCLK_48MHZ) {
        parent = clk_get(NULL, "clk48m");
        if (!parent || IS_ERR(parent))
             goto RK_CAMERA_ACTIVE_ERR;
    } else if (sensor_bus_flags & SOCAM_MCLK_27MHZ) {
        parent = clk_get(NULL, "extclk");
        if (!parent || IS_ERR(parent))
             goto RK_CAMERA_ACTIVE_ERR;
    } else {
        parent = clk_get(NULL, "xin24m");
        if (!parent || IS_ERR(parent))
             goto RK_CAMERA_ACTIVE_ERR;
    }
    clk_set_parent(pcdev->vip_out, parent);

    clk_enable(pcdev->vip_out);
   // rk30_mux_api_set(GPIO1B4_VIPCLKOUT_NAME, GPIO1L_VIP_CLKOUT);
    ndelay(10);

	ndelay(10);
//    write_vip_reg(pcdev->base,RK29_VIP_RESET, 0x76543210);  /* ddl@rock-chips.com : vip software reset */
//    udelay(10);
#endif
    write_cif_reg(pcdev->base,CIF_CIF_CTRL,AXI_BURST_16|MODE_ONEFRAME|DISABLE_CAPTURE);   /* ddl@rock-chips.com : vip ahb burst 16 */
    write_cif_reg(pcdev->base,CIF_CIF_INTEN, 0x01);    //capture complete interrupt enable
   RKCAMERA_TR("%s..%d.. CIF_CIF_CTRL = 0x%x\n",__FUNCTION__,__LINE__,read_cif_reg(pcdev->base, CIF_CIF_CTRL));
    return 0;
RK_CAMERA_ACTIVE_ERR:
    return -ENODEV;
}

static void rk_camera_deactivate(struct rk_camera_dev *pcdev)
{
    //pcdev->active = NULL;
#if 0
    write_cif_reg(pcdev->base,CIF_CIF_CTRL, 0);
    read_cif_reg(pcdev->base,CIF_CIF_INTSTAT);             //clear vip interrupte single

//    rk29_mux_api_set(GPIO1B4_VIPCLKOUT_NAME, GPIO1L_GPIO1B4);
    clk_disable(pcdev->vip_out);

	clk_disable(pcdev->vip_input);
	clk_disable(pcdev->vip_bus);


	clk_disable(pcdev->hclk_cpu_display);
	clk_disable(pcdev->vip_slave);

	clk_disable(pcdev->aclk_ddr_lcdc);
	clk_disable(pcdev->aclk_disp_matrix);

	clk_disable(pcdev->pd_display);
	#endif
    return;
}

/* The following two functions absolutely depend on the fact, that
 * there can be only one camera on RK28 quick capture interface */
static int rk_camera_add_device(struct soc_camera_device *icd)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk_camera_dev *pcdev = ici->priv;
    struct device *control = to_soc_camera_control(icd);
    struct v4l2_subdev *sd;
    int ret,i,icd_catch;
    struct rk_camera_frmivalenum *fival_list,*fival_nxt;
    
    mutex_lock(&camera_lock);

    if (pcdev->icd) {
        ret = -EBUSY;
        goto ebusy;
    }

    dev_info(&icd->dev, "RK Camera driver attached to camera%d(%s)\n",
             icd->devnum,dev_name(icd->pdev));

	pcdev->frame_inval = RK_CAM_FRAME_INVAL_INIT;
    pcdev->active = NULL;
    pcdev->icd = NULL;
	pcdev->reginfo_suspend.Inval = Reg_Invalidate;
    pcdev->zoominfo.zoom_rate = 100;
        
	/* ddl@rock-chips.com: capture list must be reset, because this list may be not empty,
     * if app havn't dequeue all videobuf before close camera device;
	*/
    INIT_LIST_HEAD(&pcdev->capture);

    ret = rk_camera_activate(pcdev,icd);
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
        pcdev->icd_frmival[0].fival_list = kzalloc(sizeof(struct rk_camera_frmivalenum),GFP_KERNEL);
    }
	RKCAMERA_TR("%s..%d.. \n",__FUNCTION__,__LINE__);
ebusy:
    mutex_unlock(&camera_lock);

    return ret;
}
static void rk_camera_remove_device(struct soc_camera_device *icd)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk_camera_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);

	mutex_lock(&camera_lock);
    BUG_ON(icd != pcdev->icd);

    dev_info(&icd->dev, "RK Camera driver detached from camera%d(%s)\n",
             icd->devnum,dev_name(icd->pdev));

	/* ddl@rock-chips.com: Application will call VIDIOC_STREAMOFF before close device, but
	   stream may be turn on again before close device, if suspend and resume happened. */
	if (read_cif_reg(pcdev->base,CIF_CIF_CTRL) & ENABLE_CAPTURE) {
		rk_camera_s_stream(icd,0);
	}

    v4l2_subdev_call(sd, core, ioctl, RK29_CAM_SUBDEV_DEACTIVATE,NULL);
	rk_camera_deactivate(pcdev);

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
	RKCAMERA_DG("%s exit\n",__FUNCTION__);

	return;
}
static int rk_camera_set_bus_param(struct soc_camera_device *icd, __u32 pixfmt)
{
    unsigned long bus_flags, camera_flags, common_flags;
    unsigned int cif_ctrl_val = 0;
	const struct soc_mbus_pixelfmt *fmt;
	int ret = 0;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct rk_camera_dev *pcdev = ici->priv;
    RKCAMERA_DG("%s..%d..\n",__FUNCTION__,__LINE__);

	fmt = soc_mbus_get_fmtdesc(icd->current_fmt->code);
	if (!fmt)
		return -EINVAL;

    bus_flags = RK_CAM_BUS_PARAM;
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
        goto RK_CAMERA_SET_BUS_PARAM_END;
    }

    ret = icd->ops->set_bus_param(icd, common_flags);
    if (ret < 0)
        goto RK_CAMERA_SET_BUS_PARAM_END;

    cif_ctrl_val = read_cif_reg(pcdev->base,CIF_CIF_FOR);
	RKCAMERA_DG("%s..%d..cif_ctrl_val = 0x%x\n",__FUNCTION__,__LINE__,cif_ctrl_val);
    if (common_flags & SOCAM_PCLK_SAMPLE_FALLING) {
   	if(IS_CIF0())
   		{
		RKCAMERA_DG("%s..%d.. before set CRU_PCLK_REG30 = 0X%x\n",__FUNCTION__,__LINE__,read_cru_reg(CRU_PCLK_REG30));
		write_cru_reg(CRU_PCLK_REG30, read_cru_reg(CRU_PCLK_REG30) | ENANABLE_INVERT_PCLK_CIF0);
		RKCAMERA_DG("%s..%d..  after set CRU_PCLK_REG30 = 0X%x\n",__FUNCTION__,__LINE__,read_cru_reg(CRU_PCLK_REG30));
   		}
	else
		{
		write_cru_reg(CRU_PCLK_REG30, read_cru_reg(CRU_PCLK_REG30) | ENANABLE_INVERT_PCLK_CIF1);
		}
    } else {
		if(IS_CIF0())
			{
			RKCAMERA_DG("%s..%d.. before set CRU_PCLK_REG30 = 0X%x\n",__FUNCTION__,__LINE__,read_cru_reg(CRU_PCLK_REG30));
			write_cru_reg(CRU_PCLK_REG30, (read_cru_reg(CRU_PCLK_REG30) & 0xFFFEFFF ) | DISABLE_INVERT_PCLK_CIF0);
			RKCAMERA_DG("%s..%d..  after set CRU_PCLK_REG30 = 0X%x\n",__FUNCTION__,__LINE__,read_cru_reg(CRU_PCLK_REG30));
			}
		else
			{
			write_cru_reg(CRU_PCLK_REG30, (read_cru_reg(CRU_PCLK_REG30) & 0xFFFEFFF) | DISABLE_INVERT_PCLK_CIF1);
			}
    }
    if (common_flags & SOCAM_HSYNC_ACTIVE_LOW) {
        cif_ctrl_val |= HSY_LOW_ACTIVE;
    } else {
		cif_ctrl_val &= ~HSY_LOW_ACTIVE;
    }
    if (common_flags & SOCAM_VSYNC_ACTIVE_HIGH) {
        cif_ctrl_val |= VSY_HIGH_ACTIVE;
    } else {
		cif_ctrl_val &= ~VSY_HIGH_ACTIVE;
    }

    /* ddl@rock-chips.com : Don't enable capture here, enable in stream_on */
    //vip_ctrl_val |= ENABLE_CAPTURE;
    write_cif_reg(pcdev->base,CIF_CIF_FOR, cif_ctrl_val);
    RKCAMERA_DG("%s..ctrl:0x%x CIF_CIF_FOR=%x  \n",__FUNCTION__,cif_ctrl_val,read_cif_reg(pcdev->base,CIF_CIF_FOR));

RK_CAMERA_SET_BUS_PARAM_END:
	if (ret)
    	RKCAMERA_TR("\n%s..%d.. ret = %d \n",__FUNCTION__,__LINE__, ret);
    return ret;
}

static int rk_camera_try_bus_param(struct soc_camera_device *icd, __u32 pixfmt)
{
    unsigned long bus_flags, camera_flags;
    int ret;

    bus_flags = RK_CAM_BUS_PARAM;
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

static const struct soc_mbus_pixelfmt rk_camera_formats[] = {
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
		.fourcc 		= V4L2_PIX_FMT_NV21,
		.name			= "YUV420 NV21",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_1_5X8,
		.order			= SOC_MBUS_ORDER_LE,
	},{
		.fourcc 		= V4L2_PIX_FMT_NV61,
		.name			= "YUV422 NV61",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	}
};

static void rk_camera_setup_format(struct soc_camera_device *icd, __u32 host_pixfmt, enum v4l2_mbus_pixelcode icd_code, struct v4l2_rect *rect)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk_camera_dev *pcdev = ici->priv;
    unsigned int cif_fs = 0,cif_crop = 0;
    unsigned int cif_fmt_val = INPUT_MODE_YUV|YUV_INPUT_422|INPUT_420_ORDER_EVEN|OUTPUT_420_ORDER_EVEN;
    switch (host_pixfmt)
    {
        case V4L2_PIX_FMT_NV16:
            cif_fmt_val |= YUV_OUTPUT_422;
		cif_fmt_val |= UV_STORAGE_ORDER_UVUV;
		pcdev->frame_inval = RK_CAM_FRAME_INVAL_DC;
		pcdev->pixfmt = host_pixfmt;
            break;
	case V4L2_PIX_FMT_NV61:
		cif_fmt_val |= YUV_OUTPUT_422;
		cif_fmt_val |= UV_STORAGE_ORDER_VUVU;
		pcdev->frame_inval = RK_CAM_FRAME_INVAL_DC;
		pcdev->pixfmt = host_pixfmt;
		break;
        case V4L2_PIX_FMT_NV12:
            cif_fmt_val |= YUV_OUTPUT_420;
		cif_fmt_val |= UV_STORAGE_ORDER_UVUV;
			if (pcdev->frame_inval != RK_CAM_FRAME_INVAL_INIT)
				pcdev->frame_inval = RK_CAM_FRAME_INVAL_INIT;
			pcdev->pixfmt = host_pixfmt;
            break;
	case V4L2_PIX_FMT_NV21:
		cif_fmt_val |= YUV_OUTPUT_420;
		cif_fmt_val |= UV_STORAGE_ORDER_VUVU;
		if (pcdev->frame_inval != RK_CAM_FRAME_INVAL_INIT)
			pcdev->frame_inval = RK_CAM_FRAME_INVAL_INIT;
		pcdev->pixfmt = host_pixfmt;
		break;
        default:                                                                                /* ddl@rock-chips.com : vip output format is hold when pixfmt is invalidate */
			cif_fmt_val |= YUV_OUTPUT_422;
            break;
    }
    switch (icd_code)
    {
        case V4L2_MBUS_FMT_UYVY8_2X8:
            cif_fmt_val |= YUV_INPUT_ORDER_UYVY;
            break;
        case V4L2_MBUS_FMT_YUYV8_2X8:
            cif_fmt_val |= YUV_INPUT_ORDER_YUYV;
            break;
	case V4L2_MBUS_FMT_YVYU8_2X8:
		cif_fmt_val |= YUV_INPUT_ORDER_YVYU;
		break;
	case V4L2_MBUS_FMT_VYUY8_2X8:
		cif_fmt_val |= YUV_INPUT_ORDER_VYUY;
		break;
        default :
			cif_fmt_val |= YUV_INPUT_ORDER_YUYV;
            break;
    }
    write_cif_reg(pcdev->base,CIF_CIF_FOR, read_cif_reg(pcdev->base,CIF_CIF_FOR) |cif_fmt_val);         /* ddl@rock-chips.com: VIP capture mode and capture format must be set before FS register set */

   // read_cif_reg(pcdev->base,CIF_CIF_INTSTAT);                     /* clear vip interrupte single  */
   write_cif_reg(pcdev->base,CIF_CIF_INTSTAT,0xFFFFFFFF); 
	if((read_cif_reg(pcdev->base,CIF_CIF_CTRL) & MODE_PINGPONG)
		||(read_cif_reg(pcdev->base,CIF_CIF_CTRL) & MODE_LINELOOP)) // it is one frame mode
	{
		BUG();
	}
       else{ // this is one frame mode
		cif_crop = (rect->left+ (rect->top<<16));
		cif_fs	= ((rect->width ) + (rect->height<<16));
	 }
	RKCAMERA_TR("%s..%d.. \n",__FUNCTION__,__LINE__);

	write_cif_reg(pcdev->base,CIF_CIF_CROP, cif_crop);
	write_cif_reg(pcdev->base,CIF_CIF_SET_SIZE, cif_fs);
	write_cif_reg(pcdev->base,CIF_CIF_VIR_LINE_WIDTH, rect->width);
	write_cif_reg(pcdev->base,CIF_CIF_FRAME_STATUS,  0x00000003);
	//MUST bypass scale 
	write_cif_reg(pcdev->base,CIF_CIF_SCL_CTRL,0x10);
	//pcdev->host_width = rect->width;
//	pcdev->host_height = rect->height;
    RKCAMERA_DG("%s.. crop:0x%x fs:0x%x cif_fmt_val:0x%x CIF_CIF_FOR:0x%x\n",__FUNCTION__,cif_crop,cif_fs,cif_fmt_val,read_cif_reg(pcdev->base,CIF_CIF_FOR));
	return;
}

static int rk_camera_get_formats(struct soc_camera_device *icd, unsigned int idx,
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

    ret = rk_camera_try_bus_param(icd, fmt->bits_per_sample);
    if (ret < 0)
        return 0;

    switch (code) {
        case V4L2_MBUS_FMT_UYVY8_2X8:
        case V4L2_MBUS_FMT_YUYV8_2X8:
	 case V4L2_MBUS_FMT_YVYU8_2X8:
	 case V4L2_MBUS_FMT_VYUY8_2X8:
            formats++;
            if (xlate) {
                xlate->host_fmt = &rk_camera_formats[0];
                xlate->code	= code;
                xlate++;
                dev_dbg(dev, "Providing format %s using code %d\n",
                	rk_camera_formats[0].name,code);
            }

            formats++;
            if (xlate) {
                xlate->host_fmt = &rk_camera_formats[1];
                xlate->code	= code;
                xlate++;
                dev_dbg(dev, "Providing format %s using code %d\n",
                	rk_camera_formats[1].name,code);
            }

            formats++;
            if (xlate) {
                xlate->host_fmt = &rk_camera_formats[2];
                xlate->code	= code;
                xlate++;
                dev_dbg(dev, "Providing format %s using code %d\n",
                	rk_camera_formats[2].name,code);
            } 

            formats++;
            if (xlate) {
                xlate->host_fmt = &rk_camera_formats[3];
                xlate->code	= code;
                xlate++;
                dev_dbg(dev, "Providing format %s using code %d\n",
                	rk_camera_formats[3].name,code);
            }
			break;		
        default:
            break;
    }

    return formats;
}

static void rk_camera_put_formats(struct soc_camera_device *icd)
{
	return;
}

static int rk_camera_set_crop(struct soc_camera_device *icd,
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

        v4l_bound_align_image(&mf.width, RK_CAM_W_MIN, RK_CAM_W_MAX, 1,
            &mf.height, RK_CAM_H_MIN, RK_CAM_H_MAX, 0,
            fourcc == V4L2_PIX_FMT_NV16 ?4 : 0);

        ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
        if (ret < 0)
            return ret;
    }

    rk_camera_setup_format(icd, fourcc, mf.code, &a->c);

    icd->user_width = mf.width;
    icd->user_height = mf.height;

    return 0;
}

static int rk_camera_set_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
    struct device *dev = icd->dev.parent;
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
    const struct soc_camera_format_xlate *xlate = NULL;
	struct soc_camera_host *ici =to_soc_camera_host(icd->dev.parent);
    struct rk_camera_dev *pcdev = ici->priv;
    struct v4l2_pix_format *pix = &f->fmt.pix;
    struct v4l2_mbus_framefmt mf;
    struct v4l2_rect rect;
    int ret,usr_w,usr_h;
    int stream_on = 0;

	usr_w = pix->width;
	usr_h = pix->height;
    RKCAMERA_TR("%s enter width:%d  height:%d\n",__FUNCTION__,usr_w,usr_h);
    xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
    if (!xlate) {
        dev_err(dev, "Format %x not found\n", pix->pixelformat);
        ret = -EINVAL;
        goto RK_CAMERA_SET_FMT_END;
    }
    
    /* ddl@rock-chips.com: sensor init code transmit in here after open */
    if (pcdev->icd_init == 0) {
        v4l2_subdev_call(sd,core, init, (u32)pcdev->pdata);        
        pcdev->icd_init = 1;
    }
    stream_on = read_cif_reg(pcdev->base,CIF_CIF_CTRL);
    if (stream_on & ENABLE_CAPTURE)
        write_cif_reg(pcdev->base,CIF_CIF_CTRL, (stream_on & (~ENABLE_CAPTURE)));
    
	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;
	ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);

	if (mf.code != xlate->code)
		return -EINVAL;
	#ifdef CONFIG_VIDEO_RK29_WORK_IPP
	if ((mf.width != usr_w) || (mf.height != usr_h)) {
	  int ratio;
        if (unlikely((mf.width <16) || (mf.width > 8190) || (mf.height < 16) || (mf.height > 8190))) {
    		RKCAMERA_TR("Senor and IPP both invalid source resolution(%dx%d)\n",mf.width,mf.height);
    		ret = -EINVAL;
    		goto RK_CAMERA_SET_FMT_END;
    	}    	
    	if (unlikely((usr_w <16)||(usr_h < 16))) {
    		RKCAMERA_TR("Senor and IPP both invalid destination resolution(%dx%d)\n",usr_w,usr_h);
    		ret = -EINVAL;
            goto RK_CAMERA_SET_FMT_END;
    	}
		//need crop ?
		if((mf.width*10/mf.height) != (usr_w*10/usr_h)){
			ratio = ((mf.width*10/usr_w) >= (mf.height*10/usr_h))?(mf.height*10/usr_h):(mf.width*10/usr_w);
			pcdev->host_width = ratio*usr_w/10;
			pcdev->host_height = ratio*usr_h/10;
			printk("ratio = %d ,host:%d*%d\n",ratio,pcdev->host_width,pcdev->host_height);
			}
		else{ // needn't crop ,just scaled by ipp
			pcdev->host_width = usr_w;
			pcdev->host_height = usr_h;
			}
	}
	else{
		pcdev->host_width = usr_w;
		pcdev->host_height = usr_h;
		}
	#else
	//according to crop and scale capability to change , here just cropt to user needed
	pcdev->host_width = usr_w;
	pcdev->host_height = usr_h;
	#endif
    icd->sense = NULL;
    if (!ret) {
	rect.left = ((mf.width - pcdev->host_width )>>1)&(~0x01);
	rect.top = ((mf.height - pcdev->host_height )>>1)&(~0x01);
	pcdev->host_left = rect.left;
	pcdev->host_top = rect.top;
      //  rect.left = 0;
      //  rect.top = 0;
        rect.width = pcdev->host_width;
        rect.height = pcdev->host_height;
	  RKCAMERA_DG("%s..%d.. host:%d*%d , sensor output:%d*%d,user demand:%d*%d\n",__FUNCTION__,__LINE__,
	  	pcdev->host_width,pcdev->host_height,mf.width,mf.height,usr_w,usr_h);
        down(&pcdev->zoominfo.sem);        
        pcdev->zoominfo.a.c.width = rect.width*100/pcdev->zoominfo.zoom_rate;
		pcdev->zoominfo.a.c.width &= ~0x03;
		pcdev->zoominfo.a.c.height = rect.height*100/pcdev->zoominfo.zoom_rate;
		pcdev->zoominfo.a.c.height &= ~0x03;
		//pcdev->zoominfo.a.c.left = ((rect.width - pcdev->zoominfo.a.c.width)>>1)&(~0x01);
		//pcdev->zoominfo.a.c.top = ((rect.height - pcdev->zoominfo.a.c.height)>>1)&(~0x01);
		pcdev->zoominfo.a.c.left = 0;
		pcdev->zoominfo.a.c.top = 0;
        up(&pcdev->zoominfo.sem);

        /* ddl@rock-chips.com: IPP work limit check */
        if ((pcdev->zoominfo.a.c.width != usr_w) || (pcdev->zoominfo.a.c.height != usr_h)) {
            if (usr_w > 0x7f0) {
                if (((usr_w>>1)&0x3f) && (((usr_w>>1)&0x3f) <= 8)) {
                    RKCAMERA_TR("IPP Destination resolution(%dx%d, ((%d div 1) mod 64)=%d is <= 8)",usr_w,usr_h, usr_w, (int)((usr_w>>1)&0x3f));
                    ret = -EINVAL;
                    goto RK_CAMERA_SET_FMT_END;
                }
            } else {
                if ((usr_w&0x3f) && ((usr_w&0x3f) <= 8)) {
                    RKCAMERA_TR("IPP Destination resolution(%dx%d, %d mod 64=%d is <= 8)",usr_w,usr_h, usr_w, (int)(usr_w&0x3f));
                    ret = -EINVAL;
                    goto RK_CAMERA_SET_FMT_END;
                }
            }
        }
        RKCAMERA_DG("%s..%s icd width:%d  host width:%d (zoom: %dx%d@(%d,%d)->%dx%d)\n",__FUNCTION__,xlate->host_fmt->name,
			           rect.width, pix->width, pcdev->zoominfo.a.c.width,pcdev->zoominfo.a.c.height, pcdev->zoominfo.a.c.left,pcdev->zoominfo.a.c.top,
			           pix->width, pix->height);
        rk_camera_setup_format(icd, pix->pixelformat, mf.code, &rect); 
        
		if (CAM_IPPWORK_IS_EN()) {
			BUG_ON(pcdev->vipmem_phybase == 0);
		}
        pix->width = mf.width;
    	pix->height = mf.height;
    	pix->field = mf.field;
    	pix->colorspace = mf.colorspace;
    	icd->current_fmt = xlate;        
    }

RK_CAMERA_SET_FMT_END:
    if (stream_on & ENABLE_CAPTURE)
        write_cif_reg(pcdev->base,CIF_CIF_CTRL, (read_cif_reg(pcdev->base,CIF_CIF_CTRL) | ENABLE_CAPTURE));
	if (ret)
    	RKCAMERA_TR("\n%s..%d.. ret = %d  \n",__FUNCTION__,__LINE__, ret);
    return ret;
}
static bool rk_camera_fmt_capturechk(struct v4l2_format *f)
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
		RKCAMERA_DG("%s %dx%d is capture format\n", __FUNCTION__, f->fmt.pix.width, f->fmt.pix.height);
	return ret;
}
static int rk_camera_try_fmt(struct soc_camera_device *icd,
                                   struct v4l2_format *f)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct rk_camera_dev *pcdev = ici->priv;
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
    const struct soc_camera_format_xlate *xlate;
    struct v4l2_pix_format *pix = &f->fmt.pix;
    __u32 pixfmt = pix->pixelformat;
    int ret,usr_w,usr_h,i;
	bool is_capture = rk_camera_fmt_capturechk(f);
	bool vipmem_is_overflow = false;
    struct v4l2_mbus_framefmt mf;

	usr_w = pix->width;
	usr_h = pix->height;
	RKCAMERA_DG("%s enter width:%d  height:%d\n",__FUNCTION__,usr_w,usr_h);

    xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
    if (!xlate) {
        dev_err(icd->dev.parent, "Format (%c%c%c%c) not found\n", pixfmt & 0xFF, (pixfmt >> 8) & 0xFF,
			(pixfmt >> 16) & 0xFF, (pixfmt >> 24) & 0xFF);
        ret = -EINVAL;
        RKCAMERA_TR("%s(version:%c%c%c) support format:\n",rk_cam_driver_description,(RK_CAM_VERSION_CODE&0xff0000)>>16,
            (RK_CAM_VERSION_CODE&0xff00)>>8,(RK_CAM_VERSION_CODE&0xff));
        for (i = 0; i < icd->num_user_formats; i++)
		    RKCAMERA_TR("(%c%c%c%c)-%s\n",
		    icd->user_formats[i].host_fmt->fourcc & 0xFF, (icd->user_formats[i].host_fmt->fourcc >> 8) & 0xFF,
			(icd->user_formats[i].host_fmt->fourcc >> 16) & 0xFF, (icd->user_formats[i].host_fmt->fourcc >> 24) & 0xFF,
			icd->user_formats[i].host_fmt->name);
        goto RK_CAMERA_TRY_FMT_END;
    }
   /* limit to rk29 hardware capabilities */
    v4l_bound_align_image(&pix->width, RK_CAM_W_MIN, RK_CAM_W_MAX, 1,
    	      &pix->height, RK_CAM_H_MIN, RK_CAM_H_MAX, 0,
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
		goto RK_CAMERA_TRY_FMT_END;
    RKCAMERA_DG("%s mf.width:%d  mf.height:%d\n",__FUNCTION__,mf.width,mf.height);
	#ifdef CONFIG_VIDEO_RK29_WORK_IPP       
	if ((mf.width > usr_w) && (mf.height > usr_h)) {
		if (is_capture) {
			vipmem_is_overflow = (PAGE_ALIGN(pix->bytesperline*pix->height) > pcdev->vipmem_size);
		} else {
			/* Assume preview buffer minimum is 4 */
			vipmem_is_overflow = (PAGE_ALIGN(pix->bytesperline*pix->height)*4 > pcdev->vipmem_size);
		}
		if (vipmem_is_overflow == false) {
			pix->width = usr_w;
			pix->height = usr_h;
		} else {
			RKCAMERA_TR("vipmem for IPP is overflow, This resolution(%dx%d -> %dx%d) is invalidate!\n",mf.width,mf.height,usr_w,usr_h);
            pix->width = mf.width;
            pix->height = mf.height;            
		}
	} else if ((mf.width < usr_w) && (mf.height < usr_h)) {
		if (((usr_w>>1) < mf.width) && ((usr_h>>1) < mf.height)) {
			if (is_capture) {
				vipmem_is_overflow = (PAGE_ALIGN(pix->bytesperline*pix->height) > pcdev->vipmem_size);
			} else {
				vipmem_is_overflow = (PAGE_ALIGN(pix->bytesperline*pix->height)*4 > pcdev->vipmem_size);
			}
			if (vipmem_is_overflow == false) {
				pix->width = usr_w;
				pix->height = usr_h;
			} else {
				RKCAMERA_TR("vipmem for IPP is overflow, This resolution(%dx%d -> %dx%d) is invalidate!\n",mf.width,mf.height,usr_w,usr_h);
		                pix->width = mf.width;
		                pix->height = mf.height;
			}
		} else {
			RKCAMERA_TR("The aspect ratio(%dx%d/%dx%d) is bigger than 2 !\n",mf.width,mf.height,usr_w,usr_h);
		            pix->width = mf.width;
		            pix->height = mf.height;
		}
	}
	#else
	//need to change according to crop and scale capablicity
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
		goto RK_CAMERA_TRY_FMT_END;
	}

RK_CAMERA_TRY_FMT_END:
	if (ret)
    	RKCAMERA_TR("\n%s..%d.. ret = %d  \n",__FUNCTION__,__LINE__, ret);
    return ret;
}

static int rk_camera_reqbufs(struct soc_camera_device *icd,
                               struct v4l2_requestbuffers *p)
{
    int i;

    /* This is for locking debugging only. I removed spinlocks and now I
     * check whether .prepare is ever called on a linked buffer, or whether
     * a dma IRQ can occur for an in-work or unlinked buffer. Until now
     * it hadn't triggered */
    for (i = 0; i < p->count; i++) {
        struct rk_camera_buffer *buf = container_of(icd->vb_vidq.bufs[i],
                                                           struct rk_camera_buffer, vb);
        buf->inwork = 0;
        INIT_LIST_HEAD(&buf->vb.queue);
    }

    return 0;
}

static unsigned int rk_camera_poll(struct file *file, poll_table *pt)
{
    struct soc_camera_device *icd = file->private_data;
    struct rk_camera_buffer *buf;

    buf = list_entry(icd->vb_vidq.stream.next, struct rk_camera_buffer,
                    vb.stream);

    poll_wait(file, &buf->vb.done, pt);

    if (buf->vb.state == VIDEOBUF_DONE ||
            buf->vb.state == VIDEOBUF_ERROR)
        return POLLIN|POLLRDNORM;

    return 0;
}

static int rk_camera_querycap(struct soc_camera_host *ici,
                                struct v4l2_capability *cap)
{
    struct rk_camera_dev *pcdev = ici->priv;
    char orientation[5];

    strlcpy(cap->card, dev_name(pcdev->icd->pdev), sizeof(cap->card));    
    if (strcmp(dev_name(pcdev->icd->pdev), pcdev->pdata->info[0].dev_name) == 0) {
        sprintf(orientation,"-%d",pcdev->pdata->info[0].orientation);
    } else {
        sprintf(orientation,"-%d",pcdev->pdata->info[1].orientation);
    }
    strcat(cap->card,orientation); 
    cap->version = RK_CAM_VERSION_CODE;
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

    return 0;
}

static int rk_camera_suspend(struct soc_camera_device *icd, pm_message_t state)
{
    struct soc_camera_host *ici =
                    to_soc_camera_host(icd->dev.parent);
    struct rk_camera_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd;
    int ret = 0,tmp;

	mutex_lock(&camera_lock);
	if ((pcdev->icd == icd) && (icd->ops->suspend)) {
		rk_camera_s_stream(icd, 0);
		sd = soc_camera_to_subdev(icd);
		v4l2_subdev_call(sd, video, s_stream, 0);
		ret = icd->ops->suspend(icd, state);

		pcdev->reginfo_suspend.cifCtrl = read_cif_reg(pcdev->base,CIF_CIF_CTRL);
		pcdev->reginfo_suspend.cifCrop = read_cif_reg(pcdev->base,CIF_CIF_CROP);
		pcdev->reginfo_suspend.cifFs = read_cif_reg(pcdev->base,CIF_CIF_SET_SIZE);
		pcdev->reginfo_suspend.cifIntEn = read_cif_reg(pcdev->base,CIF_CIF_INTEN);
		pcdev->reginfo_suspend.cifFmt= read_cif_reg(pcdev->base,CIF_CIF_FOR);
		//pcdev->reginfo_suspend.VipCrm = read_vip_reg(pcdev->base,RK29_VIP_CRM);

		tmp = pcdev->reginfo_suspend.cifFs>>16;		/* ddl@rock-chips.com */
		tmp += pcdev->reginfo_suspend.cifCrop>>16;
		pcdev->reginfo_suspend.cifFs = (pcdev->reginfo_suspend.cifFs & 0xffff) | (tmp<<16);

		pcdev->reginfo_suspend.Inval = Reg_Validate;
		rk_camera_deactivate(pcdev);

		RKCAMERA_DG("%s Enter Success...\n", __FUNCTION__);
	} else {
		RKCAMERA_DG("%s icd has been deattach, don't need enter suspend\n", __FUNCTION__);
	}
	mutex_unlock(&camera_lock);
    return ret;
}

static int rk_camera_resume(struct soc_camera_device *icd)
{
    struct soc_camera_host *ici =
                    to_soc_camera_host(icd->dev.parent);
    struct rk_camera_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd;
    int ret = 0;

	mutex_lock(&camera_lock);
	if ((pcdev->icd == icd) && (icd->ops->resume)) {
		if (pcdev->reginfo_suspend.Inval == Reg_Validate) {
			rk_camera_activate(pcdev, icd);
			write_cif_reg(pcdev->base,CIF_CIF_INTEN, pcdev->reginfo_suspend.cifIntEn);
			//write_cif_reg(pcdev->base,RK29_VIP_CRM, pcdev->reginfo_suspend.VipCrm);
			write_cif_reg(pcdev->base,CIF_CIF_CTRL, pcdev->reginfo_suspend.cifCtrl&~ENABLE_CAPTURE);
			write_cif_reg(pcdev->base,CIF_CIF_CROP, pcdev->reginfo_suspend.cifCrop);
			write_cif_reg(pcdev->base,CIF_CIF_SET_SIZE, pcdev->reginfo_suspend.cifFs);
			write_cif_reg(pcdev->base,CIF_CIF_FOR, pcdev->reginfo_suspend.cifFmt);
			
			rk_videobuf_capture(pcdev->active,pcdev);
			rk_camera_s_stream(icd, 1);
			pcdev->reginfo_suspend.Inval = Reg_Invalidate;
		} else {
			RKCAMERA_TR("Resume fail, vip register recored is invalidate!!\n");
			goto rk_camera_resume_end;
		}

		ret = icd->ops->resume(icd);
		sd = soc_camera_to_subdev(icd);
		v4l2_subdev_call(sd, video, s_stream, 1);

		RKCAMERA_DG("%s Enter success\n",__FUNCTION__);
	} else {
		RKCAMERA_DG("%s icd has been deattach, don't need enter resume\n", __FUNCTION__);
	}

rk_camera_resume_end:
	mutex_unlock(&camera_lock);
    return ret;
}

static void rk_camera_reinit_work(struct work_struct *work)
{
	struct device *control;
    struct v4l2_subdev *sd;
	struct v4l2_mbus_framefmt mf;
	const struct soc_camera_format_xlate *xlate;
	int ret;
	struct rk_camera_work *camera_work = container_of(work, struct rk_camera_work, work);
	struct rk_camera_dev *pcdev = camera_work->pcdev;
	//dump regs
	{
		RKCAMERA_TR("CIF_CIF_CTRL = 0x%x\n",read_cif_reg(pcdev->base,CIF_CIF_CTRL));
		RKCAMERA_TR("CIF_CIF_INTEN = 0x%x\n",read_cif_reg(pcdev->base,CIF_CIF_INTEN));
		RKCAMERA_TR("CIF_CIF_INTSTAT = 0x%x\n",read_cif_reg(pcdev->base,CIF_CIF_INTSTAT));
		RKCAMERA_TR("CIF_CIF_FOR = 0x%x\n",read_cif_reg(pcdev->base,CIF_CIF_FOR));
		RKCAMERA_TR("CIF_CIF_CROP = 0x%x\n",read_cif_reg(pcdev->base,CIF_CIF_CROP));
		RKCAMERA_TR("CIF_CIF_SET_SIZE = 0x%x\n",read_cif_reg(pcdev->base,CIF_CIF_SET_SIZE));
		RKCAMERA_TR("CIF_CIF_SCL_CTRL = 0x%x\n",read_cif_reg(pcdev->base,CIF_CIF_SCL_CTRL));
		RKCAMERA_TR("CRU_PCLK_REG30 = 0X%x\n",read_cru_reg(CRU_PCLK_REG30));
		RKCAMERA_TR("CIF_CIF_LAST_LINE = 0X%x\n",read_cif_reg(pcdev->base,CIF_CIF_LAST_LINE));
		
		RKCAMERA_TR("CIF_CIF_LAST_PIX = 0X%x\n",read_cif_reg(pcdev->base,CIF_CIF_LAST_PIX));
		RKCAMERA_TR("CIF_CIF_VIR_LINE_WIDTH = 0X%x\n",read_cif_reg(pcdev->base,CIF_CIF_VIR_LINE_WIDTH));
	RKCAMERA_TR("CIF_CIF_LINE_NUM_ADDR = 0X%x\n",read_cif_reg(pcdev->base,CIF_CIF_LINE_NUM_ADDR));
	RKCAMERA_TR("CIF_CIF_FRM0_ADDR_Y = 0X%x\n",read_cif_reg(pcdev->base,CIF_CIF_FRM0_ADDR_Y));
	RKCAMERA_TR("CIF_CIF_FRM0_ADDR_UV = 0X%x\n",read_cif_reg(pcdev->base,CIF_CIF_FRM0_ADDR_UV));
	RKCAMERA_TR("CIF_CIF_FRAME_STATUS = 0X%x\n",read_cif_reg(pcdev->base,CIF_CIF_FRAME_STATUS));
	}
	write_cif_reg(pcdev->base,CIF_CIF_CTRL, (read_cif_reg(pcdev->base,CIF_CIF_CTRL)&(~ENABLE_CAPTURE)));

	control = to_soc_camera_control(pcdev->icd);
	sd = dev_get_drvdata(control);
	ret = v4l2_subdev_call(sd,core, init, 1);

	mf.width = pcdev->icd->user_width;
	mf.height = pcdev->icd->user_height;
	xlate = soc_camera_xlate_by_fourcc(pcdev->icd, pcdev->icd->current_fmt->host_fmt->fourcc);	
	mf.code = xlate->code;

	ret |= v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);

	write_cif_reg(pcdev->base,CIF_CIF_CTRL, (read_cif_reg(pcdev->base,CIF_CIF_CTRL)|ENABLE_CAPTURE));

	RKCAMERA_TR("Camera host haven't recevie data from sensor,Reinit sensor now! ret:0x%x\n",ret);
}
static enum hrtimer_restart rk_camera_fps_func(struct hrtimer *timer)
{
    struct rk_camera_frmivalenum *fival_nxt=NULL,*fival_pre=NULL, *fival_rec=NULL;
	struct rk_camera_timer *fps_timer = container_of(timer, struct rk_camera_timer, timer);
	struct rk_camera_dev *pcdev = fps_timer->pcdev;
    int rec_flag,i;
    
	RKCAMERA_DG("rk_camera_fps_func fps:0x%x\n",pcdev->fps);
	if (pcdev->fps < 2) {
		RKCAMERA_TR("Camera host haven't recevie data from sensor,Reinit sensor delay!\n");
		pcdev->camera_reinit_work.pcdev = pcdev;
		INIT_WORK(&(pcdev->camera_reinit_work.work), rk_camera_reinit_work);
		queue_work(pcdev->camera_wq,&(pcdev->camera_reinit_work.work));
	} else {
	    for (i=0; i<2; i++) {
            if (pcdev->icd == pcdev->icd_frmival[i].icd) {
                fival_nxt = pcdev->icd_frmival[i].fival_list;                
            }
        }
        
        rec_flag = 0;
        fival_pre = fival_nxt;
        while (fival_nxt != NULL) {

            RKCAMERA_DG("%s %c%c%c%c %dx%d framerate : %d/%d\n", dev_name(&pcdev->icd->dev), 
                fival_nxt->fival.pixel_format & 0xFF, (fival_nxt->fival.pixel_format >> 8) & 0xFF,
			    (fival_nxt->fival.pixel_format >> 16) & 0xFF, (fival_nxt->fival.pixel_format >> 24),
			    fival_nxt->fival.width, fival_nxt->fival.height, fival_nxt->fival.discrete.denominator,
			    fival_nxt->fival.discrete.numerator);
            
            if (((fival_nxt->fival.pixel_format == pcdev->pixfmt) 
                && (fival_nxt->fival.height == pcdev->icd->user_height)
                && (fival_nxt->fival.width == pcdev->icd->user_width))
                || (fival_nxt->fival.discrete.denominator == 0)) {

                if (fival_nxt->fival.discrete.denominator == 0) {
                    fival_nxt->fival.index = 0;
                    fival_nxt->fival.width = pcdev->icd->user_width;
                    fival_nxt->fival.height= pcdev->icd->user_height;
                    fival_nxt->fival.pixel_format = pcdev->pixfmt;
                    fival_nxt->fival.discrete.denominator = pcdev->fps+2;
                    fival_nxt->fival.discrete.numerator = 1;
                    fival_nxt->fival.type = V4L2_FRMIVAL_TYPE_DISCRETE;
                } else {                
                    if (abs(pcdev->fps + 2 - fival_nxt->fival.discrete.numerator) > 2) {
                        fival_nxt->fival.discrete.denominator = pcdev->fps+2;
                        fival_nxt->fival.discrete.numerator = 1;
                        fival_nxt->fival.type = V4L2_FRMIVAL_TYPE_DISCRETE;
                    }
                }
                rec_flag = 1;
                fival_rec = fival_nxt;
            }
            fival_pre = fival_nxt;
            fival_nxt = fival_nxt->nxt;            
        }

        if ((rec_flag == 0) && fival_pre) {
            fival_pre->nxt = kzalloc(sizeof(struct rk_camera_frmivalenum), GFP_ATOMIC);
            if (fival_pre->nxt != NULL) {
                fival_pre->nxt->fival.index = fival_pre->fival.index++;
                fival_pre->nxt->fival.width = pcdev->icd->user_width;
                fival_pre->nxt->fival.height= pcdev->icd->user_height;
                fival_pre->nxt->fival.pixel_format = pcdev->pixfmt;

                fival_pre->nxt->fival.discrete.denominator = pcdev->fps+2;
                fival_pre->nxt->fival.discrete.numerator = 1;
                fival_pre->nxt->fival.type = V4L2_FRMIVAL_TYPE_DISCRETE;
                rec_flag = 1;
                fival_rec = fival_pre->nxt;
            }
        }
	}

	return HRTIMER_NORESTART;
}
static int rk_camera_s_stream(struct soc_camera_device *icd, int enable)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk_camera_dev *pcdev = ici->priv;
    int cif_ctrl_val;
	int ret;

	WARN_ON(pcdev->icd != icd);

	cif_ctrl_val = read_cif_reg(pcdev->base,CIF_CIF_CTRL);
	if (enable) {
		pcdev->fps = 0;
		hrtimer_cancel(&(pcdev->fps_timer.timer));
		pcdev->fps_timer.pcdev = pcdev;
		hrtimer_start(&(pcdev->fps_timer.timer),ktime_set(5, 0),HRTIMER_MODE_REL);
		cif_ctrl_val |= ENABLE_CAPTURE;
	} else {
        cif_ctrl_val &= ~ENABLE_CAPTURE;
		ret = hrtimer_cancel(&pcdev->fps_timer.timer);
		ret |= flush_work(&(pcdev->camera_reinit_work.work));
		RKCAMERA_DG("STREAM_OFF cancel timer and flush work:0x%x \n", ret);
	}
	write_cif_reg(pcdev->base,CIF_CIF_CTRL, cif_ctrl_val);

	RKCAMERA_DG("%s.. enable : 0x%x , CIF_CIF_CTRL = 0x%x\n", __FUNCTION__, enable,read_cif_reg(pcdev->base,CIF_CIF_CTRL));
	return 0;
}
int rk_camera_enum_frameintervals(struct soc_camera_device *icd, struct v4l2_frmivalenum *fival)
{
    struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
    struct rk_camera_dev *pcdev = ici->priv;
    struct rk_camera_frmivalenum *fival_list = NULL;
    struct v4l2_frmivalenum *fival_head;
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
            RKCAMERA_TR("%s: fival_list is NULL\n",__FUNCTION__);
            ret = -EINVAL;
        }
    } else {
        if (strcmp(dev_name(pcdev->icd->pdev),pcdev->pdata->info[0].dev_name) == 0) {
            fival_head = pcdev->pdata->info[0].fival;
        } else {
            fival_head = pcdev->pdata->info[1].fival;
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
            RKCAMERA_DG("%s %dx%d@%c%c%c%c framerate : %d/%d\n", dev_name(&pcdev->icd->dev),
                fival->width, fival->height,
                fival->pixel_format & 0xFF, (fival->pixel_format >> 8) & 0xFF,
			    (fival->pixel_format >> 16) & 0xFF, (fival->pixel_format >> 24),
			     fival->discrete.denominator,fival->discrete.numerator);			    
        } else {
            if (index == 0)
                RKCAMERA_TR("%s have not catch %d%d@%c%c%c%c index(%d) framerate\n",dev_name(&pcdev->icd->dev),
                    fival->width,fival->height, 
                    fival->pixel_format & 0xFF, (fival->pixel_format >> 8) & 0xFF,
    			    (fival->pixel_format >> 16) & 0xFF, (fival->pixel_format >> 24),
    			    index);
            else
                RKCAMERA_DG("%s have not catch %d%d@%c%c%c%c index(%d) framerate\n",dev_name(&pcdev->icd->dev),
                    fival->width,fival->height, 
                    fival->pixel_format & 0xFF, (fival->pixel_format >> 8) & 0xFF,
    			    (fival->pixel_format >> 16) & 0xFF, (fival->pixel_format >> 24),
    			    index);
            ret = -EINVAL;
        }
    }

    return ret;
}

#ifdef CONFIG_VIDEO_RK29_DIGITALZOOM_IPP_ON
static int rk_camera_set_digit_zoom(struct soc_camera_device *icd,
								const struct v4l2_queryctrl *qctrl, int zoom_rate)
{
	struct v4l2_crop a;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct rk_camera_dev *pcdev = ici->priv;
	unsigned int cif_fs = 0,cif_crop = 0;
	#if 1
/* ddl@rock-chips.com : The largest resolution is 2047x1088, so larger resolution must be operated some times
   (Assume operate times is 4),but resolution which ipp can operate ,it is width and height must be even. */
	a.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a.c.width = icd->user_width*100/zoom_rate;
	a.c.width &= ~0x03;    
	a.c.height = icd->user_height*100/zoom_rate;
	a.c.height &= ~0x03;

	a.c.left = (((pcdev->host_width - a.c.width)>>1) +pcdev->host_left)&(~0x01);
	a.c.top = (((pcdev->host_height - a.c.height)>>1) + pcdev->host_top)&(~0x01);

    down(&pcdev->zoominfo.sem);
	pcdev->zoominfo.a.c.height = a.c.height;
	pcdev->zoominfo.a.c.width = a.c.width;
	pcdev->zoominfo.a.c.top = 0;
	pcdev->zoominfo.a.c.left = 0;
    up(&pcdev->zoominfo.sem);

	cif_crop = (a.c.left+ (a.c.top<<16));
	cif_fs	= ((a.c.width ) + (a.c.height<<16));
//cif do the crop , ipp do the scale
	write_cif_reg(pcdev->base,CIF_CIF_CTRL, (read_cif_reg(pcdev->base,CIF_CIF_CTRL)&(~ENABLE_CAPTURE)));
	write_cif_reg(pcdev->base,CIF_CIF_CROP, cif_crop);
	write_cif_reg(pcdev->base,CIF_CIF_SET_SIZE, cif_fs);
	write_cif_reg(pcdev->base,CIF_CIF_VIR_LINE_WIDTH, a.c.width);
	write_cif_reg(pcdev->base,CIF_CIF_CTRL, (read_cif_reg(pcdev->base,CIF_CIF_CTRL)|(ENABLE_CAPTURE)));
	//MUST bypass scale 
	#else
	//change the crop and scale parameters
    #endif
	RKCAMERA_DG("%s..zoom_rate:%d (%dx%d at (%d,%d)-> %dx%d)\n",__FUNCTION__, zoom_rate,a.c.width, a.c.height, a.c.left, a.c.top, pcdev->host_width, pcdev->host_height );

	return 0;
}
#endif
static inline struct v4l2_queryctrl const *rk_camera_soc_camera_find_qctrl(
	struct soc_camera_host_ops *ops, int id)
{
	int i;

	for (i = 0; i < ops->num_controls; i++)
		if (ops->controls[i].id == id)
			return &ops->controls[i];

	return NULL;
}


static int rk_camera_set_ctrl(struct soc_camera_device *icd,
								struct v4l2_control *sctrl)
{

	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	const struct v4l2_queryctrl *qctrl;
    struct rk_camera_dev *pcdev = ici->priv;
    int ret = 0;

	qctrl = rk_camera_soc_camera_find_qctrl(ici->ops, sctrl->id);
	if (!qctrl) {
		ret = -ENOIOCTLCMD;
        goto rk_camera_set_ctrl_end;
	}

	switch (sctrl->id)
	{
	#ifdef CONFIG_VIDEO_RK29_DIGITALZOOM_IPP_ON
		case V4L2_CID_ZOOM_ABSOLUTE:
		{
			if ((sctrl->value < qctrl->minimum) || (sctrl->value > qctrl->maximum)){
        		ret = -EINVAL;
                goto rk_camera_set_ctrl_end;
        	}
            ret = rk_camera_set_digit_zoom(icd, qctrl, sctrl->value);
			if (ret == 0) {
				pcdev->zoominfo.zoom_rate = sctrl->value;
            } else { 
                goto rk_camera_set_ctrl_end;
            }
			break;
		}
    #endif
		default:
			ret = -ENOIOCTLCMD;
			break;
	}
rk_camera_set_ctrl_end:
	return ret;
}

static struct soc_camera_host_ops rk_soc_camera_host_ops =
{
    .owner		= THIS_MODULE,
    .add		= rk_camera_add_device,
    .remove		= rk_camera_remove_device,
    .suspend	= rk_camera_suspend,
    .resume		= rk_camera_resume,
    .enum_frameinervals = rk_camera_enum_frameintervals,
    .set_crop	= rk_camera_set_crop,
    .get_formats	= rk_camera_get_formats, 
    .put_formats	= rk_camera_put_formats,
    .set_fmt	= rk_camera_set_fmt,
    .try_fmt	= rk_camera_try_fmt,
    .init_videobuf	= rk_camera_init_videobuf,
    .reqbufs	= rk_camera_reqbufs,
    .poll		= rk_camera_poll,
    .querycap	= rk_camera_querycap,
    .set_bus_param	= rk_camera_set_bus_param,
    .s_stream = rk_camera_s_stream,   /* ddl@rock-chips.com : Add stream control for host */
    .set_ctrl = rk_camera_set_ctrl,
    .controls = rk_camera_controls,
    .num_controls = ARRAY_SIZE(rk_camera_controls)
    
};
static int rk_camera_probe(struct platform_device *pdev)
{
    struct rk_camera_dev *pcdev;
    struct resource *res;
    struct rk_camera_frmivalenum *fival_list,*fival_nxt;
    int irq,i;
    int err = 0;

    RKCAMERA_DG("%s..%s..%d  \n",__FUNCTION__,__FILE__,__LINE__);
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


    /*config output clk*/ // must modify start
    #if 0
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
//must modify end
        RKCAMERA_TR(KERN_ERR "failed to get vip_clk(axi) source\n");
        err = -ENOENT;
        goto exit_reqmem_vip;
    }

	if (!pcdev->hclk_disp_matrix || !pcdev->vip_matrix ||
		IS_ERR(pcdev->hclk_disp_matrix) || IS_ERR(pcdev->vip_matrix))  {

        RKCAMERA_TR(KERN_ERR "failed to get vip_clk(ahb) source\n");
        err = -ENOENT;
        goto exit_reqmem_vip;
    }
#endif
    dev_set_drvdata(&pdev->dev, pcdev);
    pcdev->res = res;
    pcdev->pdata = pdev->dev.platform_data;             /* ddl@rock-chips.com : Request IO in init function */
	pcdev->hostid = pdev->id;

	if (pcdev->pdata && pcdev->pdata->io_init) {
        pcdev->pdata->io_init();
    }
	#ifdef CONFIG_VIDEO_RK29_WORK_IPP
	if (pcdev->pdata && (strcmp(pcdev->pdata->meminfo.name,"camera_ipp_mem")==0)) {
		pcdev->vipmem_phybase = pcdev->pdata->meminfo.start;
		pcdev->vipmem_size = pcdev->pdata->meminfo.size;
		RKCAMERA_DG("\n%s Memory(start:0x%x size:0x%x) for IPP obtain \n",__FUNCTION__, pcdev->pdata->meminfo.start,pcdev->pdata->meminfo.size);
	} else {
		RKCAMERA_TR("\n%s Memory for IPP have not obtain! IPP Function is fail\n",__FUNCTION__);
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
     if(res){
	    if (!request_mem_region(res->start, res->end - res->start + 1,
	                            RK29_CAM_DRV_NAME)) {
	        err = -EBUSY;
	        goto exit_reqmem_vip;
	    }
	    pcdev->base = ioremap(res->start, res->end - res->start + 1);
	    if (pcdev->base == NULL) {
	        dev_err(pcdev->dev, "ioremap() of registers failed\n");
	        err = -ENXIO;
	        goto exit_ioremap_vip;
	    }
     	}
	
    pcdev->irq = irq;
    pcdev->dev = &pdev->dev;

    /* config buffer address */
    /* request irq */
   if(irq > 0){
    err = request_irq(pcdev->irq, rk_camera_irq, 0, RK29_CAM_DRV_NAME,
                      pcdev);
    if (err) {
        dev_err(pcdev->dev, "Camera interrupt register failed \n");
        goto exit_reqirq;
    }
   	}
	pcdev->camera_wq = create_workqueue("rk_camera_workqueue");
	if (pcdev->camera_wq == NULL)
		goto exit_free_irq;
	pcdev->camera_reinit_work.pcdev = pcdev;
	INIT_WORK(&(pcdev->camera_reinit_work.work), rk_camera_reinit_work);

    for (i=0; i<2; i++) {
        pcdev->icd_frmival[i].icd = NULL;
        pcdev->icd_frmival[i].fival_list = kzalloc(sizeof(struct rk_camera_frmivalenum),GFP_KERNEL);
        
    }
    pcdev->soc_host.drv_name	= RK29_CAM_DRV_NAME;
    pcdev->soc_host.ops		= &rk_soc_camera_host_ops;
    pcdev->soc_host.priv		= pcdev;
    pcdev->soc_host.v4l2_dev.dev	= &pdev->dev;
    pcdev->soc_host.nr		= pdev->id;

    err = soc_camera_host_register(&pcdev->soc_host);
    if (err)
        goto exit_free_irq;
	pcdev->fps_timer.pcdev = pcdev;
	hrtimer_init(&(pcdev->fps_timer.timer), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pcdev->fps_timer.timer.function = rk_camera_fps_func;
    pcdev->icd_cb.sensor_cb = NULL;
//	rk29_camdev_info_ptr = pcdev;
    RKCAMERA_DG("%s..%s..%d  \n",__FUNCTION__,__FILE__,__LINE__);
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
exit_ioremap_vip:
    release_mem_region(res->start, res->end - res->start + 1);

exit_reqmem_vip:
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
//    rk_camdev_info_ptr = NULL;
exit:
    return err;
}

static int __devexit rk_camera_remove(struct platform_device *pdev)
{
    struct rk_camera_dev *pcdev = platform_get_drvdata(pdev);
    struct resource *res;
    struct rk_camera_frmivalenum *fival_list,*fival_nxt;
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
        pcdev->pdata->io_deinit(pcdev->hostid);
    }

    kfree(pcdev);
 //   rk_camdev_info_ptr = NULL;
    dev_info(&pdev->dev, "RK28 Camera driver unloaded\n");

    return 0;
}

static struct platform_driver rk_camera_driver =
{
    .driver 	= {
        .name	= RK29_CAM_DRV_NAME,
    },
    .probe		= rk_camera_probe,
    .remove		= __devexit_p(rk_camera_remove),
};


static int __devinit rk_camera_init(void)
{
    RKCAMERA_DG("%s..%s..%d  \n",__FUNCTION__,__FILE__,__LINE__);
    return platform_driver_register(&rk_camera_driver);
}

static void __exit rk_camera_exit(void)
{
    platform_driver_unregister(&rk_camera_driver);
}

device_initcall_sync(rk_camera_init);
module_exit(rk_camera_exit);

MODULE_DESCRIPTION("RKSoc Camera Host driver");
MODULE_AUTHOR("ddl <ddl@rock-chips>");
MODULE_LICENSE("GPL");
#endif
