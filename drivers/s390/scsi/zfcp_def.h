/*
 * zfcp device driver
 *
 * Global definitions for the zfcp device driver.
 *
 * Copyright IBM Corporation 2002, 2010
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
#include <asm/debug.h>
#include <asm/ebcdic.h>
#include <asm/sysinfo.h>
#include "zfcp_fsf.h"
#include "zfcp_qdio.h"

struct zfcp_reqlist;

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

/*************** ADAPTER/PORT/UNIT AND FSF_REQ STATUS FLAGS ******************/

/*
 * Note, the leftmost status byte is common among adapter, port
 * and unit
 */
#define ZFCP_COMMON_FLAGS			0xfff00000

/* common status bits */
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

/* remote port status */
#define ZFCP_STATUS_PORT_PHYS_OPEN		0x00000001
#define ZFCP_STATUS_PORT_LINK_TEST		0x00000002

/* logical unit status */
#define ZFCP_STATUS_UNIT_SHARED			0x00000004
#define ZFCP_STATUS_UNIT_READONLY		0x00000008

/* FSF request status (this does not have a common part) */
#define ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT	0x00000002
#define ZFCP_STATUS_FSFREQ_ERROR		0x00000008
#define ZFCP_STATUS_FSFREQ_CLEANUP		0x00000010
#define ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED	0x00000040
#define ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED       0x00000080
#define ZFCP_STATUS_FSFREQ_TMFUNCFAILED         0x00000200
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
	mempool_t *gid_pn;
	mempool_t *qtcb_pool;
};

struct zfcp_erp_action {
	struct list_head list;
	int action;	              /* requested action code */
	struct zfcp_adapter *adapter; /* device which should be recovered */
	struct zfcp_port *port;
	struct zfcp_unit *unit;
	u32		status;	      /* recovery status */
	u32 step;	              /* active step of this erp action */
	unsigned long		fsf_req_id;
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

struct zfcp_adapter {
	struct kref		ref;
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
	struct list_head	port_list;	   /* remote port list */
	rwlock_t		port_list_lock;    /* port list lock */
	unsigned long		req_no;		   /* unique FSF req number */
	struct zfcp_reqlist	*req_list;
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
	struct zfcp_fc_wka_ports *gs;		   /* generic services */
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
	struct device          dev;
	struct fc_rport        *rport;         /* rport of fc transport class */
	struct list_head       list;	       /* list of remote ports */
	struct zfcp_adapter    *adapter;       /* adapter used to access port */
	struct list_head	unit_list;	/* head of logical unit list */
	rwlock_t		unit_list_lock; /* unit list lock */
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
	struct device          dev;
	struct list_head       list;	       /* list of logical units */
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
 * struct zfcp_fsf_req - basic FSF request structure
 * @list: list of FSF requests
 * @req_id: unique request ID
 * @adapter: adapter this request belongs to
 * @qdio_req: qdio queue related values
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
	struct zfcp_qdio_req	qdio_req;
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
	struct kmem_cache	*gpn_ft_cache;
	struct kmem_cache	*qtcb_cache;
	struct kmem_cache	*sr_buffer_cache;
	struct kmem_cache	*gid_pn_cache;
	struct kmem_cache	*adisc_cache;
};

/********************** ZFCP SPECIFIC DEFINES ********************************/

#define ZFCP_SET                0x00000100
#define ZFCP_CLEAR              0x00000200

#endif /* ZFCP_DEF_H */
