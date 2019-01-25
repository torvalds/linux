#ifndef ARMADA_PLANE_H
#define ARMADA_PLANE_H

#define armada_src_hw(state)	armada_rect_hw_fp(&(state)->src)
#define armada_dst_yx(state)	armada_rect_yx(&(state)->dst)
#define armada_dst_hw(state)	armada_rect_hw(&(state)->dst)

void armada_drm_plane_calc(struct drm_plane_state *state, u32 addrs[2][3],
	u16 pitches[3], bool interlaced);
int armada_drm_plane_prepare_fb(struct drm_plane *plane,
	struct drm_plane_state *state);
void armada_drm_plane_cleanup_fb(struct drm_plane *plane,
	struct drm_plane_state *old_state);
int armada_drm_plane_atomic_check(struct drm_plane *plane,
	struct drm_plane_state *state);
int armada_drm_primary_plane_init(struct drm_device *drm,
	struct drm_plane *primary);

#endif
