/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __INC_RA_H
#define __INC_RA_H
/*++
Copyright (c) Realtek Semiconductor Corp. All rights reserved.

Module Name:
	rate_adaptive.h

Abstract:
	Prototype of RA and related data structure.

Major Change History:
	When       Who               What
	---------- ---------------   -------------------------------
	2011-08-12 Page            Create.
--*/

/* rate adaptive define */
#define	PERENTRY	23
#define	RETRYSIZE	5
#define	RATESIZE	28
#define	TX_RPT2_ITEM_SIZE	8

#define		DM_RA_RATE_UP				1
#define		DM_RA_RATE_DOWN			2

#if (DM_ODM_SUPPORT_TYPE != ODM_WIN)
	/*
	* TX report 2 format in Rx desc
	*   */
	#define GET_TX_RPT2_DESC_PKT_LEN_88E(__prx_status_desc)				LE_BITS_TO_4BYTE(__prx_status_desc, 0, 9)
	#define GET_TX_RPT2_DESC_MACID_VALID_1_88E(__prx_status_desc)		LE_BITS_TO_4BYTE(__prx_status_desc+16, 0, 32)
	#define GET_TX_RPT2_DESC_MACID_VALID_2_88E(__prx_status_desc)		LE_BITS_TO_4BYTE(__prx_status_desc+20, 0, 32)

	#define GET_TX_REPORT_TYPE1_RERTY_0(__paddr)						LE_BITS_TO_4BYTE(__paddr, 0, 16)
	#define GET_TX_REPORT_TYPE1_RERTY_1(__paddr)						LE_BITS_TO_1BYTE(__paddr+2, 0, 8)
	#define GET_TX_REPORT_TYPE1_RERTY_2(__paddr)						LE_BITS_TO_1BYTE(__paddr+3, 0, 8)
	#define GET_TX_REPORT_TYPE1_RERTY_3(__paddr)						LE_BITS_TO_1BYTE(__paddr+4, 0, 8)
	#define GET_TX_REPORT_TYPE1_RERTY_4(__paddr)						LE_BITS_TO_1BYTE(__paddr+4+1, 0, 8)
	#define GET_TX_REPORT_TYPE1_DROP_0(__paddr)						LE_BITS_TO_1BYTE(__paddr+4+2, 0, 8)
	#define GET_TX_REPORT_TYPE1_DROP_1(__paddr)						LE_BITS_TO_1BYTE(__paddr+4+3, 0, 8)
#endif

/* End rate adaptive define */

void
odm_ra_support_init(
	struct PHY_DM_STRUCT	*p_dm_odm
);

int
odm_ra_info_init_all(
	struct PHY_DM_STRUCT		*p_dm_odm
);

int
odm_ra_info_init(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		mac_id
);

u8
odm_ra_get_sgi_8188e(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8		mac_id
);

u8
odm_ra_get_decision_rate_8188e(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8		mac_id
);

u8
odm_ra_get_hw_pwr_status_8188e(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8		mac_id
);
void
odm_ra_update_rate_info_8188e(
	struct PHY_DM_STRUCT *p_dm_odm,
	u8 mac_id,
	u8 rate_id,
	u32 rate_mask,
	u8 sgi_enable
);

void
odm_ra_set_rssi_8188e(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u8			mac_id,
	u8			rssi
);

void
odm_ra_tx_rpt2_handle_8188e(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u8			*tx_rpt_buf,
	u16			tx_rpt_len,
	u32			mac_id_valid_entry0,
	u32			mac_id_valid_entry1
);


void
odm_ra_set_tx_rpt_time(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u16			min_rpt_time
);
#endif
