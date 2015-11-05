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
#ifndef __HAL_VERSION_DEF_H__
#define __HAL_VERSION_DEF_H__

enum HAL_CHIP_TYPE {
	TEST_CHIP	=	0,
	NORMAL_CHIP	=	1,
	FPGA		=	2,
};

enum HAL_CUT_VERSION {
	A_CUT_VERSION	=	0,
	B_CUT_VERSION	=	1,
	C_CUT_VERSION	=	2,
	D_CUT_VERSION	=	3,
	E_CUT_VERSION	=	4,
	F_CUT_VERSION	=	5,
	G_CUT_VERSION	=	6,
};

enum HAL_VENDOR {
	CHIP_VENDOR_TSMC	=	0,
	CHIP_VENDOR_UMC		=	1,
};

struct HAL_VERSION {
	enum HAL_CHIP_TYPE	ChipType;
	enum HAL_CUT_VERSION	CUTVersion;
	enum HAL_VENDOR		VendorType;
};

#endif
