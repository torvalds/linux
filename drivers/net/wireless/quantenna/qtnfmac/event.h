/*
 * Copyright (c) 2015-2016 Quantenna Communications, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _QTN_FMAC_EVENT_H_
#define _QTN_FMAC_EVENT_H_

#include <linux/kernel.h>
#include <linux/module.h>

#include "qlink.h"

void qtnf_event_work_handler(struct work_struct *work);

#endif /* _QTN_FMAC_EVENT_H_ */
