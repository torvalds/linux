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

typedef union _U_MIBS_IP_ADDRESS
{
	struct
	{
		//Source Ip Address Range
		ULONG ulIpv4Addr[MIBS_MAX_IP_RANGE_LENGTH];
		//Source Ip Mask Address Range
		ULONG ulIpv4Mask[MIBS_MAX_IP_RANGE_LENGTH];
	};
	struct
	{
		//Source Ip Address Range
		ULONG ulIpv6Addr[MIBS_MAX_IP_RANGE_LENGTH * 4];
		//Source Ip Mask Address Range
		ULONG ulIpv6Mask[MIBS_MAX_IP_RANGE_LENGTH * 4];
	};
	struct
	{
		UCHAR ucIpv4Address[MIBS_MAX_IP_RANGE_LENGTH * MIBS_IP_LENGTH_OF_ADDRESS];
		UCHAR ucIpv4Mask[MIBS_MAX_IP_RANGE_LENGTH * MIBS_IP_LENGTH_OF_ADDRESS];
	};
	struct
	{
		UCHAR ucIpv6Address[MIBS_MAX_IP_RANGE_LENGTH * MIBS_IPV6_ADDRESS_SIZEINBYTES];
		UCHAR ucIpv6Mask[MIBS_MAX_IP_RANGE_LENGTH * MIBS_IPV6_ADDRESS_SIZEINBYTES];
	};
} U_MIBS_IP_ADDRESS;

typedef struct _S_MIBS_HOST_INFO
{
	ULONG64	GoodTransmits;
	ULONG64	GoodReceives;
	// this to keep track of the Tx and Rx MailBox Registers.
	ULONG	NumDesUsed;
	ULONG	CurrNumFreeDesc;
	ULONG	PrevNumFreeDesc;
	// to keep track the no of byte received
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

typedef struct _S_MIBS_CLASSIFIER_RULE
{
	ULONG	ulSFID;
	UCHAR	ucReserved[2];
	B_UINT16 uiClassifierRuleIndex;
	BOOLEAN	bUsed;
	USHORT	usVCID_Value;
	// This field detemines the Classifier Priority
	B_UINT8	u8ClassifierRulePriority;
	U_MIBS_IP_ADDRESS stSrcIpAddress;
	/*IP Source Address Length*/
	UCHAR	ucIPSourceAddressLength;
	U_MIBS_IP_ADDRESS stDestIpAddress;
	/* IP Destination Address Length */
	UCHAR	ucIPDestinationAddressLength;
	UCHAR	ucIPTypeOfServiceLength;//Type of service Length
	UCHAR	ucTosLow;//Tos Low
	UCHAR	ucTosHigh;//Tos High
	UCHAR	ucTosMask;//Tos Mask
	UCHAR	ucProtocolLength;//protocol Length
	UCHAR	ucProtocol[MIBS_MAX_PROTOCOL_LENGTH];//protocol Length
	USHORT	usSrcPortRangeLo[MIBS_MAX_PORT_RANGE];
	USHORT	usSrcPortRangeHi[MIBS_MAX_PORT_RANGE];
	UCHAR	ucSrcPortRangeLength;
	USHORT	usDestPortRangeLo[MIBS_MAX_PORT_RANGE];
	USHORT	usDestPortRangeHi[MIBS_MAX_PORT_RANGE];
	UCHAR	ucDestPortRangeLength;
	BOOLEAN	bProtocolValid;
	BOOLEAN	bTOSValid;
	BOOLEAN	bDestIpValid;
	BOOLEAN	bSrcIpValid;
	UCHAR	ucDirection;
	BOOLEAN	bIpv6Protocol;
	UINT32	u32PHSRuleID;
} S_MIBS_CLASSIFIER_RULE;

typedef struct _S_MIBS_PHS_RULE
{
	ULONG	ulSFID;
	/// brief 8bit PHSI Of The Service Flow
	B_UINT8	u8PHSI;
	/// brief PHSF Of The Service Flow
	B_UINT8	u8PHSFLength;
	B_UINT8	u8PHSF[MIBS_MAX_PHS_LENGTHS];
	/// brief PHSM Of The Service Flow
	B_UINT8	u8PHSMLength;
	B_UINT8	u8PHSM[MIBS_MAX_PHS_LENGTHS];
	/// brief 8bit PHSS Of The Service Flow
	B_UINT8	u8PHSS;
	/// brief 8bit PHSV Of The Service Flow
	B_UINT8	u8PHSV;
	// Reserved bytes are 5, so that it is similar to S_PHS_RULE structure.
	B_UINT8	reserved[5];
	LONG	PHSModifiedBytes;
	ULONG	PHSModifiedNumPackets;
	ULONG	PHSErrorNumPackets;
} S_MIBS_PHS_RULE;

typedef struct _S_MIBS_EXTSERVICEFLOW_PARAMETERS
{
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

typedef struct _S_MIBS_SERVICEFLOW_TABLE
{
	//classification extension Rule
	ULONG	ulSFID;
	USHORT	usVCID_Value;
	UINT	uiThreshold;
	// This field determines the priority of the SF Queues
	B_UINT8	u8TrafficPriority;
	BOOLEAN	bValid;
	BOOLEAN	bActive;
	BOOLEAN	bActivateRequestSent;
	//BE or rtPS
	B_UINT8	u8QueueType;
	//maximum size of the bucket for the queue
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
	UCHAR	ucDirection;
	USHORT	usCID;
	S_MIBS_EXTSERVICEFLOW_PARAMETERS stMibsExtServiceFlowTable;
	UINT	uiCurrentRxRate;
	UINT	uiThisPeriodRxBytes;
	UINT	uiTotalRxBytes;
	UINT	uiTotalTxBytes;
} S_MIBS_SERVICEFLOW_TABLE;

typedef struct _S_MIBS_DROPPED_APP_CNTRL_MESSAGES
{
	ULONG cm_responses;
	ULONG cm_control_newdsx_multiclassifier_resp;
	ULONG link_control_resp;
	ULONG status_rsp;
	ULONG stats_pointer_resp;
	ULONG idle_mode_status;
	ULONG auth_ss_host_msg;
	ULONG low_priority_message;
} S_MIBS_DROPPED_APP_CNTRL_MESSAGES;

typedef struct _S_MIBS_HOST_STATS_MIBS
{
	S_MIBS_HOST_INFO	stHostInfo;
	S_MIBS_CLASSIFIER_RULE	astClassifierTable[MIBS_MAX_CLASSIFIERS];
	S_MIBS_SERVICEFLOW_TABLE astSFtable[MIBS_MAX_SERVICEFLOWS];
	S_MIBS_PHS_RULE		astPhsRulesTable[MIBS_MAX_PHSRULES];
	S_MIBS_DROPPED_APP_CNTRL_MESSAGES stDroppedAppCntrlMsgs;
} S_MIBS_HOST_STATS_MIBS;

#endif
