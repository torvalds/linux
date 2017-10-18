#ifndef __IPUV3_PLANE_H__
#define __IPUV3_PLANE_H__

#include <drm/drm_crtc.h> /* drm_plane */

struct drm_plane;
struct drm_device;
struct ipu_soc;
struct drm_crtc;
struct drm_framebuffer;

struct ipuv3_channel;
struct dmfc_channel;
struct ipu_dp;

struct ipu_plane {
	struct drm_plane	base;

	struct ipu_soc		*ipu;
	struct ipuv3_channel	*ipu_ch;
	struct ipuv3_channel	*alpha_ch;
	struct dmfc_channel	*dmfc;
	struct ipu_dp		*dp;

	int			dma;
	int			dp_flow;

	bool			disabling;
};

struct ipu_plane *ipu_plane_init(struct drm_device *dev, struct ipu_soc *ipu,
				 int dma, int dp, unsigned int possible_crtcs,
				 enum drm_plane_type type);

/* Init IDMAC, DMFC, DP */
int ipu_plane_mode_set(struct ipu_plane *plane, struct drm_crtc *crtc,
		       struct drm_display_mode *mode,
		       struct drm_framebuffer *fb, int crtc_x, int crtc_y,
		       unsigned int crtc_w, unsigned int crtc_h,
		       uint32_t src_x, uint32_t src_y, uint32_t src_w,
		       uint32_t src_h, bool interlaced);

int ipu_plane_get_resources(struct ipu_plane *plane);
void ipu_plane_put_resources(struct ipu_plane *plane);

int ipu_plane_irq(struct ipu_plane *plane);

void ipu_plane_disable(struct ipu_plane *ipu_plane, bool disable_dp_channel);
void ipu_plane_disable_deferred(struct drm_plane *plane);

#endif
