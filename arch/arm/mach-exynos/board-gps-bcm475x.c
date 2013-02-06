#include <linux/init.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <mach/gpio.h>
#include <plat/gpio-cfg.h>
#include <mach/board-gps.h>

#if !defined(CONFIG_MACH_M0_GRANDECTC) && !defined(CONFIG_MACH_M0_DUOSCTC)

static struct device *gps_dev;

static ssize_t hwrev_show(struct device *dev, \
struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%x\n", system_rev);
}

static DEVICE_ATTR(hwrev, S_IRUGO, hwrev_show, NULL);

static int __init gps_bcm475x_init(void)
{
	int n_rst_pin = 0;
	int n_rst_nc_pin = 0;
	BUG_ON(!sec_class);
	gps_dev = device_create(sec_class, NULL, 0, NULL, "gps");
	BUG_ON(!gps_dev);

	s3c_gpio_cfgpin(GPIO_GPS_RXD, S3C_GPIO_SFN(GPIO_GPS_RXD_AF));
	s3c_gpio_setpull(GPIO_GPS_RXD, S3C_GPIO_PULL_UP);
	s3c_gpio_cfgpin(GPIO_GPS_TXD, S3C_GPIO_SFN(GPIO_GPS_TXD_AF));
	s3c_gpio_setpull(GPIO_GPS_TXD, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_GPS_CTS, S3C_GPIO_SFN(GPIO_GPS_CTS_AF));
	s3c_gpio_setpull(GPIO_GPS_CTS, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_GPS_RTS, S3C_GPIO_SFN(GPIO_GPS_RTS_AF));
	s3c_gpio_setpull(GPIO_GPS_RTS, S3C_GPIO_PULL_NONE);

#ifdef CONFIG_MACH_P2
	n_rst_pin = system_rev >= 5 ?
		GPIO_GPS_nRST_28V : GPIO_GPS_nRST;
#else
	n_rst_pin = GPIO_GPS_nRST;
#endif

	if (gpio_request(n_rst_pin, "GPS_nRST")) {
		WARN(1, "fail to request gpio (GPS_nRST)\n");
		device_destroy(sec_class, gps_dev->devt);
		return 1;
	}

	if (device_create_file(gps_dev, &dev_attr_hwrev) < 0) {
		pr_err("Failed to create device file(%s)!\n",
		       dev_attr_hwrev.attr.name);
	}

	s3c_gpio_setpull(n_rst_pin, S3C_GPIO_PULL_UP);
	s3c_gpio_cfgpin(n_rst_pin, S3C_GPIO_OUTPUT);
	gpio_direction_output(n_rst_pin, 1);

	if (gpio_request(GPIO_GPS_PWR_EN, "GPS_PWR_EN")) {
		WARN(1, "fail to request gpio (GPS_PWR_EN)\n");
		device_destroy(sec_class, gps_dev->devt);
		return 1;
	}

	s3c_gpio_setpull(GPIO_GPS_PWR_EN, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_GPS_PWR_EN, S3C_GPIO_OUTPUT);
	gpio_direction_output(GPIO_GPS_PWR_EN, 0);

	gpio_export(n_rst_pin, 1);
	gpio_export(GPIO_GPS_PWR_EN, 1);

	gpio_export_link(gps_dev, "GPS_nRST", n_rst_pin);
	gpio_export_link(gps_dev, "GPS_PWR_EN", GPIO_GPS_PWR_EN);

	printk(KERN_DEBUG "%s - system_rev : %x\n", __func__, system_rev);

	return 0;
}

device_initcall(gps_bcm475x_init);

#endif
