/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
*
* Copyright(c) 2012 - 2017 Realtek Corporation.
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

#ifdef CONFIG_RTL8821C

#ifndef _FW_HEADER_8821C_H
#define _FW_HEADER_8821C_H

#ifdef LOAD_FW_HEADER_FROM_DRIVER
#if (defined(CONFIG_AP_WOWLAN) || (DM_ODM_SUPPORT_TYPE & (ODM_AP)))
extern u8 array_mp_8821c_fw_ap[115416];
extern u32 array_length_mp_8821c_fw_ap;
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN)) || (DM_ODM_SUPPORT_TYPE & (ODM_CE))
extern u8 array_mp_8821c_fw_nic[138984];
extern u32 array_length_mp_8821c_fw_nic;
extern u8 array_mp_8821c_fw_spic[73216];
extern u32 array_length_mp_8821c_fw_spic;
#ifdef CONFIG_WOWLAN
extern u8 array_mp_8821c_fw_wowlan[103304];
extern u32 array_length_mp_8821c_fw_wowlan;
#endif /*CONFIG_WOWLAN*/
#endif
#endif /* end of LOAD_FW_HEADER_FROM_DRIVER */

#endif

#endif

