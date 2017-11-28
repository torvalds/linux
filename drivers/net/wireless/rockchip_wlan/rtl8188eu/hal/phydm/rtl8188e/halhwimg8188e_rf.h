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

/*Image2HeaderVersion: 2.18*/
#if (RTL8188E_SUPPORT == 1)
#ifndef __INC_MP_RF_HW_IMG_8188E_H
#define __INC_MP_RF_HW_IMG_8188E_H


/******************************************************************************
*                           RadioA.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_radioa(/* TC: Test Chip, MP: MP Chip*/
	struct PHY_DM_STRUCT  *p_dm_odm
);
u32 odm_get_version_mp_8188e_radioa(void);

/******************************************************************************
*                           TxPowerTrack_AP.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_ap(/* TC: Test Chip, MP: MP Chip*/
	struct PHY_DM_STRUCT  *p_dm_odm
);
u32 odm_get_version_mp_8188e_txpowertrack_ap(void);

/******************************************************************************
*                           TxPowerTrack_PCIE.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_pcie(/* TC: Test Chip, MP: MP Chip*/
	struct PHY_DM_STRUCT  *p_dm_odm
);
u32 odm_get_version_mp_8188e_txpowertrack_pcie(void);

/******************************************************************************
*                           TxPowerTrack_PCIE_ICUT.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_pcie_icut(/* TC: Test Chip, MP: MP Chip*/
	struct PHY_DM_STRUCT  *p_dm_odm
);
u32 odm_get_version_mp_8188e_txpowertrack_pcie_icut(void);

/******************************************************************************
*                           TxPowerTrack_SDIO.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_sdio(/* TC: Test Chip, MP: MP Chip*/
	struct PHY_DM_STRUCT  *p_dm_odm
);
u32 odm_get_version_mp_8188e_txpowertrack_sdio(void);

/******************************************************************************
*                           TxPowerTrack_SDIO_ICUT.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_sdio_icut(/* TC: Test Chip, MP: MP Chip*/
	struct PHY_DM_STRUCT  *p_dm_odm
);
u32 odm_get_version_mp_8188e_txpowertrack_sdio_icut(void);

/******************************************************************************
*                           TxPowerTrack_USB.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_usb(/* TC: Test Chip, MP: MP Chip*/
	struct PHY_DM_STRUCT  *p_dm_odm
);
u32 odm_get_version_mp_8188e_txpowertrack_usb(void);

/******************************************************************************
*                           TxPowerTrack_USB_ICUT.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpowertrack_usb_icut(/* TC: Test Chip, MP: MP Chip*/
	struct PHY_DM_STRUCT  *p_dm_odm
);
u32 odm_get_version_mp_8188e_txpowertrack_usb_icut(void);

/******************************************************************************
*                           TXPWR_LMT.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpwr_lmt(/* TC: Test Chip, MP: MP Chip*/
	struct PHY_DM_STRUCT  *p_dm_odm
);
u32 odm_get_version_mp_8188e_txpwr_lmt(void);

/******************************************************************************
*                           TXPWR_LMT_88EE_M2_for_MSI.TXT
******************************************************************************/

void
odm_read_and_config_mp_8188e_txpwr_lmt_88e_e_m2_for_msi(/* TC: Test Chip, MP: MP Chip*/
	struct PHY_DM_STRUCT  *p_dm_odm
);
u32 odm_get_version_mp_8188e_txpwr_lmt_88ee_m2_for_msi(void);

#endif
#endif /* end of HWIMG_SUPPORT*/
