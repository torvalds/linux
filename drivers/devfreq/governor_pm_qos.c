/*
 * Copyright (C) 2012 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/devfreq.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include "governor.h"

struct devfreq_pm_qos_notifier_block {
	struct list_head node;
	struct notifier_block nb;
	struct devfreq *df;
};

static LIST_HEAD(devfreq_pm_qos_list);
static DEFINE_MUTEX(devfreq_pm_qos_mutex);

static int devfreq_pm_qos_func(struct devfreq *df, unsigned long *freq)
{
	struct devfreq_pm_qos_data *data = df->data;
	int megabytes_per_sec;
	unsigned long kbytes_per_sec;

	megabytes_per_sec = pm_qos_request(data->pm_qos_class);
	if (megabytes_per_sec > 0) {
		kbytes_per_sec = megabytes_per_sec * 1000;
		*freq = kbytes_per_sec / data->bytes_per_sec_per_hz;
	} else {
		*freq = 0;
	}

	return 0;
}

static int devfreq_pm_qos_notifier(struct notifier_block *nb, unsigned long val,
		void *v)
{
	struct devfreq_pm_qos_notifier_block *pq_nb;

	pq_nb = container_of(nb, struct devfreq_pm_qos_notifier_block, nb);
	mutex_lock(&pq_nb->df->lock);
	update_devfreq(pq_nb->df);
	mutex_unlock(&pq_nb->df->lock);

	return NOTIFY_OK;
}

static int devfreq_pm_qos_init(struct devfreq *df)
{
	int ret;
	struct devfreq_pm_qos_notifier_block *pq_nb;
	struct devfreq_pm_qos_data *data = df->data;

	if (!data)
		return -EINVAL;

	if (!data->bytes_per_sec_per_hz)
		return -EINVAL;

	pq_nb = kzalloc(sizeof(*pq_nb), GFP_KERNEL);
	if (!pq_nb)
		return -ENOMEM;

	pq_nb->df = df;
	pq_nb->nb.notifier_call = devfreq_pm_qos_notifier;
	INIT_LIST_HEAD(&pq_nb->node);

	ret = pm_qos_add_notifier(data->pm_qos_class, &pq_nb->nb);
	if (ret < 0)
		goto err;

	mutex_lock(&devfreq_pm_qos_mutex);
	list_add_tail(&pq_nb->node, &devfreq_pm_qos_list);
	mutex_unlock(&devfreq_pm_qos_mutex);

	return 0;

err:
	kfree(pq_nb);

	return ret;
}

static void devfreq_pm_qos_exit(struct devfreq *df)
{
	struct devfreq_pm_qos_notifier_block *pq_nb;
	struct devfreq_pm_qos_data *data;

	mutex_lock(&devfreq_pm_qos_mutex);

	list_for_each_entry(pq_nb, &devfreq_pm_qos_list, node) {
		if (pq_nb->df == df) {
			data = pq_nb->df->data;
			pm_qos_remove_notifier(data->pm_qos_class, &pq_nb->nb);
			goto out;
		}
	}

out:
	mutex_unlock(&devfreq_pm_qos_mutex);
}

const struct devfreq_governor devfreq_pm_qos = {
	.name = "pm_qos",
	.get_target_freq = devfreq_pm_qos_func,
	.init = devfreq_pm_qos_init,
	.exit = devfreq_pm_qos_exit,
	.no_central_polling = true,
};
