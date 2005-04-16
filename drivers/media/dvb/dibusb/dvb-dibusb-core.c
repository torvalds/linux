/*
 * Driver for mobile USB Budget DVB-T devices based on reference 
 * design made by DiBcom (http://www.dibcom.fr/)
 * 
 * dvb-dibusb-core.c
 * 
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 * 
 * based on GPL code from DiBcom, which has
 * Copyright (C) 2004 Amaury Demol for DiBcom (ademol@dibcom.fr)
 *
 * Remote control code added by David Matthews (dm@prolingua.co.uk)
 * 
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * Acknowledgements
 * 
 *  Amaury Demol (ademol@dibcom.fr) from DiBcom for providing specs and driver
 *  sources, on which this driver (and the dib3000mb/mc/p frontends) are based.
 * 
 * see Documentation/dvb/README.dibusb for more information
 */
#include "dvb-dibusb.h"

#include <linux/moduleparam.h>

/* debug */
int dvb_dibusb_debug;
module_param_named(debug, dvb_dibusb_debug,  int, 0644);

#ifdef CONFIG_DVB_DIBCOM_DEBUG
#define DBSTATUS ""
#else
#define DBSTATUS " (debugging is not enabled)"
#endif
MODULE_PARM_DESC(debug, "set debugging level (1=info,2=xfer,4=alotmore,8=ts,16=err,32=rc (|-able))." DBSTATUS);
#undef DBSTATUS

static int pid_parse;
module_param(pid_parse, int, 0644);
MODULE_PARM_DESC(pid_parse, "enable pid parsing (filtering) when running at USB2.0");

static int rc_query_interval = 100;
module_param(rc_query_interval, int, 0644);
MODULE_PARM_DESC(rc_query_interval, "interval in msecs for remote control query (default: 100; min: 40)");

static int rc_key_repeat_count = 2;
module_param(rc_key_repeat_count, int, 0644);
MODULE_PARM_DESC(rc_key_repeat_count, "how many key repeats will be dropped before passing the key event again (default: 2)");

/* Vendor IDs */
#define USB_VID_ADSTECH						0x06e1
#define USB_VID_ANCHOR						0x0547
#define USB_VID_AVERMEDIA					0x14aa
#define USB_VID_COMPRO						0x185b
#define USB_VID_COMPRO_UNK					0x145f
#define USB_VID_CYPRESS						0x04b4
#define USB_VID_DIBCOM						0x10b8
#define USB_VID_EMPIA						0xeb1a
#define USB_VID_GRANDTEC					0x5032
#define USB_VID_HANFTEK						0x15f4
#define USB_VID_HAUPPAUGE					0x2040
#define USB_VID_HYPER_PALTEK				0x1025
#define USB_VID_IMC_NETWORKS				0x13d3
#define USB_VID_TWINHAN						0x1822
#define USB_VID_ULTIMA_ELECTRONIC			0x05d8

/* Product IDs */
#define USB_PID_ADSTECH_USB2_COLD			0xa333
#define USB_PID_ADSTECH_USB2_WARM			0xa334
#define USB_PID_AVERMEDIA_DVBT_USB_COLD		0x0001
#define USB_PID_AVERMEDIA_DVBT_USB_WARM		0x0002
#define USB_PID_COMPRO_DVBU2000_COLD		0xd000
#define USB_PID_COMPRO_DVBU2000_WARM		0xd001
#define USB_PID_COMPRO_DVBU2000_UNK_COLD	0x010c
#define USB_PID_COMPRO_DVBU2000_UNK_WARM	0x010d
#define USB_PID_DIBCOM_MOD3000_COLD			0x0bb8
#define USB_PID_DIBCOM_MOD3000_WARM			0x0bb9
#define USB_PID_DIBCOM_MOD3001_COLD			0x0bc6
#define USB_PID_DIBCOM_MOD3001_WARM			0x0bc7
#define USB_PID_DIBCOM_ANCHOR_2135_COLD		0x2131
#define USB_PID_GRANDTEC_DVBT_USB_COLD		0x0fa0
#define USB_PID_GRANDTEC_DVBT_USB_WARM		0x0fa1
#define USB_PID_KWORLD_VSTREAM_COLD			0x17de
#define USB_PID_KWORLD_VSTREAM_WARM			0x17df
#define USB_PID_TWINHAN_VP7041_COLD			0x3201
#define USB_PID_TWINHAN_VP7041_WARM			0x3202
#define USB_PID_ULTIMA_TVBOX_COLD			0x8105
#define USB_PID_ULTIMA_TVBOX_WARM			0x8106
#define USB_PID_ULTIMA_TVBOX_AN2235_COLD	0x8107
#define USB_PID_ULTIMA_TVBOX_AN2235_WARM	0x8108
#define USB_PID_ULTIMA_TVBOX_ANCHOR_COLD	0x2235
#define USB_PID_ULTIMA_TVBOX_USB2_COLD		0x8109
#define USB_PID_ULTIMA_TVBOX_USB2_FX_COLD	0x8613
#define USB_PID_ULTIMA_TVBOX_USB2_FX_WARM	0x1002
#define USB_PID_UNK_HYPER_PALTEK_COLD		0x005e
#define USB_PID_UNK_HYPER_PALTEK_WARM		0x005f
#define USB_PID_HANFTEK_UMT_010_COLD		0x0001
#define USB_PID_HANFTEK_UMT_010_WARM		0x0015
#define USB_PID_YAKUMO_DTT200U_COLD			0x0201
#define USB_PID_YAKUMO_DTT200U_WARM			0x0301
#define USB_PID_WINTV_NOVA_T_USB2_COLD		0x9300
#define USB_PID_WINTV_NOVA_T_USB2_WARM		0x9301

/* USB Driver stuff
 * table of devices that this driver is working with
 *
 * ATTENTION: Never ever change the order of this table, the particular 
 * devices depend on this order 
 *
 * Each entry is used as a reference in the device_struct. Currently this is 
 * the only non-redundant way of assigning USB ids to actual devices I'm aware 
 * of, because there is only one place in the code where the assignment of 
 * vendor and product id is done, here.
 */
static struct usb_device_id dib_table [] = {
/* 00 */	{ USB_DEVICE(USB_VID_AVERMEDIA,		USB_PID_AVERMEDIA_DVBT_USB_COLD)},
/* 01 */	{ USB_DEVICE(USB_VID_AVERMEDIA,		USB_PID_AVERMEDIA_DVBT_USB_WARM)},
/* 02 */	{ USB_DEVICE(USB_VID_AVERMEDIA,		USB_PID_YAKUMO_DTT200U_COLD) },
/* 03 */	{ USB_DEVICE(USB_VID_AVERMEDIA,		USB_PID_YAKUMO_DTT200U_WARM) },

/* 04 */	{ USB_DEVICE(USB_VID_COMPRO,		USB_PID_COMPRO_DVBU2000_COLD) },
/* 05 */	{ USB_DEVICE(USB_VID_COMPRO,		USB_PID_COMPRO_DVBU2000_WARM) },
/* 06 */	{ USB_DEVICE(USB_VID_COMPRO_UNK,	USB_PID_COMPRO_DVBU2000_UNK_COLD) },
/* 07 */	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3000_COLD) },
/* 08 */	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3000_WARM) },
/* 09 */	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3001_COLD) },
/* 10 */	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3001_WARM) },
/* 11 */	{ USB_DEVICE(USB_VID_EMPIA,			USB_PID_KWORLD_VSTREAM_COLD) },
/* 12 */	{ USB_DEVICE(USB_VID_EMPIA,			USB_PID_KWORLD_VSTREAM_WARM) },
/* 13 */	{ USB_DEVICE(USB_VID_GRANDTEC,		USB_PID_GRANDTEC_DVBT_USB_COLD) },
/* 14 */	{ USB_DEVICE(USB_VID_GRANDTEC,		USB_PID_GRANDTEC_DVBT_USB_WARM) },
/* 15 */	{ USB_DEVICE(USB_VID_GRANDTEC,		USB_PID_DIBCOM_MOD3000_COLD) },
/* 16 */	{ USB_DEVICE(USB_VID_GRANDTEC,		USB_PID_DIBCOM_MOD3000_WARM) },
/* 17 */	{ USB_DEVICE(USB_VID_HYPER_PALTEK,	USB_PID_UNK_HYPER_PALTEK_COLD) },
/* 18 */	{ USB_DEVICE(USB_VID_HYPER_PALTEK,	USB_PID_UNK_HYPER_PALTEK_WARM) },
/* 19 */	{ USB_DEVICE(USB_VID_IMC_NETWORKS,	USB_PID_TWINHAN_VP7041_COLD) },
/* 20 */	{ USB_DEVICE(USB_VID_IMC_NETWORKS,	USB_PID_TWINHAN_VP7041_WARM) },
/* 21 */	{ USB_DEVICE(USB_VID_TWINHAN, 		USB_PID_TWINHAN_VP7041_COLD) },
/* 22 */	{ USB_DEVICE(USB_VID_TWINHAN, 		USB_PID_TWINHAN_VP7041_WARM) },
/* 23 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC, USB_PID_ULTIMA_TVBOX_COLD) },
/* 24 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC, USB_PID_ULTIMA_TVBOX_WARM) },
/* 25 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC, USB_PID_ULTIMA_TVBOX_AN2235_COLD) },
/* 26 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC, USB_PID_ULTIMA_TVBOX_AN2235_WARM) },
/* 27 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC,	USB_PID_ULTIMA_TVBOX_USB2_COLD) },
	
/* 28 */	{ USB_DEVICE(USB_VID_HANFTEK,		USB_PID_HANFTEK_UMT_010_COLD) },
/* 29 */	{ USB_DEVICE(USB_VID_HANFTEK,		USB_PID_HANFTEK_UMT_010_WARM) },

/* 30 */	{ USB_DEVICE(USB_VID_HAUPPAUGE,		USB_PID_WINTV_NOVA_T_USB2_COLD) },
/* 31 */	{ USB_DEVICE(USB_VID_HAUPPAUGE,		USB_PID_WINTV_NOVA_T_USB2_WARM) },
/* 32 */	{ USB_DEVICE(USB_VID_ADSTECH,		USB_PID_ADSTECH_USB2_COLD) },
/* 33 */	{ USB_DEVICE(USB_VID_ADSTECH,		USB_PID_ADSTECH_USB2_WARM) },
/* 
 * activate the following define when you have one of the devices and want to 
 * build it from build-2.6 in dvb-kernel
 */
// #define CONFIG_DVB_DIBUSB_MISDESIGNED_DEVICES
#ifdef CONFIG_DVB_DIBUSB_MISDESIGNED_DEVICES
/* 34 */	{ USB_DEVICE(USB_VID_ANCHOR,		USB_PID_ULTIMA_TVBOX_ANCHOR_COLD) },
/* 35 */	{ USB_DEVICE(USB_VID_CYPRESS,		USB_PID_ULTIMA_TVBOX_USB2_FX_COLD) },
/* 36 */	{ USB_DEVICE(USB_VID_ANCHOR,		USB_PID_ULTIMA_TVBOX_USB2_FX_WARM) },
/* 37 */	{ USB_DEVICE(USB_VID_ANCHOR,		USB_PID_DIBCOM_ANCHOR_2135_COLD) },
#endif
			{ }		/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, dib_table);

static struct dibusb_usb_controller dibusb_usb_ctrl[] = {
	{ .name = "Cypress AN2135", .cpu_cs_register = 0x7f92 },
	{ .name = "Cypress AN2235", .cpu_cs_register = 0x7f92 },
	{ .name = "Cypress FX2",    .cpu_cs_register = 0xe600 },
};

struct dibusb_tuner dibusb_tuner[] = {
	{ DIBUSB_TUNER_CABLE_THOMSON, 
	  0x61 
	},
	{ DIBUSB_TUNER_COFDM_PANASONIC_ENV57H1XD5,
	  0x60 
	},
	{ DIBUSB_TUNER_CABLE_LG_TDTP_E102P,
	  0x61
	},
	{ DIBUSB_TUNER_COFDM_PANASONIC_ENV77H11D5,
	  0x60
	},
};

static struct dibusb_demod dibusb_demod[] = {
	{ DIBUSB_DIB3000MB,
	  16,
	  { 0x8, 0 },
	},
	{ DIBUSB_DIB3000MC,
	  32,
	  { 0x9, 0xa, 0xb, 0xc }, 
	},
	{ DIBUSB_MT352,
	  254,
	  { 0xf, 0 }, 
	},
	{ DTT200U_FE,
	  8,
	  { 0xff,0 }, /* there is no i2c bus in this device */
	}
};

static struct dibusb_device_class dibusb_device_classes[] = {
	{ .id = DIBUSB1_1, .usb_ctrl = &dibusb_usb_ctrl[0],
	  .firmware = "dvb-dibusb-5.0.0.11.fw",
	  .pipe_cmd = 0x01, .pipe_data = 0x02, 
	  .urb_count = 7, .urb_buffer_size = 4096,
	  DIBUSB_RC_NEC_PROTOCOL,
	  &dibusb_demod[DIBUSB_DIB3000MB],
	  &dibusb_tuner[DIBUSB_TUNER_CABLE_THOMSON],
	},
	{ DIBUSB1_1_AN2235, &dibusb_usb_ctrl[1],
	  "dvb-dibusb-an2235-1.fw",
	  0x01, 0x02, 
	  7, 4096,
	  DIBUSB_RC_NEC_PROTOCOL,
	  &dibusb_demod[DIBUSB_DIB3000MB],
	  &dibusb_tuner[DIBUSB_TUNER_CABLE_THOMSON],
	},
	{ DIBUSB2_0,&dibusb_usb_ctrl[2],
	  "dvb-dibusb-6.0.0.5.fw",
	  0x01, 0x06, 
	  7, 4096,
	  DIBUSB_RC_NEC_PROTOCOL,
	  &dibusb_demod[DIBUSB_DIB3000MC],
	  &dibusb_tuner[DIBUSB_TUNER_COFDM_PANASONIC_ENV57H1XD5],
	},
	{ UMT2_0, &dibusb_usb_ctrl[2],
	  "dvb-dibusb-umt-2.fw",
	  0x01, 0x06,
	  20, 512,
	  DIBUSB_RC_NO,
	  &dibusb_demod[DIBUSB_MT352],
	  &dibusb_tuner[DIBUSB_TUNER_CABLE_LG_TDTP_E102P],
	},
	{ DIBUSB2_0B,&dibusb_usb_ctrl[2],
	  "dvb-dibusb-adstech-usb2-1.fw",
	  0x01, 0x06,
	  7, 4096,
	  DIBUSB_RC_NEC_PROTOCOL,
	  &dibusb_demod[DIBUSB_DIB3000MB],
	  &dibusb_tuner[DIBUSB_TUNER_CABLE_THOMSON],
	},
	{ NOVAT_USB2,&dibusb_usb_ctrl[2],
	  "dvb-dibusb-nova-t-1.fw",
	  0x01, 0x06,
	  7, 4096,
	  DIBUSB_RC_HAUPPAUGE_PROTO,
	  &dibusb_demod[DIBUSB_DIB3000MC],
	  &dibusb_tuner[DIBUSB_TUNER_COFDM_PANASONIC_ENV57H1XD5],
	},
	{ DTT200U,&dibusb_usb_ctrl[2],
	  "dvb-dtt200u-1.fw",
	  0x01, 0x02,
	  7, 4096,
	  DIBUSB_RC_NO,
	  &dibusb_demod[DTT200U_FE],
	  NULL, /* no explicit tuner/pll-programming necessary (it has the ENV57H1XD5) */
	},
};

static struct dibusb_usb_device dibusb_devices[] = {
	{	"TwinhanDTV USB1.1 / Magic Box / HAMA USB1.1 DVB-T device",
		&dibusb_device_classes[DIBUSB1_1],
		{ &dib_table[19], &dib_table[21], NULL},
		{ &dib_table[20], &dib_table[22], NULL},
	},
	{	"KWorld V-Stream XPERT DTV - DVB-T USB1.1",
		&dibusb_device_classes[DIBUSB1_1],
		{ &dib_table[11], NULL },
		{ &dib_table[12], NULL },
	},
	{	"Grandtec USB1.1 DVB-T",
		&dibusb_device_classes[DIBUSB1_1],
		{ &dib_table[13], &dib_table[15], NULL },
		{ &dib_table[14], &dib_table[16], NULL },
	},
	{	"DiBcom USB1.1 DVB-T reference design (MOD3000)",
		&dibusb_device_classes[DIBUSB1_1],
		{ &dib_table[7],  NULL },
		{ &dib_table[8],  NULL },
	},
	{	"Artec T1 USB1.1 TVBOX with AN2135",
		&dibusb_device_classes[DIBUSB1_1],
		{ &dib_table[23], NULL },
		{ &dib_table[24], NULL },
	},
	{	"Artec T1 USB1.1 TVBOX with AN2235",
		&dibusb_device_classes[DIBUSB1_1_AN2235],
		{ &dib_table[25], NULL },
		{ &dib_table[26], NULL },
	},
	{	"Avermedia AverTV DVBT USB1.1",
		&dibusb_device_classes[DIBUSB1_1],
		{ &dib_table[0],  NULL },
		{ &dib_table[1],  NULL },
	},
	{	"Compro Videomate DVB-U2000 - DVB-T USB1.1 (please confirm to linux-dvb)",
		&dibusb_device_classes[DIBUSB1_1],
		{ &dib_table[4], &dib_table[6], NULL},
		{ &dib_table[5], NULL },
	},
	{	"Unkown USB1.1 DVB-T device ???? please report the name to the author",
		&dibusb_device_classes[DIBUSB1_1],
		{ &dib_table[17], NULL },
		{ &dib_table[18], NULL },
	},
	{	"DiBcom USB2.0 DVB-T reference design (MOD3000P)",
		&dibusb_device_classes[DIBUSB2_0],
		{ &dib_table[9],  NULL },
		{ &dib_table[10], NULL },
	},
	{	"Artec T1 USB2.0 TVBOX (please report the warm ID)",
		&dibusb_device_classes[DIBUSB2_0],
		{ &dib_table[27], NULL },
		{ NULL },
	},
	{	"Hauppauge WinTV NOVA-T USB2",
		&dibusb_device_classes[NOVAT_USB2],
		{ &dib_table[30], NULL },
		{ &dib_table[31], NULL },
	},
	{	"DTT200U (Yakumo/Hama/Typhoon) DVB-T USB2.0",
		&dibusb_device_classes[DTT200U],
		{ &dib_table[2], NULL },
		{ &dib_table[3], NULL },
	},	
	{	"Hanftek UMT-010 DVB-T USB2.0",
		&dibusb_device_classes[UMT2_0],
		{ &dib_table[28], NULL },
		{ &dib_table[29], NULL },
	},	
	{	"KWorld/ADSTech Instant DVB-T USB 2.0",
		&dibusb_device_classes[DIBUSB2_0B],
		{ &dib_table[32], NULL },
		{ &dib_table[33], NULL }, /* device ID with default DIBUSB2_0-firmware */
	},
#ifdef CONFIG_DVB_DIBUSB_MISDESIGNED_DEVICES
	{	"Artec T1 USB1.1 TVBOX with AN2235 (misdesigned)",
		&dibusb_device_classes[DIBUSB1_1_AN2235],
		{ &dib_table[34], NULL },
		{ NULL },
	},
	{	"Artec T1 USB2.0 TVBOX with FX2 IDs (misdesigned, please report the warm ID)",
		&dibusb_device_classes[DTT200U],
		{ &dib_table[35], NULL },
		{ &dib_table[36], NULL }, /* undefined, it could be that the device will get another USB ID in warm state */
	},
	{	"DiBcom USB1.1 DVB-T reference design (MOD3000) with AN2135 default IDs",
		&dibusb_device_classes[DIBUSB1_1],
		{ &dib_table[37], NULL },
		{ NULL },
	},
#endif
};

static int dibusb_exit(struct usb_dibusb *dib)
{
	deb_info("init_state before exiting everything: %x\n",dib->init_state);
	dibusb_remote_exit(dib);
	dibusb_fe_exit(dib);
	dibusb_i2c_exit(dib);
	dibusb_dvb_exit(dib);
	dibusb_urb_exit(dib);
	deb_info("init_state should be zero now: %x\n",dib->init_state);
	dib->init_state = DIBUSB_STATE_INIT;
	kfree(dib);
	return 0;
}

static int dibusb_init(struct usb_dibusb *dib)
{
	int ret = 0;
	sema_init(&dib->usb_sem, 1);
	sema_init(&dib->i2c_sem, 1);

	dib->init_state = DIBUSB_STATE_INIT;
	
	if ((ret = dibusb_urb_init(dib)) ||
		(ret = dibusb_dvb_init(dib)) || 
		(ret = dibusb_i2c_init(dib))) {
		dibusb_exit(dib);
		return ret;
	}

	if ((ret = dibusb_fe_init(dib)))
		err("could not initialize a frontend.");
	
	if ((ret = dibusb_remote_init(dib)))
		err("could not initialize remote control.");
	
	return 0;
}

static struct dibusb_usb_device * dibusb_device_class_quirk(struct usb_device *udev, struct dibusb_usb_device *dev)
{
	int i;

	/* Quirk for the Kworld/ADSTech Instant USB2.0 device. It has the same USB
	 * IDs like the USB1.1 KWorld after loading the firmware. Which is a bad
	 * idea and make this quirk necessary.
	 */
	if (dev->dev_cl->id == DIBUSB1_1 && udev->speed == USB_SPEED_HIGH) {
		info("this seems to be the Kworld/ADSTech Instant USB2.0 device or equal.");
		for (i = 0; i < sizeof(dibusb_devices)/sizeof(struct dibusb_usb_device); i++) {
			if (dibusb_devices[i].dev_cl->id == DIBUSB2_0B) {
				dev = &dibusb_devices[i];
				break;
			}
		}
	}

	return dev;
}

static struct dibusb_usb_device * dibusb_find_device (struct usb_device *udev,int *cold)
{
	int i,j;
	struct dibusb_usb_device *dev = NULL;
	*cold = -1;

	for (i = 0; i < sizeof(dibusb_devices)/sizeof(struct dibusb_usb_device); i++) {
		for (j = 0; j < DIBUSB_ID_MAX_NUM && dibusb_devices[i].cold_ids[j] != NULL; j++) {
			deb_info("check for cold %x %x\n",dibusb_devices[i].cold_ids[j]->idVendor, dibusb_devices[i].cold_ids[j]->idProduct);
			if (dibusb_devices[i].cold_ids[j]->idVendor == le16_to_cpu(udev->descriptor.idVendor) &&
				dibusb_devices[i].cold_ids[j]->idProduct == le16_to_cpu(udev->descriptor.idProduct)) {
				*cold = 1;
				dev = &dibusb_devices[i];
				break;
			}
		}

		if (dev != NULL)
			break;

		for (j = 0; j < DIBUSB_ID_MAX_NUM && dibusb_devices[i].warm_ids[j] != NULL; j++) {
			deb_info("check for warm %x %x\n",dibusb_devices[i].warm_ids[j]->idVendor, dibusb_devices[i].warm_ids[j]->idProduct);
			if (dibusb_devices[i].warm_ids[j]->idVendor == le16_to_cpu(udev->descriptor.idVendor) &&
				dibusb_devices[i].warm_ids[j]->idProduct == le16_to_cpu(udev->descriptor.idProduct)) {
				*cold = 0;
				dev = &dibusb_devices[i];
				break;
			}
		}
	}

	if (dev != NULL)
		dev = dibusb_device_class_quirk(udev,dev);

	return dev;
}

/*
 * USB 
 */
static int dibusb_probe(struct usb_interface *intf, 
		const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_dibusb *dib = NULL;
	struct dibusb_usb_device *dibdev = NULL;
	
	int ret = -ENOMEM,cold=0;

	if ((dibdev = dibusb_find_device(udev,&cold)) == NULL) {
		err("something went very wrong, "
				"unknown product ID: %.4x",le16_to_cpu(udev->descriptor.idProduct));
		return -ENODEV;
	}
	
	if (cold == 1) {
		info("found a '%s' in cold state, will try to load a firmware",dibdev->name);
		ret = dibusb_loadfirmware(udev,dibdev);
	} else {
		info("found a '%s' in warm state.",dibdev->name);
		dib = kmalloc(sizeof(struct usb_dibusb),GFP_KERNEL);
		if (dib == NULL) {
			err("no memory");
			return ret;
		}
		memset(dib,0,sizeof(struct usb_dibusb));
		
		dib->udev = udev;
		dib->dibdev = dibdev;

		/* store parameters to structures */
		dib->rc_query_interval = rc_query_interval;
		dib->pid_parse = pid_parse;
		dib->rc_key_repeat_count = rc_key_repeat_count;

		usb_set_intfdata(intf, dib);
		
		ret = dibusb_init(dib);
	}
	
	if (ret == 0)
		info("%s successfully initialized and connected.",dibdev->name);
	else 
		info("%s error while loading driver (%d)",dibdev->name,ret);
	return ret;
}

static void dibusb_disconnect(struct usb_interface *intf)
{
	struct usb_dibusb *dib = usb_get_intfdata(intf);
	const char *name = DRIVER_DESC;
	
	usb_set_intfdata(intf,NULL);
	if (dib != NULL && dib->dibdev != NULL) {
		name = dib->dibdev->name;
		dibusb_exit(dib);
	}
	info("%s successfully deinitialized and disconnected.",name);
	
}

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver dibusb_driver = {
	.owner		= THIS_MODULE,
	.name		= DRIVER_DESC,
	.probe 		= dibusb_probe,
	.disconnect = dibusb_disconnect,
	.id_table 	= dib_table,
};

/* module stuff */
static int __init usb_dibusb_init(void)
{
	int result;
	if ((result = usb_register(&dibusb_driver))) {
		err("usb_register failed. Error number %d",result);
		return result;
	}
	
	return 0;
}

static void __exit usb_dibusb_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&dibusb_driver);
}

module_init (usb_dibusb_init);
module_exit (usb_dibusb_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
