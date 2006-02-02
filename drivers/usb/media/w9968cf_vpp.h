/***************************************************************************
 * Interface for video post-processing functions for the W996[87]CF driver *
 * for Linux.                                                              *
 *                                                                         *
 * Copyright (C) 2002-2004 by Luca Risolia <luca.risolia@studio.unibo.it>  *
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

#ifndef _W9968CF_VPP_H_
#define _W9968CF_VPP_H_

#include <linux/module.h>
#include <asm/types.h>

struct w9968cf_vpp_t {
	struct module* owner;
	int (*check_headers)(const unsigned char*, const unsigned long);
	int (*decode)(const char*, const unsigned long, const unsigned,
	              const unsigned, char*);
	void (*swap_yuvbytes)(void*, unsigned long);
	void (*uyvy_to_rgbx)(u8*, unsigned long, u8*, u16, u8);
	void (*scale_up)(u8*, u8*, u16, u16, u16, u16, u16);

	u8 busy; /* read-only flag: module is/is not in use */
};

#endif /* _W9968CF_VPP_H_ */
