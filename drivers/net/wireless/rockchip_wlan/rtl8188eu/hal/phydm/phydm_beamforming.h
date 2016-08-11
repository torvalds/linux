#ifndef __INC_BEAMFORMING_H
#define __INC_BEAMFORMING_H

#ifndef BEAMFORMING_SUPPORT
#define	BEAMFORMING_SUPPORT		0
#endif

/*Beamforming Related*/
#include "txbf/halcomtxbf.h"
#include "txbf/haltxbfjaguar.h"
#include "txbf/haltxbf8192e.h"
#include "txbf/haltxbf8814a.h"
#include "txbf/haltxbf8821b.h"
#include "txbf/haltxbf8822b.h"
#include "txbf/haltxbfinterface.h"

#if (BEAMFORMING_SUPPORT == 1)

#define MAX_BEAMFORMEE_SU	2
#define MAX_BEAMFORMER_SU	2
#if (RTL8822B_SUPPORT == 1)
#define MAX_BEAMFORMEE_MU	6
#define MAX_BEAMFORMER_MU	1
#else
#define MAX_BEAMFORMEE_MU	0
#define MAX_BEAMFORMER_MU	0
#endif

#define BEAMFORMEE_ENTRY_NUM		(MAX_BEAMFORMEE_SU + MAX_BEAMFORMEE_MU)
#define BEAMFORMER_ENTRY_NUM		(MAX_BEAMFORMER_SU + MAX_BEAMFORMER_MU)

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
/*for different naming between WIN and CE*/
#define BEACON_QUEUE	BCN_QUEUE_INX
#define NORMAL_QUEUE	MGT_QUEUE_INX
#define RT_DISABLE_FUNC RTW_DISABLE_FUNC
#define RT_ENABLE_FUNC RTW_ENABLE_FUNC
#endif

typedef enum _BEAMFORMING_ENTRY_STATE {
	BEAMFORMING_ENTRY_STATE_UNINITIALIZE, 
	BEAMFORMING_ENTRY_STATE_INITIALIZEING, 
	BEAMFORMING_ENTRY_STATE_INITIALIZED, 
	BEAMFORMING_ENTRY_STATE_PROGRESSING, 
	BEAMFORMING_ENTRY_STATE_PROGRESSED 
} BEAMFORMING_ENTRY_STATE, *PBEAMFORMING_ENTRY_STATE;


typedef enum _BEAMFORMING_NOTIFY_STATE {
	BEAMFORMING_NOTIFY_NONE,
	BEAMFORMING_NOTIFY_ADD,
	BEAMFORMING_NOTIFY_DELETE,
	BEAMFORMEE_NOTIFY_ADD_SU,
	BEAMFORMEE_NOTIFY_DELETE_SU,
	BEAMFORMEE_NOTIFY_ADD_MU,
	BEAMFORMEE_NOTIFY_DELETE_MU,
	BEAMFORMING_NOTIFY_RESET
} BEAMFORMING_NOTIFY_STATE, *PBEAMFORMING_NOTIFY_STATE;

typedef enum _BEAMFORMING_CAP {
	BEAMFORMING_CAP_NONE = 0x0,
	BEAMFORMER_CAP_HT_EXPLICIT = BIT1, 
	BEAMFORMEE_CAP_HT_EXPLICIT = BIT2, 
	BEAMFORMER_CAP_VHT_SU = BIT5,			/* Self has er Cap, because Reg er  & peer ee */
	BEAMFORMEE_CAP_VHT_SU = BIT6,			/* Self has ee Cap, because Reg ee & peer er */
	BEAMFORMER_CAP_VHT_MU = BIT7,			/* Self has er Cap, because Reg er  & peer ee */
	BEAMFORMEE_CAP_VHT_MU = BIT8,			/* Self has ee Cap, because Reg ee & peer er */
	BEAMFORMER_CAP = BIT9,
	BEAMFORMEE_CAP = BIT10,
}BEAMFORMING_CAP, *PBEAMFORMING_CAP;


typedef enum _SOUNDING_MODE {
	SOUNDING_SW_VHT_TIMER = 0x0,
	SOUNDING_SW_HT_TIMER = 0x1, 
	SOUNDING_STOP_All_TIMER = 0x2, 
	SOUNDING_HW_VHT_TIMER = 0x3,			
	SOUNDING_HW_HT_TIMER = 0x4,
	SOUNDING_STOP_OID_TIMER = 0x5, 
	SOUNDING_AUTO_VHT_TIMER = 0x6,
	SOUNDING_AUTO_HT_TIMER = 0x7,
	SOUNDING_FW_VHT_TIMER = 0x8,
	SOUNDING_FW_HT_TIMER = 0x9,
}SOUNDING_MODE, *PSOUNDING_MODE;

typedef struct _RT_BEAMFORM_STAINFO {
	pu1Byte						RA; 
	u2Byte						AID; 
	u2Byte						MacID;
	u1Byte						MyMacAddr[6];
	WIRELESS_MODE				WirelessMode;
	CHANNEL_WIDTH				BW;
	BEAMFORMING_CAP			BeamformCap;
	u1Byte						HtBeamformCap;
	u2Byte						VhtBeamformCap;
	u1Byte						CurBeamform; 
	u2Byte						CurBeamformVHT;
} RT_BEAMFORM_STAINFO, *PRT_BEAMFORM_STAINFO;


typedef struct _RT_BEAMFORMEE_ENTRY {
	BOOLEAN bUsed;
	BOOLEAN	bTxBF;
	BOOLEAN bSound;
	u2Byte	AID;				/*Used to construct AID field of NDPA packet.*/
	u2Byte	MacId;				/*Used to Set Reg42C in IBSS mode. */
	u2Byte	P_AID;				/*Used to fill Reg42C & Reg714 to compare with P_AID of Tx DESC. */
	u2Byte	G_ID;				/*Used to fill Tx DESC*/
	u1Byte	MyMacAddr[6];
	u1Byte	MacAddr[6];			/*Used to fill Reg6E4 to fill Mac address of CSI report frame.*/
	CHANNEL_WIDTH			SoundBW;		/*Sounding BandWidth*/
	u2Byte					SoundPeriod;
	BEAMFORMING_CAP			BeamformEntryCap;
	BEAMFORMING_ENTRY_STATE	BeamformEntryState;	
	BOOLEAN						bBeamformingInProgress;
	/*u1Byte	LogSeq;									// Move to _RT_BEAMFORMER_ENTRY*/
	/*u2Byte	LogRetryCnt:3;		// 0~4				// Move to _RT_BEAMFORMER_ENTRY*/
	/*u2Byte	LogSuccessCnt:2;		// 0~2				// Move to _RT_BEAMFORMER_ENTRY*/
	u2Byte	LogStatusFailCnt:5;	// 0~21
	u2Byte	DefaultCSICnt:5;		// 0~21
	u1Byte	CSIMatrix[327];
	u2Byte	CSIMatrixLen;
	u1Byte	NumofSoundingDim;
	u1Byte	CompSteeringNumofBFer;
	u1Byte	su_reg_index;
	/*For MU-MIMO*/
	BOOLEAN	is_mu_sta;
	u1Byte	mu_reg_index;
	u1Byte	gid_valid[8];
	u1Byte	user_position[16];
} RT_BEAMFORMEE_ENTRY, *PRT_BEAMFORMEE_ENTRY;

typedef struct _RT_BEAMFORMER_ENTRY {
	BOOLEAN 			bUsed;
	/*P_AID of BFer entry is probably not used*/
	u2Byte				P_AID;					/*Used to fill Reg42C & Reg714 to compare with P_AID of Tx DESC. */
	u2Byte				G_ID;
	u1Byte				MyMacAddr[6];
	u1Byte				MacAddr[6];
	BEAMFORMING_CAP	BeamformEntryCap;
	u1Byte				NumofSoundingDim;
	u1Byte				ClockResetTimes;		/*Modified by Jeffery @2015-04-10*/
	u1Byte				PreLogSeq;				/*Modified by Jeffery @2015-03-30*/
	u1Byte				LogSeq;					/*Modified by Jeffery @2014-10-29*/
	u2Byte				LogRetryCnt:3;			/*Modified by Jeffery @2014-10-29*/
	u2Byte				LogSuccess:2;			/*Modified by Jeffery @2014-10-29*/
	u1Byte				su_reg_index;
	 /*For MU-MIMO*/
	BOOLEAN				is_mu_ap;
	u1Byte				gid_valid[8];
	u1Byte				user_position[16];
	u2Byte				AID;
} RT_BEAMFORMER_ENTRY, *PRT_BEAMFORMER_ENTRY;

typedef struct _RT_SOUNDING_INFO {
	u1Byte			SoundIdx;
	CHANNEL_WIDTH	SoundBW;
	SOUNDING_MODE	SoundMode; 
	u2Byte			SoundPeriod;
} RT_SOUNDING_INFO, *PRT_SOUNDING_INFO;



typedef struct _RT_BEAMFORMING_OID_INFO {
	u1Byte			SoundOidIdx;
	CHANNEL_WIDTH	SoundOidBW;	
	SOUNDING_MODE	SoundOidMode;
	u2Byte			SoundOidPeriod;
} RT_BEAMFORMING_OID_INFO, *PRT_BEAMFORMING_OID_INFO;


typedef struct _RT_BEAMFORMING_INFO {
	BEAMFORMING_CAP			BeamformCap;
	RT_BEAMFORMEE_ENTRY		BeamformeeEntry[BEAMFORMEE_ENTRY_NUM];
	RT_BEAMFORMER_ENTRY		BeamformerEntry[BEAMFORMER_ENTRY_NUM];
	RT_BEAMFORM_STAINFO		BeamformSTAinfo;
	u1Byte					BeamformeeCurIdx;
	RT_TIMER					BeamformingTimer;
	RT_TIMER					mu_timer;
	RT_SOUNDING_INFO			SoundingInfo;
	RT_BEAMFORMING_OID_INFO	BeamformingOidInfo;
	HAL_TXBF_INFO			TxbfInfo;
	u1Byte					SoundingSequence;
	u1Byte					beamformee_su_cnt;
	u1Byte					beamformer_su_cnt;
	u4Byte					beamformee_su_reg_maping;
	u4Byte					beamformer_su_reg_maping;
	/*For MU-MINO*/
	u1Byte					beamformee_mu_cnt;
	u1Byte					beamformer_mu_cnt;
	u4Byte					beamformee_mu_reg_maping;
	u1Byte					mu_ap_index;
	BOOLEAN					is_mu_sounding;
	u1Byte					FirstMUBFeeIndex;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER				SourceAdapter;
#endif
	/* Control register */
	u4Byte					RegMUTxCtrl;		/* For USB/SDIO interfaces aync I/O  */
} RT_BEAMFORMING_INFO, *PRT_BEAMFORMING_INFO;


typedef struct _RT_NDPA_STA_INFO {
	u2Byte	AID:12;	
	u2Byte	FeedbackType:1;
	u2Byte	NcIndex:3;	
} RT_NDPA_STA_INFO, *PRT_NDPA_STA_INFO;


BEAMFORMING_CAP
phydm_Beamforming_GetEntryBeamCapByMacId(
	IN	PVOID	pDM_VOID,
	IN 	u1Byte 	MacId
 );

PRT_BEAMFORMEE_ENTRY
phydm_Beamforming_GetBFeeEntryByAddr(
	IN	PVOID		pDM_VOID,
	IN	pu1Byte		RA,
	OUT	pu1Byte		Idx
	);

PRT_BEAMFORMER_ENTRY
phydm_Beamforming_GetBFerEntryByAddr(
	IN	PVOID	pDM_VOID,
	IN	pu1Byte	TA,
	OUT	pu1Byte	Idx
	);

u1Byte
Beamforming_GetHTNDPTxRate(
	IN	PVOID	pDM_VOID,
	u1Byte	CompSteeringNumofBFer
);

u1Byte
Beamforming_GetVHTNDPTxRate(
	IN	PVOID	pDM_VOID,
	u1Byte	CompSteeringNumofBFer
);

VOID
phydm_Beamforming_Notify(
	IN	PVOID	pDM_VOID
	);


VOID
Beamforming_Enter(
	IN PVOID		pDM_VOID,
	IN u2Byte	staIdx
	);

VOID
Beamforming_Leave(
	IN PVOID		pDM_VOID,
	pu1Byte			RA
	);

BOOLEAN
BeamformingStart_FW(
	IN PVOID			pDM_VOID,
	u1Byte			Idx
	);

VOID
Beamforming_CheckSoundingSuccess(
	IN PVOID			pDM_VOID,
	BOOLEAN			Status	
);

VOID
phydm_Beamforming_End_SW(
	IN PVOID		pDM_VOID,
	BOOLEAN			Status	
	);

VOID
Beamforming_TimerCallback(
	IN PVOID			pDM_VOID
	);

VOID
phydm_Beamforming_Init(
	IN	PVOID		pDM_VOID
	);



BEAMFORMING_CAP
phydm_Beamforming_GetBeamCap(
	IN PVOID			pDM_VOID,
	IN PRT_BEAMFORMING_INFO 	pBeamInfo
	);


BOOLEAN
BeamformingControl_V1(
	IN PVOID			pDM_VOID,
	pu1Byte			RA,
	u1Byte			AID,
	u1Byte			Mode,
	CHANNEL_WIDTH	BW,
	u1Byte			Rate
	);


BOOLEAN
phydm_BeamformingControl_V2(
	IN	PVOID		pDM_VOID,
	u1Byte			Idx,
	u1Byte			Mode, 
	CHANNEL_WIDTH	BW,
	u2Byte			Period
	);

VOID
phydm_Beamforming_Watchdog(
	IN	PVOID		pDM_VOID
	);

VOID
Beamforming_SWTimerCallback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PRT_TIMER		pTimer
#elif(DM_ODM_SUPPORT_TYPE == ODM_CE)
	void *FunctionContext
#endif
	);

BOOLEAN
Beamforming_SendHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	CHANNEL_WIDTH	BW, 
	IN	u1Byte			QIdx
	);


BOOLEAN
Beamforming_SendVHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	u2Byte			AID,
	IN	CHANNEL_WIDTH	BW,
	IN	u1Byte			QIdx
	);

#else
#define Beamforming_GidPAid(Adapter, pTcb)
#define Beamforming_Enter(pDM_Odm, staIdx)
#define Beamforming_Leave(pDM_Odm, RA)
#define Beamforming_End_FW(pDMOdm)
#define BeamformingControl_V1(pDM_Odm, RA, AID, Mode, BW, Rate)		TRUE
#define BeamformingControl_V2(pDM_Odm, Idx, Mode, BW, Period)		TRUE
#define phydm_Beamforming_End_SW(pDM_Odm, _Status)
#define Beamforming_TimerCallback(pDM_Odm)
#define phydm_Beamforming_Init(pDM_Odm)
#define phydm_BeamformingControl_V2(pDM_Odm, _Idx, _Mode, _BW, _Period)	FALSE
#define Beamforming_Watchdog(pDM_Odm)
#define phydm_Beamforming_Watchdog(pDM_Odm)


#endif
#endif
