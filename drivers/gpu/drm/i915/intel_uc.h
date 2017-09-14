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
#ifndef _INTEL_UC_H_
#define _INTEL_UC_H_

#include "intel_guc_fwif.h"
#include "i915_guc_reg.h"
#include "intel_ringbuffer.h"
#include "intel_guc_ct.h"
#include "i915_vma.h"

struct drm_i915_gem_request;

/*
 * This structure primarily describes the GEM object shared with the GuC.
 * The specs sometimes refer to this object as a "GuC context", but we use
 * the term "client" to avoid confusion with hardware contexts. This
 * GEM object is held for the entire lifetime of our interaction with
 * the GuC, being allocated before the GuC is loaded with its firmware.
 * Because there's no way to update the address used by the GuC after
 * initialisation, the shared object must stay pinned into the GGTT as
 * long as the GuC is in use. We also keep the first page (only) mapped
 * into kernel address space, as it includes shared data that must be
 * updated on every request submission.
 *
 * The single GEM object described here is actually made up of several
 * separate areas, as far as the GuC is concerned. The first page (kept
 * kmap'd) includes the "process descriptor" which holds sequence data for
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
 */
struct i915_guc_client {
	struct i915_vma *vma;
	void *vaddr;
	struct i915_gem_context *owner;
	struct intel_guc *guc;

	uint32_t engines;		/* bitmap of (host) engine ids	*/
	uint32_t priority;
	u32 stage_id;
	uint32_t proc_desc_offset;

	u16 doorbell_id;
	unsigned long doorbell_offset;
	u32 doorbell_cookie;

	spinlock_t wq_lock;
	uint32_t wq_offset;
	uint32_t wq_size;
	uint32_t wq_tail;
	uint32_t wq_rsvd;
	uint32_t no_wq_space;

	/* Per-engine counts of GuC submissions */
	uint64_t submissions[I915_NUM_ENGINES];
};

enum intel_uc_fw_status {
	INTEL_UC_FIRMWARE_FAIL = -1,
	INTEL_UC_FIRMWARE_NONE = 0,
	INTEL_UC_FIRMWARE_PENDING,
	INTEL_UC_FIRMWARE_SUCCESS
};

/* User-friendly representation of an enum */
static inline
const char *intel_uc_fw_status_repr(enum intel_uc_fw_status status)
{
	switch (status) {
	case INTEL_UC_FIRMWARE_FAIL:
		return "FAIL";
	case INTEL_UC_FIRMWARE_NONE:
		return "NONE";
	case INTEL_UC_FIRMWARE_PENDING:
		return "PENDING";
	case INTEL_UC_FIRMWARE_SUCCESS:
		return "SUCCESS";
	}
	return "<invalid>";
}

enum intel_uc_fw_type {
	INTEL_UC_FW_TYPE_GUC,
	INTEL_UC_FW_TYPE_HUC
};

/* User-friendly representation of an enum */
static inline const char *intel_uc_fw_type_repr(enum intel_uc_fw_type type)
{
	switch (type) {
	case INTEL_UC_FW_TYPE_GUC:
		return "GuC";
	case INTEL_UC_FW_TYPE_HUC:
		return "HuC";
	}
	return "uC";
}

/*
 * This structure encapsulates all the data needed during the process
 * of fetching, caching, and loading the firmware image into the GuC.
 */
struct intel_uc_fw {
	const char *path;
	size_t size;
	struct drm_i915_gem_object *obj;
	enum intel_uc_fw_status fetch_status;
	enum intel_uc_fw_status load_status;

	uint16_t major_ver_wanted;
	uint16_t minor_ver_wanted;
	uint16_t major_ver_found;
	uint16_t minor_ver_found;

	enum intel_uc_fw_type type;
	uint32_t header_size;
	uint32_t header_offset;
	uint32_t rsa_size;
	uint32_t rsa_offset;
	uint32_t ucode_size;
	uint32_t ucode_offset;
};

struct intel_guc_log {
	uint32_t flags;
	struct i915_vma *vma;
	/* The runtime stuff gets created only when GuC logging gets enabled */
	struct {
		void *buf_addr;
		struct workqueue_struct *flush_wq;
		struct work_struct flush_work;
		struct rchan *relay_chan;
	} runtime;
	/* logging related stats */
	u32 capture_miss_count;
	u32 flush_interrupt_count;
	u32 prev_overflow_count[GUC_MAX_LOG_BUFFER];
	u32 total_overflow_count[GUC_MAX_LOG_BUFFER];
	u32 flush_count[GUC_MAX_LOG_BUFFER];
};

struct intel_guc {
	struct intel_uc_fw fw;
	struct intel_guc_log log;
	struct intel_guc_ct ct;

	/* Log snapshot if GuC errors during load */
	struct drm_i915_gem_object *load_err_log;

	/* intel_guc_recv interrupt related state */
	bool interrupts_enabled;

	struct i915_vma *ads_vma;
	struct i915_vma *stage_desc_pool;
	void *stage_desc_pool_vaddr;
	struct ida stage_ids;

	struct i915_guc_client *execbuf_client;

	DECLARE_BITMAP(doorbell_bitmap, GUC_NUM_DOORBELLS);
	uint32_t db_cacheline;		/* Cyclic counter mod pagesize	*/

	/* GuC's FW specific registers used in MMIO send */
	struct {
		u32 base;
		unsigned int count;
		enum forcewake_domains fw_domains;
	} send_regs;

	/* To serialize the intel_guc_send actions */
	struct mutex send_mutex;

	/* GuC's FW specific send function */
	int (*send)(struct intel_guc *guc, const u32 *data, u32 len);

	/* GuC's FW specific notify function */
	void (*notify)(struct intel_guc *guc);
};

struct intel_huc {
	/* Generic uC firmware management */
	struct intel_uc_fw fw;

	/* HuC-specific additions */
};

/* intel_uc.c */
void intel_uc_sanitize_options(struct drm_i915_private *dev_priv);
void intel_uc_init_early(struct drm_i915_private *dev_priv);
void intel_uc_init_fw(struct drm_i915_private *dev_priv);
void intel_uc_fini_fw(struct drm_i915_private *dev_priv);
int intel_uc_init_hw(struct drm_i915_private *dev_priv);
void intel_uc_fini_hw(struct drm_i915_private *dev_priv);
int intel_guc_sample_forcewake(struct intel_guc *guc);
int intel_guc_send_nop(struct intel_guc *guc, const u32 *action, u32 len);
int intel_guc_send_mmio(struct intel_guc *guc, const u32 *action, u32 len);

static inline int intel_guc_send(struct intel_guc *guc, const u32 *action, u32 len)
{
	return guc->send(guc, action, len);
}

static inline void intel_guc_notify(struct intel_guc *guc)
{
	guc->notify(guc);
}

/* intel_guc_loader.c */
int intel_guc_select_fw(struct intel_guc *guc);
int intel_guc_init_hw(struct intel_guc *guc);
int intel_guc_suspend(struct drm_i915_private *dev_priv);
int intel_guc_resume(struct drm_i915_private *dev_priv);
u32 intel_guc_wopcm_size(struct drm_i915_private *dev_priv);

/* i915_guc_submission.c */
int i915_guc_submission_init(struct drm_i915_private *dev_priv);
int i915_guc_submission_enable(struct drm_i915_private *dev_priv);
int i915_guc_wq_reserve(struct drm_i915_gem_request *rq);
void i915_guc_wq_unreserve(struct drm_i915_gem_request *request);
void i915_guc_submission_disable(struct drm_i915_private *dev_priv);
void i915_guc_submission_fini(struct drm_i915_private *dev_priv);
struct i915_vma *intel_guc_allocate_vma(struct intel_guc *guc, u32 size);

/* intel_guc_log.c */
int intel_guc_log_create(struct intel_guc *guc);
void intel_guc_log_destroy(struct intel_guc *guc);
int i915_guc_log_control(struct drm_i915_private *dev_priv, u64 control_val);
void i915_guc_log_register(struct drm_i915_private *dev_priv);
void i915_guc_log_unregister(struct drm_i915_private *dev_priv);

static inline u32 guc_ggtt_offset(struct i915_vma *vma)
{
	u32 offset = i915_ggtt_offset(vma);
	GEM_BUG_ON(offset < GUC_WOPCM_TOP);
	GEM_BUG_ON(range_overflows_t(u64, offset, vma->size, GUC_GGTT_TOP));
	return offset;
}

/* intel_huc.c */
void intel_huc_select_fw(struct intel_huc *huc);
void intel_huc_init_hw(struct intel_huc *huc);
void intel_guc_auth_huc(struct drm_i915_private *dev_priv);

#endif
