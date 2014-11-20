/*
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/sched.h>
#include <linux/io.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/prom.h>

static unsigned int base_addr;

void microblaze_heartbeat(void)
{
	static unsigned int cnt, period, dist;

	if (base_addr) {
		if (cnt == 0 || cnt == dist)
			out_be32(base_addr, 1);
		else if (cnt == 7 || cnt == dist + 7)
			out_be32(base_addr, 0);

		if (++cnt > period) {
			cnt = 0;
			/*
			 * The hyperbolic function below modifies the heartbeat
			 * period length in dependency of the current (5min)
			 * load. It goes through the points f(0)=126, f(1)=86,
			 * f(5)=51, f(inf)->30.
			 */
			period = ((672 << FSHIFT) / (5 * avenrun[0] +
						(7 << FSHIFT))) + 30;
			dist = period / 4;
		}
	}
}

void microblaze_setup_heartbeat(void)
{
	struct device_node *gpio = NULL;
	int *prop;
	int j;
	const char * const gpio_list[] = {
		"xlnx,xps-gpio-1.00.a",
		NULL
	};

	for (j = 0; gpio_list[j] != NULL; j++) {
		gpio = of_find_compatible_node(NULL, NULL, gpio_list[j]);
		if (gpio)
			break;
	}

	if (gpio) {
		base_addr = be32_to_cpup(of_get_property(gpio, "reg", NULL));
		base_addr = (unsigned long) ioremap(base_addr, PAGE_SIZE);
		pr_notice("Heartbeat GPIO at 0x%x\n", base_addr);

		/* GPIO is configured as output */
		prop = (int *) of_get_property(gpio, "xlnx,is-bidir", NULL);
		if (prop)
			out_be32(base_addr + 4, 0);
	}
}
