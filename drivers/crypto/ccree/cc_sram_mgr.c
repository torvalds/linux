// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2012-2019 ARM Limited (or its affiliates). */

#include "cc_driver.h"
#include "cc_sram_mgr.h"

/**
 * cc_sram_mgr_init() - Initializes SRAM pool.
 *      The pool starts right at the beginning of SRAM.
 *      Returns zero for success, negative value otherwise.
 *
 * @drvdata: Associated device driver context
 *
 * Return:
 * 0 for success, negative error code for failure.
 */
int cc_sram_mgr_init(struct cc_drvdata *drvdata)
{
	u32 start = 0;
	struct device *dev = drvdata_to_dev(drvdata);

	if (drvdata->hw_rev < CC_HW_REV_712) {
		/* Pool starts after ROM bytes */
		start = cc_ioread(drvdata, CC_REG(HOST_SEP_SRAM_THRESHOLD));
		if ((start & 0x3) != 0) {
			dev_err(dev, "Invalid SRAM offset 0x%x\n", start);
			return -EINVAL;
		}
	}

	drvdata->sram_free_offset = start;
	return 0;
}

/**
 * cc_sram_alloc() - Allocate buffer from SRAM pool.
 *
 * @drvdata: Associated device driver context
 * @size: The requested numer of bytes to allocate
 *
 * Return:
 * Address offset in SRAM or NULL_SRAM_ADDR for failure.
 */
u32 cc_sram_alloc(struct cc_drvdata *drvdata, u32 size)
{
	struct device *dev = drvdata_to_dev(drvdata);
	u32 p;

	if ((size & 0x3)) {
		dev_err(dev, "Requested buffer size (%u) is not multiple of 4",
			size);
		return NULL_SRAM_ADDR;
	}
	if (size > (CC_CC_SRAM_SIZE - drvdata->sram_free_offset)) {
		dev_err(dev, "Not enough space to allocate %u B (at offset %u)\n",
			size, drvdata->sram_free_offset);
		return NULL_SRAM_ADDR;
	}

	p = drvdata->sram_free_offset;
	drvdata->sram_free_offset += size;
	dev_dbg(dev, "Allocated %u B @ %u\n", size, p);
	return p;
}

/**
 * cc_set_sram_desc() - Create const descriptors sequence to
 *	set values in given array into SRAM.
 * Note: each const value can't exceed word size.
 *
 * @src:	  A pointer to array of words to set as consts.
 * @dst:	  The target SRAM buffer to set into
 * @nelement:	  The number of words in "src" array
 * @seq:	  A pointer to the given IN/OUT descriptor sequence
 * @seq_len:	  A pointer to the given IN/OUT sequence length
 */
void cc_set_sram_desc(const u32 *src, u32 dst, unsigned int nelement,
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
