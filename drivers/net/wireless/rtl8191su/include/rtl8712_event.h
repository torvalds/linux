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
 *
 ******************************************************************************/ 
#ifndef _RTL8712_EVENT_H_
#define _RTL8712_EVENT_H_

void event_handle(_adapter *padapter, uint *peventbuf);


void dummy_event_callback(_adapter *adapter , u8 *pbuf);
void fwdbg_event_callback(_adapter *adapter , u8 *pbuf);
extern void got_addbareq_event_callback(_adapter *adapter , u8 *pbuf);

enum rtl8712_c2h_event
{
	GEN_EVT_CODE(_Read_MACREG)=0, /*0*/
	GEN_EVT_CODE(_Read_BBREG),
 	GEN_EVT_CODE(_Read_RFREG),
 	GEN_EVT_CODE(_Read_EEPROM),
 	GEN_EVT_CODE(_Read_EFUSE),
	GEN_EVT_CODE(_Read_CAM),			/*5*/
 	GEN_EVT_CODE(_Get_BasicRate),  
 	GEN_EVT_CODE(_Get_DataRate),   
 	GEN_EVT_CODE(_Survey),	 /*8*/
 	GEN_EVT_CODE(_SurveyDone),	 /*9*/
 	
 	GEN_EVT_CODE(_JoinBss) , /*10*/
 	GEN_EVT_CODE(_AddSTA),
 	GEN_EVT_CODE(_DelSTA),
 	GEN_EVT_CODE(_AtimDone) ,
 	GEN_EVT_CODE(_TX_Report),  
	GEN_EVT_CODE(_CCX_Report),			/*15*/
 	GEN_EVT_CODE(_DTM_Report),
 	GEN_EVT_CODE(_TX_Rate_Statistics),
 	GEN_EVT_CODE(_C2HLBK), 
 	GEN_EVT_CODE(_FWDBG),
	GEN_EVT_CODE(_C2HFEEDBACK),               /*20*/
	GEN_EVT_CODE(_ADDBA),
	GEN_EVT_CODE(_C2HBCN),
	GEN_EVT_CODE(_ReportPwrState),		//filen: only for PCIE, USB	
	GEN_EVT_CODE(_WPS_PBC),			/*24*/
	GEN_EVT_CODE(_ADDBAReq_Report),	/*25*/
	GEN_EVT_CODE(_Survey_timer),	/*26*///for softap mode, need been modified
	GEN_EVT_CODE(_OBSS_scan_timer),	/*27*/
 	MAX_C2HEVT
};


#ifdef _RTL8712_CMD_C_		

struct fwevent wlanevents[] = 
{
	{0, dummy_event_callback}, 	/*0*/
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, &survey_event_callback},		/*8*/
	{sizeof (struct surveydone_event), &surveydone_event_callback},	/*9*/
		
	{0, &joinbss_event_callback},		/*10*/
	{sizeof(struct stassoc_event), &stassoc_event_callback},
	{sizeof(struct stadel_event), &stadel_event_callback},	
	{0, &atimdone_event_callback},
	{0, dummy_event_callback},
	{0, NULL},	/*15*/
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, fwdbg_event_callback},
	{0, NULL},	 /*20*/
	{0, NULL},
	{0, NULL},	
	{0, &cpwm_event_callback},
	{0, &wpspbc_event_callback},
	{0, &got_addbareq_event_callback},
	{0, fwdbg_event_callback},//for softap mode, need been modified
	{sizeof(struct survey_timer_event), &survey_timer_event_callback},
};

#endif//_RTL8712_CMD_C_

void recv_event_bh(void *priv);

#ifdef CONFIG_MLME_EXT
int event_queuing (_adapter *padapter, struct event_node *evtnode);
#endif

#endif

