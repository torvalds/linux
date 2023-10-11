// SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB
/* Copyright (c) 2015 - 2021 Intel Corporation */
#include "osdep.h"
#include "type.h"
#include "i40iw_hw.h"
#include "protos.h"

static u32 i40iw_regs[IRDMA_MAX_REGS] = {
	I40E_PFPE_CQPTAIL,
	I40E_PFPE_CQPDB,
	I40E_PFPE_CCQPSTATUS,
	I40E_PFPE_CCQPHIGH,
	I40E_PFPE_CCQPLOW,
	I40E_PFPE_CQARM,
	I40E_PFPE_CQACK,
	I40E_PFPE_AEQALLOC,
	I40E_PFPE_CQPERRCODES,
	I40E_PFPE_WQEALLOC,
	I40E_PFINT_DYN_CTLN(0),
	I40IW_DB_ADDR_OFFSET,

	I40E_GLPCI_LBARCTRL,
	I40E_GLPE_CPUSTATUS0,
	I40E_GLPE_CPUSTATUS1,
	I40E_GLPE_CPUSTATUS2,
	I40E_PFINT_AEQCTL,
	I40E_PFINT_CEQCTL(0),
	I40E_VSIQF_CTL(0),
	I40E_PFHMC_PDINV,
	I40E_GLHMC_VFPDINV(0),
	I40E_GLPE_CRITERR,
	0xffffffff      /* PFINT_RATEN not used in FPK */
};

static u32 i40iw_stat_offsets[] = {
	I40E_GLPES_PFIP4RXDISCARD(0),
	I40E_GLPES_PFIP4RXTRUNC(0),
	I40E_GLPES_PFIP4TXNOROUTE(0),
	I40E_GLPES_PFIP6RXDISCARD(0),
	I40E_GLPES_PFIP6RXTRUNC(0),
	I40E_GLPES_PFIP6TXNOROUTE(0),
	I40E_GLPES_PFTCPRTXSEG(0),
	I40E_GLPES_PFTCPRXOPTERR(0),
	I40E_GLPES_PFTCPRXPROTOERR(0),
	I40E_GLPES_PFRXVLANERR(0),

	I40E_GLPES_PFIP4RXOCTSLO(0),
	I40E_GLPES_PFIP4RXPKTSLO(0),
	I40E_GLPES_PFIP4RXFRAGSLO(0),
	I40E_GLPES_PFIP4RXMCPKTSLO(0),
	I40E_GLPES_PFIP4TXOCTSLO(0),
	I40E_GLPES_PFIP4TXPKTSLO(0),
	I40E_GLPES_PFIP4TXFRAGSLO(0),
	I40E_GLPES_PFIP4TXMCPKTSLO(0),
	I40E_GLPES_PFIP6RXOCTSLO(0),
	I40E_GLPES_PFIP6RXPKTSLO(0),
	I40E_GLPES_PFIP6RXFRAGSLO(0),
	I40E_GLPES_PFIP6RXMCPKTSLO(0),
	I40E_GLPES_PFIP6TXOCTSLO(0),
	I40E_GLPES_PFIP6TXPKTSLO(0),
	I40E_GLPES_PFIP6TXFRAGSLO(0),
	I40E_GLPES_PFIP6TXMCPKTSLO(0),
	I40E_GLPES_PFTCPRXSEGSLO(0),
	I40E_GLPES_PFTCPTXSEGLO(0),
	I40E_GLPES_PFRDMARXRDSLO(0),
	I40E_GLPES_PFRDMARXSNDSLO(0),
	I40E_GLPES_PFRDMARXWRSLO(0),
	I40E_GLPES_PFRDMATXRDSLO(0),
	I40E_GLPES_PFRDMATXSNDSLO(0),
	I40E_GLPES_PFRDMATXWRSLO(0),
	I40E_GLPES_PFRDMAVBNDLO(0),
	I40E_GLPES_PFRDMAVINVLO(0),
	I40E_GLPES_PFIP4RXMCOCTSLO(0),
	I40E_GLPES_PFIP4TXMCOCTSLO(0),
	I40E_GLPES_PFIP6RXMCOCTSLO(0),
	I40E_GLPES_PFIP6TXMCOCTSLO(0),
	I40E_GLPES_PFUDPRXPKTSLO(0),
	I40E_GLPES_PFUDPTXPKTSLO(0)
};

static u64 i40iw_masks[IRDMA_MAX_MASKS] = {
	I40E_PFPE_CCQPSTATUS_CCQP_DONE,
	I40E_PFPE_CCQPSTATUS_CCQP_ERR,
	I40E_CQPSQ_STAG_PDID,
	I40E_CQPSQ_CQ_CEQID,
	I40E_CQPSQ_CQ_CQID,
	I40E_COMMIT_FPM_CQCNT,
};

static u64 i40iw_shifts[IRDMA_MAX_SHIFTS] = {
	I40E_PFPE_CCQPSTATUS_CCQP_DONE_S,
	I40E_PFPE_CCQPSTATUS_CCQP_ERR_S,
	I40E_CQPSQ_STAG_PDID_S,
	I40E_CQPSQ_CQ_CEQID_S,
	I40E_CQPSQ_CQ_CQID_S,
	I40E_COMMIT_FPM_CQCNT_S,
};

/**
 * i40iw_config_ceq- Configure CEQ interrupt
 * @dev: pointer to the device structure
 * @ceq_id: Completion Event Queue ID
 * @idx: vector index
 * @enable: Enable CEQ interrupt when true
 */
static void i40iw_config_ceq(struct irdma_sc_dev *dev, u32 ceq_id, u32 idx,
			     bool enable)
{
	u32 reg_val;

	reg_val = FIELD_PREP(I40E_PFINT_LNKLSTN_FIRSTQ_INDX, ceq_id) |
		  FIELD_PREP(I40E_PFINT_LNKLSTN_FIRSTQ_TYPE, QUEUE_TYPE_CEQ);
	wr32(dev->hw, I40E_PFINT_LNKLSTN(idx - 1), reg_val);

	reg_val = FIELD_PREP(I40E_PFINT_DYN_CTLN_ITR_INDX, 0x3) |
		  FIELD_PREP(I40E_PFINT_DYN_CTLN_INTENA, 0x1);
	wr32(dev->hw, I40E_PFINT_DYN_CTLN(idx - 1), reg_val);

	reg_val = FIELD_PREP(IRDMA_GLINT_CEQCTL_CAUSE_ENA, enable) |
		  FIELD_PREP(IRDMA_GLINT_CEQCTL_MSIX_INDX, idx) |
		  FIELD_PREP(I40E_PFINT_CEQCTL_NEXTQ_INDX, NULL_QUEUE_INDEX) |
		  FIELD_PREP(IRDMA_GLINT_CEQCTL_ITR_INDX, 0x3);

	wr32(dev->hw, i40iw_regs[IRDMA_GLINT_CEQCTL] + 4 * ceq_id, reg_val);
}

/**
 * i40iw_ena_irq - Enable interrupt
 * @dev: pointer to the device structure
 * @idx: vector index
 */
static void i40iw_ena_irq(struct irdma_sc_dev *dev, u32 idx)
{
	u32 val;

	val = FIELD_PREP(IRDMA_GLINT_DYN_CTL_INTENA, 0x1) |
	      FIELD_PREP(IRDMA_GLINT_DYN_CTL_CLEARPBA, 0x1) |
	      FIELD_PREP(IRDMA_GLINT_DYN_CTL_ITR_INDX, 0x3);
	wr32(dev->hw, i40iw_regs[IRDMA_GLINT_DYN_CTL] + 4 * (idx - 1), val);
}

/**
 * i40iw_disable_irq - Disable interrupt
 * @dev: pointer to the device structure
 * @idx: vector index
 */
static void i40iw_disable_irq(struct irdma_sc_dev *dev, u32 idx)
{
	wr32(dev->hw, i40iw_regs[IRDMA_GLINT_DYN_CTL] + 4 * (idx - 1), 0);
}

static const struct irdma_irq_ops i40iw_irq_ops = {
	.irdma_cfg_aeq = irdma_cfg_aeq,
	.irdma_cfg_ceq = i40iw_config_ceq,
	.irdma_dis_irq = i40iw_disable_irq,
	.irdma_en_irq = i40iw_ena_irq,
};

static const struct irdma_hw_stat_map i40iw_hw_stat_map[] = {
	[IRDMA_HW_STAT_INDEX_RXVLANERR]	=	{   0,  0, IRDMA_MAX_STATS_24 },
	[IRDMA_HW_STAT_INDEX_IP4RXOCTS] =	{   8,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4RXPKTS] =	{  16,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4RXDISCARD] =	{  24,  0, IRDMA_MAX_STATS_32 },
	[IRDMA_HW_STAT_INDEX_IP4RXTRUNC] =	{  32,  0, IRDMA_MAX_STATS_32 },
	[IRDMA_HW_STAT_INDEX_IP4RXFRAGS] =      {  40,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4RXMCPKTS] =     {  48,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6RXOCTS] =       {  56,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6RXPKTS] =       {  64,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6RXDISCARD] =    {  72,  0, IRDMA_MAX_STATS_32 },
	[IRDMA_HW_STAT_INDEX_IP6RXTRUNC] =      {  80,  0, IRDMA_MAX_STATS_32 },
	[IRDMA_HW_STAT_INDEX_IP6RXFRAGS] =      {  88,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6RXMCPKTS] =     {  96,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4TXOCTS] =       { 104,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4TXPKTS] =       { 112,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4TXFRAGS] =      { 120,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4TXMCPKTS] =     { 128,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6TXOCTS] =       { 136,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6TXPKTS] =       { 144,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6TXFRAGS] =      { 152,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6TXMCPKTS] =     { 160,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4TXNOROUTE] =    { 168,  0, IRDMA_MAX_STATS_24 },
	[IRDMA_HW_STAT_INDEX_IP6TXNOROUTE] =    { 176,  0, IRDMA_MAX_STATS_24 },
	[IRDMA_HW_STAT_INDEX_TCPRXSEGS] =       { 184,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_TCPRXOPTERR] =     { 192,  0, IRDMA_MAX_STATS_24 },
	[IRDMA_HW_STAT_INDEX_TCPRXPROTOERR] =   { 200,  0, IRDMA_MAX_STATS_24 },
	[IRDMA_HW_STAT_INDEX_TCPTXSEG] =        { 208,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_TCPRTXSEG] =       { 216,  0, IRDMA_MAX_STATS_32 },
	[IRDMA_HW_STAT_INDEX_RDMARXWRS] =       { 224,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMARXRDS] =       { 232,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMARXSNDS] =      { 240,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMATXWRS] =       { 248,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMATXRDS] =       { 256,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMATXSNDS] =      { 264,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMAVBND] =        { 272,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMAVINV] =        { 280,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4RXMCOCTS] =     { 288,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4TXMCOCTS] =     { 296,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6RXMCOCTS] =     { 304,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6TXMCOCTS] =     { 312,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_UDPRXPKTS] =       { 320,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_UDPTXPKTS] =       { 328,  0, IRDMA_MAX_STATS_48 },
};

void i40iw_init_hw(struct irdma_sc_dev *dev)
{
	int i;
	u8 __iomem *hw_addr;

	for (i = 0; i < IRDMA_MAX_REGS; ++i) {
		hw_addr = dev->hw->hw_addr;

		if (i == IRDMA_DB_ADDR_OFFSET)
			hw_addr = NULL;

		dev->hw_regs[i] = (u32 __iomem *)(i40iw_regs[i] + hw_addr);
	}

	for (i = 0; i < IRDMA_HW_STAT_INDEX_MAX_GEN_1; ++i)
		dev->hw_stats_regs[i] = i40iw_stat_offsets[i];

	dev->hw_attrs.first_hw_vf_fpm_id = I40IW_FIRST_VF_FPM_ID;
	dev->hw_attrs.max_hw_vf_fpm_id = IRDMA_MAX_VF_FPM_ID;

	for (i = 0; i < IRDMA_MAX_SHIFTS; ++i)
		dev->hw_shifts[i] = i40iw_shifts[i];

	for (i = 0; i < IRDMA_MAX_MASKS; ++i)
		dev->hw_masks[i] = i40iw_masks[i];

	dev->wqe_alloc_db = dev->hw_regs[IRDMA_WQEALLOC];
	dev->cq_arm_db = dev->hw_regs[IRDMA_CQARM];
	dev->aeq_alloc_db = dev->hw_regs[IRDMA_AEQALLOC];
	dev->cqp_db = dev->hw_regs[IRDMA_CQPDB];
	dev->cq_ack_db = dev->hw_regs[IRDMA_CQACK];
	dev->ceq_itr_mask_db = NULL;
	dev->aeq_itr_mask_db = NULL;
	dev->irq_ops = &i40iw_irq_ops;
	dev->hw_stats_map = i40iw_hw_stat_map;

	/* Setup the hardware limits, hmc may limit further */
	dev->hw_attrs.uk_attrs.max_hw_wq_frags = I40IW_MAX_WQ_FRAGMENT_COUNT;
	dev->hw_attrs.uk_attrs.max_hw_read_sges = I40IW_MAX_SGE_RD;
	dev->hw_attrs.max_hw_device_pages = I40IW_MAX_PUSH_PAGE_COUNT;
	dev->hw_attrs.uk_attrs.max_hw_inline = I40IW_MAX_INLINE_DATA_SIZE;
	dev->hw_attrs.page_size_cap = SZ_4K | SZ_2M;
	dev->hw_attrs.max_hw_ird = I40IW_MAX_IRD_SIZE;
	dev->hw_attrs.max_hw_ord = I40IW_MAX_ORD_SIZE;
	dev->hw_attrs.max_hw_wqes = I40IW_MAX_WQ_ENTRIES;
	dev->hw_attrs.uk_attrs.max_hw_rq_quanta = I40IW_QP_SW_MAX_RQ_QUANTA;
	dev->hw_attrs.uk_attrs.max_hw_wq_quanta = I40IW_QP_SW_MAX_WQ_QUANTA;
	dev->hw_attrs.uk_attrs.max_hw_sq_chunk = I40IW_MAX_QUANTA_PER_WR;
	dev->hw_attrs.max_hw_pds = I40IW_MAX_PDS;
	dev->hw_attrs.max_stat_inst = I40IW_MAX_STATS_COUNT;
	dev->hw_attrs.max_stat_idx = IRDMA_HW_STAT_INDEX_MAX_GEN_1;
	dev->hw_attrs.max_hw_outbound_msg_size = I40IW_MAX_OUTBOUND_MSG_SIZE;
	dev->hw_attrs.max_hw_inbound_msg_size = I40IW_MAX_INBOUND_MSG_SIZE;
	dev->hw_attrs.uk_attrs.min_hw_wq_size = I40IW_MIN_WQ_SIZE;
	dev->hw_attrs.max_qp_wr = I40IW_MAX_QP_WRS;
}
