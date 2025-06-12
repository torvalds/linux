/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef __LIBETH_PRIV_H
#define __LIBETH_PRIV_H

#include <linux/types.h>

/* XDP */

struct skb_shared_info;
struct xdp_frame_bulk;

struct libeth_xdp_ops {
	void	(*bulk)(const struct skb_shared_info *sinfo,
			struct xdp_frame_bulk *bq, bool frags);
};

void libeth_attach_xdp(const struct libeth_xdp_ops *ops);

static inline void libeth_detach_xdp(void)
{
	libeth_attach_xdp(NULL);
}

#endif /* __LIBETH_PRIV_H */
