// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 *
 */

#include <linux/notifier.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>

static ATOMIC_NOTIFIER_HEAD(vin_notifier_list);

int vin_notifier_register(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&vin_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(vin_notifier_register);

void vin_notifier_unregister(struct notifier_block *nb)
{
	atomic_notifier_chain_unregister(&vin_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(vin_notifier_unregister);

int vin_notifier_call(unsigned long e, void *v)
{
	return atomic_notifier_call_chain(&vin_notifier_list, e, v);
}
EXPORT_SYMBOL_GPL(vin_notifier_call);

MODULE_AUTHOR("StarFive Technology Co., Ltd.");
MODULE_DESCRIPTION("Starfive VIC video in notifier");
MODULE_LICENSE("GPL");
//MODULE_SUPPORTED_DEVICE("video");
