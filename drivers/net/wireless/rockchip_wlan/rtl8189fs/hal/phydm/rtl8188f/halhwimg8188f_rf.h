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
#if (RTL8188F_SUPPORT == 1)
#ifndef __INC_MP_RF_HW_IMG_8188F_H
#define __INC_MP_RF_HW_IMG_8188F_H


/******************************************************************************
*                           radioa.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188f_radioa( /* tc: Test Chip, mp: mp Chip*/
				    struct dm_struct *dm);
u32 odm_get_version_mp_8188f_radioa(void);

/******************************************************************************
*                           txpowertrack_ap.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188f_txpowertrack_ap( /* tc: Test Chip, mp: mp Chip*/
					     struct dm_struct *dm);
u32	odm_get_version_mp_8188f_txpowertrack_ap(void);

/******************************************************************************
*                           txpowertrack_sdio.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188f_txpowertrack_sdio( /* tc: Test Chip, mp: mp Chip*/
					       struct dm_struct *dm);
u32	odm_get_version_mp_8188f_txpowertrack_sdio(void);

/******************************************************************************
*                           txpowertrack_usb.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188f_txpowertrack_usb( /* tc: Test Chip, mp: mp Chip*/
					      struct dm_struct *dm);
u32	odm_get_version_mp_8188f_txpowertrack_usb(void);

/******************************************************************************
*                           txpwr_lmt.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188f_txpwr_lmt( /* tc: Test Chip, mp: mp Chip*/
				       struct dm_struct *dm);
u32	odm_get_version_mp_8188f_txpwr_lmt(void);

#endif
#endif /* end of HWIMG_SUPPORT*/

