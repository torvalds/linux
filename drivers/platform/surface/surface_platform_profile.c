// SPDX-License-Identifier: GPL-2.0+
/*
 * Surface Platform Profile / Performance Mode driver for Surface System
 * Aggregator Module (thermal and fan subsystem).
 *
 * Copyright (C) 2021-2022 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <asm/unaligned.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_profile.h>
#include <linux/types.h>

#include <linux/surface_aggregator/device.h>

// Enum for the platform performance profile sent to the TMP module.
enum ssam_tmp_profile {
	SSAM_TMP_PROFILE_NORMAL             = 1,
	SSAM_TMP_PROFILE_BATTERY_SAVER      = 2,
	SSAM_TMP_PROFILE_BETTER_PERFORMANCE = 3,
	SSAM_TMP_PROFILE_BEST_PERFORMANCE   = 4,
};

// Enum for the fan profile sent to the FAN module. This fan profile is
// only sent to the EC if the 'has_fan' property is set. The integers are
// not a typo, they differ from the performance profile indices.
enum ssam_fan_profile {
	SSAM_FAN_PROFILE_NORMAL             = 2,
	SSAM_FAN_PROFILE_BATTERY_SAVER      = 1,
	SSAM_FAN_PROFILE_BETTER_PERFORMANCE = 3,
	SSAM_FAN_PROFILE_BEST_PERFORMANCE   = 4,
};

struct ssam_tmp_profile_info {
	__le32 profile;
	__le16 unknown1;
	__le16 unknown2;
} __packed;

struct ssam_platform_profile_device {
	struct ssam_device *sdev;
	struct platform_profile_handler handler;
	bool has_fan;
};

SSAM_DEFINE_SYNC_REQUEST_CL_R(__ssam_tmp_profile_get, struct ssam_tmp_profile_info, {
	.target_category = SSAM_SSH_TC_TMP,
	.command_id      = 0x02,
});

SSAM_DEFINE_SYNC_REQUEST_CL_W(__ssam_tmp_profile_set, __le32, {
	.target_category = SSAM_SSH_TC_TMP,
	.command_id      = 0x03,
});

SSAM_DEFINE_SYNC_REQUEST_W(__ssam_fan_profile_set, u8, {
	.target_category = SSAM_SSH_TC_FAN,
	.target_id = SSAM_SSH_TID_SAM,
	.command_id = 0x0e,
	.instance_id = 0x01,
});

static int ssam_tmp_profile_get(struct ssam_device *sdev, enum ssam_tmp_profile *p)
{
	struct ssam_tmp_profile_info info;
	int status;

	status = ssam_retry(__ssam_tmp_profile_get, sdev, &info);
	if (status < 0)
		return status;

	*p = le32_to_cpu(info.profile);
	return 0;
}

static int ssam_tmp_profile_set(struct ssam_device *sdev, enum ssam_tmp_profile p)
{
	const __le32 profile_le = cpu_to_le32(p);

	return ssam_retry(__ssam_tmp_profile_set, sdev, &profile_le);
}

static int ssam_fan_profile_set(struct ssam_device *sdev, enum ssam_fan_profile p)
{
	const u8 profile = p;

	return ssam_retry(__ssam_fan_profile_set, sdev->ctrl, &profile);
}

static int convert_ssam_tmp_to_profile(struct ssam_device *sdev, enum ssam_tmp_profile p)
{
	switch (p) {
	case SSAM_TMP_PROFILE_NORMAL:
		return PLATFORM_PROFILE_BALANCED;

	case SSAM_TMP_PROFILE_BATTERY_SAVER:
		return PLATFORM_PROFILE_LOW_POWER;

	case SSAM_TMP_PROFILE_BETTER_PERFORMANCE:
		return PLATFORM_PROFILE_BALANCED_PERFORMANCE;

	case SSAM_TMP_PROFILE_BEST_PERFORMANCE:
		return PLATFORM_PROFILE_PERFORMANCE;

	default:
		dev_err(&sdev->dev, "invalid performance profile: %d", p);
		return -EINVAL;
	}
}


static int convert_profile_to_ssam_tmp(struct ssam_device *sdev, enum platform_profile_option p)
{
	switch (p) {
	case PLATFORM_PROFILE_LOW_POWER:
		return SSAM_TMP_PROFILE_BATTERY_SAVER;

	case PLATFORM_PROFILE_BALANCED:
		return SSAM_TMP_PROFILE_NORMAL;

	case PLATFORM_PROFILE_BALANCED_PERFORMANCE:
		return SSAM_TMP_PROFILE_BETTER_PERFORMANCE;

	case PLATFORM_PROFILE_PERFORMANCE:
		return SSAM_TMP_PROFILE_BEST_PERFORMANCE;

	default:
		/* This should have already been caught by platform_profile_store(). */
		WARN(true, "unsupported platform profile");
		return -EOPNOTSUPP;
	}
}

static int convert_profile_to_ssam_fan(struct ssam_device *sdev, enum platform_profile_option p)
{
	switch (p) {
	case PLATFORM_PROFILE_LOW_POWER:
		return SSAM_FAN_PROFILE_BATTERY_SAVER;

	case PLATFORM_PROFILE_BALANCED:
		return SSAM_FAN_PROFILE_NORMAL;

	case PLATFORM_PROFILE_BALANCED_PERFORMANCE:
		return SSAM_FAN_PROFILE_BETTER_PERFORMANCE;

	case PLATFORM_PROFILE_PERFORMANCE:
		return SSAM_FAN_PROFILE_BEST_PERFORMANCE;

	default:
		/* This should have already been caught by platform_profile_store(). */
		WARN(true, "unsupported platform profile");
		return -EOPNOTSUPP;
	}
}

static int ssam_platform_profile_get(struct platform_profile_handler *pprof,
				     enum platform_profile_option *profile)
{
	struct ssam_platform_profile_device *tpd;
	enum ssam_tmp_profile tp;
	int status;

	tpd = container_of(pprof, struct ssam_platform_profile_device, handler);

	status = ssam_tmp_profile_get(tpd->sdev, &tp);
	if (status)
		return status;

	status = convert_ssam_tmp_to_profile(tpd->sdev, tp);
	if (status < 0)
		return status;

	*profile = status;
	return 0;
}

static int ssam_platform_profile_set(struct platform_profile_handler *pprof,
				     enum platform_profile_option profile)
{
	struct ssam_platform_profile_device *tpd;
	int tp;

	tpd = container_of(pprof, struct ssam_platform_profile_device, handler);

	tp = convert_profile_to_ssam_tmp(tpd->sdev, profile);
	if (tp < 0)
		return tp;

	tp = ssam_tmp_profile_set(tpd->sdev, tp);
	if (tp < 0)
		return tp;

	if (tpd->has_fan) {
		tp = convert_profile_to_ssam_fan(tpd->sdev, profile);
		if (tp < 0)
			return tp;
		tp = ssam_fan_profile_set(tpd->sdev, tp);
	}

	return tp;
}

static int surface_platform_profile_probe(struct ssam_device *sdev)
{
	struct ssam_platform_profile_device *tpd;

	tpd = devm_kzalloc(&sdev->dev, sizeof(*tpd), GFP_KERNEL);
	if (!tpd)
		return -ENOMEM;

	tpd->sdev = sdev;

	tpd->handler.profile_get = ssam_platform_profile_get;
	tpd->handler.profile_set = ssam_platform_profile_set;

	tpd->has_fan = device_property_read_bool(&sdev->dev, "has_fan");

	set_bit(PLATFORM_PROFILE_LOW_POWER, tpd->handler.choices);
	set_bit(PLATFORM_PROFILE_BALANCED, tpd->handler.choices);
	set_bit(PLATFORM_PROFILE_BALANCED_PERFORMANCE, tpd->handler.choices);
	set_bit(PLATFORM_PROFILE_PERFORMANCE, tpd->handler.choices);

	return platform_profile_register(&tpd->handler);
}

static void surface_platform_profile_remove(struct ssam_device *sdev)
{
	platform_profile_remove();
}

static const struct ssam_device_id ssam_platform_profile_match[] = {
	{ SSAM_SDEV(TMP, SAM, 0x00, 0x01) },
	{ },
};
MODULE_DEVICE_TABLE(ssam, ssam_platform_profile_match);

static struct ssam_device_driver surface_platform_profile = {
	.probe = surface_platform_profile_probe,
	.remove = surface_platform_profile_remove,
	.match_table = ssam_platform_profile_match,
	.driver = {
		.name = "surface_platform_profile",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_ssam_device_driver(surface_platform_profile);

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("Platform Profile Support for Surface System Aggregator Module");
MODULE_LICENSE("GPL");
