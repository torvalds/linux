// SPDX-License-Identifier: GPL-2.0
/*
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

#define IRQ_TYPE_INTX			0
#define IRQ_TYPE_MSI			1
#define IRQ_TYPE_MSIX			2

#define COMMAND_RAISE_INTX_IRQ		BIT(0)
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

#define CAP_UNALIGNED_ACCESS		BIT(0)

static struct workqueue_struct *kpcitest_workqueue;

struct pci_epf_test {
	void			*reg[PCI_STD_NUM_BARS];
	struct pci_epf		*epf;
	enum pci_barno		test_reg_bar;
	size_t			msix_table_offset;
	struct delayed_work	cmd_handler;
	struct dma_chan		*dma_chan_tx;
	struct dma_chan		*dma_chan_rx;
	struct dma_chan		*transfer_chan;
	dma_cookie_t		transfer_cookie;
	enum dma_status		transfer_status;
	struct completion	transfer_complete;
	bool			dma_supported;
	bool			dma_private;
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
	u32	caps;
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
	struct dma_tx_state state;

	epf_test->transfer_status =
		dmaengine_tx_status(epf_test->transfer_chan,
				    epf_test->transfer_cookie, &state);
	if (epf_test->transfer_status == DMA_COMPLETE ||
	    epf_test->transfer_status == DMA_ERROR)
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
 * @dma_remote: remote RC physical address
 * @dir: DMA transfer direction
 *
 * Function that uses dmaengine API to transfer data between PCIe EP and remote
 * PCIe RC. The source and destination address can be a physical address given
 * by pci_epc_mem_alloc_addr or the one obtained using DMA mapping APIs.
 *
 * The function returns '0' on success and negative value on failure.
 */
static int pci_epf_test_data_transfer(struct pci_epf_test *epf_test,
				      dma_addr_t dma_dst, dma_addr_t dma_src,
				      size_t len, dma_addr_t dma_remote,
				      enum dma_transfer_direction dir)
{
	struct dma_chan *chan = (dir == DMA_MEM_TO_DEV) ?
				 epf_test->dma_chan_tx : epf_test->dma_chan_rx;
	dma_addr_t dma_local = (dir == DMA_MEM_TO_DEV) ? dma_src : dma_dst;
	enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	struct pci_epf *epf = epf_test->epf;
	struct dma_async_tx_descriptor *tx;
	struct dma_slave_config sconf = {};
	struct device *dev = &epf->dev;
	int ret;

	if (IS_ERR_OR_NULL(chan)) {
		dev_err(dev, "Invalid DMA memcpy channel\n");
		return -EINVAL;
	}

	if (epf_test->dma_private) {
		sconf.direction = dir;
		if (dir == DMA_MEM_TO_DEV)
			sconf.dst_addr = dma_remote;
		else
			sconf.src_addr = dma_remote;

		if (dmaengine_slave_config(chan, &sconf)) {
			dev_err(dev, "DMA slave config fail\n");
			return -EIO;
		}
		tx = dmaengine_prep_slave_single(chan, dma_local, len, dir,
						 flags);
	} else {
		tx = dmaengine_prep_dma_memcpy(chan, dma_dst, dma_src, len,
					       flags);
	}

	if (!tx) {
		dev_err(dev, "Failed to prepare DMA memcpy\n");
		return -EIO;
	}

	reinit_completion(&epf_test->transfer_complete);
	epf_test->transfer_chan = chan;
	tx->callback = pci_epf_test_dma_callback;
	tx->callback_param = epf_test;
	epf_test->transfer_cookie = dmaengine_submit(tx);

	ret = dma_submit_error(epf_test->transfer_cookie);
	if (ret) {
		dev_err(dev, "Failed to do DMA tx_submit %d\n", ret);
		goto terminate;
	}

	dma_async_issue_pending(chan);
	ret = wait_for_completion_interruptible(&epf_test->transfer_complete);
	if (ret < 0) {
		dev_err(dev, "DMA wait_for_completion interrupted\n");
		goto terminate;
	}

	if (epf_test->transfer_status == DMA_ERROR) {
		dev_err(dev, "DMA transfer failed\n");
		ret = -EIO;
	}

terminate:
	dmaengine_terminate_sync(chan);

	return ret;
}

struct epf_dma_filter {
	struct device *dev;
	u32 dma_mask;
};

static bool epf_dma_filter_fn(struct dma_chan *chan, void *node)
{
	struct epf_dma_filter *filter = node;
	struct dma_slave_caps caps;

	memset(&caps, 0, sizeof(caps));
	dma_get_slave_caps(chan, &caps);

	return chan->device->dev == filter->dev
		&& (filter->dma_mask & caps.directions);
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
	struct epf_dma_filter filter;
	struct dma_chan *dma_chan;
	dma_cap_mask_t mask;
	int ret;

	filter.dev = epf->epc->dev.parent;
	filter.dma_mask = BIT(DMA_DEV_TO_MEM);

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_chan = dma_request_channel(mask, epf_dma_filter_fn, &filter);
	if (!dma_chan) {
		dev_info(dev, "Failed to get private DMA rx channel. Falling back to generic one\n");
		goto fail_back_tx;
	}

	epf_test->dma_chan_rx = dma_chan;

	filter.dma_mask = BIT(DMA_MEM_TO_DEV);
	dma_chan = dma_request_channel(mask, epf_dma_filter_fn, &filter);

	if (!dma_chan) {
		dev_info(dev, "Failed to get private DMA tx channel. Falling back to generic one\n");
		goto fail_back_rx;
	}

	epf_test->dma_chan_tx = dma_chan;
	epf_test->dma_private = true;

	init_completion(&epf_test->transfer_complete);

	return 0;

fail_back_rx:
	dma_release_channel(epf_test->dma_chan_rx);
	epf_test->dma_chan_rx = NULL;

fail_back_tx:
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

	epf_test->dma_chan_tx = epf_test->dma_chan_rx = dma_chan;

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

	dma_release_channel(epf_test->dma_chan_tx);
	if (epf_test->dma_chan_tx == epf_test->dma_chan_rx) {
		epf_test->dma_chan_tx = NULL;
		epf_test->dma_chan_rx = NULL;
		return;
	}

	dma_release_channel(epf_test->dma_chan_rx);
	epf_test->dma_chan_rx = NULL;
}

static void pci_epf_test_print_rate(struct pci_epf_test *epf_test,
				    const char *op, u64 size,
				    struct timespec64 *start,
				    struct timespec64 *end, bool dma)
{
	struct timespec64 ts = timespec64_sub(*end, *start);
	u64 rate = 0, ns;

	/* calculate the rate */
	ns = timespec64_to_ns(&ts);
	if (ns)
		rate = div64_u64(size * NSEC_PER_SEC, ns * 1000);

	dev_info(&epf_test->epf->dev,
		 "%s => Size: %llu B, DMA: %s, Time: %llu.%09u s, Rate: %llu KB/s\n",
		 op, size, dma ? "YES" : "NO",
		 (u64)ts.tv_sec, (u32)ts.tv_nsec, rate);
}

static void pci_epf_test_copy(struct pci_epf_test *epf_test,
			      struct pci_epf_test_reg *reg)
{
	int ret = 0;
	struct timespec64 start, end;
	struct pci_epf *epf = epf_test->epf;
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;
	struct pci_epc_map src_map, dst_map;
	u64 src_addr = reg->src_addr;
	u64 dst_addr = reg->dst_addr;
	size_t copy_size = reg->size;
	ssize_t map_size = 0;
	void *copy_buf = NULL, *buf;

	if (reg->flags & FLAG_USE_DMA) {
		if (!dma_has_cap(DMA_MEMCPY, epf_test->dma_chan_tx->device->cap_mask)) {
			dev_err(dev, "DMA controller doesn't support MEMCPY\n");
			ret = -EINVAL;
			goto set_status;
		}
	} else {
		copy_buf = kzalloc(copy_size, GFP_KERNEL);
		if (!copy_buf) {
			ret = -ENOMEM;
			goto set_status;
		}
		buf = copy_buf;
	}

	while (copy_size) {
		ret = pci_epc_mem_map(epc, epf->func_no, epf->vfunc_no,
				      src_addr, copy_size, &src_map);
		if (ret) {
			dev_err(dev, "Failed to map source address\n");
			reg->status = STATUS_SRC_ADDR_INVALID;
			goto free_buf;
		}

		ret = pci_epc_mem_map(epf->epc, epf->func_no, epf->vfunc_no,
					   dst_addr, copy_size, &dst_map);
		if (ret) {
			dev_err(dev, "Failed to map destination address\n");
			reg->status = STATUS_DST_ADDR_INVALID;
			pci_epc_mem_unmap(epc, epf->func_no, epf->vfunc_no,
					  &src_map);
			goto free_buf;
		}

		map_size = min_t(size_t, dst_map.pci_size, src_map.pci_size);

		ktime_get_ts64(&start);
		if (reg->flags & FLAG_USE_DMA) {
			ret = pci_epf_test_data_transfer(epf_test,
					dst_map.phys_addr, src_map.phys_addr,
					map_size, 0, DMA_MEM_TO_MEM);
			if (ret) {
				dev_err(dev, "Data transfer failed\n");
				goto unmap;
			}
		} else {
			memcpy_fromio(buf, src_map.virt_addr, map_size);
			memcpy_toio(dst_map.virt_addr, buf, map_size);
			buf += map_size;
		}
		ktime_get_ts64(&end);

		copy_size -= map_size;
		src_addr += map_size;
		dst_addr += map_size;

		pci_epc_mem_unmap(epc, epf->func_no, epf->vfunc_no, &dst_map);
		pci_epc_mem_unmap(epc, epf->func_no, epf->vfunc_no, &src_map);
		map_size = 0;
	}

	pci_epf_test_print_rate(epf_test, "COPY", reg->size, &start,
				&end, reg->flags & FLAG_USE_DMA);

unmap:
	if (map_size) {
		pci_epc_mem_unmap(epc, epf->func_no, epf->vfunc_no, &dst_map);
		pci_epc_mem_unmap(epc, epf->func_no, epf->vfunc_no, &src_map);
	}

free_buf:
	kfree(copy_buf);

set_status:
	if (!ret)
		reg->status |= STATUS_COPY_SUCCESS;
	else
		reg->status |= STATUS_COPY_FAIL;
}

static void pci_epf_test_read(struct pci_epf_test *epf_test,
			      struct pci_epf_test_reg *reg)
{
	int ret = 0;
	void *src_buf, *buf;
	u32 crc32;
	struct pci_epc_map map;
	phys_addr_t dst_phys_addr;
	struct timespec64 start, end;
	struct pci_epf *epf = epf_test->epf;
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;
	struct device *dma_dev = epf->epc->dev.parent;
	u64 src_addr = reg->src_addr;
	size_t src_size = reg->size;
	ssize_t map_size = 0;

	src_buf = kzalloc(src_size, GFP_KERNEL);
	if (!src_buf) {
		ret = -ENOMEM;
		goto set_status;
	}
	buf = src_buf;

	while (src_size) {
		ret = pci_epc_mem_map(epc, epf->func_no, epf->vfunc_no,
					   src_addr, src_size, &map);
		if (ret) {
			dev_err(dev, "Failed to map address\n");
			reg->status = STATUS_SRC_ADDR_INVALID;
			goto free_buf;
		}

		map_size = map.pci_size;
		if (reg->flags & FLAG_USE_DMA) {
			dst_phys_addr = dma_map_single(dma_dev, buf, map_size,
						       DMA_FROM_DEVICE);
			if (dma_mapping_error(dma_dev, dst_phys_addr)) {
				dev_err(dev,
					"Failed to map destination buffer addr\n");
				ret = -ENOMEM;
				goto unmap;
			}

			ktime_get_ts64(&start);
			ret = pci_epf_test_data_transfer(epf_test,
					dst_phys_addr, map.phys_addr,
					map_size, src_addr, DMA_DEV_TO_MEM);
			if (ret)
				dev_err(dev, "Data transfer failed\n");
			ktime_get_ts64(&end);

			dma_unmap_single(dma_dev, dst_phys_addr, map_size,
					 DMA_FROM_DEVICE);

			if (ret)
				goto unmap;
		} else {
			ktime_get_ts64(&start);
			memcpy_fromio(buf, map.virt_addr, map_size);
			ktime_get_ts64(&end);
		}

		src_size -= map_size;
		src_addr += map_size;
		buf += map_size;

		pci_epc_mem_unmap(epc, epf->func_no, epf->vfunc_no, &map);
		map_size = 0;
	}

	pci_epf_test_print_rate(epf_test, "READ", reg->size, &start,
				&end, reg->flags & FLAG_USE_DMA);

	crc32 = crc32_le(~0, src_buf, reg->size);
	if (crc32 != reg->checksum)
		ret = -EIO;

unmap:
	if (map_size)
		pci_epc_mem_unmap(epc, epf->func_no, epf->vfunc_no, &map);

free_buf:
	kfree(src_buf);

set_status:
	if (!ret)
		reg->status |= STATUS_READ_SUCCESS;
	else
		reg->status |= STATUS_READ_FAIL;
}

static void pci_epf_test_write(struct pci_epf_test *epf_test,
			       struct pci_epf_test_reg *reg)
{
	int ret = 0;
	void *dst_buf, *buf;
	struct pci_epc_map map;
	phys_addr_t src_phys_addr;
	struct timespec64 start, end;
	struct pci_epf *epf = epf_test->epf;
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;
	struct device *dma_dev = epf->epc->dev.parent;
	u64 dst_addr = reg->dst_addr;
	size_t dst_size = reg->size;
	ssize_t map_size = 0;

	dst_buf = kzalloc(dst_size, GFP_KERNEL);
	if (!dst_buf) {
		ret = -ENOMEM;
		goto set_status;
	}
	get_random_bytes(dst_buf, dst_size);
	reg->checksum = crc32_le(~0, dst_buf, dst_size);
	buf = dst_buf;

	while (dst_size) {
		ret = pci_epc_mem_map(epc, epf->func_no, epf->vfunc_no,
					   dst_addr, dst_size, &map);
		if (ret) {
			dev_err(dev, "Failed to map address\n");
			reg->status = STATUS_DST_ADDR_INVALID;
			goto free_buf;
		}

		map_size = map.pci_size;
		if (reg->flags & FLAG_USE_DMA) {
			src_phys_addr = dma_map_single(dma_dev, buf, map_size,
						       DMA_TO_DEVICE);
			if (dma_mapping_error(dma_dev, src_phys_addr)) {
				dev_err(dev,
					"Failed to map source buffer addr\n");
				ret = -ENOMEM;
				goto unmap;
			}

			ktime_get_ts64(&start);

			ret = pci_epf_test_data_transfer(epf_test,
						map.phys_addr, src_phys_addr,
						map_size, dst_addr,
						DMA_MEM_TO_DEV);
			if (ret)
				dev_err(dev, "Data transfer failed\n");
			ktime_get_ts64(&end);

			dma_unmap_single(dma_dev, src_phys_addr, map_size,
					 DMA_TO_DEVICE);

			if (ret)
				goto unmap;
		} else {
			ktime_get_ts64(&start);
			memcpy_toio(map.virt_addr, buf, map_size);
			ktime_get_ts64(&end);
		}

		dst_size -= map_size;
		dst_addr += map_size;
		buf += map_size;

		pci_epc_mem_unmap(epc, epf->func_no, epf->vfunc_no, &map);
		map_size = 0;
	}

	pci_epf_test_print_rate(epf_test, "WRITE", reg->size, &start,
				&end, reg->flags & FLAG_USE_DMA);

	/*
	 * wait 1ms inorder for the write to complete. Without this delay L3
	 * error in observed in the host system.
	 */
	usleep_range(1000, 2000);

unmap:
	if (map_size)
		pci_epc_mem_unmap(epc, epf->func_no, epf->vfunc_no, &map);

free_buf:
	kfree(dst_buf);

set_status:
	if (!ret)
		reg->status |= STATUS_WRITE_SUCCESS;
	else
		reg->status |= STATUS_WRITE_FAIL;
}

static void pci_epf_test_raise_irq(struct pci_epf_test *epf_test,
				   struct pci_epf_test_reg *reg)
{
	struct pci_epf *epf = epf_test->epf;
	struct device *dev = &epf->dev;
	struct pci_epc *epc = epf->epc;
	u32 status = reg->status | STATUS_IRQ_RAISED;
	int count;

	/*
	 * Set the status before raising the IRQ to ensure that the host sees
	 * the updated value when it gets the IRQ.
	 */
	WRITE_ONCE(reg->status, status);

	switch (reg->irq_type) {
	case IRQ_TYPE_INTX:
		pci_epc_raise_irq(epc, epf->func_no, epf->vfunc_no,
				  PCI_IRQ_INTX, 0);
		break;
	case IRQ_TYPE_MSI:
		count = pci_epc_get_msi(epc, epf->func_no, epf->vfunc_no);
		if (reg->irq_number > count || count <= 0) {
			dev_err(dev, "Invalid MSI IRQ number %d / %d\n",
				reg->irq_number, count);
			return;
		}
		pci_epc_raise_irq(epc, epf->func_no, epf->vfunc_no,
				  PCI_IRQ_MSI, reg->irq_number);
		break;
	case IRQ_TYPE_MSIX:
		count = pci_epc_get_msix(epc, epf->func_no, epf->vfunc_no);
		if (reg->irq_number > count || count <= 0) {
			dev_err(dev, "Invalid MSIX IRQ number %d / %d\n",
				reg->irq_number, count);
			return;
		}
		pci_epc_raise_irq(epc, epf->func_no, epf->vfunc_no,
				  PCI_IRQ_MSIX, reg->irq_number);
		break;
	default:
		dev_err(dev, "Failed to raise IRQ, unknown type\n");
		break;
	}
}

static void pci_epf_test_cmd_handler(struct work_struct *work)
{
	u32 command;
	struct pci_epf_test *epf_test = container_of(work, struct pci_epf_test,
						     cmd_handler.work);
	struct pci_epf *epf = epf_test->epf;
	struct device *dev = &epf->dev;
	enum pci_barno test_reg_bar = epf_test->test_reg_bar;
	struct pci_epf_test_reg *reg = epf_test->reg[test_reg_bar];

	command = READ_ONCE(reg->command);
	if (!command)
		goto reset_handler;

	WRITE_ONCE(reg->command, 0);
	WRITE_ONCE(reg->status, 0);

	if ((READ_ONCE(reg->flags) & FLAG_USE_DMA) &&
	    !epf_test->dma_supported) {
		dev_err(dev, "Cannot transfer data using DMA\n");
		goto reset_handler;
	}

	if (reg->irq_type > IRQ_TYPE_MSIX) {
		dev_err(dev, "Failed to detect IRQ type\n");
		goto reset_handler;
	}

	switch (command) {
	case COMMAND_RAISE_INTX_IRQ:
	case COMMAND_RAISE_MSI_IRQ:
	case COMMAND_RAISE_MSIX_IRQ:
		pci_epf_test_raise_irq(epf_test, reg);
		break;
	case COMMAND_WRITE:
		pci_epf_test_write(epf_test, reg);
		pci_epf_test_raise_irq(epf_test, reg);
		break;
	case COMMAND_READ:
		pci_epf_test_read(epf_test, reg);
		pci_epf_test_raise_irq(epf_test, reg);
		break;
	case COMMAND_COPY:
		pci_epf_test_copy(epf_test, reg);
		pci_epf_test_raise_irq(epf_test, reg);
		break;
	default:
		dev_err(dev, "Invalid command 0x%x\n", command);
		break;
	}

reset_handler:
	queue_delayed_work(kpcitest_workqueue, &epf_test->cmd_handler,
			   msecs_to_jiffies(1));
}

static int pci_epf_test_set_bar(struct pci_epf *epf)
{
	int bar, ret;
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	enum pci_barno test_reg_bar = epf_test->test_reg_bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (!epf_test->reg[bar])
			continue;

		ret = pci_epc_set_bar(epc, epf->func_no, epf->vfunc_no,
				      &epf->bar[bar]);
		if (ret) {
			pci_epf_free_space(epf, epf_test->reg[bar], bar,
					   PRIMARY_INTERFACE);
			dev_err(dev, "Failed to set BAR%d\n", bar);
			if (bar == test_reg_bar)
				return ret;
		}
	}

	return 0;
}

static void pci_epf_test_clear_bar(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	struct pci_epc *epc = epf->epc;
	int bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (!epf_test->reg[bar])
			continue;

		pci_epc_clear_bar(epc, epf->func_no, epf->vfunc_no,
				  &epf->bar[bar]);
	}
}

static void pci_epf_test_set_capabilities(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	enum pci_barno test_reg_bar = epf_test->test_reg_bar;
	struct pci_epf_test_reg *reg = epf_test->reg[test_reg_bar];
	struct pci_epc *epc = epf->epc;
	u32 caps = 0;

	if (epc->ops->align_addr)
		caps |= CAP_UNALIGNED_ACCESS;

	reg->caps = cpu_to_le32(caps);
}

static int pci_epf_test_epc_init(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	struct pci_epf_header *header = epf->header;
	const struct pci_epc_features *epc_features = epf_test->epc_features;
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;
	bool linkup_notifier = false;
	int ret;

	epf_test->dma_supported = true;

	ret = pci_epf_test_init_dma_chan(epf_test);
	if (ret)
		epf_test->dma_supported = false;

	if (epf->vfunc_no <= 1) {
		ret = pci_epc_write_header(epc, epf->func_no, epf->vfunc_no, header);
		if (ret) {
			dev_err(dev, "Configuration header write failed\n");
			return ret;
		}
	}

	pci_epf_test_set_capabilities(epf);

	ret = pci_epf_test_set_bar(epf);
	if (ret)
		return ret;

	if (epc_features->msi_capable) {
		ret = pci_epc_set_msi(epc, epf->func_no, epf->vfunc_no,
				      epf->msi_interrupts);
		if (ret) {
			dev_err(dev, "MSI configuration failed\n");
			return ret;
		}
	}

	if (epc_features->msix_capable) {
		ret = pci_epc_set_msix(epc, epf->func_no, epf->vfunc_no,
				       epf->msix_interrupts,
				       epf_test->test_reg_bar,
				       epf_test->msix_table_offset);
		if (ret) {
			dev_err(dev, "MSI-X configuration failed\n");
			return ret;
		}
	}

	linkup_notifier = epc_features->linkup_notifier;
	if (!linkup_notifier)
		queue_work(kpcitest_workqueue, &epf_test->cmd_handler.work);

	return 0;
}

static void pci_epf_test_epc_deinit(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);

	cancel_delayed_work_sync(&epf_test->cmd_handler);
	pci_epf_test_clean_dma_chan(epf_test);
	pci_epf_test_clear_bar(epf);
}

static int pci_epf_test_link_up(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);

	queue_delayed_work(kpcitest_workqueue, &epf_test->cmd_handler,
			   msecs_to_jiffies(1));

	return 0;
}

static int pci_epf_test_link_down(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);

	cancel_delayed_work_sync(&epf_test->cmd_handler);

	return 0;
}

static const struct pci_epc_event_ops pci_epf_test_event_ops = {
	.epc_init = pci_epf_test_epc_init,
	.epc_deinit = pci_epf_test_epc_deinit,
	.link_up = pci_epf_test_link_up,
	.link_down = pci_epf_test_link_down,
};

static int pci_epf_test_alloc_space(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	struct device *dev = &epf->dev;
	size_t msix_table_size = 0;
	size_t test_reg_bar_size;
	size_t pba_size = 0;
	void *base;
	enum pci_barno test_reg_bar = epf_test->test_reg_bar;
	enum pci_barno bar;
	const struct pci_epc_features *epc_features = epf_test->epc_features;
	size_t test_reg_size;

	test_reg_bar_size = ALIGN(sizeof(struct pci_epf_test_reg), 128);

	if (epc_features->msix_capable) {
		msix_table_size = PCI_MSIX_ENTRY_SIZE * epf->msix_interrupts;
		epf_test->msix_table_offset = test_reg_bar_size;
		/* Align to QWORD or 8 Bytes */
		pba_size = ALIGN(DIV_ROUND_UP(epf->msix_interrupts, 8), 8);
	}
	test_reg_size = test_reg_bar_size + msix_table_size + pba_size;

	base = pci_epf_alloc_space(epf, test_reg_size, test_reg_bar,
				   epc_features, PRIMARY_INTERFACE);
	if (!base) {
		dev_err(dev, "Failed to allocated register space\n");
		return -ENOMEM;
	}
	epf_test->reg[test_reg_bar] = base;

	for (bar = BAR_0; bar < PCI_STD_NUM_BARS; bar++) {
		bar = pci_epc_get_next_free_bar(epc_features, bar);
		if (bar == NO_BAR)
			break;

		if (bar == test_reg_bar)
			continue;

		base = pci_epf_alloc_space(epf, bar_size[bar], bar,
					   epc_features, PRIMARY_INTERFACE);
		if (!base)
			dev_err(dev, "Failed to allocate space for BAR%d\n",
				bar);
		epf_test->reg[bar] = base;
	}

	return 0;
}

static void pci_epf_test_free_space(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	int bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (!epf_test->reg[bar])
			continue;

		pci_epf_free_space(epf, epf_test->reg[bar], bar,
				   PRIMARY_INTERFACE);
	}
}

static int pci_epf_test_bind(struct pci_epf *epf)
{
	int ret;
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	const struct pci_epc_features *epc_features;
	enum pci_barno test_reg_bar = BAR_0;
	struct pci_epc *epc = epf->epc;

	if (WARN_ON_ONCE(!epc))
		return -EINVAL;

	epc_features = pci_epc_get_features(epc, epf->func_no, epf->vfunc_no);
	if (!epc_features) {
		dev_err(&epf->dev, "epc_features not implemented\n");
		return -EOPNOTSUPP;
	}

	test_reg_bar = pci_epc_get_first_free_bar(epc_features);
	if (test_reg_bar < 0)
		return -EINVAL;

	epf_test->test_reg_bar = test_reg_bar;
	epf_test->epc_features = epc_features;

	ret = pci_epf_test_alloc_space(epf);
	if (ret)
		return ret;

	return 0;
}

static void pci_epf_test_unbind(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	struct pci_epc *epc = epf->epc;

	cancel_delayed_work_sync(&epf_test->cmd_handler);
	if (epc->init_complete) {
		pci_epf_test_clean_dma_chan(epf_test);
		pci_epf_test_clear_bar(epf);
	}
	pci_epf_test_free_space(epf);
}

static const struct pci_epf_device_id pci_epf_test_ids[] = {
	{
		.name = "pci_epf_test",
	},
	{},
};

static int pci_epf_test_probe(struct pci_epf *epf,
			      const struct pci_epf_device_id *id)
{
	struct pci_epf_test *epf_test;
	struct device *dev = &epf->dev;

	epf_test = devm_kzalloc(dev, sizeof(*epf_test), GFP_KERNEL);
	if (!epf_test)
		return -ENOMEM;

	epf->header = &test_header;
	epf_test->epf = epf;

	INIT_DELAYED_WORK(&epf_test->cmd_handler, pci_epf_test_cmd_handler);

	epf->event_ops = &pci_epf_test_event_ops;

	epf_set_drvdata(epf, epf_test);
	return 0;
}

static const struct pci_epf_ops ops = {
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
