/*
 * Line6 Linux USB driver - 0.9.1beta
 *
 * Copyright (C) 2005-2008 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#ifndef USBDEFS_H
#define USBDEFS_H

#define LINE6_VENDOR_ID  0x0e41

#define USB_INTERVALS_PER_SECOND 1000

/*
	Device ids.
*/
#define LINE6_DEVID_BASSPODXT     0x4250
#define LINE6_DEVID_BASSPODXTLIVE 0x4642
#define LINE6_DEVID_BASSPODXTPRO  0x4252
#define LINE6_DEVID_GUITARPORT    0x4750
#define LINE6_DEVID_POCKETPOD     0x5051
#define LINE6_DEVID_PODSTUDIO_GX  0x4153
#define LINE6_DEVID_PODSTUDIO_UX1 0x4150
#define LINE6_DEVID_PODSTUDIO_UX2 0x4151
#define LINE6_DEVID_PODX3         0x414a
#define LINE6_DEVID_PODX3LIVE     0x414b
#define LINE6_DEVID_PODXT         0x5044
#define LINE6_DEVID_PODXTLIVE     0x4650
#define LINE6_DEVID_PODXTPRO      0x5050
#define LINE6_DEVID_TONEPORT_GX   0x4147
#define LINE6_DEVID_TONEPORT_UX1  0x4141
#define LINE6_DEVID_TONEPORT_UX2  0x4142
#define LINE6_DEVID_VARIAX        0x534d

#define LINE6_BIT_BASSPODXT       (1 <<  0)
#define LINE6_BIT_BASSPODXTLIVE   (1 <<  1)
#define LINE6_BIT_BASSPODXTPRO    (1 <<  2)
#define LINE6_BIT_GUITARPORT      (1 <<  3)
#define LINE6_BIT_POCKETPOD       (1 <<  4)
#define LINE6_BIT_PODSTUDIO_GX    (1 <<  5)
#define LINE6_BIT_PODSTUDIO_UX1   (1 <<  6)
#define LINE6_BIT_PODSTUDIO_UX2   (1 <<  7)
#define LINE6_BIT_PODX3           (1 <<  8)
#define LINE6_BIT_PODX3LIVE       (1 <<  9)
#define LINE6_BIT_PODXT           (1 << 10)
#define LINE6_BIT_PODXTLIVE       (1 << 11)
#define LINE6_BIT_PODXTPRO        (1 << 12)
#define LINE6_BIT_TONEPORT_GX     (1 << 13)
#define LINE6_BIT_TONEPORT_UX1    (1 << 14)
#define LINE6_BIT_TONEPORT_UX2    (1 << 15)
#define LINE6_BIT_VARIAX          (1 << 16)

#define LINE6_BITS_PRO		(LINE6_BIT_BASSPODXTPRO | \
				 LINE6_BIT_PODXTPRO)
#define LINE6_BITS_LIVE		(LINE6_BIT_BASSPODXTLIVE | \
				 LINE6_BIT_PODXTLIVE | \
				 LINE6_BIT_PODX3LIVE)
#define LINE6_BITS_PODXTALL	(LINE6_BIT_PODXT | \
				 LINE6_BIT_PODXTLIVE | \
				 LINE6_BIT_PODXTPRO)
#define LINE6_BITS_BASSPODXTALL	(LINE6_BIT_BASSPODXT | \
				 LINE6_BIT_BASSPODXTLIVE | \
				 LINE6_BIT_BASSPODXTPRO)

/* device supports settings parameter via USB */
#define LINE6_BIT_CONTROL	(1 << 0)
/* device supports PCM input/output via USB */
#define LINE6_BIT_PCM		(1 << 1)
/* device support hardware monitoring */
#define LINE6_BIT_HWMON		(1 << 2)

#define LINE6_BIT_CONTROL_PCM_HWMON	(LINE6_BIT_CONTROL |	\
					 LINE6_BIT_PCM |	\
					 LINE6_BIT_HWMON)

#define LINE6_FALLBACK_INTERVAL		10
#define LINE6_FALLBACK_MAXPACKETSIZE	16

#endif
