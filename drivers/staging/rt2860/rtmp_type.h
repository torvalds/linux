/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

    Module Name:
    rtmp_type.h

    Abstract:

    Revision History:
    Who         When            What
    --------    ----------      ----------------------------------------------
    Name        Date            Modification logs
    Paul Lin    1-2-2004
*/
#ifndef __RTMP_TYPE_H__
#define __RTMP_TYPE_H__

#include <linux/types.h>

#define PACKED  __attribute__ ((packed))

typedef unsigned char BOOLEAN;

typedef union _LARGE_INTEGER {
	struct {
		u32 LowPart;
		int HighPart;
	} u;
	long long QuadPart;
} LARGE_INTEGER;

/* */
/* Register set pair for initialzation register set definition */
/* */
struct rt_rtmp_reg_pair {
	unsigned long Register;
	unsigned long Value;
};

struct rt_reg_pair {
	u8 Register;
	u8 Value;
};

/* */
/* Register set pair for initialzation register set definition */
/* */
struct rt_rtmp_rf_regs {
	u8 Channel;
	unsigned long R1;
	unsigned long R2;
	unsigned long R3;
	unsigned long R4;
};

struct rt_frequency_item {
	u8 Channel;
	u8 N;
	u8 R;
	u8 K;
};

#define STATUS_SUCCESS				0x00
#define STATUS_UNSUCCESSFUL		0x01

#endif /* __RTMP_TYPE_H__ // */
