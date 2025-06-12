// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#define DEFAULT_SYMBOL_NAMESPACE	"LIBETH_XDP"

#include <linux/export.h>

#include <net/libeth/xsk.h>

#include "priv.h"

/* ``XDP_TX`` bulking */

void __cold libeth_xsk_tx_return_bulk(const struct libeth_xdp_tx_frame *bq,
				      u32 count)
{
	for (u32 i = 0; i < count; i++)
		libeth_xsk_buff_free_slow(bq[i].xsk);
}

/* Rx polling path */

/**
 * libeth_xsk_buff_free_slow - free an XSk Rx buffer
 * @xdp: buffer to free
 *
 * Slowpath version of xsk_buff_free() to be used on exceptions, cleanups etc.
 * to avoid unwanted inlining.
 */
void libeth_xsk_buff_free_slow(struct libeth_xdp_buff *xdp)
{
	xsk_buff_free(&xdp->base);
}
EXPORT_SYMBOL_GPL(libeth_xsk_buff_free_slow);
