/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MSM_KMS_H__
#define __MSM_KMS_H__

#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include "msm_drv.h"

#define MAX_PLANE	4

/* As there are different display controller blocks depending on the
 * snapdragon version, the kms support is split out and the appropriate
 * implementation is loaded at runtime.  The kms module is responsible
 * for constructing the appropriate planes/crtcs/encoders/connectors.
 */
struct msm_kms_funcs {
	/* hw initialization: */
	int (*hw_init)(struct msm_kms *kms);
	/* irq handling: */
	void (*irq_preinstall)(struct msm_kms *kms);
	int (*irq_postinstall)(struct msm_kms *kms);
	void (*irq_uninstall)(struct msm_kms *kms);
	irqreturn_t (*irq)(struct msm_kms *kms);
	int (*enable_vblank)(struct msm_kms *kms, struct drm_crtc *crtc);
	void (*disable_vblank)(struct msm_kms *kms, struct drm_crtc *crtc);

	/*
	 * Atomic commit handling:
	 *
	 * Note that in the case of async commits, the funcs which take
	 * a crtc_mask (ie. ->flush_commit(), and ->complete_commit())
	 * might not be evenly balanced with ->prepare_commit(), however
	 * each crtc that effected by a ->prepare_commit() (potentially
	 * multiple times) will eventually (at end of vsync period) be
	 * flushed and completed.
	 *
	 * This has some implications about tracking of cleanup state,
	 * for example SMP blocks to release after commit completes.  Ie.
	 * cleanup state should be also duplicated in the various
	 * duplicate_state() methods, as the current cleanup state at
	 * ->complete_commit() time may have accumulated cleanup work
	 * from multiple commits.
	 */

	/**
	 * Enable/disable power/clks needed for hw access done in other
	 * commit related methods.
	 *
	 * If mdp4 is migrated to runpm, we could probably drop these
	 * and use runpm directly.
	 */
	void (*enable_commit)(struct msm_kms *kms);
	void (*disable_commit)(struct msm_kms *kms);

	/**
	 * If the kms backend supports async commit, it should implement
	 * this method to return the time of the next vsync.  This is
	 * used to determine a time slightly before vsync, for the async
	 * commit timer to run and complete an async commit.
	 */
	ktime_t (*vsync_time)(struct msm_kms *kms, struct drm_crtc *crtc);

	/**
	 * Prepare for atomic commit.  This is called after any previous
	 * (async or otherwise) commit has completed.
	 */
	void (*prepare_commit)(struct msm_kms *kms, struct drm_atomic_state *state);

	/**
	 * Flush an atomic commit.  This is called after the hardware
	 * updates have already been pushed down to effected planes/
	 * crtcs/encoders/connectors.
	 */
	void (*flush_commit)(struct msm_kms *kms, unsigned crtc_mask);

	/**
	 * Wait for any in-progress flush to complete on the specified
	 * crtcs.  This should not block if there is no in-progress
	 * commit (ie. don't just wait for a vblank), as it will also
	 * be called before ->prepare_commit() to ensure any potential
	 * "async" commit has completed.
	 */
	void (*wait_flush)(struct msm_kms *kms, unsigned crtc_mask);

	/**
	 * Clean up after commit is completed.  This is called after
	 * ->wait_flush(), to give the backend a chance to do any
	 * post-commit cleanup.
	 */
	void (*complete_commit)(struct msm_kms *kms, unsigned crtc_mask);

	/*
	 * Format handling:
	 */

	/* get msm_format w/ optional format modifiers from drm_mode_fb_cmd2 */
	const struct msm_format *(*get_format)(struct msm_kms *kms,
					const uint32_t format,
					const uint64_t modifiers);
	/* do format checking on format modified through fb_cmd2 modifiers */
	int (*check_modified_format)(const struct msm_kms *kms,
			const struct msm_format *msm_fmt,
			const struct drm_mode_fb_cmd2 *cmd,
			struct drm_gem_object **bos);

	/* misc: */
	long (*round_pixclk)(struct msm_kms *kms, unsigned long rate,
			struct drm_encoder *encoder);
	int (*set_split_display)(struct msm_kms *kms,
			struct drm_encoder *encoder,
			struct drm_encoder *slave_encoder,
			bool is_cmd_mode);
	void (*set_encoder_mode)(struct msm_kms *kms,
				 struct drm_encoder *encoder,
				 bool cmd_mode);
	/* cleanup: */
	void (*destroy)(struct msm_kms *kms);
#ifdef CONFIG_DEBUG_FS
	/* debugfs: */
	int (*debugfs_init)(struct msm_kms *kms, struct drm_minor *minor);
#endif
};

struct msm_kms;

/*
 * A per-crtc timer for pending async atomic flushes.  Scheduled to expire
 * shortly before vblank to flush pending async updates.
 */
struct msm_pending_timer {
	struct hrtimer timer;
	struct kthread_work work;
	struct kthread_worker *worker;
	struct msm_kms *kms;
	unsigned crtc_idx;
};

struct msm_kms {
	const struct msm_kms_funcs *funcs;
	struct drm_device *dev;

	/* irq number to be passed on to drm_irq_install */
	int irq;

	/* mapper-id used to request GEM buffer mapped for scanout: */
	struct msm_gem_address_space *aspace;

	/*
	 * For async commit, where ->flush_commit() and later happens
	 * from the crtc's pending_timer close to end of the frame:
	 */
	struct mutex commit_lock[MAX_CRTCS];
	struct lock_class_key commit_lock_keys[MAX_CRTCS];
	unsigned pending_crtc_mask;
	struct msm_pending_timer pending_timers[MAX_CRTCS];
};

static inline int msm_kms_init(struct msm_kms *kms,
		const struct msm_kms_funcs *funcs)
{
	unsigned i, ret;

	for (i = 0; i < ARRAY_SIZE(kms->commit_lock); i++) {
		lockdep_register_key(&kms->commit_lock_keys[i]);
		__mutex_init(&kms->commit_lock[i], "&kms->commit_lock[i]",
			     &kms->commit_lock_keys[i]);
	}

	kms->funcs = funcs;

	for (i = 0; i < ARRAY_SIZE(kms->pending_timers); i++) {
		ret = msm_atomic_init_pending_timer(&kms->pending_timers[i], kms, i);
		if (ret) {
			return ret;
		}
	}

	return 0;
}

static inline void msm_kms_destroy(struct msm_kms *kms)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(kms->pending_timers); i++)
		msm_atomic_destroy_pending_timer(&kms->pending_timers[i]);
}

struct msm_kms *mdp4_kms_init(struct drm_device *dev);
struct msm_kms *mdp5_kms_init(struct drm_device *dev);
struct msm_kms *dpu_kms_init(struct drm_device *dev);

struct msm_mdss_funcs {
	int (*enable)(struct msm_mdss *mdss);
	int (*disable)(struct msm_mdss *mdss);
	void (*destroy)(struct drm_device *dev);
};

struct msm_mdss {
	struct drm_device *dev;
	const struct msm_mdss_funcs *funcs;
};

int mdp5_mdss_init(struct drm_device *dev);
int dpu_mdss_init(struct drm_device *dev);

#define for_each_crtc_mask(dev, crtc, crtc_mask) \
	drm_for_each_crtc(crtc, dev) \
		for_each_if (drm_crtc_mask(crtc) & (crtc_mask))

#define for_each_crtc_mask_reverse(dev, crtc, crtc_mask) \
	drm_for_each_crtc_reverse(crtc, dev) \
		for_each_if (drm_crtc_mask(crtc) & (crtc_mask))

#endif /* __MSM_KMS_H__ */
