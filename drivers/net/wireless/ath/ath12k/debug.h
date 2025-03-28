/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _ATH12K_DEBUG_H_
#define _ATH12K_DEBUG_H_

#include "trace.h"

enum ath12k_debug_mask {
	ATH12K_DBG_AHB		= 0x00000001,
	ATH12K_DBG_WMI		= 0x00000002,
	ATH12K_DBG_HTC		= 0x00000004,
	ATH12K_DBG_DP_HTT	= 0x00000008,
	ATH12K_DBG_MAC		= 0x00000010,
	ATH12K_DBG_BOOT		= 0x00000020,
	ATH12K_DBG_QMI		= 0x00000040,
	ATH12K_DBG_DATA		= 0x00000080,
	ATH12K_DBG_MGMT		= 0x00000100,
	ATH12K_DBG_REG		= 0x00000200,
	ATH12K_DBG_TESTMODE	= 0x00000400,
	ATH12K_DBG_HAL		= 0x00000800,
	ATH12K_DBG_PCI		= 0x00001000,
	ATH12K_DBG_DP_TX	= 0x00002000,
	ATH12K_DBG_DP_RX	= 0x00004000,
	ATH12K_DBG_WOW		= 0x00008000,
	ATH12K_DBG_ANY		= 0xffffffff,
};

__printf(2, 3) void ath12k_info(struct ath12k_base *ab, const char *fmt, ...);
__printf(2, 3) void ath12k_err(struct ath12k_base *ab, const char *fmt, ...);
__printf(2, 3) void __ath12k_warn(struct device *dev, const char *fmt, ...);

#define ath12k_warn(ab, fmt, ...) __ath12k_warn((ab)->dev, fmt, ##__VA_ARGS__)
#define ath12k_hw_warn(ah, fmt, ...) __ath12k_warn((ah)->dev, fmt, ##__VA_ARGS__)

extern unsigned int ath12k_debug_mask;
extern bool ath12k_ftm_mode;

#ifdef CONFIG_ATH12K_DEBUG
__printf(3, 4) void __ath12k_dbg(struct ath12k_base *ab,
				 enum ath12k_debug_mask mask,
				 const char *fmt, ...);
void ath12k_dbg_dump(struct ath12k_base *ab,
		     enum ath12k_debug_mask mask,
		     const char *msg, const char *prefix,
		     const void *buf, size_t len);
#else /* CONFIG_ATH12K_DEBUG */
static inline void __ath12k_dbg(struct ath12k_base *ab,
				enum ath12k_debug_mask dbg_mask,
				const char *fmt, ...)
{
}

static inline void ath12k_dbg_dump(struct ath12k_base *ab,
				   enum ath12k_debug_mask mask,
				   const char *msg, const char *prefix,
				   const void *buf, size_t len)
{
}
#endif /* CONFIG_ATH12K_DEBUG */

#define ath12k_dbg(ab, dbg_mask, fmt, ...)			\
do {								\
	typeof(dbg_mask) mask = (dbg_mask);			\
	if (ath12k_debug_mask & mask)				\
		__ath12k_dbg(ab, mask, fmt, ##__VA_ARGS__);	\
} while (0)

#define ath12k_generic_dbg(dbg_mask, fmt, ...)			\
	ath12k_dbg(NULL, dbg_mask, fmt, ##__VA_ARGS__)

#endif /* _ATH12K_DEBUG_H_ */
