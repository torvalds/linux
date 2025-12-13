/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _xe_tile_printk_H_
#define _xe_tile_printk_H_

#include "xe_printk.h"

#define __XE_TILE_PRINTK_FMT(_tile, _fmt, _args...)	"Tile%u: " _fmt, (_tile)->id, ##_args

#define xe_tile_printk(_tile, _level, _fmt, ...) \
	xe_printk((_tile)->xe, _level, __XE_TILE_PRINTK_FMT((_tile), _fmt, ##__VA_ARGS__))

#define xe_tile_err(_tile, _fmt, ...) \
	xe_tile_printk((_tile), err, _fmt, ##__VA_ARGS__)

#define xe_tile_err_once(_tile, _fmt, ...) \
	xe_tile_printk((_tile), err_once, _fmt, ##__VA_ARGS__)

#define xe_tile_err_ratelimited(_tile, _fmt, ...) \
	xe_tile_printk((_tile), err_ratelimited, _fmt, ##__VA_ARGS__)

#define xe_tile_warn(_tile, _fmt, ...) \
	xe_tile_printk((_tile), warn, _fmt, ##__VA_ARGS__)

#define xe_tile_notice(_tile, _fmt, ...) \
	xe_tile_printk((_tile), notice, _fmt, ##__VA_ARGS__)

#define xe_tile_info(_tile, _fmt, ...) \
	xe_tile_printk((_tile), info, _fmt, ##__VA_ARGS__)

#define xe_tile_dbg(_tile, _fmt, ...) \
	xe_tile_printk((_tile), dbg, _fmt, ##__VA_ARGS__)

#define xe_tile_WARN_type(_tile, _type, _condition, _fmt, ...) \
	xe_WARN##_type((_tile)->xe, _condition, _fmt, ## __VA_ARGS__)

#define xe_tile_WARN(_tile, _condition, _fmt, ...) \
	xe_tile_WARN_type((_tile),, _condition, __XE_TILE_PRINTK_FMT((_tile), _fmt, ##__VA_ARGS__))

#define xe_tile_WARN_ONCE(_tile, _condition, _fmt, ...) \
	xe_tile_WARN_type((_tile), _ONCE, _condition, __XE_TILE_PRINTK_FMT((_tile), _fmt, ##__VA_ARGS__))

#define xe_tile_WARN_ON(_tile, _condition) \
	xe_tile_WARN((_tile), _condition, "%s(%s)", "WARN_ON", __stringify(_condition))

#define xe_tile_WARN_ON_ONCE(_tile, _condition) \
	xe_tile_WARN_ONCE((_tile), _condition, "%s(%s)", "WARN_ON_ONCE", __stringify(_condition))

static inline void __xe_tile_printfn_err(struct drm_printer *p, struct va_format *vaf)
{
	struct xe_tile *tile = p->arg;

	xe_tile_err(tile, "%pV", vaf);
}

static inline void __xe_tile_printfn_info(struct drm_printer *p, struct va_format *vaf)
{
	struct xe_tile *tile = p->arg;

	xe_tile_info(tile, "%pV", vaf);
}

static inline void __xe_tile_printfn_dbg(struct drm_printer *p, struct va_format *vaf)
{
	struct xe_tile *tile = p->arg;
	struct drm_printer dbg;

	/*
	 * The original xe_tile_dbg() callsite annotations are useless here,
	 * redirect to the tweaked xe_dbg_printer() instead.
	 */
	dbg = xe_dbg_printer(tile->xe);
	dbg.origin = p->origin;

	drm_printf(&dbg, __XE_TILE_PRINTK_FMT(tile, "%pV", vaf));
}

/**
 * xe_tile_err_printer - Construct a &drm_printer that outputs to xe_tile_err()
 * @tile: the &xe_tile pointer to use in xe_tile_err()
 *
 * Return: The &drm_printer object.
 */
static inline struct drm_printer xe_tile_err_printer(struct xe_tile *tile)
{
	struct drm_printer p = {
		.printfn = __xe_tile_printfn_err,
		.arg = tile,
	};
	return p;
}

/**
 * xe_tile_info_printer - Construct a &drm_printer that outputs to xe_tile_info()
 * @tile: the &xe_tile pointer to use in xe_tile_info()
 *
 * Return: The &drm_printer object.
 */
static inline struct drm_printer xe_tile_info_printer(struct xe_tile *tile)
{
	struct drm_printer p = {
		.printfn = __xe_tile_printfn_info,
		.arg = tile,
	};
	return p;
}

/**
 * xe_tile_dbg_printer - Construct a &drm_printer that outputs like xe_tile_dbg()
 * @tile: the &xe_tile pointer to use in xe_tile_dbg()
 *
 * Return: The &drm_printer object.
 */
static inline struct drm_printer xe_tile_dbg_printer(struct xe_tile *tile)
{
	struct drm_printer p = {
		.printfn = __xe_tile_printfn_dbg,
		.arg = tile,
		.origin = (const void *)_THIS_IP_,
	};
	return p;
}

#endif
