/*
 * Unusual Devices File for In-System Design, Inc. ISD200 ASIC
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

#if defined(CONFIG_USB_STORAGE_ISD200) || \
		defined(CONFIG_USB_STORAGE_ISD200_MODULE)

UNUSUAL_DEV(  0x054c, 0x002b, 0x0100, 0x0110,
		"Sony",
		"Portable USB Harddrive V2",
		USB_SC_ISD200, USB_PR_BULK, isd200_Initialization,
		0),

UNUSUAL_DEV(  0x05ab, 0x0031, 0x0100, 0x0110,
		"In-System",
		"USB/IDE Bridge (ATA/ATAPI)",
		USB_SC_ISD200, USB_PR_BULK, isd200_Initialization,
		0),

UNUSUAL_DEV(  0x05ab, 0x0301, 0x0100, 0x0110,
		"In-System",
		"Portable USB Harddrive V2",
		USB_SC_ISD200, USB_PR_BULK, isd200_Initialization,
		0),

UNUSUAL_DEV(  0x05ab, 0x0351, 0x0100, 0x0110,
		"In-System",
		"Portable USB Harddrive V2",
		USB_SC_ISD200, USB_PR_BULK, isd200_Initialization,
		0),

UNUSUAL_DEV(  0x05ab, 0x5701, 0x0100, 0x0110,
		"In-System",
		"USB Storage Adapter V2",
		USB_SC_ISD200, USB_PR_BULK, isd200_Initialization,
		0),

UNUSUAL_DEV(  0x0bf6, 0xa001, 0x0100, 0x0110,
		"ATI",
		"USB Cable 205",
		USB_SC_ISD200, USB_PR_BULK, isd200_Initialization,
		0),

#endif /* defined(CONFIG_USB_STORAGE_ISD200) || ... */
