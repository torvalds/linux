/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#ifndef _DEBUG_SYMBOL_H
#define _DEBUG_SYMBOL_H

#include <linux/types.h>

#if IS_ENABLED(CONFIG_QCOM_DEBUG_SYMBOL)
extern int debug_symbol_available(void);
extern unsigned long debug_symbol_lookup_name(const char *name);
#else
static inline int debug_symbol_available(void)
{
	return -EINVAL;
}

static inline unsigned long debug_symbol_lookup_name(const char *name)
{
	return 0;
}
#endif /* CONFIG_QCOM_DEBUG_SYMBOL */

#endif
