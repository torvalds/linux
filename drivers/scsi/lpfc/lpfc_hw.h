/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017 Broadcom. All Rights Reserved. The term      *
 * “Broadcom” refers to Broadcom Limited and/or its subsidiaries.  *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#define FDMI_DID        0xfffffaU
#define NameServer_DID  0xfffffcU
#define SCR_DID         0xfffffdU
#define Fabric_DID      0xfffffeU
#define Bcast_DID       0xffffffU
#define Mask_DID        0xffffffU
#define CT_DID_MASK     0xffff00U
#define Fabric_DID_MASK 0xfff000U
#define WELL_KNOWN_DID_MASK 0xfffff0U

#define PT2PT_LocalID	1
#define PT2PT_RemoteID	2

#define FF_DEF_EDTOV          2000	/* Default E_D_TOV (2000ms) */
#define FF_DEF_ALTOV            15	/* Default AL_TIME (15ms) */
#define FF_DEF_RATOV            10	/* Default RA_TOV (10s) */
#define FF_DEF_ARBTOV         1900	/* Default ARB_TOV (1900ms) */

#define LPFC_BUF_RING0        64	/* Number of buffers to post to RING
					   0 */

#define FCELSSIZE             1024	/* maximum ELS transfer size */

#define LPFC_FCP_RING            0	/* ring 0 for FCP initiator commands */
#define LPFC_EXTRA_RING          1	/* ring 1 for other protocols */
#define LPFC_ELS_RING            2	/* ring 2 for ELS commands */

#define SLI2_IOCB_CMD_R0_ENTRIES    172	/* SLI-2 FCP command ring entries */
#define SLI2_IOCB_RSP_R0_ENTRIES    134	/* SLI-2 FCP response ring entries */
#define SLI2_IOCB_CMD_R1_ENTRIES      4	/* SLI-2 extra command ring entries */
#define SLI2_IOCB_RSP_R1_ENTRIES      4	/* SLI-2 extra response ring entries */
#define SLI2_IOCB_CMD_R1XTRA_ENTRIES 36	/* SLI-2 extra FCP cmd ring entries */
#define SLI2_IOCB_RSP_R1XTRA_ENTRIES 52	/* SLI-2 extra FCP rsp ring entries */
#define SLI2_IOCB_CMD_R2_ENTRIES     20	/* SLI-2 ELS command ring entries */
#define SLI2_IOCB_RSP_R2_ENTRIES     20	/* SLI-2 ELS response ring entries */
#define SLI2_IOCB_CMD_R3_ENTRIES      0
#define SLI2_IOCB_RSP_R3_ENTRIES      0
#define SLI2_IOCB_CMD_R3XTRA_ENTRIES 24
#define SLI2_IOCB_RSP_R3XTRA_ENTRIES 32

#define SLI2_IOCB_CMD_SIZE	32
#define SLI2_IOCB_RSP_SIZE	32
#define SLI3_IOCB_CMD_SIZE	128
#define SLI3_IOCB_RSP_SIZE	64

#define LPFC_UNREG_ALL_RPIS_VPORT	0xffff
#define LPFC_UNREG_ALL_DFLT_RPIS	0xffffffff

/* vendor ID used in SCSI netlink calls */
#define LPFC_NL_VENDOR_ID (SCSI_NL_VID_TYPE_PCI | PCI_VENDOR_ID_EMULEX)

#define FW_REV_STR_SIZE	32
/* Common Transport structures and definitions */

union CtRevisionId {
	/* Structure is in Big Endian format */
	struct {
		uint32_t Revision:8;
		uint32_t InId:24;
	} bits;
	uint32_t word;
};

union CtCommandResponse {
	/* Structure is in Big Endian format */
	struct {
		uint32_t CmdRsp:16;
		uint32_t Size:16;
	} bits;
	uint32_t word;
};

/* FC4 Feature bits for RFF_ID */
#define FC4_FEATURE_TARGET	0x1
#define FC4_FEATURE_INIT	0x2
#define FC4_FEATURE_NVME_DISC	0x4

struct lpfc_sli_ct_request {
	/* Structure is in Big Endian format */
	union CtRevisionId RevisionId;
	uint8_t FsType;
	uint8_t FsSubType;
	uint8_t Options;
	uint8_t Rsrvd1;
	union CtCommandResponse CommandResponse;
	uint8_t Rsrvd2;
	uint8_t ReasonCode;
	uint8_t Explanation;
	uint8_t VendorUnique;
#define LPFC_CT_PREAMBLE	20	/* Size of CTReq + 4 up to here */

	union {
		uint32_t PortID;
		struct gid {
			uint8_t PortType;	/* for GID_PT requests */
			uint8_t DomainScope;
			uint8_t AreaScope;
			uint8_t Fc4Type;	/* for GID_FT requests */
		} gid;
		struct gid_ff {
			uint8_t Flags;
			uint8_t DomainScope;
			uint8_t AreaScope;
			uint8_t rsvd1;
			uint8_t rsvd2;
			uint8_t rsvd3;
			uint8_t Fc4FBits;
			uint8_t Fc4Type;
		} gid_ff;
		struct rft {
			uint32_t PortId;	/* For RFT_ID requests */

#ifdef __BIG_ENDIAN_BITFIELD
			uint32_t rsvd0:16;
			uint32_t rsvd1:7;
			uint32_t fcpReg:1;	/* Type 8 */
			uint32_t rsvd2:2;
			uint32_t ipReg:1;	/* Type 5 */
			uint32_t rsvd3:5;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
			uint32_t rsvd0:16;
			uint32_t fcpReg:1;	/* Type 8 */
			uint32_t rsvd1:7;
			uint32_t rsvd3:5;
			uint32_t ipReg:1;	/* Type 5 */
			uint32_t rsvd2:2;
#endif

			uint32_t rsvd[7];
		} rft;
		struct rnn {
			uint32_t PortId;	/* For RNN_ID requests */
			uint8_t wwnn[8];
		} rnn;
		struct rsnn {	/* For RSNN_ID requests */
			uint8_t wwnn[8];
			uint8_t len;
			uint8_t symbname[255];
		} rsnn;
		struct da_id { /* For DA_ID requests */
			uint32_t port_id;
		} da_id;
		struct rspn {	/* For RSPN_ID requests */
			uint32_t PortId;
			uint8_t len;
			uint8_t symbname[255];
		} rspn;
		struct gff {
			uint32_t PortId;
		} gff;
		struct gff_acc {
			uint8_t fbits[128];
		} gff_acc;
		struct gft {
			uint32_t PortId;
		} gft;
		struct gft_acc {
			uint32_t fc4_types[8];
		} gft_acc;
#define FCP_TYPE_FEATURE_OFFSET 7
		struct rff {
			uint32_t PortId;
			uint8_t reserved[2];
			uint8_t fbits;
			uint8_t type_code;     /* type=8 for FCP */
		} rff;
	} un;
};

#define LPFC_MAX_CT_SIZE	(60 * 4096)

#define  SLI_CT_REVISION        1
#define  GID_REQUEST_SZ   (offsetof(struct lpfc_sli_ct_request, un) + \
			   sizeof(struct gid))
#define  GIDFF_REQUEST_SZ (offsetof(struct lpfc_sli_ct_request, un) + \
			   sizeof(struct gid_ff))
#define  GFF_REQUEST_SZ   (offsetof(struct lpfc_sli_ct_request, un) + \
			   sizeof(struct gff))
#define  GFT_REQUEST_SZ   (offsetof(struct lpfc_sli_ct_request, un) + \
			   sizeof(struct gft))
#define  RFT_REQUEST_SZ   (offsetof(struct lpfc_sli_ct_request, un) + \
			   sizeof(struct rft))
#define  RFF_REQUEST_SZ   (offsetof(struct lpfc_sli_ct_request, un) + \
			   sizeof(struct rff))
#define  RNN_REQUEST_SZ   (offsetof(struct lpfc_sli_ct_request, un) + \
			   sizeof(struct rnn))
#define  RSNN_REQUEST_SZ  (offsetof(struct lpfc_sli_ct_request, un) + \
			   sizeof(struct rsnn))
#define DA_ID_REQUEST_SZ (offsetof(struct lpfc_sli_ct_request, un) + \
			  sizeof(struct da_id))
#define  RSPN_REQUEST_SZ  (offsetof(struct lpfc_sli_ct_request, un) + \
			   sizeof(struct rspn))

/*
 * FsType Definitions
 */

#define  SLI_CT_MANAGEMENT_SERVICE        0xFA
#define  SLI_CT_TIME_SERVICE              0xFB
#define  SLI_CT_DIRECTORY_SERVICE         0xFC
#define  SLI_CT_FABRIC_CONTROLLER_SERVICE 0xFD

/*
 * Directory Service Subtypes
 */

#define  SLI_CT_DIRECTORY_NAME_SERVER     0x02

/*
 * Response Codes
 */

#define  SLI_CT_RESPONSE_FS_RJT           0x8001
#define  SLI_CT_RESPONSE_FS_ACC           0x8002

/*
 * Reason Codes
 */

#define  SLI_CT_NO_ADDITIONAL_EXPL	  0x0
#define  SLI_CT_INVALID_COMMAND           0x01
#define  SLI_CT_INVALID_VERSION           0x02
#define  SLI_CT_LOGICAL_ERROR             0x03
#define  SLI_CT_INVALID_IU_SIZE           0x04
#define  SLI_CT_LOGICAL_BUSY              0x05
#define  SLI_CT_PROTOCOL_ERROR            0x07
#define  SLI_CT_UNABLE_TO_PERFORM_REQ     0x09
#define  SLI_CT_REQ_NOT_SUPPORTED         0x0b
#define  SLI_CT_HBA_INFO_NOT_REGISTERED	  0x10
#define  SLI_CT_MULTIPLE_HBA_ATTR_OF_SAME_TYPE  0x11
#define  SLI_CT_INVALID_HBA_ATTR_BLOCK_LEN      0x12
#define  SLI_CT_HBA_ATTR_NOT_PRESENT	  0x13
#define  SLI_CT_PORT_INFO_NOT_REGISTERED  0x20
#define  SLI_CT_MULTIPLE_PORT_ATTR_OF_SAME_TYPE 0x21
#define  SLI_CT_INVALID_PORT_ATTR_BLOCK_LEN     0x22
#define  SLI_CT_VENDOR_UNIQUE             0xff

/*
 * Name Server SLI_CT_UNABLE_TO_PERFORM_REQ Explanations
 */

#define  SLI_CT_NO_PORT_ID                0x01
#define  SLI_CT_NO_PORT_NAME              0x02
#define  SLI_CT_NO_NODE_NAME              0x03
#define  SLI_CT_NO_CLASS_OF_SERVICE       0x04
#define  SLI_CT_NO_IP_ADDRESS             0x05
#define  SLI_CT_NO_IPA                    0x06
#define  SLI_CT_NO_FC4_TYPES              0x07
#define  SLI_CT_NO_SYMBOLIC_PORT_NAME     0x08
#define  SLI_CT_NO_SYMBOLIC_NODE_NAME     0x09
#define  SLI_CT_NO_PORT_TYPE              0x0A
#define  SLI_CT_ACCESS_DENIED             0x10
#define  SLI_CT_INVALID_PORT_ID           0x11
#define  SLI_CT_DATABASE_EMPTY            0x12

/*
 * Name Server Command Codes
 */

#define  SLI_CTNS_GA_NXT      0x0100
#define  SLI_CTNS_GPN_ID      0x0112
#define  SLI_CTNS_GNN_ID      0x0113
#define  SLI_CTNS_GCS_ID      0x0114
#define  SLI_CTNS_GFT_ID      0x0117
#define  SLI_CTNS_GSPN_ID     0x0118
#define  SLI_CTNS_GPT_ID      0x011A
#define  SLI_CTNS_GFF_ID      0x011F
#define  SLI_CTNS_GID_PN      0x0121
#define  SLI_CTNS_GID_NN      0x0131
#define  SLI_CTNS_GIP_NN      0x0135
#define  SLI_CTNS_GIPA_NN     0x0136
#define  SLI_CTNS_GSNN_NN     0x0139
#define  SLI_CTNS_GNN_IP      0x0153
#define  SLI_CTNS_GIPA_IP     0x0156
#define  SLI_CTNS_GID_FT      0x0171
#define  SLI_CTNS_GID_FF      0x01F1
#define  SLI_CTNS_GID_PT      0x01A1
#define  SLI_CTNS_RPN_ID      0x0212
#define  SLI_CTNS_RNN_ID      0x0213
#define  SLI_CTNS_RCS_ID      0x0214
#define  SLI_CTNS_RFT_ID      0x0217
#define  SLI_CTNS_RSPN_ID     0x0218
#define  SLI_CTNS_RPT_ID      0x021A
#define  SLI_CTNS_RFF_ID      0x021F
#define  SLI_CTNS_RIP_NN      0x0235
#define  SLI_CTNS_RIPA_NN     0x0236
#define  SLI_CTNS_RSNN_NN     0x0239
#define  SLI_CTNS_DA_ID       0x0300

/*
 * Port Types
 */

#define SLI_CTPT_N_PORT		0x01
#define SLI_CTPT_NL_PORT	0x02
#define SLI_CTPT_FNL_PORT	0x03
#define SLI_CTPT_IP		0x04
#define SLI_CTPT_FCP		0x08
#define SLI_CTPT_NVME		0x28
#define SLI_CTPT_NX_PORT	0x7F
#define SLI_CTPT_F_PORT		0x81
#define SLI_CTPT_FL_PORT	0x82
#define SLI_CTPT_E_PORT		0x84

#define SLI_CT_LAST_ENTRY     0x80000000

/* Fibre Channel Service Parameter definitions */

#define FC_PH_4_0   6		/* FC-PH version 4.0 */
#define FC_PH_4_1   7		/* FC-PH version 4.1 */
#define FC_PH_4_2   8		/* FC-PH version 4.2 */
#define FC_PH_4_3   9		/* FC-PH version 4.3 */

#define FC_PH_LOW   8		/* Lowest supported FC-PH version */
#define FC_PH_HIGH  9		/* Highest supported FC-PH version */
#define FC_PH3   0x20		/* FC-PH-3 version */

#define FF_FRAME_SIZE     2048

struct lpfc_name {
	union {
		struct {
#ifdef __BIG_ENDIAN_BITFIELD
			uint8_t nameType:4;	/* FC Word 0, bit 28:31 */
			uint8_t IEEEextMsn:4;	/* FC Word 0, bit 24:27, bit
						   8:11 of IEEE ext */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
			uint8_t IEEEextMsn:4;	/* FC Word 0, bit 24:27, bit
						   8:11 of IEEE ext */
			uint8_t nameType:4;	/* FC Word 0, bit 28:31 */
#endif

#define NAME_IEEE           0x1	/* IEEE name - nameType */
#define NAME_IEEE_EXT       0x2	/* IEEE extended name */
#define NAME_FC_TYPE        0x3	/* FC native name type */
#define NAME_IP_TYPE        0x4	/* IP address */
#define NAME_CCITT_TYPE     0xC
#define NAME_CCITT_GR_TYPE  0xE
			uint8_t IEEEextLsb;	/* FC Word 0, bit 16:23, IEEE
						   extended Lsb */
			uint8_t IEEE[6];	/* FC IEEE address */
		} s;
		uint8_t wwn[8];
		uint64_t name;
	} u;
};

struct csp {
	uint8_t fcphHigh;	/* FC Word 0, byte 0 */
	uint8_t fcphLow;
	uint8_t bbCreditMsb;
	uint8_t bbCreditLsb;	/* FC Word 0, byte 3 */

/*
 * Word 1 Bit 31 in common service parameter is overloaded.
 * Word 1 Bit 31 in FLOGI request is multiple NPort request
 * Word 1 Bit 31 in FLOGI response is clean address bit
 */
#define clean_address_bit request_multiple_Nport /* Word 1, bit 31 */
/*
 * Word 1 Bit 30 in common service parameter is overloaded.
 * Word 1 Bit 30 in FLOGI request is Virtual Fabrics
 * Word 1 Bit 30 in PLOGI request is random offset
 */
#define virtual_fabric_support randomOffset /* Word 1, bit 30 */
/*
 * Word 1 Bit 29 in common service parameter is overloaded.
 * Word 1 Bit 29 in FLOGI response is multiple NPort assignment
 * Word 1 Bit 29 in FLOGI/PLOGI request is Valid Vendor Version Level
 */
#define valid_vendor_ver_level response_multiple_NPort /* Word 1, bit 29 */
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t request_multiple_Nport:1;	/* FC Word 1, bit 31 */
	uint16_t randomOffset:1;	/* FC Word 1, bit 30 */
	uint16_t response_multiple_NPort:1;	/* FC Word 1, bit 29 */
	uint16_t fPort:1;	/* FC Word 1, bit 28 */
	uint16_t altBbCredit:1;	/* FC Word 1, bit 27 */
	uint16_t edtovResolution:1;	/* FC Word 1, bit 26 */
	uint16_t multicast:1;	/* FC Word 1, bit 25 */
	uint16_t broadcast:1;	/* FC Word 1, bit 24 */

	uint16_t huntgroup:1;	/* FC Word 1, bit 23 */
	uint16_t simplex:1;	/* FC Word 1, bit 22 */
	uint16_t word1Reserved1:3;	/* FC Word 1, bit 21:19 */
	uint16_t dhd:1;		/* FC Word 1, bit 18 */
	uint16_t contIncSeqCnt:1;	/* FC Word 1, bit 17 */
	uint16_t payloadlength:1;	/* FC Word 1, bit 16 */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t broadcast:1;	/* FC Word 1, bit 24 */
	uint16_t multicast:1;	/* FC Word 1, bit 25 */
	uint16_t edtovResolution:1;	/* FC Word 1, bit 26 */
	uint16_t altBbCredit:1;	/* FC Word 1, bit 27 */
	uint16_t fPort:1;	/* FC Word 1, bit 28 */
	uint16_t response_multiple_NPort:1;	/* FC Word 1, bit 29 */
	uint16_t randomOffset:1;	/* FC Word 1, bit 30 */
	uint16_t request_multiple_Nport:1;	/* FC Word 1, bit 31 */

	uint16_t payloadlength:1;	/* FC Word 1, bit 16 */
	uint16_t contIncSeqCnt:1;	/* FC Word 1, bit 17 */
	uint16_t dhd:1;		/* FC Word 1, bit 18 */
	uint16_t word1Reserved1:3;	/* FC Word 1, bit 21:19 */
	uint16_t simplex:1;	/* FC Word 1, bit 22 */
	uint16_t huntgroup:1;	/* FC Word 1, bit 23 */
#endif

	uint8_t bbRcvSizeMsb;	/* Upper nibble is reserved */
	uint8_t bbRcvSizeLsb;	/* FC Word 1, byte 3 */
	union {
		struct {
			uint8_t word2Reserved1;	/* FC Word 2 byte 0 */

			uint8_t totalConcurrSeq;	/* FC Word 2 byte 1 */
			uint8_t roByCategoryMsb;	/* FC Word 2 byte 2 */

			uint8_t roByCategoryLsb;	/* FC Word 2 byte 3 */
		} nPort;
		uint32_t r_a_tov;	/* R_A_TOV must be in B.E. format */
	} w2;

	uint32_t e_d_tov;	/* E_D_TOV must be in B.E. format */
};

struct class_parms {
#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t classValid:1;	/* FC Word 0, bit 31 */
	uint8_t intermix:1;	/* FC Word 0, bit 30 */
	uint8_t stackedXparent:1;	/* FC Word 0, bit 29 */
	uint8_t stackedLockDown:1;	/* FC Word 0, bit 28 */
	uint8_t seqDelivery:1;	/* FC Word 0, bit 27 */
	uint8_t word0Reserved1:3;	/* FC Word 0, bit 24:26 */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint8_t word0Reserved1:3;	/* FC Word 0, bit 24:26 */
	uint8_t seqDelivery:1;	/* FC Word 0, bit 27 */
	uint8_t stackedLockDown:1;	/* FC Word 0, bit 28 */
	uint8_t stackedXparent:1;	/* FC Word 0, bit 29 */
	uint8_t intermix:1;	/* FC Word 0, bit 30 */
	uint8_t classValid:1;	/* FC Word 0, bit 31 */

#endif

	uint8_t word0Reserved2;	/* FC Word 0, bit 16:23 */

#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t iCtlXidReAssgn:2;	/* FC Word 0, Bit 14:15 */
	uint8_t iCtlInitialPa:2;	/* FC Word 0, bit 12:13 */
	uint8_t iCtlAck0capable:1;	/* FC Word 0, bit 11 */
	uint8_t iCtlAckNcapable:1;	/* FC Word 0, bit 10 */
	uint8_t word0Reserved3:2;	/* FC Word 0, bit  8: 9 */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint8_t word0Reserved3:2;	/* FC Word 0, bit  8: 9 */
	uint8_t iCtlAckNcapable:1;	/* FC Word 0, bit 10 */
	uint8_t iCtlAck0capable:1;	/* FC Word 0, bit 11 */
	uint8_t iCtlInitialPa:2;	/* FC Word 0, bit 12:13 */
	uint8_t iCtlXidReAssgn:2;	/* FC Word 0, Bit 14:15 */
#endif

	uint8_t word0Reserved4;	/* FC Word 0, bit  0: 7 */

#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t rCtlAck0capable:1;	/* FC Word 1, bit 31 */
	uint8_t rCtlAckNcapable:1;	/* FC Word 1, bit 30 */
	uint8_t rCtlXidInterlck:1;	/* FC Word 1, bit 29 */
	uint8_t rCtlErrorPolicy:2;	/* FC Word 1, bit 27:28 */
	uint8_t word1Reserved1:1;	/* FC Word 1, bit 26 */
	uint8_t rCtlCatPerSeq:2;	/* FC Word 1, bit 24:25 */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint8_t rCtlCatPerSeq:2;	/* FC Word 1, bit 24:25 */
	uint8_t word1Reserved1:1;	/* FC Word 1, bit 26 */
	uint8_t rCtlErrorPolicy:2;	/* FC Word 1, bit 27:28 */
	uint8_t rCtlXidInterlck:1;	/* FC Word 1, bit 29 */
	uint8_t rCtlAckNcapable:1;	/* FC Word 1, bit 30 */
	uint8_t rCtlAck0capable:1;	/* FC Word 1, bit 31 */
#endif

	uint8_t word1Reserved2;	/* FC Word 1, bit 16:23 */
	uint8_t rcvDataSizeMsb;	/* FC Word 1, bit  8:15 */
	uint8_t rcvDataSizeLsb;	/* FC Word 1, bit  0: 7 */

	uint8_t concurrentSeqMsb;	/* FC Word 2, bit 24:31 */
	uint8_t concurrentSeqLsb;	/* FC Word 2, bit 16:23 */
	uint8_t EeCreditSeqMsb;	/* FC Word 2, bit  8:15 */
	uint8_t EeCreditSeqLsb;	/* FC Word 2, bit  0: 7 */

	uint8_t openSeqPerXchgMsb;	/* FC Word 3, bit 24:31 */
	uint8_t openSeqPerXchgLsb;	/* FC Word 3, bit 16:23 */
	uint8_t word3Reserved1;	/* Fc Word 3, bit  8:15 */
	uint8_t word3Reserved2;	/* Fc Word 3, bit  0: 7 */
};

struct serv_parm {	/* Structure is in Big Endian format */
	struct csp cmn;
	struct lpfc_name portName;
	struct lpfc_name nodeName;
	struct class_parms cls1;
	struct class_parms cls2;
	struct class_parms cls3;
	struct class_parms cls4;
	union {
		uint8_t vendorVersion[16];
		struct {
			uint32_t vid;
#define LPFC_VV_EMLX_ID	0x454d4c58	/* EMLX */
			uint32_t flags;
#define LPFC_VV_SUPPRESS_RSP	1
		} vv;
	} un;
};

/*
 * Virtual Fabric Tagging Header
 */
struct fc_vft_header {
	 uint32_t word0;
#define fc_vft_hdr_r_ctl_SHIFT		24
#define fc_vft_hdr_r_ctl_MASK		0xFF
#define fc_vft_hdr_r_ctl_WORD		word0
#define fc_vft_hdr_ver_SHIFT		22
#define fc_vft_hdr_ver_MASK		0x3
#define fc_vft_hdr_ver_WORD		word0
#define fc_vft_hdr_type_SHIFT		18
#define fc_vft_hdr_type_MASK		0xF
#define fc_vft_hdr_type_WORD		word0
#define fc_vft_hdr_e_SHIFT		16
#define fc_vft_hdr_e_MASK		0x1
#define fc_vft_hdr_e_WORD		word0
#define fc_vft_hdr_priority_SHIFT	13
#define fc_vft_hdr_priority_MASK	0x7
#define fc_vft_hdr_priority_WORD	word0
#define fc_vft_hdr_vf_id_SHIFT		1
#define fc_vft_hdr_vf_id_MASK		0xFFF
#define fc_vft_hdr_vf_id_WORD		word0
	uint32_t word1;
#define fc_vft_hdr_hopct_SHIFT		24
#define fc_vft_hdr_hopct_MASK		0xFF
#define fc_vft_hdr_hopct_WORD		word1
};

/*
 *  Extended Link Service LS_COMMAND codes (Payload Word 0)
 */
#ifdef __BIG_ENDIAN_BITFIELD
#define ELS_CMD_MASK      0xffff0000
#define ELS_RSP_MASK      0xff000000
#define ELS_CMD_LS_RJT    0x01000000
#define ELS_CMD_ACC       0x02000000
#define ELS_CMD_PLOGI     0x03000000
#define ELS_CMD_FLOGI     0x04000000
#define ELS_CMD_LOGO      0x05000000
#define ELS_CMD_ABTX      0x06000000
#define ELS_CMD_RCS       0x07000000
#define ELS_CMD_RES       0x08000000
#define ELS_CMD_RSS       0x09000000
#define ELS_CMD_RSI       0x0A000000
#define ELS_CMD_ESTS      0x0B000000
#define ELS_CMD_ESTC      0x0C000000
#define ELS_CMD_ADVC      0x0D000000
#define ELS_CMD_RTV       0x0E000000
#define ELS_CMD_RLS       0x0F000000
#define ELS_CMD_ECHO      0x10000000
#define ELS_CMD_TEST      0x11000000
#define ELS_CMD_RRQ       0x12000000
#define ELS_CMD_REC       0x13000000
#define ELS_CMD_RDP       0x18000000
#define ELS_CMD_PRLI      0x20100014
#define ELS_CMD_NVMEPRLI  0x20140018
#define ELS_CMD_PRLO      0x21100014
#define ELS_CMD_PRLO_ACC  0x02100014
#define ELS_CMD_PDISC     0x50000000
#define ELS_CMD_FDISC     0x51000000
#define ELS_CMD_ADISC     0x52000000
#define ELS_CMD_FARP      0x54000000
#define ELS_CMD_FARPR     0x55000000
#define ELS_CMD_RPS       0x56000000
#define ELS_CMD_RPL       0x57000000
#define ELS_CMD_FAN       0x60000000
#define ELS_CMD_RSCN      0x61040000
#define ELS_CMD_SCR       0x62000000
#define ELS_CMD_RNID      0x78000000
#define ELS_CMD_LIRR      0x7A000000
#define ELS_CMD_LCB	  0x81000000
#else	/*  __LITTLE_ENDIAN_BITFIELD */
#define ELS_CMD_MASK      0xffff
#define ELS_RSP_MASK      0xff
#define ELS_CMD_LS_RJT    0x01
#define ELS_CMD_ACC       0x02
#define ELS_CMD_PLOGI     0x03
#define ELS_CMD_FLOGI     0x04
#define ELS_CMD_LOGO      0x05
#define ELS_CMD_ABTX      0x06
#define ELS_CMD_RCS       0x07
#define ELS_CMD_RES       0x08
#define ELS_CMD_RSS       0x09
#define ELS_CMD_RSI       0x0A
#define ELS_CMD_ESTS      0x0B
#define ELS_CMD_ESTC      0x0C
#define ELS_CMD_ADVC      0x0D
#define ELS_CMD_RTV       0x0E
#define ELS_CMD_RLS       0x0F
#define ELS_CMD_ECHO      0x10
#define ELS_CMD_TEST      0x11
#define ELS_CMD_RRQ       0x12
#define ELS_CMD_REC       0x13
#define ELS_CMD_RDP	  0x18
#define ELS_CMD_PRLI      0x14001020
#define ELS_CMD_NVMEPRLI  0x18001420
#define ELS_CMD_PRLO      0x14001021
#define ELS_CMD_PRLO_ACC  0x14001002
#define ELS_CMD_PDISC     0x50
#define ELS_CMD_FDISC     0x51
#define ELS_CMD_ADISC     0x52
#define ELS_CMD_FARP      0x54
#define ELS_CMD_FARPR     0x55
#define ELS_CMD_RPS       0x56
#define ELS_CMD_RPL       0x57
#define ELS_CMD_FAN       0x60
#define ELS_CMD_RSCN      0x0461
#define ELS_CMD_SCR       0x62
#define ELS_CMD_RNID      0x78
#define ELS_CMD_LIRR      0x7A
#define ELS_CMD_LCB	  0x81
#endif

/*
 *  LS_RJT Payload Definition
 */

struct ls_rjt {	/* Structure is in Big Endian format */
	union {
		uint32_t lsRjtError;
		struct {
			uint8_t lsRjtRsvd0;	/* FC Word 0, bit 24:31 */

			uint8_t lsRjtRsnCode;	/* FC Word 0, bit 16:23 */
			/* LS_RJT reason codes */
#define LSRJT_INVALID_CMD     0x01
#define LSRJT_LOGICAL_ERR     0x03
#define LSRJT_LOGICAL_BSY     0x05
#define LSRJT_PROTOCOL_ERR    0x07
#define LSRJT_UNABLE_TPC      0x09	/* Unable to perform command */
#define LSRJT_CMD_UNSUPPORTED 0x0B
#define LSRJT_VENDOR_UNIQUE   0xFF	/* See Byte 3 */

			uint8_t lsRjtRsnCodeExp; /* FC Word 0, bit 8:15 */
			/* LS_RJT reason explanation */
#define LSEXP_NOTHING_MORE      0x00
#define LSEXP_SPARM_OPTIONS     0x01
#define LSEXP_SPARM_ICTL        0x03
#define LSEXP_SPARM_RCTL        0x05
#define LSEXP_SPARM_RCV_SIZE    0x07
#define LSEXP_SPARM_CONCUR_SEQ  0x09
#define LSEXP_SPARM_CREDIT      0x0B
#define LSEXP_INVALID_PNAME     0x0D
#define LSEXP_INVALID_NNAME     0x0E
#define LSEXP_INVALID_CSP       0x0F
#define LSEXP_INVALID_ASSOC_HDR 0x11
#define LSEXP_ASSOC_HDR_REQ     0x13
#define LSEXP_INVALID_O_SID     0x15
#define LSEXP_INVALID_OX_RX     0x17
#define LSEXP_CMD_IN_PROGRESS   0x19
#define LSEXP_PORT_LOGIN_REQ    0x1E
#define LSEXP_INVALID_NPORT_ID  0x1F
#define LSEXP_INVALID_SEQ_ID    0x21
#define LSEXP_INVALID_XCHG      0x23
#define LSEXP_INACTIVE_XCHG     0x25
#define LSEXP_RQ_REQUIRED       0x27
#define LSEXP_OUT_OF_RESOURCE   0x29
#define LSEXP_CANT_GIVE_DATA    0x2A
#define LSEXP_REQ_UNSUPPORTED   0x2C
			uint8_t vendorUnique;	/* FC Word 0, bit  0: 7 */
		} b;
	} un;
};

/*
 *  N_Port Login (FLOGO/PLOGO Request) Payload Definition
 */

typedef struct _LOGO {		/* Structure is in Big Endian format */
	union {
		uint32_t nPortId32;	/* Access nPortId as a word */
		struct {
			uint8_t word1Reserved1;	/* FC Word 1, bit 31:24 */
			uint8_t nPortIdByte0;	/* N_port  ID bit 16:23 */
			uint8_t nPortIdByte1;	/* N_port  ID bit  8:15 */
			uint8_t nPortIdByte2;	/* N_port  ID bit  0: 7 */
		} b;
	} un;
	struct lpfc_name portName;	/* N_port name field */
} LOGO;

/*
 *  FCP Login (PRLI Request / ACC) Payload Definition
 */

#define PRLX_PAGE_LEN   0x10
#define TPRLO_PAGE_LEN  0x14

typedef struct _PRLI {		/* Structure is in Big Endian format */
	uint8_t prliType;	/* FC Parm Word 0, bit 24:31 */

#define PRLI_FCP_TYPE 0x08
#define PRLI_NVME_TYPE 0x28
	uint8_t word0Reserved1;	/* FC Parm Word 0, bit 16:23 */

#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t origProcAssocV:1;	/* FC Parm Word 0, bit 15 */
	uint8_t respProcAssocV:1;	/* FC Parm Word 0, bit 14 */
	uint8_t estabImagePair:1;	/* FC Parm Word 0, bit 13 */

	/*    ACC = imagePairEstablished */
	uint8_t word0Reserved2:1;	/* FC Parm Word 0, bit 12 */
	uint8_t acceptRspCode:4;	/* FC Parm Word 0, bit 8:11, ACC ONLY */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint8_t acceptRspCode:4;	/* FC Parm Word 0, bit 8:11, ACC ONLY */
	uint8_t word0Reserved2:1;	/* FC Parm Word 0, bit 12 */
	uint8_t estabImagePair:1;	/* FC Parm Word 0, bit 13 */
	uint8_t respProcAssocV:1;	/* FC Parm Word 0, bit 14 */
	uint8_t origProcAssocV:1;	/* FC Parm Word 0, bit 15 */
	/*    ACC = imagePairEstablished */
#endif

#define PRLI_REQ_EXECUTED     0x1	/* acceptRspCode */
#define PRLI_NO_RESOURCES     0x2
#define PRLI_INIT_INCOMPLETE  0x3
#define PRLI_NO_SUCH_PA       0x4
#define PRLI_PREDEF_CONFIG    0x5
#define PRLI_PARTIAL_SUCCESS  0x6
#define PRLI_INVALID_PAGE_CNT 0x7
	uint8_t word0Reserved3;	/* FC Parm Word 0, bit 0:7 */

	uint32_t origProcAssoc;	/* FC Parm Word 1, bit 0:31 */

	uint32_t respProcAssoc;	/* FC Parm Word 2, bit 0:31 */

	uint8_t word3Reserved1;	/* FC Parm Word 3, bit 24:31 */
	uint8_t word3Reserved2;	/* FC Parm Word 3, bit 16:23 */

#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t Word3bit15Resved:1;	/* FC Parm Word 3, bit 15 */
	uint16_t Word3bit14Resved:1;	/* FC Parm Word 3, bit 14 */
	uint16_t Word3bit13Resved:1;	/* FC Parm Word 3, bit 13 */
	uint16_t Word3bit12Resved:1;	/* FC Parm Word 3, bit 12 */
	uint16_t Word3bit11Resved:1;	/* FC Parm Word 3, bit 11 */
	uint16_t Word3bit10Resved:1;	/* FC Parm Word 3, bit 10 */
	uint16_t TaskRetryIdReq:1;	/* FC Parm Word 3, bit  9 */
	uint16_t Retry:1;	/* FC Parm Word 3, bit  8 */
	uint16_t ConfmComplAllowed:1;	/* FC Parm Word 3, bit  7 */
	uint16_t dataOverLay:1;	/* FC Parm Word 3, bit  6 */
	uint16_t initiatorFunc:1;	/* FC Parm Word 3, bit  5 */
	uint16_t targetFunc:1;	/* FC Parm Word 3, bit  4 */
	uint16_t cmdDataMixEna:1;	/* FC Parm Word 3, bit  3 */
	uint16_t dataRspMixEna:1;	/* FC Parm Word 3, bit  2 */
	uint16_t readXferRdyDis:1;	/* FC Parm Word 3, bit  1 */
	uint16_t writeXferRdyDis:1;	/* FC Parm Word 3, bit  0 */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t Retry:1;	/* FC Parm Word 3, bit  8 */
	uint16_t TaskRetryIdReq:1;	/* FC Parm Word 3, bit  9 */
	uint16_t Word3bit10Resved:1;	/* FC Parm Word 3, bit 10 */
	uint16_t Word3bit11Resved:1;	/* FC Parm Word 3, bit 11 */
	uint16_t Word3bit12Resved:1;	/* FC Parm Word 3, bit 12 */
	uint16_t Word3bit13Resved:1;	/* FC Parm Word 3, bit 13 */
	uint16_t Word3bit14Resved:1;	/* FC Parm Word 3, bit 14 */
	uint16_t Word3bit15Resved:1;	/* FC Parm Word 3, bit 15 */
	uint16_t writeXferRdyDis:1;	/* FC Parm Word 3, bit  0 */
	uint16_t readXferRdyDis:1;	/* FC Parm Word 3, bit  1 */
	uint16_t dataRspMixEna:1;	/* FC Parm Word 3, bit  2 */
	uint16_t cmdDataMixEna:1;	/* FC Parm Word 3, bit  3 */
	uint16_t targetFunc:1;	/* FC Parm Word 3, bit  4 */
	uint16_t initiatorFunc:1;	/* FC Parm Word 3, bit  5 */
	uint16_t dataOverLay:1;	/* FC Parm Word 3, bit  6 */
	uint16_t ConfmComplAllowed:1;	/* FC Parm Word 3, bit  7 */
#endif
} PRLI;

/*
 *  FCP Logout (PRLO Request / ACC) Payload Definition
 */

typedef struct _PRLO {		/* Structure is in Big Endian format */
	uint8_t prloType;	/* FC Parm Word 0, bit 24:31 */

#define PRLO_FCP_TYPE  0x08
	uint8_t word0Reserved1;	/* FC Parm Word 0, bit 16:23 */

#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t origProcAssocV:1;	/* FC Parm Word 0, bit 15 */
	uint8_t respProcAssocV:1;	/* FC Parm Word 0, bit 14 */
	uint8_t word0Reserved2:2;	/* FC Parm Word 0, bit 12:13 */
	uint8_t acceptRspCode:4;	/* FC Parm Word 0, bit 8:11, ACC ONLY */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint8_t acceptRspCode:4;	/* FC Parm Word 0, bit 8:11, ACC ONLY */
	uint8_t word0Reserved2:2;	/* FC Parm Word 0, bit 12:13 */
	uint8_t respProcAssocV:1;	/* FC Parm Word 0, bit 14 */
	uint8_t origProcAssocV:1;	/* FC Parm Word 0, bit 15 */
#endif

#define PRLO_REQ_EXECUTED     0x1	/* acceptRspCode */
#define PRLO_NO_SUCH_IMAGE    0x4
#define PRLO_INVALID_PAGE_CNT 0x7

	uint8_t word0Reserved3;	/* FC Parm Word 0, bit 0:7 */

	uint32_t origProcAssoc;	/* FC Parm Word 1, bit 0:31 */

	uint32_t respProcAssoc;	/* FC Parm Word 2, bit 0:31 */

	uint32_t word3Reserved1;	/* FC Parm Word 3, bit 0:31 */
} PRLO;

typedef struct _ADISC {		/* Structure is in Big Endian format */
	uint32_t hardAL_PA;
	struct lpfc_name portName;
	struct lpfc_name nodeName;
	uint32_t DID;
} ADISC;

typedef struct _FARP {		/* Structure is in Big Endian format */
	uint32_t Mflags:8;
	uint32_t Odid:24;
#define FARP_NO_ACTION          0	/* FARP information enclosed, no
					   action */
#define FARP_MATCH_PORT         0x1	/* Match on Responder Port Name */
#define FARP_MATCH_NODE         0x2	/* Match on Responder Node Name */
#define FARP_MATCH_IP           0x4	/* Match on IP address, not supported */
#define FARP_MATCH_IPV4         0x5	/* Match on IPV4 address, not
					   supported */
#define FARP_MATCH_IPV6         0x6	/* Match on IPV6 address, not
					   supported */
	uint32_t Rflags:8;
	uint32_t Rdid:24;
#define FARP_REQUEST_PLOGI      0x1	/* Request for PLOGI */
#define FARP_REQUEST_FARPR      0x2	/* Request for FARP Response */
	struct lpfc_name OportName;
	struct lpfc_name OnodeName;
	struct lpfc_name RportName;
	struct lpfc_name RnodeName;
	uint8_t Oipaddr[16];
	uint8_t Ripaddr[16];
} FARP;

typedef struct _FAN {		/* Structure is in Big Endian format */
	uint32_t Fdid;
	struct lpfc_name FportName;
	struct lpfc_name FnodeName;
} FAN;

typedef struct _SCR {		/* Structure is in Big Endian format */
	uint8_t resvd1;
	uint8_t resvd2;
	uint8_t resvd3;
	uint8_t Function;
#define  SCR_FUNC_FABRIC     0x01
#define  SCR_FUNC_NPORT      0x02
#define  SCR_FUNC_FULL       0x03
#define  SCR_CLEAR           0xff
} SCR;

typedef struct _RNID_TOP_DISC {
	struct lpfc_name portName;
	uint8_t resvd[8];
	uint32_t unitType;
#define RNID_HBA            0x7
#define RNID_HOST           0xa
#define RNID_DRIVER         0xd
	uint32_t physPort;
	uint32_t attachedNodes;
	uint16_t ipVersion;
#define RNID_IPV4           0x1
#define RNID_IPV6           0x2
	uint16_t UDPport;
	uint8_t ipAddr[16];
	uint16_t resvd1;
	uint16_t flags;
#define RNID_TD_SUPPORT     0x1
#define RNID_LP_VALID       0x2
} RNID_TOP_DISC;

typedef struct _RNID {		/* Structure is in Big Endian format */
	uint8_t Format;
#define RNID_TOPOLOGY_DISC  0xdf
	uint8_t CommonLen;
	uint8_t resvd1;
	uint8_t SpecificLen;
	struct lpfc_name portName;
	struct lpfc_name nodeName;
	union {
		RNID_TOP_DISC topologyDisc;	/* topology disc (0xdf) */
	} un;
} RNID;

typedef struct  _RPS {		/* Structure is in Big Endian format */
	union {
		uint32_t portNum;
		struct lpfc_name portName;
	} un;
} RPS;

typedef struct  _RPS_RSP {	/* Structure is in Big Endian format */
	uint16_t rsvd1;
	uint16_t portStatus;
	uint32_t linkFailureCnt;
	uint32_t lossSyncCnt;
	uint32_t lossSignalCnt;
	uint32_t primSeqErrCnt;
	uint32_t invalidXmitWord;
	uint32_t crcCnt;
} RPS_RSP;

struct RLS {			/* Structure is in Big Endian format */
	uint32_t rls;
#define rls_rsvd_SHIFT		24
#define rls_rsvd_MASK		0x000000ff
#define rls_rsvd_WORD		rls
#define rls_did_SHIFT		0
#define rls_did_MASK		0x00ffffff
#define rls_did_WORD		rls
};

struct  RLS_RSP {		/* Structure is in Big Endian format */
	uint32_t linkFailureCnt;
	uint32_t lossSyncCnt;
	uint32_t lossSignalCnt;
	uint32_t primSeqErrCnt;
	uint32_t invalidXmitWord;
	uint32_t crcCnt;
};

struct RRQ {			/* Structure is in Big Endian format */
	uint32_t rrq;
#define rrq_rsvd_SHIFT		24
#define rrq_rsvd_MASK		0x000000ff
#define rrq_rsvd_WORD		rrq
#define rrq_did_SHIFT		0
#define rrq_did_MASK		0x00ffffff
#define rrq_did_WORD		rrq
	uint32_t rrq_exchg;
#define rrq_oxid_SHIFT		16
#define rrq_oxid_MASK		0xffff
#define rrq_oxid_WORD		rrq_exchg
#define rrq_rxid_SHIFT		0
#define rrq_rxid_MASK		0xffff
#define rrq_rxid_WORD		rrq_exchg
};

#define LPFC_MAX_VFN_PER_PFN	255 /* Maximum VFs allowed per ARI */
#define LPFC_DEF_VFN_PER_PFN	0   /* Default VFs due to platform limitation*/

struct RTV_RSP {		/* Structure is in Big Endian format */
	uint32_t ratov;
	uint32_t edtov;
	uint32_t qtov;
#define qtov_rsvd0_SHIFT	28
#define qtov_rsvd0_MASK		0x0000000f
#define qtov_rsvd0_WORD		qtov		/* reserved */
#define qtov_edtovres_SHIFT	27
#define qtov_edtovres_MASK	0x00000001
#define qtov_edtovres_WORD	qtov		/* E_D_TOV Resolution */
#define qtov__rsvd1_SHIFT	19
#define qtov_rsvd1_MASK		0x0000003f
#define qtov_rsvd1_WORD		qtov		/* reserved */
#define qtov_rttov_SHIFT	18
#define qtov_rttov_MASK		0x00000001
#define qtov_rttov_WORD		qtov		/* R_T_TOV value */
#define qtov_rsvd2_SHIFT	0
#define qtov_rsvd2_MASK		0x0003ffff
#define qtov_rsvd2_WORD		qtov		/* reserved */
};


typedef struct  _RPL {		/* Structure is in Big Endian format */
	uint32_t maxsize;
	uint32_t index;
} RPL;

typedef struct  _PORT_NUM_BLK {
	uint32_t portNum;
	uint32_t portID;
	struct lpfc_name portName;
} PORT_NUM_BLK;

typedef struct  _RPL_RSP {	/* Structure is in Big Endian format */
	uint32_t listLen;
	uint32_t index;
	PORT_NUM_BLK port_num_blk;
} RPL_RSP;

/* This is used for RSCN command */
typedef struct _D_ID {		/* Structure is in Big Endian format */
	union {
		uint32_t word;
		struct {
#ifdef __BIG_ENDIAN_BITFIELD
			uint8_t resv;
			uint8_t domain;
			uint8_t area;
			uint8_t id;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
			uint8_t id;
			uint8_t area;
			uint8_t domain;
			uint8_t resv;
#endif
		} b;
	} un;
} D_ID;

#define RSCN_ADDRESS_FORMAT_PORT	0x0
#define RSCN_ADDRESS_FORMAT_AREA	0x1
#define RSCN_ADDRESS_FORMAT_DOMAIN	0x2
#define RSCN_ADDRESS_FORMAT_FABRIC	0x3
#define RSCN_ADDRESS_FORMAT_MASK	0x3

/*
 *  Structure to define all ELS Payload types
 */

typedef struct _ELS_PKT {	/* Structure is in Big Endian format */
	uint8_t elsCode;	/* FC Word 0, bit 24:31 */
	uint8_t elsByte1;
	uint8_t elsByte2;
	uint8_t elsByte3;
	union {
		struct ls_rjt lsRjt;	/* Payload for LS_RJT ELS response */
		struct serv_parm logi;	/* Payload for PLOGI/FLOGI/PDISC/ACC */
		LOGO logo;	/* Payload for PLOGO/FLOGO/ACC */
		PRLI prli;	/* Payload for PRLI/ACC */
		PRLO prlo;	/* Payload for PRLO/ACC */
		ADISC adisc;	/* Payload for ADISC/ACC */
		FARP farp;	/* Payload for FARP/ACC */
		FAN fan;	/* Payload for FAN */
		SCR scr;	/* Payload for SCR/ACC */
		RNID rnid;	/* Payload for RNID */
		uint8_t pad[128 - 4];	/* Pad out to payload of 128 bytes */
	} un;
} ELS_PKT;

/*
 * Link Cable Beacon (LCB) ELS Frame
 */

struct fc_lcb_request_frame {
	uint32_t      lcb_command;      /* ELS command opcode (0x81)     */
	uint8_t       lcb_sub_command;/* LCB Payload Word 1, bit 24:31 */
#define LPFC_LCB_ON    0x1
#define LPFC_LCB_OFF   0x2
	uint8_t       reserved[3];

	uint8_t       lcb_type; /* LCB Payload Word 2, bit 24:31 */
#define LPFC_LCB_GREEN 0x1
#define LPFC_LCB_AMBER 0x2
	uint8_t       lcb_frequency;    /* LCB Payload Word 2, bit 16:23 */
	uint16_t      lcb_duration;     /* LCB Payload Word 2, bit 15:0  */
};

/*
 * Link Cable Beacon (LCB) ELS Response Frame
 */
struct fc_lcb_res_frame {
	uint32_t      lcb_ls_acc;       /* Acceptance of LCB request (0x02) */
	uint8_t       lcb_sub_command;/* LCB Payload Word 1, bit 24:31 */
	uint8_t       reserved[3];
	uint8_t       lcb_type; /* LCB Payload Word 2, bit 24:31 */
	uint8_t       lcb_frequency;    /* LCB Payload Word 2, bit 16:23 */
	uint16_t      lcb_duration;     /* LCB Payload Word 2, bit 15:0  */
};

/*
 * Read Diagnostic Parameters (RDP) ELS frame.
 */
#define SFF_PG0_IDENT_SFP              0x3

#define SFP_FLAG_PT_OPTICAL            0x0
#define SFP_FLAG_PT_SWLASER            0x01
#define SFP_FLAG_PT_LWLASER_LC1310     0x02
#define SFP_FLAG_PT_LWLASER_LL1550     0x03
#define SFP_FLAG_PT_MASK               0x0F
#define SFP_FLAG_PT_SHIFT              0

#define SFP_FLAG_IS_OPTICAL_PORT       0x01
#define SFP_FLAG_IS_OPTICAL_MASK       0x010
#define SFP_FLAG_IS_OPTICAL_SHIFT      4

#define SFP_FLAG_IS_DESC_VALID         0x01
#define SFP_FLAG_IS_DESC_VALID_MASK    0x020
#define SFP_FLAG_IS_DESC_VALID_SHIFT   5

#define SFP_FLAG_CT_UNKNOWN            0x0
#define SFP_FLAG_CT_SFP_PLUS           0x01
#define SFP_FLAG_CT_MASK               0x3C
#define SFP_FLAG_CT_SHIFT              6

struct fc_rdp_port_name_info {
	uint8_t wwnn[8];
	uint8_t wwpn[8];
};


/*
 * Link Error Status Block Structure (FC-FS-3) for RDP
 * This similar to RPS ELS
 */
struct fc_link_status {
	uint32_t      link_failure_cnt;
	uint32_t      loss_of_synch_cnt;
	uint32_t      loss_of_signal_cnt;
	uint32_t      primitive_seq_proto_err;
	uint32_t      invalid_trans_word;
	uint32_t      invalid_crc_cnt;

};

#define RDP_PORT_NAMES_DESC_TAG  0x00010003
struct fc_rdp_port_name_desc {
	uint32_t	tag;     /* 0001 0003h */
	uint32_t	length;  /* set to size of payload struct */
	struct fc_rdp_port_name_info  port_names;
};


struct fc_rdp_fec_info {
	uint32_t CorrectedBlocks;
	uint32_t UncorrectableBlocks;
};

#define RDP_FEC_DESC_TAG  0x00010005
struct fc_fec_rdp_desc {
	uint32_t tag;
	uint32_t length;
	struct fc_rdp_fec_info info;
};

struct fc_rdp_link_error_status_payload_info {
	struct fc_link_status link_status; /* 24 bytes */
	uint32_t  port_type;             /* bits 31-30 only */
};

#define RDP_LINK_ERROR_STATUS_DESC_TAG  0x00010002
struct fc_rdp_link_error_status_desc {
	uint32_t         tag;     /* 0001 0002h */
	uint32_t         length;  /* set to size of payload struct */
	struct fc_rdp_link_error_status_payload_info info;
};

#define VN_PT_PHY_UNKNOWN      0x00
#define VN_PT_PHY_PF_PORT      0x01
#define VN_PT_PHY_ETH_MAC      0x10
#define VN_PT_PHY_SHIFT                30

#define RDP_PS_1GB             0x8000
#define RDP_PS_2GB             0x4000
#define RDP_PS_4GB             0x2000
#define RDP_PS_10GB            0x1000
#define RDP_PS_8GB             0x0800
#define RDP_PS_16GB            0x0400
#define RDP_PS_32GB            0x0200

#define RDP_CAP_USER_CONFIGURED 0x0002
#define RDP_CAP_UNKNOWN         0x0001
#define RDP_PS_UNKNOWN          0x0002
#define RDP_PS_NOT_ESTABLISHED  0x0001

struct fc_rdp_port_speed {
	uint16_t   capabilities;
	uint16_t   speed;
};

struct fc_rdp_port_speed_info {
	struct fc_rdp_port_speed   port_speed;
};

#define RDP_PORT_SPEED_DESC_TAG  0x00010001
struct fc_rdp_port_speed_desc {
	uint32_t         tag;            /* 00010001h */
	uint32_t         length;         /* set to size of payload struct */
	struct fc_rdp_port_speed_info info;
};

#define RDP_NPORT_ID_SIZE      4
#define RDP_N_PORT_DESC_TAG    0x00000003
struct fc_rdp_nport_desc {
	uint32_t         tag;          /* 0000 0003h, big endian */
	uint32_t         length;       /* size of RDP_N_PORT_ID struct */
	uint32_t         nport_id : 12;
	uint32_t         reserved : 8;
};


struct fc_rdp_link_service_info {
	uint32_t         els_req;    /* Request payload word 0 value.*/
};

#define RDP_LINK_SERVICE_DESC_TAG  0x00000001
struct fc_rdp_link_service_desc {
	uint32_t         tag;     /* Descriptor tag  1 */
	uint32_t         length;  /* set to size of payload struct. */
	struct fc_rdp_link_service_info  payload;
				  /* must be ELS req Word 0(0x18) */
};

struct fc_rdp_sfp_info {
	uint16_t	temperature;
	uint16_t	vcc;
	uint16_t	tx_bias;
	uint16_t	tx_power;
	uint16_t	rx_power;
	uint16_t	flags;
};

#define RDP_SFP_DESC_TAG  0x00010000
struct fc_rdp_sfp_desc {
	uint32_t         tag;
	uint32_t         length;  /* set to size of sfp_info struct */
	struct fc_rdp_sfp_info sfp_info;
};

/* Buffer Credit Descriptor */
struct fc_rdp_bbc_info {
	uint32_t              port_bbc; /* FC_Port buffer-to-buffer credit */
	uint32_t              attached_port_bbc;
	uint32_t              rtt;      /* Round trip time */
};
#define RDP_BBC_DESC_TAG  0x00010006
struct fc_rdp_bbc_desc {
	uint32_t              tag;
	uint32_t              length;
	struct fc_rdp_bbc_info  bbc_info;
};

/* Optical Element Type Transgression Flags */
#define RDP_OET_LOW_WARNING  0x1
#define RDP_OET_HIGH_WARNING 0x2
#define RDP_OET_LOW_ALARM    0x4
#define RDP_OET_HIGH_ALARM   0x8

#define RDP_OED_TEMPERATURE  0x1
#define RDP_OED_VOLTAGE      0x2
#define RDP_OED_TXBIAS       0x3
#define RDP_OED_TXPOWER      0x4
#define RDP_OED_RXPOWER      0x5

#define RDP_OED_TYPE_SHIFT   28
/* Optical Element Data descriptor */
struct fc_rdp_oed_info {
	uint16_t            hi_alarm;
	uint16_t            lo_alarm;
	uint16_t            hi_warning;
	uint16_t            lo_warning;
	uint32_t            function_flags;
};
#define RDP_OED_DESC_TAG  0x00010007
struct fc_rdp_oed_sfp_desc {
	uint32_t             tag;
	uint32_t             length;
	struct fc_rdp_oed_info oed_info;
};

/* Optical Product Data descriptor */
struct fc_rdp_opd_sfp_info {
	uint8_t            vendor_name[16];
	uint8_t            model_number[16];
	uint8_t            serial_number[16];
	uint8_t            revision[4];
	uint8_t            date[8];
};

#define RDP_OPD_DESC_TAG  0x00010008
struct fc_rdp_opd_sfp_desc {
	uint32_t             tag;
	uint32_t             length;
	struct fc_rdp_opd_sfp_info opd_info;
};

struct fc_rdp_req_frame {
	uint32_t         rdp_command;           /* ELS command opcode (0x18)*/
	uint32_t         rdp_des_length;        /* RDP Payload Word 1 */
	struct fc_rdp_nport_desc nport_id_desc; /* RDP Payload Word 2 - 4 */
};


struct fc_rdp_res_frame {
	uint32_t    reply_sequence;		/* FC word0 LS_ACC or LS_RJT */
	uint32_t   length;			/* FC Word 1      */
	struct fc_rdp_link_service_desc link_service_desc;    /* Word 2 -4   */
	struct fc_rdp_sfp_desc sfp_desc;                      /* Word 5 -9   */
	struct fc_rdp_port_speed_desc portspeed_desc;         /* Word 10 -12 */
	struct fc_rdp_link_error_status_desc link_error_desc; /* Word 13 -21 */
	struct fc_rdp_port_name_desc diag_port_names_desc;    /* Word 22 -27 */
	struct fc_rdp_port_name_desc attached_port_names_desc;/* Word 28 -33 */
	struct fc_fec_rdp_desc fec_desc;                      /* FC word 34-37*/
	struct fc_rdp_bbc_desc bbc_desc;                      /* FC Word 38-42*/
	struct fc_rdp_oed_sfp_desc oed_temp_desc;             /* FC Word 43-47*/
	struct fc_rdp_oed_sfp_desc oed_voltage_desc;          /* FC word 48-52*/
	struct fc_rdp_oed_sfp_desc oed_txbias_desc;           /* FC word 53-57*/
	struct fc_rdp_oed_sfp_desc oed_txpower_desc;          /* FC word 58-62*/
	struct fc_rdp_oed_sfp_desc oed_rxpower_desc;          /* FC word 63-67*/
	struct fc_rdp_opd_sfp_desc opd_desc;                  /* FC word 68-84*/
};


/******** FDMI ********/

/* lpfc_sli_ct_request defines the CT_IU preamble for FDMI commands */
#define  SLI_CT_FDMI_Subtypes     0x10	/* Management Service Subtype */

/*
 * Registered Port List Format
 */
struct lpfc_fdmi_reg_port_list {
	uint32_t EntryCnt;
	uint32_t pe;		/* Variable-length array */
};


/* Definitions for HBA / Port attribute entries */

struct lpfc_fdmi_attr_def { /* Defined in TLV format */
	/* Structure is in Big Endian format */
	uint32_t AttrType:16;
	uint32_t AttrLen:16;
	uint32_t AttrValue;  /* Marks start of Value (ATTRIBUTE_ENTRY) */
};


/* Attribute Entry */
struct lpfc_fdmi_attr_entry {
	union {
		uint32_t AttrInt;
		uint8_t  AttrTypes[32];
		uint8_t  AttrString[256];
		struct lpfc_name AttrWWN;
	} un;
};

#define LPFC_FDMI_MAX_AE_SIZE	sizeof(struct lpfc_fdmi_attr_entry)

/*
 * HBA Attribute Block
 */
struct lpfc_fdmi_attr_block {
	uint32_t EntryCnt;		/* Number of HBA attribute entries */
	struct lpfc_fdmi_attr_entry Entry;	/* Variable-length array */
};

/*
 * Port Entry
 */
struct lpfc_fdmi_port_entry {
	struct lpfc_name PortName;
};

/*
 * HBA Identifier
 */
struct lpfc_fdmi_hba_ident {
	struct lpfc_name PortName;
};

/*
 * Register HBA(RHBA)
 */
struct lpfc_fdmi_reg_hba {
	struct lpfc_fdmi_hba_ident hi;
	struct lpfc_fdmi_reg_port_list rpl;	/* variable-length array */
/* struct lpfc_fdmi_attr_block   ab; */
};

/*
 * Register HBA Attributes (RHAT)
 */
struct lpfc_fdmi_reg_hbaattr {
	struct lpfc_name HBA_PortName;
	struct lpfc_fdmi_attr_block ab;
};

/*
 * Register Port Attributes (RPA)
 */
struct lpfc_fdmi_reg_portattr {
	struct lpfc_name PortName;
	struct lpfc_fdmi_attr_block ab;
};

/*
 * HBA MAnagement Operations Command Codes
 */
#define  SLI_MGMT_GRHL     0x100	/* Get registered HBA list */
#define  SLI_MGMT_GHAT     0x101	/* Get HBA attributes */
#define  SLI_MGMT_GRPL     0x102	/* Get registered Port list */
#define  SLI_MGMT_GPAT     0x110	/* Get Port attributes */
#define  SLI_MGMT_GPAS     0x120	/* Get Port Statistics */
#define  SLI_MGMT_RHBA     0x200	/* Register HBA */
#define  SLI_MGMT_RHAT     0x201	/* Register HBA attributes */
#define  SLI_MGMT_RPRT     0x210	/* Register Port */
#define  SLI_MGMT_RPA      0x211	/* Register Port attributes */
#define  SLI_MGMT_DHBA     0x300	/* De-register HBA */
#define  SLI_MGMT_DHAT     0x301	/* De-register HBA attributes */
#define  SLI_MGMT_DPRT     0x310	/* De-register Port */
#define  SLI_MGMT_DPA      0x311	/* De-register Port attributes */

#define LPFC_FDMI_MAX_RETRY     3  /* Max retries for a FDMI command */

/*
 * HBA Attribute Types
 */
#define  RHBA_NODENAME           0x1 /* 8 byte WWNN */
#define  RHBA_MANUFACTURER       0x2 /* 4 to 64 byte ASCII string */
#define  RHBA_SERIAL_NUMBER      0x3 /* 4 to 64 byte ASCII string */
#define  RHBA_MODEL              0x4 /* 4 to 256 byte ASCII string */
#define  RHBA_MODEL_DESCRIPTION  0x5 /* 4 to 256 byte ASCII string */
#define  RHBA_HARDWARE_VERSION   0x6 /* 4 to 256 byte ASCII string */
#define  RHBA_DRIVER_VERSION     0x7 /* 4 to 256 byte ASCII string */
#define  RHBA_OPTION_ROM_VERSION 0x8 /* 4 to 256 byte ASCII string */
#define  RHBA_FIRMWARE_VERSION   0x9 /* 4 to 256 byte ASCII string */
#define  RHBA_OS_NAME_VERSION	 0xa /* 4 to 256 byte ASCII string */
#define  RHBA_MAX_CT_PAYLOAD_LEN 0xb /* 32-bit unsigned int */
#define  RHBA_SYM_NODENAME       0xc /* 4 to 256 byte ASCII string */
#define  RHBA_VENDOR_INFO        0xd  /* 32-bit unsigned int */
#define  RHBA_NUM_PORTS          0xe  /* 32-bit unsigned int */
#define  RHBA_FABRIC_WWNN        0xf  /* 8 byte WWNN */
#define  RHBA_BIOS_VERSION       0x10 /* 4 to 256 byte ASCII string */
#define  RHBA_BIOS_STATE         0x11 /* 32-bit unsigned int */
#define  RHBA_VENDOR_ID          0xe0 /* 8 byte ASCII string */

/* Bit mask for all individual HBA attributes */
#define LPFC_FDMI_HBA_ATTR_wwnn			0x00000001
#define LPFC_FDMI_HBA_ATTR_manufacturer		0x00000002
#define LPFC_FDMI_HBA_ATTR_sn			0x00000004
#define LPFC_FDMI_HBA_ATTR_model		0x00000008
#define LPFC_FDMI_HBA_ATTR_description		0x00000010
#define LPFC_FDMI_HBA_ATTR_hdw_ver		0x00000020
#define LPFC_FDMI_HBA_ATTR_drvr_ver		0x00000040
#define LPFC_FDMI_HBA_ATTR_rom_ver		0x00000080
#define LPFC_FDMI_HBA_ATTR_fmw_ver		0x00000100
#define LPFC_FDMI_HBA_ATTR_os_ver		0x00000200
#define LPFC_FDMI_HBA_ATTR_ct_len		0x00000400
#define LPFC_FDMI_HBA_ATTR_symbolic_name	0x00000800
#define LPFC_FDMI_HBA_ATTR_vendor_info		0x00001000 /* Not used */
#define LPFC_FDMI_HBA_ATTR_num_ports		0x00002000
#define LPFC_FDMI_HBA_ATTR_fabric_wwnn		0x00004000
#define LPFC_FDMI_HBA_ATTR_bios_ver		0x00008000
#define LPFC_FDMI_HBA_ATTR_bios_state		0x00010000 /* Not used */
#define LPFC_FDMI_HBA_ATTR_vendor_id		0x00020000

/* Bit mask for FDMI-1 defined HBA attributes */
#define LPFC_FDMI1_HBA_ATTR			0x000007ff

/* Bit mask for FDMI-2 defined HBA attributes */
/* Skip vendor_info and bios_state */
#define LPFC_FDMI2_HBA_ATTR			0x0002efff

/*
 * Port Attrubute Types
 */
#define  RPRT_SUPPORTED_FC4_TYPES     0x1 /* 32 byte binary array */
#define  RPRT_SUPPORTED_SPEED         0x2 /* 32-bit unsigned int */
#define  RPRT_PORT_SPEED              0x3 /* 32-bit unsigned int */
#define  RPRT_MAX_FRAME_SIZE          0x4 /* 32-bit unsigned int */
#define  RPRT_OS_DEVICE_NAME          0x5 /* 4 to 256 byte ASCII string */
#define  RPRT_HOST_NAME               0x6 /* 4 to 256 byte ASCII string */
#define  RPRT_NODENAME                0x7 /* 8 byte WWNN */
#define  RPRT_PORTNAME                0x8 /* 8 byte WWPN */
#define  RPRT_SYM_PORTNAME            0x9 /* 4 to 256 byte ASCII string */
#define  RPRT_PORT_TYPE               0xa /* 32-bit unsigned int */
#define  RPRT_SUPPORTED_CLASS         0xb /* 32-bit unsigned int */
#define  RPRT_FABRICNAME              0xc /* 8 byte Fabric WWPN */
#define  RPRT_ACTIVE_FC4_TYPES        0xd /* 32 byte binary array */
#define  RPRT_PORT_STATE              0x101 /* 32-bit unsigned int */
#define  RPRT_DISC_PORT               0x102 /* 32-bit unsigned int */
#define  RPRT_PORT_ID                 0x103 /* 32-bit unsigned int */
#define  RPRT_SMART_SERVICE           0xf100 /* 4 to 256 byte ASCII string */
#define  RPRT_SMART_GUID              0xf101 /* 8 byte WWNN + 8 byte WWPN */
#define  RPRT_SMART_VERSION           0xf102 /* 4 to 256 byte ASCII string */
#define  RPRT_SMART_MODEL             0xf103 /* 4 to 256 byte ASCII string */
#define  RPRT_SMART_PORT_INFO         0xf104 /* 32-bit unsigned int */
#define  RPRT_SMART_QOS               0xf105 /* 32-bit unsigned int */
#define  RPRT_SMART_SECURITY          0xf106 /* 32-bit unsigned int */

/* Bit mask for all individual PORT attributes */
#define LPFC_FDMI_PORT_ATTR_fc4type		0x00000001
#define LPFC_FDMI_PORT_ATTR_support_speed	0x00000002
#define LPFC_FDMI_PORT_ATTR_speed		0x00000004
#define LPFC_FDMI_PORT_ATTR_max_frame		0x00000008
#define LPFC_FDMI_PORT_ATTR_os_devname		0x00000010
#define LPFC_FDMI_PORT_ATTR_host_name		0x00000020
#define LPFC_FDMI_PORT_ATTR_wwnn		0x00000040
#define LPFC_FDMI_PORT_ATTR_wwpn		0x00000080
#define LPFC_FDMI_PORT_ATTR_symbolic_name	0x00000100
#define LPFC_FDMI_PORT_ATTR_port_type		0x00000200
#define LPFC_FDMI_PORT_ATTR_class		0x00000400
#define LPFC_FDMI_PORT_ATTR_fabric_wwpn		0x00000800
#define LPFC_FDMI_PORT_ATTR_port_state		0x00001000
#define LPFC_FDMI_PORT_ATTR_active_fc4type	0x00002000
#define LPFC_FDMI_PORT_ATTR_num_disc		0x00004000
#define LPFC_FDMI_PORT_ATTR_nportid		0x00008000
#define LPFC_FDMI_SMART_ATTR_service		0x00010000 /* Vendor specific */
#define LPFC_FDMI_SMART_ATTR_guid		0x00020000 /* Vendor specific */
#define LPFC_FDMI_SMART_ATTR_version		0x00040000 /* Vendor specific */
#define LPFC_FDMI_SMART_ATTR_model		0x00080000 /* Vendor specific */
#define LPFC_FDMI_SMART_ATTR_port_info		0x00100000 /* Vendor specific */
#define LPFC_FDMI_SMART_ATTR_qos		0x00200000 /* Vendor specific */
#define LPFC_FDMI_SMART_ATTR_security		0x00400000 /* Vendor specific */

/* Bit mask for FDMI-1 defined PORT attributes */
#define LPFC_FDMI1_PORT_ATTR			0x0000003f

/* Bit mask for FDMI-2 defined PORT attributes */
#define LPFC_FDMI2_PORT_ATTR			0x0000ffff

/* Bit mask for Smart SAN defined PORT attributes */
#define LPFC_FDMI2_SMART_ATTR			0x007fffff

/* Defines for PORT port state attribute */
#define LPFC_FDMI_PORTSTATE_UNKNOWN	1
#define LPFC_FDMI_PORTSTATE_ONLINE	2

/* Defines for PORT port type attribute */
#define LPFC_FDMI_PORTTYPE_UNKNOWN	0
#define LPFC_FDMI_PORTTYPE_NPORT	1
#define LPFC_FDMI_PORTTYPE_NLPORT	2

/*
 *  Begin HBA configuration parameters.
 *  The PCI configuration register BAR assignments are:
 *  BAR0, offset 0x10 - SLIM base memory address
 *  BAR1, offset 0x14 - SLIM base memory high address
 *  BAR2, offset 0x18 - REGISTER base memory address
 *  BAR3, offset 0x1c - REGISTER base memory high address
 *  BAR4, offset 0x20 - BIU I/O registers
 *  BAR5, offset 0x24 - REGISTER base io high address
 */

/* Number of rings currently used and available. */
#define MAX_SLI3_CONFIGURED_RINGS     3
#define MAX_SLI3_RINGS                4

/* IOCB / Mailbox is owned by FireFly */
#define OWN_CHIP        1

/* IOCB / Mailbox is owned by Host */
#define OWN_HOST        0

/* Number of 4-byte words in an IOCB. */
#define IOCB_WORD_SZ    8

/* network headers for Dfctl field */
#define FC_NET_HDR      0x20

/* Start FireFly Register definitions */
#define PCI_VENDOR_ID_EMULEX        0x10df
#define PCI_DEVICE_ID_FIREFLY       0x1ae5
#define PCI_DEVICE_ID_PROTEUS_VF    0xe100
#define PCI_DEVICE_ID_BALIUS        0xe131
#define PCI_DEVICE_ID_PROTEUS_PF    0xe180
#define PCI_DEVICE_ID_LANCER_FC     0xe200
#define PCI_DEVICE_ID_LANCER_FC_VF  0xe208
#define PCI_DEVICE_ID_LANCER_FCOE   0xe260
#define PCI_DEVICE_ID_LANCER_FCOE_VF 0xe268
#define PCI_DEVICE_ID_LANCER_G6_FC  0xe300
#define PCI_DEVICE_ID_SAT_SMB       0xf011
#define PCI_DEVICE_ID_SAT_MID       0xf015
#define PCI_DEVICE_ID_RFLY          0xf095
#define PCI_DEVICE_ID_PFLY          0xf098
#define PCI_DEVICE_ID_LP101         0xf0a1
#define PCI_DEVICE_ID_TFLY          0xf0a5
#define PCI_DEVICE_ID_BSMB          0xf0d1
#define PCI_DEVICE_ID_BMID          0xf0d5
#define PCI_DEVICE_ID_ZSMB          0xf0e1
#define PCI_DEVICE_ID_ZMID          0xf0e5
#define PCI_DEVICE_ID_NEPTUNE       0xf0f5
#define PCI_DEVICE_ID_NEPTUNE_SCSP  0xf0f6
#define PCI_DEVICE_ID_NEPTUNE_DCSP  0xf0f7
#define PCI_DEVICE_ID_SAT           0xf100
#define PCI_DEVICE_ID_SAT_SCSP      0xf111
#define PCI_DEVICE_ID_SAT_DCSP      0xf112
#define PCI_DEVICE_ID_FALCON        0xf180
#define PCI_DEVICE_ID_SUPERFLY      0xf700
#define PCI_DEVICE_ID_DRAGONFLY     0xf800
#define PCI_DEVICE_ID_CENTAUR       0xf900
#define PCI_DEVICE_ID_PEGASUS       0xf980
#define PCI_DEVICE_ID_THOR          0xfa00
#define PCI_DEVICE_ID_VIPER         0xfb00
#define PCI_DEVICE_ID_LP10000S      0xfc00
#define PCI_DEVICE_ID_LP11000S      0xfc10
#define PCI_DEVICE_ID_LPE11000S     0xfc20
#define PCI_DEVICE_ID_SAT_S         0xfc40
#define PCI_DEVICE_ID_PROTEUS_S     0xfc50
#define PCI_DEVICE_ID_HELIOS        0xfd00
#define PCI_DEVICE_ID_HELIOS_SCSP   0xfd11
#define PCI_DEVICE_ID_HELIOS_DCSP   0xfd12
#define PCI_DEVICE_ID_ZEPHYR        0xfe00
#define PCI_DEVICE_ID_HORNET        0xfe05
#define PCI_DEVICE_ID_ZEPHYR_SCSP   0xfe11
#define PCI_DEVICE_ID_ZEPHYR_DCSP   0xfe12
#define PCI_VENDOR_ID_SERVERENGINE  0x19a2
#define PCI_DEVICE_ID_TIGERSHARK    0x0704
#define PCI_DEVICE_ID_TOMCAT        0x0714
#define PCI_DEVICE_ID_SKYHAWK       0x0724
#define PCI_DEVICE_ID_SKYHAWK_VF    0x072c

#define JEDEC_ID_ADDRESS            0x0080001c
#define FIREFLY_JEDEC_ID            0x1ACC
#define SUPERFLY_JEDEC_ID           0x0020
#define DRAGONFLY_JEDEC_ID          0x0021
#define DRAGONFLY_V2_JEDEC_ID       0x0025
#define CENTAUR_2G_JEDEC_ID         0x0026
#define CENTAUR_1G_JEDEC_ID         0x0028
#define PEGASUS_ORION_JEDEC_ID      0x0036
#define PEGASUS_JEDEC_ID            0x0038
#define THOR_JEDEC_ID               0x0012
#define HELIOS_JEDEC_ID             0x0364
#define ZEPHYR_JEDEC_ID             0x0577
#define VIPER_JEDEC_ID              0x4838
#define SATURN_JEDEC_ID             0x1004
#define HORNET_JDEC_ID              0x2057706D

#define JEDEC_ID_MASK               0x0FFFF000
#define JEDEC_ID_SHIFT              12
#define FC_JEDEC_ID(id)             ((id & JEDEC_ID_MASK) >> JEDEC_ID_SHIFT)

typedef struct {		/* FireFly BIU registers */
	uint32_t hostAtt;	/* See definitions for Host Attention
				   register */
	uint32_t chipAtt;	/* See definitions for Chip Attention
				   register */
	uint32_t hostStatus;	/* See definitions for Host Status register */
	uint32_t hostControl;	/* See definitions for Host Control register */
	uint32_t buiConfig;	/* See definitions for BIU configuration
				   register */
} FF_REGS;

/* IO Register size in bytes */
#define FF_REG_AREA_SIZE       256

/* Host Attention Register */

#define HA_REG_OFFSET  0	/* Byte offset from register base address */

#define HA_R0RE_REQ    0x00000001	/* Bit  0 */
#define HA_R0CE_RSP    0x00000002	/* Bit  1 */
#define HA_R0ATT       0x00000008	/* Bit  3 */
#define HA_R1RE_REQ    0x00000010	/* Bit  4 */
#define HA_R1CE_RSP    0x00000020	/* Bit  5 */
#define HA_R1ATT       0x00000080	/* Bit  7 */
#define HA_R2RE_REQ    0x00000100	/* Bit  8 */
#define HA_R2CE_RSP    0x00000200	/* Bit  9 */
#define HA_R2ATT       0x00000800	/* Bit 11 */
#define HA_R3RE_REQ    0x00001000	/* Bit 12 */
#define HA_R3CE_RSP    0x00002000	/* Bit 13 */
#define HA_R3ATT       0x00008000	/* Bit 15 */
#define HA_LATT        0x20000000	/* Bit 29 */
#define HA_MBATT       0x40000000	/* Bit 30 */
#define HA_ERATT       0x80000000	/* Bit 31 */

#define HA_RXRE_REQ    0x00000001	/* Bit  0 */
#define HA_RXCE_RSP    0x00000002	/* Bit  1 */
#define HA_RXATT       0x00000008	/* Bit  3 */
#define HA_RXMASK      0x0000000f

#define HA_R0_CLR_MSK	(HA_R0RE_REQ | HA_R0CE_RSP | HA_R0ATT)
#define HA_R1_CLR_MSK	(HA_R1RE_REQ | HA_R1CE_RSP | HA_R1ATT)
#define HA_R2_CLR_MSK	(HA_R2RE_REQ | HA_R2CE_RSP | HA_R2ATT)
#define HA_R3_CLR_MSK	(HA_R3RE_REQ | HA_R3CE_RSP | HA_R3ATT)

#define HA_R0_POS	3
#define HA_R1_POS	7
#define HA_R2_POS	11
#define HA_R3_POS	15
#define HA_LE_POS	29
#define HA_MB_POS	30
#define HA_ER_POS	31
/* Chip Attention Register */

#define CA_REG_OFFSET  4	/* Byte offset from register base address */

#define CA_R0CE_REQ    0x00000001	/* Bit  0 */
#define CA_R0RE_RSP    0x00000002	/* Bit  1 */
#define CA_R0ATT       0x00000008	/* Bit  3 */
#define CA_R1CE_REQ    0x00000010	/* Bit  4 */
#define CA_R1RE_RSP    0x00000020	/* Bit  5 */
#define CA_R1ATT       0x00000080	/* Bit  7 */
#define CA_R2CE_REQ    0x00000100	/* Bit  8 */
#define CA_R2RE_RSP    0x00000200	/* Bit  9 */
#define CA_R2ATT       0x00000800	/* Bit 11 */
#define CA_R3CE_REQ    0x00001000	/* Bit 12 */
#define CA_R3RE_RSP    0x00002000	/* Bit 13 */
#define CA_R3ATT       0x00008000	/* Bit 15 */
#define CA_MBATT       0x40000000	/* Bit 30 */

/* Host Status Register */

#define HS_REG_OFFSET  8	/* Byte offset from register base address */

#define HS_MBRDY       0x00400000	/* Bit 22 */
#define HS_FFRDY       0x00800000	/* Bit 23 */
#define HS_FFER8       0x01000000	/* Bit 24 */
#define HS_FFER7       0x02000000	/* Bit 25 */
#define HS_FFER6       0x04000000	/* Bit 26 */
#define HS_FFER5       0x08000000	/* Bit 27 */
#define HS_FFER4       0x10000000	/* Bit 28 */
#define HS_FFER3       0x20000000	/* Bit 29 */
#define HS_FFER2       0x40000000	/* Bit 30 */
#define HS_FFER1       0x80000000	/* Bit 31 */
#define HS_CRIT_TEMP   0x00000100	/* Bit 8  */
#define HS_FFERM       0xFF000100	/* Mask for error bits 31:24 and 8 */
#define UNPLUG_ERR     0x00000001	/* Indicate pci hot unplug */
/* Host Control Register */

#define HC_REG_OFFSET  12	/* Byte offset from register base address */

#define HC_MBINT_ENA   0x00000001	/* Bit  0 */
#define HC_R0INT_ENA   0x00000002	/* Bit  1 */
#define HC_R1INT_ENA   0x00000004	/* Bit  2 */
#define HC_R2INT_ENA   0x00000008	/* Bit  3 */
#define HC_R3INT_ENA   0x00000010	/* Bit  4 */
#define HC_INITHBI     0x02000000	/* Bit 25 */
#define HC_INITMB      0x04000000	/* Bit 26 */
#define HC_INITFF      0x08000000	/* Bit 27 */
#define HC_LAINT_ENA   0x20000000	/* Bit 29 */
#define HC_ERINT_ENA   0x80000000	/* Bit 31 */

/* Message Signaled Interrupt eXtension (MSI-X) message identifiers */
#define MSIX_DFLT_ID	0
#define MSIX_RNG0_ID	0
#define MSIX_RNG1_ID	1
#define MSIX_RNG2_ID	2
#define MSIX_RNG3_ID	3

#define MSIX_LINK_ID	4
#define MSIX_MBOX_ID	5

#define MSIX_SPARE0_ID	6
#define MSIX_SPARE1_ID	7

/* Mailbox Commands */
#define MBX_SHUTDOWN        0x00	/* terminate testing */
#define MBX_LOAD_SM         0x01
#define MBX_READ_NV         0x02
#define MBX_WRITE_NV        0x03
#define MBX_RUN_BIU_DIAG    0x04
#define MBX_INIT_LINK       0x05
#define MBX_DOWN_LINK       0x06
#define MBX_CONFIG_LINK     0x07
#define MBX_CONFIG_RING     0x09
#define MBX_RESET_RING      0x0A
#define MBX_READ_CONFIG     0x0B
#define MBX_READ_RCONFIG    0x0C
#define MBX_READ_SPARM      0x0D
#define MBX_READ_STATUS     0x0E
#define MBX_READ_RPI        0x0F
#define MBX_READ_XRI        0x10
#define MBX_READ_REV        0x11
#define MBX_READ_LNK_STAT   0x12
#define MBX_REG_LOGIN       0x13
#define MBX_UNREG_LOGIN     0x14
#define MBX_CLEAR_LA        0x16
#define MBX_DUMP_MEMORY     0x17
#define MBX_DUMP_CONTEXT    0x18
#define MBX_RUN_DIAGS       0x19
#define MBX_RESTART         0x1A
#define MBX_UPDATE_CFG      0x1B
#define MBX_DOWN_LOAD       0x1C
#define MBX_DEL_LD_ENTRY    0x1D
#define MBX_RUN_PROGRAM     0x1E
#define MBX_SET_MASK        0x20
#define MBX_SET_VARIABLE    0x21
#define MBX_UNREG_D_ID      0x23
#define MBX_KILL_BOARD      0x24
#define MBX_CONFIG_FARP     0x25
#define MBX_BEACON          0x2A
#define MBX_CONFIG_MSI      0x30
#define MBX_HEARTBEAT       0x31
#define MBX_WRITE_VPARMS    0x32
#define MBX_ASYNCEVT_ENABLE 0x33
#define MBX_READ_EVENT_LOG_STATUS 0x37
#define MBX_READ_EVENT_LOG  0x38
#define MBX_WRITE_EVENT_LOG 0x39

#define MBX_PORT_CAPABILITIES 0x3B
#define MBX_PORT_IOV_CONTROL 0x3C

#define MBX_CONFIG_HBQ	    0x7C
#define MBX_LOAD_AREA       0x81
#define MBX_RUN_BIU_DIAG64  0x84
#define MBX_CONFIG_PORT     0x88
#define MBX_READ_SPARM64    0x8D
#define MBX_READ_RPI64      0x8F
#define MBX_REG_LOGIN64     0x93
#define MBX_READ_TOPOLOGY   0x95
#define MBX_REG_VPI	    0x96
#define MBX_UNREG_VPI	    0x97

#define MBX_WRITE_WWN       0x98
#define MBX_SET_DEBUG       0x99
#define MBX_LOAD_EXP_ROM    0x9C
#define MBX_SLI4_CONFIG	    0x9B
#define MBX_SLI4_REQ_FTRS   0x9D
#define MBX_MAX_CMDS        0x9E
#define MBX_RESUME_RPI      0x9E
#define MBX_SLI2_CMD_MASK   0x80
#define MBX_REG_VFI         0x9F
#define MBX_REG_FCFI        0xA0
#define MBX_UNREG_VFI       0xA1
#define MBX_UNREG_FCFI	    0xA2
#define MBX_INIT_VFI        0xA3
#define MBX_INIT_VPI        0xA4
#define MBX_ACCESS_VDATA    0xA5
#define MBX_REG_FCFI_MRQ    0xAF

#define MBX_AUTH_PORT       0xF8
#define MBX_SECURITY_MGMT   0xF9

/* IOCB Commands */

#define CMD_RCV_SEQUENCE_CX     0x01
#define CMD_XMIT_SEQUENCE_CR    0x02
#define CMD_XMIT_SEQUENCE_CX    0x03
#define CMD_XMIT_BCAST_CN       0x04
#define CMD_XMIT_BCAST_CX       0x05
#define CMD_QUE_RING_BUF_CN     0x06
#define CMD_QUE_XRI_BUF_CX      0x07
#define CMD_IOCB_CONTINUE_CN    0x08
#define CMD_RET_XRI_BUF_CX      0x09
#define CMD_ELS_REQUEST_CR      0x0A
#define CMD_ELS_REQUEST_CX      0x0B
#define CMD_RCV_ELS_REQ_CX      0x0D
#define CMD_ABORT_XRI_CN        0x0E
#define CMD_ABORT_XRI_CX        0x0F
#define CMD_CLOSE_XRI_CN        0x10
#define CMD_CLOSE_XRI_CX        0x11
#define CMD_CREATE_XRI_CR       0x12
#define CMD_CREATE_XRI_CX       0x13
#define CMD_GET_RPI_CN          0x14
#define CMD_XMIT_ELS_RSP_CX     0x15
#define CMD_GET_RPI_CR          0x16
#define CMD_XRI_ABORTED_CX      0x17
#define CMD_FCP_IWRITE_CR       0x18
#define CMD_FCP_IWRITE_CX       0x19
#define CMD_FCP_IREAD_CR        0x1A
#define CMD_FCP_IREAD_CX        0x1B
#define CMD_FCP_ICMND_CR        0x1C
#define CMD_FCP_ICMND_CX        0x1D
#define CMD_FCP_TSEND_CX        0x1F
#define CMD_FCP_TRECEIVE_CX     0x21
#define CMD_FCP_TRSP_CX	        0x23
#define CMD_FCP_AUTO_TRSP_CX    0x29

#define CMD_ADAPTER_MSG         0x20
#define CMD_ADAPTER_DUMP        0x22

/*  SLI_2 IOCB Command Set */

#define CMD_ASYNC_STATUS        0x7C
#define CMD_RCV_SEQUENCE64_CX   0x81
#define CMD_XMIT_SEQUENCE64_CR  0x82
#define CMD_XMIT_SEQUENCE64_CX  0x83
#define CMD_XMIT_BCAST64_CN     0x84
#define CMD_XMIT_BCAST64_CX     0x85
#define CMD_QUE_RING_BUF64_CN   0x86
#define CMD_QUE_XRI_BUF64_CX    0x87
#define CMD_IOCB_CONTINUE64_CN  0x88
#define CMD_RET_XRI_BUF64_CX    0x89
#define CMD_ELS_REQUEST64_CR    0x8A
#define CMD_ELS_REQUEST64_CX    0x8B
#define CMD_ABORT_MXRI64_CN     0x8C
#define CMD_RCV_ELS_REQ64_CX    0x8D
#define CMD_XMIT_ELS_RSP64_CX   0x95
#define CMD_XMIT_BLS_RSP64_CX   0x97
#define CMD_FCP_IWRITE64_CR     0x98
#define CMD_FCP_IWRITE64_CX     0x99
#define CMD_FCP_IREAD64_CR      0x9A
#define CMD_FCP_IREAD64_CX      0x9B
#define CMD_FCP_ICMND64_CR      0x9C
#define CMD_FCP_ICMND64_CX      0x9D
#define CMD_FCP_TSEND64_CX      0x9F
#define CMD_FCP_TRECEIVE64_CX   0xA1
#define CMD_FCP_TRSP64_CX       0xA3

#define CMD_QUE_XRI64_CX	0xB3
#define CMD_IOCB_RCV_SEQ64_CX	0xB5
#define CMD_IOCB_RCV_ELS64_CX	0xB7
#define CMD_IOCB_RET_XRI64_CX	0xB9
#define CMD_IOCB_RCV_CONT64_CX	0xBB

#define CMD_GEN_REQUEST64_CR    0xC2
#define CMD_GEN_REQUEST64_CX    0xC3

/* Unhandled SLI-3 Commands */
#define CMD_IOCB_XMIT_MSEQ64_CR		0xB0
#define CMD_IOCB_XMIT_MSEQ64_CX		0xB1
#define CMD_IOCB_RCV_SEQ_LIST64_CX	0xC1
#define CMD_IOCB_RCV_ELS_LIST64_CX	0xCD
#define CMD_IOCB_CLOSE_EXTENDED_CN	0xB6
#define CMD_IOCB_ABORT_EXTENDED_CN	0xBA
#define CMD_IOCB_RET_HBQE64_CN		0xCA
#define CMD_IOCB_FCP_IBIDIR64_CR	0xAC
#define CMD_IOCB_FCP_IBIDIR64_CX	0xAD
#define CMD_IOCB_FCP_ITASKMGT64_CX	0xAF
#define CMD_IOCB_LOGENTRY_CN		0x94
#define CMD_IOCB_LOGENTRY_ASYNC_CN	0x96

/* Data Security SLI Commands */
#define DSSCMD_IWRITE64_CR		0xF8
#define DSSCMD_IWRITE64_CX		0xF9
#define DSSCMD_IREAD64_CR		0xFA
#define DSSCMD_IREAD64_CX		0xFB

#define CMD_MAX_IOCB_CMD        0xFB
#define CMD_IOCB_MASK           0xff

#define MAX_MSG_DATA            28	/* max msg data in CMD_ADAPTER_MSG
					   iocb */
#define LPFC_MAX_ADPTMSG         32	/* max msg data */
/*
 *  Define Status
 */
#define MBX_SUCCESS                 0
#define MBXERR_NUM_RINGS            1
#define MBXERR_NUM_IOCBS            2
#define MBXERR_IOCBS_EXCEEDED       3
#define MBXERR_BAD_RING_NUMBER      4
#define MBXERR_MASK_ENTRIES_RANGE   5
#define MBXERR_MASKS_EXCEEDED       6
#define MBXERR_BAD_PROFILE          7
#define MBXERR_BAD_DEF_CLASS        8
#define MBXERR_BAD_MAX_RESPONDER    9
#define MBXERR_BAD_MAX_ORIGINATOR   10
#define MBXERR_RPI_REGISTERED       11
#define MBXERR_RPI_FULL             12
#define MBXERR_NO_RESOURCES         13
#define MBXERR_BAD_RCV_LENGTH       14
#define MBXERR_DMA_ERROR            15
#define MBXERR_ERROR                16
#define MBXERR_LINK_DOWN            0x33
#define MBXERR_SEC_NO_PERMISSION    0xF02
#define MBX_NOT_FINISHED            255

#define MBX_BUSY                   0xffffff /* Attempted cmd to busy Mailbox */
#define MBX_TIMEOUT                0xfffffe /* time-out expired waiting for */

#define TEMPERATURE_OFFSET 0xB0	/* Slim offset for critical temperature event */

/*
 * return code Fail
 */
#define FAILURE 1

/*
 *    Begin Structure Definitions for Mailbox Commands
 */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t tval;
	uint8_t tmask;
	uint8_t rval;
	uint8_t rmask;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint8_t rmask;
	uint8_t rval;
	uint8_t tmask;
	uint8_t tval;
#endif
} RR_REG;

struct ulp_bde {
	uint32_t bdeAddress;
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t bdeReserved:4;
	uint32_t bdeAddrHigh:4;
	uint32_t bdeSize:24;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t bdeSize:24;
	uint32_t bdeAddrHigh:4;
	uint32_t bdeReserved:4;
#endif
};

typedef struct ULP_BDL {	/* SLI-2 */
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t bdeFlags:8;	/* BDL Flags */
	uint32_t bdeSize:24;	/* Size of BDL array in host memory (bytes) */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t bdeSize:24;	/* Size of BDL array in host memory (bytes) */
	uint32_t bdeFlags:8;	/* BDL Flags */
#endif

	uint32_t addrLow;	/* Address 0:31 */
	uint32_t addrHigh;	/* Address 32:63 */
	uint32_t ulpIoTag32;	/* Can be used for 32 bit I/O Tag */
} ULP_BDL;

/*
 * BlockGuard Definitions
 */

enum lpfc_protgrp_type {
	LPFC_PG_TYPE_INVALID = 0, /* used to indicate errors                  */
	LPFC_PG_TYPE_NO_DIF,	  /* no DIF data pointed to by prot grp       */
	LPFC_PG_TYPE_EMBD_DIF,	  /* DIF is embedded (inline) with data       */
	LPFC_PG_TYPE_DIF_BUF	  /* DIF has its own scatter/gather list      */
};

/* PDE Descriptors */
#define LPFC_PDE5_DESCRIPTOR		0x85
#define LPFC_PDE6_DESCRIPTOR		0x86
#define LPFC_PDE7_DESCRIPTOR		0x87

/* BlockGuard Opcodes */
#define BG_OP_IN_NODIF_OUT_CRC		0x0
#define	BG_OP_IN_CRC_OUT_NODIF		0x1
#define	BG_OP_IN_NODIF_OUT_CSUM		0x2
#define	BG_OP_IN_CSUM_OUT_NODIF		0x3
#define	BG_OP_IN_CRC_OUT_CRC		0x4
#define	BG_OP_IN_CSUM_OUT_CSUM		0x5
#define	BG_OP_IN_CRC_OUT_CSUM		0x6
#define	BG_OP_IN_CSUM_OUT_CRC		0x7
#define	BG_OP_RAW_MODE			0x8

struct lpfc_pde5 {
	uint32_t word0;
#define pde5_type_SHIFT		24
#define pde5_type_MASK		0x000000ff
#define pde5_type_WORD		word0
#define pde5_rsvd0_SHIFT	0
#define pde5_rsvd0_MASK		0x00ffffff
#define pde5_rsvd0_WORD		word0
	uint32_t reftag;	/* Reference Tag Value			*/
	uint32_t reftagtr;	/* Reference Tag Translation Value 	*/
};

struct lpfc_pde6 {
	uint32_t word0;
#define pde6_type_SHIFT		24
#define pde6_type_MASK		0x000000ff
#define pde6_type_WORD		word0
#define pde6_rsvd0_SHIFT	0
#define pde6_rsvd0_MASK		0x00ffffff
#define pde6_rsvd0_WORD		word0
	uint32_t word1;
#define pde6_rsvd1_SHIFT	26
#define pde6_rsvd1_MASK		0x0000003f
#define pde6_rsvd1_WORD		word1
#define pde6_na_SHIFT		25
#define pde6_na_MASK		0x00000001
#define pde6_na_WORD		word1
#define pde6_rsvd2_SHIFT	16
#define pde6_rsvd2_MASK		0x000001FF
#define pde6_rsvd2_WORD		word1
#define pde6_apptagtr_SHIFT	0
#define pde6_apptagtr_MASK	0x0000ffff
#define pde6_apptagtr_WORD	word1
	uint32_t word2;
#define pde6_optx_SHIFT		28
#define pde6_optx_MASK		0x0000000f
#define pde6_optx_WORD		word2
#define pde6_oprx_SHIFT		24
#define pde6_oprx_MASK		0x0000000f
#define pde6_oprx_WORD		word2
#define pde6_nr_SHIFT		23
#define pde6_nr_MASK		0x00000001
#define pde6_nr_WORD		word2
#define pde6_ce_SHIFT		22
#define pde6_ce_MASK		0x00000001
#define pde6_ce_WORD		word2
#define pde6_re_SHIFT		21
#define pde6_re_MASK		0x00000001
#define pde6_re_WORD		word2
#define pde6_ae_SHIFT		20
#define pde6_ae_MASK		0x00000001
#define pde6_ae_WORD		word2
#define pde6_ai_SHIFT		19
#define pde6_ai_MASK		0x00000001
#define pde6_ai_WORD		word2
#define pde6_bs_SHIFT		16
#define pde6_bs_MASK		0x00000007
#define pde6_bs_WORD		word2
#define pde6_apptagval_SHIFT	0
#define pde6_apptagval_MASK	0x0000ffff
#define pde6_apptagval_WORD	word2
};

struct lpfc_pde7 {
	uint32_t word0;
#define pde7_type_SHIFT		24
#define pde7_type_MASK		0x000000ff
#define pde7_type_WORD		word0
#define pde7_rsvd0_SHIFT	0
#define pde7_rsvd0_MASK		0x00ffffff
#define pde7_rsvd0_WORD		word0
	uint32_t addrHigh;
	uint32_t addrLow;
};

/* Structure for MB Command LOAD_SM and DOWN_LOAD */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd2:25;
	uint32_t acknowledgment:1;
	uint32_t version:1;
	uint32_t erase_or_prog:1;
	uint32_t update_flash:1;
	uint32_t update_ram:1;
	uint32_t method:1;
	uint32_t load_cmplt:1;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t load_cmplt:1;
	uint32_t method:1;
	uint32_t update_ram:1;
	uint32_t update_flash:1;
	uint32_t erase_or_prog:1;
	uint32_t version:1;
	uint32_t acknowledgment:1;
	uint32_t rsvd2:25;
#endif

	uint32_t dl_to_adr_low;
	uint32_t dl_to_adr_high;
	uint32_t dl_len;
	union {
		uint32_t dl_from_mbx_offset;
		struct ulp_bde dl_from_bde;
		struct ulp_bde64 dl_from_bde64;
	} un;

} LOAD_SM_VAR;

/* Structure for MB Command READ_NVPARM (02) */

typedef struct {
	uint32_t rsvd1[3];	/* Read as all one's */
	uint32_t rsvd2;		/* Read as all zero's */
	uint32_t portname[2];	/* N_PORT name */
	uint32_t nodename[2];	/* NODE name */

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pref_DID:24;
	uint32_t hardAL_PA:8;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t hardAL_PA:8;
	uint32_t pref_DID:24;
#endif

	uint32_t rsvd3[21];	/* Read as all one's */
} READ_NV_VAR;

/* Structure for MB Command WRITE_NVPARMS (03) */

typedef struct {
	uint32_t rsvd1[3];	/* Must be all one's */
	uint32_t rsvd2;		/* Must be all zero's */
	uint32_t portname[2];	/* N_PORT name */
	uint32_t nodename[2];	/* NODE name */

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pref_DID:24;
	uint32_t hardAL_PA:8;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t hardAL_PA:8;
	uint32_t pref_DID:24;
#endif

	uint32_t rsvd3[21];	/* Must be all one's */
} WRITE_NV_VAR;

/* Structure for MB Command RUN_BIU_DIAG (04) */
/* Structure for MB Command RUN_BIU_DIAG64 (0x84) */

typedef struct {
	uint32_t rsvd1;
	union {
		struct {
			struct ulp_bde xmit_bde;
			struct ulp_bde rcv_bde;
		} s1;
		struct {
			struct ulp_bde64 xmit_bde64;
			struct ulp_bde64 rcv_bde64;
		} s2;
	} un;
} BIU_DIAG_VAR;

/* Structure for MB command READ_EVENT_LOG (0x38) */
struct READ_EVENT_LOG_VAR {
	uint32_t word1;
#define lpfc_event_log_SHIFT	29
#define lpfc_event_log_MASK	0x00000001
#define lpfc_event_log_WORD	word1
#define USE_MAILBOX_RESPONSE	1
	uint32_t offset;
	struct ulp_bde64 rcv_bde64;
};

/* Structure for MB Command INIT_LINK (05) */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd1:24;
	uint32_t lipsr_AL_PA:8;	/* AL_PA to issue Lip Selective Reset to */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t lipsr_AL_PA:8;	/* AL_PA to issue Lip Selective Reset to */
	uint32_t rsvd1:24;
#endif

#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t fabric_AL_PA;	/* If using a Fabric Assigned AL_PA */
	uint8_t rsvd2;
	uint16_t link_flags;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t link_flags;
	uint8_t rsvd2;
	uint8_t fabric_AL_PA;	/* If using a Fabric Assigned AL_PA */
#endif

#define FLAGS_TOPOLOGY_MODE_LOOP_PT  0x00 /* Attempt loop then pt-pt */
#define FLAGS_LOCAL_LB               0x01 /* link_flags (=1) ENDEC loopback */
#define FLAGS_TOPOLOGY_MODE_PT_PT    0x02 /* Attempt pt-pt only */
#define FLAGS_TOPOLOGY_MODE_LOOP     0x04 /* Attempt loop only */
#define FLAGS_TOPOLOGY_MODE_PT_LOOP  0x06 /* Attempt pt-pt then loop */
#define	FLAGS_UNREG_LOGIN_ALL	     0x08 /* UNREG_LOGIN all on link down */
#define FLAGS_LIRP_LILP              0x80 /* LIRP / LILP is disabled */

#define FLAGS_TOPOLOGY_FAILOVER      0x0400	/* Bit 10 */
#define FLAGS_LINK_SPEED             0x0800	/* Bit 11 */
#define FLAGS_IMED_ABORT             0x04000	/* Bit 14 */

	uint32_t link_speed;
#define LINK_SPEED_AUTO 0x0     /* Auto selection */
#define LINK_SPEED_1G   0x1     /* 1 Gigabaud */
#define LINK_SPEED_2G   0x2     /* 2 Gigabaud */
#define LINK_SPEED_4G   0x4     /* 4 Gigabaud */
#define LINK_SPEED_8G   0x8     /* 8 Gigabaud */
#define LINK_SPEED_10G  0x10    /* 10 Gigabaud */
#define LINK_SPEED_16G  0x11    /* 16 Gigabaud */
#define LINK_SPEED_32G  0x14    /* 32 Gigabaud */

} INIT_LINK_VAR;

/* Structure for MB Command DOWN_LINK (06) */

typedef struct {
	uint32_t rsvd1;
} DOWN_LINK_VAR;

/* Structure for MB Command CONFIG_LINK (07) */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t cr:1;
	uint32_t ci:1;
	uint32_t cr_delay:6;
	uint32_t cr_count:8;
	uint32_t rsvd1:8;
	uint32_t MaxBBC:8;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t MaxBBC:8;
	uint32_t rsvd1:8;
	uint32_t cr_count:8;
	uint32_t cr_delay:6;
	uint32_t ci:1;
	uint32_t cr:1;
#endif

	uint32_t myId;
	uint32_t rsvd2;
	uint32_t edtov;
	uint32_t arbtov;
	uint32_t ratov;
	uint32_t rttov;
	uint32_t altov;
	uint32_t crtov;
	uint32_t citov;
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rrq_enable:1;
	uint32_t rrq_immed:1;
	uint32_t rsvd4:29;
	uint32_t ack0_enable:1;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t ack0_enable:1;
	uint32_t rsvd4:29;
	uint32_t rrq_immed:1;
	uint32_t rrq_enable:1;
#endif
} CONFIG_LINK;

/* Structure for MB Command PART_SLIM (08)
 * will be removed since SLI1 is no longer supported!
 */
typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t offCiocb;
	uint16_t numCiocb;
	uint16_t offRiocb;
	uint16_t numRiocb;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t numCiocb;
	uint16_t offCiocb;
	uint16_t numRiocb;
	uint16_t offRiocb;
#endif
} RING_DEF;

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t unused1:24;
	uint32_t numRing:8;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t numRing:8;
	uint32_t unused1:24;
#endif

	RING_DEF ringdef[4];
	uint32_t hbainit;
} PART_SLIM_VAR;

/* Structure for MB Command CONFIG_RING (09) */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t unused2:6;
	uint32_t recvSeq:1;
	uint32_t recvNotify:1;
	uint32_t numMask:8;
	uint32_t profile:8;
	uint32_t unused1:4;
	uint32_t ring:4;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t ring:4;
	uint32_t unused1:4;
	uint32_t profile:8;
	uint32_t numMask:8;
	uint32_t recvNotify:1;
	uint32_t recvSeq:1;
	uint32_t unused2:6;
#endif

#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t maxRespXchg;
	uint16_t maxOrigXchg;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t maxOrigXchg;
	uint16_t maxRespXchg;
#endif

	RR_REG rrRegs[6];
} CONFIG_RING_VAR;

/* Structure for MB Command RESET_RING (10) */

typedef struct {
	uint32_t ring_no;
} RESET_RING_VAR;

/* Structure for MB Command READ_CONFIG (11) */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t cr:1;
	uint32_t ci:1;
	uint32_t cr_delay:6;
	uint32_t cr_count:8;
	uint32_t InitBBC:8;
	uint32_t MaxBBC:8;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t MaxBBC:8;
	uint32_t InitBBC:8;
	uint32_t cr_count:8;
	uint32_t cr_delay:6;
	uint32_t ci:1;
	uint32_t cr:1;
#endif

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t topology:8;
	uint32_t myDid:24;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t myDid:24;
	uint32_t topology:8;
#endif

	/* Defines for topology (defined previously) */
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t AR:1;
	uint32_t IR:1;
	uint32_t rsvd1:29;
	uint32_t ack0:1;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t ack0:1;
	uint32_t rsvd1:29;
	uint32_t IR:1;
	uint32_t AR:1;
#endif

	uint32_t edtov;
	uint32_t arbtov;
	uint32_t ratov;
	uint32_t rttov;
	uint32_t altov;
	uint32_t lmt;
#define LMT_RESERVED  0x000    /* Not used */
#define LMT_1Gb       0x004
#define LMT_2Gb       0x008
#define LMT_4Gb       0x040
#define LMT_8Gb       0x080
#define LMT_10Gb      0x100
#define LMT_16Gb      0x200
#define LMT_32Gb      0x400
	uint32_t rsvd2;
	uint32_t rsvd3;
	uint32_t max_xri;
	uint32_t max_iocb;
	uint32_t max_rpi;
	uint32_t avail_xri;
	uint32_t avail_iocb;
	uint32_t avail_rpi;
	uint32_t max_vpi;
	uint32_t rsvd4;
	uint32_t rsvd5;
	uint32_t avail_vpi;
} READ_CONFIG_VAR;

/* Structure for MB Command READ_RCONFIG (12) */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd2:7;
	uint32_t recvNotify:1;
	uint32_t numMask:8;
	uint32_t profile:8;
	uint32_t rsvd1:4;
	uint32_t ring:4;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t ring:4;
	uint32_t rsvd1:4;
	uint32_t profile:8;
	uint32_t numMask:8;
	uint32_t recvNotify:1;
	uint32_t rsvd2:7;
#endif

#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t maxResp;
	uint16_t maxOrig;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t maxOrig;
	uint16_t maxResp;
#endif

	RR_REG rrRegs[6];

#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t cmdRingOffset;
	uint16_t cmdEntryCnt;
	uint16_t rspRingOffset;
	uint16_t rspEntryCnt;
	uint16_t nextCmdOffset;
	uint16_t rsvd3;
	uint16_t nextRspOffset;
	uint16_t rsvd4;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t cmdEntryCnt;
	uint16_t cmdRingOffset;
	uint16_t rspEntryCnt;
	uint16_t rspRingOffset;
	uint16_t rsvd3;
	uint16_t nextCmdOffset;
	uint16_t rsvd4;
	uint16_t nextRspOffset;
#endif
} READ_RCONF_VAR;

/* Structure for MB Command READ_SPARM (13) */
/* Structure for MB Command READ_SPARM64 (0x8D) */

typedef struct {
	uint32_t rsvd1;
	uint32_t rsvd2;
	union {
		struct ulp_bde sp; /* This BDE points to struct serv_parm
				      structure */
		struct ulp_bde64 sp64;
	} un;
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t rsvd3;
	uint16_t vpi;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t vpi;
	uint16_t rsvd3;
#endif
} READ_SPARM_VAR;

/* Structure for MB Command READ_STATUS (14) */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd1:31;
	uint32_t clrCounters:1;
	uint16_t activeXriCnt;
	uint16_t activeRpiCnt;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t clrCounters:1;
	uint32_t rsvd1:31;
	uint16_t activeRpiCnt;
	uint16_t activeXriCnt;
#endif

	uint32_t xmitByteCnt;
	uint32_t rcvByteCnt;
	uint32_t xmitFrameCnt;
	uint32_t rcvFrameCnt;
	uint32_t xmitSeqCnt;
	uint32_t rcvSeqCnt;
	uint32_t totalOrigExchanges;
	uint32_t totalRespExchanges;
	uint32_t rcvPbsyCnt;
	uint32_t rcvFbsyCnt;
} READ_STATUS_VAR;

/* Structure for MB Command READ_RPI (15) */
/* Structure for MB Command READ_RPI64 (0x8F) */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t nextRpi;
	uint16_t reqRpi;
	uint32_t rsvd2:8;
	uint32_t DID:24;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t reqRpi;
	uint16_t nextRpi;
	uint32_t DID:24;
	uint32_t rsvd2:8;
#endif

	union {
		struct ulp_bde sp;
		struct ulp_bde64 sp64;
	} un;

} READ_RPI_VAR;

/* Structure for MB Command READ_XRI (16) */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t nextXri;
	uint16_t reqXri;
	uint16_t rsvd1;
	uint16_t rpi;
	uint32_t rsvd2:8;
	uint32_t DID:24;
	uint32_t rsvd3:8;
	uint32_t SID:24;
	uint32_t rsvd4;
	uint8_t seqId;
	uint8_t rsvd5;
	uint16_t seqCount;
	uint16_t oxId;
	uint16_t rxId;
	uint32_t rsvd6:30;
	uint32_t si:1;
	uint32_t exchOrig:1;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t reqXri;
	uint16_t nextXri;
	uint16_t rpi;
	uint16_t rsvd1;
	uint32_t DID:24;
	uint32_t rsvd2:8;
	uint32_t SID:24;
	uint32_t rsvd3:8;
	uint32_t rsvd4;
	uint16_t seqCount;
	uint8_t rsvd5;
	uint8_t seqId;
	uint16_t rxId;
	uint16_t oxId;
	uint32_t exchOrig:1;
	uint32_t si:1;
	uint32_t rsvd6:30;
#endif
} READ_XRI_VAR;

/* Structure for MB Command READ_REV (17) */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t cv:1;
	uint32_t rr:1;
	uint32_t rsvd2:2;
	uint32_t v3req:1;
	uint32_t v3rsp:1;
	uint32_t rsvd1:25;
	uint32_t rv:1;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t rv:1;
	uint32_t rsvd1:25;
	uint32_t v3rsp:1;
	uint32_t v3req:1;
	uint32_t rsvd2:2;
	uint32_t rr:1;
	uint32_t cv:1;
#endif

	uint32_t biuRev;
	uint32_t smRev;
	union {
		uint32_t smFwRev;
		struct {
#ifdef __BIG_ENDIAN_BITFIELD
			uint8_t ProgType;
			uint8_t ProgId;
			uint16_t ProgVer:4;
			uint16_t ProgRev:4;
			uint16_t ProgFixLvl:2;
			uint16_t ProgDistType:2;
			uint16_t DistCnt:4;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
			uint16_t DistCnt:4;
			uint16_t ProgDistType:2;
			uint16_t ProgFixLvl:2;
			uint16_t ProgRev:4;
			uint16_t ProgVer:4;
			uint8_t ProgId;
			uint8_t ProgType;
#endif

		} b;
	} un;
	uint32_t endecRev;
#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t feaLevelHigh;
	uint8_t feaLevelLow;
	uint8_t fcphHigh;
	uint8_t fcphLow;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint8_t fcphLow;
	uint8_t fcphHigh;
	uint8_t feaLevelLow;
	uint8_t feaLevelHigh;
#endif

	uint32_t postKernRev;
	uint32_t opFwRev;
	uint8_t opFwName[16];
	uint32_t sli1FwRev;
	uint8_t sli1FwName[16];
	uint32_t sli2FwRev;
	uint8_t sli2FwName[16];
	uint32_t sli3Feat;
	uint32_t RandomData[6];
} READ_REV_VAR;

/* Structure for MB Command READ_LINK_STAT (18) */

typedef struct {
	uint32_t word0;

#define lpfc_read_link_stat_rec_SHIFT   0
#define lpfc_read_link_stat_rec_MASK   0x1
#define lpfc_read_link_stat_rec_WORD   word0

#define lpfc_read_link_stat_gec_SHIFT	1
#define lpfc_read_link_stat_gec_MASK   0x1
#define lpfc_read_link_stat_gec_WORD   word0

#define lpfc_read_link_stat_w02oftow23of_SHIFT	2
#define lpfc_read_link_stat_w02oftow23of_MASK   0x3FFFFF
#define lpfc_read_link_stat_w02oftow23of_WORD   word0

#define lpfc_read_link_stat_rsvd_SHIFT	24
#define lpfc_read_link_stat_rsvd_MASK   0x1F
#define lpfc_read_link_stat_rsvd_WORD   word0

#define lpfc_read_link_stat_gec2_SHIFT  29
#define lpfc_read_link_stat_gec2_MASK   0x1
#define lpfc_read_link_stat_gec2_WORD   word0

#define lpfc_read_link_stat_clrc_SHIFT  30
#define lpfc_read_link_stat_clrc_MASK   0x1
#define lpfc_read_link_stat_clrc_WORD   word0

#define lpfc_read_link_stat_clof_SHIFT  31
#define lpfc_read_link_stat_clof_MASK   0x1
#define lpfc_read_link_stat_clof_WORD   word0

	uint32_t linkFailureCnt;
	uint32_t lossSyncCnt;
	uint32_t lossSignalCnt;
	uint32_t primSeqErrCnt;
	uint32_t invalidXmitWord;
	uint32_t crcCnt;
	uint32_t primSeqTimeout;
	uint32_t elasticOverrun;
	uint32_t arbTimeout;
	uint32_t advRecBufCredit;
	uint32_t curRecBufCredit;
	uint32_t advTransBufCredit;
	uint32_t curTransBufCredit;
	uint32_t recEofCount;
	uint32_t recEofdtiCount;
	uint32_t recEofniCount;
	uint32_t recSofcount;
	uint32_t rsvd1;
	uint32_t rsvd2;
	uint32_t recDrpXriCount;
	uint32_t fecCorrBlkCount;
	uint32_t fecUncorrBlkCount;
} READ_LNK_VAR;

/* Structure for MB Command REG_LOGIN (19) */
/* Structure for MB Command REG_LOGIN64 (0x93) */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t rsvd1;
	uint16_t rpi;
	uint32_t rsvd2:8;
	uint32_t did:24;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t rpi;
	uint16_t rsvd1;
	uint32_t did:24;
	uint32_t rsvd2:8;
#endif

	union {
		struct ulp_bde sp;
		struct ulp_bde64 sp64;
	} un;

#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t rsvd6;
	uint16_t vpi;
#else /* __LITTLE_ENDIAN_BITFIELD */
	uint16_t vpi;
	uint16_t rsvd6;
#endif

} REG_LOGIN_VAR;

/* Word 30 contents for REG_LOGIN */
typedef union {
	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		uint16_t rsvd1:12;
		uint16_t wd30_class:4;
		uint16_t xri;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
		uint16_t xri;
		uint16_t wd30_class:4;
		uint16_t rsvd1:12;
#endif
	} f;
	uint32_t word;
} REG_WD30;

/* Structure for MB Command UNREG_LOGIN (20) */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t rsvd1;
	uint16_t rpi;
	uint32_t rsvd2;
	uint32_t rsvd3;
	uint32_t rsvd4;
	uint32_t rsvd5;
	uint16_t rsvd6;
	uint16_t vpi;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t rpi;
	uint16_t rsvd1;
	uint32_t rsvd2;
	uint32_t rsvd3;
	uint32_t rsvd4;
	uint32_t rsvd5;
	uint16_t vpi;
	uint16_t rsvd6;
#endif
} UNREG_LOGIN_VAR;

/* Structure for MB Command REG_VPI (0x96) */
typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd1;
	uint32_t rsvd2:7;
	uint32_t upd:1;
	uint32_t sid:24;
	uint32_t wwn[2];
	uint32_t rsvd5;
	uint16_t vfi;
	uint16_t vpi;
#else	/*  __LITTLE_ENDIAN */
	uint32_t rsvd1;
	uint32_t sid:24;
	uint32_t upd:1;
	uint32_t rsvd2:7;
	uint32_t wwn[2];
	uint32_t rsvd5;
	uint16_t vpi;
	uint16_t vfi;
#endif
} REG_VPI_VAR;

/* Structure for MB Command UNREG_VPI (0x97) */
typedef struct {
	uint32_t rsvd1;
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t rsvd2;
	uint16_t sli4_vpi;
#else	/*  __LITTLE_ENDIAN */
	uint16_t sli4_vpi;
	uint16_t rsvd2;
#endif
	uint32_t rsvd3;
	uint32_t rsvd4;
	uint32_t rsvd5;
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t rsvd6;
	uint16_t vpi;
#else	/*  __LITTLE_ENDIAN */
	uint16_t vpi;
	uint16_t rsvd6;
#endif
} UNREG_VPI_VAR;

/* Structure for MB Command UNREG_D_ID (0x23) */

typedef struct {
	uint32_t did;
	uint32_t rsvd2;
	uint32_t rsvd3;
	uint32_t rsvd4;
	uint32_t rsvd5;
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t rsvd6;
	uint16_t vpi;
#else
	uint16_t vpi;
	uint16_t rsvd6;
#endif
} UNREG_D_ID_VAR;

/* Structure for MB Command READ_TOPOLOGY (0x95) */
struct lpfc_mbx_read_top {
	uint32_t eventTag;	/* Event tag */
	uint32_t word2;
#define lpfc_mbx_read_top_fa_SHIFT		12
#define lpfc_mbx_read_top_fa_MASK		0x00000001
#define lpfc_mbx_read_top_fa_WORD		word2
#define lpfc_mbx_read_top_mm_SHIFT		11
#define lpfc_mbx_read_top_mm_MASK		0x00000001
#define lpfc_mbx_read_top_mm_WORD		word2
#define lpfc_mbx_read_top_pb_SHIFT		9
#define lpfc_mbx_read_top_pb_MASK		0X00000001
#define lpfc_mbx_read_top_pb_WORD		word2
#define lpfc_mbx_read_top_il_SHIFT		8
#define lpfc_mbx_read_top_il_MASK		0x00000001
#define lpfc_mbx_read_top_il_WORD		word2
#define lpfc_mbx_read_top_att_type_SHIFT	0
#define lpfc_mbx_read_top_att_type_MASK		0x000000FF
#define lpfc_mbx_read_top_att_type_WORD		word2
#define LPFC_ATT_RESERVED    0x00	/* Reserved - attType */
#define LPFC_ATT_LINK_UP     0x01	/* Link is up */
#define LPFC_ATT_LINK_DOWN   0x02	/* Link is down */
	uint32_t word3;
#define lpfc_mbx_read_top_alpa_granted_SHIFT	24
#define lpfc_mbx_read_top_alpa_granted_MASK	0x000000FF
#define lpfc_mbx_read_top_alpa_granted_WORD	word3
#define lpfc_mbx_read_top_lip_alps_SHIFT	16
#define lpfc_mbx_read_top_lip_alps_MASK		0x000000FF
#define lpfc_mbx_read_top_lip_alps_WORD		word3
#define lpfc_mbx_read_top_lip_type_SHIFT	8
#define lpfc_mbx_read_top_lip_type_MASK		0x000000FF
#define lpfc_mbx_read_top_lip_type_WORD		word3
#define lpfc_mbx_read_top_topology_SHIFT	0
#define lpfc_mbx_read_top_topology_MASK		0x000000FF
#define lpfc_mbx_read_top_topology_WORD		word3
#define LPFC_TOPOLOGY_PT_PT 0x01	/* Topology is pt-pt / pt-fabric */
#define LPFC_TOPOLOGY_LOOP  0x02	/* Topology is FC-AL */
#define LPFC_TOPOLOGY_MM    0x05	/* maint mode zephtr to menlo */
	/* store the LILP AL_PA position map into */
	struct ulp_bde64 lilpBde64;
#define LPFC_ALPA_MAP_SIZE	128
	uint32_t word7;
#define lpfc_mbx_read_top_ld_lu_SHIFT		31
#define lpfc_mbx_read_top_ld_lu_MASK		0x00000001
#define lpfc_mbx_read_top_ld_lu_WORD		word7
#define lpfc_mbx_read_top_ld_tf_SHIFT		30
#define lpfc_mbx_read_top_ld_tf_MASK		0x00000001
#define lpfc_mbx_read_top_ld_tf_WORD		word7
#define lpfc_mbx_read_top_ld_link_spd_SHIFT	8
#define lpfc_mbx_read_top_ld_link_spd_MASK	0x000000FF
#define lpfc_mbx_read_top_ld_link_spd_WORD	word7
#define lpfc_mbx_read_top_ld_nl_port_SHIFT	4
#define lpfc_mbx_read_top_ld_nl_port_MASK	0x0000000F
#define lpfc_mbx_read_top_ld_nl_port_WORD	word7
#define lpfc_mbx_read_top_ld_tx_SHIFT		2
#define lpfc_mbx_read_top_ld_tx_MASK		0x00000003
#define lpfc_mbx_read_top_ld_tx_WORD		word7
#define lpfc_mbx_read_top_ld_rx_SHIFT		0
#define lpfc_mbx_read_top_ld_rx_MASK		0x00000003
#define lpfc_mbx_read_top_ld_rx_WORD		word7
	uint32_t word8;
#define lpfc_mbx_read_top_lu_SHIFT		31
#define lpfc_mbx_read_top_lu_MASK		0x00000001
#define lpfc_mbx_read_top_lu_WORD		word8
#define lpfc_mbx_read_top_tf_SHIFT		30
#define lpfc_mbx_read_top_tf_MASK		0x00000001
#define lpfc_mbx_read_top_tf_WORD		word8
#define lpfc_mbx_read_top_link_spd_SHIFT	8
#define lpfc_mbx_read_top_link_spd_MASK		0x000000FF
#define lpfc_mbx_read_top_link_spd_WORD		word8
#define lpfc_mbx_read_top_nl_port_SHIFT		4
#define lpfc_mbx_read_top_nl_port_MASK		0x0000000F
#define lpfc_mbx_read_top_nl_port_WORD		word8
#define lpfc_mbx_read_top_tx_SHIFT		2
#define lpfc_mbx_read_top_tx_MASK		0x00000003
#define lpfc_mbx_read_top_tx_WORD		word8
#define lpfc_mbx_read_top_rx_SHIFT		0
#define lpfc_mbx_read_top_rx_MASK		0x00000003
#define lpfc_mbx_read_top_rx_WORD		word8
#define LPFC_LINK_SPEED_UNKNOWN	0x0
#define LPFC_LINK_SPEED_1GHZ	0x04
#define LPFC_LINK_SPEED_2GHZ	0x08
#define LPFC_LINK_SPEED_4GHZ	0x10
#define LPFC_LINK_SPEED_8GHZ	0x20
#define LPFC_LINK_SPEED_10GHZ	0x40
#define LPFC_LINK_SPEED_16GHZ	0x80
#define LPFC_LINK_SPEED_32GHZ	0x90
};

/* Structure for MB Command CLEAR_LA (22) */

typedef struct {
	uint32_t eventTag;	/* Event tag */
	uint32_t rsvd1;
} CLEAR_LA_VAR;

/* Structure for MB Command DUMP */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd:25;
	uint32_t ra:1;
	uint32_t co:1;
	uint32_t cv:1;
	uint32_t type:4;
	uint32_t entry_index:16;
	uint32_t region_id:16;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t type:4;
	uint32_t cv:1;
	uint32_t co:1;
	uint32_t ra:1;
	uint32_t rsvd:25;
	uint32_t region_id:16;
	uint32_t entry_index:16;
#endif

	uint32_t sli4_length;
	uint32_t word_cnt;
	uint32_t resp_offset;
} DUMP_VAR;

#define  DMP_MEM_REG             0x1
#define  DMP_NV_PARAMS           0x2
#define  DMP_LMSD                0x3 /* Link Module Serial Data */
#define  DMP_WELL_KNOWN          0x4

#define  DMP_REGION_VPD          0xe
#define  DMP_VPD_SIZE            0x400  /* maximum amount of VPD */
#define  DMP_RSP_OFFSET          0x14   /* word 5 contains first word of rsp */
#define  DMP_RSP_SIZE            0x6C   /* maximum of 27 words of rsp data */

#define  DMP_REGION_VPORT	 0x16   /* VPort info region */
#define  DMP_VPORT_REGION_SIZE	 0x200
#define  DMP_MBOX_OFFSET_WORD	 0x5

#define  DMP_REGION_23		 0x17   /* fcoe param  and port state region */
#define  DMP_RGN23_SIZE		 0x400

#define  WAKE_UP_PARMS_REGION_ID    4
#define  WAKE_UP_PARMS_WORD_SIZE   15

struct vport_rec {
	uint8_t wwpn[8];
	uint8_t wwnn[8];
};

#define VPORT_INFO_SIG 0x32324752
#define VPORT_INFO_REV_MASK 0xff
#define VPORT_INFO_REV 0x1
#define MAX_STATIC_VPORT_COUNT 16
struct static_vport_info {
	uint32_t		signature;
	uint32_t		rev;
	struct vport_rec	vport_list[MAX_STATIC_VPORT_COUNT];
	uint32_t		resvd[66];
};

/* Option rom version structure */
struct prog_id {
#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t  type;
	uint8_t  id;
	uint32_t ver:4;  /* Major Version */
	uint32_t rev:4;  /* Revision */
	uint32_t lev:2;  /* Level */
	uint32_t dist:2; /* Dist Type */
	uint32_t num:4;  /* number after dist type */
#else /*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t num:4;  /* number after dist type */
	uint32_t dist:2; /* Dist Type */
	uint32_t lev:2;  /* Level */
	uint32_t rev:4;  /* Revision */
	uint32_t ver:4;  /* Major Version */
	uint8_t  id;
	uint8_t  type;
#endif
};

/* Structure for MB Command UPDATE_CFG (0x1B) */

struct update_cfg_var {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd2:16;
	uint32_t type:8;
	uint32_t rsvd:1;
	uint32_t ra:1;
	uint32_t co:1;
	uint32_t cv:1;
	uint32_t req:4;
	uint32_t entry_length:16;
	uint32_t region_id:16;
#else  /*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t req:4;
	uint32_t cv:1;
	uint32_t co:1;
	uint32_t ra:1;
	uint32_t rsvd:1;
	uint32_t type:8;
	uint32_t rsvd2:16;
	uint32_t region_id:16;
	uint32_t entry_length:16;
#endif

	uint32_t resp_info;
	uint32_t byte_cnt;
	uint32_t data_offset;
};

struct hbq_mask {
#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t tmatch;
	uint8_t tmask;
	uint8_t rctlmatch;
	uint8_t rctlmask;
#else	/*  __LITTLE_ENDIAN */
	uint8_t rctlmask;
	uint8_t rctlmatch;
	uint8_t tmask;
	uint8_t tmatch;
#endif
};


/* Structure for MB Command CONFIG_HBQ (7c) */

struct config_hbq_var {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd1      :7;
	uint32_t recvNotify :1;     /* Receive Notification */
	uint32_t numMask    :8;     /* # Mask Entries       */
	uint32_t profile    :8;     /* Selection Profile    */
	uint32_t rsvd2      :8;
#else	/*  __LITTLE_ENDIAN */
	uint32_t rsvd2      :8;
	uint32_t profile    :8;     /* Selection Profile    */
	uint32_t numMask    :8;     /* # Mask Entries       */
	uint32_t recvNotify :1;     /* Receive Notification */
	uint32_t rsvd1      :7;
#endif

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t hbqId      :16;
	uint32_t rsvd3      :12;
	uint32_t ringMask   :4;
#else	/*  __LITTLE_ENDIAN */
	uint32_t ringMask   :4;
	uint32_t rsvd3      :12;
	uint32_t hbqId      :16;
#endif

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t entry_count :16;
	uint32_t rsvd4        :8;
	uint32_t headerLen    :8;
#else	/*  __LITTLE_ENDIAN */
	uint32_t headerLen    :8;
	uint32_t rsvd4        :8;
	uint32_t entry_count :16;
#endif

	uint32_t hbqaddrLow;
	uint32_t hbqaddrHigh;

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd5      :31;
	uint32_t logEntry   :1;
#else	/*  __LITTLE_ENDIAN */
	uint32_t logEntry   :1;
	uint32_t rsvd5      :31;
#endif

	uint32_t rsvd6;    /* w7 */
	uint32_t rsvd7;    /* w8 */
	uint32_t rsvd8;    /* w9 */

	struct hbq_mask hbqMasks[6];


	union {
		uint32_t allprofiles[12];

		struct {
			#ifdef __BIG_ENDIAN_BITFIELD
				uint32_t	seqlenoff	:16;
				uint32_t	maxlen		:16;
			#else	/*  __LITTLE_ENDIAN */
				uint32_t	maxlen		:16;
				uint32_t	seqlenoff	:16;
			#endif
			#ifdef __BIG_ENDIAN_BITFIELD
				uint32_t	rsvd1		:28;
				uint32_t	seqlenbcnt	:4;
			#else	/*  __LITTLE_ENDIAN */
				uint32_t	seqlenbcnt	:4;
				uint32_t	rsvd1		:28;
			#endif
			uint32_t rsvd[10];
		} profile2;

		struct {
			#ifdef __BIG_ENDIAN_BITFIELD
				uint32_t	seqlenoff	:16;
				uint32_t	maxlen		:16;
			#else	/*  __LITTLE_ENDIAN */
				uint32_t	maxlen		:16;
				uint32_t	seqlenoff	:16;
			#endif
			#ifdef __BIG_ENDIAN_BITFIELD
				uint32_t	cmdcodeoff	:28;
				uint32_t	rsvd1		:12;
				uint32_t	seqlenbcnt	:4;
			#else	/*  __LITTLE_ENDIAN */
				uint32_t	seqlenbcnt	:4;
				uint32_t	rsvd1		:12;
				uint32_t	cmdcodeoff	:28;
			#endif
			uint32_t cmdmatch[8];

			uint32_t rsvd[2];
		} profile3;

		struct {
			#ifdef __BIG_ENDIAN_BITFIELD
				uint32_t	seqlenoff	:16;
				uint32_t	maxlen		:16;
			#else	/*  __LITTLE_ENDIAN */
				uint32_t	maxlen		:16;
				uint32_t	seqlenoff	:16;
			#endif
			#ifdef __BIG_ENDIAN_BITFIELD
				uint32_t	cmdcodeoff	:28;
				uint32_t	rsvd1		:12;
				uint32_t	seqlenbcnt	:4;
			#else	/*  __LITTLE_ENDIAN */
				uint32_t	seqlenbcnt	:4;
				uint32_t	rsvd1		:12;
				uint32_t	cmdcodeoff	:28;
			#endif
			uint32_t cmdmatch[8];

			uint32_t rsvd[2];
		} profile5;

	} profiles;

};



/* Structure for MB Command CONFIG_PORT (0x88) */
typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t cBE       :  1;
	uint32_t cET       :  1;
	uint32_t cHpcb     :  1;
	uint32_t cMA       :  1;
	uint32_t sli_mode  :  4;
	uint32_t pcbLen    : 24;       /* bit 23:0  of memory based port
					* config block */
#else	/*  __LITTLE_ENDIAN */
	uint32_t pcbLen    : 24;       /* bit 23:0  of memory based port
					* config block */
	uint32_t sli_mode  :  4;
	uint32_t cMA       :  1;
	uint32_t cHpcb     :  1;
	uint32_t cET       :  1;
	uint32_t cBE       :  1;
#endif

	uint32_t pcbLow;       /* bit 31:0  of memory based port config block */
	uint32_t pcbHigh;      /* bit 63:32 of memory based port config block */
	uint32_t hbainit[5];
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t hps	   :  1; /* bit 31 word9 Host Pointer in slim */
	uint32_t rsvd	   : 31; /* least significant 31 bits of word 9 */
#else   /*  __LITTLE_ENDIAN */
	uint32_t rsvd      : 31; /* least significant 31 bits of word 9 */
	uint32_t hps	   :  1; /* bit 31 word9 Host Pointer in slim */
#endif

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd1     : 19;  /* Reserved                             */
	uint32_t cdss      :  1;  /* Configure Data Security SLI          */
	uint32_t casabt    :  1;  /* Configure async abts status notice   */
	uint32_t rsvd2     :  2;  /* Reserved                             */
	uint32_t cbg       :  1;  /* Configure BlockGuard                 */
	uint32_t cmv       :  1;  /* Configure Max VPIs                   */
	uint32_t ccrp      :  1;  /* Config Command Ring Polling          */
	uint32_t csah      :  1;  /* Configure Synchronous Abort Handling */
	uint32_t chbs      :  1;  /* Cofigure Host Backing store          */
	uint32_t cinb      :  1;  /* Enable Interrupt Notification Block  */
	uint32_t cerbm	   :  1;  /* Configure Enhanced Receive Buf Mgmt  */
	uint32_t cmx	   :  1;  /* Configure Max XRIs                   */
	uint32_t cmr	   :  1;  /* Configure Max RPIs                   */
#else	/*  __LITTLE_ENDIAN */
	uint32_t cmr	   :  1;  /* Configure Max RPIs                   */
	uint32_t cmx	   :  1;  /* Configure Max XRIs                   */
	uint32_t cerbm	   :  1;  /* Configure Enhanced Receive Buf Mgmt  */
	uint32_t cinb      :  1;  /* Enable Interrupt Notification Block  */
	uint32_t chbs      :  1;  /* Cofigure Host Backing store          */
	uint32_t csah      :  1;  /* Configure Synchronous Abort Handling */
	uint32_t ccrp      :  1;  /* Config Command Ring Polling          */
	uint32_t cmv	   :  1;  /* Configure Max VPIs                   */
	uint32_t cbg       :  1;  /* Configure BlockGuard                 */
	uint32_t rsvd2     :  2;  /* Reserved                             */
	uint32_t casabt    :  1;  /* Configure async abts status notice   */
	uint32_t cdss      :  1;  /* Configure Data Security SLI          */
	uint32_t rsvd1     : 19;  /* Reserved                             */
#endif
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd3     : 19;  /* Reserved                             */
	uint32_t gdss      :  1;  /* Configure Data Security SLI          */
	uint32_t gasabt    :  1;  /* Grant async abts status notice       */
	uint32_t rsvd4     :  2;  /* Reserved                             */
	uint32_t gbg       :  1;  /* Grant BlockGuard                     */
	uint32_t gmv	   :  1;  /* Grant Max VPIs                       */
	uint32_t gcrp	   :  1;  /* Grant Command Ring Polling           */
	uint32_t gsah	   :  1;  /* Grant Synchronous Abort Handling     */
	uint32_t ghbs	   :  1;  /* Grant Host Backing Store             */
	uint32_t ginb	   :  1;  /* Grant Interrupt Notification Block   */
	uint32_t gerbm	   :  1;  /* Grant ERBM Request                   */
	uint32_t gmx	   :  1;  /* Grant Max XRIs                       */
	uint32_t gmr	   :  1;  /* Grant Max RPIs                       */
#else	/*  __LITTLE_ENDIAN */
	uint32_t gmr	   :  1;  /* Grant Max RPIs                       */
	uint32_t gmx	   :  1;  /* Grant Max XRIs                       */
	uint32_t gerbm	   :  1;  /* Grant ERBM Request                   */
	uint32_t ginb	   :  1;  /* Grant Interrupt Notification Block   */
	uint32_t ghbs	   :  1;  /* Grant Host Backing Store             */
	uint32_t gsah	   :  1;  /* Grant Synchronous Abort Handling     */
	uint32_t gcrp	   :  1;  /* Grant Command Ring Polling           */
	uint32_t gmv	   :  1;  /* Grant Max VPIs                       */
	uint32_t gbg       :  1;  /* Grant BlockGuard                     */
	uint32_t rsvd4     :  2;  /* Reserved                             */
	uint32_t gasabt    :  1;  /* Grant async abts status notice       */
	uint32_t gdss      :  1;  /* Configure Data Security SLI          */
	uint32_t rsvd3     : 19;  /* Reserved                             */
#endif

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t max_rpi   : 16;  /* Max RPIs Port should configure       */
	uint32_t max_xri   : 16;  /* Max XRIs Port should configure       */
#else	/*  __LITTLE_ENDIAN */
	uint32_t max_xri   : 16;  /* Max XRIs Port should configure       */
	uint32_t max_rpi   : 16;  /* Max RPIs Port should configure       */
#endif

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t max_hbq   : 16;  /* Max HBQs Host expect to configure    */
	uint32_t rsvd5     : 16;  /* Max HBQs Host expect to configure    */
#else	/*  __LITTLE_ENDIAN */
	uint32_t rsvd5     : 16;  /* Max HBQs Host expect to configure    */
	uint32_t max_hbq   : 16;  /* Max HBQs Host expect to configure    */
#endif

	uint32_t rsvd6;           /* Reserved                             */

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t fips_rev   : 3;   /* FIPS Spec Revision                   */
	uint32_t fips_level : 4;   /* FIPS Level                           */
	uint32_t sec_err    : 9;   /* security crypto error                */
	uint32_t max_vpi    : 16;  /* Max number of virt N-Ports           */
#else	/*  __LITTLE_ENDIAN */
	uint32_t max_vpi    : 16;  /* Max number of virt N-Ports           */
	uint32_t sec_err    : 9;   /* security crypto error                */
	uint32_t fips_level : 4;   /* FIPS Level                           */
	uint32_t fips_rev   : 3;   /* FIPS Spec Revision                   */
#endif

} CONFIG_PORT_VAR;

/* Structure for MB Command CONFIG_MSI (0x30) */
struct config_msi_var {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dfltMsgNum:8;	/* Default message number            */
	uint32_t rsvd1:11;	/* Reserved                          */
	uint32_t NID:5;		/* Number of secondary attention IDs */
	uint32_t rsvd2:5;	/* Reserved                          */
	uint32_t dfltPresent:1;	/* Default message number present    */
	uint32_t addFlag:1;	/* Add association flag              */
	uint32_t reportFlag:1;	/* Report association flag           */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t reportFlag:1;	/* Report association flag           */
	uint32_t addFlag:1;	/* Add association flag              */
	uint32_t dfltPresent:1;	/* Default message number present    */
	uint32_t rsvd2:5;	/* Reserved                          */
	uint32_t NID:5;		/* Number of secondary attention IDs */
	uint32_t rsvd1:11;	/* Reserved                          */
	uint32_t dfltMsgNum:8;	/* Default message number            */
#endif
	uint32_t attentionConditions[2];
	uint8_t  attentionId[16];
	uint8_t  messageNumberByHA[64];
	uint8_t  messageNumberByID[16];
	uint32_t autoClearHA[2];
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd3:16;
	uint32_t autoClearID:16;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t autoClearID:16;
	uint32_t rsvd3:16;
#endif
	uint32_t rsvd4;
};

/* SLI-2 Port Control Block */

/* SLIM POINTER */
#define SLIMOFF 0x30		/* WORD */

typedef struct _SLI2_RDSC {
	uint32_t cmdEntries;
	uint32_t cmdAddrLow;
	uint32_t cmdAddrHigh;

	uint32_t rspEntries;
	uint32_t rspAddrLow;
	uint32_t rspAddrHigh;
} SLI2_RDSC;

typedef struct _PCB {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t type:8;
#define TYPE_NATIVE_SLI2       0x01
	uint32_t feature:8;
#define FEATURE_INITIAL_SLI2   0x01
	uint32_t rsvd:12;
	uint32_t maxRing:4;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t maxRing:4;
	uint32_t rsvd:12;
	uint32_t feature:8;
#define FEATURE_INITIAL_SLI2   0x01
	uint32_t type:8;
#define TYPE_NATIVE_SLI2       0x01
#endif

	uint32_t mailBoxSize;
	uint32_t mbAddrLow;
	uint32_t mbAddrHigh;

	uint32_t hgpAddrLow;
	uint32_t hgpAddrHigh;

	uint32_t pgpAddrLow;
	uint32_t pgpAddrHigh;
	SLI2_RDSC rdsc[MAX_SLI3_RINGS];
} PCB_t;

/* NEW_FEATURE */
typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd0:27;
	uint32_t discardFarp:1;
	uint32_t IPEnable:1;
	uint32_t nodeName:1;
	uint32_t portName:1;
	uint32_t filterEnable:1;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t filterEnable:1;
	uint32_t portName:1;
	uint32_t nodeName:1;
	uint32_t IPEnable:1;
	uint32_t discardFarp:1;
	uint32_t rsvd:27;
#endif

	uint8_t portname[8];	/* Used to be struct lpfc_name */
	uint8_t nodename[8];
	uint32_t rsvd1;
	uint32_t rsvd2;
	uint32_t rsvd3;
	uint32_t IPAddress;
} CONFIG_FARP_VAR;

/* Structure for MB Command MBX_ASYNCEVT_ENABLE (0x33) */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rsvd:30;
	uint32_t ring:2;	/* Ring for ASYNC_EVENT iocb Bits 0-1*/
#else /*  __LITTLE_ENDIAN */
	uint32_t ring:2;	/* Ring for ASYNC_EVENT iocb Bits 0-1*/
	uint32_t rsvd:30;
#endif
} ASYNCEVT_ENABLE_VAR;

/* Union of all Mailbox Command types */
#define MAILBOX_CMD_WSIZE	32
#define MAILBOX_CMD_SIZE	(MAILBOX_CMD_WSIZE * sizeof(uint32_t))
/* ext_wsize times 4 bytes should not be greater than max xmit size */
#define MAILBOX_EXT_WSIZE	512
#define MAILBOX_EXT_SIZE	(MAILBOX_EXT_WSIZE * sizeof(uint32_t))
#define MAILBOX_HBA_EXT_OFFSET  0x100
/* max mbox xmit size is a page size for sysfs IO operations */
#define MAILBOX_SYSFS_MAX	4096

typedef union {
	uint32_t varWords[MAILBOX_CMD_WSIZE - 1]; /* first word is type/
						    * feature/max ring number
						    */
	LOAD_SM_VAR varLdSM;		/* cmd =  1 (LOAD_SM)        */
	READ_NV_VAR varRDnvp;		/* cmd =  2 (READ_NVPARMS)   */
	WRITE_NV_VAR varWTnvp;		/* cmd =  3 (WRITE_NVPARMS)  */
	BIU_DIAG_VAR varBIUdiag;	/* cmd =  4 (RUN_BIU_DIAG)   */
	INIT_LINK_VAR varInitLnk;	/* cmd =  5 (INIT_LINK)      */
	DOWN_LINK_VAR varDwnLnk;	/* cmd =  6 (DOWN_LINK)      */
	CONFIG_LINK varCfgLnk;		/* cmd =  7 (CONFIG_LINK)    */
	PART_SLIM_VAR varSlim;		/* cmd =  8 (PART_SLIM)      */
	CONFIG_RING_VAR varCfgRing;	/* cmd =  9 (CONFIG_RING)    */
	RESET_RING_VAR varRstRing;	/* cmd = 10 (RESET_RING)     */
	READ_CONFIG_VAR varRdConfig;	/* cmd = 11 (READ_CONFIG)    */
	READ_RCONF_VAR varRdRConfig;	/* cmd = 12 (READ_RCONFIG)   */
	READ_SPARM_VAR varRdSparm;	/* cmd = 13 (READ_SPARM(64)) */
	READ_STATUS_VAR varRdStatus;	/* cmd = 14 (READ_STATUS)    */
	READ_RPI_VAR varRdRPI;		/* cmd = 15 (READ_RPI(64))   */
	READ_XRI_VAR varRdXRI;		/* cmd = 16 (READ_XRI)       */
	READ_REV_VAR varRdRev;		/* cmd = 17 (READ_REV)       */
	READ_LNK_VAR varRdLnk;		/* cmd = 18 (READ_LNK_STAT)  */
	REG_LOGIN_VAR varRegLogin;	/* cmd = 19 (REG_LOGIN(64))  */
	UNREG_LOGIN_VAR varUnregLogin;	/* cmd = 20 (UNREG_LOGIN)    */
	CLEAR_LA_VAR varClearLA;	/* cmd = 22 (CLEAR_LA)       */
	DUMP_VAR varDmp;		/* Warm Start DUMP mbx cmd   */
	UNREG_D_ID_VAR varUnregDID;	/* cmd = 0x23 (UNREG_D_ID)   */
	CONFIG_FARP_VAR varCfgFarp;	/* cmd = 0x25 (CONFIG_FARP)
					 * NEW_FEATURE
					 */
	struct config_hbq_var varCfgHbq;/* cmd = 0x7c (CONFIG_HBQ)  */
	struct update_cfg_var varUpdateCfg; /* cmd = 0x1B (UPDATE_CFG)*/
	CONFIG_PORT_VAR varCfgPort;	/* cmd = 0x88 (CONFIG_PORT)  */
	struct lpfc_mbx_read_top varReadTop; /* cmd = 0x95 (READ_TOPOLOGY) */
	REG_VPI_VAR varRegVpi;		/* cmd = 0x96 (REG_VPI) */
	UNREG_VPI_VAR varUnregVpi;	/* cmd = 0x97 (UNREG_VPI) */
	ASYNCEVT_ENABLE_VAR varCfgAsyncEvent; /*cmd = x33 (CONFIG_ASYNC) */
	struct READ_EVENT_LOG_VAR varRdEventLog;	/* cmd = 0x38
							 * (READ_EVENT_LOG)
							 */
	struct config_msi_var varCfgMSI;/* cmd = x30 (CONFIG_MSI)     */
} MAILVARIANTS;

/*
 * SLI-2 specific structures
 */

struct lpfc_hgp {
	__le32 cmdPutInx;
	__le32 rspGetInx;
};

struct lpfc_pgp {
	__le32 cmdGetInx;
	__le32 rspPutInx;
};

struct sli2_desc {
	uint32_t unused1[16];
	struct lpfc_hgp host[MAX_SLI3_RINGS];
	struct lpfc_pgp port[MAX_SLI3_RINGS];
};

struct sli3_desc {
	struct lpfc_hgp host[MAX_SLI3_RINGS];
	uint32_t reserved[8];
	uint32_t hbq_put[16];
};

struct sli3_pgp {
	struct lpfc_pgp port[MAX_SLI3_RINGS];
	uint32_t hbq_get[16];
};

union sli_var {
	struct sli2_desc	s2;
	struct sli3_desc	s3;
	struct sli3_pgp		s3_pgp;
};

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t mbxStatus;
	uint8_t mbxCommand;
	uint8_t mbxReserved:6;
	uint8_t mbxHc:1;
	uint8_t mbxOwner:1;	/* Low order bit first word */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint8_t mbxOwner:1;	/* Low order bit first word */
	uint8_t mbxHc:1;
	uint8_t mbxReserved:6;
	uint8_t mbxCommand;
	uint16_t mbxStatus;
#endif

	MAILVARIANTS un;
	union sli_var us;
} MAILBOX_t;

/*
 *    Begin Structure Definitions for IOCB Commands
 */

typedef struct {
#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t statAction;
	uint8_t statRsn;
	uint8_t statBaExp;
	uint8_t statLocalError;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint8_t statLocalError;
	uint8_t statBaExp;
	uint8_t statRsn;
	uint8_t statAction;
#endif
	/* statRsn  P/F_RJT reason codes */
#define RJT_BAD_D_ID       0x01	/* Invalid D_ID field */
#define RJT_BAD_S_ID       0x02	/* Invalid S_ID field */
#define RJT_UNAVAIL_TEMP   0x03	/* N_Port unavailable temp. */
#define RJT_UNAVAIL_PERM   0x04	/* N_Port unavailable perm. */
#define RJT_UNSUP_CLASS    0x05	/* Class not supported */
#define RJT_DELIM_ERR      0x06	/* Delimiter usage error */
#define RJT_UNSUP_TYPE     0x07	/* Type not supported */
#define RJT_BAD_CONTROL    0x08	/* Invalid link conrtol */
#define RJT_BAD_RCTL       0x09	/* R_CTL invalid */
#define RJT_BAD_FCTL       0x0A	/* F_CTL invalid */
#define RJT_BAD_OXID       0x0B	/* OX_ID invalid */
#define RJT_BAD_RXID       0x0C	/* RX_ID invalid */
#define RJT_BAD_SEQID      0x0D	/* SEQ_ID invalid */
#define RJT_BAD_DFCTL      0x0E	/* DF_CTL invalid */
#define RJT_BAD_SEQCNT     0x0F	/* SEQ_CNT invalid */
#define RJT_BAD_PARM       0x10	/* Param. field invalid */
#define RJT_XCHG_ERR       0x11	/* Exchange error */
#define RJT_PROT_ERR       0x12	/* Protocol error */
#define RJT_BAD_LENGTH     0x13	/* Invalid Length */
#define RJT_UNEXPECTED_ACK 0x14	/* Unexpected ACK */
#define RJT_LOGIN_REQUIRED 0x16	/* Login required */
#define RJT_TOO_MANY_SEQ   0x17	/* Excessive sequences */
#define RJT_XCHG_NOT_STRT  0x18	/* Exchange not started */
#define RJT_UNSUP_SEC_HDR  0x19	/* Security hdr not supported */
#define RJT_UNAVAIL_PATH   0x1A	/* Fabric Path not available */
#define RJT_VENDOR_UNIQUE  0xFF	/* Vendor unique error */

#define IOERR_SUCCESS                 0x00	/* statLocalError */
#define IOERR_MISSING_CONTINUE        0x01
#define IOERR_SEQUENCE_TIMEOUT        0x02
#define IOERR_INTERNAL_ERROR          0x03
#define IOERR_INVALID_RPI             0x04
#define IOERR_NO_XRI                  0x05
#define IOERR_ILLEGAL_COMMAND         0x06
#define IOERR_XCHG_DROPPED            0x07
#define IOERR_ILLEGAL_FIELD           0x08
#define IOERR_BAD_CONTINUE            0x09
#define IOERR_TOO_MANY_BUFFERS        0x0A
#define IOERR_RCV_BUFFER_WAITING      0x0B
#define IOERR_NO_CONNECTION           0x0C
#define IOERR_TX_DMA_FAILED           0x0D
#define IOERR_RX_DMA_FAILED           0x0E
#define IOERR_ILLEGAL_FRAME           0x0F
#define IOERR_EXTRA_DATA              0x10
#define IOERR_NO_RESOURCES            0x11
#define IOERR_RESERVED                0x12
#define IOERR_ILLEGAL_LENGTH          0x13
#define IOERR_UNSUPPORTED_FEATURE     0x14
#define IOERR_ABORT_IN_PROGRESS       0x15
#define IOERR_ABORT_REQUESTED         0x16
#define IOERR_RECEIVE_BUFFER_TIMEOUT  0x17
#define IOERR_LOOP_OPEN_FAILURE       0x18
#define IOERR_RING_RESET              0x19
#define IOERR_LINK_DOWN               0x1A
#define IOERR_CORRUPTED_DATA          0x1B
#define IOERR_CORRUPTED_RPI           0x1C
#define IOERR_OUT_OF_ORDER_DATA       0x1D
#define IOERR_OUT_OF_ORDER_ACK        0x1E
#define IOERR_DUP_FRAME               0x1F
#define IOERR_LINK_CONTROL_FRAME      0x20	/* ACK_N received */
#define IOERR_BAD_HOST_ADDRESS        0x21
#define IOERR_RCV_HDRBUF_WAITING      0x22
#define IOERR_MISSING_HDR_BUFFER      0x23
#define IOERR_MSEQ_CHAIN_CORRUPTED    0x24
#define IOERR_ABORTMULT_REQUESTED     0x25
#define IOERR_BUFFER_SHORTAGE         0x28
#define IOERR_DEFAULT                 0x29
#define IOERR_CNT                     0x2A
#define IOERR_SLER_FAILURE            0x46
#define IOERR_SLER_CMD_RCV_FAILURE    0x47
#define IOERR_SLER_REC_RJT_ERR        0x48
#define IOERR_SLER_REC_SRR_RETRY_ERR  0x49
#define IOERR_SLER_SRR_RJT_ERR        0x4A
#define IOERR_SLER_RRQ_RJT_ERR        0x4C
#define IOERR_SLER_RRQ_RETRY_ERR      0x4D
#define IOERR_SLER_ABTS_ERR           0x4E
#define IOERR_ELXSEC_KEY_UNWRAP_ERROR		0xF0
#define IOERR_ELXSEC_KEY_UNWRAP_COMPARE_ERROR	0xF1
#define IOERR_ELXSEC_CRYPTO_ERROR		0xF2
#define IOERR_ELXSEC_CRYPTO_COMPARE_ERROR	0xF3
#define IOERR_DRVR_MASK               0x100
#define IOERR_SLI_DOWN                0x101  /* ulpStatus  - Driver defined */
#define IOERR_SLI_BRESET              0x102
#define IOERR_SLI_ABORTED             0x103
#define IOERR_PARAM_MASK              0x1ff
} PARM_ERR;

typedef union {
	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		uint8_t Rctl;	/* R_CTL field */
		uint8_t Type;	/* TYPE field */
		uint8_t Dfctl;	/* DF_CTL field */
		uint8_t Fctl;	/* Bits 0-7 of IOCB word 5 */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
		uint8_t Fctl;	/* Bits 0-7 of IOCB word 5 */
		uint8_t Dfctl;	/* DF_CTL field */
		uint8_t Type;	/* TYPE field */
		uint8_t Rctl;	/* R_CTL field */
#endif

#define BC      0x02		/* Broadcast Received  - Fctl */
#define SI      0x04		/* Sequence Initiative */
#define LA      0x08		/* Ignore Link Attention state */
#define LS      0x80		/* Last Sequence */
	} hcsw;
	uint32_t reserved;
} WORD5;

/* IOCB Command template for a generic response */
typedef struct {
	uint32_t reserved[4];
	PARM_ERR perr;
} GENERIC_RSP;

/* IOCB Command template for XMIT / XMIT_BCAST / RCV_SEQUENCE / XMIT_ELS */
typedef struct {
	struct ulp_bde xrsqbde[2];
	uint32_t xrsqRo;	/* Starting Relative Offset */
	WORD5 w5;		/* Header control/status word */
} XR_SEQ_FIELDS;

/* IOCB Command template for ELS_REQUEST */
typedef struct {
	struct ulp_bde elsReq;
	struct ulp_bde elsRsp;

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t word4Rsvd:7;
	uint32_t fl:1;
	uint32_t myID:24;
	uint32_t word5Rsvd:8;
	uint32_t remoteID:24;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t myID:24;
	uint32_t fl:1;
	uint32_t word4Rsvd:7;
	uint32_t remoteID:24;
	uint32_t word5Rsvd:8;
#endif
} ELS_REQUEST;

/* IOCB Command template for RCV_ELS_REQ */
typedef struct {
	struct ulp_bde elsReq[2];
	uint32_t parmRo;

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t word5Rsvd:8;
	uint32_t remoteID:24;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t remoteID:24;
	uint32_t word5Rsvd:8;
#endif
} RCV_ELS_REQ;

/* IOCB Command template for ABORT / CLOSE_XRI */
typedef struct {
	uint32_t rsvd[3];
	uint32_t abortType;
#define ABORT_TYPE_ABTX  0x00000000
#define ABORT_TYPE_ABTS  0x00000001
	uint32_t parm;
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t abortContextTag; /* ulpContext from command to abort/close */
	uint16_t abortIoTag;	/* ulpIoTag from command to abort/close */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t abortIoTag;	/* ulpIoTag from command to abort/close */
	uint16_t abortContextTag; /* ulpContext from command to abort/close */
#endif
} AC_XRI;

/* IOCB Command template for ABORT_MXRI64 */
typedef struct {
	uint32_t rsvd[3];
	uint32_t abortType;
	uint32_t parm;
	uint32_t iotag32;
} A_MXRI64;

/* IOCB Command template for GET_RPI */
typedef struct {
	uint32_t rsvd[4];
	uint32_t parmRo;
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t word5Rsvd:8;
	uint32_t remoteID:24;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t remoteID:24;
	uint32_t word5Rsvd:8;
#endif
} GET_RPI;

/* IOCB Command template for all FCP Initiator commands */
typedef struct {
	struct ulp_bde fcpi_cmnd;	/* FCP_CMND payload descriptor */
	struct ulp_bde fcpi_rsp;	/* Rcv buffer */
	uint32_t fcpi_parm;
	uint32_t fcpi_XRdy;	/* transfer ready for IWRITE */
} FCPI_FIELDS;

/* IOCB Command template for all FCP Target commands */
typedef struct {
	struct ulp_bde fcpt_Buffer[2];	/* FCP_CMND payload descriptor */
	uint32_t fcpt_Offset;
	uint32_t fcpt_Length;	/* transfer ready for IWRITE */
} FCPT_FIELDS;

/* SLI-2 IOCB structure definitions */

/* IOCB Command template for 64 bit XMIT / XMIT_BCAST / XMIT_ELS */
typedef struct {
	ULP_BDL bdl;
	uint32_t xrsqRo;	/* Starting Relative Offset */
	WORD5 w5;		/* Header control/status word */
} XMT_SEQ_FIELDS64;

/* This word is remote ports D_ID for XMIT_ELS_RSP64 */
#define xmit_els_remoteID xrsqRo

/* IOCB Command template for 64 bit RCV_SEQUENCE64 */
typedef struct {
	struct ulp_bde64 rcvBde;
	uint32_t rsvd1;
	uint32_t xrsqRo;	/* Starting Relative Offset */
	WORD5 w5;		/* Header control/status word */
} RCV_SEQ_FIELDS64;

/* IOCB Command template for ELS_REQUEST64 */
typedef struct {
	ULP_BDL bdl;
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t word4Rsvd:7;
	uint32_t fl:1;
	uint32_t myID:24;
	uint32_t word5Rsvd:8;
	uint32_t remoteID:24;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t myID:24;
	uint32_t fl:1;
	uint32_t word4Rsvd:7;
	uint32_t remoteID:24;
	uint32_t word5Rsvd:8;
#endif
} ELS_REQUEST64;

/* IOCB Command template for GEN_REQUEST64 */
typedef struct {
	ULP_BDL bdl;
	uint32_t xrsqRo;	/* Starting Relative Offset */
	WORD5 w5;		/* Header control/status word */
} GEN_REQUEST64;

/* IOCB Command template for RCV_ELS_REQ64 */
typedef struct {
	struct ulp_bde64 elsReq;
	uint32_t rcvd1;
	uint32_t parmRo;

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t word5Rsvd:8;
	uint32_t remoteID:24;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t remoteID:24;
	uint32_t word5Rsvd:8;
#endif
} RCV_ELS_REQ64;

/* IOCB Command template for RCV_SEQ64 */
struct rcv_seq64 {
	struct ulp_bde64 elsReq;
	uint32_t hbq_1;
	uint32_t parmRo;
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t rctl:8;
	uint32_t type:8;
	uint32_t dfctl:8;
	uint32_t ls:1;
	uint32_t fs:1;
	uint32_t rsvd2:3;
	uint32_t si:1;
	uint32_t bc:1;
	uint32_t rsvd3:1;
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t rsvd3:1;
	uint32_t bc:1;
	uint32_t si:1;
	uint32_t rsvd2:3;
	uint32_t fs:1;
	uint32_t ls:1;
	uint32_t dfctl:8;
	uint32_t type:8;
	uint32_t rctl:8;
#endif
};

/* IOCB Command template for all 64 bit FCP Initiator commands */
typedef struct {
	ULP_BDL bdl;
	uint32_t fcpi_parm;
	uint32_t fcpi_XRdy;	/* transfer ready for IWRITE */
} FCPI_FIELDS64;

/* IOCB Command template for all 64 bit FCP Target commands */
typedef struct {
	ULP_BDL bdl;
	uint32_t fcpt_Offset;
	uint32_t fcpt_Length;	/* transfer ready for IWRITE */
} FCPT_FIELDS64;

/* IOCB Command template for Async Status iocb commands */
typedef struct {
	uint32_t rsvd[4];
	uint32_t param;
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t evt_code;		/* High order bits word 5 */
	uint16_t sub_ctxt_tag;		/* Low  order bits word 5 */
#else   /*  __LITTLE_ENDIAN_BITFIELD */
	uint16_t sub_ctxt_tag;		/* High order bits word 5 */
	uint16_t evt_code;		/* Low  order bits word 5 */
#endif
} ASYNCSTAT_FIELDS;
#define ASYNC_TEMP_WARN		0x100
#define ASYNC_TEMP_SAFE		0x101
#define ASYNC_STATUS_CN		0x102

/* IOCB Command template for CMD_IOCB_RCV_ELS64_CX (0xB7)
   or CMD_IOCB_RCV_SEQ64_CX (0xB5) */

struct rcv_sli3 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint16_t ox_id;
	uint16_t seq_cnt;

	uint16_t vpi;
	uint16_t word9Rsvd;
#else  /*  __LITTLE_ENDIAN */
	uint16_t seq_cnt;
	uint16_t ox_id;

	uint16_t word9Rsvd;
	uint16_t vpi;
#endif
	uint32_t word10Rsvd;
	uint32_t acc_len;      /* accumulated length */
	struct ulp_bde64 bde2;
};

/* Structure used for a single HBQ entry */
struct lpfc_hbq_entry {
	struct ulp_bde64 bde;
	uint32_t buffer_tag;
};

/* IOCB Command template for QUE_XRI64_CX (0xB3) command */
typedef struct {
	struct lpfc_hbq_entry   buff;
	uint32_t                rsvd;
	uint32_t		rsvd1;
} QUE_XRI64_CX_FIELDS;

struct que_xri64cx_ext_fields {
	uint32_t	iotag64_low;
	uint32_t	iotag64_high;
	uint32_t	ebde_count;
	uint32_t	rsvd;
	struct lpfc_hbq_entry	buff[5];
};

struct sli3_bg_fields {
	uint32_t filler[6];	/* word 8-13 in IOCB */
	uint32_t bghm;		/* word 14 - BlockGuard High Water Mark */
/* Bitfields for bgstat (BlockGuard Status - word 15 of IOCB) */
#define BGS_BIDIR_BG_PROF_MASK		0xff000000
#define BGS_BIDIR_BG_PROF_SHIFT		24
#define BGS_BIDIR_ERR_COND_FLAGS_MASK	0x003f0000
#define BGS_BIDIR_ERR_COND_SHIFT	16
#define BGS_BG_PROFILE_MASK		0x0000ff00
#define BGS_BG_PROFILE_SHIFT		8
#define BGS_INVALID_PROF_MASK		0x00000020
#define BGS_INVALID_PROF_SHIFT		5
#define BGS_UNINIT_DIF_BLOCK_MASK	0x00000010
#define BGS_UNINIT_DIF_BLOCK_SHIFT	4
#define BGS_HI_WATER_MARK_PRESENT_MASK	0x00000008
#define BGS_HI_WATER_MARK_PRESENT_SHIFT	3
#define BGS_REFTAG_ERR_MASK		0x00000004
#define BGS_REFTAG_ERR_SHIFT		2
#define BGS_APPTAG_ERR_MASK		0x00000002
#define BGS_APPTAG_ERR_SHIFT		1
#define BGS_GUARD_ERR_MASK		0x00000001
#define BGS_GUARD_ERR_SHIFT		0
	uint32_t bgstat;	/* word 15 - BlockGuard Status */
};

static inline uint32_t
lpfc_bgs_get_bidir_bg_prof(uint32_t bgstat)
{
	return (bgstat & BGS_BIDIR_BG_PROF_MASK) >>
				BGS_BIDIR_BG_PROF_SHIFT;
}

static inline uint32_t
lpfc_bgs_get_bidir_err_cond(uint32_t bgstat)
{
	return (bgstat & BGS_BIDIR_ERR_COND_FLAGS_MASK) >>
				BGS_BIDIR_ERR_COND_SHIFT;
}

static inline uint32_t
lpfc_bgs_get_bg_prof(uint32_t bgstat)
{
	return (bgstat & BGS_BG_PROFILE_MASK) >>
				BGS_BG_PROFILE_SHIFT;
}

static inline uint32_t
lpfc_bgs_get_invalid_prof(uint32_t bgstat)
{
	return (bgstat & BGS_INVALID_PROF_MASK) >>
				BGS_INVALID_PROF_SHIFT;
}

static inline uint32_t
lpfc_bgs_get_uninit_dif_block(uint32_t bgstat)
{
	return (bgstat & BGS_UNINIT_DIF_BLOCK_MASK) >>
				BGS_UNINIT_DIF_BLOCK_SHIFT;
}

static inline uint32_t
lpfc_bgs_get_hi_water_mark_present(uint32_t bgstat)
{
	return (bgstat & BGS_HI_WATER_MARK_PRESENT_MASK) >>
				BGS_HI_WATER_MARK_PRESENT_SHIFT;
}

static inline uint32_t
lpfc_bgs_get_reftag_err(uint32_t bgstat)
{
	return (bgstat & BGS_REFTAG_ERR_MASK) >>
				BGS_REFTAG_ERR_SHIFT;
}

static inline uint32_t
lpfc_bgs_get_apptag_err(uint32_t bgstat)
{
	return (bgstat & BGS_APPTAG_ERR_MASK) >>
				BGS_APPTAG_ERR_SHIFT;
}

static inline uint32_t
lpfc_bgs_get_guard_err(uint32_t bgstat)
{
	return (bgstat & BGS_GUARD_ERR_MASK) >>
				BGS_GUARD_ERR_SHIFT;
}

#define LPFC_EXT_DATA_BDE_COUNT 3
struct fcp_irw_ext {
	uint32_t	io_tag64_low;
	uint32_t	io_tag64_high;
#ifdef __BIG_ENDIAN_BITFIELD
	uint8_t		reserved1;
	uint8_t		reserved2;
	uint8_t		reserved3;
	uint8_t		ebde_count;
#else  /* __LITTLE_ENDIAN */
	uint8_t		ebde_count;
	uint8_t		reserved3;
	uint8_t		reserved2;
	uint8_t		reserved1;
#endif
	uint32_t	reserved4;
	struct ulp_bde64 rbde;		/* response bde */
	struct ulp_bde64 dbde[LPFC_EXT_DATA_BDE_COUNT];	/* data BDE or BPL */
	uint8_t icd[32];		/* immediate command data (32 bytes) */
};

typedef struct _IOCB {	/* IOCB structure */
	union {
		GENERIC_RSP grsp;	/* Generic response */
		XR_SEQ_FIELDS xrseq;	/* XMIT / BCAST / RCV_SEQUENCE cmd */
		struct ulp_bde cont[3];	/* up to 3 continuation bdes */
		RCV_ELS_REQ rcvels;	/* RCV_ELS_REQ template */
		AC_XRI acxri;	/* ABORT / CLOSE_XRI template */
		A_MXRI64 amxri;	/* abort multiple xri command overlay */
		GET_RPI getrpi;	/* GET_RPI template */
		FCPI_FIELDS fcpi;	/* FCP Initiator template */
		FCPT_FIELDS fcpt;	/* FCP target template */

		/* SLI-2 structures */

		struct ulp_bde64 cont64[2];  /* up to 2 64 bit continuation
					      * bde_64s */
		ELS_REQUEST64 elsreq64;	/* ELS_REQUEST template */
		GEN_REQUEST64 genreq64;	/* GEN_REQUEST template */
		RCV_ELS_REQ64 rcvels64;	/* RCV_ELS_REQ template */
		XMT_SEQ_FIELDS64 xseq64;	/* XMIT / BCAST cmd */
		FCPI_FIELDS64 fcpi64;	/* FCP 64 bit Initiator template */
		FCPT_FIELDS64 fcpt64;	/* FCP 64 bit target template */
		ASYNCSTAT_FIELDS asyncstat; /* async_status iocb */
		QUE_XRI64_CX_FIELDS quexri64cx; /* que_xri64_cx fields */
		struct rcv_seq64 rcvseq64;	/* RCV_SEQ64 and RCV_CONT64 */
		struct sli4_bls_rsp bls_rsp; /* UNSOL ABTS BLS_RSP params */
		uint32_t ulpWord[IOCB_WORD_SZ - 2];	/* generic 6 'words' */
	} un;
	union {
		struct {
#ifdef __BIG_ENDIAN_BITFIELD
			uint16_t ulpContext;	/* High order bits word 6 */
			uint16_t ulpIoTag;	/* Low  order bits word 6 */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
			uint16_t ulpIoTag;	/* Low  order bits word 6 */
			uint16_t ulpContext;	/* High order bits word 6 */
#endif
		} t1;
		struct {
#ifdef __BIG_ENDIAN_BITFIELD
			uint16_t ulpContext;	/* High order bits word 6 */
			uint16_t ulpIoTag1:2;	/* Low  order bits word 6 */
			uint16_t ulpIoTag0:14;	/* Low  order bits word 6 */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
			uint16_t ulpIoTag0:14;	/* Low  order bits word 6 */
			uint16_t ulpIoTag1:2;	/* Low  order bits word 6 */
			uint16_t ulpContext;	/* High order bits word 6 */
#endif
		} t2;
	} un1;
#define ulpContext un1.t1.ulpContext
#define ulpIoTag   un1.t1.ulpIoTag
#define ulpIoTag0  un1.t2.ulpIoTag0

#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t ulpTimeout:8;
	uint32_t ulpXS:1;
	uint32_t ulpFCP2Rcvy:1;
	uint32_t ulpPU:2;
	uint32_t ulpIr:1;
	uint32_t ulpClass:3;
	uint32_t ulpCommand:8;
	uint32_t ulpStatus:4;
	uint32_t ulpBdeCount:2;
	uint32_t ulpLe:1;
	uint32_t ulpOwner:1;	/* Low order bit word 7 */
#else	/*  __LITTLE_ENDIAN_BITFIELD */
	uint32_t ulpOwner:1;	/* Low order bit word 7 */
	uint32_t ulpLe:1;
	uint32_t ulpBdeCount:2;
	uint32_t ulpStatus:4;
	uint32_t ulpCommand:8;
	uint32_t ulpClass:3;
	uint32_t ulpIr:1;
	uint32_t ulpPU:2;
	uint32_t ulpFCP2Rcvy:1;
	uint32_t ulpXS:1;
	uint32_t ulpTimeout:8;
#endif

	union {
		struct rcv_sli3 rcvsli3; /* words 8 - 15 */

		/* words 8-31 used for que_xri_cx iocb */
		struct que_xri64cx_ext_fields que_xri64cx_ext_words;
		struct fcp_irw_ext fcp_ext;
		uint32_t sli3Words[24]; /* 96 extra bytes for SLI-3 */

		/* words 8-15 for BlockGuard */
		struct sli3_bg_fields sli3_bg;
	} unsli3;

#define ulpCt_h ulpXS
#define ulpCt_l ulpFCP2Rcvy

#define IOCB_FCP	   1	/* IOCB is used for FCP ELS cmds-ulpRsvByte */
#define IOCB_IP		   2	/* IOCB is used for IP ELS cmds */
#define PARM_UNUSED        0	/* PU field (Word 4) not used */
#define PARM_REL_OFF       1	/* PU field (Word 4) = R. O. */
#define PARM_READ_CHECK    2	/* PU field (Word 4) = Data Transfer Length */
#define PARM_NPIV_DID	   3
#define CLASS1             0	/* Class 1 */
#define CLASS2             1	/* Class 2 */
#define CLASS3             2	/* Class 3 */
#define CLASS_FCP_INTERMIX 7	/* FCP Data->Cls 1, all else->Cls 2 */

#define IOSTAT_SUCCESS         0x0	/* ulpStatus  - HBA defined */
#define IOSTAT_FCP_RSP_ERROR   0x1
#define IOSTAT_REMOTE_STOP     0x2
#define IOSTAT_LOCAL_REJECT    0x3
#define IOSTAT_NPORT_RJT       0x4
#define IOSTAT_FABRIC_RJT      0x5
#define IOSTAT_NPORT_BSY       0x6
#define IOSTAT_FABRIC_BSY      0x7
#define IOSTAT_INTERMED_RSP    0x8
#define IOSTAT_LS_RJT          0x9
#define IOSTAT_BA_RJT          0xA
#define IOSTAT_RSVD1           0xB
#define IOSTAT_RSVD2           0xC
#define IOSTAT_RSVD3           0xD
#define IOSTAT_RSVD4           0xE
#define IOSTAT_NEED_BUFFER     0xF
#define IOSTAT_DRIVER_REJECT   0x10   /* ulpStatus  - Driver defined */
#define IOSTAT_DEFAULT         0xF    /* Same as rsvd5 for now */
#define IOSTAT_CNT             0x11

} IOCB_t;


#define SLI1_SLIM_SIZE   (4 * 1024)

/* Up to 498 IOCBs will fit into 16k
 * 256 (MAILBOX_t) + 140 (PCB_t) + ( 32 (IOCB_t) * 498 ) = < 16384
 */
#define SLI2_SLIM_SIZE   (64 * 1024)

/* Maximum IOCBs that will fit in SLI2 slim */
#define MAX_SLI2_IOCB    498
#define MAX_SLIM_IOCB_SIZE (SLI2_SLIM_SIZE - \
			    (sizeof(MAILBOX_t) + sizeof(PCB_t) + \
			    sizeof(uint32_t) * MAILBOX_EXT_WSIZE))

/* HBQ entries are 4 words each = 4k */
#define LPFC_TOTAL_HBQ_SIZE (sizeof(struct lpfc_hbq_entry) *  \
			     lpfc_sli_hbq_count())

struct lpfc_sli2_slim {
	MAILBOX_t mbx;
	uint32_t  mbx_ext_words[MAILBOX_EXT_WSIZE];
	PCB_t pcb;
	IOCB_t IOCBs[MAX_SLIM_IOCB_SIZE];
};

/*
 * This function checks PCI device to allow special handling for LC HBAs.
 *
 * Parameters:
 * device : struct pci_dev 's device field
 *
 * return 1 => TRUE
 *        0 => FALSE
 */
static inline int
lpfc_is_LC_HBA(unsigned short device)
{
	if ((device == PCI_DEVICE_ID_TFLY) ||
	    (device == PCI_DEVICE_ID_PFLY) ||
	    (device == PCI_DEVICE_ID_LP101) ||
	    (device == PCI_DEVICE_ID_BMID) ||
	    (device == PCI_DEVICE_ID_BSMB) ||
	    (device == PCI_DEVICE_ID_ZMID) ||
	    (device == PCI_DEVICE_ID_ZSMB) ||
	    (device == PCI_DEVICE_ID_SAT_MID) ||
	    (device == PCI_DEVICE_ID_SAT_SMB) ||
	    (device == PCI_DEVICE_ID_RFLY))
		return 1;
	else
		return 0;
}

/*
 * Determine if an IOCB failed because of a link event or firmware reset.
 */

static inline int
lpfc_error_lost_link(IOCB_t *iocbp)
{
	return (iocbp->ulpStatus == IOSTAT_LOCAL_REJECT &&
		(iocbp->un.ulpWord[4] == IOERR_SLI_ABORTED ||
		 iocbp->un.ulpWord[4] == IOERR_LINK_DOWN ||
		 iocbp->un.ulpWord[4] == IOERR_SLI_DOWN));
}

#define MENLO_TRANSPORT_TYPE 0xfe
#define MENLO_CONTEXT 0
#define MENLO_PU 3
#define MENLO_TIMEOUT 30
#define SETVAR_MLOMNT 0x103107
#define SETVAR_MLORST 0x103007

#define BPL_ALIGN_SZ 8 /* 8 byte alignment for bpl and mbufs */
