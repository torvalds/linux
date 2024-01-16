#undef TRACE_SYSTEM
#define TRACE_SYSTEM tegra

#if !defined(DRM_TEGRA_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define DRM_TEGRA_TRACE_H 1

#include <linux/device.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(register_access,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value),
	TP_STRUCT__entry(
		__field(struct device *, dev)
		__field(unsigned int, offset)
		__field(u32, value)
	),
	TP_fast_assign(
		__entry->dev = dev;
		__entry->offset = offset;
		__entry->value = value;
	),
	TP_printk("%s %04x %08x", dev_name(__entry->dev), __entry->offset,
		  __entry->value)
);

DEFINE_EVENT(register_access, dc_writel,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));
DEFINE_EVENT(register_access, dc_readl,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));

DEFINE_EVENT(register_access, hdmi_writel,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));
DEFINE_EVENT(register_access, hdmi_readl,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));

DEFINE_EVENT(register_access, dsi_writel,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));
DEFINE_EVENT(register_access, dsi_readl,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));

DEFINE_EVENT(register_access, dpaux_writel,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));
DEFINE_EVENT(register_access, dpaux_readl,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));

DEFINE_EVENT(register_access, sor_writel,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));
DEFINE_EVENT(register_access, sor_readl,
	TP_PROTO(struct device *dev, unsigned int offset, u32 value),
	TP_ARGS(dev, offset, value));

#endif /* DRM_TEGRA_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/tegra
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
