/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ABI_REG_OPS_H
#define __ABI_REG_OPS_H
#include <asm/reg_ops.h>

static inline unsigned int mfcr_hint(void)
{
	return mfcr("cr31");
}

static inline unsigned int mfcr_ccr2(void)
{
	return mfcr("cr23");
}
#endif /* __ABI_REG_OPS_H */
