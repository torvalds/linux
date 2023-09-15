// SPDX-License-Identifier: GPL-2.0
/*
 * leon_pci_grpci2.c: GRPCI2 Host PCI driver
 *
 * Copyright (C) 2011 Aeroflex Gaisler AB, Daniel Hellstrom
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/leon.h>
#include <asm/vaddrs.h>
#include <asm/sections.h>
#include <asm/leon_pci.h>

#include "irq.h"

struct grpci2_barcfg {
	unsigned long pciadr;	/* PCI Space Address */
	unsigned long ahbadr;	/* PCI Base address mapped to this AHB addr */
};

/* Device Node Configuration options:
 *  - barcfgs    : Custom Configuration of Host's 6 target BARs
 *  - irq_mask   : Limit which PCI interrupts are enabled
 *  - do_reset   : Force PCI Reset on startup
 *
 * barcfgs
 * =======
 *
 * Optional custom Target BAR configuration (see struct grpci2_barcfg). All
 * addresses are physical. Array always contains 6 elements (len=2*4*6 bytes)
 *
 * -1 means not configured (let host driver do default setup).
 *
 * [i*2+0] = PCI Address of BAR[i] on target interface
 * [i*2+1] = Accessing PCI address of BAR[i] result in this AMBA address
 *
 *
 * irq_mask
 * ========
 *
 * Limit which PCI interrupts are enabled. 0=Disable, 1=Enable. By default
 * all are enabled. Use this when PCI interrupt pins are floating on PCB.
 * int, len=4.
 *  bit0 = PCI INTA#
 *  bit1 = PCI INTB#
 *  bit2 = PCI INTC#
 *  bit3 = PCI INTD#
 *
 *
 * reset
 * =====
 *
 * Force PCI reset on startup. int, len=4
 */

/* Enable Debugging Configuration Space Access */
#undef GRPCI2_DEBUG_CFGACCESS

/*
 * GRPCI2 APB Register MAP
 */
struct grpci2_regs {
	unsigned int ctrl;		/* 0x00 Control */
	unsigned int sts_cap;		/* 0x04 Status / Capabilities */
	int res1;			/* 0x08 */
	unsigned int io_map;		/* 0x0C I/O Map address */
	unsigned int dma_ctrl;		/* 0x10 DMA */
	unsigned int dma_bdbase;	/* 0x14 DMA */
	int res2[2];			/* 0x18 */
	unsigned int bars[6];		/* 0x20 read-only PCI BARs */
	int res3[2];			/* 0x38 */
	unsigned int ahbmst_map[16];	/* 0x40 AHB->PCI Map per AHB Master */

	/* PCI Trace Buffer Registers (OPTIONAL) */
	unsigned int t_ctrl;		/* 0x80 */
	unsigned int t_cnt;		/* 0x84 */
	unsigned int t_adpat;		/* 0x88 */
	unsigned int t_admask;		/* 0x8C */
	unsigned int t_sigpat;		/* 0x90 */
	unsigned int t_sigmask;		/* 0x94 */
	unsigned int t_adstate;		/* 0x98 */
	unsigned int t_sigstate;	/* 0x9C */
};

#define REGLOAD(a)	(be32_to_cpu(__raw_readl(&(a))))
#define REGSTORE(a, v)	(__raw_writel(cpu_to_be32(v), &(a)))

#define CTRL_BUS_BIT 16

#define CTRL_RESET (1<<31)
#define CTRL_SI (1<<27)
#define CTRL_PE (1<<26)
#define CTRL_EI (1<<25)
#define CTRL_ER (1<<24)
#define CTRL_BUS (0xff<<CTRL_BUS_BIT)
#define CTRL_HOSTINT 0xf

#define STS_HOST_BIT	31
#define STS_MST_BIT	30
#define STS_TAR_BIT	29
#define STS_DMA_BIT	28
#define STS_DI_BIT	27
#define STS_HI_BIT	26
#define STS_IRQMODE_BIT	24
#define STS_TRACE_BIT	23
#define STS_CFGERRVALID_BIT 20
#define STS_CFGERR_BIT	19
#define STS_INTTYPE_BIT	12
#define STS_INTSTS_BIT	8
#define STS_FDEPTH_BIT	2
#define STS_FNUM_BIT	0

#define STS_HOST	(1<<STS_HOST_BIT)
#define STS_MST		(1<<STS_MST_BIT)
#define STS_TAR		(1<<STS_TAR_BIT)
#define STS_DMA		(1<<STS_DMA_BIT)
#define STS_DI		(1<<STS_DI_BIT)
#define STS_HI		(1<<STS_HI_BIT)
#define STS_IRQMODE	(0x3<<STS_IRQMODE_BIT)
#define STS_TRACE	(1<<STS_TRACE_BIT)
#define STS_CFGERRVALID	(1<<STS_CFGERRVALID_BIT)
#define STS_CFGERR	(1<<STS_CFGERR_BIT)
#define STS_INTTYPE	(0x3f<<STS_INTTYPE_BIT)
#define STS_INTSTS	(0xf<<STS_INTSTS_BIT)
#define STS_FDEPTH	(0x7<<STS_FDEPTH_BIT)
#define STS_FNUM	(0x3<<STS_FNUM_BIT)

#define STS_ISYSERR	(1<<17)
#define STS_IDMA	(1<<16)
#define STS_IDMAERR	(1<<15)
#define STS_IMSTABRT	(1<<14)
#define STS_ITGTABRT	(1<<13)
#define STS_IPARERR	(1<<12)

#define STS_ERR_IRQ (STS_ISYSERR | STS_IMSTABRT | STS_ITGTABRT | STS_IPARERR)

struct grpci2_bd_chan {
	unsigned int ctrl;	/* 0x00 DMA Control */
	unsigned int nchan;	/* 0x04 Next DMA Channel Address */
	unsigned int nbd;	/* 0x08 Next Data Descriptor in chan */
	unsigned int res;	/* 0x0C Reserved */
};

#define BD_CHAN_EN		0x80000000
#define BD_CHAN_TYPE		0x00300000
#define BD_CHAN_BDCNT		0x0000ffff
#define BD_CHAN_EN_BIT		31
#define BD_CHAN_TYPE_BIT	20
#define BD_CHAN_BDCNT_BIT	0

struct grpci2_bd_data {
	unsigned int ctrl;	/* 0x00 DMA Data Control */
	unsigned int pci_adr;	/* 0x04 PCI Start Address */
	unsigned int ahb_adr;	/* 0x08 AHB Start address */
	unsigned int next;	/* 0x0C Next Data Descriptor in chan */
};

#define BD_DATA_EN		0x80000000
#define BD_DATA_IE		0x40000000
#define BD_DATA_DR		0x20000000
#define BD_DATA_TYPE		0x00300000
#define BD_DATA_ER		0x00080000
#define BD_DATA_LEN		0x0000ffff
#define BD_DATA_EN_BIT		31
#define BD_DATA_IE_BIT		30
#define BD_DATA_DR_BIT		29
#define BD_DATA_TYPE_BIT	20
#define BD_DATA_ER_BIT		19
#define BD_DATA_LEN_BIT		0

/* GRPCI2 Capability */
struct grpci2_cap_first {
	unsigned int ctrl;
	unsigned int pci2ahb_map[6];
	unsigned int ext2ahb_map;
	unsigned int io_map;
	unsigned int pcibar_size[6];
};
#define CAP9_CTRL_OFS 0
#define CAP9_BAR_OFS 0x4
#define CAP9_IOMAP_OFS 0x20
#define CAP9_BARSIZE_OFS 0x24

#define TGT 256

struct grpci2_priv {
	struct leon_pci_info	info; /* must be on top of this structure */
	struct grpci2_regs __iomem *regs;
	char			irq;
	char			irq_mode; /* IRQ Mode from CAPSTS REG */
	char			bt_enabled;
	char			do_reset;
	char			irq_mask;
	u32			pciid; /* PCI ID of Host */
	unsigned char		irq_map[4];

	/* Virtual IRQ numbers */
	unsigned int		virq_err;
	unsigned int		virq_dma;

	/* AHB PCI Windows */
	unsigned long		pci_area;	/* MEMORY */
	unsigned long		pci_area_end;
	unsigned long		pci_io;		/* I/O */
	unsigned long		pci_conf;	/* CONFIGURATION */
	unsigned long		pci_conf_end;
	unsigned long		pci_io_va;

	struct grpci2_barcfg	tgtbars[6];
};

static DEFINE_SPINLOCK(grpci2_dev_lock);
static struct grpci2_priv *grpci2priv;

static int grpci2_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct grpci2_priv *priv = dev->bus->sysdata;
	int irq_group;

	/* Use default IRQ decoding on PCI BUS0 according slot numbering */
	irq_group = slot & 0x3;
	pin = ((pin - 1) + irq_group) & 0x3;

	return priv->irq_map[pin];
}

static int grpci2_cfg_r32(struct grpci2_priv *priv, unsigned int bus,
				unsigned int devfn, int where, u32 *val)
{
	unsigned int *pci_conf;
	unsigned long flags;
	u32 tmp;

	if (where & 0x3)
		return -EINVAL;

	if (bus == 0) {
		devfn += (0x8 * 6); /* start at AD16=Device0 */
	} else if (bus == TGT) {
		bus = 0;
		devfn = 0; /* special case: bridge controller itself */
	}

	/* Select bus */
	spin_lock_irqsave(&grpci2_dev_lock, flags);
	REGSTORE(priv->regs->ctrl, (REGLOAD(priv->regs->ctrl) & ~(0xff << 16)) |
				   (bus << 16));
	spin_unlock_irqrestore(&grpci2_dev_lock, flags);

	/* clear old status */
	REGSTORE(priv->regs->sts_cap, (STS_CFGERR | STS_CFGERRVALID));

	pci_conf = (unsigned int *) (priv->pci_conf |
						(devfn << 8) | (where & 0xfc));
	tmp = LEON3_BYPASS_LOAD_PA(pci_conf);

	/* Wait until GRPCI2 signals that CFG access is done, it should be
	 * done instantaneously unless a DMA operation is ongoing...
	 */
	while ((REGLOAD(priv->regs->sts_cap) & STS_CFGERRVALID) == 0)
		;

	if (REGLOAD(priv->regs->sts_cap) & STS_CFGERR) {
		*val = 0xffffffff;
	} else {
		/* Bus always little endian (unaffected by byte-swapping) */
		*val = swab32(tmp);
	}

	return 0;
}

static int grpci2_cfg_r16(struct grpci2_priv *priv, unsigned int bus,
				unsigned int devfn, int where, u32 *val)
{
	u32 v;
	int ret;

	if (where & 0x1)
		return -EINVAL;
	ret = grpci2_cfg_r32(priv, bus, devfn, where & ~0x3, &v);
	*val = 0xffff & (v >> (8 * (where & 0x3)));
	return ret;
}

static int grpci2_cfg_r8(struct grpci2_priv *priv, unsigned int bus,
				unsigned int devfn, int where, u32 *val)
{
	u32 v;
	int ret;

	ret = grpci2_cfg_r32(priv, bus, devfn, where & ~0x3, &v);
	*val = 0xff & (v >> (8 * (where & 3)));

	return ret;
}

static int grpci2_cfg_w32(struct grpci2_priv *priv, unsigned int bus,
				unsigned int devfn, int where, u32 val)
{
	unsigned int *pci_conf;
	unsigned long flags;

	if (where & 0x3)
		return -EINVAL;

	if (bus == 0) {
		devfn += (0x8 * 6); /* start at AD16=Device0 */
	} else if (bus == TGT) {
		bus = 0;
		devfn = 0; /* special case: bridge controller itself */
	}

	/* Select bus */
	spin_lock_irqsave(&grpci2_dev_lock, flags);
	REGSTORE(priv->regs->ctrl, (REGLOAD(priv->regs->ctrl) & ~(0xff << 16)) |
				   (bus << 16));
	spin_unlock_irqrestore(&grpci2_dev_lock, flags);

	/* clear old status */
	REGSTORE(priv->regs->sts_cap, (STS_CFGERR | STS_CFGERRVALID));

	pci_conf = (unsigned int *) (priv->pci_conf |
						(devfn << 8) | (where & 0xfc));
	LEON3_BYPASS_STORE_PA(pci_conf, swab32(val));

	/* Wait until GRPCI2 signals that CFG access is done, it should be
	 * done instantaneously unless a DMA operation is ongoing...
	 */
	while ((REGLOAD(priv->regs->sts_cap) & STS_CFGERRVALID) == 0)
		;

	return 0;
}

static int grpci2_cfg_w16(struct grpci2_priv *priv, unsigned int bus,
				unsigned int devfn, int where, u32 val)
{
	int ret;
	u32 v;

	if (where & 0x1)
		return -EINVAL;
	ret = grpci2_cfg_r32(priv, bus, devfn, where&~3, &v);
	if (ret)
		return ret;
	v = (v & ~(0xffff << (8 * (where & 0x3)))) |
	    ((0xffff & val) << (8 * (where & 0x3)));
	return grpci2_cfg_w32(priv, bus, devfn, where & ~0x3, v);
}

static int grpci2_cfg_w8(struct grpci2_priv *priv, unsigned int bus,
				unsigned int devfn, int where, u32 val)
{
	int ret;
	u32 v;

	ret = grpci2_cfg_r32(priv, bus, devfn, where & ~0x3, &v);
	if (ret != 0)
		return ret;
	v = (v & ~(0xff << (8 * (where & 0x3)))) |
	    ((0xff & val) << (8 * (where & 0x3)));
	return grpci2_cfg_w32(priv, bus, devfn, where & ~0x3, v);
}

/* Read from Configuration Space. When entering here the PCI layer has taken
 * the pci_lock spinlock and IRQ is off.
 */
static int grpci2_read_config(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 *val)
{
	struct grpci2_priv *priv = grpci2priv;
	unsigned int busno = bus->number;
	int ret;

	if (PCI_SLOT(devfn) > 15 || busno > 255) {
		*val = ~0;
		return 0;
	}

	switch (size) {
	case 1:
		ret = grpci2_cfg_r8(priv, busno, devfn, where, val);
		break;
	case 2:
		ret = grpci2_cfg_r16(priv, busno, devfn, where, val);
		break;
	case 4:
		ret = grpci2_cfg_r32(priv, busno, devfn, where, val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

#ifdef GRPCI2_DEBUG_CFGACCESS
	printk(KERN_INFO "grpci2_read_config: [%02x:%02x:%x] ofs=%d val=%x "
		"size=%d\n", busno, PCI_SLOT(devfn), PCI_FUNC(devfn), where,
		*val, size);
#endif

	return ret;
}

/* Write to Configuration Space. When entering here the PCI layer has taken
 * the pci_lock spinlock and IRQ is off.
 */
static int grpci2_write_config(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, u32 val)
{
	struct grpci2_priv *priv = grpci2priv;
	unsigned int busno = bus->number;

	if (PCI_SLOT(devfn) > 15 || busno > 255)
		return 0;

#ifdef GRPCI2_DEBUG_CFGACCESS
	printk(KERN_INFO "grpci2_write_config: [%02x:%02x:%x] ofs=%d size=%d "
		"val=%x\n", busno, PCI_SLOT(devfn), PCI_FUNC(devfn),
		where, size, val);
#endif

	switch (size) {
	default:
		return -EINVAL;
	case 1:
		return grpci2_cfg_w8(priv, busno, devfn, where, val);
	case 2:
		return grpci2_cfg_w16(priv, busno, devfn, where, val);
	case 4:
		return grpci2_cfg_w32(priv, busno, devfn, where, val);
	}
}

static struct pci_ops grpci2_ops = {
	.read =		grpci2_read_config,
	.write =	grpci2_write_config,
};

/* GENIRQ IRQ chip implementation for GRPCI2 irqmode=0..2. In configuration
 * 3 where all PCI Interrupts has a separate IRQ on the system IRQ controller
 * this is not needed and the standard IRQ controller can be used.
 */

static void grpci2_mask_irq(struct irq_data *data)
{
	unsigned long flags;
	unsigned int irqidx;
	struct grpci2_priv *priv = grpci2priv;

	irqidx = (unsigned int)data->chip_data - 1;
	if (irqidx > 3) /* only mask PCI interrupts here */
		return;

	spin_lock_irqsave(&grpci2_dev_lock, flags);
	REGSTORE(priv->regs->ctrl, REGLOAD(priv->regs->ctrl) & ~(1 << irqidx));
	spin_unlock_irqrestore(&grpci2_dev_lock, flags);
}

static void grpci2_unmask_irq(struct irq_data *data)
{
	unsigned long flags;
	unsigned int irqidx;
	struct grpci2_priv *priv = grpci2priv;

	irqidx = (unsigned int)data->chip_data - 1;
	if (irqidx > 3) /* only unmask PCI interrupts here */
		return;

	spin_lock_irqsave(&grpci2_dev_lock, flags);
	REGSTORE(priv->regs->ctrl, REGLOAD(priv->regs->ctrl) | (1 << irqidx));
	spin_unlock_irqrestore(&grpci2_dev_lock, flags);
}

static unsigned int grpci2_startup_irq(struct irq_data *data)
{
	grpci2_unmask_irq(data);
	return 0;
}

static void grpci2_shutdown_irq(struct irq_data *data)
{
	grpci2_mask_irq(data);
}

static struct irq_chip grpci2_irq = {
	.name		= "grpci2",
	.irq_startup	= grpci2_startup_irq,
	.irq_shutdown	= grpci2_shutdown_irq,
	.irq_mask	= grpci2_mask_irq,
	.irq_unmask	= grpci2_unmask_irq,
};

/* Handle one or multiple IRQs from the PCI core */
static void grpci2_pci_flow_irq(struct irq_desc *desc)
{
	struct grpci2_priv *priv = grpci2priv;
	int i, ack = 0;
	unsigned int ctrl, sts_cap, pci_ints;

	ctrl = REGLOAD(priv->regs->ctrl);
	sts_cap = REGLOAD(priv->regs->sts_cap);

	/* Error Interrupt? */
	if (sts_cap & STS_ERR_IRQ) {
		generic_handle_irq(priv->virq_err);
		ack = 1;
	}

	/* PCI Interrupt? */
	pci_ints = ((~sts_cap) >> STS_INTSTS_BIT) & ctrl & CTRL_HOSTINT;
	if (pci_ints) {
		/* Call respective PCI Interrupt handler */
		for (i = 0; i < 4; i++) {
			if (pci_ints & (1 << i))
				generic_handle_irq(priv->irq_map[i]);
		}
		ack = 1;
	}

	/*
	 * Decode DMA Interrupt only when shared with Err and PCI INTX#, when
	 * the DMA is a unique IRQ the DMA interrupts doesn't end up here, they
	 * goes directly to DMA ISR.
	 */
	if ((priv->irq_mode == 0) && (sts_cap & (STS_IDMA | STS_IDMAERR))) {
		generic_handle_irq(priv->virq_dma);
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
static unsigned int grpci2_build_device_irq(unsigned int irq)
{
	unsigned int virq = 0, pil;

	pil = 1 << 8;
	virq = irq_alloc(irq, pil);
	if (virq == 0)
		goto out;

	irq_set_chip_and_handler_name(virq, &grpci2_irq, handle_simple_irq,
				      "pcilvl");
	irq_set_chip_data(virq, (void *)irq);

out:
	return virq;
}

static void grpci2_hw_init(struct grpci2_priv *priv)
{
	u32 ahbadr, pciadr, bar_sz, capptr, io_map, data;
	struct grpci2_regs __iomem *regs = priv->regs;
	int i;
	struct grpci2_barcfg *barcfg = priv->tgtbars;

	/* Reset any earlier setup */
	if (priv->do_reset) {
		printk(KERN_INFO "GRPCI2: Resetting PCI bus\n");
		REGSTORE(regs->ctrl, CTRL_RESET);
		ssleep(1); /* Wait for boards to settle */
	}
	REGSTORE(regs->ctrl, 0);
	REGSTORE(regs->sts_cap, ~0); /* Clear Status */
	REGSTORE(regs->dma_ctrl, 0);
	REGSTORE(regs->dma_bdbase, 0);

	/* Translate I/O accesses to 0, I/O Space always @ PCI low 64Kbytes */
	REGSTORE(regs->io_map, REGLOAD(regs->io_map) & 0x0000ffff);

	/* set 1:1 mapping between AHB -> PCI memory space, for all Masters
	 * Each AHB master has it's own mapping registers. Max 16 AHB masters.
	 */
	for (i = 0; i < 16; i++)
		REGSTORE(regs->ahbmst_map[i], priv->pci_area);

	/* Get the GRPCI2 Host PCI ID */
	grpci2_cfg_r32(priv, TGT, 0, PCI_VENDOR_ID, &priv->pciid);

	/* Get address to first (always defined) capability structure */
	grpci2_cfg_r8(priv, TGT, 0, PCI_CAPABILITY_LIST, &capptr);

	/* Enable/Disable Byte twisting */
	grpci2_cfg_r32(priv, TGT, 0, capptr+CAP9_IOMAP_OFS, &io_map);
	io_map = (io_map & ~0x1) | (priv->bt_enabled ? 1 : 0);
	grpci2_cfg_w32(priv, TGT, 0, capptr+CAP9_IOMAP_OFS, io_map);

	/* Setup the Host's PCI Target BARs for other peripherals to access,
	 * and do DMA to the host's memory. The target BARs can be sized and
	 * enabled individually.
	 *
	 * User may set custom target BARs, but default is:
	 * The first BARs is used to map kernel low (DMA is part of normal
	 * region on sparc which is SRMMU_MAXMEM big) main memory 1:1 to the
	 * PCI bus, the other BARs are disabled. We assume that the first BAR
	 * is always available.
	 */
	for (i = 0; i < 6; i++) {
		if (barcfg[i].pciadr != ~0 && barcfg[i].ahbadr != ~0) {
			/* Target BARs must have the proper alignment */
			ahbadr = barcfg[i].ahbadr;
			pciadr = barcfg[i].pciadr;
			bar_sz = ((pciadr - 1) & ~pciadr) + 1;
		} else {
			if (i == 0) {
				/* Map main memory */
				bar_sz = 0xf0000008; /* 256MB prefetchable */
				ahbadr = 0xf0000000 & (u32)__pa(PAGE_ALIGN(
					(unsigned long) &_end));
				pciadr = ahbadr;
			} else {
				bar_sz = 0;
				ahbadr = 0;
				pciadr = 0;
			}
		}
		grpci2_cfg_w32(priv, TGT, 0, capptr+CAP9_BARSIZE_OFS+i*4,
				bar_sz);
		grpci2_cfg_w32(priv, TGT, 0, PCI_BASE_ADDRESS_0+i*4, pciadr);
		grpci2_cfg_w32(priv, TGT, 0, capptr+CAP9_BAR_OFS+i*4, ahbadr);
		printk(KERN_INFO "        TGT BAR[%d]: 0x%08x (PCI)-> 0x%08x\n",
			i, pciadr, ahbadr);
	}

	/* set as bus master and enable pci memory responses */
	grpci2_cfg_r32(priv, TGT, 0, PCI_COMMAND, &data);
	data |= (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	grpci2_cfg_w32(priv, TGT, 0, PCI_COMMAND, data);

	/* Enable Error respone (CPU-TRAP) on illegal memory access. */
	REGSTORE(regs->ctrl, CTRL_ER | CTRL_PE);
}

static irqreturn_t grpci2_jump_interrupt(int irq, void *arg)
{
	printk(KERN_ERR "GRPCI2: Jump IRQ happened\n");
	return IRQ_NONE;
}

/* Handle GRPCI2 Error Interrupt */
static irqreturn_t grpci2_err_interrupt(int irq, void *arg)
{
	struct grpci2_priv *priv = arg;
	struct grpci2_regs __iomem *regs = priv->regs;
	unsigned int status;

	status = REGLOAD(regs->sts_cap);
	if ((status & STS_ERR_IRQ) == 0)
		return IRQ_NONE;

	if (status & STS_IPARERR)
		printk(KERN_ERR "GRPCI2: Parity Error\n");

	if (status & STS_ITGTABRT)
		printk(KERN_ERR "GRPCI2: Target Abort\n");

	if (status & STS_IMSTABRT)
		printk(KERN_ERR "GRPCI2: Master Abort\n");

	if (status & STS_ISYSERR)
		printk(KERN_ERR "GRPCI2: System Error\n");

	/* Clear handled INT TYPE IRQs */
	REGSTORE(regs->sts_cap, status & STS_ERR_IRQ);

	return IRQ_HANDLED;
}

static int grpci2_of_probe(struct platform_device *ofdev)
{
	struct grpci2_regs __iomem *regs;
	struct grpci2_priv *priv;
	int err, i, len;
	const int *tmp;
	unsigned int capability;

	if (grpci2priv) {
		printk(KERN_ERR "GRPCI2: only one GRPCI2 core supported\n");
		return -ENODEV;
	}

	if (ofdev->num_resources < 3) {
		printk(KERN_ERR "GRPCI2: not enough APB/AHB resources\n");
		return -EIO;
	}

	/* Find Device Address */
	regs = of_ioremap(&ofdev->resource[0], 0,
			  resource_size(&ofdev->resource[0]),
			  "grlib-grpci2 regs");
	if (regs == NULL) {
		printk(KERN_ERR "GRPCI2: ioremap failed\n");
		return -EIO;
	}

	/*
	 * Check that we're in Host Slot and that we can act as a Host Bridge
	 * and not only as target.
	 */
	capability = REGLOAD(regs->sts_cap);
	if ((capability & STS_HOST) || !(capability & STS_MST)) {
		printk(KERN_INFO "GRPCI2: not in host system slot\n");
		err = -EIO;
		goto err1;
	}

	priv = grpci2priv = kzalloc(sizeof(struct grpci2_priv), GFP_KERNEL);
	if (grpci2priv == NULL) {
		err = -ENOMEM;
		goto err1;
	}
	priv->regs = regs;
	priv->irq = ofdev->archdata.irqs[0]; /* BASE IRQ */
	priv->irq_mode = (capability & STS_IRQMODE) >> STS_IRQMODE_BIT;

	printk(KERN_INFO "GRPCI2: host found at %p, irq%d\n", regs, priv->irq);

	/* Byte twisting should be made configurable from kernel command line */
	priv->bt_enabled = 1;

	/* Let user do custom Target BAR assignment */
	tmp = of_get_property(ofdev->dev.of_node, "barcfg", &len);
	if (tmp && (len == 2*4*6))
		memcpy(priv->tgtbars, tmp, 2*4*6);
	else
		memset(priv->tgtbars, -1, 2*4*6);

	/* Limit IRQ unmasking in irq_mode 2 and 3 */
	tmp = of_get_property(ofdev->dev.of_node, "irq_mask", &len);
	if (tmp && (len == 4))
		priv->do_reset = *tmp;
	else
		priv->irq_mask = 0xf;

	/* Optional PCI reset. Force PCI reset on startup */
	tmp = of_get_property(ofdev->dev.of_node, "reset", &len);
	if (tmp && (len == 4))
		priv->do_reset = *tmp;
	else
		priv->do_reset = 0;

	/* Find PCI Memory, I/O and Configuration Space Windows */
	priv->pci_area = ofdev->resource[1].start;
	priv->pci_area_end = ofdev->resource[1].end+1;
	priv->pci_io = ofdev->resource[2].start;
	priv->pci_conf = ofdev->resource[2].start + 0x10000;
	priv->pci_conf_end = priv->pci_conf + 0x10000;
	priv->pci_io_va = (unsigned long)ioremap(priv->pci_io, 0x10000);
	if (!priv->pci_io_va) {
		err = -EIO;
		goto err2;
	}

	printk(KERN_INFO
		"GRPCI2: MEMORY SPACE [0x%08lx - 0x%08lx]\n"
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
	memset(&priv->info.io_space, 0, sizeof(struct resource));
	priv->info.io_space.name = "GRPCI2 PCI I/O Space";
	priv->info.io_space.start = priv->pci_io_va + 0x1000;
	priv->info.io_space.end = priv->pci_io_va + 0x10000 - 1;
	priv->info.io_space.flags = IORESOURCE_IO;

	/*
	 * GRPCI2 has no prefetchable memory, map everything as
	 * non-prefetchable memory
	 */
	memset(&priv->info.mem_space, 0, sizeof(struct resource));
	priv->info.mem_space.name = "GRPCI2 PCI MEM Space";
	priv->info.mem_space.start = priv->pci_area;
	priv->info.mem_space.end = priv->pci_area_end - 1;
	priv->info.mem_space.flags = IORESOURCE_MEM;

	if (request_resource(&iomem_resource, &priv->info.mem_space) < 0)
		goto err3;
	if (request_resource(&ioport_resource, &priv->info.io_space) < 0)
		goto err4;

	/* setup maximum supported PCI buses */
	priv->info.busn.name = "GRPCI2 busn";
	priv->info.busn.start = 0;
	priv->info.busn.end = 255;

	grpci2_hw_init(priv);

	/*
	 * Get PCI Interrupt to System IRQ mapping and setup IRQ handling
	 * Error IRQ always on PCI INTA.
	 */
	if (priv->irq_mode < 2) {
		/* All PCI interrupts are shared using the same system IRQ */
		leon_update_virq_handling(priv->irq, grpci2_pci_flow_irq,
					 "pcilvl", 0);

		priv->irq_map[0] = grpci2_build_device_irq(1);
		priv->irq_map[1] = grpci2_build_device_irq(2);
		priv->irq_map[2] = grpci2_build_device_irq(3);
		priv->irq_map[3] = grpci2_build_device_irq(4);

		priv->virq_err = grpci2_build_device_irq(5);
		if (priv->irq_mode & 1)
			priv->virq_dma = ofdev->archdata.irqs[1];
		else
			priv->virq_dma = grpci2_build_device_irq(6);

		/* Enable IRQs on LEON IRQ controller */
		err = request_irq(priv->irq, grpci2_jump_interrupt, 0,
					"GRPCI2_JUMP", priv);
		if (err)
			printk(KERN_ERR "GRPCI2: ERR IRQ request failed\n");
	} else {
		/* All PCI interrupts have an unique IRQ interrupt */
		for (i = 0; i < 4; i++) {
			/* Make LEON IRQ layer handle level IRQ by acking */
			leon_update_virq_handling(ofdev->archdata.irqs[i],
						 handle_fasteoi_irq, "pcilvl",
						 1);
			priv->irq_map[i] = ofdev->archdata.irqs[i];
		}
		priv->virq_err = priv->irq_map[0];
		if (priv->irq_mode & 1)
			priv->virq_dma = ofdev->archdata.irqs[4];
		else
			priv->virq_dma = priv->irq_map[0];

		/* Unmask all PCI interrupts, request_irq will not do that */
		REGSTORE(regs->ctrl, REGLOAD(regs->ctrl)|(priv->irq_mask&0xf));
	}

	/* Setup IRQ handler for non-configuration space access errors */
	err = request_irq(priv->virq_err, grpci2_err_interrupt, IRQF_SHARED,
				"GRPCI2_ERR", priv);
	if (err) {
		printk(KERN_DEBUG "GRPCI2: ERR VIRQ request failed: %d\n", err);
		goto err5;
	}

	/*
	 * Enable Error Interrupts. PCI interrupts are unmasked once request_irq
	 * is called by the PCI Device drivers
	 */
	REGSTORE(regs->ctrl, REGLOAD(regs->ctrl) | CTRL_EI | CTRL_SI);

	/* Init common layer and scan buses */
	priv->info.ops = &grpci2_ops;
	priv->info.map_irq = grpci2_map_irq;
	leon_pci_init(ofdev, &priv->info);

	return 0;

err5:
	release_resource(&priv->info.io_space);
err4:
	release_resource(&priv->info.mem_space);
err3:
	err = -ENOMEM;
	iounmap((void __iomem *)priv->pci_io_va);
err2:
	kfree(priv);
err1:
	of_iounmap(&ofdev->resource[0], regs,
		resource_size(&ofdev->resource[0]));
	return err;
}

static const struct of_device_id grpci2_of_match[] __initconst = {
	{
	 .name = "GAISLER_GRPCI2",
	 },
	{
	 .name = "01_07c",
	 },
	{},
};

static struct platform_driver grpci2_of_driver = {
	.driver = {
		.name = "grpci2",
		.of_match_table = grpci2_of_match,
	},
	.probe = grpci2_of_probe,
};

static int __init grpci2_init(void)
{
	return platform_driver_register(&grpci2_of_driver);
}

subsys_initcall(grpci2_init);
