// SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB
/* Copyright (c) 2017 - 2021 Intel Corporation */
#include "osdep.h"
#include "type.h"
#include "icrdma_hw.h"

static u32 icrdma_regs[IRDMA_MAX_REGS] = {
	PFPE_CQPTAIL,
	PFPE_CQPDB,
	PFPE_CCQPSTATUS,
	PFPE_CCQPHIGH,
	PFPE_CCQPLOW,
	PFPE_CQARM,
	PFPE_CQACK,
	PFPE_AEQALLOC,
	PFPE_CQPERRCODES,
	PFPE_WQEALLOC,
	GLINT_DYN_CTL(0),
	ICRDMA_DB_ADDR_OFFSET,

	GLPCI_LBARCTRL,
	GLPE_CPUSTATUS0,
	GLPE_CPUSTATUS1,
	GLPE_CPUSTATUS2,
	PFINT_AEQCTL,
	GLINT_CEQCTL(0),
	VSIQF_PE_CTL1(0),
	PFHMC_PDINV,
	GLHMC_VFPDINV(0),
	GLPE_CRITERR,
	GLINT_RATE(0),
};

static u64 icrdma_masks[IRDMA_MAX_MASKS] = {
	ICRDMA_CCQPSTATUS_CCQP_DONE,
	ICRDMA_CCQPSTATUS_CCQP_ERR,
	ICRDMA_CQPSQ_STAG_PDID,
	ICRDMA_CQPSQ_CQ_CEQID,
	ICRDMA_CQPSQ_CQ_CQID,
	ICRDMA_COMMIT_FPM_CQCNT,
};

static u64 icrdma_shifts[IRDMA_MAX_SHIFTS] = {
	ICRDMA_CCQPSTATUS_CCQP_DONE_S,
	ICRDMA_CCQPSTATUS_CCQP_ERR_S,
	ICRDMA_CQPSQ_STAG_PDID_S,
	ICRDMA_CQPSQ_CQ_CEQID_S,
	ICRDMA_CQPSQ_CQ_CQID_S,
	ICRDMA_COMMIT_FPM_CQCNT_S,
};

/**
 * icrdma_ena_irq - Enable interrupt
 * @dev: pointer to the device structure
 * @idx: vector index
 */
static void icrdma_ena_irq(struct irdma_sc_dev *dev, u32 idx)
{
	u32 val;
	u32 interval = 0;

	if (dev->ceq_itr && dev->aeq->msix_idx != idx)
		interval = dev->ceq_itr >> 1; /* 2 usec units */
	val = FIELD_PREP(IRDMA_GLINT_DYN_CTL_ITR_INDX, 0) |
	      FIELD_PREP(IRDMA_GLINT_DYN_CTL_INTERVAL, interval) |
	      FIELD_PREP(IRDMA_GLINT_DYN_CTL_INTENA, 1) |
	      FIELD_PREP(IRDMA_GLINT_DYN_CTL_CLEARPBA, 1);

	if (dev->hw_attrs.uk_attrs.hw_rev != IRDMA_GEN_1)
		writel(val, dev->hw_regs[IRDMA_GLINT_DYN_CTL] + idx);
	else
		writel(val, dev->hw_regs[IRDMA_GLINT_DYN_CTL] + (idx - 1));
}

/**
 * icrdma_disable_irq - Disable interrupt
 * @dev: pointer to the device structure
 * @idx: vector index
 */
static void icrdma_disable_irq(struct irdma_sc_dev *dev, u32 idx)
{
	if (dev->hw_attrs.uk_attrs.hw_rev != IRDMA_GEN_1)
		writel(0, dev->hw_regs[IRDMA_GLINT_DYN_CTL] + idx);
	else
		writel(0, dev->hw_regs[IRDMA_GLINT_DYN_CTL] + (idx - 1));
}

/**
 * icrdma_cfg_ceq- Configure CEQ interrupt
 * @dev: pointer to the device structure
 * @ceq_id: Completion Event Queue ID
 * @idx: vector index
 * @enable: True to enable, False disables
 */
static void icrdma_cfg_ceq(struct irdma_sc_dev *dev, u32 ceq_id, u32 idx,
			   bool enable)
{
	u32 reg_val;

	reg_val = FIELD_PREP(IRDMA_GLINT_CEQCTL_CAUSE_ENA, enable) |
		  FIELD_PREP(IRDMA_GLINT_CEQCTL_MSIX_INDX, idx) |
		  FIELD_PREP(IRDMA_GLINT_CEQCTL_ITR_INDX, 3);

	writel(reg_val, dev->hw_regs[IRDMA_GLINT_CEQCTL] + ceq_id);
}

static const struct irdma_irq_ops icrdma_irq_ops = {
	.irdma_cfg_aeq = irdma_cfg_aeq,
	.irdma_cfg_ceq = icrdma_cfg_ceq,
	.irdma_dis_irq = icrdma_disable_irq,
	.irdma_en_irq = icrdma_ena_irq,
};

static const struct irdma_hw_stat_map icrdma_hw_stat_map[] = {
	[IRDMA_HW_STAT_INDEX_RXVLANERR]	=	{   0, 32, IRDMA_MAX_STATS_24 },
	[IRDMA_HW_STAT_INDEX_IP4RXOCTS] =	{   8,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4RXPKTS] =	{  16,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4RXDISCARD] =	{  24, 32, IRDMA_MAX_STATS_32 },
	[IRDMA_HW_STAT_INDEX_IP4RXTRUNC] =	{  24,  0, IRDMA_MAX_STATS_32 },
	[IRDMA_HW_STAT_INDEX_IP4RXFRAGS] =	{  32,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4RXMCOCTS] =	{  40,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4RXMCPKTS] =	{  48,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6RXOCTS] =	{  56,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6RXPKTS] =	{  64,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6RXDISCARD] =	{  72, 32, IRDMA_MAX_STATS_32 },
	[IRDMA_HW_STAT_INDEX_IP6RXTRUNC] =	{  72,  0, IRDMA_MAX_STATS_32 },
	[IRDMA_HW_STAT_INDEX_IP6RXFRAGS] =	{  80,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6RXMCOCTS] =	{  88,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6RXMCPKTS] =	{  96,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4TXOCTS] =	{ 104,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4TXPKTS] =	{ 112,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4TXFRAGS] =	{ 120,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4TXMCOCTS] =	{ 128,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4TXMCPKTS] =	{ 136,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6TXOCTS] =	{ 144,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6TXPKTS] =	{ 152,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6TXFRAGS] =	{ 160,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6TXMCOCTS] =	{ 168,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP6TXMCPKTS] =	{ 176,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_IP4TXNOROUTE] =	{ 184, 32, IRDMA_MAX_STATS_24 },
	[IRDMA_HW_STAT_INDEX_IP6TXNOROUTE] =	{ 184,  0, IRDMA_MAX_STATS_24 },
	[IRDMA_HW_STAT_INDEX_TCPRXSEGS] =	{ 192, 32, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_TCPRXOPTERR] =	{ 200, 32, IRDMA_MAX_STATS_24 },
	[IRDMA_HW_STAT_INDEX_TCPRXPROTOERR] =	{ 200,  0, IRDMA_MAX_STATS_24 },
	[IRDMA_HW_STAT_INDEX_TCPTXSEG] =	{ 208,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_TCPRTXSEG] =	{ 216, 32, IRDMA_MAX_STATS_32 },
	[IRDMA_HW_STAT_INDEX_UDPRXPKTS] =	{ 224,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_UDPTXPKTS] =	{ 232,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMARXWRS] =	{ 240,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMARXRDS] =	{ 248,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMARXSNDS] =	{ 256,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMATXWRS] =	{ 264,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMATXRDS] =	{ 272,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMATXSNDS] =	{ 280,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMAVBND] =	{ 288,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RDMAVINV] =	{ 296,  0, IRDMA_MAX_STATS_48 },
	[IRDMA_HW_STAT_INDEX_RXNPECNMARKEDPKTS] = { 304,  0, IRDMA_MAX_STATS_56 },
	[IRDMA_HW_STAT_INDEX_RXRPCNPIGNORED] =	{ 312, 32, IRDMA_MAX_STATS_24 },
	[IRDMA_HW_STAT_INDEX_RXRPCNPHANDLED] =	{ 312,  0, IRDMA_MAX_STATS_32 },
	[IRDMA_HW_STAT_INDEX_TXNPCNPSENT] =	{ 320,  0, IRDMA_MAX_STATS_32 },
};

void icrdma_init_hw(struct irdma_sc_dev *dev)
{
	int i;
	u8 __iomem *hw_addr;

	for (i = 0; i < IRDMA_MAX_REGS; ++i) {
		hw_addr = dev->hw->hw_addr;

		if (i == IRDMA_DB_ADDR_OFFSET)
			hw_addr = NULL;

		dev->hw_regs[i] = (u32 __iomem *)(hw_addr + icrdma_regs[i]);
	}
	dev->hw_attrs.max_hw_vf_fpm_id = IRDMA_MAX_VF_FPM_ID;
	dev->hw_attrs.first_hw_vf_fpm_id = IRDMA_FIRST_VF_FPM_ID;

	for (i = 0; i < IRDMA_MAX_SHIFTS; ++i)
		dev->hw_shifts[i] = icrdma_shifts[i];

	for (i = 0; i < IRDMA_MAX_MASKS; ++i)
		dev->hw_masks[i] = icrdma_masks[i];

	dev->wqe_alloc_db = dev->hw_regs[IRDMA_WQEALLOC];
	dev->cq_arm_db = dev->hw_regs[IRDMA_CQARM];
	dev->aeq_alloc_db = dev->hw_regs[IRDMA_AEQALLOC];
	dev->cqp_db = dev->hw_regs[IRDMA_CQPDB];
	dev->cq_ack_db = dev->hw_regs[IRDMA_CQACK];
	dev->irq_ops = &icrdma_irq_ops;
	dev->hw_attrs.page_size_cap = SZ_4K | SZ_2M | SZ_1G;
	dev->hw_stats_map = icrdma_hw_stat_map;
	dev->hw_attrs.max_hw_ird = ICRDMA_MAX_IRD_SIZE;
	dev->hw_attrs.max_hw_ord = ICRDMA_MAX_ORD_SIZE;
	dev->hw_attrs.max_stat_inst = ICRDMA_MAX_STATS_COUNT;
	dev->hw_attrs.max_stat_idx = IRDMA_HW_STAT_INDEX_MAX_GEN_2;

	dev->hw_attrs.uk_attrs.min_hw_wq_size = ICRDMA_MIN_WQ_SIZE;
	dev->hw_attrs.uk_attrs.max_hw_sq_chunk = IRDMA_MAX_QUANTA_PER_WR;
	dev->hw_attrs.uk_attrs.feature_flags |= IRDMA_FEATURE_RTS_AE |
						IRDMA_FEATURE_CQ_RESIZE;
}
