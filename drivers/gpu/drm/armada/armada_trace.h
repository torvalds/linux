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
	),
	TP_fast_assign(
		__entry->plane = plane;
		__entry->crtc = crtc;
		__entry->fb = fb;
	),
	TP_printk("plane %p crtc %p fb %p",
		__entry->plane, __entry->crtc, __entry->fb)
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
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>
