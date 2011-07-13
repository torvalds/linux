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
#ifndef _RTL819XU_HTTYPE_H_
#define _RTL819XU_HTTYPE_H_


#define HT_OPMODE_NO_PROTECT		0
#define HT_OPMODE_OPTIONAL		1
#define HT_OPMODE_40MHZ_PROTECT	2
#define HT_OPMODE_MIXED			3

#define MIMO_PS_STATIC				0
#define MIMO_PS_DYNAMIC			1
#define MIMO_PS_NOLIMIT			3



#define sHTCLng	4


#define HT_SUPPORTED_MCS_1SS_BITMAP					0x000000ff
#define HT_SUPPORTED_MCS_2SS_BITMAP					0x0000ff00
#define HT_SUPPORTED_MCS_1SS_2SS_BITMAP			HT_MCS_1SS_BITMAP|HT_MCS_1SS_2SS_BITMAP


typedef enum _HT_MCS_RATE{
	HT_MCS0   = 0x00000001,
	HT_MCS1   = 0x00000002,
	HT_MCS2   = 0x00000004,
	HT_MCS3   = 0x00000008,
	HT_MCS4   = 0x00000010,
	HT_MCS5   = 0x00000020,
	HT_MCS6   = 0x00000040,
	HT_MCS7   = 0x00000080,
	HT_MCS8   = 0x00000100,
	HT_MCS9   = 0x00000200,
	HT_MCS10 = 0x00000400,
	HT_MCS11 = 0x00000800,
	HT_MCS12 = 0x00001000,
	HT_MCS13 = 0x00002000,
	HT_MCS14 = 0x00004000,
	HT_MCS15 = 0x00008000,
}HT_MCS_RATE,*PHT_MCS_RATE;

typedef enum _HT_CHANNEL_WIDTH{
	HT_CHANNEL_WIDTH_20 = 0,
	HT_CHANNEL_WIDTH_20_40 = 1,
}HT_CHANNEL_WIDTH, *PHT_CHANNEL_WIDTH;

typedef enum _HT_EXTCHNL_OFFSET{
	HT_EXTCHNL_OFFSET_NO_EXT = 0,
	HT_EXTCHNL_OFFSET_UPPER = 1,
	HT_EXTCHNL_OFFSET_NO_DEF = 2,
	HT_EXTCHNL_OFFSET_LOWER = 3,
}HT_EXTCHNL_OFFSET, *PHT_EXTCHNL_OFFSET;

typedef enum _CHNLOP{
	CHNLOP_NONE = 0,
	CHNLOP_SCAN = 1,
	CHNLOP_SWBW = 2,
	CHNLOP_SWCHNL = 3,
} CHNLOP, *PCHNLOP;

#define CHHLOP_IN_PROGRESS(_pHTInfo)	\
		((_pHTInfo)->ChnlOp > CHNLOP_NONE) ? true : false

/*
typedef	union _HT_CAPABILITY{
	u16	ShortData;
	u8	CharData[2];
	struct
	{
		u16	AdvCoding:1;
		u16	ChlWidth:1;
		u16	MimoPwrSave:2;
		u16	GreenField:1;
		u16	ShortGI20Mhz:1;
		u16	ShortGI40Mhz:1;
		u16	STBC:1;
		u16	BeamForm:1;
		u16	DelayBA:1;
		u16	MaxAMSDUSize:1;
		u16	DssCCk:1;
		u16	PSMP:1;
		u16	Rsvd:3;
	}Field;
}HT_CAPABILITY, *PHT_CAPABILITY;

typedef	union _HT_CAPABILITY_MACPARA{
	u8	ShortData;
	u8	CharData[1];
	struct
	{
		u8	MaxRxAMPDU:2;
		u8	MPDUDensity:2;
		u8	Rsvd:4;
	}Field;
}HT_CAPABILITY_MACPARA, *PHT_CAPABILITY_MACPARA;
*/

typedef enum _HT_ACTION{
	ACT_RECOMMAND_WIDTH		= 0,
	ACT_MIMO_PWR_SAVE		= 1,
	ACT_PSMP					= 2,
	ACT_SET_PCO_PHASE		= 3,
	ACT_MIMO_CHL_MEASURE	= 4,
	ACT_RECIPROCITY_CORRECT	= 5,
	ACT_MIMO_CSI_MATRICS		= 6,
	ACT_MIMO_NOCOMPR_STEER	= 7,
	ACT_MIMO_COMPR_STEER		= 8,
	ACT_ANTENNA_SELECT		= 9,
} HT_ACTION, *PHT_ACTION;


typedef enum _HT_Bandwidth_40MHZ_Sub_Carrier{
	SC_MODE_DUPLICATE = 0,
	SC_MODE_LOWER = 1,
	SC_MODE_UPPER = 2,
	SC_MODE_FULL40MHZ = 3,
}HT_BW40_SC_E;

typedef	struct _HT_CAPABILITY_ELE{

	u8	AdvCoding:1;
	u8	ChlWidth:1;
	u8	MimoPwrSave:2;
	u8	GreenField:1;
	u8	ShortGI20Mhz:1;
	u8	ShortGI40Mhz:1;
	u8	TxSTBC:1;
	u8	RxSTBC:2;
	u8	DelayBA:1;
	u8	MaxAMSDUSize:1;
	u8	DssCCk:1;
	u8	PSMP:1;
	u8	Rsvd1:1;
	u8	LSigTxopProtect:1;

	u8	MaxRxAMPDUFactor:2;
	u8	MPDUDensity:3;
	u8	Rsvd2:3;

	u8	MCS[16];


	u16	ExtHTCapInfo;

	u8	TxBFCap[4];

	u8	ASCap;

} __attribute__ ((packed)) HT_CAPABILITY_ELE, *PHT_CAPABILITY_ELE;


typedef struct _HT_INFORMATION_ELE{
	u8	ControlChl;

	u8	ExtChlOffset:2;
	u8	RecommemdedTxWidth:1;
	u8	RIFS:1;
	u8	PSMPAccessOnly:1;
	u8	SrvIntGranularity:3;

	u8	OptMode:2;
	u8	NonGFDevPresent:1;
	u8	Revd1:5;
	u8	Revd2:8;

	u8	Rsvd3:6;
	u8	DualBeacon:1;
	u8	DualCTSProtect:1;

	u8	SecondaryBeacon:1;
	u8	LSigTxopProtectFull:1;
	u8	PcoActive:1;
	u8	PcoPhase:1;
	u8	Rsvd4:4;

	u8	BasicMSC[16];
} __attribute__ ((packed)) HT_INFORMATION_ELE, *PHT_INFORMATION_ELE;

typedef struct _MIMOPS_CTRL{
	u8	MimoPsEnable:1;
	u8	MimoPsMode:1;
	u8	Reserved:6;
} MIMOPS_CTRL, *PMIMOPS_CTRL;

typedef enum _HT_SPEC_VER{
	HT_SPEC_VER_IEEE = 0,
	HT_SPEC_VER_EWC = 1,
}HT_SPEC_VER, *PHT_SPEC_VER;

typedef enum _HT_AGGRE_MODE_E{
	HT_AGG_AUTO = 0,
	HT_AGG_FORCE_ENABLE = 1,
	HT_AGG_FORCE_DISABLE = 2,
}HT_AGGRE_MODE_E, *PHT_AGGRE_MODE_E;


typedef struct _RT_HIGH_THROUGHPUT{
	u8				bEnableHT;
	u8				bCurrentHTSupport;

	u8				bRegBW40MHz;
	u8				bCurBW40MHz;

	u8				bRegShortGI40MHz;
	u8				bCurShortGI40MHz;

	u8				bRegShortGI20MHz;
	u8				bCurShortGI20MHz;

	u8				bRegSuppCCK;
	u8				bCurSuppCCK;

	HT_SPEC_VER			ePeerHTSpecVer;


	HT_CAPABILITY_ELE	SelfHTCap;
	HT_INFORMATION_ELE	SelfHTInfo;

	u8				PeerHTCapBuf[32];
	u8				PeerHTInfoBuf[32];


	u8				bAMSDU_Support;
	u16				nAMSDU_MaxSize;
	u8				bCurrent_AMSDU_Support;
	u16				nCurrent_AMSDU_MaxSize;

	u8				bAMPDUEnable;
	u8				bCurrentAMPDUEnable;
	u8				AMPDU_Factor;
	u8				CurrentAMPDUFactor;
	u8				MPDU_Density;
	u8				CurrentMPDUDensity;

	HT_AGGRE_MODE_E	ForcedAMPDUMode;
	u8				ForcedAMPDUFactor;
	u8				ForcedMPDUDensity;

	HT_AGGRE_MODE_E	ForcedAMSDUMode;
	u16				ForcedAMSDUMaxSize;

	u8				bForcedShortGI;

	u8				CurrentOpMode;

	u8				SelfMimoPs;
	u8				PeerMimoPs;

	HT_EXTCHNL_OFFSET	CurSTAExtChnlOffset;
	u8				bCurTxBW40MHz;
	u8				PeerBandwidth;

	u8				bSwBwInProgress;
	CHNLOP				ChnlOp;
	u8				SwBwStep;

	u8				bRegRT2RTAggregation;
	u8				RT2RT_HT_Mode;
	u8				bCurrentRT2RTAggregation;
	u8				bCurrentRT2RTLongSlotTime;
	u8				szRT2RTAggBuffer[10];

	u8				bRegRxReorderEnable;
	u8				bCurRxReorderEnable;
	u8				RxReorderWinSize;
	u8				RxReorderPendingTime;
	u16				RxReorderDropCounter;

#ifdef USB_RX_AGGREGATION_SUPPORT
	u8				UsbRxFwAggrEn;
	u8				UsbRxFwAggrPageNum;
	u8				UsbRxFwAggrPacketNum;
	u8				UsbRxFwAggrTimeout;
	u8				UsbRxPageSize;
#endif

	u8				bIsPeerBcm;

	u8				IOTPeer;
	u32				IOTAction;
	u8				IOTRaFunc;

	u8	bWAIotBroadcom;
	u8	WAIotTH;

	u8				bAcceptAddbaReq;
} __attribute__ ((packed)) RT_HIGH_THROUGHPUT, *PRT_HIGH_THROUGHPUT;



typedef struct _RT_HTINFO_STA_ENTRY{
	u8			bEnableHT;

	u8			bSupportCck;

	u16			AMSDU_MaxSize;

	u8			AMPDU_Factor;
	u8			MPDU_Density;

	u8			HTHighestOperaRate;

	u8			bBw40MHz;

	u8			bCurTxBW40MHz;

	u8			bCurShortGI20MHz;

	u8			bCurShortGI40MHz;

	u8			MimoPs;

	u8			McsRateSet[16];

	u8                      bCurRxReorderEnable;

	u16                     nAMSDU_MaxSize;

}RT_HTINFO_STA_ENTRY, *PRT_HTINFO_STA_ENTRY;






typedef struct _BSS_HT{

	u8				bdSupportHT;

	u8					bdHTCapBuf[32];
	u16					bdHTCapLen;
	u8					bdHTInfoBuf[32];
	u16					bdHTInfoLen;

	HT_SPEC_VER				bdHTSpecVer;
	HT_CHANNEL_WIDTH			bdBandWidth;

	u8					bdRT2RTAggregation;
	u8					bdRT2RTLongSlotTime;
	u8					RT2RT_HT_Mode;
	u8					bdHT1R;
} __attribute__ ((packed)) BSS_HT, *PBSS_HT;

typedef struct _MIMO_RSSI{
	u32	EnableAntenna;
	u32	AntennaA;
	u32	AntennaB;
	u32	AntennaC;
	u32	AntennaD;
	u32	Average;
}MIMO_RSSI, *PMIMO_RSSI;

typedef struct _MIMO_EVM{
	u32	EVM1;
	u32    EVM2;
}MIMO_EVM, *PMIMO_EVM;

typedef struct _FALSE_ALARM_STATISTICS{
	u32	Cnt_Parity_Fail;
	u32	Cnt_Rate_Illegal;
	u32	Cnt_Crc8_fail;
	u32	Cnt_Mcs_fail;
	u32	Cnt_Ofdm_fail;
	u32	Cnt_Cck_fail;
	u32	Cnt_all;
}FALSE_ALARM_STATISTICS, *PFALSE_ALARM_STATISTICS;


extern u8 MCS_FILTER_ALL[16];
extern u8 MCS_FILTER_1SS[16];

#define PICK_RATE(_nLegacyRate, _nMcsRate)	\
		(_nMcsRate==0)?(_nLegacyRate&0x7f):(_nMcsRate)
#define	LEGACY_WIRELESS_MODE	IEEE_MODE_MASK

#define CURRENT_RATE(WirelessMode, LegacyRate, HTRate)	\
					((WirelessMode & (LEGACY_WIRELESS_MODE))!=0)?\
						(LegacyRate):\
						(PICK_RATE(LegacyRate, HTRate))



#define	RATE_ADPT_1SS_MASK		0xFF
#define	RATE_ADPT_2SS_MASK		0xF0
#define	RATE_ADPT_MCS32_MASK		0x01

#define		IS_11N_MCS_RATE(rate)		(rate&0x80)

typedef enum _HT_AGGRE_SIZE{
	HT_AGG_SIZE_8K = 0,
	HT_AGG_SIZE_16K = 1,
	HT_AGG_SIZE_32K = 2,
	HT_AGG_SIZE_64K = 3,
}HT_AGGRE_SIZE_E, *PHT_AGGRE_SIZE_E;

typedef enum _HT_IOT_PEER
{
	HT_IOT_PEER_UNKNOWN = 0,
	HT_IOT_PEER_REALTEK = 1,
	HT_IOT_PEER_REALTEK_92SE = 2,
	HT_IOT_PEER_BROADCOM = 3,
	HT_IOT_PEER_RALINK = 4,
	HT_IOT_PEER_ATHEROS = 5,
	HT_IOT_PEER_CISCO= 6,
	HT_IOT_PEER_MARVELL=7,
	HT_IOT_PEER_92U_SOFTAP = 8,
	HT_IOT_PEER_SELF_SOFTAP = 9,
	HT_IOT_PEER_AIRGO = 10,
	HT_IOT_PEER_MAX = 11,
}HT_IOT_PEER_E, *PHTIOT_PEER_E;

typedef enum _HT_IOT_PEER_SUBTYPE
{
	HT_IOT_PEER_ATHEROS_DIR635 = 0,
}HT_IOT_PEER_SUBTYPE_E, *PHTIOT_PEER_SUBTYPE_E;

typedef enum _HT_IOT_ACTION{
	HT_IOT_ACT_TX_USE_AMSDU_4K = 0x00000001,
	HT_IOT_ACT_TX_USE_AMSDU_8K = 0x00000002,
	HT_IOT_ACT_DISABLE_MCS14 = 0x00000004,
	HT_IOT_ACT_DISABLE_MCS15 = 0x00000008,
	HT_IOT_ACT_DISABLE_ALL_2SS = 0x00000010,
	HT_IOT_ACT_DISABLE_EDCA_TURBO = 0x00000020,
	HT_IOT_ACT_MGNT_USE_CCK_6M = 0x00000040,
	HT_IOT_ACT_CDD_FSYNC = 0x00000080,
	HT_IOT_ACT_PURE_N_MODE = 0x00000100,
	HT_IOT_ACT_FORCED_CTS2SELF = 0x00000200,
	HT_IOT_ACT_FORCED_RTS = 0x00000400,
	HT_IOT_ACT_AMSDU_ENABLE = 0x00000800,
	HT_IOT_ACT_REJECT_ADDBA_REQ = 0x00001000,
	HT_IOT_ACT_ALLOW_PEER_AGG_ONE_PKT = 0x00002000,
	HT_IOT_ACT_EDCA_BIAS_ON_RX = 0x00004000,

	HT_IOT_ACT_HYBRID_AGGREGATION = 0x00010000,
	HT_IOT_ACT_DISABLE_SHORT_GI = 0x00020000,
	HT_IOT_ACT_DISABLE_HIGH_POWER = 0x00040000,
	HT_IOT_ACT_DISABLE_TX_40_MHZ = 0x00080000,
	HT_IOT_ACT_TX_NO_AGGREGATION = 0x00100000,
	HT_IOT_ACT_DISABLE_TX_2SS = 0x00200000,

        HT_IOT_ACT_MID_HIGHPOWER = 0x00400000,
        HT_IOT_ACT_NULL_DATA_POWER_SAVING = 0x00800000,

        HT_IOT_ACT_DISABLE_CCK_RATE = 0x01000000,
	HT_IOT_ACT_FORCED_ENABLE_BE_TXOP = 0x02000000,
	HT_IOT_ACT_WA_IOT_Broadcom = 0x04000000,

	HT_IOT_ACT_DISABLE_RX_40MHZ_SHORT_GI = 0x08000000,

}HT_IOT_ACTION_E, *PHT_IOT_ACTION_E;

typedef enum _HT_IOT_RAFUNC{
	HT_IOT_RAFUNC_DISABLE_ALL = 0x00,
	HT_IOT_RAFUNC_PEER_1R = 0x01,
	HT_IOT_RAFUNC_TX_AMSDU = 0x02,
}HT_IOT_RAFUNC, *PHT_IOT_RAFUNC;

typedef enum _RT_HT_CAP{
	RT_HT_CAP_USE_TURBO_AGGR = 0x01,
	RT_HT_CAP_USE_LONG_PREAMBLE = 0x02,
	RT_HT_CAP_USE_AMPDU = 0x04,
	RT_HT_CAP_USE_WOW = 0x8,
	RT_HT_CAP_USE_SOFTAP = 0x10,
	RT_HT_CAP_USE_92SE = 0x20,
}RT_HT_CAPBILITY, *PRT_HT_CAPBILITY;

#endif
