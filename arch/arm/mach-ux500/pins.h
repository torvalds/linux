/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL), version 2
 */

#ifndef __MACH_UX500_PINS_H
#define __MACH_UX500_PINS_H

#include <linux/list.h>
#include <plat/pincfg.h>

#define PIN_LOOKUP(_name, _pins)	\
{					\
	.name	= _name,		\
	.pins	= _pins,		\
}

#define UX500_PINS(name, pins...)			\
struct ux500_pins name = {				\
	.cfg = (pin_cfg_t[]) {pins},			\
	.num = ARRAY_SIZE(((pin_cfg_t[]) {pins})),	\
}

struct ux500_pins {
	int usage;
	int num;
	pin_cfg_t *cfg;
};

struct ux500_pin_lookup {
	struct list_head	node;
	const char		*name;
	struct ux500_pins	*pins;
};

void __init ux500_pins_add(struct ux500_pin_lookup *pl, size_t num);
void __init ux500_offchip_gpio_init(struct ux500_pins *pins);
struct ux500_pins *ux500_pins_get(const char *name);
int ux500_pins_enable(struct ux500_pins *pins);
int ux500_pins_disable(struct ux500_pins *pins);
void ux500_pins_put(struct ux500_pins *pins);
int pins_for_u9500(void);

#endif
