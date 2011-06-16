/* Unusual Devices File for devices based on the Cypress USB/ATA bridge
 *	with support for ATACB
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

#if defined(CONFIG_USB_STORAGE_CYPRESS_ATACB) || \
		defined(CONFIG_USB_STORAGE_CYPRESS_ATACB_MODULE)

/* CY7C68300 : support atacb */
UNUSUAL_DEV(  0x04b4, 0x6830, 0x0000, 0x9999,
		"Cypress",
		"Cypress AT2LP",
		US_SC_CYP_ATACB, US_PR_DEVICE, NULL, 0),

/* CY7C68310 : support atacb and atacb2 */
UNUSUAL_DEV(  0x04b4, 0x6831, 0x0000, 0x9999,
		"Cypress",
		"Cypress ISD-300LP",
		US_SC_CYP_ATACB, US_PR_DEVICE, NULL, 0),

UNUSUAL_DEV( 0x14cd, 0x6116, 0x0000, 0x9999,
		"Super Top",
		"USB 2.0  SATA BRIDGE",
		US_SC_CYP_ATACB, US_PR_DEVICE, NULL, 0),

#endif /* defined(CONFIG_USB_STORAGE_CYPRESS_ATACB) || ... */
