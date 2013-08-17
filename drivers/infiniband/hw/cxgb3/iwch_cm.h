/*
 * Copyright (c) 2006 Chelsio, Inc. All rights reserved.
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
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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
#ifndef _IWCH_CM_H_
#define _IWCH_CM_H_

#include <linux/inet.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/kref.h>

#include <rdma/ib_verbs.h>
#include <rdma/iw_cm.h>

#include "cxgb3_offload.h"
#include "iwch_provider.h"

#define MPA_KEY_REQ "MPA ID Req Frame"
#define MPA_KEY_REP "MPA ID Rep Frame"

#define MPA_MAX_PRIVATE_DATA	256
#define MPA_REV		0	/* XXX - amso1100 uses rev 0 ! */
#define MPA_REJECT		0x20
#define MPA_CRC			0x40
#define MPA_MARKERS		0x80
#define MPA_FLAGS_MASK		0xE0

#define put_ep(ep) { \
	PDBG("put_ep (via %s:%u) ep %p refcnt %d\n", __func__, __LINE__,  \
	     ep, atomic_read(&((ep)->kref.refcount))); \
	WARN_ON(atomic_read(&((ep)->kref.refcount)) < 1); \
	kref_put(&((ep)->kref), __free_ep); \
}

#define get_ep(ep) { \
	PDBG("get_ep (via %s:%u) ep %p, refcnt %d\n", __func__, __LINE__, \
	     ep, atomic_read(&((ep)->kref.refcount))); \
	kref_get(&((ep)->kref));  \
}

struct mpa_message {
	u8 key[16];
	u8 flags;
	u8 revision;
	__be16 private_data_size;
	u8 private_data[0];
};

struct terminate_message {
	u8 layer_etype;
	u8 ecode;
	__be16 hdrct_rsvd;
	u8 len_hdrs[0];
};

#define TERM_MAX_LENGTH (sizeof(struct terminate_message) + 2 + 18 + 28)

enum iwch_layers_types {
	LAYER_RDMAP		= 0x00,
	LAYER_DDP		= 0x10,
	LAYER_MPA		= 0x20,
	RDMAP_LOCAL_CATA	= 0x00,
	RDMAP_REMOTE_PROT	= 0x01,
	RDMAP_REMOTE_OP		= 0x02,
	DDP_LOCAL_CATA		= 0x00,
	DDP_TAGGED_ERR		= 0x01,
	DDP_UNTAGGED_ERR	= 0x02,
	DDP_LLP			= 0x03
};

enum iwch_rdma_ecodes {
	RDMAP_INV_STAG		= 0x00,
	RDMAP_BASE_BOUNDS	= 0x01,
	RDMAP_ACC_VIOL		= 0x02,
	RDMAP_STAG_NOT_ASSOC	= 0x03,
	RDMAP_TO_WRAP		= 0x04,
	RDMAP_INV_VERS		= 0x05,
	RDMAP_INV_OPCODE	= 0x06,
	RDMAP_STREAM_CATA	= 0x07,
	RDMAP_GLOBAL_CATA	= 0x08,
	RDMAP_CANT_INV_STAG	= 0x09,
	RDMAP_UNSPECIFIED	= 0xff
};

enum iwch_ddp_ecodes {
	DDPT_INV_STAG		= 0x00,
	DDPT_BASE_BOUNDS	= 0x01,
	DDPT_STAG_NOT_ASSOC	= 0x02,
	DDPT_TO_WRAP		= 0x03,
	DDPT_INV_VERS		= 0x04,
	DDPU_INV_QN		= 0x01,
	DDPU_INV_MSN_NOBUF	= 0x02,
	DDPU_INV_MSN_RANGE	= 0x03,
	DDPU_INV_MO		= 0x04,
	DDPU_MSG_TOOBIG		= 0x05,
	DDPU_INV_VERS		= 0x06
};

enum iwch_mpa_ecodes {
	MPA_CRC_ERR		= 0x02,
	MPA_MARKER_ERR		= 0x03
};

enum iwch_ep_state {
	IDLE = 0,
	LISTEN,
	CONNECTING,
	MPA_REQ_WAIT,
	MPA_REQ_SENT,
	MPA_REQ_RCVD,
	MPA_REP_SENT,
	FPDU_MODE,
	ABORTING,
	CLOSING,
	MORIBUND,
	DEAD,
};

enum iwch_ep_flags {
	PEER_ABORT_IN_PROGRESS	= 0,
	ABORT_REQ_IN_PROGRESS	= 1,
	RELEASE_RESOURCES	= 2,
	CLOSE_SENT		= 3,
};

struct iwch_ep_common {
	struct iw_cm_id *cm_id;
	struct iwch_qp *qp;
	struct t3cdev *tdev;
	enum iwch_ep_state state;
	struct kref kref;
	spinlock_t lock;
	struct sockaddr_in local_addr;
	struct sockaddr_in remote_addr;
	wait_queue_head_t waitq;
	int rpl_done;
	int rpl_err;
	unsigned long flags;
};

struct iwch_listen_ep {
	struct iwch_ep_common com;
	unsigned int stid;
	int backlog;
};

struct iwch_ep {
	struct iwch_ep_common com;
	struct iwch_ep *parent_ep;
	struct timer_list timer;
	unsigned int atid;
	u32 hwtid;
	u32 snd_seq;
	u32 rcv_seq;
	struct l2t_entry *l2t;
	struct dst_entry *dst;
	struct sk_buff *mpa_skb;
	struct iwch_mpa_attributes mpa_attr;
	unsigned int mpa_pkt_len;
	u8 mpa_pkt[sizeof(struct mpa_message) + MPA_MAX_PRIVATE_DATA];
	u8 tos;
	u16 emss;
	u16 plen;
	u32 ird;
	u32 ord;
};

static inline struct iwch_ep *to_ep(struct iw_cm_id *cm_id)
{
	return cm_id->provider_data;
}

static inline struct iwch_listen_ep *to_listen_ep(struct iw_cm_id *cm_id)
{
	return cm_id->provider_data;
}

static inline int compute_wscale(int win)
{
	int wscale = 0;

	while (wscale < 14 && (65535<<wscale) < win)
		wscale++;
	return wscale;
}

/* CM prototypes */

int iwch_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param);
int iwch_create_listen(struct iw_cm_id *cm_id, int backlog);
int iwch_destroy_listen(struct iw_cm_id *cm_id);
int iwch_reject_cr(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len);
int iwch_accept_cr(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param);
int iwch_ep_disconnect(struct iwch_ep *ep, int abrupt, gfp_t gfp);
int iwch_quiesce_tid(struct iwch_ep *ep);
int iwch_resume_tid(struct iwch_ep *ep);
void __free_ep(struct kref *kref);
void iwch_rearp(struct iwch_ep *ep);
int iwch_ep_redirect(void *ctx, struct dst_entry *old, struct dst_entry *new, struct l2t_entry *l2t);

int __init iwch_cm_init(void);
void __exit iwch_cm_term(void);
extern int peer2peer;

#endif				/* _IWCH_CM_H_ */
