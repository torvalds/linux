#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/sensor/sensors_core.h>
#ifdef CONFIG_SENSORS_AK8975C
#include <linux/sensor/ak8975.h>
#endif
#ifdef CONFIG_SENSORS_AK8963C
#include <linux/sensor/ak8963.h>
#endif
#include <linux/sensor/k3dh.h>
#include <linux/sensor/gp2a.h>
#include <linux/sensor/lsm330dlc_accel.h>
#include <linux/sensor/lsm330dlc_gyro.h>
#include <linux/sensor/lps331ap.h>
#include <linux/sensor/cm36651.h>
#include <linux/sensor/cm3663.h>
#include <linux/sensor/bh1721.h>

#include <plat/gpio-cfg.h>
#include <mach/regs-gpio.h>
#include <mach/gpio.h>
#include "midas.h"

#if defined(CONFIG_SENSORS_LSM330DLC) ||\
	defined(CONFIG_SENSORS_K3DH)
static int stm_get_position(void);

static struct accel_platform_data accel_pdata = {
	.accel_get_position = stm_get_position,
	.axis_adjust = true,
};
#endif

#ifdef CONFIG_SENSORS_LSM330DLC
static struct gyro_platform_data gyro_pdata = {
	.gyro_get_position = stm_get_position,
	.axis_adjust = true,
};
#endif


static struct i2c_board_info i2c_devs1[] __initdata = {
#ifdef CONFIG_SENSORS_LSM330DLC
	{
		I2C_BOARD_INFO("lsm330dlc_accel", (0x32 >> 1)),
		.platform_data = &accel_pdata,
	},
	{
		I2C_BOARD_INFO("lsm330dlc_gyro", (0xD6 >> 1)),
		.platform_data = &gyro_pdata,
	},
#elif defined(CONFIG_SENSORS_K3DH)
	{
		I2C_BOARD_INFO("k3dh", 0x19),
		.platform_data	= &accel_pdata,
	},
#endif
};

static int stm_get_position(void)
{
	int position = 0;

#if defined(CONFIG_MACH_M3)
	if (system_rev == 1)
		position = 3; /* top/lower-left */
	else
		position = 2; /* top/lower-right */
#elif defined(CONFIG_MACH_M0_CMCC)
	if (system_rev == 2)
		position = 0; /* top/upper-left */
	else
		position = 2; /* top/lower-right */
#elif defined(CONFIG_MACH_C1_KOR_SKT) || defined(CONFIG_MACH_C1_KOR_KT)
	if (system_rev >= 6)
		position = 6; /* bottom/lower-right */
	else
		position = 3; /* top/lower-left */
#elif defined(CONFIG_MACH_C1_KOR_LGT)
	if (system_rev >= 6)
		position = 2; /* top/lower-right */
	else if (system_rev == 5)
		position = 4; /* bottom/upper-left */
	else
		position = 3; /* top/lower-left */
#elif defined(CONFIG_MACH_P4NOTE)
	position = 4; /* bottom/upper-left */
#elif defined(CONFIG_MACH_M0_GRANDECTC)
	if (system_rev == 13)
		position = 0; /* top/upper-left */
	else
		position = 4;
#elif defined(CONFIG_MACH_IRON)
	position = 7;  /* bottom/lower-left */
#elif defined(CONFIG_MACH_M0)
	if (system_rev == 3 || system_rev == 0)
		position = 6; /* bottom/lower-right */
	else if (system_rev == 1 || system_rev == 2\
		|| system_rev == 4 || system_rev == 5)
		position = 0; /* top/upper-left */
	else
		position = 2; /* top/lower-right */
#elif defined(CONFIG_MACH_C1)
	if (system_rev == 3 || system_rev == 0)
		position = 7; /* bottom/lower-left */
	else if (system_rev == 2)
		position = 3; /* top/lower-left */
	else
		position = 2; /* top/lower-right */
#elif defined(CONFIG_MACH_GC1)
	position = 1;
#else /* Common */
	position = 2; /* top/lower-right */
#endif
	return position;
}

#if defined(CONFIG_SENSORS_LSM330DLC) || \
	defined(CONFIG_SENSORS_K3DH)
static int accel_gpio_init(void)
{
	int ret = gpio_request(GPIO_ACC_INT, "accelerometer_irq");

	pr_info("%s\n", __func__);

	if (ret) {
		pr_err("%s, Failed to request gpio accelerometer_irq(%d)\n",
			__func__, ret);
		return ret;
	}

	/* Accelerometer sensor interrupt pin initialization */
	s3c_gpio_cfgpin(GPIO_ACC_INT, S3C_GPIO_INPUT);
	gpio_set_value(GPIO_ACC_INT, 2);
	s3c_gpio_setpull(GPIO_ACC_INT, S3C_GPIO_PULL_NONE);
	s5p_gpio_set_drvstr(GPIO_ACC_INT, S5P_GPIO_DRVSTR_LV1);
	i2c_devs1[0].irq = gpio_to_irq(GPIO_ACC_INT);

	return ret;
}
#endif

#ifdef CONFIG_SENSORS_LSM330DLC
static int gyro_gpio_init(void)
{
	int ret = gpio_request(GPIO_GYRO_INT, "lsm330dlc_gyro_irq");

	pr_info("%s\n", __func__);

	if (ret) {
		pr_err("%s, Failed to request gpio lsm330dlc_gyro_irq(%d)\n",
			__func__, ret);
		return ret;
	}

	ret = gpio_request(GPIO_GYRO_DE, "lsm330dlc_gyro_data_enable");

	if (ret) {
		pr_err("%s, Failed to request gpio lsm330dlc_gyro_data_enable(%d)\n",
			__func__, ret);
		return ret;
	}

	/* Gyro sensor interrupt pin initialization */
#if 0
	s5p_register_gpio_interrupt(GPIO_GYRO_INT);
	s3c_gpio_cfgpin(GPIO_GYRO_INT, S3C_GPIO_SFN(0xF));
#else
	s3c_gpio_cfgpin(GPIO_GYRO_INT, S3C_GPIO_INPUT);
#endif
	gpio_set_value(GPIO_GYRO_INT, 2);
	s3c_gpio_setpull(GPIO_GYRO_INT, S3C_GPIO_PULL_DOWN);
	s5p_gpio_set_drvstr(GPIO_GYRO_INT, S5P_GPIO_DRVSTR_LV1);
#if 0
	i2c_devs1[1].irq = gpio_to_irq(GPIO_GYRO_INT); /* interrupt */
#else
	i2c_devs1[1].irq = -1; /* polling */
#endif

	/* Gyro sensor data enable pin initialization */
	s3c_gpio_cfgpin(GPIO_GYRO_DE, S3C_GPIO_OUTPUT);
	gpio_set_value(GPIO_GYRO_DE, 0);
	s3c_gpio_setpull(GPIO_GYRO_DE, S3C_GPIO_PULL_DOWN);
	s5p_gpio_set_drvstr(GPIO_GYRO_DE, S5P_GPIO_DRVSTR_LV1);

	return ret;
}
#endif

#if defined(CONFIG_SENSORS_GP2A) || defined(CONFIG_SENSORS_CM36651) || \
	defined(CONFIG_SENSORS_CM3663)
static int proximity_leda_on(bool onoff)
{
	pr_info("%s, onoff = %d\n", __func__, onoff);

	gpio_set_value(GPIO_PS_ALS_EN, onoff);

	return 0;
}

static int optical_gpio_init(void)
{
	int ret = gpio_request(GPIO_PS_ALS_EN, "optical_power_supply_on");

	pr_info("%s\n", __func__);

	if (ret) {
		pr_err("%s, Failed to request gpio optical power supply(%d)\n",
			__func__, ret);
		return ret;
	}

	/* configuring for gp2a gpio for LEDA power */
	s3c_gpio_cfgpin(GPIO_PS_ALS_EN, S3C_GPIO_OUTPUT);
	gpio_set_value(GPIO_PS_ALS_EN, 0);
	s3c_gpio_setpull(GPIO_PS_ALS_EN, S3C_GPIO_PULL_NONE);
	return ret;
}
#endif

#if defined(CONFIG_SENSORS_CM36651)
/* Depends window, threshold is needed to be set */
static u8 cm36651_get_threshold(void)
{
	u8 threshold = 17;

	/* Add model config and threshold here. */
#if defined(CONFIG_MACH_M0)
	if (system_rev >= 12)
		threshold = 13;
#elif defined(CONFIG_MACH_C1_KOR_SKT) || defined(CONFIG_MACH_C1_KOR_KT) ||\
	defined(CONFIG_MACH_C1_KOR_LGT)
	if (system_rev >= 6)
		threshold = 13;
#elif defined(CONFIG_MACH_M3)
	if (system_rev >= 11)
		threshold = 13;
#elif defined(CONFIG_MACH_C1)
	if (system_rev >= 7)
		threshold = 13;
#endif

	return threshold;
}

static struct cm36651_platform_data cm36651_pdata = {
	.cm36651_led_on = proximity_leda_on,
	.cm36651_get_threshold = cm36651_get_threshold,
	.irq = GPIO_PS_ALS_INT,
};
#endif

#if defined(CONFIG_SENSORS_CM3663)
static struct cm3663_platform_data cm3663_pdata = {
	.proximity_power = proximity_leda_on,
};
#endif

#if defined(CONFIG_SENSORS_GP2A)
static struct gp2a_platform_data gp2a_pdata = {
	.gp2a_led_on	= proximity_leda_on,
	.p_out = GPIO_PS_ALS_INT,
};

static struct platform_device opt_gp2a = {
	.name = "gp2a-opt",
	.id = -1,
	.dev = {
		.platform_data = &gp2a_pdata,
	},
};
#endif

static struct i2c_board_info i2c_devs9_emul[] __initdata = {
#if defined(CONFIG_SENSORS_GP2A)
	{
		I2C_BOARD_INFO("gp2a", (0x72 >> 1)),
	},
#elif defined(CONFIG_SENSORS_CM36651)
	{
		I2C_BOARD_INFO("cm36651", (0x30 >> 1)),
		.platform_data = &cm36651_pdata,
	},
#elif defined(CONFIG_SENSORS_CM3663)
	{
		I2C_BOARD_INFO("cm3663", (0x20)),
		.irq = GPIO_PS_ALS_INT,
		.platform_data = &cm3663_pdata,
	},
#elif defined(CONFIG_SENSORS_BH1721)
	{
		I2C_BOARD_INFO("bh1721fvc", 0x23),
	},
#endif
};

#ifdef CONFIG_SENSORS_AK8975C
static struct akm8975_platform_data akm8975_pdata = {
	.gpio_data_ready_int = GPIO_MSENSOR_INT,
};

static struct i2c_board_info i2c_devs10_emul[] __initdata = {
	{
		I2C_BOARD_INFO("ak8975", 0x0C),
		.platform_data = &akm8975_pdata,
	},
};

static int ak8975c_gpio_init(void)
{
	int ret = gpio_request(GPIO_MSENSOR_INT, "gpio_akm_int");

	pr_info("%s\n", __func__);

	if (ret) {
		pr_err("%s, Failed to request gpio akm_int.(%d)\n",
			__func__, ret);
		return ret;
	}

	s5p_register_gpio_interrupt(GPIO_MSENSOR_INT);
	s3c_gpio_setpull(GPIO_MSENSOR_INT, S3C_GPIO_PULL_DOWN);
	s3c_gpio_cfgpin(GPIO_MSENSOR_INT, S3C_GPIO_SFN(0xF));
	i2c_devs10_emul[0].irq = gpio_to_irq(GPIO_MSENSOR_INT);
	return ret;
}
#endif

#ifdef CONFIG_SENSORS_AK8963C
static struct akm8963_platform_data akm8963_pdata = {
	.gpio_data_ready_int = GPIO_MSENSOR_INT,
	.layout = 1,
	.outbit = 1,
};

static struct i2c_board_info i2c_devs10_emul[] __initdata = {
	{
		I2C_BOARD_INFO("ak8963", 0x0C),
		.platform_data = &akm8963_pdata,
	},
};

static int ak8963c_gpio_init(void)
{
	int ret = gpio_request(GPIO_MSENSOR_INT, "gpio_akm_int");

	pr_info("%s\n", __func__);

	if (ret) {
		pr_err("%s, Failed to request gpio akm_int.(%d)\n",
			__func__, ret);
		return ret;
	}

	gpio_set_value(GPIO_MSENSE_RST_N, 1);

	s5p_register_gpio_interrupt(GPIO_MSENSOR_INT);
	s3c_gpio_setpull(GPIO_MSENSOR_INT, S3C_GPIO_PULL_DOWN);
	s3c_gpio_cfgpin(GPIO_MSENSOR_INT, S3C_GPIO_SFN(0xF));
	i2c_devs10_emul[0].irq = gpio_to_irq(GPIO_MSENSOR_INT);
	return ret;
}
#endif

#ifdef CONFIG_SENSORS_LPS331
static int lps331_gpio_init(void)
{
	int ret = gpio_request(GPIO_BARO_INT, "lps331_irq");

	pr_info("%s\n", __func__);

	if (ret) {
		pr_err("%s, Failed to request gpio lps331_irq(%d)\n",
			__func__, ret);
		return ret;
	}

	s3c_gpio_cfgpin(GPIO_BARO_INT, S3C_GPIO_INPUT);
	gpio_set_value(GPIO_BARO_INT, 2);
	s3c_gpio_setpull(GPIO_BARO_INT, S3C_GPIO_PULL_NONE);
	s5p_gpio_set_drvstr(GPIO_BARO_INT, S5P_GPIO_DRVSTR_LV1);
	return ret;
}

static struct lps331ap_platform_data lps331ap_pdata = {
	.irq =	GPIO_BARO_INT,
};

static struct i2c_board_info i2c_devs11_emul[] __initdata = {
	{
		I2C_BOARD_INFO("lps331ap", 0x5D),
		.platform_data = &lps331ap_pdata,
	},
};
#endif

static int __init midas_sensor_init(void)
{
	int ret = 0;

	/* Gyro & Accelerometer Sensor */
#if defined(CONFIG_SENSORS_LSM330DLC)
	ret = accel_gpio_init();
	if (ret < 0) {
		pr_err("%s, accel_gpio_init fail(err=%d)\n", __func__, ret);
		return ret;
	}
	ret = gyro_gpio_init();
	if (ret < 0) {
		pr_err("%s, gyro_gpio_init(err=%d)\n", __func__, ret);
		return ret;
	}
#elif defined(CONFIG_SENSORS_K3DH)
	ret = accel_gpio_init();
	if (ret < 0) {
		pr_err("%s, accel_gpio_init fail(err=%d)\n", __func__, ret);
		return ret;
	}
#endif
	ret = i2c_add_devices(1, i2c_devs1, ARRAY_SIZE(i2c_devs1));
	if (ret < 0) {
		pr_err("%s, i2c1 adding i2c fail(err=%d)\n", __func__, ret);
		return ret;
	}

	/* Optical Sensor */
#if defined(CONFIG_SENSORS_GP2A) || defined(CONFIG_SENSORS_CM36651) || \
	defined(CONFIG_SENSORS_CM3663)
	ret = optical_gpio_init();
	if (ret) {
		pr_err("%s, optical_gpio_init(err=%d)\n", __func__, ret);
		return ret;
	}
	ret = i2c_add_devices(9, i2c_devs9_emul, ARRAY_SIZE(i2c_devs9_emul));
	if (ret < 0) {
		pr_err("%s, i2c9 adding i2c fail(err=%d)\n", __func__, ret);
		return ret;
	}
#elif defined(CONFIG_SENSORS_BH1721)
	ret = i2c_add_devices(9, i2c_devs9_emul, ARRAY_SIZE(i2c_devs9_emul));
	if (ret < 0) {
		pr_err("%s, i2c9 adding i2c fail(err=%d)\n", __func__, ret);
		return ret;
	}
#endif

#if defined(CONFIG_SENSORS_GP2A)
	ret = platform_device_register(&opt_gp2a);
	if (ret < 0) {
		pr_err("%s, failed to register opt_gp2a(err=%d)\n",
			__func__, ret);
		return ret;
	}
#endif

	/* Magnetic Sensor */
#ifdef CONFIG_SENSORS_AK8975C
	ret = ak8975c_gpio_init();
	if (ret < 0) {
		pr_err("%s, ak8975c_gpio_init fail(err=%d)\n", __func__, ret);
		return ret;
	}
	ret = i2c_add_devices(10, i2c_devs10_emul, ARRAY_SIZE(i2c_devs10_emul));
	if (ret < 0) {
		pr_err("%s, i2c10 adding i2c fail(err=%d)\n", __func__, ret);
		return ret;
	}
#endif
		/* Magnetic Sensor */
#ifdef CONFIG_SENSORS_AK8963C
	ret = ak8963c_gpio_init();
	if (ret < 0) {
		pr_err("%s, ak8975c_gpio_init fail(err=%d)\n", __func__, ret);
		return ret;
	}
	ret = i2c_add_devices(10, i2c_devs10_emul, ARRAY_SIZE(i2c_devs10_emul));
	if (ret < 0) {
		pr_err("%s, i2c10 adding i2c fail(err=%d)\n", __func__, ret);
		return ret;
	}
#endif

	/* Pressure Sensor */
#ifdef CONFIG_SENSORS_LPS331
	ret = lps331_gpio_init();
	if (ret < 0) {
		pr_err("%s, ak8975c_gpio_init fail(err=%d)\n", __func__, ret);
		return ret;
	}
	ret = i2c_add_devices(11, i2c_devs11_emul, ARRAY_SIZE(i2c_devs11_emul));
	if (ret < 0) {
		pr_err("%s, i2c1 adding i2c fail(err=%d)\n", __func__, ret);
		return ret;
	}
#endif
	return ret;
}
module_init(midas_sensor_init);
