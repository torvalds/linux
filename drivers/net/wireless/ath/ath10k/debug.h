/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <linux/types.h>
#include "trace.h"

enum ath10k_debug_mask {
	ATH10K_DBG_PCI		= 0x00000001,
	ATH10K_DBG_WMI		= 0x00000002,
	ATH10K_DBG_HTC		= 0x00000004,
	ATH10K_DBG_HTT		= 0x00000008,
	ATH10K_DBG_MAC		= 0x00000010,
	ATH10K_DBG_BOOT		= 0x00000020,
	ATH10K_DBG_PCI_DUMP	= 0x00000040,
	ATH10K_DBG_HTT_DUMP	= 0x00000080,
	ATH10K_DBG_MGMT		= 0x00000100,
	ATH10K_DBG_DATA		= 0x00000200,
	ATH10K_DBG_BMI		= 0x00000400,
	ATH10K_DBG_ANY		= 0xffffffff,
};

extern unsigned int ath10k_debug_mask;

__printf(1, 2) int ath10k_info(const char *fmt, ...);
__printf(1, 2) int ath10k_err(const char *fmt, ...);
__printf(1, 2) int ath10k_warn(const char *fmt, ...);

#ifdef CONFIG_ATH10K_DEBUGFS
int ath10k_debug_start(struct ath10k *ar);
void ath10k_debug_stop(struct ath10k *ar);
int ath10k_debug_create(struct ath10k *ar);
void ath10k_debug_read_service_map(struct ath10k *ar,
				   void *service_map,
				   size_t map_size);
void ath10k_debug_read_target_stats(struct ath10k *ar,
				    struct wmi_stats_event *ev);

#else
static inline int ath10k_debug_start(struct ath10k *ar)
{
	return 0;
}

static inline void ath10k_debug_stop(struct ath10k *ar)
{
}

static inline int ath10k_debug_create(struct ath10k *ar)
{
	return 0;
}

static inline void ath10k_debug_read_service_map(struct ath10k *ar,
						 void *service_map,
						 size_t map_size)
{
}

static inline void ath10k_debug_read_target_stats(struct ath10k *ar,
						  struct wmi_stats_event *ev)
{
}
#endif /* CONFIG_ATH10K_DEBUGFS */

#ifdef CONFIG_ATH10K_DEBUG
__printf(2, 3) void ath10k_dbg(enum ath10k_debug_mask mask,
				      const char *fmt, ...);
void ath10k_dbg_dump(enum ath10k_debug_mask mask,
		     const char *msg, const char *prefix,
		     const void *buf, size_t len);
#else /* CONFIG_ATH10K_DEBUG */

static inline int ath10k_dbg(enum ath10k_debug_mask dbg_mask,
			     const char *fmt, ...)
{
	return 0;
}

static inline void ath10k_dbg_dump(enum ath10k_debug_mask mask,
				   const char *msg, const char *prefix,
				   const void *buf, size_t len)
{
}
#endif /* CONFIG_ATH10K_DEBUG */
#endif /* _DEBUG_H_ */
