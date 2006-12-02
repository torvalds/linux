/******************************************************************************
 *  speedtch.c  -  Alcatel SpeedTouch USB xDSL modem driver
 *
 *  Copyright (C) 2001, Alcatel
 *  Copyright (C) 2003, Duncan Sands
 *  Copyright (C) 2004, David Woodhouse
 *
 *  Based on "modem_run.c", copyright (C) 2001, Benoit Papillault
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

#include <asm/page.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/usb_ch9.h>
#include <linux/workqueue.h>

#include "usbatm.h"

#define DRIVER_AUTHOR	"Johan Verrept, Duncan Sands <duncan.sands@free.fr>"
#define DRIVER_VERSION	"1.10"
#define DRIVER_DESC	"Alcatel SpeedTouch USB driver version " DRIVER_VERSION

static const char speedtch_driver_name[] = "speedtch";

#define CTRL_TIMEOUT 2000	/* milliseconds */
#define DATA_TIMEOUT 2000	/* milliseconds */

#define OFFSET_7	0		/* size 1 */
#define OFFSET_b	1		/* size 8 */
#define OFFSET_d	9		/* size 4 */
#define OFFSET_e	13		/* size 1 */
#define OFFSET_f	14		/* size 1 */

#define SIZE_7		1
#define SIZE_b		8
#define SIZE_d		4
#define SIZE_e		1
#define SIZE_f		1

#define MIN_POLL_DELAY		5000	/* milliseconds */
#define MAX_POLL_DELAY		60000	/* milliseconds */

#define RESUBMIT_DELAY		1000	/* milliseconds */

#define DEFAULT_BULK_ALTSETTING	1
#define DEFAULT_ISOC_ALTSETTING	3
#define DEFAULT_DL_512_FIRST	0
#define DEFAULT_ENABLE_ISOC	0
#define DEFAULT_SW_BUFFERING	0

static unsigned int altsetting = 0; /* zero means: use the default */
static int dl_512_first = DEFAULT_DL_512_FIRST;
static int enable_isoc = DEFAULT_ENABLE_ISOC;
static int sw_buffering = DEFAULT_SW_BUFFERING;

#define DEFAULT_B_MAX_DSL	8128
#define DEFAULT_MODEM_MODE	11
#define MODEM_OPTION_LENGTH	16
static const unsigned char DEFAULT_MODEM_OPTION[MODEM_OPTION_LENGTH] = {
	0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static unsigned int BMaxDSL = DEFAULT_B_MAX_DSL;
static unsigned char ModemMode = DEFAULT_MODEM_MODE;
static unsigned char ModemOption[MODEM_OPTION_LENGTH];
static int num_ModemOption;

module_param(altsetting, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(altsetting,
		"Alternative setting for data interface (bulk_default: "
		__MODULE_STRING(DEFAULT_BULK_ALTSETTING) "; isoc_default: "
		__MODULE_STRING(DEFAULT_ISOC_ALTSETTING) ")");

module_param(dl_512_first, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dl_512_first,
		 "Read 512 bytes before sending firmware (default: "
		 __MODULE_STRING(DEFAULT_DL_512_FIRST) ")");

module_param(enable_isoc, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(enable_isoc,
		"Use isochronous transfers if available (default: "
		__MODULE_STRING(DEFAULT_ENABLE_ISOC) ")");

module_param(sw_buffering, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(sw_buffering,
		 "Enable software buffering (default: "
		 __MODULE_STRING(DEFAULT_SW_BUFFERING) ")");

module_param(BMaxDSL, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(BMaxDSL,
		"default: " __MODULE_STRING(DEFAULT_B_MAX_DSL));

module_param(ModemMode, byte, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ModemMode,
		"default: " __MODULE_STRING(DEFAULT_MODEM_MODE));

module_param_array(ModemOption, byte, &num_ModemOption, S_IRUGO);
MODULE_PARM_DESC(ModemOption, "default: 0x10,0x00,0x00,0x00,0x20");

#define INTERFACE_DATA		1
#define ENDPOINT_INT		0x81
#define ENDPOINT_BULK_DATA	0x07
#define ENDPOINT_ISOC_DATA	0x07
#define ENDPOINT_FIRMWARE	0x05

#define hex2int(c) ( (c >= '0') && (c <= '9') ? (c - '0') : ((c & 0xf) + 9) )

struct speedtch_params {
	unsigned int altsetting;
	unsigned int BMaxDSL;
	unsigned char ModemMode;
	unsigned char ModemOption[MODEM_OPTION_LENGTH];
};

struct speedtch_instance_data {
	struct usbatm_data *usbatm;

	struct speedtch_params params; /* set in probe, constant afterwards */

	struct work_struct status_checker;

	unsigned char last_status;

	int poll_delay; /* milliseconds */

	struct timer_list resubmit_timer;
	struct urb *int_urb;
	unsigned char int_data[16];

	unsigned char scratch_buffer[16];
};

/***************
**  firmware  **
***************/

static void speedtch_set_swbuff(struct speedtch_instance_data *instance, int state)
{
	struct usbatm_data *usbatm = instance->usbatm;
	struct usb_device *usb_dev = usbatm->usb_dev;
	int ret;

	ret = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			      0x32, 0x40, state ? 0x01 : 0x00, 0x00, NULL, 0, CTRL_TIMEOUT);
	if (ret < 0)
		usb_warn(usbatm,
			 "%sabling SW buffering: usb_control_msg returned %d\n",
			 state ? "En" : "Dis", ret);
	else
		dbg("speedtch_set_swbuff: %sbled SW buffering", state ? "En" : "Dis");
}

static void speedtch_test_sequence(struct speedtch_instance_data *instance)
{
	struct usbatm_data *usbatm = instance->usbatm;
	struct usb_device *usb_dev = usbatm->usb_dev;
	unsigned char *buf = instance->scratch_buffer;
	int ret;

	/* URB 147 */
	buf[0] = 0x1c;
	buf[1] = 0x50;
	ret = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			      0x01, 0x40, 0x0b, 0x00, buf, 2, CTRL_TIMEOUT);
	if (ret < 0)
		usb_warn(usbatm, "%s failed on URB147: %d\n", __func__, ret);

	/* URB 148 */
	buf[0] = 0x32;
	buf[1] = 0x00;
	ret = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			      0x01, 0x40, 0x02, 0x00, buf, 2, CTRL_TIMEOUT);
	if (ret < 0)
		usb_warn(usbatm, "%s failed on URB148: %d\n", __func__, ret);

	/* URB 149 */
	buf[0] = 0x01;
	buf[1] = 0x00;
	buf[2] = 0x01;
	ret = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			      0x01, 0x40, 0x03, 0x00, buf, 3, CTRL_TIMEOUT);
	if (ret < 0)
		usb_warn(usbatm, "%s failed on URB149: %d\n", __func__, ret);

	/* URB 150 */
	buf[0] = 0x01;
	buf[1] = 0x00;
	buf[2] = 0x01;
	ret = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			      0x01, 0x40, 0x04, 0x00, buf, 3, CTRL_TIMEOUT);
	if (ret < 0)
		usb_warn(usbatm, "%s failed on URB150: %d\n", __func__, ret);

	/* Extra initialisation in recent drivers - gives higher speeds */

	/* URBext1 */
	buf[0] = instance->params.ModemMode;
	ret = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			      0x01, 0x40, 0x11, 0x00, buf, 1, CTRL_TIMEOUT);
	if (ret < 0)
		usb_warn(usbatm, "%s failed on URBext1: %d\n", __func__, ret);

	/* URBext2 */
	/* This seems to be the one which actually triggers the higher sync
	   rate -- it does require the new firmware too, although it works OK
	   with older firmware */
	ret = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			      0x01, 0x40, 0x14, 0x00,
			      instance->params.ModemOption,
			      MODEM_OPTION_LENGTH, CTRL_TIMEOUT);
	if (ret < 0)
		usb_warn(usbatm, "%s failed on URBext2: %d\n", __func__, ret);

	/* URBext3 */
	buf[0] = instance->params.BMaxDSL & 0xff;
	buf[1] = instance->params.BMaxDSL >> 8;
	ret = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			      0x01, 0x40, 0x12, 0x00, buf, 2, CTRL_TIMEOUT);
	if (ret < 0)
		usb_warn(usbatm, "%s failed on URBext3: %d\n", __func__, ret);
}

static int speedtch_upload_firmware(struct speedtch_instance_data *instance,
				     const struct firmware *fw1,
				     const struct firmware *fw2)
{
	unsigned char *buffer;
	struct usbatm_data *usbatm = instance->usbatm;
	struct usb_interface *intf;
	struct usb_device *usb_dev = usbatm->usb_dev;
	int actual_length;
	int ret = 0;
	int offset;

	usb_dbg(usbatm, "%s entered\n", __func__);

	if (!(buffer = (unsigned char *)__get_free_page(GFP_KERNEL))) {
		ret = -ENOMEM;
		usb_dbg(usbatm, "%s: no memory for buffer!\n", __func__);
		goto out;
	}

	if (!(intf = usb_ifnum_to_if(usb_dev, 2))) {
		ret = -ENODEV;
		usb_dbg(usbatm, "%s: interface not found!\n", __func__);
		goto out_free;
	}

	/* URB 7 */
	if (dl_512_first) {	/* some modems need a read before writing the firmware */
		ret = usb_bulk_msg(usb_dev, usb_rcvbulkpipe(usb_dev, ENDPOINT_FIRMWARE),
				   buffer, 0x200, &actual_length, 2000);

		if (ret < 0 && ret != -ETIMEDOUT)
			usb_warn(usbatm, "%s: read BLOCK0 from modem failed (%d)!\n", __func__, ret);
		else
			usb_dbg(usbatm, "%s: BLOCK0 downloaded (%d bytes)\n", __func__, ret);
	}

	/* URB 8 : both leds are static green */
	for (offset = 0; offset < fw1->size; offset += PAGE_SIZE) {
		int thislen = min_t(int, PAGE_SIZE, fw1->size - offset);
		memcpy(buffer, fw1->data + offset, thislen);

		ret = usb_bulk_msg(usb_dev, usb_sndbulkpipe(usb_dev, ENDPOINT_FIRMWARE),
				   buffer, thislen, &actual_length, DATA_TIMEOUT);

		if (ret < 0) {
			usb_err(usbatm, "%s: write BLOCK1 to modem failed (%d)!\n", __func__, ret);
			goto out_free;
		}
		usb_dbg(usbatm, "%s: BLOCK1 uploaded (%zu bytes)\n", __func__, fw1->size);
	}

	/* USB led blinking green, ADSL led off */

	/* URB 11 */
	ret = usb_bulk_msg(usb_dev, usb_rcvbulkpipe(usb_dev, ENDPOINT_FIRMWARE),
			   buffer, 0x200, &actual_length, DATA_TIMEOUT);

	if (ret < 0) {
		usb_err(usbatm, "%s: read BLOCK2 from modem failed (%d)!\n", __func__, ret);
		goto out_free;
	}
	usb_dbg(usbatm, "%s: BLOCK2 downloaded (%d bytes)\n", __func__, actual_length);

	/* URBs 12 to 139 - USB led blinking green, ADSL led off */
	for (offset = 0; offset < fw2->size; offset += PAGE_SIZE) {
		int thislen = min_t(int, PAGE_SIZE, fw2->size - offset);
		memcpy(buffer, fw2->data + offset, thislen);

		ret = usb_bulk_msg(usb_dev, usb_sndbulkpipe(usb_dev, ENDPOINT_FIRMWARE),
				   buffer, thislen, &actual_length, DATA_TIMEOUT);

		if (ret < 0) {
			usb_err(usbatm, "%s: write BLOCK3 to modem failed (%d)!\n", __func__, ret);
			goto out_free;
		}
	}
	usb_dbg(usbatm, "%s: BLOCK3 uploaded (%zu bytes)\n", __func__, fw2->size);

	/* USB led static green, ADSL led static red */

	/* URB 142 */
	ret = usb_bulk_msg(usb_dev, usb_rcvbulkpipe(usb_dev, ENDPOINT_FIRMWARE),
			   buffer, 0x200, &actual_length, DATA_TIMEOUT);

	if (ret < 0) {
		usb_err(usbatm, "%s: read BLOCK4 from modem failed (%d)!\n", __func__, ret);
		goto out_free;
	}

	/* success */
	usb_dbg(usbatm, "%s: BLOCK4 downloaded (%d bytes)\n", __func__, actual_length);

	/* Delay to allow firmware to start up. We can do this here
	   because we're in our own kernel thread anyway. */
	msleep_interruptible(1000);

	if ((ret = usb_set_interface(usb_dev, INTERFACE_DATA, instance->params.altsetting)) < 0) {
		usb_err(usbatm, "%s: setting interface to %d failed (%d)!\n", __func__, instance->params.altsetting, ret);
		goto out_free;
	}

	/* Enable software buffering, if requested */
	if (sw_buffering)
		speedtch_set_swbuff(instance, 1);

	/* Magic spell; don't ask us what this does */
	speedtch_test_sequence(instance);

	ret = 0;

out_free:
	free_page((unsigned long)buffer);
out:
	return ret;
}

static int speedtch_find_firmware(struct usbatm_data *usbatm, struct usb_interface *intf,
				  int phase, const struct firmware **fw_p)
{
	struct device *dev = &intf->dev;
	const u16 bcdDevice = le16_to_cpu(interface_to_usbdev(intf)->descriptor.bcdDevice);
	const u8 major_revision = bcdDevice >> 8;
	const u8 minor_revision = bcdDevice & 0xff;
	char buf[24];

	sprintf(buf, "speedtch-%d.bin.%x.%02x", phase, major_revision, minor_revision);
	usb_dbg(usbatm, "%s: looking for %s\n", __func__, buf);

	if (request_firmware(fw_p, buf, dev)) {
		sprintf(buf, "speedtch-%d.bin.%x", phase, major_revision);
		usb_dbg(usbatm, "%s: looking for %s\n", __func__, buf);

		if (request_firmware(fw_p, buf, dev)) {
			sprintf(buf, "speedtch-%d.bin", phase);
			usb_dbg(usbatm, "%s: looking for %s\n", __func__, buf);

			if (request_firmware(fw_p, buf, dev)) {
				usb_err(usbatm, "%s: no stage %d firmware found!\n", __func__, phase);
				return -ENOENT;
			}
		}
	}

	usb_info(usbatm, "found stage %d firmware %s\n", phase, buf);

	return 0;
}

static int speedtch_heavy_init(struct usbatm_data *usbatm, struct usb_interface *intf)
{
	const struct firmware *fw1, *fw2;
	struct speedtch_instance_data *instance = usbatm->driver_data;
	int ret;

	if ((ret = speedtch_find_firmware(usbatm, intf, 1, &fw1)) < 0)
		return ret;

	if ((ret = speedtch_find_firmware(usbatm, intf, 2, &fw2)) < 0) {
		release_firmware(fw1);
		return ret;
	}

	if ((ret = speedtch_upload_firmware(instance, fw1, fw2)) < 0)
		usb_err(usbatm, "%s: firmware upload failed (%d)!\n", __func__, ret);

	release_firmware(fw2);
	release_firmware(fw1);

	return ret;
}


/**********
**  ATM  **
**********/

static int speedtch_read_status(struct speedtch_instance_data *instance)
{
	struct usbatm_data *usbatm = instance->usbatm;
	struct usb_device *usb_dev = usbatm->usb_dev;
	unsigned char *buf = instance->scratch_buffer;
	int ret;

	memset(buf, 0, 16);

	ret = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			      0x12, 0xc0, 0x07, 0x00, buf + OFFSET_7, SIZE_7,
			      CTRL_TIMEOUT);
	if (ret < 0) {
		atm_dbg(usbatm, "%s: MSG 7 failed\n", __func__);
		return ret;
	}

	ret = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			      0x12, 0xc0, 0x0b, 0x00, buf + OFFSET_b, SIZE_b,
			      CTRL_TIMEOUT);
	if (ret < 0) {
		atm_dbg(usbatm, "%s: MSG B failed\n", __func__);
		return ret;
	}

	ret = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			      0x12, 0xc0, 0x0d, 0x00, buf + OFFSET_d, SIZE_d,
			      CTRL_TIMEOUT);
	if (ret < 0) {
		atm_dbg(usbatm, "%s: MSG D failed\n", __func__);
		return ret;
	}

	ret = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			      0x01, 0xc0, 0x0e, 0x00, buf + OFFSET_e, SIZE_e,
			      CTRL_TIMEOUT);
	if (ret < 0) {
		atm_dbg(usbatm, "%s: MSG E failed\n", __func__);
		return ret;
	}

	ret = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			      0x01, 0xc0, 0x0f, 0x00, buf + OFFSET_f, SIZE_f,
			      CTRL_TIMEOUT);
	if (ret < 0) {
		atm_dbg(usbatm, "%s: MSG F failed\n", __func__);
		return ret;
	}

	return 0;
}

static int speedtch_start_synchro(struct speedtch_instance_data *instance)
{
	struct usbatm_data *usbatm = instance->usbatm;
	struct usb_device *usb_dev = usbatm->usb_dev;
	unsigned char *buf = instance->scratch_buffer;
	int ret;

	atm_dbg(usbatm, "%s entered\n", __func__);

	memset(buf, 0, 2);

	ret = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			      0x12, 0xc0, 0x04, 0x00,
			      buf, 2, CTRL_TIMEOUT);

	if (ret < 0)
		atm_warn(usbatm, "failed to start ADSL synchronisation: %d\n", ret);
	else
		atm_dbg(usbatm, "%s: modem prodded. %d bytes returned: %02x %02x\n",
			__func__, ret, buf[0], buf[1]);

	return ret;
}

static void speedtch_check_status(struct speedtch_instance_data *instance)
{
	struct usbatm_data *usbatm = instance->usbatm;
	struct atm_dev *atm_dev = usbatm->atm_dev;
	unsigned char *buf = instance->scratch_buffer;
	int down_speed, up_speed, ret;
	unsigned char status;

#ifdef VERBOSE_DEBUG
	atm_dbg(usbatm, "%s entered\n", __func__);
#endif

	ret = speedtch_read_status(instance);
	if (ret < 0) {
		atm_warn(usbatm, "error %d fetching device status\n", ret);
		instance->poll_delay = min(2 * instance->poll_delay, MAX_POLL_DELAY);
		return;
	}

	instance->poll_delay = max(instance->poll_delay / 2, MIN_POLL_DELAY);

	status = buf[OFFSET_7];

	if ((status != instance->last_status) || !status) {
		atm_dbg(usbatm, "%s: line state 0x%02x\n", __func__, status);

		switch (status) {
		case 0:
			atm_dev->signal = ATM_PHY_SIG_LOST;
			if (instance->last_status)
				atm_info(usbatm, "ADSL line is down\n");
			/* It may never resync again unless we ask it to... */
			ret = speedtch_start_synchro(instance);
			break;

		case 0x08:
			atm_dev->signal = ATM_PHY_SIG_UNKNOWN;
			atm_info(usbatm, "ADSL line is blocked?\n");
			break;

		case 0x10:
			atm_dev->signal = ATM_PHY_SIG_LOST;
			atm_info(usbatm, "ADSL line is synchronising\n");
			break;

		case 0x20:
			down_speed = buf[OFFSET_b] | (buf[OFFSET_b + 1] << 8)
				| (buf[OFFSET_b + 2] << 16) | (buf[OFFSET_b + 3] << 24);
			up_speed = buf[OFFSET_b + 4] | (buf[OFFSET_b + 5] << 8)
				| (buf[OFFSET_b + 6] << 16) | (buf[OFFSET_b + 7] << 24);

			if (!(down_speed & 0x0000ffff) && !(up_speed & 0x0000ffff)) {
				down_speed >>= 16;
				up_speed >>= 16;
			}

			atm_dev->link_rate = down_speed * 1000 / 424;
			atm_dev->signal = ATM_PHY_SIG_FOUND;

			atm_info(usbatm,
				 "ADSL line is up (%d kb/s down | %d kb/s up)\n",
				 down_speed, up_speed);
			break;

		default:
			atm_dev->signal = ATM_PHY_SIG_UNKNOWN;
			atm_info(usbatm, "unknown line state %02x\n", status);
			break;
		}

		instance->last_status = status;
	}
}

static void speedtch_status_poll(unsigned long data)
{
	struct speedtch_instance_data *instance = (void *)data;

	schedule_work(&instance->status_checker);

	/* The following check is racy, but the race is harmless */
	if (instance->poll_delay < MAX_POLL_DELAY)
		mod_timer(&instance->status_checker.timer, jiffies + msecs_to_jiffies(instance->poll_delay));
	else
		atm_warn(instance->usbatm, "Too many failures - disabling line status polling\n");
}

static void speedtch_resubmit_int(unsigned long data)
{
	struct speedtch_instance_data *instance = (void *)data;
	struct urb *int_urb = instance->int_urb;
	int ret;

	atm_dbg(instance->usbatm, "%s entered\n", __func__);

	if (int_urb) {
		ret = usb_submit_urb(int_urb, GFP_ATOMIC);
		if (!ret)
			schedule_work(&instance->status_checker);
		else {
			atm_dbg(instance->usbatm, "%s: usb_submit_urb failed with result %d\n", __func__, ret);
			mod_timer(&instance->resubmit_timer, jiffies + msecs_to_jiffies(RESUBMIT_DELAY));
		}
	}
}

static void speedtch_handle_int(struct urb *int_urb)
{
	struct speedtch_instance_data *instance = int_urb->context;
	struct usbatm_data *usbatm = instance->usbatm;
	unsigned int count = int_urb->actual_length;
	int ret = int_urb->status;

	/* The magic interrupt for "up state" */
	static const unsigned char up_int[6]   = { 0xa1, 0x00, 0x01, 0x00, 0x00, 0x00 };
	/* The magic interrupt for "down state" */
	static const unsigned char down_int[6] = { 0xa1, 0x00, 0x00, 0x00, 0x00, 0x00 };

	atm_dbg(usbatm, "%s entered\n", __func__);

	if (ret < 0) {
		atm_dbg(usbatm, "%s: nonzero urb status %d!\n", __func__, ret);
		goto fail;
	}

	if ((count == 6) && !memcmp(up_int, instance->int_data, 6)) {
		del_timer(&instance->status_checker.timer);
		atm_info(usbatm, "DSL line goes up\n");
	} else if ((count == 6) && !memcmp(down_int, instance->int_data, 6)) {
		atm_info(usbatm, "DSL line goes down\n");
	} else {
		int i;

		atm_dbg(usbatm, "%s: unknown interrupt packet of length %d:", __func__, count);
		for (i = 0; i < count; i++)
			printk(" %02x", instance->int_data[i]);
		printk("\n");
		goto fail;
	}

	if ((int_urb = instance->int_urb)) {
		ret = usb_submit_urb(int_urb, GFP_ATOMIC);
		schedule_work(&instance->status_checker);
		if (ret < 0) {
			atm_dbg(usbatm, "%s: usb_submit_urb failed with result %d\n", __func__, ret);
			goto fail;
		}
	}

	return;

fail:
	if ((int_urb = instance->int_urb))
		mod_timer(&instance->resubmit_timer, jiffies + msecs_to_jiffies(RESUBMIT_DELAY));
}

static int speedtch_atm_start(struct usbatm_data *usbatm, struct atm_dev *atm_dev)
{
	struct usb_device *usb_dev = usbatm->usb_dev;
	struct speedtch_instance_data *instance = usbatm->driver_data;
	int i, ret;
	unsigned char mac_str[13];

	atm_dbg(usbatm, "%s entered\n", __func__);

	/* Set MAC address, it is stored in the serial number */
	memset(atm_dev->esi, 0, sizeof(atm_dev->esi));
	if (usb_string(usb_dev, usb_dev->descriptor.iSerialNumber, mac_str, sizeof(mac_str)) == 12) {
		for (i = 0; i < 6; i++)
			atm_dev->esi[i] = (hex2int(mac_str[i * 2]) * 16) + (hex2int(mac_str[i * 2 + 1]));
	}

	/* Start modem synchronisation */
	ret = speedtch_start_synchro(instance);

	/* Set up interrupt endpoint */
	if (instance->int_urb) {
		ret = usb_submit_urb(instance->int_urb, GFP_KERNEL);
		if (ret < 0) {
			/* Doesn't matter; we'll poll anyway */
			atm_dbg(usbatm, "%s: submission of interrupt URB failed (%d)!\n", __func__, ret);
			usb_free_urb(instance->int_urb);
			instance->int_urb = NULL;
		}
	}

	/* Start status polling */
	mod_timer(&instance->status_checker.timer, jiffies + msecs_to_jiffies(1000));

	return 0;
}

static void speedtch_atm_stop(struct usbatm_data *usbatm, struct atm_dev *atm_dev)
{
	struct speedtch_instance_data *instance = usbatm->driver_data;
	struct urb *int_urb = instance->int_urb;

	atm_dbg(usbatm, "%s entered\n", __func__);

	del_timer_sync(&instance->status_checker.timer);

	/*
	 * Since resubmit_timer and int_urb can schedule themselves and
	 * each other, shutting them down correctly takes some care
	 */
	instance->int_urb = NULL; /* signal shutdown */
	mb();
	usb_kill_urb(int_urb);
	del_timer_sync(&instance->resubmit_timer);
	/*
	 * At this point, speedtch_handle_int and speedtch_resubmit_int
	 * can run or be running, but instance->int_urb == NULL means that
	 * they will not reschedule
	 */
	usb_kill_urb(int_urb);
	del_timer_sync(&instance->resubmit_timer);
	usb_free_urb(int_urb);

	flush_scheduled_work();
}


/**********
**  USB  **
**********/

static struct usb_device_id speedtch_usb_ids[] = {
	{USB_DEVICE(0x06b9, 0x4061)},
	{}
};

MODULE_DEVICE_TABLE(usb, speedtch_usb_ids);

static int speedtch_usb_probe(struct usb_interface *, const struct usb_device_id *);

static struct usb_driver speedtch_usb_driver = {
	.name		= speedtch_driver_name,
	.probe		= speedtch_usb_probe,
	.disconnect	= usbatm_usb_disconnect,
	.id_table	= speedtch_usb_ids
};

static void speedtch_release_interfaces(struct usb_device *usb_dev, int num_interfaces) {
	struct usb_interface *cur_intf;
	int i;

	for(i = 0; i < num_interfaces; i++)
		if ((cur_intf = usb_ifnum_to_if(usb_dev, i))) {
			usb_set_intfdata(cur_intf, NULL);
			usb_driver_release_interface(&speedtch_usb_driver, cur_intf);
		}
}

static int speedtch_bind(struct usbatm_data *usbatm,
			 struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct usb_interface *cur_intf, *data_intf;
	struct speedtch_instance_data *instance;
	int ifnum = intf->altsetting->desc.bInterfaceNumber;
	int num_interfaces = usb_dev->actconfig->desc.bNumInterfaces;
	int i, ret;
	int use_isoc;

	usb_dbg(usbatm, "%s entered\n", __func__);

	/* sanity checks */

	if (usb_dev->descriptor.bDeviceClass != USB_CLASS_VENDOR_SPEC) {
		usb_err(usbatm, "%s: wrong device class %d\n", __func__, usb_dev->descriptor.bDeviceClass);
		return -ENODEV;
	}

	if (!(data_intf = usb_ifnum_to_if(usb_dev, INTERFACE_DATA))) {
		usb_err(usbatm, "%s: data interface not found!\n", __func__);
		return -ENODEV;
	}

	/* claim all interfaces */

	for (i=0; i < num_interfaces; i++) {
		cur_intf = usb_ifnum_to_if(usb_dev, i);

		if ((i != ifnum) && cur_intf) {
			ret = usb_driver_claim_interface(&speedtch_usb_driver, cur_intf, usbatm);

			if (ret < 0) {
				usb_err(usbatm, "%s: failed to claim interface %2d (%d)!\n", __func__, i, ret);
				speedtch_release_interfaces(usb_dev, i);
				return ret;
			}
		}
	}

	instance = kzalloc(sizeof(*instance), GFP_KERNEL);

	if (!instance) {
		usb_err(usbatm, "%s: no memory for instance data!\n", __func__);
		ret = -ENOMEM;
		goto fail_release;
	}

	instance->usbatm = usbatm;

	/* module parameters may change at any moment, so take a snapshot */
	instance->params.altsetting = altsetting;
	instance->params.BMaxDSL = BMaxDSL;
	instance->params.ModemMode = ModemMode;
	memcpy(instance->params.ModemOption, DEFAULT_MODEM_OPTION, MODEM_OPTION_LENGTH);
	memcpy(instance->params.ModemOption, ModemOption, num_ModemOption);
	use_isoc = enable_isoc;

	if (instance->params.altsetting)
		if ((ret = usb_set_interface(usb_dev, INTERFACE_DATA, instance->params.altsetting)) < 0) {
			usb_err(usbatm, "%s: setting interface to %2d failed (%d)!\n", __func__, instance->params.altsetting, ret);
			instance->params.altsetting = 0; /* fall back to default */
		}

	if (!instance->params.altsetting && use_isoc)
		if ((ret = usb_set_interface(usb_dev, INTERFACE_DATA, DEFAULT_ISOC_ALTSETTING)) < 0) {
			usb_dbg(usbatm, "%s: setting interface to %2d failed (%d)!\n", __func__, DEFAULT_ISOC_ALTSETTING, ret);
			use_isoc = 0; /* fall back to bulk */
		}

	if (use_isoc) {
		const struct usb_host_interface *desc = data_intf->cur_altsetting;
		const __u8 target_address = USB_DIR_IN | usbatm->driver->isoc_in;
		int i;

		use_isoc = 0; /* fall back to bulk if endpoint not found */

		for (i=0; i<desc->desc.bNumEndpoints; i++) {
			const struct usb_endpoint_descriptor *endpoint_desc = &desc->endpoint[i].desc;

			if ((endpoint_desc->bEndpointAddress == target_address)) {
				use_isoc =
					usb_endpoint_xfer_isoc(endpoint_desc);
				break;
			}
		}

		if (!use_isoc)
			usb_info(usbatm, "isochronous transfer not supported - using bulk\n");
	}

	if (!use_isoc && !instance->params.altsetting)
		if ((ret = usb_set_interface(usb_dev, INTERFACE_DATA, DEFAULT_BULK_ALTSETTING)) < 0) {
			usb_err(usbatm, "%s: setting interface to %2d failed (%d)!\n", __func__, DEFAULT_BULK_ALTSETTING, ret);
			goto fail_free;
		}

	if (!instance->params.altsetting)
		instance->params.altsetting = use_isoc ? DEFAULT_ISOC_ALTSETTING : DEFAULT_BULK_ALTSETTING;

	usbatm->flags |= (use_isoc ? UDSL_USE_ISOC : 0);

	INIT_WORK(&instance->status_checker, (void *)speedtch_check_status, instance);

	instance->status_checker.timer.function = speedtch_status_poll;
	instance->status_checker.timer.data = (unsigned long)instance;
	instance->last_status = 0xff;
	instance->poll_delay = MIN_POLL_DELAY;

	init_timer(&instance->resubmit_timer);
	instance->resubmit_timer.function = speedtch_resubmit_int;
	instance->resubmit_timer.data = (unsigned long)instance;

	instance->int_urb = usb_alloc_urb(0, GFP_KERNEL);

	if (instance->int_urb)
		usb_fill_int_urb(instance->int_urb, usb_dev,
				 usb_rcvintpipe(usb_dev, ENDPOINT_INT),
				 instance->int_data, sizeof(instance->int_data),
				 speedtch_handle_int, instance, 50);
	else
		usb_dbg(usbatm, "%s: no memory for interrupt urb!\n", __func__);

	/* check whether the modem already seems to be alive */
	ret = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			      0x12, 0xc0, 0x07, 0x00,
			      instance->scratch_buffer + OFFSET_7, SIZE_7, 500);

	usbatm->flags |= (ret == SIZE_7 ? UDSL_SKIP_HEAVY_INIT : 0);

	usb_dbg(usbatm, "%s: firmware %s loaded\n", __func__, usbatm->flags & UDSL_SKIP_HEAVY_INIT ? "already" : "not");

	if (!(usbatm->flags & UDSL_SKIP_HEAVY_INIT))
		if ((ret = usb_reset_device(usb_dev)) < 0) {
			usb_err(usbatm, "%s: device reset failed (%d)!\n", __func__, ret);
			goto fail_free;
		}

        usbatm->driver_data = instance;

	return 0;

fail_free:
	usb_free_urb(instance->int_urb);
	kfree(instance);
fail_release:
	speedtch_release_interfaces(usb_dev, num_interfaces);
	return ret;
}

static void speedtch_unbind(struct usbatm_data *usbatm, struct usb_interface *intf)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct speedtch_instance_data *instance = usbatm->driver_data;

	usb_dbg(usbatm, "%s entered\n", __func__);

	speedtch_release_interfaces(usb_dev, usb_dev->actconfig->desc.bNumInterfaces);
	usb_free_urb(instance->int_urb);
	kfree(instance);
}


/***********
**  init  **
***********/

static struct usbatm_driver speedtch_usbatm_driver = {
	.driver_name	= speedtch_driver_name,
	.bind		= speedtch_bind,
	.heavy_init	= speedtch_heavy_init,
	.unbind		= speedtch_unbind,
	.atm_start	= speedtch_atm_start,
	.atm_stop	= speedtch_atm_stop,
	.bulk_in	= ENDPOINT_BULK_DATA,
	.bulk_out	= ENDPOINT_BULK_DATA,
	.isoc_in	= ENDPOINT_ISOC_DATA
};

static int speedtch_usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	return usbatm_usb_probe(intf, id, &speedtch_usbatm_driver);
}

static int __init speedtch_usb_init(void)
{
	dbg("%s: driver version %s", __func__, DRIVER_VERSION);

	return usb_register(&speedtch_usb_driver);
}

static void __exit speedtch_usb_cleanup(void)
{
	dbg("%s", __func__);

	usb_deregister(&speedtch_usb_driver);
}

module_init(speedtch_usb_init);
module_exit(speedtch_usb_cleanup);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);
