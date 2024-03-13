// SPDX-License-Identifier: GPL-2.0
#undef DEBUG

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/etherdevice.h>
#include <linux/of_address.h>
#include <asm/prom.h>

void of_parse_dma_window(struct device_node *dn, const __be32 *dma_window,
			 unsigned long *busno, unsigned long *phys,
			 unsigned long *size)
{
	u32 cells;
	const __be32 *prop;

	/* busno is always one cell */
	*busno = of_read_number(dma_window, 1);
	dma_window++;

	prop = of_get_property(dn, "ibm,#dma-address-cells", NULL);
	if (!prop)
		prop = of_get_property(dn, "#address-cells", NULL);

	cells = prop ? of_read_number(prop, 1) : of_n_addr_cells(dn);
	*phys = of_read_number(dma_window, cells);

	dma_window += cells;

	prop = of_get_property(dn, "ibm,#dma-size-cells", NULL);
	cells = prop ? of_read_number(prop, 1) : of_n_size_cells(dn);
	*size = of_read_number(dma_window, cells);
}
