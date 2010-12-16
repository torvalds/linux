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


struct ipp_drvdata {
  	struct miscdevice miscdev;
  	struct device dev;
	void *ipp_base;
	struct ipp_regs regs;
	int irq0;
	struct clk *axi_clk;
	struct clk *ahb_clk;
	struct mutex	mutex;	// mutex
};

static struct ipp_drvdata *drvdata = NULL;

static DECLARE_WAIT_QUEUE_HEAD(wait_queue);
static int wq_condition = 0;

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
#ifdef IPP_DEBUG
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
	uint32_t pre_scale_w, pre_scale_h;
	uint32_t post_scale_w = 0x1000;
	uint32_t post_scale_h = 0x1000;
	uint32_t pre_scale_target_w=0, pre_scale_target_h=0;
	uint32_t post_scale_target_w, post_scale_target_h;
	uint32_t dst0_YrgbMst=0,dst0_CbrMst=0;
	uint32_t ret = 0;
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

			ipp_write((ipp_read(IPP_CONFIG)&0xffffffef)|PRE_SCALE, IPP_CONFIG); 		//enable pre_scale
			ipp_write((pre_scale_h-1)<<3|(pre_scale_w-1),IPP_PRE_SCL_PARA);

			if((req->src0.w%pre_scale_w)!=0) //向上取整 ceil
			{
				pre_scale_target_w = req->src0.w/pre_scale_w+1;
			}
			else
			{
				pre_scale_target_w = req->src0.w/pre_scale_w;
			}

			if((req->src0.h%pre_scale_h)!=0)//向上取整 ceil
			{
				pre_scale_target_h  = req->src0.h/pre_scale_h +1;
			}
			else
			{
				pre_scale_target_h = req->src0.h/pre_scale_h;
			}

			ipp_write(((pre_scale_target_h)<<16)|(pre_scale_target_w), IPP_PRE_IMG_INFO);

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

			ipp_write((ipp_read(IPP_CONFIG)&0xffffffef)|PRE_SCALE, IPP_CONFIG); 		//enable pre_scale
			ipp_write((pre_scale_h-1)<<3|(pre_scale_w-1),IPP_PRE_SCL_PARA);

			if((req->src0.w%pre_scale_w)!=0) //向上取整 ceil
			{
				pre_scale_target_w = req->src0.w/pre_scale_w+1;
			}
			else
			{
				pre_scale_target_w = req->src0.w/pre_scale_w;
			}

			if((req->src0.h%pre_scale_h)!=0)//向上取整 ceil
			{
				pre_scale_target_h  = req->src0.h/pre_scale_h +1;
			}
			else
			{
				pre_scale_target_h = req->src0.h/pre_scale_h;
			}
		}
		ipp_write(((pre_scale_target_h)<<16)|(pre_scale_target_w), IPP_PRE_IMG_INFO);

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
			||(req->dst0.w > req->src0.h) ||(req->dst0.h > req->src0.w))
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
			||(req->dst0.w > req->src0.w) ||(req->dst0.h > req->src0.h))
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
		if((IPP_ROT_90 == rotate) || (IPP_ROT_270 == rotate))
		{
			if(pre_scale)
			{
				post_scale_target_w = pre_scale_target_w;
				post_scale_target_h = pre_scale_target_h;
			}
			else
			{
				post_scale_target_w = req->src0.w;
				post_scale_target_h = req->src0.h;
			}

			DBG("post_scale_target_w %d ,post_scale_target_h %d !!!\n",post_scale_target_w,post_scale_target_h);

			switch(req->src0.fmt)
			{
			case IPP_XRGB_8888:
			case IPP_RGB_565:
			case IPP_Y_CBCR_H1V1:
				 post_scale_w = (uint32_t)(4096*(post_scale_target_w-1)/(req->dst0.h-1));
	             break;

			case IPP_Y_CBCR_H2V1:
			case IPP_Y_CBCR_H2V2:
				 post_scale_w = (uint32_t)(4096*(post_scale_target_w/2-1)/(req->dst0.h/2-1));
	           	 break;

			default:
				break;
	        }
			post_scale_h = (uint32_t)(4096*(post_scale_target_h -1)/(req->dst0.w-1));

			DBG("1111 post_scale_w %x,post_scale_h %x!!! \n",post_scale_w,post_scale_h);
		}
		else// 0 180 x-flip y-flip
		{

			if(pre_scale)
			{
				post_scale_target_w = pre_scale_target_w;
				post_scale_target_h = pre_scale_target_h;
			}
			else
			{
				post_scale_target_w = req->src0.w;
				post_scale_target_h = req->src0.h;
			}

			switch(req->src0.fmt)
			{
			case IPP_XRGB_8888:
			case IPP_RGB_565:
			case IPP_Y_CBCR_H1V1:
				 post_scale_w = (uint32_t)(4096*(post_scale_target_w-1)/(req->dst0.w-1));
	             break;

			case IPP_Y_CBCR_H2V1:
			case IPP_Y_CBCR_H2V2:
				 post_scale_w = (uint32_t)(4096*(post_scale_target_w/2-1)/(req->dst0.w/2-1));
	           	 break;

			default:
				break;
	        }
			post_scale_h = (uint32_t)(4096*(post_scale_target_h -1)/(req->dst0.h-1));

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
	else if(req->flag > IPP_ROT_LIMIT)
	{
	    printk("rk29_ipp is not surpport rot degree!!!!\n");
		ret = -1;
		goto error_rot_limit;
	}
	else
	{
		ipp_write(ipp_read(IPP_CONFIG)|ROT_ENABLE, IPP_CONFIG);
	 	ipp_write(ipp_read(IPP_CONFIG)|rotate<<5, IPP_CONFIG);
	}

	/*Configure other*/
	ipp_write((req->dst_vir_w<<16)|req->src_vir_w, IPP_IMG_VIR);

	if((req->src0.w%4) !=0)
	ipp_write(ipp_read(IPP_CONFIG)|(1<<26), IPP_CONFIG);//store clip mode

	/* Start the operation */

	ipp_write(8, IPP_INT);//

	//msleep(5000);//debug use

	ipp_write(1, IPP_PROCESS_ST);

	if(!wait_event_timeout(wait_queue, wq_condition, msecs_to_jiffies(req->timeout)))
	{
		printk("%s wait_event_timeout \n",__FUNCTION__);
		wq_condition = 0;
		if(!drvdata)
		{
	        printk("close clk 0! \n");
	        ret = -EINVAL;
			goto error_null;
	    }
		ret =  -EAGAIN;
		goto errot_timeout;
	}

	wq_condition = 0;

	if(!drvdata)
	{
        printk("close clk! \n");
        ret =  -EINVAL;
		goto error_null;
    }

	if(((ipp_read(IPP_INT)>>6)&0x3) ==0)// idle
	{
		clk_disable(drvdata->axi_clk);
		clk_disable(drvdata->ahb_clk);
	}
	else
	{
		printk("rk29 ipp status is error!!!\n");
		ret =  -EINVAL;
		goto errot_timeout;
	}
	return 0;

error_null:
errot_timeout:
error_rot_limit:
error_ahb_clk:
	clk_disable(drvdata->ahb_clk);
error_axi_clk:
	clk_disable(drvdata->axi_clk);
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

	if (unlikely((req.src0.w <= 1) || (req.src0.h <= 1))) {
		ERR("invalid source resolution\n");
		ret = -EINVAL;
		goto err_noput;
	}

	if (unlikely((req.dst0.w <= 1) || (req.dst0.h <= 1))) {
		ERR("invalid destination resolution\n");
		ret = -EINVAL;
		goto err_noput;
	}

	if (unlikely(req.src0.YrgbMst== 0) )
	{
		ERR("could not retrieve src image from memory\n");
		ret = -EINVAL;
		goto err_noput;
	}

	if (unlikely(req.dst0.YrgbMst== 0) )
	{
		ERR("could not retrieve dst image from memory\n");
		ret = -EINVAL;
		goto err_noput;
	}

	ret = ipp_do_blit(&req);
	if(ret != 0) {
		ERR("Failed to start IPP operation (%d)\n", ret);
		ipp_soft_reset();
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
	uint32_t stat;

	DBG("rk29_ipp_irq %d \n",irq);
	stat = ipp_read(IPP_INT);

	while(!(stat & 0x1))
	{
		DBG("stat %d \n",stat);
	}
	wq_condition = 1;
	wake_up(&wait_queue);

	ipp_write(ipp_read(IPP_INT)|0xc, IPP_INT);
	ipp_write(ipp_read(IPP_INT)|0x30, IPP_INT);
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
	data->axi_clk = clk_get(&pdev->dev, "ipp_axi");
	if(NULL == data->axi_clk)
	{
		ERR("failed to find ipp axi clock source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	data->ahb_clk = clk_get(&pdev->dev, "ipp_ahb");
	if(NULL == data->ahb_clk)
	{
		ERR("failed to find ipp ahb clock source\n");
		ret = -ENOENT;
		goto err_clock;
	}

	/* map the memory */
	data->ipp_base = (void*)ioremap( RK29_IPP_PHYS, SZ_16K);//ipp size 16k
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
	ret = request_irq(data->irq0, rk29_ipp_irq, IRQF_SHARED, "rk29-ipp", pdev);
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

module_init(rk29_ipp_init);
module_exit(rk29_ipp_exit);

/* Module information */
MODULE_AUTHOR("wy@rock-chips.com");
MODULE_DESCRIPTION("Driver for rk29 ipp device");
MODULE_LICENSE("GPL");


