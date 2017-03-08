#include "mp_precomp.h"
#include "phydm_precomp.h"

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if ((RTL8197F_SUPPORT == 1)||(RTL8822B_SUPPORT == 1))
#include "rtl8197f/Hal8197FPhyReg.h"
#include "WlanHAL/HalMac88XX/halmac_reg2.h"
#else
#include "WlanHAL/HalHeader/HalComReg.h"
#endif
#endif

#if (PHYDM_LA_MODE_SUPPORT == 1)

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)

#if WPP_SOFTWARE_TRACE
#include "phydm_adc_sampling.tmh"
#endif


BOOLEAN
phydm_la_buffer_allocate(
	IN		PVOID			pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_ADCSMP		AdcSmp = &(pDM_Odm->adcsmp);
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PRT_ADCSMP_STRING	ADCSmpBuf = &(AdcSmp->ADCSmpBuf);

	DbgPrint("[LA mode BufferAllocate]\n");
	
	if (ADCSmpBuf->Length == 0) {
		if (PlatformAllocateMemoryWithZero(Adapter, (void **)&(ADCSmpBuf->Octet), ADCSmpBuf->buffer_size) == RT_STATUS_SUCCESS)
			ADCSmpBuf->Length = ADCSmpBuf->buffer_size;
		else
			return FALSE;
	}

	return TRUE;
}
#endif

VOID
phydm_la_get_tx_pkt_buf(
	IN		PVOID			pDM_VOID
	)
{
	PDM_ODM_T			pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_ADCSMP			AdcSmp = &(pDM_Odm->adcsmp);
	PRT_ADCSMP_STRING	ADCSmpBuf = &(AdcSmp->ADCSmpBuf);
	u4Byte				i = 0, value32, DataL = 0, DataH = 0;
	u4Byte				Addr, Finish_Addr;
	u4Byte				End_Addr = (ADCSmpBuf->start_pos  + ADCSmpBuf->buffer_size)-1;	/*End_Addr = 0x3ffff;*/
	BOOLEAN				bRoundUp;
	static u4Byte			page = 0xFF;
	u4Byte				smp_cnt = 0, smp_number = 0, Addr_8byte = 0;

	ODM_Memory_Set(pDM_Odm, ADCSmpBuf->Octet, 0, ADCSmpBuf->Length);
	ODM_Write1Byte(pDM_Odm, 0x0106, 0x69);

	DbgPrint("GetTxPktBuf\n");

	value32 = ODM_Read4Byte(pDM_Odm, 0x7c0);
	bRoundUp = (BOOLEAN)((value32 & BIT31) >> 31);
	Finish_Addr = (value32 & 0x7FFF0000) >> 16;	/*Reg7C0[30:16]: finish addr (unit: 8byte)*/

	if(bRoundUp) {
		Addr = (Finish_Addr+1)<<3;
		DbgPrint("bRoundUp = ((%d)), Finish_Addr=((0x%x)), 0x7c0=((0x%x)) \n", bRoundUp, Finish_Addr, value32);
		smp_number = ((ADCSmpBuf->buffer_size)>>3);	/*Byte to 64Byte*/
	} else	 {
		Addr = ADCSmpBuf->start_pos;
		
		Addr_8byte = Addr>>3;
		if(Addr_8byte > Finish_Addr)
			smp_number = Addr_8byte - Finish_Addr;
		else
			smp_number = Finish_Addr - Addr_8byte;

		DbgPrint("bRoundUp = ((%d)), Finish_Addr=((0x%x * 8Byte)), Start_Addr = ((0x%x * 8Byte)), smp_number = ((%d))\n", bRoundUp, Finish_Addr, Addr_8byte, smp_number);
		
	}
	/*	
	DbgPrint("bRoundUp = %d, Finish_Addr=0x%x, value32=0x%x\n", bRoundUp, Finish_Addr, value32);
	DbgPrint("End_Addr = %x, ADCSmpBuf->start_pos = 0x%x, ADCSmpBuf->buffer_size = 0x%x\n", End_Addr, ADCSmpBuf->start_pos, ADCSmpBuf->buffer_size);
	*/
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	watchdog_stop(pDM_Odm->priv);
#endif

	if (pDM_Odm->SupportICType & ODM_RTL8197F) {
		for (Addr = 0x0, i = 0; Addr < End_Addr; Addr += 8, i += 2) {	/*64K byte*/
			if ((Addr&0xfff) == 0)
				ODM_SetBBReg(pDM_Odm, 0x0140, bMaskLWord, 0x780+(Addr >> 12));
			DataL = ODM_GetBBReg(pDM_Odm, 0x8000+(Addr&0xfff), bMaskDWord);
			DataH = ODM_GetBBReg(pDM_Odm, 0x8000+(Addr&0xfff)+4, bMaskDWord);

			DbgPrint("%08x%08x\n", DataH, DataL);		
		}
	} else {
		while (Addr != (Finish_Addr<<3)) {
			if (page != (Addr >> 12)) {
				/*Reg140=0x780+(Addr>>12), Addr=0x30~0x3F, total 16 pages*/
				page = (Addr >> 12);
			}
			ODM_SetBBReg(pDM_Odm, 0x0140, bMaskLWord, 0x780+page);

			/*pDataL = 0x8000+(Addr&0xfff);*/
			DataL = ODM_GetBBReg(pDM_Odm, 0x8000+(Addr&0xfff), bMaskDWord);
			DataH = ODM_GetBBReg(pDM_Odm, 0x8000+(Addr&0xfff)+4, bMaskDWord);

			#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			ADCSmpBuf->Octet[i] = DataH;
			ADCSmpBuf->Octet[i+1] = DataL;
			#endif

			#if DBG
			DbgPrint("%08x%08x\n", DataH, DataL);
			#else
				#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
				RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("%08x%08x\n", ADCSmpBuf->Octet[i], ADCSmpBuf->Octet[i+1]));
				#endif
			#endif
			
			i = i + 2;
			
			if ((Addr+8) >= End_Addr)
				Addr = ADCSmpBuf->start_pos;
			else
				Addr = Addr + 8;

			smp_cnt ++;
			if (smp_cnt >= (smp_number-1))
				break;
		}
		DbgPrint("smp_cnt = ((%d))\n", smp_cnt);
		#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("smp_cnt = ((%d))\n", smp_cnt));
		#endif
	}
	
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	watchdog_resume(pDM_Odm->priv);
#endif
}	

VOID
phydm_la_mode_set_mac_iq_dump(
	IN	PVOID		pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_ADCSMP		AdcSmp = &(pDM_Odm->adcsmp);
	u4Byte			reg_value;

	ODM_Write1Byte(pDM_Odm, 0x7c0, 0);		/*clear all 0x7c0*/
	ODM_SetMACReg(pDM_Odm, 0x7c0, BIT0, 1);  /*Enable LA mode HW block*/

	if (AdcSmp->la_trig_mode == PHYDM_MAC_TRIG) {

		AdcSmp->is_bb_trigger = 0;
		ODM_SetMACReg(pDM_Odm, 0x7c0, BIT2, 1); /*polling bit for MAC mode*/
		ODM_SetMACReg(pDM_Odm, 0x7c0, BIT4|BIT3, AdcSmp->la_trigger_edge); /*trigger mode for MAC*/
		
		DbgPrint("[MAC_trig] ref_mask = ((0x%x)), ref_value = ((0x%x)), dbg_port = ((0x%x))\n", AdcSmp->la_mac_ref_mask, AdcSmp->la_TrigSigSel, AdcSmp->la_dbg_port);
		/*[Set MAC Debug Port]*/
		ODM_SetMACReg(pDM_Odm, 0xF4, BIT16, 1);
		ODM_SetMACReg(pDM_Odm, 0x38, 0xff0000, AdcSmp->la_dbg_port);
		ODM_SetMACReg(pDM_Odm, 0x7c4, bMaskDWord, AdcSmp->la_mac_ref_mask);
		ODM_SetMACReg(pDM_Odm, 0x7c8, bMaskDWord, AdcSmp->la_TrigSigSel);
		
	} else {
	
		AdcSmp->is_bb_trigger = 1;
		ODM_SetMACReg(pDM_Odm, 0x7c0, BIT1, 1); /*polling bit for BB ADC mode*/

		if (AdcSmp->la_trig_mode == PHYDM_ADC_MAC_TRIG) {

			ODM_SetMACReg(pDM_Odm, 0x7c0, BIT3, 1); /*polling bit for MAC trigger event*/
			ODM_SetMACReg(pDM_Odm, 0x7c0, BIT7|BIT6, AdcSmp->la_TrigSigSel);
			
			if (AdcSmp->la_TrigSigSel == ADCSMP_TRIG_REG)			
				ODM_SetMACReg(pDM_Odm, 0x7c0, BIT5, 1); /* manual trigger 0x7C0[5] = 0 -> 1*/
		} 
	}

	reg_value = ODM_GetBBReg(pDM_Odm, 0x7c0, 0xff);
	DbgPrint("4. [Set MAC IQ dump] 0x7c0[7:0] = ((0x%x))\n", reg_value);
	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("4. [Set MAC IQ dump] 0x7c0[7:0] = ((0x%x))\n", reg_value));
	#endif	
	
}

void
phydm_la_mode_set_dma_type(
	IN	PVOID		pDM_VOID,
	IN	u1Byte		la_dma_type
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

	DbgPrint("2. [LA mode DMA setting] Dma_type = ((%d))\n", la_dma_type);
	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("2. [LA mode DMA setting] Dma_type = ((%d))\n", la_dma_type));
	#endif	

	if (pDM_Odm->SupportICType & ODM_N_ANTDIV_SUPPORT)
		ODM_SetBBReg(pDM_Odm, 0x9a0, 0xf00, la_dma_type);	/*0x9A0[11:8]*/
	else
		ODM_SetBBReg(pDM_Odm , ODM_ADC_TRIGGER_Jaguar2, 0xf00, la_dma_type);	/*0x95C[11:8]*/
}

VOID
phydm_adc_smp_start(
	IN		PVOID			pDM_VOID
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_ADCSMP				AdcSmp = &(pDM_Odm->adcsmp);	
	u1Byte					tmpU1b;
	u1Byte					backup_DMA, while_cnt = 0;
	u1Byte					polling_ok = FALSE, target_polling_bit;

	phydm_la_mode_bb_setting(pDM_Odm);
	phydm_la_mode_set_dma_type(pDM_Odm, AdcSmp->la_dma_type);
	phydm_la_mode_set_trigger_time(pDM_Odm, AdcSmp->la_TriggerTime);	

	if (pDM_Odm->SupportICType & ODM_RTL8197F)
		ODM_SetBBReg(pDM_Odm, 0xd00, BIT26, 0x1);
	else {	/*for 8814A and 8822B?*/
		ODM_Write1Byte(pDM_Odm, 0x198c, 0x7);
		ODM_Write1Byte(pDM_Odm, 0x8b4, 0x80);
		//ODM_SetBBReg(pDM_Odm, 0x8b4, BIT7, 1);
	}
	
	phydm_la_mode_set_mac_iq_dump(pDM_Odm);
//return;
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	watchdog_stop(pDM_Odm->priv);
#endif

	target_polling_bit = (AdcSmp->is_bb_trigger) ? BIT1 : BIT2;
	do { /*Polling time always use 100ms, when it exceed 2s, break while loop*/
		tmpU1b = ODM_Read1Byte(pDM_Odm, 0x7c0);

		if (AdcSmp->ADCSmpState != ADCSMP_STATE_SET) {
			DbgPrint("[State Error] ADCSmpState != ADCSMP_STATE_SET\n");
			break;
			
		} else if (tmpU1b & target_polling_bit) {
			ODM_delay_ms(100);
			while_cnt = while_cnt + 1;
			continue;
		} else {
			DbgPrint("[LA Query OK] polling_bit=((0x%x))\n", target_polling_bit);
			polling_ok = TRUE;
			if (pDM_Odm->SupportICType & ODM_RTL8197F)
				ODM_SetBBReg(pDM_Odm, 0x7c0, BIT0, 0x0);
			break;
		}
	} while (while_cnt < 20);
	
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	watchdog_resume(pDM_Odm->priv);
#if (RTL8197F_SUPPORT)
		if (pDM_Odm->SupportICType & ODM_RTL8197F) {
			/*Stop DMA*/
			backup_DMA = ODM_GetMACReg(pDM_Odm, 0x300, bMaskLWord);
			ODM_SetMACReg(pDM_Odm, 0x300, 0x7fff, backup_DMA|0x7fff);
			
			/*move LA mode content from IMEM to TxPktBuffer 
				Src : OCPBASE_IMEM 0x00000000
				Dest : OCPBASE_TXBUF 0x18780000
				Len : 64K*/
			GET_HAL_INTERFACE(pDM_Odm->priv)->InitDDMAHandler(pDM_Odm->priv, OCPBASE_IMEM, OCPBASE_TXBUF, 0x10000);
		}
#endif
#endif

	if (AdcSmp->ADCSmpState == ADCSMP_STATE_SET) {

		if (polling_ok)
			phydm_la_get_tx_pkt_buf(pDM_Odm);
		else {
			DbgPrint("[Polling timeout]\n");
		}	
	}

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	if (pDM_Odm->SupportICType & ODM_RTL8197F) 
		ODM_SetMACReg(pDM_Odm, 0x300, 0x7fff, backup_DMA);	/*Resume DMA*/
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	if (AdcSmp->ADCSmpState == ADCSMP_STATE_SET)
		AdcSmp->ADCSmpState = ADCSMP_STATE_QUERY;
#endif

	DbgPrint("[LA mode] LA_pattern_count = ((%d))\n", AdcSmp->la_count);
	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("[LA mode] la_count = ((%d))\n", AdcSmp->la_count));
	#endif

	
	ADCSmp_Stop(pDM_Odm);
	
	if (AdcSmp->la_count == 0) {
		DbgPrint("LA Dump finished ---------->\n\n\n");
		/**/
	} else {
		AdcSmp->la_count --;
		DbgPrint("LA Dump more ---------->\n\n\n");		
		ADCSmp_Set(pDM_Odm, AdcSmp->la_trig_mode, AdcSmp->la_TrigSigSel, AdcSmp->la_dma_type, AdcSmp->la_TriggerTime, 0);
	}

}

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
VOID
ADCSmpWorkItemCallback(
	IN	PVOID	pContext
	)
{
	PADAPTER			Adapter = (PADAPTER)pContext;
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	PRT_ADCSMP		AdcSmp = &(pDM_Odm->adcsmp);

	DbgPrint("[WorkItem Call back] LA_State=((%d))\n", AdcSmp->ADCSmpState);
	phydm_adc_smp_start(pDM_Odm);
}
#endif

VOID
ADCSmp_Set(
	IN	PVOID	pDM_VOID,
	IN	u1Byte	trig_mode,
	IN	u4Byte	TrigSigSel,
	IN	u1Byte	DmaDataSigSel,
	IN	u4Byte	TriggerTime,
	IN	u2Byte	PollingTime
	)
{
	PDM_ODM_T			pDM_Odm = (PDM_ODM_T)pDM_VOID;
	BOOLEAN				is_set_success = TRUE;
	PRT_ADCSMP			AdcSmp = &(pDM_Odm->adcsmp);

	AdcSmp->la_trig_mode = trig_mode;
	AdcSmp->la_TrigSigSel = TrigSigSel;
	AdcSmp->la_dma_type = DmaDataSigSel;
	AdcSmp->la_TriggerTime = TriggerTime;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	if (AdcSmp->ADCSmpState != ADCSMP_STATE_IDLE)
		is_set_success = FALSE;
	else if (AdcSmp->ADCSmpBuf.Length == 0)
		is_set_success = phydm_la_buffer_allocate(pDM_Odm);
#endif

	if (is_set_success) {
		AdcSmp->ADCSmpState = ADCSMP_STATE_SET;
		
		DbgPrint("[LA Set Success] LA_State=((%d))\n", AdcSmp->ADCSmpState);
		
		#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)

		DbgPrint("ADCSmp_work_item_index = ((%d))\n", AdcSmp->la_work_item_index);
		if (AdcSmp->la_work_item_index != 0) {
			ODM_ScheduleWorkItem(&(AdcSmp->ADCSmpWorkItem_1));
			AdcSmp->la_work_item_index = 0;
		} else {
			ODM_ScheduleWorkItem(&(AdcSmp->ADCSmpWorkItem));
			AdcSmp->la_work_item_index = 1;
		}
		#else
		phydm_adc_smp_start(pDM_Odm);
		#endif
	} else {
		DbgPrint("[LA Set Fail] LA_State=((%d))\n", AdcSmp->ADCSmpState);
	}	

	
}

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
RT_STATUS
ADCSmp_Query(
	IN	PVOID				pDM_VOID,
	IN	ULONG				InformationBufferLength, 
	OUT	PVOID				InformationBuffer, 
	OUT	PULONG				BytesWritten
	)
{
	PDM_ODM_T			pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_ADCSMP			AdcSmp = &(pDM_Odm->adcsmp);
	RT_STATUS			retStatus = RT_STATUS_SUCCESS;
	PRT_ADCSMP_STRING	ADCSmpBuf = &(AdcSmp->ADCSmpBuf);

	DbgPrint("[%s] LA_State=((%d))", __func__, AdcSmp->ADCSmpState);

	if (InformationBufferLength != ADCSmpBuf->buffer_size)	{
		*BytesWritten = 0;
		retStatus = RT_STATUS_RESOURCE;
	} else if (ADCSmpBuf->Length != ADCSmpBuf->buffer_size) {
		*BytesWritten = 0;
		retStatus = RT_STATUS_RESOURCE;
	} else if (AdcSmp->ADCSmpState != ADCSMP_STATE_QUERY) {
		*BytesWritten = 0;
		retStatus = RT_STATUS_PENDING;
	} else {
		ODM_MoveMemory(pDM_Odm, InformationBuffer, ADCSmpBuf->Octet, ADCSmpBuf->buffer_size);
		*BytesWritten = ADCSmpBuf->buffer_size;

		AdcSmp->ADCSmpState = ADCSMP_STATE_IDLE;
	}

	DbgPrint("Return Status %d\n", retStatus);

	return retStatus;
}
#endif

VOID
ADCSmp_Stop(
	IN		PVOID			pDM_VOID
	)
{
	PDM_ODM_T			pDM_Odm = (PDM_ODM_T)pDM_VOID;	
	PRT_ADCSMP			AdcSmp = &(pDM_Odm->adcsmp);

	AdcSmp->ADCSmpState = ADCSMP_STATE_IDLE;
	DbgPrint("[LA_Stop] LA_state = ((%d))\n", AdcSmp->ADCSmpState);
}

VOID
ADCSmp_Init(
	IN		PVOID			pDM_VOID
	)
{
	PDM_ODM_T			pDM_Odm = (PDM_ODM_T)pDM_VOID;	
	PRT_ADCSMP			AdcSmp = &(pDM_Odm->adcsmp);
	PRT_ADCSMP_STRING	ADCSmpBuf = &(AdcSmp->ADCSmpBuf);

	AdcSmp->ADCSmpState = ADCSMP_STATE_IDLE;

	if (pDM_Odm->SupportICType & ODM_RTL8814A) {
		ADCSmpBuf->start_pos = 0x30000;
		ADCSmpBuf->buffer_size = 0x10000;	
	} else if (pDM_Odm->SupportICType & ODM_RTL8822B) {
		ADCSmpBuf->start_pos = 0x20000;
		ADCSmpBuf->buffer_size = 0x20000;	
	} else if (pDM_Odm->SupportICType & ODM_RTL8197F) {
		ADCSmpBuf->start_pos = 0x00000;
		ADCSmpBuf->buffer_size = 0x10000;	
	} else if (pDM_Odm->SupportICType & ODM_RTL8821C) {
		ADCSmpBuf->start_pos = 0x8000;
		ADCSmpBuf->buffer_size = 0x8000;	
	}
	
}

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
VOID
ADCSmp_DeInit(
	IN		PVOID			pDM_VOID
	)
{
	PDM_ODM_T			pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_ADCSMP			AdcSmp = &(pDM_Odm->adcsmp);
	PRT_ADCSMP_STRING	ADCSmpBuf = &(AdcSmp->ADCSmpBuf);

	ADCSmp_Stop(pDM_Odm);

	if (ADCSmpBuf->Length != 0x0) {
		ODM_FreeMemory(pDM_Odm, ADCSmpBuf->Octet, ADCSmpBuf->Length);
		ADCSmpBuf->Length = 0x0;
	}
}	

#endif


VOID
phydm_la_mode_bb_setting(
	IN	PVOID		pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_ADCSMP		AdcSmp = &(pDM_Odm->adcsmp);

	u1Byte	trig_mode = AdcSmp->la_trig_mode;
	u4Byte	TrigSigSel = AdcSmp->la_TrigSigSel;
	u4Byte	DbgPort = AdcSmp->la_dbg_port;
	u1Byte	bTriggerEdge = AdcSmp->la_trigger_edge;
	u1Byte	sampling_rate = AdcSmp->la_smp_rate;

	DbgPrint("1. [LA mode bb_setting] trig_mode = ((%d)), DbgPort = ((0x%x)), Trig_Edge = ((%d)), smp_rate = ((%d)), Trig_Sel = ((0x%x))\n",
		trig_mode, DbgPort, bTriggerEdge, sampling_rate, TrigSigSel);

	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("1. [LA mode bb_setting]trig_mode = ((%d)), DbgPort = ((0x%x)), Trig_Edge = ((%d)), smp_rate = ((%d)), Trig_Sel = ((0x%x))\n",
		trig_mode, DbgPort, bTriggerEdge, sampling_rate, TrigSigSel));
	#endif

	if (trig_mode == PHYDM_MAC_TRIG)
		TrigSigSel = 0; /*ignore this setting*/

	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {

		if (trig_mode == PHYDM_ADC_RF0_TRIG) {
			ODM_SetBBReg(pDM_Odm, 0x8f8, BIT25|BIT24|BIT23|BIT22, 9);	/*DBGOUT_RFC_a[31:0]*/
		} else if (trig_mode == PHYDM_ADC_RF1_TRIG) {
			ODM_SetBBReg(pDM_Odm, 0x8f8, BIT25|BIT24|BIT23|BIT22, 8); 	/*DBGOUT_RFC_b[31:0]*/
		} else {
			ODM_SetBBReg(pDM_Odm, 0x8f8, BIT25|BIT24|BIT23|BIT22, 0);
		}
		/*
			(0:) '{ofdm_dbg[31:0]}'
			(1:) '{cca,crc32_fail,dbg_ofdm[29:0]}'
			(2:) '{vbon,crc32_fail,dbg_ofdm[29:0]}'
			(3:) '{cca,crc32_ok,dbg_ofdm[29:0]}'
			(4:) '{vbon,crc32_ok,dbg_ofdm[29:0]}'
			(5:) '{dbg_iqk_anta}'
			(6:) '{cca,ofdm_crc_ok,dbg_dp_anta[29:0]}'
			(7:) '{dbg_iqk_antb}'
			(8:) '{DBGOUT_RFC_b[31:0]}'
			(9:) '{DBGOUT_RFC_a[31:0]}'
			(a:) '{dbg_ofdm}'
			(b:) '{dbg_cck}'
		*/
		
		ODM_SetBBReg(pDM_Odm, 0x198C , BIT2|BIT1|BIT0, 7); /*disable dbg clk gating*/

		/*dword= ODM_GetBBReg(pDM_Odm, 0x8FC, bMaskDWord);*/
		/*DbgPrint("dbg_port = ((0x%x))\n", dword);*/
		ODM_SetBBReg(pDM_Odm , 0x95C, 0x1f, TrigSigSel);	/*0x95C[4:0], BB debug port bit*/
		ODM_SetBBReg(pDM_Odm, 0x8FC, bMaskDWord, DbgPort);
		ODM_SetBBReg(pDM_Odm, 0x95C , BIT31, bTriggerEdge); /*0: posedge, 1: negedge*/
		ODM_SetBBReg(pDM_Odm, 0x95c, 0xe0, sampling_rate);
		/*	(0:) '80MHz'
			(1:) '40MHz'
			(2:) '20MHz'
			(3:) '10MHz'
			(4:) '5MHz'
			(5:) '2.5MHz'
			(6:) '1.25MHz'
			(7:) '160MHz (for BW160 ic)'
		*/
	} else {
		ODM_SetBBReg(pDM_Odm, 0x9a0, 0x1f, TrigSigSel);	/*0x9A0[4:0], BB debug port bit*/
		ODM_SetBBReg(pDM_Odm, 0x908, bMaskDWord, DbgPort);
		ODM_SetBBReg(pDM_Odm, 0x9A0 , BIT31, bTriggerEdge); /*0: posedge, 1: negedge*/
		ODM_SetBBReg(pDM_Odm, 0x9A0, 0xe0, sampling_rate);
		/*	(0:) '80MHz'
			(1:) '40MHz'
			(2:) '20MHz'
			(3:) '10MHz'
			(4:) '5MHz'
			(5:) '2.5MHz'
			(6:) '1.25MHz'
			(7:) '160MHz (for BW160 ic)'
		*/
	}
}

void
phydm_la_mode_set_trigger_time(
	IN	PVOID		pDM_VOID,
	IN	u4Byte		TriggerTime_mu_sec
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte			TriggerTime_unit_num;
	u4Byte			time_unit = 0;

	if (TriggerTime_mu_sec < 128) {
		time_unit = 0; /*unit: 1mu sec*/
	} else if (TriggerTime_mu_sec < 256) {
		time_unit = 1; /*unit: 2mu sec*/	
	} else if (TriggerTime_mu_sec < 512) {
		time_unit = 2; /*unit: 4mu sec*/	
	} else if (TriggerTime_mu_sec < 1024) {
		time_unit = 3; /*unit: 8mu sec*/	
	} else if (TriggerTime_mu_sec < 2048) {
		time_unit = 4; /*unit: 16mu sec*/	
	} else if (TriggerTime_mu_sec < 4096) {
		time_unit = 5; /*unit: 32mu sec*/	
	} else if (TriggerTime_mu_sec < 8192) {
		time_unit = 6; /*unit: 64mu sec*/	
	}
	
	TriggerTime_unit_num = (u1Byte)(TriggerTime_mu_sec>>time_unit);
	
	DbgPrint("3. [Set Trigger Time] Trig_Time = ((%d)) * unit = ((2^%d us))\n", TriggerTime_unit_num, time_unit);
	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("3. [Set Trigger Time] Trig_Time = ((%d)) * unit = ((2^%d us))\n", TriggerTime_unit_num, time_unit));
	#endif	
	
	ODM_SetMACReg(pDM_Odm, 0x7cc , BIT20|BIT19|BIT18, time_unit);
	ODM_SetMACReg(pDM_Odm, 0x7c0, 0x7f00, (TriggerTime_unit_num& 0x7f));
	
}


VOID
phydm_lamode_trigger_setting(
	IN		PVOID		pDM_VOID,
	IN 		char			input[][16],
	IN		u4Byte		*_used,
	OUT		char			*output,
	IN		u4Byte		*_out_len,
	IN 		u4Byte		input_num
	)
	{
		PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
		PRT_ADCSMP	AdcSmp = &(pDM_Odm->adcsmp);
		u1Byte		trig_mode, DmaDataSigSel;
		u4Byte		TrigSigSel;
		BOOLEAN		bEnableLaMode, bTriggerEdge;
		u4Byte		DbgPort, TriggerTime_mu_sec;
		u4Byte		mac_ref_signal_mask;
		u1Byte		sampling_rate = 0, i;
		char 		help[] = "-h";
		u4Byte 			var1[10] = {0};	
		u4Byte used = *_used;
		u4Byte out_len = *_out_len;		
		
		if (pDM_Odm->SupportICType & PHYDM_IC_SUPPORT_LA_MODE) {
			
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
			bEnableLaMode = (BOOLEAN)var1[0];
			/*DbgPrint("echo cmd input_num = %d\n", input_num);*/

			if ((strcmp(input[1], help) == 0)) {
				PHYDM_SNPRINTF((output+used, out_len-used, "{En} {0:BB,1:BB_MAC,2:RF0,3:RF1,4:MAC} \n {BB:DbgPort[bit],BB_MAC:0-ok/1-fail/2-cca,MAC:ref} {DMA type} {TrigTime} \n {PollingTime/ref_mask} {DbgPort} {0:P_Edge, 1:N_Edge} {SpRate:0-80M,1-40M,2-20M} {Capture num}\n"));
				/**/
			} else if ((bEnableLaMode == 1)) {
			
				PHYDM_SSCANF(input[2], DCMD_DECIMAL, &var1[1]);
				
				trig_mode = (u1Byte)var1[1];
				
				if (trig_mode == PHYDM_MAC_TRIG) {
					PHYDM_SSCANF(input[3], DCMD_HEX, &var1[2]);
				} else {
					PHYDM_SSCANF(input[3], DCMD_DECIMAL, &var1[2]);
				}
				TrigSigSel = var1[2];
				
				PHYDM_SSCANF(input[4], DCMD_DECIMAL, &var1[3]);
				PHYDM_SSCANF(input[5], DCMD_DECIMAL, &var1[4]);
				PHYDM_SSCANF(input[6], DCMD_HEX, &var1[5]);
				PHYDM_SSCANF(input[7], DCMD_HEX, &var1[6]);
				PHYDM_SSCANF(input[8], DCMD_DECIMAL, &var1[7]);
				PHYDM_SSCANF(input[9], DCMD_DECIMAL, &var1[8]);
				PHYDM_SSCANF(input[10], DCMD_DECIMAL, &var1[9]);

				DmaDataSigSel = (u1Byte)var1[3];
				TriggerTime_mu_sec = var1[4]; /*unit: us*/

				AdcSmp->la_mac_ref_mask = var1[5];
				AdcSmp->la_dbg_port = var1[6];
				AdcSmp->la_trigger_edge = (u1Byte) var1[7];
				AdcSmp->la_smp_rate = (u1Byte)(var1[8] & 0x7);
				AdcSmp->la_count = var1[9];


				DbgPrint("echo lamode %d %d %d %d %d %d %x %d %d %d\n", var1[0], var1[1], var1[2], var1[3], var1[4], var1[5], var1[6], var1[7], var1[8], var1[9]);
				#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
				RT_TRACE_EX(COMP_LA_MODE, DBG_LOUD, ("echo lamode %d %d %d %d %d %d %x %d %d %d\n", var1[0], var1[1], var1[2], var1[3], var1[4], var1[5], var1[6], var1[7], var1[8], var1[9]));
				#endif
				
				PHYDM_SNPRINTF((output+used, out_len-used, "a.En= ((1)),  b.mode = ((%d)), c.Trig_Sel = ((0x%x)), d.Dma_type = ((%d))\n", trig_mode, TrigSigSel, DmaDataSigSel));
				PHYDM_SNPRINTF((output+used, out_len-used, "e.Trig_Time = ((%dus)), f.mac_ref_mask = ((0x%x)), g.dbg_port = ((0x%x))\n", TriggerTime_mu_sec, AdcSmp->la_mac_ref_mask, AdcSmp->la_dbg_port));
				PHYDM_SNPRINTF((output+used, out_len-used, "h.Trig_edge = ((%d)), i.smp rate = ((%d MHz)), j.Cap_num = ((%d))\n", AdcSmp->la_trigger_edge, (80>>AdcSmp->la_smp_rate), AdcSmp->la_count ));

				ADCSmp_Set(pDM_Odm, trig_mode, TrigSigSel, DmaDataSigSel, TriggerTime_mu_sec, 0);

			} else {
				ADCSmp_Stop(pDM_Odm);
				PHYDM_SNPRINTF((output+used, out_len-used, "Disable LA mode\n"));
			}
		} 
	}

#endif	/*endif PHYDM_LA_MODE_SUPPORT == 1*/

