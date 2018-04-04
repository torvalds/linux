/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012-2013 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef HOST1X_DRM_H
#define HOST1X_DRM_H 1

#include <uapi/drm/tegra_drm.h>
#include <linux/host1x.h>
#include <linux/iova.h>
#include <linux/of_gpio.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fixed.h>

#include "gem.h"
#include "hub.h"
#include "trace.h"

struct reset_control;

struct tegra_fb {
	struct drm_framebuffer base;
	struct tegra_bo **planes;
	unsigned int num_planes;
};

#ifdef CONFIG_DRM_FBDEV_EMULATION
struct tegra_fbdev {
	struct drm_fb_helper base;
	struct tegra_fb *fb;
};
#endif

struct tegra_atomic_state {
	struct drm_atomic_state base;

	struct clk *clk_disp;
	struct tegra_dc *dc;
	unsigned long rate;
};

static inline struct tegra_atomic_state *
to_tegra_atomic_state(struct drm_atomic_state *state)
{
	return container_of(state, struct tegra_atomic_state, base);
}

struct tegra_drm {
	struct drm_device *drm;

	struct iommu_domain *domain;
	struct iommu_group *group;
	struct mutex mm_lock;
	struct drm_mm mm;

	struct {
		struct iova_domain domain;
		unsigned long shift;
		unsigned long limit;
	} carveout;

	struct mutex clients_lock;
	struct list_head clients;

#ifdef CONFIG_DRM_FBDEV_EMULATION
	struct tegra_fbdev *fbdev;
#endif

	unsigned int pitch_align;

	struct tegra_display_hub *hub;

	struct drm_atomic_state *state;
};

struct tegra_drm_client;

struct tegra_drm_context {
	struct tegra_drm_client *client;
	struct host1x_channel *channel;
	unsigned int id;
};

struct tegra_drm_client_ops {
	int (*open_channel)(struct tegra_drm_client *client,
			    struct tegra_drm_context *context);
	void (*close_channel)(struct tegra_drm_context *context);
	int (*is_addr_reg)(struct device *dev, u32 class, u32 offset);
	int (*is_valid_class)(u32 class);
	int (*submit)(struct tegra_drm_context *context,
		      struct drm_tegra_submit *args, struct drm_device *drm,
		      struct drm_file *file);
};

int tegra_drm_submit(struct tegra_drm_context *context,
		     struct drm_tegra_submit *args, struct drm_device *drm,
		     struct drm_file *file);

struct tegra_drm_client {
	struct host1x_client base;
	struct list_head list;

	const struct tegra_drm_client_ops *ops;
};

static inline struct tegra_drm_client *
host1x_to_drm_client(struct host1x_client *client)
{
	return container_of(client, struct tegra_drm_client, base);
}

int tegra_drm_register_client(struct tegra_drm *tegra,
			      struct tegra_drm_client *client);
int tegra_drm_unregister_client(struct tegra_drm *tegra,
				struct tegra_drm_client *client);

int tegra_drm_init(struct tegra_drm *tegra, struct drm_device *drm);
int tegra_drm_exit(struct tegra_drm *tegra);

void *tegra_drm_alloc(struct tegra_drm *tegra, size_t size, dma_addr_t *iova);
void tegra_drm_free(struct tegra_drm *tegra, size_t size, void *virt,
		    dma_addr_t iova);

struct cec_notifier;

struct tegra_output {
	struct device_node *of_node;
	struct device *dev;

	struct drm_panel *panel;
	struct i2c_adapter *ddc;
	const struct edid *edid;
	struct cec_notifier *notifier;
	unsigned int hpd_irq;
	int hpd_gpio;
	enum of_gpio_flags hpd_gpio_flags;

	struct drm_encoder encoder;
	struct drm_connector connector;
};

static inline struct tegra_output *encoder_to_output(struct drm_encoder *e)
{
	return container_of(e, struct tegra_output, encoder);
}

static inline struct tegra_output *connector_to_output(struct drm_connector *c)
{
	return container_of(c, struct tegra_output, connector);
}

/* from output.c */
int tegra_output_probe(struct tegra_output *output);
void tegra_output_remove(struct tegra_output *output);
int tegra_output_init(struct drm_device *drm, struct tegra_output *output);
void tegra_output_exit(struct tegra_output *output);
void tegra_output_find_possible_crtcs(struct tegra_output *output,
				      struct drm_device *drm);

int tegra_output_connector_get_modes(struct drm_connector *connector);
enum drm_connector_status
tegra_output_connector_detect(struct drm_connector *connector, bool force);
void tegra_output_connector_destroy(struct drm_connector *connector);

void tegra_output_encoder_destroy(struct drm_encoder *encoder);

/* from dpaux.c */
struct drm_dp_link;

struct drm_dp_aux *drm_dp_aux_find_by_of_node(struct device_node *np);
enum drm_connector_status drm_dp_aux_detect(struct drm_dp_aux *aux);
int drm_dp_aux_attach(struct drm_dp_aux *aux, struct tegra_output *output);
int drm_dp_aux_detach(struct drm_dp_aux *aux);
int drm_dp_aux_enable(struct drm_dp_aux *aux);
int drm_dp_aux_disable(struct drm_dp_aux *aux);
int drm_dp_aux_prepare(struct drm_dp_aux *aux, u8 encoding);
int drm_dp_aux_train(struct drm_dp_aux *aux, struct drm_dp_link *link,
		     u8 pattern);

/* from fb.c */
struct tegra_bo *tegra_fb_get_plane(struct drm_framebuffer *framebuffer,
				    unsigned int index);
bool tegra_fb_is_bottom_up(struct drm_framebuffer *framebuffer);
int tegra_fb_get_tiling(struct drm_framebuffer *framebuffer,
			struct tegra_bo_tiling *tiling);
struct drm_framebuffer *tegra_fb_create(struct drm_device *drm,
					struct drm_file *file,
					const struct drm_mode_fb_cmd2 *cmd);
int tegra_drm_fb_prepare(struct drm_device *drm);
void tegra_drm_fb_free(struct drm_device *drm);
int tegra_drm_fb_init(struct drm_device *drm);
void tegra_drm_fb_exit(struct drm_device *drm);
void tegra_drm_fb_suspend(struct drm_device *drm);
void tegra_drm_fb_resume(struct drm_device *drm);

extern struct platform_driver tegra_display_hub_driver;
extern struct platform_driver tegra_dc_driver;
extern struct platform_driver tegra_hdmi_driver;
extern struct platform_driver tegra_dsi_driver;
extern struct platform_driver tegra_dpaux_driver;
extern struct platform_driver tegra_sor_driver;
extern struct platform_driver tegra_gr2d_driver;
extern struct platform_driver tegra_gr3d_driver;
extern struct platform_driver tegra_vic_driver;

#endif /* HOST1X_DRM_H */
