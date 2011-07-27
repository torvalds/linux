/*
 *
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "usbip.h"
#include "usbip_network.h"
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <glib.h>

static const char version[] = PACKAGE_STRING;


/* /sys/devices/platform/vhci_hcd/usb6/6-1/6-1:1.1  -> 1 */
static int get_interface_number(char *path)
{
	char *c;

	c = strstr(path, vhci_driver->hc_device->bus_id);
	if (!c)
		return -1;	/* hc exist? */
	c++;
	/* -> usb6/6-1/6-1:1.1 */

	c = strchr(c, '/');
	if (!c)
		return -1;	/* hc exist? */
	c++;
	/* -> 6-1/6-1:1.1 */

	c = strchr(c, '/');
	if (!c)
		return -1;	/* no interface path */
	c++;
	/* -> 6-1:1.1 */

	c = strchr(c, ':');
	if (!c)
		return -1;	/* no configuration? */
	c++;
	/* -> 1.1 */

	c = strchr(c, '.');
	if (!c)
		return -1;	/* no interface? */
	c++;
	/* -> 1 */


	return atoi(c);
}


static struct sysfs_device *open_usb_interface(struct usb_device *udev, int i)
{
	struct sysfs_device *suinf;
	char busid[SYSFS_BUS_ID_SIZE];

	snprintf(busid, SYSFS_BUS_ID_SIZE, "%s:%d.%d",
			udev->busid, udev->bConfigurationValue, i);

	suinf = sysfs_open_device("usb", busid);
	if (!suinf)
		err("sysfs_open_device %s", busid);

	return suinf;
}


#define MAX_BUFF 100
static int record_connection(char *host, char *port, char *busid, int rhport)
{
	int fd;
	char path[PATH_MAX+1];
	char buff[MAX_BUFF+1];
	int ret;

	mkdir(VHCI_STATE_PATH, 0700);

	snprintf(path, PATH_MAX, VHCI_STATE_PATH"/port%d", rhport);

	fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
	if (fd < 0)
		return -1;

	snprintf(buff, MAX_BUFF, "%s %s %s\n",
			host, port, busid);

	ret = write(fd, buff, strlen(buff));
	if (ret != (ssize_t) strlen(buff)) {
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

static int read_record(int rhport, char *host, char *port, char *busid)
{
	FILE *file;
	char path[PATH_MAX+1];

	snprintf(path, PATH_MAX, VHCI_STATE_PATH"/port%d", rhport);

	file = fopen(path, "r");
	if (!file) {
		err("fopen");
		return -1;
	}

	if (fscanf(file, "%s %s %s\n", host, port, busid) != 3) {
		err("fscanf");
		fclose(file);
		return -1;
	}

	fclose(file);

	return 0;
}


int usbip_vhci_imported_device_dump(struct usbip_imported_device *idev)
{
	char product_name[100];
	char host[NI_MAXHOST] = "unknown host";
	char serv[NI_MAXSERV] = "unknown port";
	char remote_busid[SYSFS_BUS_ID_SIZE];
	int ret;

	if (idev->status == VDEV_ST_NULL || idev->status == VDEV_ST_NOTASSIGNED) {
		info("Port %02d: <%s>", idev->port, usbip_status_string(idev->status));
		return 0;
	}

	ret = read_record(idev->port, host, serv, remote_busid);
	if (ret) {
		err("read_record");
		return -1;
	}

	info("Port %02d: <%s> at %s", idev->port,
			usbip_status_string(idev->status), usbip_speed_string(idev->udev.speed));

	usbip_names_get_product(product_name, sizeof(product_name),
			idev->udev.idVendor, idev->udev.idProduct);

	info("       %s",  product_name);

	info("%10s -> usbip://%s:%s/%s  (remote devid %08x (bus/dev %03d/%03d))",
			idev->udev.busid, host, serv, remote_busid,
			idev->devid,
			idev->busnum, idev->devnum);

	for (int i=0; i < idev->udev.bNumInterfaces; i++) {
		/* show interface information */
		struct sysfs_device *suinf;

		suinf = open_usb_interface(&idev->udev, i);
		if (!suinf)
			continue;

		info("       %6s used by %-17s", suinf->bus_id, suinf->driver_name);
		sysfs_close_device(suinf);

		/* show class device information */
		struct class_device *cdev;

		dlist_for_each_data(idev->cdev_list, cdev, struct class_device) {
			int ifnum = get_interface_number(cdev->devpath);
			if (ifnum == i) {
				info("           %s", cdev->clspath);
			}
		}
	}

	return 0;
}




static int query_exported_devices(int sockfd)
{
	int ret;
	struct op_devlist_reply rep;
	uint16_t code = OP_REP_DEVLIST;

	bzero(&rep, sizeof(rep));

	ret = usbip_send_op_common(sockfd, OP_REQ_DEVLIST, 0);
	if (ret < 0) {
		err("send op_common");
		return -1;
	}

	ret = usbip_recv_op_common(sockfd, &code);
	if (ret < 0) {
		err("recv op_common");
		return -1;
	}

	ret = usbip_recv(sockfd, (void *) &rep, sizeof(rep));
	if (ret < 0) {
		err("recv op_devlist");
		return -1;
	}

	PACK_OP_DEVLIST_REPLY(0, &rep);
	dbg("exportable %d devices", rep.ndev);

	for (unsigned int i=0; i < rep.ndev; i++) {
		char product_name[100];
		char class_name[100];
		struct usb_device udev;

		bzero(&udev, sizeof(udev));

		ret = usbip_recv(sockfd, (void *) &udev, sizeof(udev));
		if (ret < 0) {
			err("recv usb_device[%d]", i);
			return -1;
		}
		pack_usb_device(0, &udev);

		usbip_names_get_product(product_name, sizeof(product_name),
				udev.idVendor, udev.idProduct);
		usbip_names_get_class(class_name, sizeof(class_name), udev.bDeviceClass,
				udev.bDeviceSubClass, udev.bDeviceProtocol);

		info("%8s: %s", udev.busid, product_name);
		info("%8s: %s", " ", udev.path);
		info("%8s: %s", " ", class_name);

		for (int j=0; j < udev.bNumInterfaces; j++) {
			struct usb_interface uinf;

			ret = usbip_recv(sockfd, (void *) &uinf, sizeof(uinf));
			if (ret < 0) {
				err("recv usb_interface[%d]", j);
				return -1;
			}

			pack_usb_interface(0, &uinf);
			usbip_names_get_class(class_name, sizeof(class_name), uinf.bInterfaceClass,
					uinf.bInterfaceSubClass, uinf.bInterfaceProtocol);

			info("%8s: %2d - %s", " ", j, class_name);
		}

		info(" ");
	}

	return rep.ndev;
}

static int import_device(int sockfd, struct usb_device *udev)
{
	int ret;
	int port;

	ret = usbip_vhci_driver_open();
	if (ret < 0) {
		err("open vhci_driver");
		return -1;
	}

	port = usbip_vhci_get_free_port();
	if (port < 0) {
		err("no free port");
		usbip_vhci_driver_close();
		return -1;
	}

	ret = usbip_vhci_attach_device(port, sockfd, udev->busnum,
			udev->devnum, udev->speed);
	if (ret < 0) {
		err("import device");
		usbip_vhci_driver_close();
		return -1;
	}

	usbip_vhci_driver_close();

	return port;
}


static int query_import_device(int sockfd, char *busid)
{
	int ret;
	struct op_import_request request;
	struct op_import_reply   reply;
	uint16_t code = OP_REP_IMPORT;

	bzero(&request, sizeof(request));
	bzero(&reply, sizeof(reply));


	/* send a request */
	ret = usbip_send_op_common(sockfd, OP_REQ_IMPORT, 0);
	if (ret < 0) {
		err("send op_common");
		return -1;
	}

	strncpy(request.busid, busid, SYSFS_BUS_ID_SIZE-1);

	PACK_OP_IMPORT_REQUEST(0, &request);

	ret = usbip_send(sockfd, (void *) &request, sizeof(request));
	if (ret < 0) {
		err("send op_import_request");
		return -1;
	}


	/* recieve a reply */
	ret = usbip_recv_op_common(sockfd, &code);
	if (ret < 0) {
		err("recv op_common");
		return -1;
	}

	ret = usbip_recv(sockfd, (void *) &reply, sizeof(reply));
	if (ret < 0) {
		err("recv op_import_reply");
		return -1;
	}

	PACK_OP_IMPORT_REPLY(0, &reply);


	/* check the reply */
	if (strncmp(reply.udev.busid, busid, SYSFS_BUS_ID_SIZE)) {
		err("recv different busid %s", reply.udev.busid);
		return -1;
	}


	/* import a device */
	return import_device(sockfd, &reply.udev);
}

static int attach_device(char *host, char *busid)
{
	int sockfd;
	int ret;
	int rhport;

	sockfd = tcp_connect(host, USBIP_PORT_STRING);
	if (sockfd < 0) {
		err("tcp connect");
		return -1;
	}

	rhport = query_import_device(sockfd, busid);
	if (rhport < 0) {
		err("query");
		return -1;
	}

	close(sockfd);

	ret = record_connection(host, USBIP_PORT_STRING,
			busid, rhport);
	if (ret < 0) {
		err("record connection");
		return -1;
	}

	return 0;
}

static int detach_port(char *port)
{
	int ret;
	uint8_t portnum;

	for (unsigned int i=0; i < strlen(port); i++)
		if (!isdigit(port[i])) {
			err("invalid port %s", port);
			return -1;
		}

	/* check max port */

	portnum = atoi(port);

	ret = usbip_vhci_driver_open();
	if (ret < 0) {
		err("open vhci_driver");
		return -1;
	}

	ret = usbip_vhci_detach_device(portnum);
	if (ret < 0)
		return -1;

	usbip_vhci_driver_close();

	return ret;
}

static int show_exported_devices(char *host)
{
	int ret;
	int sockfd;

	sockfd = tcp_connect(host, USBIP_PORT_STRING);
	if (sockfd < 0) {
		err("- %s failed", host);
		return -1;
	}

	info("- %s", host);

	ret = query_exported_devices(sockfd);
	if (ret < 0) {
		err("query");
		return -1;
	}

	close(sockfd);
	return 0;
}

static int attach_exported_devices(char *host, int sockfd)
{
	int ret;
	struct op_devlist_reply rep;
	uint16_t code = OP_REP_DEVLIST;

	bzero(&rep, sizeof(rep));

	ret = usbip_send_op_common(sockfd, OP_REQ_DEVLIST, 0);
	if(ret < 0) {
		err("send op_common");
		return -1;
	}

	ret = usbip_recv_op_common(sockfd, &code);
	if(ret < 0) {
		err("recv op_common");
		return -1;
	}

	ret = usbip_recv(sockfd, (void *) &rep, sizeof(rep));
	if(ret < 0) {
		err("recv op_devlist");
		return -1;
	}

	PACK_OP_DEVLIST_REPLY(0, &rep);
	dbg("exportable %d devices", rep.ndev);

	for(unsigned int i=0; i < rep.ndev; i++) {
		char product_name[100];
		char class_name[100];
		struct usb_device udev;

		bzero(&udev, sizeof(udev));

		ret = usbip_recv(sockfd, (void *) &udev, sizeof(udev));
		if(ret < 0) {
			err("recv usb_device[%d]", i);
			return -1;
		}
		pack_usb_device(0, &udev);

		usbip_names_get_product(product_name, sizeof(product_name),
				udev.idVendor, udev.idProduct);
		usbip_names_get_class(class_name, sizeof(class_name), udev.bDeviceClass,
				udev.bDeviceSubClass, udev.bDeviceProtocol);

		dbg("Attaching usb port %s from host %s on usbip, with deviceid: %s", udev.busid, host, product_name);

		for (int j=0; j < udev.bNumInterfaces; j++) {
			struct usb_interface uinf;

			ret = usbip_recv(sockfd, (void *) &uinf, sizeof(uinf));
			if (ret < 0) {
				err("recv usb_interface[%d]", j);
				return -1;
			}

			pack_usb_interface(0, &uinf);
			usbip_names_get_class(class_name, sizeof(class_name), uinf.bInterfaceClass,
					uinf.bInterfaceSubClass, uinf.bInterfaceProtocol);

			dbg("interface %2d - %s", j, class_name);
		}

		attach_device(host, udev.busid);
	}

	return rep.ndev;
}

static int attach_devices_all(char *host)
{
	int ret;
	int sockfd;

	sockfd = tcp_connect(host, USBIP_PORT_STRING);
	if(sockfd < 0) {
		err("- %s failed", host);
		return -1;
	}

	info("- %s", host);

	ret = attach_exported_devices(host, sockfd);
	if(ret < 0) {
		err("query");
		return -1;
	}

	close(sockfd);
	return 0;
}


const char help_message[] = "\
Usage: usbip [options]				\n\
	-a, --attach [host] [bus_id]		\n\
		Attach a remote USB device.	\n\
						\n\
	-x, --attachall [host]		\n\
		Attach all remote USB devices on the specific host.	\n\
						\n\
	-d, --detach [ports]			\n\
		Detach an imported USB device.	\n\
						\n\
	-l, --list [hosts]			\n\
		List exported USB devices.	\n\
						\n\
	-p, --port				\n\
		List virtual USB port status. 	\n\
						\n\
	-D, --debug				\n\
		Print debugging information.	\n\
						\n\
	-v, --version				\n\
		Show version.			\n\
						\n\
	-h, --help 				\n\
		Print this help.		\n";

static void show_help(void)
{
	printf("%s", help_message);
}

static int show_port_status(void)
{
	int ret;
	struct usbip_imported_device *idev;

	ret = usbip_vhci_driver_open();
	if (ret < 0)
		return ret;

	for (int i = 0; i < vhci_driver->nports; i++) {
		idev = &vhci_driver->idev[i];

		if (usbip_vhci_imported_device_dump(idev) < 0)
			ret = -1;
	}

	usbip_vhci_driver_close();

	return ret;
}

#define _GNU_SOURCE
#include <getopt.h>
static const struct option longopts[] = {
	{"attach",	no_argument,	NULL, 'a'},
	{"attachall",	no_argument,	NULL, 'x'},
	{"detach",	no_argument,	NULL, 'd'},
	{"port",	no_argument,	NULL, 'p'},
	{"list",	no_argument,	NULL, 'l'},
	{"version",	no_argument,	NULL, 'v'},
	{"help",	no_argument,	NULL, 'h'},
	{"debug",	no_argument,	NULL, 'D'},
	{"syslog",	no_argument,	NULL, 'S'},
	{NULL,		0,		NULL,  0}
};

int main(int argc, char *argv[])
{
	int ret;

	enum {
		cmd_attach = 1,
		cmd_attachall,
		cmd_detach,
		cmd_port,
		cmd_list,
		cmd_help,
		cmd_version
	} cmd = 0;

	usbip_use_stderr = 1;

	if (geteuid() != 0)
		g_warning("running non-root?");

	ret = usbip_names_init(USBIDS_FILE);
	if (ret)
		notice("failed to open %s", USBIDS_FILE);

	for (;;) {
		int c;
		int index = 0;

		c = getopt_long(argc, argv, "adplvhDSx", longopts, &index);

		if (c == -1)
			break;

		switch(c) {
			case 'a':
				if (!cmd)
					cmd = cmd_attach;
				else
					cmd = cmd_help;
				break;
			case 'd':
				if (!cmd)
					cmd = cmd_detach;
				else
					cmd = cmd_help;
				break;
			case 'p':
				if (!cmd)
					cmd = cmd_port;
				else cmd = cmd_help;
				break;
			case 'l':
				if (!cmd)
					cmd = cmd_list;
				else
					cmd = cmd_help;
				break;
			case 'v':
				if (!cmd)
					cmd = cmd_version;
				else
					cmd = cmd_help;
				break;
			case 'x':
				if(!cmd)
					cmd = cmd_attachall;
				else
					cmd = cmd_help;
				break;
			case 'h':
				cmd = cmd_help;
				break;
			case 'D':
				usbip_use_debug = 1;
				break;
			case 'S':
				usbip_use_syslog = 1;
				break;
			case '?':
				break;

			default:
				err("getopt");
		}
	}

	ret = 0;
	switch(cmd) {
		case cmd_attach:
			if (optind == argc - 2)
				ret = attach_device(argv[optind], argv[optind+1]);
			else
				show_help();
			break;
		case cmd_detach:
			while (optind < argc)
				ret = detach_port(argv[optind++]);
			break;
		case cmd_port:
			ret = show_port_status();
			break;
		case cmd_list:
			while (optind < argc)
				ret = show_exported_devices(argv[optind++]);
			break;
		case cmd_attachall:
			while(optind < argc)
				ret = attach_devices_all(argv[optind++]);
			break;
		case cmd_version:
			printf("%s\n", version);
			break;
		case cmd_help:
			show_help();
			break;
		default:
			show_help();
	}


	usbip_names_free();

	exit((ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE);
}
