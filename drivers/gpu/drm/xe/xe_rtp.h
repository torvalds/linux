/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_RTP_
#define _XE_RTP_

#include <linux/types.h>
#include <linux/xarray.h>

#define _XE_RTP_INCLUDE_PRIVATE_HELPERS

#include "xe_rtp_helpers.h"
#include "xe_rtp_types.h"

#undef _XE_RTP_INCLUDE_PRIVATE_HELPERS

/*
 * Register table poke infrastructure
 */

struct xe_hw_engine;
struct xe_gt;
struct xe_reg_sr;

/*
 * Macros to encode rules to match against platform, IP version, stepping, etc.
 * Shouldn't be used directly - see XE_RTP_RULES()
 */
#define _XE_RTP_RULE_PLATFORM(plat__)						\
	{ .match_type = XE_RTP_MATCH_PLATFORM, .platform = plat__ }

#define _XE_RTP_RULE_SUBPLATFORM(plat__, sub__)					\
	{ .match_type = XE_RTP_MATCH_SUBPLATFORM,				\
	  .platform = plat__, .subplatform = sub__ }

#define _XE_RTP_RULE_GRAPHICS_STEP(start__, end__)				\
	{ .match_type = XE_RTP_MATCH_GRAPHICS_STEP,				\
	  .step_start = start__, .step_end = end__ }

#define _XE_RTP_RULE_MEDIA_STEP(start__, end__)					\
	{ .match_type = XE_RTP_MATCH_MEDIA_STEP,				\
	  .step_start = start__, .step_end = end__ }

#define _XE_RTP_RULE_ENGINE_CLASS(cls__)					\
	{ .match_type = XE_RTP_MATCH_ENGINE_CLASS,				\
	  .engine_class = (cls__) }

/**
 * XE_RTP_RULE_PLATFORM - Create rule matching platform
 * @plat_: platform to match
 *
 * Refer to XE_RTP_RULES() for expected usage.
 */
#define XE_RTP_RULE_PLATFORM(plat_)						\
	_XE_RTP_RULE_PLATFORM(XE_##plat_)

/**
 * XE_RTP_RULE_SUBPLATFORM - Create rule matching platform and sub-platform
 * @plat_: platform to match
 * @sub_: sub-platform to match
 *
 * Refer to XE_RTP_RULES() for expected usage.
 */
#define XE_RTP_RULE_SUBPLATFORM(plat_, sub_)					\
	_XE_RTP_RULE_SUBPLATFORM(XE_##plat_, XE_SUBPLATFORM_##plat_##_##sub_)

/**
 * XE_RTP_RULE_GRAPHICS_STEP - Create rule matching graphics stepping
 * @start_: First stepping matching the rule
 * @end_: First stepping that does not match the rule
 *
 * Note that the range matching this rule is [ @start_, @end_ ), i.e. inclusive
 * on the left, exclusive on the right.
 *
 * Refer to XE_RTP_RULES() for expected usage.
 */
#define XE_RTP_RULE_GRAPHICS_STEP(start_, end_)					\
	_XE_RTP_RULE_GRAPHICS_STEP(STEP_##start_, STEP_##end_)

/**
 * XE_RTP_RULE_MEDIA_STEP - Create rule matching media stepping
 * @start_: First stepping matching the rule
 * @end_: First stepping that does not match the rule
 *
 * Note that the range matching this rule is [ @start_, @end_ ), i.e. inclusive
 * on the left, exclusive on the right.
 *
 * Refer to XE_RTP_RULES() for expected usage.
 */
#define XE_RTP_RULE_MEDIA_STEP(start_, end_)					\
	_XE_RTP_RULE_MEDIA_STEP(STEP_##start_, STEP_##end_)

/**
 * XE_RTP_RULE_ENGINE_CLASS - Create rule matching an engine class
 * @cls_: Engine class to match
 *
 * Refer to XE_RTP_RULES() for expected usage.
 */
#define XE_RTP_RULE_ENGINE_CLASS(cls_)						\
	_XE_RTP_RULE_ENGINE_CLASS(XE_ENGINE_CLASS_##cls_)

/**
 * XE_RTP_RULE_FUNC - Create rule using callback function for match
 * @func__: Function to call to decide if rule matches
 *
 * This allows more complex checks to be performed. The ``XE_RTP``
 * infrastructure will simply call the function @func_ passed to decide if this
 * rule matches the device.
 *
 * Refer to XE_RTP_RULES() for expected usage.
 */
#define XE_RTP_RULE_FUNC(func__)						\
	{ .match_type = XE_RTP_MATCH_FUNC,					\
	  .match_func = (func__) }

/**
 * XE_RTP_RULE_GRAPHICS_VERSION - Create rule matching graphics version
 * @ver__: Graphics IP version to match
 *
 * Refer to XE_RTP_RULES() for expected usage.
 */
#define XE_RTP_RULE_GRAPHICS_VERSION(ver__)					\
	{ .match_type = XE_RTP_MATCH_GRAPHICS_VERSION,				\
	  .ver_start = ver__, }

/**
 * XE_RTP_RULE_GRAPHICS_VERSION_RANGE - Create rule matching a range of graphics version
 * @ver_start__: First graphics IP version to match
 * @ver_end__: Last graphics IP version to match
 *
 * Note that the range matching this rule is [ @ver_start__, @ver_end__ ], i.e.
 * inclusive on boths sides
 *
 * Refer to XE_RTP_RULES() for expected usage.
 */
#define XE_RTP_RULE_GRAPHICS_VERSION_RANGE(ver_start__, ver_end__)		\
	{ .match_type = XE_RTP_MATCH_GRAPHICS_VERSION_RANGE,			\
	  .ver_start = ver_start__, .ver_end = ver_end__, }

/**
 * XE_RTP_RULE_MEDIA_VERSION - Create rule matching media version
 * @ver__: Graphics IP version to match
 *
 * Refer to XE_RTP_RULES() for expected usage.
 */
#define XE_RTP_RULE_MEDIA_VERSION(ver__)					\
	{ .match_type = XE_RTP_MATCH_MEDIA_VERSION,				\
	  .ver_start = ver__, }

/**
 * XE_RTP_RULE_MEDIA_VERSION_RANGE - Create rule matching a range of media version
 * @ver_start__: First media IP version to match
 * @ver_end__: Last media IP version to match
 *
 * Note that the range matching this rule is [ @ver_start__, @ver_end__ ], i.e.
 * inclusive on boths sides
 *
 * Refer to XE_RTP_RULES() for expected usage.
 */
#define XE_RTP_RULE_MEDIA_VERSION_RANGE(ver_start__, ver_end__)			\
	{ .match_type = XE_RTP_MATCH_MEDIA_VERSION_RANGE,			\
	  .ver_start = ver_start__, .ver_end = ver_end__, }

/**
 * XE_RTP_RULE_IS_INTEGRATED - Create a rule matching integrated graphics devices
 *
 * Refer to XE_RTP_RULES() for expected usage.
 */
#define XE_RTP_RULE_IS_INTEGRATED						\
	{ .match_type = XE_RTP_MATCH_INTEGRATED }

/**
 * XE_RTP_RULE_IS_DISCRETE - Create a rule matching discrete graphics devices
 *
 * Refer to XE_RTP_RULES() for expected usage.
 */
#define XE_RTP_RULE_IS_DISCRETE							\
	{ .match_type = XE_RTP_MATCH_DISCRETE }

/**
 * XE_RTP_ACTION_WR - Helper to write a value to the register, overriding all
 *                    the bits
 * @reg_: Register
 * @val_: Value to set
 * @...: Additional fields to override in the struct xe_rtp_action entry
 *
 * The correspondent notation in bspec is:
 *
 *	REGNAME = VALUE
 */
#define XE_RTP_ACTION_WR(reg_, val_, ...)					\
	{ .reg = XE_RTP_DROP_CAST(reg_),					\
	  .clr_bits = ~0u, .set_bits = (val_),					\
	  .read_mask = (~0u), ##__VA_ARGS__ }

/**
 * XE_RTP_ACTION_SET - Set bits from @val_ in the register.
 * @reg_: Register
 * @val_: Bits to set in the register
 * @...: Additional fields to override in the struct xe_rtp_action entry
 *
 * For masked registers this translates to a single write, while for other
 * registers it's a RMW. The correspondent bspec notation is (example for bits 2
 * and 5, but could be any):
 *
 *	REGNAME[2] = 1
 *	REGNAME[5] = 1
 */
#define XE_RTP_ACTION_SET(reg_, val_, ...)					\
	{ .reg = XE_RTP_DROP_CAST(reg_),					\
	  .clr_bits = val_, .set_bits = val_,					\
	  .read_mask = val_, ##__VA_ARGS__ }

/**
 * XE_RTP_ACTION_CLR: Clear bits from @val_ in the register.
 * @reg_: Register
 * @val_: Bits to clear in the register
 * @...: Additional fields to override in the struct xe_rtp_action entry
 *
 * For masked registers this translates to a single write, while for other
 * registers it's a RMW. The correspondent bspec notation is (example for bits 2
 * and 5, but could be any):
 *
 *	REGNAME[2] = 0
 *	REGNAME[5] = 0
 */
#define XE_RTP_ACTION_CLR(reg_, val_, ...)					\
	{ .reg = XE_RTP_DROP_CAST(reg_),					\
	  .clr_bits = val_, .set_bits = 0,					\
	  .read_mask = val_, ##__VA_ARGS__ }

/**
 * XE_RTP_ACTION_FIELD_SET: Set a bit range
 * @reg_: Register
 * @mask_bits_: Mask of bits to be changed in the register, forming a field
 * @val_: Value to set in the field denoted by @mask_bits_
 * @...: Additional fields to override in the struct xe_rtp_action entry
 *
 * For masked registers this translates to a single write, while for other
 * registers it's a RMW. The correspondent bspec notation is:
 *
 *	REGNAME[<end>:<start>] = VALUE
 */
#define XE_RTP_ACTION_FIELD_SET(reg_, mask_bits_, val_, ...)			\
	{ .reg = XE_RTP_DROP_CAST(reg_),					\
	  .clr_bits = mask_bits_, .set_bits = val_,				\
	  .read_mask = mask_bits_, ##__VA_ARGS__ }

#define XE_RTP_ACTION_FIELD_SET_NO_READ_MASK(reg_, mask_bits_, val_, ...)	\
	{ .reg = XE_RTP_DROP_CAST(reg_),					\
	  .clr_bits = (mask_bits_), .set_bits = (val_),				\
	  .read_mask = 0, ##__VA_ARGS__ }

/**
 * XE_RTP_ACTION_WHITELIST - Add register to userspace whitelist
 * @reg_: Register
 * @val_: Whitelist-specific flags to set
 * @...: Additional fields to override in the struct xe_rtp_action entry
 *
 * Add a register to the whitelist, allowing userspace to modify the ster with
 * regular user privileges.
 */
#define XE_RTP_ACTION_WHITELIST(reg_, val_, ...)				\
	/* TODO fail build if ((flags) & ~(RING_FORCE_TO_NONPRIV_MASK_VALID)) */\
	{ .reg = XE_RTP_DROP_CAST(reg_),					\
	  .set_bits = val_,							\
	  .clr_bits = RING_FORCE_TO_NONPRIV_MASK_VALID,				\
	  ##__VA_ARGS__ }

/**
 * XE_RTP_NAME - Helper to set the name in xe_rtp_entry
 * @s_: Name describing this rule, often a HW-specific number
 *
 * TODO: maybe move this behind a debug config?
 */
#define XE_RTP_NAME(s_)	.name = (s_)

/**
 * XE_RTP_ENTRY_FLAG - Helper to add multiple flags to a struct xe_rtp_entry_sr
 * @...: Entry flags, without the ``XE_RTP_ENTRY_FLAG_`` prefix
 *
 * Helper to automatically add a ``XE_RTP_ENTRY_FLAG_`` prefix to the flags
 * when defining struct xe_rtp_entry entries. Example:
 *
 * .. code-block:: c
 *
 *	const struct xe_rtp_entry_sr wa_entries[] = {
 *		...
 *		{ XE_RTP_NAME("test-entry"),
 *		  ...
 *		  XE_RTP_ENTRY_FLAG(FOREACH_ENGINE),
 *		  ...
 *		},
 *		...
 *	};
 */
#define XE_RTP_ENTRY_FLAG(...)							\
	.flags = (XE_RTP_PASTE_FOREACH(ENTRY_FLAG_, BITWISE_OR, (__VA_ARGS__)))

/**
 * XE_RTP_ACTION_FLAG - Helper to add multiple flags to a struct xe_rtp_action
 * @...: Action flags, without the ``XE_RTP_ACTION_FLAG_`` prefix
 *
 * Helper to automatically add a ``XE_RTP_ACTION_FLAG_`` prefix to the flags
 * when defining struct xe_rtp_action entries. Example:
 *
 * .. code-block:: c
 *
 *	const struct xe_rtp_entry_sr wa_entries[] = {
 *		...
 *		{ XE_RTP_NAME("test-entry"),
 *		  ...
 *		  XE_RTP_ACTION_SET(..., XE_RTP_ACTION_FLAG(FOREACH_ENGINE)),
 *		  ...
 *		},
 *		...
 *	};
 */
#define XE_RTP_ACTION_FLAG(...)							\
	.flags = (XE_RTP_PASTE_FOREACH(ACTION_FLAG_, BITWISE_OR, (__VA_ARGS__)))

/**
 * XE_RTP_RULES - Helper to set multiple rules to a struct xe_rtp_entry_sr entry
 * @...: Rules
 *
 * At least one rule is needed and up to 4 are supported. Multiple rules are
 * AND'ed together, i.e. all the rules must evaluate to true for the entry to
 * be processed. See XE_RTP_MATCH_* for the possible match rules. Example:
 *
 * .. code-block:: c
 *
 *	const struct xe_rtp_entry_sr wa_entries[] = {
 *		...
 *		{ XE_RTP_NAME("test-entry"),
 *		  XE_RTP_RULES(SUBPLATFORM(DG2, G10), GRAPHICS_STEP(A0, B0)),
 *		  ...
 *		},
 *		...
 *	};
 */
#define XE_RTP_RULES(...)							\
	.n_rules = _XE_COUNT_ARGS(__VA_ARGS__),					\
	.rules = (const struct xe_rtp_rule[]) {					\
		XE_RTP_PASTE_FOREACH(RULE_, COMMA, (__VA_ARGS__))	\
	}

/**
 * XE_RTP_ACTIONS - Helper to set multiple actions to a struct xe_rtp_entry_sr
 * @...: Actions to be taken
 *
 * At least one action is needed and up to 4 are supported. See XE_RTP_ACTION_*
 * for the possible actions. Example:
 *
 * .. code-block:: c
 *
 *	const struct xe_rtp_entry_sr wa_entries[] = {
 *		...
 *		{ XE_RTP_NAME("test-entry"),
 *		  XE_RTP_RULES(...),
 *		  XE_RTP_ACTIONS(SET(..), SET(...), CLR(...)),
 *		  ...
 *		},
 *		...
 *	};
 */
#define XE_RTP_ACTIONS(...)							\
	.n_actions = _XE_COUNT_ARGS(__VA_ARGS__),				\
	.actions = (const struct xe_rtp_action[]) {				\
		XE_RTP_PASTE_FOREACH(ACTION_, COMMA, (__VA_ARGS__))	\
	}

#define XE_RTP_PROCESS_CTX_INITIALIZER(arg__) _Generic((arg__),							\
	struct xe_hw_engine * :	(struct xe_rtp_process_ctx){ { (void *)(arg__) }, XE_RTP_PROCESS_TYPE_ENGINE },	\
	struct xe_gt * :	(struct xe_rtp_process_ctx){ { (void *)(arg__) }, XE_RTP_PROCESS_TYPE_GT })

void xe_rtp_process_ctx_enable_active_tracking(struct xe_rtp_process_ctx *ctx,
					       unsigned long *active_entries,
					       size_t n_entries);

void xe_rtp_process_to_sr(struct xe_rtp_process_ctx *ctx,
			  const struct xe_rtp_entry_sr *entries,
			  struct xe_reg_sr *sr);

void xe_rtp_process(struct xe_rtp_process_ctx *ctx,
		    const struct xe_rtp_entry *entries);

/* Match functions to be used with XE_RTP_MATCH_FUNC */

/**
 * xe_rtp_match_even_instance - Match if engine instance is even
 * @gt: GT structure
 * @hwe: Engine instance
 *
 * Returns: true if engine instance is even, false otherwise
 */
bool xe_rtp_match_even_instance(const struct xe_gt *gt,
				const struct xe_hw_engine *hwe);

/*
 * xe_rtp_match_first_render_or_compute - Match if it's first render or compute
 * engine in the GT
 *
 * @gt: GT structure
 * @hwe: Engine instance
 *
 * Registers on the render reset domain need to have their values re-applied
 * when any of those engines are reset. Since the engines reset together, a
 * programming can be set to just one of them. For simplicity the first engine
 * of either render or compute class can be chosen.
 *
 * Returns: true if engine id is the first to match the render reset domain,
 * false otherwise.
 */
bool xe_rtp_match_first_render_or_compute(const struct xe_gt *gt,
					  const struct xe_hw_engine *hwe);

/*
 * xe_rtp_match_first_gslice_fused_off - Match when first gslice is fused off
 *
 * @gt: GT structure
 * @hwe: Engine instance
 *
 * Returns: true if first gslice is fused off, false otherwise.
 */
bool xe_rtp_match_first_gslice_fused_off(const struct xe_gt *gt,
					 const struct xe_hw_engine *hwe);

#endif
