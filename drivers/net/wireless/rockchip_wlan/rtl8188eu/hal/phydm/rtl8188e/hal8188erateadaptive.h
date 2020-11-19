/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) Semiconductor - 2017 Realtek Corporation.
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
#ifndef __INC_RA_H
#define __INC_RA_H

/* rate adaptive define */
#define PERENTRY 23
#define RETRYSIZE 5
#define RATESIZE 28
#define TX_RPT2_ITEM_SIZE 8

#define DM_RA_RATE_UP 1
#define DM_RA_RATE_DOWN 2

#define AP_USB_SDIO ((DM_ODM_SUPPORT_TYPE == ODM_AP) && ((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE)))

#if (DM_ODM_SUPPORT_TYPE != ODM_WIN)
/*
	* TX report 2 format in Rx desc
	*   */
#define GET_TX_RPT2_DESC_PKT_LEN_88E(__prx_status_desc) LE_BITS_TO_4BYTE(__prx_status_desc, 0, 9)
#define GET_TX_RPT2_DESC_MACID_VALID_1_88E(__prx_status_desc) LE_BITS_TO_4BYTE(__prx_status_desc + 16, 0, 32)
#define GET_TX_RPT2_DESC_MACID_VALID_2_88E(__prx_status_desc) LE_BITS_TO_4BYTE(__prx_status_desc + 20, 0, 32)

#define GET_TX_REPORT_TYPE1_RERTY_0(__paddr) LE_BITS_TO_4BYTE(__paddr, 0, 16)
#define GET_TX_REPORT_TYPE1_RERTY_1(__paddr) LE_BITS_TO_1BYTE(__paddr + 2, 0, 8)
#define GET_TX_REPORT_TYPE1_RERTY_2(__paddr) LE_BITS_TO_1BYTE(__paddr + 3, 0, 8)
#define GET_TX_REPORT_TYPE1_RERTY_3(__paddr) LE_BITS_TO_1BYTE(__paddr + 4, 0, 8)
#define GET_TX_REPORT_TYPE1_RERTY_4(__paddr) LE_BITS_TO_1BYTE(__paddr + 4 + 1, 0, 8)
#define GET_TX_REPORT_TYPE1_DROP_0(__paddr) LE_BITS_TO_1BYTE(__paddr + 4 + 2, 0, 8)
#define GET_TX_REPORT_TYPE1_DROP_1(__paddr) LE_BITS_TO_1BYTE(__paddr + 4 + 3, 0, 8)
#endif

enum phydm_rateid_idx_88e_e { /*Copy From SD4  _RATR_TABLE_MODE*/
			      PHYDM_RAID_88E_NGB = 0, /* BGN 40 Mhz 2SS 1SS */
			      PHYDM_RAID_88E_NG = 1, /* GN or N */
			      PHYDM_RAID_88E_NB = 2, /* BGN 20 Mhz 2SS 1SS  or BN */
			      PHYDM_RAID_88E_N = 3,
			      PHYDM_RAID_88E_GB = 4,
			      PHYDM_RAID_88E_G = 5,
			      PHYDM_RAID_88E_B = 6,
			      PHYDM_RAID_88E_MC = 7,
			      PHYDM_RAID_88E_AC_N = 8
};

/* End rate adaptive define */

extern void phydm_tx_stats_rst(struct dm_struct *dm);

void odm_ra_support_init(struct dm_struct *dm);

void odm_ra_info_init_all(struct dm_struct *dm);

int odm_ra_info_init(struct dm_struct *dm, u32 mac_id);

u8 odm_ra_get_sgi_8188e(struct dm_struct *dm, u8 mac_id);

u8 odm_ra_get_decision_rate_8188e(struct dm_struct *dm, u8 mac_id);

u8 odm_ra_get_hw_pwr_status_8188e(struct dm_struct *dm, u8 mac_id);

u8 phydm_get_rate_id_88e(void *dm_void, u8 macid);

void phydm_ra_update_8188e(struct dm_struct *dm, u8 mac_id, u8 rate_id,
			   u32 rate_mask, u8 sgi_enable);

void odm_ra_set_rssi_8188e(struct dm_struct *dm, u8 mac_id, u8 rssi);

void odm_ra_tx_rpt2_handle_8188e(struct dm_struct *dm, u8 *tx_rpt_buf,
				 u16 tx_rpt_len, u32 mac_id_valid_entry0,
				 u32 mac_id_valid_entry1);

void odm_ra_set_tx_rpt_time(struct dm_struct *dm, u16 min_rpt_time);
#endif
