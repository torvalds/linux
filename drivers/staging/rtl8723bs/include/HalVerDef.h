/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __HAL_VERSION_DEF_H__
#define __HAL_VERSION_DEF_H__

/*  hal_ic_type_e */
enum hal_ic_type_e { /* tag_HAL_IC_Type_Definition */
	CHIP_8723B	=	8,
};

/* hal_chip_type_e */
enum hal_chip_type_e { /* tag_HAL_CHIP_Type_Definition */
	TEST_CHIP		=	0,
	NORMAL_CHIP	=	1,
	FPGA			=	2,
};

/* hal_cut_version_e */
enum hal_cut_version_e { /* tag_HAL_Cut_Version_Definition */
	A_CUT_VERSION		=	0,
	B_CUT_VERSION		=	1,
	C_CUT_VERSION		=	2,
	D_CUT_VERSION		=	3,
	E_CUT_VERSION		=	4,
	F_CUT_VERSION		=	5,
	G_CUT_VERSION		=	6,
	H_CUT_VERSION		=	7,
	I_CUT_VERSION		=	8,
	J_CUT_VERSION		=	9,
	K_CUT_VERSION		=	10,
};

/*  HAL_Manufacturer */
enum hal_vendor_e { /* tag_HAL_Manufacturer_Version_Definition */
	CHIP_VENDOR_TSMC	=	0,
	CHIP_VENDOR_UMC		=	1,
	CHIP_VENDOR_SMIC	=	2,
};

struct hal_version { /* tag_HAL_VERSION */
	enum hal_ic_type_e		ICType;
	enum hal_chip_type_e		ChipType;
	enum hal_cut_version_e	CUTVersion;
	enum hal_vendor_e		VendorType;
	u8 			ROMVer;
};

/* hal_version			VersionID; */

/*  Get element */
#define GET_CVID_IC_TYPE(version)			((enum hal_ic_type_e)((version).ICType))
#define GET_CVID_CHIP_TYPE(version)			((enum hal_chip_type_e)((version).ChipType))
#define GET_CVID_MANUFACTUER(version)		((enum hal_vendor_e)((version).VendorType))
#define GET_CVID_CUT_VERSION(version)		((enum hal_cut_version_e)((version).CUTVersion))
#define GET_CVID_ROM_VERSION(version)		(((version).ROMVer) & ROM_VERSION_MASK)

/*  */
/* Common Macro. -- */
/*  */
/* hal_version VersionID */

/* hal_chip_type_e */
#define IS_TEST_CHIP(version)			((GET_CVID_CHIP_TYPE(version) == TEST_CHIP) ? true : false)
#define IS_NORMAL_CHIP(version)			((GET_CVID_CHIP_TYPE(version) == NORMAL_CHIP) ? true : false)

/* hal_cut_version_e */
#define IS_A_CUT(version)				((GET_CVID_CUT_VERSION(version) == A_CUT_VERSION) ? true : false)
#define IS_B_CUT(version)				((GET_CVID_CUT_VERSION(version) == B_CUT_VERSION) ? true : false)
#define IS_C_CUT(version)				((GET_CVID_CUT_VERSION(version) == C_CUT_VERSION) ? true : false)
#define IS_D_CUT(version)				((GET_CVID_CUT_VERSION(version) == D_CUT_VERSION) ? true : false)
#define IS_E_CUT(version)				((GET_CVID_CUT_VERSION(version) == E_CUT_VERSION) ? true : false)
#define IS_I_CUT(version)				((GET_CVID_CUT_VERSION(version) == I_CUT_VERSION) ? true : false)
#define IS_J_CUT(version)				((GET_CVID_CUT_VERSION(version) == J_CUT_VERSION) ? true : false)
#define IS_K_CUT(version)				((GET_CVID_CUT_VERSION(version) == K_CUT_VERSION) ? true : false)

/* hal_vendor_e */
#define IS_CHIP_VENDOR_TSMC(version)	((GET_CVID_MANUFACTUER(version) == CHIP_VENDOR_TSMC) ? true : false)
#define IS_CHIP_VENDOR_UMC(version)	((GET_CVID_MANUFACTUER(version) == CHIP_VENDOR_UMC) ? true : false)
#define IS_CHIP_VENDOR_SMIC(version)	((GET_CVID_MANUFACTUER(version) == CHIP_VENDOR_SMIC) ? true : false)

#endif
