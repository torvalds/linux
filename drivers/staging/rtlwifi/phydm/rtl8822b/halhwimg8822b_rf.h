/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

/*Image2HeaderVersion: 3.2*/
#ifndef __INC_MP_RF_HW_IMG_8822B_H
#define __INC_MP_RF_HW_IMG_8822B_H

/******************************************************************************
 *                           radioa.TXT
 ******************************************************************************/

void odm_read_and_config_mp_8822b_radioa(struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_radioa(void);

/******************************************************************************
 *                           radiob.TXT
 ******************************************************************************/

void odm_read_and_config_mp_8822b_radiob(struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_radiob(void);

/******************************************************************************
 *                           txpowertrack.TXT
 ******************************************************************************/

void odm_read_and_config_mp_8822b_txpowertrack(struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack(void);

/******************************************************************************
 *                           txpowertrack_type0.TXT
 ******************************************************************************/

void odm_read_and_config_mp_8822b_txpowertrack_type0(struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type0(void);

/******************************************************************************
 *                           txpowertrack_type1.TXT
 ******************************************************************************/

void odm_read_and_config_mp_8822b_txpowertrack_type1(struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type1(void);

/******************************************************************************
 *                           txpowertrack_type2.TXT
 ******************************************************************************/

void odm_read_and_config_mp_8822b_txpowertrack_type2(struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type2(void);

/******************************************************************************
 *                           txpowertrack_type3_type5.TXT
 ******************************************************************************/

void odm_read_and_config_mp_8822b_txpowertrack_type3_type5(
	struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type3_type5(void);

/******************************************************************************
 *                           txpowertrack_type4.TXT
 ******************************************************************************/

void odm_read_and_config_mp_8822b_txpowertrack_type4(struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type4(void);

/******************************************************************************
 *                           txpowertrack_type6.TXT
 ******************************************************************************/

void odm_read_and_config_mp_8822b_txpowertrack_type6(struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type6(void);

/******************************************************************************
 *                           txpowertrack_type7.TXT
 ******************************************************************************/

void odm_read_and_config_mp_8822b_txpowertrack_type7(struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type7(void);

/******************************************************************************
 *                           txpowertrack_type8.TXT
 *****************************************************************************/

void odm_read_and_config_mp_8822b_txpowertrack_type8(struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type8(void);

/******************************************************************************
 *                           txpowertrack_type9.TXT
 ******************************************************************************/

void odm_read_and_config_mp_8822b_txpowertrack_type9(struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_txpowertrack_type9(void);

/******************************************************************************
 *                           txpwr_lmt.TXT
 ******************************************************************************/

void odm_read_and_config_mp_8822b_txpwr_lmt(struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_txpwr_lmt(void);

/******************************************************************************
 *                           txpwr_lmt_type5.TXT
 ******************************************************************************/

void odm_read_and_config_mp_8822b_txpwr_lmt_type5(struct phy_dm_struct *dm);
u32 odm_get_version_mp_8822b_txpwr_lmt_type5(void);

#endif
