/******************************************************************************
 *  speedtch.c  -  Alcatel SpeedTouch USB xDSL modem driver
 *
 *  Copyright (C) 2001, Alcatel
 *  Copyright (C) 2003, Duncan Sands
 *  Copyright (C) 2004, David Woodhouse
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 ******************************************************************************/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/crc32.h>
#include <linux/init.h>
#include <linux/firmware.h>

#include "usb_atm.h"

#if defined(CONFIG_FW_LOADER) || defined(CONFIG_FW_LOADER_MODULE)
#	define USE_FW_LOADER
#endif

#define DRIVER_AUTHOR	"Johan Verrept, Duncan Sands <duncan.sands@free.fr>"
#define DRIVER_VERSION	"1.8"
#define DRIVER_DESC	"Alcatel SpeedTouch USB driver version " DRIVER_VERSION

static const char speedtch_driver_name[] = "speedtch";

#define SPEEDTOUCH_VENDORID		0x06b9
#define SPEEDTOUCH_PRODUCTID		0x4061

/* Timeout in jiffies */
#define CTRL_TIMEOUT 2000
#define DATA_TIMEOUT 2000

#define OFFSET_7  0		/* size 1 */
#define OFFSET_b  1		/* size 8 */
#define OFFSET_d  9		/* size 4 */
#define OFFSET_e 13		/* size 1 */
#define OFFSET_f 14		/* size 1 */
#define TOTAL    15

#define SIZE_7 1
#define SIZE_b 8
#define SIZE_d 4
#define SIZE_e 1
#define SIZE_f 1

static int dl_512_first = 0;
static int sw_buffering = 0;

module_param(dl_512_first, bool, 0444);
MODULE_PARM_DESC(dl_512_first, "Read 512 bytes before sending firmware");

module_param(sw_buffering, uint, 0444);
MODULE_PARM_DESC(sw_buffering, "Enable software buffering");

#define UDSL_IOCTL_LINE_UP		1
#define UDSL_IOCTL_LINE_DOWN		2

#define SPEEDTCH_ENDPOINT_INT		0x81
#define SPEEDTCH_ENDPOINT_DATA		0x07
#define SPEEDTCH_ENDPOINT_FIRMWARE	0x05

#define hex2int(c) ( (c >= '0') && (c <= '9') ? (c - '0') : ((c & 0xf) + 9) )

static struct usb_device_id speedtch_usb_ids[] = {
	{USB_DEVICE(SPEEDTOUCH_VENDORID, SPEEDTOUCH_PRODUCTID)},
	{}
};

MODULE_DEVICE_TABLE(usb, speedtch_usb_ids);

struct speedtch_instance_data {
	struct udsl_instance_data u;

	/* Status */
	struct urb *int_urb;
	unsigned char int_data[16];
	struct work_struct poll_work;
	struct timer_list poll_timer;
};
/* USB */

static int speedtch_usb_probe(struct usb_interface *intf,
			      const struct usb_device_id *id);
static void speedtch_usb_disconnect(struct usb_interface *intf);
static int speedtch_usb_ioctl(struct usb_interface *intf, unsigned int code,
			      void *user_data);
static void speedtch_handle_int(struct urb *urb, struct pt_regs *regs);
static void speedtch_poll_status(struct speedtch_instance_data *instance);

static struct usb_driver speedtch_usb_driver = {
	.owner		= THIS_MODULE,
	.name		= speedtch_driver_name,
	.probe		= speedtch_usb_probe,
	.disconnect	= speedtch_usb_disconnect,
	.ioctl		= speedtch_usb_ioctl,
	.id_table	= speedtch_usb_ids,
};

/***************
**  firmware  **
***************/

static void speedtch_got_firmware(struct speedtch_instance_data *instance,
				  int got_it)
{
	int err;
	struct usb_interface *intf;

	down(&instance->u.serialize);	/* vs self, speedtch_firmware_start */
	if (instance->u.status == UDSL_LOADED_FIRMWARE)
		goto out;
	if (!got_it) {
		instance->u.status = UDSL_NO_FIRMWARE;
		goto out;
	}
	if ((err = usb_set_interface(instance->u.usb_dev, 1, 1)) < 0) {
		dbg("speedtch_got_firmware: usb_set_interface returned %d!", err);
		instance->u.status = UDSL_NO_FIRMWARE;
		goto out;
	}

	/* Set up interrupt endpoint */
	intf = usb_ifnum_to_if(instance->u.usb_dev, 0);
	if (intf && !usb_driver_claim_interface(&speedtch_usb_driver, intf, NULL)) {

		instance->int_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (instance->int_urb) {

			usb_fill_int_urb(instance->int_urb, instance->u.usb_dev,
					 usb_rcvintpipe(instance->u.usb_dev, SPEEDTCH_ENDPOINT_INT),
					 instance->int_data,
					 sizeof(instance->int_data),
					 speedtch_handle_int, instance, 50);
			err = usb_submit_urb(instance->int_urb, GFP_KERNEL);
			if (err) {
				/* Doesn't matter; we'll poll anyway */
				dbg("speedtch_got_firmware: Submission of interrupt URB failed %d", err);
				usb_free_urb(instance->int_urb);
				instance->int_urb = NULL;
				usb_driver_release_interface(&speedtch_usb_driver, intf);
			}
		}
	}
	/* Start status polling */
	mod_timer(&instance->poll_timer, jiffies + (1 * HZ));

	instance->u.status = UDSL_LOADED_FIRMWARE;
	tasklet_schedule(&instance->u.receive_tasklet);
 out:
	up(&instance->u.serialize);
	wake_up_interruptible(&instance->u.firmware_waiters);
}

static int speedtch_set_swbuff(struct speedtch_instance_data *instance,
			       int state)
{
	struct usb_device *dev = instance->u.usb_dev;
	int ret;

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			      0x32, 0x40, state ? 0x01 : 0x00,
			      0x00, NULL, 0, 100);
	if (ret < 0) {
		printk("Warning: %sabling SW buffering: usb_control_msg returned %d\n",
		     state ? "En" : "Dis", ret);
		return ret;
	}

	dbg("speedtch_set_swbuff: %sbled SW buffering", state ? "En" : "Dis");
	return 0;
}

static void speedtch_test_sequence(struct speedtch_instance_data *instance)
{
	struct usb_device *dev = instance->u.usb_dev;
	unsigned char buf[10];
	int ret;

	/* URB 147 */
	buf[0] = 0x1c;
	buf[1] = 0x50;
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			      0x01, 0x40, 0x0b, 0x00, buf, 2, 100);
	if (ret < 0)
		printk(KERN_WARNING "%s failed on URB147: %d\n", __func__, ret);

	/* URB 148 */
	buf[0] = 0x32;
	buf[1] = 0x00;
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			      0x01, 0x40, 0x02, 0x00, buf, 2, 100);
	if (ret < 0)
		printk(KERN_WARNING "%s failed on URB148: %d\n", __func__, ret);

	/* URB 149 */
	buf[0] = 0x01;
	buf[1] = 0x00;
	buf[2] = 0x01;
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			      0x01, 0x40, 0x03, 0x00, buf, 3, 100);
	if (ret < 0)
		printk(KERN_WARNING "%s failed on URB149: %d\n", __func__, ret);

	/* URB 150 */
	buf[0] = 0x01;
	buf[1] = 0x00;
	buf[2] = 0x01;
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			      0x01, 0x40, 0x04, 0x00, buf, 3, 100);
	if (ret < 0)
		printk(KERN_WARNING "%s failed on URB150: %d\n", __func__, ret);
}

static int speedtch_start_synchro(struct speedtch_instance_data *instance)
{
	struct usb_device *dev = instance->u.usb_dev;
	unsigned char buf[2];
	int ret;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			      0x12, 0xc0, 0x04, 0x00,
			      buf, sizeof(buf), CTRL_TIMEOUT);
	if (ret < 0) {
		printk(KERN_WARNING "SpeedTouch: Failed to start ADSL synchronisation: %d\n", ret);
		return ret;
	}

	dbg("speedtch_start_synchro: modem prodded. %d Bytes returned: %02x %02x", ret, buf[0], buf[1]);
	return 0;
}

static void speedtch_handle_int(struct urb *urb, struct pt_regs *regs)
{
	struct speedtch_instance_data *instance = urb->context;
	unsigned int count = urb->actual_length;
	int ret;

	/* The magic interrupt for "up state" */
	const static unsigned char up_int[6]   = { 0xa1, 0x00, 0x01, 0x00, 0x00, 0x00 };
	/* The magic interrupt for "down state" */
	const static unsigned char down_int[6] = { 0xa1, 0x00, 0x00, 0x00, 0x00, 0x00 };

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated; clean up */
		dbg("%s - urb shutting down with status: %d", __func__, urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __func__, urb->status);
		goto exit;
	}

	if (count < 6) {
		dbg("%s - int packet too short", __func__);
		goto exit;
	}

	if (!memcmp(up_int, instance->int_data, 6)) {
		del_timer(&instance->poll_timer);
		printk(KERN_NOTICE "DSL line goes up\n");
	} else if (!memcmp(down_int, instance->int_data, 6)) {
		printk(KERN_NOTICE "DSL line goes down\n");
	} else {
		int i;

		printk(KERN_DEBUG "Unknown interrupt packet of %d bytes:", count);
		for (i = 0; i < count; i++)
			printk(" %02x", instance->int_data[i]);
		printk("\n");
	}
	schedule_work(&instance->poll_work);

 exit:
	rmb();
	if (!instance->int_urb)
		return;

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret)
		err("%s - usb_submit_urb failed with result %d", __func__, ret);
}

static int speedtch_get_status(struct speedtch_instance_data *instance,
			       unsigned char *buf)
{
	struct usb_device *dev = instance->u.usb_dev;
	int ret;

	memset(buf, 0, TOTAL);

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			      0x12, 0xc0, 0x07, 0x00, buf + OFFSET_7, SIZE_7,
			      CTRL_TIMEOUT);
	if (ret < 0) {
		dbg("MSG 7 failed");
		return ret;
	}

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			      0x12, 0xc0, 0x0b, 0x00, buf + OFFSET_b, SIZE_b,
			      CTRL_TIMEOUT);
	if (ret < 0) {
		dbg("MSG B failed");
		return ret;
	}

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			      0x12, 0xc0, 0x0d, 0x00, buf + OFFSET_d, SIZE_d,
			      CTRL_TIMEOUT);
	if (ret < 0) {
		dbg("MSG D failed");
		return ret;
	}

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			      0x01, 0xc0, 0x0e, 0x00, buf + OFFSET_e, SIZE_e,
			      CTRL_TIMEOUT);
	if (ret < 0) {
		dbg("MSG E failed");
		return ret;
	}

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			      0x01, 0xc0, 0x0f, 0x00, buf + OFFSET_f, SIZE_f,
			      CTRL_TIMEOUT);
	if (ret < 0) {
		dbg("MSG F failed");
		return ret;
	}

	return 0;
}

static void speedtch_poll_status(struct speedtch_instance_data *instance)
{
	unsigned char buf[TOTAL];
	int ret;

	ret = speedtch_get_status(instance, buf);
	if (ret) {
		printk(KERN_WARNING
		       "SpeedTouch: Error %d fetching device status\n", ret);
		return;
	}

	dbg("Line state %02x", buf[OFFSET_7]);

	switch (buf[OFFSET_7]) {
	case 0:
		if (instance->u.atm_dev->signal != ATM_PHY_SIG_LOST) {
			instance->u.atm_dev->signal = ATM_PHY_SIG_LOST;
			printk(KERN_NOTICE "ADSL line is down\n");
			/* It'll never resync again unless we ask it to... */
			speedtch_start_synchro(instance);
		}
		break;

	case 0x08:
		if (instance->u.atm_dev->signal != ATM_PHY_SIG_UNKNOWN) {
			instance->u.atm_dev->signal = ATM_PHY_SIG_UNKNOWN;
			printk(KERN_NOTICE "ADSL line is blocked?\n");
		}
		break;

	case 0x10:
		if (instance->u.atm_dev->signal != ATM_PHY_SIG_LOST) {
			instance->u.atm_dev->signal = ATM_PHY_SIG_LOST;
			printk(KERN_NOTICE "ADSL line is synchronising\n");
		}
		break;

	case 0x20:
		if (instance->u.atm_dev->signal != ATM_PHY_SIG_FOUND) {
			int down_speed = buf[OFFSET_b] | (buf[OFFSET_b + 1] << 8)
				| (buf[OFFSET_b + 2] << 16) | (buf[OFFSET_b + 3] << 24);
			int up_speed = buf[OFFSET_b + 4] | (buf[OFFSET_b + 5] << 8)
				| (buf[OFFSET_b + 6] << 16) | (buf[OFFSET_b + 7] << 24);

			if (!(down_speed & 0x0000ffff) &&
			    !(up_speed & 0x0000ffff)) {
				down_speed >>= 16;
				up_speed >>= 16;
			}
			instance->u.atm_dev->link_rate = down_speed * 1000 / 424;
			instance->u.atm_dev->signal = ATM_PHY_SIG_FOUND;

			printk(KERN_NOTICE
			       "ADSL line is up (%d Kib/s down | %d Kib/s up)\n",
			       down_speed, up_speed);
		}
		break;

	default:
		if (instance->u.atm_dev->signal != ATM_PHY_SIG_UNKNOWN) {
			instance->u.atm_dev->signal = ATM_PHY_SIG_UNKNOWN;
			printk(KERN_NOTICE "Unknown line state %02x\n", buf[OFFSET_7]);
		}
		break;
	}
}

static void speedtch_timer_poll(unsigned long data)
{
	struct speedtch_instance_data *instance = (void *)data;

	schedule_work(&instance->poll_work);
	mod_timer(&instance->poll_timer, jiffies + (5 * HZ));
}

#ifdef USE_FW_LOADER
static void speedtch_upload_firmware(struct speedtch_instance_data *instance,
				     const struct firmware *fw1,
				     const struct firmware *fw2)
{
	unsigned char *buffer;
	struct usb_device *usb_dev = instance->u.usb_dev;
	struct usb_interface *intf;
	int actual_length, ret;
	int offset;

	dbg("speedtch_upload_firmware");

	if (!(intf = usb_ifnum_to_if(usb_dev, 2))) {
		dbg("speedtch_upload_firmware: interface not found!");
		goto fail;
	}

	if (!(buffer = (unsigned char *)__get_free_page(GFP_KERNEL))) {
		dbg("speedtch_upload_firmware: no memory for buffer!");
		goto fail;
	}

	/* A user-space firmware loader may already have claimed interface #2 */
	if ((ret =
	     usb_driver_claim_interface(&speedtch_usb_driver, intf, NULL)) < 0) {
		dbg("speedtch_upload_firmware: interface in use (%d)!", ret);
		goto fail_free;
	}

	/* URB 7 */
	if (dl_512_first) {	/* some modems need a read before writing the firmware */
		ret = usb_bulk_msg(usb_dev, usb_rcvbulkpipe(usb_dev, SPEEDTCH_ENDPOINT_FIRMWARE),
				   buffer, 0x200, &actual_length, 2000);

		if (ret < 0 && ret != -ETIMEDOUT)
			dbg("speedtch_upload_firmware: read BLOCK0 from modem failed (%d)!", ret);
		else
			dbg("speedtch_upload_firmware: BLOCK0 downloaded (%d bytes)", ret);
	}

	/* URB 8 : both leds are static green */
	for (offset = 0; offset < fw1->size; offset += PAGE_SIZE) {
		int thislen = min_t(int, PAGE_SIZE, fw1->size - offset);
		memcpy(buffer, fw1->data + offset, thislen);

		ret = usb_bulk_msg(usb_dev, usb_sndbulkpipe(usb_dev, SPEEDTCH_ENDPOINT_FIRMWARE),
				   buffer, thislen, &actual_length, DATA_TIMEOUT);

		if (ret < 0) {
			dbg("speedtch_upload_firmware: write BLOCK1 to modem failed (%d)!", ret);
			goto fail_release;
		}
		dbg("speedtch_upload_firmware: BLOCK1 uploaded (%zu bytes)", fw1->size);
	}

	/* USB led blinking green, ADSL led off */

	/* URB 11 */
	ret = usb_bulk_msg(usb_dev, usb_rcvbulkpipe(usb_dev, SPEEDTCH_ENDPOINT_FIRMWARE),
			   buffer, 0x200, &actual_length, DATA_TIMEOUT);

	if (ret < 0) {
		dbg("speedtch_upload_firmware: read BLOCK2 from modem failed (%d)!", ret);
		goto fail_release;
	}
	dbg("speedtch_upload_firmware: BLOCK2 downloaded (%d bytes)", actual_length);

	/* URBs 12 to 139 - USB led blinking green, ADSL led off */
	for (offset = 0; offset < fw2->size; offset += PAGE_SIZE) {
		int thislen = min_t(int, PAGE_SIZE, fw2->size - offset);
		memcpy(buffer, fw2->data + offset, thislen);

		ret = usb_bulk_msg(usb_dev, usb_sndbulkpipe(usb_dev, SPEEDTCH_ENDPOINT_FIRMWARE),
				   buffer, thislen, &actual_length, DATA_TIMEOUT);

		if (ret < 0) {
			dbg("speedtch_upload_firmware: write BLOCK3 to modem failed (%d)!", ret);
			goto fail_release;
		}
	}
	dbg("speedtch_upload_firmware: BLOCK3 uploaded (%zu bytes)", fw2->size);

	/* USB led static green, ADSL led static red */

	/* URB 142 */
	ret = usb_bulk_msg(usb_dev, usb_rcvbulkpipe(usb_dev, SPEEDTCH_ENDPOINT_FIRMWARE),
			   buffer, 0x200, &actual_length, DATA_TIMEOUT);

	if (ret < 0) {
		dbg("speedtch_upload_firmware: read BLOCK4 from modem failed (%d)!", ret);
		goto fail_release;
	}

	/* success */
	dbg("speedtch_upload_firmware: BLOCK4 downloaded (%d bytes)", actual_length);

	/* Delay to allow firmware to start up. We can do this here
	   because we're in our own kernel thread anyway. */
	msleep(1000);

	/* Enable software buffering, if requested */
	if (sw_buffering)
		speedtch_set_swbuff(instance, 1);

	/* Magic spell; don't ask us what this does */
	speedtch_test_sequence(instance);

	/* Start modem synchronisation */
	if (speedtch_start_synchro(instance))
		dbg("speedtch_start_synchro: failed");

	speedtch_got_firmware(instance, 1);

	free_page((unsigned long)buffer);
	return;

 fail_release:
	/* Only release interface #2 if uploading failed; we don't release it
	   we succeeded.  This prevents the userspace tools from trying to load
	   the firmware themselves */
	usb_driver_release_interface(&speedtch_usb_driver, intf);
 fail_free:
	free_page((unsigned long)buffer);
 fail:
	speedtch_got_firmware(instance, 0);
}

static int speedtch_find_firmware(struct speedtch_instance_data
				  *instance, int phase,
				  const struct firmware **fw_p)
{
	char buf[24];
	const u16 bcdDevice = le16_to_cpu(instance->u.usb_dev->descriptor.bcdDevice);
	const u8 major_revision = bcdDevice >> 8;
	const u8 minor_revision = bcdDevice & 0xff;

	sprintf(buf, "speedtch-%d.bin.%x.%02x", phase, major_revision, minor_revision);
	dbg("speedtch_find_firmware: looking for %s", buf);

	if (request_firmware(fw_p, buf, &instance->u.usb_dev->dev)) {
		sprintf(buf, "speedtch-%d.bin.%x", phase, major_revision);
		dbg("speedtch_find_firmware: looking for %s", buf);

		if (request_firmware(fw_p, buf, &instance->u.usb_dev->dev)) {
			sprintf(buf, "speedtch-%d.bin", phase);
			dbg("speedtch_find_firmware: looking for %s", buf);

			if (request_firmware(fw_p, buf, &instance->u.usb_dev->dev)) {
				dev_warn(&instance->u.usb_dev->dev, "no stage %d firmware found!", phase);
				return -ENOENT;
			}
		}
	}

	dev_info(&instance->u.usb_dev->dev, "found stage %d firmware %s\n", phase, buf);

	return 0;
}

static int speedtch_load_firmware(void *arg)
{
	const struct firmware *fw1, *fw2;
	struct speedtch_instance_data *instance = arg;

	BUG_ON(!instance);

	daemonize("firmware/speedtch");

	if (!speedtch_find_firmware(instance, 1, &fw1)) {
		if (!speedtch_find_firmware(instance, 2, &fw2)) {
			speedtch_upload_firmware(instance, fw1, fw2);
			release_firmware(fw2);
		}
		release_firmware(fw1);
	}

	/* In case we failed, set state back to NO_FIRMWARE so that
	   another later attempt may work. Otherwise, we never actually
	   manage to recover if, for example, the firmware is on /usr and
	   we look for it too early. */
	speedtch_got_firmware(instance, 0);

	module_put(THIS_MODULE);
	udsl_put_instance(&instance->u);
	return 0;
}
#endif /* USE_FW_LOADER */

static void speedtch_firmware_start(struct speedtch_instance_data *instance)
{
#ifdef USE_FW_LOADER
	int ret;
#endif

	dbg("speedtch_firmware_start");

	down(&instance->u.serialize);	/* vs self, speedtch_got_firmware */

	if (instance->u.status >= UDSL_LOADING_FIRMWARE) {
		up(&instance->u.serialize);
		return;
	}

	instance->u.status = UDSL_LOADING_FIRMWARE;
	up(&instance->u.serialize);

#ifdef USE_FW_LOADER
	udsl_get_instance(&instance->u);
	try_module_get(THIS_MODULE);

	ret = kernel_thread(speedtch_load_firmware, instance,
			    CLONE_FS | CLONE_FILES);

	if (ret >= 0)
		return;		/* OK */

	dbg("speedtch_firmware_start: kernel_thread failed (%d)!", ret);

	module_put(THIS_MODULE);
	udsl_put_instance(&instance->u);
	/* Just pretend it never happened... hope modem_run happens */
#endif				/* USE_FW_LOADER */

	speedtch_got_firmware(instance, 0);
}

static int speedtch_firmware_wait(struct udsl_instance_data *instance)
{
	speedtch_firmware_start((void *)instance);

	if (wait_event_interruptible(instance->firmware_waiters, instance->status != UDSL_LOADING_FIRMWARE) < 0)
		return -ERESTARTSYS;

	return (instance->status == UDSL_LOADED_FIRMWARE) ? 0 : -EAGAIN;
}

/**********
**  USB  **
**********/

static int speedtch_usb_ioctl(struct usb_interface *intf, unsigned int code,
			      void *user_data)
{
	struct speedtch_instance_data *instance = usb_get_intfdata(intf);

	dbg("speedtch_usb_ioctl entered");

	if (!instance) {
		dbg("speedtch_usb_ioctl: NULL instance!");
		return -ENODEV;
	}

	switch (code) {
	case UDSL_IOCTL_LINE_UP:
		instance->u.atm_dev->signal = ATM_PHY_SIG_FOUND;
		speedtch_got_firmware(instance, 1);
		return (instance->u.status == UDSL_LOADED_FIRMWARE) ? 0 : -EIO;
	case UDSL_IOCTL_LINE_DOWN:
		instance->u.atm_dev->signal = ATM_PHY_SIG_LOST;
		return 0;
	default:
		return -ENOTTY;
	}
}

static int speedtch_usb_probe(struct usb_interface *intf,
			      const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	int ifnum = intf->altsetting->desc.bInterfaceNumber;
	struct speedtch_instance_data *instance;
	unsigned char mac_str[13];
	int ret, i;
	char buf7[SIZE_7];

	dbg("speedtch_usb_probe: trying device with vendor=0x%x, product=0x%x, ifnum %d",
	    le16_to_cpu(dev->descriptor.idVendor),
	    le16_to_cpu(dev->descriptor.idProduct), ifnum);

	if ((dev->descriptor.bDeviceClass != USB_CLASS_VENDOR_SPEC) || 
	    (ifnum != 1))
		return -ENODEV;

	dbg("speedtch_usb_probe: device accepted");

	/* instance init */
	instance = kmalloc(sizeof(*instance), GFP_KERNEL);
	if (!instance) {
		dbg("speedtch_usb_probe: no memory for instance data!");
		return -ENOMEM;
	}

	memset(instance, 0, sizeof(struct speedtch_instance_data));

	if ((ret = usb_set_interface(dev, 0, 0)) < 0)
		goto fail;

	if ((ret = usb_set_interface(dev, 2, 0)) < 0)
		goto fail;

	instance->u.data_endpoint = SPEEDTCH_ENDPOINT_DATA;
	instance->u.firmware_wait = speedtch_firmware_wait;
	instance->u.driver_name = speedtch_driver_name;

	ret = udsl_instance_setup(dev, &instance->u);
	if (ret)
		goto fail;

	init_timer(&instance->poll_timer);
	instance->poll_timer.function = speedtch_timer_poll;
	instance->poll_timer.data = (unsigned long)instance;

	INIT_WORK(&instance->poll_work, (void *)speedtch_poll_status, instance);

	/* set MAC address, it is stored in the serial number */
	memset(instance->u.atm_dev->esi, 0, sizeof(instance->u.atm_dev->esi));
	if (usb_string(dev, dev->descriptor.iSerialNumber, mac_str, sizeof(mac_str)) == 12) {
		for (i = 0; i < 6; i++)
			instance->u.atm_dev->esi[i] =
				(hex2int(mac_str[i * 2]) * 16) + (hex2int(mac_str[i * 2 + 1]));
	}

	/* First check whether the modem already seems to be alive */
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			      0x12, 0xc0, 0x07, 0x00, buf7, SIZE_7, 500);

	if (ret == SIZE_7) {
		dbg("firmware appears to be already loaded");
		speedtch_got_firmware(instance, 1);
		speedtch_poll_status(instance);
	} else {
		speedtch_firmware_start(instance);
	}

	usb_set_intfdata(intf, instance);

	return 0;

 fail:
	kfree(instance);

	return -ENOMEM;
}

static void speedtch_usb_disconnect(struct usb_interface *intf)
{
	struct speedtch_instance_data *instance = usb_get_intfdata(intf);

	dbg("speedtch_usb_disconnect entered");

	if (!instance) {
		dbg("speedtch_usb_disconnect: NULL instance!");
		return;
	}

/*QQ need to handle disconnects on interface #2 while uploading firmware */
/*QQ and what about interface #1? */

	if (instance->int_urb) {
		struct urb *int_urb = instance->int_urb;
		instance->int_urb = NULL;
		wmb();
		usb_unlink_urb(int_urb);
		usb_free_urb(int_urb);
	}

	instance->int_data[0] = 1;
	del_timer_sync(&instance->poll_timer);
	wmb();
	flush_scheduled_work();

	udsl_instance_disconnect(&instance->u);

	/* clean up */
	usb_set_intfdata(intf, NULL);
	udsl_put_instance(&instance->u);
}

/***********
**  init  **
***********/

static int __init speedtch_usb_init(void)
{
	dbg("speedtch_usb_init: driver version " DRIVER_VERSION);

	return usb_register(&speedtch_usb_driver);
}

static void __exit speedtch_usb_cleanup(void)
{
	dbg("speedtch_usb_cleanup entered");

	usb_deregister(&speedtch_usb_driver);
}

module_init(speedtch_usb_init);
module_exit(speedtch_usb_cleanup);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);
