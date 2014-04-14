/*
 * xHCI host controller driver
 *
 * Copyright (C) 2008 Intel Corp.
 *
 * Author: Sarah Sharp
 * Some code borrowed from the Linux EHCI driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/usb.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/dmapool.h>

#include "xhci.h"

/*
 * Allocates a generic ring segment from the ring pool, sets the dma address,
 * initializes the segment to zero, and sets the private next pointer to NULL.
 *
 * Section 4.11.1.1:
 * "All components of all Command and Transfer TRBs shall be initialized to '0'"
 */
static struct xhci_segment *xhci_segment_alloc(struct xhci_hcd *xhci,
					unsigned int cycle_state, gfp_t flags)
{
	struct xhci_segment *seg;
	dma_addr_t	dma;
	int		i;

	seg = kzalloc(sizeof *seg, flags);
	if (!seg)
		return NULL;

	seg->trbs = dma_pool_alloc(xhci->segment_pool, flags, &dma);
	if (!seg->trbs) {
		kfree(seg);
		return NULL;
	}

	memset(seg->trbs, 0, TRB_SEGMENT_SIZE);
	/* If the cycle state is 0, set the cycle bit to 1 for all the TRBs */
	if (cycle_state == 0) {
		for (i = 0; i < TRBS_PER_SEGMENT; i++)
			seg->trbs[i].link.control |= TRB_CYCLE;
	}
	seg->dma = dma;
	seg->next = NULL;

	return seg;
}

static void xhci_segment_free(struct xhci_hcd *xhci, struct xhci_segment *seg)
{
	if (seg->trbs) {
		dma_pool_free(xhci->segment_pool, seg->trbs, seg->dma);
		seg->trbs = NULL;
	}
	kfree(seg);
}

static void xhci_free_segments_for_ring(struct xhci_hcd *xhci,
				struct xhci_segment *first)
{
	struct xhci_segment *seg;

	seg = first->next;
	while (seg != first) {
		struct xhci_segment *next = seg->next;
		xhci_segment_free(xhci, seg);
		seg = next;
	}
	xhci_segment_free(xhci, first);
}

/*
 * Make the prev segment point to the next segment.
 *
 * Change the last TRB in the prev segment to be a Link TRB which points to the
 * DMA address of the next segment.  The caller needs to set any Link TRB
 * related flags, such as End TRB, Toggle Cycle, and no snoop.
 */
static void xhci_link_segments(struct xhci_hcd *xhci, struct xhci_segment *prev,
		struct xhci_segment *next, enum xhci_ring_type type)
{
	u32 val;

	if (!prev || !next)
		return;
	prev->next = next;
	if (type != TYPE_EVENT) {
		prev->trbs[TRBS_PER_SEGMENT-1].link.segment_ptr =
			cpu_to_le64(next->dma);

		/* Set the last TRB in the segment to have a TRB type ID of Link TRB */
		val = le32_to_cpu(prev->trbs[TRBS_PER_SEGMENT-1].link.control);
		val &= ~TRB_TYPE_BITMASK;
		val |= TRB_TYPE(TRB_LINK);
		/* Always set the chain bit with 0.95 hardware */
		/* Set chain bit for isoc rings on AMD 0.96 host */
		if (xhci_link_trb_quirk(xhci) ||
				(type == TYPE_ISOC &&
				 (xhci->quirks & XHCI_AMD_0x96_HOST)))
			val |= TRB_CHAIN;
		prev->trbs[TRBS_PER_SEGMENT-1].link.control = cpu_to_le32(val);
	}
}

/*
 * Link the ring to the new segments.
 * Set Toggle Cycle for the new ring if needed.
 */
static void xhci_link_rings(struct xhci_hcd *xhci, struct xhci_ring *ring,
		struct xhci_segment *first, struct xhci_segment *last,
		unsigned int num_segs)
{
	struct xhci_segment *next;

	if (!ring || !first || !last)
		return;

	next = ring->enq_seg->next;
	xhci_link_segments(xhci, ring->enq_seg, first, ring->type);
	xhci_link_segments(xhci, last, next, ring->type);
	ring->num_segs += num_segs;
	ring->num_trbs_free += (TRBS_PER_SEGMENT - 1) * num_segs;

	if (ring->type != TYPE_EVENT && ring->enq_seg == ring->last_seg) {
		ring->last_seg->trbs[TRBS_PER_SEGMENT-1].link.control
			&= ~cpu_to_le32(LINK_TOGGLE);
		last->trbs[TRBS_PER_SEGMENT-1].link.control
			|= cpu_to_le32(LINK_TOGGLE);
		ring->last_seg = last;
	}
}

/* XXX: Do we need the hcd structure in all these functions? */
void xhci_ring_free(struct xhci_hcd *xhci, struct xhci_ring *ring)
{
	if (!ring)
		return;

	if (ring->first_seg)
		xhci_free_segments_for_ring(xhci, ring->first_seg);

	kfree(ring);
}

static void xhci_initialize_ring_info(struct xhci_ring *ring,
					unsigned int cycle_state)
{
	/* The ring is empty, so the enqueue pointer == dequeue pointer */
	ring->enqueue = ring->first_seg->trbs;
	ring->enq_seg = ring->first_seg;
	ring->dequeue = ring->enqueue;
	ring->deq_seg = ring->first_seg;
	/* The ring is initialized to 0. The producer must write 1 to the cycle
	 * bit to handover ownership of the TRB, so PCS = 1.  The consumer must
	 * compare CCS to the cycle bit to check ownership, so CCS = 1.
	 *
	 * New rings are initialized with cycle state equal to 1; if we are
	 * handling ring expansion, set the cycle state equal to the old ring.
	 */
	ring->cycle_state = cycle_state;
	/* Not necessary for new rings, but needed for re-initialized rings */
	ring->enq_updates = 0;
	ring->deq_updates = 0;

	/*
	 * Each segment has a link TRB, and leave an extra TRB for SW
	 * accounting purpose
	 */
	ring->num_trbs_free = ring->num_segs * (TRBS_PER_SEGMENT - 1) - 1;
}

/* Allocate segments and link them for a ring */
static int xhci_alloc_segments_for_ring(struct xhci_hcd *xhci,
		struct xhci_segment **first, struct xhci_segment **last,
		unsigned int num_segs, unsigned int cycle_state,
		enum xhci_ring_type type, gfp_t flags)
{
	struct xhci_segment *prev;

	prev = xhci_segment_alloc(xhci, cycle_state, flags);
	if (!prev)
		return -ENOMEM;
	num_segs--;

	*first = prev;
	while (num_segs > 0) {
		struct xhci_segment	*next;

		next = xhci_segment_alloc(xhci, cycle_state, flags);
		if (!next) {
			prev = *first;
			while (prev) {
				next = prev->next;
				xhci_segment_free(xhci, prev);
				prev = next;
			}
			return -ENOMEM;
		}
		xhci_link_segments(xhci, prev, next, type);

		prev = next;
		num_segs--;
	}
	xhci_link_segments(xhci, prev, *first, type);
	*last = prev;

	return 0;
}

/**
 * Create a new ring with zero or more segments.
 *
 * Link each segment together into a ring.
 * Set the end flag and the cycle toggle bit on the last segment.
 * See section 4.9.1 and figures 15 and 16.
 */
static struct xhci_ring *xhci_ring_alloc(struct xhci_hcd *xhci,
		unsigned int num_segs, unsigned int cycle_state,
		enum xhci_ring_type type, gfp_t flags)
{
	struct xhci_ring	*ring;
	int ret;

	ring = kzalloc(sizeof *(ring), flags);
	if (!ring)
		return NULL;

	ring->num_segs = num_segs;
	INIT_LIST_HEAD(&ring->td_list);
	ring->type = type;
	if (num_segs == 0)
		return ring;

	ret = xhci_alloc_segments_for_ring(xhci, &ring->first_seg,
			&ring->last_seg, num_segs, cycle_state, type, flags);
	if (ret)
		goto fail;

	/* Only event ring does not use link TRB */
	if (type != TYPE_EVENT) {
		/* See section 4.9.2.1 and 6.4.4.1 */
		ring->last_seg->trbs[TRBS_PER_SEGMENT - 1].link.control |=
			cpu_to_le32(LINK_TOGGLE);
	}
	xhci_initialize_ring_info(ring, cycle_state);
	return ring;

fail:
	kfree(ring);
	return NULL;
}

void xhci_free_or_cache_endpoint_ring(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		unsigned int ep_index)
{
	int rings_cached;

	rings_cached = virt_dev->num_rings_cached;
	if (rings_cached < XHCI_MAX_RINGS_CACHED) {
		virt_dev->ring_cache[rings_cached] =
			virt_dev->eps[ep_index].ring;
		virt_dev->num_rings_cached++;
		xhci_dbg(xhci, "Cached old ring, "
				"%d ring%s cached\n",
				virt_dev->num_rings_cached,
				(virt_dev->num_rings_cached > 1) ? "s" : "");
	} else {
		xhci_ring_free(xhci, virt_dev->eps[ep_index].ring);
		xhci_dbg(xhci, "Ring cache full (%d rings), "
				"freeing ring\n",
				virt_dev->num_rings_cached);
	}
	virt_dev->eps[ep_index].ring = NULL;
}

/* Zero an endpoint ring (except for link TRBs) and move the enqueue and dequeue
 * pointers to the beginning of the ring.
 */
static void xhci_reinit_cached_ring(struct xhci_hcd *xhci,
			struct xhci_ring *ring, unsigned int cycle_state,
			enum xhci_ring_type type)
{
	struct xhci_segment	*seg = ring->first_seg;
	int i;

	do {
		memset(seg->trbs, 0,
				sizeof(union xhci_trb)*TRBS_PER_SEGMENT);
		if (cycle_state == 0) {
			for (i = 0; i < TRBS_PER_SEGMENT; i++)
				seg->trbs[i].link.control |= TRB_CYCLE;
		}
		/* All endpoint rings have link TRBs */
		xhci_link_segments(xhci, seg, seg->next, type);
		seg = seg->next;
	} while (seg != ring->first_seg);
	ring->type = type;
	xhci_initialize_ring_info(ring, cycle_state);
	/* td list should be empty since all URBs have been cancelled,
	 * but just in case...
	 */
	INIT_LIST_HEAD(&ring->td_list);
}

/*
 * Expand an existing ring.
 * Look for a cached ring or allocate a new ring which has same segment numbers
 * and link the two rings.
 */
int xhci_ring_expansion(struct xhci_hcd *xhci, struct xhci_ring *ring,
				unsigned int num_trbs, gfp_t flags)
{
	struct xhci_segment	*first;
	struct xhci_segment	*last;
	unsigned int		num_segs;
	unsigned int		num_segs_needed;
	int			ret;

	num_segs_needed = (num_trbs + (TRBS_PER_SEGMENT - 1) - 1) /
				(TRBS_PER_SEGMENT - 1);

	/* Allocate number of segments we needed, or double the ring size */
	num_segs = ring->num_segs > num_segs_needed ?
			ring->num_segs : num_segs_needed;

	ret = xhci_alloc_segments_for_ring(xhci, &first, &last,
			num_segs, ring->cycle_state, ring->type, flags);
	if (ret)
		return -ENOMEM;

	xhci_link_rings(xhci, ring, first, last, num_segs);
	xhci_dbg(xhci, "ring expansion succeed, now has %d segments\n",
			ring->num_segs);

	return 0;
}

#define CTX_SIZE(_hcc) (HCC_64BYTE_CONTEXT(_hcc) ? 64 : 32)

static struct xhci_container_ctx *xhci_alloc_container_ctx(struct xhci_hcd *xhci,
						    int type, gfp_t flags)
{
	struct xhci_container_ctx *ctx = kzalloc(sizeof(*ctx), flags);
	if (!ctx)
		return NULL;

	BUG_ON((type != XHCI_CTX_TYPE_DEVICE) && (type != XHCI_CTX_TYPE_INPUT));
	ctx->type = type;
	ctx->size = HCC_64BYTE_CONTEXT(xhci->hcc_params) ? 2048 : 1024;
	if (type == XHCI_CTX_TYPE_INPUT)
		ctx->size += CTX_SIZE(xhci->hcc_params);

	ctx->bytes = dma_pool_alloc(xhci->device_pool, flags, &ctx->dma);
	if (!ctx->bytes) {
		kfree(ctx);
		return NULL;
	}
	memset(ctx->bytes, 0, ctx->size);
	return ctx;
}

static void xhci_free_container_ctx(struct xhci_hcd *xhci,
			     struct xhci_container_ctx *ctx)
{
	if (!ctx)
		return;
	dma_pool_free(xhci->device_pool, ctx->bytes, ctx->dma);
	kfree(ctx);
}

struct xhci_input_control_ctx *xhci_get_input_control_ctx(struct xhci_hcd *xhci,
					      struct xhci_container_ctx *ctx)
{
	BUG_ON(ctx->type != XHCI_CTX_TYPE_INPUT);
	return (struct xhci_input_control_ctx *)ctx->bytes;
}

struct xhci_slot_ctx *xhci_get_slot_ctx(struct xhci_hcd *xhci,
					struct xhci_container_ctx *ctx)
{
	if (ctx->type == XHCI_CTX_TYPE_DEVICE)
		return (struct xhci_slot_ctx *)ctx->bytes;

	return (struct xhci_slot_ctx *)
		(ctx->bytes + CTX_SIZE(xhci->hcc_params));
}

struct xhci_ep_ctx *xhci_get_ep_ctx(struct xhci_hcd *xhci,
				    struct xhci_container_ctx *ctx,
				    unsigned int ep_index)
{
	/* increment ep index by offset of start of ep ctx array */
	ep_index++;
	if (ctx->type == XHCI_CTX_TYPE_INPUT)
		ep_index++;

	return (struct xhci_ep_ctx *)
		(ctx->bytes + (ep_index * CTX_SIZE(xhci->hcc_params)));
}


/***************** Streams structures manipulation *************************/

static void xhci_free_stream_ctx(struct xhci_hcd *xhci,
		unsigned int num_stream_ctxs,
		struct xhci_stream_ctx *stream_ctx, dma_addr_t dma)
{
	struct pci_dev *pdev = to_pci_dev(xhci_to_hcd(xhci)->self.controller);

	if (num_stream_ctxs > MEDIUM_STREAM_ARRAY_SIZE)
		dma_free_coherent(&pdev->dev,
				sizeof(struct xhci_stream_ctx)*num_stream_ctxs,
				stream_ctx, dma);
	else if (num_stream_ctxs <= SMALL_STREAM_ARRAY_SIZE)
		return dma_pool_free(xhci->small_streams_pool,
				stream_ctx, dma);
	else
		return dma_pool_free(xhci->medium_streams_pool,
				stream_ctx, dma);
}

/*
 * The stream context array for each endpoint with bulk streams enabled can
 * vary in size, based on:
 *  - how many streams the endpoint supports,
 *  - the maximum primary stream array size the host controller supports,
 *  - and how many streams the device driver asks for.
 *
 * The stream context array must be a power of 2, and can be as small as
 * 64 bytes or as large as 1MB.
 */
static struct xhci_stream_ctx *xhci_alloc_stream_ctx(struct xhci_hcd *xhci,
		unsigned int num_stream_ctxs, dma_addr_t *dma,
		gfp_t mem_flags)
{
	struct pci_dev *pdev = to_pci_dev(xhci_to_hcd(xhci)->self.controller);

	if (num_stream_ctxs > MEDIUM_STREAM_ARRAY_SIZE)
		return dma_alloc_coherent(&pdev->dev,
				sizeof(struct xhci_stream_ctx)*num_stream_ctxs,
				dma, mem_flags);
	else if (num_stream_ctxs <= SMALL_STREAM_ARRAY_SIZE)
		return dma_pool_alloc(xhci->small_streams_pool,
				mem_flags, dma);
	else
		return dma_pool_alloc(xhci->medium_streams_pool,
				mem_flags, dma);
}

struct xhci_ring *xhci_dma_to_transfer_ring(
		struct xhci_virt_ep *ep,
		u64 address)
{
	if (ep->ep_state & EP_HAS_STREAMS)
		return radix_tree_lookup(&ep->stream_info->trb_address_map,
				address >> TRB_SEGMENT_SHIFT);
	return ep->ring;
}

/* Only use this when you know stream_info is valid */
#ifdef CONFIG_USB_XHCI_HCD_DEBUGGING
static struct xhci_ring *dma_to_stream_ring(
		struct xhci_stream_info *stream_info,
		u64 address)
{
	return radix_tree_lookup(&stream_info->trb_address_map,
			address >> TRB_SEGMENT_SHIFT);
}
#endif	/* CONFIG_USB_XHCI_HCD_DEBUGGING */

struct xhci_ring *xhci_stream_id_to_ring(
		struct xhci_virt_device *dev,
		unsigned int ep_index,
		unsigned int stream_id)
{
	struct xhci_virt_ep *ep = &dev->eps[ep_index];

	if (stream_id == 0)
		return ep->ring;
	if (!ep->stream_info)
		return NULL;

	if (stream_id > ep->stream_info->num_streams)
		return NULL;
	return ep->stream_info->stream_rings[stream_id];
}

#ifdef CONFIG_USB_XHCI_HCD_DEBUGGING
static int xhci_test_radix_tree(struct xhci_hcd *xhci,
		unsigned int num_streams,
		struct xhci_stream_info *stream_info)
{
	u32 cur_stream;
	struct xhci_ring *cur_ring;
	u64 addr;

	for (cur_stream = 1; cur_stream < num_streams; cur_stream++) {
		struct xhci_ring *mapped_ring;
		int trb_size = sizeof(union xhci_trb);

		cur_ring = stream_info->stream_rings[cur_stream];
		for (addr = cur_ring->first_seg->dma;
				addr < cur_ring->first_seg->dma + TRB_SEGMENT_SIZE;
				addr += trb_size) {
			mapped_ring = dma_to_stream_ring(stream_info, addr);
			if (cur_ring != mapped_ring) {
				xhci_warn(xhci, "WARN: DMA address 0x%08llx "
						"didn't map to stream ID %u; "
						"mapped to ring %p\n",
						(unsigned long long) addr,
						cur_stream,
						mapped_ring);
				return -EINVAL;
			}
		}
		/* One TRB after the end of the ring segment shouldn't return a
		 * pointer to the current ring (although it may be a part of a
		 * different ring).
		 */
		mapped_ring = dma_to_stream_ring(stream_info, addr);
		if (mapped_ring != cur_ring) {
			/* One TRB before should also fail */
			addr = cur_ring->first_seg->dma - trb_size;
			mapped_ring = dma_to_stream_ring(stream_info, addr);
		}
		if (mapped_ring == cur_ring) {
			xhci_warn(xhci, "WARN: Bad DMA address 0x%08llx "
					"mapped to valid stream ID %u; "
					"mapped ring = %p\n",
					(unsigned long long) addr,
					cur_stream,
					mapped_ring);
			return -EINVAL;
		}
	}
	return 0;
}
#endif	/* CONFIG_USB_XHCI_HCD_DEBUGGING */

/*
 * Change an endpoint's internal structure so it supports stream IDs.  The
 * number of requested streams includes stream 0, which cannot be used by device
 * drivers.
 *
 * The number of stream contexts in the stream context array may be bigger than
 * the number of streams the driver wants to use.  This is because the number of
 * stream context array entries must be a power of two.
 *
 * We need a radix tree for mapping physical addresses of TRBs to which stream
 * ID they belong to.  We need to do this because the host controller won't tell
 * us which stream ring the TRB came from.  We could store the stream ID in an
 * event data TRB, but that doesn't help us for the cancellation case, since the
 * endpoint may stop before it reaches that event data TRB.
 *
 * The radix tree maps the upper portion of the TRB DMA address to a ring
 * segment that has the same upper portion of DMA addresses.  For example, say I
 * have segments of size 1KB, that are always 64-byte aligned.  A segment may
 * start at 0x10c91000 and end at 0x10c913f0.  If I use the upper 10 bits, the
 * key to the stream ID is 0x43244.  I can use the DMA address of the TRB to
 * pass the radix tree a key to get the right stream ID:
 *
 * 	0x10c90fff >> 10 = 0x43243
 * 	0x10c912c0 >> 10 = 0x43244
 * 	0x10c91400 >> 10 = 0x43245
 *
 * Obviously, only those TRBs with DMA addresses that are within the segment
 * will make the radix tree return the stream ID for that ring.
 *
 * Caveats for the radix tree:
 *
 * The radix tree uses an unsigned long as a key pair.  On 32-bit systems, an
 * unsigned long will be 32-bits; on a 64-bit system an unsigned long will be
 * 64-bits.  Since we only request 32-bit DMA addresses, we can use that as the
 * key on 32-bit or 64-bit systems (it would also be fine if we asked for 64-bit
 * PCI DMA addresses on a 64-bit system).  There might be a problem on 32-bit
 * extended systems (where the DMA address can be bigger than 32-bits),
 * if we allow the PCI dma mask to be bigger than 32-bits.  So don't do that.
 */
struct xhci_stream_info *xhci_alloc_stream_info(struct xhci_hcd *xhci,
		unsigned int num_stream_ctxs,
		unsigned int num_streams, gfp_t mem_flags)
{
	struct xhci_stream_info *stream_info;
	u32 cur_stream;
	struct xhci_ring *cur_ring;
	unsigned long key;
	u64 addr;
	int ret;

	xhci_dbg(xhci, "Allocating %u streams and %u "
			"stream context array entries.\n",
			num_streams, num_stream_ctxs);
	if (xhci->cmd_ring_reserved_trbs == MAX_RSVD_CMD_TRBS) {
		xhci_dbg(xhci, "Command ring has no reserved TRBs available\n");
		return NULL;
	}
	xhci->cmd_ring_reserved_trbs++;

	stream_info = kzalloc(sizeof(struct xhci_stream_info), mem_flags);
	if (!stream_info)
		goto cleanup_trbs;

	stream_info->num_streams = num_streams;
	stream_info->num_stream_ctxs = num_stream_ctxs;

	/* Initialize the array of virtual pointers to stream rings. */
	stream_info->stream_rings = kzalloc(
			sizeof(struct xhci_ring *)*num_streams,
			mem_flags);
	if (!stream_info->stream_rings)
		goto cleanup_info;

	/* Initialize the array of DMA addresses for stream rings for the HW. */
	stream_info->stream_ctx_array = xhci_alloc_stream_ctx(xhci,
			num_stream_ctxs, &stream_info->ctx_array_dma,
			mem_flags);
	if (!stream_info->stream_ctx_array)
		goto cleanup_ctx;
	memset(stream_info->stream_ctx_array, 0,
			sizeof(struct xhci_stream_ctx)*num_stream_ctxs);

	/* Allocate everything needed to free the stream rings later */
	stream_info->free_streams_command =
		xhci_alloc_command(xhci, true, true, mem_flags);
	if (!stream_info->free_streams_command)
		goto cleanup_ctx;

	INIT_RADIX_TREE(&stream_info->trb_address_map, GFP_ATOMIC);

	/* Allocate rings for all the streams that the driver will use,
	 * and add their segment DMA addresses to the radix tree.
	 * Stream 0 is reserved.
	 */
	for (cur_stream = 1; cur_stream < num_streams; cur_stream++) {
		stream_info->stream_rings[cur_stream] =
			xhci_ring_alloc(xhci, 2, 1, TYPE_STREAM, mem_flags);
		cur_ring = stream_info->stream_rings[cur_stream];
		if (!cur_ring)
			goto cleanup_rings;
		cur_ring->stream_id = cur_stream;
		/* Set deq ptr, cycle bit, and stream context type */
		addr = cur_ring->first_seg->dma |
			SCT_FOR_CTX(SCT_PRI_TR) |
			cur_ring->cycle_state;
		stream_info->stream_ctx_array[cur_stream].stream_ring =
			cpu_to_le64(addr);
		xhci_dbg(xhci, "Setting stream %d ring ptr to 0x%08llx\n",
				cur_stream, (unsigned long long) addr);

		key = (unsigned long)
			(cur_ring->first_seg->dma >> TRB_SEGMENT_SHIFT);
		ret = radix_tree_insert(&stream_info->trb_address_map,
				key, cur_ring);
		if (ret) {
			xhci_ring_free(xhci, cur_ring);
			stream_info->stream_rings[cur_stream] = NULL;
			goto cleanup_rings;
		}
	}
	/* Leave the other unused stream ring pointers in the stream context
	 * array initialized to zero.  This will cause the xHC to give us an
	 * error if the device asks for a stream ID we don't have setup (if it
	 * was any other way, the host controller would assume the ring is
	 * "empty" and wait forever for data to be queued to that stream ID).
	 */
#if XHCI_DEBUG
	/* Do a little test on the radix tree to make sure it returns the
	 * correct values.
	 */
	if (xhci_test_radix_tree(xhci, num_streams, stream_info))
		goto cleanup_rings;
#endif

	return stream_info;

cleanup_rings:
	for (cur_stream = 1; cur_stream < num_streams; cur_stream++) {
		cur_ring = stream_info->stream_rings[cur_stream];
		if (cur_ring) {
			addr = cur_ring->first_seg->dma;
			radix_tree_delete(&stream_info->trb_address_map,
					addr >> TRB_SEGMENT_SHIFT);
			xhci_ring_free(xhci, cur_ring);
			stream_info->stream_rings[cur_stream] = NULL;
		}
	}
	xhci_free_command(xhci, stream_info->free_streams_command);
cleanup_ctx:
	kfree(stream_info->stream_rings);
cleanup_info:
	kfree(stream_info);
cleanup_trbs:
	xhci->cmd_ring_reserved_trbs--;
	return NULL;
}
/*
 * Sets the MaxPStreams field and the Linear Stream Array field.
 * Sets the dequeue pointer to the stream context array.
 */
void xhci_setup_streams_ep_input_ctx(struct xhci_hcd *xhci,
		struct xhci_ep_ctx *ep_ctx,
		struct xhci_stream_info *stream_info)
{
	u32 max_primary_streams;
	/* MaxPStreams is the number of stream context array entries, not the
	 * number we're actually using.  Must be in 2^(MaxPstreams + 1) format.
	 * fls(0) = 0, fls(0x1) = 1, fls(0x10) = 2, fls(0x100) = 3, etc.
	 */
	max_primary_streams = fls(stream_info->num_stream_ctxs) - 2;
	xhci_dbg(xhci, "Setting number of stream ctx array entries to %u\n",
			1 << (max_primary_streams + 1));
	ep_ctx->ep_info &= cpu_to_le32(~EP_MAXPSTREAMS_MASK);
	ep_ctx->ep_info |= cpu_to_le32(EP_MAXPSTREAMS(max_primary_streams)
				       | EP_HAS_LSA);
	ep_ctx->deq  = cpu_to_le64(stream_info->ctx_array_dma);
}

/*
 * Sets the MaxPStreams field and the Linear Stream Array field to 0.
 * Reinstalls the "normal" endpoint ring (at its previous dequeue mark,
 * not at the beginning of the ring).
 */
void xhci_setup_no_streams_ep_input_ctx(struct xhci_hcd *xhci,
		struct xhci_ep_ctx *ep_ctx,
		struct xhci_virt_ep *ep)
{
	dma_addr_t addr;
	ep_ctx->ep_info &= cpu_to_le32(~(EP_MAXPSTREAMS_MASK | EP_HAS_LSA));
	addr = xhci_trb_virt_to_dma(ep->ring->deq_seg, ep->ring->dequeue);
	ep_ctx->deq  = cpu_to_le64(addr | ep->ring->cycle_state);
}

/* Frees all stream contexts associated with the endpoint,
 *
 * Caller should fix the endpoint context streams fields.
 */
void xhci_free_stream_info(struct xhci_hcd *xhci,
		struct xhci_stream_info *stream_info)
{
	int cur_stream;
	struct xhci_ring *cur_ring;
	dma_addr_t addr;

	if (!stream_info)
		return;

	for (cur_stream = 1; cur_stream < stream_info->num_streams;
			cur_stream++) {
		cur_ring = stream_info->stream_rings[cur_stream];
		if (cur_ring) {
			addr = cur_ring->first_seg->dma;
			radix_tree_delete(&stream_info->trb_address_map,
					addr >> TRB_SEGMENT_SHIFT);
			xhci_ring_free(xhci, cur_ring);
			stream_info->stream_rings[cur_stream] = NULL;
		}
	}
	xhci_free_command(xhci, stream_info->free_streams_command);
	xhci->cmd_ring_reserved_trbs--;
	if (stream_info->stream_ctx_array)
		xhci_free_stream_ctx(xhci,
				stream_info->num_stream_ctxs,
				stream_info->stream_ctx_array,
				stream_info->ctx_array_dma);

	if (stream_info)
		kfree(stream_info->stream_rings);
	kfree(stream_info);
}


/***************** Device context manipulation *************************/

static void xhci_init_endpoint_timer(struct xhci_hcd *xhci,
		struct xhci_virt_ep *ep)
{
	init_timer(&ep->stop_cmd_timer);
	ep->stop_cmd_timer.data = (unsigned long) ep;
	ep->stop_cmd_timer.function = xhci_stop_endpoint_command_watchdog;
	ep->xhci = xhci;
}

static void xhci_free_tt_info(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		int slot_id)
{
	struct list_head *tt_list_head;
	struct xhci_tt_bw_info *tt_info, *next;
	bool slot_found = false;

	/* If the device never made it past the Set Address stage,
	 * it may not have the real_port set correctly.
	 */
	if (virt_dev->real_port == 0 ||
			virt_dev->real_port > HCS_MAX_PORTS(xhci->hcs_params1)) {
		xhci_dbg(xhci, "Bad real port.\n");
		return;
	}

	tt_list_head = &(xhci->rh_bw[virt_dev->real_port - 1].tts);
	list_for_each_entry_safe(tt_info, next, tt_list_head, tt_list) {
		/* Multi-TT hubs will have more than one entry */
		if (tt_info->slot_id == slot_id) {
			slot_found = true;
			list_del(&tt_info->tt_list);
			kfree(tt_info);
		} else if (slot_found) {
			break;
		}
	}
}

int xhci_alloc_tt_info(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		struct usb_device *hdev,
		struct usb_tt *tt, gfp_t mem_flags)
{
	struct xhci_tt_bw_info		*tt_info;
	unsigned int			num_ports;
	int				i, j;

	if (!tt->multi)
		num_ports = 1;
	else
		num_ports = hdev->maxchild;

	for (i = 0; i < num_ports; i++, tt_info++) {
		struct xhci_interval_bw_table *bw_table;

		tt_info = kzalloc(sizeof(*tt_info), mem_flags);
		if (!tt_info)
			goto free_tts;
		INIT_LIST_HEAD(&tt_info->tt_list);
		list_add(&tt_info->tt_list,
				&xhci->rh_bw[virt_dev->real_port - 1].tts);
		tt_info->slot_id = virt_dev->udev->slot_id;
		if (tt->multi)
			tt_info->ttport = i+1;
		bw_table = &tt_info->bw_table;
		for (j = 0; j < XHCI_MAX_INTERVAL; j++)
			INIT_LIST_HEAD(&bw_table->interval_bw[j].endpoints);
	}
	return 0;

free_tts:
	xhci_free_tt_info(xhci, virt_dev, virt_dev->udev->slot_id);
	return -ENOMEM;
}


/* All the xhci_tds in the ring's TD list should be freed at this point.
 * Should be called with xhci->lock held if there is any chance the TT lists
 * will be manipulated by the configure endpoint, allocate device, or update
 * hub functions while this function is removing the TT entries from the list.
 */
void xhci_free_virt_device(struct xhci_hcd *xhci, int slot_id)
{
	struct xhci_virt_device *dev;
	int i;
	int old_active_eps = 0;

	/* Slot ID 0 is reserved */
	if (slot_id == 0 || !xhci->devs[slot_id])
		return;

	dev = xhci->devs[slot_id];
	xhci->dcbaa->dev_context_ptrs[slot_id] = 0;
	if (!dev)
		return;

	if (dev->tt_info)
		old_active_eps = dev->tt_info->active_eps;

	for (i = 0; i < 31; ++i) {
		if (dev->eps[i].ring)
			xhci_ring_free(xhci, dev->eps[i].ring);
		if (dev->eps[i].stream_info)
			xhci_free_stream_info(xhci,
					dev->eps[i].stream_info);
		/* Endpoints on the TT/root port lists should have been removed
		 * when usb_disable_device() was called for the device.
		 * We can't drop them anyway, because the udev might have gone
		 * away by this point, and we can't tell what speed it was.
		 */
		if (!list_empty(&dev->eps[i].bw_endpoint_list))
			xhci_warn(xhci, "Slot %u endpoint %u "
					"not removed from BW list!\n",
					slot_id, i);
	}
	/* If this is a hub, free the TT(s) from the TT list */
	xhci_free_tt_info(xhci, dev, slot_id);
	/* If necessary, update the number of active TTs on this root port */
	xhci_update_tt_active_eps(xhci, dev, old_active_eps);

	if (dev->ring_cache) {
		for (i = 0; i < dev->num_rings_cached; i++)
			xhci_ring_free(xhci, dev->ring_cache[i]);
		kfree(dev->ring_cache);
	}

	if (dev->in_ctx)
		xhci_free_container_ctx(xhci, dev->in_ctx);
	if (dev->out_ctx)
		xhci_free_container_ctx(xhci, dev->out_ctx);

	kfree(xhci->devs[slot_id]);
	xhci->devs[slot_id] = NULL;
}

int xhci_alloc_virt_device(struct xhci_hcd *xhci, int slot_id,
		struct usb_device *udev, gfp_t flags)
{
	struct xhci_virt_device *dev;
	int i;

	/* Slot ID 0 is reserved */
	if (slot_id == 0 || xhci->devs[slot_id]) {
		xhci_warn(xhci, "Bad Slot ID %d\n", slot_id);
		return 0;
	}

	xhci->devs[slot_id] = kzalloc(sizeof(*xhci->devs[slot_id]), flags);
	if (!xhci->devs[slot_id])
		return 0;
	dev = xhci->devs[slot_id];

	/* Allocate the (output) device context that will be used in the HC. */
	dev->out_ctx = xhci_alloc_container_ctx(xhci, XHCI_CTX_TYPE_DEVICE, flags);
	if (!dev->out_ctx)
		goto fail;

	xhci_dbg(xhci, "Slot %d output ctx = 0x%llx (dma)\n", slot_id,
			(unsigned long long)dev->out_ctx->dma);

	/* Allocate the (input) device context for address device command */
	dev->in_ctx = xhci_alloc_container_ctx(xhci, XHCI_CTX_TYPE_INPUT, flags);
	if (!dev->in_ctx)
		goto fail;

	xhci_dbg(xhci, "Slot %d input ctx = 0x%llx (dma)\n", slot_id,
			(unsigned long long)dev->in_ctx->dma);

	/* Initialize the cancellation list and watchdog timers for each ep */
	for (i = 0; i < 31; i++) {
		xhci_init_endpoint_timer(xhci, &dev->eps[i]);
		INIT_LIST_HEAD(&dev->eps[i].cancelled_td_list);
		INIT_LIST_HEAD(&dev->eps[i].bw_endpoint_list);
	}

	/* Allocate endpoint 0 ring */
	dev->eps[0].ring = xhci_ring_alloc(xhci, 2, 1, TYPE_CTRL, flags);
	if (!dev->eps[0].ring)
		goto fail;

	/* Allocate pointers to the ring cache */
	dev->ring_cache = kzalloc(
			sizeof(struct xhci_ring *)*XHCI_MAX_RINGS_CACHED,
			flags);
	if (!dev->ring_cache)
		goto fail;
	dev->num_rings_cached = 0;

	init_completion(&dev->cmd_completion);
	INIT_LIST_HEAD(&dev->cmd_list);
	dev->udev = udev;

	/* Point to output device context in dcbaa. */
	xhci->dcbaa->dev_context_ptrs[slot_id] = cpu_to_le64(dev->out_ctx->dma);
	xhci_dbg(xhci, "Set slot id %d dcbaa entry %p to 0x%llx\n",
		 slot_id,
		 &xhci->dcbaa->dev_context_ptrs[slot_id],
		 le64_to_cpu(xhci->dcbaa->dev_context_ptrs[slot_id]));

	return 1;
fail:
	xhci_free_virt_device(xhci, slot_id);
	return 0;
}

void xhci_copy_ep0_dequeue_into_input_ctx(struct xhci_hcd *xhci,
		struct usb_device *udev)
{
	struct xhci_virt_device *virt_dev;
	struct xhci_ep_ctx	*ep0_ctx;
	struct xhci_ring	*ep_ring;

	virt_dev = xhci->devs[udev->slot_id];
	ep0_ctx = xhci_get_ep_ctx(xhci, virt_dev->in_ctx, 0);
	ep_ring = virt_dev->eps[0].ring;
	/*
	 * FIXME we don't keep track of the dequeue pointer very well after a
	 * Set TR dequeue pointer, so we're setting the dequeue pointer of the
	 * host to our enqueue pointer.  This should only be called after a
	 * configured device has reset, so all control transfers should have
	 * been completed or cancelled before the reset.
	 */
	ep0_ctx->deq = cpu_to_le64(xhci_trb_virt_to_dma(ep_ring->enq_seg,
							ep_ring->enqueue)
				   | ep_ring->cycle_state);
}

/*
 * The xHCI roothub may have ports of differing speeds in any order in the port
 * status registers.  xhci->port_array provides an array of the port speed for
 * each offset into the port status registers.
 *
 * The xHCI hardware wants to know the roothub port number that the USB device
 * is attached to (or the roothub port its ancestor hub is attached to).  All we
 * know is the index of that port under either the USB 2.0 or the USB 3.0
 * roothub, but that doesn't give us the real index into the HW port status
 * registers. Call xhci_find_raw_port_number() to get real index.
 */
static u32 xhci_find_real_port_number(struct xhci_hcd *xhci,
		struct usb_device *udev)
{
	struct usb_device *top_dev;
	struct usb_hcd *hcd;

	if (udev->speed == USB_SPEED_SUPER)
		hcd = xhci->shared_hcd;
	else
		hcd = xhci->main_hcd;

	for (top_dev = udev; top_dev->parent && top_dev->parent->parent;
			top_dev = top_dev->parent)
		/* Found device below root hub */;

	return	xhci_find_raw_port_number(hcd, top_dev->portnum);
}

/* Setup an xHCI virtual device for a Set Address command */
int xhci_setup_addressable_virt_dev(struct xhci_hcd *xhci, struct usb_device *udev)
{
	struct xhci_virt_device *dev;
	struct xhci_ep_ctx	*ep0_ctx;
	struct xhci_slot_ctx    *slot_ctx;
	u32			port_num;
	struct usb_device *top_dev;

	dev = xhci->devs[udev->slot_id];
	/* Slot ID 0 is reserved */
	if (udev->slot_id == 0 || !dev) {
		xhci_warn(xhci, "Slot ID %d is not assigned to this device\n",
				udev->slot_id);
		return -EINVAL;
	}
	ep0_ctx = xhci_get_ep_ctx(xhci, dev->in_ctx, 0);
	slot_ctx = xhci_get_slot_ctx(xhci, dev->in_ctx);

	/* 3) Only the control endpoint is valid - one endpoint context */
	slot_ctx->dev_info |= cpu_to_le32(LAST_CTX(1) | udev->route);
	switch (udev->speed) {
	case USB_SPEED_SUPER:
		slot_ctx->dev_info |= cpu_to_le32(SLOT_SPEED_SS);
		break;
	case USB_SPEED_HIGH:
		slot_ctx->dev_info |= cpu_to_le32(SLOT_SPEED_HS);
		break;
	case USB_SPEED_FULL:
		slot_ctx->dev_info |= cpu_to_le32(SLOT_SPEED_FS);
		break;
	case USB_SPEED_LOW:
		slot_ctx->dev_info |= cpu_to_le32(SLOT_SPEED_LS);
		break;
	case USB_SPEED_WIRELESS:
		xhci_dbg(xhci, "FIXME xHCI doesn't support wireless speeds\n");
		return -EINVAL;
		break;
	default:
		/* Speed was set earlier, this shouldn't happen. */
		BUG();
	}
	/* Find the root hub port this device is under */
	port_num = xhci_find_real_port_number(xhci, udev);
	if (!port_num)
		return -EINVAL;
	slot_ctx->dev_info2 |= cpu_to_le32(ROOT_HUB_PORT(port_num));
	/* Set the port number in the virtual_device to the faked port number */
	for (top_dev = udev; top_dev->parent && top_dev->parent->parent;
			top_dev = top_dev->parent)
		/* Found device below root hub */;
	dev->fake_port = top_dev->portnum;
	dev->real_port = port_num;
	xhci_dbg(xhci, "Set root hub portnum to %d\n", port_num);
	xhci_dbg(xhci, "Set fake root hub portnum to %d\n", dev->fake_port);

	/* Find the right bandwidth table that this device will be a part of.
	 * If this is a full speed device attached directly to a root port (or a
	 * decendent of one), it counts as a primary bandwidth domain, not a
	 * secondary bandwidth domain under a TT.  An xhci_tt_info structure
	 * will never be created for the HS root hub.
	 */
	if (!udev->tt || !udev->tt->hub->parent) {
		dev->bw_table = &xhci->rh_bw[port_num - 1].bw_table;
	} else {
		struct xhci_root_port_bw_info *rh_bw;
		struct xhci_tt_bw_info *tt_bw;

		rh_bw = &xhci->rh_bw[port_num - 1];
		/* Find the right TT. */
		list_for_each_entry(tt_bw, &rh_bw->tts, tt_list) {
			if (tt_bw->slot_id != udev->tt->hub->slot_id)
				continue;

			if (!dev->udev->tt->multi ||
					(udev->tt->multi &&
					 tt_bw->ttport == dev->udev->ttport)) {
				dev->bw_table = &tt_bw->bw_table;
				dev->tt_info = tt_bw;
				break;
			}
		}
		if (!dev->tt_info)
			xhci_warn(xhci, "WARN: Didn't find a matching TT\n");
	}

	/* Is this a LS/FS device under an external HS hub? */
	if (udev->tt && udev->tt->hub->parent) {
		slot_ctx->tt_info = cpu_to_le32(udev->tt->hub->slot_id |
						(udev->ttport << 8));
		if (udev->tt->multi)
			slot_ctx->dev_info |= cpu_to_le32(DEV_MTT);
	}
	xhci_dbg(xhci, "udev->tt = %p\n", udev->tt);
	xhci_dbg(xhci, "udev->ttport = 0x%x\n", udev->ttport);

	/* Step 4 - ring already allocated */
	/* Step 5 */
	ep0_ctx->ep_info2 = cpu_to_le32(EP_TYPE(CTRL_EP));
	/*
	 * XXX: Not sure about wireless USB devices.
	 */
	switch (udev->speed) {
	case USB_SPEED_SUPER:
		ep0_ctx->ep_info2 |= cpu_to_le32(MAX_PACKET(512));
		break;
	case USB_SPEED_HIGH:
	/* USB core guesses at a 64-byte max packet first for FS devices */
	case USB_SPEED_FULL:
		ep0_ctx->ep_info2 |= cpu_to_le32(MAX_PACKET(64));
		break;
	case USB_SPEED_LOW:
		ep0_ctx->ep_info2 |= cpu_to_le32(MAX_PACKET(8));
		break;
	case USB_SPEED_WIRELESS:
		xhci_dbg(xhci, "FIXME xHCI doesn't support wireless speeds\n");
		return -EINVAL;
		break;
	default:
		/* New speed? */
		BUG();
	}
	/* EP 0 can handle "burst" sizes of 1, so Max Burst Size field is 0 */
	ep0_ctx->ep_info2 |= cpu_to_le32(MAX_BURST(0) | ERROR_COUNT(3));

	ep0_ctx->deq = cpu_to_le64(dev->eps[0].ring->first_seg->dma |
				   dev->eps[0].ring->cycle_state);

	/* Steps 7 and 8 were done in xhci_alloc_virt_device() */

	return 0;
}

/*
 * Convert interval expressed as 2^(bInterval - 1) == interval into
 * straight exponent value 2^n == interval.
 *
 */
static unsigned int xhci_parse_exponent_interval(struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	unsigned int interval;

	interval = clamp_val(ep->desc.bInterval, 1, 16) - 1;
	if (interval != ep->desc.bInterval - 1)
		dev_warn(&udev->dev,
			 "ep %#x - rounding interval to %d %sframes\n",
			 ep->desc.bEndpointAddress,
			 1 << interval,
			 udev->speed == USB_SPEED_FULL ? "" : "micro");

	if (udev->speed == USB_SPEED_FULL) {
		/*
		 * Full speed isoc endpoints specify interval in frames,
		 * not microframes. We are using microframes everywhere,
		 * so adjust accordingly.
		 */
		interval += 3;	/* 1 frame = 2^3 uframes */
	}

	return interval;
}

/*
 * Convert bInterval expressed in microframes (in 1-255 range) to exponent of
 * microframes, rounded down to nearest power of 2.
 */
static unsigned int xhci_microframes_to_exponent(struct usb_device *udev,
		struct usb_host_endpoint *ep, unsigned int desc_interval,
		unsigned int min_exponent, unsigned int max_exponent)
{
	unsigned int interval;

	interval = fls(desc_interval) - 1;
	interval = clamp_val(interval, min_exponent, max_exponent);
	if ((1 << interval) != desc_interval)
		dev_warn(&udev->dev,
			 "ep %#x - rounding interval to %d microframes, ep desc says %d microframes\n",
			 ep->desc.bEndpointAddress,
			 1 << interval,
			 desc_interval);

	return interval;
}

static unsigned int xhci_parse_microframe_interval(struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	if (ep->desc.bInterval == 0)
		return 0;
	return xhci_microframes_to_exponent(udev, ep,
			ep->desc.bInterval, 0, 15);
}


static unsigned int xhci_parse_frame_interval(struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	return xhci_microframes_to_exponent(udev, ep,
			ep->desc.bInterval * 8, 3, 10);
}

/* Return the polling or NAK interval.
 *
 * The polling interval is expressed in "microframes".  If xHCI's Interval field
 * is set to N, it will service the endpoint every 2^(Interval)*125us.
 *
 * The NAK interval is one NAK per 1 to 255 microframes, or no NAKs if interval
 * is set to 0.
 */
static unsigned int xhci_get_endpoint_interval(struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	unsigned int interval = 0;

	switch (udev->speed) {
	case USB_SPEED_HIGH:
		/* Max NAK rate */
		if (usb_endpoint_xfer_control(&ep->desc) ||
		    usb_endpoint_xfer_bulk(&ep->desc)) {
			interval = xhci_parse_microframe_interval(udev, ep);
			break;
		}
		/* Fall through - SS and HS isoc/int have same decoding */

	case USB_SPEED_SUPER:
		if (usb_endpoint_xfer_int(&ep->desc) ||
		    usb_endpoint_xfer_isoc(&ep->desc)) {
			interval = xhci_parse_exponent_interval(udev, ep);
		}
		break;

	case USB_SPEED_FULL:
		if (usb_endpoint_xfer_isoc(&ep->desc)) {
			interval = xhci_parse_exponent_interval(udev, ep);
			break;
		}
		/*
		 * Fall through for interrupt endpoint interval decoding
		 * since it uses the same rules as low speed interrupt
		 * endpoints.
		 */

	case USB_SPEED_LOW:
		if (usb_endpoint_xfer_int(&ep->desc) ||
		    usb_endpoint_xfer_isoc(&ep->desc)) {

			interval = xhci_parse_frame_interval(udev, ep);
		}
		break;

	default:
		BUG();
	}
	return EP_INTERVAL(interval);
}

/* The "Mult" field in the endpoint context is only set for SuperSpeed isoc eps.
 * High speed endpoint descriptors can define "the number of additional
 * transaction opportunities per microframe", but that goes in the Max Burst
 * endpoint context field.
 */
static u32 xhci_get_endpoint_mult(struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	if (udev->speed != USB_SPEED_SUPER ||
			!usb_endpoint_xfer_isoc(&ep->desc))
		return 0;
	return ep->ss_ep_comp.bmAttributes;
}

static u32 xhci_get_endpoint_type(struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	int in;
	u32 type;

	in = usb_endpoint_dir_in(&ep->desc);
	if (usb_endpoint_xfer_control(&ep->desc)) {
		type = EP_TYPE(CTRL_EP);
	} else if (usb_endpoint_xfer_bulk(&ep->desc)) {
		if (in)
			type = EP_TYPE(BULK_IN_EP);
		else
			type = EP_TYPE(BULK_OUT_EP);
	} else if (usb_endpoint_xfer_isoc(&ep->desc)) {
		if (in)
			type = EP_TYPE(ISOC_IN_EP);
		else
			type = EP_TYPE(ISOC_OUT_EP);
	} else if (usb_endpoint_xfer_int(&ep->desc)) {
		if (in)
			type = EP_TYPE(INT_IN_EP);
		else
			type = EP_TYPE(INT_OUT_EP);
	} else {
		BUG();
	}
	return type;
}

/* Return the maximum endpoint service interval time (ESIT) payload.
 * Basically, this is the maxpacket size, multiplied by the burst size
 * and mult size.
 */
static u32 xhci_get_max_esit_payload(struct xhci_hcd *xhci,
		struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	int max_burst;
	int max_packet;

	/* Only applies for interrupt or isochronous endpoints */
	if (usb_endpoint_xfer_control(&ep->desc) ||
			usb_endpoint_xfer_bulk(&ep->desc))
		return 0;

	if (udev->speed == USB_SPEED_SUPER)
		return le16_to_cpu(ep->ss_ep_comp.wBytesPerInterval);

	max_packet = GET_MAX_PACKET(usb_endpoint_maxp(&ep->desc));
	max_burst = (usb_endpoint_maxp(&ep->desc) & 0x1800) >> 11;
	/* A 0 in max burst means 1 transfer per ESIT */
	return max_packet * (max_burst + 1);
}

/* Set up an endpoint with one ring segment.  Do not allocate stream rings.
 * Drivers will have to call usb_alloc_streams() to do that.
 */
int xhci_endpoint_init(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		struct usb_device *udev,
		struct usb_host_endpoint *ep,
		gfp_t mem_flags)
{
	unsigned int ep_index;
	struct xhci_ep_ctx *ep_ctx;
	struct xhci_ring *ep_ring;
	unsigned int max_packet;
	unsigned int max_burst;
	enum xhci_ring_type type;
	u32 max_esit_payload;

	ep_index = xhci_get_endpoint_index(&ep->desc);
	ep_ctx = xhci_get_ep_ctx(xhci, virt_dev->in_ctx, ep_index);

	type = usb_endpoint_type(&ep->desc);
	/* Set up the endpoint ring */
	virt_dev->eps[ep_index].new_ring =
		xhci_ring_alloc(xhci, 2, 1, type, mem_flags);
	if (!virt_dev->eps[ep_index].new_ring) {
		/* Attempt to use the ring cache */
		if (virt_dev->num_rings_cached == 0)
			return -ENOMEM;
		virt_dev->eps[ep_index].new_ring =
			virt_dev->ring_cache[virt_dev->num_rings_cached];
		virt_dev->ring_cache[virt_dev->num_rings_cached] = NULL;
		virt_dev->num_rings_cached--;
		xhci_reinit_cached_ring(xhci, virt_dev->eps[ep_index].new_ring,
					1, type);
	}
	virt_dev->eps[ep_index].skip = false;
	ep_ring = virt_dev->eps[ep_index].new_ring;
	ep_ctx->deq = cpu_to_le64(ep_ring->first_seg->dma | ep_ring->cycle_state);

	ep_ctx->ep_info = cpu_to_le32(xhci_get_endpoint_interval(udev, ep)
				      | EP_MULT(xhci_get_endpoint_mult(udev, ep)));

	/* FIXME dig Mult and streams info out of ep companion desc */

	/* Allow 3 retries for everything but isoc;
	 * CErr shall be set to 0 for Isoch endpoints.
	 */
	if (!usb_endpoint_xfer_isoc(&ep->desc))
		ep_ctx->ep_info2 = cpu_to_le32(ERROR_COUNT(3));
	else
		ep_ctx->ep_info2 = cpu_to_le32(ERROR_COUNT(0));

	ep_ctx->ep_info2 |= cpu_to_le32(xhci_get_endpoint_type(udev, ep));

	/* Set the max packet size and max burst */
	max_packet = GET_MAX_PACKET(usb_endpoint_maxp(&ep->desc));
	max_burst = 0;
	switch (udev->speed) {
	case USB_SPEED_SUPER:
		/* dig out max burst from ep companion desc */
		max_burst = ep->ss_ep_comp.bMaxBurst;
		break;
	case USB_SPEED_HIGH:
		/* Some devices get this wrong */
		if (usb_endpoint_xfer_bulk(&ep->desc))
			max_packet = 512;
		/* bits 11:12 specify the number of additional transaction
		 * opportunities per microframe (USB 2.0, section 9.6.6)
		 */
		if (usb_endpoint_xfer_isoc(&ep->desc) ||
				usb_endpoint_xfer_int(&ep->desc)) {
			max_burst = (usb_endpoint_maxp(&ep->desc)
				     & 0x1800) >> 11;
		}
		break;
	case USB_SPEED_FULL:
	case USB_SPEED_LOW:
		break;
	default:
		BUG();
	}
	ep_ctx->ep_info2 |= cpu_to_le32(MAX_PACKET(max_packet) |
			MAX_BURST(max_burst));
	max_esit_payload = xhci_get_max_esit_payload(xhci, udev, ep);
	ep_ctx->tx_info = cpu_to_le32(MAX_ESIT_PAYLOAD_FOR_EP(max_esit_payload));

	/*
	 * XXX no idea how to calculate the average TRB buffer length for bulk
	 * endpoints, as the driver gives us no clue how big each scatter gather
	 * list entry (or buffer) is going to be.
	 *
	 * For isochronous and interrupt endpoints, we set it to the max
	 * available, until we have new API in the USB core to allow drivers to
	 * declare how much bandwidth they actually need.
	 *
	 * Normally, it would be calculated by taking the total of the buffer
	 * lengths in the TD and then dividing by the number of TRBs in a TD,
	 * including link TRBs, No-op TRBs, and Event data TRBs.  Since we don't
	 * use Event Data TRBs, and we don't chain in a link TRB on short
	 * transfers, we're basically dividing by 1.
	 *
	 * xHCI 1.0 specification indicates that the Average TRB Length should
	 * be set to 8 for control endpoints.
	 */
	if (usb_endpoint_xfer_control(&ep->desc) && xhci->hci_version == 0x100)
		ep_ctx->tx_info |= cpu_to_le32(AVG_TRB_LENGTH_FOR_EP(8));
	else
		ep_ctx->tx_info |=
			 cpu_to_le32(AVG_TRB_LENGTH_FOR_EP(max_esit_payload));

	/* FIXME Debug endpoint context */
	return 0;
}

void xhci_endpoint_zero(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		struct usb_host_endpoint *ep)
{
	unsigned int ep_index;
	struct xhci_ep_ctx *ep_ctx;

	ep_index = xhci_get_endpoint_index(&ep->desc);
	ep_ctx = xhci_get_ep_ctx(xhci, virt_dev->in_ctx, ep_index);

	ep_ctx->ep_info = 0;
	ep_ctx->ep_info2 = 0;
	ep_ctx->deq = 0;
	ep_ctx->tx_info = 0;
	/* Don't free the endpoint ring until the set interface or configuration
	 * request succeeds.
	 */
}

void xhci_clear_endpoint_bw_info(struct xhci_bw_info *bw_info)
{
	bw_info->ep_interval = 0;
	bw_info->mult = 0;
	bw_info->num_packets = 0;
	bw_info->max_packet_size = 0;
	bw_info->type = 0;
	bw_info->max_esit_payload = 0;
}

void xhci_update_bw_info(struct xhci_hcd *xhci,
		struct xhci_container_ctx *in_ctx,
		struct xhci_input_control_ctx *ctrl_ctx,
		struct xhci_virt_device *virt_dev)
{
	struct xhci_bw_info *bw_info;
	struct xhci_ep_ctx *ep_ctx;
	unsigned int ep_type;
	int i;

	for (i = 1; i < 31; ++i) {
		bw_info = &virt_dev->eps[i].bw_info;

		/* We can't tell what endpoint type is being dropped, but
		 * unconditionally clearing the bandwidth info for non-periodic
		 * endpoints should be harmless because the info will never be
		 * set in the first place.
		 */
		if (!EP_IS_ADDED(ctrl_ctx, i) && EP_IS_DROPPED(ctrl_ctx, i)) {
			/* Dropped endpoint */
			xhci_clear_endpoint_bw_info(bw_info);
			continue;
		}

		if (EP_IS_ADDED(ctrl_ctx, i)) {
			ep_ctx = xhci_get_ep_ctx(xhci, in_ctx, i);
			ep_type = CTX_TO_EP_TYPE(le32_to_cpu(ep_ctx->ep_info2));

			/* Ignore non-periodic endpoints */
			if (ep_type != ISOC_OUT_EP && ep_type != INT_OUT_EP &&
					ep_type != ISOC_IN_EP &&
					ep_type != INT_IN_EP)
				continue;

			/* Added or changed endpoint */
			bw_info->ep_interval = CTX_TO_EP_INTERVAL(
					le32_to_cpu(ep_ctx->ep_info));
			/* Number of packets and mult are zero-based in the
			 * input context, but we want one-based for the
			 * interval table.
			 */
			bw_info->mult = CTX_TO_EP_MULT(
					le32_to_cpu(ep_ctx->ep_info)) + 1;
			bw_info->num_packets = CTX_TO_MAX_BURST(
					le32_to_cpu(ep_ctx->ep_info2)) + 1;
			bw_info->max_packet_size = MAX_PACKET_DECODED(
					le32_to_cpu(ep_ctx->ep_info2));
			bw_info->type = ep_type;
			bw_info->max_esit_payload = CTX_TO_MAX_ESIT_PAYLOAD(
					le32_to_cpu(ep_ctx->tx_info));
		}
	}
}

/* Copy output xhci_ep_ctx to the input xhci_ep_ctx copy.
 * Useful when you want to change one particular aspect of the endpoint and then
 * issue a configure endpoint command.
 */
void xhci_endpoint_copy(struct xhci_hcd *xhci,
		struct xhci_container_ctx *in_ctx,
		struct xhci_container_ctx *out_ctx,
		unsigned int ep_index)
{
	struct xhci_ep_ctx *out_ep_ctx;
	struct xhci_ep_ctx *in_ep_ctx;

	out_ep_ctx = xhci_get_ep_ctx(xhci, out_ctx, ep_index);
	in_ep_ctx = xhci_get_ep_ctx(xhci, in_ctx, ep_index);

	in_ep_ctx->ep_info = out_ep_ctx->ep_info;
	in_ep_ctx->ep_info2 = out_ep_ctx->ep_info2;
	in_ep_ctx->deq = out_ep_ctx->deq;
	in_ep_ctx->tx_info = out_ep_ctx->tx_info;
}

/* Copy output xhci_slot_ctx to the input xhci_slot_ctx.
 * Useful when you want to change one particular aspect of the endpoint and then
 * issue a configure endpoint command.  Only the context entries field matters,
 * but we'll copy the whole thing anyway.
 */
void xhci_slot_copy(struct xhci_hcd *xhci,
		struct xhci_container_ctx *in_ctx,
		struct xhci_container_ctx *out_ctx)
{
	struct xhci_slot_ctx *in_slot_ctx;
	struct xhci_slot_ctx *out_slot_ctx;

	in_slot_ctx = xhci_get_slot_ctx(xhci, in_ctx);
	out_slot_ctx = xhci_get_slot_ctx(xhci, out_ctx);

	in_slot_ctx->dev_info = out_slot_ctx->dev_info;
	in_slot_ctx->dev_info2 = out_slot_ctx->dev_info2;
	in_slot_ctx->tt_info = out_slot_ctx->tt_info;
	in_slot_ctx->dev_state = out_slot_ctx->dev_state;
}

/* Set up the scratchpad buffer array and scratchpad buffers, if needed. */
static int scratchpad_alloc(struct xhci_hcd *xhci, gfp_t flags)
{
	int i;
	struct device *dev = xhci_to_hcd(xhci)->self.controller;
	int num_sp = HCS_MAX_SCRATCHPAD(xhci->hcs_params2);

	xhci_dbg(xhci, "Allocating %d scratchpad buffers\n", num_sp);

	if (!num_sp)
		return 0;

	xhci->scratchpad = kzalloc(sizeof(*xhci->scratchpad), flags);
	if (!xhci->scratchpad)
		goto fail_sp;

	xhci->scratchpad->sp_array = dma_alloc_coherent(dev,
				     num_sp * sizeof(u64),
				     &xhci->scratchpad->sp_dma, flags);
	if (!xhci->scratchpad->sp_array)
		goto fail_sp2;

	xhci->scratchpad->sp_buffers = kzalloc(sizeof(void *) * num_sp, flags);
	if (!xhci->scratchpad->sp_buffers)
		goto fail_sp3;

	xhci->scratchpad->sp_dma_buffers =
		kzalloc(sizeof(dma_addr_t) * num_sp, flags);

	if (!xhci->scratchpad->sp_dma_buffers)
		goto fail_sp4;

	xhci->dcbaa->dev_context_ptrs[0] = cpu_to_le64(xhci->scratchpad->sp_dma);
	for (i = 0; i < num_sp; i++) {
		dma_addr_t dma;
		void *buf = dma_alloc_coherent(dev, xhci->page_size, &dma,
				flags);
		if (!buf)
			goto fail_sp5;

		xhci->scratchpad->sp_array[i] = dma;
		xhci->scratchpad->sp_buffers[i] = buf;
		xhci->scratchpad->sp_dma_buffers[i] = dma;
	}

	return 0;

 fail_sp5:
	for (i = i - 1; i >= 0; i--) {
		dma_free_coherent(dev, xhci->page_size,
				    xhci->scratchpad->sp_buffers[i],
				    xhci->scratchpad->sp_dma_buffers[i]);
	}
	kfree(xhci->scratchpad->sp_dma_buffers);

 fail_sp4:
	kfree(xhci->scratchpad->sp_buffers);

 fail_sp3:
	dma_free_coherent(dev, num_sp * sizeof(u64),
			    xhci->scratchpad->sp_array,
			    xhci->scratchpad->sp_dma);

 fail_sp2:
	kfree(xhci->scratchpad);
	xhci->scratchpad = NULL;

 fail_sp:
	return -ENOMEM;
}

static void scratchpad_free(struct xhci_hcd *xhci)
{
	int num_sp;
	int i;
	struct pci_dev	*pdev = to_pci_dev(xhci_to_hcd(xhci)->self.controller);

	if (!xhci->scratchpad)
		return;

	num_sp = HCS_MAX_SCRATCHPAD(xhci->hcs_params2);

	for (i = 0; i < num_sp; i++) {
		dma_free_coherent(&pdev->dev, xhci->page_size,
				    xhci->scratchpad->sp_buffers[i],
				    xhci->scratchpad->sp_dma_buffers[i]);
	}
	kfree(xhci->scratchpad->sp_dma_buffers);
	kfree(xhci->scratchpad->sp_buffers);
	dma_free_coherent(&pdev->dev, num_sp * sizeof(u64),
			    xhci->scratchpad->sp_array,
			    xhci->scratchpad->sp_dma);
	kfree(xhci->scratchpad);
	xhci->scratchpad = NULL;
}

struct xhci_command *xhci_alloc_command(struct xhci_hcd *xhci,
		bool allocate_in_ctx, bool allocate_completion,
		gfp_t mem_flags)
{
	struct xhci_command *command;

	command = kzalloc(sizeof(*command), mem_flags);
	if (!command)
		return NULL;

	if (allocate_in_ctx) {
		command->in_ctx =
			xhci_alloc_container_ctx(xhci, XHCI_CTX_TYPE_INPUT,
					mem_flags);
		if (!command->in_ctx) {
			kfree(command);
			return NULL;
		}
	}

	if (allocate_completion) {
		command->completion =
			kzalloc(sizeof(struct completion), mem_flags);
		if (!command->completion) {
			xhci_free_container_ctx(xhci, command->in_ctx);
			kfree(command);
			return NULL;
		}
		init_completion(command->completion);
	}

	command->status = 0;
	INIT_LIST_HEAD(&command->cmd_list);
	return command;
}

void xhci_urb_free_priv(struct xhci_hcd *xhci, struct urb_priv *urb_priv)
{
	if (urb_priv) {
		kfree(urb_priv->td[0]);
		kfree(urb_priv);
	}
}

void xhci_free_command(struct xhci_hcd *xhci,
		struct xhci_command *command)
{
	xhci_free_container_ctx(xhci,
			command->in_ctx);
	kfree(command->completion);
	kfree(command);
}

void xhci_mem_cleanup(struct xhci_hcd *xhci)
{
	struct pci_dev	*pdev = to_pci_dev(xhci_to_hcd(xhci)->self.controller);
	struct dev_info	*dev_info, *next;
	struct xhci_cd  *cur_cd, *next_cd;
	unsigned long	flags;
	int size;
	int i, j, num_ports;

	/* Free the Event Ring Segment Table and the actual Event Ring */
	size = sizeof(struct xhci_erst_entry)*(xhci->erst.num_entries);
	if (xhci->erst.entries)
		dma_free_coherent(&pdev->dev, size,
				xhci->erst.entries, xhci->erst.erst_dma_addr);
	xhci->erst.entries = NULL;
	xhci_dbg(xhci, "Freed ERST\n");
	if (xhci->event_ring)
		xhci_ring_free(xhci, xhci->event_ring);
	xhci->event_ring = NULL;
	xhci_dbg(xhci, "Freed event ring\n");

	if (xhci->lpm_command)
		xhci_free_command(xhci, xhci->lpm_command);
	xhci->cmd_ring_reserved_trbs = 0;
	if (xhci->cmd_ring)
		xhci_ring_free(xhci, xhci->cmd_ring);
	xhci->cmd_ring = NULL;
	xhci_dbg(xhci, "Freed command ring\n");
	list_for_each_entry_safe(cur_cd, next_cd,
			&xhci->cancel_cmd_list, cancel_cmd_list) {
		list_del(&cur_cd->cancel_cmd_list);
		kfree(cur_cd);
	}

	for (i = 1; i < MAX_HC_SLOTS; ++i)
		xhci_free_virt_device(xhci, i);

	if (xhci->segment_pool)
		dma_pool_destroy(xhci->segment_pool);
	xhci->segment_pool = NULL;
	xhci_dbg(xhci, "Freed segment pool\n");

	if (xhci->device_pool)
		dma_pool_destroy(xhci->device_pool);
	xhci->device_pool = NULL;
	xhci_dbg(xhci, "Freed device context pool\n");

	if (xhci->small_streams_pool)
		dma_pool_destroy(xhci->small_streams_pool);
	xhci->small_streams_pool = NULL;
	xhci_dbg(xhci, "Freed small stream array pool\n");

	if (xhci->medium_streams_pool)
		dma_pool_destroy(xhci->medium_streams_pool);
	xhci->medium_streams_pool = NULL;
	xhci_dbg(xhci, "Freed medium stream array pool\n");

	if (xhci->dcbaa)
		dma_free_coherent(&pdev->dev, sizeof(*xhci->dcbaa),
				xhci->dcbaa, xhci->dcbaa->dma);
	xhci->dcbaa = NULL;

	scratchpad_free(xhci);

	spin_lock_irqsave(&xhci->lock, flags);
	list_for_each_entry_safe(dev_info, next, &xhci->lpm_failed_devs, list) {
		list_del(&dev_info->list);
		kfree(dev_info);
	}
	spin_unlock_irqrestore(&xhci->lock, flags);

	if (!xhci->rh_bw)
		goto no_bw;

	num_ports = HCS_MAX_PORTS(xhci->hcs_params1);
	for (i = 0; i < num_ports; i++) {
		struct xhci_interval_bw_table *bwt = &xhci->rh_bw[i].bw_table;
		for (j = 0; j < XHCI_MAX_INTERVAL; j++) {
			struct list_head *ep = &bwt->interval_bw[j].endpoints;
			while (!list_empty(ep))
				list_del_init(ep->next);
		}
	}

	for (i = 0; i < num_ports; i++) {
		struct xhci_tt_bw_info *tt, *n;
		list_for_each_entry_safe(tt, n, &xhci->rh_bw[i].tts, tt_list) {
			list_del(&tt->tt_list);
			kfree(tt);
		}
	}

no_bw:
	xhci->num_usb2_ports = 0;
	xhci->num_usb3_ports = 0;
	xhci->num_active_eps = 0;
	kfree(xhci->usb2_ports);
	kfree(xhci->usb3_ports);
	kfree(xhci->port_array);
	kfree(xhci->rh_bw);

	xhci->page_size = 0;
	xhci->page_shift = 0;
	xhci->bus_state[0].bus_suspended = 0;
	xhci->bus_state[1].bus_suspended = 0;
}

static int xhci_test_trb_in_td(struct xhci_hcd *xhci,
		struct xhci_segment *input_seg,
		union xhci_trb *start_trb,
		union xhci_trb *end_trb,
		dma_addr_t input_dma,
		struct xhci_segment *result_seg,
		char *test_name, int test_number)
{
	unsigned long long start_dma;
	unsigned long long end_dma;
	struct xhci_segment *seg;

	start_dma = xhci_trb_virt_to_dma(input_seg, start_trb);
	end_dma = xhci_trb_virt_to_dma(input_seg, end_trb);

	seg = trb_in_td(input_seg, start_trb, end_trb, input_dma);
	if (seg != result_seg) {
		xhci_warn(xhci, "WARN: %s TRB math test %d failed!\n",
				test_name, test_number);
		xhci_warn(xhci, "Tested TRB math w/ seg %p and "
				"input DMA 0x%llx\n",
				input_seg,
				(unsigned long long) input_dma);
		xhci_warn(xhci, "starting TRB %p (0x%llx DMA), "
				"ending TRB %p (0x%llx DMA)\n",
				start_trb, start_dma,
				end_trb, end_dma);
		xhci_warn(xhci, "Expected seg %p, got seg %p\n",
				result_seg, seg);
		return -1;
	}
	return 0;
}

/* TRB math checks for xhci_trb_in_td(), using the command and event rings. */
static int xhci_check_trb_in_td_math(struct xhci_hcd *xhci, gfp_t mem_flags)
{
	struct {
		dma_addr_t		input_dma;
		struct xhci_segment	*result_seg;
	} simple_test_vector [] = {
		/* A zeroed DMA field should fail */
		{ 0, NULL },
		/* One TRB before the ring start should fail */
		{ xhci->event_ring->first_seg->dma - 16, NULL },
		/* One byte before the ring start should fail */
		{ xhci->event_ring->first_seg->dma - 1, NULL },
		/* Starting TRB should succeed */
		{ xhci->event_ring->first_seg->dma, xhci->event_ring->first_seg },
		/* Ending TRB should succeed */
		{ xhci->event_ring->first_seg->dma + (TRBS_PER_SEGMENT - 1)*16,
			xhci->event_ring->first_seg },
		/* One byte after the ring end should fail */
		{ xhci->event_ring->first_seg->dma + (TRBS_PER_SEGMENT - 1)*16 + 1, NULL },
		/* One TRB after the ring end should fail */
		{ xhci->event_ring->first_seg->dma + (TRBS_PER_SEGMENT)*16, NULL },
		/* An address of all ones should fail */
		{ (dma_addr_t) (~0), NULL },
	};
	struct {
		struct xhci_segment	*input_seg;
		union xhci_trb		*start_trb;
		union xhci_trb		*end_trb;
		dma_addr_t		input_dma;
		struct xhci_segment	*result_seg;
	} complex_test_vector [] = {
		/* Test feeding a valid DMA address from a different ring */
		{	.input_seg = xhci->event_ring->first_seg,
			.start_trb = xhci->event_ring->first_seg->trbs,
			.end_trb = &xhci->event_ring->first_seg->trbs[TRBS_PER_SEGMENT - 1],
			.input_dma = xhci->cmd_ring->first_seg->dma,
			.result_seg = NULL,
		},
		/* Test feeding a valid end TRB from a different ring */
		{	.input_seg = xhci->event_ring->first_seg,
			.start_trb = xhci->event_ring->first_seg->trbs,
			.end_trb = &xhci->cmd_ring->first_seg->trbs[TRBS_PER_SEGMENT - 1],
			.input_dma = xhci->cmd_ring->first_seg->dma,
			.result_seg = NULL,
		},
		/* Test feeding a valid start and end TRB from a different ring */
		{	.input_seg = xhci->event_ring->first_seg,
			.start_trb = xhci->cmd_ring->first_seg->trbs,
			.end_trb = &xhci->cmd_ring->first_seg->trbs[TRBS_PER_SEGMENT - 1],
			.input_dma = xhci->cmd_ring->first_seg->dma,
			.result_seg = NULL,
		},
		/* TRB in this ring, but after this TD */
		{	.input_seg = xhci->event_ring->first_seg,
			.start_trb = &xhci->event_ring->first_seg->trbs[0],
			.end_trb = &xhci->event_ring->first_seg->trbs[3],
			.input_dma = xhci->event_ring->first_seg->dma + 4*16,
			.result_seg = NULL,
		},
		/* TRB in this ring, but before this TD */
		{	.input_seg = xhci->event_ring->first_seg,
			.start_trb = &xhci->event_ring->first_seg->trbs[3],
			.end_trb = &xhci->event_ring->first_seg->trbs[6],
			.input_dma = xhci->event_ring->first_seg->dma + 2*16,
			.result_seg = NULL,
		},
		/* TRB in this ring, but after this wrapped TD */
		{	.input_seg = xhci->event_ring->first_seg,
			.start_trb = &xhci->event_ring->first_seg->trbs[TRBS_PER_SEGMENT - 3],
			.end_trb = &xhci->event_ring->first_seg->trbs[1],
			.input_dma = xhci->event_ring->first_seg->dma + 2*16,
			.result_seg = NULL,
		},
		/* TRB in this ring, but before this wrapped TD */
		{	.input_seg = xhci->event_ring->first_seg,
			.start_trb = &xhci->event_ring->first_seg->trbs[TRBS_PER_SEGMENT - 3],
			.end_trb = &xhci->event_ring->first_seg->trbs[1],
			.input_dma = xhci->event_ring->first_seg->dma + (TRBS_PER_SEGMENT - 4)*16,
			.result_seg = NULL,
		},
		/* TRB not in this ring, and we have a wrapped TD */
		{	.input_seg = xhci->event_ring->first_seg,
			.start_trb = &xhci->event_ring->first_seg->trbs[TRBS_PER_SEGMENT - 3],
			.end_trb = &xhci->event_ring->first_seg->trbs[1],
			.input_dma = xhci->cmd_ring->first_seg->dma + 2*16,
			.result_seg = NULL,
		},
	};

	unsigned int num_tests;
	int i, ret;

	num_tests = ARRAY_SIZE(simple_test_vector);
	for (i = 0; i < num_tests; i++) {
		ret = xhci_test_trb_in_td(xhci,
				xhci->event_ring->first_seg,
				xhci->event_ring->first_seg->trbs,
				&xhci->event_ring->first_seg->trbs[TRBS_PER_SEGMENT - 1],
				simple_test_vector[i].input_dma,
				simple_test_vector[i].result_seg,
				"Simple", i);
		if (ret < 0)
			return ret;
	}

	num_tests = ARRAY_SIZE(complex_test_vector);
	for (i = 0; i < num_tests; i++) {
		ret = xhci_test_trb_in_td(xhci,
				complex_test_vector[i].input_seg,
				complex_test_vector[i].start_trb,
				complex_test_vector[i].end_trb,
				complex_test_vector[i].input_dma,
				complex_test_vector[i].result_seg,
				"Complex", i);
		if (ret < 0)
			return ret;
	}
	xhci_dbg(xhci, "TRB math tests passed.\n");
	return 0;
}

static void xhci_set_hc_event_deq(struct xhci_hcd *xhci)
{
	u64 temp;
	dma_addr_t deq;

	deq = xhci_trb_virt_to_dma(xhci->event_ring->deq_seg,
			xhci->event_ring->dequeue);
	if (deq == 0 && !in_interrupt())
		xhci_warn(xhci, "WARN something wrong with SW event ring "
				"dequeue ptr.\n");
	/* Update HC event ring dequeue pointer */
	temp = xhci_read_64(xhci, &xhci->ir_set->erst_dequeue);
	temp &= ERST_PTR_MASK;
	/* Don't clear the EHB bit (which is RW1C) because
	 * there might be more events to service.
	 */
	temp &= ~ERST_EHB;
	xhci_dbg(xhci, "// Write event ring dequeue pointer, "
			"preserving EHB bit\n");
	xhci_write_64(xhci, ((u64) deq & (u64) ~ERST_PTR_MASK) | temp,
			&xhci->ir_set->erst_dequeue);
}

static void xhci_add_in_port(struct xhci_hcd *xhci, unsigned int num_ports,
		__le32 __iomem *addr, u8 major_revision)
{
	u32 temp, port_offset, port_count;
	int i;

	if (major_revision > 0x03) {
		xhci_warn(xhci, "Ignoring unknown port speed, "
				"Ext Cap %p, revision = 0x%x\n",
				addr, major_revision);
		/* Ignoring port protocol we can't understand. FIXME */
		return;
	}

	/* Port offset and count in the third dword, see section 7.2 */
	temp = xhci_readl(xhci, addr + 2);
	port_offset = XHCI_EXT_PORT_OFF(temp);
	port_count = XHCI_EXT_PORT_COUNT(temp);
	xhci_dbg(xhci, "Ext Cap %p, port offset = %u, "
			"count = %u, revision = 0x%x\n",
			addr, port_offset, port_count, major_revision);
	/* Port count includes the current port offset */
	if (port_offset == 0 || (port_offset + port_count - 1) > num_ports)
		/* WTF? "Valid values are ‘1’ to MaxPorts" */
		return;

	/* Check the host's USB2 LPM capability */
	if ((xhci->hci_version == 0x96) && (major_revision != 0x03) &&
			(temp & XHCI_L1C)) {
		xhci_dbg(xhci, "xHCI 0.96: support USB2 software lpm\n");
		xhci->sw_lpm_support = 1;
	}

	if ((xhci->hci_version >= 0x100) && (major_revision != 0x03)) {
		xhci_dbg(xhci, "xHCI 1.0: support USB2 software lpm\n");
		xhci->sw_lpm_support = 1;
		if (temp & XHCI_HLC) {
			xhci_dbg(xhci, "xHCI 1.0: support USB2 hardware lpm\n");
			xhci->hw_lpm_support = 1;
		}
	}

	port_offset--;
	for (i = port_offset; i < (port_offset + port_count); i++) {
		/* Duplicate entry.  Ignore the port if the revisions differ. */
		if (xhci->port_array[i] != 0) {
			xhci_warn(xhci, "Duplicate port entry, Ext Cap %p,"
					" port %u\n", addr, i);
			xhci_warn(xhci, "Port was marked as USB %u, "
					"duplicated as USB %u\n",
					xhci->port_array[i], major_revision);
			/* Only adjust the roothub port counts if we haven't
			 * found a similar duplicate.
			 */
			if (xhci->port_array[i] != major_revision &&
				xhci->port_array[i] != DUPLICATE_ENTRY) {
				if (xhci->port_array[i] == 0x03)
					xhci->num_usb3_ports--;
				else
					xhci->num_usb2_ports--;
				xhci->port_array[i] = DUPLICATE_ENTRY;
			}
			/* FIXME: Should we disable the port? */
			continue;
		}
		xhci->port_array[i] = major_revision;
		if (major_revision == 0x03)
			xhci->num_usb3_ports++;
		else
			xhci->num_usb2_ports++;
	}
	/* FIXME: Should we disable ports not in the Extended Capabilities? */
}

/*
 * Scan the Extended Capabilities for the "Supported Protocol Capabilities" that
 * specify what speeds each port is supposed to be.  We can't count on the port
 * speed bits in the PORTSC register being correct until a device is connected,
 * but we need to set up the two fake roothubs with the correct number of USB
 * 3.0 and USB 2.0 ports at host controller initialization time.
 */
static int xhci_setup_port_arrays(struct xhci_hcd *xhci, gfp_t flags)
{
	__le32 __iomem *addr;
	u32 offset;
	unsigned int num_ports;
	int i, j, port_index;

	addr = &xhci->cap_regs->hcc_params;
	offset = XHCI_HCC_EXT_CAPS(xhci_readl(xhci, addr));
	if (offset == 0) {
		xhci_err(xhci, "No Extended Capability registers, "
				"unable to set up roothub.\n");
		return -ENODEV;
	}

	num_ports = HCS_MAX_PORTS(xhci->hcs_params1);
	xhci->port_array = kzalloc(sizeof(*xhci->port_array)*num_ports, flags);
	if (!xhci->port_array)
		return -ENOMEM;

	xhci->rh_bw = kzalloc(sizeof(*xhci->rh_bw)*num_ports, flags);
	if (!xhci->rh_bw)
		return -ENOMEM;
	for (i = 0; i < num_ports; i++) {
		struct xhci_interval_bw_table *bw_table;

		INIT_LIST_HEAD(&xhci->rh_bw[i].tts);
		bw_table = &xhci->rh_bw[i].bw_table;
		for (j = 0; j < XHCI_MAX_INTERVAL; j++)
			INIT_LIST_HEAD(&bw_table->interval_bw[j].endpoints);
	}

	/*
	 * For whatever reason, the first capability offset is from the
	 * capability register base, not from the HCCPARAMS register.
	 * See section 5.3.6 for offset calculation.
	 */
	addr = &xhci->cap_regs->hc_capbase + offset;
	while (1) {
		u32 cap_id;

		cap_id = xhci_readl(xhci, addr);
		if (XHCI_EXT_CAPS_ID(cap_id) == XHCI_EXT_CAPS_PROTOCOL)
			xhci_add_in_port(xhci, num_ports, addr,
					(u8) XHCI_EXT_PORT_MAJOR(cap_id));
		offset = XHCI_EXT_CAPS_NEXT(cap_id);
		if (!offset || (xhci->num_usb2_ports + xhci->num_usb3_ports)
				== num_ports)
			break;
		/*
		 * Once you're into the Extended Capabilities, the offset is
		 * always relative to the register holding the offset.
		 */
		addr += offset;
	}

	if (xhci->num_usb2_ports == 0 && xhci->num_usb3_ports == 0) {
		xhci_warn(xhci, "No ports on the roothubs?\n");
		return -ENODEV;
	}
	xhci_dbg(xhci, "Found %u USB 2.0 ports and %u USB 3.0 ports.\n",
			xhci->num_usb2_ports, xhci->num_usb3_ports);

	/* Place limits on the number of roothub ports so that the hub
	 * descriptors aren't longer than the USB core will allocate.
	 */
	if (xhci->num_usb3_ports > 15) {
		xhci_dbg(xhci, "Limiting USB 3.0 roothub ports to 15.\n");
		xhci->num_usb3_ports = 15;
	}
	if (xhci->num_usb2_ports > USB_MAXCHILDREN) {
		xhci_dbg(xhci, "Limiting USB 2.0 roothub ports to %u.\n",
				USB_MAXCHILDREN);
		xhci->num_usb2_ports = USB_MAXCHILDREN;
	}

	/*
	 * Note we could have all USB 3.0 ports, or all USB 2.0 ports.
	 * Not sure how the USB core will handle a hub with no ports...
	 */
	if (xhci->num_usb2_ports) {
		xhci->usb2_ports = kmalloc(sizeof(*xhci->usb2_ports)*
				xhci->num_usb2_ports, flags);
		if (!xhci->usb2_ports)
			return -ENOMEM;

		port_index = 0;
		for (i = 0; i < num_ports; i++) {
			if (xhci->port_array[i] == 0x03 ||
					xhci->port_array[i] == 0 ||
					xhci->port_array[i] == DUPLICATE_ENTRY)
				continue;

			xhci->usb2_ports[port_index] =
				&xhci->op_regs->port_status_base +
				NUM_PORT_REGS*i;
			xhci_dbg(xhci, "USB 2.0 port at index %u, "
					"addr = %p\n", i,
					xhci->usb2_ports[port_index]);
			port_index++;
			if (port_index == xhci->num_usb2_ports)
				break;
		}
	}
	if (xhci->num_usb3_ports) {
		xhci->usb3_ports = kmalloc(sizeof(*xhci->usb3_ports)*
				xhci->num_usb3_ports, flags);
		if (!xhci->usb3_ports)
			return -ENOMEM;

		port_index = 0;
		for (i = 0; i < num_ports; i++)
			if (xhci->port_array[i] == 0x03) {
				xhci->usb3_ports[port_index] =
					&xhci->op_regs->port_status_base +
					NUM_PORT_REGS*i;
				xhci_dbg(xhci, "USB 3.0 port at index %u, "
						"addr = %p\n", i,
						xhci->usb3_ports[port_index]);
				port_index++;
				if (port_index == xhci->num_usb3_ports)
					break;
			}
	}
	return 0;
}

int xhci_mem_init(struct xhci_hcd *xhci, gfp_t flags)
{
	dma_addr_t	dma;
	struct device	*dev = xhci_to_hcd(xhci)->self.controller;
	unsigned int	val, val2;
	u64		val_64;
	struct xhci_segment	*seg;
	u32 page_size, temp;
	int i;

	INIT_LIST_HEAD(&xhci->lpm_failed_devs);
	INIT_LIST_HEAD(&xhci->cancel_cmd_list);

	page_size = xhci_readl(xhci, &xhci->op_regs->page_size);
	xhci_dbg(xhci, "Supported page size register = 0x%x\n", page_size);
	for (i = 0; i < 16; i++) {
		if ((0x1 & page_size) != 0)
			break;
		page_size = page_size >> 1;
	}
	if (i < 16)
		xhci_dbg(xhci, "Supported page size of %iK\n", (1 << (i+12)) / 1024);
	else
		xhci_warn(xhci, "WARN: no supported page size\n");
	/* Use 4K pages, since that's common and the minimum the HC supports */
	xhci->page_shift = 12;
	xhci->page_size = 1 << xhci->page_shift;
	xhci_dbg(xhci, "HCD page size set to %iK\n", xhci->page_size / 1024);

	/*
	 * Program the Number of Device Slots Enabled field in the CONFIG
	 * register with the max value of slots the HC can handle.
	 */
	val = HCS_MAX_SLOTS(xhci_readl(xhci, &xhci->cap_regs->hcs_params1));
	xhci_dbg(xhci, "// xHC can handle at most %d device slots.\n",
			(unsigned int) val);
	val2 = xhci_readl(xhci, &xhci->op_regs->config_reg);
	val |= (val2 & ~HCS_SLOTS_MASK);
	xhci_dbg(xhci, "// Setting Max device slots reg = 0x%x.\n",
			(unsigned int) val);
	xhci_writel(xhci, val, &xhci->op_regs->config_reg);

	/*
	 * Section 5.4.8 - doorbell array must be
	 * "physically contiguous and 64-byte (cache line) aligned".
	 */
	xhci->dcbaa = dma_alloc_coherent(dev, sizeof(*xhci->dcbaa), &dma,
			GFP_KERNEL);
	if (!xhci->dcbaa)
		goto fail;
	memset(xhci->dcbaa, 0, sizeof *(xhci->dcbaa));
	xhci->dcbaa->dma = dma;
	xhci_dbg(xhci, "// Device context base array address = 0x%llx (DMA), %p (virt)\n",
			(unsigned long long)xhci->dcbaa->dma, xhci->dcbaa);
	xhci_write_64(xhci, dma, &xhci->op_regs->dcbaa_ptr);

	/*
	 * Initialize the ring segment pool.  The ring must be a contiguous
	 * structure comprised of TRBs.  The TRBs must be 16 byte aligned,
	 * however, the command ring segment needs 64-byte aligned segments,
	 * so we pick the greater alignment need.
	 */
	xhci->segment_pool = dma_pool_create("xHCI ring segments", dev,
			TRB_SEGMENT_SIZE, 64, xhci->page_size);

	/* See Table 46 and Note on Figure 55 */
	xhci->device_pool = dma_pool_create("xHCI input/output contexts", dev,
			2112, 64, xhci->page_size);
	if (!xhci->segment_pool || !xhci->device_pool)
		goto fail;

	/* Linear stream context arrays don't have any boundary restrictions,
	 * and only need to be 16-byte aligned.
	 */
	xhci->small_streams_pool =
		dma_pool_create("xHCI 256 byte stream ctx arrays",
			dev, SMALL_STREAM_ARRAY_SIZE, 16, 0);
	xhci->medium_streams_pool =
		dma_pool_create("xHCI 1KB stream ctx arrays",
			dev, MEDIUM_STREAM_ARRAY_SIZE, 16, 0);
	/* Any stream context array bigger than MEDIUM_STREAM_ARRAY_SIZE
	 * will be allocated with dma_alloc_coherent()
	 */

	if (!xhci->small_streams_pool || !xhci->medium_streams_pool)
		goto fail;

	/* Set up the command ring to have one segments for now. */
	xhci->cmd_ring = xhci_ring_alloc(xhci, 1, 1, TYPE_COMMAND, flags);
	if (!xhci->cmd_ring)
		goto fail;
	xhci_dbg(xhci, "Allocated command ring at %p\n", xhci->cmd_ring);
	xhci_dbg(xhci, "First segment DMA is 0x%llx\n",
			(unsigned long long)xhci->cmd_ring->first_seg->dma);

	/* Set the address in the Command Ring Control register */
	val_64 = xhci_read_64(xhci, &xhci->op_regs->cmd_ring);
	val_64 = (val_64 & (u64) CMD_RING_RSVD_BITS) |
		(xhci->cmd_ring->first_seg->dma & (u64) ~CMD_RING_RSVD_BITS) |
		xhci->cmd_ring->cycle_state;
	xhci_dbg(xhci, "// Setting command ring address to 0x%x\n", val);
	xhci_write_64(xhci, val_64, &xhci->op_regs->cmd_ring);
	xhci_dbg_cmd_ptrs(xhci);

	xhci->lpm_command = xhci_alloc_command(xhci, true, true, flags);
	if (!xhci->lpm_command)
		goto fail;

	/* Reserve one command ring TRB for disabling LPM.
	 * Since the USB core grabs the shared usb_bus bandwidth mutex before
	 * disabling LPM, we only need to reserve one TRB for all devices.
	 */
	xhci->cmd_ring_reserved_trbs++;

	val = xhci_readl(xhci, &xhci->cap_regs->db_off);
	val &= DBOFF_MASK;
	xhci_dbg(xhci, "// Doorbell array is located at offset 0x%x"
			" from cap regs base addr\n", val);
	xhci->dba = (void __iomem *) xhci->cap_regs + val;
	xhci_dbg_regs(xhci);
	xhci_print_run_regs(xhci);
	/* Set ir_set to interrupt register set 0 */
	xhci->ir_set = &xhci->run_regs->ir_set[0];

	/*
	 * Event ring setup: Allocate a normal ring, but also setup
	 * the event ring segment table (ERST).  Section 4.9.3.
	 */
	xhci_dbg(xhci, "// Allocating event ring\n");
	xhci->event_ring = xhci_ring_alloc(xhci, ERST_NUM_SEGS, 1, TYPE_EVENT,
						flags);
	if (!xhci->event_ring)
		goto fail;
	if (xhci_check_trb_in_td_math(xhci, flags) < 0)
		goto fail;

	xhci->erst.entries = dma_alloc_coherent(dev,
			sizeof(struct xhci_erst_entry) * ERST_NUM_SEGS, &dma,
			GFP_KERNEL);
	if (!xhci->erst.entries)
		goto fail;
	xhci_dbg(xhci, "// Allocated event ring segment table at 0x%llx\n",
			(unsigned long long)dma);

	memset(xhci->erst.entries, 0, sizeof(struct xhci_erst_entry)*ERST_NUM_SEGS);
	xhci->erst.num_entries = ERST_NUM_SEGS;
	xhci->erst.erst_dma_addr = dma;
	xhci_dbg(xhci, "Set ERST to 0; private num segs = %i, virt addr = %p, dma addr = 0x%llx\n",
			xhci->erst.num_entries,
			xhci->erst.entries,
			(unsigned long long)xhci->erst.erst_dma_addr);

	/* set ring base address and size for each segment table entry */
	for (val = 0, seg = xhci->event_ring->first_seg; val < ERST_NUM_SEGS; val++) {
		struct xhci_erst_entry *entry = &xhci->erst.entries[val];
		entry->seg_addr = cpu_to_le64(seg->dma);
		entry->seg_size = cpu_to_le32(TRBS_PER_SEGMENT);
		entry->rsvd = 0;
		seg = seg->next;
	}

	/* set ERST count with the number of entries in the segment table */
	val = xhci_readl(xhci, &xhci->ir_set->erst_size);
	val &= ERST_SIZE_MASK;
	val |= ERST_NUM_SEGS;
	xhci_dbg(xhci, "// Write ERST size = %i to ir_set 0 (some bits preserved)\n",
			val);
	xhci_writel(xhci, val, &xhci->ir_set->erst_size);

	xhci_dbg(xhci, "// Set ERST entries to point to event ring.\n");
	/* set the segment table base address */
	xhci_dbg(xhci, "// Set ERST base address for ir_set 0 = 0x%llx\n",
			(unsigned long long)xhci->erst.erst_dma_addr);
	val_64 = xhci_read_64(xhci, &xhci->ir_set->erst_base);
	val_64 &= ERST_PTR_MASK;
	val_64 |= (xhci->erst.erst_dma_addr & (u64) ~ERST_PTR_MASK);
	xhci_write_64(xhci, val_64, &xhci->ir_set->erst_base);

	/* Set the event ring dequeue address */
	xhci_set_hc_event_deq(xhci);
	xhci_dbg(xhci, "Wrote ERST address to ir_set 0.\n");
	xhci_print_ir_set(xhci, 0);

	/*
	 * XXX: Might need to set the Interrupter Moderation Register to
	 * something other than the default (~1ms minimum between interrupts).
	 * See section 5.5.1.2.
	 */
	init_completion(&xhci->addr_dev);
	for (i = 0; i < MAX_HC_SLOTS; ++i)
		xhci->devs[i] = NULL;
	for (i = 0; i < USB_MAXCHILDREN; ++i) {
		xhci->bus_state[0].resume_done[i] = 0;
		xhci->bus_state[1].resume_done[i] = 0;
	}

	if (scratchpad_alloc(xhci, flags))
		goto fail;
	if (xhci_setup_port_arrays(xhci, flags))
		goto fail;

	/* Enable USB 3.0 device notifications for function remote wake, which
	 * is necessary for allowing USB 3.0 devices to do remote wakeup from
	 * U3 (device suspend).
	 */
	temp = xhci_readl(xhci, &xhci->op_regs->dev_notification);
	temp &= ~DEV_NOTE_MASK;
	temp |= DEV_NOTE_FWAKE;
	xhci_writel(xhci, temp, &xhci->op_regs->dev_notification);

	return 0;

fail:
	xhci_warn(xhci, "Couldn't initialize memory\n");
	xhci_halt(xhci);
	xhci_reset(xhci);
	xhci_mem_cleanup(xhci);
	return -ENOMEM;
}
