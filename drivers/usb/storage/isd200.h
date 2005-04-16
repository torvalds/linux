/* Header File for In-System Design, Inc. ISD200 ASIC
 *
 * First release
 *
 * Current development and maintenance by:
 *   (c) 2000 In-System Design, Inc. (support@in-system.com)
 *
 * See isd200.c for more information.
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

#ifndef _USB_ISD200_H
#define _USB_ISD200_H

extern void isd200_ata_command(struct scsi_cmnd *srb, struct us_data *us);
extern int isd200_Initialization(struct us_data *us);

#endif
