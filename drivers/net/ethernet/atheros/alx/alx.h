/*
 * Copyright (c) 2013 Johannes Berg <johannes@sipsolutions.net>
 *
 *  This file is free software: you may copy, redistribute and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation, either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This file is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 * Copyright (c) 2012 Qualcomm Atheros, Inc.
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

#ifndef _ALX_H_
#define _ALX_H_

#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include "hw.h"

#define ALX_WATCHDOG_TIME   (5 * HZ)

struct alx_buffer {
	struct sk_buff *skb;
	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(size);
};

struct alx_rx_queue {
	struct alx_rrd *rrd;
	dma_addr_t rrd_dma;

	struct alx_rfd *rfd;
	dma_addr_t rfd_dma;

	struct alx_buffer *bufs;

	u16 write_idx, read_idx;
	u16 rrd_read_idx;
};
#define ALX_RX_ALLOC_THRESH	32

struct alx_tx_queue {
	struct alx_txd *tpd;
	dma_addr_t tpd_dma;
	struct alx_buffer *bufs;
	u16 write_idx, read_idx;
};

#define ALX_DEFAULT_TX_WORK 128

enum alx_device_quirks {
	ALX_DEV_QUIRK_MSI_INTX_DISABLE_BUG = BIT(0),
};

#define ALX_FLAG_USING_MSIX	BIT(0)
#define ALX_FLAG_USING_MSI	BIT(1)

struct alx_priv {
	struct net_device *dev;

	struct alx_hw hw;

	/* msi-x vectors */
	int num_vec;
	struct msix_entry *msix_entries;
	char irq_lbl[IFNAMSIZ + 8];

	/* all descriptor memory */
	struct {
		dma_addr_t dma;
		void *virt;
		unsigned int size;
	} descmem;

	/* protect int_mask updates */
	spinlock_t irq_lock;
	u32 int_mask;

	unsigned int tx_ringsz;
	unsigned int rx_ringsz;
	unsigned int rxbuf_size;

	struct napi_struct napi;
	struct alx_tx_queue txq;
	struct alx_rx_queue rxq;

	struct work_struct link_check_wk;
	struct work_struct reset_wk;

	u16 msg_enable;

	int flags;

	/* protects hw.stats */
	spinlock_t stats_lock;
};

extern const struct ethtool_ops alx_ethtool_ops;
extern const char alx_drv_name[];

#endif
