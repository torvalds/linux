// SPDX-License-Identifier: GPL-2.0-or-later
/*
 */

/**
 * Both AR2315 and AR2316 chips have PCI interface unit, which supports DMA
 * and interrupt. PCI interface supports MMIO access method, but does not
 * seem to support I/O ports.
 *
 * Read/write operation in the region 0x80000000-0xBFFFFFFF causes
 * a memory read/write command on the PCI bus. 30 LSBs of address on
 * the bus are taken from memory read/write request and 2 MSBs are
 * determined by PCI unit configuration.
 *
 * To work with the configuration space instead of memory is necessary set
 * the CFG_SEL bit in the PCI_MISC_CONFIG register.
 *
 * Devices on the bus can perform DMA requests via chip BAR1. PCI host
 * controller BARs are programmend as if an external device is programmed.
 * Which means that during configuration, IDSEL pin of the chip should be
 * asserted.
 *
 * We know (and support) only one board that uses the PCI interface -
 * Fonera 2.0g (FON2202). It has a USB EHCI controller connected to the
 * AR2315 PCI bus. IDSEL pin of USB controller is connected to AD[13] line
 * and IDSEL pin of AR2315 is connected to AD[16] line.
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <asm/paccess.h>

/*
 * PCI Bus Interface Registers
 */
#define AR2315_PCI_1MS_REG		0x0008

#define AR2315_PCI_1MS_MASK		0x3FFFF	/* # of AHB clk cycles in 1ms */

#define AR2315_PCI_MISC_CONFIG		0x000c

#define AR2315_PCIMISC_TXD_EN	0x00000001	/* Enable TXD for fragments */
#define AR2315_PCIMISC_CFG_SEL	0x00000002	/* Mem or Config cycles */
#define AR2315_PCIMISC_GIG_MASK	0x0000000C	/* bits 31-30 for pci req */
#define AR2315_PCIMISC_RST_MODE	0x00000030
#define AR2315_PCIRST_INPUT	0x00000000	/* 4:5=0 rst is input */
#define AR2315_PCIRST_LOW	0x00000010	/* 4:5=1 rst to GND */
#define AR2315_PCIRST_HIGH	0x00000020	/* 4:5=2 rst to VDD */
#define AR2315_PCIGRANT_EN	0x00000000	/* 6:7=0 early grant en */
#define AR2315_PCIGRANT_FRAME	0x00000040	/* 6:7=1 grant waits 4 frame */
#define AR2315_PCIGRANT_IDLE	0x00000080	/* 6:7=2 grant waits 4 idle */
#define AR2315_PCIGRANT_GAP	0x00000000	/* 6:7=2 grant waits 4 idle */
#define AR2315_PCICACHE_DIS	0x00001000	/* PCI external access cache
						 * disable */

#define AR2315_PCI_OUT_TSTAMP		0x0010

#define AR2315_PCI_UNCACHE_CFG		0x0014

#define AR2315_PCI_IN_EN		0x0100

#define AR2315_PCI_IN_EN0	0x01	/* Enable chain 0 */
#define AR2315_PCI_IN_EN1	0x02	/* Enable chain 1 */
#define AR2315_PCI_IN_EN2	0x04	/* Enable chain 2 */
#define AR2315_PCI_IN_EN3	0x08	/* Enable chain 3 */

#define AR2315_PCI_IN_DIS		0x0104

#define AR2315_PCI_IN_DIS0	0x01	/* Disable chain 0 */
#define AR2315_PCI_IN_DIS1	0x02	/* Disable chain 1 */
#define AR2315_PCI_IN_DIS2	0x04	/* Disable chain 2 */
#define AR2315_PCI_IN_DIS3	0x08	/* Disable chain 3 */

#define AR2315_PCI_IN_PTR		0x0200

#define AR2315_PCI_OUT_EN		0x0400

#define AR2315_PCI_OUT_EN0	0x01	/* Enable chain 0 */

#define AR2315_PCI_OUT_DIS		0x0404

#define AR2315_PCI_OUT_DIS0	0x01	/* Disable chain 0 */

#define AR2315_PCI_OUT_PTR		0x0408

/* PCI interrupt status (write one to clear) */
#define AR2315_PCI_ISR			0x0500

#define AR2315_PCI_INT_TX	0x00000001	/* Desc In Completed */
#define AR2315_PCI_INT_TXOK	0x00000002	/* Desc In OK */
#define AR2315_PCI_INT_TXERR	0x00000004	/* Desc In ERR */
#define AR2315_PCI_INT_TXEOL	0x00000008	/* Desc In End-of-List */
#define AR2315_PCI_INT_RX	0x00000010	/* Desc Out Completed */
#define AR2315_PCI_INT_RXOK	0x00000020	/* Desc Out OK */
#define AR2315_PCI_INT_RXERR	0x00000040	/* Desc Out ERR */
#define AR2315_PCI_INT_RXEOL	0x00000080	/* Desc Out EOL */
#define AR2315_PCI_INT_TXOOD	0x00000200	/* Desc In Out-of-Desc */
#define AR2315_PCI_INT_DESCMASK	0x0000FFFF	/* Desc Mask */
#define AR2315_PCI_INT_EXT	0x02000000	/* Extern PCI INTA */
#define AR2315_PCI_INT_ABORT	0x04000000	/* PCI bus abort event */

/* PCI interrupt mask */
#define AR2315_PCI_IMR			0x0504

/* Global PCI interrupt enable */
#define AR2315_PCI_IER			0x0508

#define AR2315_PCI_IER_DISABLE		0x00	/* disable pci interrupts */
#define AR2315_PCI_IER_ENABLE		0x01	/* enable pci interrupts */

#define AR2315_PCI_HOST_IN_EN		0x0800
#define AR2315_PCI_HOST_IN_DIS		0x0804
#define AR2315_PCI_HOST_IN_PTR		0x0810
#define AR2315_PCI_HOST_OUT_EN		0x0900
#define AR2315_PCI_HOST_OUT_DIS		0x0904
#define AR2315_PCI_HOST_OUT_PTR		0x0908

/*
 * PCI interrupts, which share IP5
 * Keep ordered according to AR2315_PCI_INT_XXX bits
 */
#define AR2315_PCI_IRQ_EXT		25
#define AR2315_PCI_IRQ_ABORT		26
#define AR2315_PCI_IRQ_COUNT		27

/* Arbitrary size of memory region to access the configuration space */
#define AR2315_PCI_CFG_SIZE	0x00100000

#define AR2315_PCI_HOST_SLOT	3
#define AR2315_PCI_HOST_DEVID	((0xff18 << 16) | PCI_VENDOR_ID_ATHEROS)

/*
 * We need some arbitrary non-zero value to be programmed to the BAR1 register
 * of PCI host controller to enable DMA. The same value should be used as the
 * offset to calculate the physical address of DMA buffer for PCI devices.
 */
#define AR2315_PCI_HOST_SDRAM_BASEADDR	0x20000000

/* ??? access BAR */
#define AR2315_PCI_HOST_MBAR0		0x10000000
/* RAM access BAR */
#define AR2315_PCI_HOST_MBAR1		AR2315_PCI_HOST_SDRAM_BASEADDR
/* ??? access BAR */
#define AR2315_PCI_HOST_MBAR2		0x30000000

struct ar2315_pci_ctrl {
	void __iomem *cfg_mem;
	void __iomem *mmr_mem;
	unsigned irq;
	unsigned irq_ext;
	struct irq_domain *domain;
	struct pci_controller pci_ctrl;
	struct resource mem_res;
	struct resource io_res;
};

static inline dma_addr_t ar2315_dev_offset(struct device *dev)
{
	if (dev && dev_is_pci(dev))
		return AR2315_PCI_HOST_SDRAM_BASEADDR;
	return 0;
}

dma_addr_t __phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return paddr + ar2315_dev_offset(dev);
}

phys_addr_t __dma_to_phys(struct device *dev, dma_addr_t dma_addr)
{
	return dma_addr - ar2315_dev_offset(dev);
}

static inline struct ar2315_pci_ctrl *ar2315_pci_bus_to_apc(struct pci_bus *bus)
{
	struct pci_controller *hose = bus->sysdata;

	return container_of(hose, struct ar2315_pci_ctrl, pci_ctrl);
}

static inline u32 ar2315_pci_reg_read(struct ar2315_pci_ctrl *apc, u32 reg)
{
	return __raw_readl(apc->mmr_mem + reg);
}

static inline void ar2315_pci_reg_write(struct ar2315_pci_ctrl *apc, u32 reg,
					u32 val)
{
	__raw_writel(val, apc->mmr_mem + reg);
}

static inline void ar2315_pci_reg_mask(struct ar2315_pci_ctrl *apc, u32 reg,
				       u32 mask, u32 val)
{
	u32 ret = ar2315_pci_reg_read(apc, reg);

	ret &= ~mask;
	ret |= val;
	ar2315_pci_reg_write(apc, reg, ret);
}

static int ar2315_pci_cfg_access(struct ar2315_pci_ctrl *apc, unsigned devfn,
				 int where, int size, u32 *ptr, bool write)
{
	int func = PCI_FUNC(devfn);
	int dev = PCI_SLOT(devfn);
	u32 addr = (1 << (13 + dev)) | (func << 8) | (where & ~3);
	u32 mask = 0xffffffff >> 8 * (4 - size);
	u32 sh = (where & 3) * 8;
	u32 value, isr;

	/* Prevent access past the remapped area */
	if (addr >= AR2315_PCI_CFG_SIZE || dev > 18)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Clear pending errors */
	ar2315_pci_reg_write(apc, AR2315_PCI_ISR, AR2315_PCI_INT_ABORT);
	/* Select Configuration access */
	ar2315_pci_reg_mask(apc, AR2315_PCI_MISC_CONFIG, 0,
			    AR2315_PCIMISC_CFG_SEL);

	mb();	/* PCI must see space change before we begin */

	value = __raw_readl(apc->cfg_mem + addr);

	isr = ar2315_pci_reg_read(apc, AR2315_PCI_ISR);

	if (isr & AR2315_PCI_INT_ABORT)
		goto exit_err;

	if (write) {
		value = (value & ~(mask << sh)) | *ptr << sh;
		__raw_writel(value, apc->cfg_mem + addr);
		isr = ar2315_pci_reg_read(apc, AR2315_PCI_ISR);
		if (isr & AR2315_PCI_INT_ABORT)
			goto exit_err;
	} else {
		*ptr = (value >> sh) & mask;
	}

	goto exit;

exit_err:
	ar2315_pci_reg_write(apc, AR2315_PCI_ISR, AR2315_PCI_INT_ABORT);
	if (!write)
		*ptr = 0xffffffff;

exit:
	/* Select Memory access */
	ar2315_pci_reg_mask(apc, AR2315_PCI_MISC_CONFIG, AR2315_PCIMISC_CFG_SEL,
			    0);

	return isr & AR2315_PCI_INT_ABORT ? PCIBIOS_DEVICE_NOT_FOUND :
					    PCIBIOS_SUCCESSFUL;
}

static inline int ar2315_pci_local_cfg_rd(struct ar2315_pci_ctrl *apc,
					  unsigned devfn, int where, u32 *val)
{
	return ar2315_pci_cfg_access(apc, devfn, where, sizeof(u32), val,
				     false);
}

static inline int ar2315_pci_local_cfg_wr(struct ar2315_pci_ctrl *apc,
					  unsigned devfn, int where, u32 val)
{
	return ar2315_pci_cfg_access(apc, devfn, where, sizeof(u32), &val,
				     true);
}

static int ar2315_pci_cfg_read(struct pci_bus *bus, unsigned devfn, int where,
			       int size, u32 *value)
{
	struct ar2315_pci_ctrl *apc = ar2315_pci_bus_to_apc(bus);

	if (PCI_SLOT(devfn) == AR2315_PCI_HOST_SLOT)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return ar2315_pci_cfg_access(apc, devfn, where, size, value, false);
}

static int ar2315_pci_cfg_write(struct pci_bus *bus, unsigned devfn, int where,
				int size, u32 value)
{
	struct ar2315_pci_ctrl *apc = ar2315_pci_bus_to_apc(bus);

	if (PCI_SLOT(devfn) == AR2315_PCI_HOST_SLOT)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return ar2315_pci_cfg_access(apc, devfn, where, size, &value, true);
}

static struct pci_ops ar2315_pci_ops = {
	.read	= ar2315_pci_cfg_read,
	.write	= ar2315_pci_cfg_write,
};

static int ar2315_pci_host_setup(struct ar2315_pci_ctrl *apc)
{
	unsigned devfn = PCI_DEVFN(AR2315_PCI_HOST_SLOT, 0);
	int res;
	u32 id;

	res = ar2315_pci_local_cfg_rd(apc, devfn, PCI_VENDOR_ID, &id);
	if (res != PCIBIOS_SUCCESSFUL || id != AR2315_PCI_HOST_DEVID)
		return -ENODEV;

	/* Program MBARs */
	ar2315_pci_local_cfg_wr(apc, devfn, PCI_BASE_ADDRESS_0,
				AR2315_PCI_HOST_MBAR0);
	ar2315_pci_local_cfg_wr(apc, devfn, PCI_BASE_ADDRESS_1,
				AR2315_PCI_HOST_MBAR1);
	ar2315_pci_local_cfg_wr(apc, devfn, PCI_BASE_ADDRESS_2,
				AR2315_PCI_HOST_MBAR2);

	/* Run */
	ar2315_pci_local_cfg_wr(apc, devfn, PCI_COMMAND, PCI_COMMAND_MEMORY |
				PCI_COMMAND_MASTER | PCI_COMMAND_SPECIAL |
				PCI_COMMAND_INVALIDATE | PCI_COMMAND_PARITY |
				PCI_COMMAND_SERR | PCI_COMMAND_FAST_BACK);

	return 0;
}

static void ar2315_pci_irq_handler(struct irq_desc *desc)
{
	struct ar2315_pci_ctrl *apc = irq_desc_get_handler_data(desc);
	u32 pending = ar2315_pci_reg_read(apc, AR2315_PCI_ISR) &
		      ar2315_pci_reg_read(apc, AR2315_PCI_IMR);
	unsigned pci_irq = 0;

	if (pending)
		pci_irq = irq_find_mapping(apc->domain, __ffs(pending));

	if (pci_irq)
		generic_handle_irq(pci_irq);
	else
		spurious_interrupt();
}

static void ar2315_pci_irq_mask(struct irq_data *d)
{
	struct ar2315_pci_ctrl *apc = irq_data_get_irq_chip_data(d);

	ar2315_pci_reg_mask(apc, AR2315_PCI_IMR, BIT(d->hwirq), 0);
}

static void ar2315_pci_irq_mask_ack(struct irq_data *d)
{
	struct ar2315_pci_ctrl *apc = irq_data_get_irq_chip_data(d);
	u32 m = BIT(d->hwirq);

	ar2315_pci_reg_mask(apc, AR2315_PCI_IMR, m, 0);
	ar2315_pci_reg_write(apc, AR2315_PCI_ISR, m);
}

static void ar2315_pci_irq_unmask(struct irq_data *d)
{
	struct ar2315_pci_ctrl *apc = irq_data_get_irq_chip_data(d);

	ar2315_pci_reg_mask(apc, AR2315_PCI_IMR, 0, BIT(d->hwirq));
}

static struct irq_chip ar2315_pci_irq_chip = {
	.name = "AR2315-PCI",
	.irq_mask = ar2315_pci_irq_mask,
	.irq_mask_ack = ar2315_pci_irq_mask_ack,
	.irq_unmask = ar2315_pci_irq_unmask,
};

static int ar2315_pci_irq_map(struct irq_domain *d, unsigned irq,
			      irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &ar2315_pci_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, d->host_data);
	return 0;
}

static struct irq_domain_ops ar2315_pci_irq_domain_ops = {
	.map = ar2315_pci_irq_map,
};

static void ar2315_pci_irq_init(struct ar2315_pci_ctrl *apc)
{
	ar2315_pci_reg_mask(apc, AR2315_PCI_IER, AR2315_PCI_IER_ENABLE, 0);
	ar2315_pci_reg_mask(apc, AR2315_PCI_IMR, (AR2315_PCI_INT_ABORT |
			    AR2315_PCI_INT_EXT), 0);

	apc->irq_ext = irq_create_mapping(apc->domain, AR2315_PCI_IRQ_EXT);

	irq_set_chained_handler_and_data(apc->irq, ar2315_pci_irq_handler,
					 apc);

	/* Clear any pending Abort or external Interrupts
	 * and enable interrupt processing */
	ar2315_pci_reg_write(apc, AR2315_PCI_ISR, AR2315_PCI_INT_ABORT |
						  AR2315_PCI_INT_EXT);
	ar2315_pci_reg_mask(apc, AR2315_PCI_IER, 0, AR2315_PCI_IER_ENABLE);
}

static int ar2315_pci_probe(struct platform_device *pdev)
{
	struct ar2315_pci_ctrl *apc;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int irq, err;

	apc = devm_kzalloc(dev, sizeof(*apc), GFP_KERNEL);
	if (!apc)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;
	apc->irq = irq;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "ar2315-pci-ctrl");
	apc->mmr_mem = devm_ioremap_resource(dev, res);
	if (IS_ERR(apc->mmr_mem))
		return PTR_ERR(apc->mmr_mem);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "ar2315-pci-ext");
	if (!res)
		return -EINVAL;

	apc->mem_res.name = "AR2315 PCI mem space";
	apc->mem_res.parent = res;
	apc->mem_res.start = res->start;
	apc->mem_res.end = res->end;
	apc->mem_res.flags = IORESOURCE_MEM;

	/* Remap PCI config space */
	apc->cfg_mem = devm_ioremap_nocache(dev, res->start,
					    AR2315_PCI_CFG_SIZE);
	if (!apc->cfg_mem) {
		dev_err(dev, "failed to remap PCI config space\n");
		return -ENOMEM;
	}

	/* Reset the PCI bus by setting bits 5-4 in PCI_MCFG */
	ar2315_pci_reg_mask(apc, AR2315_PCI_MISC_CONFIG,
			    AR2315_PCIMISC_RST_MODE,
			    AR2315_PCIRST_LOW);
	msleep(100);

	/* Bring the PCI out of reset */
	ar2315_pci_reg_mask(apc, AR2315_PCI_MISC_CONFIG,
			    AR2315_PCIMISC_RST_MODE,
			    AR2315_PCIRST_HIGH | AR2315_PCICACHE_DIS | 0x8);

	ar2315_pci_reg_write(apc, AR2315_PCI_UNCACHE_CFG,
			     0x1E | /* 1GB uncached */
			     (1 << 5) | /* Enable uncached */
			     (0x2 << 30) /* Base: 0x80000000 */);
	ar2315_pci_reg_read(apc, AR2315_PCI_UNCACHE_CFG);

	msleep(500);

	err = ar2315_pci_host_setup(apc);
	if (err)
		return err;

	apc->domain = irq_domain_add_linear(NULL, AR2315_PCI_IRQ_COUNT,
					    &ar2315_pci_irq_domain_ops, apc);
	if (!apc->domain) {
		dev_err(dev, "failed to add IRQ domain\n");
		return -ENOMEM;
	}

	ar2315_pci_irq_init(apc);

	/* PCI controller does not support I/O ports */
	apc->io_res.name = "AR2315 IO space";
	apc->io_res.start = 0;
	apc->io_res.end = 0;
	apc->io_res.flags = IORESOURCE_IO,

	apc->pci_ctrl.pci_ops = &ar2315_pci_ops;
	apc->pci_ctrl.mem_resource = &apc->mem_res,
	apc->pci_ctrl.io_resource = &apc->io_res,

	register_pci_controller(&apc->pci_ctrl);

	dev_info(dev, "register PCI controller\n");

	return 0;
}

static struct platform_driver ar2315_pci_driver = {
	.probe = ar2315_pci_probe,
	.driver = {
		.name = "ar2315-pci",
	},
};

static int __init ar2315_pci_init(void)
{
	return platform_driver_register(&ar2315_pci_driver);
}
arch_initcall(ar2315_pci_init);

int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct ar2315_pci_ctrl *apc = ar2315_pci_bus_to_apc(dev->bus);

	return slot ? 0 : apc->irq_ext;
}

int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
