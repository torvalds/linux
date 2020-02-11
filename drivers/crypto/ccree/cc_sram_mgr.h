/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2019 ARM Limited (or its affiliates). */

#ifndef __CC_SRAM_MGR_H__
#define __CC_SRAM_MGR_H__

#ifndef CC_CC_SRAM_SIZE
#define CC_CC_SRAM_SIZE 4096
#endif

struct cc_drvdata;

#define NULL_SRAM_ADDR ((u32)-1)

/**
 * cc_sram_mgr_init() - Initializes SRAM pool.
 * The first X bytes of SRAM are reserved for ROM usage, hence, pool
 * starts right after X bytes.
 *
 * @drvdata: Associated device driver context
 *
 * Return:
 * Zero for success, negative value otherwise.
 */
int cc_sram_mgr_init(struct cc_drvdata *drvdata);

/**
 * cc_sram_alloc() - Allocate buffer from SRAM pool.
 *
 * @drvdata: Associated device driver context
 * @size: The requested bytes to allocate
 *
 * Return:
 * Address offset in SRAM or NULL_SRAM_ADDR for failure.
 */
u32 cc_sram_alloc(struct cc_drvdata *drvdata, u32 size);

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
		      struct cc_hw_desc *seq, unsigned int *seq_len);

#endif /*__CC_SRAM_MGR_H__*/
