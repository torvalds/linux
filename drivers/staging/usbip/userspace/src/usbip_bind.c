/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <sysfs/libsysfs.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>

#include "usbip_common.h"
#include "utils.h"
#include "usbip.h"

enum unbind_status {
	UNBIND_ST_OK,
	UNBIND_ST_USBIP_HOST,
	UNBIND_ST_FAILED
};

static const char usbip_bind_usage_string[] =
	"usbip bind <args>\n"
	"    -b, --busid=<busid>    Bind " USBIP_HOST_DRV_NAME ".ko to device "
	"on <busid>\n";

void usbip_bind_usage(void)
{
	printf("usage: %s", usbip_bind_usage_string);
}

/* call at unbound state */
static int bind_usbip(char *busid)
{
	char bus_type[] = "usb";
	char attr_name[] = "bind";
	char sysfs_mntpath[SYSFS_PATH_MAX];
	char bind_attr_path[SYSFS_PATH_MAX];
	struct sysfs_attribute *bind_attr;
	int failed = 0;
	int rc, ret = -1;

	rc = sysfs_get_mnt_path(sysfs_mntpath, SYSFS_PATH_MAX);
	if (rc < 0) {
		err("sysfs must be mounted: %s", strerror(errno));
		return -1;
	}

	snprintf(bind_attr_path, sizeof(bind_attr_path), "%s/%s/%s/%s/%s/%s",
		 sysfs_mntpath, SYSFS_BUS_NAME, bus_type, SYSFS_DRIVERS_NAME,
		 USBIP_HOST_DRV_NAME, attr_name);

	bind_attr = sysfs_open_attribute(bind_attr_path);
	if (!bind_attr) {
		dbg("problem getting bind attribute: %s", strerror(errno));
		return -1;
	}

	rc = sysfs_write_attribute(bind_attr, busid, SYSFS_BUS_ID_SIZE);
	if (rc < 0) {
		dbg("bind driver at %s failed", busid);
		failed = 1;
	}

	if (!failed)
		ret = 0;

	sysfs_close_attribute(bind_attr);

	return ret;
}

/* buggy driver may cause dead lock */
static int unbind_other(char *busid)
{
	char bus_type[] = "usb";
	struct sysfs_device *busid_dev;
	struct sysfs_device *dev;
	struct sysfs_driver *drv;
	struct sysfs_attribute *unbind_attr;
	struct sysfs_attribute *bDevClass;
	int rc;
	enum unbind_status status = UNBIND_ST_OK;

	busid_dev = sysfs_open_device(bus_type, busid);
	if (!busid_dev) {
		dbg("sysfs_open_device %s failed: %s", busid, strerror(errno));
		return -1;
	}
	dbg("busid path: %s", busid_dev->path);

	bDevClass  = sysfs_get_device_attr(busid_dev, "bDeviceClass");
	if (!bDevClass) {
		dbg("problem getting device attribute: %s",
		    strerror(errno));
		goto err_close_busid_dev;
	}

	if (!strncmp(bDevClass->value, "09", bDevClass->len)) {
		dbg("skip unbinding of hub");
		goto err_close_busid_dev;
	}

	dev = sysfs_open_device(bus_type, busid);
	if (!dev) {
		dbg("could not open device: %s",
				strerror(errno));
		goto err_close_busid_dev;
	}

	dbg("%s -> %s", dev->name,  dev->driver_name);

	if (!strncmp("unknown", dev->driver_name, SYSFS_NAME_LEN)) {
		/* unbound interface */
		sysfs_close_device(dev);
		goto out;
	}

	if (!strncmp(USBIP_HOST_DRV_NAME, dev->driver_name,
				SYSFS_NAME_LEN)) {
		/* already bound to usbip-host */
		status = UNBIND_ST_USBIP_HOST;
		sysfs_close_device(dev);
		goto out;
	}

	/* unbinding */
	drv = sysfs_open_driver(bus_type, dev->driver_name);
	if (!drv) {
		dbg("could not open device driver on %s: %s",
				dev->name, strerror(errno));
		goto err_close_intf_dev;
	}
	dbg("device driver: %s", drv->path);

	unbind_attr = sysfs_get_driver_attr(drv, "unbind");
	if (!unbind_attr) {
		dbg("problem getting device driver attribute: %s",
				strerror(errno));
		goto err_close_intf_drv;
	}

	rc = sysfs_write_attribute(unbind_attr, dev->bus_id,
			SYSFS_BUS_ID_SIZE);
	if (rc < 0) {
		/* NOTE: why keep unbinding other interfaces? */
		dbg("unbind driver at %s failed", dev->bus_id);
		status = UNBIND_ST_FAILED;
	}

	sysfs_close_driver(drv);
	sysfs_close_device(dev);

	goto out;

err_close_intf_drv:
	sysfs_close_driver(drv);
err_close_intf_dev:
	sysfs_close_device(dev);
err_close_busid_dev:
	status = UNBIND_ST_FAILED;
out:
	sysfs_close_device(busid_dev);

	return status;
}

static int bind_device(char *busid)
{
	int rc;

	rc = unbind_other(busid);
	if (rc == UNBIND_ST_FAILED) {
		err("could not unbind driver from device on busid %s", busid);
		return -1;
	} else if (rc == UNBIND_ST_USBIP_HOST) {
		err("device on busid %s is already bound to %s", busid,
		    USBIP_HOST_DRV_NAME);
		return -1;
	}

	rc = modify_match_busid(busid, 1);
	if (rc < 0) {
		err("unable to bind device on %s", busid);
		return -1;
	}

	rc = bind_usbip(busid);
	if (rc < 0) {
		err("could not bind device to %s", USBIP_HOST_DRV_NAME);
		modify_match_busid(busid, 0);
		return -1;
	}

	printf("bind device on busid %s: complete\n", busid);

	return 0;
}

int usbip_bind(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "busid", required_argument, NULL, 'b' },
		{ NULL,    0,                 NULL,  0  }
	};

	int opt;
	int ret = -1;

	for (;;) {
		opt = getopt_long(argc, argv, "b:", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'b':
			ret = bind_device(optarg);
			goto out;
		default:
			goto err_out;
		}
	}

err_out:
	usbip_bind_usage();
out:
	return ret;
}
