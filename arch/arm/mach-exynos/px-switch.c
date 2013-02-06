#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/semaphore.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <plat/gpio-cfg.h>
#include <mach/gpio.h>
#include <mach/usb_switch.h>

struct device *sec_switch_dev;

enum usb_path_t current_path = USB_PATH_NONE;

static struct semaphore usb_switch_sem;

static bool usb_connected;

static ssize_t show_usb_sel(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	const char *mode;

	if (current_path & USB_PATH_CP) {
		/* CP */
		mode = "MODEM";
	} else {
		/* AP */
		mode = "PDA";
	}

	pr_info("%s: %s\n", __func__, mode);

	return sprintf(buf, "%s\n", mode);
}

static ssize_t store_usb_sel(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	pr_info("%s: %s\n", __func__, buf);

	if (!strncasecmp(buf, "PDA", 3)) {
		usb_switch_lock();
		usb_switch_clr_path(USB_PATH_CP);
		usb_switch_unlock();
	} else if (!strncasecmp(buf, "MODEM", 5)) {
		usb_switch_lock();
		usb_switch_set_path(USB_PATH_CP);
		usb_switch_unlock();
	} else {
		pr_err("%s: wrong usb_sel value(%s)!!\n", __func__, buf);
		return -EINVAL;
	}

	return count;
}

static ssize_t show_uart_sel(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
#ifdef CONFIG_MACH_P8LTE
	/* 2 for LTE, 1 for AP, 0 for CP */
	int val_sel1, val_sel2;
	val_sel1 = gpio_get_value(GPIO_UART_SEL1);
	val_sel2 = gpio_get_value(GPIO_UART_SEL2);
	return sprintf(buf, "%d", val_sel1 << (1 - val_sel2));
#else
	int val_sel;
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
	int val_sel2;
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */
	const char *mode;

	val_sel = gpio_get_value(GPIO_UART_SEL);
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
	val_sel2 = gpio_get_value(GPIO_UART_SEL2);
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */

	if (val_sel == 0) {
		/* CP */
		mode = "CP";
	} else {
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
		if (val_sel2 == 0) {
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */
			/* AP */
			mode = "AP";
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
		} else {
			/* Keyboard DOCK */
			mode = "DOCK";
		}
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */
	}

	pr_info("%s: %s\n", __func__, mode);

	return sprintf(buf, "%s\n", mode);
#endif /* CONFIG_MACH_P8LTE */
}

static ssize_t store_uart_sel(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int uart_sel = -1;
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
	int uart_sel2 = -1;
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */

#ifdef CONFIG_MACH_P8LTE
	int set_val1, set_val2, ret = 0;
#endif /* CONFIG_MACH_P8LTE */

	pr_info("%s: %s\n", __func__, buf);

#ifdef CONFIG_MACH_P8LTE
	/* 2 for LTE, 1 for AP, 0 for CP */
	ret = sscanf(buf, "%d", &uart_sel);

	if (ret != 1)
		return -EINVAL;

	set_val1 = (uart_sel > 0) ? 1 : 0;
	set_val2 = uart_sel & 0x0001;

	gpio_set_value(GPIO_UART_SEL1, set_val1);
	gpio_set_value(GPIO_UART_SEL2, set_val2);
#else
	uart_sel = gpio_get_value(GPIO_UART_SEL);
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
	uart_sel2 = gpio_get_value(GPIO_UART_SEL2);
#endif
	if (!strncasecmp(buf, "AP", 2)) {
		uart_sel = 1;
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
		uart_sel2 = 0;
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */
	} else if (!strncasecmp(buf, "CP", 2)) {
		uart_sel = 0;
	} else {
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
		if (!strncasecmp(buf, "DOCK", 4)) {
			uart_sel = 1;
			uart_sel2 = 1;
		} else {
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */
		pr_err("%s: wrong uart_sel value(%s)!!\n", __func__, buf);
		return -EINVAL;
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
		}
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */
	}

	/* 1 for AP, 0 for CP */
	gpio_set_value(GPIO_UART_SEL, uart_sel);
	pr_info("%s: uart_sel(%d)\n", __func__, uart_sel);
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
	/* 1 for (AP)DOCK, 0 for (AP)FAC */
	gpio_set_value(GPIO_UART_SEL2, uart_sel2);
	pr_info("%s: uart_sel2(%d)\n", __func__, uart_sel2);
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */
#endif /* CONFIG_MACH_P8LTE */

	return count;
}

static ssize_t show_usb_state(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	const char *state;

	if (usb_connected)
		state = "USB_STATE_CONFIGURED";
	else
		state = "USB_STATE_NOTCONFIGURED";

	pr_info("%s: %s\n", __func__, state);

	return sprintf(buf, "%s\n", state);
}

static DEVICE_ATTR(usb_sel, 0664, show_usb_sel, store_usb_sel);
static DEVICE_ATTR(uart_sel, 0664, show_uart_sel, store_uart_sel);
static DEVICE_ATTR(usb_state, S_IRUGO, show_usb_state, NULL);

static struct attribute *px_switch_attributes[] = {
	&dev_attr_usb_sel.attr,
	&dev_attr_uart_sel.attr,
	&dev_attr_usb_state.attr,
	NULL
};

static const struct attribute_group px_switch_group = {
	.attrs = px_switch_attributes,
};

void set_usb_connection_state(bool connected)
{
	pr_info("%s: set %s\n", __func__, (connected ? "True" : "False"));

	if (usb_connected != connected) {
		usb_connected = connected;

		pr_info("%s: send \"usb_state\" sysfs_notify\n", __func__);
		sysfs_notify(&sec_switch_dev->kobj, NULL, "usb_state");
	}
}

static void pmic_safeout2(int onoff)
{
#if !defined(CONFIG_MACH_P4NOTE)
	struct regulator *regulator;

	regulator = regulator_get(NULL, "safeout2");
	BUG_ON(IS_ERR_OR_NULL(regulator));

	if (onoff) {
		if (!regulator_is_enabled(regulator)) {
			regulator_enable(regulator);
		} else {
			pr_info("%s: onoff:%d No change in safeout2\n",
			       __func__, onoff);
		}
	} else {
		if (regulator_is_enabled(regulator)) {
			regulator_force_disable(regulator);
		} else {
			pr_info("%s: onoff:%d No change in safeout2\n",
			       __func__, onoff);
		}
	}

	regulator_put(regulator);
#else
	if (onoff) {
		if (!gpio_get_value(GPIO_USB_SEL_CP)) {
			gpio_set_value(GPIO_USB_SEL_CP, onoff);
		} else {
			pr_info("%s: onoff:%d No change in safeout2\n",
			       __func__, onoff);
		}
	} else {
		if (gpio_get_value(GPIO_USB_SEL_CP)) {
			gpio_set_value(GPIO_USB_SEL_CP, onoff);
		} else {
			pr_info("%s: onoff:%d No change in safeout2\n",
			       __func__, onoff);
		}
	}
#endif
}

static void usb_apply_path(enum usb_path_t path)
{
#if defined(CONFIG_MACH_P4NOTE)
	pr_info("%s: current gpio before changing : sel0:%d sel1:%d sel_cp:%d\n",
	       __func__, gpio_get_value(GPIO_USB_SEL0),
	       gpio_get_value(GPIO_USB_SEL1), gpio_get_value(GPIO_USB_SEL_CP));
	pr_info("%s: target path %x\n", __func__, path);
#else
	pr_info("%s: current gpio before changing : sel1:%d sel2:%d sel3:%d\n",
	       __func__, gpio_get_value(GPIO_USB_SEL1),
	       gpio_get_value(GPIO_USB_SEL2), gpio_get_value(GPIO_USB_SEL3));
	pr_info("%s: target path %x\n", __func__, path);
#endif

	/* following checks are ordered according to priority */
	if (path & USB_PATH_ADCCHECK) {
#if defined(CONFIG_MACH_P4NOTE)
		gpio_set_value(GPIO_USB_SEL0, 1);
		gpio_set_value(GPIO_USB_SEL1, 0);
#else
		gpio_set_value(GPIO_USB_SEL1, 0);
		gpio_set_value(GPIO_USB_SEL2, 1);
		/* don't care SEL3 */
#if defined(CONFIG_MACH_P8LTE)
		gpio_set_value(GPIO_USB_SEL3, 1);
#endif /* CONFIG_MACH_P8LTE */
#endif /* CONFIG_MACH_P4NOTE */
		goto out_nochange;
	}

#if defined(CONFIG_MACH_P4NOTE)
	if (path & USB_PATH_TA) {
		gpio_set_value(GPIO_USB_SEL0, 0);
		gpio_set_value(GPIO_USB_SEL1, 0);
		goto out_nochange;
	}
#endif /* CONFIG_MACH_P4NOTE */

	if (path & USB_PATH_CP) {
		pr_info("DEBUG: set USB path to CP\n");
#if defined(CONFIG_MACH_P4NOTE)
		gpio_set_value(GPIO_USB_SEL0, 0);
		gpio_set_value(GPIO_USB_SEL1, 1);
#else
		gpio_set_value(GPIO_USB_SEL1, 0);
		gpio_set_value(GPIO_USB_SEL2, 0);
		/* don't care SEL3 */
#if defined(CONFIG_MACH_P8LTE)
		gpio_set_value(GPIO_USB_SEL3, 1);
#endif /* CONFIG_MACH_P8LTE */
#endif /* CONFIG_MACH_P4NOTE */
		mdelay(3);
		goto out_cp;
	}
#if defined(CONFIG_MACH_P4NOTE)
	if (path & USB_PATH_AP) {
		gpio_set_value(GPIO_USB_SEL0, 1);
		gpio_set_value(GPIO_USB_SEL1, 1);
		goto out_ap;
	}
#else
	if (path & USB_PATH_OTG) {
		gpio_set_value(GPIO_USB_SEL1, 1);
		/* don't care SEL2 */
		gpio_set_value(GPIO_USB_SEL3, 1);
		goto out_ap;
	}
	if (path & USB_PATH_HOST) {
#if !defined(CONFIG_MACH_P8LTE)
		gpio_set_value(GPIO_USB_SEL1, 1);
#endif /* !CONFIG_MACH_P8LTE */
		/* don't care SEL2 */
		gpio_set_value(GPIO_USB_SEL3, 0);
		goto out_ap;
	}
#endif /* CONFIG_MACH_P4NOTE */

	/* default */
#if defined(CONFIG_MACH_P4NOTE)
	gpio_set_value(GPIO_USB_SEL0, 1);
	gpio_set_value(GPIO_USB_SEL1, 1);
#else
	gpio_set_value(GPIO_USB_SEL1, 1);
#ifdef CONFIG_MACH_P8LTE
	gpio_set_value(GPIO_USB_SEL2, 1);
#else
	gpio_set_value(GPIO_USB_SEL2, 0);
#endif /* CONFIG_MACH_P8LTE */
	gpio_set_value(GPIO_USB_SEL3, 1);
#endif /* CONFIG_MACH_P4NOTE */

out_ap:
	pr_info("%s: %x safeout2 off\n", __func__, path);
	pmic_safeout2(0);
	goto sysfs_noti;

out_cp:
	pr_info("%s: %x safeout2 on\n", __func__, path);
	pmic_safeout2(1);
	goto sysfs_noti;

out_nochange:
	pr_info("%s: %x safeout2 no change\n", __func__, path);
	return;

sysfs_noti:
	pr_info("%s: send \"usb_sel\" sysfs_notify\n", __func__);
	sysfs_notify(&sec_switch_dev->kobj, NULL, "usb_sel");
	return;
}

/*
  Typical usage of usb switch:

  usb_switch_lock();  (alternatively from hard/soft irq context)
  ( or usb_switch_trylock() )
  ...
  usb_switch_set_path(USB_PATH_ADCCHECK);
  ...
  usb_switch_set_path(USB_PATH_TA);
  ...
  usb_switch_unlock(); (this restores previous usb switch settings)
*/
enum usb_path_t usb_switch_get_path(void)
{
	pr_info("%s: current path(%d)\n", __func__, current_path);

	return current_path;
}

void usb_switch_set_path(enum usb_path_t path)
{
	pr_info("%s: %x current_path before changing\n",
		__func__, current_path);

	current_path |= path;
	usb_apply_path(current_path);
}

void usb_switch_clr_path(enum usb_path_t path)
{
	pr_info("%s: %x current_path before changing\n",
		__func__, current_path);

	current_path &= ~path;
	usb_apply_path(current_path);
}

int usb_switch_lock(void)
{
	return down_interruptible(&usb_switch_sem);
}

int usb_switch_trylock(void)
{
	return down_trylock(&usb_switch_sem);
}

void usb_switch_unlock(void)
{
	up(&usb_switch_sem);
}

#ifdef CONFIG_MACH_P4NOTE
static void init_gpio(void)
{
	int uart_sel = -1;
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
	int uart_sel2 = -1;
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */

	s3c_gpio_cfgpin(GPIO_USB_SEL0, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_USB_SEL0, S3C_GPIO_PULL_NONE);

	s3c_gpio_cfgpin(GPIO_USB_SEL1, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_USB_SEL1, S3C_GPIO_PULL_NONE);

	s3c_gpio_cfgpin(GPIO_USB_SEL_CP, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_USB_SEL_CP, S3C_GPIO_PULL_NONE);

	s3c_gpio_cfgpin(GPIO_UART_SEL, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_UART_SEL, S3C_GPIO_PULL_NONE);

#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
	s3c_gpio_cfgpin(GPIO_UART_SEL2, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_UART_SEL2, S3C_GPIO_PULL_NONE);
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */

	uart_sel = gpio_get_value(GPIO_UART_SEL);
	pr_info("%s: uart_sel(%d)\n", __func__, uart_sel);
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
	uart_sel2 = gpio_get_value(GPIO_UART_SEL2);
	pr_info("%s: uart_sel2(%d)\n", __func__, uart_sel2);
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */
}
#endif

#ifdef CONFIG_TARGET_LOCALE_KOR
#include <plat/devs.h>
#include "../../../drivers/usb/gadget/s3c_udc.h"
/* usb access control for SEC DM */
struct device *usb_lock;
static int is_usb_locked;

int px_switch_get_usb_lock_state(void)
{
	return is_usb_locked;
}
EXPORT_SYMBOL(px_switch_get_usb_lock_state);

static ssize_t px_switch_show_usb_lock(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	if (is_usb_locked)
		return snprintf(buf, PAGE_SIZE, "USB_LOCK");
	else
		return snprintf(buf, PAGE_SIZE, "USB_UNLOCK");
}

static ssize_t px_switch_store_usb_lock(struct device *dev,
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
		   px_switch_show_usb_lock, px_switch_store_usb_lock);
#endif

static int __init usb_switch_init(void)
{
	int ret;

/* USB_SEL gpio_request */
#if defined(CONFIG_MACH_P4NOTE)
	gpio_request(GPIO_USB_SEL0, "GPIO_USB_SEL0");
	gpio_request(GPIO_USB_SEL1, "GPIO_USB_SEL1");
	gpio_request(GPIO_USB_SEL_CP, "GPIO_USB_SEL_CP");
#else
	gpio_request(GPIO_USB_SEL1, "GPIO_USB_SEL1");
	gpio_request(GPIO_USB_SEL2, "GPIO_USB_SEL2");
	gpio_request(GPIO_USB_SEL3, "GPIO_USB_SEL3");
#endif /* CONFIG_MACH_P4NOTE */

/* UART_SEL gpio_request */
#ifdef CONFIG_MACH_P8LTE
	gpio_request(GPIO_UART_SEL1, "GPIO_UART_SEL1");
	gpio_request(GPIO_UART_SEL2, "GPIO_UART_SEL2");
#else
	gpio_request(GPIO_UART_SEL, "GPIO_UART_SEL");
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
	gpio_request(GPIO_UART_SEL2, "GPIO_UART_SEL2");
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */
#endif /* CONFIG_MACH_P8LTE */

/* USB_SEL gpio_export */
#if defined(CONFIG_MACH_P4NOTE)
	gpio_export(GPIO_USB_SEL0, 1);
	gpio_export(GPIO_USB_SEL1, 1);
	gpio_export(GPIO_USB_SEL_CP, 1);
#else
	gpio_export(GPIO_USB_SEL1, 1);
	gpio_export(GPIO_USB_SEL2, 1);
	gpio_export(GPIO_USB_SEL3, 1);
#endif /* CONFIG_MACH_P4NOTE */

/* UART_SEL gpio_export */
#ifdef CONFIG_MACH_P8LTE
	gpio_export(GPIO_UART_SEL1, 1);
	gpio_export(GPIO_UART_SEL2, 1);
#else
	gpio_export(GPIO_UART_SEL, 1);
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
	gpio_export(GPIO_UART_SEL2, 1);
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */
#endif /* CONFIG_MACH_P8LTE */

	BUG_ON(!sec_class);
	sec_switch_dev = device_create(sec_class, NULL, 0, NULL, "switch");

	BUG_ON(!sec_switch_dev);

/* USB_SEL gpio_export_link */
#if defined(CONFIG_MACH_P4NOTE)
	gpio_export_link(sec_switch_dev, "GPIO_USB_SEL0", GPIO_USB_SEL0);
	gpio_export_link(sec_switch_dev, "GPIO_USB_SEL1", GPIO_USB_SEL1);
	gpio_export_link(sec_switch_dev, "GPIO_USB_SEL_CP", GPIO_USB_SEL_CP);
#else
	gpio_export_link(sec_switch_dev, "GPIO_USB_SEL1", GPIO_USB_SEL1);
	gpio_export_link(sec_switch_dev, "GPIO_USB_SEL2", GPIO_USB_SEL2);
	gpio_export_link(sec_switch_dev, "GPIO_USB_SEL3", GPIO_USB_SEL3);
#endif /* CONFIG_MACH_P4NOTE */

/* UART_SEL gpio_export_link */
#ifdef CONFIG_MACH_P8LTE
	gpio_export_link(sec_switch_dev, "GPIO_UART_SEL1", GPIO_UART_SEL1);
	gpio_export_link(sec_switch_dev, "GPIO_UART_SEL2", GPIO_UART_SEL2);
#else
	gpio_export_link(sec_switch_dev, "GPIO_UART_SEL", GPIO_UART_SEL);
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
	gpio_export_link(sec_switch_dev, "GPIO_UART_SEL2", GPIO_UART_SEL2);
#endif /* (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2) */
#endif /* CONFIG_MACH_P8LTE */

#ifdef CONFIG_TARGET_LOCALE_KOR
	usb_lock = device_create(sec_class, sec_switch_dev,
			MKDEV(0, 0), NULL, ".usb_lock");

	if (IS_ERR(usb_lock))
		pr_err("Failed to create device (usb_lock)!\n");

	if (device_create_file(usb_lock, &dev_attr_enable) < 0)
		pr_err("Failed to create device file(.usblock/enable)!\n");
#endif

	/*init_MUTEX(&usb_switch_sem);*/
	sema_init(&usb_switch_sem, 1);

#ifdef CONFIG_MACH_P4NOTE
	init_gpio();
#endif

#if !defined(CONFIG_MACH_P4NOTE)
	if (!gpio_get_value(GPIO_USB_SEL1)) {
#else
	if ((!gpio_get_value(GPIO_USB_SEL0)) && (gpio_get_value(GPIO_USB_SEL1))) {
#endif
		usb_switch_lock();
		usb_switch_set_path(USB_PATH_CP);
		usb_switch_unlock();
	}

	/* create sysfs group */
	ret = sysfs_create_group(&sec_switch_dev->kobj, &px_switch_group);
	if (ret) {
		pr_err("failed to create px switch attribute group\n");
		return ret;
	}

	return 0;
}

device_initcall(usb_switch_init);
