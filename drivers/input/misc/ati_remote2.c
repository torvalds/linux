/*
 * ati_remote2 - ATI/Philips USB RF remote driver
 *
 * Copyright (C) 2005-2008 Ville Syrjala <syrjala@sci.fi>
 * Copyright (C) 2007-2008 Peter Stokes <linux@dadeos.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#include <linux/usb/input.h>

#define DRIVER_DESC    "ATI/Philips USB RF remote driver"
#define DRIVER_VERSION "0.3"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR("Ville Syrjala <syrjala@sci.fi>");
MODULE_LICENSE("GPL");

/*
 * ATI Remote Wonder II Channel Configuration
 *
 * The remote control can by assigned one of sixteen "channels" in order to facilitate
 * the use of multiple remote controls within range of each other.
 * A remote's "channel" may be altered by pressing and holding the "PC" button for
 * approximately 3 seconds, after which the button will slowly flash the count of the
 * currently configured "channel", using the numeric keypad enter a number between 1 and
 * 16 and then press the "PC" button again, the button will slowly flash the count of the
 * newly configured "channel".
 */

enum {
	ATI_REMOTE2_MAX_CHANNEL_MASK = 0xFFFF,
	ATI_REMOTE2_MAX_MODE_MASK = 0x1F,
};

static int ati_remote2_set_mask(const char *val,
				struct kernel_param *kp, unsigned int max)
{
	unsigned long mask;
	int ret;

	if (!val)
		return -EINVAL;

	ret = strict_strtoul(val, 0, &mask);
	if (ret)
		return ret;

	if (mask & ~max)
		return -EINVAL;

	*(unsigned int *)kp->arg = mask;

	return 0;
}

static int ati_remote2_set_channel_mask(const char *val,
					struct kernel_param *kp)
{
	pr_debug("%s()\n", __func__);

	return ati_remote2_set_mask(val, kp, ATI_REMOTE2_MAX_CHANNEL_MASK);
}

static int ati_remote2_get_channel_mask(char *buffer, struct kernel_param *kp)
{
	pr_debug("%s()\n", __func__);

	return sprintf(buffer, "0x%04x", *(unsigned int *)kp->arg);
}

static int ati_remote2_set_mode_mask(const char *val, struct kernel_param *kp)
{
	pr_debug("%s()\n", __func__);

	return ati_remote2_set_mask(val, kp, ATI_REMOTE2_MAX_MODE_MASK);
}

static int ati_remote2_get_mode_mask(char *buffer, struct kernel_param *kp)
{
	pr_debug("%s()\n", __func__);

	return sprintf(buffer, "0x%02x", *(unsigned int *)kp->arg);
}

static unsigned int channel_mask = ATI_REMOTE2_MAX_CHANNEL_MASK;
#define param_check_channel_mask(name, p) __param_check(name, p, unsigned int)
#define param_set_channel_mask ati_remote2_set_channel_mask
#define param_get_channel_mask ati_remote2_get_channel_mask
module_param(channel_mask, channel_mask, 0644);
MODULE_PARM_DESC(channel_mask, "Bitmask of channels to accept <15:Channel16>...<1:Channel2><0:Channel1>");

static unsigned int mode_mask = ATI_REMOTE2_MAX_MODE_MASK;
#define param_check_mode_mask(name, p) __param_check(name, p, unsigned int)
#define param_set_mode_mask ati_remote2_set_mode_mask
#define param_get_mode_mask ati_remote2_get_mode_mask
module_param(mode_mask, mode_mask, 0644);
MODULE_PARM_DESC(mode_mask, "Bitmask of modes to accept <4:PC><3:AUX4><2:AUX3><1:AUX2><0:AUX1>");

static struct usb_device_id ati_remote2_id_table[] = {
	{ USB_DEVICE(0x0471, 0x0602) },	/* ATI Remote Wonder II */
	{ }
};
MODULE_DEVICE_TABLE(usb, ati_remote2_id_table);

static DEFINE_MUTEX(ati_remote2_mutex);

enum {
	ATI_REMOTE2_OPENED = 0x1,
	ATI_REMOTE2_SUSPENDED = 0x2,
};

enum {
	ATI_REMOTE2_AUX1,
	ATI_REMOTE2_AUX2,
	ATI_REMOTE2_AUX3,
	ATI_REMOTE2_AUX4,
	ATI_REMOTE2_PC,
	ATI_REMOTE2_MODES,
};

static const struct {
	u8  hw_code;
	u16 keycode;
} ati_remote2_key_table[] = {
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },
	{ 0x0c, KEY_POWER },
	{ 0x0d, KEY_MUTE },
	{ 0x10, KEY_VOLUMEUP },
	{ 0x11, KEY_VOLUMEDOWN },
	{ 0x20, KEY_CHANNELUP },
	{ 0x21, KEY_CHANNELDOWN },
	{ 0x28, KEY_FORWARD },
	{ 0x29, KEY_REWIND },
	{ 0x2c, KEY_PLAY },
	{ 0x30, KEY_PAUSE },
	{ 0x31, KEY_STOP },
	{ 0x37, KEY_RECORD },
	{ 0x38, KEY_DVD },
	{ 0x39, KEY_TV },
	{ 0x3f, KEY_PROG1 }, /* AUX1-AUX4 and PC */
	{ 0x54, KEY_MENU },
	{ 0x58, KEY_UP },
	{ 0x59, KEY_DOWN },
	{ 0x5a, KEY_LEFT },
	{ 0x5b, KEY_RIGHT },
	{ 0x5c, KEY_OK },
	{ 0x78, KEY_A },
	{ 0x79, KEY_B },
	{ 0x7a, KEY_C },
	{ 0x7b, KEY_D },
	{ 0x7c, KEY_E },
	{ 0x7d, KEY_F },
	{ 0x82, KEY_ENTER },
	{ 0x8e, KEY_VENDOR },
	{ 0x96, KEY_COFFEE },
	{ 0xa9, BTN_LEFT },
	{ 0xaa, BTN_RIGHT },
	{ 0xbe, KEY_QUESTION },
	{ 0xd0, KEY_EDIT },
	{ 0xd5, KEY_FRONT },
	{ 0xf9, KEY_INFO },
};

struct ati_remote2 {
	struct input_dev *idev;
	struct usb_device *udev;

	struct usb_interface *intf[2];
	struct usb_endpoint_descriptor *ep[2];
	struct urb *urb[2];
	void *buf[2];
	dma_addr_t buf_dma[2];

	unsigned long jiffies;
	int mode;

	char name[64];
	char phys[64];

	/* Each mode (AUX1-AUX4 and PC) can have an independent keymap. */
	u16 keycode[ATI_REMOTE2_MODES][ARRAY_SIZE(ati_remote2_key_table)];

	unsigned int flags;

	unsigned int channel_mask;
	unsigned int mode_mask;
};

static int ati_remote2_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void ati_remote2_disconnect(struct usb_interface *interface);
static int ati_remote2_suspend(struct usb_interface *interface, pm_message_t message);
static int ati_remote2_resume(struct usb_interface *interface);
static int ati_remote2_reset_resume(struct usb_interface *interface);
static int ati_remote2_pre_reset(struct usb_interface *interface);
static int ati_remote2_post_reset(struct usb_interface *interface);

static struct usb_driver ati_remote2_driver = {
	.name       = "ati_remote2",
	.probe      = ati_remote2_probe,
	.disconnect = ati_remote2_disconnect,
	.id_table   = ati_remote2_id_table,
	.suspend    = ati_remote2_suspend,
	.resume     = ati_remote2_resume,
	.reset_resume = ati_remote2_reset_resume,
	.pre_reset  = ati_remote2_pre_reset,
	.post_reset = ati_remote2_post_reset,
	.supports_autosuspend = 1,
};

static int ati_remote2_submit_urbs(struct ati_remote2 *ar2)
{
	int r;

	r = usb_submit_urb(ar2->urb[0], GFP_KERNEL);
	if (r) {
		dev_err(&ar2->intf[0]->dev,
			"%s(): usb_submit_urb() = %d\n", __func__, r);
		return r;
	}
	r = usb_submit_urb(ar2->urb[1], GFP_KERNEL);
	if (r) {
		usb_kill_urb(ar2->urb[0]);
		dev_err(&ar2->intf[1]->dev,
			"%s(): usb_submit_urb() = %d\n", __func__, r);
		return r;
	}

	return 0;
}

static void ati_remote2_kill_urbs(struct ati_remote2 *ar2)
{
	usb_kill_urb(ar2->urb[1]);
	usb_kill_urb(ar2->urb[0]);
}

static int ati_remote2_open(struct input_dev *idev)
{
	struct ati_remote2 *ar2 = input_get_drvdata(idev);
	int r;

	dev_dbg(&ar2->intf[0]->dev, "%s()\n", __func__);

	r = usb_autopm_get_interface(ar2->intf[0]);
	if (r) {
		dev_err(&ar2->intf[0]->dev,
			"%s(): usb_autopm_get_interface() = %d\n", __func__, r);
		goto fail1;
	}

	mutex_lock(&ati_remote2_mutex);

	if (!(ar2->flags & ATI_REMOTE2_SUSPENDED)) {
		r = ati_remote2_submit_urbs(ar2);
		if (r)
			goto fail2;
	}

	ar2->flags |= ATI_REMOTE2_OPENED;

	mutex_unlock(&ati_remote2_mutex);

	usb_autopm_put_interface(ar2->intf[0]);

	return 0;

 fail2:
	mutex_unlock(&ati_remote2_mutex);
	usb_autopm_put_interface(ar2->intf[0]);
 fail1:
	return r;
}

static void ati_remote2_close(struct input_dev *idev)
{
	struct ati_remote2 *ar2 = input_get_drvdata(idev);

	dev_dbg(&ar2->intf[0]->dev, "%s()\n", __func__);

	mutex_lock(&ati_remote2_mutex);

	if (!(ar2->flags & ATI_REMOTE2_SUSPENDED))
		ati_remote2_kill_urbs(ar2);

	ar2->flags &= ~ATI_REMOTE2_OPENED;

	mutex_unlock(&ati_remote2_mutex);
}

static void ati_remote2_input_mouse(struct ati_remote2 *ar2)
{
	struct input_dev *idev = ar2->idev;
	u8 *data = ar2->buf[0];
	int channel, mode;

	channel = data[0] >> 4;

	if (!((1 << channel) & ar2->channel_mask))
		return;

	mode = data[0] & 0x0F;

	if (mode > ATI_REMOTE2_PC) {
		dev_err(&ar2->intf[0]->dev,
			"Unknown mode byte (%02x %02x %02x %02x)\n",
			data[3], data[2], data[1], data[0]);
		return;
	}

	if (!((1 << mode) & ar2->mode_mask))
		return;

	input_event(idev, EV_REL, REL_X, (s8) data[1]);
	input_event(idev, EV_REL, REL_Y, (s8) data[2]);
	input_sync(idev);
}

static int ati_remote2_lookup(unsigned int hw_code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ati_remote2_key_table); i++)
		if (ati_remote2_key_table[i].hw_code == hw_code)
			return i;

	return -1;
}

static void ati_remote2_input_key(struct ati_remote2 *ar2)
{
	struct input_dev *idev = ar2->idev;
	u8 *data = ar2->buf[1];
	int channel, mode, hw_code, index;

	channel = data[0] >> 4;

	if (!((1 << channel) & ar2->channel_mask))
		return;

	mode = data[0] & 0x0F;

	if (mode > ATI_REMOTE2_PC) {
		dev_err(&ar2->intf[1]->dev,
			"Unknown mode byte (%02x %02x %02x %02x)\n",
			data[3], data[2], data[1], data[0]);
		return;
	}

	hw_code = data[2];
	if (hw_code == 0x3f) {
		/*
		 * For some incomprehensible reason the mouse pad generates
		 * events which look identical to the events from the last
		 * pressed mode key. Naturally we don't want to generate key
		 * events for the mouse pad so we filter out any subsequent
		 * events from the same mode key.
		 */
		if (ar2->mode == mode)
			return;

		if (data[1] == 0)
			ar2->mode = mode;
	}

	if (!((1 << mode) & ar2->mode_mask))
		return;

	index = ati_remote2_lookup(hw_code);
	if (index < 0) {
		dev_err(&ar2->intf[1]->dev,
			"Unknown code byte (%02x %02x %02x %02x)\n",
			data[3], data[2], data[1], data[0]);
		return;
	}

	switch (data[1]) {
	case 0:	/* release */
		break;
	case 1:	/* press */
		ar2->jiffies = jiffies + msecs_to_jiffies(idev->rep[REP_DELAY]);
		break;
	case 2:	/* repeat */

		/* No repeat for mouse buttons. */
		if (ar2->keycode[mode][index] == BTN_LEFT ||
		    ar2->keycode[mode][index] == BTN_RIGHT)
			return;

		if (!time_after_eq(jiffies, ar2->jiffies))
			return;

		ar2->jiffies = jiffies + msecs_to_jiffies(idev->rep[REP_PERIOD]);
		break;
	default:
		dev_err(&ar2->intf[1]->dev,
			"Unknown state byte (%02x %02x %02x %02x)\n",
			data[3], data[2], data[1], data[0]);
		return;
	}

	input_event(idev, EV_KEY, ar2->keycode[mode][index], data[1]);
	input_sync(idev);
}

static void ati_remote2_complete_mouse(struct urb *urb)
{
	struct ati_remote2 *ar2 = urb->context;
	int r;

	switch (urb->status) {
	case 0:
		usb_mark_last_busy(ar2->udev);
		ati_remote2_input_mouse(ar2);
		break;
	case -ENOENT:
	case -EILSEQ:
	case -ECONNRESET:
	case -ESHUTDOWN:
		dev_dbg(&ar2->intf[0]->dev,
			"%s(): urb status = %d\n", __func__, urb->status);
		return;
	default:
		usb_mark_last_busy(ar2->udev);
		dev_err(&ar2->intf[0]->dev,
			"%s(): urb status = %d\n", __func__, urb->status);
	}

	r = usb_submit_urb(urb, GFP_ATOMIC);
	if (r)
		dev_err(&ar2->intf[0]->dev,
			"%s(): usb_submit_urb() = %d\n", __func__, r);
}

static void ati_remote2_complete_key(struct urb *urb)
{
	struct ati_remote2 *ar2 = urb->context;
	int r;

	switch (urb->status) {
	case 0:
		usb_mark_last_busy(ar2->udev);
		ati_remote2_input_key(ar2);
		break;
	case -ENOENT:
	case -EILSEQ:
	case -ECONNRESET:
	case -ESHUTDOWN:
		dev_dbg(&ar2->intf[1]->dev,
			"%s(): urb status = %d\n", __func__, urb->status);
		return;
	default:
		usb_mark_last_busy(ar2->udev);
		dev_err(&ar2->intf[1]->dev,
			"%s(): urb status = %d\n", __func__, urb->status);
	}

	r = usb_submit_urb(urb, GFP_ATOMIC);
	if (r)
		dev_err(&ar2->intf[1]->dev,
			"%s(): usb_submit_urb() = %d\n", __func__, r);
}

static int ati_remote2_getkeycode(struct input_dev *idev,
				  int scancode, int *keycode)
{
	struct ati_remote2 *ar2 = input_get_drvdata(idev);
	int index, mode;

	mode = scancode >> 8;
	if (mode > ATI_REMOTE2_PC || !((1 << mode) & ar2->mode_mask))
		return -EINVAL;

	index = ati_remote2_lookup(scancode & 0xFF);
	if (index < 0)
		return -EINVAL;

	*keycode = ar2->keycode[mode][index];
	return 0;
}

static int ati_remote2_setkeycode(struct input_dev *idev, int scancode, int keycode)
{
	struct ati_remote2 *ar2 = input_get_drvdata(idev);
	int index, mode, old_keycode;

	mode = scancode >> 8;
	if (mode > ATI_REMOTE2_PC || !((1 << mode) & ar2->mode_mask))
		return -EINVAL;

	index = ati_remote2_lookup(scancode & 0xFF);
	if (index < 0)
		return -EINVAL;

	if (keycode < KEY_RESERVED || keycode > KEY_MAX)
		return -EINVAL;

	old_keycode = ar2->keycode[mode][index];
	ar2->keycode[mode][index] = keycode;
	__set_bit(keycode, idev->keybit);

	for (mode = 0; mode < ATI_REMOTE2_MODES; mode++) {
		for (index = 0; index < ARRAY_SIZE(ati_remote2_key_table); index++) {
			if (ar2->keycode[mode][index] == old_keycode)
				return 0;
		}
	}

	__clear_bit(old_keycode, idev->keybit);

	return 0;
}

static int ati_remote2_input_init(struct ati_remote2 *ar2)
{
	struct input_dev *idev;
	int index, mode, retval;

	idev = input_allocate_device();
	if (!idev)
		return -ENOMEM;

	ar2->idev = idev;
	input_set_drvdata(idev, ar2);

	idev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP) | BIT_MASK(EV_REL);
	idev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) |
		BIT_MASK(BTN_RIGHT);
	idev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);

	for (mode = 0; mode < ATI_REMOTE2_MODES; mode++) {
		for (index = 0; index < ARRAY_SIZE(ati_remote2_key_table); index++) {
			ar2->keycode[mode][index] = ati_remote2_key_table[index].keycode;
			__set_bit(ar2->keycode[mode][index], idev->keybit);
		}
	}

	/* AUX1-AUX4 and PC generate the same scancode. */
	index = ati_remote2_lookup(0x3f);
	ar2->keycode[ATI_REMOTE2_AUX1][index] = KEY_PROG1;
	ar2->keycode[ATI_REMOTE2_AUX2][index] = KEY_PROG2;
	ar2->keycode[ATI_REMOTE2_AUX3][index] = KEY_PROG3;
	ar2->keycode[ATI_REMOTE2_AUX4][index] = KEY_PROG4;
	ar2->keycode[ATI_REMOTE2_PC][index] = KEY_PC;
	__set_bit(KEY_PROG1, idev->keybit);
	__set_bit(KEY_PROG2, idev->keybit);
	__set_bit(KEY_PROG3, idev->keybit);
	__set_bit(KEY_PROG4, idev->keybit);
	__set_bit(KEY_PC, idev->keybit);

	idev->rep[REP_DELAY]  = 250;
	idev->rep[REP_PERIOD] = 33;

	idev->open = ati_remote2_open;
	idev->close = ati_remote2_close;

	idev->getkeycode = ati_remote2_getkeycode;
	idev->setkeycode = ati_remote2_setkeycode;

	idev->name = ar2->name;
	idev->phys = ar2->phys;

	usb_to_input_id(ar2->udev, &idev->id);
	idev->dev.parent = &ar2->udev->dev;

	retval = input_register_device(idev);
	if (retval)
		input_free_device(idev);

	return retval;
}

static int ati_remote2_urb_init(struct ati_remote2 *ar2)
{
	struct usb_device *udev = ar2->udev;
	int i, pipe, maxp;

	for (i = 0; i < 2; i++) {
		ar2->buf[i] = usb_buffer_alloc(udev, 4, GFP_KERNEL, &ar2->buf_dma[i]);
		if (!ar2->buf[i])
			return -ENOMEM;

		ar2->urb[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!ar2->urb[i])
			return -ENOMEM;

		pipe = usb_rcvintpipe(udev, ar2->ep[i]->bEndpointAddress);
		maxp = usb_maxpacket(udev, pipe, usb_pipeout(pipe));
		maxp = maxp > 4 ? 4 : maxp;

		usb_fill_int_urb(ar2->urb[i], udev, pipe, ar2->buf[i], maxp,
				 i ? ati_remote2_complete_key : ati_remote2_complete_mouse,
				 ar2, ar2->ep[i]->bInterval);
		ar2->urb[i]->transfer_dma = ar2->buf_dma[i];
		ar2->urb[i]->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}

	return 0;
}

static void ati_remote2_urb_cleanup(struct ati_remote2 *ar2)
{
	int i;

	for (i = 0; i < 2; i++) {
		usb_free_urb(ar2->urb[i]);
		usb_buffer_free(ar2->udev, 4, ar2->buf[i], ar2->buf_dma[i]);
	}
}

static int ati_remote2_setup(struct ati_remote2 *ar2, unsigned int ch_mask)
{
	int r, i, channel;

	/*
	 * Configure receiver to only accept input from remote "channel"
	 *  channel == 0  -> Accept input from any remote channel
	 *  channel == 1  -> Only accept input from remote channel 1
	 *  channel == 2  -> Only accept input from remote channel 2
	 *  ...
	 *  channel == 16 -> Only accept input from remote channel 16
	 */

	channel = 0;
	for (i = 0; i < 16; i++) {
		if ((1 << i) & ch_mask) {
			if (!(~(1 << i) & ch_mask))
				channel = i + 1;
			break;
		}
	}

	r = usb_control_msg(ar2->udev, usb_sndctrlpipe(ar2->udev, 0),
			    0x20,
			    USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			    channel, 0x0, NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (r) {
		dev_err(&ar2->udev->dev, "%s - failed to set channel due to error: %d\n",
			__func__, r);
		return r;
	}

	return 0;
}

static ssize_t ati_remote2_show_channel_mask(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct usb_device *udev = to_usb_device(dev);
	struct usb_interface *intf = usb_ifnum_to_if(udev, 0);
	struct ati_remote2 *ar2 = usb_get_intfdata(intf);

	return sprintf(buf, "0x%04x\n", ar2->channel_mask);
}

static ssize_t ati_remote2_store_channel_mask(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	struct usb_device *udev = to_usb_device(dev);
	struct usb_interface *intf = usb_ifnum_to_if(udev, 0);
	struct ati_remote2 *ar2 = usb_get_intfdata(intf);
	unsigned long mask;
	int r;

	if (strict_strtoul(buf, 0, &mask))
		return -EINVAL;

	if (mask & ~ATI_REMOTE2_MAX_CHANNEL_MASK)
		return -EINVAL;

	r = usb_autopm_get_interface(ar2->intf[0]);
	if (r) {
		dev_err(&ar2->intf[0]->dev,
			"%s(): usb_autopm_get_interface() = %d\n", __func__, r);
		return r;
	}

	mutex_lock(&ati_remote2_mutex);

	if (mask != ar2->channel_mask && !ati_remote2_setup(ar2, mask))
		ar2->channel_mask = mask;

	mutex_unlock(&ati_remote2_mutex);

	usb_autopm_put_interface(ar2->intf[0]);

	return count;
}

static ssize_t ati_remote2_show_mode_mask(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct usb_device *udev = to_usb_device(dev);
	struct usb_interface *intf = usb_ifnum_to_if(udev, 0);
	struct ati_remote2 *ar2 = usb_get_intfdata(intf);

	return sprintf(buf, "0x%02x\n", ar2->mode_mask);
}

static ssize_t ati_remote2_store_mode_mask(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct usb_device *udev = to_usb_device(dev);
	struct usb_interface *intf = usb_ifnum_to_if(udev, 0);
	struct ati_remote2 *ar2 = usb_get_intfdata(intf);
	unsigned long mask;

	if (strict_strtoul(buf, 0, &mask))
		return -EINVAL;

	if (mask & ~ATI_REMOTE2_MAX_MODE_MASK)
		return -EINVAL;

	ar2->mode_mask = mask;

	return count;
}

static DEVICE_ATTR(channel_mask, 0644, ati_remote2_show_channel_mask,
		   ati_remote2_store_channel_mask);

static DEVICE_ATTR(mode_mask, 0644, ati_remote2_show_mode_mask,
		   ati_remote2_store_mode_mask);

static struct attribute *ati_remote2_attrs[] = {
	&dev_attr_channel_mask.attr,
	&dev_attr_mode_mask.attr,
	NULL,
};

static struct attribute_group ati_remote2_attr_group = {
	.attrs = ati_remote2_attrs,
};

static int ati_remote2_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_host_interface *alt = interface->cur_altsetting;
	struct ati_remote2 *ar2;
	int r;

	if (alt->desc.bInterfaceNumber)
		return -ENODEV;

	ar2 = kzalloc(sizeof (struct ati_remote2), GFP_KERNEL);
	if (!ar2)
		return -ENOMEM;

	ar2->udev = udev;

	ar2->intf[0] = interface;
	ar2->ep[0] = &alt->endpoint[0].desc;

	ar2->intf[1] = usb_ifnum_to_if(udev, 1);
	r = usb_driver_claim_interface(&ati_remote2_driver, ar2->intf[1], ar2);
	if (r)
		goto fail1;
	alt = ar2->intf[1]->cur_altsetting;
	ar2->ep[1] = &alt->endpoint[0].desc;

	r = ati_remote2_urb_init(ar2);
	if (r)
		goto fail2;

	ar2->channel_mask = channel_mask;
	ar2->mode_mask = mode_mask;

	r = ati_remote2_setup(ar2, ar2->channel_mask);
	if (r)
		goto fail2;

	usb_make_path(udev, ar2->phys, sizeof(ar2->phys));
	strlcat(ar2->phys, "/input0", sizeof(ar2->phys));

	strlcat(ar2->name, "ATI Remote Wonder II", sizeof(ar2->name));

	r = sysfs_create_group(&udev->dev.kobj, &ati_remote2_attr_group);
	if (r)
		goto fail2;

	r = ati_remote2_input_init(ar2);
	if (r)
		goto fail3;

	usb_set_intfdata(interface, ar2);

	interface->needs_remote_wakeup = 1;

	return 0;

 fail3:
	sysfs_remove_group(&udev->dev.kobj, &ati_remote2_attr_group);
 fail2:
	ati_remote2_urb_cleanup(ar2);
	usb_driver_release_interface(&ati_remote2_driver, ar2->intf[1]);
 fail1:
	kfree(ar2);

	return r;
}

static void ati_remote2_disconnect(struct usb_interface *interface)
{
	struct ati_remote2 *ar2;
	struct usb_host_interface *alt = interface->cur_altsetting;

	if (alt->desc.bInterfaceNumber)
		return;

	ar2 = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	input_unregister_device(ar2->idev);

	sysfs_remove_group(&ar2->udev->dev.kobj, &ati_remote2_attr_group);

	ati_remote2_urb_cleanup(ar2);

	usb_driver_release_interface(&ati_remote2_driver, ar2->intf[1]);

	kfree(ar2);
}

static int ati_remote2_suspend(struct usb_interface *interface,
			       pm_message_t message)
{
	struct ati_remote2 *ar2;
	struct usb_host_interface *alt = interface->cur_altsetting;

	if (alt->desc.bInterfaceNumber)
		return 0;

	ar2 = usb_get_intfdata(interface);

	dev_dbg(&ar2->intf[0]->dev, "%s()\n", __func__);

	mutex_lock(&ati_remote2_mutex);

	if (ar2->flags & ATI_REMOTE2_OPENED)
		ati_remote2_kill_urbs(ar2);

	ar2->flags |= ATI_REMOTE2_SUSPENDED;

	mutex_unlock(&ati_remote2_mutex);

	return 0;
}

static int ati_remote2_resume(struct usb_interface *interface)
{
	struct ati_remote2 *ar2;
	struct usb_host_interface *alt = interface->cur_altsetting;
	int r = 0;

	if (alt->desc.bInterfaceNumber)
		return 0;

	ar2 = usb_get_intfdata(interface);

	dev_dbg(&ar2->intf[0]->dev, "%s()\n", __func__);

	mutex_lock(&ati_remote2_mutex);

	if (ar2->flags & ATI_REMOTE2_OPENED)
		r = ati_remote2_submit_urbs(ar2);

	if (!r)
		ar2->flags &= ~ATI_REMOTE2_SUSPENDED;

	mutex_unlock(&ati_remote2_mutex);

	return r;
}

static int ati_remote2_reset_resume(struct usb_interface *interface)
{
	struct ati_remote2 *ar2;
	struct usb_host_interface *alt = interface->cur_altsetting;
	int r = 0;

	if (alt->desc.bInterfaceNumber)
		return 0;

	ar2 = usb_get_intfdata(interface);

	dev_dbg(&ar2->intf[0]->dev, "%s()\n", __func__);

	mutex_lock(&ati_remote2_mutex);

	r = ati_remote2_setup(ar2, ar2->channel_mask);
	if (r)
		goto out;

	if (ar2->flags & ATI_REMOTE2_OPENED)
		r = ati_remote2_submit_urbs(ar2);

	if (!r)
		ar2->flags &= ~ATI_REMOTE2_SUSPENDED;

 out:
	mutex_unlock(&ati_remote2_mutex);

	return r;
}

static int ati_remote2_pre_reset(struct usb_interface *interface)
{
	struct ati_remote2 *ar2;
	struct usb_host_interface *alt = interface->cur_altsetting;

	if (alt->desc.bInterfaceNumber)
		return 0;

	ar2 = usb_get_intfdata(interface);

	dev_dbg(&ar2->intf[0]->dev, "%s()\n", __func__);

	mutex_lock(&ati_remote2_mutex);

	if (ar2->flags == ATI_REMOTE2_OPENED)
		ati_remote2_kill_urbs(ar2);

	return 0;
}

static int ati_remote2_post_reset(struct usb_interface *interface)
{
	struct ati_remote2 *ar2;
	struct usb_host_interface *alt = interface->cur_altsetting;
	int r = 0;

	if (alt->desc.bInterfaceNumber)
		return 0;

	ar2 = usb_get_intfdata(interface);

	dev_dbg(&ar2->intf[0]->dev, "%s()\n", __func__);

	if (ar2->flags == ATI_REMOTE2_OPENED)
		r = ati_remote2_submit_urbs(ar2);

	mutex_unlock(&ati_remote2_mutex);

	return r;
}

static int __init ati_remote2_init(void)
{
	int r;

	r = usb_register(&ati_remote2_driver);
	if (r)
		printk(KERN_ERR "ati_remote2: usb_register() = %d\n", r);
	else
		printk(KERN_INFO "ati_remote2: " DRIVER_DESC " " DRIVER_VERSION "\n");

	return r;
}

static void __exit ati_remote2_exit(void)
{
	usb_deregister(&ati_remote2_driver);
}

module_init(ati_remote2_init);
module_exit(ati_remote2_exit);
