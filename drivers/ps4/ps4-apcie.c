#define DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <asm/irqdomain.h>
#include <asm/irq_remapping.h>

#include <asm/msi.h>

#include <asm/ps4.h>

#include "aeolia.h"

/* #define QEMU_HACK_NO_IOMMU */

/* Number of implemented MSI registers per function */
static const int subfuncs_per_func[AEOLIA_NUM_FUNCS] = {
	4, 4, 4, 4, 31, 2, 2, 4
};

static inline u32 glue_read32(struct apcie_dev *sc, u32 offset) {
	return ioread32(sc->bar4 + offset);
}

static inline void glue_write32(struct apcie_dev *sc, u32 offset, u32 value) {
	iowrite32(value, sc->bar4 + offset);
}

static inline void glue_set_region(struct apcie_dev *sc, u32 func, u32 bar,
			    u32 base, u32 mask) {
	glue_write32(sc, APCIE_REG_BAR_MASK(func, bar), mask);
	glue_write32(sc, APCIE_REG_BAR_ADDR(func, bar), base);
}

static inline void glue_set_mask(struct apcie_dev *sc, u32 offset, u32 mask) {
	void __iomem *ptr = sc->bar4 + offset;
	iowrite32(ioread32(ptr) | mask, ptr);
}

static inline void glue_clear_mask(struct apcie_dev *sc, u32 offset, u32 mask) {
	void __iomem *ptr = sc->bar4 + offset;
	iowrite32(ioread32(ptr) & ~mask, ptr);
}

static inline void glue_mask_and_set(struct apcie_dev *sc, u32 offset, u32 mask, u32 set) {
	void __iomem *ptr = sc->bar4 + offset;
	iowrite32((ioread32(ptr) & ~mask) | set, ptr);
}

static void apcie_config_msi(struct apcie_dev *sc, u32 func, u32 subfunc,
			     u32 addr, u32 data) {
	u32 offset;

	sc_dbg("apcie_config_msi: func: %u, subfunc: %u, addr %08x data: 0x%08x (%u)\n",
		func, subfunc, addr, data, data);

	glue_clear_mask(sc, APCIE_REG_MSI_CONTROL, APCIE_REG_MSI_CONTROL_ENABLE);
	/* Unknown */
	glue_write32(sc, APCIE_REG_MSI(0x8), 0xffffffff);
	/* Unknown */
	glue_write32(sc, APCIE_REG_MSI(0xc + (func << 2)), 0xB7FFFF00 + func * 16);
	glue_write32(sc, APCIE_REG_MSI_ADDR(func), addr);
	/* Unknown */
	glue_write32(sc, APCIE_REG_MSI(0xcc + (func << 2)), 0);
	glue_write32(sc, APCIE_REG_MSI_DATA_HI(func), data & 0xffe0);

	if (func < 4) {
		/* First 4 functions have 4 IRQs/subfuncs each */
		offset = (func << 4) | (subfunc << 2);
	} else if (func == 4) {
		/* Function 4 gets 24 consecutive slots,
		 * then 7 more at the end. */
		if (subfunc < 24)
			offset = 0x40 + (subfunc << 2);
		else
			offset = 0xe0 + ((subfunc - 24) << 2);
	} else {
		offset = 0xa0 + ((func - 5) << 4) + (subfunc << 2);
	}
	glue_write32(sc, APCIE_REG_MSI_DATA_LO(offset), data & 0x1f);

	if (func == AEOLIA_FUNC_ID_PCIE)
		glue_set_mask(sc, APCIE_REG_MSI_MASK(func), APCIE_REG_MSI_MASK_FUNC4);
	else
		glue_set_mask(sc, APCIE_REG_MSI_MASK(func), APCIE_REG_MSI_MASK_FUNC);

	glue_set_mask(sc, APCIE_REG_MSI_CONTROL, APCIE_REG_MSI_CONTROL_ENABLE);
}

static void apcie_msi_write_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct apcie_dev *sc = data->chip_data;
	u32 func = data->hwirq >> 8;
	u32 subfunc = data->hwirq & 0xff;

	/* Linux likes to unconfigure MSIs like this, but since we share the
	 * address between subfunctions, we can't do that. The IRQ should be
	 * masked via apcie_msi_mask anyway, so just do nothing. */
	if (!msg->address_lo) {
		return;
	}

	sc_dbg("apcie_msi_write_msg(%08x, %08x) mask=0x%x irq=%d hwirq=0x%lx %p\n",
	       msg->address_lo, msg->data, data->mask, data->irq, data->hwirq, sc);

	if (subfunc == 0xff) {
		int i;
		for (i = 0; i < subfuncs_per_func[func]; i++)
			apcie_config_msi(sc, func, i, msg->address_lo, msg->data);
	} else {
		apcie_config_msi(sc, func, subfunc, msg->address_lo, msg->data);
	}
}

static void apcie_msi_unmask(struct irq_data *data)
{
	struct apcie_dev *sc = data->chip_data;
	u32 func = data->hwirq >> 8;

	glue_set_mask(sc, APCIE_REG_MSI_MASK(func), data->mask);
}

static void apcie_msi_mask(struct irq_data *data)
{
	struct apcie_dev *sc = data->chip_data;
	u32 func = data->hwirq >> 8;

	glue_clear_mask(sc, APCIE_REG_MSI_MASK(func), data->mask);
}

static void apcie_msi_calc_mask(struct irq_data *data) {
	u32 func = data->hwirq >> 8;
	u32 subfunc = data->hwirq & 0xff;

	if (subfunc == 0xff) {
		data->mask = (1 << subfuncs_per_func[func]) - 1;
	} else {
		data->mask = 1 << subfunc;
	}
}

static struct irq_chip apcie_msi_controller = {
	.name = "Aeolia-MSI",
	.irq_unmask = apcie_msi_unmask,
	.irq_mask = apcie_msi_mask,
	.irq_ack = irq_chip_ack_parent,
	.irq_set_affinity = msi_domain_set_affinity,
	.irq_retrigger = irq_chip_retrigger_hierarchy,
	.irq_compose_msi_msg = irq_msi_compose_msg,
	.irq_write_msi_msg = apcie_msi_write_msg,
	.flags = IRQCHIP_SKIP_SET_WAKE,
};

static irq_hw_number_t apcie_msi_get_hwirq(struct msi_domain_info *info,
					  msi_alloc_info_t *arg)
{
	return arg->msi_hwirq;
}

static int apcie_msi_init(struct irq_domain *domain,
			 struct msi_domain_info *info, unsigned int virq,
			 irq_hw_number_t hwirq, msi_alloc_info_t *arg)
{
	struct irq_data *data;
	pr_devel("apcie_msi_init(%p, %p, %d, 0x%lx, %p)\n", domain, info, virq, hwirq, arg);

	data = irq_domain_get_irq_data(domain, virq);
	irq_domain_set_info(domain, virq, hwirq, info->chip, info->chip_data,
			    handle_edge_irq, NULL, "edge");
	apcie_msi_calc_mask(data);
	return 0;
}

static void apcie_msi_free(struct irq_domain *domain,
			  struct msi_domain_info *info, unsigned int virq)
{
	pr_devel("apcie_msi_free(%d)\n", virq);
}

static struct msi_domain_ops apcie_msi_domain_ops = {
	.get_hwirq	= apcie_msi_get_hwirq,
	.msi_init	= apcie_msi_init,
	.msi_free	= apcie_msi_free,
};

static struct msi_domain_info apcie_msi_domain_info = {
	.flags		= MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS,
	.ops		= &apcie_msi_domain_ops,
	.chip		= &apcie_msi_controller,
	.handler	= handle_edge_irq,
};

struct irq_domain *apcie_create_irq_domain(struct apcie_dev *sc)
{
	struct irq_domain *parent;
	struct irq_alloc_info info;

	sc_dbg("apcie_create_irq_domain\n");
	if (x86_vector_domain == NULL)
		return NULL;

	apcie_msi_domain_info.chip_data = (void *)sc;

	init_irq_alloc_info(&info, NULL);
	info.type = X86_IRQ_ALLOC_TYPE_MSI;
	info.msi_dev = sc->pdev;
	parent = irq_remapping_get_ir_irq_domain(&info);
	if (parent == NULL) {
		parent = x86_vector_domain;
	} else {
		apcie_msi_domain_info.flags |= MSI_FLAG_MULTI_PCI_MSI;
		apcie_msi_controller.name = "IR-Aeolia-MSI";
	}

	return msi_create_irq_domain(NULL, &apcie_msi_domain_info, parent);
}

int apcie_assign_irqs(struct pci_dev *dev, int nvec)
{
	int ret;
	unsigned int sc_devfn;
	struct pci_dev *sc_dev;
	struct apcie_dev *sc;
	struct irq_alloc_info info;

	sc_devfn = (dev->devfn & ~7) | AEOLIA_FUNC_ID_PCIE;
	sc_dev = pci_get_slot(dev->bus, sc_devfn);
	if (!sc_dev || sc_dev->vendor != PCI_VENDOR_ID_SONY ||
		sc_dev->device != PCI_DEVICE_ID_APCIE) {
		dev_err(&dev->dev, "apcie: this is not an Aeolia device\n");
		ret = -ENODEV;
		goto fail;
	}
	sc = pci_get_drvdata(sc_dev);
	if (!sc) {
		dev_err(&dev->dev, "apcie: not ready yet, cannot assign IRQs\n");
		ret = -ENODEV;
		goto fail;
	}

	init_irq_alloc_info(&info, NULL);
	info.type = X86_IRQ_ALLOC_TYPE_MSI;
	/* IRQs "come from" function 4 as far as the IOMMU/system see */
	info.msi_dev = sc->pdev;
	/* Our hwirq number is function << 8 plus subfunction.
	 * Subfunction is usually 0 and implicitly increments per hwirq,
	 * but can also be 0xff to indicate that this is a shared IRQ. */
	info.msi_hwirq = PCI_FUNC(dev->devfn) << 8;

	dev_dbg(&dev->dev, "apcie_assign_irqs(%d)\n", nvec);

#ifndef QEMU_HACK_NO_IOMMU
	info.flags = X86_IRQ_ALLOC_CONTIGUOUS_VECTORS;
	if (!(apcie_msi_domain_info.flags & MSI_FLAG_MULTI_PCI_MSI)) {
		nvec = 1;
		info.msi_hwirq |= 0xff; /* Shared IRQ for all subfunctions */
	}
#endif

	ret = irq_domain_alloc_irqs(sc->irqdomain, nvec, NUMA_NO_NODE, &info);
	if (ret >= 0) {
		dev->irq = ret;
		ret = nvec;
	}

fail:
	dev_dbg(&dev->dev, "apcie_assign_irqs returning %d\n", ret);
	if (sc_dev)
		pci_dev_put(sc_dev);
	return ret;
}
EXPORT_SYMBOL(apcie_assign_irqs);

void apcie_free_irqs(unsigned int virq, unsigned int nr_irqs)
{
	irq_domain_free_irqs(virq, nr_irqs);
}
EXPORT_SYMBOL(apcie_free_irqs);

static void apcie_glue_remove(struct apcie_dev *sc);

static int apcie_glue_init(struct apcie_dev *sc)
{
	int i;

	sc_info("apcie glue probe\n");

	if (!request_mem_region(pci_resource_start(sc->pdev, 4) +
				APCIE_RGN_PCIE_BASE, APCIE_RGN_PCIE_SIZE,
				"apcie.glue")) {
		sc_err("Failed to request pcie region\n");
		return -EBUSY;

	}

	if (!request_mem_region(pci_resource_start(sc->pdev, 2) +
				APCIE_RGN_CHIPID_BASE, APCIE_RGN_CHIPID_SIZE,
				"apcie.chipid")) {
		sc_err("Failed to request chipid region\n");
		release_mem_region(pci_resource_start(sc->pdev, 4) +
				   APCIE_RGN_PCIE_BASE, APCIE_RGN_PCIE_SIZE);
		return -EBUSY;
	}

	glue_set_region(sc, AEOLIA_FUNC_ID_PCIE, 2, 0xbf018000, 0x7fff);

	sc_info("Aeolia chip revision: %08x:%08x:%08x\n",
		ioread32(sc->bar2 + APCIE_REG_CHIPID_0),
		ioread32(sc->bar2 + APCIE_REG_CHIPID_1),
		ioread32(sc->bar2 + APCIE_REG_CHIPREV));

	/* Mask all MSIs first, to avoid spurious IRQs */
	for (i = 0; i < AEOLIA_NUM_FUNCS; i++) {
		glue_write32(sc, APCIE_REG_MSI_MASK(i), 0);
		glue_write32(sc, APCIE_REG_MSI_ADDR(i), 0);
		glue_write32(sc, APCIE_REG_MSI_DATA_HI(i), 0);
	}

	for (i = 0; i < 0xfc; i += 4)
		glue_write32(sc, APCIE_REG_MSI_DATA_LO(i), 0);

	glue_set_region(sc, AEOLIA_FUNC_ID_GBE, 0, 0xbfa00000, 0x3fff);
	glue_set_region(sc, AEOLIA_FUNC_ID_AHCI, 5, 0xbfa04000, 0xfff);
	glue_set_region(sc, AEOLIA_FUNC_ID_SDHCI, 0, 0xbfa80000, 0xfff);
	glue_set_region(sc, AEOLIA_FUNC_ID_SDHCI, 1, 0, 0);
	glue_set_region(sc, AEOLIA_FUNC_ID_DMAC, 0, 0xbfa05000, 0xfff);
	glue_set_region(sc, AEOLIA_FUNC_ID_DMAC, 1, 0, 0);
	glue_set_region(sc, AEOLIA_FUNC_ID_DMAC, 2, 0xbfa06000, 0xfff);
	glue_set_region(sc, AEOLIA_FUNC_ID_DMAC, 3, 0, 0);
	glue_set_region(sc, AEOLIA_FUNC_ID_MEM, 2, 0xc0000000, 0x3fffffff);
	glue_set_region(sc, AEOLIA_FUNC_ID_MEM, 3, 0, 0);
	glue_set_region(sc, AEOLIA_FUNC_ID_XHCI, 0, 0xbf400000, 0x1fffff);
	glue_set_region(sc, AEOLIA_FUNC_ID_XHCI, 1, 0, 0);
	glue_set_region(sc, AEOLIA_FUNC_ID_XHCI, 2, 0xbf600000, 0x1fffff);
	glue_set_region(sc, AEOLIA_FUNC_ID_XHCI, 3, 0, 0);
	glue_set_region(sc, AEOLIA_FUNC_ID_XHCI, 4, 0xbf800000, 0x1fffff);
	glue_set_region(sc, AEOLIA_FUNC_ID_XHCI, 5, 0, 0);

	sc->irqdomain = apcie_create_irq_domain(sc);
	if (!sc->irqdomain) {
		sc_err("Failed to create IRQ domain");
		apcie_glue_remove(sc);
		return -EIO;
	}
	sc->nvec = apcie_assign_irqs(sc->pdev, APCIE_NUM_SUBFUNC);
	if (sc->nvec <= 0) {
		sc_err("Failed to assign IRQs");
		apcie_glue_remove(sc);
		return -EIO;
	}
	sc_dbg("dev->irq=%d\n", sc->pdev->irq);

	return 0;
}

static void apcie_glue_remove(struct apcie_dev *sc) {
	sc_info("apcie glue remove\n");

	if (sc->nvec > 0) {
		apcie_free_irqs(sc->pdev->irq, sc->nvec);
		sc->nvec = 0;
	}
	if (sc->irqdomain) {
		irq_domain_remove(sc->irqdomain);
		sc->irqdomain = NULL;
	}
	release_mem_region(pci_resource_start(sc->pdev, 2) +
			   APCIE_RGN_CHIPID_BASE, APCIE_RGN_CHIPID_SIZE);
	release_mem_region(pci_resource_start(sc->pdev, 4) +
			   APCIE_RGN_PCIE_BASE, APCIE_RGN_PCIE_SIZE);
}

#ifdef CONFIG_PM
static int apcie_glue_suspend(struct apcie_dev *sc, pm_message_t state) {
	return 0;
}

static int apcie_glue_resume(struct apcie_dev *sc) {
	return 0;
}
#endif


int apcie_uart_init(struct apcie_dev *sc);
int apcie_icc_init(struct apcie_dev *sc);
void apcie_uart_remove(struct apcie_dev *sc);
void apcie_icc_remove(struct apcie_dev *sc);
#ifdef CONFIG_PM
void apcie_uart_suspend(struct apcie_dev *sc, pm_message_t state);
void apcie_icc_suspend(struct apcie_dev *sc, pm_message_t state);
void apcie_uart_resume(struct apcie_dev *sc);
void apcie_icc_resume(struct apcie_dev *sc);
#endif

/* From arch/x86/platform/ps4/ps4.c */
extern bool apcie_initialized;

static int apcie_probe(struct pci_dev *dev, const struct pci_device_id *id) {
	struct apcie_dev *sc;
	int ret;

	dev_dbg(&dev->dev, "apcie_probe()\n");

	ret = pci_enable_device(dev);
	if (ret) {
		dev_err(&dev->dev,
			"apcie_probe(): pci_enable_device failed: %d\n", ret);
		return ret;
	}

	sc = kzalloc(sizeof(*sc), GFP_KERNEL);
	if (!sc) {
		dev_err(&dev->dev, "apcie_probe(): alloc sc failed\n");
		ret = -ENOMEM;
		goto disable_dev;
	}
	sc->pdev = dev;
	pci_set_drvdata(dev, sc);

	// eMMC ... unused?
	sc->bar0 = pci_ioremap_bar(dev, 0);
	// pervasive 0
	sc->bar2 = pci_ioremap_bar(dev, 2);
	// pervasive 1 - misc peripherals
	sc->bar4 = pci_ioremap_bar(dev, 4);

	if (!sc->bar0 || !sc->bar2 || !sc->bar4) {
		sc_err("failed to map some BARs, bailing out\n");
		ret = -EIO;
		goto free_bars;
	}

	if ((ret = apcie_glue_init(sc)) < 0)
		goto free_bars;
	if ((ret = apcie_uart_init(sc)) < 0)
		goto remove_glue;
	if ((ret = apcie_icc_init(sc)) < 0)
		goto remove_uart;

	apcie_initialized = true;
	return 0;

remove_uart:
	apcie_uart_remove(sc);
remove_glue:
	apcie_glue_remove(sc);
free_bars:
	if (sc->bar0)
		iounmap(sc->bar0);
	if (sc->bar2)
		iounmap(sc->bar2);
	if (sc->bar4)
		iounmap(sc->bar4);
	kfree(sc);
disable_dev:
	pci_disable_device(dev);
	return ret;
}

static void apcie_remove(struct pci_dev *dev) {
	struct apcie_dev *sc;
	sc = pci_get_drvdata(dev);

	apcie_icc_remove(sc);
	apcie_uart_remove(sc);
	apcie_glue_remove(sc);

	if (sc->bar0)
		iounmap(sc->bar0);
	if (sc->bar2)
		iounmap(sc->bar2);
	if (sc->bar4)
		iounmap(sc->bar4);
	kfree(sc);
	pci_disable_device(dev);
}

#ifdef CONFIG_PM
static int apcie_suspend(struct pci_dev *dev, pm_message_t state) {
	struct apcie_dev *sc;
	sc = pci_get_drvdata(dev);

	apcie_icc_suspend(sc, state);
	apcie_uart_suspend(sc, state);
	apcie_glue_suspend(sc, state);
	return 0;
}

static int apcie_resume(struct pci_dev *dev) {
	struct apcie_dev *sc;
	sc = pci_get_drvdata(dev);

	apcie_icc_resume(sc);
	apcie_glue_resume(sc);
	apcie_uart_resume(sc);
	return 0;
}
#endif

static const struct pci_device_id apcie_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_SONY, PCI_DEVICE_ID_APCIE), },
	{ }
};
MODULE_DEVICE_TABLE(pci, apcie_pci_tbl);

static struct pci_driver apcie_driver = {
	.name		= "aeolia_pcie",
	.id_table	= apcie_pci_tbl,
	.probe		= apcie_probe,
	.remove		= apcie_remove,
#ifdef CONFIG_PM
	.suspend	= apcie_suspend,
	.resume		= apcie_resume,
#endif
};
module_pci_driver(apcie_driver);
