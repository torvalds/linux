/*
 * linux/arch/arm/mach-exynos/naples-naples.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/i2c.h>

#include <linux/regulator/machine.h>

#include <plat/devs.h>
#include <plat/csis.h>
#include <plat/pd.h>
#include <plat/gpio-cfg.h>

#include <media/exynos_flite.h>

#include <media/s5k4ecgx_platform.h>
#include <media/db8131m_platform.h>

struct class *camera_class;
struct device *s5k4ecgx_dev; /*sys/class/camera/rear*/
struct device *db8131m_dev; /*sys/class/camera/front*/

/* flash_type: FLASH[0], TORCH[1] */
static int max77693_led_ctrl(int ctrl)
{
	int ret = 0;

	pr_info("%s, flash_type[%d]", __func__, ctrl);

	/* Flash TORCH_EN(FLASHEN) */
	ret = gpio_request(GPIO_TORCH_EN, "GPJ1");
	if (ret) {
		pr_err("fail to request gpio(GPIO_TORCH_EN)");
		return ret;
	}

	/* Flash TORCH_SET(TORCHEN) */
	ret = gpio_request(GPIO_TORCH_SET, "GPJ1");
	if (ret) {
		pr_err("faile to request gpio(GPIO_TORCH_SET)");
		return ret;
	}

	if (ctrl == CAM_FLASH_ON)
		/* FLSH mode */
		ret = gpio_direction_output(GPIO_TORCH_EN, 1);
	else if (ctrl == CAM_FLASH_TORCH)
		/* TORCH mode */
		ret = gpio_direction_output(GPIO_TORCH_SET, 1);
	else {
		ret = gpio_direction_output(GPIO_TORCH_EN, 0);
		ret |= gpio_direction_output(GPIO_TORCH_SET, 0);
	}

	gpio_free(GPIO_TORCH_EN);
	gpio_free(GPIO_TORCH_SET);

	if (unlikely((ret) < 0)) {					\
		pr_err("fail to %s: err = %d", "Flash control", ret);	\
	}

	return ret;
}

#if defined(CONFIG_VIDEO_FIMC)
/*
 * External camera reset
 * Because the most of cameras take i2c bus signal, so that
 * you have to reset at the boot time for other i2c slave devices.
 * This function also called at fimc_init_camera()
 * Do optimization for cameras on your platform.
 */
int s3c_csis_power(int enable)
{
	struct regulator *regulator;
	int ret = 0;

	/* mipi_1.1v ,mipi_1.8v are always powered-on.
	 * If they are off, we then power them on.
	 */
	if (enable) {
		/* VMIPI_1.0V */
		regulator = regulator_get(NULL, "vmipi_1.0v");
		if (IS_ERR(regulator))
			goto error_out;
		if (!regulator_is_enabled(regulator)) {
			pr_warn("%s: vmipi_1.1v is off. so ON",
			       __func__);
			ret = regulator_enable(regulator);
		}
		regulator_put(regulator);

		/* VMIPI_1.8V */
		regulator = regulator_get(NULL, "vmipi_1.8v");
		if (IS_ERR(regulator))
			goto error_out;
		if (!regulator_is_enabled(regulator)) {
			pr_warn("%s: vmipi_1.8v is off. so ON",
			       __func__);
			ret = regulator_enable(regulator);
		}
		regulator_put(regulator);
		pr_warn("%s: vmipi_1.0v and vmipi_1.8v were ON",
		       __func__);
	}

	return 0;

error_out:
	pr_err("%s: ERROR: failed to check mipi-power", __func__);
	return 0;
}

#ifdef WRITEBACK_ENABLED
static int get_i2c_busnum_writeback(void)
{
	return 0;
}

static struct i2c_board_info __initdata writeback_i2c_info = {
	I2C_BOARD_INFO("WriteBack", 0x0),
};

static struct s3c_platform_camera writeback = {
	.id		= CAMERA_WB,
	.fmt		= ITU_601_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_CBYCRY,
	.get_i2c_busnum = get_i2c_busnum_writeback,
	.info		= &writeback_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_YUV444,
	.line_length	= 800,
	.width		= 480,
	.height		= 800,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 480,
		.height	= 800,
	},

	.initialized	= 0,
};
#endif

#define CAM_CHECK_ERR_RET(x, msg)					\
	if (unlikely((x) < 0)) {					\
		pr_err("fail to %s: err = %d", msg, x);	\
		return x;						\
	}
#define CAM_CHECK_ERR(x, msg)						\
	if (unlikely((x) < 0)) {					\
		pr_err("fail to %s: err = %d", msg, x);	\
	}

static int s5k4ecgx_gpio_request(void)
{
	int ret = 0;

	ret = gpio_request(GPIO_5M_CAM_RESET, "GPF1");
	if (ret) {
		pr_err("faile to request gpio(GPIO_5M_CAM_RESET)");
		return ret;
	}

	ret = gpio_request(GPIO_5M_CAM_nSTBY, "GPM0");
	if (ret) {
		pr_err("faile to request gpio(GPIO_5M_CAM_nSTBY)");
		return ret;
	}

	return ret;
}

static int s5k4ecgx_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	pr_debug("%s: in", __func__);

	s5k4ecgx_gpio_request();

	/* CAM_SENSOR_IO_1.8V */
	regulator = regulator_get(NULL, "cam_sensor_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_sensor_io_1.8v");

	/* CAM_SENSOR_A2.8V */
	regulator = regulator_get(NULL, "cam_sensor_a2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_sensor_a2.8v");

	/* CAM_DVDD_1.5V(1.3M Core 1.8V) */
	regulator = regulator_get(NULL, "cam_dvdd_1.5v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_dvdd_1.5v");

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);

	/* CAM_ISP_CORE_1.2V */
	regulator = regulator_get(NULL, "cam_isp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp_core_1.2v");

	/* VT_CAM_nSTBY(1.3M EN) LOW */
	ret = gpio_request(GPIO_VT_CAM_nSTBY, "GPM0");
	if (ret) {
		pr_err("faile to request gpio(GPIO_VT_CAM_nSTBY)");
		return ret;
	}
	ret = gpio_direction_output(GPIO_VT_CAM_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "low VT_CAM_nSTBY");

	/* CAM_VT_nRST(1.3M RESET) LOW */
	ret = gpio_request(GPIO_CAM_VT_nRST, "GPM1");
	if (ret) {
		pr_err("faile to request gpio(GPIO_CAM_VT_nRST)");
		return ret;
	}
	ret = gpio_direction_output(GPIO_CAM_VT_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "low CAM_VT_nRST");

	/* CAM_AF_2.8V */
	regulator = regulator_get(NULL, "cam_af_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_af_2.8v");

	mdelay(1);		/* 10us */

	/* 5M_CAM_nSTBY(5M STBY) */
	ret = gpio_direction_output(GPIO_5M_CAM_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "high 5M_CAM_nSTBY");

	mdelay(1);		/* 15us */

	/* 5M_CAM_RESET(5M RESET) */
	ret = gpio_direction_output(GPIO_5M_CAM_RESET, 1);
	CAM_CHECK_ERR_RET(ret, "high 5M_CAM_RESET");

	mdelay(1);		/* 60us */

	gpio_free(GPIO_VT_CAM_nSTBY);
	gpio_free(GPIO_CAM_VT_nRST);
	gpio_free(GPIO_5M_CAM_nSTBY);
	gpio_free(GPIO_5M_CAM_RESET);

	return ret;
}

static int s5k4ecgx_power_down(void)
{
	struct regulator *regulator;
	int ret = 0;

	pr_debug("%s: in", __func__);

	s5k4ecgx_gpio_request();

	/* VT_CAM_nSTBY(1.3M EN) LOW */
	ret = gpio_request(GPIO_VT_CAM_nSTBY, "GPM0");
	if (ret) {
		pr_err("faile to request gpio(GPIO_VT_CAM_nSTBY)");
		return ret;
	}
	ret = gpio_direction_output(GPIO_VT_CAM_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "low VT_CAM_nSTBY");

	/* CAM_VT_nRST(1.3M RESET) LOW */
	ret = gpio_request(GPIO_CAM_VT_nRST, "GPM1");
	if (ret) {
		pr_err("faile to request gpio(GPIO_CAM_VT_nRST)");
		return ret;
	}
	ret = gpio_direction_output(GPIO_CAM_VT_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "low CAM_VT_nRST");

	/* 5M_CAM_RESET(5M RESET) LOW */
	ret = gpio_direction_output(GPIO_5M_CAM_RESET, 0);
	CAM_CHECK_ERR_RET(ret, "low 5M_CAM_RESET");

	mdelay(1);		/* 50us */

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");

	/* 5M_CAM_nSTBY(5M STBY) LOW */
	ret = gpio_direction_output(GPIO_5M_CAM_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "low 5M_CAM_nSTBY");

	/* CAM_AF_2.8V */
	regulator = regulator_get(NULL, "cam_af_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "disable cam_af_2.8v");

	/* CAM_DVDD_1.5V(1.3M Core 1.8V) */
	regulator = regulator_get(NULL, "cam_dvdd_1.5v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "disable cam_dvdd_1.5v");

	/* CAM_SENSOR_IO_1.8V */
	regulator = regulator_get(NULL, "cam_sensor_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "disable cam_sensor_io_1.8v");

	/* CAM_SENSOR_A2.8V */
	regulator = regulator_get(NULL, "cam_sensor_a2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "disable cam_sensor_a2.8v");

	/* CAM_ISP_CORE_1.2V */
	regulator = regulator_get(NULL, "cam_isp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "disable cam_isp_core_1.2v");

	gpio_free(GPIO_VT_CAM_nSTBY);
	gpio_free(GPIO_CAM_VT_nRST);
	gpio_free(GPIO_5M_CAM_RESET);
	gpio_free(GPIO_5M_CAM_nSTBY);

	return ret;
}

static int s5k4ecgx_power(int enable)
{
	int ret = 0;

	pr_info("%s %s", __func__, enable ? "on" : "down");

	if (enable) {
		ret = s5k4ecgx_power_on();

		if (unlikely(ret))
			goto error_out;
	} else
		ret = s5k4ecgx_power_down();

	ret = s3c_csis_power(enable);

error_out:
	return ret;
}

static int s5k4ecgx_get_i2c_busnum(void)
{
	/* HW I2C Num 0 */
	return 0;
}

static const char *s5k4ecgx_get_clk_name(void)
{
	return "sclk_cam0";
}

static struct s5k4ecgx_platform_data s5k4ecgx_plat = {
	.default_width = 640,
	.default_height = 480,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 1,
	.flash_ctrl = max77693_led_ctrl,
};

static struct i2c_board_info s5k4ecgx_i2c_info = {
	I2C_BOARD_INFO("S5K4ECGX", 0xAC >> 1),
	.platform_data = &s5k4ecgx_plat,
};

static struct s3c_platform_camera s5k4ecgx = {
	.id = CAMERA_CSI_C,
	.type = CAM_TYPE_MIPI,
	.fmt = MIPI_CSI_YCBCR422_8BIT, /*ITU_601_YCBCR422_8BIT,*/
	.order422 = CAM_ORDER422_8BIT_CBYCRY,
	.mipi_lanes = 2,
	.mipi_settle = 12, /* only CONFIG_MIPI_CSI_ADV_FEATURE */
	.mipi_align = 32, /* only CONFIG_MIPI_CSI_ADV_FEATURE */

	.get_i2c_busnum = s5k4ecgx_get_i2c_busnum,
	.get_clk_name = s5k4ecgx_get_clk_name,
	.info = &s5k4ecgx_i2c_info,
	.pixelformat = V4L2_PIX_FMT_VYUY,
	.srclk_name = "xusbxti",
	.clk_rate = 24000000,
	.line_length = 640,
	.width = 640,
	.height = 480,
	.window = {
		.left = 0,
		.top = 0,
		.width = 640,
		.height = 480,
	},

	/* Polarity */
	.inv_pclk = 0,
	.inv_vsync = 1,
	.inv_href = 0,
	.inv_hsync = 0,
	.reset_camera = 0,
	.initialized = 0,
	.cam_power = s5k4ecgx_power,
};


static int db8131m_gpio_request(void)
{
	int ret = 0;

	ret = gpio_request(GPIO_VT_CAM_nSTBY, "GPM0");
	if (ret) {
		pr_err("faile to request gpio(GPIO_VT_CAM_nSTBY)");
		return ret;
	}

	ret = gpio_request(GPIO_CAM_VT_nRST, "GPM1");
	if (ret) {
		pr_err("faile to request gpio(GPIO_CAM_VT_nRST)");
		return ret;
	}

	ret = gpio_request(GPIO_VT_CAM_ID, "GPF1");
	if (ret) {
		pr_err("faile to request gpio(GPIO_VT_CAM_ID)");
		return ret;
	}

	return ret;
}

static int db8131m_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	pr_debug("%s: in", __func__);

	db8131m_gpio_request();

	/* 5M_CAM_nSTBY(5M STBY) LOW */
	ret = gpio_request(GPIO_5M_CAM_nSTBY, "GPM0");
	if (ret) {
		pr_err("faile to request gpio(GPIO_5M_CAM_nSTBY)");
		return ret;
	}
	ret = gpio_direction_output(GPIO_5M_CAM_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "low 5M_CAM_nSTBY");

	/* 5M_CAM_RESET(5M RESET) LOW */
	ret = gpio_request(GPIO_5M_CAM_RESET, "GPF1");
	if (ret) {
		pr_err("faile to request gpio(GPIO_5M_CAM_RESET)");
		return ret;
	}
	ret = gpio_direction_output(GPIO_5M_CAM_RESET, 0);
	CAM_CHECK_ERR_RET(ret, "low 5M_CAM_RESET");

	/* VT_CAM_1.8V(VDDIO) */
	regulator = regulator_get(NULL, "vt_cam_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable vt_cam_1.8v");

	/* CAM_SENSOR_A2.8V */
	regulator = regulator_get(NULL, "cam_sensor_a2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_sensor_a2.8v");

	/* CAM_DVDD_1.5V(1.3M Core 1.8V) */
	regulator = regulator_get(NULL, "cam_dvdd_1.5v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_dvdd_1.5v");

	/* CAM_ISP_CORE_1.2V ENABLE */
	regulator = regulator_get(NULL, "cam_isp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp_core_1.2v");

	mdelay(2);		/* 1ms */

	/* CAM_ISP_CORE_1.2V DISABLE */
	ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "disable cam_isp_core_1.2v");

	/* VT_CAM_nSTBY(1.3M EN) EN */
	ret = gpio_direction_output(GPIO_VT_CAM_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "high VT_CAM_nSTBY");

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);

	mdelay(1);		/* 20us */

	/* CAM_VT_nRST(1.3M RESET) EN */
	ret = gpio_direction_output(GPIO_CAM_VT_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "high CAM_VT_nRST");

	mdelay(5);		/* 70000 cycle */

	gpio_free(GPIO_5M_CAM_nSTBY);
	gpio_free(GPIO_5M_CAM_RESET);
	gpio_free(GPIO_VT_CAM_nSTBY);
	gpio_free(GPIO_CAM_VT_nRST);
	gpio_free(GPIO_VT_CAM_ID);

	return ret;
}

static int db8131m_power_down(void)
{
	struct regulator *regulator;
	int ret = 0;

	pr_debug("%s: in", __func__);

	db8131m_gpio_request();

	/* VT_CAM_nSTBY(1.3M EN) DIS */
	ret = gpio_direction_output(GPIO_VT_CAM_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "low VT_CAM_nSTBY");

	/* CAM_VT_nRST(1.3M RESET) DIS */
	ret = gpio_direction_output(GPIO_CAM_VT_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "low CAM_VT_nRST");

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");

	/* CAM_DVDD_1.5V(1.3M Core 1.8V) */
	regulator = regulator_get(NULL, "cam_dvdd_1.5v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "disable cam_dvdd_1.5v");

	/* CAM_SENSOR_A2.8V */
	regulator = regulator_get(NULL, "cam_sensor_a2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "disable cam_sensor_a2.8v");

	/* VT_CAM_1.8V(VDDIO) */
	regulator = regulator_get(NULL, "vt_cam_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "disable vt_cam_1.8v");

	gpio_free(GPIO_VT_CAM_nSTBY);
	gpio_free(GPIO_CAM_VT_nRST);
	gpio_free(GPIO_VT_CAM_ID);

	return ret;
}

static int db8131m_power(int enable)
{
	int ret = 0;

	pr_err("%s %s", __func__, enable ? "on" : "down");

	if (enable) {
		ret = db8131m_power_on();

		if (unlikely(ret))
			goto error_out;
	} else
		ret = db8131m_power_down();

	ret = s3c_csis_power(enable);

error_out:
	return ret;
}

static int db8131m_get_i2c_busnum(void)
{
	/* SW gpio I2C Num 20 */
	return 20; /*I2C_VTCAM*/
}

static const char *db8131m_get_clk_name(void)
{
	return "sclk_cam0";
}

static struct s5k4ecgx_platform_data db8131m_plat = {
	.default_width = 640,
	.default_height = 480,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 1,
};

static struct i2c_board_info db8131m_i2c_info = {
	I2C_BOARD_INFO("DB8131M", 0x8A >> 1),
	.platform_data = &db8131m_plat,
};

static struct s3c_platform_camera db8131m = {
	.id = CAMERA_CSI_D,
	.type = CAM_TYPE_MIPI,
	.fmt = MIPI_CSI_YCBCR422_8BIT, /*ITU_601_YCBCR422_8BIT,*/
	.order422 = CAM_ORDER422_8BIT_CBYCRY,
	.mipi_lanes = 1,
	.mipi_settle = 12, /* only CONFIG_MIPI_CSI_ADV_FEATURE */
	.mipi_align = 32, /* only CONFIG_MIPI_CSI_ADV_FEATURE */

	.get_i2c_busnum = db8131m_get_i2c_busnum,
	.get_clk_name	= db8131m_get_clk_name,
	.info = &db8131m_i2c_info,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.srclk_name = "xusbxti",
	.clk_rate = 24000000,
	.line_length = 640,
	.width = 640,
	.height = 480,
	.window = {
		.left = 0,
		.top = 0,
		.width = 640,
		.height = 480,
	},

	/* Polarity */
	.inv_pclk = 0,
	.inv_vsync = 0,
	.inv_href = 0,
	.inv_hsync = 0,
	.reset_camera = 0,
	.initialized = 0,
	.cam_power = db8131m_power,
};

/* Interface setting */
static struct s3c_platform_fimc fimc_plat = {
	.default_cam = CAMERA_CSI_C,
#ifdef WRITEBACK_ENABLED
	.default_cam = CAMERA_WB,
#endif
	.camera = {
		&s5k4ecgx, /* 5M CAM */
		&db8131m, /* 1.3M CAM */
#ifdef WRITEBACK_ENABLED
		&writeback,
#endif
	},
	.hw_ver		= 0x51,
};
#endif /* CONFIG_VIDEO_FIMC */

static ssize_t s5k4ecgx_camera_rear_flash(struct device *dev,
	struct device_attribute *attr, const char *buf,
	size_t count)
{
	int err = 0;

	if (buf[0] == '0')
		err = max77693_led_ctrl(CAM_FLASH_OFF);
	else
		err = max77693_led_ctrl(CAM_FLASH_TORCH);

	if (err < 0)
		pr_err("failed to s5k4ecgx_camera_rear_flash!\n");

	return count;
}

ssize_t s5k4ecgx_camera_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char *cam_type;
	pr_info("%s\n", __func__);

	cam_type = "SLSI_S5K4ECGX";

	return sprintf(buf, "%s\n", cam_type);
}

static ssize_t s5k4ecgx_camera_rear_camfw_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char *fw_type;
	pr_info("%s\n", __func__);

	fw_type = "fw is not supported";

	return sprintf(buf, "%s\n", fw_type);
}
static DEVICE_ATTR(rear_flash, S_IWUSR | S_IWGRP, NULL,
		s5k4ecgx_camera_rear_flash);
static DEVICE_ATTR(rear_camtype, S_IRUGO,
	s5k4ecgx_camera_type_show, NULL);
static DEVICE_ATTR(rear_camfw, S_IRUGO,
	s5k4ecgx_camera_rear_camfw_show, NULL);

ssize_t db8131m_camera_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char *cam_type;
	pr_info("%s\n", __func__);

	cam_type = "Dongbu_DB8131M";

	return sprintf(buf, "%s\n", cam_type);
}

static ssize_t db8131m_camera_rear_camfw_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char *fw_type;
	pr_info("%s\n", __func__);

	fw_type = "fw is not supported";

	return sprintf(buf, "%s\n", fw_type);
}
static DEVICE_ATTR(front_camtype, S_IRUGO,
	db8131m_camera_type_show, NULL);
static DEVICE_ATTR(front_camfw, S_IRUGO,
	db8131m_camera_rear_camfw_show, NULL);

void __init midas_camera_init(void)
{
#ifdef CONFIG_VIDEO_FIMC
	s3c_fimc0_set_platdata(&fimc_plat);
	s3c_fimc1_set_platdata(&fimc_plat);
	s3c_fimc2_set_platdata(NULL);
	s3c_fimc3_set_platdata(NULL);
#ifdef CONFIG_EXYNOS_DEV_PD
	s3c_device_fimc0.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_fimc1.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_fimc2.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_fimc3.dev.parent = &exynos4_device_pd[PD_CAM].dev;
#endif
#ifdef CONFIG_VIDEO_FIMC_MIPI
	s3c_csis0_set_platdata(NULL);
	s3c_csis1_set_platdata(NULL);
#ifdef CONFIG_EXYNOS_DEV_PD
	s3c_device_csis0.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_csis1.dev.parent = &exynos4_device_pd[PD_CAM].dev;
#endif
#endif
#endif /* CONFIG_VIDEO_FIMC */

	camera_class = class_create(THIS_MODULE, "camera");

	if (!s5k4ecgx_dev) {
		s5k4ecgx_dev = device_create(camera_class,
				NULL, 0, NULL, "rear");
		if (IS_ERR(s5k4ecgx_dev)) {
			pr_err("s5k4ecgx_dev : failed to create device!\n");
		} else {
			if (device_create_file(s5k4ecgx_dev,
				&dev_attr_rear_flash) < 0) {
				pr_err("failed to create device file, %s\n",
					dev_attr_rear_flash.attr.name);
			}
			if (device_create_file(s5k4ecgx_dev,
				&dev_attr_rear_camtype)
					< 0) {
				pr_err("failed to create device file, %s\n",
					dev_attr_rear_camtype.attr.name);
			}
			if (device_create_file(s5k4ecgx_dev,
				&dev_attr_rear_camfw) < 0) {
				pr_err("failed to create device file, %s\n",
					dev_attr_rear_camfw.attr.name);
			}
		}
	}
	if (!db8131m_dev) {
		db8131m_dev = device_create(camera_class,
				NULL, 0, NULL, "front");
		if (IS_ERR(db8131m_dev)) {
			pr_err("db8131m_dev : failed to create device!\n");
		} else {
			if (device_create_file(db8131m_dev,
				&dev_attr_front_camtype)
					< 0) {
				pr_err("failed to create device file, %s\n",
					dev_attr_front_camtype.attr.name);
			}
			if (device_create_file(db8131m_dev,
				&dev_attr_front_camfw) < 0) {
				pr_err("failed to create device file, %s\n",
					dev_attr_front_camfw.attr.name);
			}
		}
	}
}
