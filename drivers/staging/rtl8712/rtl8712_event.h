/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef _RTL8712_EVENT_H_
#define _RTL8712_EVENT_H_

void r8712_event_handle(struct _adapter *padapter, __le32 *peventbuf);
void r8712_got_addbareq_event_callback(struct _adapter *adapter, u8 *pbuf);

enum rtl8712_c2h_event {
	GEN_EVT_CODE(_Read_MACREG) = 0,		/*0*/
	GEN_EVT_CODE(_Read_BBREG),
	GEN_EVT_CODE(_Read_RFREG),
	GEN_EVT_CODE(_Read_EEPROM),
	GEN_EVT_CODE(_Read_EFUSE),
	GEN_EVT_CODE(_Read_CAM),		/*5*/
	GEN_EVT_CODE(_Get_BasicRate),
	GEN_EVT_CODE(_Get_DataRate),
	GEN_EVT_CODE(_Survey),			/*8*/
	GEN_EVT_CODE(_SurveyDone),		/*9*/

	GEN_EVT_CODE(_JoinBss),			/*10*/
	GEN_EVT_CODE(_AddSTA),
	GEN_EVT_CODE(_DelSTA),
	GEN_EVT_CODE(_AtimDone),
	GEN_EVT_CODE(_TX_Report),
	GEN_EVT_CODE(_CCX_Report),		/*15*/
	GEN_EVT_CODE(_DTM_Report),
	GEN_EVT_CODE(_TX_Rate_Statistics),
	GEN_EVT_CODE(_C2HLBK),
	GEN_EVT_CODE(_FWDBG),
	GEN_EVT_CODE(_C2HFEEDBACK),		/*20*/
	GEN_EVT_CODE(_ADDBA),
	GEN_EVT_CODE(_C2HBCN),
	GEN_EVT_CODE(_ReportPwrState),		/*filen: only for PCIE, USB*/
	GEN_EVT_CODE(_WPS_PBC),			/*24*/
	GEN_EVT_CODE(_ADDBAReq_Report),		/*25*/
	MAX_C2HEVT
};

#ifdef _RTL8712_CMD_C_

static struct fwevent wlanevents[] = {
	{0, NULL},	/*0*/
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, &r8712_survey_event_callback},		/*8*/
	{sizeof(struct surveydone_event),
		&r8712_surveydone_event_callback},	/*9*/

	{0, &r8712_joinbss_event_callback},		/*10*/
	{sizeof(struct stassoc_event), &r8712_stassoc_event_callback},
	{sizeof(struct stadel_event), &r8712_stadel_event_callback},
	{0, &r8712_atimdone_event_callback},
	{0, NULL},
	{0, NULL},	/*15*/
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},	/*fwdbg_event_callback},*/
	{0, NULL},	/*20*/
	{0, NULL},
	{0, NULL},
	{0, &r8712_cpwm_event_callback},
	{0, &r8712_wpspbc_event_callback},
	{0, &r8712_got_addbareq_event_callback},
};

#endif/*_RTL8712_CMD_C_*/

#endif
