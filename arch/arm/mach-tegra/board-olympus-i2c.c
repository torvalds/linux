/*
 * arch/arm/mach-tegra/board-olympus-i2c.c
 *
 * Copyright (C) 2010 Google, Inc.
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

#include <linux/resource.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <asm/mach-types.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/pinmux.h>
#include <linux/qtouch_obp_ts.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include "gpio-names.h"

#define OLYMPUS_TOUCH_IRQ_GPIO TEGRA_GPIO_PF5
#define OLYMPUS_COMPASS_IRQ_GPIO TEGRA_GPIO_PE2

static int sholes_touch_reset(void)
{
	return 0;
}

static struct vkey sholes_touch_vkeys[] = {
	{
		.code		= KEY_BACK,
	},
	{
		.code		= KEY_MENU,
	},
	{
		.code		= KEY_HOME,
	},
	{
		.code		= KEY_SEARCH,
	},
};

static struct qtm_touch_keyarray_cfg sholes_key_array_data[] = {
	{
		.ctrl = 0,
		.x_origin = 0,
		.y_origin = 0,
		.x_size = 0,
		.y_size = 0,
		.aks_cfg = 0,
		.burst_len = 0,
		.tch_det_thr = 0,
		.tch_det_int = 0,
		.rsvd1 = 0,
		.rsvd2 = 0,
	},
	{
		.ctrl = 0,
		.x_origin = 0,
		.y_origin = 0,
		.x_size = 0,
		.y_size = 0,
		.aks_cfg = 0,
		.burst_len = 0,
		.tch_det_thr = 0,
		.tch_det_int = 0,
		.rsvd1 = 0,
		.rsvd2 = 0,
	},
};

static struct qtouch_ts_platform_data sholes_ts_platform_data = {
	.irqflags	= IRQF_TRIGGER_LOW,
	.flags		= (QTOUCH_SWAP_XY |
			   QTOUCH_FLIP_X |
			   QTOUCH_USE_MULTITOUCH |
			   QTOUCH_CFG_BACKUPNV |
			   QTOUCH_EEPROM_CHECKSUM),
	.abs_min_x	= 0,
	.abs_max_x	= 1024,
	.abs_min_y	= 0,
	.abs_max_y	= 1024,
	.abs_min_p	= 0,
	.abs_max_p	= 255,
	.abs_min_w	= 0,
	.abs_max_w	= 15,
	.x_delta	= 400,
	.y_delta	= 250,
	.nv_checksum	= 0xfaf5,
	.fuzz_x		= 0,
	.fuzz_y		= 0,
	.fuzz_p		= 2,
	.fuzz_w		= 2,
	.hw_reset	= sholes_touch_reset,
	.power_cfg	= {
		.idle_acq_int	= 0xff,
		.active_acq_int	= 0xff,
		.active_idle_to	= 0x01,
	},
	.acquire_cfg	= {
		.charge_time	= 12,
		.atouch_drift	= 5,
		.touch_drift	= 20,
		.drift_susp	= 20,
		.touch_autocal	= 0x96,
		.sync		= 0,
	},
	.multi_touch_cfg	= {
		.ctrl		= 0x0b,
		.x_origin	= 0,
		.y_origin	= 0,
		.x_size		= 19,
		.y_size		= 11,
		.aks_cfg	= 0,
		.burst_len	= 0x40,
		.tch_det_thr	= 0x12,
		.tch_det_int	= 0x2,
		.mov_hyst_init	= 0xe,
		.mov_hyst_next	= 0xe,
		.mov_filter	= 0x9,
		.num_touch	= 2,
		.merge_hyst	= 0,
		.merge_thresh	= 3,
		.amp_hyst = 2,
		 .x_res = 0x0000,
		 .y_res = 0x0000,
		 .x_low_clip = 0x00,
		 .x_high_clip = 0x00,
		 .y_low_clip = 0x00,
		 .y_high_clip = 0x00,
	},
    .linear_tbl_cfg = {
		  .ctrl = 0x01,
		  .x_offset = 0x0000,
		  .x_segment = {
			  0x48, 0x3f, 0x3c, 0x3E,
			  0x3f, 0x3e, 0x3e, 0x3e,
			  0x3f, 0x42, 0x41, 0x3f,
			  0x41, 0x40, 0x41, 0x46
		  },
		  .y_offset = 0x0000,
		  .y_segment = {
			  0x44, 0x38, 0x37, 0x3e,
			  0x3e, 0x41, 0x41, 0x3f,
			  0x42, 0x41, 0x42, 0x42,
			  0x41, 0x3f, 0x41, 0x45
		  },
	  },
	.grip_suppression_cfg = {
		.ctrl		= 0x00,
		.xlogrip	= 0x00,
		.xhigrip	= 0x00,
		.ylogrip	= 0x00,
		.yhigrip	= 0x00,
		.maxtchs	= 0x00,
		.reserve0   = 0x00,
		.szthr1	= 0x00,
		.szthr2	= 0x00,
		.shpthr1	= 0x00,
		.shpthr2	= 0x00,
	},
	.noise1_suppression_cfg = {
		.ctrl = 0x01,
		.reserved = 0x01,
		.atchthr = 0x64,
		.duty_cycle = 0x08,
	},
	.key_array      = {
		.cfg		= sholes_key_array_data,
		.num_keys   = ARRAY_SIZE(sholes_key_array_data),
	},
	.vkeys			= {
		.keys		= sholes_touch_vkeys,
		.count		= ARRAY_SIZE(sholes_touch_vkeys),
		.start		= 961,
	},
};

static struct i2c_board_info __initdata sholes_i2c_bus1_board_info[] = {
	{
		I2C_BOARD_INFO(QTOUCH_TS_NAME, 0x4a),
		.platform_data = &sholes_ts_platform_data,
		.irq = TEGRA_GPIO_TO_IRQ(OLYMPUS_TOUCH_IRQ_GPIO),
	},
};

static struct i2c_board_info __initdata olympus_i2c_bus4_board_info[] = {
        {
                I2C_BOARD_INFO("akm8973", 0x0C),
                .irq = TEGRA_GPIO_TO_IRQ(OLYMPUS_COMPASS_IRQ_GPIO),
        },
};

static struct resource i2c_resource1[] = {
	[0] = {
		.start  = INT_I2C,
		.end    = INT_I2C,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_I2C_BASE,
		.end	= TEGRA_I2C_BASE + TEGRA_I2C_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource i2c_resource2[] = {
	[0] = {
		.start  = INT_I2C2,
		.end    = INT_I2C2,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_I2C2_BASE,
		.end	= TEGRA_I2C2_BASE + TEGRA_I2C2_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource i2c_resource3[] = {
	[0] = {
		.start  = INT_I2C3,
		.end    = INT_I2C3,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_I2C3_BASE,
		.end	= TEGRA_I2C3_BASE + TEGRA_I2C3_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource i2c_resource4[] = {
	[0] = {
		.start  = INT_DVC,
		.end    = INT_DVC,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_DVC_BASE,
		.end	= TEGRA_DVC_BASE + TEGRA_DVC_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device tegra_i2c_device1 = {
	.name		= "tegra-i2c",
	.id		= 0,
	.resource	= i2c_resource1,
	.num_resources	= ARRAY_SIZE(i2c_resource1),
	.dev = {
		.platform_data = 0,
	},
};

static struct platform_device tegra_i2c_device2 = {
	.name		= "tegra-i2c",
	.id		= 1,
	.resource	= i2c_resource2,
	.num_resources	= ARRAY_SIZE(i2c_resource2),
	.dev = {
		.platform_data = 0,
	},
};

static struct platform_device tegra_i2c_device3 = {
	.name		= "tegra-i2c",
	.id		= 2,
	.resource	= i2c_resource3,
	.num_resources	= ARRAY_SIZE(i2c_resource3),
	.dev = {
		.platform_data = 0,
	},
};

static struct platform_device tegra_i2c_device4 = {
	.name		= "tegra-i2c",
	.id		= 3,
	.resource	= i2c_resource4,
	.num_resources	= ARRAY_SIZE(i2c_resource4),
	.dev = {
		.platform_data = 0,
	},
};

static int __init olympus_init_i2c(void)
{
	int ret;

	if (!machine_is_olympus())
		return 0;

	ret = platform_device_register(&tegra_i2c_device1);
	ret = platform_device_register(&tegra_i2c_device2);
	ret = platform_device_register(&tegra_i2c_device3);
	ret = platform_device_register(&tegra_i2c_device4);

	tegra_gpio_enable(OLYMPUS_TOUCH_IRQ_GPIO);
	gpio_request(OLYMPUS_TOUCH_IRQ_GPIO, "touch_irq");
	gpio_direction_input(OLYMPUS_TOUCH_IRQ_GPIO);

	i2c_register_board_info(0, sholes_i2c_bus1_board_info, 1);

	i2c_register_board_info(3, olympus_i2c_bus4_board_info, 1);
	if (ret != 0)
		return ret;

	return 0;
}

device_initcall(olympus_init_i2c);

