// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */

#include <linux/types.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/refcount.h>
#include <linux/scatterlist.h>
#include <linux/dma-direction.h>

#include "gsi.h"
#include "gsi_private.h"
#include "gsi_trans.h"
#include "ipa_gsi.h"
#include "ipa_data.h"
#include "ipa_cmd.h"

/**
 * DOC: GSI Transactions
 *
 * A GSI transaction abstracts the behavior of a GSI channel by representing
 * everything about a related group of IPA commands in a single structure.
 * (A "command" in this sense is either a data transfer or an IPA immediate
 * command.)  Most details of interaction with the GSI hardware are managed
 * by the GSI transaction core, allowing users to simply describe commands
 * to be performed.  When a transaction has completed a callback function
 * (dependent on the type of endpoint associated with the channel) allows
 * cleanup of resources associated with the transaction.
 *
 * To perform a command (or set of them), a user of the GSI transaction
 * interface allocates a transaction, indicating the number of TREs required
 * (one per command).  If sufficient TREs are available, they are reserved
 * for use in the transaction and the allocation succeeds.  This way
 * exhaustion of the available TREs in a channel ring is detected
 * as early as possible.  All resources required to complete a transaction
 * are allocated at transaction allocation time.
 *
 * Commands performed as part of a transaction are represented in an array
 * of Linux scatterlist structures.  This array is allocated with the
 * transaction, and its entries are initialized using standard scatterlist
 * functions (such as sg_set_buf() or skb_to_sgvec()).
 *
 * Once a transaction's scatterlist structures have been initialized, the
 * transaction is committed.  The caller is responsible for mapping buffers
 * for DMA if necessary, and this should be done *before* allocating
 * the transaction.  Between a successful allocation and commit of a
 * transaction no errors should occur.
 *
 * Committing transfers ownership of the entire transaction to the GSI
 * transaction core.  The GSI transaction code formats the content of
 * the scatterlist array into the channel ring buffer and informs the
 * hardware that new TREs are available to process.
 *
 * The last TRE in each transaction is marked to interrupt the AP when the
 * GSI hardware has completed it.  Because transfers described by TREs are
 * performed strictly in order, signaling the completion of just the last
 * TRE in the transaction is sufficient to indicate the full transaction
 * is complete.
 *
 * When a transaction is complete, ipa_gsi_trans_complete() is called by the
 * GSI code into the IPA layer, allowing it to perform any final cleanup
 * required before the transaction is freed.
 */

/* Hardware values representing a transfer element type */
enum gsi_tre_type {
	GSI_RE_XFER	= 0x2,
	GSI_RE_IMMD_CMD	= 0x3,
};

/* An entry in a channel ring */
struct gsi_tre {
	__le64 addr;		/* DMA address */
	__le16 len_opcode;	/* length in bytes or enum IPA_CMD_* */
	__le16 reserved;
	__le32 flags;		/* TRE_FLAGS_* */
};

/* gsi_tre->flags mask values (in CPU byte order) */
#define TRE_FLAGS_CHAIN_FMASK	GENMASK(0, 0)
#define TRE_FLAGS_IEOT_FMASK	GENMASK(9, 9)
#define TRE_FLAGS_BEI_FMASK	GENMASK(10, 10)
#define TRE_FLAGS_TYPE_FMASK	GENMASK(23, 16)

int gsi_trans_pool_init(struct gsi_trans_pool *pool, size_t size, u32 count,
			u32 max_alloc)
{
	void *virt;

	if (!size)
		return -EINVAL;
	if (count < max_alloc)
		return -EINVAL;
	if (!max_alloc)
		return -EINVAL;

	/* By allocating a few extra entries in our pool (one less
	 * than the maximum number that will be requested in a
	 * single allocation), we can always satisfy requests without
	 * ever worrying about straddling the end of the pool array.
	 * If there aren't enough entries starting at the free index,
	 * we just allocate free entries from the beginning of the pool.
	 */
	virt = kcalloc(count + max_alloc - 1, size, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	pool->base = virt;
	/* If the allocator gave us any extra memory, use it */
	pool->count = ksize(pool->base) / size;
	pool->free = 0;
	pool->max_alloc = max_alloc;
	pool->size = size;
	pool->addr = 0;		/* Only used for DMA pools */

	return 0;
}

void gsi_trans_pool_exit(struct gsi_trans_pool *pool)
{
	kfree(pool->base);
	memset(pool, 0, sizeof(*pool));
}

/* Allocate the requested number of (zeroed) entries from the pool */
/* Home-grown DMA pool.  This way we can preallocate and use the tre_count
 * to guarantee allocations will succeed.  Even though we specify max_alloc
 * (and it can be more than one), we only allow allocation of a single
 * element from a DMA pool.
 */
int gsi_trans_pool_init_dma(struct device *dev, struct gsi_trans_pool *pool,
			    size_t size, u32 count, u32 max_alloc)
{
	size_t total_size;
	dma_addr_t addr;
	void *virt;

	if (!size)
		return -EINVAL;
	if (count < max_alloc)
		return -EINVAL;
	if (!max_alloc)
		return -EINVAL;

	/* Don't let allocations cross a power-of-two boundary */
	size = __roundup_pow_of_two(size);
	total_size = (count + max_alloc - 1) * size;

	/* The allocator will give us a power-of-2 number of pages
	 * sufficient to satisfy our request.  Round up our requested
	 * size to avoid any unused space in the allocation.  This way
	 * gsi_trans_pool_exit_dma() can assume the total allocated
	 * size is exactly (count * size).
	 */
	total_size = get_order(total_size) << PAGE_SHIFT;

	virt = dma_alloc_coherent(dev, total_size, &addr, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	pool->base = virt;
	pool->count = total_size / size;
	pool->free = 0;
	pool->size = size;
	pool->max_alloc = max_alloc;
	pool->addr = addr;

	return 0;
}

void gsi_trans_pool_exit_dma(struct device *dev, struct gsi_trans_pool *pool)
{
	size_t total_size = pool->count * pool->size;

	dma_free_coherent(dev, total_size, pool->base, pool->addr);
	memset(pool, 0, sizeof(*pool));
}

/* Return the byte offset of the next free entry in the pool */
static u32 gsi_trans_pool_alloc_common(struct gsi_trans_pool *pool, u32 count)
{
	u32 offset;

	WARN_ON(!count);
	WARN_ON(count > pool->max_alloc);

	/* Allocate from beginning if wrap would occur */
	if (count > pool->count - pool->free)
		pool->free = 0;

	offset = pool->free * pool->size;
	pool->free += count;
	memset(pool->base + offset, 0, count * pool->size);

	return offset;
}

/* Allocate a contiguous block of zeroed entries from a pool */
void *gsi_trans_pool_alloc(struct gsi_trans_pool *pool, u32 count)
{
	return pool->base + gsi_trans_pool_alloc_common(pool, count);
}

/* Allocate a single zeroed entry from a DMA pool */
void *gsi_trans_pool_alloc_dma(struct gsi_trans_pool *pool, dma_addr_t *addr)
{
	u32 offset = gsi_trans_pool_alloc_common(pool, 1);

	*addr = pool->addr + offset;

	return pool->base + offset;
}

/* Return the pool element that immediately follows the one given.
 * This only works done if elements are allocated one at a time.
 */
void *gsi_trans_pool_next(struct gsi_trans_pool *pool, void *element)
{
	void *end = pool->base + pool->count * pool->size;

	WARN_ON(element < pool->base);
	WARN_ON(element >= end);
	WARN_ON(pool->max_alloc != 1);

	element += pool->size;

	return element < end ? element : pool->base;
}

/* Map a given ring entry index to the transaction associated with it */
static void gsi_channel_trans_map(struct gsi_channel *channel, u32 index,
				  struct gsi_trans *trans)
{
	/* Note: index *must* be used modulo the ring count here */
	channel->trans_info.map[index % channel->tre_ring.count] = trans;
}

/* Return the transaction mapped to a given ring entry */
struct gsi_trans *
gsi_channel_trans_mapped(struct gsi_channel *channel, u32 index)
{
	/* Note: index *must* be used modulo the ring count here */
	return channel->trans_info.map[index % channel->tre_ring.count];
}

/* Return the oldest completed transaction for a channel (or null) */
struct gsi_trans *gsi_channel_trans_complete(struct gsi_channel *channel)
{
	return list_first_entry_or_null(&channel->trans_info.complete,
					struct gsi_trans, links);
}

/* Move a transaction from the allocated list to the pending list */
static void gsi_trans_move_pending(struct gsi_trans *trans)
{
	struct gsi_channel *channel = &trans->gsi->channel[trans->channel_id];
	struct gsi_trans_info *trans_info = &channel->trans_info;

	spin_lock_bh(&trans_info->spinlock);

	list_move_tail(&trans->links, &trans_info->pending);

	spin_unlock_bh(&trans_info->spinlock);
}

/* Move a transaction and all of its predecessors from the pending list
 * to the completed list.
 */
void gsi_trans_move_complete(struct gsi_trans *trans)
{
	struct gsi_channel *channel = &trans->gsi->channel[trans->channel_id];
	struct gsi_trans_info *trans_info = &channel->trans_info;
	struct list_head list;

	spin_lock_bh(&trans_info->spinlock);

	/* Move this transaction and all predecessors to completed list */
	list_cut_position(&list, &trans_info->pending, &trans->links);
	list_splice_tail(&list, &trans_info->complete);

	spin_unlock_bh(&trans_info->spinlock);
}

/* Move a transaction from the completed list to the polled list */
void gsi_trans_move_polled(struct gsi_trans *trans)
{
	struct gsi_channel *channel = &trans->gsi->channel[trans->channel_id];
	struct gsi_trans_info *trans_info = &channel->trans_info;

	spin_lock_bh(&trans_info->spinlock);

	list_move_tail(&trans->links, &trans_info->polled);

	spin_unlock_bh(&trans_info->spinlock);
}

/* Reserve some number of TREs on a channel.  Returns true if successful */
static bool
gsi_trans_tre_reserve(struct gsi_trans_info *trans_info, u32 tre_count)
{
	int avail = atomic_read(&trans_info->tre_avail);
	int new;

	do {
		new = avail - (int)tre_count;
		if (unlikely(new < 0))
			return false;
	} while (!atomic_try_cmpxchg(&trans_info->tre_avail, &avail, new));

	return true;
}

/* Release previously-reserved TRE entries to a channel */
static void
gsi_trans_tre_release(struct gsi_trans_info *trans_info, u32 tre_count)
{
	atomic_add(tre_count, &trans_info->tre_avail);
}

/* Return true if no transactions are allocated, false otherwise */
bool gsi_channel_trans_idle(struct gsi *gsi, u32 channel_id)
{
	u32 tre_max = gsi_channel_tre_max(gsi, channel_id);
	struct gsi_trans_info *trans_info;

	trans_info = &gsi->channel[channel_id].trans_info;

	return atomic_read(&trans_info->tre_avail) == tre_max;
}

/* Allocate a GSI transaction on a channel */
struct gsi_trans *gsi_channel_trans_alloc(struct gsi *gsi, u32 channel_id,
					  u32 tre_count,
					  enum dma_data_direction direction)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];
	struct gsi_trans_info *trans_info;
	struct gsi_trans *trans;

	if (WARN_ON(tre_count > gsi_channel_trans_tre_max(gsi, channel_id)))
		return NULL;

	trans_info = &channel->trans_info;

	/* We reserve the TREs now, but consume them at commit time.
	 * If there aren't enough available, we're done.
	 */
	if (!gsi_trans_tre_reserve(trans_info, tre_count))
		return NULL;

	/* Allocate and initialize non-zero fields in the the transaction */
	trans = gsi_trans_pool_alloc(&trans_info->pool, 1);
	trans->gsi = gsi;
	trans->channel_id = channel_id;
	trans->tre_count = tre_count;
	init_completion(&trans->completion);

	/* Allocate the scatterlist and (if requested) info entries. */
	trans->sgl = gsi_trans_pool_alloc(&trans_info->sg_pool, tre_count);
	sg_init_marker(trans->sgl, tre_count);

	trans->direction = direction;

	spin_lock_bh(&trans_info->spinlock);

	list_add_tail(&trans->links, &trans_info->alloc);

	spin_unlock_bh(&trans_info->spinlock);

	refcount_set(&trans->refcount, 1);

	return trans;
}

/* Free a previously-allocated transaction */
void gsi_trans_free(struct gsi_trans *trans)
{
	refcount_t *refcount = &trans->refcount;
	struct gsi_trans_info *trans_info;
	bool last;

	/* We must hold the lock to release the last reference */
	if (refcount_dec_not_one(refcount))
		return;

	trans_info = &trans->gsi->channel[trans->channel_id].trans_info;

	spin_lock_bh(&trans_info->spinlock);

	/* Reference might have been added before we got the lock */
	last = refcount_dec_and_test(refcount);
	if (last)
		list_del(&trans->links);

	spin_unlock_bh(&trans_info->spinlock);

	if (!last)
		return;

	ipa_gsi_trans_release(trans);

	/* Releasing the reserved TREs implicitly frees the sgl[] and
	 * (if present) info[] arrays, plus the transaction itself.
	 */
	gsi_trans_tre_release(trans_info, trans->tre_count);
}

/* Add an immediate command to a transaction */
void gsi_trans_cmd_add(struct gsi_trans *trans, void *buf, u32 size,
		       dma_addr_t addr, enum dma_data_direction direction,
		       enum ipa_cmd_opcode opcode)
{
	struct ipa_cmd_info *info;
	u32 which = trans->used++;
	struct scatterlist *sg;

	WARN_ON(which >= trans->tre_count);

	/* Commands are quite different from data transfer requests.
	 * Their payloads come from a pool whose memory is allocated
	 * using dma_alloc_coherent().  We therefore do *not* map them
	 * for DMA (unlike what we do for pages and skbs).
	 *
	 * When a transaction completes, the SGL is normally unmapped.
	 * A command transaction has direction DMA_NONE, which tells
	 * gsi_trans_complete() to skip the unmapping step.
	 *
	 * The only things we use directly in a command scatter/gather
	 * entry are the DMA address and length.  We still need the SG
	 * table flags to be maintained though, so assign a NULL page
	 * pointer for that purpose.
	 */
	sg = &trans->sgl[which];
	sg_assign_page(sg, NULL);
	sg_dma_address(sg) = addr;
	sg_dma_len(sg) = size;

	info = &trans->info[which];
	info->opcode = opcode;
	info->direction = direction;
}

/* Add a page transfer to a transaction.  It will fill the only TRE. */
int gsi_trans_page_add(struct gsi_trans *trans, struct page *page, u32 size,
		       u32 offset)
{
	struct scatterlist *sg = &trans->sgl[0];
	int ret;

	if (WARN_ON(trans->tre_count != 1))
		return -EINVAL;
	if (WARN_ON(trans->used))
		return -EINVAL;

	sg_set_page(sg, page, size, offset);
	ret = dma_map_sg(trans->gsi->dev, sg, 1, trans->direction);
	if (!ret)
		return -ENOMEM;

	trans->used++;	/* Transaction now owns the (DMA mapped) page */

	return 0;
}

/* Add an SKB transfer to a transaction.  No other TREs will be used. */
int gsi_trans_skb_add(struct gsi_trans *trans, struct sk_buff *skb)
{
	struct scatterlist *sg = &trans->sgl[0];
	u32 used;
	int ret;

	if (WARN_ON(trans->tre_count != 1))
		return -EINVAL;
	if (WARN_ON(trans->used))
		return -EINVAL;

	/* skb->len will not be 0 (checked early) */
	ret = skb_to_sgvec(skb, sg, 0, skb->len);
	if (ret < 0)
		return ret;
	used = ret;

	ret = dma_map_sg(trans->gsi->dev, sg, used, trans->direction);
	if (!ret)
		return -ENOMEM;

	trans->used += used;	/* Transaction now owns the (DMA mapped) skb */

	return 0;
}

/* Compute the length/opcode value to use for a TRE */
static __le16 gsi_tre_len_opcode(enum ipa_cmd_opcode opcode, u32 len)
{
	return opcode == IPA_CMD_NONE ? cpu_to_le16((u16)len)
				      : cpu_to_le16((u16)opcode);
}

/* Compute the flags value to use for a given TRE */
static __le32 gsi_tre_flags(bool last_tre, bool bei, enum ipa_cmd_opcode opcode)
{
	enum gsi_tre_type tre_type;
	u32 tre_flags;

	tre_type = opcode == IPA_CMD_NONE ? GSI_RE_XFER : GSI_RE_IMMD_CMD;
	tre_flags = u32_encode_bits(tre_type, TRE_FLAGS_TYPE_FMASK);

	/* Last TRE contains interrupt flags */
	if (last_tre) {
		/* All transactions end in a transfer completion interrupt */
		tre_flags |= TRE_FLAGS_IEOT_FMASK;
		/* Don't interrupt when outbound commands are acknowledged */
		if (bei)
			tre_flags |= TRE_FLAGS_BEI_FMASK;
	} else {	/* All others indicate there's more to come */
		tre_flags |= TRE_FLAGS_CHAIN_FMASK;
	}

	return cpu_to_le32(tre_flags);
}

static void gsi_trans_tre_fill(struct gsi_tre *dest_tre, dma_addr_t addr,
			       u32 len, bool last_tre, bool bei,
			       enum ipa_cmd_opcode opcode)
{
	struct gsi_tre tre;

	tre.addr = cpu_to_le64(addr);
	tre.len_opcode = gsi_tre_len_opcode(opcode, len);
	tre.reserved = 0;
	tre.flags = gsi_tre_flags(last_tre, bei, opcode);

	/* ARM64 can write 16 bytes as a unit with a single instruction.
	 * Doing the assignment this way is an attempt to make that happen.
	 */
	*dest_tre = tre;
}

/**
 * __gsi_trans_commit() - Common GSI transaction commit code
 * @trans:	Transaction to commit
 * @ring_db:	Whether to tell the hardware about these queued transfers
 *
 * Formats channel ring TRE entries based on the content of the scatterlist.
 * Maps a transaction pointer to the last ring entry used for the transaction,
 * so it can be recovered when it completes.  Moves the transaction to the
 * pending list.  Finally, updates the channel ring pointer and optionally
 * rings the doorbell.
 */
static void __gsi_trans_commit(struct gsi_trans *trans, bool ring_db)
{
	struct gsi_channel *channel = &trans->gsi->channel[trans->channel_id];
	struct gsi_ring *ring = &channel->tre_ring;
	enum ipa_cmd_opcode opcode = IPA_CMD_NONE;
	bool bei = channel->toward_ipa;
	struct ipa_cmd_info *info;
	struct gsi_tre *dest_tre;
	struct scatterlist *sg;
	u32 byte_count = 0;
	u32 avail;
	u32 i;

	WARN_ON(!trans->used);

	/* Consume the entries.  If we cross the end of the ring while
	 * filling them we'll switch to the beginning to finish.
	 * If there is no info array we're doing a simple data
	 * transfer request, whose opcode is IPA_CMD_NONE.
	 */
	info = trans->info ? &trans->info[0] : NULL;
	avail = ring->count - ring->index % ring->count;
	dest_tre = gsi_ring_virt(ring, ring->index);
	for_each_sg(trans->sgl, sg, trans->used, i) {
		bool last_tre = i == trans->used - 1;
		dma_addr_t addr = sg_dma_address(sg);
		u32 len = sg_dma_len(sg);

		byte_count += len;
		if (!avail--)
			dest_tre = gsi_ring_virt(ring, 0);
		if (info)
			opcode = info++->opcode;

		gsi_trans_tre_fill(dest_tre, addr, len, last_tre, bei, opcode);
		dest_tre++;
	}
	ring->index += trans->used;

	if (channel->toward_ipa) {
		/* We record TX bytes when they are sent */
		trans->len = byte_count;
		trans->trans_count = channel->trans_count;
		trans->byte_count = channel->byte_count;
		channel->trans_count++;
		channel->byte_count += byte_count;
	}

	/* Associate the last TRE with the transaction */
	gsi_channel_trans_map(channel, ring->index - 1, trans);

	gsi_trans_move_pending(trans);

	/* Ring doorbell if requested, or if all TREs are allocated */
	if (ring_db || !atomic_read(&channel->trans_info.tre_avail)) {
		/* Report what we're handing off to hardware for TX channels */
		if (channel->toward_ipa)
			gsi_channel_tx_queued(channel);
		gsi_channel_doorbell(channel);
	}
}

/* Commit a GSI transaction */
void gsi_trans_commit(struct gsi_trans *trans, bool ring_db)
{
	if (trans->used)
		__gsi_trans_commit(trans, ring_db);
	else
		gsi_trans_free(trans);
}

/* Commit a GSI transaction and wait for it to complete */
void gsi_trans_commit_wait(struct gsi_trans *trans)
{
	if (!trans->used)
		goto out_trans_free;

	refcount_inc(&trans->refcount);

	__gsi_trans_commit(trans, true);

	wait_for_completion(&trans->completion);

out_trans_free:
	gsi_trans_free(trans);
}

/* Commit a GSI transaction and wait for it to complete, with timeout */
int gsi_trans_commit_wait_timeout(struct gsi_trans *trans,
				  unsigned long timeout)
{
	unsigned long timeout_jiffies = msecs_to_jiffies(timeout);
	unsigned long remaining = 1;	/* In case of empty transaction */

	if (!trans->used)
		goto out_trans_free;

	refcount_inc(&trans->refcount);

	__gsi_trans_commit(trans, true);

	remaining = wait_for_completion_timeout(&trans->completion,
						timeout_jiffies);
out_trans_free:
	gsi_trans_free(trans);

	return remaining ? 0 : -ETIMEDOUT;
}

/* Process the completion of a transaction; called while polling */
void gsi_trans_complete(struct gsi_trans *trans)
{
	/* If the entire SGL was mapped when added, unmap it now */
	if (trans->direction != DMA_NONE)
		dma_unmap_sg(trans->gsi->dev, trans->sgl, trans->used,
			     trans->direction);

	ipa_gsi_trans_complete(trans);

	complete(&trans->completion);

	gsi_trans_free(trans);
}

/* Cancel a channel's pending transactions */
void gsi_channel_trans_cancel_pending(struct gsi_channel *channel)
{
	struct gsi_trans_info *trans_info = &channel->trans_info;
	struct gsi_trans *trans;
	bool cancelled;

	/* channel->gsi->mutex is held by caller */
	spin_lock_bh(&trans_info->spinlock);

	cancelled = !list_empty(&trans_info->pending);
	list_for_each_entry(trans, &trans_info->pending, links)
		trans->cancelled = true;

	list_splice_tail_init(&trans_info->pending, &trans_info->complete);

	spin_unlock_bh(&trans_info->spinlock);

	/* Schedule NAPI polling to complete the cancelled transactions */
	if (cancelled)
		napi_schedule(&channel->napi);
}

/* Issue a command to read a single byte from a channel */
int gsi_trans_read_byte(struct gsi *gsi, u32 channel_id, dma_addr_t addr)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];
	struct gsi_ring *ring = &channel->tre_ring;
	struct gsi_trans_info *trans_info;
	struct gsi_tre *dest_tre;

	trans_info = &channel->trans_info;

	/* First reserve the TRE, if possible */
	if (!gsi_trans_tre_reserve(trans_info, 1))
		return -EBUSY;

	/* Now fill the the reserved TRE and tell the hardware */

	dest_tre = gsi_ring_virt(ring, ring->index);
	gsi_trans_tre_fill(dest_tre, addr, 1, true, false, IPA_CMD_NONE);

	ring->index++;
	gsi_channel_doorbell(channel);

	return 0;
}

/* Mark a gsi_trans_read_byte() request done */
void gsi_trans_read_byte_done(struct gsi *gsi, u32 channel_id)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];

	gsi_trans_tre_release(&channel->trans_info, 1);
}

/* Initialize a channel's GSI transaction info */
int gsi_channel_trans_init(struct gsi *gsi, u32 channel_id)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];
	struct gsi_trans_info *trans_info;
	u32 tre_max;
	int ret;

	/* Ensure the size of a channel element is what's expected */
	BUILD_BUG_ON(sizeof(struct gsi_tre) != GSI_RING_ELEMENT_SIZE);

	/* The map array is used to determine what transaction is associated
	 * with a TRE that the hardware reports has completed.  We need one
	 * map entry per TRE.
	 */
	trans_info = &channel->trans_info;
	trans_info->map = kcalloc(channel->tre_count, sizeof(*trans_info->map),
				  GFP_KERNEL);
	if (!trans_info->map)
		return -ENOMEM;

	/* We can't use more TREs than there are available in the ring.
	 * This limits the number of transactions that can be oustanding.
	 * Worst case is one TRE per transaction (but we actually limit
	 * it to something a little less than that).  We allocate resources
	 * for transactions (including transaction structures) based on
	 * this maximum number.
	 */
	tre_max = gsi_channel_tre_max(channel->gsi, channel_id);

	/* Transactions are allocated one at a time. */
	ret = gsi_trans_pool_init(&trans_info->pool, sizeof(struct gsi_trans),
				  tre_max, 1);
	if (ret)
		goto err_kfree;

	/* A transaction uses a scatterlist array to represent the data
	 * transfers implemented by the transaction.  Each scatterlist
	 * element is used to fill a single TRE when the transaction is
	 * committed.  So we need as many scatterlist elements as the
	 * maximum number of TREs that can be outstanding.
	 *
	 * All TREs in a transaction must fit within the channel's TLV FIFO.
	 * A transaction on a channel can allocate as many TREs as that but
	 * no more.
	 */
	ret = gsi_trans_pool_init(&trans_info->sg_pool,
				  sizeof(struct scatterlist),
				  tre_max, channel->tlv_count);
	if (ret)
		goto err_trans_pool_exit;

	/* Finally, the tre_avail field is what ultimately limits the number
	 * of outstanding transactions and their resources.  A transaction
	 * allocation succeeds only if the TREs available are sufficient for
	 * what the transaction might need.  Transaction resource pools are
	 * sized based on the maximum number of outstanding TREs, so there
	 * will always be resources available if there are TREs available.
	 */
	atomic_set(&trans_info->tre_avail, tre_max);

	spin_lock_init(&trans_info->spinlock);
	INIT_LIST_HEAD(&trans_info->alloc);
	INIT_LIST_HEAD(&trans_info->pending);
	INIT_LIST_HEAD(&trans_info->complete);
	INIT_LIST_HEAD(&trans_info->polled);

	return 0;

err_trans_pool_exit:
	gsi_trans_pool_exit(&trans_info->pool);
err_kfree:
	kfree(trans_info->map);

	dev_err(gsi->dev, "error %d initializing channel %u transactions\n",
		ret, channel_id);

	return ret;
}

/* Inverse of gsi_channel_trans_init() */
void gsi_channel_trans_exit(struct gsi_channel *channel)
{
	struct gsi_trans_info *trans_info = &channel->trans_info;

	gsi_trans_pool_exit(&trans_info->sg_pool);
	gsi_trans_pool_exit(&trans_info->pool);
	kfree(trans_info->map);
}
