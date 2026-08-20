// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2026 Morse Micro
 */
#include <linux/module.h>
#include "core.h"
#include "bus.h"
#include "hif.h"
#include "mac.h"

static int mm81x_core_attach_regs(struct mm81x *mors)
{
	int ret = 0;

	mm81x_claim_bus(mors);
	ret = mm81x_reg32_read(mors, MM8108_REG_CHIP_ID, &mors->chip_id);
	mm81x_release_bus(mors);

	if (ret < 0) {
		dev_err(mors->dev, "failed to read chip id %d", ret);
		return ret;
	}

	switch (mors->chip_id) {
	case (CHIP_ID_MM8108):
		mors->regs = &mm8108_regs;
		mors->hif.ops = &mm81x_yaps_ops;
		break;
	default:
		return -ENODEV;
	}

	return ret;
}

static void mm81x_core_init_mac_addr(struct mm81x *mors)
{
	int ret = mm81x_hw_otp_get_mac_addr(mors);

	if (ret || !is_valid_ether_addr(mors->macaddr))
		eth_random_addr(mors->macaddr);
}

char *mm81x_core_get_fw_path(u32 chip_id, u32 fw_ver)
{
	const char *fw_base;

	switch (chip_id) {
	case CHIP_ID_MM8108:
		fw_base = MM8108_FW_BASE;
		break;
	default:
		return NULL;
	}

	return kasprintf(GFP_KERNEL, MM81X_FW_DIR "/v%u/%s" MM81X_FW_EXT,
			 fw_ver, fw_base);
}
EXPORT_SYMBOL_GPL(mm81x_core_get_fw_path);

struct mm81x *mm81x_core_alloc(size_t priv_size, struct device *dev)
{
	return mm81x_mac_alloc(priv_size, dev);
}
EXPORT_SYMBOL_GPL(mm81x_core_alloc);

int mm81x_core_init(struct mm81x *mors)
{
	int ret;

	set_bit(MM81X_STATE_CHIP_UNRESPONSIVE, &mors->state_flags);
	set_bit(MM81X_STATE_RELOAD_FW_AFTER_START, &mors->state_flags);

	mm81x_core_init_mac_addr(mors);

	ret = mm81x_core_attach_regs(mors);
	if (ret)
		return ret;

	mors->chip_wq = create_singlethread_workqueue("chip_wq");
	if (!mors->chip_wq)
		return -ENOMEM;

	mors->net_wq = create_singlethread_workqueue("net_wq");
	if (!mors->net_wq) {
		ret = -ENOMEM;
		goto err_chip_wq;
	}

	ret = mm81x_hif_init(mors);
	if (ret)
		goto err_wqs;

	return 0;

err_wqs:
	flush_workqueue(mors->net_wq);
	destroy_workqueue(mors->net_wq);

err_chip_wq:
	flush_workqueue(mors->chip_wq);
	destroy_workqueue(mors->chip_wq);

	return ret;
}
EXPORT_SYMBOL_GPL(mm81x_core_init);

int mm81x_core_register(struct mm81x *mors)
{
	return mm81x_mac_register(mors);
}
EXPORT_SYMBOL_GPL(mm81x_core_register);

void mm81x_core_unregister(struct mm81x *mors)
{
	mm81x_mac_unregister(mors);
}
EXPORT_SYMBOL_GPL(mm81x_core_unregister);

void mm81x_core_deinit(struct mm81x *mors)
{
	mm81x_hif_finish(mors);
	flush_workqueue(mors->net_wq);
	destroy_workqueue(mors->net_wq);
	flush_workqueue(mors->chip_wq);
	destroy_workqueue(mors->chip_wq);
}
EXPORT_SYMBOL_GPL(mm81x_core_deinit);

void mm81x_core_free(struct mm81x *mors)
{
	mm81x_mac_free(mors);
}
EXPORT_SYMBOL_GPL(mm81x_core_free);

MODULE_AUTHOR("Morse Micro");
MODULE_DESCRIPTION("Driver support for Morse Micro MM81X core");
MODULE_LICENSE("Dual BSD/GPL");
