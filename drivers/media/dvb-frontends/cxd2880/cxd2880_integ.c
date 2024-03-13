// SPDX-License-Identifier: GPL-2.0
/*
 * cxd2880_integ.c
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * integration layer common functions
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#include <linux/ktime.h>
#include <linux/errno.h>

#include "cxd2880_tnrdmd.h"
#include "cxd2880_tnrdmd_mon.h"
#include "cxd2880_integ.h"

int cxd2880_integ_init(struct cxd2880_tnrdmd *tnr_dmd)
{
	int ret;
	ktime_t start;
	u8 cpu_task_completed = 0;

	if (!tnr_dmd)
		return -EINVAL;

	ret = cxd2880_tnrdmd_init1(tnr_dmd);
	if (ret)
		return ret;

	start = ktime_get();

	while (1) {
		ret =
		    cxd2880_tnrdmd_check_internal_cpu_status(tnr_dmd,
						     &cpu_task_completed);
		if (ret)
			return ret;

		if (cpu_task_completed)
			break;

		if (ktime_to_ms(ktime_sub(ktime_get(), start)) >
					CXD2880_TNRDMD_WAIT_INIT_TIMEOUT)
			return -ETIMEDOUT;

		usleep_range(CXD2880_TNRDMD_WAIT_INIT_INTVL,
			     CXD2880_TNRDMD_WAIT_INIT_INTVL + 1000);
	}

	return cxd2880_tnrdmd_init2(tnr_dmd);
}

int cxd2880_integ_cancel(struct cxd2880_tnrdmd *tnr_dmd)
{
	if (!tnr_dmd)
		return -EINVAL;

	atomic_set(&tnr_dmd->cancel, 1);

	return 0;
}

int cxd2880_integ_check_cancellation(struct cxd2880_tnrdmd *tnr_dmd)
{
	if (!tnr_dmd)
		return -EINVAL;

	if (atomic_read(&tnr_dmd->cancel) != 0)
		return -ECANCELED;

	return 0;
}
