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
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/clkdev.h>
#include <media/exynos_gscaler.h>
#include <media/exynos_flite.h>
#include <media/m5mols.h>
#include <linux/spi/spi.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/iic.h>
#include <plat/tv-core.h>
#include <plat/mipi_csis.h>
#include <plat/gpio-cfg.h>
#include <plat/fimg2d.h>
#include <plat/jpeg.h>
#include <plat/s3c64xx-spi.h>
#include <mach/spi-clocks.h>

#include <mach/hs-iic.h>
#include <mach/exynos-tv.h>
#include <mach/exynos-mfc.h>
#include <mach/exynos-scaler.h>

#include <media/exynos_fimc_is.h>
#include <media/exynos_fimc_is_sensor.h>
#include <media/s5k4ecgx_platform.h>

#include "board-odroidxu.h"

#ifdef CONFIG_VIDEO_EXYNOS_FIMC_LITE
/* 1 MIPI Cameras */
#ifdef CONFIG_VIDEO_S5K4ECGX
static int s5k4ecgx_power(int enable)
{
	int err = 0;

	err = gpio_request(EXYNOS5410_GPX3(3), "cam_reset");
	if (err)
		printk(KERN_ERR "#### failed to request GPX3_1 ####\n");

	s3c_gpio_setpull(EXYNOS5410_GPX3(3), S3C_GPIO_PULL_NONE);

	if(enable) {
		gpio_direction_output(EXYNOS5410_GPX3(3), 0);
		mdelay(200);
		gpio_direction_output(EXYNOS5410_GPX3(3), 1);
		mdelay(50);
	}
	else {
		gpio_direction_output(EXYNOS5410_GPX3(3), 0);
	}
	gpio_free(EXYNOS5410_GPX3(3));

	return 0;
}

static struct s5k4ecgx_platform_data s5k4ecgx_plat = {
	.default_width = 640,
	.default_height = 480,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000UL,

	.is_mipi = 1,
	.streamoff_delay = 100,
	.set_power = s5k4ecgx_power,
};

static struct i2c_board_info hs_i2c_devs1[] __initdata = {
	{
		I2C_BOARD_INFO("S5K4ECGX", 0x5a>>1),
		.platform_data = &s5k4ecgx_plat,
	},
};
#endif
#endif /* CONFIG_VIDEO_EXYNOS_FIMC_LITE */

#ifdef CONFIG_VIDEO_EXYNOS_MIPI_CSIS
static struct regulator_consumer_supply mipi_csi_fixed_voltage_supplies[] = {
	REGULATOR_SUPPLY("mipi_csi", "s5p-mipi-csis.0"),
	REGULATOR_SUPPLY("mipi_csi", "s5p-mipi-csis.1"),
	REGULATOR_SUPPLY("mipi_csi", "s5p-mipi-csis.2"),
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
#ifdef CONFIG_VIDEO_EXYNOS5_FIMC_IS
static struct exynos5_platform_fimc_is exynos5_fimc_is_data;
#endif

#ifdef CONFIG_VIDEO_EXYNOS_HDMI_CEC
static struct s5p_platform_cec hdmi_cec_data __initdata = {
};
#endif

#if defined(CONFIG_VIDEO_EXYNOS_TV) && defined(CONFIG_VIDEO_EXYNOS_MIXER)
	static struct s5p_mxr_platdata mxr_platdata __initdata = {
	};
#endif

#if defined(CONFIG_VIDEO_EXYNOS_TV) && defined(CONFIG_VIDEO_EXYNOS_HDMI)
static struct s5p_hdmi_platdata hdmi_platdata __initdata = {
};
#endif

#ifdef CONFIG_VIDEO_EXYNOS_TV

#define		GPIO_I2C2_SDA	EXYNOS5410_GPA0(6)
#define		GPIO_I2C2_SCL	EXYNOS5410_GPA0(7)

static struct 	i2c_gpio_platform_data 	i2c2_gpio_platdata = {
	.sda_pin = GPIO_I2C2_SDA,   // gpio number
	.scl_pin = GPIO_I2C2_SCL,
	.udelay  = 10,               // 50KHz
	.sda_is_open_drain = 0,
	.scl_is_open_drain = 0,
	.scl_is_output_only = 0
};

static struct 	platform_device 	i2c2_gpio_device = {
	.name 	= "i2c-gpio",
	.id  	= 2,    // adepter number
	.dev.platform_data = &i2c2_gpio_platdata,
};

static struct i2c_board_info i2c_devs2[] __initdata = {
	{
		I2C_BOARD_INFO("exynos_hdcp", (0x74 >> 1)),
	},
	{
		I2C_BOARD_INFO("exynos_edid", (0xA0 >> 1)),
	},
};
#endif

#ifdef CONFIG_VIDEO_EXYNOS_MFC
#ifdef CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ
static struct s5p_mfc_qos smdk5410_mfc_qos_table[] = {
	[0] = {
		.thrd_mb	= 0,
		.freq_int	= 160000,
		.freq_mif	= 400000,
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
		.freq_cpu	= 0,
#else
		.freq_cpu	= 0,
		.freq_kfc	= 0,
#endif
	},
	[1] = {
		.thrd_mb	= 161568,
		.freq_int	= 200000,
		.freq_mif	= 400000,
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
		.freq_cpu	= 0,
#else
		.freq_cpu	= 0,
		.freq_kfc	= 0,
#endif
	},
	[2] = {
		.thrd_mb	= 244800,
		.freq_int	= 267000,
		.freq_mif	= 400000,
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
		.freq_cpu	= 400000,
#else
		.freq_cpu	= 0,
		.freq_kfc	= 400000,
#endif
	},
	[3] = {
		.thrd_mb	= 328032,
		.freq_int	= 400000,
		.freq_mif	= 400000,
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
		.freq_cpu	= 400000,
#else
		.freq_cpu	= 0,
		.freq_kfc	= 400000,
#endif
	},
};
#endif

static struct s5p_mfc_platdata smdk5410_mfc_pd = {
	.ip_ver		= IP_VER_MFC_5A_1,
	.clock_rate	= 333 * MHZ,
#ifdef CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ
	.num_qos_steps	= ARRAY_SIZE(smdk5410_mfc_qos_table),
	.qos_table	= smdk5410_mfc_qos_table,
#endif
};
#endif

#ifdef CONFIG_VIDEO_EXYNOS_FIMG2D
static struct fimg2d_platdata fimg2d_data __initdata = {
	.ip_ver		= IP_VER_G2D_5A,
	.hw_ver		= 0x42,
	.gate_clkname	= "fimg2d",
};
#endif

static struct platform_device *smdk5410_media_devices[] __initdata = {
#if defined (CONFIG_CSI_D) || defined (CONFIG_S5K6B2_CSI_D) || defined (CONFIG_CSI_C)
	&exynos5_device_hs_i2c1,
#endif
#if defined (CONFIG_CSI_E) || defined (CONFIG_S5K6B2_CSI_E)
	&exynos5_device_hs_i2c2,
#endif
#ifdef CONFIG_EXYNOS_MEDIA_DEVICE
	&exynos_device_md0,
	&exynos_device_md1,
	&exynos_device_md2,
#endif
	&exynos5_device_scaler0,
	&exynos5_device_rotator,
#ifdef CONFIG_VIDEO_EXYNOS_GSCALER
	&exynos5_device_gsc0,
	&exynos5_device_gsc1,
	&exynos5_device_gsc2,
	&exynos5_device_gsc3,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_LITE
	&exynos_device_flite0,
	&exynos_device_flite1,
	&exynos_device_flite2,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_MIPI_CSIS
	&s5p_device_mipi_csis0,
	&s5p_device_mipi_csis1,
	&s5p_device_mipi_csis2,
	&mipi_csi_fixed_voltage,
#endif

#ifdef CONFIG_VIDEO_EXYNOS_TV
#ifdef CONFIG_VIDEO_EXYNOS_HDMI
    &i2c2_gpio_device,
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
	&s3c_device_i2c2,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_MFC
	&s5p_device_mfc,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_FIMG2D
	&s5p_device_fimg2d,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_JPEG
	&s5p_device_jpeg,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_JPEG_HX
	&exynos5_device_jpeg_hx,
#endif
};

#ifdef CONFIG_VIDEO_EXYNOS_FIMC_LITE
static void __init smdk5410_camera_gpio_cfg(void)
{
#ifdef CONFIG_VIDEO_S5K4ECGX
#ifdef CONFIG_CSI_C
	gpio_request(EXYNOS5410_GPM7(5), "GPM7");
	s3c_gpio_cfgpin(EXYNOS5410_GPM7(5), S3C_GPIO_SFN(0x2));
	s3c_gpio_setpull(EXYNOS5410_GPM7(5), S3C_GPIO_PULL_NONE);
	gpio_free(EXYNOS5410_GPM7(5));
#endif
#ifdef CONFIG_CSI_D
	gpio_request(EXYNOS5410_GPM7(6), "GPM7");
	s3c_gpio_cfgpin(EXYNOS5410_GPM7(6), S3C_GPIO_SFN(0x2));
	s3c_gpio_setpull(EXYNOS5410_GPM7(6), S3C_GPIO_PULL_NONE);
	gpio_free(EXYNOS5410_GPM7(6));
#endif
#ifdef CONFIG_CSI_E
	gpio_request(EXYNOS5410_GPM7(7), "GPM7");
	s3c_gpio_cfgpin(EXYNOS5410_GPM7(7), S3C_GPIO_SFN(0x2));
	s3c_gpio_setpull(EXYNOS5410_GPM7(7), S3C_GPIO_PULL_NONE);
	gpio_free(EXYNOS5410_GPM7(7));
#endif
	s3c_gpio_cfgpin(EXYNOS5410_GPX3(5), S3C_GPIO_SFN(0xF));
	s3c_gpio_setpull(EXYNOS5410_GPX3(5), S3C_GPIO_PULL_NONE);
#endif
}
#endif

#if defined(CONFIG_VIDEO_EXYNOS_GSCALER) && defined(CONFIG_VIDEO_EXYNOS_FIMC_LITE)
#ifdef CONFIG_VIDEO_S5K4ECGX
static struct exynos_isp_info s5k4ecgx = {
	.board_info	= hs_i2c_devs1,
	.cam_srclk_name	= "sclk_isp_sensor",
	.clk_frequency  = 24000000UL,
	.bus_type	= CAM_TYPE_MIPI,
	.cam_clk_src_name = "dout_aclk_333_432_gscl",
	.cam_clk_name	= "aclk_333_432_gscl",
#ifdef CONFIG_CSI_C
	.camif_clk_name	= "gscl_flite0",
	.i2c_bus_num	= 5,
	.cam_port	= CAM_PORT_A, /* A-Port : 0 */
#endif
#ifdef CONFIG_CSI_D
	.camif_clk_name	= "gscl_flite1",
	.i2c_bus_num	= 5,
	.cam_port	= CAM_PORT_B, /* B-Port : 1 */
#endif
#ifdef CONFIG_CSI_E
	.camif_clk_name	= "gscl_flite2",
	.i2c_bus_num	= 6,
	.cam_port	= CAM_PORT_C, /* C-Port : 2 */
#endif
	.flags		= CAM_CLK_INV_VSYNC,
	.csi_data_align = 24,
};
/* This is for platdata of fimc-lite */
static struct s3c_platform_camera flite_s5k4ecgx = {
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

static void __init smdk5410_set_camera_platdata(void)
{
	int gsc_cam_index = 0;
	int flite0_cam_index = 0;
	int flite1_cam_index = 0;
	int flite2_cam_index = 0;

#if defined(CONFIG_VIDEO_S5K4ECGX)
	exynos_gsc1_default_data.isp_info[gsc_cam_index++] = &s5k4ecgx;
#if defined(CONFIG_CSI_C)
	exynos_flite0_default_data.cam[flite0_cam_index] = &flite_s5k4ecgx;
	exynos_flite0_default_data.isp_info[flite0_cam_index] = &s5k4ecgx;
	flite0_cam_index++;
#endif
#if defined(CONFIG_CSI_D)
	exynos_flite1_default_data.cam[flite1_cam_index] = &flite_s5k4ecgx;
	exynos_flite1_default_data.isp_info[flite1_cam_index] = &s5k4ecgx;
	flite1_cam_index++;
#endif
#if defined(CONFIG_CSI_E)
	exynos_flite2_default_data.cam[flite2_cam_index] = &flite_s5k4ecgx;
	exynos_flite2_default_data.isp_info[flite2_cam_index] = &s5k4ecgx;
	flite2_cam_index++;
#endif
#endif

	/* flite platdata register */
	__set_flite_camera_config(&exynos_flite0_default_data, 0, flite0_cam_index);
	__set_flite_camera_config(&exynos_flite1_default_data, 0, flite1_cam_index);
	__set_flite_camera_config(&exynos_flite2_default_data, 0, flite2_cam_index);

	/* gscaler platdata register */
	/* GSC-0 */
	__set_gsc_camera_config(&exynos_gsc1_default_data, 0, 1, 0, gsc_cam_index);

	/* GSC-1 */
	/* GSC-2 */
	/* GSC-3 */
}
#endif /* CONFIG_VIDEO_EXYNOS_GSCALER */

void __init exynos5_odroidxu_media_init(void)
{
#if defined (CONFIG_CSI_D) || defined (CONFIG_S5K6B2_CSI_D) || defined (CONFIG_CSI_C)
	exynos5_hs_i2c1_set_platdata(NULL);
#endif
#if defined (CONFIG_CSI_E) || defined (CONFIG_S5K6B2_CSI_E)
	exynos5_hs_i2c2_set_platdata(NULL);
#endif
#ifdef CONFIG_VIDEO_EXYNOS_MFC
	s5p_mfc_set_platdata(&smdk5410_mfc_pd);

	dev_set_name(&s5p_device_mfc.dev, "s3c-mfc");
	clk_add_alias("mfc", "s5p-mfc-v6", "mfc", &s5p_device_mfc.dev);
	s5p_mfc_setname(&s5p_device_mfc, "s5p-mfc-v6");
#endif

	platform_add_devices(smdk5410_media_devices,
			ARRAY_SIZE(smdk5410_media_devices));

	s3c_set_platdata(&exynos5410_scaler_pd, sizeof(exynos5410_scaler_pd),
			&exynos5_device_scaler0);

#ifdef CONFIG_VIDEO_EXYNOS_MIPI_CSIS
	s3c_set_platdata(&s5p_mipi_csis0_default_data,
			sizeof(s5p_mipi_csis0_default_data), &s5p_device_mipi_csis0);
	s3c_set_platdata(&s5p_mipi_csis1_default_data,
			sizeof(s5p_mipi_csis1_default_data), &s5p_device_mipi_csis1);
	s3c_set_platdata(&s5p_mipi_csis2_default_data,
			sizeof(s5p_mipi_csis2_default_data), &s5p_device_mipi_csis2);
#endif
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_LITE
	smdk5410_camera_gpio_cfg();
	smdk5410_set_camera_platdata();

	s3c_set_platdata(&exynos_flite0_default_data,
			sizeof(exynos_flite0_default_data), &exynos_device_flite0);
	s3c_set_platdata(&exynos_flite1_default_data,
			sizeof(exynos_flite1_default_data), &exynos_device_flite1);
	s3c_set_platdata(&exynos_flite2_default_data,
			sizeof(exynos_flite2_default_data), &exynos_device_flite2);
#endif

#if defined(CONFIG_VIDEO_EXYNOS_TV)
	dev_set_name(&s5p_device_hdmi.dev, "exynos5-hdmi");
	mxr_platdata.ip_ver = IP_VER_TV_5A_1;
	hdmi_platdata.ip_ver = IP_VER_TV_5A_1;

	s5p_tv_setup();
/* Below should be enabled after power domain is available */
#if 0
	s5p_device_hdmi.dev.parent = &exynos5_device_pd[PD_DISP1].dev;
	s5p_device_mixer.dev.parent = &exynos5_device_pd[PD_DISP1].dev;
#endif
#ifdef CONFIG_VIDEO_EXYNOS_HDMI_CEC
	s5p_hdmi_cec_set_platdata(&hdmi_cec_data);
#endif
	s3c_set_platdata(&mxr_platdata, sizeof(mxr_platdata), &s5p_device_mixer);
	s5p_hdmi_set_platdata(&hdmi_platdata);
	i2c_register_board_info(2, i2c_devs2, ARRAY_SIZE(i2c_devs2));
#endif
#ifdef CONFIG_VIDEO_EXYNOS_GSCALER
	exynos5_gsc_set_ip_ver(IP_VER_GSC_5A);

	s3c_set_platdata(&exynos_gsc0_default_data, sizeof(exynos_gsc0_default_data),
			&exynos5_device_gsc0);
	s3c_set_platdata(&exynos_gsc1_default_data, sizeof(exynos_gsc1_default_data),
			&exynos5_device_gsc1);
	s3c_set_platdata(&exynos_gsc2_default_data, sizeof(exynos_gsc2_default_data),
			&exynos5_device_gsc2);
	s3c_set_platdata(&exynos_gsc3_default_data, sizeof(exynos_gsc3_default_data),
			&exynos5_device_gsc3);
#endif

#ifdef CONFIG_VIDEO_EXYNOS_FIMG2D
	s5p_fimg2d_set_platdata(&fimg2d_data);
#endif
#ifdef CONFIG_VIDEO_EXYNOS_JPEG
	exynos5_jpeg_fimp_setup_clock(&s5p_device_jpeg.dev, 166500000);
#endif
#ifdef CONFIG_VIDEO_EXYNOS_JPEG_HX
	exynos5_jpeg_hx_setup_clock(&exynos5_device_jpeg_hx.dev, 300000000);
#endif
}
