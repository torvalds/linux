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

#define TRUE 	_TRUE	
#define FALSE	_FALSE

// HAL_IC_TYPE_E
typedef enum tag_HAL_IC_Type_Definition
{
	CHIP_8192S	=	0,
	CHIP_8188C	=	1,
	CHIP_8192C	=	2,
	CHIP_8192D	=	3,
	CHIP_8723A	=	4,
	CHIP_8188E	=	5,
	CHIP_8812	=	6,
	CHIP_8821	=	7,
	CHIP_8723B 	=	8,
	CHIP_8192E 	=	9,
	CHIP_8814A	=	10,
	CHIP_8703B	=	11,
	CHIP_8188F	=	12,
}HAL_IC_TYPE_E;

//HAL_CHIP_TYPE_E
typedef enum tag_HAL_CHIP_Type_Definition
{
	TEST_CHIP 		=	0,
	NORMAL_CHIP 	=	1,
	FPGA			=	2,
}HAL_CHIP_TYPE_E;

//HAL_CUT_VERSION_E
typedef enum tag_HAL_Cut_Version_Definition
{
	A_CUT_VERSION 		=	0,
	B_CUT_VERSION 		=	1,
	C_CUT_VERSION 		=	2,
	D_CUT_VERSION 		=	3,
	E_CUT_VERSION 		=	4,
	F_CUT_VERSION 		=	5,
	G_CUT_VERSION 		=	6,
	H_CUT_VERSION 		=	7,	
	I_CUT_VERSION 		=	8,
	J_CUT_VERSION 		=	9,
	K_CUT_VERSION 		=	10,
}HAL_CUT_VERSION_E;

// HAL_Manufacturer
typedef enum tag_HAL_Manufacturer_Version_Definition
{
	CHIP_VENDOR_TSMC 	=	0,
	CHIP_VENDOR_UMC 	=	1,
	CHIP_VENDOR_SMIC 	=	2, 
}HAL_VENDOR_E;

typedef enum tag_HAL_RF_Type_Definition
{
	RF_TYPE_1T1R 	=	0,
	RF_TYPE_1T2R 	=	1,
	RF_TYPE_2T2R	=	2,
	RF_TYPE_2T3R	=	3,
	RF_TYPE_2T4R	=	4,
	RF_TYPE_3T3R	=	5,
	RF_TYPE_3T4R	=	6,
	RF_TYPE_4T4R	=	7,
}HAL_RF_TYPE_E;

typedef	struct tag_HAL_VERSION
{
	HAL_IC_TYPE_E		ICType;
	HAL_CHIP_TYPE_E		ChipType;
	HAL_CUT_VERSION_E	CUTVersion;
	HAL_VENDOR_E		VendorType;
	HAL_RF_TYPE_E		RFType;	
	u8					ROMVer;
}HAL_VERSION,*PHAL_VERSION;

//VERSION_8192C			VersionID;
//HAL_VERSION			VersionID;

// Get element
#define GET_CVID_IC_TYPE(version)			((HAL_IC_TYPE_E)(((HAL_VERSION)version).ICType)	)
#define GET_CVID_CHIP_TYPE(version)			((HAL_CHIP_TYPE_E)(((HAL_VERSION)version).ChipType)	)
#define GET_CVID_RF_TYPE(version)			((HAL_RF_TYPE_E)(((HAL_VERSION)version).RFType))
#define GET_CVID_MANUFACTUER(version)		((HAL_VENDOR_E)(((HAL_VERSION)version).VendorType))
#define GET_CVID_CUT_VERSION(version)		((HAL_CUT_VERSION_E)(((HAL_VERSION)version).CUTVersion))
#define GET_CVID_ROM_VERSION(version)		((((HAL_VERSION)version).ROMVer) & ROM_VERSION_MASK)

//----------------------------------------------------------------------------
//Common Macro. --
//----------------------------------------------------------------------------
//HAL_VERSION VersionID

// HAL_IC_TYPE_E
#if 0
#define IS_81XXC(version)				(((GET_CVID_IC_TYPE(version) == CHIP_8192C)||(GET_CVID_IC_TYPE(version) == CHIP_8188C))? TRUE : FALSE)
#define IS_8723_SERIES(version)			((GET_CVID_IC_TYPE(version) == CHIP_8723A)? TRUE : FALSE)
#define IS_92D(version)					((GET_CVID_IC_TYPE(version) == CHIP_8192D)? TRUE : FALSE)
#endif

#define IS_8188E(version)					((GET_CVID_IC_TYPE(version) == CHIP_8188E)? TRUE : FALSE)
#define IS_8188F(version)					((GET_CVID_IC_TYPE(version) == CHIP_8188F) ? TRUE : FALSE)
#define IS_8192E(version)					((GET_CVID_IC_TYPE(version) == CHIP_8192E)? TRUE : FALSE)
#define IS_8812_SERIES(version)			((GET_CVID_IC_TYPE(version) == CHIP_8812)? TRUE : FALSE)
#define IS_8821_SERIES(version)			((GET_CVID_IC_TYPE(version) == CHIP_8821)? TRUE : FALSE)
#define IS_8814A_SERIES(version)			((GET_CVID_IC_TYPE(version) == CHIP_8814A) ? TRUE : FALSE)
#define IS_8723B_SERIES(version)			((GET_CVID_IC_TYPE(version) == CHIP_8723B)? TRUE : FALSE)
#define IS_8703B_SERIES(version)			((GET_CVID_IC_TYPE(version) == CHIP_8703B)? TRUE : FALSE)

//HAL_CHIP_TYPE_E
#define IS_TEST_CHIP(version)			((GET_CVID_CHIP_TYPE(version)==TEST_CHIP)? TRUE: FALSE)
#define IS_NORMAL_CHIP(version)			((GET_CVID_CHIP_TYPE(version)==NORMAL_CHIP)? TRUE: FALSE)

//HAL_CUT_VERSION_E
#define IS_A_CUT(version)				((GET_CVID_CUT_VERSION(version) == A_CUT_VERSION) ? TRUE : FALSE)
#define IS_B_CUT(version)				((GET_CVID_CUT_VERSION(version) == B_CUT_VERSION) ? TRUE : FALSE)
#define IS_C_CUT(version)				((GET_CVID_CUT_VERSION(version) == C_CUT_VERSION) ? TRUE : FALSE)
#define IS_D_CUT(version)				((GET_CVID_CUT_VERSION(version) == D_CUT_VERSION) ? TRUE : FALSE)
#define IS_E_CUT(version)				((GET_CVID_CUT_VERSION(version) == E_CUT_VERSION) ? TRUE : FALSE)
#define IS_F_CUT(version)				((GET_CVID_CUT_VERSION(version) == F_CUT_VERSION) ? TRUE : FALSE)
#define IS_I_CUT(version)				((GET_CVID_CUT_VERSION(version) == I_CUT_VERSION) ? TRUE : FALSE)
#define IS_J_CUT(version)				((GET_CVID_CUT_VERSION(version) == J_CUT_VERSION) ? TRUE : FALSE)
#define IS_K_CUT(version)				((GET_CVID_CUT_VERSION(version) == K_CUT_VERSION) ? TRUE : FALSE)

//HAL_VENDOR_E
#define IS_CHIP_VENDOR_TSMC(version)	((GET_CVID_MANUFACTUER(version) == CHIP_VENDOR_TSMC)? TRUE: FALSE)
#define IS_CHIP_VENDOR_UMC(version)	((GET_CVID_MANUFACTUER(version) == CHIP_VENDOR_UMC)? TRUE: FALSE)
#define IS_CHIP_VENDOR_SMIC(version)	((GET_CVID_MANUFACTUER(version) == CHIP_VENDOR_SMIC)? TRUE: FALSE)

//HAL_RF_TYPE_E
#define IS_1T1R(version)					((GET_CVID_RF_TYPE(version) == RF_TYPE_1T1R)? TRUE : FALSE )
#define IS_1T2R(version)					((GET_CVID_RF_TYPE(version) == RF_TYPE_1T2R)? TRUE : FALSE)
#define IS_2T2R(version)					((GET_CVID_RF_TYPE(version) == RF_TYPE_2T2R)? TRUE : FALSE)
#define IS_3T3R(version)					((GET_CVID_RF_TYPE(version) == RF_TYPE_3T3R)? TRUE : FALSE)
#define IS_3T4R(version)					((GET_CVID_RF_TYPE(version) == RF_TYPE_3T4R)? TRUE : FALSE)
#define IS_4T4R(version)					((GET_CVID_RF_TYPE(version) == RF_TYPE_4T4R)? TRUE : FALSE)



//----------------------------------------------------------------------------
//Chip version Macro. --
//----------------------------------------------------------------------------
#if 0
#define IS_81XXC_TEST_CHIP(version)		((IS_81XXC(version) && (!IS_NORMAL_CHIP(version)))? TRUE: FALSE)

#define IS_92C_SERIAL(version)   					((IS_81XXC(version) && IS_2T2R(version)) ? TRUE : FALSE)
#define IS_81xxC_VENDOR_UMC_A_CUT(version)	(IS_81XXC(version)?(IS_CHIP_VENDOR_UMC(version) ? (IS_A_CUT(version) ? TRUE : FALSE) : FALSE): FALSE)
#define IS_81xxC_VENDOR_UMC_B_CUT(version)	(IS_81XXC(version)?(IS_CHIP_VENDOR_UMC(version) ? (IS_B_CUT(version) ? TRUE : FALSE) : FALSE): FALSE)
#define IS_81xxC_VENDOR_UMC_C_CUT(version)	(IS_81XXC(version)?(IS_CHIP_VENDOR_UMC(version) ? (IS_C_CUT(version) ? TRUE : FALSE) : FALSE): FALSE)

#define IS_NORMAL_CHIP92D(version)		(( IS_92D(version))?((GET_CVID_CHIP_TYPE(version)==NORMAL_CHIP)? TRUE: FALSE):FALSE)

#define IS_92D_SINGLEPHY(version)		((IS_92D(version)) ? (IS_2T2R(version) ? TRUE: FALSE) : FALSE)
#define IS_92D_C_CUT(version)			((IS_92D(version)) ? (IS_C_CUT(version) ? TRUE : FALSE) : FALSE)
#define IS_92D_D_CUT(version)			((IS_92D(version)) ? (IS_D_CUT(version) ? TRUE : FALSE) : FALSE)
#define IS_92D_E_CUT(version)			((IS_92D(version)) ? (IS_E_CUT(version) ? TRUE : FALSE) : FALSE)

#define IS_8723A_A_CUT(version)				((IS_8723_SERIES(version)) ? ( IS_A_CUT(version)?TRUE : FALSE) : FALSE)
#define IS_8723A_B_CUT(version)				((IS_8723_SERIES(version)) ? ( IS_B_CUT(version)?TRUE : FALSE) : FALSE)
#endif
#ifdef CONFIG_USB_HCI
#define IS_VENDOR_8188E_I_CUT_SERIES(_Adapter)		(FALSE)
#else
#define IS_VENDOR_8188E_I_CUT_SERIES(_Adapter)		((IS_8188E(GET_HAL_DATA(_Adapter)->VersionID)) ? ((GET_CVID_CUT_VERSION(GET_HAL_DATA(_Adapter)->VersionID) >= I_CUT_VERSION) ? TRUE : FALSE) : FALSE)
#endif
#define IS_VENDOR_8812A_TEST_CHIP(_Adapter)		((IS_8812_SERIES(GET_HAL_DATA(_Adapter)->VersionID)) ? ((IS_NORMAL_CHIP(GET_HAL_DATA(_Adapter)->VersionID)) ? FALSE : TRUE) : FALSE)
#define IS_VENDOR_8812A_MP_CHIP(_Adapter)		((IS_8812_SERIES(GET_HAL_DATA(_Adapter)->VersionID)) ? ((IS_NORMAL_CHIP(GET_HAL_DATA(_Adapter)->VersionID)) ? TRUE : FALSE) : FALSE)
#define IS_VENDOR_8812A_C_CUT(_Adapter)			((IS_8812_SERIES(GET_HAL_DATA(_Adapter)->VersionID)) ? ((GET_CVID_CUT_VERSION(GET_HAL_DATA(_Adapter)->VersionID) == C_CUT_VERSION) ? TRUE : FALSE) : FALSE)

#define IS_VENDOR_8821A_TEST_CHIP(_Adapter)	((IS_8821_SERIES(GET_HAL_DATA(_Adapter)->VersionID)) ? ((IS_NORMAL_CHIP(GET_HAL_DATA(_Adapter)->VersionID)) ? FALSE : TRUE) : FALSE)
#define IS_VENDOR_8821A_MP_CHIP(_Adapter)		((IS_8821_SERIES(GET_HAL_DATA(_Adapter)->VersionID)) ? ((IS_NORMAL_CHIP(GET_HAL_DATA(_Adapter)->VersionID)) ? TRUE : FALSE) : FALSE)

#define IS_VENDOR_8192E_B_CUT(_Adapter)		((GET_CVID_CUT_VERSION(GET_HAL_DATA(_Adapter)->VersionID) == B_CUT_VERSION) ? TRUE : FALSE)

#define IS_VENDOR_8723B_TEST_CHIP(_Adapter)	((IS_8723B_SERIES(GET_HAL_DATA(_Adapter)->VersionID)) ? ((IS_NORMAL_CHIP(GET_HAL_DATA(_Adapter)->VersionID)) ? FALSE : TRUE) : FALSE)
#define IS_VENDOR_8723B_MP_CHIP(_Adapter)		((IS_8723B_SERIES(GET_HAL_DATA(_Adapter)->VersionID)) ? ((IS_NORMAL_CHIP(GET_HAL_DATA(_Adapter)->VersionID)) ? TRUE : FALSE) : FALSE)

#define IS_VENDOR_8703B_TEST_CHIP(_Adapter)	((IS_8703B_SERIES(GET_HAL_DATA(_Adapter)->VersionID)) ? ((IS_NORMAL_CHIP(GET_HAL_DATA(_Adapter)->VersionID)) ? FALSE : TRUE) : FALSE)
#define IS_VENDOR_8703B_MP_CHIP(_Adapter)		((IS_8703B_SERIES(GET_HAL_DATA(_Adapter)->VersionID)) ? ((IS_NORMAL_CHIP(GET_HAL_DATA(_Adapter)->VersionID)) ? TRUE : FALSE) : FALSE)
#define IS_VENDOR_8814A_TEST_CHIP(_Adapter)	((IS_8814A_SERIES(GET_HAL_DATA(_Adapter)->VersionID)) ? ((IS_NORMAL_CHIP(GET_HAL_DATA(_Adapter)->VersionID)) ? FALSE : TRUE) : FALSE)
#define IS_VENDOR_8814A_MP_CHIP(_Adapter)		((IS_8814A_SERIES(GET_HAL_DATA(_Adapter)->VersionID)) ? ((IS_NORMAL_CHIP(GET_HAL_DATA(_Adapter)->VersionID)) ? TRUE : FALSE) : FALSE)

#endif

