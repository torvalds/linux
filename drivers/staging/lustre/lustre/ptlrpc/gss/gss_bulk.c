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
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ptlrpc/gss/gss_bulk.c
 *
 * Author: Eric Mei <eric.mei@sun.com>
 */

#define DEBUG_SUBSYSTEM S_SEC
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/crypto.h>

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

int gss_cli_ctx_wrap_bulk(struct ptlrpc_cli_ctx *ctx,
			  struct ptlrpc_request *req,
			  struct ptlrpc_bulk_desc *desc)
{
	struct gss_cli_ctx	      *gctx;
	struct lustre_msg	       *msg;
	struct ptlrpc_bulk_sec_desc     *bsd;
	rawobj_t			 token;
	__u32			    maj;
	int			      offset;
	int			      rc;
	ENTRY;

	LASSERT(req->rq_pack_bulk);
	LASSERT(req->rq_bulk_read || req->rq_bulk_write);

	gctx = container_of(ctx, struct gss_cli_ctx, gc_base);
	LASSERT(gctx->gc_mechctx);

	switch (SPTLRPC_FLVR_SVC(req->rq_flvr.sf_rpc)) {
	case SPTLRPC_SVC_NULL:
		LASSERT(req->rq_reqbuf->lm_bufcount >= 3);
		msg = req->rq_reqbuf;
		offset = msg->lm_bufcount - 1;
		break;
	case SPTLRPC_SVC_AUTH:
	case SPTLRPC_SVC_INTG:
		LASSERT(req->rq_reqbuf->lm_bufcount >= 4);
		msg = req->rq_reqbuf;
		offset = msg->lm_bufcount - 2;
		break;
	case SPTLRPC_SVC_PRIV:
		LASSERT(req->rq_clrbuf->lm_bufcount >= 2);
		msg = req->rq_clrbuf;
		offset = msg->lm_bufcount - 1;
		break;
	default:
		LBUG();
	}

	bsd = lustre_msg_buf(msg, offset, sizeof(*bsd));
	bsd->bsd_version = 0;
	bsd->bsd_flags = 0;
	bsd->bsd_type = SPTLRPC_BULK_DEFAULT;
	bsd->bsd_svc = SPTLRPC_FLVR_BULK_SVC(req->rq_flvr.sf_rpc);

	if (bsd->bsd_svc == SPTLRPC_BULK_SVC_NULL)
		RETURN(0);

	LASSERT(bsd->bsd_svc == SPTLRPC_BULK_SVC_INTG ||
		bsd->bsd_svc == SPTLRPC_BULK_SVC_PRIV);

	if (req->rq_bulk_read) {
		/*
		 * bulk read: prepare receiving pages only for privacy mode.
		 */
		if (bsd->bsd_svc == SPTLRPC_BULK_SVC_PRIV)
			return gss_cli_prep_bulk(req, desc);
	} else {
		/*
		 * bulk write: sign or encrypt bulk pages.
		 */
		bsd->bsd_nob = desc->bd_nob;

		if (bsd->bsd_svc == SPTLRPC_BULK_SVC_INTG) {
			/* integrity mode */
			token.data = bsd->bsd_data;
			token.len = lustre_msg_buflen(msg, offset) -
				    sizeof(*bsd);

			maj = lgss_get_mic(gctx->gc_mechctx, 0, NULL,
					   desc->bd_iov_count, desc->bd_iov,
					   &token);
			if (maj != GSS_S_COMPLETE) {
				CWARN("failed to sign bulk data: %x\n", maj);
				RETURN(-EACCES);
			}
		} else {
			/* privacy mode */
			if (desc->bd_iov_count == 0)
				RETURN(0);

			rc = sptlrpc_enc_pool_get_pages(desc);
			if (rc) {
				CERROR("bulk write: failed to allocate "
				       "encryption pages: %d\n", rc);
				RETURN(rc);
			}

			token.data = bsd->bsd_data;
			token.len = lustre_msg_buflen(msg, offset) -
				    sizeof(*bsd);

			maj = lgss_wrap_bulk(gctx->gc_mechctx, desc, &token, 0);
			if (maj != GSS_S_COMPLETE) {
				CWARN("fail to encrypt bulk data: %x\n", maj);
				RETURN(-EACCES);
			}
		}
	}

	RETURN(0);
}

int gss_cli_ctx_unwrap_bulk(struct ptlrpc_cli_ctx *ctx,
			    struct ptlrpc_request *req,
			    struct ptlrpc_bulk_desc *desc)
{
	struct gss_cli_ctx	      *gctx;
	struct lustre_msg	       *rmsg, *vmsg;
	struct ptlrpc_bulk_sec_desc     *bsdr, *bsdv;
	rawobj_t			 token;
	__u32			    maj;
	int			      roff, voff;
	ENTRY;

	LASSERT(req->rq_pack_bulk);
	LASSERT(req->rq_bulk_read || req->rq_bulk_write);

	switch (SPTLRPC_FLVR_SVC(req->rq_flvr.sf_rpc)) {
	case SPTLRPC_SVC_NULL:
		vmsg = req->rq_repdata;
		voff = vmsg->lm_bufcount - 1;
		LASSERT(vmsg && vmsg->lm_bufcount >= 3);

		rmsg = req->rq_reqbuf;
		roff = rmsg->lm_bufcount - 1; /* last segment */
		LASSERT(rmsg && rmsg->lm_bufcount >= 3);
		break;
	case SPTLRPC_SVC_AUTH:
	case SPTLRPC_SVC_INTG:
		vmsg = req->rq_repdata;
		voff = vmsg->lm_bufcount - 2;
		LASSERT(vmsg && vmsg->lm_bufcount >= 4);

		rmsg = req->rq_reqbuf;
		roff = rmsg->lm_bufcount - 2; /* second last segment */
		LASSERT(rmsg && rmsg->lm_bufcount >= 4);
		break;
	case SPTLRPC_SVC_PRIV:
		vmsg = req->rq_repdata;
		voff = vmsg->lm_bufcount - 1;
		LASSERT(vmsg && vmsg->lm_bufcount >= 2);

		rmsg = req->rq_clrbuf;
		roff = rmsg->lm_bufcount - 1; /* last segment */
		LASSERT(rmsg && rmsg->lm_bufcount >= 2);
		break;
	default:
		LBUG();
	}

	bsdr = lustre_msg_buf(rmsg, roff, sizeof(*bsdr));
	bsdv = lustre_msg_buf(vmsg, voff, sizeof(*bsdv));
	LASSERT(bsdr && bsdv);

	if (bsdr->bsd_version != bsdv->bsd_version ||
	    bsdr->bsd_type != bsdv->bsd_type ||
	    bsdr->bsd_svc != bsdv->bsd_svc) {
		CERROR("bulk security descriptor mismatch: "
		       "(%u,%u,%u) != (%u,%u,%u)\n",
		       bsdr->bsd_version, bsdr->bsd_type, bsdr->bsd_svc,
		       bsdv->bsd_version, bsdv->bsd_type, bsdv->bsd_svc);
		RETURN(-EPROTO);
	}

	LASSERT(bsdv->bsd_svc == SPTLRPC_BULK_SVC_NULL ||
		bsdv->bsd_svc == SPTLRPC_BULK_SVC_INTG ||
		bsdv->bsd_svc == SPTLRPC_BULK_SVC_PRIV);

	/*
	 * in privacy mode if return success, make sure bd_nob_transferred
	 * is the actual size of the clear text, otherwise upper layer
	 * may be surprised.
	 */
	if (req->rq_bulk_write) {
		if (bsdv->bsd_flags & BSD_FL_ERR) {
			CERROR("server reported bulk i/o failure\n");
			RETURN(-EIO);
		}

		if (bsdv->bsd_svc == SPTLRPC_BULK_SVC_PRIV)
			desc->bd_nob_transferred = desc->bd_nob;
	} else {
		/*
		 * bulk read, upon return success, bd_nob_transferred is
		 * the size of plain text actually received.
		 */
		gctx = container_of(ctx, struct gss_cli_ctx, gc_base);
		LASSERT(gctx->gc_mechctx);

		if (bsdv->bsd_svc == SPTLRPC_BULK_SVC_INTG) {
			int i, nob;

			/* fix the actual data size */
			for (i = 0, nob = 0; i < desc->bd_iov_count; i++) {
				if (desc->bd_iov[i].kiov_len + nob >
				    desc->bd_nob_transferred) {
					desc->bd_iov[i].kiov_len =
						desc->bd_nob_transferred - nob;
				}
				nob += desc->bd_iov[i].kiov_len;
			}

			token.data = bsdv->bsd_data;
			token.len = lustre_msg_buflen(vmsg, voff) -
				    sizeof(*bsdv);

			maj = lgss_verify_mic(gctx->gc_mechctx, 0, NULL,
					      desc->bd_iov_count, desc->bd_iov,
					      &token);
			if (maj != GSS_S_COMPLETE) {
				CERROR("failed to verify bulk read: %x\n", maj);
				RETURN(-EACCES);
			}
		} else if (bsdv->bsd_svc == SPTLRPC_BULK_SVC_PRIV) {
			desc->bd_nob = bsdv->bsd_nob;
			if (desc->bd_nob == 0)
				RETURN(0);

			token.data = bsdv->bsd_data;
			token.len = lustre_msg_buflen(vmsg, voff) -
				    sizeof(*bsdr);

			maj = lgss_unwrap_bulk(gctx->gc_mechctx, desc,
					       &token, 1);
			if (maj != GSS_S_COMPLETE) {
				CERROR("failed to decrypt bulk read: %x\n",
				       maj);
				RETURN(-EACCES);
			}

			desc->bd_nob_transferred = desc->bd_nob;
		}
	}

	RETURN(0);
}

static int gss_prep_bulk(struct ptlrpc_bulk_desc *desc,
			 struct gss_ctx *mechctx)
{
	int     rc;

	if (desc->bd_iov_count == 0)
		return 0;

	rc = sptlrpc_enc_pool_get_pages(desc);
	if (rc)
		return rc;

	if (lgss_prep_bulk(mechctx, desc) != GSS_S_COMPLETE)
		return -EACCES;

	return 0;
}

int gss_cli_prep_bulk(struct ptlrpc_request *req,
		      struct ptlrpc_bulk_desc *desc)
{
	int	     rc;
	ENTRY;

	LASSERT(req->rq_cli_ctx);
	LASSERT(req->rq_pack_bulk);
	LASSERT(req->rq_bulk_read);

	if (SPTLRPC_FLVR_BULK_SVC(req->rq_flvr.sf_rpc) != SPTLRPC_BULK_SVC_PRIV)
		RETURN(0);

	rc = gss_prep_bulk(desc, ctx2gctx(req->rq_cli_ctx)->gc_mechctx);
	if (rc)
		CERROR("bulk read: failed to prepare encryption "
		       "pages: %d\n", rc);

	RETURN(rc);
}

int gss_svc_prep_bulk(struct ptlrpc_request *req,
		      struct ptlrpc_bulk_desc *desc)
{
	struct gss_svc_reqctx	*grctx;
	struct ptlrpc_bulk_sec_desc  *bsd;
	int			   rc;
	ENTRY;

	LASSERT(req->rq_svc_ctx);
	LASSERT(req->rq_pack_bulk);
	LASSERT(req->rq_bulk_write);

	grctx = gss_svc_ctx2reqctx(req->rq_svc_ctx);
	LASSERT(grctx->src_reqbsd);
	LASSERT(grctx->src_repbsd);
	LASSERT(grctx->src_ctx);
	LASSERT(grctx->src_ctx->gsc_mechctx);

	bsd = grctx->src_reqbsd;
	if (bsd->bsd_svc != SPTLRPC_BULK_SVC_PRIV)
		RETURN(0);

	rc = gss_prep_bulk(desc, grctx->src_ctx->gsc_mechctx);
	if (rc)
		CERROR("bulk write: failed to prepare encryption "
		       "pages: %d\n", rc);

	RETURN(rc);
}

int gss_svc_unwrap_bulk(struct ptlrpc_request *req,
			struct ptlrpc_bulk_desc *desc)
{
	struct gss_svc_reqctx	*grctx;
	struct ptlrpc_bulk_sec_desc  *bsdr, *bsdv;
	rawobj_t		      token;
	__u32			 maj;
	ENTRY;

	LASSERT(req->rq_svc_ctx);
	LASSERT(req->rq_pack_bulk);
	LASSERT(req->rq_bulk_write);

	grctx = gss_svc_ctx2reqctx(req->rq_svc_ctx);

	LASSERT(grctx->src_reqbsd);
	LASSERT(grctx->src_repbsd);
	LASSERT(grctx->src_ctx);
	LASSERT(grctx->src_ctx->gsc_mechctx);

	bsdr = grctx->src_reqbsd;
	bsdv = grctx->src_repbsd;

	/* bsdr has been sanity checked during unpacking */
	bsdv->bsd_version = 0;
	bsdv->bsd_type = SPTLRPC_BULK_DEFAULT;
	bsdv->bsd_svc = bsdr->bsd_svc;
	bsdv->bsd_flags = 0;

	switch (bsdv->bsd_svc) {
	case SPTLRPC_BULK_SVC_INTG:
		token.data = bsdr->bsd_data;
		token.len = grctx->src_reqbsd_size - sizeof(*bsdr);

		maj = lgss_verify_mic(grctx->src_ctx->gsc_mechctx, 0, NULL,
				      desc->bd_iov_count, desc->bd_iov, &token);
		if (maj != GSS_S_COMPLETE) {
			bsdv->bsd_flags |= BSD_FL_ERR;
			CERROR("failed to verify bulk signature: %x\n", maj);
			RETURN(-EACCES);
		}
		break;
	case SPTLRPC_BULK_SVC_PRIV:
		if (bsdr->bsd_nob != desc->bd_nob) {
			bsdv->bsd_flags |= BSD_FL_ERR;
			CERROR("prepared nob %d doesn't match the actual "
			       "nob %d\n", desc->bd_nob, bsdr->bsd_nob);
			RETURN(-EPROTO);
		}

		if (desc->bd_iov_count == 0) {
			LASSERT(desc->bd_nob == 0);
			break;
		}

		token.data = bsdr->bsd_data;
		token.len = grctx->src_reqbsd_size - sizeof(*bsdr);

		maj = lgss_unwrap_bulk(grctx->src_ctx->gsc_mechctx,
				       desc, &token, 0);
		if (maj != GSS_S_COMPLETE) {
			bsdv->bsd_flags |= BSD_FL_ERR;
			CERROR("failed decrypt bulk data: %x\n", maj);
			RETURN(-EACCES);
		}
		break;
	}

	RETURN(0);
}

int gss_svc_wrap_bulk(struct ptlrpc_request *req,
		      struct ptlrpc_bulk_desc *desc)
{
	struct gss_svc_reqctx	*grctx;
	struct ptlrpc_bulk_sec_desc  *bsdr, *bsdv;
	rawobj_t		      token;
	__u32			 maj;
	int			   rc;
	ENTRY;

	LASSERT(req->rq_svc_ctx);
	LASSERT(req->rq_pack_bulk);
	LASSERT(req->rq_bulk_read);

	grctx = gss_svc_ctx2reqctx(req->rq_svc_ctx);

	LASSERT(grctx->src_reqbsd);
	LASSERT(grctx->src_repbsd);
	LASSERT(grctx->src_ctx);
	LASSERT(grctx->src_ctx->gsc_mechctx);

	bsdr = grctx->src_reqbsd;
	bsdv = grctx->src_repbsd;

	/* bsdr has been sanity checked during unpacking */
	bsdv->bsd_version = 0;
	bsdv->bsd_type = SPTLRPC_BULK_DEFAULT;
	bsdv->bsd_svc = bsdr->bsd_svc;
	bsdv->bsd_flags = 0;

	switch (bsdv->bsd_svc) {
	case SPTLRPC_BULK_SVC_INTG:
		token.data = bsdv->bsd_data;
		token.len = grctx->src_repbsd_size - sizeof(*bsdv);

		maj = lgss_get_mic(grctx->src_ctx->gsc_mechctx, 0, NULL,
				   desc->bd_iov_count, desc->bd_iov, &token);
		if (maj != GSS_S_COMPLETE) {
			bsdv->bsd_flags |= BSD_FL_ERR;
			CERROR("failed to sign bulk data: %x\n", maj);
			RETURN(-EACCES);
		}
		break;
	case SPTLRPC_BULK_SVC_PRIV:
		bsdv->bsd_nob = desc->bd_nob;

		if (desc->bd_iov_count == 0) {
			LASSERT(desc->bd_nob == 0);
			break;
		}

		rc = sptlrpc_enc_pool_get_pages(desc);
		if (rc) {
			bsdv->bsd_flags |= BSD_FL_ERR;
			CERROR("bulk read: failed to allocate encryption "
			       "pages: %d\n", rc);
			RETURN(rc);
		}

		token.data = bsdv->bsd_data;
		token.len = grctx->src_repbsd_size - sizeof(*bsdv);

		maj = lgss_wrap_bulk(grctx->src_ctx->gsc_mechctx,
				     desc, &token, 1);
		if (maj != GSS_S_COMPLETE) {
			bsdv->bsd_flags |= BSD_FL_ERR;
			CERROR("failed to encrypt bulk data: %x\n", maj);
			RETURN(-EACCES);
		}
		break;
	}

	RETURN(0);
}
