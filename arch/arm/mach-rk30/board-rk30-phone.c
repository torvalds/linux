/* arch/arm/mach-rk30/board-rk30-sdk.c
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
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/skbuff.h>
#include <linux/spi/spi.h>
#include <linux/mmc/host.h>
#include <linux/ion.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/hardware/gic.h>

#include <mach/board.h>
#include <mach/hardware.h>
#include <mach/io.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/mpu.h>

#include <linux/regulator/fixed.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/regulator/machine.h>
#include "../../../drivers/headset_observe/rk_headset.h"

#if defined(CONFIG_SPIM_RK29)
#include "../../../drivers/spi/rk29_spim.h"
#endif
#if defined(CONFIG_ANDROID_TIMED_GPIO)
#include "../../../drivers/staging/android/timed_gpio.h"
#endif
#if defined(CONFIG_BACKLIGHT_AW9364)
#include "../../../drivers/video/backlight/aw9364_bl.h"
#endif
#if defined(CONFIG_RK29_SC8800)
#include "../../../drivers/tty/serial/sc8800.h"
#endif
#if defined(CONFIG_TDSC8800)
#include <linux/mtk23d.h>
#endif
#if defined(CONFIG_SMS_SPI_ROCKCHIP)
#include "../../../drivers/cmmb/siano/smsspiphy.h"
#endif


#define RK30_FB0_MEM_SIZE 8*SZ_1M

#ifdef CONFIG_VIDEO_RK29
/*---------------- Camera Sensor Macro Define Begin  ------------------------*/
/*---------------- Camera Sensor Configuration Macro Begin ------------------------*/
#define CONFIG_SENSOR_0 RK29_CAM_SENSOR_OV5642						/* back camera sensor */
#define CONFIG_SENSOR_IIC_ADDR_0		0x78
#define CONFIG_SENSOR_IIC_ADAPTER_ID_0	  4
#define CONFIG_SENSOR_CIF_INDEX_0                    1
#define CONFIG_SENSOR_ORIENTATION_0 	  90
#define CONFIG_SENSOR_POWER_PIN_0		  INVALID_GPIO
#define CONFIG_SENSOR_RESET_PIN_0		  INVALID_GPIO
#define CONFIG_SENSOR_POWERDN_PIN_0 	  RK30_PIN1_PD6
#define CONFIG_SENSOR_FALSH_PIN_0		  INVALID_GPIO
#define CONFIG_SENSOR_POWERACTIVE_LEVEL_0 RK29_CAM_POWERACTIVE_L
#define CONFIG_SENSOR_RESETACTIVE_LEVEL_0 RK29_CAM_RESETACTIVE_L
#define CONFIG_SENSOR_POWERDNACTIVE_LEVEL_0 RK29_CAM_POWERDNACTIVE_H
#define CONFIG_SENSOR_FLASHACTIVE_LEVEL_0 RK29_CAM_FLASHACTIVE_L

#define CONFIG_SENSOR_QCIF_FPS_FIXED_0		15000
#define CONFIG_SENSOR_240X160_FPS_FIXED_0   15000
#define CONFIG_SENSOR_QVGA_FPS_FIXED_0		15000
#define CONFIG_SENSOR_CIF_FPS_FIXED_0		15000
#define CONFIG_SENSOR_VGA_FPS_FIXED_0		15000
#define CONFIG_SENSOR_480P_FPS_FIXED_0		15000
#define CONFIG_SENSOR_SVGA_FPS_FIXED_0		15000
#define CONFIG_SENSOR_720P_FPS_FIXED_0		30000

#define CONFIG_SENSOR_01  RK29_CAM_SENSOR_OV5642                   /* back camera sensor 1 */
#define CONFIG_SENSOR_IIC_ADDR_01 	    0x00
#define CONFIG_SENSOR_CIF_INDEX_01                    1
#define CONFIG_SENSOR_IIC_ADAPTER_ID_01    4
#define CONFIG_SENSOR_ORIENTATION_01       90
#define CONFIG_SENSOR_POWER_PIN_01         INVALID_GPIO
#define CONFIG_SENSOR_RESET_PIN_01         INVALID_GPIO
#define CONFIG_SENSOR_POWERDN_PIN_01       RK30_PIN1_PD6
#define CONFIG_SENSOR_FALSH_PIN_01         INVALID_GPIO
#define CONFIG_SENSOR_POWERACTIVE_LEVEL_01 RK29_CAM_POWERACTIVE_L
#define CONFIG_SENSOR_RESETACTIVE_LEVEL_01 RK29_CAM_RESETACTIVE_L
#define CONFIG_SENSOR_POWERDNACTIVE_LEVEL_01 RK29_CAM_POWERDNACTIVE_H
#define CONFIG_SENSOR_FLASHACTIVE_LEVEL_01 RK29_CAM_FLASHACTIVE_L

#define CONFIG_SENSOR_QCIF_FPS_FIXED_01      15000
#define CONFIG_SENSOR_240X160_FPS_FIXED_01   15000
#define CONFIG_SENSOR_QVGA_FPS_FIXED_01      15000
#define CONFIG_SENSOR_CIF_FPS_FIXED_01       15000
#define CONFIG_SENSOR_VGA_FPS_FIXED_01       15000
#define CONFIG_SENSOR_480P_FPS_FIXED_01      15000
#define CONFIG_SENSOR_SVGA_FPS_FIXED_01      15000
#define CONFIG_SENSOR_720P_FPS_FIXED_01     30000

#define CONFIG_SENSOR_02 RK29_CAM_SENSOR_OV5640                      /* back camera sensor 2 */
#define CONFIG_SENSOR_IIC_ADDR_02 	    0x00
#define CONFIG_SENSOR_CIF_INDEX_02                    1
#define CONFIG_SENSOR_IIC_ADAPTER_ID_02    4
#define CONFIG_SENSOR_ORIENTATION_02       90
#define CONFIG_SENSOR_POWER_PIN_02         INVALID_GPIO
#define CONFIG_SENSOR_RESET_PIN_02         INVALID_GPIO
#define CONFIG_SENSOR_POWERDN_PIN_02       RK30_PIN1_PD6
#define CONFIG_SENSOR_FALSH_PIN_02         INVALID_GPIO
#define CONFIG_SENSOR_POWERACTIVE_LEVEL_02 RK29_CAM_POWERACTIVE_L
#define CONFIG_SENSOR_RESETACTIVE_LEVEL_02 RK29_CAM_RESETACTIVE_L
#define CONFIG_SENSOR_POWERDNACTIVE_LEVEL_02 RK29_CAM_POWERDNACTIVE_H
#define CONFIG_SENSOR_FLASHACTIVE_LEVEL_02 RK29_CAM_FLASHACTIVE_L

#define CONFIG_SENSOR_QCIF_FPS_FIXED_02      15000
#define CONFIG_SENSOR_240X160_FPS_FIXED_02   15000
#define CONFIG_SENSOR_QVGA_FPS_FIXED_02      15000
#define CONFIG_SENSOR_CIF_FPS_FIXED_02       15000
#define CONFIG_SENSOR_VGA_FPS_FIXED_02       15000
#define CONFIG_SENSOR_480P_FPS_FIXED_02      15000
#define CONFIG_SENSOR_SVGA_FPS_FIXED_02      15000
#define CONFIG_SENSOR_720P_FPS_FIXED_02      30000

#define CONFIG_SENSOR_1 RK29_CAM_SENSOR_OV2659                      /* front camera sensor 0 */
#define CONFIG_SENSOR_IIC_ADDR_1 	    0x60
#define CONFIG_SENSOR_IIC_ADAPTER_ID_1	  3
#define CONFIG_SENSOR_CIF_INDEX_1				  0
#define CONFIG_SENSOR_ORIENTATION_1       270
#define CONFIG_SENSOR_POWER_PIN_1         INVALID_GPIO
#define CONFIG_SENSOR_RESET_PIN_1         INVALID_GPIO
#define CONFIG_SENSOR_POWERDN_PIN_1 	  RK30_PIN1_PB7
#define CONFIG_SENSOR_FALSH_PIN_1         INVALID_GPIO
#define CONFIG_SENSOR_POWERACTIVE_LEVEL_1 RK29_CAM_POWERACTIVE_L
#define CONFIG_SENSOR_RESETACTIVE_LEVEL_1 RK29_CAM_RESETACTIVE_L
#define CONFIG_SENSOR_POWERDNACTIVE_LEVEL_1 RK29_CAM_POWERDNACTIVE_H
#define CONFIG_SENSOR_FLASHACTIVE_LEVEL_1 RK29_CAM_FLASHACTIVE_L

#define CONFIG_SENSOR_QCIF_FPS_FIXED_1		15000
#define CONFIG_SENSOR_240X160_FPS_FIXED_1   15000
#define CONFIG_SENSOR_QVGA_FPS_FIXED_1		15000
#define CONFIG_SENSOR_CIF_FPS_FIXED_1		15000
#define CONFIG_SENSOR_VGA_FPS_FIXED_1		15000
#define CONFIG_SENSOR_480P_FPS_FIXED_1		15000
#define CONFIG_SENSOR_SVGA_FPS_FIXED_1		15000
#define CONFIG_SENSOR_720P_FPS_FIXED_1		30000

#define CONFIG_SENSOR_11 RK29_CAM_SENSOR_OV2659                      /* front camera sensor 1 */
#define CONFIG_SENSOR_IIC_ADDR_11 	    0x00
#define CONFIG_SENSOR_IIC_ADAPTER_ID_11    3
#define CONFIG_SENSOR_CIF_INDEX_1				  0
#define CONFIG_SENSOR_ORIENTATION_11       270
#define CONFIG_SENSOR_POWER_PIN_11         INVALID_GPIO
#define CONFIG_SENSOR_RESET_PIN_11         INVALID_GPIO
#define CONFIG_SENSOR_POWERDN_PIN_11       RK30_PIN1_PB7
#define CONFIG_SENSOR_FALSH_PIN_11         INVALID_GPIO
#define CONFIG_SENSOR_POWERACTIVE_LEVEL_11 RK29_CAM_POWERACTIVE_L
#define CONFIG_SENSOR_RESETACTIVE_LEVEL_11 RK29_CAM_RESETACTIVE_L
#define CONFIG_SENSOR_POWERDNACTIVE_LEVEL_11 RK29_CAM_POWERDNACTIVE_H
#define CONFIG_SENSOR_FLASHACTIVE_LEVEL_11 RK29_CAM_FLASHACTIVE_L

#define CONFIG_SENSOR_QCIF_FPS_FIXED_11      15000
#define CONFIG_SENSOR_240X160_FPS_FIXED_11   15000
#define CONFIG_SENSOR_QVGA_FPS_FIXED_11      15000
#define CONFIG_SENSOR_CIF_FPS_FIXED_11       15000
#define CONFIG_SENSOR_VGA_FPS_FIXED_11       15000
#define CONFIG_SENSOR_480P_FPS_FIXED_11      15000
#define CONFIG_SENSOR_SVGA_FPS_FIXED_11      15000
#define CONFIG_SENSOR_720P_FPS_FIXED_11      30000

#define CONFIG_SENSOR_12 RK29_CAM_SENSOR_OV2655                      /* front camera sensor 2 */
#define CONFIG_SENSOR_IIC_ADDR_12 	    0x00
#define CONFIG_SENSOR_IIC_ADAPTER_ID_12    3
#define CONFIG_SENSOR_CIF_INDEX_1				  0
#define CONFIG_SENSOR_ORIENTATION_12       270
#define CONFIG_SENSOR_POWER_PIN_12         INVALID_GPIO
#define CONFIG_SENSOR_RESET_PIN_12         INVALID_GPIO
#define CONFIG_SENSOR_POWERDN_PIN_12       RK30_PIN1_PB7
#define CONFIG_SENSOR_FALSH_PIN_12         INVALID_GPIO
#define CONFIG_SENSOR_POWERACTIVE_LEVEL_12 RK29_CAM_POWERACTIVE_L
#define CONFIG_SENSOR_RESETACTIVE_LEVEL_12 RK29_CAM_RESETACTIVE_L
#define CONFIG_SENSOR_POWERDNACTIVE_LEVEL_12 RK29_CAM_POWERDNACTIVE_H
#define CONFIG_SENSOR_FLASHACTIVE_LEVEL_12 RK29_CAM_FLASHACTIVE_L

#define CONFIG_SENSOR_QCIF_FPS_FIXED_12      15000
#define CONFIG_SENSOR_240X160_FPS_FIXED_12   15000
#define CONFIG_SENSOR_QVGA_FPS_FIXED_12      15000
#define CONFIG_SENSOR_CIF_FPS_FIXED_12       15000
#define CONFIG_SENSOR_VGA_FPS_FIXED_12       15000
#define CONFIG_SENSOR_480P_FPS_FIXED_12      15000
#define CONFIG_SENSOR_SVGA_FPS_FIXED_12      15000
#define CONFIG_SENSOR_720P_FPS_FIXED_12      30000


#endif  //#ifdef CONFIG_VIDEO_RK29
/*---------------- Camera Sensor Configuration Macro End------------------------*/
#include "../../../drivers/media/video/rk30_camera.c"
/*---------------- Camera Sensor Macro Define End  ---------*/

#define PMEM_CAM_SIZE PMEM_CAM_NECESSARY
/*****************************************************************************************
 * camera  devices
 * author: ddl@rock-chips.com
 *****************************************************************************************/
#ifdef CONFIG_VIDEO_RK29
#define CONFIG_SENSOR_POWER_IOCTL_USR	   0
#define CONFIG_SENSOR_RESET_IOCTL_USR	   0
#define CONFIG_SENSOR_POWERDOWN_IOCTL_USR	   0
#define CONFIG_SENSOR_FLASH_IOCTL_USR	   0

#if CONFIG_SENSOR_POWER_IOCTL_USR
static int sensor_power_usr_cb (struct rk29camera_gpio_res *res,int on)
{
	#error "CONFIG_SENSOR_POWER_IOCTL_USR is 1, sensor_power_usr_cb function must be writed!!";
}
#endif

#if CONFIG_SENSOR_RESET_IOCTL_USR
static int sensor_reset_usr_cb (struct rk29camera_gpio_res *res,int on)
{
	#error "CONFIG_SENSOR_RESET_IOCTL_USR is 1, sensor_reset_usr_cb function must be writed!!";
}
#endif

#if CONFIG_SENSOR_POWERDOWN_IOCTL_USR
static int sensor_powerdown_usr_cb (struct rk29camera_gpio_res *res,int on)
{
	#error "CONFIG_SENSOR_POWERDOWN_IOCTL_USR is 1, sensor_powerdown_usr_cb function must be writed!!";
}
#endif

#if CONFIG_SENSOR_FLASH_IOCTL_USR
static int sensor_flash_usr_cb (struct rk29camera_gpio_res *res,int on)
{
	#error "CONFIG_SENSOR_FLASH_IOCTL_USR is 1, sensor_flash_usr_cb function must be writed!!";
}
#endif

static struct rk29camera_platform_ioctl_cb	sensor_ioctl_cb = {
	#if CONFIG_SENSOR_POWER_IOCTL_USR
	.sensor_power_cb = sensor_power_usr_cb,
	#else
	.sensor_power_cb = NULL,
	#endif

	#if CONFIG_SENSOR_RESET_IOCTL_USR
	.sensor_reset_cb = sensor_reset_usr_cb,
	#else
	.sensor_reset_cb = NULL,
	#endif

	#if CONFIG_SENSOR_POWERDOWN_IOCTL_USR
	.sensor_powerdown_cb = sensor_powerdown_usr_cb,
	#else
	.sensor_powerdown_cb = NULL,
	#endif

	#if CONFIG_SENSOR_FLASH_IOCTL_USR
	.sensor_flash_cb = sensor_flash_usr_cb,
	#else
	.sensor_flash_cb = NULL,
	#endif
};

#if CONFIG_SENSOR_IIC_ADDR_0
static struct reginfo_t rk_init_data_sensor_reg_0[] =
{
		{0x0000, 0x00,0,0}
	};
static struct reginfo_t rk_init_data_sensor_winseqreg_0[] ={
	{0x0000, 0x00,0,0}
	};
#endif

#if CONFIG_SENSOR_IIC_ADDR_1
static struct reginfo_t rk_init_data_sensor_reg_1[] =
{
    {0x0000, 0x00,0,0}
};
static struct reginfo_t rk_init_data_sensor_winseqreg_1[] =
{
       {0x0000, 0x00,0,0}
};
#endif
static rk_sensor_user_init_data_s rk_init_data_sensor[RK_CAM_NUM] = 
{
    {
       .rk_sensor_init_width = INVALID_VALUE,
       .rk_sensor_init_height = INVALID_VALUE,
       .rk_sensor_init_bus_param = INVALID_VALUE,
       .rk_sensor_init_pixelcode = INVALID_VALUE,
       .rk_sensor_init_data = NULL,//rk_init_data_sensor_reg_0,
       .rk_sensor_init_winseq = NULL,//rk_init_data_sensor_winseqreg_0,
       .rk_sensor_winseq_size = 0,//sizeof(rk_init_data_sensor_winseqreg_0) / sizeof(struct reginfo_t),
    },{
        .rk_sensor_init_width = INVALID_VALUE,
       .rk_sensor_init_height = INVALID_VALUE,
       .rk_sensor_init_bus_param = INVALID_VALUE,
       .rk_sensor_init_pixelcode = INVALID_VALUE,
       .rk_sensor_init_data = NULL,//rk_init_data_sensor_reg_1,
       .rk_sensor_init_winseq = NULL,//rk_init_data_sensor_winseqreg_1,
       .rk_sensor_winseq_size = 0,//sizeof(rk_init_data_sensor_winseqreg_1) / sizeof(struct reginfo_t),
    }

 };
#include "../../../drivers/media/video/rk30_camera.c"

#endif /* CONFIG_VIDEO_RK29 */

#if defined(CONFIG_TOUCHSCREEN_ILI2102_IIC) 
#include "../../../drivers/input/touchscreen/ili2102_ts.h"
#define TOUCH_GPIO_INT      RK30_PIN4_PC2
#define TOUCH_GPIO_RESET    RK30_PIN4_PD0
static struct ili2102_platform_data ili2102_info = {
	.model			= 2102,
	.swap_xy		= 0,
	.x_min			= 0,
	.x_max			= 480,
	.y_min			= 0,
	.y_max			= 800,
	.gpio_reset     = TOUCH_GPIO_RESET,
	.gpio_reset_active_low = 1,
	.gpio_pendown		= TOUCH_GPIO_INT,
	.pendown_iomux_name = GPIO4C2_SMCDATA2_TRACEDATA2_NAME,
	.resetpin_iomux_name = GPIO4D0_SMCDATA8_TRACEDATA8_NAME,
	.pendown_iomux_mode = GPIO4C_GPIO4C2,
	.resetpin_iomux_mode = GPIO4D_GPIO4D0,
};
#endif


#if defined(CONFIG_TOUCHSCREEN_GT8XX)
#define TOUCH_RESET_PIN  RK30_PIN4_PD0
#define TOUCH_PWR_PIN    INVALID_GPIO
int goodix_init_platform_hw(void)
{
	int ret;
	
	rk30_mux_api_set(GPIO4D0_SMCDATA8_TRACEDATA8_NAME, GPIO4D_GPIO4D0);
	rk30_mux_api_set(GPIO4C2_SMCDATA2_TRACEDATA2_NAME, GPIO4C_GPIO4C2);
	printk("%s:0x%x,0x%x\n",__func__,rk30_mux_api_get(GPIO4D0_SMCDATA8_TRACEDATA8_NAME),rk30_mux_api_get(GPIO4C2_SMCDATA2_TRACEDATA2_NAME));

	if (TOUCH_PWR_PIN != INVALID_GPIO) {
		ret = gpio_request(TOUCH_PWR_PIN, "goodix power pin");
		if (ret != 0) {
			gpio_free(TOUCH_PWR_PIN);
			printk("goodix power error\n");
			return -EIO;
		}
		gpio_direction_output(TOUCH_PWR_PIN, 0);
		gpio_set_value(TOUCH_PWR_PIN, GPIO_LOW);
		msleep(100);
	}

	if (TOUCH_RESET_PIN != INVALID_GPIO) {
		ret = gpio_request(TOUCH_RESET_PIN, "goodix reset pin");
		if (ret != 0) {
			gpio_free(TOUCH_RESET_PIN);
			printk("goodix gpio_request error\n");
			return -EIO;
		}
		gpio_direction_output(TOUCH_RESET_PIN, 0);
		gpio_set_value(TOUCH_RESET_PIN, GPIO_LOW);
		msleep(10);
		gpio_set_value(TOUCH_RESET_PIN, GPIO_HIGH);
		msleep(500);
	}
	return 0;
}

struct goodix_platform_data goodix_info = {
	.model = 8105,
	.irq_pin = RK30_PIN4_PC2,
	.rest_pin = TOUCH_RESET_PIN,
	.init_platform_hw = goodix_init_platform_hw,
};
#endif

#if defined(CONFIG_RK29_SC8800)
static int sc8800_io_init(void)
{
	rk30_mux_api_set(GPIO2B5_LCDC1DATA13_SMCADDR17_HSADCDATA8_NAME, GPIO2B_GPIO2B5);
	rk30_mux_api_set(GPIO2A2_LCDCDATA2_SMCADDR6_NAME, GPIO2A_GPIO2A2);
	return 0;
}

static int sc8800_io_deinit(void)
{

	return 0;
}


static struct plat_sc8800 sc8800_plat_data = {
	.slav_rts_pin = RK30_PIN6_PA0,
	.slav_rdy_pin = RK30_PIN2_PB5,
	.master_rts_pin = RK30_PIN6_PA1,
	.master_rdy_pin = RK30_PIN2_PA2,
	//.poll_time = 100,
	.io_init = sc8800_io_init,
    .io_deinit = sc8800_io_deinit,
};

static struct rk29xx_spi_chip sc8800_spi_chip = {
	//.poll_mode = 1,
	.enable_dma = 1,
};

#endif
#if defined(CONFIG_SMS_SPI_ROCKCHIP)
#define CMMB_1186_SPIIRQ RK30_PIN4_PB7
#define CMMB_1186_RESET RK30_PIN0_PD5

void cmmb_io_init_mux(void)
{
//	rk30_mux_api_set(GPIO1A4_UART1SIN_SPI0CSN0_NAME,GPIO1A_GPIO1A4);

}
void cmmb_io_set_for_pm(void)
{
	printk("entering cmmb_io_set_for_pm\n");
	rk30_mux_api_set(GPIO0D5_I2S22CHSDO_SMCADDR1_NAME,GPIO0D_GPIO0D5);
	gpio_request(CMMB_1186_RESET, NULL);//cmmb reset pin
	gpio_direction_output(CMMB_1186_RESET,0);
	rk30_mux_api_set(GPIO1A4_UART1SIN_SPI0CSN0_NAME,GPIO1A_GPIO1A4);
	gpio_request(RK30_PIN1_PA4, NULL);//cmmb cs pin
	gpio_direction_input(RK30_PIN1_PA4);
	gpio_pull_updown(RK30_PIN1_PA4, 0);
}

void cmmb_power_on_by_wm831x(void)
{
	struct regulator *ldo;
#if 0
	printk("entering cmmb_power_on_by_wm831x\n");

	rk29_mux_api_set(GPIO1A3_EMMCDETECTN_SPI1CS1_NAME,GPIO1L_SPI0_CSN1);
	gpio_request(RK29_PIN6_PD2, NULL);
	gpio_direction_output(RK29_PIN6_PD2,0);
	mdelay(200);

	ldo = regulator_get(NULL, "ldo8");		//cmmb
	regulator_set_voltage(ldo,1200000,1200000);
	regulator_set_suspend_voltage(ldo,1200000);
	regulator_enable(ldo);
	printk("%s set ldo8=%dmV end\n", __FUNCTION__, regulator_get_voltage(ldo));
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo9");		//cmmb
	regulator_set_voltage(ldo,3000000,3000000);
	regulator_set_suspend_voltage(ldo,3000000);
	regulator_enable(ldo);
	printk("%s set ldo9=%dmV end\n", __FUNCTION__, regulator_get_voltage(ldo));
	regulator_put(ldo);

	mdelay(200);
	gpio_direction_output(RK29_PIN6_PD2,1);
#endif
}

void cmmb_power_down_by_wm831x(void)
{
	struct regulator* ldo;
#if 0
	printk("entering cmmb_power_down_by_wm831x\n");

	ldo = regulator_get(NULL, "ldo8");
	regulator_set_voltage(ldo,0,0);
	regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo9");
	regulator_set_voltage(ldo,0,0);
	regulator_disable(ldo);
	regulator_put(ldo);
#endif
}

static struct cmmb_io_def_s cmmb_io = {
	.cmmb_pw_en = INVALID_GPIO,
	.cmmb_pw_dwn = INVALID_GPIO,
	.cmmb_pw_rst = CMMB_1186_RESET,
	.cmmb_irq = CMMB_1186_SPIIRQ,
	.io_init_mux = cmmb_io_init_mux,
	.cmmb_io_pm = cmmb_io_set_for_pm,
	.cmmb_power_on = cmmb_power_on_by_wm831x,
	.cmmb_power_down = cmmb_power_down_by_wm831x
};

static struct rk29xx_spi_chip cmb_spi_chip = {
	//.poll_mode = 1,
	.enable_dma = 1,
};

#endif

static struct spi_board_info board_spi_devices[] = {
#if defined(CONFIG_RK29_SC8800)
		{
			.modalias  = "sc8800",
			.bus_num = 1,
			.platform_data = &sc8800_plat_data,
			.max_speed_hz  = 12*1000*1000,
			.chip_select   = 0,		
			.controller_data = &sc8800_spi_chip,
		},
#endif
	
#if defined(CONFIG_SMS_SPI_ROCKCHIP)
		{
			.modalias	= "siano1186",
			.chip_select	= 0,
			.max_speed_hz	= 12*1000*1000,
			.bus_num	= 0,
			.irq		=CMMB_1186_SPIIRQ,
			.platform_data = &cmmb_io,	
			.controller_data = &cmb_spi_chip,
		},
#endif

};
/*****************************************************************************************
 * wm8994  codec
 * author: qjb@rock-chips.com
 *****************************************************************************************/
#if defined(CONFIG_MFD_WM8994)
static struct regulator_consumer_supply wm8994_fixed_voltage0_supplies[] = {
	REGULATOR_SUPPLY("DBVDD", "0-001a"),
	REGULATOR_SUPPLY("AVDD2", "0-001a"),
	REGULATOR_SUPPLY("CPVDD", "0-001a"),
};

static struct regulator_consumer_supply wm8994_fixed_voltage1_supplies[] = {
	REGULATOR_SUPPLY("SPKVDD1", "0-001a"),
	REGULATOR_SUPPLY("SPKVDD2", "0-001a"),
};

static struct regulator_init_data wm8994_fixed_voltage0_init_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies	= ARRAY_SIZE(wm8994_fixed_voltage0_supplies),
	.consumer_supplies	= wm8994_fixed_voltage0_supplies,
};

static struct regulator_init_data wm8994_fixed_voltage1_init_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies	= ARRAY_SIZE(wm8994_fixed_voltage1_supplies),
	.consumer_supplies	= wm8994_fixed_voltage1_supplies,
};

static struct fixed_voltage_config wm8994_fixed_voltage0_config = {
	.supply_name	= "VCC_1.8V_PDA",
	.microvolts	= 1800000,
	.gpio		= -EINVAL,
	.init_data	= &wm8994_fixed_voltage0_init_data,
};

static struct fixed_voltage_config wm8994_fixed_voltage1_config = {
	.supply_name	= "V_BAT",
	.microvolts	= 3700000,
	.gpio		= -EINVAL,
	.init_data	= &wm8994_fixed_voltage1_init_data,
};

static struct platform_device wm8994_fixed_voltage0 = {
	.name		= "reg-fixed-voltage",
	.id		= 0,
	.dev		= {
		.platform_data	= &wm8994_fixed_voltage0_config,
	},
};

static struct platform_device wm8994_fixed_voltage1 = {
	.name		= "reg-fixed-voltage",
	.id		= 1,
	.dev		= {
		.platform_data	= &wm8994_fixed_voltage1_config,
	},
};

static struct regulator_consumer_supply wm8994_avdd1_supply =
	REGULATOR_SUPPLY("AVDD1", "0-001a");

static struct regulator_consumer_supply wm8994_dcvdd_supply =
	REGULATOR_SUPPLY("DCVDD", "0-001a");



static struct regulator_init_data wm8994_ldo1_data = {
	.constraints	= {
		.name		= "AVDD1_3.0V",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &wm8994_avdd1_supply,
};

static struct regulator_init_data wm8994_ldo2_data = {
	.constraints	= {
		.name		= "DCVDD_1.0V",
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &wm8994_dcvdd_supply,
};

static struct wm8994_pdata wm8994_platform_data = {
#if defined (CONFIG_GPIO_WM8994)	
	.gpio_base = WM8994_GPIO_EXPANDER_BASE,
	//Fill value to initialize the GPIO	
//	.gpio_defaults ={},
	/* configure gpio1 function: 0x0001(Logic level input/output) */
//	.gpio_defaults[0] = 0x0001,
	/* configure gpio3/4/5/7 function for AIF2 voice */
	.gpio_defaults[2] = 0x2100,
	.gpio_defaults[3] = 0x2100,
	.gpio_defaults[4] = 0xA100,
//	.gpio_defaults[6] = 0x0100,
	/* configure gpio8/9/10/11 function for AIF3 BT */
	.gpio_defaults[7] = 0xA100,
	.gpio_defaults[8] = 0x2100,
	.gpio_defaults[9] = 0x2100,
	.gpio_defaults[10] = 0x2100,	
#endif		

	.ldo[0]	= { RK30_PIN6_PB2, NULL, &wm8994_ldo1_data },	/* XM0FRNB_2 */
	.ldo[1]	= { 0, NULL, &wm8994_ldo2_data },

	.micdet_irq = 0,
	.irq_base = 0,

	.lineout1_diff = 1,
//      .BB_input_diff = 1,
};
#endif 

#ifdef CONFIG_RK_HEADSET_DET
#define HEADSET_GPIO RK29_PIN4_PD2
struct rk_headset_pdata rk_headset_info = {
	.Headset_gpio		= RK30_PIN6_PA1,
	.headset_in_type= HEADSET_IN_HIGH,
	.Hook_gpio = RK30_PIN1_PB2,//Detection Headset--Must be set
	.hook_key_code = KEY_MEDIA,
};

struct platform_device rk_device_headset = {
		.name	= "rk_headsetdet",
		.id 	= 0,
		.dev    = {
		    .platform_data = &rk_headset_info,
		}
};
#endif


/***********************************************************
*	rk30  backlight
************************************************************/
#ifdef CONFIG_BACKLIGHT_AW9364
static int aw9364_backlight_io_init(void)
{
	rk30_mux_api_set(GPIO4D2_SMCDATA10_TRACEDATA10_NAME, GPIO4D_GPIO4D2);
	return 0;
}

static int aw9364_backlight_io_deinit(void)
{
	return 0;
}
struct aw9364_platform_data aw9364_bl_info = {
	.pin_en   = RK30_PIN4_PD2,
	.io_init   = aw9364_backlight_io_init,
	.io_deinit = aw9364_backlight_io_deinit,
};

struct platform_device aw9364_device_backlight = {
	.name = "aw9364_backlight",
	.id = -1,		
	.dev		= {
	.platform_data = &aw9364_bl_info,	
	}			
};
	
#endif

#ifdef CONFIG_BACKLIGHT_RK29_BL
#define PWM_ID            0
#define PWM_MUX_NAME      GPIO0A3_PWM0_NAME
#define PWM_MUX_MODE      GPIO0A_PWM0
#define PWM_MUX_MODE_GPIO GPIO0A_GPIO0A3
#define PWM_GPIO 	  RK30_PIN0_PA3
#define PWM_EFFECT_VALUE  1

#define LCD_DISP_ON_PIN

#ifdef  LCD_DISP_ON_PIN
//#define BL_EN_MUX_NAME    GPIOF34_UART3_SEL_NAME
//#define BL_EN_MUX_MODE    IOMUXB_GPIO1_B34

#define BL_EN_PIN         RK30_PIN6_PB3
#define BL_EN_VALUE       GPIO_HIGH
#endif
static int rk29_backlight_io_init(void)
{
	int ret = 0;
	rk30_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE);
#ifdef  LCD_DISP_ON_PIN
	// rk30_mux_api_set(BL_EN_MUX_NAME, BL_EN_MUX_MODE);

	ret = gpio_request(BL_EN_PIN, NULL);
	if (ret != 0) {
		gpio_free(BL_EN_PIN);
	}

	gpio_direction_output(BL_EN_PIN, 0);
	gpio_set_value(BL_EN_PIN, BL_EN_VALUE);
#endif
	return ret;
}

static int rk29_backlight_io_deinit(void)
{
	int ret = 0;
#ifdef  LCD_DISP_ON_PIN
	gpio_free(BL_EN_PIN);
#endif
	rk30_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE_GPIO);
	return ret;
}

static int rk29_backlight_pwm_suspend(void)
{
	int ret = 0;
	rk30_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE_GPIO);
	if (gpio_request(PWM_GPIO, NULL)) {
		printk("func %s, line %d: request gpio fail\n", __FUNCTION__, __LINE__);
		return -1;
	}
	gpio_direction_output(PWM_GPIO, GPIO_LOW);
#ifdef  LCD_DISP_ON_PIN
	gpio_direction_output(BL_EN_PIN, 0);
	gpio_set_value(BL_EN_PIN, !BL_EN_VALUE);
#endif
	return ret;
}

static int rk29_backlight_pwm_resume(void)
{
	gpio_free(PWM_GPIO);
	rk30_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE);
#ifdef  LCD_DISP_ON_PIN
	msleep(30);
	gpio_direction_output(BL_EN_PIN, 1);
	gpio_set_value(BL_EN_PIN, BL_EN_VALUE);
#endif
	return 0;
}

static struct rk29_bl_info rk29_bl_info = {
	.pwm_id = PWM_ID,
	.bl_ref = PWM_EFFECT_VALUE,
	.io_init = rk29_backlight_io_init,
	.io_deinit = rk29_backlight_io_deinit,
	.pwm_suspend = rk29_backlight_pwm_suspend,
	.pwm_resume = rk29_backlight_pwm_resume,
};

static struct platform_device rk29_device_backlight = {
	.name	= "rk29_backlight",
	.id 	= -1,
	.dev	= {
		.platform_data  = &rk29_bl_info,
	}
};

#endif

/*MMA8452 gsensor*/
#if defined (CONFIG_GS_MMA8452)
#define MMA8452_INT_PIN   RK30_PIN4_PC0

static int mma8452_init_platform_hw(void)
{
	rk30_mux_api_set(GPIO4C0_SMCDATA0_TRACEDATA0_NAME, GPIO4C_GPIO4C0);

	if (gpio_request(MMA8452_INT_PIN, NULL) != 0) {
		gpio_free(MMA8452_INT_PIN);
		printk("mma8452_init_platform_hw gpio_request error\n");
		return -EIO;
	}
	gpio_pull_updown(MMA8452_INT_PIN, 1);
	return 0;
}

static struct mma8452_platform_data mma8452_info = {
	.model = 8452,
	.swap_xy = 0,
	.swap_xyz = 1,
	.init_platform_hw = mma8452_init_platform_hw,
	.orientation = {-1, 0, 0, 0, 0, 1, 0, -1, 0},
};
#endif
#if defined (CONFIG_COMPASS_AK8975)
static struct akm8975_platform_data akm8975_info =
{
	.m_layout = 
	{
		{
			{-1, 0, 0},
			{0, 0, 1},
			{0, -1, 0},
		},

		{
			{1, 0, 0},
			{0, 1, 0},
			{0, 0, 1},
		},

		{
			{1, 0, 0},
			{0, 1, 0},
			{0, 0, 1},
		},

		{
			{1, 0, 0},
			{0, 1, 0},
			{0, 0, 1},
		},
	}
};

#endif

/*mpu3050*/
#if defined (CONFIG_MPU_SENSORS_MPU3050)
static struct mpu_platform_data mpu3050_data = {
	.int_config = 0x10,
	.orientation = { 1, 0, 0,0, 1, 0, 0, 0, 1 },
};
#endif

/* accel */
#if defined (CONFIG_MPU_SENSORS_MMA845X)
static struct ext_slave_platform_data inv_mpu_mma845x_data = {
	.bus         = EXT_SLAVE_BUS_SECONDARY,
	.adapt_num = 0,
	.orientation = {1, 0, 0, 0, 1, 0, 0, 0, 1},
};
#endif

/* compass */
#if defined (CONFIG_MPU_SENSORS_AK8975)
static struct ext_slave_platform_data inv_mpu_ak8975_data = {
	.bus         = EXT_SLAVE_BUS_PRIMARY,
	.adapt_num = 0,
	.orientation = {0, 1, 0, -1, 0, 0, 0, 0, 1},
};
#endif


#if defined(CONFIG_GYRO_L3G4200D)

#include <linux/l3g4200d.h>
#define L3G4200D_INT_PIN  RK30_PIN4_PC3

static int l3g4200d_init_platform_hw(void)
{
	if (gpio_request(L3G4200D_INT_PIN, NULL) != 0) {
		gpio_free(L3G4200D_INT_PIN);
		printk("%s: request l3g4200d int pin error\n", __func__);
		return -EIO;
	}
	gpio_pull_updown(L3G4200D_INT_PIN, 1);
	return 0;
}

static struct l3g4200d_platform_data l3g4200d_info = {
	.fs_range = 1,

	.axis_map_x = 0,
	.axis_map_y = 1,
	.axis_map_z = 2,

	.negate_x = 1,
	.negate_y = 1,
	.negate_z = 0,

	.init = l3g4200d_init_platform_hw,
};

#endif



#ifdef CONFIG_LS_CM3217

#define CM3217_POWER_PIN 	INVALID_GPIO
#define CM3217_IRQ_PIN		INVALID_GPIO
static int cm3217_init_hw(void)
{
#if 0
	if (gpio_request(CM3217_POWER_PIN, NULL) != 0) {
		gpio_free(CM3217_POWER_PIN);
		printk("%s: request cm3217 power pin error\n", __func__);
		return -EIO;
	}
	gpio_pull_updown(CM3217_POWER_PIN, PullDisable);

	if (gpio_request(CM3217_IRQ_PIN, NULL) != 0) {
		gpio_free(CM3217_IRQ_PIN);
		printk("%s: request cm3217 int pin error\n", __func__);
		return -EIO;
	}
	gpio_pull_updown(CM3217_IRQ_PIN, PullDisable);
#endif
	return 0;
}

static void cm3217_exit_hw(void)
{
#if 0
	gpio_free(CM3217_POWER_PIN);
	gpio_free(CM3217_IRQ_PIN);
#endif
	return;
}

static struct cm3217_platform_data cm3217_info = {
	.irq_pin = CM3217_IRQ_PIN,
	.power_pin = CM3217_POWER_PIN,
	.init_platform_hw = cm3217_init_hw,
	.exit_platform_hw = cm3217_exit_hw,
};
#endif

#ifdef CONFIG_FB_ROCKCHIP

/*****************************************************************************************
 * lcd  devices
 * author: zyw@rock-chips.com
 *****************************************************************************************/
//#ifdef  CONFIG_LCD_TD043MGEA1

#define LCD_TXD_PIN		RK30_PIN0_PB7
#define LCD_CLK_PIN		RK30_PIN0_PB6
#define LCD_CS_PIN		RK30_PIN0_PB5
#define LCD_RST_PIN		RK30_PIN4_PC7

/*****************************************************************************************
* frame buffer  devices
* author: zyw@rock-chips.com
*****************************************************************************************/
#define FB_ID                       0
#define FB_DISPLAY_ON_PIN           INVALID_GPIO//RK29_PIN6_PD0
#define FB_LCD_STANDBY_PIN          INVALID_GPIO//RK29_PIN6_PD1
#define FB_LCD_CABC_EN_PIN          INVALID_GPIO//RK29_PIN6_PD2
#define FB_MCU_FMK_PIN              INVALID_GPIO

#define FB_DISPLAY_ON_VALUE         GPIO_HIGH
#define FB_LCD_STANDBY_VALUE        GPIO_HIGH


//#endif
static int rk29_lcd_io_init(void)
{
	int ret = 0;
	//printk("rk29_lcd_io_init\n");
	//ret = gpio_request(LCD_RXD_PIN, NULL);
	ret = gpio_request(LCD_TXD_PIN, NULL);
	ret = gpio_request(LCD_CLK_PIN, NULL);
	ret = gpio_request(LCD_CS_PIN, NULL);

	rk30_mux_api_set(GPIO0B7_I2S8CHSDO3_NAME, GPIO0B_GPIO0B7);
	rk30_mux_api_set(GPIO0B6_I2S8CHSDO2_NAME, GPIO0B_GPIO0B6);
	rk30_mux_api_set(GPIO0B5_I2S8CHSDO1_NAME, GPIO0B_GPIO0B5);
	

	gpio_request(LCD_RST_PIN, NULL);
	gpio_direction_output(LCD_RST_PIN, 1);
	gpio_direction_output(LCD_RST_PIN, 0);
	usleep_range(5*1000, 5*1000);
	gpio_set_value(LCD_RST_PIN, 1);
	usleep_range(50*1000, 50*1000);
	gpio_free(LCD_RST_PIN);

	return ret;
}

#if defined (CONFIG_RK29_WORKING_POWER_MANAGEMENT)
static int rk29_lcd_io_deinit(void)
{
	int ret = 0;
	
	gpio_direction_output(LCD_TXD_PIN, 1);
	gpio_direction_output(LCD_CLK_PIN, 1);

	gpio_free(LCD_CS_PIN);
	gpio_free(LCD_CLK_PIN);
	gpio_free(LCD_TXD_PIN);

	return ret;
}
#else
static int rk29_lcd_io_deinit(void)
{
	int ret = 0;
	//printk("rk29_lcd_io_deinit\n");
	gpio_free(LCD_CS_PIN);
	gpio_free(LCD_CLK_PIN);
	gpio_free(LCD_TXD_PIN);
	//gpio_free(LCD_RXD_PIN);

	//rk30_mux_api_set(GPIO0B7_I2S8CHSDO3_NAME, GPIO0B_GPIO0B7);
	//rk30_mux_api_set(GPIO0B6_I2S8CHSDO2_NAME, GPIO0B_GPIO0B6);
	//rk30_mux_api_set(GPIO0B5_I2S8CHSDO1_NAME, GPIO0B_GPIO0B5);

	return ret;
}
#endif


static struct rk29lcd_info rk29_lcd_info = {
    .txd_pin  = LCD_TXD_PIN,
    .clk_pin = LCD_CLK_PIN,
    .cs_pin = LCD_CS_PIN,
    .io_init   = rk29_lcd_io_init,
    .io_deinit = rk29_lcd_io_deinit,
};



#define LCD_EN_MUX_NAME    GPIO4C7_SMCDATA7_TRACEDATA7_NAME
#define LCD_EN_PIN         RK30_PIN4_PC7
#define LCD_EN_VALUE       GPIO_HIGH

static int rk_fb_io_init(void)
{
	int ret = 0;
	rk30_mux_api_set(LCD_EN_MUX_NAME, GPIO4C_GPIO4C7);
	ret = gpio_request(LCD_EN_PIN, NULL);
	if (ret != 0)
	{
		gpio_free(LCD_EN_PIN);
		printk(KERN_ERR "request lcd en pin fail!\n");
		return -1;
	}
	else
	{
		gpio_direction_output(LCD_EN_PIN, 1);
		gpio_set_value(LCD_EN_PIN, LCD_EN_VALUE);
	}
	return 0;
}
static int rk_fb_io_disable(void)
{
	gpio_set_value(LCD_EN_PIN, ~LCD_EN_VALUE);
	return 0;
}
static int rk_fb_io_enable(void)
{
	gpio_set_value(LCD_EN_PIN, LCD_EN_VALUE);
	return 0;
}


static struct rk29fb_info rk_fb_info = {
	.fb_id   = FB_ID,
    	.mcu_fmk_pin = FB_MCU_FMK_PIN,
    	.lcd_info = &rk29_lcd_info,
	.io_init   = rk_fb_io_init,
	.io_disable = rk_fb_io_disable,
	.io_enable = rk_fb_io_enable,
};

static struct resource resource_fb[] = {
	[0] = {
		.name  = "fb0 buf",
		.start = 0,
		.end   = 0,//RK30_FB0_MEM_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.name  = "ipp buf",  //for rotate
		.start = 0,
		.end   = 0,//RK30_FB0_MEM_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.name  = "fb2 buf",
		.start = 0,
		.end   = 0,//RK30_FB0_MEM_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device device_fb = {
	.name		= "rk-fb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resource_fb),
	.resource	= resource_fb,
	.dev            = {
		.platform_data  = &rk_fb_info,
	}
};
#endif

#ifdef CONFIG_ANDROID_TIMED_GPIO
static struct timed_gpio timed_gpios[] = {
	{
		.name = "vibrator",
		.gpio = RK30_PIN0_PA4,
		.max_timeout = 1000,
		.active_low = 0,
		.adjust_time =20,      //adjust for diff product
	},
};

static struct timed_gpio_platform_data rk29_vibrator_info = {
	.num_gpios = 1,
	.gpios = timed_gpios,
};

static struct platform_device rk29_device_vibrator = {
	.name = "timed-gpio",
	.id = -1,
	.dev = {
		.platform_data = &rk29_vibrator_info,
	},

};
#endif

#ifdef CONFIG_LEDS_GPIO_PLATFORM
static struct gpio_led rk29_leds[] = {
	{
		.name = "button-backlight",
		.gpio = RK30_PIN4_PD7,
		.default_trigger = "timer",
		.active_low = 0,
		.retain_state_suspended = 0,
		.default_state = LEDS_GPIO_DEFSTATE_OFF,
	},
};

static struct gpio_led_platform_data rk29_leds_pdata = {
	.leds = rk29_leds,
	.num_leds = ARRAY_SIZE(rk29_leds),
};

static struct platform_device rk29_device_gpio_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data  = &rk29_leds_pdata,
	},
};
#endif

#ifdef CONFIG_RK_IRDA
#define IRDA_IRQ_PIN           RK30_PIN6_PA1

static int irda_iomux_init(void)
{
	int ret = 0;

	//irda irq pin
	ret = gpio_request(IRDA_IRQ_PIN, NULL);
	if (ret != 0) {
		gpio_free(IRDA_IRQ_PIN);
		printk(">>>>>> IRDA_IRQ_PIN gpio_request err \n ");
	}
	gpio_pull_updown(IRDA_IRQ_PIN, PullDisable);
	gpio_direction_input(IRDA_IRQ_PIN);

	return 0;
}

static int irda_iomux_deinit(void)
{
	gpio_free(IRDA_IRQ_PIN);
	return 0;
}

static struct irda_info rk29_irda_info = {
	.intr_pin = IRDA_IRQ_PIN,
	.iomux_init = irda_iomux_init,
	.iomux_deinit = irda_iomux_deinit,
	//.irda_pwr_ctl = bu92747guw_power_ctl,
};

static struct platform_device irda_device = {
#ifdef CONFIG_RK_IRDA_NET
	.name = "rk_irda",
#else
	.name = "bu92747_irda",
#endif
	.id = -1,
	.dev = {
		.platform_data = &rk29_irda_info,
	}
};
#endif

#ifdef CONFIG_ION
#define ION_RESERVE_SIZE        (80 * SZ_1M)
static struct ion_platform_data rk30_ion_pdata = {
	.nr = 1,
	.heaps = {
		{
			.type = ION_HEAP_TYPE_CARVEOUT,
			.id = ION_NOR_HEAP_ID,
			.name = "norheap",
			.size = ION_RESERVE_SIZE,
		}
	},
};

static struct platform_device device_ion = {
	.name = "ion-rockchip",
	.id = 0,
	.dev = {
		.platform_data = &rk30_ion_pdata,
	},
};
#endif

/**************************************************************************************************
 * SDMMC devices,  include the module of SD,MMC,and sdio.noted by xbw at 2012-03-05
**************************************************************************************************/
#ifdef CONFIG_SDMMC_RK29
#include "board-rk30-sdk-sdmmc.c"

#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
#define SDMMC0_WRITE_PROTECT_PIN	RK30_PIN3_PB7	//According to your own project to set the value of write-protect-pin.
#endif

#if defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
#define SDMMC1_WRITE_PROTECT_PIN	RK30_PIN3_PC7	//According to your own project to set the value of write-protect-pin.
#endif

#define RK29SDK_WIFI_SDIO_CARD_DETECT_N    RK30_PIN6_PB2

#endif //endif ---#ifdef CONFIG_SDMMC_RK29

#ifdef CONFIG_SDMMC0_RK29
static int rk29_sdmmc0_cfg_gpio(void)
{
#ifdef CONFIG_SDMMC_RK29_OLD
	rk30_mux_api_set(GPIO3B1_SDMMC0CMD_NAME, GPIO3B_SDMMC0_CMD);
	rk30_mux_api_set(GPIO3B0_SDMMC0CLKOUT_NAME, GPIO3B_SDMMC0_CLKOUT);
	rk30_mux_api_set(GPIO3B2_SDMMC0DATA0_NAME, GPIO3B_SDMMC0_DATA0);
	rk30_mux_api_set(GPIO3B3_SDMMC0DATA1_NAME, GPIO3B_SDMMC0_DATA1);
	rk30_mux_api_set(GPIO3B4_SDMMC0DATA2_NAME, GPIO3B_SDMMC0_DATA2);
	rk30_mux_api_set(GPIO3B5_SDMMC0DATA3_NAME, GPIO3B_SDMMC0_DATA3);

	rk30_mux_api_set(GPIO3B6_SDMMC0DETECTN_NAME, GPIO3B_GPIO3B6);

	rk30_mux_api_set(GPIO3A7_SDMMC0PWREN_NAME, GPIO3A_GPIO3A7);
	gpio_request(RK30_PIN3_PA7, "sdmmc-power");
	gpio_direction_output(RK30_PIN3_PA7, GPIO_LOW);

#else
	rk29_sdmmc_set_iomux(0, 0xFFFF);

	rk30_mux_api_set(GPIO3B6_SDMMC0DETECTN_NAME, GPIO3B_SDMMC0_DETECT_N);

#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
	gpio_request(SDMMC0_WRITE_PROTECT_PIN, "sdmmc-wp");
	gpio_direction_input(SDMMC0_WRITE_PROTECT_PIN);
#endif

#endif

	return 0;
}

#define CONFIG_SDMMC0_USE_DMA
struct rk29_sdmmc_platform_data default_sdmmc0_data = {
	.host_ocr_avail =
	    (MMC_VDD_25_26 | MMC_VDD_26_27 | MMC_VDD_27_28 | MMC_VDD_28_29 |
	     MMC_VDD_29_30 | MMC_VDD_30_31 | MMC_VDD_31_32 | MMC_VDD_32_33 |
	     MMC_VDD_33_34 | MMC_VDD_34_35 | MMC_VDD_35_36),
	.host_caps =
	    (MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
	.io_init = rk29_sdmmc0_cfg_gpio,

#if !defined(CONFIG_SDMMC_RK29_OLD)
	.set_iomux = rk29_sdmmc_set_iomux,
#endif

	.dma_name = "sd_mmc",
#ifdef CONFIG_SDMMC0_USE_DMA
	.use_dma = 1,
#else
	.use_dma = 0,
#endif
	.detect_irq = RK30_PIN3_PB6,	// INVALID_GPIO
	.enable_sd_wakeup = 0,

#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
	.write_prt = SDMMC0_WRITE_PROTECT_PIN,
#else
	.write_prt = INVALID_GPIO,
#endif
};
#endif // CONFIG_SDMMC0_RK29

#ifdef CONFIG_SDMMC1_RK29
#define CONFIG_SDMMC1_USE_DMA
static int rk29_sdmmc1_cfg_gpio(void)
{
#if defined(CONFIG_SDMMC_RK29_OLD)
	rk30_mux_api_set(GPIO3C0_SMMC1CMD_NAME, GPIO3C_SMMC1_CMD);
	rk30_mux_api_set(GPIO3C5_SDMMC1CLKOUT_NAME, GPIO3C_SDMMC1_CLKOUT);
	rk30_mux_api_set(GPIO3C1_SDMMC1DATA0_NAME, GPIO3C_SDMMC1_DATA0);
	rk30_mux_api_set(GPIO3C2_SDMMC1DATA1_NAME, GPIO3C_SDMMC1_DATA1);
	rk30_mux_api_set(GPIO3C3_SDMMC1DATA2_NAME, GPIO3C_SDMMC1_DATA2);
	rk30_mux_api_set(GPIO3C4_SDMMC1DATA3_NAME, GPIO3C_SDMMC1_DATA3);
	//rk30_mux_api_set(GPIO3C6_SDMMC1DETECTN_NAME, GPIO3C_SDMMC1_DETECT_N);

#else

#if defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
	gpio_request(SDMMC1_WRITE_PROTECT_PIN, "sdio-wp");
	gpio_direction_input(SDMMC1_WRITE_PROTECT_PIN);
#endif

#endif

	return 0;
}

struct rk29_sdmmc_platform_data default_sdmmc1_data = {
	.host_ocr_avail =
	    (MMC_VDD_25_26 | MMC_VDD_26_27 | MMC_VDD_27_28 | MMC_VDD_28_29 |
	     MMC_VDD_29_30 | MMC_VDD_30_31 | MMC_VDD_31_32 | MMC_VDD_32_33 |
	     MMC_VDD_33_34),

#if !defined(CONFIG_USE_SDMMC1_FOR_WIFI_DEVELOP_BOARD)
	.host_caps = (MMC_CAP_4_BIT_DATA | MMC_CAP_SDIO_IRQ |
		      MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
#else
	.host_caps =
	    (MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
#endif

	.io_init = rk29_sdmmc1_cfg_gpio,

#if !defined(CONFIG_SDMMC_RK29_OLD)
	.set_iomux = rk29_sdmmc_set_iomux,
#endif

	.dma_name = "sdio",
#ifdef CONFIG_SDMMC1_USE_DMA
	.use_dma = 1,
#else
	.use_dma = 0,
#endif

#if !defined(CONFIG_USE_SDMMC1_FOR_WIFI_DEVELOP_BOARD)
#ifdef CONFIG_WIFI_CONTROL_FUNC
	.status = rk29sdk_wifi_status,
	.register_status_notify = rk29sdk_wifi_status_register,
#endif
#if 0
	.detect_irq = RK29SDK_WIFI_SDIO_CARD_DETECT_N,
#endif

#if defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
	.write_prt = SDMMC1_WRITE_PROTECT_PIN,
#else
	.write_prt = INVALID_GPIO,
#endif

#else
	.detect_irq = INVALID_GPIO,
	.enable_sd_wakeup = 0,
#endif

};
#endif //endif--#ifdef CONFIG_SDMMC1_RK29

/* bluetooth rfkill device */
static struct platform_device rk29sdk_rfkill = {
        .name = "rk29sdk_rfkill",
        .id = -1,
};

/**************************************************************************************************
 * the end of setting for SDMMC devices
**************************************************************************************************/

/*************td modem sc8800 power control*************/

#if defined(CONFIG_TDSC8800)
#define BP_VOL_PIN			RK30_PIN6_PB2

static int tdsc8800_io_init(void)
{
	
	return 0;
}

static int tdsc8800_io_deinit(void)
{

	return 0;
}

struct rk2818_23d_data rk29_tdsc8800_info = {
	.io_init = tdsc8800_io_init,
    .io_deinit = tdsc8800_io_deinit,
	.bp_power = BP_VOL_PIN,
	.bp_power_active_low = 1,
};
struct platform_device rk29_device_tdsc8800 = {
    .name = "tdsc8800",
	.id = -1,
	.dev		= {
		.platform_data = &rk29_tdsc8800_info,
	}
    };
#endif


static struct platform_device *devices[] __initdata = {
#ifdef CONFIG_BACKLIGHT_RK29_BL
	&rk29_device_backlight,
#endif
#ifdef CONFIG_BACKLIGHT_AW9364
	&aw9364_device_backlight,
#endif
#ifdef CONFIG_FB_ROCKCHIP
	&device_fb,
#endif
#ifdef CONFIG_ION
	&device_ion,
#endif
#ifdef CONFIG_ANDROID_TIMED_GPIO
	&rk29_device_vibrator,
#endif
#ifdef CONFIG_LEDS_GPIO_PLATFORM
	&rk29_device_gpio_leds,
#endif
#ifdef CONFIG_RK_IRDA
	&irda_device,
#endif
#ifdef CONFIG_WIFI_CONTROL_FUNC
	&rk29sdk_wifi_device,
#endif
#ifdef CONFIG_BT
    &rk29sdk_rfkill,
#endif
#ifdef CONFIG_MFD_WM8994
	&wm8994_fixed_voltage0,
	&wm8994_fixed_voltage1,
#endif	
#ifdef CONFIG_RK_HEADSET_DET
    &rk_device_headset,
#endif
#ifdef CONFIG_TDSC8800
	&rk29_device_tdsc8800
#endif
};

// i2c
#ifdef CONFIG_I2C0_RK30
static struct i2c_board_info __initdata i2c0_info[] = {
#if defined (CONFIG_GS_MMA8452)
	{
		.type	        = "gs_mma8452",
		.addr	        = 0x1d,
		.flags	        = 0,
		.irq	        = MMA8452_INT_PIN,
		.platform_data = &mma8452_info,
	},
#endif
#if defined (CONFIG_COMPASS_AK8975)
	{
		.type          = "ak8975",
		.addr          = 0x0d,
		.flags         = 0,
		.irq           = RK30_PIN4_PC1,
		.platform_data = &akm8975_info,
	},
#endif
#if defined (CONFIG_GYRO_L3G4200D)
	{
		.type          = "l3g4200d_gryo",
		.addr          = 0x69,
		.flags         = 0,
		.irq           = L3G4200D_INT_PIN,
		.platform_data = &l3g4200d_info,
	},
#endif
#if defined (CONFIG_MPU_SENSORS_MPU3050) 
	{
		.type			= "mpu3050",
		.addr			= 0x68,
		.flags			= 0,
		.irq			= RK30_PIN4_PC3,
		.platform_data	= &mpu3050_data,
	},
#endif
#if defined (CONFIG_MPU_SENSORS_MMA845X)
	{
		.type    		= "mma845x",
		.addr           = 0x1c,
		.flags			= 0,	
		.irq 			= RK30_PIN4_PC3,
		.platform_data = &inv_mpu_mma845x_data,
	},
#endif
#if defined (CONFIG_MPU_SENSORS_AK8975)
	{
		.type			= "ak8975",
		.addr			= 0x0d,
		.flags			= 0,	
		.irq 			= RK30_PIN4_PC1,
		.platform_data = &inv_mpu_ak8975_data,
	},
#endif

#if defined (CONFIG_SND_SOC_RK1000)
	{
		.type          = "rk1000_i2c_codec",
		.addr          = 0x60,
		.flags         = 0,
	},
	{
		.type          = "rk1000_control",
		.addr          = 0x40,
		.flags         = 0,
	},
#endif
#if defined (CONFIG_SND_SOC_RT5631)
        {
                .type                   = "rt5631",
                .addr                   = 0x1a,
                .flags                  = 0,
        },
#endif

#ifdef CONFIG_MFD_RK610
		{
			.type			= "rk610_ctl",
			.addr			= 0x40,
			.flags			= 0,
		},
#ifdef CONFIG_RK610_TVOUT
		{
			.type			= "rk610_tvout",
			.addr			= 0x42,
			.flags			= 0,
		},
#endif
#ifdef CONFIG_RK610_HDMI
		{
			.type			= "rk610_hdmi",
			.addr			= 0x46,
			.flags			= 0,
			.irq			= RK29_PIN5_PA2,
		},
#endif
#ifdef CONFIG_SND_SOC_RK610
		{//RK610_CODEC addr  from 0x60 to 0x80 (0x60~0x80)
			.type			= "rk610_i2c_codec",
			.addr			= 0x60,
			.flags			= 0,
		},
#endif
#endif
#if defined (CONFIG_SND_SOC_WM8994)
	{
		.type    		= "wm8994",
		.addr           = 0x1a,
		.flags			= 0,
	#if defined(CONFIG_MFD_WM8994)			
		.platform_data  = &wm8994_platform_data,	
	#endif			
	},
#endif

};
#endif

#ifdef CONFIG_I2C1_RK30
#if 0
#include "board-rk30-phone-wm831x.c"

static struct i2c_board_info __initdata i2c1_info[] = {
#if defined (CONFIG_MFD_WM831X_I2C)
	{
		.type          = "wm8310",
		.addr          = 0x34,
		.flags         = 0,
		.irq           = RK30_PIN6_PA4,
		.platform_data = &wm831x_platdata,
	},
#endif
};
#endif
#endif

#ifdef CONFIG_I2C2_RK30
static struct i2c_board_info __initdata i2c2_info[] = {
#if defined (CONFIG_TOUCHSCREEN_ILI2102_IIC)
	{
		.type			= "ili2102_ts",
		.addr			= 0x41,
		.flags			= I2C_M_NEED_DELAY,
		.udelay 	 = 100,
		.irq			= TOUCH_GPIO_INT,
		.platform_data = &ili2102_info,
	},	
#endif

#if defined (CONFIG_TOUCHSCREEN_GT8XX)
	{
		.type          = "Goodix-TS",
		.addr          = 0x55,
		.flags         = 0,
		.irq           = RK30_PIN4_PC2,
		.platform_data = &goodix_info,
	},
#endif
#if defined (CONFIG_LS_CM3217)
	{
		.type          = "lightsensor",
		.addr          = 0x10,
		.flags         = 0,
		.irq           = CM3217_IRQ_PIN,
		.platform_data = &cm3217_info,
	},
#endif
};
#endif

#ifdef CONFIG_I2C3_RK30
static struct i2c_board_info __initdata i2c3_info[] = {
};
#endif

#ifdef CONFIG_I2C4_RK30
static struct i2c_board_info __initdata i2c4_info[] = {
};
#endif

#ifdef CONFIG_I2C_GPIO_RK30
#include "board-rk30-phone-wm831x.c"

#define I2C_SDA_PIN     RK30_PIN2_PD7   //set sda_pin here
#define I2C_SCL_PIN     RK30_PIN2_PD6   //set scl_pin here
static int rk30_i2c_io_init(void)
{
        //set iomux (gpio) here
        rk30_mux_api_set(GPIO2D7_I2C1SCL_NAME, GPIO2D_GPIO2D7);
        rk30_mux_api_set(GPIO2D6_I2C1SDA_NAME, GPIO2D_GPIO2D6);

        return 0;
}
struct i2c_gpio_platform_data default_i2c_gpio_data = {
       .sda_pin = I2C_SDA_PIN,
       .scl_pin = I2C_SCL_PIN,
       .udelay = 5, // clk = 500/udelay = 100Khz
       .timeout = 100,//msecs_to_jiffies(100),
       .bus_num    = 5,
       .io_init = rk30_i2c_io_init,
};
static struct i2c_board_info __initdata i2c_gpio_info[] = {
#if defined (CONFIG_MFD_WM831X_I2C)
		{
			.type		   = "wm8310",
			.addr		   = 0x34,
			.flags		   = 0,
			.irq		   = RK30_PIN6_PA4,
			.platform_data = &wm831x_platdata,
		},
#endif
};
#endif


static void __init rk30_i2c_register_board_info(void)
{
#ifdef CONFIG_I2C0_RK30
	i2c_register_board_info(0, i2c0_info, ARRAY_SIZE(i2c0_info));
#endif
#ifdef CONFIG_I2C1_RK30
	i2c_register_board_info(1, i2c1_info, ARRAY_SIZE(i2c1_info));
#endif
#ifdef CONFIG_I2C2_RK30
	i2c_register_board_info(2, i2c2_info, ARRAY_SIZE(i2c2_info));
#endif
#ifdef CONFIG_I2C3_RK30
	i2c_register_board_info(3, i2c3_info, ARRAY_SIZE(i2c3_info));
#endif
#ifdef CONFIG_I2C4_RK30
	i2c_register_board_info(4, i2c4_info, ARRAY_SIZE(i2c4_info));
#endif
#ifdef CONFIG_I2C_GPIO_RK30
	i2c_register_board_info(5, i2c_gpio_info, ARRAY_SIZE(i2c_gpio_info));
#endif

}
//end of i2c

#define POWER_ON_PIN RK30_PIN6_PB0   //power_hold
static void rk30_pm_power_off(void)
{
	printk(KERN_ERR "rk30_pm_power_off start...\n");
	gpio_direction_output(POWER_ON_PIN, GPIO_LOW);
#if defined(CONFIG_MFD_WM831X)
	wm831x_device_shutdown(Wm831x);//wm8326 shutdown
#endif
	while (1);
}

static void __init machine_rk30_board_init(void)
{
	gpio_request(POWER_ON_PIN, "poweronpin");
	gpio_direction_output(POWER_ON_PIN, GPIO_HIGH);
	
	pm_power_off = rk30_pm_power_off;
	
	rk30_i2c_register_board_info();
	spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
	platform_add_devices(devices, ARRAY_SIZE(devices));
	board_usb_detect_init(RK30_PIN6_PA3);
	
#if defined (CONFIG_MPU_SENSORS_MPU3050)
	rk30_mux_api_set(GPIO4C3_SMCDATA3_TRACEDATA3_NAME, GPIO4C_GPIO4C3);
#endif

#ifdef CONFIG_WIFI_CONTROL_FUNC
	rk29sdk_wifi_bt_gpio_control_init();
#endif
}

static void __init rk30_reserve(void)
{
#ifdef CONFIG_ION
	rk30_ion_pdata.heaps[0].base = board_mem_reserve_add("ion", ION_RESERVE_SIZE);
#endif
#ifdef CONFIG_FB_ROCKCHIP
	resource_fb[0].start = board_mem_reserve_add("fb0", RK30_FB0_MEM_SIZE);
	resource_fb[0].end = resource_fb[0].start + RK30_FB0_MEM_SIZE - 1;
	resource_fb[1].start = board_mem_reserve_add("ipp buf", RK30_FB0_MEM_SIZE);
	resource_fb[1].end = resource_fb[1].start + RK30_FB0_MEM_SIZE - 1;
	resource_fb[2].start = board_mem_reserve_add("fb2", RK30_FB0_MEM_SIZE);
	resource_fb[2].end = resource_fb[2].start + RK30_FB0_MEM_SIZE - 1;
#endif
#ifdef CONFIG_VIDEO_RK29
	rk30_camera_request_reserve_mem();
#endif
	board_mem_reserved();
}

void __init board_clock_init(void)
{
	rk30_clock_data_init(periph_pll_297mhz, codec_pll_360mhz, max_i2s_12288khz);
}

MACHINE_START(RK30, "RK30board")
	.boot_params	= PLAT_PHYS_OFFSET + 0x800,
	.fixup		= rk30_fixup,
	.reserve	= &rk30_reserve,
	.map_io		= rk30_map_io,
	.init_irq	= rk30_init_irq,
	.timer		= &rk30_timer,
	.init_machine	= machine_rk30_board_init,
MACHINE_END
