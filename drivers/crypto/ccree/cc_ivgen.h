/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

#ifndef __CC_IVGEN_H__
#define __CC_IVGEN_H__

#include "cc_hw_queue_defs.h"

#define CC_IVPOOL_SEQ_LEN 8

/*!
 * Allocates iv-pool and maps resources.
 * This function generates the first IV pool.
 *
 * \param drvdata Driver's private context
 *
 * \return int Zero for success, negative value otherwise.
 */
int cc_ivgen_init(struct cc_drvdata *drvdata);

/*!
 * Free iv-pool and ivgen context.
 *
 * \param drvdata
 */
void cc_ivgen_fini(struct cc_drvdata *drvdata);

/*!
 * Generates the initial pool in SRAM.
 * This function should be invoked when resuming DX driver.
 *
 * \param drvdata
 *
 * \return int Zero for success, negative value otherwise.
 */
int cc_init_iv_sram(struct cc_drvdata *drvdata);

/*!
 * Acquires 16 Bytes IV from the iv-pool
 *
 * \param drvdata Driver private context
 * \param iv_out_dma Array of physical IV out addresses
 * \param iv_out_dma_len Length of iv_out_dma array (additional elements of
 *                       iv_out_dma array are ignore)
 * \param iv_out_size May be 8 or 16 bytes long
 * \param iv_seq IN/OUT array to the descriptors sequence
 * \param iv_seq_len IN/OUT pointer to the sequence length
 *
 * \return int Zero for success, negative value otherwise.
 */
int cc_get_iv(struct cc_drvdata *drvdata, dma_addr_t iv_out_dma[],
	      unsigned int iv_out_dma_len, unsigned int iv_out_size,
	      struct cc_hw_desc iv_seq[], unsigned int *iv_seq_len);

#endif /*__CC_IVGEN_H__*/
