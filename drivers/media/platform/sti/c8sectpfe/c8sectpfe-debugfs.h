/**
 * c8sectpfe-debugfs.h - C8SECTPFE STi DVB driver debugfs header
 *
 * Copyright (c) STMicroelectronics 2015
 *
 * Authors: Peter Griffin <peter.griffin@linaro.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __C8SECTPFE_DEBUG_H
#define __C8SECTPFE_DEBUG_H

#include "c8sectpfe-core.h"

void c8sectpfe_debugfs_init(struct c8sectpfei *);
void c8sectpfe_debugfs_exit(struct c8sectpfei *);

#endif /* __C8SECTPFE_DEBUG_H */
