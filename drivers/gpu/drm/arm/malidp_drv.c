/*
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
 * Author: Liviu Dudau <Liviu.Dudau@arm.com>
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * ARM Mali DP500/DP550/DP650 KMS/DRM driver
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_runtime.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_of.h>

#include "malidp_drv.h"
#include "malidp_regs.h"
#include "malidp_hw.h"

#define MALIDP_CONF_VALID_TIMEOUT	250

static void malidp_write_gamma_table(struct malidp_hw_device *hwdev,
				     u32 data[MALIDP_COEFFTAB_NUM_COEFFS])
{
	int i;
	/* Update all channels with a single gamma curve. */
	const u32 gamma_write_mask = GENMASK(18, 16);
	/*
	 * Always write an entire table, so the address field in
	 * DE_COEFFTAB_ADDR is 0 and we can use the gamma_write_mask bitmask
	 * directly.
	 */
	malidp_hw_write(hwdev, gamma_write_mask,
			hwdev->hw->map.coeffs_base + MALIDP_COEF_TABLE_ADDR);
	for (i = 0; i < MALIDP_COEFFTAB_NUM_COEFFS; ++i)
		malidp_hw_write(hwdev, data[i],
				hwdev->hw->map.coeffs_base +
				MALIDP_COEF_TABLE_DATA);
}

static void malidp_atomic_commit_update_gamma(struct drm_crtc *crtc,
					      struct drm_crtc_state *old_state)
{
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;

	if (!crtc->state->color_mgmt_changed)
		return;

	if (!crtc->state->gamma_lut) {
		malidp_hw_clearbits(hwdev,
				    MALIDP_DISP_FUNC_GAMMA,
				    MALIDP_DE_DISPLAY_FUNC);
	} else {
		struct malidp_crtc_state *mc =
			to_malidp_crtc_state(crtc->state);

		if (!old_state->gamma_lut || (crtc->state->gamma_lut->base.id !=
					      old_state->gamma_lut->base.id))
			malidp_write_gamma_table(hwdev, mc->gamma_coeffs);

		malidp_hw_setbits(hwdev, MALIDP_DISP_FUNC_GAMMA,
				  MALIDP_DE_DISPLAY_FUNC);
	}
}

static
void malidp_atomic_commit_update_coloradj(struct drm_crtc *crtc,
					  struct drm_crtc_state *old_state)
{
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;
	int i;

	if (!crtc->state->color_mgmt_changed)
		return;

	if (!crtc->state->ctm) {
		malidp_hw_clearbits(hwdev, MALIDP_DISP_FUNC_CADJ,
				    MALIDP_DE_DISPLAY_FUNC);
	} else {
		struct malidp_crtc_state *mc =
			to_malidp_crtc_state(crtc->state);

		if (!old_state->ctm || (crtc->state->ctm->base.id !=
					old_state->ctm->base.id))
			for (i = 0; i < MALIDP_COLORADJ_NUM_COEFFS; ++i)
				malidp_hw_write(hwdev,
						mc->coloradj_coeffs[i],
						hwdev->hw->map.coeffs_base +
						MALIDP_COLOR_ADJ_COEF + 4 * i);

		malidp_hw_setbits(hwdev, MALIDP_DISP_FUNC_CADJ,
				  MALIDP_DE_DISPLAY_FUNC);
	}
}

static void malidp_atomic_commit_se_config(struct drm_crtc *crtc,
					   struct drm_crtc_state *old_state)
{
	struct malidp_crtc_state *cs = to_malidp_crtc_state(crtc->state);
	struct malidp_crtc_state *old_cs = to_malidp_crtc_state(old_state);
	struct malidp_drm *malidp = crtc_to_malidp_device(crtc);
	struct malidp_hw_device *hwdev = malidp->dev;
	struct malidp_se_config *s = &cs->scaler_config;
	struct malidp_se_config *old_s = &old_cs->scaler_config;
	u32 se_control = hwdev->hw->map.se_base +
			 ((hwdev->hw->map.features & MALIDP_REGMAP_HAS_CLEARIRQ) ?
			 0x10 : 0xC);
	u32 layer_control = se_control + MALIDP_SE_LAYER_CONTROL;
	u32 scr = se_control + MALIDP_SE_SCALING_CONTROL;
	u32 val;

	/* Set SE_CONTROL */
	if (!s->scale_enable) {
		val = malidp_hw_read(hwdev, se_control);
		val &= ~MALIDP_SE_SCALING_EN;
		malidp_hw_write(hwdev, val, se_control);
		return;
	}

	hwdev->hw->se_set_scaling_coeffs(hwdev, s, old_s);
	val = malidp_hw_read(hwdev, se_control);
	val |= MALIDP_SE_SCALING_EN | MALIDP_SE_ALPHA_EN;

	val &= ~MALIDP_SE_ENH(MALIDP_SE_ENH_MASK);
	val |= s->enhancer_enable ? MALIDP_SE_ENH(3) : 0;

	val |= MALIDP_SE_RGBO_IF_EN;
	malidp_hw_write(hwdev, val, se_control);

	/* Set IN_SIZE & OUT_SIZE. */
	val = MALIDP_SE_SET_V_SIZE(s->input_h) |
	      MALIDP_SE_SET_H_SIZE(s->input_w);
	malidp_hw_write(hwdev, val, layer_control + MALIDP_SE_L0_IN_SIZE);
	val = MALIDP_SE_SET_V_SIZE(s->output_h) |
	      MALIDP_SE_SET_H_SIZE(s->output_w);
	malidp_hw_write(hwdev, val, layer_control + MALIDP_SE_L0_OUT_SIZE);

	/* Set phase regs. */
	malidp_hw_write(hwdev, s->h_init_phase, scr + MALIDP_SE_H_INIT_PH);
	malidp_hw_write(hwdev, s->h_delta_phase, scr + MALIDP_SE_H_DELTA_PH);
	malidp_hw_write(hwdev, s->v_init_phase, scr + MALIDP_SE_V_INIT_PH);
	malidp_hw_write(hwdev, s->v_delta_phase, scr + MALIDP_SE_V_DELTA_PH);
}

/*
 * set the "config valid" bit and wait until the hardware acts on it
 */
static int malidp_set_and_wait_config_valid(struct drm_device *drm)
{
	struct malidp_drm *malidp = drm->dev_private;
	struct malidp_hw_device *hwdev = malidp->dev;
	int ret;

	atomic_set(&malidp->config_valid, 0);
	hwdev->hw->set_config_valid(hwdev);
	/* don't wait for config_valid flag if we are in config mode */
	if (hwdev->hw->in_config_mode(hwdev))
		return 0;

	ret = wait_event_interruptible_timeout(malidp->wq,
			atomic_read(&malidp->config_valid) == 1,
			msecs_to_jiffies(MALIDP_CONF_VALID_TIMEOUT));

	return (ret > 0) ? 0 : -ETIMEDOUT;
}

static void malidp_atomic_commit_hw_done(struct drm_atomic_state *state)
{
	struct drm_pending_vblank_event *event;
	struct drm_device *drm = state->dev;
	struct malidp_drm *malidp = drm->dev_private;

	if (malidp->crtc.enabled) {
		/* only set config_valid if the CRTC is enabled */
		if (malidp_set_and_wait_config_valid(drm))
			DRM_DEBUG_DRIVER("timed out waiting for updated configuration\n");
	}

	event = malidp->crtc.state->event;
	if (event) {
		malidp->crtc.state->event = NULL;

		spin_lock_irq(&drm->event_lock);
		if (drm_crtc_vblank_get(&malidp->crtc) == 0)
			drm_crtc_arm_vblank_event(&malidp->crtc, event);
		else
			drm_crtc_send_vblank_event(&malidp->crtc, event);
		spin_unlock_irq(&drm->event_lock);
	}
	drm_atomic_helper_commit_hw_done(state);
}

static void malidp_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct drm_device *drm = state->dev;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	int i;

	pm_runtime_get_sync(drm->dev);

	drm_atomic_helper_commit_modeset_disables(drm, state);

	for_each_old_crtc_in_state(state, crtc, old_crtc_state, i) {
		malidp_atomic_commit_update_gamma(crtc, old_crtc_state);
		malidp_atomic_commit_update_coloradj(crtc, old_crtc_state);
		malidp_atomic_commit_se_config(crtc, old_crtc_state);
	}

	drm_atomic_helper_commit_planes(drm, state, 0);

	drm_atomic_helper_commit_modeset_enables(drm, state);

	malidp_atomic_commit_hw_done(state);

	drm_atomic_helper_wait_for_vblanks(drm, state);

	pm_runtime_put(drm->dev);

	drm_atomic_helper_cleanup_planes(drm, state);
}

static const struct drm_mode_config_helper_funcs malidp_mode_config_helpers = {
	.atomic_commit_tail = malidp_atomic_commit_tail,
};

static const struct drm_mode_config_funcs malidp_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int malidp_init(struct drm_device *drm)
{
	int ret;
	struct malidp_drm *malidp = drm->dev_private;
	struct malidp_hw_device *hwdev = malidp->dev;

	drm_mode_config_init(drm);

	drm->mode_config.min_width = hwdev->min_line_size;
	drm->mode_config.min_height = hwdev->min_line_size;
	drm->mode_config.max_width = hwdev->max_line_size;
	drm->mode_config.max_height = hwdev->max_line_size;
	drm->mode_config.funcs = &malidp_mode_config_funcs;
	drm->mode_config.helper_private = &malidp_mode_config_helpers;

	ret = malidp_crtc_init(drm);
	if (ret) {
		drm_mode_config_cleanup(drm);
		return ret;
	}

	return 0;
}

static void malidp_fini(struct drm_device *drm)
{
	malidp_de_planes_destroy(drm);
	drm_mode_config_cleanup(drm);
}

static int malidp_irq_init(struct platform_device *pdev)
{
	int irq_de, irq_se, ret = 0;
	struct drm_device *drm = dev_get_drvdata(&pdev->dev);

	/* fetch the interrupts from DT */
	irq_de = platform_get_irq_byname(pdev, "DE");
	if (irq_de < 0) {
		DRM_ERROR("no 'DE' IRQ specified!\n");
		return irq_de;
	}
	irq_se = platform_get_irq_byname(pdev, "SE");
	if (irq_se < 0) {
		DRM_ERROR("no 'SE' IRQ specified!\n");
		return irq_se;
	}

	ret = malidp_de_irq_init(drm, irq_de);
	if (ret)
		return ret;

	ret = malidp_se_irq_init(drm, irq_se);
	if (ret) {
		malidp_de_irq_fini(drm);
		return ret;
	}

	return 0;
}

DEFINE_DRM_GEM_CMA_FOPS(fops);

static struct drm_driver malidp_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC |
			   DRIVER_PRIME,
	.lastclose = drm_fb_helper_lastclose,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops = &drm_gem_cma_vm_ops,
	.dumb_create = drm_gem_cma_dumb_create,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_get_sg_table = drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap = drm_gem_cma_prime_vmap,
	.gem_prime_vunmap = drm_gem_cma_prime_vunmap,
	.gem_prime_mmap = drm_gem_cma_prime_mmap,
	.fops = &fops,
	.name = "mali-dp",
	.desc = "ARM Mali Display Processor driver",
	.date = "20160106",
	.major = 1,
	.minor = 0,
};

static const struct of_device_id  malidp_drm_of_match[] = {
	{
		.compatible = "arm,mali-dp500",
		.data = &malidp_device[MALIDP_500]
	},
	{
		.compatible = "arm,mali-dp550",
		.data = &malidp_device[MALIDP_550]
	},
	{
		.compatible = "arm,mali-dp650",
		.data = &malidp_device[MALIDP_650]
	},
	{},
};
MODULE_DEVICE_TABLE(of, malidp_drm_of_match);

static bool malidp_is_compatible_hw_id(struct malidp_hw_device *hwdev,
				       const struct of_device_id *dev_id)
{
	u32 core_id;
	const char *compatstr_dp500 = "arm,mali-dp500";
	bool is_dp500;
	bool dt_is_dp500;

	/*
	 * The DP500 CORE_ID register is in a different location, so check it
	 * first. If the product id field matches, then this is DP500, otherwise
	 * check the DP550/650 CORE_ID register.
	 */
	core_id = malidp_hw_read(hwdev, MALIDP500_DC_BASE + MALIDP_DE_CORE_ID);
	/* Offset 0x18 will never read 0x500 on products other than DP500. */
	is_dp500 = (MALIDP_PRODUCT_ID(core_id) == 0x500);
	dt_is_dp500 = strnstr(dev_id->compatible, compatstr_dp500,
			      sizeof(dev_id->compatible)) != NULL;
	if (is_dp500 != dt_is_dp500) {
		DRM_ERROR("Device-tree expects %s, but hardware %s DP500.\n",
			  dev_id->compatible, is_dp500 ? "is" : "is not");
		return false;
	} else if (!dt_is_dp500) {
		u16 product_id;
		char buf[32];

		core_id = malidp_hw_read(hwdev,
					 MALIDP550_DC_BASE + MALIDP_DE_CORE_ID);
		product_id = MALIDP_PRODUCT_ID(core_id);
		snprintf(buf, sizeof(buf), "arm,mali-dp%X", product_id);
		if (!strnstr(dev_id->compatible, buf,
			     sizeof(dev_id->compatible))) {
			DRM_ERROR("Device-tree expects %s, but hardware is DP%03X.\n",
				  dev_id->compatible, product_id);
			return false;
		}
	}
	return true;
}

static bool malidp_has_sufficient_address_space(const struct resource *res,
						const struct of_device_id *dev_id)
{
	resource_size_t res_size = resource_size(res);
	const char *compatstr_dp500 = "arm,mali-dp500";

	if (!strnstr(dev_id->compatible, compatstr_dp500,
		     sizeof(dev_id->compatible)))
		return res_size >= MALIDP550_ADDR_SPACE_SIZE;
	else if (res_size < MALIDP500_ADDR_SPACE_SIZE)
		return false;
	return true;
}

static ssize_t core_id_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct malidp_drm *malidp = drm->dev_private;

	return snprintf(buf, PAGE_SIZE, "%08x\n", malidp->core_id);
}

DEVICE_ATTR_RO(core_id);

static int malidp_init_sysfs(struct device *dev)
{
	int ret = device_create_file(dev, &dev_attr_core_id);

	if (ret)
		DRM_ERROR("failed to create device file for core_id\n");

	return ret;
}

static void malidp_fini_sysfs(struct device *dev)
{
	device_remove_file(dev, &dev_attr_core_id);
}

#define MAX_OUTPUT_CHANNELS	3

static int malidp_runtime_pm_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct malidp_drm *malidp = drm->dev_private;
	struct malidp_hw_device *hwdev = malidp->dev;

	/* we can only suspend if the hardware is in config mode */
	WARN_ON(!hwdev->hw->in_config_mode(hwdev));

	hwdev->pm_suspended = true;
	clk_disable_unprepare(hwdev->mclk);
	clk_disable_unprepare(hwdev->aclk);
	clk_disable_unprepare(hwdev->pclk);

	return 0;
}

static int malidp_runtime_pm_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct malidp_drm *malidp = drm->dev_private;
	struct malidp_hw_device *hwdev = malidp->dev;

	clk_prepare_enable(hwdev->pclk);
	clk_prepare_enable(hwdev->aclk);
	clk_prepare_enable(hwdev->mclk);
	hwdev->pm_suspended = false;

	return 0;
}

static int malidp_bind(struct device *dev)
{
	struct resource *res;
	struct drm_device *drm;
	struct malidp_drm *malidp;
	struct malidp_hw_device *hwdev;
	struct platform_device *pdev = to_platform_device(dev);
	struct of_device_id const *dev_id;
	/* number of lines for the R, G and B output */
	u8 output_width[MAX_OUTPUT_CHANNELS];
	int ret = 0, i;
	u32 version, out_depth = 0;

	malidp = devm_kzalloc(dev, sizeof(*malidp), GFP_KERNEL);
	if (!malidp)
		return -ENOMEM;

	hwdev = devm_kzalloc(dev, sizeof(*hwdev), GFP_KERNEL);
	if (!hwdev)
		return -ENOMEM;

	hwdev->hw = (struct malidp_hw *)of_device_get_match_data(dev);
	malidp->dev = hwdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hwdev->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(hwdev->regs))
		return PTR_ERR(hwdev->regs);

	hwdev->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(hwdev->pclk))
		return PTR_ERR(hwdev->pclk);

	hwdev->aclk = devm_clk_get(dev, "aclk");
	if (IS_ERR(hwdev->aclk))
		return PTR_ERR(hwdev->aclk);

	hwdev->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(hwdev->mclk))
		return PTR_ERR(hwdev->mclk);

	hwdev->pxlclk = devm_clk_get(dev, "pxlclk");
	if (IS_ERR(hwdev->pxlclk))
		return PTR_ERR(hwdev->pxlclk);

	/* Get the optional framebuffer memory resource */
	ret = of_reserved_mem_device_init(dev);
	if (ret && ret != -ENODEV)
		return ret;

	drm = drm_dev_alloc(&malidp_driver, dev);
	if (IS_ERR(drm)) {
		ret = PTR_ERR(drm);
		goto alloc_fail;
	}

	drm->dev_private = malidp;
	dev_set_drvdata(dev, drm);

	/* Enable power management */
	pm_runtime_enable(dev);

	/* Resume device to enable the clocks */
	if (pm_runtime_enabled(dev))
		pm_runtime_get_sync(dev);
	else
		malidp_runtime_pm_resume(dev);

	dev_id = of_match_device(malidp_drm_of_match, dev);
	if (!dev_id) {
		ret = -EINVAL;
		goto query_hw_fail;
	}

	if (!malidp_has_sufficient_address_space(res, dev_id)) {
		DRM_ERROR("Insufficient address space in device-tree.\n");
		ret = -EINVAL;
		goto query_hw_fail;
	}

	if (!malidp_is_compatible_hw_id(hwdev, dev_id)) {
		ret = -EINVAL;
		goto query_hw_fail;
	}

	ret = hwdev->hw->query_hw(hwdev);
	if (ret) {
		DRM_ERROR("Invalid HW configuration\n");
		goto query_hw_fail;
	}

	version = malidp_hw_read(hwdev, hwdev->hw->map.dc_base + MALIDP_DE_CORE_ID);
	DRM_INFO("found ARM Mali-DP%3x version r%dp%d\n", version >> 16,
		 (version >> 12) & 0xf, (version >> 8) & 0xf);

	malidp->core_id = version;

	/* set the number of lines used for output of RGB data */
	ret = of_property_read_u8_array(dev->of_node,
					"arm,malidp-output-port-lines",
					output_width, MAX_OUTPUT_CHANNELS);
	if (ret)
		goto query_hw_fail;

	for (i = 0; i < MAX_OUTPUT_CHANNELS; i++)
		out_depth = (out_depth << 8) | (output_width[i] & 0xf);
	malidp_hw_write(hwdev, out_depth, hwdev->hw->map.out_depth_base);

	atomic_set(&malidp->config_valid, 0);
	init_waitqueue_head(&malidp->wq);

	ret = malidp_init(drm);
	if (ret < 0)
		goto query_hw_fail;

	ret = malidp_init_sysfs(dev);
	if (ret)
		goto init_fail;

	/* Set the CRTC's port so that the encoder component can find it */
	malidp->crtc.port = of_graph_get_port_by_id(dev->of_node, 0);

	ret = component_bind_all(dev, drm);
	if (ret) {
		DRM_ERROR("Failed to bind all components\n");
		goto bind_fail;
	}

	ret = malidp_irq_init(pdev);
	if (ret < 0)
		goto irq_init_fail;

	drm->irq_enabled = true;

	ret = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (ret < 0) {
		DRM_ERROR("failed to initialise vblank\n");
		goto vblank_fail;
	}
	pm_runtime_put(dev);

	drm_mode_config_reset(drm);

	ret = drm_fb_cma_fbdev_init(drm, 32, 0);
	if (ret)
		goto fbdev_fail;

	drm_kms_helper_poll_init(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto register_fail;

	return 0;

register_fail:
	drm_fb_cma_fbdev_fini(drm);
	drm_kms_helper_poll_fini(drm);
fbdev_fail:
	pm_runtime_get_sync(dev);
vblank_fail:
	malidp_se_irq_fini(drm);
	malidp_de_irq_fini(drm);
	drm->irq_enabled = false;
irq_init_fail:
	component_unbind_all(dev, drm);
bind_fail:
	of_node_put(malidp->crtc.port);
	malidp->crtc.port = NULL;
init_fail:
	malidp_fini_sysfs(dev);
	malidp_fini(drm);
query_hw_fail:
	pm_runtime_put(dev);
	if (pm_runtime_enabled(dev))
		pm_runtime_disable(dev);
	else
		malidp_runtime_pm_suspend(dev);
	drm->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm);
alloc_fail:
	of_reserved_mem_device_release(dev);

	return ret;
}

static void malidp_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct malidp_drm *malidp = drm->dev_private;

	drm_dev_unregister(drm);
	drm_fb_cma_fbdev_fini(drm);
	drm_kms_helper_poll_fini(drm);
	pm_runtime_get_sync(dev);
	malidp_se_irq_fini(drm);
	malidp_de_irq_fini(drm);
	component_unbind_all(dev, drm);
	of_node_put(malidp->crtc.port);
	malidp->crtc.port = NULL;
	malidp_fini_sysfs(dev);
	malidp_fini(drm);
	pm_runtime_put(dev);
	if (pm_runtime_enabled(dev))
		pm_runtime_disable(dev);
	else
		malidp_runtime_pm_suspend(dev);
	drm->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm);
	of_reserved_mem_device_release(dev);
}

static const struct component_master_ops malidp_master_ops = {
	.bind = malidp_bind,
	.unbind = malidp_unbind,
};

static int malidp_compare_dev(struct device *dev, void *data)
{
	struct device_node *np = data;

	return dev->of_node == np;
}

static int malidp_platform_probe(struct platform_device *pdev)
{
	struct device_node *port;
	struct component_match *match = NULL;

	if (!pdev->dev.of_node)
		return -ENODEV;

	/* there is only one output port inside each device, find it */
	port = of_graph_get_remote_node(pdev->dev.of_node, 0, 0);
	if (!port)
		return -ENODEV;

	drm_of_component_match_add(&pdev->dev, &match, malidp_compare_dev,
				   port);
	of_node_put(port);
	return component_master_add_with_match(&pdev->dev, &malidp_master_ops,
					       match);
}

static int malidp_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &malidp_master_ops);
	return 0;
}

static int __maybe_unused malidp_pm_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm);
}

static int __maybe_unused malidp_pm_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	drm_mode_config_helper_resume(drm);

	return 0;
}

static const struct dev_pm_ops malidp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(malidp_pm_suspend, malidp_pm_resume) \
	SET_RUNTIME_PM_OPS(malidp_runtime_pm_suspend, malidp_runtime_pm_resume, NULL)
};

static struct platform_driver malidp_platform_driver = {
	.probe		= malidp_platform_probe,
	.remove		= malidp_platform_remove,
	.driver	= {
		.name = "mali-dp",
		.pm = &malidp_pm_ops,
		.of_match_table	= malidp_drm_of_match,
	},
};

module_platform_driver(malidp_platform_driver);

MODULE_AUTHOR("Liviu Dudau <Liviu.Dudau@arm.com>");
MODULE_DESCRIPTION("ARM Mali DP DRM driver");
MODULE_LICENSE("GPL v2");
