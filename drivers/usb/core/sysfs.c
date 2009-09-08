/*
 * drivers/usb/core/sysfs.c
 *
 * (C) Copyright 2002 David Brownell
 * (C) Copyright 2002,2004 Greg Kroah-Hartman
 * (C) Copyright 2002,2004 IBM Corp.
 *
 * All of the sysfs file attributes for usb devices and interfaces.
 *
 */


#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <linux/usb/quirks.h>
#include "usb.h"

/* Active configuration fields */
#define usb_actconfig_show(field, multiplier, format_string)		\
static ssize_t  show_##field(struct device *dev,			\
		struct device_attribute *attr, char *buf)		\
{									\
	struct usb_device *udev;					\
	struct usb_host_config *actconfig;				\
									\
	udev = to_usb_device(dev);					\
	actconfig = udev->actconfig;					\
	if (actconfig)							\
		return sprintf(buf, format_string,			\
				actconfig->desc.field * multiplier);	\
	else								\
		return 0;						\
}									\

#define usb_actconfig_attr(field, multiplier, format_string)		\
usb_actconfig_show(field, multiplier, format_string)			\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL);

usb_actconfig_attr(bNumInterfaces, 1, "%2d\n")
usb_actconfig_attr(bmAttributes, 1, "%2x\n")
usb_actconfig_attr(bMaxPower, 2, "%3dmA\n")

static ssize_t show_configuration_string(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct usb_device *udev;
	struct usb_host_config *actconfig;

	udev = to_usb_device(dev);
	actconfig = udev->actconfig;
	if ((!actconfig) || (!actconfig->string))
		return 0;
	return sprintf(buf, "%s\n", actconfig->string);
}
static DEVICE_ATTR(configuration, S_IRUGO, show_configuration_string, NULL);

/* configuration value is always present, and r/w */
usb_actconfig_show(bConfigurationValue, 1, "%u\n");

static ssize_t
set_bConfigurationValue(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct usb_device	*udev = to_usb_device(dev);
	int			config, value;

	if (sscanf(buf, "%d", &config) != 1 || config < -1 || config > 255)
		return -EINVAL;
	usb_lock_device(udev);
	value = usb_set_configuration(udev, config);
	usb_unlock_device(udev);
	return (value < 0) ? value : count;
}

static DEVICE_ATTR(bConfigurationValue, S_IRUGO | S_IWUSR,
		show_bConfigurationValue, set_bConfigurationValue);

/* String fields */
#define usb_string_attr(name)						\
static ssize_t  show_##name(struct device *dev,				\
		struct device_attribute *attr, char *buf)		\
{									\
	struct usb_device *udev;					\
									\
	udev = to_usb_device(dev);					\
	return sprintf(buf, "%s\n", udev->name);			\
}									\
static DEVICE_ATTR(name, S_IRUGO, show_##name, NULL);

usb_string_attr(product);
usb_string_attr(manufacturer);
usb_string_attr(serial);

static ssize_t
show_speed(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_device *udev;
	char *speed;

	udev = to_usb_device(dev);

	switch (udev->speed) {
	case USB_SPEED_LOW:
		speed = "1.5";
		break;
	case USB_SPEED_UNKNOWN:
	case USB_SPEED_FULL:
		speed = "12";
		break;
	case USB_SPEED_HIGH:
		speed = "480";
		break;
	default:
		speed = "unknown";
	}
	return sprintf(buf, "%s\n", speed);
}
static DEVICE_ATTR(speed, S_IRUGO, show_speed, NULL);

static ssize_t
show_busnum(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_device *udev;

	udev = to_usb_device(dev);
	return sprintf(buf, "%d\n", udev->bus->busnum);
}
static DEVICE_ATTR(busnum, S_IRUGO, show_busnum, NULL);

static ssize_t
show_devnum(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_device *udev;

	udev = to_usb_device(dev);
	return sprintf(buf, "%d\n", udev->devnum);
}
static DEVICE_ATTR(devnum, S_IRUGO, show_devnum, NULL);

static ssize_t
show_version(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_device *udev;
	u16 bcdUSB;

	udev = to_usb_device(dev);
	bcdUSB = le16_to_cpu(udev->descriptor.bcdUSB);
	return sprintf(buf, "%2x.%02x\n", bcdUSB >> 8, bcdUSB & 0xff);
}
static DEVICE_ATTR(version, S_IRUGO, show_version, NULL);

static ssize_t
show_maxchild(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_device *udev;

	udev = to_usb_device(dev);
	return sprintf(buf, "%d\n", udev->maxchild);
}
static DEVICE_ATTR(maxchild, S_IRUGO, show_maxchild, NULL);

static ssize_t
show_quirks(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_device *udev;

	udev = to_usb_device(dev);
	return sprintf(buf, "0x%x\n", udev->quirks);
}
static DEVICE_ATTR(quirks, S_IRUGO, show_quirks, NULL);

static ssize_t
show_urbnum(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_device *udev;

	udev = to_usb_device(dev);
	return sprintf(buf, "%d\n", atomic_read(&udev->urbnum));
}
static DEVICE_ATTR(urbnum, S_IRUGO, show_urbnum, NULL);


#ifdef	CONFIG_PM

static const char power_group[] = "power";

static ssize_t
show_persist(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_device *udev = to_usb_device(dev);

	return sprintf(buf, "%d\n", udev->persist_enabled);
}

static ssize_t
set_persist(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct usb_device *udev = to_usb_device(dev);
	int value;

	/* Hubs are always enabled for USB_PERSIST */
	if (udev->descriptor.bDeviceClass == USB_CLASS_HUB)
		return -EPERM;

	if (sscanf(buf, "%d", &value) != 1)
		return -EINVAL;
	usb_pm_lock(udev);
	udev->persist_enabled = !!value;
	usb_pm_unlock(udev);
	return count;
}

static DEVICE_ATTR(persist, S_IRUGO | S_IWUSR, show_persist, set_persist);

static int add_persist_attributes(struct device *dev)
{
	int rc = 0;

	if (is_usb_device(dev)) {
		struct usb_device *udev = to_usb_device(dev);

		/* Hubs are automatically enabled for USB_PERSIST,
		 * no point in creating the attribute file.
		 */
		if (udev->descriptor.bDeviceClass != USB_CLASS_HUB)
			rc = sysfs_add_file_to_group(&dev->kobj,
					&dev_attr_persist.attr,
					power_group);
	}
	return rc;
}

static void remove_persist_attributes(struct device *dev)
{
	sysfs_remove_file_from_group(&dev->kobj,
			&dev_attr_persist.attr,
			power_group);
}
#else

#define add_persist_attributes(dev)	0
#define remove_persist_attributes(dev)	do {} while (0)

#endif	/* CONFIG_PM */

#ifdef	CONFIG_USB_SUSPEND

static ssize_t
show_connected_duration(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct usb_device *udev = to_usb_device(dev);

	return sprintf(buf, "%u\n",
			jiffies_to_msecs(jiffies - udev->connect_time));
}

static DEVICE_ATTR(connected_duration, S_IRUGO, show_connected_duration, NULL);

/*
 * If the device is resumed, the last time the device was suspended has
 * been pre-subtracted from active_duration.  We add the current time to
 * get the duration that the device was actually active.
 *
 * If the device is suspended, the active_duration is up-to-date.
 */
static ssize_t
show_active_duration(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct usb_device *udev = to_usb_device(dev);
	int duration;

	if (udev->state != USB_STATE_SUSPENDED)
		duration = jiffies_to_msecs(jiffies + udev->active_duration);
	else
		duration = jiffies_to_msecs(udev->active_duration);
	return sprintf(buf, "%u\n", duration);
}

static DEVICE_ATTR(active_duration, S_IRUGO, show_active_duration, NULL);

static ssize_t
show_autosuspend(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_device *udev = to_usb_device(dev);

	return sprintf(buf, "%d\n", udev->autosuspend_delay / HZ);
}

static ssize_t
set_autosuspend(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct usb_device *udev = to_usb_device(dev);
	int value;

	if (sscanf(buf, "%d", &value) != 1 || value >= INT_MAX/HZ ||
			value <= - INT_MAX/HZ)
		return -EINVAL;
	value *= HZ;

	udev->autosuspend_delay = value;
	if (value >= 0)
		usb_try_autosuspend_device(udev);
	else {
		if (usb_autoresume_device(udev) == 0)
			usb_autosuspend_device(udev);
	}
	return count;
}

static DEVICE_ATTR(autosuspend, S_IRUGO | S_IWUSR,
		show_autosuspend, set_autosuspend);

static const char on_string[] = "on";
static const char auto_string[] = "auto";
static const char suspend_string[] = "suspend";

static ssize_t
show_level(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_device *udev = to_usb_device(dev);
	const char *p = auto_string;

	if (udev->state == USB_STATE_SUSPENDED) {
		if (udev->autoresume_disabled)
			p = suspend_string;
	} else {
		if (udev->autosuspend_disabled)
			p = on_string;
	}
	return sprintf(buf, "%s\n", p);
}

static ssize_t
set_level(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct usb_device *udev = to_usb_device(dev);
	int len = count;
	char *cp;
	int rc = 0;
	int old_autosuspend_disabled, old_autoresume_disabled;

	cp = memchr(buf, '\n', count);
	if (cp)
		len = cp - buf;

	usb_lock_device(udev);
	old_autosuspend_disabled = udev->autosuspend_disabled;
	old_autoresume_disabled = udev->autoresume_disabled;

	/* Setting the flags without calling usb_pm_lock is a subject to
	 * races, but who cares...
	 */
	if (len == sizeof on_string - 1 &&
			strncmp(buf, on_string, len) == 0) {
		udev->autosuspend_disabled = 1;
		udev->autoresume_disabled = 0;
		rc = usb_external_resume_device(udev, PMSG_USER_RESUME);

	} else if (len == sizeof auto_string - 1 &&
			strncmp(buf, auto_string, len) == 0) {
		udev->autosuspend_disabled = 0;
		udev->autoresume_disabled = 0;
		rc = usb_external_resume_device(udev, PMSG_USER_RESUME);

	} else if (len == sizeof suspend_string - 1 &&
			strncmp(buf, suspend_string, len) == 0) {
		udev->autosuspend_disabled = 0;
		udev->autoresume_disabled = 1;
		rc = usb_external_suspend_device(udev, PMSG_USER_SUSPEND);

	} else
		rc = -EINVAL;

	if (rc) {
		udev->autosuspend_disabled = old_autosuspend_disabled;
		udev->autoresume_disabled = old_autoresume_disabled;
	}
	usb_unlock_device(udev);
	return (rc < 0 ? rc : count);
}

static DEVICE_ATTR(level, S_IRUGO | S_IWUSR, show_level, set_level);

static int add_power_attributes(struct device *dev)
{
	int rc = 0;

	if (is_usb_device(dev)) {
		rc = sysfs_add_file_to_group(&dev->kobj,
				&dev_attr_autosuspend.attr,
				power_group);
		if (rc == 0)
			rc = sysfs_add_file_to_group(&dev->kobj,
					&dev_attr_level.attr,
					power_group);
		if (rc == 0)
			rc = sysfs_add_file_to_group(&dev->kobj,
					&dev_attr_connected_duration.attr,
					power_group);
		if (rc == 0)
			rc = sysfs_add_file_to_group(&dev->kobj,
					&dev_attr_active_duration.attr,
					power_group);
	}
	return rc;
}

static void remove_power_attributes(struct device *dev)
{
	sysfs_remove_file_from_group(&dev->kobj,
			&dev_attr_active_duration.attr,
			power_group);
	sysfs_remove_file_from_group(&dev->kobj,
			&dev_attr_connected_duration.attr,
			power_group);
	sysfs_remove_file_from_group(&dev->kobj,
			&dev_attr_level.attr,
			power_group);
	sysfs_remove_file_from_group(&dev->kobj,
			&dev_attr_autosuspend.attr,
			power_group);
}

#else

#define add_power_attributes(dev)	0
#define remove_power_attributes(dev)	do {} while (0)

#endif	/* CONFIG_USB_SUSPEND */


/* Descriptor fields */
#define usb_descriptor_attr_le16(field, format_string)			\
static ssize_t								\
show_##field(struct device *dev, struct device_attribute *attr,	\
		char *buf)						\
{									\
	struct usb_device *udev;					\
									\
	udev = to_usb_device(dev);					\
	return sprintf(buf, format_string, 				\
			le16_to_cpu(udev->descriptor.field));		\
}									\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL);

usb_descriptor_attr_le16(idVendor, "%04x\n")
usb_descriptor_attr_le16(idProduct, "%04x\n")
usb_descriptor_attr_le16(bcdDevice, "%04x\n")

#define usb_descriptor_attr(field, format_string)			\
static ssize_t								\
show_##field(struct device *dev, struct device_attribute *attr,	\
		char *buf)						\
{									\
	struct usb_device *udev;					\
									\
	udev = to_usb_device(dev);					\
	return sprintf(buf, format_string, udev->descriptor.field);	\
}									\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL);

usb_descriptor_attr(bDeviceClass, "%02x\n")
usb_descriptor_attr(bDeviceSubClass, "%02x\n")
usb_descriptor_attr(bDeviceProtocol, "%02x\n")
usb_descriptor_attr(bNumConfigurations, "%d\n")
usb_descriptor_attr(bMaxPacketSize0, "%d\n")



/* show if the device is authorized (1) or not (0) */
static ssize_t usb_dev_authorized_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct usb_device *usb_dev = to_usb_device(dev);
	return snprintf(buf, PAGE_SIZE, "%u\n", usb_dev->authorized);
}


/*
 * Authorize a device to be used in the system
 *
 * Writing a 0 deauthorizes the device, writing a 1 authorizes it.
 */
static ssize_t usb_dev_authorized_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	ssize_t result;
	struct usb_device *usb_dev = to_usb_device(dev);
	unsigned val;
	result = sscanf(buf, "%u\n", &val);
	if (result != 1)
		result = -EINVAL;
	else if (val == 0)
		result = usb_deauthorize_device(usb_dev);
	else
		result = usb_authorize_device(usb_dev);
	return result < 0? result : size;
}

static DEVICE_ATTR(authorized, 0644,
	    usb_dev_authorized_show, usb_dev_authorized_store);


static struct attribute *dev_attrs[] = {
	/* current configuration's attributes */
	&dev_attr_configuration.attr,
	&dev_attr_bNumInterfaces.attr,
	&dev_attr_bConfigurationValue.attr,
	&dev_attr_bmAttributes.attr,
	&dev_attr_bMaxPower.attr,
	&dev_attr_urbnum.attr,
	/* device attributes */
	&dev_attr_idVendor.attr,
	&dev_attr_idProduct.attr,
	&dev_attr_bcdDevice.attr,
	&dev_attr_bDeviceClass.attr,
	&dev_attr_bDeviceSubClass.attr,
	&dev_attr_bDeviceProtocol.attr,
	&dev_attr_bNumConfigurations.attr,
	&dev_attr_bMaxPacketSize0.attr,
	&dev_attr_speed.attr,
	&dev_attr_busnum.attr,
	&dev_attr_devnum.attr,
	&dev_attr_version.attr,
	&dev_attr_maxchild.attr,
	&dev_attr_quirks.attr,
	&dev_attr_authorized.attr,
	NULL,
};
static struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};

/* When modifying this list, be sure to modify dev_string_attrs_are_visible()
 * accordingly.
 */
static struct attribute *dev_string_attrs[] = {
	&dev_attr_manufacturer.attr,
	&dev_attr_product.attr,
	&dev_attr_serial.attr,
	NULL
};

static mode_t dev_string_attrs_are_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct usb_device *udev = to_usb_device(dev);

	if (a == &dev_attr_manufacturer.attr) {
		if (udev->manufacturer == NULL)
			return 0;
	} else if (a == &dev_attr_product.attr) {
		if (udev->product == NULL)
			return 0;
	} else if (a == &dev_attr_serial.attr) {
		if (udev->serial == NULL)
			return 0;
	}
	return a->mode;
}

static struct attribute_group dev_string_attr_grp = {
	.attrs =	dev_string_attrs,
	.is_visible =	dev_string_attrs_are_visible,
};

struct attribute_group *usb_device_groups[] = {
	&dev_attr_grp,
	&dev_string_attr_grp,
	NULL
};

/* Binary descriptors */

static ssize_t
read_descriptors(struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct usb_device *udev = to_usb_device(dev);
	size_t nleft = count;
	size_t srclen, n;
	int cfgno;
	void *src;

	/* The binary attribute begins with the device descriptor.
	 * Following that are the raw descriptor entries for all the
	 * configurations (config plus subsidiary descriptors).
	 */
	for (cfgno = -1; cfgno < udev->descriptor.bNumConfigurations &&
			nleft > 0; ++cfgno) {
		if (cfgno < 0) {
			src = &udev->descriptor;
			srclen = sizeof(struct usb_device_descriptor);
		} else {
			src = udev->rawdescriptors[cfgno];
			srclen = __le16_to_cpu(udev->config[cfgno].desc.
					wTotalLength);
		}
		if (off < srclen) {
			n = min(nleft, srclen - (size_t) off);
			memcpy(buf, src + off, n);
			nleft -= n;
			buf += n;
			off = 0;
		} else {
			off -= srclen;
		}
	}
	return count - nleft;
}

static struct bin_attribute dev_bin_attr_descriptors = {
	.attr = {.name = "descriptors", .mode = 0444},
	.read = read_descriptors,
	.size = 18 + 65535,	/* dev descr + max-size raw descriptor */
};

int usb_create_sysfs_dev_files(struct usb_device *udev)
{
	struct device *dev = &udev->dev;
	int retval;

	retval = device_create_bin_file(dev, &dev_bin_attr_descriptors);
	if (retval)
		goto error;

	retval = add_persist_attributes(dev);
	if (retval)
		goto error;

	retval = add_power_attributes(dev);
	if (retval)
		goto error;
	return retval;
error:
	usb_remove_sysfs_dev_files(udev);
	return retval;
}

void usb_remove_sysfs_dev_files(struct usb_device *udev)
{
	struct device *dev = &udev->dev;

	remove_power_attributes(dev);
	remove_persist_attributes(dev);
	device_remove_bin_file(dev, &dev_bin_attr_descriptors);
}

/* Interface Accociation Descriptor fields */
#define usb_intf_assoc_attr(field, format_string)			\
static ssize_t								\
show_iad_##field(struct device *dev, struct device_attribute *attr,	\
		char *buf)						\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
									\
	return sprintf(buf, format_string,				\
			intf->intf_assoc->field); 			\
}									\
static DEVICE_ATTR(iad_##field, S_IRUGO, show_iad_##field, NULL);

usb_intf_assoc_attr(bFirstInterface, "%02x\n")
usb_intf_assoc_attr(bInterfaceCount, "%02d\n")
usb_intf_assoc_attr(bFunctionClass, "%02x\n")
usb_intf_assoc_attr(bFunctionSubClass, "%02x\n")
usb_intf_assoc_attr(bFunctionProtocol, "%02x\n")

/* Interface fields */
#define usb_intf_attr(field, format_string)				\
static ssize_t								\
show_##field(struct device *dev, struct device_attribute *attr,	\
		char *buf)						\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
									\
	return sprintf(buf, format_string,				\
			intf->cur_altsetting->desc.field); 		\
}									\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL);

usb_intf_attr(bInterfaceNumber, "%02x\n")
usb_intf_attr(bAlternateSetting, "%2d\n")
usb_intf_attr(bNumEndpoints, "%02x\n")
usb_intf_attr(bInterfaceClass, "%02x\n")
usb_intf_attr(bInterfaceSubClass, "%02x\n")
usb_intf_attr(bInterfaceProtocol, "%02x\n")

static ssize_t show_interface_string(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf;
	char *string;

	intf = to_usb_interface(dev);
	string = intf->cur_altsetting->string;
	barrier();		/* The altsetting might change! */

	if (!string)
		return 0;
	return sprintf(buf, "%s\n", string);
}
static DEVICE_ATTR(interface, S_IRUGO, show_interface_string, NULL);

static ssize_t show_modalias(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf;
	struct usb_device *udev;
	struct usb_host_interface *alt;

	intf = to_usb_interface(dev);
	udev = interface_to_usbdev(intf);
	alt = intf->cur_altsetting;

	return sprintf(buf, "usb:v%04Xp%04Xd%04Xdc%02Xdsc%02Xdp%02X"
			"ic%02Xisc%02Xip%02X\n",
			le16_to_cpu(udev->descriptor.idVendor),
			le16_to_cpu(udev->descriptor.idProduct),
			le16_to_cpu(udev->descriptor.bcdDevice),
			udev->descriptor.bDeviceClass,
			udev->descriptor.bDeviceSubClass,
			udev->descriptor.bDeviceProtocol,
			alt->desc.bInterfaceClass,
			alt->desc.bInterfaceSubClass,
			alt->desc.bInterfaceProtocol);
}
static DEVICE_ATTR(modalias, S_IRUGO, show_modalias, NULL);

static ssize_t show_supports_autosuspend(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf;
	struct usb_device *udev;
	int ret;

	intf = to_usb_interface(dev);
	udev = interface_to_usbdev(intf);

	usb_lock_device(udev);
	/* Devices will be autosuspended even when an interface isn't claimed */
	if (!intf->dev.driver ||
			to_usb_driver(intf->dev.driver)->supports_autosuspend)
		ret = sprintf(buf, "%u\n", 1);
	else
		ret = sprintf(buf, "%u\n", 0);
	usb_unlock_device(udev);

	return ret;
}
static DEVICE_ATTR(supports_autosuspend, S_IRUGO, show_supports_autosuspend, NULL);

static struct attribute *intf_attrs[] = {
	&dev_attr_bInterfaceNumber.attr,
	&dev_attr_bAlternateSetting.attr,
	&dev_attr_bNumEndpoints.attr,
	&dev_attr_bInterfaceClass.attr,
	&dev_attr_bInterfaceSubClass.attr,
	&dev_attr_bInterfaceProtocol.attr,
	&dev_attr_modalias.attr,
	&dev_attr_supports_autosuspend.attr,
	NULL,
};
static struct attribute_group intf_attr_grp = {
	.attrs = intf_attrs,
};

static struct attribute *intf_assoc_attrs[] = {
	&dev_attr_iad_bFirstInterface.attr,
	&dev_attr_iad_bInterfaceCount.attr,
	&dev_attr_iad_bFunctionClass.attr,
	&dev_attr_iad_bFunctionSubClass.attr,
	&dev_attr_iad_bFunctionProtocol.attr,
	NULL,
};

static mode_t intf_assoc_attrs_are_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct usb_interface *intf = to_usb_interface(dev);

	if (intf->intf_assoc == NULL)
		return 0;
	return a->mode;
}

static struct attribute_group intf_assoc_attr_grp = {
	.attrs =	intf_assoc_attrs,
	.is_visible =	intf_assoc_attrs_are_visible,
};

struct attribute_group *usb_interface_groups[] = {
	&intf_attr_grp,
	&intf_assoc_attr_grp,
	NULL
};

int usb_create_sysfs_intf_files(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_host_interface *alt = intf->cur_altsetting;
	int retval;

	if (intf->sysfs_files_created || intf->unregistering)
		return 0;

	if (alt->string == NULL &&
			!(udev->quirks & USB_QUIRK_CONFIG_INTF_STRINGS))
		alt->string = usb_cache_string(udev, alt->desc.iInterface);
	if (alt->string)
		retval = device_create_file(&intf->dev, &dev_attr_interface);
	intf->sysfs_files_created = 1;
	return 0;
}

void usb_remove_sysfs_intf_files(struct usb_interface *intf)
{
	if (!intf->sysfs_files_created)
		return;

	device_remove_file(&intf->dev, &dev_attr_interface);
	intf->sysfs_files_created = 0;
}
