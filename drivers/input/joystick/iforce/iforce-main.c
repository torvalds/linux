/*
 * $Id: iforce-main.c,v 1.19 2002/07/07 10:22:50 jdeneux Exp $
 *
 *  Copyright (c) 2000-2002 Vojtech Pavlik <vojtech@ucw.cz>
 *  Copyright (c) 2001-2002 Johann Deneux <deneux@ifrance.com>
 *
 *  USB/RS232 I-Force joysticks and wheels.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include "iforce.h"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>, Johann Deneux <deneux@ifrance.com>");
MODULE_DESCRIPTION("USB/RS232 I-Force joysticks and wheels driver");
MODULE_LICENSE("GPL");

static signed short btn_joystick[] =
{ BTN_TRIGGER, BTN_TOP, BTN_THUMB, BTN_TOP2, BTN_BASE,
  BTN_BASE2, BTN_BASE3, BTN_BASE4, BTN_BASE5, BTN_A, BTN_B, BTN_C, -1 };

static signed short btn_avb_pegasus[] =
{ BTN_TRIGGER, BTN_TOP, BTN_THUMB, BTN_TOP2, BTN_BASE,
  BTN_BASE2, BTN_BASE3, BTN_BASE4, -1 };

static signed short btn_wheel[] =
{ BTN_TRIGGER, BTN_TOP, BTN_THUMB, BTN_TOP2, BTN_BASE,
  BTN_BASE2, BTN_BASE3, BTN_BASE4, BTN_BASE5, BTN_A, BTN_B, BTN_C, -1 };

static signed short btn_avb_tw[] =
{ BTN_TRIGGER, BTN_THUMB, BTN_TOP, BTN_TOP2, BTN_BASE,
  BTN_BASE2, BTN_BASE3, BTN_BASE4, -1 };

static signed short btn_avb_wheel[] =
{ BTN_GEAR_DOWN, BTN_GEAR_UP, BTN_BASE, BTN_BASE2, BTN_BASE3,
  BTN_BASE4, BTN_BASE5, BTN_BASE6, -1 };

static signed short abs_joystick[] =
{ ABS_X, ABS_Y, ABS_THROTTLE, ABS_HAT0X, ABS_HAT0Y, -1 };

static signed short abs_avb_pegasus[] =
{ ABS_X, ABS_Y, ABS_THROTTLE, ABS_RUDDER, ABS_HAT0X, ABS_HAT0Y,
  ABS_HAT1X, ABS_HAT1Y, -1 };

static signed short abs_wheel[] =
{ ABS_WHEEL, ABS_GAS, ABS_BRAKE, ABS_HAT0X, ABS_HAT0Y, -1 };

static signed short ff_iforce[] =
{ FF_PERIODIC, FF_CONSTANT, FF_SPRING, FF_DAMPER,
  FF_SQUARE, FF_TRIANGLE, FF_SINE, FF_SAW_UP, FF_SAW_DOWN, FF_GAIN,
  FF_AUTOCENTER, -1 };

static struct iforce_device iforce_device[] = {
	{ 0x044f, 0xa01c, "Thrustmaster Motor Sport GT",		btn_wheel, abs_wheel, ff_iforce },
	{ 0x046d, 0xc281, "Logitech WingMan Force",			btn_joystick, abs_joystick, ff_iforce },
	{ 0x046d, 0xc291, "Logitech WingMan Formula Force",		btn_wheel, abs_wheel, ff_iforce },
	{ 0x05ef, 0x020a, "AVB Top Shot Pegasus",			btn_avb_pegasus, abs_avb_pegasus, ff_iforce },
	{ 0x05ef, 0x8884, "AVB Mag Turbo Force",			btn_avb_wheel, abs_wheel, ff_iforce },
	{ 0x05ef, 0x8888, "AVB Top Shot Force Feedback Racing Wheel",	btn_avb_tw, abs_wheel, ff_iforce }, //?
	{ 0x061c, 0xc0a4, "ACT LABS Force RS",                          btn_wheel, abs_wheel, ff_iforce }, //?
	{ 0x06f8, 0x0001, "Guillemot Race Leader Force Feedback",	btn_wheel, abs_wheel, ff_iforce }, //?
	{ 0x06f8, 0x0004, "Guillemot Force Feedback Racing Wheel",	btn_wheel, abs_wheel, ff_iforce }, //?
	{ 0x06f8, 0x0004, "Gullemot Jet Leader 3D",			btn_joystick, abs_joystick, ff_iforce }, //?
	{ 0x0000, 0x0000, "Unknown I-Force Device [%04x:%04x]",		btn_joystick, abs_joystick, ff_iforce }
};



static int iforce_input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct iforce* iforce = (struct iforce*)(dev->private);
	unsigned char data[3];

	if (type != EV_FF)
		return -1;

	switch (code) {

		case FF_GAIN:

			data[0] = value >> 9;
			iforce_send_packet(iforce, FF_CMD_GAIN, data);

			return 0;

		case FF_AUTOCENTER:

			data[0] = 0x03;
			data[1] = value >> 9;
			iforce_send_packet(iforce, FF_CMD_AUTOCENTER, data);

			data[0] = 0x04;
			data[1] = 0x01;
			iforce_send_packet(iforce, FF_CMD_AUTOCENTER, data);

			return 0;

		default: /* Play or stop an effect */

			if (!CHECK_OWNERSHIP(code, iforce)) {
				return -1;
			}
			if (value > 0) {
				set_bit(FF_CORE_SHOULD_PLAY, iforce->core_effects[code].flags);
			}
			else {
				clear_bit(FF_CORE_SHOULD_PLAY, iforce->core_effects[code].flags);
			}

			iforce_control_playback(iforce, code, value);
			return 0;
	}

	return -1;
}

/*
 * Function called when an ioctl is performed on the event dev entry.
 * It uploads an effect to the device
 */
static int iforce_upload_effect(struct input_dev *dev, struct ff_effect *effect)
{
	struct iforce* iforce = (struct iforce*)(dev->private);
	int id;
	int ret;
	int is_update;

/* Check this effect type is supported by this device */
	if (!test_bit(effect->type, iforce->dev->ffbit))
		return -EINVAL;

/*
 * If we want to create a new effect, get a free id
 */
	if (effect->id == -1) {

		for (id = 0; id < FF_EFFECTS_MAX; ++id)
			if (!test_and_set_bit(FF_CORE_IS_USED, iforce->core_effects[id].flags))
				break;

		if (id == FF_EFFECTS_MAX || id >= iforce->dev->ff_effects_max)
			return -ENOMEM;

		effect->id = id;
		iforce->core_effects[id].owner = current->pid;
		iforce->core_effects[id].flags[0] = (1 << FF_CORE_IS_USED);	/* Only IS_USED bit must be set */

		is_update = FALSE;
	}
	else {
		/* We want to update an effect */
		if (!CHECK_OWNERSHIP(effect->id, iforce))
			return -EACCES;

		/* Parameter type cannot be updated */
		if (effect->type != iforce->core_effects[effect->id].effect.type)
			return -EINVAL;

		/* Check the effect is not already being updated */
		if (test_bit(FF_CORE_UPDATE, iforce->core_effects[effect->id].flags))
			return -EAGAIN;

		is_update = TRUE;
	}

/*
 * Upload the effect
 */
	switch (effect->type) {

		case FF_PERIODIC:
			ret = iforce_upload_periodic(iforce, effect, is_update);
			break;

		case FF_CONSTANT:
			ret = iforce_upload_constant(iforce, effect, is_update);
			break;

		case FF_SPRING:
		case FF_DAMPER:
			ret = iforce_upload_condition(iforce, effect, is_update);
			break;

		default:
			return -EINVAL;
	}
	if (ret == 0) {
		/* A packet was sent, forbid new updates until we are notified
		 * that the packet was updated
		 */
		set_bit(FF_CORE_UPDATE, iforce->core_effects[effect->id].flags);
	}
	iforce->core_effects[effect->id].effect = *effect;
	return ret;
}

/*
 * Erases an effect: it frees the effect id and mark as unused the memory
 * allocated for the parameters
 */
static int iforce_erase_effect(struct input_dev *dev, int effect_id)
{
	struct iforce* iforce = (struct iforce*)(dev->private);
	int err = 0;
	struct iforce_core_effect* core_effect;

	/* Check who is trying to erase this effect */
	if (iforce->core_effects[effect_id].owner != current->pid) {
		printk(KERN_WARNING "iforce-main.c: %d tried to erase an effect belonging to %d\n", current->pid, iforce->core_effects[effect_id].owner);
		return -EACCES;
	}

	if (effect_id < 0 || effect_id >= FF_EFFECTS_MAX)
		return -EINVAL;

	core_effect = iforce->core_effects + effect_id;

	if (test_bit(FF_MOD1_IS_USED, core_effect->flags))
		err = release_resource(&(iforce->core_effects[effect_id].mod1_chunk));

	if (!err && test_bit(FF_MOD2_IS_USED, core_effect->flags))
		err = release_resource(&(iforce->core_effects[effect_id].mod2_chunk));

	/*TODO: remember to change that if more FF_MOD* bits are added */
	core_effect->flags[0] = 0;

	return err;
}

static int iforce_open(struct input_dev *dev)
{
	struct iforce *iforce = dev->private;

	switch (iforce->bus) {
#ifdef CONFIG_JOYSTICK_IFORCE_USB
		case IFORCE_USB:
			iforce->irq->dev = iforce->usbdev;
			if (usb_submit_urb(iforce->irq, GFP_KERNEL))
				return -EIO;
			break;
#endif
	}

	/* Enable force feedback */
	iforce_send_packet(iforce, FF_CMD_ENABLE, "\004");

	return 0;
}

static int iforce_flush(struct input_dev *dev, struct file *file)
{
	struct iforce *iforce = dev->private;
	int i;

	/* Erase all effects this process owns */
	for (i=0; i<dev->ff_effects_max; ++i) {

		if (test_bit(FF_CORE_IS_USED, iforce->core_effects[i].flags) &&
			current->pid == iforce->core_effects[i].owner) {

			/* Stop effect */
			input_report_ff(dev, i, 0);

			/* Free ressources assigned to effect */
			if (iforce_erase_effect(dev, i)) {
				printk(KERN_WARNING "iforce_flush: erase effect %d failed\n", i);
			}
		}

	}
	return 0;
}

static void iforce_release(struct input_dev *dev)
{
	struct iforce *iforce = dev->private;
	int i;

	/* Check: no effect should be present in memory */
	for (i=0; i<dev->ff_effects_max; ++i) {
		if (test_bit(FF_CORE_IS_USED, iforce->core_effects[i].flags))
			break;
	}
	if (i<dev->ff_effects_max) {
		printk(KERN_WARNING "iforce_release: Device still owns effects\n");
	}

	/* Disable force feedback playback */
	iforce_send_packet(iforce, FF_CMD_ENABLE, "\001");

	switch (iforce->bus) {
#ifdef CONFIG_JOYSTICK_IFORCE_USB
		case IFORCE_USB:
			usb_unlink_urb(iforce->irq);

			/* The device was unplugged before the file
			 * was released */
			if (iforce->usbdev == NULL) {
				iforce_delete_device(iforce);
				kfree(iforce);
			}
		break;
#endif
	}
}

void iforce_delete_device(struct iforce *iforce)
{
	switch (iforce->bus) {
#ifdef CONFIG_JOYSTICK_IFORCE_USB
	case IFORCE_USB:
		iforce_usb_delete(iforce);
		break;
#endif
#ifdef CONFIG_JOYSTICK_IFORCE_232
	case IFORCE_232:
		//TODO: Wait for the last packets to be sent
		break;
#endif
	}
}

int iforce_init_device(struct iforce *iforce)
{
	struct input_dev *input_dev;
	unsigned char c[] = "CEOV";
	int i;

	input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	init_waitqueue_head(&iforce->wait);
	spin_lock_init(&iforce->xmit_lock);
	mutex_init(&iforce->mem_mutex);
	iforce->xmit.buf = iforce->xmit_data;
	iforce->dev = input_dev;

/*
 * Input device fields.
 */

	switch (iforce->bus) {
#ifdef CONFIG_JOYSTICK_IFORCE_USB
	case IFORCE_USB:
		input_dev->id.bustype = BUS_USB;
		input_dev->cdev.dev = &iforce->usbdev->dev;
		break;
#endif
#ifdef CONFIG_JOYSTICK_IFORCE_232
	case IFORCE_232:
		input_dev->id.bustype = BUS_RS232;
		input_dev->cdev.dev = &iforce->serio->dev;
		break;
#endif
	}

	input_dev->private = iforce;
	input_dev->name = "Unknown I-Force device";
	input_dev->open = iforce_open;
	input_dev->close = iforce_release;
	input_dev->flush = iforce_flush;
	input_dev->event = iforce_input_event;
	input_dev->upload_effect = iforce_upload_effect;
	input_dev->erase_effect = iforce_erase_effect;
	input_dev->ff_effects_max = 10;

/*
 * On-device memory allocation.
 */

	iforce->device_memory.name = "I-Force device effect memory";
	iforce->device_memory.start = 0;
	iforce->device_memory.end = 200;
	iforce->device_memory.flags = IORESOURCE_MEM;
	iforce->device_memory.parent = NULL;
	iforce->device_memory.child = NULL;
	iforce->device_memory.sibling = NULL;

/*
 * Wait until device ready - until it sends its first response.
 */

	for (i = 0; i < 20; i++)
		if (!iforce_get_id_packet(iforce, "O"))
			break;

	if (i == 20) { /* 5 seconds */
		printk(KERN_ERR "iforce-main.c: Timeout waiting for response from device.\n");
		input_free_device(input_dev);
		return -ENODEV;
	}

/*
 * Get device info.
 */

	if (!iforce_get_id_packet(iforce, "M"))
		input_dev->id.vendor = (iforce->edata[2] << 8) | iforce->edata[1];
	else
		printk(KERN_WARNING "iforce-main.c: Device does not respond to id packet M\n");

	if (!iforce_get_id_packet(iforce, "P"))
		input_dev->id.product = (iforce->edata[2] << 8) | iforce->edata[1];
	else
		printk(KERN_WARNING "iforce-main.c: Device does not respond to id packet P\n");

	if (!iforce_get_id_packet(iforce, "B"))
		iforce->device_memory.end = (iforce->edata[2] << 8) | iforce->edata[1];
	else
		printk(KERN_WARNING "iforce-main.c: Device does not respond to id packet B\n");

	if (!iforce_get_id_packet(iforce, "N"))
		iforce->dev->ff_effects_max = iforce->edata[1];
	else
		printk(KERN_WARNING "iforce-main.c: Device does not respond to id packet N\n");

	/* Check if the device can store more effects than the driver can really handle */
	if (iforce->dev->ff_effects_max > FF_EFFECTS_MAX) {
		printk(KERN_WARNING "input??: Device can handle %d effects, but N_EFFECTS_MAX is set to %d in iforce.h\n",
			iforce->dev->ff_effects_max, FF_EFFECTS_MAX);
		iforce->dev->ff_effects_max = FF_EFFECTS_MAX;
	}

/*
 * Display additional info.
 */

	for (i = 0; c[i]; i++)
		if (!iforce_get_id_packet(iforce, c + i))
			iforce_dump_packet("info", iforce->ecmd, iforce->edata);

/*
 * Disable spring, enable force feedback.
 * FIXME: We should use iforce_set_autocenter() et al here.
 */

	iforce_send_packet(iforce, FF_CMD_AUTOCENTER, "\004\000");

/*
 * Find appropriate device entry
 */

	for (i = 0; iforce_device[i].idvendor; i++)
		if (iforce_device[i].idvendor == input_dev->id.vendor &&
		    iforce_device[i].idproduct == input_dev->id.product)
			break;

	iforce->type = iforce_device + i;
	input_dev->name = iforce->type->name;

/*
 * Set input device bitfields and ranges.
 */

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS) | BIT(EV_FF) | BIT(EV_FF_STATUS);

	for (i = 0; iforce->type->btn[i] >= 0; i++) {
		signed short t = iforce->type->btn[i];
		set_bit(t, input_dev->keybit);
	}
	set_bit(BTN_DEAD, input_dev->keybit);

	for (i = 0; iforce->type->abs[i] >= 0; i++) {

		signed short t = iforce->type->abs[i];

		switch (t) {

			case ABS_X:
			case ABS_Y:
			case ABS_WHEEL:

				input_set_abs_params(input_dev, t, -1920, 1920, 16, 128);
				set_bit(t, input_dev->ffbit);
				break;

			case ABS_THROTTLE:
			case ABS_GAS:
			case ABS_BRAKE:

				input_set_abs_params(input_dev, t, 0, 255, 0, 0);
				break;

			case ABS_RUDDER:

				input_set_abs_params(input_dev, t, -128, 127, 0, 0);
				break;

			case ABS_HAT0X:
			case ABS_HAT0Y:
		        case ABS_HAT1X:
		        case ABS_HAT1Y:

				input_set_abs_params(input_dev, t, -1, 1, 0, 0);
				break;
		}
	}

	for (i = 0; iforce->type->ff[i] >= 0; i++)
		set_bit(iforce->type->ff[i], input_dev->ffbit);

/*
 * Register input device.
 */

	input_register_device(iforce->dev);

	printk(KERN_DEBUG "iforce->dev->open = %p\n", iforce->dev->open);

	return 0;
}

static int __init iforce_init(void)
{
#ifdef CONFIG_JOYSTICK_IFORCE_USB
	usb_register(&iforce_usb_driver);
#endif
#ifdef CONFIG_JOYSTICK_IFORCE_232
	serio_register_driver(&iforce_serio_drv);
#endif
	return 0;
}

static void __exit iforce_exit(void)
{
#ifdef CONFIG_JOYSTICK_IFORCE_USB
	usb_deregister(&iforce_usb_driver);
#endif
#ifdef CONFIG_JOYSTICK_IFORCE_232
	serio_unregister_driver(&iforce_serio_drv);
#endif
}

module_init(iforce_init);
module_exit(iforce_exit);
