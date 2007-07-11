/*
 *  USB HID quirks support for Linux
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2007 Paul Walmsley
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>

#define USB_VENDOR_ID_A4TECH		0x09da
#define USB_DEVICE_ID_A4TECH_WCP32PU	0x0006

#define USB_VENDOR_ID_AASHIMA		0x06d6
#define USB_DEVICE_ID_AASHIMA_GAMEPAD	0x0025
#define USB_DEVICE_ID_AASHIMA_PREDATOR	0x0026

#define USB_VENDOR_ID_ACECAD		0x0460
#define USB_DEVICE_ID_ACECAD_FLAIR	0x0004
#define USB_DEVICE_ID_ACECAD_302	0x0008

#define USB_VENDOR_ID_AIPTEK		0x08ca
#define USB_DEVICE_ID_AIPTEK_01		0x0001
#define USB_DEVICE_ID_AIPTEK_10		0x0010
#define USB_DEVICE_ID_AIPTEK_20		0x0020
#define USB_DEVICE_ID_AIPTEK_21		0x0021
#define USB_DEVICE_ID_AIPTEK_22		0x0022
#define USB_DEVICE_ID_AIPTEK_23		0x0023
#define USB_DEVICE_ID_AIPTEK_24		0x0024

#define USB_VENDOR_ID_AIRCABLE		0x16CA
#define USB_DEVICE_ID_AIRCABLE1		0x1502

#define USB_VENDOR_ID_ALCOR		0x058f
#define USB_DEVICE_ID_ALCOR_USBRS232	0x9720

#define USB_VENDOR_ID_ALPS		0x0433
#define USB_DEVICE_ID_IBM_GAMEPAD	0x1101

#define USB_VENDOR_ID_APPLE		0x05ac
#define USB_DEVICE_ID_APPLE_MIGHTYMOUSE	0x0304
#define USB_DEVICE_ID_APPLE_FOUNTAIN_ANSI	0x020e
#define USB_DEVICE_ID_APPLE_FOUNTAIN_ISO	0x020f
#define USB_DEVICE_ID_APPLE_GEYSER_ANSI	0x0214
#define USB_DEVICE_ID_APPLE_GEYSER_ISO	0x0215
#define USB_DEVICE_ID_APPLE_GEYSER_JIS	0x0216
#define USB_DEVICE_ID_APPLE_GEYSER3_ANSI	0x0217
#define USB_DEVICE_ID_APPLE_GEYSER3_ISO	0x0218
#define USB_DEVICE_ID_APPLE_GEYSER3_JIS	0x0219
#define USB_DEVICE_ID_APPLE_GEYSER4_ANSI	0x021a
#define USB_DEVICE_ID_APPLE_GEYSER4_ISO	0x021b
#define USB_DEVICE_ID_APPLE_GEYSER4_JIS	0x021c
#define USB_DEVICE_ID_APPLE_FOUNTAIN_TP_ONLY	0x030a
#define USB_DEVICE_ID_APPLE_GEYSER1_TP_ONLY	0x030b

#define USB_VENDOR_ID_ATEN		0x0557
#define USB_DEVICE_ID_ATEN_UC100KM	0x2004
#define USB_DEVICE_ID_ATEN_CS124U	0x2202
#define USB_DEVICE_ID_ATEN_2PORTKVM	0x2204
#define USB_DEVICE_ID_ATEN_4PORTKVM	0x2205
#define USB_DEVICE_ID_ATEN_4PORTKVMC	0x2208

#define USB_VENDOR_ID_BELKIN           0x050d
#define USB_DEVICE_ID_FLIP_KVM         0x3201

#define USB_VENDOR_ID_BERKSHIRE		0x0c98
#define USB_DEVICE_ID_BERKSHIRE_PCWD	0x1140

#define USB_VENDOR_ID_CHERRY		0x046a
#define USB_DEVICE_ID_CHERRY_CYMOTION	0x0023

#define USB_VENDOR_ID_CHIC		0x05fe
#define USB_DEVICE_ID_CHIC_GAMEPAD	0x0014

#define USB_VENDOR_ID_CIDC		0x1677

#define USB_VENDOR_ID_CODEMERCS		0x07c0
#define USB_DEVICE_ID_CODEMERCS_IOW_FIRST	0x1500
#define USB_DEVICE_ID_CODEMERCS_IOW_LAST	0x15ff

#define USB_VENDOR_ID_CYPRESS		0x04b4
#define USB_DEVICE_ID_CYPRESS_MOUSE	0x0001
#define USB_DEVICE_ID_CYPRESS_HIDCOM	0x5500
#define USB_DEVICE_ID_CYPRESS_ULTRAMOUSE	0x7417
#define USB_DEVICE_ID_CYPRESS_BARCODE_1	0xde61
#define USB_DEVICE_ID_CYPRESS_BARCODE_2	0xde64

#define USB_VENDOR_ID_DELL		0x413c
#define USB_DEVICE_ID_DELL_W7658	0x2005

#define USB_VENDOR_ID_DELORME		0x1163
#define USB_DEVICE_ID_DELORME_EARTHMATE 0x0100
#define USB_DEVICE_ID_DELORME_EM_LT20	0x0200

#define USB_VENDOR_ID_ESSENTIAL_REALITY	0x0d7f
#define USB_DEVICE_ID_ESSENTIAL_REALITY_P5 0x0100

#define USB_VENDOR_ID_GAMERON		0x0810
#define USB_DEVICE_ID_GAMERON_DUAL_PSX_ADAPTOR	0x0001

#define USB_VENDOR_ID_GLAB		0x06c2
#define USB_DEVICE_ID_4_PHIDGETSERVO_30	0x0038
#define USB_DEVICE_ID_1_PHIDGETSERVO_30	0x0039
#define USB_DEVICE_ID_0_0_4_IF_KIT	0x0040
#define USB_DEVICE_ID_0_16_16_IF_KIT	0x0044
#define USB_DEVICE_ID_8_8_8_IF_KIT	0x0045
#define USB_DEVICE_ID_0_8_7_IF_KIT	0x0051
#define USB_DEVICE_ID_0_8_8_IF_KIT	0x0053
#define USB_DEVICE_ID_PHIDGET_MOTORCONTROL	0x0058

#define USB_VENDOR_ID_GRIFFIN		0x077d
#define USB_DEVICE_ID_POWERMATE		0x0410
#define USB_DEVICE_ID_SOUNDKNOB		0x04AA

#define USB_VENDOR_ID_GTCO		0x078c
#define USB_DEVICE_ID_GTCO_90		0x0090
#define USB_DEVICE_ID_GTCO_100		0x0100
#define USB_DEVICE_ID_GTCO_101		0x0101
#define USB_DEVICE_ID_GTCO_103		0x0103
#define USB_DEVICE_ID_GTCO_104		0x0104
#define USB_DEVICE_ID_GTCO_105		0x0105
#define USB_DEVICE_ID_GTCO_106		0x0106
#define USB_DEVICE_ID_GTCO_107		0x0107
#define USB_DEVICE_ID_GTCO_108		0x0108
#define USB_DEVICE_ID_GTCO_200		0x0200
#define USB_DEVICE_ID_GTCO_201		0x0201
#define USB_DEVICE_ID_GTCO_202		0x0202
#define USB_DEVICE_ID_GTCO_203		0x0203
#define USB_DEVICE_ID_GTCO_204		0x0204
#define USB_DEVICE_ID_GTCO_205		0x0205
#define USB_DEVICE_ID_GTCO_206		0x0206
#define USB_DEVICE_ID_GTCO_207		0x0207
#define USB_DEVICE_ID_GTCO_300		0x0300
#define USB_DEVICE_ID_GTCO_301		0x0301
#define USB_DEVICE_ID_GTCO_302		0x0302
#define USB_DEVICE_ID_GTCO_303		0x0303
#define USB_DEVICE_ID_GTCO_304		0x0304
#define USB_DEVICE_ID_GTCO_305		0x0305
#define USB_DEVICE_ID_GTCO_306		0x0306
#define USB_DEVICE_ID_GTCO_307		0x0307
#define USB_DEVICE_ID_GTCO_308		0x0308
#define USB_DEVICE_ID_GTCO_309		0x0309
#define USB_DEVICE_ID_GTCO_400		0x0400
#define USB_DEVICE_ID_GTCO_401		0x0401
#define USB_DEVICE_ID_GTCO_402		0x0402
#define USB_DEVICE_ID_GTCO_403		0x0403
#define USB_DEVICE_ID_GTCO_404		0x0404
#define USB_DEVICE_ID_GTCO_405		0x0405
#define USB_DEVICE_ID_GTCO_500		0x0500
#define USB_DEVICE_ID_GTCO_501		0x0501
#define USB_DEVICE_ID_GTCO_502		0x0502
#define USB_DEVICE_ID_GTCO_503		0x0503
#define USB_DEVICE_ID_GTCO_504		0x0504
#define USB_DEVICE_ID_GTCO_1000		0x1000
#define USB_DEVICE_ID_GTCO_1001		0x1001
#define USB_DEVICE_ID_GTCO_1002		0x1002
#define USB_DEVICE_ID_GTCO_1003		0x1003
#define USB_DEVICE_ID_GTCO_1004		0x1004
#define USB_DEVICE_ID_GTCO_1005		0x1005
#define USB_DEVICE_ID_GTCO_1006		0x1006

#define USB_VENDOR_ID_HAPP		0x078b
#define USB_DEVICE_ID_UGCI_DRIVING	0x0010
#define USB_DEVICE_ID_UGCI_FLYING	0x0020
#define USB_DEVICE_ID_UGCI_FIGHTING	0x0030

#define USB_VENDOR_ID_IMATION		0x0718
#define USB_DEVICE_ID_DISC_STAKKA	0xd000

#define USB_VENDOR_ID_KBGEAR		0x084e
#define USB_DEVICE_ID_KBGEAR_JAMSTUDIO	0x1001

#define USB_VENDOR_ID_LD		0x0f11
#define USB_DEVICE_ID_LD_CASSY		0x1000
#define USB_DEVICE_ID_LD_POCKETCASSY	0x1010
#define USB_DEVICE_ID_LD_MOBILECASSY	0x1020
#define USB_DEVICE_ID_LD_JWM		0x1080
#define USB_DEVICE_ID_LD_DMMP		0x1081
#define USB_DEVICE_ID_LD_UMIP		0x1090
#define USB_DEVICE_ID_LD_XRAY1		0x1100
#define USB_DEVICE_ID_LD_XRAY2		0x1101
#define USB_DEVICE_ID_LD_VIDEOCOM	0x1200
#define USB_DEVICE_ID_LD_COM3LAB	0x2000
#define USB_DEVICE_ID_LD_TELEPORT	0x2010
#define USB_DEVICE_ID_LD_NETWORKANALYSER 0x2020
#define USB_DEVICE_ID_LD_POWERCONTROL	0x2030
#define USB_DEVICE_ID_LD_MACHINETEST	0x2040

#define USB_VENDOR_ID_LOGITECH		0x046d
#define USB_DEVICE_ID_LOGITECH_RECEIVER	0xc101
#define USB_DEVICE_ID_LOGITECH_WHEEL	0xc294
#define USB_DEVICE_ID_LOGITECH_KBD	0xc311
#define USB_DEVICE_ID_S510_RECEIVER	0xc50c
#define USB_DEVICE_ID_S510_RECEIVER_2	0xc517
#define USB_DEVICE_ID_LOGITECH_CORDLESS_DESKTOP_LX500	0xc512
#define USB_DEVICE_ID_MX3000_RECEIVER	0xc513
#define USB_DEVICE_ID_DINOVO_EDGE	0xc714

#define USB_VENDOR_ID_MCC		0x09db
#define USB_DEVICE_ID_MCC_PMD1024LS	0x0076
#define USB_DEVICE_ID_MCC_PMD1208LS	0x007a

#define USB_VENDOR_ID_MGE		0x0463
#define USB_DEVICE_ID_MGE_UPS		0xffff
#define USB_DEVICE_ID_MGE_UPS1		0x0001

#define USB_VENDOR_ID_MICROSOFT		0x045e
#define USB_DEVICE_ID_SIDEWINDER_GV	0x003b

#define USB_VENDOR_ID_NCR		0x0404
#define USB_DEVICE_ID_NCR_FIRST		0x0300
#define USB_DEVICE_ID_NCR_LAST		0x03ff

#define USB_VENDOR_ID_NEC		0x073e
#define USB_DEVICE_ID_NEC_USB_GAME_PAD	0x0301

#define USB_VENDOR_ID_ONTRAK		0x0a07
#define USB_DEVICE_ID_ONTRAK_ADU100	0x0064

#define USB_VENDOR_ID_PANJIT		0x134c

#define USB_VENDOR_ID_PANTHERLORD	0x0810
#define USB_DEVICE_ID_PANTHERLORD_TWIN_USB_JOYSTICK	0x0001

#define USB_VENDOR_ID_PETALYNX		0x18b1
#define USB_DEVICE_ID_PETALYNX_MAXTER_REMOTE	0x0037

#define USB_VENDOR_ID_PLAYDOTCOM	0x0b43
#define USB_DEVICE_ID_PLAYDOTCOM_EMS_USBII	0x0003

#define USB_VENDOR_ID_SAITEK		0x06a3
#define USB_DEVICE_ID_SAITEK_RUMBLEPAD	0xff17

#define USB_VENDOR_ID_SONY			0x054c
#define USB_DEVICE_ID_SONY_PS3_CONTROLLER	0x0268

#define USB_VENDOR_ID_SUN		0x0430
#define USB_DEVICE_ID_RARITAN_KVM_DONGLE	0xcdab

#define USB_VENDOR_ID_TOPMAX		0x0663
#define USB_DEVICE_ID_TOPMAX_COBRAPAD	0x0103

#define USB_VENDOR_ID_TURBOX		0x062a
#define USB_DEVICE_ID_TURBOX_KEYBOARD	0x0201

#define USB_VENDOR_ID_VERNIER		0x08f7
#define USB_DEVICE_ID_VERNIER_LABPRO	0x0001
#define USB_DEVICE_ID_VERNIER_GOTEMP	0x0002
#define USB_DEVICE_ID_VERNIER_SKIP	0x0003
#define USB_DEVICE_ID_VERNIER_CYCLOPS	0x0004

#define USB_VENDOR_ID_WACOM		0x056a

#define USB_VENDOR_ID_WISEGROUP		0x0925
#define USB_DEVICE_ID_1_PHIDGETSERVO_20	0x8101
#define USB_DEVICE_ID_4_PHIDGETSERVO_20	0x8104
#define USB_DEVICE_ID_8_8_4_IF_KIT	0x8201
#define USB_DEVICE_ID_QUAD_USB_JOYPAD	0x8800
#define USB_DEVICE_ID_DUAL_USB_JOYPAD	0x8866

#define USB_VENDOR_ID_WISEGROUP_LTD	0x6677
#define USB_DEVICE_ID_SMARTJOY_DUAL_PLUS 0x8802

#define USB_VENDOR_ID_YEALINK		0x6993
#define USB_DEVICE_ID_YEALINK_P1K_P4K_B2K	0xb001

/*
 * Alphabetically sorted blacklist by quirk type.
 */

static const struct hid_blacklist {
	__u16 idVendor;
	__u16 idProduct;
	__u32 quirks;
} hid_blacklist[] = {

	{ USB_VENDOR_ID_A4TECH, USB_DEVICE_ID_A4TECH_WCP32PU, HID_QUIRK_2WHEEL_MOUSE_HACK_7 },
	{ USB_VENDOR_ID_CYPRESS, USB_DEVICE_ID_CYPRESS_MOUSE, HID_QUIRK_2WHEEL_MOUSE_HACK_5 },

	{ USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_RECEIVER, HID_QUIRK_BAD_RELATIVE_KEYS },

	{ USB_VENDOR_ID_AASHIMA, USB_DEVICE_ID_AASHIMA_GAMEPAD, HID_QUIRK_BADPAD },
	{ USB_VENDOR_ID_AASHIMA, USB_DEVICE_ID_AASHIMA_PREDATOR, HID_QUIRK_BADPAD },
	{ USB_VENDOR_ID_ALPS, USB_DEVICE_ID_IBM_GAMEPAD, HID_QUIRK_BADPAD },
	{ USB_VENDOR_ID_CHIC, USB_DEVICE_ID_CHIC_GAMEPAD, HID_QUIRK_BADPAD },
	{ USB_VENDOR_ID_GAMERON, USB_DEVICE_ID_GAMERON_DUAL_PSX_ADAPTOR, HID_QUIRK_MULTI_INPUT },
	{ USB_VENDOR_ID_HAPP, USB_DEVICE_ID_UGCI_DRIVING, HID_QUIRK_BADPAD | HID_QUIRK_MULTI_INPUT },
	{ USB_VENDOR_ID_HAPP, USB_DEVICE_ID_UGCI_FLYING, HID_QUIRK_BADPAD | HID_QUIRK_MULTI_INPUT },
	{ USB_VENDOR_ID_HAPP, USB_DEVICE_ID_UGCI_FIGHTING, HID_QUIRK_BADPAD | HID_QUIRK_MULTI_INPUT },
	{ USB_VENDOR_ID_NEC, USB_DEVICE_ID_NEC_USB_GAME_PAD, HID_QUIRK_BADPAD },
	{ USB_VENDOR_ID_SAITEK, USB_DEVICE_ID_SAITEK_RUMBLEPAD, HID_QUIRK_BADPAD },
	{ USB_VENDOR_ID_TOPMAX, USB_DEVICE_ID_TOPMAX_COBRAPAD, HID_QUIRK_BADPAD },
	
	{ USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_DINOVO_EDGE, HID_QUIRK_DUPLICATE_USAGES },

	{ USB_VENDOR_ID_BELKIN, USB_DEVICE_ID_FLIP_KVM, HID_QUIRK_HIDDEV },
	{ USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_SIDEWINDER_GV, HID_QUIRK_HIDINPUT },

	{ USB_VENDOR_ID_AIPTEK, USB_DEVICE_ID_AIPTEK_01, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_AIPTEK, USB_DEVICE_ID_AIPTEK_10, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_AIPTEK, USB_DEVICE_ID_AIPTEK_20, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_AIPTEK, USB_DEVICE_ID_AIPTEK_21, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_AIPTEK, USB_DEVICE_ID_AIPTEK_22, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_AIPTEK, USB_DEVICE_ID_AIPTEK_23, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_AIPTEK, USB_DEVICE_ID_AIPTEK_24, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_AIRCABLE, USB_DEVICE_ID_AIRCABLE1, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_ALCOR, USB_DEVICE_ID_ALCOR_USBRS232, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_BERKSHIRE, USB_DEVICE_ID_BERKSHIRE_PCWD, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_CIDC, 0x0103, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_CYPRESS, USB_DEVICE_ID_CYPRESS_HIDCOM, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_CYPRESS, USB_DEVICE_ID_CYPRESS_ULTRAMOUSE, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_DELORME, USB_DEVICE_ID_DELORME_EARTHMATE, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_DELORME, USB_DEVICE_ID_DELORME_EM_LT20, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_ESSENTIAL_REALITY, USB_DEVICE_ID_ESSENTIAL_REALITY_P5, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GLAB, USB_DEVICE_ID_4_PHIDGETSERVO_30, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GLAB, USB_DEVICE_ID_1_PHIDGETSERVO_30, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GLAB, USB_DEVICE_ID_0_0_4_IF_KIT, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GLAB, USB_DEVICE_ID_0_16_16_IF_KIT, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GLAB, USB_DEVICE_ID_8_8_8_IF_KIT, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GLAB, USB_DEVICE_ID_0_8_7_IF_KIT, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GLAB, USB_DEVICE_ID_0_8_8_IF_KIT, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GLAB, USB_DEVICE_ID_PHIDGET_MOTORCONTROL, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GRIFFIN, USB_DEVICE_ID_POWERMATE, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GRIFFIN, USB_DEVICE_ID_SOUNDKNOB, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_90, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_100, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_101, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_103, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_104, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_105, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_106, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_107, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_108, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_200, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_201, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_202, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_203, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_204, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_205, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_206, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_207, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_300, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_301, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_302, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_303, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_304, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_305, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_306, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_307, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_308, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_309, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_400, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_401, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_402, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_403, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_404, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_405, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_500, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_501, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_502, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_503, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_504, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_1000, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_1001, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_1002, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_1003, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_1004, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_1005, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_GTCO, USB_DEVICE_ID_GTCO_1006, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_IMATION, USB_DEVICE_ID_DISC_STAKKA, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_KBGEAR, USB_DEVICE_ID_KBGEAR_JAMSTUDIO, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_CASSY, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_POCKETCASSY, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_MOBILECASSY, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_JWM, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_DMMP, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_UMIP, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_XRAY1, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_XRAY2, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_VIDEOCOM, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_COM3LAB, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_TELEPORT, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_NETWORKANALYSER, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_POWERCONTROL, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_MACHINETEST, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_MCC, USB_DEVICE_ID_MCC_PMD1024LS, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_MCC, USB_DEVICE_ID_MCC_PMD1208LS, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_MGE, USB_DEVICE_ID_MGE_UPS, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_MGE, USB_DEVICE_ID_MGE_UPS1, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_ONTRAK, USB_DEVICE_ID_ONTRAK_ADU100, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_ONTRAK, USB_DEVICE_ID_ONTRAK_ADU100 + 20, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_ONTRAK, USB_DEVICE_ID_ONTRAK_ADU100 + 30, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_ONTRAK, USB_DEVICE_ID_ONTRAK_ADU100 + 100, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_ONTRAK, USB_DEVICE_ID_ONTRAK_ADU100 + 108, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_ONTRAK, USB_DEVICE_ID_ONTRAK_ADU100 + 118, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_ONTRAK, USB_DEVICE_ID_ONTRAK_ADU100 + 200, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_ONTRAK, USB_DEVICE_ID_ONTRAK_ADU100 + 300, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_ONTRAK, USB_DEVICE_ID_ONTRAK_ADU100 + 400, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_ONTRAK, USB_DEVICE_ID_ONTRAK_ADU100 + 500, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_PANJIT, 0x0001, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_PANJIT, 0x0002, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_PANJIT, 0x0003, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_PANJIT, 0x0004, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_VERNIER, USB_DEVICE_ID_VERNIER_LABPRO, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_VERNIER, USB_DEVICE_ID_VERNIER_GOTEMP, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_VERNIER, USB_DEVICE_ID_VERNIER_SKIP, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_VERNIER, USB_DEVICE_ID_VERNIER_CYCLOPS, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_WISEGROUP, USB_DEVICE_ID_4_PHIDGETSERVO_20, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_WISEGROUP, USB_DEVICE_ID_1_PHIDGETSERVO_20, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_WISEGROUP, USB_DEVICE_ID_8_8_4_IF_KIT, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_YEALINK, USB_DEVICE_ID_YEALINK_P1K_P4K_B2K, HID_QUIRK_IGNORE },

	{ USB_VENDOR_ID_ACECAD, USB_DEVICE_ID_ACECAD_FLAIR, HID_QUIRK_IGNORE },
	{ USB_VENDOR_ID_ACECAD, USB_DEVICE_ID_ACECAD_302, HID_QUIRK_IGNORE },

	{ USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_CORDLESS_DESKTOP_LX500, HID_QUIRK_LOGITECH_IGNORE_DOUBLED_WHEEL | HID_QUIRK_LOGITECH_EXPANDED_KEYMAP },

	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_MIGHTYMOUSE, HID_QUIRK_MIGHTYMOUSE | HID_QUIRK_INVERT_HWHEEL },

	{ USB_VENDOR_ID_PANTHERLORD, USB_DEVICE_ID_PANTHERLORD_TWIN_USB_JOYSTICK, HID_QUIRK_MULTI_INPUT | HID_QUIRK_SKIP_OUTPUT_REPORTS },
	{ USB_VENDOR_ID_PLAYDOTCOM, USB_DEVICE_ID_PLAYDOTCOM_EMS_USBII, HID_QUIRK_MULTI_INPUT },

	{ USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_CONTROLLER, HID_QUIRK_SONY_PS3_CONTROLLER },

	{ USB_VENDOR_ID_ATEN, USB_DEVICE_ID_ATEN_UC100KM, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_ATEN, USB_DEVICE_ID_ATEN_CS124U, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_ATEN, USB_DEVICE_ID_ATEN_2PORTKVM, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_ATEN, USB_DEVICE_ID_ATEN_4PORTKVM, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_ATEN, USB_DEVICE_ID_ATEN_4PORTKVMC, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_WHEEL, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_PETALYNX, USB_DEVICE_ID_PETALYNX_MAXTER_REMOTE, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_SUN, USB_DEVICE_ID_RARITAN_KVM_DONGLE, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_TURBOX, USB_DEVICE_ID_TURBOX_KEYBOARD, HID_QUIRK_NOGET },
	{ USB_VENDOR_ID_WISEGROUP, USB_DEVICE_ID_DUAL_USB_JOYPAD, HID_QUIRK_NOGET | HID_QUIRK_MULTI_INPUT },
	{ USB_VENDOR_ID_WISEGROUP, USB_DEVICE_ID_QUAD_USB_JOYPAD, HID_QUIRK_NOGET | HID_QUIRK_MULTI_INPUT },

	{ USB_VENDOR_ID_WISEGROUP_LTD, USB_DEVICE_ID_SMARTJOY_DUAL_PLUS, HID_QUIRK_NOGET | HID_QUIRK_MULTI_INPUT },

	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_FOUNTAIN_ANSI, HID_QUIRK_POWERBOOK_HAS_FN | HID_QUIRK_IGNORE_MOUSE },
	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_FOUNTAIN_ISO, HID_QUIRK_POWERBOOK_HAS_FN | HID_QUIRK_IGNORE_MOUSE },
	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER_ANSI, HID_QUIRK_POWERBOOK_HAS_FN | HID_QUIRK_IGNORE_MOUSE },
	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER_ISO, HID_QUIRK_POWERBOOK_HAS_FN | HID_QUIRK_IGNORE_MOUSE | HID_QUIRK_POWERBOOK_ISO_KEYBOARD},
	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER_JIS, HID_QUIRK_POWERBOOK_HAS_FN | HID_QUIRK_IGNORE_MOUSE },
	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER3_ANSI, HID_QUIRK_POWERBOOK_HAS_FN | HID_QUIRK_IGNORE_MOUSE },
	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER3_ISO, HID_QUIRK_POWERBOOK_HAS_FN | HID_QUIRK_IGNORE_MOUSE | HID_QUIRK_POWERBOOK_ISO_KEYBOARD},
	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER3_JIS, HID_QUIRK_POWERBOOK_HAS_FN | HID_QUIRK_IGNORE_MOUSE },
	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_ANSI, HID_QUIRK_POWERBOOK_HAS_FN | HID_QUIRK_IGNORE_MOUSE },
	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_ISO, HID_QUIRK_POWERBOOK_HAS_FN | HID_QUIRK_IGNORE_MOUSE | HID_QUIRK_POWERBOOK_ISO_KEYBOARD},
	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER4_JIS, HID_QUIRK_POWERBOOK_HAS_FN | HID_QUIRK_IGNORE_MOUSE },
	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_FOUNTAIN_TP_ONLY, HID_QUIRK_POWERBOOK_HAS_FN | HID_QUIRK_IGNORE_MOUSE },
	{ USB_VENDOR_ID_APPLE, USB_DEVICE_ID_APPLE_GEYSER1_TP_ONLY, HID_QUIRK_POWERBOOK_HAS_FN | HID_QUIRK_IGNORE_MOUSE },

	{ USB_VENDOR_ID_DELL, USB_DEVICE_ID_DELL_W7658, HID_QUIRK_RESET_LEDS },
	{ USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_KBD, HID_QUIRK_RESET_LEDS },

	{ 0, 0 }
};

/* Quirks for devices which require report descriptor fixup go here */
static const struct hid_rdesc_blacklist {
	__u16 idVendor;
	__u16 idProduct;
	__u32 quirks;
} hid_rdesc_blacklist[] = {

	{ USB_VENDOR_ID_CHERRY, USB_DEVICE_ID_CHERRY_CYMOTION, HID_QUIRK_RDESC_CYMOTION },

	{ USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_MX3000_RECEIVER, HID_QUIRK_RDESC_LOGITECH },
	{ USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_S510_RECEIVER, HID_QUIRK_RDESC_LOGITECH },
	{ USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_S510_RECEIVER_2, HID_QUIRK_RDESC_LOGITECH },

	{ USB_VENDOR_ID_PETALYNX, USB_DEVICE_ID_PETALYNX_MAXTER_REMOTE, HID_QUIRK_RDESC_PETALYNX },

	{ USB_VENDOR_ID_CYPRESS, USB_DEVICE_ID_CYPRESS_BARCODE_1, HID_QUIRK_RDESC_SWAPPED_MIN_MAX },
	{ USB_VENDOR_ID_CYPRESS, USB_DEVICE_ID_CYPRESS_BARCODE_2, HID_QUIRK_RDESC_SWAPPED_MIN_MAX },

	{ 0, 0 }
};

/* Dynamic HID quirks list - specified at runtime */
struct quirks_list_struct {
	struct hid_blacklist hid_bl_item;
	struct list_head node;
};

static LIST_HEAD(dquirks_list);
static DECLARE_RWSEM(dquirks_rwsem);

/* Runtime ("dynamic") quirks manipulation functions */

/**
 * usbhid_exists_dquirk: find any dynamic quirks for a USB HID device
 * @idVendor: the 16-bit USB vendor ID, in native byteorder
 * @idProduct: the 16-bit USB product ID, in native byteorder
 *
 * Description:
 *         Scans dquirks_list for a matching dynamic quirk and returns
 *         the pointer to the relevant struct hid_blacklist if found.
 *         Must be called with a read lock held on dquirks_rwsem.
 *
 * Returns: NULL if no quirk found, struct hid_blacklist * if found.
 */
static struct hid_blacklist *usbhid_exists_dquirk(const u16 idVendor,
		const u16 idProduct)
{
	struct quirks_list_struct *q;
	struct hid_blacklist *bl_entry = NULL;

	list_for_each_entry(q, &dquirks_list, node) {
		if (q->hid_bl_item.idVendor == idVendor &&
				q->hid_bl_item.idProduct == idProduct) {
			bl_entry = &q->hid_bl_item;
			break;
		}
	}

	if (bl_entry != NULL)
		dbg_hid("Found dynamic quirk 0x%x for USB HID vendor 0x%hx prod 0x%hx\n",
				bl_entry->quirks, bl_entry->idVendor,
				bl_entry->idProduct);

	return bl_entry;
}


/**
 * usbhid_modify_dquirk: add/replace a HID quirk
 * @idVendor: the 16-bit USB vendor ID, in native byteorder
 * @idProduct: the 16-bit USB product ID, in native byteorder
 * @quirks: the u32 quirks value to add/replace
 *
 * Description:
 *         If an dynamic quirk exists in memory for this (idVendor,
 *         idProduct) pair, replace its quirks value with what was
 *         provided.  Otherwise, add the quirk to the dynamic quirks list.
 *
 * Returns: 0 OK, -error on failure.
 */
int usbhid_modify_dquirk(const u16 idVendor, const u16 idProduct,
		const u32 quirks)
{
	struct quirks_list_struct *q_new, *q;
	int list_edited = 0;

	if (!idVendor) {
		dbg_hid("Cannot add a quirk with idVendor = 0\n");
		return -EINVAL;
	}

	q_new = kmalloc(sizeof(struct quirks_list_struct), GFP_KERNEL);
	if (!q_new) {
		dbg_hid("Could not allocate quirks_list_struct\n");
		return -ENOMEM;
	}

	q_new->hid_bl_item.idVendor = idVendor;
	q_new->hid_bl_item.idProduct = idProduct;
	q_new->hid_bl_item.quirks = quirks;

	down_write(&dquirks_rwsem);

	list_for_each_entry(q, &dquirks_list, node) {

		if (q->hid_bl_item.idVendor == idVendor &&
				q->hid_bl_item.idProduct == idProduct) {

			list_replace(&q->node, &q_new->node);
			kfree(q);
			list_edited = 1;
			break;

		}

	}

	if (!list_edited)
		list_add_tail(&q_new->node, &dquirks_list);

	up_write(&dquirks_rwsem);

	return 0;
}

/**
 * usbhid_remove_all_dquirks: remove all runtime HID quirks from memory
 *
 * Description:
 *         Free all memory associated with dynamic quirks - called before
 *         module unload.
 *
 */
static void usbhid_remove_all_dquirks(void)
{
	struct quirks_list_struct *q, *temp;

	down_write(&dquirks_rwsem);
	list_for_each_entry_safe(q, temp, &dquirks_list, node) {
		list_del(&q->node);
		kfree(q);
	}
	up_write(&dquirks_rwsem);

}

/** 
 * usbhid_quirks_init: apply USB HID quirks specified at module load time
 */
int usbhid_quirks_init(char **quirks_param)
{
	u16 idVendor, idProduct;
	u32 quirks;
	int n = 0, m;

	for (; quirks_param[n] && n < MAX_USBHID_BOOT_QUIRKS; n++) {

		m = sscanf(quirks_param[n], "0x%hx:0x%hx:0x%x",
				&idVendor, &idProduct, &quirks);

		if (m != 3 ||
				usbhid_modify_dquirk(idVendor, idProduct, quirks) != 0) {
			printk(KERN_WARNING
					"Could not parse HID quirk module param %s\n",
					quirks_param[n]);
		}
	}

	return 0;
}

/**
 * usbhid_quirks_exit: release memory associated with dynamic_quirks
 *
 * Description:
 *     Release all memory associated with dynamic quirks.  Called upon
 *     module unload.
 *
 * Returns: nothing
 */
void usbhid_quirks_exit(void)
{
	usbhid_remove_all_dquirks();
}

/**
 * usbhid_exists_squirk: return any static quirks for a USB HID device
 * @idVendor: the 16-bit USB vendor ID, in native byteorder
 * @idProduct: the 16-bit USB product ID, in native byteorder
 *
 * Description:
 *     Given a USB vendor ID and product ID, return a pointer to
 *     the hid_blacklist entry associated with that device.
 *
 * Returns: pointer if quirk found, or NULL if no quirks found.
 */
static const struct hid_blacklist *usbhid_exists_squirk(const u16 idVendor,
		const u16 idProduct)
{
	const struct hid_blacklist *bl_entry = NULL;
	int n = 0;

	for (; hid_blacklist[n].idVendor; n++)
		if (hid_blacklist[n].idVendor == idVendor &&
				hid_blacklist[n].idProduct == idProduct)
			bl_entry = &hid_blacklist[n];

	if (bl_entry != NULL)
		dbg_hid("Found squirk 0x%x for USB HID vendor 0x%hx prod 0x%hx\n",
				bl_entry->quirks, bl_entry->idVendor, 
				bl_entry->idProduct);
	return bl_entry;
}

/**
 * usbhid_lookup_quirk: return any quirks associated with a USB HID device
 * @idVendor: the 16-bit USB vendor ID, in native byteorder
 * @idProduct: the 16-bit USB product ID, in native byteorder
 *
 * Description:
 *     Given a USB vendor ID and product ID, return any quirks associated
 *     with that device.
 *
 * Returns: a u32 quirks value.
 */
u32 usbhid_lookup_quirk(const u16 idVendor, const u16 idProduct)
{
	u32 quirks = 0;
	const struct hid_blacklist *bl_entry = NULL;

	/* Ignore all Wacom devices */
	if (idVendor == USB_VENDOR_ID_WACOM)
		return HID_QUIRK_IGNORE;

	/* ignore all Code Mercenaries IOWarrior devices */
	if (idVendor == USB_VENDOR_ID_CODEMERCS)
		if (idProduct >= USB_DEVICE_ID_CODEMERCS_IOW_FIRST &&
				idProduct <= USB_DEVICE_ID_CODEMERCS_IOW_LAST)
			return HID_QUIRK_IGNORE;

	/* NCR devices must not be queried for reports */
	if (idVendor == USB_VENDOR_ID_NCR &&
			idProduct >= USB_DEVICE_ID_NCR_FIRST &&
			idProduct <= USB_DEVICE_ID_NCR_LAST)
			return HID_QUIRK_NOGET;

	down_read(&dquirks_rwsem);
	bl_entry = usbhid_exists_dquirk(idVendor, idProduct);
	if (!bl_entry)
		bl_entry = usbhid_exists_squirk(idVendor, idProduct);
	if (bl_entry)
		quirks = bl_entry->quirks;
	up_read(&dquirks_rwsem);

	return quirks;
}

/*
 * Cherry Cymotion keyboard have an invalid HID report descriptor,
 * that needs fixing before we can parse it.
 */
static void usbhid_fixup_cymotion_descriptor(char *rdesc, int rsize)
{
	if (rsize >= 17 && rdesc[11] == 0x3c && rdesc[12] == 0x02) {
		printk(KERN_INFO "Fixing up Cherry Cymotion report descriptor\n");
		rdesc[11] = rdesc[16] = 0xff;
		rdesc[12] = rdesc[17] = 0x03;
	}
}


/*
 * Certain Logitech keyboards send in report #3 keys which are far
 * above the logical maximum described in descriptor. This extends
 * the original value of 0x28c of logical maximum to 0x104d
 */
static void usbhid_fixup_logitech_descriptor(unsigned char *rdesc, int rsize)
{
	if (rsize >= 90 && rdesc[83] == 0x26
			&& rdesc[84] == 0x8c
			&& rdesc[85] == 0x02) {
		printk(KERN_INFO "Fixing up Logitech keyboard report descriptor\n");
		rdesc[84] = rdesc[89] = 0x4d;
		rdesc[85] = rdesc[90] = 0x10;
	}
}

/* Petalynx Maxter Remote has maximum for consumer page set too low */
static void usbhid_fixup_petalynx_descriptor(unsigned char *rdesc, int rsize)
{
	if (rsize >= 60 && rdesc[39] == 0x2a
			&& rdesc[40] == 0xf5
			&& rdesc[41] == 0x00
			&& rdesc[59] == 0x26
			&& rdesc[60] == 0xf9
			&& rdesc[61] == 0x00) {
		printk(KERN_INFO "Fixing up Petalynx Maxter Remote report descriptor\n");
		rdesc[60] = 0xfa;
		rdesc[40] = 0xfa;
	}
}

/*
 * Some USB barcode readers from cypress have usage min and usage max in
 * the wrong order
 */
static void usbhid_fixup_cypress_descriptor(unsigned char *rdesc, int rsize)
{
	short fixed = 0;
	int i;

	for (i = 0; i < rsize - 4; i++) {
		if (rdesc[i] == 0x29 && rdesc [i+2] == 0x19) {
			unsigned char tmp;

			rdesc[i] = 0x19; rdesc[i+2] = 0x29;
			tmp = rdesc[i+3];
			rdesc[i+3] = rdesc[i+1];
			rdesc[i+1] = tmp;
		}
	}

	if (fixed)
		printk(KERN_INFO "Fixing up Cypress report descriptor\n");
}


static void __usbhid_fixup_report_descriptor(__u32 quirks, char *rdesc, unsigned rsize)
{
	if ((quirks & HID_QUIRK_RDESC_CYMOTION))
		usbhid_fixup_cymotion_descriptor(rdesc, rsize);

	if (quirks & HID_QUIRK_RDESC_LOGITECH)
		usbhid_fixup_logitech_descriptor(rdesc, rsize);

	if (quirks & HID_QUIRK_RDESC_SWAPPED_MIN_MAX)
		usbhid_fixup_cypress_descriptor(rdesc, rsize);

	if (quirks & HID_QUIRK_RDESC_PETALYNX)
		usbhid_fixup_petalynx_descriptor(rdesc, rsize);
}

/**
 * usbhid_fixup_report_descriptor: check if report descriptor needs fixup
 *
 * Description:
 *	Walks the hid_rdesc_blacklist[] array and checks whether the device
 *	is known to have broken report descriptor that needs to be fixed up
 *	prior to entering the HID parser
 *
 * Returns: nothing
 */
void usbhid_fixup_report_descriptor(const u16 idVendor, const u16 idProduct,
				    char *rdesc, unsigned rsize, char **quirks_param)
{
	int n, m;
	u16 paramVendor, paramProduct;
	u32 quirks;

	/* static rdesc quirk entries */
	for (n = 0; hid_rdesc_blacklist[n].idVendor; n++)
		if (hid_rdesc_blacklist[n].idVendor == idVendor &&
				hid_rdesc_blacklist[n].idProduct == idProduct)
			__usbhid_fixup_report_descriptor(hid_rdesc_blacklist[n].quirks,
					rdesc, rsize);

	/* runtime rdesc quirk entries handling */
	for (n = 0; quirks_param[n] && n < MAX_USBHID_BOOT_QUIRKS; n++) {
		m = sscanf(quirks_param[n], "0x%hx:0x%hx:0x%x",
				&paramVendor, &paramProduct, &quirks);

		if (m != 3)
			printk(KERN_WARNING
				"Could not parse HID quirk module param %s\n",
				quirks_param[n]);
		else if (paramVendor == idVendor && paramProduct == idProduct)
			__usbhid_fixup_report_descriptor(quirks, rdesc, rsize);
	}

}
