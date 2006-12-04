/* 
 * USBVISION_IOCTL.H
 *  IOCTL for usbvision
 *
 * Copyright (c) 1999-2005 Joerg Heckenbach <joerg@heckenbach-aw.de>
 *
 * This module is part of usbvision driver project.
 * Updates to driver completed by Dwaine P. Garden
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


struct usbvision_reg {
	unsigned char addr;
	unsigned char value;
};

#define UVIOCGREG		_IOWR('u',240,struct usbvision_reg)		// get register
#define UVIOCSREG		_IOW('u',241,struct usbvision_reg)		// set register
#define UVIOCSVINM		_IOW('u',242,int)						// set usbvision vin_mode

