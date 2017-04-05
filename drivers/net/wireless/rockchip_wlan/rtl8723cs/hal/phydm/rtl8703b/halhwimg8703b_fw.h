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

/*Image2HeaderVersion: 2.16*/
#if (RTL8703B_SUPPORT == 1)
#ifndef __INC_MP_FW_HW_IMG_8703B_H
#define __INC_MP_FW_HW_IMG_8703B_H


/******************************************************************************
*                           FW_AP.TXT
******************************************************************************/

void
odm_read_firmware_mp_8703b_fw_ap(
	struct PHY_DM_STRUCT    *p_dm_odm,
	u8       *p_firmware,
	u32       *p_firmware_size
);

/******************************************************************************
*                           FW_NIC.TXT
******************************************************************************/

void
odm_read_firmware_mp_8703b_fw_nic(
	struct PHY_DM_STRUCT    *p_dm_odm,
	u8       *p_firmware,
	u32       *p_firmware_size
);

/******************************************************************************
*                           FW_WoWLAN.TXT
******************************************************************************/

void
odm_read_firmware_mp_8703b_fw_wowlan(
	struct PHY_DM_STRUCT    *p_dm_odm,
	u8       *p_firmware,
	u32       *p_firmware_size
);

#endif
#endif /* end of HWIMG_SUPPORT*/
