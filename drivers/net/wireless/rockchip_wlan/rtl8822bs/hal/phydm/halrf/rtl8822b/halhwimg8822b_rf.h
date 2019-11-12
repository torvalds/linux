/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

/*Image2HeaderVersion: R3 1.5.10*/
#if (RTL8822B_SUPPORT == 1)
#ifndef __INC_MP_RF_HW_IMG_8822B_H
#define __INC_MP_RF_HW_IMG_8822B_H

/* Please add following compiler flags definition (#define CONFIG_XXX_DRV_DIS)
 * into driver source code to reduce code size if necessary.
 * #define CONFIG_8822B_DRV_DIS
 * #define CONFIG_8822B_TYPE0_DRV_DIS
 * #define CONFIG_8822B_TYPE1_DRV_DIS
 * #define CONFIG_8822B_TYPE10_DRV_DIS
 * #define CONFIG_8822B_TYPE11_DRV_DIS
 * #define CONFIG_8822B_TYPE12_DRV_DIS
 * #define CONFIG_8822B_TYPE13_DRV_DIS
 * #define CONFIG_8822B_TYPE14_DRV_DIS
 * #define CONFIG_8822B_TYPE15_DRV_DIS
 * #define CONFIG_8822B_TYPE16_DRV_DIS
 * #define CONFIG_8822B_TYPE17_DRV_DIS
 * #define CONFIG_8822B_TYPE18_DRV_DIS
 * #define CONFIG_8822B_TYPE19_DRV_DIS
 * #define CONFIG_8822B_TYPE2_DRV_DIS
 * #define CONFIG_8822B_TYPE3_TYPE5_DRV_DIS
 * #define CONFIG_8822B_TYPE4_DRV_DIS
 * #define CONFIG_8822B_TYPE6_DRV_DIS
 * #define CONFIG_8822B_TYPE7_DRV_DIS
 * #define CONFIG_8822B_TYPE8_DRV_DIS
 * #define CONFIG_8822B_TYPE9_DRV_DIS
 * #define CONFIG_8822B_TYPE3_DRV_DIS
 * #define CONFIG_8822B_TYPE5_DRV_DIS
 */

#define CONFIG_8822B
#ifdef CONFIG_8822B_DRV_DIS
    #undef CONFIG_8822B
#endif

#define CONFIG_8822B_TYPE0
#ifdef CONFIG_8822B_TYPE0_DRV_DIS
    #undef CONFIG_8822B_TYPE0
#endif

#define CONFIG_8822B_TYPE1
#ifdef CONFIG_8822B_TYPE1_DRV_DIS
    #undef CONFIG_8822B_TYPE1
#endif

#define CONFIG_8822B_TYPE10
#ifdef CONFIG_8822B_TYPE10_DRV_DIS
    #undef CONFIG_8822B_TYPE10
#endif

#define CONFIG_8822B_TYPE11
#ifdef CONFIG_8822B_TYPE11_DRV_DIS
    #undef CONFIG_8822B_TYPE11
#endif

#define CONFIG_8822B_TYPE12
#ifdef CONFIG_8822B_TYPE12_DRV_DIS
    #undef CONFIG_8822B_TYPE12
#endif

#define CONFIG_8822B_TYPE13
#ifdef CONFIG_8822B_TYPE13_DRV_DIS
    #undef CONFIG_8822B_TYPE13
#endif

#define CONFIG_8822B_TYPE14
#ifdef CONFIG_8822B_TYPE14_DRV_DIS
    #undef CONFIG_8822B_TYPE14
#endif

#define CONFIG_8822B_TYPE15
#ifdef CONFIG_8822B_TYPE15_DRV_DIS
    #undef CONFIG_8822B_TYPE15
#endif

#define CONFIG_8822B_TYPE16
#ifdef CONFIG_8822B_TYPE16_DRV_DIS
    #undef CONFIG_8822B_TYPE16
#endif

#define CONFIG_8822B_TYPE17
#ifdef CONFIG_8822B_TYPE17_DRV_DIS
    #undef CONFIG_8822B_TYPE17
#endif

#define CONFIG_8822B_TYPE18
#ifdef CONFIG_8822B_TYPE18_DRV_DIS
    #undef CONFIG_8822B_TYPE18
#endif

#define CONFIG_8822B_TYPE19
#ifdef CONFIG_8822B_TYPE19_DRV_DIS
    #undef CONFIG_8822B_TYPE19
#endif

#define CONFIG_8822B_TYPE2
#ifdef CONFIG_8822B_TYPE2_DRV_DIS
    #undef CONFIG_8822B_TYPE2
#endif

#define CONFIG_8822B_TYPE3_TYPE5
#ifdef CONFIG_8822B_TYPE3_TYPE5_DRV_DIS
    #undef CONFIG_8822B_TYPE3_TYPE5
#endif

#define CONFIG_8822B_TYPE4
#ifdef CONFIG_8822B_TYPE4_DRV_DIS
    #undef CONFIG_8822B_TYPE4
#endif

#define CONFIG_8822B_TYPE6
#ifdef CONFIG_8822B_TYPE6_DRV_DIS
    #undef CONFIG_8822B_TYPE6
#endif

#define CONFIG_8822B_TYPE7
#ifdef CONFIG_8822B_TYPE7_DRV_DIS
    #undef CONFIG_8822B_TYPE7
#endif

#define CONFIG_8822B_TYPE8
#ifdef CONFIG_8822B_TYPE8_DRV_DIS
    #undef CONFIG_8822B_TYPE8
#endif

#define CONFIG_8822B_TYPE9
#ifdef CONFIG_8822B_TYPE9_DRV_DIS
    #undef CONFIG_8822B_TYPE9
#endif

#define CONFIG_8822B_TYPE3
#ifdef CONFIG_8822B_TYPE3_DRV_DIS
    #undef CONFIG_8822B_TYPE3
#endif

#define CONFIG_8822B_TYPE5
#ifdef CONFIG_8822B_TYPE5_DRV_DIS
    #undef CONFIG_8822B_TYPE5
#endif

/******************************************************************************
 *                           radioa.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_radioa(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_radioa(void);

/******************************************************************************
 *                           radiob.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_radiob(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_radiob(void);

/******************************************************************************
 *                           txpowertrack.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack(void);

/******************************************************************************
 *                           txpowertrack_type0.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type0(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type0(void);

/******************************************************************************
 *                           txpowertrack_type1.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type1(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type1(void);

/******************************************************************************
 *                           txpowertrack_type10.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type10(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type10(void);

/******************************************************************************
 *                           txpowertrack_type11.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type11(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type11(void);

/******************************************************************************
 *                           txpowertrack_type12.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type12(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type12(void);

/******************************************************************************
 *                           txpowertrack_type13.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type13(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type13(void);

/******************************************************************************
 *                           txpowertrack_type14.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type14(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type14(void);

/******************************************************************************
 *                           txpowertrack_type15.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type15(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type15(void);

/******************************************************************************
 *                           txpowertrack_type16.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type16(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type16(void);

/******************************************************************************
 *                           txpowertrack_type17.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type17(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type17(void);

/******************************************************************************
 *                           txpowertrack_type18.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type18(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type18(void);

/******************************************************************************
 *                           txpowertrack_type19.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type19(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type19(void);

/******************************************************************************
 *                           txpowertrack_type2.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type2(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type2(void);

/******************************************************************************
 *                           txpowertrack_type3_type5.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type3_type5(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type3_type5(void);

/******************************************************************************
 *                           txpowertrack_type4.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type4(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type4(void);

/******************************************************************************
 *                           txpowertrack_type6.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type6(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type6(void);

/******************************************************************************
 *                           txpowertrack_type7.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type7(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type7(void);

/******************************************************************************
 *                           txpowertrack_type8.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type8(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type8(void);

/******************************************************************************
 *                           txpowertrack_type9.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpowertrack_type9(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type9(void);

/******************************************************************************
 *                           txpwr_lmt.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpwr_lmt(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpwr_lmt(void);

/******************************************************************************
 *                           txpwr_lmt_type12.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpwr_lmt_type12(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpwr_lmt_type12(void);

/******************************************************************************
 *                           txpwr_lmt_type15.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpwr_lmt_type15(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpwr_lmt_type15(void);

/******************************************************************************
 *                           txpwr_lmt_type16.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpwr_lmt_type16(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpwr_lmt_type16(void);

/******************************************************************************
 *                           txpwr_lmt_type17.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpwr_lmt_type17(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpwr_lmt_type17(void);

/******************************************************************************
 *                           txpwr_lmt_type18.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpwr_lmt_type18(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpwr_lmt_type18(void);

/******************************************************************************
 *                           txpwr_lmt_type19.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpwr_lmt_type19(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpwr_lmt_type19(void);

/******************************************************************************
 *                           txpwr_lmt_type2.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpwr_lmt_type2(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpwr_lmt_type2(void);

/******************************************************************************
 *                           txpwr_lmt_type3.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpwr_lmt_type3(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpwr_lmt_type3(void);

/******************************************************************************
 *                           txpwr_lmt_type4.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpwr_lmt_type4(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpwr_lmt_type4(void);

/******************************************************************************
 *                           txpwr_lmt_type5.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_txpwr_lmt_type5(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_txpwr_lmt_type5(void);

#endif
#endif /* end of HWIMG_SUPPORT*/

