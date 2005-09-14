/* DVB USB framework compliant Linux driver for the AVerMedia AverTV DVB-T
 * USB2.0 (A800) DVB-T receiver.
 *
 * Copyright (C) 2005 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * Thanks to
 *   - AVerMedia who kindly provided information and
 *   - Glen Harris who suffered from my mistakes during development.
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "dibusb.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (rc=1 (or-able))." DVB_USB_DEBUG_STATUS);
#define deb_rc(args...)   dprintk(debug,0x01,args)

static int a800_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	/* do nothing for the AVerMedia */
	return 0;
}

static struct dvb_usb_rc_key a800_rc_keys[] = {
	{ 0x02, 0x01, KEY_PROG1 },       /* SOURCE */
	{ 0x02, 0x00, KEY_POWER },       /* POWER */
	{ 0x02, 0x05, KEY_1 },           /* 1 */
	{ 0x02, 0x06, KEY_2 },           /* 2 */
	{ 0x02, 0x07, KEY_3 },           /* 3 */
	{ 0x02, 0x09, KEY_4 },           /* 4 */
	{ 0x02, 0x0a, KEY_5 },           /* 5 */
	{ 0x02, 0x0b, KEY_6 },           /* 6 */
	{ 0x02, 0x0d, KEY_7 },           /* 7 */
	{ 0x02, 0x0e, KEY_8 },           /* 8 */
	{ 0x02, 0x0f, KEY_9 },           /* 9 */
	{ 0x02, 0x12, KEY_LEFT },        /* L / DISPLAY */
	{ 0x02, 0x11, KEY_0 },           /* 0 */
	{ 0x02, 0x13, KEY_RIGHT },       /* R / CH RTN */
	{ 0x02, 0x17, KEY_PROG2 },       /* SNAP SHOT */
	{ 0x02, 0x10, KEY_PROG3 },       /* 16-CH PREV */
	{ 0x02, 0x03, KEY_CHANNELUP },   /* CH UP */
	{ 0x02, 0x1e, KEY_VOLUMEDOWN },  /* VOL DOWN */
	{ 0x02, 0x0c, KEY_ZOOM },        /* FULL SCREEN */
	{ 0x02, 0x1f, KEY_VOLUMEUP },    /* VOL UP */
	{ 0x02, 0x02, KEY_CHANNELDOWN }, /* CH DOWN */
	{ 0x02, 0x14, KEY_MUTE },        /* MUTE */
	{ 0x02, 0x08, KEY_AUDIO },       /* AUDIO */
	{ 0x02, 0x19, KEY_RECORD },      /* RECORD */
	{ 0x02, 0x18, KEY_PLAY },        /* PLAY */
	{ 0x02, 0x1b, KEY_STOP },        /* STOP */
	{ 0x02, 0x1a, KEY_PLAYPAUSE },   /* TIMESHIFT / PAUSE */
	{ 0x02, 0x1d, KEY_BACK },        /* << / RED */
	{ 0x02, 0x1c, KEY_FORWARD },     /* >> / YELLOW */
	{ 0x02, 0x03, KEY_TEXT },        /* TELETEXT */
	{ 0x02, 0x01, KEY_FIRST },       /* |<< / GREEN */
	{ 0x02, 0x00, KEY_LAST },        /* >>| / BLUE */
	{ 0x02, 0x04, KEY_EPG },         /* EPG */
	{ 0x02, 0x15, KEY_MENU },        /* MENU */

	{ 0x03, 0x03, KEY_CHANNELUP },   /* CH UP */
	{ 0x03, 0x02, KEY_CHANNELDOWN }, /* CH DOWN */
	{ 0x03, 0x01, KEY_FIRST },       /* |<< / GREEN */
	{ 0x03, 0x00, KEY_LAST },        /* >>| / BLUE */

};

int a800_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	u8 key[5];
	if (usb_control_msg(d->udev,usb_rcvctrlpipe(d->udev,0),
				0x04, USB_TYPE_VENDOR | USB_DIR_IN, 0, 0, key, 5,
				2000) != 5)
		return -ENODEV;

	/* call the universal NEC remote processor, to find out the key's state and event */
	dvb_usb_nec_rc_key_to_event(d,key,event,state);
	if (key[0] != 0)
		deb_rc("key: %x %x %x %x %x\n",key[0],key[1],key[2],key[3],key[4]);
	return 0;
}

/* USB Driver stuff */
static struct dvb_usb_properties a800_properties;

static int a800_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf,&a800_properties,THIS_MODULE,NULL);
}

/* do not change the order of the ID table */
static struct usb_device_id a800_table [] = {
/* 00 */	{ USB_DEVICE(USB_VID_AVERMEDIA,     USB_PID_AVERMEDIA_DVBT_USB2_COLD) },
/* 01 */	{ USB_DEVICE(USB_VID_AVERMEDIA,     USB_PID_AVERMEDIA_DVBT_USB2_WARM) },
			{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, a800_table);

static struct dvb_usb_properties a800_properties = {
	.caps = DVB_USB_HAS_PID_FILTER | DVB_USB_PID_FILTER_CAN_BE_TURNED_OFF | DVB_USB_IS_AN_I2C_ADAPTER,
	.pid_filter_count = 32,

	.usb_ctrl = CYPRESS_FX2,

	.firmware = "dvb-usb-avertv-a800-02.fw",

	.size_of_priv     = sizeof(struct dibusb_state),

	.streaming_ctrl   = dibusb2_0_streaming_ctrl,
	.pid_filter       = dibusb_pid_filter,
	.pid_filter_ctrl  = dibusb_pid_filter_ctrl,
	.power_ctrl       = a800_power_ctrl,
	.frontend_attach  = dibusb_dib3000mc_frontend_attach,
	.tuner_attach     = dibusb_dib3000mc_tuner_attach,

	.rc_interval      = DEFAULT_RC_INTERVAL,
	.rc_key_map       = a800_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(a800_rc_keys),
	.rc_query         = a800_rc_query,

	.i2c_algo         = &dibusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,
	/* parameter for the MPEG2-data transfer */
	.urb = {
		.type = DVB_USB_BULK,
		.count = 7,
		.endpoint = 0x06,
		.u = {
			.bulk = {
				.buffersize = 4096,
			}
		}
	},

	.num_device_descs = 1,
	.devices = {
		{   "AVerMedia AverTV DVB-T USB 2.0 (A800)",
			{ &a800_table[0], NULL },
			{ &a800_table[1], NULL },
		},
	}
};

static struct usb_driver a800_driver = {
	.owner		= THIS_MODULE,
	.name		= "dvb_usb_a800",
	.probe		= a800_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= a800_table,
};

/* module stuff */
static int __init a800_module_init(void)
{
	int result;
	if ((result = usb_register(&a800_driver))) {
		err("usb_register failed. Error number %d",result);
		return result;
	}

	return 0;
}

static void __exit a800_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&a800_driver);
}

module_init (a800_module_init);
module_exit (a800_module_exit);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("AVerMedia AverTV DVB-T USB 2.0 (A800)");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
