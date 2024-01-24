// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/ipc_logging.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/msm_pcie.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#define PCIE_MSI_CTRL_BASE (0x820)
#define PCIE_MSI_CTRL_ADDR_OFFS (PCIE_MSI_CTRL_BASE)
#define PCIE_MSI_CTRL_UPPER_ADDR_OFFS (PCIE_MSI_CTRL_BASE + 0x4)
#define PCIE_MSI_CTRL_INT_N_EN_OFFS(n) (PCIE_MSI_CTRL_BASE + 0x8 + 0xc * (n))
#define PCIE_MSI_CTRL_INT_N_MASK_OFFS(n) (PCIE_MSI_CTRL_BASE + 0xc + 0xc * (n))
#define PCIE_MSI_CTRL_INT_N_STATUS_OFFS(n) \
	(PCIE_MSI_CTRL_BASE + 0x10 + 0xc * (n))

#define MSI_IRQ_PER_GRP (32)

enum msi_type {
	MSM_MSI_TYPE_QCOM,
	MSM_MSI_TYPE_SNPS,
};

struct msm_msi_irq {
	struct msm_msi_client *client;
	struct msm_msi_grp *grp; /* group the irq belongs to */
	u32 grp_index; /* index in the group */
	unsigned int hwirq; /* MSI controller hwirq */
	unsigned int virq; /* MSI controller virq */
	u32 pos; /* position in MSI bitmap */
};

struct msm_msi_grp {
	/* registers for SNPS only */
	void __iomem *int_en_reg;
	void __iomem *int_mask_reg;
	void __iomem *int_status_reg;
	u32 mask; /* tracks masked/unmasked MSI */

	struct msm_msi_irq irqs[MSI_IRQ_PER_GRP];
};

struct msm_msi {
	struct list_head clients;
	struct device *dev;
	struct device_node *of_node;
	int nr_hwirqs;
	int nr_virqs;
	int nr_grps;
	struct msm_msi_grp *grps;
	unsigned long *bitmap; /* tracks used/unused MSI */
	struct mutex mutex; /* mutex for modifying MSI client list and bitmap */
	struct irq_domain *inner_domain; /* parent domain; gen irq related */
	struct irq_domain *msi_domain; /* child domain; pci related */
	phys_addr_t msi_addr;
	u32 msi_addr_size;
	enum msi_type type;
	spinlock_t cfg_lock; /* lock for configuring Synopsys MSI registers */
	bool cfg_access; /* control access to MSI registers */
	void __iomem *pcie_cfg;

	void (*mask_irq)(struct irq_data *data);
	void (*unmask_irq)(struct irq_data *data);
};

/* structure for each client of MSI controller */
struct msm_msi_client {
	struct list_head node;
	struct msm_msi *msi;
	struct device *dev; /* client's dev of pci_dev */
	u32 nr_irqs; /* nr_irqs allocated for client */
	dma_addr_t msi_addr;
};

static void msm_msi_snps_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct msm_msi_grp *msi_grp;
	int i;
	u32 status, mask;

	chained_irq_enter(chip, desc);

	msi_grp = irq_desc_get_handler_data(desc);

	status = readl_relaxed(msi_grp->int_status_reg);

	/* always update the mask set in msm_msi_snps_mask_irq */
	mask = msi_grp->mask;
	writel_relaxed(mask, msi_grp->int_mask_reg);

	/* process only interrupts which are not masked */
	status ^= (status & mask);
	writel_relaxed(status, msi_grp->int_status_reg);

	for (i = 0; status; i++, status >>= 1)
		if (status & 0x1)
			generic_handle_irq(msi_grp->irqs[i].virq);

	chained_irq_exit(chip, desc);
}

static void msm_msi_qgic_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct msm_msi *msi;
	unsigned int virq;

	chained_irq_enter(chip, desc);

	msi = irq_desc_get_handler_data(desc);
	virq = irq_find_mapping(msi->inner_domain, irq_desc_get_irq(desc));

	generic_handle_irq(virq);

	chained_irq_exit(chip, desc);
}

static void msm_msi_snps_mask_irq(struct irq_data *data)
{
	struct msm_msi_irq *msi_irq = irq_data_get_irq_chip_data(data);
	struct msm_msi_grp *msi_grp = msi_irq->grp;
	struct msm_msi *msi = msi_irq->client->msi;
	unsigned long flags;

	spin_lock_irqsave(&msi->cfg_lock, flags);
	msi_grp->mask |= BIT(msi_irq->grp_index);
	spin_unlock_irqrestore(&msi->cfg_lock, flags);
}

static void msm_msi_qgic_mask_irq(struct irq_data *data)
{
	struct irq_data *parent_data;

	parent_data = irq_get_irq_data(irqd_to_hwirq(data));
	if (!parent_data || !parent_data->chip)
		return;

	parent_data->chip->irq_mask(parent_data);
}

static void msm_msi_mask_irq(struct irq_data *data)
{
	struct irq_data *parent_data;
	struct msm_msi_irq *msi_irq;
	struct msm_msi *msi;
	unsigned long flags;

	parent_data = data->parent_data;
	if (!parent_data)
		return;

	msi_irq = irq_data_get_irq_chip_data(parent_data);
	msi = msi_irq->client->msi;

	spin_lock_irqsave(&msi->cfg_lock, flags);
	if (msi->cfg_access)
		pci_msi_mask_irq(data);
	spin_unlock_irqrestore(&msi->cfg_lock, flags);

	msi->mask_irq(parent_data);
}

static void msm_msi_snps_unmask_irq(struct irq_data *data)
{
	struct msm_msi_irq *msi_irq = irq_data_get_irq_chip_data(data);
	struct msm_msi_grp *msi_grp = msi_irq->grp;
	struct msm_msi *msi = msi_irq->client->msi;
	unsigned long flags;

	spin_lock_irqsave(&msi->cfg_lock, flags);

	msi_grp->mask &= ~BIT(msi_irq->grp_index);
	if (msi->cfg_access)
		writel_relaxed(msi_grp->mask, msi_grp->int_mask_reg);

	spin_unlock_irqrestore(&msi->cfg_lock, flags);
}

static void msm_msi_qgic_unmask_irq(struct irq_data *data)
{
	struct irq_data *parent_data;

	parent_data = irq_get_irq_data(irqd_to_hwirq(data));
	if (!parent_data || !parent_data->chip)
		return;

	parent_data->chip->irq_unmask(parent_data);
}

static void msm_msi_unmask_irq(struct irq_data *data)
{
	struct irq_data *parent_data;
	struct msm_msi_irq *msi_irq;
	struct msm_msi *msi;
	unsigned long flags;

	parent_data = data->parent_data;
	if (!parent_data)
		return;

	msi_irq = irq_data_get_irq_chip_data(parent_data);
	msi = msi_irq->client->msi;

	msi->unmask_irq(parent_data);

	spin_lock_irqsave(&msi->cfg_lock, flags);
	if (msi->cfg_access)
		pci_msi_unmask_irq(data);
	spin_unlock_irqrestore(&msi->cfg_lock, flags);
}

static struct irq_chip msm_msi_irq_chip = {
	.name = "gic_msm_pci_msi",
	.irq_enable = msm_msi_unmask_irq,
	.irq_disable = msm_msi_mask_irq,
	.irq_mask = msm_msi_mask_irq,
	.irq_unmask = msm_msi_unmask_irq,
};

static int msm_msi_domain_prepare(struct irq_domain *domain, struct device *dev,
				int nvec, msi_alloc_info_t *arg)
{
	struct msm_msi *msi = domain->parent->host_data;
	struct msm_msi_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	client->msi = msi;
	client->dev = dev;
	client->msi_addr = msi->msi_addr;

	/*
	 * Accesses to QGIC MSI doorbell register goes through PCIe SMMU and
	 * needs to be mapped. Synopsys MSI doorbell is within the PCIe core
	 * and does not need to be mapped.
	 */
	if (msi->type == MSM_MSI_TYPE_QCOM) {
		client->msi_addr = dma_map_resource(client->dev, msi->msi_addr,
					msi->msi_addr_size, DMA_FROM_DEVICE, 0);
		if (dma_mapping_error(client->dev, client->msi_addr)) {
			dev_err(msi->dev, "MSI: failed to map msi address\n");
			client->msi_addr = 0;
			kfree(client);
			return -ENOMEM;
		}
	}

	mutex_lock(&msi->mutex);
	list_add_tail(&client->node, &msi->clients);
	mutex_unlock(&msi->mutex);

	/* zero out struct for framework */
	memset(arg, 0, sizeof(*arg));

	return 0;
}

static struct msi_domain_ops msm_msi_domain_ops = {
	.msi_prepare = msm_msi_domain_prepare,
};

static struct msi_domain_info msm_msi_domain_info = {
	.flags = MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		MSI_FLAG_MULTI_PCI_MSI | MSI_FLAG_PCI_MSIX,
	.ops = &msm_msi_domain_ops,
	.chip = &msm_msi_irq_chip,
};

static int msm_msi_irq_set_affinity(struct irq_data *data,
				      const struct cpumask *mask, bool force)
{
	struct irq_data *parent_data = irq_get_irq_data(irqd_to_hwirq(data));

	if (!parent_data)
		return -ENODEV;

	/* set affinity for MSM MSI HW IRQ */
	if (parent_data->chip->irq_set_affinity)
		return parent_data->chip->irq_set_affinity(parent_data,
				mask, force);

	return -EINVAL;
}

static void msm_msi_irq_compose_msi_msg(struct irq_data *data,
					  struct msi_msg *msg)
{
	struct msm_msi_irq *msi_irq = irq_data_get_irq_chip_data(data);
	struct irq_data *parent_data = irq_get_irq_data(irqd_to_hwirq(data));
	struct msm_msi_client *client = msi_irq->client;
	struct msm_msi *msi = client->msi;

	if (!parent_data)
		return;

	msg->address_lo = lower_32_bits(client->msi_addr);
	msg->address_hi = upper_32_bits(client->msi_addr);

	msg->data = (msi->type == MSM_MSI_TYPE_QCOM) ?
			irqd_to_hwirq(parent_data) : msi_irq->pos;
}

static struct irq_chip msm_msi_bottom_irq_chip = {
	.name = "msm_msi",
	.irq_set_affinity = msm_msi_irq_set_affinity,
	.irq_compose_msi_msg = msm_msi_irq_compose_msi_msg,
};

static int msm_msi_irq_domain_alloc(struct irq_domain *domain,
				      unsigned int virq, unsigned int nr_irqs,
				      void *args)
{
	struct msm_msi *msi = domain->host_data;
	struct msm_msi_client *tmp, *client = NULL;
	struct device *dev = ((msi_alloc_info_t *)args)->desc->dev;
	int i, ret = 0;
	int pos;

	mutex_lock(&msi->mutex);
	list_for_each_entry(tmp, &msi->clients, node) {
		if (tmp->dev == dev) {
			client = tmp;
			break;
		}
	}

	if (!client) {
		dev_err(msi->dev, "MSI: failed to find client dev\n");
		ret = -ENODEV;
		goto out;
	}

	pos = bitmap_find_next_zero_area(msi->bitmap, msi->nr_virqs, 0,
					nr_irqs, nr_irqs - 1);
	if (pos < msi->nr_virqs) {
		bitmap_set(msi->bitmap, pos, nr_irqs);
	} else {
		ret = -ENOSPC;
		goto out;
	}

	for (i = 0; i < nr_irqs; i++) {
		u32 grp = pos / MSI_IRQ_PER_GRP;
		u32 index = pos % MSI_IRQ_PER_GRP;
		struct msm_msi_irq *msi_irq = &msi->grps[grp].irqs[index];

		msi_irq->virq = virq + i;
		msi_irq->client = client;
		irq_domain_set_info(domain, msi_irq->virq,
				msi_irq->hwirq,
				&msm_msi_bottom_irq_chip, msi_irq,
				handle_simple_irq, NULL, NULL);
		client->nr_irqs++;
		pos++;
	}

out:
	mutex_unlock(&msi->mutex);
	return ret;
}

static void msm_msi_irq_domain_free(struct irq_domain *domain,
				      unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *data = irq_domain_get_irq_data(domain, virq);
	struct msm_msi_irq *msi_irq;
	struct msm_msi_client *client;
	struct msm_msi *msi;

	if (!data)
		return;

	msi_irq = irq_data_get_irq_chip_data(data);
	client  = msi_irq->client;
	msi = client->msi;

	mutex_lock(&msi->mutex);

	bitmap_clear(msi->bitmap, msi_irq->pos, nr_irqs);
	client->nr_irqs -= nr_irqs;

	if (!client->nr_irqs) {
		if (msi->type == MSM_MSI_TYPE_QCOM)
			dma_unmap_resource(client->dev, client->msi_addr,
					PAGE_SIZE, DMA_FROM_DEVICE, 0);
		list_del(&client->node);
		kfree(client);
	}

	mutex_unlock(&msi->mutex);

	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
}

static const struct irq_domain_ops msi_domain_ops = {
	.alloc = msm_msi_irq_domain_alloc,
	.free = msm_msi_irq_domain_free,
};

static int msm_msi_alloc_domains(struct msm_msi *msi)
{
	msi->inner_domain = irq_domain_add_linear(NULL, msi->nr_virqs,
						  &msi_domain_ops, msi);
	if (!msi->inner_domain) {
		dev_err(msi->dev, "MSI: failed to create IRQ domain\n");
		return -ENOMEM;
	}

	msi->msi_domain = pci_msi_create_irq_domain(
					of_node_to_fwnode(msi->of_node),
					&msm_msi_domain_info,
					msi->inner_domain);
	if (!msi->msi_domain) {
		dev_err(msi->dev, "MSI: failed to create MSI domain\n");
		irq_domain_remove(msi->inner_domain);
		return -ENOMEM;
	}

	return 0;
}

static int msm_msi_snps_irq_setup(struct msm_msi *msi)
{
	int i, index, ret;
	struct msm_msi_grp *msi_grp;
	struct msm_msi_irq *msi_irq;
	unsigned int irq = 0;

	/* setup each MSI group. nr_hwirqs == nr_grps */
	for (i = 0; i < msi->nr_hwirqs; i++) {
		irq = irq_of_parse_and_map(msi->of_node, i);
		if (!irq) {
			dev_err(msi->dev,
				"MSI: failed to parse/map interrupt\n");
			ret = -ENODEV;
			goto free_irqs;
		}

		ret = enable_irq_wake(irq);
		if (ret) {
			dev_err(msi->dev,
				"MSI: Unable to set enable_irq_wake for interrupt: %d: %d\n",
				i, irq);
			goto free_irq;
		}

		msi_grp = &msi->grps[i];
		msi_grp->int_en_reg = msi->pcie_cfg +
				PCIE_MSI_CTRL_INT_N_EN_OFFS(i);
		msi_grp->int_mask_reg = msi->pcie_cfg +
				PCIE_MSI_CTRL_INT_N_MASK_OFFS(i);
		msi_grp->int_status_reg = msi->pcie_cfg +
				PCIE_MSI_CTRL_INT_N_STATUS_OFFS(i);

		for (index = 0; index < MSI_IRQ_PER_GRP; index++) {
			msi_irq = &msi_grp->irqs[index];

			msi_irq->grp = msi_grp;
			msi_irq->grp_index = index;
			msi_irq->pos = (i * MSI_IRQ_PER_GRP) + index;
			msi_irq->hwirq = irq;
		}

		irq_set_chained_handler_and_data(irq, msm_msi_snps_handler,
						msi_grp);
	}

	return 0;

free_irq:
	irq_dispose_mapping(irq);
free_irqs:
	for (--i; i >= 0; i--) {
		irq = msi->grps[i].irqs[0].hwirq;

		irq_set_chained_handler_and_data(irq, NULL, NULL);
		disable_irq_wake(irq);
		irq_dispose_mapping(irq);
	}

	return ret;
}

static int msm_msi_qgic_irq_setup(struct msm_msi *msi)
{
	int i, ret;
	u32 index, grp;
	struct msm_msi_grp *msi_grp;
	struct msm_msi_irq *msi_irq;
	unsigned int irq = 0;

	for (i = 0; i < msi->nr_hwirqs; i++) {
		irq = irq_of_parse_and_map(msi->of_node, i);
		if (!irq) {
			dev_err(msi->dev,
				"MSI: failed to parse/map interrupt\n");
			ret = -ENODEV;
			goto free_irqs;
		}

		ret = enable_irq_wake(irq);
		if (ret) {
			dev_err(msi->dev,
				"MSI: Unable to set enable_irq_wake for interrupt: %d: %d\n",
				i, irq);
			goto free_irq;
		}

		grp = i / MSI_IRQ_PER_GRP;
		index = i % MSI_IRQ_PER_GRP;
		msi_grp = &msi->grps[grp];
		msi_irq = &msi_grp->irqs[index];

		msi_irq->grp = msi_grp;
		msi_irq->grp_index = index;
		msi_irq->pos = i;
		msi_irq->hwirq = irq;

		irq_set_chained_handler_and_data(irq, msm_msi_qgic_handler,
						msi);
	}

	return 0;

free_irq:
	irq_dispose_mapping(irq);
free_irqs:
	for (--i; i >= 0; i--) {
		grp = i / MSI_IRQ_PER_GRP;
		index = i % MSI_IRQ_PER_GRP;
		irq = msi->grps[grp].irqs[index].hwirq;

		irq_set_chained_handler_and_data(irq, NULL, NULL);
		disable_irq_wake(irq);
		irq_dispose_mapping(irq);
	}

	return ret;
}

/* control access to PCIe MSI registers */
void msm_msi_config_access(struct irq_domain *domain, bool allow)
{
	struct msm_msi *msi = domain->parent->host_data;
	unsigned long flags;

	spin_lock_irqsave(&msi->cfg_lock, flags);
	msi->cfg_access = allow;
	spin_unlock_irqrestore(&msi->cfg_lock, flags);
}
EXPORT_SYMBOL(msm_msi_config_access);

void msm_msi_config(struct irq_domain *domain)
{
	struct msm_msi *msi;
	int i;

	msi = domain->parent->host_data;

	/* PCIe core driver sets to false during LPM */
	msm_msi_config_access(domain, true);

	if (msi->type == MSM_MSI_TYPE_QCOM)
		return;

	/* program Synopsys MSI termination address */
	writel_relaxed(msi->msi_addr, msi->pcie_cfg + PCIE_MSI_CTRL_ADDR_OFFS);
	writel_relaxed(0, msi->pcie_cfg + PCIE_MSI_CTRL_UPPER_ADDR_OFFS);

	/* restore mask and enable all interrupts for each group */
	for (i = 0; i < msi->nr_grps; i++) {
		struct msm_msi_grp *msi_grp = &msi->grps[i];

		writel_relaxed(msi_grp->mask, msi_grp->int_mask_reg);
		writel_relaxed(~0, msi_grp->int_en_reg);
	}
}
EXPORT_SYMBOL(msm_msi_config);

int msm_msi_init(struct device *dev)
{
	int ret;
	struct msm_msi *msi;
	struct device_node *of_node;
	const __be32 *prop_val;
	struct of_phandle_args irq;
	u32 size_exp = 0;
	struct resource *res;
	int (*msi_irq_setup)(struct msm_msi *msi);

	if (!dev->of_node) {
		dev_err(dev, "MSI: missing DT node\n");
		return -EINVAL;
	}

	of_node = of_parse_phandle(dev->of_node, "msi-parent", 0);
	if (!of_node) {
		dev_err(dev, "MSI: no phandle for MSI found\n");
		return -ENODEV;
	}

	if (!of_device_is_compatible(of_node, "qcom,pci-msi")) {
		dev_err(dev, "MSI: no compatible qcom,pci-msi found\n");
		ret = -ENODEV;
		goto err;
	}

	if (!of_find_property(of_node, "msi-controller", NULL)) {
		ret = -ENODEV;
		goto err;
	}

	msi = kzalloc(sizeof(*msi), GFP_KERNEL);
	if (!msi) {
		ret = -ENOMEM;
		goto err;
	}

	msi->dev = dev;
	msi->of_node = of_node;
	mutex_init(&msi->mutex);
	spin_lock_init(&msi->cfg_lock);
	INIT_LIST_HEAD(&msi->clients);

	prop_val = of_get_address(msi->of_node, 0, NULL, NULL);
	if (!prop_val) {
		dev_err(msi->dev, "MSI: missing 'reg' devicetree\n");
		ret = -EINVAL;
		goto err;
	}

	msi->msi_addr = be32_to_cpup(prop_val);
	if (!msi->msi_addr) {
		dev_err(msi->dev, "MSI: failed to get MSI address\n");
		ret = -EINVAL;
		goto err;
	}

	of_property_read_u32(of_node, "qcom,msi-addr-size-exp", &size_exp);

	size_exp = (size_exp > PAGE_SHIFT) ? size_exp : PAGE_SHIFT;

	msi->msi_addr_size = 1 << size_exp;

	msi->type = of_property_read_bool(msi->of_node, "qcom,snps") ?
			MSM_MSI_TYPE_SNPS : MSM_MSI_TYPE_QCOM;
	dev_info(msi->dev, "MSI: %s controller is present\n",
		msi->type == MSM_MSI_TYPE_SNPS ? "synopsys" : "qgic");

	while (of_irq_parse_one(msi->of_node, msi->nr_hwirqs, &irq) == 0)
		msi->nr_hwirqs++;

	if (!msi->nr_hwirqs) {
		dev_err(msi->dev, "MSI: found no MSI interrupts\n");
		ret = -ENODEV;
		goto err;
	}

	if (msi->type == MSM_MSI_TYPE_SNPS) {
		res = platform_get_resource_byname(to_platform_device(dev),
						IORESOURCE_MEM, "dm_core");
		if (!res) {
			dev_err(msi->dev,
				"MSI: failed to get PCIe register base\n");
			ret = -ENODEV;
			goto err;
		}

		msi->pcie_cfg = ioremap(res->start, resource_size(res));
		if (!msi->pcie_cfg) {
			ret = -ENOMEM;
			goto free_msi;
		}

		msi->nr_virqs = msi->nr_hwirqs * MSI_IRQ_PER_GRP;
		msi->nr_grps = msi->nr_hwirqs;
		msi->mask_irq = msm_msi_snps_mask_irq;
		msi->unmask_irq = msm_msi_snps_unmask_irq;
		msi_irq_setup = msm_msi_snps_irq_setup;
	} else {
		msi->nr_virqs = msi->nr_hwirqs;
		msi->nr_grps = 1;
		msi->mask_irq = msm_msi_qgic_mask_irq;
		msi->unmask_irq = msm_msi_qgic_unmask_irq;
		msi_irq_setup = msm_msi_qgic_irq_setup;
	}

	msi->grps = kcalloc(msi->nr_grps, sizeof(*msi->grps), GFP_KERNEL);
	if (!msi->grps) {
		ret = -ENOMEM;
		goto unmap_cfg;
	}

	msi->bitmap = kcalloc(BITS_TO_LONGS(msi->nr_virqs),
			      sizeof(*msi->bitmap), GFP_KERNEL);
	if (!msi->bitmap) {
		ret = -ENOMEM;
		goto free_grps;
	}

	ret = msm_msi_alloc_domains(msi);
	if (ret) {
		dev_err(msi->dev, "MSI: failed to allocate MSI domains\n");
		goto free_bitmap;
	}

	ret = msi_irq_setup(msi);
	if (ret)
		goto remove_domains;

	msm_msi_config(msi->msi_domain);

	return 0;

remove_domains:
	irq_domain_remove(msi->msi_domain);
	irq_domain_remove(msi->inner_domain);
free_bitmap:
	kfree(msi->bitmap);
free_grps:
	kfree(msi->grps);
unmap_cfg:
	iounmap(msi->pcie_cfg);
free_msi:
	kfree(msi);
err:
	of_node_put(of_node);

	return ret;
}
EXPORT_SYMBOL(msm_msi_init);
