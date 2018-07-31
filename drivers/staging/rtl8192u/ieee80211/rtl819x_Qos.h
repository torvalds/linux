/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __INC_QOS_TYPE_H
#define __INC_QOS_TYPE_H

/*
 * ACI/AIFSN Field.
 * Ref: WMM spec 2.2.2: WME Parameter Element, p.12.
 * Note: 1 Byte Length
 */
struct aci_aifsn {
	u8	aifsn:4;
	u8	acm:1;
	u8	aci:2;
	u8:1;
};

//
// Direction Field Values.
// Ref: WMM spec 2.2.11: WME TSPEC Element, p.18.
//
enum direction_value {
	DIR_UP			= 0,		// 0x00	// UpLink
	DIR_DOWN		= 1,		// 0x01	// DownLink
	DIR_DIRECT		= 2,		// 0x10	// DirectLink
	DIR_BI_DIR		= 3,		// 0x11	// Bi-Direction
};

/*
 * TS Info field in WMM TSPEC Element.
 * Ref:
 *	1. WMM spec 2.2.11: WME TSPEC Element, p.18.
 *	2. 8185 QoS code: QOS_TSINFO [def. in QoS_mp.h]
 * Note: sizeof 3 Bytes
 */
struct qos_tsinfo {
	u16		uc_traffic_type:1;	        //WMM is reserved
	u16		uc_tsid:4;
	u16		uc_direction:2;
	u16		uc_access_policy:2;	        //WMM: bit8=0, bit7=1
	u16		uc_aggregation:1;	        //WMM is reserved
	u16		uc_psb:1;		        //WMMSA is APSD
	u16		uc_up:3;
	u16		uc_ts_info_ack_policy:2;	//WMM is reserved
	u8		uc_schedule:1;		        //WMM is reserved
	u8:7;
};

/*
 * WMM TSPEC Body.
 * Ref: WMM spec 2.2.11: WME TSPEC Element, p.16.
 * Note: sizeof 55 bytes
 */
struct tspec_body {
	struct qos_tsinfo	ts_info;	//u8	TSInfo[3];
	u16	nominal_msd_usize;
	u16	max_msd_usize;
	u32	min_service_itv;
	u32	max_service_itv;
	u32	inactivity_itv;
	u32	suspen_itv;
	u32	service_start_time;
	u32	min_data_rate;
	u32	mean_data_rate;
	u32	peak_data_rate;
	u32	max_burst_size;
	u32	delay_bound;
	u32	min_phy_rate;
	u16	surplus_bandwidth_allowance;
	u16	medium_time;
};

typedef union _QOS_TCLAS {

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
} QOS_TCLAS, *PQOS_TCLAS;

//----------------------------------------------------------------------------
//      802.11 Management frame Status Code field
//----------------------------------------------------------------------------
typedef struct _OCTET_STRING {
	u8		*Octet;
	u16             Length;
} OCTET_STRING, *POCTET_STRING;

//Added by joseph
//UP Mapping to AC, using in MgntQuery_SequenceNumber() and maybe for DSCP
//#define UP2AC(up)			((up<3)?((up==0)?1:0):(up>>1))
#define IsACValid(ac)			((ac <= 7) ? true : false)

#endif // #ifndef __INC_QOS_TYPE_H
