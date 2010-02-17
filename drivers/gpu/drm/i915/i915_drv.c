/* i915_drv.c -- i830,i845,i855,i865,i915 driver -*- linux-c -*-
 */
/*
 *
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/device.h>
#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"

#include <linux/console.h>
#include "drm_crtc_helper.h"

static int i915_modeset = -1;
module_param_named(modeset, i915_modeset, int, 0400);

unsigned int i915_fbpercrtc = 0;
module_param_named(fbpercrtc, i915_fbpercrtc, int, 0400);

unsigned int i915_powersave = 1;
module_param_named(powersave, i915_powersave, int, 0400);

unsigned int i915_lvds_downclock = 0;
module_param_named(lvds_downclock, i915_lvds_downclock, int, 0400);

static struct drm_driver driver;

#define INTEL_VGA_DEVICE(id, info) {		\
	.class = PCI_CLASS_DISPLAY_VGA << 8,	\
	.class_mask = 0xffff00,			\
	.vendor = 0x8086,			\
	.device = id,				\
	.subvendor = PCI_ANY_ID,		\
	.subdevice = PCI_ANY_ID,		\
	.driver_data = (unsigned long) info }

const static struct intel_device_info intel_i830_info = {
	.is_i8xx = 1, .is_mobile = 1, .cursor_needs_physical = 1,
};

const static struct intel_device_info intel_845g_info = {
	.is_i8xx = 1,
};

const static struct intel_device_info intel_i85x_info = {
	.is_i8xx = 1, .is_mobile = 1, .cursor_needs_physical = 1,
};

const static struct intel_device_info intel_i865g_info = {
	.is_i8xx = 1,
};

const static struct intel_device_info intel_i915g_info = {
	.is_i915g = 1, .is_i9xx = 1, .cursor_needs_physical = 1,
};
const static struct intel_device_info intel_i915gm_info = {
	.is_i9xx = 1,  .is_mobile = 1, .has_fbc = 1,
	.cursor_needs_physical = 1,
};
const static struct intel_device_info intel_i945g_info = {
	.is_i9xx = 1, .has_hotplug = 1, .cursor_needs_physical = 1,
};
const static struct intel_device_info intel_i945gm_info = {
	.is_i945gm = 1, .is_i9xx = 1, .is_mobile = 1, .has_fbc = 1,
	.has_hotplug = 1, .cursor_needs_physical = 1,
};

const static struct intel_device_info intel_i965g_info = {
	.is_i965g = 1, .is_i9xx = 1, .has_hotplug = 1,
};

const static struct intel_device_info intel_i965gm_info = {
	.is_i965g = 1, .is_mobile = 1, .is_i965gm = 1, .is_i9xx = 1,
	.is_mobile = 1, .has_fbc = 1, .has_rc6 = 1,
	.has_hotplug = 1,
};

const static struct intel_device_info intel_g33_info = {
	.is_g33 = 1, .is_i9xx = 1, .need_gfx_hws = 1,
	.has_hotplug = 1,
};

const static struct intel_device_info intel_g45_info = {
	.is_i965g = 1, .is_g4x = 1, .is_i9xx = 1, .need_gfx_hws = 1,
	.has_pipe_cxsr = 1,
	.has_hotplug = 1,
};

const static struct intel_device_info intel_gm45_info = {
	.is_i965g = 1, .is_mobile = 1, .is_g4x = 1, .is_i9xx = 1,
	.is_mobile = 1, .need_gfx_hws = 1, .has_fbc = 1, .has_rc6 = 1,
	.has_pipe_cxsr = 1,
	.has_hotplug = 1,
};

const static struct intel_device_info intel_pineview_info = {
	.is_g33 = 1, .is_pineview = 1, .is_mobile = 1, .is_i9xx = 1,
	.need_gfx_hws = 1,
	.has_hotplug = 1,
};

const static struct intel_device_info intel_ironlake_d_info = {
	.is_ironlake = 1, .is_i965g = 1, .is_i9xx = 1, .need_gfx_hws = 1,
	.has_pipe_cxsr = 1,
	.has_hotplug = 1,
};

const static struct intel_device_info intel_ironlake_m_info = {
	.is_ironlake = 1, .is_mobile = 1, .is_i965g = 1, .is_i9xx = 1,
	.need_gfx_hws = 1, .has_rc6 = 1,
	.has_hotplug = 1,
};

const static struct pci_device_id pciidlist[] = {
	INTEL_VGA_DEVICE(0x3577, &intel_i830_info),
	INTEL_VGA_DEVICE(0x2562, &intel_845g_info),
	INTEL_VGA_DEVICE(0x3582, &intel_i85x_info),
	INTEL_VGA_DEVICE(0x35e8, &intel_i85x_info),
	INTEL_VGA_DEVICE(0x2572, &intel_i865g_info),
	INTEL_VGA_DEVICE(0x2582, &intel_i915g_info),
	INTEL_VGA_DEVICE(0x258a, &intel_i915g_info),
	INTEL_VGA_DEVICE(0x2592, &intel_i915gm_info),
	INTEL_VGA_DEVICE(0x2772, &intel_i945g_info),
	INTEL_VGA_DEVICE(0x27a2, &intel_i945gm_info),
	INTEL_VGA_DEVICE(0x27ae, &intel_i945gm_info),
	INTEL_VGA_DEVICE(0x2972, &intel_i965g_info),
	INTEL_VGA_DEVICE(0x2982, &intel_i965g_info),
	INTEL_VGA_DEVICE(0x2992, &intel_i965g_info),
	INTEL_VGA_DEVICE(0x29a2, &intel_i965g_info),
	INTEL_VGA_DEVICE(0x29b2, &intel_g33_info),
	INTEL_VGA_DEVICE(0x29c2, &intel_g33_info),
	INTEL_VGA_DEVICE(0x29d2, &intel_g33_info),
	INTEL_VGA_DEVICE(0x2a02, &intel_i965gm_info),
	INTEL_VGA_DEVICE(0x2a12, &intel_i965gm_info),
	INTEL_VGA_DEVICE(0x2a42, &intel_gm45_info),
	INTEL_VGA_DEVICE(0x2e02, &intel_g45_info),
	INTEL_VGA_DEVICE(0x2e12, &intel_g45_info),
	INTEL_VGA_DEVICE(0x2e22, &intel_g45_info),
	INTEL_VGA_DEVICE(0x2e32, &intel_g45_info),
	INTEL_VGA_DEVICE(0x2e42, &intel_g45_info),
	INTEL_VGA_DEVICE(0xa001, &intel_pineview_info),
	INTEL_VGA_DEVICE(0xa011, &intel_pineview_info),
	INTEL_VGA_DEVICE(0x0042, &intel_ironlake_d_info),
	INTEL_VGA_DEVICE(0x0046, &intel_ironlake_m_info),
	{0, 0, 0}
};

#if defined(CONFIG_DRM_I915_KMS)
MODULE_DEVICE_TABLE(pci, pciidlist);
#endif

static int i915_drm_freeze(struct drm_device *dev)
{
	pci_save_state(dev->pdev);

	/* If KMS is active, we do the leavevt stuff here */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		int error = i915_gem_idle(dev);
		if (error) {
			dev_err(&dev->pdev->dev,
				"GEM idle failed, resume might fail\n");
			return error;
		}
		drm_irq_uninstall(dev);
	}

	i915_save_state(dev);

	return 0;
}

static void i915_drm_suspend(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	intel_opregion_free(dev, 1);

	/* Modeset on resume, not lid events */
	dev_priv->modeset_on_lid = 0;
}

static int i915_suspend(struct drm_device *dev, pm_message_t state)
{
	int error;

	if (!dev || !dev->dev_private) {
		DRM_ERROR("dev: %p\n", dev);
		DRM_ERROR("DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	if (state.event == PM_EVENT_PRETHAW)
		return 0;

	error = i915_drm_freeze(dev);
	if (error)
		return error;

	i915_drm_suspend(dev);

	if (state.event == PM_EVENT_SUSPEND) {
		/* Shut down the device */
		pci_disable_device(dev->pdev);
		pci_set_power_state(dev->pdev, PCI_D3hot);
	}

	return 0;
}

static int i915_drm_thaw(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int error = 0;

	/* KMS EnterVT equivalent */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		mutex_lock(&dev->struct_mutex);
		dev_priv->mm.suspended = 0;

		error = i915_gem_init_ringbuffer(dev);
		mutex_unlock(&dev->struct_mutex);

		drm_irq_install(dev);

		/* Resume the modeset for every activated CRTC */
		drm_helper_resume_force_mode(dev);
	}

	dev_priv->modeset_on_lid = 0;

	return error;
}

static int i915_resume(struct drm_device *dev)
{
	if (pci_enable_device(dev->pdev))
		return -EIO;

	pci_set_master(dev->pdev);

	i915_restore_state(dev);

	intel_opregion_init(dev, 1);

	return i915_drm_thaw(dev);
}

/**
 * i965_reset - reset chip after a hang
 * @dev: drm device to reset
 * @flags: reset domains
 *
 * Reset the chip.  Useful if a hang is detected. Returns zero on successful
 * reset or otherwise an error code.
 *
 * Procedure is fairly simple:
 *   - reset the chip using the reset reg
 *   - re-init context state
 *   - re-init hardware status page
 *   - re-init ring buffer
 *   - re-init interrupt state
 *   - re-init display
 */
int i965_reset(struct drm_device *dev, u8 flags)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	unsigned long timeout;
	u8 gdrst;
	/*
	 * We really should only reset the display subsystem if we actually
	 * need to
	 */
	bool need_display = true;

	mutex_lock(&dev->struct_mutex);

	/*
	 * Clear request list
	 */
	i915_gem_retire_requests(dev);

	if (need_display)
		i915_save_display(dev);

	if (IS_I965G(dev) || IS_G4X(dev)) {
		/*
		 * Set the domains we want to reset, then the reset bit (bit 0).
		 * Clear the reset bit after a while and wait for hardware status
		 * bit (bit 1) to be set
		 */
		pci_read_config_byte(dev->pdev, GDRST, &gdrst);
		pci_write_config_byte(dev->pdev, GDRST, gdrst | flags | ((flags == GDRST_FULL) ? 0x1 : 0x0));
		udelay(50);
		pci_write_config_byte(dev->pdev, GDRST, gdrst & 0xfe);

		/* ...we don't want to loop forever though, 500ms should be plenty */
	       timeout = jiffies + msecs_to_jiffies(500);
		do {
			udelay(100);
			pci_read_config_byte(dev->pdev, GDRST, &gdrst);
		} while ((gdrst & 0x1) && time_after(timeout, jiffies));

		if (gdrst & 0x1) {
			WARN(true, "i915: Failed to reset chip\n");
			mutex_unlock(&dev->struct_mutex);
			return -EIO;
		}
	} else {
		DRM_ERROR("Error occurred. Don't know how to reset this chip.\n");
		return -ENODEV;
	}

	/* Ok, now get things going again... */

	/*
	 * Everything depends on having the GTT running, so we need to start
	 * there.  Fortunately we don't need to do this unless we reset the
	 * chip at a PCI level.
	 *
	 * Next we need to restore the context, but we don't use those
	 * yet either...
	 *
	 * Ring buffer needs to be re-initialized in the KMS case, or if X
	 * was running at the time of the reset (i.e. we weren't VT
	 * switched away).
	 */
	if (drm_core_check_feature(dev, DRIVER_MODESET) ||
	    !dev_priv->mm.suspended) {
		drm_i915_ring_buffer_t *ring = &dev_priv->ring;
		struct drm_gem_object *obj = ring->ring_obj;
		struct drm_i915_gem_object *obj_priv = obj->driver_private;
		dev_priv->mm.suspended = 0;

		/* Stop the ring if it's running. */
		I915_WRITE(PRB0_CTL, 0);
		I915_WRITE(PRB0_TAIL, 0);
		I915_WRITE(PRB0_HEAD, 0);

		/* Initialize the ring. */
		I915_WRITE(PRB0_START, obj_priv->gtt_offset);
		I915_WRITE(PRB0_CTL,
			   ((obj->size - 4096) & RING_NR_PAGES) |
			   RING_NO_REPORT |
			   RING_VALID);
		if (!drm_core_check_feature(dev, DRIVER_MODESET))
			i915_kernel_lost_context(dev);
		else {
			ring->head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
			ring->tail = I915_READ(PRB0_TAIL) & TAIL_ADDR;
			ring->space = ring->head - (ring->tail + 8);
			if (ring->space < 0)
				ring->space += ring->Size;
		}

		mutex_unlock(&dev->struct_mutex);
		drm_irq_uninstall(dev);
		drm_irq_install(dev);
		mutex_lock(&dev->struct_mutex);
	}

	/*
	 * Display needs restore too...
	 */
	if (need_display)
		i915_restore_display(dev);

	mutex_unlock(&dev->struct_mutex);
	return 0;
}


static int __devinit
i915_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	return drm_get_dev(pdev, ent, &driver);
}

static void
i915_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static int i915_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int error;

	if (!drm_dev || !drm_dev->dev_private) {
		dev_err(dev, "DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	error = i915_drm_freeze(drm_dev);
	if (error)
		return error;

	i915_drm_suspend(drm_dev);

	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int i915_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	return i915_resume(drm_dev);
}

static int i915_pm_freeze(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	if (!drm_dev || !drm_dev->dev_private) {
		dev_err(dev, "DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	return i915_drm_freeze(drm_dev);
}

static int i915_pm_thaw(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	return i915_drm_thaw(drm_dev);
}

static int i915_pm_poweroff(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int error;

	error = i915_drm_freeze(drm_dev);
	if (!error)
		i915_drm_suspend(drm_dev);

	return error;
}

const struct dev_pm_ops i915_pm_ops = {
     .suspend = i915_pm_suspend,
     .resume = i915_pm_resume,
     .freeze = i915_pm_freeze,
     .thaw = i915_pm_thaw,
     .poweroff = i915_pm_poweroff,
     .restore = i915_pm_resume,
};

static struct vm_operations_struct i915_gem_vm_ops = {
	.fault = i915_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static struct drm_driver driver = {
	/* don't use mtrr's here, the Xserver or user space app should
	 * deal with them for intel hardware.
	 */
	.driver_features =
	    DRIVER_USE_AGP | DRIVER_REQUIRE_AGP | /* DRIVER_USE_MTRR |*/
	    DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | DRIVER_GEM,
	.load = i915_driver_load,
	.unload = i915_driver_unload,
	.open = i915_driver_open,
	.lastclose = i915_driver_lastclose,
	.preclose = i915_driver_preclose,
	.postclose = i915_driver_postclose,

	/* Used in place of i915_pm_ops for non-DRIVER_MODESET */
	.suspend = i915_suspend,
	.resume = i915_resume,

	.device_is_agp = i915_driver_device_is_agp,
	.enable_vblank = i915_enable_vblank,
	.disable_vblank = i915_disable_vblank,
	.irq_preinstall = i915_driver_irq_preinstall,
	.irq_postinstall = i915_driver_irq_postinstall,
	.irq_uninstall = i915_driver_irq_uninstall,
	.irq_handler = i915_driver_irq_handler,
	.reclaim_buffers = drm_core_reclaim_buffers,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.master_create = i915_master_create,
	.master_destroy = i915_master_destroy,
#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = i915_debugfs_init,
	.debugfs_cleanup = i915_debugfs_cleanup,
#endif
	.gem_init_object = i915_gem_init_object,
	.gem_free_object = i915_gem_free_object,
	.gem_vm_ops = &i915_gem_vm_ops,
	.ioctls = i915_ioctls,
	.fops = {
		 .owner = THIS_MODULE,
		 .open = drm_open,
		 .release = drm_release,
		 .unlocked_ioctl = drm_ioctl,
		 .mmap = drm_gem_mmap,
		 .poll = drm_poll,
		 .fasync = drm_fasync,
		 .read = drm_read,
#ifdef CONFIG_COMPAT
		 .compat_ioctl = i915_compat_ioctl,
#endif
	},

	.pci_driver = {
		 .name = DRIVER_NAME,
		 .id_table = pciidlist,
		 .probe = i915_pci_probe,
		 .remove = i915_pci_remove,
		 .driver.pm = &i915_pm_ops,
	},

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int __init i915_init(void)
{
	driver.num_ioctls = i915_max_ioctl;

	i915_gem_shrinker_init();

	/*
	 * If CONFIG_DRM_I915_KMS is set, default to KMS unless
	 * explicitly disabled with the module pararmeter.
	 *
	 * Otherwise, just follow the parameter (defaulting to off).
	 *
	 * Allow optional vga_text_mode_force boot option to override
	 * the default behavior.
	 */
#if defined(CONFIG_DRM_I915_KMS)
	if (i915_modeset != 0)
		driver.driver_features |= DRIVER_MODESET;
#endif
	if (i915_modeset == 1)
		driver.driver_features |= DRIVER_MODESET;

#ifdef CONFIG_VGA_CONSOLE
	if (vgacon_text_force() && i915_modeset == -1)
		driver.driver_features &= ~DRIVER_MODESET;
#endif

	return drm_init(&driver);
}

static void __exit i915_exit(void)
{
	i915_gem_shrinker_exit();
	drm_exit(&driver);
}

module_init(i915_init);
module_exit(i915_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
