/*
 * Copyright (c) 2012 Broadcom Corporation
 * Copyright (c) 2012 Canonical Ltd.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef _BRCMS_DEBUG_H_
#define _BRCMS_DEBUG_H_

#include <linux/device.h>
#include <linux/bcma/bcma.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include "main.h"
#include "mac80211_if.h"

__printf(2, 3)
void __brcms_info(struct device *dev, const char *fmt, ...);
__printf(2, 3)
void __brcms_warn(struct device *dev, const char *fmt, ...);
__printf(2, 3)
void __brcms_err(struct device *dev, const char *fmt, ...);
__printf(2, 3)
void __brcms_crit(struct device *dev, const char *fmt, ...);

#if defined(CONFIG_BRCMDBG) || defined(CONFIG_BRCM_TRACING)
__printf(4, 5)
void __brcms_dbg(struct device *dev, u32 level, const char *func,
		 const char *fmt, ...);
#else
static inline __printf(4, 5)
void __brcms_dbg(struct device *dev, u32 level, const char *func,
		 const char *fmt, ...)
{
}
#endif

/*
 * Debug macros cannot be used when wlc is uninitialized. Generally
 * this means any code that could run before brcms_c_attach() has
 * returned successfully probably shouldn't use the following macros.
 */

#define brcms_dbg(core, l, f, a...)	__brcms_dbg(&(core)->dev, l, __func__, f, ##a)
#define brcms_info(core, f, a...)	__brcms_info(&(core)->dev, f, ##a)
#define brcms_warn(core, f, a...)	__brcms_warn(&(core)->dev, f, ##a)
#define brcms_err(core, f, a...)	__brcms_err(&(core)->dev, f, ##a)
#define brcms_crit(core, f, a...)	__brcms_crit(&(core)->dev, f, ##a)

#define brcms_dbg_info(core, f, a...)		brcms_dbg(core, BRCM_DL_INFO, f, ##a)
#define brcms_dbg_mac80211(core, f, a...)	brcms_dbg(core, BRCM_DL_MAC80211, f, ##a)
#define brcms_dbg_rx(core, f, a...)		brcms_dbg(core, BRCM_DL_RX, f, ##a)
#define brcms_dbg_tx(core, f, a...)		brcms_dbg(core, BRCM_DL_TX, f, ##a)
#define brcms_dbg_int(core, f, a...)		brcms_dbg(core, BRCM_DL_INT, f, ##a)
#define brcms_dbg_dma(core, f, a...)		brcms_dbg(core, BRCM_DL_DMA, f, ##a)
#define brcms_dbg_ht(core, f, a...)		brcms_dbg(core, BRCM_DL_HT, f, ##a)

struct brcms_pub;
void brcms_debugfs_init(void);
void brcms_debugfs_exit(void);
void brcms_debugfs_attach(struct brcms_pub *drvr);
void brcms_debugfs_detach(struct brcms_pub *drvr);
struct dentry *brcms_debugfs_get_devdir(struct brcms_pub *drvr);
void brcms_debugfs_create_files(struct brcms_pub *drvr);

#endif /* _BRCMS_DEBUG_H_ */
