/*
 * local dimming interface
 *
 * Author: Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2012 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/notifier.h>
#include <linux/module.h>

static BLOCKING_NOTIFIER_HEAD(ld_notifier_list);

/**
 * register a client notifier
 * @nb: notifier block to callback on events
 */
int ld_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&ld_notifier_list, nb);
}
EXPORT_SYMBOL(ld_register_notifier);

/**
 * unregister a client notifier
 * @nb: notifier block to callback on events
 */
int ld_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&ld_notifier_list, nb);
}
EXPORT_SYMBOL(ld_unregister_notifier);

/**
 * notify clients
 *
 */
int ld_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&ld_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(ld_notifier_call_chain);

