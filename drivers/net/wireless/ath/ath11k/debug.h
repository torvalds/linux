/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#ifndef _ATH11K_DEBUG_H_
#define _ATH11K_DEBUG_H_

#include "trace.h"
#include "debugfs.h"

enum ath11k_debug_mask {
	ATH11K_DBG_AHB		= 0x00000001,
	ATH11K_DBG_WMI		= 0x00000002,
	ATH11K_DBG_HTC		= 0x00000004,
	ATH11K_DBG_DP_HTT	= 0x00000008,
	ATH11K_DBG_MAC		= 0x00000010,
	ATH11K_DBG_BOOT		= 0x00000020,
	ATH11K_DBG_QMI		= 0x00000040,
	ATH11K_DBG_DATA		= 0x00000080,
	ATH11K_DBG_MGMT		= 0x00000100,
	ATH11K_DBG_REG		= 0x00000200,
	ATH11K_DBG_TESTMODE	= 0x00000400,
	ATH11k_DBG_HAL		= 0x00000800,
	ATH11K_DBG_PCI		= 0x00001000,
	ATH11K_DBG_DP_TX	= 0x00001000,
	ATH11K_DBG_DP_RX	= 0x00002000,
	ATH11K_DBG_ANY		= 0xffffffff,
};

__printf(2, 3) void ath11k_info(struct ath11k_base *ab, const char *fmt, ...);
__printf(2, 3) void ath11k_err(struct ath11k_base *ab, const char *fmt, ...);
__printf(2, 3) void ath11k_warn(struct ath11k_base *ab, const char *fmt, ...);

extern unsigned int ath11k_debug_mask;

#ifdef CONFIG_ATH11K_DEBUG
__printf(3, 4) void __ath11k_dbg(struct ath11k_base *ab,
				 enum ath11k_debug_mask mask,
				 const char *fmt, ...);
void ath11k_dbg_dump(struct ath11k_base *ab,
		     enum ath11k_debug_mask mask,
		     const char *msg, const char *prefix,
		     const void *buf, size_t len);
#else /* CONFIG_ATH11K_DEBUG */
static inline int __ath11k_dbg(struct ath11k_base *ab,
			       enum ath11k_debug_mask dbg_mask,
			       const char *fmt, ...)
{
	return 0;
}

static inline void ath11k_dbg_dump(struct ath11k_base *ab,
				   enum ath11k_debug_mask mask,
				   const char *msg, const char *prefix,
				   const void *buf, size_t len)
{
}
#endif /* CONFIG_ATH11K_DEBUG */

#define ath11k_dbg(ar, dbg_mask, fmt, ...)			\
do {								\
	if ((ath11k_debug_mask & dbg_mask) ||			\
	    trace_ath11k_log_dbg_enabled())			\
		__ath11k_dbg(ar, dbg_mask, fmt, ##__VA_ARGS__);	\
} while (0)

#endif /* _ATH11K_DEBUG_H_ */
