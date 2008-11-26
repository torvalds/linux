/*
 * PCI code for the Freescale MPC52xx embedded CPU.
 *
 * Copyright (C) 2006 Secret Lab Technologies Ltd.
 *                        Grant Likely <grant.likely@secretlab.ca>
 * Copyright (C) 2004 Sylvain Munaut <tnt@246tNt.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#undef DEBUG

#include <asm/pci.h>
#include <asm/mpc52xx.h>
#include <asm/delay.h>
#include <asm/machdep.h>
#include <linux/kernel.h>


/* ======================================================================== */
/* PCI windows config                                                       */
/* ======================================================================== */

#define MPC52xx_PCI_TARGET_IO	0xf0000000
#define MPC52xx_PCI_TARGET_MEM	0x00000000


/* ======================================================================== */
/* Structures mapping & Defines for PCI Unit                                */
/* ======================================================================== */

#define MPC52xx_PCI_GSCR_BM		0x40000000
#define MPC52xx_PCI_GSCR_PE		0x20000000
#define MPC52xx_PCI_GSCR_SE		0x10000000
#define MPC52xx_PCI_GSCR_XLB2PCI_MASK	0x07000000
#define MPC52xx_PCI_GSCR_XLB2PCI_SHIFT	24
#define MPC52xx_PCI_GSCR_IPG2PCI_MASK	0x00070000
#define MPC52xx_PCI_GSCR_IPG2PCI_SHIFT	16
#define MPC52xx_PCI_GSCR_BME		0x00004000
#define MPC52xx_PCI_GSCR_PEE		0x00002000
#define MPC52xx_PCI_GSCR_SEE		0x00001000
#define MPC52xx_PCI_GSCR_PR		0x00000001


#define MPC52xx_PCI_IWBTAR_TRANSLATION(proc_ad,pci_ad,size)	  \
		( ( (proc_ad) & 0xff000000 )			| \
		  ( (((size) - 1) >> 8) & 0x00ff0000 )		| \
		  ( ((pci_ad) >> 16) & 0x0000ff00 ) )

#define MPC52xx_PCI_IWCR_PACK(win0,win1,win2)	(((win0) << 24) | \
						 ((win1) << 16) | \
						 ((win2) <<  8))

#define MPC52xx_PCI_IWCR_DISABLE	0x0
#define MPC52xx_PCI_IWCR_ENABLE		0x1
#define MPC52xx_PCI_IWCR_READ		0x0
#define MPC52xx_PCI_IWCR_READ_LINE	0x2
#define MPC52xx_PCI_IWCR_READ_MULTI	0x4
#define MPC52xx_PCI_IWCR_MEM		0x0
#define MPC52xx_PCI_IWCR_IO		0x8

#define MPC52xx_PCI_TCR_P		0x01000000
#define MPC52xx_PCI_TCR_LD		0x00010000
#define MPC52xx_PCI_TCR_WCT8		0x00000008

#define MPC52xx_PCI_TBATR_DISABLE	0x0
#define MPC52xx_PCI_TBATR_ENABLE	0x1

struct mpc52xx_pci {
	u32	idr;		/* PCI + 0x00 */
	u32	scr;		/* PCI + 0x04 */
	u32	ccrir;		/* PCI + 0x08 */
	u32	cr1;		/* PCI + 0x0C */
	u32	bar0;		/* PCI + 0x10 */
	u32	bar1;		/* PCI + 0x14 */
	u8	reserved1[16];	/* PCI + 0x18 */
	u32	ccpr;		/* PCI + 0x28 */
	u32	sid;		/* PCI + 0x2C */
	u32	erbar;		/* PCI + 0x30 */
	u32	cpr;		/* PCI + 0x34 */
	u8	reserved2[4];	/* PCI + 0x38 */
	u32	cr2;		/* PCI + 0x3C */
	u8	reserved3[32];	/* PCI + 0x40 */
	u32	gscr;		/* PCI + 0x60 */
	u32	tbatr0;		/* PCI + 0x64 */
	u32	tbatr1;		/* PCI + 0x68 */
	u32	tcr;		/* PCI + 0x6C */
	u32	iw0btar;	/* PCI + 0x70 */
	u32	iw1btar;	/* PCI + 0x74 */
	u32	iw2btar;	/* PCI + 0x78 */
	u8	reserved4[4];	/* PCI + 0x7C */
	u32	iwcr;		/* PCI + 0x80 */
	u32	icr;		/* PCI + 0x84 */
	u32	isr;		/* PCI + 0x88 */
	u32	arb;		/* PCI + 0x8C */
	u8	reserved5[104];	/* PCI + 0x90 */
	u32	car;		/* PCI + 0xF8 */
	u8	reserved6[4];	/* PCI + 0xFC */
};

/* MPC5200 device tree match tables */
const struct of_device_id mpc52xx_pci_ids[] __initdata = {
	{ .type = "pci", .compatible = "fsl,mpc5200-pci", },
	{ .type = "pci", .compatible = "mpc5200-pci", },
	{}
};

/* ======================================================================== */
/* PCI configuration acess                                                  */
/* ======================================================================== */

static int
mpc52xx_pci_read_config(struct pci_bus *bus, unsigned int devfn,
				int offset, int len, u32 *val)
{
	struct pci_controller *hose = bus->sysdata;
	u32 value;

	if (ppc_md.pci_exclude_device)
		if (ppc_md.pci_exclude_device(hose, bus->number, devfn))
			return PCIBIOS_DEVICE_NOT_FOUND;

	out_be32(hose->cfg_addr,
		(1 << 31) |
		(bus->number << 16) |
		(devfn << 8) |
		(offset & 0xfc));
	mb();

#if defined(CONFIG_PPC_MPC5200_BUGFIX)
	if (bus->number) {
		/* workaround for the bug 435 of the MPC5200 (L25R);
		 * Don't do 32 bits config access during type-1 cycles */
		switch (len) {
		      case 1:
			value = in_8(((u8 __iomem *)hose->cfg_data) +
			             (offset & 3));
			break;
		      case 2:
			value = in_le16(((u16 __iomem *)hose->cfg_data) +
			                ((offset>>1) & 1));
			break;

		      default:
			value = in_le16((u16 __iomem *)hose->cfg_data) |
				(in_le16(((u16 __iomem *)hose->cfg_data) + 1) << 16);
			break;
		}
	}
	else
#endif
	{
		value = in_le32(hose->cfg_data);

		if (len != 4) {
			value >>= ((offset & 0x3) << 3);
			value &= 0xffffffff >> (32 - (len << 3));
		}
	}

	*val = value;

	out_be32(hose->cfg_addr, 0);
	mb();

	return PCIBIOS_SUCCESSFUL;
}

static int
mpc52xx_pci_write_config(struct pci_bus *bus, unsigned int devfn,
				int offset, int len, u32 val)
{
	struct pci_controller *hose = bus->sysdata;
	u32 value, mask;

	if (ppc_md.pci_exclude_device)
		if (ppc_md.pci_exclude_device(hose, bus->number, devfn))
			return PCIBIOS_DEVICE_NOT_FOUND;

	out_be32(hose->cfg_addr,
		(1 << 31) |
		(bus->number << 16) |
		(devfn << 8) |
		(offset & 0xfc));
	mb();

#if defined(CONFIG_PPC_MPC5200_BUGFIX)
	if (bus->number) {
		/* workaround for the bug 435 of the MPC5200 (L25R);
		 * Don't do 32 bits config access during type-1 cycles */
		switch (len) {
		      case 1:
			out_8(((u8 __iomem *)hose->cfg_data) +
				(offset & 3), val);
			break;
		      case 2:
			out_le16(((u16 __iomem *)hose->cfg_data) +
				((offset>>1) & 1), val);
			break;

		      default:
			out_le16((u16 __iomem *)hose->cfg_data,
				(u16)val);
			out_le16(((u16 __iomem *)hose->cfg_data) + 1,
				(u16)(val>>16));
			break;
		}
	}
	else
#endif
	{
		if (len != 4) {
			value = in_le32(hose->cfg_data);

			offset = (offset & 0x3) << 3;
			mask = (0xffffffff >> (32 - (len << 3)));
			mask <<= offset;

			value &= ~mask;
			val = value | ((val << offset) & mask);
		}

		out_le32(hose->cfg_data, val);
	}
	mb();

	out_be32(hose->cfg_addr, 0);
	mb();

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops mpc52xx_pci_ops = {
	.read  = mpc52xx_pci_read_config,
	.write = mpc52xx_pci_write_config
};


/* ======================================================================== */
/* PCI setup                                                                */
/* ======================================================================== */

static void __init
mpc52xx_pci_setup(struct pci_controller *hose,
                  struct mpc52xx_pci __iomem *pci_regs)
{
	struct resource *res;
	u32 tmp;
	int iwcr0 = 0, iwcr1 = 0, iwcr2 = 0;

	pr_debug("mpc52xx_pci_setup(hose=%p, pci_regs=%p)\n", hose, pci_regs);

	/* pci_process_bridge_OF_ranges() found all our addresses for us;
	 * now store them in the right places */
	hose->cfg_addr = &pci_regs->car;
	hose->cfg_data = hose->io_base_virt;

	/* Control regs */
	tmp = in_be32(&pci_regs->scr);
	tmp |= PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
	out_be32(&pci_regs->scr, tmp);

	/* Memory windows */
	res = &hose->mem_resources[0];
	if (res->flags) {
		pr_debug("mem_resource[0] = "
		         "{.start=%llx, .end=%llx, .flags=%llx}\n",
		         (unsigned long long)res->start,
			 (unsigned long long)res->end,
			 (unsigned long long)res->flags);
		out_be32(&pci_regs->iw0btar,
		         MPC52xx_PCI_IWBTAR_TRANSLATION(res->start, res->start,
		                  res->end - res->start + 1));
		iwcr0 = MPC52xx_PCI_IWCR_ENABLE | MPC52xx_PCI_IWCR_MEM;
		if (res->flags & IORESOURCE_PREFETCH)
			iwcr0 |= MPC52xx_PCI_IWCR_READ_MULTI;
		else
			iwcr0 |= MPC52xx_PCI_IWCR_READ;
	}

	res = &hose->mem_resources[1];
	if (res->flags) {
		pr_debug("mem_resource[1] = {.start=%x, .end=%x, .flags=%lx}\n",
		         res->start, res->end, res->flags);
		out_be32(&pci_regs->iw1btar,
		         MPC52xx_PCI_IWBTAR_TRANSLATION(res->start, res->start,
		                  res->end - res->start + 1));
		iwcr1 = MPC52xx_PCI_IWCR_ENABLE | MPC52xx_PCI_IWCR_MEM;
		if (res->flags & IORESOURCE_PREFETCH)
			iwcr1 |= MPC52xx_PCI_IWCR_READ_MULTI;
		else
			iwcr1 |= MPC52xx_PCI_IWCR_READ;
	}

	/* IO resources */
	res = &hose->io_resource;
	if (!res) {
		printk(KERN_ERR "%s: Didn't find IO resources\n", __FILE__);
		return;
	}
	pr_debug(".io_resource={.start=%llx,.end=%llx,.flags=%llx} "
	         ".io_base_phys=0x%p\n",
	         (unsigned long long)res->start,
		 (unsigned long long)res->end,
		 (unsigned long long)res->flags, (void*)hose->io_base_phys);
	out_be32(&pci_regs->iw2btar,
	         MPC52xx_PCI_IWBTAR_TRANSLATION(hose->io_base_phys,
	                                        res->start,
	                                        res->end - res->start + 1));
	iwcr2 = MPC52xx_PCI_IWCR_ENABLE | MPC52xx_PCI_IWCR_IO;

	/* Set all the IWCR fields at once; they're in the same reg */
	out_be32(&pci_regs->iwcr, MPC52xx_PCI_IWCR_PACK(iwcr0, iwcr1, iwcr2));

	out_be32(&pci_regs->tbatr0,
		MPC52xx_PCI_TBATR_ENABLE | MPC52xx_PCI_TARGET_IO );
	out_be32(&pci_regs->tbatr1,
		MPC52xx_PCI_TBATR_ENABLE | MPC52xx_PCI_TARGET_MEM );

	out_be32(&pci_regs->tcr, MPC52xx_PCI_TCR_LD | MPC52xx_PCI_TCR_WCT8);

	tmp = in_be32(&pci_regs->gscr);
#if 0
	/* Reset the exteral bus ( internal PCI controller is NOT resetted ) */
	/* Not necessary and can be a bad thing if for example the bootloader
	   is displaying a splash screen or ... Just left here for
	   documentation purpose if anyone need it */
	out_be32(&pci_regs->gscr, tmp | MPC52xx_PCI_GSCR_PR);
	udelay(50);
#endif

	/* Make sure the PCI bridge is out of reset */
	out_be32(&pci_regs->gscr, tmp & ~MPC52xx_PCI_GSCR_PR);
}

static void
mpc52xx_pci_fixup_resources(struct pci_dev *dev)
{
	int i;

	pr_debug("mpc52xx_pci_fixup_resources() %.4x:%.4x\n",
	         dev->vendor, dev->device);

	/* We don't rely on boot loader for PCI and resets all
	   devices */
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		struct resource *res = &dev->resource[i];
		if (res->end > res->start) {	/* Only valid resources */
			res->end -= res->start;
			res->start = 0;
			res->flags |= IORESOURCE_UNSET;
		}
	}

	/* The PCI Host bridge of MPC52xx has a prefetch memory resource
	   fixed to 1Gb. Doesn't fit in the resource system so we remove it */
	if ( (dev->vendor == PCI_VENDOR_ID_MOTOROLA) &&
	     (   dev->device == PCI_DEVICE_ID_MOTOROLA_MPC5200
	      || dev->device == PCI_DEVICE_ID_MOTOROLA_MPC5200B) ) {
		struct resource *res = &dev->resource[1];
		res->start = res->end = res->flags = 0;
	}
}

int __init
mpc52xx_add_bridge(struct device_node *node)
{
	int len;
	struct mpc52xx_pci __iomem *pci_regs;
	struct pci_controller *hose;
	const int *bus_range;
	struct resource rsrc;

	pr_debug("Adding MPC52xx PCI host bridge %s\n", node->full_name);

	ppc_pci_flags |= PPC_PCI_REASSIGN_ALL_BUS;

	if (of_address_to_resource(node, 0, &rsrc) != 0) {
		printk(KERN_ERR "Can't get %s resources\n", node->full_name);
		return -EINVAL;
	}

	bus_range = of_get_property(node, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING "Can't get %s bus-range, assume bus 0\n",
		       node->full_name);
		bus_range = NULL;
	}

	/* There are some PCI quirks on the 52xx, register the hook to
	 * fix them. */
	ppc_md.pcibios_fixup_resources = mpc52xx_pci_fixup_resources;

	/* Alloc and initialize the pci controller.  Values in the device
	 * tree are needed to configure the 52xx PCI controller.  Rather
	 * than parse the tree here, let pci_process_bridge_OF_ranges()
	 * do it for us and extract the values after the fact */
	hose = pcibios_alloc_controller(node);
	if (!hose)
		return -ENOMEM;

	hose->first_busno = bus_range ? bus_range[0] : 0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	hose->ops = &mpc52xx_pci_ops;

	pci_regs = ioremap(rsrc.start, rsrc.end - rsrc.start + 1);
	if (!pci_regs)
		return -ENOMEM;

	pci_process_bridge_OF_ranges(hose, node, 1);

	/* Finish setting up PCI using values obtained by
	 * pci_proces_bridge_OF_ranges */
	mpc52xx_pci_setup(hose, pci_regs);

	return 0;
}

void __init mpc52xx_setup_pci(void)
{
	struct device_node *pci;

	pci = of_find_matching_node(NULL, mpc52xx_pci_ids);
	if (!pci)
		return;

	mpc52xx_add_bridge(pci);
	of_node_put(pci);
}
