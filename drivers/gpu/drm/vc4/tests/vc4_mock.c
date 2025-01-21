// SPDX-License-Identifier: GPL-2.0

#include <drm/drm_drv.h>
#include <drm/drm_kunit_helpers.h>

#include <kunit/test.h>

#include "vc4_mock.h"

struct vc4_mock_output_desc {
	enum vc4_encoder_type	vc4_encoder_type;
	unsigned int		encoder_type;
	unsigned int		connector_type;
};

#define VC4_MOCK_OUTPUT_DESC(_vc4_type, _etype, _ctype)					\
	{										\
		.vc4_encoder_type = _vc4_type,						\
		.encoder_type = _etype,							\
		.connector_type = _ctype,						\
	}

struct vc4_mock_pipe_desc {
	const struct vc4_crtc_data *data;
	const struct vc4_mock_output_desc *outputs;
	unsigned int noutputs;
};

#define VC4_MOCK_CRTC_DESC(_data, ...)							\
	{										\
		.data = _data,								\
		.outputs = (struct vc4_mock_output_desc[]) { __VA_ARGS__ },		\
		.noutputs = sizeof((struct vc4_mock_output_desc[]) { __VA_ARGS__ }) /	\
			     sizeof(struct vc4_mock_output_desc),			\
	}

#define VC4_MOCK_PIXELVALVE_DESC(_data, ...)						\
	VC4_MOCK_CRTC_DESC(&(_data)->base, __VA_ARGS__)

struct vc4_mock_desc {
	const struct vc4_mock_pipe_desc *pipes;
	unsigned int npipes;
};

#define VC4_MOCK_DESC(...)								\
	{										\
		.pipes = (struct vc4_mock_pipe_desc[]) { __VA_ARGS__ },			\
		.npipes = sizeof((struct vc4_mock_pipe_desc[]) { __VA_ARGS__ }) /	\
			     sizeof(struct vc4_mock_pipe_desc),				\
	}

static const struct vc4_mock_desc vc4_mock =
	VC4_MOCK_DESC(
		VC4_MOCK_CRTC_DESC(&vc4_txp_crtc_data,
				   VC4_MOCK_OUTPUT_DESC(VC4_ENCODER_TYPE_TXP,
							DRM_MODE_ENCODER_VIRTUAL,
							DRM_MODE_CONNECTOR_WRITEBACK)),
		VC4_MOCK_PIXELVALVE_DESC(&bcm2835_pv0_data,
					 VC4_MOCK_OUTPUT_DESC(VC4_ENCODER_TYPE_DSI0,
							      DRM_MODE_ENCODER_DSI,
							      DRM_MODE_CONNECTOR_DSI),
					 VC4_MOCK_OUTPUT_DESC(VC4_ENCODER_TYPE_DPI,
							      DRM_MODE_ENCODER_DPI,
							      DRM_MODE_CONNECTOR_DPI)),
		VC4_MOCK_PIXELVALVE_DESC(&bcm2835_pv1_data,
					 VC4_MOCK_OUTPUT_DESC(VC4_ENCODER_TYPE_DSI1,
							      DRM_MODE_ENCODER_DSI,
							      DRM_MODE_CONNECTOR_DSI)),
		VC4_MOCK_PIXELVALVE_DESC(&bcm2835_pv2_data,
					 VC4_MOCK_OUTPUT_DESC(VC4_ENCODER_TYPE_HDMI0,
							      DRM_MODE_ENCODER_TMDS,
							      DRM_MODE_CONNECTOR_HDMIA),
					 VC4_MOCK_OUTPUT_DESC(VC4_ENCODER_TYPE_VEC,
							      DRM_MODE_ENCODER_TVDAC,
							      DRM_MODE_CONNECTOR_Composite)),
);

static const struct vc4_mock_desc vc5_mock =
	VC4_MOCK_DESC(
		VC4_MOCK_CRTC_DESC(&vc4_txp_crtc_data,
				   VC4_MOCK_OUTPUT_DESC(VC4_ENCODER_TYPE_TXP,
							DRM_MODE_ENCODER_VIRTUAL,
							DRM_MODE_CONNECTOR_WRITEBACK)),
		VC4_MOCK_PIXELVALVE_DESC(&bcm2711_pv0_data,
					 VC4_MOCK_OUTPUT_DESC(VC4_ENCODER_TYPE_DSI0,
							      DRM_MODE_ENCODER_DSI,
							      DRM_MODE_CONNECTOR_DSI),
					 VC4_MOCK_OUTPUT_DESC(VC4_ENCODER_TYPE_DPI,
							      DRM_MODE_ENCODER_DPI,
							      DRM_MODE_CONNECTOR_DPI)),
		VC4_MOCK_PIXELVALVE_DESC(&bcm2711_pv1_data,
					 VC4_MOCK_OUTPUT_DESC(VC4_ENCODER_TYPE_DSI1,
							      DRM_MODE_ENCODER_DSI,
							      DRM_MODE_CONNECTOR_DSI)),
		VC4_MOCK_PIXELVALVE_DESC(&bcm2711_pv2_data,
					 VC4_MOCK_OUTPUT_DESC(VC4_ENCODER_TYPE_HDMI0,
							      DRM_MODE_ENCODER_TMDS,
							      DRM_MODE_CONNECTOR_HDMIA)),
		VC4_MOCK_PIXELVALVE_DESC(&bcm2711_pv3_data,
					 VC4_MOCK_OUTPUT_DESC(VC4_ENCODER_TYPE_VEC,
							      DRM_MODE_ENCODER_TVDAC,
							      DRM_MODE_CONNECTOR_Composite)),
		VC4_MOCK_PIXELVALVE_DESC(&bcm2711_pv4_data,
					 VC4_MOCK_OUTPUT_DESC(VC4_ENCODER_TYPE_HDMI1,
							      DRM_MODE_ENCODER_TMDS,
							      DRM_MODE_CONNECTOR_HDMIA)),
);

static int __build_one_pipe(struct kunit *test, struct drm_device *drm,
			    const struct vc4_mock_pipe_desc *pipe)
{
	struct drm_plane *plane;
	struct vc4_dummy_crtc *dummy_crtc;
	struct drm_crtc *crtc;
	unsigned int i;

	plane = vc4_dummy_plane(test, drm, DRM_PLANE_TYPE_PRIMARY);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, plane);

	dummy_crtc = vc4_mock_pv(test, drm, plane, pipe->data);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dummy_crtc);

	crtc = &dummy_crtc->crtc.base;
	for (i = 0; i < pipe->noutputs; i++) {
		const struct vc4_mock_output_desc *mock_output = &pipe->outputs[i];
		struct vc4_dummy_output *dummy_output;

		dummy_output = vc4_dummy_output(test, drm, crtc,
						mock_output->vc4_encoder_type,
						mock_output->encoder_type,
						mock_output->connector_type);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dummy_output);
	}

	return 0;
}

static int __build_mock(struct kunit *test, struct drm_device *drm,
			const struct vc4_mock_desc *mock)
{
	unsigned int i;

	for (i = 0; i < mock->npipes; i++) {
		const struct vc4_mock_pipe_desc *pipe = &mock->pipes[i];
		int ret;

		ret = __build_one_pipe(test, drm, pipe);
		KUNIT_ASSERT_EQ(test, ret, 0);
	}

	return 0;
}

KUNIT_DEFINE_ACTION_WRAPPER(kunit_action_drm_dev_unregister,
			    drm_dev_unregister,
			    struct drm_device *);

static struct vc4_dev *__mock_device(struct kunit *test, enum vc4_gen gen)
{
	struct drm_device *drm;
	const struct drm_driver *drv = (gen == VC4_GEN_5) ? &vc5_drm_driver : &vc4_drm_driver;
	const struct vc4_mock_desc *desc = (gen == VC4_GEN_5) ? &vc5_mock : &vc4_mock;
	struct vc4_dev *vc4;
	struct device *dev;
	int ret;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	vc4 = drm_kunit_helper_alloc_drm_device_with_driver(test, dev,
							    struct vc4_dev, base,
							    drv);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, vc4);

	vc4->dev = dev;
	vc4->gen = gen;

	vc4->hvs = __vc4_hvs_alloc(vc4, NULL, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, vc4->hvs);

	drm = &vc4->base;
	ret = __build_mock(test, drm, desc);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = vc4_kms_load(drm);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = drm_dev_register(drm, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_add_action_or_reset(test,
					kunit_action_drm_dev_unregister,
					drm);
	KUNIT_ASSERT_EQ(test, ret, 0);

	return vc4;
}

struct vc4_dev *vc4_mock_device(struct kunit *test)
{
	return __mock_device(test, VC4_GEN_4);
}

struct vc4_dev *vc5_mock_device(struct kunit *test)
{
	return __mock_device(test, VC4_GEN_5);
}
