/* dvb-usb-remote.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for initializing the the input-device and for handling remote-control-queries.
 */
#include "dvb-usb-common.h"

/* Remote-control poll function - called every dib->rc_query_interval ms to see
 * whether the remote control has received anything.
 *
 * TODO: Fix the repeat rate of the input device.
 */
static void dvb_usb_read_remote_control(void *data)
{
	struct dvb_usb_device *d = data;
	u32 event;
	int state;

	/* TODO: need a lock here.  We can simply skip checking for the remote control
	   if we're busy. */

	/* when the parameter has been set to 1 via sysfs while the driver was running */
	if (dvb_usb_disable_rc_polling)
		return;

	if (d->props.rc_query(d,&event,&state)) {
		err("error while querying for an remote control event.");
		goto schedule;
	}


	switch (state) {
		case REMOTE_NO_KEY_PRESSED:
			break;
		case REMOTE_KEY_PRESSED:
			deb_rc("key pressed\n");
			d->last_event = event;
		case REMOTE_KEY_REPEAT:
			deb_rc("key repeated\n");
			input_event(d->rc_input_dev, EV_KEY, event, 1);
			input_event(d->rc_input_dev, EV_KEY, d->last_event, 0);
			input_sync(d->rc_input_dev);
			break;
		default:
			break;
	}

/* improved repeat handling ???
	switch (state) {
		case REMOTE_NO_KEY_PRESSED:
			deb_rc("NO KEY PRESSED\n");
			if (d->last_state != REMOTE_NO_KEY_PRESSED) {
				deb_rc("releasing event %d\n",d->last_event);
				input_event(d->rc_input_dev, EV_KEY, d->last_event, 0);
				input_sync(d->rc_input_dev);
			}
			d->last_state = REMOTE_NO_KEY_PRESSED;
			d->last_event = 0;
			break;
		case REMOTE_KEY_PRESSED:
			deb_rc("KEY PRESSED\n");
			deb_rc("pressing event %d\n",event);

			input_event(d->rc_input_dev, EV_KEY, event, 1);
			input_sync(d->rc_input_dev);

			d->last_event = event;
			d->last_state = REMOTE_KEY_PRESSED;
			break;
		case REMOTE_KEY_REPEAT:
			deb_rc("KEY_REPEAT\n");
			if (d->last_state != REMOTE_NO_KEY_PRESSED) {
				deb_rc("repeating event %d\n",d->last_event);
				input_event(d->rc_input_dev, EV_KEY, d->last_event, 2);
				input_sync(d->rc_input_dev);
				d->last_state = REMOTE_KEY_REPEAT;
			}
		default:
			break;
	}
*/

schedule:
	schedule_delayed_work(&d->rc_query_work,msecs_to_jiffies(d->props.rc_interval));
}

int dvb_usb_remote_init(struct dvb_usb_device *d)
{
	int i;

	if (d->props.rc_key_map == NULL ||
		d->props.rc_query == NULL ||
		dvb_usb_disable_rc_polling)
		return 0;

	usb_make_path(d->udev, d->rc_phys, sizeof(d->rc_phys));
	strlcpy(d->rc_phys, "/ir0", sizeof(d->rc_phys));

	d->rc_input_dev = input_allocate_device();
	if (!d->rc_input_dev)
		return -ENOMEM;

	d->rc_input_dev->evbit[0] = BIT(EV_KEY);
	d->rc_input_dev->keycodesize = sizeof(unsigned char);
	d->rc_input_dev->keycodemax = KEY_MAX;
	d->rc_input_dev->name = "IR-receiver inside an USB DVB receiver";
	d->rc_input_dev->phys = d->rc_phys;

	/* set the bits for the keys */
	deb_rc("key map size: %d\n", d->props.rc_key_map_size);
	for (i = 0; i < d->props.rc_key_map_size; i++) {
		deb_rc("setting bit for event %d item %d\n",d->props.rc_key_map[i].event, i);
		set_bit(d->props.rc_key_map[i].event, d->rc_input_dev->keybit);
	}

	/* Start the remote-control polling. */
	if (d->props.rc_interval < 40)
		d->props.rc_interval = 100; /* default */

	/* setting these two values to non-zero, we have to manage key repeats */
	d->rc_input_dev->rep[REP_PERIOD] = d->props.rc_interval;
	d->rc_input_dev->rep[REP_DELAY]  = d->props.rc_interval + 150;

	input_register_device(d->rc_input_dev);

	INIT_WORK(&d->rc_query_work, dvb_usb_read_remote_control, d);

	info("schedule remote query interval to %d msecs.", d->props.rc_interval);
	schedule_delayed_work(&d->rc_query_work,msecs_to_jiffies(d->props.rc_interval));

	d->state |= DVB_USB_STATE_REMOTE;

	return 0;
}

int dvb_usb_remote_exit(struct dvb_usb_device *d)
{
	if (d->state & DVB_USB_STATE_REMOTE) {
		cancel_delayed_work(&d->rc_query_work);
		flush_scheduled_work();
		input_unregister_device(d->rc_input_dev);
	}
	d->state &= ~DVB_USB_STATE_REMOTE;
	return 0;
}

#define DVB_USB_RC_NEC_EMPTY           0x00
#define DVB_USB_RC_NEC_KEY_PRESSED     0x01
#define DVB_USB_RC_NEC_KEY_REPEATED    0x02
int dvb_usb_nec_rc_key_to_event(struct dvb_usb_device *d,
		u8 keybuf[5], u32 *event, int *state)
{
	int i;
	struct dvb_usb_rc_key *keymap = d->props.rc_key_map;
	*event = 0;
	*state = REMOTE_NO_KEY_PRESSED;
	switch (keybuf[0]) {
		case DVB_USB_RC_NEC_EMPTY:
			break;
		case DVB_USB_RC_NEC_KEY_PRESSED:
			if ((u8) ~keybuf[1] != keybuf[2] ||
				(u8) ~keybuf[3] != keybuf[4]) {
				deb_err("remote control checksum failed.\n");
				break;
			}
			/* See if we can match the raw key code. */
			for (i = 0; i < d->props.rc_key_map_size; i++)
				if (keymap[i].custom == keybuf[1] &&
					keymap[i].data == keybuf[3]) {
					*event = keymap[i].event;
					*state = REMOTE_KEY_PRESSED;
					return 0;
				}
			deb_err("key mapping failed - no appropriate key found in keymapping\n");
			break;
		case DVB_USB_RC_NEC_KEY_REPEATED:
			*state = REMOTE_KEY_REPEAT;
			break;
		default:
			deb_err("unkown type of remote status: %d\n",keybuf[0]);
			break;
	}
	return 0;
}
EXPORT_SYMBOL(dvb_usb_nec_rc_key_to_event);
