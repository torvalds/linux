/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QAIC_TIMESYNC_H__
#define __QAIC_TIMESYNC_H__

#include <linux/mhi.h>

int qaic_timesync_init(void);
void qaic_timesync_deinit(void);
void qaic_mqts_ch_stop_timer(struct mhi_device *mhi_dev);
#endif /* __QAIC_TIMESYNC_H__ */
