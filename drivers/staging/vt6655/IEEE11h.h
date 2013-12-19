/*
 * Copyright (c) 1996, 2005 VIA Networking Technologies, Inc.
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
 * File: IEEE11h.h
 *
 * Purpose: Defines the macros, types, and functions for dealing
 *          with IEEE 802.11h.
 *
 * Author: Yiching Chen
 *
 * Date: Mar. 31, 2005
 *
 */

#ifndef __IEEE11h_H__
#define __IEEE11h_H__

#include "ttype.h"
#include "80211hdr.h"
#include "80211mgr.h"

/*---------------------  Export Definitions -------------------------*/

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Types  ------------------------------*/

/*---------------------  Export Functions  --------------------------*/

bool IEEE11hbMSRRepTx(
	void *pMgmtHandle
);

#endif // __IEEE11h_H__
