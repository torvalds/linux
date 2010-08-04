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
 * File: channel.h
 *
 * Purpose: Country Regulation Rules header file
 *
 * Author: Lucas Lin
 *
 * Date: Dec 23, 2004
 *
 */

#ifndef _CHANNEL_H_
#define _CHANNEL_H_

#include "ttype.h"

/*---------------------  Export Definitions -------------------------*/

/*---------------------  Export Classes  ----------------------------*/
typedef struct tagSChannelTblElement {
    BYTE    byChannelNumber;
    unsigned int    uFrequency;
    BOOL    bValid;
}SChannelTblElement, *PSChannelTblElement;

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/
BOOL    ChannelValid(unsigned int CountryCode, unsigned int ChannelNum);
void    CHvInitChannelTable(void *pDeviceHandler);
BYTE    CHbyGetChannelMapping(BYTE byChannelNumber);

BOOL
CHvChannelGetList (
      unsigned int       uCountryCodeIdx,
     PBYTE      pbyChannelTable
    );

#endif  /* _REGULATE_H_ */
