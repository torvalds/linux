/* SPDX-License-Identifier: GPL-2.0 */
/*
 *   Driver for KeyStream, KS7010 based SDIO cards.
 *
 *   Copyright (C) 2006-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 */
#ifndef _KS7010_SDIO_H
#define _KS7010_SDIO_H

struct ks_wlan_private;

/**
 * struct ks_sdio_card - SDIO device data.
 *
 * Structure is used as the &struct sdio_func private data.
 *
 * @func: Pointer to the SDIO function device.
 * @priv: Pointer to the &struct net_device private data.
 */
struct ks_sdio_card {
	struct sdio_func *func;
	struct ks_wlan_private *priv;
};

#endif /* _KS7010_SDIO_H */
