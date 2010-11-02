#ifndef CNTRL_SIGNALING_INTERFACE_
#define CNTRL_SIGNALING_INTERFACE_


#ifdef BECEEM_TARGET

#include <mac_common.h>
#include <msg_Dsa.h>
#include <msg_Dsc.h>
#include <msg_Dsd.h>
#include <sch_definitions.h>
using namespace Beceem;
#ifdef ENABLE_CORRIGENDUM2_UPDATE
extern B_UINT32 g_u32Corr2MacFlags;
#endif

#else


#define DSA_REQ 11
#define DSA_RSP 12
#define DSA_ACK 13
#define DSC_REQ 14
#define DSC_RSP 15
#define DSC_ACK 16
#define DSD_REQ 17
#define DSD_RSP 18
#define DSD_ACK 19
#define MAX_CLASSIFIERS_IN_SF  4

#endif

#define MAX_STRING_LEN 20
#define MAX_PHS_LENGTHS 255
#define VENDOR_PHS_PARAM_LENGTH 10
#define MAX_NUM_ACTIVE_BS 10
#define AUTH_TOKEN_LENGTH	10
#define NUM_HARQ_CHANNELS	16	//Changed from 10 to 16 to accomodate all HARQ channels
#define VENDOR_CLASSIFIER_PARAM_LENGTH 1 //Changed the size to 1 byte since we dnt use it
#define  VENDOR_SPECIF_QOS_PARAM 1
#define VENDOR_PHS_PARAM_LENGTH	10
#define MBS_CONTENTS_ID_LENGTH	10
#define GLOBAL_SF_CLASSNAME_LENGTH 6

#define TYPE_OF_SERVICE_LENGTH				3
#define IP_MASKED_SRC_ADDRESS_LENGTH			32
#define IP_MASKED_DEST_ADDRESS_LENGTH		32
#define PROTOCOL_SRC_PORT_RANGE_LENGTH		4
#define PROTOCOL_DEST_PORT_RANGE_LENGTH		4
#define ETHERNET_DEST_MAC_ADDR_LENGTH		12
#define ETHERNET_SRC_MAC_ADDR_LENGTH		12
#define NUM_ETHERTYPE_BYTES  3
#define NUM_IPV6_FLOWLABLE_BYTES 3


////////////////////////////////////////////////////////////////////////////////
////////////////////////structure Definitions///////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
/// \brief class cCPacketClassificationRule
#ifdef BECEEM_TARGET
class CCPacketClassificationRuleSI{
	public:
		/// \brief Constructor for the class
	CCPacketClassificationRuleSI():
		u8ClassifierRulePriority(mClassifierRulePriority),
		u8IPTypeOfServiceLength(mIPTypeOfService),
		u8Protocol(mProtocol),
		u8IPMaskedSourceAddressLength(0),
		u8IPDestinationAddressLength(0),
		u8ProtocolSourcePortRangeLength(0),
		u8ProtocolDestPortRangeLength(0),
		u8EthernetDestMacAddressLength(0),
		u8EthernetSourceMACAddressLength(0),
		u8EthertypeLength(0),
		u16UserPriority(mUserPriority),
		u16VLANID(mVLANID),
		u8AssociatedPHSI(mAssociatedPHSI),
		u16PacketClassificationRuleIndex(mPacketClassifierRuleIndex),
		u8VendorSpecificClassifierParamLength(mVendorSpecificClassifierParamLength),
		u8IPv6FlowLableLength(mIPv6FlowLableLength),
		u8ClassifierActionRule(mClassifierActionRule)

		{}
              void Reset()
              {
                    CCPacketClassificationRuleSI();
              }
#else
struct _stCPacketClassificationRuleSI{
#endif

	/**  16bit UserPriority Of The Service Flow*/
    B_UINT16                        u16UserPriority;
	/**  16bit VLANID Of The Service Flow*/
    B_UINT16                        u16VLANID;
	/**  16bit Packet Classification RuleIndex Of The Service Flow*/
    B_UINT16                        u16PacketClassificationRuleIndex;
	/**  8bit Classifier Rule Priority Of The Service Flow*/
    B_UINT8                         u8ClassifierRulePriority;
	/**  Length of IP TypeOfService field*/
	B_UINT8                         u8IPTypeOfServiceLength;
	/**  3bytes IP TypeOfService */
    B_UINT8                         u8IPTypeOfService[TYPE_OF_SERVICE_LENGTH];
	/** Protocol used in classification of Service Flow*/
    B_UINT8                         u8Protocol;
	/**  Length of IP Masked Source Address */
    B_UINT8                         u8IPMaskedSourceAddressLength;
	/**  IP Masked Source Address used in classification for the Service Flow*/
    B_UINT8                         u8IPMaskedSourceAddress[IP_MASKED_SRC_ADDRESS_LENGTH];
	/**  Length of IP Destination Address */
    B_UINT8                         u8IPDestinationAddressLength;
	/**  IP Destination Address used in classification for the Service Flow*/
    B_UINT8                         u8IPDestinationAddress[IP_MASKED_DEST_ADDRESS_LENGTH];
	/** Length of Protocol Source Port Range */
    B_UINT8                         u8ProtocolSourcePortRangeLength;
	/**  Protocol Source Port Range used in the Service Flow*/
    B_UINT8                         u8ProtocolSourcePortRange[PROTOCOL_SRC_PORT_RANGE_LENGTH];
	/** Length of Protocol Dest Port Range */
    B_UINT8                         u8ProtocolDestPortRangeLength;
	/**  Protocol Dest Port Range used in the Service Flow*/
    B_UINT8                         u8ProtocolDestPortRange[PROTOCOL_DEST_PORT_RANGE_LENGTH];
	/** Length of Ethernet Destination MAC Address  */
    B_UINT8                         u8EthernetDestMacAddressLength;
	/**  Ethernet Destination MAC Address  used in classification of the Service Flow*/
    B_UINT8                         u8EthernetDestMacAddress[ETHERNET_DEST_MAC_ADDR_LENGTH];
	/** Length of Ethernet Source MAC Address  */
    B_UINT8                         u8EthernetSourceMACAddressLength;
	/**  Ethernet Source MAC Address  used in classification of the Service Flow*/
    B_UINT8                         u8EthernetSourceMACAddress[ETHERNET_SRC_MAC_ADDR_LENGTH];
	/**  Length of Ethertype */
	B_UINT8                         u8EthertypeLength;
	/**  3bytes Ethertype Of The Service Flow*/
    B_UINT8                         u8Ethertype[NUM_ETHERTYPE_BYTES];
	/**  8bit Associated PHSI Of The Service Flow*/
    B_UINT8                         u8AssociatedPHSI;
	/** Length of Vendor Specific Classifier Param length Of The Service Flow*/
    B_UINT8                         u8VendorSpecificClassifierParamLength;
	/**  Vendor Specific Classifier Param Of The Service Flow*/
    B_UINT8                         u8VendorSpecificClassifierParam[VENDOR_CLASSIFIER_PARAM_LENGTH];
    /** Length Of IPv6 Flow Lable of the Service Flow*/
    B_UINT8                         u8IPv6FlowLableLength;
	/**  IPv6 Flow Lable Of The Service Flow*/
    B_UINT8                         u8IPv6FlowLable[NUM_IPV6_FLOWLABLE_BYTES];
	/**  Action associated with the classifier rule*/
    B_UINT8							u8ClassifierActionRule;
    B_UINT16							u16ValidityBitMap;
};
#ifndef BECEEM_TARGET
typedef struct _stCPacketClassificationRuleSI CCPacketClassificationRuleSI,stCPacketClassificationRuleSI, *pstCPacketClassificationRuleSI;
#endif

/// \brief class CPhsRuleSI
#ifdef BECEEM_TARGET
class CPhsRuleSI{
	public:
		/// \brief Constructor for the class
		CPhsRuleSI():
			u8PHSI(mPHSI),
			u8PHSFLength(0),
			u8PHSMLength(0),
			u8PHSS(mPHSS),
			u8PHSV(mPHSV),
			u8VendorSpecificPHSParamsLength(mVendorSpecificPHSParamLength){}
                void Reset()
                {
        		CPhsRuleSI();
                }
#else
typedef struct _stPhsRuleSI {
#endif
	/**  8bit PHS Index Of The Service Flow*/
    B_UINT8                         u8PHSI;
	/**  PHSF Length Of The Service Flow*/
    B_UINT8                         u8PHSFLength;
    /** String of bytes containing header information to be supressed by the sending CS and reconstructed by the receiving CS*/
    B_UINT8                         u8PHSF[MAX_PHS_LENGTHS];
	/**  PHSM Length Of The Service Flow*/
    B_UINT8                         u8PHSMLength;
	/**  PHS Mask for the SF*/
    B_UINT8                         u8PHSM[MAX_PHS_LENGTHS];
	/**  8bit Total number of bytes to be supressed for the Service Flow*/
    B_UINT8                         u8PHSS;
	/**  8bit Indicates whether or not Packet Header contents need to be verified prior to supression */
    B_UINT8                         u8PHSV;
	/**  Vendor Specific PHS param Length Of The Service Flow*/
    B_UINT8                         u8VendorSpecificPHSParamsLength;
	/**  Vendor Specific PHS param Of The Service Flow*/
    B_UINT8                         u8VendorSpecificPHSParams[VENDOR_PHS_PARAM_LENGTH];

	B_UINT8                         u8Padding[2];
#ifdef BECEEM_TARGET
};
#else
}stPhsRuleSI,*pstPhsRuleSI;
typedef stPhsRuleSI CPhsRuleSI;
#endif

/// \brief structure cConvergenceSLTypes
#ifdef BECEEM_TARGET
class CConvergenceSLTypes{
	public:
		/// \brief Constructor for the class
		CConvergenceSLTypes():
		u8ClassfierDSCAction(mClassifierDSCAction),
		u8PhsDSCAction	(mPhsDSCAction)
		{}
              void Reset()
              {
                    CConvergenceSLTypes();
                    cCPacketClassificationRule.Reset();
                    cPhsRule.Reset();
              }
#else
struct _stConvergenceSLTypes{
#endif
	/**  8bit Phs Classfier Action Of The Service Flow*/
    B_UINT8                         u8ClassfierDSCAction;
	/**  8bit Phs DSC Action Of The Service Flow*/
    B_UINT8                         u8PhsDSCAction;
	/**   16bit Padding */
    B_UINT8                         u8Padding[2];
    /// \brief class cCPacketClassificationRule
#ifdef BECEEM_TARGET
    CCPacketClassificationRuleSI      cCPacketClassificationRule;
#else
    stCPacketClassificationRuleSI     cCPacketClassificationRule;
#endif
    /// \brief class CPhsRuleSI
#ifdef BECEEM_TARGET
    CPhsRuleSI				cPhsRule;
#else
     struct _stPhsRuleSI		cPhsRule;
#endif
};
#ifndef BECEEM_TARGET
typedef struct _stConvergenceSLTypes stConvergenceSLTypes,CConvergenceSLTypes, *pstConvergenceSLTypes;
#endif


/// \brief structure CServiceFlowParamSI
#ifdef BECEEM_TARGET
class CServiceFlowParamSI{
	public:
		/// \brief Constructor for the class
		CServiceFlowParamSI():
			u32SFID(mSFid),
			u16CID(mCid),
			u8ServiceClassNameLength(mServiceClassNameLength),
			u8MBSService(mMBSService),
			u8QosParamSet(mQosParamSetType),
			u8TrafficPriority(mTrafficPriority),
			u32MaxSustainedTrafficRate(mMaximumSustainedTrafficRate),
			u32MaxTrafficBurst(mMaximumTrafficBurst),
			u32MinReservedTrafficRate(mMinimumReservedTrafficRate),
			u8ServiceFlowSchedulingType(mServiceFlowSchedulingType),
			u8RequesttransmissionPolicy(mRequestTransmissionPolicy),
			u32ToleratedJitter(mToleratedJitter),
			u32MaximumLatency(mMaximumLatency),
			u8FixedLengthVSVariableLengthSDUIndicator
			(mFixedLengthVSVariableLength),
			u8SDUSize(mSDUSize),
			u16TargetSAID(mTargetSAID),
			u8ARQEnable(mARQEnable),
			u16ARQWindowSize(mARQWindowSize),
			u16ARQBlockLifeTime(mARQBlockLifeTime),
			u16ARQSyncLossTimeOut(mARQSyncLossTimeOut),
			u8ARQDeliverInOrder(mARQDeliverInOrder),
			u16ARQRxPurgeTimeOut(mARQRXPurgeTimeOut),
			//Add ARQ BLOCK SIZE, ARQ TX and RX delay initializations here
			//after we move to only CORR2
			u8RxARQAckProcessingTime(mRxARQAckProcessingTime),
			u8CSSpecification(mCSSpecification),
			u8TypeOfDataDeliveryService(mTypeOfDataDeliveryService),
			u16SDUInterArrivalTime(mSDUInterArrivalTime),
			u16TimeBase(mTimeBase),
			u8PagingPreference(mPagingPreference),
			u8MBSZoneIdentifierassignment(mMBSZoneIdentifierassignmentLength),
			u8TrafficIndicationPreference(mTrafficIndicationPreference),
			u8GlobalServicesClassNameLength(mGlobalServicesClassNameLength),
			u8SNFeedbackEnabled(mSNFeedbackEnabled),
			u8FSNSize(mFSNSize),
			u8CIDAllocation4activeBSsLength(mCIDAllocation4activeBSsLength),
			u16UnsolicitedGrantInterval(mUnsolicitedGrantInterval),
			u16UnsolicitedPollingInterval(mUnsolicitedPollingInterval),
			u8PDUSNExtendedSubheader4HarqReordering(mPDUSNExtendedSubheader4HarqReordering),
			u8MBSContentsIDLength(mMBSContentsIDLength),
			u8HARQServiceFlows(mHARQServiceFlows),
			u8AuthTokenLength(mAuthTokenLength),
			u8HarqChannelMappingLength(mHarqChannelMappingLength),
			u8VendorSpecificQoSParamLength(mVendorSpecificQoSParamLength),
            bValid(FALSE),
	     u8TotalClassifiers()
{
//Remove the bolck after we move to Corr2 only code
#ifdef ENABLE_CORRIGENDUM2_UPDATE
	if((g_u32Corr2MacFlags & CORR_2_DSX)  ||  (g_u32Corr2MacFlags & CORR_2_ARQ))
	{
	/* IEEE Comment #627 / MTG Comment #426 */
       	u16ARQBlockSize = mARQBlockSize;
		if(g_u32Corr2MacFlags & CORR_2_ARQ) {
			u16ARQRetryTxTimeOut = mARQRetryTimeOutTxDelay;
			if(g_u32VENDOR_TYPE == VENDOR_ALCATEL) {
				u16ARQRetryRxTimeOut = mARQRetryTimeOutRxDelay_ALU;
			} else {
				u16ARQRetryRxTimeOut = mARQRetryTimeOutRxDelay;
			}
		}
		else
		{
			u16ARQRetryTxTimeOut = mARQRetryTimeOutTxDelayCorr1;
			u16ARQRetryRxTimeOut = mARQRetryTimeOutRxDelayCorr1;
		}
	}
	else
#endif
	{
		u16ARQBlockSize = mARQBlockSizeCorr1;
		u16ARQRetryTxTimeOut = mARQRetryTimeOutTxDelayCorr1;
		u16ARQRetryRxTimeOut = mARQRetryTimeOutRxDelayCorr1;
	}
}

	void ComputeMacOverhead(B_UINT8	u8SecOvrhead);
	B_UINT16	GetMacOverhead() { return 	u16MacOverhead; }
#else
typedef struct _stServiceFlowParamSI{
#endif //end of ifdef BECEEM_TARGET

     /**  32bitSFID Of The Service Flow*/
    B_UINT32                        u32SFID;

     /**  32bit Maximum Sustained Traffic Rate of the Service Flow*/
    B_UINT32                        u32MaxSustainedTrafficRate;

     /**  32bit Maximum Traffic Burst allowed for the Service Flow*/
    B_UINT32                        u32MaxTrafficBurst;

    /**  32bit Minimum Reserved Traffic Rate of the Service Flow*/
    B_UINT32                        u32MinReservedTrafficRate;

	/**  32bit Tolerated Jitter of the Service Flow*/
    	B_UINT32                        u32ToleratedJitter;

   /**  32bit Maximum Latency of the Service Flow*/
    B_UINT32                        u32MaximumLatency;

	/**  16bitCID Of The Service Flow*/
    B_UINT16                        u16CID;

     /**  16bit SAID on which the service flow being set up shall be mapped*/
    B_UINT16                        u16TargetSAID;

	/** 16bit  ARQ window size negotiated*/
    B_UINT16                        u16ARQWindowSize;

     /**  16bit Total Tx delay incl sending, receiving & processing delays 	*/
    B_UINT16                        u16ARQRetryTxTimeOut;

	/**  16bit Total Rx delay incl sending, receiving & processing delays 	*/
    B_UINT16                        u16ARQRetryRxTimeOut;

	/**  16bit ARQ block lifetime	*/
    B_UINT16                        u16ARQBlockLifeTime;

	/**  16bit ARQ Sync loss timeout*/
    B_UINT16                        u16ARQSyncLossTimeOut;

	 /**  16bit ARQ Purge timeout */
    B_UINT16                        u16ARQRxPurgeTimeOut;
#if 0 //def ENABLE_CORRIGENDUM2_UPDATE
/* IEEE Comment #627 / MTG Comment #426 */
    /// \brief Size of an ARQ block, changed from 2 bytes to 1
    B_UINT8                        u8ARQBlockSize;
#endif
//TODO::Remove this once we move to a new CORR2 driver
    /// \brief Size of an ARQ block
    B_UINT16                        u16ARQBlockSize;

//#endif
	/**  16bit Nominal interval b/w consecutive SDU arrivals at MAC SAP*/
	B_UINT16                        u16SDUInterArrivalTime;

	/**  16bit Specifies the time base for rate measurement 	*/
	B_UINT16                        u16TimeBase;

	 /** 16bit Interval b/w Successive Grant oppurtunities*/
	B_UINT16                        u16UnsolicitedGrantInterval;

	/** 16bit Interval b/w Successive Polling grant oppurtunities*/
	B_UINT16						u16UnsolicitedPollingInterval;

	 /**   internal var to get the overhead */
	B_UINT16						u16MacOverhead;

	 /**  MBS contents Identifier*/
	B_UINT16						u16MBSContentsID[MBS_CONTENTS_ID_LENGTH];

	/**  MBS contents Identifier length*/
	B_UINT8							u8MBSContentsIDLength;

	/**	 ServiceClassName Length Of The Service Flow*/
    B_UINT8                         u8ServiceClassNameLength;

	/**  32bytes ServiceClassName Of The Service Flow*/
    B_UINT8                         u8ServiceClassName[32];

	/**  8bit Indicates whether or not MBS service is requested for this Serivce Flow*/
	B_UINT8							u8MBSService;

    /**  8bit QOS Parameter Set specifies proper application of QoS paramters to Provisioned, Admitted and Active sets*/
    B_UINT8                         u8QosParamSet;

   /**  8bit Traffic Priority Of the Service Flow */
    B_UINT8                         u8TrafficPriority;

   /**  8bit Uplink Grant Scheduling Type of The Service Flow */
    B_UINT8                         u8ServiceFlowSchedulingType;

  /**  8bit Request transmission Policy of the Service Flow*/
    B_UINT8							u8RequesttransmissionPolicy;

	/**  8bit Specifies whether SDUs for this Service flow are of FixedLength or Variable length */
    B_UINT8                         u8FixedLengthVSVariableLengthSDUIndicator;

	/**  8bit Length of the SDU for a fixed length SDU service flow*/
	B_UINT8                         u8SDUSize;

	 /** 8bit Indicates whether or not ARQ is requested for this connection*/
       B_UINT8                         u8ARQEnable;

	/**<  8bit Indicates whether or not data has tobe delivered in order to higher layer*/
       B_UINT8                         u8ARQDeliverInOrder;

	/**  8bit Receiver ARQ ACK processing time */
	B_UINT8                         u8RxARQAckProcessingTime;

	/**  8bit Convergence Sublayer Specification Of The Service Flow*/
       B_UINT8                         u8CSSpecification;

	 /**  8 bit Type of data delivery service*/
	B_UINT8                         u8TypeOfDataDeliveryService;

	/** 8bit Specifies whether a service flow may generate Paging	*/
	B_UINT8                         u8PagingPreference;

	/**  8bit Indicates the MBS Zone through which the connection or virtual connection is valid	*/
       B_UINT8                         u8MBSZoneIdentifierassignment;

       /**  8bit Specifies whether traffic on SF should generate MOB_TRF_IND to MS in sleep mode*/
	B_UINT8                         u8TrafficIndicationPreference;

	/** 8bit Speciifes the length of predefined Global QoS parameter set encoding for this SF	*/
	B_UINT8                         u8GlobalServicesClassNameLength;

	 /**  6 byte Speciifes the predefined Global QoS parameter set encoding for this SF	*/
	B_UINT8                         u8GlobalServicesClassName[GLOBAL_SF_CLASSNAME_LENGTH];

	 /**  8bit Indicates whether or not SN feedback is enabled for the conn	*/
	B_UINT8                         u8SNFeedbackEnabled;

	 /**  Indicates the size of the Fragment Sequence Number for the connection	*/
	B_UINT8                         u8FSNSize;

	/** 8bit Number of CIDs in active BS list 	*/
	B_UINT8							u8CIDAllocation4activeBSsLength;

	/**  CIDs of BS in the active list	*/
	B_UINT8							u8CIDAllocation4activeBSs[MAX_NUM_ACTIVE_BS];

	 /**  Specifies if PDU extended subheader should be applied on every PDU on this conn*/
	B_UINT8                         u8PDUSNExtendedSubheader4HarqReordering;

	 /**  8bit Specifies whether the connection uses HARQ or not	*/
	B_UINT8                         u8HARQServiceFlows;

	/**  Specifies the length of Authorization token*/
	B_UINT8							u8AuthTokenLength;

	/**  Specifies the Authorization token*/
	B_UINT8							u8AuthToken[AUTH_TOKEN_LENGTH];

	/**  specifes Number of HARQ channels used to carry data length*/
	B_UINT8							u8HarqChannelMappingLength;

	 /**  specifes HARQ channels used to carry data*/
	B_UINT8							u8HARQChannelMapping[NUM_HARQ_CHANNELS];

	/**  8bit Length of Vendor Specific QoS Params */
    B_UINT8                         u8VendorSpecificQoSParamLength;

	/** 1byte  Vendor Specific QoS Param Of The Service Flow*/
    B_UINT8                          u8VendorSpecificQoSParam[VENDOR_SPECIF_QOS_PARAM];

	// indicates total classifiers in the SF
	B_UINT8                         u8TotalClassifiers;  /**< Total number of valid classifiers*/
	B_UINT8							bValid;	/**<  Validity flag */
	B_UINT8				u8Padding;	 /**<  Padding byte*/

#ifdef BECEEM_TARGET
/**
Structure for Convergence SubLayer Types with a maximum of 4 classifiers
*/
	CConvergenceSLTypes		cConvergenceSLTypes[MAX_CLASSIFIERS_IN_SF];
#else
/**
Structure for Convergence SubLayer Types with a maximum of 4 classifiers
*/
	stConvergenceSLTypes		cConvergenceSLTypes[MAX_CLASSIFIERS_IN_SF];
#endif

#ifdef BECEEM_TARGET
};
#else
} stServiceFlowParamSI, *pstServiceFlowParamSI;
typedef stServiceFlowParamSI CServiceFlowParamSI;
#endif

/**
structure stLocalSFAddRequest
*/
typedef struct _stLocalSFAddRequest{
#ifdef BECEEM_TARGET
	   _stLocalSFAddRequest( ) :
	   	u8Type(0x00),  eConnectionDir(0x00),
		u16TID(0x0000), u16CID(0x0000),  u16VCID(0x0000)
	   		{}
#endif

	B_UINT8                         u8Type;	/**<  Type*/
	B_UINT8      eConnectionDir;		/**<  Connection direction*/
	/// \brief 16 bit TID
	B_UINT16                        u16TID;	/**<  16bit TID*/
	/// \brief 16bitCID
    	B_UINT16                        u16CID;	/**<  16bit CID*/
	/// \brief 16bitVCID
	B_UINT16                        u16VCID;	/**<  16bit VCID*/
    /// \brief structure ParameterSet
#ifdef BECEEM_SIGNALLING_INTERFACE_API
	CServiceFlowParamSI sfParameterSet;
#endif

#ifdef BECEEM_TARGET
    CServiceFlowParamSI              *psfParameterSet;
#else
	stServiceFlowParamSI	*psfParameterSet;	/**<  structure ParameterSet*/
#endif

#ifdef USING_VXWORKS
    USE_DATA_MEMORY_MANAGER();
#endif
}stLocalSFAddRequest, *pstLocalSFAddRequest;


/**
structure stLocalSFAddIndication
*/
typedef struct _stLocalSFAddIndication{
#ifdef BECEEM_TARGET
	   _stLocalSFAddIndication( ) :
	   	u8Type(0x00),  eConnectionDir(0x00),
		u16TID(0x0000), u16CID(0x0000),  u16VCID(0x0000)
	   		{}
#endif

	B_UINT8                         u8Type;	/**<  Type*/
	B_UINT8      eConnectionDir;	/**<  Connection Direction*/
	/// \brief 16 bit TID
	B_UINT16                         u16TID;	/**<  TID*/
    /// \brief 16bitCID
    B_UINT16                        u16CID;		/**<  16bitCID*/
    /// \brief 16bitVCID
    B_UINT16                        u16VCID;	 /**<  16bitVCID*/

#ifdef 	BECEEM_SIGNALLING_INTERFACE_API
	CServiceFlowParamSI              sfAuthorizedSet;
    /// \brief structure AdmittedSet
    CServiceFlowParamSI              sfAdmittedSet;
    /// \brief structure ActiveSet
    CServiceFlowParamSI              sfActiveSet;
#endif

    /// \brief structure AuthorizedSet
#ifdef BECEEM_TARGET
    CServiceFlowParamSI              *psfAuthorizedSet;
    /// \brief structure AdmittedSet
    CServiceFlowParamSI              *psfAdmittedSet;
    /// \brief structure ActiveSet
    CServiceFlowParamSI              *psfActiveSet;
#else
    /// \brief structure AuthorizedSet
    stServiceFlowParamSI              *psfAuthorizedSet;	/**<  AuthorizedSet of type stServiceFlowParamSI*/
    /// \brief structure AdmittedSet
    stServiceFlowParamSI              *psfAdmittedSet;	/**<  AdmittedSet of type stServiceFlowParamSI*/
    /// \brief structure ActiveSet
    stServiceFlowParamSI              *psfActiveSet;	/**<  sfActiveSet of type stServiceFlowParamSI*/
#endif
	B_UINT8				   u8CC;	/**<  Confirmation Code*/
	B_UINT8				   u8Padd;		/**<  8-bit Padding */

    B_UINT16               u16Padd;	/**< 16 bit Padding */

#ifdef USING_VXWORKS
    USE_DATA_MEMORY_MANAGER();
#endif
}stLocalSFAddIndication;


typedef struct _stLocalSFAddIndication *pstLocalSFAddIndication;
/**
structure stLocalSFChangeRequest is same as structure stLocalSFAddIndication
*/
typedef struct _stLocalSFAddIndication stLocalSFChangeRequest, *pstLocalSFChangeRequest;
/**
structure stLocalSFChangeIndication is same as structure stLocalSFAddIndication
*/
typedef struct _stLocalSFAddIndication stLocalSFChangeIndication, *pstLocalSFChangeIndication;

/**
structure stLocalSFDeleteRequest
*/
typedef struct _stLocalSFDeleteRequest{
#ifdef BECEEM_TARGET
	   _stLocalSFDeleteRequest( ) :
	   	u8Type(0x00),  u8Padding(0x00),
		u16TID(0x0000), u32SFID (0x00000000)
	   		{}
#endif
	B_UINT8                         u8Type;	 /**< Type*/
	B_UINT8                         u8Padding;	 /**<  Padding byte*/
	B_UINT16			u16TID;		 /**<  TID*/
    /// \brief 32bitSFID
    B_UINT32                        u32SFID;	 /**<  SFID*/
#ifdef USING_VXWORKS
    USE_DATA_MEMORY_MANAGER();
#endif
}stLocalSFDeleteRequest, *pstLocalSFDeleteRequest;

/**
structure stLocalSFDeleteIndication
*/
typedef struct stLocalSFDeleteIndication{
#ifdef BECEEM_TARGET
	   stLocalSFDeleteIndication( ) :
	   	u8Type(0x00),  u8Padding(0x00),
		u16TID(0x0000), u16CID(0x0000),
		u16VCID(0x0000),u32SFID (0x00000000)
	   		{}
#endif
	B_UINT8                         u8Type;	/**< Type */
	B_UINT8                         u8Padding;	/**< Padding  */
	B_UINT16			u16TID;			/**< TID */
       /// \brief 16bitCID
    B_UINT16                        u16CID;			/**< CID */
    /// \brief 16bitVCID
    B_UINT16                        u16VCID;		/**< VCID */
    /// \brief 32bitSFID
    B_UINT32                        u32SFID; 		/**< SFID */
	/// \brief 8bit Confirmation code
	B_UINT8                         u8ConfirmationCode;	/**< Confirmation code */
	B_UINT8                         u8Padding1[3];		/**< 3 byte Padding  */
#ifdef USING_VXWORKS
    USE_DATA_MEMORY_MANAGER();
#endif
}stLocalSFDeleteIndication;

typedef struct _stIM_SFHostNotify
{
	B_UINT32 	SFID;      //SFID of the service flow
	B_UINT16 	newCID;   //the new/changed CID
	B_UINT16 	VCID;             //Get new Vcid if the flow has been made active in CID update TLV, but was inactive earlier or the orig vcid
	B_UINT8  	RetainSF;        //Indication to Host if the SF is to be retained or deleted; if TRUE-retain else delete
	B_UINT8 	QoSParamSet; //QoS paramset of the retained SF
	B_UINT16 	u16reserved;  //For byte alignment

} stIM_SFHostNotify;

#endif
