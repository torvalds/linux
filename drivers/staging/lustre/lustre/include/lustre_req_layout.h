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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/lustre_req_layout.h
 *
 * Lustre Metadata Target (mdt) request handler
 *
 * Author: Nikita Danilov <nikita@clusterfs.com>
 */

#ifndef _LUSTRE_REQ_LAYOUT_H__
#define _LUSTRE_REQ_LAYOUT_H__

/** \defgroup req_layout req_layout
 *
 * @{
 */

struct req_msg_field;
struct req_format;
struct req_capsule;

struct ptlrpc_request;

enum req_location {
	RCL_CLIENT,
	RCL_SERVER,
	RCL_NR
};

/* Maximal number of fields (buffers) in a request message. */
#define REQ_MAX_FIELD_NR  9

struct req_capsule {
	struct ptlrpc_request   *rc_req;
	const struct req_format *rc_fmt;
	enum req_location	rc_loc;
	__u32		    rc_area[RCL_NR][REQ_MAX_FIELD_NR];
};

#if !defined(__REQ_LAYOUT_USER__)

/* struct ptlrpc_request, lustre_msg* */
#include "lustre_net.h"

void req_capsule_init(struct req_capsule *pill, struct ptlrpc_request *req,
		      enum req_location location);
void req_capsule_fini(struct req_capsule *pill);

void req_capsule_set(struct req_capsule *pill, const struct req_format *fmt);
int req_capsule_filled_sizes(struct req_capsule *pill, enum req_location loc);
int  req_capsule_server_pack(struct req_capsule *pill);

void *req_capsule_client_get(struct req_capsule *pill,
			     const struct req_msg_field *field);
void *req_capsule_client_swab_get(struct req_capsule *pill,
				  const struct req_msg_field *field,
				  void *swabber);
void *req_capsule_client_sized_get(struct req_capsule *pill,
				   const struct req_msg_field *field,
				   int len);
void *req_capsule_server_get(struct req_capsule *pill,
			     const struct req_msg_field *field);
void *req_capsule_server_sized_get(struct req_capsule *pill,
				   const struct req_msg_field *field,
				   int len);
void *req_capsule_server_swab_get(struct req_capsule *pill,
				  const struct req_msg_field *field,
				  void *swabber);
void *req_capsule_server_sized_swab_get(struct req_capsule *pill,
					const struct req_msg_field *field,
					int len, void *swabber);

void req_capsule_set_size(struct req_capsule *pill,
			  const struct req_msg_field *field,
			  enum req_location loc, int size);
int req_capsule_get_size(const struct req_capsule *pill,
			  const struct req_msg_field *field,
			  enum req_location loc);
int req_capsule_msg_size(struct req_capsule *pill, enum req_location loc);
int req_capsule_fmt_size(__u32 magic, const struct req_format *fmt,
			 enum req_location loc);
void req_capsule_extend(struct req_capsule *pill, const struct req_format *fmt);

int req_capsule_has_field(const struct req_capsule *pill,
			  const struct req_msg_field *field,
			  enum req_location loc);
void req_capsule_shrink(struct req_capsule *pill,
			const struct req_msg_field *field,
			unsigned int newlen,
			enum req_location loc);
int  req_layout_init(void);
void req_layout_fini(void);

/* __REQ_LAYOUT_USER__ */
#endif

extern struct req_format RQF_OBD_PING;
extern struct req_format RQF_OBD_SET_INFO;
extern struct req_format RQF_SEC_CTX;
extern struct req_format RQF_OBD_IDX_READ;
/* MGS req_format */
extern struct req_format RQF_MGS_TARGET_REG;
extern struct req_format RQF_MGS_SET_INFO;
extern struct req_format RQF_MGS_CONFIG_READ;
/* fid/fld req_format */
extern struct req_format RQF_SEQ_QUERY;
extern struct req_format RQF_FLD_QUERY;
/* MDS req_format */
extern struct req_format RQF_MDS_CONNECT;
extern struct req_format RQF_MDS_DISCONNECT;
extern struct req_format RQF_MDS_STATFS;
extern struct req_format RQF_MDS_GETSTATUS;
extern struct req_format RQF_MDS_SYNC;
extern struct req_format RQF_MDS_GETXATTR;
extern struct req_format RQF_MDS_GETATTR;
extern struct req_format RQF_UPDATE_OBJ;

/*
 * This is format of direct (non-intent) MDS_GETATTR_NAME request.
 */
extern struct req_format RQF_MDS_GETATTR_NAME;
extern struct req_format RQF_MDS_CLOSE;
extern struct req_format RQF_MDS_RELEASE_CLOSE;
extern struct req_format RQF_MDS_PIN;
extern struct req_format RQF_MDS_UNPIN;
extern struct req_format RQF_MDS_CONNECT;
extern struct req_format RQF_MDS_DISCONNECT;
extern struct req_format RQF_MDS_GET_INFO;
extern struct req_format RQF_MDS_READPAGE;
extern struct req_format RQF_MDS_WRITEPAGE;
extern struct req_format RQF_MDS_IS_SUBDIR;
extern struct req_format RQF_MDS_DONE_WRITING;
extern struct req_format RQF_MDS_REINT;
extern struct req_format RQF_MDS_REINT_CREATE;
extern struct req_format RQF_MDS_REINT_CREATE_RMT_ACL;
extern struct req_format RQF_MDS_REINT_CREATE_SLAVE;
extern struct req_format RQF_MDS_REINT_CREATE_SYM;
extern struct req_format RQF_MDS_REINT_OPEN;
extern struct req_format RQF_MDS_REINT_UNLINK;
extern struct req_format RQF_MDS_REINT_LINK;
extern struct req_format RQF_MDS_REINT_RENAME;
extern struct req_format RQF_MDS_REINT_SETATTR;
extern struct req_format RQF_MDS_REINT_SETXATTR;
extern struct req_format RQF_MDS_QUOTACHECK;
extern struct req_format RQF_MDS_QUOTACTL;
extern struct req_format RQF_QC_CALLBACK;
extern struct req_format RQF_QUOTA_DQACQ;
extern struct req_format RQF_MDS_SWAP_LAYOUTS;
/* MDS hsm formats */
extern struct req_format RQF_MDS_HSM_STATE_GET;
extern struct req_format RQF_MDS_HSM_STATE_SET;
extern struct req_format RQF_MDS_HSM_ACTION;
extern struct req_format RQF_MDS_HSM_PROGRESS;
extern struct req_format RQF_MDS_HSM_CT_REGISTER;
extern struct req_format RQF_MDS_HSM_CT_UNREGISTER;
extern struct req_format RQF_MDS_HSM_REQUEST;
/* OST req_format */
extern struct req_format RQF_OST_CONNECT;
extern struct req_format RQF_OST_DISCONNECT;
extern struct req_format RQF_OST_QUOTACHECK;
extern struct req_format RQF_OST_QUOTACTL;
extern struct req_format RQF_OST_GETATTR;
extern struct req_format RQF_OST_SETATTR;
extern struct req_format RQF_OST_CREATE;
extern struct req_format RQF_OST_PUNCH;
extern struct req_format RQF_OST_SYNC;
extern struct req_format RQF_OST_DESTROY;
extern struct req_format RQF_OST_BRW_READ;
extern struct req_format RQF_OST_BRW_WRITE;
extern struct req_format RQF_OST_STATFS;
extern struct req_format RQF_OST_SET_GRANT_INFO;
extern struct req_format RQF_OST_GET_INFO_GENERIC;
extern struct req_format RQF_OST_GET_INFO_LAST_ID;
extern struct req_format RQF_OST_GET_INFO_LAST_FID;
extern struct req_format RQF_OST_SET_INFO_LAST_FID;
extern struct req_format RQF_OST_GET_INFO_FIEMAP;

/* LDLM req_format */
extern struct req_format RQF_LDLM_ENQUEUE;
extern struct req_format RQF_LDLM_ENQUEUE_LVB;
extern struct req_format RQF_LDLM_CONVERT;
extern struct req_format RQF_LDLM_INTENT;
extern struct req_format RQF_LDLM_INTENT_BASIC;
extern struct req_format RQF_LDLM_INTENT_LAYOUT;
extern struct req_format RQF_LDLM_INTENT_GETATTR;
extern struct req_format RQF_LDLM_INTENT_OPEN;
extern struct req_format RQF_LDLM_INTENT_CREATE;
extern struct req_format RQF_LDLM_INTENT_UNLINK;
extern struct req_format RQF_LDLM_INTENT_GETXATTR;
extern struct req_format RQF_LDLM_INTENT_QUOTA;
extern struct req_format RQF_LDLM_CANCEL;
extern struct req_format RQF_LDLM_CALLBACK;
extern struct req_format RQF_LDLM_CP_CALLBACK;
extern struct req_format RQF_LDLM_BL_CALLBACK;
extern struct req_format RQF_LDLM_GL_CALLBACK;
extern struct req_format RQF_LDLM_GL_DESC_CALLBACK;
/* LOG req_format */
extern struct req_format RQF_LOG_CANCEL;
extern struct req_format RQF_LLOG_ORIGIN_HANDLE_CREATE;
extern struct req_format RQF_LLOG_ORIGIN_HANDLE_DESTROY;
extern struct req_format RQF_LLOG_ORIGIN_HANDLE_NEXT_BLOCK;
extern struct req_format RQF_LLOG_ORIGIN_HANDLE_PREV_BLOCK;
extern struct req_format RQF_LLOG_ORIGIN_HANDLE_READ_HEADER;
extern struct req_format RQF_LLOG_ORIGIN_CONNECT;

extern struct req_format RQF_CONNECT;

extern struct req_msg_field RMF_GENERIC_DATA;
extern struct req_msg_field RMF_PTLRPC_BODY;
extern struct req_msg_field RMF_MDT_BODY;
extern struct req_msg_field RMF_MDT_EPOCH;
extern struct req_msg_field RMF_OBD_STATFS;
extern struct req_msg_field RMF_NAME;
extern struct req_msg_field RMF_SYMTGT;
extern struct req_msg_field RMF_TGTUUID;
extern struct req_msg_field RMF_CLUUID;
extern struct req_msg_field RMF_SETINFO_VAL;
extern struct req_msg_field RMF_SETINFO_KEY;
extern struct req_msg_field RMF_GETINFO_VAL;
extern struct req_msg_field RMF_GETINFO_VALLEN;
extern struct req_msg_field RMF_GETINFO_KEY;
extern struct req_msg_field RMF_IDX_INFO;
extern struct req_msg_field RMF_CLOSE_DATA;

/*
 * connection handle received in MDS_CONNECT request.
 */
extern struct req_msg_field RMF_CONN;
extern struct req_msg_field RMF_CONNECT_DATA;
extern struct req_msg_field RMF_DLM_REQ;
extern struct req_msg_field RMF_DLM_REP;
extern struct req_msg_field RMF_DLM_LVB;
extern struct req_msg_field RMF_DLM_GL_DESC;
extern struct req_msg_field RMF_LDLM_INTENT;
extern struct req_msg_field RMF_LAYOUT_INTENT;
extern struct req_msg_field RMF_MDT_MD;
extern struct req_msg_field RMF_REC_REINT;
extern struct req_msg_field RMF_EADATA;
extern struct req_msg_field RMF_EAVALS;
extern struct req_msg_field RMF_EAVALS_LENS;
extern struct req_msg_field RMF_ACL;
extern struct req_msg_field RMF_LOGCOOKIES;
extern struct req_msg_field RMF_CAPA1;
extern struct req_msg_field RMF_CAPA2;
extern struct req_msg_field RMF_OBD_QUOTACHECK;
extern struct req_msg_field RMF_OBD_QUOTACTL;
extern struct req_msg_field RMF_QUOTA_BODY;
extern struct req_msg_field RMF_STRING;
extern struct req_msg_field RMF_SWAP_LAYOUTS;
extern struct req_msg_field RMF_MDS_HSM_PROGRESS;
extern struct req_msg_field RMF_MDS_HSM_REQUEST;
extern struct req_msg_field RMF_MDS_HSM_USER_ITEM;
extern struct req_msg_field RMF_MDS_HSM_ARCHIVE;
extern struct req_msg_field RMF_HSM_USER_STATE;
extern struct req_msg_field RMF_HSM_STATE_SET;
extern struct req_msg_field RMF_MDS_HSM_CURRENT_ACTION;
extern struct req_msg_field RMF_MDS_HSM_REQUEST;

/* seq-mgr fields */
extern struct req_msg_field RMF_SEQ_OPC;
extern struct req_msg_field RMF_SEQ_RANGE;
extern struct req_msg_field RMF_FID_SPACE;

/* FLD fields */
extern struct req_msg_field RMF_FLD_OPC;
extern struct req_msg_field RMF_FLD_MDFLD;

extern struct req_msg_field RMF_LLOGD_BODY;
extern struct req_msg_field RMF_LLOG_LOG_HDR;
extern struct req_msg_field RMF_LLOGD_CONN_BODY;

extern struct req_msg_field RMF_MGS_TARGET_INFO;
extern struct req_msg_field RMF_MGS_SEND_PARAM;

extern struct req_msg_field RMF_OST_BODY;
extern struct req_msg_field RMF_OBD_IOOBJ;
extern struct req_msg_field RMF_OBD_ID;
extern struct req_msg_field RMF_FID;
extern struct req_msg_field RMF_NIOBUF_REMOTE;
extern struct req_msg_field RMF_RCS;
extern struct req_msg_field RMF_FIEMAP_KEY;
extern struct req_msg_field RMF_FIEMAP_VAL;
extern struct req_msg_field RMF_OST_ID;

/* MGS config read message format */
extern struct req_msg_field RMF_MGS_CONFIG_BODY;
extern struct req_msg_field RMF_MGS_CONFIG_RES;

/* generic uint32 */
extern struct req_msg_field RMF_U32;

/* OBJ update format */
extern struct req_msg_field RMF_UPDATE;
extern struct req_msg_field RMF_UPDATE_REPLY;
/** @} req_layout */

#endif /* _LUSTRE_REQ_LAYOUT_H__ */
