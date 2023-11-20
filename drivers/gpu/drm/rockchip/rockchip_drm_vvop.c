// SPDX-License-Identifier: GPL-2.0+
/*
 * Virtual vop driver based on vkms
 *
 */

#include <linux/module.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#define DRIVER_NAME	"virtual-vop"

#define XRES_MIN	32
#define YRES_MIN	32

#define XRES_DEF	1024
#define YRES_DEF	768

#define XRES_MAX	8192
#define YRES_MAX	8192

#define VVOP_MAX_CRTC	8

static struct platform_device *vvop_pdev;

struct vvop_crtc {
	struct drm_crtc crtc;
	struct drm_plane plane;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct hrtimer vblank_hrtimer;
	ktime_t period_ns;
	struct drm_pending_vblank_event *event;

	struct drm_property *is_virtual_prop;
	struct drm_property *soc_id_prop;
};

struct vvop {
	struct device *dev;
	struct drm_device *drm_dev;
	struct platform_device *pdev;

	struct vvop_crtc vcrtc[VVOP_MAX_CRTC];

	uint32_t crtc_mask;
};

static const u32 vvop_formats[] = {
	DRM_FORMAT_XRGB8888,
};

#define drm_crtc_to_vvop_crtc(crtc) \
	container_of(crtc, struct vvop_crtc, crtc)


static const struct drm_plane_funcs vvop_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static void vvop_plane_atomic_update(struct drm_plane *plane,
				     struct drm_plane_state *old_state)
{
}

static const struct drm_plane_helper_funcs vvop_plane_helper_funcs = {
	.atomic_update		= vvop_plane_atomic_update,
};

static int vvop_plane_init(struct drm_device *dev, struct drm_plane *primary)
{
	int ret;

	ret = drm_universal_plane_init(dev, primary, 0,
				       &vvop_plane_funcs,
				       vvop_formats, ARRAY_SIZE(vvop_formats),
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;

	drm_plane_helper_add(primary, &vvop_plane_helper_funcs);

	return 0;
}

static enum hrtimer_restart vvop_vblank_simulate(struct hrtimer *timer)
{
	struct vvop_crtc *vcrtc = container_of(timer, struct vvop_crtc, vblank_hrtimer);
	struct drm_crtc *crtc = &vcrtc->crtc;
	bool ret;

	hrtimer_forward_now(&vcrtc->vblank_hrtimer, vcrtc->period_ns);

	ret = drm_crtc_handle_vblank(crtc);
	/* Don't queue timer again when vblank is disabled. */
	if (!ret) {
		drm_dbg(crtc->dev, "vblank is already disabled\n");
		return HRTIMER_NORESTART;
	}

	return HRTIMER_RESTART;
}

static int vvop_enable_vblank(struct drm_crtc *crtc)
{
	struct vvop_crtc *vcrtc = drm_crtc_to_vvop_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];

	drm_calc_timestamping_constants(crtc, &crtc->mode);

	vcrtc->period_ns = ktime_set(0, vblank->framedur_ns);
	hrtimer_start(&vcrtc->vblank_hrtimer, vcrtc->period_ns, HRTIMER_MODE_REL);

	return 0;
}

static void vvop_disable_vblank(struct drm_crtc *crtc)
{
	struct vvop_crtc *vcrtc = drm_crtc_to_vvop_crtc(crtc);

	hrtimer_try_to_cancel(&vcrtc->vblank_hrtimer);
}

static void vvop_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs vvop_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = vvop_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_encoder_funcs vvop_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static struct drm_display_mode vvop_modes_builtin[] = {
	/* 1280x720@30Hz */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 37125, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1920x1080@30Hz */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2560x1440@30Hz */
	{ DRM_MODE("2560x1440", DRM_MODE_TYPE_DRIVER, 120750, 2560, 2608,
		   2640, 2720, 0, 1440, 1443, 1448, 1481, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 3840x2160@30Hz */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 297000, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 4096x2160@30Hz */
	{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 297000, 4096, 4184,
		   4272, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 720x1280@30Hz */
	{ DRM_MODE("720x1280", DRM_MODE_TYPE_DRIVER, 37125, 720, 725,
		   730, 750, 0, 1280, 1390, 1430, 1650, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1080x1920@30Hz */
	{ DRM_MODE("1080x1920", DRM_MODE_TYPE_DRIVER, 74250, 1080, 1084,
		   1089, 1125, 0, 1920, 2008, 2052, 2200, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1440x2560@30Hz */
	{ DRM_MODE("1440x2560", DRM_MODE_TYPE_DRIVER, 120750, 1440, 1443,
		   1448, 1481, 0, 2560, 2608, 2640, 2720, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 2160x3840@30Hz */
	{ DRM_MODE("2160x3840", DRM_MODE_TYPE_DRIVER, 297000, 2160, 2168,
		   2178, 2250, 0, 3840, 4016, 4104, 4400, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2160x4096@30Hz */
	{ DRM_MODE("2160x4096", DRM_MODE_TYPE_DRIVER, 297000, 2160, 2168,
		   2178, 2250, 0, 4096, 4184, 4272, 4400, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },

	/* 1280x720@60Hz */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1920x1080@60Hz */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2560x1440@60Hz */
	{ DRM_MODE("2560x1440", DRM_MODE_TYPE_DRIVER, 241500, 2560, 2608,
		   2640, 2720, 0, 1440, 1443, 1448, 1481, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 3840x2160@60Hz */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 4096x2160@60Hz */
	{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 594000, 4096, 4184,
		   4272, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 720x1280@60Hz */
	{ DRM_MODE("720x1280", DRM_MODE_TYPE_DRIVER, 74250, 720, 725,
		   730, 750, 0, 1280, 1390, 1430, 1650, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1080x1920@60Hz */
	{ DRM_MODE("1080x1920", DRM_MODE_TYPE_DRIVER, 148500, 1080, 1084,
		   1089, 1125, 0, 1920, 2008, 2052, 2200, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1440x2560@60Hz */
	{ DRM_MODE("1440x2560", DRM_MODE_TYPE_DRIVER, 241500, 1440, 1443,
		   1448, 1481, 0, 2560, 2608, 2640, 2720, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 2160x3840@60Hz */
	{ DRM_MODE("2160x3840", DRM_MODE_TYPE_DRIVER, 594000, 2160, 2168,
		   2178, 2250, 0, 3840, 4016, 4104, 4400, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2160x4096@60Hz */
	{ DRM_MODE("2160x4096", DRM_MODE_TYPE_DRIVER, 594000, 2160, 2168,
		   2178, 2250, 0, 4096, 4184, 4272, 4400, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },

	/* 1280x720@90Hz */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 111375, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1920x1080@90Hz */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 222750, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2560x1440@90Hz */
	{ DRM_MODE("2560x1440", DRM_MODE_TYPE_DRIVER, 362250, 2560, 2608,
		   2640, 2720, 0, 1440, 1443, 1448, 1481, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 3840x2160@90Hz */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 891000, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 4096x2160@90Hz */
	{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 891000, 4096, 4184,
		   4272, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 720x1280@90Hz */
	{ DRM_MODE("720x1280", DRM_MODE_TYPE_DRIVER, 111375, 720, 725,
		   730, 750, 0, 1280, 1390, 1430, 1650, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1080x1920@90Hz */
	{ DRM_MODE("1080x1920", DRM_MODE_TYPE_DRIVER, 222750, 1080, 1084,
		   1089, 1125, 0, 1920, 2008, 2052, 2200, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1440x2560@90Hz */
	{ DRM_MODE("1440x2560", DRM_MODE_TYPE_DRIVER, 362250, 1440, 1443,
		   1448, 1481, 0, 2560, 2608, 2640, 2720, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 2160x3840@90Hz */
	{ DRM_MODE("2160x3840", DRM_MODE_TYPE_DRIVER, 891000, 2160, 2168,
		   2178, 2250, 0, 3840, 4016, 4104, 4400, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2160x4096@90Hz */
	{ DRM_MODE("2160x4096", DRM_MODE_TYPE_DRIVER, 891000, 2160, 2168,
		   2178, 2250, 0, 4096, 4184, 4272, 4400, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },

	/* 1280x720@120Hz */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 148500, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1920x1080@120Hz */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 297000, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2560x1440@120Hz */
	{ DRM_MODE("2560x1440", DRM_MODE_TYPE_DRIVER, 483000, 2560, 2608,
		   2640, 2720, 0, 1440, 1443, 1448, 1481, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 3840x2160@120Hz */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 1188000, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 4096x2160@120Hz */
	{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 1188000, 4096, 4184,
		   4272, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 720x1280@120Hz */
	{ DRM_MODE("720x1280", DRM_MODE_TYPE_DRIVER, 148500, 720, 725,
		   730, 750, 0, 1280, 1390, 1430, 1650, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1080x1920@120Hz */
	{ DRM_MODE("1080x1920", DRM_MODE_TYPE_DRIVER, 297000, 1080, 1084,
		   1089, 1125, 0, 1920, 2008, 2052, 2200, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1440x2560@120Hz */
	{ DRM_MODE("1440x2560", DRM_MODE_TYPE_DRIVER, 483000, 1440, 1443,
		   1448, 1481, 0, 2560, 2608, 2640, 2720, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 2160x3840@120Hz */
	{ DRM_MODE("2160x3840", DRM_MODE_TYPE_DRIVER, 1188000, 2160, 2168,
		   2178, 2250, 0, 3840, 4016, 4104, 4400, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2160x4096@120Hz */
	{ DRM_MODE("2160x4096", DRM_MODE_TYPE_DRIVER, 1188000, 2160, 2168,
		   2178, 2250, 0, 4096, 4184, 4272, 4400, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },

	/* 1280x720@144Hz */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 178200, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1920x1080@144Hz */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 356400, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2560x1440@144Hz */
	{ DRM_MODE("2560x1440", DRM_MODE_TYPE_DRIVER, 483000, 2560, 2608,
		   2640, 2720, 0, 1440, 1443, 1448, 1481, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 3840x2160@144Hz */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 1425600, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 4096x2160@144Hz */
	{ DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 1425600, 4096, 4184,
		   4272, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 720x1280@144Hz */
	{ DRM_MODE("720x1280", DRM_MODE_TYPE_DRIVER, 178200, 720, 725,
		   730, 750, 0, 1280, 1390, 1430, 1650, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1080x1920@144Hz */
	{ DRM_MODE("1080x1920", DRM_MODE_TYPE_DRIVER, 356400, 1080, 1084,
		   1089, 1125, 0, 1920, 2008, 2052, 2200, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 1440x2560@144Hz */
	{ DRM_MODE("1440x2560", DRM_MODE_TYPE_DRIVER, 580000, 1440, 1443,
		   1448, 1481, 0, 2560, 2608, 2640, 2720, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 2160x3840@144Hz */
	{ DRM_MODE("2160x3840", DRM_MODE_TYPE_DRIVER, 1425600, 2160, 2168,
		   2178, 2250, 0, 3840, 4016, 4104, 4400, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	/* 2160x4096@144Hz */
	{ DRM_MODE("2160x4096", DRM_MODE_TYPE_DRIVER, 1425600, 2160, 2168,
		   2178, 2250, 0, 4096, 4184, 4272, 4400, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
};

static int vvop_conn_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode *bmode;
	int count = 0;
	int i;

	count += drm_add_modes_noedid(connector, XRES_MAX, YRES_MAX);
	for (i = 0; vvop_modes_builtin[i].type != 0; i++) {
		bmode = &vvop_modes_builtin[i];

		mode = drm_mode_duplicate(connector->dev, bmode);
		if (!mode)
			return 0;

		drm_mode_probed_add(connector, mode);
		count++;
	}
	drm_set_preferred_mode(connector, XRES_DEF, YRES_DEF);

	return count;
}

static const struct drm_connector_helper_funcs vvop_conn_helper_funcs = {
	.get_modes    = vvop_conn_get_modes,
};

static const struct drm_crtc_funcs vvop_crtc_funcs = {
	.set_config		= drm_atomic_helper_set_config,
	.destroy		= drm_crtc_cleanup,
	.page_flip		= drm_atomic_helper_page_flip,
	.reset			= drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= vvop_enable_vblank,
	.disable_vblank		= vvop_disable_vblank,
};

static void vvop_crtc_atomic_enable(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	drm_crtc_vblank_on(crtc);
}

static void vvop_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_state)
{
	unsigned long flags;

	drm_crtc_vblank_off(crtc);
	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

		crtc->state->event = NULL;
	}

}

static void vvop_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_crtc_state)
{
	unsigned long flags;

	if (crtc->state->event) {
		spin_lock_irqsave(&crtc->dev->event_lock, flags);

		if (drm_crtc_vblank_get(crtc) != 0)
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
		else
			drm_crtc_arm_vblank_event(crtc, crtc->state->event);

		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

		crtc->state->event = NULL;
	}
}

static const struct drm_crtc_helper_funcs vvop_crtc_helper_funcs = {
	.atomic_flush	= vvop_crtc_atomic_flush,
	.atomic_enable	= vvop_crtc_atomic_enable,
	.atomic_disable	= vvop_crtc_atomic_disable,
};

static u64 vvop_get_soc_id(void)
{
	if (of_machine_is_compatible("rockchip,rk3588"))
		return 0x3588;
	else if (of_machine_is_compatible("rockchip,rk3568"))
		return 0x3568;
	else if (of_machine_is_compatible("rockchip,rk3566"))
		return 0x3566;
	else if (of_machine_is_compatible("rockchip,rk3562"))
		return 0x3562;
	else if (of_machine_is_compatible("rockchip,rk3528"))
		return 0x3528;
	else
		return 0;
}

static void vvop_crtc_deinit(struct drm_crtc *crtc)
{
	struct vvop_crtc *vcrtc = drm_crtc_to_vvop_crtc(crtc);

	hrtimer_cancel(&vcrtc->vblank_hrtimer);
	drm_crtc_cleanup(crtc);
}

static int vvop_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
			  struct drm_plane *primary, struct drm_plane *cursor)
{
	struct vvop_crtc *vcrtc = drm_crtc_to_vvop_crtc(crtc);
	int ret;

	ret = drm_crtc_init_with_planes(dev, crtc, primary, cursor,
					&vvop_crtc_funcs, NULL);
	if (ret) {
		DRM_ERROR("Failed to init CRTC\n");
		return ret;
	}

	drm_crtc_helper_add(crtc, &vvop_crtc_helper_funcs);

	vcrtc->is_virtual_prop = drm_property_create_object(dev,
							    DRM_MODE_PROP_ATOMIC |
							    DRM_MODE_PROP_IMMUTABLE,
							    "IS_VIRTUAL", DRM_MODE_OBJECT_CRTC);
	drm_object_attach_property(&crtc->base, vcrtc->is_virtual_prop, 1);

	vcrtc->soc_id_prop = drm_property_create_object(dev,
							DRM_MODE_PROP_ATOMIC |
							DRM_MODE_PROP_IMMUTABLE,
							"SOC_ID", DRM_MODE_OBJECT_CRTC);
	drm_object_attach_property(&crtc->base, vcrtc->soc_id_prop, vvop_get_soc_id());

	hrtimer_init(&vcrtc->vblank_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vcrtc->vblank_hrtimer.function = &vvop_vblank_simulate;

	return ret;
}

static int vvop_create_crtc(struct vvop *vvop, int index)
{
	struct drm_crtc *crtc = &vvop->vcrtc[index].crtc;
	struct drm_connector *connector = &vvop->vcrtc[index].connector;
	struct drm_encoder *encoder = &vvop->vcrtc[index].encoder;
	struct drm_plane *primary = &vvop->vcrtc[index].plane;
	int ret;

	ret = vvop_plane_init(vvop->drm_dev, primary);
	if (ret) {
		DRM_ERROR("Failed to init primary plane for crtc-%d\n", index);
		return ret;
	}

	ret = vvop_crtc_init(vvop->drm_dev, crtc, primary, NULL);
	if (ret) {
		DRM_ERROR("Failed to init crtc-%d\n", index);
		goto err_crtc;
	}

	ret = drm_connector_init(vvop->drm_dev, connector, &vvop_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret) {
		DRM_ERROR("Failed to init connector-%d\n", index);
		goto err_connector;
	}
	drm_connector_helper_add(connector, &vvop_conn_helper_funcs);

	ret = drm_connector_register(connector);
	if (ret) {
		DRM_ERROR("Failed to register connector-%d\n", index);
		goto err_connector_register;
	}

	ret = drm_encoder_init(vvop->drm_dev, encoder, &vvop_encoder_funcs,
			       DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret) {
		DRM_ERROR("Failed to init encoder-%d\n", index);
		goto err_encoder;
	}
	encoder->possible_crtcs = BIT(index);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret) {
		DRM_ERROR("Failed to attach connector-%d to encoder-%d\n", index, index);
		goto err_attach;
	}

	vvop->crtc_mask |= drm_crtc_mask(crtc);

	return 0;

err_attach:
	drm_encoder_cleanup(encoder);
err_encoder:
	drm_connector_unregister(connector);
err_connector_register:
	drm_connector_cleanup(connector);
err_connector:
	vvop_crtc_deinit(crtc);
err_crtc:
	drm_plane_cleanup(primary);

	return ret;
}

static int vvop_create_crtcs(struct vvop *vvop)
{
	int ret;
	int i;

	for (i = 0; i < VVOP_MAX_CRTC; i++) {
		ret = vvop_create_crtc(vvop, i);
		if (ret) {
			DRM_WARN("Failed to create virtual crtc, index = %d\n", i);
			break;
		}
	}

	DRM_INFO("Create %d(total: %d) virtual crtcs\n", i, VVOP_MAX_CRTC);

	return 0;
}

static int vvop_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm_dev = data;
	struct vvop *vvop;

	vvop = devm_kzalloc(dev, sizeof(*vvop), GFP_KERNEL);
	if (!vvop)
		return -ENOMEM;

	vvop->dev = dev;
	vvop->drm_dev = drm_dev;
	dev_set_drvdata(dev, vvop);

	vvop_create_crtcs(vvop);

	return 0;
}

static void vvop_unbind(struct device *dev, struct device *master, void *data)
{
	struct vvop *vvop = dev_get_drvdata(dev);
	struct drm_device *drm_dev = vvop->drm_dev;
	struct list_head *crtc_list = &drm_dev->mode_config.crtc_list;
	struct drm_crtc *crtc, *tmp_crtc;
	struct vvop_crtc *vcrtc;

	list_for_each_entry_safe(crtc, tmp_crtc, crtc_list, head) {
		if (vvop->crtc_mask & drm_crtc_mask(crtc)) {
			vcrtc = drm_crtc_to_vvop_crtc(crtc);

			drm_encoder_cleanup(&vcrtc->encoder);
			drm_connector_unregister(&vcrtc->connector);
			drm_connector_cleanup(&vcrtc->connector);
			drm_plane_cleanup(&vcrtc->plane);
			vvop->crtc_mask &= ~(drm_crtc_mask(crtc));
			vvop_crtc_deinit(crtc);
		}
	}
}

const struct component_ops vvop_component_ops = {
	.bind = vvop_bind,
	.unbind = vvop_unbind,
};

static int vvop_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	DRM_DEV_INFO(dev, "virtual vop probe\n");

	return component_add(dev, &vvop_component_ops);
}

static int vvop_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vvop_component_ops);

	return 0;
}


struct platform_driver vvop_platform_driver = {
	.probe = vvop_probe,
	.remove = vvop_remove,
	.driver = {
		.name = DRIVER_NAME,
	},
};

static int __init vvop_init(void)
{
	vvop_pdev = platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
	if (IS_ERR(vvop_pdev)) {
		DRM_ERROR("failed to register platform device %s\n", DRIVER_NAME);
		return PTR_ERR(vvop_pdev);
	}

	return 0;
}

static void __exit vvop_exit(void)
{
	platform_device_unregister(vvop_pdev);
}

rootfs_initcall(vvop_init);
module_exit(vvop_exit);

MODULE_AUTHOR("Andy Yan <rock-chips@.com>");
MODULE_LICENSE("GPL");
