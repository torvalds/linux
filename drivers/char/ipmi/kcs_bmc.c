// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015-2018, Intel Corporation.
 */

#include <linux/module.h>

#include "kcs_bmc.h"

u8 kcs_bmc_read_data(struct kcs_bmc *kcs_bmc)
{
	return kcs_bmc->io_inputb(kcs_bmc, kcs_bmc->ioreg.idr);
}
EXPORT_SYMBOL(kcs_bmc_read_data);

void kcs_bmc_write_data(struct kcs_bmc *kcs_bmc, u8 data)
{
	kcs_bmc->io_outputb(kcs_bmc, kcs_bmc->ioreg.odr, data);
}
EXPORT_SYMBOL(kcs_bmc_write_data);

u8 kcs_bmc_read_status(struct kcs_bmc *kcs_bmc)
{
	return kcs_bmc->io_inputb(kcs_bmc, kcs_bmc->ioreg.str);
}
EXPORT_SYMBOL(kcs_bmc_read_status);

void kcs_bmc_write_status(struct kcs_bmc *kcs_bmc, u8 data)
{
	kcs_bmc->io_outputb(kcs_bmc, kcs_bmc->ioreg.str, data);
}
EXPORT_SYMBOL(kcs_bmc_write_status);

void kcs_bmc_update_status(struct kcs_bmc *kcs_bmc, u8 mask, u8 val)
{
	kcs_bmc->io_updateb(kcs_bmc, kcs_bmc->ioreg.str, mask, val);
}
EXPORT_SYMBOL(kcs_bmc_update_status);

int kcs_bmc_ipmi_event(struct kcs_bmc *kcs_bmc);
int kcs_bmc_handle_event(struct kcs_bmc *kcs_bmc)
{
	return kcs_bmc_ipmi_event(kcs_bmc);
}
EXPORT_SYMBOL(kcs_bmc_handle_event);

struct kcs_bmc *kcs_bmc_ipmi_alloc(struct device *dev, int sizeof_priv, u32 channel);
struct kcs_bmc *kcs_bmc_alloc(struct device *dev, int sizeof_priv, u32 channel)
{
	return kcs_bmc_ipmi_alloc(dev, sizeof_priv, channel);
}
EXPORT_SYMBOL(kcs_bmc_alloc);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Haiyue Wang <haiyue.wang@linux.intel.com>");
MODULE_DESCRIPTION("KCS BMC to handle the IPMI request from system software");
