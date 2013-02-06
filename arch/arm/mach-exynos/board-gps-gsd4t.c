#include <linux/init.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <mach/gpio.h>
#include <plat/gpio-cfg.h>
#include <mach/board-gps.h>

static struct device *gps_dev;

static int __init gps_gsd4t_init(void)
{
	BUG_ON(!sec_class);
	gps_dev = device_create(sec_class, NULL, 0, NULL, "gps");
	BUG_ON(!gps_dev);

	s3c_gpio_cfgpin(GPIO_GPS_RXD, S3C_GPIO_SFN(GPIO_GPS_RXD_AF));
	s3c_gpio_setpull(GPIO_GPS_RXD, S3C_GPIO_PULL_UP);
	s3c_gpio_cfgpin(GPIO_GPS_TXD, S3C_GPIO_SFN(GPIO_GPS_TXD_AF));
	s3c_gpio_setpull(GPIO_GPS_TXD, S3C_GPIO_PULL_NONE);

	if (gpio_request(GPIO_GPS_nRST, "GPS_nRST"))
		WARN(1, "fail to request gpio (GPS_nRST)\n");

	s3c_gpio_setpull(GPIO_GPS_nRST, S3C_GPIO_PULL_UP);
	s3c_gpio_cfgpin(GPIO_GPS_nRST, S3C_GPIO_OUTPUT);
	gpio_direction_output(GPIO_GPS_nRST, 1);

#ifdef CONFIG_TARGET_LOCALE_NTT
	if (system_rev >= 11) {
		if (gpio_request(GPIO_GPS_PWR_EN, "GPS_PWR_EN"))
			WARN(1, "fail to request gpio (GPS_PWR_EN)\n");
		s3c_gpio_setpull(GPIO_GPS_PWR_EN, S3C_GPIO_PULL_NONE);
		s3c_gpio_cfgpin(GPIO_GPS_PWR_EN, S3C_GPIO_OUTPUT);
		gpio_direction_output(GPIO_GPS_PWR_EN, 0);

		gpio_export(GPIO_GPS_nRST, 1);
		gpio_export(GPIO_GPS_PWR_EN, 1);
	} else {
		if (gpio_request(GPIO_GPS_PWR_EN_SPI, "GPS_PWR_EN"))
			WARN(1, "fail to request gpio (GPS_PWR_EN)\n");
		s3c_gpio_setpull(GPIO_GPS_PWR_EN_SPI, S3C_GPIO_PULL_NONE);
		s3c_gpio_cfgpin(GPIO_GPS_PWR_EN_SPI, S3C_GPIO_OUTPUT);
		gpio_direction_output(GPIO_GPS_PWR_EN_SPI, 0);

		gpio_export(GPIO_GPS_nRST, 1);
		gpio_export(GPIO_GPS_PWR_EN_SPI, 1);
	}
#else
	if (gpio_request(GPIO_GPS_PWR_EN, "GPS_PWR_EN"))
		WARN(1, "fail to request gpio (GPS_PWR_EN)\n");

	s3c_gpio_setpull(GPIO_GPS_PWR_EN, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_GPS_PWR_EN, S3C_GPIO_OUTPUT);
	gpio_direction_output(GPIO_GPS_PWR_EN, 0);

	gpio_export(GPIO_GPS_nRST, 1);
	gpio_export(GPIO_GPS_PWR_EN, 1);
#endif

#ifdef CONFIG_TARGET_LOCALE_KOR
	if (system_rev >= 7) {
		if (gpio_request(GPIO_GPS_RTS, "GPS_RTS"))
			WARN(1, "fail to request gpio (GPS_RTS)\n");
		s3c_gpio_setpull(GPIO_GPS_RTS, S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(GPIO_GPS_RTS, S3C_GPIO_OUTPUT);
		gpio_direction_output(GPIO_GPS_RTS, 1);

		if (gpio_request(GPIO_GPS_CTS, "GPS_CTS"))
			WARN(1, "fail to request gpio (GPS_RTS)\n");
		s3c_gpio_setpull(GPIO_GPS_CTS, S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(GPIO_GPS_CTS, S3C_GPIO_OUTPUT);
		gpio_direction_output(GPIO_GPS_CTS, 1);
	}
#endif

	gpio_export_link(gps_dev, "GPS_nRST", GPIO_GPS_nRST);
#ifdef CONFIG_TARGET_LOCALE_NTT
	if (system_rev >= 11)
		gpio_export_link(gps_dev, "GPS_PWR_EN", GPIO_GPS_PWR_EN);
	else
		gpio_export_link(gps_dev, "GPS_PWR_EN", GPIO_GPS_PWR_EN_SPI);
#else
	gpio_export_link(gps_dev, "GPS_PWR_EN", GPIO_GPS_PWR_EN);
#endif

	return 0;
}

device_initcall(gps_gsd4t_init);
