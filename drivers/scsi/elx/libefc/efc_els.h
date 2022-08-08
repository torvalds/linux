/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#ifndef __EFC_ELS_H__
#define __EFC_ELS_H__

#define EFC_STATUS_INVALID	INT_MAX
#define EFC_ELS_IO_POOL_SZ	1024

struct efc_els_io_req {
	struct list_head	list_entry;
	struct kref		ref;
	void			(*release)(struct kref *arg);
	struct efc_node		*node;
	void			*cb;
	u32			els_retries_remaining;
	bool			els_req_free;
	struct timer_list       delay_timer;

	const char		*display_name;

	struct efc_disc_io	io;
};

typedef int(*efc_hw_srrs_cb_t)(void *arg, u32 length, int status,
			       u32 ext_status);

void _efc_els_io_free(struct kref *arg);
struct efc_els_io_req *
efc_els_io_alloc(struct efc_node *node, u32 reqlen);
struct efc_els_io_req *
efc_els_io_alloc_size(struct efc_node *node, u32 reqlen, u32 rsplen);
void efc_els_io_free(struct efc_els_io_req *els);

/* ELS command send */
typedef void (*els_cb_t)(struct efc_node *node,
			 struct efc_node_cb *cbdata, void *arg);
int
efc_send_plogi(struct efc_node *node);
int
efc_send_flogi(struct efc_node *node);
int
efc_send_fdisc(struct efc_node *node);
int
efc_send_prli(struct efc_node *node);
int
efc_send_prlo(struct efc_node *node);
int
efc_send_logo(struct efc_node *node);
int
efc_send_adisc(struct efc_node *node);
int
efc_send_pdisc(struct efc_node *node);
int
efc_send_scr(struct efc_node *node);
int
efc_ns_send_rftid(struct efc_node *node);
int
efc_ns_send_rffid(struct efc_node *node);
int
efc_ns_send_gidpt(struct efc_node *node);
void
efc_els_io_cleanup(struct efc_els_io_req *els, int evt, void *arg);

/* ELS acc send */
int
efc_send_ls_acc(struct efc_node *node, u32 ox_id);
int
efc_send_ls_rjt(struct efc_node *node, u32 ox_id, u32 reason_cod,
		u32 reason_code_expl, u32 vendor_unique);
int
efc_send_flogi_p2p_acc(struct efc_node *node, u32 ox_id, u32 s_id);
int
efc_send_flogi_acc(struct efc_node *node, u32 ox_id, u32 is_fport);
int
efc_send_plogi_acc(struct efc_node *node, u32 ox_id);
int
efc_send_prli_acc(struct efc_node *node, u32 ox_id);
int
efc_send_logo_acc(struct efc_node *node, u32 ox_id);
int
efc_send_prlo_acc(struct efc_node *node, u32 ox_id);
int
efc_send_adisc_acc(struct efc_node *node, u32 ox_id);

int
efc_bls_send_acc_hdr(struct efc *efc, struct efc_node *node,
		     struct fc_frame_header *hdr);
int
efc_bls_send_rjt_hdr(struct efc_els_io_req *io, struct fc_frame_header *hdr);

int
efc_els_io_list_empty(struct efc_node *node, struct list_head *list);

/* CT */
int
efc_send_ct_rsp(struct efc *efc, struct efc_node *node, u16 ox_id,
		struct fc_ct_hdr *ct_hdr, u32 cmd_rsp_code, u32 reason_code,
		u32 reason_code_explanation);

int
efc_send_bls_acc(struct efc_node *node, struct fc_frame_header *hdr);

#endif /* __EFC_ELS_H__ */
