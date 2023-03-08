/* SPDX-License-Identifier: GPL-2.0 */

#ifndef VC4_MOCK_H_
#define VC4_MOCK_H_

#include "../vc4_drv.h"

static inline
struct drm_crtc *vc4_find_crtc_for_encoder(struct kunit *test,
					   struct drm_device *drm,
					   struct drm_encoder *encoder)
{
	struct drm_crtc *crtc;

	KUNIT_ASSERT_EQ(test, hweight32(encoder->possible_crtcs), 1);

	drm_for_each_crtc(crtc, drm)
		if (encoder->possible_crtcs & drm_crtc_mask(crtc))
			return crtc;

	return NULL;
}

struct vc4_dummy_plane {
	struct vc4_plane plane;
};

struct vc4_dummy_plane *vc4_dummy_plane(struct kunit *test,
					struct drm_device *drm,
					enum drm_plane_type type);

struct vc4_dummy_crtc {
	struct vc4_crtc crtc;
};

struct vc4_dummy_crtc *vc4_mock_pv(struct kunit *test,
				   struct drm_device *drm,
				   struct drm_plane *plane,
				   const struct vc4_crtc_data *data);

struct vc4_dummy_output {
	struct vc4_encoder encoder;
	struct drm_connector connector;
};

struct vc4_dummy_output *vc4_dummy_output(struct kunit *test,
					  struct drm_device *drm,
					  struct drm_crtc *crtc,
					  enum vc4_encoder_type vc4_encoder_type,
					  unsigned int kms_encoder_type,
					  unsigned int connector_type);

struct vc4_dev *vc4_mock_device(struct kunit *test);
struct vc4_dev *vc5_mock_device(struct kunit *test);

int vc4_mock_atomic_add_output(struct kunit *test,
			       struct drm_atomic_state *state,
			       enum vc4_encoder_type type);
int vc4_mock_atomic_del_output(struct kunit *test,
			       struct drm_atomic_state *state,
			       enum vc4_encoder_type type);

#endif // VC4_MOCK_H_
