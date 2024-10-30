// SPDX-License-Identifier: GPL-2.0+
/*
 * Surface System Aggregator Module (SSAM) tablet mode switch driver.
 *
 * Copyright (C) 2022 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <linux/unaligned.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <linux/surface_aggregator/controller.h>
#include <linux/surface_aggregator/device.h>


/* -- SSAM generic tablet switch driver framework. -------------------------- */

struct ssam_tablet_sw;

struct ssam_tablet_sw_state {
	u32 source;
	u32 state;
};

struct ssam_tablet_sw_ops {
	int (*get_state)(struct ssam_tablet_sw *sw, struct ssam_tablet_sw_state *state);
	const char *(*state_name)(struct ssam_tablet_sw *sw,
				  const struct ssam_tablet_sw_state *state);
	bool (*state_is_tablet_mode)(struct ssam_tablet_sw *sw,
				     const struct ssam_tablet_sw_state *state);
};

struct ssam_tablet_sw {
	struct ssam_device *sdev;

	struct ssam_tablet_sw_state state;
	struct work_struct update_work;
	struct input_dev *mode_switch;

	struct ssam_tablet_sw_ops ops;
	struct ssam_event_notifier notif;
};

struct ssam_tablet_sw_desc {
	struct {
		const char *name;
		const char *phys;
	} dev;

	struct {
		u32 (*notify)(struct ssam_event_notifier *nf, const struct ssam_event *event);
		int (*get_state)(struct ssam_tablet_sw *sw, struct ssam_tablet_sw_state *state);
		const char *(*state_name)(struct ssam_tablet_sw *sw,
					  const struct ssam_tablet_sw_state *state);
		bool (*state_is_tablet_mode)(struct ssam_tablet_sw *sw,
					     const struct ssam_tablet_sw_state *state);
	} ops;

	struct {
		struct ssam_event_registry reg;
		struct ssam_event_id id;
		enum ssam_event_mask mask;
		u8 flags;
	} event;
};

static ssize_t state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ssam_tablet_sw *sw = dev_get_drvdata(dev);
	const char *state = sw->ops.state_name(sw, &sw->state);

	return sysfs_emit(buf, "%s\n", state);
}
static DEVICE_ATTR_RO(state);

static struct attribute *ssam_tablet_sw_attrs[] = {
	&dev_attr_state.attr,
	NULL,
};

static const struct attribute_group ssam_tablet_sw_group = {
	.attrs = ssam_tablet_sw_attrs,
};

static void ssam_tablet_sw_update_workfn(struct work_struct *work)
{
	struct ssam_tablet_sw *sw = container_of(work, struct ssam_tablet_sw, update_work);
	struct ssam_tablet_sw_state state;
	int tablet, status;

	status = sw->ops.get_state(sw, &state);
	if (status)
		return;

	if (sw->state.source == state.source && sw->state.state == state.state)
		return;
	sw->state = state;

	/* Send SW_TABLET_MODE event. */
	tablet = sw->ops.state_is_tablet_mode(sw, &state);
	input_report_switch(sw->mode_switch, SW_TABLET_MODE, tablet);
	input_sync(sw->mode_switch);
}

static int __maybe_unused ssam_tablet_sw_resume(struct device *dev)
{
	struct ssam_tablet_sw *sw = dev_get_drvdata(dev);

	schedule_work(&sw->update_work);
	return 0;
}
static SIMPLE_DEV_PM_OPS(ssam_tablet_sw_pm_ops, NULL, ssam_tablet_sw_resume);

static int ssam_tablet_sw_probe(struct ssam_device *sdev)
{
	const struct ssam_tablet_sw_desc *desc;
	struct ssam_tablet_sw *sw;
	int tablet, status;

	desc = ssam_device_get_match_data(sdev);
	if (!desc) {
		WARN(1, "no driver match data specified");
		return -EINVAL;
	}

	sw = devm_kzalloc(&sdev->dev, sizeof(*sw), GFP_KERNEL);
	if (!sw)
		return -ENOMEM;

	sw->sdev = sdev;

	sw->ops.get_state = desc->ops.get_state;
	sw->ops.state_name = desc->ops.state_name;
	sw->ops.state_is_tablet_mode = desc->ops.state_is_tablet_mode;

	INIT_WORK(&sw->update_work, ssam_tablet_sw_update_workfn);

	ssam_device_set_drvdata(sdev, sw);

	/* Get initial state. */
	status = sw->ops.get_state(sw, &sw->state);
	if (status)
		return status;

	/* Set up tablet mode switch. */
	sw->mode_switch = devm_input_allocate_device(&sdev->dev);
	if (!sw->mode_switch)
		return -ENOMEM;

	sw->mode_switch->name = desc->dev.name;
	sw->mode_switch->phys = desc->dev.phys;
	sw->mode_switch->id.bustype = BUS_HOST;
	sw->mode_switch->dev.parent = &sdev->dev;

	tablet = sw->ops.state_is_tablet_mode(sw, &sw->state);
	input_set_capability(sw->mode_switch, EV_SW, SW_TABLET_MODE);
	input_report_switch(sw->mode_switch, SW_TABLET_MODE, tablet);

	status = input_register_device(sw->mode_switch);
	if (status)
		return status;

	/* Set up notifier. */
	sw->notif.base.priority = 0;
	sw->notif.base.fn = desc->ops.notify;
	sw->notif.event.reg = desc->event.reg;
	sw->notif.event.id = desc->event.id;
	sw->notif.event.mask = desc->event.mask;
	sw->notif.event.flags = SSAM_EVENT_SEQUENCED;

	status = ssam_device_notifier_register(sdev, &sw->notif);
	if (status)
		return status;

	status = sysfs_create_group(&sdev->dev.kobj, &ssam_tablet_sw_group);
	if (status)
		goto err;

	/* We might have missed events during setup, so check again. */
	schedule_work(&sw->update_work);
	return 0;

err:
	ssam_device_notifier_unregister(sdev, &sw->notif);
	cancel_work_sync(&sw->update_work);
	return status;
}

static void ssam_tablet_sw_remove(struct ssam_device *sdev)
{
	struct ssam_tablet_sw *sw = ssam_device_get_drvdata(sdev);

	sysfs_remove_group(&sdev->dev.kobj, &ssam_tablet_sw_group);

	ssam_device_notifier_unregister(sdev, &sw->notif);
	cancel_work_sync(&sw->update_work);
}


/* -- SSAM KIP tablet switch implementation. -------------------------------- */

#define SSAM_EVENT_KIP_CID_COVER_STATE_CHANGED	0x1d

enum ssam_kip_cover_state {
	SSAM_KIP_COVER_STATE_DISCONNECTED  = 0x01,
	SSAM_KIP_COVER_STATE_CLOSED        = 0x02,
	SSAM_KIP_COVER_STATE_LAPTOP        = 0x03,
	SSAM_KIP_COVER_STATE_FOLDED_CANVAS = 0x04,
	SSAM_KIP_COVER_STATE_FOLDED_BACK   = 0x05,
	SSAM_KIP_COVER_STATE_BOOK          = 0x06,
};

static const char *ssam_kip_cover_state_name(struct ssam_tablet_sw *sw,
					     const struct ssam_tablet_sw_state *state)
{
	switch (state->state) {
	case SSAM_KIP_COVER_STATE_DISCONNECTED:
		return "disconnected";

	case SSAM_KIP_COVER_STATE_CLOSED:
		return "closed";

	case SSAM_KIP_COVER_STATE_LAPTOP:
		return "laptop";

	case SSAM_KIP_COVER_STATE_FOLDED_CANVAS:
		return "folded-canvas";

	case SSAM_KIP_COVER_STATE_FOLDED_BACK:
		return "folded-back";

	case SSAM_KIP_COVER_STATE_BOOK:
		return "book";

	default:
		dev_warn(&sw->sdev->dev, "unknown KIP cover state: %u\n", state->state);
		return "<unknown>";
	}
}

static bool ssam_kip_cover_state_is_tablet_mode(struct ssam_tablet_sw *sw,
						const struct ssam_tablet_sw_state *state)
{
	switch (state->state) {
	case SSAM_KIP_COVER_STATE_DISCONNECTED:
	case SSAM_KIP_COVER_STATE_FOLDED_CANVAS:
	case SSAM_KIP_COVER_STATE_FOLDED_BACK:
	case SSAM_KIP_COVER_STATE_BOOK:
		return true;

	case SSAM_KIP_COVER_STATE_CLOSED:
	case SSAM_KIP_COVER_STATE_LAPTOP:
		return false;

	default:
		dev_warn(&sw->sdev->dev, "unknown KIP cover state: %d\n", state->state);
		return true;
	}
}

SSAM_DEFINE_SYNC_REQUEST_R(__ssam_kip_get_cover_state, u8, {
	.target_category = SSAM_SSH_TC_KIP,
	.target_id       = SSAM_SSH_TID_SAM,
	.command_id      = 0x1d,
	.instance_id     = 0x00,
});

static int ssam_kip_get_cover_state(struct ssam_tablet_sw *sw, struct ssam_tablet_sw_state *state)
{
	int status;
	u8 raw;

	status = ssam_retry(__ssam_kip_get_cover_state, sw->sdev->ctrl, &raw);
	if (status < 0) {
		dev_err(&sw->sdev->dev, "failed to query KIP lid state: %d\n", status);
		return status;
	}

	state->source = 0;	/* Unused for KIP switch. */
	state->state = raw;
	return 0;
}

static u32 ssam_kip_sw_notif(struct ssam_event_notifier *nf, const struct ssam_event *event)
{
	struct ssam_tablet_sw *sw = container_of(nf, struct ssam_tablet_sw, notif);

	if (event->command_id != SSAM_EVENT_KIP_CID_COVER_STATE_CHANGED)
		return 0;	/* Return "unhandled". */

	if (event->length < 1)
		dev_warn(&sw->sdev->dev, "unexpected payload size: %u\n", event->length);

	schedule_work(&sw->update_work);
	return SSAM_NOTIF_HANDLED;
}

static const struct ssam_tablet_sw_desc ssam_kip_sw_desc = {
	.dev = {
		.name = "Microsoft Surface KIP Tablet Mode Switch",
		.phys = "ssam/01:0e:01:00:01/input0",
	},
	.ops = {
		.notify = ssam_kip_sw_notif,
		.get_state = ssam_kip_get_cover_state,
		.state_name = ssam_kip_cover_state_name,
		.state_is_tablet_mode = ssam_kip_cover_state_is_tablet_mode,
	},
	.event = {
		.reg = SSAM_EVENT_REGISTRY_SAM,
		.id = {
			.target_category = SSAM_SSH_TC_KIP,
			.instance = 0,
		},
		.mask = SSAM_EVENT_MASK_TARGET,
	},
};


/* -- SSAM POS tablet switch implementation. -------------------------------- */

static bool tablet_mode_in_slate_state = true;
module_param(tablet_mode_in_slate_state, bool, 0644);
MODULE_PARM_DESC(tablet_mode_in_slate_state, "Enable tablet mode in slate device posture, default is 'true'");

#define SSAM_EVENT_POS_CID_POSTURE_CHANGED	0x03
#define SSAM_POS_MAX_SOURCES			4

enum ssam_pos_source_id {
	SSAM_POS_SOURCE_COVER = 0x00,
	SSAM_POS_SOURCE_SLS   = 0x03,
};

enum ssam_pos_state_cover {
	SSAM_POS_COVER_DISCONNECTED  = 0x01,
	SSAM_POS_COVER_CLOSED        = 0x02,
	SSAM_POS_COVER_LAPTOP        = 0x03,
	SSAM_POS_COVER_FOLDED_CANVAS = 0x04,
	SSAM_POS_COVER_FOLDED_BACK   = 0x05,
	SSAM_POS_COVER_BOOK          = 0x06,
};

enum ssam_pos_state_sls {
	SSAM_POS_SLS_LID_CLOSED = 0x00,
	SSAM_POS_SLS_LAPTOP     = 0x01,
	SSAM_POS_SLS_SLATE      = 0x02,
	SSAM_POS_SLS_TABLET     = 0x03,
};

struct ssam_sources_list {
	__le32 count;
	__le32 id[SSAM_POS_MAX_SOURCES];
} __packed;

static const char *ssam_pos_state_name_cover(struct ssam_tablet_sw *sw, u32 state)
{
	switch (state) {
	case SSAM_POS_COVER_DISCONNECTED:
		return "disconnected";

	case SSAM_POS_COVER_CLOSED:
		return "closed";

	case SSAM_POS_COVER_LAPTOP:
		return "laptop";

	case SSAM_POS_COVER_FOLDED_CANVAS:
		return "folded-canvas";

	case SSAM_POS_COVER_FOLDED_BACK:
		return "folded-back";

	case SSAM_POS_COVER_BOOK:
		return "book";

	default:
		dev_warn(&sw->sdev->dev, "unknown device posture for type-cover: %u\n", state);
		return "<unknown>";
	}
}

static const char *ssam_pos_state_name_sls(struct ssam_tablet_sw *sw, u32 state)
{
	switch (state) {
	case SSAM_POS_SLS_LID_CLOSED:
		return "closed";

	case SSAM_POS_SLS_LAPTOP:
		return "laptop";

	case SSAM_POS_SLS_SLATE:
		return "slate";

	case SSAM_POS_SLS_TABLET:
		return "tablet";

	default:
		dev_warn(&sw->sdev->dev, "unknown device posture for SLS: %u\n", state);
		return "<unknown>";
	}
}

static const char *ssam_pos_state_name(struct ssam_tablet_sw *sw,
				       const struct ssam_tablet_sw_state *state)
{
	switch (state->source) {
	case SSAM_POS_SOURCE_COVER:
		return ssam_pos_state_name_cover(sw, state->state);

	case SSAM_POS_SOURCE_SLS:
		return ssam_pos_state_name_sls(sw, state->state);

	default:
		dev_warn(&sw->sdev->dev, "unknown device posture source: %u\n", state->source);
		return "<unknown>";
	}
}

static bool ssam_pos_state_is_tablet_mode_cover(struct ssam_tablet_sw *sw, u32 state)
{
	switch (state) {
	case SSAM_POS_COVER_DISCONNECTED:
	case SSAM_POS_COVER_FOLDED_CANVAS:
	case SSAM_POS_COVER_FOLDED_BACK:
	case SSAM_POS_COVER_BOOK:
		return true;

	case SSAM_POS_COVER_CLOSED:
	case SSAM_POS_COVER_LAPTOP:
		return false;

	default:
		dev_warn(&sw->sdev->dev, "unknown device posture for type-cover: %u\n", state);
		return true;
	}
}

static bool ssam_pos_state_is_tablet_mode_sls(struct ssam_tablet_sw *sw, u32 state)
{
	switch (state) {
	case SSAM_POS_SLS_LAPTOP:
	case SSAM_POS_SLS_LID_CLOSED:
		return false;

	case SSAM_POS_SLS_SLATE:
		return tablet_mode_in_slate_state;

	case SSAM_POS_SLS_TABLET:
		return true;

	default:
		dev_warn(&sw->sdev->dev, "unknown device posture for SLS: %u\n", state);
		return true;
	}
}

static bool ssam_pos_state_is_tablet_mode(struct ssam_tablet_sw *sw,
					  const struct ssam_tablet_sw_state *state)
{
	switch (state->source) {
	case SSAM_POS_SOURCE_COVER:
		return ssam_pos_state_is_tablet_mode_cover(sw, state->state);

	case SSAM_POS_SOURCE_SLS:
		return ssam_pos_state_is_tablet_mode_sls(sw, state->state);

	default:
		dev_warn(&sw->sdev->dev, "unknown device posture source: %u\n", state->source);
		return true;
	}
}

static int ssam_pos_get_sources_list(struct ssam_tablet_sw *sw, struct ssam_sources_list *sources)
{
	struct ssam_request rqst;
	struct ssam_response rsp;
	int status;

	rqst.target_category = SSAM_SSH_TC_POS;
	rqst.target_id = SSAM_SSH_TID_SAM;
	rqst.command_id = 0x01;
	rqst.instance_id = 0x00;
	rqst.flags = SSAM_REQUEST_HAS_RESPONSE;
	rqst.length = 0;
	rqst.payload = NULL;

	rsp.capacity = sizeof(*sources);
	rsp.length = 0;
	rsp.pointer = (u8 *)sources;

	status = ssam_retry(ssam_request_do_sync_onstack, sw->sdev->ctrl, &rqst, &rsp, 0);
	if (status)
		return status;

	/* We need at least the 'sources->count' field. */
	if (rsp.length < sizeof(__le32)) {
		dev_err(&sw->sdev->dev, "received source list response is too small\n");
		return -EPROTO;
	}

	/* Make sure 'sources->count' matches with the response length. */
	if (get_unaligned_le32(&sources->count) * sizeof(__le32) + sizeof(__le32) != rsp.length) {
		dev_err(&sw->sdev->dev, "mismatch between number of sources and response size\n");
		return -EPROTO;
	}

	return 0;
}

static int ssam_pos_get_source(struct ssam_tablet_sw *sw, u32 *source_id)
{
	struct ssam_sources_list sources = {};
	int status;

	status = ssam_pos_get_sources_list(sw, &sources);
	if (status)
		return status;

	if (get_unaligned_le32(&sources.count) == 0) {
		dev_err(&sw->sdev->dev, "no posture sources found\n");
		return -ENODEV;
	}

	/*
	 * We currently don't know what to do with more than one posture
	 * source. At the moment, only one source seems to be used/provided.
	 * The WARN_ON() here should hopefully let us know quickly once there
	 * is a device that provides multiple sources, at which point we can
	 * then try to figure out how to handle them.
	 */
	WARN_ON(get_unaligned_le32(&sources.count) > 1);

	*source_id = get_unaligned_le32(&sources.id[0]);
	return 0;
}

SSAM_DEFINE_SYNC_REQUEST_WR(__ssam_pos_get_posture_for_source, __le32, __le32, {
	.target_category = SSAM_SSH_TC_POS,
	.target_id       = SSAM_SSH_TID_SAM,
	.command_id      = 0x02,
	.instance_id     = 0x00,
});

static int ssam_pos_get_posture_for_source(struct ssam_tablet_sw *sw, u32 source_id, u32 *posture)
{
	__le32 source_le = cpu_to_le32(source_id);
	__le32 rspval_le = 0;
	int status;

	status = ssam_retry(__ssam_pos_get_posture_for_source, sw->sdev->ctrl,
			    &source_le, &rspval_le);
	if (status)
		return status;

	*posture = le32_to_cpu(rspval_le);
	return 0;
}

static int ssam_pos_get_posture(struct ssam_tablet_sw *sw, struct ssam_tablet_sw_state *state)
{
	u32 source_id;
	u32 source_state;
	int status;

	status = ssam_pos_get_source(sw, &source_id);
	if (status) {
		dev_err(&sw->sdev->dev, "failed to get posture source ID: %d\n", status);
		return status;
	}

	status = ssam_pos_get_posture_for_source(sw, source_id, &source_state);
	if (status) {
		dev_err(&sw->sdev->dev, "failed to get posture value for source %u: %d\n",
			source_id, status);
		return status;
	}

	state->source = source_id;
	state->state = source_state;
	return 0;
}

static u32 ssam_pos_sw_notif(struct ssam_event_notifier *nf, const struct ssam_event *event)
{
	struct ssam_tablet_sw *sw = container_of(nf, struct ssam_tablet_sw, notif);

	if (event->command_id != SSAM_EVENT_POS_CID_POSTURE_CHANGED)
		return 0;	/* Return "unhandled". */

	if (event->length != sizeof(__le32) * 3)
		dev_warn(&sw->sdev->dev, "unexpected payload size: %u\n", event->length);

	schedule_work(&sw->update_work);
	return SSAM_NOTIF_HANDLED;
}

static const struct ssam_tablet_sw_desc ssam_pos_sw_desc = {
	.dev = {
		.name = "Microsoft Surface POS Tablet Mode Switch",
		.phys = "ssam/01:26:01:00:01/input0",
	},
	.ops = {
		.notify = ssam_pos_sw_notif,
		.get_state = ssam_pos_get_posture,
		.state_name = ssam_pos_state_name,
		.state_is_tablet_mode = ssam_pos_state_is_tablet_mode,
	},
	.event = {
		.reg = SSAM_EVENT_REGISTRY_SAM,
		.id = {
			.target_category = SSAM_SSH_TC_POS,
			.instance = 0,
		},
		.mask = SSAM_EVENT_MASK_TARGET,
	},
};


/* -- Driver registration. -------------------------------------------------- */

static const struct ssam_device_id ssam_tablet_sw_match[] = {
	{ SSAM_SDEV(KIP, SAM, 0x00, 0x01), (unsigned long)&ssam_kip_sw_desc },
	{ SSAM_SDEV(POS, SAM, 0x00, 0x01), (unsigned long)&ssam_pos_sw_desc },
	{ },
};
MODULE_DEVICE_TABLE(ssam, ssam_tablet_sw_match);

static struct ssam_device_driver ssam_tablet_sw_driver = {
	.probe = ssam_tablet_sw_probe,
	.remove = ssam_tablet_sw_remove,
	.match_table = ssam_tablet_sw_match,
	.driver = {
		.name = "surface_aggregator_tablet_mode_switch",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm = &ssam_tablet_sw_pm_ops,
	},
};
module_ssam_device_driver(ssam_tablet_sw_driver);

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("Tablet mode switch driver for Surface devices using the Surface Aggregator Module");
MODULE_LICENSE("GPL");
