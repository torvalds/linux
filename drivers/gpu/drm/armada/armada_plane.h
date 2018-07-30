#ifndef ARMADA_PLANE_H
#define ARMADA_PLANE_H

void armada_drm_plane_calc_addrs(u32 *addrs, struct drm_framebuffer *fb,
	int x, int y);
int armada_drm_plane_prepare_fb(struct drm_plane *plane,
	struct drm_plane_state *state);
void armada_drm_plane_cleanup_fb(struct drm_plane *plane,
	struct drm_plane_state *old_state);
int armada_drm_plane_atomic_check(struct drm_plane *plane,
	struct drm_plane_state *state);
int armada_drm_plane_init(struct armada_plane *plane);
int armada_drm_primary_plane_init(struct drm_device *drm,
	struct armada_plane *primary);

#endif
