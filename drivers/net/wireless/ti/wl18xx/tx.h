/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2011 Texas Instruments. All rights reserved.
 */

#ifndef __WL18XX_TX_H__
#define __WL18XX_TX_H__

#include "../wlcore/wlcore.h"

#define WL18XX_TX_HW_BLOCK_SPARE        1
/* for special cases - namely, TKIP and GEM */
#define WL18XX_TX_HW_EXTRA_BLOCK_SPARE  2
#define WL18XX_TX_HW_BLOCK_SIZE         268

#define WL18XX_TX_STATUS_DESC_ID_MASK    0x7F
#define WL18XX_TX_STATUS_STAT_BIT_IDX    7

/* Indicates this TX HW frame is not padded to SDIO block size */
#define WL18XX_TX_CTRL_NOT_PADDED	BIT(7)

/*
 * The FW uses a special bit to indicate a wide channel should be used in
 * the rate policy.
 */
#define CONF_TX_RATE_USE_WIDE_CHAN BIT(31)

void wl18xx_tx_immediate_complete(struct wl1271 *wl);

#endif /* __WL12XX_TX_H__ */
