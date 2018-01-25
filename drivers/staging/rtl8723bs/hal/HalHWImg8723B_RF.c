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

#include <linux/kernel.h>
#include "odm_precomp.h"

static bool CheckPositive(
	PDM_ODM_T pDM_Odm, const u32 Condition1, const u32 Condition2
)
{
	u8 _BoardType =
			((pDM_Odm->BoardType & BIT4) >> 4) << 0 | /*  _GLNA */
			((pDM_Odm->BoardType & BIT3) >> 3) << 1 | /*  _GPA */
			((pDM_Odm->BoardType & BIT7) >> 7) << 2 | /*  _ALNA */
			((pDM_Odm->BoardType & BIT6) >> 6) << 3 | /*  _APA */
			((pDM_Odm->BoardType & BIT2) >> 2) << 4;  /*  _BT */

	u32 cond1 = Condition1, cond2 = Condition2;
	u32 driver1 =
		pDM_Odm->CutVersion << 24 |
		pDM_Odm->SupportPlatform << 16 |
		pDM_Odm->PackageType << 12 |
		pDM_Odm->SupportInterface << 8 |
		_BoardType;

	u32 driver2 =
		pDM_Odm->TypeGLNA <<  0 |
		pDM_Odm->TypeGPA  <<  8 |
		pDM_Odm->TypeALNA << 16 |
		pDM_Odm->TypeAPA  << 24;

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_TRACE,
		(
			"===> [8812A] CheckPositive (cond1, cond2) = (0x%X 0x%X)\n",
			cond1,
			cond2
		)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_TRACE,
		(
			"===> [8812A] CheckPositive (driver1, driver2) = (0x%X 0x%X)\n",
			driver1,
			driver2
		)
	);

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_TRACE,
		(
			"	(Platform, Interface) = (0x%X, 0x%X)\n",
			pDM_Odm->SupportPlatform,
			pDM_Odm->SupportInterface
		)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_TRACE,
		(
			"	(Board, Package) = (0x%X, 0x%X)\n",
			pDM_Odm->BoardType,
			pDM_Odm->PackageType
		)
	);

	/*  Value Defined Check =============== */
	/* QFN Type [15:12] and Cut Version [27:24] need to do value check */

	if (
		((cond1 & 0x0000F000) != 0) &&
		((cond1 & 0x0000F000) != (driver1 & 0x0000F000))
	)
		return false;

	if (
		((cond1 & 0x0F000000) != 0) &&
		((cond1 & 0x0F000000) != (driver1 & 0x0F000000))
	)
		return false;

	/*  Bit Defined Check ================ */
	/*  We don't care [31:28] and [23:20] */
	cond1   &= 0x000F0FFF;
	driver1 &= 0x000F0FFF;

	if ((cond1 & driver1) == cond1) {
		u32 bitMask = 0;

		if ((cond1 & 0x0F) == 0) /*  BoardType is DONTCARE */
			return true;

		if ((cond1 & BIT0) != 0) /* GLNA */
			bitMask |= 0x000000FF;
		if ((cond1 & BIT1) != 0) /* GPA */
			bitMask |= 0x0000FF00;
		if ((cond1 & BIT2) != 0) /* ALNA */
			bitMask |= 0x00FF0000;
		if ((cond1 & BIT3) != 0) /* APA */
			bitMask |= 0xFF000000;

		/*  BoardType of each RF path is matched */
		if ((cond2 & bitMask) == (driver2 & bitMask))
			return true;

		return false;
	}

	return false;
}

static bool CheckNegative(
	PDM_ODM_T pDM_Odm, const u32  Condition1, const u32 Condition2
)
{
	return true;
}

/******************************************************************************
*                           RadioA.TXT
******************************************************************************/

static u32 Array_MP_8723B_RadioA[] = {
		0x000, 0x00010000,
		0x0B0, 0x000DFFE0,
		0x0FE, 0x00000000,
		0x0FE, 0x00000000,
		0x0FE, 0x00000000,
		0x0B1, 0x00000018,
		0x0FE, 0x00000000,
		0x0FE, 0x00000000,
		0x0FE, 0x00000000,
		0x0B2, 0x00084C00,
		0x0B5, 0x0000D2CC,
		0x0B6, 0x000925AA,
		0x0B7, 0x00000010,
		0x0B8, 0x0000907F,
		0x05C, 0x00000002,
		0x07C, 0x00000002,
		0x07E, 0x00000005,
		0x08B, 0x0006FC00,
		0x0B0, 0x000FF9F0,
		0x01C, 0x000739D2,
		0x01E, 0x00000000,
		0x0DF, 0x00000780,
		0x050, 0x00067435,
	0x80002000, 0x00000000, 0x40000000, 0x00000000,
		0x051, 0x0006B10E,
	0x90003000, 0x00000000, 0x40000000, 0x00000000,
		0x051, 0x0006B10E,
	0x90004000, 0x00000000, 0x40000000, 0x00000000,
		0x051, 0x0006B10E,
	0xA0000000, 0x00000000,
		0x051, 0x0006B04E,
	0xB0000000, 0x00000000,
		0x052, 0x000007D2,
		0x053, 0x00000000,
		0x054, 0x00050400,
		0x055, 0x0004026E,
		0x0DD, 0x0000004C,
		0x070, 0x00067435,
	0x80002000, 0x00000000, 0x40000000, 0x00000000,
		0x071, 0x0006B10E,
	0x90003000, 0x00000000, 0x40000000, 0x00000000,
		0x071, 0x0006B10E,
	0x90004000, 0x00000000, 0x40000000, 0x00000000,
		0x071, 0x0006B10E,
	0xA0000000, 0x00000000,
		0x071, 0x0006B04E,
	0xB0000000, 0x00000000,
		0x072, 0x000007D2,
		0x073, 0x00000000,
		0x074, 0x00050400,
		0x075, 0x0004026E,
		0x0EF, 0x00000100,
		0x034, 0x0000ADD7,
		0x035, 0x00005C00,
		0x034, 0x00009DD4,
		0x035, 0x00005000,
		0x034, 0x00008DD1,
		0x035, 0x00004400,
		0x034, 0x00007DCE,
		0x035, 0x00003800,
		0x034, 0x00006CD1,
		0x035, 0x00004400,
		0x034, 0x00005CCE,
		0x035, 0x00003800,
		0x034, 0x000048CE,
		0x035, 0x00004400,
		0x034, 0x000034CE,
		0x035, 0x00003800,
		0x034, 0x00002451,
		0x035, 0x00004400,
		0x034, 0x0000144E,
		0x035, 0x00003800,
		0x034, 0x00000051,
		0x035, 0x00004400,
		0x0EF, 0x00000000,
		0x0EF, 0x00000100,
		0x0ED, 0x00000010,
		0x044, 0x0000ADD7,
		0x044, 0x00009DD4,
		0x044, 0x00008DD1,
		0x044, 0x00007DCE,
		0x044, 0x00006CC1,
		0x044, 0x00005CCE,
		0x044, 0x000044D1,
		0x044, 0x000034CE,
		0x044, 0x00002451,
		0x044, 0x0000144E,
		0x044, 0x00000051,
		0x0EF, 0x00000000,
		0x0ED, 0x00000000,
		0x07F, 0x00020080,
		0x0EF, 0x00002000,
		0x03B, 0x000380EF,
		0x03B, 0x000302FE,
		0x03B, 0x00028CE6,
		0x03B, 0x000200BC,
		0x03B, 0x000188A5,
		0x03B, 0x00010FBC,
		0x03B, 0x00008F71,
		0x03B, 0x00000900,
		0x0EF, 0x00000000,
		0x0ED, 0x00000001,
		0x040, 0x000380EF,
		0x040, 0x000302FE,
		0x040, 0x00028CE6,
		0x040, 0x000200BC,
		0x040, 0x000188A5,
		0x040, 0x00010FBC,
		0x040, 0x00008F71,
		0x040, 0x00000900,
		0x0ED, 0x00000000,
		0x082, 0x00080000,
		0x083, 0x00008000,
		0x084, 0x00048D80,
		0x085, 0x00068000,
		0x0A2, 0x00080000,
		0x0A3, 0x00008000,
		0x0A4, 0x00048D80,
		0x0A5, 0x00068000,
		0x0ED, 0x00000002,
		0x0EF, 0x00000002,
		0x056, 0x00000032,
		0x076, 0x00000032,
		0x001, 0x00000780,

};

void ODM_ReadAndConfig_MP_8723B_RadioA(PDM_ODM_T pDM_Odm)
{
	u32 i = 0;
	u32 ArrayLen = ARRAY_SIZE(Array_MP_8723B_RadioA);
	u32 *Array = Array_MP_8723B_RadioA;

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_LOUD,
		("===> ODM_ReadAndConfig_MP_8723B_RadioA\n")
	);

	for (i = 0; i < ArrayLen; i += 2) {
		u32 v1 = Array[i];
		u32 v2 = Array[i+1];

		/*  This (offset, data) pair doesn't care the condition. */
		if (v1 < 0x40000000) {
			odm_ConfigRF_RadioA_8723B(pDM_Odm, v1, v2);
			continue;
		} else {
			/*  This line is the beginning of branch. */
			bool bMatched = true;
			u8  cCond  = (u8)((v1 & (BIT29|BIT28)) >> 28);

			if (cCond == COND_ELSE) { /*  ELSE, ENDIF */
				bMatched = true;
				READ_NEXT_PAIR(v1, v2, i);
			} else if (!CheckPositive(pDM_Odm, v1, v2)) {
				bMatched = false;
				READ_NEXT_PAIR(v1, v2, i);
				READ_NEXT_PAIR(v1, v2, i);
			} else {
				READ_NEXT_PAIR(v1, v2, i);
				if (!CheckNegative(pDM_Odm, v1, v2))
					bMatched = false;
				else
					bMatched = true;
				READ_NEXT_PAIR(v1, v2, i);
			}

			if (bMatched == false) {
				/*  Condition isn't matched.
				*   Discard the following (offset, data) pairs.
				*/
				while (v1 < 0x40000000 && i < ArrayLen-2)
					READ_NEXT_PAIR(v1, v2, i);

				i -= 2; /*  prevent from for-loop += 2 */
			} else {
				/*  Configure matched pairs and skip to end of if-else. */
				while (v1 < 0x40000000 && i < ArrayLen-2) {
					odm_ConfigRF_RadioA_8723B(pDM_Odm, v1, v2);
					READ_NEXT_PAIR(v1, v2, i);
				}

				/*  Keeps reading until ENDIF. */
				cCond = (u8)((v1 & (BIT29|BIT28)) >> 28);
				while (cCond != COND_ENDIF && i < ArrayLen-2) {
					READ_NEXT_PAIR(v1, v2, i);
					cCond = (u8)((v1 & (BIT29|BIT28)) >> 28);
				}
			}
		}
	}
}

/******************************************************************************
*                           TxPowerTrack_SDIO.TXT
******************************************************************************/

static u8 gDeltaSwingTableIdx_MP_5GB_N_TxPowerTrack_SDIO_8723B[][DELTA_SWINGIDX_SIZE] = {
	{
		0, 1, 1, 2, 2, 3, 4, 5, 5, 6,  6,  7,  7,  8,  8,  9,
		9, 10, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14
	},
	{
		0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  7,  8,  8,  9,  9, 10,
		10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14
	},
	{
		0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  7,  8,  8,  9,  9, 10,
		10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14
	},
};
static u8 gDeltaSwingTableIdx_MP_5GB_P_TxPowerTrack_SDIO_8723B[][DELTA_SWINGIDX_SIZE] = {
	{
		0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12,
		12, 13, 14, 15, 15, 16, 16, 17, 17, 18, 19, 20, 20, 20
	},
	{
		0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12,
		12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 19, 20, 20, 20
	},
	{
		0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12,
		12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21
	},
};
static u8 gDeltaSwingTableIdx_MP_5GA_N_TxPowerTrack_SDIO_8723B[][DELTA_SWINGIDX_SIZE] = {
	{
		0, 1, 2, 3, 3, 4, 4, 5, 5, 6,  7,  8,  8,  9,  9, 10,
		10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14
	},
	{
		0, 1, 2, 3, 3, 4, 5, 6, 6, 6,  7,  7,  8,  8,  9, 10,
		11, 11, 12, 13, 13, 14, 15, 16, 16, 16, 16, 16, 16, 16
	},
	{
		0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 10, 11,
		11, 12, 13, 14, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16
	},
};
static u8 gDeltaSwingTableIdx_MP_5GA_P_TxPowerTrack_SDIO_8723B[][DELTA_SWINGIDX_SIZE] = {
	{
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	},
	{
		0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12,
		12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21
	},
	{
		0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12,
		12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21
	},
};
static u8 gDeltaSwingTableIdx_MP_2GB_N_TxPowerTrack_SDIO_8723B[] = {
	0, 0, 1, 2, 2, 2, 3, 3, 3, 4,  5,  5,  6,  6, 6,  6,
	7,  7,  7, 8,  8,  9,  9, 10, 10, 11, 12, 13, 14, 15
};
static u8 gDeltaSwingTableIdx_MP_2GB_P_TxPowerTrack_SDIO_8723B[] = {
	0, 0, 1, 2, 2, 3, 3, 4, 5, 5,  6,  6,  7,  7,  8,  8,
	9,  9, 10, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15
};
static u8 gDeltaSwingTableIdx_MP_2GA_N_TxPowerTrack_SDIO_8723B[] = {
	0, 0, 1, 2, 2, 2, 3, 3, 3, 4,  5,  5,  6,  6,  6,  6,
	7,  7,  7,  8,  8,  9,  9, 10, 10, 11, 12, 13, 14, 15
};
static u8 gDeltaSwingTableIdx_MP_2GA_P_TxPowerTrack_SDIO_8723B[] = {
	0, 0, 1, 2, 2, 3, 3, 4, 5, 5,  6,  6,  7,  7,  8,  8,
	9,  9, 10, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15
};
static u8 gDeltaSwingTableIdx_MP_2GCCKB_N_TxPowerTrack_SDIO_8723B[] = {
	0, 0, 1, 2, 2, 3, 3, 4, 4, 5,  6,  6,  7,  7,  7,  8,
	8,  8,  9,  9,  9, 10, 10, 11, 11, 12, 12, 13, 14, 15
};
static u8 gDeltaSwingTableIdx_MP_2GCCKB_P_TxPowerTrack_SDIO_8723B[] = {
	0, 0, 1, 2, 2, 2, 3, 3, 3, 4,  5,  5,  6,  6,  7,  7,
	8,  8,  9,  9,  9, 10, 10, 11, 11, 12, 12, 13, 14, 15
};
static u8 gDeltaSwingTableIdx_MP_2GCCKA_N_TxPowerTrack_SDIO_8723B[] = {
	0, 0, 1, 2, 2, 3, 3, 4, 4, 5,  6,  6,  7,  7,  7,  8,
	8,  8,  9,  9,  9, 10, 10, 11, 11, 12, 12, 13, 14, 15
};
static u8 gDeltaSwingTableIdx_MP_2GCCKA_P_TxPowerTrack_SDIO_8723B[] = {
	0, 0, 1, 2, 2, 2, 3, 3, 3, 4,  5,  5,  6,  6,  7,  7,
	8,  8,  9,  9,  9, 10, 10, 11, 11, 12, 12, 13, 14, 15
};

void ODM_ReadAndConfig_MP_8723B_TxPowerTrack_SDIO(PDM_ODM_T pDM_Odm)
{
	PODM_RF_CAL_T pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_LOUD,
		("===> ODM_ReadAndConfig_MP_MP_8723B\n")
	);


	memcpy(
		pRFCalibrateInfo->DeltaSwingTableIdx_2GA_P,
		gDeltaSwingTableIdx_MP_2GA_P_TxPowerTrack_SDIO_8723B,
		DELTA_SWINGIDX_SIZE
	);
	memcpy(
		pRFCalibrateInfo->DeltaSwingTableIdx_2GA_N,
		gDeltaSwingTableIdx_MP_2GA_N_TxPowerTrack_SDIO_8723B,
		DELTA_SWINGIDX_SIZE
	);
	memcpy(
		pRFCalibrateInfo->DeltaSwingTableIdx_2GB_P,
		gDeltaSwingTableIdx_MP_2GB_P_TxPowerTrack_SDIO_8723B,
		DELTA_SWINGIDX_SIZE
	);
	memcpy(
		pRFCalibrateInfo->DeltaSwingTableIdx_2GB_N,
		gDeltaSwingTableIdx_MP_2GB_N_TxPowerTrack_SDIO_8723B,
		DELTA_SWINGIDX_SIZE
	);

	memcpy(
		pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_P,
		gDeltaSwingTableIdx_MP_2GCCKA_P_TxPowerTrack_SDIO_8723B,
		DELTA_SWINGIDX_SIZE
	);
	memcpy(
		pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_N,
		gDeltaSwingTableIdx_MP_2GCCKA_N_TxPowerTrack_SDIO_8723B,
		DELTA_SWINGIDX_SIZE
	);
	memcpy(
		pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_P,
		gDeltaSwingTableIdx_MP_2GCCKB_P_TxPowerTrack_SDIO_8723B,
		DELTA_SWINGIDX_SIZE
	);
	memcpy(
		pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_N,
		gDeltaSwingTableIdx_MP_2GCCKB_N_TxPowerTrack_SDIO_8723B,
		DELTA_SWINGIDX_SIZE
	);

	memcpy(
		pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P,
		gDeltaSwingTableIdx_MP_5GA_P_TxPowerTrack_SDIO_8723B,
		DELTA_SWINGIDX_SIZE*3
	);
	memcpy(
		pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N,
		gDeltaSwingTableIdx_MP_5GA_N_TxPowerTrack_SDIO_8723B,
		DELTA_SWINGIDX_SIZE*3
	);
	memcpy(
		pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P,
		gDeltaSwingTableIdx_MP_5GB_P_TxPowerTrack_SDIO_8723B,
		DELTA_SWINGIDX_SIZE*3
	);
	memcpy(
		pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N,
		gDeltaSwingTableIdx_MP_5GB_N_TxPowerTrack_SDIO_8723B,
		DELTA_SWINGIDX_SIZE*3
	);
}

/******************************************************************************
*                           TXPWR_LMT.TXT
******************************************************************************/

static u8 *Array_MP_8723B_TXPWR_LMT[] = {
	"FCC", "2.4G", "20M", "CCK", "1T", "01", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "01", "32",
	"MKK", "2.4G", "20M", "CCK", "1T", "01", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "02", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "02", "32",
	"MKK", "2.4G", "20M", "CCK", "1T", "02", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "03", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "03", "32",
	"MKK", "2.4G", "20M", "CCK", "1T", "03", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "04", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "04", "32",
	"MKK", "2.4G", "20M", "CCK", "1T", "04", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "05", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "05", "32",
	"MKK", "2.4G", "20M", "CCK", "1T", "05", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "06", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "06", "32",
	"MKK", "2.4G", "20M", "CCK", "1T", "06", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "07", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "07", "32",
	"MKK", "2.4G", "20M", "CCK", "1T", "07", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "08", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "08", "32",
	"MKK", "2.4G", "20M", "CCK", "1T", "08", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "09", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "09", "32",
	"MKK", "2.4G", "20M", "CCK", "1T", "09", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "10", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "10", "32",
	"MKK", "2.4G", "20M", "CCK", "1T", "10", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "11", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "11", "32",
	"MKK", "2.4G", "20M", "CCK", "1T", "11", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "12", "63",
	"ETSI", "2.4G", "20M", "CCK", "1T", "12", "32",
	"MKK", "2.4G", "20M", "CCK", "1T", "12", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "13", "63",
	"ETSI", "2.4G", "20M", "CCK", "1T", "13", "32",
	"MKK", "2.4G", "20M", "CCK", "1T", "13", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "14", "63",
	"ETSI", "2.4G", "20M", "CCK", "1T", "14", "63",
	"MKK", "2.4G", "20M", "CCK", "1T", "14", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "01", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "01", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "01", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "02", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "02", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "02", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "03", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "03", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "03", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "04", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "04", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "04", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "05", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "05", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "05", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "06", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "06", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "06", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "07", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "07", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "07", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "08", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "08", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "08", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "09", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "09", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "09", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "10", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "10", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "10", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "11", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "11", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "11", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "12", "63",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "12", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "12", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "13", "63",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "13", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "13", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "14", "63",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "14", "63",
	"MKK", "2.4G", "20M", "OFDM", "1T", "14", "63",
	"FCC", "2.4G", "20M", "HT", "1T", "01", "26",
	"ETSI", "2.4G", "20M", "HT", "1T", "01", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "01", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "02", "26",
	"ETSI", "2.4G", "20M", "HT", "1T", "02", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "02", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "03", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "03", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "03", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "04", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "04", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "04", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "05", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "05", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "05", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "06", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "06", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "06", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "07", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "07", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "07", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "08", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "08", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "08", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "09", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "09", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "09", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "10", "26",
	"ETSI", "2.4G", "20M", "HT", "1T", "10", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "10", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "11", "26",
	"ETSI", "2.4G", "20M", "HT", "1T", "11", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "11", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "12", "63",
	"ETSI", "2.4G", "20M", "HT", "1T", "12", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "12", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "13", "63",
	"ETSI", "2.4G", "20M", "HT", "1T", "13", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "13", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "14", "63",
	"ETSI", "2.4G", "20M", "HT", "1T", "14", "63",
	"MKK", "2.4G", "20M", "HT", "1T", "14", "63",
	"FCC", "2.4G", "20M", "HT", "2T", "01", "30",
	"ETSI", "2.4G", "20M", "HT", "2T", "01", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "01", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "02", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "02", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "02", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "03", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "03", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "03", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "04", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "04", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "04", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "05", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "05", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "05", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "06", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "06", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "06", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "07", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "07", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "07", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "08", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "08", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "08", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "09", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "09", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "09", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "10", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "10", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "10", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "11", "30",
	"ETSI", "2.4G", "20M", "HT", "2T", "11", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "11", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "12", "63",
	"ETSI", "2.4G", "20M", "HT", "2T", "12", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "12", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "13", "63",
	"ETSI", "2.4G", "20M", "HT", "2T", "13", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "13", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "14", "63",
	"ETSI", "2.4G", "20M", "HT", "2T", "14", "63",
	"MKK", "2.4G", "20M", "HT", "2T", "14", "63",
	"FCC", "2.4G", "40M", "HT", "1T", "01", "63",
	"ETSI", "2.4G", "40M", "HT", "1T", "01", "63",
	"MKK", "2.4G", "40M", "HT", "1T", "01", "63",
	"FCC", "2.4G", "40M", "HT", "1T", "02", "63",
	"ETSI", "2.4G", "40M", "HT", "1T", "02", "63",
	"MKK", "2.4G", "40M", "HT", "1T", "02", "63",
	"FCC", "2.4G", "40M", "HT", "1T", "03", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "03", "32",
	"MKK", "2.4G", "40M", "HT", "1T", "03", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "04", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "04", "32",
	"MKK", "2.4G", "40M", "HT", "1T", "04", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "05", "32",
	"ETSI", "2.4G", "40M", "HT", "1T", "05", "32",
	"MKK", "2.4G", "40M", "HT", "1T", "05", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "06", "32",
	"ETSI", "2.4G", "40M", "HT", "1T", "06", "32",
	"MKK", "2.4G", "40M", "HT", "1T", "06", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "07", "32",
	"ETSI", "2.4G", "40M", "HT", "1T", "07", "32",
	"MKK", "2.4G", "40M", "HT", "1T", "07", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "08", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "08", "32",
	"MKK", "2.4G", "40M", "HT", "1T", "08", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "09", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "09", "32",
	"MKK", "2.4G", "40M", "HT", "1T", "09", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "10", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "10", "32",
	"MKK", "2.4G", "40M", "HT", "1T", "10", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "11", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "11", "32",
	"MKK", "2.4G", "40M", "HT", "1T", "11", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "12", "63",
	"ETSI", "2.4G", "40M", "HT", "1T", "12", "32",
	"MKK", "2.4G", "40M", "HT", "1T", "12", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "13", "63",
	"ETSI", "2.4G", "40M", "HT", "1T", "13", "32",
	"MKK", "2.4G", "40M", "HT", "1T", "13", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "14", "63",
	"ETSI", "2.4G", "40M", "HT", "1T", "14", "63",
	"MKK", "2.4G", "40M", "HT", "1T", "14", "63",
	"FCC", "2.4G", "40M", "HT", "2T", "01", "63",
	"ETSI", "2.4G", "40M", "HT", "2T", "01", "63",
	"MKK", "2.4G", "40M", "HT", "2T", "01", "63",
	"FCC", "2.4G", "40M", "HT", "2T", "02", "63",
	"ETSI", "2.4G", "40M", "HT", "2T", "02", "63",
	"MKK", "2.4G", "40M", "HT", "2T", "02", "63",
	"FCC", "2.4G", "40M", "HT", "2T", "03", "30",
	"ETSI", "2.4G", "40M", "HT", "2T", "03", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "03", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "04", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "04", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "04", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "05", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "05", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "05", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "06", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "06", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "06", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "07", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "07", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "07", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "08", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "08", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "08", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "09", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "09", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "09", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "10", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "10", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "10", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "11", "30",
	"ETSI", "2.4G", "40M", "HT", "2T", "11", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "11", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "12", "63",
	"ETSI", "2.4G", "40M", "HT", "2T", "12", "32",
	"MKK", "2.4G", "40M", "HT", "2T", "12", "32",
	"FCC", "2.4G", "40M", "HT", "2T", "13", "63",
	"ETSI", "2.4G", "40M", "HT", "2T", "13", "32",
	"MKK", "2.4G", "40M", "HT", "2T", "13", "32",
	"FCC", "2.4G", "40M", "HT", "2T", "14", "63",
	"ETSI", "2.4G", "40M", "HT", "2T", "14", "63",
	"MKK", "2.4G", "40M", "HT", "2T", "14", "63"
};

void ODM_ReadAndConfig_MP_8723B_TXPWR_LMT(PDM_ODM_T pDM_Odm)
{
	u32 i = 0;
	u8 **Array = Array_MP_8723B_TXPWR_LMT;

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_LOUD,
		("===> ODM_ReadAndConfig_MP_8723B_TXPWR_LMT\n")
	);

	for (i = 0; i < ARRAY_SIZE(Array_MP_8723B_TXPWR_LMT); i += 7) {
		u8 *regulation = Array[i];
		u8 *band = Array[i+1];
		u8 *bandwidth = Array[i+2];
		u8 *rate = Array[i+3];
		u8 *rfPath = Array[i+4];
		u8 *chnl = Array[i+5];
		u8 *val = Array[i+6];

		odm_ConfigBB_TXPWR_LMT_8723B(
			pDM_Odm,
			regulation,
			band,
			bandwidth,
			rate,
			rfPath,
			chnl,
			val
		);
	}
}
