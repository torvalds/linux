/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * Maintained at www.Open-FCoE.org
 */

#ifndef _FC_ELS_H_
#define	_FC_ELS_H_

#include <linux/types.h>
#include <asm/byteorder.h>

/*
 * Fibre Channel Switch - Enhanced Link Services definitions.
 * From T11 FC-LS Rev 1.2 June 7, 2005.
 */

/*
 * ELS Command codes - byte 0 of the frame payload
 */
enum fc_els_cmd {
	ELS_LS_RJT =	0x01,	/* ESL reject */
	ELS_LS_ACC =	0x02,	/* ESL Accept */
	ELS_PLOGI =	0x03,	/* N_Port login */
	ELS_FLOGI =	0x04,	/* F_Port login */
	ELS_LOGO =	0x05,	/* Logout */
	ELS_ABTX =	0x06,	/* Abort exchange - obsolete */
	ELS_RCS =	0x07,	/* read connection status */
	ELS_RES =	0x08,	/* read exchange status block */
	ELS_RSS =	0x09,	/* read sequence status block */
	ELS_RSI =	0x0a,	/* read sequence initiative */
	ELS_ESTS =	0x0b,	/* establish streaming */
	ELS_ESTC =	0x0c,	/* estimate credit */
	ELS_ADVC =	0x0d,	/* advise credit */
	ELS_RTV =	0x0e,	/* read timeout value */
	ELS_RLS =	0x0f,	/* read link error status block */
	ELS_ECHO =	0x10,	/* echo */
	ELS_TEST =	0x11,	/* test */
	ELS_RRQ =	0x12,	/* reinstate recovery qualifier */
	ELS_REC =	0x13,	/* read exchange concise */
	ELS_SRR =	0x14,	/* sequence retransmission request */
	ELS_FPIN =	0x16,	/* Fabric Performance Impact Notification */
	ELS_EDC =	0x17,	/* Exchange Diagnostic Capabilities */
	ELS_RDP =	0x18,	/* Read Diagnostic Parameters */
	ELS_RDF =	0x19,	/* Register Diagnostic Functions */
	ELS_PRLI =	0x20,	/* process login */
	ELS_PRLO =	0x21,	/* process logout */
	ELS_SCN =	0x22,	/* state change notification */
	ELS_TPLS =	0x23,	/* test process login state */
	ELS_TPRLO =	0x24,	/* third party process logout */
	ELS_LCLM =	0x25,	/* login control list mgmt (obs) */
	ELS_GAID =	0x30,	/* get alias_ID */
	ELS_FACT =	0x31,	/* fabric activate alias_id */
	ELS_FDACDT =	0x32,	/* fabric deactivate alias_id */
	ELS_NACT =	0x33,	/* N-port activate alias_id */
	ELS_NDACT =	0x34,	/* N-port deactivate alias_id */
	ELS_QOSR =	0x40,	/* quality of service request */
	ELS_RVCS =	0x41,	/* read virtual circuit status */
	ELS_PDISC =	0x50,	/* discover N_port service params */
	ELS_FDISC =	0x51,	/* discover F_port service params */
	ELS_ADISC =	0x52,	/* discover address */
	ELS_RNC =	0x53,	/* report node cap (obs) */
	ELS_FARP_REQ =	0x54,	/* FC ARP request */
	ELS_FARP_REPL =	0x55,	/* FC ARP reply */
	ELS_RPS =	0x56,	/* read port status block */
	ELS_RPL =	0x57,	/* read port list */
	ELS_RPBC =	0x58,	/* read port buffer condition */
	ELS_FAN =	0x60,	/* fabric address notification */
	ELS_RSCN =	0x61,	/* registered state change notification */
	ELS_SCR =	0x62,	/* state change registration */
	ELS_RNFT =	0x63,	/* report node FC-4 types */
	ELS_CSR =	0x68,	/* clock synch. request */
	ELS_CSU =	0x69,	/* clock synch. update */
	ELS_LINIT =	0x70,	/* loop initialize */
	ELS_LSTS =	0x72,	/* loop status */
	ELS_RNID =	0x78,	/* request node ID data */
	ELS_RLIR =	0x79,	/* registered link incident report */
	ELS_LIRR =	0x7a,	/* link incident record registration */
	ELS_SRL =	0x7b,	/* scan remote loop */
	ELS_SBRP =	0x7c,	/* set bit-error reporting params */
	ELS_RPSC =	0x7d,	/* report speed capabilities */
	ELS_QSA =	0x7e,	/* query security attributes */
	ELS_EVFP =	0x7f,	/* exchange virt. fabrics params */
	ELS_LKA =	0x80,	/* link keep-alive */
	ELS_AUTH_ELS =	0x90,	/* authentication ELS */
};

/*
 * Initializer useful for decoding table.
 * Please keep this in sync with the above definitions.
 */
#define	FC_ELS_CMDS_INIT {			\
	[ELS_LS_RJT] =	"LS_RJT",		\
	[ELS_LS_ACC] =	"LS_ACC",		\
	[ELS_PLOGI] =	"PLOGI",		\
	[ELS_FLOGI] =	"FLOGI",		\
	[ELS_LOGO] =	"LOGO",			\
	[ELS_ABTX] =	"ABTX",			\
	[ELS_RCS] =	"RCS",			\
	[ELS_RES] =	"RES",			\
	[ELS_RSS] =	"RSS",			\
	[ELS_RSI] =	"RSI",			\
	[ELS_ESTS] =	"ESTS",			\
	[ELS_ESTC] =	"ESTC",			\
	[ELS_ADVC] =	"ADVC",			\
	[ELS_RTV] =	"RTV",			\
	[ELS_RLS] =	"RLS",			\
	[ELS_ECHO] =	"ECHO",			\
	[ELS_TEST] =	"TEST",			\
	[ELS_RRQ] =	"RRQ",			\
	[ELS_REC] =	"REC",			\
	[ELS_SRR] =	"SRR",			\
	[ELS_FPIN] =	"FPIN",			\
	[ELS_EDC] =	"EDC",			\
	[ELS_RDP] =	"RDP",			\
	[ELS_RDF] =	"RDF",			\
	[ELS_PRLI] =	"PRLI",			\
	[ELS_PRLO] =	"PRLO",			\
	[ELS_SCN] =	"SCN",			\
	[ELS_TPLS] =	"TPLS",			\
	[ELS_TPRLO] =	"TPRLO",		\
	[ELS_LCLM] =	"LCLM",			\
	[ELS_GAID] =	"GAID",			\
	[ELS_FACT] =	"FACT",			\
	[ELS_FDACDT] =	"FDACDT",		\
	[ELS_NACT] =	"NACT",			\
	[ELS_NDACT] =	"NDACT",		\
	[ELS_QOSR] =	"QOSR",			\
	[ELS_RVCS] =	"RVCS",			\
	[ELS_PDISC] =	"PDISC",		\
	[ELS_FDISC] =	"FDISC",		\
	[ELS_ADISC] =	"ADISC",		\
	[ELS_RNC] =	"RNC",			\
	[ELS_FARP_REQ] = "FARP_REQ",		\
	[ELS_FARP_REPL] =  "FARP_REPL",		\
	[ELS_RPS] =	"RPS",			\
	[ELS_RPL] =	"RPL",			\
	[ELS_RPBC] =	"RPBC",			\
	[ELS_FAN] =	"FAN",			\
	[ELS_RSCN] =	"RSCN",			\
	[ELS_SCR] =	"SCR",			\
	[ELS_RNFT] =	"RNFT",			\
	[ELS_CSR] =	"CSR",			\
	[ELS_CSU] =	"CSU",			\
	[ELS_LINIT] =	"LINIT",		\
	[ELS_LSTS] =	"LSTS",			\
	[ELS_RNID] =	"RNID",			\
	[ELS_RLIR] =	"RLIR",			\
	[ELS_LIRR] =	"LIRR",			\
	[ELS_SRL] =	"SRL",			\
	[ELS_SBRP] =	"SBRP",			\
	[ELS_RPSC] =	"RPSC",			\
	[ELS_QSA] =	"QSA",			\
	[ELS_EVFP] =	"EVFP",			\
	[ELS_LKA] =	"LKA",			\
	[ELS_AUTH_ELS] = "AUTH_ELS",		\
}

/*
 * LS_ACC payload.
 */
struct fc_els_ls_acc {
	__u8          la_cmd;		/* command code ELS_LS_ACC */
	__u8          la_resv[3];	/* reserved */
};

/*
 * ELS reject payload.
 */
struct fc_els_ls_rjt {
	__u8	er_cmd;		/* command code ELS_LS_RJT */
	__u8	er_resv[4];	/* reserved must be zero */
	__u8	er_reason;	/* reason (enum fc_els_rjt_reason below) */
	__u8	er_explan;	/* explanation (enum fc_els_rjt_explan below) */
	__u8	er_vendor;	/* vendor specific code */
};

/*
 * ELS reject reason codes (er_reason).
 */
enum fc_els_rjt_reason {
	ELS_RJT_NONE =		0,	/* no reject - not to be sent */
	ELS_RJT_INVAL =		0x01,	/* invalid ELS command code */
	ELS_RJT_LOGIC =		0x03,	/* logical error */
	ELS_RJT_BUSY =		0x05,	/* logical busy */
	ELS_RJT_PROT =		0x07,	/* protocol error */
	ELS_RJT_UNAB =		0x09,	/* unable to perform command request */
	ELS_RJT_UNSUP =		0x0b,	/* command not supported */
	ELS_RJT_INPROG =	0x0e,	/* command already in progress */
	ELS_RJT_FIP =		0x20,	/* FIP error */
	ELS_RJT_VENDOR =	0xff,	/* vendor specific error */
};


/*
 * reason code explanation (er_explan).
 */
enum fc_els_rjt_explan {
	ELS_EXPL_NONE =		0x00,	/* No additional explanation */
	ELS_EXPL_SPP_OPT_ERR =	0x01,	/* service parameter error - options */
	ELS_EXPL_SPP_ICTL_ERR =	0x03,	/* service parm error - initiator ctl */
	ELS_EXPL_AH =		0x11,	/* invalid association header */
	ELS_EXPL_AH_REQ =	0x13,	/* association_header required */
	ELS_EXPL_SID =		0x15,	/* invalid originator S_ID */
	ELS_EXPL_OXID_RXID =	0x17,	/* invalid OX_ID-RX_ID combination */
	ELS_EXPL_INPROG =	0x19,	/* Request already in progress */
	ELS_EXPL_PLOGI_REQD =	0x1e,	/* N_Port login required */
	ELS_EXPL_INSUF_RES =	0x29,	/* insufficient resources */
	ELS_EXPL_UNAB_DATA =	0x2a,	/* unable to supply requested data */
	ELS_EXPL_UNSUPR =	0x2c,	/* Request not supported */
	ELS_EXPL_INV_LEN =	0x2d,	/* Invalid payload length */
	ELS_EXPL_NOT_NEIGHBOR = 0x62,	/* VN2VN_Port not in neighbor set */
	/* TBD - above definitions incomplete */
};

/*
 * Link Service TLV Descriptor Tag Values
 */
enum fc_ls_tlv_dtag {
	ELS_DTAG_LS_REQ_INFO =		0x00000001,
		/* Link Service Request Information Descriptor */
	ELS_DTAG_LNK_FAULT_CAP =	0x0001000D,
		/* Link Fault Capability Descriptor */
	ELS_DTAG_CG_SIGNAL_CAP =	0x0001000F,
		/* Congestion Signaling Capability Descriptor */
	ELS_DTAG_LNK_INTEGRITY =	0x00020001,
		/* Link Integrity Notification Descriptor */
	ELS_DTAG_DELIVERY =		0x00020002,
		/* Delivery Notification Descriptor */
	ELS_DTAG_PEER_CONGEST =		0x00020003,
		/* Peer Congestion Notification Descriptor */
	ELS_DTAG_CONGESTION =		0x00020004,
		/* Congestion Notification Descriptor */
	ELS_DTAG_FPIN_REGISTER =	0x00030001,
		/* FPIN Registration Descriptor */
};

/*
 * Initializer useful for decoding table.
 * Please keep this in sync with the above definitions.
 */
#define FC_LS_TLV_DTAG_INIT {					      \
	{ ELS_DTAG_LS_REQ_INFO,		"Link Service Request Information" }, \
	{ ELS_DTAG_LNK_FAULT_CAP,	"Link Fault Capability" },	      \
	{ ELS_DTAG_CG_SIGNAL_CAP,	"Congestion Signaling Capability" },  \
	{ ELS_DTAG_LNK_INTEGRITY,	"Link Integrity Notification" },      \
	{ ELS_DTAG_DELIVERY,		"Delivery Notification Present" },    \
	{ ELS_DTAG_PEER_CONGEST,	"Peer Congestion Notification" },     \
	{ ELS_DTAG_CONGESTION,		"Congestion Notification" },	      \
	{ ELS_DTAG_FPIN_REGISTER,	"FPIN Registration" },		      \
}


/*
 * Generic Link Service TLV Descriptor format
 *
 * This structure, as it defines no payload, will also be referred to
 * as the "tlv header" - which contains the tag and len fields.
 */
struct fc_tlv_desc {
	__be32		desc_tag;	/* Notification Descriptor Tag */
	__be32		desc_len;	/* Length of Descriptor (in bytes).
					 * Size of descriptor excluding
					 * desc_tag and desc_len fields.
					 */
	__u8		desc_value[0];  /* Descriptor Value */
};

/* Descriptor tag and len fields are considered the mandatory header
 * for a descriptor
 */
#define FC_TLV_DESC_HDR_SZ	sizeof(struct fc_tlv_desc)

/*
 * Macro, used when initializing payloads, to return the descriptor length.
 * Length is size of descriptor minus the tag and len fields.
 */
#define FC_TLV_DESC_LENGTH_FROM_SZ(desc)	\
		(sizeof(desc) - FC_TLV_DESC_HDR_SZ)

/* Macro, used on received payloads, to return the descriptor length */
#define FC_TLV_DESC_SZ_FROM_LENGTH(tlv)		\
		(__be32_to_cpu((tlv)->desc_len) + FC_TLV_DESC_HDR_SZ)

/*
 * This helper is used to walk descriptors in a descriptor list.
 * Given the address of the current descriptor, which minimally contains a
 * tag and len field, calculate the address of the next descriptor based
 * on the len field.
 */
static __inline__ void *fc_tlv_next_desc(void *desc)
{
	struct fc_tlv_desc *tlv = desc;

	return (desc + FC_TLV_DESC_SZ_FROM_LENGTH(tlv));
}


/*
 * Link Service Request Information Descriptor
 */
struct fc_els_lsri_desc {
	__be32		desc_tag;	/* descriptor tag (0x0000 0001) */
	__be32		desc_len;	/* Length of Descriptor (in bytes) (4).
					 * Size of descriptor excluding
					 * desc_tag and desc_len fields.
					 */
	struct {
		__u8	cmd;		/* ELS cmd byte */
		__u8	bytes[3];	/* bytes 1..3 */
	} rqst_w0;			/* Request word 0 */
};


/*
 * Common service parameters (N ports).
 */
struct fc_els_csp {
	__u8		sp_hi_ver;	/* highest version supported (obs.) */
	__u8		sp_lo_ver;	/* highest version supported (obs.) */
	__be16		sp_bb_cred;	/* buffer-to-buffer credits */
	__be16		sp_features;	/* common feature flags */
	__be16		sp_bb_data;	/* b-b state number and data field sz */
	union {
		struct {
			__be16	_sp_tot_seq; /* total concurrent sequences */
			__be16	_sp_rel_off; /* rel. offset by info cat */
		} sp_plogi;
		struct {
			__be32	_sp_r_a_tov; /* resource alloc. timeout msec */
		} sp_flogi_acc;
	} sp_u;
	__be32		sp_e_d_tov;	/* error detect timeout value */
};
#define	sp_tot_seq	sp_u.sp_plogi._sp_tot_seq
#define	sp_rel_off	sp_u.sp_plogi._sp_rel_off
#define	sp_r_a_tov	sp_u.sp_flogi_acc._sp_r_a_tov

#define	FC_SP_BB_DATA_MASK 0xfff /* mask for data field size in sp_bb_data */

/*
 * Minimum and maximum values for max data field size in service parameters.
 */
#define	FC_SP_MIN_MAX_PAYLOAD	FC_MIN_MAX_PAYLOAD
#define	FC_SP_MAX_MAX_PAYLOAD	FC_MAX_PAYLOAD

/*
 * sp_features
 */
#define	FC_SP_FT_NPIV	0x8000	/* multiple N_Port_ID support (FLOGI) */
#define	FC_SP_FT_CIRO	0x8000	/* continuously increasing rel off (PLOGI) */
#define	FC_SP_FT_CLAD	0x8000	/* clean address (in FLOGI LS_ACC) */
#define	FC_SP_FT_RAND	0x4000	/* random relative offset */
#define	FC_SP_FT_VAL	0x2000	/* valid vendor version level */
#define	FC_SP_FT_NPIV_ACC	0x2000	/* NPIV assignment (FLOGI LS_ACC) */
#define	FC_SP_FT_FPORT	0x1000	/* F port (1) vs. N port (0) */
#define	FC_SP_FT_ABB	0x0800	/* alternate BB_credit management */
#define	FC_SP_FT_EDTR	0x0400	/* E_D_TOV Resolution is nanoseconds */
#define	FC_SP_FT_MCAST	0x0200	/* multicast */
#define	FC_SP_FT_BCAST	0x0100	/* broadcast */
#define	FC_SP_FT_HUNT	0x0080	/* hunt group */
#define	FC_SP_FT_SIMP	0x0040	/* dedicated simplex */
#define	FC_SP_FT_SEC	0x0020	/* reserved for security */
#define	FC_SP_FT_CSYN	0x0010	/* clock synch. supported */
#define	FC_SP_FT_RTTOV	0x0008	/* R_T_TOV value 100 uS, else 100 mS */
#define	FC_SP_FT_HALF	0x0004	/* dynamic half duplex */
#define	FC_SP_FT_SEQC	0x0002	/* SEQ_CNT */
#define	FC_SP_FT_PAYL	0x0001	/* FLOGI payload length 256, else 116 */

/*
 * Class-specific service parameters.
 */
struct fc_els_cssp {
	__be16		cp_class;	/* class flags */
	__be16		cp_init;	/* initiator flags */
	__be16		cp_recip;	/* recipient flags */
	__be16		cp_rdfs;	/* receive data field size */
	__be16		cp_con_seq;	/* concurrent sequences */
	__be16		cp_ee_cred;	/* N-port end-to-end credit */
	__u8		cp_resv1;	/* reserved */
	__u8		cp_open_seq;	/* open sequences per exchange */
	__u8		_cp_resv2[2];	/* reserved */
};

/*
 * cp_class flags.
 */
#define	FC_CPC_VALID	0x8000		/* class valid */
#define	FC_CPC_IMIX	0x4000		/* intermix mode */
#define	FC_CPC_SEQ	0x0800		/* sequential delivery */
#define	FC_CPC_CAMP	0x0200		/* camp-on */
#define	FC_CPC_PRI	0x0080		/* priority */

/*
 * cp_init flags.
 * (TBD: not all flags defined here).
 */
#define	FC_CPI_CSYN	0x0010		/* clock synch. capable */

/*
 * cp_recip flags.
 */
#define	FC_CPR_CSYN	0x0008		/* clock synch. capable */

/*
 * NFC_ELS_FLOGI: Fabric login request.
 * NFC_ELS_PLOGI: Port login request (same format).
 */
struct fc_els_flogi {
	__u8		fl_cmd;		/* command */
	__u8		_fl_resvd[3];	/* must be zero */
	struct fc_els_csp fl_csp;	/* common service parameters */
	__be64		fl_wwpn;	/* port name */
	__be64		fl_wwnn;	/* node name */
	struct fc_els_cssp fl_cssp[4];	/* class 1-4 service parameters */
	__u8		fl_vend[16];	/* vendor version level */
} __attribute__((__packed__));

/*
 * Process login service parameter page.
 */
struct fc_els_spp {
	__u8		spp_type;	/* type code or common service params */
	__u8		spp_type_ext;	/* type code extension */
	__u8		spp_flags;
	__u8		_spp_resvd;
	__be32		spp_orig_pa;	/* originator process associator */
	__be32		spp_resp_pa;	/* responder process associator */
	__be32		spp_params;	/* service parameters */
};

/*
 * spp_flags.
 */
#define	FC_SPP_OPA_VAL	    0x80	/* originator proc. assoc. valid */
#define	FC_SPP_RPA_VAL	    0x40	/* responder proc. assoc. valid */
#define	FC_SPP_EST_IMG_PAIR 0x20	/* establish image pair */
#define	FC_SPP_RESP_MASK    0x0f	/* mask for response code (below) */

/*
 * SPP response code in spp_flags - lower 4 bits.
 */
enum fc_els_spp_resp {
	FC_SPP_RESP_ACK	=	1,	/* request executed */
	FC_SPP_RESP_RES =	2,	/* unable due to lack of resources */
	FC_SPP_RESP_INIT =	3,	/* initialization not complete */
	FC_SPP_RESP_NO_PA = 	4,	/* unknown process associator */
	FC_SPP_RESP_CONF = 	5,	/* configuration precludes image pair */
	FC_SPP_RESP_COND = 	6,	/* request completed conditionally */
	FC_SPP_RESP_MULT = 	7,	/* unable to handle multiple SPPs */
	FC_SPP_RESP_INVL = 	8,	/* SPP is invalid */
};

/*
 * ELS_RRQ - Reinstate Recovery Qualifier
 */
struct fc_els_rrq {
	__u8		rrq_cmd;	/* command (0x12) */
	__u8		rrq_zero[3];	/* specified as zero - part of cmd */
	__u8		rrq_resvd;	/* reserved */
	__u8		rrq_s_id[3];	/* originator FID */
	__be16		rrq_ox_id;	/* originator exchange ID */
	__be16		rrq_rx_id;	/* responders exchange ID */
};

/*
 * ELS_REC - Read exchange concise.
 */
struct fc_els_rec {
	__u8		rec_cmd;	/* command (0x13) */
	__u8		rec_zero[3];	/* specified as zero - part of cmd */
	__u8		rec_resvd;	/* reserved */
	__u8		rec_s_id[3];	/* originator FID */
	__be16		rec_ox_id;	/* originator exchange ID */
	__be16		rec_rx_id;	/* responders exchange ID */
};

/*
 * ELS_REC LS_ACC payload.
 */
struct fc_els_rec_acc {
	__u8		reca_cmd;	/* accept (0x02) */
	__u8		reca_zero[3];	/* specified as zero - part of cmd */
	__be16		reca_ox_id;	/* originator exchange ID */
	__be16		reca_rx_id;	/* responders exchange ID */
	__u8		reca_resvd1;	/* reserved */
	__u8		reca_ofid[3];	/* originator FID */
	__u8		reca_resvd2;	/* reserved */
	__u8		reca_rfid[3];	/* responder FID */
	__be32		reca_fc4value;	/* FC4 value */
	__be32		reca_e_stat;	/* ESB (exchange status block) status */
};

/*
 * ELS_PRLI - Process login request and response.
 */
struct fc_els_prli {
	__u8		prli_cmd;	/* command */
	__u8		prli_spp_len;	/* length of each serv. parm. page */
	__be16		prli_len;	/* length of entire payload */
	/* service parameter pages follow */
};

/*
 * ELS_PRLO - Process logout request and response.
 */
struct fc_els_prlo {
	__u8            prlo_cmd;       /* command */
	__u8            prlo_obs;       /* obsolete, but shall be set to 10h */
	__be16          prlo_len;       /* payload length */
};

/*
 * ELS_ADISC payload
 */
struct fc_els_adisc {
	__u8		adisc_cmd;
	__u8		adisc_resv[3];
	__u8            adisc_resv1;
	__u8            adisc_hard_addr[3];
	__be64          adisc_wwpn;
	__be64          adisc_wwnn;
	__u8            adisc_resv2;
	__u8            adisc_port_id[3];
} __attribute__((__packed__));

/*
 * ELS_LOGO - process or fabric logout.
 */
struct fc_els_logo {
	__u8		fl_cmd;		/* command code */
	__u8		fl_zero[3];	/* specified as zero - part of cmd */
	__u8		fl_resvd;	/* reserved */
	__u8		fl_n_port_id[3];/* N port ID */
	__be64		fl_n_port_wwn;	/* port name */
};

/*
 * ELS_RTV - read timeout value.
 */
struct fc_els_rtv {
	__u8		rtv_cmd;	/* command code 0x0e */
	__u8		rtv_zero[3];	/* specified as zero - part of cmd */
};

/*
 * LS_ACC for ELS_RTV - read timeout value.
 */
struct fc_els_rtv_acc {
	__u8		rtv_cmd;	/* command code 0x02 */
	__u8		rtv_zero[3];	/* specified as zero - part of cmd */
	__be32		rtv_r_a_tov;	/* resource allocation timeout value */
	__be32		rtv_e_d_tov;	/* error detection timeout value */
	__be32		rtv_toq;	/* timeout qualifier (see below) */
};

/*
 * rtv_toq bits.
 */
#define	FC_ELS_RTV_EDRES (1 << 26)	/* E_D_TOV resolution is nS else mS */
#define	FC_ELS_RTV_RTTOV (1 << 19)	/* R_T_TOV is 100 uS else 100 mS */

/*
 * ELS_SCR - state change registration payload.
 */
struct fc_els_scr {
	__u8		scr_cmd;	/* command code */
	__u8		scr_resv[6];	/* reserved */
	__u8		scr_reg_func;	/* registration function (see below) */
};

enum fc_els_scr_func {
	ELS_SCRF_FAB =	1,	/* fabric-detected registration */
	ELS_SCRF_NPORT = 2,	/* Nx_Port-detected registration */
	ELS_SCRF_FULL =	3,	/* full registration */
	ELS_SCRF_CLEAR = 255,	/* remove any current registrations */
};

/*
 * ELS_RSCN - registered state change notification payload.
 */
struct fc_els_rscn {
	__u8		rscn_cmd;	/* RSCN opcode (0x61) */
	__u8		rscn_page_len;	/* page length (4) */
	__be16		rscn_plen;	/* payload length including this word */

	/* followed by 4-byte generic affected Port_ID pages */
};

struct fc_els_rscn_page {
	__u8		rscn_page_flags; /* event and address format */
	__u8		rscn_fid[3];	/* fabric ID */
};

#define	ELS_RSCN_EV_QUAL_BIT	2	/* shift count for event qualifier */
#define	ELS_RSCN_EV_QUAL_MASK	0xf	/* mask for event qualifier */
#define	ELS_RSCN_ADDR_FMT_BIT	0	/* shift count for address format */
#define	ELS_RSCN_ADDR_FMT_MASK	0x3	/* mask for address format */

enum fc_els_rscn_ev_qual {
	ELS_EV_QUAL_NONE = 0,		/* unspecified */
	ELS_EV_QUAL_NS_OBJ = 1,		/* changed name server object */
	ELS_EV_QUAL_PORT_ATTR = 2,	/* changed port attribute */
	ELS_EV_QUAL_SERV_OBJ = 3,	/* changed service object */
	ELS_EV_QUAL_SW_CONFIG = 4,	/* changed switch configuration */
	ELS_EV_QUAL_REM_OBJ = 5,	/* removed object */
};

enum fc_els_rscn_addr_fmt {
	ELS_ADDR_FMT_PORT = 0,	/* rscn_fid is a port address */
	ELS_ADDR_FMT_AREA = 1,	/* rscn_fid is a area address */
	ELS_ADDR_FMT_DOM = 2,	/* rscn_fid is a domain address */
	ELS_ADDR_FMT_FAB = 3,	/* anything on fabric may have changed */
};

/*
 * ELS_RNID - request Node ID.
 */
struct fc_els_rnid {
	__u8		rnid_cmd;	/* RNID opcode (0x78) */
	__u8		rnid_resv[3];	/* reserved */
	__u8		rnid_fmt;	/* data format */
	__u8		rnid_resv2[3];	/* reserved */
};

/*
 * Node Identification Data formats (rnid_fmt)
 */
enum fc_els_rnid_fmt {
	ELS_RNIDF_NONE = 0,		/* no specific identification data */
	ELS_RNIDF_GEN = 0xdf,		/* general topology discovery format */
};

/*
 * ELS_RNID response.
 */
struct fc_els_rnid_resp {
	__u8		rnid_cmd;	/* response code (LS_ACC) */
	__u8		rnid_resv[3];	/* reserved */
	__u8		rnid_fmt;	/* data format */
	__u8		rnid_cid_len;	/* common ID data length */
	__u8		rnid_resv2;	/* reserved */
	__u8		rnid_sid_len;	/* specific ID data length */
};

struct fc_els_rnid_cid {
	__be64		rnid_wwpn;	/* N port name */
	__be64		rnid_wwnn;	/* node name */
};

struct fc_els_rnid_gen {
	__u8		rnid_vend_id[16]; /* vendor-unique ID */
	__be32		rnid_atype;	/* associated type (see below) */
	__be32		rnid_phys_port;	/* physical port number */
	__be32		rnid_att_nodes;	/* number of attached nodes */
	__u8		rnid_node_mgmt;	/* node management (see below) */
	__u8		rnid_ip_ver;	/* IP version (see below) */
	__be16		rnid_prot_port;	/* UDP / TCP port number */
	__be32		rnid_ip_addr[4]; /* IP address */
	__u8		rnid_resvd[2];	/* reserved */
	__be16		rnid_vend_spec;	/* vendor-specific field */
};

enum fc_els_rnid_atype {
	ELS_RNIDA_UNK =		0x01,	/* unknown */
	ELS_RNIDA_OTHER =	0x02,	/* none of the following */
	ELS_RNIDA_HUB =		0x03,
	ELS_RNIDA_SWITCH =	0x04,
	ELS_RNIDA_GATEWAY =	0x05,
	ELS_RNIDA_CONV =	0x06,   /* Obsolete, do not use this value */
	ELS_RNIDA_HBA =	        0x07,   /* Obsolete, do not use this value */
	ELS_RNIDA_PROXY =       0x08,   /* Obsolete, do not use this value */
	ELS_RNIDA_STORAGE =	0x09,
	ELS_RNIDA_HOST =	0x0a,
	ELS_RNIDA_SUBSYS =	0x0b,	/* storage subsystem (e.g., RAID) */
	ELS_RNIDA_ACCESS =	0x0e,	/* access device (e.g. media changer) */
	ELS_RNIDA_NAS =		0x11,	/* NAS server */
	ELS_RNIDA_BRIDGE =	0x12,	/* bridge */
	ELS_RNIDA_VIRT =	0x13,	/* virtualization device */
	ELS_RNIDA_MF =		0xff,	/* multifunction device (bits below) */
	ELS_RNIDA_MF_HUB =	1UL << 31, 	/* hub */
	ELS_RNIDA_MF_SW =	1UL << 30, 	/* switch */
	ELS_RNIDA_MF_GW =	1UL << 29,	/* gateway */
	ELS_RNIDA_MF_ST =	1UL << 28,	/* storage */
	ELS_RNIDA_MF_HOST =	1UL << 27,	/* host */
	ELS_RNIDA_MF_SUB =	1UL << 26,	/* storage subsystem */
	ELS_RNIDA_MF_ACC =	1UL << 25,	/* storage access dev */
	ELS_RNIDA_MF_WDM =	1UL << 24,	/* wavelength division mux */
	ELS_RNIDA_MF_NAS =	1UL << 23,	/* NAS server */
	ELS_RNIDA_MF_BR =	1UL << 22,	/* bridge */
	ELS_RNIDA_MF_VIRT =	1UL << 21,	/* virtualization device */
};

enum fc_els_rnid_mgmt {
	ELS_RNIDM_SNMP =	0,
	ELS_RNIDM_TELNET =	1,
	ELS_RNIDM_HTTP =	2,
	ELS_RNIDM_HTTPS =	3,
	ELS_RNIDM_XML =		4,	/* HTTP + XML */
};

enum fc_els_rnid_ipver {
	ELS_RNIDIP_NONE =	0,	/* no IP support or node mgmt. */
	ELS_RNIDIP_V4 =		1,	/* IPv4 */
	ELS_RNIDIP_V6 =		2,	/* IPv6 */
};

/*
 * ELS RPL - Read Port List.
 */
struct fc_els_rpl {
	__u8		rpl_cmd;	/* command */
	__u8		rpl_resv[5];	/* reserved - must be zero */
	__be16		rpl_max_size;	/* maximum response size or zero */
	__u8		rpl_resv1;	/* reserved - must be zero */
	__u8		rpl_index[3];	/* starting index */
};

/*
 * Port number block in RPL response.
 */
struct fc_els_pnb {
	__be32		pnb_phys_pn;	/* physical port number */
	__u8		pnb_resv;	/* reserved */
	__u8		pnb_port_id[3];	/* port ID */
	__be64		pnb_wwpn;	/* port name */
};

/*
 * RPL LS_ACC response.
 */
struct fc_els_rpl_resp {
	__u8		rpl_cmd;	/* ELS_LS_ACC */
	__u8		rpl_resv1;	/* reserved - must be zero */
	__be16		rpl_plen;	/* payload length */
	__u8		rpl_resv2;	/* reserved - must be zero */
	__u8		rpl_llen[3];	/* list length */
	__u8		rpl_resv3;	/* reserved - must be zero */
	__u8		rpl_index[3];	/* starting index */
	struct fc_els_pnb rpl_pnb[1];	/* variable number of PNBs */
};

/*
 * Link Error Status Block.
 */
struct fc_els_lesb {
	__be32		lesb_link_fail;	/* link failure count */
	__be32		lesb_sync_loss;	/* loss of synchronization count */
	__be32		lesb_sig_loss;	/* loss of signal count */
	__be32		lesb_prim_err;	/* primitive sequence error count */
	__be32		lesb_inv_word;	/* invalid transmission word count */
	__be32		lesb_inv_crc;	/* invalid CRC count */
};

/*
 * ELS RPS - Read Port Status Block request.
 */
struct fc_els_rps {
	__u8		rps_cmd;	/* command */
	__u8		rps_resv[2];	/* reserved - must be zero */
	__u8		rps_flag;	/* flag - see below */
	__be64		rps_port_spec;	/* port selection */
};

enum fc_els_rps_flag {
	FC_ELS_RPS_DID =	0x00,	/* port identified by D_ID of req. */
	FC_ELS_RPS_PPN =	0x01,	/* port_spec is physical port number */
	FC_ELS_RPS_WWPN =	0x02,	/* port_spec is port WWN */
};

/*
 * ELS RPS LS_ACC response.
 */
struct fc_els_rps_resp {
	__u8		rps_cmd;	/* command - LS_ACC */
	__u8		rps_resv[2];	/* reserved - must be zero */
	__u8		rps_flag;	/* flag - see below */
	__u8		rps_resv2[2];	/* reserved */
	__be16		rps_status;	/* port status - see below */
	struct fc_els_lesb rps_lesb;	/* link error status block */
};

enum fc_els_rps_resp_flag {
	FC_ELS_RPS_LPEV =	0x01,	/* L_port extension valid */
};

enum fc_els_rps_resp_status {
	FC_ELS_RPS_PTP =	1 << 5,	/* point-to-point connection */
	FC_ELS_RPS_LOOP =	1 << 4,	/* loop mode */
	FC_ELS_RPS_FAB =	1 << 3,	/* fabric present */
	FC_ELS_RPS_NO_SIG =	1 << 2,	/* loss of signal */
	FC_ELS_RPS_NO_SYNC =	1 << 1,	/* loss of synchronization */
	FC_ELS_RPS_RESET =	1 << 0,	/* in link reset protocol */
};

/*
 * ELS LIRR - Link Incident Record Registration request.
 */
struct fc_els_lirr {
	__u8		lirr_cmd;	/* command */
	__u8		lirr_resv[3];	/* reserved - must be zero */
	__u8		lirr_func;	/* registration function */
	__u8		lirr_fmt;	/* FC-4 type of RLIR requested */
	__u8		lirr_resv2[2];	/* reserved - must be zero */
};

enum fc_els_lirr_func {
	ELS_LIRR_SET_COND = 	0x01,	/* set - conditionally receive */
	ELS_LIRR_SET_UNCOND = 	0x02,	/* set - unconditionally receive */
	ELS_LIRR_CLEAR = 	0xff	/* clear registration */
};

/*
 * ELS SRL - Scan Remote Loop request.
 */
struct fc_els_srl {
	__u8		srl_cmd;	/* command */
	__u8		srl_resv[3];	/* reserved - must be zero */
	__u8		srl_flag;	/* flag - see below */
	__u8		srl_flag_param[3];	/* flag parameter */
};

enum fc_els_srl_flag {
	FC_ELS_SRL_ALL =	0x00,	/* scan all FL ports */
	FC_ELS_SRL_ONE =	0x01,	/* scan specified loop */
	FC_ELS_SRL_EN_PER =	0x02,	/* enable periodic scanning (param) */
	FC_ELS_SRL_DIS_PER =	0x03,	/* disable periodic scanning */
};

/*
 * ELS RLS - Read Link Error Status Block request.
 */
struct fc_els_rls {
	__u8		rls_cmd;	/* command */
	__u8		rls_resv[4];	/* reserved - must be zero */
	__u8		rls_port_id[3];	/* port ID */
};

/*
 * ELS RLS LS_ACC Response.
 */
struct fc_els_rls_resp {
	__u8		rls_cmd;	/* ELS_LS_ACC */
	__u8		rls_resv[3];	/* reserved - must be zero */
	struct fc_els_lesb rls_lesb;	/* link error status block */
};

/*
 * ELS RLIR - Registered Link Incident Report.
 * This is followed by the CLIR and the CLID, described below.
 */
struct fc_els_rlir {
	__u8		rlir_cmd;	/* command */
	__u8		rlir_resv[3];	/* reserved - must be zero */
	__u8		rlir_fmt;	/* format (FC4-type if type specific) */
	__u8		rlir_clr_len;	/* common link incident record length */
	__u8		rlir_cld_len;	/* common link incident desc. length */
	__u8		rlir_slr_len;	/* spec. link incident record length */
};

/*
 * CLIR - Common Link Incident Record Data. - Sent via RLIR.
 */
struct fc_els_clir {
	__be64		clir_wwpn;	/* incident port name */
	__be64		clir_wwnn;	/* incident port node name */
	__u8		clir_port_type;	/* incident port type */
	__u8		clir_port_id[3];	/* incident port ID */

	__be64		clir_conn_wwpn;	/* connected port name */
	__be64		clir_conn_wwnn;	/* connected node name */
	__be64		clir_fab_name;	/* fabric name */
	__be32		clir_phys_port;	/* physical port number */
	__be32		clir_trans_id;	/* transaction ID */
	__u8		clir_resv[3];	/* reserved */
	__u8		clir_ts_fmt;	/* time stamp format */
	__be64		clir_timestamp;	/* time stamp */
};

/*
 * CLIR clir_ts_fmt - time stamp format values.
 */
enum fc_els_clir_ts_fmt {
	ELS_CLIR_TS_UNKNOWN = 	0,	/* time stamp field unknown */
	ELS_CLIR_TS_SEC_FRAC = 	1,	/* time in seconds and fractions */
	ELS_CLIR_TS_CSU =	2,	/* time in clock synch update format */
};

/*
 * Common Link Incident Descriptor - sent via RLIR.
 */
struct fc_els_clid {
	__u8		clid_iq;	/* incident qualifier flags */
	__u8		clid_ic;	/* incident code */
	__be16		clid_epai;	/* domain/area of ISL */
};

/*
 * CLID incident qualifier flags.
 */
enum fc_els_clid_iq {
	ELS_CLID_SWITCH =	0x20,	/* incident port is a switch node */
	ELS_CLID_E_PORT =	0x10,	/* incident is an ISL (E) port */
	ELS_CLID_SEV_MASK =	0x0c,	/* severity 2-bit field mask */
	ELS_CLID_SEV_INFO =	0x00,	/* report is informational */
	ELS_CLID_SEV_INOP =	0x08,	/* link not operational */
	ELS_CLID_SEV_DEG =	0x04,	/* link degraded but operational */
	ELS_CLID_LASER =	0x02,	/* subassembly is a laser */
	ELS_CLID_FRU =		0x01,	/* format can identify a FRU */
};

/*
 * CLID incident code.
 */
enum fc_els_clid_ic {
	ELS_CLID_IC_IMPL =	1,	/* implicit incident */
	ELS_CLID_IC_BER =	2,	/* bit-error-rate threshold exceeded */
	ELS_CLID_IC_LOS =	3,	/* loss of synch or signal */
	ELS_CLID_IC_NOS =	4,	/* non-operational primitive sequence */
	ELS_CLID_IC_PST =	5,	/* primitive sequence timeout */
	ELS_CLID_IC_INVAL =	6,	/* invalid primitive sequence */
	ELS_CLID_IC_LOOP_TO =	7,	/* loop initialization time out */
	ELS_CLID_IC_LIP =	8,	/* receiving LIP */
};

/*
 * Link Integrity event types
 */
enum fc_fpin_li_event_types {
	FPIN_LI_UNKNOWN =		0x0,
	FPIN_LI_LINK_FAILURE =		0x1,
	FPIN_LI_LOSS_OF_SYNC =		0x2,
	FPIN_LI_LOSS_OF_SIG =		0x3,
	FPIN_LI_PRIM_SEQ_ERR =		0x4,
	FPIN_LI_INVALID_TX_WD =		0x5,
	FPIN_LI_INVALID_CRC =		0x6,
	FPIN_LI_DEVICE_SPEC =		0xF,
};

/*
 * Initializer useful for decoding table.
 * Please keep this in sync with the above definitions.
 */
#define FC_FPIN_LI_EVT_TYPES_INIT {					\
	{ FPIN_LI_UNKNOWN,		"Unknown" },			\
	{ FPIN_LI_LINK_FAILURE,		"Link Failure" },		\
	{ FPIN_LI_LOSS_OF_SYNC,		"Loss of Synchronization" },	\
	{ FPIN_LI_LOSS_OF_SIG,		"Loss of Signal" },		\
	{ FPIN_LI_PRIM_SEQ_ERR,		"Primitive Sequence Protocol Error" }, \
	{ FPIN_LI_INVALID_TX_WD,	"Invalid Transmission Word" },	\
	{ FPIN_LI_INVALID_CRC,		"Invalid CRC" },		\
	{ FPIN_LI_DEVICE_SPEC,		"Device Specific" },		\
}

/*
 * Delivery event types
 */
enum fc_fpin_deli_event_types {
	FPIN_DELI_UNKNOWN =		0x0,
	FPIN_DELI_TIMEOUT =		0x1,
	FPIN_DELI_UNABLE_TO_ROUTE =	0x2,
	FPIN_DELI_DEVICE_SPEC =		0xF,
};

/*
 * Initializer useful for decoding table.
 * Please keep this in sync with the above definitions.
 */
#define FC_FPIN_DELI_EVT_TYPES_INIT {					\
	{ FPIN_DELI_UNKNOWN,		"Unknown" },			\
	{ FPIN_DELI_TIMEOUT,		"Timeout" },			\
	{ FPIN_DELI_UNABLE_TO_ROUTE,	"Unable to Route" },		\
	{ FPIN_DELI_DEVICE_SPEC,	"Device Specific" },		\
}

/*
 * Congestion event types
 */
enum fc_fpin_congn_event_types {
	FPIN_CONGN_CLEAR =		0x0,
	FPIN_CONGN_LOST_CREDIT =	0x1,
	FPIN_CONGN_CREDIT_STALL =	0x2,
	FPIN_CONGN_OVERSUBSCRIPTION =	0x3,
	FPIN_CONGN_DEVICE_SPEC =	0xF,
};

/*
 * Initializer useful for decoding table.
 * Please keep this in sync with the above definitions.
 */
#define FC_FPIN_CONGN_EVT_TYPES_INIT {					\
	{ FPIN_CONGN_CLEAR,		"Clear" },			\
	{ FPIN_CONGN_LOST_CREDIT,	"Lost Credit" },		\
	{ FPIN_CONGN_CREDIT_STALL,	"Credit Stall" },		\
	{ FPIN_CONGN_OVERSUBSCRIPTION,	"Oversubscription" },		\
	{ FPIN_CONGN_DEVICE_SPEC,	"Device Specific" },		\
}

enum fc_fpin_congn_severity_types {
	FPIN_CONGN_SEVERITY_WARNING =	0xF1,
	FPIN_CONGN_SEVERITY_ERROR =	0xF7,
};

/*
 * Link Integrity Notification Descriptor
 */
struct fc_fn_li_desc {
	__be32		desc_tag;	/* Descriptor Tag (0x00020001) */
	__be32		desc_len;	/* Length of Descriptor (in bytes).
					 * Size of descriptor excluding
					 * desc_tag and desc_len fields.
					 */
	__be64		detecting_wwpn;	/* Port Name that detected event */
	__be64		attached_wwpn;	/* Port Name of device attached to
					 * detecting Port Name
					 */
	__be16		event_type;	/* see enum fc_fpin_li_event_types */
	__be16		event_modifier;	/* Implementation specific value
					 * describing the event type
					 */
	__be32		event_threshold;/* duration in ms of the link
					 * integrity detection cycle
					 */
	__be32		event_count;	/* minimum number of event
					 * occurrences during the event
					 * threshold to caause the LI event
					 */
	__be32		pname_count;	/* number of portname_list elements */
	__be64		pname_list[0];	/* list of N_Port_Names accessible
					 * through the attached port
					 */
};

/*
 * Delivery Notification Descriptor
 */
struct fc_fn_deli_desc {
	__be32		desc_tag;	/* Descriptor Tag (0x00020002) */
	__be32		desc_len;	/* Length of Descriptor (in bytes).
					 * Size of descriptor excluding
					 * desc_tag and desc_len fields.
					 */
	__be64		detecting_wwpn;	/* Port Name that detected event */
	__be64		attached_wwpn;	/* Port Name of device attached to
					 * detecting Port Name
					 */
	__be32		deli_reason_code;/* see enum fc_fpin_deli_event_types */
};

/*
 * Peer Congestion Notification Descriptor
 */
struct fc_fn_peer_congn_desc {
	__be32		desc_tag;	/* Descriptor Tag (0x00020003) */
	__be32		desc_len;	/* Length of Descriptor (in bytes).
					 * Size of descriptor excluding
					 * desc_tag and desc_len fields.
					 */
	__be64		detecting_wwpn;	/* Port Name that detected event */
	__be64		attached_wwpn;	/* Port Name of device attached to
					 * detecting Port Name
					 */
	__be16		event_type;	/* see enum fc_fpin_congn_event_types */
	__be16		event_modifier;	/* Implementation specific value
					 * describing the event type
					 */
	__be32		event_period;	/* duration (ms) of the detected
					 * congestion event
					 */
	__be32		pname_count;	/* number of portname_list elements */
	__be64		pname_list[0];	/* list of N_Port_Names accessible
					 * through the attached port
					 */
};

/*
 * Congestion Notification Descriptor
 */
struct fc_fn_congn_desc {
	__be32		desc_tag;	/* Descriptor Tag (0x00020004) */
	__be32		desc_len;	/* Length of Descriptor (in bytes).
					 * Size of descriptor excluding
					 * desc_tag and desc_len fields.
					 */
	__be16		event_type;	/* see enum fc_fpin_congn_event_types */
	__be16		event_modifier;	/* Implementation specific value
					 * describing the event type
					 */
	__be32		event_period;	/* duration (ms) of the detected
					 * congestion event
					 */
	__u8		severity;	/* command */
	__u8		resv[3];	/* reserved - must be zero */
};

/*
 * ELS_FPIN - Fabric Performance Impact Notification
 */
struct fc_els_fpin {
	__u8		fpin_cmd;	/* command (0x16) */
	__u8		fpin_zero[3];	/* specified as zero - part of cmd */
	__be32		desc_len;	/* Length of Descriptor List (in bytes).
					 * Size of ELS excluding fpin_cmd,
					 * fpin_zero and desc_len fields.
					 */
	struct fc_tlv_desc	fpin_desc[0];	/* Descriptor list */
};

/* Diagnostic Function Descriptor - FPIN Registration */
struct fc_df_desc_fpin_reg {
	__be32		desc_tag;	/* FPIN Registration (0x00030001) */
	__be32		desc_len;	/* Length of Descriptor (in bytes).
					 * Size of descriptor excluding
					 * desc_tag and desc_len fields.
					 */
	__be32		count;		/* Number of desc_tags elements */
	__be32		desc_tags[0];	/* Array of Descriptor Tags.
					 * Each tag indicates a function
					 * supported by the N_Port (request)
					 * or by the  N_Port and Fabric
					 * Controller (reply; may be a subset
					 * of the request).
					 * See ELS_FN_DTAG_xxx for tag values.
					 */
};

/*
 * ELS_RDF - Register Diagnostic Functions
 */
struct fc_els_rdf {
	__u8		fpin_cmd;	/* command (0x19) */
	__u8		fpin_zero[3];	/* specified as zero - part of cmd */
	__be32		desc_len;	/* Length of Descriptor List (in bytes).
					 * Size of ELS excluding fpin_cmd,
					 * fpin_zero and desc_len fields.
					 */
	struct fc_tlv_desc	desc[0];	/* Descriptor list */
};

/*
 * ELS RDF LS_ACC Response.
 */
struct fc_els_rdf_resp {
	struct fc_els_ls_acc	acc_hdr;
	__be32			desc_list_len;	/* Length of response (in
						 * bytes). Excludes acc_hdr
						 * and desc_list_len fields.
						 */
	struct fc_els_lsri_desc	lsri;
	struct fc_tlv_desc	desc[0];	/* Supported Descriptor list */
};


/*
 * Diagnostic Capability Descriptors for EDC ELS
 */

/*
 * Diagnostic: Link Fault Capability Descriptor
 */
struct fc_diag_lnkflt_desc {
	__be32		desc_tag;	/* Descriptor Tag (0x0001000D) */
	__be32		desc_len;	/* Length of Descriptor (in bytes).
					 * Size of descriptor excluding
					 * desc_tag and desc_len fields.
					 * 12 bytes
					 */
	__be32		degrade_activate_threshold;
	__be32		degrade_deactivate_threshold;
	__be32		fec_degrade_interval;
};

enum fc_edc_cg_signal_cap_types {
	/* Note: Capability: bits 31:4 Rsvd; bits 3:0 are capabilities */
	EDC_CG_SIG_NOTSUPPORTED =	0x00, /* neither supported */
	EDC_CG_SIG_WARN_ONLY =		0x01,
	EDC_CG_SIG_WARN_ALARM =		0x02, /* both supported */
};

/*
 * Initializer useful for decoding table.
 * Please keep this in sync with the above definitions.
 */
#define FC_EDC_CG_SIGNAL_CAP_TYPES_INIT {				\
	{ EDC_CG_SIG_NOTSUPPORTED,	"Signaling Not Supported" },	\
	{ EDC_CG_SIG_WARN_ONLY,		"Warning Signal" },		\
	{ EDC_CG_SIG_WARN_ALARM,	"Warning and Alarm Signals" },	\
}

enum fc_diag_cg_sig_freq_types {
	EDC_CG_SIGFREQ_CNT_MIN =	1,	/* Min Frequency Count */
	EDC_CG_SIGFREQ_CNT_MAX =	999,	/* Max Frequency Count */

	EDC_CG_SIGFREQ_SEC =		0x1,	/* Units: seconds */
	EDC_CG_SIGFREQ_MSEC =		0x2,	/* Units: milliseconds */
};

struct fc_diag_cg_sig_freq {
	__be16		count;		/* Time between signals
					 * note: upper 6 bits rsvd
					 */
	__be16		units;		/* Time unit for count
					 * note: upper 12 bits rsvd
					 */
};

/*
 * Diagnostic: Congestion Signaling Capability Descriptor
 */
struct fc_diag_cg_sig_desc {
	__be32		desc_tag;	/* Descriptor Tag (0x0001000F) */
	__be32		desc_len;	/* Length of Descriptor (in bytes).
					 * Size of descriptor excluding
					 * desc_tag and desc_len fields.
					 * 16 bytes
					 */
	__be32				xmt_signal_capability;
	struct fc_diag_cg_sig_freq	xmt_signal_frequency;
	__be32				rcv_signal_capability;
	struct fc_diag_cg_sig_freq	rcv_signal_frequency;
};

/*
 * ELS_EDC - Exchange Diagnostic Capabilities
 */
struct fc_els_edc {
	__u8		edc_cmd;	/* command (0x17) */
	__u8		edc_zero[3];	/* specified as zero - part of cmd */
	__be32		desc_len;	/* Length of Descriptor List (in bytes).
					 * Size of ELS excluding edc_cmd,
					 * edc_zero and desc_len fields.
					 */
	struct fc_tlv_desc	desc[0];
					/* Diagnostic Descriptor list */
};

/*
 * ELS EDC LS_ACC Response.
 */
struct fc_els_edc_resp {
	struct fc_els_ls_acc	acc_hdr;
	__be32			desc_list_len;	/* Length of response (in
						 * bytes). Excludes acc_hdr
						 * and desc_list_len fields.
						 */
	struct fc_els_lsri_desc	lsri;
	struct fc_tlv_desc	desc[0];
				    /* Supported Diagnostic Descriptor list */
};


#endif /* _FC_ELS_H_ */
