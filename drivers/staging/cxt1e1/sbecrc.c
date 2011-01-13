/*   Based on "File Verification Using CRC" by Mark R. Nelson in Dr. Dobbs'
 *   Journal, May 1992, pp. 64-67.  This algorithm generates the same CRC
 *   values as ZMODEM and PKZIP
 *
 * Copyright (C) 2002-2005  SBE, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/types.h>
#include "pmcc4_sysdep.h"
#include "sbecom_inline_linux.h"
#include "sbe_promformat.h"

/* defines */
#define CRC32_POLYNOMIAL                0xEDB88320L
#define CRC_TABLE_ENTRIES                       256



static      u_int32_t crcTableInit;

#ifdef STATIC_CRC_TABLE
static u_int32_t CRCTable[CRC_TABLE_ENTRIES];

#endif


/***************************************************************************
*
* genCrcTable - fills in CRCTable, as used by sbeCrc()
*
* RETURNS: N/A
*
* ERRNO: N/A
***************************************************************************/

static void
genCrcTable (u_int32_t *CRCTable)
{
    int         ii, jj;
    u_int32_t      crc;

    for (ii = 0; ii < CRC_TABLE_ENTRIES; ii++)
    {
        crc = ii;
        for (jj = 8; jj > 0; jj--)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            else
                crc >>= 1;
        }
        CRCTable[ii] = crc;
    }

    crcTableInit++;
}


/***************************************************************************
*
* sbeCrc - generates a CRC on a given buffer, and initial CRC
*
* This routine calculates the CRC for a buffer of data using the
* table lookup method. It accepts an original value for the crc,
* and returns the updated value. This permits "catenation" of
* discontiguous buffers. An original value of 0 for the "first"
* buffer is the norm.
*
* Based on "File Verification Using CRC" by Mark R. Nelson in Dr. Dobb's
* Journal, May 1992, pp. 64-67.  This algorithm generates the same CRC
* values as ZMODEM and PKZIP.
*
* RETURNS: calculated crc of block
*
*/

void
sbeCrc (u_int8_t *buffer,          /* data buffer to crc */
        u_int32_t count,           /* length of block in bytes */
        u_int32_t initialCrc,      /* starting CRC */
        u_int32_t *result)
{
    u_int32_t     *tbl = 0;
    u_int32_t      temp1, temp2, crc;

    /*
     * if table not yet created, do so. Don't care about "extra" time
     * checking this everytime sbeCrc() is called, since CRC calculations are
     * already time consuming
     */
    if (!crcTableInit)
    {
#ifdef STATIC_CRC_TABLE
        tbl = &CRCTable;
        genCrcTable (tbl);
#else
        tbl = (u_int32_t *) OS_kmalloc (CRC_TABLE_ENTRIES * sizeof (u_int32_t));
        if (tbl == 0)
        {
            *result = 0;            /* dummy up return value due to malloc
                                     * failure */
            return;
        }
        genCrcTable (tbl);
#endif
    }
    /* inverting bits makes ZMODEM & PKZIP compatible */
    crc = initialCrc ^ 0xFFFFFFFFL;

    while (count-- != 0)
    {
        temp1 = (crc >> 8) & 0x00FFFFFFL;
        temp2 = tbl[((int) crc ^ *buffer++) & 0xff];
        crc = temp1 ^ temp2;
    }

    crc ^= 0xFFFFFFFFL;

    *result = crc;

#ifndef STATIC_CRC_TABLE
    crcTableInit = 0;
    OS_kfree (tbl);
#endif
}

/*** End-of-File ***/
