/*
 * Copyright (C) 2005 Philips Semiconductors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA, or http://www.gnu.org/licenses/gpl.html
*/

#define QCIF_W  (176)
#define QCIF_H  (144)

#define CIF_W   (352)
#define CIF_H   (288)

#define LCD_X_RES	208
#define LCD_Y_RES	320
#define LCD_X_PAD	256
#define LCD_BBP		4	/* Bytes Per Pixel */

#define DISP_MAX_X_SIZE     (320)
#define DISP_MAX_Y_SIZE     (208)

#define RETURNVAL_BASE (0x400)

enum fb_ioctl_returntype {
	ENORESOURCESLEFT = RETURNVAL_BASE,
	ERESOURCESNOTFREED,
	EPROCNOTOWNER,
	EFBNOTOWNER,
	ECOPYFAILED,
	EIOREMAPFAILED,
};
