// SPDX-License-Identifier: GPL-2.0
/*
 * USB Serial "Simple" driver
 *
 * Copyright (C) 2001-2006,2008,2013 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2005 Arthur Huillet (ahuillet@users.sf.net)
 * Copyright (C) 2005 Thomas Hergenhahn <thomas.hergenhahn@suse.de>
 * Copyright (C) 2009 Outpost Embedded, LLC
 * Copyright (C) 2010 Zilogic Systems <code@zilogic.com>
 * Copyright (C) 2013 Wei Shuai <cpuwolf@gmail.com>
 * Copyright (C) 2013 Linux Foundation
 */

#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define DEVICE_N(vendor, IDS, nport)				\
static const struct usb_device_id vendor##_id_table[] = {	\
	IDS(),							\
	{ },							\
};								\
static struct usb_serial_driver vendor##_device = {		\
	.driver = {						\
		.owner =	THIS_MODULE,			\
		.name =		#vendor,			\
	},							\
	.id_table =		vendor##_id_table,		\
	.num_ports =		nport,				\
};

#define DEVICE(vendor, IDS)	DEVICE_N(vendor, IDS, 1)

/* Medtronic CareLink USB driver */
#define CARELINK_IDS()			\
	{ USB_DEVICE(0x0a21, 0x8001) }	/* MMT-7305WW */
DEVICE(carelink, CARELINK_IDS);

/* ZIO Motherboard USB driver */
#define ZIO_IDS()			\
	{ USB_DEVICE(0x1CBE, 0x0103) }
DEVICE(zio, ZIO_IDS);

/* Funsoft Serial USB driver */
#define FUNSOFT_IDS()			\
	{ USB_DEVICE(0x1404, 0xcddc) }
DEVICE(funsoft, FUNSOFT_IDS);

/* Infineon Flashloader driver */
#define FLASHLOADER_IDS()		\
	{ USB_DEVICE_INTERFACE_CLASS(0x058b, 0x0041, USB_CLASS_CDC_DATA) }, \
	{ USB_DEVICE(0x8087, 0x0716) }, \
	{ USB_DEVICE(0x8087, 0x0801) }
DEVICE(flashloader, FLASHLOADER_IDS);

/* Google Serial USB SubClass */
#define GOOGLE_IDS()						\
	{ USB_VENDOR_AND_INTERFACE_INFO(0x18d1,			\
					USB_CLASS_VENDOR_SPEC,	\
					0x50,			\
					0x01) }
DEVICE(google, GOOGLE_IDS);

/* Libtransistor USB console */
#define LIBTRANSISTOR_IDS()			\
	{ USB_DEVICE(0x1209, 0x8b00) }
DEVICE(libtransistor, LIBTRANSISTOR_IDS);

/* ViVOpay USB Serial Driver */
#define VIVOPAY_IDS()			\
	{ USB_DEVICE(0x1d5f, 0x1004) }	/* ViVOpay 8800 */
DEVICE(vivopay, VIVOPAY_IDS);

/* Motorola USB Phone driver */
#define MOTO_IDS()			\
	{ USB_DEVICE(0x05c6, 0x3197) },	/* unknown Motorola phone */	\
	{ USB_DEVICE(0x0c44, 0x0022) },	/* unknown Motorola phone */	\
	{ USB_DEVICE(0x22b8, 0x2a64) },	/* Motorola KRZR K1m */		\
	{ USB_DEVICE(0x22b8, 0x2c84) },	/* Motorola VE240 phone */	\
	{ USB_DEVICE(0x22b8, 0x2c64) }	/* Motorola V950 phone */
DEVICE(moto_modem, MOTO_IDS);

/* Motorola Tetra driver */
#define MOTOROLA_TETRA_IDS()			\
	{ USB_DEVICE(0x0cad, 0x9011) },	/* Motorola Solutions TETRA PEI */ \
	{ USB_DEVICE(0x0cad, 0x9012) },	/* MTP6550 */ \
	{ USB_DEVICE(0x0cad, 0x9013) },	/* MTP3xxx */ \
	{ USB_DEVICE(0x0cad, 0x9015) },	/* MTP85xx */ \
	{ USB_DEVICE(0x0cad, 0x9016) }	/* TPG2200 */
DEVICE(motorola_tetra, MOTOROLA_TETRA_IDS);

/* Novatel Wireless GPS driver */
#define NOVATEL_IDS()			\
	{ USB_DEVICE(0x09d7, 0x0100) }	/* NovAtel FlexPack GPS */
DEVICE_N(novatel_gps, NOVATEL_IDS, 3);

/* HP4x (48/49) Generic Serial driver */
#define HP4X_IDS()			\
	{ USB_DEVICE(0x03f0, 0x0121) }
DEVICE(hp4x, HP4X_IDS);

/* Suunto ANT+ USB Driver */
#define SUUNTO_IDS()			\
	{ USB_DEVICE(0x0fcf, 0x1008) },	\
	{ USB_DEVICE(0x0fcf, 0x1009) } /* Dynastream ANT USB-m Stick */
DEVICE(suunto, SUUNTO_IDS);

/* Siemens USB/MPI adapter */
#define SIEMENS_IDS()			\
	{ USB_DEVICE(0x908, 0x0004) }
DEVICE(siemens_mpi, SIEMENS_IDS);

/* All of the above structures mushed into two lists */
static struct usb_serial_driver * const serial_drivers[] = {
	&carelink_device,
	&zio_device,
	&funsoft_device,
	&flashloader_device,
	&google_device,
	&libtransistor_device,
	&vivopay_device,
	&moto_modem_device,
	&motorola_tetra_device,
	&novatel_gps_device,
	&hp4x_device,
	&suunto_device,
	&siemens_mpi_device,
	NULL
};

static const struct usb_device_id id_table[] = {
	CARELINK_IDS(),
	ZIO_IDS(),
	FUNSOFT_IDS(),
	FLASHLOADER_IDS(),
	GOOGLE_IDS(),
	LIBTRANSISTOR_IDS(),
	VIVOPAY_IDS(),
	MOTO_IDS(),
	MOTOROLA_TETRA_IDS(),
	NOVATEL_IDS(),
	HP4X_IDS(),
	SUUNTO_IDS(),
	SIEMENS_IDS(),
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

module_usb_serial_driver(serial_drivers, id_table);
MODULE_LICENSE("GPL v2");
