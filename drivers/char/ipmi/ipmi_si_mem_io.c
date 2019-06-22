// SPDX-License-Identifier: GPL-2.0+

#include <linux/io.h>
#include "ipmi_si.h"

static unsigned char intf_mem_inb(const struct si_sm_io *io,
				  unsigned int offset)
{
	return readb((io->addr)+(offset * io->regspacing));
}

static void intf_mem_outb(const struct si_sm_io *io, unsigned int offset,
			  unsigned char b)
{
	writeb(b, (io->addr)+(offset * io->regspacing));
}

static unsigned char intf_mem_inw(const struct si_sm_io *io,
				  unsigned int offset)
{
	return (readw((io->addr)+(offset * io->regspacing)) >> io->regshift)
		& 0xff;
}

static void intf_mem_outw(const struct si_sm_io *io, unsigned int offset,
			  unsigned char b)
{
	writeb(b << io->regshift, (io->addr)+(offset * io->regspacing));
}

static unsigned char intf_mem_inl(const struct si_sm_io *io,
				  unsigned int offset)
{
	return (readl((io->addr)+(offset * io->regspacing)) >> io->regshift)
		& 0xff;
}

static void intf_mem_outl(const struct si_sm_io *io, unsigned int offset,
			  unsigned char b)
{
	writel(b << io->regshift, (io->addr)+(offset * io->regspacing));
}

#ifdef readq
static unsigned char mem_inq(const struct si_sm_io *io, unsigned int offset)
{
	return (readq((io->addr)+(offset * io->regspacing)) >> io->regshift)
		& 0xff;
}

static void mem_outq(const struct si_sm_io *io, unsigned int offset,
		     unsigned char b)
{
	writeq(b << io->regshift, (io->addr)+(offset * io->regspacing));
}
#endif

static void mem_region_cleanup(struct si_sm_io *io, int num)
{
	unsigned long addr = io->addr_data;
	int idx;

	for (idx = 0; idx < num; idx++)
		release_mem_region(addr + idx * io->regspacing,
				   io->regsize);
}

static void mem_cleanup(struct si_sm_io *io)
{
	if (io->addr) {
		iounmap(io->addr);
		mem_region_cleanup(io, io->io_size);
	}
}

int ipmi_si_mem_setup(struct si_sm_io *io)
{
	unsigned long addr = io->addr_data;
	int           mapsize, idx;

	if (!addr)
		return -ENODEV;

	/*
	 * Figure out the actual readb/readw/readl/etc routine to use based
	 * upon the register size.
	 */
	switch (io->regsize) {
	case 1:
		io->inputb = intf_mem_inb;
		io->outputb = intf_mem_outb;
		break;
	case 2:
		io->inputb = intf_mem_inw;
		io->outputb = intf_mem_outw;
		break;
	case 4:
		io->inputb = intf_mem_inl;
		io->outputb = intf_mem_outl;
		break;
#ifdef readq
	case 8:
		io->inputb = mem_inq;
		io->outputb = mem_outq;
		break;
#endif
	default:
		dev_warn(io->dev, "Invalid register size: %d\n",
			 io->regsize);
		return -EINVAL;
	}

	/*
	 * Some BIOSes reserve disjoint memory regions in their ACPI
	 * tables.  This causes problems when trying to request the
	 * entire region.  Therefore we must request each register
	 * separately.
	 */
	for (idx = 0; idx < io->io_size; idx++) {
		if (request_mem_region(addr + idx * io->regspacing,
				       io->regsize, DEVICE_NAME) == NULL) {
			/* Undo allocations */
			mem_region_cleanup(io, idx);
			return -EIO;
		}
	}

	/*
	 * Calculate the total amount of memory to claim.  This is an
	 * unusual looking calculation, but it avoids claiming any
	 * more memory than it has to.  It will claim everything
	 * between the first address to the end of the last full
	 * register.
	 */
	mapsize = ((io->io_size * io->regspacing)
		   - (io->regspacing - io->regsize));
	io->addr = ioremap(addr, mapsize);
	if (io->addr == NULL) {
		mem_region_cleanup(io, io->io_size);
		return -EIO;
	}

	io->io_cleanup = mem_cleanup;

	return 0;
}
