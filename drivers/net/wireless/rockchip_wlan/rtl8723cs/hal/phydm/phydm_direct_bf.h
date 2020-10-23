/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __PHYDM_DIR_BF_H__
#define __PHYDM_DIR_BF_H__

#ifdef CONFIG_DIRECTIONAL_BF
#define ANGLE_NUM	12

/*@
 * ============================================================
 * function prototype
 * ============================================================
 */
void phydm_iq_gen_en(void *dm_void);
void phydm_dis_cdd(void *dm_void);
void phydm_pathb_q_matrix_rotate_en(void *dm_void);
void phydm_pathb_q_matrix_rotate(void *dm_void, u16 idx);
void phydm_set_direct_bfer(void *dm_void, u16 phs_idx, u8 su_idx);
void phydm_set_direct_bfer_txdesc_en(void *dm_void, u8 enable);
#endif
#endif
