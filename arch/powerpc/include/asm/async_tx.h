/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2008-2009 DENX Software Engineering.
 *
 * Author: Yuri Tikhonov <yur@emcraft.com>
 */
#ifndef _ASM_POWERPC_ASYNC_TX_H_
#define _ASM_POWERPC_ASYNC_TX_H_

#if defined(CONFIG_440SPe) || defined(CONFIG_440SP)
extern struct dma_chan *
ppc440spe_async_tx_find_best_channel(enum dma_transaction_type cap,
	struct page **dst_lst, int dst_cnt, struct page **src_lst,
	int src_cnt, size_t src_sz);

#define async_tx_find_channel(dep, cap, dst_lst, dst_cnt, src_lst, \
			      src_cnt, src_sz) \
	ppc440spe_async_tx_find_best_channel(cap, dst_lst, dst_cnt, src_lst, \
					     src_cnt, src_sz)
#else

#define async_tx_find_channel(dep, type, dst, dst_count, src, src_count, len) \
	__async_tx_find_channel(dep, type)

struct dma_chan *
__async_tx_find_channel(struct async_submit_ctl *submit,
			enum dma_transaction_type tx_type);

#endif

#endif
