#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/gpio_event.h>
#include <plat/devs.h>
#include <plat/gpio-cfg.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/input.h>
#include <plat/udc-hs.h>
/*#include <linux/mmc/host.h>*/
#include <linux/regulator/machine.h>
#include <linux/regulator/max8649.h>
#include <linux/regulator/fixed.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/mfd/max8997.h>
#include <linux/mfd/max8997-private.h>
/* #include <linux/mfd/max77686.h> */
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#ifdef CONFIG_BATTERY_SAMSUNG
#include <linux/power_supply.h>
#include <linux/battery/samsung_battery.h>
#endif
#include <linux/switch.h>
#include <linux/sii9234.h>

#ifdef CONFIG_USB_HOST_NOTIFY
#include <linux/host_notify.h>
#endif
#include <linux/pm_runtime.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#ifdef CONFIG_JACK_MON
#include <linux/jack.h>
#endif

extern struct class *sec_class;

struct device *switch_dev;
EXPORT_SYMBOL(switch_dev);

#if 0
static ssize_t u1_switch_show_vbus(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	int i;
	struct regulator *regulator;

	regulator = regulator_get(NULL, "safeout1");
	if (IS_ERR(regulator)) {
		pr_warn("%s: fail to get regulator\n", __func__);
		return sprintf(buf, "UNKNOWN\n");
	}
	if (regulator_is_enabled(regulator))
		i = sprintf(buf, "VBUS is enabled\n");
	else
		i = sprintf(buf, "VBUS is disabled\n");
	MUIC_PRINT_LOG();
	regulator_put(regulator);

	return i;
}

static ssize_t u1_switch_store_vbus(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int disable, ret, usb_mode;
	struct regulator *regulator;
	/* struct s3c_udc *udc = platform_get_drvdata(&s3c_device_usbgadget); */

	MUIC_PRINT_LOG();
	if (!strncmp(buf, "0", 1))
		disable = 0;
	else if (!strncmp(buf, "1", 1))
		disable = 1;
	else {
		pr_warn("%s: Wrong command\n", __func__);
		return count;
	}

	pr_info("%s: disable=%d\n", __func__, disable);
	usb_mode =
	    disable ? USB_CABLE_DETACHED_WITHOUT_NOTI : USB_CABLE_ATTACHED;
	/* ret = udc->change_usb_mode(usb_mode); */
	ret = -1;
	if (ret < 0)
		pr_err("%s: fail to change mode!!!\n", __func__);

	regulator = regulator_get(NULL, "safeout1");
	if (IS_ERR(regulator)) {
		pr_warn("%s: fail to get regulator\n", __func__);
		return count;
	}

	if (disable) {
		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);
		if (!regulator_is_enabled(regulator))
			regulator_enable(regulator);
	} else {
		if (!regulator_is_enabled(regulator))
			regulator_enable(regulator);
	}
	regulator_put(regulator);

	return count;
}

DEVICE_ATTR(disable_vbus, 0664, u1_switch_show_vbus,
	    u1_switch_store_vbus);
#endif

#if defined(CONFIG_TARGET_LOCALE_NA)
#define USB_PATH_AP	0
#define USB_PATH_CP	1
#define USB_PATH_ALL	2
static int hub_usb_path;

int u1_get_usb_hub_path(void)
{
	return hub_usb_path;
}
EXPORT_SYMBOL_GPL(u1_get_usb_hub_path);

static ssize_t u1_switch_show_usb_path(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i;

	switch (hub_usb_path) {
	case USB_PATH_AP:
		i = sprintf(buf, "USB_PATH: AP\n");
		break;
	case USB_PATH_CP:
		i = sprintf(buf, "USB_PATH: CP\n");
		break;
	case USB_PATH_ALL:
		i = sprintf(buf, "USB_PATH: ALL\n");
		break;
	default:
		i = sprintf(buf, "USB_PATH: Unknown!\n");
		break;
	}

	return i;
}

static ssize_t u1_switch_store_usb_path(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	if (!strncmp(buf, "AP", 2))
		hub_usb_path = USB_PATH_AP;
	else if (!strncmp(buf, "CP", 2))
		hub_usb_path = USB_PATH_CP;
	else if (!strncmp(buf, "ALL", 3))
		hub_usb_path = USB_PATH_ALL;
	else {
		pr_warn("%s: Wrong command\n", __func__);
		return count;
	}
	pr_info("%s: USB PATH = %d\n", __func__, hub_usb_path);

	return count;
}

static DEVICE_ATTR(set_usb_path, 0664, u1_switch_show_usb_path,
		   u1_switch_store_usb_path);
#endif /* CONFIG_TARGET_LOCALE_NA */

#ifdef CONFIG_TARGET_LOCALE_KOR
#include "../../../drivers/usb/gadget/s3c_udc.h"
/* usb access control for SEC DM */
struct device *usb_lock;
static int is_usb_locked;

int u1_switch_get_usb_lock_state(void)
{
	return is_usb_locked;
}
EXPORT_SYMBOL(u1_switch_get_usb_lock_state);

static ssize_t u1_switch_show_usb_lock(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	if (is_usb_locked)
		return snprintf(buf, PAGE_SIZE, "USB_LOCK");
	else
		return snprintf(buf, PAGE_SIZE, "USB_UNLOCK");
}

static ssize_t u1_switch_store_usb_lock(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int lock;
	struct s3c_udc *udc = platform_get_drvdata(&s3c_device_usbgadget);

	if (!strncmp(buf, "0", 1))
		lock = 0;
	else if (!strncmp(buf, "1", 1))
		lock = 1;
	else {
		pr_warn("%s: Wrong command\n", __func__);
		return count;
	}

	if (IS_ERR_OR_NULL(udc))
		return count;

	pr_info("%s: lock=%d\n", __func__, lock);

	if (lock != is_usb_locked) {
		is_usb_locked = lock;

		if (lock) {
			if (udc->udc_enabled)
				usb_gadget_vbus_disconnect(&udc->gadget);
		}
	}

	return count;
}

static DEVICE_ATTR(enable, 0664,
		   u1_switch_show_usb_lock, u1_switch_store_usb_lock);
#endif /* CONFIG_TARGET_LOCALE_KOR */

static int uart_switch_init(void)
{
	int ret, val;

	ret = gpio_request(GPIO_UART_SEL, "UART_SEL");
	if (ret < 0) {
		pr_err("Failed to request GPIO_UART_SEL!\n");
		return -ENODEV;
	}
	s3c_gpio_setpull(GPIO_UART_SEL, S3C_GPIO_PULL_NONE);
	val = gpio_get_value(GPIO_UART_SEL);
	pr_info("##MUIC [ %s ]- func : %s !! val:-%d-\n", __FILE__, __func__,
		val);
	gpio_direction_output(GPIO_UART_SEL, val);

	gpio_export(GPIO_UART_SEL, 1);

	gpio_export_link(switch_dev, "uart_sel", GPIO_UART_SEL);

	return 0;
}

static int __init u1_sec_switch_init(void)
{
	int ret;
	switch_dev = device_create(sec_class, NULL, 0, NULL, "switch");

	if (IS_ERR(switch_dev))
		pr_err("Failed to create device(switch)!\n");

#if 0
	ret = device_create_file(switch_dev, &dev_attr_disable_vbus);
	if (ret)
		pr_err("Failed to create device file(disable_vbus)!\n");
#endif

#ifdef CONFIG_TARGET_LOCALE_NA
	ret = device_create_file(switch_dev, &dev_attr_set_usb_path);
	if (ret)
		pr_err("Failed to create device file(disable_vbus)!\n");
#endif /* CONFIG_TARGET_LOCALE_NA */

#ifdef CONFIG_TARGET_LOCALE_KOR
	usb_lock = device_create(sec_class, switch_dev,
				MKDEV(0, 0), NULL, ".usb_lock");

	if (IS_ERR(usb_lock))
		pr_err("Failed to create device (usb_lock)!\n");

	if (device_create_file(usb_lock, &dev_attr_enable) < 0)
		pr_err("Failed to create device file(.usblock/enable)!\n");
#endif /* CONFIG_TARGET_LOCALE_KOR */

	ret = uart_switch_init();
	if (ret)
		pr_err("Failed to create uart_switch\n");

	return 0;
}
device_initcall(u1_sec_switch_init);
