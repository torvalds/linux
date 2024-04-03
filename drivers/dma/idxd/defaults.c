// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Intel Corporation. All rights rsvd. */
#include <linux/kernel.h>
#include "idxd.h"

int idxd_load_iaa_device_defaults(struct idxd_device *idxd)
{
	struct idxd_engine *engine;
	struct idxd_group *group;
	struct idxd_wq *wq;

	if (!test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		return 0;

	wq = idxd->wqs[0];

	if (wq->state != IDXD_WQ_DISABLED)
		return -EPERM;

	/* set mode to "dedicated" */
	set_bit(WQ_FLAG_DEDICATED, &wq->flags);
	wq->threshold = 0;

	/* only setting up 1 wq, so give it all the wq space */
	wq->size = idxd->max_wq_size;

	/* set priority to 10 */
	wq->priority = 10;

	/* set type to "kernel" */
	wq->type = IDXD_WQT_KERNEL;

	/* set wq group to 0 */
	group = idxd->groups[0];
	wq->group = group;
	group->num_wqs++;

	/* set name to "iaa_crypto" */
	memset(wq->name, 0, WQ_NAME_SIZE + 1);
	strscpy(wq->name, "iaa_crypto", WQ_NAME_SIZE + 1);

	/* set driver_name to "crypto" */
	memset(wq->driver_name, 0, DRIVER_NAME_SIZE + 1);
	strscpy(wq->driver_name, "crypto", DRIVER_NAME_SIZE + 1);

	engine = idxd->engines[0];

	/* set engine group to 0 */
	engine->group = idxd->groups[0];
	engine->group->num_engines++;

	return 0;
}
