/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_GUC_LOG_H_
#define _INTEL_GUC_LOG_H_

#include <linux/mutex.h>
#include <linux/relay.h>
#include <linux/workqueue.h>

#include "intel_guc_fwif.h"
#include "i915_gem.h"

struct intel_guc;

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

enum {
	GUC_LOG_SECTIONS_CRASH,
	GUC_LOG_SECTIONS_DEBUG,
	GUC_LOG_SECTIONS_CAPTURE,
	GUC_LOG_SECTIONS_LIMIT
};

struct intel_guc_log {
	u32 level;

	/*
	 * Protects concurrent access and modification of intel_guc_log->level.
	 *
	 * This lock replaces the legacy struct_mutex usage in
	 * intel_guc_log system.
	 */
	struct mutex guc_lock;

	/* Allocation settings */
	struct {
		s32 bytes;	/* Size in bytes */
		s32 units;	/* GuC API units - 1MB or 4KB */
		s32 count;	/* Number of API units */
		u32 flag;	/* GuC API units flag */
	} sizes[GUC_LOG_SECTIONS_LIMIT];
	bool sizes_initialised;

	/* Combined buffer allocation */
	struct i915_vma *vma;
	void *buf_addr;

	/* RelayFS support */
	struct {
		bool buf_in_use;
		bool started;
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
bool intel_guc_check_log_buf_overflow(struct intel_guc_log *log, enum guc_log_buffer_type type,
				      unsigned int full_cnt);
unsigned int intel_guc_get_log_buffer_size(struct intel_guc_log *log,
					   enum guc_log_buffer_type type);
size_t intel_guc_get_log_buffer_offset(struct intel_guc_log *log, enum guc_log_buffer_type type);
int intel_guc_log_create(struct intel_guc_log *log);
void intel_guc_log_destroy(struct intel_guc_log *log);

int intel_guc_log_set_level(struct intel_guc_log *log, u32 level);
bool intel_guc_log_relay_created(const struct intel_guc_log *log);
int intel_guc_log_relay_open(struct intel_guc_log *log);
int intel_guc_log_relay_start(struct intel_guc_log *log);
void intel_guc_log_relay_flush(struct intel_guc_log *log);
void intel_guc_log_relay_close(struct intel_guc_log *log);

void intel_guc_log_handle_flush_event(struct intel_guc_log *log);

static inline u32 intel_guc_log_get_level(struct intel_guc_log *log)
{
	return log->level;
}

void intel_guc_log_info(struct intel_guc_log *log, struct drm_printer *p);
int intel_guc_log_dump(struct intel_guc_log *log, struct drm_printer *p,
		       bool dump_load_err);

u32 intel_guc_log_section_size_capture(struct intel_guc_log *log);

#endif
