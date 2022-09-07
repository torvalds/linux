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

static const struct ring_info {
	unsigned int hw_pio;
	const char *ring_name;
} sw_ring_info[] = {
	{ AMDGPU_RING_PRIO_DEFAULT, "gfx_low"},
	{ AMDGPU_RING_PRIO_2, "gfx_high"},
};

int amdgpu_ring_mux_init(struct amdgpu_ring_mux *mux, struct amdgpu_ring *ring,
			 unsigned int entry_size)
{
	mux->real_ring = ring;
	mux->num_ring_entries = 0;
	mux->ring_entry = kcalloc(entry_size, sizeof(struct amdgpu_mux_entry), GFP_KERNEL);
	if (!mux->ring_entry)
		return -ENOMEM;

	mux->ring_entry_size = entry_size;
	spin_lock_init(&mux->lock);

	return 0;
}

void amdgpu_ring_mux_fini(struct amdgpu_ring_mux *mux)
{
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

	mux->num_ring_entries += 1;
	return 0;
}

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

void amdgpu_ring_mux_set_wptr(struct amdgpu_ring_mux *mux, struct amdgpu_ring *ring, u64 wptr)
{
	struct amdgpu_mux_entry *e;

	e = amdgpu_ring_mux_sw_entry(mux, ring);
	if (!e) {
		DRM_ERROR("cannot find entry for sw ring\n");
		return;
	}

	spin_lock(&mux->lock);
	e->sw_cptr = e->sw_wptr;
	e->sw_wptr = wptr;
	e->start_ptr_in_hw_ring = mux->real_ring->wptr;

	amdgpu_ring_mux_copy_pkt_from_sw_ring(mux, ring, e->sw_cptr, wptr);
	e->end_ptr_in_hw_ring = mux->real_ring->wptr;
	amdgpu_ring_commit(mux->real_ring);

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
