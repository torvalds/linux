#ifndef ARMADA_PLANE_H
#define ARMADA_PLANE_H

struct armada_plane_state {
	struct drm_plane_state base;
	u32 src_hw;
	u32 dst_yx;
	u32 dst_hw;
	u32 addrs[2][3];
	u16 pitches[3];
	bool interlace;
};

#define to_armada_plane_state(st) \
	container_of(st, struct armada_plane_state, base)
#define armada_src_hw(state) to_armada_plane_state(state)->src_hw
#define armada_dst_yx(state) to_armada_plane_state(state)->dst_yx
#define armada_dst_hw(state) to_armada_plane_state(state)->dst_hw
#define armada_addr(state, f, p) to_armada_plane_state(state)->addrs[f][p]
#define armada_pitch(state, n) to_armada_plane_state(state)->pitches[n]

void armada_drm_plane_calc(struct drm_plane_state *state, u32 addrs[2][3],
	u16 pitches[3], bool interlaced);
int armada_drm_plane_prepare_fb(struct drm_plane *plane,
	struct drm_plane_state *state);
void armada_drm_plane_cleanup_fb(struct drm_plane *plane,
	struct drm_plane_state *old_state);
int armada_drm_plane_atomic_check(struct drm_plane *plane,
	struct drm_atomic_state *state);
void armada_plane_reset(struct drm_plane *plane);
struct drm_plane_state *armada_plane_duplicate_state(struct drm_plane *plane);
void armada_plane_destroy_state(struct drm_plane *plane,
				struct drm_plane_state *state);

int armada_drm_primary_plane_init(struct drm_device *drm,
	struct drm_plane *primary);

#endif
