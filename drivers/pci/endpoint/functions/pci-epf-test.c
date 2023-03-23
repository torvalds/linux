// SPDX-License-Identifier: GPL-2.0
/**
 * Test driver to test endpoint functionality
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci_ids.h>
#include <linux/random.h>

#include <linux/pci-epc.h>
#include <linux/pci-epf.h>
#include <linux/pci_regs.h>

#define IRQ_TYPE_LEGACY			0
#define IRQ_TYPE_MSI			1
#define IRQ_TYPE_MSIX			2

#define COMMAND_RAISE_LEGACY_IRQ	BIT(0)
#define COMMAND_RAISE_MSI_IRQ		BIT(1)
#define COMMAND_RAISE_MSIX_IRQ		BIT(2)
#define COMMAND_READ			BIT(3)
#define COMMAND_WRITE			BIT(4)
#define COMMAND_COPY			BIT(5)

#define STATUS_READ_SUCCESS		BIT(0)
#define STATUS_READ_FAIL		BIT(1)
#define STATUS_WRITE_SUCCESS		BIT(2)
#define STATUS_WRITE_FAIL		BIT(3)
#define STATUS_COPY_SUCCESS		BIT(4)
#define STATUS_COPY_FAIL		BIT(5)
#define STATUS_IRQ_RAISED		BIT(6)
#define STATUS_SRC_ADDR_INVALID		BIT(7)
#define STATUS_DST_ADDR_INVALID		BIT(8)

#define FLAG_USE_DMA			BIT(0)

#define TIMER_RESOLUTION		1

static struct workqueue_struct *kpcitest_workqueue;

struct pci_epf_test {
	void			*reg[PCI_STD_NUM_BARS];
	struct pci_epf		*epf;
	enum pci_barno		test_reg_bar;
	size_t			msix_table_offset;
	struct delayed_work	cmd_handler;
	struct dma_chan		*dma_chan;
	struct completion	transfer_complete;
	bool			dma_supported;
	const struct pci_epc_features *epc_features;
};

struct pci_epf_test_reg {
	u32	magic;
	u32	command;
	u32	status;
	u64	src_addr;
	u64	dst_addr;
	u32	size;
	u32	checksum;
	u32	irq_type;
	u32	irq_number;
	u32	flags;
} __packed;

static struct pci_epf_header test_header = {
	.vendorid	= PCI_ANY_ID,
	.deviceid	= PCI_ANY_ID,
	.baseclass_code = PCI_CLASS_OTHERS,
	.interrupt_pin	= PCI_INTERRUPT_INTA,
};

static size_t bar_size[] = { 512, 512, 1024, 16384, 131072, 1048576 };

static void pci_epf_test_dma_callback(void *param)
{
	struct pci_epf_test *epf_test = param;

	complete(&epf_test->transfer_complete);
}

/**
 * pci_epf_test_data_transfer() - Function that uses dmaengine API to transfer
 *				  data between PCIe EP and remote PCIe RC
 * @epf_test: the EPF test device that performs the data transfer operation
 * @dma_dst: The destination address of the data transfer. It can be a physical
 *	     address given by pci_epc_mem_alloc_addr or DMA mapping APIs.
 * @dma_src: The source address of the data transfer. It can be a physical
 *	     address given by pci_epc_mem_alloc_addr or DMA mapping APIs.
 * @len: The size of the data transfer
 *
 * Function that uses dmaengine API to transfer data between PCIe EP and remote
 * PCIe RC. The source and destination address can be a physical address given
 * by pci_epc_mem_alloc_addr or the one obtained using DMA mapping APIs.
 *
 * The function returns '0' on success and negative value on failure.
 */
static int pci_epf_test_data_transfer(struct pci_epf_test *epf_test,
				      dma_addr_t dma_dst, dma_addr_t dma_src,
				      size_t len)
{
	enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	struct dma_chan *chan = epf_test->dma_chan;
	struct pci_epf *epf = epf_test->epf;
	struct dma_async_tx_descriptor *tx;
	struct device *dev = &epf->dev;
	dma_cookie_t cookie;
	int ret;

	if (IS_ERR_OR_NULL(chan)) {
		dev_err(dev, "Invalid DMA memcpy channel\n");
		return -EINVAL;
	}

	tx = dmaengine_prep_dma_memcpy(chan, dma_dst, dma_src, len, flags);
	if (!tx) {
		dev_err(dev, "Failed to prepare DMA memcpy\n");
		return -EIO;
	}

	tx->callback = pci_epf_test_dma_callback;
	tx->callback_param = epf_test;
	cookie = tx->tx_submit(tx);
	reinit_completion(&epf_test->transfer_complete);

	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(dev, "Failed to do DMA tx_submit %d\n", cookie);
		return -EIO;
	}

	dma_async_issue_pending(chan);
	ret = wait_for_completion_interruptible(&epf_test->transfer_complete);
	if (ret < 0) {
		dmaengine_terminate_sync(chan);
		dev_err(dev, "DMA wait_for_completion_timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * pci_epf_test_init_dma_chan() - Function to initialize EPF test DMA channel
 * @epf_test: the EPF test device that performs data transfer operation
 *
 * Function to initialize EPF test DMA channel.
 */
static int pci_epf_test_init_dma_chan(struct pci_epf_test *epf_test)
{
	struct pci_epf *epf = epf_test->epf;
	struct device *dev = &epf->dev;
	struct dma_chan *dma_chan;
	dma_cap_mask_t mask;
	int ret;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	dma_chan = dma_request_chan_by_mask(&mask);
	if (IS_ERR(dma_chan)) {
		ret = PTR_ERR(dma_chan);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get DMA channel\n");
		return ret;
	}
	init_completion(&epf_test->transfer_complete);

	epf_test->dma_chan = dma_chan;

	return 0;
}

/**
 * pci_epf_test_clean_dma_chan() - Function to cleanup EPF test DMA channel
 * @epf_test: the EPF test device that performs data transfer operation
 *
 * Helper to cleanup EPF test DMA channel.
 */
static void pci_epf_test_clean_dma_chan(struct pci_epf_test *epf_test)
{
	if (!epf_test->dma_supported)
		return;

	dma_release_channel(epf_test->dma_chan);
	epf_test->dma_chan = NULL;
}

static void pci_epf_test_print_rate(const char *ops, u64 size,
				    struct timespec64 *start,
				    struct timespec64 *end, bool dma)
{
	struct timespec64 ts;
	u64 rate, ns;

	ts = timespec64_sub(*end, *start);

	/* convert both size (stored in 'rate') and time in terms of 'ns' */
	ns = timespec64_to_ns(&ts);
	rate = size * NSEC_PER_SEC;

	/* Divide both size (stored in 'rate') and ns by a common factor */
	while (ns > UINT_MAX) {
		rate >>= 1;
		ns >>= 1;
	}

	if (!ns)
		return;

	/* calculate the rate */
	do_div(rate, (uint32_t)ns);

	pr_info("\n%s => Size: %llu bytes\t DMA: %s\t Time: %llu.%09u seconds\t"
		"Rate: %llu KB/s\n", ops, size, dma ? "YES" : "NO",
		(u64)ts.tv_sec, (u32)ts.tv_nsec, rate / 1024);
}

static int pci_epf_test_copy(struct pci_epf_test *epf_test)
{
	int ret;
	bool use_dma;
	void __iomem *src_addr;
	void __iomem *dst_addr;
	phys_addr_t src_phys_addr;
	phys_addr_t dst_phys_addr;
	struct timespec64 start, end;
	struct pci_epf *epf = epf_test->epf;
	struct device *dev = &epf->dev;
	struct pci_epc *epc = epf->epc;
	enum pci_barno test_reg_bar = epf_test->test_reg_bar;
	struct pci_epf_test_reg *reg = epf_test->reg[test_reg_bar];

	src_addr = pci_epc_mem_alloc_addr(epc, &src_phys_addr, reg->size);
	if (!src_addr) {
		dev_err(dev, "Failed to allocate source address\n");
		reg->status = STATUS_SRC_ADDR_INVALID;
		ret = -ENOMEM;
		goto err;
	}

	ret = pci_epc_map_addr(epc, epf->func_no, src_phys_addr, reg->src_addr,
			       reg->size);
	if (ret) {
		dev_err(dev, "Failed to map source address\n");
		reg->status = STATUS_SRC_ADDR_INVALID;
		goto err_src_addr;
	}

	dst_addr = pci_epc_mem_alloc_addr(epc, &dst_phys_addr, reg->size);
	if (!dst_addr) {
		dev_err(dev, "Failed to allocate destination address\n");
		reg->status = STATUS_DST_ADDR_INVALID;
		ret = -ENOMEM;
		goto err_src_map_addr;
	}

	ret = pci_epc_map_addr(epc, epf->func_no, dst_phys_addr, reg->dst_addr,
			       reg->size);
	if (ret) {
		dev_err(dev, "Failed to map destination address\n");
		reg->status = STATUS_DST_ADDR_INVALID;
		goto err_dst_addr;
	}

	ktime_get_ts64(&start);
	use_dma = !!(reg->flags & FLAG_USE_DMA);
	if (use_dma) {
		if (!epf_test->dma_supported) {
			dev_err(dev, "Cannot transfer data using DMA\n");
			ret = -EINVAL;
			goto err_map_addr;
		}

		ret = pci_epf_test_data_transfer(epf_test, dst_phys_addr,
						 src_phys_addr, reg->size);
		if (ret)
			dev_err(dev, "Data transfer failed\n");
	} else {
		void *buf;

		buf = kzalloc(reg->size, GFP_KERNEL);
		if (!buf) {
			ret = -ENOMEM;
			goto err_map_addr;
		}

		memcpy_fromio(buf, src_addr, reg->size);
		memcpy_toio(dst_addr, buf, reg->size);
		kfree(buf);
	}
	ktime_get_ts64(&end);
	pci_epf_test_print_rate("COPY", reg->size, &start, &end, use_dma);

err_map_addr:
	pci_epc_unmap_addr(epc, epf->func_no, dst_phys_addr);

err_dst_addr:
	pci_epc_mem_free_addr(epc, dst_phys_addr, dst_addr, reg->size);

err_src_map_addr:
	pci_epc_unmap_addr(epc, epf->func_no, src_phys_addr);

err_src_addr:
	pci_epc_mem_free_addr(epc, src_phys_addr, src_addr, reg->size);

err:
	return ret;
}

static int pci_epf_test_read(struct pci_epf_test *epf_test)
{
	int ret;
	void __iomem *src_addr;
	void *buf;
	u32 crc32;
	bool use_dma;
	phys_addr_t phys_addr;
	phys_addr_t dst_phys_addr;
	struct timespec64 start, end;
	struct pci_epf *epf = epf_test->epf;
	struct device *dev = &epf->dev;
	struct pci_epc *epc = epf->epc;
	struct device *dma_dev = epf->epc->dev.parent;
	enum pci_barno test_reg_bar = epf_test->test_reg_bar;
	struct pci_epf_test_reg *reg = epf_test->reg[test_reg_bar];

	src_addr = pci_epc_mem_alloc_addr(epc, &phys_addr, reg->size);
	if (!src_addr) {
		dev_err(dev, "Failed to allocate address\n");
		reg->status = STATUS_SRC_ADDR_INVALID;
		ret = -ENOMEM;
		goto err;
	}

	ret = pci_epc_map_addr(epc, epf->func_no, phys_addr, reg->src_addr,
			       reg->size);
	if (ret) {
		dev_err(dev, "Failed to map address\n");
		reg->status = STATUS_SRC_ADDR_INVALID;
		goto err_addr;
	}

	buf = kzalloc(reg->size, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto err_map_addr;
	}

	use_dma = !!(reg->flags & FLAG_USE_DMA);
	if (use_dma) {
		if (!epf_test->dma_supported) {
			dev_err(dev, "Cannot transfer data using DMA\n");
			ret = -EINVAL;
			goto err_dma_map;
		}

		dst_phys_addr = dma_map_single(dma_dev, buf, reg->size,
					       DMA_FROM_DEVICE);
		if (dma_mapping_error(dma_dev, dst_phys_addr)) {
			dev_err(dev, "Failed to map destination buffer addr\n");
			ret = -ENOMEM;
			goto err_dma_map;
		}

		ktime_get_ts64(&start);
		ret = pci_epf_test_data_transfer(epf_test, dst_phys_addr,
						 phys_addr, reg->size);
		if (ret)
			dev_err(dev, "Data transfer failed\n");
		ktime_get_ts64(&end);

		dma_unmap_single(dma_dev, dst_phys_addr, reg->size,
				 DMA_FROM_DEVICE);
	} else {
		ktime_get_ts64(&start);
		memcpy_fromio(buf, src_addr, reg->size);
		ktime_get_ts64(&end);
	}

	pci_epf_test_print_rate("READ", reg->size, &start, &end, use_dma);

	crc32 = crc32_le(~0, buf, reg->size);
	if (crc32 != reg->checksum)
		ret = -EIO;

err_dma_map:
	kfree(buf);

err_map_addr:
	pci_epc_unmap_addr(epc, epf->func_no, phys_addr);

err_addr:
	pci_epc_mem_free_addr(epc, phys_addr, src_addr, reg->size);

err:
	return ret;
}

static int pci_epf_test_write(struct pci_epf_test *epf_test)
{
	int ret;
	void __iomem *dst_addr;
	void *buf;
	bool use_dma;
	phys_addr_t phys_addr;
	phys_addr_t src_phys_addr;
	struct timespec64 start, end;
	struct pci_epf *epf = epf_test->epf;
	struct device *dev = &epf->dev;
	struct pci_epc *epc = epf->epc;
	struct device *dma_dev = epf->epc->dev.parent;
	enum pci_barno test_reg_bar = epf_test->test_reg_bar;
	struct pci_epf_test_reg *reg = epf_test->reg[test_reg_bar];

	dst_addr = pci_epc_mem_alloc_addr(epc, &phys_addr, reg->size);
	if (!dst_addr) {
		dev_err(dev, "Failed to allocate address\n");
		reg->status = STATUS_DST_ADDR_INVALID;
		ret = -ENOMEM;
		goto err;
	}

	ret = pci_epc_map_addr(epc, epf->func_no, phys_addr, reg->dst_addr,
			       reg->size);
	if (ret) {
		dev_err(dev, "Failed to map address\n");
		reg->status = STATUS_DST_ADDR_INVALID;
		goto err_addr;
	}

	buf = kzalloc(reg->size, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto err_map_addr;
	}

	get_random_bytes(buf, reg->size);
	reg->checksum = crc32_le(~0, buf, reg->size);

	use_dma = !!(reg->flags & FLAG_USE_DMA);
	if (use_dma) {
		if (!epf_test->dma_supported) {
			dev_err(dev, "Cannot transfer data using DMA\n");
			ret = -EINVAL;
			goto err_dma_map;
		}

		src_phys_addr = dma_map_single(dma_dev, buf, reg->size,
					       DMA_TO_DEVICE);
		if (dma_mapping_error(dma_dev, src_phys_addr)) {
			dev_err(dev, "Failed to map source buffer addr\n");
			ret = -ENOMEM;
			goto err_dma_map;
		}

		ktime_get_ts64(&start);
		ret = pci_epf_test_data_transfer(epf_test, phys_addr,
						 src_phys_addr, reg->size);
		if (ret)
			dev_err(dev, "Data transfer failed\n");
		ktime_get_ts64(&end);

		dma_unmap_single(dma_dev, src_phys_addr, reg->size,
				 DMA_TO_DEVICE);
	} else {
		ktime_get_ts64(&start);
		memcpy_toio(dst_addr, buf, reg->size);
		ktime_get_ts64(&end);
	}

	pci_epf_test_print_rate("WRITE", reg->size, &start, &end, use_dma);

	/*
	 * wait 1ms inorder for the write to complete. Without this delay L3
	 * error in observed in the host system.
	 */
	usleep_range(1000, 2000);

err_dma_map:
	kfree(buf);

err_map_addr:
	pci_epc_unmap_addr(epc, epf->func_no, phys_addr);

err_addr:
	pci_epc_mem_free_addr(epc, phys_addr, dst_addr, reg->size);

err:
	return ret;
}

static void pci_epf_test_raise_irq(struct pci_epf_test *epf_test, u8 irq_type,
				   u16 irq)
{
	struct pci_epf *epf = epf_test->epf;
	struct device *dev = &epf->dev;
	struct pci_epc *epc = epf->epc;
	enum pci_barno test_reg_bar = epf_test->test_reg_bar;
	struct pci_epf_test_reg *reg = epf_test->reg[test_reg_bar];

	reg->status |= STATUS_IRQ_RAISED;

	switch (irq_type) {
	case IRQ_TYPE_LEGACY:
		pci_epc_raise_irq(epc, epf->func_no, PCI_EPC_IRQ_LEGACY, 0);
		break;
	case IRQ_TYPE_MSI:
		pci_epc_raise_irq(epc, epf->func_no, PCI_EPC_IRQ_MSI, irq);
		break;
	case IRQ_TYPE_MSIX:
		pci_epc_raise_irq(epc, epf->func_no, PCI_EPC_IRQ_MSIX, irq);
		break;
	default:
		dev_err(dev, "Failed to raise IRQ, unknown type\n");
		break;
	}
}

static void pci_epf_test_cmd_handler(struct work_struct *work)
{
	int ret;
	int count;
	u32 command;
	struct pci_epf_test *epf_test = container_of(work, struct pci_epf_test,
						     cmd_handler.work);
	struct pci_epf *epf = epf_test->epf;
	struct device *dev = &epf->dev;
	struct pci_epc *epc = epf->epc;
	enum pci_barno test_reg_bar = epf_test->test_reg_bar;
	struct pci_epf_test_reg *reg = epf_test->reg[test_reg_bar];

	command = reg->command;
	if (!command)
		goto reset_handler;

	reg->command = 0;
	reg->status = 0;

	if (reg->irq_type > IRQ_TYPE_MSIX) {
		dev_err(dev, "Failed to detect IRQ type\n");
		goto reset_handler;
	}

	if (command & COMMAND_RAISE_LEGACY_IRQ) {
		reg->status = STATUS_IRQ_RAISED;
		pci_epc_raise_irq(epc, epf->func_no, PCI_EPC_IRQ_LEGACY, 0);
		goto reset_handler;
	}

	if (command & COMMAND_WRITE) {
		ret = pci_epf_test_write(epf_test);
		if (ret)
			reg->status |= STATUS_WRITE_FAIL;
		else
			reg->status |= STATUS_WRITE_SUCCESS;
		pci_epf_test_raise_irq(epf_test, reg->irq_type,
				       reg->irq_number);
		goto reset_handler;
	}

	if (command & COMMAND_READ) {
		ret = pci_epf_test_read(epf_test);
		if (!ret)
			reg->status |= STATUS_READ_SUCCESS;
		else
			reg->status |= STATUS_READ_FAIL;
		pci_epf_test_raise_irq(epf_test, reg->irq_type,
				       reg->irq_number);
		goto reset_handler;
	}

	if (command & COMMAND_COPY) {
		ret = pci_epf_test_copy(epf_test);
		if (!ret)
			reg->status |= STATUS_COPY_SUCCESS;
		else
			reg->status |= STATUS_COPY_FAIL;
		pci_epf_test_raise_irq(epf_test, reg->irq_type,
				       reg->irq_number);
		goto reset_handler;
	}

	if (command & COMMAND_RAISE_MSI_IRQ) {
		count = pci_epc_get_msi(epc, epf->func_no);
		if (reg->irq_number > count || count <= 0)
			goto reset_handler;
		reg->status = STATUS_IRQ_RAISED;
		pci_epc_raise_irq(epc, epf->func_no, PCI_EPC_IRQ_MSI,
				  reg->irq_number);
		goto reset_handler;
	}

	if (command & COMMAND_RAISE_MSIX_IRQ) {
		count = pci_epc_get_msix(epc, epf->func_no);
		if (reg->irq_number > count || count <= 0)
			goto reset_handler;
		reg->status = STATUS_IRQ_RAISED;
		pci_epc_raise_irq(epc, epf->func_no, PCI_EPC_IRQ_MSIX,
				  reg->irq_number);
		goto reset_handler;
	}

reset_handler:
	queue_delayed_work(kpcitest_workqueue, &epf_test->cmd_handler,
			   msecs_to_jiffies(1));
}

static void pci_epf_test_unbind(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	struct pci_epc *epc = epf->epc;
	struct pci_epf_bar *epf_bar;
	int bar;

	cancel_delayed_work(&epf_test->cmd_handler);
	pci_epf_test_clean_dma_chan(epf_test);
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		epf_bar = &epf->bar[bar];

		if (epf_test->reg[bar]) {
			pci_epc_clear_bar(epc, epf->func_no, epf_bar);
			pci_epf_free_space(epf, epf_test->reg[bar], bar);
		}
	}
}

static int pci_epf_test_set_bar(struct pci_epf *epf)
{
	int bar, add;
	int ret;
	struct pci_epf_bar *epf_bar;
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	enum pci_barno test_reg_bar = epf_test->test_reg_bar;
	const struct pci_epc_features *epc_features;

	epc_features = epf_test->epc_features;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar += add) {
		epf_bar = &epf->bar[bar];
		/*
		 * pci_epc_set_bar() sets PCI_BASE_ADDRESS_MEM_TYPE_64
		 * if the specific implementation required a 64-bit BAR,
		 * even if we only requested a 32-bit BAR.
		 */
		add = (epf_bar->flags & PCI_BASE_ADDRESS_MEM_TYPE_64) ? 2 : 1;

		if (!!(epc_features->reserved_bar & (1 << bar)))
			continue;

		ret = pci_epc_set_bar(epc, epf->func_no, epf_bar);
		if (ret) {
			pci_epf_free_space(epf, epf_test->reg[bar], bar);
			dev_err(dev, "Failed to set BAR%d\n", bar);
			if (bar == test_reg_bar)
				return ret;
		}
	}

	return 0;
}

static int pci_epf_test_core_init(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	struct pci_epf_header *header = epf->header;
	const struct pci_epc_features *epc_features;
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;
	bool msix_capable = false;
	bool msi_capable = true;
	int ret;

	epc_features = pci_epc_get_features(epc, epf->func_no);
	if (epc_features) {
		msix_capable = epc_features->msix_capable;
		msi_capable = epc_features->msi_capable;
	}

	ret = pci_epc_write_header(epc, epf->func_no, header);
	if (ret) {
		dev_err(dev, "Configuration header write failed\n");
		return ret;
	}

	ret = pci_epf_test_set_bar(epf);
	if (ret)
		return ret;

	if (msi_capable) {
		ret = pci_epc_set_msi(epc, epf->func_no, epf->msi_interrupts);
		if (ret) {
			dev_err(dev, "MSI configuration failed\n");
			return ret;
		}
	}

	if (msix_capable) {
		ret = pci_epc_set_msix(epc, epf->func_no, epf->msix_interrupts,
				       epf_test->test_reg_bar,
				       epf_test->msix_table_offset);
		if (ret) {
			dev_err(dev, "MSI-X configuration failed\n");
			return ret;
		}
	}

	return 0;
}

static int pci_epf_test_notifier(struct notifier_block *nb, unsigned long val,
				 void *data)
{
	struct pci_epf *epf = container_of(nb, struct pci_epf, nb);
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	int ret;

	switch (val) {
	case CORE_INIT:
		ret = pci_epf_test_core_init(epf);
		if (ret)
			return NOTIFY_BAD;
		break;

	case LINK_UP:
		queue_delayed_work(kpcitest_workqueue, &epf_test->cmd_handler,
				   msecs_to_jiffies(1));
		break;

	default:
		dev_err(&epf->dev, "Invalid EPF test notifier event\n");
		return NOTIFY_BAD;
	}

	return NOTIFY_OK;
}

static int pci_epf_test_alloc_space(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	struct device *dev = &epf->dev;
	struct pci_epf_bar *epf_bar;
	size_t msix_table_size = 0;
	size_t test_reg_bar_size;
	size_t pba_size = 0;
	bool msix_capable;
	void *base;
	int bar, add;
	enum pci_barno test_reg_bar = epf_test->test_reg_bar;
	const struct pci_epc_features *epc_features;
	size_t test_reg_size;

	epc_features = epf_test->epc_features;

	test_reg_bar_size = ALIGN(sizeof(struct pci_epf_test_reg), 128);

	msix_capable = epc_features->msix_capable;
	if (msix_capable) {
		msix_table_size = PCI_MSIX_ENTRY_SIZE * epf->msix_interrupts;
		epf_test->msix_table_offset = test_reg_bar_size;
		/* Align to QWORD or 8 Bytes */
		pba_size = ALIGN(DIV_ROUND_UP(epf->msix_interrupts, 8), 8);
	}
	test_reg_size = test_reg_bar_size + msix_table_size + pba_size;

	if (epc_features->bar_fixed_size[test_reg_bar]) {
		if (test_reg_size > bar_size[test_reg_bar])
			return -ENOMEM;
		test_reg_size = bar_size[test_reg_bar];
	}

	base = pci_epf_alloc_space(epf, test_reg_size, test_reg_bar,
				   epc_features->align);
	if (!base) {
		dev_err(dev, "Failed to allocated register space\n");
		return -ENOMEM;
	}
	epf_test->reg[test_reg_bar] = base;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar += add) {
		epf_bar = &epf->bar[bar];
		add = (epf_bar->flags & PCI_BASE_ADDRESS_MEM_TYPE_64) ? 2 : 1;

		if (bar == test_reg_bar)
			continue;

		if (!!(epc_features->reserved_bar & (1 << bar)))
			continue;

		base = pci_epf_alloc_space(epf, bar_size[bar], bar,
					   epc_features->align);
		if (!base)
			dev_err(dev, "Failed to allocate space for BAR%d\n",
				bar);
		epf_test->reg[bar] = base;
	}

	return 0;
}

static void pci_epf_configure_bar(struct pci_epf *epf,
				  const struct pci_epc_features *epc_features)
{
	struct pci_epf_bar *epf_bar;
	bool bar_fixed_64bit;
	int i;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		epf_bar = &epf->bar[i];
		bar_fixed_64bit = !!(epc_features->bar_fixed_64bit & (1 << i));
		if (bar_fixed_64bit)
			epf_bar->flags |= PCI_BASE_ADDRESS_MEM_TYPE_64;
		if (epc_features->bar_fixed_size[i])
			bar_size[i] = epc_features->bar_fixed_size[i];
	}
}

static int pci_epf_test_bind(struct pci_epf *epf)
{
	int ret;
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	const struct pci_epc_features *epc_features;
	enum pci_barno test_reg_bar = BAR_0;
	struct pci_epc *epc = epf->epc;
	bool linkup_notifier = false;
	bool core_init_notifier = false;

	if (WARN_ON_ONCE(!epc))
		return -EINVAL;

	epc_features = pci_epc_get_features(epc, epf->func_no);
	if (!epc_features) {
		dev_err(&epf->dev, "epc_features not implemented\n");
		return -EOPNOTSUPP;
	}

	linkup_notifier = epc_features->linkup_notifier;
	core_init_notifier = epc_features->core_init_notifier;
	test_reg_bar = pci_epc_get_first_free_bar(epc_features);
	if (test_reg_bar < 0)
		return -EINVAL;
	pci_epf_configure_bar(epf, epc_features);

	epf_test->test_reg_bar = test_reg_bar;
	epf_test->epc_features = epc_features;

	ret = pci_epf_test_alloc_space(epf);
	if (ret)
		return ret;

	if (!core_init_notifier) {
		ret = pci_epf_test_core_init(epf);
		if (ret)
			return ret;
	}

	epf_test->dma_supported = true;

	ret = pci_epf_test_init_dma_chan(epf_test);
	if (ret)
		epf_test->dma_supported = false;

	if (linkup_notifier || core_init_notifier) {
		epf->nb.notifier_call = pci_epf_test_notifier;
		pci_epc_register_notifier(epc, &epf->nb);
	} else {
		queue_work(kpcitest_workqueue, &epf_test->cmd_handler.work);
	}

	return 0;
}

static const struct pci_epf_device_id pci_epf_test_ids[] = {
	{
		.name = "pci_epf_test",
	},
	{},
};

static int pci_epf_test_probe(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test;
	struct device *dev = &epf->dev;

	epf_test = devm_kzalloc(dev, sizeof(*epf_test), GFP_KERNEL);
	if (!epf_test)
		return -ENOMEM;

	epf->header = &test_header;
	epf_test->epf = epf;

	INIT_DELAYED_WORK(&epf_test->cmd_handler, pci_epf_test_cmd_handler);

	epf_set_drvdata(epf, epf_test);
	return 0;
}

static struct pci_epf_ops ops = {
	.unbind	= pci_epf_test_unbind,
	.bind	= pci_epf_test_bind,
};

static struct pci_epf_driver test_driver = {
	.driver.name	= "pci_epf_test",
	.probe		= pci_epf_test_probe,
	.id_table	= pci_epf_test_ids,
	.ops		= &ops,
	.owner		= THIS_MODULE,
};

static int __init pci_epf_test_init(void)
{
	int ret;

	kpcitest_workqueue = alloc_workqueue("kpcitest",
					     WQ_MEM_RECLAIM | WQ_HIGHPRI, 0);
	if (!kpcitest_workqueue) {
		pr_err("Failed to allocate the kpcitest work queue\n");
		return -ENOMEM;
	}

	ret = pci_epf_register_driver(&test_driver);
	if (ret) {
		destroy_workqueue(kpcitest_workqueue);
		pr_err("Failed to register pci epf test driver --> %d\n", ret);
		return ret;
	}

	return 0;
}
module_init(pci_epf_test_init);

static void __exit pci_epf_test_exit(void)
{
	if (kpcitest_workqueue)
		destroy_workqueue(kpcitest_workqueue);
	pci_epf_unregister_driver(&test_driver);
}
module_exit(pci_epf_test_exit);

MODULE_DESCRIPTION("PCI EPF TEST DRIVER");
MODULE_AUTHOR("Kishon Vijay Abraham I <kishon@ti.com>");
MODULE_LICENSE("GPL v2");
