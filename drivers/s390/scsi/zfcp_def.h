/*
 * zfcp device driver
 *
 * Global definitions for the zfcp device driver.
 *
 * Copyright IBM Corporation 2002, 2009
 */

#ifndef ZFCP_DEF_H
#define ZFCP_DEF_H

/*************************** INCLUDES *****************************************/

#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/mempool.h>
#include <linux/syscalls.h>
#include <linux/scatterlist.h>
#include <linux/ioctl.h>
#include <scsi/fc/fc_fs.h>
#include <scsi/fc/fc_gs.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_bsg_fc.h>
#include <asm/ccwdev.h>
#include <asm/qdio.h>
#include <asm/debug.h>
#include <asm/ebcdic.h>
#include <asm/sysinfo.h>
#include "zfcp_fsf.h"

/********************* GENERAL DEFINES *********************************/

#define REQUEST_LIST_SIZE 128

/********************* SCSI SPECIFIC DEFINES *********************************/
#define ZFCP_SCSI_ER_TIMEOUT                    (10*HZ)

/********************* CIO/QDIO SPECIFIC DEFINES *****************************/

/* DMQ bug workaround: don't use last SBALE */
#define ZFCP_MAX_SBALES_PER_SBAL	(QDIO_MAX_ELEMENTS_PER_BUFFER - 1)

/* index of last SBALE (with respect to DMQ bug workaround) */
#define ZFCP_LAST_SBALE_PER_SBAL	(ZFCP_MAX_SBALES_PER_SBAL - 1)

/* max. number of (data buffer) SBALEs in largest SBAL chain */
#define ZFCP_MAX_SBALES_PER_REQ		\
	(FSF_MAX_SBALS_PER_REQ * ZFCP_MAX_SBALES_PER_SBAL - 2)
        /* request ID + QTCB in SBALE 0 + 1 of first SBAL in chain */

#define ZFCP_MAX_SECTORS (ZFCP_MAX_SBALES_PER_REQ * 8)
        /* max. number of (data buffer) SBALEs in largest SBAL chain
           multiplied with number of sectors per 4k block */

/********************* FSF SPECIFIC DEFINES *********************************/

/* ATTENTION: value must not be used by hardware */
#define FSF_QTCB_UNSOLICITED_STATUS		0x6305

/* timeout value for "default timer" for fsf requests */
#define ZFCP_FSF_REQUEST_TIMEOUT (60*HZ)

/*************** FIBRE CHANNEL PROTOCOL SPECIFIC DEFINES ********************/

/* task attribute values in FCP-2 FCP_CMND IU */
#define SIMPLE_Q	0
#define HEAD_OF_Q	1
#define ORDERED_Q	2
#define ACA_Q		4
#define UNTAGGED	5

/* task management flags in FCP-2 FCP_CMND IU */
#define FCP_CLEAR_ACA		0x40
#define FCP_TARGET_RESET	0x20
#define FCP_LOGICAL_UNIT_RESET	0x10
#define FCP_CLEAR_TASK_SET	0x04
#define FCP_ABORT_TASK_SET	0x02

#define FCP_CDB_LENGTH		16

#define ZFCP_DID_MASK           0x00FFFFFF

/* FCP(-2) FCP_CMND IU */
struct fcp_cmnd_iu {
	u64 fcp_lun;	   /* FCP logical unit number */
	u8  crn;	           /* command reference number */
	u8  reserved0:5;	   /* reserved */
	u8  task_attribute:3;	   /* task attribute */
	u8  task_management_flags; /* task management flags */
	u8  add_fcp_cdb_length:6;  /* additional FCP_CDB length */
	u8  rddata:1;              /* read data */
	u8  wddata:1;              /* write data */
	u8  fcp_cdb[FCP_CDB_LENGTH];
} __attribute__((packed));

/* FCP(-2) FCP_RSP IU */
struct fcp_rsp_iu {
	u8  reserved0[10];
	union {
		struct {
			u8 reserved1:3;
			u8 fcp_conf_req:1;
			u8 fcp_resid_under:1;
			u8 fcp_resid_over:1;
			u8 fcp_sns_len_valid:1;
			u8 fcp_rsp_len_valid:1;
		} bits;
		u8 value;
	} validity;
	u8  scsi_status;
	u32 fcp_resid;
	u32 fcp_sns_len;
	u32 fcp_rsp_len;
} __attribute__((packed));


#define RSP_CODE_GOOD		 0
#define RSP_CODE_LENGTH_MISMATCH 1
#define RSP_CODE_FIELD_INVALID	 2
#define RSP_CODE_RO_MISMATCH	 3
#define RSP_CODE_TASKMAN_UNSUPP	 4
#define RSP_CODE_TASKMAN_FAILED	 5

/* see fc-fs */
#define LS_RSCN  0x61
#define LS_LOGO  0x05
#define LS_PLOGI 0x03

struct fcp_rscn_head {
        u8  command;
        u8  page_length; /* always 0x04 */
        u16 payload_len;
} __attribute__((packed));

struct fcp_rscn_element {
        u8  reserved:2;
        u8  event_qual:4;
        u8  addr_format:2;
        u32 nport_did:24;
} __attribute__((packed));

/* see fc-ph */
struct fcp_logo {
        u32 command;
        u32 nport_did;
	u64 nport_wwpn;
} __attribute__((packed));

/*
 * FC-FS stuff
 */
#define R_A_TOV				10 /* seconds */

#define ZFCP_LS_RLS			0x0f
#define ZFCP_LS_ADISC			0x52
#define ZFCP_LS_RPS			0x56
#define ZFCP_LS_RSCN			0x61
#define ZFCP_LS_RNID			0x78

struct zfcp_ls_adisc {
	u8		code;
	u8		field[3];
	u32		hard_nport_id;
	u64		wwpn;
	u64		wwnn;
	u32		nport_id;
} __attribute__ ((packed));

/*
 * FC-GS-2 stuff
 */
#define ZFCP_CT_REVISION		0x01
#define ZFCP_CT_DIRECTORY_SERVICE	0xFC
#define ZFCP_CT_NAME_SERVER		0x02
#define ZFCP_CT_SYNCHRONOUS		0x00
#define ZFCP_CT_SCSI_FCP		0x08
#define ZFCP_CT_UNABLE_TO_PERFORM_CMD	0x09
#define ZFCP_CT_GID_PN			0x0121
#define ZFCP_CT_GPN_FT			0x0172
#define ZFCP_CT_ACCEPT			0x8002
#define ZFCP_CT_REJECT			0x8001

/*
 * FC-GS-4 stuff
 */
#define ZFCP_CT_TIMEOUT			(3 * R_A_TOV)

/*************** ADAPTER/PORT/UNIT AND FSF_REQ STATUS FLAGS ******************/

/*
 * Note, the leftmost status byte is common among adapter, port
 * and unit
 */
#define ZFCP_COMMON_FLAGS			0xfff00000

/* common status bits */
#define ZFCP_STATUS_COMMON_REMOVE		0x80000000
#define ZFCP_STATUS_COMMON_RUNNING		0x40000000
#define ZFCP_STATUS_COMMON_ERP_FAILED		0x20000000
#define ZFCP_STATUS_COMMON_UNBLOCKED		0x10000000
#define ZFCP_STATUS_COMMON_OPEN                 0x04000000
#define ZFCP_STATUS_COMMON_ERP_INUSE		0x01000000
#define ZFCP_STATUS_COMMON_ACCESS_DENIED	0x00800000
#define ZFCP_STATUS_COMMON_ACCESS_BOXED		0x00400000
#define ZFCP_STATUS_COMMON_NOESC		0x00200000

/* adapter status */
#define ZFCP_STATUS_ADAPTER_QDIOUP		0x00000002
#define ZFCP_STATUS_ADAPTER_XCONFIG_OK		0x00000008
#define ZFCP_STATUS_ADAPTER_HOST_CON_INIT	0x00000010
#define ZFCP_STATUS_ADAPTER_ERP_PENDING		0x00000100
#define ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED	0x00000200

/* FC-PH/FC-GS well-known address identifiers for generic services */
#define ZFCP_DID_WKA				0xFFFFF0

/* remote port status */
#define ZFCP_STATUS_PORT_PHYS_OPEN		0x00000001
#define ZFCP_STATUS_PORT_LINK_TEST		0x00000002

/* well known address (WKA) port status*/
enum zfcp_wka_status {
	ZFCP_WKA_PORT_OFFLINE,
	ZFCP_WKA_PORT_CLOSING,
	ZFCP_WKA_PORT_OPENING,
	ZFCP_WKA_PORT_ONLINE,
};

/* logical unit status */
#define ZFCP_STATUS_UNIT_SHARED			0x00000004
#define ZFCP_STATUS_UNIT_READONLY		0x00000008

/* FSF request status (this does not have a common part) */
#define ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT	0x00000002
#define ZFCP_STATUS_FSFREQ_ERROR		0x00000008
#define ZFCP_STATUS_FSFREQ_CLEANUP		0x00000010
#define ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED	0x00000040
#define ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED       0x00000080
#define ZFCP_STATUS_FSFREQ_ABORTED              0x00000100
#define ZFCP_STATUS_FSFREQ_TMFUNCFAILED         0x00000200
#define ZFCP_STATUS_FSFREQ_TMFUNCNOTSUPP        0x00000400
#define ZFCP_STATUS_FSFREQ_RETRY                0x00000800
#define ZFCP_STATUS_FSFREQ_DISMISSED            0x00001000

/************************* STRUCTURE DEFINITIONS *****************************/

struct zfcp_fsf_req;

/* holds various memory pools of an adapter */
struct zfcp_adapter_mempool {
	mempool_t *erp_req;
	mempool_t *gid_pn_req;
	mempool_t *scsi_req;
	mempool_t *scsi_abort;
	mempool_t *status_read_req;
	mempool_t *status_read_data;
	mempool_t *gid_pn_data;
	mempool_t *qtcb_pool;
};

/*
 * header for CT_IU
 */
struct ct_hdr {
	u8 revision;		// 0x01
	u8 in_id[3];		// 0x00
	u8 gs_type;		// 0xFC	Directory Service
	u8 gs_subtype;		// 0x02	Name Server
	u8 options;		// 0x00 single bidirectional exchange
	u8 reserved0;
	u16 cmd_rsp_code;	// 0x0121 GID_PN, or 0x0100 GA_NXT
	u16 max_res_size;	// <= (4096 - 16) / 4
	u8 reserved1;
	u8 reason_code;
	u8 reason_code_expl;
	u8 vendor_unique;
} __attribute__ ((packed));

/* nameserver request CT_IU -- for requests where
 * a port name is required */
struct ct_iu_gid_pn_req {
	struct ct_hdr header;
	u64 wwpn;
} __attribute__ ((packed));

/* FS_ACC IU and data unit for GID_PN nameserver request */
struct ct_iu_gid_pn_resp {
	struct ct_hdr header;
	u32 d_id;
} __attribute__ ((packed));

struct ct_iu_gpn_ft_req {
	struct ct_hdr header;
	u8 flags;
	u8 domain_id_scope;
	u8 area_id_scope;
	u8 fc4_type;
} __attribute__ ((packed));


/**
 * struct zfcp_send_ct - used to pass parameters to function zfcp_fsf_send_ct
 * @wka_port: port where the request is sent to
 * @req: scatter-gather list for request
 * @resp: scatter-gather list for response
 * @handler: handler function (called for response to the request)
 * @handler_data: data passed to handler function
 * @completion: completion for synchronization purposes
 * @status: used to pass error status to calling function
 */
struct zfcp_send_ct {
	struct zfcp_wka_port *wka_port;
	struct scatterlist *req;
	struct scatterlist *resp;
	void (*handler)(unsigned long);
	unsigned long handler_data;
	struct completion *completion;
	int status;
};

/* used for name server requests in error recovery */
struct zfcp_gid_pn_data {
	struct zfcp_send_ct ct;
	struct scatterlist req;
	struct scatterlist resp;
	struct ct_iu_gid_pn_req ct_iu_req;
	struct ct_iu_gid_pn_resp ct_iu_resp;
        struct zfcp_port *port;
};

/**
 * struct zfcp_send_els - used to pass parameters to function zfcp_fsf_send_els
 * @adapter: adapter where request is sent from
 * @port: port where ELS is destinated (port reference count has to be increased)
 * @d_id: destiniation id of port where request is sent to
 * @req: scatter-gather list for request
 * @resp: scatter-gather list for response
 * @handler: handler function (called for response to the request)
 * @handler_data: data passed to handler function
 * @completion: completion for synchronization purposes
 * @ls_code: hex code of ELS command
 * @status: used to pass error status to calling function
 */
struct zfcp_send_els {
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	u32 d_id;
	struct scatterlist *req;
	struct scatterlist *resp;
	void (*handler)(unsigned long);
	unsigned long handler_data;
	struct completion *completion;
	int ls_code;
	int status;
};

struct zfcp_wka_port {
	struct zfcp_adapter	*adapter;
	wait_queue_head_t	completion_wq;
	enum zfcp_wka_status	status;
	atomic_t		refcount;
	u32			d_id;
	u32			handle;
	struct mutex		mutex;
	struct delayed_work	work;
};

struct zfcp_wka_ports {
	struct zfcp_wka_port ms; 	/* management service */
	struct zfcp_wka_port ts; 	/* time service */
	struct zfcp_wka_port ds; 	/* directory service */
	struct zfcp_wka_port as; 	/* alias service */
	struct zfcp_wka_port ks; 	/* key distribution service */
};

struct zfcp_qdio_queue {
	struct qdio_buffer *sbal[QDIO_MAX_BUFFERS_PER_Q];
	u8		   first;	/* index of next free bfr in queue */
	atomic_t           count;	/* number of free buffers in queue */
};

struct zfcp_erp_action {
	struct list_head list;
	int action;	              /* requested action code */
	struct zfcp_adapter *adapter; /* device which should be recovered */
	struct zfcp_port *port;
	struct zfcp_unit *unit;
	u32		status;	      /* recovery status */
	u32 step;	              /* active step of this erp action */
	struct zfcp_fsf_req *fsf_req; /* fsf request currently pending
					 for this action */
	struct timer_list timer;
};

struct fsf_latency_record {
	u32 min;
	u32 max;
	u64 sum;
};

struct latency_cont {
	struct fsf_latency_record channel;
	struct fsf_latency_record fabric;
	u64 counter;
};

struct zfcp_latencies {
	struct latency_cont read;
	struct latency_cont write;
	struct latency_cont cmd;
	spinlock_t lock;
};

/** struct zfcp_qdio - basic QDIO data structure
 * @resp_q: response queue
 * @req_q: request queue
 * @stat_lock: lock to protect req_q_util and req_q_time
 * @req_q_lock; lock to serialize access to request queue
 * @req_q_time: time of last fill level change
 * @req_q_util: used for accounting
 * @req_q_full: queue full incidents
 * @req_q_wq: used to wait for SBAL availability
 * @adapter: adapter used in conjunction with this QDIO structure
 */
struct zfcp_qdio {
	struct zfcp_qdio_queue	resp_q;
	struct zfcp_qdio_queue	req_q;
	spinlock_t		stat_lock;
	spinlock_t		req_q_lock;
	unsigned long long	req_q_time;
	u64			req_q_util;
	atomic_t		req_q_full;
	wait_queue_head_t	req_q_wq;
	struct zfcp_adapter	*adapter;
};

struct zfcp_adapter {
	atomic_t                refcount;          /* reference count */
	wait_queue_head_t	remove_wq;         /* can be used to wait for
						      refcount drop to zero */
	u64			peer_wwnn;	   /* P2P peer WWNN */
	u64			peer_wwpn;	   /* P2P peer WWPN */
	u32			peer_d_id;	   /* P2P peer D_ID */
	struct ccw_device       *ccw_device;	   /* S/390 ccw device */
	struct zfcp_qdio	*qdio;
	u32			hydra_version;	   /* Hydra version */
	u32			fsf_lic_version;
	u32			adapter_features;  /* FCP channel features */
	u32			connection_features; /* host connection features */
        u32			hardware_version;  /* of FCP channel */
	u16			timer_ticks;       /* time int for a tick */
	struct Scsi_Host	*scsi_host;	   /* Pointer to mid-layer */
	struct list_head	port_list_head;	   /* remote port list */
	unsigned long		req_no;		   /* unique FSF req number */
	struct list_head	*req_list;	   /* list of pending reqs */
	spinlock_t		req_list_lock;	   /* request list lock */
	u32			fsf_req_seq_no;	   /* FSF cmnd seq number */
	rwlock_t		abort_lock;        /* Protects against SCSI
						      stack abort/command
						      completion races */
	atomic_t		stat_miss;	   /* # missing status reads*/
	struct work_struct	stat_work;
	atomic_t		status;	           /* status of this adapter */
	struct list_head	erp_ready_head;	   /* error recovery for this
						      adapter/devices */
	wait_queue_head_t	erp_ready_wq;
	struct list_head	erp_running_head;
	rwlock_t		erp_lock;
	wait_queue_head_t	erp_done_wqh;
	struct zfcp_erp_action	erp_action;	   /* pending error recovery */
        atomic_t                erp_counter;
	u32			erp_total_count;   /* total nr of enqueued erp
						      actions */
	u32			erp_low_mem_count; /* nr of erp actions waiting
						      for memory */
	struct task_struct	*erp_thread;
	struct zfcp_wka_ports	*gs;		   /* generic services */
	struct zfcp_dbf		*dbf;		   /* debug traces */
	struct zfcp_adapter_mempool	pool;      /* Adapter memory pools */
	struct fc_host_statistics *fc_stats;
	struct fsf_qtcb_bottom_port *stats_reset_data;
	unsigned long		stats_reset;
	struct work_struct	scan_work;
	struct service_level	service_level;
	struct workqueue_struct	*work_queue;
};

struct zfcp_port {
	struct device          sysfs_device;   /* sysfs device */
	struct fc_rport        *rport;         /* rport of fc transport class */
	struct list_head       list;	       /* list of remote ports */
	atomic_t               refcount;       /* reference count */
	wait_queue_head_t      remove_wq;      /* can be used to wait for
						  refcount drop to zero */
	struct zfcp_adapter    *adapter;       /* adapter used to access port */
	struct list_head       unit_list_head; /* head of logical unit list */
	atomic_t	       status;	       /* status of this remote port */
	u64		       wwnn;	       /* WWNN if known */
	u64		       wwpn;	       /* WWPN */
	u32		       d_id;	       /* D_ID */
	u32		       handle;	       /* handle assigned by FSF */
	struct zfcp_erp_action erp_action;     /* pending error recovery */
        atomic_t               erp_counter;
	u32                    maxframe_size;
	u32                    supported_classes;
	struct work_struct     gid_pn_work;
	struct work_struct     test_link_work;
	struct work_struct     rport_work;
	enum { RPORT_NONE, RPORT_ADD, RPORT_DEL }  rport_task;
};

struct zfcp_unit {
	struct device          sysfs_device;   /* sysfs device */
	struct list_head       list;	       /* list of logical units */
	atomic_t               refcount;       /* reference count */
	wait_queue_head_t      remove_wq;      /* can be used to wait for
						  refcount drop to zero */
	struct zfcp_port       *port;	       /* remote port of unit */
	atomic_t	       status;	       /* status of this logical unit */
	u64		       fcp_lun;	       /* own FCP_LUN */
	u32		       handle;	       /* handle assigned by FSF */
        struct scsi_device     *device;        /* scsi device struct pointer */
	struct zfcp_erp_action erp_action;     /* pending error recovery */
        atomic_t               erp_counter;
	struct zfcp_latencies	latencies;
	struct work_struct	scsi_work;
};

/**
 * struct zfcp_queue_req - queue related values for a request
 * @sbal_number: number of free SBALs
 * @sbal_first: first SBAL for this request
 * @sbal_last: last SBAL for this request
 * @sbal_limit: last possible SBAL for this request
 * @sbale_curr: current SBALE at creation of this request
 * @sbal_response: SBAL used in interrupt
 * @qdio_outb_usage: usage of outbound queue
 * @qdio_inb_usage: usage of inbound queue
 */
struct zfcp_queue_req {
	u8		       sbal_number;
	u8		       sbal_first;
	u8		       sbal_last;
	u8		       sbal_limit;
	u8		       sbale_curr;
	u8		       sbal_response;
	u16		       qdio_outb_usage;
	u16		       qdio_inb_usage;
};

/**
 * struct zfcp_fsf_req - basic FSF request structure
 * @list: list of FSF requests
 * @req_id: unique request ID
 * @adapter: adapter this request belongs to
 * @queue_req: queue related values
 * @completion: used to signal the completion of the request
 * @status: status of the request
 * @fsf_command: FSF command issued
 * @qtcb: associated QTCB
 * @seq_no: sequence number of this request
 * @data: private data
 * @timer: timer data of this request
 * @erp_action: reference to erp action if request issued on behalf of ERP
 * @pool: reference to memory pool if used for this request
 * @issued: time when request was send (STCK)
 * @unit: reference to unit if this request is a SCSI request
 * @handler: handler which should be called to process response
 */
struct zfcp_fsf_req {
	struct list_head	list;
	unsigned long		req_id;
	struct zfcp_adapter	*adapter;
	struct zfcp_queue_req	queue_req;
	struct completion	completion;
	u32			status;
	u32			fsf_command;
	struct fsf_qtcb		*qtcb;
	u32			seq_no;
	void			*data;
	struct timer_list	timer;
	struct zfcp_erp_action	*erp_action;
	mempool_t		*pool;
	unsigned long long	issued;
	struct zfcp_unit	*unit;
	void			(*handler)(struct zfcp_fsf_req *);
};

/* driver data */
struct zfcp_data {
	struct scsi_host_template scsi_host_template;
	struct scsi_transport_template *scsi_transport_template;
	rwlock_t                config_lock;        /* serialises changes
						       to adapter/port/unit
						       lists */
	struct mutex		config_mutex;
	struct kmem_cache	*gpn_ft_cache;
	struct kmem_cache	*qtcb_cache;
	struct kmem_cache	*sr_buffer_cache;
	struct kmem_cache	*gid_pn_cache;
};

/********************** ZFCP SPECIFIC DEFINES ********************************/

#define ZFCP_SET                0x00000100
#define ZFCP_CLEAR              0x00000200

/*
 * Helper functions for request ID management.
 */
static inline int zfcp_reqlist_hash(unsigned long req_id)
{
	return req_id % REQUEST_LIST_SIZE;
}

static inline void zfcp_reqlist_remove(struct zfcp_adapter *adapter,
				       struct zfcp_fsf_req *fsf_req)
{
	list_del(&fsf_req->list);
}

static inline struct zfcp_fsf_req *
zfcp_reqlist_find(struct zfcp_adapter *adapter, unsigned long req_id)
{
	struct zfcp_fsf_req *request;
	unsigned int idx;

	idx = zfcp_reqlist_hash(req_id);
	list_for_each_entry(request, &adapter->req_list[idx], list)
		if (request->req_id == req_id)
			return request;
	return NULL;
}

static inline struct zfcp_fsf_req *
zfcp_reqlist_find_safe(struct zfcp_adapter *adapter, struct zfcp_fsf_req *req)
{
	struct zfcp_fsf_req *request;
	unsigned int idx;

	for (idx = 0; idx < REQUEST_LIST_SIZE; idx++) {
		list_for_each_entry(request, &adapter->req_list[idx], list)
			if (request == req)
				return request;
	}
	return NULL;
}

/*
 *  functions needed for reference/usage counting
 */

static inline void
zfcp_unit_get(struct zfcp_unit *unit)
{
	atomic_inc(&unit->refcount);
}

static inline void
zfcp_unit_put(struct zfcp_unit *unit)
{
	if (atomic_dec_return(&unit->refcount) == 0)
		wake_up(&unit->remove_wq);
}

static inline void
zfcp_port_get(struct zfcp_port *port)
{
	atomic_inc(&port->refcount);
}

static inline void
zfcp_port_put(struct zfcp_port *port)
{
	if (atomic_dec_return(&port->refcount) == 0)
		wake_up(&port->remove_wq);
}

static inline void
zfcp_adapter_get(struct zfcp_adapter *adapter)
{
	atomic_inc(&adapter->refcount);
}

static inline void
zfcp_adapter_put(struct zfcp_adapter *adapter)
{
	if (atomic_dec_return(&adapter->refcount) == 0)
		wake_up(&adapter->remove_wq);
}

#endif /* ZFCP_DEF_H */
