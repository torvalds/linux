// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

#include "cc_driver.h"
#include "cc_sram_mgr.h"

/**
 * struct cc_sram_ctx -Internal RAM context manager
 * @sram_free_offset:   the offset to the non-allocated area
 */
struct cc_sram_ctx {
	cc_sram_addr_t sram_free_offset;
};

/**
 * cc_sram_mgr_fini() - Cleanup SRAM pool.
 *
 * @drvdata: Associated device driver context
 */
void cc_sram_mgr_fini(struct cc_drvdata *drvdata)
{
	/* Free "this" context */
	kfree(drvdata->sram_mgr_handle);
}

/**
 * cc_sram_mgr_init() - Initializes SRAM pool.
 *      The pool starts right at the beginning of SRAM.
 *      Returns zero for success, negative value otherwise.
 *
 * @drvdata: Associated device driver context
 */
int cc_sram_mgr_init(struct cc_drvdata *drvdata)
{
	struct cc_sram_ctx *ctx;

	/* Allocate "this" context */
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);

	if (!ctx)
		return -ENOMEM;

	drvdata->sram_mgr_handle = ctx;

	return 0;
}

/*!
 * Allocated buffer from SRAM pool.
 * Note: Caller is responsible to free the LAST allocated buffer.
 * This function does not taking care of any fragmentation may occur
 * by the order of calls to alloc/free.
 *
 * \param drvdata
 * \param size The requested bytes to allocate
 */
cc_sram_addr_t cc_sram_alloc(struct cc_drvdata *drvdata, u32 size)
{
	struct cc_sram_ctx *smgr_ctx = drvdata->sram_mgr_handle;
	struct device *dev = drvdata_to_dev(drvdata);
	cc_sram_addr_t p;

	if ((size & 0x3)) {
		dev_err(dev, "Requested buffer size (%u) is not multiple of 4",
			size);
		return NULL_SRAM_ADDR;
	}
	if (size > (CC_CC_SRAM_SIZE - smgr_ctx->sram_free_offset)) {
		dev_err(dev, "Not enough space to allocate %u B (at offset %llu)\n",
			size, smgr_ctx->sram_free_offset);
		return NULL_SRAM_ADDR;
	}

	p = smgr_ctx->sram_free_offset;
	smgr_ctx->sram_free_offset += size;
	dev_dbg(dev, "Allocated %u B @ %u\n", size, (unsigned int)p);
	return p;
}

/**
 * cc_set_sram_desc() - Create const descriptors sequence to
 *	set values in given array into SRAM.
 * Note: each const value can't exceed word size.
 *
 * @src:	  A pointer to array of words to set as consts.
 * @dst:	  The target SRAM buffer to set into
 * @nelements:	  The number of words in "src" array
 * @seq:	  A pointer to the given IN/OUT descriptor sequence
 * @seq_len:	  A pointer to the given IN/OUT sequence length
 */
void cc_set_sram_desc(const u32 *src, cc_sram_addr_t dst,
		      unsigned int nelement, struct cc_hw_desc *seq,
		      unsigned int *seq_len)
{
	u32 i;
	unsigned int idx = *seq_len;

	for (i = 0; i < nelement; i++, idx++) {
		hw_desc_init(&seq[idx]);
		set_din_const(&seq[idx], src[i], sizeof(u32));
		set_dout_sram(&seq[idx], dst + (i * sizeof(u32)), sizeof(u32));
		set_flow_mode(&seq[idx], BYPASS);
	}

	*seq_len = idx;
}

