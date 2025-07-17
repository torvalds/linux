/*
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include <linux/slab.h>
#include <drm/drm_print.h>

#include "amdgpu_ring_mux.h"
#include "amdgpu_ring.h"
#include "amdgpu.h"

#define AMDGPU_MUX_RESUBMIT_JIFFIES_TIMEOUT (HZ / 2)
#define AMDGPU_MAX_LAST_UNSIGNALED_THRESHOLD_US 10000

static const struct ring_info {
	unsigned int hw_pio;
	const char *ring_name;
} sw_ring_info[] = {
	{ AMDGPU_RING_PRIO_DEFAULT, "gfx_low"},
	{ AMDGPU_RING_PRIO_2, "gfx_high"},
};

static struct kmem_cache *amdgpu_mux_chunk_slab;

static inline struct amdgpu_mux_entry *amdgpu_ring_mux_sw_entry(struct amdgpu_ring_mux *mux,
								struct amdgpu_ring *ring)
{
	return ring->entry_index < mux->ring_entry_size ?
			&mux->ring_entry[ring->entry_index] : NULL;
}

/* copy packages on sw ring range[begin, end) */
static void amdgpu_ring_mux_copy_pkt_from_sw_ring(struct amdgpu_ring_mux *mux,
						  struct amdgpu_ring *ring,
						  u64 s_start, u64 s_end)
{
	u64 start, end;
	struct amdgpu_ring *real_ring = mux->real_ring;

	start = s_start & ring->buf_mask;
	end = s_end & ring->buf_mask;

	if (start == end) {
		DRM_ERROR("no more data copied from sw ring\n");
		return;
	}
	if (start > end) {
		amdgpu_ring_alloc(real_ring, (ring->ring_size >> 2) + end - start);
		amdgpu_ring_write_multiple(real_ring, (void *)&ring->ring[start],
					   (ring->ring_size >> 2) - start);
		amdgpu_ring_write_multiple(real_ring, (void *)&ring->ring[0], end);
	} else {
		amdgpu_ring_alloc(real_ring, end - start);
		amdgpu_ring_write_multiple(real_ring, (void *)&ring->ring[start], end - start);
	}
}

static void amdgpu_mux_resubmit_chunks(struct amdgpu_ring_mux *mux)
{
	struct amdgpu_mux_entry *e = NULL;
	struct amdgpu_mux_chunk *chunk;
	uint32_t seq, last_seq;
	int i;

	/*find low priority entries:*/
	if (!mux->s_resubmit)
		return;

	for (i = 0; i < mux->num_ring_entries; i++) {
		if (mux->ring_entry[i].ring->hw_prio <= AMDGPU_RING_PRIO_DEFAULT) {
			e = &mux->ring_entry[i];
			break;
		}
	}

	if (!e) {
		DRM_ERROR("%s no low priority ring found\n", __func__);
		return;
	}

	last_seq = atomic_read(&e->ring->fence_drv.last_seq);
	seq = mux->seqno_to_resubmit;
	if (last_seq < seq) {
		/*resubmit all the fences between (last_seq, seq]*/
		list_for_each_entry(chunk, &e->list, entry) {
			if (chunk->sync_seq > last_seq && chunk->sync_seq <= seq) {
				amdgpu_fence_update_start_timestamp(e->ring,
								    chunk->sync_seq,
								    ktime_get());
				if (chunk->sync_seq ==
					le32_to_cpu(*(e->ring->fence_drv.cpu_addr + 2))) {
					if (chunk->cntl_offset <= e->ring->buf_mask)
						amdgpu_ring_patch_cntl(e->ring,
								       chunk->cntl_offset);
					if (chunk->ce_offset <= e->ring->buf_mask)
						amdgpu_ring_patch_ce(e->ring, chunk->ce_offset);
					if (chunk->de_offset <= e->ring->buf_mask)
						amdgpu_ring_patch_de(e->ring, chunk->de_offset);
				}
				amdgpu_ring_mux_copy_pkt_from_sw_ring(mux, e->ring,
								      chunk->start,
								      chunk->end);
				mux->wptr_resubmit = chunk->end;
				amdgpu_ring_commit(mux->real_ring);
			}
		}
	}

	timer_delete(&mux->resubmit_timer);
	mux->s_resubmit = false;
}

static void amdgpu_ring_mux_schedule_resubmit(struct amdgpu_ring_mux *mux)
{
	mod_timer(&mux->resubmit_timer, jiffies + AMDGPU_MUX_RESUBMIT_JIFFIES_TIMEOUT);
}

static void amdgpu_mux_resubmit_fallback(struct timer_list *t)
{
	struct amdgpu_ring_mux *mux = timer_container_of(mux, t,
							 resubmit_timer);

	if (!spin_trylock(&mux->lock)) {
		amdgpu_ring_mux_schedule_resubmit(mux);
		DRM_ERROR("reschedule resubmit\n");
		return;
	}
	amdgpu_mux_resubmit_chunks(mux);
	spin_unlock(&mux->lock);
}

int amdgpu_ring_mux_init(struct amdgpu_ring_mux *mux, struct amdgpu_ring *ring,
			 unsigned int entry_size)
{
	mux->real_ring = ring;
	mux->num_ring_entries = 0;

	mux->ring_entry = kcalloc(entry_size, sizeof(struct amdgpu_mux_entry), GFP_KERNEL);
	if (!mux->ring_entry)
		return -ENOMEM;

	mux->ring_entry_size = entry_size;
	mux->s_resubmit = false;

	amdgpu_mux_chunk_slab = KMEM_CACHE(amdgpu_mux_chunk, SLAB_HWCACHE_ALIGN);
	if (!amdgpu_mux_chunk_slab) {
		DRM_ERROR("create amdgpu_mux_chunk cache failed\n");
		return -ENOMEM;
	}

	spin_lock_init(&mux->lock);
	timer_setup(&mux->resubmit_timer, amdgpu_mux_resubmit_fallback, 0);

	return 0;
}

void amdgpu_ring_mux_fini(struct amdgpu_ring_mux *mux)
{
	struct amdgpu_mux_entry *e;
	struct amdgpu_mux_chunk *chunk, *chunk2;
	int i;

	for (i = 0; i < mux->num_ring_entries; i++) {
		e = &mux->ring_entry[i];
		list_for_each_entry_safe(chunk, chunk2, &e->list, entry) {
			list_del(&chunk->entry);
			kmem_cache_free(amdgpu_mux_chunk_slab, chunk);
		}
	}
	kmem_cache_destroy(amdgpu_mux_chunk_slab);
	kfree(mux->ring_entry);
	mux->ring_entry = NULL;
	mux->num_ring_entries = 0;
	mux->ring_entry_size = 0;
}

int amdgpu_ring_mux_add_sw_ring(struct amdgpu_ring_mux *mux, struct amdgpu_ring *ring)
{
	struct amdgpu_mux_entry *e;

	if (mux->num_ring_entries >= mux->ring_entry_size) {
		DRM_ERROR("add sw ring exceeding max entry size\n");
		return -ENOENT;
	}

	e = &mux->ring_entry[mux->num_ring_entries];
	ring->entry_index = mux->num_ring_entries;
	e->ring = ring;

	INIT_LIST_HEAD(&e->list);
	mux->num_ring_entries += 1;
	return 0;
}

void amdgpu_ring_mux_set_wptr(struct amdgpu_ring_mux *mux, struct amdgpu_ring *ring, u64 wptr)
{
	struct amdgpu_mux_entry *e;

	spin_lock(&mux->lock);

	if (ring->hw_prio <= AMDGPU_RING_PRIO_DEFAULT)
		amdgpu_mux_resubmit_chunks(mux);

	e = amdgpu_ring_mux_sw_entry(mux, ring);
	if (!e) {
		DRM_ERROR("cannot find entry for sw ring\n");
		spin_unlock(&mux->lock);
		return;
	}

	/* We could skip this set wptr as preemption in process. */
	if (ring->hw_prio <= AMDGPU_RING_PRIO_DEFAULT && mux->pending_trailing_fence_signaled) {
		spin_unlock(&mux->lock);
		return;
	}

	e->sw_cptr = e->sw_wptr;
	/* Update cptr if the package already copied in resubmit functions */
	if (ring->hw_prio <= AMDGPU_RING_PRIO_DEFAULT && e->sw_cptr < mux->wptr_resubmit)
		e->sw_cptr = mux->wptr_resubmit;
	e->sw_wptr = wptr;
	e->start_ptr_in_hw_ring = mux->real_ring->wptr;

	/* Skip copying for the packages already resubmitted.*/
	if (ring->hw_prio > AMDGPU_RING_PRIO_DEFAULT || mux->wptr_resubmit < wptr) {
		amdgpu_ring_mux_copy_pkt_from_sw_ring(mux, ring, e->sw_cptr, wptr);
		e->end_ptr_in_hw_ring = mux->real_ring->wptr;
		amdgpu_ring_commit(mux->real_ring);
	} else {
		e->end_ptr_in_hw_ring = mux->real_ring->wptr;
	}
	spin_unlock(&mux->lock);
}

u64 amdgpu_ring_mux_get_wptr(struct amdgpu_ring_mux *mux, struct amdgpu_ring *ring)
{
	struct amdgpu_mux_entry *e;

	e = amdgpu_ring_mux_sw_entry(mux, ring);
	if (!e) {
		DRM_ERROR("cannot find entry for sw ring\n");
		return 0;
	}

	return e->sw_wptr;
}

/**
 * amdgpu_ring_mux_get_rptr - get the readptr of the software ring
 * @mux: the multiplexer the software rings attach to
 * @ring: the software ring of which we calculate the readptr
 *
 * The return value of the readptr is not precise while the other rings could
 * write data onto the real ring buffer.After overwriting on the real ring, we
 * can not decide if our packages have been excuted or not read yet. However,
 * this function is only called by the tools such as umr to collect the latest
 * packages for the hang analysis. We assume the hang happens near our latest
 * submit. Thus we could use the following logic to give the clue:
 * If the readptr is between start and end, then we return the copy pointer
 * plus the distance from start to readptr. If the readptr is before start, we
 * return the copy pointer. Lastly, if the readptr is past end, we return the
 * write pointer.
 */
u64 amdgpu_ring_mux_get_rptr(struct amdgpu_ring_mux *mux, struct amdgpu_ring *ring)
{
	struct amdgpu_mux_entry *e;
	u64 readp, offset, start, end;

	e = amdgpu_ring_mux_sw_entry(mux, ring);
	if (!e) {
		DRM_ERROR("no sw entry found!\n");
		return 0;
	}

	readp = amdgpu_ring_get_rptr(mux->real_ring);

	start = e->start_ptr_in_hw_ring & mux->real_ring->buf_mask;
	end = e->end_ptr_in_hw_ring & mux->real_ring->buf_mask;
	if (start > end) {
		if (readp <= end)
			readp += mux->real_ring->ring_size >> 2;
		end += mux->real_ring->ring_size >> 2;
	}

	if (start <= readp && readp <= end) {
		offset = readp - start;
		e->sw_rptr = (e->sw_cptr + offset) & ring->buf_mask;
	} else if (readp < start) {
		e->sw_rptr = e->sw_cptr;
	} else {
		/* end < readptr */
		e->sw_rptr = e->sw_wptr;
	}

	return e->sw_rptr;
}

u64 amdgpu_sw_ring_get_rptr_gfx(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ring_mux *mux = &adev->gfx.muxer;

	WARN_ON(!ring->is_sw_ring);
	return amdgpu_ring_mux_get_rptr(mux, ring);
}

u64 amdgpu_sw_ring_get_wptr_gfx(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ring_mux *mux = &adev->gfx.muxer;

	WARN_ON(!ring->is_sw_ring);
	return amdgpu_ring_mux_get_wptr(mux, ring);
}

void amdgpu_sw_ring_set_wptr_gfx(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ring_mux *mux = &adev->gfx.muxer;

	WARN_ON(!ring->is_sw_ring);
	amdgpu_ring_mux_set_wptr(mux, ring, ring->wptr);
}

/* Override insert_nop to prevent emitting nops to the software rings */
void amdgpu_sw_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
{
	WARN_ON(!ring->is_sw_ring);
}

const char *amdgpu_sw_ring_name(int idx)
{
	return idx < ARRAY_SIZE(sw_ring_info) ?
		sw_ring_info[idx].ring_name : NULL;
}

unsigned int amdgpu_sw_ring_priority(int idx)
{
	return idx < ARRAY_SIZE(sw_ring_info) ?
		sw_ring_info[idx].hw_pio : AMDGPU_RING_PRIO_DEFAULT;
}

/*Scan on low prio rings to have unsignaled fence and high ring has no fence.*/
static int amdgpu_mcbp_scan(struct amdgpu_ring_mux *mux)
{
	struct amdgpu_ring *ring;
	int i, need_preempt;

	need_preempt = 0;
	for (i = 0; i < mux->num_ring_entries; i++) {
		ring = mux->ring_entry[i].ring;
		if (ring->hw_prio > AMDGPU_RING_PRIO_DEFAULT &&
		    amdgpu_fence_count_emitted(ring) > 0)
			return 0;
		if (ring->hw_prio <= AMDGPU_RING_PRIO_DEFAULT &&
		    amdgpu_fence_last_unsignaled_time_us(ring) >
		    AMDGPU_MAX_LAST_UNSIGNALED_THRESHOLD_US)
			need_preempt = 1;
	}
	return need_preempt && !mux->s_resubmit;
}

/* Trigger Mid-Command Buffer Preemption (MCBP) and find if we need to resubmit. */
static int amdgpu_mcbp_trigger_preempt(struct amdgpu_ring_mux *mux)
{
	int r;

	spin_lock(&mux->lock);
	mux->pending_trailing_fence_signaled = true;
	r = amdgpu_ring_preempt_ib(mux->real_ring);
	spin_unlock(&mux->lock);
	return r;
}

void amdgpu_sw_ring_ib_begin(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ring_mux *mux = &adev->gfx.muxer;

	WARN_ON(!ring->is_sw_ring);
	if (adev->gfx.mcbp && ring->hw_prio > AMDGPU_RING_PRIO_DEFAULT) {
		if (amdgpu_mcbp_scan(mux) > 0)
			amdgpu_mcbp_trigger_preempt(mux);
		return;
	}

	amdgpu_ring_mux_start_ib(mux, ring);
}

void amdgpu_sw_ring_ib_end(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ring_mux *mux = &adev->gfx.muxer;

	WARN_ON(!ring->is_sw_ring);
	if (adev->gfx.mcbp && ring->hw_prio > AMDGPU_RING_PRIO_DEFAULT)
		return;
	amdgpu_ring_mux_end_ib(mux, ring);
}

void amdgpu_sw_ring_ib_mark_offset(struct amdgpu_ring *ring, enum amdgpu_ring_mux_offset_type type)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ring_mux *mux = &adev->gfx.muxer;
	unsigned offset;

	if (ring->hw_prio > AMDGPU_RING_PRIO_DEFAULT)
		return;

	offset = ring->wptr & ring->buf_mask;

	amdgpu_ring_mux_ib_mark_offset(mux, ring, offset, type);
}

void amdgpu_ring_mux_start_ib(struct amdgpu_ring_mux *mux, struct amdgpu_ring *ring)
{
	struct amdgpu_mux_entry *e;
	struct amdgpu_mux_chunk *chunk;

	spin_lock(&mux->lock);
	amdgpu_mux_resubmit_chunks(mux);
	spin_unlock(&mux->lock);

	e = amdgpu_ring_mux_sw_entry(mux, ring);
	if (!e) {
		DRM_ERROR("cannot find entry!\n");
		return;
	}

	chunk = kmem_cache_alloc(amdgpu_mux_chunk_slab, GFP_KERNEL);
	if (!chunk) {
		DRM_ERROR("alloc amdgpu_mux_chunk_slab failed\n");
		return;
	}

	chunk->start = ring->wptr;
	/* the initialized value used to check if they are set by the ib submission*/
	chunk->cntl_offset = ring->buf_mask + 1;
	chunk->de_offset = ring->buf_mask + 1;
	chunk->ce_offset = ring->buf_mask + 1;
	list_add_tail(&chunk->entry, &e->list);
}

static void scan_and_remove_signaled_chunk(struct amdgpu_ring_mux *mux, struct amdgpu_ring *ring)
{
	uint32_t last_seq = 0;
	struct amdgpu_mux_entry *e;
	struct amdgpu_mux_chunk *chunk, *tmp;

	e = amdgpu_ring_mux_sw_entry(mux, ring);
	if (!e) {
		DRM_ERROR("cannot find entry!\n");
		return;
	}

	last_seq = atomic_read(&ring->fence_drv.last_seq);

	list_for_each_entry_safe(chunk, tmp, &e->list, entry) {
		if (chunk->sync_seq <= last_seq) {
			list_del(&chunk->entry);
			kmem_cache_free(amdgpu_mux_chunk_slab, chunk);
		}
	}
}

void amdgpu_ring_mux_ib_mark_offset(struct amdgpu_ring_mux *mux,
				    struct amdgpu_ring *ring, u64 offset,
				    enum amdgpu_ring_mux_offset_type type)
{
	struct amdgpu_mux_entry *e;
	struct amdgpu_mux_chunk *chunk;

	e = amdgpu_ring_mux_sw_entry(mux, ring);
	if (!e) {
		DRM_ERROR("cannot find entry!\n");
		return;
	}

	chunk = list_last_entry(&e->list, struct amdgpu_mux_chunk, entry);
	if (!chunk) {
		DRM_ERROR("cannot find chunk!\n");
		return;
	}

	switch (type) {
	case AMDGPU_MUX_OFFSET_TYPE_CONTROL:
		chunk->cntl_offset = offset;
		break;
	case AMDGPU_MUX_OFFSET_TYPE_DE:
		chunk->de_offset = offset;
		break;
	case AMDGPU_MUX_OFFSET_TYPE_CE:
		chunk->ce_offset = offset;
		break;
	default:
		DRM_ERROR("invalid type (%d)\n", type);
		break;
	}
}

void amdgpu_ring_mux_end_ib(struct amdgpu_ring_mux *mux, struct amdgpu_ring *ring)
{
	struct amdgpu_mux_entry *e;
	struct amdgpu_mux_chunk *chunk;

	e = amdgpu_ring_mux_sw_entry(mux, ring);
	if (!e) {
		DRM_ERROR("cannot find entry!\n");
		return;
	}

	chunk = list_last_entry(&e->list, struct amdgpu_mux_chunk, entry);
	if (!chunk) {
		DRM_ERROR("cannot find chunk!\n");
		return;
	}

	chunk->end = ring->wptr;
	chunk->sync_seq = READ_ONCE(ring->fence_drv.sync_seq);

	scan_and_remove_signaled_chunk(mux, ring);
}

bool amdgpu_mcbp_handle_trailing_fence_irq(struct amdgpu_ring_mux *mux)
{
	struct amdgpu_mux_entry *e;
	struct amdgpu_ring *ring = NULL;
	int i;

	if (!mux->pending_trailing_fence_signaled)
		return false;

	if (mux->real_ring->trail_seq != le32_to_cpu(*mux->real_ring->trail_fence_cpu_addr))
		return false;

	for (i = 0; i < mux->num_ring_entries; i++) {
		e = &mux->ring_entry[i];
		if (e->ring->hw_prio <= AMDGPU_RING_PRIO_DEFAULT) {
			ring = e->ring;
			break;
		}
	}

	if (!ring) {
		DRM_ERROR("cannot find low priority ring\n");
		return false;
	}

	amdgpu_fence_process(ring);
	if (amdgpu_fence_count_emitted(ring) > 0) {
		mux->s_resubmit = true;
		mux->seqno_to_resubmit = ring->fence_drv.sync_seq;
		amdgpu_ring_mux_schedule_resubmit(mux);
	}

	mux->pending_trailing_fence_signaled = false;
	return true;
}
