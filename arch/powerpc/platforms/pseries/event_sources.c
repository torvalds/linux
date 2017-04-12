/*
 * Copyright (C) 2001 Dave Engebretsen IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <asm/prom.h>

#include "pseries.h"

void request_event_sources_irqs(struct device_node *np,
				irq_handler_t handler,
				const char *name)
{
	int i, index, count = 0;
	struct of_phandle_args oirq;
	unsigned int virqs[16];

	/* First try to do a proper OF tree parsing */
	for (index = 0; of_irq_parse_one(np, index, &oirq) == 0;
	     index++) {
		if (count > 15)
			break;
		virqs[count] = irq_create_of_mapping(&oirq);
		if (!virqs[count]) {
			pr_err("event-sources: Unable to allocate "
			       "interrupt number for %s\n",
			       np->full_name);
			WARN_ON(1);
		} else {
			count++;
		}
	}

	/* Now request them */
	for (i = 0; i < count; i++) {
		if (request_irq(virqs[i], handler, 0, name, NULL)) {
			pr_err("event-sources: Unable to request interrupt "
			       "%d for %s\n", virqs[i], np->full_name);
			WARN_ON(1);
			return;
		}
	}
}

