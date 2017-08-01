/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef _RTL871X_BYTEORDER_H_
#define _RTL871X_BYTEORDER_H_


#if defined(CONFIG_LITTLE_ENDIAN) && defined (CONFIG_BIG_ENDIAN)
	#error "Shall be CONFIG_LITTLE_ENDIAN or CONFIG_BIG_ENDIAN, but not both!\n"
#endif

#if defined(CONFIG_LITTLE_ENDIAN)
	#ifndef CONFIG_PLATFORM_MSTAR389
		#include <byteorder/little_endian.h>
	#endif
#elif defined (CONFIG_BIG_ENDIAN)
	#include <byteorder/big_endian.h>
#else
	#  error "Must be LITTLE/BIG Endian Host"
#endif

#endif /* _RTL871X_BYTEORDER_H_ */
