/**
 * \file radeon_drv.c
 * ATI Radeon driver
 *
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


#include <linux/compat.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/vga_switcheroo.h>
#include <linux/mmu_notifier.h>

#include <drm/drm_agpsupport.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_pci.h>
#include <drm/drm_pciids.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <drm/radeon_drm.h>

#include "radeon_drv.h"

/*
 * KMS wrapper.
 * - 2.0.0 - initial interface
 * - 2.1.0 - add square tiling interface
 * - 2.2.0 - add r6xx/r7xx const buffer support
 * - 2.3.0 - add MSPOS + 3D texture + r500 VAP regs
 * - 2.4.0 - add crtc id query
 * - 2.5.0 - add get accel 2 to work around ddx breakage for evergreen
 * - 2.6.0 - add tiling config query (r6xx+), add initial HiZ support (r300->r500)
 *   2.7.0 - fixups for r600 2D tiling support. (no external ABI change), add eg dyn gpr regs
 *   2.8.0 - pageflip support, r500 US_FORMAT regs. r500 ARGB2101010 colorbuf, r300->r500 CMASK, clock crystal query
 *   2.9.0 - r600 tiling (s3tc,rgtc) working, SET_PREDICATION packet 3 on r600 + eg, backend query
 *   2.10.0 - fusion 2D tiling
 *   2.11.0 - backend map, initial compute support for the CS checker
 *   2.12.0 - RADEON_CS_KEEP_TILING_FLAGS
 *   2.13.0 - virtual memory support, streamout
 *   2.14.0 - add evergreen tiling informations
 *   2.15.0 - add max_pipes query
 *   2.16.0 - fix evergreen 2D tiled surface calculation
 *   2.17.0 - add STRMOUT_BASE_UPDATE for r7xx
 *   2.18.0 - r600-eg: allow "invalid" DB formats
 *   2.19.0 - r600-eg: MSAA textures
 *   2.20.0 - r600-si: RADEON_INFO_TIMESTAMP query
 *   2.21.0 - r600-r700: FMASK and CMASK
 *   2.22.0 - r600 only: RESOLVE_BOX allowed
 *   2.23.0 - allow STRMOUT_BASE_UPDATE on RS780 and RS880
 *   2.24.0 - eg only: allow MIP_ADDRESS=0 for MSAA textures
 *   2.25.0 - eg+: new info request for num SE and num SH
 *   2.26.0 - r600-eg: fix htile size computation
 *   2.27.0 - r600-SI: Add CS ioctl support for async DMA
 *   2.28.0 - r600-eg: Add MEM_WRITE packet support
 *   2.29.0 - R500 FP16 color clear registers
 *   2.30.0 - fix for FMASK texturing
 *   2.31.0 - Add fastfb support for rs690
 *   2.32.0 - new info request for rings working
 *   2.33.0 - Add SI tiling mode array query
 *   2.34.0 - Add CIK tiling mode array query
 *   2.35.0 - Add CIK macrotile mode array query
 *   2.36.0 - Fix CIK DCE tiling setup
 *   2.37.0 - allow GS ring setup on r6xx/r7xx
 *   2.38.0 - RADEON_GEM_OP (GET_INITIAL_DOMAIN, SET_INITIAL_DOMAIN),
 *            CIK: 1D and linear tiling modes contain valid PIPE_CONFIG
 *   2.39.0 - Add INFO query for number of active CUs
 *   2.40.0 - Add RADEON_GEM_GTT_WC/UC, flush HDP cache before submitting
 *            CS to GPU on >= r600
 *   2.41.0 - evergreen/cayman: Add SET_BASE/DRAW_INDIRECT command parsing support
 *   2.42.0 - Add VCE/VUI (Video Usability Information) support
 *   2.43.0 - RADEON_INFO_GPU_RESET_COUNTER
 *   2.44.0 - SET_APPEND_CNT packet3 support
 *   2.45.0 - Allow setting shader registers using DMA/COPY packet3 on SI
 *   2.46.0 - Add PFP_SYNC_ME support on evergreen
 *   2.47.0 - Add UVD_NO_OP register support
 *   2.48.0 - TA_CS_BC_BASE_ADDR allowed on SI
 *   2.49.0 - DRM_RADEON_GEM_INFO ioctl returns correct vram_size/visible values
 *   2.50.0 - Allows unaligned shader loads on CIK. (needed by OpenGL)
 */
#define KMS_DRIVER_MAJOR	2
#define KMS_DRIVER_MINOR	50
#define KMS_DRIVER_PATCHLEVEL	0
int radeon_driver_load_kms(struct drm_device *dev, unsigned long flags);
void radeon_driver_unload_kms(struct drm_device *dev);
void radeon_driver_lastclose_kms(struct drm_device *dev);
int radeon_driver_open_kms(struct drm_device *dev, struct drm_file *file_priv);
void radeon_driver_postclose_kms(struct drm_device *dev,
				 struct drm_file *file_priv);
int radeon_suspend_kms(struct drm_device *dev, bool suspend,
		       bool fbcon, bool freeze);
int radeon_resume_kms(struct drm_device *dev, bool resume, bool fbcon);
void radeon_driver_irq_preinstall_kms(struct drm_device *dev);
int radeon_driver_irq_postinstall_kms(struct drm_device *dev);
void radeon_driver_irq_uninstall_kms(struct drm_device *dev);
irqreturn_t radeon_driver_irq_handler_kms(int irq, void *arg);
void radeon_gem_object_free(struct drm_gem_object *obj);
int radeon_gem_object_open(struct drm_gem_object *obj,
				struct drm_file *file_priv);
void radeon_gem_object_close(struct drm_gem_object *obj,
				struct drm_file *file_priv);
struct dma_buf *radeon_gem_prime_export(struct drm_gem_object *gobj,
					int flags);
extern int radeon_get_crtc_scanoutpos(struct drm_device *dev, unsigned int crtc,
				      unsigned int flags, int *vpos, int *hpos,
				      ktime_t *stime, ktime_t *etime,
				      const struct drm_display_mode *mode);
extern bool radeon_is_px(struct drm_device *dev);
extern const struct drm_ioctl_desc radeon_ioctls_kms[];
extern int radeon_max_kms_ioctl;
int radeon_mmap(struct file *filp, struct vm_area_struct *vma);
int radeon_mode_dumb_mmap(struct drm_file *filp,
			  struct drm_device *dev,
			  uint32_t handle, uint64_t *offset_p);
int radeon_mode_dumb_create(struct drm_file *file_priv,
			    struct drm_device *dev,
			    struct drm_mode_create_dumb *args);
struct sg_table *radeon_gem_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *radeon_gem_prime_import_sg_table(struct drm_device *dev,
							struct dma_buf_attachment *,
							struct sg_table *sg);
int radeon_gem_prime_pin(struct drm_gem_object *obj);
void radeon_gem_prime_unpin(struct drm_gem_object *obj);
void *radeon_gem_prime_vmap(struct drm_gem_object *obj);
void radeon_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);

/* atpx handler */
#if defined(CONFIG_VGA_SWITCHEROO)
void radeon_register_atpx_handler(void);
void radeon_unregister_atpx_handler(void);
bool radeon_has_atpx_dgpu_power_cntl(void);
bool radeon_is_atpx_hybrid(void);
#else
static inline void radeon_register_atpx_handler(void) {}
static inline void radeon_unregister_atpx_handler(void) {}
static inline bool radeon_has_atpx_dgpu_power_cntl(void) { return false; }
static inline bool radeon_is_atpx_hybrid(void) { return false; }
#endif

int radeon_no_wb;
int radeon_modeset = -1;
int radeon_dynclks = -1;
int radeon_r4xx_atom = 0;
#ifdef __powerpc__
/* Default to PCI on PowerPC (fdo #95017) */
int radeon_agpmode = -1;
#else
int radeon_agpmode = 0;
#endif
int radeon_vram_limit = 0;
int radeon_gart_size = -1; /* auto */
int radeon_benchmarking = 0;
int radeon_testing = 0;
int radeon_connector_table = 0;
int radeon_tv = 1;
int radeon_audio = -1;
int radeon_disp_priority = 0;
int radeon_hw_i2c = 0;
int radeon_pcie_gen2 = -1;
int radeon_msi = -1;
int radeon_lockup_timeout = 10000;
int radeon_fastfb = 0;
int radeon_dpm = -1;
int radeon_aspm = -1;
int radeon_runtime_pm = -1;
int radeon_hard_reset = 0;
int radeon_vm_size = 8;
int radeon_vm_block_size = -1;
int radeon_deep_color = 0;
int radeon_use_pflipirq = 2;
int radeon_bapm = -1;
int radeon_backlight = -1;
int radeon_auxch = -1;
int radeon_mst = 0;
int radeon_uvd = 1;
int radeon_vce = 1;

MODULE_PARM_DESC(no_wb, "Disable AGP writeback for scratch registers");
module_param_named(no_wb, radeon_no_wb, int, 0444);

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, radeon_modeset, int, 0400);

MODULE_PARM_DESC(dynclks, "Disable/Enable dynamic clocks");
module_param_named(dynclks, radeon_dynclks, int, 0444);

MODULE_PARM_DESC(r4xx_atom, "Enable ATOMBIOS modesetting for R4xx");
module_param_named(r4xx_atom, radeon_r4xx_atom, int, 0444);

MODULE_PARM_DESC(vramlimit, "Restrict VRAM for testing, in megabytes");
module_param_named(vramlimit, radeon_vram_limit, int, 0600);

MODULE_PARM_DESC(agpmode, "AGP Mode (-1 == PCI)");
module_param_named(agpmode, radeon_agpmode, int, 0444);

MODULE_PARM_DESC(gartsize, "Size of PCIE/IGP gart to setup in megabytes (32, 64, etc., -1 = auto)");
module_param_named(gartsize, radeon_gart_size, int, 0600);

MODULE_PARM_DESC(benchmark, "Run benchmark");
module_param_named(benchmark, radeon_benchmarking, int, 0444);

MODULE_PARM_DESC(test, "Run tests");
module_param_named(test, radeon_testing, int, 0444);

MODULE_PARM_DESC(connector_table, "Force connector table");
module_param_named(connector_table, radeon_connector_table, int, 0444);

MODULE_PARM_DESC(tv, "TV enable (0 = disable)");
module_param_named(tv, radeon_tv, int, 0444);

MODULE_PARM_DESC(audio, "Audio enable (-1 = auto, 0 = disable, 1 = enable)");
module_param_named(audio, radeon_audio, int, 0444);

MODULE_PARM_DESC(disp_priority, "Display Priority (0 = auto, 1 = normal, 2 = high)");
module_param_named(disp_priority, radeon_disp_priority, int, 0444);

MODULE_PARM_DESC(hw_i2c, "hw i2c engine enable (0 = disable)");
module_param_named(hw_i2c, radeon_hw_i2c, int, 0444);

MODULE_PARM_DESC(pcie_gen2, "PCIE Gen2 mode (-1 = auto, 0 = disable, 1 = enable)");
module_param_named(pcie_gen2, radeon_pcie_gen2, int, 0444);

MODULE_PARM_DESC(msi, "MSI support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(msi, radeon_msi, int, 0444);

MODULE_PARM_DESC(lockup_timeout, "GPU lockup timeout in ms (default 10000 = 10 seconds, 0 = disable)");
module_param_named(lockup_timeout, radeon_lockup_timeout, int, 0444);

MODULE_PARM_DESC(fastfb, "Direct FB access for IGP chips (0 = disable, 1 = enable)");
module_param_named(fastfb, radeon_fastfb, int, 0444);

MODULE_PARM_DESC(dpm, "DPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(dpm, radeon_dpm, int, 0444);

MODULE_PARM_DESC(aspm, "ASPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(aspm, radeon_aspm, int, 0444);

MODULE_PARM_DESC(runpm, "PX runtime pm (1 = force enable, 0 = disable, -1 = PX only default)");
module_param_named(runpm, radeon_runtime_pm, int, 0444);

MODULE_PARM_DESC(hard_reset, "PCI config reset (1 = force enable, 0 = disable (default))");
module_param_named(hard_reset, radeon_hard_reset, int, 0444);

MODULE_PARM_DESC(vm_size, "VM address space size in gigabytes (default 4GB)");
module_param_named(vm_size, radeon_vm_size, int, 0444);

MODULE_PARM_DESC(vm_block_size, "VM page table size in bits (default depending on vm_size)");
module_param_named(vm_block_size, radeon_vm_block_size, int, 0444);

MODULE_PARM_DESC(deep_color, "Deep Color support (1 = enable, 0 = disable (default))");
module_param_named(deep_color, radeon_deep_color, int, 0444);

MODULE_PARM_DESC(use_pflipirq, "Pflip irqs for pageflip completion (0 = disable, 1 = as fallback, 2 = exclusive (default))");
module_param_named(use_pflipirq, radeon_use_pflipirq, int, 0444);

MODULE_PARM_DESC(bapm, "BAPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(bapm, radeon_bapm, int, 0444);

MODULE_PARM_DESC(backlight, "backlight support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(backlight, radeon_backlight, int, 0444);

MODULE_PARM_DESC(auxch, "Use native auxch experimental support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(auxch, radeon_auxch, int, 0444);

MODULE_PARM_DESC(mst, "DisplayPort MST experimental support (1 = enable, 0 = disable)");
module_param_named(mst, radeon_mst, int, 0444);

MODULE_PARM_DESC(uvd, "uvd enable/disable uvd support (1 = enable, 0 = disable)");
module_param_named(uvd, radeon_uvd, int, 0444);

MODULE_PARM_DESC(vce, "vce enable/disable vce support (1 = enable, 0 = disable)");
module_param_named(vce, radeon_vce, int, 0444);

int radeon_si_support = 1;
MODULE_PARM_DESC(si_support, "SI support (1 = enabled (default), 0 = disabled)");
module_param_named(si_support, radeon_si_support, int, 0444);

int radeon_cik_support = 1;
MODULE_PARM_DESC(cik_support, "CIK support (1 = enabled (default), 0 = disabled)");
module_param_named(cik_support, radeon_cik_support, int, 0444);

static struct pci_device_id pciidlist[] = {
	radeon_PCI_IDS
};

MODULE_DEVICE_TABLE(pci, pciidlist);

static struct drm_driver kms_driver;

bool radeon_device_is_virtual(void);

static int radeon_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	unsigned long flags = 0;
	struct drm_device *dev;
	int ret;

	if (!ent)
		return -ENODEV; /* Avoid NULL-ptr deref in drm_get_pci_dev */

	flags = ent->driver_data;

	if (!radeon_si_support) {
		switch (flags & RADEON_FAMILY_MASK) {
		case CHIP_TAHITI:
		case CHIP_PITCAIRN:
		case CHIP_VERDE:
		case CHIP_OLAND:
		case CHIP_HAINAN:
			dev_info(&pdev->dev,
				 "SI support disabled by module param\n");
			return -ENODEV;
		}
	}
	if (!radeon_cik_support) {
		switch (flags & RADEON_FAMILY_MASK) {
		case CHIP_KAVERI:
		case CHIP_BONAIRE:
		case CHIP_HAWAII:
		case CHIP_KABINI:
		case CHIP_MULLINS:
			dev_info(&pdev->dev,
				 "CIK support disabled by module param\n");
			return -ENODEV;
		}
	}

	if (vga_switcheroo_client_probe_defer(pdev))
		return -EPROBE_DEFER;

	/* Get rid of things like offb */
	ret = drm_fb_helper_remove_conflicting_pci_framebuffers(pdev, "radeondrmfb");
	if (ret)
		return ret;

	dev = drm_dev_alloc(&kms_driver, &pdev->dev);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	ret = pci_enable_device(pdev);
	if (ret)
		goto err_free;

	dev->pdev = pdev;
#ifdef __alpha__
	dev->hose = pdev->sysdata;
#endif

	pci_set_drvdata(pdev, dev);

	if (pci_find_capability(dev->pdev, PCI_CAP_ID_AGP))
		dev->agp = drm_agp_init(dev);
	if (dev->agp) {
		dev->agp->agp_mtrr = arch_phys_wc_add(
			dev->agp->agp_info.aper_base,
			dev->agp->agp_info.aper_size *
			1024 * 1024);
	}

	ret = drm_dev_register(dev, ent->driver_data);
	if (ret)
		goto err_agp;

	return 0;

err_agp:
	if (dev->agp)
		arch_phys_wc_del(dev->agp->agp_mtrr);
	kfree(dev->agp);
	pci_disable_device(pdev);
err_free:
	drm_dev_put(dev);
	return ret;
}

static void
radeon_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static void
radeon_pci_shutdown(struct pci_dev *pdev)
{
	/* if we are running in a VM, make sure the device
	 * torn down properly on reboot/shutdown
	 */
	if (radeon_device_is_virtual())
		radeon_pci_remove(pdev);

#ifdef CONFIG_PPC64
	/*
	 * Some adapters need to be suspended before a
	 * shutdown occurs in order to prevent an error
	 * during kexec.
	 * Make this power specific becauase it breaks
	 * some non-power boards.
	 */
	radeon_suspend_kms(pci_get_drvdata(pdev), true, true, false);
#endif
}

static int radeon_pmops_suspend(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	return radeon_suspend_kms(drm_dev, true, true, false);
}

static int radeon_pmops_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	/* GPU comes up enabled by the bios on resume */
	if (radeon_is_px(drm_dev)) {
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	return radeon_resume_kms(drm_dev, true, true);
}

static int radeon_pmops_freeze(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	return radeon_suspend_kms(drm_dev, false, true, true);
}

static int radeon_pmops_thaw(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	return radeon_resume_kms(drm_dev, false, true);
}

static int radeon_pmops_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int ret;

	if (!radeon_is_px(drm_dev)) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	drm_dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
	drm_kms_helper_poll_disable(drm_dev);

	ret = radeon_suspend_kms(drm_dev, false, false, false);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_ignore_hotplug(pdev);
	if (radeon_is_atpx_hybrid())
		pci_set_power_state(pdev, PCI_D3cold);
	else if (!radeon_has_atpx_dgpu_power_cntl())
		pci_set_power_state(pdev, PCI_D3hot);
	drm_dev->switch_power_state = DRM_SWITCH_POWER_DYNAMIC_OFF;

	return 0;
}

static int radeon_pmops_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int ret;

	if (!radeon_is_px(drm_dev))
		return -EINVAL;

	drm_dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;

	if (radeon_is_atpx_hybrid() ||
	    !radeon_has_atpx_dgpu_power_cntl())
		pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	pci_set_master(pdev);

	ret = radeon_resume_kms(drm_dev, false, false);
	drm_kms_helper_poll_enable(drm_dev);
	drm_dev->switch_power_state = DRM_SWITCH_POWER_ON;
	return 0;
}

static int radeon_pmops_runtime_idle(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct drm_crtc *crtc;

	if (!radeon_is_px(drm_dev)) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	list_for_each_entry(crtc, &drm_dev->mode_config.crtc_list, head) {
		if (crtc->enabled) {
			DRM_DEBUG_DRIVER("failing to power off - crtc active\n");
			return -EBUSY;
		}
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_autosuspend(dev);
	/* we don't want the main rpm_idle to call suspend - we want to autosuspend */
	return 1;
}

long radeon_drm_ioctl(struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev;
	long ret;
	dev = file_priv->minor->dev;
	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0)
		return ret;

	ret = drm_ioctl(filp, cmd, arg);
	
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
	return ret;
}

#ifdef CONFIG_COMPAT
static long radeon_kms_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int nr = DRM_IOCTL_NR(cmd);
	int ret;

	if (nr < DRM_COMMAND_BASE)
		return drm_compat_ioctl(filp, cmd, arg);

	ret = radeon_drm_ioctl(filp, cmd, arg);

	return ret;
}
#endif

static const struct dev_pm_ops radeon_pm_ops = {
	.suspend = radeon_pmops_suspend,
	.resume = radeon_pmops_resume,
	.freeze = radeon_pmops_freeze,
	.thaw = radeon_pmops_thaw,
	.poweroff = radeon_pmops_freeze,
	.restore = radeon_pmops_resume,
	.runtime_suspend = radeon_pmops_runtime_suspend,
	.runtime_resume = radeon_pmops_runtime_resume,
	.runtime_idle = radeon_pmops_runtime_idle,
};

static const struct file_operations radeon_driver_kms_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = radeon_drm_ioctl,
	.mmap = radeon_mmap,
	.poll = drm_poll,
	.read = drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl = radeon_kms_compat_ioctl,
#endif
};

static struct drm_driver kms_driver = {
	.driver_features =
	    DRIVER_GEM | DRIVER_RENDER,
	.load = radeon_driver_load_kms,
	.open = radeon_driver_open_kms,
	.postclose = radeon_driver_postclose_kms,
	.lastclose = radeon_driver_lastclose_kms,
	.unload = radeon_driver_unload_kms,
	.irq_preinstall = radeon_driver_irq_preinstall_kms,
	.irq_postinstall = radeon_driver_irq_postinstall_kms,
	.irq_uninstall = radeon_driver_irq_uninstall_kms,
	.irq_handler = radeon_driver_irq_handler_kms,
	.ioctls = radeon_ioctls_kms,
	.gem_free_object_unlocked = radeon_gem_object_free,
	.gem_open_object = radeon_gem_object_open,
	.gem_close_object = radeon_gem_object_close,
	.dumb_create = radeon_mode_dumb_create,
	.dumb_map_offset = radeon_mode_dumb_mmap,
	.fops = &radeon_driver_kms_fops,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = radeon_gem_prime_export,
	.gem_prime_pin = radeon_gem_prime_pin,
	.gem_prime_unpin = radeon_gem_prime_unpin,
	.gem_prime_get_sg_table = radeon_gem_prime_get_sg_table,
	.gem_prime_import_sg_table = radeon_gem_prime_import_sg_table,
	.gem_prime_vmap = radeon_gem_prime_vmap,
	.gem_prime_vunmap = radeon_gem_prime_vunmap,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = KMS_DRIVER_MAJOR,
	.minor = KMS_DRIVER_MINOR,
	.patchlevel = KMS_DRIVER_PATCHLEVEL,
};

static struct drm_driver *driver;
static struct pci_driver *pdriver;

static struct pci_driver radeon_kms_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = radeon_pci_probe,
	.remove = radeon_pci_remove,
	.shutdown = radeon_pci_shutdown,
	.driver.pm = &radeon_pm_ops,
};

static int __init radeon_init(void)
{
	if (vgacon_text_force() && radeon_modeset == -1) {
		DRM_INFO("VGACON disable radeon kernel modesetting.\n");
		radeon_modeset = 0;
	}
	/* set to modesetting by default if not nomodeset */
	if (radeon_modeset == -1)
		radeon_modeset = 1;

	if (radeon_modeset == 1) {
		DRM_INFO("radeon kernel modesetting enabled.\n");
		driver = &kms_driver;
		pdriver = &radeon_kms_pci_driver;
		driver->driver_features |= DRIVER_MODESET;
		driver->num_ioctls = radeon_max_kms_ioctl;
		radeon_register_atpx_handler();

	} else {
		DRM_ERROR("No UMS support in radeon module!\n");
		return -EINVAL;
	}

	return pci_register_driver(pdriver);
}

static void __exit radeon_exit(void)
{
	pci_unregister_driver(pdriver);
	radeon_unregister_atpx_handler();
	mmu_notifier_synchronize();
}

module_init(radeon_init);
module_exit(radeon_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
