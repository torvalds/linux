/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2011 Realtek Semiconductor Corp. */

#ifndef __INC_RA_H
#define __INC_RA_H
/* Module Name: RateAdaptive.h
 * Abstract: Prototype of RA and related data structure.
 */

#include <linux/bitfield.h>

/*  Rate adaptive define */
#define	PERENTRY	23
#define	RETRYSIZE	5
#define	RATESIZE	28
#define	TX_RPT2_ITEM_SIZE	8

/*  TX report 2 format in Rx desc */
#define GET_TX_RPT2_DESC_PKT_LEN_88E(__rxstatusdesc)		\
	le32_get_bits(*(__le32 *)__rxstatusdesc, GENMASK(8, 0))
#define GET_TX_RPT2_DESC_MACID_VALID_1_88E(__rxstatusdesc)	\
	le32_to_cpu((*(__le32 *)(__rxstatusdesc + 16))
#define GET_TX_RPT2_DESC_MACID_VALID_2_88E(__rxstatusdesc)	\
	le32_to_cpu((*(__le32 *)(__rxstatusdesc + 20))

#define GET_TX_REPORT_TYPE1_RERTY_0(__paddr)			\
	le16_get_bits(*(__le16 *)__paddr, GENMASK(15, 0))
#define GET_TX_REPORT_TYPE1_RERTY_1(__paddr)			\
	LE_BITS_TO_1BYTE(__paddr + 2, 0, 8)
#define GET_TX_REPORT_TYPE1_RERTY_2(__paddr)			\
	LE_BITS_TO_1BYTE(__paddr + 3, 0, 8)
#define GET_TX_REPORT_TYPE1_RERTY_3(__paddr)			\
	LE_BITS_TO_1BYTE(__paddr + 4, 0, 8)
#define GET_TX_REPORT_TYPE1_RERTY_4(__paddr)			\
	LE_BITS_TO_1BYTE(__paddr + 5, 0, 8)
#define GET_TX_REPORT_TYPE1_DROP_0(__paddr)			\
	LE_BITS_TO_1BYTE(__paddr + 6, 0, 8)
/*  End rate adaptive define */

void ODM_RASupport_Init(struct odm_dm_struct *dm_odm);

int ODM_RAInfo_Init_all(struct odm_dm_struct *dm_odm);

int ODM_RAInfo_Init(struct odm_dm_struct *dm_odm, u8 MacID);

u8 ODM_RA_GetShortGI_8188E(struct odm_dm_struct *dm_odm, u8 MacID);

u8 ODM_RA_GetDecisionRate_8188E(struct odm_dm_struct *dm_odm, u8 MacID);

u8 ODM_RA_GetHwPwrStatus_8188E(struct odm_dm_struct *dm_odm, u8 MacID);
void ODM_RA_UpdateRateInfo_8188E(struct odm_dm_struct *dm_odm, u8 MacID,
				 u8 RateID, u32 RateMask,
				 u8 SGIEnable);

void ODM_RA_SetRSSI_8188E(struct odm_dm_struct *dm_odm, u8 macid,
			  u8 rssi);

void ODM_RA_TxRPT2Handle_8188E(struct odm_dm_struct *dm_odm,
			       u8 *txrpt_buf, u16 txrpt_len,
			       u32 validentry0, u32 validentry1);

void ODM_RA_Set_TxRPT_Time(struct odm_dm_struct *dm_odm, u16 minRptTime);

#endif
