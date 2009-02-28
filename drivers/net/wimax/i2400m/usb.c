/*
 * Intel Wireless WiMAX Connection 2400m
 * Linux driver model glue for USB device, reset & fw upload
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 * Yanir Lubetkin <yanirx.lubetkin@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * See i2400m-usb.h for a general description of this driver.
 *
 * This file implements driver model glue, and hook ups for the
 * generic driver to implement the bus-specific functions (device
 * communication setup/tear down, firmware upload and resetting).
 *
 * ROADMAP
 *
 * i2400mu_probe()
 *   alloc_netdev()...
 *     i2400mu_netdev_setup()
 *       i2400mu_init()
 *       i2400m_netdev_setup()
 *   i2400m_setup()...
 *
 * i2400mu_disconnect
 *   i2400m_release()
 *   free_netdev()
 *
 * i2400mu_suspend()
 *   i2400m_cmd_enter_powersave()
 *   i2400mu_notification_release()
 *
 * i2400mu_resume()
 *   i2400mu_notification_setup()
 *
 * i2400mu_bus_dev_start()        Called by i2400m_dev_start() [who is
 *   i2400mu_tx_setup()           called by i2400m_setup()]
 *   i2400mu_rx_setup()
 *   i2400mu_notification_setup()
 *
 * i2400mu_bus_dev_stop()         Called by i2400m_dev_stop() [who is
 *   i2400mu_notification_release()  called by i2400m_release()]
 *   i2400mu_rx_release()
 *   i2400mu_tx_release()
 *
 * i2400mu_bus_reset()            Called by i2400m->bus_reset
 *   __i2400mu_reset()
 *     __i2400mu_send_barker()
 *   usb_reset_device()
 */
#include "i2400m-usb.h"
#include <linux/wimax/i2400m.h>
#include <linux/debugfs.h>


#define D_SUBMODULE usb
#include "usb-debug-levels.h"


/* Our firmware file name */
static const char *i2400mu_bus_fw_names[] = {
#define I2400MU_FW_FILE_NAME_v1_4 "i2400m-fw-usb-1.4.sbcf"
	I2400MU_FW_FILE_NAME_v1_4,
#define I2400MU_FW_FILE_NAME_v1_3 "i2400m-fw-usb-1.3.sbcf"
	I2400MU_FW_FILE_NAME_v1_3,
	NULL,
};


static
int i2400mu_bus_dev_start(struct i2400m *i2400m)
{
	int result;
	struct i2400mu *i2400mu = container_of(i2400m, struct i2400mu, i2400m);
	struct device *dev = &i2400mu->usb_iface->dev;

	d_fnstart(3, dev, "(i2400m %p)\n", i2400m);
	result = i2400mu_tx_setup(i2400mu);
	if (result < 0)
		goto error_usb_tx_setup;
	result = i2400mu_rx_setup(i2400mu);
	if (result < 0)
		goto error_usb_rx_setup;
	result = i2400mu_notification_setup(i2400mu);
	if (result < 0)
		goto error_notif_setup;
	d_fnend(3, dev, "(i2400m %p) = %d\n", i2400m, result);
	return result;

error_notif_setup:
	i2400mu_rx_release(i2400mu);
error_usb_rx_setup:
	i2400mu_tx_release(i2400mu);
error_usb_tx_setup:
	d_fnend(3, dev, "(i2400m %p) = void\n", i2400m);
	return result;
}


static
void i2400mu_bus_dev_stop(struct i2400m *i2400m)
{
	struct i2400mu *i2400mu = container_of(i2400m, struct i2400mu, i2400m);
	struct device *dev = &i2400mu->usb_iface->dev;

	d_fnstart(3, dev, "(i2400m %p)\n", i2400m);
	i2400mu_notification_release(i2400mu);
	i2400mu_rx_release(i2400mu);
	i2400mu_tx_release(i2400mu);
	d_fnend(3, dev, "(i2400m %p) = void\n", i2400m);
}


/*
 * Sends a barker buffer to the device
 *
 * This helper will allocate a kmalloced buffer and use it to transmit
 * (then free it). Reason for this is that other arches cannot use
 * stack/vmalloc/text areas for DMA transfers.
 *
 * Error recovery here is simpler: anything is considered a hard error
 * and will move the reset code to use a last-resort bus-based reset.
 */
static
int __i2400mu_send_barker(struct i2400mu *i2400mu,
			  const __le32 *barker,
			  size_t barker_size,
			  unsigned endpoint)
{
	struct usb_endpoint_descriptor *epd = NULL;
	int pipe, actual_len, ret;
	struct device *dev = &i2400mu->usb_iface->dev;
	void *buffer;
	int do_autopm = 1;

	ret = usb_autopm_get_interface(i2400mu->usb_iface);
	if (ret < 0) {
		dev_err(dev, "RESET: can't get autopm: %d\n", ret);
		do_autopm = 0;
	}
	ret = -ENOMEM;
	buffer = kmalloc(barker_size, GFP_KERNEL);
	if (buffer == NULL)
		goto error_kzalloc;
	epd = usb_get_epd(i2400mu->usb_iface, endpoint);
	pipe = usb_sndbulkpipe(i2400mu->usb_dev, epd->bEndpointAddress);
	memcpy(buffer, barker, barker_size);
	ret = usb_bulk_msg(i2400mu->usb_dev, pipe, buffer, barker_size,
			   &actual_len, HZ);
	if (ret < 0) {
		if (ret != -EINVAL)
			dev_err(dev, "E: barker error: %d\n", ret);
	} else if (actual_len != barker_size) {
		dev_err(dev, "E: only %d bytes transmitted\n", actual_len);
		ret = -EIO;
	}
	kfree(buffer);
error_kzalloc:
	if (do_autopm)
		usb_autopm_put_interface(i2400mu->usb_iface);
	return ret;
}


/*
 * Reset a device at different levels (warm, cold or bus)
 *
 * @i2400m: device descriptor
 * @reset_type: soft, warm or bus reset (I2400M_RT_WARM/SOFT/BUS)
 *
 * Warm and cold resets get a USB reset if they fail.
 *
 * Warm reset:
 *
 * The device will be fully reset internally, but won't be
 * disconnected from the USB bus (so no reenumeration will
 * happen). Firmware upload will be neccessary.
 *
 * The device will send a reboot barker in the notification endpoint
 * that will trigger the driver to reinitialize the state
 * automatically from notif.c:i2400m_notification_grok() into
 * i2400m_dev_bootstrap_delayed().
 *
 * Cold and bus (USB) reset:
 *
 * The device will be fully reset internally, disconnected from the
 * USB bus an a reenumeration will happen. Firmware upload will be
 * neccessary. Thus, we don't do any locking or struct
 * reinitialization, as we are going to be fully disconnected and
 * reenumerated.
 *
 * Note we need to return -ENODEV if a warm reset was requested and we
 * had to resort to a bus reset. See i2400m_op_reset(), wimax_reset()
 * and wimax_dev->op_reset.
 *
 * WARNING: no driver state saved/fixed
 */
static
int i2400mu_bus_reset(struct i2400m *i2400m, enum i2400m_reset_type rt)
{
	int result;
	struct i2400mu *i2400mu =
		container_of(i2400m, struct i2400mu, i2400m);
	struct device *dev = i2400m_dev(i2400m);
	static const __le32 i2400m_WARM_BOOT_BARKER[4] = {
		cpu_to_le32(I2400M_WARM_RESET_BARKER),
		cpu_to_le32(I2400M_WARM_RESET_BARKER),
		cpu_to_le32(I2400M_WARM_RESET_BARKER),
		cpu_to_le32(I2400M_WARM_RESET_BARKER),
	};
	static const __le32 i2400m_COLD_BOOT_BARKER[4] = {
		cpu_to_le32(I2400M_COLD_RESET_BARKER),
		cpu_to_le32(I2400M_COLD_RESET_BARKER),
		cpu_to_le32(I2400M_COLD_RESET_BARKER),
		cpu_to_le32(I2400M_COLD_RESET_BARKER),
	};

	d_fnstart(3, dev, "(i2400m %p rt %u)\n", i2400m, rt);
	if (rt == I2400M_RT_WARM)
		result = __i2400mu_send_barker(i2400mu, i2400m_WARM_BOOT_BARKER,
					       sizeof(i2400m_WARM_BOOT_BARKER),
					       I2400MU_EP_BULK_OUT);
	else if (rt == I2400M_RT_COLD)
		result = __i2400mu_send_barker(i2400mu, i2400m_COLD_BOOT_BARKER,
					       sizeof(i2400m_COLD_BOOT_BARKER),
					       I2400MU_EP_RESET_COLD);
	else if (rt == I2400M_RT_BUS) {
do_bus_reset:
		result = usb_reset_device(i2400mu->usb_dev);
		switch (result) {
		case 0:
		case -EINVAL:	/* device is gone */
		case -ENODEV:
		case -ENOENT:
		case -ESHUTDOWN:
			result = rt == I2400M_RT_WARM ? -ENODEV : 0;
			break;	/* We assume the device is disconnected */
		default:
			dev_err(dev, "USB reset failed (%d), giving up!\n",
				result);
		}
	} else
		BUG();
	if (result < 0
	    && result != -EINVAL	/* device is gone */
	    && rt != I2400M_RT_BUS) {
		dev_err(dev, "%s reset failed (%d); trying USB reset\n",
			rt == I2400M_RT_WARM ? "warm" : "cold", result);
		rt = I2400M_RT_BUS;
		goto do_bus_reset;
	}
	d_fnend(3, dev, "(i2400m %p rt %u) = %d\n", i2400m, rt, result);
	return result;
}


static
void i2400mu_netdev_setup(struct net_device *net_dev)
{
	struct i2400m *i2400m = net_dev_to_i2400m(net_dev);
	struct i2400mu *i2400mu = container_of(i2400m, struct i2400mu, i2400m);
	i2400mu_init(i2400mu);
	i2400m_netdev_setup(net_dev);
}


/*
 * Debug levels control; see debug.h
 */
struct d_level D_LEVEL[] = {
	D_SUBMODULE_DEFINE(usb),
	D_SUBMODULE_DEFINE(fw),
	D_SUBMODULE_DEFINE(notif),
	D_SUBMODULE_DEFINE(rx),
	D_SUBMODULE_DEFINE(tx),
};
size_t D_LEVEL_SIZE = ARRAY_SIZE(D_LEVEL);


#define __debugfs_register(prefix, name, parent)			\
do {									\
	result = d_level_register_debugfs(prefix, name, parent);	\
	if (result < 0)							\
		goto error;						\
} while (0)


static
int i2400mu_debugfs_add(struct i2400mu *i2400mu)
{
	int result;
	struct device *dev = &i2400mu->usb_iface->dev;
	struct dentry *dentry = i2400mu->i2400m.wimax_dev.debugfs_dentry;
	struct dentry *fd;

	dentry = debugfs_create_dir("i2400m-usb", dentry);
	result = PTR_ERR(dentry);
	if (IS_ERR(dentry)) {
		if (result == -ENODEV)
			result = 0;	/* No debugfs support */
		goto error;
	}
	i2400mu->debugfs_dentry = dentry;
	__debugfs_register("dl_", usb, dentry);
	__debugfs_register("dl_", fw, dentry);
	__debugfs_register("dl_", notif, dentry);
	__debugfs_register("dl_", rx, dentry);
	__debugfs_register("dl_", tx, dentry);

	/* Don't touch these if you don't know what you are doing */
	fd = debugfs_create_u8("rx_size_auto_shrink", 0600, dentry,
			       &i2400mu->rx_size_auto_shrink);
	result = PTR_ERR(fd);
	if (IS_ERR(fd) && result != -ENODEV) {
		dev_err(dev, "Can't create debugfs entry "
			"rx_size_auto_shrink: %d\n", result);
		goto error;
	}

	fd = debugfs_create_size_t("rx_size", 0600, dentry,
				   &i2400mu->rx_size);
	result = PTR_ERR(fd);
	if (IS_ERR(fd) && result != -ENODEV) {
		dev_err(dev, "Can't create debugfs entry "
			"rx_size: %d\n", result);
		goto error;
	}

	return 0;

error:
	debugfs_remove_recursive(i2400mu->debugfs_dentry);
	return result;
}


/*
 * Probe a i2400m interface and register it
 *
 * @iface:   USB interface to link to
 * @id:      USB class/subclass/protocol id
 * @returns: 0 if ok, < 0 errno code on error.
 *
 * Alloc a net device, initialize the bus-specific details and then
 * calls the bus-generic initialization routine. That will register
 * the wimax and netdev devices, upload the firmware [using
 * _bus_bm_*()], call _bus_dev_start() to finalize the setup of the
 * communication with the device and then will start to talk to it to
 * finnish setting it up.
 */
static
int i2400mu_probe(struct usb_interface *iface,
		  const struct usb_device_id *id)
{
	int result;
	struct net_device *net_dev;
	struct device *dev = &iface->dev;
	struct i2400m *i2400m;
	struct i2400mu *i2400mu;
	struct usb_device *usb_dev = interface_to_usbdev(iface);

	if (usb_dev->speed != USB_SPEED_HIGH)
		dev_err(dev, "device not connected as high speed\n");

	/* Allocate instance [calls i2400m_netdev_setup() on it]. */
	result = -ENOMEM;
	net_dev = alloc_netdev(sizeof(*i2400mu), "wmx%d",
			       i2400mu_netdev_setup);
	if (net_dev == NULL) {
		dev_err(dev, "no memory for network device instance\n");
		goto error_alloc_netdev;
	}
	SET_NETDEV_DEV(net_dev, dev);
	i2400m = net_dev_to_i2400m(net_dev);
	i2400mu = container_of(i2400m, struct i2400mu, i2400m);
	i2400m->wimax_dev.net_dev = net_dev;
	i2400mu->usb_dev = usb_get_dev(usb_dev);
	i2400mu->usb_iface = iface;
	usb_set_intfdata(iface, i2400mu);

	i2400m->bus_tx_block_size = I2400MU_BLK_SIZE;
	i2400m->bus_pl_size_max = I2400MU_PL_SIZE_MAX;
	i2400m->bus_dev_start = i2400mu_bus_dev_start;
	i2400m->bus_dev_stop = i2400mu_bus_dev_stop;
	i2400m->bus_tx_kick = i2400mu_bus_tx_kick;
	i2400m->bus_reset = i2400mu_bus_reset;
	i2400m->bus_bm_cmd_send = i2400mu_bus_bm_cmd_send;
	i2400m->bus_bm_wait_for_ack = i2400mu_bus_bm_wait_for_ack;
	i2400m->bus_fw_names = i2400mu_bus_fw_names;
	i2400m->bus_bm_mac_addr_impaired = 0;

#ifdef CONFIG_PM
	iface->needs_remote_wakeup = 1;		/* autosuspend (15s delay) */
	device_init_wakeup(dev, 1);
	usb_autopm_enable(i2400mu->usb_iface);
	usb_dev->autosuspend_delay = 15 * HZ;
	usb_dev->autosuspend_disabled = 0;
#endif

	result = i2400m_setup(i2400m, I2400M_BRI_MAC_REINIT);
	if (result < 0) {
		dev_err(dev, "cannot setup device: %d\n", result);
		goto error_setup;
	}
	result = i2400mu_debugfs_add(i2400mu);
	if (result < 0) {
		dev_err(dev, "Can't register i2400mu's debugfs: %d\n", result);
		goto error_debugfs_add;
	}
	return 0;

error_debugfs_add:
	i2400m_release(i2400m);
error_setup:
	usb_set_intfdata(iface, NULL);
	usb_put_dev(i2400mu->usb_dev);
	free_netdev(net_dev);
error_alloc_netdev:
	return result;
}


/*
 * Disconect a i2400m from the system.
 *
 * i2400m_stop() has been called before, so al the rx and tx contexts
 * have been taken down already. Make sure the queue is stopped,
 * unregister netdev and i2400m, free and kill.
 */
static
void i2400mu_disconnect(struct usb_interface *iface)
{
	struct i2400mu *i2400mu = usb_get_intfdata(iface);
	struct i2400m *i2400m = &i2400mu->i2400m;
	struct net_device *net_dev = i2400m->wimax_dev.net_dev;
	struct device *dev = &iface->dev;

	d_fnstart(3, dev, "(iface %p i2400m %p)\n", iface, i2400m);

	debugfs_remove_recursive(i2400mu->debugfs_dentry);
	i2400m_release(i2400m);
	usb_set_intfdata(iface, NULL);
	usb_put_dev(i2400mu->usb_dev);
	free_netdev(net_dev);
	d_fnend(3, dev, "(iface %p i2400m %p) = void\n", iface, i2400m);
}


/*
 * Get the device ready for USB port or system standby and hibernation
 *
 * USB port and system standby are handled the same.
 *
 * When the system hibernates, the USB device is powered down and then
 * up, so we don't really have to do much here, as it will be seen as
 * a reconnect. Still for simplicity we consider this case the same as
 * suspend, so that the device has a chance to do notify the base
 * station (if connected).
 *
 * So at the end, the three cases require common handling.
 *
 * If at the time of this call the device's firmware is not loaded,
 * nothing has to be done.
 *
 * If the firmware is loaded, we need to:
 *
 *  - tell the device to go into host interface power save mode, wait
 *    for it to ack
 *
 *    This is quite more interesting than it is; we need to execute a
 *    command, but this time, we don't want the code in usb-{tx,rx}.c
 *    to call the usb_autopm_get/put_interface() barriers as it'd
 *    deadlock, so we need to decrement i2400mu->do_autopm, that acts
 *    as a poor man's semaphore. Ugly, but it works.
 *
 *    As well, the device might refuse going to sleep for whichever
 *    reason. In this case we just fail. For system suspend/hibernate,
 *    we *can't* fail. We look at usb_dev->auto_pm to see if the
 *    suspend call comes from the USB stack or from the system and act
 *    in consequence.
 *
 *  - stop the notification endpoint polling
 */
static
int i2400mu_suspend(struct usb_interface *iface, pm_message_t pm_msg)
{
	int result = 0;
	struct device *dev = &iface->dev;
	struct i2400mu *i2400mu = usb_get_intfdata(iface);
#ifdef CONFIG_PM
	struct usb_device *usb_dev = i2400mu->usb_dev;
#endif
	struct i2400m *i2400m = &i2400mu->i2400m;

	d_fnstart(3, dev, "(iface %p pm_msg %u)\n", iface, pm_msg.event);
	if (i2400m->updown == 0)
		goto no_firmware;
	d_printf(1, dev, "fw up, requesting standby\n");
	atomic_dec(&i2400mu->do_autopm);
	result = i2400m_cmd_enter_powersave(i2400m);
	atomic_inc(&i2400mu->do_autopm);
#ifdef CONFIG_PM
	if (result < 0 && usb_dev->auto_pm == 0) {
		/* System suspend, can't fail */
		dev_err(dev, "failed to suspend, will reset on resume\n");
		result = 0;
	}
#endif
	if (result < 0)
		goto error_enter_powersave;
	i2400mu_notification_release(i2400mu);
	d_printf(1, dev, "fw up, got standby\n");
error_enter_powersave:
no_firmware:
	d_fnend(3, dev, "(iface %p pm_msg %u) = %d\n",
		iface, pm_msg.event, result);
	return result;
}


static
int i2400mu_resume(struct usb_interface *iface)
{
	int ret = 0;
	struct device *dev = &iface->dev;
	struct i2400mu *i2400mu = usb_get_intfdata(iface);
	struct i2400m *i2400m = &i2400mu->i2400m;

	d_fnstart(3, dev, "(iface %p)\n", iface);
	if (i2400m->updown == 0) {
		d_printf(1, dev, "fw was down, no resume neeed\n");
		goto out;
	}
	d_printf(1, dev, "fw was up, resuming\n");
	i2400mu_notification_setup(i2400mu);
	/* USB has flow control, so we don't need to give it time to
	 * come back; otherwise, we'd use something like a get-state
	 * command... */
out:
	d_fnend(3, dev, "(iface %p) = %d\n", iface, ret);
	return ret;
}


static
struct usb_device_id i2400mu_id_table[] = {
	{ USB_DEVICE(0x8086, 0x0181) },
	{ USB_DEVICE(0x8086, 0x1403) },
	{ USB_DEVICE(0x8086, 0x1405) },
	{ USB_DEVICE(0x8086, 0x0180) },
	{ USB_DEVICE(0x8086, 0x0182) },
	{ USB_DEVICE(0x8086, 0x1406) },
	{ USB_DEVICE(0x8086, 0x1403) },
	{ },
};
MODULE_DEVICE_TABLE(usb, i2400mu_id_table);


static
struct usb_driver i2400mu_driver = {
	.name = KBUILD_MODNAME,
	.suspend = i2400mu_suspend,
	.resume = i2400mu_resume,
	.probe = i2400mu_probe,
	.disconnect = i2400mu_disconnect,
	.id_table = i2400mu_id_table,
	.supports_autosuspend = 1,
};

static
int __init i2400mu_driver_init(void)
{
	return usb_register(&i2400mu_driver);
}
module_init(i2400mu_driver_init);


static
void __exit i2400mu_driver_exit(void)
{
	flush_scheduled_work();	/* for the stuff we schedule from sysfs.c */
	usb_deregister(&i2400mu_driver);
}
module_exit(i2400mu_driver_exit);

MODULE_AUTHOR("Intel Corporation <linux-wimax@intel.com>");
MODULE_DESCRIPTION("Intel 2400M WiMAX networking for USB");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(I2400MU_FW_FILE_NAME_v1_4);
MODULE_FIRMWARE(I2400MU_FW_FILE_NAME_v1_3);
