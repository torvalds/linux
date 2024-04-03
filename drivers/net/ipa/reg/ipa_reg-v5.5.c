// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2023 Linaro Ltd. */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bits.h>

#include "../ipa_reg.h"
#include "../ipa_version.h"

static const u32 reg_flavor_0_fmask[] = {
	[MAX_PIPES]					= GENMASK(7, 0),
	[MAX_CONS_PIPES]				= GENMASK(15, 8),
	[MAX_PROD_PIPES]				= GENMASK(23, 16),
	[PROD_LOWEST]					= GENMASK(31, 24),
};

REG_FIELDS(FLAVOR_0, flavor_0, 0x00000000);

static const u32 reg_comp_cfg_fmask[] = {
	[RAM_ARB_PRI_CLIENT_SAMP_FIX_DIS]		= BIT(0),
	[GSI_SNOC_BYPASS_DIS]				= BIT(1),
	[GEN_QMB_0_SNOC_BYPASS_DIS]			= BIT(2),
	[GEN_QMB_1_SNOC_BYPASS_DIS]			= BIT(3),
						/* Bit 4 reserved */
	[IPA_QMB_SELECT_CONS_EN]			= BIT(5),
	[IPA_QMB_SELECT_PROD_EN]			= BIT(6),
	[GSI_MULTI_INORDER_RD_DIS]			= BIT(7),
	[GSI_MULTI_INORDER_WR_DIS]			= BIT(8),
	[GEN_QMB_0_MULTI_INORDER_RD_DIS]		= BIT(9),
	[GEN_QMB_1_MULTI_INORDER_RD_DIS]		= BIT(10),
	[GEN_QMB_0_MULTI_INORDER_WR_DIS]		= BIT(11),
	[GEN_QMB_1_MULTI_INORDER_WR_DIS]		= BIT(12),
	[GEN_QMB_0_SNOC_CNOC_LOOP_PROT_DIS]		= BIT(13),
	[GSI_SNOC_CNOC_LOOP_PROT_DISABLE]		= BIT(14),
	[GSI_MULTI_AXI_MASTERS_DIS]			= BIT(15),
	[IPA_QMB_SELECT_GLOBAL_EN]			= BIT(16),
						/* Bits 17-18 reserved */
	[QMB_RAM_RD_CACHE_DISABLE]			= BIT(19),
	[GENQMB_AOOOWR]					= BIT(20),
	[IF_OUT_OF_BUF_STOP_RESET_MASK_EN]		= BIT(21),
	[ATOMIC_FETCHER_ARB_LOCK_DIS]			= GENMASK(27, 22),
						/* Bits 28-29 reserved */
	[GEN_QMB_1_DYNAMIC_ASIZE]			= BIT(30),
	[GEN_QMB_0_DYNAMIC_ASIZE]			= BIT(31),
};

REG_FIELDS(COMP_CFG, comp_cfg, 0x00000048);

static const u32 reg_clkon_cfg_fmask[] = {
	[CLKON_RX]					= BIT(0),
	[CLKON_PROC]					= BIT(1),
	[TX_WRAPPER]					= BIT(2),
	[CLKON_MISC]					= BIT(3),
	[RAM_ARB]					= BIT(4),
	[FTCH_HPS]					= BIT(5),
	[FTCH_DPS]					= BIT(6),
	[CLKON_HPS]					= BIT(7),
	[CLKON_DPS]					= BIT(8),
	[RX_HPS_CMDQS]					= BIT(9),
	[HPS_DPS_CMDQS]					= BIT(10),
	[DPS_TX_CMDQS]					= BIT(11),
	[RSRC_MNGR]					= BIT(12),
	[CTX_HANDLER]					= BIT(13),
	[ACK_MNGR]					= BIT(14),
	[D_DCPH]					= BIT(15),
	[H_DCPH]					= BIT(16),
						/* Bit 17 reserved */
	[NTF_TX_CMDQS]					= BIT(18),
	[CLKON_TX_0]					= BIT(19),
	[CLKON_TX_1]					= BIT(20),
	[CLKON_FNR]					= BIT(21),
	[QSB2AXI_CMDQ_L]				= BIT(22),
	[AGGR_WRAPPER]					= BIT(23),
	[RAM_SLAVEWAY]					= BIT(24),
	[CLKON_QMB]					= BIT(25),
	[WEIGHT_ARB]					= BIT(26),
	[GSI_IF]					= BIT(27),
	[CLKON_GLOBAL]					= BIT(28),
	[GLOBAL_2X_CLK]					= BIT(29),
	[DPL_FIFO]					= BIT(30),
	[DRBIP]						= BIT(31),
};

REG_FIELDS(CLKON_CFG, clkon_cfg, 0x00000050);

static const u32 reg_route_fmask[] = {
	[ROUTE_DEF_PIPE]				= GENMASK(7, 0),
	[ROUTE_FRAG_DEF_PIPE]				= GENMASK(15, 8),
	[ROUTE_DEF_HDR_OFST]				= GENMASK(25, 16),
	[ROUTE_DEF_HDR_TABLE]				= BIT(26),
	[ROUTE_DEF_RETAIN_HDR]				= BIT(27),
	[ROUTE_DIS]					= BIT(28),
						/* Bits 29-31 reserved */
};

REG_FIELDS(ROUTE, route, 0x00000054);

static const u32 reg_shared_mem_size_fmask[] = {
	[MEM_SIZE]					= GENMASK(15, 0),
	[MEM_BADDR]					= GENMASK(31, 16),
};

REG_FIELDS(SHARED_MEM_SIZE, shared_mem_size, 0x0000005c);

static const u32 reg_qsb_max_writes_fmask[] = {
	[GEN_QMB_0_MAX_WRITES]				= GENMASK(3, 0),
	[GEN_QMB_1_MAX_WRITES]				= GENMASK(7, 4),
						/* Bits 8-31 reserved */
};

REG_FIELDS(QSB_MAX_WRITES, qsb_max_writes, 0x00000070);

static const u32 reg_qsb_max_reads_fmask[] = {
	[GEN_QMB_0_MAX_READS]				= GENMASK(3, 0),
	[GEN_QMB_1_MAX_READS]				= GENMASK(7, 4),
						/* Bits 8-15 reserved */
	[GEN_QMB_0_MAX_READS_BEATS]			= GENMASK(23, 16),
	[GEN_QMB_1_MAX_READS_BEATS]			= GENMASK(31, 24),
};

REG_FIELDS(QSB_MAX_READS, qsb_max_reads, 0x00000074);

/* Valid bits defined by ipa->available */

REG_STRIDE(STATE_AGGR_ACTIVE, state_aggr_active, 0x00000120, 0x0004);

static const u32 reg_filt_rout_cache_flush_fmask[] = {
	[ROUTER_CACHE]					= BIT(0),
						/* Bits 1-3 reserved */
	[FILTER_CACHE]					= BIT(4),
						/* Bits 5-31 reserved */
};

REG_FIELDS(FILT_ROUT_CACHE_FLUSH, filt_rout_cache_flush, 0x0000404);

static const u32 reg_local_pkt_proc_cntxt_fmask[] = {
	[IPA_BASE_ADDR]					= GENMASK(17, 0),
						/* Bits 18-31 reserved */
};

/* Offset must be a multiple of 8 */
REG_FIELDS(LOCAL_PKT_PROC_CNTXT, local_pkt_proc_cntxt, 0x00000478);

static const u32 reg_ipa_tx_cfg_fmask[] = {
						/* Bits 0-1 reserved */
	[PREFETCH_ALMOST_EMPTY_SIZE_TX0]		= GENMASK(5, 2),
	[DMAW_SCND_OUTSD_PRED_THRESHOLD]		= GENMASK(9, 6),
	[DMAW_SCND_OUTSD_PRED_EN]			= BIT(10),
	[DMAW_MAX_BEATS_256_DIS]			= BIT(11),
	[PA_MASK_EN]					= BIT(12),
	[PREFETCH_ALMOST_EMPTY_SIZE_TX1]		= GENMASK(16, 13),
	[DUAL_TX_ENABLE]				= BIT(17),
	[SSPND_PA_NO_START_STATE]			= BIT(18),
						/* Bit 19 reserved */
	[HOLB_STICKY_DROP_EN]				= BIT(20),
						/* Bits 21-31 reserved */
};

REG_FIELDS(IPA_TX_CFG, ipa_tx_cfg, 0x00000488);

static const u32 reg_idle_indication_cfg_fmask[] = {
	[ENTER_IDLE_DEBOUNCE_THRESH]			= GENMASK(15, 0),
	[CONST_NON_IDLE_ENABLE]				= BIT(16),
						/* Bits 17-31 reserved */
};

REG_FIELDS(IDLE_INDICATION_CFG, idle_indication_cfg, 0x000004a8);

static const u32 reg_qtime_timestamp_cfg_fmask[] = {
						/* Bits 0-7 reserved */
	[TAG_TIMESTAMP_LSB]				= GENMASK(12, 8),
						/* Bits 13-15 reserved */
	[NAT_TIMESTAMP_LSB]				= GENMASK(20, 16),
						/* Bits 21-31 reserved */
};

REG_FIELDS(QTIME_TIMESTAMP_CFG, qtime_timestamp_cfg, 0x000004ac);

static const u32 reg_timers_xo_clk_div_cfg_fmask[] = {
	[DIV_VALUE]					= GENMASK(8, 0),
						/* Bits 9-30 reserved */
	[DIV_ENABLE]					= BIT(31),
};

REG_FIELDS(TIMERS_XO_CLK_DIV_CFG, timers_xo_clk_div_cfg, 0x000004b0);

static const u32 reg_timers_pulse_gran_cfg_fmask[] = {
	[PULSE_GRAN_0]					= GENMASK(2, 0),
	[PULSE_GRAN_1]					= GENMASK(5, 3),
	[PULSE_GRAN_2]					= GENMASK(8, 6),
	[PULSE_GRAN_3]					= GENMASK(11, 9),
						/* Bits 12-31 reserved */
};

REG_FIELDS(TIMERS_PULSE_GRAN_CFG, timers_pulse_gran_cfg, 0x000004b4);

static const u32 reg_src_rsrc_grp_01_rsrc_type_fmask[] = {
	[X_MIN_LIM]					= GENMASK(5, 0),
						/* Bits 6-7 reserved */
	[X_MAX_LIM]					= GENMASK(13, 8),
						/* Bits 14-15 reserved */
	[Y_MIN_LIM]					= GENMASK(21, 16),
						/* Bits 22-23 reserved */
	[Y_MAX_LIM]					= GENMASK(29, 24),
						/* Bits 30-31 reserved */
};

REG_STRIDE_FIELDS(SRC_RSRC_GRP_01_RSRC_TYPE, src_rsrc_grp_01_rsrc_type,
		  0x00000500, 0x0020);

static const u32 reg_src_rsrc_grp_23_rsrc_type_fmask[] = {
	[X_MIN_LIM]					= GENMASK(5, 0),
						/* Bits 6-7 reserved */
	[X_MAX_LIM]					= GENMASK(13, 8),
						/* Bits 14-15 reserved */
	[Y_MIN_LIM]					= GENMASK(21, 16),
						/* Bits 22-23 reserved */
	[Y_MAX_LIM]					= GENMASK(29, 24),
						/* Bits 30-31 reserved */
};

REG_STRIDE_FIELDS(SRC_RSRC_GRP_23_RSRC_TYPE, src_rsrc_grp_23_rsrc_type,
		  0x00000504, 0x0020);

static const u32 reg_src_rsrc_grp_45_rsrc_type_fmask[] = {
	[X_MIN_LIM]					= GENMASK(5, 0),
						/* Bits 6-7 reserved */
	[X_MAX_LIM]					= GENMASK(13, 8),
						/* Bits 14-15 reserved */
	[Y_MIN_LIM]					= GENMASK(21, 16),
						/* Bits 22-23 reserved */
	[Y_MAX_LIM]					= GENMASK(29, 24),
						/* Bits 30-31 reserved */
};

REG_STRIDE_FIELDS(SRC_RSRC_GRP_45_RSRC_TYPE, src_rsrc_grp_45_rsrc_type,
		  0x00000508, 0x0020);

static const u32 reg_src_rsrc_grp_67_rsrc_type_fmask[] = {
	[X_MIN_LIM]					= GENMASK(5, 0),
						/* Bits 6-7 reserved */
	[X_MAX_LIM]					= GENMASK(13, 8),
						/* Bits 14-15 reserved */
	[Y_MIN_LIM]					= GENMASK(21, 16),
						/* Bits 22-23 reserved */
	[Y_MAX_LIM]					= GENMASK(29, 24),
						/* Bits 30-31 reserved */
};

REG_STRIDE_FIELDS(SRC_RSRC_GRP_67_RSRC_TYPE, src_rsrc_grp_67_rsrc_type,
		  0x0000050c, 0x0020);

static const u32 reg_dst_rsrc_grp_01_rsrc_type_fmask[] = {
	[X_MIN_LIM]					= GENMASK(5, 0),
						/* Bits 6-7 reserved */
	[X_MAX_LIM]					= GENMASK(13, 8),
						/* Bits 14-15 reserved */
	[Y_MIN_LIM]					= GENMASK(21, 16),
						/* Bits 22-23 reserved */
	[Y_MAX_LIM]					= GENMASK(29, 24),
						/* Bits 30-31 reserved */
};

REG_STRIDE_FIELDS(DST_RSRC_GRP_01_RSRC_TYPE, dst_rsrc_grp_01_rsrc_type,
		  0x00000600, 0x0020);

static const u32 reg_dst_rsrc_grp_23_rsrc_type_fmask[] = {
	[X_MIN_LIM]					= GENMASK(5, 0),
						/* Bits 6-7 reserved */
	[X_MAX_LIM]					= GENMASK(13, 8),
						/* Bits 14-15 reserved */
	[Y_MIN_LIM]					= GENMASK(21, 16),
						/* Bits 22-23 reserved */
	[Y_MAX_LIM]					= GENMASK(29, 24),
						/* Bits 30-31 reserved */
};

REG_STRIDE_FIELDS(DST_RSRC_GRP_23_RSRC_TYPE, dst_rsrc_grp_23_rsrc_type,
		  0x00000604, 0x0020);

static const u32 reg_dst_rsrc_grp_45_rsrc_type_fmask[] = {
	[X_MIN_LIM]					= GENMASK(5, 0),
						/* Bits 6-7 reserved */
	[X_MAX_LIM]					= GENMASK(13, 8),
						/* Bits 14-15 reserved */
	[Y_MIN_LIM]					= GENMASK(21, 16),
						/* Bits 22-23 reserved */
	[Y_MAX_LIM]					= GENMASK(29, 24),
						/* Bits 30-31 reserved */
};

REG_STRIDE_FIELDS(DST_RSRC_GRP_45_RSRC_TYPE, dst_rsrc_grp_45_rsrc_type,
		  0x00000608, 0x0020);

static const u32 reg_dst_rsrc_grp_67_rsrc_type_fmask[] = {
	[X_MIN_LIM]					= GENMASK(5, 0),
						/* Bits 6-7 reserved */
	[X_MAX_LIM]					= GENMASK(13, 8),
						/* Bits 14-15 reserved */
	[Y_MIN_LIM]					= GENMASK(21, 16),
						/* Bits 22-23 reserved */
	[Y_MAX_LIM]					= GENMASK(29, 24),
						/* Bits 30-31 reserved */
};

REG_STRIDE_FIELDS(DST_RSRC_GRP_67_RSRC_TYPE, dst_rsrc_grp_67_rsrc_type,
		  0x0000060c, 0x0020);

/* Valid bits defined by ipa->available */

REG_STRIDE(AGGR_FORCE_CLOSE, aggr_force_close, 0x000006b0, 0x0004);

static const u32 reg_endp_init_cfg_fmask[] = {
	[FRAG_OFFLOAD_EN]				= BIT(0),
	[CS_OFFLOAD_EN]					= GENMASK(2, 1),
	[CS_METADATA_HDR_OFFSET]			= GENMASK(6, 3),
						/* Bit 7 reserved */
	[CS_GEN_QMB_MASTER_SEL]				= BIT(8),
	[PIPE_REPLICATE_EN]				= BIT(9),
						/* Bits 10-31 reserved */
};

REG_STRIDE_FIELDS(ENDP_INIT_CFG, endp_init_cfg, 0x00001008, 0x0080);

static const u32 reg_endp_init_nat_fmask[] = {
	[NAT_EN]					= GENMASK(1, 0),
						/* Bits 2-31 reserved */
};

REG_STRIDE_FIELDS(ENDP_INIT_NAT, endp_init_nat, 0x0000100c, 0x0080);

static const u32 reg_endp_init_hdr_fmask[] = {
	[HDR_LEN]					= GENMASK(5, 0),
	[HDR_OFST_METADATA_VALID]			= BIT(6),
	[HDR_OFST_METADATA]				= GENMASK(12, 7),
	[HDR_ADDITIONAL_CONST_LEN]			= GENMASK(18, 13),
	[HDR_OFST_PKT_SIZE_VALID]			= BIT(19),
	[HDR_OFST_PKT_SIZE]				= GENMASK(25, 20),
						/* Bit 26 reserved */
	[HDR_LEN_INC_DEAGG_HDR]				= BIT(27),
	[HDR_LEN_MSB]					= GENMASK(29, 28),
	[HDR_OFST_METADATA_MSB]				= GENMASK(31, 30),
};

REG_STRIDE_FIELDS(ENDP_INIT_HDR, endp_init_hdr, 0x00001010, 0x0080);

static const u32 reg_endp_init_hdr_ext_fmask[] = {
	[HDR_ENDIANNESS]				= BIT(0),
	[HDR_TOTAL_LEN_OR_PAD_VALID]			= BIT(1),
	[HDR_TOTAL_LEN_OR_PAD]				= BIT(2),
	[HDR_PAYLOAD_LEN_INC_PADDING]			= BIT(3),
	[HDR_TOTAL_LEN_OR_PAD_OFFSET]			= GENMASK(9, 4),
	[HDR_PAD_TO_ALIGNMENT]				= GENMASK(13, 10),
						/* Bits 14-15 reserved */
	[HDR_TOTAL_LEN_OR_PAD_OFFSET_MSB]		= GENMASK(17, 16),
	[HDR_OFST_PKT_SIZE_MSB]				= GENMASK(19, 18),
	[HDR_ADDITIONAL_CONST_LEN_MSB]			= GENMASK(21, 20),
	[HDR_BYTES_TO_REMOVE_VALID]			= BIT(22),
						/* Bit 23 reserved */
	[HDR_BYTES_TO_REMOVE]				= GENMASK(31, 24),
};

REG_STRIDE_FIELDS(ENDP_INIT_HDR_EXT, endp_init_hdr_ext, 0x00001014, 0x0080);

REG_STRIDE(ENDP_INIT_HDR_METADATA_MASK, endp_init_hdr_metadata_mask,
	   0x00001018, 0x0080);

static const u32 reg_endp_init_mode_fmask[] = {
	[ENDP_MODE]					= GENMASK(2, 0),
	[DCPH_ENABLE]					= BIT(3),
	[DEST_PIPE_INDEX]				= GENMASK(11, 4),
	[BYTE_THRESHOLD]				= GENMASK(27, 12),
						/* Bit 28 reserved */
	[PAD_EN]					= BIT(29),
	[DRBIP_ACL_ENABLE]				= BIT(30),
						/* Bit 31 reserved */
};

REG_STRIDE_FIELDS(ENDP_INIT_MODE, endp_init_mode, 0x00001020, 0x0080);

static const u32 reg_endp_init_aggr_fmask[] = {
	[AGGR_EN]					= GENMASK(1, 0),
	[AGGR_TYPE]					= GENMASK(4, 2),
	[BYTE_LIMIT]					= GENMASK(10, 5),
						/* Bit 11 reserved */
	[TIME_LIMIT]					= GENMASK(16, 12),
	[PKT_LIMIT]					= GENMASK(22, 17),
	[SW_EOF_ACTIVE]					= BIT(23),
	[FORCE_CLOSE]					= BIT(24),
						/* Bit 25 reserved */
	[HARD_BYTE_LIMIT_EN]				= BIT(26),
	[AGGR_GRAN_SEL]					= BIT(27),
	[AGGR_COAL_L2]					= BIT(28),
						/* Bits 27-31 reserved */
};

REG_STRIDE_FIELDS(ENDP_INIT_AGGR, endp_init_aggr, 0x00001024, 0x0080);

static const u32 reg_endp_init_hol_block_en_fmask[] = {
	[HOL_BLOCK_EN]					= BIT(0),
						/* Bits 1-31 reserved */
};

REG_STRIDE_FIELDS(ENDP_INIT_HOL_BLOCK_EN, endp_init_hol_block_en,
		  0x0000102c, 0x0080);

static const u32 reg_endp_init_hol_block_timer_fmask[] = {
	[TIMER_LIMIT]					= GENMASK(4, 0),
						/* Bits 5-7 reserved */
	[TIMER_GRAN_SEL]				= GENMASK(9, 8),
						/* Bits 10-31 reserved */
};

REG_STRIDE_FIELDS(ENDP_INIT_HOL_BLOCK_TIMER, endp_init_hol_block_timer,
		  0x00001030, 0x0080);

static const u32 reg_endp_init_deaggr_fmask[] = {
	[DEAGGR_HDR_LEN]				= GENMASK(5, 0),
	[SYSPIPE_ERR_DETECTION]				= BIT(6),
	[PACKET_OFFSET_VALID]				= BIT(7),
	[PACKET_OFFSET_LOCATION]			= GENMASK(13, 8),
	[IGNORE_MIN_PKT_ERR]				= BIT(14),
						/* Bit 15 reserved */
	[MAX_PACKET_LEN]				= GENMASK(31, 16),
};

REG_STRIDE_FIELDS(ENDP_INIT_DEAGGR, endp_init_deaggr, 0x00001034, 0x0080);

static const u32 reg_endp_init_rsrc_grp_fmask[] = {
	[ENDP_RSRC_GRP]					= GENMASK(2, 0),
						/* Bits 3-31 reserved */
};

REG_STRIDE_FIELDS(ENDP_INIT_RSRC_GRP, endp_init_rsrc_grp, 0x00001038, 0x0080);

static const u32 reg_endp_init_seq_fmask[] = {
	[SEQ_TYPE]					= GENMASK(7, 0),
						/* Bits 8-31 reserved */
};

REG_STRIDE_FIELDS(ENDP_INIT_SEQ, endp_init_seq, 0x0000103c, 0x0080);

static const u32 reg_endp_status_fmask[] = {
	[STATUS_EN]					= BIT(0),
	[STATUS_ENDP]					= GENMASK(8, 1),
	[STATUS_PKT_SUPPRESS]				= BIT(9),
						/* Bits 10-31 reserved */
};

REG_STRIDE_FIELDS(ENDP_STATUS, endp_status, 0x00001040, 0x0080);

static const u32 reg_endp_filter_cache_cfg_fmask[] = {
	[CACHE_MSK_SRC_ID]				= BIT(0),
	[CACHE_MSK_SRC_IP]				= BIT(1),
	[CACHE_MSK_DST_IP]				= BIT(2),
	[CACHE_MSK_SRC_PORT]				= BIT(3),
	[CACHE_MSK_DST_PORT]				= BIT(4),
	[CACHE_MSK_PROTOCOL]				= BIT(5),
	[CACHE_MSK_METADATA]				= BIT(6),
						/* Bits 7-31 reserved */
};

REG_STRIDE_FIELDS(ENDP_FILTER_CACHE_CFG, endp_filter_cache_cfg,
		  0x0000105c, 0x0080);

static const u32 reg_endp_router_cache_cfg_fmask[] = {
	[CACHE_MSK_SRC_ID]				= BIT(0),
	[CACHE_MSK_SRC_IP]				= BIT(1),
	[CACHE_MSK_DST_IP]				= BIT(2),
	[CACHE_MSK_SRC_PORT]				= BIT(3),
	[CACHE_MSK_DST_PORT]				= BIT(4),
	[CACHE_MSK_PROTOCOL]				= BIT(5),
	[CACHE_MSK_METADATA]				= BIT(6),
						/* Bits 7-31 reserved */
};

REG_STRIDE_FIELDS(ENDP_ROUTER_CACHE_CFG, endp_router_cache_cfg,
		  0x00001060, 0x0080);

/* Valid bits defined by enum ipa_irq_id; only used for GSI_EE_AP */
REG(IPA_IRQ_STTS, ipa_irq_stts, 0x0000c008 + 0x1000 * GSI_EE_AP);

/* Valid bits defined by enum ipa_irq_id; only used for GSI_EE_AP */
REG(IPA_IRQ_EN, ipa_irq_en, 0x0000c00c + 0x1000 * GSI_EE_AP);

/* Valid bits defined by enum ipa_irq_id; only used for GSI_EE_AP */
REG(IPA_IRQ_CLR, ipa_irq_clr, 0x0000c010 + 0x1000 * GSI_EE_AP);

static const u32 reg_ipa_irq_uc_fmask[] = {
	[UC_INTR]					= BIT(0),
						/* Bits 1-31 reserved */
};

REG_FIELDS(IPA_IRQ_UC, ipa_irq_uc, 0x0000c01c + 0x1000 * GSI_EE_AP);

/* Valid bits defined by ipa->available */

REG_STRIDE(IRQ_SUSPEND_INFO, irq_suspend_info,
	   0x0000c030 + 0x1000 * GSI_EE_AP, 0x0004);

/* Valid bits defined by ipa->available */

REG_STRIDE(IRQ_SUSPEND_EN, irq_suspend_en,
	   0x0000c050 + 0x1000 * GSI_EE_AP, 0x0004);

/* Valid bits defined by ipa->available */

REG_STRIDE(IRQ_SUSPEND_CLR, irq_suspend_clr,
	   0x0000c070 + 0x1000 * GSI_EE_AP, 0x0004);

static const struct reg *reg_array[] = {
	[COMP_CFG]			= &reg_comp_cfg,
	[CLKON_CFG]			= &reg_clkon_cfg,
	[ROUTE]				= &reg_route,
	[SHARED_MEM_SIZE]		= &reg_shared_mem_size,
	[QSB_MAX_WRITES]		= &reg_qsb_max_writes,
	[QSB_MAX_READS]			= &reg_qsb_max_reads,
	[FILT_ROUT_CACHE_FLUSH]		= &reg_filt_rout_cache_flush,
	[STATE_AGGR_ACTIVE]		= &reg_state_aggr_active,
	[LOCAL_PKT_PROC_CNTXT]		= &reg_local_pkt_proc_cntxt,
	[AGGR_FORCE_CLOSE]		= &reg_aggr_force_close,
	[IPA_TX_CFG]			= &reg_ipa_tx_cfg,
	[FLAVOR_0]			= &reg_flavor_0,
	[IDLE_INDICATION_CFG]		= &reg_idle_indication_cfg,
	[QTIME_TIMESTAMP_CFG]		= &reg_qtime_timestamp_cfg,
	[TIMERS_XO_CLK_DIV_CFG]		= &reg_timers_xo_clk_div_cfg,
	[TIMERS_PULSE_GRAN_CFG]		= &reg_timers_pulse_gran_cfg,
	[SRC_RSRC_GRP_01_RSRC_TYPE]	= &reg_src_rsrc_grp_01_rsrc_type,
	[SRC_RSRC_GRP_23_RSRC_TYPE]	= &reg_src_rsrc_grp_23_rsrc_type,
	[SRC_RSRC_GRP_45_RSRC_TYPE]	= &reg_src_rsrc_grp_45_rsrc_type,
	[SRC_RSRC_GRP_67_RSRC_TYPE]	= &reg_src_rsrc_grp_67_rsrc_type,
	[DST_RSRC_GRP_01_RSRC_TYPE]	= &reg_dst_rsrc_grp_01_rsrc_type,
	[DST_RSRC_GRP_23_RSRC_TYPE]	= &reg_dst_rsrc_grp_23_rsrc_type,
	[DST_RSRC_GRP_45_RSRC_TYPE]	= &reg_dst_rsrc_grp_45_rsrc_type,
	[DST_RSRC_GRP_67_RSRC_TYPE]	= &reg_dst_rsrc_grp_67_rsrc_type,
	[ENDP_INIT_CFG]			= &reg_endp_init_cfg,
	[ENDP_INIT_NAT]			= &reg_endp_init_nat,
	[ENDP_INIT_HDR]			= &reg_endp_init_hdr,
	[ENDP_INIT_HDR_EXT]		= &reg_endp_init_hdr_ext,
	[ENDP_INIT_HDR_METADATA_MASK]	= &reg_endp_init_hdr_metadata_mask,
	[ENDP_INIT_MODE]		= &reg_endp_init_mode,
	[ENDP_INIT_AGGR]		= &reg_endp_init_aggr,
	[ENDP_INIT_HOL_BLOCK_EN]	= &reg_endp_init_hol_block_en,
	[ENDP_INIT_HOL_BLOCK_TIMER]	= &reg_endp_init_hol_block_timer,
	[ENDP_INIT_DEAGGR]		= &reg_endp_init_deaggr,
	[ENDP_INIT_RSRC_GRP]		= &reg_endp_init_rsrc_grp,
	[ENDP_INIT_SEQ]			= &reg_endp_init_seq,
	[ENDP_STATUS]			= &reg_endp_status,
	[ENDP_FILTER_CACHE_CFG]		= &reg_endp_filter_cache_cfg,
	[ENDP_ROUTER_CACHE_CFG]		= &reg_endp_router_cache_cfg,
	[IPA_IRQ_STTS]			= &reg_ipa_irq_stts,
	[IPA_IRQ_EN]			= &reg_ipa_irq_en,
	[IPA_IRQ_CLR]			= &reg_ipa_irq_clr,
	[IPA_IRQ_UC]			= &reg_ipa_irq_uc,
	[IRQ_SUSPEND_INFO]		= &reg_irq_suspend_info,
	[IRQ_SUSPEND_EN]		= &reg_irq_suspend_en,
	[IRQ_SUSPEND_CLR]		= &reg_irq_suspend_clr,
};

const struct regs ipa_regs_v5_5 = {
	.reg_count	= ARRAY_SIZE(reg_array),
	.reg		= reg_array,
};
