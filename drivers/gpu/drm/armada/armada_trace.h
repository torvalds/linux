/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(ARMADA_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define ARMADA_TRACE_H

#include <linux/tracepoint.h>
#include <drm/drmP.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM armada
#define TRACE_INCLUDE_FILE armada_trace

TRACE_EVENT(armada_drm_irq,
	TP_PROTO(struct drm_crtc *crtc, u32 stat),
	TP_ARGS(crtc, stat),
	TP_STRUCT__entry(
		__field(struct drm_crtc *, crtc)
		__field(u32, stat)
	),
	TP_fast_assign(
		__entry->crtc = crtc;
		__entry->stat = stat;
	),
	TP_printk("crtc %p stat 0x%08x",
		__entry->crtc, __entry->stat)
);

TRACE_EVENT(armada_ovl_plane_update,
	TP_PROTO(struct drm_plane *plane, struct drm_crtc *crtc,
		     struct drm_framebuffer *fb,
		     int crtc_x, int crtc_y, unsigned crtc_w, unsigned crtc_h,
		     uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h),
	TP_ARGS(plane, crtc, fb, crtc_x, crtc_y, crtc_w, crtc_h, src_x, src_y, src_w, src_h),
	TP_STRUCT__entry(
		__field(struct drm_plane *, plane)
		__field(struct drm_crtc *, crtc)
		__field(struct drm_framebuffer *, fb)
		__field(int, crtc_x)
		__field(int, crtc_y)
		__field(unsigned int, crtc_w)
		__field(unsigned int, crtc_h)
		__field(u32, src_x)
		__field(u32, src_y)
		__field(u32, src_w)
		__field(u32, src_h)
	),
	TP_fast_assign(
		__entry->plane = plane;
		__entry->crtc = crtc;
		__entry->fb = fb;
		__entry->crtc_x = crtc_x;
		__entry->crtc_y = crtc_y;
		__entry->crtc_w = crtc_w;
		__entry->crtc_h = crtc_h;
		__entry->src_x = src_x;
		__entry->src_y = src_y;
		__entry->src_w = src_w;
		__entry->src_h = src_h;
	),
	TP_printk("plane %p crtc %p fb %p crtc @ (%d,%d, %ux%u) src @ (%u,%u, %ux%u)",
		__entry->plane, __entry->crtc, __entry->fb,
		__entry->crtc_x, __entry->crtc_y,
		__entry->crtc_w, __entry->crtc_h,
		__entry->src_x >> 16, __entry->src_y >> 16,
		__entry->src_w >> 16, __entry->src_h >> 16)
);

TRACE_EVENT(armada_ovl_plane_work,
	TP_PROTO(struct drm_crtc *crtc, struct drm_plane *plane),
	TP_ARGS(crtc, plane),
	TP_STRUCT__entry(
		__field(struct drm_plane *, plane)
		__field(struct drm_crtc *, crtc)
	),
	TP_fast_assign(
		__entry->plane = plane;
		__entry->crtc = crtc;
	),
	TP_printk("plane %p crtc %p",
		__entry->plane, __entry->crtc)
);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/armada
#include <trace/define_trace.h>
