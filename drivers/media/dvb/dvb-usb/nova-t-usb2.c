/* DVB USB framework compliant Linux driver for the Hauppauge WinTV-NOVA-T usb2
 * DVB-T receiver.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
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
MODULE_PARM_DESC(debug, "set debugging level (1=rc,2=eeprom (|-able))." DVB_USB_DEBUG_STATUS);

#define deb_rc(args...) dprintk(debug,0x01,args)
#define deb_ee(args...) dprintk(debug,0x02,args)

/* Hauppauge NOVA-T USB2 keys */
static struct dvb_usb_rc_key haupp_rc_keys [] = {
	{ 0x1e, 0x00, KEY_0 },
	{ 0x1e, 0x01, KEY_1 },
	{ 0x1e, 0x02, KEY_2 },
	{ 0x1e, 0x03, KEY_3 },
	{ 0x1e, 0x04, KEY_4 },
	{ 0x1e, 0x05, KEY_5 },
	{ 0x1e, 0x06, KEY_6 },
	{ 0x1e, 0x07, KEY_7 },
	{ 0x1e, 0x08, KEY_8 },
	{ 0x1e, 0x09, KEY_9 },
	{ 0x1e, 0x0a, KEY_KPASTERISK },
	{ 0x1e, 0x0b, KEY_RED },
	{ 0x1e, 0x0c, KEY_RADIO },
	{ 0x1e, 0x0d, KEY_MENU },
	{ 0x1e, 0x0e, KEY_GRAVE }, /* # */
	{ 0x1e, 0x0f, KEY_MUTE },
	{ 0x1e, 0x10, KEY_VOLUMEUP },
	{ 0x1e, 0x11, KEY_VOLUMEDOWN },
	{ 0x1e, 0x12, KEY_CHANNEL },
	{ 0x1e, 0x14, KEY_UP },
	{ 0x1e, 0x15, KEY_DOWN },
	{ 0x1e, 0x16, KEY_LEFT },
	{ 0x1e, 0x17, KEY_RIGHT },
	{ 0x1e, 0x18, KEY_VIDEO },
	{ 0x1e, 0x19, KEY_AUDIO },
	{ 0x1e, 0x1a, KEY_MEDIA },
	{ 0x1e, 0x1b, KEY_EPG },
	{ 0x1e, 0x1c, KEY_TV },
	{ 0x1e, 0x1e, KEY_NEXT },
	{ 0x1e, 0x1f, KEY_BACK },
	{ 0x1e, 0x20, KEY_CHANNELUP },
	{ 0x1e, 0x21, KEY_CHANNELDOWN },
	{ 0x1e, 0x24, KEY_LAST }, /* Skip backwards */
	{ 0x1e, 0x25, KEY_OK },
	{ 0x1e, 0x29, KEY_BLUE},
	{ 0x1e, 0x2e, KEY_GREEN },
	{ 0x1e, 0x30, KEY_PAUSE },
	{ 0x1e, 0x32, KEY_REWIND },
	{ 0x1e, 0x34, KEY_FASTFORWARD },
	{ 0x1e, 0x35, KEY_PLAY },
	{ 0x1e, 0x36, KEY_STOP },
	{ 0x1e, 0x37, KEY_RECORD },
	{ 0x1e, 0x38, KEY_YELLOW },
	{ 0x1e, 0x3b, KEY_GOTO },
	{ 0x1e, 0x3d, KEY_POWER },
};

/* Firmware bug? sometimes, when a new key is pressed, the previous pressed key
 * is delivered. No workaround yet, maybe a new firmware.
 */
static int nova_t_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	u8 key[5],cmd[2] = { DIBUSB_REQ_POLL_REMOTE, 0x35 }, data,toggle,custom;
	u16 raw;
	int i;
	struct dibusb_device_state *st = d->priv;

	dvb_usb_generic_rw(d,cmd,2,key,5,0);

	*state = REMOTE_NO_KEY_PRESSED;
	switch (key[0]) {
		case DIBUSB_RC_HAUPPAUGE_KEY_PRESSED:
			raw = ((key[1] << 8) | key[2]) >> 3;
			toggle = !!(raw & 0x800);
			data = raw & 0x3f;
			custom = (raw >> 6) & 0x1f;

			deb_rc("raw key code 0x%02x, 0x%02x, 0x%02x to c: %02x d: %02x toggle: %d\n",key[1],key[2],key[3],custom,data,toggle);

			for (i = 0; i < ARRAY_SIZE(haupp_rc_keys); i++) {
				deb_rc("c: %x, d: %x\n",haupp_rc_keys[i].data,haupp_rc_keys[i].custom);
				if (haupp_rc_keys[i].data == data &&
					haupp_rc_keys[i].custom == custom) {
					*event = haupp_rc_keys[i].event;
					*state = REMOTE_KEY_PRESSED;
					if (st->old_toggle == toggle) {
						if (st->last_repeat_count++ < 2)
							*state = REMOTE_NO_KEY_PRESSED;
					} else {
						st->last_repeat_count = 0;
						st->old_toggle = toggle;
					}
					break;
				}
			}

			break;
		case DIBUSB_RC_HAUPPAUGE_KEY_EMPTY:
		default:
			break;
	}

	return 0;
}

static int nova_t_read_mac_address (struct dvb_usb_device *d, u8 mac[6])
{
	int i;
	u8 b;

	mac[0] = 0x00;
	mac[1] = 0x0d;
	mac[2] = 0xfe;

	/* this is a complete guess, but works for my box */
	for (i = 136; i < 139; i++) {
		dibusb_read_eeprom_byte(d,i, &b);

		mac[5 - (i - 136)] = b;
	}

	return 0;
}

/* USB Driver stuff */
static struct dvb_usb_device_properties nova_t_properties;

static int nova_t_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf,&nova_t_properties,THIS_MODULE,NULL);
}

/* do not change the order of the ID table */
static struct usb_device_id nova_t_table [] = {
/* 00 */	{ USB_DEVICE(USB_VID_HAUPPAUGE,     USB_PID_WINTV_NOVA_T_USB2_COLD) },
/* 01 */	{ USB_DEVICE(USB_VID_HAUPPAUGE,     USB_PID_WINTV_NOVA_T_USB2_WARM) },
			{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, nova_t_table);

static struct dvb_usb_device_properties nova_t_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-nova-t-usb2-02.fw",

	.num_adapters     = 1,
	.adapter          = {
		{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.pid_filter_count = 32,

	.streaming_ctrl   = dibusb2_0_streaming_ctrl,
	.pid_filter       = dibusb_pid_filter,
	.pid_filter_ctrl  = dibusb_pid_filter_ctrl,
	.frontend_attach  = dibusb_dib3000mc_frontend_attach,
	.tuner_attach     = dibusb_dib3000mc_tuner_attach,

	/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
		.count = 7,
		.endpoint = 0x06,
		.u = {
			.bulk = {
				.buffersize = 4096,
			}
		}
	},

			.size_of_priv     = sizeof(struct dibusb_state),
		}
	},
	.size_of_priv     = sizeof(struct dibusb_device_state),

	.power_ctrl       = dibusb2_0_power_ctrl,
	.read_mac_address = nova_t_read_mac_address,

	.rc_interval      = 100,
	.rc_key_map       = haupp_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(haupp_rc_keys),
	.rc_query         = nova_t_rc_query,

	.i2c_algo         = &dibusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{   "Hauppauge WinTV-NOVA-T usb2",
			{ &nova_t_table[0], NULL },
			{ &nova_t_table[1], NULL },
		},
		{ NULL },
	}
};

static struct usb_driver nova_t_driver = {
	.name		= "dvb_usb_nova_t_usb2",
	.probe		= nova_t_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= nova_t_table,
};

/* module stuff */
static int __init nova_t_module_init(void)
{
	int result;
	if ((result = usb_register(&nova_t_driver))) {
		err("usb_register failed. Error number %d",result);
		return result;
	}

	return 0;
}

static void __exit nova_t_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&nova_t_driver);
}

module_init (nova_t_module_init);
module_exit (nova_t_module_exit);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("Hauppauge WinTV-NOVA-T usb2");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
