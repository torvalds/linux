// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Surface System Aggregator Module (SSAM) subsystem device hubs.
 *
 * Provides a driver for SSAM subsystems device hubs. This driver performs
 * instantiation of the devices managed by said hubs and takes care of
 * (hot-)removal.
 *
 * Copyright (C) 2020-2022 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <linux/surface_aggregator/device.h>


/* -- SSAM generic subsystem hub driver framework. -------------------------- */

enum ssam_hub_state {
	SSAM_HUB_UNINITIALIZED,		/* Only set during initialization. */
	SSAM_HUB_CONNECTED,
	SSAM_HUB_DISCONNECTED,
};

enum ssam_hub_flags {
	SSAM_HUB_HOT_REMOVED,
};

struct ssam_hub;

struct ssam_hub_ops {
	int (*get_state)(struct ssam_hub *hub, enum ssam_hub_state *state);
};

struct ssam_hub {
	struct ssam_device *sdev;

	enum ssam_hub_state state;
	unsigned long flags;

	struct delayed_work update_work;
	unsigned long connect_delay;

	struct ssam_event_notifier notif;
	struct ssam_hub_ops ops;
};

struct ssam_hub_desc {
	struct {
		struct ssam_event_registry reg;
		struct ssam_event_id id;
		enum ssam_event_mask mask;
	} event;

	struct {
		u32 (*notify)(struct ssam_event_notifier *nf, const struct ssam_event *event);
		int (*get_state)(struct ssam_hub *hub, enum ssam_hub_state *state);
	} ops;

	unsigned long connect_delay_ms;
};

static void ssam_hub_update_workfn(struct work_struct *work)
{
	struct ssam_hub *hub = container_of(work, struct ssam_hub, update_work.work);
	enum ssam_hub_state state;
	int status = 0;

	status = hub->ops.get_state(hub, &state);
	if (status)
		return;

	/*
	 * There is a small possibility that hub devices were hot-removed and
	 * re-added before we were able to remove them here. In that case, both
	 * the state returned by get_state() and the state of the hub will
	 * equal SSAM_HUB_CONNECTED and we would bail early below, which would
	 * leave child devices without proper (re-)initialization and the
	 * hot-remove flag set.
	 *
	 * Therefore, we check whether devices have been hot-removed via an
	 * additional flag on the hub and, in this case, override the returned
	 * hub state. In case of a missed disconnect (i.e. get_state returned
	 * "connected"), we further need to re-schedule this work (with the
	 * appropriate delay) as the actual connect work submission might have
	 * been merged with this one.
	 *
	 * This then leads to one of two cases: Either we submit an unnecessary
	 * work item (which will get ignored via either the queue or the state
	 * checks) or, in the unlikely case that the work is actually required,
	 * double the normal connect delay.
	 */
	if (test_and_clear_bit(SSAM_HUB_HOT_REMOVED, &hub->flags)) {
		if (state == SSAM_HUB_CONNECTED)
			schedule_delayed_work(&hub->update_work, hub->connect_delay);

		state = SSAM_HUB_DISCONNECTED;
	}

	if (hub->state == state)
		return;
	hub->state = state;

	if (hub->state == SSAM_HUB_CONNECTED)
		status = ssam_device_register_clients(hub->sdev);
	else
		ssam_remove_clients(&hub->sdev->dev);

	if (status)
		dev_err(&hub->sdev->dev, "failed to update hub child devices: %d\n", status);
}

static int ssam_hub_mark_hot_removed(struct device *dev, void *_data)
{
	struct ssam_device *sdev = to_ssam_device(dev);

	if (is_ssam_device(dev))
		ssam_device_mark_hot_removed(sdev);

	return 0;
}

static void ssam_hub_update(struct ssam_hub *hub, bool connected)
{
	unsigned long delay;

	/* Mark devices as hot-removed before we remove any. */
	if (!connected) {
		set_bit(SSAM_HUB_HOT_REMOVED, &hub->flags);
		device_for_each_child_reverse(&hub->sdev->dev, NULL, ssam_hub_mark_hot_removed);
	}

	/*
	 * Delay update when the base/keyboard cover is being connected to give
	 * devices/EC some time to set up.
	 */
	delay = connected ? hub->connect_delay : 0;

	schedule_delayed_work(&hub->update_work, delay);
}

static int __maybe_unused ssam_hub_resume(struct device *dev)
{
	struct ssam_hub *hub = dev_get_drvdata(dev);

	schedule_delayed_work(&hub->update_work, 0);
	return 0;
}
static SIMPLE_DEV_PM_OPS(ssam_hub_pm_ops, NULL, ssam_hub_resume);

static int ssam_hub_probe(struct ssam_device *sdev)
{
	const struct ssam_hub_desc *desc;
	struct ssam_hub *hub;
	int status;

	desc = ssam_device_get_match_data(sdev);
	if (!desc) {
		WARN(1, "no driver match data specified");
		return -EINVAL;
	}

	hub = devm_kzalloc(&sdev->dev, sizeof(*hub), GFP_KERNEL);
	if (!hub)
		return -ENOMEM;

	hub->sdev = sdev;
	hub->state = SSAM_HUB_UNINITIALIZED;

	hub->notif.base.priority = INT_MAX;  /* This notifier should run first. */
	hub->notif.base.fn = desc->ops.notify;
	hub->notif.event.reg = desc->event.reg;
	hub->notif.event.id = desc->event.id;
	hub->notif.event.mask = desc->event.mask;
	hub->notif.event.flags = SSAM_EVENT_SEQUENCED;

	hub->connect_delay = msecs_to_jiffies(desc->connect_delay_ms);
	hub->ops.get_state = desc->ops.get_state;

	INIT_DELAYED_WORK(&hub->update_work, ssam_hub_update_workfn);

	ssam_device_set_drvdata(sdev, hub);

	status = ssam_device_notifier_register(sdev, &hub->notif);
	if (status)
		return status;

	schedule_delayed_work(&hub->update_work, 0);
	return 0;
}

static void ssam_hub_remove(struct ssam_device *sdev)
{
	struct ssam_hub *hub = ssam_device_get_drvdata(sdev);

	ssam_device_notifier_unregister(sdev, &hub->notif);
	cancel_delayed_work_sync(&hub->update_work);
	ssam_remove_clients(&sdev->dev);
}


/* -- SSAM base-subsystem hub driver. --------------------------------------- */

/*
 * Some devices (especially battery) may need a bit of time to be fully usable
 * after being (re-)connected. This delay has been determined via
 * experimentation.
 */
#define SSAM_BASE_UPDATE_CONNECT_DELAY		2500

SSAM_DEFINE_SYNC_REQUEST_R(ssam_bas_query_opmode, u8, {
	.target_category = SSAM_SSH_TC_BAS,
	.target_id       = 0x01,
	.command_id      = 0x0d,
	.instance_id     = 0x00,
});

#define SSAM_BAS_OPMODE_TABLET		0x00
#define SSAM_EVENT_BAS_CID_CONNECTION	0x0c

static int ssam_base_hub_query_state(struct ssam_hub *hub, enum ssam_hub_state *state)
{
	u8 opmode;
	int status;

	status = ssam_retry(ssam_bas_query_opmode, hub->sdev->ctrl, &opmode);
	if (status < 0) {
		dev_err(&hub->sdev->dev, "failed to query base state: %d\n", status);
		return status;
	}

	if (opmode != SSAM_BAS_OPMODE_TABLET)
		*state = SSAM_HUB_CONNECTED;
	else
		*state = SSAM_HUB_DISCONNECTED;

	return 0;
}

static u32 ssam_base_hub_notif(struct ssam_event_notifier *nf, const struct ssam_event *event)
{
	struct ssam_hub *hub = container_of(nf, struct ssam_hub, notif);

	if (event->command_id != SSAM_EVENT_BAS_CID_CONNECTION)
		return 0;

	if (event->length < 1) {
		dev_err(&hub->sdev->dev, "unexpected payload size: %u\n", event->length);
		return 0;
	}

	ssam_hub_update(hub, event->data[0]);

	/*
	 * Do not return SSAM_NOTIF_HANDLED: The event should be picked up and
	 * consumed by the detachment system driver. We're just a (more or less)
	 * silent observer.
	 */
	return 0;
}

static const struct ssam_hub_desc base_hub = {
	.event = {
		.reg = SSAM_EVENT_REGISTRY_SAM,
		.id = {
			.target_category = SSAM_SSH_TC_BAS,
			.instance = 0,
		},
		.mask = SSAM_EVENT_MASK_NONE,
	},
	.ops = {
		.notify = ssam_base_hub_notif,
		.get_state = ssam_base_hub_query_state,
	},
	.connect_delay_ms = SSAM_BASE_UPDATE_CONNECT_DELAY,
};


/* -- SSAM KIP-subsystem hub driver. ---------------------------------------- */

/*
 * Some devices may need a bit of time to be fully usable after being
 * (re-)connected. This delay has been determined via experimentation.
 */
#define SSAM_KIP_UPDATE_CONNECT_DELAY		250

#define SSAM_EVENT_KIP_CID_CONNECTION		0x2c

SSAM_DEFINE_SYNC_REQUEST_R(__ssam_kip_query_state, u8, {
	.target_category = SSAM_SSH_TC_KIP,
	.target_id       = 0x01,
	.command_id      = 0x2c,
	.instance_id     = 0x00,
});

static int ssam_kip_hub_query_state(struct ssam_hub *hub, enum ssam_hub_state *state)
{
	int status;
	u8 connected;

	status = ssam_retry(__ssam_kip_query_state, hub->sdev->ctrl, &connected);
	if (status < 0) {
		dev_err(&hub->sdev->dev, "failed to query KIP connection state: %d\n", status);
		return status;
	}

	*state = connected ? SSAM_HUB_CONNECTED : SSAM_HUB_DISCONNECTED;
	return 0;
}

static u32 ssam_kip_hub_notif(struct ssam_event_notifier *nf, const struct ssam_event *event)
{
	struct ssam_hub *hub = container_of(nf, struct ssam_hub, notif);

	if (event->command_id != SSAM_EVENT_KIP_CID_CONNECTION)
		return 0;	/* Return "unhandled". */

	if (event->length < 1) {
		dev_err(&hub->sdev->dev, "unexpected payload size: %u\n", event->length);
		return 0;
	}

	ssam_hub_update(hub, event->data[0]);
	return SSAM_NOTIF_HANDLED;
}

static const struct ssam_hub_desc kip_hub = {
	.event = {
		.reg = SSAM_EVENT_REGISTRY_SAM,
		.id = {
			.target_category = SSAM_SSH_TC_KIP,
			.instance = 0,
		},
		.mask = SSAM_EVENT_MASK_TARGET,
	},
	.ops = {
		.notify = ssam_kip_hub_notif,
		.get_state = ssam_kip_hub_query_state,
	},
	.connect_delay_ms = SSAM_KIP_UPDATE_CONNECT_DELAY,
};


/* -- Driver registration. -------------------------------------------------- */

static const struct ssam_device_id ssam_hub_match[] = {
	{ SSAM_VDEV(HUB, 0x01, SSAM_SSH_TC_KIP, 0x00), (unsigned long)&kip_hub  },
	{ SSAM_VDEV(HUB, 0x02, SSAM_SSH_TC_BAS, 0x00), (unsigned long)&base_hub },
	{ }
};
MODULE_DEVICE_TABLE(ssam, ssam_hub_match);

static struct ssam_device_driver ssam_subsystem_hub_driver = {
	.probe = ssam_hub_probe,
	.remove = ssam_hub_remove,
	.match_table = ssam_hub_match,
	.driver = {
		.name = "surface_aggregator_subsystem_hub",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm = &ssam_hub_pm_ops,
	},
};
module_ssam_device_driver(ssam_subsystem_hub_driver);

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("Subsystem device hub driver for Surface System Aggregator Module");
MODULE_LICENSE("GPL");
