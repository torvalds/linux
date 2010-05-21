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
 * File: rc4.c
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

#include "rc4.h"

void rc4_init(PRC4Ext pRC4, PBYTE pbyKey, UINT cbKey_len)
{
    UINT  ust1, ust2;
    UINT  keyindex;
    UINT  stateindex;
    PBYTE pbyst;
    UINT  idx;

    pbyst = pRC4->abystate;
    pRC4->ux = 0;
    pRC4->uy = 0;
    for (idx = 0; idx < 256; idx++)
        pbyst[idx] = (BYTE)idx;
    keyindex = 0;
    stateindex = 0;
    for (idx = 0; idx < 256; idx++) {
        ust1 = pbyst[idx];
        stateindex = (stateindex + pbyKey[keyindex] + ust1) & 0xff;
        ust2 = pbyst[stateindex];
        pbyst[stateindex] = (BYTE)ust1;
        pbyst[idx] = (BYTE)ust2;
        if (++keyindex >= cbKey_len)
            keyindex = 0;
    }
}

UINT rc4_byte(PRC4Ext pRC4)
{
    UINT ux;
    UINT uy;
    UINT ustx, usty;
    PBYTE pbyst;

    pbyst = pRC4->abystate;
    ux = (pRC4->ux + 1) & 0xff;
    ustx = pbyst[ux];
    uy = (ustx + pRC4->uy) & 0xff;
    usty = pbyst[uy];
    pRC4->ux = ux;
    pRC4->uy = uy;
    pbyst[uy] = (BYTE)ustx;
    pbyst[ux] = (BYTE)usty;

    return pbyst[(ustx + usty) & 0xff];
}

void rc4_encrypt(PRC4Ext pRC4, PBYTE pbyDest,
                     PBYTE pbySrc, UINT cbData_len)
{
    UINT ii;
    for (ii = 0; ii < cbData_len; ii++)
        pbyDest[ii] = (BYTE)(pbySrc[ii] ^ rc4_byte(pRC4));
}
