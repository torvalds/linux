// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 */

#include <linux/dmi.h>

#include "i915_drv.h"
#include "intel_display_types.h"
#include "intel_quirks.h"

static void intel_set_quirk(struct intel_display *display, enum intel_quirk_id quirk)
{
	display->quirks.mask |= BIT(quirk);
}

static void intel_set_dpcd_quirk(struct intel_dp *intel_dp, enum intel_quirk_id quirk)
{
	intel_dp->quirks.mask |= BIT(quirk);
}

/*
 * Some machines (Lenovo U160) do not work with SSC on LVDS for some reason
 */
static void quirk_ssc_force_disable(struct intel_display *display)
{
	intel_set_quirk(display, QUIRK_LVDS_SSC_DISABLE);
	drm_info(display->drm, "applying lvds SSC disable quirk\n");
}

/*
 * A machine (e.g. Acer Aspire 5734Z) may need to invert the panel backlight
 * brightness value
 */
static void quirk_invert_brightness(struct intel_display *display)
{
	intel_set_quirk(display, QUIRK_INVERT_BRIGHTNESS);
	drm_info(display->drm, "applying inverted panel brightness quirk\n");
}

/* Some VBT's incorrectly indicate no backlight is present */
static void quirk_backlight_present(struct intel_display *display)
{
	intel_set_quirk(display, QUIRK_BACKLIGHT_PRESENT);
	drm_info(display->drm, "applying backlight present quirk\n");
}

/* Toshiba Satellite P50-C-18C requires T12 delay to be min 800ms
 * which is 300 ms greater than eDP spec T12 min.
 */
static void quirk_increase_t12_delay(struct intel_display *display)
{
	intel_set_quirk(display, QUIRK_INCREASE_T12_DELAY);
	drm_info(display->drm, "Applying T12 delay quirk\n");
}

/*
 * GeminiLake NUC HDMI outputs require additional off time
 * this allows the onboard retimer to correctly sync to signal
 */
static void quirk_increase_ddi_disabled_time(struct intel_display *display)
{
	intel_set_quirk(display, QUIRK_INCREASE_DDI_DISABLED_TIME);
	drm_info(display->drm, "Applying Increase DDI Disabled quirk\n");
}

static void quirk_no_pps_backlight_power_hook(struct intel_display *display)
{
	intel_set_quirk(display, QUIRK_NO_PPS_BACKLIGHT_POWER_HOOK);
	drm_info(display->drm, "Applying no pps backlight power quirk\n");
}

static void quirk_fw_sync_len(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);

	intel_set_dpcd_quirk(intel_dp, QUIRK_FW_SYNC_LEN);
	drm_info(display->drm, "Applying Fast Wake sync pulse count quirk\n");
}

struct intel_quirk {
	int device;
	int subsystem_vendor;
	int subsystem_device;
	void (*hook)(struct intel_display *display);
};

struct intel_dpcd_quirk {
	int device;
	int subsystem_vendor;
	int subsystem_device;
	u8 sink_oui[3];
	u8 sink_device_id[6];
	void (*hook)(struct intel_dp *intel_dp);
};

#define SINK_OUI(first, second, third) { (first), (second), (third) }
#define SINK_DEVICE_ID(first, second, third, fourth, fifth, sixth) \
	{ (first), (second), (third), (fourth), (fifth), (sixth) }

#define SINK_DEVICE_ID_ANY	SINK_DEVICE_ID(0, 0, 0, 0, 0, 0)

/* For systems that don't have a meaningful PCI subdevice/subvendor ID */
struct intel_dmi_quirk {
	void (*hook)(struct intel_display *display);
	const struct dmi_system_id (*dmi_id_list)[];
};

static int intel_dmi_reverse_brightness(const struct dmi_system_id *id)
{
	DRM_INFO("Backlight polarity reversed on %s\n", id->ident);
	return 1;
}

static int intel_dmi_no_pps_backlight(const struct dmi_system_id *id)
{
	DRM_INFO("No pps backlight support on %s\n", id->ident);
	return 1;
}

static const struct intel_dmi_quirk intel_dmi_quirks[] = {
	{
		.dmi_id_list = &(const struct dmi_system_id[]) {
			{
				.callback = intel_dmi_reverse_brightness,
				.ident = "NCR Corporation",
				.matches = {DMI_MATCH(DMI_SYS_VENDOR, "NCR Corporation"),
					    DMI_MATCH(DMI_PRODUCT_NAME, ""),
				},
			},
			{
				.callback = intel_dmi_reverse_brightness,
				.ident = "Thundersoft TST178 tablet",
				/* DMI strings are too generic, also match on BIOS date */
				.matches = {DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
					    DMI_EXACT_MATCH(DMI_BOARD_NAME, "Aptio CRB"),
					    DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "To be filled by O.E.M."),
					    DMI_EXACT_MATCH(DMI_BIOS_DATE, "04/15/2014"),
				},
			},
			{ }  /* terminating entry */
		},
		.hook = quirk_invert_brightness,
	},
	{
		.dmi_id_list = &(const struct dmi_system_id[]) {
			{
				.callback = intel_dmi_no_pps_backlight,
				.ident = "Google Lillipup sku524294",
				.matches = {DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "Google"),
					    DMI_EXACT_MATCH(DMI_BOARD_NAME, "Lindar"),
					    DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "sku524294"),
				},
			},
			{
				.callback = intel_dmi_no_pps_backlight,
				.ident = "Google Lillipup sku524295",
				.matches = {DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "Google"),
					    DMI_EXACT_MATCH(DMI_BOARD_NAME, "Lindar"),
					    DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "sku524295"),
				},
			},
			{ }
		},
		.hook = quirk_no_pps_backlight_power_hook,
	},
};

static struct intel_quirk intel_quirks[] = {
	/* Lenovo U160 cannot use SSC on LVDS */
	{ 0x0046, 0x17aa, 0x3920, quirk_ssc_force_disable },

	/* Sony Vaio Y cannot use SSC on LVDS */
	{ 0x0046, 0x104d, 0x9076, quirk_ssc_force_disable },

	/* Acer Aspire 5734Z must invert backlight brightness */
	{ 0x2a42, 0x1025, 0x0459, quirk_invert_brightness },

	/* Acer/eMachines G725 */
	{ 0x2a42, 0x1025, 0x0210, quirk_invert_brightness },

	/* Acer/eMachines e725 */
	{ 0x2a42, 0x1025, 0x0212, quirk_invert_brightness },

	/* Acer/Packard Bell NCL20 */
	{ 0x2a42, 0x1025, 0x034b, quirk_invert_brightness },

	/* Acer Aspire 4736Z */
	{ 0x2a42, 0x1025, 0x0260, quirk_invert_brightness },

	/* Acer Aspire 5336 */
	{ 0x2a42, 0x1025, 0x048a, quirk_invert_brightness },

	/* Acer C720 and C720P Chromebooks (Celeron 2955U) have backlights */
	{ 0x0a06, 0x1025, 0x0a11, quirk_backlight_present },

	/* Acer C720 Chromebook (Core i3 4005U) */
	{ 0x0a16, 0x1025, 0x0a11, quirk_backlight_present },

	/* Apple Macbook 2,1 (Core 2 T7400) */
	{ 0x27a2, 0x8086, 0x7270, quirk_backlight_present },

	/* Apple Macbook 4,1 */
	{ 0x2a02, 0x106b, 0x00a1, quirk_backlight_present },

	/* Toshiba CB35 Chromebook (Celeron 2955U) */
	{ 0x0a06, 0x1179, 0x0a88, quirk_backlight_present },

	/* HP Chromebook 14 (Celeron 2955U) */
	{ 0x0a06, 0x103c, 0x21ed, quirk_backlight_present },

	/* Dell Chromebook 11 */
	{ 0x0a06, 0x1028, 0x0a35, quirk_backlight_present },

	/* Dell Chromebook 11 (2015 version) */
	{ 0x0a16, 0x1028, 0x0a35, quirk_backlight_present },

	/* Toshiba Satellite P50-C-18C */
	{ 0x191B, 0x1179, 0xF840, quirk_increase_t12_delay },

	/* GeminiLake NUC */
	{ 0x3185, 0x8086, 0x2072, quirk_increase_ddi_disabled_time },
	{ 0x3184, 0x8086, 0x2072, quirk_increase_ddi_disabled_time },
	/* ASRock ITX*/
	{ 0x3185, 0x1849, 0x2212, quirk_increase_ddi_disabled_time },
	{ 0x3184, 0x1849, 0x2212, quirk_increase_ddi_disabled_time },
	/* ECS Liva Q2 */
	{ 0x3185, 0x1019, 0xa94d, quirk_increase_ddi_disabled_time },
	{ 0x3184, 0x1019, 0xa94d, quirk_increase_ddi_disabled_time },
	/* HP Notebook - 14-r206nv */
	{ 0x0f31, 0x103c, 0x220f, quirk_invert_brightness },
};

static const struct intel_dpcd_quirk intel_dpcd_quirks[] = {
	/* Dell Precision 5490 */
	{
		.device = 0x7d55,
		.subsystem_vendor = 0x1028,
		.subsystem_device = 0x0cc7,
		.sink_oui = SINK_OUI(0x38, 0xec, 0x11),
		.hook = quirk_fw_sync_len,
	},

};

void intel_init_quirks(struct intel_display *display)
{
	struct pci_dev *d = to_pci_dev(display->drm->dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(intel_quirks); i++) {
		struct intel_quirk *q = &intel_quirks[i];

		if (d->device == q->device &&
		    (d->subsystem_vendor == q->subsystem_vendor ||
		     q->subsystem_vendor == PCI_ANY_ID) &&
		    (d->subsystem_device == q->subsystem_device ||
		     q->subsystem_device == PCI_ANY_ID))
			q->hook(display);
	}
	for (i = 0; i < ARRAY_SIZE(intel_dmi_quirks); i++) {
		if (dmi_check_system(*intel_dmi_quirks[i].dmi_id_list) != 0)
			intel_dmi_quirks[i].hook(display);
	}
}

void intel_init_dpcd_quirks(struct intel_dp *intel_dp,
			    const struct drm_dp_dpcd_ident *ident)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct pci_dev *d = to_pci_dev(display->drm->dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(intel_dpcd_quirks); i++) {
		const struct intel_dpcd_quirk *q = &intel_dpcd_quirks[i];

		if (d->device == q->device &&
		    (d->subsystem_vendor == q->subsystem_vendor ||
		     q->subsystem_vendor == PCI_ANY_ID) &&
		    (d->subsystem_device == q->subsystem_device ||
		     q->subsystem_device == PCI_ANY_ID) &&
		    !memcmp(q->sink_oui, ident->oui, sizeof(ident->oui)) &&
		    (!memcmp(q->sink_device_id, ident->device_id,
			    sizeof(ident->device_id)) ||
		     mem_is_zero(q->sink_device_id, sizeof(q->sink_device_id))))
			q->hook(intel_dp);
	}
}

bool intel_has_quirk(struct intel_display *display, enum intel_quirk_id quirk)
{
	return display->quirks.mask & BIT(quirk);
}

bool intel_has_dpcd_quirk(struct intel_dp *intel_dp, enum intel_quirk_id quirk)
{
	return intel_dp->quirks.mask & BIT(quirk);
}
