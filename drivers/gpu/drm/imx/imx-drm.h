/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _IMX_DRM_H_
#define _IMX_DRM_H_

struct device_node;
struct drm_crtc;
struct drm_connector;
struct drm_device;
struct drm_display_mode;
struct drm_encoder;
struct drm_framebuffer;
struct drm_plane;
struct platform_device;

struct imx_crtc_state {
	struct drm_crtc_state			base;
	u32					bus_format;
	u32					bus_flags;
	int					di_hsync_pin;
	int					di_vsync_pin;
};

static inline struct imx_crtc_state *to_imx_crtc_state(struct drm_crtc_state *s)
{
	return container_of(s, struct imx_crtc_state, base);
}
int imx_drm_init_drm(struct platform_device *pdev,
		int preferred_bpp);
int imx_drm_exit_drm(void);

extern struct platform_driver ipu_drm_driver;

void imx_drm_mode_config_init(struct drm_device *drm);

struct drm_gem_cma_object *imx_drm_fb_get_obj(struct drm_framebuffer *fb);

int imx_drm_encoder_parse_of(struct drm_device *drm,
	struct drm_encoder *encoder, struct device_node *np);

void imx_drm_connector_destroy(struct drm_connector *connector);
void imx_drm_encoder_destroy(struct drm_encoder *encoder);

int ipu_planes_assign_pre(struct drm_device *dev,
			  struct drm_atomic_state *state);

#endif /* _IMX_DRM_H_ */
