/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_ASSERT_H_
#define _XE_ASSERT_H_

#include <linux/string_helpers.h>

#include <drm/drm_print.h>

#include "xe_device_types.h"
#include "xe_step.h"

/**
 * DOC: Xe ASSERTs
 *
 * While Xe driver aims to be simpler than legacy i915 driver it is still
 * complex enough that some changes introduced while adding new functionality
 * could break the existing code.
 *
 * Adding &drm_WARN or &drm_err to catch unwanted programming usage could lead
 * to undesired increased driver footprint and may impact production driver
 * performance as this additional code will be always present.
 *
 * To allow annotate functions with additional detailed debug checks to assert
 * that all prerequisites are satisfied, without worrying about footprint or
 * performance penalty on production builds where all potential misuses
 * introduced during code integration were already fixed, we introduce family
 * of Xe assert macros that try to follow classic assert() utility:
 *
 *  * xe_assert()
 *  * xe_tile_assert()
 *  * xe_gt_assert()
 *
 * These macros are implemented on top of &drm_WARN, but unlikely to the origin,
 * warning is triggered when provided condition is false. Additionally all above
 * assert macros cannot be used in expressions or as a condition, since
 * underlying code will be compiled out on non-debug builds.
 *
 * Note that these macros are not intended for use to cover known gaps in the
 * implementation; for such cases use regular &drm_WARN or &drm_err and provide
 * valid safe fallback.
 *
 * Also in cases where performance or footprint is not an issue, developers
 * should continue to use the regular &drm_WARN or &drm_err to ensure that bug
 * reports from production builds will contain meaningful diagnostics data.
 *
 * Below code shows how asserts could help in debug to catch unplanned use::
 *
 *	static void one_igfx(struct xe_device *xe)
 *	{
 *		xe_assert(xe, xe->info.is_dgfx == false);
 *		xe_assert(xe, xe->info.tile_count == 1);
 *	}
 *
 *	static void two_dgfx(struct xe_device *xe)
 *	{
 *		xe_assert(xe, xe->info.is_dgfx);
 *		xe_assert(xe, xe->info.tile_count == 2);
 *	}
 *
 *	void foo(struct xe_device *xe)
 *	{
 *		if (xe->info.dgfx)
 *			return two_dgfx(xe);
 *		return one_igfx(xe);
 *	}
 *
 *	void bar(struct xe_device *xe)
 *	{
 *		if (drm_WARN_ON(xe->drm, xe->info.tile_count > 2))
 *			return;
 *
 *		if (xe->info.tile_count == 2)
 *			return two_dgfx(xe);
 *		return one_igfx(xe);
 *	}
 */

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
#define __xe_assert_msg(xe, condition, msg, arg...) ({						\
	(void)drm_WARN(&(xe)->drm, !(condition), "[" DRM_NAME "] Assertion `%s` failed!\n" msg,	\
		       __stringify(condition), ## arg);						\
})
#else
#define __xe_assert_msg(xe, condition, msg, arg...) ({						\
	typecheck(const struct xe_device *, xe);						\
	BUILD_BUG_ON_INVALID(condition);							\
})
#endif

/**
 * xe_assert - warn if condition is false when debugging.
 * @xe: the &struct xe_device pointer to which &condition applies
 * @condition: condition to check
 *
 * xe_assert() uses &drm_WARN to emit a warning and print additional information
 * that could be read from the &xe pointer if provided &condition is false.
 *
 * Contrary to &drm_WARN, xe_assert() is effective only on debug builds
 * (&CONFIG_DRM_XE_DEBUG must be enabled) and cannot be used in expressions
 * or as a condition.
 *
 * See `Xe ASSERTs`_ for general usage guidelines.
 */
#define xe_assert(xe, condition) xe_assert_msg((xe), condition, "")
#define xe_assert_msg(xe, condition, msg, arg...) ({						\
	const struct xe_device *__xe = (xe);							\
	__xe_assert_msg(__xe, condition,							\
			"platform: %d subplatform: %d\n"					\
			"graphics: %s %u.%02u step %s\n"					\
			"media: %s %u.%02u step %s\n"						\
			msg,									\
			__xe->info.platform, __xe->info.subplatform,				\
			__xe->info.graphics_name,						\
			__xe->info.graphics_verx100 / 100,					\
			__xe->info.graphics_verx100 % 100,					\
			xe_step_name(__xe->info.step.graphics),					\
			__xe->info.media_name,							\
			__xe->info.media_verx100 / 100,						\
			__xe->info.media_verx100 % 100,						\
			xe_step_name(__xe->info.step.media),					\
			## arg);								\
})

/**
 * xe_tile_assert - warn if condition is false when debugging.
 * @tile: the &struct xe_tile pointer to which &condition applies
 * @condition: condition to check
 *
 * xe_tile_assert() uses &drm_WARN to emit a warning and print additional
 * information that could be read from the &tile pointer if provided &condition
 * is false.
 *
 * Contrary to &drm_WARN, xe_tile_assert() is effective only on debug builds
 * (&CONFIG_DRM_XE_DEBUG must be enabled) and cannot be used in expressions
 * or as a condition.
 *
 * See `Xe ASSERTs`_ for general usage guidelines.
 */
#define xe_tile_assert(tile, condition) xe_tile_assert_msg((tile), condition, "")
#define xe_tile_assert_msg(tile, condition, msg, arg...) ({					\
	const struct xe_tile *__tile = (tile);							\
	char __buf[10] __maybe_unused;								\
	xe_assert_msg(tile_to_xe(__tile), condition, "tile: %u VRAM %s\n" msg,			\
		      __tile->id, ({ string_get_size(__tile->mem.vram.actual_physical_size, 1,	\
				     STRING_UNITS_2, __buf, sizeof(__buf)); __buf; }), ## arg);	\
})

/**
 * xe_gt_assert - warn if condition is false when debugging.
 * @gt: the &struct xe_gt pointer to which &condition applies
 * @condition: condition to check
 *
 * xe_gt_assert() uses &drm_WARN to emit a warning and print additional
 * information that could be safetely read from the &gt pointer if provided
 * &condition is false.
 *
 * Contrary to &drm_WARN, xe_gt_assert() is effective only on debug builds
 * (&CONFIG_DRM_XE_DEBUG must be enabled) and cannot be used in expressions
 * or as a condition.
 *
 * See `Xe ASSERTs`_ for general usage guidelines.
 */
#define xe_gt_assert(gt, condition) xe_gt_assert_msg((gt), condition, "")
#define xe_gt_assert_msg(gt, condition, msg, arg...) ({						\
	const struct xe_gt *__gt = (gt);							\
	xe_tile_assert_msg(gt_to_tile(__gt), condition, "GT: %u type %d\n" msg,			\
			   __gt->info.id, __gt->info.type, ## arg);				\
})

#endif
