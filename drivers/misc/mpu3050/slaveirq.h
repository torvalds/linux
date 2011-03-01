/*
 $License:
    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.
 $
 */

#ifndef __SLAVEIRQ__
#define __SLAVEIRQ__

#ifdef __KERNEL__
#include <linux/i2c-dev.h>
#endif

#include "mpu.h"
#include "mpuirq.h"

#define SLAVEIRQ_ENABLE_DEBUG          (1)
#define SLAVEIRQ_GET_INTERRUPT_CNT     (2)
#define SLAVEIRQ_GET_IRQ_TIME          (3)
#define SLAVEIRQ_GET_LED_VALUE         (4)
#define SLAVEIRQ_SET_TIMEOUT           (5)
#define SLAVEIRQ_SET_SLAVE_INFO        (6)

#ifdef __KERNEL__

void slaveirq_exit(struct ext_slave_platform_data *pdata);
int slaveirq_init(struct i2c_adapter *slave_adapter,
		struct ext_slave_platform_data *pdata,
		char *name);

#endif

#endif
