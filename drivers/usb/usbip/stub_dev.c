// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2003-2008 Takahiro Hirofuchi
 */

#include <linux/device.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/module.h>

#include "usbip_common.h"
#include "stub.h"

/*
 * usbip_status shows the status of usbip-host as long as this driver is bound
 * to the target device.
 */
static ssize_t usbip_status_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct stub_device *sdev = dev_get_drvdata(dev);
	int status;

	if (!sdev) {
		dev_err(dev, "sdev is null\n");
		return -ENODEV;
	}

	spin_lock_irq(&sdev->ud.lock);
	status = sdev->ud.status;
	spin_unlock_irq(&sdev->ud.lock);

	return snprintf(buf, PAGE_SIZE, "%d\n", status);
}
static DEVICE_ATTR_RO(usbip_status);

/*
 * usbip_sockfd gets a socket descriptor of an established TCP connection that
 * is used to transfer usbip requests by kernel threads. -1 is a magic number
 * by which usbip connection is finished.
 */
static ssize_t store_sockfd(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct stub_device *sdev = dev_get_drvdata(dev);
	int sockfd = 0;
	struct socket *socket;
	int rv;

	if (!sdev) {
		dev_err(dev, "sdev is null\n");
		return -ENODEV;
	}

	rv = sscanf(buf, "%d", &sockfd);
	if (rv != 1)
		return -EINVAL;

	if (sockfd != -1) {
		int err;

		dev_info(dev, "stub up\n");

		spin_lock_irq(&sdev->ud.lock);

		if (sdev->ud.status != SDEV_ST_AVAILABLE) {
			dev_err(dev, "not ready\n");
			goto err;
		}

		socket = sockfd_lookup(sockfd, &err);
		if (!socket)
			goto err;

		sdev->ud.tcp_socket = socket;

		spin_unlock_irq(&sdev->ud.lock);

		sdev->ud.tcp_rx = kthread_get_run(stub_rx_loop, &sdev->ud,
						  "stub_rx");
		sdev->ud.tcp_tx = kthread_get_run(stub_tx_loop, &sdev->ud,
						  "stub_tx");

		spin_lock_irq(&sdev->ud.lock);
		sdev->ud.status = SDEV_ST_USED;
		spin_unlock_irq(&sdev->ud.lock);

	} else {
		dev_info(dev, "stub down\n");

		spin_lock_irq(&sdev->ud.lock);
		if (sdev->ud.status != SDEV_ST_USED)
			goto err;

		spin_unlock_irq(&sdev->ud.lock);

		usbip_event_add(&sdev->ud, SDEV_EVENT_DOWN);
	}

	return count;

err:
	spin_unlock_irq(&sdev->ud.lock);
	return -EINVAL;
}
static DEVICE_ATTR(usbip_sockfd, S_IWUSR, NULL, store_sockfd);

static int stub_add_files(struct device *dev)
{
	int err = 0;

	err = device_create_file(dev, &dev_attr_usbip_status);
	if (err)
		goto err_status;

	err = device_create_file(dev, &dev_attr_usbip_sockfd);
	if (err)
		goto err_sockfd;

	err = device_create_file(dev, &dev_attr_usbip_debug);
	if (err)
		goto err_debug;

	return 0;

err_debug:
	device_remove_file(dev, &dev_attr_usbip_sockfd);
err_sockfd:
	device_remove_file(dev, &dev_attr_usbip_status);
err_status:
	return err;
}

static void stub_remove_files(struct device *dev)
{
	device_remove_file(dev, &dev_attr_usbip_status);
	device_remove_file(dev, &dev_attr_usbip_sockfd);
	device_remove_file(dev, &dev_attr_usbip_debug);
}

static void stub_shutdown_connection(struct usbip_device *ud)
{
	struct stub_device *sdev = container_of(ud, struct stub_device, ud);

	/*
	 * When removing an exported device, kernel panic sometimes occurred
	 * and then EIP was sk_wait_data of stub_rx thread. Is this because
	 * sk_wait_data returned though stub_rx thread was already finished by
	 * step 1?
	 */
	if (ud->tcp_socket) {
		dev_dbg(&sdev->udev->dev, "shutdown sockfd %d\n", ud->sockfd);
		kernel_sock_shutdown(ud->tcp_socket, SHUT_RDWR);
	}

	/* 1. stop threads */
	if (ud->tcp_rx) {
		kthread_stop_put(ud->tcp_rx);
		ud->tcp_rx = NULL;
	}
	if (ud->tcp_tx) {
		kthread_stop_put(ud->tcp_tx);
		ud->tcp_tx = NULL;
	}

	/*
	 * 2. close the socket
	 *
	 * tcp_socket is freed after threads are killed so that usbip_xmit does
	 * not touch NULL socket.
	 */
	if (ud->tcp_socket) {
		sockfd_put(ud->tcp_socket);
		ud->tcp_socket = NULL;
	}

	/* 3. free used data */
	stub_device_cleanup_urbs(sdev);

	/* 4. free stub_unlink */
	{
		unsigned long flags;
		struct stub_unlink *unlink, *tmp;

		spin_lock_irqsave(&sdev->priv_lock, flags);
		list_for_each_entry_safe(unlink, tmp, &sdev->unlink_tx, list) {
			list_del(&unlink->list);
			kfree(unlink);
		}
		list_for_each_entry_safe(unlink, tmp, &sdev->unlink_free,
					 list) {
			list_del(&unlink->list);
			kfree(unlink);
		}
		spin_unlock_irqrestore(&sdev->priv_lock, flags);
	}
}

static void stub_device_reset(struct usbip_device *ud)
{
	struct stub_device *sdev = container_of(ud, struct stub_device, ud);
	struct usb_device *udev = sdev->udev;
	int ret;

	dev_dbg(&udev->dev, "device reset");

	ret = usb_lock_device_for_reset(udev, NULL);
	if (ret < 0) {
		dev_err(&udev->dev, "lock for reset\n");
		spin_lock_irq(&ud->lock);
		ud->status = SDEV_ST_ERROR;
		spin_unlock_irq(&ud->lock);
		return;
	}

	/* try to reset the device */
	ret = usb_reset_device(udev);
	usb_unlock_device(udev);

	spin_lock_irq(&ud->lock);
	if (ret) {
		dev_err(&udev->dev, "device reset\n");
		ud->status = SDEV_ST_ERROR;
	} else {
		dev_info(&udev->dev, "device reset\n");
		ud->status = SDEV_ST_AVAILABLE;
	}
	spin_unlock_irq(&ud->lock);
}

static void stub_device_unusable(struct usbip_device *ud)
{
	spin_lock_irq(&ud->lock);
	ud->status = SDEV_ST_ERROR;
	spin_unlock_irq(&ud->lock);
}

/**
 * stub_device_alloc - allocate a new stub_device struct
 * @udev: usb_device of a new device
 *
 * Allocates and initializes a new stub_device struct.
 */
static struct stub_device *stub_device_alloc(struct usb_device *udev)
{
	struct stub_device *sdev;
	int busnum = udev->bus->busnum;
	int devnum = udev->devnum;

	dev_dbg(&udev->dev, "allocating stub device");

	/* yes, it's a new device */
	sdev = kzalloc(sizeof(struct stub_device), GFP_KERNEL);
	if (!sdev)
		return NULL;

	sdev->udev = usb_get_dev(udev);

	/*
	 * devid is defined with devnum when this driver is first allocated.
	 * devnum may change later if a device is reset. However, devid never
	 * changes during a usbip connection.
	 */
	sdev->devid		= (busnum << 16) | devnum;
	sdev->ud.side		= USBIP_STUB;
	sdev->ud.status		= SDEV_ST_AVAILABLE;
	spin_lock_init(&sdev->ud.lock);
	sdev->ud.tcp_socket	= NULL;

	INIT_LIST_HEAD(&sdev->priv_init);
	INIT_LIST_HEAD(&sdev->priv_tx);
	INIT_LIST_HEAD(&sdev->priv_free);
	INIT_LIST_HEAD(&sdev->unlink_free);
	INIT_LIST_HEAD(&sdev->unlink_tx);
	spin_lock_init(&sdev->priv_lock);

	init_waitqueue_head(&sdev->tx_waitq);

	sdev->ud.eh_ops.shutdown = stub_shutdown_connection;
	sdev->ud.eh_ops.reset    = stub_device_reset;
	sdev->ud.eh_ops.unusable = stub_device_unusable;

	usbip_start_eh(&sdev->ud);

	dev_dbg(&udev->dev, "register new device\n");

	return sdev;
}

static void stub_device_free(struct stub_device *sdev)
{
	kfree(sdev);
}

static int stub_probe(struct usb_device *udev)
{
	struct stub_device *sdev = NULL;
	const char *udev_busid = dev_name(&udev->dev);
	struct bus_id_priv *busid_priv;
	int rc;

	dev_dbg(&udev->dev, "Enter\n");

	/* check we should claim or not by busid_table */
	busid_priv = get_busid_priv(udev_busid);
	if (!busid_priv || (busid_priv->status == STUB_BUSID_REMOV) ||
	    (busid_priv->status == STUB_BUSID_OTHER)) {
		dev_info(&udev->dev,
			"%s is not in match_busid table... skip!\n",
			udev_busid);

		/*
		 * Return value should be ENODEV or ENOXIO to continue trying
		 * other matched drivers by the driver core.
		 * See driver_probe_device() in driver/base/dd.c
		 */
		return -ENODEV;
	}

	if (udev->descriptor.bDeviceClass == USB_CLASS_HUB) {
		dev_dbg(&udev->dev, "%s is a usb hub device... skip!\n",
			 udev_busid);
		return -ENODEV;
	}

	if (!strcmp(udev->bus->bus_name, "vhci_hcd")) {
		dev_dbg(&udev->dev,
			"%s is attached on vhci_hcd... skip!\n",
			udev_busid);

		return -ENODEV;
	}

	/* ok, this is my device */
	sdev = stub_device_alloc(udev);
	if (!sdev)
		return -ENOMEM;

	dev_info(&udev->dev,
		"usbip-host: register new device (bus %u dev %u)\n",
		udev->bus->busnum, udev->devnum);

	busid_priv->shutdown_busid = 0;

	/* set private data to usb_device */
	dev_set_drvdata(&udev->dev, sdev);
	busid_priv->sdev = sdev;
	busid_priv->udev = udev;

	/*
	 * Claim this hub port.
	 * It doesn't matter what value we pass as owner
	 * (struct dev_state) as long as it is unique.
	 */
	rc = usb_hub_claim_port(udev->parent, udev->portnum,
			(struct usb_dev_state *) udev);
	if (rc) {
		dev_dbg(&udev->dev, "unable to claim port\n");
		goto err_port;
	}

	rc = stub_add_files(&udev->dev);
	if (rc) {
		dev_err(&udev->dev, "stub_add_files for %s\n", udev_busid);
		goto err_files;
	}
	busid_priv->status = STUB_BUSID_ALLOC;

	return 0;
err_files:
	usb_hub_release_port(udev->parent, udev->portnum,
			     (struct usb_dev_state *) udev);
err_port:
	dev_set_drvdata(&udev->dev, NULL);
	usb_put_dev(udev);

	busid_priv->sdev = NULL;
	stub_device_free(sdev);
	return rc;
}

static void shutdown_busid(struct bus_id_priv *busid_priv)
{
	if (busid_priv->sdev && !busid_priv->shutdown_busid) {
		busid_priv->shutdown_busid = 1;
		usbip_event_add(&busid_priv->sdev->ud, SDEV_EVENT_REMOVED);

		/* wait for the stop of the event handler */
		usbip_stop_eh(&busid_priv->sdev->ud);
	}
}

/*
 * called in usb_disconnect() or usb_deregister()
 * but only if actconfig(active configuration) exists
 */
static void stub_disconnect(struct usb_device *udev)
{
	struct stub_device *sdev;
	const char *udev_busid = dev_name(&udev->dev);
	struct bus_id_priv *busid_priv;
	int rc;

	dev_dbg(&udev->dev, "Enter\n");

	busid_priv = get_busid_priv(udev_busid);
	if (!busid_priv) {
		BUG();
		return;
	}

	sdev = dev_get_drvdata(&udev->dev);

	/* get stub_device */
	if (!sdev) {
		dev_err(&udev->dev, "could not get device");
		return;
	}

	dev_set_drvdata(&udev->dev, NULL);

	/*
	 * NOTE: rx/tx threads are invoked for each usb_device.
	 */
	stub_remove_files(&udev->dev);

	/* release port */
	rc = usb_hub_release_port(udev->parent, udev->portnum,
				  (struct usb_dev_state *) udev);
	if (rc) {
		dev_dbg(&udev->dev, "unable to release port\n");
		return;
	}

	/* If usb reset is called from event handler */
	if (usbip_in_eh(current))
		return;

	/* shutdown the current connection */
	shutdown_busid(busid_priv);

	usb_put_dev(sdev->udev);

	/* free sdev */
	busid_priv->sdev = NULL;
	stub_device_free(sdev);

	if (busid_priv->status == STUB_BUSID_ALLOC) {
		busid_priv->status = STUB_BUSID_ADDED;
	} else {
		busid_priv->status = STUB_BUSID_OTHER;
		del_match_busid((char *)udev_busid);
	}
}

#ifdef CONFIG_PM

/* These functions need usb_port_suspend and usb_port_resume,
 * which reside in drivers/usb/core/usb.h. Skip for now. */

static int stub_suspend(struct usb_device *udev, pm_message_t message)
{
	dev_dbg(&udev->dev, "stub_suspend\n");

	return 0;
}

static int stub_resume(struct usb_device *udev, pm_message_t message)
{
	dev_dbg(&udev->dev, "stub_resume\n");

	return 0;
}

#endif	/* CONFIG_PM */

struct usb_device_driver stub_driver = {
	.name		= "usbip-host",
	.probe		= stub_probe,
	.disconnect	= stub_disconnect,
#ifdef CONFIG_PM
	.suspend	= stub_suspend,
	.resume		= stub_resume,
#endif
	.supports_autosuspend	=	0,
};
