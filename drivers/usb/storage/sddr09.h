/* Driver for SanDisk SDDR-09 SmartMedia reader
 * Header File
 *
 * $Id: sddr09.h,v 1.5 2000/08/25 00:13:51 mdharm Exp $
 *
 * Current development and maintenance by:
 *   (c) 2000 Robert Baruch (autophile@dol.net)
 *   (c) 2002 Andries Brouwer (aeb@cwi.nl)
 *
 * See sddr09.c for more explanation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _USB_SHUTTLE_EUSB_SDDR09_H
#define _USB_SHUTTLE_EUSB_SDDR09_H

/* Sandisk SDDR-09 stuff */

extern int sddr09_transport(struct scsi_cmnd *srb, struct us_data *us);

extern int usb_stor_sddr09_dpcm_init(struct us_data *us);
extern int usb_stor_sddr09_init(struct us_data *us);

#endif
