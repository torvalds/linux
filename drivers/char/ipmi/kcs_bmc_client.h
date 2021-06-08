/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021, IBM Corp. */

#ifndef __KCS_BMC_CONSUMER_H__
#define __KCS_BMC_CONSUMER_H__

#include <linux/irqreturn.h>

struct kcs_bmc;
struct kcs_bmc_client_ops;

struct kcs_bmc_client {
	const struct kcs_bmc_client_ops *ops;

	struct kcs_bmc *dev;
};

struct kcs_bmc_client_ops {
	irqreturn_t (*event)(struct kcs_bmc_client *client);
};

u8 kcs_bmc_read_data(struct kcs_bmc *kcs_bmc);
void kcs_bmc_write_data(struct kcs_bmc *kcs_bmc, u8 data);
u8 kcs_bmc_read_status(struct kcs_bmc *kcs_bmc);
void kcs_bmc_write_status(struct kcs_bmc *kcs_bmc, u8 data);
void kcs_bmc_update_status(struct kcs_bmc *kcs_bmc, u8 mask, u8 val);
#endif
