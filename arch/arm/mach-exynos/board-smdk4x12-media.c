/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/iic.h>
#include <plat/fimc-core.h>
#include <plat/gpio-cfg.h>
#include <plat/clock.h>

#include <mach/exynos-mfc.h>
#include <media/m5mols.h>

#include "board-smdk4x12.h"

#if defined(CONFIG_VIDEO_SAMSUNG_S5P_FIMC)
#include <plat/fimc-core.h>
#include <media/s5p_fimc.h>
#include <media/v4l2-mediabus.h>
#endif
#if defined(CONFIG_VIDEO_S5P_MIPI_CSIS)
#include <plat/mipi_csis.h>
#endif
#if defined(CONFIG_VIDEO_EXYNOS4_FIMC_IS)
#include <media/exynos_fimc_is.h>
#endif
#if defined(CONFIG_VIDEO_S5P_FIMC_LITE)
#include <media/exynos_flite.h>
#endif

#ifdef CONFIG_VIDEO_M5MOLS
static struct m5mols_platform_data m5mols_platdata = {
#ifdef CONFIG_CSI_C
	.gpio_rst = EXYNOS4_GPX1(2), /* ISP_RESET */
#endif
#ifdef CONFIG_CSI_D
	.gpio_rst = EXYNOS4_GPX1(0), /* ISP_RESET */
#endif
	.enable_rst = true, /* positive reset */
	.irq = IRQ_EINT(27),
};

static struct i2c_board_info m5mols_board_info = {
	I2C_BOARD_INFO("M5MOLS", 0x1F),
	.platform_data = &m5mols_platdata,
};
#endif

#if defined CONFIG_VIDEO_EXYNOS4_FIMC_IS
static struct fimc_is_platform_data exynos4_fimc_is_data;

#if defined CONFIG_VIDEO_S5K3H7
static struct fimc_is_sensor_info s5k3h7 = {
	.sensor_name = "S5K3H7",
	.sensor_id = FIMC_IS_SENSOR_ID_S5K3H7,
#if defined CONFIG_S5K3H7_POSITION_FRONT
	.sensor_position = SENSOR_POSITION_FRONT,
#elif defined CONFIG_S5K3H7_POSITION_REAR
	.sensor_position = SENSOR_POSITION_REAR,
#endif
#if defined CONFIG_S5K3H7_CSI_C
	.csi_id = CSI_ID_A,
	.flite_id = FLITE_ID_A,
	.i2c_id = FIMC_IS_I2C_CH_0,
	.gpio_reset = EXYNOS4_GPX1(2), /* Sensor reset */
#elif defined CONFIG_S5K3H7_CSI_D
	.csi_id = CSI_ID_B,
	.flite_id = FLITE_ID_B,
	.i2c_id = FIMC_IS_I2C_CH_1,
	.gpio_reset = EXYNOS4_GPX1(0), /* Sensor reset */
#endif
	.bus_type = FIMC_IS_WB,
};

#endif

#if defined CONFIG_VIDEO_S5K6A3
static struct fimc_is_sensor_info s5k6a3 = {
	.sensor_name = "S5K6A3",
	.sensor_id = FIMC_IS_SENSOR_ID_S5K6A3,
#if defined CONFIG_S5K6A3_POSITION_FRONT
	.sensor_position = SENSOR_POSITION_FRONT,
#elif defined CONFIG_S5K6A3_POSITION_REAR
	.sensor_position = SENSOR_POSITION_REAR,
#endif
#if defined CONFIG_S5K6A3_CSI_C
	.csi_id = CSI_ID_A,
	.flite_id = FLITE_ID_A,
	.i2c_id = FIMC_IS_I2C_CH_0,
	.gpio_reset = EXYNOS4_GPX1(2), /* Sensor reset */
#elif defined CONFIG_S5K6A3_CSI_D
	.csi_id = CSI_ID_B,
	.flite_id = FLITE_ID_B,
	.i2c_id = FIMC_IS_I2C_CH_1,
	.gpio_reset = EXYNOS4_GPX1(0), /* Sensor reset */
#endif
	.bus_type = FIMC_IS_WB,
};
#endif
#endif

#ifdef CONFIG_VIDEO_S5P_MIPI_CSIS
static struct regulator_consumer_supply mipi_csi_fixed_voltage_supplies[] = {
	REGULATOR_SUPPLY("vdd11", "s5p-mipi-csis.0"),
	REGULATOR_SUPPLY("vdd11", "s5p-mipi-csis.1"),
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

#ifdef CONFIG_VIDEO_SAMSUNG_S5P_FIMC
struct platform_device exynos4_fimc_md = {
	.name = "s5p-fimc-md",
	.id = 0,
};
#endif

static struct platform_device *smdk4x12_media_devices[] __initdata = {
#ifdef CONFIG_VIDEO_SAMSUNG_S5P_FIMC
	&exynos4_fimc_md,
#endif
#ifdef CONFIG_S5P_DEV_FIMC0
	&s5p_device_fimc0,
#endif
#ifdef CONFIG_S5P_DEV_FIMC1
	&s5p_device_fimc1,
#endif
#ifdef CONFIG_S5P_DEV_FIMC2
	&s5p_device_fimc2,
#endif
#ifdef CONFIG_S5P_DEV_FIMC3
	&s5p_device_fimc3,
#endif
#ifdef CONFIG_VIDEO_S5P_FIMC_LITE
#ifdef CONFIG_S5P_DEV_FLITE0
	&s5p_device_flite0,
#endif
#ifdef CONFIG_S5P_DEV_FLITE1
	&s5p_device_flite1,
#endif
#endif
	&s5p_device_mfc,
#ifdef CONFIG_VIDEO_S5P_MIPI_CSIS
	&s5p_device_mipi_csis0,
	&s5p_device_mipi_csis1,
	&mipi_csi_fixed_voltage,
#endif
#ifdef CONFIG_VIDEO_M5MOLS
	&m5mols_fixed_voltage,
#endif
#ifdef CONFIG_VIDEO_EXYNOS4_FIMC_IS
	&exynos4_device_fimc_is,
#endif
};

static struct s5p_mfc_platdata smdk4412_mfc_pd = {
	.ip_ver = IP_VER_MFC_4P_1,
	.clock_rate = 200 * MHZ,
};

#ifdef CONFIG_VIDEO_SAMSUNG_S5P_FIMC
static struct s5p_fimc_isp_info isp_info[] = {
#if defined(CONFIG_VIDEO_M5MOLS)
	{
		.board_info	= &m5mols_board_info,
		.is_sensor_info	= NULL,
		.clk_frequency	= 24000000UL,
		.bus_type	= FIMC_MIPI_CSI2,
#ifdef CONFIG_CSI_C
		.i2c_bus_num	= 4,
		.mux_id		= 0, /* A-Port : 0, B-Port : 1 */
		.clk_id		= 0,
#endif
#ifdef CONFIG_CSI_D
		.i2c_bus_num	= 5,
		.mux_id		= 1, /* A-Port : 0, B-Port : 1 */
		.clk_id		= 1,
#endif
		.flags		= 0,
		.csi_data_align	= 32,
		.use_isp	= false,
	},
#endif
#if defined(CONFIG_VIDEO_S5K3H7)
	{
		.board_info	= NULL,
		.is_sensor_info	= &s5k3h7,
		.clk_frequency	= 24000000UL,
		.bus_type	= FIMC_IS_WB,
#ifdef CONFIG_S5K3H7_CSI_C
		.i2c_bus_num	= 0,
		.mux_id		= 0, /* A-Port : 0, B-Port : 1 */
		.clk_id		= 0,
#endif
#ifdef CONFIG_S5K3H7_CSI_D
		.i2c_bus_num	= 1,
		.mux_id		= 1, /* A-Port : 0, B-Port : 1 */
		.clk_id		= 1,
#endif
		.flags		= 0,
		.csi_data_align	= 24,
		.use_isp	= true,
	},
#endif
#if defined(CONFIG_VIDEO_S5K6A3)
	{
		.board_info	= NULL,
		.is_sensor_info	= &s5k6a3,
		.clk_frequency	= 24000000UL,
		.bus_type	= FIMC_IS_WB,
#ifdef CONFIG_S5K6A3_CSI_C
		.i2c_bus_num	= 0,
		.mux_id		= 0, /* A-Port : 0, B-Port : 1 */
		.clk_id		= 0,
#endif
#ifdef CONFIG_S5K6A3_CSI_D
		.i2c_bus_num	= 1,
		.mux_id		= 1, /* A-Port : 0, B-Port : 1 */
		.clk_id		= 1,
#endif
		.flags		= 0,
		.csi_data_align	= 12,
		.use_isp	= true,
	},
#endif
};

static void __init smdk4x12_subdev_config(void)
{
	s5p_fimc_md_platdata.isp_info = &isp_info[0];
	s5p_fimc_md_platdata.num_clients = ARRAY_SIZE(isp_info);
}

static void __init smdk4x12_camera_config(void)
{
	/* CAM A port(b0010) : PCLK, VSYNC, HREF, DATA[0-4] */
	s3c_gpio_cfgrange_nopull(EXYNOS4_GPJ0(0), 8, S3C_GPIO_SFN(2));
	/* CAM A port(b0010) : DATA[5-7], CLKOUT(MIPI CAM also), FIELD */
	s3c_gpio_cfgrange_nopull(EXYNOS4_GPJ1(0), 5, S3C_GPIO_SFN(2));
	/* CAM B port(b0011) : PCLK, DATA[0-6] */
	s3c_gpio_cfgrange_nopull(EXYNOS4_GPM0(0), 8, S3C_GPIO_SFN(3));
	/* CAM B port(b0011) : FIELD, DATA[7]*/
	s3c_gpio_cfgrange_nopull(EXYNOS4_GPM1(0), 2, S3C_GPIO_SFN(3));
	/* CAM B port(b0011) : VSYNC, HREF, CLKOUT*/
	s3c_gpio_cfgrange_nopull(EXYNOS4_GPM2(0), 3, S3C_GPIO_SFN(3));

#ifdef CONFIG_VIDEO_M5MOLS
	s3c_gpio_cfgpin(EXYNOS4_GPX3(3), S3C_GPIO_SFN(0xF));
	s3c_gpio_setpull(EXYNOS4_GPX3(3), S3C_GPIO_PULL_NONE);
#endif
}
#endif

void __init exynos4_smdk4x12_media_init(void)
{
#ifdef CONFIG_VIDEO_EXYNOS4_FIMC_IS
	int index = 0;
#endif
	platform_add_devices(smdk4x12_media_devices,
			ARRAY_SIZE(smdk4x12_media_devices));

	s5p_mfc_set_platdata(&smdk4412_mfc_pd);

	dev_set_name(&s5p_device_mfc.dev, "s5p-mfc");
	clk_add_alias("mfc", "s5p-mfc-v5", "mfc", &s5p_device_mfc.dev);
	clk_add_alias("sclk_mfc", "s5p-mfc-v5", "sclk_mfc",
						&s5p_device_mfc.dev);
	s5p_mfc_setname(&s5p_device_mfc, "s5p-mfc-v5");
#ifdef CONFIG_VIDEO_SAMSUNG_S5P_FIMC
	smdk4x12_camera_config();
	smdk4x12_subdev_config();

	dev_set_name(&s5p_device_fimc0.dev, "exynos4-fimc.0");
	dev_set_name(&s5p_device_fimc1.dev, "exynos4-fimc.1");
	dev_set_name(&s5p_device_fimc2.dev, "exynos4-fimc.2");
	dev_set_name(&s5p_device_fimc3.dev, "exynos4-fimc.3");

	s3c_set_platdata(&s5p_fimc_md_platdata,
			 sizeof(s5p_fimc_md_platdata), &exynos4_fimc_md);
#ifdef CONFIG_EXYNOS_DEV_PD
	s5p_device_fimc0.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s5p_device_fimc1.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s5p_device_fimc2.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s5p_device_fimc3.dev.parent = &exynos4_device_pd[PD_CAM].dev;
#endif
#endif
#ifdef CONFIG_VIDEO_S5P_MIPI_CSIS
	dev_set_name(&s5p_device_mipi_csis0.dev, "s5p-mipi-csis.0");
	dev_set_name(&s5p_device_mipi_csis1.dev, "s5p-mipi-csis.1");

	if (samsung_rev() >= EXYNOS4412_REV_2_0) {
		s5p_mipi_csis0_default_data.clk_rate = 176000000;
		s5p_mipi_csis1_default_data.clk_rate = 176000000;
	}
#if defined(CONFIG_VIDEO_S5K3H7)
	s5p_mipi_csis0_default_data.wclk_source = 1;
	s5p_mipi_csis0_default_data.alignment = 24;
	s5p_mipi_csis0_default_data.hs_settle = 12;
	s5p_mipi_csis0_default_data.lanes = 2;
#elif defined(CONFIG_VIDEO_S5K6A3)
	s5p_mipi_csis1_default_data.wclk_source = 1;
	s5p_mipi_csis1_default_data.alignment = 12;
	s5p_mipi_csis1_default_data.hs_settle = 18;
	s5p_mipi_csis1_default_data.lanes = 1;
#endif
	s3c_set_platdata(&s5p_mipi_csis0_default_data,
			sizeof(s5p_mipi_csis0_default_data),
			&s5p_device_mipi_csis0);
	s3c_set_platdata(&s5p_mipi_csis1_default_data,
			sizeof(s5p_mipi_csis1_default_data),
			&s5p_device_mipi_csis1);

#ifdef CONFIG_EXYNOS_DEV_PD
	s5p_device_mipi_csis0.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s5p_device_mipi_csis1.dev.parent = &exynos4_device_pd[PD_CAM].dev;
#endif
#endif
#ifdef CONFIG_VIDEO_EXYNOS4_FIMC_IS
	dev_set_name(&exynos4_device_fimc_is.dev, FIMC_IS_MODULE_NAME);
#ifdef CONFIG_VIDEO_S5K3H7
	exynos4_fimc_is_data.sensors[index++] = &s5k3h7;
#endif
#ifdef CONFIG_VIDEO_S5K6A3
	exynos4_fimc_is_data.sensors[index++] = &s5k6a3;
#endif
	exynos4_fimc_is_data.num_sensors = index;
	exynos4_fimc_is_set_platdata(&exynos4_fimc_is_data);
#endif
}
