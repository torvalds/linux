// SPDX-License-Identifier: GPL-2.0
/*
 * PCI EPF driver for MHI Endpoint devices
 *
 * Copyright (C) 2023 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/dmaengine.h>
#include <linux/mhi_ep.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/pci-epc.h>
#include <linux/pci-epf.h>

#define MHI_VERSION_1_0 0x01000000

#define to_epf_mhi(cntrl) container_of(cntrl, struct pci_epf_mhi, cntrl)

/* Platform specific flags */
#define MHI_EPF_USE_DMA BIT(0)

struct pci_epf_mhi_dma_transfer {
	struct pci_epf_mhi *epf_mhi;
	struct mhi_ep_buf_info buf_info;
	struct list_head node;
	dma_addr_t paddr;
	enum dma_data_direction dir;
	size_t size;
};

struct pci_epf_mhi_ep_info {
	const struct mhi_ep_cntrl_config *config;
	struct pci_epf_header *epf_header;
	enum pci_barno bar_num;
	u32 epf_flags;
	u32 msi_count;
	u32 mru;
	u32 flags;
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

static struct pci_epf_header sm8450_header = {
	.vendorid = PCI_VENDOR_ID_QCOM,
	.deviceid = 0x0306,
	.baseclass_code = PCI_CLASS_OTHERS,
	.interrupt_pin = PCI_INTERRUPT_INTA,
};

static const struct pci_epf_mhi_ep_info sm8450_info = {
	.config = &mhi_v1_config,
	.epf_header = &sm8450_header,
	.bar_num = BAR_0,
	.epf_flags = PCI_BASE_ADDRESS_MEM_TYPE_32,
	.msi_count = 32,
	.mru = 0x8000,
	.flags = MHI_EPF_USE_DMA,
};

static struct pci_epf_header sa8775p_header = {
	.vendorid = PCI_VENDOR_ID_QCOM,
	.deviceid = 0x0306,               /* FIXME: Update deviceid for sa8775p EP */
	.baseclass_code = PCI_CLASS_OTHERS,
	.interrupt_pin = PCI_INTERRUPT_INTA,
};

static const struct pci_epf_mhi_ep_info sa8775p_info = {
	.config = &mhi_v1_config,
	.epf_header = &sa8775p_header,
	.bar_num = BAR_0,
	.epf_flags = PCI_BASE_ADDRESS_MEM_TYPE_32,
	.msi_count = 32,
	.mru = 0x8000,
	.flags = MHI_EPF_USE_DMA,
};

struct pci_epf_mhi {
	const struct pci_epc_features *epc_features;
	const struct pci_epf_mhi_ep_info *info;
	struct mhi_ep_cntrl mhi_cntrl;
	struct pci_epf *epf;
	struct mutex lock;
	void __iomem *mmio;
	resource_size_t mmio_phys;
	struct dma_chan *dma_chan_tx;
	struct dma_chan *dma_chan_rx;
	struct workqueue_struct *dma_wq;
	struct work_struct dma_work;
	struct list_head dma_list;
	spinlock_t list_lock;
	u32 mmio_size;
	int irq;
};

static size_t get_align_offset(struct pci_epf_mhi *epf_mhi, u64 addr)
{
	return addr & (epf_mhi->epc_features->align -1);
}

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
	size_t offset = get_align_offset(epf_mhi, pci_addr);

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
	size_t offset = get_align_offset(epf_mhi, pci_addr);

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
	pci_epc_raise_irq(epc, epf->func_no, epf->vfunc_no, PCI_IRQ_MSI,
			  vector + 1);
}

static int pci_epf_mhi_iatu_read(struct mhi_ep_cntrl *mhi_cntrl,
				 struct mhi_ep_buf_info *buf_info)
{
	struct pci_epf_mhi *epf_mhi = to_epf_mhi(mhi_cntrl);
	size_t offset = get_align_offset(epf_mhi, buf_info->host_addr);
	void __iomem *tre_buf;
	phys_addr_t tre_phys;
	int ret;

	mutex_lock(&epf_mhi->lock);

	ret = __pci_epf_mhi_alloc_map(mhi_cntrl, buf_info->host_addr, &tre_phys,
				      &tre_buf, offset, buf_info->size);
	if (ret) {
		mutex_unlock(&epf_mhi->lock);
		return ret;
	}

	memcpy_fromio(buf_info->dev_addr, tre_buf, buf_info->size);

	__pci_epf_mhi_unmap_free(mhi_cntrl, buf_info->host_addr, tre_phys,
				 tre_buf, offset, buf_info->size);

	mutex_unlock(&epf_mhi->lock);

	if (buf_info->cb)
		buf_info->cb(buf_info);

	return 0;
}

static int pci_epf_mhi_iatu_write(struct mhi_ep_cntrl *mhi_cntrl,
				  struct mhi_ep_buf_info *buf_info)
{
	struct pci_epf_mhi *epf_mhi = to_epf_mhi(mhi_cntrl);
	size_t offset = get_align_offset(epf_mhi, buf_info->host_addr);
	void __iomem *tre_buf;
	phys_addr_t tre_phys;
	int ret;

	mutex_lock(&epf_mhi->lock);

	ret = __pci_epf_mhi_alloc_map(mhi_cntrl, buf_info->host_addr, &tre_phys,
				      &tre_buf, offset, buf_info->size);
	if (ret) {
		mutex_unlock(&epf_mhi->lock);
		return ret;
	}

	memcpy_toio(tre_buf, buf_info->dev_addr, buf_info->size);

	__pci_epf_mhi_unmap_free(mhi_cntrl, buf_info->host_addr, tre_phys,
				 tre_buf, offset, buf_info->size);

	mutex_unlock(&epf_mhi->lock);

	if (buf_info->cb)
		buf_info->cb(buf_info);

	return 0;
}

static void pci_epf_mhi_dma_callback(void *param)
{
	complete(param);
}

static int pci_epf_mhi_edma_read(struct mhi_ep_cntrl *mhi_cntrl,
				 struct mhi_ep_buf_info *buf_info)
{
	struct pci_epf_mhi *epf_mhi = to_epf_mhi(mhi_cntrl);
	struct device *dma_dev = epf_mhi->epf->epc->dev.parent;
	struct dma_chan *chan = epf_mhi->dma_chan_rx;
	struct device *dev = &epf_mhi->epf->dev;
	DECLARE_COMPLETION_ONSTACK(complete);
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config config = {};
	dma_cookie_t cookie;
	dma_addr_t dst_addr;
	int ret;

	if (buf_info->size < SZ_4K)
		return pci_epf_mhi_iatu_read(mhi_cntrl, buf_info);

	mutex_lock(&epf_mhi->lock);

	config.direction = DMA_DEV_TO_MEM;
	config.src_addr = buf_info->host_addr;

	ret = dmaengine_slave_config(chan, &config);
	if (ret) {
		dev_err(dev, "Failed to configure DMA channel\n");
		goto err_unlock;
	}

	dst_addr = dma_map_single(dma_dev, buf_info->dev_addr, buf_info->size,
				  DMA_FROM_DEVICE);
	ret = dma_mapping_error(dma_dev, dst_addr);
	if (ret) {
		dev_err(dev, "Failed to map remote memory\n");
		goto err_unlock;
	}

	desc = dmaengine_prep_slave_single(chan, dst_addr, buf_info->size,
					   DMA_DEV_TO_MEM,
					   DMA_CTRL_ACK | DMA_PREP_INTERRUPT);
	if (!desc) {
		dev_err(dev, "Failed to prepare DMA\n");
		ret = -EIO;
		goto err_unmap;
	}

	desc->callback = pci_epf_mhi_dma_callback;
	desc->callback_param = &complete;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(dev, "Failed to do DMA submit\n");
		goto err_unmap;
	}

	dma_async_issue_pending(chan);
	ret = wait_for_completion_timeout(&complete, msecs_to_jiffies(1000));
	if (!ret) {
		dev_err(dev, "DMA transfer timeout\n");
		dmaengine_terminate_sync(chan);
		ret = -ETIMEDOUT;
	}

err_unmap:
	dma_unmap_single(dma_dev, dst_addr, buf_info->size, DMA_FROM_DEVICE);
err_unlock:
	mutex_unlock(&epf_mhi->lock);

	return ret;
}

static int pci_epf_mhi_edma_write(struct mhi_ep_cntrl *mhi_cntrl,
				  struct mhi_ep_buf_info *buf_info)
{
	struct pci_epf_mhi *epf_mhi = to_epf_mhi(mhi_cntrl);
	struct device *dma_dev = epf_mhi->epf->epc->dev.parent;
	struct dma_chan *chan = epf_mhi->dma_chan_tx;
	struct device *dev = &epf_mhi->epf->dev;
	DECLARE_COMPLETION_ONSTACK(complete);
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config config = {};
	dma_cookie_t cookie;
	dma_addr_t src_addr;
	int ret;

	if (buf_info->size < SZ_4K)
		return pci_epf_mhi_iatu_write(mhi_cntrl, buf_info);

	mutex_lock(&epf_mhi->lock);

	config.direction = DMA_MEM_TO_DEV;
	config.dst_addr = buf_info->host_addr;

	ret = dmaengine_slave_config(chan, &config);
	if (ret) {
		dev_err(dev, "Failed to configure DMA channel\n");
		goto err_unlock;
	}

	src_addr = dma_map_single(dma_dev, buf_info->dev_addr, buf_info->size,
				  DMA_TO_DEVICE);
	ret = dma_mapping_error(dma_dev, src_addr);
	if (ret) {
		dev_err(dev, "Failed to map remote memory\n");
		goto err_unlock;
	}

	desc = dmaengine_prep_slave_single(chan, src_addr, buf_info->size,
					   DMA_MEM_TO_DEV,
					   DMA_CTRL_ACK | DMA_PREP_INTERRUPT);
	if (!desc) {
		dev_err(dev, "Failed to prepare DMA\n");
		ret = -EIO;
		goto err_unmap;
	}

	desc->callback = pci_epf_mhi_dma_callback;
	desc->callback_param = &complete;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(dev, "Failed to do DMA submit\n");
		goto err_unmap;
	}

	dma_async_issue_pending(chan);
	ret = wait_for_completion_timeout(&complete, msecs_to_jiffies(1000));
	if (!ret) {
		dev_err(dev, "DMA transfer timeout\n");
		dmaengine_terminate_sync(chan);
		ret = -ETIMEDOUT;
	}

err_unmap:
	dma_unmap_single(dma_dev, src_addr, buf_info->size, DMA_TO_DEVICE);
err_unlock:
	mutex_unlock(&epf_mhi->lock);

	return ret;
}

static void pci_epf_mhi_dma_worker(struct work_struct *work)
{
	struct pci_epf_mhi *epf_mhi = container_of(work, struct pci_epf_mhi, dma_work);
	struct device *dma_dev = epf_mhi->epf->epc->dev.parent;
	struct pci_epf_mhi_dma_transfer *itr, *tmp;
	struct mhi_ep_buf_info *buf_info;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&epf_mhi->list_lock, flags);
	list_splice_tail_init(&epf_mhi->dma_list, &head);
	spin_unlock_irqrestore(&epf_mhi->list_lock, flags);

	list_for_each_entry_safe(itr, tmp, &head, node) {
		list_del(&itr->node);
		dma_unmap_single(dma_dev, itr->paddr, itr->size, itr->dir);
		buf_info = &itr->buf_info;
		buf_info->cb(buf_info);
		kfree(itr);
	}
}

static void pci_epf_mhi_dma_async_callback(void *param)
{
	struct pci_epf_mhi_dma_transfer *transfer = param;
	struct pci_epf_mhi *epf_mhi = transfer->epf_mhi;

	spin_lock(&epf_mhi->list_lock);
	list_add_tail(&transfer->node, &epf_mhi->dma_list);
	spin_unlock(&epf_mhi->list_lock);

	queue_work(epf_mhi->dma_wq, &epf_mhi->dma_work);
}

static int pci_epf_mhi_edma_read_async(struct mhi_ep_cntrl *mhi_cntrl,
				       struct mhi_ep_buf_info *buf_info)
{
	struct pci_epf_mhi *epf_mhi = to_epf_mhi(mhi_cntrl);
	struct device *dma_dev = epf_mhi->epf->epc->dev.parent;
	struct pci_epf_mhi_dma_transfer *transfer = NULL;
	struct dma_chan *chan = epf_mhi->dma_chan_rx;
	struct device *dev = &epf_mhi->epf->dev;
	DECLARE_COMPLETION_ONSTACK(complete);
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config config = {};
	dma_cookie_t cookie;
	dma_addr_t dst_addr;
	int ret;

	mutex_lock(&epf_mhi->lock);

	config.direction = DMA_DEV_TO_MEM;
	config.src_addr = buf_info->host_addr;

	ret = dmaengine_slave_config(chan, &config);
	if (ret) {
		dev_err(dev, "Failed to configure DMA channel\n");
		goto err_unlock;
	}

	dst_addr = dma_map_single(dma_dev, buf_info->dev_addr, buf_info->size,
				  DMA_FROM_DEVICE);
	ret = dma_mapping_error(dma_dev, dst_addr);
	if (ret) {
		dev_err(dev, "Failed to map remote memory\n");
		goto err_unlock;
	}

	desc = dmaengine_prep_slave_single(chan, dst_addr, buf_info->size,
					   DMA_DEV_TO_MEM,
					   DMA_CTRL_ACK | DMA_PREP_INTERRUPT);
	if (!desc) {
		dev_err(dev, "Failed to prepare DMA\n");
		ret = -EIO;
		goto err_unmap;
	}

	transfer = kzalloc(sizeof(*transfer), GFP_KERNEL);
	if (!transfer) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	transfer->epf_mhi = epf_mhi;
	transfer->paddr = dst_addr;
	transfer->size = buf_info->size;
	transfer->dir = DMA_FROM_DEVICE;
	memcpy(&transfer->buf_info, buf_info, sizeof(*buf_info));

	desc->callback = pci_epf_mhi_dma_async_callback;
	desc->callback_param = transfer;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(dev, "Failed to do DMA submit\n");
		goto err_free_transfer;
	}

	dma_async_issue_pending(chan);

	goto err_unlock;

err_free_transfer:
	kfree(transfer);
err_unmap:
	dma_unmap_single(dma_dev, dst_addr, buf_info->size, DMA_FROM_DEVICE);
err_unlock:
	mutex_unlock(&epf_mhi->lock);

	return ret;
}

static int pci_epf_mhi_edma_write_async(struct mhi_ep_cntrl *mhi_cntrl,
					struct mhi_ep_buf_info *buf_info)
{
	struct pci_epf_mhi *epf_mhi = to_epf_mhi(mhi_cntrl);
	struct device *dma_dev = epf_mhi->epf->epc->dev.parent;
	struct pci_epf_mhi_dma_transfer *transfer = NULL;
	struct dma_chan *chan = epf_mhi->dma_chan_tx;
	struct device *dev = &epf_mhi->epf->dev;
	DECLARE_COMPLETION_ONSTACK(complete);
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config config = {};
	dma_cookie_t cookie;
	dma_addr_t src_addr;
	int ret;

	mutex_lock(&epf_mhi->lock);

	config.direction = DMA_MEM_TO_DEV;
	config.dst_addr = buf_info->host_addr;

	ret = dmaengine_slave_config(chan, &config);
	if (ret) {
		dev_err(dev, "Failed to configure DMA channel\n");
		goto err_unlock;
	}

	src_addr = dma_map_single(dma_dev, buf_info->dev_addr, buf_info->size,
				  DMA_TO_DEVICE);
	ret = dma_mapping_error(dma_dev, src_addr);
	if (ret) {
		dev_err(dev, "Failed to map remote memory\n");
		goto err_unlock;
	}

	desc = dmaengine_prep_slave_single(chan, src_addr, buf_info->size,
					   DMA_MEM_TO_DEV,
					   DMA_CTRL_ACK | DMA_PREP_INTERRUPT);
	if (!desc) {
		dev_err(dev, "Failed to prepare DMA\n");
		ret = -EIO;
		goto err_unmap;
	}

	transfer = kzalloc(sizeof(*transfer), GFP_KERNEL);
	if (!transfer) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	transfer->epf_mhi = epf_mhi;
	transfer->paddr = src_addr;
	transfer->size = buf_info->size;
	transfer->dir = DMA_TO_DEVICE;
	memcpy(&transfer->buf_info, buf_info, sizeof(*buf_info));

	desc->callback = pci_epf_mhi_dma_async_callback;
	desc->callback_param = transfer;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(dev, "Failed to do DMA submit\n");
		goto err_free_transfer;
	}

	dma_async_issue_pending(chan);

	goto err_unlock;

err_free_transfer:
	kfree(transfer);
err_unmap:
	dma_unmap_single(dma_dev, src_addr, buf_info->size, DMA_TO_DEVICE);
err_unlock:
	mutex_unlock(&epf_mhi->lock);

	return ret;
}

struct epf_dma_filter {
	struct device *dev;
	u32 dma_mask;
};

static bool pci_epf_mhi_filter(struct dma_chan *chan, void *node)
{
	struct epf_dma_filter *filter = node;
	struct dma_slave_caps caps;

	memset(&caps, 0, sizeof(caps));
	dma_get_slave_caps(chan, &caps);

	return chan->device->dev == filter->dev && filter->dma_mask &
					caps.directions;
}

static int pci_epf_mhi_dma_init(struct pci_epf_mhi *epf_mhi)
{
	struct device *dma_dev = epf_mhi->epf->epc->dev.parent;
	struct device *dev = &epf_mhi->epf->dev;
	struct epf_dma_filter filter;
	dma_cap_mask_t mask;
	int ret;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	filter.dev = dma_dev;
	filter.dma_mask = BIT(DMA_MEM_TO_DEV);
	epf_mhi->dma_chan_tx = dma_request_channel(mask, pci_epf_mhi_filter,
						   &filter);
	if (IS_ERR_OR_NULL(epf_mhi->dma_chan_tx)) {
		dev_err(dev, "Failed to request tx channel\n");
		return -ENODEV;
	}

	filter.dma_mask = BIT(DMA_DEV_TO_MEM);
	epf_mhi->dma_chan_rx = dma_request_channel(mask, pci_epf_mhi_filter,
						   &filter);
	if (IS_ERR_OR_NULL(epf_mhi->dma_chan_rx)) {
		dev_err(dev, "Failed to request rx channel\n");
		ret = -ENODEV;
		goto err_release_tx;
	}

	epf_mhi->dma_wq = alloc_workqueue("pci_epf_mhi_dma_wq", 0, 0);
	if (!epf_mhi->dma_wq) {
		ret = -ENOMEM;
		goto err_release_rx;
	}

	INIT_LIST_HEAD(&epf_mhi->dma_list);
	INIT_WORK(&epf_mhi->dma_work, pci_epf_mhi_dma_worker);
	spin_lock_init(&epf_mhi->list_lock);

	return 0;

err_release_rx:
	dma_release_channel(epf_mhi->dma_chan_rx);
	epf_mhi->dma_chan_rx = NULL;
err_release_tx:
	dma_release_channel(epf_mhi->dma_chan_tx);
	epf_mhi->dma_chan_tx = NULL;

	return ret;
}

static void pci_epf_mhi_dma_deinit(struct pci_epf_mhi *epf_mhi)
{
	destroy_workqueue(epf_mhi->dma_wq);
	dma_release_channel(epf_mhi->dma_chan_tx);
	dma_release_channel(epf_mhi->dma_chan_rx);
	epf_mhi->dma_chan_tx = NULL;
	epf_mhi->dma_chan_rx = NULL;
}

static int pci_epf_mhi_epc_init(struct pci_epf *epf)
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

	epf_mhi->epc_features = pci_epc_get_features(epc, epf->func_no, epf->vfunc_no);
	if (!epf_mhi->epc_features)
		return -ENODATA;

	if (info->flags & MHI_EPF_USE_DMA) {
		ret = pci_epf_mhi_dma_init(epf_mhi);
		if (ret) {
			dev_err(dev, "Failed to initialize DMA: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static void pci_epf_mhi_epc_deinit(struct pci_epf *epf)
{
	struct pci_epf_mhi *epf_mhi = epf_get_drvdata(epf);
	const struct pci_epf_mhi_ep_info *info = epf_mhi->info;
	struct pci_epf_bar *epf_bar = &epf->bar[info->bar_num];
	struct mhi_ep_cntrl *mhi_cntrl = &epf_mhi->mhi_cntrl;
	struct pci_epc *epc = epf->epc;

	if (mhi_cntrl->mhi_dev) {
		mhi_ep_power_down(mhi_cntrl);
		if (info->flags & MHI_EPF_USE_DMA)
			pci_epf_mhi_dma_deinit(epf_mhi);
		mhi_ep_unregister_controller(mhi_cntrl);
	}

	pci_epc_clear_bar(epc, epf->func_no, epf->vfunc_no, epf_bar);
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
	mhi_cntrl->read_sync = mhi_cntrl->read_async = pci_epf_mhi_iatu_read;
	mhi_cntrl->write_sync = mhi_cntrl->write_async = pci_epf_mhi_iatu_write;
	if (info->flags & MHI_EPF_USE_DMA) {
		mhi_cntrl->read_sync = pci_epf_mhi_edma_read;
		mhi_cntrl->write_sync = pci_epf_mhi_edma_write;
		mhi_cntrl->read_async = pci_epf_mhi_edma_read_async;
		mhi_cntrl->write_async = pci_epf_mhi_edma_write_async;
	}

	/* Register the MHI EP controller */
	ret = mhi_ep_register_controller(mhi_cntrl, info->config);
	if (ret) {
		dev_err(dev, "Failed to register MHI EP controller: %d\n", ret);
		if (info->flags & MHI_EPF_USE_DMA)
			pci_epf_mhi_dma_deinit(epf_mhi);
		return ret;
	}

	return 0;
}

static int pci_epf_mhi_link_down(struct pci_epf *epf)
{
	struct pci_epf_mhi *epf_mhi = epf_get_drvdata(epf);
	const struct pci_epf_mhi_ep_info *info = epf_mhi->info;
	struct mhi_ep_cntrl *mhi_cntrl = &epf_mhi->mhi_cntrl;

	if (mhi_cntrl->mhi_dev) {
		mhi_ep_power_down(mhi_cntrl);
		if (info->flags & MHI_EPF_USE_DMA)
			pci_epf_mhi_dma_deinit(epf_mhi);
		mhi_ep_unregister_controller(mhi_cntrl);
	}

	return 0;
}

static int pci_epf_mhi_bus_master_enable(struct pci_epf *epf)
{
	struct pci_epf_mhi *epf_mhi = epf_get_drvdata(epf);
	const struct pci_epf_mhi_ep_info *info = epf_mhi->info;
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
			if (info->flags & MHI_EPF_USE_DMA)
				pci_epf_mhi_dma_deinit(epf_mhi);
			mhi_ep_unregister_controller(mhi_cntrl);
		}
	}

	return 0;
}

static int pci_epf_mhi_bind(struct pci_epf *epf)
{
	struct pci_epf_mhi *epf_mhi = epf_get_drvdata(epf);
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;
	struct platform_device *pdev = to_platform_device(epc->dev.parent);
	struct resource *res;
	int ret;

	/* Get MMIO base address from Endpoint controller */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mmio");
	if (!res) {
		dev_err(dev, "Failed to get \"mmio\" resource\n");
		return -ENODEV;
	}

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
	 * stack back to working state after successive bind is by getting Bus
	 * Master Enable event from host.
	 */
	if (mhi_cntrl->mhi_dev) {
		mhi_ep_power_down(mhi_cntrl);
		if (info->flags & MHI_EPF_USE_DMA)
			pci_epf_mhi_dma_deinit(epf_mhi);
		mhi_ep_unregister_controller(mhi_cntrl);
	}

	iounmap(epf_mhi->mmio);
	pci_epc_clear_bar(epc, epf->func_no, epf->vfunc_no, epf_bar);
}

static const struct pci_epc_event_ops pci_epf_mhi_event_ops = {
	.epc_init = pci_epf_mhi_epc_init,
	.epc_deinit = pci_epf_mhi_epc_deinit,
	.link_up = pci_epf_mhi_link_up,
	.link_down = pci_epf_mhi_link_down,
	.bus_master_enable = pci_epf_mhi_bus_master_enable,
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
	{ .name = "pci_epf_mhi_sa8775p", .driver_data = (kernel_ulong_t)&sa8775p_info },
	{ .name = "pci_epf_mhi_sdx55", .driver_data = (kernel_ulong_t)&sdx55_info },
	{ .name = "pci_epf_mhi_sm8450", .driver_data = (kernel_ulong_t)&sm8450_info },
	{},
};

static const struct pci_epf_ops pci_epf_mhi_ops = {
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
