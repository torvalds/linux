/*
  usb-midi.h  --  USB-MIDI driver

  Copyright (C) 2001
      NAGANO Daisuke <breeze.nagano@nifty.ne.jp>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* ------------------------------------------------------------------------- */

#ifndef _USB_MIDI_H_
#define _USB_MIDI_H_

#ifndef USB_SUBCLASS_MIDISTREAMING
#define USB_SUBCLASS_MIDISTREAMING	3
#endif

/* ------------------------------------------------------------------------- */
/* Roland MIDI Devices */

#define USB_VENDOR_ID_ROLAND		0x0582
#define USBMIDI_ROLAND_UA100G		0x0000
#define USBMIDI_ROLAND_MPU64		0x0002
#define USBMIDI_ROLAND_SC8850		0x0003
#define USBMIDI_ROLAND_SC8820		0x0007
#define USBMIDI_ROLAND_UM2		0x0005
#define USBMIDI_ROLAND_UM1		0x0009
#define USBMIDI_ROLAND_PC300		0x0008

/* YAMAHA MIDI Devices */
#define USB_VENDOR_ID_YAMAHA		0x0499
#define USBMIDI_YAMAHA_MU1000		0x1001

/* Steinberg MIDI Devices */
#define USB_VENDOR_ID_STEINBERG		0x0763
#define USBMIDI_STEINBERG_USB2MIDI	0x1001

/* Mark of the Unicorn MIDI Devices */
#define USB_VENDOR_ID_MOTU		0x07fd
#define USBMIDI_MOTU_FASTLANE		0x0001

/* ------------------------------------------------------------------------- */
/* Supported devices */

struct usb_midi_endpoint {
	int  endpoint;
	int  cableId; /* if bit-n == 1 then cableId-n is enabled (n: 0 - 15) */
};

struct usb_midi_device {
	char  *deviceName;

	u16    idVendor;
	u16    idProduct;
	int    interface;
	int    altSetting; /* -1: auto detect */

	struct usb_midi_endpoint in[15];
	struct usb_midi_endpoint out[15];
};

static struct usb_midi_device usb_midi_devices[] = {
  { /* Roland UM-1 */
    "Roland UM-1",
    USB_VENDOR_ID_ROLAND, USBMIDI_ROLAND_UM1, 2, -1,
    { { 0x81, 1 }, {-1, -1} },
    { { 0x01, 1,}, {-1, -1} },
  },

  { /* Roland UM-2 */
    "Roland UM-2" ,
    USB_VENDOR_ID_ROLAND, USBMIDI_ROLAND_UM2, 2, -1,
    { { 0x81, 3 }, {-1, -1} },
    { { 0x01, 3,}, {-1, -1} },
  },

/** Next entry courtesy research by Michael Minn <michael@michaelminn.com> **/
  { /* Roland UA-100 */
    "Roland UA-100",
    USB_VENDOR_ID_ROLAND, USBMIDI_ROLAND_UA100G, 2, -1,
    { { 0x82, 7 }, {-1, -1} }, /** cables 0,1 and 2 for SYSEX **/
    { { 0x02, 7 }, {-1, -1} },
  },

/** Next entry courtesy research by Michael Minn <michael@michaelminn.com> **/
  { /* Roland SC8850 */
    "Roland SC8850",
    USB_VENDOR_ID_ROLAND, USBMIDI_ROLAND_SC8850, 2, -1,
    { { 0x81, 0x3f }, {-1, -1} },
    { { 0x01, 0x3f }, {-1, -1} },
  },

  { /* Roland SC8820 */
    "Roland SC8820",
    USB_VENDOR_ID_ROLAND, USBMIDI_ROLAND_SC8820, 2, -1,
    { { 0x81, 0x13 }, {-1, -1} },
    { { 0x01, 0x13 }, {-1, -1} },
  },

  { /* Roland SC8820 */
    "Roland SC8820",
    USB_VENDOR_ID_ROLAND, USBMIDI_ROLAND_SC8820, 2, -1,
    { { 0x81, 17 }, {-1, -1} },
    { { 0x01, 17 }, {-1, -1} },
  },

  { /* YAMAHA MU1000 */
    "YAMAHA MU1000",
    USB_VENDOR_ID_YAMAHA, USBMIDI_YAMAHA_MU1000, 0, -1, 
    { { 0x81, 1 }, {-1, -1} },
    { { 0x01, 15 }, {-1, -1} },
  },
  { /* Roland PC-300 */
    "Roland PC-300",
    USB_VENDOR_ID_ROLAND, USBMIDI_ROLAND_PC300, 2, -1, 
    { { 0x81, 1 }, {-1, -1} },
    { { 0x01, 1 }, {-1, -1} },
  },
  { /* MOTU Fastlane USB */
    "MOTU Fastlane USB",
    USB_VENDOR_ID_MOTU, USBMIDI_MOTU_FASTLANE, 1, 0,
    { { 0x82, 3 }, {-1, -1} },
    { { 0x02, 3 }, {-1, -1} },
  }
};

#define VENDOR_SPECIFIC_USB_MIDI_DEVICES (sizeof(usb_midi_devices)/sizeof(struct usb_midi_device))

/* for Hot-Plugging */

static struct usb_device_id usb_midi_ids [] = {
	{ .match_flags = (USB_DEVICE_ID_MATCH_INT_CLASS | USB_DEVICE_ID_MATCH_INT_SUBCLASS),
	  .bInterfaceClass = USB_CLASS_AUDIO, .bInterfaceSubClass = USB_SUBCLASS_MIDISTREAMING},
	{ USB_DEVICE( USB_VENDOR_ID_ROLAND, USBMIDI_ROLAND_UM1    ) },
	{ USB_DEVICE( USB_VENDOR_ID_ROLAND, USBMIDI_ROLAND_UM2    ) },
	{ USB_DEVICE( USB_VENDOR_ID_ROLAND, USBMIDI_ROLAND_UA100G ) },
	{ USB_DEVICE( USB_VENDOR_ID_ROLAND, USBMIDI_ROLAND_PC300 ) },
	{ USB_DEVICE( USB_VENDOR_ID_ROLAND, USBMIDI_ROLAND_SC8850 ) },
	{ USB_DEVICE( USB_VENDOR_ID_ROLAND, USBMIDI_ROLAND_SC8820 ) },
	{ USB_DEVICE( USB_VENDOR_ID_YAMAHA, USBMIDI_YAMAHA_MU1000 ) },
	{ USB_DEVICE( USB_VENDOR_ID_MOTU,   USBMIDI_MOTU_FASTLANE ) },
/*	{ USB_DEVICE( USB_VENDOR_ID_STEINBERG, USBMIDI_STEINBERG_USB2MIDI ) },*/
	{ } /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usb_midi_ids);

/* ------------------------------------------------------------------------- */
#endif /* _USB_MIDI_H_ */


