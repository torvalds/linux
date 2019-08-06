/*
 * Copyright © 2014-2017 Intel Corporation
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

#ifndef _INTEL_GUC_LOG_H_
#define _INTEL_GUC_LOG_H_

#include <linux/mutex.h>
#include <linux/relay.h>
#include <linux/workqueue.h>

#include "intel_guc_fwif.h"
#include "i915_gem.h"

struct intel_guc;

#ifdef CONFIG_DRM_I915_DEBUG_GUC
#define CRASH_BUFFER_SIZE	SZ_2M
#define DPC_BUFFER_SIZE		SZ_8M
#define ISR_BUFFER_SIZE		SZ_8M
#else
#define CRASH_BUFFER_SIZE	SZ_8K
#define DPC_BUFFER_SIZE		SZ_32K
#define ISR_BUFFER_SIZE		SZ_32K
#endif

/*
 * While we're using plain log level in i915, GuC controls are much more...
 * "elaborate"? We have a couple of bits for verbosity, separate bit for actual
 * log enabling, and separate bit for default logging - which "conveniently"
 * ignores the enable bit.
 */
#define GUC_LOG_LEVEL_DISABLED		0
#define GUC_LOG_LEVEL_NON_VERBOSE	1
#define GUC_LOG_LEVEL_IS_ENABLED(x)	((x) > GUC_LOG_LEVEL_DISABLED)
#define GUC_LOG_LEVEL_IS_VERBOSE(x)	((x) > GUC_LOG_LEVEL_NON_VERBOSE)
#define GUC_LOG_LEVEL_TO_VERBOSITY(x) ({		\
	typeof(x) _x = (x);				\
	GUC_LOG_LEVEL_IS_VERBOSE(_x) ? _x - 2 : 0;	\
})
#define GUC_VERBOSITY_TO_LOG_LEVEL(x)	((x) + 2)
#define GUC_LOG_LEVEL_MAX GUC_VERBOSITY_TO_LOG_LEVEL(GUC_LOG_VERBOSITY_MAX)

struct intel_guc_log {
	u32 level;
	struct i915_vma *vma;
	struct {
		void *buf_addr;
		struct work_struct flush_work;
		struct rchan *channel;
		struct mutex lock;
		u32 full_count;
	} relay;
	/* logging related stats */
	struct {
		u32 sampled_overflow;
		u32 overflow;
		u32 flush;
	} stats[GUC_MAX_LOG_BUFFER];
};

void intel_guc_log_init_early(struct intel_guc_log *log);
int intel_guc_log_create(struct intel_guc_log *log);
void intel_guc_log_destroy(struct intel_guc_log *log);

int intel_guc_log_set_level(struct intel_guc_log *log, u32 level);
bool intel_guc_log_relay_enabled(const struct intel_guc_log *log);
int intel_guc_log_relay_open(struct intel_guc_log *log);
void intel_guc_log_relay_flush(struct intel_guc_log *log);
void intel_guc_log_relay_close(struct intel_guc_log *log);

void intel_guc_log_handle_flush_event(struct intel_guc_log *log);

static inline u32 intel_guc_log_get_level(struct intel_guc_log *log)
{
	return log->level;
}

#endif
