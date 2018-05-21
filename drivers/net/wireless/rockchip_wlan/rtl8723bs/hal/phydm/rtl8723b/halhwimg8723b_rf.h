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
******************************************************************************/

/*Image2HeaderVersion: 3.5.1*/
#if (RTL8723B_SUPPORT == 1)
#ifndef __INC_MP_RF_HW_IMG_8723B_H
#define __INC_MP_RF_HW_IMG_8723B_H


/******************************************************************************
*                           radioa.TXT
******************************************************************************/

void
odm_read_and_config_mp_8723b_radioa(/* tc: Test Chip, mp: mp Chip*/
	struct	PHY_DM_STRUCT *p_dm
);
u32	odm_get_version_mp_8723b_radioa(void);

/******************************************************************************
*                           txpowertrack_ap.TXT
******************************************************************************/

void
odm_read_and_config_mp_8723b_txpowertrack_ap(/* tc: Test Chip, mp: mp Chip*/
	struct	PHY_DM_STRUCT *p_dm
);
u32	odm_get_version_mp_8723b_txpowertrack_ap(void);

/******************************************************************************
*                           txpowertrack_pcie.TXT
******************************************************************************/

void
odm_read_and_config_mp_8723b_txpowertrack_pcie(/* tc: Test Chip, mp: mp Chip*/
	struct	PHY_DM_STRUCT *p_dm
);
u32	odm_get_version_mp_8723b_txpowertrack_pcie(void);

/******************************************************************************
*                           txpowertrack_sdio.TXT
******************************************************************************/

void
odm_read_and_config_mp_8723b_txpowertrack_sdio(/* tc: Test Chip, mp: mp Chip*/
	struct	PHY_DM_STRUCT *p_dm
);
u32	odm_get_version_mp_8723b_txpowertrack_sdio(void);

/******************************************************************************
*                           txpowertrack_usb.TXT
******************************************************************************/

void
odm_read_and_config_mp_8723b_txpowertrack_usb(/* tc: Test Chip, mp: mp Chip*/
	struct	PHY_DM_STRUCT *p_dm
);
u32	odm_get_version_mp_8723b_txpowertrack_usb(void);

/******************************************************************************
*                           txpwr_lmt.TXT
******************************************************************************/

void
odm_read_and_config_mp_8723b_txpwr_lmt(/* tc: Test Chip, mp: mp Chip*/
	struct	PHY_DM_STRUCT *p_dm
);
u32	odm_get_version_mp_8723b_txpwr_lmt(void);

#endif
#endif /* end of HWIMG_SUPPORT*/

