/*
 * Copyright 2015 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _DRV_CROS_EC_DEBUGFS_H_
#define _DRV_CROS_EC_DEBUGFS_H_

#include "cros_ec_dev.h"

/* debugfs stuff */
int cros_ec_debugfs_init(struct cros_ec_dev *ec);
void cros_ec_debugfs_remove(struct cros_ec_dev *ec);

#endif  /* _DRV_CROS_EC_DEBUGFS_H_ */
