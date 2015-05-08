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
 ******************************************************************************/
#ifndef __HAL_VERSION_DEF_H__
#define __HAL_VERSION_DEF_H__

enum hal_ic_type {
	CHIP_8192S		=	0,
	CHIP_8188C	=	1,
	CHIP_8192C	=	2,
	CHIP_8192D	=	3,
	CHIP_8723A	=	4,
	CHIP_8188E	=	5,
	CHIP_8881A	=	6,
	CHIP_8812A	=	7,
	CHIP_8821A	=	8,
	CHIP_8723B	=	9,
	CHIP_8192E		=	10,
};

enum hal_chip_type {
	TEST_CHIP		=	0,
	NORMAL_CHIP		=	1,
	FPGA			=	2,
};

enum hal_cut_version {
	A_CUT_VERSION		=	0,
	B_CUT_VERSION		=	1,
	C_CUT_VERSION		=	2,
	D_CUT_VERSION		=	3,
	E_CUT_VERSION		=	4,
	F_CUT_VERSION		=	5,
	G_CUT_VERSION		=	6,
};

/*  HAL_Manufacturer */
enum hal_vendor {
	CHIP_VENDOR_TSMC	=	0,
	CHIP_VENDOR_UMC		=	1,
};

struct hal_version {
	enum hal_ic_type	ICType;
	enum hal_chip_type	ChipType;
	enum hal_cut_version	CUTVersion;
	enum hal_vendor		VendorType;
	u8			ROMVer;
};

/*  Get element */
#define GET_CVID_IC_TYPE(version)	((version).ICType)
#define GET_CVID_CHIP_TYPE(version)	((version).ChipType)
#define GET_CVID_MANUFACTUER(version)	((version).VendorType)
#define GET_CVID_CUT_VERSION(version)	((version).CUTVersion)
#define GET_CVID_ROM_VERSION(version)	(((version).ROMVer) & ROM_VERSION_MASK)

/* Common Macro. -- */

#define IS_81XXC(version)			\
	(((GET_CVID_IC_TYPE(version) == CHIP_8192C) ||	\
	 (GET_CVID_IC_TYPE(version) == CHIP_8188C)) ? true : false)
#define IS_8723_SERIES(version)			\
	((GET_CVID_IC_TYPE(version) == CHIP_8723A) ? true : false)

#define IS_TEST_CHIP(version)			\
	((GET_CVID_CHIP_TYPE(version) == TEST_CHIP) ? true : false)
#define IS_NORMAL_CHIP(version)			\
	((GET_CVID_CHIP_TYPE(version) == NORMAL_CHIP) ? true : false)

#define IS_A_CUT(version)			\
	((GET_CVID_CUT_VERSION(version) == A_CUT_VERSION) ? true : false)
#define IS_B_CUT(version)			\
	((GET_CVID_CUT_VERSION(version) == B_CUT_VERSION) ? true : false)
#define IS_C_CUT(version)			\
	((GET_CVID_CUT_VERSION(version) == C_CUT_VERSION) ? true : false)
#define IS_D_CUT(version)			\
	((GET_CVID_CUT_VERSION(version) == D_CUT_VERSION) ? true : false)
#define IS_E_CUT(version)			\
	((GET_CVID_CUT_VERSION(version) == E_CUT_VERSION) ? true : false)

#define IS_CHIP_VENDOR_TSMC(version)		\
	((GET_CVID_MANUFACTUER(version) == CHIP_VENDOR_TSMC) ? true : false)
#define IS_CHIP_VENDOR_UMC(version)		\
	((GET_CVID_MANUFACTUER(version) == CHIP_VENDOR_UMC) ? true : false)

/* Chip version Macro. -- */

#define IS_81xxC_VENDOR_UMC_A_CUT(version)			\
	(IS_81XXC(version)?(IS_CHIP_VENDOR_UMC(version) ?	\
	(IS_A_CUT(version) ? true : false) : false) : false)
#define IS_81xxC_VENDOR_UMC_B_CUT(version)			\
	(IS_81XXC(version) ? (IS_CHIP_VENDOR_UMC(version) ?	\
	 (IS_B_CUT(version) ? true : false) : false): false)
#define IS_81xxC_VENDOR_UMC_C_CUT(version)			\
	(IS_81XXC(version)?(IS_CHIP_VENDOR_UMC(version) ?	\
	(IS_C_CUT(version) ? true : false) : false) : false)
#define IS_8723A_A_CUT(version)				\
	((IS_8723_SERIES(version)) ? (IS_A_CUT(version) ? true : false) : false)
#define IS_8723A_B_CUT(version)					\
	((IS_8723_SERIES(version)) ? (IS_B_CUT(version) ? true : false) : false)

#endif
