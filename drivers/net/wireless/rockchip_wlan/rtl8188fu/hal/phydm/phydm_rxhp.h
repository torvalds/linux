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
#ifndef	__PHYDMRXHP_H__
#define    __PHYDMRXHP_H__

#define RXHP_VERSION	"1.0"

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

#define	AFH_PSD		1	//0:normal PSD scan, 1: only do 20 pts PSD
#define	MODE_40M		0	//0:20M, 1:40M
#define	PSD_TH2		3  
#define	PSD_CHMIN		20   // Minimum channel number for BT AFH
#define	SIR_STEP_SIZE	3
#define   Smooth_Size_1 	5
#define	Smooth_TH_1	3
#define   Smooth_Size_2 	10
#define	Smooth_TH_2	4
#define   Smooth_Size_3 	20
#define	Smooth_TH_3	4
#define   Smooth_Step_Size 5
#define	Adaptive_SIR	1
#define	PSD_RESCAN		4
#define	PSD_SCAN_INTERVAL	700 //ms

typedef struct _RX_High_Power_
{
	u1Byte		RXHP_flag;
	u1Byte		PSD_func_trigger;
	u1Byte		PSD_bitmap_RXHP[80];
	u1Byte		Pre_IGI;
	u1Byte		Cur_IGI;
	u1Byte		Pre_pw_th;
	u1Byte		Cur_pw_th;
	BOOLEAN		First_time_enter;
	BOOLEAN		RXHP_enable;
	u1Byte		TP_Mode;
	RT_TIMER	PSDTimer;
	#if USE_WORKITEM
	RT_WORK_ITEM		PSDTimeWorkitem;
	#endif
}RXHP_T, *pRXHP_T;

#define	dm_PSDMonitorCallback	odm_PSDMonitorCallback
VOID	odm_PSDMonitorCallback(PRT_TIMER		pTimer);

VOID
odm_PSDMonitorInit(
	IN		PVOID			pDM_VOID
	);

void	odm_RXHPInit(
	IN		PVOID			pDM_VOID);

void odm_RXHP(
	IN		PVOID			pDM_VOID);

VOID
odm_PSD_RXHPCallback(
	PRT_TIMER		pTimer
);

 VOID
ODM_PSDDbgControl(
	IN	PADAPTER	Adapter,
	IN	u4Byte		mode,
	IN	u4Byte		btRssi
	);

 VOID
odm_PSD_RXHPCallback(
	PRT_TIMER		pTimer
);

VOID
odm_PSD_RXHPWorkitemCallback(
    IN PVOID            pContext
    );

VOID
odm_PSDMonitorWorkItemCallback(
    IN PVOID            pContext
    );

 #endif

 #endif
 