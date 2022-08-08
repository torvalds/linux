// SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB
/* Copyright (c) 2016 - 2021 Intel Corporation */
#include "osdep.h"
#include "status.h"
#include "hmc.h"
#include "defs.h"
#include "type.h"
#include "protos.h"
#include "uda.h"
#include "uda_d.h"

/**
 * irdma_sc_access_ah() - Create, modify or delete AH
 * @cqp: struct for cqp hw
 * @info: ah information
 * @op: Operation
 * @scratch: u64 saved to be used during cqp completion
 */
enum irdma_status_code irdma_sc_access_ah(struct irdma_sc_cqp *cqp,
					  struct irdma_ah_info *info,
					  u32 op, u64 scratch)
{
	__le64 *wqe;
	u64 qw1, qw2;

	wqe = irdma_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return IRDMA_ERR_RING_FULL;

	set_64bit_val(wqe, 0, ether_addr_to_u64(info->mac_addr) << 16);
	qw1 = FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_PDINDEXLO, info->pd_idx) |
	      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_TC, info->tc_tos) |
	      FIELD_PREP(IRDMA_UDAQPC_VLANTAG, info->vlan_tag);

	qw2 = FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ARPINDEX, info->dst_arpindex) |
	      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_FLOWLABEL, info->flow_label) |
	      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_HOPLIMIT, info->hop_ttl) |
	      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_PDINDEXHI, info->pd_idx >> 16);

	if (!info->ipv4_valid) {
		set_64bit_val(wqe, 40,
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR0, info->dest_ip_addr[0]) |
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR1, info->dest_ip_addr[1]));
		set_64bit_val(wqe, 32,
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR2, info->dest_ip_addr[2]) |
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR3, info->dest_ip_addr[3]));

		set_64bit_val(wqe, 56,
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR0, info->src_ip_addr[0]) |
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR1, info->src_ip_addr[1]));
		set_64bit_val(wqe, 48,
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR2, info->src_ip_addr[2]) |
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR3, info->src_ip_addr[3]));
	} else {
		set_64bit_val(wqe, 32,
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR3, info->dest_ip_addr[0]));

		set_64bit_val(wqe, 48,
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR3, info->src_ip_addr[0]));
	}

	set_64bit_val(wqe, 8, qw1);
	set_64bit_val(wqe, 16, qw2);

	dma_wmb(); /* need write block before writing WQE header */

	set_64bit_val(
		wqe, 24,
		FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_WQEVALID, cqp->polarity) |
		FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_OPCODE, op) |
		FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_DOLOOPBACKK, info->do_lpbk) |
		FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_IPV4VALID, info->ipv4_valid) |
		FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_AVIDX, info->ah_idx) |
		FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_INSERTVLANTAG, info->insert_vlan_tag));

	print_hex_dump_debug("WQE: MANAGE_AH WQE", DUMP_PREFIX_OFFSET, 16, 8,
			     wqe, IRDMA_CQP_WQE_SIZE * 8, false);
	irdma_sc_cqp_post_sq(cqp);

	return 0;
}

/**
 * irdma_create_mg_ctx() - create a mcg context
 * @info: multicast group context info
 */
static enum irdma_status_code
irdma_create_mg_ctx(struct irdma_mcast_grp_info *info)
{
	struct irdma_mcast_grp_ctx_entry_info *entry_info = NULL;
	u8 idx = 0; /* index in the array */
	u8 ctx_idx = 0; /* index in the MG context */

	memset(info->dma_mem_mc.va, 0, IRDMA_MAX_MGS_PER_CTX * sizeof(u64));

	for (idx = 0; idx < IRDMA_MAX_MGS_PER_CTX; idx++) {
		entry_info = &info->mg_ctx_info[idx];
		if (entry_info->valid_entry) {
			set_64bit_val((__le64 *)info->dma_mem_mc.va,
				      ctx_idx * sizeof(u64),
				      FIELD_PREP(IRDMA_UDA_MGCTX_DESTPORT, entry_info->dest_port) |
				      FIELD_PREP(IRDMA_UDA_MGCTX_VALIDENT, entry_info->valid_entry) |
				      FIELD_PREP(IRDMA_UDA_MGCTX_QPID, entry_info->qp_id));
			ctx_idx++;
		}
	}

	return 0;
}

/**
 * irdma_access_mcast_grp() - Access mcast group based on op
 * @cqp: Control QP
 * @info: multicast group context info
 * @op: operation to perform
 * @scratch: u64 saved to be used during cqp completion
 */
enum irdma_status_code irdma_access_mcast_grp(struct irdma_sc_cqp *cqp,
					      struct irdma_mcast_grp_info *info,
					      u32 op, u64 scratch)
{
	__le64 *wqe;
	enum irdma_status_code ret_code = 0;

	if (info->mg_id >= IRDMA_UDA_MAX_FSI_MGS) {
		ibdev_dbg(to_ibdev(cqp->dev), "WQE: mg_id out of range\n");
		return IRDMA_ERR_PARAM;
	}

	wqe = irdma_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe) {
		ibdev_dbg(to_ibdev(cqp->dev), "WQE: ring full\n");
		return IRDMA_ERR_RING_FULL;
	}

	ret_code = irdma_create_mg_ctx(info);
	if (ret_code)
		return ret_code;

	set_64bit_val(wqe, 32, info->dma_mem_mc.pa);
	set_64bit_val(wqe, 16,
		      FIELD_PREP(IRDMA_UDA_CQPSQ_MG_VLANID, info->vlan_id) |
		      FIELD_PREP(IRDMA_UDA_CQPSQ_QS_HANDLE, info->qs_handle));
	set_64bit_val(wqe, 0, ether_addr_to_u64(info->dest_mac_addr));
	set_64bit_val(wqe, 8,
		      FIELD_PREP(IRDMA_UDA_CQPSQ_MG_HMC_FCN_ID, info->hmc_fcn_id));

	if (!info->ipv4_valid) {
		set_64bit_val(wqe, 56,
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR0, info->dest_ip_addr[0]) |
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR1, info->dest_ip_addr[1]));
		set_64bit_val(wqe, 48,
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR2, info->dest_ip_addr[2]) |
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR3, info->dest_ip_addr[3]));
	} else {
		set_64bit_val(wqe, 48,
			      FIELD_PREP(IRDMA_UDA_CQPSQ_MAV_ADDR3, info->dest_ip_addr[0]));
	}

	dma_wmb(); /* need write memory block before writing the WQE header. */

	set_64bit_val(wqe, 24,
		      FIELD_PREP(IRDMA_UDA_CQPSQ_MG_WQEVALID, cqp->polarity) |
		      FIELD_PREP(IRDMA_UDA_CQPSQ_MG_OPCODE, op) |
		      FIELD_PREP(IRDMA_UDA_CQPSQ_MG_MGIDX, info->mg_id) |
		      FIELD_PREP(IRDMA_UDA_CQPSQ_MG_VLANVALID, info->vlan_valid) |
		      FIELD_PREP(IRDMA_UDA_CQPSQ_MG_IPV4VALID, info->ipv4_valid));

	print_hex_dump_debug("WQE: MANAGE_MCG WQE", DUMP_PREFIX_OFFSET, 16, 8,
			     wqe, IRDMA_CQP_WQE_SIZE * 8, false);
	print_hex_dump_debug("WQE: MCG_HOST CTX WQE", DUMP_PREFIX_OFFSET, 16,
			     8, info->dma_mem_mc.va,
			     IRDMA_MAX_MGS_PER_CTX * 8, false);
	irdma_sc_cqp_post_sq(cqp);

	return 0;
}

/**
 * irdma_compare_mgs - Compares two multicast group structures
 * @entry1: Multcast group info
 * @entry2: Multcast group info in context
 */
static bool irdma_compare_mgs(struct irdma_mcast_grp_ctx_entry_info *entry1,
			      struct irdma_mcast_grp_ctx_entry_info *entry2)
{
	if (entry1->dest_port == entry2->dest_port &&
	    entry1->qp_id == entry2->qp_id)
		return true;

	return false;
}

/**
 * irdma_sc_add_mcast_grp - Allocates mcast group entry in ctx
 * @ctx: Multcast group context
 * @mg: Multcast group info
 */
enum irdma_status_code irdma_sc_add_mcast_grp(struct irdma_mcast_grp_info *ctx,
					      struct irdma_mcast_grp_ctx_entry_info *mg)
{
	u32 idx;
	bool free_entry_found = false;
	u32 free_entry_idx = 0;

	/* find either an identical or a free entry for a multicast group */
	for (idx = 0; idx < IRDMA_MAX_MGS_PER_CTX; idx++) {
		if (ctx->mg_ctx_info[idx].valid_entry) {
			if (irdma_compare_mgs(&ctx->mg_ctx_info[idx], mg)) {
				ctx->mg_ctx_info[idx].use_cnt++;
				return 0;
			}
			continue;
		}
		if (!free_entry_found) {
			free_entry_found = true;
			free_entry_idx = idx;
		}
	}

	if (free_entry_found) {
		ctx->mg_ctx_info[free_entry_idx] = *mg;
		ctx->mg_ctx_info[free_entry_idx].valid_entry = true;
		ctx->mg_ctx_info[free_entry_idx].use_cnt = 1;
		ctx->no_of_mgs++;
		return 0;
	}

	return IRDMA_ERR_NO_MEMORY;
}

/**
 * irdma_sc_del_mcast_grp - Delete mcast group
 * @ctx: Multcast group context
 * @mg: Multcast group info
 *
 * Finds and removes a specific mulicast group from context, all
 * parameters must match to remove a multicast group.
 */
enum irdma_status_code irdma_sc_del_mcast_grp(struct irdma_mcast_grp_info *ctx,
					      struct irdma_mcast_grp_ctx_entry_info *mg)
{
	u32 idx;

	/* find an entry in multicast group context */
	for (idx = 0; idx < IRDMA_MAX_MGS_PER_CTX; idx++) {
		if (!ctx->mg_ctx_info[idx].valid_entry)
			continue;

		if (irdma_compare_mgs(mg, &ctx->mg_ctx_info[idx])) {
			ctx->mg_ctx_info[idx].use_cnt--;

			if (!ctx->mg_ctx_info[idx].use_cnt) {
				ctx->mg_ctx_info[idx].valid_entry = false;
				ctx->no_of_mgs--;
				/* Remove gap if element was not the last */
				if (idx != ctx->no_of_mgs &&
				    ctx->no_of_mgs > 0) {
					memcpy(&ctx->mg_ctx_info[idx],
					       &ctx->mg_ctx_info[ctx->no_of_mgs - 1],
					       sizeof(ctx->mg_ctx_info[idx]));
					ctx->mg_ctx_info[ctx->no_of_mgs - 1].valid_entry = false;
				}
			}

			return 0;
		}
	}

	return IRDMA_ERR_PARAM;
}
