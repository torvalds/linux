// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include <linux/component.h>
#include <linux/interrupt.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_irq.h>
#include <drm/drm_vblank.h>

#include "komeda_dev.h"
#include "komeda_framebuffer.h"
#include "komeda_kms.h"

DEFINE_DRM_GEM_CMA_FOPS(komeda_cma_fops);

static int komeda_gem_cma_dumb_create(struct drm_file *file,
				      struct drm_device *dev,
				      struct drm_mode_create_dumb *args)
{
	struct komeda_dev *mdev = dev->dev_private;
	u32 pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	args->pitch = ALIGN(pitch, mdev->chip.bus_width);

	return drm_gem_cma_dumb_create_internal(file, dev, args);
}

static irqreturn_t komeda_kms_irq_handler(int irq, void *data)
{
	struct drm_device *drm = data;
	struct komeda_dev *mdev = drm->dev_private;
	struct komeda_kms_dev *kms = to_kdev(drm);
	struct komeda_events evts;
	irqreturn_t status;
	u32 i;

	/* Call into the CHIP to recognize events */
	memset(&evts, 0, sizeof(evts));
	status = mdev->funcs->irq_handler(mdev, &evts);

	/* Notify the crtc to handle the events */
	for (i = 0; i < kms->n_crtcs; i++)
		komeda_crtc_handle_event(&kms->crtcs[i], &evts);

	return status;
}

static struct drm_driver komeda_kms_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC |
			   DRIVER_PRIME | DRIVER_HAVE_IRQ,
	.lastclose			= drm_fb_helper_lastclose,
	.irq_handler			= komeda_kms_irq_handler,
	.gem_free_object_unlocked	= drm_gem_cma_free_object,
	.gem_vm_ops			= &drm_gem_cma_vm_ops,
	.dumb_create			= komeda_gem_cma_dumb_create,
	.prime_handle_to_fd		= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle		= drm_gem_prime_fd_to_handle,
	.gem_prime_export		= drm_gem_prime_export,
	.gem_prime_import		= drm_gem_prime_import,
	.gem_prime_get_sg_table		= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table	= drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap			= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap		= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap			= drm_gem_cma_prime_mmap,
	.fops = &komeda_cma_fops,
	.name = "komeda",
	.desc = "Arm Komeda Display Processor driver",
	.date = "20181101",
	.major = 0,
	.minor = 1,
};

static void komeda_kms_commit_tail(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;

	drm_atomic_helper_commit_modeset_disables(dev, old_state);

	drm_atomic_helper_commit_planes(dev, old_state, 0);

	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	drm_atomic_helper_wait_for_flip_done(dev, old_state);

	drm_atomic_helper_commit_hw_done(old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);
}

static const struct drm_mode_config_helper_funcs komeda_mode_config_helpers = {
	.atomic_commit_tail = komeda_kms_commit_tail,
};

static int komeda_kms_check(struct drm_device *dev,
			    struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_st, *new_crtc_st;
	int i, err;

	err = drm_atomic_helper_check_modeset(dev, state);
	if (err)
		return err;

	/* komeda need to re-calculate resource assumption in every commit
	 * so need to add all affected_planes (even unchanged) to
	 * drm_atomic_state.
	 */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_st, new_crtc_st, i) {
		err = drm_atomic_add_affected_planes(state, crtc);
		if (err)
			return err;
	}

	err = drm_atomic_helper_check_planes(dev, state);
	if (err)
		return err;

	return 0;
}

static const struct drm_mode_config_funcs komeda_mode_config_funcs = {
	.fb_create		= komeda_fb_create,
	.atomic_check		= komeda_kms_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

static void komeda_kms_mode_config_init(struct komeda_kms_dev *kms,
					struct komeda_dev *mdev)
{
	struct drm_mode_config *config = &kms->base.mode_config;

	drm_mode_config_init(&kms->base);

	komeda_kms_setup_crtcs(kms, mdev);

	/* Get value from dev */
	config->min_width	= 0;
	config->min_height	= 0;
	config->max_width	= 4096;
	config->max_height	= 4096;
	config->allow_fb_modifiers = false;

	config->funcs = &komeda_mode_config_funcs;
	config->helper_private = &komeda_mode_config_helpers;
}

struct komeda_kms_dev *komeda_kms_attach(struct komeda_dev *mdev)
{
	struct komeda_kms_dev *kms = kzalloc(sizeof(*kms), GFP_KERNEL);
	struct drm_device *drm;
	int err;

	if (!kms)
		return ERR_PTR(-ENOMEM);

	drm = &kms->base;
	err = drm_dev_init(drm, &komeda_kms_driver, mdev->dev);
	if (err)
		goto free_kms;

	drm->dev_private = mdev;

	komeda_kms_mode_config_init(kms, mdev);

	err = komeda_kms_add_private_objs(kms, mdev);
	if (err)
		goto cleanup_mode_config;

	err = komeda_kms_add_planes(kms, mdev);
	if (err)
		goto cleanup_mode_config;

	err = drm_vblank_init(drm, kms->n_crtcs);
	if (err)
		goto cleanup_mode_config;

	err = komeda_kms_add_crtcs(kms, mdev);
	if (err)
		goto cleanup_mode_config;

	err = component_bind_all(mdev->dev, kms);
	if (err)
		goto cleanup_mode_config;

	drm_mode_config_reset(drm);

	err = drm_irq_install(drm, mdev->irq);
	if (err)
		goto cleanup_mode_config;

	err = mdev->funcs->enable_irq(mdev);
	if (err)
		goto uninstall_irq;

	err = drm_dev_register(drm, 0);
	if (err)
		goto uninstall_irq;

	return kms;

uninstall_irq:
	drm_irq_uninstall(drm);
cleanup_mode_config:
	drm_mode_config_cleanup(drm);
	komeda_kms_cleanup_private_objs(kms);
free_kms:
	kfree(kms);
	return ERR_PTR(err);
}

void komeda_kms_detach(struct komeda_kms_dev *kms)
{
	struct drm_device *drm = &kms->base;
	struct komeda_dev *mdev = drm->dev_private;

	mdev->funcs->disable_irq(mdev);
	drm_dev_unregister(drm);
	drm_irq_uninstall(drm);
	component_unbind_all(mdev->dev, drm);
	komeda_kms_cleanup_private_objs(kms);
	drm_mode_config_cleanup(drm);
	drm->dev_private = NULL;
	drm_dev_put(drm);
}
