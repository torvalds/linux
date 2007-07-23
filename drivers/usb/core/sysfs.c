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


#if defined(CONFIG_USB_PERSIST) || defined(CONFIG_USB_SUSPEND)
static const char power_group[] = "power";
#endif

#ifdef	CONFIG_USB_PERSIST

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

		/* Hubs are automatically enabled for USB_PERSIST */
		if (udev->descriptor.bDeviceClass == USB_CLASS_HUB)
			udev->persist_enabled = 1;
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

#endif	/* CONFIG_USB_PERSIST */

#ifdef	CONFIG_USB_SUSPEND

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
		rc = usb_external_resume_device(udev);

	} else if (len == sizeof auto_string - 1 &&
			strncmp(buf, auto_string, len) == 0) {
		udev->autosuspend_disabled = 0;
		udev->autoresume_disabled = 0;
		rc = usb_external_resume_device(udev);

	} else if (len == sizeof suspend_string - 1 &&
			strncmp(buf, suspend_string, len) == 0) {
		udev->autosuspend_disabled = 0;
		udev->autoresume_disabled = 1;
		rc = usb_external_suspend_device(udev, PMSG_SUSPEND);

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
	}
	return rc;
}

static void remove_power_attributes(struct device *dev)
{
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

static struct attribute *dev_attrs[] = {
	/* current configuration's attributes */
	&dev_attr_configuration.attr,
	&dev_attr_bNumInterfaces.attr,
	&dev_attr_bConfigurationValue.attr,
	&dev_attr_bmAttributes.attr,
	&dev_attr_bMaxPower.attr,
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
	NULL,
};
static struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};

/* Binary descriptors */

static ssize_t
read_descriptors(struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct usb_device *udev = to_usb_device(
			container_of(kobj, struct device, kobj));
	size_t nleft = count;
	size_t srclen, n;

	usb_lock_device(udev);

	/* The binary attribute begins with the device descriptor */
	srclen = sizeof(struct usb_device_descriptor);
	if (off < srclen) {
		n = min_t(size_t, nleft, srclen - off);
		memcpy(buf, off + (char *) &udev->descriptor, n);
		nleft -= n;
		buf += n;
		off = 0;
	} else {
		off -= srclen;
	}

	/* Then follows the raw descriptor entry for the current
	 * configuration (config plus subsidiary descriptors).
	 */
	if (udev->actconfig) {
		int cfgno = udev->actconfig - udev->config;

		srclen = __le16_to_cpu(udev->actconfig->desc.wTotalLength);
		if (off < srclen) {
			n = min_t(size_t, nleft, srclen - off);
			memcpy(buf, off + udev->rawdescriptors[cfgno], n);
			nleft -= n;
		}
	}
	usb_unlock_device(udev);
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

	retval = sysfs_create_group(&dev->kobj, &dev_attr_grp);
	if (retval)
		return retval;

	retval = device_create_bin_file(dev, &dev_bin_attr_descriptors);
	if (retval)
		goto error;

	retval = add_persist_attributes(dev);
	if (retval)
		goto error;

	retval = add_power_attributes(dev);
	if (retval)
		goto error;

	if (udev->manufacturer) {
		retval = device_create_file(dev, &dev_attr_manufacturer);
		if (retval)
			goto error;
	}
	if (udev->product) {
		retval = device_create_file(dev, &dev_attr_product);
		if (retval)
			goto error;
	}
	if (udev->serial) {
		retval = device_create_file(dev, &dev_attr_serial);
		if (retval)
			goto error;
	}
	retval = usb_create_ep_files(dev, &udev->ep0, udev);
	if (retval)
		goto error;
	return 0;
error:
	usb_remove_sysfs_dev_files(udev);
	return retval;
}

void usb_remove_sysfs_dev_files(struct usb_device *udev)
{
	struct device *dev = &udev->dev;

	usb_remove_ep_files(&udev->ep0);
	device_remove_file(dev, &dev_attr_manufacturer);
	device_remove_file(dev, &dev_attr_product);
	device_remove_file(dev, &dev_attr_serial);
	remove_power_attributes(dev);
	remove_persist_attributes(dev);
	device_remove_bin_file(dev, &dev_bin_attr_descriptors);
	sysfs_remove_group(&dev->kobj, &dev_attr_grp);
}

/* Interface Accociation Descriptor fields */
#define usb_intf_assoc_attr(field, format_string)			\
static ssize_t								\
show_iad_##field (struct device *dev, struct device_attribute *attr,	\
		char *buf)						\
{									\
	struct usb_interface *intf = to_usb_interface (dev);		\
									\
	return sprintf (buf, format_string,				\
			intf->intf_assoc->field); 		\
}									\
static DEVICE_ATTR(iad_##field, S_IRUGO, show_iad_##field, NULL);

usb_intf_assoc_attr (bFirstInterface, "%02x\n")
usb_intf_assoc_attr (bInterfaceCount, "%02d\n")
usb_intf_assoc_attr (bFunctionClass, "%02x\n")
usb_intf_assoc_attr (bFunctionSubClass, "%02x\n")
usb_intf_assoc_attr (bFunctionProtocol, "%02x\n")

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
	struct usb_device *udev;
	int len;

	intf = to_usb_interface(dev);
	udev = interface_to_usbdev(intf);
	len = snprintf(buf, 256, "%s", intf->cur_altsetting->string);
	if (len < 0)
		return 0;
	buf[len] = '\n';
	buf[len+1] = 0;
	return len+1;
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

static struct attribute *intf_assoc_attrs[] = {
	&dev_attr_iad_bFirstInterface.attr,
	&dev_attr_iad_bInterfaceCount.attr,
	&dev_attr_iad_bFunctionClass.attr,
	&dev_attr_iad_bFunctionSubClass.attr,
	&dev_attr_iad_bFunctionProtocol.attr,
	NULL,
};
static struct attribute_group intf_assoc_attr_grp = {
	.attrs = intf_assoc_attrs,
};

static struct attribute *intf_attrs[] = {
	&dev_attr_bInterfaceNumber.attr,
	&dev_attr_bAlternateSetting.attr,
	&dev_attr_bNumEndpoints.attr,
	&dev_attr_bInterfaceClass.attr,
	&dev_attr_bInterfaceSubClass.attr,
	&dev_attr_bInterfaceProtocol.attr,
	&dev_attr_modalias.attr,
	NULL,
};
static struct attribute_group intf_attr_grp = {
	.attrs = intf_attrs,
};

static inline void usb_create_intf_ep_files(struct usb_interface *intf,
		struct usb_device *udev)
{
	struct usb_host_interface *iface_desc;
	int i;

	iface_desc = intf->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i)
		usb_create_ep_files(&intf->dev, &iface_desc->endpoint[i],
				udev);
}

static inline void usb_remove_intf_ep_files(struct usb_interface *intf)
{
	struct usb_host_interface *iface_desc;
	int i;

	iface_desc = intf->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i)
		usb_remove_ep_files(&iface_desc->endpoint[i]);
}

int usb_create_sysfs_intf_files(struct usb_interface *intf)
{
	struct device *dev = &intf->dev;
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_host_interface *alt = intf->cur_altsetting;
	int retval;

	retval = sysfs_create_group(&dev->kobj, &intf_attr_grp);
	if (retval)
		return retval;

	if (alt->string == NULL)
		alt->string = usb_cache_string(udev, alt->desc.iInterface);
	if (alt->string)
		retval = device_create_file(dev, &dev_attr_interface);
	if (intf->intf_assoc)
		retval = sysfs_create_group(&dev->kobj, &intf_assoc_attr_grp);
	usb_create_intf_ep_files(intf, udev);
	return 0;
}

void usb_remove_sysfs_intf_files(struct usb_interface *intf)
{
	struct device *dev = &intf->dev;

	usb_remove_intf_ep_files(intf);
	device_remove_file(dev, &dev_attr_interface);
	sysfs_remove_group(&dev->kobj, &intf_attr_grp);
	sysfs_remove_group(&intf->dev.kobj, &intf_assoc_attr_grp);
}
