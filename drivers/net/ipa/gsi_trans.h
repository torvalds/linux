/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */
#ifndef _GSI_TRANS_H_
#define _GSI_TRANS_H_

#include <linux/types.h>
#include <linux/refcount.h>
#include <linux/completion.h>
#include <linux/dma-direction.h>

#include "ipa_cmd.h"

struct page;
struct scatterlist;
struct device;
struct sk_buff;

struct gsi;
struct gsi_trans;
struct gsi_trans_pool;

/* Maximum number of TREs in an IPA immediate command transaction */
#define IPA_COMMAND_TRANS_TRE_MAX	8

/**
 * struct gsi_trans - a GSI transaction
 *
 * Most fields in this structure for internal use by the transaction core code:
 * @links:	Links for channel transaction lists by state
 * @gsi:	GSI pointer
 * @channel_id: Channel number transaction is associated with
 * @cancelled:	If set by the core code, transaction was cancelled
 * @tre_count:	Number of TREs reserved for this transaction
 * @used:	Number of TREs *used* (could be less than tre_count)
 * @len:	Total # of transfer bytes represented in sgl[] (set by core)
 * @data:	Preserved but not touched by the core transaction code
 * @cmd_opcode:	Array of command opcodes (command channel only)
 * @sgl:	An array of scatter/gather entries managed by core code
 * @direction:	DMA transfer direction (DMA_NONE for commands)
 * @refcount:	Reference count used for destruction
 * @completion:	Completed when the transaction completes
 * @byte_count:	TX channel byte count recorded when transaction committed
 * @trans_count: Channel transaction count when committed (for BQL accounting)
 *
 * The size used for some fields in this structure were chosen to ensure
 * the full structure size is no larger than 128 bytes.
 */
struct gsi_trans {
	struct list_head links;		/* gsi_channel lists */

	struct gsi *gsi;
	u8 channel_id;

	bool cancelled;			/* true if transaction was cancelled */

	u8 tre_count;			/* # TREs requested */
	u8 used;			/* # entries used in sgl[] */
	u32 len;			/* total # bytes across sgl[] */

	union {
		void *data;
		u8 cmd_opcode[IPA_COMMAND_TRANS_TRE_MAX];
	};
	struct scatterlist *sgl;
	enum dma_data_direction direction;

	refcount_t refcount;
	struct completion completion;

	u64 byte_count;			/* channel byte_count when committed */
	u64 trans_count;		/* channel trans_count when committed */
};

/**
 * gsi_trans_pool_init() - Initialize a pool of structures for transactions
 * @pool:	GSI transaction poll pointer
 * @size:	Size of elements in the pool
 * @count:	Minimum number of elements in the pool
 * @max_alloc:	Maximum number of elements allocated at a time from pool
 *
 * Return:	0 if successful, or a negative error code
 */
int gsi_trans_pool_init(struct gsi_trans_pool *pool, size_t size, u32 count,
			u32 max_alloc);

/**
 * gsi_trans_pool_alloc() - Allocate one or more elements from a pool
 * @pool:	Pool pointer
 * @count:	Number of elements to allocate from the pool
 *
 * Return:	Virtual address of element(s) allocated from the pool
 */
void *gsi_trans_pool_alloc(struct gsi_trans_pool *pool, u32 count);

/**
 * gsi_trans_pool_exit() - Inverse of gsi_trans_pool_init()
 * @pool:	Pool pointer
 */
void gsi_trans_pool_exit(struct gsi_trans_pool *pool);

/**
 * gsi_trans_pool_init_dma() - Initialize a pool of DMA-able structures
 * @dev:	Device used for DMA
 * @pool:	Pool pointer
 * @size:	Size of elements in the pool
 * @count:	Minimum number of elements in the pool
 * @max_alloc:	Maximum number of elements allocated at a time from pool
 *
 * Return:	0 if successful, or a negative error code
 *
 * Structures in this pool reside in DMA-coherent memory.
 */
int gsi_trans_pool_init_dma(struct device *dev, struct gsi_trans_pool *pool,
			    size_t size, u32 count, u32 max_alloc);

/**
 * gsi_trans_pool_alloc_dma() - Allocate an element from a DMA pool
 * @pool:	DMA pool pointer
 * @addr:	DMA address "handle" associated with the allocation
 *
 * Return:	Virtual address of element allocated from the pool
 *
 * Only one element at a time may be allocated from a DMA pool.
 */
void *gsi_trans_pool_alloc_dma(struct gsi_trans_pool *pool, dma_addr_t *addr);

/**
 * gsi_trans_pool_exit_dma() - Inverse of gsi_trans_pool_init_dma()
 * @dev:	Device used for DMA
 * @pool:	Pool pointer
 */
void gsi_trans_pool_exit_dma(struct device *dev, struct gsi_trans_pool *pool);

/**
 * gsi_channel_trans_idle() - Return whether no transactions are allocated
 * @gsi:	GSI pointer
 * @channel_id:	Channel the transaction is associated with
 *
 * Return:	True if no transactions are allocated, false otherwise
 *
 */
bool gsi_channel_trans_idle(struct gsi *gsi, u32 channel_id);

/**
 * gsi_channel_trans_alloc() - Allocate a GSI transaction on a channel
 * @gsi:	GSI pointer
 * @channel_id:	Channel the transaction is associated with
 * @tre_count:	Number of elements in the transaction
 * @direction:	DMA direction for entire SGL (or DMA_NONE)
 *
 * Return:	A GSI transaction structure, or a null pointer if all
 *		available transactions are in use
 */
struct gsi_trans *gsi_channel_trans_alloc(struct gsi *gsi, u32 channel_id,
					  u32 tre_count,
					  enum dma_data_direction direction);

/**
 * gsi_trans_free() - Free a previously-allocated GSI transaction
 * @trans:	Transaction to be freed
 */
void gsi_trans_free(struct gsi_trans *trans);

/**
 * gsi_trans_cmd_add() - Add an immediate command to a transaction
 * @trans:	Transaction
 * @buf:	Buffer pointer for command payload
 * @size:	Number of bytes in buffer
 * @addr:	DMA address for payload
 * @opcode:	IPA immediate command opcode
 */
void gsi_trans_cmd_add(struct gsi_trans *trans, void *buf, u32 size,
		       dma_addr_t addr, enum ipa_cmd_opcode opcode);

/**
 * gsi_trans_page_add() - Add a page transfer to a transaction
 * @trans:	Transaction
 * @page:	Page pointer
 * @size:	Number of bytes (starting at offset) to transfer
 * @offset:	Offset within page for start of transfer
 */
int gsi_trans_page_add(struct gsi_trans *trans, struct page *page, u32 size,
		       u32 offset);

/**
 * gsi_trans_skb_add() - Add a socket transfer to a transaction
 * @trans:	Transaction
 * @skb:	Socket buffer for transfer (outbound)
 *
 * Return:	0, or -EMSGSIZE if socket data won't fit in transaction.
 */
int gsi_trans_skb_add(struct gsi_trans *trans, struct sk_buff *skb);

/**
 * gsi_trans_commit() - Commit a GSI transaction
 * @trans:	Transaction to commit
 * @ring_db:	Whether to tell the hardware about these queued transfers
 */
void gsi_trans_commit(struct gsi_trans *trans, bool ring_db);

/**
 * gsi_trans_commit_wait() - Commit a GSI transaction and wait for it
 *			     to complete
 * @trans:	Transaction to commit
 */
void gsi_trans_commit_wait(struct gsi_trans *trans);

/**
 * gsi_trans_read_byte() - Issue a single byte read TRE on a channel
 * @gsi:	GSI pointer
 * @channel_id:	Channel on which to read a byte
 * @addr:	DMA address into which to transfer the one byte
 *
 * This is not a transaction operation at all.  It's defined here because
 * it needs to be done in coordination with other transaction activity.
 */
int gsi_trans_read_byte(struct gsi *gsi, u32 channel_id, dma_addr_t addr);

/**
 * gsi_trans_read_byte_done() - Clean up after a single byte read TRE
 * @gsi:	GSI pointer
 * @channel_id:	Channel on which byte was read
 *
 * This function needs to be called to signal that the work related
 * to reading a byte initiated by gsi_trans_read_byte() is complete.
 */
void gsi_trans_read_byte_done(struct gsi *gsi, u32 channel_id);

#endif /* _GSI_TRANS_H_ */
