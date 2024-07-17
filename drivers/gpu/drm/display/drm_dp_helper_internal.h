/* SPDX-License-Identifier: MIT */

#ifndef DRM_DP_HELPER_INTERNAL_H
#define DRM_DP_HELPER_INTERNAL_H

struct drm_dp_aux;

#ifdef CONFIG_DRM_DISPLAY_DP_AUX_CHARDEV
int drm_dp_aux_dev_init(void);
void drm_dp_aux_dev_exit(void);
int drm_dp_aux_register_devnode(struct drm_dp_aux *aux);
void drm_dp_aux_unregister_devnode(struct drm_dp_aux *aux);
#else
static inline int drm_dp_aux_dev_init(void)
{
	return 0;
}

static inline void drm_dp_aux_dev_exit(void)
{
}

static inline int drm_dp_aux_register_devnode(struct drm_dp_aux *aux)
{
	return 0;
}

static inline void drm_dp_aux_unregister_devnode(struct drm_dp_aux *aux)
{
}
#endif

#endif
