/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef TEGRA_DRM_H
#define TEGRA_DRM_H 1

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fixed.h>

struct tegra_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_cma_object *obj;
};

static inline struct tegra_framebuffer *to_tegra_fb(struct drm_framebuffer *fb)
{
	return container_of(fb, struct tegra_framebuffer, base);
}

struct host1x {
	struct drm_device *drm;
	struct device *dev;
	void __iomem *regs;
	struct clk *clk;
	int syncpt;
	int irq;

	struct mutex drm_clients_lock;
	struct list_head drm_clients;
	struct list_head drm_active;

	struct mutex clients_lock;
	struct list_head clients;

	struct drm_fbdev_cma *fbdev;
	struct tegra_framebuffer fb;
};

struct host1x_client;

struct host1x_client_ops {
	int (*drm_init)(struct host1x_client *client, struct drm_device *drm);
	int (*drm_exit)(struct host1x_client *client);
};

struct host1x_client {
	struct host1x *host1x;
	struct device *dev;

	const struct host1x_client_ops *ops;

	struct list_head list;
};

extern int host1x_drm_init(struct host1x *host1x, struct drm_device *drm);
extern int host1x_drm_exit(struct host1x *host1x);

extern int host1x_register_client(struct host1x *host1x,
				  struct host1x_client *client);
extern int host1x_unregister_client(struct host1x *host1x,
				    struct host1x_client *client);

struct tegra_output;

struct tegra_dc {
	struct host1x_client client;

	struct host1x *host1x;
	struct device *dev;

	struct drm_crtc base;
	int pipe;

	struct clk *clk;

	void __iomem *regs;
	int irq;

	struct tegra_output *rgb;

	struct list_head list;

	struct drm_info_list *debugfs_files;
	struct drm_minor *minor;
	struct dentry *debugfs;
};

static inline struct tegra_dc *host1x_client_to_dc(struct host1x_client *client)
{
	return container_of(client, struct tegra_dc, client);
}

static inline struct tegra_dc *to_tegra_dc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct tegra_dc, base);
}

static inline void tegra_dc_writel(struct tegra_dc *dc, unsigned long value,
				   unsigned long reg)
{
	writel(value, dc->regs + (reg << 2));
}

static inline unsigned long tegra_dc_readl(struct tegra_dc *dc,
					   unsigned long reg)
{
	return readl(dc->regs + (reg << 2));
}

struct tegra_output_ops {
	int (*enable)(struct tegra_output *output);
	int (*disable)(struct tegra_output *output);
	int (*setup_clock)(struct tegra_output *output, struct clk *clk,
			   unsigned long pclk);
	int (*check_mode)(struct tegra_output *output,
			  struct drm_display_mode *mode,
			  enum drm_mode_status *status);
};

enum tegra_output_type {
	TEGRA_OUTPUT_RGB,
};

struct tegra_output {
	struct device_node *of_node;
	struct device *dev;

	const struct tegra_output_ops *ops;
	enum tegra_output_type type;

	struct i2c_adapter *ddc;
	const struct edid *edid;
	unsigned int hpd_irq;
	int hpd_gpio;

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

static inline int tegra_output_enable(struct tegra_output *output)
{
	if (output && output->ops && output->ops->enable)
		return output->ops->enable(output);

	return output ? -ENOSYS : -EINVAL;
}

static inline int tegra_output_disable(struct tegra_output *output)
{
	if (output && output->ops && output->ops->disable)
		return output->ops->disable(output);

	return output ? -ENOSYS : -EINVAL;
}

static inline int tegra_output_setup_clock(struct tegra_output *output,
					   struct clk *clk, unsigned long pclk)
{
	if (output && output->ops && output->ops->setup_clock)
		return output->ops->setup_clock(output, clk, pclk);

	return output ? -ENOSYS : -EINVAL;
}

static inline int tegra_output_check_mode(struct tegra_output *output,
					  struct drm_display_mode *mode,
					  enum drm_mode_status *status)
{
	if (output && output->ops && output->ops->check_mode)
		return output->ops->check_mode(output, mode, status);

	return output ? -ENOSYS : -EINVAL;
}

/* from rgb.c */
extern int tegra_dc_rgb_probe(struct tegra_dc *dc);
extern int tegra_dc_rgb_init(struct drm_device *drm, struct tegra_dc *dc);
extern int tegra_dc_rgb_exit(struct tegra_dc *dc);

/* from output.c */
extern int tegra_output_parse_dt(struct tegra_output *output);
extern int tegra_output_init(struct drm_device *drm, struct tegra_output *output);
extern int tegra_output_exit(struct tegra_output *output);

/* from gem.c */
extern struct tegra_gem_object *tegra_gem_alloc(struct drm_device *drm,
						size_t size);
extern int tegra_gem_handle_create(struct drm_device *drm,
				   struct drm_file *file, size_t size,
				   unsigned long flags, uint32_t *handle);
extern int tegra_gem_dumb_create(struct drm_file *file, struct drm_device *drm,
				 struct drm_mode_create_dumb *args);
extern int tegra_gem_dumb_map_offset(struct drm_file *file,
				     struct drm_device *drm, uint32_t handle,
				     uint64_t *offset);
extern int tegra_gem_dumb_destroy(struct drm_file *file,
				  struct drm_device *drm, uint32_t handle);
extern int tegra_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma);
extern int tegra_gem_init_object(struct drm_gem_object *obj);
extern void tegra_gem_free_object(struct drm_gem_object *obj);
extern struct vm_operations_struct tegra_gem_vm_ops;

/* from fb.c */
extern int tegra_drm_fb_init(struct drm_device *drm);
extern void tegra_drm_fb_exit(struct drm_device *drm);

extern struct platform_driver tegra_host1x_driver;
extern struct platform_driver tegra_dc_driver;
extern struct drm_driver tegra_drm_driver;

#endif /* TEGRA_DRM_H */
