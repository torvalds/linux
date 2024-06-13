/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  cobalt interrupt handling
 *
 *  Copyright 2012-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

#include <linux/interrupt.h>

irqreturn_t cobalt_irq_handler(int irq, void *dev_id);
void cobalt_irq_work_handler(struct work_struct *work);
void cobalt_irq_log_status(struct cobalt *cobalt);
