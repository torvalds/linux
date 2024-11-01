/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

#ifndef _QTN_FMAC_EVENT_H_
#define _QTN_FMAC_EVENT_H_

#include <linux/kernel.h>
#include <linux/module.h>

#include "qlink.h"

void qtnf_event_work_handler(struct work_struct *work);

#endif /* _QTN_FMAC_EVENT_H_ */
