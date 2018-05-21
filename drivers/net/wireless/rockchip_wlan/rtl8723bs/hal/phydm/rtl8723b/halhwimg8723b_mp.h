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

#if (RTL8723B_SUPPORT == 1)
#ifndef __INC_MP_HW_IMG_8723B_H

#define __INC_MP_HW_IMG_8723B_H

#ifdef CONFIG_MP_INCLUDED

#define rtl8723b_fw_bt_img_array_length 20564

#define rtl8723b_phyreg_array_mp_length 4

extern u8 rtl8723b_fw_bt_img_array[rtl8723b_fw_bt_img_array_length];
extern const u32 rtl8723b_phyreg_array_mp[rtl8723b_phyreg_array_mp_length];

void
odm_read_firmware_mp_8723b_fw_mp(
	struct PHY_DM_STRUCT    *p_dm,
	u8	*p_firmware,
	u32	*p_firmware_size
);


#endif /* CONFIG_MP_INCLUDED */
#endif
#endif
