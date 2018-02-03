/*
 * Copyright (C) 2003-2008 Takahiro Hirofuchi
 * Copyright (C) 2015-2016 Nobuo Iwata
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 */

#include <linux/kthread.h>
#include <linux/file.h>
#include <linux/net.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "usbip_common.h"
#include "vhci.h"

/* TODO: refine locking ?*/

/*
 * output example:
 * hub port sta spd dev       sockfd    local_busid
 * hs  0000 004 000 00000000  3         1-2.3
 * ................................................
 * ss  0008 004 000 00000000  4         2-3.4
 * ................................................
 *
 * Output includes socket fd instead of socket pointer address to avoid
 * leaking kernel memory address in:
 *	/sys/devices/platform/vhci_hcd.0/status and in debug output.
 * The socket pointer address is not used at the moment and it was made
 * visible as a convenient way to find IP address from socket pointer
 * address by looking up /proc/net/{tcp,tcp6}. As this opens a security
 * hole, the change is made to use sockfd instead.
 *
 */
static void port_show_vhci(char **out, int hub, int port, struct vhci_device *vdev)
{
	if (hub == HUB_SPEED_HIGH)
		*out += sprintf(*out, "hs  %04u %03u ",
				      port, vdev->ud.status);
	else /* hub == HUB_SPEED_SUPER */
		*out += sprintf(*out, "ss  %04u %03u ",
				      port, vdev->ud.status);

	if (vdev->ud.status == VDEV_ST_USED) {
		*out += sprintf(*out, "%03u %08x ",
				      vdev->speed, vdev->devid);
		*out += sprintf(*out, "%u %s",
				      vdev->ud.sockfd,
				      dev_name(&vdev->udev->dev));

	} else {
		*out += sprintf(*out, "000 00000000 ");
		*out += sprintf(*out, "0000000000000000 0-0");
	}

	*out += sprintf(*out, "\n");
}

/* Sysfs entry to show port status */
static ssize_t status_show_vhci(int pdev_nr, char *out)
{
	struct platform_device *pdev = vhcis[pdev_nr].pdev;
	struct vhci *vhci;
	struct usb_hcd *hcd;
	struct vhci_hcd *vhci_hcd;
	char *s = out;
	int i;
	unsigned long flags;

	if (!pdev || !out) {
		usbip_dbg_vhci_sysfs("show status error\n");
		return 0;
	}

	hcd = platform_get_drvdata(pdev);
	vhci_hcd = hcd_to_vhci_hcd(hcd);
	vhci = vhci_hcd->vhci;

	spin_lock_irqsave(&vhci->lock, flags);

	for (i = 0; i < VHCI_HC_PORTS; i++) {
		struct vhci_device *vdev = &vhci->vhci_hcd_hs->vdev[i];

		spin_lock(&vdev->ud.lock);
		port_show_vhci(&out, HUB_SPEED_HIGH,
			       pdev_nr * VHCI_PORTS + i, vdev);
		spin_unlock(&vdev->ud.lock);
	}

	for (i = 0; i < VHCI_HC_PORTS; i++) {
		struct vhci_device *vdev = &vhci->vhci_hcd_ss->vdev[i];

		spin_lock(&vdev->ud.lock);
		port_show_vhci(&out, HUB_SPEED_SUPER,
			       pdev_nr * VHCI_PORTS + VHCI_HC_PORTS + i, vdev);
		spin_unlock(&vdev->ud.lock);
	}

	spin_unlock_irqrestore(&vhci->lock, flags);

	return out - s;
}

static ssize_t status_show_not_ready(int pdev_nr, char *out)
{
	char *s = out;
	int i = 0;

	for (i = 0; i < VHCI_HC_PORTS; i++) {
		out += sprintf(out, "hs  %04u %03u ",
				    (pdev_nr * VHCI_PORTS) + i,
				    VDEV_ST_NOTASSIGNED);
		out += sprintf(out, "000 00000000 0000000000000000 0-0");
		out += sprintf(out, "\n");
	}

	for (i = 0; i < VHCI_HC_PORTS; i++) {
		out += sprintf(out, "ss  %04u %03u ",
				    (pdev_nr * VHCI_PORTS) + VHCI_HC_PORTS + i,
				    VDEV_ST_NOTASSIGNED);
		out += sprintf(out, "000 00000000 0000000000000000 0-0");
		out += sprintf(out, "\n");
	}
	return out - s;
}

static int status_name_to_id(const char *name)
{
	char *c;
	long val;
	int ret;

	c = strchr(name, '.');
	if (c == NULL)
		return 0;

	ret = kstrtol(c+1, 10, &val);
	if (ret < 0)
		return ret;

	return val;
}

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *out)
{
	char *s = out;
	int pdev_nr;

	out += sprintf(out,
		       "hub port sta spd dev      socket           local_busid\n");

	pdev_nr = status_name_to_id(attr->attr.name);
	if (pdev_nr < 0)
		out += status_show_not_ready(pdev_nr, out);
	else
		out += status_show_vhci(pdev_nr, out);

	return out - s;
}

static ssize_t nports_show(struct device *dev, struct device_attribute *attr,
			   char *out)
{
	char *s = out;

	/*
	 * Half the ports are for SPEED_HIGH and half for SPEED_SUPER,
	 * thus the * 2.
	 */
	out += sprintf(out, "%d\n", VHCI_PORTS * vhci_num_controllers);
	return out - s;
}
static DEVICE_ATTR_RO(nports);

/* Sysfs entry to shutdown a virtual connection */
static int vhci_port_disconnect(struct vhci_hcd *vhci_hcd, __u32 rhport)
{
	struct vhci_device *vdev = &vhci_hcd->vdev[rhport];
	struct vhci *vhci = vhci_hcd->vhci;
	unsigned long flags;

	usbip_dbg_vhci_sysfs("enter\n");

	/* lock */
	spin_lock_irqsave(&vhci->lock, flags);
	spin_lock(&vdev->ud.lock);

	if (vdev->ud.status == VDEV_ST_NULL) {
		pr_err("not connected %d\n", vdev->ud.status);

		/* unlock */
		spin_unlock(&vdev->ud.lock);
		spin_unlock_irqrestore(&vhci->lock, flags);

		return -EINVAL;
	}

	/* unlock */
	spin_unlock(&vdev->ud.lock);
	spin_unlock_irqrestore(&vhci->lock, flags);

	usbip_event_add(&vdev->ud, VDEV_EVENT_DOWN);

	return 0;
}

static int valid_port(__u32 pdev_nr, __u32 rhport)
{
	if (pdev_nr >= vhci_num_controllers) {
		pr_err("pdev %u\n", pdev_nr);
		return 0;
	}
	if (rhport >= VHCI_HC_PORTS) {
		pr_err("rhport %u\n", rhport);
		return 0;
	}
	return 1;
}

static ssize_t store_detach(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	__u32 port = 0, pdev_nr = 0, rhport = 0;
	struct usb_hcd *hcd;
	struct vhci_hcd *vhci_hcd;
	int ret;

	if (kstrtoint(buf, 10, &port) < 0)
		return -EINVAL;

	pdev_nr = port_to_pdev_nr(port);
	rhport = port_to_rhport(port);

	if (!valid_port(pdev_nr, rhport))
		return -EINVAL;

	hcd = platform_get_drvdata(vhcis[pdev_nr].pdev);
	if (hcd == NULL) {
		dev_err(dev, "port is not ready %u\n", port);
		return -EAGAIN;
	}

	usbip_dbg_vhci_sysfs("rhport %d\n", rhport);

	if ((port / VHCI_HC_PORTS) % 2)
		vhci_hcd = hcd_to_vhci_hcd(hcd)->vhci->vhci_hcd_ss;
	else
		vhci_hcd = hcd_to_vhci_hcd(hcd)->vhci->vhci_hcd_hs;

	ret = vhci_port_disconnect(vhci_hcd, rhport);
	if (ret < 0)
		return -EINVAL;

	usbip_dbg_vhci_sysfs("Leave\n");

	return count;
}
static DEVICE_ATTR(detach, S_IWUSR, NULL, store_detach);

static int valid_args(__u32 pdev_nr, __u32 rhport, enum usb_device_speed speed)
{
	if (!valid_port(pdev_nr, rhport)) {
		return 0;
	}

	switch (speed) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
	case USB_SPEED_HIGH:
	case USB_SPEED_WIRELESS:
	case USB_SPEED_SUPER:
		break;
	default:
		pr_err("Failed attach request for unsupported USB speed: %s\n",
			usb_speed_string(speed));
		return 0;
	}

	return 1;
}

/* Sysfs entry to establish a virtual connection */
/*
 * To start a new USB/IP attachment, a userland program needs to setup a TCP
 * connection and then write its socket descriptor with remote device
 * information into this sysfs file.
 *
 * A remote device is virtually attached to the root-hub port of @rhport with
 * @speed. @devid is embedded into a request to specify the remote device in a
 * server host.
 *
 * write() returns 0 on success, else negative errno.
 */
static ssize_t store_attach(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct socket *socket;
	int sockfd = 0;
	__u32 port = 0, pdev_nr = 0, rhport = 0, devid = 0, speed = 0;
	struct usb_hcd *hcd;
	struct vhci_hcd *vhci_hcd;
	struct vhci_device *vdev;
	struct vhci *vhci;
	int err;
	unsigned long flags;

	/*
	 * @rhport: port number of vhci_hcd
	 * @sockfd: socket descriptor of an established TCP connection
	 * @devid: unique device identifier in a remote host
	 * @speed: usb device speed in a remote host
	 */
	if (sscanf(buf, "%u %u %u %u", &port, &sockfd, &devid, &speed) != 4)
		return -EINVAL;
	pdev_nr = port_to_pdev_nr(port);
	rhport = port_to_rhport(port);

	usbip_dbg_vhci_sysfs("port(%u) pdev(%d) rhport(%u)\n",
			     port, pdev_nr, rhport);
	usbip_dbg_vhci_sysfs("sockfd(%u) devid(%u) speed(%u)\n",
			     sockfd, devid, speed);

	/* check received parameters */
	if (!valid_args(pdev_nr, rhport, speed))
		return -EINVAL;

	hcd = platform_get_drvdata(vhcis[pdev_nr].pdev);
	if (hcd == NULL) {
		dev_err(dev, "port %d is not ready\n", port);
		return -EAGAIN;
	}

	vhci_hcd = hcd_to_vhci_hcd(hcd);
	vhci = vhci_hcd->vhci;

	if (speed == USB_SPEED_SUPER)
		vdev = &vhci->vhci_hcd_ss->vdev[rhport];
	else
		vdev = &vhci->vhci_hcd_hs->vdev[rhport];

	/* Extract socket from fd. */
	socket = sockfd_lookup(sockfd, &err);
	if (!socket)
		return -EINVAL;

	/* now need lock until setting vdev status as used */

	/* begin a lock */
	spin_lock_irqsave(&vhci->lock, flags);
	spin_lock(&vdev->ud.lock);

	if (vdev->ud.status != VDEV_ST_NULL) {
		/* end of the lock */
		spin_unlock(&vdev->ud.lock);
		spin_unlock_irqrestore(&vhci->lock, flags);

		sockfd_put(socket);

		dev_err(dev, "port %d already used\n", rhport);
		/*
		 * Will be retried from userspace
		 * if there's another free port.
		 */
		return -EBUSY;
	}

	dev_info(dev, "pdev(%u) rhport(%u) sockfd(%d)\n",
		 pdev_nr, rhport, sockfd);
	dev_info(dev, "devid(%u) speed(%u) speed_str(%s)\n",
		 devid, speed, usb_speed_string(speed));

	vdev->devid         = devid;
	vdev->speed         = speed;
	vdev->ud.sockfd     = sockfd;
	vdev->ud.tcp_socket = socket;
	vdev->ud.status     = VDEV_ST_NOTASSIGNED;

	spin_unlock(&vdev->ud.lock);
	spin_unlock_irqrestore(&vhci->lock, flags);
	/* end the lock */

	vdev->ud.tcp_rx = kthread_get_run(vhci_rx_loop, &vdev->ud, "vhci_rx");
	vdev->ud.tcp_tx = kthread_get_run(vhci_tx_loop, &vdev->ud, "vhci_tx");

	rh_port_connect(vdev, speed);

	return count;
}
static DEVICE_ATTR(attach, S_IWUSR, NULL, store_attach);

#define MAX_STATUS_NAME 16

struct status_attr {
	struct device_attribute attr;
	char name[MAX_STATUS_NAME+1];
};

static struct status_attr *status_attrs;

static void set_status_attr(int id)
{
	struct status_attr *status;

	status = status_attrs + id;
	if (id == 0)
		strcpy(status->name, "status");
	else
		snprintf(status->name, MAX_STATUS_NAME+1, "status.%d", id);
	status->attr.attr.name = status->name;
	status->attr.attr.mode = S_IRUGO;
	status->attr.show = status_show;
	sysfs_attr_init(&status->attr.attr);
}

static int init_status_attrs(void)
{
	int id;

	status_attrs = kcalloc(vhci_num_controllers, sizeof(struct status_attr),
			       GFP_KERNEL);
	if (status_attrs == NULL)
		return -ENOMEM;

	for (id = 0; id < vhci_num_controllers; id++)
		set_status_attr(id);

	return 0;
}

static void finish_status_attrs(void)
{
	kfree(status_attrs);
}

struct attribute_group vhci_attr_group = {
	.attrs = NULL,
};

int vhci_init_attr_group(void)
{
	struct attribute **attrs;
	int ret, i;

	attrs = kcalloc((vhci_num_controllers + 5), sizeof(struct attribute *),
			GFP_KERNEL);
	if (attrs == NULL)
		return -ENOMEM;

	ret = init_status_attrs();
	if (ret) {
		kfree(attrs);
		return ret;
	}
	*attrs = &dev_attr_nports.attr;
	*(attrs + 1) = &dev_attr_detach.attr;
	*(attrs + 2) = &dev_attr_attach.attr;
	*(attrs + 3) = &dev_attr_usbip_debug.attr;
	for (i = 0; i < vhci_num_controllers; i++)
		*(attrs + i + 4) = &((status_attrs + i)->attr.attr);
	vhci_attr_group.attrs = attrs;
	return 0;
}

void vhci_finish_attr_group(void)
{
	finish_status_attrs();
	kfree(vhci_attr_group.attrs);
}
