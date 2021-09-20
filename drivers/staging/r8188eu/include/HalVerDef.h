/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */
#ifndef __HAL_VERSION_DEF_H__
#define __HAL_VERSION_DEF_H__

enum HAL_IC_TYPE {
	CHIP_8192S	=	0,
	CHIP_8188C	=	1,
	CHIP_8192C	=	2,
	CHIP_8192D	=	3,
	CHIP_8723A	=	4,
	CHIP_8188E	=	5,
	CHIP_8881A	=	6,
	CHIP_8812A	=	7,
	CHIP_8821A	=	8,
	CHIP_8723B	=	9,
	CHIP_8192E	=	10,
};

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
	enum HAL_IC_TYPE	ICType;
	enum HAL_CHIP_TYPE	ChipType;
	enum HAL_CUT_VERSION	CUTVersion;
	enum HAL_VENDOR		VendorType;
	enum HAL_RF_TYPE	RFType;
	u8			ROMVer;
};

/*  Get element */
#define GET_CVID_IC_TYPE(version)	(((version).ICType))
#define GET_CVID_CHIP_TYPE(version)	(((version).ChipType))
#define GET_CVID_RF_TYPE(version)	(((version).RFType))
#define GET_CVID_MANUFACTUER(version)	(((version).VendorType))
#define GET_CVID_CUT_VERSION(version)	(((version).CUTVersion))
#define GET_CVID_ROM_VERSION(version)	(((version).ROMVer) & ROM_VERSION_MASK)

/* Common Macro. -- */
/* HAL_VERSION VersionID */

/*  HAL_IC_TYPE_E */
#define IS_81XXC(version)				\
	(((GET_CVID_IC_TYPE(version) == CHIP_8192C) ||	\
	 (GET_CVID_IC_TYPE(version) == CHIP_8188C)) ? true : false)
#define IS_8723_SERIES(version)				\
	((GET_CVID_IC_TYPE(version) == CHIP_8723A) ? true : false)
#define IS_92D(version)					\
	((GET_CVID_IC_TYPE(version) == CHIP_8192D) ? true : false)
#define IS_8188E(version)				\
	((GET_CVID_IC_TYPE(version) == CHIP_8188E) ? true : false)

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

/* Chip version Macro. -- */
#define IS_81XXC_TEST_CHIP(version)			\
	((IS_81XXC(version) && (!IS_NORMAL_CHIP(version))) ? true : false)

#define IS_92C_SERIAL(version)				\
	((IS_81XXC(version) && IS_2T2R(version)) ? true : false)
#define IS_81xxC_VENDOR_UMC_A_CUT(version)		\
	(IS_81XXC(version) ? (IS_CHIP_VENDOR_UMC(version) ?	\
	(IS_A_CUT(version) ? true : false) : false) : false)
#define IS_81xxC_VENDOR_UMC_B_CUT(version)		\
	(IS_81XXC(version) ? (IS_CHIP_VENDOR_UMC(version) ?	\
	(IS_B_CUT(version) ? true : false) : false) : false)
#define IS_81xxC_VENDOR_UMC_C_CUT(version)		\
	(IS_81XXC(version) ? (IS_CHIP_VENDOR_UMC(version) ? \
	 (IS_C_CUT(version) ? true : false) : false) : false)

#define IS_NORMAL_CHIP92D(version)			\
	((IS_92D(version)) ?				\
	((GET_CVID_CHIP_TYPE(version) == NORMAL_CHIP) ? true : false) : false)

#define IS_92D_SINGLEPHY(version)			\
	((IS_92D(version)) ? (IS_2T2R(version) ? true : false) : false)
#define IS_92D_C_CUT(version)				\
	((IS_92D(version)) ? (IS_C_CUT(version) ? true : false) : false)
#define IS_92D_D_CUT(version)				\
	((IS_92D(version)) ? (IS_D_CUT(version) ? true : false) : false)
#define IS_92D_E_CUT(version)				\
	((IS_92D(version)) ? (IS_E_CUT(version) ? true : false) : false)

#define IS_8723A_A_CUT(version)				\
	((IS_8723_SERIES(version)) ? (IS_A_CUT(version) ? true : false) : false)
#define IS_8723A_B_CUT(version)				\
	((IS_8723_SERIES(version)) ? (IS_B_CUT(version) ? true : false) : false)

#endif
