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

#include <sys/types.h>
#include <sysfs/libsysfs.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>
#include <netdb.h>
#include <unistd.h>

#include "usbip_common.h"
#include "usbip_network.h"
#include "usbip.h"

static const char usbip_list_usage_string[] =
	"usbip list [-p|--parsable] <args>\n"
	"    -p, --parsable         Parsable list format\n"
	"    -r, --remote=<host>    List the exportable USB devices on <host>\n"
	"    -l, --local            List the local USB devices\n";

void usbip_list_usage(void)
{
	printf("usage: %s", usbip_list_usage_string);
}

static int get_exported_devices(char *host, int sockfd)
{
	char product_name[100];
	char class_name[100];
	struct op_devlist_reply reply;
	uint16_t code = OP_REP_DEVLIST;
	struct usbip_usb_device udev;
	struct usbip_usb_interface uintf;
	unsigned int i;
	int j, rc;

	rc = usbip_net_send_op_common(sockfd, OP_REQ_DEVLIST, 0);
	if (rc < 0) {
		dbg("usbip_net_send_op_common failed");
		return -1;
	}

	rc = usbip_net_recv_op_common(sockfd, &code);
	if (rc < 0) {
		dbg("usbip_net_recv_op_common failed");
		return -1;
	}

	memset(&reply, 0, sizeof(reply));
	rc = usbip_net_recv(sockfd, &reply, sizeof(reply));
	if (rc < 0) {
		dbg("usbip_net_recv_op_devlist failed");
		return -1;
	}
	PACK_OP_DEVLIST_REPLY(0, &reply);
	dbg("exportable devices: %d\n", reply.ndev);

	if (reply.ndev == 0) {
		info("no exportable devices found on %s", host);
		return 0;
	}

	printf("Exportable USB devices\n");
	printf("======================\n");
	printf(" - %s\n", host);

	for (i = 0; i < reply.ndev; i++) {
		memset(&udev, 0, sizeof(udev));
		rc = usbip_net_recv(sockfd, &udev, sizeof(udev));
		if (rc < 0) {
			dbg("usbip_net_recv failed: usbip_usb_device[%d]", i);
			return -1;
		}
		usbip_net_pack_usb_device(0, &udev);

		usbip_names_get_product(product_name, sizeof(product_name),
					udev.idVendor, udev.idProduct);
		usbip_names_get_class(class_name, sizeof(class_name),
				      udev.bDeviceClass, udev.bDeviceSubClass,
				      udev.bDeviceProtocol);
		printf("%11s: %s\n", udev.busid, product_name);
		printf("%11s: %s\n", "", udev.path);
		printf("%11s: %s\n", "", class_name);

		for (j = 0; j < udev.bNumInterfaces; j++) {
			rc = usbip_net_recv(sockfd, &uintf, sizeof(uintf));
			if (rc < 0) {
				dbg("usbip_net_recv failed: usbip_usb_intf[%d]",
				    j);

				return -1;
			}
			usbip_net_pack_usb_interface(0, &uintf);

			usbip_names_get_class(class_name, sizeof(class_name),
					      uintf.bInterfaceClass,
					      uintf.bInterfaceSubClass,
					      uintf.bInterfaceProtocol);
			printf("%11s: %2d - %s\n", "", j, class_name);
		}
		printf("\n");
	}

	return 0;
}

static int list_exported_devices(char *host)
{
	int rc;
	int sockfd;

	sockfd = usbip_net_tcp_connect(host, USBIP_PORT_STRING);
	if (sockfd < 0) {
		err("could not connect to %s:%s: %s", host,
		    USBIP_PORT_STRING, gai_strerror(sockfd));
		return -1;
	}
	dbg("connected to %s:%s", host, USBIP_PORT_STRING);

	rc = get_exported_devices(host, sockfd);
	if (rc < 0) {
		err("failed to get device list from %s", host);
		return -1;
	}

	close(sockfd);

	return 0;
}

static void print_device(char *busid, char *vendor, char *product,
			 bool parsable)
{
	if (parsable)
		printf("busid=%s#usbid=%.4s:%.4s#", busid, vendor, product);
	else
		printf(" - busid %s (%.4s:%.4s)\n", busid, vendor, product);
}

static void print_interface(char *busid, char *driver, bool parsable)
{
	if (parsable)
		printf("%s=%s#", busid, driver);
	else
		printf("%9s%s -> %s\n", "", busid, driver);
}

static int is_device(void *x)
{
	struct sysfs_attribute *devpath;
	struct sysfs_device *dev = x;
	int ret = 0;

	devpath = sysfs_get_device_attr(dev, "devpath");
	if (devpath && *devpath->value != '0')
		ret = 1;

	return ret;
}

static int devcmp(void *a, void *b)
{
	return strcmp(a, b);
}

static int list_devices(bool parsable)
{
	char bus_type[] = "usb";
	char busid[SYSFS_BUS_ID_SIZE];
	struct sysfs_bus *ubus;
	struct sysfs_device *dev;
	struct sysfs_device *intf;
	struct sysfs_attribute *idVendor;
	struct sysfs_attribute *idProduct;
	struct sysfs_attribute *bConfValue;
	struct sysfs_attribute *bNumIntfs;
	struct dlist *devlist;
	int i;
	int ret = -1;

	ubus = sysfs_open_bus(bus_type);
	if (!ubus) {
		err("could not open %s bus: %s", bus_type, strerror(errno));
		return -1;
	}

	devlist = sysfs_get_bus_devices(ubus);
	if (!devlist) {
		err("could not get %s bus devices: %s", bus_type,
		    strerror(errno));
		goto err_out;
	}

	/* remove interfaces and root hubs from device list */
	dlist_filter_sort(devlist, is_device, devcmp);

	if (!parsable) {
		printf("Local USB devices\n");
		printf("=================\n");
	}
	dlist_for_each_data(devlist, dev, struct sysfs_device) {
		idVendor   = sysfs_get_device_attr(dev, "idVendor");
		idProduct  = sysfs_get_device_attr(dev, "idProduct");
		bConfValue = sysfs_get_device_attr(dev, "bConfigurationValue");
		bNumIntfs  = sysfs_get_device_attr(dev, "bNumInterfaces");
		if (!idVendor || !idProduct || !bConfValue || !bNumIntfs) {
			err("problem getting device attributes: %s",
			    strerror(errno));
			goto err_out;
		}

		print_device(dev->bus_id, idVendor->value, idProduct->value,
			     parsable);

		for (i = 0; i < atoi(bNumIntfs->value); i++) {
			snprintf(busid, sizeof(busid), "%s:%.1s.%d",
				 dev->bus_id, bConfValue->value, i);
			intf = sysfs_open_device(bus_type, busid);
			if (!intf) {
				err("could not open device interface: %s",
				    strerror(errno));
				goto err_out;
			}
			print_interface(busid, intf->driver_name, parsable);
			sysfs_close_device(intf);
		}
		printf("\n");
	}

	ret = 0;

err_out:
	sysfs_close_bus(ubus);

	return ret;
}

int usbip_list(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "parsable", no_argument,       NULL, 'p' },
		{ "remote",   required_argument, NULL, 'r' },
		{ "local",    no_argument,       NULL, 'l' },
		{ NULL,       0,                 NULL,  0  }
	};

	bool parsable = false;
	int opt;
	int ret = -1;

	if (usbip_names_init(USBIDS_FILE))
		err("failed to open %s", USBIDS_FILE);

	for (;;) {
		opt = getopt_long(argc, argv, "pr:l", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'p':
			parsable = true;
			break;
		case 'r':
			ret = list_exported_devices(optarg);
			goto out;
		case 'l':
			ret = list_devices(parsable);
			goto out;
		default:
			goto err_out;
		}
	}

err_out:
	usbip_list_usage();
out:
	usbip_names_free();

	return ret;
}
