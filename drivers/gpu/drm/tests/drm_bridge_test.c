// SPDX-License-Identifier: GPL-2.0
/*
 * Kunit test for drm_bridge functions
 */
#include <linux/cleanup.h>
#include <linux/media-bus-format.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_bridge_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_managed.h>

#include <kunit/device.h>
#include <kunit/test.h>

#include "drm_kunit_edid.h"

/*
 * Mimick the typical "private" struct defined by a bridge driver, which
 * embeds a bridge plus other fields.
 *
 * Having at least one member before @bridge ensures we test non-zero
 * @bridge offset.
 */
struct drm_bridge_priv {
	unsigned int enable_count;
	unsigned int disable_count;
	struct drm_bridge bridge;
	void *data;
};

struct drm_bridge_init_priv {
	struct drm_device drm;
	/** @dev: device, only for tests not needing a whole drm_device */
	struct device *dev;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct drm_encoder encoder;
	struct drm_bridge_priv *test_bridge;
	struct drm_connector *connector;
	bool destroyed;
};

struct drm_bridge_chain_priv {
	struct drm_device drm;
	struct drm_encoder encoder;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct drm_connector *connector;
	unsigned int num_bridges;

	/**
	 * @test_bridges: array of pointers to &struct drm_bridge_priv entries
	 *                of which the first @num_bridges entries are valid.
	 */
	struct drm_bridge_priv **test_bridges;
};

static struct drm_bridge_priv *bridge_to_priv(struct drm_bridge *bridge)
{
	return container_of(bridge, struct drm_bridge_priv, bridge);
}

static void drm_test_bridge_priv_destroy(struct drm_bridge *bridge)
{
	struct drm_bridge_priv *bridge_priv = bridge_to_priv(bridge);
	struct drm_bridge_init_priv *priv = (struct drm_bridge_init_priv *)bridge_priv->data;

	priv->destroyed = true;
}

static void drm_test_bridge_atomic_enable(struct drm_bridge *bridge,
					  struct drm_atomic_commit *state)
{
	struct drm_bridge_priv *priv = bridge_to_priv(bridge);

	priv->enable_count++;
}

static void drm_test_bridge_atomic_disable(struct drm_bridge *bridge,
					   struct drm_atomic_commit *state)
{
	struct drm_bridge_priv *priv = bridge_to_priv(bridge);

	priv->disable_count++;
}

static const struct drm_bridge_funcs drm_test_bridge_atomic_funcs = {
	.destroy		= drm_test_bridge_priv_destroy,
	.atomic_enable		= drm_test_bridge_atomic_enable,
	.atomic_disable		= drm_test_bridge_atomic_disable,
	.atomic_destroy_state	= drm_atomic_helper_bridge_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_bridge_duplicate_state,
	.atomic_create_state	= drm_atomic_helper_bridge_create_state,
};

static int dummy_clear_infoframe(struct drm_bridge *bridge)
{
	return 0;
}

static int dummy_write_infoframe(struct drm_bridge *bridge, const u8 *buffer,
				 size_t len)
{
	return 0;
}

static const struct drm_bridge_funcs drm_test_bridge_bus_fmts_funcs = {
	.atomic_get_output_bus_fmts	= drm_atomic_helper_bridge_get_hdmi_output_bus_fmts,
	.atomic_destroy_state		= drm_atomic_helper_bridge_destroy_state,
	.atomic_duplicate_state		= drm_atomic_helper_bridge_duplicate_state,
	.atomic_create_state		= drm_atomic_helper_bridge_create_state,
	.hdmi_write_avi_infoframe	= dummy_write_infoframe,
	.hdmi_write_hdmi_infoframe	= dummy_write_infoframe,
	.hdmi_clear_avi_infoframe	= dummy_clear_infoframe,
	.hdmi_clear_hdmi_infoframe	= dummy_clear_infoframe,
};

/**
 * struct fmt_tuple - a tuple of input/output MEDIA_BUS_FMT_*
 */
struct fmt_tuple {
	u32 in_fmt;
	u32 out_fmt;
};

/*
 * Format mapping that only accepts RGB888, and outputs only RGB888
 */
static const struct fmt_tuple rgb8_passthrough[] = {
	{ MEDIA_BUS_FMT_RGB888_1X24, MEDIA_BUS_FMT_RGB888_1X24 },
};

/*
 * Format mapping that only accepts YUV444, and outputs only YUV444
 */
static const struct fmt_tuple yuv8_passthrough[] = {
	{ MEDIA_BUS_FMT_YUV8_1X24,   MEDIA_BUS_FMT_YUV8_1X24 },
};

/*
 * Format mapping where 8bpc RGB -> 8bpc YUV444, or ID(RGB) or ID(YUV444)
 */
static const struct fmt_tuple rgb8_to_yuv8_or_id[] = {
	{ MEDIA_BUS_FMT_RGB888_1X24, MEDIA_BUS_FMT_RGB888_1X24 },
	{ MEDIA_BUS_FMT_YUV8_1X24,   MEDIA_BUS_FMT_YUV8_1X24 },
	{ MEDIA_BUS_FMT_RGB888_1X24, MEDIA_BUS_FMT_YUV8_1X24 },
};

static const struct fmt_tuple rgb8_to_id_yuv8_or_yuv8_to_yuv422_yuv420[] = {
	{ MEDIA_BUS_FMT_RGB888_1X24, MEDIA_BUS_FMT_RGB888_1X24 },
	{ MEDIA_BUS_FMT_RGB888_1X24, MEDIA_BUS_FMT_YUV8_1X24 },
	{ MEDIA_BUS_FMT_YUV8_1X24,   MEDIA_BUS_FMT_UYVY8_1X16 },
	{ MEDIA_BUS_FMT_YUV8_1X24,   MEDIA_BUS_FMT_UYYVYY8_0_5X24 },
};

/*
 * Format mapping where 8bpc YUV444 -> 8bpc RGB, or ID(YUV444)
 */
static const struct fmt_tuple yuv8_to_rgb8_or_id[] = {
	{ MEDIA_BUS_FMT_YUV8_1X24, MEDIA_BUS_FMT_YUV8_1X24 },
	{ MEDIA_BUS_FMT_YUV8_1X24, MEDIA_BUS_FMT_RGB888_1X24 },
};

/*
 * A format mapping that acts like a video processor that generates an RGB signal
 */
static const struct fmt_tuple rgb_producer[] = {
	{ MEDIA_BUS_FMT_FIXED, MEDIA_BUS_FMT_RGB888_1X24 },
	{ MEDIA_BUS_FMT_FIXED, MEDIA_BUS_FMT_RGB101010_1X30 },
	{ MEDIA_BUS_FMT_FIXED, MEDIA_BUS_FMT_RGB121212_1X36 },
};

/*
 * A format mapping that acts like a video processor that generates an 8-bit RGB,
 * YUV444 or YUV420 signal
 */
static const struct fmt_tuple rgb_yuv444_yuv420_producer[] = {
	{ MEDIA_BUS_FMT_FIXED, MEDIA_BUS_FMT_RGB888_1X24 },
	{ MEDIA_BUS_FMT_FIXED, MEDIA_BUS_FMT_YUV8_1X24 },
	{ MEDIA_BUS_FMT_FIXED, MEDIA_BUS_FMT_UYYVYY8_0_5X24 },
};

static const struct fmt_tuple rgb8_yuv444_yuv422_passthrough[] = {
	{ MEDIA_BUS_FMT_RGB888_1X24, MEDIA_BUS_FMT_RGB888_1X24 },
	{ MEDIA_BUS_FMT_YUV8_1X24,   MEDIA_BUS_FMT_YUV8_1X24 },
	{ MEDIA_BUS_FMT_UYVY8_1X16,  MEDIA_BUS_FMT_UYVY8_1X16 },
};

static const struct fmt_tuple yuv444_yuv422_rgb8_passthrough[] = {
	{ MEDIA_BUS_FMT_YUV8_1X24,   MEDIA_BUS_FMT_YUV8_1X24 },
	{ MEDIA_BUS_FMT_UYVY8_1X16,  MEDIA_BUS_FMT_UYVY8_1X16 },
	{ MEDIA_BUS_FMT_RGB888_1X24, MEDIA_BUS_FMT_RGB888_1X24 },
};

static bool fmt_in_list(const u32 fmt, const u32 *out_fmts, const size_t num_fmts)
{
	size_t i;

	for (i = 0; i < num_fmts; i++)
		if (out_fmts[i] == fmt)
			return true;

	return false;
}

/**
 * get_tuples_out_fmts - Get unique output formats of a &struct fmt_tuple list
 * @fmt_tuples: array of &struct fmt_tuple
 * @num_fmt_tuples: number of entries in @fmt_tuples
 * @out_fmts: target array to store the unique output bus formats
 *
 * Returns the number of unique output formats, i.e. the number of entries in
 * @out_fmts that were populated with sensible values.
 */
static size_t get_tuples_out_fmts(const struct fmt_tuple *fmt_tuples,
				  const size_t num_fmt_tuples, u32 *out_fmts)
{
	size_t num_unique = 0;
	size_t i;

	for (i = 0; i < num_fmt_tuples; i++)
		if (!fmt_in_list(fmt_tuples[i].out_fmt, out_fmts, num_unique))
			out_fmts[num_unique++] = fmt_tuples[i].out_fmt;

	return num_unique;
}

#define DEFINE_FMT_FUNCS_FROM_TUPLES(name) \
static u32 *drm_test_bridge_ ## name ## _out_fmts(struct drm_bridge *bridge,			\
						  struct drm_bridge_state *bridge_state,	\
						  struct drm_crtc_state *crtc_state,		\
						  struct drm_connector_state *conn_state,	\
						  unsigned int *num_output_fmts)		\
{												\
	u32 *out_fmts = kcalloc(ARRAY_SIZE((name)), sizeof(u32), GFP_KERNEL);			\
												\
	if (out_fmts)										\
		*num_output_fmts = get_tuples_out_fmts((name), ARRAY_SIZE((name)), out_fmts);	\
	else											\
		*num_output_fmts = 0;								\
												\
	return out_fmts;									\
}												\
												\
static u32 *drm_test_bridge_ ## name ## _in_fmts(struct drm_bridge *bridge,			\
						 struct drm_bridge_state *bridge_state,		\
						 struct drm_crtc_state *crtc_state,		\
						 struct drm_connector_state *conn_state,	\
						 u32 output_fmt,				\
						 unsigned int *num_input_fmts)			\
{												\
	u32 *in_fmts = kcalloc(ARRAY_SIZE((name)), sizeof(u32), GFP_KERNEL);			\
	unsigned int num_fmts = 0;								\
	size_t i;										\
												\
	if (!in_fmts) {										\
		*num_input_fmts = 0;								\
		return NULL;									\
	}											\
												\
	for (i = 0; i < ARRAY_SIZE((name)); i++)						\
		if ((name)[i].out_fmt == output_fmt)						\
			in_fmts[num_fmts++] = (name)[i].in_fmt;					\
												\
	*num_input_fmts = num_fmts;								\
												\
	return in_fmts;										\
}

#define DRM_BRIDGE_ATOMIC_WITH_BUS_FMT_HDMI_FUNC(ident, input_fmts_func, output_fmts_func,	\
						 hdmi_write_infoframe_func,			\
						 hdmi_clear_infoframe_func)			\
static const struct drm_bridge_funcs (ident) = {						\
	.atomic_enable			= drm_test_bridge_atomic_enable,			\
	.atomic_disable			= drm_test_bridge_atomic_disable,			\
	.atomic_destroy_state		= drm_atomic_helper_bridge_destroy_state,		\
	.atomic_duplicate_state		= drm_atomic_helper_bridge_duplicate_state,		\
	.atomic_create_state		= drm_atomic_helper_bridge_create_state,		\
	.atomic_get_input_bus_fmts	= (input_fmts_func),					\
	.atomic_get_output_bus_fmts	= (output_fmts_func),					\
	.hdmi_write_avi_infoframe	= (hdmi_write_infoframe_func),				\
	.hdmi_clear_avi_infoframe	= (hdmi_clear_infoframe_func),				\
	.hdmi_write_hdmi_infoframe	= (hdmi_write_infoframe_func),				\
	.hdmi_clear_hdmi_infoframe	= (hdmi_clear_infoframe_func),				\
}

#define DRM_BRIDGE_ATOMIC_WITH_BUS_FMT_FUNC(ident, input_fmts_func, output_fmts_func)		\
	DRM_BRIDGE_ATOMIC_WITH_BUS_FMT_HDMI_FUNC(ident, input_fmts_func, output_fmts_func,	\
						 NULL, NULL)

#define DRM_BRIDGE_ATOMIC_WITH_BUS_FMT(_name)						\
	DRM_BRIDGE_ATOMIC_WITH_BUS_FMT_FUNC(_name ## _funcs,				\
					    drm_test_bridge_ ## _name ## _in_fmts,	\
					    drm_test_bridge_ ## _name ## _out_fmts)

static int drm_test_bridge_write_infoframe_stub(struct drm_bridge *bridge,
						const u8 *buffer, size_t len)
{
	return 0;
}

static int drm_test_bridge_clear_infoframe_stub(struct drm_bridge *bridge)
{
	return 0;
}

#define DRM_BRIDGE_ATOMIC_WITH_BUS_FMT_HDMI(_name)						\
	DRM_BRIDGE_ATOMIC_WITH_BUS_FMT_HDMI_FUNC(_name ## _hdmi ## _funcs,			\
						 drm_test_bridge_ ## _name ## _in_fmts,		\
						 drm_test_bridge_ ## _name ## _out_fmts,	\
						 drm_test_bridge_write_infoframe_stub,		\
						 drm_test_bridge_clear_infoframe_stub)
DEFINE_FMT_FUNCS_FROM_TUPLES(rgb8_passthrough)
DRM_BRIDGE_ATOMIC_WITH_BUS_FMT(rgb8_passthrough);

DEFINE_FMT_FUNCS_FROM_TUPLES(yuv8_passthrough)
DRM_BRIDGE_ATOMIC_WITH_BUS_FMT(yuv8_passthrough);

DEFINE_FMT_FUNCS_FROM_TUPLES(rgb8_to_yuv8_or_id)
DRM_BRIDGE_ATOMIC_WITH_BUS_FMT(rgb8_to_yuv8_or_id);

DEFINE_FMT_FUNCS_FROM_TUPLES(yuv8_to_rgb8_or_id)
DRM_BRIDGE_ATOMIC_WITH_BUS_FMT(yuv8_to_rgb8_or_id);

DEFINE_FMT_FUNCS_FROM_TUPLES(rgb_producer)
DRM_BRIDGE_ATOMIC_WITH_BUS_FMT(rgb_producer);

DEFINE_FMT_FUNCS_FROM_TUPLES(rgb_yuv444_yuv420_producer)
DRM_BRIDGE_ATOMIC_WITH_BUS_FMT(rgb_yuv444_yuv420_producer);

DEFINE_FMT_FUNCS_FROM_TUPLES(rgb8_to_id_yuv8_or_yuv8_to_yuv422_yuv420)
DRM_BRIDGE_ATOMIC_WITH_BUS_FMT(rgb8_to_id_yuv8_or_yuv8_to_yuv422_yuv420);

DEFINE_FMT_FUNCS_FROM_TUPLES(rgb8_yuv444_yuv422_passthrough)
DRM_BRIDGE_ATOMIC_WITH_BUS_FMT(rgb8_yuv444_yuv422_passthrough);

DEFINE_FMT_FUNCS_FROM_TUPLES(yuv444_yuv422_rgb8_passthrough)
DRM_BRIDGE_ATOMIC_WITH_BUS_FMT(yuv444_yuv422_rgb8_passthrough);
DRM_BRIDGE_ATOMIC_WITH_BUS_FMT_HDMI(yuv444_yuv422_rgb8_passthrough);

KUNIT_DEFINE_ACTION_WRAPPER(drm_bridge_remove_wrapper,
			    drm_bridge_remove,
			    struct drm_bridge *);

static int drm_kunit_bridge_add(struct kunit *test,
				struct drm_bridge *bridge)
{
	drm_bridge_add(bridge);

	return kunit_add_action_or_reset(test,
					 drm_bridge_remove_wrapper,
					 bridge);
}

static struct drm_bridge_init_priv *
drm_test_bridge_init(struct kunit *test, const struct drm_bridge_funcs *funcs)
{
	struct drm_bridge_init_priv *priv;
	struct drm_encoder *enc;
	struct drm_bridge *bridge;
	struct drm_device *drm;
	struct device *dev;
	int ret;

	dev = drm_kunit_helper_alloc_device(test);
	if (IS_ERR(dev))
		return ERR_CAST(dev);

	priv = drm_kunit_helper_alloc_drm_device(test, dev,
						 struct drm_bridge_init_priv, drm,
						 DRIVER_MODESET | DRIVER_ATOMIC);
	if (IS_ERR(priv))
		return ERR_CAST(priv);

	priv->test_bridge = devm_drm_bridge_alloc(dev, struct drm_bridge_priv, bridge, funcs);
	if (IS_ERR(priv->test_bridge))
		return ERR_CAST(priv->test_bridge);

	priv->test_bridge->data = priv;

	drm = &priv->drm;
	priv->plane = drm_kunit_helper_create_primary_plane(test, drm,
							    NULL,
							    NULL,
							    NULL, 0,
							    NULL);
	if (IS_ERR(priv->plane))
		return ERR_CAST(priv->plane);

	priv->crtc = drm_kunit_helper_create_crtc(test, drm,
						  priv->plane, NULL,
						  NULL,
						  NULL);
	if (IS_ERR(priv->crtc))
		return ERR_CAST(priv->crtc);

	enc = &priv->encoder;
	ret = drmm_encoder_init(drm, enc, NULL, DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		return ERR_PTR(ret);

	enc->possible_crtcs = drm_crtc_mask(priv->crtc);

	bridge = &priv->test_bridge->bridge;
	bridge->type = DRM_MODE_CONNECTOR_VIRTUAL;

	ret = drm_kunit_bridge_add(test, bridge);
	if (ret)
		return ERR_PTR(ret);

	ret = drm_bridge_attach(enc, bridge, NULL, 0);
	if (ret)
		return ERR_PTR(ret);

	priv->connector = drm_bridge_connector_init(drm, enc);
	if (IS_ERR(priv->connector))
		return ERR_CAST(priv->connector);

	drm_mode_config_reset(drm);

	return priv;
}

static struct drm_bridge_chain_priv *
drm_test_bridge_chain_init(struct kunit *test, unsigned int num_bridges,
			   const struct drm_bridge_funcs **funcs)
{
	struct drm_bridge_chain_priv *priv;
	const struct drm_edid *edid;
	struct drm_bridge *prev;
	struct drm_encoder *enc;
	struct drm_bridge *bridge;
	struct drm_device *drm;
	bool has_hdmi = false;
	struct device *dev;
	unsigned int i;
	int ret;

	dev = drm_kunit_helper_alloc_device(test);
	if (IS_ERR(dev))
		return ERR_CAST(dev);

	priv = drm_kunit_helper_alloc_drm_device(test, dev, struct drm_bridge_chain_priv,
						 drm, DRIVER_MODESET | DRIVER_ATOMIC);
	if (IS_ERR(priv))
		return ERR_CAST(priv);

	drm = &priv->drm;

	priv->test_bridges = drmm_kmalloc_array(drm, num_bridges, sizeof(*priv->test_bridges),
						GFP_KERNEL);
	if (!priv->test_bridges)
		return ERR_PTR(-ENOMEM);

	priv->num_bridges = num_bridges;

	for (i = 0; i < num_bridges; i++) {
		priv->test_bridges[i] = devm_drm_bridge_alloc(dev, struct drm_bridge_priv,
							      bridge, funcs[i]);
		if (IS_ERR(priv->test_bridges[i]))
			return ERR_CAST(priv->test_bridges[i]);

		priv->test_bridges[i]->data = priv;
	}

	priv->plane = drm_kunit_helper_create_primary_plane(test, drm, NULL, NULL,
							    NULL, 0, NULL);
	if (IS_ERR(priv->plane))
		return ERR_CAST(priv->plane);

	priv->crtc = drm_kunit_helper_create_crtc(test, drm, priv->plane, NULL,
						  NULL, NULL);
	if (IS_ERR(priv->crtc))
		return ERR_CAST(priv->crtc);

	enc = &priv->encoder;
	ret = drmm_encoder_init(drm, enc, NULL, DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		return ERR_PTR(ret);

	enc->possible_crtcs = drm_crtc_mask(priv->crtc);

	prev = NULL;
	for (i = 0; i < num_bridges; i++) {
		bridge = &priv->test_bridges[i]->bridge;
		bridge->type = DRM_MODE_CONNECTOR_VIRTUAL;

		if (bridge->funcs->hdmi_write_hdmi_infoframe) {
			has_hdmi = true;
			bridge->ops |= DRM_BRIDGE_OP_HDMI;
			bridge->type = DRM_MODE_CONNECTOR_HDMIA;
			bridge->vendor = "LNX";
			bridge->product = "KUnit";
			bridge->supported_formats = (BIT(DRM_OUTPUT_COLOR_FORMAT_RGB444) |
						     BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR444) |
						     BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR422) |
						     BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR420));
		}

		ret = drm_kunit_bridge_add(test, bridge);
		if (ret)
			return ERR_PTR(ret);

		ret = drm_bridge_attach(enc, bridge, prev, 0);
		if (ret)
			return ERR_PTR(ret);

		prev = bridge;
	}

	priv->connector = drm_bridge_connector_init(drm, enc);
	if (IS_ERR(priv->connector))
		return ERR_CAST(priv->connector);

	drm_connector_attach_encoder(priv->connector, enc);

	drm_mode_config_reset(drm);

	if (!has_hdmi)
		return priv;

	scoped_guard(mutex, &drm->mode_config.mutex) {
		edid = drm_edid_alloc(test_edid_hdmi_1080p_rgb_yuv_4k_yuv420_dc_max_200mhz,
				ARRAY_SIZE(test_edid_hdmi_1080p_rgb_yuv_4k_yuv420_dc_max_200mhz));
		if (!edid)
			return ERR_PTR(-EINVAL);

		drm_edid_connector_update(priv->connector, edid);
		KUNIT_ASSERT_GT(test, drm_edid_connector_add_modes(priv->connector), 0);

		ret = priv->connector->funcs->fill_modes(priv->connector, 4096, 4096);
	}

	return priv;
}

static struct drm_bridge_init_priv *
drm_test_bridge_hdmi_init(struct kunit *test, const struct drm_bridge_funcs *funcs,
			  unsigned int supported_formats, int max_bpc)
{
	struct drm_bridge_init_priv *priv;
	struct drm_encoder *enc;
	struct drm_bridge *bridge;
	struct drm_device *drm;
	struct device *dev;
	int ret;

	dev = drm_kunit_helper_alloc_device(test);
	if (IS_ERR(dev))
		return ERR_CAST(dev);

	priv = drm_kunit_helper_alloc_drm_device(test, dev,
						 struct drm_bridge_init_priv, drm,
						 DRIVER_MODESET | DRIVER_ATOMIC);
	if (IS_ERR(priv))
		return ERR_CAST(priv);

	priv->test_bridge = devm_drm_bridge_alloc(dev, struct drm_bridge_priv, bridge, funcs);
	if (IS_ERR(priv->test_bridge))
		return ERR_CAST(priv->test_bridge);

	priv->test_bridge->data = priv;

	drm = &priv->drm;
	priv->plane = drm_kunit_helper_create_primary_plane(test, drm,
							    NULL,
							    NULL,
							    NULL, 0,
							    NULL);
	if (IS_ERR(priv->plane))
		return ERR_CAST(priv->plane);

	priv->crtc = drm_kunit_helper_create_crtc(test, drm,
						  priv->plane, NULL,
						  NULL,
						  NULL);
	if (IS_ERR(priv->crtc))
		return ERR_CAST(priv->crtc);

	enc = &priv->encoder;
	ret = drmm_encoder_init(drm, enc, NULL, DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		return ERR_PTR(ret);

	enc->possible_crtcs = drm_crtc_mask(priv->crtc);

	bridge = &priv->test_bridge->bridge;
	bridge->type = DRM_MODE_CONNECTOR_HDMIA;
	bridge->supported_formats = supported_formats;
	bridge->max_bpc = max_bpc;
	bridge->ops |= DRM_BRIDGE_OP_HDMI;
	bridge->vendor = "LNX";
	bridge->product = "KUnit";

	ret = drm_kunit_bridge_add(test, bridge);
	if (ret)
		return ERR_PTR(ret);

	ret = drm_bridge_attach(enc, bridge, NULL, 0);
	if (ret)
		return ERR_PTR(ret);

	priv->connector = drm_bridge_connector_init(drm, enc);
	if (IS_ERR(priv->connector))
		return ERR_CAST(priv->connector);

	drm_connector_attach_encoder(priv->connector, enc);

	drm_mode_config_reset(drm);

	return priv;
}

/*
 * Test that drm_bridge_get_current_state() returns the last committed
 * state for an atomic bridge.
 */
static void drm_test_drm_bridge_get_current_state_atomic(struct kunit *test)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_bridge_init_priv *priv;
	struct drm_bridge_state *curr_bridge_state;
	struct drm_bridge_state *bridge_state;
	struct drm_atomic_commit *state;
	struct drm_bridge *bridge;
	struct drm_device *drm;
	int ret;

	priv = drm_test_bridge_init(test, &drm_test_bridge_atomic_funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	drm_modeset_acquire_init(&ctx, 0);

	drm = &priv->drm;
	state = drm_kunit_helper_atomic_state_alloc(test, drm, &ctx);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);

retry_commit:
	bridge = &priv->test_bridge->bridge;
	bridge_state = drm_atomic_get_bridge_state(state, bridge);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bridge_state);

	ret = drm_atomic_commit(state);
	if (ret == -EDEADLK) {
		drm_atomic_commit_clear(state);
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	drm_modeset_acquire_init(&ctx, 0);

retry_state:
	ret = drm_modeset_lock(&bridge->base.lock, &ctx);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_state;
	}

	curr_bridge_state = drm_bridge_get_current_state(bridge);
	KUNIT_EXPECT_PTR_EQ(test, curr_bridge_state, bridge_state);

	drm_modeset_unlock(&bridge->base.lock);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

static struct kunit_case drm_bridge_get_current_state_tests[] = {
	KUNIT_CASE(drm_test_drm_bridge_get_current_state_atomic),
	{ }
};


static struct kunit_suite drm_bridge_get_current_state_test_suite = {
	.name = "drm_test_bridge_get_current_state",
	.test_cases = drm_bridge_get_current_state_tests,
};

/*
 * Test that an atomic bridge is properly power-cycled when calling
 * drm_bridge_helper_reset_crtc().
 */
static void drm_test_drm_bridge_helper_reset_crtc_atomic(struct kunit *test)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_bridge_init_priv *priv;
	struct drm_display_mode *mode;
	struct drm_bridge_priv *bridge_priv;
	int ret;

	priv = drm_test_bridge_init(test, &drm_test_bridge_atomic_funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	mode = drm_kunit_display_mode_from_cea_vic(test, &priv->drm, 16);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mode);

	drm_modeset_acquire_init(&ctx, 0);

retry_commit:
	ret = drm_kunit_helper_enable_crtc_connector(test,
						     &priv->drm, priv->crtc,
						     priv->connector,
						     mode,
						     &ctx);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	bridge_priv = priv->test_bridge;
	KUNIT_ASSERT_EQ(test, bridge_priv->enable_count, 1);
	KUNIT_ASSERT_EQ(test, bridge_priv->disable_count, 0);

	drm_modeset_acquire_init(&ctx, 0);

retry_reset:
	ret = drm_bridge_helper_reset_crtc(&bridge_priv->bridge, &ctx);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_reset;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	KUNIT_EXPECT_EQ(test, bridge_priv->enable_count, 2);
	KUNIT_EXPECT_EQ(test, bridge_priv->disable_count, 1);
}

/*
 * Test that calling drm_bridge_helper_reset_crtc() on a disabled atomic
 * bridge will fail and not call the enable / disable callbacks
 */
static void drm_test_drm_bridge_helper_reset_crtc_atomic_disabled(struct kunit *test)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_bridge_init_priv *priv;
	struct drm_display_mode *mode;
	struct drm_bridge_priv *bridge_priv;
	int ret;

	priv = drm_test_bridge_init(test, &drm_test_bridge_atomic_funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	mode = drm_kunit_display_mode_from_cea_vic(test, &priv->drm, 16);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mode);

	bridge_priv = priv->test_bridge;
	KUNIT_ASSERT_EQ(test, bridge_priv->enable_count, 0);
	KUNIT_ASSERT_EQ(test, bridge_priv->disable_count, 0);

	drm_modeset_acquire_init(&ctx, 0);

retry_reset:
	ret = drm_bridge_helper_reset_crtc(&bridge_priv->bridge, &ctx);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_reset;
	}
	KUNIT_EXPECT_LT(test, ret, 0);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	KUNIT_EXPECT_EQ(test, bridge_priv->enable_count, 0);
	KUNIT_EXPECT_EQ(test, bridge_priv->disable_count, 0);
}

/*
 * Test that a bridge using the drm_atomic_helper_bridge_get_hdmi_output_bus_fmts()
 * function for &drm_bridge_funcs.atomic_get_output_bus_fmts behaves as expected
 * for an HDMI connector bridge. Does so by creating an HDMI bridge connector
 * with RGB444, YCBCR444, and YCBCR420 (but not YCBCR422) as supported formats,
 * sets the output depth to 8 bits per component, and then validates the returned
 * list of bus formats.
 */
static void drm_test_drm_bridge_helper_hdmi_output_bus_fmts(struct kunit *test)
{
	struct drm_connector_state *conn_state;
	struct drm_bridge_state *bridge_state;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_bridge_init_priv *priv;
	struct drm_crtc_state *crtc_state;
	struct drm_atomic_commit *state;
	struct drm_display_mode *mode;
	unsigned int num_output_fmts;
	struct drm_bridge *bridge;
	u32 *out_bus_fmts;
	int ret;

	priv = drm_test_bridge_hdmi_init(test, &drm_test_bridge_bus_fmts_funcs,
					 BIT(DRM_OUTPUT_COLOR_FORMAT_RGB444) |
					 BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR444) |
					 BIT(DRM_OUTPUT_COLOR_FORMAT_YCBCR420),
					 12);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	bridge = &priv->test_bridge->bridge;

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_kunit_helper_atomic_state_alloc(test, &priv->drm, &ctx);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);

retry_commit:
	conn_state = drm_atomic_get_connector_state(state, priv->connector);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, conn_state);

	conn_state->hdmi.output_bpc = 8;

	mode = drm_kunit_display_mode_from_cea_vic(test, &priv->drm, 16);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mode);

	ret = drm_atomic_set_crtc_for_connector(conn_state, priv->crtc);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	crtc_state = drm_atomic_get_crtc_state(state, priv->crtc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_state);

	ret = drm_atomic_set_mode_for_crtc(crtc_state, mode);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	crtc_state->enable = true;
	crtc_state->active = true;

	bridge_state = drm_atomic_get_bridge_state(state, bridge);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bridge_state);

	out_bus_fmts = bridge->funcs->atomic_get_output_bus_fmts(
		bridge, bridge_state, crtc_state, conn_state, &num_output_fmts);
	KUNIT_EXPECT_NOT_NULL(test, out_bus_fmts);
	KUNIT_EXPECT_EQ(test, num_output_fmts, 3);

	KUNIT_EXPECT_EQ(test, out_bus_fmts[0], MEDIA_BUS_FMT_RGB888_1X24);
	KUNIT_EXPECT_EQ(test, out_bus_fmts[1], MEDIA_BUS_FMT_YUV8_1X24);
	KUNIT_EXPECT_EQ(test, out_bus_fmts[2], MEDIA_BUS_FMT_UYYVYY8_0_5X24);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	kfree(out_bus_fmts);
}

static struct kunit_case drm_bridge_helper_reset_crtc_tests[] = {
	KUNIT_CASE(drm_test_drm_bridge_helper_reset_crtc_atomic),
	KUNIT_CASE(drm_test_drm_bridge_helper_reset_crtc_atomic_disabled),
	KUNIT_CASE(drm_test_drm_bridge_helper_hdmi_output_bus_fmts),
	{ }
};

static struct kunit_suite drm_bridge_helper_reset_crtc_test_suite = {
	.name = "drm_test_bridge_helper_reset_crtc",
	.test_cases = drm_bridge_helper_reset_crtc_tests,
};

static int drm_test_bridge_alloc_init(struct kunit *test)
{
	struct drm_bridge_init_priv *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	priv->dev = kunit_device_register(test, "drm-bridge-dev");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->dev);

	test->priv = priv;

	priv->test_bridge = devm_drm_bridge_alloc(priv->dev, struct drm_bridge_priv, bridge,
						  &drm_test_bridge_atomic_funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->test_bridge);

	priv->test_bridge->data = priv;

	KUNIT_ASSERT_FALSE(test, priv->destroyed);

	return 0;
}

/*
 * Test that a bridge is freed when the device is destroyed in lack of
 * other drm_bridge_get/put() operations.
 */
static void drm_test_drm_bridge_alloc_basic(struct kunit *test)
{
	struct drm_bridge_init_priv *priv = test->priv;

	KUNIT_ASSERT_FALSE(test, priv->destroyed);

	kunit_device_unregister(test, priv->dev);
	KUNIT_EXPECT_TRUE(test, priv->destroyed);
}

/*
 * Test that a bridge is not freed when the device is destroyed when there
 * is still a reference to it, and freed when that reference is put.
 */
static void drm_test_drm_bridge_alloc_get_put(struct kunit *test)
{
	struct drm_bridge_init_priv *priv = test->priv;

	KUNIT_ASSERT_FALSE(test, priv->destroyed);

	drm_bridge_get(&priv->test_bridge->bridge);
	KUNIT_EXPECT_FALSE(test, priv->destroyed);

	kunit_device_unregister(test, priv->dev);
	KUNIT_EXPECT_FALSE(test, priv->destroyed);

	drm_bridge_put(&priv->test_bridge->bridge);
	KUNIT_EXPECT_TRUE(test, priv->destroyed);
}

static struct kunit_case drm_bridge_alloc_tests[] = {
	KUNIT_CASE(drm_test_drm_bridge_alloc_basic),
	KUNIT_CASE(drm_test_drm_bridge_alloc_get_put),
	{ }
};

static struct kunit_suite drm_bridge_alloc_test_suite = {
	.name = "drm_bridge_alloc",
	.init = drm_test_bridge_alloc_init,
	.test_cases = drm_bridge_alloc_tests,
};

/**
 * drm_test_bridge_chain_verify_fmt - Verify bridge chain format selection
 * @test: pointer to KUnit test object
 * @priv: pointer to a &struct drm_bridge_chain_priv for this chain
 * @expected: constant array of &struct fmt_tuple describing the expected
 *            input and output bus formats
 * @num_expected: number of entries in @expected
 *
 * Runs the KUNIT_EXPECT clauses to verify the bridge chain format selection
 * resulted in the expected formats. If %0 is given as a format in a
 * &struct fmt_tuple, then it is understood to mean "any".
 *
 * Must be called with the modeset lock held.
 */
static void drm_test_bridge_chain_verify_fmt(struct kunit *test,
					     struct drm_bridge_chain_priv *priv,
					     const struct fmt_tuple *const expected,
					     const unsigned int num_expected)
{
	struct drm_bridge_state *bstate;
	unsigned int i = 0;

	drm_for_each_bridge_in_chain(&priv->encoder, bridge) {
		KUNIT_ASSERT_LT(test, i, num_expected);

		bstate = drm_bridge_get_current_state(bridge);
		if (expected[i].in_fmt)
			KUNIT_EXPECT_EQ(test, bstate->input_bus_cfg.format,
					expected[i].in_fmt);
		if (expected[i].out_fmt)
			KUNIT_EXPECT_EQ(test, bstate->output_bus_cfg.format,
					expected[i].out_fmt);

		i++;
	}

	KUNIT_ASSERT_EQ_MSG(test, i, num_expected,
			    "Fewer bridges (%u) than expected (%u)\n", i, num_expected);
}

/*
 * Test that constructs a bridge chain in which an RGB888 producer is chained to
 * two bridges that will convert from RGB to YUV and from YUV to RGB respectively.
 *
 * The test requests an output color_format of RGB using the color_format property,
 * so to satisfy this request, the bridge chain must take a detour over YUV.
 */
static void drm_test_bridge_rgb_yuv_rgb(struct kunit *test)
{
	static const struct drm_bridge_funcs *funcs[] = {
		&rgb_producer_funcs,
		&rgb8_to_yuv8_or_id_funcs,
		&yuv8_to_rgb8_or_id_funcs,
	};
	static const struct fmt_tuple expected[] = {
		{ MEDIA_BUS_FMT_FIXED, MEDIA_BUS_FMT_RGB888_1X24 },
		{ MEDIA_BUS_FMT_RGB888_1X24, MEDIA_BUS_FMT_YUV8_1X24 },
		{ MEDIA_BUS_FMT_YUV8_1X24, MEDIA_BUS_FMT_RGB888_1X24 },
	};
	struct drm_connector_state *conn_state;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_bridge_chain_priv *priv;
	struct drm_crtc_state *crtc_state;
	struct drm_atomic_commit *state;
	struct drm_display_mode *mode;
	int ret;

	priv = drm_test_bridge_chain_init(test, ARRAY_SIZE(funcs), funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_kunit_helper_atomic_state_alloc(test, &priv->drm, &ctx);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);

retry_commit:
	conn_state = drm_atomic_get_connector_state(state, priv->connector);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, conn_state);

	mode = drm_kunit_display_mode_from_cea_vic(test, &priv->drm, 16);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mode);

	conn_state->color_format = DRM_CONNECTOR_COLOR_FORMAT_RGB444;

	ret = drm_atomic_set_crtc_for_connector(conn_state, priv->crtc);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	crtc_state = drm_atomic_get_crtc_state(state, priv->crtc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_state);

	ret = drm_atomic_set_mode_for_crtc(crtc_state, mode);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	crtc_state->enable = true;
	crtc_state->active = true;

	ret = drm_atomic_commit(state);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_test_bridge_chain_verify_fmt(test, priv, expected, ARRAY_SIZE(expected));

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

/*
 * Test in which a bridge capable of producing RGB, YUV444 and YUV420 has to
 * produce RGB and convert with a downstream bridge in the chain to reach the
 * requested YUV444 color format, as no direct path exists between its YUV444
 * and the last bridge.
 *
 * The rationale behind this test is to devise a scenario in which naively
 * assuming any format the video processor can output, and the connector
 * requests, is the right format to pick, does not work.
 */
static void drm_test_bridge_must_convert_to_yuv444(struct kunit *test)
{
	static const struct drm_bridge_funcs *funcs[] = {
		&rgb_yuv444_yuv420_producer_funcs,
		&rgb8_passthrough_funcs,
		&rgb8_to_id_yuv8_or_yuv8_to_yuv422_yuv420_funcs,
		&rgb8_yuv444_yuv422_passthrough_funcs,
	};
	static const struct fmt_tuple expected[] = {
		{ MEDIA_BUS_FMT_FIXED, MEDIA_BUS_FMT_RGB888_1X24 },
		{ MEDIA_BUS_FMT_RGB888_1X24, MEDIA_BUS_FMT_RGB888_1X24 },
		{ MEDIA_BUS_FMT_RGB888_1X24, MEDIA_BUS_FMT_YUV8_1X24 },
		{ MEDIA_BUS_FMT_YUV8_1X24, MEDIA_BUS_FMT_YUV8_1X24 },
	};
	struct drm_connector_state *conn_state;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_bridge_chain_priv *priv;
	struct drm_crtc_state *crtc_state;
	struct drm_atomic_commit *state;
	struct drm_display_mode *mode;
	int ret;

	priv = drm_test_bridge_chain_init(test, ARRAY_SIZE(funcs), funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_kunit_helper_atomic_state_alloc(test, &priv->drm, &ctx);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);

retry_commit:
	conn_state = drm_atomic_get_connector_state(state, priv->connector);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, conn_state);

	mode = drm_kunit_display_mode_from_cea_vic(test, &priv->drm, 16);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mode);

	conn_state->color_format = DRM_CONNECTOR_COLOR_FORMAT_YCBCR444;

	ret = drm_atomic_set_crtc_for_connector(conn_state, priv->crtc);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	crtc_state = drm_atomic_get_crtc_state(state, priv->crtc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_state);

	ret = drm_atomic_set_mode_for_crtc(crtc_state, mode);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	crtc_state->enable = true;
	crtc_state->active = true;

	ret = drm_atomic_commit(state);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_test_bridge_chain_verify_fmt(test, priv, expected, ARRAY_SIZE(expected));

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

/*
 * Test which checks that no matter the order of bus formats returned by an
 * HDMI bridge, RGB is preferred on DRM_CONNECTOR_COLOR_FORMAT_AUTO if it's
 * available.
 */
static void drm_test_bridge_hdmi_auto_rgb(struct kunit *test)
{
	static const struct drm_bridge_funcs *funcs[] = {
		&rgb_yuv444_yuv420_producer_funcs,
		&yuv444_yuv422_rgb8_passthrough_hdmi_funcs,
	};
	static const struct fmt_tuple expected[] = {
		{ MEDIA_BUS_FMT_FIXED, MEDIA_BUS_FMT_RGB888_1X24 },
		{ MEDIA_BUS_FMT_RGB888_1X24, MEDIA_BUS_FMT_RGB888_1X24 },
	};
	struct drm_connector_state *conn_state;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_bridge_chain_priv *priv;
	struct drm_crtc_state *crtc_state;
	struct drm_atomic_commit *state;
	struct drm_display_mode *mode;
	int ret;

	priv = drm_test_bridge_chain_init(test, ARRAY_SIZE(funcs), funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_kunit_helper_atomic_state_alloc(test, &priv->drm, &ctx);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);

retry_commit:
	conn_state = drm_atomic_get_connector_state(state, priv->connector);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, conn_state);

	mode = drm_kunit_display_mode_from_cea_vic(test, &priv->drm, 16);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mode);

	KUNIT_ASSERT_EQ(test, conn_state->color_format, DRM_CONNECTOR_COLOR_FORMAT_AUTO);

	ret = drm_atomic_set_crtc_for_connector(conn_state, priv->crtc);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	crtc_state = drm_atomic_get_crtc_state(state, priv->crtc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_state);

	ret = drm_atomic_set_mode_for_crtc(crtc_state, mode);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	crtc_state->enable = true;
	crtc_state->active = true;

	ret = drm_atomic_commit(state);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, conn_state->hdmi.output_format, DRM_OUTPUT_COLOR_FORMAT_RGB444);

	drm_test_bridge_chain_verify_fmt(test, priv, expected, ARRAY_SIZE(expected));

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

/*
 * Test which checks that DRM_CONNECTOR_COLOR_FORMAT_AUTO on non-HDMI connectors
 * will result in the first bus format on the output to be picked.
 */
static void drm_test_bridge_auto_first(struct kunit *test)
{
	static const struct drm_bridge_funcs *funcs[] = {
		&rgb_yuv444_yuv420_producer_funcs,
		&yuv444_yuv422_rgb8_passthrough_funcs,
	};
	static const struct fmt_tuple expected[] = {
		{ MEDIA_BUS_FMT_FIXED, MEDIA_BUS_FMT_YUV8_1X24 },
		{ MEDIA_BUS_FMT_YUV8_1X24, MEDIA_BUS_FMT_YUV8_1X24 },
	};
	struct drm_connector_state *conn_state;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_bridge_chain_priv *priv;
	struct drm_crtc_state *crtc_state;
	struct drm_atomic_commit *state;
	struct drm_display_mode *mode;
	int ret;

	priv = drm_test_bridge_chain_init(test, ARRAY_SIZE(funcs), funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_kunit_helper_atomic_state_alloc(test, &priv->drm, &ctx);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);

retry_commit:
	conn_state = drm_atomic_get_connector_state(state, priv->connector);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, conn_state);

	mode = drm_kunit_display_mode_from_cea_vic(test, &priv->drm, 16);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mode);

	KUNIT_ASSERT_EQ(test, conn_state->color_format, DRM_CONNECTOR_COLOR_FORMAT_AUTO);

	ret = drm_atomic_set_crtc_for_connector(conn_state, priv->crtc);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	crtc_state = drm_atomic_get_crtc_state(state, priv->crtc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_state);

	ret = drm_atomic_set_mode_for_crtc(crtc_state, mode);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	crtc_state->enable = true;
	crtc_state->active = true;

	ret = drm_atomic_commit(state);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	drm_test_bridge_chain_verify_fmt(test, priv, expected, ARRAY_SIZE(expected));

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

/*
 * Test which checks that in a configuration of bridge chains where an RGB
 * producer is hooked to a YUV444-only pass-through, the atomic commit fails as
 * the bridge format selection cannot find a valid sequence of bus formats.
 */
static void drm_test_bridge_rgb_yuv_no_path(struct kunit *test)
{
	static const struct drm_bridge_funcs *funcs[] = {
		&rgb_producer_funcs,
		&yuv8_passthrough_funcs,
		&rgb8_yuv444_yuv422_passthrough_funcs,
	};
	struct drm_connector_state *conn_state;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_bridge_chain_priv *priv;
	struct drm_crtc_state *crtc_state;
	struct drm_atomic_commit *state;
	struct drm_display_mode *mode;
	int ret;

	priv = drm_test_bridge_chain_init(test, ARRAY_SIZE(funcs), funcs);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_kunit_helper_atomic_state_alloc(test, &priv->drm, &ctx);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);

retry_commit:
	conn_state = drm_atomic_get_connector_state(state, priv->connector);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, conn_state);

	mode = drm_kunit_display_mode_from_cea_vic(test, &priv->drm, 16);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mode);

	ret = drm_atomic_set_crtc_for_connector(conn_state, priv->crtc);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	crtc_state = drm_atomic_get_crtc_state(state, priv->crtc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_state);

	ret = drm_atomic_set_mode_for_crtc(crtc_state, mode);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_ASSERT_EQ(test, ret, 0);

	crtc_state->enable = true;
	crtc_state->active = true;

	ret = drm_atomic_commit(state);
	if (ret == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry_commit;
	}
	KUNIT_EXPECT_EQ(test, ret, -ENOTSUPP);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

static struct kunit_case drm_bridge_bus_fmt_tests[] = {
	KUNIT_CASE(drm_test_bridge_rgb_yuv_rgb),
	KUNIT_CASE(drm_test_bridge_must_convert_to_yuv444),
	KUNIT_CASE(drm_test_bridge_hdmi_auto_rgb),
	KUNIT_CASE(drm_test_bridge_auto_first),
	KUNIT_CASE(drm_test_bridge_rgb_yuv_no_path),
	{ }
};

static struct kunit_suite drm_bridge_bus_fmt_test_suite = {
	.name = "drm_bridge_bus_fmt",
	.test_cases = drm_bridge_bus_fmt_tests,
};

kunit_test_suites(
	&drm_bridge_get_current_state_test_suite,
	&drm_bridge_helper_reset_crtc_test_suite,
	&drm_bridge_alloc_test_suite,
	&drm_bridge_bus_fmt_test_suite,
);

MODULE_AUTHOR("Maxime Ripard <mripard@kernel.org>");
MODULE_AUTHOR("Luca Ceresoli <luca.ceresoli@bootlin.com>");
MODULE_AUTHOR("Nicolas Frattaroli <nicolas.frattaroli@collabora.com>");

MODULE_DESCRIPTION("Kunit test for drm_bridge functions");
MODULE_LICENSE("GPL");
