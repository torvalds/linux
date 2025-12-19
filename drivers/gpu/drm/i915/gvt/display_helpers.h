/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2025 Intel Corporation
 */

#ifndef __DISPLAY_HELPERS_H__
#define __DISPLAY_HELPERS_H__

#include "display/intel_gvt_api.h"

#ifdef DISPLAY_MMIO_BASE
#undef DISPLAY_MMIO_BASE
#endif
#define DISPLAY_MMIO_BASE(display) \
	intel_display_device_mmio_base((display))

#ifdef INTEL_DISPLAY_DEVICE_PIPE_OFFSET
#undef INTEL_DISPLAY_DEVICE_PIPE_OFFSET
#endif
/*
 * #FIXME:
 * TRANSCONF() uses pipe-based addressing via _MMIO_PIPE2().
 * Some GVT call sites pass enum transcoder instead of enum pipe.
 * Cast the argument to enum pipe for now since TRANSCODER_A..D map
 * 1:1 to PIPE_A..D.
 * TRANSCODER_EDP is an exception, the cast preserves the existing
 * behaviour but this needs to be handled later either by using the
 * correct pipe or by switching TRANSCONF() to use _MMIO_TRANS2().
 */
#define INTEL_DISPLAY_DEVICE_PIPE_OFFSET(display, idx) \
	intel_display_device_pipe_offset((display), (enum pipe)(idx))

#ifdef INTEL_DISPLAY_DEVICE_TRANS_OFFSET
#undef INTEL_DISPLAY_DEVICE_TRANS_OFFSET
#endif
#define INTEL_DISPLAY_DEVICE_TRANS_OFFSET(display, trans) \
	intel_display_device_trans_offset((display), (trans))

#ifdef INTEL_DISPLAY_DEVICE_CURSOR_OFFSET
#undef INTEL_DISPLAY_DEVICE_CURSOR_OFFSET
#endif
#define INTEL_DISPLAY_DEVICE_CURSOR_OFFSET(display, pipe) \
	intel_display_device_cursor_offset((display), (pipe))

#define gvt_for_each_pipe(display, __p) \
	for ((__p) = PIPE_A; (__p) < I915_MAX_PIPES; (__p)++) \
		for_each_if(intel_display_device_pipe_valid((display), (__p)))

#endif /* __DISPLAY_HELPERS_H__ */
