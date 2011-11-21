/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/*
 * FDDI MIB
 */

/*
 * typedefs
 */

typedef	u_long	Counter ;
typedef u_char	TimeStamp[8] ;
typedef struct fddi_addr LongAddr ;
typedef	u_long	Timer_2 ;
typedef	u_long	Timer ;
typedef	u_short	ResId ;
typedef u_short	SMTEnum ;
typedef	u_char	SMTFlag ;

typedef struct {
	Counter		count ;
	TimeStamp	timestamp ;
} SetCountType ;

/*
 * bits for bit string "available_path"
 */
#define MIB_PATH_P	(1<<0)
#define MIB_PATH_S	(1<<1)
#define MIB_PATH_L	(1<<2)

/*
 * bits for bit string PermittedPaths & RequestedPaths (SIZE(8))
 */
#define MIB_P_PATH_LOCAL	(1<<0)
#define MIB_P_PATH_SEC_ALTER	(1<<1)
#define MIB_P_PATH_PRIM_ALTER	(1<<2)
#define MIB_P_PATH_CON_ALTER	(1<<3)
#define MIB_P_PATH_SEC_PREFER	(1<<4)
#define MIB_P_PATH_PRIM_PREFER	(1<<5)
#define MIB_P_PATH_CON_PREFER	(1<<6)
#define MIB_P_PATH_THRU		(1<<7)

/*
 * enum current path
 */
#define MIB_PATH_ISOLATED	0
#define MIB_PATH_LOCAL		1
#define MIB_PATH_SECONDARY	2
#define MIB_PATH_PRIMARY	3
#define MIB_PATH_CONCATENATED	4
#define MIB_PATH_THRU		5

/*
 * enum PMDClass
 */
#define MIB_PMDCLASS_MULTI	0
#define MIB_PMDCLASS_SINGLE1	1
#define MIB_PMDCLASS_SINGLE2	2
#define MIB_PMDCLASS_SONET	3
#define MIB_PMDCLASS_LCF	4
#define MIB_PMDCLASS_TP		5
#define MIB_PMDCLASS_UNKNOWN	6
#define MIB_PMDCLASS_UNSPEC	7

/*
 * enum SMTStationStatus
 */
#define MIB_SMT_STASTA_CON	0
#define MIB_SMT_STASTA_SEPA	1
#define MIB_SMT_STASTA_THRU	2


struct fddi_mib {
	/*
	 * private
	 */
	u_char			fddiPRPMFPasswd[8] ;
	struct smt_sid		fddiPRPMFStation ;

#ifdef	ESS
	/*
	 * private variables for static allocation of the
	 * End Station Support
	 */
	u_long	fddiESSPayload ;	/* payload for static alloc */
	u_long	fddiESSOverhead ;	/* frame ov for static alloc */
	u_long	fddiESSMaxTNeg ;	/* maximum of T-NEG */
	u_long	fddiESSMinSegmentSize ;	/* min size of the sync frames */
	u_long	fddiESSCategory ;	/* category for the Alloc req */
	short	fddiESSSynchTxMode ;	/* send all LLC frames as sync */
#endif	/* ESS */
#ifdef	SBA
	/*
	 * private variables for the Synchronous Bandwidth Allocator
	 */
	char	fddiSBACommand ;	/* holds the parsed SBA cmd */
	u_char	fddiSBAAvailable ;	/* SBA allocatable value */
#endif	/* SBA */

	/*
	 * SMT standard mib
	 */
	struct smt_sid		fddiSMTStationId ;
	u_short			fddiSMTOpVersionId ;
	u_short			fddiSMTHiVersionId ;
	u_short			fddiSMTLoVersionId ;
	u_char			fddiSMTManufacturerData[32] ;
	u_char			fddiSMTUserData[32] ;
	u_short			fddiSMTMIBVersionId ;

	/*
	 * ConfigGrp
	 */
	u_char			fddiSMTMac_Ct ;
	u_char			fddiSMTNonMaster_Ct ;
	u_char			fddiSMTMaster_Ct ;
	u_char			fddiSMTAvailablePaths ;
	u_short			fddiSMTConfigCapabilities ;
	u_short			fddiSMTConfigPolicy ;
	u_short			fddiSMTConnectionPolicy ;
	u_short			fddiSMTTT_Notify ;
	u_char			fddiSMTStatRptPolicy ;
	u_long			fddiSMTTrace_MaxExpiration ;
	u_short			fddiSMTPORTIndexes[NUMPHYS] ;
	u_short			fddiSMTMACIndexes ;
	u_char			fddiSMTBypassPresent ;

	/*
	 * StatusGrp
	 */
	SMTEnum			fddiSMTECMState ;
	SMTEnum			fddiSMTCF_State ;
	SMTEnum			fddiSMTStationStatus ;
	u_char			fddiSMTRemoteDisconnectFlag ;
	u_char			fddiSMTPeerWrapFlag ;

	/*
	 * MIBOperationGrp
	 */
	TimeStamp		fddiSMTTimeStamp ;
	TimeStamp		fddiSMTTransitionTimeStamp ;
	SetCountType		fddiSMTSetCount ;
	struct smt_sid		fddiSMTLastSetStationId ;

	struct fddi_mib_m {
		u_short		fddiMACFrameStatusFunctions ;
		Timer_2		fddiMACT_MaxCapabilitiy ;
		Timer_2		fddiMACTVXCapabilitiy ;

		/* ConfigGrp */
		u_char		fddiMACMultiple_N ;	/* private */
		u_char		fddiMACMultiple_P ;	/* private */
		u_char		fddiMACDuplicateAddressCond ;/* private */
		u_char		fddiMACAvailablePaths ;
		u_short		fddiMACCurrentPath ;
		LongAddr	fddiMACUpstreamNbr ;
		LongAddr	fddiMACDownstreamNbr ;
		LongAddr	fddiMACOldUpstreamNbr ;
		LongAddr	fddiMACOldDownstreamNbr ;
		SMTEnum		fddiMACDupAddressTest ;
		u_short		fddiMACRequestedPaths ;
		SMTEnum		fddiMACDownstreamPORTType ;
		ResId		fddiMACIndex ;

		/* AddressGrp */
		LongAddr	fddiMACSMTAddress ;

		/* OperationGrp */
		Timer_2		fddiMACT_Min ;	/* private */
		Timer_2		fddiMACT_ReqMIB ;
		Timer_2		fddiMACT_Req ;	/* private */
		Timer_2		fddiMACT_Neg ;
		Timer_2		fddiMACT_MaxMIB ;
		Timer_2		fddiMACT_Max ;	/* private */
		Timer_2		fddiMACTvxValueMIB ;
		Timer_2		fddiMACTvxValue ; /* private */
		Timer_2		fddiMACT_Pri0 ;
		Timer_2		fddiMACT_Pri1 ;
		Timer_2		fddiMACT_Pri2 ;
		Timer_2		fddiMACT_Pri3 ;
		Timer_2		fddiMACT_Pri4 ;
		Timer_2		fddiMACT_Pri5 ;
		Timer_2		fddiMACT_Pri6 ;

		/* CountersGrp */
		Counter		fddiMACFrame_Ct ;
		Counter		fddiMACCopied_Ct ;
		Counter		fddiMACTransmit_Ct ;
		Counter		fddiMACToken_Ct ;
		Counter		fddiMACError_Ct ;
		Counter		fddiMACLost_Ct ;
		Counter		fddiMACTvxExpired_Ct ;
		Counter		fddiMACNotCopied_Ct ;
		Counter		fddiMACRingOp_Ct ;

		Counter		fddiMACSMTCopied_Ct ;		/* private */
		Counter		fddiMACSMTTransmit_Ct ;		/* private */

		/* private for delta ratio */
		Counter		fddiMACOld_Frame_Ct ;
		Counter		fddiMACOld_Copied_Ct ;
		Counter		fddiMACOld_Error_Ct ;
		Counter		fddiMACOld_Lost_Ct ;
		Counter		fddiMACOld_NotCopied_Ct ;

		/* FrameErrorConditionGrp */
		u_short		fddiMACFrameErrorThreshold ;
		u_short		fddiMACFrameErrorRatio ;

		/* NotCopiedConditionGrp */
		u_short		fddiMACNotCopiedThreshold ;
		u_short		fddiMACNotCopiedRatio ;

		/* StatusGrp */
		SMTEnum		fddiMACRMTState ;
		SMTFlag		fddiMACDA_Flag ;
		SMTFlag		fddiMACUNDA_Flag ;
		SMTFlag		fddiMACFrameErrorFlag ;
		SMTFlag		fddiMACNotCopiedFlag ;
		SMTFlag		fddiMACMA_UnitdataAvailable ;
		SMTFlag		fddiMACHardwarePresent ;
		SMTFlag		fddiMACMA_UnitdataEnable ;

	} m[NUMMACS] ;
#define MAC0	0

	struct fddi_mib_a {
		ResId		fddiPATHIndex ;
		u_long		fddiPATHSbaPayload ;
		u_long		fddiPATHSbaOverhead ;
		/* fddiPATHConfiguration is built on demand */
		/* u_long		fddiPATHConfiguration ; */
		Timer		fddiPATHT_Rmode ;
		u_long		fddiPATHSbaAvailable ;
		Timer_2		fddiPATHTVXLowerBound ;
		Timer_2		fddiPATHT_MaxLowerBound ;
		Timer_2		fddiPATHMaxT_Req ;
	} a[NUMPATHS] ;
#define PATH0	0

	struct fddi_mib_p {
		/* ConfigGrp */
		SMTEnum		fddiPORTMy_Type ;
		SMTEnum		fddiPORTNeighborType ;
		u_char		fddiPORTConnectionPolicies ;
		struct {
			u_char	T_val ;
			u_char	R_val ;
		} fddiPORTMacIndicated ;
		SMTEnum		fddiPORTCurrentPath ;
		/* must be 4: is 32 bit in SMT format
		 * indices :
		 *	1	none
		 *	2	tree
		 *	3	peer
		 */
		u_char		fddiPORTRequestedPaths[4] ;
		u_short		fddiPORTMACPlacement ;
		u_char		fddiPORTAvailablePaths ;
		u_char		fddiPORTConnectionCapabilities ;
		SMTEnum		fddiPORTPMDClass ;
		ResId		fddiPORTIndex ;

		/* OperationGrp */
		SMTEnum		fddiPORTMaint_LS ;
		SMTEnum		fddiPORTPC_LS ;
		u_char		fddiPORTBS_Flag ;

		/* ErrorCtrsGrp */
		Counter		fddiPORTLCTFail_Ct ;
		Counter		fddiPORTEBError_Ct ;
		Counter		fddiPORTOldEBError_Ct ;

		/* LerGrp */
		Counter		fddiPORTLem_Reject_Ct ;
		Counter		fddiPORTLem_Ct ;
		u_char		fddiPORTLer_Estimate ;
		u_char		fddiPORTLer_Cutoff ;
		u_char		fddiPORTLer_Alarm ;

		/* StatusGrp */
		SMTEnum		fddiPORTConnectState ;
		SMTEnum		fddiPORTPCMState ;	/* real value */
		SMTEnum		fddiPORTPCMStateX ;	/* value for MIB */
		SMTEnum		fddiPORTPC_Withhold ;
		SMTFlag		fddiPORTHardwarePresent ;
		u_char		fddiPORTLerFlag ;

		u_char		fddiPORTMultiple_U ;	/* private */
		u_char		fddiPORTMultiple_P ;	/* private */
		u_char		fddiPORTEB_Condition ;	/* private */
	} p[NUMPHYS] ;
	struct {
		Counter		fddiPRIVECF_Req_Rx ;	/* ECF req received */
		Counter		fddiPRIVECF_Reply_Rx ;	/* ECF repl received */
		Counter		fddiPRIVECF_Req_Tx ;	/* ECF req transm */
		Counter		fddiPRIVECF_Reply_Tx ;	/* ECF repl transm */
		Counter		fddiPRIVPMF_Get_Rx ;	/* PMF Get rec */
		Counter		fddiPRIVPMF_Set_Rx ;	/* PMF Set rec */
		Counter		fddiPRIVRDF_Rx ;	/* RDF received */
		Counter		fddiPRIVRDF_Tx ;	/* RDF transmitted */
	} priv ;
} ;

/*
 * OIDs for statistics
 */
#define	SMT_OID_CF_STATE	1	/* fddiSMTCF_State */
#define	SMT_OID_PCM_STATE_A	2	/* fddiPORTPCMState port A */
#define	SMT_OID_PCM_STATE_B	17	/* fddiPORTPCMState port B */
#define	SMT_OID_RMT_STATE	3	/* fddiMACRMTState */
#define	SMT_OID_UNA		4	/* fddiMACUpstreamNbr */
#define	SMT_OID_DNA		5	/* fddiMACOldDownstreamNbr */
#define	SMT_OID_ERROR_CT	6	/* fddiMACError_Ct */
#define	SMT_OID_LOST_CT		7	/* fddiMACLost_Ct */
#define	SMT_OID_LEM_CT		8	/* fddiPORTLem_Ct */
#define	SMT_OID_LEM_CT_A	11	/* fddiPORTLem_Ct port A */
#define	SMT_OID_LEM_CT_B	12	/* fddiPORTLem_Ct port B */
#define	SMT_OID_LCT_FAIL_CT	9	/* fddiPORTLCTFail_Ct */
#define	SMT_OID_LCT_FAIL_CT_A	13	/* fddiPORTLCTFail_Ct port A */
#define	SMT_OID_LCT_FAIL_CT_B	14	/* fddiPORTLCTFail_Ct port B */
#define	SMT_OID_LEM_REJECT_CT	10	/* fddiPORTLem_Reject_Ct */
#define	SMT_OID_LEM_REJECT_CT_A	15	/* fddiPORTLem_Reject_Ct port A */
#define	SMT_OID_LEM_REJECT_CT_B	16	/* fddiPORTLem_Reject_Ct port B */

/*
 * SK MIB
 */
#define SMT_OID_ECF_REQ_RX	20	/* ECF requests received */
#define SMT_OID_ECF_REPLY_RX	21	/* ECF replies received */
#define SMT_OID_ECF_REQ_TX	22	/* ECF requests transmitted */
#define SMT_OID_ECF_REPLY_TX	23	/* ECF replies transmitted */
#define SMT_OID_PMF_GET_RX	24	/* PMF get requests received */
#define SMT_OID_PMF_SET_RX	25	/* PMF set requests received */
#define SMT_OID_RDF_RX		26	/* RDF received */
#define SMT_OID_RDF_TX		27	/* RDF transmitted */
