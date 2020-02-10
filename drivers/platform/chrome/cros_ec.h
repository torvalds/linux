/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ChromeOS Embedded Controller core interface.
 *
 * Copyright (C) 2020 Google LLC
 */

#ifndef __CROS_EC_H
#define __CROS_EC_H

int cros_ec_register(struct cros_ec_device *ec_dev);
int cros_ec_unregister(struct cros_ec_device *ec_dev);

int cros_ec_suspend(struct cros_ec_device *ec_dev);
int cros_ec_resume(struct cros_ec_device *ec_dev);

bool cros_ec_handle_event(struct cros_ec_device *ec_dev);

#endif /* __CROS_EC_H */
