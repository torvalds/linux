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


#ifndef _RTW_QOS_H_
#define _RTW_QOS_H_

#define DRV_CFG_UAPSD_VO 	BIT0
#define DRV_CFG_UAPSD_VI 	BIT1
#define DRV_CFG_UAPSD_BK 	BIT2
#define DRV_CFG_UAPSD_BE 	BIT3

#define WMM_IE_UAPSD_VO 	BIT0
#define WMM_IE_UAPSD_VI 	BIT1
#define WMM_IE_UAPSD_BK 	BIT2
#define WMM_IE_UAPSD_BE 	BIT3

#define WMM_TID0 	BIT0
#define WMM_TID1 	BIT1
#define WMM_TID2 	BIT2
#define WMM_TID3 	BIT3
#define WMM_TID4 	BIT4
#define WMM_TID5 	BIT5
#define WMM_TID6 	BIT6
#define WMM_TID7 	BIT7

#define AP_SUPPORTED_UAPSD BIT7
/* TC = Traffic Category,  TID0~7 represents TC */
#define BIT_MASK_TID_TC 0xff
/* TS = Traffic Stream,  TID8~15 represents TS */
#define BIT_MASK_TID_TS 0xff00
#define ALL_TID_TC_SUPPORTED_UAPSD 0xff

struct	qos_priv	{

	unsigned int	  qos_option;	/* bit mask option: u-apsd, s-apsd, ts, block ack...		 */

#ifdef CONFIG_WMMPS_STA
	/* uapsd (unscheduled automatic power-save delivery) = a kind of wmmps */
	u8 uapsd_max_sp_len;
	/* declare uapsd_tid as a bitmap for the uapsd setting of TID 0~15 */
	u16 uapsd_tid;
	/* declare uapsd_tid_delivery_enabled as a bitmap for the delivery-enabled setting of TID 0~7 */
	u8 uapsd_tid_delivery_enabled;
	/* declare uapsd_tid_trigger_enabled as a bitmap for the trigger-enabled setting of TID 0~7 */
	u8 uapsd_tid_trigger_enabled;
	/* declare uapsd_ap_supported to record whether the connected ap  supports uapsd or not */
	u8 uapsd_ap_supported;
#endif /* CONFIG_WMMPS_STA */	

};


#endif /* _RTL871X_QOS_H_ */