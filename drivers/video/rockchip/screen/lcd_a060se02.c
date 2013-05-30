#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk29_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include "screen.h"

/* Base */
#define OUT_TYPE		SCREEN_MCU
#define OUT_FACE		OUT_P16BPP4

#define LCDC_ACLK       150000000     //29 lcdc axi DMA 频率

/* Timing */
#define H_PW			1
#define H_BP			1
#define H_VD			600
#define H_FP			5

#define V_PW			1
#define V_BP			1
#define V_VD			800
#define V_FP			1

#define P_WR            200

/* Other */
#define DCLK_POL		0
#define SWAP_RB			0

#define LCD_WIDTH       600    //need modify
#define LCD_HEIGHT      800


int lcd_init(void)
{
    mcu_ioctl(MCU_SETBYPASS, 1);

    // init set
    mcu_ioctl(MCU_WRCMD, 0x0000);
    mcu_ioctl(MCU_WRDATA, 0x0001);

    // start display
    mcu_ioctl(MCU_WRCMD, 0x1001);
    mcu_ioctl(MCU_WRDATA, 0x0001);
    mcu_ioctl(MCU_WRDATA, 0x0001);
    mcu_ioctl(MCU_WRDATA, 0x0320);
    mcu_ioctl(MCU_WRDATA, 0x0258);

    // 初始化图象
    int i=0, j=0;
    while(0)
    {
        mcu_ioctl(MCU_WRCMD, 0x1001);
        mcu_ioctl(MCU_WRDATA, 0x0001);
        mcu_ioctl(MCU_WRDATA, 0x0001);
        mcu_ioctl(MCU_WRDATA, 0x0320);
        mcu_ioctl(MCU_WRDATA, 0x0258);

        for(i=0; i<800*100; i++)
            mcu_ioctl(MCU_WRDATA, j%2 ? 0xffff : 0x0000);
        for(i=0; i<800*100; i++)
            mcu_ioctl(MCU_WRDATA, j%2 ? 0x0000 : 0xffff);
        j++;

        mcu_ioctl(MCU_WRCMD, 0x1002);
        msleep(2000);
        printk(">>>>>> lcd_init : send test image! \n");
    }

    mcu_ioctl(MCU_SETBYPASS, 0);
    return 0;
}

int lcd_standby(u8 enable)
{
    return 0;
}

int lcd_refresh(u8 arg)
{
    mcu_ioctl(MCU_SETBYPASS, 1);

    switch(arg)
    {
    case REFRESH_PRE:   //DMA传送前准备
    #if 0
        mcu_ioctl(MCU_WRCMD, 0x1001);
        mcu_ioctl(MCU_WRDATA, 0x0001);
        mcu_ioctl(MCU_WRDATA, 0x0001);
        mcu_ioctl(MCU_WRDATA, 0x0320);
        mcu_ioctl(MCU_WRDATA, 0x0258);
        printk(">>>>>> lcd_refresh : REFRESH_PRE! \n");
    #else
        // init set
        mcu_ioctl(MCU_WRCMD, 0x0000);
        mcu_ioctl(MCU_WRDATA, 0x0001);
        // start display
        mcu_ioctl(MCU_WRCMD, 0x1001);
        mcu_ioctl(MCU_WRDATA, 0x0001);
        mcu_ioctl(MCU_WRDATA, 0x0001);
        mcu_ioctl(MCU_WRDATA, 0x0320);
        mcu_ioctl(MCU_WRDATA, 0x0258);
        printk(">>>>>> lcd_refresh : REFRESH_PRE!!! \n");
    #endif
        break;

    case REFRESH_END:   //DMA传送结束后
        mcu_ioctl(MCU_WRCMD, 0x1002);
        printk(">>>>>> lcd_refresh : REFRESH_END! \n");
        break;

    default:
        break;
    }

    mcu_ioctl(MCU_SETBYPASS, 0);

    return 0;
}



void set_lcd_info(struct rk29fb_screen *screen)
{
    /* screen type & face */
    screen->type = OUT_TYPE;
    screen->face = OUT_FACE;

    /* Screen size */
    screen->x_res = H_VD;
    screen->y_res = V_VD;

    screen->width = LCD_WIDTH;
    screen->height = LCD_HEIGHT;

    /* Timing */
    screen->lcdc_aclk = LCDC_ACLK;
	screen->left_margin = H_BP;
	screen->right_margin = H_FP;
	screen->hsync_len = H_PW;
	screen->upper_margin = V_BP;
	screen->lower_margin = V_FP;
	screen->vsync_len = V_PW;
	screen->mcu_wrperiod = P_WR;

	/* Pin polarity */
	screen->pin_hsync = 0;
	screen->pin_vsync = 0;
	screen->pin_den = 0;
	screen->pin_dclk = DCLK_POL;

	/* Swap rule */
    screen->swap_rb = SWAP_RB;
    screen->swap_rg = 0;
    screen->swap_gb = 0;
    screen->swap_delta = 0;
    screen->swap_dumy = 0;

    /* Operation function*/
    screen->init = lcd_init;
    screen->standby = lcd_standby;
    screen->refresh = lcd_refresh;
}




