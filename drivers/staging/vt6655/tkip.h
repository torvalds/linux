/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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
 *
 * File: tkip.h
 *
 * Purpose: Implement functions for 802.11i TKIP
 *
 * Author: Jerry Chen
 *
 * Date: Mar. 11, 2003
 *
 */

#ifndef __TKIP_H__
#define __TKIP_H__

#include "ttype.h"
#include "tether.h"

/*---------------------  Export Definitions -------------------------*/
#define TKIP_KEY_LEN        16

/*---------------------  Export Types  ------------------------------*/

/*---------------------  Export Macros ------------------------------*/

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

void TKIPvMixKey(
    unsigned char *pbyTKey,
    unsigned char *pbyTA,
    unsigned short wTSC15_0,
    unsigned long dwTSC47_16,
    unsigned char *pbyRC4Key
    );

#endif // __TKIP_H__



