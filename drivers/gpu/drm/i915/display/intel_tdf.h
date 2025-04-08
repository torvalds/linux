/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef __INTEL_TDF_H__
#define __INTEL_TDF_H__

/*
 * TDF (Transient-Data-Flush) is needed for Xe2+ where special L3:XD caching can
 * be enabled through various PAT index modes. Idea is to use this caching mode
 * when for example rendering onto the display surface, with the promise that
 * KMD will ensure transient cache entries are always flushed by the time we do
 * the display flip, since display engine is never coherent with CPU/GPU caches.
 */

struct intel_display;

#ifdef I915
static inline void intel_td_flush(struct intel_display *display) {}
#else
void intel_td_flush(struct intel_display *display);
#endif

#endif
