// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2022 Linaro Ltd. */

#include <linux/types.h>

#include "../ipa.h"
#include "../ipa_reg.h"

static const u32 ipa_reg_comp_cfg_fmask[] = {
	[COMP_CFG_ENABLE]				= BIT(0),
	[GSI_SNOC_BYPASS_DIS]				= BIT(1),
	[GEN_QMB_0_SNOC_BYPASS_DIS]			= BIT(2),
	[GEN_QMB_1_SNOC_BYPASS_DIS]			= BIT(3),
	[IPA_DCMP_FAST_CLK_EN]				= BIT(4),
						/* Bits 5-31 reserved */
};

IPA_REG_FIELDS(COMP_CFG, comp_cfg, 0x0000003c);

IPA_REG(CLKON_CFG, clkon_cfg, 0x00000044);

IPA_REG(ROUTE, route, 0x00000048);

IPA_REG(SHARED_MEM_SIZE, shared_mem_size, 0x00000054);

IPA_REG(QSB_MAX_WRITES, qsb_max_writes, 0x00000074);

IPA_REG(QSB_MAX_READS, qsb_max_reads, 0x00000078);

IPA_REG(FILT_ROUT_HASH_EN, filt_rout_hash_en, 0x000008c);

IPA_REG(FILT_ROUT_HASH_FLUSH, filt_rout_hash_flush, 0x0000090);

/* Valid bits defined by ipa->available */
IPA_REG(STATE_AGGR_ACTIVE, state_aggr_active, 0x0000010c);

IPA_REG(IPA_BCR, ipa_bcr, 0x000001d0);

/* Offset must be a multiple of 8 */
IPA_REG(LOCAL_PKT_PROC_CNTXT, local_pkt_proc_cntxt, 0x000001e8);

/* Valid bits defined by ipa->available */
IPA_REG(AGGR_FORCE_CLOSE, aggr_force_close, 0x000001ec);

IPA_REG(COUNTER_CFG, counter_cfg, 0x000001f0);

IPA_REG(IPA_TX_CFG, ipa_tx_cfg, 0x000001fc);

IPA_REG(FLAVOR_0, flavor_0, 0x00000210);

IPA_REG(IDLE_INDICATION_CFG, idle_indication_cfg, 0x00000220);

IPA_REG_STRIDE(SRC_RSRC_GRP_01_RSRC_TYPE, src_rsrc_grp_01_rsrc_type,
	       0x00000400, 0x0020);

IPA_REG_STRIDE(SRC_RSRC_GRP_23_RSRC_TYPE, src_rsrc_grp_23_rsrc_type,
	       0x00000404, 0x0020);

IPA_REG_STRIDE(DST_RSRC_GRP_01_RSRC_TYPE, dst_rsrc_grp_01_rsrc_type,
	       0x00000500, 0x0020);

IPA_REG_STRIDE(DST_RSRC_GRP_23_RSRC_TYPE, dst_rsrc_grp_23_rsrc_type,
	       0x00000504, 0x0020);

IPA_REG_STRIDE(ENDP_INIT_CTRL, endp_init_ctrl, 0x00000800, 0x0070);

IPA_REG_STRIDE(ENDP_INIT_CFG, endp_init_cfg, 0x00000808, 0x0070);

IPA_REG_STRIDE(ENDP_INIT_NAT, endp_init_nat, 0x0000080c, 0x0070);

IPA_REG_STRIDE(ENDP_INIT_HDR, endp_init_hdr, 0x00000810, 0x0070);

IPA_REG_STRIDE(ENDP_INIT_HDR_EXT, endp_init_hdr_ext, 0x00000814, 0x0070);

IPA_REG_STRIDE(ENDP_INIT_HDR_METADATA_MASK, endp_init_hdr_metadata_mask,
	       0x00000818, 0x0070);

IPA_REG_STRIDE(ENDP_INIT_MODE, endp_init_mode, 0x00000820, 0x0070);

IPA_REG_STRIDE(ENDP_INIT_AGGR, endp_init_aggr, 0x00000824, 0x0070);

IPA_REG_STRIDE(ENDP_INIT_HOL_BLOCK_EN, endp_init_hol_block_en,
	       0x0000082c, 0x0070);

IPA_REG_STRIDE(ENDP_INIT_HOL_BLOCK_TIMER, endp_init_hol_block_timer,
	       0x00000830, 0x0070);

IPA_REG_STRIDE(ENDP_INIT_DEAGGR, endp_init_deaggr, 0x00000834, 0x0070);

IPA_REG_STRIDE(ENDP_INIT_RSRC_GRP, endp_init_rsrc_grp, 0x00000838, 0x0070);

IPA_REG_STRIDE(ENDP_INIT_SEQ, endp_init_seq, 0x0000083c, 0x0070);

IPA_REG_STRIDE(ENDP_STATUS, endp_status, 0x00000840, 0x0070);

IPA_REG_STRIDE(ENDP_FILTER_ROUTER_HSH_CFG, endp_filter_router_hsh_cfg,
	       0x0000085c, 0x0070);

/* Valid bits defined by enum ipa_irq_id; only used for GSI_EE_AP */
IPA_REG(IPA_IRQ_STTS, ipa_irq_stts, 0x00003008 + 0x1000 * GSI_EE_AP);

/* Valid bits defined by enum ipa_irq_id; only used for GSI_EE_AP */
IPA_REG(IPA_IRQ_EN, ipa_irq_en, 0x0000300c + 0x1000 * GSI_EE_AP);

/* Valid bits defined by enum ipa_irq_id; only used for GSI_EE_AP */
IPA_REG(IPA_IRQ_CLR, ipa_irq_clr, 0x00003010 + 0x1000 * GSI_EE_AP);

IPA_REG(IPA_IRQ_UC, ipa_irq_uc, 0x0000301c + 0x1000 * GSI_EE_AP);

/* Valid bits defined by ipa->available */
IPA_REG(IRQ_SUSPEND_INFO, irq_suspend_info, 0x00003030 + 0x1000 * GSI_EE_AP);

/* Valid bits defined by ipa->available */
IPA_REG(IRQ_SUSPEND_EN, irq_suspend_en, 0x00003034 + 0x1000 * GSI_EE_AP);

/* Valid bits defined by ipa->available */
IPA_REG(IRQ_SUSPEND_CLR, irq_suspend_clr, 0x00003038 + 0x1000 * GSI_EE_AP);

static const struct ipa_reg *ipa_reg_array[] = {
	[COMP_CFG]			= &ipa_reg_comp_cfg,
	[CLKON_CFG]			= &ipa_reg_clkon_cfg,
	[ROUTE]				= &ipa_reg_route,
	[SHARED_MEM_SIZE]		= &ipa_reg_shared_mem_size,
	[QSB_MAX_WRITES]		= &ipa_reg_qsb_max_writes,
	[QSB_MAX_READS]			= &ipa_reg_qsb_max_reads,
	[FILT_ROUT_HASH_EN]		= &ipa_reg_filt_rout_hash_en,
	[FILT_ROUT_HASH_FLUSH]		= &ipa_reg_filt_rout_hash_flush,
	[STATE_AGGR_ACTIVE]		= &ipa_reg_state_aggr_active,
	[IPA_BCR]			= &ipa_reg_ipa_bcr,
	[LOCAL_PKT_PROC_CNTXT]		= &ipa_reg_local_pkt_proc_cntxt,
	[AGGR_FORCE_CLOSE]		= &ipa_reg_aggr_force_close,
	[COUNTER_CFG]			= &ipa_reg_counter_cfg,
	[IPA_TX_CFG]			= &ipa_reg_ipa_tx_cfg,
	[FLAVOR_0]			= &ipa_reg_flavor_0,
	[IDLE_INDICATION_CFG]		= &ipa_reg_idle_indication_cfg,
	[SRC_RSRC_GRP_01_RSRC_TYPE]	= &ipa_reg_src_rsrc_grp_01_rsrc_type,
	[SRC_RSRC_GRP_23_RSRC_TYPE]	= &ipa_reg_src_rsrc_grp_23_rsrc_type,
	[DST_RSRC_GRP_01_RSRC_TYPE]	= &ipa_reg_dst_rsrc_grp_01_rsrc_type,
	[DST_RSRC_GRP_23_RSRC_TYPE]	= &ipa_reg_dst_rsrc_grp_23_rsrc_type,
	[ENDP_INIT_CTRL]		= &ipa_reg_endp_init_ctrl,
	[ENDP_INIT_CFG]			= &ipa_reg_endp_init_cfg,
	[ENDP_INIT_NAT]			= &ipa_reg_endp_init_nat,
	[ENDP_INIT_HDR]			= &ipa_reg_endp_init_hdr,
	[ENDP_INIT_HDR_EXT]		= &ipa_reg_endp_init_hdr_ext,
	[ENDP_INIT_HDR_METADATA_MASK]	= &ipa_reg_endp_init_hdr_metadata_mask,
	[ENDP_INIT_MODE]		= &ipa_reg_endp_init_mode,
	[ENDP_INIT_AGGR]		= &ipa_reg_endp_init_aggr,
	[ENDP_INIT_HOL_BLOCK_EN]	= &ipa_reg_endp_init_hol_block_en,
	[ENDP_INIT_HOL_BLOCK_TIMER]	= &ipa_reg_endp_init_hol_block_timer,
	[ENDP_INIT_DEAGGR]		= &ipa_reg_endp_init_deaggr,
	[ENDP_INIT_RSRC_GRP]		= &ipa_reg_endp_init_rsrc_grp,
	[ENDP_INIT_SEQ]			= &ipa_reg_endp_init_seq,
	[ENDP_STATUS]			= &ipa_reg_endp_status,
	[ENDP_FILTER_ROUTER_HSH_CFG]	= &ipa_reg_endp_filter_router_hsh_cfg,
	[IPA_IRQ_STTS]			= &ipa_reg_ipa_irq_stts,
	[IPA_IRQ_EN]			= &ipa_reg_ipa_irq_en,
	[IPA_IRQ_CLR]			= &ipa_reg_ipa_irq_clr,
	[IPA_IRQ_UC]			= &ipa_reg_ipa_irq_uc,
	[IRQ_SUSPEND_INFO]		= &ipa_reg_irq_suspend_info,
	[IRQ_SUSPEND_EN]		= &ipa_reg_irq_suspend_en,
	[IRQ_SUSPEND_CLR]		= &ipa_reg_irq_suspend_clr,
};

const struct ipa_regs ipa_regs_v3_5_1 = {
	.reg_count	= ARRAY_SIZE(ipa_reg_array),
	.reg		= ipa_reg_array,
};
