#ifndef ISCSI_PARAMETERS_H
#define ISCSI_PARAMETERS_H

#include <linux/types.h>
#include <scsi/iscsi_proto.h>

struct iscsi_extra_response {
	char key[KEY_MAXLEN];
	char value[32];
	struct list_head er_list;
} ____cacheline_aligned;

struct iscsi_param {
	char *name;
	char *value;
	u8 set_param;
	u8 phase;
	u8 scope;
	u8 sender;
	u8 type;
	u8 use;
	u16 type_range;
	u32 state;
	struct list_head p_list;
} ____cacheline_aligned;

struct iscsi_conn;
struct iscsi_conn_ops;
struct iscsi_param_list;
struct iscsi_sess_ops;

extern int iscsi_login_rx_data(struct iscsi_conn *, char *, int);
extern int iscsi_login_tx_data(struct iscsi_conn *, char *, char *, int);
extern void iscsi_dump_conn_ops(struct iscsi_conn_ops *);
extern void iscsi_dump_sess_ops(struct iscsi_sess_ops *);
extern void iscsi_print_params(struct iscsi_param_list *);
extern int iscsi_create_default_params(struct iscsi_param_list **);
extern int iscsi_set_keys_to_negotiate(struct iscsi_param_list *, bool);
extern int iscsi_set_keys_irrelevant_for_discovery(struct iscsi_param_list *);
extern int iscsi_copy_param_list(struct iscsi_param_list **,
			struct iscsi_param_list *, int);
extern int iscsi_change_param_value(char *, struct iscsi_param_list *, int);
extern void iscsi_release_param_list(struct iscsi_param_list *);
extern struct iscsi_param *iscsi_find_param_from_key(char *, struct iscsi_param_list *);
extern int iscsi_extract_key_value(char *, char **, char **);
extern int iscsi_update_param_value(struct iscsi_param *, char *);
extern int iscsi_decode_text_input(u8, u8, char *, u32, struct iscsi_conn *);
extern int iscsi_encode_text_output(u8, u8, char *, u32 *,
			struct iscsi_param_list *, bool);
extern int iscsi_check_negotiated_keys(struct iscsi_param_list *);
extern void iscsi_set_connection_parameters(struct iscsi_conn_ops *,
			struct iscsi_param_list *);
extern void iscsi_set_session_parameters(struct iscsi_sess_ops *,
			struct iscsi_param_list *, int);

#define YES				"Yes"
#define NO				"No"
#define ALL				"All"
#define IRRELEVANT			"Irrelevant"
#define NONE				"None"
#define NOTUNDERSTOOD			"NotUnderstood"
#define REJECT				"Reject"

/*
 * The Parameter Names.
 */
#define AUTHMETHOD			"AuthMethod"
#define HEADERDIGEST			"HeaderDigest"
#define DATADIGEST			"DataDigest"
#define MAXCONNECTIONS			"MaxConnections"
#define SENDTARGETS			"SendTargets"
#define TARGETNAME			"TargetName"
#define INITIATORNAME			"InitiatorName"
#define TARGETALIAS			"TargetAlias"
#define INITIATORALIAS			"InitiatorAlias"
#define TARGETADDRESS			"TargetAddress"
#define TARGETPORTALGROUPTAG		"TargetPortalGroupTag"
#define INITIALR2T			"InitialR2T"
#define IMMEDIATEDATA			"ImmediateData"
#define MAXRECVDATASEGMENTLENGTH	"MaxRecvDataSegmentLength"
#define MAXXMITDATASEGMENTLENGTH	"MaxXmitDataSegmentLength"
#define MAXBURSTLENGTH			"MaxBurstLength"
#define FIRSTBURSTLENGTH		"FirstBurstLength"
#define DEFAULTTIME2WAIT		"DefaultTime2Wait"
#define DEFAULTTIME2RETAIN		"DefaultTime2Retain"
#define MAXOUTSTANDINGR2T		"MaxOutstandingR2T"
#define DATAPDUINORDER			"DataPDUInOrder"
#define DATASEQUENCEINORDER		"DataSequenceInOrder"
#define ERRORRECOVERYLEVEL		"ErrorRecoveryLevel"
#define SESSIONTYPE			"SessionType"
#define IFMARKER			"IFMarker"
#define OFMARKER			"OFMarker"
#define IFMARKINT			"IFMarkInt"
#define OFMARKINT			"OFMarkInt"
#define X_EXTENSIONKEY			"X-com.sbei.version"
#define X_EXTENSIONKEY_CISCO_NEW	"X-com.cisco.protocol"
#define X_EXTENSIONKEY_CISCO_OLD	"X-com.cisco.iscsi.draft"

/*
 * Parameter names of iSCSI Extentions for RDMA (iSER).  See RFC-5046
 */
#define RDMAEXTENSIONS			"RDMAExtensions"
#define INITIATORRECVDATASEGMENTLENGTH	"InitiatorRecvDataSegmentLength"
#define TARGETRECVDATASEGMENTLENGTH	"TargetRecvDataSegmentLength"

/*
 * For AuthMethod.
 */
#define KRB5				"KRB5"
#define SPKM1				"SPKM1"
#define SPKM2				"SPKM2"
#define SRP				"SRP"
#define CHAP				"CHAP"

/*
 * Initial values for Parameter Negotiation.
 */
#define INITIAL_AUTHMETHOD			CHAP
#define INITIAL_HEADERDIGEST			"CRC32C,None"
#define INITIAL_DATADIGEST			"CRC32C,None"
#define INITIAL_MAXCONNECTIONS			"1"
#define INITIAL_SENDTARGETS			ALL
#define INITIAL_TARGETNAME			"LIO.Target"
#define INITIAL_INITIATORNAME			"LIO.Initiator"
#define INITIAL_TARGETALIAS			"LIO Target"
#define INITIAL_INITIATORALIAS			"LIO Initiator"
#define INITIAL_TARGETADDRESS			"0.0.0.0:0000,0"
#define INITIAL_TARGETPORTALGROUPTAG		"1"
#define INITIAL_INITIALR2T			YES
#define INITIAL_IMMEDIATEDATA			YES
#define INITIAL_MAXRECVDATASEGMENTLENGTH	"8192"
/*
 * Match outgoing MXDSL default to incoming Open-iSCSI default
 */
#define INITIAL_MAXXMITDATASEGMENTLENGTH	"262144"
#define INITIAL_MAXBURSTLENGTH			"262144"
#define INITIAL_FIRSTBURSTLENGTH		"65536"
#define INITIAL_DEFAULTTIME2WAIT		"2"
#define INITIAL_DEFAULTTIME2RETAIN		"20"
#define INITIAL_MAXOUTSTANDINGR2T		"1"
#define INITIAL_DATAPDUINORDER			YES
#define INITIAL_DATASEQUENCEINORDER		YES
#define INITIAL_ERRORRECOVERYLEVEL		"0"
#define INITIAL_SESSIONTYPE			NORMAL
#define INITIAL_IFMARKER			NO
#define INITIAL_OFMARKER			NO
#define INITIAL_IFMARKINT			REJECT
#define INITIAL_OFMARKINT			REJECT

/*
 * Initial values for iSER parameters following RFC-5046 Section 6
 */
#define INITIAL_RDMAEXTENSIONS			NO
#define INITIAL_INITIATORRECVDATASEGMENTLENGTH	"262144"
#define INITIAL_TARGETRECVDATASEGMENTLENGTH	"8192"

/*
 * For [Header,Data]Digests.
 */
#define CRC32C				"CRC32C"

/*
 * For SessionType.
 */
#define DISCOVERY			"Discovery"
#define NORMAL				"Normal"

/*
 * struct iscsi_param->use
 */
#define USE_LEADING_ONLY		0x01
#define USE_INITIAL_ONLY		0x02
#define USE_ALL				0x04

#define IS_USE_LEADING_ONLY(p)		((p)->use & USE_LEADING_ONLY)
#define IS_USE_INITIAL_ONLY(p)		((p)->use & USE_INITIAL_ONLY)
#define IS_USE_ALL(p)			((p)->use & USE_ALL)

#define SET_USE_INITIAL_ONLY(p)		((p)->use |= USE_INITIAL_ONLY)

/*
 * struct iscsi_param->sender
 */
#define	SENDER_INITIATOR		0x01
#define SENDER_TARGET			0x02
#define SENDER_BOTH			0x03
/* Used in iscsi_check_key() */
#define SENDER_RECEIVER			0x04

#define IS_SENDER_INITIATOR(p)		((p)->sender & SENDER_INITIATOR)
#define IS_SENDER_TARGET(p)		((p)->sender & SENDER_TARGET)
#define IS_SENDER_BOTH(p)		((p)->sender & SENDER_BOTH)

/*
 * struct iscsi_param->scope
 */
#define SCOPE_CONNECTION_ONLY		0x01
#define SCOPE_SESSION_WIDE		0x02

#define IS_SCOPE_CONNECTION_ONLY(p)	((p)->scope & SCOPE_CONNECTION_ONLY)
#define IS_SCOPE_SESSION_WIDE(p)	((p)->scope & SCOPE_SESSION_WIDE)

/*
 * struct iscsi_param->phase
 */
#define PHASE_SECURITY			0x01
#define PHASE_OPERATIONAL		0x02
#define PHASE_DECLARATIVE		0x04
#define PHASE_FFP0			0x08

#define IS_PHASE_SECURITY(p)		((p)->phase & PHASE_SECURITY)
#define IS_PHASE_OPERATIONAL(p)		((p)->phase & PHASE_OPERATIONAL)
#define IS_PHASE_DECLARATIVE(p)		((p)->phase & PHASE_DECLARATIVE)
#define IS_PHASE_FFP0(p)		((p)->phase & PHASE_FFP0)

/*
 * struct iscsi_param->type
 */
#define TYPE_BOOL_AND			0x01
#define TYPE_BOOL_OR			0x02
#define TYPE_NUMBER			0x04
#define TYPE_NUMBER_RANGE		0x08
#define TYPE_STRING			0x10
#define TYPE_VALUE_LIST			0x20

#define IS_TYPE_BOOL_AND(p)		((p)->type & TYPE_BOOL_AND)
#define IS_TYPE_BOOL_OR(p)		((p)->type & TYPE_BOOL_OR)
#define IS_TYPE_NUMBER(p)		((p)->type & TYPE_NUMBER)
#define IS_TYPE_NUMBER_RANGE(p)		((p)->type & TYPE_NUMBER_RANGE)
#define IS_TYPE_STRING(p)		((p)->type & TYPE_STRING)
#define IS_TYPE_VALUE_LIST(p)		((p)->type & TYPE_VALUE_LIST)

/*
 * struct iscsi_param->type_range
 */
#define TYPERANGE_BOOL_AND		0x0001
#define TYPERANGE_BOOL_OR		0x0002
#define TYPERANGE_0_TO_2		0x0004
#define TYPERANGE_0_TO_3600		0x0008
#define TYPERANGE_0_TO_32767		0x0010
#define TYPERANGE_0_TO_65535		0x0020
#define TYPERANGE_1_TO_65535		0x0040
#define TYPERANGE_2_TO_3600		0x0080
#define TYPERANGE_512_TO_16777215	0x0100
#define TYPERANGE_AUTH			0x0200
#define TYPERANGE_DIGEST		0x0400
#define TYPERANGE_ISCSINAME		0x0800
#define TYPERANGE_SESSIONTYPE		0x1000
#define TYPERANGE_TARGETADDRESS		0x2000
#define TYPERANGE_UTF8			0x4000

#define IS_TYPERANGE_0_TO_2(p)		((p)->type_range & TYPERANGE_0_TO_2)
#define IS_TYPERANGE_0_TO_3600(p)	((p)->type_range & TYPERANGE_0_TO_3600)
#define IS_TYPERANGE_0_TO_32767(p)	((p)->type_range & TYPERANGE_0_TO_32767)
#define IS_TYPERANGE_0_TO_65535(p)	((p)->type_range & TYPERANGE_0_TO_65535)
#define IS_TYPERANGE_1_TO_65535(p)	((p)->type_range & TYPERANGE_1_TO_65535)
#define IS_TYPERANGE_2_TO_3600(p)	((p)->type_range & TYPERANGE_2_TO_3600)
#define IS_TYPERANGE_512_TO_16777215(p)	((p)->type_range & \
						TYPERANGE_512_TO_16777215)
#define IS_TYPERANGE_AUTH_PARAM(p)	((p)->type_range & TYPERANGE_AUTH)
#define IS_TYPERANGE_DIGEST_PARAM(p)	((p)->type_range & TYPERANGE_DIGEST)
#define IS_TYPERANGE_SESSIONTYPE(p)	((p)->type_range & \
						TYPERANGE_SESSIONTYPE)

/*
 * struct iscsi_param->state
 */
#define PSTATE_ACCEPTOR			0x01
#define PSTATE_NEGOTIATE		0x02
#define PSTATE_PROPOSER			0x04
#define PSTATE_IRRELEVANT		0x08
#define PSTATE_REJECT			0x10
#define PSTATE_REPLY_OPTIONAL		0x20
#define PSTATE_RESPONSE_GOT		0x40
#define PSTATE_RESPONSE_SENT		0x80

#define IS_PSTATE_ACCEPTOR(p)		((p)->state & PSTATE_ACCEPTOR)
#define IS_PSTATE_NEGOTIATE(p)		((p)->state & PSTATE_NEGOTIATE)
#define IS_PSTATE_PROPOSER(p)		((p)->state & PSTATE_PROPOSER)
#define IS_PSTATE_IRRELEVANT(p)		((p)->state & PSTATE_IRRELEVANT)
#define IS_PSTATE_REJECT(p)		((p)->state & PSTATE_REJECT)
#define IS_PSTATE_REPLY_OPTIONAL(p)	((p)->state & PSTATE_REPLY_OPTIONAL)
#define IS_PSTATE_RESPONSE_GOT(p)	((p)->state & PSTATE_RESPONSE_GOT)
#define IS_PSTATE_RESPONSE_SENT(p)	((p)->state & PSTATE_RESPONSE_SENT)

#define SET_PSTATE_ACCEPTOR(p)		((p)->state |= PSTATE_ACCEPTOR)
#define SET_PSTATE_NEGOTIATE(p)		((p)->state |= PSTATE_NEGOTIATE)
#define SET_PSTATE_PROPOSER(p)		((p)->state |= PSTATE_PROPOSER)
#define SET_PSTATE_IRRELEVANT(p)	((p)->state |= PSTATE_IRRELEVANT)
#define SET_PSTATE_REJECT(p)		((p)->state |= PSTATE_REJECT)
#define SET_PSTATE_REPLY_OPTIONAL(p)	((p)->state |= PSTATE_REPLY_OPTIONAL)
#define SET_PSTATE_RESPONSE_GOT(p)	((p)->state |= PSTATE_RESPONSE_GOT)
#define SET_PSTATE_RESPONSE_SENT(p)	((p)->state |= PSTATE_RESPONSE_SENT)

#endif /* ISCSI_PARAMETERS_H */
