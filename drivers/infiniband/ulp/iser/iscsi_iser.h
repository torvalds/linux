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
#define DRV_VER		"1.4.8"

#define iser_dbg(fmt, arg...)				 \
	do {						 \
		if (iser_debug_level > 2)		 \
			printk(KERN_DEBUG PFX "%s: " fmt,\
				__func__ , ## arg);	 \
	} while (0)

#define iser_warn(fmt, arg...)				\
	do {						\
		if (iser_debug_level > 0)		\
			pr_warn(PFX "%s: " fmt,		\
				__func__ , ## arg);	\
	} while (0)

#define iser_info(fmt, arg...)				\
	do {						\
		if (iser_debug_level > 1)		\
			pr_info(PFX "%s: " fmt,		\
				__func__ , ## arg);	\
	} while (0)

#define iser_err(fmt, arg...)				\
	do {						\
		printk(KERN_ERR PFX "%s: " fmt,		\
		       __func__ , ## arg);		\
	} while (0)

#define SHIFT_4K	12
#define SIZE_4K	(1ULL << SHIFT_4K)
#define MASK_4K	(~(SIZE_4K-1))
					/* support up to 512KB in one RDMA */
#define ISCSI_ISER_SG_TABLESIZE         (0x80000 >> SHIFT_4K)
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

#define ISER_WC_BATCH_COUNT   16
#define ISER_SIGNAL_CMD_COUNT 32

#define ISER_VER			0x10
#define ISER_WSV			0x08
#define ISER_RSV			0x04

#define ISER_FASTREG_LI_WRID		0xffffffffffffffffULL
#define ISER_BEACON_WRID		0xfffffffffffffffeULL

/**
 * struct iser_hdr - iSER header
 *
 * @flags:        flags support (zbva, remote_inv)
 * @rsvd:         reserved
 * @write_stag:   write rkey
 * @write_va:     write virtual address
 * @reaf_stag:    read rkey
 * @read_va:      read virtual address
 */
struct iser_hdr {
	u8      flags;
	u8      rsvd[3];
	__be32  write_stag;
	__be64  write_va;
	__be32  read_stag;
	__be64  read_va;
} __attribute__((packed));


#define ISER_ZBVA_NOT_SUPPORTED		0x80
#define ISER_SEND_W_INV_NOT_SUPPORTED	0x40

struct iser_cm_hdr {
	u8      flags;
	u8      rsvd[3];
} __packed;

/* Constant PDU lengths calculations */
#define ISER_HEADERS_LEN  (sizeof(struct iser_hdr) + sizeof(struct iscsi_hdr))

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
 * @buf:          pointer to the sg list
 * @size:         num entries of this sg
 * @data_len:     total beffer byte len
 * @dma_nents:    returned by dma_map_sg
 * @copy_buf:     allocated copy buf for SGs unaligned
 *                for rdma which are copied
 * @sg_single:    SG-ified clone of a non SG SC or
 *                unaligned SG
 */
struct iser_data_buf {
	void               *buf;
	unsigned int       size;
	unsigned long      data_len;
	unsigned int       dma_nents;
	char               *copy_buf;
	struct scatterlist sg_single;
  };

/* fwd declarations */
struct iser_device;
struct iscsi_iser_task;
struct iscsi_endpoint;

/**
 * struct iser_mem_reg - iSER memory registration info
 *
 * @lkey:         MR local key
 * @rkey:         MR remote key
 * @va:           MR start address (buffer va)
 * @len:          MR length
 * @mem_h:        pointer to registration context (FMR/Fastreg)
 * @is_mr:        indicates weather we registered the buffer
 */
struct iser_mem_reg {
	u32  lkey;
	u32  rkey;
	u64  va;
	u64  len;
	void *mem_h;
	int  is_mr;
};

/**
 * struct iser_regd_buf - iSER buffer registration desc
 *
 * @reg:          memory registration info
 * @virt_addr:    virtual address of buffer
 * @device:       reference to iser device
 * @direction:    dma direction (for dma_unmap)
 * @data_size:    data buffer size in bytes
 */
struct iser_regd_buf {
	struct iser_mem_reg     reg;
	void                    *virt_addr;
	struct iser_device      *device;
	enum dma_data_direction direction;
	unsigned int            data_size;
};

enum iser_desc_type {
	ISCSI_TX_CONTROL ,
	ISCSI_TX_SCSI_COMMAND,
	ISCSI_TX_DATAOUT
};

/**
 * struct iser_tx_desc - iSER TX descriptor (for send wr_id)
 *
 * @iser_header:   iser header
 * @iscsi_header:  iscsi header
 * @type:          command/control/dataout
 * @dam_addr:      header buffer dma_address
 * @tx_sg:         sg[0] points to iser/iscsi headers
 *                 sg[1] optionally points to either of immediate data
 *                 unsolicited data-out or control
 * @num_sge:       number sges used on this TX task
 */
struct iser_tx_desc {
	struct iser_hdr              iser_header;
	struct iscsi_hdr             iscsi_header;
	enum   iser_desc_type        type;
	u64		             dma_addr;
	struct ib_sge		     tx_sg[2];
	int                          num_sge;
};

#define ISER_RX_PAD_SIZE	(256 - (ISER_RX_PAYLOAD_SIZE + \
					sizeof(u64) + sizeof(struct ib_sge)))
/**
 * struct iser_rx_desc - iSER RX descriptor (for recv wr_id)
 *
 * @iser_header:   iser header
 * @iscsi_header:  iscsi header
 * @data:          received data segment
 * @dma_addr:      receive buffer dma address
 * @rx_sg:         ib_sge of receive buffer
 * @pad:           for sense data TODO: Modify to maximum sense length supported
 */
struct iser_rx_desc {
	struct iser_hdr              iser_header;
	struct iscsi_hdr             iscsi_header;
	char		             data[ISER_RECV_DATA_SEG_LEN];
	u64		             dma_addr;
	struct ib_sge		     rx_sg;
	char		             pad[ISER_RX_PAD_SIZE];
} __attribute__((packed));

#define ISER_MAX_CQ 4

struct iser_conn;
struct ib_conn;
struct iscsi_iser_task;

/**
 * struct iser_comp - iSER completion context
 *
 * @device:     pointer to device handle
 * @cq:         completion queue
 * @wcs:        work completion array
 * @tasklet:    Tasklet handle
 * @active_qps: Number of active QPs attached
 *              to completion context
 */
struct iser_comp {
	struct iser_device      *device;
	struct ib_cq		*cq;
	struct ib_wc		 wcs[ISER_WC_BATCH_COUNT];
	struct tasklet_struct	 tasklet;
	int                      active_qps;
};

/**
 * struct iser_device - iSER device handle
 *
 * @ib_device:     RDMA device
 * @pd:            Protection Domain for this device
 * @dev_attr:      Device attributes container
 * @mr:            Global DMA memory region
 * @event_handler: IB events handle routine
 * @ig_list:	   entry in devices list
 * @refcount:      Reference counter, dominated by open iser connections
 * @comps_used:    Number of completion contexts used, Min between online
 *                 cpus and device max completion vectors
 * @comps:         Dinamically allocated array of completion handlers
 * Memory registration pool Function pointers (FMR or Fastreg):
 *     @iser_alloc_rdma_reg_res: Allocation of memory regions pool
 *     @iser_free_rdma_reg_res:  Free of memory regions pool
 *     @iser_reg_rdma_mem:       Memory registration routine
 *     @iser_unreg_rdma_mem:     Memory deregistration routine
 */
struct iser_device {
	struct ib_device             *ib_device;
	struct ib_pd	             *pd;
	struct ib_device_attr	     dev_attr;
	struct ib_mr	             *mr;
	struct ib_event_handler      event_handler;
	struct list_head             ig_list;
	int                          refcount;
	int			     comps_used;
	struct iser_comp	     comps[ISER_MAX_CQ];
	int                          (*iser_alloc_rdma_reg_res)(struct ib_conn *ib_conn,
								unsigned cmds_max);
	void                         (*iser_free_rdma_reg_res)(struct ib_conn *ib_conn);
	int                          (*iser_reg_rdma_mem)(struct iscsi_iser_task *iser_task,
							  enum iser_data_dir cmd_dir);
	void                         (*iser_unreg_rdma_mem)(struct iscsi_iser_task *iser_task,
							    enum iser_data_dir cmd_dir);
};

#define ISER_CHECK_GUARD	0xc0
#define ISER_CHECK_REFTAG	0x0f
#define ISER_CHECK_APPTAG	0x30

enum iser_reg_indicator {
	ISER_DATA_KEY_VALID	= 1 << 0,
	ISER_PROT_KEY_VALID	= 1 << 1,
	ISER_SIG_KEY_VALID	= 1 << 2,
	ISER_FASTREG_PROTECTED	= 1 << 3,
};

/**
 * struct iser_pi_context - Protection information context
 *
 * @prot_mr:        protection memory region
 * @prot_frpl:      protection fastreg page list
 * @sig_mr:         signature feature enabled memory region
 */
struct iser_pi_context {
	struct ib_mr                   *prot_mr;
	struct ib_fast_reg_page_list   *prot_frpl;
	struct ib_mr                   *sig_mr;
};

/**
 * struct fast_reg_descriptor - Fast registration descriptor
 *
 * @list:           entry in connection fastreg pool
 * @data_mr:        data memory region
 * @data_frpl:      data fastreg page list
 * @pi_ctx:         protection information context
 * @reg_indicators: fast registration indicators
 */
struct fast_reg_descriptor {
	struct list_head		  list;
	struct ib_mr			 *data_mr;
	struct ib_fast_reg_page_list     *data_frpl;
	struct iser_pi_context		 *pi_ctx;
	u8				  reg_indicators;
};

/**
 * struct ib_conn - Infiniband related objects
 *
 * @cma_id:              rdma_cm connection maneger handle
 * @qp:                  Connection Queue-pair
 * @post_recv_buf_count: post receive counter
 * @rx_wr:               receive work request for batch posts
 * @device:              reference to iser device
 * @comp:                iser completion context
 * @pi_support:          Indicate device T10-PI support
 * @beacon:              beacon send wr to signal all flush errors were drained
 * @flush_comp:          completes when all connection completions consumed
 * @lock:                protects fmr/fastreg pool
 * @union.fmr:
 *     @pool:            FMR pool for fast registrations
 *     @page_vec:        page vector to hold mapped commands pages
 *                       used for registration
 * @union.fastreg:
 *     @pool:            Fast registration descriptors pool for fast
 *                       registrations
 *     @pool_size:       Size of pool
 */
struct ib_conn {
	struct rdma_cm_id           *cma_id;
	struct ib_qp	            *qp;
	int                          post_recv_buf_count;
	struct ib_recv_wr	     rx_wr[ISER_MIN_POSTED_RX];
	struct iser_device          *device;
	struct iser_comp	    *comp;
	bool			     pi_support;
	struct ib_send_wr	     beacon;
	struct completion	     flush_comp;
	spinlock_t		     lock;
	union {
		struct {
			struct ib_fmr_pool      *pool;
			struct iser_page_vec	*page_vec;
		} fmr;
		struct {
			struct list_head	 pool;
			int			 pool_size;
		} fastreg;
	};
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
 * @name:             connection peer portal
 * @release_work:     deffered work for release job
 * @state_mutex:      protects iser onnection state
 * @stop_completion:  conn_stop completion
 * @ib_completion:    RDMA cleanup completion
 * @up_completion:    connection establishment completed
 *                    (state is ISER_CONN_UP)
 * @conn_list:        entry in ig conn list
 * @login_buf:        login data buffer (stores login parameters)
 * @login_req_buf:    login request buffer
 * @login_req_dma:    login request buffer dma address
 * @login_resp_buf:   login response buffer
 * @login_resp_dma:   login response buffer dma address
 * @rx_desc_head:     head of rx_descs cyclic buffer
 * @rx_descs:         rx buffers array (cyclic buffer)
 * @num_rx_descs:     number of rx descriptors
 */
struct iser_conn {
	struct ib_conn		     ib_conn;
	struct iscsi_conn	     *iscsi_conn;
	struct iscsi_endpoint	     *ep;
	enum iser_conn_state	     state;
	unsigned		     qp_max_recv_dtos;
	unsigned		     qp_max_recv_dtos_mask;
	unsigned		     min_posted_rx;
	char 			     name[ISER_OBJECT_NAME_SIZE];
	struct work_struct	     release_work;
	struct mutex		     state_mutex;
	struct completion	     stop_completion;
	struct completion	     ib_completion;
	struct completion	     up_completion;
	struct list_head	     conn_list;

	char  			     *login_buf;
	char			     *login_req_buf, *login_resp_buf;
	u64			     login_req_dma, login_resp_dma;
	unsigned int 		     rx_desc_head;
	struct iser_rx_desc	     *rx_descs;
	u32                          num_rx_descs;
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
 * @rdma_regd:        task rdma registration desc
 * @data:             iser data buffer desc
 * @data_copy:        iser data copy buffer desc (bounce buffer)
 * @prot:             iser protection buffer desc
 * @prot_copy:        iser protection copy buffer desc (bounce buffer)
 */
struct iscsi_iser_task {
	struct iser_tx_desc          desc;
	struct iser_conn	     *iser_conn;
	enum iser_task_status 	     status;
	struct scsi_cmnd	     *sc;
	int                          command_sent;
	int                          dir[ISER_DIRS_NUM];
	struct iser_regd_buf         rdma_regd[ISER_DIRS_NUM];
	struct iser_data_buf         data[ISER_DIRS_NUM];
	struct iser_data_buf         data_copy[ISER_DIRS_NUM];
	struct iser_data_buf         prot[ISER_DIRS_NUM];
	struct iser_data_buf         prot_copy[ISER_DIRS_NUM];
};

struct iser_page_vec {
	u64 *pages;
	int length;
	int offset;
	int data_size;
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

void iser_rcv_completion(struct iser_rx_desc *desc,
			 unsigned long dto_xfer_len,
			 struct ib_conn *ib_conn);

void iser_snd_completion(struct iser_tx_desc *desc,
			 struct ib_conn *ib_conn);

void iser_task_rdma_init(struct iscsi_iser_task *task);

void iser_task_rdma_finalize(struct iscsi_iser_task *task);

void iser_free_rx_descriptors(struct iser_conn *iser_conn);

void iser_finalize_rdma_unaligned_sg(struct iscsi_iser_task *iser_task,
				     struct iser_data_buf *mem,
				     struct iser_data_buf *mem_copy,
				     enum iser_data_dir cmd_dir);

int  iser_reg_rdma_mem_fmr(struct iscsi_iser_task *task,
			   enum iser_data_dir cmd_dir);
int  iser_reg_rdma_mem_fastreg(struct iscsi_iser_task *task,
			       enum iser_data_dir cmd_dir);

int  iser_connect(struct iser_conn *iser_conn,
		  struct sockaddr *src_addr,
		  struct sockaddr *dst_addr,
		  int non_blocking);

int  iser_reg_page_vec(struct ib_conn *ib_conn,
		       struct iser_page_vec *page_vec,
		       struct iser_mem_reg *mem_reg);

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
			      struct iser_data_buf *data);
int  iser_initialize_task_headers(struct iscsi_task *task,
			struct iser_tx_desc *tx_desc);
int iser_alloc_rx_descriptors(struct iser_conn *iser_conn,
			      struct iscsi_session *session);
int iser_create_fmr_pool(struct ib_conn *ib_conn, unsigned cmds_max);
void iser_free_fmr_pool(struct ib_conn *ib_conn);
int iser_create_fastreg_pool(struct ib_conn *ib_conn, unsigned cmds_max);
void iser_free_fastreg_pool(struct ib_conn *ib_conn);
u8 iser_check_task_pi_status(struct iscsi_iser_task *iser_task,
			     enum iser_data_dir cmd_dir, sector_t *sector);
#endif
