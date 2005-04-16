/* Driver for Lexar "Jumpshot" USB Compact Flash reader
 * Header File
 *
 * Current development and maintenance by:
 *   (c) 2000 Jimmie Mayfield (mayfield+usb@sackheads.org)
 *
 * See jumpshot.c for more explanation
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

#ifndef _USB_JUMPSHOT_H
#define _USB_JUMPSHOT_H

extern int jumpshot_transport(struct scsi_cmnd *srb, struct us_data *us);

struct jumpshot_info {
   unsigned long   sectors;     // total sector count
   unsigned long   ssize;       // sector size in bytes
 
   // the following aren't used yet
   unsigned char   sense_key;
   unsigned long   sense_asc;   // additional sense code
   unsigned long   sense_ascq;  // additional sense code qualifier
};

#endif
