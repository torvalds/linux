/*
 * File: rc4.h
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Purpose:
 *
 * Functions:
 *
 * Revision History:
 *
 * Author: Kyle Hsu
 *
 * Date: Sep 4, 2002
 *
 */

#ifndef __RC4_H__
#define __RC4_H__

#include "ttype.h"

/*---------------------  Export Definitions -------------------------*/
/*---------------------  Export Types  ------------------------------*/
typedef struct {
    unsigned int ux;
    unsigned int uy;
    u8 abystate[256];
} RC4Ext, *PRC4Ext;

void rc4_init(PRC4Ext pRC4, u8 * pbyKey, unsigned int cbKey_len);
unsigned int rc4_byte(PRC4Ext pRC4);
void rc4_encrypt(PRC4Ext pRC4, u8 * pbyDest, u8 * pbySrc,
		 unsigned int cbData_len);

#endif /* __RC4_H__ */
