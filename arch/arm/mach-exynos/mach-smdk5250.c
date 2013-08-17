/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/cma.h>
#include <linux/gpio.h>
#include <linux/mmc/host.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/i2c.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/pwm_backlight.h>
#include <linux/input.h>
#include <linux/gpio_event.h>
#include <linux/platform_data/exynos_usb3_drd.h>
#include <linux/persistent_ram.h>
#include <linux/clk.h>
#include <linux/spi/spi.h>

#include <video/platform_lcd.h>
#include <video/s5p-dp.h>

#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>

#include <media/m5mols.h>
#include <media/exynos_gscaler.h>
#include <media/exynos_flite.h>
#include <media/exynos_fimc_is.h>

#include <plat/adc.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/fb.h>
#include <plat/dp.h>
#include <plat/regs-serial.h>
#include <plat/gpio-cfg.h>
#include <plat/backlight.h>
#include <plat/devs.h>
#include <plat/regs-fb-v4.h>
#include <plat/iic.h>
#include <plat/mipi_csis.h>
#include <plat/jpeg.h>
#include <plat/tv-core.h>
#include <plat/ehci.h>
#include <plat/s3c64xx-spi.h>

#include <mach/exynos_fiq_debugger.h>
#include <mach/map.h>
#include <mach/exynos-mfc.h>
#include <mach/tmu.h>
#include <mach/dwmci.h>
#include <mach/ohci.h>
#include <mach/spi-clocks.h>

#include <plat/dsim.h>
#include <plat/mipi_dsi.h>
#include <plat/fimg2d.h>

#include "common.h"

static struct platform_device ramconsole_device = {
	.name           = "ram_console",
	.id             = -1,
};

static struct platform_device persistent_trace_device = {
	.name           = "persistent_trace",
	.id             = -1,
};

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define SMDK5250_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define SMDK5250_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDK5250_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg smdk5250_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= SMDK5250_UCON_DEFAULT,
		.ulcon		= SMDK5250_ULCON_DEFAULT,
		.ufcon		= SMDK5250_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= SMDK5250_UCON_DEFAULT,
		.ulcon		= SMDK5250_ULCON_DEFAULT,
		.ufcon		= SMDK5250_UFCON_DEFAULT,
	},
	[2] = {
#ifndef CONFIG_EXYNOS_FIQ_DEBUGGER
	/*
	 * Don't need to initialize hwport 2, when FIQ debugger is
	 * enabled. Because it will be handled by fiq_debugger.
	 */
		.hwport		= 2,
		.flags		= 0,
		.ucon		= SMDK5250_UCON_DEFAULT,
		.ulcon		= SMDK5250_ULCON_DEFAULT,
		.ufcon		= SMDK5250_UFCON_DEFAULT,
	},
	[3] = {
#endif
		.hwport		= 3,
		.flags		= 0,
		.ucon		= SMDK5250_UCON_DEFAULT,
		.ulcon		= SMDK5250_ULCON_DEFAULT,
		.ufcon		= SMDK5250_UFCON_DEFAULT,
	},
};

static struct gpio_event_direct_entry smdk5250_keypad_key_map[] = {
	{
		.gpio   = EXYNOS5_GPX0(0),
		.code   = KEY_POWER,
	}
};

static struct gpio_event_input_info smdk5250_keypad_key_info = {
	.info.func              = gpio_event_input_func,
	.info.no_suspend        = true,
	.debounce_time.tv64	= 5 * NSEC_PER_MSEC,
	.type                   = EV_KEY,
	.keymap                 = smdk5250_keypad_key_map,
	.keymap_size            = ARRAY_SIZE(smdk5250_keypad_key_map)
};

static struct gpio_event_info *smdk5250_input_info[] = {
	&smdk5250_keypad_key_info.info,
};

static struct gpio_event_platform_data smdk5250_input_data = {
	.names  = {
		"smdk5250-keypad",
		NULL,
	},
	.info           = smdk5250_input_info,
	.info_count     = ARRAY_SIZE(smdk5250_input_info),
};

static struct platform_device smdk5250_input_device = {
	.name   = GPIO_EVENT_DEV_NAME,
	.id     = 0,
	.dev    = {
		.platform_data = &smdk5250_input_data,
	},
};

static void __init smdk5250_gpio_power_init(void)
{
	int err = 0;

	err = gpio_request_one(EXYNOS5_GPX0(0), 0, "GPX0");
	if (err) {
		printk(KERN_ERR "failed to request GPX0 for "
				"suspend/resume control\n");
		return;
	}
	s3c_gpio_setpull(EXYNOS5_GPX0(0), S3C_GPIO_PULL_NONE);

	gpio_free(EXYNOS5_GPX0(0));
}

#ifdef CONFIG_EXYNOS_MEDIA_DEVICE
struct platform_device exynos_device_md0 = {
	.name = "exynos-mdev",
	.id = 0,
};

struct platform_device exynos_device_md1 = {
	.name = "exynos-mdev",
	.id = 1,
};

struct platform_device exynos_device_md2 = {
	.name = "exynos-mdev",
	.id = 2,
};
#endif

#ifdef CONFIG_VIDEO_EXYNOS_FIMC_LITE
#if defined(CONFIG_ITU_A)
static int smdk5250_cam0_reset(int dummy)
{
	int err;
	/* Camera A */
	err = gpio_request(EXYNOS5_GPX1(2), "GPX1");
	if (err)
		printk(KERN_ERR "#### failed to request GPX1_2 ####\n");

	s3c_gpio_setpull(EXYNOS5_GPX1(2), S3C_GPIO_PULL_NONE);
	gpio_direction_output(EXYNOS5_GPX1(2), 0);
	gpio_direction_output(EXYNOS5_GPX1(2), 1);
	gpio_free(EXYNOS5_GPX1(2));

	return 0;
}
#endif
#if defined(CONFIG_ITU_B)
static int smdk5250_cam1_reset(int dummy)
{
	int err;
	/* Camera A */
	err = gpio_request(EXYNOS5_GPX1(0), "GPX1");
	if (err)
		printk(KERN_ERR "#### failed to request GPX1_2 ####\n");

	s3c_gpio_setpull(EXYNOS5_GPX1(0), S3C_GPIO_PULL_NONE);
	gpio_direction_output(EXYNOS5_GPX1(0), 0);
	gpio_direction_output(EXYNOS5_GPX1(0), 1);
	gpio_free(EXYNOS5_GPX1(0));

	return 0;
}
#endif

/* 1 MIPI Cameras */
#ifdef CONFIG_VIDEO_M5MOLS
static struct m5mols_platform_data m5mols_platdata = {
#ifdef CONFIG_CSI_C
	.gpio_rst = EXYNOS5_GPX1(2), /* ISP_RESET */
#endif
#ifdef CONFIG_CSI_D
	.gpio_rst = EXYNOS5_GPX1(0), /* ISP_RESET */
#endif
	.enable_rst = true, /* positive reset */
	.irq = IRQ_EINT(22),
};

static struct i2c_board_info m5mols_board_info = {
	I2C_BOARD_INFO("M5MOLS", 0x1F),
	.platform_data = &m5mols_platdata,
};
#endif
#endif /* CONFIG_VIDEO_EXYNOS_FIMC_LITE */

#ifdef CONFIG_VIDEO_EXYNOS_MIPI_CSIS
static struct regulator_consumer_supply mipi_csi_fixed_voltage_supplies[] = {
	REGULATOR_SUPPLY("mipi_csi", "s5p-mipi-csis.0"),
	REGULATOR_SUPPLY("mipi_csi", "s5p-mipi-csis.1"),
};

static struct regulator_init_data mipi_csi_fixed_voltage_init_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies	= ARRAY_SIZE(mipi_csi_fixed_voltage_supplies),
	.consumer_supplies	= mipi_csi_fixed_voltage_supplies,
};

static struct fixed_voltage_config mipi_csi_fixed_voltage_config = {
	.supply_name	= "DC_5V",
	.microvolts	= 5000000,
	.gpio		= -EINVAL,
	.init_data	= &mipi_csi_fixed_voltage_init_data,
};

static struct platform_device mipi_csi_fixed_voltage = {
	.name		= "reg-fixed-voltage",
	.id		= 3,
	.dev		= {
		.platform_data	= &mipi_csi_fixed_voltage_config,
	},
};
#endif

#ifdef CONFIG_VIDEO_M5MOLS
static struct regulator_consumer_supply m5mols_fixed_voltage_supplies[] = {
	REGULATOR_SUPPLY("core", NULL),
	REGULATOR_SUPPLY("dig_18", NULL),
	REGULATOR_SUPPLY("d_sensor", NULL),
	REGULATOR_SUPPLY("dig_28", NULL),
	REGULATOR_SUPPLY("a_sensor", NULL),
	REGULATOR_SUPPLY("dig_12", NULL),
};

static struct regulator_init_data m5mols_fixed_voltage_init_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies	= ARRAY_SIZE(m5mols_fixed_voltage_supplies),
	.consumer_supplies	= m5mols_fixed_voltage_supplies,
};

static struct fixed_voltage_config m5mols_fixed_voltage_config = {
	.supply_name	= "CAM_SENSOR",
	.microvolts	= 1800000,
	.gpio		= -EINVAL,
	.init_data	= &m5mols_fixed_voltage_init_data,
};

static struct platform_device m5mols_fixed_voltage = {
	.name		= "reg-fixed-voltage",
	.id		= 4,
	.dev		= {
		.platform_data	= &m5mols_fixed_voltage_config,
	},
};
#endif

#if defined CONFIG_VIDEO_EXYNOS5_FIMC_IS
static struct exynos5_platform_fimc_is exynos5_fimc_is_data;

#if defined CONFIG_VIDEO_S5K4E5
static struct exynos5_fimc_is_sensor_info s5k4e5 = {
	.sensor_name = "S5K4E5",
	.sensor_id = SENSOR_NAME_S5K4E5,
#if defined CONFIG_S5K4E5_POSITION_FRONT
	.sensor_position = SENSOR_POSITION_FRONT,
#elif defined CONFIG_S5K4E5_POSITION_REAR
	.sensor_position = SENSOR_POSITION_REAR,
#endif
#if defined CONFIG_S5K4E5_CSI_C
	.csi_id = CSI_ID_A,
	.flite_id = FLITE_ID_A,
	.i2c_channel = SENSOR_CONTROL_I2C0,
#elif defined CONFIG_S5K4E5_CSI_D
	.csi_id = CSI_ID_B,
	.flite_id = FLITE_ID_B,
	.i2c_channel = SENSOR_CONTROL_I2C1,
#endif
	.max_width = 2560,
	.max_height = 1920,
	.max_frame_rate = 30,

	.mipi_lanes = 2,
	.mipi_settle = 12,
	.mipi_align = 24,
	.sensor_gpio = {
		.cfg[0] = {
			.pin = EXYNOS5_GPE0(0),
			.name = "GPE0",
			.value = (2<<0),
			.act = GPIO_PULL_NONE,
		},
		.cfg[1] = {
			.pin = EXYNOS5_GPE0(1),
			.name = "GPE0",
			.value = (2<<4),
			.act = GPIO_PULL_NONE,
		},
		.cfg[2] = {
			.pin = EXYNOS5_GPE0(2),
			.name = "GPE0",
			.value = (3<<8),
			.act = GPIO_PULL_NONE,
		},
		.cfg[3] = {
			.pin = EXYNOS5_GPE0(3),
			.name = "GPE0",
			.value = (3<<12),
			.act = GPIO_PULL_NONE,
		},
		.cfg[4] = {
			.pin = EXYNOS5_GPE0(4),
			.name = "GPE0",
			.value = (3<<16),
			.act = GPIO_PULL_NONE,
		},
		.cfg[5] = {
			.pin = EXYNOS5_GPE0(5),
			.name = "GPE0",
			.value = (3<<20),
			.act = GPIO_PULL_NONE,
		},
		.cfg[6] = {
			.pin = EXYNOS5_GPE0(6),
			.name = "GPE0",
			.value = (3<<24),
			.act = GPIO_PULL_NONE,
		},
		.cfg[7] = {
			.pin = EXYNOS5_GPE0(7),
			.name = "GPE0",
			.value = (3<<28),
			.act = GPIO_PULL_NONE,
		},
		.cfg[8] = {
			.pin = EXYNOS5_GPE1(0),
			.name = "GPE1",
			.value = (3<<0),
			.act = GPIO_PULL_NONE,
		},
		.cfg[9] = {
			.pin = EXYNOS5_GPE1(1),
			.name = "GPE1",
			.value = (3<<4),
			.act = GPIO_PULL_NONE,
		},
		.cfg[10] = {
			.pin = EXYNOS5_GPF0(0),
			.name = "GPF0",
			.value = (2<<0),
			.act = GPIO_PULL_NONE,
		},
		.cfg[11] = {
			.pin = EXYNOS5_GPF0(1),
			.name = "GPF0",
			.value = (2<<4),
			.act = GPIO_PULL_NONE,
		},
		.cfg[12] = {
			.pin = EXYNOS5_GPF0(2),
			.name = "GPF0",
			.value = (2<<8),
			.act = GPIO_PULL_NONE,
		},
		.cfg[13] = {
			.pin = EXYNOS5_GPF0(3),
			.name = "GPF0",
			.value = (2<<12),
			.act = GPIO_PULL_NONE,
		},
		.cfg[14] = {
			.pin = EXYNOS5_GPF1(0),
			.name = "GPF1",
			.value = (3<<0),
			.act = GPIO_PULL_NONE,
		},
		.cfg[15] = {
			.pin = EXYNOS5_GPF1(1),
			.name = "GPF1",
			.value = (3<<4),
			.act = GPIO_PULL_NONE,
		},
		.cfg[16] = {
			.pin = EXYNOS5_GPG2(1),
			.name = "GPG2",
			.value = (2<<4),
			.act = GPIO_PULL_NONE,
		},
		.cfg[17] = {
			.pin = EXYNOS5_GPH0(3),
			.name = "GPH0",
			.value = (2<<12),
			.act = GPIO_PULL_NONE,
		},
		.reset_myself = {
			.pin = EXYNOS5_GPX1(0),
			.name = "GPX1",
			.value = 0,
			.act = GPIO_RESET,

		},
	},
};
#endif

#if defined CONFIG_VIDEO_S5K6A3
static struct exynos5_fimc_is_sensor_info s5k6a3 = {
	.sensor_name = "S5K6A3",
	.sensor_id = SENSOR_NAME_S5K6A3,
#if defined CONFIG_S5K6A3_POSITION_FRONT
	.sensor_position = SENSOR_POSITION_FRONT,
#elif defined CONFIG_S5K6A3_POSITION_REAR
	.sensor_position = SENSOR_POSITION_REAR,
#endif
#if defined CONFIG_S5K6A3_CSI_C
	.csi_id = CSI_ID_A,
	.flite_id = FLITE_ID_A,
	.i2c_channel = SENSOR_CONTROL_I2C0,
#elif defined CONFIG_S5K6A3_CSI_D
	.csi_id = CSI_ID_B,
	.flite_id = FLITE_ID_B,
	.i2c_channel = SENSOR_CONTROL_I2C1,
#endif
	.max_width = 1280,
	.max_height = 720,
	.max_frame_rate = 30,

	.mipi_lanes = 1,
	.mipi_settle = 12,
	.mipi_align = 24,
	.sensor_gpio = {
		.cfg[0] = {
			.pin = EXYNOS5_GPE0(0),
			.name = "GPE0",
			.value = (2<<0),
			.act = GPIO_PULL_NONE,
		},
		.cfg[1] = {
			.pin = EXYNOS5_GPE0(1),
			.name = "GPE0",
			.value = (2<<4),
			.act = GPIO_PULL_NONE,
		},
		.cfg[2] = {
			.pin = EXYNOS5_GPE0(2),
			.name = "GPE0",
			.value = (3<<8),
			.act = GPIO_PULL_NONE,
		},
		.cfg[3] = {
			.pin = EXYNOS5_GPE0(3),
			.name = "GPE0",
			.value = (3<<12),
			.act = GPIO_PULL_NONE,
		},
		.cfg[4] = {
			.pin = EXYNOS5_GPE0(4),
			.name = "GPE0",
			.value = (3<<16),
			.act = GPIO_PULL_NONE,
		},
		.cfg[5] = {
			.pin = EXYNOS5_GPE0(5),
			.name = "GPE0",
			.value = (3<<20),
			.act = GPIO_PULL_NONE,
		},
		.cfg[6] = {
			.pin = EXYNOS5_GPE0(6),
			.name = "GPE0",
			.value = (3<<24),
			.act = GPIO_PULL_NONE,
		},
		.cfg[7] = {
			.pin = EXYNOS5_GPE0(7),
			.name = "GPE0",
			.value = (3<<28),
			.act = GPIO_PULL_NONE,
		},
		.cfg[8] = {
			.pin = EXYNOS5_GPE1(0),
			.name = "GPE1",
			.value = (3<<0),
			.act = GPIO_PULL_NONE,
		},
		.cfg[9] = {
			.pin = EXYNOS5_GPE1(1),
			.name = "GPE1",
			.value = (3<<4),
			.act = GPIO_PULL_NONE,
		},
		.cfg[10] = {
			.pin = EXYNOS5_GPF0(0),
			.name = "GPF0",
			.value = (2<<0),
			.act = GPIO_PULL_NONE,
		},
		.cfg[11] = {
			.pin = EXYNOS5_GPF0(1),
			.name = "GPF0",
			.value = (2<<4),
			.act = GPIO_PULL_NONE,
		},
		.cfg[12] = {
			.pin = EXYNOS5_GPF0(2),
			.name = "GPF0",
			.value = (2<<8),
			.act = GPIO_PULL_NONE,
		},
		.cfg[13] = {
			.pin = EXYNOS5_GPF0(3),
			.name = "GPF0",
			.value = (2<<12),
			.act = GPIO_PULL_NONE,
		},
		.cfg[14] = {
			.pin = EXYNOS5_GPF1(0),
			.name = "GPF1",
			.value = (3<<0),
			.act = GPIO_PULL_NONE,
		},
		.cfg[15] = {
			.pin = EXYNOS5_GPF1(1),
			.name = "GPF1",
			.value = (3<<4),
			.act = GPIO_PULL_NONE,
		},
		.cfg[16] = {
			.pin = EXYNOS5_GPG2(1),
			.name = "GPG2",
			.value = (2<<4),
			.act = GPIO_PULL_NONE,
		},
		.cfg[17] = {
			.pin = EXYNOS5_GPH0(3),
			.name = "GPH0",
			.value = (2<<12),
			.act = GPIO_PULL_NONE,
		},
		.reset_myself = {
			.pin = EXYNOS5_GPX1(2),
			.name = "GPX1",
			.value = 0,
			.act = GPIO_RESET,

		},
	},
};
#endif
#endif
static struct regulator_consumer_supply wm8994_fixed_voltage0_supplies[] = {
	REGULATOR_SUPPLY("AVDD2", "1-001a"),
	REGULATOR_SUPPLY("CPVDD", "1-001a"),
};

static struct regulator_consumer_supply wm8994_fixed_voltage1_supplies[] = {
	REGULATOR_SUPPLY("SPKVDD1", "1-001a"),
	REGULATOR_SUPPLY("SPKVDD2", "1-001a"),
};

static struct regulator_consumer_supply wm8994_fixed_voltage2_supplies =
	REGULATOR_SUPPLY("DBVDD", "1-001a");

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

static struct regulator_init_data wm8994_fixed_voltage2_init_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &wm8994_fixed_voltage2_supplies,
};

static struct fixed_voltage_config wm8994_fixed_voltage0_config = {
	.supply_name	= "VDD_1.8V",
	.microvolts	= 1800000,
	.gpio		= -EINVAL,
	.init_data	= &wm8994_fixed_voltage0_init_data,
};

static struct fixed_voltage_config wm8994_fixed_voltage1_config = {
	.supply_name	= "DC_5V",
	.microvolts	= 5000000,
	.gpio		= -EINVAL,
	.init_data	= &wm8994_fixed_voltage1_init_data,
};

static struct fixed_voltage_config wm8994_fixed_voltage2_config = {
	.supply_name	= "VDD_3.3V",
	.microvolts	= 3300000,
	.gpio		= -EINVAL,
	.init_data	= &wm8994_fixed_voltage2_init_data,
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

static struct platform_device wm8994_fixed_voltage2 = {
	.name		= "reg-fixed-voltage",
	.id		= 2,
	.dev		= {
		.platform_data	= &wm8994_fixed_voltage2_config,
	},
};

static struct regulator_consumer_supply wm8994_avdd1_supply =
	REGULATOR_SUPPLY("AVDD1", "1-001a");

static struct regulator_consumer_supply wm8994_dcvdd_supply =
	REGULATOR_SUPPLY("DCVDD", "1-001a");

static struct regulator_init_data wm8994_ldo1_data = {
	.constraints	= {
		.name		= "AVDD1",
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &wm8994_avdd1_supply,
};

static struct regulator_init_data wm8994_ldo2_data = {
	.constraints	= {
		.name		= "DCVDD",
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &wm8994_dcvdd_supply,
};

static struct wm8994_pdata wm8994_platform_data = {
	/* configure gpio1 function: 0x0001(Logic level input/output) */
	.gpio_defaults[0] = 0x0001,
	/* If the i2s0 and i2s2 is enabled simultaneously */
	.gpio_defaults[7] = 0x8100, /* GPIO8  DACDAT3 in */
	.gpio_defaults[8] = 0x0100, /* GPIO9  ADCDAT3 out */
	.gpio_defaults[9] = 0x0100, /* GPIO10 LRCLK3  out */
	.gpio_defaults[10] = 0x0100,/* GPIO11 BCLK3   out */
	.ldo[0] = { 0, &wm8994_ldo1_data },
	.ldo[1] = { 0, &wm8994_ldo2_data },
};

static struct i2c_board_info i2c_devs0[] __initdata = {
	{
		I2C_BOARD_INFO("s5m87xx", 0xCC >> 1),
		.irq		= IRQ_EINT(26),
	},
};

static struct i2c_board_info i2c_devs1[] __initdata = {
	{
		I2C_BOARD_INFO("wm8994", 0x1a),
		.platform_data	= &wm8994_platform_data,
	},
};

static struct i2c_board_info i2c_devs2[] __initdata = {
	{
		I2C_BOARD_INFO("exynos_hdcp", (0x74 >> 1)),
	},
};

static struct i2c_board_info i2c_devs7[] __initdata = {
	{
		I2C_BOARD_INFO("egalax_i2c", 0x04),
		.irq		= IRQ_EINT(25),
	},
};

/* ADC */
static struct s3c_adc_platdata smdk5250_adc_data __initdata = {
	.phy_init       = s3c_adc_phy_init,
	.phy_exit       = s3c_adc_phy_exit,
};

#if defined(CONFIG_VIDEO_EXYNOS_TV) && defined(CONFIG_VIDEO_EXYNOS_MIXER)
static struct s5p_mxr_platdata mxr_platdata __initdata = {
};
#endif

#if defined(CONFIG_VIDEO_EXYNOS_TV) && defined(CONFIG_VIDEO_EXYNOS_HDMI)
static struct s5p_hdmi_platdata hdmi_platdata __initdata = {
};
#endif

#if defined(CONFIG_VIDEO_EXYNOS_TV) && defined(CONFIG_VIDEO_EXYNOS_HDMI_CEC)
static struct s5p_platform_cec hdmi_cec_data __initdata = {

};
#endif

#ifdef CONFIG_VIDEO_EXYNOS_FIMC_LITE
static void __init smdk5250_camera_gpio_cfg(void)
{
	/* CAM A port(b0010) : PCLK, VSYNC, HREF, CLK_OUT */
	s3c_gpio_cfgrange_nopull(EXYNOS5_GPH0(0), 4, S3C_GPIO_SFN(2));
	/* CAM A port(b0010) : DATA[0-7] */
	s3c_gpio_cfgrange_nopull(EXYNOS5_GPH1(0), 8, S3C_GPIO_SFN(2));
	/* CAM B port(b0010) : PCLK, BAY_RGB[0-6] */
	s3c_gpio_cfgrange_nopull(EXYNOS5_GPG0(0), 8, S3C_GPIO_SFN(2));
	/* CAM B port(b0010) : BAY_Vsync, BAY_RGB[7-13] */
	s3c_gpio_cfgrange_nopull(EXYNOS5_GPG1(0), 8, S3C_GPIO_SFN(2));
	/* CAM B port(b0010) : BAY_Hsync, BAY_MCLK */
	s3c_gpio_cfgrange_nopull(EXYNOS5_GPG2(0), 2, S3C_GPIO_SFN(2));
	/* This is externel interrupt for m5mo */
#ifdef CONFIG_VIDEO_M5MOLS
	s3c_gpio_cfgpin(EXYNOS5_GPX2(6), S3C_GPIO_SFN(0xF));
	s3c_gpio_setpull(EXYNOS5_GPX2(6), S3C_GPIO_PULL_NONE);
#endif
}
#endif

#if defined(CONFIG_VIDEO_EXYNOS_GSCALER) && defined(CONFIG_VIDEO_EXYNOS_FIMC_LITE)
#if defined(CONFIG_VIDEO_M5MOLS)
static struct exynos_isp_info m5mols = {
	.board_info	= &m5mols_board_info,
	.cam_srclk_name	= "xxti",
	.clk_frequency  = 24000000UL,
	.bus_type	= CAM_TYPE_MIPI,
#ifdef CONFIG_CSI_C
	.cam_clk_name	= "sclk_cam0",
	.i2c_bus_num	= 4,
	.cam_port	= CAM_PORT_A, /* A-Port : 0, B-Port : 1 */
#endif
#ifdef CONFIG_CSI_D
	.cam_clk_name	= "sclk_cam1",
	.i2c_bus_num	= 5,
	.cam_port	= CAM_PORT_B, /* A-Port : 0, B-Port : 1 */
#endif
	.flags		= CAM_CLK_INV_PCLK | CAM_CLK_INV_VSYNC,
	.csi_data_align = 32,
};
/* This is for platdata of fimc-lite */
static struct s3c_platform_camera flite_m5mo = {
	.type		= CAM_TYPE_MIPI,
	.use_isp	= true,
	.inv_pclk	= 1,
	.inv_vsync	= 1,
	.inv_href	= 0,
	.inv_hsync	= 0,
};
#endif

static void __set_gsc_camera_config(struct exynos_platform_gscaler *data,
					u32 active_index, u32 preview,
					u32 camcording, u32 max_cam)
{
	data->active_cam_index = active_index;
	data->cam_preview = preview;
	data->cam_camcording = camcording;
	data->num_clients = max_cam;
}

static void __set_flite_camera_config(struct exynos_platform_flite *data,
					u32 active_index, u32 max_cam)
{
	data->active_cam_index = active_index;
	data->num_clients = max_cam;
}

static void __init smdk5250_set_camera_platdata(void)
{
	int gsc_cam_index = 0;
	int flite0_cam_index = 0;
	int flite1_cam_index = 0;
#if defined(CONFIG_VIDEO_M5MOLS)
	exynos_gsc0_default_data.isp_info[gsc_cam_index++] = &m5mols;
#if defined(CONFIG_CSI_C)
	exynos_flite0_default_data.cam[flite0_cam_index] = &flite_m5mo;
	exynos_flite0_default_data.isp_info[flite0_cam_index] = &m5mols;
	flite0_cam_index++;
#endif
#if defined(CONFIG_CSI_D)
	exynos_flite1_default_data.cam[flite1_cam_index] = &flite_m5mo;
	exynos_flite1_default_data.isp_info[flite1_cam_index] = &m5mols;
	flite1_cam_index++;
#endif
#endif
	/* flite platdata register */
	__set_flite_camera_config(&exynos_flite0_default_data, 0, flite0_cam_index);
	__set_flite_camera_config(&exynos_flite1_default_data, 0, flite1_cam_index);

	/* gscaler platdata register */
	/* GSC-0 */
	__set_gsc_camera_config(&exynos_gsc0_default_data, 0, 1, 0, gsc_cam_index);

	/* GSC-1 */
	/* GSC-2 */
	/* GSC-3 */
}
#endif /* CONFIG_VIDEO_EXYNOS_GSCALER */

static int exynos_dwmci0_get_bus_wd(u32 slot_id)
{
	return 8;
}

static void exynos_dwmci0_cfg_gpio(int width)
{
	unsigned int gpio;

	for (gpio = EXYNOS5_GPC0(0); gpio < EXYNOS5_GPC0(2); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	switch (width) {
	case 8:
		for (gpio = EXYNOS5_GPC1(0); gpio <= EXYNOS5_GPC1(3); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
		}
	case 4:
		for (gpio = EXYNOS5_GPC0(3); gpio <= EXYNOS5_GPC0(6); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
		}
		break;
	case 1:
		gpio = EXYNOS5_GPC0(3);
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	default:
		break;
	}
}

static struct dw_mci_board exynos_dwmci0_pdata __initdata = {
	.num_slots		= 1,
	.quirks			= DW_MCI_QUIRK_BROKEN_CARD_DETECTION |
				  DW_MCI_QUIRK_HIGHSPEED |
				  DW_MCI_QUIRK_NO_DETECT_EBIT,
	.bus_hz			= 200 * 1000 * 1000,
	.caps			= MMC_CAP_UHS_DDR50 | MMC_CAP_1_8V_DDR |
				  MMC_CAP_8_BIT_DATA | MMC_CAP_CMD23,
	.caps2			= MMC_CAP2_HS200_1_8V_SDR | MMC_CAP2_PACKED_WR,
	.desc_sz		= 4,
	.fifo_depth             = 0x80,
	.detect_delay_ms	= 200,
	.hclk_name		= "dwmci",
	.cclk_name		= "sclk_dwmci",
	.cfg_gpio		= exynos_dwmci0_cfg_gpio,
	.get_bus_wd		= exynos_dwmci0_get_bus_wd,
	.sdr_timing		= 0x03020001,
	.ddr_timing		= 0x03030002,
	.clk_drv		= 0x3,
};

#ifdef CONFIG_FB_S3C
#if defined(CONFIG_LCD_MIPI_S6E8AB0)
static void mipi_lcd_set_power(struct plat_lcd_data *pd,
				unsigned int power)
{
	/* reset */
	gpio_request_one(EXYNOS5_GPX1(5), GPIOF_OUT_INIT_HIGH, "GPX1");

	mdelay(20);
	if (power) {
		/* fire nRESET on power up */
		gpio_set_value(EXYNOS5_GPX1(5), 0);
		mdelay(20);
		gpio_set_value(EXYNOS5_GPX1(5), 1);
		mdelay(20);
		gpio_free(EXYNOS5_GPX1(5));
	} else {
		/* fire nRESET on power off */
		gpio_set_value(EXYNOS5_GPX1(5), 0);
		mdelay(20);
		gpio_set_value(EXYNOS5_GPX1(5), 1);
		mdelay(20);
		gpio_free(EXYNOS5_GPX1(5));
	}
	mdelay(20);
	/* power */
	gpio_request_one(EXYNOS5_GPX3(0), GPIOF_OUT_INIT_LOW, "GPX3");
	if (power) {
		/* fire nRESET on power up */
		gpio_set_value(EXYNOS5_GPX3(0), 1);
		gpio_free(EXYNOS5_GPX3(0));
	} else {
		/* fire nRESET on power off */
		gpio_set_value(EXYNOS5_GPX3(0), 0);
		gpio_free(EXYNOS5_GPX3(0));
	}

#ifndef CONFIG_BACKLIGHT_PWM
	/* backlight */
	gpio_request_one(EXYNOS5_GPB2(0), GPIOF_OUT_INIT_LOW, "GPB2");
	if (power) {
		/* fire nRESET on power up */
		gpio_set_value(EXYNOS5_GPB2(0), 1);
		gpio_free(EXYNOS5_GPB2(0));
	} else {
		/* fire nRESET on power off */
		gpio_set_value(EXYNOS5_GPB2(0), 0);
		gpio_free(EXYNOS5_GPB2(0));
	}
#endif
}

static struct plat_lcd_data smdk5250_mipi_lcd_data = {
	.set_power	= mipi_lcd_set_power,
};

static struct platform_device smdk5250_mipi_lcd = {
	.name			= "platform-lcd",
	.dev.platform_data	= &smdk5250_mipi_lcd_data,
};

static struct s3c_fb_pd_win smdk5250_fb_win0 = {
	.win_mode = {
		.left_margin	= 0x4,
		.right_margin	= 0x4,
		.upper_margin	= 4,
		.lower_margin	= 4,
		.hsync_len	= 4,
		.vsync_len	= 4,
		.xres		= 1280,
		.yres		= 800,
	},
	.virtual_x		= 1280,
	.virtual_y		= 800 * 2,
	.width			= 223,
	.height			= 125,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_pd_win smdk5250_fb_win1 = {
	.win_mode = {
		.left_margin	= 0x4,
		.right_margin	= 0x4,
		.upper_margin	= 4,
		.lower_margin	= 4,
		.hsync_len	= 4,
		.vsync_len	= 4,
		.xres		= 1280,
		.yres		= 800,
	},
	.virtual_x		= 1280,
	.virtual_y		= 800 * 2,
	.width			= 223,
	.height			= 125,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_pd_win smdk5250_fb_win2 = {
	.win_mode = {
		.left_margin	= 0x4,
		.right_margin	= 0x4,
		.upper_margin	= 4,
		.lower_margin	= 4,
		.hsync_len	= 4,
		.vsync_len	= 4,
		.xres		= 1280,
		.yres		= 800,
	},
	.virtual_x		= 1280,
	.virtual_y		= 800 * 2,
	.width			= 223,
	.height			= 125,
	.max_bpp		= 32,
	.default_bpp		= 24,
};
#elif defined(CONFIG_LCD_MIPI_TC358764)
static void mipi_lcd_set_power(struct plat_lcd_data *pd,
				unsigned int power)
{
	/* reset */
	gpio_request_one(EXYNOS5_GPX1(5), GPIOF_OUT_INIT_HIGH, "GPX1");

	mdelay(20);
	if (power) {
		/* fire nRESET on power up */
		gpio_set_value(EXYNOS5_GPX1(5), 0);
		mdelay(20);
		gpio_set_value(EXYNOS5_GPX1(5), 1);
		mdelay(20);
		gpio_free(EXYNOS5_GPX1(5));
	} else {
		/* fire nRESET on power off */
		gpio_set_value(EXYNOS5_GPX1(5), 0);
		mdelay(20);
		gpio_set_value(EXYNOS5_GPX1(5), 1);
		mdelay(20);
		gpio_free(EXYNOS5_GPX1(5));
	}
	mdelay(20);
	/* power */
	gpio_request_one(EXYNOS5_GPX3(0), GPIOF_OUT_INIT_LOW, "GPX3");
	if (power) {
		/* fire nRESET on power up */
		gpio_set_value(EXYNOS5_GPX3(0), 1);
		gpio_free(EXYNOS5_GPX3(0));
	} else {
		/* fire nRESET on power off */
		gpio_set_value(EXYNOS5_GPX3(0), 0);
		gpio_free(EXYNOS5_GPX3(0));
	}

#ifndef CONFIG_BACKLIGHT_PWM
	/* backlight */
	gpio_request_one(EXYNOS5_GPB2(0), GPIOF_OUT_INIT_LOW, "GPB2");
	if (power) {
		/* fire nRESET on power up */
		gpio_set_value(EXYNOS5_GPB2(0), 1);
		gpio_free(EXYNOS5_GPB2(0));
	} else {
		/* fire nRESET on power off */
		gpio_set_value(EXYNOS5_GPB2(0), 0);
		gpio_free(EXYNOS5_GPB2(0));
	}
#endif
}

static struct plat_lcd_data smdk5250_mipi_lcd_data = {
	.set_power	= mipi_lcd_set_power,
};

static struct platform_device smdk5250_mipi_lcd = {
	.name			= "platform-lcd",
	.dev.platform_data	= &smdk5250_mipi_lcd_data,
};

static struct s3c_fb_pd_win smdk5250_fb_win0 = {
	.win_mode = {
		.left_margin	= 4,
		.right_margin	= 4,
		.upper_margin	= 4,
		.lower_margin	= 4,
		.hsync_len	= 4,
		.vsync_len	= 4,
		.xres		= 1280,
		.yres		= 800,
	},
	.virtual_x		= 1280,
	.virtual_y		= 800 * 2,
	.width			= 223,
	.height			= 125,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_pd_win smdk5250_fb_win1 = {
	.win_mode = {
		.left_margin	= 4,
		.right_margin	= 4,
		.upper_margin	= 4,
		.lower_margin	= 4,
		.hsync_len	= 4,
		.vsync_len	= 4,
		.xres		= 1280,
		.yres		= 800,
	},
	.virtual_x		= 1280,
	.virtual_y		= 800 * 2,
	.width			= 223,
	.height			= 125,
	.max_bpp		= 32,
	.default_bpp		= 24,
};
static struct s3c_fb_pd_win smdk5250_fb_win2 = {
	.win_mode = {
		.left_margin	= 4,
		.right_margin	= 4,
		.upper_margin	= 4,
		.lower_margin	= 4,
		.hsync_len	= 4,
		.vsync_len	= 4,
		.xres		= 1280,
		.yres		= 800,
	},
	.virtual_x		= 1280,
	.virtual_y		= 800 * 2,
	.width			= 223,
	.height			= 125,
	.max_bpp		= 32,
	.default_bpp		= 24,
};
#elif defined(CONFIG_S5P_DP)
static void s5p_dp_backlight_on(void);
static void s5p_dp_backlight_off(void);

static void s5p_lcd_on(void)
{
#ifndef CONFIG_BACKLIGHT_PWM
	/* LCD_PWM_IN_2.8V: LCD_B_PWM, GPB2_0 */
	gpio_request(EXYNOS5_GPB2(0), "GPB2");
#endif
	/* LCD_APS_EN_2.8V: GPD0_6 */
	gpio_request(EXYNOS5_GPD0(6), "GPD0");

	/* LCD_EN: GPD0_5 */
	gpio_request(EXYNOS5_GPD0(5), "GPD0");

	/* LCD_EN: GPD0_5 */
	gpio_direction_output(EXYNOS5_GPD0(5), 1);
	mdelay(20);

	/* LCD_APS_EN_2.8V: GPD0_6 */
	gpio_direction_output(EXYNOS5_GPD0(6), 1);
	mdelay(20);
#ifndef CONFIG_BACKLIGHT_PWM
	/* LCD_PWM_IN_2.8V: LCD_B_PWM, GPB2_0 */
	gpio_direction_output(EXYNOS5_GPB2(0), 1);

	gpio_free(EXYNOS5_GPB2(0));
#endif
	gpio_free(EXYNOS5_GPD0(6));
	gpio_free(EXYNOS5_GPD0(5));
}

static void s5p_lcd_off(void)
{
#ifndef CONFIG_BACKLIGHT_PWM
	/* LCD_PWM_IN_2.8V: LCD_B_PWM, GPB2_0 */
	gpio_request(EXYNOS5_GPB2(0), "GPB2");
#endif
	/* LCD_APS_EN_2.8V: GPD0_6 */
	gpio_request(EXYNOS5_GPD0(6), "GPD0");

	/* LCD_EN: GPD0_5 */
	gpio_request(EXYNOS5_GPD0(5), "GPD0");

	/* LCD_EN: GPD0_5 */
	gpio_direction_output(EXYNOS5_GPD0(5), 0);
	mdelay(20);

	/* LCD_APS_EN_2.8V: GPD0_6 */
	gpio_direction_output(EXYNOS5_GPD0(6), 0);
	mdelay(20);
#ifndef CONFIG_BACKLIGHT_PWM
	/* LCD_PWM_IN_2.8V: LCD_B_PWM, GPB2_0 */
	gpio_direction_output(EXYNOS5_GPB2(0), 0);

	gpio_free(EXYNOS5_GPB2(0));
#endif
	gpio_free(EXYNOS5_GPD0(6));
	gpio_free(EXYNOS5_GPD0(5));
}

static void dp_lcd_set_power(struct plat_lcd_data *pd,
				unsigned int power)
{
	if (power)
		s5p_lcd_on();
	else
		s5p_lcd_off();
}

static struct plat_lcd_data smdk5250_dp_lcd_data = {
	.set_power	= dp_lcd_set_power,
};

static struct platform_device smdk5250_dp_lcd = {
	.name	= "platform-lcd",
	.dev	= {
		.parent		= &s5p_device_fimd1.dev,
		.platform_data	= &smdk5250_dp_lcd_data,
	},
};

static struct s3c_fb_pd_win smdk5250_fb_win2 = {
	.win_mode = {
		.left_margin	= 80,
		.right_margin	= 48,
		.upper_margin	= 37,
		.lower_margin	= 3,
		.hsync_len	= 32,
		.vsync_len	= 6,
		.xres		= 2560,
		.yres		= 1600,
	},
	.virtual_x		= 2560,
	.virtual_y		= 1600 * 2,
	.max_bpp		= 32,
	.default_bpp		= 24,
};
#endif

static struct s3c_fb_platdata smdk5250_lcd1_pdata __initdata = {
#if defined(CONFIG_LCD_MIPI_S6E8AB0)
	.win[0]		= &smdk5250_fb_win0,
	.win[1]		= &smdk5250_fb_win1,
	.win[2]		= &smdk5250_fb_win2,
#elif defined(CONFIG_LCD_MIPI_TC358764)
	.win[0]		= &smdk5250_fb_win0,
	.win[1]		= &smdk5250_fb_win1,
	.win[2]		= &smdk5250_fb_win2,
#elif defined(CONFIG_S5P_DP)
	.win[0]		= &smdk5250_fb_win2,
	.win[1]		= &smdk5250_fb_win2,
	.win[2]		= &smdk5250_fb_win2,
	.win[3]		= &smdk5250_fb_win2,
	.win[4]		= &smdk5250_fb_win2,
#endif
	.default_win	= 2,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
#if defined(CONFIG_LCD_MIPI_S6E8AB0)
	.vidcon1	= VIDCON1_INV_VCLK,
#elif defined(CONFIG_LCD_MIPI_TC358764)
	.vidcon1	= VIDCON1_INV_VCLK,
#elif defined(CONFIG_S5P_DP)
	.vidcon1	= 0,
#endif
	.setup_gpio	= exynos5_fimd1_gpio_setup_24bpp,
	.backlight_off	= s5p_dp_backlight_off,
	.lcd_off	= s5p_lcd_off,
};

#endif

#ifdef CONFIG_FB_MIPI_DSIM
#if defined(CONFIG_LCD_MIPI_S6E8AB0)
static struct mipi_dsim_config dsim_info = {
	.e_interface     = DSIM_VIDEO,
	.e_pixel_format  = DSIM_24BPP_888,
	/* main frame fifo auto flush at VSYNC pulse */
	.auto_flush		= false,
	.eot_disable		= false,
	.auto_vertical_cnt	= true,
	.hse = false,
	.hfp = false,
	.hbp = false,
	.hsa = false,

	.e_no_data_lane	= DSIM_DATA_LANE_4,
	.e_byte_clk	= DSIM_PLL_OUT_DIV8,
	.e_burst_mode	= DSIM_BURST,

	.p = 2,
	.m = 57,
	.s = 1,
	/* D-PHY PLL stable time spec :min = 200usec ~ max 400usec */
	.pll_stable_time = 500,

	.esc_clk = 20 * 1000000,	/* escape clk : 10MHz */

	/* stop state holding counter after bta change count 0 ~ 0xfff */
	.stop_holding_cnt = 0x0fff,
	.bta_timeout = 0xff,		/* bta timeout 0 ~ 0xff */
	.rx_timeout = 0xffff,		/* lp rx timeout 0 ~ 0xffff */

	.dsim_ddi_pd = &s6e8ab0_mipi_lcd_driver,
};
static struct mipi_dsim_lcd_config dsim_lcd_info = {
	.rgb_timing.left_margin  = 0xa,
	.rgb_timing.right_margin = 0xa,
	.rgb_timing.upper_margin = 80,
	.rgb_timing.lower_margin = 48,
	.rgb_timing.hsync_len	 = 5,
	.rgb_timing.vsync_len	 = 32,
	.cpu_timing.cs_setup	 = 0,
	.cpu_timing.wr_setup	 = 1,
	.cpu_timing.wr_act	 = 0,
	.cpu_timing.wr_hold	 = 0,
	.lcd_size.width		 = 1280,
	.lcd_size.height	 = 800,
};
#elif defined (CONFIG_LCD_MIPI_TC358764)
static struct mipi_dsim_config dsim_info = {
	.e_interface	 = DSIM_VIDEO,
	.e_pixel_format  = DSIM_24BPP_888,
	/* main frame fifo auto flush at VSYNC pulse */
	.auto_flush	 = false,
	.eot_disable	 = false,
	.auto_vertical_cnt = false,
	.hse = false,
	.hfp = false,
	.hbp = false,
	.hsa = false,

	.e_no_data_lane  = DSIM_DATA_LANE_4,
	.e_byte_clk	 = DSIM_PLL_OUT_DIV8,
	.e_burst_mode	 = DSIM_BURST,

	.p = 3,
	.m = 115,
	.s = 1,

	/* D-PHY PLL stable time spec :min = 200usec ~ max 400usec */
	.pll_stable_time = 500,

	.esc_clk = 0.4 * 1000000,	/* escape clk : 10MHz */

	/* stop state holding counter after bta change count 0 ~ 0xfff */
	.stop_holding_cnt	= 0x0f,
	.bta_timeout		= 0xff,		/* bta timeout 0 ~ 0xff */
	.rx_timeout		= 0xffff,	/* lp rx timeout 0 ~ 0xffff */

	.dsim_ddi_pd = &tc358764_mipi_lcd_driver,
};

static struct mipi_dsim_lcd_config dsim_lcd_info = {
	.rgb_timing.left_margin  = 0x4,
	.rgb_timing.right_margin = 0x4,
	.rgb_timing.upper_margin = 0x4,
	.rgb_timing.lower_margin = 0x4,
	.rgb_timing.hsync_len	 = 0x4,
	.rgb_timing.vsync_len	 = 0x4,
	.cpu_timing.cs_setup	 = 0,
	.cpu_timing.wr_setup	 = 1,
	.cpu_timing.wr_act	 = 0,
	.cpu_timing.wr_hold	 = 0,
	.lcd_size.width		 = 1280,
	.lcd_size.height	 = 800,
};
#endif

static struct s5p_platform_mipi_dsim dsim_platform_data = {
	.clk_name		= "dsim0",
	.dsim_config		= &dsim_info,
	.dsim_lcd_config	= &dsim_lcd_info,

	.part_reset		= s5p_dsim_part_reset,
	.init_d_phy		= s5p_dsim_init_d_phy,
	.get_fb_frame_done	= NULL,
	.trigger		= NULL,
	/*
	* the stable time of needing to write data on SFR
	* when the mipi mode becomes LP mode.
	*/
	.delay_for_stabilization = 600,
};
#endif

#ifdef CONFIG_S5P_DP
static struct video_info smdk5250_dp_config = {
	.name			= "WQXGA(2560x1600) LCD, for SMDK TEST",

	.h_sync_polarity	= 0,
	.v_sync_polarity	= 0,
	.interlaced		= 0,

	.color_space		= COLOR_RGB,
	.dynamic_range		= VESA,
	.ycbcr_coeff		= COLOR_YCBCR601,
	.color_depth		= COLOR_8,

	.link_rate		= LINK_RATE_2_70GBPS,
	.lane_count		= LANE_COUNT4,
};

static void s5p_dp_backlight_on(void)
{
	/* LED_BACKLIGHT_RESET: GPX1_5 */
	gpio_request(EXYNOS5_GPX1(5), "GPX1");

	gpio_direction_output(EXYNOS5_GPX1(5), 1);
	mdelay(20);

	gpio_free(EXYNOS5_GPX1(5));
}

static void s5p_dp_backlight_off(void)
{
	/* LED_BACKLIGHT_RESET: GPX1_5 */
	gpio_request(EXYNOS5_GPX1(5), "GPX1");

	gpio_direction_output(EXYNOS5_GPX1(5), 0);
	mdelay(20);

	gpio_free(EXYNOS5_GPX1(5));
}

static struct s5p_dp_platdata smdk5250_dp_data __initdata = {
	.video_info	= &smdk5250_dp_config,
	.phy_init	= s5p_dp_phy_init,
	.phy_exit	= s5p_dp_phy_exit,
	.backlight_on	= s5p_dp_backlight_on,
	.backlight_off  = s5p_dp_backlight_off,
	.lcd_on		= s5p_lcd_on,
	.lcd_off	= s5p_lcd_off,
};
#endif

#ifdef CONFIG_VIDEO_EXYNOS_FIMG2D
static struct fimg2d_platdata fimg2d_data __initdata = {
	.ip_ver		= IP_VER_G2D_5G,
	.hw_ver		= 0x42,
	.gate_clkname	= "fimg2d",
};
#endif

/* LCD Backlight data */
static struct samsung_bl_gpio_info smdk5250_bl_gpio_info = {
	.no	= EXYNOS5_GPB2(0),
	.func	= S3C_GPIO_SFN(2),
};

static struct platform_pwm_backlight_data smdk5250_bl_data = {
	.pwm_id		= 0,
	.pwm_period_ns	= 30000,
};

/* DEVFREQ controlling mif */
static struct platform_device exynos_bus_mif_devfreq = {
	.name                   = "exynos5-bus-mif",
};

/* DEVFREQ controlling int */
static struct platform_device exynos_bus_int_devfreq = {
	.name                   = "exynos5-bus-int",
};


static struct platform_device *smdk5250_devices[] __initdata = {
	&ramconsole_device,
	&persistent_trace_device,
	&s3c_device_rtc,
	&s3c_device_i2c0,
	&s3c_device_i2c1,
	&s3c_device_i2c2,
	&s3c_device_i2c4,
	&s3c_device_i2c5,
	&s3c_device_i2c7,
	&s3c_device_adc,
	&s3c_device_wdt,
	&smdk5250_input_device,
#ifdef CONFIG_VIDEO_EXYNOS_MFC
	&s5p_device_mfc,
#endif
#ifdef CONFIG_EXYNOS_DEV_TMU
	&exynos_device_tmu,
#endif
#ifdef CONFIG_EXYNOS_MEDIA_DEVICE
	&exynos_device_md0,
	&exynos_device_md1,
	&exynos_device_md2,
#endif
#ifdef CONFIG_VIDEO_EXYNOS5_FIMC_IS
	&exynos5_device_fimc_is,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_GSCALER
	&exynos5_device_gsc0,
	&exynos5_device_gsc1,
	&exynos5_device_gsc2,
	&exynos5_device_gsc3,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_LITE
	&exynos_device_flite0,
	&exynos_device_flite1,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_MIPI_CSIS
	&s5p_device_mipi_csis0,
	&s5p_device_mipi_csis1,
	&mipi_csi_fixed_voltage,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_FIMG2D
	&s5p_device_fimg2d,
#endif
	&exynos5_device_rotator,
#ifdef CONFIG_VIDEO_M5MOLS
	&m5mols_fixed_voltage,
#endif
	&exynos5_device_dwmci0,
#ifdef CONFIG_FB_S3C
#ifdef CONFIG_FB_MIPI_DSIM
	&smdk5250_mipi_lcd,
	&s5p_device_mipi_dsim,
#endif
	&s5p_device_fimd1,
#ifdef CONFIG_S5P_DP
	&s5p_device_dp,
	&smdk5250_dp_lcd,
#endif
#endif
#ifdef CONFIG_VIDEO_EXYNOS_TV
#ifdef CONFIG_VIDEO_EXYNOS_HDMI
	&s5p_device_hdmi,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_HDMIPHY
	&s5p_device_i2c_hdmiphy,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_MIXER
	&s5p_device_mixer,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_HDMI_CEC
	&s5p_device_cec,
#endif
#endif
	&wm8994_fixed_voltage0,
	&wm8994_fixed_voltage1,
	&wm8994_fixed_voltage2,
	&samsung_asoc_dma,
	&samsung_asoc_idma,
	&exynos5_device_srp,
	&exynos5_device_i2s0,
	&exynos5_device_pcm0,
	&exynos5_device_spdif,
	&s5p_device_ehci,
	&exynos4_device_ohci,
	&exynos_device_ss_udc,
#ifdef CONFIG_VIDEO_EXYNOS_JPEG
	&s5p_device_jpeg,
#endif
#ifdef CONFIG_S5P_DEV_ACE
	&s5p_device_ace,
#endif
	&s3c64xx_device_spi0,
	&s3c64xx_device_spi1,
	&s3c64xx_device_spi2,
	&exynos_bus_mif_devfreq,
	&exynos_bus_int_devfreq,
#ifdef CONFIG_MALI_T6XX
	&exynos5_device_g3d,
#endif
};

/* TMU */
static struct tmu_data smdk5250_tmu_pdata __initdata = {
	.ts = {
		.stop_throttle		= 78,
		.start_throttle		= 80,
		.start_tripping		= 110,
		.start_emergency	= 120,
		.stop_mem_throttle	= 80,
		.start_mem_throttle	= 85,
	},

	.efuse_value	= 80,
	.slope		= 0x10608802,
};

#if defined(CONFIG_CMA)
/* defined in arch/arm/mach-exynos/reserve-mem.c */
extern void exynos_cma_region_reserve(struct cma_region *,
				struct cma_region *, size_t, const char *);
static void __init exynos_reserve_mem(void)
{
	static struct cma_region regions[] = {
		{
			.name = "ion",
#ifdef CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE
			.size = CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE * SZ_1K,
#endif
			.start = 0
		},
#ifdef CONFIG_AUDIO_SAMSUNG_MEMSIZE_SRP
		{
			.name = "srp",
			.size = CONFIG_AUDIO_SAMSUNG_MEMSIZE_SRP * SZ_1K,
			.start = 0,
		},
#endif
#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
#ifdef CONFIG_ION_EXYNOS_DRM_MFC_SH
		{
			.name = "drm_mfc_sh",
			.size = SZ_1M,
		},
#endif
#ifdef CONFIG_ION_EXYNOS_DRM_MSGBOX_SH
		{
			.name = "drm_msgbox_sh",
			.size = SZ_1M,
		},
#endif
#endif
		{
			.size = 0
		},
	};
#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
	static struct cma_region regions_secure[] = {
#ifdef CONFIG_ION_EXYNOS_DRM_MEMSIZE_FIMD_VIDEO
	       {
		       .name = "drm_fimd_video",
		       .size = CONFIG_ION_EXYNOS_DRM_MEMSIZE_FIMD_VIDEO *
			       SZ_1K,
	       },
#endif
#ifdef CONFIG_ION_EXYNOS_DRM_MEMSIZE_MFC_OUTPUT
	       {
		       .name = "drm_mfc_output",
		       .size = CONFIG_ION_EXYNOS_DRM_MEMSIZE_MFC_OUTPUT *
			       SZ_1K,
	       },
#endif
#ifdef CONFIG_ION_EXYNOS_DRM_MEMSIZE_MFC_INPUT
	       {
		       .name = "drm_mfc_input",
		       .size = CONFIG_ION_EXYNOS_DRM_MEMSIZE_MFC_INPUT *
			       SZ_1K,
	       },
#endif
#ifdef CONFIG_ION_EXYNOS_DRM_MFC_FW
		{
			.name = "drm_mfc_fw",
			.size = SZ_1M,
		},
#endif
#ifdef CONFIG_ION_EXYNOS_DRM_SECTBL
		{
			.name = "drm_sectbl",
			.size = SZ_1M,
		},
#endif
		{
			.size = 0
		},
	};
#else /* !CONFIG_EXYNOS_CONTENT_PATH_PROTECTION */
	struct cma_region *regions_secure = NULL;
#endif /* CONFIG_EXYNOS_CONTENT_PATH_PROTECTION */
	static const char map[] __initconst =
#ifdef CONFIG_EXYNOS_C2C
		"samsung-c2c=c2c_shdmem;"
#endif
		"s3cfb.0=fimd;exynos5-fb.1=fimd;"
		"samsung-rp=srp;"
		"exynos-gsc.0=gsc0;exynos-gsc.1=gsc1;exynos-gsc.2=gsc2;exynos-gsc.3=gsc3;"
		"exynos-fimc-lite.0=flite0;exynos-fimc-lite.1=flite1;"
#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
		"ion-exynos/mfc_sh=drm_mfc_sh;"
		"ion-exynos/msgbox_sh=drm_msgbox_sh;"
		"ion-exynos/fimd_video=drm_fimd_video;"
		"ion-exynos/mfc_output=drm_mfc_output;"
		"ion-exynos/mfc_input=drm_mfc_input;"
		"ion-exynos/mfc_fw=drm_mfc_fw;"
		"ion-exynos/sectbl=drm_sectbl;"
		"s5p-smem/mfc_sh=drm_mfc_sh;"
		"s5p-smem/msgbox_sh=drm_msgbox_sh;"
		"s5p-smem/fimd_video=drm_fimd_video;"
		"s5p-smem/mfc_output=drm_mfc_output;"
		"s5p-smem/mfc_input=drm_mfc_input;"
		"s5p-smem/mfc_fw=drm_mfc_fw;"
		"s5p-smem/sectbl=drm_sectbl;"
#endif
		"ion-exynos=ion;"
		"exynos-rot=rot;"
		"s5p-mfc-v6/f=fw;"
		"s5p-mfc-v6/a=b1;"
		"s5p-mixer=tv;"
		"exynos5-fimc-is=fimc_is;";

	exynos_cma_region_reserve(regions, regions_secure, 0, map);
}
#else /* !CONFIG_CMA*/
static inline void exynos_reserve_mem(void)
{
}
#endif

/* USB EHCI */
static struct s5p_ehci_platdata smdk5250_ehci_pdata;

static void __init smdk5250_ehci_init(void)
{
	struct s5p_ehci_platdata *pdata = &smdk5250_ehci_pdata;

	s5p_ehci_set_platdata(pdata);
}

/* USB OHCI */
static struct exynos4_ohci_platdata smdk5250_ohci_pdata;

static void __init smdk5250_ohci_init(void)
{
	struct exynos4_ohci_platdata *pdata = &smdk5250_ohci_pdata;

	exynos4_ohci_set_platdata(pdata);
}

static struct exynos_usb3_drd_pdata smdk5250_ss_udc_pdata;

static void __init smdk5250_ss_udc_init(void)
{
	struct exynos_usb3_drd_pdata *pdata = &smdk5250_ss_udc_pdata;

	exynos_ss_udc_set_platdata(pdata);
}

static void __init smdk5250_dwmci_init(void)
{
	exynos_dwmci_set_platdata(&exynos_dwmci0_pdata, 0);
	dev_set_name(&exynos5_device_dwmci0.dev, "exynos4-sdhci.0");
	clk_add_alias("dwmci", "dw_mmc.0", "hsmmc", &exynos5_device_dwmci0.dev);
	clk_add_alias("sclk_dwmci", "dw_mmc.0", "sclk_mmc",
		      &exynos5_device_dwmci0.dev);
}

#if defined(CONFIG_VIDEO_EXYNOS_MFC)
static struct s5p_mfc_platdata smdk5250_mfc_pd = {
	.clock_rate = 333 * MHZ,
};
#endif

static struct s3c64xx_spi_csinfo spi0_csi[] = {
	[0] = {
		.line		= EXYNOS5_GPA2(1),
		.set_level	= gpio_set_value,
		.fb_delay	= 0x2,
	},
};

static struct spi_board_info spi0_board_info[] __initdata = {
	{
		.modalias		= "spidev",
		.platform_data		= NULL,
		.max_speed_hz		= 10 * 1000 * 1000,
		.bus_num		= 0,
		.chip_select		= 0,
		.mode			= SPI_MODE_0,
		.controller_data	= &spi0_csi[0],
	}
};

static struct s3c64xx_spi_csinfo spi1_csi[] = {
	[0] = {
		.line		= EXYNOS5_GPA2(5),
		.set_level	= gpio_set_value,
		.fb_delay	= 0x2,
	},
};

static struct spi_board_info spi1_board_info[] __initdata = {
	{
		.modalias		= "spidev",
		.platform_data		= NULL,
		.max_speed_hz		= 10 * 1000 * 1000,
		.bus_num		= 1,
		.chip_select		= 0,
		.mode			= SPI_MODE_0,
		.controller_data	= &spi1_csi[0],
	}
};

static struct s3c64xx_spi_csinfo spi2_csi[] = {
	[0] = {
		.line		= EXYNOS5_GPB1(2),
		.set_level	= gpio_set_value,
		.fb_delay	= 0x2,
	},
};

static struct spi_board_info spi2_board_info[] __initdata = {
	{
		.modalias		= "spidev",
		.platform_data		= NULL,
		.max_speed_hz		= 10 * 1000 * 1000,
		.bus_num		= 2,
		.chip_select		= 0,
		.mode			= SPI_MODE_0,
		.controller_data	= &spi2_csi[0],
	}
};

static void __init smdk5250_map_io(void)
{
	clk_xusbxti.rate = 24000000;
	clk_xxti.rate = 24000000;
	exynos_init_io(NULL, 0);
	s3c24xx_init_clocks(clk_xusbxti.rate);
	s3c24xx_init_uarts(smdk5250_uartcfgs, ARRAY_SIZE(smdk5250_uartcfgs));
}

static struct persistent_ram_descriptor smdk5250_prd[] __initdata = {
	{
		.name = "ram_console",
		.size = SZ_2M,
	},
#ifdef CONFIG_PERSISTENT_TRACER
	{
		.name = "persistent_trace",
		.size = SZ_1M,
	},
#endif
};

static struct persistent_ram smdk5250_pr __initdata = {
	.descs = smdk5250_prd,
	.num_descs = ARRAY_SIZE(smdk5250_prd),
	.start = PLAT_PHYS_OFFSET + SZ_1G + SZ_512M,
#ifdef CONFIG_PERSISTENT_TRACER
	.size = 3 * SZ_1M,
#else
	.size = SZ_2M,
#endif
};

static void __init smdk5250_init_early(void)
{
	persistent_ram_early_init(&smdk5250_pr);
}

static void __init smdk5250_machine_init(void)
{
#ifdef CONFIG_EXYNOS_FIQ_DEBUGGER
	exynos_serial_debug_init(2, 0);
#endif

	s3c_i2c0_set_platdata(NULL);
	i2c_register_board_info(0, i2c_devs0, ARRAY_SIZE(i2c_devs0));

	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, i2c_devs1, ARRAY_SIZE(i2c_devs1));

	s3c_i2c2_set_platdata(NULL);
	i2c_register_board_info(2, i2c_devs2, ARRAY_SIZE(i2c_devs2));

	s3c_i2c4_set_platdata(NULL);
	s3c_i2c5_set_platdata(NULL);
	s3c_i2c7_set_platdata(NULL);
	i2c_register_board_info(7, i2c_devs7, ARRAY_SIZE(i2c_devs7));

	s3c_adc_set_platdata(&smdk5250_adc_data);

	smdk5250_dwmci_init();

#ifdef CONFIG_VIDEO_EXYNOS_MFC
	s5p_mfc_set_platdata(&smdk5250_mfc_pd);
#endif
	samsung_bl_set(&smdk5250_bl_gpio_info, &smdk5250_bl_data);

#ifdef CONFIG_FB_S3C
	s5p_fimd1_set_platdata(&smdk5250_lcd1_pdata);
	dev_set_name(&s5p_device_fimd1.dev, "exynos5-fb.1");
	clk_add_alias("lcd", "exynos5-fb.1", "fimd", &s5p_device_fimd1.dev);
#endif
#ifdef CONFIG_S5P_DP
	s5p_dp_set_platdata(&smdk5250_dp_data);
#endif
#ifdef CONFIG_FB_MIPI_DSIM
	s5p_dsim_set_platdata(&dsim_platform_data);
#endif
#ifdef CONFIG_EXYNOS_DEV_TMU
	exynos_tmu_set_platdata(&smdk5250_tmu_pdata);
#endif
	smdk5250_gpio_power_init();

	smdk5250_ehci_init();
	smdk5250_ohci_init();
	smdk5250_ss_udc_init();

	platform_add_devices(smdk5250_devices, ARRAY_SIZE(smdk5250_devices));

#ifdef CONFIG_FB_S3C
#if defined(CONFIG_S5P_DP)
	exynos5_fimd1_setup_clock(&s5p_device_fimd1.dev,
			"sclk_fimd", "sclk_vpll", 268 * MHZ);
#else
	exynos5_fimd1_setup_clock(&s5p_device_fimd1.dev, "sclk_fimd", "mout_mpll_user",
				800 * MHZ);
#endif
#endif
#ifdef CONFIG_VIDEO_EXYNOS_MIPI_CSIS
	s3c_set_platdata(&s5p_mipi_csis0_default_data,
			sizeof(s5p_mipi_csis0_default_data), &s5p_device_mipi_csis0);
	s3c_set_platdata(&s5p_mipi_csis1_default_data,
			sizeof(s5p_mipi_csis1_default_data), &s5p_device_mipi_csis1);
#endif
#ifdef CONFIG_VIDEO_EXYNOS_FIMG2D
	s5p_fimg2d_set_platdata(&fimg2d_data);
#endif
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_LITE
	smdk5250_camera_gpio_cfg();
	smdk5250_set_camera_platdata();
	s3c_set_platdata(&exynos_flite0_default_data,
			sizeof(exynos_flite0_default_data), &exynos_device_flite0);
	s3c_set_platdata(&exynos_flite1_default_data,
			sizeof(exynos_flite1_default_data), &exynos_device_flite1);
/* In EVT0, for using camclk, gscaler clock should be enabled */
	dev_set_name(&exynos_device_flite0.dev, "exynos-gsc.0");
	clk_add_alias("gscl", "exynos-fimc-lite.0", "gscl",
			&exynos_device_flite0.dev);
	dev_set_name(&exynos_device_flite0.dev, "exynos-fimc-lite.0");

	dev_set_name(&exynos_device_flite1.dev, "exynos-gsc.0");
	clk_add_alias("gscl", "exynos-fimc-lite.1", "gscl",
			&exynos_device_flite1.dev);
	dev_set_name(&exynos_device_flite1.dev, "exynos-fimc-lite.1");
#endif
#ifdef CONFIG_VIDEO_EXYNOS5_FIMC_IS
	dev_set_name(&exynos5_device_fimc_is.dev, "s5p-mipi-csis.0");
	clk_add_alias("gscl_wrap0", FIMC_IS_MODULE_NAME,
			"gscl_wrap0", &exynos5_device_fimc_is.dev);
	clk_add_alias("sclk_gscl_wrap0", FIMC_IS_MODULE_NAME,
			"sclk_gscl_wrap0", &exynos5_device_fimc_is.dev);

	dev_set_name(&exynos5_device_fimc_is.dev, "s5p-mipi-csis.1");
	clk_add_alias("gscl_wrap1", FIMC_IS_MODULE_NAME,
			"gscl_wrap1", &exynos5_device_fimc_is.dev);
	clk_add_alias("sclk_gscl_wrap1", FIMC_IS_MODULE_NAME,
			"sclk_gscl_wrap1", &exynos5_device_fimc_is.dev);

	dev_set_name(&exynos5_device_fimc_is.dev, "exynos-gsc.0");
	clk_add_alias("gscl", FIMC_IS_MODULE_NAME,
			"gscl", &exynos5_device_fimc_is.dev);
	dev_set_name(&exynos5_device_fimc_is.dev, FIMC_IS_MODULE_NAME);

#if defined CONFIG_VIDEO_S5K6A3
	exynos5_fimc_is_data.sensor_info[s5k6a3.sensor_position] = &s5k6a3;
#endif
#if defined CONFIG_VIDEO_S5K4E5
	exynos5_fimc_is_data.sensor_info[s5k4e5.sensor_position] = &s5k4e5;
#endif

	exynos5_fimc_is_set_platdata(&exynos5_fimc_is_data);
#endif
#ifdef CONFIG_VIDEO_EXYNOS_GSCALER
	s3c_set_platdata(&exynos_gsc0_default_data, sizeof(exynos_gsc0_default_data),
			&exynos5_device_gsc0);
	s3c_set_platdata(&exynos_gsc1_default_data, sizeof(exynos_gsc1_default_data),
			&exynos5_device_gsc1);
	s3c_set_platdata(&exynos_gsc2_default_data, sizeof(exynos_gsc2_default_data),
			&exynos5_device_gsc2);
	s3c_set_platdata(&exynos_gsc3_default_data, sizeof(exynos_gsc3_default_data),
			&exynos5_device_gsc3);
#endif
#ifdef CONFIG_VIDEO_EXYNOS_JPEG
	exynos5_jpeg_setup_clock(&s5p_device_jpeg.dev, 150000000);
#endif
#if defined(CONFIG_VIDEO_EXYNOS_TV)
	mxr_platdata.ip_ver = IP_VER_TV_5G_1;
	hdmi_platdata.ip_ver = IP_VER_TV_5G_1;

	dev_set_name(&s5p_device_hdmi.dev, "exynos5-hdmi");
	clk_add_alias("hdmi", "s5p-hdmi", "hdmi", &s5p_device_hdmi.dev);

	/* direct HPD to HDMI chip */
	gpio_request(EXYNOS5_GPX3(7), "hpd-plug");
	gpio_direction_input(EXYNOS5_GPX3(7));
	s3c_gpio_cfgpin(EXYNOS5_GPX3(7), S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(EXYNOS5_GPX3(7), S3C_GPIO_PULL_NONE);

	/* HDMI CEC */
	gpio_request(EXYNOS5_GPX3(6), "hdmi-cec");
	gpio_direction_input(EXYNOS5_GPX3(6));
	s3c_gpio_cfgpin(EXYNOS5_GPX3(6), S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(EXYNOS5_GPX3(6), S3C_GPIO_PULL_NONE);

#if defined(CONFIG_VIDEO_EXYNOS_HDMIPHY)
	s5p_hdmi_set_platdata(&hdmi_platdata);
	s5p_i2c_hdmiphy_set_platdata(NULL);
#endif
#ifdef CONFIG_VIDEO_EXYNOS_HDMI_CEC
	s5p_hdmi_cec_set_platdata(&hdmi_cec_data);
#endif
#endif

	exynos_spi_clock_setup(&s3c64xx_device_spi0.dev, 0);
	exynos_spi_clock_setup(&s3c64xx_device_spi1.dev, 1);
	exynos_spi_clock_setup(&s3c64xx_device_spi2.dev, 2);

	if (!exynos_spi_cfg_cs(spi0_csi[0].line, 0)) {
		s3c64xx_spi0_set_platdata(&s3c64xx_spi0_pdata,
				EXYNOS_SPI_SRCCLK_SCLK, ARRAY_SIZE(spi0_csi));

		spi_register_board_info(spi0_board_info,
				ARRAY_SIZE(spi0_board_info));
	} else {
		pr_err("%s: Error requesting gpio for SPI-CH0 CS\n", __func__);
	}

	if (!exynos_spi_cfg_cs(spi1_csi[0].line, 1)) {
		s3c64xx_spi1_set_platdata(&s3c64xx_spi1_pdata,
				EXYNOS_SPI_SRCCLK_SCLK, ARRAY_SIZE(spi1_csi));

		spi_register_board_info(spi1_board_info,
				ARRAY_SIZE(spi1_board_info));
	} else {
		pr_err("%s: Error requesting gpio for SPI-CH1 CS\n", __func__);
	}

	if (!exynos_spi_cfg_cs(spi2_csi[0].line, 2)) {
		s3c64xx_spi2_set_platdata(&s3c64xx_spi2_pdata,
				EXYNOS_SPI_SRCCLK_SCLK, ARRAY_SIZE(spi2_csi));

		spi_register_board_info(spi2_board_info,
				ARRAY_SIZE(spi2_board_info));
	} else {
		pr_err("%s: Error requesting gpio for SPI-CH2 CS\n", __func__);
	}
}

MACHINE_START(SMDK5250, "SMDK5250")
	.atag_offset	= 0x100,
	.init_early	= smdk5250_init_early,
	.init_irq	= exynos5_init_irq,
	.map_io		= smdk5250_map_io,
	.handle_irq	= gic_handle_irq,
	.init_machine	= smdk5250_machine_init,
	.timer		= &exynos4_timer,
	.restart	= exynos5_restart,
	.reserve	= exynos_reserve_mem,
MACHINE_END
