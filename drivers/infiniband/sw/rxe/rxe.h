/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef RXE_H
#define RXE_H

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/crc32.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_pack.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_addr.h>
#include <crypto/hash.h>

#include "rxe_net.h"
#include "rxe_opcode.h"
#include "rxe_hdr.h"
#include "rxe_param.h"
#include "rxe_verbs.h"
#include "rxe_loc.h"

/*
 * Version 1 and Version 2 are identical on 64 bit machines, but on 32 bit
 * machines Version 2 has a different struct layout.
 */
#define RXE_UVERBS_ABI_VERSION		2

#define RDMA_LINK_PHYS_STATE_LINK_UP	(5)
#define RDMA_LINK_PHYS_STATE_DISABLED	(3)
#define RDMA_LINK_PHYS_STATE_POLLING	(2)

#define RXE_ROCE_V2_SPORT		(0xc000)

static inline u32 rxe_crc32(struct rxe_dev *rxe,
			    u32 crc, void *next, size_t len)
{
	u32 retval;
	int err;

	SHASH_DESC_ON_STACK(shash, rxe->tfm);

	shash->tfm = rxe->tfm;
	shash->flags = 0;
	*(u32 *)shash_desc_ctx(shash) = crc;
	err = crypto_shash_update(shash, next, len);
	if (unlikely(err)) {
		pr_warn_ratelimited("failed crc calculation, err: %d\n", err);
		return crc32_le(crc, next, len);
	}

	retval = *(u32 *)shash_desc_ctx(shash);
	barrier_data(shash_desc_ctx(shash));
	return retval;
}

void rxe_set_mtu(struct rxe_dev *rxe, unsigned int dev_mtu);

int rxe_add(struct rxe_dev *rxe, unsigned int mtu);

void rxe_rcv(struct sk_buff *skb);

struct rxe_dev *get_rxe_by_name(const char *name);

/* The caller must do a matching ib_device_put(&dev->ib_dev) */
static inline struct rxe_dev *rxe_get_dev_from_net(struct net_device *ndev)
{
	struct ib_device *ibdev =
		ib_device_get_by_netdev(ndev, RDMA_DRIVER_RXE);

	if (!ibdev)
		return NULL;
	return container_of(ibdev, struct rxe_dev, ib_dev);
}

void rxe_port_up(struct rxe_dev *rxe);
void rxe_port_down(struct rxe_dev *rxe);
void rxe_set_port_state(struct rxe_dev *rxe);

#endif /* RXE_H */
