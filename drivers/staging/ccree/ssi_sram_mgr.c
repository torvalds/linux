/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "ssi_driver.h"
#include "ssi_sram_mgr.h"

/**
 * struct ssi_sram_mgr_ctx -Internal RAM context manager
 * @sram_free_offset:   the offset to the non-allocated area
 */
struct ssi_sram_mgr_ctx {
	ssi_sram_addr_t sram_free_offset;
};

/**
 * ssi_sram_mgr_fini() - Cleanup SRAM pool.
 *
 * @drvdata: Associated device driver context
 */
void ssi_sram_mgr_fini(struct ssi_drvdata *drvdata)
{
	struct ssi_sram_mgr_ctx *smgr_ctx = drvdata->sram_mgr_handle;

	/* Free "this" context */
	if (smgr_ctx) {
		memset(smgr_ctx, 0, sizeof(struct ssi_sram_mgr_ctx));
		kfree(smgr_ctx);
	}
}

/**
 * ssi_sram_mgr_init() - Initializes SRAM pool.
 *      The pool starts right at the beginning of SRAM.
 *      Returns zero for success, negative value otherwise.
 *
 * @drvdata: Associated device driver context
 */
int ssi_sram_mgr_init(struct ssi_drvdata *drvdata)
{
	/* Allocate "this" context */
	drvdata->sram_mgr_handle = kzalloc(sizeof(struct ssi_sram_mgr_ctx),
					   GFP_KERNEL);

	if (!drvdata->sram_mgr_handle)
		return -ENOMEM;

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
ssi_sram_addr_t ssi_sram_mgr_alloc(struct ssi_drvdata *drvdata, u32 size)
{
	struct ssi_sram_mgr_ctx *smgr_ctx = drvdata->sram_mgr_handle;
	struct device *dev = drvdata_to_dev(drvdata);
	ssi_sram_addr_t p;

	if (unlikely((size & 0x3) != 0)) {
		dev_err(dev, "Requested buffer size (%u) is not multiple of 4",
			size);
		return NULL_SRAM_ADDR;
	}
	if (unlikely(size > (SSI_CC_SRAM_SIZE - smgr_ctx->sram_free_offset))) {
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
 * ssi_sram_mgr_const2sram_desc() - Create const descriptors sequence to
 *	set values in given array into SRAM.
 * Note: each const value can't exceed word size.
 *
 * @src:	  A pointer to array of words to set as consts.
 * @dst:	  The target SRAM buffer to set into
 * @nelements:	  The number of words in "src" array
 * @seq:	  A pointer to the given IN/OUT descriptor sequence
 * @seq_len:	  A pointer to the given IN/OUT sequence length
 */
void ssi_sram_mgr_const2sram_desc(
	const u32 *src, ssi_sram_addr_t dst,
	unsigned int nelement,
	struct cc_hw_desc *seq, unsigned int *seq_len)
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

