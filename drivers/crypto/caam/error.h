/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAAM Error Reporting code header
 *
 * Copyright 2009-2011 Freescale Semiconductor, Inc.
 */

#ifndef CAAM_ERROR_H
#define CAAM_ERROR_H

#include "desc.h"

#define CAAM_ERROR_STR_MAX 302

void caam_strstatus(struct device *dev, u32 status, bool qi_v2);

#define caam_jr_strstatus(jrdev, status) caam_strstatus(jrdev, status, false)
#define caam_qi2_strstatus(qidev, status) caam_strstatus(qidev, status, true)

void caam_dump_sg(const char *level, const char *prefix_str, int prefix_type,
		  int rowsize, int groupsize, struct scatterlist *sg,
		  size_t tlen, bool ascii);

static inline bool is_mdha(u32 algtype)
{
	return (algtype & OP_ALG_ALGSEL_MASK & ~OP_ALG_ALGSEL_SUBMASK) ==
	       OP_ALG_CHA_MDHA;
}
#endif /* CAAM_ERROR_H */
