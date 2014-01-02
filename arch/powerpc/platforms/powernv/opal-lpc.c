/*
 * PowerNV LPC bus handling.
 *
 * Copyright 2013 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/bug.h>

#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/xics.h>
#include <asm/opal.h>
#include <asm/prom.h>

static int opal_lpc_chip_id = -1;

static u8 opal_lpc_inb(unsigned long port)
{
	int64_t rc;
	uint32_t data;

	if (opal_lpc_chip_id < 0 || port > 0xffff)
		return 0xff;
	rc = opal_lpc_read(opal_lpc_chip_id, OPAL_LPC_IO, port, &data, 1);
	return rc ? 0xff : data;
}

static __le16 __opal_lpc_inw(unsigned long port)
{
	int64_t rc;
	uint32_t data;

	if (opal_lpc_chip_id < 0 || port > 0xfffe)
		return 0xffff;
	if (port & 1)
		return (__le16)opal_lpc_inb(port) << 8 | opal_lpc_inb(port + 1);
	rc = opal_lpc_read(opal_lpc_chip_id, OPAL_LPC_IO, port, &data, 2);
	return rc ? 0xffff : data;
}
static u16 opal_lpc_inw(unsigned long port)
{
	return le16_to_cpu(__opal_lpc_inw(port));
}

static __le32 __opal_lpc_inl(unsigned long port)
{
	int64_t rc;
	uint32_t data;

	if (opal_lpc_chip_id < 0 || port > 0xfffc)
		return 0xffffffff;
	if (port & 3)
		return (__le32)opal_lpc_inb(port    ) << 24 |
		       (__le32)opal_lpc_inb(port + 1) << 16 |
		       (__le32)opal_lpc_inb(port + 2) <<  8 |
			       opal_lpc_inb(port + 3);
	rc = opal_lpc_read(opal_lpc_chip_id, OPAL_LPC_IO, port, &data, 4);
	return rc ? 0xffffffff : data;
}

static u32 opal_lpc_inl(unsigned long port)
{
	return le32_to_cpu(__opal_lpc_inl(port));
}

static void opal_lpc_outb(u8 val, unsigned long port)
{
	if (opal_lpc_chip_id < 0 || port > 0xffff)
		return;
	opal_lpc_write(opal_lpc_chip_id, OPAL_LPC_IO, port, val, 1);
}

static void __opal_lpc_outw(__le16 val, unsigned long port)
{
	if (opal_lpc_chip_id < 0 || port > 0xfffe)
		return;
	if (port & 1) {
		opal_lpc_outb(val >> 8, port);
		opal_lpc_outb(val     , port + 1);
		return;
	}
	opal_lpc_write(opal_lpc_chip_id, OPAL_LPC_IO, port, val, 2);
}

static void opal_lpc_outw(u16 val, unsigned long port)
{
	__opal_lpc_outw(cpu_to_le16(val), port);
}

static void __opal_lpc_outl(__le32 val, unsigned long port)
{
	if (opal_lpc_chip_id < 0 || port > 0xfffc)
		return;
	if (port & 3) {
		opal_lpc_outb(val >> 24, port);
		opal_lpc_outb(val >> 16, port + 1);
		opal_lpc_outb(val >>  8, port + 2);
		opal_lpc_outb(val      , port + 3);
		return;
	}
	opal_lpc_write(opal_lpc_chip_id, OPAL_LPC_IO, port, val, 4);
}

static void opal_lpc_outl(u32 val, unsigned long port)
{
	__opal_lpc_outl(cpu_to_le32(val), port);
}

static void opal_lpc_insb(unsigned long p, void *b, unsigned long c)
{
	u8 *ptr = b;

	while(c--)
		*(ptr++) = opal_lpc_inb(p);
}

static void opal_lpc_insw(unsigned long p, void *b, unsigned long c)
{
	__le16 *ptr = b;

	while(c--)
		*(ptr++) = __opal_lpc_inw(p);
}

static void opal_lpc_insl(unsigned long p, void *b, unsigned long c)
{
	__le32 *ptr = b;

	while(c--)
		*(ptr++) = __opal_lpc_inl(p);
}

static void opal_lpc_outsb(unsigned long p, const void *b, unsigned long c)
{
	const u8 *ptr = b;

	while(c--)
		opal_lpc_outb(*(ptr++), p);
}

static void opal_lpc_outsw(unsigned long p, const void *b, unsigned long c)
{
	const __le16 *ptr = b;

	while(c--)
		__opal_lpc_outw(*(ptr++), p);
}

static void opal_lpc_outsl(unsigned long p, const void *b, unsigned long c)
{
	const __le32 *ptr = b;

	while(c--)
		__opal_lpc_outl(*(ptr++), p);
}

static const struct ppc_pci_io opal_lpc_io = {
	.inb	= opal_lpc_inb,
	.inw	= opal_lpc_inw,
	.inl	= opal_lpc_inl,
	.outb	= opal_lpc_outb,
	.outw	= opal_lpc_outw,
	.outl	= opal_lpc_outl,
	.insb	= opal_lpc_insb,
	.insw	= opal_lpc_insw,
	.insl	= opal_lpc_insl,
	.outsb	= opal_lpc_outsb,
	.outsw	= opal_lpc_outsw,
	.outsl	= opal_lpc_outsl,
};

void opal_lpc_init(void)
{
	struct device_node *np;

	/*
	 * Look for a Power8 LPC bus tagged as "primary",
	 * we currently support only one though the OPAL APIs
	 * support any number.
	 */
	for_each_compatible_node(np, NULL, "ibm,power8-lpc") {
		if (!of_device_is_available(np))
			continue;
		if (!of_get_property(np, "primary", NULL))
			continue;
		opal_lpc_chip_id = of_get_ibm_chip_id(np);
		break;
	}
	if (opal_lpc_chip_id < 0)
		return;

	/* Setup special IO ops */
	ppc_pci_io = opal_lpc_io;
	isa_io_special = true;

	pr_info("OPAL: Power8 LPC bus found, chip ID %d\n", opal_lpc_chip_id);
}
