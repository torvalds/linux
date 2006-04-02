/*
 * LED Class Core
 *
 * Copyright 2005-2006 Openedhand Ltd.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/leds.h>
#include "leds.h"

rwlock_t leds_list_lock = RW_LOCK_UNLOCKED;
LIST_HEAD(leds_list);

EXPORT_SYMBOL_GPL(leds_list);
EXPORT_SYMBOL_GPL(leds_list_lock);
