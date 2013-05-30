#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk29_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include "screen.h"

/* Base */
#define OUT_TYPE		SCREEN_MCU
#define OUT_FACE		OUT_P888

/* Timing */
#define H_PW			1
#define H_BP		    1
#define H_VD			480
#define H_FP			5

#define V_PW			1
#define V_BP			1
#define V_VD			800
#define V_FP			1

#define LCD_WIDTH       480    //need modify
#define LCD_HEIGHT      800

#define LCDC_ACLK       150000000     //29 lcdc axi DMA 频率

#define P_WR            27
#define USE_FMARK       0 //2               //是否使用FMK (0:不支持 1:横屏支持 2:横竖屏都支持)
#define FRMRATE         60              //MCU屏的刷新率 (FMK有效时用)


/* Other */
#define DCLK_POL		0
#define SWAP_RB			0

void Set_LCD_8B_REG(unsigned char regh,unsigned char regl, u32 data)
{
    u32 cmd;
	cmd = (regh<<8) + regl;
	if(-1==data) {
	    mcu_ioctl(MCU_WRCMD,cmd);
	} else {
	    mcu_ioctl(MCU_WRCMD,cmd);
	    mcu_ioctl(MCU_WRDATA,data);
    }
}

int lcd_init(void)
{
    int i = 0;

#if 0
    GPIO_SetPinDirection(reset_pin, GPIO_OUT);
    GPIO_SetPinLevel(reset_pin,GPIO_HIGH);
    DelayMs_nops(100);
    GPIO_SetPinLevel(reset_pin,GPIO_LOW);
    DelayMs_nops(100);
    GPIO_SetPinLevel(reset_pin,GPIO_HIGH);
#endif

    mcu_ioctl(MCU_SETBYPASS, 1);



    Set_LCD_8B_REG(0xC0,0X00,0x86);

    Set_LCD_8B_REG(0xC0,0X01,0x00);
    Set_LCD_8B_REG(0xC0,0X02,0x86);
    Set_LCD_8B_REG(0xC0,0X03,0x00);

    Set_LCD_8B_REG(0xC1,0X00,0x60); //0x004f
    Set_LCD_8B_REG(0xC2,0X00,0x21);
    Set_LCD_8B_REG(0xC2,0X02,0x70); //0x0202

    Set_LCD_8B_REG(0xB6,0x00,0x10); //0x0030
    Set_LCD_8B_REG(0xB6,0x02,0x30);

    Set_LCD_8B_REG(0xC7,0X00,0x6F);

    Set_LCD_8B_REG(0xE0,0X00,0X0E);
    Set_LCD_8B_REG(0xE0,0X01,0X14);
    Set_LCD_8B_REG(0xE0,0X02,0X29);
    Set_LCD_8B_REG(0xE0,0X03,0X3A);
    Set_LCD_8B_REG(0xE0,0X04,0X1D);
    Set_LCD_8B_REG(0xE0,0X05,0X30);
    Set_LCD_8B_REG(0xE0,0X06,0X61);
    Set_LCD_8B_REG(0xE0,0X07,0X3D);
    Set_LCD_8B_REG(0xE0,0X08,0X22);
    Set_LCD_8B_REG(0xE0,0X09,0X2A);
    Set_LCD_8B_REG(0xE0,0X0A,0X87);
    Set_LCD_8B_REG(0xE0,0X0B,0X16);
    Set_LCD_8B_REG(0xE0,0X0C,0X3B);
    Set_LCD_8B_REG(0xE0,0X0D,0X4C);
    Set_LCD_8B_REG(0xE0,0X0E,0X78);
    Set_LCD_8B_REG(0xE0,0X0F,0X96);
    Set_LCD_8B_REG(0xE0,0X10,0X4A);
    Set_LCD_8B_REG(0xE0,0X11,0X4D);

    Set_LCD_8B_REG(0xE1,0X00,0X0E);
    Set_LCD_8B_REG(0xE1,0X01,0X14);
    Set_LCD_8B_REG(0xE1,0X02,0X29);
    Set_LCD_8B_REG(0xE1,0X03,0X3A);
    Set_LCD_8B_REG(0xE1,0X04,0X1D);
    Set_LCD_8B_REG(0xE1,0X05,0X30);
    Set_LCD_8B_REG(0xE1,0X06,0X61);
    Set_LCD_8B_REG(0xE1,0X07,0X3F);
    Set_LCD_8B_REG(0xE1,0X08,0X20);
    Set_LCD_8B_REG(0xE1,0X09,0X26);
    Set_LCD_8B_REG(0xE1,0X0A,0X83);
    Set_LCD_8B_REG(0xE1,0X0B,0X16);
    Set_LCD_8B_REG(0xE1,0X0C,0X3B);
    Set_LCD_8B_REG(0xE1,0X0D,0X4C);
    Set_LCD_8B_REG(0xE1,0X0E,0X78);
    Set_LCD_8B_REG(0xE1,0X0F,0X96);
    Set_LCD_8B_REG(0xE1,0X10,0X4A);
    Set_LCD_8B_REG(0xE1,0X11,0X4D);

    Set_LCD_8B_REG(0xE2,0X00,0X0E);
    Set_LCD_8B_REG(0xE2,0X01,0X14);
    Set_LCD_8B_REG(0xE2,0X02,0X29);
    Set_LCD_8B_REG(0xE2,0X03,0X3A);
    Set_LCD_8B_REG(0xE2,0X04,0X1D);
    Set_LCD_8B_REG(0xE2,0X05,0X30);
    Set_LCD_8B_REG(0xE2,0X06,0X61);
    Set_LCD_8B_REG(0xE2,0X07,0X3D);
    Set_LCD_8B_REG(0xE2,0X08,0X22);
    Set_LCD_8B_REG(0xE2,0X09,0X2A);
    Set_LCD_8B_REG(0xE2,0X0A,0X87);
    Set_LCD_8B_REG(0xE2,0X0B,0X16);
    Set_LCD_8B_REG(0xE2,0X0C,0X3B);
    Set_LCD_8B_REG(0xE2,0X0D,0X4C);
    Set_LCD_8B_REG(0xE2,0X0E,0X78);
    Set_LCD_8B_REG(0xE2,0X0F,0X96);
    Set_LCD_8B_REG(0xE2,0X10,0X4A);
    Set_LCD_8B_REG(0xE2,0X11,0X4D);

    Set_LCD_8B_REG(0xE3,0X00,0X0E);
    Set_LCD_8B_REG(0xE3,0X01,0X14);
    Set_LCD_8B_REG(0xE3,0X02,0X29);
    Set_LCD_8B_REG(0xE3,0X03,0X3A);
    Set_LCD_8B_REG(0xE3,0X04,0X1D);
    Set_LCD_8B_REG(0xE3,0X05,0X30);
    Set_LCD_8B_REG(0xE3,0X06,0X61);
    Set_LCD_8B_REG(0xE3,0X07,0X3F);
    Set_LCD_8B_REG(0xE3,0X08,0X20);
    Set_LCD_8B_REG(0xE3,0X09,0X26);
    Set_LCD_8B_REG(0xE3,0X0A,0X83);
    Set_LCD_8B_REG(0xE3,0X0B,0X16);
    Set_LCD_8B_REG(0xE3,0X0C,0X3B);
    Set_LCD_8B_REG(0xE3,0X0D,0X4C);
    Set_LCD_8B_REG(0xE3,0X0E,0X78);
    Set_LCD_8B_REG(0xE3,0X0F,0X96);
    Set_LCD_8B_REG(0xE3,0X10,0X4A);
    Set_LCD_8B_REG(0xE3,0X11,0X4D);

    Set_LCD_8B_REG(0xE4,0X00,0X0E);
    Set_LCD_8B_REG(0xE4,0X01,0X14);
    Set_LCD_8B_REG(0xE4,0X02,0X29);
    Set_LCD_8B_REG(0xE4,0X03,0X3A);
    Set_LCD_8B_REG(0xE4,0X04,0X1D);
    Set_LCD_8B_REG(0xE4,0X05,0X30);
    Set_LCD_8B_REG(0xE4,0X06,0X61);
    Set_LCD_8B_REG(0xE4,0X07,0X3D);
    Set_LCD_8B_REG(0xE4,0X08,0X22);
    Set_LCD_8B_REG(0xE4,0X09,0X2A);
    Set_LCD_8B_REG(0xE4,0X0A,0X87);
    Set_LCD_8B_REG(0xE4,0X0B,0X16);
    Set_LCD_8B_REG(0xE4,0X0C,0X3B);
    Set_LCD_8B_REG(0xE4,0X0D,0X4C);
    Set_LCD_8B_REG(0xE4,0X0E,0X78);
    Set_LCD_8B_REG(0xE4,0X0F,0X96);
    Set_LCD_8B_REG(0xE4,0X10,0X4A);
    Set_LCD_8B_REG(0xE4,0X11,0X4D);

    Set_LCD_8B_REG(0xE5,0X00,0X0E);
    Set_LCD_8B_REG(0xE5,0X01,0X14);
    Set_LCD_8B_REG(0xE5,0X02,0X29);
    Set_LCD_8B_REG(0xE5,0X03,0X3A);
    Set_LCD_8B_REG(0xE5,0X04,0X1D);
    Set_LCD_8B_REG(0xE5,0X05,0X30);
    Set_LCD_8B_REG(0xE5,0X06,0X61);
    Set_LCD_8B_REG(0xE5,0X07,0X3F);
    Set_LCD_8B_REG(0xE5,0X08,0X20);
    Set_LCD_8B_REG(0xE5,0X09,0X26);
    Set_LCD_8B_REG(0xE5,0X0A,0X83);
    Set_LCD_8B_REG(0xE5,0X0B,0X16);
    Set_LCD_8B_REG(0xE5,0X0C,0X3B);
    Set_LCD_8B_REG(0xE5,0X0D,0X4C);
    Set_LCD_8B_REG(0xE5,0X0E,0X78);
    Set_LCD_8B_REG(0xE5,0X0F,0X96);
    Set_LCD_8B_REG(0xE5,0X10,0X4A);
    Set_LCD_8B_REG(0xE5,0X11,0X4D);

    Set_LCD_8B_REG(0x36,0X01,0X01);

    Set_LCD_8B_REG(0x11,0X00,0X00);
    msleep(100);
    Set_LCD_8B_REG(0x29,0X00,0X00);
    msleep(100);


    Set_LCD_8B_REG(0x2a,0X00,0X00);
    Set_LCD_8B_REG(0x2a,0X01,0X00);
    Set_LCD_8B_REG(0x2a,0X02,0X01);
    Set_LCD_8B_REG(0x2a,0X03,0Xdf);
    msleep(100);
    Set_LCD_8B_REG(0x2b,0X00,0X00);
    Set_LCD_8B_REG(0x2b,0X01,0X00);
    Set_LCD_8B_REG(0x2b,0X02,0X03);
    Set_LCD_8B_REG(0x2b,0X03,0X1f);
    msleep(100);
    {
        u32 fte = 0;
        Set_LCD_8B_REG(0x44,0x00,(fte>>8)&0xff);
        Set_LCD_8B_REG(0x44,0x01,(fte)&0xff);
    }
    Set_LCD_8B_REG(0x0E,0X00,0X80);
    Set_LCD_8B_REG(0x35,0X00,0X80);

#if (480==H_VD)
    Set_LCD_8B_REG(0x36,0X00,0x00);
#else
    Set_LCD_8B_REG(0x36,0X00,0x22);
#endif
    Set_LCD_8B_REG(0x2c,0X00,-1);

    for(i=0; i<480*800; i++) {
        mcu_ioctl(MCU_WRDATA, 0x00000000);
    }

#if 0
    // for test
    while(1) {
        int i = 0;
        for(i=0; i<400*480; i++)
            mcu_ioctl(MCU_WRDATA, 0xffffffff);
        for(i=0; i<400*480; i++)
            mcu_ioctl(MCU_WRDATA, 0x00000000);
        msleep(1000);
        printk(">>>>> MCU_WRDATA ...\n");

        for(i=0; i<400*480; i++)
            mcu_ioctl(MCU_WRDATA, 0x00000000);
        for(i=0; i<400*480; i++)
            mcu_ioctl(MCU_WRDATA, 0xffffffff);
        msleep(1000);
        printk(">>>>> MCU_WRDATA ...\n");
    }
#endif

    mcu_ioctl(MCU_SETBYPASS, 0);
    return 0;
}


int lcd_standby(u8 enable)
{
    mcu_ioctl(MCU_SETBYPASS, 1);
    if(enable) {
        Set_LCD_8B_REG(0x10,0X00,-1);
    } else {
        Set_LCD_8B_REG(0x11,0X00,-1);
    }
    mcu_ioctl(MCU_SETBYPASS, 0);
    return 0;
}


int lcd_refresh(u8 arg)
{
    switch(arg)
    {
    case REFRESH_PRE:   //DMA传送前准备
        mcu_ioctl(MCU_SETBYPASS, 1);
        Set_LCD_8B_REG(0x2c,0X00,-1);
        mcu_ioctl(MCU_SETBYPASS, 0);
        break;

    case REFRESH_END:   //DMA传送结束后
        mcu_ioctl(MCU_SETBYPASS, 1);
        Set_LCD_8B_REG(0x29,0X00,-1);
        mcu_ioctl(MCU_SETBYPASS, 0);
        break;

    default:
        break;
    }

    return 0;
}


int lcd_scandir(u16 dir)
{
    mcu_ioctl(MCU_SETBYPASS, 1);

    // 暂时关闭MCU显示,在lcd_refresh的case REFRESH_END再打开
    // 否则画面会异常
    Set_LCD_8B_REG(0x28,0X00,-1);

    Set_LCD_8B_REG(0x2a,0X00,0X00);
    Set_LCD_8B_REG(0x2a,0X01,0X00);
    Set_LCD_8B_REG(0x2a,0X02,0X01);
    Set_LCD_8B_REG(0x2a,0X03,0Xdf);
    Set_LCD_8B_REG(0x2b,0X00,0X00);
    Set_LCD_8B_REG(0x2b,0X01,0X00);
    Set_LCD_8B_REG(0x2b,0X02,0X03);
    Set_LCD_8B_REG(0x2b,0X03,0X1f);

    switch(dir)
    {
    case 0:
        Set_LCD_8B_REG(0x36,0X00,0x00);
        break;
    case 90:
        Set_LCD_8B_REG(0x36,0X00,0x22);
        break;
    case 180:
        Set_LCD_8B_REG(0x36,0X00,0x03);
        break;
    case 270:
        Set_LCD_8B_REG(0x36,0X00,0x21);
        break;
    default:
        break;
    }

    mcu_ioctl(MCU_SETBYPASS, 0);
    return 0;
}


int lcd_disparea(u8 area)
{
    u32 x0, y0, x1, y1, fte;

	mcu_ioctl(MCU_SETBYPASS, 1);

    switch(area)
    {
    case 0:
        fte = 0;
        x0 = 0;
        y0 = 0;
        x1 = 399;
        y1 = 479;
        break;

    case 2:
        x0 = 0;
        y0 = 0;
        x1 = 799;
        y1 = 479;
        break;

    case 1:
    default:
        fte = 400;
        x0 = 400;
        y0 = 0;
        x1 = 799;
        y1 = 479;
        break;
    }

    //Set_LCD_8B_REG(0x44,0x00,(fte>>8)&0xff);
    //Set_LCD_8B_REG(0x44,0x01,(fte)&0xff);
    Set_LCD_8B_REG(0x2a,0X00,(y0>>8)&0xff);
	Set_LCD_8B_REG(0x2a,0X01,y0&0xff);
	Set_LCD_8B_REG(0x2a,0X02,(y1>>8)&0xff);
	Set_LCD_8B_REG(0x2a,0X03,y1&0xff);

	Set_LCD_8B_REG(0x2b,0X00,(x0>>8)&0xff);
	Set_LCD_8B_REG(0x2b,0X01,x0&0xff);
	Set_LCD_8B_REG(0x2b,0X02,(x1>>8)&0xff);
	Set_LCD_8B_REG(0x2b,0X03,x1&0xff);
    Set_LCD_8B_REG(0x2c,0X00,-1);

	mcu_ioctl(MCU_SETBYPASS, 0);

    return (0);

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
	screen->mcu_usefmk = USE_FMARK;
    screen->mcu_frmrate = FRMRATE;

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
    screen->scandir = lcd_scandir;
    screen->refresh = lcd_refresh;
    screen->disparea = lcd_disparea;
}





