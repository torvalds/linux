/*
 * device driver for Telegent tlg2300 based TV cards
 *
 * Author :
 * 	Kang Yong	<kangyong@telegent.com>
 * 	Zhang Xiaobing	<xbzhang@telegent.com>
 * 	Huang Shijie	<zyziii@telegent.com> or <shijie8@gmail.com>
 *
 *	(c) 2009 Telegent Systems
 *	(c) 2010 Telegent Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/suspend.h>
#include <linux/usb/quirks.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/firmware.h>

#include "vendorcmds.h"
#include "pd-common.h"

#define VENDOR_ID	0x1B24
#define PRODUCT_ID	0x4001
static struct usb_device_id id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(VENDOR_ID, PRODUCT_ID, 255, 1, 0) },
	{ USB_DEVICE_AND_INTERFACE_INFO(VENDOR_ID, PRODUCT_ID, 255, 1, 1) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

int debug_mode;
module_param(debug_mode, int, 0644);
MODULE_PARM_DESC(debug_mode, "0 = disable, 1 = enable, 2 = verbose");

static const char *firmware_name = "tlg2300_firmware.bin";
static struct usb_driver poseidon_driver;
static LIST_HEAD(pd_device_list);

/*
 * send set request to USB firmware.
 */
s32 send_set_req(struct poseidon *pd, u8 cmdid, s32 param, s32 *cmd_status)
{
	s32 ret;
	s8  data[32] = {};
	u16 lower_16, upper_16;

	if (pd->state & POSEIDON_STATE_DISCONNECT)
		return -ENODEV;

	mdelay(30);

	if (param == 0) {
		upper_16 = lower_16 = 0;
	} else {
		/* send 32 bit param as  two 16 bit param,little endian */
		lower_16 = (unsigned short)(param & 0xffff);
		upper_16 = (unsigned short)((param >> 16) & 0xffff);
	}
	ret = usb_control_msg(pd->udev,
			 usb_rcvctrlpipe(pd->udev, 0),
			 REQ_SET_CMD | cmdid,
			 USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			 lower_16,
			 upper_16,
			 &data,
			 sizeof(*cmd_status),
			 USB_CTRL_GET_TIMEOUT);

	if (!ret) {
		return -ENXIO;
	} else {
		/*  1st 4 bytes into cmd_status   */
		memcpy((char *)cmd_status, &(data[0]), sizeof(*cmd_status));
	}
	return 0;
}

/*
 * send get request to Poseidon firmware.
 */
s32 send_get_req(struct poseidon *pd, u8 cmdid, s32 param,
			void *buf, s32 *cmd_status, s32 datalen)
{
	s32 ret;
	s8 data[128] = {};
	u16 lower_16, upper_16;

	if (pd->state & POSEIDON_STATE_DISCONNECT)
		return -ENODEV;

	mdelay(30);
	if (param == 0) {
		upper_16 = lower_16 = 0;
	} else {
		/*send 32 bit param as two 16 bit param, little endian */
		lower_16 = (unsigned short)(param & 0xffff);
		upper_16 = (unsigned short)((param >> 16) & 0xffff);
	}
	ret = usb_control_msg(pd->udev,
			 usb_rcvctrlpipe(pd->udev, 0),
			 REQ_GET_CMD | cmdid,
			 USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			 lower_16,
			 upper_16,
			 &data,
			 (datalen + sizeof(*cmd_status)),
			 USB_CTRL_GET_TIMEOUT);

	if (ret < 0) {
		return -ENXIO;
	} else {
		/* 1st 4 bytes into cmd_status, remaining data into cmd_data */
		memcpy((char *)cmd_status, &data[0], sizeof(*cmd_status));
		memcpy((char *)buf, &data[sizeof(*cmd_status)], datalen);
	}
	return 0;
}

static int pm_notifier_block(struct notifier_block *nb,
				unsigned long event, void *dummy)
{
	struct poseidon *pd = NULL;
	struct list_head *node, *next;

	switch (event) {
	case PM_POST_HIBERNATION:
		list_for_each_safe(node, next, &pd_device_list) {
			struct usb_device *udev;
			struct usb_interface *iface;
			int rc = 0;

			pd = container_of(node, struct poseidon, device_list);
			udev = pd->udev;
			iface = pd->interface;

			/* It will cause the system to reload the firmware */
			rc = usb_lock_device_for_reset(udev, iface);
			if (rc >= 0) {
				usb_reset_device(udev);
				usb_unlock_device(udev);
			}
		}
		break;
	default:
		break;
	}
	log("event :%ld\n", event);
	return 0;
}

static struct notifier_block pm_notifer = {
	.notifier_call = pm_notifier_block,
};

int set_tuner_mode(struct poseidon *pd, unsigned char mode)
{
	s32 ret, cmd_status;

	if (pd->state & POSEIDON_STATE_DISCONNECT)
		return -ENODEV;

	ret = send_set_req(pd, TUNE_MODE_SELECT, mode, &cmd_status);
	if (ret || cmd_status)
		return -ENXIO;
	return 0;
}

void poseidon_delete(struct kref *kref)
{
	struct poseidon *pd = container_of(kref, struct poseidon, kref);

	if (!pd)
		return;
	list_del_init(&pd->device_list);

	pd_dvb_usb_device_cleanup(pd);
	/* clean_audio_data(&pd->audio_data);*/

	if (pd->udev) {
		usb_put_dev(pd->udev);
		pd->udev = NULL;
	}
	if (pd->interface) {
		usb_put_intf(pd->interface);
		pd->interface = NULL;
	}
	kfree(pd);
	log();
}

static int firmware_download(struct usb_device *udev)
{
	int ret = 0, actual_length;
	const struct firmware *fw = NULL;
	void *fwbuf = NULL;
	size_t fwlength = 0, offset;
	size_t max_packet_size;

	ret = request_firmware(&fw, firmware_name, &udev->dev);
	if (ret) {
		log("download err : %d", ret);
		return ret;
	}

	fwlength = fw->size;

	fwbuf = kmemdup(fw->data, fwlength, GFP_KERNEL);
	if (!fwbuf) {
		ret = -ENOMEM;
		goto out;
	}

	max_packet_size = udev->ep_out[0x1]->desc.wMaxPacketSize;
	log("\t\t download size : %d", (int)max_packet_size);

	for (offset = 0; offset < fwlength; offset += max_packet_size) {
		actual_length = 0;
		ret = usb_bulk_msg(udev,
				usb_sndbulkpipe(udev, 0x01), /* ep 1 */
				fwbuf + offset,
				min(max_packet_size, fwlength - offset),
				&actual_length,
				HZ * 10);
		if (ret)
			break;
	}
	kfree(fwbuf);
out:
	release_firmware(fw);
	return ret;
}

static inline struct poseidon *get_pd(struct usb_interface *intf)
{
	return usb_get_intfdata(intf);
}

#ifdef CONFIG_PM
/* one-to-one map : poseidon{} <----> usb_device{}'s port */
static inline void set_map_flags(struct poseidon *pd, struct usb_device *udev)
{
	pd->portnum = udev->portnum;
}

static inline int get_autopm_ref(struct poseidon *pd)
{
	return  pd->video_data.users + pd->vbi_data.users + pd->audio.users
		+ atomic_read(&pd->dvb_data.users) + pd->radio_data.users;
}

/* fixup something for poseidon */
static inline struct poseidon *fixup(struct poseidon *pd)
{
	int count;

	/* old udev and interface have gone, so put back reference . */
	count = get_autopm_ref(pd);
	log("count : %d, ref count : %d", count, get_pm_count(pd));
	while (count--)
		usb_autopm_put_interface(pd->interface);
	/*usb_autopm_set_interface(pd->interface); */

	usb_put_dev(pd->udev);
	usb_put_intf(pd->interface);
	log("event : %d\n", pd->msg.event);
	return pd;
}

static struct poseidon *find_old_poseidon(struct usb_device *udev)
{
	struct poseidon *pd;

	list_for_each_entry(pd, &pd_device_list, device_list) {
		if (pd->portnum == udev->portnum && in_hibernation(pd))
			return fixup(pd);
	}
	return NULL;
}

/* Is the card working now ? */
static inline int is_working(struct poseidon *pd)
{
	return get_pm_count(pd) > 0;
}

static int poseidon_suspend(struct usb_interface *intf, pm_message_t msg)
{
	struct poseidon *pd = get_pd(intf);

	if (!pd)
		return 0;
	if (!is_working(pd)) {
		if (get_pm_count(pd) <= 0 && !in_hibernation(pd)) {
			pd->msg.event = PM_EVENT_AUTO_SUSPEND;
			pd->pm_resume = NULL; /*  a good guard */
			printk(KERN_DEBUG "\n\t+ TLG2300 auto suspend +\n\n");
		}
		return 0;
	}
	pd->msg = msg; /* save it here */
	logpm(pd);
	return pd->pm_suspend ? pd->pm_suspend(pd) : 0;
}

static int poseidon_resume(struct usb_interface *intf)
{
	struct poseidon *pd = get_pd(intf);

	if (!pd)
		return 0;
	printk(KERN_DEBUG "\n\t ++ TLG2300 resume ++\n\n");

	if (!is_working(pd)) {
		if (PM_EVENT_AUTO_SUSPEND == pd->msg.event)
			pd->msg = PMSG_ON;
		return 0;
	}
	if (in_hibernation(pd)) {
		logpm(pd);
		return 0;
	}
	logpm(pd);
	return pd->pm_resume ? pd->pm_resume(pd) : 0;
}

static void hibernation_resume(struct work_struct *w)
{
	struct poseidon *pd = container_of(w, struct poseidon, pm_work);
	int count;

	pd->msg.event = 0; /* clear it here */
	pd->state &= ~POSEIDON_STATE_DISCONNECT;

	/* set the new interface's reference */
	count = get_autopm_ref(pd);
	while (count--)
		usb_autopm_get_interface(pd->interface);

	/* resume the context */
	logpm(pd);
	if (pd->pm_resume)
		pd->pm_resume(pd);
}
#else /* CONFIG_PM is not enabled: */
static inline struct poseidon *find_old_poseidon(struct usb_device *udev)
{
	return NULL;
}

static inline void set_map_flags(struct poseidon *pd, struct usb_device *udev)
{
}
#endif

static bool check_firmware(struct usb_device *udev, int *down_firmware)
{
	void *buf;
	int ret;
	struct cmd_firmware_vers_s *cmd_firm;

	buf = kzalloc(sizeof(*cmd_firm) + sizeof(u32), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	ret = usb_control_msg(udev,
			 usb_rcvctrlpipe(udev, 0),
			 REQ_GET_CMD | GET_FW_ID,
			 USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			 0,
			 0,
			 buf,
			 sizeof(*cmd_firm) + sizeof(u32),
			 USB_CTRL_GET_TIMEOUT);
	kfree(buf);

	if (ret < 0) {
		*down_firmware = 1;
		return firmware_download(udev);
	}
	return ret;
}

static int poseidon_probe(struct usb_interface *interface,
				const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct poseidon *pd = NULL;
	int ret = 0;
	int new_one = 0;

	/* download firmware */
	check_firmware(udev, &ret);
	if (ret)
		return 0;

	/* Do I recovery from the hibernate ? */
	pd = find_old_poseidon(udev);
	if (!pd) {
		pd = kzalloc(sizeof(*pd), GFP_KERNEL);
		if (!pd)
			return -ENOMEM;
		kref_init(&pd->kref);
		set_map_flags(pd, udev);
		new_one = 1;
	}

	pd->udev	= usb_get_dev(udev);
	pd->interface	= usb_get_intf(interface);
	usb_set_intfdata(interface, pd);

	if (new_one) {
		struct device *dev = &interface->dev;

		logpm(pd);
		mutex_init(&pd->lock);

		/* register v4l2 device */
		snprintf(pd->v4l2_dev.name, sizeof(pd->v4l2_dev.name), "%s %s",
			dev->driver->name, dev_name(dev));
		ret = v4l2_device_register(NULL, &pd->v4l2_dev);

		/* register devices in directory /dev */
		ret = pd_video_init(pd);
		poseidon_audio_init(pd);
		poseidon_fm_init(pd);
		pd_dvb_usb_device_init(pd);

		INIT_LIST_HEAD(&pd->device_list);
		list_add_tail(&pd->device_list, &pd_device_list);
	}

	device_init_wakeup(&udev->dev, 1);
#ifdef CONFIG_PM
	pd->udev->autosuspend_delay = HZ * PM_SUSPEND_DELAY;
	usb_enable_autosuspend(pd->udev);

	if (in_hibernation(pd)) {
		INIT_WORK(&pd->pm_work, hibernation_resume);
		schedule_work(&pd->pm_work);
	}
#endif
	return 0;
}

static void poseidon_disconnect(struct usb_interface *interface)
{
	struct poseidon *pd = get_pd(interface);

	if (!pd)
		return;
	logpm(pd);
	if (in_hibernation(pd))
		return;

	mutex_lock(&pd->lock);
	pd->state |= POSEIDON_STATE_DISCONNECT;
	mutex_unlock(&pd->lock);

	/* stop urb transferring */
	stop_all_video_stream(pd);
	dvb_stop_streaming(&pd->dvb_data);

	/*unregister v4l2 device */
	v4l2_device_unregister(&pd->v4l2_dev);

	pd_dvb_usb_device_exit(pd);
	poseidon_fm_exit(pd);

	poseidon_audio_free(pd);
	pd_video_exit(pd);

	usb_set_intfdata(interface, NULL);
	kref_put(&pd->kref, poseidon_delete);
}

static struct usb_driver poseidon_driver = {
	.name		= "poseidon",
	.probe		= poseidon_probe,
	.disconnect	= poseidon_disconnect,
	.id_table	= id_table,
#ifdef CONFIG_PM
	.suspend	= poseidon_suspend,
	.resume		= poseidon_resume,
#endif
	.supports_autosuspend = 1,
};

static int __init poseidon_init(void)
{
	int ret;

	ret = usb_register(&poseidon_driver);
	if (ret)
		return ret;
	register_pm_notifier(&pm_notifer);
	return ret;
}

static void __exit poseidon_exit(void)
{
	log();
	unregister_pm_notifier(&pm_notifer);
	usb_deregister(&poseidon_driver);
}

module_init(poseidon_init);
module_exit(poseidon_exit);

MODULE_AUTHOR("Telegent Systems");
MODULE_DESCRIPTION("For tlg2300-based USB device ");
MODULE_LICENSE("GPL");
