/*
 * support.c - standard functions for the use of pnp protocol drivers
 *
 * Copyright 2003 Adam Belay <ambx1@neo.rr.com>
 * Copyright (C) 2008 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 */

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/pnp.h>
#include "base.h"

/**
 * pnp_is_active - Determines if a device is active based on its current
 *	resources
 * @dev: pointer to the desired PnP device
 */
int pnp_is_active(struct pnp_dev *dev)
{
	/*
	 * I don't think this is very reliable because pnp_disable_dev()
	 * only clears out auto-assigned resources.
	 */
	if (!pnp_port_start(dev, 0) && pnp_port_len(dev, 0) <= 1 &&
	    !pnp_mem_start(dev, 0) && pnp_mem_len(dev, 0) <= 1 &&
	    pnp_irq(dev, 0) == -1 && pnp_dma(dev, 0) == -1)
		return 0;
	else
		return 1;
}

EXPORT_SYMBOL(pnp_is_active);

/*
 * Functionally similar to acpi_ex_eisa_id_to_string(), but that's
 * buried in the ACPI CA, and we can't depend on it being present.
 */
void pnp_eisa_id_to_string(u32 id, char *str)
{
	id = be32_to_cpu(id);

	/*
	 * According to the specs, the first three characters are five-bit
	 * compressed ASCII, and the left-over high order bit should be zero.
	 * However, the Linux ISAPNP code historically used six bits for the
	 * first character, and there seem to be IDs that depend on that,
	 * e.g., "nEC8241" in the Linux 8250_pnp serial driver and the
	 * FreeBSD sys/pc98/cbus/sio_cbus.c driver.
	 */
	str[0] = 'A' + ((id >> 26) & 0x3f) - 1;
	str[1] = 'A' + ((id >> 21) & 0x1f) - 1;
	str[2] = 'A' + ((id >> 16) & 0x1f) - 1;
	str[3] = hex_asc_hi(id >> 8);
	str[4] = hex_asc_lo(id >> 8);
	str[5] = hex_asc_hi(id);
	str[6] = hex_asc_lo(id);
	str[7] = '\0';
}

char *pnp_resource_type_name(struct resource *res)
{
	switch (pnp_resource_type(res)) {
	case IORESOURCE_IO:
		return "io";
	case IORESOURCE_MEM:
		return "mem";
	case IORESOURCE_IRQ:
		return "irq";
	case IORESOURCE_DMA:
		return "dma";
	}
	return NULL;
}

void dbg_pnp_show_resources(struct pnp_dev *dev, char *desc)
{
	char buf[128];
	int len;
	struct pnp_resource *pnp_res;
	struct resource *res;

	if (list_empty(&dev->resources)) {
		pnp_dbg(&dev->dev, "%s: no current resources\n", desc);
		return;
	}

	pnp_dbg(&dev->dev, "%s: current resources:\n", desc);
	list_for_each_entry(pnp_res, &dev->resources, list) {
		res = &pnp_res->res;
		len = 0;

		len += scnprintf(buf + len, sizeof(buf) - len, "  %-3s ",
				 pnp_resource_type_name(res));

		if (res->flags & IORESOURCE_DISABLED) {
			pnp_dbg(&dev->dev, "%sdisabled\n", buf);
			continue;
		}

		switch (pnp_resource_type(res)) {
		case IORESOURCE_IO:
		case IORESOURCE_MEM:
			len += scnprintf(buf + len, sizeof(buf) - len,
					 "%#llx-%#llx flags %#lx",
					 (unsigned long long) res->start,
					 (unsigned long long) res->end,
					 res->flags);
			break;
		case IORESOURCE_IRQ:
		case IORESOURCE_DMA:
			len += scnprintf(buf + len, sizeof(buf) - len,
					 "%lld flags %#lx",
					 (unsigned long long) res->start,
					 res->flags);
			break;
		}
		pnp_dbg(&dev->dev, "%s\n", buf);
	}
}

char *pnp_option_priority_name(struct pnp_option *option)
{
	switch (pnp_option_priority(option)) {
	case PNP_RES_PRIORITY_PREFERRED:
		return "preferred";
	case PNP_RES_PRIORITY_ACCEPTABLE:
		return "acceptable";
	case PNP_RES_PRIORITY_FUNCTIONAL:
		return "functional";
	}
	return "invalid";
}

void dbg_pnp_show_option(struct pnp_dev *dev, struct pnp_option *option)
{
	char buf[128];
	int len = 0, i;
	struct pnp_port *port;
	struct pnp_mem *mem;
	struct pnp_irq *irq;
	struct pnp_dma *dma;

	if (pnp_option_is_dependent(option))
		len += scnprintf(buf + len, sizeof(buf) - len,
				 "  dependent set %d (%s) ",
				 pnp_option_set(option),
				 pnp_option_priority_name(option));
	else
		len += scnprintf(buf + len, sizeof(buf) - len,
				 "  independent ");

	switch (option->type) {
	case IORESOURCE_IO:
		port = &option->u.port;
		len += scnprintf(buf + len, sizeof(buf) - len, "io  min %#llx "
				 "max %#llx align %lld size %lld flags %#x",
				 (unsigned long long) port->min,
				 (unsigned long long) port->max,
				 (unsigned long long) port->align,
				 (unsigned long long) port->size, port->flags);
		break;
	case IORESOURCE_MEM:
		mem = &option->u.mem;
		len += scnprintf(buf + len, sizeof(buf) - len, "mem min %#llx "
				 "max %#llx align %lld size %lld flags %#x",
				 (unsigned long long) mem->min,
				 (unsigned long long) mem->max,
				 (unsigned long long) mem->align,
				 (unsigned long long) mem->size, mem->flags);
		break;
	case IORESOURCE_IRQ:
		irq = &option->u.irq;
		len += scnprintf(buf + len, sizeof(buf) - len, "irq");
		if (bitmap_empty(irq->map.bits, PNP_IRQ_NR))
			len += scnprintf(buf + len, sizeof(buf) - len,
					 " <none>");
		else {
			for (i = 0; i < PNP_IRQ_NR; i++)
				if (test_bit(i, irq->map.bits))
					len += scnprintf(buf + len,
							 sizeof(buf) - len,
							 " %d", i);
		}
		len += scnprintf(buf + len, sizeof(buf) - len, " flags %#x",
				 irq->flags);
		if (irq->flags & IORESOURCE_IRQ_OPTIONAL)
			len += scnprintf(buf + len, sizeof(buf) - len,
					 " (optional)");
		break;
	case IORESOURCE_DMA:
		dma = &option->u.dma;
		len += scnprintf(buf + len, sizeof(buf) - len, "dma");
		if (!dma->map)
			len += scnprintf(buf + len, sizeof(buf) - len,
					 " <none>");
		else {
			for (i = 0; i < 8; i++)
				if (dma->map & (1 << i))
					len += scnprintf(buf + len,
							 sizeof(buf) - len,
							 " %d", i);
		}
		len += scnprintf(buf + len, sizeof(buf) - len, " (bitmask %#x) "
				 "flags %#x", dma->map, dma->flags);
		break;
	}
	pnp_dbg(&dev->dev, "%s\n", buf);
}
