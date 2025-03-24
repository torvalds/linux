// SPDX-License-Identifier: GPL-2.0-only
/*
 * Shared helpers to register GPIO-connected buttons and LEDs
 * on AMD Geode boards.
 */

#ifndef __PLATFORM_GEODE_COMMON_H
#define __PLATFORM_GEODE_COMMON_H

#include <linux/property.h>

struct geode_led {
	unsigned int pin;
	bool default_on;
};

int geode_create_restart_key(unsigned int pin);
int geode_create_leds(const char *label, const struct geode_led *leds,
		      unsigned int n_leds);

#endif /* __PLATFORM_GEODE_COMMON_H */
