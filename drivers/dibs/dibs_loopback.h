/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  dibs loopback (aka loopback-ism) device structure definitions.
 *
 *  Copyright (c) 2024, Alibaba Inc.
 *
 *  Author: Wen Gu <guwen@linux.alibaba.com>
 *          Tony Lu <tonylu@linux.alibaba.com>
 *
 */

#ifndef _DIBS_LOOPBACK_H
#define _DIBS_LOOPBACK_H

#include <linux/dibs.h>
#include <linux/types.h>
#include <linux/wait.h>

#if IS_ENABLED(CONFIG_DIBS_LO)

struct dibs_lo_dev {
	struct dibs_dev *dibs;
};

int dibs_loopback_init(void);
void dibs_loopback_exit(void);
#else
static inline int dibs_loopback_init(void)
{
	return 0;
}

static inline void dibs_loopback_exit(void)
{
}
#endif

#endif /* _DIBS_LOOPBACK_H */
