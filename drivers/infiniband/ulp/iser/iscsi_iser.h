/*
 * iSER transport for the Open iSCSI Initiator & iSER transport internals
 *
 * Copyright (C) 2004 Dmitry Yusupov
 * Copyright (C) 2004 Alex Aizman
 * Copyright (C) 2005 Mike Christie
 * based on code maintained by open-iscsi@googlegroups.com
 *
 * Copyright (c) 2004, 2005, 2006 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems.  All rights reserved.
 * Copyright (c) 2013-2014 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __ISCSI_ISER_H__
#define __ISCSI_ISER_H__

#include <linux/types.h>
#include <linux/net.h>
#include <linux/printk.h>
#include <scsi/libiscsi.h>
#include <scsi/scsi_transport_iscsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/iser.h>

#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/mutex.h>
#include <linux/mempool.h>
#include <linux/uio.h>

#include <linux/socket.h>
#include <linux/in.h>
#include <linux/in6.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_fmr_pool.h>
#include <rdma/rdma_cm.h>

#define DRV_NAME	"iser"
#define PFX		DRV_NAME ": "
#define DRV_VER		"1.6"

#define iser_dbg(fmt, arg...)				 \
	do {						 \
		if (unlikely(iser_debug_level > 2))	 \
			printk(KERN_DEBUG PFX "%s: " fmt,\
				__func__ , ## arg);	 \
	} while (0)

#define iser_warn(fmt, arg...)				\
	do {						\
		if (unlikely(iser_debug_level > 0))	\
			pr_warn(PFX "%s: " fmt,		\
				__func__ , ## arg);	\
	} while (0)

#define iser_info(fmt, arg...)				\
	do {						\
		if (unlikely(iser_debug_level > 1))	\
			pr_info(PFX "%s: " fmt,		\
				__func__ , ## arg);	\
	} while (0)

#define iser_err(fmt, arg...) \
	pr_err(PFX "%s: " fmt, __func__ , ## arg)

#define SHIFT_4K	12
#define SIZE_4K	(1ULL << SHIFT_4K)
#define MASK_4K	(~(SIZE_4K-1))

/* Default support is 512KB I/O size */
#define ISER_DEF_MAX_SECTORS		1024
#define ISCSI_ISER_DEF_SG_TABLESIZE	((ISER_DEF_MAX_SECTORS * 512) >> SHIFT_4K)
/* Maximum support is 8MB I/O size */
#define ISCSI_ISER_MAX_SG_TABLESIZE	((16384 * 512) >> SHIFT_4K)

#define ISER_DEF_XMIT_CMDS_DEFAULT		512
#if ISCSI_DEF_XMIT_CMDS_MAX > ISER_DEF_XMIT_CMDS_DEFAULT
	#define ISER_DEF_XMIT_CMDS_MAX		ISCSI_DEF_XMIT_CMDS_MAX
#else
	#define ISER_DEF_XMIT_CMDS_MAX		ISER_DEF_XMIT_CMDS_DEFAULT
#endif
#define ISER_DEF_CMD_PER_LUN		ISER_DEF_XMIT_CMDS_MAX

/* QP settings */
/* Maximal bounds on received asynchronous PDUs */
#define ISER_MAX_RX_MISC_PDUS		4 /* NOOP_IN(2) , ASYNC_EVENT(2)   */

#define ISER_MAX_TX_MISC_PDUS		6 /* NOOP_OUT(2), TEXT(1),         *
					   * SCSI_TMFUNC(2), LOGOUT(1) */

#define ISER_QP_MAX_RECV_DTOS		(ISER_DEF_XMIT_CMDS_MAX)

#define ISER_MIN_POSTED_RX		(ISER_DEF_XMIT_CMDS_MAX >> 2)

/* the max TX (send) WR supported by the iSER QP is defined by                 *
 * max_send_wr = T * (1 + D) + C ; D is how many inflight dataouts we expect   *
 * to have at max for SCSI command. The tx posting & completion handling code  *
 * supports -EAGAIN scheme where tx is suspended till the QP has room for more *
 * send WR. D=8 comes from 64K/8K                                              */

#define ISER_INFLIGHT_DATAOUTS		8

#define ISER_QP_MAX_REQ_DTOS		(ISER_DEF_XMIT_CMDS_MAX *    \
					(1 + ISER_INFLIGHT_DATAOUTS) + \
					ISER_MAX_TX_MISC_PDUS        + \
					ISER_MAX_RX_MISC_PDUS)

/* Max registration work requests per command */
#define ISER_MAX_REG_WR_PER_CMD		5

/* For Signature we don't support DATAOUTs so no need to make room for them */
#define ISER_QP_SIG_MAX_REQ_DTOS	(ISER_DEF_XMIT_CMDS_MAX	*       \
					(1 + ISER_MAX_REG_WR_PER_CMD) + \
					ISER_MAX_TX_MISC_PDUS         + \
					ISER_MAX_RX_MISC_PDUS)

#define ISER_GET_MAX_XMIT_CMDS(send_wr) ((send_wr			\
					 - ISER_MAX_TX_MISC_PDUS	\
					 - ISER_MAX_RX_MISC_PDUS) /	\
					 (1 + ISER_INFLIGHT_DATAOUTS))

#define ISER_SIGNAL_CMD_COUNT 32

/* Constant PDU lengths calculations */
#define ISER_HEADERS_LEN	(sizeof(struct iser_ctrl) + sizeof(struct iscsi_hdr))

#define ISER_RECV_DATA_SEG_LEN	128
#define ISER_RX_PAYLOAD_SIZE	(ISER_HEADERS_LEN + ISER_RECV_DATA_SEG_LEN)
#define ISER_RX_LOGIN_SIZE	(ISER_HEADERS_LEN + ISCSI_DEF_MAX_RECV_SEG_LEN)

/* Length of an object name string */
#define ISER_OBJECT_NAME_SIZE		    64

enum iser_conn_state {
	ISER_CONN_INIT,		   /* descriptor allocd, no conn          */
	ISER_CONN_PENDING,	   /* in the process of being established */
	ISER_CONN_UP,		   /* up and running                      */
	ISER_CONN_TERMINATING,	   /* in the process of being terminated  */
	ISER_CONN_DOWN,		   /* shut down                           */
	ISER_CONN_STATES_NUM
};

enum iser_task_status {
	ISER_TASK_STATUS_INIT = 0,
	ISER_TASK_STATUS_STARTED,
	ISER_TASK_STATUS_COMPLETED
};

enum iser_data_dir {
	ISER_DIR_IN = 0,	   /* to initiator */
	ISER_DIR_OUT,		   /* from initiator */
	ISER_DIRS_NUM
};

/**
 * struct iser_data_buf - iSER data buffer
 *
 * @sg:           pointer to the sg list
 * @size:         num entries of this sg
 * @data_len:     total beffer byte len
 * @dma_nents:    returned by dma_map_sg
 */
struct iser_data_buf {
	struct scatterlist *sg;
	int                size;
	unsigned long      data_len;
	unsigned int       dma_nents;
};

/* fwd declarations */
struct iser_device;
struct iscsi_iser_task;
struct iscsi_endpoint;
struct iser_reg_resources;

/**
 * struct iser_mem_reg - iSER memory registration info
 *
 * @sge:          memory region sg element
 * @rkey:         memory region remote key
 * @mem_h:        pointer to registration context (FMR/Fastreg)
 */
struct iser_mem_reg {
	struct ib_sge	 sge;
	u32		 rkey;
	void		*mem_h;
};

enum iser_desc_type {
	ISCSI_TX_CONTROL ,
	ISCSI_TX_SCSI_COMMAND,
	ISCSI_TX_DATAOUT
};

/* Maximum number of work requests per task:
 * Data memory region local invalidate + fast registration
 * Protection memory region local invalidate + fast registration
 * Signature memory region local invalidate + fast registration
 * PDU send
 */
#define ISER_MAX_WRS 7

/**
 * struct iser_tx_desc - iSER TX descriptor
 *
 * @iser_header:   iser header
 * @iscsi_header:  iscsi header
 * @type:          command/control/dataout
 * @dam_addr:      header buffer dma_address
 * @tx_sg:         sg[0] points to iser/iscsi headers
 *                 sg[1] optionally points to either of immediate data
 *                 unsolicited data-out or control
 * @num_sge:       number sges used on this TX task
 * @mapped:        Is the task header mapped
 * @wr_idx:        Current WR index
 * @wrs:           Array of WRs per task
 * @data_reg:      Data buffer registration details
 * @prot_reg:      Protection buffer registration details
 * @sig_attrs:     Signature attributes
 */
struct iser_tx_desc {
	struct iser_ctrl             iser_header;
	struct iscsi_hdr             iscsi_header;
	enum   iser_desc_type        type;
	u64		             dma_addr;
	struct ib_sge		     tx_sg[2];
	int                          num_sge;
	struct ib_cqe		     cqe;
	bool			     mapped;
	u8                           wr_idx;
	union iser_wr {
		struct ib_send_wr		send;
		struct ib_reg_wr		fast_reg;
		struct ib_sig_handover_wr	sig;
	} wrs[ISER_MAX_WRS];
	struct iser_mem_reg          data_reg;
	struct iser_mem_reg          prot_reg;
	struct ib_sig_attrs          sig_attrs;
};

#define ISER_RX_PAD_SIZE	(256 - (ISER_RX_PAYLOAD_SIZE + \
				 sizeof(u64) + sizeof(struct ib_sge) + \
				 sizeof(struct ib_cqe)))
/**
 * struct iser_rx_desc - iSER RX descriptor
 *
 * @iser_header:   iser header
 * @iscsi_header:  iscsi header
 * @data:          received data segment
 * @dma_addr:      receive buffer dma address
 * @rx_sg:         ib_sge of receive buffer
 * @pad:           for sense data TODO: Modify to maximum sense length supported
 */
struct iser_rx_desc {
	struct iser_ctrl             iser_header;
	struct iscsi_hdr             iscsi_header;
	char		             data[ISER_RECV_DATA_SEG_LEN];
	u64		             dma_addr;
	struct ib_sge		     rx_sg;
	struct ib_cqe		     cqe;
	char		             pad[ISER_RX_PAD_SIZE];
} __packed;

/**
 * struct iser_login_desc - iSER login descriptor
 *
 * @req:           pointer to login request buffer
 * @resp:          pointer to login response buffer
 * @req_dma:       DMA address of login request buffer
 * @rsp_dma:      DMA address of login response buffer
 * @sge:           IB sge for login post recv
 * @cqe:           completion handler
 */
struct iser_login_desc {
	void                         *req;
	void                         *rsp;
	u64                          req_dma;
	u64                          rsp_dma;
	struct ib_sge                sge;
	struct ib_cqe		     cqe;
} __attribute__((packed));

struct iser_conn;
struct ib_conn;
struct iscsi_iser_task;

/**
 * struct iser_comp - iSER completion context
 *
 * @cq:         completion queue
 * @active_qps: Number of active QPs attached
 *              to completion context
 */
struct iser_comp {
	struct ib_cq		*cq;
	int                      active_qps;
};

/**
 * struct iser_device - Memory registration operations
 *     per-device registration schemes
 *
 * @alloc_reg_res:     Allocate registration resources
 * @free_reg_res:      Free registration resources
 * @fast_reg_mem:      Register memory buffers
 * @unreg_mem:         Un-register memory buffers
 * @reg_desc_get:      Get a registration descriptor for pool
 * @reg_desc_put:      Get a registration descriptor to pool
 */
struct iser_reg_ops {
	int            (*alloc_reg_res)(struct ib_conn *ib_conn,
					unsigned cmds_max,
					unsigned int size);
	void           (*free_reg_res)(struct ib_conn *ib_conn);
	int            (*reg_mem)(struct iscsi_iser_task *iser_task,
				  struct iser_data_buf *mem,
				  struct iser_reg_resources *rsc,
				  struct iser_mem_reg *reg);
	void           (*unreg_mem)(struct iscsi_iser_task *iser_task,
				    enum iser_data_dir cmd_dir);
	struct iser_fr_desc * (*reg_desc_get)(struct ib_conn *ib_conn);
	void           (*reg_desc_put)(struct ib_conn *ib_conn,
				       struct iser_fr_desc *desc);
};

/**
 * struct iser_device - iSER device handle
 *
 * @ib_device:     RDMA device
 * @pd:            Protection Domain for this device
 * @mr:            Global DMA memory region
 * @event_handler: IB events handle routine
 * @ig_list:	   entry in devices list
 * @refcount:      Reference counter, dominated by open iser connections
 * @comps_used:    Number of completion contexts used, Min between online
 *                 cpus and device max completion vectors
 * @comps:         Dinamically allocated array of completion handlers
 * @reg_ops:       Registration ops
 * @remote_inv_sup: Remote invalidate is supported on this device
 */
struct iser_device {
	struct ib_device             *ib_device;
	struct ib_pd	             *pd;
	struct ib_event_handler      event_handler;
	struct list_head             ig_list;
	int                          refcount;
	int			     comps_used;
	struct iser_comp	     *comps;
	const struct iser_reg_ops    *reg_ops;
	bool                         remote_inv_sup;
};

/**
 * struct iser_reg_resources - Fast registration recources
 *
 * @mr:         memory region
 * @fmr_pool:   pool of fmrs
 * @page_vec:   fast reg page list used by fmr pool
 * @mr_valid:   is mr valid indicator
 */
struct iser_reg_resources {
	union {
		struct ib_mr             *mr;
		struct ib_fmr_pool       *fmr_pool;
	};
	struct iser_page_vec             *page_vec;
	u8				  mr_valid:1;
};

/**
 * struct iser_pi_context - Protection information context
 *
 * @rsc:             protection buffer registration resources
 * @sig_mr:          signature enable memory region
 * @sig_mr_valid:    is sig_mr valid indicator
 * @sig_protected:   is region protected indicator
 */
struct iser_pi_context {
	struct iser_reg_resources	rsc;
	struct ib_mr                   *sig_mr;
	u8                              sig_mr_valid:1;
	u8                              sig_protected:1;
};

/**
 * struct iser_fr_desc - Fast registration descriptor
 *
 * @list:           entry in connection fastreg pool
 * @rsc:            data buffer registration resources
 * @pi_ctx:         protection information context
 */
struct iser_fr_desc {
	struct list_head		  list;
	struct iser_reg_resources	  rsc;
	struct iser_pi_context		 *pi_ctx;
	struct list_head                  all_list;
};

/**
 * struct iser_fr_pool: connection fast registration pool
 *
 * @list:                list of fastreg descriptors
 * @lock:                protects fmr/fastreg pool
 * @size:                size of the pool
 */
struct iser_fr_pool {
	struct list_head        list;
	spinlock_t              lock;
	int                     size;
	struct list_head        all_list;
};

/**
 * struct ib_conn - Infiniband related objects
 *
 * @cma_id:              rdma_cm connection maneger handle
 * @qp:                  Connection Queue-pair
 * @post_recv_buf_count: post receive counter
 * @sig_count:           send work request signal count
 * @rx_wr:               receive work request for batch posts
 * @device:              reference to iser device
 * @comp:                iser completion context
 * @fr_pool:             connection fast registration poool
 * @pi_support:          Indicate device T10-PI support
 */
struct ib_conn {
	struct rdma_cm_id           *cma_id;
	struct ib_qp	            *qp;
	int                          post_recv_buf_count;
	u8                           sig_count;
	struct ib_recv_wr	     rx_wr[ISER_MIN_POSTED_RX];
	struct iser_device          *device;
	struct iser_comp	    *comp;
	struct iser_fr_pool          fr_pool;
	bool			     pi_support;
	struct ib_cqe		     reg_cqe;
};

/**
 * struct iser_conn - iSER connection context
 *
 * @ib_conn:          connection RDMA resources
 * @iscsi_conn:       link to matching iscsi connection
 * @ep:               transport handle
 * @state:            connection logical state
 * @qp_max_recv_dtos: maximum number of data outs, corresponds
 *                    to max number of post recvs
 * @qp_max_recv_dtos_mask: (qp_max_recv_dtos - 1)
 * @min_posted_rx:    (qp_max_recv_dtos >> 2)
 * @max_cmds:         maximum cmds allowed for this connection
 * @name:             connection peer portal
 * @release_work:     deffered work for release job
 * @state_mutex:      protects iser onnection state
 * @stop_completion:  conn_stop completion
 * @ib_completion:    RDMA cleanup completion
 * @up_completion:    connection establishment completed
 *                    (state is ISER_CONN_UP)
 * @conn_list:        entry in ig conn list
 * @login_desc:       login descriptor
 * @rx_desc_head:     head of rx_descs cyclic buffer
 * @rx_descs:         rx buffers array (cyclic buffer)
 * @num_rx_descs:     number of rx descriptors
 * @scsi_sg_tablesize: scsi host sg_tablesize
 * @pages_per_mr:     maximum pages available for registration
 */
struct iser_conn {
	struct ib_conn		     ib_conn;
	struct iscsi_conn	     *iscsi_conn;
	struct iscsi_endpoint	     *ep;
	enum iser_conn_state	     state;
	unsigned		     qp_max_recv_dtos;
	unsigned		     qp_max_recv_dtos_mask;
	unsigned		     min_posted_rx;
	u16                          max_cmds;
	char 			     name[ISER_OBJECT_NAME_SIZE];
	struct work_struct	     release_work;
	struct mutex		     state_mutex;
	struct completion	     stop_completion;
	struct completion	     ib_completion;
	struct completion	     up_completion;
	struct list_head	     conn_list;
	struct iser_login_desc       login_desc;
	unsigned int 		     rx_desc_head;
	struct iser_rx_desc	     *rx_descs;
	u32                          num_rx_descs;
	unsigned short               scsi_sg_tablesize;
	unsigned short               pages_per_mr;
	bool			     snd_w_inv;
};

/**
 * struct iscsi_iser_task - iser task context
 *
 * @desc:     TX descriptor
 * @iser_conn:        link to iser connection
 * @status:           current task status
 * @sc:               link to scsi command
 * @command_sent:     indicate if command was sent
 * @dir:              iser data direction
 * @rdma_reg:         task rdma registration desc
 * @data:             iser data buffer desc
 * @prot:             iser protection buffer desc
 */
struct iscsi_iser_task {
	struct iser_tx_desc          desc;
	struct iser_conn	     *iser_conn;
	enum iser_task_status 	     status;
	struct scsi_cmnd	     *sc;
	int                          command_sent;
	int                          dir[ISER_DIRS_NUM];
	struct iser_mem_reg          rdma_reg[ISER_DIRS_NUM];
	struct iser_data_buf         data[ISER_DIRS_NUM];
	struct iser_data_buf         prot[ISER_DIRS_NUM];
};

struct iser_page_vec {
	u64 *pages;
	int npages;
	struct ib_mr fake_mr;
};

/**
 * struct iser_global: iSER global context
 *
 * @device_list_mutex:    protects device_list
 * @device_list:          iser devices global list
 * @connlist_mutex:       protects connlist
 * @connlist:             iser connections global list
 * @desc_cache:           kmem cache for tx dataout
 */
struct iser_global {
	struct mutex      device_list_mutex;
	struct list_head  device_list;
	struct mutex      connlist_mutex;
	struct list_head  connlist;
	struct kmem_cache *desc_cache;
};

extern struct iser_global ig;
extern int iser_debug_level;
extern bool iser_pi_enable;
extern int iser_pi_guard;
extern unsigned int iser_max_sectors;
extern bool iser_always_reg;

int iser_assign_reg_ops(struct iser_device *device);

int iser_send_control(struct iscsi_conn *conn,
		      struct iscsi_task *task);

int iser_send_command(struct iscsi_conn *conn,
		      struct iscsi_task *task);

int iser_send_data_out(struct iscsi_conn *conn,
		       struct iscsi_task *task,
		       struct iscsi_data *hdr);

void iscsi_iser_recv(struct iscsi_conn *conn,
		     struct iscsi_hdr *hdr,
		     char *rx_data,
		     int rx_data_len);

void iser_conn_init(struct iser_conn *iser_conn);

void iser_conn_release(struct iser_conn *iser_conn);

int iser_conn_terminate(struct iser_conn *iser_conn);

void iser_release_work(struct work_struct *work);

void iser_err_comp(struct ib_wc *wc, const char *type);
void iser_login_rsp(struct ib_cq *cq, struct ib_wc *wc);
void iser_task_rsp(struct ib_cq *cq, struct ib_wc *wc);
void iser_cmd_comp(struct ib_cq *cq, struct ib_wc *wc);
void iser_ctrl_comp(struct ib_cq *cq, struct ib_wc *wc);
void iser_dataout_comp(struct ib_cq *cq, struct ib_wc *wc);
void iser_reg_comp(struct ib_cq *cq, struct ib_wc *wc);

void iser_task_rdma_init(struct iscsi_iser_task *task);

void iser_task_rdma_finalize(struct iscsi_iser_task *task);

void iser_free_rx_descriptors(struct iser_conn *iser_conn);

void iser_finalize_rdma_unaligned_sg(struct iscsi_iser_task *iser_task,
				     struct iser_data_buf *mem,
				     enum iser_data_dir cmd_dir);

int iser_reg_rdma_mem(struct iscsi_iser_task *task,
		      enum iser_data_dir dir,
		      bool all_imm);
void iser_unreg_rdma_mem(struct iscsi_iser_task *task,
			 enum iser_data_dir dir);

int  iser_connect(struct iser_conn *iser_conn,
		  struct sockaddr *src_addr,
		  struct sockaddr *dst_addr,
		  int non_blocking);

void iser_unreg_mem_fmr(struct iscsi_iser_task *iser_task,
			enum iser_data_dir cmd_dir);
void iser_unreg_mem_fastreg(struct iscsi_iser_task *iser_task,
			    enum iser_data_dir cmd_dir);

int  iser_post_recvl(struct iser_conn *iser_conn);
int  iser_post_recvm(struct iser_conn *iser_conn, int count);
int  iser_post_send(struct ib_conn *ib_conn, struct iser_tx_desc *tx_desc,
		    bool signal);

int iser_dma_map_task_data(struct iscsi_iser_task *iser_task,
			   struct iser_data_buf *data,
			   enum iser_data_dir iser_dir,
			   enum dma_data_direction dma_dir);

void iser_dma_unmap_task_data(struct iscsi_iser_task *iser_task,
			      struct iser_data_buf *data,
			      enum dma_data_direction dir);

int  iser_initialize_task_headers(struct iscsi_task *task,
			struct iser_tx_desc *tx_desc);
int iser_alloc_rx_descriptors(struct iser_conn *iser_conn,
			      struct iscsi_session *session);
int iser_alloc_fmr_pool(struct ib_conn *ib_conn,
			unsigned cmds_max,
			unsigned int size);
void iser_free_fmr_pool(struct ib_conn *ib_conn);
int iser_alloc_fastreg_pool(struct ib_conn *ib_conn,
			    unsigned cmds_max,
			    unsigned int size);
void iser_free_fastreg_pool(struct ib_conn *ib_conn);
u8 iser_check_task_pi_status(struct iscsi_iser_task *iser_task,
			     enum iser_data_dir cmd_dir, sector_t *sector);
struct iser_fr_desc *
iser_reg_desc_get_fr(struct ib_conn *ib_conn);
void
iser_reg_desc_put_fr(struct ib_conn *ib_conn,
		     struct iser_fr_desc *desc);
struct iser_fr_desc *
iser_reg_desc_get_fmr(struct ib_conn *ib_conn);
void
iser_reg_desc_put_fmr(struct ib_conn *ib_conn,
		      struct iser_fr_desc *desc);

static inline struct ib_send_wr *
iser_tx_next_wr(struct iser_tx_desc *tx_desc)
{
	struct ib_send_wr *cur_wr = &tx_desc->wrs[tx_desc->wr_idx].send;
	struct ib_send_wr *last_wr;

	if (tx_desc->wr_idx) {
		last_wr = &tx_desc->wrs[tx_desc->wr_idx - 1].send;
		last_wr->next = cur_wr;
	}
	tx_desc->wr_idx++;

	return cur_wr;
}

static inline struct iser_conn *
to_iser_conn(struct ib_conn *ib_conn)
{
	return container_of(ib_conn, struct iser_conn, ib_conn);
}

static inline struct iser_rx_desc *
iser_rx(struct ib_cqe *cqe)
{
	return container_of(cqe, struct iser_rx_desc, cqe);
}

static inline struct iser_tx_desc *
iser_tx(struct ib_cqe *cqe)
{
	return container_of(cqe, struct iser_tx_desc, cqe);
}

static inline struct iser_login_desc *
iser_login(struct ib_cqe *cqe)
{
	return container_of(cqe, struct iser_login_desc, cqe);
}

#endif
