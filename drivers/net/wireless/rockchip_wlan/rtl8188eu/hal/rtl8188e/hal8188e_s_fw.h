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

#ifndef _FW_HEADER_8188E_S_H
#define _FW_HEADER_8188E_S_H

#ifdef CONFIG_SFW_SUPPORTED

#ifdef LOAD_FW_HEADER_FROM_DRIVER
#if (defined(CONFIG_AP_WOWLAN) || (DM_ODM_SUPPORT_TYPE & (ODM_AP)))
extern u8 array_mp_8188e_s_fw_ap[16054];
extern u32 array_length_mp_8188e_s_fw_ap;
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN)) || (DM_ODM_SUPPORT_TYPE & (ODM_CE))
extern u8 array_mp_8188e_s_fw_nic[19206];
extern u32 array_length_mp_8188e_s_fw_nic;
#ifdef CONFIG_WOWLAN
extern u8 array_mp_8188e_s_fw_wowlan[22710];
extern u32 array_length_mp_8188e_s_fw_wowlan;
#endif /*CONFIG_WOWLAN*/
#endif
#endif /* end of LOAD_FW_HEADER_FROM_DRIVER */

#endif /* end of CONFIG_SFW_SUPPORTED */

#endif

