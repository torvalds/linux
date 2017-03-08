#ifndef __INC_ADCSMP_H
#define __INC_ADCSMP_H

#define DYNAMIC_LA_MODE	"1.0"  /*2016.07.15  Dino */

#if (PHYDM_LA_MODE_SUPPORT == 1)

typedef struct _RT_ADCSMP_STRING {
	pu4Byte		Octet;
	u4Byte		Length;
	u4Byte		buffer_size;
	u4Byte		start_pos;
} RT_ADCSMP_STRING, *PRT_ADCSMP_STRING;


typedef enum _RT_ADCSMP_TRIG_SEL {
	PHYDM_ADC_BB_TRIG	= 0, 
	PHYDM_ADC_MAC_TRIG	= 1, 
	PHYDM_ADC_RF0_TRIG	= 2, 
	PHYDM_ADC_RF1_TRIG	= 3, 
	PHYDM_MAC_TRIG		= 4 
} RT_ADCSMP_TRIG_SEL, *PRT_ADCSMP_TRIG_SEL;


typedef enum _RT_ADCSMP_TRIG_SIG_SEL {
	ADCSMP_TRIG_CRCOK	= 0, 
	ADCSMP_TRIG_CRCFAIL	= 1, 
	ADCSMP_TRIG_CCA		= 2,
	ADCSMP_TRIG_REG		= 3
} RT_ADCSMP_TRIG_SIG_SEL, *PRT_ADCSMP_TRIG_SIG_SEL;


typedef enum _RT_ADCSMP_STATE {
	ADCSMP_STATE_IDLE		= 0, 
	ADCSMP_STATE_SET		= 1, 
	ADCSMP_STATE_QUERY	=	2
} RT_ADCSMP_STATE, *PRT_ADCSMP_STATE;


typedef struct _RT_ADCSMP {
	RT_ADCSMP_STRING		ADCSmpBuf;
	RT_ADCSMP_STATE		ADCSmpState;
	u1Byte					la_trig_mode;
	u4Byte					la_TrigSigSel;
	u1Byte					la_dma_type;
	u4Byte					la_TriggerTime;
	u4Byte					la_mac_ref_mask;
	u4Byte					la_dbg_port;
	u1Byte					la_trigger_edge;
	u1Byte					la_smp_rate;
	u4Byte					la_count;
	u1Byte					is_bb_trigger;
	u1Byte					la_work_item_index;

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	
	RT_WORK_ITEM	ADCSmpWorkItem;
	RT_WORK_ITEM	ADCSmpWorkItem_1;
	#endif	
} RT_ADCSMP, *PRT_ADCSMP;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
VOID
ADCSmpWorkItemCallback(
	IN	PVOID	pContext
);
#endif

VOID
ADCSmp_Set(
	IN	PVOID	pDM_VOID,
	IN	u1Byte	trig_mode,
	IN	u4Byte	TrigSigSel,
	IN	u1Byte	DmaDataSigSel,
	IN	u4Byte	TriggerTime,
	IN	u2Byte	PollingTime
);

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
RT_STATUS
ADCSmp_Query(
	IN	PVOID	pDM_VOID,
	IN	ULONG	InformationBufferLength, 
	OUT	PVOID	InformationBuffer, 
	OUT	PULONG	BytesWritten
);
#endif
VOID
ADCSmp_Stop(
	IN	PVOID	pDM_VOID
);

VOID
ADCSmp_Init(
	IN	PVOID	pDM_VOID
);

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
VOID
ADCSmp_DeInit(
	IN		PVOID			pDM_VOID
);
#endif

VOID
phydm_la_mode_bb_setting(
	IN	PVOID		pDM_VOID
);

void
phydm_la_mode_set_trigger_time(
	IN	PVOID		pDM_VOID,
	IN	u4Byte		TriggerTime_mu_sec
);

VOID
phydm_lamode_trigger_setting(
	IN		PVOID		pDM_VOID,
	IN		char			input[][16],
	IN		u4Byte		*_used,
	OUT		char			*output,
	IN		u4Byte		*_out_len,
	IN		u4Byte		input_num
	);
#endif
#endif

