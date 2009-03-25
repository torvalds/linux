/*
 * linux/arch/arm/mach-omap2/board-rx51-flash.c
 *
 * Copyright (C) 2008-2009 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/i2c/twl4030.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/regulator/machine.h>
#include <linux/gpio.h>

#include <mach/mcspi.h>
#include <mach/mux.h>
#include <mach/board.h>
#include <mach/common.h>
#include <mach/dma.h>
#include <mach/gpmc.h>
#include <mach/keypad.h>

#include "mmc-twl4030.h"


#define SMC91X_CS			1
#define SMC91X_GPIO_IRQ			54
#define SMC91X_GPIO_RESET		164
#define SMC91X_GPIO_PWRDWN		86

static struct resource rx51_smc91x_resources[] = {
	[0] = {
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.flags		= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

static struct platform_device rx51_smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rx51_smc91x_resources),
	.resource	= rx51_smc91x_resources,
};

static int rx51_keymap[] = {
	KEY(0, 0, KEY_Q),
	KEY(0, 1, KEY_W),
	KEY(0, 2, KEY_E),
	KEY(0, 3, KEY_R),
	KEY(0, 4, KEY_T),
	KEY(0, 5, KEY_Y),
	KEY(0, 6, KEY_U),
	KEY(0, 7, KEY_I),
	KEY(1, 0, KEY_O),
	KEY(1, 1, KEY_D),
	KEY(1, 2, KEY_DOT),
	KEY(1, 3, KEY_V),
	KEY(1, 4, KEY_DOWN),
	KEY(2, 0, KEY_P),
	KEY(2, 1, KEY_F),
	KEY(2, 2, KEY_UP),
	KEY(2, 3, KEY_B),
	KEY(2, 4, KEY_RIGHT),
	KEY(3, 0, KEY_COMMA),
	KEY(3, 1, KEY_G),
	KEY(3, 2, KEY_ENTER),
	KEY(3, 3, KEY_N),
	KEY(4, 0, KEY_BACKSPACE),
	KEY(4, 1, KEY_H),
	KEY(4, 3, KEY_M),
	KEY(4, 4, KEY_LEFTCTRL),
	KEY(5, 1, KEY_J),
	KEY(5, 2, KEY_Z),
	KEY(5, 3, KEY_SPACE),
	KEY(5, 4, KEY_LEFTSHIFT),
	KEY(6, 0, KEY_A),
	KEY(6, 1, KEY_K),
	KEY(6, 2, KEY_X),
	KEY(6, 3, KEY_SPACE),
	KEY(6, 4, KEY_FN),
	KEY(7, 0, KEY_S),
	KEY(7, 1, KEY_L),
	KEY(7, 2, KEY_C),
	KEY(7, 3, KEY_LEFT),
	KEY(0xff, 0, KEY_F6),
	KEY(0xff, 1, KEY_F7),
	KEY(0xff, 2, KEY_F8),
	KEY(0xff, 4, KEY_F9),
	KEY(0xff, 5, KEY_F10),
};

static struct twl4030_keypad_data rx51_kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap		= rx51_keymap,
	.keymapsize	= ARRAY_SIZE(rx51_keymap),
	.rep		= 1,
};

static struct platform_device *rx51_peripherals_devices[] = {
	&rx51_smc91x_device,
};

/*
 * Timings are taken from smsc-lan91c96-ms.pdf
 */
static int smc91x_init_gpmc(int cs)
{
	struct gpmc_timings t;
	const int t2_r = 45;		/* t2 in Figure 12.10 */
	const int t2_w = 30;		/* t2 in Figure 12.11 */
	const int t3 = 15;		/* t3 in Figure 12.10 */
	const int t5_r = 0;		/* t5 in Figure 12.10 */
	const int t6_r = 45;		/* t6 in Figure 12.10 */
	const int t6_w = 0;		/* t6 in Figure 12.11 */
	const int t7_w = 15;		/* t7 in Figure 12.11 */
	const int t15 = 12;		/* t15 in Figure 12.2 */
	const int t20 = 185;		/* t20 in Figure 12.2 */

	memset(&t, 0, sizeof(t));

	t.cs_on = t15;
	t.cs_rd_off = t3 + t2_r + t5_r;	/* Figure 12.10 */
	t.cs_wr_off = t3 + t2_w + t6_w;	/* Figure 12.11 */
	t.adv_on = t3;			/* Figure 12.10 */
	t.adv_rd_off = t3 + t2_r;	/* Figure 12.10 */
	t.adv_wr_off = t3 + t2_w;	/* Figure 12.11 */
	t.oe_off = t3 + t2_r + t5_r;	/* Figure 12.10 */
	t.oe_on = t.oe_off - t6_r;	/* Figure 12.10 */
	t.we_off = t3 + t2_w + t6_w;	/* Figure 12.11 */
	t.we_on = t.we_off - t7_w;	/* Figure 12.11 */
	t.rd_cycle = t20;		/* Figure 12.2 */
	t.wr_cycle = t20;		/* Figure 12.4 */
	t.access = t3 + t2_r + t5_r;	/* Figure 12.10 */
	t.wr_access = t3 + t2_w + t6_w;	/* Figure 12.11 */

	gpmc_cs_write_reg(cs, GPMC_CS_CONFIG1, GPMC_CONFIG1_DEVICESIZE_16);

	return gpmc_cs_set_timings(cs, &t);
}

static void __init rx51_init_smc91x(void)
{
	unsigned long cs_mem_base;
	int ret;

	omap_cfg_reg(U8_34XX_GPIO54_DOWN);
	omap_cfg_reg(G25_34XX_GPIO86_OUT);
	omap_cfg_reg(H19_34XX_GPIO164_OUT);

	if (gpmc_cs_request(SMC91X_CS, SZ_16M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed to request GPMC mem for smc91x\n");
		return;
	}

	rx51_smc91x_resources[0].start = cs_mem_base + 0x300;
	rx51_smc91x_resources[0].end = cs_mem_base + 0x30f;

	smc91x_init_gpmc(SMC91X_CS);

	if (gpio_request(SMC91X_GPIO_IRQ, "SMC91X irq") < 0)
		goto free1;

	gpio_direction_input(SMC91X_GPIO_IRQ);
	rx51_smc91x_resources[1].start = gpio_to_irq(SMC91X_GPIO_IRQ);

	ret = gpio_request(SMC91X_GPIO_PWRDWN, "SMC91X powerdown");
	if (ret)
		goto free2;
	gpio_direction_output(SMC91X_GPIO_PWRDWN, 0);

	ret = gpio_request(SMC91X_GPIO_RESET, "SMC91X reset");
	if (ret)
		goto free3;
	gpio_direction_output(SMC91X_GPIO_RESET, 0);
	gpio_set_value(SMC91X_GPIO_RESET, 1);
	msleep(100);
	gpio_set_value(SMC91X_GPIO_RESET, 0);

	return;

free3:
	gpio_free(SMC91X_GPIO_PWRDWN);
free2:
	gpio_free(SMC91X_GPIO_IRQ);
free1:
	gpmc_cs_free(SMC91X_CS);

	printk(KERN_ERR "Could not initialize smc91x\n");
}

static struct twl4030_madc_platform_data rx51_madc_data = {
	.irq_line		= 1,
};

static struct twl4030_hsmmc_info mmc[] = {
	{
		.name		= "external",
		.mmc		= 1,
		.wires		= 4,
		.cover_only	= true,
		.gpio_cd	= 160,
		.gpio_wp	= -EINVAL,
	},
	{
		.name		= "internal",
		.mmc		= 2,
		.wires		= 8,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
	{}	/* Terminator */
};

static struct regulator_consumer_supply rx51_vmmc1_supply = {
	.supply			= "vmmc",
};

static struct regulator_consumer_supply rx51_vmmc2_supply = {
	.supply			= "vmmc",
};

static struct regulator_consumer_supply rx51_vsim_supply = {
	.supply			= "vmmc_aux",
};

static struct regulator_init_data rx51_vaux1 = {
	.constraints = {
		.name			= "V28",
		.min_uV			= 2800000,
		.max_uV			= 2800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data rx51_vaux2 = {
	.constraints = {
		.name			= "VCSI",
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

/* VAUX3 - adds more power to VIO_18 rail */
static struct regulator_init_data rx51_vaux3 = {
	.constraints = {
		.name			= "VCAM_DIG_18",
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data rx51_vaux4 = {
	.constraints = {
		.name			= "VCAM_ANA_28",
		.min_uV			= 2800000,
		.max_uV			= 2800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data rx51_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &rx51_vmmc1_supply,
};

static struct regulator_init_data rx51_vmmc2 = {
	.constraints = {
		.name			= "VMMC2_30",
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &rx51_vmmc2_supply,
};

static struct regulator_init_data rx51_vsim = {
	.constraints = {
		.name			= "VMMC2_IO_18",
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &rx51_vsim_supply,
};

static struct regulator_init_data rx51_vdac = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static int rx51_twlgpio_setup(struct device *dev, unsigned gpio, unsigned n)
{
	/* FIXME this gpio setup is just a placeholder for now */
	gpio_request(gpio + 6, "backlight_pwm");
	gpio_direction_output(gpio + 6, 0);
	gpio_request(gpio + 7, "speaker_en");
	gpio_direction_output(gpio + 7, 1);

	/* set up MMC adapters, linking their regulators to them */
	twl4030_mmc_init(mmc);
	rx51_vmmc1_supply.dev = mmc[0].dev;
	rx51_vmmc2_supply.dev = mmc[1].dev;
	rx51_vsim_supply.dev = mmc[1].dev;

	return 0;
}

static struct twl4030_gpio_platform_data rx51_gpio_data = {
	.gpio_base		= OMAP_MAX_GPIO_LINES,
	.irq_base		= TWL4030_GPIO_IRQ_BASE,
	.irq_end		= TWL4030_GPIO_IRQ_END,
	.pulldowns		= BIT(0) | BIT(1) | BIT(2) | BIT(3)
				| BIT(4) | BIT(5)
				| BIT(8) | BIT(9) | BIT(10) | BIT(11)
				| BIT(12) | BIT(13) | BIT(14) | BIT(15)
				| BIT(16) | BIT(17) ,
	.setup			= rx51_twlgpio_setup,
};

static struct twl4030_platform_data rx51_twldata = {
	.irq_base		= TWL4030_IRQ_BASE,
	.irq_end		= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.gpio			= &rx51_gpio_data,
	.keypad			= &rx51_kp_data,
	.madc			= &rx51_madc_data,

	.vaux1			= &rx51_vaux1,
	.vaux2			= &rx51_vaux2,
	.vaux3			= &rx51_vaux3,
	.vaux4			= &rx51_vaux4,
	.vmmc1			= &rx51_vmmc1,
	.vmmc2			= &rx51_vmmc2,
	.vsim			= &rx51_vsim,
	.vdac			= &rx51_vdac,
};

static struct i2c_board_info __initdata rx51_peripherals_i2c_board_info_1[] = {
	{
		I2C_BOARD_INFO("twl5030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &rx51_twldata,
	},
};

static int __init rx51_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, rx51_peripherals_i2c_board_info_1,
			ARRAY_SIZE(rx51_peripherals_i2c_board_info_1));
	omap_register_i2c_bus(2, 100, NULL, 0);
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}


void __init rx51_peripherals_init(void)
{
	platform_add_devices(rx51_peripherals_devices,
				ARRAY_SIZE(rx51_peripherals_devices));
	rx51_i2c_init();
	rx51_init_smc91x();
}

