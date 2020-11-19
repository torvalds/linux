/* SPDX-License-Identifier: GPL-2.0 */
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
 *****************************************************************************/

/*Image2HeaderVersion: 3.5.2*/
#if (RTL8188E_SUPPORT == 1)
#ifndef __INC_MP_RF_HW_IMG_8188E_H
#define __INC_MP_RF_HW_IMG_8188E_H


/******************************************************************************
*                           radioa.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_radioa( /* tc: Test Chip, mp: mp Chip*/
				    struct dm_struct *dm);
u32 odm_get_version_mp_8188e_radioa(void);

/******************************************************************************
*                           txpowertrack_ap.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_ap( /* tc: Test Chip, mp: mp Chip*/
					     struct dm_struct *dm);
u32	odm_get_version_mp_8188e_txpowertrack_ap(void);

/******************************************************************************
*                           txpowertrack_pcie.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_pcie( /* tc: Test Chip, mp: mp Chip*/
					       struct dm_struct *dm);
u32	odm_get_version_mp_8188e_txpowertrack_pcie(void);

/******************************************************************************
*                           txpowertrack_pcie_icut.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_pcie_icut(
						    /* tc: Test Chip, mp: mp Chip*/
						    struct dm_struct *dm);
u32	odm_get_version_mp_8188e_txpowertrack_pcie_icut(void);

/******************************************************************************
*                           txpowertrack_sdio.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_sdio( /* tc: Test Chip, mp: mp Chip*/
					       struct dm_struct *dm);
u32	odm_get_version_mp_8188e_txpowertrack_sdio(void);

/******************************************************************************
*                           txpowertrack_sdio_icut.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_sdio_icut(
						    /* tc: Test Chip, mp: mp Chip*/
						    struct dm_struct *dm);
u32	odm_get_version_mp_8188e_txpowertrack_sdio_icut(void);

/******************************************************************************
*                           txpowertrack_usb.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_usb( /* tc: Test Chip, mp: mp Chip*/
					      struct dm_struct *dm);
u32	odm_get_version_mp_8188e_txpowertrack_usb(void);

/******************************************************************************
*                           txpowertrack_usb_icut.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_usb_icut(
						   /* tc: Test Chip, mp: mp Chip*/
						   struct dm_struct *dm);
u32	odm_get_version_mp_8188e_txpowertrack_usb_icut(void);

/******************************************************************************
*                           txpwr_lmt.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpwr_lmt( /* tc: Test Chip, mp: mp Chip*/
				       struct dm_struct *dm);
u32	odm_get_version_mp_8188e_txpwr_lmt(void);

/******************************************************************************
*                           txpwr_lmt_88ee_m2_for_msi.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpwr_lmt_88ee_m2_for_msi(
						       /* tc: Test Chip, mp: mp Chip*/
						       struct dm_struct *dm);
u32	odm_get_version_mp_8188e_txpwr_lmt_88ee_m2_for_msi(void);

#endif
#endif /* end of HWIMG_SUPPORT*/

