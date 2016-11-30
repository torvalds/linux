/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <linux/console.h>
#include <linux/vgaarb.h>
#include <linux/vga_switcheroo.h>

#include "i915_drv.h"

#define GEN_DEFAULT_PIPEOFFSETS \
	.pipe_offsets = { PIPE_A_OFFSET, PIPE_B_OFFSET, \
			  PIPE_C_OFFSET, PIPE_EDP_OFFSET }, \
	.trans_offsets = { TRANSCODER_A_OFFSET, TRANSCODER_B_OFFSET, \
			   TRANSCODER_C_OFFSET, TRANSCODER_EDP_OFFSET }, \
	.palette_offsets = { PALETTE_A_OFFSET, PALETTE_B_OFFSET }

#define GEN_CHV_PIPEOFFSETS \
	.pipe_offsets = { PIPE_A_OFFSET, PIPE_B_OFFSET, \
			  CHV_PIPE_C_OFFSET }, \
	.trans_offsets = { TRANSCODER_A_OFFSET, TRANSCODER_B_OFFSET, \
			   CHV_TRANSCODER_C_OFFSET, }, \
	.palette_offsets = { PALETTE_A_OFFSET, PALETTE_B_OFFSET, \
			     CHV_PALETTE_C_OFFSET }

#define CURSOR_OFFSETS \
	.cursor_offsets = { CURSOR_A_OFFSET, CURSOR_B_OFFSET, CHV_CURSOR_C_OFFSET }

#define IVB_CURSOR_OFFSETS \
	.cursor_offsets = { CURSOR_A_OFFSET, IVB_CURSOR_B_OFFSET, IVB_CURSOR_C_OFFSET }

#define BDW_COLORS \
	.color = { .degamma_lut_size = 512, .gamma_lut_size = 512 }
#define CHV_COLORS \
	.color = { .degamma_lut_size = 65, .gamma_lut_size = 257 }

#define GEN2_FEATURES \
	.gen = 2, .num_pipes = 1, \
	.has_overlay = 1, .overlay_needs_physical = 1, \
	.has_gmch_display = 1, \
	.hws_needs_physical = 1, \
	.ring_mask = RENDER_RING, \
	GEN_DEFAULT_PIPEOFFSETS, \
	CURSOR_OFFSETS

static const struct intel_device_info intel_i830_info = {
	GEN2_FEATURES,
	.is_mobile = 1, .cursor_needs_physical = 1,
	.num_pipes = 2, /* legal, last one wins */
};

static const struct intel_device_info intel_845g_info = {
	GEN2_FEATURES,
};

static const struct intel_device_info intel_i85x_info = {
	GEN2_FEATURES,
	.is_i85x = 1, .is_mobile = 1,
	.num_pipes = 2, /* legal, last one wins */
	.cursor_needs_physical = 1,
	.has_fbc = 1,
};

static const struct intel_device_info intel_i865g_info = {
	GEN2_FEATURES,
};

#define GEN3_FEATURES \
	.gen = 3, .num_pipes = 2, \
	.has_gmch_display = 1, \
	.ring_mask = RENDER_RING, \
	GEN_DEFAULT_PIPEOFFSETS, \
	CURSOR_OFFSETS

static const struct intel_device_info intel_i915g_info = {
	GEN3_FEATURES,
	.is_i915g = 1, .cursor_needs_physical = 1,
	.has_overlay = 1, .overlay_needs_physical = 1,
	.hws_needs_physical = 1,
};
static const struct intel_device_info intel_i915gm_info = {
	GEN3_FEATURES,
	.is_mobile = 1,
	.cursor_needs_physical = 1,
	.has_overlay = 1, .overlay_needs_physical = 1,
	.supports_tv = 1,
	.has_fbc = 1,
	.hws_needs_physical = 1,
};
static const struct intel_device_info intel_i945g_info = {
	GEN3_FEATURES,
	.has_hotplug = 1, .cursor_needs_physical = 1,
	.has_overlay = 1, .overlay_needs_physical = 1,
	.hws_needs_physical = 1,
};
static const struct intel_device_info intel_i945gm_info = {
	GEN3_FEATURES,
	.is_i945gm = 1, .is_mobile = 1,
	.has_hotplug = 1, .cursor_needs_physical = 1,
	.has_overlay = 1, .overlay_needs_physical = 1,
	.supports_tv = 1,
	.has_fbc = 1,
	.hws_needs_physical = 1,
};

#define GEN4_FEATURES \
	.gen = 4, .num_pipes = 2, \
	.has_hotplug = 1, \
	.has_gmch_display = 1, \
	.ring_mask = RENDER_RING, \
	GEN_DEFAULT_PIPEOFFSETS, \
	CURSOR_OFFSETS

static const struct intel_device_info intel_i965g_info = {
	GEN4_FEATURES,
	.is_broadwater = 1,
	.has_overlay = 1,
	.hws_needs_physical = 1,
};

static const struct intel_device_info intel_i965gm_info = {
	GEN4_FEATURES,
	.is_crestline = 1,
	.is_mobile = 1, .has_fbc = 1,
	.has_overlay = 1,
	.supports_tv = 1,
	.hws_needs_physical = 1,
};

static const struct intel_device_info intel_g33_info = {
	GEN3_FEATURES,
	.is_g33 = 1,
	.has_hotplug = 1,
	.has_overlay = 1,
};

static const struct intel_device_info intel_g45_info = {
	GEN4_FEATURES,
	.is_g4x = 1,
	.has_pipe_cxsr = 1,
	.ring_mask = RENDER_RING | BSD_RING,
};

static const struct intel_device_info intel_gm45_info = {
	GEN4_FEATURES,
	.is_g4x = 1,
	.is_mobile = 1, .has_fbc = 1,
	.has_pipe_cxsr = 1,
	.supports_tv = 1,
	.ring_mask = RENDER_RING | BSD_RING,
};

static const struct intel_device_info intel_pineview_info = {
	GEN3_FEATURES,
	.is_g33 = 1, .is_pineview = 1, .is_mobile = 1,
	.has_hotplug = 1,
	.has_overlay = 1,
};

#define GEN5_FEATURES \
	.gen = 5, .num_pipes = 2, \
	.has_hotplug = 1, \
	.has_gmbus_irq = 1, \
	.ring_mask = RENDER_RING | BSD_RING, \
	GEN_DEFAULT_PIPEOFFSETS, \
	CURSOR_OFFSETS

static const struct intel_device_info intel_ironlake_d_info = {
	GEN5_FEATURES,
};

static const struct intel_device_info intel_ironlake_m_info = {
	GEN5_FEATURES,
	.is_mobile = 1,
};

#define GEN6_FEATURES \
	.gen = 6, .num_pipes = 2, \
	.has_hotplug = 1, \
	.has_fbc = 1, \
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING, \
	.has_llc = 1, \
	.has_rc6 = 1, \
	.has_rc6p = 1, \
	.has_gmbus_irq = 1, \
	.has_hw_contexts = 1, \
	GEN_DEFAULT_PIPEOFFSETS, \
	CURSOR_OFFSETS

static const struct intel_device_info intel_sandybridge_d_info = {
	GEN6_FEATURES,
};

static const struct intel_device_info intel_sandybridge_m_info = {
	GEN6_FEATURES,
	.is_mobile = 1,
};

#define GEN7_FEATURES  \
	.gen = 7, .num_pipes = 3, \
	.has_hotplug = 1, \
	.has_fbc = 1, \
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING, \
	.has_llc = 1, \
	.has_rc6 = 1, \
	.has_rc6p = 1, \
	.has_gmbus_irq = 1, \
	.has_hw_contexts = 1, \
	GEN_DEFAULT_PIPEOFFSETS, \
	IVB_CURSOR_OFFSETS

static const struct intel_device_info intel_ivybridge_d_info = {
	GEN7_FEATURES,
	.is_ivybridge = 1,
	.has_l3_dpf = 1,
};

static const struct intel_device_info intel_ivybridge_m_info = {
	GEN7_FEATURES,
	.is_ivybridge = 1,
	.is_mobile = 1,
	.has_l3_dpf = 1,
};

static const struct intel_device_info intel_ivybridge_q_info = {
	GEN7_FEATURES,
	.is_ivybridge = 1,
	.num_pipes = 0, /* legal, last one wins */
	.has_l3_dpf = 1,
};

#define VLV_FEATURES  \
	.gen = 7, .num_pipes = 2, \
	.has_psr = 1, \
	.has_runtime_pm = 1, \
	.has_rc6 = 1, \
	.has_gmbus_irq = 1, \
	.has_hw_contexts = 1, \
	.has_gmch_display = 1, \
	.has_hotplug = 1, \
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING, \
	.display_mmio_offset = VLV_DISPLAY_BASE, \
	GEN_DEFAULT_PIPEOFFSETS, \
	CURSOR_OFFSETS

static const struct intel_device_info intel_valleyview_info = {
	VLV_FEATURES,
	.is_valleyview = 1,
};

#define HSW_FEATURES  \
	GEN7_FEATURES, \
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING | VEBOX_RING, \
	.has_ddi = 1, \
	.has_fpga_dbg = 1, \
	.has_psr = 1, \
	.has_resource_streamer = 1, \
	.has_dp_mst = 1, \
	.has_rc6p = 0 /* RC6p removed-by HSW */, \
	.has_runtime_pm = 1

static const struct intel_device_info intel_haswell_info = {
	HSW_FEATURES,
	.is_haswell = 1,
	.has_l3_dpf = 1,
};

#define BDW_FEATURES \
	HSW_FEATURES, \
	BDW_COLORS, \
	.has_logical_ring_contexts = 1, \
	.has_64bit_reloc = 1

static const struct intel_device_info intel_broadwell_info = {
	BDW_FEATURES,
	.gen = 8,
	.is_broadwell = 1,
};

static const struct intel_device_info intel_broadwell_gt3_info = {
	BDW_FEATURES,
	.gen = 8,
	.is_broadwell = 1,
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING | VEBOX_RING | BSD2_RING,
};

static const struct intel_device_info intel_cherryview_info = {
	.gen = 8, .num_pipes = 3,
	.has_hotplug = 1,
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING | VEBOX_RING,
	.is_cherryview = 1,
	.has_64bit_reloc = 1,
	.has_psr = 1,
	.has_runtime_pm = 1,
	.has_resource_streamer = 1,
	.has_rc6 = 1,
	.has_gmbus_irq = 1,
	.has_hw_contexts = 1,
	.has_logical_ring_contexts = 1,
	.has_gmch_display = 1,
	.display_mmio_offset = VLV_DISPLAY_BASE,
	GEN_CHV_PIPEOFFSETS,
	CURSOR_OFFSETS,
	CHV_COLORS,
};

static const struct intel_device_info intel_skylake_info = {
	BDW_FEATURES,
	.is_skylake = 1,
	.gen = 9,
	.has_csr = 1,
	.has_guc = 1,
	.ddb_size = 896,
};

static const struct intel_device_info intel_skylake_gt3_info = {
	BDW_FEATURES,
	.is_skylake = 1,
	.gen = 9,
	.has_csr = 1,
	.has_guc = 1,
	.ddb_size = 896,
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING | VEBOX_RING | BSD2_RING,
};

static const struct intel_device_info intel_broxton_info = {
	.is_broxton = 1,
	.gen = 9,
	.has_hotplug = 1,
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING | VEBOX_RING,
	.num_pipes = 3,
	.has_64bit_reloc = 1,
	.has_ddi = 1,
	.has_fpga_dbg = 1,
	.has_fbc = 1,
	.has_runtime_pm = 1,
	.has_pooled_eu = 0,
	.has_csr = 1,
	.has_resource_streamer = 1,
	.has_rc6 = 1,
	.has_dp_mst = 1,
	.has_gmbus_irq = 1,
	.has_hw_contexts = 1,
	.has_logical_ring_contexts = 1,
	.has_guc = 1,
	.has_decoupled_mmio = 1,
	.ddb_size = 512,
	GEN_DEFAULT_PIPEOFFSETS,
	IVB_CURSOR_OFFSETS,
	BDW_COLORS,
};

static const struct intel_device_info intel_kabylake_info = {
	BDW_FEATURES,
	.is_kabylake = 1,
	.gen = 9,
	.has_csr = 1,
	.has_guc = 1,
	.ddb_size = 896,
};

static const struct intel_device_info intel_kabylake_gt3_info = {
	BDW_FEATURES,
	.is_kabylake = 1,
	.gen = 9,
	.has_csr = 1,
	.has_guc = 1,
	.ddb_size = 896,
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING | VEBOX_RING | BSD2_RING,
};

/*
 * Make sure any device matches here are from most specific to most
 * general.  For example, since the Quanta match is based on the subsystem
 * and subvendor IDs, we need it to come before the more general IVB
 * PCI ID matches, otherwise we'll use the wrong info struct above.
 */
static const struct pci_device_id pciidlist[] = {
	INTEL_I830_IDS(&intel_i830_info),
	INTEL_I845G_IDS(&intel_845g_info),
	INTEL_I85X_IDS(&intel_i85x_info),
	INTEL_I865G_IDS(&intel_i865g_info),
	INTEL_I915G_IDS(&intel_i915g_info),
	INTEL_I915GM_IDS(&intel_i915gm_info),
	INTEL_I945G_IDS(&intel_i945g_info),
	INTEL_I945GM_IDS(&intel_i945gm_info),
	INTEL_I965G_IDS(&intel_i965g_info),
	INTEL_G33_IDS(&intel_g33_info),
	INTEL_I965GM_IDS(&intel_i965gm_info),
	INTEL_GM45_IDS(&intel_gm45_info),
	INTEL_G45_IDS(&intel_g45_info),
	INTEL_PINEVIEW_IDS(&intel_pineview_info),
	INTEL_IRONLAKE_D_IDS(&intel_ironlake_d_info),
	INTEL_IRONLAKE_M_IDS(&intel_ironlake_m_info),
	INTEL_SNB_D_IDS(&intel_sandybridge_d_info),
	INTEL_SNB_M_IDS(&intel_sandybridge_m_info),
	INTEL_IVB_Q_IDS(&intel_ivybridge_q_info), /* must be first IVB */
	INTEL_IVB_M_IDS(&intel_ivybridge_m_info),
	INTEL_IVB_D_IDS(&intel_ivybridge_d_info),
	INTEL_HSW_IDS(&intel_haswell_info),
	INTEL_VLV_IDS(&intel_valleyview_info),
	INTEL_BDW_GT12_IDS(&intel_broadwell_info),
	INTEL_BDW_GT3_IDS(&intel_broadwell_gt3_info),
	INTEL_CHV_IDS(&intel_cherryview_info),
	INTEL_SKL_GT1_IDS(&intel_skylake_info),
	INTEL_SKL_GT2_IDS(&intel_skylake_info),
	INTEL_SKL_GT3_IDS(&intel_skylake_gt3_info),
	INTEL_SKL_GT4_IDS(&intel_skylake_gt3_info),
	INTEL_BXT_IDS(&intel_broxton_info),
	INTEL_KBL_GT1_IDS(&intel_kabylake_info),
	INTEL_KBL_GT2_IDS(&intel_kabylake_info),
	INTEL_KBL_GT3_IDS(&intel_kabylake_gt3_info),
	INTEL_KBL_GT4_IDS(&intel_kabylake_gt3_info),
	{0, 0, 0}
};
MODULE_DEVICE_TABLE(pci, pciidlist);

static int i915_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct intel_device_info *intel_info =
		(struct intel_device_info *) ent->driver_data;

	if (IS_ALPHA_SUPPORT(intel_info) && !i915.alpha_support) {
		DRM_INFO("The driver support for your hardware in this kernel version is alpha quality\n"
			 "See CONFIG_DRM_I915_ALPHA_SUPPORT or i915.alpha_support module parameter\n"
			 "to enable support in this kernel version, or check for kernel updates.\n");
		return -ENODEV;
	}

	/* Only bind to function 0 of the device. Early generations
	 * used function 1 as a placeholder for multi-head. This causes
	 * us confusion instead, especially on the systems where both
	 * functions have the same PCI-ID!
	 */
	if (PCI_FUNC(pdev->devfn))
		return -ENODEV;

	/*
	 * apple-gmux is needed on dual GPU MacBook Pro
	 * to probe the panel if we're the inactive GPU.
	 */
	if (vga_switcheroo_client_probe_defer(pdev))
		return -EPROBE_DEFER;

	return i915_driver_load(pdev, ent);
}

static void i915_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	i915_driver_unload(dev);
	drm_dev_unref(dev);
}

static struct pci_driver i915_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = i915_pci_probe,
	.remove = i915_pci_remove,
	.driver.pm = &i915_pm_ops,
};

static int __init i915_init(void)
{
	bool use_kms = true;

	/*
	 * Enable KMS by default, unless explicitly overriden by
	 * either the i915.modeset prarameter or by the
	 * vga_text_mode_force boot option.
	 */

	if (i915.modeset == 0)
		use_kms = false;

	if (vgacon_text_force() && i915.modeset == -1)
		use_kms = false;

	if (!use_kms) {
		/* Silently fail loading to not upset userspace. */
		DRM_DEBUG_DRIVER("KMS disabled.\n");
		return 0;
	}

	return pci_register_driver(&i915_pci_driver);
}

static void __exit i915_exit(void)
{
	if (!i915_pci_driver.driver.owner)
		return;

	pci_unregister_driver(&i915_pci_driver);
}

module_init(i915_init);
module_exit(i915_exit);

MODULE_AUTHOR("Tungsten Graphics, Inc.");
MODULE_AUTHOR("Intel Corporation");

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
