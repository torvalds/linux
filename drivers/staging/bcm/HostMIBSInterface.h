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

union bcm_mibs_ip_addr {
	struct {
		/* Source Ip Address Range */
		unsigned long ulIpv4Addr[MIBS_MAX_IP_RANGE_LENGTH];
		/* Source Ip Mask Address Range */
		unsigned long ulIpv4Mask[MIBS_MAX_IP_RANGE_LENGTH];
	};
	struct {
		/* Source Ip Address Range */
		unsigned long ulIpv6Addr[MIBS_MAX_IP_RANGE_LENGTH * 4];
		/* Source Ip Mask Address Range */
		unsigned long ulIpv6Mask[MIBS_MAX_IP_RANGE_LENGTH * 4];
	};
	struct {
		unsigned char ucIpv4Address[MIBS_MAX_IP_RANGE_LENGTH * MIBS_IP_LENGTH_OF_ADDRESS];
		unsigned char ucIpv4Mask[MIBS_MAX_IP_RANGE_LENGTH * MIBS_IP_LENGTH_OF_ADDRESS];
	};
	struct {
		unsigned char ucIpv6Address[MIBS_MAX_IP_RANGE_LENGTH * MIBS_IPV6_ADDRESS_SIZEINBYTES];
		unsigned char ucIpv6Mask[MIBS_MAX_IP_RANGE_LENGTH * MIBS_IPV6_ADDRESS_SIZEINBYTES];
	};
};

struct bcm_mibs_host_info {
	u64	GoodTransmits;
	u64	GoodReceives;
	/* this to keep track of the Tx and Rx MailBox Registers. */
	unsigned long	NumDesUsed;
	unsigned long	CurrNumFreeDesc;
	unsigned long	PrevNumFreeDesc;
	/* to keep track the no of byte received */
	unsigned long	PrevNumRcevBytes;
	unsigned long	CurrNumRcevBytes;
	/* QOS Related */
	unsigned long	BEBucketSize;
	unsigned long	rtPSBucketSize;
	unsigned long	LastTxQueueIndex;
	bool	TxOutofDescriptors;
	bool	TimerActive;
	u32	u32TotalDSD;
	u32	aTxPktSizeHist[MIBS_MAX_HIST_ENTRIES];
	u32	aRxPktSizeHist[MIBS_MAX_HIST_ENTRIES];
};

struct bcm_mibs_classifier_rule {
	unsigned long	ulSFID;
	unsigned char	ucReserved[2];
	u16	uiClassifierRuleIndex;
	bool	bUsed;
	unsigned short	usVCID_Value;
	u8	u8ClassifierRulePriority;
	union bcm_mibs_ip_addr stSrcIpAddress;
	/* IP Source Address Length */
	unsigned char	ucIPSourceAddressLength;
	union bcm_mibs_ip_addr stDestIpAddress;
	/* IP Destination Address Length */
	unsigned char	ucIPDestinationAddressLength;
	unsigned char	ucIPTypeOfServiceLength;
	unsigned char	ucTosLow;
	unsigned char	ucTosHigh;
	unsigned char	ucTosMask;
	unsigned char	ucProtocolLength;
	unsigned char	ucProtocol[MIBS_MAX_PROTOCOL_LENGTH];
	unsigned short	usSrcPortRangeLo[MIBS_MAX_PORT_RANGE];
	unsigned short	usSrcPortRangeHi[MIBS_MAX_PORT_RANGE];
	unsigned char	ucSrcPortRangeLength;
	unsigned short	usDestPortRangeLo[MIBS_MAX_PORT_RANGE];
	unsigned short	usDestPortRangeHi[MIBS_MAX_PORT_RANGE];
	unsigned char	ucDestPortRangeLength;
	bool	bProtocolValid;
	bool	bTOSValid;
	bool	bDestIpValid;
	bool	bSrcIpValid;
	unsigned char	ucDirection;
	bool	bIpv6Protocol;
	u32	u32PHSRuleID;
};

struct bcm_mibs_phs_rule {
	unsigned long	ulSFID;
	u8	u8PHSI;
	u8	u8PHSFLength;
	u8	u8PHSF[MIBS_MAX_PHS_LENGTHS];
	u8	u8PHSMLength;
	u8	u8PHSM[MIBS_MAX_PHS_LENGTHS];
	u8	u8PHSS;
	u8	u8PHSV;
	u8	reserved[5];
	long	PHSModifiedBytes;
	unsigned long	PHSModifiedNumPackets;
	unsigned long	PHSErrorNumPackets;
};

struct bcm_mibs_parameters {
	u32 wmanIfSfid;
	u32 wmanIfCmnCpsSfState;
	u32 wmanIfCmnCpsMaxSustainedRate;
	u32 wmanIfCmnCpsMaxTrafficBurst;
	u32 wmanIfCmnCpsMinReservedRate;
	u32 wmanIfCmnCpsToleratedJitter;
	u32 wmanIfCmnCpsMaxLatency;
	u32 wmanIfCmnCpsFixedVsVariableSduInd;
	u32 wmanIfCmnCpsSduSize;
	u32 wmanIfCmnCpsSfSchedulingType;
	u32 wmanIfCmnCpsArqEnable;
	u32 wmanIfCmnCpsArqWindowSize;
	u32 wmanIfCmnCpsArqBlockLifetime;
	u32 wmanIfCmnCpsArqSyncLossTimeout;
	u32 wmanIfCmnCpsArqDeliverInOrder;
	u32 wmanIfCmnCpsArqRxPurgeTimeout;
	u32 wmanIfCmnCpsArqBlockSize;
	u32 wmanIfCmnCpsMinRsvdTolerableRate;
	u32 wmanIfCmnCpsReqTxPolicy;
	u32 wmanIfCmnSfCsSpecification;
	u32 wmanIfCmnCpsTargetSaid;
};

struct bcm_mibs_table {
	unsigned long	ulSFID;
	unsigned short	usVCID_Value;
	unsigned int	uiThreshold;
	u8	u8TrafficPriority;
	bool	bValid;
	bool	bActive;
	bool	bActivateRequestSent;
	u8	u8QueueType;
	unsigned int	uiMaxBucketSize;
	unsigned int	uiCurrentQueueDepthOnTarget;
	unsigned int	uiCurrentBytesOnHost;
	unsigned int	uiCurrentPacketsOnHost;
	unsigned int	uiDroppedCountBytes;
	unsigned int	uiDroppedCountPackets;
	unsigned int	uiSentBytes;
	unsigned int	uiSentPackets;
	unsigned int	uiCurrentDrainRate;
	unsigned int	uiThisPeriodSentBytes;
	u64	liDrainCalculated;
	unsigned int	uiCurrentTokenCount;
	u64	liLastUpdateTokenAt;
	unsigned int	uiMaxAllowedRate;
	unsigned int	NumOfPacketsSent;
	unsigned char ucDirection;
	unsigned short	usCID;
	struct bcm_mibs_parameters stMibsExtServiceFlowTable;
	unsigned int	uiCurrentRxRate;
	unsigned int	uiThisPeriodRxBytes;
	unsigned int	uiTotalRxBytes;
	unsigned int	uiTotalTxBytes;
};

struct bcm_mibs_dropped_cntrl_msg {
	unsigned long cm_responses;
	unsigned long cm_control_newdsx_multiclassifier_resp;
	unsigned long link_control_resp;
	unsigned long status_rsp;
	unsigned long stats_pointer_resp;
	unsigned long idle_mode_status;
	unsigned long auth_ss_host_msg;
	unsigned long low_priority_message;
};

struct bcm_host_stats_mibs {
	struct bcm_mibs_host_info stHostInfo;
	struct bcm_mibs_classifier_rule astClassifierTable[MIBS_MAX_CLASSIFIERS];
	struct bcm_mibs_table	astSFtable[MIBS_MAX_SERVICEFLOWS];
	struct bcm_mibs_phs_rule astPhsRulesTable[MIBS_MAX_PHSRULES];
	struct bcm_mibs_dropped_cntrl_msg stDroppedAppCntrlMsgs;
};

#endif
