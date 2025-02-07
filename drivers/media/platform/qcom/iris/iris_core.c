// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_firmware.h"
#include "iris_state.h"

void iris_core_deinit(struct iris_core *core)
{
	mutex_lock(&core->lock);
	iris_fw_unload(core);
	iris_hfi_queues_deinit(core);
	core->state = IRIS_CORE_DEINIT;
	mutex_unlock(&core->lock);
}

int iris_core_init(struct iris_core *core)
{
	int ret;

	mutex_lock(&core->lock);
	if (core->state == IRIS_CORE_INIT) {
		ret = 0;
		goto exit;
	} else if (core->state == IRIS_CORE_ERROR) {
		ret = -EINVAL;
		goto error;
	}

	core->state = IRIS_CORE_INIT;

	ret = iris_hfi_queues_init(core);
	if (ret)
		goto error;

	ret = iris_fw_load(core);
	if (ret)
		goto error_queue_deinit;

	mutex_unlock(&core->lock);

	return 0;

error_queue_deinit:
	iris_hfi_queues_deinit(core);
error:
	core->state = IRIS_CORE_DEINIT;
exit:
	mutex_unlock(&core->lock);

	return ret;
}
