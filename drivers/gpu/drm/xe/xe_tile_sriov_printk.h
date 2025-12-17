/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_TILE_SRIOV_PRINTK_H_
#define _XE_TILE_SRIOV_PRINTK_H_

#include "xe_tile_printk.h"
#include "xe_sriov_printk.h"

#define __XE_TILE_SRIOV_PRINTK_FMT(_tile, _fmt, ...) \
	__XE_TILE_PRINTK_FMT((_tile), _fmt, ##__VA_ARGS__)

#define xe_tile_sriov_printk(_tile, _level, _fmt, ...) \
	xe_sriov_##_level((_tile)->xe, __XE_TILE_SRIOV_PRINTK_FMT((_tile), _fmt, ##__VA_ARGS__))

#define xe_tile_sriov_err(_tile, _fmt, ...) \
	xe_tile_sriov_printk(_tile, err, _fmt, ##__VA_ARGS__)

#define xe_tile_sriov_notice(_tile, _fmt, ...) \
	xe_tile_sriov_printk(_tile, notice, _fmt, ##__VA_ARGS__)

#define xe_tile_sriov_info(_tile, _fmt, ...) \
	xe_tile_sriov_printk(_tile, info, _fmt, ##__VA_ARGS__)

#define xe_tile_sriov_dbg(_tile, _fmt, ...) \
	xe_tile_sriov_printk(_tile, dbg, _fmt, ##__VA_ARGS__)

#define xe_tile_sriov_dbg_verbose(_tile, _fmt, ...) \
	xe_tile_sriov_printk(_tile, dbg_verbose, _fmt, ##__VA_ARGS__)

#endif
