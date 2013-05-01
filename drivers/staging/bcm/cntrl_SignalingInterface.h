#ifndef CNTRL_SIGNALING_INTERFACE_
#define CNTRL_SIGNALING_INTERFACE_

#define DSA_REQ			11
#define DSA_RSP			12
#define DSA_ACK			13
#define DSC_REQ			14
#define DSC_RSP			15
#define DSC_ACK			16
#define DSD_REQ			17
#define DSD_RSP			18
#define DSD_ACK			19
#define MAX_CLASSIFIERS_IN_SF	4

#define MAX_STRING_LEN			20
#define MAX_PHS_LENGTHS			255
#define VENDOR_PHS_PARAM_LENGTH		10
#define MAX_NUM_ACTIVE_BS		10
#define AUTH_TOKEN_LENGTH		10
#define NUM_HARQ_CHANNELS		16 /* Changed from 10 to 16 to accommodate all HARQ channels */
#define VENDOR_CLASSIFIER_PARAM_LENGTH	1  /* Changed the size to 1 byte since we dnt use it */
#define  VENDOR_SPECIF_QOS_PARAM	1
#define VENDOR_PHS_PARAM_LENGTH		10
#define MBS_CONTENTS_ID_LENGTH		10
#define GLOBAL_SF_CLASSNAME_LENGTH	6

#define TYPE_OF_SERVICE_LENGTH		3
#define IP_MASKED_SRC_ADDRESS_LENGTH	32
#define IP_MASKED_DEST_ADDRESS_LENGTH	32
#define PROTOCOL_SRC_PORT_RANGE_LENGTH	4
#define PROTOCOL_DEST_PORT_RANGE_LENGTH	4
#define ETHERNET_DEST_MAC_ADDR_LENGTH	12
#define ETHERNET_SRC_MAC_ADDR_LENGTH	12
#define NUM_ETHERTYPE_BYTES		3
#define NUM_IPV6_FLOWLABLE_BYTES	3

struct bcm_packet_class_rules {
	/* 16bit UserPriority Of The Service Flow */
	u16 u16UserPriority;
	/* 16bit VLANID Of The Service Flow */
	u16 u16VLANID;
	/* 16bit Packet Classification RuleIndex Of The Service Flow */
	u16 u16PacketClassificationRuleIndex;
	/* 8bit Classifier Rule Priority Of The Service Flow */
	u8 u8ClassifierRulePriority;
	/* Length of IP TypeOfService field */
	u8 u8IPTypeOfServiceLength;
	/* 3bytes IP TypeOfService */
	u8 u8IPTypeOfService[TYPE_OF_SERVICE_LENGTH];
	/* Protocol used in classification of Service Flow */
	u8 u8Protocol;
	/* Length of IP Masked Source Address */
	u8 u8IPMaskedSourceAddressLength;
	/* IP Masked Source Address used in classification for the Service Flow */
	u8 u8IPMaskedSourceAddress[IP_MASKED_SRC_ADDRESS_LENGTH];
	/* Length of IP Destination Address */
	u8 u8IPDestinationAddressLength;
	/* IP Destination Address used in classification for the Service Flow */
	u8 u8IPDestinationAddress[IP_MASKED_DEST_ADDRESS_LENGTH];
	/* Length of Protocol Source Port Range */
	u8 u8ProtocolSourcePortRangeLength;
	/* Protocol Source Port Range used in the Service Flow */
	u8 u8ProtocolSourcePortRange[PROTOCOL_SRC_PORT_RANGE_LENGTH];
	/* Length of Protocol Dest Port Range */
	u8 u8ProtocolDestPortRangeLength;
	/* Protocol Dest Port Range used in the Service Flow */
	u8 u8ProtocolDestPortRange[PROTOCOL_DEST_PORT_RANGE_LENGTH];
	/* Length of Ethernet Destination MAC Address */
	u8 u8EthernetDestMacAddressLength;
	/* Ethernet Destination MAC Address  used in classification of the Service Flow */
	u8 u8EthernetDestMacAddress[ETHERNET_DEST_MAC_ADDR_LENGTH];
	/* Length of Ethernet Source MAC Address */
	u8 u8EthernetSourceMACAddressLength;
	/* Ethernet Source MAC Address  used in classification of the Service Flow */
	u8 u8EthernetSourceMACAddress[ETHERNET_SRC_MAC_ADDR_LENGTH];
	/* Length of Ethertype */
	u8 u8EthertypeLength;
	/* 3bytes Ethertype Of The Service Flow */
	u8 u8Ethertype[NUM_ETHERTYPE_BYTES];
	/* 8bit Associated PHSI Of The Service Flow */
	u8 u8AssociatedPHSI;
	/* Length of Vendor Specific Classifier Param length Of The Service Flow */
	u8 u8VendorSpecificClassifierParamLength;
	/* Vendor Specific Classifier Param Of The Service Flow */
	u8 u8VendorSpecificClassifierParam[VENDOR_CLASSIFIER_PARAM_LENGTH];
	/* Length Of IPv6 Flow Lable of the Service Flow */
	u8 u8IPv6FlowLableLength;
	/* IPv6 Flow Lable Of The Service Flow */
	u8 u8IPv6FlowLable[NUM_IPV6_FLOWLABLE_BYTES];
	/* Action associated with the classifier rule */
	u8 u8ClassifierActionRule;
	u16 u16ValidityBitMap;
};

struct bcm_phs_rules {
	/* 8bit PHS Index Of The Service Flow */
	u8 u8PHSI;
	/* PHSF Length Of The Service Flow */
	u8 u8PHSFLength;
	/* String of bytes containing header information to be suppressed by the sending CS and reconstructed by the receiving CS */
	u8 u8PHSF[MAX_PHS_LENGTHS];
	/* PHSM Length Of The Service Flow */
	u8 u8PHSMLength;
	/* PHS Mask for the SF */
	u8 u8PHSM[MAX_PHS_LENGTHS];
	/* 8bit Total number of bytes to be suppressed for the Service Flow */
	u8 u8PHSS;
	/* 8bit Indicates whether or not Packet Header contents need to be verified prior to suppression */
	u8 u8PHSV;
	/* Vendor Specific PHS param Length Of The Service Flow */
	u8 u8VendorSpecificPHSParamsLength;
	/* Vendor Specific PHS param Of The Service Flow */
	u8 u8VendorSpecificPHSParams[VENDOR_PHS_PARAM_LENGTH];
	u8 u8Padding[2];
};

struct bcm_convergence_types {
	/* 8bit Phs Classfier Action Of The Service Flow */
	u8 u8ClassfierDSCAction;
	/* 8bit Phs DSC Action Of The Service Flow */
	u8 u8PhsDSCAction;
	/* 16bit Padding */
	u8 u8Padding[2];
	/* Packet classification rules structure */
	struct bcm_packet_class_rules cCPacketClassificationRule;
	/* Payload header suppression rules structure */
	struct bcm_phs_rules cPhsRule;
};

struct bcm_connect_mgr_params {
	/* 32bitSFID Of The Service Flow */
	u32 u32SFID;
	/* 32bit Maximum Sustained Traffic Rate of the Service Flow */
	u32 u32MaxSustainedTrafficRate;
	/* 32bit Maximum Traffic Burst allowed for the Service Flow */
	u32 u32MaxTrafficBurst;
	/* 32bit Minimum Reserved Traffic Rate of the Service Flow */
	u32 u32MinReservedTrafficRate;
	/* 32bit Tolerated Jitter of the Service Flow */
	u32 u32ToleratedJitter;
	/* 32bit Maximum Latency of the Service Flow */
	u32 u32MaximumLatency;
	/* 16bitCID Of The Service Flow */
	u16 u16CID;
	/* 16bit SAID on which the service flow being set up shall be mapped */
	u16 u16TargetSAID;
	/* 16bit  ARQ window size negotiated */
	u16 u16ARQWindowSize;
	/* 16bit Total Tx delay incl sending, receiving & processing delays */
	u16 u16ARQRetryTxTimeOut;
	/* 16bit Total Rx delay incl sending, receiving & processing delays */
	u16 u16ARQRetryRxTimeOut;
	/* 16bit ARQ block lifetime */
	u16 u16ARQBlockLifeTime;
	/* 16bit ARQ Sync loss timeout */
	u16 u16ARQSyncLossTimeOut;
	/* 16bit ARQ Purge timeout */
	u16 u16ARQRxPurgeTimeOut;
	/* TODO::Remove this once we move to a new CORR2 driver
	 * brief Size of an ARQ block
	 */
	u16 u16ARQBlockSize;
	/* #endif */
	/* 16bit Nominal interval b/w consecutive SDU arrivals at MAC SAP */
	u16 u16SDUInterArrivalTime;
	/* 16bit Specifies the time base for rate measurement */
	u16 u16TimeBase;
	/* 16bit Interval b/w Successive Grant oppurtunities */
	u16 u16UnsolicitedGrantInterval;
	/* 16bit Interval b/w Successive Polling grant oppurtunities */
	u16 u16UnsolicitedPollingInterval;
	/* internal var to get the overhead */
	u16 u16MacOverhead;
	/* MBS contents Identifier */
	u16 u16MBSContentsID[MBS_CONTENTS_ID_LENGTH];
	/* MBS contents Identifier length */
	u8 u8MBSContentsIDLength;
	/* ServiceClassName Length Of The Service Flow */
	u8 u8ServiceClassNameLength;
	/* 32bytes ServiceClassName Of The Service Flow */
	u8 u8ServiceClassName[32];
	/* 8bit Indicates whether or not MBS service is requested for this Serivce Flow */
	u8 u8MBSService;
	/* 8bit QOS Parameter Set specifies proper application of QoS parameters to Provisioned, Admitted and Active sets */
	u8 u8QosParamSet;
	/* 8bit Traffic Priority Of the Service Flow */
	u8 u8TrafficPriority;
	/* 8bit Uplink Grant Scheduling Type of The Service Flow */
	u8 u8ServiceFlowSchedulingType;
	/* 8bit Request transmission Policy of the Service Flow */
	u8 u8RequesttransmissionPolicy;
	/* 8bit Specifies whether SDUs for this Service flow are of FixedLength or Variable length */
	u8 u8FixedLengthVSVariableLengthSDUIndicator;
	/* 8bit Length of the SDU for a fixed length SDU service flow */
	u8 u8SDUSize;
	/* 8bit Indicates whether or not ARQ is requested for this connection */
	u8 u8ARQEnable;
	/* < 8bit Indicates whether or not data has tobe delivered in order to higher layer */
	u8 u8ARQDeliverInOrder;
	/* 8bit Receiver ARQ ACK processing time */
	u8 u8RxARQAckProcessingTime;
	/* 8bit Convergence Sublayer Specification Of The Service Flow */
	u8 u8CSSpecification;
	/* 8 bit Type of data delivery service */
	u8 u8TypeOfDataDeliveryService;
	/* 8bit Specifies whether a service flow may generate Paging */
	u8 u8PagingPreference;
	/* 8bit Indicates the MBS Zone through which the connection or virtual connection is valid */
	u8 u8MBSZoneIdentifierassignment;
	/* 8bit Specifies whether traffic on SF should generate MOB_TRF_IND to MS in sleep mode */
	u8 u8TrafficIndicationPreference;
	/* 8bit Speciifes the length of predefined Global QoS parameter set encoding for this SF */
	u8 u8GlobalServicesClassNameLength;
	/* 6 byte Speciifes the predefined Global QoS parameter set encoding for this SF */
	u8 u8GlobalServicesClassName[GLOBAL_SF_CLASSNAME_LENGTH];
	/* 8bit Indicates whether or not SN feedback is enabled for the conn */
	u8 u8SNFeedbackEnabled;
	/* Indicates the size of the Fragment Sequence Number for the connection */
	u8 u8FSNSize;
	/* 8bit Number of CIDs in active BS list */
	u8 u8CIDAllocation4activeBSsLength;
	/* CIDs of BS in the active list */
	u8 u8CIDAllocation4activeBSs[MAX_NUM_ACTIVE_BS];
	/* Specifies if PDU extended subheader should be applied on every PDU on this conn */
	u8 u8PDUSNExtendedSubheader4HarqReordering;
	/* 8bit Specifies whether the connection uses HARQ or not */
	u8 u8HARQServiceFlows;
	/* Specifies the length of Authorization token */
	u8 u8AuthTokenLength;
	/* Specifies the Authorization token */
	u8 u8AuthToken[AUTH_TOKEN_LENGTH];
	/* specifes Number of HARQ channels used to carry data length */
	u8 u8HarqChannelMappingLength;
	/* specifes HARQ channels used to carry data */
	u8 u8HARQChannelMapping[NUM_HARQ_CHANNELS];
	/* 8bit Length of Vendor Specific QoS Params */
	u8 u8VendorSpecificQoSParamLength;
	/* 1byte  Vendor Specific QoS Param Of The Service Flow */
	u8 u8VendorSpecificQoSParam[VENDOR_SPECIF_QOS_PARAM];
	/* indicates total classifiers in the SF */
	u8 u8TotalClassifiers;  /* < Total number of valid classifiers */
	u8 bValid;	/* < Validity flag */
	u8 u8Padding;	 /* < Padding byte */
	/*
	 * Structure for Convergence SubLayer Types with a maximum of 4 classifiers
	 */
	struct bcm_convergence_types cConvergenceSLTypes[MAX_CLASSIFIERS_IN_SF];
};

struct bcm_add_request {
	u8 u8Type;	/* < Type */
	u8 eConnectionDir; /* < Connection direction */
	/* brief 16 bit TID */
	u16 u16TID; /* < 16bit TID */
	/* brief 16bitCID */
	u16 u16CID; /* < 16bit CID */
	/* brief 16bitVCID */
	u16 u16VCID; /* < 16bit VCID */
	struct bcm_connect_mgr_params *psfParameterSet; /* < connection manager parameters */
};

struct bcm_add_indication {
	u8 u8Type;	/* < Type */
	u8 eConnectionDir;	/* < Connection Direction */
	/* brief 16 bit TID */
	u16 u16TID; /* < TID */
	/* brief 16bitCID */
	u16 u16CID; /* < 16bitCID */
	/* brief 16bitVCID */
	u16 u16VCID; /* < 16bitVCID */
	struct bcm_connect_mgr_params *psfAuthorizedSet; /* Authorized set of connection manager parameters */
	struct bcm_connect_mgr_params *psfAdmittedSet; /* Admitted set of connection manager parameters */
	struct bcm_connect_mgr_params *psfActiveSet; /* Activeset of connection manager parameters */
	u8 u8CC; /* <Confirmation Code */
	u8 u8Padd; /* < 8-bit Padding */
	u16 u16Padd; /* < 16 bit Padding */
};

struct bcm_del_request {
	u8 u8Type; /* < Type */
	u8 u8Padding; /* < Padding byte */
	u16 u16TID; /* < TID */
	/* brief 32bitSFID */
	u32 u32SFID; /* < SFID */
};

struct bcm_del_indication {
	u8 u8Type;	/* < Type */
	u8 u8Padding; /* < Padding */
	u16 u16TID; /* < TID */
	/* brief 16bitCID */
	u16 u16CID; /* < CID */
	/* brief 16bitVCID */
	u16 u16VCID; /* < VCID */
	/* brief 32bitSFID */
	u32 u32SFID; /* < SFID */
	/* brief 8bit Confirmation code */
	u8 u8ConfirmationCode; /* < Confirmation code */
	u8 u8Padding1[3]; /* < 3 byte Padding */
};

struct bcm_stim_sfhostnotify {
	u32 SFID; /* SFID of the service flow */
	u16 newCID; /* the new/changed CID */
	u16 VCID; /* Get new Vcid if the flow has been made active in CID update TLV, but was inactive earlier or the orig vcid */
	u8 RetainSF; /* Indication to Host if the SF is to be retained or deleted; if TRUE-retain else delete */
	u8 QoSParamSet; /* QoS paramset of the retained SF */
	u16 u16reserved; /* For byte alignment */
};

#endif
