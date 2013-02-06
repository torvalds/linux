/*
 * camera class init
 */

#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/vmalloc.h>
#include <media/v4l2-device.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/regulator/machine.h>
#include <linux/init.h>

#include <plat/devs.h>
#include <plat/csis.h>
#include <plat/pd.h>
#include <plat/gpio-cfg.h>
#ifdef CONFIG_VIDEO_SAMSUNG_S5P_FIMC
#include <plat/fimc-core.h>
#include <media/s5p_fimc.h>
#endif

#ifdef CONFIG_VIDEO_EXYNOS_FIMC_LITE
#include <media/exynos_flite.h>
#endif

#if defined(CONFIG_VIDEO_S5C73M3) || defined(CONFIG_VIDEO_SLP_S5C73M3)
#include <media/s5c73m3_platform.h>
#endif

#if defined(CONFIG_VIDEO_M5MO)
#include <mach/regs-gpio.h>
#include <media/m5mo_platform.h>
#endif

#if defined(CONFIG_VIDEO_M9MO)
#include <mach/regs-gpio.h>
#include <media/m9mo_platform.h>
#endif

#if defined(CONFIG_VIDEO_ISX012)
#include <media/isx012_platform.h>
#endif
#if defined(CONFIG_VIDEO_S5K5CCGX_COMMON)
#include <media/s5k5ccgx_platform.h>
#endif

#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
#include <mach/secmem.h>
#endif

#ifdef CONFIG_VIDEO_SR200PC20M
#include <media/sr200pc20m_platform.h>
#endif

#ifdef CONFIG_VIDEO_SR200PC20
#include <media/sr200pc20_platform.h>
#endif

struct class *camera_class;

static int __init camera_class_init(void)
{
	camera_class = class_create(THIS_MODULE, "camera");
	if (IS_ERR(camera_class)) {
		pr_err("Failed to create class(camera)!\n");
		return PTR_ERR(camera_class);
	}

	return 0;
}

subsys_initcall(camera_class_init);

#if defined(CONFIG_MACH_C1_KOR_SKT) || defined(CONFIG_MACH_C1_KOR_KT)
#define FRONT_CAM_MCLK_DEVIDED_REVISION	0x06
#define USE_8M_CAM_SENSOR_CORE_REVISION	0x09
#elif defined(CONFIG_MACH_C1_KOR_LGT)
#define FRONT_CAM_MCLK_DEVIDED_REVISION	0x04
#define USE_8M_CAM_SENSOR_CORE_REVISION	0x07
#elif defined(CONFIG_MACH_C1_USA_ATT)
#define FRONT_CAM_MCLK_DEVIDED_REVISION	0x05
#elif defined(CONFIG_MACH_M3)
#define FRONT_CAM_MCLK_DEVIDED_REVISION	0x0A
#else
#define FRONT_CAM_MCLK_DEVIDED_REVISION	0x08
#endif

#if defined(CONFIG_VIDEO_FIMC)
/*
 * External camera reset
 * Because the most of cameras take i2c bus signal, so that
 * you have to reset at the boot time for other i2c slave devices.
 * This function also called at fimc_init_camera()
 * Do optimization for cameras on your platform.
 */

#define CAM_CHECK_ERR_RET(x, msg)					\
	if (unlikely((x) < 0)) {					\
		printk(KERN_ERR "\nfail to %s: err = %d\n", msg, x);	\
		return x;						\
	}
#define CAM_CHECK_ERR(x, msg)						\
	if (unlikely((x) < 0)) {					\
		printk(KERN_ERR "\nfail to %s: err = %d\n", msg, x);	\
	}
#define CAM_CHECK_ERR_GOTO(x, out, fmt, ...) \
	if (unlikely((x) < 0)) { \
		printk(KERN_ERR fmt, ##__VA_ARGS__); \
		goto out; \
	}

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
		regulator_enable(regulator);
		regulator_put(regulator);

		/* VMIPI_1.8V */
		regulator = regulator_get(NULL, "vmipi_1.8v");
		if (IS_ERR(regulator))
			goto error_out;
		regulator_enable(regulator);
		regulator_put(regulator);

		printk(KERN_WARNING "%s: vmipi_1.0v and vmipi_1.8v were ON\n",
		       __func__);
	} else {
		/* VMIPI_1.8V */
		regulator = regulator_get(NULL, "vmipi_1.8v");
		if (IS_ERR(regulator))
			goto error_out;
		if (regulator_is_enabled(regulator)) {
			printk(KERN_WARNING "%s: vmipi_1.8v is on. so OFF\n",
			       __func__);
			ret = regulator_disable(regulator);
		}
		regulator_put(regulator);

		/* VMIPI_1.0V */
		regulator = regulator_get(NULL, "vmipi_1.0v");
		if (IS_ERR(regulator))
			goto error_out;
		if (regulator_is_enabled(regulator)) {
			printk(KERN_WARNING "%s: vmipi_1.1v is on. so OFF\n",
			       __func__);
			ret = regulator_disable(regulator);
		}
		regulator_put(regulator);

		printk(KERN_WARNING "%s: vmipi_1.0v and vmipi_1.8v were OFF\n",
		       __func__);
	}

	return 0;

error_out:
	printk(KERN_ERR "%s: ERROR: failed to check mipi-power\n", __func__);
	return 0;
}

#ifdef CONFIG_WRITEBACK_ENABLED
#define WRITEBACK_ENABLED
#endif
#ifdef WRITEBACK_ENABLED
static int get_i2c_busnum_writeback(void)
{
	return 0;
}

static struct i2c_board_info writeback_i2c_info = {
	I2C_BOARD_INFO("WriteBack", 0x0),
};

static struct s3c_platform_camera writeback = {
	.id		= CAMERA_WB,
	.fmt		= ITU_601_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_CBYCRY,
	.get_i2c_busnum = get_i2c_busnum_writeback,
	.info		= &writeback_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_YUV444,
	.line_length	= 1280,
	.width		= 720,
	.height		= 1280,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 720,
		.height	= 1280,
	},

	.initialized	= 0,
};
#endif

#ifdef CONFIG_VIDEO_EXYNOS_FIMC_IS
#ifdef CONFIG_VIDEO_S5K6A3
#ifdef CONFIG_MACH_P4NOTE
/* For P4Note PegasusQ */
static int s5k6a3_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);

	ret = gpio_request(GPIO_VT_CAM_nRST, "GPJ1");
	if (unlikely(ret)) {
		printk(KERN_ERR "request GPIO_VT_CAM_nRST\n");
		return ret;
	}

	/* VT_CAM_2.8V */
	regulator = regulator_get(NULL, "cam_a2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "cam_a2.8v");
	udelay(100);

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_VTCAM_MCLK, S3C_GPIO_SFN(3));
	s3c_gpio_setpull(GPIO_VTCAM_MCLK, S3C_GPIO_PULL_NONE);
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	udelay(1);

	/* VT_CAM_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "vt_core_1.8v");
	udelay(1000);

	/* VT_CAM_nRST */
	ret = gpio_direction_output(GPIO_VT_CAM_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "GPIO_VT_CAM_nRST");
	udelay(600);

	ret = gpio_direction_output(GPIO_VT_CAM_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "GPIO_VT_CAM_nRST");
	udelay(600);

	ret = gpio_direction_output(GPIO_VT_CAM_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "GPIO_VT_CAM_nRST");

	gpio_free(GPIO_VT_CAM_nRST);

	return ret;
}

static int s5k6a3_power_down(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);

	ret = gpio_request(GPIO_VT_CAM_nRST, "GPJ1");
	if (unlikely(ret)) {
		printk(KERN_ERR "request GPIO_VT_CAM_nRST\n");
		return ret;
	}

	/* VT_CAM_nRST */
	ret = gpio_direction_output(GPIO_VT_CAM_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "GPIO_VT_CAM_nRST");

	/* VT_CAM_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "vt_core_1.8v");


	/* VT_CAM_2.8V */
	regulator = regulator_get(NULL, "cam_a2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "cam_a2.8v");
	udelay(100);

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_VTCAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_VTCAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	udelay(1);

	gpio_free(GPIO_VT_CAM_nRST);

	return ret;
}
#else /* !CONFIG_MACH_P4NOTE */
static int s5k6a3_gpio_request(void)
{
	int ret = 0;

	/* SENSOR_A2.8V */
	ret = gpio_request(GPIO_CAM_IO_EN, "GPM0");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(GPIO_CAM_IO_EN)\n");
		return ret;
	}

	if (system_rev <= FRONT_CAM_MCLK_DEVIDED_REVISION)
		ret = gpio_request(GPIO_CAM_MCLK, "GPJ1");
	else
		ret = gpio_request(GPIO_VTCAM_MCLK, "GPM2");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(GPIO_VTCAM_MCLK)\n");
		return ret;
	}

	ret = gpio_request(GPIO_CAM_VT_nRST, "GPM1");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(GPIO_CAM_VT_nRST)\n");
		return ret;
	}

	return ret;
}

static int s5k6a3_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);

	s5k6a3_gpio_request();

	/* CAM_SENSOR_A2.8V */
	ret = gpio_direction_output(GPIO_CAM_IO_EN, 1);
	CAM_CHECK_ERR_RET(ret, "output GPIO_CAM_IO_EN");
	/* delay is needed : external LDO control is slower than MCLK control*/
	udelay(100);

	/* MCLK */
	if (system_rev <= FRONT_CAM_MCLK_DEVIDED_REVISION) {
		ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(GPIO_CAM_MCLK, S5P_GPIO_DRVSTR_LV2);
	} else {
		ret = s3c_gpio_cfgpin(GPIO_VTCAM_MCLK, S3C_GPIO_SFN(3));
		s3c_gpio_setpull(GPIO_VTCAM_MCLK, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(GPIO_VTCAM_MCLK, S5P_GPIO_DRVSTR_LV2);
	}
	CAM_CHECK_ERR_RET(ret, "cfg mclk");

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_cam_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable vt_cam_1.8v");
	udelay(1000);

	/* VT_2.8V */
	regulator = regulator_get(NULL, "vt_cam_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable vt_cam_2.8v");
	udelay(100);

	/* VT_RESET */
	ret = gpio_direction_output(GPIO_CAM_VT_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "output GPIO_CAM_VT_nRST");
	udelay(600);

	ret = gpio_direction_output(GPIO_CAM_VT_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "output GPIO_CAM_VT_nRST");
	udelay(600);

	ret = gpio_direction_output(GPIO_CAM_VT_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "output GPIO_CAM_VT_nRST");

	gpio_free(GPIO_CAM_IO_EN);
	gpio_free(GPIO_CAM_VT_nRST);
	if (system_rev <= FRONT_CAM_MCLK_DEVIDED_REVISION)
		gpio_free(GPIO_CAM_MCLK);
	else
		gpio_free(GPIO_VTCAM_MCLK);

	return ret;
}

static int s5k6a3_power_down(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);

	s5k6a3_gpio_request();

	/* VT_RESET */
	ret = gpio_direction_output(GPIO_CAM_VT_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "output GPIO_CAM_VT_nRST");

	/* VT_2.8V */
	regulator = regulator_get(NULL, "vt_cam_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable vt_cam_2.8v");

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_cam_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable vt_cam_1.8v");

	/* CAM_SENSOR_A2.8V */
	ret = gpio_direction_output(GPIO_CAM_IO_EN, 0);
	CAM_CHECK_ERR_RET(ret, "output GPIO_CAM_IO_EN");
	/* delay is needed : external LDO control is slower than MCLK control*/
	udelay(500);

	/* MCLK */
	if (system_rev <= FRONT_CAM_MCLK_DEVIDED_REVISION) {
		ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
		s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);

	} else {
		ret = s3c_gpio_cfgpin(GPIO_VTCAM_MCLK, S3C_GPIO_INPUT);
		s3c_gpio_setpull(GPIO_VTCAM_MCLK, S3C_GPIO_PULL_DOWN);
	}
	CAM_CHECK_ERR(ret, "cfg mclk");

	gpio_free(GPIO_CAM_IO_EN);
	gpio_free(GPIO_CAM_VT_nRST);

	if (system_rev <= FRONT_CAM_MCLK_DEVIDED_REVISION)
		gpio_free(GPIO_CAM_MCLK);
	else
		gpio_free(GPIO_VTCAM_MCLK);

	return ret;
}
#endif /* CONFIG_MACH_P4NOTE */

static int s5k6a3_power(int enable)
{
	int ret = 0;

	if (enable) {
		ret = s5k6a3_power_on();
		if (unlikely(ret)) {
			printk(KERN_ERR "%s: power-on fail\n", __func__);
			goto error_out;
		}
	} else
		ret = s5k6a3_power_down();

	ret = s3c_csis_power(enable);

error_out:
	return ret;
}

static const char *s5k6a3_get_clk_name(void)
{
#ifdef CONFIG_MACH_P4NOTE
	return "sclk_cam1";
#else
	if (system_rev <= FRONT_CAM_MCLK_DEVIDED_REVISION)
		return "sclk_cam0";
	else
		return "sclk_cam1";
#endif
}

static struct s3c_platform_camera s5k6a3 = {
	.id		= CAMERA_CSI_D,
	.get_clk_name = s5k6a3_get_clk_name,
	.cam_power	= s5k6a3_power,
	.type		= CAM_TYPE_MIPI,
	.fmt		= MIPI_CSI_RAW10,
	.order422	= CAM_ORDER422_8BIT_YCBYCR,
	.pixelformat	= V4L2_PIX_FMT_UYVY,
	.line_length	= 1920,
	.width		= 1920,
	.height		= 1080,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 1920,
		.height	= 1080,
	},
	.srclk_name	= "xusbxti",
	.clk_rate	= 24000000,
	.mipi_lanes	= 1,
	.mipi_settle	= 18,
	.mipi_align	= 24,

	.initialized	= 0,
	.flite_id	= FLITE_IDX_B,
	.use_isp	= true,
	.sensor_index	= 102,
};

#ifdef CONFIG_S5K6A3_CSI_D
static struct s3c_platform_camera s5k6a3_fd = {
	.id		= CAMERA_CSI_D,
	.get_clk_name = s5k6a3_get_clk_name,
	.cam_power	= s5k6a3_power,
	.type		= CAM_TYPE_MIPI,
	.fmt		= MIPI_CSI_RAW10,
	.order422	= CAM_ORDER422_8BIT_YCBYCR,
	.pixelformat	= V4L2_PIX_FMT_UYVY,
	.line_length	= 1920,
	.width		= 1920,
	.height		= 1080,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 1920,
		.height	= 1080,
	},
	.srclk_name	= "xusbxti",
	.clk_rate	= 24000000,
	.mipi_lanes	= 1,
	.mipi_settle	= 18,
	.mipi_align	= 24,

	.initialized	= 0,
	.flite_id	= FLITE_IDX_B,
	.use_isp	= true,
	.sensor_index	= 200,
};
#endif
#endif
#endif

#if defined(CONFIG_VIDEO_S5C73M3) || defined(CONFIG_VIDEO_SLP_S5C73M3)
static int vddCore = 1150000;
static bool isVddCoreSet;
static void s5c73m3_set_vdd_core(int level)
{
	vddCore = level;
	isVddCoreSet = true;
	printk(KERN_ERR "%s : %d\n", __func__, vddCore);
}

static void s5c73m3_check_vdd_core(void)
{
	struct file *fp;
	mm_segment_t old_fs;
	u8 *buf = NULL;
	int err = 0;
	int nread = 0;
	int voltage = 0;
	int count = 0;

	if (!isVddCoreSet) {
		old_fs = get_fs();
		set_fs(KERNEL_DS);

		fp = filp_open("/data/ISP_CV", O_RDONLY, 0);
		if (IS_ERR(fp)) {
			printk(KERN_ERR "failed open file. :: %ld\n",
				PTR_ERR(fp));
			set_fs(old_fs);
			return;
		}

		buf = vmalloc(10);
		if (!buf) {
			printk(KERN_ERR "failed to allocate memory\n");
			err = -ENOMEM;
			goto out;
		}

		nread = vfs_read(fp, (char __user *)buf, 10, &fp->f_pos);
		if (nread != 10) {
			printk(KERN_ERR "failed to read file, %d Bytes\n",
				nread);
			err = -EIO;
			goto out;
		}

		while (buf[count] != '\0' &&
			buf[count] >= '0' && buf[count] <= '9') {
			voltage = voltage * 10 + buf[count] - '0';
			++count;
		}

		if (voltage == 1000000 || voltage == 1050000 ||
		voltage == 1100000 || voltage == 1150000) {
			printk(KERN_ERR "@@@@ Voltage = %d", voltage);
			vddCore = voltage;
			/*isVddCoreSet = true;*/
		}
out:
		if (buf != NULL)
			vfree(buf);

		if (fp !=  NULL)
			filp_close(fp, current->files);

		set_fs(old_fs);
	}
}
static bool s5c73m3_is_vdd_core_set(void)
{
	return isVddCoreSet;
}

static int s5c73m3_is_isp_reset(void)
{
	int ret = 0;

		ret = gpio_request(GPIO_ISP_RESET, "GPF1");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_ISP_RESET)\n");
		return ret;
	}

	/* ISP_RESET */
	ret = gpio_direction_output(GPIO_ISP_RESET, 0);
	CAM_CHECK_ERR_RET(ret, "output GPIO_ISP_RESET");
	udelay(10);	/* 200 cycle */
	ret = gpio_direction_output(GPIO_ISP_RESET, 1);
	CAM_CHECK_ERR_RET(ret, "output GPIO_ISP_RESET");
	udelay(10);	/* 200 cycle */

	gpio_free(GPIO_ISP_RESET);

	return ret;
}

static int s5c73m3_gpio_request(void)
{
	int ret = 0;

	ret = gpio_request(GPIO_ISP_STANDBY, "GPM0");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_ISP_STANDBY)\n");
		return ret;
	}

	ret = gpio_request(GPIO_ISP_RESET, "GPF1");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_ISP_RESET)\n");
		return ret;
	}

	/* SENSOR_A2.8V */
	ret = gpio_request(GPIO_CAM_IO_EN, "GPM0");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(GPIO_CAM_IO_EN)\n");
		return ret;
	}

	ret = gpio_request(GPIO_CAM_AF_EN, "GPM0");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_AF_EN)\n");
		return ret;
	}

	ret = gpio_request(GPIO_ISP_CORE_EN, "GPM0");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(GPIO_ISP_CORE_EN)\n");
		return ret;
	}

#if defined(CONFIG_MACH_C1) && defined(CONFIG_TARGET_LOCALE_KOR)
	if (system_rev >= USE_8M_CAM_SENSOR_CORE_REVISION) {
		ret = gpio_request(GPIO_CAM_SENSOR_CORE_EN, "GPM0");
		if (ret) {
			printk(KERN_ERR "fail to request gpio(GPIO_CAM_SENSOR_CORE_EN)\n");
			return ret;
		}
	}
#endif

	return ret;
}

static void s5c73m3_gpio_free(void)
{
	gpio_free(GPIO_ISP_STANDBY);
	gpio_free(GPIO_ISP_RESET);
	gpio_free(GPIO_CAM_IO_EN);
	gpio_free(GPIO_CAM_AF_EN);
	gpio_free(GPIO_ISP_CORE_EN);

#if defined(CONFIG_MACH_C1) && defined(CONFIG_TARGET_LOCALE_KOR)
	if (system_rev >= USE_8M_CAM_SENSOR_CORE_REVISION)
		gpio_free(GPIO_CAM_SENSOR_CORE_EN);
#endif
}

static int s5c73m3_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);

#ifndef CONFIG_VIDEO_SLP_S5C73M3
	s5c73m3_check_vdd_core();
#endif
	printk(KERN_DEBUG "s5c73m3 vddCore : %d\n", vddCore);

	s5c73m3_gpio_request();

	/* CAM_ISP_CORE_1.2V */
	ret = gpio_direction_output(GPIO_ISP_CORE_EN, 1);
	CAM_CHECK_ERR_RET(ret, "output GPIO_ISP_CORE_EN");

#if !defined(CONFIG_MACH_GRANDE)
	regulator = regulator_get(NULL, "cam_isp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	regulator_set_voltage(regulator, vddCore, vddCore);
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp_core_1.2v");
#endif

	/* CAM_SENSOR_A2.8V */
	ret = gpio_direction_output(GPIO_CAM_IO_EN, 1);
	CAM_CHECK_ERR_RET(ret, "output IO_EN");

	/* CAM_SENSOR_CORE_1.2V */
#if defined(CONFIG_MACH_C1) && defined(CONFIG_TARGET_LOCALE_KOR)
	if (system_rev >= USE_8M_CAM_SENSOR_CORE_REVISION) {
		ret = gpio_direction_output(GPIO_CAM_SENSOR_CORE_EN, 1);
		CAM_CHECK_ERR_RET(ret, "output CAM_SENSOR_CORE_EN");
		mdelay(5);
	} else {
		regulator = regulator_get(NULL, "cam_sensor_core_1.2v");
		if (IS_ERR(regulator))
			return -ENODEV;
		ret = regulator_enable(regulator);
		regulator_put(regulator);
		CAM_CHECK_ERR_RET(ret, "enable cam_sensor_core_1.2v");
		/* delay is needed : pmu control is slower than gpio control*/
		mdelay(5);
	}
#else
	regulator = regulator_get(NULL, "cam_sensor_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_sensor_core_1.2v");
       /* delay is needed : pmu control is slower than gpio control*/
	mdelay(5);
#endif

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	s5p_gpio_set_drvstr(GPIO_CAM_MCLK, S5P_GPIO_DRVSTR_LV3);

	/* CAM_AF_2.8V */
	ret = gpio_direction_output(GPIO_CAM_AF_EN, 1);
	CAM_CHECK_ERR_RET(ret, "output GPIO_CAM_AF_EN");
	udelay(2000);

	/* CAM_ISP_SENSOR_1.8V */
	regulator = regulator_get(NULL, "cam_isp_sensor_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp_sensor_1.8v");

	/* CAM_ISP_MIPI_1.2V */
	regulator = regulator_get(NULL, "cam_isp_mipi_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp_mipi_1.2v");
       /* delay is needed : pmu control is slower than gpio control*/
	mdelay(5);

	/* ISP_STANDBY */
	ret = gpio_direction_output(GPIO_ISP_STANDBY, 1);
	CAM_CHECK_ERR_RET(ret, "output GPIO_ISP_STANDBY");
	udelay(100);		/* 2000 cycle */

	/* ISP_RESET */
	ret = gpio_direction_output(GPIO_ISP_RESET, 1);
	CAM_CHECK_ERR_RET(ret, "output GPIO_ISP_RESET");
	udelay(10);		/* 200 cycle */

	s5c73m3_gpio_free();

	return ret;
}

static int s5c73m3_power_down(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);

	s5c73m3_gpio_request();

	/* ISP_STANDBY */
	ret = gpio_direction_output(GPIO_ISP_STANDBY, 0);
	CAM_CHECK_ERR_RET(ret, "output GPIO_ISP_STANDBY");
	udelay(2);		/* 40 cycle */

	/* ISP_RESET */
	ret = gpio_direction_output(GPIO_ISP_RESET, 0);
	CAM_CHECK_ERR_RET(ret, "output GPIO_ISP_RESET");

	/* CAM_AF_2.8V */
	ret = gpio_direction_output(GPIO_CAM_AF_EN, 0);
	CAM_CHECK_ERR_RET(ret, "output GPIO_CAM_AF_EN");

	/* CAM_ISP_MIPI_1.2V */
	regulator = regulator_get(NULL, "cam_isp_mipi_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_isp_mipi_1.2v");
	udelay(10);		/* 200 cycle */

	/* CAM_ISP_SENSOR_1.8V */
	regulator = regulator_get(NULL, "cam_isp_sensor_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_isp_sensor_1.8v");

	/* CAM_SENSOR_CORE_1.2V */
#if defined(CONFIG_MACH_C1) && defined(CONFIG_TARGET_LOCALE_KOR)
	if (system_rev >= USE_8M_CAM_SENSOR_CORE_REVISION) {
		ret = gpio_direction_output(GPIO_CAM_SENSOR_CORE_EN, 0);
		CAM_CHECK_ERR_RET(ret, "output CAM_SENSOR_CORE_EN");
		udelay(500);
	} else {
		regulator = regulator_get(NULL, "cam_sensor_core_1.2v");
		if (IS_ERR(regulator))
			return -ENODEV;
		if (regulator_is_enabled(regulator))
			ret = regulator_force_disable(regulator);
		regulator_put(regulator);
		CAM_CHECK_ERR(ret, "disable cam_sensor_core_1.2v");
		/* delay is needed : hw request*/
		udelay(500);
	}
#else
	regulator = regulator_get(NULL, "cam_sensor_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_sensor_core_1.2v");
	/* delay is needed : hw request*/
	udelay(500);
#endif

	/* CAM_SENSOR_A2.8V */
	ret = gpio_direction_output(GPIO_CAM_IO_EN, 0);
	CAM_CHECK_ERR_RET(ret, "output GPIO_CAM_IO_EN");

#if !defined(CONFIG_MACH_GRANDE)
	/* CAM_ISP_CORE_1.2V */
	regulator = regulator_get(NULL, "cam_isp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_isp_core_1.2v");
#endif

	ret = gpio_direction_output(GPIO_ISP_CORE_EN, 0);
	CAM_CHECK_ERR_RET(ret, "output GPIO_CAM_ISP_CORE_EN");
	/* delay is needed : hw request*/
	mdelay(30);

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");

	s5c73m3_gpio_free();

	return ret;
}

static int s5c73m3_power(int enable)
{
	int ret = 0;

	if (enable) {
		ret = s5c73m3_power_on();

		if (unlikely(ret))
			goto error_out;
	} else
		ret = s5c73m3_power_down();

	ret = s3c_csis_power(enable);

error_out:
	return ret;
}

static int s5c73m3_get_i2c_busnum(void)
{
	return 0;
}

static const char *s5c73m3_get_clk_name(void)
{
	return "sclk_cam0";
}

static struct s5c73m3_platform_data s5c73m3_plat = {
	.default_width = 640,	/* 1920 */
	.default_height = 480,	/* 1080 */
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 1,
	.set_vdd_core = s5c73m3_set_vdd_core,
	.is_vdd_core_set = s5c73m3_is_vdd_core_set,
	.is_isp_reset = s5c73m3_is_isp_reset,
	.power_on_off = s5c73m3_power,
};

static struct i2c_board_info s5c73m3_i2c_info = {
	I2C_BOARD_INFO("S5C73M3", 0x78 >> 1),
	.platform_data = &s5c73m3_plat,
};

static struct s3c_platform_camera s5c73m3 = {
	.id = CAMERA_CSI_C,
	.get_clk_name = s5c73m3_get_clk_name,
	.get_i2c_busnum = s5c73m3_get_i2c_busnum,
	.cam_power = s5c73m3_power,
	.type = CAM_TYPE_MIPI,
	.fmt = MIPI_CSI_YCBCR422_8BIT,
	.order422 = CAM_ORDER422_8BIT_YCBYCR,
	.info = &s5c73m3_i2c_info,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.srclk_name = "xusbxti",	/* "mout_mpll" */
	.clk_rate = 24000000,	/* 48000000 */
	.line_length = 1920,
	.width = 640,
	.height = 480,
	.window = {
		.left = 0,
		.top = 0,
		.width = 640,
		.height = 480,
	},

	.mipi_lanes = 4,
	.mipi_settle = 12,
	.mipi_align = 32,

	/* Polarity */
	.inv_pclk = 1,
	.inv_vsync = 1,
	.inv_href = 0,
	.inv_hsync = 0,
	.reset_camera = 0,
	.initialized = 0,
};
#endif

#ifdef CONFIG_VIDEO_M5MO
static int m5mo_get_i2c_busnum(void)
{
#ifdef CONFIG_VIDEO_M5MO_USE_SWI2C
	return 25;
#else
	return 0;
#endif
}

static int m5mo_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);

	ret = gpio_request(GPIO_CAM_VT_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_VT_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_ISP_CORE_EN, "GPM0");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(CAM_SENSOR_CORE)\n");
		return ret;
	}
	ret = gpio_request(GPIO_ISP_RESET, "GPY3");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_ISP_RESET)\n");
		return ret;
	}
	/* CAM_VT_nSTBY low */
	ret = gpio_direction_output(GPIO_CAM_VT_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "output VT_nSTBY");

	/* CAM_VT_nRST	low */
	gpio_direction_output(GPIO_CAM_VT_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "output VT_nRST");
	udelay(10);

	/* CAM_ISP_CORE_1.2V */
	ret = gpio_direction_output(GPIO_ISP_CORE_EN, 1);
	CAM_CHECK_ERR_RET(ret, "output GPIO_ISP_CORE_EN");

	regulator = regulator_get(NULL, "cam_isp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp_core_1.2v");
	udelay(10);
	/* CAM_SENSOR_CORE_1.2V */
	regulator = regulator_get(NULL, "cam_sensor_core_1.2v");
	if (IS_ERR(regulator)) {
		CAM_CHECK_ERR_RET(ret, "output Err cam_sensor_core_1.2v");
		return -ENODEV;
	}
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_sensor_core_1.2v");
	udelay(10);

	/* CAM_SENSOR_A2.8V */
	regulator = regulator_get(NULL, "cam_sensor_a2.8v");
	if (IS_ERR(regulator)) {
		CAM_CHECK_ERR_RET(ret, "output Err cam_sensor_a2.8v");
		return -ENODEV;
	}
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_sensor_a2.8v");
	/* it takes about 100us at least during level transition.*/
	udelay(160); /* 130us -> 160us */
	/* VT_CAM_DVDD_1.8V */
	regulator = regulator_get(NULL, "vt_cam_dvdd_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable vt_cam_dvdd_1.8v");
	udelay(10);

	/* CAM_AF_2.8V */
	regulator = regulator_get(NULL, "cam_af_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "output cam_af_2.8v");
	mdelay(7);

	/* VT_CAM_1.8V */
	regulator = regulator_get(NULL, "vt_cam_1.8v");
	if (IS_ERR(regulator)) {
		CAM_CHECK_ERR_RET(ret, "output Err vt_cam_1.8v");
		return -ENODEV;
	}
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable vt_cam_1.8v");
	udelay(20);

	/* CAM_ISP_1.8V */
	regulator = regulator_get(NULL, "cam_isp_1.8v");
	if (IS_ERR(regulator)) {
		CAM_CHECK_ERR_RET(ret, "output Err cam_isp_1.8v");
		return -ENODEV;
	}
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp_1.8v");
	udelay(120); /* at least */

	/* CAM_ISP_SEN_IO_1.8V */
	regulator = regulator_get(NULL, "cam_isp_sensor_1.8v");
	if (IS_ERR(regulator)) {
		CAM_CHECK_ERR_RET(ret, "output Err cam_isp_sensor_1.8v");
		return -ENODEV;
	}
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp_sensor_1.8v");
	udelay(30);

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	udelay(70);
	/* ISP_RESET */
	ret = gpio_direction_output(GPIO_ISP_RESET, 1);
	CAM_CHECK_ERR_RET(ret, "output reset");
	mdelay(4);

	gpio_free(GPIO_CAM_VT_nSTBY);
	gpio_free(GPIO_CAM_VT_nRST);
	gpio_free(GPIO_ISP_CORE_EN);
	gpio_free(GPIO_ISP_RESET);

	return ret;
}

static int m5mo_power_down(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);

	ret = gpio_request(GPIO_CAM_VT_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_VT_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_ISP_CORE_EN, "GPM0");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(CAM_SENSOR_CORE)\n");
		return ret;
	}
	ret = gpio_request(GPIO_ISP_RESET, "GPY3");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_ISP_RESET)\n");
		return ret;
	}

	/* s3c_i2c0_force_stop(); */

	mdelay(3);

	/* ISP_RESET */
	ret = gpio_direction_output(GPIO_ISP_RESET, 0);
	CAM_CHECK_ERR(ret, "output reset");
	mdelay(2);

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");
	udelay(20);

	/* CAM_AF_2.8V */
	regulator = regulator_get(NULL, "cam_af_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_af_2.8v");

	/* CAM_ISP_SEN_IO_1.8V */
	regulator = regulator_get(NULL, "cam_isp_sensor_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable, cam_isp_sensor_1.8v");
	udelay(10);

	/* CAM_ISP_1.8V */
	regulator = regulator_get(NULL, "cam_isp_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_isp_1.8v");
	udelay(500); /* 100us -> 500us */

	/* VT_CAM_1.8V */
	regulator = regulator_get(NULL, "vt_cam_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable vt_cam_1.8v");
	udelay(250); /* 10us -> 250us */

	/* VT_CAM_DVDD_1.8V */
	regulator = regulator_get(NULL, "vt_cam_dvdd_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable vt_cam_dvdd_1.8v");
	udelay(300); /*10 -> 300 us */

	/* CAM_SENSOR_A2.8V */
	regulator = regulator_get(NULL, "cam_sensor_a2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_sensor_a2.8v");
	udelay(800);

	/* CAM_SENSOR_CORE_1.2V */
	regulator = regulator_get(NULL, "cam_sensor_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_sensor_core_1.2v");
	udelay(5);

	/* CAM_ISP_CORE_1.2V */
	ret = gpio_direction_output(GPIO_ISP_CORE_EN, 0);
	CAM_CHECK_ERR(ret, "output ISP_CORE");

	regulator = regulator_get(NULL, "cam_isp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "disable cam_isp_core_1.2v");

	gpio_free(GPIO_CAM_VT_nSTBY);
	gpio_free(GPIO_CAM_VT_nRST);
	gpio_free(GPIO_ISP_CORE_EN);
	gpio_free(GPIO_ISP_RESET);

	return ret;
}

static int m5mo_flash_power(int enable)
{
/* TODO */
	return 0;
}

static int m5mo_power(int enable)
{
	int ret = 0;

	printk(KERN_ERR "%s %s\n", __func__, enable ? "on" : "down");
	if (enable) {
		ret = m5mo_power_on();
		if (unlikely(ret))
			goto error_out;
	} else
		ret = m5mo_power_down();

	ret = s3c_csis_power(enable);
	m5mo_flash_power(enable);

error_out:
	return ret;
}

static int m5mo_config_isp_irq(void)
{
	s3c_gpio_cfgpin(GPIO_ISP_INT, S3C_GPIO_SFN(0xF));
	s3c_gpio_setpull(GPIO_ISP_INT, S3C_GPIO_PULL_NONE);
	return 0;
}

static const char *m5mo_get_clk_name(void)
{
	return "sclk_cam0";
}

static struct m5mo_platform_data m5mo_plat = {
	.default_width = 640, /* 1920 */
	.default_height = 480, /* 1080 */
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 1,
	.config_isp_irq = m5mo_config_isp_irq,
	.irq = IRQ_EINT(24),
};

static struct i2c_board_info  m5mo_i2c_info = {
	I2C_BOARD_INFO("M5MO", 0x1F),
	.platform_data = &m5mo_plat,
};

static struct s3c_platform_camera m5mo = {
	.id		= CAMERA_CSI_C,
	.get_clk_name	= m5mo_get_clk_name,
	.get_i2c_busnum	= m5mo_get_i2c_busnum,
	.cam_power	= m5mo_power, /*smdkv310_mipi_cam0_reset,*/
	.type		= CAM_TYPE_MIPI,
	.fmt		= ITU_601_YCBCR422_8BIT, /*MIPI_CSI_YCBCR422_8BIT*/
	.order422	= CAM_ORDER422_8BIT_CBYCRY,

	.info		= &m5mo_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_UYVY,
	.srclk_name	= "xusbxti", /* "mout_mpll" */
	.clk_rate	= 24000000, /* 48000000 */
	.line_length	= 1920,
	.width		= 640,
	.height		= 480,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 640,
		.height	= 480,
	},

	.mipi_lanes	= 2,
	.mipi_settle	= 12,
	.mipi_align	= 32,

	/* Polarity */
	.inv_pclk	= 0,
	.inv_vsync	= 1,
	.inv_href	= 0,
	.inv_hsync	= 0,
	.reset_camera	= 0,
	.initialized	= 0,
};
#endif /* #ifdef CONFIG_VIDEO_M5MO */

#ifdef CONFIG_VIDEO_M9MO
static int m9mo_get_i2c_busnum(void)
{
	return 0;
}

static int m9mo_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);

	ret = gpio_request(GPIO_ISP_CORE_EN, "GPM0");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_ISP_CORE_EN)\n");
		return ret;
	}

	ret = gpio_request(GPIO_ISP_RESET, "GPF1");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_ISP_RESET)\n");
		return ret;
	}
	/* CAM_ISP_CORE_EN */
	ret = gpio_direction_output(GPIO_ISP_CORE_EN, 1);
	CAM_CHECK_ERR_RET(ret, "output GPIO_ISP_CORE_EN");

	/* CAM_ISP_1.2V (ISP 1.2V) => BUCK 9*/
	regulator = regulator_get(NULL, "cam_isp_1.2v");
	if (IS_ERR(regulator)) {
		CAM_CHECK_ERR_RET(ret, "output Err cam_isp_1.2v");
		return -ENODEV;
	}
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp_1.2v");

	/* CAM_SENSOR_CORE_1.2V (CIS 1.2V) => LDO17*/
	regulator = regulator_get(NULL, "cam_sensor_core_1.2v");
	if (IS_ERR(regulator)) {
		CAM_CHECK_ERR_RET(ret, "output Err cam_sensor_core_1.2v");
		return -ENODEV;
	}
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_sensor_core_1.2v");

	/* CAM_ISP_1.8V (ISP 1.8V) => LDO23*/
	regulator = regulator_get(NULL, "cam_isp_1.8v");
	if (IS_ERR(regulator)) {
		CAM_CHECK_ERR_RET(ret, "output Err cam_isp_1.8v");
		return -ENODEV;
	}
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp_1.8v");

	/* CAM_SENSOR_1.8V (CIS 1.8V) => LDO19*/
	regulator = regulator_get(NULL, "cam_sensor_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_sensor_1.8v");

	/* CAM_SENSOR_2.8V (CIS 2.8V) => LDO25*/
	regulator = regulator_get(NULL, "cam_sensor_2.8v");
	if (IS_ERR(regulator)) {
		CAM_CHECK_ERR_RET(ret, "output Err cam_sensor_2.8v");
		return -ENODEV;
	}
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_sensor_2.8v");

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	udelay(70);

	/* ISP_RESET */
	ret = gpio_direction_output(GPIO_ISP_RESET, 1);
	CAM_CHECK_ERR_RET(ret, "output reset");
	mdelay(4);

	gpio_free(GPIO_ISP_CORE_EN);
	gpio_free(GPIO_ISP_RESET);

	return ret;
}

static int m9mo_power_down(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);

	if (system_rev > 0) {
		ret = gpio_request(GPIO_MOT_EN, "GPM0");
		if (ret) {
			printk(KERN_ERR "faile to request gpio(GPIO_MOT_EN)\n");
			return ret;
		}
		ret = gpio_request(GPIO_SAMBAZ_RESET, "GPM0");
		if (ret) {
			printk(KERN_ERR "faile to request gpio(GPIO_SAMBAZ_RESET)\n");
			return ret;
		}
	}
	ret = gpio_request(GPIO_ISP_CORE_EN, "GPM0");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_ISP_CORE_EN)\n");
		return ret;
	}

	ret = gpio_request(GPIO_ISP_RESET, "GPF1");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_ISP_RESET)\n");
		return ret;
	}
	/* s3c_i2c0_force_stop(); */

	mdelay(3);

	/*MOT_3.3*/
	regulator = regulator_get(NULL, "mot_3.3v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "mot_3.3v");

	/*OIS_1.5*/
	regulator = regulator_get(NULL, "ois_1.5v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable ois_1.5v");
	msleep(10);

	/* ISP_RESET */
	ret = gpio_direction_output(GPIO_ISP_RESET, 0);
	CAM_CHECK_ERR(ret, "output reset");
	mdelay(2);

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");
	udelay(20);

	/* CAM_SENSOR_2.8V (CIS 2.8V) => LDO25*/
	regulator = regulator_get(NULL, "cam_sensor_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_sensor_2.8v");

	/* CAM_SENSOR_1.8V (CIS 1.8V) => LDO19*/
	regulator = regulator_get(NULL, "cam_sensor_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_sensor_1.8v");

	/* CAM_ISP_1.8V (ISP 1.8V) => LDO23*/
	regulator = regulator_get(NULL, "cam_isp_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_isp_1.8v");

	/* CAM_SENSOR_CORE_1.2V (CIS 1.2V) => LDO17*/
	regulator = regulator_get(NULL, "cam_sensor_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_sensor_core_1.2v");

	/* CAM_ISP_1.2V (ISP 1.2V) => BUCK 9*/
	regulator = regulator_get(NULL, "cam_isp_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable, cam_isp_1.2v");

	/* CAM_ISP_CORE_1.2V */
	ret = gpio_direction_output(GPIO_ISP_CORE_EN, 0);
	CAM_CHECK_ERR(ret, "output ISP_CORE");

	if (system_rev > 0) {
		ret = gpio_direction_output(GPIO_SAMBAZ_RESET, 0);
		CAM_CHECK_ERR(ret, "output GPIO_SAMBAZ_RESET");
		mdelay(100);

		ret = gpio_direction_output(GPIO_MOT_EN, 0);
		CAM_CHECK_ERR(ret, "output GPIO_MOT_EN");
		mdelay(2);

		gpio_free(GPIO_MOT_EN);
		gpio_free(GPIO_SAMBAZ_RESET);
	}
	gpio_free(GPIO_ISP_CORE_EN);
	gpio_free(GPIO_ISP_RESET);

	return ret;
}

static int m9mo_flash_power(int enable)
{
/* TODO */
	return 0;
}

static int m9mo_power(int enable)
{
	int ret = 0;

	printk(KERN_ERR "%s %s\n", __func__, enable ? "on" : "down");
	if (enable) {
		ret = m9mo_power_on();
		if (unlikely(ret))
			goto error_out;
	} else
		ret = m9mo_power_down();

	ret = s3c_csis_power(enable);
	m9mo_flash_power(enable);

error_out:
	return ret;
}

static int m9mo_config_isp_irq(void)
{
printk(KERN_ERR "m9mo_config_isp_irq~~~~~~~~~~\n");
	s3c_gpio_cfgpin(GPIO_ISP_INT, S3C_GPIO_SFN(0xF));
	s3c_gpio_setpull(GPIO_ISP_INT, S3C_GPIO_PULL_NONE);
	return 0;
}

static int m9mo_config_sambaz(int enable)
{
	struct regulator *regulator;
	int ret = 0;

	if (enable) {
		if (system_rev > 0) {
			ret = gpio_request(GPIO_MOT_EN, "GPM0");
			if (ret) {
				printk(KERN_ERR "faile to request gpio(GPIO_ISP_CORE_EN)\n");
				return ret;
			}
			ret = gpio_request(GPIO_SAMBAZ_RESET, "GPM0");
			if (ret) {
				printk(KERN_ERR "faile to request gpio(GPIO_ISP_RESET)\n");
				return ret;
			}
			ret = gpio_direction_output(GPIO_MOT_EN, 1);
			CAM_CHECK_ERR(ret, "output reset");
			msleep(100);
		}

		regulator = regulator_get(NULL, "mot_3.3v");
		if (IS_ERR(regulator))
			return -ENODEV;
		ret = regulator_enable(regulator);
		regulator_put(regulator);
		CAM_CHECK_ERR_RET(ret, "mot_3.3v");
		mdelay(100);

		regulator = regulator_get(NULL, "ois_1.5v");
		if (IS_ERR(regulator))
			return -ENODEV;
		ret = regulator_enable(regulator);
		regulator_put(regulator);
		CAM_CHECK_ERR_RET(ret, "ois_1.5v");
		mdelay(10);

		if (system_rev > 0) {
			ret = gpio_direction_output(GPIO_SAMBAZ_RESET, 1);
			CAM_CHECK_ERR(ret, "output reset");
			msleep(100);

			gpio_free(GPIO_MOT_EN);
			gpio_free(GPIO_SAMBAZ_RESET);
		}

	} else {
		regulator = regulator_get(NULL, "mot_3.3v");
		if (IS_ERR(regulator))
			return -ENODEV;
		if (regulator_is_enabled(regulator))
			ret = regulator_force_disable(regulator);
		regulator_put(regulator);
		CAM_CHECK_ERR(ret, "mot_3.3v");

		regulator = regulator_get(NULL, "ois_1.5v");
		if (IS_ERR(regulator))
			return -ENODEV;
		if (regulator_is_enabled(regulator))
			ret = regulator_force_disable(regulator);
		regulator_put(regulator);
		CAM_CHECK_ERR(ret, "disable ois_1.5v");
	}
	return ret;
}

static const char *m9mo_get_clk_name(void)
{
	return "sclk_cam0";
}

static struct m9mo_platform_data m9mo_plat = {
	.default_width = 640, /* 1920 */
	.default_height = 480, /* 1080 */
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 1,
	.config_isp_irq = m9mo_config_isp_irq,
	.config_sambaz = m9mo_config_sambaz,
	.irq = IRQ_EINT(2),
};

static struct i2c_board_info  m9mo_i2c_info = {
	I2C_BOARD_INFO("M9MO", 0x1F),
	.platform_data = &m9mo_plat,
};

static struct s3c_platform_camera m9mo = {
	.id		= CAMERA_CSI_C,
	.get_clk_name	= m9mo_get_clk_name,
	.get_i2c_busnum	= m9mo_get_i2c_busnum,
	.cam_power	= m9mo_power, /*smdkv310_mipi_cam0_reset,*/
	.type		= CAM_TYPE_MIPI,
	.fmt		= ITU_601_YCBCR422_8BIT, /*MIPI_CSI_YCBCR422_8BIT*/
	.order422	= CAM_ORDER422_8BIT_CBYCRY,

	.info		= &m9mo_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_UYVY,
	.srclk_name	= "xusbxti", /* "mout_mpll" */
	.clk_rate	= 24000000, /* 48000000 */
	.line_length	= 1920,
	.width		= 640,
	.height		= 480,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 640,
		.height	= 480,
	},

	.mipi_lanes	= 4,
	.mipi_settle	= 12,
	.mipi_align	= 32,

	/* Polarity */
	.inv_pclk	= 0,
	.inv_vsync	= 1,
	.inv_href	= 0,
	.inv_hsync	= 0,
	.reset_camera	= 0,
	.initialized	= 0,
};
#endif /* #ifdef CONFIG_VIDEO_M9MO */

#ifdef CONFIG_VIDEO_ISX012
static int isx012_get_i2c_busnum(void)
{
	return 0;
}

static void isx012_flashtimer_handler(unsigned long data)
{
	int ret = -ENODEV;
	atomic_t *flash_status = (atomic_t *)data;

	pr_info("********** flashtimer_handler **********\n");

	ret = gpio_direction_output(GPIO_CAM_FLASH_EN, 0);
	atomic_set(flash_status, ISX012_FLASH_OFF);
	if (unlikely(ret))
		pr_err("flash_timer: ERROR, failed to oneshot flash off\n");

}

static atomic_t flash_status = ATOMIC_INIT(ISX012_FLASH_OFF);
static int isx012_flash_en(u32 mode, u32 onoff)
{
	static int flash_mode = ISX012_FLASH_MODE_NORMAL;
	static DEFINE_MUTEX(flash_lock);
	static DEFINE_TIMER(flash_timer, isx012_flashtimer_handler,
			0, (unsigned long)&flash_status);
	int ret = 0;

	printk(KERN_DEBUG "flash_en: mode=%d, on=%d\n", mode, onoff);

	if (unlikely((u32)mode >= ISX012_FLASH_MODE_MAX)) {
		pr_err("flash_en: ERROR, invalid flash mode(%d)\n", mode);
		return -EINVAL;
	}

	/* We could not use spin lock because of gpio kernel API.*/
	mutex_lock(&flash_lock);
	if (atomic_read(&flash_status) == onoff) {
		mutex_unlock(&flash_lock);
		pr_warn("flash_en: WARNING, already flash %s\n",
			onoff ? "On" : "Off");
		return 0;
	}

	switch (onoff) {
	case ISX012_FLASH_ON:
		if (mode == ISX012_FLASH_MODE_MOVIE)
			ret = gpio_direction_output(GPIO_CAM_MOVIE_EN, 1);
		else {
			ret = gpio_direction_output(GPIO_CAM_FLASH_EN, 1);
			flash_timer.expires = get_jiffies_64() + HZ / 2;
			add_timer(&flash_timer);
		}
		CAM_CHECK_ERR_GOTO(ret, out,
			"flash_en: ERROR, fail to turn flash on (mode:%d)\n",
			mode);
		flash_mode = mode;
		break;

	case ISX012_FLASH_OFF:
		if (unlikely(flash_mode != mode)) {
			pr_err("flash_en: ERROR, unmatched flash mode(%d, %d)\n",
				flash_mode, mode);
			WARN_ON(1);
			goto out;
		}

		if (mode == ISX012_FLASH_MODE_MOVIE)
			ret = gpio_direction_output(GPIO_CAM_MOVIE_EN, 0);
		else {
			if (del_timer_sync(&flash_timer)) {
				pr_info("flash_en: terminate flash timer...\n");
				ret = gpio_direction_output(GPIO_CAM_FLASH_EN,
							0);
			}
		}
		CAM_CHECK_ERR_GOTO(ret, out,
			"flash_en: ERROR, flash off (mode:%d)\n", mode);
		break;

	default:
		pr_err("flash_en: ERROR, invalid flash cmd(%d)\n", onoff);
		goto out;
		break;
	}

	atomic_set(&flash_status, onoff);

out:
	mutex_unlock(&flash_lock);
	return 0;
}

static int isx012_is_flash_on(void)
{
	return atomic_read(&flash_status);
}

/* Power up/down func for P4C, P2. */
static int isx012_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "[ISX012] power on\n");

#ifndef USE_CAM_GPIO_CFG
	ret = gpio_request(GPIO_5M_nSTBY, "GPJ0");
	if (unlikely(ret)) {
		printk(KERN_ERR "error: request 5M_nSTBY\n");
		return ret;
	}
	ret = gpio_request(GPIO_5M_nRST, "GPL1");
	if (unlikely(ret)) {
		printk(KERN_ERR "error: request 5M_nRST\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_EN2, "GPJ0");
	if (unlikely(ret)) {
		printk(KERN_ERR "error: request CAM_EN2\n");
		return ret;
	}
#endif

	/* 5MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "3mp_core_1.2v");
	udelay(10);

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "cam_io_1.8v");
	udelay(10);

	/* CAM_A2.8V */
	ret = gpio_direction_output(GPIO_CAM_EN2, 1);
	CAM_CHECK_ERR_RET(ret, "CAM_A2.8V");
	udelay(200);

	/* CAM_MCLK */
	/*s5p_gpio_set_drvstr(GPIO_CAM_MCLK, S5P_GPIO_DRVSTR_LV2);*/
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	udelay(10);

	/* 3M_nRST */
	ret = gpio_direction_output(GPIO_5M_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "3M_nRST");
	udelay(10);

	/* 5MP_AF_2.8V */
	regulator = regulator_get(NULL, "3mp_af_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "3mp_af_2.8v");
	usleep_range(6000, 6500);

#ifndef USE_CAM_GPIO_CFG
	gpio_free(GPIO_5M_nSTBY);
	gpio_free(GPIO_5M_nRST);
	gpio_free(GPIO_CAM_EN2);
#endif

	return ret;
}

static int isx012_power_down(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "[ISX012] power down\n");

	ret = gpio_request(GPIO_5M_nSTBY, "GPJ0");
	if (unlikely(ret)) {
		printk(KERN_ERR "error: request 3M_nSTBY\n");
		return ret;
	}
	ret = gpio_request(GPIO_5M_nRST, "GPL1");
	if (unlikely(ret)) {
		printk(KERN_ERR "error: request 3M_nRST\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_EN2, "GPJ0");
	if (unlikely(ret)) {
		printk(KERN_ERR "error: request CAM_EN2\n");
		return ret;
	}

	/* 5MP_AF_2.8V */
	regulator = regulator_get(NULL, "3mp_af_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	CAM_CHECK_ERR_RET(ret, "3mp_af_2.8v");
	udelay(10);

	/* 5M_nSTBY */
	ret = gpio_direction_output(GPIO_5M_nSTBY, 0);
	CAM_CHECK_ERR(ret, "5M_nSTBY");
	udelay(10);

	/* 5M_nRST */
	ret = gpio_direction_output(GPIO_5M_nRST, 0);
	CAM_CHECK_ERR(ret, "5M_nRST");
	udelay(50);

	/* CAM_MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");
	udelay(10);

	/* CAM_A2.8V */
	ret = gpio_direction_output(GPIO_CAM_EN2, 0);
	CAM_CHECK_ERR_RET(ret, "CAM_A2.8V");
	udelay(10);

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "cam_io_1.8v");
	udelay(10);

	/* 5MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "3mp_core_1.2v");

	gpio_free(GPIO_5M_nSTBY);
	gpio_free(GPIO_5M_nRST);
	gpio_free(GPIO_CAM_EN2);

	return ret;
}

static int isx012_power(int enable)
{
	int ret = 0;

	if (enable) {
		ret = isx012_power_on();
	} else
		ret = isx012_power_down();

	if (unlikely(ret)) {
		pr_err("%s: power-on/down failed\n", __func__);
		return ret;
	}

	ret = s3c_csis_power(enable);
	if (unlikely(ret)) {
		pr_err("%s: csis power-on failed\n", __func__);
		return ret;
	}

	return ret;
}

static int isx012_enable_standby(bool enable)
{
	int err;

	pr_info("%s: %s\n", __func__, enable ? "on" : "off");

	err = gpio_request(GPIO_5M_nSTBY, "GPJ0");
	if (unlikely(err)) {
		printk(KERN_ERR "error: request 5M_nSTBY\n");
		return err;
	}

	/* GPIO_5M_nSTBY */
	err = gpio_direction_output(GPIO_5M_nSTBY, enable ?
			GPIO_LEVEL_LOW : GPIO_LEVEL_HIGH);
	CAM_CHECK_ERR_RET(err, "GPIO_5M_nSTBY");

	gpio_free(GPIO_5M_nSTBY);
	return 0;
}

static int px_cam_cfg_init(void)
{
	int ret = -ENODEV;

	pr_info("cam_cfg_init\n");

	ret = gpio_request(GPIO_CAM_MOVIE_EN, "GPM3");
	if (unlikely(ret)) {
		pr_err("cam_cfg_init: fail to get gpio(MOVIE_EN), "
			"err=%d\n", ret);
		goto out;
	}

	ret = gpio_request(GPIO_CAM_FLASH_EN, "GPM3");
	if (unlikely(ret)) {
		pr_err("cam_cfg_init: fail to get gpio(FLASH_EN), "
			"err=%d\n", ret);
		goto out_free;
	}

	return 0;

out_free:
	gpio_free(GPIO_CAM_MOVIE_EN);
out:
	return ret;
}

static const char *isx012_get_clk_name(void)
{
	return "sclk_cam0";
}

static struct isx012_platform_data isx012_plat = {
	.default_width = 1024,
	.default_height = 768,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 1,
	.streamoff_delay = ISX012_STREAMOFF_DELAY,
	.flash_en = isx012_flash_en,
	.is_flash_on = isx012_is_flash_on,
	.stby_on = isx012_enable_standby,
	.dbg_level = CAMDBG_LEVEL_DEFAULT,
};
#define REAR_CAM_PLAT		(isx012_plat)

static struct i2c_board_info  isx012_i2c_info = {
	I2C_BOARD_INFO("ISX012", 0x7A>>1),
	.platform_data = &isx012_plat,
};

static struct s3c_platform_camera isx012 = {
	.id		= CAMERA_CSI_C,
	.get_clk_name	= isx012_get_clk_name,
	.get_i2c_busnum	= isx012_get_i2c_busnum,
	.cam_power	= isx012_power, /*smdkv310_mipi_cam0_reset,*/
	.type		= CAM_TYPE_MIPI,
	.fmt		= ITU_601_YCBCR422_8BIT, /*MIPI_CSI_YCBCR422_8BIT*/
	.order422	= CAM_ORDER422_8BIT_CBYCRY,
	.info		= &isx012_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_UYVY,
	.srclk_name	= "xusbxti", /* "mout_mpll" */
	.clk_rate	= 24000000, /* 48000000 */
	.line_length	= 640,
	.width		= 640,
	.height		= 480,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 640,
		.height	= 480,
	},

	.mipi_lanes	= 2,
	.mipi_settle	= 12,
	.mipi_align	= 32,

	/* Polarity */
	.inv_pclk	= 0,
	.inv_vsync	= 1,
	.inv_href	= 0,
	.inv_hsync	= 0,
	.reset_camera	= 0,
	.initialized	= 0,
};

static ssize_t isx012_camtype_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	const char cam_type[] = "SONY_ISX012";
	pr_info("%s\n", __func__);
	return sprintf(buf, "%s\n", cam_type);
}
static DEVICE_ATTR(rear_camtype, S_IRUGO, isx012_camtype_show, NULL);

static ssize_t isx012_camfw_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char type[] = "ISX012";
	return sprintf(buf, "%s %s\n", type, type);

}
static DEVICE_ATTR(rear_camfw, S_IRUGO, isx012_camfw_show, NULL);

static ssize_t flash_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%s\n", isx012_is_flash_on() ? "on" : "off");
}

static ssize_t flash_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{

	isx012_flash_en(ISX012_FLASH_MODE_MOVIE, (*buf == '0') ?
		ISX012_FLASH_OFF : ISX012_FLASH_ON);

	return count;
}
static DEVICE_ATTR(rear_flash, 0664, flash_show, flash_store);

int isx012_create_file(struct class *cls)
{
	struct device *dev_rear = NULL;
	int ret = -ENODEV;

	dev_rear = device_create(cls, NULL, 0, NULL, "rear");
	if (IS_ERR(dev_rear)) {
		pr_err("cam_init: failed to create device(rearcam_dev)\n");
		return -ENODEV;
	}

	ret = device_create_file(dev_rear, &dev_attr_rear_camtype);
	if (unlikely(ret < 0))
		pr_err("cam_init: failed to create device file, %s\n",
			dev_attr_rear_camtype.attr.name);

	ret = device_create_file(dev_rear, &dev_attr_rear_camfw);
	if (unlikely(ret < 0))
		pr_err("cam_init: failed to create device file, %s\n",
			dev_attr_rear_camtype.attr.name);

	ret = device_create_file(dev_rear, &dev_attr_rear_flash);
	if (unlikely(ret < 0))
		pr_err("cam_init: failed to create device file, %s\n",
			dev_attr_rear_flash.attr.name);

	return 0;
}
#endif /* CONFIG_VIDEO_ISX012*/

#ifdef CONFIG_VIDEO_S5K5CCGX_COMMON
static int s5k5ccgx_get_i2c_busnum(void)
{
	return 0;
}

/* Power up/down func for P4C, P2. */
static int s5k5ccgx_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s in P4C,P2\n", __func__);

#ifndef USE_CAM_GPIO_CFG
	ret = gpio_request(GPIO_2M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_2M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nRST)\n");
		return ret;
	}
#endif

	/* 2M_nSTBY low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");

	/* 2M_nRST low */
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");

	/* 3MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "3mp_core_1.2v");

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "cam_io_1.8v");

	/* CAM_A2.8V, LDO13 */
	regulator = regulator_get(NULL, "cam_a2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "cam_a2.8v");

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "vt_core_1.8v");
	udelay(20);

	/* 2M_nSTBY High */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	udelay(3);

	/* CAM_MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	msleep(5); /* >=5ms */

	/* 2M_nSTBY Low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	msleep(10); /* >=10ms */

	/* 2M_nRST High */
	ret = gpio_direction_output(GPIO_2M_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");
	msleep(5);

	/* 2M_nSTBY High */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	udelay(2);

	/* 3M_nSTBY */
	ret = gpio_direction_output(GPIO_3M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "3M_nSTBY");
	udelay(16);

	/* 3M_nRST */
	ret = gpio_direction_output(GPIO_3M_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "3M_nRST");
	/* udelay(10); */

	/* 3MP_AF_2.8V */
	regulator = regulator_get(NULL, "3mp_af_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "3mp_af_2.8v");
	msleep(10);

#ifndef USE_CAM_GPIO_CFG
	gpio_free(GPIO_2M_nSTBY);
	gpio_free(GPIO_2M_nRST);
	gpio_free(GPIO_3M_nSTBY);
	gpio_free(GPIO_3M_nRST);
#endif

	return ret;
}

static int s5k5ccgx_power_down(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s in P4C,P2\n", __func__);

#ifndef USE_CAM_GPIO_CFG
	ret = gpio_request(GPIO_2M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_2M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nSTBY)\n");
		return ret;
	}
#endif
	/* 3MP_AF_2.8V */
	regulator = regulator_get(NULL, "3mp_af_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "3mp_af_2.8v");

	/* 3M_nRST Low*/
	ret = gpio_direction_output(GPIO_3M_nRST, 0);
	CAM_CHECK_ERR(ret, "3M_nSTBY");
	udelay(50);

	/* CAM_MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");
	udelay(5);

	/* 3M_nSTBY */
	ret = gpio_direction_output(GPIO_3M_nSTBY, 0);
	CAM_CHECK_ERR(ret, "3M_nSTBY");
	udelay(1);

	/* 2M_nRST Low */
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");

	/* 2M_nSTBY Low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "vt_core_1.8v");

	/* CAM_A2.8V */
	regulator = regulator_get(NULL, "cam_a2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "cam_a2.8v");
	/* udelay(50); */

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "cam_io_1.8v");
	/*udelay(50); */

	/* 3MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "3mp_core_1.2v");

#ifndef USE_CAM_GPIO_CFG
	gpio_free(GPIO_2M_nSTBY);
	gpio_free(GPIO_2M_nRST);
	gpio_free(GPIO_3M_nSTBY);
	gpio_free(GPIO_3M_nRST);
#endif
	return ret;
}

static int s5k5ccgx_power(int enable)
{
	int ret = 0;

	printk(KERN_DEBUG "%s %s\n", __func__, enable ? "on" : "down");
	if (enable) {
#ifdef USE_CAM_GPIO_CFG
		if (cfg_gpio_err) {
			printk(KERN_ERR "%s: ERROR: gpio configuration",
				__func__);
			return cfg_gpio_err;
		}
#endif
		ret = s5k5ccgx_power_on();
	} else
		ret = s5k5ccgx_power_down();

	s3c_csis_power(enable);

	return ret;
}

static void s5k5ccgx_flashtimer_handler(unsigned long data)
{
#if 0 /* dslim */
	int ret = -ENODEV;
	atomic_t *flash_status = (atomic_t *)data;

	pr_info("********** flashtimer_handler **********\n");

	ret = gpio_direction_output(GPIO_CAM_FLASH_EN, 0);
	atomic_set(flash_status, S5K5CCGX_FLASH_OFF);
	if (unlikely(ret))
		pr_err("flash_timer: ERROR, failed to oneshot flash off\n");
#endif
}

static atomic_t flash_status = ATOMIC_INIT(S5K5CCGX_FLASH_OFF);
static int s5k5ccgx_flash_en(u32 mode, u32 onoff)
{
#if 0 /* dslim */

	static int flash_mode = S5K5CCGX_FLASH_MODE_NORMAL;
	static DEFINE_MUTEX(flash_lock);
	static DEFINE_TIMER(flash_timer, s5k5ccgx_flashtimer_handler,
			0, (unsigned long)&flash_status);
	int ret = 0;

	printk(KERN_DEBUG "flash_en: mode=%d, on=%d\n", mode, onoff);

	if (unlikely((u32)mode >= S5K5CCGX_FLASH_MODE_MAX)) {
		pr_err("flash_en: ERROR, invalid flash mode(%d)\n", mode);
		return -EINVAL;
	}

	/* We could not use spin lock because of gpio kernel API.*/
	mutex_lock(&flash_lock);
	if (atomic_read(&flash_status) == onoff) {
		mutex_unlock(&flash_lock);
		pr_warn("flash_en: WARNING, already flash %s\n",
			onoff ? "On" : "Off");
		return 0;
	}

	switch (onoff) {
	case S5K5CCGX_FLASH_ON:
		if (mode == S5K5CCGX_FLASH_MODE_MOVIE)
			ret = gpio_direction_output(GPIO_CAM_MOVIE_EN, 1);
		else {
			ret = gpio_direction_output(GPIO_CAM_FLASH_EN, 1);
			flash_timer.expires = get_jiffies_64() + HZ / 2;
			add_timer(&flash_timer);
		}
		CAM_CHECK_ERR_GOTO(ret, out,
			"flash_en: ERROR, fail to turn flash on (mode:%d)\n",
			mode);
		flash_mode = mode;
		break;

	case S5K5CCGX_FLASH_OFF:
		if (unlikely(flash_mode != mode)) {
			pr_err("flash_en: ERROR, unmatched flash mode(%d, %d)\n",
				flash_mode, mode);
			WARN_ON(1);
			goto out;
		}

		if (mode == S5K5CCGX_FLASH_MODE_MOVIE)
			ret = gpio_direction_output(GPIO_CAM_MOVIE_EN, 0);
		else {
			if (del_timer_sync(&flash_timer)) {
				pr_info("flash_en: terminate flash timer...\n");
				ret = gpio_direction_output(GPIO_CAM_FLASH_EN,
							0);
			}
		}
		CAM_CHECK_ERR_GOTO(ret, out,
			"flash_en: ERROR, flash off (mode:%d)\n", mode);
		break;

	default:
		pr_err("flash_en: ERROR, invalid flash cmd(%d)\n", onoff);
		goto out;
		break;
	}

	atomic_set(&flash_status, onoff);

out:
	mutex_unlock(&flash_lock);
#endif
	return 0;
}

static int s5k5ccgx_is_flash_on(void)
{
	return atomic_read(&flash_status);
}

static int px_cam_cfg_init(void)
{
	int ret = -ENODEV;

	/* pr_info("%s\n", __func__); */
#if 0 /* dslim */
	ret = gpio_request(GPIO_CAM_MOVIE_EN, "GPL0");
	if (unlikely(ret)) {
		pr_err("cam_cfg_init: fail to get gpio(MOVIE_EN), "
			"err=%d\n", ret);
		goto out;
	}

	ret = gpio_request(GPIO_CAM_FLASH_EN, "GPL0");
	if (unlikely(ret)) {
		pr_err("cam_cfg_init: fail to get gpio(FLASH_EN), "
			"err=%d\n", ret);
		goto out_free;
	}

	return 0;

out_free:
	gpio_free(GPIO_CAM_MOVIE_EN);
out:
	return ret;
#else
	return 0;
#endif
}

static const char *s5k5ccgx_get_clk_name(void)
{
	return "sclk_cam0";
}

static struct s5k5ccgx_platform_data s5k5ccgx_plat = {
	.default_width = 1024,
	.default_height = 768,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 1,
	.streamoff_delay = S5K5CCGX_STREAMOFF_DELAY,
	.flash_en = s5k5ccgx_flash_en,
	.is_flash_on = s5k5ccgx_is_flash_on,
	.dbg_level = CAMDBG_LEVEL_DEFAULT,
};
#define REAR_CAM_PLAT		(s5k5ccgx_plat)

static struct i2c_board_info  s5k5ccgx_i2c_info = {
	I2C_BOARD_INFO("S5K5CCGX", 0x78>>1),
	.platform_data = &s5k5ccgx_plat,
};

static struct s3c_platform_camera s5k5ccgx = {
	.id		= CAMERA_CSI_C,
	.get_clk_name	= s5k5ccgx_get_clk_name,
	.get_i2c_busnum	= s5k5ccgx_get_i2c_busnum,
	.cam_power	= s5k5ccgx_power, /*smdkv310_mipi_cam0_reset,*/
	.type		= CAM_TYPE_MIPI,
	.fmt		= ITU_601_YCBCR422_8BIT, /*MIPI_CSI_YCBCR422_8BIT*/
	.order422	= CAM_ORDER422_8BIT_CBYCRY,
	.info		= &s5k5ccgx_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_UYVY,
	.srclk_name	= "xusbxti", /* "mout_mpll" */
	.clk_rate	= 24000000, /* 48000000 */
	.line_length	= 640,
	.width		= 640,
	.height		= 480,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 640,
		.height	= 480,
	},

	.mipi_lanes	= 1,
	.mipi_settle	= 6,
	.mipi_align	= 32,

	/* Polarity */
	.inv_pclk	= 0,
	.inv_vsync	= 1,
	.inv_href	= 0,
	.inv_hsync	= 0,
	.reset_camera	= 0,
	.initialized	= 0,
};
#endif /* #ifdef CONFIG_VIDEO_S5K5CCGX_COMMON */


#ifdef CONFIG_VIDEO_SR200PC20M
static int sr200pc20m_get_i2c_busnum(void)
{
		return 13;
}

static int sr200pc20m_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	ret = gpio_request(GPIO_CAM_VT_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_VT_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_ISP_CORE_EN, "GPM0");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(CAM_SENSOR_CORE)\n");
		return ret;
	}
	ret = gpio_request(GPIO_ISP_RESET, "GPY3");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_ISP_RESET)\n");
		return ret;
	}

	/* CAM_VT_nSTBY low */
	ret = gpio_direction_output(GPIO_CAM_VT_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "output VT_nSTBY");

	/* CAM_VT_nRST	low */
	gpio_direction_output(GPIO_CAM_VT_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "output VT_nRST");

	/* ISP_RESET */
	ret = gpio_direction_output(GPIO_ISP_RESET, 0);
	CAM_CHECK_ERR(ret, "output reset");

	/* CAM_ISP_CORE_1.2V */
	ret = gpio_direction_output(GPIO_ISP_CORE_EN, 1);
	CAM_CHECK_ERR_RET(ret, "output GPIO_ISP_CORE_EN");

	regulator = regulator_get(NULL, "cam_isp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp_core_1.2v");
	/* No delay */

	/* CAM_SENSOR_CORE_1.2V */
	regulator = regulator_get(NULL, "cam_sensor_core_1.2v");
	if (IS_ERR(regulator)) {
		CAM_CHECK_ERR_RET(ret, "output Err cam_sensor_core_1.2v");
		return -ENODEV;
	}
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_sensor_core_1.2v");
	udelay(10);

	/* CAM_SENSOR_A2.8V */
	regulator = regulator_get(NULL, "cam_sensor_a2.8v");
	if (IS_ERR(regulator)) {
		CAM_CHECK_ERR_RET(ret, "output Err cam_sensor_a2.8v");
		return -ENODEV;
	}
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_sensor_a2.8v");
	/* it takes about 100us at least during level transition.*/
	udelay(160); /* 130us -> 160us */

	/* VT_CAM_DVDD_1.8V */
	regulator = regulator_get(NULL, "vt_cam_dvdd_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable vt_cam_dvdd_1.8v");
	udelay(10);

	/* VT_CAM_1.8V */
	regulator = regulator_get(NULL, "vt_cam_1.8v");
	if (IS_ERR(regulator)) {
		CAM_CHECK_ERR_RET(ret, "output Err vt_cam_1.8v");
		return -ENODEV;
	}
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable vt_cam_1.8v");
	udelay(20);

	/* CAM_VT_nSTBY high */
	ret = gpio_direction_output(GPIO_CAM_VT_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "output VT_nSTBY");
	mdelay(2);

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	mdelay(30);

	/* CAM_VT_nRST	high */
	gpio_direction_output(GPIO_CAM_VT_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "output VT_nRST");

	gpio_free(GPIO_CAM_VT_nSTBY);
	gpio_free(GPIO_CAM_VT_nRST);
	gpio_free(GPIO_ISP_CORE_EN);
	gpio_free(GPIO_ISP_RESET);

	return ret;
}

static int sr200pc20m_power_off(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s in\n", __func__);

	ret = gpio_request(GPIO_CAM_VT_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_VT_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_ISP_CORE_EN, "GPM0");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(CAM_SENSOR_CORE)\n");
		return ret;
	}
	ret = gpio_request(GPIO_ISP_RESET, "GPY3");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_ISP_RESET)\n");
		return ret;
	}

	/* ISP_RESET */
	ret = gpio_direction_output(GPIO_ISP_RESET, 0);
	CAM_CHECK_ERR(ret, "output reset");

	/* CAM_VT_nRST low */
	gpio_direction_output(GPIO_CAM_VT_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "output VT_nRST");

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");
	udelay(20);

	/* CAM_VT_nSTBY low */
	ret = gpio_direction_output(GPIO_CAM_VT_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "output VT_nSTBY");
	mdelay(2);

	/* CAM_SENSOR_CORE_1.2V */
	regulator = regulator_get(NULL, "cam_sensor_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_sensor_core_1.2v");
	udelay(5);

	/* CAM_ISP_CORE_1.2V */
	ret = gpio_direction_output(GPIO_ISP_CORE_EN, 0);
	CAM_CHECK_ERR(ret, "output ISP_CORE");

	regulator = regulator_get(NULL, "cam_isp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "disable cam_isp_core_1.2v");

	/* CAM_SENSOR_A2.8V */
	regulator = regulator_get(NULL, "cam_sensor_a2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_sensor_a2.8v");
	udelay(800);

	/* VT_CAM_1.8V */
	regulator = regulator_get(NULL, "vt_cam_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable vt_cam_1.8v");
	udelay(250); /* 10us -> 250us */

	/* VT_CAM_DVDD_1.8V */
	regulator = regulator_get(NULL, "vt_cam_dvdd_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable vt_cam_dvdd_1.8v");
	udelay(300); /*10 -> 300 us */

	gpio_free(GPIO_CAM_VT_nSTBY);
	gpio_free(GPIO_CAM_VT_nRST);
	gpio_free(GPIO_ISP_CORE_EN);
	gpio_free(GPIO_ISP_RESET);

	return ret;
}

static int sr200pc20m_power(int onoff)
{
	int ret = 0;

	printk(KERN_DEBUG "%s(): %s\n", __func__, onoff ? "on" : "down");

	if (onoff) {
		ret = sr200pc20m_power_on();
		if (unlikely(ret))
			goto error_out;
	} else {
		ret = sr200pc20m_power_off();
	}

	ret = s3c_csis_power(onoff);

error_out:
	return ret;
}

static const char *sr200pc20m_get_clk_name(void)
{
	return "sclk_cam0";
}

static struct sr200pc20m_platform_data sr200pc20m_plat = {
	.default_width = 640,
	.default_height = 480,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 1,
	.streamoff_delay = 0,
	.dbg_level = CAMDBG_LEVEL_DEFAULT,
};

static struct i2c_board_info  sr200pc20m_i2c_info = {
	I2C_BOARD_INFO("SR200PC20M", 0x40 >> 1),
	.platform_data = &sr200pc20m_plat,
};

static struct s3c_platform_camera sr200pc20m = {
	.id		= CAMERA_CSI_D,
	.get_clk_name	= sr200pc20m_get_clk_name,
	.type		= CAM_TYPE_MIPI,
	.fmt		= ITU_601_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_YCBYCR,
	.get_i2c_busnum	= sr200pc20m_get_i2c_busnum,
	.info		= &sr200pc20m_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_UYVY,
	.srclk_name	= "xusbxti",
	.clk_rate	= 24000000,
	.line_length	= 640,
	.width		= 640,
	.height		= 480,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 640,
		.height	= 480,
	},

	.mipi_lanes	= 1,
	.mipi_settle = 6,
	.mipi_align	= 32,

	.inv_pclk	= 0,
	.inv_vsync	= 0,
	.inv_href	= 0,
	.inv_hsync	= 0,
	.reset_camera	= 0,
	.initialized	= 0,
	.cam_power	= sr200pc20m_power,
};
#endif /* CONFIG_VIDEO_SR200PC20M */

#ifdef CONFIG_VIDEO_SR200PC20
static int sr200pc20_get_i2c_busnum(void)
{
#ifdef CONFIG_MACH_P4
	pr_info("%s: system_rev=%d\n", __func__);
	if (system_rev >= 2)
		return 0;
	else
#endif
		return 13;
}

static int sr200pc20_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

#ifndef USE_CAM_GPIO_CFG
	ret = gpio_request(GPIO_2M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_2M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nRST)\n");
		return ret;
	}
#endif

	/* 3M_nSTBY low */
	ret = gpio_direction_output(GPIO_3M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "3M_nSTBY");

	/* 3M_nRST low */
	ret = gpio_direction_output(GPIO_3M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "3M_nRST");

	/* 2M_nSTBY low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");

	/* 2M_nRST low */
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");

	/* 3MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "3mp_core_1.2v");
	/* udelay(5); */

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "cam_io_1.8v");
	/*udelay(5); */

	/* CAM_A2.8V */
	regulator = regulator_get(NULL, "cam_a2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "cam_a2.8v");
	/* udelay(5); */

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "vt_core_1.8v");
	udelay(20);

	/* ENB High */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	udelay(3); /* 30 -> 3 */

	/* CAM_MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	msleep(5); /* >= 5ms */

	/* ENB Low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	msleep(10); /* >= 10ms */

	/* 2M_nRST High*/
	ret = gpio_direction_output(GPIO_2M_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");
	/*msleep(7);*/ /* >= 7ms */

#if 0
	/* ENB High */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	msleep(12); /* >= 10ms */

	/* ENB Low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	msleep(12); /* >= 10ms */

	/* 2M_nRST Low*/
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");
	udelay(10); /* >= 16 cycle */

	/* 2M_nRST High */
	ret = gpio_direction_output(GPIO_2M_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");
#endif
	udelay(10); /* >= 16 cycle */

#ifndef USE_CAM_GPIO_CFG
	gpio_free(GPIO_2M_nSTBY);
	gpio_free(GPIO_2M_nRST);
	gpio_free(GPIO_3M_nSTBY);
	gpio_free(GPIO_3M_nRST);
#endif
	return 0;
}

static int sr200pc20_power_off(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s in\n", __func__);

#ifndef USE_CAM_GPIO_CFG
	ret = gpio_request(GPIO_2M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_2M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nRST)\n");
		return ret;
	}
#endif

#if 0
	/* 2M_nRST */
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");
	udelay(100);

	/* 2M_nSTBY */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	udelay(100);
#endif
	/* Sleep command */
	mdelay(1);

	/* 2M_nRST Low*/
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");
	udelay(3);

	/* CAM_MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");
	udelay(10);

	/* ENB High*/
	ret = gpio_direction_output(GPIO_2M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	mdelay(5);

	/* ENB Low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_spnSTBY");
	/* udelay(1); */

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "vt_core_1.8v");
	/* udelay(10); */

	/* CAM_A2.8V */
	regulator = regulator_get(NULL, "cam_a2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "cam_a2.8v");
	/* udelay(10); */

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "cam_io_1.8v");
	/*udelay(10); */

	/* 3MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core_1.2v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);

#ifndef USE_CAM_GPIO_CFG
	gpio_free(GPIO_2M_nSTBY);
	gpio_free(GPIO_2M_nRST);
#endif
	return 0;
}

static int sr200pc20_power(int onoff)
{
	int ret = 0;

	printk(KERN_DEBUG "%s(): %s\n", __func__, onoff ? "on" : "down");

	if (onoff) {
#ifdef USE_CAM_GPIO_CFG
		if (cfg_gpio_err) {
			printk(KERN_ERR "%s: ERROR: gpio configuration",
				__func__);
			return cfg_gpio_err;
		}
#endif
		ret = sr200pc20_power_on();
	} else {
		ret = sr200pc20_power_off();
		/* s3c_i2c0_force_stop();*/ /* DSLIM. Should be implemented */
	}

	return ret;
}

static const char *sr200pc20_get_clk_name(void)
{
	return "sclk_cam0";
}

static struct sr200pc20_platform_data sr200pc20_plat = {
	.default_width = 800,
	.default_height = 600,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.is_mipi = 0,
	.streamoff_delay = 0,
	.dbg_level = CAMDBG_LEVEL_DEFAULT,
};
#define FRONT_CAM_PLAT		(sr200pc20_plat)

static struct i2c_board_info  sr200pc20_i2c_info = {
	I2C_BOARD_INFO("SR200PC20", 0x40 >> 1),
	.platform_data = &sr200pc20_plat,
};

static struct s3c_platform_camera sr200pc20 = {
	.id		= CAMERA_PAR_A,
	.get_clk_name	= sr200pc20_get_clk_name,
	.type		= CAM_TYPE_ITU,
	.fmt		= ITU_601_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_YCBYCR,
	.get_i2c_busnum	= sr200pc20_get_i2c_busnum,
	.info		= &sr200pc20_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_UYVY,
	.srclk_name	= "xusbxti",
	.clk_rate	= 24000000,
	.line_length	= 800,
	.width		= 800,
	.height		= 600,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 800,
		.height	= 600,
	},

	/* Polarity */
#if 1 /*def CONFIG_VIDEO_SR200PC20_P4W */
	.inv_pclk	= 0,
	.inv_vsync	= 1,
#else
	.inv_pclk	= 1,
	.inv_vsync	= 0,
#endif
	.inv_href	= 0,
	.inv_hsync	= 0,
	.reset_camera	= 0,
	.initialized	= 0,
	.cam_power	= sr200pc20_power,
};
#endif /* CONFIG_VIDEO_SR200PC20 */


/* Interface setting */
static struct s3c_platform_fimc fimc_plat = {
	.default_cam = CAMERA_CSI_D,
	.camera = {
#if defined(CONFIG_VIDEO_S5C73M3) || defined(CONFIG_VIDEO_SLP_S5C73M3)
		&s5c73m3,
#endif
#ifdef CONFIG_VIDEO_ISX012
		&isx012,
#endif
#ifdef CONFIG_VIDEO_S5K6A3
		&s5k6a3,
#endif
#if defined(CONFIG_VIDEO_S5K6A3) && defined(CONFIG_S5K6A3_CSI_D)
		&s5k6a3_fd,
#endif
#if defined(CONFIG_VIDEO_M5MO)
		&m5mo,
#endif
#if defined(CONFIG_VIDEO_M9MO)
		&m9mo,
#endif
#if defined(CONFIG_VIDEO_SR200PC20M)
		&sr200pc20m,
#endif
#ifdef CONFIG_VIDEO_S5K5CCGX_COMMON
		&s5k5ccgx,
#endif
#ifdef CONFIG_VIDEO_SR200PC20
		&sr200pc20,
#endif
#ifdef WRITEBACK_ENABLED
		&writeback,
#endif
	},
	.hw_ver		= 0x51,
};

#ifdef CONFIG_VIDEO_EXYNOS_FIMC_LITE
static void __set_flite_camera_config(struct exynos_platform_flite *data,
			u32 active_index, u32 max_cam)
{
	data->active_cam_index = active_index;
	data->num_clients = max_cam;
}

static void __init smdk4x12_set_camera_flite_platdata(void)
{
	int flite0_cam_index = 0;
	int flite1_cam_index = 0;
#ifdef CONFIG_VIDEO_S5K6A3
	exynos_flite1_default_data.cam[flite1_cam_index++] = &s5k6a3;
#endif
#ifdef CONFIG_VIDEO_SR200PC20M
	exynos_flite1_default_data.cam[flite1_cam_index++] = &sr200pc20m;
#endif
	__set_flite_camera_config(&exynos_flite0_default_data, 0, flite0_cam_index);
	__set_flite_camera_config(&exynos_flite1_default_data, 0, flite1_cam_index);
}
#endif /* CONFIG_VIDEO_EXYNOS_FIMC_LITE */
#endif /* CONFIG_VIDEO_FIMC */

#ifdef CONFIG_VIDEO_SAMSUNG_S5P_FIMC
static struct i2c_board_info __initdata test_info = {
	I2C_BOARD_INFO("testinfo", 0x0),
};

static struct s5p_fimc_isp_info isp_info[] = {
	{
		.board_info	= &test_info,
		.bus_type	= FIMC_LCD_WB,
		.i2c_bus_num	= 0,
		.mux_id		= 0, /* A-Port : 0, B-Port : 1 */
		.flags		= FIMC_CLK_INV_VSYNC,
	},
};

static void __init midas_subdev_config(void)
{
	s3c_fimc0_default_data.isp_info[0] = &isp_info[0];
	s3c_fimc0_default_data.isp_info[0]->use_cam = true;
	s3c_fimc0_default_data.isp_info[1] = &isp_info[1];
	s3c_fimc0_default_data.isp_info[1]->use_cam = false;
	s3c_fimc0_default_data.isp_info[2] = &isp_info[1];
	s3c_fimc0_default_data.isp_info[2]->use_cam = false;
	s3c_fimc0_default_data.isp_info[3] = &isp_info[1];
	s3c_fimc0_default_data.isp_info[3]->use_cam = false;
}
#endif	/* CONFIG_VIDEO_SAMSUNG_S5P_FIMC */

void __init midas_camera_init(void)
{
#ifdef CONFIG_VIDEO_FIMC
	s3c_fimc0_set_platdata(&fimc_plat);
	s3c_fimc1_set_platdata(&fimc_plat);
	s3c_fimc2_set_platdata(NULL);
#ifdef CONFIG_DRM_EXYNOS_FIMD_WB
	s3c_fimc3_set_platdata(&fimc_plat);
#else
	s3c_fimc3_set_platdata(NULL);
#endif
#ifdef CONFIG_EXYNOS_DEV_PD
	s3c_device_fimc0.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_fimc1.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_fimc2.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_fimc3.dev.parent = &exynos4_device_pd[PD_CAM].dev;
#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
	secmem.parent = &exynos4_device_pd[PD_CAM].dev;
#endif
#endif
#ifdef CONFIG_VIDEO_FIMC_MIPI
	s3c_csis0_set_platdata(NULL);
	s3c_csis1_set_platdata(NULL);
#ifdef CONFIG_EXYNOS_DEV_PD
	s3c_device_csis0.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_csis1.dev.parent = &exynos4_device_pd[PD_CAM].dev;
#endif
#endif
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_LITE
	smdk4x12_set_camera_flite_platdata();
	s3c_set_platdata(&exynos_flite0_default_data,
		sizeof(exynos_flite0_default_data), &exynos_device_flite0);
	s3c_set_platdata(&exynos_flite1_default_data,
		sizeof(exynos_flite1_default_data), &exynos_device_flite1);
#endif
#endif /* CONFIG_VIDEO_FIMC */

#ifdef CONFIG_VIDEO_SAMSUNG_S5P_FIMC
	midas_subdev_config();

	dev_set_name(&s5p_device_fimc0.dev, "s3c-fimc.0");
	dev_set_name(&s5p_device_fimc1.dev, "s3c-fimc.1");
	dev_set_name(&s5p_device_fimc2.dev, "s3c-fimc.2");
	dev_set_name(&s5p_device_fimc3.dev, "s3c-fimc.3");

	clk_add_alias("fimc", "exynos4210-fimc.0", "fimc",
			&s5p_device_fimc0.dev);
	clk_add_alias("fimc", "exynos4210-fimc.1", "fimc",
			&s5p_device_fimc1.dev);
	clk_add_alias("fimc", "exynos4210-fimc.2", "fimc",
			&s5p_device_fimc2.dev);
	clk_add_alias("fimc", "exynos4210-fimc.3", "fimc",
			&s5p_device_fimc3.dev);
	clk_add_alias("sclk_fimc", "exynos4210-fimc.0", "sclk_fimc",
			&s5p_device_fimc0.dev);
	clk_add_alias("sclk_fimc", "exynos4210-fimc.1", "sclk_fimc",
			&s5p_device_fimc1.dev);
	clk_add_alias("sclk_fimc", "exynos4210-fimc.2", "sclk_fimc",
			&s5p_device_fimc2.dev);
	clk_add_alias("sclk_fimc", "exynos4210-fimc.3", "sclk_fimc",
			&s5p_device_fimc3.dev);

	s3c_fimc_setname(0, "exynos4210-fimc");
	s3c_fimc_setname(1, "exynos4210-fimc");
	s3c_fimc_setname(2, "exynos4210-fimc");
	s3c_fimc_setname(3, "exynos4210-fimc");

	s3c_set_platdata(&s3c_fimc0_default_data,
			 sizeof(s3c_fimc0_default_data), &s5p_device_fimc0);
	s3c_set_platdata(&s3c_fimc1_default_data,
			 sizeof(s3c_fimc1_default_data), &s5p_device_fimc1);
	s3c_set_platdata(&s3c_fimc2_default_data,
			 sizeof(s3c_fimc2_default_data), &s5p_device_fimc2);
	s3c_set_platdata(&s3c_fimc3_default_data,
			 sizeof(s3c_fimc3_default_data), &s5p_device_fimc3);
#ifdef CONFIG_EXYNOS_DEV_PD
	s5p_device_fimc0.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s5p_device_fimc1.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s5p_device_fimc2.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s5p_device_fimc3.dev.parent = &exynos4_device_pd[PD_CAM].dev;
#endif
#endif /* CONFIG_VIDEO_S5P_FIMC */

#if defined(CONFIG_MACH_P4NOTE) && defined(CONFIG_VIDEO_ISX012)
	px_cam_cfg_init();
#endif
}
