/*
 * Support for viafb GPIO ports.
 *
 * Copyright 2009 Jonathan Corbet <corbet@lwn.net>
 * Distributable under version 2 of the GNU General Public License.
 */

#ifndef __VIA_GPIO_H__
#define __VIA_GPIO_H__

extern int viafb_create_gpios(struct viafb_dev *vdev,
		const struct via_port_cfg *port_cfg);
extern int viafb_destroy_gpios(void);
extern int viafb_gpio_lookup(const char *name);
#endif
