// SPDX-License-Identifier: GPL-2.0
/*
 * PCI EPF driver for MHI Endpoint devices
 *
 * Copyright (C) 2023 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/mhi_ep.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pci-epc.h>
#include <linux/pci-epf.h>

#define MHI_VERSION_1_0 0x01000000

#define to_epf_mhi(cntrl) container_of(cntrl, struct pci_epf_mhi, cntrl)

struct pci_epf_mhi_ep_info {
	const struct mhi_ep_cntrl_config *config;
	struct pci_epf_header *epf_header;
	enum pci_barno bar_num;
	u32 epf_flags;
	u32 msi_count;
	u32 mru;
};

#define MHI_EP_CHANNEL_CONFIG(ch_num, ch_name, direction)	\
	{							\
		.num = ch_num,					\
		.name = ch_name,				\
		.dir = direction,				\
	}

#define MHI_EP_CHANNEL_CONFIG_UL(ch_num, ch_name)		\
	MHI_EP_CHANNEL_CONFIG(ch_num, ch_name, DMA_TO_DEVICE)

#define MHI_EP_CHANNEL_CONFIG_DL(ch_num, ch_name)		\
	MHI_EP_CHANNEL_CONFIG(ch_num, ch_name, DMA_FROM_DEVICE)

static const struct mhi_ep_channel_config mhi_v1_channels[] = {
	MHI_EP_CHANNEL_CONFIG_UL(0, "LOOPBACK"),
	MHI_EP_CHANNEL_CONFIG_DL(1, "LOOPBACK"),
	MHI_EP_CHANNEL_CONFIG_UL(2, "SAHARA"),
	MHI_EP_CHANNEL_CONFIG_DL(3, "SAHARA"),
	MHI_EP_CHANNEL_CONFIG_UL(4, "DIAG"),
	MHI_EP_CHANNEL_CONFIG_DL(5, "DIAG"),
	MHI_EP_CHANNEL_CONFIG_UL(6, "SSR"),
	MHI_EP_CHANNEL_CONFIG_DL(7, "SSR"),
	MHI_EP_CHANNEL_CONFIG_UL(8, "QDSS"),
	MHI_EP_CHANNEL_CONFIG_DL(9, "QDSS"),
	MHI_EP_CHANNEL_CONFIG_UL(10, "EFS"),
	MHI_EP_CHANNEL_CONFIG_DL(11, "EFS"),
	MHI_EP_CHANNEL_CONFIG_UL(12, "MBIM"),
	MHI_EP_CHANNEL_CONFIG_DL(13, "MBIM"),
	MHI_EP_CHANNEL_CONFIG_UL(14, "QMI"),
	MHI_EP_CHANNEL_CONFIG_DL(15, "QMI"),
	MHI_EP_CHANNEL_CONFIG_UL(16, "QMI"),
	MHI_EP_CHANNEL_CONFIG_DL(17, "QMI"),
	MHI_EP_CHANNEL_CONFIG_UL(18, "IP-CTRL-1"),
	MHI_EP_CHANNEL_CONFIG_DL(19, "IP-CTRL-1"),
	MHI_EP_CHANNEL_CONFIG_UL(20, "IPCR"),
	MHI_EP_CHANNEL_CONFIG_DL(21, "IPCR"),
	MHI_EP_CHANNEL_CONFIG_UL(32, "DUN"),
	MHI_EP_CHANNEL_CONFIG_DL(33, "DUN"),
	MHI_EP_CHANNEL_CONFIG_UL(46, "IP_SW0"),
	MHI_EP_CHANNEL_CONFIG_DL(47, "IP_SW0"),
};

static const struct mhi_ep_cntrl_config mhi_v1_config = {
	.max_channels = 128,
	.num_channels = ARRAY_SIZE(mhi_v1_channels),
	.ch_cfg = mhi_v1_channels,
	.mhi_version = MHI_VERSION_1_0,
};

static struct pci_epf_header sdx55_header = {
	.vendorid = PCI_VENDOR_ID_QCOM,
	.deviceid = 0x0306,
	.baseclass_code = PCI_BASE_CLASS_COMMUNICATION,
	.subclass_code = PCI_CLASS_COMMUNICATION_MODEM & 0xff,
	.interrupt_pin	= PCI_INTERRUPT_INTA,
};

static const struct pci_epf_mhi_ep_info sdx55_info = {
	.config = &mhi_v1_config,
	.epf_header = &sdx55_header,
	.bar_num = BAR_0,
	.epf_flags = PCI_BASE_ADDRESS_MEM_TYPE_32,
	.msi_count = 32,
	.mru = 0x8000,
};

struct pci_epf_mhi {
	const struct pci_epf_mhi_ep_info *info;
	struct mhi_ep_cntrl mhi_cntrl;
	struct pci_epf *epf;
	struct mutex lock;
	void __iomem *mmio;
	resource_size_t mmio_phys;
	u32 mmio_size;
	int irq;
};

static int __pci_epf_mhi_alloc_map(struct mhi_ep_cntrl *mhi_cntrl, u64 pci_addr,
				 phys_addr_t *paddr, void __iomem **vaddr,
				 size_t offset, size_t size)
{
	struct pci_epf_mhi *epf_mhi = to_epf_mhi(mhi_cntrl);
	struct pci_epf *epf = epf_mhi->epf;
	struct pci_epc *epc = epf->epc;
	int ret;

	*vaddr = pci_epc_mem_alloc_addr(epc, paddr, size + offset);
	if (!*vaddr)
		return -ENOMEM;

	ret = pci_epc_map_addr(epc, epf->func_no, epf->vfunc_no, *paddr,
			       pci_addr - offset, size + offset);
	if (ret) {
		pci_epc_mem_free_addr(epc, *paddr, *vaddr, size + offset);
		return ret;
	}

	*paddr = *paddr + offset;
	*vaddr = *vaddr + offset;

	return 0;
}

static int pci_epf_mhi_alloc_map(struct mhi_ep_cntrl *mhi_cntrl, u64 pci_addr,
				 phys_addr_t *paddr, void __iomem **vaddr,
				 size_t size)
{
	struct pci_epf_mhi *epf_mhi = to_epf_mhi(mhi_cntrl);
	struct pci_epc *epc = epf_mhi->epf->epc;
	size_t offset = pci_addr & (epc->mem->window.page_size - 1);

	return __pci_epf_mhi_alloc_map(mhi_cntrl, pci_addr, paddr, vaddr,
				      offset, size);
}

static void __pci_epf_mhi_unmap_free(struct mhi_ep_cntrl *mhi_cntrl,
				     u64 pci_addr, phys_addr_t paddr,
				     void __iomem *vaddr, size_t offset,
				     size_t size)
{
	struct pci_epf_mhi *epf_mhi = to_epf_mhi(mhi_cntrl);
	struct pci_epf *epf = epf_mhi->epf;
	struct pci_epc *epc = epf->epc;

	pci_epc_unmap_addr(epc, epf->func_no, epf->vfunc_no, paddr - offset);
	pci_epc_mem_free_addr(epc, paddr - offset, vaddr - offset,
			      size + offset);
}

static void pci_epf_mhi_unmap_free(struct mhi_ep_cntrl *mhi_cntrl, u64 pci_addr,
				   phys_addr_t paddr, void __iomem *vaddr,
				   size_t size)
{
	struct pci_epf_mhi *epf_mhi = to_epf_mhi(mhi_cntrl);
	struct pci_epf *epf = epf_mhi->epf;
	struct pci_epc *epc = epf->epc;
	size_t offset = pci_addr & (epc->mem->window.page_size - 1);

	__pci_epf_mhi_unmap_free(mhi_cntrl, pci_addr, paddr, vaddr, offset,
				 size);
}

static void pci_epf_mhi_raise_irq(struct mhi_ep_cntrl *mhi_cntrl, u32 vector)
{
	struct pci_epf_mhi *epf_mhi = to_epf_mhi(mhi_cntrl);
	struct pci_epf *epf = epf_mhi->epf;
	struct pci_epc *epc = epf->epc;

	/*
	 * MHI supplies 0 based MSI vectors but the API expects the vector
	 * number to start from 1, so we need to increment the vector by 1.
	 */
	pci_epc_raise_irq(epc, epf->func_no, epf->vfunc_no, PCI_EPC_IRQ_MSI,
			  vector + 1);
}

static int pci_epf_mhi_read_from_host(struct mhi_ep_cntrl *mhi_cntrl, u64 from,
				      void *to, size_t size)
{
	struct pci_epf_mhi *epf_mhi = to_epf_mhi(mhi_cntrl);
	size_t offset = from % SZ_4K;
	void __iomem *tre_buf;
	phys_addr_t tre_phys;
	int ret;

	mutex_lock(&epf_mhi->lock);

	ret = __pci_epf_mhi_alloc_map(mhi_cntrl, from, &tre_phys, &tre_buf,
				      offset, size);
	if (ret) {
		mutex_unlock(&epf_mhi->lock);
		return ret;
	}

	memcpy_fromio(to, tre_buf, size);

	__pci_epf_mhi_unmap_free(mhi_cntrl, from, tre_phys, tre_buf, offset,
				 size);

	mutex_unlock(&epf_mhi->lock);

	return 0;
}

static int pci_epf_mhi_write_to_host(struct mhi_ep_cntrl *mhi_cntrl,
				     void *from, u64 to, size_t size)
{
	struct pci_epf_mhi *epf_mhi = to_epf_mhi(mhi_cntrl);
	size_t offset = to % SZ_4K;
	void __iomem *tre_buf;
	phys_addr_t tre_phys;
	int ret;

	mutex_lock(&epf_mhi->lock);

	ret = __pci_epf_mhi_alloc_map(mhi_cntrl, to, &tre_phys, &tre_buf,
				      offset, size);
	if (ret) {
		mutex_unlock(&epf_mhi->lock);
		return ret;
	}

	memcpy_toio(tre_buf, from, size);

	__pci_epf_mhi_unmap_free(mhi_cntrl, to, tre_phys, tre_buf, offset,
				 size);

	mutex_unlock(&epf_mhi->lock);

	return 0;
}

static int pci_epf_mhi_core_init(struct pci_epf *epf)
{
	struct pci_epf_mhi *epf_mhi = epf_get_drvdata(epf);
	const struct pci_epf_mhi_ep_info *info = epf_mhi->info;
	struct pci_epf_bar *epf_bar = &epf->bar[info->bar_num];
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;
	int ret;

	epf_bar->phys_addr = epf_mhi->mmio_phys;
	epf_bar->size = epf_mhi->mmio_size;
	epf_bar->barno = info->bar_num;
	epf_bar->flags = info->epf_flags;
	ret = pci_epc_set_bar(epc, epf->func_no, epf->vfunc_no, epf_bar);
	if (ret) {
		dev_err(dev, "Failed to set BAR: %d\n", ret);
		return ret;
	}

	ret = pci_epc_set_msi(epc, epf->func_no, epf->vfunc_no,
			      order_base_2(info->msi_count));
	if (ret) {
		dev_err(dev, "Failed to set MSI configuration: %d\n", ret);
		return ret;
	}

	ret = pci_epc_write_header(epc, epf->func_no, epf->vfunc_no,
				   epf->header);
	if (ret) {
		dev_err(dev, "Failed to set Configuration header: %d\n", ret);
		return ret;
	}

	return 0;
}

static int pci_epf_mhi_link_up(struct pci_epf *epf)
{
	struct pci_epf_mhi *epf_mhi = epf_get_drvdata(epf);
	const struct pci_epf_mhi_ep_info *info = epf_mhi->info;
	struct mhi_ep_cntrl *mhi_cntrl = &epf_mhi->mhi_cntrl;
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;
	int ret;

	mhi_cntrl->mmio = epf_mhi->mmio;
	mhi_cntrl->irq = epf_mhi->irq;
	mhi_cntrl->mru = info->mru;

	/* Assign the struct dev of PCI EP as MHI controller device */
	mhi_cntrl->cntrl_dev = epc->dev.parent;
	mhi_cntrl->raise_irq = pci_epf_mhi_raise_irq;
	mhi_cntrl->alloc_map = pci_epf_mhi_alloc_map;
	mhi_cntrl->unmap_free = pci_epf_mhi_unmap_free;
	mhi_cntrl->read_from_host = pci_epf_mhi_read_from_host;
	mhi_cntrl->write_to_host = pci_epf_mhi_write_to_host;

	/* Register the MHI EP controller */
	ret = mhi_ep_register_controller(mhi_cntrl, info->config);
	if (ret) {
		dev_err(dev, "Failed to register MHI EP controller: %d\n", ret);
		return ret;
	}

	return 0;
}

static int pci_epf_mhi_link_down(struct pci_epf *epf)
{
	struct pci_epf_mhi *epf_mhi = epf_get_drvdata(epf);
	struct mhi_ep_cntrl *mhi_cntrl = &epf_mhi->mhi_cntrl;

	if (mhi_cntrl->mhi_dev) {
		mhi_ep_power_down(mhi_cntrl);
		mhi_ep_unregister_controller(mhi_cntrl);
	}

	return 0;
}

static int pci_epf_mhi_bme(struct pci_epf *epf)
{
	struct pci_epf_mhi *epf_mhi = epf_get_drvdata(epf);
	struct mhi_ep_cntrl *mhi_cntrl = &epf_mhi->mhi_cntrl;
	struct device *dev = &epf->dev;
	int ret;

	/*
	 * Power up the MHI EP stack if link is up and stack is in power down
	 * state.
	 */
	if (!mhi_cntrl->enabled && mhi_cntrl->mhi_dev) {
		ret = mhi_ep_power_up(mhi_cntrl);
		if (ret) {
			dev_err(dev, "Failed to power up MHI EP: %d\n", ret);
			mhi_ep_unregister_controller(mhi_cntrl);
		}
	}

	return 0;
}

static int pci_epf_mhi_bind(struct pci_epf *epf)
{
	struct pci_epf_mhi *epf_mhi = epf_get_drvdata(epf);
	struct pci_epc *epc = epf->epc;
	struct platform_device *pdev = to_platform_device(epc->dev.parent);
	struct resource *res;
	int ret;

	/* Get MMIO base address from Endpoint controller */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mmio");
	epf_mhi->mmio_phys = res->start;
	epf_mhi->mmio_size = resource_size(res);

	epf_mhi->mmio = ioremap(epf_mhi->mmio_phys, epf_mhi->mmio_size);
	if (!epf_mhi->mmio)
		return -ENOMEM;

	ret = platform_get_irq_byname(pdev, "doorbell");
	if (ret < 0) {
		iounmap(epf_mhi->mmio);
		return ret;
	}

	epf_mhi->irq = ret;

	return 0;
}

static void pci_epf_mhi_unbind(struct pci_epf *epf)
{
	struct pci_epf_mhi *epf_mhi = epf_get_drvdata(epf);
	const struct pci_epf_mhi_ep_info *info = epf_mhi->info;
	struct pci_epf_bar *epf_bar = &epf->bar[info->bar_num];
	struct mhi_ep_cntrl *mhi_cntrl = &epf_mhi->mhi_cntrl;
	struct pci_epc *epc = epf->epc;

	/*
	 * Forcefully power down the MHI EP stack. Only way to bring the MHI EP
	 * stack back to working state after successive bind is by getting BME
	 * from host.
	 */
	if (mhi_cntrl->mhi_dev) {
		mhi_ep_power_down(mhi_cntrl);
		mhi_ep_unregister_controller(mhi_cntrl);
	}

	iounmap(epf_mhi->mmio);
	pci_epc_clear_bar(epc, epf->func_no, epf->vfunc_no, epf_bar);
}

static struct pci_epc_event_ops pci_epf_mhi_event_ops = {
	.core_init = pci_epf_mhi_core_init,
	.link_up = pci_epf_mhi_link_up,
	.link_down = pci_epf_mhi_link_down,
	.bme = pci_epf_mhi_bme,
};

static int pci_epf_mhi_probe(struct pci_epf *epf,
			     const struct pci_epf_device_id *id)
{
	struct pci_epf_mhi_ep_info *info =
			(struct pci_epf_mhi_ep_info *)id->driver_data;
	struct pci_epf_mhi *epf_mhi;
	struct device *dev = &epf->dev;

	epf_mhi = devm_kzalloc(dev, sizeof(*epf_mhi), GFP_KERNEL);
	if (!epf_mhi)
		return -ENOMEM;

	epf->header = info->epf_header;
	epf_mhi->info = info;
	epf_mhi->epf = epf;

	epf->event_ops = &pci_epf_mhi_event_ops;

	mutex_init(&epf_mhi->lock);

	epf_set_drvdata(epf, epf_mhi);

	return 0;
}

static const struct pci_epf_device_id pci_epf_mhi_ids[] = {
	{
		.name = "sdx55", .driver_data = (kernel_ulong_t)&sdx55_info,
	},
	{},
};

static struct pci_epf_ops pci_epf_mhi_ops = {
	.unbind	= pci_epf_mhi_unbind,
	.bind	= pci_epf_mhi_bind,
};

static struct pci_epf_driver pci_epf_mhi_driver = {
	.driver.name	= "pci_epf_mhi",
	.probe		= pci_epf_mhi_probe,
	.id_table	= pci_epf_mhi_ids,
	.ops		= &pci_epf_mhi_ops,
	.owner		= THIS_MODULE,
};

static int __init pci_epf_mhi_init(void)
{
	return pci_epf_register_driver(&pci_epf_mhi_driver);
}
module_init(pci_epf_mhi_init);

static void __exit pci_epf_mhi_exit(void)
{
	pci_epf_unregister_driver(&pci_epf_mhi_driver);
}
module_exit(pci_epf_mhi_exit);

MODULE_DESCRIPTION("PCI EPF driver for MHI Endpoint devices");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_LICENSE("GPL");
