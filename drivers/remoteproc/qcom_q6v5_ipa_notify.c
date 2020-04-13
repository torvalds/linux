// SPDX-License-Identifier: GPL-2.0

/*
 * Qualcomm IPA notification subdev support
 *
 * Copyright (C) 2019 Linaro Ltd.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/qcom_q6v5_ipa_notify.h>

static void
ipa_notify_common(struct rproc_subdev *subdev, enum qcom_rproc_event event)
{
	struct qcom_rproc_ipa_notify *ipa_notify;
	qcom_ipa_notify_t notify;

	ipa_notify = container_of(subdev, struct qcom_rproc_ipa_notify, subdev);
	notify = ipa_notify->notify;
	if (notify)
		notify(ipa_notify->data, event);
}

static int ipa_notify_prepare(struct rproc_subdev *subdev)
{
	ipa_notify_common(subdev, MODEM_STARTING);

	return 0;
}

static int ipa_notify_start(struct rproc_subdev *subdev)
{
	ipa_notify_common(subdev, MODEM_RUNNING);

	return 0;
}

static void ipa_notify_stop(struct rproc_subdev *subdev, bool crashed)

{
	ipa_notify_common(subdev, crashed ? MODEM_CRASHED : MODEM_STOPPING);
}

static void ipa_notify_unprepare(struct rproc_subdev *subdev)
{
	ipa_notify_common(subdev, MODEM_OFFLINE);
}

static void ipa_notify_removing(struct rproc_subdev *subdev)
{
	ipa_notify_common(subdev, MODEM_REMOVING);
}

/* Register the IPA notification subdevice with the Q6V5 MSS remoteproc */
void qcom_add_ipa_notify_subdev(struct rproc *rproc,
		struct qcom_rproc_ipa_notify *ipa_notify)
{
	ipa_notify->notify = NULL;
	ipa_notify->data = NULL;
	ipa_notify->subdev.prepare = ipa_notify_prepare;
	ipa_notify->subdev.start = ipa_notify_start;
	ipa_notify->subdev.stop = ipa_notify_stop;
	ipa_notify->subdev.unprepare = ipa_notify_unprepare;

	rproc_add_subdev(rproc, &ipa_notify->subdev);
}
EXPORT_SYMBOL_GPL(qcom_add_ipa_notify_subdev);

/* Remove the IPA notification subdevice */
void qcom_remove_ipa_notify_subdev(struct rproc *rproc,
		struct qcom_rproc_ipa_notify *ipa_notify)
{
	struct rproc_subdev *subdev = &ipa_notify->subdev;

	ipa_notify_removing(subdev);

	rproc_remove_subdev(rproc, subdev);
	ipa_notify->notify = NULL;	/* Make it obvious */
}
EXPORT_SYMBOL_GPL(qcom_remove_ipa_notify_subdev);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm IPA notification remoteproc subdev");
