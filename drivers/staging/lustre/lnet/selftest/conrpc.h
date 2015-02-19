/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * /lnet/selftest/conrpc.h
 *
 * Console rpc
 *
 * Author: Liang Zhen <liang@whamcloud.com>
 */

#ifndef __LST_CONRPC_H__
#define __LST_CONRPC_H__

#include "../../include/linux/libcfs/libcfs.h"
#include "../../include/linux/lnet/lnet.h"
#include "../../include/linux/lnet/lib-types.h"
#include "../../include/linux/lnet/lnetst.h"
#include "rpc.h"
#include "selftest.h"

/* Console rpc and rpc transaction */
#define LST_TRANS_TIMEOUT       30
#define LST_TRANS_MIN_TIMEOUT   3

#define LST_VALIDATE_TIMEOUT(t) min(max(t, LST_TRANS_MIN_TIMEOUT), LST_TRANS_TIMEOUT)

#define LST_PING_INTERVAL       8

struct lstcon_rpc_trans;
struct lstcon_tsb_hdr;
struct lstcon_test;
struct lstcon_node;

typedef struct lstcon_rpc {
	struct list_head	       crp_link;       /* chain on rpc transaction */
	srpc_client_rpc_t       *crp_rpc;	/* client rpc */
	struct lstcon_node      *crp_node;       /* destination node */
	struct lstcon_rpc_trans *crp_trans;     /* conrpc transaction */

	unsigned int		 crp_posted:1;   /* rpc is posted */
	unsigned int		 crp_finished:1; /* rpc is finished */
	unsigned int		 crp_unpacked:1; /* reply is unpacked */
	/** RPC is embedded in other structure and can't free it */
	unsigned int		 crp_embedded:1;
	int		      crp_status;     /* console rpc errors */
	unsigned long	       crp_stamp;      /* replied time stamp */
} lstcon_rpc_t;

typedef struct lstcon_rpc_trans {
	struct list_head	    tas_olink;     /* link chain on owner list */
	struct list_head	    tas_link;      /* link chain on global list */
	int		   tas_opc;       /* operation code of transaction */
	/* features mask is uptodate */
	unsigned	      tas_feats_updated;
	/* test features mask */
	unsigned	      tas_features;
	wait_queue_head_t	   tas_waitq;     /* wait queue head */
	atomic_t	  tas_remaining; /* # of un-scheduled rpcs */
	struct list_head	    tas_rpcs_list; /* queued requests */
} lstcon_rpc_trans_t;

#define LST_TRANS_PRIVATE       0x1000

#define LST_TRANS_SESNEW	(LST_TRANS_PRIVATE | 0x01)
#define LST_TRANS_SESEND	(LST_TRANS_PRIVATE | 0x02)
#define LST_TRANS_SESQRY	0x03
#define LST_TRANS_SESPING       0x04

#define LST_TRANS_TSBCLIADD     (LST_TRANS_PRIVATE | 0x11)
#define LST_TRANS_TSBSRVADD     (LST_TRANS_PRIVATE | 0x12)
#define LST_TRANS_TSBRUN	(LST_TRANS_PRIVATE | 0x13)
#define LST_TRANS_TSBSTOP       (LST_TRANS_PRIVATE | 0x14)
#define LST_TRANS_TSBCLIQRY     0x15
#define LST_TRANS_TSBSRVQRY     0x16

#define LST_TRANS_STATQRY       0x21

typedef int (* lstcon_rpc_cond_func_t)(int, struct lstcon_node *, void *);
typedef int (* lstcon_rpc_readent_func_t)(int, srpc_msg_t *, lstcon_rpc_ent_t *);

int  lstcon_sesrpc_prep(struct lstcon_node *nd, int transop,
			unsigned version, lstcon_rpc_t **crpc);
int  lstcon_dbgrpc_prep(struct lstcon_node *nd,
			unsigned version, lstcon_rpc_t **crpc);
int  lstcon_batrpc_prep(struct lstcon_node *nd, int transop, unsigned version,
			struct lstcon_tsb_hdr *tsb, lstcon_rpc_t **crpc);
int  lstcon_testrpc_prep(struct lstcon_node *nd, int transop, unsigned version,
			 struct lstcon_test *test, lstcon_rpc_t **crpc);
int  lstcon_statrpc_prep(struct lstcon_node *nd, unsigned version,
			 lstcon_rpc_t **crpc);
void lstcon_rpc_put(lstcon_rpc_t *crpc);
int  lstcon_rpc_trans_prep(struct list_head *translist,
			   int transop, lstcon_rpc_trans_t **transpp);
int  lstcon_rpc_trans_ndlist(struct list_head *ndlist,
			     struct list_head *translist, int transop,
			     void *arg, lstcon_rpc_cond_func_t condition,
			     lstcon_rpc_trans_t **transpp);
void lstcon_rpc_trans_stat(lstcon_rpc_trans_t *trans,
			   lstcon_trans_stat_t *stat);
int  lstcon_rpc_trans_interpreter(lstcon_rpc_trans_t *trans,
				  struct list_head *head_up,
				  lstcon_rpc_readent_func_t readent);
void lstcon_rpc_trans_abort(lstcon_rpc_trans_t *trans, int error);
void lstcon_rpc_trans_destroy(lstcon_rpc_trans_t *trans);
void lstcon_rpc_trans_addreq(lstcon_rpc_trans_t *trans, lstcon_rpc_t *req);
int  lstcon_rpc_trans_postwait(lstcon_rpc_trans_t *trans, int timeout);
int  lstcon_rpc_pinger_start(void);
void lstcon_rpc_pinger_stop(void);
void lstcon_rpc_cleanup_wait(void);
int  lstcon_rpc_module_init(void);
void lstcon_rpc_module_fini(void);


#endif
