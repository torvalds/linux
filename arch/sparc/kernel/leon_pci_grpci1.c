// SPDX-License-Identifier: GPL-2.0
/*
 * leon_pci_grpci1.c: GRPCI1 Host PCI driver
 *
 * Copyright (C) 2013 Aeroflex Gaisler AB
 *
 * This GRPCI1 driver does not support PCI interrupts taken from
 * GPIO pins. Interrupt generation at PCI parity and system error
 * detection is by default turned off since some GRPCI1 cores does
 * not support detection. It can be turned on from the bootloader
 * using the all_pci_errors property.
 *
 * Contributors: Daniel Hellstrom <daniel@gaisler.com>
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include <asm/leon_pci.h>
#include <asm/sections.h>
#include <asm/vaddrs.h>
#include <asm/leon.h>
#include <asm/io.h>

#include "irq.h"

/* Enable/Disable Debugging Configuration Space Access */
#undef GRPCI1_DEBUG_CFGACCESS

/*
 * GRPCI1 APB Register MAP
 */
struct grpci1_regs {
	unsigned int cfg_stat;		/* 0x00 Configuration / Status */
	unsigned int bar0;		/* 0x04 BAR0 (RO) */
	unsigned int page0;		/* 0x08 PAGE0 (RO) */
	unsigned int bar1;		/* 0x0C BAR1 (RO) */
	unsigned int page1;		/* 0x10 PAGE1 */
	unsigned int iomap;		/* 0x14 IO Map */
	unsigned int stat_cmd;		/* 0x18 PCI Status & Command (RO) */
	unsigned int irq;		/* 0x1C Interrupt register */
};

#define REGLOAD(a)	(be32_to_cpu(__raw_readl(&(a))))
#define REGSTORE(a, v)	(__raw_writel(cpu_to_be32(v), &(a)))

#define PAGE0_BTEN_BIT    0
#define PAGE0_BTEN        (1 << PAGE0_BTEN_BIT)

#define CFGSTAT_HOST_BIT  13
#define CFGSTAT_CTO_BIT   8
#define CFGSTAT_HOST      (1 << CFGSTAT_HOST_BIT)
#define CFGSTAT_CTO       (1 << CFGSTAT_CTO_BIT)

#define IRQ_DPE (1 << 9)
#define IRQ_SSE (1 << 8)
#define IRQ_RMA (1 << 7)
#define IRQ_RTA (1 << 6)
#define IRQ_STA (1 << 5)
#define IRQ_DPED (1 << 4)
#define IRQ_INTD (1 << 3)
#define IRQ_INTC (1 << 2)
#define IRQ_INTB (1 << 1)
#define IRQ_INTA (1 << 0)
#define IRQ_DEF_ERRORS (IRQ_RMA | IRQ_RTA | IRQ_STA)
#define IRQ_ALL_ERRORS (IRQ_DPED | IRQ_DEF_ERRORS | IRQ_SSE | IRQ_DPE)
#define IRQ_INTX (IRQ_INTA | IRQ_INTB | IRQ_INTC | IRQ_INTD)
#define IRQ_MASK_BIT 16

#define DEF_PCI_ERRORS (PCI_STATUS_SIG_TARGET_ABORT | \
			PCI_STATUS_REC_TARGET_ABORT | \
			PCI_STATUS_REC_MASTER_ABORT)
#define ALL_PCI_ERRORS (PCI_STATUS_PARITY | PCI_STATUS_DETECTED_PARITY | \
			PCI_STATUS_SIG_SYSTEM_ERROR | DEF_PCI_ERRORS)

#define TGT 256

struct grpci1_priv {
	struct leon_pci_info	info; /* must be on top of this structure */
	struct grpci1_regs __iomem *regs;		/* GRPCI register map */
	struct device		*dev;
	int			pci_err_mask;	/* STATUS register error mask */
	int			irq;		/* LEON irqctrl GRPCI IRQ */
	unsigned char		irq_map[4];	/* GRPCI nexus PCI INTX# IRQs */
	unsigned int		irq_err;	/* GRPCI nexus Virt Error IRQ */

	/* AHB PCI Windows */
	unsigned long		pci_area;	/* MEMORY */
	unsigned long		pci_area_end;
	unsigned long		pci_io;		/* I/O */
	unsigned long		pci_conf;	/* CONFIGURATION */
	unsigned long		pci_conf_end;
	unsigned long		pci_io_va;
};

static struct grpci1_priv *grpci1priv;

static int grpci1_cfg_w32(struct grpci1_priv *priv, unsigned int bus,
				unsigned int devfn, int where, u32 val);

static int grpci1_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct grpci1_priv *priv = dev->bus->sysdata;
	int irq_group;

	/* Use default IRQ decoding on PCI BUS0 according slot numbering */
	irq_group = slot & 0x3;
	pin = ((pin - 1) + irq_group) & 0x3;

	return priv->irq_map[pin];
}

static int grpci1_cfg_r32(struct grpci1_priv *priv, unsigned int bus,
				unsigned int devfn, int where, u32 *val)
{
	u32 *pci_conf, tmp, cfg;

	if (where & 0x3)
		return -EINVAL;

	if (bus == 0) {
		devfn += (0x8 * 6); /* start at AD16=Device0 */
	} else if (bus == TGT) {
		bus = 0;
		devfn = 0; /* special case: bridge controller itself */
	}

	/* Select bus */
	cfg = REGLOAD(priv->regs->cfg_stat);
	REGSTORE(priv->regs->cfg_stat, (cfg & ~(0xf << 23)) | (bus << 23));

	/* do read access */
	pci_conf = (u32 *) (priv->pci_conf | (devfn << 8) | (where & 0xfc));
	tmp = LEON3_BYPASS_LOAD_PA(pci_conf);

	/* check if master abort was received */
	if (REGLOAD(priv->regs->cfg_stat) & CFGSTAT_CTO) {
		*val = 0xffffffff;
		/* Clear Master abort bit in PCI cfg space (is set) */
		tmp = REGLOAD(priv->regs->stat_cmd);
		grpci1_cfg_w32(priv, TGT, 0, PCI_COMMAND, tmp);
	} else {
		/* Bus always little endian (unaffected by byte-swapping) */
		*val = swab32(tmp);
	}

	return 0;
}

static int grpci1_cfg_r16(struct grpci1_priv *priv, unsigned int bus,
				unsigned int devfn, int where, u32 *val)
{
	u32 v;
	int ret;

	if (where & 0x1)
		return -EINVAL;
	ret = grpci1_cfg_r32(priv, bus, devfn, where & ~0x3, &v);
	*val = 0xffff & (v >> (8 * (where & 0x3)));
	return ret;
}

static int grpci1_cfg_r8(struct grpci1_priv *priv, unsigned int bus,
				unsigned int devfn, int where, u32 *val)
{
	u32 v;
	int ret;

	ret = grpci1_cfg_r32(priv, bus, devfn, where & ~0x3, &v);
	*val = 0xff & (v >> (8 * (where & 3)));

	return ret;
}

static int grpci1_cfg_w32(struct grpci1_priv *priv, unsigned int bus,
				unsigned int devfn, int where, u32 val)
{
	unsigned int *pci_conf;
	u32 cfg;

	if (where & 0x3)
		return -EINVAL;

	if (bus == 0) {
		devfn += (0x8 * 6); /* start at AD16=Device0 */
	} else if (bus == TGT) {
		bus = 0;
		devfn = 0; /* special case: bridge controller itself */
	}

	/* Select bus */
	cfg = REGLOAD(priv->regs->cfg_stat);
	REGSTORE(priv->regs->cfg_stat, (cfg & ~(0xf << 23)) | (bus << 23));

	pci_conf = (unsigned int *) (priv->pci_conf |
						(devfn << 8) | (where & 0xfc));
	LEON3_BYPASS_STORE_PA(pci_conf, swab32(val));

	return 0;
}

static int grpci1_cfg_w16(struct grpci1_priv *priv, unsigned int bus,
				unsigned int devfn, int where, u32 val)
{
	int ret;
	u32 v;

	if (where & 0x1)
		return -EINVAL;
	ret = grpci1_cfg_r32(priv, bus, devfn, where&~3, &v);
	if (ret)
		return ret;
	v = (v & ~(0xffff << (8 * (where & 0x3)))) |
	    ((0xffff & val) << (8 * (where & 0x3)));
	return grpci1_cfg_w32(priv, bus, devfn, where & ~0x3, v);
}

static int grpci1_cfg_w8(struct grpci1_priv *priv, unsigned int bus,
				unsigned int devfn, int where, u32 val)
{
	int ret;
	u32 v;

	ret = grpci1_cfg_r32(priv, bus, devfn, where & ~0x3, &v);
	if (ret != 0)
		return ret;
	v = (v & ~(0xff << (8 * (where & 0x3)))) |
	    ((0xff & val) << (8 * (where & 0x3)));
	return grpci1_cfg_w32(priv, bus, devfn, where & ~0x3, v);
}

/* Read from Configuration Space. When entering here the PCI layer has taken
 * the pci_lock spinlock and IRQ is off.
 */
static int grpci1_read_config(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 *val)
{
	struct grpci1_priv *priv = grpci1priv;
	unsigned int busno = bus->number;
	int ret;

	if (PCI_SLOT(devfn) > 15 || busno > 15) {
		*val = ~0;
		return 0;
	}

	switch (size) {
	case 1:
		ret = grpci1_cfg_r8(priv, busno, devfn, where, val);
		break;
	case 2:
		ret = grpci1_cfg_r16(priv, busno, devfn, where, val);
		break;
	case 4:
		ret = grpci1_cfg_r32(priv, busno, devfn, where, val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

#ifdef GRPCI1_DEBUG_CFGACCESS
	printk(KERN_INFO
		"grpci1_read_config: [%02x:%02x:%x] ofs=%d val=%x size=%d\n",
		busno, PCI_SLOT(devfn), PCI_FUNC(devfn), where, *val, size);
#endif

	return ret;
}

/* Write to Configuration Space. When entering here the PCI layer has taken
 * the pci_lock spinlock and IRQ is off.
 */
static int grpci1_write_config(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, u32 val)
{
	struct grpci1_priv *priv = grpci1priv;
	unsigned int busno = bus->number;

	if (PCI_SLOT(devfn) > 15 || busno > 15)
		return 0;

#ifdef GRPCI1_DEBUG_CFGACCESS
	printk(KERN_INFO
		"grpci1_write_config: [%02x:%02x:%x] ofs=%d size=%d val=%x\n",
		busno, PCI_SLOT(devfn), PCI_FUNC(devfn), where, size, val);
#endif

	switch (size) {
	default:
		return -EINVAL;
	case 1:
		return grpci1_cfg_w8(priv, busno, devfn, where, val);
	case 2:
		return grpci1_cfg_w16(priv, busno, devfn, where, val);
	case 4:
		return grpci1_cfg_w32(priv, busno, devfn, where, val);
	}
}

static struct pci_ops grpci1_ops = {
	.read =		grpci1_read_config,
	.write =	grpci1_write_config,
};

/* GENIRQ IRQ chip implementation for grpci1 irqmode=0..2. In configuration
 * 3 where all PCI Interrupts has a separate IRQ on the system IRQ controller
 * this is not needed and the standard IRQ controller can be used.
 */

static void grpci1_mask_irq(struct irq_data *data)
{
	u32 irqidx;
	struct grpci1_priv *priv = grpci1priv;

	irqidx = (u32)data->chip_data - 1;
	if (irqidx > 3) /* only mask PCI interrupts here */
		return;
	irqidx += IRQ_MASK_BIT;

	REGSTORE(priv->regs->irq, REGLOAD(priv->regs->irq) & ~(1 << irqidx));
}

static void grpci1_unmask_irq(struct irq_data *data)
{
	u32 irqidx;
	struct grpci1_priv *priv = grpci1priv;

	irqidx = (u32)data->chip_data - 1;
	if (irqidx > 3) /* only unmask PCI interrupts here */
		return;
	irqidx += IRQ_MASK_BIT;

	REGSTORE(priv->regs->irq, REGLOAD(priv->regs->irq) | (1 << irqidx));
}

static unsigned int grpci1_startup_irq(struct irq_data *data)
{
	grpci1_unmask_irq(data);
	return 0;
}

static void grpci1_shutdown_irq(struct irq_data *data)
{
	grpci1_mask_irq(data);
}

static struct irq_chip grpci1_irq = {
	.name		= "grpci1",
	.irq_startup	= grpci1_startup_irq,
	.irq_shutdown	= grpci1_shutdown_irq,
	.irq_mask	= grpci1_mask_irq,
	.irq_unmask	= grpci1_unmask_irq,
};

/* Handle one or multiple IRQs from the PCI core */
static void grpci1_pci_flow_irq(struct irq_desc *desc)
{
	struct grpci1_priv *priv = grpci1priv;
	int i, ack = 0;
	unsigned int irqreg;

	irqreg = REGLOAD(priv->regs->irq);
	irqreg = (irqreg >> IRQ_MASK_BIT) & irqreg;

	/* Error Interrupt? */
	if (irqreg & IRQ_ALL_ERRORS) {
		generic_handle_irq(priv->irq_err);
		ack = 1;
	}

	/* PCI Interrupt? */
	if (irqreg & IRQ_INTX) {
		/* Call respective PCI Interrupt handler */
		for (i = 0; i < 4; i++) {
			if (irqreg & (1 << i))
				generic_handle_irq(priv->irq_map[i]);
		}
		ack = 1;
	}

	/*
	 * Call "first level" IRQ chip end-of-irq handler. It will ACK LEON IRQ
	 * Controller, this must be done after IRQ sources have been handled to
	 * avoid double IRQ generation
	 */
	if (ack)
		desc->irq_data.chip->irq_eoi(&desc->irq_data);
}

/* Create a virtual IRQ */
static unsigned int grpci1_build_device_irq(unsigned int irq)
{
	unsigned int virq = 0, pil;

	pil = 1 << 8;
	virq = irq_alloc(irq, pil);
	if (virq == 0)
		goto out;

	irq_set_chip_and_handler_name(virq, &grpci1_irq, handle_simple_irq,
				      "pcilvl");
	irq_set_chip_data(virq, (void *)irq);

out:
	return virq;
}

/*
 * Initialize mappings AMBA<->PCI, clear IRQ state, setup PCI interface
 *
 * Target BARs:
 *  BAR0: unused in this implementation
 *  BAR1: peripheral DMA to host's memory (size at least 256MByte)
 *  BAR2..BAR5: not implemented in hardware
 */
static void grpci1_hw_init(struct grpci1_priv *priv)
{
	u32 ahbadr, bar_sz, data, pciadr;
	struct grpci1_regs __iomem *regs = priv->regs;

	/* set 1:1 mapping between AHB -> PCI memory space */
	REGSTORE(regs->cfg_stat, priv->pci_area & 0xf0000000);

	/* map PCI accesses to target BAR1 to Linux kernel memory 1:1 */
	ahbadr = 0xf0000000 & (u32)__pa(PAGE_ALIGN((unsigned long) &_end));
	REGSTORE(regs->page1, ahbadr);

	/* translate I/O accesses to 0, I/O Space always @ PCI low 64Kbytes */
	REGSTORE(regs->iomap, REGLOAD(regs->iomap) & 0x0000ffff);

	/* disable and clear pending interrupts */
	REGSTORE(regs->irq, 0);

	/* Setup BAR0 outside access range so that it does not conflict with
	 * peripheral DMA. There is no need to set up the PAGE0 register.
	 */
	grpci1_cfg_w32(priv, TGT, 0, PCI_BASE_ADDRESS_0, 0xffffffff);
	grpci1_cfg_r32(priv, TGT, 0, PCI_BASE_ADDRESS_0, &bar_sz);
	bar_sz = ~bar_sz + 1;
	pciadr = priv->pci_area - bar_sz;
	grpci1_cfg_w32(priv, TGT, 0, PCI_BASE_ADDRESS_0, pciadr);

	/*
	 * Setup the Host's PCI Target BAR1 for other peripherals to access,
	 * and do DMA to the host's memory.
	 */
	grpci1_cfg_w32(priv, TGT, 0, PCI_BASE_ADDRESS_1, ahbadr);

	/*
	 * Setup Latency Timer and cache line size. Default cache line
	 * size will result in poor performance (256 word fetches), 0xff
	 * will set it according to the max size of the PCI FIFO.
	 */
	grpci1_cfg_w8(priv, TGT, 0, PCI_CACHE_LINE_SIZE, 0xff);
	grpci1_cfg_w8(priv, TGT, 0, PCI_LATENCY_TIMER, 0x40);

	/* set as bus master, enable pci memory responses, clear status bits */
	grpci1_cfg_r32(priv, TGT, 0, PCI_COMMAND, &data);
	data |= (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	grpci1_cfg_w32(priv, TGT, 0, PCI_COMMAND, data);
}

static irqreturn_t grpci1_jump_interrupt(int irq, void *arg)
{
	struct grpci1_priv *priv = arg;
	dev_err(priv->dev, "Jump IRQ happened\n");
	return IRQ_NONE;
}

/* Handle GRPCI1 Error Interrupt */
static irqreturn_t grpci1_err_interrupt(int irq, void *arg)
{
	struct grpci1_priv *priv = arg;
	u32 status;

	grpci1_cfg_r16(priv, TGT, 0, PCI_STATUS, &status);
	status &= priv->pci_err_mask;

	if (status == 0)
		return IRQ_NONE;

	if (status & PCI_STATUS_PARITY)
		dev_err(priv->dev, "Data Parity Error\n");

	if (status & PCI_STATUS_SIG_TARGET_ABORT)
		dev_err(priv->dev, "Signalled Target Abort\n");

	if (status & PCI_STATUS_REC_TARGET_ABORT)
		dev_err(priv->dev, "Received Target Abort\n");

	if (status & PCI_STATUS_REC_MASTER_ABORT)
		dev_err(priv->dev, "Received Master Abort\n");

	if (status & PCI_STATUS_SIG_SYSTEM_ERROR)
		dev_err(priv->dev, "Signalled System Error\n");

	if (status & PCI_STATUS_DETECTED_PARITY)
		dev_err(priv->dev, "Parity Error\n");

	/* Clear handled INT TYPE IRQs */
	grpci1_cfg_w16(priv, TGT, 0, PCI_STATUS, status);

	return IRQ_HANDLED;
}

static int grpci1_of_probe(struct platform_device *ofdev)
{
	struct grpci1_regs __iomem *regs;
	struct grpci1_priv *priv;
	int err, len;
	const int *tmp;
	u32 cfg, size, err_mask;
	struct resource *res;

	if (grpci1priv) {
		dev_err(&ofdev->dev, "only one GRPCI1 supported\n");
		return -ENODEV;
	}

	if (ofdev->num_resources < 3) {
		dev_err(&ofdev->dev, "not enough APB/AHB resources\n");
		return -EIO;
	}

	priv = devm_kzalloc(&ofdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&ofdev->dev, "memory allocation failed\n");
		return -ENOMEM;
	}
	platform_set_drvdata(ofdev, priv);
	priv->dev = &ofdev->dev;

	/* find device register base address */
	res = platform_get_resource(ofdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&ofdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	/*
	 * check that we're in Host Slot and that we can act as a Host Bridge
	 * and not only as target/peripheral.
	 */
	cfg = REGLOAD(regs->cfg_stat);
	if ((cfg & CFGSTAT_HOST) == 0) {
		dev_err(&ofdev->dev, "not in host system slot\n");
		return -EIO;
	}

	/* check that BAR1 support 256 MByte so that we can map kernel space */
	REGSTORE(regs->page1, 0xffffffff);
	size = ~REGLOAD(regs->page1) + 1;
	if (size < 0x10000000) {
		dev_err(&ofdev->dev, "BAR1 must be at least 256MByte\n");
		return -EIO;
	}

	/* hardware must support little-endian PCI (byte-twisting) */
	if ((REGLOAD(regs->page0) & PAGE0_BTEN) == 0) {
		dev_err(&ofdev->dev, "byte-twisting is required\n");
		return -EIO;
	}

	priv->regs = regs;
	priv->irq = irq_of_parse_and_map(ofdev->dev.of_node, 0);
	dev_info(&ofdev->dev, "host found at 0x%p, irq%d\n", regs, priv->irq);

	/* Find PCI Memory, I/O and Configuration Space Windows */
	priv->pci_area = ofdev->resource[1].start;
	priv->pci_area_end = ofdev->resource[1].end+1;
	priv->pci_io = ofdev->resource[2].start;
	priv->pci_conf = ofdev->resource[2].start + 0x10000;
	priv->pci_conf_end = priv->pci_conf + 0x10000;
	priv->pci_io_va = (unsigned long)ioremap(priv->pci_io, 0x10000);
	if (!priv->pci_io_va) {
		dev_err(&ofdev->dev, "unable to map PCI I/O area\n");
		return -EIO;
	}

	printk(KERN_INFO
		"GRPCI1: MEMORY SPACE [0x%08lx - 0x%08lx]\n"
		"        I/O    SPACE [0x%08lx - 0x%08lx]\n"
		"        CONFIG SPACE [0x%08lx - 0x%08lx]\n",
		priv->pci_area, priv->pci_area_end-1,
		priv->pci_io, priv->pci_conf-1,
		priv->pci_conf, priv->pci_conf_end-1);

	/*
	 * I/O Space resources in I/O Window mapped into Virtual Adr Space
	 * We never use low 4KB because some devices seem have problems using
	 * address 0.
	 */
	priv->info.io_space.name = "GRPCI1 PCI I/O Space";
	priv->info.io_space.start = priv->pci_io_va + 0x1000;
	priv->info.io_space.end = priv->pci_io_va + 0x10000 - 1;
	priv->info.io_space.flags = IORESOURCE_IO;

	/*
	 * grpci1 has no prefetchable memory, map everything as
	 * non-prefetchable memory
	 */
	priv->info.mem_space.name = "GRPCI1 PCI MEM Space";
	priv->info.mem_space.start = priv->pci_area;
	priv->info.mem_space.end = priv->pci_area_end - 1;
	priv->info.mem_space.flags = IORESOURCE_MEM;

	if (request_resource(&iomem_resource, &priv->info.mem_space) < 0) {
		dev_err(&ofdev->dev, "unable to request PCI memory area\n");
		err = -ENOMEM;
		goto err1;
	}

	if (request_resource(&ioport_resource, &priv->info.io_space) < 0) {
		dev_err(&ofdev->dev, "unable to request PCI I/O area\n");
		err = -ENOMEM;
		goto err2;
	}

	/* setup maximum supported PCI buses */
	priv->info.busn.name = "GRPCI1 busn";
	priv->info.busn.start = 0;
	priv->info.busn.end = 15;

	grpci1priv = priv;

	/* Initialize hardware */
	grpci1_hw_init(priv);

	/*
	 * Get PCI Interrupt to System IRQ mapping and setup IRQ handling
	 * Error IRQ. All PCI and PCI-Error interrupts are shared using the
	 * same system IRQ.
	 */
	leon_update_virq_handling(priv->irq, grpci1_pci_flow_irq, "pcilvl", 0);

	priv->irq_map[0] = grpci1_build_device_irq(1);
	priv->irq_map[1] = grpci1_build_device_irq(2);
	priv->irq_map[2] = grpci1_build_device_irq(3);
	priv->irq_map[3] = grpci1_build_device_irq(4);
	priv->irq_err = grpci1_build_device_irq(5);

	printk(KERN_INFO "        PCI INTA..D#: IRQ%d, IRQ%d, IRQ%d, IRQ%d\n",
		priv->irq_map[0], priv->irq_map[1], priv->irq_map[2],
		priv->irq_map[3]);

	/* Enable IRQs on LEON IRQ controller */
	err = devm_request_irq(&ofdev->dev, priv->irq, grpci1_jump_interrupt, 0,
				"GRPCI1_JUMP", priv);
	if (err) {
		dev_err(&ofdev->dev, "ERR IRQ request failed: %d\n", err);
		goto err3;
	}

	/* Setup IRQ handler for access errors */
	err = devm_request_irq(&ofdev->dev, priv->irq_err,
				grpci1_err_interrupt, IRQF_SHARED, "GRPCI1_ERR",
				priv);
	if (err) {
		dev_err(&ofdev->dev, "ERR VIRQ request failed: %d\n", err);
		goto err3;
	}

	tmp = of_get_property(ofdev->dev.of_node, "all_pci_errors", &len);
	if (tmp && (len == 4)) {
		priv->pci_err_mask = ALL_PCI_ERRORS;
		err_mask = IRQ_ALL_ERRORS << IRQ_MASK_BIT;
	} else {
		priv->pci_err_mask = DEF_PCI_ERRORS;
		err_mask = IRQ_DEF_ERRORS << IRQ_MASK_BIT;
	}

	/*
	 * Enable Error Interrupts. PCI interrupts are unmasked once request_irq
	 * is called by the PCI Device drivers
	 */
	REGSTORE(regs->irq, err_mask);

	/* Init common layer and scan buses */
	priv->info.ops = &grpci1_ops;
	priv->info.map_irq = grpci1_map_irq;
	leon_pci_init(ofdev, &priv->info);

	return 0;

err3:
	release_resource(&priv->info.io_space);
err2:
	release_resource(&priv->info.mem_space);
err1:
	iounmap((void __iomem *)priv->pci_io_va);
	grpci1priv = NULL;
	return err;
}

static const struct of_device_id grpci1_of_match[] __initconst = {
	{
	 .name = "GAISLER_PCIFBRG",
	 },
	{
	 .name = "01_014",
	 },
	{},
};

static struct platform_driver grpci1_of_driver = {
	.driver = {
		.name = "grpci1",
		.of_match_table = grpci1_of_match,
	},
	.probe = grpci1_of_probe,
};

static int __init grpci1_init(void)
{
	return platform_driver_register(&grpci1_of_driver);
}

subsys_initcall(grpci1_init);
