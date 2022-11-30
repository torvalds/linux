// SPDX-License-Identifier: GPL-2.0

/* Driver for ETAS GmbH ES58X USB CAN(-FD) Bus Interfaces.
 *
 * File es58x_devlink.c: report the product information using devlink.
 *
 * Copyright (c) 2022 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#include <net/devlink.h>

const struct devlink_ops es58x_dl_ops = {
};
