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
 *
 * $Id: iscsi_iser.h 7051 2006-05-10 12:29:11Z ogerlitz $
 */
#ifndef __ISCSI_ISER_H__
#define __ISCSI_ISER_H__

#include <linux/types.h>
#include <linux/net.h>
#include <scsi/libiscsi.h>
#include <scsi/scsi_transport_iscsi.h>

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
#define DRV_VER		"0.1"
#define DRV_DATE	"May 7th, 2006"

#define iser_dbg(fmt, arg...)				\
	do {						\
		if (iser_debug_level > 0)		\
			printk(KERN_DEBUG PFX "%s:" fmt,\
				__func__ , ## arg);	\
	} while (0)

#define iser_err(fmt, arg...)				\
	do {						\
		printk(KERN_ERR PFX "%s:" fmt,          \
		       __func__ , ## arg);		\
	} while (0)

#define SHIFT_4K	12
#define SIZE_4K	(1UL << SHIFT_4K)
#define MASK_4K	(~(SIZE_4K-1))

					/* support upto 512KB in one RDMA */
#define ISCSI_ISER_SG_TABLESIZE         (0x80000 >> SHIFT_4K)
#define ISCSI_ISER_MAX_LUN		256
#define ISCSI_ISER_MAX_CMD_LEN		16

/* QP settings */
/* Maximal bounds on received asynchronous PDUs */
#define ISER_MAX_RX_MISC_PDUS		4 /* NOOP_IN(2) , ASYNC_EVENT(2)   */

#define ISER_MAX_TX_MISC_PDUS		6 /* NOOP_OUT(2), TEXT(1),         *
					   * SCSI_TMFUNC(2), LOGOUT(1) */

#define ISER_QP_MAX_RECV_DTOS		(ISCSI_DEF_XMIT_CMDS_MAX + \
					ISER_MAX_RX_MISC_PDUS    +  \
					ISER_MAX_TX_MISC_PDUS)

/* the max TX (send) WR supported by the iSER QP is defined by                 *
 * max_send_wr = T * (1 + D) + C ; D is how many inflight dataouts we expect   *
 * to have at max for SCSI command. The tx posting & completion handling code  *
 * supports -EAGAIN scheme where tx is suspended till the QP has room for more *
 * send WR. D=8 comes from 64K/8K                                              */

#define ISER_INFLIGHT_DATAOUTS		8

#define ISER_QP_MAX_REQ_DTOS		(ISCSI_DEF_XMIT_CMDS_MAX *    \
					(1 + ISER_INFLIGHT_DATAOUTS) + \
					ISER_MAX_TX_MISC_PDUS        + \
					ISER_MAX_RX_MISC_PDUS)

#define ISER_VER			0x10
#define ISER_WSV			0x08
#define ISER_RSV			0x04

struct iser_hdr {
	u8      flags;
	u8      rsvd[3];
	__be32  write_stag; /* write rkey */
	__be64  write_va;
	__be32  read_stag;  /* read rkey */
	__be64  read_va;
} __attribute__((packed));


/* Length of an object name string */
#define ISER_OBJECT_NAME_SIZE		    64

enum iser_ib_conn_state {
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

struct iser_data_buf {
	void               *buf;      /* pointer to the sg list               */
	unsigned int       size;      /* num entries of this sg               */
	unsigned long      data_len;  /* total data len                       */
	unsigned int       dma_nents; /* returned by dma_map_sg               */
	char       	   *copy_buf; /* allocated copy buf for SGs unaligned *
	                               * for rdma which are copied            */
	struct scatterlist sg_single; /* SG-ified clone of a non SG SC or     *
				       * unaligned SG                         */
  };

/* fwd declarations */
struct iser_device;
struct iscsi_iser_conn;
struct iscsi_iser_cmd_task;

struct iser_mem_reg {
	u32  lkey;
	u32  rkey;
	u64  va;
	u64  len;
	void *mem_h;
	int  is_fmr;
};

struct iser_regd_buf {
	struct iser_mem_reg     reg;        /* memory registration info        */
	void                    *virt_addr;
	struct iser_device      *device;    /* device->device for dma_unmap    */
	u64                     dma_addr;   /* if non zero, addr for dma_unmap */
	enum dma_data_direction direction;  /* direction for dma_unmap	       */
	unsigned int            data_size;
	atomic_t                ref_count;  /* refcount, freed when dec to 0   */
};

#define MAX_REGD_BUF_VECTOR_LEN	2

struct iser_dto {
	struct iscsi_iser_cmd_task *ctask;
	struct iser_conn *ib_conn;
	int                        notify_enable;

	/* vector of registered buffers */
	unsigned int               regd_vector_len;
	struct iser_regd_buf       *regd[MAX_REGD_BUF_VECTOR_LEN];

	/* offset into the registered buffer may be specified */
	unsigned int               offset[MAX_REGD_BUF_VECTOR_LEN];

	/* a smaller size may be specified, if 0, then full size is used */
	unsigned int               used_sz[MAX_REGD_BUF_VECTOR_LEN];
};

enum iser_desc_type {
	ISCSI_RX,
	ISCSI_TX_CONTROL ,
	ISCSI_TX_SCSI_COMMAND,
	ISCSI_TX_DATAOUT
};

struct iser_desc {
	struct iser_hdr              iser_header;
	struct iscsi_hdr             iscsi_header;
	struct iser_regd_buf         hdr_regd_buf;
	void                         *data;         /* used by RX & TX_CONTROL */
	struct iser_regd_buf         data_regd_buf; /* used by RX & TX_CONTROL */
	enum   iser_desc_type        type;
	struct iser_dto              dto;
};

struct iser_device {
	struct ib_device             *ib_device;
	struct ib_pd	             *pd;
	struct ib_cq	             *cq;
	struct ib_mr	             *mr;
	struct tasklet_struct	     cq_tasklet;
	struct list_head             ig_list; /* entry in ig devices list */
	int                          refcount;
};

struct iser_conn {
	struct iscsi_iser_conn       *iser_conn; /* iser conn for upcalls  */
	enum iser_ib_conn_state	     state;	    /* rdma connection state   */
	spinlock_t		     lock;	    /* used for state changes  */
	struct iser_device           *device;       /* device context          */
	struct rdma_cm_id            *cma_id;       /* CMA ID		       */
	struct ib_qp	             *qp;           /* QP 		       */
	struct ib_fmr_pool           *fmr_pool;     /* pool of IB FMRs         */
	int                          disc_evt_flag; /* disconn event delivered */
	wait_queue_head_t	     wait;          /* waitq for conn/disconn  */
	atomic_t                     post_recv_buf_count; /* posted rx count   */
	atomic_t                     post_send_buf_count; /* posted tx count   */
	char 			     name[ISER_OBJECT_NAME_SIZE];
	struct iser_page_vec         *page_vec;     /* represents SG to fmr maps*
						     * maps serialized as tx is*/
	struct list_head	     conn_list;       /* entry in ig conn list */
};

struct iscsi_iser_conn {
	struct iscsi_conn            *iscsi_conn;/* ptr to iscsi conn */
	struct iser_conn             *ib_conn;   /* iSER IB conn      */

	rwlock_t		     lock;
};

struct iscsi_iser_cmd_task {
	struct iser_desc             desc;
	struct iscsi_iser_conn	     *iser_conn;
	enum iser_task_status 	     status;
	int                          command_sent;  /* set if command  sent  */
	int                          dir[ISER_DIRS_NUM];      /* set if dir use*/
	struct iser_regd_buf         rdma_regd[ISER_DIRS_NUM];/* regd rdma buf */
	struct iser_data_buf         data[ISER_DIRS_NUM];     /* orig. data des*/
	struct iser_data_buf         data_copy[ISER_DIRS_NUM];/* contig. copy  */
};

struct iser_page_vec {
	u64 *pages;
	int length;
	int offset;
	int data_size;
};

struct iser_global {
	struct mutex      device_list_mutex;/*                   */
	struct list_head  device_list;	     /* all iSER devices */
	struct mutex      connlist_mutex;
	struct list_head  connlist;		/* all iSER IB connections */

	struct kmem_cache *desc_cache;
};

extern struct iser_global ig;
extern int iser_debug_level;

/* allocate connection resources needed for rdma functionality */
int iser_conn_set_full_featured_mode(struct iscsi_conn *conn);

int iser_send_control(struct iscsi_conn      *conn,
		      struct iscsi_mgmt_task *mtask);

int iser_send_command(struct iscsi_conn      *conn,
		      struct iscsi_cmd_task  *ctask);

int iser_send_data_out(struct iscsi_conn     *conn,
		       struct iscsi_cmd_task *ctask,
		       struct iscsi_data          *hdr);

void iscsi_iser_recv(struct iscsi_conn *conn,
		     struct iscsi_hdr       *hdr,
		     char                   *rx_data,
		     int                    rx_data_len);

int  iser_conn_init(struct iser_conn **ib_conn);

void iser_conn_terminate(struct iser_conn *ib_conn);

void iser_rcv_completion(struct iser_desc *desc,
			 unsigned long    dto_xfer_len);

void iser_snd_completion(struct iser_desc *desc);

void iser_ctask_rdma_init(struct iscsi_iser_cmd_task     *ctask);

void iser_ctask_rdma_finalize(struct iscsi_iser_cmd_task *ctask);

void iser_dto_buffs_release(struct iser_dto *dto);

int  iser_regd_buff_release(struct iser_regd_buf *regd_buf);

void iser_reg_single(struct iser_device      *device,
		     struct iser_regd_buf    *regd_buf,
		     enum dma_data_direction direction);

void iser_finalize_rdma_unaligned_sg(struct iscsi_iser_cmd_task *ctask,
				     enum iser_data_dir         cmd_dir);

int  iser_reg_rdma_mem(struct iscsi_iser_cmd_task *ctask,
		       enum   iser_data_dir        cmd_dir);

int  iser_connect(struct iser_conn   *ib_conn,
		  struct sockaddr_in *src_addr,
		  struct sockaddr_in *dst_addr,
		  int                non_blocking);

int  iser_reg_page_vec(struct iser_conn     *ib_conn,
		       struct iser_page_vec *page_vec,
		       struct iser_mem_reg  *mem_reg);

void iser_unreg_mem(struct iser_mem_reg *mem_reg);

int  iser_post_recv(struct iser_desc *rx_desc);
int  iser_post_send(struct iser_desc *tx_desc);

int iser_conn_state_comp(struct iser_conn *ib_conn,
			 enum iser_ib_conn_state comp);

int iser_dma_map_task_data(struct iscsi_iser_cmd_task *iser_ctask,
			    struct iser_data_buf       *data,
			    enum   iser_data_dir       iser_dir,
			    enum   dma_data_direction  dma_dir);

void iser_dma_unmap_task_data(struct iscsi_iser_cmd_task *iser_ctask);
#endif
