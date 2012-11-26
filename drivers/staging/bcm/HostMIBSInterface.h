#ifndef _HOST_MIBSINTERFACE_H
#define _HOST_MIBSINTERFACE_H

/*
 * Copyright (c) 2007 Beceem Communications Pvt. Ltd
 * File Name: HostMIBSInterface.h
 * Abstract: This file contains DS used by the Host to update the Host
 * statistics used for the MIBS.
 */

#define MIBS_MAX_CLASSIFIERS		100
#define MIBS_MAX_PHSRULES		100
#define MIBS_MAX_SERVICEFLOWS		17
#define MIBS_MAX_IP_RANGE_LENGTH	4
#define MIBS_MAX_PORT_RANGE		4
#define MIBS_MAX_PROTOCOL_LENGTH	32
#define MIBS_MAX_PHS_LENGTHS		255
#define MIBS_IPV6_ADDRESS_SIZEINBYTES	0x10
#define MIBS_IP_LENGTH_OF_ADDRESS	4
#define MIBS_MAX_HIST_ENTRIES		12
#define MIBS_PKTSIZEHIST_RANGE		128

typedef union _U_MIBS_IP_ADDRESS {
	struct {
		/* Source Ip Address Range */
		ULONG ulIpv4Addr[MIBS_MAX_IP_RANGE_LENGTH];
		/* Source Ip Mask Address Range */
		ULONG ulIpv4Mask[MIBS_MAX_IP_RANGE_LENGTH];
	};
	struct {
		/* Source Ip Address Range */
		ULONG ulIpv6Addr[MIBS_MAX_IP_RANGE_LENGTH * 4];
		/* Source Ip Mask Address Range */
		ULONG ulIpv6Mask[MIBS_MAX_IP_RANGE_LENGTH * 4];
	};
	struct {
		unsigned char ucIpv4Address[MIBS_MAX_IP_RANGE_LENGTH * MIBS_IP_LENGTH_OF_ADDRESS];
		unsigned char ucIpv4Mask[MIBS_MAX_IP_RANGE_LENGTH * MIBS_IP_LENGTH_OF_ADDRESS];
	};
	struct {
		unsigned char ucIpv6Address[MIBS_MAX_IP_RANGE_LENGTH * MIBS_IPV6_ADDRESS_SIZEINBYTES];
		unsigned char ucIpv6Mask[MIBS_MAX_IP_RANGE_LENGTH * MIBS_IPV6_ADDRESS_SIZEINBYTES];
	};
} U_MIBS_IP_ADDRESS;

typedef struct _S_MIBS_HOST_INFO {
	u64	GoodTransmits;
	u64	GoodReceives;
	/* this to keep track of the Tx and Rx MailBox Registers. */
	ULONG	NumDesUsed;
	ULONG	CurrNumFreeDesc;
	ULONG	PrevNumFreeDesc;
	/* to keep track the no of byte received */
	ULONG	PrevNumRcevBytes;
	ULONG	CurrNumRcevBytes;
	/* QOS Related */
	ULONG	BEBucketSize;
	ULONG	rtPSBucketSize;
	ULONG	LastTxQueueIndex;
	BOOLEAN	TxOutofDescriptors;
	BOOLEAN	TimerActive;
	UINT32	u32TotalDSD;
	UINT32	aTxPktSizeHist[MIBS_MAX_HIST_ENTRIES];
	UINT32	aRxPktSizeHist[MIBS_MAX_HIST_ENTRIES];
} S_MIBS_HOST_INFO;

typedef struct _S_MIBS_CLASSIFIER_RULE {
	ULONG	ulSFID;
	unsigned char	ucReserved[2];
	B_UINT16 uiClassifierRuleIndex;
	BOOLEAN	bUsed;
	USHORT	usVCID_Value;
	B_UINT8	u8ClassifierRulePriority;
	U_MIBS_IP_ADDRESS stSrcIpAddress;
	/* IP Source Address Length */
	unsigned char	ucIPSourceAddressLength;
	U_MIBS_IP_ADDRESS stDestIpAddress;
	/* IP Destination Address Length */
	unsigned char	ucIPDestinationAddressLength;
	unsigned char	ucIPTypeOfServiceLength;
	unsigned char	ucTosLow;
	unsigned char	ucTosHigh;
	unsigned char	ucTosMask;
	unsigned char	ucProtocolLength;
	unsigned char	ucProtocol[MIBS_MAX_PROTOCOL_LENGTH];
	USHORT	usSrcPortRangeLo[MIBS_MAX_PORT_RANGE];
	USHORT	usSrcPortRangeHi[MIBS_MAX_PORT_RANGE];
	unsigned char	ucSrcPortRangeLength;
	USHORT	usDestPortRangeLo[MIBS_MAX_PORT_RANGE];
	USHORT	usDestPortRangeHi[MIBS_MAX_PORT_RANGE];
	unsigned char	ucDestPortRangeLength;
	BOOLEAN	bProtocolValid;
	BOOLEAN	bTOSValid;
	BOOLEAN	bDestIpValid;
	BOOLEAN	bSrcIpValid;
	unsigned char	ucDirection;
	BOOLEAN	bIpv6Protocol;
	UINT32	u32PHSRuleID;
} S_MIBS_CLASSIFIER_RULE;

typedef struct _S_MIBS_PHS_RULE {
	ULONG	ulSFID;
	B_UINT8	u8PHSI;
	B_UINT8	u8PHSFLength;
	B_UINT8	u8PHSF[MIBS_MAX_PHS_LENGTHS];
	B_UINT8	u8PHSMLength;
	B_UINT8	u8PHSM[MIBS_MAX_PHS_LENGTHS];
	B_UINT8	u8PHSS;
	B_UINT8	u8PHSV;
	B_UINT8	reserved[5];
	LONG	PHSModifiedBytes;
	ULONG	PHSModifiedNumPackets;
	ULONG	PHSErrorNumPackets;
} S_MIBS_PHS_RULE;

typedef struct _S_MIBS_EXTSERVICEFLOW_PARAMETERS {
	UINT32	wmanIfSfid;
	UINT32	wmanIfCmnCpsSfState;
	UINT32	wmanIfCmnCpsMaxSustainedRate;
	UINT32	wmanIfCmnCpsMaxTrafficBurst;
	UINT32	wmanIfCmnCpsMinReservedRate;
	UINT32	wmanIfCmnCpsToleratedJitter;
	UINT32	wmanIfCmnCpsMaxLatency;
	UINT32	wmanIfCmnCpsFixedVsVariableSduInd;
	UINT32	wmanIfCmnCpsSduSize;
	UINT32	wmanIfCmnCpsSfSchedulingType;
	UINT32	wmanIfCmnCpsArqEnable;
	UINT32	wmanIfCmnCpsArqWindowSize;
	UINT32	wmanIfCmnCpsArqBlockLifetime;
	UINT32	wmanIfCmnCpsArqSyncLossTimeout;
	UINT32	wmanIfCmnCpsArqDeliverInOrder;
	UINT32	wmanIfCmnCpsArqRxPurgeTimeout;
	UINT32	wmanIfCmnCpsArqBlockSize;
	UINT32	wmanIfCmnCpsMinRsvdTolerableRate;
	UINT32	wmanIfCmnCpsReqTxPolicy;
	UINT32	wmanIfCmnSfCsSpecification;
	UINT32	wmanIfCmnCpsTargetSaid;
} S_MIBS_EXTSERVICEFLOW_PARAMETERS;

typedef struct _S_MIBS_SERVICEFLOW_TABLE {
	ULONG	ulSFID;
	USHORT	usVCID_Value;
	UINT	uiThreshold;
	B_UINT8	u8TrafficPriority;
	BOOLEAN	bValid;
	BOOLEAN	bActive;
	BOOLEAN	bActivateRequestSent;
	B_UINT8	u8QueueType;
	UINT	uiMaxBucketSize;
	UINT	uiCurrentQueueDepthOnTarget;
	UINT	uiCurrentBytesOnHost;
	UINT	uiCurrentPacketsOnHost;
	UINT	uiDroppedCountBytes;
	UINT	uiDroppedCountPackets;
	UINT	uiSentBytes;
	UINT	uiSentPackets;
	UINT	uiCurrentDrainRate;
	UINT	uiThisPeriodSentBytes;
	LARGE_INTEGER	liDrainCalculated;
	UINT	uiCurrentTokenCount;
	LARGE_INTEGER	liLastUpdateTokenAt;
	UINT	uiMaxAllowedRate;
	UINT	NumOfPacketsSent;
	unsigned char ucDirection;
	USHORT	usCID;
	S_MIBS_EXTSERVICEFLOW_PARAMETERS stMibsExtServiceFlowTable;
	UINT	uiCurrentRxRate;
	UINT	uiThisPeriodRxBytes;
	UINT	uiTotalRxBytes;
	UINT	uiTotalTxBytes;
} S_MIBS_SERVICEFLOW_TABLE;

typedef struct _S_MIBS_DROPPED_APP_CNTRL_MESSAGES {
	ULONG cm_responses;
	ULONG cm_control_newdsx_multiclassifier_resp;
	ULONG link_control_resp;
	ULONG status_rsp;
	ULONG stats_pointer_resp;
	ULONG idle_mode_status;
	ULONG auth_ss_host_msg;
	ULONG low_priority_message;
} S_MIBS_DROPPED_APP_CNTRL_MESSAGES;

typedef struct _S_MIBS_HOST_STATS_MIBS {
	S_MIBS_HOST_INFO	stHostInfo;
	S_MIBS_CLASSIFIER_RULE	astClassifierTable[MIBS_MAX_CLASSIFIERS];
	S_MIBS_SERVICEFLOW_TABLE astSFtable[MIBS_MAX_SERVICEFLOWS];
	S_MIBS_PHS_RULE		astPhsRulesTable[MIBS_MAX_PHSRULES];
	S_MIBS_DROPPED_APP_CNTRL_MESSAGES stDroppedAppCntrlMsgs;
} S_MIBS_HOST_STATS_MIBS;

#endif
