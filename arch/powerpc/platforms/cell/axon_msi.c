/*
 * Copyright 2007, Michael Ellerman, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */


#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/export.h>
#include <linux/of_platform.h>
#include <linux/debugfs.h>
#include <linux/slab.h>

#include <asm/dcr.h>
#include <asm/machdep.h>
#include <asm/prom.h>

#include "cell.h"

/*
 * MSIC registers, specified as offsets from dcr_base
 */
#define MSIC_CTRL_REG	0x0

/* Base Address registers specify FIFO location in BE memory */
#define MSIC_BASE_ADDR_HI_REG	0x3
#define MSIC_BASE_ADDR_LO_REG	0x4

/* Hold the read/write offsets into the FIFO */
#define MSIC_READ_OFFSET_REG	0x5
#define MSIC_WRITE_OFFSET_REG	0x6


/* MSIC control register flags */
#define MSIC_CTRL_ENABLE		0x0001
#define MSIC_CTRL_FIFO_FULL_ENABLE	0x0002
#define MSIC_CTRL_IRQ_ENABLE		0x0008
#define MSIC_CTRL_FULL_STOP_ENABLE	0x0010

/*
 * The MSIC can be configured to use a FIFO of 32KB, 64KB, 128KB or 256KB.
 * Currently we're using a 64KB FIFO size.
 */
#define MSIC_FIFO_SIZE_SHIFT	16
#define MSIC_FIFO_SIZE_BYTES	(1 << MSIC_FIFO_SIZE_SHIFT)

/*
 * To configure the FIFO size as (1 << n) bytes, we write (n - 15) into bits
 * 8-9 of the MSIC control reg.
 */
#define MSIC_CTRL_FIFO_SIZE	(((MSIC_FIFO_SIZE_SHIFT - 15) << 8) & 0x300)

/*
 * We need to mask the read/write offsets to make sure they stay within
 * the bounds of the FIFO. Also they should always be 16-byte aligned.
 */
#define MSIC_FIFO_SIZE_MASK	((MSIC_FIFO_SIZE_BYTES - 1) & ~0xFu)

/* Each entry in the FIFO is 16 bytes, the first 4 bytes hold the irq # */
#define MSIC_FIFO_ENTRY_SIZE	0x10


struct axon_msic {
	struct irq_domain *irq_domain;
	__le32 *fifo_virt;
	dma_addr_t fifo_phys;
	dcr_host_t dcr_host;
	u32 read_offset;
#ifdef DEBUG
	u32 __iomem *trigger;
#endif
};

#ifdef DEBUG
void axon_msi_debug_setup(struct device_node *dn, struct axon_msic *msic);
#else
static inline void axon_msi_debug_setup(struct device_node *dn,
					struct axon_msic *msic) { }
#endif


static void msic_dcr_write(struct axon_msic *msic, unsigned int dcr_n, u32 val)
{
	pr_devel("axon_msi: dcr_write(0x%x, 0x%x)\n", val, dcr_n);

	dcr_write(msic->dcr_host, dcr_n, val);
}

static void axon_msi_cascade(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct axon_msic *msic = irq_desc_get_handler_data(desc);
	u32 write_offset, msi;
	int idx;
	int retry = 0;

	write_offset = dcr_read(msic->dcr_host, MSIC_WRITE_OFFSET_REG);
	pr_devel("axon_msi: original write_offset 0x%x\n", write_offset);

	/* write_offset doesn't wrap properly, so we have to mask it */
	write_offset &= MSIC_FIFO_SIZE_MASK;

	while (msic->read_offset != write_offset && retry < 100) {
		idx  = msic->read_offset / sizeof(__le32);
		msi  = le32_to_cpu(msic->fifo_virt[idx]);
		msi &= 0xFFFF;

		pr_devel("axon_msi: woff %x roff %x msi %x\n",
			  write_offset, msic->read_offset, msi);

		if (msi < nr_irqs && irq_get_chip_data(msi) == msic) {
			generic_handle_irq(msi);
			msic->fifo_virt[idx] = cpu_to_le32(0xffffffff);
		} else {
			/*
			 * Reading the MSIC_WRITE_OFFSET_REG does not
			 * reliably flush the outstanding DMA to the
			 * FIFO buffer. Here we were reading stale
			 * data, so we need to retry.
			 */
			udelay(1);
			retry++;
			pr_devel("axon_msi: invalid irq 0x%x!\n", msi);
			continue;
		}

		if (retry) {
			pr_devel("axon_msi: late irq 0x%x, retry %d\n",
				 msi, retry);
			retry = 0;
		}

		msic->read_offset += MSIC_FIFO_ENTRY_SIZE;
		msic->read_offset &= MSIC_FIFO_SIZE_MASK;
	}

	if (retry) {
		printk(KERN_WARNING "axon_msi: irq timed out\n");

		msic->read_offset += MSIC_FIFO_ENTRY_SIZE;
		msic->read_offset &= MSIC_FIFO_SIZE_MASK;
	}

	chip->irq_eoi(&desc->irq_data);
}

static struct axon_msic *find_msi_translator(struct pci_dev *dev)
{
	struct irq_domain *irq_domain;
	struct device_node *dn, *tmp;
	const phandle *ph;
	struct axon_msic *msic = NULL;

	dn = of_node_get(pci_device_to_OF_node(dev));
	if (!dn) {
		dev_dbg(&dev->dev, "axon_msi: no pci_dn found\n");
		return NULL;
	}

	for (; dn; dn = of_get_next_parent(dn)) {
		ph = of_get_property(dn, "msi-translator", NULL);
		if (ph)
			break;
	}

	if (!ph) {
		dev_dbg(&dev->dev,
			"axon_msi: no msi-translator property found\n");
		goto out_error;
	}

	tmp = dn;
	dn = of_find_node_by_phandle(*ph);
	of_node_put(tmp);
	if (!dn) {
		dev_dbg(&dev->dev,
			"axon_msi: msi-translator doesn't point to a node\n");
		goto out_error;
	}

	irq_domain = irq_find_host(dn);
	if (!irq_domain) {
		dev_dbg(&dev->dev, "axon_msi: no irq_domain found for node %s\n",
			dn->full_name);
		goto out_error;
	}

	msic = irq_domain->host_data;

out_error:
	of_node_put(dn);

	return msic;
}

static int setup_msi_msg_address(struct pci_dev *dev, struct msi_msg *msg)
{
	struct device_node *dn;
	struct msi_desc *entry;
	int len;
	const u32 *prop;

	dn = of_node_get(pci_device_to_OF_node(dev));
	if (!dn) {
		dev_dbg(&dev->dev, "axon_msi: no pci_dn found\n");
		return -ENODEV;
	}

	entry = first_pci_msi_entry(dev);

	for (; dn; dn = of_get_next_parent(dn)) {
		if (entry->msi_attrib.is_64) {
			prop = of_get_property(dn, "msi-address-64", &len);
			if (prop)
				break;
		}

		prop = of_get_property(dn, "msi-address-32", &len);
		if (prop)
			break;
	}

	if (!prop) {
		dev_dbg(&dev->dev,
			"axon_msi: no msi-address-(32|64) properties found\n");
		return -ENOENT;
	}

	switch (len) {
	case 8:
		msg->address_hi = prop[0];
		msg->address_lo = prop[1];
		break;
	case 4:
		msg->address_hi = 0;
		msg->address_lo = prop[0];
		break;
	default:
		dev_dbg(&dev->dev,
			"axon_msi: malformed msi-address-(32|64) property\n");
		of_node_put(dn);
		return -EINVAL;
	}

	of_node_put(dn);

	return 0;
}

static int axon_msi_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	unsigned int virq, rc;
	struct msi_desc *entry;
	struct msi_msg msg;
	struct axon_msic *msic;

	msic = find_msi_translator(dev);
	if (!msic)
		return -ENODEV;

	rc = setup_msi_msg_address(dev, &msg);
	if (rc)
		return rc;

	for_each_pci_msi_entry(entry, dev) {
		virq = irq_create_direct_mapping(msic->irq_domain);
		if (virq == NO_IRQ) {
			dev_warn(&dev->dev,
				 "axon_msi: virq allocation failed!\n");
			return -1;
		}
		dev_dbg(&dev->dev, "axon_msi: allocated virq 0x%x\n", virq);

		irq_set_msi_desc(virq, entry);
		msg.data = virq;
		pci_write_msi_msg(virq, &msg);
	}

	return 0;
}

static void axon_msi_teardown_msi_irqs(struct pci_dev *dev)
{
	struct msi_desc *entry;

	dev_dbg(&dev->dev, "axon_msi: tearing down msi irqs\n");

	for_each_pci_msi_entry(entry, dev) {
		if (entry->irq == NO_IRQ)
			continue;

		irq_set_msi_desc(entry->irq, NULL);
		irq_dispose_mapping(entry->irq);
	}
}

static struct irq_chip msic_irq_chip = {
	.irq_mask	= pci_msi_mask_irq,
	.irq_unmask	= pci_msi_unmask_irq,
	.irq_shutdown	= pci_msi_mask_irq,
	.name		= "AXON-MSI",
};

static int msic_host_map(struct irq_domain *h, unsigned int virq,
			 irq_hw_number_t hw)
{
	irq_set_chip_data(virq, h->host_data);
	irq_set_chip_and_handler(virq, &msic_irq_chip, handle_simple_irq);

	return 0;
}

static const struct irq_domain_ops msic_host_ops = {
	.map	= msic_host_map,
};

static void axon_msi_shutdown(struct platform_device *device)
{
	struct axon_msic *msic = dev_get_drvdata(&device->dev);
	u32 tmp;

	pr_devel("axon_msi: disabling %s\n",
		 irq_domain_get_of_node(msic->irq_domain)->full_name);
	tmp  = dcr_read(msic->dcr_host, MSIC_CTRL_REG);
	tmp &= ~MSIC_CTRL_ENABLE & ~MSIC_CTRL_IRQ_ENABLE;
	msic_dcr_write(msic, MSIC_CTRL_REG, tmp);
}

static int axon_msi_probe(struct platform_device *device)
{
	struct device_node *dn = device->dev.of_node;
	struct axon_msic *msic;
	unsigned int virq;
	int dcr_base, dcr_len;

	pr_devel("axon_msi: setting up dn %s\n", dn->full_name);

	msic = kzalloc(sizeof(struct axon_msic), GFP_KERNEL);
	if (!msic) {
		printk(KERN_ERR "axon_msi: couldn't allocate msic for %s\n",
		       dn->full_name);
		goto out;
	}

	dcr_base = dcr_resource_start(dn, 0);
	dcr_len = dcr_resource_len(dn, 0);

	if (dcr_base == 0 || dcr_len == 0) {
		printk(KERN_ERR
		       "axon_msi: couldn't parse dcr properties on %s\n",
			dn->full_name);
		goto out_free_msic;
	}

	msic->dcr_host = dcr_map(dn, dcr_base, dcr_len);
	if (!DCR_MAP_OK(msic->dcr_host)) {
		printk(KERN_ERR "axon_msi: dcr_map failed for %s\n",
		       dn->full_name);
		goto out_free_msic;
	}

	msic->fifo_virt = dma_alloc_coherent(&device->dev, MSIC_FIFO_SIZE_BYTES,
					     &msic->fifo_phys, GFP_KERNEL);
	if (!msic->fifo_virt) {
		printk(KERN_ERR "axon_msi: couldn't allocate fifo for %s\n",
		       dn->full_name);
		goto out_free_msic;
	}

	virq = irq_of_parse_and_map(dn, 0);
	if (virq == NO_IRQ) {
		printk(KERN_ERR "axon_msi: irq parse and map failed for %s\n",
		       dn->full_name);
		goto out_free_fifo;
	}
	memset(msic->fifo_virt, 0xff, MSIC_FIFO_SIZE_BYTES);

	/* We rely on being able to stash a virq in a u16, so limit irqs to < 65536 */
	msic->irq_domain = irq_domain_add_nomap(dn, 65536, &msic_host_ops, msic);
	if (!msic->irq_domain) {
		printk(KERN_ERR "axon_msi: couldn't allocate irq_domain for %s\n",
		       dn->full_name);
		goto out_free_fifo;
	}

	irq_set_handler_data(virq, msic);
	irq_set_chained_handler(virq, axon_msi_cascade);
	pr_devel("axon_msi: irq 0x%x setup for axon_msi\n", virq);

	/* Enable the MSIC hardware */
	msic_dcr_write(msic, MSIC_BASE_ADDR_HI_REG, msic->fifo_phys >> 32);
	msic_dcr_write(msic, MSIC_BASE_ADDR_LO_REG,
				  msic->fifo_phys & 0xFFFFFFFF);
	msic_dcr_write(msic, MSIC_CTRL_REG,
			MSIC_CTRL_IRQ_ENABLE | MSIC_CTRL_ENABLE |
			MSIC_CTRL_FIFO_SIZE);

	msic->read_offset = dcr_read(msic->dcr_host, MSIC_WRITE_OFFSET_REG)
				& MSIC_FIFO_SIZE_MASK;

	dev_set_drvdata(&device->dev, msic);

	cell_pci_controller_ops.setup_msi_irqs = axon_msi_setup_msi_irqs;
	cell_pci_controller_ops.teardown_msi_irqs = axon_msi_teardown_msi_irqs;

	axon_msi_debug_setup(dn, msic);

	printk(KERN_DEBUG "axon_msi: setup MSIC on %s\n", dn->full_name);

	return 0;

out_free_fifo:
	dma_free_coherent(&device->dev, MSIC_FIFO_SIZE_BYTES, msic->fifo_virt,
			  msic->fifo_phys);
out_free_msic:
	kfree(msic);
out:

	return -1;
}

static const struct of_device_id axon_msi_device_id[] = {
	{
		.compatible	= "ibm,axon-msic"
	},
	{}
};

static struct platform_driver axon_msi_driver = {
	.probe		= axon_msi_probe,
	.shutdown	= axon_msi_shutdown,
	.driver = {
		.name = "axon-msi",
		.of_match_table = axon_msi_device_id,
	},
};

static int __init axon_msi_init(void)
{
	return platform_driver_register(&axon_msi_driver);
}
subsys_initcall(axon_msi_init);


#ifdef DEBUG
static int msic_set(void *data, u64 val)
{
	struct axon_msic *msic = data;
	out_le32(msic->trigger, val);
	return 0;
}

static int msic_get(void *data, u64 *val)
{
	*val = 0;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_msic, msic_get, msic_set, "%llu\n");

void axon_msi_debug_setup(struct device_node *dn, struct axon_msic *msic)
{
	char name[8];
	u64 addr;

	addr = of_translate_address(dn, of_get_property(dn, "reg", NULL));
	if (addr == OF_BAD_ADDR) {
		pr_devel("axon_msi: couldn't translate reg property\n");
		return;
	}

	msic->trigger = ioremap(addr, 0x4);
	if (!msic->trigger) {
		pr_devel("axon_msi: ioremap failed\n");
		return;
	}

	snprintf(name, sizeof(name), "msic_%d", of_node_to_nid(dn));

	if (!debugfs_create_file(name, 0600, powerpc_debugfs_root,
				 msic, &fops_msic)) {
		pr_devel("axon_msi: debugfs_create_file failed!\n");
		return;
	}
}
#endif /* DEBUG */
