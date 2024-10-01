/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015-2018, Intel Corporation.
 */

#ifndef __KCS_BMC_H__
#define __KCS_BMC_H__

#include <linux/list.h>

#define KCS_BMC_EVENT_TYPE_OBE	BIT(0)
#define KCS_BMC_EVENT_TYPE_IBF	BIT(1)

#define KCS_BMC_STR_OBF		BIT(0)
#define KCS_BMC_STR_IBF		BIT(1)
#define KCS_BMC_STR_CMD_DAT	BIT(3)

/* IPMI 2.0 - 9.5, KCS Interface Registers
 * @idr: Input Data Register
 * @odr: Output Data Register
 * @str: Status Register
 */
struct kcs_ioreg {
	u32 idr;
	u32 odr;
	u32 str;
};

struct kcs_bmc_device_ops;
struct kcs_bmc_client;

struct kcs_bmc_device {
	struct list_head entry;

	struct device *dev;
	u32 channel;

	struct kcs_ioreg ioreg;

	const struct kcs_bmc_device_ops *ops;

	spinlock_t lock;
	struct kcs_bmc_client *client;
};

#endif /* __KCS_BMC_H__ */
