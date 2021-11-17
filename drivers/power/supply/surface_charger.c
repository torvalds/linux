// SPDX-License-Identifier: GPL-2.0+
/*
 * AC driver for 7th-generation Microsoft Surface devices via Surface System
 * Aggregator Module (SSAM).
 *
 * Copyright (C) 2019-2021 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <asm/unaligned.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/types.h>

#include <linux/surface_aggregator/device.h>


/* -- SAM interface. -------------------------------------------------------- */

enum sam_event_cid_bat {
	SAM_EVENT_CID_BAT_ADP   = 0x17,
};

enum sam_battery_sta {
	SAM_BATTERY_STA_OK      = 0x0f,
	SAM_BATTERY_STA_PRESENT	= 0x10,
};

/* Get battery status (_STA). */
SSAM_DEFINE_SYNC_REQUEST_CL_R(ssam_bat_get_sta, __le32, {
	.target_category = SSAM_SSH_TC_BAT,
	.command_id      = 0x01,
});

/* Get platform power source for battery (_PSR / DPTF PSRC). */
SSAM_DEFINE_SYNC_REQUEST_CL_R(ssam_bat_get_psrc, __le32, {
	.target_category = SSAM_SSH_TC_BAT,
	.command_id      = 0x0d,
});


/* -- Device structures. ---------------------------------------------------- */

struct spwr_psy_properties {
	const char *name;
	struct ssam_event_registry registry;
};

struct spwr_ac_device {
	struct ssam_device *sdev;

	char name[32];
	struct power_supply *psy;
	struct power_supply_desc psy_desc;

	struct ssam_event_notifier notif;

	struct mutex lock;  /* Guards access to state below. */

	__le32 state;
};


/* -- State management. ----------------------------------------------------- */

static int spwr_ac_update_unlocked(struct spwr_ac_device *ac)
{
	__le32 old = ac->state;
	int status;

	lockdep_assert_held(&ac->lock);

	status = ssam_retry(ssam_bat_get_psrc, ac->sdev, &ac->state);
	if (status < 0)
		return status;

	return old != ac->state;
}

static int spwr_ac_update(struct spwr_ac_device *ac)
{
	int status;

	mutex_lock(&ac->lock);
	status = spwr_ac_update_unlocked(ac);
	mutex_unlock(&ac->lock);

	return status;
}

static int spwr_ac_recheck(struct spwr_ac_device *ac)
{
	int status;

	status = spwr_ac_update(ac);
	if (status > 0)
		power_supply_changed(ac->psy);

	return status >= 0 ? 0 : status;
}

static u32 spwr_notify_ac(struct ssam_event_notifier *nf, const struct ssam_event *event)
{
	struct spwr_ac_device *ac;
	int status;

	ac = container_of(nf, struct spwr_ac_device, notif);

	dev_dbg(&ac->sdev->dev, "power event (cid = %#04x, iid = %#04x, tid = %#04x)\n",
		event->command_id, event->instance_id, event->target_id);

	/*
	 * Allow events of all targets/instances here. Global adapter status
	 * seems to be handled via target=1 and instance=1, but events are
	 * reported on all targets/instances in use.
	 *
	 * While it should be enough to just listen on 1/1, listen everywhere to
	 * make sure we don't miss anything.
	 */

	switch (event->command_id) {
	case SAM_EVENT_CID_BAT_ADP:
		status = spwr_ac_recheck(ac);
		return ssam_notifier_from_errno(status) | SSAM_NOTIF_HANDLED;

	default:
		return 0;
	}
}


/* -- Properties. ----------------------------------------------------------- */

static const enum power_supply_property spwr_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int spwr_ac_get_property(struct power_supply *psy, enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct spwr_ac_device *ac = power_supply_get_drvdata(psy);
	int status;

	mutex_lock(&ac->lock);

	status = spwr_ac_update_unlocked(ac);
	if (status)
		goto out;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = !!le32_to_cpu(ac->state);
		break;

	default:
		status = -EINVAL;
		goto out;
	}

out:
	mutex_unlock(&ac->lock);
	return status;
}


/* -- Device setup. --------------------------------------------------------- */

static char *battery_supplied_to[] = {
	"BAT1",
	"BAT2",
};

static void spwr_ac_init(struct spwr_ac_device *ac, struct ssam_device *sdev,
			 struct ssam_event_registry registry, const char *name)
{
	mutex_init(&ac->lock);
	strncpy(ac->name, name, ARRAY_SIZE(ac->name) - 1);

	ac->sdev = sdev;

	ac->notif.base.priority = 1;
	ac->notif.base.fn = spwr_notify_ac;
	ac->notif.event.reg = registry;
	ac->notif.event.id.target_category = sdev->uid.category;
	ac->notif.event.id.instance = 0;
	ac->notif.event.mask = SSAM_EVENT_MASK_NONE;
	ac->notif.event.flags = SSAM_EVENT_SEQUENCED;

	ac->psy_desc.name = ac->name;
	ac->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;
	ac->psy_desc.properties = spwr_ac_props;
	ac->psy_desc.num_properties = ARRAY_SIZE(spwr_ac_props);
	ac->psy_desc.get_property = spwr_ac_get_property;
}

static int spwr_ac_register(struct spwr_ac_device *ac)
{
	struct power_supply_config psy_cfg = {};
	__le32 sta;
	int status;

	/* Make sure the device is there and functioning properly. */
	status = ssam_retry(ssam_bat_get_sta, ac->sdev, &sta);
	if (status)
		return status;

	if ((le32_to_cpu(sta) & SAM_BATTERY_STA_OK) != SAM_BATTERY_STA_OK)
		return -ENODEV;

	psy_cfg.drv_data = ac;
	psy_cfg.supplied_to = battery_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(battery_supplied_to);

	ac->psy = devm_power_supply_register(&ac->sdev->dev, &ac->psy_desc, &psy_cfg);
	if (IS_ERR(ac->psy))
		return PTR_ERR(ac->psy);

	return ssam_notifier_register(ac->sdev->ctrl, &ac->notif);
}


/* -- Driver setup. --------------------------------------------------------- */

static int __maybe_unused surface_ac_resume(struct device *dev)
{
	return spwr_ac_recheck(dev_get_drvdata(dev));
}
static SIMPLE_DEV_PM_OPS(surface_ac_pm_ops, NULL, surface_ac_resume);

static int surface_ac_probe(struct ssam_device *sdev)
{
	const struct spwr_psy_properties *p;
	struct spwr_ac_device *ac;

	p = ssam_device_get_match_data(sdev);
	if (!p)
		return -ENODEV;

	ac = devm_kzalloc(&sdev->dev, sizeof(*ac), GFP_KERNEL);
	if (!ac)
		return -ENOMEM;

	spwr_ac_init(ac, sdev, p->registry, p->name);
	ssam_device_set_drvdata(sdev, ac);

	return spwr_ac_register(ac);
}

static void surface_ac_remove(struct ssam_device *sdev)
{
	struct spwr_ac_device *ac = ssam_device_get_drvdata(sdev);

	ssam_notifier_unregister(sdev->ctrl, &ac->notif);
}

static const struct spwr_psy_properties spwr_psy_props_adp1 = {
	.name = "ADP1",
	.registry = SSAM_EVENT_REGISTRY_SAM,
};

static const struct ssam_device_id surface_ac_match[] = {
	{ SSAM_SDEV(BAT, 0x01, 0x01, 0x01), (unsigned long)&spwr_psy_props_adp1 },
	{ },
};
MODULE_DEVICE_TABLE(ssam, surface_ac_match);

static struct ssam_device_driver surface_ac_driver = {
	.probe = surface_ac_probe,
	.remove = surface_ac_remove,
	.match_table = surface_ac_match,
	.driver = {
		.name = "surface_ac",
		.pm = &surface_ac_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_ssam_device_driver(surface_ac_driver);

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("AC driver for Surface System Aggregator Module");
MODULE_LICENSE("GPL");
