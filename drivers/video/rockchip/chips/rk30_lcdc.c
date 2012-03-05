
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>

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
#include <linux/fb.h>

#include <mach/board.h>
#include <mach/pmu.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
#include <asm/cacheflush.h>
#include "../../display/screen/screen.h"
#include "../rk_fb.h"
#include "rk30_lcdc.h"




static int dbg_thresd =1;
#define DBG(x...) do { if(unlikely(dbg_thresd)) printk(KERN_INFO x); } while (0)


static struct platform_device * g_lcdc_pdev;
static  rk_screen * to_screen(struct rk30_lcdc_device *lcdc_dev)
{
	return &(lcdc_dev->driver->screen);
}


static int init_rk30_lcdc(struct lcdc_info *info)
{
	struct rk30_lcdc_device * lcdc_dev = &info->lcdc0;
	struct rk30_lcdc_device * lcdc_dev1 = &info->lcdc1;
	info->lcdc0.hclk = clk_get(NULL,"hclk_lcdc0"); 
	info->lcdc0.aclk = clk_get(NULL,"aclk_lcdc0");
	info->lcdc0.dclk = clk_get(NULL,"dclk_lcdc0");
	
	info->lcdc1.hclk = clk_get(NULL,"hclk_lcdc1"); 
	info->lcdc1.aclk = clk_get(NULL,"aclk_lcdc1");
	info->lcdc1.dclk = clk_get(NULL,"dclk_lcdc1");
	if ((IS_ERR(info->lcdc0.aclk)) ||(IS_ERR(info->lcdc0.dclk)) || (IS_ERR(info->lcdc0.hclk))||
		(IS_ERR(info->lcdc1.aclk)) ||(IS_ERR(info->lcdc1.dclk)) || (IS_ERR(info->lcdc1.hclk)))
    {
        printk(KERN_ERR "failed to get lcdc_hclk source\n");
        return PTR_ERR(info->lcdc0.aclk);
    }
	clk_enable(info->lcdc0.hclk);  //enable aclk for register config
	clk_enable(info->lcdc1.hclk);
	
	LcdSetBit(lcdc_dev,DSP_CTRL0, m_LCDC_AXICLK_AUTO_ENABLE);//eanble axi-clk auto gating for low power
	LcdSetBit(lcdc_dev1,DSP_CTRL0, m_LCDC_AXICLK_AUTO_ENABLE);
	return 0;
}

int rk30_lcdc_deinit(void)
{
    return 0;
}

void rk30_load_screen(struct rk30_lcdc_device*lcdc_dev, bool initscreen)
{
    int ret = -EINVAL;
    rk_screen *screen = to_screen(lcdc_dev);
    u16 face;
    u16 mcu_total, mcu_rwstart, mcu_csstart, mcu_rwend, mcu_csend;
    u16 right_margin = screen->right_margin;
	u16 lower_margin = screen->lower_margin;
    u16 x_res = screen->x_res, y_res = screen->y_res;
    u32 aclk_rate = 150000000;

    // set the rgb or mcu

	if(screen->type==SCREEN_MCU)
	{
    	LcdMskReg(lcdc_dev, MCU_CTRL, m_MCU_OUTPUT_SELECT,v_MCU_OUTPUT_SELECT(1));
		// set out format and mcu timing
   		 mcu_total  = (screen->mcu_wrperiod*150*1000)/1000000;
    	if(mcu_total>31)    mcu_total = 31;
   		if(mcu_total<3)     mcu_total = 3;
    	mcu_rwstart = (mcu_total+1)/4 - 1;
    	mcu_rwend = ((mcu_total+1)*3)/4 - 1;
    	mcu_csstart = (mcu_rwstart>2) ? (mcu_rwstart-3) : (0);
    	mcu_csend = (mcu_rwend>15) ? (mcu_rwend-1) : (mcu_rwend);

    	DBG(">> mcu_total=%d, mcu_rwstart=%d, mcu_csstart=%d, mcu_rwend=%d, mcu_csend=%d \n",
        mcu_total, mcu_rwstart, mcu_csstart, mcu_rwend, mcu_csend);

		// set horizontal & vertical out timing
	
	    right_margin = x_res/6; 
		screen->pixclock = 150000000; //mcu fix to 150 MHz
	
	}

	

	// set synchronous pin polarity and data pin swap rule
	switch (screen->face)
	{
        case OUT_P565:
            face = OUT_P565;
            LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0));
            break;
        case OUT_P666:
            face = OUT_P666;
            LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1));
            break;
        case OUT_D888_P565:
            face = OUT_P888;
            LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0));
            break;
        case OUT_D888_P666:
            face = OUT_P888;
            LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1));
            break;
        case OUT_P888:
            face = OUT_P888;
            LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_UP_EN, v_DITHER_UP_EN(1));
            LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(0) | v_DITHER_DOWN_MODE(0));
            break;
        default:
            LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_UP_EN, v_DITHER_UP_EN(0));
            LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(0) | v_DITHER_DOWN_MODE(0));
            face = screen->face;
            break;
    }

	//use default overlay,set vsyn hsync den dclk polarity
     LcdMskReg(lcdc_dev, DSP_CTRL0,m_DISPLAY_FORMAT | m_HSYNC_POLARITY | m_VSYNC_POLARITY 
	 	| m_DEN_POLARITY |m_DCLK_POLARITY,
        v_DISPLAY_FORMAT(face) | v_HSYNC_POLARITY(screen->pin_hsync) | v_VSYNC_POLARITY(screen->pin_vsync) |
        v_DEN_POLARITY(screen->pin_den) | v_DCLK_POLARITY(screen->pin_dclk));

	//set background color to black,set swap according to the screen panel
     LcdMskReg(lcdc_dev, DSP_CTRL1, m_BG_COLOR | m_OUTPUT_RB_SWAP | m_OUTPUT_RG_SWAP | m_DELTA_SWAP | 
	 	m_DUMMY_SWAP,  v_BG_COLOR(0x000000) | v_OUTPUT_RB_SWAP(screen->swap_rb) | 
	 	v_OUTPUT_RG_SWAP(screen->swap_rg) | v_DELTA_SWAP(screen->swap_delta) | v_DUMMY_SWAP(screen->swap_dumy) );

	
    LcdWrReg(lcdc_dev, DSP_HTOTAL_HS_END,v_HSYNC(screen->hsync_len) |
             v_HORPRD(screen->hsync_len + screen->left_margin + x_res + right_margin));
    LcdWrReg(lcdc_dev, DSP_HACT_ST_END, v_HAEP(screen->hsync_len + screen->left_margin + x_res) |
             v_HASP(screen->hsync_len + screen->left_margin));

    LcdWrReg(lcdc_dev, DSP_VTOTAL_VS_END, v_VSYNC(screen->vsync_len) |
              v_VERPRD(screen->vsync_len + screen->upper_margin + y_res + lower_margin));
    LcdWrReg(lcdc_dev, DSP_VACT_ST_END,  v_VAEP(screen->vsync_len + screen->upper_margin+y_res)|
              v_VASP(screen->vsync_len + screen->upper_margin));
	// let above to take effect
    LcdWrReg(lcdc_dev, REG_CFG_DONE, 0x01);
                                                        
    if(initscreen == 0)    //not init
    {
        clk_disable(lcdc_dev->dclk);
      //  clk_disable(inf->aclk);
    }

 

  //  ret = clk_set_rate(lcdc_dev->dclk, screen->pixclock);
    if(ret)
    {
        printk(KERN_ERR ">>>>>> set lcdc dclk failed\n");
    }
    lcdc_dev->driver->pixclock = lcdc_dev->pixclock = div_u64(1000000000000llu, clk_get_rate(lcdc_dev->dclk));
    if(initscreen)
    {
        //ret = clk_set_parent(lcdc_dev->aclk, lcdc_dev->aclk_parent);
        if(screen->lcdc_aclk){
           aclk_rate = screen->lcdc_aclk;
        }
       // ret = clk_set_rate(lcdc_dev->aclk, aclk_rate);
        if(ret){
            printk(KERN_ERR ">>>>>> set lcdc aclk failed\n");
        }
        clk_enable(lcdc_dev->aclk);
    }
  
    clk_enable(lcdc_dev->dclk);
   
    if(screen->init)
    {
    	screen->init();
    }
	printk("%s>>>>>ok!\n",__func__);
}

static int mcu_refresh(struct rk30_lcdc_device *lcdc_dev)
{
   
    return 0;
}

static int win0_blank(int blank_mode,struct rk30_lcdc_device *lcdc_dev)
{
	switch(blank_mode)
	{
		case FB_BLANK_UNBLANK:
        	LcdMskReg(lcdc_dev, SYS_CTRL1, m_W0_EN, v_W0_EN(1));
        	break;
    	case FB_BLANK_NORMAL:
         	LcdMskReg(lcdc_dev, SYS_CTRL1, m_W0_EN, v_W0_EN(0));
	     	break;
    	default:
        	LcdMskReg(lcdc_dev, SYS_CTRL1, m_W0_EN, v_W1_EN(1));
        	break;
	}
  	mcu_refresh(lcdc_dev);
    return 0;
}

static int win1_blank(int blank_mode, struct rk30_lcdc_device *lcdc_dev)
{
	printk(KERN_INFO "%s>>>>>>>>>>mode:%d\n",__func__,blank_mode);
	switch(blank_mode)
    {
    case FB_BLANK_UNBLANK:
        LcdMskReg(lcdc_dev, SYS_CTRL1, m_W1_EN, v_W1_EN(1));
        break;
    case FB_BLANK_NORMAL:
         LcdMskReg(lcdc_dev, SYS_CTRL1, m_W1_EN, v_W1_EN(0));
	     break;
    default:
        LcdMskReg(lcdc_dev, SYS_CTRL1, m_W1_EN, v_W1_EN(0));
        break;
    }
    
	//mcu_refresh(inf);
    return 0;
}
static int rk30_lcdc_blank(struct rk_lcdc_device_driver*fb_drv,int layer_id,int blank_mode)
{
	struct rk30_lcdc_device * lcdc_dev = NULL;
	struct lcdc_info * info = platform_get_drvdata(g_lcdc_pdev);
	if(!strcmp(fb_drv->name,"lcdc0"))
	{
		lcdc_dev =  &(info->lcdc0); 
	}
	else if(!strcmp(fb_drv->name,"lcdc1"))
	{
		lcdc_dev = &(info->lcdc1);
	}
	if(layer_id==0)
	{
	 	win0_blank(blank_mode,lcdc_dev);
	}
	else if(layer_id==1)
	{
		win1_blank(blank_mode,lcdc_dev);
	}

	LcdWrReg(lcdc_dev, REG_CFG_DONE, 0x01);
    return 0;
}

static  int win0_set_par(struct rk30_lcdc_device *lcdc_dev,rk_screen *screen,
	struct layer_par *par )
{
    u32 xact, yact, xvir, yvir, xpos, ypos;
    u32 ScaleYrgbX,ScaleYrgbY, ScaleCbrX, ScaleCbrY;
    u32 y_addr,uv_addr;

    xact = par->xact;			    /*active (origin) picture window width/height		*/
    yact = par->yact;
    xvir = par->xres_virtual;		/* virtual resolution		*/
    yvir = par->yres_virtual;
    xpos = par->xpos+screen->left_margin + screen->hsync_len;
    ypos = par->ypos+screen->upper_margin + screen->vsync_len;
    y_addr = par->smem_start + par->y_offset;
    uv_addr = par->cbr_start + par->c_offset;
	
    ScaleYrgbX = CalScale(xact, par->xsize); //both RGB and yuv need this two factor
    ScaleYrgbY = CalScale(yact, par->ysize);
    switch (par->format)
    {
       case YUV422:// yuv422
            ScaleCbrX=  CalScale((xact/2), par->xsize);
            ScaleCbrY = CalScale(yact, par->ysize);
       	    break;
       case YUV420: // yuv420
           ScaleCbrX= CalScale(xact/2, par->xsize);
           ScaleCbrY =  CalScale(yact/2, par->ysize);
           break;
       case YUV444:// yuv444
           ScaleCbrX= CalScale(xact, par->xsize);
           ScaleCbrY = CalScale(yact, par->ysize);
           break;
       default:
            printk("un support format!\n");
           break;
    }

	DBG("%s>>format:%d>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d>>xvir:%d>>yvir:%d>>ypos:%d>>y_addr:0x%x>>uv_addr:0x%x\n",
		__func__,par->format,xact,yact,par->xsize,par->ysize,xvir,yvir,ypos,y_addr,uv_addr);
    LcdWrReg(lcdc_dev, WIN0_YRGB_MST0, y_addr);
    LcdWrReg(lcdc_dev,WIN0_CBR_MST0,uv_addr);
    LcdMskReg(lcdc_dev,SYS_CTRL1,  m_W0_FORMAT , v_W0_FORMAT(par->format));		//(inf->video_mode==0)
    LcdWrReg(lcdc_dev, WIN0_ACT_INFO,v_ACT_WIDTH(xact) | v_ACT_HEIGHT(yact));
    LcdWrReg(lcdc_dev, WIN0_DSP_ST, v_DSP_STX(xpos) | v_DSP_STY(ypos));
    LcdWrReg(lcdc_dev, WIN0_DSP_INFO, v_DSP_WIDTH(par->xsize) | v_DSP_HEIGHT(par->ysize));
    LcdWrReg(lcdc_dev, WIN0_SCL_FACTOR_YRGB, v_X_SCL_FACTOR(ScaleYrgbX) | v_Y_SCL_FACTOR(ScaleYrgbY));
    LcdWrReg(lcdc_dev, WIN0_SCL_FACTOR_CBR,  v_X_SCL_FACTOR(ScaleCbrX) | v_Y_SCL_FACTOR(ScaleCbrY));
    switch(par->format)
    {
        case ARGB888:
            LcdWrReg(lcdc_dev, WIN0_VIR,v_ARGB888_VIRWIDTH(xvir));
            LcdMskReg(lcdc_dev,SYS_CTRL1,m_W0_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(1));
            break;
        case RGB888:  //rgb888
            LcdWrReg(lcdc_dev, WIN0_VIR,v_RGB888_VIRWIDTH(xvir));
            LcdMskReg(lcdc_dev,SYS_CTRL1,m_W0_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(1));
            break;
        case RGB565:  //rgb565
            LcdWrReg(lcdc_dev, WIN0_VIR,v_RGB565_VIRWIDTH(xvir));
            break;
        case YUV422:
        case YUV420:   
            LcdWrReg(lcdc_dev, WIN0_VIR,v_YUV_VIRWIDTH(xvir));
            break;
        default:
            LcdWrReg(lcdc_dev, WIN0_VIR,v_RGB888_VIRWIDTH(xvir));
            break;
    }

    LcdWrReg(lcdc_dev, REG_CFG_DONE, 0x01);
	
    return 0;

}

static int win1_set_par(struct rk30_lcdc_device *lcdc_dev,rk_screen *screen,
	struct layer_par *par )

{
	u32 xact, yact, xvir, yvir, xpos, ypos;
	u32 ScaleYrgbX,ScaleYrgbY, ScaleCbrX, ScaleCbrY;
	u32 y_addr,uv_addr;
	xact = par->xact;			    /* visible resolution		*/
	yact = par->yact;
	xvir = par->xres_virtual;		/* virtual resolution		*/
	yvir = par->yres_virtual;
	xpos = par->xpos+screen->left_margin + screen->hsync_len;
	ypos = par->ypos+screen->upper_margin + screen->vsync_len;
	y_addr = par->smem_start + par->y_offset;
	uv_addr = par->cbr_start + par->c_offset;
	
	ScaleYrgbX = CalScale(xact, par->xsize);
	ScaleYrgbY = CalScale(yact, par->ysize);
	DBG("%s>>format:%d>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d>>xvir:%d>>yvir:%d>>ypos:%d>>y_addr:0x%x>>uv_addr:0x%x\n",
		__func__,par->format,xact,yact,par->xsize,par->ysize,xvir,yvir,ypos,y_addr,uv_addr);
	switch (par->format)
 	{
		case YUV422:// yuv422
			ScaleCbrX =	CalScale((xact/2), par->xsize);
			ScaleCbrY = CalScale(yact, par->ysize);
			break;
		case YUV420: // yuv420
			ScaleCbrX = CalScale(xact/2, par->xsize);
			ScaleCbrY =	CalScale(yact/2, par->ysize);
			break;
		case YUV444:// yuv444
			ScaleCbrX = CalScale(xact, par->xsize);
			ScaleCbrY = CalScale(yact, par->ysize);
			break;
		default:
			printk("%s>>un support format!\n",__func__);
			break;
	}

    LcdWrReg(lcdc_dev, WIN1_SCL_FACTOR_YRGB, v_X_SCL_FACTOR(ScaleYrgbX) | v_Y_SCL_FACTOR(ScaleYrgbY));
    LcdWrReg(lcdc_dev, WIN1_SCL_FACTOR_CBR,  v_X_SCL_FACTOR(ScaleCbrX) | v_Y_SCL_FACTOR(ScaleCbrY));
    LcdMskReg(lcdc_dev, SYS_CTRL1, m_W1_EN|m_W1_FORMAT, v_W1_EN(1)|v_W1_FORMAT(par->format));
    LcdWrReg(lcdc_dev, WIN1_YRGB_MST, y_addr);
    LcdWrReg(lcdc_dev,WIN1_CBR_MST,uv_addr);
    LcdWrReg(lcdc_dev, WIN1_ACT_INFO,v_ACT_WIDTH(xact) | v_ACT_HEIGHT(yact));
    LcdWrReg(lcdc_dev, WIN1_DSP_ST,v_DSP_STX(xpos) | v_DSP_STY(ypos));
    LcdWrReg(lcdc_dev, WIN1_DSP_INFO,v_DSP_WIDTH(par->xsize) | v_DSP_HEIGHT(par->ysize));
     // enable win1 color key and set the color to black(rgb=0)
    LcdMskReg(lcdc_dev, WIN1_COLOR_KEY_CTRL, m_COLORKEY_EN | m_KEYCOLOR, v_COLORKEY_EN(1) | v_KEYCOLOR(0));
	switch(par->format)
    {
        case ARGB888:
            LcdWrReg(lcdc_dev, WIN1_VIR,v_ARGB888_VIRWIDTH(xvir));
            LcdMskReg(lcdc_dev,SYS_CTRL1,m_W1_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(1));
            break;
        case RGB888:  //rgb888
            LcdWrReg(lcdc_dev, WIN1_VIR,v_RGB888_VIRWIDTH(xvir));
            LcdMskReg(lcdc_dev,SYS_CTRL1,m_W1_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(1));
            break;
        case RGB565:  //rgb565
            LcdWrReg(lcdc_dev, WIN1_VIR,v_RGB565_VIRWIDTH(xvir));
            break;
        case YUV422:
        case YUV420:   
            LcdWrReg(lcdc_dev, WIN1_VIR,v_YUV_VIRWIDTH(xvir));
            break;
        default:
            LcdWrReg(lcdc_dev, WIN1_VIR,v_RGB888_VIRWIDTH(xvir));
            break;
    }
    
	LcdWrReg(lcdc_dev, REG_CFG_DONE, 0x01);

    return 0;
}

static int rk30_lcdc_set_par(struct rk_lcdc_device_driver *fb_drv,int layer_id)
{
	struct rk30_lcdc_device *lcdc_dev=NULL;
	struct layer_par *par = &fb_drv->layer_par[0];
	rk_screen *screen = &fb_drv->screen;
 	struct lcdc_info * info = platform_get_drvdata(g_lcdc_pdev);
	if(!strcmp(fb_drv->name,"lcdc0"))
	{
		lcdc_dev =  &(info->lcdc0); 
	}
	else if(!strcmp(fb_drv->name,"lcdc1"))
	{
		lcdc_dev = &(info->lcdc1);
	}
	
	if(!screen)
	{
		printk(KERN_ERR "screen is null!\n");
	}
	if(layer_id==0)
	{
        win0_set_par(lcdc_dev,screen,par);
	}
	else if(layer_id==1)
	{
        win1_set_par(lcdc_dev,screen,&(fb_drv->layer_par[1]));
	}
	
	return 0;
}
 
int rk30_lcdc_pan(struct rk_lcdc_device_driver * dev_drv,int layer_id)
{
	
    return 0;
}

int rk30_lcdc_ioctl(unsigned int cmd, unsigned long arg,struct layer_par *layer_par)
{
   return 0;
}

int rk30_lcdc_suspend(struct layer_par *layer_par)
{
    return 0;
}


int rk30_lcdc_resume(struct layer_par *layer_par)
{  
    
    return 0;
}

static struct layer_par lcdc0_layer[] = {
	[0] = {
       	.name  		= "win0",
		.id			= 0,
		.support_3d	= true,
	},
	[1] = {
        .name  		= "win1",
		.id			= 1,
		.support_3d	= false,
	},
};
static struct layer_par lcdc1_layer[] = {
	[0] = {
       	.name  		= "win0",
		.id			= 0,
		.support_3d	= true,
	},
	[1] = {
        .name  		= "win1",
		.id			= 1,
		.support_3d	= false,
	},
};

static struct rk_lcdc_device_driver lcdc0_driver = {
	.name			= "lcdc0",
	.layer_par		= lcdc0_layer,
	.num_layer		= ARRAY_SIZE(lcdc0_layer),
	.ioctl			= rk30_lcdc_ioctl,
	.suspend		= rk30_lcdc_suspend,
	.resume			= rk30_lcdc_resume,
	.set_par       = rk30_lcdc_set_par,
	.blank         = rk30_lcdc_blank,
	.pan           = rk30_lcdc_pan,
};
static struct rk_lcdc_device_driver lcdc1_driver = {
	.name			= "lcdc1",
	.layer_par		= lcdc1_layer,
	.num_layer		= ARRAY_SIZE(lcdc1_layer),
	.ioctl			= rk30_lcdc_ioctl,
	.suspend		= rk30_lcdc_suspend,
	.resume			= rk30_lcdc_resume,
	.set_par       = rk30_lcdc_set_par,
	.blank         = rk30_lcdc_blank,
	.pan           = rk30_lcdc_pan,
};

static int __devinit rk30_lcdc_probe (struct platform_device *pdev)
{
	struct lcdc_info *inf =	NULL;
	struct resource *res = NULL;
	struct resource *mem;
	int ret = 0;
	
	g_lcdc_pdev = pdev; //set g_pdev

	/*************Malloc rk30lcdc_inf and set it to pdev for drvdata**********/
	inf = kmalloc(sizeof(struct lcdc_info), GFP_KERNEL);
    if(!inf)
    {
        dev_err(&pdev->dev, ">>rk30 lcdc inf kmalloc fail!");
        ret = -ENOMEM;
    }
	memset(inf, 0, sizeof(struct lcdc_info));
	platform_set_drvdata(pdev, inf);

	/****************	get lcdc0 reg  		*************************/
	 res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lcdc0 reg");
    if (res == NULL)
    {
        dev_err(&pdev->dev, "failed to get memory registers\n");
        ret = -ENOENT;
		//goto release_drvdata;
    }
    inf->lcdc0.reg_phy_base = res->start;
    inf->lcdc0.len = (res->end - res->start) + 1;
    mem = request_mem_region(inf->lcdc0.reg_phy_base, inf->lcdc0.len, pdev->name);
    if (mem == NULL)
    {
        dev_err(&pdev->dev, "failed to get memory region\n");
        ret = -ENOENT;
		//goto release_drvdata;
    }
    inf->lcdc0.reg_vir_base = ioremap(inf->lcdc0.reg_phy_base, inf->lcdc0.len);
    if (inf->lcdc0.reg_vir_base == NULL)
    {
        dev_err(&pdev->dev, "ioremap() of registers failed\n");
        ret = -ENXIO;
		//goto release_drvdata;
    }
    inf->lcdc0.preg = (LCDC_REG*)inf->lcdc0.reg_vir_base;
	printk("lcdc0 reg_phy_base = 0x%08x,reg_vir_base:0x%p\n", inf->lcdc0.reg_phy_base, inf->lcdc0.preg);
	/****************	get lcdc1 reg  		*************************/
	 res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lcdc1 reg");
    if (res == NULL)
    {
        dev_err(&pdev->dev, "failed to get memory registers\n");
        ret = -ENOENT;
		//goto release_drvdata;
    }
    inf->lcdc1.reg_phy_base = res->start;
    inf->lcdc1.len = (res->end - res->start) + 1;
    mem = request_mem_region(inf->lcdc1.reg_phy_base, inf->lcdc1.len, pdev->name);
    if (mem == NULL)
    {
        dev_err(&pdev->dev, "failed to get memory region\n");
        ret = -ENOENT;
		//goto release_drvdata;
    }
    inf->lcdc1.reg_vir_base = ioremap(inf->lcdc1.reg_phy_base, inf->lcdc1.len);
    if (inf->lcdc1.reg_vir_base == NULL)
    {
        dev_err(&pdev->dev, "ioremap() of registers failed\n");
        ret = -ENXIO;
		//goto release_drvdata;
    }
    inf->lcdc1.preg = (LCDC_REG*)inf->lcdc1.reg_vir_base;
	printk("lcdc1 reg_phy_base = 0x%08x,reg_vir_base:0x%p\n", inf->lcdc1.reg_phy_base, inf->lcdc1.preg);
	/*****************	 LCDC driver		********/
	inf->lcdc0.driver = &lcdc0_driver;
	inf->lcdc0.driver->dev=&pdev->dev;
	inf->lcdc1.driver = &lcdc1_driver;
	inf->lcdc1.driver->dev=&pdev->dev;
	/*****************	set lcdc screen	********/
	set_lcd_info(&inf->lcdc0.driver->screen, NULL);
	set_lcd_info(&inf->lcdc1.driver->screen, NULL);
	/*****************	INIT LCDC		********/
	init_rk30_lcdc(inf);
	rk30_load_screen(&inf->lcdc0,1);
	//rk30_load_screen(&inf->lcdc1,1);
	/*****************	lcdc register		********/
	rk_fb_register(&lcdc0_driver);
	//rk_fb_register(&lcdc1_driver);

	
	printk("rk30 lcdc probe ok!\n");
	return 0;
    
}
static int __devexit rk30_lcdc_remove(struct platform_device *pdev)
{
	return 0;
}

static void rk30_lcdc_shutdown(struct platform_device *pdev)
{

}


static struct platform_driver rk30lcdc_driver = {
	.probe		= rk30_lcdc_probe,
	.remove		= __devexit_p(rk30_lcdc_remove),
	.driver		= {
		.name	= "rk30-lcdc",
		.owner	= THIS_MODULE,
	},
	.shutdown   = rk30_lcdc_shutdown,
};

static int __init rk30_lcdc_init(void)
{
	//wake_lock_init(&idlelock, WAKE_LOCK_IDLE, "fb");
    return platform_driver_register(&rk30lcdc_driver);
}

static void __exit rk30_lcdc_exit(void)
{
    platform_driver_unregister(&rk30lcdc_driver);
}



fs_initcall(rk30_lcdc_init);
module_exit(rk30_lcdc_exit);



