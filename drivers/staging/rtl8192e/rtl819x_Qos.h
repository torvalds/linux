/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#ifndef __INC_QOS_TYPE_H
#define __INC_QOS_TYPE_H

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

struct octet_string {
	u8 *Octet;
	u16 Length;
};

#define AC0_BE	0
#define AC1_BK	1
#define AC2_VI	2
#define AC3_VO	3
#define AC_MAX	4

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
		u8		SrcAddr[ETH_ALEN];
		u8		DstAddr[ETH_ALEN];
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

#endif
