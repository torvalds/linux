# SPDX-License-Identifier: GPL-2.0

# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

CFLAGS-$(CONFIG_DRM_USE_DYNAMIC_DEBUG)	+= -DDYNAMIC_DEBUG_MODULE

# Unconditionally enable W=1 warnings locally
# --- begin copy-paste W=1 warnings from scripts/Makefile.extrawarn
subdir-ccflags-y += -Wextra -Wunused -Wno-unused-parameter
subdir-ccflags-y += $(call cc-option, -Wrestrict)
subdir-ccflags-y += -Wmissing-format-attribute
subdir-ccflags-y += -Wold-style-definition
subdir-ccflags-y += -Wmissing-include-dirs
subdir-ccflags-y += $(call cc-option, -Wunused-but-set-variable)
subdir-ccflags-y += $(call cc-option, -Wunused-const-variable)
subdir-ccflags-y += $(call cc-option, -Wpacked-not-aligned)
subdir-ccflags-y += $(call cc-option, -Wformat-overflow)
# FIXME: fix -Wformat-truncation warnings and uncomment
#subdir-ccflags-y += $(call cc-option, -Wformat-truncation)
subdir-ccflags-y += $(call cc-option, -Wstringop-truncation)
# The following turn off the warnings enabled by -Wextra
ifeq ($(findstring 2, $(KBUILD_EXTRA_WARN)),)
subdir-ccflags-y += -Wno-missing-field-initializers
subdir-ccflags-y += -Wno-type-limits
subdir-ccflags-y += -Wno-shift-negative-value
endif
ifeq ($(findstring 3, $(KBUILD_EXTRA_WARN)),)
subdir-ccflags-y += -Wno-sign-compare
endif
# --- end copy-paste

# Enable -Werror in CI and development
subdir-ccflags-$(CONFIG_DRM_WERROR) += -Werror

drm-y := \
	drm_aperture.o \
	drm_atomic.o \
	drm_atomic_uapi.o \
	drm_auth.o \
	drm_blend.o \
	drm_bridge.o \
	drm_cache.o \
	drm_client.o \
	drm_client_modeset.o \
	drm_color_mgmt.o \
	drm_connector.o \
	drm_crtc.o \
	drm_displayid.o \
	drm_drv.o \
	drm_dumb_buffers.o \
	drm_edid.o \
	drm_eld.o \
	drm_encoder.o \
	drm_file.o \
	drm_fourcc.o \
	drm_framebuffer.o \
	drm_gem.o \
	drm_ioctl.o \
	drm_lease.o \
	drm_managed.o \
	drm_mm.o \
	drm_mode_config.o \
	drm_mode_object.o \
	drm_modes.o \
	drm_modeset_lock.o \
	drm_plane.o \
	drm_prime.o \
	drm_print.o \
	drm_property.o \
	drm_syncobj.o \
	drm_sysfs.o \
	drm_trace_points.o \
	drm_vblank.o \
	drm_vblank_work.o \
	drm_vma_manager.o \
	drm_writeback.o
drm-$(CONFIG_DRM_LIB_RANDOM) += lib/drm_random.o
drm-$(CONFIG_COMPAT) += drm_ioc32.o
drm-$(CONFIG_DRM_PANEL) += drm_panel.o
drm-$(CONFIG_OF) += drm_of.o
drm-$(CONFIG_PCI) += drm_pci.o
drm-$(CONFIG_DEBUG_FS) += \
	drm_debugfs.o \
	drm_debugfs_crc.o
drm-$(CONFIG_DRM_LOAD_EDID_FIRMWARE) += drm_edid_load.o
drm-$(CONFIG_DRM_PRIVACY_SCREEN) += \
	drm_privacy_screen.o \
	drm_privacy_screen_x86.o
drm-$(CONFIG_DRM_ACCEL) += ../../accel/drm_accel.o
drm-$(CONFIG_DRM_PANIC) += drm_panic.o
obj-$(CONFIG_DRM)	+= drm.o

obj-$(CONFIG_DRM_PANEL_ORIENTATION_QUIRKS) += drm_panel_orientation_quirks.o

#
# Memory-management helpers
#
#
obj-$(CONFIG_DRM_EXEC) += drm_exec.o
obj-$(CONFIG_DRM_GPUVM) += drm_gpuvm.o

obj-$(CONFIG_DRM_BUDDY) += drm_buddy.o

drm_dma_helper-y := drm_gem_dma_helper.o
drm_dma_helper-$(CONFIG_DRM_FBDEV_EMULATION) += drm_fbdev_dma.o
drm_dma_helper-$(CONFIG_DRM_KMS_HELPER) += drm_fb_dma_helper.o
obj-$(CONFIG_DRM_GEM_DMA_HELPER) += drm_dma_helper.o

drm_shmem_helper-y := drm_gem_shmem_helper.o
drm_shmem_helper-$(CONFIG_DRM_FBDEV_EMULATION) += drm_fbdev_shmem.o
obj-$(CONFIG_DRM_GEM_SHMEM_HELPER) += drm_shmem_helper.o

drm_suballoc_helper-y := drm_suballoc.o
obj-$(CONFIG_DRM_SUBALLOC_HELPER) += drm_suballoc_helper.o

drm_vram_helper-y := drm_gem_vram_helper.o
obj-$(CONFIG_DRM_VRAM_HELPER) += drm_vram_helper.o

drm_ttm_helper-y := drm_gem_ttm_helper.o
drm_ttm_helper-$(CONFIG_DRM_FBDEV_EMULATION) += drm_fbdev_ttm.o
obj-$(CONFIG_DRM_TTM_HELPER) += drm_ttm_helper.o

#
# Modesetting helpers
#

drm_kms_helper-y := \
	drm_atomic_helper.o \
	drm_atomic_state_helper.o \
	drm_crtc_helper.o \
	drm_damage_helper.o \
	drm_encoder_slave.o \
	drm_flip_work.o \
	drm_format_helper.o \
	drm_gem_atomic_helper.o \
	drm_gem_framebuffer_helper.o \
	drm_kms_helper_common.o \
	drm_modeset_helper.o \
	drm_plane_helper.o \
	drm_probe_helper.o \
	drm_rect.o \
	drm_self_refresh_helper.o \
	drm_simple_kms_helper.o
drm_kms_helper-$(CONFIG_DRM_PANEL_BRIDGE) += bridge/panel.o
drm_kms_helper-$(CONFIG_DRM_FBDEV_EMULATION) += drm_fb_helper.o
obj-$(CONFIG_DRM_KMS_HELPER) += drm_kms_helper.o

#
# Drivers and the rest
#

obj-y			+= tests/

obj-$(CONFIG_DRM_MIPI_DBI) += drm_mipi_dbi.o
obj-$(CONFIG_DRM_MIPI_DSI) += drm_mipi_dsi.o
obj-y			+= arm/
obj-y			+= display/
obj-$(CONFIG_DRM_TTM)	+= ttm/
obj-$(CONFIG_DRM_SCHED)	+= scheduler/
obj-$(CONFIG_DRM_RADEON)+= radeon/
obj-$(CONFIG_DRM_AMDGPU)+= amd/amdgpu/
obj-$(CONFIG_DRM_AMDGPU)+= amd/amdxcp/
obj-$(CONFIG_DRM_I915)	+= i915/
obj-$(CONFIG_DRM_XE)	+= xe/
obj-$(CONFIG_DRM_KMB_DISPLAY)  += kmb/
obj-$(CONFIG_DRM_MGAG200) += mgag200/
obj-$(CONFIG_DRM_V3D)  += v3d/
obj-$(CONFIG_DRM_VC4)  += vc4/
obj-$(CONFIG_DRM_VMWGFX)+= vmwgfx/
obj-$(CONFIG_DRM_VGEM)	+= vgem/
obj-$(CONFIG_DRM_VKMS)	+= vkms/
obj-$(CONFIG_DRM_NOUVEAU) +=nouveau/
obj-$(CONFIG_DRM_EXYNOS) +=exynos/
obj-$(CONFIG_DRM_ROCKCHIP) +=rockchip/
obj-$(CONFIG_DRM_GMA500) += gma500/
obj-$(CONFIG_DRM_UDL) += udl/
obj-$(CONFIG_DRM_AST) += ast/
obj-$(CONFIG_DRM_ARMADA) += armada/
obj-$(CONFIG_DRM_ATMEL_HLCDC)	+= atmel-hlcdc/
obj-y			+= renesas/
obj-y			+= omapdrm/
obj-$(CONFIG_DRM_SUN4I) += sun4i/
obj-y			+= tilcdc/
obj-$(CONFIG_DRM_QXL) += qxl/
obj-$(CONFIG_DRM_VIRTIO_GPU) += virtio/
obj-$(CONFIG_DRM_MSM) += msm/
obj-$(CONFIG_DRM_TEGRA) += tegra/
obj-$(CONFIG_DRM_STM) += stm/
obj-$(CONFIG_DRM_STI) += sti/
obj-y 			+= imx/
obj-$(CONFIG_DRM_INGENIC) += ingenic/
obj-$(CONFIG_DRM_LOGICVC) += logicvc/
obj-$(CONFIG_DRM_MEDIATEK) += mediatek/
obj-$(CONFIG_DRM_MESON)	+= meson/
obj-y			+= i2c/
obj-y			+= panel/
obj-y			+= bridge/
obj-$(CONFIG_DRM_FSL_DCU) += fsl-dcu/
obj-$(CONFIG_DRM_ETNAVIV) += etnaviv/
obj-y			+= hisilicon/
obj-y			+= mxsfb/
obj-y			+= tiny/
obj-$(CONFIG_DRM_PL111) += pl111/
obj-$(CONFIG_DRM_TVE200) += tve200/
obj-$(CONFIG_DRM_XEN) += xen/
obj-$(CONFIG_DRM_VBOXVIDEO) += vboxvideo/
obj-$(CONFIG_DRM_LIMA)  += lima/
obj-$(CONFIG_DRM_PANFROST) += panfrost/
obj-$(CONFIG_DRM_PANTHOR) += panthor/
obj-$(CONFIG_DRM_ASPEED_GFX) += aspeed/
obj-$(CONFIG_DRM_MCDE) += mcde/
obj-$(CONFIG_DRM_TIDSS) += tidss/
obj-y			+= xlnx/
obj-y			+= gud/
obj-$(CONFIG_DRM_HYPERV) += hyperv/
obj-y			+= solomon/
obj-$(CONFIG_DRM_SPRD) += sprd/
obj-$(CONFIG_DRM_LOONGSON) += loongson/
obj-$(CONFIG_DRM_POWERVR) += imagination/
