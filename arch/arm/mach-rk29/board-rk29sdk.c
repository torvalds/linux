/* arch/arm/mach-rk29/board-rk29.c
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
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/mmc/host.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/hardware/gic.h>

#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/irqs.h>
#include <mach/rk29_iomap.h>
#include <mach/board.h>
#include <mach/rk29_nand.h>


#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include "devices.h"

extern struct sys_timer rk29_timer;

int rk29_nand_io_init(void)
{
    return 0;
}

struct rk29_nand_platform_data rk29_nand_data = {
    .width      = 1,     /* data bus width in bytes */
    .hw_ecc     = 1,     /* hw ecc 0: soft ecc */
    .num_flash    = 1,
    .io_init   = rk29_nand_io_init,
};

static struct rk29_gpio_bank rk29_gpiobankinit[] = {
	{
		.id		= RK29_ID_GPIO0,
		.offset	= RK29_GPIO0_BASE,
	},
	{
		.id		= RK29_ID_GPIO1,
		.offset	= RK29_GPIO1_BASE,
	}, 
	{
		.id		= RK29_ID_GPIO2,
		.offset	= RK29_GPIO2_BASE,
	}, 
	{
		.id		= RK29_ID_GPIO3,
		.offset	= RK29_GPIO3_BASE,
	}, 
	{
		.id		= RK29_ID_GPIO4,
		.offset	= RK29_GPIO4_BASE,
	}, 
	{
		.id		= RK29_ID_GPIO5,
		.offset	= RK29_GPIO5_BASE,
	}, 
	{
		.id		= RK29_ID_GPIO6,
		.offset	= RK29_GPIO6_BASE,
	},  	
};

/*****************************************************************************************
 * lcd  devices
 * author: zyw@rock-chips.com
 *****************************************************************************************/
//#ifdef  CONFIG_LCD_TD043MGEA1
#define LCD_TXD_PIN          RK29_PIN0_PA6   // 乱填,得修改
#define LCD_CLK_PIN          RK29_PIN0_PA7   // 乱填,得修改
#define LCD_CS_PIN           RK29_PIN0_PB6   // 乱填,得修改
#define LCD_TXD_MUX_NAME     GPIOE_U1IR_I2C1_NAME
#define LCD_CLK_MUX_NAME     NULL
#define LCD_CS_MUX_NAME      GPIOH6_IQ_SEL_NAME
#define LCD_TXD_MUX_MODE     0
#define LCD_CLK_MUX_MODE     0
#define LCD_CS_MUX_MODE      0
//#endif
static int rk29_lcd_io_init(void)
{
    int ret = 0;

#if 0
    rk29_mux_api_set(LCD_CS_MUX_NAME, LCD_CS_MUX_MODE);
    if (LCD_CS_PIN != INVALID_GPIO) {
        ret = gpio_request(LCD_CS_PIN, NULL);
        if(ret != 0)
        {
            goto err1;
            printk(">>>>>> lcd cs gpio_request err \n ");
        }
    }

    rk29_mux_api_set(LCD_CLK_MUX_NAME, LCD_CLK_MUX_MODE);
    if (LCD_CLK_PIN != INVALID_GPIO) {
        ret = gpio_request(LCD_CLK_PIN, NULL);
        if(ret != 0)
        {
            goto err2;
            printk(">>>>>> lcd clk gpio_request err \n ");
        }
    }

    rk29_mux_api_set(LCD_TXD_MUX_NAME, LCD_TXD_MUX_MODE);
    if (LCD_TXD_PIN != INVALID_GPIO) {
        ret = gpio_request(LCD_TXD_PIN, NULL);
        if(ret != 0)
        {
            goto err3;
            printk(">>>>>> lcd txd gpio_request err \n ");
        }
    }

    return 0;

err3:
    if (LCD_CLK_PIN != INVALID_GPIO) {
        gpio_free(LCD_CLK_PIN);
    }
err2:
    if (LCD_CS_PIN != INVALID_GPIO) {
        gpio_free(LCD_CS_PIN);
    }
err1:
#endif
    return ret;
}

static int rk29_lcd_io_deinit(void)
{
    int ret = 0;
#if 0
    gpio_direction_output(LCD_CLK_PIN, 0);
    gpio_set_value(LCD_CLK_PIN, GPIO_HIGH);
    gpio_direction_output(LCD_TXD_PIN, 0);
    gpio_set_value(LCD_TXD_PIN, GPIO_HIGH);

    gpio_free(LCD_CS_PIN);
    rk29_mux_api_mode_resume(LCD_CS_MUX_NAME);
    gpio_free(LCD_CLK_PIN);
    gpio_free(LCD_TXD_PIN);
    rk29_mux_api_mode_resume(LCD_TXD_MUX_NAME);
    rk29_mux_api_mode_resume(LCD_CLK_MUX_NAME);
#endif
    return ret;
}

struct rk29lcd_info rk29_lcd_info = {
    //.txd_pin  = LCD_TXD_PIN,
    //.clk_pin = LCD_CLK_PIN,
    //.cs_pin = LCD_CS_PIN,
    .io_init   = rk29_lcd_io_init,
    .io_deinit = rk29_lcd_io_deinit,
};


/*****************************************************************************************
 * frame buffe  devices
 * author: zyw@rock-chips.com
 *****************************************************************************************/

#define FB_ID                       0
#define FB_DISPLAY_ON_PIN           RK29_PIN0_PB1   // 乱填,得修改
#define FB_LCD_STANDBY_PIN          INVALID_GPIO
#define FB_MCU_FMK_PIN              INVALID_GPIO

#if 0
#define FB_DISPLAY_ON_VALUE         GPIO_LOW
#define FB_LCD_STANDBY_VALUE        0

#define FB_DISPLAY_ON_MUX_NAME      GPIOB1_SMCS1_MMC0PCA_NAME
#define FB_DISPLAY_ON_MUX_MODE      IOMUXA_GPIO0_B1

#define FB_LCD_STANDBY_MUX_NAME     NULL
#define FB_LCD_STANDBY_MUX_MODE     1

#define FB_MCU_FMK_PIN_MUX_NAME     NULL
#define FB_MCU_FMK_MUX_MODE         0

#define FB_DATA0_16_MUX_NAME       GPIOC_LCDC16BIT_SEL_NAME
#define FB_DATA0_16_MUX_MODE        1

#define FB_DATA17_18_MUX_NAME      GPIOC_LCDC18BIT_SEL_NAME
#define FB_DATA17_18_MUX_MODE       1

#define FB_DATA19_24_MUX_NAME      GPIOC_LCDC24BIT_SEL_NAME
#define FB_DATA19_24_MUX_MODE       1

#define FB_DEN_MUX_NAME            CXGPIO_LCDDEN_SEL_NAME
#define FB_DEN_MUX_MODE             1

#define FB_VSYNC_MUX_NAME          CXGPIO_LCDVSYNC_SEL_NAME
#define FB_VSYNC_MUX_MODE           1

#define FB_MCU_FMK_MUX_NAME        NULL
#define FB_MCU_FMK_MUX_MODE         0
#endif
static int rk29_fb_io_init(struct rk29_fb_setting_info *fb_setting)
{
    int ret = 0;
#if 0
    if(fb_setting->data_num <=16)
        rk29_mux_api_set(FB_DATA0_16_MUX_NAME, FB_DATA0_16_MUX_MODE);
    if(fb_setting->data_num >16 && fb_setting->data_num<=18)
        rk29_mux_api_set(FB_DATA17_18_MUX_NAME, FB_DATA17_18_MUX_MODE);
    if(fb_setting->data_num >18)
        rk29_mux_api_set(FB_DATA19_24_MUX_NAME, FB_DATA19_24_MUX_MODE);

    if(fb_setting->vsync_en)
        rk29_mux_api_set(FB_VSYNC_MUX_NAME, FB_VSYNC_MUX_MODE);

    if(fb_setting->den_en)
        rk29_mux_api_set(FB_DEN_MUX_NAME, FB_DEN_MUX_MODE);

    if(fb_setting->mcu_fmk_en && FB_MCU_FMK_MUX_NAME && (FB_MCU_FMK_PIN != INVALID_GPIO))
    {
        rk29_mux_api_set(FB_MCU_FMK_MUX_NAME, FB_MCU_FMK_MUX_MODE);
        ret = gpio_request(FB_MCU_FMK_PIN, NULL);
        if(ret != 0)
        {
            gpio_free(FB_MCU_FMK_PIN);
            printk(">>>>>> FB_MCU_FMK_PIN gpio_request err \n ");
        }
        gpio_direction_input(FB_MCU_FMK_PIN);
    }

    if(fb_setting->disp_on_en && FB_DISPLAY_ON_MUX_NAME && (FB_DISPLAY_ON_PIN != INVALID_GPIO))
    {
        rk29_mux_api_set(FB_DISPLAY_ON_MUX_NAME, FB_DISPLAY_ON_MUX_MODE);
        ret = gpio_request(FB_DISPLAY_ON_PIN, NULL);
        if(ret != 0)
        {
            gpio_free(FB_DISPLAY_ON_PIN);
            printk(">>>>>> FB_DISPLAY_ON_PIN gpio_request err \n ");
        }
    }

    if(fb_setting->disp_on_en && FB_LCD_STANDBY_MUX_NAME && (FB_LCD_STANDBY_PIN != INVALID_GPIO))
    {
        rk29_mux_api_set(FB_LCD_STANDBY_MUX_NAME, FB_LCD_STANDBY_MUX_MODE);
        ret = gpio_request(FB_LCD_STANDBY_PIN, NULL);
        if(ret != 0)
        {
            gpio_free(FB_LCD_STANDBY_PIN);
            printk(">>>>>> FB_LCD_STANDBY_PIN gpio_request err \n ");
        }
    }
#endif
    return ret;
}

struct rk29fb_info rk29_fb_info = {
    .fb_id   = FB_ID,
    //.disp_on_pin = FB_DISPLAY_ON_PIN,
    //.disp_on_value = FB_DISPLAY_ON_VALUE,
    //.standby_pin = FB_LCD_STANDBY_PIN,
    //.standby_value = FB_LCD_STANDBY_VALUE,
    //.mcu_fmk_pin = FB_MCU_FMK_PIN,
    .lcd_info = &rk29_lcd_info,
    .io_init   = rk29_fb_io_init,
};


static struct platform_device *devices[] __initdata = {
#ifdef CONFIG_UART1_RK29	
	&rk29_device_uart1,
#endif	
#ifdef CONFIG_MTD_NAND_RK29
	&rk29_device_nand,
#endif

#ifdef CONFIG_FB_RK29
    &rk29_device_fb,
#endif

#ifdef CONFIG_VIVANTE
	&rk29_device_gpu,
#endif

};

static void __init rk29_gic_init_irq(void)
{
	gic_dist_init(0, (void __iomem *)RK29_GICPERI_BASE, 32);
	gic_cpu_init(0, (void __iomem *)RK29_GICCPU_BASE);
}

static void __init machine_rk29_init_irq(void)
{
	rk29_gic_init_irq();
	rk29_gpio_init(rk29_gpiobankinit, MAX_BANK);
	rk29_gpio_irq_setup();
}
static void __init machine_rk29_board_init(void)
{ 
	platform_add_devices(devices, ARRAY_SIZE(devices));	
}

static void __init machine_rk29_mapio(void)
{
	rk29_map_common_io();
	rk29_clock_init();
	rk29_iomux_init();	
}

MACHINE_START(RK29, "RK29board")

/* UART for LL DEBUG */
	.phys_io	= RK29_UART1_PHYS, 
	.io_pg_offst	= ((RK29_UART1_BASE) >> 18) & 0xfffc,
	.boot_params	= RK29_SDRAM_PHYS + 0x88000,
	.map_io		= machine_rk29_mapio,
	.init_irq	= machine_rk29_init_irq,
	.init_machine	= machine_rk29_board_init,
	.timer		= &rk29_timer,
MACHINE_END
