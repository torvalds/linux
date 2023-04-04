// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2022 Linaro Ltd.
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
 * everything about a related group of IPA operations in a single structure.
 * (A "operation" in this sense is either a data transfer or an IPA immediate
 * command.)  Most details of interaction with the GSI hardware are managed
 * by the GSI transaction core, allowing users to simply describe operations
 * to be performed.  When a transaction has completed a callback function
 * (dependent on the type of endpoint associated with the channel) allows
 * cleanup of resources associated with the transaction.
 *
 * To perform an operation (or set of them), a user of the GSI transaction
 * interface allocates a transaction, indicating the number of TREs required
 * (one per operation).  If sufficient TREs are available, they are reserved
 * for use in the transaction and the allocation succeeds.  This way
 * exhaustion of the available TREs in a channel ring is detected as early
 * as possible.  Any other resources that might be needed to complete a
 * transaction are also allocated when the transaction is allocated.
 *
 * Operations performed as part of a transaction are represented in an array
 * of Linux scatterlist structures, allocated with the transaction.  These
 * scatterlist structures are initialized by "adding" operations to the
 * transaction.  If a buffer in an operation must be mapped for DMA, this is
 * done at the time it is added to the transaction.  It is possible for a
 * mapping error to occur when an operation is added.  In this case the
 * transaction should simply be freed; this correctly releases resources
 * associated with the transaction.
 *
 * Once all operations have been successfully added to a transaction, the
 * transaction is committed.  Committing transfers ownership of the entire
 * transaction to the GSI transaction core.  The GSI transaction code
 * formats the content of the scatterlist array into the channel ring
 * buffer and informs the hardware that new TREs are available to process.
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
	size_t alloc_size;
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
	alloc_size = size_mul(count + max_alloc - 1, size);
	alloc_size = kmalloc_size_roundup(alloc_size);
	virt = kzalloc(alloc_size, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	pool->base = virt;
	/* If the allocator gave us any extra memory, use it */
	pool->count = alloc_size / size;
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

/* Home-grown DMA pool.  This way we can preallocate the pool, and guarantee
 * allocations will succeed.  The immediate commands in a transaction can
 * require up to max_alloc elements from the pool.  But we only allow
 * allocation of a single element from a DMA pool at a time.
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
	total_size = PAGE_SIZE << get_order(total_size);

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

/* Map a TRE ring entry index to the transaction it is associated with */
static void gsi_trans_map(struct gsi_trans *trans, u32 index)
{
	struct gsi_channel *channel = &trans->gsi->channel[trans->channel_id];

	/* The completion event will indicate the last TRE used */
	index += trans->used_count - 1;

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
	struct gsi_trans_info *trans_info = &channel->trans_info;
	u16 trans_id = trans_info->completed_id;

	if (trans_id == trans_info->pending_id) {
		gsi_channel_update(channel);
		if (trans_id == trans_info->pending_id)
			return NULL;
	}

	return &trans_info->trans[trans_id %= channel->tre_count];
}

/* Move a transaction from allocated to committed state */
static void gsi_trans_move_committed(struct gsi_trans *trans)
{
	struct gsi_channel *channel = &trans->gsi->channel[trans->channel_id];
	struct gsi_trans_info *trans_info = &channel->trans_info;

	/* This allocated transaction is now committed */
	trans_info->allocated_id++;
}

/* Move committed transactions to pending state */
static void gsi_trans_move_pending(struct gsi_trans *trans)
{
	struct gsi_channel *channel = &trans->gsi->channel[trans->channel_id];
	struct gsi_trans_info *trans_info = &channel->trans_info;
	u16 trans_index = trans - &trans_info->trans[0];
	u16 delta;

	/* These committed transactions are now pending */
	delta = trans_index - trans_info->committed_id + 1;
	trans_info->committed_id += delta % channel->tre_count;
}

/* Move pending transactions to completed state */
void gsi_trans_move_complete(struct gsi_trans *trans)
{
	struct gsi_channel *channel = &trans->gsi->channel[trans->channel_id];
	struct gsi_trans_info *trans_info = &channel->trans_info;
	u16 trans_index = trans - trans_info->trans;
	u16 delta;

	/* These pending transactions are now completed */
	delta = trans_index - trans_info->pending_id + 1;
	delta %= channel->tre_count;
	trans_info->pending_id += delta;
}

/* Move a transaction from completed to polled state */
void gsi_trans_move_polled(struct gsi_trans *trans)
{
	struct gsi_channel *channel = &trans->gsi->channel[trans->channel_id];
	struct gsi_trans_info *trans_info = &channel->trans_info;

	/* This completed transaction is now polled */
	trans_info->completed_id++;
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
	u16 trans_index;

	if (WARN_ON(tre_count > channel->trans_tre_max))
		return NULL;

	trans_info = &channel->trans_info;

	/* If we can't reserve the TREs for the transaction, we're done */
	if (!gsi_trans_tre_reserve(trans_info, tre_count))
		return NULL;

	trans_index = trans_info->free_id % channel->tre_count;
	trans = &trans_info->trans[trans_index];
	memset(trans, 0, sizeof(*trans));

	/* Initialize non-zero fields in the transaction */
	trans->gsi = gsi;
	trans->channel_id = channel_id;
	trans->rsvd_count = tre_count;
	init_completion(&trans->completion);

	/* Allocate the scatterlist */
	trans->sgl = gsi_trans_pool_alloc(&trans_info->sg_pool, tre_count);
	sg_init_marker(trans->sgl, tre_count);

	trans->direction = direction;
	refcount_set(&trans->refcount, 1);

	/* This free transaction is now allocated */
	trans_info->free_id++;

	return trans;
}

/* Free a previously-allocated transaction */
void gsi_trans_free(struct gsi_trans *trans)
{
	struct gsi_trans_info *trans_info;

	if (!refcount_dec_and_test(&trans->refcount))
		return;

	/* Unused transactions are allocated but never committed, pending,
	 * completed, or polled.
	 */
	trans_info = &trans->gsi->channel[trans->channel_id].trans_info;
	if (!trans->used_count) {
		trans_info->allocated_id++;
		trans_info->committed_id++;
		trans_info->pending_id++;
		trans_info->completed_id++;
	} else {
		ipa_gsi_trans_release(trans);
	}

	/* This transaction is now free */
	trans_info->polled_id++;

	/* Releasing the reserved TREs implicitly frees the sgl[] and
	 * (if present) info[] arrays, plus the transaction itself.
	 */
	gsi_trans_tre_release(trans_info, trans->rsvd_count);
}

/* Add an immediate command to a transaction */
void gsi_trans_cmd_add(struct gsi_trans *trans, void *buf, u32 size,
		       dma_addr_t addr, enum ipa_cmd_opcode opcode)
{
	u32 which = trans->used_count++;
	struct scatterlist *sg;

	WARN_ON(which >= trans->rsvd_count);

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

	trans->cmd_opcode[which] = opcode;
}

/* Add a page transfer to a transaction.  It will fill the only TRE. */
int gsi_trans_page_add(struct gsi_trans *trans, struct page *page, u32 size,
		       u32 offset)
{
	struct scatterlist *sg = &trans->sgl[0];
	int ret;

	if (WARN_ON(trans->rsvd_count != 1))
		return -EINVAL;
	if (WARN_ON(trans->used_count))
		return -EINVAL;

	sg_set_page(sg, page, size, offset);
	ret = dma_map_sg(trans->gsi->dev, sg, 1, trans->direction);
	if (!ret)
		return -ENOMEM;

	trans->used_count++;	/* Transaction now owns the (DMA mapped) page */

	return 0;
}

/* Add an SKB transfer to a transaction.  No other TREs will be used. */
int gsi_trans_skb_add(struct gsi_trans *trans, struct sk_buff *skb)
{
	struct scatterlist *sg = &trans->sgl[0];
	u32 used_count;
	int ret;

	if (WARN_ON(trans->rsvd_count != 1))
		return -EINVAL;
	if (WARN_ON(trans->used_count))
		return -EINVAL;

	/* skb->len will not be 0 (checked early) */
	ret = skb_to_sgvec(skb, sg, 0, skb->len);
	if (ret < 0)
		return ret;
	used_count = ret;

	ret = dma_map_sg(trans->gsi->dev, sg, used_count, trans->direction);
	if (!ret)
		return -ENOMEM;

	/* Transaction now owns the (DMA mapped) skb */
	trans->used_count += used_count;

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
 * so it can be recovered when it completes.  Moves the transaction to
 * pending state.  Finally, updates the channel ring pointer and optionally
 * rings the doorbell.
 */
static void __gsi_trans_commit(struct gsi_trans *trans, bool ring_db)
{
	struct gsi_channel *channel = &trans->gsi->channel[trans->channel_id];
	struct gsi_ring *tre_ring = &channel->tre_ring;
	enum ipa_cmd_opcode opcode = IPA_CMD_NONE;
	bool bei = channel->toward_ipa;
	struct gsi_tre *dest_tre;
	struct scatterlist *sg;
	u32 byte_count = 0;
	u8 *cmd_opcode;
	u32 avail;
	u32 i;

	WARN_ON(!trans->used_count);

	/* Consume the entries.  If we cross the end of the ring while
	 * filling them we'll switch to the beginning to finish.
	 * If there is no info array we're doing a simple data
	 * transfer request, whose opcode is IPA_CMD_NONE.
	 */
	cmd_opcode = channel->command ? &trans->cmd_opcode[0] : NULL;
	avail = tre_ring->count - tre_ring->index % tre_ring->count;
	dest_tre = gsi_ring_virt(tre_ring, tre_ring->index);
	for_each_sg(trans->sgl, sg, trans->used_count, i) {
		bool last_tre = i == trans->used_count - 1;
		dma_addr_t addr = sg_dma_address(sg);
		u32 len = sg_dma_len(sg);

		byte_count += len;
		if (!avail--)
			dest_tre = gsi_ring_virt(tre_ring, 0);
		if (cmd_opcode)
			opcode = *cmd_opcode++;

		gsi_trans_tre_fill(dest_tre, addr, len, last_tre, bei, opcode);
		dest_tre++;
	}
	/* Associate the TRE with the transaction */
	gsi_trans_map(trans, tre_ring->index);

	tre_ring->index += trans->used_count;

	trans->len = byte_count;
	if (channel->toward_ipa)
		gsi_trans_tx_committed(trans);

	gsi_trans_move_committed(trans);

	/* Ring doorbell if requested, or if all TREs are allocated */
	if (ring_db || !atomic_read(&channel->trans_info.tre_avail)) {
		/* Report what we're handing off to hardware for TX channels */
		if (channel->toward_ipa)
			gsi_trans_tx_queued(trans);
		gsi_trans_move_pending(trans);
		gsi_channel_doorbell(channel);
	}
}

/* Commit a GSI transaction */
void gsi_trans_commit(struct gsi_trans *trans, bool ring_db)
{
	if (trans->used_count)
		__gsi_trans_commit(trans, ring_db);
	else
		gsi_trans_free(trans);
}

/* Commit a GSI transaction and wait for it to complete */
void gsi_trans_commit_wait(struct gsi_trans *trans)
{
	if (!trans->used_count)
		goto out_trans_free;

	refcount_inc(&trans->refcount);

	__gsi_trans_commit(trans, true);

	wait_for_completion(&trans->completion);

out_trans_free:
	gsi_trans_free(trans);
}

/* Process the completion of a transaction; called while polling */
void gsi_trans_complete(struct gsi_trans *trans)
{
	/* If the entire SGL was mapped when added, unmap it now */
	if (trans->direction != DMA_NONE)
		dma_unmap_sg(trans->gsi->dev, trans->sgl, trans->used_count,
			     trans->direction);

	ipa_gsi_trans_complete(trans);

	complete(&trans->completion);

	gsi_trans_free(trans);
}

/* Cancel a channel's pending transactions */
void gsi_channel_trans_cancel_pending(struct gsi_channel *channel)
{
	struct gsi_trans_info *trans_info = &channel->trans_info;
	u16 trans_id = trans_info->pending_id;

	/* channel->gsi->mutex is held by caller */

	/* If there are no pending transactions, we're done */
	if (trans_id == trans_info->committed_id)
		return;

	/* Mark all pending transactions cancelled */
	do {
		struct gsi_trans *trans;

		trans = &trans_info->trans[trans_id % channel->tre_count];
		trans->cancelled = true;
	} while (++trans_id != trans_info->committed_id);

	/* All pending transactions are now completed */
	trans_info->pending_id = trans_info->committed_id;

	/* Schedule NAPI polling to complete the cancelled transactions */
	napi_schedule(&channel->napi);
}

/* Issue a command to read a single byte from a channel */
int gsi_trans_read_byte(struct gsi *gsi, u32 channel_id, dma_addr_t addr)
{
	struct gsi_channel *channel = &gsi->channel[channel_id];
	struct gsi_ring *tre_ring = &channel->tre_ring;
	struct gsi_trans_info *trans_info;
	struct gsi_tre *dest_tre;

	trans_info = &channel->trans_info;

	/* First reserve the TRE, if possible */
	if (!gsi_trans_tre_reserve(trans_info, 1))
		return -EBUSY;

	/* Now fill the reserved TRE and tell the hardware */

	dest_tre = gsi_ring_virt(tre_ring, tre_ring->index);
	gsi_trans_tre_fill(dest_tre, addr, 1, true, false, IPA_CMD_NONE);

	tre_ring->index++;
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
	u32 tre_count = channel->tre_count;
	struct gsi_trans_info *trans_info;
	u32 tre_max;
	int ret;

	/* Ensure the size of a channel element is what's expected */
	BUILD_BUG_ON(sizeof(struct gsi_tre) != GSI_RING_ELEMENT_SIZE);

	trans_info = &channel->trans_info;

	/* The tre_avail field is what ultimately limits the number of
	 * outstanding transactions and their resources.  A transaction
	 * allocation succeeds only if the TREs available are sufficient
	 * for what the transaction might need.
	 */
	tre_max = gsi_channel_tre_max(channel->gsi, channel_id);
	atomic_set(&trans_info->tre_avail, tre_max);

	/* We can't use more TREs than the number available in the ring.
	 * This limits the number of transactions that can be outstanding.
	 * Worst case is one TRE per transaction (but we actually limit
	 * it to something a little less than that).  By allocating a
	 * power-of-two number of transactions we can use an index
	 * modulo that number to determine the next one that's free.
	 * Transactions are allocated one at a time.
	 */
	trans_info->trans = kcalloc(tre_count, sizeof(*trans_info->trans),
				    GFP_KERNEL);
	if (!trans_info->trans)
		return -ENOMEM;
	trans_info->free_id = 0;	/* all modulo channel->tre_count */
	trans_info->allocated_id = 0;
	trans_info->committed_id = 0;
	trans_info->pending_id = 0;
	trans_info->completed_id = 0;
	trans_info->polled_id = 0;

	/* A completion event contains a pointer to the TRE that caused
	 * the event (which will be the last one used by the transaction).
	 * Each entry in this map records the transaction associated
	 * with a corresponding completed TRE.
	 */
	trans_info->map = kcalloc(tre_count, sizeof(*trans_info->map),
				  GFP_KERNEL);
	if (!trans_info->map) {
		ret = -ENOMEM;
		goto err_trans_free;
	}

	/* A transaction uses a scatterlist array to represent the data
	 * transfers implemented by the transaction.  Each scatterlist
	 * element is used to fill a single TRE when the transaction is
	 * committed.  So we need as many scatterlist elements as the
	 * maximum number of TREs that can be outstanding.
	 */
	ret = gsi_trans_pool_init(&trans_info->sg_pool,
				  sizeof(struct scatterlist),
				  tre_max, channel->trans_tre_max);
	if (ret)
		goto err_map_free;


	return 0;

err_map_free:
	kfree(trans_info->map);
err_trans_free:
	kfree(trans_info->trans);

	dev_err(gsi->dev, "error %d initializing channel %u transactions\n",
		ret, channel_id);

	return ret;
}

/* Inverse of gsi_channel_trans_init() */
void gsi_channel_trans_exit(struct gsi_channel *channel)
{
	struct gsi_trans_info *trans_info = &channel->trans_info;

	gsi_trans_pool_exit(&trans_info->sg_pool);
	kfree(trans_info->trans);
	kfree(trans_info->map);
}
