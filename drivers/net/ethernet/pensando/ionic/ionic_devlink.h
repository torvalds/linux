/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#ifndef _IONIC_DEVLINK_H_
#define _IONIC_DEVLINK_H_

#include <net/devlink.h>

struct ionic *ionic_devlink_alloc(struct device *dev);
void ionic_devlink_free(struct ionic *ionic);
int ionic_devlink_register(struct ionic *ionic);
void ionic_devlink_unregister(struct ionic *ionic);

#endif /* _IONIC_DEVLINK_H_ */
