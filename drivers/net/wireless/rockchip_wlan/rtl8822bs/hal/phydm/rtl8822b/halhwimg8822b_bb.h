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
#ifndef __INC_MP_BB_HW_IMG_8822B_H
#define __INC_MP_BB_HW_IMG_8822B_H

/******************************************************************************
 *                           agc_tab.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_agc_tab(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_agc_tab(void);

/******************************************************************************
 *                           phy_reg.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_phy_reg(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_phy_reg(void);

/******************************************************************************
 *                           phy_reg_pg.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_phy_reg_pg(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_phy_reg_pg(void);

/******************************************************************************
 *                           phy_reg_pg_type12.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_phy_reg_pg_type12(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_phy_reg_pg_type12(void);

/******************************************************************************
 *                           phy_reg_pg_type15.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_phy_reg_pg_type15(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_phy_reg_pg_type15(void);

/******************************************************************************
 *                           phy_reg_pg_type16.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_phy_reg_pg_type16(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_phy_reg_pg_type16(void);

/******************************************************************************
 *                           phy_reg_pg_type17.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_phy_reg_pg_type17(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_phy_reg_pg_type17(void);

/******************************************************************************
 *                           phy_reg_pg_type18.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_phy_reg_pg_type18(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_phy_reg_pg_type18(void);

/******************************************************************************
 *                           phy_reg_pg_type19.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_phy_reg_pg_type19(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_phy_reg_pg_type19(void);

/******************************************************************************
 *                           phy_reg_pg_type2.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_phy_reg_pg_type2(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_phy_reg_pg_type2(void);

/******************************************************************************
 *                           phy_reg_pg_type3.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_phy_reg_pg_type3(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_phy_reg_pg_type3(void);

/******************************************************************************
 *                           phy_reg_pg_type4.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_phy_reg_pg_type4(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_phy_reg_pg_type4(void);

/******************************************************************************
 *                           phy_reg_pg_type5.TXT
 ******************************************************************************/

/* tc: Test Chip, mp: mp Chip*/
void
odm_read_and_config_mp_8822b_phy_reg_pg_type5(struct dm_struct *dm);
u32 odm_get_version_mp_8822b_phy_reg_pg_type5(void);

#endif
#endif /* end of HWIMG_SUPPORT*/

