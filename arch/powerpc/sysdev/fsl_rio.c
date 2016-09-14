/*
 * Freescale MPC85xx/MPC86xx RapidIO support
 *
 * Copyright 2009 Sysgo AG
 * Thomas Moll <thomas.moll@sysgo.com>
 * - fixed maintenance access routines, check for aligned access
 *
 * Copyright 2009 Integrated Device Technology, Inc.
 * Alex Bounine <alexandre.bounine@idt.com>
 * - Added Port-Write message handling
 * - Added Machine Check exception handling
 *
 * Copyright (C) 2007, 2008, 2010, 2011 Freescale Semiconductor, Inc.
 * Zhang Wei <wei.zhang@freescale.com>
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/extable.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <linux/io.h>
#include <linux/uaccess.h>
#include <asm/machdep.h>

#include "fsl_rio.h"

#undef DEBUG_PW	/* Port-Write debugging */

#define RIO_PORT1_EDCSR		0x0640
#define RIO_PORT2_EDCSR		0x0680
#define RIO_PORT1_IECSR		0x10130
#define RIO_PORT2_IECSR		0x101B0

#define RIO_GCCSR		0x13c
#define RIO_ESCSR		0x158
#define ESCSR_CLEAR		0x07120204
#define RIO_PORT2_ESCSR		0x178
#define RIO_CCSR		0x15c
#define RIO_LTLEDCSR_IER	0x80000000
#define RIO_LTLEDCSR_PRT	0x01000000
#define IECSR_CLEAR		0x80000000
#define RIO_ISR_AACR		0x10120
#define RIO_ISR_AACR_AA		0x1	/* Accept All ID */

#define RIWTAR_TRAD_VAL_SHIFT	12
#define RIWTAR_TRAD_MASK	0x00FFFFFF
#define RIWBAR_BADD_VAL_SHIFT	12
#define RIWBAR_BADD_MASK	0x003FFFFF
#define RIWAR_ENABLE		0x80000000
#define RIWAR_TGINT_LOCAL	0x00F00000
#define RIWAR_RDTYP_NO_SNOOP	0x00040000
#define RIWAR_RDTYP_SNOOP	0x00050000
#define RIWAR_WRTYP_NO_SNOOP	0x00004000
#define RIWAR_WRTYP_SNOOP	0x00005000
#define RIWAR_WRTYP_ALLOC	0x00006000
#define RIWAR_SIZE_MASK		0x0000003F

#define __fsl_read_rio_config(x, addr, err, op)		\
	__asm__ __volatile__(				\
		"1:	"op" %1,0(%2)\n"		\
		"	eieio\n"			\
		"2:\n"					\
		".section .fixup,\"ax\"\n"		\
		"3:	li %1,-1\n"			\
		"	li %0,%3\n"			\
		"	b 2b\n"				\
		".section __ex_table,\"a\"\n"		\
			PPC_LONG_ALIGN "\n"		\
			PPC_LONG "1b,3b\n"		\
		".text"					\
		: "=r" (err), "=r" (x)			\
		: "b" (addr), "i" (-EFAULT), "0" (err))

void __iomem *rio_regs_win;
void __iomem *rmu_regs_win;
resource_size_t rio_law_start;

struct fsl_rio_dbell *dbell;
struct fsl_rio_pw *pw;

#ifdef CONFIG_E500
int fsl_rio_mcheck_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *entry;
	unsigned long reason;

	if (!rio_regs_win)
		return 0;

	reason = in_be32((u32 *)(rio_regs_win + RIO_LTLEDCSR));
	if (reason & (RIO_LTLEDCSR_IER | RIO_LTLEDCSR_PRT)) {
		/* Check if we are prepared to handle this fault */
		entry = search_exception_tables(regs->nip);
		if (entry) {
			pr_debug("RIO: %s - MC Exception handled\n",
				 __func__);
			out_be32((u32 *)(rio_regs_win + RIO_LTLEDCSR),
				 0);
			regs->msr |= MSR_RI;
			regs->nip = entry->fixup;
			return 1;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fsl_rio_mcheck_exception);
#endif

/**
 * fsl_local_config_read - Generate a MPC85xx local config space read
 * @mport: RapidIO master port info
 * @index: ID of RapdiIO interface
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @data: Value to be read into
 *
 * Generates a MPC85xx local configuration space read. Returns %0 on
 * success or %-EINVAL on failure.
 */
static int fsl_local_config_read(struct rio_mport *mport,
				int index, u32 offset, int len, u32 *data)
{
	struct rio_priv *priv = mport->priv;
	pr_debug("fsl_local_config_read: index %d offset %8.8x\n", index,
		 offset);
	*data = in_be32(priv->regs_win + offset);

	return 0;
}

/**
 * fsl_local_config_write - Generate a MPC85xx local config space write
 * @mport: RapidIO master port info
 * @index: ID of RapdiIO interface
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @data: Value to be written
 *
 * Generates a MPC85xx local configuration space write. Returns %0 on
 * success or %-EINVAL on failure.
 */
static int fsl_local_config_write(struct rio_mport *mport,
				int index, u32 offset, int len, u32 data)
{
	struct rio_priv *priv = mport->priv;
	pr_debug
		("fsl_local_config_write: index %d offset %8.8x data %8.8x\n",
		index, offset, data);
	out_be32(priv->regs_win + offset, data);

	return 0;
}

/**
 * fsl_rio_config_read - Generate a MPC85xx read maintenance transaction
 * @mport: RapidIO master port info
 * @index: ID of RapdiIO interface
 * @destid: Destination ID of transaction
 * @hopcount: Number of hops to target device
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @val: Location to be read into
 *
 * Generates a MPC85xx read maintenance transaction. Returns %0 on
 * success or %-EINVAL on failure.
 */
static int
fsl_rio_config_read(struct rio_mport *mport, int index, u16 destid,
			u8 hopcount, u32 offset, int len, u32 *val)
{
	struct rio_priv *priv = mport->priv;
	u8 *data;
	u32 rval, err = 0;

	pr_debug
		("fsl_rio_config_read:"
		" index %d destid %d hopcount %d offset %8.8x len %d\n",
		index, destid, hopcount, offset, len);

	/* 16MB maintenance window possible */
	/* allow only aligned access to maintenance registers */
	if (offset > (0x1000000 - len) || !IS_ALIGNED(offset, len))
		return -EINVAL;

	out_be32(&priv->maint_atmu_regs->rowtar,
		 (destid << 22) | (hopcount << 12) | (offset >> 12));
	out_be32(&priv->maint_atmu_regs->rowtear, (destid >> 10));

	data = (u8 *) priv->maint_win + (offset & (RIO_MAINT_WIN_SIZE - 1));
	switch (len) {
	case 1:
		__fsl_read_rio_config(rval, data, err, "lbz");
		break;
	case 2:
		__fsl_read_rio_config(rval, data, err, "lhz");
		break;
	case 4:
		__fsl_read_rio_config(rval, data, err, "lwz");
		break;
	default:
		return -EINVAL;
	}

	if (err) {
		pr_debug("RIO: cfg_read error %d for %x:%x:%x\n",
			 err, destid, hopcount, offset);
	}

	*val = rval;

	return err;
}

/**
 * fsl_rio_config_write - Generate a MPC85xx write maintenance transaction
 * @mport: RapidIO master port info
 * @index: ID of RapdiIO interface
 * @destid: Destination ID of transaction
 * @hopcount: Number of hops to target device
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @val: Value to be written
 *
 * Generates an MPC85xx write maintenance transaction. Returns %0 on
 * success or %-EINVAL on failure.
 */
static int
fsl_rio_config_write(struct rio_mport *mport, int index, u16 destid,
			u8 hopcount, u32 offset, int len, u32 val)
{
	struct rio_priv *priv = mport->priv;
	u8 *data;
	pr_debug
		("fsl_rio_config_write:"
		" index %d destid %d hopcount %d offset %8.8x len %d val %8.8x\n",
		index, destid, hopcount, offset, len, val);

	/* 16MB maintenance windows possible */
	/* allow only aligned access to maintenance registers */
	if (offset > (0x1000000 - len) || !IS_ALIGNED(offset, len))
		return -EINVAL;

	out_be32(&priv->maint_atmu_regs->rowtar,
		 (destid << 22) | (hopcount << 12) | (offset >> 12));
	out_be32(&priv->maint_atmu_regs->rowtear, (destid >> 10));

	data = (u8 *) priv->maint_win + (offset & (RIO_MAINT_WIN_SIZE - 1));
	switch (len) {
	case 1:
		out_8((u8 *) data, val);
		break;
	case 2:
		out_be16((u16 *) data, val);
		break;
	case 4:
		out_be32((u32 *) data, val);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void fsl_rio_inbound_mem_init(struct rio_priv *priv)
{
	int i;

	/* close inbound windows */
	for (i = 0; i < RIO_INB_ATMU_COUNT; i++)
		out_be32(&priv->inb_atmu_regs[i].riwar, 0);
}

int fsl_map_inb_mem(struct rio_mport *mport, dma_addr_t lstart,
	u64 rstart, u64 size, u32 flags)
{
	struct rio_priv *priv = mport->priv;
	u32 base_size;
	unsigned int base_size_log;
	u64 win_start, win_end;
	u32 riwar;
	int i;

	if ((size & (size - 1)) != 0 || size > 0x400000000ULL)
		return -EINVAL;

	base_size_log = ilog2(size);
	base_size = 1 << base_size_log;

	/* check if addresses are aligned with the window size */
	if (lstart & (base_size - 1))
		return -EINVAL;
	if (rstart & (base_size - 1))
		return -EINVAL;

	/* check for conflicting ranges */
	for (i = 0; i < RIO_INB_ATMU_COUNT; i++) {
		riwar = in_be32(&priv->inb_atmu_regs[i].riwar);
		if ((riwar & RIWAR_ENABLE) == 0)
			continue;
		win_start = ((u64)(in_be32(&priv->inb_atmu_regs[i].riwbar) & RIWBAR_BADD_MASK))
			<< RIWBAR_BADD_VAL_SHIFT;
		win_end = win_start + ((1 << ((riwar & RIWAR_SIZE_MASK) + 1)) - 1);
		if (rstart < win_end && (rstart + size) > win_start)
			return -EINVAL;
	}

	/* find unused atmu */
	for (i = 0; i < RIO_INB_ATMU_COUNT; i++) {
		riwar = in_be32(&priv->inb_atmu_regs[i].riwar);
		if ((riwar & RIWAR_ENABLE) == 0)
			break;
	}
	if (i >= RIO_INB_ATMU_COUNT)
		return -ENOMEM;

	out_be32(&priv->inb_atmu_regs[i].riwtar, lstart >> RIWTAR_TRAD_VAL_SHIFT);
	out_be32(&priv->inb_atmu_regs[i].riwbar, rstart >> RIWBAR_BADD_VAL_SHIFT);
	out_be32(&priv->inb_atmu_regs[i].riwar, RIWAR_ENABLE | RIWAR_TGINT_LOCAL |
		RIWAR_RDTYP_SNOOP | RIWAR_WRTYP_SNOOP | (base_size_log - 1));

	return 0;
}

void fsl_unmap_inb_mem(struct rio_mport *mport, dma_addr_t lstart)
{
	u32 win_start_shift, base_start_shift;
	struct rio_priv *priv = mport->priv;
	u32 riwar, riwtar;
	int i;

	/* skip default window */
	base_start_shift = lstart >> RIWTAR_TRAD_VAL_SHIFT;
	for (i = 0; i < RIO_INB_ATMU_COUNT; i++) {
		riwar = in_be32(&priv->inb_atmu_regs[i].riwar);
		if ((riwar & RIWAR_ENABLE) == 0)
			continue;

		riwtar = in_be32(&priv->inb_atmu_regs[i].riwtar);
		win_start_shift = riwtar & RIWTAR_TRAD_MASK;
		if (win_start_shift == base_start_shift) {
			out_be32(&priv->inb_atmu_regs[i].riwar, riwar & ~RIWAR_ENABLE);
			return;
		}
	}
}

void fsl_rio_port_error_handler(int offset)
{
	/*XXX: Error recovery is not implemented, we just clear errors */
	out_be32((u32 *)(rio_regs_win + RIO_LTLEDCSR), 0);

	if (offset == 0) {
		out_be32((u32 *)(rio_regs_win + RIO_PORT1_EDCSR), 0);
		out_be32((u32 *)(rio_regs_win + RIO_PORT1_IECSR), IECSR_CLEAR);
		out_be32((u32 *)(rio_regs_win + RIO_ESCSR), ESCSR_CLEAR);
	} else {
		out_be32((u32 *)(rio_regs_win + RIO_PORT2_EDCSR), 0);
		out_be32((u32 *)(rio_regs_win + RIO_PORT2_IECSR), IECSR_CLEAR);
		out_be32((u32 *)(rio_regs_win + RIO_PORT2_ESCSR), ESCSR_CLEAR);
	}
}
static inline void fsl_rio_info(struct device *dev, u32 ccsr)
{
	const char *str;
	if (ccsr & 1) {
		/* Serial phy */
		switch (ccsr >> 30) {
		case 0:
			str = "1";
			break;
		case 1:
			str = "4";
			break;
		default:
			str = "Unknown";
			break;
		}
		dev_info(dev, "Hardware port width: %s\n", str);

		switch ((ccsr >> 27) & 7) {
		case 0:
			str = "Single-lane 0";
			break;
		case 1:
			str = "Single-lane 2";
			break;
		case 2:
			str = "Four-lane";
			break;
		default:
			str = "Unknown";
			break;
		}
		dev_info(dev, "Training connection status: %s\n", str);
	} else {
		/* Parallel phy */
		if (!(ccsr & 0x80000000))
			dev_info(dev, "Output port operating in 8-bit mode\n");
		if (!(ccsr & 0x08000000))
			dev_info(dev, "Input port operating in 8-bit mode\n");
	}
}

/**
 * fsl_rio_setup - Setup Freescale PowerPC RapidIO interface
 * @dev: platform_device pointer
 *
 * Initializes MPC85xx RapidIO hardware interface, configures
 * master port with system-specific info, and registers the
 * master port with the RapidIO subsystem.
 */
int fsl_rio_setup(struct platform_device *dev)
{
	struct rio_ops *ops;
	struct rio_mport *port;
	struct rio_priv *priv;
	int rc = 0;
	const u32 *dt_range, *cell, *port_index;
	u32 active_ports = 0;
	struct resource regs, rmu_regs;
	struct device_node *np, *rmu_node;
	int rlen;
	u32 ccsr;
	u64 range_start, range_size;
	int paw, aw, sw;
	u32 i;
	static int tmp;
	struct device_node *rmu_np[MAX_MSG_UNIT_NUM] = {NULL};

	if (!dev->dev.of_node) {
		dev_err(&dev->dev, "Device OF-Node is NULL");
		return -ENODEV;
	}

	rc = of_address_to_resource(dev->dev.of_node, 0, &regs);
	if (rc) {
		dev_err(&dev->dev, "Can't get %s property 'reg'\n",
				dev->dev.of_node->full_name);
		return -EFAULT;
	}
	dev_info(&dev->dev, "Of-device full name %s\n",
			dev->dev.of_node->full_name);
	dev_info(&dev->dev, "Regs: %pR\n", &regs);

	rio_regs_win = ioremap(regs.start, resource_size(&regs));
	if (!rio_regs_win) {
		dev_err(&dev->dev, "Unable to map rio register window\n");
		rc = -ENOMEM;
		goto err_rio_regs;
	}

	ops = kzalloc(sizeof(struct rio_ops), GFP_KERNEL);
	if (!ops) {
		rc = -ENOMEM;
		goto err_ops;
	}
	ops->lcread = fsl_local_config_read;
	ops->lcwrite = fsl_local_config_write;
	ops->cread = fsl_rio_config_read;
	ops->cwrite = fsl_rio_config_write;
	ops->dsend = fsl_rio_doorbell_send;
	ops->pwenable = fsl_rio_pw_enable;
	ops->open_outb_mbox = fsl_open_outb_mbox;
	ops->open_inb_mbox = fsl_open_inb_mbox;
	ops->close_outb_mbox = fsl_close_outb_mbox;
	ops->close_inb_mbox = fsl_close_inb_mbox;
	ops->add_outb_message = fsl_add_outb_message;
	ops->add_inb_buffer = fsl_add_inb_buffer;
	ops->get_inb_message = fsl_get_inb_message;
	ops->map_inb = fsl_map_inb_mem;
	ops->unmap_inb = fsl_unmap_inb_mem;

	rmu_node = of_parse_phandle(dev->dev.of_node, "fsl,srio-rmu-handle", 0);
	if (!rmu_node) {
		dev_err(&dev->dev, "No valid fsl,srio-rmu-handle property\n");
		rc = -ENOENT;
		goto err_rmu;
	}
	rc = of_address_to_resource(rmu_node, 0, &rmu_regs);
	if (rc) {
		dev_err(&dev->dev, "Can't get %s property 'reg'\n",
				rmu_node->full_name);
		goto err_rmu;
	}
	rmu_regs_win = ioremap(rmu_regs.start, resource_size(&rmu_regs));
	if (!rmu_regs_win) {
		dev_err(&dev->dev, "Unable to map rmu register window\n");
		rc = -ENOMEM;
		goto err_rmu;
	}
	for_each_compatible_node(np, NULL, "fsl,srio-msg-unit") {
		rmu_np[tmp] = np;
		tmp++;
	}

	/*set up doobell node*/
	np = of_find_compatible_node(NULL, NULL, "fsl,srio-dbell-unit");
	if (!np) {
		dev_err(&dev->dev, "No fsl,srio-dbell-unit node\n");
		rc = -ENODEV;
		goto err_dbell;
	}
	dbell = kzalloc(sizeof(struct fsl_rio_dbell), GFP_KERNEL);
	if (!(dbell)) {
		dev_err(&dev->dev, "Can't alloc memory for 'fsl_rio_dbell'\n");
		rc = -ENOMEM;
		goto err_dbell;
	}
	dbell->dev = &dev->dev;
	dbell->bellirq = irq_of_parse_and_map(np, 1);
	dev_info(&dev->dev, "bellirq: %d\n", dbell->bellirq);

	aw = of_n_addr_cells(np);
	dt_range = of_get_property(np, "reg", &rlen);
	if (!dt_range) {
		pr_err("%s: unable to find 'reg' property\n",
			np->full_name);
		rc = -ENOMEM;
		goto err_pw;
	}
	range_start = of_read_number(dt_range, aw);
	dbell->dbell_regs = (struct rio_dbell_regs *)(rmu_regs_win +
				(u32)range_start);

	/*set up port write node*/
	np = of_find_compatible_node(NULL, NULL, "fsl,srio-port-write-unit");
	if (!np) {
		dev_err(&dev->dev, "No fsl,srio-port-write-unit node\n");
		rc = -ENODEV;
		goto err_pw;
	}
	pw = kzalloc(sizeof(struct fsl_rio_pw), GFP_KERNEL);
	if (!(pw)) {
		dev_err(&dev->dev, "Can't alloc memory for 'fsl_rio_pw'\n");
		rc = -ENOMEM;
		goto err_pw;
	}
	pw->dev = &dev->dev;
	pw->pwirq = irq_of_parse_and_map(np, 0);
	dev_info(&dev->dev, "pwirq: %d\n", pw->pwirq);
	aw = of_n_addr_cells(np);
	dt_range = of_get_property(np, "reg", &rlen);
	if (!dt_range) {
		pr_err("%s: unable to find 'reg' property\n",
			np->full_name);
		rc = -ENOMEM;
		goto err;
	}
	range_start = of_read_number(dt_range, aw);
	pw->pw_regs = (struct rio_pw_regs *)(rmu_regs_win + (u32)range_start);

	/*set up ports node*/
	for_each_child_of_node(dev->dev.of_node, np) {
		port_index = of_get_property(np, "cell-index", NULL);
		if (!port_index) {
			dev_err(&dev->dev, "Can't get %s property 'cell-index'\n",
					np->full_name);
			continue;
		}

		dt_range = of_get_property(np, "ranges", &rlen);
		if (!dt_range) {
			dev_err(&dev->dev, "Can't get %s property 'ranges'\n",
					np->full_name);
			continue;
		}

		/* Get node address wide */
		cell = of_get_property(np, "#address-cells", NULL);
		if (cell)
			aw = *cell;
		else
			aw = of_n_addr_cells(np);
		/* Get node size wide */
		cell = of_get_property(np, "#size-cells", NULL);
		if (cell)
			sw = *cell;
		else
			sw = of_n_size_cells(np);
		/* Get parent address wide wide */
		paw = of_n_addr_cells(np);
		range_start = of_read_number(dt_range + aw, paw);
		range_size = of_read_number(dt_range + aw + paw, sw);

		dev_info(&dev->dev, "%s: LAW start 0x%016llx, size 0x%016llx.\n",
				np->full_name, range_start, range_size);

		port = kzalloc(sizeof(struct rio_mport), GFP_KERNEL);
		if (!port)
			continue;

		rc = rio_mport_initialize(port);
		if (rc) {
			kfree(port);
			continue;
		}

		i = *port_index - 1;
		port->index = (unsigned char)i;

		priv = kzalloc(sizeof(struct rio_priv), GFP_KERNEL);
		if (!priv) {
			dev_err(&dev->dev, "Can't alloc memory for 'priv'\n");
			kfree(port);
			continue;
		}

		INIT_LIST_HEAD(&port->dbells);
		port->iores.start = range_start;
		port->iores.end = port->iores.start + range_size - 1;
		port->iores.flags = IORESOURCE_MEM;
		port->iores.name = "rio_io_win";

		if (request_resource(&iomem_resource, &port->iores) < 0) {
			dev_err(&dev->dev, "RIO: Error requesting master port region"
				" 0x%016llx-0x%016llx\n",
				(u64)port->iores.start, (u64)port->iores.end);
				kfree(priv);
				kfree(port);
				continue;
		}
		sprintf(port->name, "RIO mport %d", i);

		priv->dev = &dev->dev;
		port->dev.parent = &dev->dev;
		port->ops = ops;
		port->priv = priv;
		port->phys_efptr = 0x100;
		port->phys_rmap = 1;
		priv->regs_win = rio_regs_win;

		ccsr = in_be32(priv->regs_win + RIO_CCSR + i*0x20);

		/* Checking the port training status */
		if (in_be32((priv->regs_win + RIO_ESCSR + i*0x20)) & 1) {
			dev_err(&dev->dev, "Port %d is not ready. "
			"Try to restart connection...\n", i);
			/* Disable ports */
			out_be32(priv->regs_win
				+ RIO_CCSR + i*0x20, 0);
			/* Set 1x lane */
			setbits32(priv->regs_win
				+ RIO_CCSR + i*0x20, 0x02000000);
			/* Enable ports */
			setbits32(priv->regs_win
				+ RIO_CCSR + i*0x20, 0x00600000);
			msleep(100);
			if (in_be32((priv->regs_win
					+ RIO_ESCSR + i*0x20)) & 1) {
				dev_err(&dev->dev,
					"Port %d restart failed.\n", i);
				release_resource(&port->iores);
				kfree(priv);
				kfree(port);
				continue;
			}
			dev_info(&dev->dev, "Port %d restart success!\n", i);
		}
		fsl_rio_info(&dev->dev, ccsr);

		port->sys_size = (in_be32((priv->regs_win + RIO_PEF_CAR))
					& RIO_PEF_CTLS) >> 4;
		dev_info(&dev->dev, "RapidIO Common Transport System size: %d\n",
				port->sys_size ? 65536 : 256);

		if (port->host_deviceid >= 0)
			out_be32(priv->regs_win + RIO_GCCSR, RIO_PORT_GEN_HOST |
				RIO_PORT_GEN_MASTER | RIO_PORT_GEN_DISCOVERED);
		else
			out_be32(priv->regs_win + RIO_GCCSR,
				RIO_PORT_GEN_MASTER);

		priv->atmu_regs = (struct rio_atmu_regs *)(priv->regs_win
			+ ((i == 0) ? RIO_ATMU_REGS_PORT1_OFFSET :
			RIO_ATMU_REGS_PORT2_OFFSET));

		priv->maint_atmu_regs = priv->atmu_regs + 1;
		priv->inb_atmu_regs = (struct rio_inb_atmu_regs __iomem *)
			(priv->regs_win +
			((i == 0) ? RIO_INB_ATMU_REGS_PORT1_OFFSET :
			RIO_INB_ATMU_REGS_PORT2_OFFSET));

		/* Set to receive packets with any dest ID */
		out_be32((priv->regs_win + RIO_ISR_AACR + i*0x80),
			 RIO_ISR_AACR_AA);

		/* Configure maintenance transaction window */
		out_be32(&priv->maint_atmu_regs->rowbar,
			port->iores.start >> 12);
		out_be32(&priv->maint_atmu_regs->rowar,
			 0x80077000 | (ilog2(RIO_MAINT_WIN_SIZE) - 1));

		priv->maint_win = ioremap(port->iores.start,
				RIO_MAINT_WIN_SIZE);

		rio_law_start = range_start;

		fsl_rio_setup_rmu(port, rmu_np[i]);
		fsl_rio_inbound_mem_init(priv);

		dbell->mport[i] = port;
		pw->mport[i] = port;

		if (rio_register_mport(port)) {
			release_resource(&port->iores);
			kfree(priv);
			kfree(port);
			continue;
		}
		active_ports++;
	}

	if (!active_ports) {
		rc = -ENOLINK;
		goto err;
	}

	fsl_rio_doorbell_init(dbell);
	fsl_rio_port_write_init(pw);

	return 0;
err:
	kfree(pw);
	pw = NULL;
err_pw:
	kfree(dbell);
	dbell = NULL;
err_dbell:
	iounmap(rmu_regs_win);
	rmu_regs_win = NULL;
err_rmu:
	kfree(ops);
err_ops:
	iounmap(rio_regs_win);
	rio_regs_win = NULL;
err_rio_regs:
	return rc;
}

/* The probe function for RapidIO peer-to-peer network.
 */
static int fsl_of_rio_rpn_probe(struct platform_device *dev)
{
	printk(KERN_INFO "Setting up RapidIO peer-to-peer network %s\n",
			dev->dev.of_node->full_name);

	return fsl_rio_setup(dev);
};

static const struct of_device_id fsl_of_rio_rpn_ids[] = {
	{
		.compatible = "fsl,srio",
	},
	{},
};

static struct platform_driver fsl_of_rio_rpn_driver = {
	.driver = {
		.name = "fsl-of-rio",
		.of_match_table = fsl_of_rio_rpn_ids,
	},
	.probe = fsl_of_rio_rpn_probe,
};

static __init int fsl_of_rio_rpn_init(void)
{
	return platform_driver_register(&fsl_of_rio_rpn_driver);
}

subsys_initcall(fsl_of_rio_rpn_init);
