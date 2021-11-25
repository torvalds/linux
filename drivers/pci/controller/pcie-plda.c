/*
 * PCIe host controller driver for Starfive JH7110 Soc.
 *
 * Based on pcie-altera.c, pcie-altera-msi.c.
 *
 * Copyright (C) Shanghai StarFive Technology Co., Ltd.
 *
 * Author: ke.zhu@starfivetech.com
 *
 * THE PRESENT SOFTWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
 * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
 * TIME. AS A RESULT, STARFIVE SHALL NOT BE HELD LIABLE FOR ANY
 * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
 * FROM THE CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
 * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
 */

#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/msi.h>



#define PCIE_BASIC_STATUS	0x018
#define PCIE_CFGNUM		0x140
#define IMASK_LOCAL		0x180
#define ISTATUS_LOCAL		0x184
#define IMSI_ADDR		0x190
#define ISTATUS_MSI		0x194
#define CFG_SPACE		0x1000
#define GEN_SETTINGS         0x80

#define XR3PCI_ATR_PCIE_WIN0		0x600
#define XR3PCI_ATR_PCIE_WIN1		0x700
#define XR3PCI_ATR_AXI4_SLV0		0x800

#define XR3PCI_ATR_TABLE_SIZE		0x20
#define XR3PCI_ATR_SRC_ADDR_LOW		0x0
#define XR3PCI_ATR_SRC_ADDR_HIGH	0x4
#define XR3PCI_ATR_TRSL_ADDR_LOW	0x8
#define XR3PCI_ATR_TRSL_ADDR_HIGH	0xc
#define XR3PCI_ATR_TRSL_PARAM		0x10

/* IDs used in the XR3PCI_ATR_TRSL_PARAM */
#define XR3PCI_ATR_TRSLID_AXIDEVICE	(0x420004)
#define XR3PCI_ATR_TRSLID_AXIMEMORY	(0x4e0004)  /* Write-through, read/write allocate */
#define XR3PCI_ATR_TRSLID_PCIE_CONF	(0x000001)
#define XR3PCI_ATR_TRSLID_PCIE_IO	(0x020000)
#define XR3PCI_ATR_TRSLID_PCIE_MEMORY	(0x000000)


#define PCIE_MEM_BASE_ADDR      0x40000000//ddr space
#define PCIE_MEM_SIZE_1G           30
#define PCIE_MEM_SIZE_2G           31
#define PCIE_MEM_SIZE_4G           32



#define CFGNUM_DEVFN_SHIFT	0
#define CFGNUM_BUS_SHIFT	8
#define CFGNUM_BE_SHIFT		16
#define CFGNUM_FBE_SHIFT	20

#define INT_AXI_POST_ERROR	BIT(16)
#define INT_AXI_FETCH_ERROR	BIT(17)
#define INT_AXI_DISCARD_ERROR	BIT(18)
#define INT_PCIE_POST_ERROR	BIT(20)
#define INT_PCIE_FETCH_ERROR	BIT(21)
#define INT_PCIE_DISCARD_ERROR	BIT(22)
#define INT_ERRORS		(INT_AXI_POST_ERROR | INT_AXI_FETCH_ERROR |    \
				 INT_AXI_DISCARD_ERROR | INT_PCIE_POST_ERROR | \
				 INT_PCIE_FETCH_ERROR | INT_PCIE_DISCARD_ERROR)
#define INTA_OFFSET		24
#define INTA			BIT(24)
#define INTB			BIT(25)
#define INTC			BIT(26)
#define INTD			BIT(27)
#define INT_MSI			BIT(28)
#define INT_INTX_MASK		(INTA | INTB | INTC | INTD)
#define INT_MASK		(INT_INTX_MASK | INT_MSI | INT_ERRORS)


#define INT_PCI_MSI_NR		32
#define LINK_UP_MASK		0xff

#define DWORD_MASK		3


#define PCI_DEV(d)		(((d) >> 3) & 0x1f)
#define PCI_FUNC(d)		(((d) ) & 0x7)


#define PLDA_PCIE0_MEM_BASE 0x900000000
#define PLDA_PCIE0_CONFIG_BASE 0x940000000

#define PLDA_PCIE1_MEM_BASE 0x980000000
#define PLDA_PCIE1_CONFIG_BASE 0x9c0000000

#define PLDA_PCIE0_MMIO_BASE 0x30000000

#define PLDA_PCIE_MEM_SIZE 30 
#define PLDA_PCIE_CONFIG_SIZE 27
#define PLDA_PCIE_MMIO_SIZE 27

struct plda_msi {			/* MSI information */
	DECLARE_BITMAP(used, INT_PCI_MSI_NR);
	struct irq_domain *msi_domain;
	struct irq_domain *inner_domain;
	struct mutex lock;		/* protect bitmap variable */
};

struct plda_pcie {
	struct platform_device	*pdev;
	void __iomem		*reg_base;
	void __iomem		*config_base;
	int			irq;
	struct irq_domain	*legacy_irq_domain;
	struct pci_host_bridge  *bridge;
	struct plda_msi		msi;
};

static inline void plda_writel(struct plda_pcie *pcie, const u32 value,
			       const u32 reg)
{
	writel_relaxed(value, pcie->reg_base + reg);
}

static inline u32 plda_readl(struct plda_pcie *pcie, const u32 reg)
{
	return readl_relaxed(pcie->reg_base + reg);
}

static bool plda_pcie_link_is_up(struct plda_pcie *pcie)
{
	return !!((plda_readl(pcie, PCIE_BASIC_STATUS) & LINK_UP_MASK));
}

static bool plda_pcie_hide_rc_bar(struct pci_bus *bus, unsigned int  devfn,
				  int offset)
{
	if (pci_is_root_bus(bus) && (devfn == 0) &&
	    (offset == PCI_BASE_ADDRESS_0))
		return true;

	return false;
}

static void __iomem *plda_map_bus(struct plda_pcie *pcie, unsigned char busno,
				  unsigned int devfn,
				  int where, u8 byte_en)
{
	plda_writel(pcie, (busno << CFGNUM_BUS_SHIFT) |
			(devfn << CFGNUM_DEVFN_SHIFT) |
			(byte_en << CFGNUM_BE_SHIFT) |
			(1 << CFGNUM_FBE_SHIFT),
			PCIE_CFGNUM);

	return pcie->reg_base + CFG_SPACE + where;
}

static int _plda_pcie_config_read(struct plda_pcie *pcie, unsigned char busno,
				  unsigned int devfn, int where, int size,
				  u32 *value)
{
	void __iomem *addr;

	addr = pcie->config_base;
	addr += (busno << 20);
	addr += (PCI_DEV(devfn) << 15);
	addr += (PCI_FUNC(devfn) << 12);
	addr += where;
	
	if(!addr )
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		*(unsigned char *)value = readb(addr);
		break;
	case 2:
		*(unsigned short *)value = readw(addr);
		break;
	case 4:
		*(unsigned int *)value = readl(addr);
		break;
	default:
		return PCIBIOS_SET_FAILED;
	}
	
	return PCIBIOS_SUCCESSFUL;
}

int _plda_pcie_config_write(struct plda_pcie *pcie, unsigned char busno,
			    unsigned int devfn, int where, int size, u32 value)
{
	void __iomem *addr;

	addr = pcie->config_base;
	addr += (busno << 20);
	addr += (PCI_DEV(devfn) << 15);
	addr += (PCI_FUNC(devfn) << 12);
	addr += where;

	if(!addr )
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		writeb(value, addr);
		break;
	case 2:
		writew(value, addr);
		break;
	case 4:
		writel(value, addr);
		break;
	default:
		return PCIBIOS_SET_FAILED;
	}	

	return PCIBIOS_SUCCESSFUL;
}

static int plda_pcie_config_read(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *value)
{
	struct plda_pcie *pcie = bus->sysdata;

	return _plda_pcie_config_read(pcie, bus->number, devfn, where, size,
				      value);
}

int plda_pcie_config_write(struct pci_bus *bus, unsigned int devfn,
			   int where, int size, u32 value)
{
	struct plda_pcie *pcie = bus->sysdata;

	if (plda_pcie_hide_rc_bar(bus, devfn, where))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return _plda_pcie_config_write(pcie, bus->number, devfn, where, size,
				       value);
}

static void plda_pcie_handle_msi_irq(struct plda_pcie *pcie)
{
	struct plda_msi *msi = &pcie->msi;
	u32 bit;
	u32 virq;
	unsigned long status = plda_readl(pcie, ISTATUS_MSI);

	for_each_set_bit(bit, &status, INT_PCI_MSI_NR) {
		/* clear interrupts */
		plda_writel(pcie, 1 << bit, ISTATUS_MSI);

		virq = irq_find_mapping(msi->inner_domain, bit);
		if (virq) {
			if (test_bit(bit, msi->used))
				generic_handle_irq(virq);
			else
				dev_err(&pcie->pdev->dev,
					"unhandled MSI, MSI%d virq %d\n", bit,
					virq);
		} else
			dev_err(&pcie->pdev->dev, "unexpected MSI, MSI%d\n",
				bit);
	}
	plda_writel(pcie, INT_MSI, ISTATUS_LOCAL);
}

static void plda_pcie_handle_intx_irq(struct plda_pcie *pcie,
				      unsigned long status)
{
	u32 bit;
	u32 virq;

	status >>= INTA_OFFSET;

	for_each_set_bit(bit, &status, PCI_NUM_INTX) {
		/* clear interrupts */
		plda_writel(pcie, 1 << (bit + INTA_OFFSET), ISTATUS_LOCAL);
		
		virq = irq_find_mapping(pcie->legacy_irq_domain, bit);
		if (virq)
			generic_handle_irq(virq);
		else
			dev_err(&pcie->pdev->dev,
				"plda_pcie_handle_intx_irq unexpected IRQ, INT%d\n", bit);
	}
}

static void plda_pcie_handle_errors_irq(struct plda_pcie *pcie, u32 status)
{
 	if (status & INT_AXI_POST_ERROR)
  		dev_err(&pcie->pdev->dev, "AXI post error\n");
 	if (status & INT_AXI_FETCH_ERROR)
  		dev_err(&pcie->pdev->dev, "AXI fetch error\n");
 	if (status & INT_AXI_DISCARD_ERROR)
  		dev_err(&pcie->pdev->dev, "AXI discard error\n");
 	if (status & INT_PCIE_POST_ERROR)
  		dev_err(&pcie->pdev->dev, "PCIe post error\n");
 	if (status & INT_PCIE_FETCH_ERROR)
  		dev_err(&pcie->pdev->dev, "PCIe fetch error\n");
	if (status & INT_PCIE_DISCARD_ERROR)
  		dev_err(&pcie->pdev->dev, "PCIe discard error\n");

 	plda_writel(pcie, INT_ERRORS, ISTATUS_LOCAL);	
}

static void plda_pcie_isr(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct plda_pcie *pcie;
	u32 status;
	 
	chained_irq_enter(chip, desc);
	pcie = irq_desc_get_handler_data(desc);
	
	status = plda_readl(pcie, ISTATUS_LOCAL);	
	while ((status = (plda_readl(pcie, ISTATUS_LOCAL) & INT_MASK))) {
		if (status & INT_INTX_MASK)
			plda_pcie_handle_intx_irq(pcie, status);

		if (status & INT_MSI)
			plda_pcie_handle_msi_irq(pcie);

		if (status & INT_ERRORS)
			plda_pcie_handle_errors_irq(pcie, status);
	}
	
	chained_irq_exit(chip, desc);
}


#ifdef CONFIG_PCI_MSI
static struct irq_chip plda_msi_irq_chip = {
	.name = "PLDA PCIe MSI",
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static struct msi_domain_info plda_msi_domain_info = {
	.flags = (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		  MSI_FLAG_PCI_MSIX),
	.chip = &plda_msi_irq_chip,
};
#endif

static void plda_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct plda_pcie *pcie = irq_data_get_irq_chip_data(data);
	phys_addr_t msi_addr = plda_readl(pcie, IMSI_ADDR);

	msg->address_lo = lower_32_bits(msi_addr);
	msg->address_hi = upper_32_bits(msi_addr);
	msg->data = data->hwirq;

	printk("msi#%d address_hi %#x address_lo %#x\n",
		(int)data->hwirq, msg->address_hi, msg->address_lo);	
}

static int plda_msi_set_affinity(struct irq_data *irq_data,
				 const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static struct irq_chip plda_irq_chip = {
	.name = "PLDA MSI",
	.irq_compose_msi_msg = plda_compose_msi_msg,
	.irq_set_affinity = plda_msi_set_affinity,
};

static int plda_msi_alloc(struct irq_domain *domain, unsigned int virq,
			  unsigned int nr_irqs, void *args)
{
	struct plda_pcie *pcie = domain->host_data;
	struct plda_msi *msi = &pcie->msi;
	int bit;

	WARN_ON(nr_irqs != 1);
	mutex_lock(&msi->lock);

	bit = find_first_zero_bit(msi->used, INT_PCI_MSI_NR);
	if (bit >= INT_PCI_MSI_NR) {
		mutex_unlock(&msi->lock);
		return -ENOSPC;
	}

	set_bit(bit, msi->used);

	irq_domain_set_info(domain, virq, bit, &plda_irq_chip,
			    domain->host_data, handle_simple_irq,
			    NULL, NULL);
	mutex_unlock(&msi->lock);

	return 0;
}

static void plda_msi_free(struct irq_domain *domain, unsigned int virq,
			  unsigned int nr_irqs)
{
	struct irq_data *data = irq_domain_get_irq_data(domain, virq);
	struct plda_pcie *pcie = irq_data_get_irq_chip_data(data);
	struct plda_msi *msi = &pcie->msi;

	mutex_lock(&msi->lock);

	if (!test_bit(data->hwirq, msi->used))
		dev_err(&pcie->pdev->dev, "trying to free unused MSI#%lu\n",
			data->hwirq);
	else
		__clear_bit(data->hwirq, msi->used);

	mutex_unlock(&msi->lock);
}

static const struct irq_domain_ops dev_msi_domain_ops = {
	.alloc  = plda_msi_alloc,
	.free   = plda_msi_free,
};

static void plda_msi_free_irq_domain(struct plda_pcie *pcie)
{
#ifdef CONFIG_PCI_MSI
	struct plda_msi *msi = &pcie->msi;
	u32 irq;
	int i;

	for (i = 0; i < INT_PCI_MSI_NR; i++) {
		irq = irq_find_mapping(msi->inner_domain, i);
		if (irq > 0)
			irq_dispose_mapping(irq);
	}

	if (msi->msi_domain)
		irq_domain_remove(msi->msi_domain);

	if (msi->inner_domain)
		irq_domain_remove(msi->inner_domain);
#endif
}

static void plda_pcie_free_irq_domain(struct plda_pcie *pcie)
{
	int i;
	u32 irq;

	/* Disable all interrupts */
	plda_writel(pcie, 0, IMASK_LOCAL);

	if (pcie->legacy_irq_domain) {
		for (i = 0; i < PCI_NUM_INTX; i++) {
			irq = irq_find_mapping(pcie->legacy_irq_domain, i );
			if (irq > 0)
				irq_dispose_mapping(irq);
		}
		irq_domain_remove(pcie->legacy_irq_domain);
	}

	if (pci_msi_enabled())
		plda_msi_free_irq_domain(pcie);
	irq_set_chained_handler_and_data(pcie->irq, NULL, NULL);
}

static int plda_pcie_init_msi_irq_domain(struct plda_pcie *pcie)
{
#ifdef CONFIG_PCI_MSI
	struct fwnode_handle *fwn = of_node_to_fwnode(pcie->pdev->dev.of_node);
	struct plda_msi *msi = &pcie->msi;

	msi->inner_domain = irq_domain_add_linear(NULL, INT_PCI_MSI_NR,
						  &dev_msi_domain_ops, pcie);
	if (!msi->inner_domain) {
		dev_err(&pcie->pdev->dev, "failed to create dev IRQ domain\n");
		return -ENOMEM;
	}
	msi->msi_domain = pci_msi_create_irq_domain(fwn, &plda_msi_domain_info,
						    msi->inner_domain);
	if (!msi->msi_domain) {
		dev_err(&pcie->pdev->dev, "failed to create msi IRQ domain\n");
		irq_domain_remove(msi->inner_domain);
		return -ENOMEM;
	}
#endif
	return 0;
}

static int plda_pcie_enable_msi(struct plda_pcie *pcie, struct pci_bus *bus)
{
	struct plda_msi *msi = &pcie->msi;
	u32 reg;

	mutex_init(&msi->lock);

	/* Enable MSI */
	reg = plda_readl(pcie, IMASK_LOCAL);
	reg |= INT_MSI;
	plda_writel(pcie, reg, IMASK_LOCAL);
	return 0;
}

static int plda_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
			      irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = plda_pcie_intx_map,
	.xlate = pci_irqd_intx_xlate,		
};

static int plda_pcie_init_irq_domain(struct plda_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct device_node *node = dev->of_node;
	int ret;

	if (pci_msi_enabled()){
		ret = plda_pcie_init_msi_irq_domain(pcie);
		if(ret != 0)
			return -ENOMEM;
	}	

	/* Setup INTx */
	pcie->legacy_irq_domain = irq_domain_add_linear(node, PCI_NUM_INTX,
					&intx_domain_ops, pcie);

	if (!pcie->legacy_irq_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return -ENOMEM;
	}

	irq_set_chained_handler_and_data(pcie->irq, plda_pcie_isr, pcie);  
	return 0;
}

static int plda_pcie_parse_dt(struct plda_pcie *pcie)
{
	struct resource *reg_res,*config_res;
	struct platform_device *pdev = pcie->pdev;


	reg_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "reg");
	if (!reg_res) {
		dev_err(&pdev->dev, "missing required reg address range");
		return -ENODEV;
	}
	
	pcie->reg_base = devm_ioremap_resource(&pdev->dev, reg_res);
	if (IS_ERR(pcie->reg_base)){
		dev_err(&pdev->dev, "failed to map reg memory\n");
		return PTR_ERR(pcie->reg_base);
	}

	config_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config");
	if (!config_res) {
		dev_err(&pdev->dev, "missing required config address range");
		return -ENODEV;
	}
	
	pcie->config_base = devm_ioremap_resource(&pdev->dev, config_res);
	if (IS_ERR(pcie->config_base)){
		dev_err(&pdev->dev, "failed to map config memory\n");
		return PTR_ERR(pcie->config_base);
	}
	
	pcie->irq = platform_get_irq(pdev, 0);
	if (pcie->irq <= 0) {
		dev_err(&pdev->dev, "failed to get IRQ: %d\n", pcie->irq);
		return -EINVAL;
	}

	/* clear all interrupts */
	plda_writel(pcie, 0xffffffff, ISTATUS_LOCAL);
	plda_writel(pcie, INT_INTX_MASK | INT_ERRORS, IMASK_LOCAL);
	
	return 0;
}

static struct pci_ops plda_pcie_ops = {
	.read           = plda_pcie_config_read,
	.write          = plda_pcie_config_write,
};


void plda_set_atr_entry(unsigned long base, unsigned long src_addr,
			unsigned long trsl_addr, int window_size,
			int trsl_param)
{
	/* X3PCI_ATR_SRC_ADDR_LOW:
	     - bit 0: enable entry,
	     - bits 1-6: ATR window size: total size in bytes: 2^(ATR_WSIZE + 1)
	     - bits 7-11: reserved
	     - bits 12-31: start of source address
	*/
	writel((u32)(src_addr & 0xfffff000) | (window_size - 1) << 1 | 1,
	       base + XR3PCI_ATR_SRC_ADDR_LOW);
	writel((u32)(src_addr >> 32), base + XR3PCI_ATR_SRC_ADDR_HIGH);
	writel((u32)(trsl_addr & 0xfffff000), base + XR3PCI_ATR_TRSL_ADDR_LOW);
	writel((u32)(trsl_addr >> 32), base + XR3PCI_ATR_TRSL_ADDR_HIGH);
	writel(trsl_param, base + XR3PCI_ATR_TRSL_PARAM);

	printk("ATR entry: 0x%010lx %s 0x%010lx [0x%010llx] (param: 0x%06x)\n",
	       src_addr, (trsl_param & 0x400000) ? "<-" : "->", trsl_addr,
	       ((u64)1) << window_size, trsl_param);
}
			

void plda_pcie_hw_init(struct plda_pcie *pcie)
{
	unsigned int value;	
	unsigned long base;
	unsigned int primary_bus = 0;

	/* add credits */
    value = readl(pcie->reg_base + GEN_SETTINGS);
    value |= 0x1;
    value &= ~(1 << 12);//disable 5G/s
    writel(value, pcie->reg_base + GEN_SETTINGS);

	/* setup CPU to PCIe address translation table */
	base = pcie->reg_base + XR3PCI_ATR_AXI4_SLV0;

	/* setup config space translation */
	plda_set_atr_entry(base,PLDA_PCIE0_MMIO_BASE,PLDA_PCIE0_MMIO_BASE, PLDA_PCIE_MMIO_SIZE,
			     XR3PCI_ATR_TRSLID_PCIE_MEMORY);		

}

static int plda_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct plda_pcie *pcie;
	struct pci_bus *bus;
	struct pci_bus *child;
	struct pci_host_bridge *bridge;	
	int ret;
	u32 status;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->pdev = pdev;

	ret = plda_pcie_parse_dt(pcie);
	if (ret) {
		dev_err(&pdev->dev, "Parsing DT failed\n");
		return ret;
	}

	platform_set_drvdata(pdev, pcie);

	ret = plda_pcie_init_irq_domain(pcie);
	if (ret) {
		dev_err(&pdev->dev, "Failed creating IRQ Domain\n");
		return ret;
	}

	bridge = devm_pci_alloc_host_bridge(dev, 0);
	if (!bridge)
		return -ENOMEM;

	/* Set default bus ops */
	bridge->ops = &plda_pcie_ops;
	bridge->sysdata = pcie;
	pcie->bridge = bridge;
	
	plda_pcie_hw_init(pcie);


	ret = pci_host_probe(bridge);
	if(ret < 0){
		dev_err(&pdev->dev,"failed to pci host probe: %d\n", ret);
		return ret;
	}
	
	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		ret = plda_pcie_enable_msi(pcie, bus);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"failed to enable MSI support: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

static int plda_pcie_remove(struct platform_device *pdev)
{
	struct plda_pcie *pcie = platform_get_drvdata(pdev);

	plda_pcie_free_irq_domain(pcie);
	platform_set_drvdata(pdev, NULL);
	return 0;
}


static const struct of_device_id plda_pcie_of_match[] = {
	{ .compatible = "plda,pci-xpressrich3-axi"},
	{ },
};
MODULE_DEVICE_TABLE(of, plda_pcie_of_match);
	

static struct platform_driver plda_pcie_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(plda_pcie_of_match),
	},
	.probe = plda_pcie_probe,
	.remove = plda_pcie_remove,
};
module_platform_driver(plda_pcie_driver);

MODULE_DESCRIPTION("StarFive JH7110 PCIe host driver");
MODULE_AUTHOR("ke.zhu <ke.zhu@starfivetech.com>");
MODULE_LICENSE("GPL v2");
