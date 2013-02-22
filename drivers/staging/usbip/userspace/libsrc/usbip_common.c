/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include "usbip_common.h"
#include "names.h"

#undef  PROGNAME
#define PROGNAME "libusbip"

int usbip_use_syslog;
int usbip_use_stderr;
int usbip_use_debug;

struct speed_string {
	int num;
	char *speed;
	char *desc;
};

static const struct speed_string speed_strings[] = {
	{ USB_SPEED_UNKNOWN, "unknown", "Unknown Speed"},
	{ USB_SPEED_LOW,  "1.5", "Low Speed(1.5Mbps)"  },
	{ USB_SPEED_FULL, "12",  "Full Speed(12Mbps)" },
	{ USB_SPEED_HIGH, "480", "High Speed(480Mbps)" },
	{ 0, NULL, NULL }
};

struct portst_string {
	int num;
	char *desc;
};

static struct portst_string portst_strings[] = {
	{ SDEV_ST_AVAILABLE,	"Device Available" },
	{ SDEV_ST_USED,		"Device in Use" },
	{ SDEV_ST_ERROR,	"Device Error"},
	{ VDEV_ST_NULL,		"Port Available"},
	{ VDEV_ST_NOTASSIGNED,	"Port Initializing"},
	{ VDEV_ST_USED,		"Port in Use"},
	{ VDEV_ST_ERROR,	"Port Error"},
	{ 0, NULL}
};

const char *usbip_status_string(int32_t status)
{
	for (int i=0; portst_strings[i].desc != NULL; i++)
		if (portst_strings[i].num == status)
			return portst_strings[i].desc;

	return "Unknown Status";
}

const char *usbip_speed_string(int num)
{
	for (int i=0; speed_strings[i].speed != NULL; i++)
		if (speed_strings[i].num == num)
			return speed_strings[i].desc;

	return "Unknown Speed";
}


#define DBG_UDEV_INTEGER(name)\
	dbg("%-20s = %x", to_string(name), (int) udev->name)

#define DBG_UINF_INTEGER(name)\
	dbg("%-20s = %x", to_string(name), (int) uinf->name)

void dump_usb_interface(struct usbip_usb_interface *uinf)
{
	char buff[100];
	usbip_names_get_class(buff, sizeof(buff),
			uinf->bInterfaceClass,
			uinf->bInterfaceSubClass,
			uinf->bInterfaceProtocol);
	dbg("%-20s = %s", "Interface(C/SC/P)", buff);
}

void dump_usb_device(struct usbip_usb_device *udev)
{
	char buff[100];


	dbg("%-20s = %s", "path",  udev->path);
	dbg("%-20s = %s", "busid", udev->busid);

	usbip_names_get_class(buff, sizeof(buff),
			udev->bDeviceClass,
			udev->bDeviceSubClass,
			udev->bDeviceProtocol);
	dbg("%-20s = %s", "Device(C/SC/P)", buff);

	DBG_UDEV_INTEGER(bcdDevice);

	usbip_names_get_product(buff, sizeof(buff),
			udev->idVendor,
			udev->idProduct);
	dbg("%-20s = %s", "Vendor/Product", buff);

	DBG_UDEV_INTEGER(bNumConfigurations);
	DBG_UDEV_INTEGER(bNumInterfaces);

	dbg("%-20s = %s", "speed",
			usbip_speed_string(udev->speed));

	DBG_UDEV_INTEGER(busnum);
	DBG_UDEV_INTEGER(devnum);
}


int read_attr_value(struct sysfs_device *dev, const char *name, const char *format)
{
	char attrpath[SYSFS_PATH_MAX];
	struct sysfs_attribute *attr;
	int num = 0;
	int ret;

	snprintf(attrpath, sizeof(attrpath), "%s/%s", dev->path, name);

	attr = sysfs_open_attribute(attrpath);
	if (!attr) {
		dbg("sysfs_open_attribute failed: %s", attrpath);
		return 0;
	}

	ret = sysfs_read_attribute(attr);
	if (ret < 0) {
		dbg("sysfs_read_attribute failed");
		goto err;
	}

	ret = sscanf(attr->value, format, &num);
	if (ret < 1) {
		dbg("sscanf failed");
		goto err;
	}

err:
	sysfs_close_attribute(attr);

	return num;
}


int read_attr_speed(struct sysfs_device *dev)
{
	char attrpath[SYSFS_PATH_MAX];
	struct sysfs_attribute *attr;
	char speed[100];
	int ret;

	snprintf(attrpath, sizeof(attrpath), "%s/%s", dev->path, "speed");

	attr = sysfs_open_attribute(attrpath);
	if (!attr) {
		dbg("sysfs_open_attribute failed: %s", attrpath);
		return 0;
	}

	ret = sysfs_read_attribute(attr);
	if (ret < 0) {
		dbg("sysfs_read_attribute failed");
		goto err;
	}

	ret = sscanf(attr->value, "%s\n", speed);
	if (ret < 1) {
		dbg("sscanf failed");
		goto err;
	}
err:
	sysfs_close_attribute(attr);

	for (int i=0; speed_strings[i].speed != NULL; i++) {
		if (!strcmp(speed, speed_strings[i].speed))
			return speed_strings[i].num;
	}

	return USB_SPEED_UNKNOWN;
}

#define READ_ATTR(object, type, dev, name, format)\
	do { (object)->name = (type) read_attr_value(dev, to_string(name), format); } while (0)


int read_usb_device(struct sysfs_device *sdev, struct usbip_usb_device *udev)
{
	uint32_t busnum, devnum;

	READ_ATTR(udev, uint8_t,  sdev, bDeviceClass,		"%02x\n");
	READ_ATTR(udev, uint8_t,  sdev, bDeviceSubClass,	"%02x\n");
	READ_ATTR(udev, uint8_t,  sdev, bDeviceProtocol,	"%02x\n");

	READ_ATTR(udev, uint16_t, sdev, idVendor,		"%04x\n");
	READ_ATTR(udev, uint16_t, sdev, idProduct,		"%04x\n");
	READ_ATTR(udev, uint16_t, sdev, bcdDevice,		"%04x\n");

	READ_ATTR(udev, uint8_t,  sdev, bConfigurationValue,	"%02x\n");
	READ_ATTR(udev, uint8_t,  sdev, bNumConfigurations,	"%02x\n");
	READ_ATTR(udev, uint8_t,  sdev, bNumInterfaces,		"%02x\n");

	READ_ATTR(udev, uint8_t,  sdev, devnum,			"%d\n");
	udev->speed = read_attr_speed(sdev);

	strncpy(udev->path,  sdev->path,  SYSFS_PATH_MAX);
	strncpy(udev->busid, sdev->name, SYSFS_BUS_ID_SIZE);

	sscanf(sdev->name, "%u-%u", &busnum, &devnum);
	udev->busnum = busnum;

	return 0;
}

int read_usb_interface(struct usbip_usb_device *udev, int i,
		       struct usbip_usb_interface *uinf)
{
	char busid[SYSFS_BUS_ID_SIZE];
	struct sysfs_device *sif;

	sprintf(busid, "%s:%d.%d", udev->busid, udev->bConfigurationValue, i);

	sif = sysfs_open_device("usb", busid);
	if (!sif) {
		dbg("sysfs_open_device(\"usb\", \"%s\") failed", busid);
		return -1;
	}

	READ_ATTR(uinf, uint8_t,  sif, bInterfaceClass,		"%02x\n");
	READ_ATTR(uinf, uint8_t,  sif, bInterfaceSubClass,	"%02x\n");
	READ_ATTR(uinf, uint8_t,  sif, bInterfaceProtocol,	"%02x\n");

	sysfs_close_device(sif);

	return 0;
}

int usbip_names_init(char *f)
{
	return names_init(f);
}

void usbip_names_free()
{
	names_free();
}

void usbip_names_get_product(char *buff, size_t size, uint16_t vendor, uint16_t product)
{
	const char *prod, *vend;

	prod = names_product(vendor, product);
	if (!prod)
		prod = "unknown product";


	vend = names_vendor(vendor);
	if (!vend)
		vend = "unknown vendor";

	snprintf(buff, size, "%s : %s (%04x:%04x)", vend, prod, vendor, product);
}

void usbip_names_get_class(char *buff, size_t size, uint8_t class, uint8_t subclass, uint8_t protocol)
{
	const char *c, *s, *p;

	if (class == 0 && subclass == 0 && protocol == 0) {
		snprintf(buff, size, "(Defined at Interface level) (%02x/%02x/%02x)", class, subclass, protocol);
		return;
	}

	p = names_protocol(class, subclass, protocol);
	if (!p)
		p = "unknown protocol";

	s = names_subclass(class, subclass);
	if (!s)
		s = "unknown subclass";

	c = names_class(class);
	if (!c)
		c = "unknown class";

	snprintf(buff, size, "%s / %s / %s (%02x/%02x/%02x)", c, s, p, class, subclass, protocol);
}
