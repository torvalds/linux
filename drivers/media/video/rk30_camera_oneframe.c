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
#include <linux/kthread.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf-dma-contig.h>
#include <media/soc_camera.h>
#include <media/soc_mediabus.h>
#include <media/videobuf-core.h>
#include <linux/rockchip/iomap.h>

#include "../../video/rockchip/rga/rga.h"
#include "../../../arch/arm/mach-rockchip/rk30_camera.h"/*yzm*/
#include <linux/rockchip/cru.h>

/*******yzm*********

#include <plat/efuse.h>
#if (defined(CONFIG_ARCH_RK2928) || defined(CONFIG_ARCH_RK3026))
#include <mach/rk2928_camera.h>
#include <mach/cru.h>
#include <mach/pmu.h>
#define SOFT_RST_CIF1 (SOFT_RST_MAX+1)
#endif
*/
#include <asm/cacheflush.h>

#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <media/soc_camera.h>
#include <media/camsys_head.h>
#include <linux/of.h>
#include <linux/of_irq.h>
 
static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define CAMMODULE_NAME     "rk_cam_cif"   

#define wprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	    printk(KERN_ERR "%s(%d): " fmt,CAMMODULE_NAME,__LINE__,## arg); } while (0)

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	    printk(KERN_ERR"%s(%d): " fmt,CAMMODULE_NAME,__LINE__,## arg); } while (0)	    

#define RKCAMERA_TR(format, ...)  printk(KERN_ERR "%s(%d):" format,CAMMODULE_NAME,__LINE__,## __VA_ARGS__)
#define RKCAMERA_DG1(format, ...) wprintk(1, format, ## __VA_ARGS__)
#define RKCAMERA_DG2(format, ...) dprintk(2, format, ## __VA_ARGS__)
#define debug_printk(format, ...) dprintk(3, format, ## __VA_ARGS__)

/* CIF Reg Offset*/
#define  CIF_CIF_CTRL                       0x00
#define  CIF_CIF_INTEN                      0x04
#define  CIF_CIF_INTSTAT                    0x08
#define  CIF_CIF_FOR                        0x0c
#define  CIF_CIF_LINE_NUM_ADDR              0x10
#define  CIF_CIF_FRM0_ADDR_Y                0x14
#define  CIF_CIF_FRM0_ADDR_UV               0x18
#define  CIF_CIF_FRM1_ADDR_Y                0x1c
#define  CIF_CIF_FRM1_ADDR_UV               0x20
#define  CIF_CIF_VIR_LINE_WIDTH             0x24
#define  CIF_CIF_SET_SIZE                   0x28
#define  CIF_CIF_SCM_ADDR_Y                 0x2c
#define  CIF_CIF_SCM_ADDR_U                 0x30
#define  CIF_CIF_SCM_ADDR_V                 0x34
#define  CIF_CIF_WB_UP_FILTER               0x38
#define  CIF_CIF_WB_LOW_FILTER              0x3c
#define  CIF_CIF_WBC_CNT                    0x40
#define  CIF_CIF_CROP                       0x44
#define  CIF_CIF_SCL_CTRL                   0x48
#define	 CIF_CIF_SCL_DST                    0x4c
#define	 CIF_CIF_SCL_FCT                    0x50
#define	 CIF_CIF_SCL_VALID_NUM              0x54
#define	 CIF_CIF_LINE_LOOP_CTR              0x58
#define	 CIF_CIF_FRAME_STATUS               0x60
#define	 CIF_CIF_CUR_DST                    0x64
#define	 CIF_CIF_LAST_LINE                  0x68
#define	 CIF_CIF_LAST_PIX                   0x6c

/*The key register bit descrition*/
/* CIF_CTRL Reg , ignore SCM,WBC,ISP,*/
#define  DISABLE_CAPTURE              (0x00<<0)
#define  ENABLE_CAPTURE               (0x01<<0)
#define  MODE_ONEFRAME                (0x00<<1)
#define  MODE_PINGPONG                (0x01<<1)
#define  MODE_LINELOOP                (0x02<<1)
#define  AXI_BURST_16                 (0x0F << 12)

/*CIF_CIF_INTEN*/
#define  FRAME_END_EN			(0x01<<1)
#define  BUS_ERR_EN				(0x01<<6)
#define  SCL_ERR_EN				(0x01<<7)

/*CIF_CIF_FOR*/
#define  VSY_HIGH_ACTIVE                   (0x01<<0)
#define  VSY_LOW_ACTIVE                    (0x00<<0)
#define  HSY_LOW_ACTIVE 			       (0x01<<1)
#define  HSY_HIGH_ACTIVE			       (0x00<<1)
#define  INPUT_MODE_YUV 			       (0x00<<2)
#define  INPUT_MODE_PAL 			       (0x02<<2)
#define  INPUT_MODE_NTSC 			       (0x03<<2)
#define  INPUT_MODE_RAW 			       (0x04<<2)
#define  INPUT_MODE_JPEG 			       (0x05<<2)
#define  INPUT_MODE_MIPI			       (0x06<<2)
#define  YUV_INPUT_ORDER_UYVY(ori)	       (ori & (~(0x03<<5)))
#define  YUV_INPUT_ORDER_YVYU(ori)		   ((ori & (~(0x01<<6)))|(0x01<<5))
#define  YUV_INPUT_ORDER_VYUY(ori)		   ((ori & (~(0x01<<5))) | (0x1<<6))
#define  YUV_INPUT_ORDER_YUYV(ori)		   (ori|(0x03<<5))
#define  YUV_INPUT_422		               (0x00<<7)
#define  YUV_INPUT_420		               (0x01<<7)
#define  INPUT_420_ORDER_EVEN		       (0x00<<8)
#define  INPUT_420_ORDER_ODD		       (0x01<<8)
#define  CCIR_INPUT_ORDER_ODD		       (0x00<<9)
#define  CCIR_INPUT_ORDER_EVEN             (0x01<<9)
#define  RAW_DATA_WIDTH_8                  (0x00<<11)
#define  RAW_DATA_WIDTH_10                 (0x01<<11)
#define  RAW_DATA_WIDTH_12                 (0x02<<11)	
#define  YUV_OUTPUT_422                    (0x00<<16)
#define  YUV_OUTPUT_420                    (0x01<<16)
#define  OUTPUT_420_ORDER_EVEN             (0x00<<17)
#define  OUTPUT_420_ORDER_ODD              (0x01<<17)
#define  RAWD_DATA_LITTLE_ENDIAN           (0x00<<18)
#define  RAWD_DATA_BIG_ENDIAN              (0x01<<18)
#define  UV_STORAGE_ORDER_UVUV             (0x00<<19)
#define  UV_STORAGE_ORDER_VUVU             (0x01<<19)

/*CIF_CIF_SCL_CTRL*/
#define ENABLE_SCL_DOWN                    (0x01<<0)		
#define DISABLE_SCL_DOWN                   (0x00<<0)
#define ENABLE_SCL_UP                      (0x01<<1)		
#define DISABLE_SCL_UP                     (0x00<<1)
#define ENABLE_YUV_16BIT_BYPASS            (0x01<<4)
#define DISABLE_YUV_16BIT_BYPASS           (0x00<<4)
#define ENABLE_RAW_16BIT_BYPASS            (0x01<<5)
#define DISABLE_RAW_16BIT_BYPASS           (0x00<<5)
#define ENABLE_32BIT_BYPASS                (0x01<<6)
#define DISABLE_32BIT_BYPASS               (0x00<<6)


#define MIN(x,y)   ((x<y) ? x: y)
#define MAX(x,y)    ((x>y) ? x: y)
#define RK_SENSOR_24MHZ      24*1000*1000          /* MHz */
#define RK_SENSOR_48MHZ      48

#define __raw_readl(p)		  (*(unsigned long *)(p))
#define __raw_writel(v,p)     (*(unsigned long *)(p) = (v))

#define write_cif_reg(base,addr, val)  __raw_writel(val, addr+(base))
#define read_cif_reg(base,addr) __raw_readl(addr+(base))
#define mask_cif_reg(addr, msk, val)    write_cif_reg(addr, (val)|((~(msk))&read_cif_reg(addr)))

/*
#if defined(CONFIG_ARCH_RK30) || defined(CONFIG_ARCH_RK3188)
//CRU,PIXCLOCK
#define CRU_PCLK_REG30                     0xbc
#define ENANABLE_INVERT_PCLK_CIF0          ((0x1<<24)|(0x1<<8))
#define DISABLE_INVERT_PCLK_CIF0           ((0x1<<24)|(0x0<<8))
#define ENANABLE_INVERT_PCLK_CIF1          ((0x1<<28)|(0x1<<12))
#define DISABLE_INVERT_PCLK_CIF1           ((0x1<<28)|(0x0<<12))

#define CRU_CIF_RST_REG30                  0x128
#define MASK_RST_CIF0                      (0x01 << 30)
#define MASK_RST_CIF1                      (0x01 << 31)
#define RQUEST_RST_CIF0                    (0x01 << 14)
#define RQUEST_RST_CIF1                    (0x01 << 15)

#define write_cru_reg(addr, val)            __raw_writel(val, addr+RK30_CRU_BASE)
#define read_cru_reg(addr)                  __raw_readl(addr+RK30_CRU_BASE)
#define mask_cru_reg(addr, msk, val)        write_cru_reg(addr,(val)|((~(msk))&read_cru_reg(addr)))
#endif
*/
/*********yzm**********/

static u32 CRU_PCLK_REG30;
static u32 ENANABLE_INVERT_PCLK_CIF0;
static u32 DISABLE_INVERT_PCLK_CIF0;
static u32 ENANABLE_INVERT_PCLK_CIF1;
static u32 DISABLE_INVERT_PCLK_CIF1;
	
#define write_cru_reg(addr, val)            __raw_writel(val, addr+RK_CRU_VIRT)
#define read_cru_reg(addr)                  __raw_readl(addr+RK_CRU_VIRT)
#define mask_cru_reg(addr, msk, val)        write_cru_reg(addr,(val)|((~(msk))&read_cru_reg(addr)))
/*********yzm*********end*/
/*
#if defined(CONFIG_ARCH_RK2928)
#define write_cru_reg(addr, val)  
#define read_cru_reg(addr)                 0 
#define mask_cru_reg(addr, msk, val)	
#endif
*/

/*
#if defined(CONFIG_ARCH_RK3066B) || defined(CONFIG_ARCH_RK3188)
//GRF_IO_CON3                        0x100
#define CIF_DRIVER_STRENGTH_2MA            (0x00 << 12)
#define CIF_DRIVER_STRENGTH_4MA            (0x01 << 12)
#define CIF_DRIVER_STRENGTH_8MA            (0x02 << 12)
#define CIF_DRIVER_STRENGTH_12MA           (0x03 << 12)
#define CIF_DRIVER_STRENGTH_MASK           (0x03 << 28)

//GRF_IO_CON4                        0x104
#define CIF_CLKOUT_AMP_3V3                 (0x00 << 10)
#define CIF_CLKOUT_AMP_1V8                 (0x01 << 10)
#define CIF_CLKOUT_AMP_MASK                (0x01 << 26)

#define write_grf_reg(addr, val)           __raw_writel(val, addr+RK30_GRF_BASE)
#define read_grf_reg(addr)                 __raw_readl(addr+RK30_GRF_BASE)
#define mask_grf_reg(addr, msk, val)       write_grf_reg(addr,(val)|((~(msk))&read_grf_reg(addr)))
#else
#define write_grf_reg(addr, val)  
#define read_grf_reg(addr)                 0
#define mask_grf_reg(addr, msk, val)	
#endif
*/
#define CAM_WORKQUEUE_IS_EN()   (true)
#define CAM_IPPWORK_IS_EN()     (false)/*((pcdev->zoominfo.a.c.width != pcdev->icd->user_width) || (pcdev->zoominfo.a.c.height != pcdev->icd->user_height))*/

#define IS_CIF0()		(true)/*(pcdev->hostid == RK_CAM_PLATFORM_DEV_ID_0)*/
#if (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_IPP)
#define CROP_ALIGN_BYTES (0x03)
#define CIF_DO_CROP 0
#elif (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_ARM)
#define CROP_ALIGN_BYTES (0x0f)
#define CIF_DO_CROP 0
#elif (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_RGA)
#define CROP_ALIGN_BYTES (0x03)
#define CIF_DO_CROP 0
#elif(CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_PP)
#define CROP_ALIGN_BYTES (0x0F)
#define CIF_DO_CROP 1
#endif

/*
*v0.1.0 : this driver is 3.10 kernel driver;
		copy and updata from v0.3.0x19;
		support rk312x;
*v0.1.1:
         1. spin lock in struct rk_cif_clk is not neccessary,and scheduled func clk_get called in this spin lock scope
            cause warning, so remove this spin lock .
*v0.1.2:
		 1. rk3126 and rk3128 use different dts file.		 
*v0.1.3:
		 1. i2c 1 and wifi use the common io in rk3128,so just enable i2c1 in rk3126 dts file
*v0.1.4:
		 1. When cif was at work, the aclk is closed ,may cause bus abnormal ,so sleep 100ms before close aclk 
*v0.1.5:	
           1. Improve the code to support all configuration.reset,af,flash...
*v0.1.6:
		 1. Delete SOCAM_DATAWIDTH_8 in SENSOR_BUS_PARAM parameters,it conflict with V4L2_MBUS_PCLK_SAMPLE_FALLING.
*v0.1.7:
		 1. Add  power and powerdown controled by PMU.
*/
#define RK_CAM_VERSION_CODE KERNEL_VERSION(0, 1, 0x7)
static int version = RK_CAM_VERSION_CODE;
module_param(version, int, S_IRUGO);

/* limit to rk29 hardware capabilities */
#define RK_CAM_BUS_PARAM   (V4L2_MBUS_MASTER |\
                V4L2_MBUS_HSYNC_ACTIVE_HIGH |\
                V4L2_MBUS_HSYNC_ACTIVE_LOW |\
                V4L2_MBUS_VSYNC_ACTIVE_HIGH |\
                V4L2_MBUS_VSYNC_ACTIVE_LOW |\
                V4L2_MBUS_PCLK_SAMPLE_RISING |\
                V4L2_MBUS_PCLK_SAMPLE_FALLING|\
                V4L2_MBUS_DATA_ACTIVE_HIGH |\
                V4L2_MBUS_DATA_ACTIVE_LOW|\
                SOCAM_DATAWIDTH_8|SOCAM_DATAWIDTH_10|\
                SOCAM_MCLK_24MHZ |SOCAM_MCLK_48MHZ)

#define RK_CAM_W_MIN        48
#define RK_CAM_H_MIN        32
#define RK_CAM_W_MAX        3856                /* ddl@rock-chips.com : 10M Pixel */
#define RK_CAM_H_MAX        2764
#define RK_CAM_FRAME_INVAL_INIT      0
#define RK_CAM_FRAME_INVAL_DC        0          /* ddl@rock-chips.com :  */
#define RK30_CAM_FRAME_MEASURE       5


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
    unsigned int cifVirWidth;
    unsigned int cifScale;
/*	unsigned int VipCrm;*/
	enum rk_camera_reg_state Inval;
};
struct rk_camera_work
{
	struct videobuf_buffer *vb;
	struct rk_camera_dev *pcdev;
	struct work_struct work;
    struct list_head queue;
    unsigned int index;    
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
    int vir_width;
    int vir_height;
    int zoom_rate;
};
#if CAMERA_VIDEOBUF_ARM_ACCESS
struct rk29_camera_vbinfo
{
    unsigned int phy_addr;
    void __iomem *vir_addr;
    unsigned int size;
};
#endif
struct rk_camera_timer{
	struct rk_camera_dev *pcdev;
	struct hrtimer timer;
    bool istarted;
};
struct rk_cif_clk 
{
    /************must modify start************/
	struct clk *pd_cif;
	struct clk *aclk_cif;
    struct clk *hclk_cif;
    struct clk *cif_clk_in;
    struct clk *cif_clk_out;
	/************must modify end************/

   // spinlock_t lock;
    bool on;
};

struct rk_cif_crop
{
    spinlock_t lock;
    struct v4l2_rect c;
    struct v4l2_rect bounds;
};

struct rk_cif_irqinfo
{
    unsigned int irq;
    unsigned long cifirq_idx;
    unsigned long cifirq_normal_idx;
    unsigned long cifirq_abnormal_idx;

    unsigned long dmairq_idx;
    spinlock_t lock;
};

struct rk_camera_dev
{
    struct soc_camera_host	soc_host;    
    struct device		*dev;
    /* RK2827x is only supposed to handle one camera on its Quick Capture
     * interface. If anyone ever builds hardware to enable more than
     * one camera, they will have to modify this driver too */
    struct soc_camera_device *icd;
    void __iomem *base;
    int frame_inval;           /* ddl@rock-chips.com : The first frames is invalidate  */

    unsigned int fps;
    unsigned int last_fps;
    unsigned long frame_interval;
    unsigned int pixfmt;
    /*for ipp	*/
    unsigned int vipmem_phybase;
    void __iomem *vipmem_virbase;
    unsigned int vipmem_size;
    unsigned int vipmem_bsize;
#if CAMERA_VIDEOBUF_ARM_ACCESS    
    struct rk29_camera_vbinfo *vbinfo;
    unsigned int vbinfo_count;
#endif    
    int host_width;
    int host_height;
    int host_left;  /*sensor output size ?*/
    int host_top;
    int hostid;
    int icd_width;
    int icd_height;

    struct rk_cif_crop cropinfo;
    struct rk_cif_irqinfo irqinfo;

    struct rk29camera_platform_data *pdata;
    struct resource		*res;
    struct list_head	capture;
    struct rk_camera_zoominfo zoominfo;

    spinlock_t		lock;

    struct videobuf_buffer	*active;
    struct rk_camera_reg reginfo_suspend;
    struct workqueue_struct *camera_wq;
    struct rk_camera_work *camera_work;
    struct list_head camera_work_queue;
    spinlock_t camera_work_lock;
    unsigned int camera_work_count;
    struct rk_camera_timer fps_timer;
    struct rk_camera_work camera_reinit_work;
    int icd_init;
    rk29_camera_sensor_cb_s icd_cb;
    struct rk_camera_frmivalinfo icd_frmival[2];
    bool timer_get_fps;
    unsigned int reinit_times; 
    struct videobuf_queue *video_vq;
    atomic_t stop_cif;
    struct timeval first_tv;
    
    int chip_id;
};

static const struct v4l2_queryctrl rk_camera_controls[] =
{
    {
        .id		= V4L2_CID_ZOOM_ABSOLUTE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "DigitalZoom Control",
        .minimum	= 100,
        .maximum	= 300,
        .step		= 5,
        .default_value = 100,
    }
};

static struct rk_cif_clk  cif_clk[2];

static DEFINE_MUTEX(camera_lock);
static const char *rk_cam_driver_description = "RK_Camera";

static int rk_camera_s_stream(struct soc_camera_device *icd, int enable);
static void rk_camera_capture_process(struct work_struct *work);

static void rk_camera_diffchips(const char *rockchip_name)
{
	if(strstr(rockchip_name,"3128")||strstr(rockchip_name,"3126"))
	{	
		CRU_PCLK_REG30 = 0xbc;
		ENANABLE_INVERT_PCLK_CIF0 = ((0x1<<23)|(0x1<<7));
		DISABLE_INVERT_PCLK_CIF0  = ((0x1<<23)|(0x0<<7));
		ENANABLE_INVERT_PCLK_CIF1 = ENANABLE_INVERT_PCLK_CIF0;
		DISABLE_INVERT_PCLK_CIF1  = DISABLE_INVERT_PCLK_CIF0;
	}
}
static inline void rk_cru_set_soft_reset(u32 idx, bool on , u32 RK_CRU_SOFTRST_CON)
{
	void __iomem *reg = RK_CRU_VIRT + RK_CRU_SOFTRST_CON;
	u32 val = on ? 0x10001U << 14 : 0x10000U << 14;
	writel_relaxed(val, reg);
	dsb();
}

static void rk_camera_cif_reset(struct rk_camera_dev *pcdev, int only_rst)
{
    int ctrl_reg,inten_reg,crop_reg,set_size_reg,for_reg,vir_line_width_reg,scl_reg,y_reg,uv_reg;
	u32 RK_CRU_SOFTRST_CON = 0;
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);
	if(strstr(pcdev->pdata->rockchip_name,"3128")||strstr(pcdev->pdata->rockchip_name,"3126"))
		RK_CRU_SOFTRST_CON = RK312X_CRU_SOFTRSTS_CON(6);
	
	if (only_rst == true) {
        rk_cru_set_soft_reset(0, true ,RK_CRU_SOFTRST_CON);
        udelay(5);
        rk_cru_set_soft_reset(0, false ,RK_CRU_SOFTRST_CON);
    } else {
        ctrl_reg = read_cif_reg(pcdev->base,CIF_CIF_CTRL);
        if (ctrl_reg & ENABLE_CAPTURE) {
            write_cif_reg(pcdev->base,CIF_CIF_CTRL, ctrl_reg&~ENABLE_CAPTURE);
        }
    	crop_reg = read_cif_reg(pcdev->base,CIF_CIF_CROP);
    	set_size_reg = read_cif_reg(pcdev->base,CIF_CIF_SET_SIZE);
    	inten_reg = read_cif_reg(pcdev->base,CIF_CIF_INTEN);
    	for_reg = read_cif_reg(pcdev->base,CIF_CIF_FOR);
    	vir_line_width_reg = read_cif_reg(pcdev->base,CIF_CIF_VIR_LINE_WIDTH);
    	scl_reg = read_cif_reg(pcdev->base,CIF_CIF_SCL_CTRL);
    	y_reg = read_cif_reg(pcdev->base, CIF_CIF_FRM0_ADDR_Y);
    	uv_reg = read_cif_reg(pcdev->base, CIF_CIF_FRM0_ADDR_UV);
    	
    	rk_cru_set_soft_reset(0, true ,RK_CRU_SOFTRST_CON);
    	udelay(5);
    	rk_cru_set_soft_reset(0, false ,RK_CRU_SOFTRST_CON); 

        write_cif_reg(pcdev->base,CIF_CIF_CTRL, ctrl_reg&~ENABLE_CAPTURE);
	    write_cif_reg(pcdev->base,CIF_CIF_INTEN, inten_reg);
	    write_cif_reg(pcdev->base,CIF_CIF_CROP, crop_reg);
	    write_cif_reg(pcdev->base,CIF_CIF_SET_SIZE, set_size_reg);
	    write_cif_reg(pcdev->base,CIF_CIF_FOR, for_reg);
	    write_cif_reg(pcdev->base,CIF_CIF_VIR_LINE_WIDTH,vir_line_width_reg);
	    write_cif_reg(pcdev->base,CIF_CIF_SCL_CTRL,scl_reg);
	    write_cif_reg(pcdev->base,CIF_CIF_FRM0_ADDR_Y,y_reg);      /*ddl@rock-chips.com v0.3.0x13 */
	    write_cif_reg(pcdev->base,CIF_CIF_FRM0_ADDR_UV,uv_reg);
    }
    return;
}


/*
 *  Videobuf operations
 */
static int rk_videobuf_setup(struct videobuf_queue *vq, unsigned int *count,
                               unsigned int *size)
{
    struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;
	unsigned int i;
    struct rk_camera_work *wk;

	 struct soc_mbus_pixelfmt fmt;
	int bytes_per_line;
	int bytes_per_line_host;
	fmt.packing = SOC_MBUS_PACKING_1_5X8;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


		bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);
	if(icd->current_fmt->host_fmt->fourcc ==  V4L2_PIX_FMT_RGB565)
		 bytes_per_line_host = soc_mbus_bytes_per_line(pcdev->host_width,
						&fmt);
	else if(icd->current_fmt->host_fmt->fourcc ==  V4L2_PIX_FMT_RGB24)
		bytes_per_line_host = pcdev->host_width*3;
	else
		bytes_per_line_host = soc_mbus_bytes_per_line(pcdev->host_width,
					   icd->current_fmt->host_fmt);
   /* dev_dbg(&icd->dev, "count=%d, size=%d\n", *count, *size);*/ /*yzm*/

	if (bytes_per_line_host < 0)
		return bytes_per_line_host;

	/* planar capture requires Y, U and V buffers to be page aligned */
	*size = PAGE_ALIGN(bytes_per_line*icd->user_height);	   /* Y pages UV pages, yuv422*/
	pcdev->vipmem_bsize = PAGE_ALIGN(bytes_per_line_host * pcdev->host_height);

	if (CAM_WORKQUEUE_IS_EN()) {
		
        if (CAM_IPPWORK_IS_EN())  {
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
			pcdev->camera_work = wk = kzalloc(sizeof(struct rk_camera_work)*(*count), GFP_KERNEL);
			if (pcdev->camera_work == NULL) {
				RKCAMERA_TR("kmalloc failed\n");
				BUG();
			}
            INIT_LIST_HEAD(&pcdev->camera_work_queue);

            for (i=0; i<(*count); i++) {
                wk->index = i;                
                list_add_tail(&wk->queue, &pcdev->camera_work_queue);
                wk++; 
            }
			pcdev->camera_work_count = (*count);
		}
#if CAMERA_VIDEOBUF_ARM_ACCESS
        if (pcdev->vbinfo && (pcdev->vbinfo_count != *count)) {
            kfree(pcdev->vbinfo);
            pcdev->vbinfo = NULL;
            pcdev->vbinfo_count = 0x00;
        }

        if (pcdev->vbinfo == NULL) {
            pcdev->vbinfo = kzalloc(sizeof(struct rk29_camera_vbinfo)*(*count), GFP_KERNEL);
            if (pcdev->vbinfo == NULL) {
				RKCAMERA_TR("vbinfo kmalloc fail\n");
				BUG();
			}
            memset(pcdev->vbinfo,0,sizeof(struct rk29_camera_vbinfo)*(*count));
			pcdev->vbinfo_count = *count;
        }
#endif        
	}
    pcdev->video_vq = vq;
    RKCAMERA_DG1("videobuf size:%d, vipmem_buf size:%d, count:%d \n",*size,pcdev->vipmem_size, *count);

    return 0;
}
static void rk_videobuf_free(struct videobuf_queue *vq, struct rk_camera_buffer *buf)
{
    struct soc_camera_device *icd = vq->priv_data;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);


    dev_dbg(icd->control, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,/*yzm*/
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
	videobuf_waiton(vq, &buf->vb, 0, 0);
    videobuf_dma_contig_free(vq, &buf->vb);
    /*dev_dbg(&icd->dev, "%s freed\n", __func__);*/ /*yzm*/
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

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);
	
	if ((bytes_per_line < 0) || (vb->boff == 0))
		return -EINVAL;

    buf = container_of(vb, struct rk_camera_buffer, vb);

    /*dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,*/ /*yzm*/
      /*      vb, vb->baddr, vb->bsize);*/ /*yzm*/
    
    /* Added list head initialization on alloc */
    WARN_ON(!list_empty(&vb->queue));    

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

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);


    if (vb) {
		if (CAM_WORKQUEUE_IS_EN() & CAM_IPPWORK_IS_EN()) {
			y_addr = pcdev->vipmem_phybase + vb->i*pcdev->vipmem_bsize;
			uv_addr = y_addr + pcdev->zoominfo.vir_width*pcdev->zoominfo.vir_height;
			if (y_addr > (pcdev->vipmem_phybase + pcdev->vipmem_size - pcdev->vipmem_bsize)) {
				RKCAMERA_TR("vipmem for IPP is overflow! %dx%d -> %dx%d vb_index:%d\n",pcdev->host_width,pcdev->host_height,
					          pcdev->icd->user_width,pcdev->icd->user_height, vb->i);
				BUG();
			}
		} else {
			y_addr = vb->boff;
			uv_addr = y_addr + vb->width * vb->height;
		}
#if defined(CONFIG_ARCH_RK3188)
		rk_camera_cif_reset(pcdev,false);
#endif
        write_cif_reg(pcdev->base,CIF_CIF_FRM0_ADDR_Y, y_addr);
        write_cif_reg(pcdev->base,CIF_CIF_FRM0_ADDR_UV, uv_addr);
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
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;
#if CAMERA_VIDEOBUF_ARM_ACCESS    
    struct rk29_camera_vbinfo *vb_info;
#endif


	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

    /*dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__, 
            vb, vb->baddr, vb->bsize); */ /*yzm*/

    vb->state = VIDEOBUF_QUEUED;
	if (list_empty(&pcdev->capture)) {
		list_add_tail(&vb->queue, &pcdev->capture);
	} else {
		if (list_entry(pcdev->capture.next, struct videobuf_buffer, queue) != vb)
			list_add_tail(&vb->queue, &pcdev->capture);
		else
			BUG();    /* ddl@rock-chips.com : The same videobuffer queue again */
	}
#if CAMERA_VIDEOBUF_ARM_ACCESS
    if (pcdev->vbinfo) {
        vb_info = pcdev->vbinfo+vb->i;
        if ((vb_info->phy_addr != vb->boff) || (vb_info->size != vb->bsize)) {
            if (vb_info->vir_addr) {
                iounmap(vb_info->vir_addr);
                release_mem_region(vb_info->phy_addr, vb_info->size);
                vb_info->vir_addr = NULL;
                vb_info->phy_addr = 0x00;
                vb_info->size = 0x00;
            }

            if (request_mem_region(vb->boff,vb->bsize,"rk_camera_vb")) {
                vb_info->vir_addr = ioremap_cached(vb->boff,vb->bsize); 
            }
            
            if (vb_info->vir_addr) {
                vb_info->size = vb->bsize;
                vb_info->phy_addr = vb->boff;
            } else {
                RKCAMERA_TR("ioremap videobuf %d failed\n",vb->i);
            }
        }
    }
#endif    
    if (!pcdev->active) {
        pcdev->active = vb;
        rk_videobuf_capture(vb,pcdev);
        if (atomic_read(&pcdev->stop_cif) == false) {           //ddl@rock-chips.com v0.3.0x13
            write_cif_reg(pcdev->base,CIF_CIF_CTRL, (read_cif_reg(pcdev->base,CIF_CIF_CTRL) | ENABLE_CAPTURE));
        }       
    }
}

#if (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_RGA)
static int rk_pixfmt2rgafmt(unsigned int pixfmt, int *ippfmt)
{

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	switch (pixfmt)
	{
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_UYVY: /* yuv 422, but sensor has defined this format(in fact ,should be defined yuv 420), treat this as yuv 420.*/
		case V4L2_PIX_FMT_YUYV: 
			{
				*ippfmt = RK_FORMAT_YCbCr_420_SP;
				break;
			}
		case V4L2_PIX_FMT_YVU420:
		case V4L2_PIX_FMT_VYUY:
		case V4L2_PIX_FMT_YVYU:
			{
				*ippfmt = RK_FORMAT_YCrCb_420_SP;
				break;
			}
		case V4L2_PIX_FMT_RGB565:
			{
				*ippfmt = RK_FORMAT_RGB_565;
				break;
			}
		case V4L2_PIX_FMT_RGB24:
			{
				*ippfmt = RK_FORMAT_RGB_888;
				break;
			}
		default:
			goto rk_pixfmt2rgafmt_err;
	}

	return 0;
rk_pixfmt2rgafmt_err:
	return -1;
}
#endif
#if (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_PP)
static int rk_camera_scale_crop_pp(struct work_struct *work){
	struct rk_camera_work *camera_work = container_of(work, struct rk_camera_work, work);
	struct videobuf_buffer *vb = camera_work->vb;
	struct rk_camera_dev *pcdev = camera_work->pcdev;
	int vipdata_base;
	unsigned long int flags;
	int scale_times,w,h;
	int src_y_offset;
	int ret = 0;

	return ret;
}
#endif
#if (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_RGA)
extern rga_service_info rga_service;
extern int rga_blit_sync(rga_session *session, struct rga_req *req);
extern	 void rga_service_session_clear(rga_session *session);
static int rk_camera_scale_crop_rga(struct work_struct *work){
	struct rk_camera_work *camera_work = container_of(work, struct rk_camera_work, work);
	struct videobuf_buffer *vb = camera_work->vb;
	struct rk_camera_dev *pcdev = camera_work->pcdev;
	int vipdata_base;
	unsigned long int flags;
	int scale_times,w,h;
	int src_y_offset;
	struct rga_req req;
	rga_session session;
	int rga_times = 3;
	const struct soc_mbus_pixelfmt *fmt;
	int ret = 0;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

		return ret;
	
}

#endif
#if (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_IPP)

static int rk_camera_scale_crop_ipp(struct work_struct *work)
{
   
	return 0;    
}
#endif
static void rk_camera_capture_process(struct work_struct *work)
{
    struct rk_camera_work *camera_work = container_of(work, struct rk_camera_work, work);    
    struct videobuf_buffer *vb = camera_work->vb;    
    struct rk_camera_dev *pcdev = camera_work->pcdev;    
    /*enum v4l2_mbus_pixelcode icd_code = pcdev->icd->current_fmt->code;    */
    unsigned long flags = 0;    
    int err = 0;    

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);


    if (atomic_read(&pcdev->stop_cif)==true) {
        err = -EINVAL;
        goto rk_camera_capture_process_end; 
    }
    
    if (!CAM_WORKQUEUE_IS_EN()) {
        err = -EINVAL;
        goto rk_camera_capture_process_end; 
    }
    
    down(&pcdev->zoominfo.sem);
    if (pcdev->icd_cb.scale_crop_cb){
        err = (pcdev->icd_cb.scale_crop_cb)(work);
    	}
    up(&pcdev->zoominfo.sem); 
    
    if (pcdev->icd_cb.sensor_cb)        
        (pcdev->icd_cb.sensor_cb)(vb);    

rk_camera_capture_process_end:    
    if (err) {        
        vb->state = VIDEOBUF_ERROR;    
    } else {
        if ((vb->state == VIDEOBUF_QUEUED) || (vb->state == VIDEOBUF_ACTIVE)) {
	        vb->state = VIDEOBUF_DONE;
	        vb->field_count++;
		}
    }       
    spin_lock_irqsave(&pcdev->camera_work_lock, flags);    
    list_add_tail(&camera_work->queue, &pcdev->camera_work_queue);    
    spin_unlock_irqrestore(&pcdev->camera_work_lock, flags); 
    wake_up(&(camera_work->vb->done));     /* ddl@rock-chips.com : v0.3.9 */ 
    return;
}

static void rk_camera_cifrest_delay(struct work_struct *work)
{
    struct rk_camera_work *camera_work = container_of(work, struct rk_camera_work, work);  
    struct rk_camera_dev *pcdev = camera_work->pcdev; 
    unsigned long flags = 0;   

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

    
    mdelay(1);
    rk_camera_cif_reset(pcdev,false);

    spin_lock_irqsave(&pcdev->camera_work_lock, flags);    
    list_add_tail(&camera_work->queue, &pcdev->camera_work_queue);    
    spin_unlock_irqrestore(&pcdev->camera_work_lock, flags); 

    spin_lock_irqsave(&pcdev->lock,flags);
    if (atomic_read(&pcdev->stop_cif) == false) {
        write_cif_reg(pcdev->base,CIF_CIF_CTRL, (read_cif_reg(pcdev->base,CIF_CIF_CTRL) | ENABLE_CAPTURE));
        RKCAMERA_DG2("After reset cif, enable capture again!\n");
    }
    spin_unlock_irqrestore(&pcdev->lock,flags);
    return;
}

static inline irqreturn_t rk_camera_cifirq(int irq, void *data)
{
    struct rk_camera_dev *pcdev = data;
    struct rk_camera_work *wk;
    unsigned int reg_cifctrl, reg_lastpix, reg_lastline;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);


    write_cif_reg(pcdev->base,CIF_CIF_INTSTAT,0x0200);  /* clear vip interrupte single  */
    
    reg_cifctrl = read_cif_reg(pcdev->base,CIF_CIF_CTRL);
    reg_lastpix = read_cif_reg(pcdev->base,CIF_CIF_LAST_PIX);
    reg_lastline = read_cif_reg(pcdev->base,CIF_CIF_LAST_LINE);
	
    pcdev->irqinfo.cifirq_idx++;    
    if ((reg_lastline != pcdev->host_height) /*|| (reg_lastpix != pcdev->host_width)*/) {
        pcdev->irqinfo.cifirq_abnormal_idx = pcdev->irqinfo.cifirq_idx;
        RKCAMERA_DG2("Cif irq-%ld is error, %dx%d != %dx%d\n",pcdev->irqinfo.cifirq_abnormal_idx,
                    reg_lastpix,reg_lastline,pcdev->host_width,pcdev->host_height);
    } else {
        pcdev->irqinfo.cifirq_normal_idx = pcdev->irqinfo.cifirq_idx;
    }
    
    if(reg_cifctrl & ENABLE_CAPTURE) {
        write_cif_reg(pcdev->base,CIF_CIF_CTRL, (reg_cifctrl & ~ENABLE_CAPTURE));
    } 

    if (pcdev->irqinfo.cifirq_abnormal_idx>0) {
        if ((pcdev->irqinfo.cifirq_idx - pcdev->irqinfo.cifirq_abnormal_idx) == 1 ) {
            if (!list_empty(&pcdev->camera_work_queue)) {
                RKCAMERA_DG2("Receive cif irq-%ld and queue work to cif reset\n",pcdev->irqinfo.cifirq_idx);
                wk = list_entry(pcdev->camera_work_queue.next, struct rk_camera_work, queue);
                list_del_init(&wk->queue);
                INIT_WORK(&(wk->work), rk_camera_cifrest_delay);
                wk->pcdev = pcdev;                
                queue_work(pcdev->camera_wq, &(wk->work));
            }  
        }
    }
    
    return IRQ_HANDLED;
}

static inline irqreturn_t rk_camera_dmairq(int irq, void *data)
{
    struct rk_camera_dev *pcdev = data;
    struct videobuf_buffer *vb;
	struct rk_camera_work *wk;
	struct timeval tv;
    unsigned long reg_cifctrl;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);


    reg_cifctrl = read_cif_reg(pcdev->base,CIF_CIF_CTRL);
    /* ddl@rock-chps.com : Current VIP is run in One Frame Mode, Frame 1 is validate */
    if (read_cif_reg(pcdev->base,CIF_CIF_FRAME_STATUS) & 0x01) {   //frame 0 ready yzm
        write_cif_reg(pcdev->base,CIF_CIF_INTSTAT,0x01);  /* clear vip interrupte single  */

        pcdev->irqinfo.dmairq_idx++;
        if (pcdev->irqinfo.cifirq_abnormal_idx == pcdev->irqinfo.dmairq_idx) {
            write_cif_reg(pcdev->base,CIF_CIF_FRAME_STATUS,  0x00000002);
            goto end;
        }

        if (!pcdev->fps) {
            do_gettimeofday(&pcdev->first_tv);            
        }
        pcdev->fps++;
        if (!pcdev->active)
            goto end;
        if (pcdev->frame_inval>0) {
            pcdev->frame_inval--;
            rk_videobuf_capture(pcdev->active,pcdev);
            goto end;
        } else if (pcdev->frame_inval) {
            RKCAMERA_TR("frame_inval : %0x",pcdev->frame_inval);
            pcdev->frame_inval = 0;
        }
        
        if(pcdev->fps == RK30_CAM_FRAME_MEASURE) {
            do_gettimeofday(&tv);            
            pcdev->frame_interval = ((tv.tv_sec*1000000 + tv.tv_usec) - (pcdev->first_tv.tv_sec*1000000 + pcdev->first_tv.tv_usec))
                /(RK30_CAM_FRAME_MEASURE-1);
        }
        
        vb = pcdev->active;
        if (!vb) {
            printk("no acticve buffer!!!\n");
            goto end;
        }
        
        /* ddl@rock-chips.com : this vb may be deleted from queue */
        if ((vb->state == VIDEOBUF_QUEUED) || (vb->state == VIDEOBUF_ACTIVE)) {
            list_del_init(&vb->queue);
        }
        pcdev->active = NULL;
        if (!list_empty(&pcdev->capture)) {
            pcdev->active = list_entry(pcdev->capture.next, struct videobuf_buffer, queue);
            if (pcdev->active) {
                WARN_ON(pcdev->active->state != VIDEOBUF_QUEUED);                     
                rk_videobuf_capture(pcdev->active,pcdev);
            }
        }
        if (pcdev->active == NULL) {
            RKCAMERA_DG1("video_buf queue is empty!\n");
        }

        do_gettimeofday(&vb->ts);
        if (CAM_WORKQUEUE_IS_EN()) {
            if (!list_empty(&pcdev->camera_work_queue)) {
                wk = list_entry(pcdev->camera_work_queue.next, struct rk_camera_work, queue);
                list_del_init(&wk->queue);
                INIT_WORK(&(wk->work), rk_camera_capture_process);
                wk->vb = vb;
                wk->pcdev = pcdev;
                queue_work(pcdev->camera_wq, &(wk->work));
            }             			
        } else {
            if ((vb->state == VIDEOBUF_QUEUED) || (vb->state == VIDEOBUF_ACTIVE)) {
                vb->state = VIDEOBUF_DONE;    	        
                vb->field_count++;
            }
            wake_up(&vb->done);
        }
    }

end:
    if((reg_cifctrl & ENABLE_CAPTURE) == 0)
        write_cif_reg(pcdev->base,CIF_CIF_CTRL, (reg_cifctrl | ENABLE_CAPTURE));
    return IRQ_HANDLED;
}

static irqreturn_t rk_camera_irq(int irq, void *data)
{
    struct rk_camera_dev *pcdev = data;
    unsigned long reg_intstat;



    spin_lock(&pcdev->lock);

    if(atomic_read(&pcdev->stop_cif) == true) {
        write_cif_reg(pcdev->base,CIF_CIF_INTSTAT,0xffffffff);
        goto end;
    }

    reg_intstat = read_cif_reg(pcdev->base,CIF_CIF_INTSTAT);
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s() ,reg_intstat 0x%lx\n", __FILE__, __LINE__,__FUNCTION__,reg_intstat);
    if (reg_intstat & 0x0200)
        rk_camera_cifirq(irq,data);

    if (reg_intstat & 0x01) 
        rk_camera_dmairq(irq,data);

end:    
    spin_unlock(&pcdev->lock);
    return IRQ_HANDLED;
}


static void rk_videobuf_release(struct videobuf_queue *vq,
                                  struct videobuf_buffer *vb)
{
    struct rk_camera_buffer *buf = container_of(vb, struct rk_camera_buffer, vb);
    struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;
#if CAMERA_VIDEOBUF_ARM_ACCESS    
    struct rk29_camera_vbinfo *vb_info =NULL;
#endif

#ifdef DEBUG

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


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

    flush_workqueue(pcdev->camera_wq); 
    
    rk_videobuf_free(vq, buf);
    
#if CAMERA_VIDEOBUF_ARM_ACCESS
    if ((pcdev->vbinfo) && (vb->i < pcdev->vbinfo_count)) {
        vb_info = pcdev->vbinfo + vb->i;
        
        if (vb_info->vir_addr) {
            iounmap(vb_info->vir_addr);
            release_mem_region(vb_info->phy_addr, vb_info->size);
            memset(vb_info, 0x00, sizeof(struct rk29_camera_vbinfo));
        }       
		
	}
#endif  
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
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


    /* We must pass NULL as dev pointer, then all pci_* dma operations
     * transform to normal dma_* ones. */
    videobuf_queue_dma_contig_init(q,
                                   &rk_videobuf_ops,
                                   ici->v4l2_dev.dev, &pcdev->lock,
                                   V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                   V4L2_FIELD_NONE,
                                   sizeof(struct rk_camera_buffer),
                                   icd,&(ici->host_lock) );
}

static int rk_camera_mclk_ctrl(int cif_idx, int on, int clk_rate)
{
    int err = 0,cif;    
    struct rk_cif_clk *clk;
    struct clk *cif_clk_out_div;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);


    cif = cif_idx - RK29_CAM_PLATFORM_DEV_ID;
    if ((cif<0)||(cif>1)) {
        RKCAMERA_TR(KERN_ERR "cif index(%d) is invalidate\n",cif_idx);
        err = -1;
        goto rk_camera_clk_ctrl_end;
    } 

    clk = &cif_clk[cif];
   
    if(!clk->aclk_cif || !clk->hclk_cif || !clk->cif_clk_in || !clk->cif_clk_out) {
        RKCAMERA_TR(KERN_ERR "failed to get cif clock source\n");
        err = -ENOENT;
        goto rk_camera_clk_ctrl_end;
    }
   
    //spin_lock(&clk->lock);
    if (on && !clk->on) {  
        clk_prepare_enable(clk->pd_cif);    /*yzm*/
        clk_prepare_enable(clk->aclk_cif);
    	clk_prepare_enable(clk->hclk_cif);
    	clk_prepare_enable(clk->cif_clk_in);
    	clk_prepare_enable(clk->cif_clk_out);
        clk_set_rate(clk->cif_clk_out,clk_rate);
        clk->on = true;
    } else if (!on && clk->on) {
    	msleep(100);
        clk_disable_unprepare(clk->aclk_cif);
    	clk_disable_unprepare(clk->hclk_cif);
    	clk_disable_unprepare(clk->cif_clk_in);
    	clk_disable_unprepare(clk->cif_clk_out);
    	clk_disable_unprepare(clk->pd_cif);
        clk->on = false;
        if(cif){
            cif_clk_out_div =  clk_get(NULL, "cif1_out_div");
        }else{
            cif_clk_out_div =  clk_get(NULL, "cif0_out_div");
            if(IS_ERR_OR_NULL(cif_clk_out_div)) {
                cif_clk_out_div =  clk_get(NULL, "cif_out_div");
            }
        }
        
        if(!IS_ERR_OR_NULL(cif_clk_out_div)) {   /* ddl@rock-chips.com: v0.3.0x13 */ 
            err = clk_set_parent(clk->cif_clk_out, cif_clk_out_div);
            clk_put(cif_clk_out_div);
        } else {
            err = -1;
        }
        
        if(err)
           RKCAMERA_TR("WARNING %s_%s_%d: camera sensor mclk maybe not close, please check!!!\n", __FILE__, __FUNCTION__, __LINE__); 
    }
    //spin_unlock(&clk->lock);
rk_camera_clk_ctrl_end:
    return err;
}
static int rk_camera_activate(struct rk_camera_dev *pcdev, struct soc_camera_device *icd)
{
    /*
    * ddl@rock-chips.com : Cif clk control in rk_sensor_power which in rk_camera.c
    */

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

    write_cif_reg(pcdev->base,CIF_CIF_CTRL,AXI_BURST_16|MODE_ONEFRAME|DISABLE_CAPTURE);   /* ddl@rock-chips.com : vip ahb burst 16 */
	//write_cif_reg(pcdev->base,CIF_CIF_INTEN, 0x01);    //capture complete interrupt enable
    return 0;
}

static void rk_camera_deactivate(struct rk_camera_dev *pcdev)
{ 
    /*
    * ddl@rock-chips.com : Cif clk control in rk_sensor_power which in rk_camera.c
    */
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

    
    return;
}

/* The following two functions absolutely depend on the fact, that
 * there can be only one camera on RK28 quick capture interface */
static int rk_camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;    /*Initialize in rk_camra_prob*/
    struct device *control = to_soc_camera_control(icd);
    struct v4l2_subdev *sd;
    int ret,i,icd_catch;
    struct rk_camera_frmivalenum *fival_list,*fival_nxt;
    struct v4l2_cropcap cropcap;
    struct v4l2_mbus_framefmt mf;
    const struct soc_camera_format_xlate *xlate = NULL;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

    
    mutex_lock(&camera_lock);

    if (pcdev->icd) {
        ret = -EBUSY;
        goto ebusy;
    }

    RKCAMERA_DG1("%s driver attached to %s\n",RK29_CAM_DRV_NAME,dev_name(icd->pdev));

	pcdev->frame_inval = RK_CAM_FRAME_INVAL_INIT;
    pcdev->active = NULL;
    pcdev->icd = NULL;
	pcdev->reginfo_suspend.Inval = Reg_Invalidate;
    pcdev->zoominfo.zoom_rate = 100;
    pcdev->fps_timer.istarted = false;
        
	/* ddl@rock-chips.com: capture list must be reset, because this list may be not empty,
     * if app havn't dequeue all videobuf before close camera device;
	*/
    INIT_LIST_HEAD(&pcdev->capture);

    ret = rk_camera_activate(pcdev,icd);
    if (ret)
        goto ebusy;
    /* ddl@rock-chips.com : v4l2_subdev is not created when ici->ops->add called in soc_camera_probe  */
    if (control) {		//TRUE in open ,FALSE in kernel start
        sd = dev_get_drvdata(control);
		v4l2_subdev_call(sd, core, ioctl, RK29_CAM_SUBDEV_IOREQUEST,(void*)pcdev->pdata);
        #if 0
        ret = v4l2_subdev_call(sd,core, init, 0);
        if (ret)
            goto ebusy;
        #endif
		/* call generic_sensor_ioctl*/
        v4l2_subdev_call(sd, core, ioctl, RK29_CAM_SUBDEV_CB_REGISTER,(void*)(&pcdev->icd_cb));
		/* call generic_sensor_cropcap*/
        if (v4l2_subdev_call(sd, video, cropcap, &cropcap) == 0) {
            memcpy(&pcdev->cropinfo.bounds ,&cropcap.bounds,sizeof(struct v4l2_rect));
        } else {
            xlate = soc_camera_xlate_by_fourcc(icd, V4L2_PIX_FMT_NV12);
            mf.width = 10000;
            mf.height = 10000;
            mf.field = V4L2_FIELD_NONE;
            mf.code = xlate->code;
            mf.reserved[6] = 0xfefe5a5a;
            v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);

            pcdev->cropinfo.bounds.left = 0;
            pcdev->cropinfo.bounds.top = 0;
            pcdev->cropinfo.bounds.width = mf.width;
            pcdev->cropinfo.bounds.height = mf.height;
        }
    }

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);
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
ebusy:
    mutex_unlock(&camera_lock);

    return ret;
}
static void rk_camera_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;
	/*struct v4l2_subdev *sd = soc_camera_to_subdev(icd);*/
#if CAMERA_VIDEOBUF_ARM_ACCESS    
    struct rk29_camera_vbinfo *vb_info;
    unsigned int i;
#endif 

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);


	mutex_lock(&camera_lock);
    BUG_ON(icd != pcdev->icd);

    RKCAMERA_DG1("%s driver detached from %s\n",RK29_CAM_DRV_NAME,dev_name(icd->pdev));
    
    /* ddl@rock-chips.com: Application will call VIDIOC_STREAMOFF before close device, but
	   stream may be turn on again before close device, if suspend and resume happened. */
	/*if (read_cif_reg(pcdev->base,CIF_CIF_CTRL) & ENABLE_CAPTURE) {*/
	if ((atomic_read(&pcdev->stop_cif) == false) && pcdev->fps_timer.istarted) {       /* ddl@rock-chips.com: v0.3.0x15*/
		rk_camera_s_stream(icd,0);
	} 
	/* move DEACTIVATE into generic_sensor_s_power*/
    /* v4l2_subdev_call(sd, core, ioctl, RK29_CAM_SUBDEV_DEACTIVATE,NULL);*/ /*yzm*/
    /* if stream off is not been executed,timer is running.*/
    if(pcdev->fps_timer.istarted){
         hrtimer_cancel(&pcdev->fps_timer.timer);
         pcdev->fps_timer.istarted = false;
    }
    flush_work(&(pcdev->camera_reinit_work.work));
	flush_workqueue((pcdev->camera_wq));
    
	if (pcdev->camera_work) {
		kfree(pcdev->camera_work);
		pcdev->camera_work = NULL;
		pcdev->camera_work_count = 0;
        INIT_LIST_HEAD(&pcdev->camera_work_queue);
	}
	rk_camera_deactivate(pcdev);
#if CAMERA_VIDEOBUF_ARM_ACCESS
    if (pcdev->vbinfo) {
        vb_info = pcdev->vbinfo;
        for (i=0; i<pcdev->vbinfo_count; i++) {
            if (vb_info->vir_addr) {
                iounmap(vb_info->vir_addr);
                release_mem_region(vb_info->phy_addr, vb_info->size);
                memset(vb_info, 0x00, sizeof(struct rk29_camera_vbinfo));
            }
            vb_info++;
        }
		kfree(pcdev->vbinfo);
		pcdev->vbinfo = NULL;
		pcdev->vbinfo_count = 0;
	}
#endif
	pcdev->active = NULL;
    pcdev->icd = NULL;
    pcdev->icd_cb.sensor_cb = NULL;
	pcdev->reginfo_suspend.Inval = Reg_Invalidate;
	/* ddl@rock-chips.com: capture list must be reset, because this list may be not empty,
     * if app havn't dequeue all videobuf before close camera device;
	*/
    INIT_LIST_HEAD(&pcdev->capture);

	mutex_unlock(&camera_lock);

	return;
}
static int rk_camera_set_bus_param(struct soc_camera_device *icd, __u32 pixfmt)
{
    unsigned long bus_flags, camera_flags, common_flags = 0;
    unsigned int cif_for = 0;
	const struct soc_mbus_pixelfmt *fmt;
	int ret = 0;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent); /*yzm*/
	struct rk_camera_dev *pcdev = ici->priv;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


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
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);
	if (icd->ops->query_bus_param)
    	camera_flags = icd->ops->query_bus_param(icd);
	else
		camera_flags = 0;

/**************yzm************
    common_flags = soc_camera_bus_param_compatible(camera_flags, bus_flags);
    if (!common_flags) {
        ret = -EINVAL;
        goto RK_CAMERA_SET_BUS_PARAM_END;
    }
*/
/***************yzm************end*/

	
	common_flags = camera_flags;
    ret = icd->ops->set_bus_param(icd, common_flags);
    if (ret < 0)
        goto RK_CAMERA_SET_BUS_PARAM_END;

    cif_for = read_cif_reg(pcdev->base,CIF_CIF_FOR);
    
    if (common_flags & V4L2_MBUS_PCLK_SAMPLE_FALLING) {
       	if(IS_CIF0()) {
    		write_cru_reg(CRU_PCLK_REG30, read_cru_reg(CRU_PCLK_REG30) | ENANABLE_INVERT_PCLK_CIF0);
        } else {
    		write_cru_reg(CRU_PCLK_REG30, read_cru_reg(CRU_PCLK_REG30) | ENANABLE_INVERT_PCLK_CIF1);
        }
    } else {
		if(IS_CIF0()){
			write_cru_reg(CRU_PCLK_REG30, (read_cru_reg(CRU_PCLK_REG30) & 0xFFFFEFF ) | DISABLE_INVERT_PCLK_CIF0);
        } else {
			write_cru_reg(CRU_PCLK_REG30, (read_cru_reg(CRU_PCLK_REG30) & 0xFFFEFFF) | DISABLE_INVERT_PCLK_CIF1);
        }
    }
    
    if (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW) {
        cif_for |= HSY_LOW_ACTIVE;
    } else {
		cif_for &= ~HSY_LOW_ACTIVE;
    }
    if (common_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH) {
        cif_for |= VSY_HIGH_ACTIVE;
    } else {
		cif_for &= ~VSY_HIGH_ACTIVE;
    }

    // ddl@rock-chips.com : Don't enable capture here, enable in stream_on 
    //vip_ctrl_val |= ENABLE_CAPTURE;
    write_cif_reg(pcdev->base,CIF_CIF_FOR, cif_for);
    RKCAMERA_DG1("CIF_CIF_FOR: 0x%x \n",cif_for);

RK_CAMERA_SET_BUS_PARAM_END:
	if (ret)
    	RKCAMERA_TR("rk_camera_set_bus_param ret = %d \n", ret);
    return ret;
}

static int rk_camera_try_bus_param(struct soc_camera_device *icd, __u32 pixfmt)
{
/*    unsigned long bus_flags, camera_flags;*/
/*    int ret;*/

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);
/**********yzm***********

    bus_flags = RK_CAM_BUS_PARAM;
	if (icd->ops->query_bus_param) {
        camera_flags = icd->ops->query_bus_param(icd);  //generic_sensor_query_bus_param()
	} else {
		camera_flags = 0;
	}
    ret = soc_camera_bus_param_compatible(camera_flags, bus_flags) ;

    if (ret < 0)
        dev_warn(icd->dev.parent,
			 "Flags incompatible: camera %lx, host %lx\n",
			 camera_flags, bus_flags);

    return ret;
*///************yzm **************end
	return 0;

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
	},{
		.fourcc 		= V4L2_PIX_FMT_RGB565,
		.name			= "RGB565",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	},{
		.fourcc 		= V4L2_PIX_FMT_RGB24,
		.name			= "RGB888",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	}

};

static void rk_camera_setup_format(struct soc_camera_device *icd, __u32 host_pixfmt, enum v4l2_mbus_pixelcode icd_code, struct v4l2_rect *rect)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;
    unsigned int cif_fs = 0,cif_crop = 0;
    unsigned int cif_fmt_val = read_cif_reg(pcdev->base,CIF_CIF_FOR) | INPUT_MODE_YUV|YUV_INPUT_422|INPUT_420_ORDER_EVEN|OUTPUT_420_ORDER_EVEN;
	
	const struct soc_mbus_pixelfmt *fmt;
	fmt = soc_mbus_get_fmtdesc(icd_code);

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


	if((host_pixfmt == V4L2_PIX_FMT_RGB565) || (host_pixfmt == V4L2_PIX_FMT_RGB24)){
		if(fmt->fourcc == V4L2_PIX_FMT_NV12)
			host_pixfmt = V4L2_PIX_FMT_NV12;
		else if(fmt->fourcc == V4L2_PIX_FMT_NV21)
			host_pixfmt = V4L2_PIX_FMT_NV21;
	}
    switch (host_pixfmt)
    {
        case V4L2_PIX_FMT_NV16:
            cif_fmt_val &= ~YUV_OUTPUT_422;
		    cif_fmt_val &= ~UV_STORAGE_ORDER_UVUV;
		    pcdev->frame_inval = RK_CAM_FRAME_INVAL_DC;
		    pcdev->pixfmt = host_pixfmt;
            break;
    	case V4L2_PIX_FMT_NV61:
    		cif_fmt_val &= ~YUV_OUTPUT_422;
    		cif_fmt_val |= UV_STORAGE_ORDER_VUVU;
    		pcdev->frame_inval = RK_CAM_FRAME_INVAL_DC;
    		pcdev->pixfmt = host_pixfmt;
    		break;
        case V4L2_PIX_FMT_NV12:
            cif_fmt_val |= YUV_OUTPUT_420;
    		cif_fmt_val &= ~UV_STORAGE_ORDER_UVUV;
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
            cif_fmt_val = YUV_INPUT_ORDER_UYVY(cif_fmt_val);
            break;
        case V4L2_MBUS_FMT_YUYV8_2X8:
            cif_fmt_val = YUV_INPUT_ORDER_YUYV(cif_fmt_val);
            break;
    	case V4L2_MBUS_FMT_YVYU8_2X8:
    		cif_fmt_val = YUV_INPUT_ORDER_YVYU(cif_fmt_val);
    		break;
    	case V4L2_MBUS_FMT_VYUY8_2X8:
    		cif_fmt_val = YUV_INPUT_ORDER_VYUY(cif_fmt_val);
    		break;
        default :
			cif_fmt_val = YUV_INPUT_ORDER_YUYV(cif_fmt_val);
            break;
    }

    mdelay(100);
    rk_camera_cif_reset(pcdev,true);
    
    write_cif_reg(pcdev->base,CIF_CIF_CTRL,AXI_BURST_16|MODE_ONEFRAME|DISABLE_CAPTURE);   /* ddl@rock-chips.com : vip ahb burst 16 */
    //write_cif_reg(pcdev->base,CIF_CIF_INTEN, 0x01|0x200);    /*capture complete interrupt enable*/

    write_cif_reg(pcdev->base,CIF_CIF_FOR,cif_fmt_val);         /* ddl@rock-chips.com: VIP capture mode and capture format must be set before FS register set */

    write_cif_reg(pcdev->base,CIF_CIF_INTSTAT,0xFFFFFFFF); 
    if((read_cif_reg(pcdev->base,CIF_CIF_CTRL) & MODE_PINGPONG)
		||(read_cif_reg(pcdev->base,CIF_CIF_CTRL) & MODE_LINELOOP)) {
	    BUG();	
    } else{ /* this is one frame mode*/
	    cif_crop = (rect->left + (rect->top <<16));
	    cif_fs	= (rect->width + (rect->height <<16));
	}

	write_cif_reg(pcdev->base,CIF_CIF_CROP, cif_crop);
	write_cif_reg(pcdev->base,CIF_CIF_SET_SIZE, cif_fs);
	write_cif_reg(pcdev->base,CIF_CIF_VIR_LINE_WIDTH, rect->width);
	write_cif_reg(pcdev->base,CIF_CIF_FRAME_STATUS,  0x00000003);

    /*MUST bypass scale */
	write_cif_reg(pcdev->base,CIF_CIF_SCL_CTRL,0x10);
    RKCAMERA_DG1("CIF_CIF_CROP:0x%x  CIF_CIF_FS:0x%x  CIF_CIF_FOR:0x%x\n",cif_crop,cif_fs,cif_fmt_val);
	return;
}

static int rk_camera_get_formats(struct soc_camera_device *icd, unsigned int idx,
				  struct soc_camera_format_xlate *xlate)
{
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;/*yzm*/
    int formats = 0, ret;
	enum v4l2_mbus_pixelcode code;
	const struct soc_mbus_pixelfmt *fmt;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, idx, &code);  /*call generic_sensor_enum_fmt()*/
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
        {
        
#if (CONFIG_CAMERA_SCALE_CROP_MACHINE != RK_CAM_SCALE_CROP_RGA)
    		formats++;
    		if (xlate) {
    			xlate->host_fmt = &rk_camera_formats[0];
    			xlate->code = code;
    			xlate++;
    			dev_dbg(dev, "Providing format %s using code %d\n",
    				rk_camera_formats[0].name,code);
    		}
    		
    		formats++;
    		if (xlate) {
    			xlate->host_fmt = &rk_camera_formats[1];
    			xlate->code = code;
    			xlate++;
    			dev_dbg(dev, "Providing format %s using code %d\n",
    				rk_camera_formats[1].name,code);
    		}
    		
    		formats++;
    		if (xlate) {
    			xlate->host_fmt = &rk_camera_formats[2];
    			xlate->code = code;
    			xlate++;
    			dev_dbg(dev, "Providing format %s using code %d\n",
    				rk_camera_formats[2].name,code);
    		} 
    		
    		formats++;
    		if (xlate) {
    			xlate->host_fmt = &rk_camera_formats[3];
    			xlate->code = code;
    			xlate++;
    			dev_dbg(dev, "Providing format %s using code %d\n",
    				rk_camera_formats[3].name,code);
    		}
    		break;	
#else 
    		formats++;
    		if (xlate) {
    			xlate->host_fmt = &rk_camera_formats[4];
    			xlate->code = code;
    			xlate++;
    			dev_dbg(dev, "Providing format %s using code %d\n",
    				rk_camera_formats[4].name,code);
    		}
    		formats++;
    		if (xlate) {
    			xlate->host_fmt = &rk_camera_formats[5];
    			xlate->code = code;
    			xlate++;
    			dev_dbg(dev, "Providing format %s using code %d\n",
    				rk_camera_formats[5].name,code);
    		}
			break;		
#endif			
        }
        default:
            break;
    }

    return formats;
}

static void rk_camera_put_formats(struct soc_camera_device *icd)
{

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	return;
}
static int rk_camera_cropcap(struct soc_camera_device *icd, struct v4l2_cropcap *cropcap)
{
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
    int ret=0;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

    ret = v4l2_subdev_call(sd, video, cropcap, cropcap);
    if (ret != 0)
        goto end;
    /* ddl@rock-chips.com: driver decide the cropping rectangle */
    memset(&cropcap->defrect,0x00,sizeof(struct v4l2_rect));
end:    
    return ret;
}
static int rk_camera_get_crop(struct soc_camera_device *icd,struct v4l2_crop *crop)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


    spin_lock(&pcdev->cropinfo.lock);
    memcpy(&crop->c,&pcdev->cropinfo.c,sizeof(struct v4l2_rect));
    spin_unlock(&pcdev->cropinfo.lock);
    
    return 0;
}
static int rk_camera_set_crop(struct soc_camera_device *icd,
			       const struct v4l2_crop *crop)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


    spin_lock(&pcdev->cropinfo.lock);
    memcpy(&pcdev->cropinfo.c,&crop->c,sizeof(struct v4l2_rect));
    spin_unlock(&pcdev->cropinfo.lock);
    return 0;
}
static bool rk_camera_fmt_capturechk(struct v4l2_format *f)
{
    bool ret = false;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


    if (f->fmt.pix.priv == 0xfefe5a5a) {
        ret = true;
    }   
   
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
	} else if ((f->fmt.pix.width == 3264) && (f->fmt.pix.height == 2448)) {
		ret = true;
	}

	if (ret == true)
		RKCAMERA_DG1("%dx%d is capture format\n",f->fmt.pix.width, f->fmt.pix.height);
	return ret;
}
static int rk_camera_set_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct device *dev = icd->parent;/*yzm*/
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
    const struct soc_camera_format_xlate *xlate = NULL;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;
    struct v4l2_pix_format *pix = &f->fmt.pix;
    struct v4l2_mbus_framefmt mf;
    struct v4l2_rect rect;
    int ret,usr_w,usr_h,sensor_w,sensor_h;
    int stream_on = 0;
    int ratio, bounds_aspect;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	
	usr_w = pix->width;
	usr_h = pix->height;
    
    RKCAMERA_DG1("enter width:%d  height:%d\n",usr_w,usr_h);
    xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
    if (!xlate) {
        dev_err(dev, "Format %x not found\n", pix->pixelformat);
        ret = -EINVAL;
        goto RK_CAMERA_SET_FMT_END;
    }
    
    /* ddl@rock-chips.com: sensor init code transmit in here after open */
    if (pcdev->icd_init == 0) {
        v4l2_subdev_call(sd,core, init, 0);  /*call generic_sensor_init()*/
        pcdev->icd_init = 1;
        return 0;							/*directly return !!!!!!*/
    }
    stream_on = read_cif_reg(pcdev->base,CIF_CIF_CTRL);
    if (stream_on & ENABLE_CAPTURE)
        write_cif_reg(pcdev->base,CIF_CIF_CTRL, (stream_on & (~ENABLE_CAPTURE)));

    mf.width	= pix->width;
    mf.height	= pix->height;
    mf.field	= pix->field;
    mf.colorspace	= pix->colorspace;
    mf.code		= xlate->code;
    mf.reserved[0] = pix->priv;              /* ddl@rock-chips.com : v0.3.3 */
    mf.reserved[1] = 0;

    ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);  /*generic_sensor_s_fmt*/
    if (mf.code != xlate->code)
		return -EINVAL;    		

    if ((pcdev->cropinfo.c.width == pcdev->cropinfo.bounds.width) && 
        (pcdev->cropinfo.c.height == pcdev->cropinfo.bounds.height)) {
        bounds_aspect = (pcdev->cropinfo.bounds.width*10/pcdev->cropinfo.bounds.height);
        if ((mf.width*10/mf.height) != bounds_aspect) {
            RKCAMERA_DG1("User request fov unchanged in %dx%d, But sensor %dx%d is croped, so switch to full resolution %dx%d\n",
                        usr_w,usr_h,mf.width, mf.height,pcdev->cropinfo.bounds.width,pcdev->cropinfo.bounds.height);
            
            mf.width	= pcdev->cropinfo.bounds.width/4;
            mf.height	= pcdev->cropinfo.bounds.height/4;

            mf.field	= pix->field;
            mf.colorspace	= pix->colorspace;
            mf.code		= xlate->code;
            mf.reserved[0] = pix->priv; 
            mf.reserved[1] = 0;

            ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
            if (mf.code != xlate->code)
            	return -EINVAL; 
        }
    }

    sensor_w = mf.width;
    sensor_h = mf.height;

    ratio = ((mf.width*mf.reserved[1])/100)&(~0x03);      /* 4 align*/
    mf.width -= ratio;

    ratio = ((ratio*mf.height/mf.width)+1)&(~0x01);       /* 2 align*/
    mf.height -= ratio;

	if ((mf.width != usr_w) || (mf.height != usr_h)) {
        
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
        
		spin_lock(&pcdev->cropinfo.lock);
		if (((mf.width*10/mf.height) != (usr_w*10/usr_h))) {  
            if ((pcdev->cropinfo.c.width == 0)&&(pcdev->cropinfo.c.height == 0)) {        
                /*Scale + Crop center is for keep aspect ratio unchange*/
                ratio = ((mf.width*10/usr_w) >= (mf.height*10/usr_h))?(mf.height*10/usr_h):(mf.width*10/usr_w);
                pcdev->host_width = ratio*usr_w/10;
                pcdev->host_height = ratio*usr_h/10;
                pcdev->host_width &= ~CROP_ALIGN_BYTES;
                pcdev->host_height &= ~CROP_ALIGN_BYTES;

                pcdev->host_left = ((sensor_w-pcdev->host_width )>>1);
                pcdev->host_top = ((sensor_h-pcdev->host_height)>>1);
            } else {    
                /*Scale + crop(user define)*/
                pcdev->host_width = pcdev->cropinfo.c.width*mf.width/pcdev->cropinfo.bounds.width;
                pcdev->host_height = pcdev->cropinfo.c.height*mf.height/pcdev->cropinfo.bounds.height;
                pcdev->host_left = (pcdev->cropinfo.c.left*mf.width/pcdev->cropinfo.bounds.width);
                pcdev->host_top = (pcdev->cropinfo.c.top*mf.height/pcdev->cropinfo.bounds.height);
            }

            pcdev->host_left &= (~0x01);
            pcdev->host_top &= (~0x01);
        } else { 
            if ((pcdev->cropinfo.c.width == 0)&&(pcdev->cropinfo.c.height == 0)) {
                /*Crop Center for cif can work , then scale*/
                pcdev->host_width = mf.width;
                pcdev->host_height = mf.height;
                pcdev->host_left = ((sensor_w - mf.width)>>1)&(~0x01);
                pcdev->host_top = ((sensor_h - mf.height)>>1)&(~0x01);
            } else {
                /*Crop center for cif can work + crop(user define), then scale */
                pcdev->host_width = pcdev->cropinfo.c.width*mf.width/pcdev->cropinfo.bounds.width;
                pcdev->host_height = pcdev->cropinfo.c.height*mf.height/pcdev->cropinfo.bounds.height;
                pcdev->host_left = (pcdev->cropinfo.c.left*mf.width/pcdev->cropinfo.bounds.width)+((sensor_w - mf.width)>>1);
                pcdev->host_top = (pcdev->cropinfo.c.top*mf.height/pcdev->cropinfo.bounds.height)+((sensor_h - mf.height)>>1);
            }

            pcdev->host_left &= (~0x01);
            pcdev->host_top &= (~0x01);
        }
        spin_unlock(&pcdev->cropinfo.lock);
    } else {
        spin_lock(&pcdev->cropinfo.lock);
        if ((pcdev->cropinfo.c.width == 0)&&(pcdev->cropinfo.c.height == 0)) {
            pcdev->host_width = mf.width;
            pcdev->host_height = mf.height;
            pcdev->host_left = 0;
            pcdev->host_top = 0;
        } else {
            pcdev->host_width = pcdev->cropinfo.c.width*mf.width/pcdev->cropinfo.bounds.width;
            pcdev->host_height = pcdev->cropinfo.c.height*mf.height/pcdev->cropinfo.bounds.height;
            pcdev->host_left = (pcdev->cropinfo.c.left*mf.width/pcdev->cropinfo.bounds.width);
            pcdev->host_top = (pcdev->cropinfo.c.top*mf.height/pcdev->cropinfo.bounds.height);
        }
        spin_unlock(&pcdev->cropinfo.lock);
    }
    
    icd->sense = NULL;
    if (!ret) {
        rect.width = pcdev->host_width;
        rect.height = pcdev->host_height;
    	rect.left = pcdev->host_left;
    	rect.top = pcdev->host_top;
        
        down(&pcdev->zoominfo.sem);
#if CIF_DO_CROP   /* this crop is only for digital zoom*/
        pcdev->zoominfo.a.c.left = 0;
        pcdev->zoominfo.a.c.top = 0;
        pcdev->zoominfo.a.c.width = pcdev->host_width*100/pcdev->zoominfo.zoom_rate;
        pcdev->zoominfo.a.c.width &= ~CROP_ALIGN_BYTES;
        pcdev->zoominfo.a.c.height = pcdev->host_height*100/pcdev->zoominfo.zoom_rate;
        pcdev->zoominfo.a.c.height &= ~CROP_ALIGN_BYTES;
        pcdev->zoominfo.vir_width = pcdev->zoominfo.a.c.width;
        pcdev->zoominfo.vir_height = pcdev->zoominfo.a.c.height;
        /*recalculate the CIF width & height*/
        rect.width = pcdev->zoominfo.a.c.width ;
        rect.height = pcdev->zoominfo.a.c.height;
        rect.left = ((((pcdev->host_width - pcdev->zoominfo.a.c.width)>>1))+pcdev->host_left)&(~0x01);
        rect.top = ((((pcdev->host_height - pcdev->zoominfo.a.c.height)>>1))+pcdev->host_top)&(~0x01);
#else
        pcdev->zoominfo.a.c.width = pcdev->host_width*100/pcdev->zoominfo.zoom_rate;
        pcdev->zoominfo.a.c.width &= ~CROP_ALIGN_BYTES;
        pcdev->zoominfo.a.c.height = pcdev->host_height*100/pcdev->zoominfo.zoom_rate;
        pcdev->zoominfo.a.c.height &= ~CROP_ALIGN_BYTES;
        /*now digital zoom use ipp to do crop and scale*/
        if(pcdev->zoominfo.zoom_rate != 100){
            pcdev->zoominfo.a.c.left = ((pcdev->host_width - pcdev->zoominfo.a.c.width)>>1)&(~0x01);
            pcdev->zoominfo.a.c.top = ((pcdev->host_height - pcdev->zoominfo.a.c.height)>>1)&(~0x01);
        } else {
            pcdev->zoominfo.a.c.left = 0;
            pcdev->zoominfo.a.c.top = 0;
        }
        pcdev->zoominfo.vir_width = pcdev->host_width;
        pcdev->zoominfo.vir_height = pcdev->host_height;
#endif
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
        
        RKCAMERA_DG1("%s CIF Host:%dx%d@(%d,%d) Sensor:%dx%d->%dx%d User crop:(%d,%d,%d,%d)in(%d,%d) (zoom: %dx%d@(%d,%d)->%dx%d)\n",xlate->host_fmt->name,
			           pcdev->host_width,pcdev->host_height,pcdev->host_left,pcdev->host_top,
			           sensor_w,sensor_h,mf.width,mf.height,
			           pcdev->cropinfo.c.left,pcdev->cropinfo.c.top,pcdev->cropinfo.c.width,pcdev->cropinfo.c.height,
			           pcdev->cropinfo.bounds.width,pcdev->cropinfo.bounds.height,
			           pcdev->zoominfo.a.c.width,pcdev->zoominfo.a.c.height, pcdev->zoominfo.a.c.left,pcdev->zoominfo.a.c.top,
			           pix->width, pix->height);
			           
        rk_camera_setup_format(icd, pix->pixelformat, mf.code, &rect); 
        
		if (CAM_IPPWORK_IS_EN()) {
			BUG_ON(pcdev->vipmem_phybase == 0);
		}
        pix->width = usr_w;
    	pix->height = usr_h;
    	pix->field = mf.field;
    	pix->colorspace = mf.colorspace;
    	icd->current_fmt = xlate;   
        pcdev->icd_width = mf.width;
        pcdev->icd_height = mf.height;
    }

RK_CAMERA_SET_FMT_END:
    if (stream_on & ENABLE_CAPTURE)
        write_cif_reg(pcdev->base,CIF_CIF_CTRL, (read_cif_reg(pcdev->base,CIF_CIF_CTRL) | ENABLE_CAPTURE));
	if (ret)
    	RKCAMERA_TR("\n%s..%d.. ret = %d  \n",__FUNCTION__,__LINE__, ret);
    return ret;
}

static int rk_camera_try_fmt(struct soc_camera_device *icd,
                                   struct v4l2_format *f)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
	struct rk_camera_dev *pcdev = ici->priv;
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
    const struct soc_camera_format_xlate *xlate;
    struct v4l2_pix_format *pix = &f->fmt.pix;
    __u32 pixfmt = pix->pixelformat;
    int ret,usr_w,usr_h,i;
	bool is_capture = rk_camera_fmt_capturechk(f);  /* testing f is in line with the already set*/
	bool vipmem_is_overflow = false;
    struct v4l2_mbus_framefmt mf;
    int bytes_per_line_host;
    
	usr_w = pix->width;
	usr_h = pix->height;
    
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

    xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);  
    if (!xlate) {
        /*dev_err(icd->dev.parent, "Format (%c%c%c%c) not found\n", pixfmt & 0xFF, (pixfmt >> 8) & 0xFF,*/
		dev_err(icd->parent, "Format (%c%c%c%c) not found\n", pixfmt & 0xFF, (pixfmt >> 8) & 0xFF,/*yzm*/
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
    memset(&mf,0x00,sizeof(struct v4l2_mbus_framefmt));
	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;
    /* ddl@rock-chips.com : It is query max resolution only. */
    if ((usr_w == 10000) && (usr_h == 10000)) {
        mf.reserved[6] = 0xfefe5a5a;
    }
	/* call generic_sensor_try_fmt()*/
	ret = v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
	if (ret < 0)
		goto RK_CAMERA_TRY_FMT_END;
    
	/*query resolution.*/
	if((usr_w == 10000) && (usr_h == 10000)) {
		pix->width = mf.width;
        pix->height = mf.height;
        RKCAMERA_DG1("Sensor resolution : %dx%d\n",mf.width,mf.height);
		goto RK_CAMERA_TRY_FMT_END;
	} else {
        RKCAMERA_DG1("user demand: %dx%d  sensor output: %dx%d \n",usr_w,usr_h,mf.width,mf.height);
	}    
	    
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
			/*RKCAMERA_TR("vipmem for IPP is overflow, This resolution(%dx%d -> %dx%d) is invalidate!\n",mf.width,mf.height,usr_w,usr_h);*/ /*yzm*/
            pix->width = mf.width;
            pix->height = mf.height;            
		}
        /* ddl@rock-chips.com: Invalidate these code, because sensor need interpolate */
        #if 0     
        if ((mf.width < usr_w) || (mf.height < usr_h)) {
            if (((usr_w>>1) > mf.width) || ((usr_h>>1) > mf.height)) {
                RKCAMERA_TR("The aspect ratio(%dx%d/%dx%d) is bigger than 2 !\n",mf.width,mf.height,usr_w,usr_h);
                pix->width = mf.width;
                pix->height = mf.height;
            }
        }    
        #endif
	}
	
    pix->colorspace	= mf.colorspace;    

    switch (mf.field) {
	case V4L2_FIELD_ANY:
	case V4L2_FIELD_NONE:
		pix->field	= V4L2_FIELD_NONE;
		break;
	default:
		/* TODO: support interlaced at least in pass-through mode */
		dev_err(icd->parent, "Field type %d unsupported.\n",
			mf.field);
		goto RK_CAMERA_TRY_FMT_END;
	}

RK_CAMERA_TRY_FMT_END:
	if (ret<0)
    	RKCAMERA_TR("\n%s..%d.. ret = %d  \n",__FUNCTION__,__LINE__, ret);
    return ret;
}

static int rk_camera_reqbufs(struct soc_camera_device *icd,
                               struct v4l2_requestbuffers *p)
{
    int i;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


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

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


    buf = list_entry(icd->vb_vidq.stream.next, struct rk_camera_buffer,
                    vb.stream);

    poll_wait(file, &buf->vb.done, pt);

    if (buf->vb.state == VIDEOBUF_DONE ||
            buf->vb.state == VIDEOBUF_ERROR)
        return POLLIN|POLLRDNORM;

    return 0;
}
/*
*card:  sensor name _ facing _ device index - orientation _ fov horizontal _ fov vertical
*           10          5           1            3              3                3         + 5 < 32           
*/

static int rk_camera_querycap(struct soc_camera_host *ici,
                                struct v4l2_capability *cap)
{
    struct rk_camera_dev *pcdev = ici->priv;
    struct rkcamera_platform_data *new_camera;
    char orientation[5];
    char fov[9];
    int i;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);


    strlcpy(cap->card, dev_name(pcdev->icd->pdev), 18);       
    memset(orientation,0x00,sizeof(orientation));

    i=0;
    new_camera = pcdev->pdata->register_dev_new;
    while(new_camera != NULL){
        if (strcmp(dev_name(pcdev->icd->pdev), new_camera->dev_name) == 0) {
            sprintf(orientation,"-%d",new_camera->orientation);
            sprintf(fov,"_%d_%d",new_camera->fov_h,new_camera->fov_v);
        }
        new_camera = new_camera->next_camera;
    }
    
    if (orientation[0] != '-') {
        RKCAMERA_TR("%s: %s is not registered in rk29_camera_platform_data, orientation apply default value",__FUNCTION__,dev_name(pcdev->icd->pdev));
        if (strstr(dev_name(pcdev->icd->pdev),"front")) 
            strcat(cap->card,"-270");
        else 
            strcat(cap->card,"-90");
    } else {
        strcat(cap->card,orientation); 
    }
    
    strcat(cap->card,fov);                          /* ddl@rock-chips.com: v0.3.f */
    cap->version = RK_CAM_VERSION_CODE;
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

    return 0;
}
static int rk_camera_suspend(struct soc_camera_device *icd, pm_message_t state)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd;
    int ret = 0;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


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
		pcdev->reginfo_suspend.cifVirWidth = read_cif_reg(pcdev->base,CIF_CIF_VIR_LINE_WIDTH);
		pcdev->reginfo_suspend.cifScale= read_cif_reg(pcdev->base,CIF_CIF_SCL_CTRL);
		
		pcdev->reginfo_suspend.Inval = Reg_Validate;
		rk_camera_deactivate(pcdev);

		RKCAMERA_DG1("%s Enter Success...\n", __FUNCTION__);
	} else {
		RKCAMERA_DG1("%s icd has been deattach, don't need enter suspend\n", __FUNCTION__);
	}
	mutex_unlock(&camera_lock);
    return ret;
}

static int rk_camera_resume(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd;
    int ret = 0;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


	mutex_lock(&camera_lock);
	if ((pcdev->icd == icd) && (icd->ops->resume)) {
		if (pcdev->reginfo_suspend.Inval == Reg_Validate) {
			rk_camera_activate(pcdev, icd);
			write_cif_reg(pcdev->base,CIF_CIF_CTRL, pcdev->reginfo_suspend.cifCtrl&~ENABLE_CAPTURE);
			write_cif_reg(pcdev->base,CIF_CIF_INTEN, pcdev->reginfo_suspend.cifIntEn);
			write_cif_reg(pcdev->base,CIF_CIF_CROP, pcdev->reginfo_suspend.cifCrop);
			write_cif_reg(pcdev->base,CIF_CIF_SET_SIZE, pcdev->reginfo_suspend.cifFs);
			write_cif_reg(pcdev->base,CIF_CIF_FOR, pcdev->reginfo_suspend.cifFmt);
			write_cif_reg(pcdev->base,CIF_CIF_VIR_LINE_WIDTH,pcdev->reginfo_suspend.cifVirWidth);
			write_cif_reg(pcdev->base,CIF_CIF_SCL_CTRL, pcdev->reginfo_suspend.cifScale);
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

		RKCAMERA_DG1("%s Enter success\n",__FUNCTION__);
	} else {
		RKCAMERA_DG1("%s icd has been deattach, don't need enter resume\n", __FUNCTION__);
	}

rk_camera_resume_end:
	mutex_unlock(&camera_lock);
    return ret;
}

static void rk_camera_reinit_work(struct work_struct *work)
{
    struct v4l2_subdev *sd;
	struct rk_camera_work *camera_work = container_of(work, struct rk_camera_work, work);
	struct rk_camera_dev *pcdev = camera_work->pcdev;
    /*struct soc_camera_link *tmp_soc_cam_link;*/
    struct v4l2_mbus_framefmt mf;
    int index = 0;
	unsigned long flags = 0;
    int ctrl;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);
	//return;
	
    if(pcdev->icd == NULL)
        return;
    sd = soc_camera_to_subdev(pcdev->icd);
    /*tmp_soc_cam_desc = to_soc_camera_link(pcdev->icd);*/ /*yzm*/
	/*dump regs*/
	{
		RKCAMERA_TR("CIF_CIF_CTRL = 0x%lx\n",read_cif_reg(pcdev->base,CIF_CIF_CTRL));
		RKCAMERA_TR("CIF_CIF_INTEN = 0x%lx\n",read_cif_reg(pcdev->base,CIF_CIF_INTEN));
		RKCAMERA_TR("CIF_CIF_INTSTAT = 0x%lx\n",read_cif_reg(pcdev->base,CIF_CIF_INTSTAT));
		RKCAMERA_TR("CIF_CIF_FOR = 0x%lx\n",read_cif_reg(pcdev->base,CIF_CIF_FOR));
		RKCAMERA_TR("CIF_CIF_CROP = 0x%lx\n",read_cif_reg(pcdev->base,CIF_CIF_CROP));
		RKCAMERA_TR("CIF_CIF_SET_SIZE = 0x%lx\n",read_cif_reg(pcdev->base,CIF_CIF_SET_SIZE));
		RKCAMERA_TR("CIF_CIF_SCL_CTRL = 0x%lx\n",read_cif_reg(pcdev->base,CIF_CIF_SCL_CTRL));
		RKCAMERA_TR("CRU_PCLK_REG30 = 0X%lx\n",read_cru_reg(CRU_PCLK_REG30));
		RKCAMERA_TR("CIF_CIF_LAST_LINE = 0X%lx\n",read_cif_reg(pcdev->base,CIF_CIF_LAST_LINE));
		
		RKCAMERA_TR("CIF_CIF_LAST_PIX = 0X%lx\n",read_cif_reg(pcdev->base,CIF_CIF_LAST_PIX));
		RKCAMERA_TR("CIF_CIF_VIR_LINE_WIDTH = 0X%lx\n",read_cif_reg(pcdev->base,CIF_CIF_VIR_LINE_WIDTH));
    	RKCAMERA_TR("CIF_CIF_LINE_NUM_ADDR = 0X%lx\n",read_cif_reg(pcdev->base,CIF_CIF_LINE_NUM_ADDR));
    	RKCAMERA_TR("CIF_CIF_FRM0_ADDR_Y = 0X%lx\n",read_cif_reg(pcdev->base,CIF_CIF_FRM0_ADDR_Y));
    	RKCAMERA_TR("CIF_CIF_FRM0_ADDR_UV = 0X%lx\n",read_cif_reg(pcdev->base,CIF_CIF_FRM0_ADDR_UV));
    	RKCAMERA_TR("CIF_CIF_FRAME_STATUS = 0X%lx\n",read_cif_reg(pcdev->base,CIF_CIF_FRAME_STATUS));
    	RKCAMERA_TR("CIF_CIF_SCL_VALID_NUM = 0X%lx\n",read_cif_reg(pcdev->base,CIF_CIF_SCL_VALID_NUM));
    	RKCAMERA_TR("CIF_CIF_CUR_DST = 0X%lx\n",read_cif_reg(pcdev->base,CIF_CIF_CUR_DST));
    	RKCAMERA_TR("CIF_CIF_LINE_NUM_ADDR = 0X%lx\n",read_cif_reg(pcdev->base,CIF_CIF_LINE_NUM_ADDR));
	}
	
    ctrl = read_cif_reg(pcdev->base,CIF_CIF_CTRL);          /*ddl@rock-chips.com v0.3.0x13*/
    if (pcdev->reinit_times == 1) {
        if (ctrl & ENABLE_CAPTURE) {        
            RKCAMERA_TR("Sensor data transfer may be error, so reset CIF and reinit sensor for resume!\n");
            pcdev->irqinfo.cifirq_idx = pcdev->irqinfo.dmairq_idx;
            rk_camera_cif_reset(pcdev,false);

            
            v4l2_subdev_call(sd,core, init, 0); 
            
            mf.width	= pcdev->icd_width;
            mf.height	= pcdev->icd_height;
            mf.field	= V4L2_FIELD_NONE;            
            mf.code		= pcdev->icd->current_fmt->code;
            mf.reserved[0] = 0x5a5afefe;              
            mf.reserved[1] = 0;

            v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
            
            write_cif_reg(pcdev->base,CIF_CIF_CTRL, (read_cif_reg(pcdev->base,CIF_CIF_CTRL)|ENABLE_CAPTURE));
        } else if (pcdev->irqinfo.cifirq_idx != pcdev->irqinfo.dmairq_idx) { 
            RKCAMERA_TR("CIF may be error, so reset cif for resume\n");
            pcdev->irqinfo.cifirq_idx = pcdev->irqinfo.dmairq_idx;
            rk_camera_cif_reset(pcdev,false);
            write_cif_reg(pcdev->base,CIF_CIF_CTRL, (read_cif_reg(pcdev->base,CIF_CIF_CTRL)|ENABLE_CAPTURE));
        }
        return;
    }
    
    atomic_set(&pcdev->stop_cif,true);
	write_cif_reg(pcdev->base,CIF_CIF_CTRL, (read_cif_reg(pcdev->base,CIF_CIF_CTRL)&(~ENABLE_CAPTURE)));
	RKCAMERA_DG1("the reinit times = %d\n",pcdev->reinit_times);
	
    if(pcdev->video_vq && pcdev->video_vq->irqlock){
        spin_lock_irqsave(pcdev->video_vq->irqlock, flags);
        for (index = 0; index < VIDEO_MAX_FRAME; index++) {
            if (NULL == pcdev->video_vq->bufs[index])
                continue;

            if (pcdev->video_vq->bufs[index]->state == VIDEOBUF_QUEUED) {
                list_del_init(&pcdev->video_vq->bufs[index]->queue);
                pcdev->video_vq->bufs[index]->state = VIDEOBUF_NEEDS_INIT;
                wake_up_all(&pcdev->video_vq->bufs[index]->done);
                printk("wake up video buffer index = %d  !!!\n",index);
            }
        }
        spin_unlock_irqrestore(pcdev->video_vq->irqlock, flags); 
    }else{
        RKCAMERA_TR("video queue has somthing wrong !!\n");
    }

	RKCAMERA_TR("the %d reinit times ,wake up video buffers!\n ",pcdev->reinit_times);
}
static enum hrtimer_restart rk_camera_fps_func(struct hrtimer *timer)
{
    struct rk_camera_frmivalenum *fival_nxt=NULL,*fival_pre=NULL, *fival_rec=NULL;
	struct rk_camera_timer *fps_timer = container_of(timer, struct rk_camera_timer, timer);
	struct rk_camera_dev *pcdev = fps_timer->pcdev;
    int rec_flag,i;
	/*static unsigned int last_fps = 0;*/
	/*struct soc_camera_link *tmp_soc_cam_link;*/ /*yzm*/
	/*tmp_soc_cam_link = to_soc_camera_link(pcdev->icd);*/ /*yzm*/

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	RKCAMERA_DG1("rk_camera_fps_func fps:0x%x\n",pcdev->fps);
	if ((pcdev->fps < 1) || (pcdev->last_fps == pcdev->fps)) {
		RKCAMERA_TR("Camera host haven't recevie data from sensor,last fps = %d,pcdev->fps = %d,cif_irq: %ld,dma_irq: %ld!\n",
		            pcdev->last_fps,pcdev->fps,pcdev->irqinfo.cifirq_idx, pcdev->irqinfo.dmairq_idx);
		pcdev->camera_reinit_work.pcdev = pcdev;
		/*INIT_WORK(&(pcdev->camera_reinit_work.work), rk_camera_reinit_work);*/
        pcdev->reinit_times++;
		queue_work(pcdev->camera_wq,&(pcdev->camera_reinit_work.work));
	} else if(!pcdev->timer_get_fps) {
	    pcdev->timer_get_fps = true;
	    for (i=0; i<2; i++) {
            if (pcdev->icd == pcdev->icd_frmival[i].icd) {
                fival_nxt = pcdev->icd_frmival[i].fival_list;                
            }
        }
        
        rec_flag = 0;
        fival_pre = fival_nxt;
        while (fival_nxt != NULL) {

            RKCAMERA_DG1("%s %c%c%c%c %dx%d framerate : %d/%d\n", dev_name(pcdev->icd->control), /*yzm*/
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
                    fival_nxt->fival.discrete.denominator = pcdev->frame_interval;
                    fival_nxt->fival.reserved[1] = (pcdev->icd_width<<16)
                                                    |(pcdev->icd_height);
                    fival_nxt->fival.discrete.numerator = 1000000;
                    fival_nxt->fival.type = V4L2_FRMIVAL_TYPE_DISCRETE;
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

                fival_pre->nxt->fival.discrete.denominator = pcdev->frame_interval;
                fival_pre->nxt->fival.reserved[1] = (pcdev->icd_width<<16)
                                                    |(pcdev->icd_height);
                fival_pre->nxt->fival.discrete.numerator = 1000000;
                fival_pre->nxt->fival.type = V4L2_FRMIVAL_TYPE_DISCRETE;
                rec_flag = 1;
                fival_rec = fival_pre->nxt;
            }
        }
	}

    if ((pcdev->last_fps != pcdev->fps) && (pcdev->reinit_times))             /*ddl@rock-chips.com v0.3.0x13*/
        pcdev->reinit_times = 0;
	
    pcdev->last_fps = pcdev->fps ;
    pcdev->fps_timer.timer.node.expires= ktime_add_us(pcdev->fps_timer.timer.node.expires, ktime_to_us(ktime_set(3, 0)));
    pcdev->fps_timer.timer._softexpires= ktime_add_us(pcdev->fps_timer.timer._softexpires, ktime_to_us(ktime_set(3, 0)));
    /*return HRTIMER_NORESTART;*/
    if(pcdev->reinit_times >=2)
        return HRTIMER_NORESTART;
    else
        return HRTIMER_RESTART;
}
static int rk_camera_s_stream(struct soc_camera_device *icd, int enable)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;
    int cif_ctrl_val;
	int ret;
	unsigned long flags;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);	

	WARN_ON(pcdev->icd != icd);

	cif_ctrl_val = read_cif_reg(pcdev->base,CIF_CIF_CTRL);
	if (enable) {
		pcdev->fps = 0;
        pcdev->last_fps = 0;
        pcdev->frame_interval = 0;
		hrtimer_cancel(&(pcdev->fps_timer.timer));
		pcdev->fps_timer.pcdev = pcdev;
        pcdev->timer_get_fps = false;
        pcdev->reinit_times  = 0;

        spin_lock_irqsave(&pcdev->lock,flags);
        atomic_set(&pcdev->stop_cif,false);
        pcdev->irqinfo.cifirq_idx = 0;
        pcdev->irqinfo.cifirq_normal_idx = 0;
        pcdev->irqinfo.cifirq_abnormal_idx = 0;
        pcdev->irqinfo.dmairq_idx = 0;
		
		write_cif_reg(pcdev->base,CIF_CIF_INTEN, 0x01|0x200);    /*capture complete interrupt enable*/
		cif_ctrl_val |= ENABLE_CAPTURE;
        write_cif_reg(pcdev->base,CIF_CIF_CTRL, cif_ctrl_val);
        spin_unlock_irqrestore(&pcdev->lock,flags);
        printk("%s:stream enable CIF_CIF_CTRL 0x%lx",__func__,read_cif_reg(pcdev->base,CIF_CIF_CTRL));
		hrtimer_start(&(pcdev->fps_timer.timer),ktime_set(3, 0),HRTIMER_MODE_REL);
        pcdev->fps_timer.istarted = true;
	} else {
	    /*cancel timer before stop cif*/
		ret = hrtimer_cancel(&pcdev->fps_timer.timer);
        pcdev->fps_timer.istarted = false;
        flush_work(&(pcdev->camera_reinit_work.work));
        
        cif_ctrl_val &= ~ENABLE_CAPTURE;
		spin_lock_irqsave(&pcdev->lock, flags);
    	write_cif_reg(pcdev->base,CIF_CIF_CTRL, cif_ctrl_val);
        atomic_set(&pcdev->stop_cif,true);
		write_cif_reg(pcdev->base,CIF_CIF_INTEN, 0x0); 
    	spin_unlock_irqrestore(&pcdev->lock, flags);
		flush_workqueue((pcdev->camera_wq));
		msleep(100);
	}
    /*must be reinit,or will be somthing wrong in irq process.*/
    if(enable == false) {
        pcdev->active = NULL;
        INIT_LIST_HEAD(&pcdev->capture);
    }
	RKCAMERA_DG1("s_stream: enable : 0x%x , CIF_CIF_CTRL = 0x%lx\n",enable,read_cif_reg(pcdev->base,CIF_CIF_CTRL));
	return 0;
}
int rk_camera_enum_frameintervals(struct soc_camera_device *icd, struct v4l2_frmivalenum *fival)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
    struct rk_camera_dev *pcdev = ici->priv;
    struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
    struct rk_camera_frmivalenum *fival_list = NULL;
    struct v4l2_frmivalenum *fival_head = NULL;
    struct rkcamera_platform_data *new_camera;
    int i,ret = 0,index;
    const struct soc_camera_format_xlate *xlate;
    struct v4l2_mbus_framefmt mf;
    __u32 pixfmt;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);	
    
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
    }  else {  

        if (fival_head) {
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

                pixfmt = fival->pixel_format;     /* ddl@rock-chips.com: v0.3.9 */
                xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
                memset(&mf,0x00,sizeof(struct v4l2_mbus_framefmt));
            	mf.width	= fival->width;
            	mf.height	= fival->height;            	
            	mf.code		= xlate->code;                

            	v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
                
                fival->reserved[1] = (mf.width<<16)|(mf.height);
                
                RKCAMERA_DG1("%s %dx%d@%c%c%c%c framerate : %d/%d\n", dev_name(pcdev->icd->pdev),
                    fival->width, fival->height,
                    fival->pixel_format & 0xFF, (fival->pixel_format >> 8) & 0xFF,
    			    (fival->pixel_format >> 16) & 0xFF, (fival->pixel_format >> 24),
    			     fival->discrete.denominator,fival->discrete.numerator);			    
            } else {
                if (index == 0)
                    RKCAMERA_TR("%s have not catch %d%d@%c%c%c%c index(%d) framerate\n",dev_name(pcdev->icd->pdev),
                        fival->width,fival->height, 
                        fival->pixel_format & 0xFF, (fival->pixel_format >> 8) & 0xFF,
        			    (fival->pixel_format >> 16) & 0xFF, (fival->pixel_format >> 24),
        			    index);
                else
                    RKCAMERA_DG1("%s have not catch %d%d@%c%c%c%c index(%d) framerate\n",dev_name(pcdev->icd->pdev),
                        fival->width,fival->height, 
                        fival->pixel_format & 0xFF, (fival->pixel_format >> 8) & 0xFF,
        			    (fival->pixel_format >> 16) & 0xFF, (fival->pixel_format >> 24),
        			    index);
                ret = -EINVAL;
                goto rk_camera_enum_frameintervals_end;
            }
        } else {
            i = 0x00;
            new_camera = pcdev->pdata->register_dev_new;
            while(new_camera != NULL){
                if (strcmp(new_camera->dev_name, dev_name(pcdev->icd->pdev)) == 0) {
                    i = 0x01;                
                    break;
                }
                new_camera = new_camera->next_camera;
            }

            if (i == 0x00) {
                printk(KERN_ERR "%s(%d): %s have not found in new_camera[] and rk_camera_platform_data!",
                    __FUNCTION__,__LINE__,dev_name(pcdev->icd->pdev));
            } else {

                pixfmt = fival->pixel_format;     /* ddl@rock-chips.com: v0.3.9 */
                xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
                memset(&mf,0x00,sizeof(struct v4l2_mbus_framefmt));
            	mf.width	= fival->width;
            	mf.height	= fival->height;            	
            	mf.code		= xlate->code;                

            	v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
                
                fival->discrete.numerator= 1000;
                fival->discrete.denominator = 15000;
                fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
                fival->reserved[1] = (mf.width<<16)|(mf.height);                
            }
        }
    }
rk_camera_enum_frameintervals_end:
    return ret;
}

static int rk_camera_set_digit_zoom(struct soc_camera_device *icd,
								const struct v4l2_queryctrl *qctrl, int zoom_rate)
{
	struct v4l2_crop a;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
	struct rk_camera_dev *pcdev = ici->priv;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


#if CIF_DO_CROP    
	unsigned long tmp_cifctrl; 
#endif	

	/*change the crop and scale parameters*/
	
#if CIF_DO_CROP
    a.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    /*a.c.width = pcdev->host_width*100/zoom_rate;*/
    a.c.width = pcdev->host_width*100/zoom_rate;
    a.c.width &= ~CROP_ALIGN_BYTES;    
    a.c.height = pcdev->host_height*100/zoom_rate;
    a.c.height &= ~CROP_ALIGN_BYTES;
    a.c.left = (((pcdev->host_width - a.c.width)>>1)+pcdev->host_left)&(~0x01);
    a.c.top = (((pcdev->host_height - a.c.height)>>1)+pcdev->host_top)&(~0x01);
    atomic_set(&pcdev->stop_cif,true);
    tmp_cifctrl = read_cif_reg(pcdev->base,CIF_CIF_CTRL);
    write_cif_reg(pcdev->base,CIF_CIF_CTRL, (tmp_cifctrl & ~ENABLE_CAPTURE));
    hrtimer_cancel(&(pcdev->fps_timer.timer));
    flush_workqueue((pcdev->camera_wq));
    
    down(&pcdev->zoominfo.sem);
    pcdev->zoominfo.a.c.left = 0;
    pcdev->zoominfo.a.c.top = 0;
    pcdev->zoominfo.a.c.width = a.c.width;
    pcdev->zoominfo.a.c.height = a.c.height;
    pcdev->zoominfo.vir_width = pcdev->zoominfo.a.c.width;
    pcdev->zoominfo.vir_height = pcdev->zoominfo.a.c.height;
    pcdev->frame_inval = 1;
    write_cif_reg(pcdev->base,CIF_CIF_CROP, (a.c.left + (a.c.top<<16)));
    write_cif_reg(pcdev->base,CIF_CIF_SET_SIZE, ((a.c.width ) + (a.c.height<<16)));
    write_cif_reg(pcdev->base,CIF_CIF_VIR_LINE_WIDTH, a.c.width);
    write_cif_reg(pcdev->base,CIF_CIF_FRAME_STATUS,  0x00000002);//frame1 has been ready to receive data,frame 2 is not used
    if(pcdev->active)
        rk_videobuf_capture(pcdev->active,pcdev);
    if(tmp_cifctrl & ENABLE_CAPTURE)
        write_cif_reg(pcdev->base,CIF_CIF_CTRL, (tmp_cifctrl | ENABLE_CAPTURE));
    up(&pcdev->zoominfo.sem);
    
    atomic_set(&pcdev->stop_cif,false);
    hrtimer_start(&(pcdev->fps_timer.timer),ktime_set(3, 0),HRTIMER_MODE_REL);
    RKCAMERA_DG1("zoom_rate:%d (%dx%d at (%d,%d)-> %dx%d)\n", zoom_rate,a.c.width, a.c.height, a.c.left, a.c.top, icd->user_width, icd->user_height );
#else
    a.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    a.c.width = pcdev->host_width*100/zoom_rate;
    a.c.width &= ~CROP_ALIGN_BYTES;    
    a.c.height = pcdev->host_height*100/zoom_rate;
    a.c.height &= ~CROP_ALIGN_BYTES;
    a.c.left = (pcdev->host_width - a.c.width)>>1;
    a.c.top = (pcdev->host_height - a.c.height)>>1;
    
    down(&pcdev->zoominfo.sem);
    pcdev->zoominfo.a.c.height = a.c.height;
    pcdev->zoominfo.a.c.width = a.c.width;
    pcdev->zoominfo.a.c.top = a.c.top;
    pcdev->zoominfo.a.c.left = a.c.left;
    pcdev->zoominfo.vir_width = pcdev->host_width;
    pcdev->zoominfo.vir_height= pcdev->host_height;
    up(&pcdev->zoominfo.sem);
    
    RKCAMERA_DG1("zoom_rate:%d (%dx%d at (%d,%d)-> %dx%d)\n", zoom_rate,a.c.width, a.c.height, a.c.left, a.c.top, icd->user_width, icd->user_height );
#endif	

	return 0;
}

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
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);/*yzm*/
	const struct v4l2_queryctrl *qctrl;
    struct rk_camera_dev *pcdev = ici->priv;

    int ret = 0;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

	qctrl = rk_camera_soc_camera_find_qctrl(ici->ops, sctrl->id);
	if (!qctrl) {
		ret = -ENOIOCTLCMD;
        goto rk_camera_set_ctrl_end;
	}

	switch (sctrl->id)
	{
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
    .cropcap    = rk_camera_cropcap,
    .set_crop	= rk_camera_set_crop,
    .get_crop   = rk_camera_get_crop,
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

/**********yzm***********/
static int rk_camera_cif_iomux(struct device *dev)
{

    struct pinctrl      *pinctrl;
    struct pinctrl_state    *state;
    int retval = 0;
    char state_str[20] = {0};

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);
    strcpy(state_str,"cif_pin_jpe");

	/*__raw_writel(((1<<1)|(1<<(1+16))),RK_GRF_VIRT+0x0380);*/


    /*mux CIF0_CLKOUT*/

    pinctrl = devm_pinctrl_get(dev);
    if (IS_ERR(pinctrl)) {
        printk(KERN_EMERG "%s:Get pinctrl failed!\n",__func__);
        return -1;
    }
    state = pinctrl_lookup_state(pinctrl,
                         state_str);
    if (IS_ERR(state)){
        dev_err(dev, "%s:could not get %s pinstate\n",__func__,state_str);
        return -1;
        }

    if (!IS_ERR(state)) {
        retval = pinctrl_select_state(pinctrl, state);
        if (retval){
            dev_err(dev,
                "%s:could not set %s pins\n",__func__,state_str);
                return -1;

                }
    }
    return 0;
            
}
/***********yzm***********/
static int rk_camera_probe(struct platform_device *pdev)
{
    struct rk_camera_dev *pcdev;
    struct resource *res;
    struct rk_camera_frmivalenum *fival_list,*fival_nxt;
    int irq,i;
    int err = 0;
    struct rk_cif_clk *clk=NULL;
	struct device *dev_cif = ((struct rk29camera_platform_data*)pdev->dev.platform_data)->cif_dev;/*yzm*/

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

	RKCAMERA_TR("%s version: v%d.%d.%d  Zoom by %s",RK29_CAM_DRV_NAME,(RK_CAM_VERSION_CODE&0xff0000)>>16,
        (RK_CAM_VERSION_CODE&0xff00)>>8,RK_CAM_VERSION_CODE&0xff,CAMERA_SCALE_CROP_MACHINE);    

    if ((pdev->id == RK_CAM_PLATFORM_DEV_ID_1) && (RK_SUPPORT_CIF1 == 0)) {
        RKCAMERA_TR("%s(%d): This chip is not support CIF1!!\n",__FUNCTION__,__LINE__);
        BUG();
    }

    if ((pdev->id == RK_CAM_PLATFORM_DEV_ID_0) && (RK_SUPPORT_CIF0 == 0)) {
        RKCAMERA_TR("%s(%d): This chip is not support CIF0!!\n",__FUNCTION__,__LINE__);
        BUG();
    }

/***********yzm**********/
	rk_camera_diffchips(((struct rk29camera_platform_data*)pdev->dev.platform_data)->rockchip_name);

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    irq = platform_get_irq(pdev, 0);

	/*	irq = irq_of_parse_and_map(dev_cif->of_node, 0);
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n res = [%x--%x] \n",res->start , res->end);
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n irq_num = %d\n", irq);
	*/
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

    pcdev->zoominfo.zoom_rate = 100;
    pcdev->hostid = pdev->id;        /* get host id*/
    #ifdef CONFIG_SOC_RK3028
    pcdev->chip_id = rk3028_version_val();
    #else
    pcdev->chip_id = -1;
    #endif

	/***********yzm***********/
	if (IS_CIF0()) {
		debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$/is_cif0\n");
        clk = &cif_clk[0];
        cif_clk[0].pd_cif = devm_clk_get(dev_cif, "pd_cif0");
        cif_clk[0].aclk_cif = devm_clk_get(dev_cif, "aclk_cif0");
        cif_clk[0].hclk_cif = devm_clk_get(dev_cif, "hclk_cif0");
        cif_clk[0].cif_clk_in = devm_clk_get(dev_cif, "cif0_in");
        cif_clk[0].cif_clk_out = devm_clk_get(dev_cif, "cif0_out");
        //spin_lock_init(&cif_clk[0].lock);
        cif_clk[0].on = false;
        rk_camera_cif_iomux(dev_cif);/*yzm*/
    } else {
    	clk = &cif_clk[1];
        cif_clk[1].pd_cif = devm_clk_get(dev_cif, "pd_cif0");/*cif0  only yzm*/
        cif_clk[1].aclk_cif = devm_clk_get(dev_cif, "aclk_cif0");
        cif_clk[1].hclk_cif = devm_clk_get(dev_cif, "hclk_cif0");
        cif_clk[1].cif_clk_in = devm_clk_get(dev_cif, "cif0_in");
        cif_clk[1].cif_clk_out = devm_clk_get(dev_cif, "cif0_out");
        //spin_lock_init(&cif_clk[1].lock);
        cif_clk[1].on = false;
        rk_camera_cif_iomux(dev_cif);/*yzm*/
    }

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

	/***********yzm**********/
    dev_set_drvdata(&pdev->dev, pcdev);
    pcdev->res = res;
    pcdev->pdata = pdev->dev.platform_data;             /* ddl@rock-chips.com : Request IO in init function */
					/*= rk_camera_platform_data */
	if (pcdev->pdata && pcdev->pdata->io_init) {
		
        pcdev->pdata->io_init();/* call rk_sensor_io_init()*/

        if (pcdev->pdata->sensor_mclk == NULL)
            pcdev->pdata->sensor_mclk = rk_camera_mclk_ctrl;
    }

    INIT_LIST_HEAD(&pcdev->capture);
    INIT_LIST_HEAD(&pcdev->camera_work_queue);
    spin_lock_init(&pcdev->lock);
    spin_lock_init(&pcdev->camera_work_lock);

    memset(&pcdev->cropinfo.c,0x00,sizeof(struct v4l2_rect));
    spin_lock_init(&pcdev->cropinfo.lock);
    sema_init(&pcdev->zoominfo.sem,1);

    /*
     * Request the regions.
     */
    if(res) {
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
	
    pcdev->irqinfo.irq = irq;
    pcdev->dev = &pdev->dev;

    /* config buffer address */
    /* request irq */
    if(irq > 0){
        err = request_irq(pcdev->irqinfo.irq, rk_camera_irq, 0, RK29_CAM_DRV_NAME,
                          pcdev);
        if (err) {
            dev_err(pcdev->dev, "Camera interrupt register failed \n");
            goto exit_reqirq;
        }
   	}
   
    if(IS_CIF0()) {
    	pcdev->camera_wq = create_workqueue("rk_cam_wkque_cif0");
    } else {
    	pcdev->camera_wq = create_workqueue("rk_cam_wkque_cif1");
    }
    if (pcdev->camera_wq == NULL) {
        RKCAMERA_TR("%s(%d): Create workqueue failed!\n",__FUNCTION__,__LINE__);
        goto exit_free_irq;
    }

	pcdev->camera_reinit_work.pcdev = pcdev;
	INIT_WORK(&(pcdev->camera_reinit_work.work), rk_camera_reinit_work);

    for (i=0; i<2; i++) {
        pcdev->icd_frmival[i].icd = NULL;
        pcdev->icd_frmival[i].fival_list = kzalloc(sizeof(struct rk_camera_frmivalenum),GFP_KERNEL);
        
    }
    pcdev->soc_host.drv_name	= RK29_CAM_DRV_NAME;
    pcdev->soc_host.ops		= &rk_soc_camera_host_ops;
    pcdev->soc_host.priv		= pcdev;        /*to itself,csll in rk_camera_add_device*/
    pcdev->soc_host.v4l2_dev.dev	= &pdev->dev;
    pcdev->soc_host.nr		= pdev->id;
	debug_printk("/$$$$$$$$$$$$$$$$$$$$$$/next soc_camera_host_register\n");
    err = soc_camera_host_register(&pcdev->soc_host);


    if (err) {
        RKCAMERA_TR("%s(%d): soc_camera_host_register failed\n",__FUNCTION__,__LINE__);
        goto exit_free_irq;
    }
	pcdev->fps_timer.pcdev = pcdev;
	hrtimer_init(&(pcdev->fps_timer.timer), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pcdev->fps_timer.timer.function = rk_camera_fps_func;
    pcdev->icd_cb.sensor_cb = NULL;

#if (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_IPP)
    pcdev->icd_cb.scale_crop_cb = rk_camera_scale_crop_ipp;
#elif (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_ARM)
    pcdev->icd_cb.scale_crop_cb = rk_camera_scale_crop_arm;
#elif (CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_RGA)
    pcdev->icd_cb.scale_crop_cb = rk_camera_scale_crop_rga;	
#elif(CONFIG_CAMERA_SCALE_CROP_MACHINE == RK_CAM_SCALE_CROP_PP)
	pcdev->icd_cb.scale_crop_cb = rk_camera_scale_crop_pp; 
#endif
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
    
    free_irq(pcdev->irqinfo.irq, pcdev);
	if (pcdev->camera_wq) {
		destroy_workqueue(pcdev->camera_wq);
		pcdev->camera_wq = NULL;
	}
exit_reqirq:
    iounmap(pcdev->base);
exit_ioremap_vip:
    release_mem_region(res->start, res->end - res->start + 1);
exit_reqmem_vip:
    if (clk) {
        if (clk->pd_cif)
            clk_put(clk->pd_cif);
        if (clk->aclk_cif)
            clk_put(clk->aclk_cif);
        if (clk->hclk_cif)
            clk_put(clk->hclk_cif);
        if (clk->cif_clk_in)
            clk_put(clk->cif_clk_in);
        if (clk->cif_clk_out)
            clk_put(clk->cif_clk_out);
    }
    kfree(pcdev);
exit_alloc:

exit:
    return err;
}

static int __exit rk_camera_remove(struct platform_device *pdev)
{
    struct rk_camera_dev *pcdev = platform_get_drvdata(pdev);
    struct resource *res;
    struct rk_camera_frmivalenum *fival_list,*fival_nxt;
    int i;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	
    free_irq(pcdev->irqinfo.irq, pcdev);

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
    iounmap((void __iomem*)pcdev->base);
    release_mem_region(res->start, res->end - res->start + 1);
    if (pcdev->pdata && pcdev->pdata->io_deinit) {         /* ddl@rock-chips.com : Free IO in deinit function */
        pcdev->pdata->io_deinit(0);
        pcdev->pdata->io_deinit(1);
    }

    kfree(pcdev);

    dev_info(&pdev->dev, "RK28 Camera driver unloaded\n");

    return 0;
}

static struct platform_driver rk_camera_driver =
{
    .driver 	= {
        .name	= RK29_CAM_DRV_NAME,       /*host */      
    },
    .probe		= rk_camera_probe,
    .remove		= (rk_camera_remove),
};

static int rk_camera_init_async(void *unused)
{

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);
    platform_driver_register(&rk_camera_driver);	
    return 0;
}

static int __init rk_camera_init(void)
{

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

    kthread_run(rk_camera_init_async, NULL, "rk_camera_init");
	
    return 0;
}

static void __exit rk_camera_exit(void)
{
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

    platform_driver_unregister(&rk_camera_driver);
}

device_initcall_sync(rk_camera_init);
module_exit(rk_camera_exit);

MODULE_DESCRIPTION("RKSoc Camera Host driver");
MODULE_AUTHOR("ddl <ddl@rock-chips>");
MODULE_LICENSE("GPL");
