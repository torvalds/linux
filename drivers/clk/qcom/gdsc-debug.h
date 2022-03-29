/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef __QCOM_GDSC_DEBUG_H__
#define __QCOM_GDSC_DEBUG_H__

#if IS_ENABLED(CONFIG_QCOM_GDSC_REGULATOR)
void gdsc_debug_print_regs(struct regulator *regulator);
#else
static inline void gdsc_debug_print_regs(struct regulator *regulator)
{ }
#endif

#endif  /* __QCOM_GDSC_DEBUG_H__ */
