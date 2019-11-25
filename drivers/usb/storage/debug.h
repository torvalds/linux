// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for USB Mass Storage compliant devices
 * Debugging Functions Header File
 *
 * Current development and maintenance by:
 *   (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Initial work by:
 *   (c) 1999 Michael Gee (michael@linuxspecific.com)
 *
 * This driver is based on the 'USB Mass Storage Class' document. This
 * describes in detail the protocol used to communicate with such
 * devices.  Clearly, the designers had SCSI and ATAPI commands in
 * mind when they created this document.  The commands are all very
 * similar to commands in the SCSI-II and ATAPI specifications.
 *
 * It is important to note that in a number of cases this class
 * exhibits class-specific exemptions from the USB specification.
 * Notably the usage of NAK, STALL and ACK differs from the norm, in
 * that they are used to communicate wait, failed and OK on commands.
 *
 * Also, for certain devices, the interrupt endpoint is used to convey
 * status of a command.
 */

#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <linux/kernel.h>

#ifdef CONFIG_USB_STORAGE_DEBUG
void usb_stor_show_command(const struct us_data *us, struct scsi_cmnd *srb);
void usb_stor_show_sense(const struct us_data *us, unsigned char key,
			 unsigned char asc, unsigned char ascq);
__printf(2, 3) void usb_stor_dbg(const struct us_data *us,
				 const char *fmt, ...);

#define US_DEBUG(x)		x
#else
__printf(2, 3)
static inline void _usb_stor_dbg(const struct us_data *us,
				 const char *fmt, ...)
{
}
#define usb_stor_dbg(us, fmt, ...)				\
	do { if (0) _usb_stor_dbg(us, fmt, ##__VA_ARGS__); } while (0)
#define US_DEBUG(x)
#endif

#endif
