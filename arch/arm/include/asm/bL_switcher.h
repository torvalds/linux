/*
 * arch/arm/include/asm/bL_switcher.h
 *
 * Created by:  Nicolas Pitre, April 2012
 * Copyright:   (C) 2012  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ASM_BL_SWITCHER_H
#define ASM_BL_SWITCHER_H

enum switch_event {
	SWITCH_ENTER,
	SWITCH_EXIT,
};

struct bL_power_ops;

int __init bL_switcher_init(const struct bL_power_ops *ops);
void bL_switch_request(unsigned int cpu, unsigned int new_cluster_id);
int bL_cluster_switch_request(unsigned int new_cluster);
int register_bL_swicher_notifier(struct notifier_block *nb);
int unregister_bL_swicher_notifier(struct notifier_block *nb);
bool bL_check_auto_switcher_enable(void);

#endif
