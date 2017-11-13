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
#if (RTL8723D_SUPPORT == 1)
#ifndef __INC_MP_MAC_HW_IMG_8723D_H
#define __INC_MP_MAC_HW_IMG_8723D_H


/******************************************************************************
*                           mac_reg.TXT
******************************************************************************/

void
odm_read_and_config_mp_8723d_mac_reg(/* tc: Test Chip, mp: mp Chip*/
	struct	PHY_DM_STRUCT *p_dm
);
u32	odm_get_version_mp_8723d_mac_reg(void);

#endif
#endif /* end of HWIMG_SUPPORT*/

