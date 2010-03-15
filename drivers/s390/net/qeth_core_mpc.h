/*
 *  drivers/s390/net/qeth_core_mpc.h
 *
 *    Copyright IBM Corp. 2007
 *    Author(s): Frank Pavlic <fpavlic@de.ibm.com>,
 *		 Thomas Spatzier <tspat@de.ibm.com>,
 *		 Frank Blaschka <frank.blaschka@de.ibm.com>
 */

#ifndef __QETH_CORE_MPC_H__
#define __QETH_CORE_MPC_H__

#include <asm/qeth.h>

#define IPA_PDU_HEADER_SIZE	0x40
#define QETH_IPA_PDU_LEN_TOTAL(buffer) (buffer + 0x0e)
#define QETH_IPA_PDU_LEN_PDU1(buffer) (buffer + 0x26)
#define QETH_IPA_PDU_LEN_PDU2(buffer) (buffer + 0x29)
#define QETH_IPA_PDU_LEN_PDU3(buffer) (buffer + 0x3a)

extern unsigned char IPA_PDU_HEADER[];
#define QETH_IPA_CMD_DEST_ADDR(buffer) (buffer + 0x2c)

#define IPA_CMD_LENGTH	(IPA_PDU_HEADER_SIZE + sizeof(struct qeth_ipa_cmd))

#define QETH_SEQ_NO_LENGTH	4
#define QETH_MPC_TOKEN_LENGTH	4
#define QETH_MCL_LENGTH		4
#define OSA_ADDR_LEN		6

#define QETH_TIMEOUT		(10 * HZ)
#define QETH_IPA_TIMEOUT	(45 * HZ)
#define QETH_IDX_COMMAND_SEQNO	0xffff0000
#define SR_INFO_LEN		16

#define QETH_CLEAR_CHANNEL_PARM	-10
#define QETH_HALT_CHANNEL_PARM	-11
#define QETH_RCD_PARM -12

/*****************************************************************************/
/* IP Assist related definitions                                             */
/*****************************************************************************/
#define IPA_CMD_INITIATOR_HOST  0x00
#define IPA_CMD_INITIATOR_OSA   0x01
#define IPA_CMD_INITIATOR_HOST_REPLY  0x80
#define IPA_CMD_INITIATOR_OSA_REPLY   0x81
#define IPA_CMD_PRIM_VERSION_NO 0x01

enum qeth_card_types {
	QETH_CARD_TYPE_UNKNOWN = 0,
	QETH_CARD_TYPE_OSAE    = 10,
	QETH_CARD_TYPE_IQD     = 1234,
	QETH_CARD_TYPE_OSN     = 11,
};

#define QETH_MPC_DIFINFO_LEN_INDICATES_LINK_TYPE 0x18
/* only the first two bytes are looked at in qeth_get_cardname_short */
enum qeth_link_types {
	QETH_LINK_TYPE_FAST_ETH     = 0x01,
	QETH_LINK_TYPE_HSTR         = 0x02,
	QETH_LINK_TYPE_GBIT_ETH     = 0x03,
	QETH_LINK_TYPE_OSN          = 0x04,
	QETH_LINK_TYPE_10GBIT_ETH   = 0x10,
	QETH_LINK_TYPE_LANE_ETH100  = 0x81,
	QETH_LINK_TYPE_LANE_TR      = 0x82,
	QETH_LINK_TYPE_LANE_ETH1000 = 0x83,
	QETH_LINK_TYPE_LANE         = 0x88,
	QETH_LINK_TYPE_ATM_NATIVE   = 0x90,
};

enum qeth_tr_macaddr_modes {
	QETH_TR_MACADDR_NONCANONICAL = 0,
	QETH_TR_MACADDR_CANONICAL    = 1,
};

enum qeth_tr_broadcast_modes {
	QETH_TR_BROADCAST_ALLRINGS = 0,
	QETH_TR_BROADCAST_LOCAL    = 1,
};

/* these values match CHECKSUM_* in include/linux/skbuff.h */
enum qeth_checksum_types {
	SW_CHECKSUMMING = 0, /* TODO: set to bit flag used in IPA Command */
	HW_CHECKSUMMING = 1,
	NO_CHECKSUMMING = 2,
};
#define QETH_CHECKSUM_DEFAULT SW_CHECKSUMMING

/*
 * Routing stuff
 */
#define RESET_ROUTING_FLAG 0x10 /* indicate that routing type shall be set */
enum qeth_routing_types {
	/* TODO: set to bit flag used in IPA Command */
	NO_ROUTER		= 0,
	PRIMARY_ROUTER		= 1,
	SECONDARY_ROUTER	= 2,
	MULTICAST_ROUTER	= 3,
	PRIMARY_CONNECTOR	= 4,
	SECONDARY_CONNECTOR	= 5,
};

/* IPA Commands */
enum qeth_ipa_cmds {
	IPA_CMD_STARTLAN		= 0x01,
	IPA_CMD_STOPLAN			= 0x02,
	IPA_CMD_SETVMAC			= 0x21,
	IPA_CMD_DELVMAC			= 0x22,
	IPA_CMD_SETGMAC			= 0x23,
	IPA_CMD_DELGMAC			= 0x24,
	IPA_CMD_SETVLAN			= 0x25,
	IPA_CMD_DELVLAN			= 0x26,
	IPA_CMD_SETCCID			= 0x41,
	IPA_CMD_DELCCID			= 0x42,
	IPA_CMD_MODCCID			= 0x43,
	IPA_CMD_SETIP			= 0xb1,
	IPA_CMD_QIPASSIST		= 0xb2,
	IPA_CMD_SETASSPARMS		= 0xb3,
	IPA_CMD_SETIPM			= 0xb4,
	IPA_CMD_DELIPM			= 0xb5,
	IPA_CMD_SETRTG			= 0xb6,
	IPA_CMD_DELIP			= 0xb7,
	IPA_CMD_SETADAPTERPARMS		= 0xb8,
	IPA_CMD_SET_DIAG_ASS		= 0xb9,
	IPA_CMD_CREATE_ADDR		= 0xc3,
	IPA_CMD_DESTROY_ADDR		= 0xc4,
	IPA_CMD_REGISTER_LOCAL_ADDR	= 0xd1,
	IPA_CMD_UNREGISTER_LOCAL_ADDR	= 0xd2,
	IPA_CMD_UNKNOWN			= 0x00
};

enum qeth_ip_ass_cmds {
	IPA_CMD_ASS_START	= 0x0001,
	IPA_CMD_ASS_STOP	= 0x0002,
	IPA_CMD_ASS_CONFIGURE	= 0x0003,
	IPA_CMD_ASS_ENABLE	= 0x0004,
};

enum qeth_arp_process_subcmds {
	IPA_CMD_ASS_ARP_SET_NO_ENTRIES	= 0x0003,
	IPA_CMD_ASS_ARP_QUERY_CACHE	= 0x0004,
	IPA_CMD_ASS_ARP_ADD_ENTRY	= 0x0005,
	IPA_CMD_ASS_ARP_REMOVE_ENTRY	= 0x0006,
	IPA_CMD_ASS_ARP_FLUSH_CACHE	= 0x0007,
	IPA_CMD_ASS_ARP_QUERY_INFO	= 0x0104,
	IPA_CMD_ASS_ARP_QUERY_STATS	= 0x0204,
};


/* Return Codes for IPA Commands
 * according to OSA card Specs */

enum qeth_ipa_return_codes {
	IPA_RC_SUCCESS			= 0x0000,
	IPA_RC_NOTSUPP			= 0x0001,
	IPA_RC_IP_TABLE_FULL		= 0x0002,
	IPA_RC_UNKNOWN_ERROR		= 0x0003,
	IPA_RC_UNSUPPORTED_COMMAND	= 0x0004,
	IPA_RC_TRACE_ALREADY_ACTIVE	= 0x0005,
	IPA_RC_INVALID_FORMAT		= 0x0006,
	IPA_RC_DUP_IPV6_REMOTE		= 0x0008,
	IPA_RC_DUP_IPV6_HOME		= 0x0010,
	IPA_RC_UNREGISTERED_ADDR	= 0x0011,
	IPA_RC_NO_ID_AVAILABLE		= 0x0012,
	IPA_RC_ID_NOT_FOUND		= 0x0013,
	IPA_RC_INVALID_IP_VERSION	= 0x0020,
	IPA_RC_LAN_FRAME_MISMATCH	= 0x0040,
	IPA_RC_L2_UNSUPPORTED_CMD	= 0x2003,
	IPA_RC_L2_DUP_MAC		= 0x2005,
	IPA_RC_L2_ADDR_TABLE_FULL	= 0x2006,
	IPA_RC_L2_DUP_LAYER3_MAC	= 0x200a,
	IPA_RC_L2_GMAC_NOT_FOUND	= 0x200b,
	IPA_RC_L2_MAC_NOT_AUTH_BY_HYP	= 0x200c,
	IPA_RC_L2_MAC_NOT_AUTH_BY_ADP	= 0x200d,
	IPA_RC_L2_MAC_NOT_FOUND		= 0x2010,
	IPA_RC_L2_INVALID_VLAN_ID	= 0x2015,
	IPA_RC_L2_DUP_VLAN_ID		= 0x2016,
	IPA_RC_L2_VLAN_ID_NOT_FOUND	= 0x2017,
	IPA_RC_DATA_MISMATCH		= 0xe001,
	IPA_RC_INVALID_MTU_SIZE		= 0xe002,
	IPA_RC_INVALID_LANTYPE		= 0xe003,
	IPA_RC_INVALID_LANNUM		= 0xe004,
	IPA_RC_DUPLICATE_IP_ADDRESS	= 0xe005,
	IPA_RC_IP_ADDR_TABLE_FULL	= 0xe006,
	IPA_RC_LAN_PORT_STATE_ERROR	= 0xe007,
	IPA_RC_SETIP_NO_STARTLAN	= 0xe008,
	IPA_RC_SETIP_ALREADY_RECEIVED	= 0xe009,
	IPA_RC_IP_ADDR_ALREADY_USED	= 0xe00a,
	IPA_RC_MC_ADDR_NOT_FOUND	= 0xe00b,
	IPA_RC_SETIP_INVALID_VERSION	= 0xe00d,
	IPA_RC_UNSUPPORTED_SUBCMD	= 0xe00e,
	IPA_RC_ARP_ASSIST_NO_ENABLE	= 0xe00f,
	IPA_RC_PRIMARY_ALREADY_DEFINED	= 0xe010,
	IPA_RC_SECOND_ALREADY_DEFINED	= 0xe011,
	IPA_RC_INVALID_SETRTG_INDICATOR	= 0xe012,
	IPA_RC_MC_ADDR_ALREADY_DEFINED	= 0xe013,
	IPA_RC_LAN_OFFLINE		= 0xe080,
	IPA_RC_INVALID_IP_VERSION2	= 0xf001,
	IPA_RC_FFFF			= 0xffff
};
/* for DELIP */
#define IPA_RC_IP_ADDRESS_NOT_DEFINED	IPA_RC_PRIMARY_ALREADY_DEFINED
/* for SET_DIAGNOSTIC_ASSIST */
#define IPA_RC_INVALID_SUBCMD		IPA_RC_IP_TABLE_FULL
#define IPA_RC_HARDWARE_AUTH_ERROR	IPA_RC_UNKNOWN_ERROR

/* IPA function flags; each flag marks availability of respective function */
enum qeth_ipa_funcs {
	IPA_ARP_PROCESSING      = 0x00000001L,
	IPA_INBOUND_CHECKSUM    = 0x00000002L,
	IPA_OUTBOUND_CHECKSUM   = 0x00000004L,
	IPA_IP_FRAGMENTATION    = 0x00000008L,
	IPA_FILTERING           = 0x00000010L,
	IPA_IPV6                = 0x00000020L,
	IPA_MULTICASTING        = 0x00000040L,
	IPA_IP_REASSEMBLY       = 0x00000080L,
	IPA_QUERY_ARP_COUNTERS  = 0x00000100L,
	IPA_QUERY_ARP_ADDR_INFO = 0x00000200L,
	IPA_SETADAPTERPARMS     = 0x00000400L,
	IPA_VLAN_PRIO           = 0x00000800L,
	IPA_PASSTHRU            = 0x00001000L,
	IPA_FLUSH_ARP_SUPPORT   = 0x00002000L,
	IPA_FULL_VLAN           = 0x00004000L,
	IPA_INBOUND_PASSTHRU    = 0x00008000L,
	IPA_SOURCE_MAC          = 0x00010000L,
	IPA_OSA_MC_ROUTER       = 0x00020000L,
	IPA_QUERY_ARP_ASSIST	= 0x00040000L,
	IPA_INBOUND_TSO         = 0x00080000L,
	IPA_OUTBOUND_TSO        = 0x00100000L,
};

/* SETIP/DELIP IPA Command: ***************************************************/
enum qeth_ipa_setdelip_flags {
	QETH_IPA_SETDELIP_DEFAULT          = 0x00L, /* default */
	QETH_IPA_SETIP_VIPA_FLAG           = 0x01L, /* no grat. ARP */
	QETH_IPA_SETIP_TAKEOVER_FLAG       = 0x02L, /* nofail on grat. ARP */
	QETH_IPA_DELIP_ADDR_2_B_TAKEN_OVER = 0x20L,
	QETH_IPA_DELIP_VIPA_FLAG           = 0x40L,
	QETH_IPA_DELIP_ADDR_NEEDS_SETIP    = 0x80L,
};

/* SETADAPTER IPA Command: ****************************************************/
enum qeth_ipa_setadp_cmd {
	IPA_SETADP_QUERY_COMMANDS_SUPPORTED	= 0x00000001L,
	IPA_SETADP_ALTER_MAC_ADDRESS		= 0x00000002L,
	IPA_SETADP_ADD_DELETE_GROUP_ADDRESS	= 0x00000004L,
	IPA_SETADP_ADD_DELETE_FUNCTIONAL_ADDR	= 0x00000008L,
	IPA_SETADP_SET_ADDRESSING_MODE		= 0x00000010L,
	IPA_SETADP_SET_CONFIG_PARMS		= 0x00000020L,
	IPA_SETADP_SET_CONFIG_PARMS_EXTENDED	= 0x00000040L,
	IPA_SETADP_SET_BROADCAST_MODE		= 0x00000080L,
	IPA_SETADP_SEND_OSA_MESSAGE		= 0x00000100L,
	IPA_SETADP_SET_SNMP_CONTROL		= 0x00000200L,
	IPA_SETADP_QUERY_CARD_INFO		= 0x00000400L,
	IPA_SETADP_SET_PROMISC_MODE		= 0x00000800L,
	IPA_SETADP_SET_DIAG_ASSIST		= 0x00002000L,
	IPA_SETADP_SET_ACCESS_CONTROL		= 0x00010000L,
};
enum qeth_ipa_mac_ops {
	CHANGE_ADDR_READ_MAC		= 0,
	CHANGE_ADDR_REPLACE_MAC		= 1,
	CHANGE_ADDR_ADD_MAC		= 2,
	CHANGE_ADDR_DEL_MAC		= 4,
	CHANGE_ADDR_RESET_MAC		= 8,
};
enum qeth_ipa_addr_ops {
	CHANGE_ADDR_READ_ADDR		= 0,
	CHANGE_ADDR_ADD_ADDR		= 1,
	CHANGE_ADDR_DEL_ADDR		= 2,
	CHANGE_ADDR_FLUSH_ADDR_TABLE	= 4,
};
enum qeth_ipa_promisc_modes {
	SET_PROMISC_MODE_OFF		= 0,
	SET_PROMISC_MODE_ON		= 1,
};
enum qeth_ipa_isolation_modes {
	ISOLATION_MODE_NONE		= 0x00000000L,
	ISOLATION_MODE_FWD		= 0x00000001L,
	ISOLATION_MODE_DROP		= 0x00000002L,
};
enum qeth_ipa_set_access_mode_rc {
	SET_ACCESS_CTRL_RC_SUCCESS		= 0x0000,
	SET_ACCESS_CTRL_RC_NOT_SUPPORTED	= 0x0004,
	SET_ACCESS_CTRL_RC_ALREADY_NOT_ISOLATED	= 0x0008,
	SET_ACCESS_CTRL_RC_ALREADY_ISOLATED	= 0x0010,
	SET_ACCESS_CTRL_RC_NONE_SHARED_ADAPTER	= 0x0014,
	SET_ACCESS_CTRL_RC_ACTIVE_CHECKSUM_OFF	= 0x0018,
};


/* (SET)DELIP(M) IPA stuff ***************************************************/
struct qeth_ipacmd_setdelip4 {
	__u8   ip_addr[4];
	__u8   mask[4];
	__u32  flags;
} __attribute__ ((packed));

struct qeth_ipacmd_setdelip6 {
	__u8   ip_addr[16];
	__u8   mask[16];
	__u32  flags;
} __attribute__ ((packed));

struct qeth_ipacmd_setdelipm {
	__u8 mac[6];
	__u8 padding[2];
	__u8 ip6[12];
	__u8 ip4[4];
} __attribute__ ((packed));

struct qeth_ipacmd_layer2setdelmac {
	__u32 mac_length;
	__u8 mac[6];
} __attribute__ ((packed));

struct qeth_ipacmd_layer2setdelvlan {
	__u16 vlan_id;
} __attribute__ ((packed));


struct qeth_ipacmd_setassparms_hdr {
	__u32 assist_no;
	__u16 length;
	__u16 command_code;
	__u16 return_code;
	__u8 number_of_replies;
	__u8 seq_no;
} __attribute__((packed));

struct qeth_arp_query_data {
	__u16 request_bits;
	__u16 reply_bits;
	__u32 no_entries;
	char data;
} __attribute__((packed));

/* used as parameter for arp_query reply */
struct qeth_arp_query_info {
	__u32 udata_len;
	__u16 mask_bits;
	__u32 udata_offset;
	__u32 no_entries;
	char *udata;
};

/* SETASSPARMS IPA Command: */
struct qeth_ipacmd_setassparms {
	struct qeth_ipacmd_setassparms_hdr hdr;
	union {
		__u32 flags_32bit;
		struct qeth_arp_cache_entry add_arp_entry;
		struct qeth_arp_query_data query_arp;
		__u8 ip[16];
	} data;
} __attribute__ ((packed));


/* SETRTG IPA Command:    ****************************************************/
struct qeth_set_routing {
	__u8 type;
};

/* SETADAPTERPARMS IPA Command:    *******************************************/
struct qeth_query_cmds_supp {
	__u32 no_lantypes_supp;
	__u8 lan_type;
	__u8 reserved1[3];
	__u32 supported_cmds;
	__u8 reserved2[8];
} __attribute__ ((packed));

struct qeth_change_addr {
	__u32 cmd;
	__u32 addr_size;
	__u32 no_macs;
	__u8 addr[OSA_ADDR_LEN];
} __attribute__ ((packed));


struct qeth_snmp_cmd {
	__u8  token[16];
	__u32 request;
	__u32 interface;
	__u32 returncode;
	__u32 firmwarelevel;
	__u32 seqno;
	__u8  data;
} __attribute__ ((packed));

struct qeth_snmp_ureq_hdr {
	__u32   data_len;
	__u32   req_len;
	__u32   reserved1;
	__u32   reserved2;
} __attribute__ ((packed));

struct qeth_snmp_ureq {
	struct qeth_snmp_ureq_hdr hdr;
	struct qeth_snmp_cmd cmd;
} __attribute__((packed));

/* SET_ACCESS_CONTROL: same format for request and reply */
struct qeth_set_access_ctrl {
	__u32 subcmd_code;
} __attribute__((packed));

struct qeth_ipacmd_setadpparms_hdr {
	__u32 supp_hw_cmds;
	__u32 reserved1;
	__u16 cmdlength;
	__u16 reserved2;
	__u32 command_code;
	__u16 return_code;
	__u8  used_total;
	__u8  seq_no;
	__u32 reserved3;
} __attribute__ ((packed));

struct qeth_ipacmd_setadpparms {
	struct qeth_ipacmd_setadpparms_hdr hdr;
	union {
		struct qeth_query_cmds_supp query_cmds_supp;
		struct qeth_change_addr change_addr;
		struct qeth_snmp_cmd snmp;
		struct qeth_set_access_ctrl set_access_ctrl;
		__u32 mode;
	} data;
} __attribute__ ((packed));

/* CREATE_ADDR IPA Command:    ***********************************************/
struct qeth_create_destroy_address {
	__u8 unique_id[8];
} __attribute__ ((packed));

/* SET DIAGNOSTIC ASSIST IPA Command:	 *************************************/

enum qeth_diags_cmds {
	QETH_DIAGS_CMD_QUERY	= 0x0001,
	QETH_DIAGS_CMD_TRAP	= 0x0002,
	QETH_DIAGS_CMD_TRACE	= 0x0004,
	QETH_DIAGS_CMD_NOLOG	= 0x0008,
	QETH_DIAGS_CMD_DUMP	= 0x0010,
};

enum qeth_diags_trace_types {
	QETH_DIAGS_TYPE_HIPERSOCKET	= 0x02,
};

enum qeth_diags_trace_cmds {
	QETH_DIAGS_CMD_TRACE_ENABLE	= 0x0001,
	QETH_DIAGS_CMD_TRACE_DISABLE	= 0x0002,
	QETH_DIAGS_CMD_TRACE_MODIFY	= 0x0004,
	QETH_DIAGS_CMD_TRACE_REPLACE	= 0x0008,
	QETH_DIAGS_CMD_TRACE_QUERY	= 0x0010,
};

struct qeth_ipacmd_diagass {
	__u32  host_tod2;
	__u32:32;
	__u16  subcmd_len;
	__u16:16;
	__u32  subcmd;
	__u8   type;
	__u8   action;
	__u16  options;
	__u32:32;
} __attribute__ ((packed));

/* Header for each IPA command */
struct qeth_ipacmd_hdr {
	__u8   command;
	__u8   initiator;
	__u16  seqno;
	__u16  return_code;
	__u8   adapter_type;
	__u8   rel_adapter_no;
	__u8   prim_version_no;
	__u8   param_count;
	__u16  prot_version;
	__u32  ipa_supported;
	__u32  ipa_enabled;
} __attribute__ ((packed));

/* The IPA command itself */
struct qeth_ipa_cmd {
	struct qeth_ipacmd_hdr hdr;
	union {
		struct qeth_ipacmd_setdelip4		setdelip4;
		struct qeth_ipacmd_setdelip6		setdelip6;
		struct qeth_ipacmd_setdelipm		setdelipm;
		struct qeth_ipacmd_setassparms		setassparms;
		struct qeth_ipacmd_layer2setdelmac	setdelmac;
		struct qeth_ipacmd_layer2setdelvlan	setdelvlan;
		struct qeth_create_destroy_address	create_destroy_addr;
		struct qeth_ipacmd_setadpparms		setadapterparms;
		struct qeth_set_routing			setrtg;
		struct qeth_ipacmd_diagass		diagass;
	} data;
} __attribute__ ((packed));

/*
 * special command for ARP processing.
 * this is not included in setassparms command before, because we get
 * problem with the size of struct qeth_ipacmd_setassparms otherwise
 */
enum qeth_ipa_arp_return_codes {
	QETH_IPA_ARP_RC_SUCCESS      = 0x0000,
	QETH_IPA_ARP_RC_FAILED       = 0x0001,
	QETH_IPA_ARP_RC_NOTSUPP      = 0x0002,
	QETH_IPA_ARP_RC_OUT_OF_RANGE = 0x0003,
	QETH_IPA_ARP_RC_Q_NOTSUPP    = 0x0004,
	QETH_IPA_ARP_RC_Q_NO_DATA    = 0x0008,
};

extern char *qeth_get_ipa_msg(enum qeth_ipa_return_codes rc);
extern char *qeth_get_ipa_cmd_name(enum qeth_ipa_cmds cmd);

#define QETH_SETASS_BASE_LEN (sizeof(struct qeth_ipacmd_hdr) + \
			       sizeof(struct qeth_ipacmd_setassparms_hdr))
#define QETH_IPA_ARP_DATA_POS(buffer) (buffer + IPA_PDU_HEADER_SIZE + \
				       QETH_SETASS_BASE_LEN)
#define QETH_SETADP_BASE_LEN (sizeof(struct qeth_ipacmd_hdr) + \
			      sizeof(struct qeth_ipacmd_setadpparms_hdr))
#define QETH_SNMP_SETADP_CMDLENGTH 16

#define QETH_ARP_DATA_SIZE 3968
#define QETH_ARP_CMD_LEN (QETH_ARP_DATA_SIZE + 8)
/* Helper functions */
#define IS_IPA_REPLY(cmd) ((cmd->hdr.initiator == IPA_CMD_INITIATOR_HOST) || \
			   (cmd->hdr.initiator == IPA_CMD_INITIATOR_OSA_REPLY))

/*****************************************************************************/
/* END OF   IP Assist related definitions                                    */
/*****************************************************************************/


extern unsigned char WRITE_CCW[];
extern unsigned char READ_CCW[];

extern unsigned char CM_ENABLE[];
#define CM_ENABLE_SIZE 0x63
#define QETH_CM_ENABLE_ISSUER_RM_TOKEN(buffer) (buffer + 0x2c)
#define QETH_CM_ENABLE_FILTER_TOKEN(buffer) (buffer + 0x53)
#define QETH_CM_ENABLE_USER_DATA(buffer) (buffer + 0x5b)

#define QETH_CM_ENABLE_RESP_FILTER_TOKEN(buffer) \
		(PDU_ENCAPSULATION(buffer) + 0x13)


extern unsigned char CM_SETUP[];
#define CM_SETUP_SIZE 0x64
#define QETH_CM_SETUP_DEST_ADDR(buffer) (buffer + 0x2c)
#define QETH_CM_SETUP_CONNECTION_TOKEN(buffer) (buffer + 0x51)
#define QETH_CM_SETUP_FILTER_TOKEN(buffer) (buffer + 0x5a)

#define QETH_CM_SETUP_RESP_DEST_ADDR(buffer) \
		(PDU_ENCAPSULATION(buffer) + 0x1a)

extern unsigned char ULP_ENABLE[];
#define ULP_ENABLE_SIZE 0x6b
#define QETH_ULP_ENABLE_LINKNUM(buffer) (buffer + 0x61)
#define QETH_ULP_ENABLE_DEST_ADDR(buffer) (buffer + 0x2c)
#define QETH_ULP_ENABLE_FILTER_TOKEN(buffer) (buffer + 0x53)
#define QETH_ULP_ENABLE_PORTNAME_AND_LL(buffer) (buffer + 0x62)
#define QETH_ULP_ENABLE_RESP_FILTER_TOKEN(buffer) \
		(PDU_ENCAPSULATION(buffer) + 0x13)
#define QETH_ULP_ENABLE_RESP_MAX_MTU(buffer) \
		(PDU_ENCAPSULATION(buffer) + 0x1f)
#define QETH_ULP_ENABLE_RESP_DIFINFO_LEN(buffer) \
		(PDU_ENCAPSULATION(buffer) + 0x17)
#define QETH_ULP_ENABLE_RESP_LINK_TYPE(buffer) \
		(PDU_ENCAPSULATION(buffer) + 0x2b)
/* Layer 2 definitions */
#define QETH_PROT_LAYER2 0x08
#define QETH_PROT_TCPIP  0x03
#define QETH_PROT_OSN2   0x0a
#define QETH_ULP_ENABLE_PROT_TYPE(buffer) (buffer + 0x50)
#define QETH_IPA_CMD_PROT_TYPE(buffer) (buffer + 0x19)

extern unsigned char ULP_SETUP[];
#define ULP_SETUP_SIZE 0x6c
#define QETH_ULP_SETUP_DEST_ADDR(buffer) (buffer + 0x2c)
#define QETH_ULP_SETUP_CONNECTION_TOKEN(buffer) (buffer + 0x51)
#define QETH_ULP_SETUP_FILTER_TOKEN(buffer) (buffer + 0x5a)
#define QETH_ULP_SETUP_CUA(buffer) (buffer + 0x68)
#define QETH_ULP_SETUP_REAL_DEVADDR(buffer) (buffer + 0x6a)

#define QETH_ULP_SETUP_RESP_CONNECTION_TOKEN(buffer) \
		(PDU_ENCAPSULATION(buffer) + 0x1a)


extern unsigned char DM_ACT[];
#define DM_ACT_SIZE 0x55
#define QETH_DM_ACT_DEST_ADDR(buffer) (buffer + 0x2c)
#define QETH_DM_ACT_CONNECTION_TOKEN(buffer) (buffer + 0x51)



#define QETH_TRANSPORT_HEADER_SEQ_NO(buffer) (buffer + 4)
#define QETH_PDU_HEADER_SEQ_NO(buffer) (buffer + 0x1c)
#define QETH_PDU_HEADER_ACK_SEQ_NO(buffer) (buffer + 0x20)

extern unsigned char IDX_ACTIVATE_READ[];
extern unsigned char IDX_ACTIVATE_WRITE[];

#define IDX_ACTIVATE_SIZE	0x22
#define QETH_IDX_ACT_PNO(buffer) (buffer+0x0b)
#define QETH_IDX_ACT_ISSUER_RM_TOKEN(buffer) (buffer + 0x0c)
#define QETH_IDX_NO_PORTNAME_REQUIRED(buffer) ((buffer)[0x0b] & 0x80)
#define QETH_IDX_ACT_FUNC_LEVEL(buffer) (buffer + 0x10)
#define QETH_IDX_ACT_DATASET_NAME(buffer) (buffer + 0x16)
#define QETH_IDX_ACT_QDIO_DEV_CUA(buffer) (buffer + 0x1e)
#define QETH_IDX_ACT_QDIO_DEV_REALADDR(buffer) (buffer + 0x20)
#define QETH_IS_IDX_ACT_POS_REPLY(buffer) (((buffer)[0x08] & 3) == 2)
#define QETH_IDX_REPLY_LEVEL(buffer) (buffer + 0x12)
#define QETH_IDX_ACT_CAUSE_CODE(buffer) (buffer)[0x09]

#define PDU_ENCAPSULATION(buffer) \
	(buffer + *(buffer + (*(buffer + 0x0b)) + \
	 *(buffer + *(buffer + 0x0b) + 0x11) + 0x07))

#define IS_IPA(buffer) \
	((buffer) && \
	 (*(buffer + ((*(buffer + 0x0b)) + 4)) == 0xc1))

#define ADDR_FRAME_TYPE_DIX 1
#define ADDR_FRAME_TYPE_802_3 2
#define ADDR_FRAME_TYPE_TR_WITHOUT_SR 0x10
#define ADDR_FRAME_TYPE_TR_WITH_SR 0x20

#endif
