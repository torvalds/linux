# SPDX-License-Identifier: GPL-2.0

subdir-ccflags-y += -I$(src)/..

drm_client_lib-y := drm_client_setup.o
drm_client_lib-$(CONFIG_DRM_CLIENT_LOG) += drm_log.o
drm_client_lib-$(CONFIG_DRM_FBDEV_EMULATION) += drm_fbdev_client.o
obj-$(CONFIG_DRM_CLIENT_LIB) += drm_client_lib.o
