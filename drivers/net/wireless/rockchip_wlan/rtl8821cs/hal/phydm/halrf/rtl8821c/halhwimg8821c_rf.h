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

/*Image2HeaderVersion: R3 1.5.8*/
#if (RTL8821C_SUPPORT == 1)
#ifndef __INC_MP_RF_HW_IMG_8821C_H
#define __INC_MP_RF_HW_IMG_8821C_H

/* Please add following compiler flags definition (#define CONFIG_XXX_DRV_DIS)
 * into driver source code to reduce code size if necessary.
 * #define CONFIG_8821C_DRV_DIS
 * #define CONFIG_8821C_TYPE0X20_DRV_DIS
 * #define CONFIG_8821C_TYPE0X28_DRV_DIS
 * #define CONFIG_8821C_FCCSAR_DRV_DIS
 * #define CONFIG_8821C_ICSAR_DRV_DIS
 */

#define CONFIG_8821C
#ifdef CONFIG_8821C_DRV_DIS
    #undef CONFIG_8821C
#endif

#define CONFIG_8821C_TYPE0X20
#ifdef CONFIG_8821C_TYPE0X20_DRV_DIS
    #undef CONFIG_8821C_TYPE0X20
#endif

#define CONFIG_8821C_TYPE0X28
#ifdef CONFIG_8821C_TYPE0X28_DRV_DIS
    #undef CONFIG_8821C_TYPE0X28
#endif

#define CONFIG_8821C_FCCSAR
#ifdef CONFIG_8821C_FCCSAR_DRV_DIS
    #undef CONFIG_8821C_FCCSAR
#endif

#define CONFIG_8821C_ICSAR
#ifdef CONFIG_8821C_ICSAR_DRV_DIS
    #undef CONFIG_8821C_ICSAR
#endif

/******************************************************************************
 *                           radioa.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8821c_radioa(struct dm_struct *dm);
u32 odm_get_version_mp_8821c_radioa(void);

/******************************************************************************
 *                           txpowertrack.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8821c_txpowertrack(struct dm_struct *dm);
u32 odm_get_version_mp_8821c_txpowertrack(void);

/******************************************************************************
 *                           txpowertrack_type0x20.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8821c_txpowertrack_type0x20(struct dm_struct *dm);
u32 odm_get_version_mp_8821c_txpowertrack_type0x20(void);

/******************************************************************************
 *                           txpowertrack_type0x28.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8821c_txpowertrack_type0x28(struct dm_struct *dm);
u32 odm_get_version_mp_8821c_txpowertrack_type0x28(void);

/******************************************************************************
 *                           txpwr_lmt.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8821c_txpwr_lmt(struct dm_struct *dm);
u32 odm_get_version_mp_8821c_txpwr_lmt(void);

/******************************************************************************
 *                           txpwr_lmt_fccsar.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8821c_txpwr_lmt_fccsar(struct dm_struct *dm);
u32 odm_get_version_mp_8821c_txpwr_lmt_fccsar(void);

/******************************************************************************
 *                           txpwr_lmt_icsar.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8821c_txpwr_lmt_icsar(struct dm_struct *dm);
u32 odm_get_version_mp_8821c_txpwr_lmt_icsar(void);

#endif
#endif /* end of HWIMG_SUPPORT*/

