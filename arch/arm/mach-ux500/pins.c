/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL), version 2
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <plat/pincfg.h>

#include "pins.h"

static LIST_HEAD(pin_lookups);
static DEFINE_MUTEX(pin_lookups_mutex);
static DEFINE_SPINLOCK(pins_lock);

void __init ux500_pins_add(struct ux500_pin_lookup *pl, size_t num)
{
	mutex_lock(&pin_lookups_mutex);

	while (num--) {
		list_add_tail(&pl->node, &pin_lookups);
		pl++;
	}

	mutex_unlock(&pin_lookups_mutex);
}

struct ux500_pins *ux500_pins_get(const char *name)
{
	struct ux500_pins *pins = NULL;
	struct ux500_pin_lookup *pl;

	mutex_lock(&pin_lookups_mutex);

	list_for_each_entry(pl, &pin_lookups, node) {
		if (!strcmp(pl->name, name)) {
			pins = pl->pins;
			goto out;
		}
	}

out:
	mutex_unlock(&pin_lookups_mutex);
	return pins;
}

int ux500_pins_enable(struct ux500_pins *pins)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&pins_lock, flags);

	if (pins->usage++ == 0)
		ret = nmk_config_pins(pins->cfg, pins->num);

	spin_unlock_irqrestore(&pins_lock, flags);
	return ret;
}

int ux500_pins_disable(struct ux500_pins *pins)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&pins_lock, flags);

	if (WARN_ON(pins->usage == 0))
		goto out;

	if (--pins->usage == 0)
		ret = nmk_config_pins_sleep(pins->cfg, pins->num);

out:
	spin_unlock_irqrestore(&pins_lock, flags);
	return ret;
}

void ux500_pins_put(struct ux500_pins *pins)
{
	WARN_ON(!pins);
}
