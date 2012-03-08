/*
  Date Created:	1/12/2007
  File Name:		metro-usb.h
  Description:	metro-usb.h is the drivers header file. The driver is a USB to Serial converter.
		The driver takes USB data and sends it to a virtual ttyUSB# serial port.
		The driver interfaces with the usbserial.ko driver supplied by Linux.

		NOTES:
		To install the driver:
		1. Install the usbserial.ko module supplied by Linux with: # insmod usbserial.ko
		2. Install the metro-usb.ko module with: # insmod metro-usb.ko vender=0x#### product=0x#### debug=1
		   The vendor, product and debug parameters are optional.

  Copyright:	2007 Metrologic Instruments. All rights reserved.
  Copyright:	2011 Azimut Ltd. <http://azimutrzn.ru/>
  Requirements: Notepad.exe

  Revision History:

  Date:			Developer:			Revisions:
  ------------------------------------------------------------------------------
  1/12/2007		Philip Nicastro		Initial release. (v1.0.0.0)
  10/07/2011		Aleksey Babahin		Update for new kernel (tested on 2.6.38)
						Add unidirection mode support


*/

#ifndef __LINUX_USB_SERIAL_METRO
#define __LINUX_USB_SERIAL_METRO

/* Product information. */
#define FOCUS_VENDOR_ID		0x0C2E
#define FOCUS_PRODUCT_ID	0x0720
#define FOCUS_PRODUCT_ID_UNI	0x0710

#define METROUSB_SET_REQUEST_TYPE	0x40
#define METROUSB_SET_MODEM_CTRL_REQUEST	10
#define METROUSB_SET_BREAK_REQUEST	0x40
#define METROUSB_MCR_NONE               0x8     /* Deactivate DTR and RTS. */
#define METROUSB_MCR_RTS                0xa     /* Activate RTS. */
#define METROUSB_MCR_DTR                0x9     /* Activate DTR. */
#define WDR_TIMEOUT			5000 	/* default urb timeout. */

/* Private data structure. */
struct metrousb_private {
	spinlock_t lock;
	int throttled;
	unsigned long control_state;
};

#endif
