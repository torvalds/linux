/* drivers/staging/rk29/ipp/rk29-ipp.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <asm/delay.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <mach/io.h>
#include <mach/irqs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/firmware.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <plat/ipp.h>
#include <linux/time.h>
#include <asm/cacheflush.h>
#include <linux/slab.h>

#ifdef CONFIG_ARCH_RK29
#define IPP_VERSION "rk29-ipp 1.003"
#endif

#ifdef CONFIG_ARCH_RK30
#define IPP_VERSION "rk30-ipp 1.003"
#endif

//#define IPP_TEST
#ifdef IPP_TEST

struct delayed_work d_work;
static DECLARE_WAIT_QUEUE_HEAD(test_queue);
int test_condition;
static struct timeval hw_set;
static struct timeval irq_ret;
static struct timeval irq_done;
static struct timeval wait_done;

ktime_t hw_start; 
ktime_t hw_end;
ktime_t irq_start; 
ktime_t irq_end;

#endif


struct ipp_drvdata {
  	struct miscdevice miscdev;
  	struct device dev;
	struct ipp_regs regs;
	void *ipp_base;
	int irq0;

	struct clk *pd_ipp;
	struct clk *aclk_lcdc;
	struct clk *hclk_lcdc;
	struct clk *aclk_ddr_lcdc;
	struct clk *hclk_cpu_display;
	struct clk *aclk_disp_matrix;
	struct clk *hclk_disp_matrix;
	struct clk *axi_clk;
	struct clk *ahb_clk;
	
	struct mutex	mutex;	// mutex
	struct mutex	power_mutex;	// mutex
	struct delayed_work power_off_work;
	wait_queue_head_t ipp_wait;                 
	atomic_t ipp_event;
	bool issync;         						//sync or async
	bool enable;        						//clk enable or disable
	int ipp_result;      						//0:success
	int ipp_async_result;						//ipp_blit_async result 0:success
	void (*ipp_irq_callback)(int ipp_retval);   //callback function used by aync call
};

static struct ipp_drvdata *drvdata = NULL;

static DECLARE_WAIT_QUEUE_HEAD(hw_wait_queue);
static volatile int wq_condition = 1;                //0:interact with hardware
static DECLARE_WAIT_QUEUE_HEAD(blit_wait_queue);     
static volatile int idle_condition = 1;              //1:idle, 0:busy


#define IPP_MAJOR		232


#define RK29_IPP_PHYS	0x10110000
#define RK29_IPP_SIZE	SZ_16K
#define IPP_RESET_TIMEOUT	1000

/* Driver information */
#define DRIVER_DESC		"RK29 IPP Device Driver"
#define DRIVER_NAME		"rk29-ipp"



/* Logging */
#define IPP_DEBUG 0
#if IPP_DEBUG
#define DBG(format, args...) printk(KERN_DEBUG "%s: " format, DRIVER_NAME, ## args)
#define ERR(format, args...) printk(KERN_DEBUG "%s: " format, DRIVER_NAME, ## args)
#define WARNING(format, args...) printk(KERN_DEBUG "%s: " format, DRIVER_NAME, ## args)
#define INFO(format, args...) printk(KERN_DEBUG "%s: " format, DRIVER_NAME, ## args)
#else
#define DBG(format, args...)
#define ERR(format, args...)
#define WARNING(format, args...)
#define INFO(format, args...)
#endif


static inline void ipp_write( uint32_t b, uint32_t r)
{
	__raw_writel(b, drvdata->ipp_base + r);
}

static inline uint32_t ipp_read( uint32_t r)
{
	return __raw_readl(drvdata->ipp_base + r);
}

static void ipp_soft_reset(void)
{
	uint32_t i;
	uint32_t reg;

	ipp_write(1, IPP_SRESET);

	for(i = 0; i < IPP_RESET_TIMEOUT; i++) {
		reg = ipp_read( IPP_SRESET) & 1;

		if(reg == 0)
			break;

		udelay(1);
	}

	if(i == IPP_RESET_TIMEOUT)
		ERR("soft reset timeout.\n");
}

static void ipp_power_on(void)
{
	//printk("ipp_power_on\n");
	cancel_delayed_work_sync(&drvdata->power_off_work);
	
	mutex_lock(&drvdata->power_mutex);
	
	if (drvdata->enable)
	{
		mutex_unlock(&drvdata->power_mutex);
		return;
	}
	
	clk_enable(drvdata->pd_ipp);
#ifdef CONFIG_ARCH_RK29
	clk_enable(drvdata->aclk_lcdc);
	clk_enable(drvdata->hclk_lcdc);
	clk_enable(drvdata->aclk_ddr_lcdc);
	clk_enable(drvdata->hclk_cpu_display);
	clk_enable(drvdata->aclk_disp_matrix);
	clk_enable(drvdata->hclk_disp_matrix);
#endif
	clk_enable(drvdata->axi_clk);
	clk_enable(drvdata->ahb_clk);

	drvdata->enable = true;
	
	mutex_unlock(&drvdata->power_mutex);
}


static void ipp_power_off(struct work_struct *work)
{
	//printk("ipp_power_off\n");
	
	mutex_lock(&drvdata->power_mutex);
	
	if(!drvdata->enable)
	{
		mutex_unlock(&drvdata->power_mutex);
		return;
	}
	
#ifdef CONFIG_ARCH_RK29
	clk_disable(drvdata->aclk_lcdc);
	clk_disable(drvdata->hclk_lcdc);
	clk_disable(drvdata->aclk_ddr_lcdc);
	clk_disable(drvdata->hclk_cpu_display);
	clk_disable(drvdata->aclk_disp_matrix);
	clk_disable(drvdata->hclk_disp_matrix);
#endif
	clk_disable(drvdata->axi_clk);
	clk_disable(drvdata->ahb_clk);
	clk_disable(drvdata->pd_ipp);
	
	drvdata->enable = false;

	mutex_unlock(&drvdata->power_mutex);
}

static unsigned int ipp_poll(struct file *filep, poll_table *wait)
{
	//printk("ipp_poll\n");
	
	poll_wait(filep, &drvdata->ipp_wait, wait);

	if (atomic_read(&drvdata->ipp_event))
	{
		atomic_set(&drvdata->ipp_event, 0);
		return POLLIN | POLLRDNORM;
	}
	return 0;
}

static void ipp_blit_complete(int retval)
{

	uint32_t event = IPP_BLIT_COMPLETE_EVENT;
	//printk("ipp_blit_complete\n");

	atomic_set(&drvdata->ipp_event, event);
	wake_up_interruptible(&drvdata->ipp_wait);
}

static int ipp_get_result(unsigned long arg)
{
	int ret =0;
	
	//printk("ipp_get_result %d\n",drvdata->ipp_result);
	if (unlikely(copy_to_user((void __user *)arg, &drvdata->ipp_async_result, sizeof(int)))) {
			printk("copy_to_user failed\n");
			ERR("copy_to_user failed\n");
			ret =  -EFAULT;	
		}
	
	return ret;
}

int ipp_check_param(const struct rk29_ipp_req *req)
{
	/*IPP can support up to 8191*8191 resolution in RGB format,but we limit the image size to 8190*8190 here*/
	//check src width and height
	if (unlikely((req->src0.w <16) || (req->src0.w > 8190) || (req->src0.h < 16) || (req->src0.h > 8190))) {
		printk("ipp invalid source resolution\n");
		return  -EINVAL;
	}

	//check dst width and height
	if (unlikely((req->dst0.w <16) || (req->dst0.w > 2047) || (req->dst0.h < 16) || (req->dst0.h > 2047))) {
		printk("ipp invalid destination resolution\n");
		return	-EINVAL;
	}

	if(req->src0.fmt == IPP_Y_CBCR_H2V2)
	{
		if (unlikely(((req->src0.w&0x1) != 0) || ((req->src0.h&0x1) != 0) || ((req->dst0.w&0x1) != 0)) || ((req->dst0.h&0x1) != 0)) {
				printk("YUV420 src or dst resolution is invalid! \n");
				return	-EINVAL;
			}
	}
	
	//check src_vir_w
	if(unlikely(req->src_vir_w < req->src0.w)){
		printk("ipp invalid src_vir_w\n");
		return	-EINVAL;
	}

	//check dst_vir_w
	if(unlikely(req->dst_vir_w < req->dst0.w)){
		printk("ipp invalid dst_vir_w\n");
		return	-EINVAL;
	}

	//check src address
	if (unlikely(req->src0.YrgbMst== 0) )
	{
		ERR("could not retrieve src image from memory\n");
		return	-EINVAL;
	}

	//check src address
	if (unlikely(req->dst0.YrgbMst== 0) )
	{
		ERR("could not retrieve dst image from memory\n");
		return	-EINVAL;
	}
	
	//check rotate degree
	if(req->flag >= IPP_ROT_LIMIT)
	{
		printk("rk29_ipp does not surpport rot degree!!!!\n");
		return	-EINVAL;
	}
	return 0;
}

int ipp_blit(const struct rk29_ipp_req *req)
{
	
	uint32_t rotate;
	uint32_t pre_scale	= 0;
	uint32_t post_scale = 0;
	uint32_t pre_scale_w, pre_scale_h;//pre_scale para
	uint32_t post_scale_w = 0x1000;
	uint32_t post_scale_h = 0x1000;
	uint32_t pre_scale_output_w=0, pre_scale_output_h=0;//pre_scale output with&height
	uint32_t post_scale_input_w, post_scale_input_h;//post_scale input width&height
	uint32_t dst0_YrgbMst=0,dst0_CbrMst=0;
	uint32_t ret = 0;
	uint32_t deinterlace_config = 0;
	uint32_t src0_w = req->src0.w;
	uint32_t src0_h = req->src0.h;
	
	//printk("ipp_blit\n");
	if (drvdata == NULL) {			/* ddl@rock-chips.com : check driver is normal or not */
		printk("%s drvdata is NULL, IPP driver probe is fail!!\n", __FUNCTION__);
		return -EPERM;
	}

	drvdata->ipp_result = -1;

	//When ipp_blit_async is called in kernel space req->complete should NOT be NULL, otherwise req->complete should be NULL
	if(req->complete)
	{
		drvdata->ipp_irq_callback = req->complete;
	}
	else
	{
		drvdata->ipp_irq_callback = ipp_blit_complete;
	}

	ret = ipp_check_param(req);
	if(ret ==  -EINVAL)
	{	
		printk("IPP invalid input!\n");
		goto erorr_input;
	}
	
	rotate = req->flag;
	switch (rotate) {
	case IPP_ROT_90:
		//for rotation 90 degree
		DBG("rotate %d, src0.fmt %d",rotate,req->src0.fmt);
		switch(req->src0.fmt)
		{
		case IPP_XRGB_8888:
			dst0_YrgbMst  =  req->dst0.YrgbMst + req->dst0.w*4;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_RGB_565:
			dst0_YrgbMst  =  req->dst0.YrgbMst +  req->dst0.w*2;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_Y_CBCR_H1V1:
			dst0_YrgbMst  =  req->dst0.YrgbMst + req->dst0.w;
			dst0_CbrMst   =  req->dst0.CbrMst + req->dst0.w*2;
			break;

		case IPP_Y_CBCR_H2V1:
			dst0_YrgbMst =	req->dst0.YrgbMst + req->dst0.w;
			dst0_CbrMst  =	req->dst0.CbrMst + req->dst0.w*2;
			break;

		case IPP_Y_CBCR_H2V2:
			dst0_YrgbMst = req->dst0.YrgbMst+ req->dst0.w;
			dst0_CbrMst  = req->dst0.CbrMst + req->dst0.w;
			break;

		default:

			break;
		}
	break;

	case IPP_ROT_180:
		//for rotation 180 degree
		DBG("rotate %d, src0.fmt %d",rotate,req->src0.fmt);
		switch(req->src0.fmt)
		{
		case IPP_XRGB_8888:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w*4+req->dst0.w*4;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_RGB_565:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w*2+req->dst0.w*2;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_Y_CBCR_H1V1:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w+req->dst0.w;
			dst0_CbrMst  =	req->dst0.CbrMst+(req->dst0.h-1)*req->dst_vir_w*2+req->dst0.w*2;
			break;

		case IPP_Y_CBCR_H2V1:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w+req->dst0.w;
			dst0_CbrMst  =	req->dst0.CbrMst+(req->dst0.h-1)*req->dst_vir_w+req->dst0.w;
			break;

		case IPP_Y_CBCR_H2V2:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w+req->dst0.w;
			dst0_CbrMst  =	req->dst0.CbrMst+((req->dst0.h/2)-1)*req->dst_vir_w+req->dst0.w;
			break;

		default:
			break;
		}
			break;

	case IPP_ROT_270:
		DBG("rotate %d, src0.fmt %d \n",rotate,req->src0.fmt);
		switch(req->src0.fmt)
		{
		case IPP_XRGB_8888:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w*4;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_RGB_565:
			dst0_YrgbMst =	req->dst0.YrgbMst +(req->dst0.h-1)*req->dst_vir_w*2;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_Y_CBCR_H1V1:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w;
			dst0_CbrMst  =	req->dst0.CbrMst+(req->dst0.h-1)*req->dst_vir_w*2;
			break;

		case IPP_Y_CBCR_H2V1:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w;
			dst0_CbrMst  =	req->dst0.CbrMst+(req->dst0.h-1)*req->dst_vir_w*2;
			break;

		case IPP_Y_CBCR_H2V2:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w;
			dst0_CbrMst  =	req->dst0.CbrMst+((req->dst0.h/2)-1)*req->dst_vir_w;
			break;

		default:
			break;
		}
		break;

	case IPP_ROT_X_FLIP:
		DBG("rotate %d, src0.fmt %d",rotate,req->src0.fmt);
		switch(req->src0.fmt)
		{
		case IPP_XRGB_8888:
			dst0_YrgbMst =	req->dst0.YrgbMst+req->dst0.w*4;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_RGB_565:
			dst0_YrgbMst =	req->dst0.YrgbMst+req->dst0.w*2;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_Y_CBCR_H1V1:
			dst0_YrgbMst =	req->dst0.YrgbMst+req->dst0.w;
			dst0_CbrMst  =	req->dst0.CbrMst+req->dst0.w*2;
			break;

		case IPP_Y_CBCR_H2V1:
			dst0_YrgbMst =	req->dst0.YrgbMst+req->dst0.w;
			dst0_CbrMst  =	req->dst0.CbrMst+req->dst0.w;
			break;

		case IPP_Y_CBCR_H2V2:
			dst0_YrgbMst =	req->dst0.YrgbMst+req->dst0.w;
			dst0_CbrMst  =	req->dst0.CbrMst+req->dst0.w;
			break;

		default:
			break;
		}

	break;

	case IPP_ROT_Y_FLIP:
		DBG("rotate %d, src0.fmt %d",rotate,req->src0.fmt);
		switch(req->src0.fmt)
		{
		case IPP_XRGB_8888:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w*4;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_RGB_565:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w*2;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_Y_CBCR_H1V1:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w;
			dst0_CbrMst = req->dst0.CbrMst+(req->dst0.h-1)*req->dst_vir_w*2;
			break;

		case IPP_Y_CBCR_H2V1:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w;
			dst0_CbrMst = req->dst0.CbrMst+(req->dst0.h-1)*req->dst_vir_w;
			break;

		case IPP_Y_CBCR_H2V2:
			dst0_YrgbMst =	req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w;
			dst0_CbrMst = req->dst0.CbrMst+((req->dst0.h/2)-1)*req->dst_vir_w;
			break;

	default:
		break;
	}

	break;
	case IPP_ROT_0:
		//for  0 degree
		DBG("rotate %d, src0.fmt %d",rotate,req->src0.fmt);
		dst0_YrgbMst =	req->dst0.YrgbMst;
		dst0_CbrMst  = req->dst0.CbrMst;
	break;

	default:
		ERR("ipp is not surpport degree!!\n" );
		break;
	}
	
	//we start to interact with hw now
	wq_condition = 0;

	ipp_power_on();
	
	
	//check if IPP is idle
	if(((ipp_read(IPP_INT)>>6)&0x3) !=0)// idle
	{
		printk("IPP staus is not idle,can not set register\n");
		goto error_status;
	}


	/* Configure source image */
	DBG("src YrgbMst 0x%x , CbrMst0x%x,  %dx%d, fmt = %d\n", req->src0.YrgbMst,req->src0.CbrMst,
					req->src0.w, req->src0.h, req->src0.fmt);

	ipp_write(req->src0.YrgbMst, IPP_SRC0_Y_MST);
	if(IS_YCRCB(req->src0.fmt))
	{
		ipp_write(req->src0.CbrMst, IPP_SRC0_CBR_MST);
	}
	//ipp_write(req->src0.h<<16|req->src0.w, IPP_SRC_IMG_INFO);
	ipp_write((ipp_read(IPP_CONFIG)&(~0x7))|req->src0.fmt, IPP_CONFIG);

	/* Configure destination image */
	DBG("dst YrgbMst 0x%x , CbrMst0x%x, %dx%d\n", dst0_YrgbMst,dst0_CbrMst,
					req->dst0.w, req->dst0.h);

	ipp_write(dst0_YrgbMst, IPP_DST0_Y_MST);

	if(IS_YCRCB(req->src0.fmt))
	{
		ipp_write(dst0_CbrMst, IPP_DST0_CBR_MST);
	}

	ipp_write(req->dst0.h<<16|req->dst0.w, IPP_DST_IMG_INFO);

	/*Configure Pre_scale*/
	if((IPP_ROT_90 == rotate) || (IPP_ROT_270 == rotate))
	{
		pre_scale = ((req->dst0.w <= req->src0.h/2) ? 1 : 0)||((req->dst0.h <= req->src0.w/2) ? 1 : 0);
	}
	else //other degree
	{
		pre_scale = ((req->dst0.w <= req->src0.w/2) ? 1 : 0)||((req->dst0.h <= req->src0.h/2) ? 1 : 0);
	}

	if(pre_scale)
	{
		if(((IPP_ROT_90 == rotate) || (IPP_ROT_270 == rotate)))
		{
			if((req->src0.w>req->dst0.h))
			{
				pre_scale_w = (uint32_t)( req->src0.w/req->dst0.h);//floor
			}
			else
			{
			   pre_scale_w = 1;
			}
			if((req->src0.h>req->dst0.w))
			{
				pre_scale_h = (uint32_t)( req->src0.h/req->dst0.w);//floor
			}
			else
			{
				pre_scale_h = 1;
			}

			DBG("!!!!!pre_scale_h %d,pre_scale_w %d \n",pre_scale_h,pre_scale_w);
		}
		else//0 180 x ,y
		{
			if((req->src0.w>req->dst0.w))
			{
				pre_scale_w = (uint32_t)( req->src0.w/req->dst0.w);//floor
			}
			else
			{
			   pre_scale_w = 1;
			}

			if((req->src0.h>req->dst0.h))
			{
				pre_scale_h = (uint32_t)( req->src0.h/req->dst0.h);//floor
			}
			else
			{
				pre_scale_h = 1;
			}
			DBG("!!!!!pre_scale_h %d,pre_scale_w %d \n",pre_scale_h,pre_scale_w);
		}

		//pre_scale only support 1/2 to 1/8 time
		if(pre_scale_w > 8)
		{
			if(pre_scale_w < 16)
			{
				pre_scale_w = 8;
			}
			else
			{
				printk("invalid pre_scale operation! Down scaling ratio should not be more than 16!\n");
				goto error_scale;
			}
		}
		if(pre_scale_h > 8)
		{
			if(pre_scale_h < 16)
			{
				pre_scale_h = 8;
			}
			else
			{
				printk("invalid pre_scale operation! Down scaling ratio should not be more than 16!\n");
				goto error_scale;
			}
		}
			
		if((req->src0.w%pre_scale_w)!=0) //ceil
		{
			pre_scale_output_w = req->src0.w/pre_scale_w+1;
		}
		else
		{
			pre_scale_output_w = req->src0.w/pre_scale_w;
		}

		if((req->src0.h%pre_scale_h)!=0)//ceil
		{
			pre_scale_output_h	= req->src0.h/pre_scale_h +1;
		}
		else
		{
			pre_scale_output_h = req->src0.h/pre_scale_h;
		}

		//when fmt is YUV,make sure pre_scale_output_w and pre_scale_output_h are even
		if(IS_YCRCB(req->src0.fmt))
		{
			if((pre_scale_output_w & 0x1) != 0)
			{
				pre_scale_output_w -=1;
				src0_w = pre_scale_output_w * pre_scale_w;
			}
			if((pre_scale_output_h & 0x1) != 0)
			{
				pre_scale_output_h -=1;
				src0_h = pre_scale_output_h * pre_scale_h;
			}
		}
			
		ipp_write((ipp_read(IPP_CONFIG)&0xffffffef)|PRE_SCALE, IPP_CONFIG); 		//enable pre_scale
		ipp_write((pre_scale_h-1)<<3|(pre_scale_w-1),IPP_PRE_SCL_PARA);
		ipp_write(((pre_scale_output_h)<<16)|(pre_scale_output_w), IPP_PRE_IMG_INFO);

	}
	else//no pre_scale
	{
		ipp_write(ipp_read(IPP_CONFIG)&(~PRE_SCALE), IPP_CONFIG); //disable pre_scale
		ipp_write(0,IPP_PRE_SCL_PARA);
		ipp_write((req->src0.h<<16)|req->src0.w, IPP_PRE_IMG_INFO);
	}

	ipp_write(src0_h<<16|src0_w, IPP_SRC_IMG_INFO);
	
	/*Configure Post_scale*/
	if((IPP_ROT_90 == rotate) || (IPP_ROT_270 == rotate))
	{
		if (( (req->src0.h%req->dst0.w)!=0)||( (req->src0.w%req->dst0.h)!= 0)//non-interger down-scaling 
			||((req->src0.h/req->dst0.w)>8)||((req->src0.h/req->dst0.w)>8)	 //down-scaling ratio > 8
			||(req->dst0.w > req->src0.h)  ||(req->dst0.h > req->src0.w))	 //up-scaling
							
		{
			post_scale = 1;
		}
		else
		{
			post_scale = 0;
		}
	}
	else  //0 180 x-flip y-flip
	{
		if (( (req->src0.w%req->dst0.w)!=0)||( (req->src0.h%req->dst0.h)!= 0)//non-interger down-scaling 
			||((req->src0.w/req->dst0.w)>8)||((req->src0.h/req->dst0.h)>8)	 //down-scaling ratio > 8
			||(req->dst0.w > req->src0.w)  ||(req->dst0.h > req->src0.h))	 //up-scaling
		{
			post_scale = 1;
		}
		else
		{
			post_scale = 0;
		}
	}

	if(post_scale)
	{
		if(pre_scale)
		{
			post_scale_input_w = pre_scale_output_w;
			post_scale_input_h = pre_scale_output_h;
		}
		else
		{
			post_scale_input_w = req->src0.w;
			post_scale_input_h = req->src0.h;
		}
			
		if((IPP_ROT_90 == rotate) || (IPP_ROT_270 == rotate))
		{
			DBG("post_scale_input_w %d ,post_scale_input_h %d !!!\n",post_scale_input_w,post_scale_input_h);

			switch(req->src0.fmt)
			{
			case IPP_XRGB_8888:
			case IPP_RGB_565:
			case IPP_Y_CBCR_H1V1:
				//In horiaontial scale case, the factor must be minus 1 if the result of the factor is integer
				 if(((4096*(post_scale_input_w-1))%(req->dst0.h-1))==0)
				 {
					post_scale_w = (uint32_t)(4096*(post_scale_input_w-1)/(req->dst0.h-1))-1;
				 }
				 else
				 {
					 post_scale_w = (uint32_t)(4096*(post_scale_input_w-1)/(req->dst0.h-1));
				 }
				 break;

			case IPP_Y_CBCR_H2V1:
			case IPP_Y_CBCR_H2V2:
				//In horiaontial scale case, the factor must be minus 1 if the result of the factor is integer
				 if(((4096*(post_scale_input_w/2-1))%(req->dst0.h/2-1))==0)
				 {
					post_scale_w = (uint32_t)(4096*(post_scale_input_w/2-1)/(req->dst0.h/2-1))-1;
				 }
				 else
				 {
					 post_scale_w = (uint32_t)(4096*(post_scale_input_w/2-1)/(req->dst0.h/2-1));
				 }
				 break;

			default:
				break;
			}
			post_scale_h = (uint32_t)(4096*(post_scale_input_h -1)/(req->dst0.w-1));

			DBG("1111 post_scale_w %x,post_scale_h %x!!! \n",post_scale_w,post_scale_h);
		}
		else// 0 180 x-flip y-flip
		{
			switch(req->src0.fmt)
			{
			case IPP_XRGB_8888:
			case IPP_RGB_565:
			case IPP_Y_CBCR_H1V1:
				//In horiaontial scale case, the factor must be minus 1 if the result of the factor is integer
				 if(((4096*(post_scale_input_w-1))%(req->dst0.w-1))==0)
				 {
					post_scale_w = (uint32_t)(4096*(post_scale_input_w-1)/(req->dst0.w-1))-1;
				 }
				 else
				 {
					 post_scale_w = (uint32_t)(4096*(post_scale_input_w-1)/(req->dst0.w-1));
				 }
				 break;

			case IPP_Y_CBCR_H2V1:
			case IPP_Y_CBCR_H2V2:
				////In horiaontial scale case, the factor must be minus 1 if the result of the factor is integer
				 if(((4096*(post_scale_input_w/2-1))%(req->dst0.w/2-1))==0)
				 {
					post_scale_w = (uint32_t)(4096*(post_scale_input_w/2-1)/(req->dst0.w/2-1))-1;
				 }
				 else
				 {
					 post_scale_w = (uint32_t)(4096*(post_scale_input_w/2-1)/(req->dst0.w/2-1));
				 }
				 break;

			default:
				break;
			}
			post_scale_h = (uint32_t)(4096*(post_scale_input_h -1)/(req->dst0.h-1));

		}

		/*only support 1/2 to 4 times scaling,but two cases can pass
		1.176*144->480*800, YUV420 
		2.128*128->480*800, YUV420
		*/
		if(!((req->src0.fmt == IPP_Y_CBCR_H2V2)&&
			(((req->src0.w == 176)&&(req->src0.h == 144))||((req->src0.w == 128)&&(req->src0.h == 128)))&&
			((req->dst0.w == 480)&&(req->dst0.h == 800))))	
		{	
			
			if(post_scale_w<0x3ff || post_scale_w>0x1fff || post_scale_h<0x400 || post_scale_h>0x2000 )
			{
				printk("invalid post_scale para!\n");
				goto error_scale;
			}
		}
		ipp_write((ipp_read(IPP_CONFIG)&0xfffffff7)|POST_SCALE, IPP_CONFIG); //enable post_scale
		ipp_write((post_scale_h<<16)|post_scale_w, IPP_POST_SCL_PARA);
	}
	else //no post_scale
	{
		DBG("no post_scale !!!!!! \n");
		ipp_write(ipp_read(IPP_CONFIG)&(~POST_SCALE), IPP_CONFIG); //disable post_scale
		ipp_write((post_scale_h<<16)|post_scale_w, IPP_POST_SCL_PARA);
	}

	/* Configure rotation */

	if(IPP_ROT_0 == req->flag)
	{
		ipp_write(ipp_read(IPP_CONFIG)&(~ROT_ENABLE), IPP_CONFIG);
	}
	else
	{
		ipp_write(ipp_read(IPP_CONFIG)|ROT_ENABLE, IPP_CONFIG);
		ipp_write((ipp_read(IPP_CONFIG)&0xffffff1f)|(rotate<<5), IPP_CONFIG);
	}

	/*Configure deinterlace*/
	if(req->deinterlace_enable == 1)
	{
		//only support YUV format
		if(IS_YCRCB(req->src0.fmt))
		{
			//If pre_scale is enable, Deinterlace is done by scale filter
			if(!pre_scale)
			{
				//check the deinterlace parameters
				if((req->deinterlace_para0 < 32) && (req->deinterlace_para1 < 32) && (req->deinterlace_para2 < 32) 
					&& ((req->deinterlace_para0 + req->deinterlace_para1 + req->deinterlace_para2) == 32))
				{
					deinterlace_config = (req->deinterlace_enable<<24) | (req->deinterlace_para0<<19) | (req->deinterlace_para1<<14) | (req->deinterlace_para2<<9);
					DBG("para0 %d, para1 %d, para2 %d,deinterlace_config  %x\n",req->deinterlace_para0,req->deinterlace_para1,req->deinterlace_para2,deinterlace_config);
					#ifdef CONFIG_DEINTERLACE
						ipp_write((ipp_read(IPP_CONFIG)&0xFE0001FF)|deinterlace_config, IPP_CONFIG);
					#else
						printk("does not support deinterlacing!\n");
						ipp_write(ipp_read(IPP_CONFIG)&(~DEINTERLACE_ENABLE), IPP_CONFIG); //disable deinterlace
					#endif
				}
				else
				{
					ERR("invalid deinterlace parameters!\n");
				}
			}
		}
		else
		{
			ERR("only support YUV format!\n");
		}
	}
	else
	{
		ipp_write(ipp_read(IPP_CONFIG)&(~DEINTERLACE_ENABLE), IPP_CONFIG); //disable deinterlace
	}

	/*Configure other*/
	ipp_write((req->dst_vir_w<<16)|req->src_vir_w, IPP_IMG_VIR);

	//store clip mode 
	if(req->store_clip_mode == 1)
	{
		ipp_write(ipp_read(IPP_CONFIG)|STORE_CLIP_MODE, IPP_CONFIG);
	}
	else
	{
		ipp_write(ipp_read(IPP_CONFIG)&(~STORE_CLIP_MODE), IPP_CONFIG);
	}


	/* Start the operation */
	ipp_write(8, IPP_INT);		

	ipp_write(1, IPP_PROCESS_ST);

	dsb();
	dmac_clean_range(drvdata->ipp_base,drvdata->ipp_base+0x54);
#ifdef IPP_TEST
	hw_start = ktime_get(); 
#endif	

    goto error_noerror;


error_status:
error_scale:
	ret = -EINVAL;
	ipp_soft_reset();
	ipp_power_off(NULL);
erorr_input:
error_noerror:
	drvdata->ipp_result = ret;
	//printk("ipp_blit done\n");
	return ret;
}

int ipp_blit_async(const struct rk29_ipp_req *req)
{
	int ret = -1;
	//printk("ipp_blit_async *******************\n");
	
	//If IPP is busy now,wait until it becomes idle
	mutex_lock(&drvdata->mutex);
	{
		ret = wait_event_interruptible(blit_wait_queue, idle_condition);
		
		if(ret < 0)
		{
			printk("ipp_blit_async wait_event_interruptible=%d\n",ret);
			mutex_unlock(&drvdata->mutex);
			return ret;
		}
		
		idle_condition = 0;
	}
	mutex_unlock(&drvdata->mutex);

	drvdata->issync = false;	
	ret = ipp_blit(req);
	
	//printk("ipp_blit_async done******************\n");
	return ret;
}

static int ipp_blit_sync_real(const struct rk29_ipp_req *req)
{
	int status;
	int wait_ret;
	
	//printk("ipp_blit_sync -------------------\n");

	//If IPP is busy now,wait until it becomes idle
	mutex_lock(&drvdata->mutex);
	{
		status = wait_event_interruptible(blit_wait_queue, idle_condition);
		
		if(status < 0)
		{
			printk("ipp_blit_sync_real wait_event_interruptible=%d\n",status);
			mutex_unlock(&drvdata->mutex);
			return status;
		}
		
		idle_condition = 0;
		
	}
	mutex_unlock(&drvdata->mutex);

	
  	drvdata->issync = true;
	drvdata->ipp_result = ipp_blit(req);
   
	if(drvdata->ipp_result == 0)
	{
		//wait_ret = wait_event_interruptible_timeout(hw_wait_queue, wq_condition, msecs_to_jiffies(req->timeout));
		wait_ret = wait_event_timeout(hw_wait_queue, wq_condition, msecs_to_jiffies(req->timeout));
#ifdef IPP_TEST
		irq_end = ktime_get(); 
		irq_end = ktime_sub(irq_end,irq_start);
		hw_end = ktime_sub(hw_end,hw_start);
		if((((int)ktime_to_us(hw_end)/1000)>10)||(((int)ktime_to_us(irq_end)/1000)>10))
		{
			//printk("hw time: %d ms, irq time: %d ms\n",(int)ktime_to_us(hw_end)/1000,(int)ktime_to_us(irq_end)/1000);
		}
#endif				
		if (wait_ret <= 0)
		{
			printk("%s wait_ret=%d,wq_condition =%d,wait_event_timeout:%dms! \n",__FUNCTION__,wait_ret,wq_condition,req->timeout);

			if(wq_condition==0)
			{
				//print all register's value
				printk("IPP_CONFIG: %x\n",ipp_read(IPP_CONFIG));
				printk("IPP_SRC_IMG_INFO: %x\n",ipp_read(IPP_SRC_IMG_INFO));
				printk("IPP_DST_IMG_INFO: %x\n",ipp_read(IPP_DST_IMG_INFO));
				printk("IPP_IMG_VIR: %x\n",ipp_read(IPP_IMG_VIR));
				printk("IPP_INT: %x\n",ipp_read(IPP_INT));
				printk("IPP_SRC0_Y_MST: %x\n",ipp_read(IPP_SRC0_Y_MST));
				printk("IPP_SRC0_CBR_MST: %x\n",ipp_read(IPP_SRC0_CBR_MST));
				printk("IPP_SRC1_Y_MST: %x\n",ipp_read(IPP_SRC1_Y_MST));
				printk("IPP_SRC1_CBR_MST: %x\n",ipp_read(IPP_SRC1_CBR_MST));
				printk("IPP_DST0_Y_MST: %x\n",ipp_read(IPP_DST0_Y_MST));
				printk("IPP_DST0_CBR_MST: %x\n",ipp_read(IPP_DST0_CBR_MST));
				printk("IPP_DST1_Y_MST: %x\n",ipp_read(IPP_DST1_Y_MST));
				printk("IPP_DST1_CBR_MST: %x\n",ipp_read(IPP_DST1_CBR_MST));
				printk("IPP_PRE_SCL_PARA: %x\n",ipp_read(IPP_PRE_SCL_PARA));
				printk("IPP_POST_SCL_PARA: %x\n",ipp_read(IPP_POST_SCL_PARA));
				printk("IPP_SWAP_CTRL: %x\n",ipp_read(IPP_SWAP_CTRL));
				printk("IPP_PRE_IMG_INFO: %x\n",ipp_read(IPP_PRE_IMG_INFO));
				printk("IPP_AXI_ID: %x\n",ipp_read(IPP_AXI_ID));
				printk("IPP_SRESET: %x\n",ipp_read(IPP_SRESET));
				printk("IPP_PROCESS_ST: %x\n",ipp_read(IPP_PROCESS_ST));
		
				ipp_soft_reset();
				drvdata->ipp_result = -EAGAIN;
			}
		}

		ipp_power_off(NULL);
	}
	drvdata->issync = false;

	//IPP is idle, wake up the wait queue
	//printk("ipp_blit_sync done ----------------\n");
	status = drvdata->ipp_result;
	idle_condition = 1;
	wake_up_interruptible_sync(&blit_wait_queue);
	
	return status;
}

static int stretch_blit(unsigned long arg ,unsigned int cmd)
{
	struct rk29_ipp_req req;
	int ret = 0;
	
	if (unlikely(copy_from_user(&req, (struct rk29_ipp_req*)arg,
					sizeof(struct rk29_ipp_req)))) {
		ERR("copy_from_user failed\n");
		ret = -EFAULT;
		goto err_noput;
	}
	
	if(req.deinterlace_enable==2)
	{
		#ifdef	CONFIG_DEINTERLACE
			printk("ipp support deinterlacing\n");
    		return 0;
		#else
			printk("ipp dose not support deinterlacing\n");
			return -EPERM;
		#endif
	}

	if(cmd == IPP_BLIT_SYNC)
	{
		ret = ipp_blit_sync(&req);
	}
	else
	{
		ret = ipp_blit_async(&req);
	}
	
	if(ret != 0) {
		ERR("Failed to start IPP operation (%d)\n", ret);
		goto err_noput;
	}

err_noput:

	return ret;
}

static long ipp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	
	switch (cmd)
	{
		case IPP_BLIT_SYNC:
		case IPP_BLIT_ASYNC:
			ret = stretch_blit(arg, cmd);
			break;
		case IPP_GET_RESULT:
			ret = ipp_get_result(arg);
			break;
		default:
			ERR("unknown ioctl cmd!\n");
			ret = -EINVAL;
			break;
	}
	return ret;
}

static int rk29_ipp_open(struct inode *inode, struct file *file)
{
#if 0
   struct ipp_context *ctx;

    ctx = kmalloc(sizeof(struct ipp_context), GFP_KERNEL);
	if (ctx == NULL) {
		ERR("Context allocation failed\n");
		return -ENOMEM;
	}

	memset(ctx, 0, sizeof(struct ipp_context));
	ctx->data	= drvdata;
	ctx->rot	= IPP_ROT_0;
	file->private_data = ctx;

	DBG("device opened\n");
#endif
	return 0;
}

static int ipp_release(struct inode *inode, struct file *file)
{
	//printk("ipp_release\n");

	//clear event if app forget to get result
	if(atomic_read(&drvdata->ipp_event))
	{
		atomic_set(&drvdata->ipp_event, 0);
	}
	
//chenli: all these work has been done in the rk29_ipp_irq, they should not be done again!
#if 0
	
	//If app has not got the result after sending the request,the situation of idle_condition equals 1 will block the next request.
	//so we must set idle_condition 1 here.
	idle_condition = 1;
	wq_condition = 1;
	
	//delay 20ms to wait HW completed
	schedule_delayed_work(&drvdata->power_off_work, msecs_to_jiffies(20));
#endif

	return 0;
}

static irqreturn_t rk29_ipp_irq(int irq,  void *dev_id)
{
	DBG("rk29_ipp_irq %d \n",irq);
	//printk("rk29_ipp_irq %d \n",irq);

#ifdef IPP_TEST
	hw_end = ktime_get();
#endif

	ipp_write(ipp_read(IPP_INT)|0x3c, IPP_INT);
	if(((ipp_read(IPP_INT)>>6)&0x3) !=0)// idle
	{	
		printk("IPP is not idle!\n");
		ipp_soft_reset();
		drvdata->ipp_result =  -EAGAIN;
	}
	
	//interacting with hardware done
	wq_condition = 1;
	
#ifdef IPP_TEST
	irq_start = ktime_get();
#endif

	if(drvdata->issync)//sync
	{
		//wake_up_interruptible_sync(&hw_wait_queue);
		wake_up(&hw_wait_queue);
	}
	else//async
	{
		//power off
		schedule_delayed_work(&drvdata->power_off_work, msecs_to_jiffies(50));
		drvdata->ipp_irq_callback(drvdata->ipp_result);
		
		//In the case of async call ,we wake up the wait queue here
		drvdata->ipp_async_result = drvdata->ipp_result;
		idle_condition = 1;
		wake_up_interruptible_sync(&blit_wait_queue);
	}
	
	
	return IRQ_HANDLED;
}

static int ipp_suspend(struct platform_device *pdev, pm_message_t state)
{
	//printk("ipp_suspend\n");
	
	if(drvdata->enable)
	{
		//delay 20ms to wait hardware work completed
		mdelay(20);

		// power off right now
	    ipp_power_off(NULL);
	}

	return 0;
}

static int ipp_resume(struct platform_device *pdev)
{
	//printk("ipp_resume\n");
	//If IPP is working before suspending, we power it on now
	if (!wq_condition) 
	{
		drvdata->enable = false;
		ipp_power_on();
	}
	return 0;
}

#ifdef IPP_TEST
void ipp_test_complete(int retval)
{
	printk("ipp_test_complete retval=%d\n",retval);
	if(retval==0)
	{
		test_condition = 1;
		wake_up_interruptible_sync(&test_queue);
	}
}

static void ipp_test_work_handler(struct work_struct *work)
{

		 struct rk29_ipp_req ipp_req;
         int i=0,j;
		 int ret = 0,ret1=0,ret2=0;

		 uint8_t *srcY ;
		 uint8_t *dstY;
		 uint8_t *srcUV ;
		 uint8_t *dstUV;
		  uint32_t src_addr;
		 uint32_t dst_addr;
		 uint8_t *p;
		 pm_message_t pm;
#if 1
//test WIMO bug
#define CAMERA_MEM 0x90400000

		src_addr = CAMERA_MEM;
		dst_addr = CAMERA_MEM + 1280*800*4;
		printk("src_addr: %x-%x, dst_addr: %x-%x\n",srcY,src_addr,dstY,dst_addr);

		ipp_req.src0.YrgbMst = src_addr;
		//ipp_req.src0.CbrMst = src_addr + 1280*800;
		ipp_req.src0.w = 1280;
		ipp_req.src0.h = 800;
		ipp_req.src0.fmt = IPP_XRGB_8888;
		
		ipp_req.dst0.YrgbMst = dst_addr;
		//ipp_req.dst0.CbrMst = dst_addr + 1024*768;
		ipp_req.dst0.w = 1280;
		ipp_req.dst0.h = 800;
	
		ipp_req.src_vir_w = 1280;
		ipp_req.dst_vir_w = 1280;
		ipp_req.timeout = 100;
		ipp_req.flag = IPP_ROT_90;

		while(ret==0)
		{
			ret = ipp_blit_sync_real(&ipp_req);
			msleep(10);
		}
#endif
/*
//test zyc's bug		 

		uint32_t size = 800*480;
		srcY = (uint32_t)__get_free_pages(GFP_KERNEL, 9); //320*240*2*2bytes=300k < 128 pages
		srcUV = srcY + size;
		dstY = srcUV + size;
		dstUV = dstY + size;
		src_addr = virt_to_phys(srcY);
		dst_addr = virt_to_phys(dstY);
		printk("src_addr: %x-%x, dst_addr: %x-%x\n",srcY,srcUV,dstY,dstUV);

		
		ipp_req.src0.YrgbMst = src_addr;
		ipp_req.src0.CbrMst = src_addr + size;
		ipp_req.src0.w = 128;
		ipp_req.src0.h = 128;
		ipp_req.src0.fmt = IPP_Y_CBCR_H2V2;
		
		ipp_req.dst0.YrgbMst = dst_addr;
		ipp_req.dst0.CbrMst = dst_addr + size;
		ipp_req.dst0.w = 480;
		ipp_req.dst0.h = 800;
	
		ipp_req.src_vir_w = 128;
		ipp_req.dst_vir_w = 480;
		ipp_req.timeout = 100;
		ipp_req.flag = IPP_ROT_0;
		
		ipp_req.deinterlace_enable =0;
		ipp_req.deinterlace_para0 = 16;
		ipp_req.deinterlace_para1 = 16;
		ipp_req.deinterlace_para2 = 0;

		ipp_req.complete = ipp_test_complete;
*/
		/*0 test whether IPP_CONFIG is set correctly*/
		/*
		ipp_blit_sync(&ipp_req);
		ipp_req.dst0.w = 480;
		ipp_req.dst0.h = 320;
		ipp_req.dst_vir_w = 480;
		ipp_blit_sync(&ipp_req);
		*/
		
		/*1 test ipp_blit_sync*/
		/*
		while(!ret)
		{
			ipp_req.timeout = 50;
			//ret = ipp_do_blit(&ipp_req);
			ret = ipp_blit_sync(&ipp_req);
		}*/
		

		/*2.test ipp_blit_async*/
		/*
		do
		{
			test_condition = 0;
			ret = ipp_blit_async(&ipp_req);
			if(ret == 0)
				ret = wait_event_interruptible_timeout(test_queue, test_condition, msecs_to_jiffies(ipp_req.timeout));
			
			irq_end = ktime_get(); 
			irq_end = ktime_sub(irq_end,irq_start);
			hw_end = ktime_sub(hw_end,hw_start);
			if((((int)ktime_to_us(hw_end)/1000)>10)||(((int)ktime_to_us(irq_end)/1000)>10))
			{
				printk("hw time: %d ms, irq time: %d ms\n",(int)ktime_to_us(hw_end)/1000,(int)ktime_to_us(irq_end)/1000);
			}
		}while(ret>0);
		printk("test ipp_blit_async over!!!\n");
		*/

		/*3.two async req,get the rigt result respectively*/
		
		/*
	[   10.253532] ipp_blit_async
	[   10.256210] ipp_blit_async2
	[   10.259000] ipp_blit_async
	[   10.261700] ipp_test_complete retval=0
	[   10.282832] ipp_blit_async2
	[   10.284304] ipp_test_complete retval=1        
		*/
		/*
		ret1 = ipp_blit_async(&ipp_req);
		ret2 = ipp_blit_async(&ipp_req);
		*/

		/*4 two sync req,get the rigt result respectively*/
		/*
		[   10.703628] ipp_blit_async
		[   10.711211] wait_event_interruptible waitret= 0
		[   10.712878] ipp_blit_async2
		[   10.720036] ipp_blit_sync done
		[   10.720229] ipp_blit_async
		[   10.722920] wait_event_interruptible waitret= 0
		[   10.727422] ipp_blit_async2
		[   10.790052] hw time: 1 ms, irq time: 48 ms
		[   10.791294] ipp_blit_sync wait_ret=0,wait_event_timeout 
		*/
		/*
		ret1 = ipp_blit_sync(&ipp_req);
		ret2 = ipp_blit_sync(&ipp_req);*/

		/*5*/
		/*
		ret1 = ipp_blit_async(&ipp_req);
		ret2 = ipp_blit_sync(&ipp_req);	
		ret1 = ipp_blit_sync(&ipp_req);
		ret2 = ipp_blit_async(&ipp_req);
		*/

		/*6.call IPP driver in the kernel space and the user space at the same time*/
		/*
			ipp_req.src_vir_w = 280;
		ipp_req.dst_vir_w = 800;
		do
		{
			 ret = ipp_blit_sync(&ipp_req);
			 msleep(40);
		}
		while(ret==0);

		printk("error! ret =%d\n",ret);

		ipp_req.src_vir_w = 480;
		ipp_req.dst_vir_w = 600;

		ipp_blit_sync(&ipp_req);
		*/
		
		/*7.suspand and resume*/
		/*
		//do
		{
			 ret = ipp_blit_async(&ipp_req);
			 ipp_suspend(NULL,pm);
			 msleep(1000);
			 ipp_resume(NULL);
		}
		//while(ret==0);*/
	/*	
		do
		{
			 ret = ipp_blit_async(&ipp_req);
			 if(ret == 0)
				ret = wait_event_interruptible_timeout(test_queue, test_condition, msecs_to_jiffies(ipp_req.timeout));
			 msleep(100);

		}
		while(ret>0);
*/

		/*8 test special up scaling*/
		/*
		ipp_req.src0.fmt = IPP_Y_CBCR_H2V2;
		ipp_req.src0.w = 128;
		ipp_req.src0.h = 128;
		ipp_req.dst0.w = 480;
		ipp_req.dst0.h = 800;
		ipp_req.src_vir_w = 128;
		ipp_req.dst_vir_w = 480;
		ret = -1;
		ret = ipp_blit_sync(&ipp_req);
		printk("128x128->480x800: %d \n",ret);

		ipp_req.src0.w = 160;
		ipp_req.src0.h = 160;
		ipp_req.src_vir_w = 160;
		ret = -1;
		ret = ipp_blit_sync(&ipp_req);
		printk("160x160->480x800: %d \n",ret);

		ipp_req.src0.w = 176;
		ipp_req.src0.h = 144;
		ipp_req.src_vir_w = 176;
		ret = -1;
		ret = ipp_blit_sync(&ipp_req);
		printk("176x144->480x800: %d \n",ret);

		ipp_req.src0.fmt = IPP_Y_CBCR_H2V1;
		ret = -1;
		ret = ipp_blit_sync(&ipp_req);
		printk("fmt:422 176x144->480x800 : %d \n",ret);

		ipp_req.src0.fmt = IPP_Y_CBCR_H2V2;
		ipp_req.dst0.w = 800;
		ipp_req.dst0.h = 480;
		ipp_req.dst_vir_w = 800;
		ret = -1;
		ret = ipp_blit_sync(&ipp_req);
		printk("176x144->800x480: %d \n",ret);
		*/

		/*9 test rotate config*/
/*
		ipp_req.flag = IPP_ROT_180;
		ipp_blit_sync(&ipp_req);
		ipp_req.flag = IPP_ROT_270;
		ipp_blit_sync(&ipp_req);
		
		free_pages(srcY, 9);
*/
//test deinterlace
#if 0
		uint32_t size = 16*16;
		srcY = (uint32_t*)kmalloc(size*2*2,GFP_KERNEL);
		memset(srcY,0,size*2*2);
		srcUV = srcY + size;
		dstY = srcY + size*2;
		dstUV = dstY + size;
		src_addr = virt_to_phys(srcY);
		dst_addr = virt_to_phys(dstY);
		printk("src_addr: %x-%x, dst_addr: %x-%x\n",srcY,srcUV,dstY,dstUV);

		//set srcY
		p=srcY;
		for(i=0;i<256; i++)
		{
			*p++ = i;
		}
		
		//set srcUV
		p=srcUV;
		for(i=0;i<128; i++)
		{
			*p++ = 2*i;
		}
		
		dmac_clean_range(srcY,srcY+size*2);
		
		ipp_req.src0.YrgbMst = src_addr;
		ipp_req.src0.CbrMst = src_addr + size;
		ipp_req.src0.w = 16;
		ipp_req.src0.h = 16;
		ipp_req.src0.fmt = IPP_Y_CBCR_H2V2;
		
		ipp_req.dst0.YrgbMst = dst_addr;
		ipp_req.dst0.CbrMst = dst_addr + size;
		ipp_req.dst0.w = 16;
		ipp_req.dst0.h = 16;
	
		ipp_req.src_vir_w = 16;
		ipp_req.dst_vir_w = 16;
		ipp_req.timeout = 1000;
		ipp_req.flag = IPP_ROT_0;

		ipp_req.deinterlace_enable =1;
		ipp_req.deinterlace_para0 = 16;
		ipp_req.deinterlace_para1 = 16;
		ipp_req.deinterlace_para2 = 0;
		
		ipp_do_blit(&ipp_req);

		
/*
		for(i=0;i<8;i++)
		{
			for(j=0;j<32;j++)
			{
				printk("%4x ",*(srcY+j+i*32));
			}
			printk("\n");
		}

		
		printk("\n\n");
		dmac_inv_range(dstY,dstY+size*2);
		for(i=0;i<8;i++)
		{
			for(j=0;j<32;j++)
			{
				printk("%4x ",*(dstY+j+i*32));
			}
			printk("\n");
		}

		printk("\n\n");
		for(i=0;i<8;i++)
		{
			for(j=0;j<16;j++)
			{
				printk("%4x ",*(srcUV+j+i*16));
			}
			printk("\n");
		}
		printk("\n\n");
		for(i=0;i<8;i++)
		{
			for(j=0;j<16;j++)
			{
				printk("%4x ",*(dstUV+j+i*16));
			}
			printk("\n");
		}
*/		
		kfree(srcY);
#endif

}
#endif

struct file_operations ipp_fops = {
	.owner		= THIS_MODULE,
	.open		= rk29_ipp_open,
	.release	= ipp_release,
	.unlocked_ioctl		= ipp_ioctl,
	.poll		= ipp_poll,
};

static struct miscdevice ipp_dev ={
    .minor = IPP_MAJOR,
    .name  = "rk29-ipp",
    .fops  = &ipp_fops,
};

static int __devinit ipp_drv_probe(struct platform_device *pdev)
{
	struct ipp_drvdata *data;
	int ret = 0;

	data = kmalloc(sizeof(struct ipp_drvdata), GFP_KERNEL);
	if(NULL==data)
	{
		ERR("failed to allocate driver data.\n");
		return  -ENOMEM;
	}

	/* get the clock */
#ifdef CONFIG_ARCH_RK29
	data->pd_ipp = clk_get(&pdev->dev, "pd_display");
	if (IS_ERR(data->pd_ipp))
	{
		ERR("failed to find ipp pd_display source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->aclk_lcdc = clk_get(&pdev->dev, "aclk_lcdc");
	if (IS_ERR(data->aclk_lcdc))
	{
		ERR("failed to find ipp aclk_lcdc source\n");
		ret = -ENOENT;
		goto err_clock;
	}
	
	data->hclk_lcdc = clk_get(&pdev->dev, "hclk_lcdc");
	if (IS_ERR(data->hclk_lcdc))
	{
		ERR("failed to find ipp hclk_lcdc source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->aclk_ddr_lcdc = clk_get(&pdev->dev, "aclk_ddr_lcdc");
	if (IS_ERR(data->aclk_ddr_lcdc))
	{
		ERR("failed to find ipp aclk_ddr_lcdc source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->hclk_cpu_display = clk_get(&pdev->dev, "hclk_cpu_display");
	if (IS_ERR(data->hclk_cpu_display))
	{
		ERR("failed to find ipp hclk_cpu_display source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->aclk_disp_matrix = clk_get(&pdev->dev, "aclk_disp_matrix");
	if (IS_ERR(data->aclk_disp_matrix))
	{
		ERR("failed to find ipp aclk_disp_matrix source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->hclk_disp_matrix = clk_get(&pdev->dev, "hclk_disp_matrix");
	if (IS_ERR(data->hclk_disp_matrix))
	{
		ERR("failed to find ipp hclk_disp_matrix source\n");
		ret = -ENOENT;
		goto err_clock;
	}
#endif

#ifdef CONFIG_ARCH_RK30
	data->pd_ipp = clk_get(&pdev->dev, "pd_ipp");
	if (IS_ERR(data->pd_ipp))
	{
		ERR("failed to find ipp pd_ipp source\n");
		ret = -ENOENT;
		goto err_clock;
	}
#endif

	data->axi_clk = clk_get(&pdev->dev, "aclk_ipp");
	if (IS_ERR(data->axi_clk))
	{
		ERR("failed to find ipp axi clock source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->ahb_clk = clk_get(&pdev->dev, "hclk_ipp");
	if (IS_ERR(data->ahb_clk))
	{
		ERR("failed to find ipp ahb clock source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	/* map the memory */
	data->ipp_base = (void*)ioremap_nocache( RK29_IPP_PHYS, SZ_16K);//ipp size 16k
	if (data->ipp_base == NULL)
	{
		ERR("ioremap failed\n");
		ret = -ENOENT;
		goto err_ioremap;
	}

	/* get the IRQ */
	data->irq0 = pdev->resource[1].start;
	printk("ipp irq %d\n",data->irq0);
	if (data->irq0 <= 0)
	{
		ERR("failed to get irq resource (%d).\n", data->irq0);
		ret = data->irq0;
		goto err_irq;
	}

	/* request the IRQ */
	//ret = request_irq(data->irq0, rk29_ipp_irq, IRQF_SHARED, "rk29-ipp", pdev);
	ret = request_irq(data->irq0, rk29_ipp_irq, 0/*IRQF_DISABLED*/, "rk29-ipp", pdev);
	if (ret)
	{
		ERR("request_irq failed (%d).\n", ret);
		goto err_irq;
	}

	mutex_init(&data->mutex);
	mutex_init(&data->power_mutex);
	data->enable = false;


	atomic_set(&data->ipp_event, 0);
	init_waitqueue_head(&data->ipp_wait);
	INIT_DELAYED_WORK(&data->power_off_work, ipp_power_off);
	data->ipp_result = -1;
	data->ipp_irq_callback = NULL;



	
	platform_set_drvdata(pdev, data);
	drvdata = data;

	ret = misc_register(&ipp_dev);
	if(ret)
	{
		ERR("cannot register miscdev (%d)\n", ret);
		goto err_misc_register;
	}
	DBG("Driver loaded succesfully\n");

#ifdef IPP_TEST
	INIT_DELAYED_WORK(&d_work, ipp_test_work_handler);
	schedule_delayed_work(&d_work, msecs_to_jiffies(65000));
#endif

	return 0;

err_misc_register:
	free_irq(data->irq0, pdev);
err_irq:
	iounmap(data->ipp_base);
err_ioremap:
err_clock:
	kfree(data);

	return ret;
}

static int __devexit ipp_drv_remove(struct platform_device *pdev)
{
	struct ipp_drvdata *data = platform_get_drvdata(pdev);
    DBG("%s [%d]\n",__FUNCTION__,__LINE__);

    misc_deregister(&(data->miscdev));
	free_irq(data->irq0, &data->miscdev);
    iounmap((void __iomem *)(data->ipp_base));

	if(data->axi_clk) {
		clk_put(data->axi_clk);
	}
	if(data->ahb_clk) {
		clk_put(data->ahb_clk);
	}
#ifdef CONFIG_ARCH_RK29
	if(data->aclk_disp_matrix) {
		clk_put(data->aclk_disp_matrix);
	}

	if(data->hclk_disp_matrix) {
		clk_put(data->hclk_disp_matrix);
	}
	
	if(data->aclk_ddr_lcdc) {
		clk_put(data->aclk_ddr_lcdc);
	}
	
	if(data->hclk_lcdc) {
		clk_put(data->hclk_lcdc);
	}

	if(data->aclk_lcdc) {
		clk_put(data->aclk_lcdc);
	}
	
	if(data->hclk_cpu_display) {
		clk_put(data->hclk_cpu_display);
	}
#endif
	if(data->pd_ipp){
		clk_put(data->pd_ipp);
	}

kfree(data);
    return 0;
}



static struct platform_driver rk29_ipp_driver = {
	.probe		= ipp_drv_probe,
	.remove		= __devexit_p(ipp_drv_remove),
	.suspend    = ipp_suspend,
	.resume     = ipp_resume,
	.driver		= {
		.owner  = THIS_MODULE,
		.name	= "rk29-ipp",
	},
};

static int __init rk29_ipp_init(void)
{
	int ret;

	//set ipp_blit_sync pointer
	ipp_blit_sync = ipp_blit_sync_real;
	
	if ((ret = platform_driver_register(&rk29_ipp_driver)) != 0)
	{
		ERR("Platform device register failed (%d).\n", ret);
		return ret;
	}
	
	INFO("Module initialized.\n");
	printk("IPP init, version %s\n",IPP_VERSION);
	return 0;
}      

static void __exit rk29_ipp_exit(void)
{
	platform_driver_unregister(&rk29_ipp_driver);
}



module_init(rk29_ipp_init);
module_exit(rk29_ipp_exit);

/* Module information */
MODULE_AUTHOR("wy@rock-chips.com");
MODULE_DESCRIPTION("Driver for rk29 ipp device");
MODULE_LICENSE("GPL");


