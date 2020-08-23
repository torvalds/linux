/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Marvell/Qlogic FastLinQ NIC driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 */
#ifndef _QED_DEVLINK_H
#define _QED_DEVLINK_H

#include <linux/qed/qed_if.h>
#include <net/devlink.h>

struct devlink *qed_devlink_register(struct qed_dev *cdev);
void qed_devlink_unregister(struct devlink *devlink);

#endif
