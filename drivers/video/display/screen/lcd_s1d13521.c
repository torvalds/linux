#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk29_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include "screen.h"

#include "s1d13521.h"
#include "s1d13521ioctl.h"

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

#define LCD_WIDTH       600    //need modify
#define LCD_HEIGHT      800

/* Other */
#define DCLK_POL		0
#define SWAP_RB			0



int s1d13521if_refresh(u8 arg);

#define GPIO_RESET_L	RK2818_PIN_PC0//reset pin
#define GPIO_HIRQ		RK2818_PIN_PC1	//IRQ
#define GPIO_HDC		RK2818_PIN_PC2	//Data(HIHG) or Command(LOW)
#define GPIO_HCS_L		RK2818_PIN_PC3	//Chip select
#define GPIO_HRD_L		RK2818_PIN_PC4	//Read mode, low active
#define GPIO_HWE_L		RK2818_PIN_PC5	//Write mode, low active
#define GPIO_HRDY		RK2818_PIN_PC6	//Bus ready
#define GPIO_RMODE		RK2818_PIN_PC7	//rmode ->CNF1


//----------------------------------------------------------------------------
// PRIVATE FUNCTION:
// Set registers to initial values
//----------------------------------------------------------------------------

int s1d13521if_wait_for_ready(void)
{
    int cnt = 1000;
    int d = 0;
    gpio_request(GPIO_HRDY, 0);
    d = gpio_get_value(GPIO_HRDY);

    while (d == 0)
    {
        mdelay(1);

        if (--cnt <= 0)         // Avoid endless loop
        {
            printk(">>>>>> wait_for_ready -- timeout! \n");
            return -1;
        }

        d = gpio_get_value(GPIO_HRDY);
    }
    gpio_free(GPIO_HRDY);
    return 0;
}


int s1d13521if_cmd(unsigned ioctlcmd,s1d13521_ioctl_cmd_params *params, int numparams)
{
    int i;
    s1d13521if_wait_for_ready();

    mcu_ioctl(MCU_WRCMD, ioctlcmd);
    for (i = 0; i < numparams; i++) {
        mcu_ioctl(MCU_WRDATA, params->param[i]);
    }

    return 0;
}

void s1d13521if_WriteReg16(u16 Index, u16 Value)
{
    s1d13521if_wait_for_ready();
    mcu_ioctl(MCU_WRCMD, WR_REG);
    mcu_ioctl(MCU_WRDATA, Index);
    mcu_ioctl(MCU_WRDATA, Value);
}


void s1d13521fb_InitRegisters(void)
{
    unsigned i, cmd,j,numparams;
    s1d13521_ioctl_cmd_params cmd_params;
    S1D_INSTANTIATE_REGISTERS(static,InitCmdArray);

    i = 0;

    while (i < sizeof(InitCmdArray)/sizeof(InitCmdArray[0]))
    {
        cmd = InitCmdArray[i++];
        numparams = InitCmdArray[i++];

        for (j = 0; j < numparams; j++)
                cmd_params.param[j] = InitCmdArray[i++];

        s1d13521if_cmd(cmd,&cmd_params,numparams);
    }
}

void s1d13521if_init_gpio(void)
{
    int i;
    int ret=0;

	rk29_mux_api_set(GPIOC_LCDC18BIT_SEL_NAME, IOMUXB_GPIO0_C01);
	rk29_mux_api_set(GPIOC_LCDC24BIT_SEL_NAME, IOMUXB_GPIO0_C2_7);

	for(i = 0; i < 8; i++)
    {
		if(i == 1 || i == 6)//HIRQ, HRDY
        {
    		ret = gpio_request(GPIO_RESET_L+i, NULL);
            if(ret != 0)
            {
                gpio_free(GPIO_RESET_L+i);
                printk(">>>>>> lcd cs gpio_request err \n ");
            }
            gpio_direction_input(GPIO_RESET_L+i);
            gpio_free(GPIO_RESET_L+i);
		}
        else  //RESET_L, HD/C, HCS_L, HRD_L, HWE_L, RMODE
		{
            ret = gpio_request(GPIO_RESET_L+i, NULL);
            if(ret != 0)
            {
                gpio_free(GPIO_RESET_L+i);
                printk(">>>>>> lcd cs gpio_request err \n ");
            }
            gpio_direction_output(GPIO_RESET_L+i, 0);
            gpio_set_value(GPIO_RESET_L+i, GPIO_HIGH);
            gpio_free(GPIO_RESET_L+i);
		}
	}
}

void s1d13521if_set_reset(void)
{
    gpio_request(GPIO_RMODE, 0);
	gpio_set_value(GPIO_RMODE, GPIO_HIGH);
    gpio_request(GPIO_RESET_L, 0);

    // reset pulse
    mdelay(10);
	gpio_set_value(GPIO_RESET_L, GPIO_LOW);
	mdelay(10);
	gpio_set_value(GPIO_RESET_L, GPIO_HIGH);
	mdelay(10);
    gpio_free(GPIO_RMODE);
    gpio_free(GPIO_RESET_L);

	//s1d13521if_WaitForHRDY();
}



int s1d13521if_init(void)
{
    int i=0;
    s1d13521_ioctl_cmd_params cmd_params;

    s1d13521if_init_gpio();
    s1d13521if_set_reset();

    mcu_ioctl(MCU_SETBYPASS, 1);

    s1d13521fb_InitRegisters();

#if 1
    s1d13521if_WriteReg16(0x330,0x84);              // LUT AutoSelect+P4N
    s1d13521if_cmd(WAIT_DSPE_TRG,&cmd_params,0);
    cmd_params.param[0] = (0x2 << 4);
    s1d13521if_cmd(LD_IMG,&cmd_params,1);
    cmd_params.param[0] = 0x154;
    s1d13521if_cmd(WR_REG,&cmd_params,1);

    for(i=0; i<600*200; i++) {
        mcu_ioctl(MCU_WRDATA, 0xffff);
    }

    s1d13521if_cmd(LD_IMG_END,&cmd_params,0);
    cmd_params.param[0] = ((WF_MODE_INIT<<8) |0x4000);
    s1d13521if_cmd(UPD_FULL,&cmd_params,1);         // update all pixels
    s1d13521if_cmd(WAIT_DSPE_TRG,&cmd_params,0);
    s1d13521if_cmd(WAIT_DSPE_FREND,&cmd_params,0);
#endif

    mcu_ioctl(MCU_SETBYPASS, 0);


#if 0
    // 初始化图象
    mcu_ioctl(MCU_SETBYPASS, 1);
    while(1)
    {
        int i=0, j=0;
        // Copy virtual framebuffer to display framebuffer.

        unsigned mode = WF_MODE_GC;
        unsigned cmd = UPD_FULL;

//        unsigned reg330 = s1d13521if_ReadReg16(0x330);
        s1d13521if_WriteReg16(0x330,0x84);              // LUT AutoSelect + P4N
        // Copy virtual framebuffer to hardware via indirect interface burst mode write
        s1d13521if_cmd(WAIT_DSPE_TRG,&cmd_params,0);
        cmd_params.param[0] = (0x2<<4);
        s1d13521if_cmd(LD_IMG,&cmd_params,1);
        cmd_params.param[0] = 0x154;
        s1d13521if_cmd(WR_REG,&cmd_params,1);

        for(i=0; i<600*100; i++) {
            mcu_ioctl(MCU_WRDATA, 0xffff);
        }
        for(i=0; i<600*100; i++) {
            mcu_ioctl(MCU_WRDATA, 0x0000);
        }

        s1d13521if_cmd(LD_IMG_END,&cmd_params,0);
        cmd_params.param[0] = (mode<<8);
        s1d13521if_cmd(cmd,&cmd_params,1);              // update all pixels
        s1d13521if_cmd(WAIT_DSPE_TRG,&cmd_params,0);
        s1d13521if_cmd(WAIT_DSPE_FREND,&cmd_params,0);

        msleep(2000);
        printk(">>>>>> lcd_init : send test image! \n");
    }
    mcu_ioctl(MCU_SETBYPASS, 0);
#endif


    return 0;
}

int s1d13521if_standby(u8 enable)
{
    return 0;
}

int s1d13521if_refresh(u8 arg)
{
    mcu_ioctl(MCU_SETBYPASS, 1);

    switch(arg)
    {
    case REFRESH_PRE:   //DMA传送前准备
        {
            // Copy virtual framebuffer to display framebuffer.
            s1d13521_ioctl_cmd_params cmd_params;

    //        unsigned reg330 = s1d13521if_ReadReg16(0x330);
            s1d13521if_WriteReg16(0x330,0x84);              // LUT AutoSelect + P4N

            // Copy virtual framebuffer to hardware via indirect interface burst mode write
            s1d13521if_cmd(WAIT_DSPE_TRG,&cmd_params,0);
            cmd_params.param[0] = (0x2<<4);
            s1d13521if_cmd(LD_IMG,&cmd_params,1);
            cmd_params.param[0] = 0x154;
            s1d13521if_cmd(WR_REG,&cmd_params,1);
        }
        printk(">>>>>> lcd_refresh : REFRESH_PRE! \n");
        break;

    case REFRESH_END:   //DMA传送结束后
        {
            s1d13521_ioctl_cmd_params cmd_params;
            unsigned mode = WF_MODE_GC;
            unsigned cmd = UPD_FULL;

            s1d13521if_cmd(LD_IMG_END,&cmd_params,0);

            cmd_params.param[0] = (mode<<8);
            s1d13521if_cmd(cmd,&cmd_params,1);              // update all pixels

            s1d13521if_cmd(WAIT_DSPE_TRG,&cmd_params,0);
            s1d13521if_cmd(WAIT_DSPE_FREND,&cmd_params,0);
        }
        printk(">>>>>> lcd_refresh : REFRESH_END! \n");
        break;

    default:
        break;
    }

    mcu_ioctl(MCU_SETBYPASS, 0);

    return 0;
}



void set_lcd_info(struct rk28fb_screen *screen)
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
    screen->init = s1d13521if_init;
    screen->standby = s1d13521if_standby;
    screen->refresh = s1d13521if_refresh;
}




