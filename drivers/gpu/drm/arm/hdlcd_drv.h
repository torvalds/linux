/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  ARM HDLCD Controller register definition
 */

#ifndef __HDLCD_DRV_H__
#define __HDLCD_DRV_H__

struct hdlcd_drm_private {
	struct drm_device		base;
	void __iomem			*mmio;
	struct clk			*clk;
	struct drm_crtc			crtc;
	struct drm_plane		*plane;
	unsigned int			irq;
#ifdef CONFIG_DEBUG_FS
	atomic_t buffer_underrun_count;
	atomic_t bus_error_count;
	atomic_t vsync_count;
	atomic_t dma_end_count;
#endif
};

#define drm_to_hdlcd_priv(x)	container_of(x, struct hdlcd_drm_private, base)
#define crtc_to_hdlcd_priv(x)	container_of(x, struct hdlcd_drm_private, crtc)

static inline void hdlcd_write(struct hdlcd_drm_private *hdlcd,
			       unsigned int reg, u32 value)
{
	writel(value, hdlcd->mmio + reg);
}

static inline u32 hdlcd_read(struct hdlcd_drm_private *hdlcd, unsigned int reg)
{
	return readl(hdlcd->mmio + reg);
}

int hdlcd_setup_crtc(struct drm_device *dev);
void hdlcd_set_scanout(struct hdlcd_drm_private *hdlcd);

#endif /* __HDLCD_DRV_H__ */
