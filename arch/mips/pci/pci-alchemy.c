/*
 * Alchemy PCI host mode support.
 *
 * Copyright 2001-2003, 2007-2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com>
 *
 * Support for all devices (greater than 16) added by David Gathright.
 */

#include <linux/export.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/vmalloc.h>

#include <asm/mach-au1x00/au1000.h>

#ifdef CONFIG_DEBUG_PCI
#define DBG(x...) printk(KERN_DEBUG x)
#else
#define DBG(x...) do {} while (0)
#endif

#define PCI_ACCESS_READ		0
#define PCI_ACCESS_WRITE	1

struct alchemy_pci_context {
	struct pci_controller alchemy_pci_ctrl;	/* leave as first member! */
	void __iomem *regs;			/* ctrl base */
	/* tools for wired entry for config space access */
	unsigned long last_elo0;
	unsigned long last_elo1;
	int wired_entry;
	struct vm_struct *pci_cfg_vm;

	unsigned long pm[12];

	int (*board_map_irq)(const struct pci_dev *d, u8 slot, u8 pin);
	int (*board_pci_idsel)(unsigned int devsel, int assert);
};

/* IO/MEM resources for PCI. Keep the memres in sync with __fixup_bigphys_addr
 * in arch/mips/alchemy/common/setup.c
 */
static struct resource alchemy_pci_def_memres = {
	.start	= ALCHEMY_PCI_MEMWIN_START,
	.end	= ALCHEMY_PCI_MEMWIN_END,
	.name	= "PCI memory space",
	.flags	= IORESOURCE_MEM
};

static struct resource alchemy_pci_def_iores = {
	.start	= ALCHEMY_PCI_IOWIN_START,
	.end	= ALCHEMY_PCI_IOWIN_END,
	.name	= "PCI IO space",
	.flags	= IORESOURCE_IO
};

static void mod_wired_entry(int entry, unsigned long entrylo0,
		unsigned long entrylo1, unsigned long entryhi,
		unsigned long pagemask)
{
	unsigned long old_pagemask;
	unsigned long old_ctx;

	/* Save old context and create impossible VPN2 value */
	old_ctx = read_c0_entryhi() & 0xff;
	old_pagemask = read_c0_pagemask();
	write_c0_index(entry);
	write_c0_pagemask(pagemask);
	write_c0_entryhi(entryhi);
	write_c0_entrylo0(entrylo0);
	write_c0_entrylo1(entrylo1);
	tlb_write_indexed();
	write_c0_entryhi(old_ctx);
	write_c0_pagemask(old_pagemask);
}

static void alchemy_pci_wired_entry(struct alchemy_pci_context *ctx)
{
	ctx->wired_entry = read_c0_wired();
	add_wired_entry(0, 0, (unsigned long)ctx->pci_cfg_vm->addr, PM_4K);
	ctx->last_elo0 = ctx->last_elo1 = ~0;
}

static int config_access(unsigned char access_type, struct pci_bus *bus,
			 unsigned int dev_fn, unsigned char where, u32 *data)
{
	struct alchemy_pci_context *ctx = bus->sysdata;
	unsigned int device = PCI_SLOT(dev_fn);
	unsigned int function = PCI_FUNC(dev_fn);
	unsigned long offset, status, cfg_base, flags, entryLo0, entryLo1, r;
	int error = PCIBIOS_SUCCESSFUL;

	if (device > 19) {
		*data = 0xffffffff;
		return -1;
	}

	/* YAMON on all db1xxx boards wipes the TLB and writes zero to C0_wired
	 * on resume, clearing our wired entry.  Unfortunately the ->resume()
	 * callback is called way way way too late (and ->suspend() too early)
	 * to have them destroy and recreate it.  Instead just test if c0_wired
	 * is now lower than the index we retrieved before suspending and then
	 * recreate the entry if necessary.  Of course this is totally bonkers
	 * and breaks as soon as someone else adds another wired entry somewhere
	 * else.  Anyone have any ideas how to handle this better?
	 */
	if (unlikely(read_c0_wired() < ctx->wired_entry))
		alchemy_pci_wired_entry(ctx);

	local_irq_save(flags);
	r = __raw_readl(ctx->regs + PCI_REG_STATCMD) & 0x0000ffff;
	r |= PCI_STATCMD_STATUS(0x2000);
	__raw_writel(r, ctx->regs + PCI_REG_STATCMD);
	wmb();

	/* Allow board vendors to implement their own off-chip IDSEL.
	 * If it doesn't succeed, may as well bail out at this point.
	 */
	if (ctx->board_pci_idsel(device, 1) == 0) {
		*data = 0xffffffff;
		local_irq_restore(flags);
		return -1;
	}

	/* Setup the config window */
	if (bus->number == 0)
		cfg_base = (1 << device) << 11;
	else
		cfg_base = 0x80000000 | (bus->number << 16) | (device << 11);

	/* Setup the lower bits of the 36-bit address */
	offset = (function << 8) | (where & ~0x3);
	/* Pick up any address that falls below the page mask */
	offset |= cfg_base & ~PAGE_MASK;

	/* Page boundary */
	cfg_base = cfg_base & PAGE_MASK;

	/* To improve performance, if the current device is the same as
	 * the last device accessed, we don't touch the TLB.
	 */
	entryLo0 = (6 << 26) | (cfg_base >> 6) | (2 << 3) | 7;
	entryLo1 = (6 << 26) | (cfg_base >> 6) | (0x1000 >> 6) | (2 << 3) | 7;
	if ((entryLo0 != ctx->last_elo0) || (entryLo1 != ctx->last_elo1)) {
		mod_wired_entry(ctx->wired_entry, entryLo0, entryLo1,
				(unsigned long)ctx->pci_cfg_vm->addr, PM_4K);
		ctx->last_elo0 = entryLo0;
		ctx->last_elo1 = entryLo1;
	}

	if (access_type == PCI_ACCESS_WRITE)
		__raw_writel(*data, ctx->pci_cfg_vm->addr + offset);
	else
		*data = __raw_readl(ctx->pci_cfg_vm->addr + offset);
	wmb();

	DBG("alchemy-pci: cfg access %d bus %u dev %u at %x dat %x conf %lx\n",
	    access_type, bus->number, device, where, *data, offset);

	/* check for errors, master abort */
	status = __raw_readl(ctx->regs + PCI_REG_STATCMD);
	if (status & (1 << 29)) {
		*data = 0xffffffff;
		error = -1;
		DBG("alchemy-pci: master abort on cfg access %d bus %d dev %d",
		    access_type, bus->number, device);
	} else if ((status >> 28) & 0xf) {
		DBG("alchemy-pci: PCI ERR detected: dev %d, status %lx\n",
		    device, (status >> 28) & 0xf);

		/* clear errors */
		__raw_writel(status & 0xf000ffff, ctx->regs + PCI_REG_STATCMD);

		*data = 0xffffffff;
		error = -1;
	}

	/* Take away the IDSEL. */
	(void)ctx->board_pci_idsel(device, 0);

	local_irq_restore(flags);
	return error;
}

static int read_config_byte(struct pci_bus *bus, unsigned int devfn,
			    int where,	u8 *val)
{
	u32 data;
	int ret = config_access(PCI_ACCESS_READ, bus, devfn, where, &data);

	if (where & 1)
		data >>= 8;
	if (where & 2)
		data >>= 16;
	*val = data & 0xff;
	return ret;
}

static int read_config_word(struct pci_bus *bus, unsigned int devfn,
			    int where, u16 *val)
{
	u32 data;
	int ret = config_access(PCI_ACCESS_READ, bus, devfn, where, &data);

	if (where & 2)
		data >>= 16;
	*val = data & 0xffff;
	return ret;
}

static int read_config_dword(struct pci_bus *bus, unsigned int devfn,
			     int where, u32 *val)
{
	return config_access(PCI_ACCESS_READ, bus, devfn, where, val);
}

static int write_config_byte(struct pci_bus *bus, unsigned int devfn,
			     int where, u8 val)
{
	u32 data = 0;

	if (config_access(PCI_ACCESS_READ, bus, devfn, where, &data))
		return -1;

	data = (data & ~(0xff << ((where & 3) << 3))) |
	       (val << ((where & 3) << 3));

	if (config_access(PCI_ACCESS_WRITE, bus, devfn, where, &data))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

static int write_config_word(struct pci_bus *bus, unsigned int devfn,
			     int where, u16 val)
{
	u32 data = 0;

	if (config_access(PCI_ACCESS_READ, bus, devfn, where, &data))
		return -1;

	data = (data & ~(0xffff << ((where & 3) << 3))) |
	       (val << ((where & 3) << 3));

	if (config_access(PCI_ACCESS_WRITE, bus, devfn, where, &data))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

static int write_config_dword(struct pci_bus *bus, unsigned int devfn,
			      int where, u32 val)
{
	return config_access(PCI_ACCESS_WRITE, bus, devfn, where, &val);
}

static int alchemy_pci_read(struct pci_bus *bus, unsigned int devfn,
		       int where, int size, u32 *val)
{
	switch (size) {
	case 1: {
			u8 _val;
			int rc = read_config_byte(bus, devfn, where, &_val);

			*val = _val;
			return rc;
		}
	case 2: {
			u16 _val;
			int rc = read_config_word(bus, devfn, where, &_val);

			*val = _val;
			return rc;
		}
	default:
		return read_config_dword(bus, devfn, where, val);
	}
}

static int alchemy_pci_write(struct pci_bus *bus, unsigned int devfn,
			     int where, int size, u32 val)
{
	switch (size) {
	case 1:
		return write_config_byte(bus, devfn, where, (u8) val);
	case 2:
		return write_config_word(bus, devfn, where, (u16) val);
	default:
		return write_config_dword(bus, devfn, where, val);
	}
}

static struct pci_ops alchemy_pci_ops = {
	.read	= alchemy_pci_read,
	.write	= alchemy_pci_write,
};

static int alchemy_pci_def_idsel(unsigned int devsel, int assert)
{
	return 1;	/* success */
}

static int __devinit alchemy_pci_probe(struct platform_device *pdev)
{
	struct alchemy_pci_platdata *pd = pdev->dev.platform_data;
	struct alchemy_pci_context *ctx;
	void __iomem *virt_io;
	unsigned long val;
	struct resource *r;
	int ret;

	/* need at least PCI IRQ mapping table */
	if (!pd) {
		dev_err(&pdev->dev, "need platform data for PCI setup\n");
		ret = -ENODEV;
		goto out;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		dev_err(&pdev->dev, "no memory for pcictl context\n");
		ret = -ENOMEM;
		goto out;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "no  pcictl ctrl regs resource\n");
		ret = -ENODEV;
		goto out1;
	}

	if (!request_mem_region(r->start, resource_size(r), pdev->name)) {
		dev_err(&pdev->dev, "cannot claim pci regs\n");
		ret = -ENODEV;
		goto out1;
	}

	ctx->regs = ioremap_nocache(r->start, resource_size(r));
	if (!ctx->regs) {
		dev_err(&pdev->dev, "cannot map pci regs\n");
		ret = -ENODEV;
		goto out2;
	}

	/* map parts of the PCI IO area */
	/* REVISIT: if this changes with a newer variant (doubt it) make this
	 * a platform resource.
	 */
	virt_io = ioremap(AU1500_PCI_IO_PHYS_ADDR, 0x00100000);
	if (!virt_io) {
		dev_err(&pdev->dev, "cannot remap pci io space\n");
		ret = -ENODEV;
		goto out3;
	}
	ctx->alchemy_pci_ctrl.io_map_base = (unsigned long)virt_io;

#ifdef CONFIG_DMA_NONCOHERENT
	/* Au1500 revisions older than AD have borked coherent PCI */
	if ((alchemy_get_cputype() == ALCHEMY_CPU_AU1500) &&
	    (read_c0_prid() < 0x01030202)) {
		val = __raw_readl(ctx->regs + PCI_REG_CONFIG);
		val |= PCI_CONFIG_NC;
		__raw_writel(val, ctx->regs + PCI_REG_CONFIG);
		wmb();
		dev_info(&pdev->dev, "non-coherent PCI on Au1500 AA/AB/AC\n");
	}
#endif

	if (pd->board_map_irq)
		ctx->board_map_irq = pd->board_map_irq;

	if (pd->board_pci_idsel)
		ctx->board_pci_idsel = pd->board_pci_idsel;
	else
		ctx->board_pci_idsel = alchemy_pci_def_idsel;

	/* fill in relevant pci_controller members */
	ctx->alchemy_pci_ctrl.pci_ops = &alchemy_pci_ops;
	ctx->alchemy_pci_ctrl.mem_resource = &alchemy_pci_def_memres;
	ctx->alchemy_pci_ctrl.io_resource = &alchemy_pci_def_iores;

	/* we can't ioremap the entire pci config space because it's too large,
	 * nor can we dynamically ioremap it because some drivers use the
	 * PCI config routines from within atomic contex and that becomes a
	 * problem in get_vm_area().  Instead we use one wired TLB entry to
	 * handle all config accesses for all busses.
	 */
	ctx->pci_cfg_vm = get_vm_area(0x2000, VM_IOREMAP);
	if (!ctx->pci_cfg_vm) {
		dev_err(&pdev->dev, "unable to get vm area\n");
		ret = -ENOMEM;
		goto out4;
	}
	ctx->wired_entry = 8192;	/* impossibly high value */

	set_io_port_base((unsigned long)ctx->alchemy_pci_ctrl.io_map_base);

	/* board may want to modify bits in the config register, do it now */
	val = __raw_readl(ctx->regs + PCI_REG_CONFIG);
	val &= ~pd->pci_cfg_clr;
	val |= pd->pci_cfg_set;
	val &= ~PCI_CONFIG_PD;		/* clear disable bit */
	__raw_writel(val, ctx->regs + PCI_REG_CONFIG);
	wmb();

	platform_set_drvdata(pdev, ctx);
	register_pci_controller(&ctx->alchemy_pci_ctrl);

	return 0;

out4:
	iounmap(virt_io);
out3:
	iounmap(ctx->regs);
out2:
	release_mem_region(r->start, resource_size(r));
out1:
	kfree(ctx);
out:
	return ret;
}


#ifdef CONFIG_PM
/* save PCI controller register contents. */
static int alchemy_pci_suspend(struct device *dev)
{
	struct alchemy_pci_context *ctx = dev_get_drvdata(dev);

	ctx->pm[0]  = __raw_readl(ctx->regs + PCI_REG_CMEM);
	ctx->pm[1]  = __raw_readl(ctx->regs + PCI_REG_CONFIG) & 0x0009ffff;
	ctx->pm[2]  = __raw_readl(ctx->regs + PCI_REG_B2BMASK_CCH);
	ctx->pm[3]  = __raw_readl(ctx->regs + PCI_REG_B2BBASE0_VID);
	ctx->pm[4]  = __raw_readl(ctx->regs + PCI_REG_B2BBASE1_SID);
	ctx->pm[5]  = __raw_readl(ctx->regs + PCI_REG_MWMASK_DEV);
	ctx->pm[6]  = __raw_readl(ctx->regs + PCI_REG_MWBASE_REV_CCL);
	ctx->pm[7]  = __raw_readl(ctx->regs + PCI_REG_ID);
	ctx->pm[8]  = __raw_readl(ctx->regs + PCI_REG_CLASSREV);
	ctx->pm[9]  = __raw_readl(ctx->regs + PCI_REG_PARAM);
	ctx->pm[10] = __raw_readl(ctx->regs + PCI_REG_MBAR);
	ctx->pm[11] = __raw_readl(ctx->regs + PCI_REG_TIMEOUT);

	return 0;
}

static int alchemy_pci_resume(struct device *dev)
{
	struct alchemy_pci_context *ctx = dev_get_drvdata(dev);

	__raw_writel(ctx->pm[0],  ctx->regs + PCI_REG_CMEM);
	__raw_writel(ctx->pm[2],  ctx->regs + PCI_REG_B2BMASK_CCH);
	__raw_writel(ctx->pm[3],  ctx->regs + PCI_REG_B2BBASE0_VID);
	__raw_writel(ctx->pm[4],  ctx->regs + PCI_REG_B2BBASE1_SID);
	__raw_writel(ctx->pm[5],  ctx->regs + PCI_REG_MWMASK_DEV);
	__raw_writel(ctx->pm[6],  ctx->regs + PCI_REG_MWBASE_REV_CCL);
	__raw_writel(ctx->pm[7],  ctx->regs + PCI_REG_ID);
	__raw_writel(ctx->pm[8],  ctx->regs + PCI_REG_CLASSREV);
	__raw_writel(ctx->pm[9],  ctx->regs + PCI_REG_PARAM);
	__raw_writel(ctx->pm[10], ctx->regs + PCI_REG_MBAR);
	__raw_writel(ctx->pm[11], ctx->regs + PCI_REG_TIMEOUT);
	wmb();
	__raw_writel(ctx->pm[1],  ctx->regs + PCI_REG_CONFIG);
	wmb();

	return 0;
}

static const struct dev_pm_ops alchemy_pci_pmops = {
	.suspend	= alchemy_pci_suspend,
	.resume		= alchemy_pci_resume,
};

#define ALCHEMY_PCICTL_PM	(&alchemy_pci_pmops)

#else
#define ALCHEMY_PCICTL_PM	NULL
#endif

static struct platform_driver alchemy_pcictl_driver = {
	.probe		= alchemy_pci_probe,
	.driver	= {
		.name	= "alchemy-pci",
		.owner	= THIS_MODULE,
		.pm	= ALCHEMY_PCICTL_PM,
	},
};

static int __init alchemy_pci_init(void)
{
	/* Au1500/Au1550 have PCI */
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1500:
	case ALCHEMY_CPU_AU1550:
		return platform_driver_register(&alchemy_pcictl_driver);
	}
	return 0;
}
arch_initcall(alchemy_pci_init);


int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct alchemy_pci_context *ctx = dev->sysdata;
	if (ctx && ctx->board_map_irq)
		return ctx->board_map_irq(dev, slot, pin);
	return -1;
}

int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
