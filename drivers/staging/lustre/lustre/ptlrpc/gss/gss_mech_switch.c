/*
 * Modifications for Lustre
 *
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Copyright (c) 2012, Intel Corporation.
 *
 * Author: Eric Mei <ericm@clusterfs.com>
 */

/*
 *  linux/net/sunrpc/gss_mech_switch.c
 *
 *  Copyright (c) 2001 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  J. Bruce Fields   <bfields@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#define DEBUG_SUBSYSTEM S_SEC
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include <obd.h>
#include <obd_class.h>
#include <obd_support.h>
#include <lustre/lustre_idl.h>
#include <lustre_net.h>
#include <lustre_import.h>
#include <lustre_sec.h>

#include "gss_err.h"
#include "gss_internal.h"
#include "gss_api.h"

static LIST_HEAD(registered_mechs);
static DEFINE_SPINLOCK(registered_mechs_lock);

int lgss_mech_register(struct gss_api_mech *gm)
{
	spin_lock(&registered_mechs_lock);
	list_add(&gm->gm_list, &registered_mechs);
	spin_unlock(&registered_mechs_lock);
	CWARN("Register %s mechanism\n", gm->gm_name);
	return 0;
}

void lgss_mech_unregister(struct gss_api_mech *gm)
{
	spin_lock(&registered_mechs_lock);
	list_del(&gm->gm_list);
	spin_unlock(&registered_mechs_lock);
	CWARN("Unregister %s mechanism\n", gm->gm_name);
}


struct gss_api_mech *lgss_mech_get(struct gss_api_mech *gm)
{
	__module_get(gm->gm_owner);
	return gm;
}

struct gss_api_mech *lgss_name_to_mech(char *name)
{
	struct gss_api_mech *pos, *gm = NULL;

	spin_lock(&registered_mechs_lock);
	list_for_each_entry(pos, &registered_mechs, gm_list) {
		if (0 == strcmp(name, pos->gm_name)) {
			if (!try_module_get(pos->gm_owner))
				continue;
			gm = pos;
			break;
		}
	}
	spin_unlock(&registered_mechs_lock);
	return gm;

}

static inline
int mech_supports_subflavor(struct gss_api_mech *gm, __u32 subflavor)
{
	int i;

	for (i = 0; i < gm->gm_sf_num; i++) {
		if (gm->gm_sfs[i].sf_subflavor == subflavor)
			return 1;
	}
	return 0;
}

struct gss_api_mech *lgss_subflavor_to_mech(__u32 subflavor)
{
	struct gss_api_mech *pos, *gm = NULL;

	spin_lock(&registered_mechs_lock);
	list_for_each_entry(pos, &registered_mechs, gm_list) {
		if (!try_module_get(pos->gm_owner))
			continue;
		if (!mech_supports_subflavor(pos, subflavor)) {
			module_put(pos->gm_owner);
			continue;
		}
		gm = pos;
		break;
	}
	spin_unlock(&registered_mechs_lock);
	return gm;
}

void lgss_mech_put(struct gss_api_mech *gm)
{
	module_put(gm->gm_owner);
}

/* The mech could probably be determined from the token instead, but it's just
 * as easy for now to pass it in. */
__u32 lgss_import_sec_context(rawobj_t *input_token,
			      struct gss_api_mech *mech,
			      struct gss_ctx **ctx_id)
{
	OBD_ALLOC_PTR(*ctx_id);
	if (*ctx_id == NULL)
		return GSS_S_FAILURE;

	(*ctx_id)->mech_type = lgss_mech_get(mech);

	LASSERT(mech);
	LASSERT(mech->gm_ops);
	LASSERT(mech->gm_ops->gss_import_sec_context);
	return mech->gm_ops->gss_import_sec_context(input_token, *ctx_id);
}

__u32 lgss_copy_reverse_context(struct gss_ctx *ctx_id,
				struct gss_ctx **ctx_id_new)
{
	struct gss_api_mech *mech = ctx_id->mech_type;
	__u32		major;

	LASSERT(mech);

	OBD_ALLOC_PTR(*ctx_id_new);
	if (*ctx_id_new == NULL)
		return GSS_S_FAILURE;

	(*ctx_id_new)->mech_type = lgss_mech_get(mech);

	LASSERT(mech);
	LASSERT(mech->gm_ops);
	LASSERT(mech->gm_ops->gss_copy_reverse_context);

	major = mech->gm_ops->gss_copy_reverse_context(ctx_id, *ctx_id_new);
	if (major != GSS_S_COMPLETE) {
		lgss_mech_put(mech);
		OBD_FREE_PTR(*ctx_id_new);
		*ctx_id_new = NULL;
	}
	return major;
}

/*
 * this interface is much simplified, currently we only need endtime.
 */
__u32 lgss_inquire_context(struct gss_ctx *context_handle,
			   unsigned long  *endtime)
{
	LASSERT(context_handle);
	LASSERT(context_handle->mech_type);
	LASSERT(context_handle->mech_type->gm_ops);
	LASSERT(context_handle->mech_type->gm_ops->gss_inquire_context);

	return context_handle->mech_type->gm_ops
		->gss_inquire_context(context_handle,
				      endtime);
}

/* gss_get_mic: compute a mic over message and return mic_token. */
__u32 lgss_get_mic(struct gss_ctx *context_handle,
		   int msgcnt,
		   rawobj_t *msg,
		   int iovcnt,
		   lnet_kiov_t *iovs,
		   rawobj_t *mic_token)
{
	LASSERT(context_handle);
	LASSERT(context_handle->mech_type);
	LASSERT(context_handle->mech_type->gm_ops);
	LASSERT(context_handle->mech_type->gm_ops->gss_get_mic);

	return context_handle->mech_type->gm_ops
		->gss_get_mic(context_handle,
			      msgcnt,
			      msg,
			      iovcnt,
			      iovs,
			      mic_token);
}

/* gss_verify_mic: check whether the provided mic_token verifies message. */
__u32 lgss_verify_mic(struct gss_ctx *context_handle,
		      int msgcnt,
		      rawobj_t *msg,
		      int iovcnt,
		      lnet_kiov_t *iovs,
		      rawobj_t *mic_token)
{
	LASSERT(context_handle);
	LASSERT(context_handle->mech_type);
	LASSERT(context_handle->mech_type->gm_ops);
	LASSERT(context_handle->mech_type->gm_ops->gss_verify_mic);

	return context_handle->mech_type->gm_ops
		->gss_verify_mic(context_handle,
				 msgcnt,
				 msg,
				 iovcnt,
				 iovs,
				 mic_token);
}

__u32 lgss_wrap(struct gss_ctx *context_handle,
		rawobj_t *gsshdr,
		rawobj_t *msg,
		int msg_buflen,
		rawobj_t *out_token)
{
	LASSERT(context_handle);
	LASSERT(context_handle->mech_type);
	LASSERT(context_handle->mech_type->gm_ops);
	LASSERT(context_handle->mech_type->gm_ops->gss_wrap);

	return context_handle->mech_type->gm_ops
		->gss_wrap(context_handle, gsshdr, msg, msg_buflen, out_token);
}

__u32 lgss_unwrap(struct gss_ctx *context_handle,
		  rawobj_t *gsshdr,
		  rawobj_t *token,
		  rawobj_t *out_msg)
{
	LASSERT(context_handle);
	LASSERT(context_handle->mech_type);
	LASSERT(context_handle->mech_type->gm_ops);
	LASSERT(context_handle->mech_type->gm_ops->gss_unwrap);

	return context_handle->mech_type->gm_ops
		->gss_unwrap(context_handle, gsshdr, token, out_msg);
}


__u32 lgss_prep_bulk(struct gss_ctx *context_handle,
		     struct ptlrpc_bulk_desc *desc)
{
	LASSERT(context_handle);
	LASSERT(context_handle->mech_type);
	LASSERT(context_handle->mech_type->gm_ops);
	LASSERT(context_handle->mech_type->gm_ops->gss_prep_bulk);

	return context_handle->mech_type->gm_ops
		->gss_prep_bulk(context_handle, desc);
}

__u32 lgss_wrap_bulk(struct gss_ctx *context_handle,
		     struct ptlrpc_bulk_desc *desc,
		     rawobj_t *token,
		     int adj_nob)
{
	LASSERT(context_handle);
	LASSERT(context_handle->mech_type);
	LASSERT(context_handle->mech_type->gm_ops);
	LASSERT(context_handle->mech_type->gm_ops->gss_wrap_bulk);

	return context_handle->mech_type->gm_ops
		->gss_wrap_bulk(context_handle, desc, token, adj_nob);
}

__u32 lgss_unwrap_bulk(struct gss_ctx *context_handle,
		       struct ptlrpc_bulk_desc *desc,
		       rawobj_t *token,
		       int adj_nob)
{
	LASSERT(context_handle);
	LASSERT(context_handle->mech_type);
	LASSERT(context_handle->mech_type->gm_ops);
	LASSERT(context_handle->mech_type->gm_ops->gss_unwrap_bulk);

	return context_handle->mech_type->gm_ops
		->gss_unwrap_bulk(context_handle, desc, token, adj_nob);
}

/* gss_delete_sec_context: free all resources associated with context_handle.
 * Note this differs from the RFC 2744-specified prototype in that we don't
 * bother returning an output token, since it would never be used anyway. */

__u32 lgss_delete_sec_context(struct gss_ctx **context_handle)
{
	struct gss_api_mech *mech;

	CDEBUG(D_SEC, "deleting %p\n", *context_handle);

	if (!*context_handle)
		return(GSS_S_NO_CONTEXT);

	mech = (*context_handle)->mech_type;
	if ((*context_handle)->internal_ctx_id != 0) {
		LASSERT(mech);
		LASSERT(mech->gm_ops);
		LASSERT(mech->gm_ops->gss_delete_sec_context);
		mech->gm_ops->gss_delete_sec_context(
					(*context_handle)->internal_ctx_id);
	}
	if (mech)
		lgss_mech_put(mech);

	OBD_FREE_PTR(*context_handle);
	*context_handle=NULL;
	return GSS_S_COMPLETE;
}

int lgss_display(struct gss_ctx *ctx,
		 char	   *buf,
		 int	     bufsize)
{
	LASSERT(ctx);
	LASSERT(ctx->mech_type);
	LASSERT(ctx->mech_type->gm_ops);
	LASSERT(ctx->mech_type->gm_ops->gss_display);

	return ctx->mech_type->gm_ops->gss_display(ctx, buf, bufsize);
}
