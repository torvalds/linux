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

void rc4_init(PRC4Ext pRC4, unsigned char *pbyKey, unsigned int cbKey_len)
{
    unsigned int ust1, ust2;
    unsigned int keyindex;
    unsigned int stateindex;
    unsigned char *pbyst;
    unsigned int idx;

    pbyst = pRC4->abystate;
    pRC4->ux = 0;
    pRC4->uy = 0;
    for (idx = 0; idx < 256; idx++)
        pbyst[idx] = (unsigned char)idx;
    keyindex = 0;
    stateindex = 0;
    for (idx = 0; idx < 256; idx++) {
        ust1 = pbyst[idx];
        stateindex = (stateindex + pbyKey[keyindex] + ust1) & 0xff;
        ust2 = pbyst[stateindex];
        pbyst[stateindex] = (unsigned char)ust1;
        pbyst[idx] = (unsigned char)ust2;
        if (++keyindex >= cbKey_len)
            keyindex = 0;
    }
}

unsigned int rc4_byte(PRC4Ext pRC4)
{
    unsigned int ux;
    unsigned int uy;
    unsigned int ustx, usty;
    unsigned char *pbyst;

    pbyst = pRC4->abystate;
    ux = (pRC4->ux + 1) & 0xff;
    ustx = pbyst[ux];
    uy = (ustx + pRC4->uy) & 0xff;
    usty = pbyst[uy];
    pRC4->ux = ux;
    pRC4->uy = uy;
    pbyst[uy] = (unsigned char)ustx;
    pbyst[ux] = (unsigned char)usty;

    return pbyst[(ustx + usty) & 0xff];
}

void rc4_encrypt(PRC4Ext pRC4, unsigned char *pbyDest,
                     unsigned char *pbySrc, unsigned int cbData_len)
{
    unsigned int ii;
    for (ii = 0; ii < cbData_len; ii++)
        pbyDest[ii] = (unsigned char)(pbySrc[ii] ^ rc4_byte(pRC4));
}
