// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2001 Dave Engebretsen IBM Corporation
 */

#include <linux/interrupt.h>
#include <linux/of_irq.h>

#include "pseries.h"

void __init request_event_sources_irqs(struct device_node *np,
				irq_handler_t handler,
				const char *name)
{
	int i, virq, rc;

	for (i = 0; i < 16; i++) {
		virq = of_irq_get(np, i);
		if (virq < 0)
			return;
		if (WARN(!virq, "event-sources: Unable to allocate "
			        "interrupt number for %pOF\n", np))
			continue;

		rc = request_irq(virq, handler, 0, name, NULL);
		if (WARN(rc, "event-sources: Unable to request interrupt %d for %pOF\n",
		    virq, np))
			return;
	}
}
