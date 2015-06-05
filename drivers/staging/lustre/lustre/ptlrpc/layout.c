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
 * lustre/ptlrpc/layout.c
 *
 * Lustre Metadata Target (mdt) request handler
 *
 * Author: Nikita Danilov <nikita@clusterfs.com>
 */
/*
 * This file contains the "capsule/pill" abstraction layered above PTLRPC.
 *
 * Every struct ptlrpc_request contains a "pill", which points to a description
 * of the format that the request conforms to.
 */

#if !defined(__REQ_LAYOUT_USER__)

#define DEBUG_SUBSYSTEM S_RPC

#include <linux/module.h>

/* LUSTRE_VERSION_CODE */
#include "../include/lustre_ver.h"

#include "../include/obd_support.h"
/* lustre_swab_mdt_body */
#include "../include/lustre/lustre_idl.h"
/* obd2cli_tgt() (required by DEBUG_REQ()) */
#include "../include/obd.h"

/* __REQ_LAYOUT_USER__ */
#endif
/* struct ptlrpc_request, lustre_msg* */
#include "../include/lustre_req_layout.h"
#include "../include/lustre_acl.h"
#include "../include/lustre_debug.h"

/*
 * RQFs (see below) refer to two struct req_msg_field arrays describing the
 * client request and server reply, respectively.
 */
/* empty set of fields... for suitable definition of emptiness. */
static const struct req_msg_field *empty[] = {
	&RMF_PTLRPC_BODY
};

static const struct req_msg_field *mgs_target_info_only[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MGS_TARGET_INFO
};

static const struct req_msg_field *mgs_set_info[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MGS_SEND_PARAM
};

static const struct req_msg_field *mgs_config_read_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MGS_CONFIG_BODY
};

static const struct req_msg_field *mgs_config_read_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MGS_CONFIG_RES
};

static const struct req_msg_field *log_cancel_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_LOGCOOKIES
};

static const struct req_msg_field *mdt_body_only[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY
};

static const struct req_msg_field *mdt_body_capa[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_CAPA1
};

static const struct req_msg_field *quotactl_only[] = {
	&RMF_PTLRPC_BODY,
	&RMF_OBD_QUOTACTL
};

static const struct req_msg_field *quota_body_only[] = {
	&RMF_PTLRPC_BODY,
	&RMF_QUOTA_BODY
};

static const struct req_msg_field *ldlm_intent_quota_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REQ,
	&RMF_LDLM_INTENT,
	&RMF_QUOTA_BODY
};

static const struct req_msg_field *ldlm_intent_quota_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REP,
	&RMF_DLM_LVB,
	&RMF_QUOTA_BODY
};

static const struct req_msg_field *mdt_close_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_EPOCH,
	&RMF_REC_REINT,
	&RMF_CAPA1
};

static const struct req_msg_field *mdt_release_close_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_EPOCH,
	&RMF_REC_REINT,
	&RMF_CAPA1,
	&RMF_CLOSE_DATA
};

static const struct req_msg_field *obd_statfs_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_OBD_STATFS
};

static const struct req_msg_field *seq_query_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_SEQ_OPC,
	&RMF_SEQ_RANGE
};

static const struct req_msg_field *seq_query_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_SEQ_RANGE
};

static const struct req_msg_field *fld_query_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_FLD_OPC,
	&RMF_FLD_MDFLD
};

static const struct req_msg_field *fld_query_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_FLD_MDFLD
};

static const struct req_msg_field *mds_getattr_name_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_CAPA1,
	&RMF_NAME
};

static const struct req_msg_field *mds_reint_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_REC_REINT
};

static const struct req_msg_field *mds_reint_create_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_REC_REINT,
	&RMF_CAPA1,
	&RMF_NAME
};

static const struct req_msg_field *mds_reint_create_slave_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_REC_REINT,
	&RMF_CAPA1,
	&RMF_NAME,
	&RMF_EADATA,
	&RMF_DLM_REQ
};

static const struct req_msg_field *mds_reint_create_rmt_acl_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_REC_REINT,
	&RMF_CAPA1,
	&RMF_NAME,
	&RMF_EADATA,
	&RMF_DLM_REQ
};

static const struct req_msg_field *mds_reint_create_sym_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_REC_REINT,
	&RMF_CAPA1,
	&RMF_NAME,
	&RMF_SYMTGT,
	&RMF_DLM_REQ
};

static const struct req_msg_field *mds_reint_open_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_REC_REINT,
	&RMF_CAPA1,
	&RMF_CAPA2,
	&RMF_NAME,
	&RMF_EADATA
};

static const struct req_msg_field *mds_reint_open_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_MDT_MD,
	&RMF_ACL,
	&RMF_CAPA1,
	&RMF_CAPA2
};

static const struct req_msg_field *mds_reint_unlink_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_REC_REINT,
	&RMF_CAPA1,
	&RMF_NAME,
	&RMF_DLM_REQ
};

static const struct req_msg_field *mds_reint_link_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_REC_REINT,
	&RMF_CAPA1,
	&RMF_CAPA2,
	&RMF_NAME,
	&RMF_DLM_REQ
};

static const struct req_msg_field *mds_reint_rename_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_REC_REINT,
	&RMF_CAPA1,
	&RMF_CAPA2,
	&RMF_NAME,
	&RMF_SYMTGT,
	&RMF_DLM_REQ
};

static const struct req_msg_field *mds_last_unlink_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_MDT_MD,
	&RMF_LOGCOOKIES,
	&RMF_CAPA1,
	&RMF_CAPA2
};

static const struct req_msg_field *mds_reint_setattr_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_REC_REINT,
	&RMF_CAPA1,
	&RMF_MDT_EPOCH,
	&RMF_EADATA,
	&RMF_LOGCOOKIES,
	&RMF_DLM_REQ
};

static const struct req_msg_field *mds_reint_setxattr_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_REC_REINT,
	&RMF_CAPA1,
	&RMF_NAME,
	&RMF_EADATA,
	&RMF_DLM_REQ
};

static const struct req_msg_field *mdt_swap_layouts[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_SWAP_LAYOUTS,
	&RMF_CAPA1,
	&RMF_CAPA2,
	&RMF_DLM_REQ
};

static const struct req_msg_field *obd_connect_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_TGTUUID,
	&RMF_CLUUID,
	&RMF_CONN,
	&RMF_CONNECT_DATA
};

static const struct req_msg_field *obd_connect_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_CONNECT_DATA
};

static const struct req_msg_field *obd_set_info_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_SETINFO_KEY,
	&RMF_SETINFO_VAL
};

static const struct req_msg_field *ost_grant_shrink_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_SETINFO_KEY,
	&RMF_OST_BODY
};

static const struct req_msg_field *mds_getinfo_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_GETINFO_KEY,
	&RMF_GETINFO_VALLEN
};

static const struct req_msg_field *mds_getinfo_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_GETINFO_VAL,
};

static const struct req_msg_field *ldlm_enqueue_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REQ
};

static const struct req_msg_field *ldlm_enqueue_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REP
};

static const struct req_msg_field *ldlm_enqueue_lvb_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REP,
	&RMF_DLM_LVB
};

static const struct req_msg_field *ldlm_cp_callback_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REQ,
	&RMF_DLM_LVB
};

static const struct req_msg_field *ldlm_gl_callback_desc_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REQ,
	&RMF_DLM_GL_DESC
};

static const struct req_msg_field *ldlm_gl_callback_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_LVB
};

static const struct req_msg_field *ldlm_intent_basic_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REQ,
	&RMF_LDLM_INTENT,
};

static const struct req_msg_field *ldlm_intent_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REQ,
	&RMF_LDLM_INTENT,
	&RMF_REC_REINT
};

static const struct req_msg_field *ldlm_intent_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REP,
	&RMF_MDT_BODY,
	&RMF_MDT_MD,
	&RMF_ACL
};

static const struct req_msg_field *ldlm_intent_layout_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REQ,
	&RMF_LDLM_INTENT,
	&RMF_LAYOUT_INTENT,
	&RMF_EADATA /* for new layout to be set up */
};
static const struct req_msg_field *ldlm_intent_open_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REP,
	&RMF_MDT_BODY,
	&RMF_MDT_MD,
	&RMF_ACL,
	&RMF_CAPA1,
	&RMF_CAPA2
};

static const struct req_msg_field *ldlm_intent_getattr_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REQ,
	&RMF_LDLM_INTENT,
	&RMF_MDT_BODY,     /* coincides with mds_getattr_name_client[] */
	&RMF_CAPA1,
	&RMF_NAME
};

static const struct req_msg_field *ldlm_intent_getattr_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REP,
	&RMF_MDT_BODY,
	&RMF_MDT_MD,
	&RMF_ACL,
	&RMF_CAPA1
};

static const struct req_msg_field *ldlm_intent_create_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REQ,
	&RMF_LDLM_INTENT,
	&RMF_REC_REINT,    /* coincides with mds_reint_create_client[] */
	&RMF_CAPA1,
	&RMF_NAME,
	&RMF_EADATA
};

static const struct req_msg_field *ldlm_intent_open_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REQ,
	&RMF_LDLM_INTENT,
	&RMF_REC_REINT,    /* coincides with mds_reint_open_client[] */
	&RMF_CAPA1,
	&RMF_CAPA2,
	&RMF_NAME,
	&RMF_EADATA
};

static const struct req_msg_field *ldlm_intent_unlink_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REQ,
	&RMF_LDLM_INTENT,
	&RMF_REC_REINT,    /* coincides with mds_reint_unlink_client[] */
	&RMF_CAPA1,
	&RMF_NAME
};

static const struct req_msg_field *ldlm_intent_getxattr_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REQ,
	&RMF_LDLM_INTENT,
	&RMF_MDT_BODY,
	&RMF_CAPA1,
};

static const struct req_msg_field *ldlm_intent_getxattr_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_DLM_REP,
	&RMF_MDT_BODY,
	&RMF_MDT_MD,
	&RMF_ACL, /* for req_capsule_extend/mdt_intent_policy */
	&RMF_EADATA,
	&RMF_EAVALS,
	&RMF_EAVALS_LENS
};

static const struct req_msg_field *mds_getxattr_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_CAPA1,
	&RMF_NAME,
	&RMF_EADATA
};

static const struct req_msg_field *mds_getxattr_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_EADATA
};

static const struct req_msg_field *mds_getattr_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_MDT_MD,
	&RMF_ACL,
	&RMF_CAPA1,
	&RMF_CAPA2
};

static const struct req_msg_field *mds_setattr_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_MDT_MD,
	&RMF_ACL,
	&RMF_CAPA1,
	&RMF_CAPA2
};

static const struct req_msg_field *mds_update_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_UPDATE,
};

static const struct req_msg_field *mds_update_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_UPDATE_REPLY,
};

static const struct req_msg_field *llog_origin_handle_create_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_LLOGD_BODY,
	&RMF_NAME
};

static const struct req_msg_field *llogd_body_only[] = {
	&RMF_PTLRPC_BODY,
	&RMF_LLOGD_BODY
};

static const struct req_msg_field *llog_log_hdr_only[] = {
	&RMF_PTLRPC_BODY,
	&RMF_LLOG_LOG_HDR
};

static const struct req_msg_field *llogd_conn_body_only[] = {
	&RMF_PTLRPC_BODY,
	&RMF_LLOGD_CONN_BODY
};

static const struct req_msg_field *llog_origin_handle_next_block_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_LLOGD_BODY,
	&RMF_EADATA
};

static const struct req_msg_field *obd_idx_read_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_IDX_INFO
};

static const struct req_msg_field *obd_idx_read_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_IDX_INFO
};

static const struct req_msg_field *ost_body_only[] = {
	&RMF_PTLRPC_BODY,
	&RMF_OST_BODY
};

static const struct req_msg_field *ost_body_capa[] = {
	&RMF_PTLRPC_BODY,
	&RMF_OST_BODY,
	&RMF_CAPA1
};

static const struct req_msg_field *ost_destroy_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_OST_BODY,
	&RMF_DLM_REQ,
	&RMF_CAPA1
};


static const struct req_msg_field *ost_brw_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_OST_BODY,
	&RMF_OBD_IOOBJ,
	&RMF_NIOBUF_REMOTE,
	&RMF_CAPA1
};

static const struct req_msg_field *ost_brw_read_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_OST_BODY
};

static const struct req_msg_field *ost_brw_write_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_OST_BODY,
	&RMF_RCS
};

static const struct req_msg_field *ost_get_info_generic_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_GENERIC_DATA,
};

static const struct req_msg_field *ost_get_info_generic_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_SETINFO_KEY
};

static const struct req_msg_field *ost_get_last_id_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_OBD_ID
};

static const struct req_msg_field *ost_get_last_fid_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_FID,
};

static const struct req_msg_field *ost_get_fiemap_client[] = {
	&RMF_PTLRPC_BODY,
	&RMF_FIEMAP_KEY,
	&RMF_FIEMAP_VAL
};

static const struct req_msg_field *ost_get_fiemap_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_FIEMAP_VAL
};

static const struct req_msg_field *mdt_hsm_progress[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_MDS_HSM_PROGRESS,
};

static const struct req_msg_field *mdt_hsm_ct_register[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_MDS_HSM_ARCHIVE,
};

static const struct req_msg_field *mdt_hsm_ct_unregister[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
};

static const struct req_msg_field *mdt_hsm_action_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_MDS_HSM_CURRENT_ACTION,
};

static const struct req_msg_field *mdt_hsm_state_get_server[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_HSM_USER_STATE,
};

static const struct req_msg_field *mdt_hsm_state_set[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_CAPA1,
	&RMF_HSM_STATE_SET,
};

static const struct req_msg_field *mdt_hsm_request[] = {
	&RMF_PTLRPC_BODY,
	&RMF_MDT_BODY,
	&RMF_MDS_HSM_REQUEST,
	&RMF_MDS_HSM_USER_ITEM,
	&RMF_GENERIC_DATA,
};

static struct req_format *req_formats[] = {
	&RQF_OBD_PING,
	&RQF_OBD_SET_INFO,
	&RQF_OBD_IDX_READ,
	&RQF_SEC_CTX,
	&RQF_MGS_TARGET_REG,
	&RQF_MGS_SET_INFO,
	&RQF_MGS_CONFIG_READ,
	&RQF_SEQ_QUERY,
	&RQF_FLD_QUERY,
	&RQF_MDS_CONNECT,
	&RQF_MDS_DISCONNECT,
	&RQF_MDS_GET_INFO,
	&RQF_MDS_GETSTATUS,
	&RQF_MDS_STATFS,
	&RQF_MDS_GETATTR,
	&RQF_MDS_GETATTR_NAME,
	&RQF_MDS_GETXATTR,
	&RQF_MDS_SYNC,
	&RQF_MDS_CLOSE,
	&RQF_MDS_RELEASE_CLOSE,
	&RQF_MDS_PIN,
	&RQF_MDS_UNPIN,
	&RQF_MDS_READPAGE,
	&RQF_MDS_WRITEPAGE,
	&RQF_MDS_IS_SUBDIR,
	&RQF_MDS_DONE_WRITING,
	&RQF_MDS_REINT,
	&RQF_MDS_REINT_CREATE,
	&RQF_MDS_REINT_CREATE_RMT_ACL,
	&RQF_MDS_REINT_CREATE_SLAVE,
	&RQF_MDS_REINT_CREATE_SYM,
	&RQF_MDS_REINT_OPEN,
	&RQF_MDS_REINT_UNLINK,
	&RQF_MDS_REINT_LINK,
	&RQF_MDS_REINT_RENAME,
	&RQF_MDS_REINT_SETATTR,
	&RQF_MDS_REINT_SETXATTR,
	&RQF_MDS_QUOTACHECK,
	&RQF_MDS_QUOTACTL,
	&RQF_MDS_HSM_PROGRESS,
	&RQF_MDS_HSM_CT_REGISTER,
	&RQF_MDS_HSM_CT_UNREGISTER,
	&RQF_MDS_HSM_STATE_GET,
	&RQF_MDS_HSM_STATE_SET,
	&RQF_MDS_HSM_ACTION,
	&RQF_MDS_HSM_REQUEST,
	&RQF_MDS_SWAP_LAYOUTS,
	&RQF_UPDATE_OBJ,
	&RQF_QC_CALLBACK,
	&RQF_OST_CONNECT,
	&RQF_OST_DISCONNECT,
	&RQF_OST_QUOTACHECK,
	&RQF_OST_QUOTACTL,
	&RQF_OST_GETATTR,
	&RQF_OST_SETATTR,
	&RQF_OST_CREATE,
	&RQF_OST_PUNCH,
	&RQF_OST_SYNC,
	&RQF_OST_DESTROY,
	&RQF_OST_BRW_READ,
	&RQF_OST_BRW_WRITE,
	&RQF_OST_STATFS,
	&RQF_OST_SET_GRANT_INFO,
	&RQF_OST_GET_INFO_GENERIC,
	&RQF_OST_GET_INFO_LAST_ID,
	&RQF_OST_GET_INFO_LAST_FID,
	&RQF_OST_SET_INFO_LAST_FID,
	&RQF_OST_GET_INFO_FIEMAP,
	&RQF_LDLM_ENQUEUE,
	&RQF_LDLM_ENQUEUE_LVB,
	&RQF_LDLM_CONVERT,
	&RQF_LDLM_CANCEL,
	&RQF_LDLM_CALLBACK,
	&RQF_LDLM_CP_CALLBACK,
	&RQF_LDLM_BL_CALLBACK,
	&RQF_LDLM_GL_CALLBACK,
	&RQF_LDLM_GL_DESC_CALLBACK,
	&RQF_LDLM_INTENT,
	&RQF_LDLM_INTENT_BASIC,
	&RQF_LDLM_INTENT_LAYOUT,
	&RQF_LDLM_INTENT_GETATTR,
	&RQF_LDLM_INTENT_OPEN,
	&RQF_LDLM_INTENT_CREATE,
	&RQF_LDLM_INTENT_UNLINK,
	&RQF_LDLM_INTENT_GETXATTR,
	&RQF_LDLM_INTENT_QUOTA,
	&RQF_QUOTA_DQACQ,
	&RQF_LOG_CANCEL,
	&RQF_LLOG_ORIGIN_HANDLE_CREATE,
	&RQF_LLOG_ORIGIN_HANDLE_DESTROY,
	&RQF_LLOG_ORIGIN_HANDLE_NEXT_BLOCK,
	&RQF_LLOG_ORIGIN_HANDLE_PREV_BLOCK,
	&RQF_LLOG_ORIGIN_HANDLE_READ_HEADER,
	&RQF_LLOG_ORIGIN_CONNECT,
	&RQF_CONNECT,
};

struct req_msg_field {
	const __u32 rmf_flags;
	const char  *rmf_name;
	/**
	 * Field length. (-1) means "variable length".  If the
	 * \a RMF_F_STRUCT_ARRAY flag is set the field is also variable-length,
	 * but the actual size must be a whole multiple of \a rmf_size.
	 */
	const int   rmf_size;
	void	(*rmf_swabber)(void *);
	void	(*rmf_dumper)(void *);
	int	 rmf_offset[ARRAY_SIZE(req_formats)][RCL_NR];
};

enum rmf_flags {
	/**
	 * The field is a string, must be NUL-terminated.
	 */
	RMF_F_STRING = 1 << 0,
	/**
	 * The field's buffer size need not match the declared \a rmf_size.
	 */
	RMF_F_NO_SIZE_CHECK = 1 << 1,
	/**
	 * The field's buffer size must be a whole multiple of the declared \a
	 * rmf_size and the \a rmf_swabber function must work on the declared \a
	 * rmf_size worth of bytes.
	 */
	RMF_F_STRUCT_ARRAY = 1 << 2
};

struct req_capsule;

/*
 * Request fields.
 */
#define DEFINE_MSGF(name, flags, size, swabber, dumper) {       \
	.rmf_name    = (name),				  \
	.rmf_flags   = (flags),				 \
	.rmf_size    = (size),				  \
	.rmf_swabber = (void (*)(void *))(swabber),	      \
	.rmf_dumper  = (void (*)(void *))(dumper)		\
}

struct req_msg_field RMF_GENERIC_DATA =
	DEFINE_MSGF("generic_data", 0,
		    -1, NULL, NULL);
EXPORT_SYMBOL(RMF_GENERIC_DATA);

struct req_msg_field RMF_MGS_TARGET_INFO =
	DEFINE_MSGF("mgs_target_info", 0,
		    sizeof(struct mgs_target_info),
		    lustre_swab_mgs_target_info, NULL);
EXPORT_SYMBOL(RMF_MGS_TARGET_INFO);

struct req_msg_field RMF_MGS_SEND_PARAM =
	DEFINE_MSGF("mgs_send_param", 0,
		    sizeof(struct mgs_send_param),
		    NULL, NULL);
EXPORT_SYMBOL(RMF_MGS_SEND_PARAM);

struct req_msg_field RMF_MGS_CONFIG_BODY =
	DEFINE_MSGF("mgs_config_read request", 0,
		    sizeof(struct mgs_config_body),
		    lustre_swab_mgs_config_body, NULL);
EXPORT_SYMBOL(RMF_MGS_CONFIG_BODY);

struct req_msg_field RMF_MGS_CONFIG_RES =
	DEFINE_MSGF("mgs_config_read reply ", 0,
		    sizeof(struct mgs_config_res),
		    lustre_swab_mgs_config_res, NULL);
EXPORT_SYMBOL(RMF_MGS_CONFIG_RES);

struct req_msg_field RMF_U32 =
	DEFINE_MSGF("generic u32", 0,
		    sizeof(__u32), lustre_swab_generic_32s, NULL);
EXPORT_SYMBOL(RMF_U32);

struct req_msg_field RMF_SETINFO_VAL =
	DEFINE_MSGF("setinfo_val", 0, -1, NULL, NULL);
EXPORT_SYMBOL(RMF_SETINFO_VAL);

struct req_msg_field RMF_GETINFO_KEY =
	DEFINE_MSGF("getinfo_key", 0, -1, NULL, NULL);
EXPORT_SYMBOL(RMF_GETINFO_KEY);

struct req_msg_field RMF_GETINFO_VALLEN =
	DEFINE_MSGF("getinfo_vallen", 0,
		    sizeof(__u32), lustre_swab_generic_32s, NULL);
EXPORT_SYMBOL(RMF_GETINFO_VALLEN);

struct req_msg_field RMF_GETINFO_VAL =
	DEFINE_MSGF("getinfo_val", 0, -1, NULL, NULL);
EXPORT_SYMBOL(RMF_GETINFO_VAL);

struct req_msg_field RMF_SEQ_OPC =
	DEFINE_MSGF("seq_query_opc", 0,
		    sizeof(__u32), lustre_swab_generic_32s, NULL);
EXPORT_SYMBOL(RMF_SEQ_OPC);

struct req_msg_field RMF_SEQ_RANGE =
	DEFINE_MSGF("seq_query_range", 0,
		    sizeof(struct lu_seq_range),
		    lustre_swab_lu_seq_range, NULL);
EXPORT_SYMBOL(RMF_SEQ_RANGE);

struct req_msg_field RMF_FLD_OPC =
	DEFINE_MSGF("fld_query_opc", 0,
		    sizeof(__u32), lustre_swab_generic_32s, NULL);
EXPORT_SYMBOL(RMF_FLD_OPC);

struct req_msg_field RMF_FLD_MDFLD =
	DEFINE_MSGF("fld_query_mdfld", 0,
		    sizeof(struct lu_seq_range),
		    lustre_swab_lu_seq_range, NULL);
EXPORT_SYMBOL(RMF_FLD_MDFLD);

struct req_msg_field RMF_MDT_BODY =
	DEFINE_MSGF("mdt_body", 0,
		    sizeof(struct mdt_body), lustre_swab_mdt_body, NULL);
EXPORT_SYMBOL(RMF_MDT_BODY);

struct req_msg_field RMF_OBD_QUOTACTL =
	DEFINE_MSGF("obd_quotactl", 0,
		    sizeof(struct obd_quotactl),
		    lustre_swab_obd_quotactl, NULL);
EXPORT_SYMBOL(RMF_OBD_QUOTACTL);

struct req_msg_field RMF_QUOTA_BODY =
	DEFINE_MSGF("quota_body", 0,
		    sizeof(struct quota_body), lustre_swab_quota_body, NULL);
EXPORT_SYMBOL(RMF_QUOTA_BODY);

struct req_msg_field RMF_MDT_EPOCH =
	DEFINE_MSGF("mdt_ioepoch", 0,
		    sizeof(struct mdt_ioepoch), lustre_swab_mdt_ioepoch, NULL);
EXPORT_SYMBOL(RMF_MDT_EPOCH);

struct req_msg_field RMF_PTLRPC_BODY =
	DEFINE_MSGF("ptlrpc_body", 0,
		    sizeof(struct ptlrpc_body), lustre_swab_ptlrpc_body, NULL);
EXPORT_SYMBOL(RMF_PTLRPC_BODY);

struct req_msg_field RMF_CLOSE_DATA =
	DEFINE_MSGF("data_version", 0,
		    sizeof(struct close_data), lustre_swab_close_data, NULL);
EXPORT_SYMBOL(RMF_CLOSE_DATA);

struct req_msg_field RMF_OBD_STATFS =
	DEFINE_MSGF("obd_statfs", 0,
		    sizeof(struct obd_statfs), lustre_swab_obd_statfs, NULL);
EXPORT_SYMBOL(RMF_OBD_STATFS);

struct req_msg_field RMF_SETINFO_KEY =
	DEFINE_MSGF("setinfo_key", 0, -1, NULL, NULL);
EXPORT_SYMBOL(RMF_SETINFO_KEY);

struct req_msg_field RMF_NAME =
	DEFINE_MSGF("name", RMF_F_STRING, -1, NULL, NULL);
EXPORT_SYMBOL(RMF_NAME);

struct req_msg_field RMF_SYMTGT =
	DEFINE_MSGF("symtgt", RMF_F_STRING, -1, NULL, NULL);
EXPORT_SYMBOL(RMF_SYMTGT);

struct req_msg_field RMF_TGTUUID =
	DEFINE_MSGF("tgtuuid", RMF_F_STRING, sizeof(struct obd_uuid) - 1, NULL,
	NULL);
EXPORT_SYMBOL(RMF_TGTUUID);

struct req_msg_field RMF_CLUUID =
	DEFINE_MSGF("cluuid", RMF_F_STRING, sizeof(struct obd_uuid) - 1, NULL,
	NULL);
EXPORT_SYMBOL(RMF_CLUUID);

struct req_msg_field RMF_STRING =
	DEFINE_MSGF("string", RMF_F_STRING, -1, NULL, NULL);
EXPORT_SYMBOL(RMF_STRING);

struct req_msg_field RMF_LLOGD_BODY =
	DEFINE_MSGF("llogd_body", 0,
		    sizeof(struct llogd_body), lustre_swab_llogd_body, NULL);
EXPORT_SYMBOL(RMF_LLOGD_BODY);

struct req_msg_field RMF_LLOG_LOG_HDR =
	DEFINE_MSGF("llog_log_hdr", 0,
		    sizeof(struct llog_log_hdr), lustre_swab_llog_hdr, NULL);
EXPORT_SYMBOL(RMF_LLOG_LOG_HDR);

struct req_msg_field RMF_LLOGD_CONN_BODY =
	DEFINE_MSGF("llogd_conn_body", 0,
		    sizeof(struct llogd_conn_body),
		    lustre_swab_llogd_conn_body, NULL);
EXPORT_SYMBOL(RMF_LLOGD_CONN_BODY);

/*
 * connection handle received in MDS_CONNECT request.
 *
 * No swabbing needed because struct lustre_handle contains only a 64-bit cookie
 * that the client does not interpret at all.
 */
struct req_msg_field RMF_CONN =
	DEFINE_MSGF("conn", 0, sizeof(struct lustre_handle), NULL, NULL);
EXPORT_SYMBOL(RMF_CONN);

struct req_msg_field RMF_CONNECT_DATA =
	DEFINE_MSGF("cdata",
		    RMF_F_NO_SIZE_CHECK /* we allow extra space for interop */,
		    sizeof(struct obd_connect_data),
		    lustre_swab_connect, NULL);
EXPORT_SYMBOL(RMF_CONNECT_DATA);

struct req_msg_field RMF_DLM_REQ =
	DEFINE_MSGF("dlm_req", RMF_F_NO_SIZE_CHECK /* ldlm_request_bufsize */,
		    sizeof(struct ldlm_request),
		    lustre_swab_ldlm_request, NULL);
EXPORT_SYMBOL(RMF_DLM_REQ);

struct req_msg_field RMF_DLM_REP =
	DEFINE_MSGF("dlm_rep", 0,
		    sizeof(struct ldlm_reply), lustre_swab_ldlm_reply, NULL);
EXPORT_SYMBOL(RMF_DLM_REP);

struct req_msg_field RMF_LDLM_INTENT =
	DEFINE_MSGF("ldlm_intent", 0,
		    sizeof(struct ldlm_intent), lustre_swab_ldlm_intent, NULL);
EXPORT_SYMBOL(RMF_LDLM_INTENT);

struct req_msg_field RMF_DLM_LVB =
	DEFINE_MSGF("dlm_lvb", 0, -1, NULL, NULL);
EXPORT_SYMBOL(RMF_DLM_LVB);

struct req_msg_field RMF_DLM_GL_DESC =
	DEFINE_MSGF("dlm_gl_desc", 0, sizeof(union ldlm_gl_desc),
		    lustre_swab_gl_desc, NULL);
EXPORT_SYMBOL(RMF_DLM_GL_DESC);

struct req_msg_field RMF_MDT_MD =
	DEFINE_MSGF("mdt_md", RMF_F_NO_SIZE_CHECK, MIN_MD_SIZE, NULL, NULL);
EXPORT_SYMBOL(RMF_MDT_MD);

struct req_msg_field RMF_REC_REINT =
	DEFINE_MSGF("rec_reint", 0, sizeof(struct mdt_rec_reint),
		    lustre_swab_mdt_rec_reint, NULL);
EXPORT_SYMBOL(RMF_REC_REINT);

/* FIXME: this length should be defined as a macro */
struct req_msg_field RMF_EADATA = DEFINE_MSGF("eadata", 0, -1,
						    NULL, NULL);
EXPORT_SYMBOL(RMF_EADATA);

struct req_msg_field RMF_EAVALS = DEFINE_MSGF("eavals", 0, -1, NULL, NULL);
EXPORT_SYMBOL(RMF_EAVALS);

struct req_msg_field RMF_ACL =
	DEFINE_MSGF("acl", RMF_F_NO_SIZE_CHECK,
		    LUSTRE_POSIX_ACL_MAX_SIZE, NULL, NULL);
EXPORT_SYMBOL(RMF_ACL);

/* FIXME: this should be made to use RMF_F_STRUCT_ARRAY */
struct req_msg_field RMF_LOGCOOKIES =
	DEFINE_MSGF("logcookies", RMF_F_NO_SIZE_CHECK /* multiple cookies */,
		    sizeof(struct llog_cookie), NULL, NULL);
EXPORT_SYMBOL(RMF_LOGCOOKIES);

struct req_msg_field RMF_CAPA1 =
	DEFINE_MSGF("capa", 0, sizeof(struct lustre_capa),
		    lustre_swab_lustre_capa, NULL);
EXPORT_SYMBOL(RMF_CAPA1);

struct req_msg_field RMF_CAPA2 =
	DEFINE_MSGF("capa", 0, sizeof(struct lustre_capa),
		    lustre_swab_lustre_capa, NULL);
EXPORT_SYMBOL(RMF_CAPA2);

struct req_msg_field RMF_LAYOUT_INTENT =
	DEFINE_MSGF("layout_intent", 0,
		    sizeof(struct layout_intent), lustre_swab_layout_intent,
		    NULL);
EXPORT_SYMBOL(RMF_LAYOUT_INTENT);

/*
 * OST request field.
 */
struct req_msg_field RMF_OST_BODY =
	DEFINE_MSGF("ost_body", 0,
		    sizeof(struct ost_body), lustre_swab_ost_body, dump_ost_body);
EXPORT_SYMBOL(RMF_OST_BODY);

struct req_msg_field RMF_OBD_IOOBJ =
	DEFINE_MSGF("obd_ioobj", RMF_F_STRUCT_ARRAY,
		    sizeof(struct obd_ioobj), lustre_swab_obd_ioobj, dump_ioo);
EXPORT_SYMBOL(RMF_OBD_IOOBJ);

struct req_msg_field RMF_NIOBUF_REMOTE =
	DEFINE_MSGF("niobuf_remote", RMF_F_STRUCT_ARRAY,
		    sizeof(struct niobuf_remote), lustre_swab_niobuf_remote,
		    dump_rniobuf);
EXPORT_SYMBOL(RMF_NIOBUF_REMOTE);

struct req_msg_field RMF_RCS =
	DEFINE_MSGF("niobuf_remote", RMF_F_STRUCT_ARRAY, sizeof(__u32),
		    lustre_swab_generic_32s, dump_rcs);
EXPORT_SYMBOL(RMF_RCS);

struct req_msg_field RMF_EAVALS_LENS =
	DEFINE_MSGF("eavals_lens", RMF_F_STRUCT_ARRAY, sizeof(__u32),
		lustre_swab_generic_32s, NULL);
EXPORT_SYMBOL(RMF_EAVALS_LENS);

struct req_msg_field RMF_OBD_ID =
	DEFINE_MSGF("u64", 0,
		    sizeof(u64), lustre_swab_ost_last_id, NULL);
EXPORT_SYMBOL(RMF_OBD_ID);

struct req_msg_field RMF_FID =
	DEFINE_MSGF("fid", 0,
		    sizeof(struct lu_fid), lustre_swab_lu_fid, NULL);
EXPORT_SYMBOL(RMF_FID);

struct req_msg_field RMF_OST_ID =
	DEFINE_MSGF("ost_id", 0,
		    sizeof(struct ost_id), lustre_swab_ost_id, NULL);
EXPORT_SYMBOL(RMF_OST_ID);

struct req_msg_field RMF_FIEMAP_KEY =
	DEFINE_MSGF("fiemap", 0, sizeof(struct ll_fiemap_info_key),
		    lustre_swab_fiemap, NULL);
EXPORT_SYMBOL(RMF_FIEMAP_KEY);

struct req_msg_field RMF_FIEMAP_VAL =
	DEFINE_MSGF("fiemap", 0, -1, lustre_swab_fiemap, NULL);
EXPORT_SYMBOL(RMF_FIEMAP_VAL);

struct req_msg_field RMF_IDX_INFO =
	DEFINE_MSGF("idx_info", 0, sizeof(struct idx_info),
		    lustre_swab_idx_info, NULL);
EXPORT_SYMBOL(RMF_IDX_INFO);
struct req_msg_field RMF_HSM_USER_STATE =
	DEFINE_MSGF("hsm_user_state", 0, sizeof(struct hsm_user_state),
		    lustre_swab_hsm_user_state, NULL);
EXPORT_SYMBOL(RMF_HSM_USER_STATE);

struct req_msg_field RMF_HSM_STATE_SET =
	DEFINE_MSGF("hsm_state_set", 0, sizeof(struct hsm_state_set),
		    lustre_swab_hsm_state_set, NULL);
EXPORT_SYMBOL(RMF_HSM_STATE_SET);

struct req_msg_field RMF_MDS_HSM_PROGRESS =
	DEFINE_MSGF("hsm_progress", 0, sizeof(struct hsm_progress_kernel),
		    lustre_swab_hsm_progress_kernel, NULL);
EXPORT_SYMBOL(RMF_MDS_HSM_PROGRESS);

struct req_msg_field RMF_MDS_HSM_CURRENT_ACTION =
	DEFINE_MSGF("hsm_current_action", 0, sizeof(struct hsm_current_action),
		    lustre_swab_hsm_current_action, NULL);
EXPORT_SYMBOL(RMF_MDS_HSM_CURRENT_ACTION);

struct req_msg_field RMF_MDS_HSM_USER_ITEM =
	DEFINE_MSGF("hsm_user_item", RMF_F_STRUCT_ARRAY,
		    sizeof(struct hsm_user_item), lustre_swab_hsm_user_item,
		    NULL);
EXPORT_SYMBOL(RMF_MDS_HSM_USER_ITEM);

struct req_msg_field RMF_MDS_HSM_ARCHIVE =
	DEFINE_MSGF("hsm_archive", 0,
		    sizeof(__u32), lustre_swab_generic_32s, NULL);
EXPORT_SYMBOL(RMF_MDS_HSM_ARCHIVE);

struct req_msg_field RMF_MDS_HSM_REQUEST =
	DEFINE_MSGF("hsm_request", 0, sizeof(struct hsm_request),
		    lustre_swab_hsm_request, NULL);
EXPORT_SYMBOL(RMF_MDS_HSM_REQUEST);

struct req_msg_field RMF_UPDATE = DEFINE_MSGF("update", 0, -1,
					      lustre_swab_update_buf, NULL);
EXPORT_SYMBOL(RMF_UPDATE);

struct req_msg_field RMF_UPDATE_REPLY = DEFINE_MSGF("update_reply", 0, -1,
						lustre_swab_update_reply_buf,
						    NULL);
EXPORT_SYMBOL(RMF_UPDATE_REPLY);

struct req_msg_field RMF_SWAP_LAYOUTS =
	DEFINE_MSGF("swap_layouts", 0, sizeof(struct  mdc_swap_layouts),
		    lustre_swab_swap_layouts, NULL);
EXPORT_SYMBOL(RMF_SWAP_LAYOUTS);
/*
 * Request formats.
 */

struct req_format {
	const char *rf_name;
	int	 rf_idx;
	struct {
		int			  nr;
		const struct req_msg_field **d;
	} rf_fields[RCL_NR];
};

#define DEFINE_REQ_FMT(name, client, client_nr, server, server_nr) {    \
	.rf_name   = name,					      \
	.rf_fields = {						  \
		[RCL_CLIENT] = {					\
			.nr = client_nr,				\
			.d  = client				    \
		},						      \
		[RCL_SERVER] = {					\
			.nr = server_nr,				\
			.d  = server				    \
		}						       \
	}							       \
}

#define DEFINE_REQ_FMT0(name, client, server)				  \
DEFINE_REQ_FMT(name, client, ARRAY_SIZE(client), server, ARRAY_SIZE(server))

struct req_format RQF_OBD_PING =
	DEFINE_REQ_FMT0("OBD_PING", empty, empty);
EXPORT_SYMBOL(RQF_OBD_PING);

struct req_format RQF_OBD_SET_INFO =
	DEFINE_REQ_FMT0("OBD_SET_INFO", obd_set_info_client, empty);
EXPORT_SYMBOL(RQF_OBD_SET_INFO);

/* Read index file through the network */
struct req_format RQF_OBD_IDX_READ =
	DEFINE_REQ_FMT0("OBD_IDX_READ",
			obd_idx_read_client, obd_idx_read_server);
EXPORT_SYMBOL(RQF_OBD_IDX_READ);

struct req_format RQF_SEC_CTX =
	DEFINE_REQ_FMT0("SEC_CTX", empty, empty);
EXPORT_SYMBOL(RQF_SEC_CTX);

struct req_format RQF_MGS_TARGET_REG =
	DEFINE_REQ_FMT0("MGS_TARGET_REG", mgs_target_info_only,
			 mgs_target_info_only);
EXPORT_SYMBOL(RQF_MGS_TARGET_REG);

struct req_format RQF_MGS_SET_INFO =
	DEFINE_REQ_FMT0("MGS_SET_INFO", mgs_set_info,
			 mgs_set_info);
EXPORT_SYMBOL(RQF_MGS_SET_INFO);

struct req_format RQF_MGS_CONFIG_READ =
	DEFINE_REQ_FMT0("MGS_CONFIG_READ", mgs_config_read_client,
			 mgs_config_read_server);
EXPORT_SYMBOL(RQF_MGS_CONFIG_READ);

struct req_format RQF_SEQ_QUERY =
	DEFINE_REQ_FMT0("SEQ_QUERY", seq_query_client, seq_query_server);
EXPORT_SYMBOL(RQF_SEQ_QUERY);

struct req_format RQF_FLD_QUERY =
	DEFINE_REQ_FMT0("FLD_QUERY", fld_query_client, fld_query_server);
EXPORT_SYMBOL(RQF_FLD_QUERY);

struct req_format RQF_LOG_CANCEL =
	DEFINE_REQ_FMT0("OBD_LOG_CANCEL", log_cancel_client, empty);
EXPORT_SYMBOL(RQF_LOG_CANCEL);

struct req_format RQF_MDS_QUOTACHECK =
	DEFINE_REQ_FMT0("MDS_QUOTACHECK", quotactl_only, empty);
EXPORT_SYMBOL(RQF_MDS_QUOTACHECK);

struct req_format RQF_OST_QUOTACHECK =
	DEFINE_REQ_FMT0("OST_QUOTACHECK", quotactl_only, empty);
EXPORT_SYMBOL(RQF_OST_QUOTACHECK);

struct req_format RQF_MDS_QUOTACTL =
	DEFINE_REQ_FMT0("MDS_QUOTACTL", quotactl_only, quotactl_only);
EXPORT_SYMBOL(RQF_MDS_QUOTACTL);

struct req_format RQF_OST_QUOTACTL =
	DEFINE_REQ_FMT0("OST_QUOTACTL", quotactl_only, quotactl_only);
EXPORT_SYMBOL(RQF_OST_QUOTACTL);

struct req_format RQF_QC_CALLBACK =
	DEFINE_REQ_FMT0("QC_CALLBACK", quotactl_only, empty);
EXPORT_SYMBOL(RQF_QC_CALLBACK);

struct req_format RQF_QUOTA_DQACQ =
	DEFINE_REQ_FMT0("QUOTA_DQACQ", quota_body_only, quota_body_only);
EXPORT_SYMBOL(RQF_QUOTA_DQACQ);

struct req_format RQF_LDLM_INTENT_QUOTA =
	DEFINE_REQ_FMT0("LDLM_INTENT_QUOTA",
			ldlm_intent_quota_client,
			ldlm_intent_quota_server);
EXPORT_SYMBOL(RQF_LDLM_INTENT_QUOTA);

struct req_format RQF_MDS_GETSTATUS =
	DEFINE_REQ_FMT0("MDS_GETSTATUS", mdt_body_only, mdt_body_capa);
EXPORT_SYMBOL(RQF_MDS_GETSTATUS);

struct req_format RQF_MDS_STATFS =
	DEFINE_REQ_FMT0("MDS_STATFS", empty, obd_statfs_server);
EXPORT_SYMBOL(RQF_MDS_STATFS);

struct req_format RQF_MDS_SYNC =
	DEFINE_REQ_FMT0("MDS_SYNC", mdt_body_capa, mdt_body_only);
EXPORT_SYMBOL(RQF_MDS_SYNC);

struct req_format RQF_MDS_GETATTR =
	DEFINE_REQ_FMT0("MDS_GETATTR", mdt_body_capa, mds_getattr_server);
EXPORT_SYMBOL(RQF_MDS_GETATTR);

struct req_format RQF_MDS_GETXATTR =
	DEFINE_REQ_FMT0("MDS_GETXATTR",
			mds_getxattr_client, mds_getxattr_server);
EXPORT_SYMBOL(RQF_MDS_GETXATTR);

struct req_format RQF_MDS_GETATTR_NAME =
	DEFINE_REQ_FMT0("MDS_GETATTR_NAME",
			mds_getattr_name_client, mds_getattr_server);
EXPORT_SYMBOL(RQF_MDS_GETATTR_NAME);

struct req_format RQF_MDS_REINT =
	DEFINE_REQ_FMT0("MDS_REINT", mds_reint_client, mdt_body_only);
EXPORT_SYMBOL(RQF_MDS_REINT);

struct req_format RQF_MDS_REINT_CREATE =
	DEFINE_REQ_FMT0("MDS_REINT_CREATE",
			mds_reint_create_client, mdt_body_capa);
EXPORT_SYMBOL(RQF_MDS_REINT_CREATE);

struct req_format RQF_MDS_REINT_CREATE_RMT_ACL =
	DEFINE_REQ_FMT0("MDS_REINT_CREATE_RMT_ACL",
			mds_reint_create_rmt_acl_client, mdt_body_capa);
EXPORT_SYMBOL(RQF_MDS_REINT_CREATE_RMT_ACL);

struct req_format RQF_MDS_REINT_CREATE_SLAVE =
	DEFINE_REQ_FMT0("MDS_REINT_CREATE_EA",
			mds_reint_create_slave_client, mdt_body_capa);
EXPORT_SYMBOL(RQF_MDS_REINT_CREATE_SLAVE);

struct req_format RQF_MDS_REINT_CREATE_SYM =
	DEFINE_REQ_FMT0("MDS_REINT_CREATE_SYM",
			mds_reint_create_sym_client, mdt_body_capa);
EXPORT_SYMBOL(RQF_MDS_REINT_CREATE_SYM);

struct req_format RQF_MDS_REINT_OPEN =
	DEFINE_REQ_FMT0("MDS_REINT_OPEN",
			mds_reint_open_client, mds_reint_open_server);
EXPORT_SYMBOL(RQF_MDS_REINT_OPEN);

struct req_format RQF_MDS_REINT_UNLINK =
	DEFINE_REQ_FMT0("MDS_REINT_UNLINK", mds_reint_unlink_client,
			mds_last_unlink_server);
EXPORT_SYMBOL(RQF_MDS_REINT_UNLINK);

struct req_format RQF_MDS_REINT_LINK =
	DEFINE_REQ_FMT0("MDS_REINT_LINK",
			mds_reint_link_client, mdt_body_only);
EXPORT_SYMBOL(RQF_MDS_REINT_LINK);

struct req_format RQF_MDS_REINT_RENAME =
	DEFINE_REQ_FMT0("MDS_REINT_RENAME", mds_reint_rename_client,
			mds_last_unlink_server);
EXPORT_SYMBOL(RQF_MDS_REINT_RENAME);

struct req_format RQF_MDS_REINT_SETATTR =
	DEFINE_REQ_FMT0("MDS_REINT_SETATTR",
			mds_reint_setattr_client, mds_setattr_server);
EXPORT_SYMBOL(RQF_MDS_REINT_SETATTR);

struct req_format RQF_MDS_REINT_SETXATTR =
	DEFINE_REQ_FMT0("MDS_REINT_SETXATTR",
			mds_reint_setxattr_client, mdt_body_only);
EXPORT_SYMBOL(RQF_MDS_REINT_SETXATTR);

struct req_format RQF_MDS_CONNECT =
	DEFINE_REQ_FMT0("MDS_CONNECT",
			obd_connect_client, obd_connect_server);
EXPORT_SYMBOL(RQF_MDS_CONNECT);

struct req_format RQF_MDS_DISCONNECT =
	DEFINE_REQ_FMT0("MDS_DISCONNECT", empty, empty);
EXPORT_SYMBOL(RQF_MDS_DISCONNECT);

struct req_format RQF_MDS_GET_INFO =
	DEFINE_REQ_FMT0("MDS_GET_INFO", mds_getinfo_client,
			mds_getinfo_server);
EXPORT_SYMBOL(RQF_MDS_GET_INFO);

struct req_format RQF_UPDATE_OBJ =
	DEFINE_REQ_FMT0("OBJECT_UPDATE_OBJ", mds_update_client,
			mds_update_server);
EXPORT_SYMBOL(RQF_UPDATE_OBJ);

struct req_format RQF_LDLM_ENQUEUE =
	DEFINE_REQ_FMT0("LDLM_ENQUEUE",
			ldlm_enqueue_client, ldlm_enqueue_lvb_server);
EXPORT_SYMBOL(RQF_LDLM_ENQUEUE);

struct req_format RQF_LDLM_ENQUEUE_LVB =
	DEFINE_REQ_FMT0("LDLM_ENQUEUE_LVB",
			ldlm_enqueue_client, ldlm_enqueue_lvb_server);
EXPORT_SYMBOL(RQF_LDLM_ENQUEUE_LVB);

struct req_format RQF_LDLM_CONVERT =
	DEFINE_REQ_FMT0("LDLM_CONVERT",
			ldlm_enqueue_client, ldlm_enqueue_server);
EXPORT_SYMBOL(RQF_LDLM_CONVERT);

struct req_format RQF_LDLM_CANCEL =
	DEFINE_REQ_FMT0("LDLM_CANCEL", ldlm_enqueue_client, empty);
EXPORT_SYMBOL(RQF_LDLM_CANCEL);

struct req_format RQF_LDLM_CALLBACK =
	DEFINE_REQ_FMT0("LDLM_CALLBACK", ldlm_enqueue_client, empty);
EXPORT_SYMBOL(RQF_LDLM_CALLBACK);

struct req_format RQF_LDLM_CP_CALLBACK =
	DEFINE_REQ_FMT0("LDLM_CP_CALLBACK", ldlm_cp_callback_client, empty);
EXPORT_SYMBOL(RQF_LDLM_CP_CALLBACK);

struct req_format RQF_LDLM_BL_CALLBACK =
	DEFINE_REQ_FMT0("LDLM_BL_CALLBACK", ldlm_enqueue_client, empty);
EXPORT_SYMBOL(RQF_LDLM_BL_CALLBACK);

struct req_format RQF_LDLM_GL_CALLBACK =
	DEFINE_REQ_FMT0("LDLM_GL_CALLBACK", ldlm_enqueue_client,
			ldlm_gl_callback_server);
EXPORT_SYMBOL(RQF_LDLM_GL_CALLBACK);

struct req_format RQF_LDLM_GL_DESC_CALLBACK =
	DEFINE_REQ_FMT0("LDLM_GL_CALLBACK", ldlm_gl_callback_desc_client,
			ldlm_gl_callback_server);
EXPORT_SYMBOL(RQF_LDLM_GL_DESC_CALLBACK);

struct req_format RQF_LDLM_INTENT_BASIC =
	DEFINE_REQ_FMT0("LDLM_INTENT_BASIC",
			ldlm_intent_basic_client, ldlm_enqueue_lvb_server);
EXPORT_SYMBOL(RQF_LDLM_INTENT_BASIC);

struct req_format RQF_LDLM_INTENT =
	DEFINE_REQ_FMT0("LDLM_INTENT",
			ldlm_intent_client, ldlm_intent_server);
EXPORT_SYMBOL(RQF_LDLM_INTENT);

struct req_format RQF_LDLM_INTENT_LAYOUT =
	DEFINE_REQ_FMT0("LDLM_INTENT_LAYOUT ",
			ldlm_intent_layout_client, ldlm_enqueue_lvb_server);
EXPORT_SYMBOL(RQF_LDLM_INTENT_LAYOUT);

struct req_format RQF_LDLM_INTENT_GETATTR =
	DEFINE_REQ_FMT0("LDLM_INTENT_GETATTR",
			ldlm_intent_getattr_client, ldlm_intent_getattr_server);
EXPORT_SYMBOL(RQF_LDLM_INTENT_GETATTR);

struct req_format RQF_LDLM_INTENT_OPEN =
	DEFINE_REQ_FMT0("LDLM_INTENT_OPEN",
			ldlm_intent_open_client, ldlm_intent_open_server);
EXPORT_SYMBOL(RQF_LDLM_INTENT_OPEN);

struct req_format RQF_LDLM_INTENT_CREATE =
	DEFINE_REQ_FMT0("LDLM_INTENT_CREATE",
			ldlm_intent_create_client, ldlm_intent_getattr_server);
EXPORT_SYMBOL(RQF_LDLM_INTENT_CREATE);

struct req_format RQF_LDLM_INTENT_UNLINK =
	DEFINE_REQ_FMT0("LDLM_INTENT_UNLINK",
			ldlm_intent_unlink_client, ldlm_intent_server);
EXPORT_SYMBOL(RQF_LDLM_INTENT_UNLINK);

struct req_format RQF_LDLM_INTENT_GETXATTR =
	DEFINE_REQ_FMT0("LDLM_INTENT_GETXATTR",
			ldlm_intent_getxattr_client,
			ldlm_intent_getxattr_server);
EXPORT_SYMBOL(RQF_LDLM_INTENT_GETXATTR);

struct req_format RQF_MDS_CLOSE =
	DEFINE_REQ_FMT0("MDS_CLOSE",
			mdt_close_client, mds_last_unlink_server);
EXPORT_SYMBOL(RQF_MDS_CLOSE);

struct req_format RQF_MDS_RELEASE_CLOSE =
	DEFINE_REQ_FMT0("MDS_CLOSE",
			mdt_release_close_client, mds_last_unlink_server);
EXPORT_SYMBOL(RQF_MDS_RELEASE_CLOSE);

struct req_format RQF_MDS_PIN =
	DEFINE_REQ_FMT0("MDS_PIN",
			mdt_body_capa, mdt_body_only);
EXPORT_SYMBOL(RQF_MDS_PIN);

struct req_format RQF_MDS_UNPIN =
	DEFINE_REQ_FMT0("MDS_UNPIN", mdt_body_only, empty);
EXPORT_SYMBOL(RQF_MDS_UNPIN);

struct req_format RQF_MDS_DONE_WRITING =
	DEFINE_REQ_FMT0("MDS_DONE_WRITING",
			mdt_close_client, mdt_body_only);
EXPORT_SYMBOL(RQF_MDS_DONE_WRITING);

struct req_format RQF_MDS_READPAGE =
	DEFINE_REQ_FMT0("MDS_READPAGE",
			mdt_body_capa, mdt_body_only);
EXPORT_SYMBOL(RQF_MDS_READPAGE);

struct req_format RQF_MDS_HSM_ACTION =
	DEFINE_REQ_FMT0("MDS_HSM_ACTION", mdt_body_capa, mdt_hsm_action_server);
EXPORT_SYMBOL(RQF_MDS_HSM_ACTION);

struct req_format RQF_MDS_HSM_PROGRESS =
	DEFINE_REQ_FMT0("MDS_HSM_PROGRESS", mdt_hsm_progress, empty);
EXPORT_SYMBOL(RQF_MDS_HSM_PROGRESS);

struct req_format RQF_MDS_HSM_CT_REGISTER =
	DEFINE_REQ_FMT0("MDS_HSM_CT_REGISTER", mdt_hsm_ct_register, empty);
EXPORT_SYMBOL(RQF_MDS_HSM_CT_REGISTER);

struct req_format RQF_MDS_HSM_CT_UNREGISTER =
	DEFINE_REQ_FMT0("MDS_HSM_CT_UNREGISTER", mdt_hsm_ct_unregister, empty);
EXPORT_SYMBOL(RQF_MDS_HSM_CT_UNREGISTER);

struct req_format RQF_MDS_HSM_STATE_GET =
	DEFINE_REQ_FMT0("MDS_HSM_STATE_GET",
			mdt_body_capa, mdt_hsm_state_get_server);
EXPORT_SYMBOL(RQF_MDS_HSM_STATE_GET);

struct req_format RQF_MDS_HSM_STATE_SET =
	DEFINE_REQ_FMT0("MDS_HSM_STATE_SET", mdt_hsm_state_set, empty);
EXPORT_SYMBOL(RQF_MDS_HSM_STATE_SET);

struct req_format RQF_MDS_HSM_REQUEST =
	DEFINE_REQ_FMT0("MDS_HSM_REQUEST", mdt_hsm_request, empty);
EXPORT_SYMBOL(RQF_MDS_HSM_REQUEST);

struct req_format RQF_MDS_SWAP_LAYOUTS =
	DEFINE_REQ_FMT0("MDS_SWAP_LAYOUTS",
			mdt_swap_layouts, empty);
EXPORT_SYMBOL(RQF_MDS_SWAP_LAYOUTS);

/* This is for split */
struct req_format RQF_MDS_WRITEPAGE =
	DEFINE_REQ_FMT0("MDS_WRITEPAGE",
			mdt_body_capa, mdt_body_only);
EXPORT_SYMBOL(RQF_MDS_WRITEPAGE);

struct req_format RQF_MDS_IS_SUBDIR =
	DEFINE_REQ_FMT0("MDS_IS_SUBDIR",
			mdt_body_only, mdt_body_only);
EXPORT_SYMBOL(RQF_MDS_IS_SUBDIR);

struct req_format RQF_LLOG_ORIGIN_HANDLE_CREATE =
	DEFINE_REQ_FMT0("LLOG_ORIGIN_HANDLE_CREATE",
			llog_origin_handle_create_client, llogd_body_only);
EXPORT_SYMBOL(RQF_LLOG_ORIGIN_HANDLE_CREATE);

struct req_format RQF_LLOG_ORIGIN_HANDLE_DESTROY =
	DEFINE_REQ_FMT0("LLOG_ORIGIN_HANDLE_DESTROY",
			llogd_body_only, llogd_body_only);
EXPORT_SYMBOL(RQF_LLOG_ORIGIN_HANDLE_DESTROY);

struct req_format RQF_LLOG_ORIGIN_HANDLE_NEXT_BLOCK =
	DEFINE_REQ_FMT0("LLOG_ORIGIN_HANDLE_NEXT_BLOCK",
			llogd_body_only, llog_origin_handle_next_block_server);
EXPORT_SYMBOL(RQF_LLOG_ORIGIN_HANDLE_NEXT_BLOCK);

struct req_format RQF_LLOG_ORIGIN_HANDLE_PREV_BLOCK =
	DEFINE_REQ_FMT0("LLOG_ORIGIN_HANDLE_PREV_BLOCK",
			llogd_body_only, llog_origin_handle_next_block_server);
EXPORT_SYMBOL(RQF_LLOG_ORIGIN_HANDLE_PREV_BLOCK);

struct req_format RQF_LLOG_ORIGIN_HANDLE_READ_HEADER =
	DEFINE_REQ_FMT0("LLOG_ORIGIN_HANDLE_READ_HEADER",
			llogd_body_only, llog_log_hdr_only);
EXPORT_SYMBOL(RQF_LLOG_ORIGIN_HANDLE_READ_HEADER);

struct req_format RQF_LLOG_ORIGIN_CONNECT =
	DEFINE_REQ_FMT0("LLOG_ORIGIN_CONNECT", llogd_conn_body_only, empty);
EXPORT_SYMBOL(RQF_LLOG_ORIGIN_CONNECT);

struct req_format RQF_CONNECT =
	DEFINE_REQ_FMT0("CONNECT", obd_connect_client, obd_connect_server);
EXPORT_SYMBOL(RQF_CONNECT);

struct req_format RQF_OST_CONNECT =
	DEFINE_REQ_FMT0("OST_CONNECT",
			obd_connect_client, obd_connect_server);
EXPORT_SYMBOL(RQF_OST_CONNECT);

struct req_format RQF_OST_DISCONNECT =
	DEFINE_REQ_FMT0("OST_DISCONNECT", empty, empty);
EXPORT_SYMBOL(RQF_OST_DISCONNECT);

struct req_format RQF_OST_GETATTR =
	DEFINE_REQ_FMT0("OST_GETATTR", ost_body_capa, ost_body_only);
EXPORT_SYMBOL(RQF_OST_GETATTR);

struct req_format RQF_OST_SETATTR =
	DEFINE_REQ_FMT0("OST_SETATTR", ost_body_capa, ost_body_only);
EXPORT_SYMBOL(RQF_OST_SETATTR);

struct req_format RQF_OST_CREATE =
	DEFINE_REQ_FMT0("OST_CREATE", ost_body_only, ost_body_only);
EXPORT_SYMBOL(RQF_OST_CREATE);

struct req_format RQF_OST_PUNCH =
	DEFINE_REQ_FMT0("OST_PUNCH", ost_body_capa, ost_body_only);
EXPORT_SYMBOL(RQF_OST_PUNCH);

struct req_format RQF_OST_SYNC =
	DEFINE_REQ_FMT0("OST_SYNC", ost_body_capa, ost_body_only);
EXPORT_SYMBOL(RQF_OST_SYNC);

struct req_format RQF_OST_DESTROY =
	DEFINE_REQ_FMT0("OST_DESTROY", ost_destroy_client, ost_body_only);
EXPORT_SYMBOL(RQF_OST_DESTROY);

struct req_format RQF_OST_BRW_READ =
	DEFINE_REQ_FMT0("OST_BRW_READ", ost_brw_client, ost_brw_read_server);
EXPORT_SYMBOL(RQF_OST_BRW_READ);

struct req_format RQF_OST_BRW_WRITE =
	DEFINE_REQ_FMT0("OST_BRW_WRITE", ost_brw_client, ost_brw_write_server);
EXPORT_SYMBOL(RQF_OST_BRW_WRITE);

struct req_format RQF_OST_STATFS =
	DEFINE_REQ_FMT0("OST_STATFS", empty, obd_statfs_server);
EXPORT_SYMBOL(RQF_OST_STATFS);

struct req_format RQF_OST_SET_GRANT_INFO =
	DEFINE_REQ_FMT0("OST_SET_GRANT_INFO", ost_grant_shrink_client,
			 ost_body_only);
EXPORT_SYMBOL(RQF_OST_SET_GRANT_INFO);

struct req_format RQF_OST_GET_INFO_GENERIC =
	DEFINE_REQ_FMT0("OST_GET_INFO", ost_get_info_generic_client,
					ost_get_info_generic_server);
EXPORT_SYMBOL(RQF_OST_GET_INFO_GENERIC);

struct req_format RQF_OST_GET_INFO_LAST_ID =
	DEFINE_REQ_FMT0("OST_GET_INFO_LAST_ID", ost_get_info_generic_client,
						ost_get_last_id_server);
EXPORT_SYMBOL(RQF_OST_GET_INFO_LAST_ID);

struct req_format RQF_OST_GET_INFO_LAST_FID =
	DEFINE_REQ_FMT0("OST_GET_INFO_LAST_FID", obd_set_info_client,
						 ost_get_last_fid_server);
EXPORT_SYMBOL(RQF_OST_GET_INFO_LAST_FID);

struct req_format RQF_OST_SET_INFO_LAST_FID =
	DEFINE_REQ_FMT0("OST_SET_INFO_LAST_FID", obd_set_info_client,
						 empty);
EXPORT_SYMBOL(RQF_OST_SET_INFO_LAST_FID);

struct req_format RQF_OST_GET_INFO_FIEMAP =
	DEFINE_REQ_FMT0("OST_GET_INFO_FIEMAP", ost_get_fiemap_client,
					       ost_get_fiemap_server);
EXPORT_SYMBOL(RQF_OST_GET_INFO_FIEMAP);

#if !defined(__REQ_LAYOUT_USER__)

/* Convenience macro */
#define FMT_FIELD(fmt, i, j) (fmt)->rf_fields[(i)].d[(j)]

/**
 * Initializes the capsule abstraction by computing and setting the \a rf_idx
 * field of RQFs and the \a rmf_offset field of RMFs.
 */
int req_layout_init(void)
{
	int i;
	int j;
	int k;
	struct req_format *rf = NULL;

	for (i = 0; i < ARRAY_SIZE(req_formats); ++i) {
		rf = req_formats[i];
		rf->rf_idx = i;
		for (j = 0; j < RCL_NR; ++j) {
			LASSERT(rf->rf_fields[j].nr <= REQ_MAX_FIELD_NR);
			for (k = 0; k < rf->rf_fields[j].nr; ++k) {
				struct req_msg_field *field;

				field = (typeof(field))rf->rf_fields[j].d[k];
				LASSERT(!(field->rmf_flags & RMF_F_STRUCT_ARRAY)
					|| field->rmf_size > 0);
				LASSERT(field->rmf_offset[i][j] == 0);
				/*
				 * k + 1 to detect unused format/field
				 * combinations.
				 */
				field->rmf_offset[i][j] = k + 1;
			}
		}
	}
	return 0;
}
EXPORT_SYMBOL(req_layout_init);

void req_layout_fini(void)
{
}
EXPORT_SYMBOL(req_layout_fini);

/**
 * Initializes the expected sizes of each RMF in a \a pill (\a rc_area) to -1.
 *
 * Actual/expected field sizes are set elsewhere in functions in this file:
 * req_capsule_init(), req_capsule_server_pack(), req_capsule_set_size() and
 * req_capsule_msg_size().  The \a rc_area information is used by.
 * ptlrpc_request_set_replen().
 */
void req_capsule_init_area(struct req_capsule *pill)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pill->rc_area[RCL_CLIENT]); i++) {
		pill->rc_area[RCL_CLIENT][i] = -1;
		pill->rc_area[RCL_SERVER][i] = -1;
	}
}
EXPORT_SYMBOL(req_capsule_init_area);

/**
 * Initialize a pill.
 *
 * The \a location indicates whether the caller is executing on the client side
 * (RCL_CLIENT) or server side (RCL_SERVER)..
 */
void req_capsule_init(struct req_capsule *pill,
		      struct ptlrpc_request *req,
		      enum req_location location)
{
	LASSERT(location == RCL_SERVER || location == RCL_CLIENT);

	/*
	 * Today all capsules are embedded in ptlrpc_request structs,
	 * but just in case that ever isn't the case, we don't reach
	 * into req unless req != NULL and pill is the one embedded in
	 * the req.
	 *
	 * The req->rq_pill_init flag makes it safe to initialize a pill
	 * twice, which might happen in the OST paths as a result of the
	 * high-priority RPC queue getting peeked at before ost_handle()
	 * handles an OST RPC.
	 */
	if (req != NULL && pill == &req->rq_pill && req->rq_pill_init)
		return;

	memset(pill, 0, sizeof(*pill));
	pill->rc_req = req;
	pill->rc_loc = location;
	req_capsule_init_area(pill);

	if (req != NULL && pill == &req->rq_pill)
		req->rq_pill_init = 1;
}
EXPORT_SYMBOL(req_capsule_init);

void req_capsule_fini(struct req_capsule *pill)
{
}
EXPORT_SYMBOL(req_capsule_fini);

static int __req_format_is_sane(const struct req_format *fmt)
{
	return
		0 <= fmt->rf_idx && fmt->rf_idx < ARRAY_SIZE(req_formats) &&
		req_formats[fmt->rf_idx] == fmt;
}

static struct lustre_msg *__req_msg(const struct req_capsule *pill,
				    enum req_location loc)
{
	struct ptlrpc_request *req;

	req = pill->rc_req;
	return loc == RCL_CLIENT ? req->rq_reqmsg : req->rq_repmsg;
}

/**
 * Set the format (\a fmt) of a \a pill; format changes are not allowed here
 * (see req_capsule_extend()).
 */
void req_capsule_set(struct req_capsule *pill, const struct req_format *fmt)
{
	LASSERT(pill->rc_fmt == NULL || pill->rc_fmt == fmt);
	LASSERT(__req_format_is_sane(fmt));

	pill->rc_fmt = fmt;
}
EXPORT_SYMBOL(req_capsule_set);

/**
 * Fills in any parts of the \a rc_area of a \a pill that haven't been filled in
 * yet.

 * \a rc_area is an array of REQ_MAX_FIELD_NR elements, used to store sizes of
 * variable-sized fields.  The field sizes come from the declared \a rmf_size
 * field of a \a pill's \a rc_fmt's RMF's.
 */
int req_capsule_filled_sizes(struct req_capsule *pill,
			   enum req_location loc)
{
	const struct req_format *fmt = pill->rc_fmt;
	int		      i;

	LASSERT(fmt != NULL);

	for (i = 0; i < fmt->rf_fields[loc].nr; ++i) {
		if (pill->rc_area[loc][i] == -1) {
			pill->rc_area[loc][i] =
					    fmt->rf_fields[loc].d[i]->rmf_size;
			if (pill->rc_area[loc][i] == -1) {
				/*
				 * Skip the following fields.
				 *
				 * If this LASSERT() trips then you're missing a
				 * call to req_capsule_set_size().
				 */
				LASSERT(loc != RCL_SERVER);
				break;
			}
		}
	}
	return i;
}
EXPORT_SYMBOL(req_capsule_filled_sizes);

/**
 * Capsule equivalent of lustre_pack_request() and lustre_pack_reply().
 *
 * This function uses the \a pill's \a rc_area as filled in by
 * req_capsule_set_size() or req_capsule_filled_sizes() (the latter is called by
 * this function).
 */
int req_capsule_server_pack(struct req_capsule *pill)
{
	const struct req_format *fmt;
	int		      count;
	int		      rc;

	LASSERT(pill->rc_loc == RCL_SERVER);
	fmt = pill->rc_fmt;
	LASSERT(fmt != NULL);

	count = req_capsule_filled_sizes(pill, RCL_SERVER);
	rc = lustre_pack_reply(pill->rc_req, count,
			       pill->rc_area[RCL_SERVER], NULL);
	if (rc != 0) {
		DEBUG_REQ(D_ERROR, pill->rc_req,
		       "Cannot pack %d fields in format `%s': ",
		       count, fmt->rf_name);
	}
	return rc;
}
EXPORT_SYMBOL(req_capsule_server_pack);

/**
 * Returns the PTLRPC request or reply (\a loc) buffer offset of a \a pill
 * corresponding to the given RMF (\a field).
 */
static int __req_capsule_offset(const struct req_capsule *pill,
				const struct req_msg_field *field,
				enum req_location loc)
{
	int offset;

	offset = field->rmf_offset[pill->rc_fmt->rf_idx][loc];
	LASSERTF(offset > 0, "%s:%s, off=%d, loc=%d\n",
			    pill->rc_fmt->rf_name,
			    field->rmf_name, offset, loc);
	offset--;

	LASSERT(0 <= offset && offset < REQ_MAX_FIELD_NR);
	return offset;
}

/**
 * Helper for __req_capsule_get(); swabs value / array of values and/or dumps
 * them if desired.
 */
static
void
swabber_dumper_helper(struct req_capsule *pill,
		      const struct req_msg_field *field,
		      enum req_location loc,
		      int offset,
		      void *value, int len, int dump, void (*swabber)(void *))
{
	void    *p;
	int     i;
	int     n;
	int     do_swab;
	int     inout = loc == RCL_CLIENT;

	swabber = swabber ?: field->rmf_swabber;

	if (ptlrpc_buf_need_swab(pill->rc_req, inout, offset) &&
	    swabber != NULL && value != NULL)
		do_swab = 1;
	else
		do_swab = 0;

	if (!field->rmf_dumper)
		dump = 0;

	if (!(field->rmf_flags & RMF_F_STRUCT_ARRAY)) {
		if (dump) {
			CDEBUG(D_RPCTRACE, "Dump of %sfield %s follows\n",
			       do_swab ? "unswabbed " : "", field->rmf_name);
			field->rmf_dumper(value);
		}
		if (!do_swab)
			return;
		swabber(value);
		ptlrpc_buf_set_swabbed(pill->rc_req, inout, offset);
		if (dump) {
			CDEBUG(D_RPCTRACE, "Dump of swabbed field %s follows\n",
			       field->rmf_name);
			field->rmf_dumper(value);
		}

		return;
	}

	/*
	 * We're swabbing an array; swabber() swabs a single array element, so
	 * swab every element.
	 */
	LASSERT((len % field->rmf_size) == 0);
	for (p = value, i = 0, n = len / field->rmf_size;
	     i < n;
	     i++, p += field->rmf_size) {
		if (dump) {
			CDEBUG(D_RPCTRACE, "Dump of %sarray field %s, element %d follows\n",
			       do_swab ? "unswabbed " : "", field->rmf_name, i);
			field->rmf_dumper(p);
		}
		if (!do_swab)
			continue;
		swabber(p);
		if (dump) {
			CDEBUG(D_RPCTRACE, "Dump of swabbed array field %s, element %d follows\n",
			       field->rmf_name, i);
			field->rmf_dumper(value);
		}
	}
	if (do_swab)
		ptlrpc_buf_set_swabbed(pill->rc_req, inout, offset);
}

/**
 * Returns the pointer to a PTLRPC request or reply (\a loc) buffer of a \a pill
 * corresponding to the given RMF (\a field).
 *
 * The buffer will be swabbed using the given \a swabber.  If \a swabber == NULL
 * then the \a rmf_swabber from the RMF will be used.  Soon there will be no
 * calls to __req_capsule_get() with a non-NULL \a swabber; \a swabber will then
 * be removed.  Fields with the \a RMF_F_STRUCT_ARRAY flag set will have each
 * element of the array swabbed.
 */
static void *__req_capsule_get(struct req_capsule *pill,
			       const struct req_msg_field *field,
			       enum req_location loc,
			       void (*swabber)(void *),
			       int dump)
{
	const struct req_format *fmt;
	struct lustre_msg       *msg;
	void		    *value;
	int		      len;
	int		      offset;

	void *(*getter)(struct lustre_msg *m, int n, int minlen);

	static const char *rcl_names[RCL_NR] = {
		[RCL_CLIENT] = "client",
		[RCL_SERVER] = "server"
	};

	LASSERT(pill != NULL);
	LASSERT(pill != LP_POISON);
	fmt = pill->rc_fmt;
	LASSERT(fmt != NULL);
	LASSERT(fmt != LP_POISON);
	LASSERT(__req_format_is_sane(fmt));

	offset = __req_capsule_offset(pill, field, loc);

	msg = __req_msg(pill, loc);
	LASSERT(msg != NULL);

	getter = (field->rmf_flags & RMF_F_STRING) ?
		(typeof(getter))lustre_msg_string : lustre_msg_buf;

	if (field->rmf_flags & RMF_F_STRUCT_ARRAY) {
		/*
		 * We've already asserted that field->rmf_size > 0 in
		 * req_layout_init().
		 */
		len = lustre_msg_buflen(msg, offset);
		if ((len % field->rmf_size) != 0) {
			CERROR("%s: array field size mismatch %d modulo %d != 0 (%d)\n",
			       field->rmf_name, len, field->rmf_size, loc);
			return NULL;
		}
	} else if (pill->rc_area[loc][offset] != -1) {
		len = pill->rc_area[loc][offset];
	} else {
		len = max(field->rmf_size, 0);
	}
	value = getter(msg, offset, len);

	if (value == NULL) {
		DEBUG_REQ(D_ERROR, pill->rc_req,
			  "Wrong buffer for field `%s' (%d of %d) in format `%s': %d vs. %d (%s)\n",
			  field->rmf_name, offset, lustre_msg_bufcount(msg),
			  fmt->rf_name, lustre_msg_buflen(msg, offset), len,
			  rcl_names[loc]);
	} else {
		swabber_dumper_helper(pill, field, loc, offset, value, len,
				      dump, swabber);
	}

	return value;
}

/**
 * Dump a request and/or reply
 */
static void __req_capsule_dump(struct req_capsule *pill, enum req_location loc)
{
	const struct    req_format *fmt;
	const struct    req_msg_field *field;
	int	     len;
	int	     i;

	fmt = pill->rc_fmt;

	DEBUG_REQ(D_RPCTRACE, pill->rc_req, "BEGIN REQ CAPSULE DUMP\n");
	for (i = 0; i < fmt->rf_fields[loc].nr; ++i) {
		field = FMT_FIELD(fmt, loc, i);
		if (field->rmf_dumper == NULL) {
			/*
			 * FIXME Add a default hex dumper for fields that don't
			 * have a specific dumper
			 */
			len = req_capsule_get_size(pill, field, loc);
			CDEBUG(D_RPCTRACE, "Field %s has no dumper function; field size is %d\n",
			       field->rmf_name, len);
		} else {
			/* It's the dumping side-effect that we're interested in */
			(void) __req_capsule_get(pill, field, loc, NULL, 1);
		}
	}
	CDEBUG(D_RPCTRACE, "END REQ CAPSULE DUMP\n");
}

/**
 * Dump a request.
 */
void req_capsule_client_dump(struct req_capsule *pill)
{
	__req_capsule_dump(pill, RCL_CLIENT);
}
EXPORT_SYMBOL(req_capsule_client_dump);

/**
 * Dump a reply
 */
void req_capsule_server_dump(struct req_capsule *pill)
{
	__req_capsule_dump(pill, RCL_SERVER);
}
EXPORT_SYMBOL(req_capsule_server_dump);

/**
 * Trivial wrapper around __req_capsule_get(), that returns the PTLRPC request
 * buffer corresponding to the given RMF (\a field) of a \a pill.
 */
void *req_capsule_client_get(struct req_capsule *pill,
			     const struct req_msg_field *field)
{
	return __req_capsule_get(pill, field, RCL_CLIENT, NULL, 0);
}
EXPORT_SYMBOL(req_capsule_client_get);

/**
 * Same as req_capsule_client_get(), but with a \a swabber argument.
 *
 * Currently unused; will be removed when req_capsule_server_swab_get() is
 * unused too.
 */
void *req_capsule_client_swab_get(struct req_capsule *pill,
				  const struct req_msg_field *field,
				  void *swabber)
{
	return __req_capsule_get(pill, field, RCL_CLIENT, swabber, 0);
}
EXPORT_SYMBOL(req_capsule_client_swab_get);

/**
 * Utility that combines req_capsule_set_size() and req_capsule_client_get().
 *
 * First the \a pill's request \a field's size is set (\a rc_area) using
 * req_capsule_set_size() with the given \a len.  Then the actual buffer is
 * returned.
 */
void *req_capsule_client_sized_get(struct req_capsule *pill,
				   const struct req_msg_field *field,
				   int len)
{
	req_capsule_set_size(pill, field, RCL_CLIENT, len);
	return __req_capsule_get(pill, field, RCL_CLIENT, NULL, 0);
}
EXPORT_SYMBOL(req_capsule_client_sized_get);

/**
 * Trivial wrapper around __req_capsule_get(), that returns the PTLRPC reply
 * buffer corresponding to the given RMF (\a field) of a \a pill.
 */
void *req_capsule_server_get(struct req_capsule *pill,
			     const struct req_msg_field *field)
{
	return __req_capsule_get(pill, field, RCL_SERVER, NULL, 0);
}
EXPORT_SYMBOL(req_capsule_server_get);

/**
 * Same as req_capsule_server_get(), but with a \a swabber argument.
 *
 * Ideally all swabbing should be done pursuant to RMF definitions, with no
 * swabbing done outside this capsule abstraction.
 */
void *req_capsule_server_swab_get(struct req_capsule *pill,
				  const struct req_msg_field *field,
				  void *swabber)
{
	return __req_capsule_get(pill, field, RCL_SERVER, swabber, 0);
}
EXPORT_SYMBOL(req_capsule_server_swab_get);

/**
 * Utility that combines req_capsule_set_size() and req_capsule_server_get().
 *
 * First the \a pill's request \a field's size is set (\a rc_area) using
 * req_capsule_set_size() with the given \a len.  Then the actual buffer is
 * returned.
 */
void *req_capsule_server_sized_get(struct req_capsule *pill,
				   const struct req_msg_field *field,
				   int len)
{
	req_capsule_set_size(pill, field, RCL_SERVER, len);
	return __req_capsule_get(pill, field, RCL_SERVER, NULL, 0);
}
EXPORT_SYMBOL(req_capsule_server_sized_get);

void *req_capsule_server_sized_swab_get(struct req_capsule *pill,
					const struct req_msg_field *field,
					int len, void *swabber)
{
	req_capsule_set_size(pill, field, RCL_SERVER, len);
	return __req_capsule_get(pill, field, RCL_SERVER, swabber, 0);
}
EXPORT_SYMBOL(req_capsule_server_sized_swab_get);

/**
 * Returns the buffer of a \a pill corresponding to the given \a field from the
 * request (if the caller is executing on the server-side) or reply (if the
 * caller is executing on the client-side).
 *
 * This function convenient for use is code that could be executed on the
 * client and server alike.
 */
const void *req_capsule_other_get(struct req_capsule *pill,
				  const struct req_msg_field *field)
{
	return __req_capsule_get(pill, field, pill->rc_loc ^ 1, NULL, 0);
}
EXPORT_SYMBOL(req_capsule_other_get);

/**
 * Set the size of the PTLRPC request/reply (\a loc) buffer for the given \a
 * field of the given \a pill.
 *
 * This function must be used when constructing variable sized fields of a
 * request or reply.
 */
void req_capsule_set_size(struct req_capsule *pill,
			  const struct req_msg_field *field,
			  enum req_location loc, int size)
{
	LASSERT(loc == RCL_SERVER || loc == RCL_CLIENT);

	if ((size != field->rmf_size) &&
	    (field->rmf_size != -1) &&
	    !(field->rmf_flags & RMF_F_NO_SIZE_CHECK) &&
	    (size > 0)) {
		if ((field->rmf_flags & RMF_F_STRUCT_ARRAY) &&
		    (size % field->rmf_size != 0)) {
			CERROR("%s: array field size mismatch %d %% %d != 0 (%d)\n",
			       field->rmf_name, size, field->rmf_size, loc);
			LBUG();
		} else if (!(field->rmf_flags & RMF_F_STRUCT_ARRAY) &&
		    size < field->rmf_size) {
			CERROR("%s: field size mismatch %d != %d (%d)\n",
			       field->rmf_name, size, field->rmf_size, loc);
			LBUG();
		}
	}

	pill->rc_area[loc][__req_capsule_offset(pill, field, loc)] = size;
}
EXPORT_SYMBOL(req_capsule_set_size);

/**
 * Return the actual PTLRPC buffer length of a request or reply (\a loc)
 * for the given \a pill's given \a field.
 *
 * NB: this function doesn't correspond with req_capsule_set_size(), which
 * actually sets the size in pill.rc_area[loc][offset], but this function
 * returns the message buflen[offset], maybe we should use another name.
 */
int req_capsule_get_size(const struct req_capsule *pill,
			 const struct req_msg_field *field,
			 enum req_location loc)
{
	LASSERT(loc == RCL_SERVER || loc == RCL_CLIENT);

	return lustre_msg_buflen(__req_msg(pill, loc),
				 __req_capsule_offset(pill, field, loc));
}
EXPORT_SYMBOL(req_capsule_get_size);

/**
 * Wrapper around lustre_msg_size() that returns the PTLRPC size needed for the
 * given \a pill's request or reply (\a loc) given the field size recorded in
 * the \a pill's rc_area.
 *
 * See also req_capsule_set_size().
 */
int req_capsule_msg_size(struct req_capsule *pill, enum req_location loc)
{
	return lustre_msg_size(pill->rc_req->rq_import->imp_msg_magic,
			       pill->rc_fmt->rf_fields[loc].nr,
			       pill->rc_area[loc]);
}

/**
 * While req_capsule_msg_size() computes the size of a PTLRPC request or reply
 * (\a loc) given a \a pill's \a rc_area, this function computes the size of a
 * PTLRPC request or reply given only an RQF (\a fmt).
 *
 * This function should not be used for formats which contain variable size
 * fields.
 */
int req_capsule_fmt_size(__u32 magic, const struct req_format *fmt,
			 enum req_location loc)
{
	int size, i = 0;

	/*
	 * This function should probably LASSERT() that fmt has no fields with
	 * RMF_F_STRUCT_ARRAY in rmf_flags, since we can't know here how many
	 * elements in the array there will ultimately be, but then, we could
	 * assume that there will be at least one element, and that's just what
	 * we do.
	 */
	size = lustre_msg_hdr_size(magic, fmt->rf_fields[loc].nr);
	if (size < 0)
		return size;

	for (; i < fmt->rf_fields[loc].nr; ++i)
		if (fmt->rf_fields[loc].d[i]->rmf_size != -1)
			size += cfs_size_round(fmt->rf_fields[loc].d[i]->
					       rmf_size);
	return size;
}

/**
 * Changes the format of an RPC.
 *
 * The pill must already have been initialized, which means that it already has
 * a request format.  The new format \a fmt must be an extension of the pill's
 * old format.  Specifically: the new format must have as many request and reply
 * fields as the old one, and all fields shared by the old and new format must
 * be at least as large in the new format.
 *
 * The new format's fields may be of different "type" than the old format, but
 * only for fields that are "opaque" blobs: fields which have a) have no
 * \a rmf_swabber, b) \a rmf_flags == 0 or RMF_F_NO_SIZE_CHECK, and c) \a
 * rmf_size == -1 or \a rmf_flags == RMF_F_NO_SIZE_CHECK.  For example,
 * OBD_SET_INFO has a key field and an opaque value field that gets interpreted
 * according to the key field.  When the value, according to the key, contains a
 * structure (or array thereof) to be swabbed, the format should be changed to
 * one where the value field has \a rmf_size/rmf_flags/rmf_swabber set
 * accordingly.
 */
void req_capsule_extend(struct req_capsule *pill, const struct req_format *fmt)
{
	int i;
	int j;

	const struct req_format *old;

	LASSERT(pill->rc_fmt != NULL);
	LASSERT(__req_format_is_sane(fmt));

	old = pill->rc_fmt;
	/*
	 * Sanity checking...
	 */
	for (i = 0; i < RCL_NR; ++i) {
		LASSERT(fmt->rf_fields[i].nr >= old->rf_fields[i].nr);
		for (j = 0; j < old->rf_fields[i].nr - 1; ++j) {
			const struct req_msg_field *ofield = FMT_FIELD(old, i, j);

			/* "opaque" fields can be transmogrified */
			if (ofield->rmf_swabber == NULL &&
			    (ofield->rmf_flags & ~RMF_F_NO_SIZE_CHECK) == 0 &&
			    (ofield->rmf_size == -1 ||
			    ofield->rmf_flags == RMF_F_NO_SIZE_CHECK))
				continue;
			LASSERT(FMT_FIELD(fmt, i, j) == FMT_FIELD(old, i, j));
		}
		/*
		 * Last field in old format can be shorter than in new.
		 */
		LASSERT(FMT_FIELD(fmt, i, j)->rmf_size >=
			FMT_FIELD(old, i, j)->rmf_size);
	}

	pill->rc_fmt = fmt;
}
EXPORT_SYMBOL(req_capsule_extend);

/**
 * This function returns a non-zero value if the given \a field is present in
 * the format (\a rc_fmt) of \a pill's PTLRPC request or reply (\a loc), else it
 * returns 0.
 */
int req_capsule_has_field(const struct req_capsule *pill,
			  const struct req_msg_field *field,
			  enum req_location loc)
{
	LASSERT(loc == RCL_SERVER || loc == RCL_CLIENT);

	return field->rmf_offset[pill->rc_fmt->rf_idx][loc];
}
EXPORT_SYMBOL(req_capsule_has_field);

/**
 * Returns a non-zero value if the given \a field is present in the given \a
 * pill's PTLRPC request or reply (\a loc), else it returns 0.
 */
int req_capsule_field_present(const struct req_capsule *pill,
			      const struct req_msg_field *field,
			      enum req_location loc)
{
	int offset;

	LASSERT(loc == RCL_SERVER || loc == RCL_CLIENT);
	LASSERT(req_capsule_has_field(pill, field, loc));

	offset = __req_capsule_offset(pill, field, loc);
	return lustre_msg_bufcount(__req_msg(pill, loc)) > offset;
}
EXPORT_SYMBOL(req_capsule_field_present);

/**
 * This function shrinks the size of the _buffer_ of the \a pill's PTLRPC
 * request or reply (\a loc).
 *
 * This is not the opposite of req_capsule_extend().
 */
void req_capsule_shrink(struct req_capsule *pill,
			const struct req_msg_field *field,
			unsigned int newlen,
			enum req_location loc)
{
	const struct req_format *fmt;
	struct lustre_msg       *msg;
	int		      len;
	int		      offset;

	fmt = pill->rc_fmt;
	LASSERT(fmt != NULL);
	LASSERT(__req_format_is_sane(fmt));
	LASSERT(req_capsule_has_field(pill, field, loc));
	LASSERT(req_capsule_field_present(pill, field, loc));

	offset = __req_capsule_offset(pill, field, loc);

	msg = __req_msg(pill, loc);
	len = lustre_msg_buflen(msg, offset);
	LASSERTF(newlen <= len, "%s:%s, oldlen=%d, newlen=%d\n",
				fmt->rf_name, field->rmf_name, len, newlen);

	if (loc == RCL_CLIENT)
		pill->rc_req->rq_reqlen = lustre_shrink_msg(msg, offset, newlen,
							    1);
	else
		pill->rc_req->rq_replen = lustre_shrink_msg(msg, offset, newlen,
							    1);
}
EXPORT_SYMBOL(req_capsule_shrink);

int req_capsule_server_grow(struct req_capsule *pill,
			    const struct req_msg_field *field,
			    unsigned int newlen)
{
	struct ptlrpc_reply_state *rs = pill->rc_req->rq_reply_state, *nrs;
	char *from, *to;
	int offset, len, rc;

	LASSERT(pill->rc_fmt != NULL);
	LASSERT(__req_format_is_sane(pill->rc_fmt));
	LASSERT(req_capsule_has_field(pill, field, RCL_SERVER));
	LASSERT(req_capsule_field_present(pill, field, RCL_SERVER));

	len = req_capsule_get_size(pill, field, RCL_SERVER);
	offset = __req_capsule_offset(pill, field, RCL_SERVER);
	if (pill->rc_req->rq_repbuf_len >=
	    lustre_packed_msg_size(pill->rc_req->rq_repmsg) - len + newlen)
		CERROR("Inplace repack might be done\n");

	pill->rc_req->rq_reply_state = NULL;
	req_capsule_set_size(pill, field, RCL_SERVER, newlen);
	rc = req_capsule_server_pack(pill);
	if (rc) {
		/* put old rs back, the caller will decide what to do */
		pill->rc_req->rq_reply_state = rs;
		return rc;
	}
	nrs = pill->rc_req->rq_reply_state;
	/* Now we need only buffers, copy first chunk */
	to = lustre_msg_buf(nrs->rs_msg, 0, 0);
	from = lustre_msg_buf(rs->rs_msg, 0, 0);
	len = (char *)lustre_msg_buf(rs->rs_msg, offset, 0) - from;
	memcpy(to, from, len);
	/* check if we have tail and copy it too */
	if (rs->rs_msg->lm_bufcount > offset + 1) {
		to = lustre_msg_buf(nrs->rs_msg, offset + 1, 0);
		from = lustre_msg_buf(rs->rs_msg, offset + 1, 0);
		offset = rs->rs_msg->lm_bufcount - 1;
		len = (char *)lustre_msg_buf(rs->rs_msg, offset, 0) +
		      cfs_size_round(rs->rs_msg->lm_buflens[offset]) - from;
		memcpy(to, from, len);
	}
	/* drop old reply if everything is fine */
	if (rs->rs_difficult) {
		/* copy rs data */
		int i;

		nrs->rs_difficult = 1;
		nrs->rs_no_ack = rs->rs_no_ack;
		for (i = 0; i < rs->rs_nlocks; i++) {
			nrs->rs_locks[i] = rs->rs_locks[i];
			nrs->rs_modes[i] = rs->rs_modes[i];
			nrs->rs_nlocks++;
		}
		rs->rs_nlocks = 0;
		rs->rs_difficult = 0;
		rs->rs_no_ack = 0;
	}
	ptlrpc_rs_decref(rs);
	return 0;
}
EXPORT_SYMBOL(req_capsule_server_grow);
/* __REQ_LAYOUT_USER__ */
#endif
