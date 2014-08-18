#ifndef _IMX_DRM_H_
#define _IMX_DRM_H_

struct device_node;
struct drm_crtc;
struct drm_connector;
struct drm_device;
struct drm_display_mode;
struct drm_encoder;
struct drm_fbdev_cma;
struct drm_framebuffer;
struct imx_drm_crtc;
struct platform_device;

int imx_drm_crtc_id(struct imx_drm_crtc *crtc);

struct imx_drm_crtc_helper_funcs {
	int (*enable_vblank)(struct drm_crtc *crtc);
	void (*disable_vblank)(struct drm_crtc *crtc);
	int (*set_interface_pix_fmt)(struct drm_crtc *crtc, u32 encoder_type,
			u32 pix_fmt, int hsync_pin, int vsync_pin);
	const struct drm_crtc_helper_funcs *crtc_helper_funcs;
	const struct drm_crtc_funcs *crtc_funcs;
};

int imx_drm_add_crtc(struct drm_device *drm, struct drm_crtc *crtc,
		struct imx_drm_crtc **new_crtc,
		const struct imx_drm_crtc_helper_funcs *imx_helper_funcs,
		struct device_node *port);
int imx_drm_remove_crtc(struct imx_drm_crtc *);
int imx_drm_init_drm(struct platform_device *pdev,
		int preferred_bpp);
int imx_drm_exit_drm(void);

int imx_drm_crtc_vblank_get(struct imx_drm_crtc *imx_drm_crtc);
void imx_drm_crtc_vblank_put(struct imx_drm_crtc *imx_drm_crtc);
void imx_drm_handle_vblank(struct imx_drm_crtc *imx_drm_crtc);

void imx_drm_mode_config_init(struct drm_device *drm);

struct drm_gem_cma_object *imx_drm_fb_get_obj(struct drm_framebuffer *fb);

int imx_drm_panel_format_pins(struct drm_encoder *encoder,
		u32 interface_pix_fmt, int hsync_pin, int vsync_pin);
int imx_drm_panel_format(struct drm_encoder *encoder,
		u32 interface_pix_fmt);

int imx_drm_encoder_get_mux_id(struct device_node *node,
		struct drm_encoder *encoder);
int imx_drm_encoder_parse_of(struct drm_device *drm,
	struct drm_encoder *encoder, struct device_node *np);

void imx_drm_connector_destroy(struct drm_connector *connector);
void imx_drm_encoder_destroy(struct drm_encoder *encoder);

#endif /* _IMX_DRM_H_ */
