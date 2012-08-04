/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include "usbip_common.h"
#include "vhci_driver.h"

#undef  PROGNAME
#define PROGNAME "libusbip"

struct usbip_vhci_driver *vhci_driver;

static struct usbip_imported_device *imported_device_init(struct usbip_imported_device *idev, char *busid)
{
	struct sysfs_device *sudev;

	sudev = sysfs_open_device("usb", busid);
	if (!sudev) {
		dbg("sysfs_open_device failed: %s", busid);
		goto err;
	}
	read_usb_device(sudev, &idev->udev);
	sysfs_close_device(sudev);

	/* add class devices of this imported device */
	struct usbip_class_device *cdev;
	dlist_for_each_data(vhci_driver->cdev_list, cdev,
			    struct usbip_class_device) {
		if (!strncmp(cdev->dev_path, idev->udev.path,
			     strlen(idev->udev.path))) {
			struct usbip_class_device *new_cdev;

			/* alloc and copy because dlist is linked from only one list */
			new_cdev = calloc(1, sizeof(*new_cdev));
			if (!new_cdev)
				goto err;

			memcpy(new_cdev, cdev, sizeof(*new_cdev));
			dlist_unshift(idev->cdev_list, (void*) new_cdev);
		}
	}

	return idev;

err:
	return NULL;
}



static int parse_status(char *value)
{
	int ret = 0;
	char *c;


	for (int i = 0; i < vhci_driver->nports; i++)
		memset(&vhci_driver->idev[i], 0, sizeof(vhci_driver->idev[i]));


	/* skip a header line */
	c = strchr(value, '\n');
	if (!c)
		return -1;
	c++;

	while (*c != '\0') {
		int port, status, speed, devid;
		unsigned long socket;
		char lbusid[SYSFS_BUS_ID_SIZE];

		ret = sscanf(c, "%d %d %d %x %lx %s\n",
				&port, &status, &speed,
				&devid, &socket, lbusid);

		if (ret < 5) {
			dbg("sscanf failed: %d", ret);
			BUG();
		}

		dbg("port %d status %d speed %d devid %x",
				port, status, speed, devid);
		dbg("socket %lx lbusid %s", socket, lbusid);


		/* if a device is connected, look at it */
		{
			struct usbip_imported_device *idev = &vhci_driver->idev[port];

			idev->port	= port;
			idev->status	= status;

			idev->devid	= devid;

			idev->busnum	= (devid >> 16);
			idev->devnum	= (devid & 0x0000ffff);

			idev->cdev_list = dlist_new(sizeof(struct usbip_class_device));
			if (!idev->cdev_list) {
				dbg("dlist_new failed");
				return -1;
			}

			if (idev->status != VDEV_ST_NULL && idev->status != VDEV_ST_NOTASSIGNED) {
				idev = imported_device_init(idev, lbusid);
				if (!idev) {
					dbg("imported_device_init failed");
					return -1;
				}
			}
		}


		/* go to the next line */
		c = strchr(c, '\n');
		if (!c)
			break;
		c++;
	}

	dbg("exit");

	return 0;
}


static int check_usbip_device(struct sysfs_class_device *cdev)
{
	char class_path[SYSFS_PATH_MAX]; /* /sys/class/video4linux/video0/device */
	char dev_path[SYSFS_PATH_MAX];	 /* /sys/devices/platform/vhci_hcd/usb6/6-1:1.1 */
	int ret;
	struct usbip_class_device *usbip_cdev;

	snprintf(class_path, sizeof(class_path), "%s/device", cdev->path);

	ret = sysfs_get_link(class_path, dev_path, sizeof(dev_path));
	if (ret == 0) {
		if (!strncmp(dev_path, vhci_driver->hc_device->path,
			     strlen(vhci_driver->hc_device->path))) {
			/* found usbip device */
			usbip_cdev = calloc(1, sizeof(*usbip_cdev));
			if (!usbip_cdev) {
				dbg("calloc failed");
				return -1;
			}
			dlist_unshift(vhci_driver->cdev_list, usbip_cdev);
			strncpy(usbip_cdev->class_path, class_path,
				sizeof(usbip_cdev->class_path));
			strncpy(usbip_cdev->dev_path, dev_path,
				sizeof(usbip_cdev->dev_path));
			dbg("found: %s %s", class_path, dev_path);
		}
	}

	return 0;
}


static int search_class_for_usbip_device(char *cname)
{
	struct sysfs_class *class;
	struct dlist *cdev_list;
	struct sysfs_class_device *cdev;
	int ret = 0;

	class = sysfs_open_class(cname);
	if (!class) {
		dbg("sysfs_open_class failed");
		return -1;
	}

	dbg("class: %s", class->name);

	cdev_list = sysfs_get_class_devices(class);
	if (!cdev_list)
		/* nothing */
		goto out;

	dlist_for_each_data(cdev_list, cdev, struct sysfs_class_device) {
		dbg("cdev: %s", cdev->name);
		ret = check_usbip_device(cdev);
		if (ret < 0)
			goto out;
	}

out:
	sysfs_close_class(class);

	return ret;
}


static int refresh_class_device_list(void)
{
	int ret;
	struct dlist *cname_list;
	char *cname;
	char sysfs_mntpath[SYSFS_PATH_MAX];
	char class_path[SYSFS_PATH_MAX];

	ret = sysfs_get_mnt_path(sysfs_mntpath, SYSFS_PATH_MAX);
	if (ret < 0) {
		dbg("sysfs_get_mnt_path failed");
		return -1;
	}

	snprintf(class_path, sizeof(class_path), "%s/%s", sysfs_mntpath,
		 SYSFS_CLASS_NAME);

	/* search under /sys/class */
	cname_list = sysfs_open_directory_list(class_path);
	if (!cname_list) {
		dbg("sysfs_open_directory failed");
		return -1;
	}

	dlist_for_each_data(cname_list, cname, char) {
		ret = search_class_for_usbip_device(cname);
		if (ret < 0) {
			sysfs_close_list(cname_list);
			return -1;
		}
	}

	sysfs_close_list(cname_list);

	/* seach under /sys/block */
	ret = search_class_for_usbip_device(SYSFS_BLOCK_NAME);
	if (ret < 0)
		return -1;

	return 0;
}


static int refresh_imported_device_list(void)
{
	struct sysfs_attribute *attr_status;


	attr_status = sysfs_get_device_attr(vhci_driver->hc_device, "status");
	if (!attr_status) {
		dbg("sysfs_get_device_attr(\"status\") failed: %s",
		    vhci_driver->hc_device->name);
		return -1;
	}

	dbg("name: %s  path: %s  len: %d  method: %d  value: %s",
	    attr_status->name, attr_status->path, attr_status->len,
	    attr_status->method, attr_status->value);

	return parse_status(attr_status->value);
}

static int get_nports(void)
{
	char *c;
	int nports = 0;
	struct sysfs_attribute *attr_status;

	attr_status = sysfs_get_device_attr(vhci_driver->hc_device, "status");
	if (!attr_status) {
		dbg("sysfs_get_device_attr(\"status\") failed: %s",
		    vhci_driver->hc_device->name);
		return -1;
	}

	dbg("name: %s  path: %s  len: %d  method: %d  value: %s",
	    attr_status->name, attr_status->path, attr_status->len,
	    attr_status->method, attr_status->value);

	/* skip a header line */
	c = strchr(attr_status->value, '\n');
	if (!c)
		return 0;
	c++;

	while (*c != '\0') {
		/* go to the next line */
		c = strchr(c, '\n');
		if (!c)
			return nports;
		c++;
		nports += 1;
	}

	return nports;
}

static int get_hc_busid(char *sysfs_mntpath, char *hc_busid)
{
        struct sysfs_driver *sdriver;
        char sdriver_path[SYSFS_PATH_MAX];

	struct sysfs_device *hc_dev;
	struct dlist *hc_devs;

	int found = 0;

        snprintf(sdriver_path, SYSFS_PATH_MAX, "%s/%s/%s/%s/%s", sysfs_mntpath,
		 SYSFS_BUS_NAME, USBIP_VHCI_BUS_TYPE, SYSFS_DRIVERS_NAME,
		 USBIP_VHCI_DRV_NAME);

        sdriver = sysfs_open_driver_path(sdriver_path);
        if (!sdriver) {
		dbg("sysfs_open_driver_path failed: %s", sdriver_path);
                dbg("make sure " USBIP_CORE_MOD_NAME ".ko and "
		    USBIP_VHCI_DRV_NAME ".ko are loaded!");
                return -1;
        }

	hc_devs = sysfs_get_driver_devices(sdriver);
	if (!hc_devs) {
		dbg("sysfs_get_driver failed");
		goto err;
	}

	/* assume only one vhci_hcd */
	dlist_for_each_data(hc_devs, hc_dev, struct sysfs_device) {
		strncpy(hc_busid, hc_dev->bus_id, SYSFS_BUS_ID_SIZE);
		found = 1;
	}

err:
	sysfs_close_driver(sdriver);

	if (found)
		return 0;

	dbg("%s not found", hc_busid);
	return -1;
}


/* ---------------------------------------------------------------------- */

int usbip_vhci_driver_open(void)
{
	int ret;
	char hc_busid[SYSFS_BUS_ID_SIZE];

	vhci_driver = (struct usbip_vhci_driver *) calloc(1, sizeof(*vhci_driver));
	if (!vhci_driver) {
		dbg("calloc failed");
		return -1;
	}

	ret = sysfs_get_mnt_path(vhci_driver->sysfs_mntpath, SYSFS_PATH_MAX);
	if (ret < 0) {
		dbg("sysfs_get_mnt_path failed");
		goto err;
	}

	ret = get_hc_busid(vhci_driver->sysfs_mntpath, hc_busid);
	if (ret < 0)
		goto err;

	/* will be freed in usbip_driver_close() */
	vhci_driver->hc_device = sysfs_open_device(USBIP_VHCI_BUS_TYPE,
						   hc_busid);
	if (!vhci_driver->hc_device) {
		dbg("sysfs_open_device failed");
		goto err;
	}

	vhci_driver->nports = get_nports();

	dbg("available ports: %d", vhci_driver->nports);

	vhci_driver->cdev_list = dlist_new(sizeof(struct usbip_class_device));
	if (!vhci_driver->cdev_list)
		goto err;

	if (refresh_class_device_list())
		goto err;

	if (refresh_imported_device_list())
		goto err;


	return 0;


err:
	if (vhci_driver->cdev_list)
		dlist_destroy(vhci_driver->cdev_list);
	if (vhci_driver->hc_device)
		sysfs_close_device(vhci_driver->hc_device);
	if (vhci_driver)
		free(vhci_driver);

	vhci_driver = NULL;
	return -1;
}


void usbip_vhci_driver_close()
{
	if (!vhci_driver)
		return;

	if (vhci_driver->cdev_list)
		dlist_destroy(vhci_driver->cdev_list);

	for (int i = 0; i < vhci_driver->nports; i++) {
		if (vhci_driver->idev[i].cdev_list)
			dlist_destroy(vhci_driver->idev[i].cdev_list);
	}

	if (vhci_driver->hc_device)
		sysfs_close_device(vhci_driver->hc_device);
	free(vhci_driver);

	vhci_driver = NULL;
}


int usbip_vhci_refresh_device_list(void)
{
	if (vhci_driver->cdev_list)
		dlist_destroy(vhci_driver->cdev_list);


	for (int i = 0; i < vhci_driver->nports; i++) {
		if (vhci_driver->idev[i].cdev_list)
			dlist_destroy(vhci_driver->idev[i].cdev_list);
	}

	vhci_driver->cdev_list = dlist_new(sizeof(struct usbip_class_device));
	if (!vhci_driver->cdev_list)
		goto err;

	if (refresh_class_device_list())
		goto err;

	if (refresh_imported_device_list())
		goto err;

	return 0;
err:
	if (vhci_driver->cdev_list)
		dlist_destroy(vhci_driver->cdev_list);

	for (int i = 0; i < vhci_driver->nports; i++) {
		if (vhci_driver->idev[i].cdev_list)
			dlist_destroy(vhci_driver->idev[i].cdev_list);
	}

	dbg("failed to refresh device list");
	return -1;
}


int usbip_vhci_get_free_port(void)
{
	for (int i = 0; i < vhci_driver->nports; i++) {
		if (vhci_driver->idev[i].status == VDEV_ST_NULL)
			return i;
	}

	return -1;
}

int usbip_vhci_attach_device2(uint8_t port, int sockfd, uint32_t devid,
		uint32_t speed) {
	struct sysfs_attribute *attr_attach;
	char buff[200]; /* what size should be ? */
	int ret;

	attr_attach = sysfs_get_device_attr(vhci_driver->hc_device, "attach");
	if (!attr_attach) {
		dbg("sysfs_get_device_attr(\"attach\") failed: %s",
		    vhci_driver->hc_device->name);
		return -1;
	}

	snprintf(buff, sizeof(buff), "%u %u %u %u",
			port, sockfd, devid, speed);
	dbg("writing: %s", buff);

	ret = sysfs_write_attribute(attr_attach, buff, strlen(buff));
	if (ret < 0) {
		dbg("sysfs_write_attribute failed");
		return -1;
	}

	dbg("attached port: %d", port);

	return 0;
}

static unsigned long get_devid(uint8_t busnum, uint8_t devnum)
{
	return (busnum << 16) | devnum;
}

/* will be removed */
int usbip_vhci_attach_device(uint8_t port, int sockfd, uint8_t busnum,
		uint8_t devnum, uint32_t speed)
{
	int devid = get_devid(busnum, devnum);

	return usbip_vhci_attach_device2(port, sockfd, devid, speed);
}

int usbip_vhci_detach_device(uint8_t port)
{
	struct sysfs_attribute  *attr_detach;
	char buff[200]; /* what size should be ? */
	int ret;

	attr_detach = sysfs_get_device_attr(vhci_driver->hc_device, "detach");
	if (!attr_detach) {
		dbg("sysfs_get_device_attr(\"detach\") failed: %s",
		    vhci_driver->hc_device->name);
		return -1;
	}

	snprintf(buff, sizeof(buff), "%u", port);
	dbg("writing: %s", buff);

	ret = sysfs_write_attribute(attr_detach, buff, strlen(buff));
	if (ret < 0) {
		dbg("sysfs_write_attribute failed");
		return -1;
	}

	dbg("detached port: %d", port);

	return 0;
}
