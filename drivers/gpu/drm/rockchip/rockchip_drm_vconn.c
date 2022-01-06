// SPDX-License-Identifier: GPL-2.0+
#include <drm/drm_of.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define XRES_DEF  1920
#define YRES_DEF  1080

struct vconn_device {
	struct rockchip_vconn *vconn;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct list_head list;
	int encoder_type;
	int output_type;
	int output_mode;
	int bus_format;
	int if_id;
	int vp_id_mask;
};

struct rockchip_vconn {
	struct device *dev;
	struct drm_device *drm_dev;
	struct platform_device *pdev;
	struct list_head list_head;
};

#define to_vconn_device(x)	container_of(x, struct vconn_device, x)

static const struct drm_display_mode edid_cea_modes_1[] = {
	/* 1 - 640x480@60Hz 4:3 */
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 25175, 640, 656,
		   752, 800, 0, 480, 490, 492, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
	/* 2 - 720x480@60Hz 4:3 */
	{ DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 27000, 720, 736,
		   798, 858, 0, 480, 489, 495, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
	/* 3 - 720x480@60Hz 16:9 */
	{ DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 27000, 720, 736,
		   798, 858, 0, 480, 489, 495, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 4 - 1280x720@60Hz 16:9 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 5 - 1920x1080i@60Hz 16:9 */
	{ DRM_MODE("1920x1080i", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1094, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 6 - 720(1440)x480i@60Hz 4:3 */
	{ DRM_MODE("720x480i", DRM_MODE_TYPE_DRIVER, 13500, 720, 739,
		   801, 858, 0, 480, 488, 494, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
	/* 7 - 720(1440)x480i@60Hz 16:9 */
	{ DRM_MODE("720x480i", DRM_MODE_TYPE_DRIVER, 13500, 720, 739,
		   801, 858, 0, 480, 488, 494, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 16 - 1920x1080@60Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 17 - 720x576@50Hz 4:3 */
	{ DRM_MODE("720x576", DRM_MODE_TYPE_DRIVER, 27000, 720, 732,
		   796, 864, 0, 576, 581, 586, 625, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
	/* 18 - 720x576@50Hz 16:9 */
	{ DRM_MODE("720x576", DRM_MODE_TYPE_DRIVER, 27000, 720, 732,
		   796, 864, 0, 576, 581, 586, 625, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 19 - 1280x720@50Hz 16:9 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1720,
		   1760, 1980, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 20 - 1920x1080i@50Hz 16:9 */
	{ DRM_MODE("1920x1080i", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2448,
		   2492, 2640, 0, 1080, 1084, 1094, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 21 - 720(1440)x576i@50Hz 4:3 */
	{ DRM_MODE("720x576i", DRM_MODE_TYPE_DRIVER, 13500, 720, 732,
		   795, 864, 0, 576, 580, 586, 625, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
	/* 22 - 720(1440)x576i@50Hz 16:9 */
	{ DRM_MODE("720x576i", DRM_MODE_TYPE_DRIVER, 13500, 720, 732,
		   795, 864, 0, 576, 580, 586, 625, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 31 - 1920x1080@50Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2448,
		   2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 34 - 1920x1080@30Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 39 - 1920x1080i@50Hz 16:9 */
	{ DRM_MODE("1920x1080i", DRM_MODE_TYPE_DRIVER, 72000, 1920, 1952,
		   2120, 2304, 0, 1080, 1126, 1136, 1250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC |
		   DRM_MODE_FLAG_INTERLACE),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 62 - 1280x720@30Hz 16:9 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 3040,
		   3080, 3300, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 63 - 1920x1080@120Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 297000, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 67 - 1280x720@30Hz 64:27 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 3040,
		   3080, 3300, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27, },
	/* 68 - 1280x720@50Hz 64:27 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1720,
		   1760, 1980, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27, },
	/* 69 - 1280x720@60Hz 64:27 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27, },
	/* 70 - 1280x720@100Hz 64:27 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 148500, 1280, 1720,
		   1760, 1980, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27, },
	/* 71 - 1280x720@120Hz 64:27 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 148500, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27, },
	/* 74 - 1920x1080@30Hz 64:27 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27, },
	/* 75 - 1920x1080@50Hz 64:27 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2448,
		   2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27, },
	/* 76 - 1920x1080@60Hz 64:27 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27, },
	/* 77 - 1920x1080@100Hz 64:27 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 297000, 1920, 2448,
		   2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27, },
	/* 78 - 1920x1080@120Hz 64:27 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 297000, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27, },
	/* 95 - 3840x2160@30Hz 16:9 */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 297000, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 96 - 3840x2160@50Hz 16:9 */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4896,
		   4984, 5280, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 97 - 3840x2160@60Hz 16:9 */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 100 - 4096x2160@30Hz 256:135 */
	{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 297000, 4096, 4184,
		   4272, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_256_135, },
	/* 101 - 4096x2160@50Hz 256:135 */
	{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 594000, 4096, 5064,
		   5152, 5280, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_256_135, },
	/* 102 - 4096x2160@60Hz 256:135 */
	{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 594000, 4096, 4184,
		   4272, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_256_135, },
	/* 105 - 3840x2160@30Hz 64:27 */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 297000, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27, },
	/* 106 - 3840x2160@50Hz 64:27 */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4896,
		   4984, 5280, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27, },
	/* 107 - 3840x2160@60Hz 64:27 */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .picture_aspect_ratio = HDMI_PICTURE_ASPECT_64_27, },
};

int vconn_drm_add_modes_noedid(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;
	int i, count, num_modes = 0;

	count = ARRAY_SIZE(edid_cea_modes_1);

	for (i = 0; i < count; i++) {
		const struct drm_display_mode *ptr = &edid_cea_modes_1[i];

		mode = drm_mode_duplicate(dev, ptr);
		if (mode) {
			drm_mode_probed_add(connector, mode);
			num_modes++;
		}
	}

	return num_modes;
}

static void rockchip_virtual_encoder_enable(struct drm_encoder *encoder)
{
	struct vconn_device *vconn_dev = to_vconn_device(encoder);
	struct rockchip_vconn *vconn = vconn_dev->vconn;

	dev_info(vconn->dev, "encoder enable for output%d\n", ffs(vconn_dev->if_id) - 1);
}

static void rockchip_virtual_encoder_disable(struct drm_encoder *encoder)
{
	struct vconn_device *vconn_dev = to_vconn_device(encoder);
	struct rockchip_vconn *vconn = vconn_dev->vconn;
	struct drm_crtc *crtc = encoder->crtc;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);

	vcstate->output_if &= ~vconn_dev->if_id;

	dev_info(vconn->dev, "encoder disable for output%d\n", ffs(vconn_dev->if_id) - 1);
}

static int rockchip_virtual_encoder_atomic_check(struct drm_encoder *encoder,
						 struct drm_crtc_state *crtc_state,
						 struct drm_connector_state *conn_state)
{
	return 0;
}

static void rockchip_virtual_encoder_mode_set(struct drm_encoder *encoder,
					      struct drm_display_mode *mode,
					      struct drm_display_mode *adj)
{
	struct drm_crtc *crtc = encoder->crtc;
	struct rockchip_crtc_state *vcstate = to_rockchip_crtc_state(crtc->state);
	struct vconn_device *vconn_dev = to_vconn_device(encoder);
	struct rockchip_vconn *vconn = vconn_dev->vconn;

	vcstate->output_if |= vconn_dev->if_id;
	vcstate->output_mode = vconn_dev->output_mode;
	vcstate->output_type = vconn_dev->output_type;
	vcstate->bus_format = vconn_dev->bus_format;

	dev_info(vconn->dev, "mode set for output%d\n", ffs(vconn_dev->if_id) - 1);
}

static const struct drm_encoder_helper_funcs rockchip_virtual_encoder_helper_funcs = {
	.enable     = rockchip_virtual_encoder_enable,
	.disable    = rockchip_virtual_encoder_disable,
	.atomic_check = rockchip_virtual_encoder_atomic_check,
	.mode_set = rockchip_virtual_encoder_mode_set,
};

static void rockchip_virtual_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs rockchip_virtual_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rockchip_virtual_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int vvop_conn_get_modes(struct drm_connector *connector)
{
	int count;

	count = vconn_drm_add_modes_noedid(connector);
	drm_set_preferred_mode(connector, XRES_DEF, YRES_DEF);

	return count;
}

static const struct drm_connector_helper_funcs rockchip_virtual_connector_helper_funcs = {
	.get_modes    = vvop_conn_get_modes,
};

static int rockchip_virtual_connector_register(struct rockchip_vconn *vconn)
{
	struct vconn_device *vconn_dev, *n;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret;

	list_for_each_entry_safe(vconn_dev, n, &vconn->list_head, list) {
		encoder = &vconn_dev->encoder;
		connector = &vconn_dev->connector;
		encoder->possible_crtcs = vconn_dev->vp_id_mask;
		drm_encoder_helper_add(encoder, &rockchip_virtual_encoder_helper_funcs);
		ret = drm_simple_encoder_init(vconn->drm_dev, encoder, vconn_dev->encoder_type);
		if (ret < 0) {
			dev_err(vconn->dev, "failed to register encoder for output%d",
				ffs(vconn_dev->if_id) - 1);
			return ret;
		}

		drm_connector_helper_add(connector, &rockchip_virtual_connector_helper_funcs);
		ret = drm_connector_init(vconn->drm_dev, connector,
					 &rockchip_virtual_connector_funcs, vconn_dev->output_type);
		if (ret) {
			dev_err(vconn->dev, "Failed to initialize connector for output%d\n",
				ffs(vconn_dev->if_id) - 1);
			return ret;
		}

		drm_connector_attach_encoder(connector, encoder);
	}

	return 0;
}

static int rockchip_vconn_parse_vp_id(struct rockchip_vconn *vconn, const char *name)
{
	struct device_node *np = vconn->dev->of_node;
	char propname[16];
	int ret;
	u32 id;

	if (strlen(name) > 6) {
		dev_err(vconn->dev, "invalid connector name %s\n", name);
		return -EINVAL;
	}

	snprintf(propname, sizeof(propname), "%s-enable", name);
	if (of_property_read_bool(np, propname)) {
		snprintf(propname, sizeof(propname), "%s-vp-id", name);
		ret = of_property_read_u32(np, propname, &id);
		if (ret < 0) {
			dev_err(vconn->dev, "no specific vp-id for %s\n", name);
			return ret;
		}
		return id;
	}

	return -ENODEV;
}

static int rockchip_vconn_get_encoder_type(int conn_type)
{
	if (conn_type == DRM_MODE_CONNECTOR_DSI)
		return DRM_MODE_ENCODER_DSI;
	else if (conn_type == DRM_MODE_CONNECTOR_DPI)
		return DRM_MODE_ENCODER_DPI;
	else
		return DRM_MODE_ENCODER_TMDS;
}

static int rockchip_vconn_device_create(struct rockchip_vconn *vconn,
					const char *name,
					int conn_type,
					int output_mode,
					int bus_format,
					int if_id)
{
	struct vconn_device *vconn_dev;
	int id;

	id = rockchip_vconn_parse_vp_id(vconn, name);
	if (id >= 0) {
		vconn_dev = devm_kzalloc(vconn->dev, sizeof(*vconn_dev), GFP_KERNEL);
		if (!vconn_dev)
			return -ENOMEM;
		vconn_dev->vconn = vconn;
		vconn_dev->encoder_type = rockchip_vconn_get_encoder_type(conn_type);
		vconn_dev->output_type = conn_type;
		vconn_dev->output_mode = output_mode;
		vconn_dev->bus_format = bus_format;
		vconn_dev->if_id = if_id;
		vconn_dev->vp_id_mask = BIT(id);
		list_add_tail(&vconn_dev->list, &vconn->list_head);
	}

	return 0;
}

static int rockchip_virtual_connector_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct rockchip_vconn *vconn;

	if (!pdev->dev.of_node)
		return -ENODEV;

	vconn = devm_kzalloc(&pdev->dev, sizeof(*vconn), GFP_KERNEL);
	if (!vconn)
		return -ENOMEM;
	vconn->dev = dev;
	vconn->drm_dev = drm;

	INIT_LIST_HEAD(&vconn->list_head);

	rockchip_vconn_device_create(vconn, "hdmi0", DRM_MODE_CONNECTOR_HDMIA,
				     ROCKCHIP_OUT_MODE_AAAA, MEDIA_BUS_FMT_RGB888_1X24,
				     VOP_OUTPUT_IF_HDMI0);

	rockchip_vconn_device_create(vconn, "hdmi1", DRM_MODE_CONNECTOR_HDMIA,
				     ROCKCHIP_OUT_MODE_AAAA, MEDIA_BUS_FMT_RGB888_1X24,
				     VOP_OUTPUT_IF_HDMI1);

	rockchip_vconn_device_create(vconn, "dp0", DRM_MODE_CONNECTOR_DisplayPort,
				     ROCKCHIP_OUT_MODE_AAAA, MEDIA_BUS_FMT_RGB888_1X24,
				     VOP_OUTPUT_IF_DP0);

	rockchip_vconn_device_create(vconn, "dp1", DRM_MODE_CONNECTOR_DisplayPort,
				     ROCKCHIP_OUT_MODE_AAAA, MEDIA_BUS_FMT_RGB888_1X24,
				     VOP_OUTPUT_IF_DP1);

	rockchip_vconn_device_create(vconn, "edp0", DRM_MODE_CONNECTOR_eDP,
				     ROCKCHIP_OUT_MODE_AAAA, MEDIA_BUS_FMT_RGB888_1X24,
				     VOP_OUTPUT_IF_eDP0);

	rockchip_vconn_device_create(vconn, "edp0", DRM_MODE_CONNECTOR_eDP,
				     ROCKCHIP_OUT_MODE_AAAA, MEDIA_BUS_FMT_RGB888_1X24,
				     VOP_OUTPUT_IF_eDP1);

	rockchip_vconn_device_create(vconn, "mipi0", DRM_MODE_CONNECTOR_DSI,
				     ROCKCHIP_OUT_MODE_P888, MEDIA_BUS_FMT_RGB888_1X24,
				     VOP_OUTPUT_IF_MIPI0);

	rockchip_vconn_device_create(vconn, "mipi1", DRM_MODE_CONNECTOR_DSI,
				     ROCKCHIP_OUT_MODE_P888, MEDIA_BUS_FMT_RGB888_1X24,
				     VOP_OUTPUT_IF_MIPI1);

	rockchip_vconn_device_create(vconn, "rgb", DRM_MODE_CONNECTOR_DPI,
				     ROCKCHIP_OUT_MODE_P888, MEDIA_BUS_FMT_RGB888_1X24,
				     VOP_OUTPUT_IF_RGB);

	platform_set_drvdata(pdev, vconn);

	rockchip_virtual_connector_register(vconn);

	return 0;
}

static void rockchip_virtual_connector_unbind(struct device *dev, struct device *master,
					       void *data)
{
}

static const struct component_ops rockchip_virtual_connector_ops = {
	.bind	= rockchip_virtual_connector_bind,
	.unbind	= rockchip_virtual_connector_unbind,
};

static int rockchip_virtual_connector_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &rockchip_virtual_connector_ops);
}

static void rockchip_virtual_connector_shutdown(struct platform_device *pdev)
{
}

static int rockchip_virtual_connector_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rockchip_virtual_connector_ops);

	return 0;
}

static const struct of_device_id rockchip_virtual_connector_dt_ids[] = {
	{ .compatible = "rockchip,virtual-connector",
	},
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_virtual_connector_dt_ids);

struct platform_driver vconn_platform_driver = {
	.probe  = rockchip_virtual_connector_probe,
	.remove = rockchip_virtual_connector_remove,
	.shutdown = rockchip_virtual_connector_shutdown,
	.driver = {
		.name = "drm-virtual-connector",
		.of_match_table = rockchip_virtual_connector_dt_ids,
	},
};
