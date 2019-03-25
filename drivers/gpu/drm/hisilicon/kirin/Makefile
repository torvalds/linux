# SPDX-License-Identifier: GPL-2.0-only
EXTRA_CFLAGS += \
		-Iinclude/drm

kirin-drm-y := kirin_drm_drv.o
kirin-drm-$(CONFIG_DRM_HISI_KIRIN620) += kirin_drm_ade.o
kirin-drm-$(CONFIG_DRM_HISI_KIRIN960) += kirin_drm_dpe.o

kirin-dsi-y := kirin_drm_dsi.o
kirin-dsi-$(CONFIG_DRM_HISI_KIRIN620) += kirin/dw_drm_dsi.o
kirin-dsi-$(CONFIG_DRM_HISI_KIRIN960) += kirin960/dw_drm_dsi.o

obj-$(CONFIG_DRM_HISI_KIRIN) += kirin-drm.o kirin-dsi.o
