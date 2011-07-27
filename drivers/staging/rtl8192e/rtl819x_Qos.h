/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#ifndef __INC_QOS_TYPE_H
#define __INC_QOS_TYPE_H

#include "rtllib_endianfree.h"

#define BIT0		    0x00000001
#define BIT1		    0x00000002
#define BIT2		    0x00000004
#define BIT3		    0x00000008
#define BIT4		    0x00000010
#define BIT5		    0x00000020
#define BIT6		    0x00000040
#define BIT7		    0x00000080
#define BIT8		    0x00000100
#define BIT9		    0x00000200
#define BIT10		   0x00000400
#define BIT11		   0x00000800
#define BIT12		   0x00001000
#define BIT13		   0x00002000
#define BIT14		   0x00004000
#define BIT15		   0x00008000
#define BIT16		   0x00010000
#define BIT17		   0x00020000
#define BIT18		   0x00040000
#define BIT19		   0x00080000
#define BIT20		   0x00100000
#define BIT21		   0x00200000
#define BIT22		   0x00400000
#define BIT23		   0x00800000
#define BIT24		   0x01000000
#define BIT25		   0x02000000
#define BIT26		   0x04000000
#define BIT27		   0x08000000
#define BIT28		   0x10000000
#define BIT29		   0x20000000
#define BIT30		   0x40000000
#define BIT31		   0x80000000

union qos_tsinfo {
	u8		charData[3];
	struct {
		u8		ucTrafficType:1;
		u8		ucTSID:4;
		u8		ucDirection:2;
		u8		ucAccessPolicy:2;
		u8		ucAggregation:1;
		u8		ucPSB:1;
		u8		ucUP:3;
		u8		ucTSInfoAckPolicy:2;
		u8		ucSchedule:1;
		u8		ucReserved:7;
	} field;
};

union tspec_body {
	u8		charData[55];

	struct {
		union qos_tsinfo TSInfo;
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
	} f;
};

struct wmm_tspec {
	u8		ID;
	u8		Length;
	u8		OUI[3];
	u8		OUI_Type;
	u8		OUI_SubType;
	u8		Version;
	union tspec_body Body;
};

struct octet_string {
	u8 *Octet;
	u16 Length;
};

#define	MAX_WMMELE_LENGTH	64

#define QOS_MODE u32

#define QOS_DISABLE		0
#define QOS_WMM		1
#define QOS_WMMSA		2
#define QOS_EDCA		4
#define QOS_HCCA		8
#define QOS_WMM_UAPSD		16

#define WMM_PARAM_ELE_BODY_LEN	18

#define MAX_STA_TS_COUNT			16
#define MAX_AP_TS_COUNT			32
#define QOS_TSTREAM_KEY_SIZE		13

#define WMM_ACTION_CATEGORY_CODE	17
#define WMM_PARAM_ELE_BODY_LEN	18

#define MAX_TSPEC_TSID				15
#define SESSION_REJECT_TSID			0xfe
#define DEFAULT_TSID					0xff

#define ADDTS_TIME_SLOT				100

#define ACM_TIMEOUT				1000
#define SESSION_REJECT_TIMEOUT		60000

enum ack_policy {
	eAckPlc0_ACK		= 0x00,
	eAckPlc1_NoACK		= 0x01,
};


#define SET_WMM_QOS_INFO_FIELD(_pStart, _val)	\
	WriteEF1Byte(_pStart, _val)

#define GET_WMM_QOS_INFO_FIELD_PARAMETERSET_COUNT(_pStart) \
	LE_BITS_TO_1BYTE(_pStart, 0, 4)
#define SET_WMM_QOS_INFO_FIELD_PARAMETERSET_COUNT(_pStart, _val) \
	SET_BITS_TO_LE_1BYTE(_pStart, 0, 4, _val)

#define GET_WMM_QOS_INFO_FIELD_AP_UAPSD(_pStart) \
	LE_BITS_TO_1BYTE(_pStart, 7, 1)
#define SET_WMM_QOS_INFO_FIELD_AP_UAPSD(_pStart, _val) \
	SET_BITS_TO_LE_1BYTE(_pStart, 7, 1, _val)

#define GET_WMM_QOS_INFO_FIELD_STA_AC_VO_UAPSD(_pStart) \
	LE_BITS_TO_1BYTE(_pStart, 0, 1)
#define SET_WMM_QOS_INFO_FIELD_STA_AC_VO_UAPSD(_pStart, _val) \
	SET_BITS_TO_LE_1BYTE(_pStart, 0, 1, _val)

#define GET_WMM_QOS_INFO_FIELD_STA_AC_VI_UAPSD(_pStart) \
	LE_BITS_TO_1BYTE(_pStart, 1, 1)
#define SET_WMM_QOS_INFO_FIELD_STA_AC_VI_UAPSD(_pStart, _val) \
	SET_BITS_TO_LE_1BYTE(_pStart, 1, 1, _val)

#define GET_WMM_QOS_INFO_FIELD_STA_AC_BE_UAPSD(_pStart) \
	LE_BITS_TO_1BYTE(_pStart, 2, 1)
#define SET_WMM_QOS_INFO_FIELD_STA_AC_BE_UAPSD(_pStart, _val) \
	SET_BITS_TO_LE_1BYTE(_pStart, 2, 1, _val)

#define GET_WMM_QOS_INFO_FIELD_STA_AC_BK_UAPSD(_pStart) \
	LE_BITS_TO_1BYTE(_pStart, 3, 1)
#define SET_WMM_QOS_INFO_FIELD_STA_AC_BK_UAPSD(_pStart, _val) \
	SET_BITS_TO_LE_1BYTE(_pStart, 3, 1, _val)

#define GET_WMM_QOS_INFO_FIELD_STA_MAX_SP_LEN(_pStart) \
	LE_BITS_TO_1BYTE(_pStart, 5, 2)
#define SET_WMM_QOS_INFO_FIELD_STA_MAX_SP_LEN(_pStart, _val) \
	SET_BITS_TO_LE_1BYTE(_pStart, 5, 2, _val)

enum qos_ie_source {
	QOSIE_SRC_ADDTSREQ,
	QOSIE_SRC_ADDTSRSP,
	QOSIE_SRC_REASOCREQ,
	QOSIE_SRC_REASOCRSP,
	QOSIE_SRC_DELTS,
};


#define AC_CODING u32

#define AC0_BE	0
#define AC1_BK	1
#define AC2_VI	2
#define AC3_VO	3
#define AC_MAX	4


#define AC_PARAM_SIZE	4

#define GET_WMM_AC_PARAM_AIFSN(_pStart) \
	((u8)LE_BITS_TO_4BYTE(_pStart, 0, 4))
#define SET_WMM_AC_PARAM_AIFSN(_pStart, _val) \
	SET_BITS_TO_LE_4BYTE(_pStart, 0, 4, _val)

#define GET_WMM_AC_PARAM_ACM(_pStart) \
	((u8)LE_BITS_TO_4BYTE(_pStart, 4, 1))
#define SET_WMM_AC_PARAM_ACM(_pStart, _val) \
	SET_BITS_TO_LE_4BYTE(_pStart, 4, 1, _val)

#define GET_WMM_AC_PARAM_ACI(_pStart) \
	((u8)LE_BITS_TO_4BYTE(_pStart, 5, 2))
#define SET_WMM_AC_PARAM_ACI(_pStart, _val) \
	SET_BITS_TO_LE_4BYTE(_pStart, 5, 2, _val)

#define GET_WMM_AC_PARAM_ACI_AIFSN(_pStart) \
	((u8)LE_BITS_TO_4BYTE(_pStart, 0, 8))
#define SET_WMM_AC_PARAM_ACI_AIFSN(_pStart, _val) \
	SET_BITS_TO_LE_4BYTE(_pStart, 0, 8, _val)

#define GET_WMM_AC_PARAM_ECWMIN(_pStart) \
	((u8)LE_BITS_TO_4BYTE(_pStart, 8, 4))
#define SET_WMM_AC_PARAM_ECWMIN(_pStart, _val) \
	SET_BITS_TO_LE_4BYTE(_pStart, 8, 4, _val)

#define GET_WMM_AC_PARAM_ECWMAX(_pStart) \
	((u8)LE_BITS_TO_4BYTE(_pStart, 12, 4))
#define SET_WMM_AC_PARAM_ECWMAX(_pStart, _val) \
	SET_BITS_TO_LE_4BYTE(_pStart, 12, 4, _val)

#define GET_WMM_AC_PARAM_TXOP_LIMIT(_pStart) \
	((u8)LE_BITS_TO_4BYTE(_pStart, 16, 16))
#define SET_WMM_AC_PARAM_TXOP_LIMIT(_pStart, _val) \
	SET_BITS_TO_LE_4BYTE(_pStart, 16, 16, _val)



#define WMM_PARAM_ELEMENT_SIZE	(8+(4*AC_PARAM_SIZE))

enum qos_ele_subtype {
	QOSELE_TYPE_INFO		= 0x00,
	QOSELE_TYPE_PARAM	= 0x01,
};


enum direction_value {
	DIR_UP			= 0,
	DIR_DOWN		= 1,
	DIR_DIRECT		= 2,
	DIR_BI_DIR		= 3,
};

enum acm_method {
	eAcmWay0_SwAndHw		= 0,
	eAcmWay1_HW			= 1,
	eAcmWay2_SW			= 2,
};


struct acm {
	u64		UsedTime;
	u64		MediumTime;
	u8		HwAcmCtl;
};



#define AC_UAPSD	u8

#define	GET_VO_UAPSD(_apsd) ((_apsd) & BIT0)
#define	SET_VO_UAPSD(_apsd) ((_apsd) |= BIT0)

#define	GET_VI_UAPSD(_apsd) ((_apsd) & BIT1)
#define	SET_VI_UAPSD(_apsd) ((_apsd) |= BIT1)

#define	GET_BK_UAPSD(_apsd) ((_apsd) & BIT2)
#define	SET_BK_UAPSD(_apsd) ((_apsd) |= BIT2)

#define	GET_BE_UAPSD(_apsd) ((_apsd) & BIT3)
#define	SET_BE_UAPSD(_apsd) ((_apsd) |= BIT3)

union qos_tclas {

	struct _TYPE_GENERAL {
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
	} TYPE_GENERAL;

	struct _TYPE0_ETH {
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u8		SrcAddr[6];
		u8		DstAddr[6];
		u16		Type;
	} TYPE0_ETH;

	struct _TYPE1_IPV4 {
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

	struct _TYPE1_IPV6 {
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

	struct _TYPE2_8021Q {
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u16		TagType;
	} TYPE2_8021Q;
};

struct qos_tstream {

	bool			bUsed;
	u16			MsduLifetime;
	bool			bEstablishing;
	u8			TimeSlotCount;
	u8			DialogToken;
	struct wmm_tspec TSpec;
	struct wmm_tspec OutStandingTSpec;
	u8			NominalPhyRate;
};

struct sta_qos {
	u8 WMMIEBuf[MAX_WMMELE_LENGTH];
	u8 *WMMIE;

	QOS_MODE QosCapability;
	QOS_MODE CurrentQosMode;

	AC_UAPSD b4ac_Uapsd;
	AC_UAPSD Curr4acUapsd;
	u8 bInServicePeriod;
	u8 MaxSPLength;
	int NumBcnBeforeTrigger;

	u8 *pWMMInfoEle;
	u8 WMMParamEle[WMM_PARAM_ELEMENT_SIZE];

	struct acm acm[4];
	enum acm_method AcmMethod;

	struct qos_tstream StaTsArray[MAX_STA_TS_COUNT];
	u8				DialogToken;
	struct wmm_tspec TSpec;

	u8				QBssWirelessMode;

	bool				bNoAck;

	bool				bEnableRxImmBA;

};

#define QBSS_LOAD_SIZE				5
#define GET_QBSS_LOAD_STA_COUNT(__pStart)	\
		ReadEF2Byte(__pStart)
#define SET_QBSS_LOAD_STA_COUNT(__pStart, __Value)	\
		WriteEF2Byte(__pStart, __Value)
#define GET_QBSS_LOAD_CHNL_UTILIZATION(__pStart)	\
		ReadEF1Byte((u8 *)(__pStart) + 2)
#define SET_QBSS_LOAD_CHNL_UTILIZATION(__pStart, __Value)	\
		WriteEF1Byte((u8 *)(__pStart) + 2, __Value)
#define GET_QBSS_LOAD_AVAILABLE_CAPACITY(__pStart)	\
		ReadEF2Byte((u8 *)(__pStart) + 3)
#define SET_QBSS_LOAD_AVAILABLE_CAPACITY(__pStart, __Value) \
		WriteEF2Byte((u8 *)(__pStart) + 3, __Value)

struct bss_qos {
	QOS_MODE bdQoSMode;
	u8 bdWMMIEBuf[MAX_WMMELE_LENGTH];
	struct octet_string bdWMMIE;

	enum qos_ele_subtype EleSubType;

	u8 *pWMMInfoEle;
	u8 *pWMMParamEle;

	u8 QBssLoad[QBSS_LOAD_SIZE];
	bool bQBssLoadValid;
};

#define sQoSCtlLng	2
#define QOS_CTRL_LEN(_QosMode)	((_QosMode > QOS_DISABLE) ? sQoSCtlLng : 0)


#define IsACValid(ac)		((ac >= 0 && ac <= 7) ? true : false)


union aci_aifsn {
	u8	charData;

	struct {
		u8	AIFSN:4;
		u8	acm:1;
		u8	ACI:2;
		u8	Reserved:1;
	} f;
};

union ecw {
	u8	charData;
	struct {
		u8	ECWmin:4;
		u8	ECWmax:4;
	} f;
};

union ac_param {
	u32	longData;
	u8	charData[4];

	struct {
		union aci_aifsn AciAifsn;
		union ecw Ecw;
		u16		TXOPLimit;
	} f;
};

#endif
