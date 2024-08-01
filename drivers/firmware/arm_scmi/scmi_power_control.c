// SPDX-License-Identifier: GPL-2.0
/*
 * SCMI Generic SystemPower Control driver.
 *
 * Copyright (C) 2020-2022 ARM Ltd.
 */
/*
 * In order to handle platform originated SCMI SystemPower requests (like
 * shutdowns or cold/warm resets) we register an SCMI Notification notifier
 * block to react when such SCMI SystemPower events are emitted by platform.
 *
 * Once such a notification is received we act accordingly to perform the
 * required system transition depending on the kind of request.
 *
 * Graceful requests are routed to userspace through the same API methods
 * (orderly_poweroff/reboot()) used by ACPI when handling ACPI Shutdown bus
 * events.
 *
 * Direct forceful requests are not supported since are not meant to be sent
 * by the SCMI platform to an OSPM like Linux.
 *
 * Additionally, graceful request notifications can carry an optional timeout
 * field stating the maximum amount of time allowed by the platform for
 * completion after which they are converted to forceful ones: the assumption
 * here is that even graceful requests can be upper-bound by a maximum final
 * timeout strictly enforced by the platform itself which can ultimately cut
 * the power off at will anytime; in order to avoid such extreme scenario, we
 * track progress of graceful requests through the means of a reboot notifier
 * converting timed-out graceful requests to forceful ones, so at least we
 * try to perform a clean sync and shutdown/restart before the power is cut.
 *
 * Given the peculiar nature of SCMI SystemPower protocol, that is being in
 * charge of triggering system wide shutdown/reboot events, there should be
 * only one SCMI platform actively emitting SystemPower events.
 * For this reason the SCMI core takes care to enforce the creation of one
 * single unique device associated to the SCMI System Power protocol; no matter
 * how many SCMI platforms are defined on the system, only one can be designated
 * to support System Power: as a consequence this driver will never be probed
 * more than once.
 *
 * For similar reasons as soon as the first valid SystemPower is received by
 * this driver and the shutdown/reboot is started, any further notification
 * possibly emitted by the platform will be ignored.
 */

#include <linux/math.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/reboot.h>
#include <linux/scmi_protocol.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/time64.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#ifndef MODULE
#include <linux/fs.h>
#endif

enum scmi_syspower_state {
	SCMI_SYSPOWER_IDLE,
	SCMI_SYSPOWER_IN_PROGRESS,
	SCMI_SYSPOWER_REBOOTING
};

/**
 * struct scmi_syspower_conf  -  Common configuration
 *
 * @dev: A reference device
 * @state: Current SystemPower state
 * @state_mtx: @state related mutex
 * @required_transition: The requested transition as decribed in the received
 *			 SCMI SystemPower notification
 * @userspace_nb: The notifier_block registered against the SCMI SystemPower
 *		  notification to start the needed userspace interactions.
 * @reboot_nb: A notifier_block optionally used to track reboot progress
 * @forceful_work: A worker used to trigger a forceful transition once a
 *		   graceful has timed out.
 * @suspend_work: A worker used to trigger system suspend
 */
struct scmi_syspower_conf {
	struct device *dev;
	enum scmi_syspower_state state;
	/* Protect access to state */
	struct mutex state_mtx;
	enum scmi_system_events required_transition;

	struct notifier_block userspace_nb;
	struct notifier_block reboot_nb;

	struct delayed_work forceful_work;
	struct work_struct suspend_work;
};

#define userspace_nb_to_sconf(x)	\
	container_of(x, struct scmi_syspower_conf, userspace_nb)

#define reboot_nb_to_sconf(x)		\
	container_of(x, struct scmi_syspower_conf, reboot_nb)

#define dwork_to_sconf(x)		\
	container_of(x, struct scmi_syspower_conf, forceful_work)

/**
 * scmi_reboot_notifier  - A reboot notifier to catch an ongoing successful
 * system transition
 * @nb: Reference to the related notifier block
 * @reason: The reason for the ongoing reboot
 * @__unused: The cmd being executed on a restart request (unused)
 *
 * When an ongoing system transition is detected, compatible with the one
 * requested by SCMI, cancel the delayed work.
 *
 * Return: NOTIFY_OK in any case
 */
static int scmi_reboot_notifier(struct notifier_block *nb,
				unsigned long reason, void *__unused)
{
	struct scmi_syspower_conf *sc = reboot_nb_to_sconf(nb);

	mutex_lock(&sc->state_mtx);
	switch (reason) {
	case SYS_HALT:
	case SYS_POWER_OFF:
		if (sc->required_transition == SCMI_SYSTEM_SHUTDOWN)
			sc->state = SCMI_SYSPOWER_REBOOTING;
		break;
	case SYS_RESTART:
		if (sc->required_transition == SCMI_SYSTEM_COLDRESET ||
		    sc->required_transition == SCMI_SYSTEM_WARMRESET)
			sc->state = SCMI_SYSPOWER_REBOOTING;
		break;
	default:
		break;
	}

	if (sc->state == SCMI_SYSPOWER_REBOOTING) {
		dev_dbg(sc->dev, "Reboot in progress...cancel delayed work.\n");
		cancel_delayed_work_sync(&sc->forceful_work);
	}
	mutex_unlock(&sc->state_mtx);

	return NOTIFY_OK;
}

/**
 * scmi_request_forceful_transition  - Request forceful SystemPower transition
 * @sc: A reference to the configuration data
 *
 * Initiates the required SystemPower transition without involving userspace:
 * just trigger the action at the kernel level after issuing an emergency
 * sync. (if possible at all)
 */
static inline void
scmi_request_forceful_transition(struct scmi_syspower_conf *sc)
{
	dev_dbg(sc->dev, "Serving forceful request:%d\n",
		sc->required_transition);

#ifndef MODULE
	emergency_sync();
#endif
	switch (sc->required_transition) {
	case SCMI_SYSTEM_SHUTDOWN:
		kernel_power_off();
		break;
	case SCMI_SYSTEM_COLDRESET:
	case SCMI_SYSTEM_WARMRESET:
		kernel_restart(NULL);
		break;
	default:
		break;
	}
}

static void scmi_forceful_work_func(struct work_struct *work)
{
	struct scmi_syspower_conf *sc;
	struct delayed_work *dwork;

	if (system_state > SYSTEM_RUNNING)
		return;

	dwork = to_delayed_work(work);
	sc = dwork_to_sconf(dwork);

	dev_dbg(sc->dev, "Graceful request timed out...forcing !\n");
	mutex_lock(&sc->state_mtx);
	/* avoid deadlock by unregistering reboot notifier first */
	unregister_reboot_notifier(&sc->reboot_nb);
	if (sc->state == SCMI_SYSPOWER_IN_PROGRESS)
		scmi_request_forceful_transition(sc);
	mutex_unlock(&sc->state_mtx);
}

/**
 * scmi_request_graceful_transition  - Request graceful SystemPower transition
 * @sc: A reference to the configuration data
 * @timeout_ms: The desired timeout to wait for the shutdown to complete before
 *		system is forcibly shutdown.
 *
 * Initiates the required SystemPower transition, requesting userspace
 * co-operation: it uses the same orderly_ methods used by ACPI Shutdown event
 * processing.
 *
 * Takes care also to register a reboot notifier and to schedule a delayed work
 * in order to detect if userspace actions are taking too long and in such a
 * case to trigger a forceful transition.
 */
static void scmi_request_graceful_transition(struct scmi_syspower_conf *sc,
					     unsigned int timeout_ms)
{
	unsigned int adj_timeout_ms = 0;

	if (timeout_ms) {
		int ret;

		sc->reboot_nb.notifier_call = &scmi_reboot_notifier;
		ret = register_reboot_notifier(&sc->reboot_nb);
		if (!ret) {
			/* Wait only up to 75% of the advertised timeout */
			adj_timeout_ms = mult_frac(timeout_ms, 3, 4);
			INIT_DELAYED_WORK(&sc->forceful_work,
					  scmi_forceful_work_func);
			schedule_delayed_work(&sc->forceful_work,
					      msecs_to_jiffies(adj_timeout_ms));
		} else {
			/* Carry on best effort even without a reboot notifier */
			dev_warn(sc->dev,
				 "Cannot register reboot notifier !\n");
		}
	}

	dev_dbg(sc->dev,
		"Serving graceful req:%d (timeout_ms:%u  adj_timeout_ms:%u)\n",
		sc->required_transition, timeout_ms, adj_timeout_ms);

	switch (sc->required_transition) {
	case SCMI_SYSTEM_SHUTDOWN:
		/*
		 * When triggered early at boot-time the 'orderly' call will
		 * partially fail due to the lack of userspace itself, but
		 * the force=true argument will start anyway a successful
		 * forced shutdown.
		 */
		orderly_poweroff(true);
		break;
	case SCMI_SYSTEM_COLDRESET:
	case SCMI_SYSTEM_WARMRESET:
		orderly_reboot();
		break;
	case SCMI_SYSTEM_SUSPEND:
		schedule_work(&sc->suspend_work);
		break;
	default:
		break;
	}
}

/**
 * scmi_userspace_notifier  - Notifier callback to act on SystemPower
 * Notifications
 * @nb: Reference to the related notifier block
 * @event: The SystemPower notification event id
 * @data: The SystemPower event report
 *
 * This callback is in charge of decoding the received SystemPower report
 * and act accordingly triggering a graceful or forceful system transition.
 *
 * Note that once a valid SCMI SystemPower event starts being served, any
 * other following SystemPower notification received from the same SCMI
 * instance (handle) will be ignored.
 *
 * Return: NOTIFY_OK once a valid SystemPower event has been successfully
 * processed.
 */
static int scmi_userspace_notifier(struct notifier_block *nb,
				   unsigned long event, void *data)
{
	struct scmi_system_power_state_notifier_report *er = data;
	struct scmi_syspower_conf *sc = userspace_nb_to_sconf(nb);

	if (er->system_state >= SCMI_SYSTEM_MAX ||
	    er->system_state == SCMI_SYSTEM_POWERUP) {
		dev_err(sc->dev, "Ignoring unsupported system_state: 0x%X\n",
			er->system_state);
		return NOTIFY_DONE;
	}

	if (!SCMI_SYSPOWER_IS_REQUEST_GRACEFUL(er->flags)) {
		dev_err(sc->dev, "Ignoring forceful notification.\n");
		return NOTIFY_DONE;
	}

	/*
	 * Bail out if system is already shutting down or an SCMI SystemPower
	 * requested is already being served.
	 */
	if (system_state > SYSTEM_RUNNING)
		return NOTIFY_DONE;
	mutex_lock(&sc->state_mtx);
	if (sc->state != SCMI_SYSPOWER_IDLE) {
		dev_dbg(sc->dev,
			"Transition already in progress...ignore.\n");
		mutex_unlock(&sc->state_mtx);
		return NOTIFY_DONE;
	}
	sc->state = SCMI_SYSPOWER_IN_PROGRESS;
	mutex_unlock(&sc->state_mtx);

	sc->required_transition = er->system_state;

	/* Leaving a trace in logs of who triggered the shutdown/reboot. */
	dev_info(sc->dev, "Serving shutdown/reboot request: %d\n",
		 sc->required_transition);

	scmi_request_graceful_transition(sc, er->timeout);

	return NOTIFY_OK;
}

static void scmi_suspend_work_func(struct work_struct *work)
{
	struct scmi_syspower_conf *sc =
		container_of(work, struct scmi_syspower_conf, suspend_work);

	pm_suspend(PM_SUSPEND_MEM);

	sc->state = SCMI_SYSPOWER_IDLE;
}

static int scmi_syspower_probe(struct scmi_device *sdev)
{
	int ret;
	struct scmi_syspower_conf *sc;
	struct scmi_handle *handle = sdev->handle;

	if (!handle)
		return -ENODEV;

	ret = handle->devm_protocol_acquire(sdev, SCMI_PROTOCOL_SYSTEM);
	if (ret)
		return ret;

	sc = devm_kzalloc(&sdev->dev, sizeof(*sc), GFP_KERNEL);
	if (!sc)
		return -ENOMEM;

	sc->state = SCMI_SYSPOWER_IDLE;
	mutex_init(&sc->state_mtx);
	sc->required_transition = SCMI_SYSTEM_MAX;
	sc->userspace_nb.notifier_call = &scmi_userspace_notifier;
	sc->dev = &sdev->dev;

	INIT_WORK(&sc->suspend_work, scmi_suspend_work_func);

	return handle->notify_ops->devm_event_notifier_register(sdev,
							   SCMI_PROTOCOL_SYSTEM,
					 SCMI_EVENT_SYSTEM_POWER_STATE_NOTIFIER,
						       NULL, &sc->userspace_nb);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_SYSTEM, "syspower" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_system_power_driver = {
	.name = "scmi-system-power",
	.probe = scmi_syspower_probe,
	.id_table = scmi_id_table,
};
module_scmi_driver(scmi_system_power_driver);

MODULE_AUTHOR("Cristian Marussi <cristian.marussi@arm.com>");
MODULE_DESCRIPTION("ARM SCMI SystemPower Control driver");
MODULE_LICENSE("GPL");
