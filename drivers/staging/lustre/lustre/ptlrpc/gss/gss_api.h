/*
 * Modifications for Lustre
 *
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Author: Eric Mei <ericm@clusterfs.com>
 */

/*
 * Somewhat simplified version of the gss api.
 *
 * Dug Song <dugsong@monkey.org>
 * Andy Adamson <andros@umich.edu>
 * Bruce Fields <bfields@umich.edu>
 * Copyright (c) 2000 The Regents of the University of Michigan
 *
 */

#ifndef __PTLRPC_GSS_GSS_API_H_
#define __PTLRPC_GSS_GSS_API_H_

struct gss_api_mech;

/* The mechanism-independent gss-api context: */
struct gss_ctx {
	struct gss_api_mech    *mech_type;
	void		   *internal_ctx_id;
};

#define GSS_C_NO_BUFFER	 ((rawobj_t) 0)
#define GSS_C_NO_CONTEXT	((struct gss_ctx *) 0)
#define GSS_C_NULL_OID	  ((rawobj_t) 0)

/*
 * gss-api prototypes; note that these are somewhat simplified versions of
 * the prototypes specified in RFC 2744.
 */
__u32 lgss_import_sec_context(
		rawobj_t		*input_token,
		struct gss_api_mech     *mech,
		struct gss_ctx	 **ctx);
__u32 lgss_copy_reverse_context(
		struct gss_ctx	  *ctx,
		struct gss_ctx	 **ctx_new);
__u32 lgss_inquire_context(
		struct gss_ctx	  *ctx,
		unsigned long	   *endtime);
__u32 lgss_get_mic(
		struct gss_ctx	  *ctx,
		int		      msgcnt,
		rawobj_t		*msgs,
		int		      iovcnt,
		lnet_kiov_t	     *iovs,
		rawobj_t		*mic_token);
__u32 lgss_verify_mic(
		struct gss_ctx	  *ctx,
		int		      msgcnt,
		rawobj_t		*msgs,
		int		      iovcnt,
		lnet_kiov_t	     *iovs,
		rawobj_t		*mic_token);
__u32 lgss_wrap(
		struct gss_ctx	  *ctx,
		rawobj_t		*gsshdr,
		rawobj_t		*msg,
		int		      msg_buflen,
		rawobj_t		*out_token);
__u32 lgss_unwrap(
		struct gss_ctx	  *ctx,
		rawobj_t		*gsshdr,
		rawobj_t		*token,
		rawobj_t		*out_msg);
__u32 lgss_prep_bulk(
		struct gss_ctx	  *gctx,
		struct ptlrpc_bulk_desc *desc);
__u32 lgss_wrap_bulk(
		struct gss_ctx	  *gctx,
		struct ptlrpc_bulk_desc *desc,
		rawobj_t		*token,
		int		      adj_nob);
__u32 lgss_unwrap_bulk(
		struct gss_ctx	  *gctx,
		struct ptlrpc_bulk_desc *desc,
		rawobj_t		*token,
		int		      adj_nob);
__u32 lgss_delete_sec_context(
		struct gss_ctx	 **ctx);
int lgss_display(
		struct gss_ctx	  *ctx,
		char		    *buf,
		int		      bufsize);

struct subflavor_desc {
	__u32	   sf_subflavor;
	__u32	   sf_qop;
	__u32	   sf_service;
	char	   *sf_name;
};

/* Each mechanism is described by the following struct: */
struct gss_api_mech {
	struct list_head	      gm_list;
	module_t	   *gm_owner;
	char		   *gm_name;
	rawobj_t		gm_oid;
	atomic_t	    gm_count;
	struct gss_api_ops     *gm_ops;
	int		     gm_sf_num;
	struct subflavor_desc  *gm_sfs;
};

/* and must provide the following operations: */
struct gss_api_ops {
	__u32 (*gss_import_sec_context)(
			rawobj_t	       *input_token,
			struct gss_ctx	 *ctx);
	__u32 (*gss_copy_reverse_context)(
			struct gss_ctx	 *ctx,
			struct gss_ctx	 *ctx_new);
	__u32 (*gss_inquire_context)(
			struct gss_ctx	 *ctx,
			unsigned long	  *endtime);
	__u32 (*gss_get_mic)(
			struct gss_ctx	 *ctx,
			int		     msgcnt,
			rawobj_t	       *msgs,
			int		     iovcnt,
			lnet_kiov_t	    *iovs,
			rawobj_t	       *mic_token);
	__u32 (*gss_verify_mic)(
			struct gss_ctx	 *ctx,
			int		     msgcnt,
			rawobj_t	       *msgs,
			int		     iovcnt,
			lnet_kiov_t	    *iovs,
			rawobj_t	       *mic_token);
	__u32 (*gss_wrap)(
			struct gss_ctx	 *ctx,
			rawobj_t	       *gsshdr,
			rawobj_t	       *msg,
			int		     msg_buflen,
			rawobj_t	       *out_token);
	__u32 (*gss_unwrap)(
			struct gss_ctx	 *ctx,
			rawobj_t	       *gsshdr,
			rawobj_t	       *token,
			rawobj_t	       *out_msg);
	__u32 (*gss_prep_bulk)(
			struct gss_ctx	 *gctx,
			struct ptlrpc_bulk_desc *desc);
	__u32 (*gss_wrap_bulk)(
			struct gss_ctx	 *gctx,
			struct ptlrpc_bulk_desc *desc,
			rawobj_t	       *token,
			int		     adj_nob);
	__u32 (*gss_unwrap_bulk)(
			struct gss_ctx	 *gctx,
			struct ptlrpc_bulk_desc *desc,
			rawobj_t	       *token,
			int		     adj_nob);
	void (*gss_delete_sec_context)(
			void		   *ctx);
	int  (*gss_display)(
			struct gss_ctx	 *ctx,
			char		   *buf,
			int		     bufsize);
};

int lgss_mech_register(struct gss_api_mech *mech);
void lgss_mech_unregister(struct gss_api_mech *mech);

struct gss_api_mech * lgss_OID_to_mech(rawobj_t *oid);
struct gss_api_mech * lgss_name_to_mech(char *name);
struct gss_api_mech * lgss_subflavor_to_mech(__u32 subflavor);

struct gss_api_mech * lgss_mech_get(struct gss_api_mech *mech);
void lgss_mech_put(struct gss_api_mech *mech);

#endif /* __PTLRPC_GSS_GSS_API_H_ */
