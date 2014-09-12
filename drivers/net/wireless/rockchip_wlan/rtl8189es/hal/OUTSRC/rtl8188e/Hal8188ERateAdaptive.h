#ifndef __INC_RA_H
#define __INC_RA_H
/*++
Copyright (c) Realtek Semiconductor Corp. All rights reserved.

Module Name:
	RateAdaptive.h
	
Abstract:
	Prototype of RA and related data structure.
	    
Major Change History:
	When       Who               What
	---------- ---------------   -------------------------------
	2011-08-12 Page            Create.	
--*/

// Rate adaptive define
#define	PERENTRY	23
#define	RETRYSIZE	5
#define	RATESIZE	28
#define	TX_RPT2_ITEM_SIZE 	8

#if (DM_ODM_SUPPORT_TYPE  != ODM_WIN)
//
// TX report 2 format in Rx desc
//
#define GET_TX_RPT2_DESC_PKT_LEN_88E(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc, 0, 9)
#define GET_TX_RPT2_DESC_MACID_VALID_1_88E(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc+16, 0, 32)
#define GET_TX_RPT2_DESC_MACID_VALID_2_88E(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc+20, 0, 32)

#define GET_TX_REPORT_TYPE1_RERTY_0(__pAddr)						LE_BITS_TO_4BYTE( __pAddr, 0, 16)
#define GET_TX_REPORT_TYPE1_RERTY_1(__pAddr)						LE_BITS_TO_1BYTE( __pAddr+2, 0, 8)
#define GET_TX_REPORT_TYPE1_RERTY_2(__pAddr)						LE_BITS_TO_1BYTE( __pAddr+3, 0, 8)
#define GET_TX_REPORT_TYPE1_RERTY_3(__pAddr)						LE_BITS_TO_1BYTE( __pAddr+4, 0, 8)
#define GET_TX_REPORT_TYPE1_RERTY_4(__pAddr)						LE_BITS_TO_1BYTE( __pAddr+4+1, 0, 8)
#define GET_TX_REPORT_TYPE1_DROP_0(__pAddr)						LE_BITS_TO_1BYTE( __pAddr+4+2, 0, 8)
#define GET_TX_REPORT_TYPE1_DROP_1(__pAddr)						LE_BITS_TO_1BYTE( __pAddr+4+3, 0, 8)
#endif

// End rate adaptive define

VOID
ODM_RASupport_Init(
	IN	PDM_ODM_T	pDM_Odm
	);

int 
ODM_RAInfo_Init_all(
	IN    PDM_ODM_T		pDM_Odm
	);

int 
ODM_RAInfo_Init(
	IN 	PDM_ODM_T 	pDM_Odm,
	IN 	u1Byte 		MacID	
	);

u1Byte 
ODM_RA_GetShortGI_8188E(
	IN 	PDM_ODM_T 	pDM_Odm, 
	IN 	u1Byte 		MacID
	);

u1Byte 
ODM_RA_GetDecisionRate_8188E(
	IN 	PDM_ODM_T 	pDM_Odm, 
	IN 	u1Byte 		MacID
	);

u1Byte
ODM_RA_GetHwPwrStatus_8188E(
	IN 	PDM_ODM_T 	pDM_Odm, 
	IN 	u1Byte 		MacID
	);
VOID 
ODM_RA_UpdateRateInfo_8188E(
	IN PDM_ODM_T pDM_Odm,
	IN u1Byte MacID,
	IN u1Byte RateID, 
	IN u4Byte RateMask,
	IN u1Byte SGIEnable
	);

VOID 
ODM_RA_SetRSSI_8188E(
	IN 	PDM_ODM_T 		pDM_Odm, 
	IN 	u1Byte 			MacID, 
	IN 	u1Byte 			Rssi
	);

VOID
ODM_RA_TxRPT2Handle_8188E(	
	IN	PDM_ODM_T		pDM_Odm,
	IN	pu1Byte			TxRPT_Buf,
	IN	u2Byte			TxRPT_Len,
	IN	u4Byte			MacIDValidEntry0,
	IN	u4Byte			MacIDValidEntry1
	);
	

VOID 
ODM_RA_Set_TxRPT_Time(
	IN	PDM_ODM_T		pDM_Odm,
	IN	u2Byte 			minRptTime
	);	
#endif

