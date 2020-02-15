// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/thermal.h>
#include "core.h"
#include "debug.h"

static int
ath11k_thermal_get_max_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long *state)
{
	*state = ATH11K_THERMAL_THROTTLE_MAX;

	return 0;
}

static int
ath11k_thermal_get_cur_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long *state)
{
	struct ath11k *ar = cdev->devdata;

	mutex_lock(&ar->conf_mutex);
	*state = ar->thermal.throttle_state;
	mutex_unlock(&ar->conf_mutex);

	return 0;
}

static int
ath11k_thermal_set_cur_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long throttle_state)
{
	struct ath11k *ar = cdev->devdata;
	int ret;

	if (throttle_state > ATH11K_THERMAL_THROTTLE_MAX) {
		ath11k_warn(ar->ab, "throttle state %ld is exceeding the limit %d\n",
			    throttle_state, ATH11K_THERMAL_THROTTLE_MAX);
		return -EINVAL;
	}
	mutex_lock(&ar->conf_mutex);
	ret = ath11k_thermal_set_throttling(ar, throttle_state);
	if (ret == 0)
		ar->thermal.throttle_state = throttle_state;
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static struct thermal_cooling_device_ops ath11k_thermal_ops = {
	.get_max_state = ath11k_thermal_get_max_throttle_state,
	.get_cur_state = ath11k_thermal_get_cur_throttle_state,
	.set_cur_state = ath11k_thermal_set_cur_throttle_state,
};

int ath11k_thermal_set_throttling(struct ath11k *ar, u32 throttle_state)
{
	struct ath11k_base *sc = ar->ab;
	struct thermal_mitigation_params param;
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_ON)
		return 0;

	memset(&param, 0, sizeof(param));
	param.pdev_id = ar->pdev->pdev_id;
	param.enable = throttle_state ? 1 : 0;
	param.dc = ATH11K_THERMAL_DEFAULT_DUTY_CYCLE;
	param.dc_per_event = 0xFFFFFFFF;

	param.levelconf[0].tmplwm = ATH11K_THERMAL_TEMP_LOW_MARK;
	param.levelconf[0].tmphwm = ATH11K_THERMAL_TEMP_HIGH_MARK;
	param.levelconf[0].dcoffpercent = throttle_state;
	param.levelconf[0].priority = 0; /* disable all data tx queues */

	ret = ath11k_wmi_send_thermal_mitigation_param_cmd(ar, &param);
	if (ret) {
		ath11k_warn(sc, "failed to send thermal mitigation duty cycle %u ret %d\n",
			    throttle_state, ret);
	}

	return ret;
}

int ath11k_thermal_register(struct ath11k_base *sc)
{
	struct thermal_cooling_device *cdev;
	struct ath11k *ar;
	struct ath11k_pdev *pdev;
	int i, ret;

	for (i = 0; i < sc->num_radios; i++) {
		pdev = &sc->pdevs[i];
		ar = pdev->ar;
		if (!ar)
			continue;

		cdev = thermal_cooling_device_register("ath11k_thermal", ar,
						       &ath11k_thermal_ops);

		if (IS_ERR(cdev)) {
			ath11k_err(sc, "failed to setup thermal device result: %ld\n",
				   PTR_ERR(cdev));
			return -EINVAL;
		}

		ret = sysfs_create_link(&ar->hw->wiphy->dev.kobj, &cdev->device.kobj,
					"cooling_device");
		if (ret) {
			ath11k_err(sc, "failed to create cooling device symlink\n");
			goto err_thermal_destroy;
		}

		ar->thermal.cdev = cdev;
	}

	return 0;

err_thermal_destroy:
	ath11k_thermal_unregister(sc);
	return ret;
}

void ath11k_thermal_unregister(struct ath11k_base *sc)
{
	struct ath11k *ar;
	struct ath11k_pdev *pdev;
	int i;

	for (i = 0; i < sc->num_radios; i++) {
		pdev = &sc->pdevs[i];
		ar = pdev->ar;
		if (!ar)
			continue;

		sysfs_remove_link(&ar->hw->wiphy->dev.kobj, "cooling_device");
		thermal_cooling_device_unregister(ar->thermal.cdev);
	}
}
