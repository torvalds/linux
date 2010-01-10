/***************************************************************************
 * Table of device identifiers of the SN9C1xx PC Camera Controllers        *
 *                                                                         *
 * Copyright (C) 2007 by Luca Risolia <luca.risolia@studio.unibo.it>       *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 2 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program; if not, write to the Free Software             *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.               *
 ***************************************************************************/

#ifndef _SN9C102_DEVTABLE_H_
#define _SN9C102_DEVTABLE_H_

#include <linux/usb.h>

struct sn9c102_device;

/*
   Each SN9C1xx camera has proper PID/VID identifiers.
   SN9C103, SN9C105, SN9C120 support multiple interfaces, but we only have to
   handle the video class interface.
*/
#define SN9C102_USB_DEVICE(vend, prod, bridge)                                \
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE |                           \
		       USB_DEVICE_ID_MATCH_INT_CLASS,                         \
	.idVendor = (vend),                                                   \
	.idProduct = (prod),                                                  \
	.bInterfaceClass = 0xff,                                              \
	.driver_info = (bridge)

static const struct usb_device_id sn9c102_id_table[] = {
	/* SN9C101 and SN9C102 */
#if !defined CONFIG_USB_GSPCA && !defined CONFIG_USB_GSPCA_MODULE
	{ SN9C102_USB_DEVICE(0x0c45, 0x6001, BRIDGE_SN9C102), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x6005, BRIDGE_SN9C102), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x6007, BRIDGE_SN9C102), },
#endif
	{ SN9C102_USB_DEVICE(0x0c45, 0x6009, BRIDGE_SN9C102), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x600d, BRIDGE_SN9C102), },
/*	{ SN9C102_USB_DEVICE(0x0c45, 0x6011, BRIDGE_SN9C102), }, OV6650 */
	{ SN9C102_USB_DEVICE(0x0c45, 0x6019, BRIDGE_SN9C102), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x6024, BRIDGE_SN9C102), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x6025, BRIDGE_SN9C102), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x6028, BRIDGE_SN9C102), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x6029, BRIDGE_SN9C102), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x602a, BRIDGE_SN9C102), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x602b, BRIDGE_SN9C102), },
#if !defined CONFIG_USB_GSPCA && !defined CONFIG_USB_GSPCA_MODULE
	{ SN9C102_USB_DEVICE(0x0c45, 0x602c, BRIDGE_SN9C102), },
#endif
/*	{ SN9C102_USB_DEVICE(0x0c45, 0x602d, BRIDGE_SN9C102), }, HV7131R */
	{ SN9C102_USB_DEVICE(0x0c45, 0x602e, BRIDGE_SN9C102), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x6030, BRIDGE_SN9C102), },
	/* SN9C103 */
	{ SN9C102_USB_DEVICE(0x0c45, 0x6080, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x6082, BRIDGE_SN9C103), },
/*	{ SN9C102_USB_DEVICE(0x0c45, 0x6083, BRIDGE_SN9C103), }, HY7131D/E */
	{ SN9C102_USB_DEVICE(0x0c45, 0x6088, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x608a, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x608b, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x608c, BRIDGE_SN9C103), },
/*	{ SN9C102_USB_DEVICE(0x0c45, 0x608e, BRIDGE_SN9C103), }, CISVF10 */
#if !defined CONFIG_USB_GSPCA && !defined CONFIG_USB_GSPCA_MODULE
	{ SN9C102_USB_DEVICE(0x0c45, 0x608f, BRIDGE_SN9C103), },
#endif
	{ SN9C102_USB_DEVICE(0x0c45, 0x60a0, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60a2, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60a3, BRIDGE_SN9C103), },
/*	{ SN9C102_USB_DEVICE(0x0c45, 0x60a8, BRIDGE_SN9C103), }, PAS106 */
/*	{ SN9C102_USB_DEVICE(0x0c45, 0x60aa, BRIDGE_SN9C103), }, TAS5130 */
/*	{ SN9C102_USB_DEVICE(0x0c45, 0x60ab, BRIDGE_SN9C103), }, TAS5130 */
	{ SN9C102_USB_DEVICE(0x0c45, 0x60ac, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60ae, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60af, BRIDGE_SN9C103), },
#if !defined CONFIG_USB_GSPCA && !defined CONFIG_USB_GSPCA_MODULE
	{ SN9C102_USB_DEVICE(0x0c45, 0x60b0, BRIDGE_SN9C103), },
#endif
	{ SN9C102_USB_DEVICE(0x0c45, 0x60b2, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60b3, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60b8, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60ba, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60bb, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60bc, BRIDGE_SN9C103), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60be, BRIDGE_SN9C103), },
	/* SN9C105 */
#if !defined CONFIG_USB_GSPCA && !defined CONFIG_USB_GSPCA_MODULE
	{ SN9C102_USB_DEVICE(0x045e, 0x00f5, BRIDGE_SN9C105), },
	{ SN9C102_USB_DEVICE(0x045e, 0x00f7, BRIDGE_SN9C105), },
	{ SN9C102_USB_DEVICE(0x0471, 0x0327, BRIDGE_SN9C105), },
	{ SN9C102_USB_DEVICE(0x0471, 0x0328, BRIDGE_SN9C105), },
#endif
	{ SN9C102_USB_DEVICE(0x0c45, 0x60c0, BRIDGE_SN9C105), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60c2, BRIDGE_SN9C105), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60c8, BRIDGE_SN9C105), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60cc, BRIDGE_SN9C105), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60ea, BRIDGE_SN9C105), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60ec, BRIDGE_SN9C105), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60ef, BRIDGE_SN9C105), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60fa, BRIDGE_SN9C105), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60fb, BRIDGE_SN9C105), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60fc, BRIDGE_SN9C105), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x60fe, BRIDGE_SN9C105), },
	/* SN9C120 */
	{ SN9C102_USB_DEVICE(0x0458, 0x7025, BRIDGE_SN9C120), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x6102, BRIDGE_SN9C120), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x6108, BRIDGE_SN9C120), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x610f, BRIDGE_SN9C120), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x6130, BRIDGE_SN9C120), },
/*	{ SN9C102_USB_DEVICE(0x0c45, 0x6138, BRIDGE_SN9C120), }, MO8000 */
#if !defined CONFIG_USB_GSPCA && !defined CONFIG_USB_GSPCA_MODULE
	{ SN9C102_USB_DEVICE(0x0c45, 0x613a, BRIDGE_SN9C120), },
#endif
	{ SN9C102_USB_DEVICE(0x0c45, 0x613b, BRIDGE_SN9C120), },
#if !defined CONFIG_USB_GSPCA && !defined CONFIG_USB_GSPCA_MODULE
	{ SN9C102_USB_DEVICE(0x0c45, 0x613c, BRIDGE_SN9C120), },
	{ SN9C102_USB_DEVICE(0x0c45, 0x613e, BRIDGE_SN9C120), },
#endif
	{ }
};

/*
   Probing functions: on success, you must attach the sensor to the camera
   by calling sn9c102_attach_sensor().
   To enable the I2C communication, you might need to perform a really basic
   initialization of the SN9C1XX chip.
   Functions must return 0 on success, the appropriate error otherwise.
*/
extern int sn9c102_probe_hv7131d(struct sn9c102_device* cam);
extern int sn9c102_probe_hv7131r(struct sn9c102_device* cam);
extern int sn9c102_probe_mi0343(struct sn9c102_device* cam);
extern int sn9c102_probe_mi0360(struct sn9c102_device* cam);
extern int sn9c102_probe_mt9v111(struct sn9c102_device *cam);
extern int sn9c102_probe_ov7630(struct sn9c102_device* cam);
extern int sn9c102_probe_ov7660(struct sn9c102_device* cam);
extern int sn9c102_probe_pas106b(struct sn9c102_device* cam);
extern int sn9c102_probe_pas202bcb(struct sn9c102_device* cam);
extern int sn9c102_probe_tas5110c1b(struct sn9c102_device* cam);
extern int sn9c102_probe_tas5110d(struct sn9c102_device* cam);
extern int sn9c102_probe_tas5130d1b(struct sn9c102_device* cam);

#endif /* _SN9C102_DEVTABLE_H_ */
