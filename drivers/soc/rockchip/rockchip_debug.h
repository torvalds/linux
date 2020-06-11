/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */

/* Copyright (c) 2020 Fuzhou Rockchip Electronics Co., Ltd */

#ifndef _ROCKCHIP_DEBUG
#define _ROCKCHIP_DEBUG

struct fiq_debugger_output;

#if IS_ENABLED(CONFIG_FIQ_DEBUGGER)
int rockchip_debug_dump_pcsr(struct fiq_debugger_output *output);
#endif

#endif
