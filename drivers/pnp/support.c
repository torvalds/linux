/*
 * support.c - standard functions for the use of pnp protocol drivers
 *
 * Copyright 2003 Adam Belay <ambx1@neo.rr.com>
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
	str[3] = hex_asc((id >> 12) & 0xf);
	str[4] = hex_asc((id >>  8) & 0xf);
	str[5] = hex_asc((id >>  4) & 0xf);
	str[6] = hex_asc((id >>  0) & 0xf);
	str[7] = '\0';
}

void dbg_pnp_show_resources(struct pnp_dev *dev, char *desc)
{
#ifdef DEBUG
	struct resource *res;
	int i;

	dev_dbg(&dev->dev, "current resources: %s\n", desc);

	for (i = 0; i < PNP_MAX_IRQ; i++) {
		res = pnp_get_resource(dev, IORESOURCE_IRQ, i);
		if (res && !(res->flags & IORESOURCE_UNSET))
			dev_dbg(&dev->dev, "  irq %lld flags %#lx\n",
				(unsigned long long) res->start, res->flags);
	}
	for (i = 0; i < PNP_MAX_DMA; i++) {
		res = pnp_get_resource(dev, IORESOURCE_DMA, i);
		if (res && !(res->flags & IORESOURCE_UNSET))
			dev_dbg(&dev->dev, "  dma %lld flags %#lx\n",
				(unsigned long long) res->start, res->flags);
	}
	for (i = 0; i < PNP_MAX_PORT; i++) {
		res = pnp_get_resource(dev, IORESOURCE_IO, i);
		if (res && !(res->flags & IORESOURCE_UNSET))
			dev_dbg(&dev->dev, "  io  %#llx-%#llx flags %#lx\n",
				(unsigned long long) res->start,
				(unsigned long long) res->end, res->flags);
	}
	for (i = 0; i < PNP_MAX_MEM; i++) {
		res = pnp_get_resource(dev, IORESOURCE_MEM, i);
		if (res && !(res->flags & IORESOURCE_UNSET))
			dev_dbg(&dev->dev, "  mem %#llx-%#llx flags %#lx\n",
				(unsigned long long) res->start,
				(unsigned long long) res->end, res->flags);
	}
#endif
}
