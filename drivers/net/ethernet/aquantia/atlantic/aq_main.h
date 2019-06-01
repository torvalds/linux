/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 */

/* File aq_main.h: Main file for aQuantia Linux driver. */

#ifndef AQ_MAIN_H
#define AQ_MAIN_H

#include "aq_common.h"
#include "aq_nic.h"

void aq_ndev_schedule_work(struct work_struct *work);
struct net_device *aq_ndev_alloc(void);

#endif /* AQ_MAIN_H */
