/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include "usbip_common.h"
#include "vhci_driver.h"
#include <limits.h>
#include <netdb.h>
#include <libudev.h>
#include "sysfs_utils.h"

#undef  PROGNAME
#define PROGNAME "libusbip"

struct usbip_vhci_driver *vhci_driver;
struct udev *udev_context;

static struct usbip_imported_device *
imported_device_init(struct usbip_imported_device *idev, char *busid)
{
	struct udev_device *sudev;

	sudev = udev_device_new_from_subsystem_sysname(udev_context,
						       "usb", busid);
	if (!sudev) {
		dbg("udev_device_new_from_subsystem_sysname failed: %s", busid);
		goto err;
	}
	read_usb_device(sudev, &idev->udev);
	udev_device_unref(sudev);

	return idev;

err:
	return NULL;
}



static int parse_status(const char *value)
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

		ret = sscanf(c, "%d %d %d %x %lx %31s\n",
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

			if (idev->status != VDEV_ST_NULL
			    && idev->status != VDEV_ST_NOTASSIGNED) {
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

static int refresh_imported_device_list(void)
{
	const char *attr_status;

	attr_status = udev_device_get_sysattr_value(vhci_driver->hc_device,
					       "status");
	if (!attr_status) {
		err("udev_device_get_sysattr_value failed");
		return -1;
	}

	return parse_status(attr_status);
}

static int get_nports(void)
{
	char *c;
	int nports = 0;
	const char *attr_status;

	attr_status = udev_device_get_sysattr_value(vhci_driver->hc_device,
					       "status");
	if (!attr_status) {
		err("udev_device_get_sysattr_value failed");
		return -1;
	}

	/* skip a header line */
	c = strchr(attr_status, '\n');
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

/*
 * Read the given port's record.
 *
 * To avoid buffer overflow we will read the entire line and
 * validate each part's size. The initial buffer is padded by 4 to
 * accommodate the 2 spaces, 1 newline and an additional character
 * which is needed to properly validate the 3rd part without it being
 * truncated to an acceptable length.
 */
static int read_record(int rhport, char *host, unsigned long host_len,
		char *port, unsigned long port_len, char *busid)
{
	int part;
	FILE *file;
	char path[PATH_MAX+1];
	char *buffer, *start, *end;
	char delim[] = {' ', ' ', '\n'};
	int max_len[] = {(int)host_len, (int)port_len, SYSFS_BUS_ID_SIZE};
	size_t buffer_len = host_len + port_len + SYSFS_BUS_ID_SIZE + 4;

	buffer = malloc(buffer_len);
	if (!buffer)
		return -1;

	snprintf(path, PATH_MAX, VHCI_STATE_PATH"/port%d", rhport);

	file = fopen(path, "r");
	if (!file) {
		err("fopen");
		free(buffer);
		return -1;
	}

	if (fgets(buffer, buffer_len, file) == NULL) {
		err("fgets");
		free(buffer);
		fclose(file);
		return -1;
	}
	fclose(file);

	/* validate the length of each of the 3 parts */
	start = buffer;
	for (part = 0; part < 3; part++) {
		end = strchr(start, delim[part]);
		if (end == NULL || (end - start) > max_len[part]) {
			free(buffer);
			return -1;
		}
		start = end + 1;
	}

	if (sscanf(buffer, "%s %s %s\n", host, port, busid) != 3) {
		err("sscanf");
		free(buffer);
		return -1;
	}

	free(buffer);

	return 0;
}

/* ---------------------------------------------------------------------- */

int usbip_vhci_driver_open(void)
{
	udev_context = udev_new();
	if (!udev_context) {
		err("udev_new failed");
		return -1;
	}

	vhci_driver = calloc(1, sizeof(struct usbip_vhci_driver));

	/* will be freed in usbip_driver_close() */
	vhci_driver->hc_device =
		udev_device_new_from_subsystem_sysname(udev_context,
						       USBIP_VHCI_BUS_TYPE,
						       USBIP_VHCI_DRV_NAME);
	if (!vhci_driver->hc_device) {
		err("udev_device_new_from_subsystem_sysname failed");
		goto err;
	}

	vhci_driver->nports = get_nports();

	dbg("available ports: %d", vhci_driver->nports);

	if (refresh_imported_device_list())
		goto err;

	return 0;

err:
	udev_device_unref(vhci_driver->hc_device);

	if (vhci_driver)
		free(vhci_driver);

	vhci_driver = NULL;

	udev_unref(udev_context);

	return -1;
}


void usbip_vhci_driver_close()
{
	if (!vhci_driver)
		return;

	udev_device_unref(vhci_driver->hc_device);

	free(vhci_driver);

	vhci_driver = NULL;

	udev_unref(udev_context);
}


int usbip_vhci_refresh_device_list(void)
{

	if (refresh_imported_device_list())
		goto err;

	return 0;
err:
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
	char buff[200]; /* what size should be ? */
	char attach_attr_path[SYSFS_PATH_MAX];
	char attr_attach[] = "attach";
	const char *path;
	int ret;

	snprintf(buff, sizeof(buff), "%u %d %u %u",
			port, sockfd, devid, speed);
	dbg("writing: %s", buff);

	path = udev_device_get_syspath(vhci_driver->hc_device);
	snprintf(attach_attr_path, sizeof(attach_attr_path), "%s/%s",
		 path, attr_attach);
	dbg("attach attribute path: %s", attach_attr_path);

	ret = write_sysfs_attribute(attach_attr_path, buff, strlen(buff));
	if (ret < 0) {
		dbg("write_sysfs_attribute failed");
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
	char detach_attr_path[SYSFS_PATH_MAX];
	char attr_detach[] = "detach";
	char buff[200]; /* what size should be ? */
	const char *path;
	int ret;

	snprintf(buff, sizeof(buff), "%u", port);
	dbg("writing: %s", buff);

	path = udev_device_get_syspath(vhci_driver->hc_device);
	snprintf(detach_attr_path, sizeof(detach_attr_path), "%s/%s",
		 path, attr_detach);
	dbg("detach attribute path: %s", detach_attr_path);

	ret = write_sysfs_attribute(detach_attr_path, buff, strlen(buff));
	if (ret < 0) {
		dbg("write_sysfs_attribute failed");
		return -1;
	}

	dbg("detached port: %d", port);

	return 0;
}

int usbip_vhci_imported_device_dump(struct usbip_imported_device *idev)
{
	char product_name[100];
	char host[NI_MAXHOST] = "unknown host";
	char serv[NI_MAXSERV] = "unknown port";
	char remote_busid[SYSFS_BUS_ID_SIZE];
	int ret;
	int read_record_error = 0;

	if (idev->status == VDEV_ST_NULL || idev->status == VDEV_ST_NOTASSIGNED)
		return 0;

	ret = read_record(idev->port, host, sizeof(host), serv, sizeof(serv),
			  remote_busid);
	if (ret) {
		err("read_record");
		read_record_error = 1;
	}

	printf("Port %02d: <%s> at %s\n", idev->port,
	       usbip_status_string(idev->status),
	       usbip_speed_string(idev->udev.speed));

	usbip_names_get_product(product_name, sizeof(product_name),
				idev->udev.idVendor, idev->udev.idProduct);

	printf("       %s\n",  product_name);

	if (!read_record_error) {
		printf("%10s -> usbip://%s:%s/%s\n", idev->udev.busid,
		       host, serv, remote_busid);
		printf("%10s -> remote bus/dev %03d/%03d\n", " ",
		       idev->busnum, idev->devnum);
	} else {
		printf("%10s -> unknown host, remote port and remote busid\n",
		       idev->udev.busid);
		printf("%10s -> remote bus/dev %03d/%03d\n", " ",
		       idev->busnum, idev->devnum);
	}

	return 0;
}
