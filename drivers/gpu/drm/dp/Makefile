# SPDX-License-Identifier: MIT

obj-$(CONFIG_DRM_DP_AUX_BUS) += drm_dp_aux_bus.o

drm_dp_helper-y := drm_dp.o drm_dp_dual_mode_helper.o drm_dp_helper_mod.o drm_dp_mst_topology.o
drm_dp_helper-$(CONFIG_DRM_DP_AUX_CHARDEV) += drm_dp_aux_dev.o
drm_dp_helper-$(CONFIG_DRM_DP_CEC) += drm_dp_cec.o

obj-$(CONFIG_DRM_DP_HELPER) += drm_dp_helper.o
