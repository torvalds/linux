// SPDX-License-Identifier: GPL-2.0-only
#include "edac_module.h"

static struct workqueue_struct *wq;

bool edac_queue_work(struct delayed_work *work, unsigned long delay)
{
	return queue_delayed_work(wq, work, delay);
}
EXPORT_SYMBOL_GPL(edac_queue_work);

bool edac_mod_work(struct delayed_work *work, unsigned long delay)
{
	return mod_delayed_work(wq, work, delay);
}
EXPORT_SYMBOL_GPL(edac_mod_work);

bool edac_stop_work(struct delayed_work *work)
{
	bool ret;

	ret = cancel_delayed_work_sync(work);
	flush_workqueue(wq);

	return ret;
}
EXPORT_SYMBOL_GPL(edac_stop_work);

int edac_workqueue_setup(void)
{
	wq = alloc_ordered_workqueue("edac-poller", WQ_MEM_RECLAIM);
	if (!wq)
		return -ENODEV;
	else
		return 0;
}

void edac_workqueue_teardown(void)
{
	flush_workqueue(wq);
	destroy_workqueue(wq);
	wq = NULL;
}
