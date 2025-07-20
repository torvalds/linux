// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include "fbnic.h"

#define FBNIC_BOUNDS(section) { \
	.start = FBNIC_CSR_START_##section, \
	.end = FBNIC_CSR_END_##section + 1, \
}

struct fbnic_csr_bounds {
	u32	start;
	u32	end;
};

static const struct fbnic_csr_bounds fbnic_csr_sects[] = {
	FBNIC_BOUNDS(INTR),
	FBNIC_BOUNDS(INTR_CQ),
	FBNIC_BOUNDS(QM_TX),
	FBNIC_BOUNDS(QM_RX),
	FBNIC_BOUNDS(TCE),
	FBNIC_BOUNDS(TCE_RAM),
	FBNIC_BOUNDS(TMI),
	FBNIC_BOUNDS(PTP),
	FBNIC_BOUNDS(RXB),
	FBNIC_BOUNDS(RPC),
	FBNIC_BOUNDS(FAB),
	FBNIC_BOUNDS(MASTER),
	FBNIC_BOUNDS(PCS),
	FBNIC_BOUNDS(RSFEC),
	FBNIC_BOUNDS(MAC_MAC),
	FBNIC_BOUNDS(SIG),
	FBNIC_BOUNDS(PCIE_SS_COMPHY),
	FBNIC_BOUNDS(PUL_USER),
	FBNIC_BOUNDS(QUEUE),
	FBNIC_BOUNDS(RPC_RAM),
};

#define FBNIC_RPC_TCAM_ACT_DW_PER_ENTRY			14
#define FBNIC_RPC_TCAM_ACT_NUM_ENTRIES			64

#define FBNIC_RPC_TCAM_MACDA_DW_PER_ENTRY		4
#define FBNIC_RPC_TCAM_MACDA_NUM_ENTRIES		32

#define FBNIC_RPC_TCAM_OUTER_IPSRC_DW_PER_ENTRY		9
#define FBNIC_RPC_TCAM_OUTER_IPSRC_NUM_ENTRIES		8

#define FBNIC_RPC_TCAM_OUTER_IPDST_DW_PER_ENTRY		9
#define FBNIC_RPC_TCAM_OUTER_IPDST_NUM_ENTRIES		8

#define FBNIC_RPC_TCAM_IPSRC_DW_PER_ENTRY		9
#define FBNIC_RPC_TCAM_IPSRC_NUM_ENTRIES		8

#define FBNIC_RPC_TCAM_IPDST_DW_PER_ENTRY		9
#define FBNIC_RPC_TCAM_IPDST_NUM_ENTRIES		8

#define FBNIC_RPC_RSS_TBL_DW_PER_ENTRY			2
#define FBNIC_RPC_RSS_TBL_NUM_ENTRIES			256

static void fbnic_csr_get_regs_rpc_ram(struct fbnic_dev *fbd, u32 **data_p)
{
	u32 start = FBNIC_CSR_START_RPC_RAM;
	u32 end = FBNIC_CSR_END_RPC_RAM;
	u32 *data = *data_p;
	u32 i, j;

	*(data++) = start;
	*(data++) = end;

	/* FBNIC_RPC_TCAM_ACT */
	for (i = 0; i < FBNIC_RPC_TCAM_ACT_NUM_ENTRIES; i++) {
		for (j = 0; j < FBNIC_RPC_TCAM_ACT_DW_PER_ENTRY; j++)
			*(data++) = rd32(fbd, FBNIC_RPC_TCAM_ACT(i, j));
	}

	/* FBNIC_RPC_TCAM_MACDA */
	for (i = 0; i < FBNIC_RPC_TCAM_MACDA_NUM_ENTRIES; i++) {
		for (j = 0; j < FBNIC_RPC_TCAM_MACDA_DW_PER_ENTRY; j++)
			*(data++) = rd32(fbd, FBNIC_RPC_TCAM_MACDA(i, j));
	}

	/* FBNIC_RPC_TCAM_OUTER_IPSRC */
	for (i = 0; i < FBNIC_RPC_TCAM_OUTER_IPSRC_NUM_ENTRIES; i++) {
		for (j = 0; j < FBNIC_RPC_TCAM_OUTER_IPSRC_DW_PER_ENTRY; j++)
			*(data++) = rd32(fbd, FBNIC_RPC_TCAM_OUTER_IPSRC(i, j));
	}

	/* FBNIC_RPC_TCAM_OUTER_IPDST */
	for (i = 0; i < FBNIC_RPC_TCAM_OUTER_IPDST_NUM_ENTRIES; i++) {
		for (j = 0; j < FBNIC_RPC_TCAM_OUTER_IPDST_DW_PER_ENTRY; j++)
			*(data++) = rd32(fbd, FBNIC_RPC_TCAM_OUTER_IPDST(i, j));
	}

	/* FBNIC_RPC_TCAM_IPSRC */
	for (i = 0; i < FBNIC_RPC_TCAM_IPSRC_NUM_ENTRIES; i++) {
		for (j = 0; j < FBNIC_RPC_TCAM_IPSRC_DW_PER_ENTRY; j++)
			*(data++) = rd32(fbd, FBNIC_RPC_TCAM_IPSRC(i, j));
	}

	/* FBNIC_RPC_TCAM_IPDST */
	for (i = 0; i < FBNIC_RPC_TCAM_IPDST_NUM_ENTRIES; i++) {
		for (j = 0; j < FBNIC_RPC_TCAM_IPDST_DW_PER_ENTRY; j++)
			*(data++) = rd32(fbd, FBNIC_RPC_TCAM_IPDST(i, j));
	}

	/* FBNIC_RPC_RSS_TBL */
	for (i = 0; i < FBNIC_RPC_RSS_TBL_NUM_ENTRIES; i++) {
		for (j = 0; j < FBNIC_RPC_RSS_TBL_DW_PER_ENTRY; j++)
			*(data++) = rd32(fbd, FBNIC_RPC_RSS_TBL(i, j));
	}

	*data_p = data;
}

void fbnic_csr_get_regs(struct fbnic_dev *fbd, u32 *data, u32 *regs_version)
{
	const struct fbnic_csr_bounds *bound;
	u32 *start = data;
	int i, j;

	*regs_version = 1u;

	/* Skip RPC_RAM section which cannot be dumped linearly */
	for (i = 0, bound = fbnic_csr_sects;
	     i < ARRAY_SIZE(fbnic_csr_sects) - 1; i++, ++bound) {
		*(data++) = bound->start;
		*(data++) = bound->end - 1;
		for (j = bound->start; j < bound->end; j++)
			*(data++) = rd32(fbd, j);
	}

	/* Dump the RPC_RAM as special case registers */
	fbnic_csr_get_regs_rpc_ram(fbd, &data);

	WARN_ON(data - start != fbnic_csr_regs_len(fbd));
}

int fbnic_csr_regs_len(struct fbnic_dev *fbd)
{
	int i, len = 0;

	/* Dump includes start and end information of each section
	 * which results in an offset of 2
	 */
	for (i = 0; i < ARRAY_SIZE(fbnic_csr_sects); i++)
		len += fbnic_csr_sects[i].end - fbnic_csr_sects[i].start + 2;

	return len;
}
