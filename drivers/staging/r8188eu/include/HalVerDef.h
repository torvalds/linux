/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */
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

enum HAL_RF_TYPE {
	RF_TYPE_1T1R	=	0,
	RF_TYPE_1T2R	=	1,
	RF_TYPE_2T2R	=	2,
	RF_TYPE_2T3R	=	3,
	RF_TYPE_2T4R	=	4,
	RF_TYPE_3T3R	=	5,
	RF_TYPE_3T4R	=	6,
	RF_TYPE_4T4R	=	7,
};

struct HAL_VERSION {
	enum HAL_CHIP_TYPE	ChipType;
	enum HAL_CUT_VERSION	CUTVersion;
	enum HAL_VENDOR		VendorType;
	enum HAL_RF_TYPE	RFType;
	u8			ROMVer;
};

/*  Get element */
#define GET_CVID_CHIP_TYPE(version)	(((version).ChipType))
#define GET_CVID_RF_TYPE(version)	(((version).RFType))
#define GET_CVID_MANUFACTUER(version)	(((version).VendorType))
#define GET_CVID_CUT_VERSION(version)	(((version).CUTVersion))
#define GET_CVID_ROM_VERSION(version)	(((version).ROMVer) & ROM_VERSION_MASK)

/* Common Macro. -- */
/* HAL_VERSION VersionID */

/* HAL_CHIP_TYPE_E */
#define IS_TEST_CHIP(version)				\
	((GET_CVID_CHIP_TYPE(version) == TEST_CHIP) ? true : false)
#define IS_NORMAL_CHIP(version)				\
	((GET_CVID_CHIP_TYPE(version) == NORMAL_CHIP) ? true : false)

/* HAL_CUT_VERSION_E */
#define IS_A_CUT(version)				\
	((GET_CVID_CUT_VERSION(version) == A_CUT_VERSION) ? true : false)
#define IS_B_CUT(version)				\
	((GET_CVID_CUT_VERSION(version) == B_CUT_VERSION) ? true : false)
#define IS_C_CUT(version)				\
	((GET_CVID_CUT_VERSION(version) == C_CUT_VERSION) ? true : false)
#define IS_D_CUT(version)				\
	((GET_CVID_CUT_VERSION(version) == D_CUT_VERSION) ? true : false)
#define IS_E_CUT(version)				\
	((GET_CVID_CUT_VERSION(version) == E_CUT_VERSION) ? true : false)

/* HAL_VENDOR_E */
#define IS_CHIP_VENDOR_TSMC(version)			\
	((GET_CVID_MANUFACTUER(version) == CHIP_VENDOR_TSMC) ? true : false)
#define IS_CHIP_VENDOR_UMC(version)			\
	((GET_CVID_MANUFACTUER(version) == CHIP_VENDOR_UMC) ? true : false)

/* HAL_RF_TYPE_E */
#define IS_1T1R(version)				\
	((GET_CVID_RF_TYPE(version) == RF_TYPE_1T1R) ? true : false)
#define IS_1T2R(version)				\
	((GET_CVID_RF_TYPE(version) == RF_TYPE_1T2R) ? true : false)
#define IS_2T2R(version)				\
	((GET_CVID_RF_TYPE(version) == RF_TYPE_2T2R) ? true : false)

#endif
