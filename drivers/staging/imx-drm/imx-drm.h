#ifndef _IMX_DRM_H_
#define _IMX_DRM_H_

struct imx_drm_crtc;
struct drm_fbdev_cma;

struct imx_drm_crtc_helper_funcs {
	int (*enable_vblank)(struct drm_crtc *crtc);
	void (*disable_vblank)(struct drm_crtc *crtc);
	int (*set_interface_pix_fmt)(struct drm_crtc *crtc, u32 encoder_type,
			u32 pix_fmt);
	const struct drm_crtc_helper_funcs *crtc_helper_funcs;
	const struct drm_crtc_funcs *crtc_funcs;
};

int imx_drm_add_crtc(struct drm_crtc *crtc,
		struct imx_drm_crtc **new_crtc,
		const struct imx_drm_crtc_helper_funcs *imx_helper_funcs,
		struct module *owner, void *cookie, int id);
int imx_drm_remove_crtc(struct imx_drm_crtc *);
int imx_drm_init_drm(struct platform_device *pdev,
		int preferred_bpp);
int imx_drm_exit_drm(void);

int imx_drm_crtc_vblank_get(struct imx_drm_crtc *imx_drm_crtc);
void imx_drm_crtc_vblank_put(struct imx_drm_crtc *imx_drm_crtc);
void imx_drm_handle_vblank(struct imx_drm_crtc *imx_drm_crtc);

struct imx_drm_encoder;
int imx_drm_add_encoder(struct drm_encoder *encoder,
		struct imx_drm_encoder **new_enc,
		struct module *owner);
int imx_drm_remove_encoder(struct imx_drm_encoder *);

struct imx_drm_connector;
int imx_drm_add_connector(struct drm_connector *connector,
		struct imx_drm_connector **new_con,
		struct module *owner);
int imx_drm_remove_connector(struct imx_drm_connector *);

void imx_drm_mode_config_init(struct drm_device *drm);

struct drm_gem_cma_object *imx_drm_fb_get_obj(struct drm_framebuffer *fb);

struct drm_device *imx_drm_device_get(void);
void imx_drm_device_put(void);
int imx_drm_crtc_panel_format(struct drm_crtc *crtc, u32 encoder_type,
		u32 interface_pix_fmt);
void imx_drm_fb_helper_set(struct drm_fbdev_cma *fbdev_helper);

struct device_node;

int imx_drm_encoder_get_mux_id(struct imx_drm_encoder *imx_drm_encoder,
		struct drm_crtc *crtc);
int imx_drm_encoder_add_possible_crtcs(struct imx_drm_encoder *imx_drm_encoder,
		struct device_node *np);

#endif /* _IMX_DRM_H_ */
