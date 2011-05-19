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
#include <mach/rk29_iomap.h>
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
#include <mach/rk29-ipp.h>
#include <linux/time.h>

//#define IPP_TEST
#ifdef IPP_TEST
#include <asm/cacheflush.h>
struct delayed_work d_work;

static struct timeval hw_set;
static struct timeval irq_ret;
static struct timeval irq_done;
static struct timeval wait_done;
#endif

struct ipp_drvdata {
  	struct miscdevice miscdev;
  	struct device dev;
	void *ipp_base;
	struct ipp_regs regs;
	int irq0;

	struct clk *pd_display;
	struct clk *aclk_lcdc;
	struct clk *hclk_lcdc;
	struct clk *aclk_ddr_lcdc;
	struct clk *hclk_cpu_display;
	struct clk *aclk_disp_matrix;
	struct clk *hclk_disp_matrix;
	struct clk *axi_clk;
	struct clk *ahb_clk;
	
	struct mutex	mutex;	// mutex
};

static struct ipp_drvdata *drvdata = NULL;

static DECLARE_WAIT_QUEUE_HEAD(wait_queue);
static volatile int wq_condition = 0;

/* Context data (unique) */
struct ipp_context
{
	struct ipp_drvdata	*data;	// driver data
	struct rk29_ipp_req	*blit;	// blit request
	uint32_t			rot;	// rotation mode
	struct file			*srcf;	// source file (for PMEM)
	struct file			*dstf;	// destination file (for PMEM)
};

#define IPP_MAJOR		232
#define RK_IPP_BLIT 	0x5017
#define RK_IPP_SYNC   	0x5018

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

int ipp_do_blit(struct rk29_ipp_req *req)
{
	uint32_t rotate;
	uint32_t pre_scale 	= 0;
	uint32_t post_scale = 0;
	uint32_t pre_scale_w, pre_scale_h;//pre_scale para
	uint32_t post_scale_w = 0x1000;
	uint32_t post_scale_h = 0x1000;
	uint32_t pre_scale_output_w=0, pre_scale_output_h=0;//pre_scale的输出宽高
	uint32_t post_scale_input_w, post_scale_input_h;//post_scale的输入宽高
	uint32_t dst0_YrgbMst=0,dst0_CbrMst=0;
	uint32_t ret = 0;
	uint32_t deinterlace_config = 0;
	int wait_ret;

	mutex_lock(&drvdata->mutex);
	
	if (drvdata == NULL) {			/* ddl@rock-chips.com : check driver is normal or not */
		//printk(KERN_ERR, "%s drvdata is NULL, IPP driver probe is fail!!\n", __FUNCTION__);
		printk("%s drvdata is NULL, IPP driver probe is fail!!\n", __FUNCTION__);
		return -EPERM;
	}


	/*IPP can support up to 8191*8191 resolution in RGB format,but we limit the image size to 8190*8190 here*/
	//check src width and height
	if (unlikely((req->src0.w <16) || (req->src0.w > 8190) || (req->src0.h < 16) || (req->src0.h > 8190))) {
		ERR("invalid source resolution\n");
		ret = -EINVAL;
		goto erorr_input;
	}

	//check dst width and height
	if (unlikely((req->dst0.w <16) || (req->dst0.w > 2047) || (req->dst0.h < 16) || (req->dst0.h > 2047))) {
		ERR("invalid destination resolution\n");
		ret = -EINVAL;
		goto erorr_input;
	}

	//check src address
	if (unlikely(req->src0.YrgbMst== 0) )
	{
		ERR("could not retrieve src image from memory\n");
		ret = -EINVAL;
		goto erorr_input;
	}

	//check src address
	if (unlikely(req->dst0.YrgbMst== 0) )
	{
		ERR("could not retrieve dst image from memory\n");
		ret = -EINVAL;
		goto erorr_input;
	}
	//check rotate degree
	if(req->flag >= IPP_ROT_LIMIT)
	{
	    ERR("rk29_ipp is not surpport rot degree!!!!\n");
		ret =  -EINVAL;
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
			dst0_YrgbMst =  req->dst0.YrgbMst + req->dst0.w;
			dst0_CbrMst  =  req->dst0.CbrMst + req->dst0.w*2;
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
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w*4+req->dst0.w*4;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_RGB_565:
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w*2+req->dst0.w*2;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_Y_CBCR_H1V1:
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w+req->dst0.w;
			dst0_CbrMst  =  req->dst0.CbrMst+(req->dst0.h-1)*req->dst_vir_w*2+req->dst0.w*2;
			break;

		case IPP_Y_CBCR_H2V1:
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w+req->dst0.w;
			dst0_CbrMst  =  req->dst0.CbrMst+(req->dst0.h-1)*req->dst_vir_w+req->dst0.w;
			break;

		case IPP_Y_CBCR_H2V2:
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w+req->dst0.w;
			dst0_CbrMst  =  req->dst0.CbrMst+((req->dst0.h/2)-1)*req->dst_vir_w+req->dst0.w;
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
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w*4;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_RGB_565:
			dst0_YrgbMst =  req->dst0.YrgbMst +(req->dst0.h-1)*req->dst_vir_w*2;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_Y_CBCR_H1V1:
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w;
			dst0_CbrMst  =  req->dst0.CbrMst+(req->dst0.h-1)*req->dst_vir_w*2;
			break;

		case IPP_Y_CBCR_H2V1:
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w;
			dst0_CbrMst  =  req->dst0.CbrMst+(req->dst0.h-1)*req->dst_vir_w*2;
			break;

		case IPP_Y_CBCR_H2V2:
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w;
			dst0_CbrMst  =  req->dst0.CbrMst+((req->dst0.h/2)-1)*req->dst_vir_w;
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
			dst0_YrgbMst =  req->dst0.YrgbMst+req->dst0.w*4;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_RGB_565:
			dst0_YrgbMst =  req->dst0.YrgbMst+req->dst0.w*2;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_Y_CBCR_H1V1:
			dst0_YrgbMst =  req->dst0.YrgbMst+req->dst0.w;
			dst0_CbrMst  =  req->dst0.CbrMst+req->dst0.w*2;
			break;

		case IPP_Y_CBCR_H2V1:
			dst0_YrgbMst =  req->dst0.YrgbMst+req->dst0.w;
			dst0_CbrMst  =  req->dst0.CbrMst+req->dst0.w;
			break;

		case IPP_Y_CBCR_H2V2:
			dst0_YrgbMst =  req->dst0.YrgbMst+req->dst0.w;
			dst0_CbrMst  =  req->dst0.CbrMst+req->dst0.w;
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
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w*4;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_RGB_565:
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w*2;
			dst0_CbrMst  = req->dst0.CbrMst;
			break;

		case IPP_Y_CBCR_H1V1:
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w;
			dst0_CbrMst = req->dst0.CbrMst+(req->dst0.h-1)*req->dst_vir_w*2;
			break;

		case IPP_Y_CBCR_H2V1:
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w;
			dst0_CbrMst = req->dst0.CbrMst+(req->dst0.h-1)*req->dst_vir_w;
			break;

		case IPP_Y_CBCR_H2V2:
			dst0_YrgbMst =  req->dst0.YrgbMst+(req->dst0.h-1)*req->dst_vir_w;
			dst0_CbrMst = req->dst0.CbrMst+((req->dst0.h/2)-1)*req->dst_vir_w;
			break;

	default:
		break;
	}

	break;
	case IPP_ROT_0:
		//for  0 degree
		DBG("rotate %d, src0.fmt %d",rotate,req->src0.fmt);
		dst0_YrgbMst =  req->dst0.YrgbMst;
		dst0_CbrMst  = req->dst0.CbrMst;
	break;

	default:
		ERR("ipp is not surpport degree!!\n" );
		break;
	}

	//enable clk
	if(drvdata->pd_display)
	{
		clk_enable(drvdata->pd_display);
	}
	else
	{
		printk("pd_display is null \n");
		ret =  -EINVAL;
		goto error_pd_display;
	}
	
	if(drvdata->aclk_lcdc)
	{
		clk_enable(drvdata->aclk_lcdc);
	}
	else
	{
		printk("aclk_lcdc is null \n");
		ret =  -EINVAL;
		goto error_aclk_lcdc;
	}

	if(drvdata->hclk_lcdc)
	{
		clk_enable(drvdata->hclk_lcdc);
	}
	else
	{
		printk("hclk_lcdc is null \n");
		ret =  -EINVAL;
		goto error_hclk_lcdc;
	}

	if(drvdata->aclk_ddr_lcdc)
	{
		clk_enable(drvdata->aclk_ddr_lcdc);
	}
	else
	{
		printk("aclk_ddr_lcdc is null \n");
		ret =  -EINVAL;
		goto error_aclk_ddr_lcdc;
	}
	
	if(drvdata->hclk_cpu_display)
	{
		clk_enable(drvdata->hclk_cpu_display);
	}
	else
	{
		printk("hclk_cpu_display is null \n");
		ret =  -EINVAL;
		goto error_hclk_cpu_display;
	}
	
	if(drvdata->aclk_disp_matrix)
	{
		clk_enable(drvdata->aclk_disp_matrix);
	}
	else
	{
		printk("aclk_disp_matrix is null \n");
		ret =  -EINVAL;
		goto error_aclk_disp_matrix;
	}

	if(drvdata->hclk_disp_matrix)
	{
		clk_enable(drvdata->hclk_disp_matrix);
	}
	else
	{
		printk("hclk_disp_matrix is null \n");
		ret =  -EINVAL;
		goto error_hclk_disp_matrix;
	}
	
	if(drvdata->axi_clk)
	{
		clk_enable(drvdata->axi_clk);
	}
	else
	{
		printk("axi_clk is null \n");
		ret =  -EINVAL;
		goto error_axi_clk;
	}

	if(drvdata->ahb_clk)
	{
		clk_enable(drvdata->ahb_clk);
	}
	else
	{
		printk("ahb_clk is null \n");
		ret = -EINVAL;
		goto error_ahb_clk;
	}
	//enable clk end

	//check if IPP is idle
	if(((ipp_read(IPP_INT)>>6)&0x3) !=0)// idle
	{
		printk("IPP staus is not idle,can noe set register\n");
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
	ipp_write(req->src0.h<<16|req->src0.w, IPP_SRC_IMG_INFO);
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
		pre_scale = ((req->dst0.w < req->src0.h) ? 1 : 0)||((req->dst0.h < req->src0.w) ? 1 : 0);
	}
	else //other degree
	{
	 	pre_scale = ((req->dst0.w < req->src0.w) ? 1 : 0)||((req->dst0.h < req->src0.h) ? 1 : 0);
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
				printk("invalid pre_scale operation! pre_scale_w should not be more than 8!\n");
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
				printk("invalid pre_scale operation! pre_scale_h should not be more than 8!\n");
				goto error_scale;
			}
		}
			
		if((req->src0.w%pre_scale_w)!=0) //向上取整 ceil
		{
			pre_scale_output_w = req->src0.w/pre_scale_w+1;
		}
		else
		{
			pre_scale_output_w = req->src0.w/pre_scale_w;
		}

		if((req->src0.h%pre_scale_h)!=0)//向上取整 ceil
		{
			pre_scale_output_h  = req->src0.h/pre_scale_h +1;
		}
		else
		{
			pre_scale_output_h = req->src0.h/pre_scale_h;
		}
			
		ipp_write((ipp_read(IPP_CONFIG)&0xffffffef)|PRE_SCALE, IPP_CONFIG); 		//enable pre_scale
		ipp_write((pre_scale_h-1)<<3|(pre_scale_w-1),IPP_PRE_SCL_PARA);
		ipp_write(((pre_scale_output_h)<<16)|(pre_scale_output_w), IPP_PRE_IMG_INFO);

	}
	else//no pre_scale
	{
		ipp_write(0,IPP_PRE_SCL_PARA);
		ipp_write((req->src0.h<<16)|req->src0.w, IPP_PRE_IMG_INFO);
	}

	/*Configure Post_scale*/
	if((IPP_ROT_90 == rotate) || (IPP_ROT_270 == rotate))
	{
		if (( (req->src0.h%req->dst0.w)!=0)||( (req->src0.w%req->dst0.h)!= 0)//小数倍缩小
			||((req->src0.h/req->dst0.w)>8)||((req->src0.h%req->dst0.w)>8)	 //缩小系数大于8
			||(req->dst0.w > req->src0.h)  ||(req->dst0.h > req->src0.w))    //放大
							
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
		if (( (req->src0.w%req->dst0.w)!=0)||( (req->src0.h%req->dst0.h)!= 0)//小数倍缩小
		    ||((req->src0.w/req->dst0.w)>8)||((req->src0.h%req->dst0.h)>8)	 //缩小系数大于8
			||(req->dst0.w > req->src0.w)  ||(req->dst0.h > req->src0.h))    //放大
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

		if(!((req->src0.fmt != IPP_Y_CBCR_H2V1)&&(req->src0.w == 176)&&(req->src0.h == 144)&&(req->dst0.w == 480)&&(req->src0.h == 800)))
		{	
			//only support 1/2 to 4 times scaling,but 176*144->480*800 can pass
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
	 	ipp_write(ipp_read(IPP_CONFIG)|rotate<<5, IPP_CONFIG);
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
					ipp_write((ipp_read(IPP_CONFIG)&0xFE0001FF)|deinterlace_config, IPP_CONFIG);

					printk("IPP_CONFIG2 = 0x%x\n",ipp_read(IPP_CONFIG));
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

	/*Configure other*/
	ipp_write((req->dst_vir_w<<16)|req->src_vir_w, IPP_IMG_VIR);

	if((req->src0.w%4) !=0)
	ipp_write(ipp_read(IPP_CONFIG)|(1<<26), IPP_CONFIG);//store clip mode

	/* Start the operation */

	ipp_write(8, IPP_INT);//

	//msleep(5000);//debug use

	wq_condition = 0;
	dsb();
#ifdef IPP_TEST
	memset(&hw_set,0,sizeof(struct timeval));
	memset(&irq_ret,0,sizeof(struct timeval));
	memset(&irq_done,0,sizeof(struct timeval));
	memset(&wait_done,0,sizeof(struct timeval));
	do_gettimeofday(&hw_set);
#endif

	ipp_write(1, IPP_PROCESS_ST);
	//Important!Without msleep,ipp driver may occupy too much CPU,this can lead the ipp interrupt to timeout
	msleep(1);

	wait_ret = wait_event_interruptible_timeout(wait_queue, wq_condition, msecs_to_jiffies(req->timeout));

#ifdef IPP_TEST
	do_gettimeofday(&wait_done);
	if ((((irq_ret.tv_sec - hw_set.tv_sec) * 1000 + (irq_ret.tv_usec - hw_set.tv_usec) / 1000) > 10) ||
		(((wait_done.tv_sec - irq_done.tv_sec) * 1000 + (wait_done.tv_usec - irq_done.tv_usec) / 1000) > 10)) {
		printk("hw time: %8d irq time %8d\n",
			((irq_ret.tv_sec - hw_set.tv_sec) * 1000 + (irq_ret.tv_usec - hw_set.tv_usec) / 1000),
			((wait_done.tv_sec - irq_done.tv_sec) * 1000 + (wait_done.tv_usec - irq_done.tv_usec) / 1000));
	}
#endif
	

	if (wait_ret <= 0)
	{
		printk("%s wait_event_timeout \n",__FUNCTION__);

#ifdef IPP_TEST
		//print all register's value
		printk("wait_ret: %d\n", wait_ret);
		printk("wq_condition: %d\n", wq_condition);
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

		while(1)
		{

		}
#endif
		
		wq_condition = 0;
		if(!drvdata)
		{
	        printk("close clk 0! \n");
	        ret = -EINVAL;
			goto error_null;
	    }
		ret =  -EAGAIN;
		goto error_timeout;
	}


	if(!drvdata)
	{
        printk("close clk! \n");
        ret =  -EINVAL;
		goto error_null;
    }

	if(((ipp_read(IPP_INT)>>6)&0x3) ==0)// idle
	{
		goto error_noerror;
	}
	else
	{
		printk("rk29 ipp status is error!!!\n");
		ret =  -EINVAL;
		goto error_timeout;
	}
	
error_status:
error_scale:
error_null:
error_timeout:
	//soft rest
	ipp_soft_reset();
error_noerror:
	clk_disable(drvdata->ahb_clk);
error_ahb_clk:
	clk_disable(drvdata->axi_clk);
error_axi_clk:	
	clk_disable(drvdata->hclk_disp_matrix);
error_hclk_disp_matrix:
	clk_disable(drvdata->aclk_disp_matrix);
error_aclk_disp_matrix:
	clk_disable(drvdata->hclk_cpu_display);
error_hclk_cpu_display:
    clk_disable(drvdata->aclk_ddr_lcdc);
error_aclk_ddr_lcdc:
	clk_disable(drvdata->hclk_lcdc);
error_hclk_lcdc:
	clk_disable(drvdata->aclk_lcdc);
error_aclk_lcdc:	
	clk_disable(drvdata->pd_display);
error_pd_display:
erorr_input:
	mutex_unlock(&drvdata->mutex);
	return ret;
}

static int stretch_blit(struct ipp_context *ctx,  unsigned long arg )
{
	struct ipp_drvdata *data = ctx->data;
	int ret = 0;
	struct rk29_ipp_req req;

	mutex_lock(&data->mutex);

	if (unlikely(copy_from_user(&req, (struct rk29_ipp_req*)arg,
					sizeof(struct rk29_ipp_req)))) {
		ERR("copy_from_user failed\n");
		ret = -EFAULT;
		goto err_noput;
	}

	ret = ipp_do_blit(&req);
	if(ret != 0) {
		ERR("Failed to start IPP operation (%d)\n", ret);
		goto err_noput;
	}

err_noput:

	mutex_unlock(&data->mutex);

	return ret;
}

static int ipp_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{

	struct ipp_context *ctx = (struct ipp_context *)file->private_data;
	int ret = 0;
	switch (cmd)
	{
		case RK_IPP_BLIT:
			ret = stretch_blit(ctx, arg);
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

	return 0;
}

static int ipp_release(struct inode *inode, struct file *file)
{
    struct ipp_context *ctx = (struct ipp_context *)file->private_data;
	kfree(ctx);

	DBG("device released\n");
	return 0;
}

static irqreturn_t rk29_ipp_irq(int irq,  void *dev_id)
{
#ifdef IPP_TEST
	do_gettimeofday(&irq_ret);
#endif
	ipp_write(ipp_read(IPP_INT)|0x3c, IPP_INT);

	DBG("rk29_ipp_irq %d \n",irq);

	wq_condition = 1;
	dsb();
	
#ifdef IPP_TEST
	do_gettimeofday(&irq_done);
#endif
	wake_up_interruptible_sync(&wait_queue);

	return IRQ_HANDLED;
}

struct file_operations ipp_fops = {
	.owner		= THIS_MODULE,
	.open		= rk29_ipp_open,
	.release	= ipp_release,
	.ioctl		= ipp_ioctl,
};

static struct miscdevice ipp_dev ={
    .minor = IPP_MAJOR,
    .name  = "rk29-ipp",
    .fops  = &ipp_fops,
};

#ifdef IPP_TEST

static void ipp_test_work_handler(struct work_struct *work)
{

		 struct rk29_ipp_req ipp_req;
         int i=0,j;
		 int ret = 0;

		 uint8_t *srcY ;
		 uint8_t *dstY;
		 uint8_t *srcUV ;
		 uint8_t *dstUV;
		  uint32_t src_addr;
		 uint32_t dst_addr;
		 uint8_t *p;
#if 0
//test lzg's bug
uint32_t size = 8*1024*1024;

#define PMEM_VPU_BASE 0x76000000
#define PMEM_VPU_SIZE  0x4000000
	//	srcY = ioremap_cached(PMEM_VPU_BASE, PMEM_VPU_SIZE);


	//	srcUV = srcY + size;
	//	dstY = srcUV + size;
	//	dstUV = dstY + size;
		//src_addr = virt_to_phys(srcY);
		//dst_addr = virt_to_phys(dstY);
		src_addr = PMEM_VPU_BASE;
		dst_addr = PMEM_VPU_BASE + size*2;
		printk("src_addr: %x-%x, dst_addr: %x-%x\n",srcY,src_addr,dstY,dst_addr);

		ipp_req.src0.YrgbMst = src_addr;
		ipp_req.src0.CbrMst = src_addr + size;
		ipp_req.src0.w = 2048;
		ipp_req.src0.h = 1440;
		ipp_req.src0.fmt = IPP_Y_CBCR_H2V2;
		
		ipp_req.dst0.YrgbMst = dst_addr;
		ipp_req.dst0.CbrMst = dst_addr + size;
		ipp_req.dst0.w = 2048;
		ipp_req.dst0.h = 1440;
	
		ipp_req.src_vir_w = 2048;
		ipp_req.dst_vir_w = 2048;
		ipp_req.timeout = 500;
		ipp_req.flag = IPP_ROT_0;
		
		ret = ipp_do_blit(&ipp_req);
#undef PMEM_VPU_BASE
#undef PMEM_VPU_SIZE

#endif

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
		ipp_req.src0.w = 800;
		ipp_req.src0.h = 480;
		ipp_req.src0.fmt = IPP_Y_CBCR_H2V2;
		
		ipp_req.dst0.YrgbMst = dst_addr;
		ipp_req.dst0.CbrMst = dst_addr + size;
		ipp_req.dst0.w = 800;
		ipp_req.dst0.h = 480;
	
		ipp_req.src_vir_w = 800;
		ipp_req.dst_vir_w = 800;
		ipp_req.timeout = 50;
		ipp_req.flag = IPP_ROT_0;

		
		while(!ret)
		{
			ipp_req.timeout = 50;
			ret = ipp_do_blit(&ipp_req);
		}
		
		free_pages(srcY, 9);


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

static int __init ipp_drv_probe(struct platform_device *pdev)
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
	data->pd_display = clk_get(&pdev->dev, "pd_display");
	if (IS_ERR(data->pd_display))
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
	schedule_delayed_work(&d_work, msecs_to_jiffies(5000));
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

static int ipp_drv_remove(struct platform_device *pdev)
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

	if(data->pd_display){
		clk_put(data->pd_display);
	}

    kfree(data);
    return 0;
}

static struct platform_driver rk29_ipp_driver = {
	.probe		= ipp_drv_probe,
	.remove		= ipp_drv_remove,
	.driver		= {
		.owner  = THIS_MODULE,
		.name	= "rk29-ipp",
	},
};

static int __init rk29_ipp_init(void)
{
	int ret;

	if ((ret = platform_driver_register(&rk29_ipp_driver)) != 0)
		{
			ERR("Platform device register failed (%d).\n", ret);
			return ret;
		}
		INFO("Module initialized.\n");
		return 0;
}

static void __exit rk29_ipp_exit(void)
{
	platform_driver_unregister(&rk29_ipp_driver);
}

device_initcall_sync(rk29_ipp_init);
module_exit(rk29_ipp_exit);

/* Module information */
MODULE_AUTHOR("wy@rock-chips.com");
MODULE_DESCRIPTION("Driver for rk29 ipp device");
MODULE_LICENSE("GPL");


