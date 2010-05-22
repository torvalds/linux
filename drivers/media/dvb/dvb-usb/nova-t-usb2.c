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

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define deb_rc(args...) dprintk(debug,0x01,args)
#define deb_ee(args...) dprintk(debug,0x02,args)

/* Hauppauge NOVA-T USB2 keys */
static struct dvb_usb_rc_key ir_codes_haupp_table [] = {
	{ 0x1e00, KEY_0 },
	{ 0x1e01, KEY_1 },
	{ 0x1e02, KEY_2 },
	{ 0x1e03, KEY_3 },
	{ 0x1e04, KEY_4 },
	{ 0x1e05, KEY_5 },
	{ 0x1e06, KEY_6 },
	{ 0x1e07, KEY_7 },
	{ 0x1e08, KEY_8 },
	{ 0x1e09, KEY_9 },
	{ 0x1e0a, KEY_KPASTERISK },
	{ 0x1e0b, KEY_RED },
	{ 0x1e0c, KEY_RADIO },
	{ 0x1e0d, KEY_MENU },
	{ 0x1e0e, KEY_GRAVE }, /* # */
	{ 0x1e0f, KEY_MUTE },
	{ 0x1e10, KEY_VOLUMEUP },
	{ 0x1e11, KEY_VOLUMEDOWN },
	{ 0x1e12, KEY_CHANNEL },
	{ 0x1e14, KEY_UP },
	{ 0x1e15, KEY_DOWN },
	{ 0x1e16, KEY_LEFT },
	{ 0x1e17, KEY_RIGHT },
	{ 0x1e18, KEY_VIDEO },
	{ 0x1e19, KEY_AUDIO },
	{ 0x1e1a, KEY_MEDIA },
	{ 0x1e1b, KEY_EPG },
	{ 0x1e1c, KEY_TV },
	{ 0x1e1e, KEY_NEXT },
	{ 0x1e1f, KEY_BACK },
	{ 0x1e20, KEY_CHANNELUP },
	{ 0x1e21, KEY_CHANNELDOWN },
	{ 0x1e24, KEY_LAST }, /* Skip backwards */
	{ 0x1e25, KEY_OK },
	{ 0x1e29, KEY_BLUE},
	{ 0x1e2e, KEY_GREEN },
	{ 0x1e30, KEY_PAUSE },
	{ 0x1e32, KEY_REWIND },
	{ 0x1e34, KEY_FASTFORWARD },
	{ 0x1e35, KEY_PLAY },
	{ 0x1e36, KEY_STOP },
	{ 0x1e37, KEY_RECORD },
	{ 0x1e38, KEY_YELLOW },
	{ 0x1e3b, KEY_GOTO },
	{ 0x1e3d, KEY_POWER },
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

			for (i = 0; i < ARRAY_SIZE(ir_codes_haupp_table); i++) {
				if (rc5_data(&ir_codes_haupp_table[i]) == data &&
					rc5_custom(&ir_codes_haupp_table[i]) == custom) {

					deb_rc("c: %x, d: %x\n", rc5_data(&ir_codes_haupp_table[i]),
								 rc5_custom(&ir_codes_haupp_table[i]));

					*event = ir_codes_haupp_table[i].event;
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
	return dvb_usb_device_init(intf, &nova_t_properties,
				   THIS_MODULE, NULL, adapter_nr);
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
	.rc_key_map       = ir_codes_haupp_table,
	.rc_key_map_size  = ARRAY_SIZE(ir_codes_haupp_table),
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
