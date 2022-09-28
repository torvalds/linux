/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mem_buf

#if !defined(_TRACE_MEM_BUF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MEM_BUF_H
#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/mem-buf.h>

#include "mem-buf-msgq.h"

#ifdef CREATE_TRACE_POINTS
static void __maybe_unused gh_acl_to_vmid_perms(struct gh_acl_desc *acl_desc,
						u16 *vmids, u8 *perms)
{
	unsigned int i;

	for (i = 0; i < acl_desc->n_acl_entries; i++) {
		vmids[i] = acl_desc->acl_entries[i].vmid;
		perms[i] = acl_desc->acl_entries[i].perms;
	}
}

static void __maybe_unused
gh_sgl_to_ipa_bases_sizes(struct gh_sgl_desc *sgl_desc,
			  u64 *ipa_bases, u64 *sizes)
{
	unsigned int i;

	for (i = 0; i < sgl_desc->n_sgl_entries; i++) {
		ipa_bases[i] = sgl_desc->sgl_entries[i].ipa_base;
		sizes[i] = sgl_desc->sgl_entries[i].size;
	}
}

static char __maybe_unused *mem_type_to_str(enum mem_buf_mem_type type)
{
	if (type == MEM_BUF_ION_MEM_TYPE)
		return "ION_MEM_TYPE";

	return NULL;
}

static char __maybe_unused *msg_type_to_str(enum mem_buf_msg_type type)
{
	if (type == MEM_BUF_ALLOC_REQ)
		return "MEM_BUF_ALLOC_REQ";
	else if (type == MEM_BUF_ALLOC_RESP)
		return "MEM_BUF_ALLOC_RESP";
	else if (type == MEM_BUF_ALLOC_RELINQUISH)
		return "MEM_BUF_ALLOC_RELINQUISH";
	else if (type == MEM_BUF_ALLOC_RELINQUISH_RESP)
		return "MEM_BUF_ALLOC_RELINQUISH_RESP";

	return NULL;
}
#endif /* CREATE_TRACE_POINTS */

TRACE_EVENT(mem_buf_alloc_info,

	TP_PROTO(size_t size, enum mem_buf_mem_type src_mem_type,
		 enum mem_buf_mem_type dst_mem_type,
		 struct gh_acl_desc *acl_desc),

	TP_ARGS(size, src_mem_type, dst_mem_type, acl_desc),

	TP_STRUCT__entry(
		__field(size_t, size)
		__field(u32, nr_acl_entries)
		__string(src_type, mem_type_to_str(src_mem_type))
		__string(dst_type, mem_type_to_str(dst_mem_type))
		__dynamic_array(u16, vmids, acl_desc->n_acl_entries)
		__dynamic_array(u8, perms, acl_desc->n_acl_entries)
	),

	TP_fast_assign(
		__entry->size = size;
		__assign_str(src_type, mem_type_to_str(src_mem_type));
		__assign_str(dst_type, mem_type_to_str(dst_mem_type));
		__entry->nr_acl_entries = acl_desc->n_acl_entries;
		gh_acl_to_vmid_perms(acl_desc, __get_dynamic_array(vmids),
				     __get_dynamic_array(perms));
	),

	TP_printk("size: 0x%lx src mem type: %s dst mem type: %s nr ACL entries: %d ACL VMIDs: %s ACL Perms: %s",
		  __entry->size, __get_str(src_type), __get_str(dst_type),
		  __entry->nr_acl_entries,
		  __print_array(__get_dynamic_array(vmids),
				__entry->nr_acl_entries, sizeof(u16)),
		  __print_array(__get_dynamic_array(perms),
				__entry->nr_acl_entries, sizeof(u8))
	)
);

DECLARE_EVENT_CLASS(alloc_req_msg_class,

	TP_PROTO(struct mem_buf_alloc_req *req),

	TP_ARGS(req),

	TP_STRUCT__entry(
		__field(u32, txn_id)
		__string(msg_type, msg_type_to_str(req->hdr.msg_type))
		__field(u64, size)
		__string(src_type, mem_type_to_str(req->src_mem_type))
		__field(u32, nr_acl_entries)
		__dynamic_array(u16, vmids, req->acl_desc.n_acl_entries)
		__dynamic_array(u8, perms, req->acl_desc.n_acl_entries)
	),

	TP_fast_assign(
		__entry->txn_id = req->hdr.txn_id;
		__assign_str(msg_type, msg_type_to_str(req->hdr.msg_type));
		__entry->size = req->size;
		__assign_str(src_type, mem_type_to_str(req->src_mem_type));
		__entry->nr_acl_entries = req->acl_desc.n_acl_entries;
		gh_acl_to_vmid_perms(&req->acl_desc, __get_dynamic_array(vmids),
				     __get_dynamic_array(perms));
	),

	TP_printk("txn_id: %d msg_type: %s alloc_sz: 0x%lx src_mem_type: %s nr ACL entries: %d ACL VMIDs: %s ACL Perms: %s",
		  __entry->txn_id, __get_str(msg_type), __entry->size,
		  __get_str(src_type), __entry->nr_acl_entries,
		  __print_array(__get_dynamic_array(vmids),
				__entry->nr_acl_entries, sizeof(u16)),
		  __print_array(__get_dynamic_array(perms),
				__entry->nr_acl_entries, sizeof(u8))
	)
);

DEFINE_EVENT(alloc_req_msg_class, send_alloc_req,

	TP_PROTO(struct mem_buf_alloc_req *req),

	TP_ARGS(req)
);

DEFINE_EVENT(alloc_req_msg_class, receive_alloc_req,

	TP_PROTO(struct mem_buf_alloc_req *req),

	TP_ARGS(req)
);

DECLARE_EVENT_CLASS(relinquish_req_msg_class,

	TP_PROTO(struct mem_buf_alloc_relinquish *rel_req),

	TP_ARGS(rel_req),

	TP_STRUCT__entry(
		__string(msg_type, msg_type_to_str(rel_req->hdr.msg_type))
		__field(gh_memparcel_handle_t, hdl)
		__field(u32, txn_id)
	),

	TP_fast_assign(
		__assign_str(msg_type, msg_type_to_str(rel_req->hdr.msg_type));
		__entry->hdl = rel_req->hdl;
		__entry->txn_id = rel_req->hdr.txn_id;
	),

	TP_printk("msg_type: %s memparcel_hdl: 0x%x txn_id: 0x%x",
		  __get_str(msg_type), __entry->hdl, __entry->txn_id)
);

DEFINE_EVENT(relinquish_req_msg_class, send_relinquish_msg,

	TP_PROTO(struct mem_buf_alloc_relinquish *rel_req),

	TP_ARGS(rel_req)
);


DEFINE_EVENT(relinquish_req_msg_class, receive_relinquish_msg,

	TP_PROTO(struct mem_buf_alloc_relinquish *rel_req),

	TP_ARGS(rel_req)
);

DECLARE_EVENT_CLASS(alloc_resp_class,

	TP_PROTO(struct mem_buf_alloc_resp *resp),

	TP_ARGS(resp),

	TP_STRUCT__entry(
		__field(u32, txn_id)
		__string(msg_type, msg_type_to_str(resp->hdr.msg_type))
		__field(s32, ret)
		__field(gh_memparcel_handle_t, hdl)
	),

	TP_fast_assign(
		__entry->txn_id = resp->hdr.txn_id;
		__assign_str(msg_type, msg_type_to_str(resp->hdr.msg_type));
		__entry->ret = resp->ret;
		__entry->hdl = resp->hdl;
	),

	TP_printk("txn_id: %d msg_type: %s ret: %d memparcel_hdl: 0x%x",
		  __entry->txn_id, __get_str(msg_type), __entry->ret,
		  __entry->hdl
	)
);

DEFINE_EVENT(alloc_resp_class, send_alloc_resp_msg,

	TP_PROTO(struct mem_buf_alloc_resp *resp),

	TP_ARGS(resp)
);

DEFINE_EVENT(alloc_resp_class, receive_alloc_resp_msg,

	TP_PROTO(struct mem_buf_alloc_resp *resp),

	TP_ARGS(resp)
);

DECLARE_EVENT_CLASS(relinquish_resp_class,

	TP_PROTO(struct mem_buf_alloc_relinquish *resp),

	TP_ARGS(resp),

	TP_STRUCT__entry(
		__field(u32, txn_id)
		__string(msg_type, msg_type_to_str(resp->hdr.msg_type))
	),

	TP_fast_assign(
		__entry->txn_id = resp->hdr.txn_id;
		__assign_str(msg_type, msg_type_to_str(resp->hdr.msg_type));
	),

	TP_printk("txn_id: %d msg_type: %s",
		  __entry->txn_id, __get_str(msg_type)
	)
);

DEFINE_EVENT(relinquish_resp_class, send_relinquish_resp_msg,

	TP_PROTO(struct mem_buf_alloc_relinquish *resp),

	TP_ARGS(resp)
);

DEFINE_EVENT(relinquish_resp_class, receive_relinquish_resp_msg,

	TP_PROTO(struct mem_buf_alloc_relinquish *resp),

	TP_ARGS(resp)
);

TRACE_EVENT(lookup_sgl,

	TP_PROTO(struct gh_sgl_desc *sgl_desc, int ret,
		     gh_memparcel_handle_t hdl),

	TP_ARGS(sgl_desc, ret, hdl),

	TP_STRUCT__entry(
		__field(u16, nr_sgl_entries)
		__dynamic_array(u64, ipa_bases, sgl_desc->n_sgl_entries)
		__dynamic_array(u64, sizes, sgl_desc->n_sgl_entries)
		__field(int, ret)
		__field(gh_memparcel_handle_t, hdl)
	),

	TP_fast_assign(
		__entry->nr_sgl_entries = sgl_desc->n_sgl_entries;
		gh_sgl_to_ipa_bases_sizes(sgl_desc,
					  __get_dynamic_array(ipa_bases),
					  __get_dynamic_array(sizes));
		__entry->ret = ret;
		__entry->hdl = hdl;
	),

	TP_printk("SGL entries: %d SGL IPA bases: %s SGL sizes: %s ret: %d memparcel_hdl: 0x%x",
		  __entry->nr_sgl_entries,
		  __print_array(__get_dynamic_array(ipa_bases),
				__entry->nr_sgl_entries, sizeof(u64)),
		  __print_array(__get_dynamic_array(sizes),
				__entry->nr_sgl_entries, sizeof(u64)),
		  __entry->ret, __entry->hdl
	)
);

TRACE_EVENT(map_mem_s2,

	TP_PROTO(gh_memparcel_handle_t hdl, struct gh_sgl_desc *sgl_desc),

	TP_ARGS(hdl, sgl_desc),

	TP_STRUCT__entry(
		__field(gh_memparcel_handle_t, hdl)
		__field(u16, nr_sgl_entries)
		__dynamic_array(u64, ipa_bases, sgl_desc->n_sgl_entries)
		__dynamic_array(u64, sizes, sgl_desc->n_sgl_entries)
	),

	TP_fast_assign(
		__entry->hdl = hdl;
		__entry->nr_sgl_entries = sgl_desc->n_sgl_entries;
		gh_sgl_to_ipa_bases_sizes(sgl_desc,
					  __get_dynamic_array(ipa_bases),
					  __get_dynamic_array(sizes));
	),

	TP_printk("MEM_ACCEPT successful memparcel hdl: 0x%x SGL entries: %d SGL IPA bases: %s SGL sizes: %s",
		  __entry->hdl, __entry->nr_sgl_entries,
		  __print_array(__get_dynamic_array(ipa_bases),
				__entry->nr_sgl_entries, sizeof(u64)),
		  __print_array(__get_dynamic_array(sizes),
				__entry->nr_sgl_entries, sizeof(u64))
	)
);

#endif /* _TRACE_MEM_BUF_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../../drivers/soc/qcom/mem_buf/

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-mem-buf

/* This part must be outside protection */
#include <trace/define_trace.h>
