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
 * File: Michael.h
 *
 * Purpose: Reference implementation for Michael
 *          written by Niels Ferguson
 *
 * Author: Kyle Hsu
 *
 * Date: Jan 2, 2003
 *
 */

#ifndef __MICHAEL_H__
#define __MICHAEL_H__

/*---------------------  Export Definitions -------------------------*/

/*---------------------  Export Types  ------------------------------*/

void MIC_vInit(DWORD dwK0, DWORD dwK1);

void MIC_vUnInit(void);

// Append bytes to the message to be MICed
void MIC_vAppend(PBYTE src, unsigned int nBytes);

// Get the MIC result. Destination should accept 8 bytes of result.
// This also resets the message to empty.
void MIC_vGetMIC(PDWORD pdwL, PDWORD pdwR);

/*---------------------  Export Macros ------------------------------*/

// Rotation functions on 32 bit values
#define ROL32(A, n) \
 (((A) << (n)) | (((A)>>(32-(n)))  & ((1UL << (n)) - 1)))
#define ROR32(A, n) ROL32((A), 32-(n))

#endif /* __MICHAEL_H__ */
