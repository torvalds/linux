/*
 * Copyright © 2014 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */
#ifndef _INTEL_GUC_H_
#define _INTEL_GUC_H_

#include "intel_guc_fwif.h"
#include "i915_guc_reg.h"

struct drm_i915_gem_request;

/*
 * This structure primarily describes the GEM object shared with the GuC.
 * The GEM object is held for the entire lifetime of our interaction with
 * the GuC, being allocated before the GuC is loaded with its firmware.
 * Because there's no way to update the address used by the GuC after
 * initialisation, the shared object must stay pinned into the GGTT as
 * long as the GuC is in use. We also keep the first page (only) mapped
 * into kernel address space, as it includes shared data that must be
 * updated on every request submission.
 *
 * The single GEM object described here is actually made up of several
 * separate areas, as far as the GuC is concerned. The first page (kept
 * kmap'd) includes the "process decriptor" which holds sequence data for
 * the doorbell, and one cacheline which actually *is* the doorbell; a
 * write to this will "ring the doorbell" (i.e. send an interrupt to the
 * GuC). The subsequent  pages of the client object constitute the work
 * queue (a circular array of work items), again described in the process
 * descriptor. Work queue pages are mapped momentarily as required.
 *
 * We also keep a few statistics on failures. Ideally, these should all
 * be zero!
 *   no_wq_space: times that the submission pre-check found no space was
 *                available in the work queue (note, the queue is shared,
 *                not per-engine). It is OK for this to be nonzero, but
 *                it should not be huge!
 *   q_fail: failed to enqueue a work item. This should never happen,
 *           because we check for space beforehand.
 *   b_fail: failed to ring the doorbell. This should never happen, unless
 *           somehow the hardware misbehaves, or maybe if the GuC firmware
 *           crashes? We probably need to reset the GPU to recover.
 *   retcode: errno from last guc_submit()
 */
struct i915_guc_client {
	struct drm_i915_gem_object *client_obj;
	void *client_base;		/* first page (only) of above	*/
	struct i915_gem_context *owner;
	struct intel_guc *guc;
	uint32_t priority;
	uint32_t ctx_index;

	uint32_t proc_desc_offset;
	uint32_t doorbell_offset;
	uint32_t cookie;
	uint16_t doorbell_id;
	uint16_t padding;		/* Maintain alignment		*/

	uint32_t wq_offset;
	uint32_t wq_size;
	uint32_t wq_tail;
	uint32_t unused;		/* Was 'wq_head'		*/

	uint32_t no_wq_space;
	uint32_t q_fail;		/* No longer used		*/
	uint32_t b_fail;
	int retcode;

	/* Per-engine counts of GuC submissions */
	uint64_t submissions[GUC_MAX_ENGINES_NUM];
};

enum intel_guc_fw_status {
	GUC_FIRMWARE_FAIL = -1,
	GUC_FIRMWARE_NONE = 0,
	GUC_FIRMWARE_PENDING,
	GUC_FIRMWARE_SUCCESS
};

/*
 * This structure encapsulates all the data needed during the process
 * of fetching, caching, and loading the firmware image into the GuC.
 */
struct intel_guc_fw {
	struct drm_device *		guc_dev;
	const char *			guc_fw_path;
	size_t				guc_fw_size;
	struct drm_i915_gem_object *	guc_fw_obj;
	enum intel_guc_fw_status	guc_fw_fetch_status;
	enum intel_guc_fw_status	guc_fw_load_status;

	uint16_t			guc_fw_major_wanted;
	uint16_t			guc_fw_minor_wanted;
	uint16_t			guc_fw_major_found;
	uint16_t			guc_fw_minor_found;

	uint32_t header_size;
	uint32_t header_offset;
	uint32_t rsa_size;
	uint32_t rsa_offset;
	uint32_t ucode_size;
	uint32_t ucode_offset;
};

struct intel_guc {
	struct intel_guc_fw guc_fw;
	uint32_t log_flags;
	struct drm_i915_gem_object *log_obj;

	struct drm_i915_gem_object *ads_obj;

	struct drm_i915_gem_object *ctx_pool_obj;
	struct ida ctx_ids;

	struct i915_guc_client *execbuf_client;

	DECLARE_BITMAP(doorbell_bitmap, GUC_MAX_DOORBELLS);
	uint32_t db_cacheline;		/* Cyclic counter mod pagesize	*/

	/* Action status & statistics */
	uint64_t action_count;		/* Total commands issued	*/
	uint32_t action_cmd;		/* Last command word		*/
	uint32_t action_status;		/* Last return status		*/
	uint32_t action_fail;		/* Total number of failures	*/
	int32_t action_err;		/* Last error code		*/

	uint64_t submissions[GUC_MAX_ENGINES_NUM];
	uint32_t last_seqno[GUC_MAX_ENGINES_NUM];
};

/* intel_guc_loader.c */
extern void intel_guc_init(struct drm_device *dev);
extern int intel_guc_setup(struct drm_device *dev);
extern void intel_guc_fini(struct drm_device *dev);
extern const char *intel_guc_fw_status_repr(enum intel_guc_fw_status status);
extern int intel_guc_suspend(struct drm_device *dev);
extern int intel_guc_resume(struct drm_device *dev);

/* i915_guc_submission.c */
int i915_guc_submission_init(struct drm_device *dev);
int i915_guc_submission_enable(struct drm_device *dev);
int i915_guc_wq_check_space(struct drm_i915_gem_request *rq);
int i915_guc_submit(struct drm_i915_gem_request *rq);
void i915_guc_submission_disable(struct drm_device *dev);
void i915_guc_submission_fini(struct drm_device *dev);

#endif
