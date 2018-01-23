// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017 IBM Corp.
#include <asm/pnv-ocxl.h>
#include <asm/opal.h>
#include "pci.h"

#define PNV_OCXL_TL_P9_RECV_CAP		0x000000000000000Full
/* PASIDs are 20-bit, but on P9, NPU can only handle 15 bits */
#define PNV_OCXL_PASID_BITS		15
#define PNV_OCXL_PASID_MAX		((1 << PNV_OCXL_PASID_BITS) - 1)


static void set_templ_rate(unsigned int templ, unsigned int rate, char *buf)
{
	int shift, idx;

	WARN_ON(templ > PNV_OCXL_TL_MAX_TEMPLATE);
	idx = (PNV_OCXL_TL_MAX_TEMPLATE - templ) / 2;
	shift = 4 * (1 - ((PNV_OCXL_TL_MAX_TEMPLATE - templ) % 2));
	buf[idx] |= rate << shift;
}

int pnv_ocxl_get_tl_cap(struct pci_dev *dev, long *cap,
			char *rate_buf, int rate_buf_size)
{
	if (rate_buf_size != PNV_OCXL_TL_RATE_BUF_SIZE)
		return -EINVAL;
	/*
	 * The TL capabilities are a characteristic of the NPU, so
	 * we go with hard-coded values.
	 *
	 * The receiving rate of each template is encoded on 4 bits.
	 *
	 * On P9:
	 * - templates 0 -> 3 are supported
	 * - templates 0, 1 and 3 have a 0 receiving rate
	 * - template 2 has receiving rate of 1 (extra cycle)
	 */
	memset(rate_buf, 0, rate_buf_size);
	set_templ_rate(2, 1, rate_buf);
	*cap = PNV_OCXL_TL_P9_RECV_CAP;
	return 0;
}
EXPORT_SYMBOL_GPL(pnv_ocxl_get_tl_cap);

int pnv_ocxl_set_tl_conf(struct pci_dev *dev, long cap,
			uint64_t rate_buf_phys, int rate_buf_size)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	int rc;

	if (rate_buf_size != PNV_OCXL_TL_RATE_BUF_SIZE)
		return -EINVAL;

	rc = opal_npu_tl_set(phb->opal_id, dev->devfn, cap,
			rate_buf_phys, rate_buf_size);
	if (rc) {
		dev_err(&dev->dev, "Can't configure host TL: %d\n", rc);
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(pnv_ocxl_set_tl_conf);

int pnv_ocxl_get_xsl_irq(struct pci_dev *dev, int *hwirq)
{
	int rc;

	rc = of_property_read_u32(dev->dev.of_node, "ibm,opal-xsl-irq", hwirq);
	if (rc) {
		dev_err(&dev->dev,
			"Can't get translation interrupt for device\n");
		return rc;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(pnv_ocxl_get_xsl_irq);

void pnv_ocxl_unmap_xsl_regs(void __iomem *dsisr, void __iomem *dar,
			void __iomem *tfc, void __iomem *pe_handle)
{
	iounmap(dsisr);
	iounmap(dar);
	iounmap(tfc);
	iounmap(pe_handle);
}
EXPORT_SYMBOL_GPL(pnv_ocxl_unmap_xsl_regs);

int pnv_ocxl_map_xsl_regs(struct pci_dev *dev, void __iomem **dsisr,
			void __iomem **dar, void __iomem **tfc,
			void __iomem **pe_handle)
{
	u64 reg;
	int i, j, rc = 0;
	void __iomem *regs[4];

	/*
	 * opal stores the mmio addresses of the DSISR, DAR, TFC and
	 * PE_HANDLE registers in a device tree property, in that
	 * order
	 */
	for (i = 0; i < 4; i++) {
		rc = of_property_read_u64_index(dev->dev.of_node,
						"ibm,opal-xsl-mmio", i, &reg);
		if (rc)
			break;
		regs[i] = ioremap(reg, 8);
		if (!regs[i]) {
			rc = -EINVAL;
			break;
		}
	}
	if (rc) {
		dev_err(&dev->dev, "Can't map translation mmio registers\n");
		for (j = i - 1; j >= 0; j--)
			iounmap(regs[j]);
	} else {
		*dsisr = regs[0];
		*dar = regs[1];
		*tfc = regs[2];
		*pe_handle = regs[3];
	}
	return rc;
}
EXPORT_SYMBOL_GPL(pnv_ocxl_map_xsl_regs);

struct spa_data {
	u64 phb_opal_id;
	u32 bdfn;
};

int pnv_ocxl_spa_setup(struct pci_dev *dev, void *spa_mem, int PE_mask,
		void **platform_data)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct spa_data *data;
	u32 bdfn;
	int rc;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	bdfn = (dev->bus->number << 8) | dev->devfn;
	rc = opal_npu_spa_setup(phb->opal_id, bdfn, virt_to_phys(spa_mem),
				PE_mask);
	if (rc) {
		dev_err(&dev->dev, "Can't setup Shared Process Area: %d\n", rc);
		kfree(data);
		return rc;
	}
	data->phb_opal_id = phb->opal_id;
	data->bdfn = bdfn;
	*platform_data = (void *) data;
	return 0;
}
EXPORT_SYMBOL_GPL(pnv_ocxl_spa_setup);

void pnv_ocxl_spa_release(void *platform_data)
{
	struct spa_data *data = (struct spa_data *) platform_data;
	int rc;

	rc = opal_npu_spa_setup(data->phb_opal_id, data->bdfn, 0, 0);
	WARN_ON(rc);
	kfree(data);
}
EXPORT_SYMBOL_GPL(pnv_ocxl_spa_release);

int pnv_ocxl_spa_remove_pe(void *platform_data, int pe_handle)
{
	struct spa_data *data = (struct spa_data *) platform_data;
	int rc;

	rc = opal_npu_spa_clear_cache(data->phb_opal_id, data->bdfn, pe_handle);
	return rc;
}
EXPORT_SYMBOL_GPL(pnv_ocxl_spa_remove_pe);
