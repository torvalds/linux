#ifndef ISCSI_TARGET_CORE_H
#define ISCSI_TARGET_CORE_H

#include <linux/in.h>
#include <linux/configfs.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/iscsi_proto.h>
#include <target/target_core_base.h>

#define ISCSIT_VERSION			"v4.1.0"
#define ISCSI_MAX_DATASN_MISSING_COUNT	16
#define ISCSI_TX_THREAD_TCP_TIMEOUT	2
#define ISCSI_RX_THREAD_TCP_TIMEOUT	2
#define SECONDS_FOR_ASYNC_LOGOUT	10
#define SECONDS_FOR_ASYNC_TEXT		10
#define SECONDS_FOR_LOGOUT_COMP		15
#define WHITE_SPACE			" \t\v\f\n\r"
#define ISCSIT_MIN_TAGS			16
#define ISCSIT_EXTRA_TAGS		8
#define ISCSIT_TCP_BACKLOG		256

/* struct iscsi_node_attrib sanity values */
#define NA_DATAOUT_TIMEOUT		3
#define NA_DATAOUT_TIMEOUT_MAX		60
#define NA_DATAOUT_TIMEOUT_MIX		2
#define NA_DATAOUT_TIMEOUT_RETRIES	5
#define NA_DATAOUT_TIMEOUT_RETRIES_MAX	15
#define NA_DATAOUT_TIMEOUT_RETRIES_MIN	1
#define NA_NOPIN_TIMEOUT		15
#define NA_NOPIN_TIMEOUT_MAX		60
#define NA_NOPIN_TIMEOUT_MIN		3
#define NA_NOPIN_RESPONSE_TIMEOUT	30
#define NA_NOPIN_RESPONSE_TIMEOUT_MAX	60
#define NA_NOPIN_RESPONSE_TIMEOUT_MIN	3
#define NA_RANDOM_DATAIN_PDU_OFFSETS	0
#define NA_RANDOM_DATAIN_SEQ_OFFSETS	0
#define NA_RANDOM_R2T_OFFSETS		0

/* struct iscsi_tpg_attrib sanity values */
#define TA_AUTHENTICATION		1
#define TA_LOGIN_TIMEOUT		15
#define TA_LOGIN_TIMEOUT_MAX		30
#define TA_LOGIN_TIMEOUT_MIN		5
#define TA_NETIF_TIMEOUT		2
#define TA_NETIF_TIMEOUT_MAX		15
#define TA_NETIF_TIMEOUT_MIN		2
#define TA_GENERATE_NODE_ACLS		0
#define TA_DEFAULT_CMDSN_DEPTH		64
#define TA_DEFAULT_CMDSN_DEPTH_MAX	512
#define TA_DEFAULT_CMDSN_DEPTH_MIN	1
#define TA_CACHE_DYNAMIC_ACLS		0
/* Enabled by default in demo mode (generic_node_acls=1) */
#define TA_DEMO_MODE_WRITE_PROTECT	1
/* Disabled by default in production mode w/ explict ACLs */
#define TA_PROD_MODE_WRITE_PROTECT	0
#define TA_DEMO_MODE_DISCOVERY		1
#define TA_DEFAULT_ERL			0
#define TA_CACHE_CORE_NPS		0


#define ISCSI_IOV_DATA_BUFFER		5

enum iscsit_transport_type {
	ISCSI_TCP				= 0,
	ISCSI_SCTP_TCP				= 1,
	ISCSI_SCTP_UDP				= 2,
	ISCSI_IWARP_TCP				= 3,
	ISCSI_IWARP_SCTP			= 4,
	ISCSI_INFINIBAND			= 5,
};

/* RFC-3720 7.1.4  Standard Connection State Diagram for a Target */
enum target_conn_state_table {
	TARG_CONN_STATE_FREE			= 0x1,
	TARG_CONN_STATE_XPT_UP			= 0x3,
	TARG_CONN_STATE_IN_LOGIN		= 0x4,
	TARG_CONN_STATE_LOGGED_IN		= 0x5,
	TARG_CONN_STATE_IN_LOGOUT		= 0x6,
	TARG_CONN_STATE_LOGOUT_REQUESTED	= 0x7,
	TARG_CONN_STATE_CLEANUP_WAIT		= 0x8,
};

/* RFC-3720 7.3.2  Session State Diagram for a Target */
enum target_sess_state_table {
	TARG_SESS_STATE_FREE			= 0x1,
	TARG_SESS_STATE_ACTIVE			= 0x2,
	TARG_SESS_STATE_LOGGED_IN		= 0x3,
	TARG_SESS_STATE_FAILED			= 0x4,
	TARG_SESS_STATE_IN_CONTINUE		= 0x5,
};

/* struct iscsi_data_count->type */
enum data_count_type {
	ISCSI_RX_DATA	= 1,
	ISCSI_TX_DATA	= 2,
};

/* struct iscsi_datain_req->dr_complete */
enum datain_req_comp_table {
	DATAIN_COMPLETE_NORMAL			= 1,
	DATAIN_COMPLETE_WITHIN_COMMAND_RECOVERY = 2,
	DATAIN_COMPLETE_CONNECTION_RECOVERY	= 3,
};

/* struct iscsi_datain_req->recovery */
enum datain_req_rec_table {
	DATAIN_WITHIN_COMMAND_RECOVERY		= 1,
	DATAIN_CONNECTION_RECOVERY		= 2,
};

/* struct iscsi_portal_group->state */
enum tpg_state_table {
	TPG_STATE_FREE				= 0,
	TPG_STATE_ACTIVE			= 1,
	TPG_STATE_INACTIVE			= 2,
	TPG_STATE_COLD_RESET			= 3,
};

/* struct iscsi_tiqn->tiqn_state */
enum tiqn_state_table {
	TIQN_STATE_ACTIVE			= 1,
	TIQN_STATE_SHUTDOWN			= 2,
};

/* struct iscsi_cmd->cmd_flags */
enum cmd_flags_table {
	ICF_GOT_LAST_DATAOUT			= 0x00000001,
	ICF_GOT_DATACK_SNACK			= 0x00000002,
	ICF_NON_IMMEDIATE_UNSOLICITED_DATA	= 0x00000004,
	ICF_SENT_LAST_R2T			= 0x00000008,
	ICF_WITHIN_COMMAND_RECOVERY		= 0x00000010,
	ICF_CONTIG_MEMORY			= 0x00000020,
	ICF_ATTACHED_TO_RQUEUE			= 0x00000040,
	ICF_OOO_CMDSN				= 0x00000080,
	IFC_SENDTARGETS_ALL			= 0x00000100,
	IFC_SENDTARGETS_SINGLE			= 0x00000200,
};

/* struct iscsi_cmd->i_state */
enum cmd_i_state_table {
	ISTATE_NO_STATE			= 0,
	ISTATE_NEW_CMD			= 1,
	ISTATE_DEFERRED_CMD		= 2,
	ISTATE_UNSOLICITED_DATA		= 3,
	ISTATE_RECEIVE_DATAOUT		= 4,
	ISTATE_RECEIVE_DATAOUT_RECOVERY	= 5,
	ISTATE_RECEIVED_LAST_DATAOUT	= 6,
	ISTATE_WITHIN_DATAOUT_RECOVERY	= 7,
	ISTATE_IN_CONNECTION_RECOVERY	= 8,
	ISTATE_RECEIVED_TASKMGT		= 9,
	ISTATE_SEND_ASYNCMSG		= 10,
	ISTATE_SENT_ASYNCMSG		= 11,
	ISTATE_SEND_DATAIN		= 12,
	ISTATE_SEND_LAST_DATAIN		= 13,
	ISTATE_SENT_LAST_DATAIN		= 14,
	ISTATE_SEND_LOGOUTRSP		= 15,
	ISTATE_SENT_LOGOUTRSP		= 16,
	ISTATE_SEND_NOPIN		= 17,
	ISTATE_SENT_NOPIN		= 18,
	ISTATE_SEND_REJECT		= 19,
	ISTATE_SENT_REJECT		= 20,
	ISTATE_SEND_R2T			= 21,
	ISTATE_SENT_R2T			= 22,
	ISTATE_SEND_R2T_RECOVERY	= 23,
	ISTATE_SENT_R2T_RECOVERY	= 24,
	ISTATE_SEND_LAST_R2T		= 25,
	ISTATE_SENT_LAST_R2T		= 26,
	ISTATE_SEND_LAST_R2T_RECOVERY	= 27,
	ISTATE_SENT_LAST_R2T_RECOVERY	= 28,
	ISTATE_SEND_STATUS		= 29,
	ISTATE_SEND_STATUS_BROKEN_PC	= 30,
	ISTATE_SENT_STATUS		= 31,
	ISTATE_SEND_STATUS_RECOVERY	= 32,
	ISTATE_SENT_STATUS_RECOVERY	= 33,
	ISTATE_SEND_TASKMGTRSP		= 34,
	ISTATE_SENT_TASKMGTRSP		= 35,
	ISTATE_SEND_TEXTRSP		= 36,
	ISTATE_SENT_TEXTRSP		= 37,
	ISTATE_SEND_NOPIN_WANT_RESPONSE	= 38,
	ISTATE_SENT_NOPIN_WANT_RESPONSE	= 39,
	ISTATE_SEND_NOPIN_NO_RESPONSE	= 40,
	ISTATE_REMOVE			= 41,
	ISTATE_FREE			= 42,
};

/* Used for iscsi_recover_cmdsn() return values */
enum recover_cmdsn_ret_table {
	CMDSN_ERROR_CANNOT_RECOVER	= -1,
	CMDSN_NORMAL_OPERATION		= 0,
	CMDSN_LOWER_THAN_EXP		= 1,
	CMDSN_HIGHER_THAN_EXP		= 2,
	CMDSN_MAXCMDSN_OVERRUN		= 3,
};

/* Used for iscsi_handle_immediate_data() return values */
enum immedate_data_ret_table {
	IMMEDIATE_DATA_CANNOT_RECOVER	= -1,
	IMMEDIATE_DATA_NORMAL_OPERATION = 0,
	IMMEDIATE_DATA_ERL1_CRC_FAILURE = 1,
};

/* Used for iscsi_decide_dataout_action() return values */
enum dataout_action_ret_table {
	DATAOUT_CANNOT_RECOVER		= -1,
	DATAOUT_NORMAL			= 0,
	DATAOUT_SEND_R2T		= 1,
	DATAOUT_SEND_TO_TRANSPORT	= 2,
	DATAOUT_WITHIN_COMMAND_RECOVERY = 3,
};

/* Used for struct iscsi_node_auth->naf_flags */
enum naf_flags_table {
	NAF_USERID_SET			= 0x01,
	NAF_PASSWORD_SET		= 0x02,
	NAF_USERID_IN_SET		= 0x04,
	NAF_PASSWORD_IN_SET		= 0x08,
};

/* Used by various struct timer_list to manage iSCSI specific state */
enum iscsi_timer_flags_table {
	ISCSI_TF_RUNNING		= 0x01,
	ISCSI_TF_STOP			= 0x02,
	ISCSI_TF_EXPIRED		= 0x04,
};

/* Used for struct iscsi_np->np_flags */
enum np_flags_table {
	NPF_IP_NETWORK		= 0x00,
};

/* Used for struct iscsi_np->np_thread_state */
enum np_thread_state_table {
	ISCSI_NP_THREAD_ACTIVE		= 1,
	ISCSI_NP_THREAD_INACTIVE	= 2,
	ISCSI_NP_THREAD_RESET		= 3,
	ISCSI_NP_THREAD_SHUTDOWN	= 4,
	ISCSI_NP_THREAD_EXIT		= 5,
};

struct iscsi_conn_ops {
	u8	HeaderDigest;			/* [0,1] == [None,CRC32C] */
	u8	DataDigest;			/* [0,1] == [None,CRC32C] */
	u32	MaxRecvDataSegmentLength;	/* [512..2**24-1] */
	u32	MaxXmitDataSegmentLength;	/* [512..2**24-1] */
	u8	OFMarker;			/* [0,1] == [No,Yes] */
	u8	IFMarker;			/* [0,1] == [No,Yes] */
	u32	OFMarkInt;			/* [1..65535] */
	u32	IFMarkInt;			/* [1..65535] */
	/*
	 * iSER specific connection parameters
	 */
	u32	InitiatorRecvDataSegmentLength;	/* [512..2**24-1] */
	u32	TargetRecvDataSegmentLength;	/* [512..2**24-1] */
};

struct iscsi_sess_ops {
	char	InitiatorName[224];
	char	InitiatorAlias[256];
	char	TargetName[224];
	char	TargetAlias[256];
	char	TargetAddress[256];
	u16	TargetPortalGroupTag;		/* [0..65535] */
	u16	MaxConnections;			/* [1..65535] */
	u8	InitialR2T;			/* [0,1] == [No,Yes] */
	u8	ImmediateData;			/* [0,1] == [No,Yes] */
	u32	MaxBurstLength;			/* [512..2**24-1] */
	u32	FirstBurstLength;		/* [512..2**24-1] */
	u16	DefaultTime2Wait;		/* [0..3600] */
	u16	DefaultTime2Retain;		/* [0..3600] */
	u16	MaxOutstandingR2T;		/* [1..65535] */
	u8	DataPDUInOrder;			/* [0,1] == [No,Yes] */
	u8	DataSequenceInOrder;		/* [0,1] == [No,Yes] */
	u8	ErrorRecoveryLevel;		/* [0..2] */
	u8	SessionType;			/* [0,1] == [Normal,Discovery]*/
	/*
	 * iSER specific session parameters
	 */
	u8	RDMAExtensions;			/* [0,1] == [No,Yes] */
};

struct iscsi_queue_req {
	int			state;
	struct iscsi_cmd	*cmd;
	struct list_head	qr_list;
};

struct iscsi_data_count {
	int			data_length;
	int			sync_and_steering;
	enum data_count_type	type;
	u32			iov_count;
	u32			ss_iov_count;
	u32			ss_marker_count;
	struct kvec		*iov;
};

struct iscsi_param_list {
	bool			iser;
	struct list_head	param_list;
	struct list_head	extra_response_list;
};

struct iscsi_datain_req {
	enum datain_req_comp_table dr_complete;
	int			generate_recovery_values;
	enum datain_req_rec_table recovery;
	u32			begrun;
	u32			runlength;
	u32			data_length;
	u32			data_offset;
	u32			data_sn;
	u32			next_burst_len;
	u32			read_data_done;
	u32			seq_send_order;
	struct list_head	cmd_datain_node;
} ____cacheline_aligned;

struct iscsi_ooo_cmdsn {
	u16			cid;
	u32			batch_count;
	u32			cmdsn;
	u32			exp_cmdsn;
	struct iscsi_cmd	*cmd;
	struct list_head	ooo_list;
} ____cacheline_aligned;

struct iscsi_datain {
	u8			flags;
	u32			data_sn;
	u32			length;
	u32			offset;
} ____cacheline_aligned;

struct iscsi_r2t {
	int			seq_complete;
	int			recovery_r2t;
	int			sent_r2t;
	u32			r2t_sn;
	u32			offset;
	u32			targ_xfer_tag;
	u32			xfer_len;
	struct list_head	r2t_list;
} ____cacheline_aligned;

struct iscsi_cmd {
	enum iscsi_timer_flags_table dataout_timer_flags;
	/* DataOUT timeout retries */
	u8			dataout_timeout_retries;
	/* Within command recovery count */
	u8			error_recovery_count;
	/* iSCSI dependent state for out or order CmdSNs */
	enum cmd_i_state_table	deferred_i_state;
	/* iSCSI dependent state */
	enum cmd_i_state_table	i_state;
	/* Command is an immediate command (ISCSI_OP_IMMEDIATE set) */
	u8			immediate_cmd;
	/* Immediate data present */
	u8			immediate_data;
	/* iSCSI Opcode */
	u8			iscsi_opcode;
	/* iSCSI Response Code */
	u8			iscsi_response;
	/* Logout reason when iscsi_opcode == ISCSI_INIT_LOGOUT_CMND */
	u8			logout_reason;
	/* Logout response code when iscsi_opcode == ISCSI_INIT_LOGOUT_CMND */
	u8			logout_response;
	/* MaxCmdSN has been incremented */
	u8			maxcmdsn_inc;
	/* Immediate Unsolicited Dataout */
	u8			unsolicited_data;
	/* Reject reason code */
	u8			reject_reason;
	/* CID contained in logout PDU when opcode == ISCSI_INIT_LOGOUT_CMND */
	u16			logout_cid;
	/* Command flags */
	enum cmd_flags_table	cmd_flags;
	/* Initiator Task Tag assigned from Initiator */
	itt_t			init_task_tag;
	/* Target Transfer Tag assigned from Target */
	u32			targ_xfer_tag;
	/* CmdSN assigned from Initiator */
	u32			cmd_sn;
	/* ExpStatSN assigned from Initiator */
	u32			exp_stat_sn;
	/* StatSN assigned to this ITT */
	u32			stat_sn;
	/* DataSN Counter */
	u32			data_sn;
	/* R2TSN Counter */
	u32			r2t_sn;
	/* Last DataSN acknowledged via DataAck SNACK */
	u32			acked_data_sn;
	/* Used for echoing NOPOUT ping data */
	u32			buf_ptr_size;
	/* Used to store DataDigest */
	u32			data_crc;
	/* Counter for MaxOutstandingR2T */
	u32			outstanding_r2ts;
	/* Next R2T Offset when DataSequenceInOrder=Yes */
	u32			r2t_offset;
	/* Iovec current and orig count for iscsi_cmd->iov_data */
	u32			iov_data_count;
	u32			orig_iov_data_count;
	/* Number of miscellaneous iovecs used for IP stack calls */
	u32			iov_misc_count;
	/* Number of struct iscsi_pdu in struct iscsi_cmd->pdu_list */
	u32			pdu_count;
	/* Next struct iscsi_pdu to send in struct iscsi_cmd->pdu_list */
	u32			pdu_send_order;
	/* Current struct iscsi_pdu in struct iscsi_cmd->pdu_list */
	u32			pdu_start;
	/* Next struct iscsi_seq to send in struct iscsi_cmd->seq_list */
	u32			seq_send_order;
	/* Number of struct iscsi_seq in struct iscsi_cmd->seq_list */
	u32			seq_count;
	/* Current struct iscsi_seq in struct iscsi_cmd->seq_list */
	u32			seq_no;
	/* Lowest offset in current DataOUT sequence */
	u32			seq_start_offset;
	/* Highest offset in current DataOUT sequence */
	u32			seq_end_offset;
	/* Total size in bytes received so far of READ data */
	u32			read_data_done;
	/* Total size in bytes received so far of WRITE data */
	u32			write_data_done;
	/* Counter for FirstBurstLength key */
	u32			first_burst_len;
	/* Counter for MaxBurstLength key */
	u32			next_burst_len;
	/* Transfer size used for IP stack calls */
	u32			tx_size;
	/* Buffer used for various purposes */
	void			*buf_ptr;
	/* Used by SendTargets=[iqn.,eui.] discovery */
	void			*text_in_ptr;
	/* See include/linux/dma-mapping.h */
	enum dma_data_direction	data_direction;
	/* iSCSI PDU Header + CRC */
	unsigned char		pdu[ISCSI_HDR_LEN + ISCSI_CRC_LEN];
	/* Number of times struct iscsi_cmd is present in immediate queue */
	atomic_t		immed_queue_count;
	atomic_t		response_queue_count;
	spinlock_t		datain_lock;
	spinlock_t		dataout_timeout_lock;
	/* spinlock for protecting struct iscsi_cmd->i_state */
	spinlock_t		istate_lock;
	/* spinlock for adding within command recovery entries */
	spinlock_t		error_lock;
	/* spinlock for adding R2Ts */
	spinlock_t		r2t_lock;
	/* DataIN List */
	struct list_head	datain_list;
	/* R2T List */
	struct list_head	cmd_r2t_list;
	/* Timer for DataOUT */
	struct timer_list	dataout_timer;
	/* Iovecs for SCSI data payload RX/TX w/ kernel level sockets */
	struct kvec		*iov_data;
	/* Iovecs for miscellaneous purposes */
#define ISCSI_MISC_IOVECS			5
	struct kvec		iov_misc[ISCSI_MISC_IOVECS];
	/* Array of struct iscsi_pdu used for DataPDUInOrder=No */
	struct iscsi_pdu	*pdu_list;
	/* Current struct iscsi_pdu used for DataPDUInOrder=No */
	struct iscsi_pdu	*pdu_ptr;
	/* Array of struct iscsi_seq used for DataSequenceInOrder=No */
	struct iscsi_seq	*seq_list;
	/* Current struct iscsi_seq used for DataSequenceInOrder=No */
	struct iscsi_seq	*seq_ptr;
	/* TMR Request when iscsi_opcode == ISCSI_OP_SCSI_TMFUNC */
	struct iscsi_tmr_req	*tmr_req;
	/* Connection this command is alligient to */
	struct iscsi_conn	*conn;
	/* Pointer to connection recovery entry */
	struct iscsi_conn_recovery *cr;
	/* Session the command is part of,  used for connection recovery */
	struct iscsi_session	*sess;
	/* list_head for connection list */
	struct list_head	i_conn_node;
	/* The TCM I/O descriptor that is accessed via container_of() */
	struct se_cmd		se_cmd;
	/* Sense buffer that will be mapped into outgoing status */
#define ISCSI_SENSE_BUFFER_LEN          (TRANSPORT_SENSE_BUFFER + 2)
	unsigned char		sense_buffer[ISCSI_SENSE_BUFFER_LEN];

	u32			padding;
	u8			pad_bytes[4];

	struct scatterlist	*first_data_sg;
	u32			first_data_sg_off;
	u32			kmapped_nents;
	sense_reason_t		sense_reason;
}  ____cacheline_aligned;

struct iscsi_tmr_req {
	bool			task_reassign:1;
	u32			exp_data_sn;
	struct iscsi_cmd	*ref_cmd;
	struct iscsi_conn_recovery *conn_recovery;
	struct se_tmr_req	*se_tmr_req;
};

struct iscsi_conn {
	wait_queue_head_t	queues_wq;
	/* Authentication Successful for this connection */
	u8			auth_complete;
	/* State connection is currently in */
	u8			conn_state;
	u8			conn_logout_reason;
	u8			network_transport;
	enum iscsi_timer_flags_table nopin_timer_flags;
	enum iscsi_timer_flags_table nopin_response_timer_flags;
	/* Used to know what thread encountered a transport failure */
	u8			which_thread;
	/* connection id assigned by the Initiator */
	u16			cid;
	/* Remote TCP Port */
	u16			login_port;
	u16			local_port;
	int			net_size;
	int			login_family;
	u32			auth_id;
	u32			conn_flags;
	/* Used for iscsi_tx_login_rsp() */
	itt_t			login_itt;
	u32			exp_statsn;
	/* Per connection status sequence number */
	u32			stat_sn;
	/* IFMarkInt's Current Value */
	u32			if_marker;
	/* OFMarkInt's Current Value */
	u32			of_marker;
	/* Used for calculating OFMarker offset to next PDU */
	u32			of_marker_offset;
#define IPV6_ADDRESS_SPACE				48
	unsigned char		login_ip[IPV6_ADDRESS_SPACE];
	unsigned char		local_ip[IPV6_ADDRESS_SPACE];
	int			conn_usage_count;
	int			conn_waiting_on_uc;
	atomic_t		check_immediate_queue;
	atomic_t		conn_logout_remove;
	atomic_t		connection_exit;
	atomic_t		connection_recovery;
	atomic_t		connection_reinstatement;
	atomic_t		connection_wait_rcfr;
	atomic_t		sleep_on_conn_wait_comp;
	atomic_t		transport_failed;
	struct completion	conn_post_wait_comp;
	struct completion	conn_wait_comp;
	struct completion	conn_wait_rcfr_comp;
	struct completion	conn_waiting_on_uc_comp;
	struct completion	conn_logout_comp;
	struct completion	tx_half_close_comp;
	struct completion	rx_half_close_comp;
	/* socket used by this connection */
	struct socket		*sock;
	void			(*orig_data_ready)(struct sock *, int);
	void			(*orig_state_change)(struct sock *);
#define LOGIN_FLAGS_READ_ACTIVE		1
#define LOGIN_FLAGS_CLOSED		2
#define LOGIN_FLAGS_READY		4
	unsigned long		login_flags;
	struct delayed_work	login_work;
	struct delayed_work	login_cleanup_work;
	struct iscsi_login	*login;
	struct timer_list	nopin_timer;
	struct timer_list	nopin_response_timer;
	struct timer_list	transport_timer;
	struct task_struct	*login_kworker;
	/* Spinlock used for add/deleting cmd's from conn_cmd_list */
	spinlock_t		cmd_lock;
	spinlock_t		conn_usage_lock;
	spinlock_t		immed_queue_lock;
	spinlock_t		nopin_timer_lock;
	spinlock_t		response_queue_lock;
	spinlock_t		state_lock;
	/* libcrypto RX and TX contexts for crc32c */
	struct hash_desc	conn_rx_hash;
	struct hash_desc	conn_tx_hash;
	/* Used for scheduling TX and RX connection kthreads */
	cpumask_var_t		conn_cpumask;
	unsigned int		conn_rx_reset_cpumask:1;
	unsigned int		conn_tx_reset_cpumask:1;
	/* list_head of struct iscsi_cmd for this connection */
	struct list_head	conn_cmd_list;
	struct list_head	immed_queue_list;
	struct list_head	response_queue_list;
	struct iscsi_conn_ops	*conn_ops;
	struct iscsi_login	*conn_login;
	struct iscsit_transport *conn_transport;
	struct iscsi_param_list	*param_list;
	/* Used for per connection auth state machine */
	void			*auth_protocol;
	void			*context;
	struct iscsi_login_thread_s *login_thread;
	struct iscsi_portal_group *tpg;
	struct iscsi_tpg_np	*tpg_np;
	/* Pointer to parent session */
	struct iscsi_session	*sess;
	/* Pointer to thread_set in use for this conn's threads */
	struct iscsi_thread_set	*thread_set;
	/* list_head for session connection list */
	struct list_head	conn_list;
} ____cacheline_aligned;

struct iscsi_conn_recovery {
	u16			cid;
	u32			cmd_count;
	u32			maxrecvdatasegmentlength;
	u32			maxxmitdatasegmentlength;
	int			ready_for_reallegiance;
	struct list_head	conn_recovery_cmd_list;
	spinlock_t		conn_recovery_cmd_lock;
	struct timer_list	time2retain_timer;
	struct iscsi_session	*sess;
	struct list_head	cr_list;
}  ____cacheline_aligned;

struct iscsi_session {
	u8			initiator_vendor;
	u8			isid[6];
	enum iscsi_timer_flags_table time2retain_timer_flags;
	u8			version_active;
	u16			cid_called;
	u16			conn_recovery_count;
	u16			tsih;
	/* state session is currently in */
	u32			session_state;
	/* session wide counter: initiator assigned task tag */
	itt_t			init_task_tag;
	/* session wide counter: target assigned task tag */
	u32			targ_xfer_tag;
	u32			cmdsn_window;

	/* protects cmdsn values */
	struct mutex		cmdsn_mutex;
	/* session wide counter: expected command sequence number */
	u32			exp_cmd_sn;
	/* session wide counter: maximum allowed command sequence number */
	u32			max_cmd_sn;
	struct list_head	sess_ooo_cmdsn_list;

	/* LIO specific session ID */
	u32			sid;
	char			auth_type[8];
	/* unique within the target */
	int			session_index;
	/* Used for session reference counting */
	int			session_usage_count;
	int			session_waiting_on_uc;
	atomic_long_t		cmd_pdus;
	atomic_long_t		rsp_pdus;
	atomic_long_t		tx_data_octets;
	atomic_long_t		rx_data_octets;
	atomic_long_t		conn_digest_errors;
	atomic_long_t		conn_timeout_errors;
	u64			creation_time;
	/* Number of active connections */
	atomic_t		nconn;
	atomic_t		session_continuation;
	atomic_t		session_fall_back_to_erl0;
	atomic_t		session_logout;
	atomic_t		session_reinstatement;
	atomic_t		session_stop_active;
	atomic_t		sleep_on_sess_wait_comp;
	/* connection list */
	struct list_head	sess_conn_list;
	struct list_head	cr_active_list;
	struct list_head	cr_inactive_list;
	spinlock_t		conn_lock;
	spinlock_t		cr_a_lock;
	spinlock_t		cr_i_lock;
	spinlock_t		session_usage_lock;
	spinlock_t		ttt_lock;
	struct completion	async_msg_comp;
	struct completion	reinstatement_comp;
	struct completion	session_wait_comp;
	struct completion	session_waiting_on_uc_comp;
	struct timer_list	time2retain_timer;
	struct iscsi_sess_ops	*sess_ops;
	struct se_session	*se_sess;
	struct iscsi_portal_group *tpg;
} ____cacheline_aligned;

struct iscsi_login {
	u8 auth_complete;
	u8 checked_for_existing;
	u8 current_stage;
	u8 leading_connection;
	u8 first_request;
	u8 version_min;
	u8 version_max;
	u8 login_complete;
	u8 login_failed;
	bool zero_tsih;
	char isid[6];
	u32 cmd_sn;
	itt_t init_task_tag;
	u32 initial_exp_statsn;
	u32 rsp_length;
	u16 cid;
	u16 tsih;
	char req[ISCSI_HDR_LEN];
	char rsp[ISCSI_HDR_LEN];
	char *req_buf;
	char *rsp_buf;
	struct iscsi_conn *conn;
	struct iscsi_np *np;
} ____cacheline_aligned;

struct iscsi_node_attrib {
	u32			dataout_timeout;
	u32			dataout_timeout_retries;
	u32			default_erl;
	u32			nopin_timeout;
	u32			nopin_response_timeout;
	u32			random_datain_pdu_offsets;
	u32			random_datain_seq_offsets;
	u32			random_r2t_offsets;
	u32			tmr_cold_reset;
	u32			tmr_warm_reset;
	struct iscsi_node_acl *nacl;
};

struct se_dev_entry_s;

struct iscsi_node_auth {
	enum naf_flags_table	naf_flags;
	int			authenticate_target;
	/* Used for iscsit_global->discovery_auth,
	 * set to zero (auth disabled) by default */
	int			enforce_discovery_auth;
#define MAX_USER_LEN				256
#define MAX_PASS_LEN				256
	char			userid[MAX_USER_LEN];
	char			password[MAX_PASS_LEN];
	char			userid_mutual[MAX_USER_LEN];
	char			password_mutual[MAX_PASS_LEN];
};

#include "iscsi_target_stat.h"

struct iscsi_node_stat_grps {
	struct config_group	iscsi_sess_stats_group;
	struct config_group	iscsi_conn_stats_group;
};

struct iscsi_node_acl {
	struct iscsi_node_attrib node_attrib;
	struct iscsi_node_auth	node_auth;
	struct iscsi_node_stat_grps node_stat_grps;
	struct se_node_acl	se_node_acl;
};

struct iscsi_tpg_attrib {
	u32			authentication;
	u32			login_timeout;
	u32			netif_timeout;
	u32			generate_node_acls;
	u32			cache_dynamic_acls;
	u32			default_cmdsn_depth;
	u32			demo_mode_write_protect;
	u32			prod_mode_write_protect;
	u32			demo_mode_discovery;
	u32			default_erl;
	struct iscsi_portal_group *tpg;
};

struct iscsi_np {
	int			np_network_transport;
	int			np_ip_proto;
	int			np_sock_type;
	enum np_thread_state_table np_thread_state;
	enum iscsi_timer_flags_table np_login_timer_flags;
	u32			np_exports;
	enum np_flags_table	np_flags;
	unsigned char		np_ip[IPV6_ADDRESS_SPACE];
	u16			np_port;
	spinlock_t		np_thread_lock;
	struct completion	np_restart_comp;
	struct socket		*np_socket;
	struct __kernel_sockaddr_storage np_sockaddr;
	struct task_struct	*np_thread;
	struct timer_list	np_login_timer;
	void			*np_context;
	struct iscsit_transport *np_transport;
	struct list_head	np_list;
} ____cacheline_aligned;

struct iscsi_tpg_np {
	struct iscsi_np		*tpg_np;
	struct iscsi_portal_group *tpg;
	struct iscsi_tpg_np	*tpg_np_parent;
	struct list_head	tpg_np_list;
	struct list_head	tpg_np_child_list;
	struct list_head	tpg_np_parent_list;
	struct se_tpg_np	se_tpg_np;
	spinlock_t		tpg_np_parent_lock;
	struct completion	tpg_np_comp;
	struct kref		tpg_np_kref;
};

struct iscsi_portal_group {
	unsigned char		tpg_chap_id;
	/* TPG State */
	enum tpg_state_table	tpg_state;
	/* Target Portal Group Tag */
	u16			tpgt;
	/* Id assigned to target sessions */
	u16			ntsih;
	/* Number of active sessions */
	u32			nsessions;
	/* Number of Network Portals available for this TPG */
	u32			num_tpg_nps;
	/* Per TPG LIO specific session ID. */
	u32			sid;
	/* Spinlock for adding/removing Network Portals */
	spinlock_t		tpg_np_lock;
	spinlock_t		tpg_state_lock;
	struct se_portal_group tpg_se_tpg;
	struct mutex		tpg_access_lock;
	struct semaphore	np_login_sem;
	struct iscsi_tpg_attrib	tpg_attrib;
	struct iscsi_node_auth	tpg_demo_auth;
	/* Pointer to default list of iSCSI parameters for TPG */
	struct iscsi_param_list	*param_list;
	struct iscsi_tiqn	*tpg_tiqn;
	struct list_head	tpg_gnp_list;
	struct list_head	tpg_list;
} ____cacheline_aligned;

struct iscsi_wwn_stat_grps {
	struct config_group	iscsi_stat_group;
	struct config_group	iscsi_instance_group;
	struct config_group	iscsi_sess_err_group;
	struct config_group	iscsi_tgt_attr_group;
	struct config_group	iscsi_login_stats_group;
	struct config_group	iscsi_logout_stats_group;
};

struct iscsi_tiqn {
#define ISCSI_IQN_LEN				224
	unsigned char		tiqn[ISCSI_IQN_LEN];
	enum tiqn_state_table	tiqn_state;
	int			tiqn_access_count;
	u32			tiqn_active_tpgs;
	u32			tiqn_ntpgs;
	u32			tiqn_num_tpg_nps;
	u32			tiqn_nsessions;
	struct list_head	tiqn_list;
	struct list_head	tiqn_tpg_list;
	spinlock_t		tiqn_state_lock;
	spinlock_t		tiqn_tpg_lock;
	struct se_wwn		tiqn_wwn;
	struct iscsi_wwn_stat_grps tiqn_stat_grps;
	int			tiqn_index;
	struct iscsi_sess_err_stats  sess_err_stats;
	struct iscsi_login_stats     login_stats;
	struct iscsi_logout_stats    logout_stats;
} ____cacheline_aligned;

struct iscsit_global {
	/* In core shutdown */
	u32			in_shutdown;
	u32			active_ts;
	/* Unique identifier used for the authentication daemon */
	u32			auth_id;
	u32			inactive_ts;
	/* Thread Set bitmap count */
	int			ts_bitmap_count;
	/* Thread Set bitmap pointer */
	unsigned long		*ts_bitmap;
	/* Used for iSCSI discovery session authentication */
	struct iscsi_node_acl	discovery_acl;
	struct iscsi_portal_group	*discovery_tpg;
};

#endif /* ISCSI_TARGET_CORE_H */
