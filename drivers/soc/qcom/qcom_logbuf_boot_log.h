/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_LOGBUF_BOOT_LOG_H
#define __QCOM_LOGBUF_BOOT_LOG_H

#include "../../../kernel/printk/printk_ringbuffer.h"

void register_log_minidump(struct printk_ringbuffer *prb);

#if IS_ENABLED(CONFIG_QCOM_LOGBUF_BOOTLOG)
int boot_log_register(struct device *dev);
void boot_log_unregister(void);
#else
static inline int boot_log_register(struct device *dev)
{
	return 0;
}
static inline void boot_log_unregister(void)
{
}

#endif
#endif
