/*
 * dvb-dibusb-remote.c is part of the driver for mobile USB Budget DVB-T devices
 * based on reference design made by DiBcom (http://www.dibcom.fr/)
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * see dvb-dibusb-core.c for more copyright details.
 *
 * This file contains functions for handling the event device on the software
 * side and the remote control on the hardware side.
 */
#include "dvb-dibusb.h"

/* Table to map raw key codes to key events.  This should not be hard-wired
   into the kernel.  */
static const struct { u8 c0, c1, c2; uint32_t key; } nec_rc_keys [] =
{
	/* Key codes for the little Artec T1/Twinhan/HAMA/ remote. */
	{ 0x00, 0xff, 0x16, KEY_POWER },
	{ 0x00, 0xff, 0x10, KEY_MUTE },
	{ 0x00, 0xff, 0x03, KEY_1 },
	{ 0x00, 0xff, 0x01, KEY_2 },
	{ 0x00, 0xff, 0x06, KEY_3 },
	{ 0x00, 0xff, 0x09, KEY_4 },
	{ 0x00, 0xff, 0x1d, KEY_5 },
	{ 0x00, 0xff, 0x1f, KEY_6 },
	{ 0x00, 0xff, 0x0d, KEY_7 },
	{ 0x00, 0xff, 0x19, KEY_8 },
	{ 0x00, 0xff, 0x1b, KEY_9 },
	{ 0x00, 0xff, 0x15, KEY_0 },
	{ 0x00, 0xff, 0x05, KEY_CHANNELUP },
	{ 0x00, 0xff, 0x02, KEY_CHANNELDOWN },
	{ 0x00, 0xff, 0x1e, KEY_VOLUMEUP },
	{ 0x00, 0xff, 0x0a, KEY_VOLUMEDOWN },
	{ 0x00, 0xff, 0x11, KEY_RECORD },
	{ 0x00, 0xff, 0x17, KEY_FAVORITES }, /* Heart symbol - Channel list. */
	{ 0x00, 0xff, 0x14, KEY_PLAY },
	{ 0x00, 0xff, 0x1a, KEY_STOP },
	{ 0x00, 0xff, 0x40, KEY_REWIND },
	{ 0x00, 0xff, 0x12, KEY_FASTFORWARD },
	{ 0x00, 0xff, 0x0e, KEY_PREVIOUS }, /* Recall - Previous channel. */
	{ 0x00, 0xff, 0x4c, KEY_PAUSE },
	{ 0x00, 0xff, 0x4d, KEY_SCREEN }, /* Full screen mode. */
	{ 0x00, 0xff, 0x54, KEY_AUDIO }, /* MTS - Switch to secondary audio. */
	/* additional keys TwinHan VisionPlus, the Artec seemingly not have */
	{ 0x00, 0xff, 0x0c, KEY_CANCEL }, /* Cancel */
	{ 0x00, 0xff, 0x1c, KEY_EPG }, /* EPG */
	{ 0x00, 0xff, 0x00, KEY_TAB }, /* Tab */
	{ 0x00, 0xff, 0x48, KEY_INFO }, /* Preview */
	{ 0x00, 0xff, 0x04, KEY_LIST }, /* RecordList */
	{ 0x00, 0xff, 0x0f, KEY_TEXT }, /* Teletext */
	/* Key codes for the KWorld/ADSTech/JetWay remote. */
	{ 0x86, 0x6b, 0x12, KEY_POWER },
	{ 0x86, 0x6b, 0x0f, KEY_SELECT }, /* source */
	{ 0x86, 0x6b, 0x0c, KEY_UNKNOWN }, /* scan */
	{ 0x86, 0x6b, 0x0b, KEY_EPG },
	{ 0x86, 0x6b, 0x10, KEY_MUTE },
	{ 0x86, 0x6b, 0x01, KEY_1 },
	{ 0x86, 0x6b, 0x02, KEY_2 },
	{ 0x86, 0x6b, 0x03, KEY_3 },
	{ 0x86, 0x6b, 0x04, KEY_4 },
	{ 0x86, 0x6b, 0x05, KEY_5 },
	{ 0x86, 0x6b, 0x06, KEY_6 },
	{ 0x86, 0x6b, 0x07, KEY_7 },
	{ 0x86, 0x6b, 0x08, KEY_8 },
	{ 0x86, 0x6b, 0x09, KEY_9 },
	{ 0x86, 0x6b, 0x0a, KEY_0 },
	{ 0x86, 0x6b, 0x18, KEY_ZOOM },
	{ 0x86, 0x6b, 0x1c, KEY_UNKNOWN }, /* preview */
	{ 0x86, 0x6b, 0x13, KEY_UNKNOWN }, /* snap */
	{ 0x86, 0x6b, 0x00, KEY_UNDO },
	{ 0x86, 0x6b, 0x1d, KEY_RECORD },
	{ 0x86, 0x6b, 0x0d, KEY_STOP },
	{ 0x86, 0x6b, 0x0e, KEY_PAUSE },
	{ 0x86, 0x6b, 0x16, KEY_PLAY },
	{ 0x86, 0x6b, 0x11, KEY_BACK },
	{ 0x86, 0x6b, 0x19, KEY_FORWARD },
	{ 0x86, 0x6b, 0x14, KEY_UNKNOWN }, /* pip */
	{ 0x86, 0x6b, 0x15, KEY_ESC },
	{ 0x86, 0x6b, 0x1a, KEY_UP },
	{ 0x86, 0x6b, 0x1e, KEY_DOWN },
	{ 0x86, 0x6b, 0x1f, KEY_LEFT },
	{ 0x86, 0x6b, 0x1b, KEY_RIGHT },
};

/* Hauppauge NOVA-T USB2 keys */
static const struct { u16 raw; uint32_t key; } haupp_rc_keys [] = {
	{ 0xddf, KEY_GOTO },
	{ 0xdef, KEY_POWER },
	{ 0xce7, KEY_TV },
	{ 0xcc7, KEY_VIDEO },
	{ 0xccf, KEY_AUDIO },
	{ 0xcd7, KEY_MEDIA },
	{ 0xcdf, KEY_EPG },
	{ 0xca7, KEY_UP },
	{ 0xc67, KEY_RADIO },
	{ 0xcb7, KEY_LEFT },
	{ 0xd2f, KEY_OK },
	{ 0xcbf, KEY_RIGHT },
	{ 0xcff, KEY_BACK },
	{ 0xcaf, KEY_DOWN },
	{ 0xc6f, KEY_MENU },
	{ 0xc87, KEY_VOLUMEUP },
	{ 0xc8f, KEY_VOLUMEDOWN },
	{ 0xc97, KEY_CHANNEL },
	{ 0xc7f, KEY_MUTE },
	{ 0xd07, KEY_CHANNELUP },
	{ 0xd0f, KEY_CHANNELDOWN },
	{ 0xdbf, KEY_RECORD },
	{ 0xdb7, KEY_STOP },
	{ 0xd97, KEY_REWIND },
	{ 0xdaf, KEY_PLAY },
	{ 0xda7, KEY_FASTFORWARD },
	{ 0xd27, KEY_LAST }, /* Skip backwards */
	{ 0xd87, KEY_PAUSE },
	{ 0xcf7, KEY_NEXT },
	{ 0xc07, KEY_0 },
	{ 0xc0f, KEY_1 },
	{ 0xc17, KEY_2 },
	{ 0xc1f, KEY_3 },
	{ 0xc27, KEY_4 },
	{ 0xc2f, KEY_5 },
	{ 0xc37, KEY_6 },
	{ 0xc3f, KEY_7 },
	{ 0xc47, KEY_8 },
	{ 0xc4f, KEY_9 },
	{ 0xc57, KEY_KPASTERISK },
	{ 0xc77, KEY_GRAVE }, /* # */
	{ 0xc5f, KEY_RED },
	{ 0xd77, KEY_GREEN },
	{ 0xdc7, KEY_YELLOW },
	{ 0xd4f, KEY_BLUE},
};

static int dibusb_key2event_nec(struct usb_dibusb *dib,u8 rb[5])
{
	int i;
	switch (rb[0]) {
		case DIBUSB_RC_NEC_KEY_PRESSED:
			/* rb[1-3] is the actual key, rb[4] is a checksum */
			deb_rc("raw key code 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
				rb[1], rb[2], rb[3], rb[4]);

			if ((0xff - rb[3]) != rb[4]) {
				deb_rc("remote control checksum failed.\n");
				break;
			}

			/* See if we can match the raw key code. */
			for (i = 0; i < sizeof(nec_rc_keys)/sizeof(nec_rc_keys[0]); i++) {
				if (nec_rc_keys[i].c0 == rb[1] &&
					nec_rc_keys[i].c1 == rb[2] &&
					nec_rc_keys[i].c2 == rb[3]) {

					dib->last_event = nec_rc_keys[i].key;
					return 1;
				}
			}
			break;
		case DIBUSB_RC_NEC_KEY_REPEATED:
			/* rb[1]..rb[4] are always zero.*/
			/* Repeats often seem to occur so for the moment just ignore this. */
			return 0;
		case DIBUSB_RC_NEC_EMPTY: /* No (more) remote control keys. */
		default:
			break;
	}
	return -1;
}

static int dibusb_key2event_hauppauge(struct usb_dibusb *dib,u8 rb[4])
{
	u16 raw;
	int i,state;
	switch (rb[0]) {
		case DIBUSB_RC_HAUPPAUGE_KEY_PRESSED:
			raw = ((rb[1] & 0x0f) << 8) | rb[2];

			state = !!(rb[1] & 0x40);

			deb_rc("raw key code 0x%02x, 0x%02x, 0x%02x to %04x state: %d\n",rb[1],rb[2],rb[3],raw,state);
			for (i = 0; i < sizeof(haupp_rc_keys)/sizeof(haupp_rc_keys[0]); i++) {
				if (haupp_rc_keys[i].raw == raw) {
					if (dib->last_event == haupp_rc_keys[i].key &&
						dib->last_state == state) {
						deb_rc("key repeat\n");
						return 0;
					} else {
						dib->last_event = haupp_rc_keys[i].key;
						dib->last_state = state;
						return 1;
					}
				}
			}

			break;
		case DIBUSB_RC_HAUPPAUGE_KEY_EMPTY:
		default:
			break;
	}
	return -1;
}

/*
 * Read the remote control and feed the appropriate event.
 * NEC protocol is used for remote controls
 */
static int dibusb_read_remote_control(struct usb_dibusb *dib)
{
	u8 b[1] = { DIBUSB_REQ_POLL_REMOTE }, rb[5];
	int ret,event = 0;

	if ((ret = dibusb_readwrite_usb(dib,b,1,rb,5)))
		return ret;

	switch (dib->dibdev->dev_cl->remote_type) {
		case DIBUSB_RC_NEC_PROTOCOL:
			event = dibusb_key2event_nec(dib,rb);
			break;
		case DIBUSB_RC_HAUPPAUGE_PROTO:
			event = dibusb_key2event_hauppauge(dib,rb);
		default:
			break;
	}

	/* key repeat */
	if (event == 0)
		if (++dib->repeat_key_count < dib->rc_key_repeat_count) {
			deb_rc("key repeat dropped. (%d)\n",dib->repeat_key_count);
			event = -1; /* skip this key repeat */
		}

	if (event == 1 || event == 0) {
		deb_rc("Translated key 0x%04x\n",event);

		/* Signal down and up events for this key. */
		input_report_key(&dib->rc_input_dev, dib->last_event, 1);
		input_report_key(&dib->rc_input_dev, dib->last_event, 0);
		input_sync(&dib->rc_input_dev);

		if (event == 1)
			dib->repeat_key_count = 0;
	}
	return 0;
}

/* Remote-control poll function - called every dib->rc_query_interval ms to see
   whether the remote control has received anything. */
static void dibusb_remote_query(void *data)
{
	struct usb_dibusb *dib = (struct usb_dibusb *) data;
	/* TODO: need a lock here.  We can simply skip checking for the remote control
	   if we're busy. */
	dibusb_read_remote_control(dib);
	schedule_delayed_work(&dib->rc_query_work,
			      msecs_to_jiffies(dib->rc_query_interval));
}

int dibusb_remote_init(struct usb_dibusb *dib)
{
	int i;

	if (dib->dibdev->dev_cl->remote_type == DIBUSB_RC_NO)
		return 0;
	
	/* Initialise the remote-control structures.*/
	init_input_dev(&dib->rc_input_dev);

	dib->rc_input_dev.evbit[0] = BIT(EV_KEY);
	dib->rc_input_dev.keycodesize = sizeof(unsigned char);
	dib->rc_input_dev.keycodemax = KEY_MAX;
	dib->rc_input_dev.name = DRIVER_DESC " remote control";

	switch (dib->dibdev->dev_cl->remote_type) {
		case DIBUSB_RC_NEC_PROTOCOL:
			for (i=0; i<sizeof(nec_rc_keys)/sizeof(nec_rc_keys[0]); i++)
				set_bit(nec_rc_keys[i].key, dib->rc_input_dev.keybit);
			break;
		case DIBUSB_RC_HAUPPAUGE_PROTO:
			for (i=0; i<sizeof(haupp_rc_keys)/sizeof(haupp_rc_keys[0]); i++)
				set_bit(haupp_rc_keys[i].key, dib->rc_input_dev.keybit);
			break;
		default:
			break;
	}


	input_register_device(&dib->rc_input_dev);

	INIT_WORK(&dib->rc_query_work, dibusb_remote_query, dib);

	/* Start the remote-control polling. */
	if (dib->rc_query_interval < 40)
		dib->rc_query_interval = 100; /* default */

	info("schedule remote query interval to %d msecs.",dib->rc_query_interval);
	schedule_delayed_work(&dib->rc_query_work,msecs_to_jiffies(dib->rc_query_interval));

	dib->init_state |= DIBUSB_STATE_REMOTE;
	
	return 0;
}

int dibusb_remote_exit(struct usb_dibusb *dib)
{
	if (dib->dibdev->dev_cl->remote_type == DIBUSB_RC_NO)
		return 0;

	if (dib->init_state & DIBUSB_STATE_REMOTE) {
		cancel_delayed_work(&dib->rc_query_work);
		flush_scheduled_work();
		input_unregister_device(&dib->rc_input_dev);
	}
	dib->init_state &= ~DIBUSB_STATE_REMOTE;
	return 0;
}
