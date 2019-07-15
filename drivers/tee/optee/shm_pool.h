/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015, Linaro Limited
 * Copyright (c) 2016, EPAM Systems
 */

#ifndef SHM_POOL_H
#define SHM_POOL_H

#include <linux/tee_drv.h>

struct tee_shm_pool_mgr *optee_shm_pool_alloc_pages(void);

#endif
