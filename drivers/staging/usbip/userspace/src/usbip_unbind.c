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
#include <string.h>

#include <getopt.h>

#include "usbip_common.h"
#include "utils.h"
#include "usbip.h"

static const char usbip_unbind_usage_string[] =
	"usbip unbind <args>\n"
	"    -b, --busid=<busid>    Unbind " USBIP_HOST_DRV_NAME ".ko from "
	"device on <busid>\n";

void usbip_unbind_usage(void)
{
	printf("usage: %s", usbip_unbind_usage_string);
}

static int unbind_device(char *busid)
{
	char bus_type[] = "usb";
	struct sysfs_driver *usbip_host_drv;
	struct sysfs_device *dev;
	struct dlist *devlist;
	int verified = 0;
	int rc, ret = -1;

	char attr_name[] = "bConfigurationValue";
	char sysfs_mntpath[SYSFS_PATH_MAX];
	char busid_attr_path[SYSFS_PATH_MAX];
	struct sysfs_attribute *busid_attr;
	char *val = NULL;
	int len;

	/* verify the busid device is using usbip-host */
	usbip_host_drv = sysfs_open_driver(bus_type, USBIP_HOST_DRV_NAME);
	if (!usbip_host_drv) {
		err("could not open %s driver: %s", USBIP_HOST_DRV_NAME,
		    strerror(errno));
		return -1;
	}

	devlist = sysfs_get_driver_devices(usbip_host_drv);
	if (!devlist) {
		err("%s is not in use by any devices", USBIP_HOST_DRV_NAME);
		goto err_close_usbip_host_drv;
	}

	dlist_for_each_data(devlist, dev, struct sysfs_device) {
		if (!strncmp(busid, dev->name, strlen(busid)) &&
		    !strncmp(dev->driver_name, USBIP_HOST_DRV_NAME,
			     strlen(USBIP_HOST_DRV_NAME))) {
			verified = 1;
			break;
		}
	}

	if (!verified) {
		err("device on busid %s is not using %s", busid,
		    USBIP_HOST_DRV_NAME);
		goto err_close_usbip_host_drv;
	}

	/*
	 * NOTE: A read and write of an attribute value of the device busid
	 * refers to must be done to start probing. That way a rebind of the
	 * default driver for the device occurs.
	 *
	 * This seems very hackish and adds a lot of pointless code. I think it
	 * should be done in the kernel by the driver after del_match_busid is
	 * finished!
	 */

	rc = sysfs_get_mnt_path(sysfs_mntpath, SYSFS_PATH_MAX);
	if (rc < 0) {
		err("sysfs must be mounted: %s", strerror(errno));
		return -1;
	}

	snprintf(busid_attr_path, sizeof(busid_attr_path), "%s/%s/%s/%s/%s/%s",
		 sysfs_mntpath, SYSFS_BUS_NAME, bus_type, SYSFS_DEVICES_NAME,
		 busid, attr_name);

	/* read a device attribute */
	busid_attr = sysfs_open_attribute(busid_attr_path);
	if (!busid_attr) {
		err("could not open %s/%s: %s", busid, attr_name,
		    strerror(errno));
		return -1;
	}

	if (sysfs_read_attribute(busid_attr) < 0) {
		err("problem reading attribute: %s", strerror(errno));
		goto err_out;
	}

	len = busid_attr->len;
	val = malloc(len);
	*val = *busid_attr->value;
	sysfs_close_attribute(busid_attr);

	/* notify driver of unbind */
	rc = modify_match_busid(busid, 0);
	if (rc < 0) {
		err("unable to unbind device on %s", busid);
		goto err_out;
	}

	/* write the device attribute */
	busid_attr = sysfs_open_attribute(busid_attr_path);
	if (!busid_attr) {
		err("could not open %s/%s: %s", busid, attr_name,
		    strerror(errno));
		return -1;
	}

	rc = sysfs_write_attribute(busid_attr, val, len);
	if (rc < 0) {
		err("problem writing attribute: %s", strerror(errno));
		goto err_out;
	}
	sysfs_close_attribute(busid_attr);

	ret = 0;
	printf("unbind device on busid %s: complete\n", busid);

err_out:
	free(val);
err_close_usbip_host_drv:
	sysfs_close_driver(usbip_host_drv);

	return ret;
}

int usbip_unbind(int argc, char *argv[])
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
			ret = unbind_device(optarg);
			goto out;
		default:
			goto err_out;
		}
	}

err_out:
	usbip_unbind_usage();
out:
	return ret;
}
