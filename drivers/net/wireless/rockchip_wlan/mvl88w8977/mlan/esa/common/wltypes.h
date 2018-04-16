/** @file  wltypes.h
 *
 *  @brief Basic types common to all the modules must be defined in this file.
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
#if !defined(WLTYPES_H__)
#define WLTYPES_H__
/*
*                Copyright 2003, Marvell Semiconductor, Inc.
* This code contains confidential information of Marvell Semiconductor, Inc.
* No rights are granted herein under any patent, mask work right or copyright
* of Marvell or any third party.
* Marvell reserves the right at its sole discretion to request that this code
* be immediately returned to Marvell. This code is provided "as is".
* Marvell makes no warranties, express, implied or otherwise, regarding its
* accuracy, completeness or performance.
*/

/*!
 * \file    wltypes.h
 * \brief   Basic types common to all the modules must be defined in this file.
 *
*/
 /** @defgroup DataTypes Data Types used in SDK
 *  Functions exported by wltypes.h
 *  @{
 */
/*! Create type names for native C types for portability reasons */
typedef unsigned long long UINT64;	//!< 64 bit unsigned
typedef signed long long SINT64;	//!< 64 bit signed
typedef unsigned int UINT32;	//!< 32 bit unsigned
typedef signed int SINT32;	//!< 32 bit signed
typedef unsigned short UINT16;	//!< 16 bit unsigned
typedef signed short SINT16;	//!< 16 bit signed
typedef unsigned char UINT8;	//!< 8 bit unsigned
typedef signed char SINT8;	//!< 8 bit signed

typedef unsigned long long uint64;	//!< 64 bit unsigned
typedef signed long long sint64;	//!< 64 bit signed
typedef unsigned int uint32;	//!< 32 bit unsigned

typedef signed int sint32;	//!< 32 bit signed
typedef unsigned short uint16;	//!< 16 bit unsigned
typedef signed short sint16;	//!< 16 bit signed
typedef unsigned char uint8;	//!< 8 bit unsigned
typedef signed char sint8;	//!< 8 bit signed
typedef signed char int8;	//!< 8 bit signed
typedef int BOOLEAN;		//!< boolean
typedef int Boolean;		//!< boolean
typedef signed int BIT_FIELD;	//!< bit field

typedef unsigned int UINT;	//!< unsigned integer

//From dwc_os.h
#define size_t uint32

extern SINT8 SINT8_minus_SINT8(SINT8 x, SINT8 y);
extern SINT8 SINT8_plus_SINT8(SINT8 x, SINT8 y);

#ifdef __GNUC__
/** Structure packing begins */
#define MLAN_PACK_START
/** Structure packeing end */
#define MLAN_PACK_END  __attribute__((packed))
#else /* !__GNUC__ */
#ifdef PRAGMA_PACK
/** Structure packing begins */
#define MLAN_PACK_START
/** Structure packeing end */
#define MLAN_PACK_END
#else /* !PRAGMA_PACK */
/** Structure packing begins */
#define MLAN_PACK_START   __packed
/** Structure packing end */
#define MLAN_PACK_END
#endif /* PRAGMA_PACK */
#endif /* __GNUC__ */

#ifndef INLINE
#ifdef __GNUC__
/** inline directive */
#define	INLINE	inline
#else
/** inline directive */
#define	INLINE	__inline
#endif
#endif

typedef struct _REFCLK_VAL8_REG7F {
	UINT8 refClk;
	UINT8 val;
} REFCLK_VAL8_REG7F;

#define REG8_TERMINATOR (0xFF)
typedef struct _ADDR8_VAL8_REG {
	UINT8 reg;
	UINT8 val;
} ADDR8_VAL8_REG;

#define REG16_TERMINATOR (0xFFFF)
typedef struct _ADDR16_VAL8_REG {
	UINT16 reg;
	UINT8 val;
} ADDR16_VAL8_REG;

#define REG32_TERMINATOR (0xFFFFFFFF)
typedef struct _ADDR32_VAL32_REG {
	UINT32 reg;
	UINT32 val;
} ADDR32_VAL32_REG;

#if defined(BBP_9BIT_ADDR)
#define BBP_TERMINATOR REG16_TERMINATOR
#else
#define BBP_TERMINATOR REG8_TERMINATOR
#endif

/*! Generic status code */
#define WL_STATUS_OK         0	//!< ok
#define WL_STATUS_ERR       -1	//!< error
#define WL_STATUS_BAD_PARAM -2	//!< bad parameter

/* Some BT files require access to wltypes.h file which result in duplicate definition of FALSE & TRUE
 * undef TRUE and FALSE to avoid these warnings */
#undef FALSE
#undef TRUE
#undef INLINE

/*! BOOLEAN values */
#define FALSE 0			//!< False
#define TRUE  1			//!< True

/**
*** @brief Enumeration of Status type
**/
typedef enum {
	SUCCESS,		//!< 0
	FAIL			//!< 1
} Status_e;

typedef Status_e WL_STATUS;

/*! Value for NULL pointer */
#undef NULL
#define NULL ((void *)0)	//!< null
#if 0
#ifndef LINT
#define MLAN_PACK_START   __packed	//!< packed structure
#define MLAN_PACK_END		//!< end of packed structure

#define ALIGNED_START(x) __align(x)	//!< allignment macro
#define ALIGNED_END(x)		//!< allignment end
#else
#define MLAN_PACK_START
#define MLAN_PACK_END

#define ALIGNED_START(x)
#define ALIGNED_END(x)
#endif
#endif
#define INLINE

#define BIT0    (0x00000001 << 0)
#define BIT1    (0x00000001 << 1)
#define BIT2    (0x00000001 << 2)
#define BIT3    (0x00000001 << 3)
#define BIT4    (0x00000001 << 4)
#define BIT5    (0x00000001 << 5)
#define BIT6    (0x00000001 << 6)
#define BIT7    (0x00000001 << 7)
#define BIT8    (0x00000001 << 8)
#define BIT9    (0x00000001 << 9)
#define BIT10   (0x00000001 << 10)
#define BIT11   (0x00000001 << 11)
#define BIT12   (0x00000001 << 12)
#define BIT13   (0x00000001 << 13)
#define BIT14   (0x00000001 << 14)
#define BIT15   (0x00000001 << 15)
#define BIT16   (0x00000001 << 16)
#define BIT17   (0x00000001 << 17)
#define BIT18   (0x00000001 << 18)
#define BIT19   (0x00000001 << 19)
#define BIT20   (0x00000001 << 20)
#define BIT21   (0x00000001 << 21)
#define BIT22   (0x00000001 << 22)
#define BIT23   (0x00000001 << 23)
#define BIT24   (0x00000001 << 24)
#define BIT25   (0x00000001 << 25)
#define BIT26   (0x00000001 << 26)
#define BIT27   (0x00000001 << 27)
#define BIT28   (0x00000001 << 28)
#define BIT29   (0x00000001 << 29)
#define BIT30   (0x00000001 << 30)
#define BIT31   (0x00000001UL << 31)

// VOLT_RESOLUTION_50mV
#define VOLT_0_5    10		// 10x50mV = 5x100mV
#define VOLT_0_6    12		// 12x50mV = 6x100mV
#define VOLT_0_7    14		// 14x50mV = 7x100mV
#define VOLT_0_7_5  15		// 15x50mV = 7.5x100mV
#define VOLT_0_8    16		// 16x50mV = 8x100mV
#define VOLT_0_8_5  17		// 17x50mV = 8.5x100mV
#define VOLT_0_9    18		// 18x50mV = 9x100mV
#define VOLT_1_0    20		// 20x50mV = 10x100mV
#define VOLT_1_0_5  21		// 21x50mV
#define VOLT_1_1    22		// 22x50mV = 11x100mV
#define VOLT_1_1_5  23		// 23x50mV
#define VOLT_1_2    24		// 24x50mV = 12x100mV
#define VOLT_1_2_5  25		// 25x50mV
#define VOLT_1_3    26		// 26x50mV = 13x100mV
#define VOLT_1_4    28		// 28x50mV = 14x100mV
#define VOLT_1_5    30		// 30x50mV = 15x100mV
#define VOLT_1_5_5  31		// 31x50mV = 15.5x100mV
#define VOLT_1_6    32		// 32x50mV = 16x100mV
#define VOLT_1_6_5  33		// 33x50mV = 16.5x100mV
#define VOLT_1_7    34		// 34x50mV = 17x100mV
#define VOLT_1_7_5  35		// 35x50mV = 17.5x100mV
#define VOLT_1_8    36		// 36x50mV = 18x100mV
#define VOLT_1_8_5  37		// 37x50mV = 18.5x100mV
#define VOLT_1_9    38		// 38x50mV = 19x100mV
#define VOLT_1_9_5  39		// 39x50mV = 19.5x100mV
#define VOLT_2_0    40		// 40x50mV = 20x100mV
#define VOLT_2_1    42		// 42x50mV = 21x100mV
#define VOLT_2_2    44		// 44x50mV = 22x100mV
#define VOLT_2_3    46		// 46x50mV = 23x100mV
#define VOLT_2_4    48		// 48x50mV = 24x100mV
#define VOLT_2_5    50		// 50x50mV = 25x100mV
#define VOLT_2_6    52		// 52x50mV = 26x100mV
#define VOLT_2_7    54		// 54x50mV = 27x100mV
#define VOLT_2_8    56		// 56x50mV = 28x100mV
#define VOLT_2_9    58		// 58x50mV = 29x100mV
#define VOLT_3_0    60		// 60x50mV = 30x100mV
#define VOLT_3_1    62		// 62x50mV = 31x100mV
#define VOLT_3_2    64		// 64x50mV = 32x100mV
#define VOLT_3_3    66		// 66x50mV = 33x100mV
#define VOLT_3_4    68		// 68x50mV = 34x100mV
#define VOLT_3_5    70		// 70x50mV = 35x100mV
#define VOLT_3_6    72		// 72x50mV = 36x100mV

typedef enum {
	OFF = 0,
	ON = 1,
	UNKOWN = 0xFF
} ON_OFF_UNKOWN_e;

typedef unsigned char MRVL_RATEID;

#if defined(DOT11AC) && defined(STREAM_2x2)
typedef unsigned long long MRVL_RATEID_BITMAP_UNIT;
typedef struct {
	unsigned long long bitmap[2];
} MRVL_RATEID_BITMAP;
#define MRVL_RATEID_BIT(rateId)     (1ULL << (rateId))
#else
#if defined(STREAM_2x2) || defined(DOT11AC)
typedef unsigned long long MRVL_RATEID_BITMAP_UNIT;
typedef unsigned long long MRVL_RATEID_BITMAP;
#define MRVL_RATEID_BIT(rateId)     (1ULL << (rateId))
#else
typedef unsigned int MRVL_RATEID_BITMAP_UNIT;
typedef unsigned int MRVL_RATEID_BITMAP;
#define MRVL_RATEID_BIT(rateId)     (1UL << (rateId))
#endif
#endif

#define NUM_RATE_BITMAP_UNIT  (sizeof(MRVL_RATEID_BITMAP_UNIT)*8)

enum {
	RATEID_DBPSK1Mbps,	//(0)
	RATEID_DQPSK2Mbps,	//(1)
	RATEID_CCK5_5Mbps,	//(2)
	RATEID_CCK11Mbps,	//(3)
	RATEID_CCK22Mbps,	//(4)
	RATEID_OFDM6Mbps,	//(5)
	RATEID_OFDM9Mbps,	//(6)
	RATEID_OFDM12Mbps,	//(7)
	RATEID_OFDM18Mbps,	//(8)
	RATEID_OFDM24Mbps,	//(9)
	RATEID_OFDM36Mbps,	//(10)
	RATEID_OFDM48Mbps,	//(11)
	RATEID_OFDM54Mbps,	//(12)
	RATEID_OFDM72Mbps,	//(13)

	RATEID_MCS0_6d5Mbps,	//(14)    //RATEID_OFDM72Mbps + 1
	RATEID_MCS1_13Mbps,	//(15)
	RATEID_MCS2_19d5Mbps,	//(16)
	RATEID_MCS3_26Mbps,	//(17)
	RATEID_MCS4_39Mbps,	//(18)
	RATEID_MCS5_52Mbps,	//(19)
	RATEID_MCS6_58d5Mbps,	//(20)
	RATEID_MCS7_65Mbps,	//(21)

#ifdef STREAM_2x2
	RATEID_MCS8_13Mbps,	//(22)
	RATEID_MCS9_26Mbps,	//(23)
	RATEID_MCS10_39Mbps,	//(24)
	RATEID_MCS11_52Mbps,	//(25)
	RATEID_MCS12_78Mbps,	//(26)
	RATEID_MCS13_104Mbps,	//(27)
	RATEID_MCS14_117Mbps,	//(28)
	RATEID_MCS15_130Mbps,	//(29)
#endif

	RATEID_MCS32BW40_6Mbps,	//(30), (22)
	RATEID_MCS0BW40_13d5Mbps,	//(31), (23)
	RATEID_MCS1BW40_27Mbps,	//(32), (24)
	RATEID_MCS2BW40_40d5Mbps,	//(33), (25)
	RATEID_MCS3BW40_54Mbps,	//(34), (26)
	RATEID_MCS4BW40_81Mbps,	//(35), (27)
	RATEID_MCS5BW40_108Mbps,	//(36), (28)
	RATEID_MCS6BW40_121d5Mbps,	//(37), (29)
	RATEID_MCS7BW40_135Mbps,	//(38), (30)

#ifdef STREAM_2x2
	RATEID_MCS8BW40_27Mbps,	//(39)
	RATEID_MCS9BW40_54Mbps,	//(40)
	RATEID_MCS10BW40_81Mbps,	//(41)
	RATEID_MCS11BW40_108Mbps,	//(42)
	RATEID_MCS12BW40_162Mbps,	//(43)
	RATEID_MCS13BW40_216Mbps,	//(44)
	RATEID_MCS14BW40_243Mbps,	//(45)
	RATEID_MCS15BW40_270Mbps,	//(46)
#endif

#if defined(DOT11AC)
	RATEID_VHT_MCS0_1SS_BW20,	//(47), (31) //6.5  Mbps
	RATEID_VHT_MCS1_1SS_BW20,	//(48), (32) //13   Mbps
	RATEID_VHT_MCS2_1SS_BW20,	//(49), (33) //19.5 Mbps
	RATEID_VHT_MCS3_1SS_BW20,	//(50), (34) //26   Mbps
	RATEID_VHT_MCS4_1SS_BW20,	//(51), (35) //39   Mbps
	RATEID_VHT_MCS5_1SS_BW20,	//(52), (36) //52   Mbps
	RATEID_VHT_MCS6_1SS_BW20,	//(53), (37) //58.5 Mbps
	RATEID_VHT_MCS7_1SS_BW20,	//(54), (38) //65   Mbps
	RATEID_VHT_MCS8_1SS_BW20,	//(55), (39) //78   Mbps
	RATEID_VHT_MCS9_1SS_BW20,	//(56), (40) //86.7 Mbps(INVALID)

#ifdef STREAM_2x2
	RATEID_VHT_MCS0_2SS_BW20,	//(57)       //13    Mbps
	RATEID_VHT_MCS1_2SS_BW20,	//(58)       //26    Mbps
	RATEID_VHT_MCS2_2SS_BW20,	//(59)       //39    Mbps
	RATEID_VHT_MCS3_2SS_BW20,	//(60)       //52    Mbps
	RATEID_VHT_MCS4_2SS_BW20,	//(61)       //78    Mbps
	RATEID_VHT_MCS5_2SS_BW20,	//(62)       //104   Mbps
	RATEID_VHT_MCS6_2SS_BW20,	//(63)       //117   Mbps
	RATEID_VHT_MCS7_2SS_BW20,	//(64)       //130   Mbps
	RATEID_VHT_MCS8_2SS_BW20,	//(65)       //156   Mbps
	RATEID_VHT_MCS9_2SS_BW20,	//(66)       //173.3 Mbps(INVALID)
#endif

	RATEID_VHT_MCS0_1SS_BW40,	//(67), (41) //13.5  Mbps
	RATEID_VHT_MCS1_1SS_BW40,	//(68), (42) //27    Mbps
	RATEID_VHT_MCS2_1SS_BW40,	//(69), (43) //40.5  Mbps
	RATEID_VHT_MCS3_1SS_BW40,	//(70), (44) //54    Mbps
	RATEID_VHT_MCS4_1SS_BW40,	//(71), (45) //81    Mbps
	RATEID_VHT_MCS5_1SS_BW40,	//(72), (46) //108   Mbps
	RATEID_VHT_MCS6_1SS_BW40,	//(73), (47) //121.5 Mbps
	RATEID_VHT_MCS7_1SS_BW40,	//(74), (48) //135   Mbps
	RATEID_VHT_MCS8_1SS_BW40,	//(75), (49) //162   Mbps
	RATEID_VHT_MCS9_1SS_BW40,	//(76), (50) //180   Mbps

#ifdef STREAM_2x2
	RATEID_VHT_MCS0_2SS_BW40,	//(77)       //27  Mbps
	RATEID_VHT_MCS1_2SS_BW40,	//(78)       //54  Mbps
	RATEID_VHT_MCS2_2SS_BW40,	//(79)       //81  Mbps
	RATEID_VHT_MCS3_2SS_BW40,	//(80)       //108 Mbps
	RATEID_VHT_MCS4_2SS_BW40,	//(81)       //162 Mbps
	RATEID_VHT_MCS5_2SS_BW40,	//(82)       //216 Mbps
	RATEID_VHT_MCS6_2SS_BW40,	//(83)       //243 Mbps
	RATEID_VHT_MCS7_2SS_BW40,	//(84)       //270 Mbps
	RATEID_VHT_MCS8_2SS_BW40,	//(85)       //324 Mbps
	RATEID_VHT_MCS9_2SS_BW40,	//(86)       //360 Mbps
#endif

	RATEID_VHT_MCS0_1SS_BW80,	//(87), (51) //29.3  Mbps
	RATEID_VHT_MCS1_1SS_BW80,	//(88), (52) //58.5  Mbps
	RATEID_VHT_MCS2_1SS_BW80,	//(89), (53) //87.8  Mbps
	RATEID_VHT_MCS3_1SS_BW80,	//(90), (54) //117   Mbps
	RATEID_VHT_MCS4_1SS_BW80,	//(91), (55) //175.5 Mbps
	RATEID_VHT_MCS5_1SS_BW80,	//(92), (56) //234   Mbps
	RATEID_VHT_MCS6_1SS_BW80,	//(93), (57) //263.3 Mbps
	RATEID_VHT_MCS7_1SS_BW80,	//(94), (58) //292.5 Mbps
	RATEID_VHT_MCS8_1SS_BW80,	//(95), (59) //351   Mbps
	RATEID_VHT_MCS9_1SS_BW80,	//(96), (60) //390   Mbps

#ifdef STREAM_2x2
	RATEID_VHT_MCS0_2SS_BW80,	//(97)       //58.5  Mbps
	RATEID_VHT_MCS1_2SS_BW80,	//(98)       //117   Mbps
	RATEID_VHT_MCS2_2SS_BW80,	//(99)       //175   Mbps
	RATEID_VHT_MCS3_2SS_BW80,	//(100)      //234   Mbps
	RATEID_VHT_MCS4_2SS_BW80,	//(101)      //351   Mbps
	RATEID_VHT_MCS5_2SS_BW80,	//(102)      //468   Mbps
	RATEID_VHT_MCS6_2SS_BW80,	//(103)      //526.5 Mbps
	RATEID_VHT_MCS7_2SS_BW80,	//(104)      //585   Mbps
	RATEID_VHT_MCS8_2SS_BW80,	//(105)      //702   Mbps
	RATEID_VHT_MCS9_2SS_BW80,	//(106)      //780   Mbps
#endif
#endif				//DOT11AC

	RATEID_AUTO = 0xFE,
	RATEID_UNKNOWN = 0xFF
};

//MAX RATE DEFINITION
#define RATEID_DSSSMAX      RATEID_DQPSK2Mbps

#define RATEID_CCKMAX       RATEID_CCK11Mbps

#define RATEID_OFDMMAX      RATEID_OFDM54Mbps
#define RATEID_OFDMMIN      RATEID_OFDM6Mbps

#define RATEID_HTBW20MIN    RATEID_MCS0_6d5Mbps
#ifdef STREAM_2x2
#define RATEID_HTBW20MAX    RATEID_MCS15_130Mbps
#else
#define RATEID_HTBW20MAX    RATEID_MCS7_65Mbps
#endif

#define RATEID_HTBW40MIN    RATEID_MCS32BW40_6Mbps
#ifdef STREAM_2x2
#define RATEID_HTBW40MAX    RATEID_MCS15BW40_270Mbps
#else
#define RATEID_HTBW40MAX    RATEID_MCS7BW40_135Mbps
#endif

#define RATEID_HTMIN        RATEID_HTBW20MIN
#define RATEID_HTMAX        RATEID_HTBW40MAX

#if defined(DOT11AC)
#define RATEID_VHTBW20MIN  RATEID_VHT_MCS0_1SS_BW20
#define RATEID_VHTBW40MIN  RATEID_VHT_MCS0_1SS_BW40
#define RATEID_VHTBW80MIN  RATEID_VHT_MCS0_1SS_BW80

#define RATEID_VHTMIN      RATEID_VHT_MCS0_1SS_BW20
#ifdef STREAM_2x2
#define RATEID_VHTBW20MAX  RATEID_VHT_MCS9_2SS_BW20
#define RATEID_VHTBW40MAX  RATEID_VHT_MCS9_2SS_BW40
#define RATEID_VHTMAX      RATEID_VHT_MCS9_2SS_BW80
#else
#define RATEID_VHTBW20MAX  RATEID_VHT_MCS9_1SS_BW20
#define RATEID_VHTBW40MAX  RATEID_VHT_MCS9_1SS_BW40
#define RATEID_VHTMAX      RATEID_VHT_MCS9_1SS_BW80
#endif
#define RATEID_VHTBW80MAX  RATEID_VHTMAX

/* 160 is not supported yet; define them correctly when supported */
#define RATEID_VHTBW160MIN 0xFF
#define RATEID_VHTBW160MAX 0xFF

#endif //DOT11AC

#define MLME_SUCCESS     0
#define MLME_INPROCESS   1
#define MLME_FAILURE    -1

#define RATEID_9_6_MASK     (((1<<(1+RATEID_OFDM9Mbps-RATEID_OFDM6Mbps))-1) \
                                                    << RATEID_OFDM6Mbps)

#define RATEID_12_6_MASK   (((1<<(1+RATEID_OFDM12Mbps-RATEID_OFDM6Mbps))-1) \
                                                    << RATEID_OFDM6Mbps)

#define RATEID_18_6_MASK   (((1<<(1+RATEID_OFDM18Mbps-RATEID_OFDM6Mbps))-1) \
                                                    << RATEID_OFDM6Mbps)

#define RATEID_24_6_MASK   (((1<<(1+RATEID_OFDM24Mbps-RATEID_OFDM6Mbps))-1) \
                                                    << RATEID_OFDM6Mbps)

#define RATEID_36_6_MASK   (((1<<(1+RATEID_OFDM36Mbps-RATEID_OFDM6Mbps))-1) \
                                                    << RATEID_OFDM6Mbps)

#define RATEID_48_6_MASK   (((1<<(1+RATEID_OFDM48Mbps-RATEID_OFDM6Mbps))-1) \
                                                    << RATEID_OFDM6Mbps)

#define RATEID_54_6_MASK   (((1<<(1+RATEID_OFDM54Mbps-RATEID_OFDM6Mbps))-1) \
                                                    << RATEID_OFDM6Mbps)

#define RATEID_CCK_MASK      ( (1 << RATEID_DBPSK1Mbps)  \
                             | (1 << RATEID_DQPSK2Mbps)  \
                             | (1 << RATEID_CCK5_5Mbps)  \
                             | (1 << RATEID_CCK11Mbps) )

#define HT_RATE_MASK_MCS0_7  ( (1ULL << RATEID_MCS0_6d5Mbps)  \
                             | (1ULL << RATEID_MCS1_13Mbps)   \
                             | (1ULL << RATEID_MCS2_19d5Mbps) \
                             | (1ULL << RATEID_MCS3_26Mbps)   \
                             | (1ULL << RATEID_MCS4_39Mbps)   \
                             | (1ULL << RATEID_MCS5_52Mbps)   \
                             | (1ULL << RATEID_MCS6_58d5Mbps) \
                             | (1ULL << RATEID_MCS7_65Mbps) )
#ifdef STREAM_2x2
#define HT_RATE_MASK_MCS8_15 ( (1ULL << RATEID_MCS8_13Mbps) \
                             | (1ULL << RATEID_MCS9_26Mbps) \
                             | (1ULL << RATEID_MCS10_39Mbps) \
                             | (1ULL << RATEID_MCS11_52Mbps) \
                             | (1ULL << RATEID_MCS12_78Mbps) \
                             | (1ULL << RATEID_MCS13_104Mbps) \
                             | (1ULL << RATEID_MCS14_117Mbps) \
                             | (1ULL << RATEID_MCS15_130Mbps) )

#else //1x1
#define HT_RATE_MASK_MCS8_15  (0)
#endif

#define HT_RATE_MASK_MCS0_7_32_40MHZ  ( (1ULL << RATEID_MCS32BW40_6Mbps)    \
                                      | (1ULL << RATEID_MCS0BW40_13d5Mbps)  \
                                      | (1ULL << RATEID_MCS1BW40_27Mbps)    \
                                      | (1ULL << RATEID_MCS2BW40_40d5Mbps)  \
                                      | (1ULL << RATEID_MCS3BW40_54Mbps)    \
                                      | (1ULL << RATEID_MCS4BW40_81Mbps)    \
                                      | (1ULL << RATEID_MCS5BW40_108Mbps)   \
                                      | (1ULL << RATEID_MCS6BW40_121d5Mbps) \
                                      | (1ULL << RATEID_MCS7BW40_135Mbps) )

#ifdef STREAM_2x2
#define HT_RATE_MASK_MCS8_15_40MHZ    ( (1ULL << RATEID_MCS8BW40_27Mbps)    \
                                      | (1ULL << RATEID_MCS9BW40_54Mbps)    \
                                      | (1ULL << RATEID_MCS10BW40_81Mbps)   \
                                      | (1ULL << RATEID_MCS11BW40_108Mbps)  \
                                      | (1ULL << RATEID_MCS12BW40_162Mbps)  \
                                      | (1ULL << RATEID_MCS13BW40_216Mbps)  \
                                      | (1ULL << RATEID_MCS14BW40_243Mbps)  \
                                      | (1ULL << RATEID_MCS15BW40_270Mbps) )

#else //1x1
#define HT_RATE_MASK_MCS8_15_40MHZ    (0)
#endif

#define RATEID_MCS_MASK      ( HT_RATE_MASK_MCS0_7           \
                             | HT_RATE_MASK_MCS8_15          \
                             | HT_RATE_MASK_MCS0_7_32_40MHZ  \
                             | HT_RATE_MASK_MCS8_15_40MHZ )

#define RATEID_MCSBW40_MASK  ( HT_RATE_MASK_MCS0_7_32_40MHZ  \
                             | HT_RATE_MASK_MCS8_15_40MHZ )

#define RATEID_MCS1x1_MASK   ( HT_RATE_MASK_MCS0_7 \
                             | HT_RATE_MASK_MCS0_7_32_40MHZ)

#define RATEID_MCS2x2_MASK   ( HT_RATE_MASK_MCS8_15 \
                             | HT_RATE_MASK_MCS8_15_40MHZ )

#define IS_DSSS_FRAME(rateid)   (rateid < RATEID_OFDM6Mbps)
#define IS_OFDM_FRAME(rateid)   (rateid >= RATEID_OFDM6Mbps)

typedef unsigned char MRVL_TRPCID;
typedef signed long PWR_in_dBm;

#if 0
#define ASSERT_RATEID(rateid)   {while(rateid > RATEID_MAX);}
#define ASSERT_NonZero(x)       {while(x == 0);}
#else
#define ASSERT_RATEID(rateid)
#define ASSERT_NonZero(x)
#endif

#endif /* _WLTYPES_H_ */
