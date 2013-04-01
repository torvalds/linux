#ifndef __INC_QOS_TYPE_H
#define __INC_QOS_TYPE_H

#define BIT0                    0x00000001
#define BIT1                    0x00000002
#define BIT2                    0x00000004
#define BIT3                    0x00000008
#define BIT4                    0x00000010
#define BIT5                    0x00000020
#define BIT6                    0x00000040
#define BIT7                    0x00000080
#define BIT8                    0x00000100
#define BIT9                    0x00000200
#define BIT10                   0x00000400
#define BIT11                   0x00000800
#define BIT12                   0x00001000
#define BIT13                   0x00002000
#define BIT14                   0x00004000
#define BIT15                   0x00008000
#define BIT16                   0x00010000
#define BIT17                   0x00020000
#define BIT18                   0x00040000
#define BIT19                   0x00080000
#define BIT20                   0x00100000
#define BIT21                   0x00200000
#define BIT22                   0x00400000
#define BIT23                   0x00800000
#define BIT24                   0x01000000
#define BIT25                   0x02000000
#define BIT26                   0x04000000
#define BIT27                   0x08000000
#define BIT28                   0x10000000
#define BIT29                   0x20000000
#define BIT30                   0x40000000
#define BIT31                   0x80000000

#define	MAX_WMMELE_LENGTH	64

//
// QoS mode.
// enum 0, 1, 2, 4: since we can use the OR(|) operation.
//
// QOS_MODE is redefined for enum can't be ++, | under C++ compiler, 2006.05.17, by rcnjko.
//typedef	enum _QOS_MODE{
//	QOS_DISABLE		= 0,
//	QOS_WMM			= 1,
//	QOS_EDCA			= 2,
//	QOS_HCCA			= 4,
//}QOS_MODE,*PQOS_MODE;
//
typedef u32 QOS_MODE, *PQOS_MODE;
#define QOS_DISABLE		0
#define QOS_WMM			1
#define QOS_WMMSA		2
#define QOS_EDCA		4
#define QOS_HCCA		8
#define QOS_WMM_UAPSD		16   //WMM Power Save, 2006-06-14 Isaiah

#define AC_PARAM_SIZE	4
#define WMM_PARAM_ELE_BODY_LEN	18

//
// QoS ACK Policy Field Values
// Ref: WMM spec 2.1.6: QoS Control Field, p.10.
//
typedef	enum _ACK_POLICY{
	eAckPlc0_ACK		= 0x00,
	eAckPlc1_NoACK		= 0x01,
}ACK_POLICY,*PACK_POLICY;

#define WMM_PARAM_ELEMENT_SIZE	(8+(4*AC_PARAM_SIZE))

//
// QoS Control Field
// Ref:
//	1. WMM spec 2.1.6: QoS Control Field, p.9.
//	2. 802.11e/D13.0 7.1.3.5, p.26.
//
typedef	union _QOS_CTRL_FIELD{
	u8	charData[2];
	u16	shortData;

	// WMM spec
	struct {
		u8		UP:3;
		u8		usRsvd1:1;
		u8		EOSP:1;
		u8		AckPolicy:2;
		u8		usRsvd2:1;
		u8		ucRsvdByte;
	}WMM;

	// 802.11e: QoS data type frame sent by non-AP QSTAs.
	struct {
		u8		TID:4;
		u8		bIsQsize:1;// 0: BIT[8:15] is TXOP Duration Requested, 1: BIT[8:15] is Queue Size.
		u8		AckPolicy:2;
		u8		usRsvd:1;
		u8		TxopOrQsize;	// (BIT4=0)TXOP Duration Requested or (BIT4=1)Queue Size.
	}BySta;

	// 802.11e: QoS data, QoS Null, and QoS Data+CF-Ack frames sent by HC.
	struct {
		u8		TID:4;
		u8		EOSP:1;
		u8		AckPolicy:2;
		u8		usRsvd:1;
		u8		PSBufState;		// QAP PS Buffer State.
	}ByHc_Data;

	// 802.11e: QoS (+) CF-Poll frames sent by HC.
	struct {
		u8		TID:4;
		u8		EOSP:1;
		u8		AckPolicy:2;
		u8		usRsvd:1;
		u8		TxopLimit;		// TXOP Limit.
	}ByHc_CFP;

}QOS_CTRL_FIELD, *PQOS_CTRL_FIELD;


//
// QoS Info Field
// Ref:
//	1. WMM spec 2.2.1: WME Information Element, p.11.
//	2. 8185 QoS code: QOS_INFO [def. in QoS_mp.h]
//
typedef	union _QOS_INFO_FIELD{
	u8	charData;

	struct {
		u8		ucParameterSetCount:4;
		u8		ucReserved:4;
	}WMM;

	struct {
		//Ref WMM_Specification_1-1.pdf, 2006-06-13 Isaiah
		u8		ucAC_VO_UAPSD:1;
		u8		ucAC_VI_UAPSD:1;
		u8		ucAC_BE_UAPSD:1;
		u8		ucAC_BK_UAPSD:1;
		u8		ucReserved1:1;
		u8		ucMaxSPLen:2;
		u8		ucReserved2:1;

	}ByWmmPsSta;

	struct {
		//Ref WMM_Specification_1-1.pdf, 2006-06-13 Isaiah
		u8		ucParameterSetCount:4;
		u8		ucReserved:3;
		u8		ucApUapsd:1;
	}ByWmmPsAp;

	struct {
		u8		ucAC3_UAPSD:1;
		u8		ucAC2_UAPSD:1;
		u8		ucAC1_UAPSD:1;
		u8		ucAC0_UAPSD:1;
		u8		ucQAck:1;
		u8		ucMaxSPLen:2;
		u8		ucMoreDataAck:1;
	} By11eSta;

	struct {
		u8		ucParameterSetCount:4;
		u8		ucQAck:1;
		u8		ucQueueReq:1;
		u8		ucTXOPReq:1;
		u8		ucReserved:1;
	} By11eAp;

	struct {
		u8		ucReserved1:4;
		u8		ucQAck:1;
		u8		ucReserved2:2;
		u8		ucMoreDataAck:1;
	} ByWmmsaSta;

	struct {
		u8		ucReserved1:4;
		u8		ucQAck:1;
		u8		ucQueueReq:1;
		u8		ucTXOPReq:1;
		u8		ucReserved2:1;
	} ByWmmsaAp;

	struct {
		u8		ucAC3_UAPSD:1;
		u8		ucAC2_UAPSD:1;
		u8		ucAC1_UAPSD:1;
		u8		ucAC0_UAPSD:1;
		u8		ucQAck:1;
		u8		ucMaxSPLen:2;
		u8		ucMoreDataAck:1;
	} ByAllSta;

	struct {
		u8		ucParameterSetCount:4;
		u8		ucQAck:1;
		u8		ucQueueReq:1;
		u8		ucTXOPReq:1;
		u8		ucApUapsd:1;
	} ByAllAp;

}QOS_INFO_FIELD, *PQOS_INFO_FIELD;

//
// ACI to AC coding.
// Ref: WMM spec 2.2.2: WME Parameter Element, p.13.
//
// AC_CODING is redefined for enum can't be ++, | under C++ compiler, 2006.05.17, by rcnjko.
//typedef	enum _AC_CODING{
//	AC0_BE	= 0,		// ACI: 0x00	// Best Effort
//	AC1_BK	= 1,		// ACI: 0x01	// Background
//	AC2_VI	= 2,		// ACI: 0x10	// Video
//	AC3_VO	= 3,		// ACI: 0x11	// Voice
//	AC_MAX = 4,		// Max: define total number; Should not to be used as a real enum.
//}AC_CODING,*PAC_CODING;
//
typedef u32 AC_CODING;
#define AC0_BE	0		// ACI: 0x00	// Best Effort
#define AC1_BK	1		// ACI: 0x01	// Background
#define AC2_VI	2		// ACI: 0x10	// Video
#define AC3_VO	3		// ACI: 0x11	// Voice
#define AC_MAX	4		// Max: define total number; Should not to be used as a real enum.

//
// ACI/AIFSN Field.
// Ref: WMM spec 2.2.2: WME Parameter Element, p.12.
//
typedef	union _ACI_AIFSN{
	u8	charData;

	struct {
		u8	AIFSN:4;
		u8	ACM:1;
		u8	ACI:2;
		u8	Reserved:1;
	}f;	// Field
}ACI_AIFSN, *PACI_AIFSN;

//
// ECWmin/ECWmax field.
// Ref: WMM spec 2.2.2: WME Parameter Element, p.13.
//
typedef	union _ECW{
	u8	charData;
	struct {
		u8	ECWmin:4;
		u8	ECWmax:4;
	}f;	// Field
}ECW, *PECW;

//
// AC Parameters Record Format.
// Ref: WMM spec 2.2.2: WME Parameter Element, p.12.
//
typedef	union _AC_PARAM{
	u32	longData;
	u8	charData[4];

	struct {
		ACI_AIFSN	AciAifsn;
		ECW		Ecw;
		u16		TXOPLimit;
	}f;	// Field
}AC_PARAM, *PAC_PARAM;



//
// QoS element subtype
//
typedef	enum _QOS_ELE_SUBTYPE{
	QOSELE_TYPE_INFO	= 0x00,		// 0x00: Information element
	QOSELE_TYPE_PARAM	= 0x01,		// 0x01: parameter element
}QOS_ELE_SUBTYPE,*PQOS_ELE_SUBTYPE;


//
// Direction Field Values.
// Ref: WMM spec 2.2.11: WME TSPEC Element, p.18.
//
typedef	enum _DIRECTION_VALUE{
	DIR_UP			= 0,		// 0x00	// UpLink
	DIR_DOWN		= 1,		// 0x01	// DownLink
	DIR_DIRECT		= 2,		// 0x10	// DirectLink
	DIR_BI_DIR		= 3,		// 0x11	// Bi-Direction
}DIRECTION_VALUE,*PDIRECTION_VALUE;


//
// TS Info field in WMM TSPEC Element.
// Ref:
//	1. WMM spec 2.2.11: WME TSPEC Element, p.18.
//	2. 8185 QoS code: QOS_TSINFO [def. in QoS_mp.h]
//
typedef union _QOS_TSINFO{
	u8		charData[3];
	struct {
		u8		ucTrafficType:1;			//WMM is reserved
		u8		ucTSID:4;
		u8		ucDirection:2;
		u8		ucAccessPolicy:2;	//WMM: bit8=0, bit7=1
		u8		ucAggregation:1;		//WMM is reserved
		u8		ucPSB:1;				//WMMSA is APSD
		u8		ucUP:3;
		u8		ucTSInfoAckPolicy:2;		//WMM is reserved
		u8		ucSchedule:1;			//WMM is reserved
		u8		ucReserved:7;
	}field;
}QOS_TSINFO, *PQOS_TSINFO;

//
// WMM TSPEC Body.
// Ref: WMM spec 2.2.11: WME TSPEC Element, p.16.
//
typedef union _TSPEC_BODY{
	u8		charData[55];

	struct {
		QOS_TSINFO	TSInfo;	//u8	TSInfo[3];
		u16	NominalMSDUsize;
		u16	MaxMSDUsize;
		u32	MinServiceItv;
		u32	MaxServiceItv;
		u32	InactivityItv;
		u32	SuspenItv;
		u32	ServiceStartTime;
		u32	MinDataRate;
		u32	MeanDataRate;
		u32	PeakDataRate;
		u32	MaxBurstSize;
		u32	DelayBound;
		u32	MinPhyRate;
		u16	SurplusBandwidthAllowance;
		u16	MediumTime;
	} f;	// Field
}TSPEC_BODY, *PTSPEC_BODY;


//
// WMM TSPEC Element.
// Ref: WMM spec 2.2.11: WME TSPEC Element, p.16.
//
typedef struct _WMM_TSPEC{
	u8		ID;
	u8		Length;
	u8		OUI[3];
	u8		OUI_Type;
	u8		OUI_SubType;
	u8		Version;
	TSPEC_BODY	Body;
} WMM_TSPEC, *PWMM_TSPEC;

//
// ACM implementation method.
// Annie, 2005-12-13.
//
typedef	enum _ACM_METHOD{
	eAcmWay0_SwAndHw		= 0,		// By SW and HW.
	eAcmWay1_HW			= 1,		// By HW.
	eAcmWay2_SW			= 2,		// By SW.
}ACM_METHOD,*PACM_METHOD;


typedef struct _ACM{
//	u8		RegEnableACM;
	u64		UsedTime;
	u64		MediumTime;
	u8		HwAcmCtl;	// TRUE: UsedTime exceed => Do NOT USE this AC. It wll be written to ACM_CONTROL(0xBF BIT 0/1/2 in 8185B).
}ACM, *PACM;

typedef	u8		AC_UAPSD, *PAC_UAPSD;

#define	GET_VO_UAPSD(_apsd) ((_apsd) & BIT0)
#define	SET_VO_UAPSD(_apsd) ((_apsd) |= BIT0)

#define	GET_VI_UAPSD(_apsd) ((_apsd) & BIT1)
#define	SET_VI_UAPSD(_apsd) ((_apsd) |= BIT1)

#define	GET_BK_UAPSD(_apsd) ((_apsd) & BIT2)
#define	SET_BK_UAPSD(_apsd) ((_apsd) |= BIT2)

#define	GET_BE_UAPSD(_apsd) ((_apsd) & BIT3)
#define	SET_BE_UAPSD(_apsd) ((_apsd) |= BIT3)


//typedef struct _TCLASS{
// TODO
//} TCLASS, *PTCLASS;
typedef union _QOS_TCLAS{

	struct _TYPE_GENERAL{
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
	} TYPE_GENERAL;

	struct _TYPE0_ETH{
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u8		SrcAddr[6];
		u8		DstAddr[6];
		u16		Type;
	} TYPE0_ETH;

	struct _TYPE1_IPV4{
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u8		Version;
		u8		SrcIP[4];
		u8		DstIP[4];
		u16		SrcPort;
		u16		DstPort;
		u8		DSCP;
		u8		Protocol;
		u8		Reserved;
	} TYPE1_IPV4;

	struct _TYPE1_IPV6{
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u8		Version;
		u8		SrcIP[16];
		u8		DstIP[16];
		u16		SrcPort;
		u16		DstPort;
		u8		FlowLabel[3];
	} TYPE1_IPV6;

	struct _TYPE2_8021Q{
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u16		TagType;
	} TYPE2_8021Q;
} QOS_TCLAS, *PQOS_TCLAS;

//typedef struct _WMM_TSTREAM{
//
//- TSPEC
//- AC (which to mapping)
//} WMM_TSTREAM, *PWMM_TSTREAM;
typedef struct _QOS_TSTREAM{
	u8			AC;
	WMM_TSPEC		TSpec;
	QOS_TCLAS		TClass;
} QOS_TSTREAM, *PQOS_TSTREAM;

//typedef struct _U_APSD{
//- TriggerEnable [4]
//- MaxSPLength
//- HighestAcBuffered
//} U_APSD, *PU_APSD;

//joseph TODO:
//	UAPSD function should be implemented by 2 data structure
//	"Qos control field" and "Qos info field"
//typedef struct _QOS_UAPSD{
//	u8			bTriggerEnable[4];
//	u8			MaxSPLength;
//	u8			HighestBufAC;
//} QOS_UAPSD, *PQOS_APSD;

//----------------------------------------------------------------------------
//      802.11 Management frame Status Code field
//----------------------------------------------------------------------------
typedef struct _OCTET_STRING{
	u8		*Octet;
	u16             Length;
}OCTET_STRING, *POCTET_STRING;

//
// STA QoS data.
// Ref: DOT11_QOS in 8185 code. [def. in QoS_mp.h]
//
typedef struct _STA_QOS{
	//DECLARE_RT_OBJECT(STA_QOS);
	u8				WMMIEBuf[MAX_WMMELE_LENGTH];
	u8*				WMMIE;

	// Part 1. Self QoS Mode.
	QOS_MODE			QosCapability; //QoS Capability, 2006-06-14 Isaiah
	QOS_MODE			CurrentQosMode;

	// For WMM Power Save Mode :
	// ACs are trigger/delivery enabled or legacy power save enabled. 2006-06-13 Isaiah
	AC_UAPSD			b4ac_Uapsd;  //VoUapsd(bit0), ViUapsd(bit1),  BkUapsd(bit2), BeUapsd(bit3),
	AC_UAPSD			Curr4acUapsd;
	u8				bInServicePeriod;
	u8				MaxSPLength;
	int				NumBcnBeforeTrigger;

	// Part 2. EDCA Parameter (perAC)
	u8 *				pWMMInfoEle;
	u8				WMMParamEle[WMM_PARAM_ELEMENT_SIZE];
	u8				WMMPELength;

	// <Bruce_Note>
	//2 ToDo: remove the Qos Info Field and replace it by the above WMM Info element.
	// By Bruce, 2008-01-30.
	// Part 2. EDCA Parameter (perAC)
	QOS_INFO_FIELD			QosInfoField_STA;	// Maintained by STA
	QOS_INFO_FIELD			QosInfoField_AP;	// Retrieved from AP

	AC_PARAM			CurAcParameters[4];

	// Part 3. ACM
	ACM				acm[4];
	ACM_METHOD			AcmMethod;

	// Part 4. Per TID (Part 5: TCLASS will be described by TStream)
	QOS_TSTREAM			TStream[16];
	WMM_TSPEC			TSpec;

	u32				QBssWirelessMode;

	// No Ack Setting
	u8				bNoAck;

	// Enable/Disable Rx immediate BA capability.
	u8				bEnableRxImmBA;

}STA_QOS, *PSTA_QOS;

//
// BSS QOS data.
// Ref: BssDscr in 8185 code. [def. in BssDscr.h]
//
typedef struct _BSS_QOS{
	QOS_MODE		bdQoSMode;

	u8			bdWMMIEBuf[MAX_WMMELE_LENGTH];
	u8*		bdWMMIE;

	QOS_ELE_SUBTYPE		EleSubType;

	u8 *			pWMMInfoEle;
	u8 *			pWMMParamEle;

	QOS_INFO_FIELD		QosInfoField;
	AC_PARAM		AcParameter[4];
}BSS_QOS, *PBSS_QOS;


//
// Ref: sQoSCtlLng and QoSCtl definition in 8185 QoS code.
//#define QoSCtl   ((	(Adapter->bRegQoS) && (Adapter->dot11QoS.QoSMode &(QOS_EDCA|QOS_HCCA))	  )  ?sQoSCtlLng:0)
//
#define sQoSCtlLng			2
#define	QOS_CTRL_LEN(_QosMode)		((_QosMode > QOS_DISABLE)? sQoSCtlLng : 0)


//Added by joseph
//UP Mapping to AC, using in MgntQuery_SequenceNumber() and maybe for DSCP
//#define UP2AC(up)			((up<3)?((up==0)?1:0):(up>>1))
#define IsACValid(ac)			((ac<=7 )?true:false )

#endif // #ifndef __INC_QOS_TYPE_H
