/*
 * drivers/video/rk30_fb.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/backlight.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/earlysuspend.h>
#include <linux/cpufreq.h>
#include <linux/wakelock.h>

#include <asm/io.h>
#include <asm/div64.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>

#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/pmu.h>

#include "../display/screen/screen.h"
#include "rk_fb.h"

#if 0
	#define fbprintk(msg...)	printk(msg);
#else
	#define fbprintk(msg...)
#endif

#if 0
#define CHK_SUSPEND(inf)	\
	if(inf->in_suspend)	{	\
		fbprintk(">>>>>> fb is in suspend! return! \n");	\
		return -EPERM;	\
	}
#else
#define CHK_SUSPEND(inf)
#endif

static struct platform_device *g_fb_pdev;

static struct rk_fb_rgb def_rgb_16 = {
     red:    { offset: 11, length: 5, },
     green:  { offset: 5,  length: 6, },
     blue:   { offset: 0,  length: 5, },
     transp: { offset: 0,  length: 0, },
};

int fb0_open(struct fb_info *info, int user)
{
  return 0; 
}

int fb0_release(struct fb_info *info, int user)
{
  
    return 0;
}

static int fb0_blank(int blank_mode, struct fb_info *info)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
	struct rk_lcdc_device_driver *rk_fb_dev_drv = fb_inf->rk_lcdc_device[0];
	 fb_inf->rk_lcdc_device[0]->blank(rk_fb_dev_drv,0,blank_mode);

    return 0;
}

static int fb0_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
 
 	struct rk_fb_inf *inf = dev_get_drvdata(info->device);
	rk_screen *screen = &inf->rk_lcdc_device[0]->screen;
	 u16 xpos = (var->nonstd>>8) & 0xfff;
	 u16 ypos = (var->nonstd>>20) & 0xfff;
	 u16 xlcd = screen->x_res;
	 u16 ylcd = screen->y_res;
 
	 //fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);
 
	 CHK_SUSPEND(inf);
 
	 if( 0==var->xres_virtual || 0==var->yres_virtual ||
		 0==var->xres || 0==var->yres || var->xres<16 ||
		 ((16!=var->bits_per_pixel)&&(32!=var->bits_per_pixel)) )
	 {
		 printk(">>>>>> fb0_check_var fail 1!!! \n");
		 printk(">>>>>> 0==%d || 0==%d ", var->xres_virtual,var->yres_virtual);
		 printk("0==%d || 0==%d || %d<16 || ", var->xres,var->yres,var->xres<16);
		 printk("bits_per_pixel=%d \n", var->bits_per_pixel);
		 return -EINVAL;
	 }
 
	 if( (var->xoffset+var->xres)>var->xres_virtual ||
		 (var->yoffset+var->yres)>var->yres_virtual*2 )
	 {
		 printk(">>>>>> fb0_check_var fail 2!!! \n");
		 printk(">>>>>> (%d+%d)>%d || ", var->xoffset,var->xres,var->xres_virtual);
		 printk("(%d+%d)>%d || ", var->yoffset,var->yres,var->yres_virtual);
		 printk("(%d+%d)>%d || (%d+%d)>%d \n", xpos,var->xres,xlcd,ypos,var->yres,ylcd);
		 return -EINVAL;
	 }
 
	 switch(var->bits_per_pixel)
	 {
	 case 16:	 // rgb565
		 var->xres_virtual = (var->xres_virtual + 0x1) & (~0x1);
		 var->xres = (var->xres + 0x1) & (~0x1);
		 var->xoffset = (var->xoffset) & (~0x1);
		 break;
	 default:	 // rgb888
		 var->bits_per_pixel = 32;
		 break;
	 }
    return 0;
}


static int fb0_set_par(struct fb_info *info)
{
	struct rk_fb_inf *inf =  platform_get_drvdata(g_fb_pdev);
	struct fb_var_screeninfo *var = &info->var;
	struct fb_fix_screeninfo *fix = &info->fix;
	struct rk_lcdc_device_driver *rk_fb_dev_drv = inf->rk_lcdc_device[0];
	struct layer_par *par = &rk_fb_dev_drv->layer_par[0];
	rk_screen *screen = &rk_fb_dev_drv->screen;
	
	u32 offset=0,  smem_len=0;
    u16 xres_virtual = var->xres_virtual;      //virtual screen size
    u16 xpos_virtual = var->xoffset;           //visiable offset in virtual screen
    u16 ypos_virtual = var->yoffset;

	 switch(var->bits_per_pixel)
     {
    	case 16:    // rgb565
        	par->format = RGB565;
   			fix->line_length = 2 * xres_virtual;  
			offset = (ypos_virtual*xres_virtual + xpos_virtual)*2;
        	break;
    	case 32:    // rgb888
    	default:
        	par->format = RGB888;
        	fix->line_length = 4 * xres_virtual;
        	offset = (ypos_virtual*xres_virtual + xpos_virtual)*4;
        
        	if(ypos_virtual >= 2*var->yres)
        	{
            	par->format = RGB565;
            	if(ypos_virtual == 3*var->yres)
            	{            
                	offset -= var->yres * var->xres *2;
            	}
        	}
        	break;
     }

	 smem_len = fix->line_length * var->yres_virtual;
     if (smem_len > fix->smem_len)     // buffer need realloc
     {
        printk("%s sorry!!! win0 buf is not enough\n",__FUNCTION__);
        printk("line_length = %d, yres_virtual = %d, win0_buf only = %dB\n",fix->line_length,var->yres_virtual,fix->smem_len);
        printk("you can change buf size MEM_FB_SIZE in board-xxx.c \n");
		return 0;
     }
	  par->smem_start = fix->smem_start;
	  par->y_offset = offset;
      par->xpos = 0;
      par->ypos = 0;
	  par->xact = var->xres;
	  par->yact = var->yres;
	  par->xres_virtual = var->xres_virtual;		/* virtual resolution		*/
	  par->yres_virtual = var->yres_virtual;
      par->xsize = screen->x_res;
      par->ysize = screen->y_res;
      inf->rk_lcdc_device[0]->set_par(rk_fb_dev_drv,0);
	return 0;
}

static int fb0_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
    return 0;
}

static int fb0_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
    return 0;
}

static int fb1_blank(int blank_mode, struct fb_info *info)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
   return 0;
}

static int fb1_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    return 0;
}

static int fb1_set_par(struct fb_info *info)
{
 struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
   return 0;
    return 0;
}

static int fb1_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
    return 0;
}

static int fb1_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
  struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
    return 0;
}
int fb1_open(struct fb_info *info, int user)
{
  return 0; 
}

int fb1_release(struct fb_info *info, int user)
{
  
    return 0;
}
static int fb2_blank(int blank_mode, struct fb_info *info)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
   return 0;
}

static int fb2_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    return 0;
}

static int fb2_set_par(struct fb_info *info)
{
 struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
   return 0;
    return 0;
}

static int fb2_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
    return 0;
}

static int fb2_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
  struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
    return 0;
}
int fb2_open(struct fb_info *info, int user)
{
  return 0; 
}

int fb2_release(struct fb_info *info, int user)
{
  
    return 0;
}

static int fb3_blank(int blank_mode, struct fb_info *info)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
   return 0;
}

static int fb3_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    return 0;
}

static int fb3_set_par(struct fb_info *info)
{
 struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
   return 0;
 
}

static int fb3_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
    return 0;
}

static int fb3_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
  struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
    return 0;
}
int fb3_open(struct fb_info *info, int user)
{
  return 0; 
}

int fb3_release(struct fb_info *info, int user)
{
  
    return 0;
}


static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int fb_setcolreg(unsigned regno,
			       unsigned red, unsigned green, unsigned blue,
			       unsigned transp, struct fb_info *info)
{
	unsigned int val;
//	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/* true-colour, use pseudo-palette */
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;
			val  = chan_to_field(red,   &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue,  &info->var.blue);
			pal[regno] = val;
		}
		break;
	default:
		return -1;	/* unknown type */
	}

	return 0;
}

static struct fb_ops fb_ops[] = {
	[0]={
		.owner		= THIS_MODULE,
		.fb_open    = fb0_open,
		.fb_release = fb0_release,
		.fb_check_var	= fb0_check_var,
		.fb_set_par	= fb0_set_par,
		.fb_blank	= fb0_blank,
		.fb_pan_display = fb0_pan_display,
		.fb_ioctl = fb0_ioctl,
		.fb_setcolreg	= fb_setcolreg,
		.fb_fillrect	= cfb_fillrect,
		.fb_copyarea	= cfb_copyarea,
		.fb_imageblit	= cfb_imageblit,
	},
	[1]={
		.owner		= THIS_MODULE,
		.fb_open    = fb1_open,
		.fb_release = fb1_release,
		.fb_check_var	= fb1_check_var,
		.fb_set_par	= fb1_set_par,
		.fb_blank	= fb1_blank,
		.fb_pan_display = fb1_pan_display,
		.fb_ioctl = fb1_ioctl,
		.fb_setcolreg	= fb_setcolreg,
		.fb_fillrect	= cfb_fillrect,
		.fb_copyarea	= cfb_copyarea,
		.fb_imageblit	= cfb_imageblit,	
	},
	[2]={
		.owner		= THIS_MODULE,
		.fb_open    = fb2_open,
		.fb_release = fb2_release,
		.fb_check_var	= fb2_check_var,
		.fb_set_par	= fb2_set_par,
		.fb_blank	= fb2_blank,
		.fb_pan_display = fb2_pan_display,
		.fb_ioctl = fb2_ioctl,
		.fb_setcolreg	= fb_setcolreg,
		.fb_fillrect	= cfb_fillrect,
		.fb_copyarea	= cfb_copyarea,
		.fb_imageblit	= cfb_imageblit,
	},
	[3]={
		.owner		= THIS_MODULE,
		.fb_open    = fb3_open,
		.fb_release = fb3_release,
		.fb_check_var	= fb3_check_var,
		.fb_set_par	= fb3_set_par,
		.fb_blank	= fb3_blank,
		.fb_pan_display = fb3_pan_display,
		.fb_ioctl = fb3_ioctl,
		.fb_setcolreg	= fb_setcolreg,
		.fb_fillrect	= cfb_fillrect,
		.fb_copyarea	= cfb_copyarea,
		.fb_imageblit	= cfb_imageblit,
	}
};



static struct fb_var_screeninfo def_var = {
    .red    = {11,5,0},//def_rgb_16.red,
    .green  = {5,6,0},//def_rgb_16.green,
    .blue   = {0,5,0},
    .transp = {0,0,0},	
    .nonstd      = 0, //win1 format & ypos & xpos (ypos<<20 + xpos<<8 + format)
    .grayscale   = 0,  //win1 transprent mode & value(mode<<8 + value)
    .activate    = FB_ACTIVATE_NOW,
    .accel_flags = 0,
    .vmode       = FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo def_fix = {
	.type		 = FB_TYPE_PACKED_PIXELS,
	.type_aux	 = 0,
	.xpanstep	 = 1,
	.ypanstep	 = 1,
	.ywrapstep	 = 0,
	.accel		 = FB_ACCEL_NONE,
	.visual 	 = FB_VISUAL_TRUECOLOR,
		
};

static int request_fb_buffer(struct fb_info *fbi,int fb_id)
{
	struct resource *res;
	int ret = 0;
	switch(fb_id)
	{
		case 0:
			res = platform_get_resource_byname(g_fb_pdev, IORESOURCE_MEM, "fb0 buf");
			if (res == NULL)
			{
        		dev_err(&g_fb_pdev->dev, "failed to get win0 memory \n");
        		ret = -ENOENT;
    		}
    		fbi->fix.smem_start = res->start;
    		fbi->fix.smem_len = res->end - res->start + 1;
    		fbi->screen_base = ioremap(res->start, fbi->fix.smem_len);
    		memset(fbi->screen_base, 0, fbi->fix.smem_len);
			break;
		#ifdef CONFIG_FB_WORK_IPP
		case 1:
       		/* alloc ipp buf for rotate */
    		res = platform_get_resource_byname(g_fb_pdev, IORESOURCE_MEM, "ipp buf");
			if (res == NULL)
    		{
        		dev_err(&g_fb_pdev->dev, "failed to get win1 ipp memory \n");
        		ret = -ENOENT;
        	}
    		fbi->fix.mmio_start = res->start;
    		fbi->fix.mmio_len = res->end - res->start + 1;
			break;
		#endif
		case 2:
			res = platform_get_resource_byname(g_fb_pdev, IORESOURCE_MEM, "fb2 buf");
			if (res == NULL)
			{
        		dev_err(&g_fb_pdev->dev, "failed to get win0 memory \n");
        		ret = -ENOENT;
    		}
    		fbi->fix.smem_start = res->start;
    		fbi->fix.smem_len = res->end - res->start + 1;
    		fbi->screen_base = ioremap(res->start, fbi->fix.smem_len);
    		memset(fbi->screen_base, 0, fbi->fix.smem_len);
		    break;
		default:
			break;
			
	}
	return ret;
}
int rk_fb_register(struct rk_lcdc_device_driver *dev_drv)
{
	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
	struct fb_info *fbi;
	int i=0,ret = 0;
	int lcdc_id = 0;
	int win0_index = 0;
	if(NULL==dev_drv){
		printk("rk_fb_register lcdc register fail");
		return -ENOENT;
	}
	for(i=0;i<RK30_MAX_LCDC_SUPPORT;i++){
		if(NULL==fb_inf->rk_lcdc_device[i]){
		fb_inf->rk_lcdc_device[i] = dev_drv;
		fb_inf->rk_lcdc_device[i]->id = i;
		fb_inf->num_lcdc++;
		break;
		}
	}
	if(i==RK30_MAX_LCDC_SUPPORT){
		printk("rk_fb_register lcdc out of support %d",i);
		return -ENOENT;
	}
	lcdc_id = i;
	
	/************fb set,one layer one fb ***********/
	for(i=0;i<dev_drv->num_layer;i++)
	{
		fbi= framebuffer_alloc(0, &g_fb_pdev->dev);
		if(!fbi)
		{
			dev_err(&g_fb_pdev->dev,">> fb framebuffer_alloc fail!");
			fbi = NULL;
			ret = -ENOMEM;
		}
		fbi->var = def_var;
		fbi->fix = def_fix;
		sprintf(fbi->fix.id,"fb%d",fb_inf->num_fb);
		fbi->var.xres = fb_inf->rk_lcdc_device[lcdc_id]->screen.x_res;
		fbi->var.yres = fb_inf->rk_lcdc_device[lcdc_id]->screen.y_res;
		fbi->var.bits_per_pixel = 16;
		fbi->var.xres_virtual = fb_inf->rk_lcdc_device[lcdc_id]->screen.x_res;
		fbi->var.yres_virtual = fb_inf->rk_lcdc_device[lcdc_id]->screen.y_res;
		fbi->var.width = fb_inf->rk_lcdc_device[lcdc_id]->screen.width;
		fbi->var.height = fb_inf->rk_lcdc_device[lcdc_id]->screen.height;
		fbi->var.pixclock =fb_inf->rk_lcdc_device[lcdc_id]->pixclock;
		fbi->var.left_margin = fb_inf->rk_lcdc_device[lcdc_id]->screen.left_margin;
		fbi->var.right_margin = fb_inf->rk_lcdc_device[lcdc_id]->screen.right_margin;
		fbi->var.upper_margin = fb_inf->rk_lcdc_device[lcdc_id]->screen.upper_margin;
		fbi->var.lower_margin = fb_inf->rk_lcdc_device[lcdc_id]->screen.lower_margin;
		fbi->var.vsync_len = fb_inf->rk_lcdc_device[lcdc_id]->screen.vsync_len;
		fbi->var.hsync_len = fb_inf->rk_lcdc_device[lcdc_id]->screen.hsync_len;
		fbi->fbops			 = &fb_ops[fb_inf->num_fb];
		fbi->flags			 = FBINFO_FLAG_DEFAULT;
		fbi->pseudo_palette  = fb_inf->rk_lcdc_device[lcdc_id]->layer_par[i].pseudo_pal;
		request_fb_buffer(fbi,fb_inf->num_fb);
		fb_inf->fb[fb_inf->num_fb] = fbi;
		printk("%s>>>>>%s\n",__func__,fb_inf->fb[fb_inf->num_fb]->fix.id);
		fb_inf->num_fb++;
		
	}
#if !defined(CONFIG_FRAMEBUFFER_CONSOLE) && defined(CONFIG_LOGO)
    fb_inf->fb[fb_inf->num_fb-2]->fbops->fb_set_par(fb_inf->fb[fb_inf->num_fb-2]);
    if(fb_prepare_logo(fb_inf->fb[fb_inf->num_fb-2], FB_ROTATE_UR)) {
        /* Start display and show logo on boot */
        fb_set_cmap(&fb_inf->fb[fb_inf->num_fb-2]->cmap, fb_inf->fb[fb_inf->num_fb-2]);
        fb_show_logo(fb_inf->fb[fb_inf->num_fb-2], FB_ROTATE_UR);
        fb_inf->fb[fb_inf->num_fb-2]->fbops->fb_blank(FB_BLANK_UNBLANK, fb_inf->fb[fb_inf->num_fb-2]);
    }
#endif
	return 0;
	
	
}
int rk_fb_unregister(struct rk_lcdc_device_driver *fb_device_driver)
{

	struct rk_fb_inf *fb_inf = platform_get_drvdata(g_fb_pdev);
	int i=0;
	if(NULL==fb_device_driver){
		printk("rk_fb_register lcdc register fail");
		return -ENOENT;
		}
	for(i=0;i<RK30_MAX_LCDC_SUPPORT;i++){
		if(fb_inf->rk_lcdc_device[i]->id == i ){
		fb_inf->rk_lcdc_device[i] = NULL;
		fb_inf->num_lcdc--;
		break;
		}
	}
	if(i==RK30_MAX_LCDC_SUPPORT){
		printk("rk_fb_unregister lcdc out of support %d",i);
		return -ENOENT;
	}
	

	return 0;
}

static int __devinit rk_fb_probe (struct platform_device *pdev)
{
	struct rk_fb_inf *fb_inf	= NULL;
	int ret = 0;
	g_fb_pdev=pdev;
    /* Malloc rk29fb_inf and set it to pdev for drvdata */
    fb_inf = kmalloc(sizeof(struct rk_fb_inf), GFP_KERNEL);
    if(!fb_inf)
    {
        dev_err(&pdev->dev, ">>fb inf kmalloc fail!");
        ret = -ENOMEM;
    }
    memset(fb_inf, 0, sizeof(struct rk_fb_inf));
	platform_set_drvdata(pdev, fb_inf);
	printk("rk fb probe ok!\n");
    return 0;
}

static int __devexit rk_fb_remove(struct platform_device *pdev)
{
    return 0;
}

static void rk_fb_shutdown(struct platform_device *pdev)
{

}

static struct platform_driver rk_fb_driver = {
	.probe		= rk_fb_probe,
	.remove		= __devexit_p(rk_fb_remove),
	.driver		= {
		.name	= "rk-fb",
		.owner	= THIS_MODULE,
	},
	.shutdown   = rk_fb_shutdown,
};

static int __init rk_fb_init(void)
{
    return platform_driver_register(&rk_fb_driver);
}

static void __exit rk_fb_exit(void)
{
    platform_driver_unregister(&rk_fb_driver);
}

fs_initcall(rk_fb_init);
module_exit(rk_fb_exit);

