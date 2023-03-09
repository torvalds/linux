/*
 * Copyright(c) 2011-2017 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "i915_drv.h"
#include "gvt.h"

/**
 * intel_vgpu_find_page_track - find page track rcord of guest page
 * @vgpu: a vGPU
 * @gfn: the gfn of guest page
 *
 * Returns:
 * A pointer to struct intel_vgpu_page_track if found, else NULL returned.
 */
struct intel_vgpu_page_track *intel_vgpu_find_page_track(
		struct intel_vgpu *vgpu, unsigned long gfn)
{
	return radix_tree_lookup(&vgpu->page_track_tree, gfn);
}

/**
 * intel_vgpu_register_page_track - register a guest page to be tacked
 * @vgpu: a vGPU
 * @gfn: the gfn of guest page
 * @handler: page track handler
 * @priv: tracker private
 *
 * Returns:
 * zero on success, negative error code if failed.
 */
int intel_vgpu_register_page_track(struct intel_vgpu *vgpu, unsigned long gfn,
		gvt_page_track_handler_t handler, void *priv)
{
	struct intel_vgpu_page_track *track;
	int ret;

	track = intel_vgpu_find_page_track(vgpu, gfn);
	if (track)
		return -EEXIST;

	track = kzalloc(sizeof(*track), GFP_KERNEL);
	if (!track)
		return -ENOMEM;

	track->handler = handler;
	track->priv_data = priv;

	ret = radix_tree_insert(&vgpu->page_track_tree, gfn, track);
	if (ret) {
		kfree(track);
		return ret;
	}

	return 0;
}

/**
 * intel_vgpu_unregister_page_track - unregister the tracked guest page
 * @vgpu: a vGPU
 * @gfn: the gfn of guest page
 *
 */
void intel_vgpu_unregister_page_track(struct intel_vgpu *vgpu,
		unsigned long gfn)
{
	struct intel_vgpu_page_track *track;

	track = radix_tree_delete(&vgpu->page_track_tree, gfn);
	if (track) {
		if (track->tracked)
			intel_gvt_page_track_remove(vgpu, gfn);
		kfree(track);
	}
}

/**
 * intel_vgpu_enable_page_track - set write-protection on guest page
 * @vgpu: a vGPU
 * @gfn: the gfn of guest page
 *
 * Returns:
 * zero on success, negative error code if failed.
 */
int intel_vgpu_enable_page_track(struct intel_vgpu *vgpu, unsigned long gfn)
{
	struct intel_vgpu_page_track *track;
	int ret;

	track = intel_vgpu_find_page_track(vgpu, gfn);
	if (!track)
		return -ENXIO;

	if (track->tracked)
		return 0;

	ret = intel_gvt_page_track_add(vgpu, gfn);
	if (ret)
		return ret;
	track->tracked = true;
	return 0;
}

/**
 * intel_vgpu_disable_page_track - cancel write-protection on guest page
 * @vgpu: a vGPU
 * @gfn: the gfn of guest page
 *
 * Returns:
 * zero on success, negative error code if failed.
 */
int intel_vgpu_disable_page_track(struct intel_vgpu *vgpu, unsigned long gfn)
{
	struct intel_vgpu_page_track *track;
	int ret;

	track = intel_vgpu_find_page_track(vgpu, gfn);
	if (!track)
		return -ENXIO;

	if (!track->tracked)
		return 0;

	ret = intel_gvt_page_track_remove(vgpu, gfn);
	if (ret)
		return ret;
	track->tracked = false;
	return 0;
}

/**
 * intel_vgpu_page_track_handler - called when write to write-protected page
 * @vgpu: a vGPU
 * @gpa: the gpa of this write
 * @data: the writed data
 * @bytes: the length of this write
 *
 * Returns:
 * zero on success, negative error code if failed.
 */
int intel_vgpu_page_track_handler(struct intel_vgpu *vgpu, u64 gpa,
		void *data, unsigned int bytes)
{
	struct intel_vgpu_page_track *page_track;
	int ret = 0;

	mutex_lock(&vgpu->vgpu_lock);

	page_track = intel_vgpu_find_page_track(vgpu, gpa >> PAGE_SHIFT);
	if (!page_track) {
		ret = -ENXIO;
		goto out;
	}

	if (unlikely(vgpu->failsafe)) {
		/* Remove write protection to prevent furture traps. */
		intel_gvt_page_track_remove(vgpu, gpa >> PAGE_SHIFT);
	} else {
		ret = page_track->handler(page_track, gpa, data, bytes);
		if (ret)
			gvt_err("guest page write error, gpa %llx\n", gpa);
	}

out:
	mutex_unlock(&vgpu->vgpu_lock);
	return ret;
}
