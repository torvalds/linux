/*
 * Sonics Silicon Backplane
 * Embedded systems support code
 *
 * Copyright 2005-2008, Broadcom Corporation
 * Copyright 2006-2008, Michael Buesch <mb@bu3sch.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/ssb/ssb.h>
#include <linux/ssb/ssb_embedded.h>

#include "ssb_private.h"


int ssb_watchdog_timer_set(struct ssb_bus *bus, u32 ticks)
{
	if (ssb_chipco_available(&bus->chipco)) {
		ssb_chipco_watchdog_timer_set(&bus->chipco, ticks);
		return 0;
	}
	if (ssb_extif_available(&bus->extif)) {
		ssb_extif_watchdog_timer_set(&bus->extif, ticks);
		return 0;
	}
	return -ENODEV;
}

u32 ssb_gpio_in(struct ssb_bus *bus, u32 mask)
{
	unsigned long flags;
	u32 res = 0;

	spin_lock_irqsave(&bus->gpio_lock, flags);
	if (ssb_chipco_available(&bus->chipco))
		res = ssb_chipco_gpio_in(&bus->chipco, mask);
	else if (ssb_extif_available(&bus->extif))
		res = ssb_extif_gpio_in(&bus->extif, mask);
	else
		SSB_WARN_ON(1);
	spin_unlock_irqrestore(&bus->gpio_lock, flags);

	return res;
}
EXPORT_SYMBOL(ssb_gpio_in);

u32 ssb_gpio_out(struct ssb_bus *bus, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res = 0;

	spin_lock_irqsave(&bus->gpio_lock, flags);
	if (ssb_chipco_available(&bus->chipco))
		res = ssb_chipco_gpio_out(&bus->chipco, mask, value);
	else if (ssb_extif_available(&bus->extif))
		res = ssb_extif_gpio_out(&bus->extif, mask, value);
	else
		SSB_WARN_ON(1);
	spin_unlock_irqrestore(&bus->gpio_lock, flags);

	return res;
}
EXPORT_SYMBOL(ssb_gpio_out);

u32 ssb_gpio_outen(struct ssb_bus *bus, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res = 0;

	spin_lock_irqsave(&bus->gpio_lock, flags);
	if (ssb_chipco_available(&bus->chipco))
		res = ssb_chipco_gpio_outen(&bus->chipco, mask, value);
	else if (ssb_extif_available(&bus->extif))
		res = ssb_extif_gpio_outen(&bus->extif, mask, value);
	else
		SSB_WARN_ON(1);
	spin_unlock_irqrestore(&bus->gpio_lock, flags);

	return res;
}
EXPORT_SYMBOL(ssb_gpio_outen);

u32 ssb_gpio_control(struct ssb_bus *bus, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res = 0;

	spin_lock_irqsave(&bus->gpio_lock, flags);
	if (ssb_chipco_available(&bus->chipco))
		res = ssb_chipco_gpio_control(&bus->chipco, mask, value);
	spin_unlock_irqrestore(&bus->gpio_lock, flags);

	return res;
}
EXPORT_SYMBOL(ssb_gpio_control);

u32 ssb_gpio_intmask(struct ssb_bus *bus, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res = 0;

	spin_lock_irqsave(&bus->gpio_lock, flags);
	if (ssb_chipco_available(&bus->chipco))
		res = ssb_chipco_gpio_intmask(&bus->chipco, mask, value);
	else if (ssb_extif_available(&bus->extif))
		res = ssb_extif_gpio_intmask(&bus->extif, mask, value);
	else
		SSB_WARN_ON(1);
	spin_unlock_irqrestore(&bus->gpio_lock, flags);

	return res;
}
EXPORT_SYMBOL(ssb_gpio_intmask);

u32 ssb_gpio_polarity(struct ssb_bus *bus, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res = 0;

	spin_lock_irqsave(&bus->gpio_lock, flags);
	if (ssb_chipco_available(&bus->chipco))
		res = ssb_chipco_gpio_polarity(&bus->chipco, mask, value);
	else if (ssb_extif_available(&bus->extif))
		res = ssb_extif_gpio_polarity(&bus->extif, mask, value);
	else
		SSB_WARN_ON(1);
	spin_unlock_irqrestore(&bus->gpio_lock, flags);

	return res;
}
EXPORT_SYMBOL(ssb_gpio_polarity);
