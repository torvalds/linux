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

void qed_fw_reporters_create(struct devlink *devlink);
void qed_fw_reporters_destroy(struct devlink *devlink);

int qed_report_fatal_error(struct devlink *dl, enum qed_hw_err_type err_type);

#endif
