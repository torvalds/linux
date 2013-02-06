/* Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "subsys-restart: %s(): " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/time.h>
#include <linux/wakelock.h>
#include <linux/suspend.h>
#include <mach/subsystem_restart.h>
#include <mach/sec_debug.h>

struct subsys_soc_restart_order {
	const char * const *subsystem_list;
	int count;

	struct mutex shutdown_lock;
	struct mutex powerup_lock;
	struct subsys_data *subsys_ptrs[];
};

struct restart_wq_data {
	struct subsys_data *subsys;
	struct wake_lock ssr_wake_lock;
	char wakelockname[64];
	int coupled;
	struct work_struct work;
};

struct restart_log {
	struct timeval time;
	struct subsys_data *subsys;
	struct list_head list;
};

static int restart_level;
static int mdm_dump = 1;
struct workqueue_struct *ssr_wq;

static LIST_HEAD(restart_log_list);
static LIST_HEAD(subsystem_list);
static DEFINE_MUTEX(subsystem_list_lock);
static DEFINE_MUTEX(soc_order_reg_lock);
static DEFINE_MUTEX(restart_log_mutex);

/* These will be assigned to one of the sets above after
 * runtime SoC identification.
 */
module_param(mdm_dump, int, S_IRUGO | S_IWUSR);

static struct subsys_data *_find_subsystem(const char *subsys_name)
{
	struct subsys_data *subsys;

	mutex_lock(&subsystem_list_lock);
	list_for_each_entry(subsys, &subsystem_list, list)
		if (!strncmp(subsys->name, subsys_name,
				SUBSYS_NAME_MAX_LENGTH)) {
			mutex_unlock(&subsystem_list_lock);
			return subsys;
		}
	mutex_unlock(&subsystem_list_lock);

	return NULL;
}

static int max_restarts;
module_param(max_restarts, int, 0644);

static long max_history_time = 3600;
module_param(max_history_time, long, 0644);

static void subsystem_restart_wq_func(struct work_struct *work)
{
	struct restart_wq_data *r_work = container_of(work,
						struct restart_wq_data, work);
	struct subsys_data **restart_list;
	struct subsys_data *subsys = r_work->subsys;

	struct mutex *powerup_lock;
	struct mutex *shutdown_lock;

	int i;
	int restart_list_count = 0;

	/* It's OK to not take the registration lock at this point.
	 * This is because the subsystem list inside the relevant
	 * restart order is not being traversed.
	 */
	restart_list = subsys->single_restart_list;
	restart_list_count = 1;
	powerup_lock = &subsys->powerup_lock;
	shutdown_lock = &subsys->shutdown_lock;
	pr_info("subsys[%p], powerup lock[%p], shutdown_lock[%p]\n", subsys,
						powerup_lock, shutdown_lock);

	pr_info("[%p]: Attempting to get shutdown lock!\n", current);

	/* Try to acquire shutdown_lock. If this fails, these subsystems are
	 * already being restarted - return.
	 */
	if (!mutex_trylock(shutdown_lock))
		goto out;

	pr_info("[%p]: Attempting to get powerup lock!\n", current);

	/* Now that we've acquired the shutdown lock, either we're the first to
	 * restart these subsystems or some other thread is doing the powerup
	 * sequence for these subsystems. In the latter case, panic and bail
	 * out, since a subsystem died in its powerup sequence.
	 */
	if (!mutex_trylock(powerup_lock))
		panic("%s[%p]: Subsystem died during powerup!",
						__func__, current);

	/* Now it is necessary to take the registration lock. This is because
	 * the subsystem list in the SoC restart order will be traversed
	 * and it shouldn't be changed until _this_ restart sequence completes.
	 */
	mutex_lock(&soc_order_reg_lock);

	pr_info("[%p]: Starting restart sequence for %s\n", current,
			r_work->subsys->name);

	for (i = 0; i < restart_list_count; i++) {

		if (!restart_list[i])
			continue;

		pr_info("[%p]: Shutting down %s\n", current,
			restart_list[i]->name);

		if (restart_list[i]->shutdown(subsys) < 0)
			panic("subsys-restart: %s[%p]: Failed to shutdown %s!",
				__func__, current, restart_list[i]->name);
	}

	/* Now that we've finished shutting down these subsystems, release the
	 * shutdown lock. If a subsystem restart request comes in for a
	 * subsystem in _this_ restart order after the unlock below, and
	 * before the powerup lock is released, panic and bail out.
	 */
	mutex_unlock(shutdown_lock);

	/* Collect ram dumps for all subsystems in order here */
	for (i = 0; i < restart_list_count; i++) {
		if (!restart_list[i])
			continue;

		if (restart_list[i]->ramdump)
			if (restart_list[i]->ramdump(mdm_dump, subsys) < 0)
				pr_warn("%s[%p]: Ramdump failed.\n",
						restart_list[i]->name, current);
	}

	for (i = restart_list_count - 1; i >= 0; i--) {

		if (!restart_list[i])
			continue;

		pr_info("[%p]: Powering up %s\n", current,
					restart_list[i]->name);

		if (restart_list[i]->powerup(subsys) < 0)
			panic("%s[%p]: Failed to powerup %s!", __func__,
				current, restart_list[i]->name);
	}

	pr_info("[%p]: Restart sequence for %s completed.\n",
			current, r_work->subsys->name);

	mutex_unlock(powerup_lock);

	mutex_unlock(&soc_order_reg_lock);

	pr_info("[%p]: Released powerup lock!\n", current);

out:
	wake_unlock(&r_work->ssr_wake_lock);
	wake_lock_destroy(&r_work->ssr_wake_lock);
	kfree(r_work);
	subsys->ongoing = false;
}

int subsystem_restart(const char *subsys_name)
{
	struct subsys_data *subsys;
	struct restart_wq_data *data = NULL;
	int rc;

	if (!subsys_name) {
		pr_err("Invalid subsystem name.\n");
		return -EINVAL;
	}

	pr_info("Restart sequence requested for %s, restart_level = %d.\n",
		subsys_name, restart_level);

	/* List of subsystems is protected by a lock. New subsystems can
	 * still come in.
	 */
	subsys = _find_subsystem(subsys_name);

	if (!subsys) {
		pr_warn("Unregistered subsystem %s!\n", subsys_name);
		return -EINVAL;
	}

	if (subsys->ongoing) {
		pr_warn("subsys-restart: already requested, return busy\n");
		return -EBUSY;
	}

	subsys->ongoing = true;

	/* check debug level */
	if (!sec_debug_level.uint_val) {
		/* debug level is low, set mdm_dump to Zero */
		mdm_dump = 0;
	}

	data = kzalloc(sizeof(struct restart_wq_data), GFP_KERNEL);
	if (!data) {
		pr_warn("Failed to alloc restart data. Resetting.\n");
		panic("subsys-restart: Resetting the SoC - %s crashed.",
								subsys->name);
	} else {
		data->coupled = 0;
		data->subsys = subsys;
	}
	pr_debug("Restarting %s [level=%d]!\n", subsys_name, restart_level);

	snprintf(data->wakelockname, sizeof(data->wakelockname),
						"ssr(%s)", subsys_name);
	wake_lock_init(&data->ssr_wake_lock, WAKE_LOCK_SUSPEND,
							data->wakelockname);
	wake_lock(&data->ssr_wake_lock);

	INIT_WORK(&data->work, subsystem_restart_wq_func);
	rc = schedule_work(&data->work);
	if (rc < 0)
		panic("%s: Unable to schedule work to restart %s",
						     __func__, subsys->name);
	return 0;
}
EXPORT_SYMBOL(subsystem_restart);

int ssr_register_subsystem(struct subsys_data *subsys)
{
	pr_info("%s\n", __func__);
	if (!subsys)
		goto err;

	if (!subsys->name)
		goto err;

	if (!subsys->powerup || !subsys->shutdown)
		goto err;

	subsys->single_restart_list[0] = subsys;

	mutex_init(&subsys->shutdown_lock);
	mutex_init(&subsys->powerup_lock);

	mutex_lock(&subsystem_list_lock);
	list_add(&subsys->list, &subsystem_list);
	mutex_unlock(&subsystem_list_lock);

	return 0;

err:
	return -EINVAL;
}
EXPORT_SYMBOL(ssr_register_subsystem);

static int ssr_panic_handler(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct subsys_data *subsys;

	list_for_each_entry(subsys, &subsystem_list, list)
		if (subsys->crash_shutdown)
			subsys->crash_shutdown(subsys);
	return NOTIFY_DONE;
}

static struct notifier_block panic_nb = {
	.notifier_call  = ssr_panic_handler,
};

static int __init subsys_restart_init(void)
{
	int ret = 0;

	pr_info("%s\n", __func__);

	restart_level = RESET_SOC;

	ssr_wq = alloc_workqueue("ssr_wq", 0, 0);

	if (!ssr_wq)
		panic("Couldn't allocate workqueue for subsystem restart.\n");

	atomic_notifier_chain_register(&panic_notifier_list,
			&panic_nb);

	return ret;
}

arch_initcall(subsys_restart_init);

MODULE_DESCRIPTION("Subsystem Restart Driver");
MODULE_LICENSE("GPL v2");
