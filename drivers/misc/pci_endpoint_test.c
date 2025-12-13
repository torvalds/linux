// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host side test driver to test endpoint functionality
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/crc32.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#include <linux/pci_regs.h>

#include <uapi/linux/pcitest.h>

#define DRV_MODULE_NAME				"pci-endpoint-test"

#define PCI_ENDPOINT_TEST_MAGIC			0x0

#define PCI_ENDPOINT_TEST_COMMAND		0x4
#define COMMAND_RAISE_INTX_IRQ			BIT(0)
#define COMMAND_RAISE_MSI_IRQ			BIT(1)
#define COMMAND_RAISE_MSIX_IRQ			BIT(2)
#define COMMAND_READ				BIT(3)
#define COMMAND_WRITE				BIT(4)
#define COMMAND_COPY				BIT(5)
#define COMMAND_ENABLE_DOORBELL			BIT(6)
#define COMMAND_DISABLE_DOORBELL		BIT(7)

#define PCI_ENDPOINT_TEST_STATUS		0x8
#define STATUS_READ_SUCCESS			BIT(0)
#define STATUS_READ_FAIL			BIT(1)
#define STATUS_WRITE_SUCCESS			BIT(2)
#define STATUS_WRITE_FAIL			BIT(3)
#define STATUS_COPY_SUCCESS			BIT(4)
#define STATUS_COPY_FAIL			BIT(5)
#define STATUS_IRQ_RAISED			BIT(6)
#define STATUS_SRC_ADDR_INVALID			BIT(7)
#define STATUS_DST_ADDR_INVALID			BIT(8)
#define STATUS_DOORBELL_SUCCESS			BIT(9)
#define STATUS_DOORBELL_ENABLE_SUCCESS		BIT(10)
#define STATUS_DOORBELL_ENABLE_FAIL		BIT(11)
#define STATUS_DOORBELL_DISABLE_SUCCESS		BIT(12)
#define STATUS_DOORBELL_DISABLE_FAIL		BIT(13)

#define PCI_ENDPOINT_TEST_LOWER_SRC_ADDR	0x0c
#define PCI_ENDPOINT_TEST_UPPER_SRC_ADDR	0x10

#define PCI_ENDPOINT_TEST_LOWER_DST_ADDR	0x14
#define PCI_ENDPOINT_TEST_UPPER_DST_ADDR	0x18

#define PCI_ENDPOINT_TEST_SIZE			0x1c
#define PCI_ENDPOINT_TEST_CHECKSUM		0x20

#define PCI_ENDPOINT_TEST_IRQ_TYPE		0x24
#define PCI_ENDPOINT_TEST_IRQ_NUMBER		0x28

#define PCI_ENDPOINT_TEST_FLAGS			0x2c

#define FLAG_USE_DMA				BIT(0)

#define PCI_ENDPOINT_TEST_CAPS			0x30
#define CAP_UNALIGNED_ACCESS			BIT(0)
#define CAP_MSI					BIT(1)
#define CAP_MSIX				BIT(2)
#define CAP_INTX				BIT(3)

#define PCI_ENDPOINT_TEST_DB_BAR		0x34
#define PCI_ENDPOINT_TEST_DB_OFFSET		0x38
#define PCI_ENDPOINT_TEST_DB_DATA		0x3c

#define PCI_DEVICE_ID_TI_AM654			0xb00c
#define PCI_DEVICE_ID_TI_J7200			0xb00f
#define PCI_DEVICE_ID_TI_AM64			0xb010
#define PCI_DEVICE_ID_TI_J721S2		0xb013
#define PCI_DEVICE_ID_LS1088A			0x80c0
#define PCI_DEVICE_ID_IMX8			0x0808

#define is_am654_pci_dev(pdev)		\
		((pdev)->device == PCI_DEVICE_ID_TI_AM654)

#define PCI_DEVICE_ID_RENESAS_R8A774A1		0x0028
#define PCI_DEVICE_ID_RENESAS_R8A774B1		0x002b
#define PCI_DEVICE_ID_RENESAS_R8A774C0		0x002d
#define PCI_DEVICE_ID_RENESAS_R8A774E1		0x0025
#define PCI_DEVICE_ID_RENESAS_R8A779F0		0x0031

#define PCI_DEVICE_ID_ROCKCHIP_RK3588		0x3588

static DEFINE_IDA(pci_endpoint_test_ida);

#define to_endpoint_test(priv) container_of((priv), struct pci_endpoint_test, \
					    miscdev)

enum pci_barno {
	BAR_0,
	BAR_1,
	BAR_2,
	BAR_3,
	BAR_4,
	BAR_5,
	NO_BAR = -1,
};

struct pci_endpoint_test {
	struct pci_dev	*pdev;
	void __iomem	*base;
	void __iomem	*bar[PCI_STD_NUM_BARS];
	struct completion irq_raised;
	int		last_irq;
	int		num_irqs;
	int		irq_type;
	/* mutex to protect the ioctls */
	struct mutex	mutex;
	struct miscdevice miscdev;
	enum pci_barno test_reg_bar;
	size_t alignment;
	u32 ep_caps;
	const char *name;
};

struct pci_endpoint_test_data {
	enum pci_barno test_reg_bar;
	size_t alignment;
};

static inline u32 pci_endpoint_test_readl(struct pci_endpoint_test *test,
					  u32 offset)
{
	return readl(test->base + offset);
}

static inline void pci_endpoint_test_writel(struct pci_endpoint_test *test,
					    u32 offset, u32 value)
{
	writel(value, test->base + offset);
}

static irqreturn_t pci_endpoint_test_irqhandler(int irq, void *dev_id)
{
	struct pci_endpoint_test *test = dev_id;
	u32 reg;

	reg = pci_endpoint_test_readl(test, PCI_ENDPOINT_TEST_STATUS);
	if (reg & STATUS_IRQ_RAISED) {
		test->last_irq = irq;
		complete(&test->irq_raised);
	}

	return IRQ_HANDLED;
}

static void pci_endpoint_test_free_irq_vectors(struct pci_endpoint_test *test)
{
	struct pci_dev *pdev = test->pdev;

	pci_free_irq_vectors(pdev);
	test->irq_type = PCITEST_IRQ_TYPE_UNDEFINED;
}

static int pci_endpoint_test_alloc_irq_vectors(struct pci_endpoint_test *test,
						int type)
{
	int irq;
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;

	switch (type) {
	case PCITEST_IRQ_TYPE_INTX:
		irq = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_INTX);
		if (irq < 0) {
			dev_err(dev, "Failed to get Legacy interrupt\n");
			return irq;
		}

		break;
	case PCITEST_IRQ_TYPE_MSI:
		irq = pci_alloc_irq_vectors(pdev, 1, 32, PCI_IRQ_MSI);
		if (irq < 0) {
			dev_err(dev, "Failed to get MSI interrupts\n");
			return irq;
		}

		break;
	case PCITEST_IRQ_TYPE_MSIX:
		irq = pci_alloc_irq_vectors(pdev, 1, 2048, PCI_IRQ_MSIX);
		if (irq < 0) {
			dev_err(dev, "Failed to get MSI-X interrupts\n");
			return irq;
		}

		break;
	default:
		dev_err(dev, "Invalid IRQ type selected\n");
		return -EINVAL;
	}

	test->irq_type = type;
	test->num_irqs = irq;

	return 0;
}

static void pci_endpoint_test_release_irq(struct pci_endpoint_test *test)
{
	int i;
	struct pci_dev *pdev = test->pdev;

	for (i = 0; i < test->num_irqs; i++)
		free_irq(pci_irq_vector(pdev, i), test);

	test->num_irqs = 0;
}

static int pci_endpoint_test_request_irq(struct pci_endpoint_test *test)
{
	int i;
	int ret;
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;

	for (i = 0; i < test->num_irqs; i++) {
		ret = request_irq(pci_irq_vector(pdev, i),
				  pci_endpoint_test_irqhandler, IRQF_SHARED,
				  test->name, test);
		if (ret)
			goto fail;
	}

	return 0;

fail:
	switch (test->irq_type) {
	case PCITEST_IRQ_TYPE_INTX:
		dev_err(dev, "Failed to request IRQ %d for Legacy\n",
			pci_irq_vector(pdev, i));
		break;
	case PCITEST_IRQ_TYPE_MSI:
		dev_err(dev, "Failed to request IRQ %d for MSI %d\n",
			pci_irq_vector(pdev, i),
			i + 1);
		break;
	case PCITEST_IRQ_TYPE_MSIX:
		dev_err(dev, "Failed to request IRQ %d for MSI-X %d\n",
			pci_irq_vector(pdev, i),
			i + 1);
		break;
	}

	test->num_irqs = i;
	pci_endpoint_test_release_irq(test);

	return ret;
}

static const u32 bar_test_pattern[] = {
	0xA0A0A0A0,
	0xA1A1A1A1,
	0xA2A2A2A2,
	0xA3A3A3A3,
	0xA4A4A4A4,
	0xA5A5A5A5,
};

static int pci_endpoint_test_bar_memcmp(struct pci_endpoint_test *test,
					enum pci_barno barno,
					resource_size_t offset, void *write_buf,
					void *read_buf, int size)
{
	memset(write_buf, bar_test_pattern[barno], size);
	memcpy_toio(test->bar[barno] + offset, write_buf, size);

	memcpy_fromio(read_buf, test->bar[barno] + offset, size);

	return memcmp(write_buf, read_buf, size);
}

static int pci_endpoint_test_bar(struct pci_endpoint_test *test,
				  enum pci_barno barno)
{
	resource_size_t bar_size, offset = 0;
	void *write_buf __free(kfree) = NULL;
	void *read_buf __free(kfree) = NULL;
	struct pci_dev *pdev = test->pdev;
	int buf_size;

	bar_size = pci_resource_len(pdev, barno);
	if (!bar_size)
		return -ENODATA;

	if (!test->bar[barno])
		return -ENOMEM;

	if (barno == test->test_reg_bar)
		bar_size = 0x4;

	/*
	 * Allocate a buffer of max size 1MB, and reuse that buffer while
	 * iterating over the whole BAR size (which might be much larger).
	 */
	buf_size = min(SZ_1M, bar_size);

	write_buf = kmalloc(buf_size, GFP_KERNEL);
	if (!write_buf)
		return -ENOMEM;

	read_buf = kmalloc(buf_size, GFP_KERNEL);
	if (!read_buf)
		return -ENOMEM;

	while (offset < bar_size) {
		if (pci_endpoint_test_bar_memcmp(test, barno, offset, write_buf,
						 read_buf, buf_size))
			return -EIO;
		offset += buf_size;
	}

	return 0;
}

static u32 bar_test_pattern_with_offset(enum pci_barno barno, int offset)
{
	u32 val;

	/* Keep the BAR pattern in the top byte. */
	val = bar_test_pattern[barno] & 0xff000000;
	/* Store the (partial) offset in the remaining bytes. */
	val |= offset & 0x00ffffff;

	return val;
}

static void pci_endpoint_test_bars_write_bar(struct pci_endpoint_test *test,
					     enum pci_barno barno)
{
	struct pci_dev *pdev = test->pdev;
	int j, size;

	size = pci_resource_len(pdev, barno);

	if (barno == test->test_reg_bar)
		size = 0x4;

	for (j = 0; j < size; j += 4)
		writel_relaxed(bar_test_pattern_with_offset(barno, j),
			       test->bar[barno] + j);
}

static int pci_endpoint_test_bars_read_bar(struct pci_endpoint_test *test,
					    enum pci_barno barno)
{
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;
	int j, size;
	u32 val;

	size = pci_resource_len(pdev, barno);

	if (barno == test->test_reg_bar)
		size = 0x4;

	for (j = 0; j < size; j += 4) {
		u32 expected = bar_test_pattern_with_offset(barno, j);

		val = readl_relaxed(test->bar[barno] + j);
		if (val != expected) {
			dev_err(dev,
				"BAR%d incorrect data at offset: %#x, got: %#x expected: %#x\n",
				barno, j, val, expected);
			return -EIO;
		}
	}

	return 0;
}

static int pci_endpoint_test_bars(struct pci_endpoint_test *test)
{
	enum pci_barno bar;
	int ret;

	/* Write all BARs in order (without reading). */
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		if (test->bar[bar])
			pci_endpoint_test_bars_write_bar(test, bar);

	/*
	 * Read all BARs in order (without writing).
	 * If there is an address translation issue on the EP, writing one BAR
	 * might have overwritten another BAR. Ensure that this is not the case.
	 * (Reading back the BAR directly after writing can not detect this.)
	 */
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (test->bar[bar]) {
			ret = pci_endpoint_test_bars_read_bar(test, bar);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int pci_endpoint_test_intx_irq(struct pci_endpoint_test *test)
{
	u32 val;

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_TYPE,
				 PCITEST_IRQ_TYPE_INTX);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_NUMBER, 0);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_COMMAND,
				 COMMAND_RAISE_INTX_IRQ);
	val = wait_for_completion_timeout(&test->irq_raised,
					  msecs_to_jiffies(1000));
	if (!val)
		return -ETIMEDOUT;

	return 0;
}

static int pci_endpoint_test_msi_irq(struct pci_endpoint_test *test,
				       u16 msi_num, bool msix)
{
	struct pci_dev *pdev = test->pdev;
	u32 val;
	int irq;

	irq = pci_irq_vector(pdev, msi_num - 1);
	if (irq < 0)
		return irq;

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_TYPE,
				 msix ? PCITEST_IRQ_TYPE_MSIX :
				 PCITEST_IRQ_TYPE_MSI);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_NUMBER, msi_num);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_COMMAND,
				 msix ? COMMAND_RAISE_MSIX_IRQ :
				 COMMAND_RAISE_MSI_IRQ);
	val = wait_for_completion_timeout(&test->irq_raised,
					  msecs_to_jiffies(1000));
	if (!val)
		return -ETIMEDOUT;

	if (irq != test->last_irq)
		return -EIO;

	return 0;
}

static int pci_endpoint_test_validate_xfer_params(struct device *dev,
		struct pci_endpoint_test_xfer_param *param, size_t alignment)
{
	if (!param->size) {
		dev_dbg(dev, "Data size is zero\n");
		return -EINVAL;
	}

	if (param->size > SIZE_MAX - alignment) {
		dev_dbg(dev, "Maximum transfer data size exceeded\n");
		return -EINVAL;
	}

	return 0;
}

static int pci_endpoint_test_copy(struct pci_endpoint_test *test,
				   unsigned long arg)
{
	struct pci_endpoint_test_xfer_param param;
	void *src_addr;
	void *dst_addr;
	u32 flags = 0;
	bool use_dma;
	size_t size;
	dma_addr_t src_phys_addr;
	dma_addr_t dst_phys_addr;
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;
	void *orig_src_addr;
	dma_addr_t orig_src_phys_addr;
	void *orig_dst_addr;
	dma_addr_t orig_dst_phys_addr;
	size_t offset;
	size_t alignment = test->alignment;
	int irq_type = test->irq_type;
	u32 src_crc32;
	u32 dst_crc32;
	int ret;

	ret = copy_from_user(&param, (void __user *)arg, sizeof(param));
	if (ret) {
		dev_err(dev, "Failed to get transfer param\n");
		return -EFAULT;
	}

	ret = pci_endpoint_test_validate_xfer_params(dev, &param, alignment);
	if (ret)
		return ret;

	size = param.size;

	use_dma = !!(param.flags & PCITEST_FLAGS_USE_DMA);
	if (use_dma)
		flags |= FLAG_USE_DMA;

	if (irq_type < PCITEST_IRQ_TYPE_INTX ||
	    irq_type > PCITEST_IRQ_TYPE_MSIX) {
		dev_err(dev, "Invalid IRQ type option\n");
		return -EINVAL;
	}

	orig_src_addr = kzalloc(size + alignment, GFP_KERNEL);
	if (!orig_src_addr) {
		dev_err(dev, "Failed to allocate source buffer\n");
		return -ENOMEM;
	}

	get_random_bytes(orig_src_addr, size + alignment);
	orig_src_phys_addr = dma_map_single(dev, orig_src_addr,
					    size + alignment, DMA_TO_DEVICE);
	ret = dma_mapping_error(dev, orig_src_phys_addr);
	if (ret) {
		dev_err(dev, "failed to map source buffer address\n");
		goto err_src_phys_addr;
	}

	if (alignment && !IS_ALIGNED(orig_src_phys_addr, alignment)) {
		src_phys_addr = PTR_ALIGN(orig_src_phys_addr, alignment);
		offset = src_phys_addr - orig_src_phys_addr;
		src_addr = orig_src_addr + offset;
	} else {
		src_phys_addr = orig_src_phys_addr;
		src_addr = orig_src_addr;
	}

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_LOWER_SRC_ADDR,
				 lower_32_bits(src_phys_addr));

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_UPPER_SRC_ADDR,
				 upper_32_bits(src_phys_addr));

	src_crc32 = crc32_le(~0, src_addr, size);

	orig_dst_addr = kzalloc(size + alignment, GFP_KERNEL);
	if (!orig_dst_addr) {
		dev_err(dev, "Failed to allocate destination address\n");
		ret = -ENOMEM;
		goto err_dst_addr;
	}

	orig_dst_phys_addr = dma_map_single(dev, orig_dst_addr,
					    size + alignment, DMA_FROM_DEVICE);
	ret = dma_mapping_error(dev, orig_dst_phys_addr);
	if (ret) {
		dev_err(dev, "failed to map destination buffer address\n");
		goto err_dst_phys_addr;
	}

	if (alignment && !IS_ALIGNED(orig_dst_phys_addr, alignment)) {
		dst_phys_addr = PTR_ALIGN(orig_dst_phys_addr, alignment);
		offset = dst_phys_addr - orig_dst_phys_addr;
		dst_addr = orig_dst_addr + offset;
	} else {
		dst_phys_addr = orig_dst_phys_addr;
		dst_addr = orig_dst_addr;
	}

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_LOWER_DST_ADDR,
				 lower_32_bits(dst_phys_addr));
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_UPPER_DST_ADDR,
				 upper_32_bits(dst_phys_addr));

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_SIZE,
				 size);

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_FLAGS, flags);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_TYPE, irq_type);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_NUMBER, 1);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_COMMAND,
				 COMMAND_COPY);

	wait_for_completion(&test->irq_raised);

	dma_unmap_single(dev, orig_dst_phys_addr, size + alignment,
			 DMA_FROM_DEVICE);

	dst_crc32 = crc32_le(~0, dst_addr, size);
	if (dst_crc32 != src_crc32)
		ret = -EIO;

err_dst_phys_addr:
	kfree(orig_dst_addr);

err_dst_addr:
	dma_unmap_single(dev, orig_src_phys_addr, size + alignment,
			 DMA_TO_DEVICE);

err_src_phys_addr:
	kfree(orig_src_addr);
	return ret;
}

static int pci_endpoint_test_write(struct pci_endpoint_test *test,
				    unsigned long arg)
{
	struct pci_endpoint_test_xfer_param param;
	u32 flags = 0;
	bool use_dma;
	u32 reg;
	void *addr;
	dma_addr_t phys_addr;
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;
	void *orig_addr;
	dma_addr_t orig_phys_addr;
	size_t offset;
	size_t alignment = test->alignment;
	int irq_type = test->irq_type;
	size_t size;
	u32 crc32;
	int ret;

	ret = copy_from_user(&param, (void __user *)arg, sizeof(param));
	if (ret) {
		dev_err(dev, "Failed to get transfer param\n");
		return -EFAULT;
	}

	ret = pci_endpoint_test_validate_xfer_params(dev, &param, alignment);
	if (ret)
		return ret;

	size = param.size;

	use_dma = !!(param.flags & PCITEST_FLAGS_USE_DMA);
	if (use_dma)
		flags |= FLAG_USE_DMA;

	if (irq_type < PCITEST_IRQ_TYPE_INTX ||
	    irq_type > PCITEST_IRQ_TYPE_MSIX) {
		dev_err(dev, "Invalid IRQ type option\n");
		return -EINVAL;
	}

	orig_addr = kzalloc(size + alignment, GFP_KERNEL);
	if (!orig_addr) {
		dev_err(dev, "Failed to allocate address\n");
		return -ENOMEM;
	}

	get_random_bytes(orig_addr, size + alignment);

	orig_phys_addr = dma_map_single(dev, orig_addr, size + alignment,
					DMA_TO_DEVICE);
	ret = dma_mapping_error(dev, orig_phys_addr);
	if (ret) {
		dev_err(dev, "failed to map source buffer address\n");
		goto err_phys_addr;
	}

	if (alignment && !IS_ALIGNED(orig_phys_addr, alignment)) {
		phys_addr =  PTR_ALIGN(orig_phys_addr, alignment);
		offset = phys_addr - orig_phys_addr;
		addr = orig_addr + offset;
	} else {
		phys_addr = orig_phys_addr;
		addr = orig_addr;
	}

	crc32 = crc32_le(~0, addr, size);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_CHECKSUM,
				 crc32);

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_LOWER_SRC_ADDR,
				 lower_32_bits(phys_addr));
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_UPPER_SRC_ADDR,
				 upper_32_bits(phys_addr));

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_SIZE, size);

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_FLAGS, flags);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_TYPE, irq_type);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_NUMBER, 1);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_COMMAND,
				 COMMAND_READ);

	wait_for_completion(&test->irq_raised);

	reg = pci_endpoint_test_readl(test, PCI_ENDPOINT_TEST_STATUS);
	if (!(reg & STATUS_READ_SUCCESS))
		ret = -EIO;

	dma_unmap_single(dev, orig_phys_addr, size + alignment,
			 DMA_TO_DEVICE);

err_phys_addr:
	kfree(orig_addr);
	return ret;
}

static int pci_endpoint_test_read(struct pci_endpoint_test *test,
				   unsigned long arg)
{
	struct pci_endpoint_test_xfer_param param;
	u32 flags = 0;
	bool use_dma;
	size_t size;
	void *addr;
	dma_addr_t phys_addr;
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;
	void *orig_addr;
	dma_addr_t orig_phys_addr;
	size_t offset;
	size_t alignment = test->alignment;
	int irq_type = test->irq_type;
	u32 crc32;
	int ret;

	ret = copy_from_user(&param, (void __user *)arg, sizeof(param));
	if (ret) {
		dev_err(dev, "Failed to get transfer param\n");
		return -EFAULT;
	}

	ret = pci_endpoint_test_validate_xfer_params(dev, &param, alignment);
	if (ret)
		return ret;

	size = param.size;

	use_dma = !!(param.flags & PCITEST_FLAGS_USE_DMA);
	if (use_dma)
		flags |= FLAG_USE_DMA;

	if (irq_type < PCITEST_IRQ_TYPE_INTX ||
	    irq_type > PCITEST_IRQ_TYPE_MSIX) {
		dev_err(dev, "Invalid IRQ type option\n");
		return -EINVAL;
	}

	orig_addr = kzalloc(size + alignment, GFP_KERNEL);
	if (!orig_addr) {
		dev_err(dev, "Failed to allocate destination address\n");
		return -ENOMEM;
	}

	orig_phys_addr = dma_map_single(dev, orig_addr, size + alignment,
					DMA_FROM_DEVICE);
	ret = dma_mapping_error(dev, orig_phys_addr);
	if (ret) {
		dev_err(dev, "failed to map source buffer address\n");
		goto err_phys_addr;
	}

	if (alignment && !IS_ALIGNED(orig_phys_addr, alignment)) {
		phys_addr = PTR_ALIGN(orig_phys_addr, alignment);
		offset = phys_addr - orig_phys_addr;
		addr = orig_addr + offset;
	} else {
		phys_addr = orig_phys_addr;
		addr = orig_addr;
	}

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_LOWER_DST_ADDR,
				 lower_32_bits(phys_addr));
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_UPPER_DST_ADDR,
				 upper_32_bits(phys_addr));

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_SIZE, size);

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_FLAGS, flags);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_TYPE, irq_type);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_NUMBER, 1);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_COMMAND,
				 COMMAND_WRITE);

	wait_for_completion(&test->irq_raised);

	dma_unmap_single(dev, orig_phys_addr, size + alignment,
			 DMA_FROM_DEVICE);

	crc32 = crc32_le(~0, addr, size);
	if (crc32 != pci_endpoint_test_readl(test, PCI_ENDPOINT_TEST_CHECKSUM))
		ret = -EIO;

err_phys_addr:
	kfree(orig_addr);
	return ret;
}

static int pci_endpoint_test_clear_irq(struct pci_endpoint_test *test)
{
	pci_endpoint_test_release_irq(test);
	pci_endpoint_test_free_irq_vectors(test);

	return 0;
}

static int pci_endpoint_test_set_irq(struct pci_endpoint_test *test,
				      int req_irq_type)
{
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;
	int ret;

	if (req_irq_type < PCITEST_IRQ_TYPE_INTX ||
	    req_irq_type > PCITEST_IRQ_TYPE_AUTO) {
		dev_err(dev, "Invalid IRQ type option\n");
		return -EINVAL;
	}

	if (req_irq_type == PCITEST_IRQ_TYPE_AUTO) {
		if (test->ep_caps & CAP_MSI)
			req_irq_type = PCITEST_IRQ_TYPE_MSI;
		else if (test->ep_caps & CAP_MSIX)
			req_irq_type = PCITEST_IRQ_TYPE_MSIX;
		else if (test->ep_caps & CAP_INTX)
			req_irq_type = PCITEST_IRQ_TYPE_INTX;
		else
			/* fallback to MSI if no caps defined */
			req_irq_type = PCITEST_IRQ_TYPE_MSI;
	}

	if (test->irq_type == req_irq_type)
		return 0;

	pci_endpoint_test_release_irq(test);
	pci_endpoint_test_free_irq_vectors(test);

	ret = pci_endpoint_test_alloc_irq_vectors(test, req_irq_type);
	if (ret)
		return ret;

	ret = pci_endpoint_test_request_irq(test);
	if (ret) {
		pci_endpoint_test_free_irq_vectors(test);
		return ret;
	}

	return 0;
}

static int pci_endpoint_test_doorbell(struct pci_endpoint_test *test)
{
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;
	int irq_type = test->irq_type;
	enum pci_barno bar;
	u32 data, status;
	u32 addr;
	int left;

	if (irq_type < PCITEST_IRQ_TYPE_INTX ||
	    irq_type > PCITEST_IRQ_TYPE_MSIX) {
		dev_err(dev, "Invalid IRQ type\n");
		return -EINVAL;
	}

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_TYPE, irq_type);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_NUMBER, 1);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_COMMAND,
				 COMMAND_ENABLE_DOORBELL);

	left = wait_for_completion_timeout(&test->irq_raised, msecs_to_jiffies(1000));

	status = pci_endpoint_test_readl(test, PCI_ENDPOINT_TEST_STATUS);
	if (!left || (status & STATUS_DOORBELL_ENABLE_FAIL)) {
		dev_err(dev, "Failed to enable doorbell\n");
		return -EINVAL;
	}

	data = pci_endpoint_test_readl(test, PCI_ENDPOINT_TEST_DB_DATA);
	addr = pci_endpoint_test_readl(test, PCI_ENDPOINT_TEST_DB_OFFSET);
	bar = pci_endpoint_test_readl(test, PCI_ENDPOINT_TEST_DB_BAR);

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_TYPE, irq_type);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_NUMBER, 1);

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_STATUS, 0);

	bar = pci_endpoint_test_readl(test, PCI_ENDPOINT_TEST_DB_BAR);

	writel(data, test->bar[bar] + addr);

	left = wait_for_completion_timeout(&test->irq_raised, msecs_to_jiffies(1000));

	status = pci_endpoint_test_readl(test, PCI_ENDPOINT_TEST_STATUS);

	if (!left || !(status & STATUS_DOORBELL_SUCCESS))
		dev_err(dev, "Failed to trigger doorbell in endpoint\n");

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_COMMAND,
				 COMMAND_DISABLE_DOORBELL);

	wait_for_completion_timeout(&test->irq_raised, msecs_to_jiffies(1000));

	status |= pci_endpoint_test_readl(test, PCI_ENDPOINT_TEST_STATUS);

	if (status & STATUS_DOORBELL_DISABLE_FAIL) {
		dev_err(dev, "Failed to disable doorbell\n");
		return -EINVAL;
	}

	if (!(status & STATUS_DOORBELL_SUCCESS))
		return -EINVAL;

	return 0;
}

static long pci_endpoint_test_ioctl(struct file *file, unsigned int cmd,
				    unsigned long arg)
{
	int ret = -EINVAL;
	enum pci_barno bar;
	struct pci_endpoint_test *test = to_endpoint_test(file->private_data);
	struct pci_dev *pdev = test->pdev;

	mutex_lock(&test->mutex);

	reinit_completion(&test->irq_raised);
	test->last_irq = -ENODATA;

	switch (cmd) {
	case PCITEST_BAR:
		bar = arg;
		if (bar <= NO_BAR || bar > BAR_5)
			goto ret;
		if (is_am654_pci_dev(pdev) && bar == BAR_0)
			goto ret;
		ret = pci_endpoint_test_bar(test, bar);
		break;
	case PCITEST_BARS:
		ret = pci_endpoint_test_bars(test);
		break;
	case PCITEST_INTX_IRQ:
		ret = pci_endpoint_test_intx_irq(test);
		break;
	case PCITEST_MSI:
	case PCITEST_MSIX:
		ret = pci_endpoint_test_msi_irq(test, arg, cmd == PCITEST_MSIX);
		break;
	case PCITEST_WRITE:
		ret = pci_endpoint_test_write(test, arg);
		break;
	case PCITEST_READ:
		ret = pci_endpoint_test_read(test, arg);
		break;
	case PCITEST_COPY:
		ret = pci_endpoint_test_copy(test, arg);
		break;
	case PCITEST_SET_IRQTYPE:
		ret = pci_endpoint_test_set_irq(test, arg);
		break;
	case PCITEST_GET_IRQTYPE:
		ret = test->irq_type;
		break;
	case PCITEST_CLEAR_IRQ:
		ret = pci_endpoint_test_clear_irq(test);
		break;
	case PCITEST_DOORBELL:
		ret = pci_endpoint_test_doorbell(test);
		break;
	}

ret:
	mutex_unlock(&test->mutex);
	return ret;
}

static const struct file_operations pci_endpoint_test_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = pci_endpoint_test_ioctl,
};

static void pci_endpoint_test_get_capabilities(struct pci_endpoint_test *test)
{
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;

	test->ep_caps = pci_endpoint_test_readl(test, PCI_ENDPOINT_TEST_CAPS);
	dev_dbg(dev, "PCI_ENDPOINT_TEST_CAPS: %#x\n", test->ep_caps);

	/* CAP_UNALIGNED_ACCESS is set if the EP can do unaligned access */
	if (test->ep_caps & CAP_UNALIGNED_ACCESS)
		test->alignment = 0;
}

static int pci_endpoint_test_probe(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	int ret;
	int id;
	char name[29];
	enum pci_barno bar;
	void __iomem *base;
	struct device *dev = &pdev->dev;
	struct pci_endpoint_test *test;
	struct pci_endpoint_test_data *data;
	enum pci_barno test_reg_bar = BAR_0;
	struct miscdevice *misc_device;

	if (pci_is_bridge(pdev))
		return -ENODEV;

	test = devm_kzalloc(dev, sizeof(*test), GFP_KERNEL);
	if (!test)
		return -ENOMEM;

	test->pdev = pdev;
	test->irq_type = PCITEST_IRQ_TYPE_UNDEFINED;

	data = (struct pci_endpoint_test_data *)ent->driver_data;
	if (data) {
		test_reg_bar = data->test_reg_bar;
		test->test_reg_bar = test_reg_bar;
		test->alignment = data->alignment;
	}

	init_completion(&test->irq_raised);
	mutex_init(&test->mutex);

	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(48));

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(dev, "Cannot enable PCI device\n");
		return ret;
	}

	ret = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (ret) {
		dev_err(dev, "Cannot obtain PCI resources\n");
		goto err_disable_pdev;
	}

	pci_set_master(pdev);

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (pci_resource_flags(pdev, bar) & IORESOURCE_MEM) {
			base = pci_ioremap_bar(pdev, bar);
			if (!base) {
				dev_err(dev, "Failed to read BAR%d\n", bar);
				WARN_ON(bar == test_reg_bar);
			}
			test->bar[bar] = base;
		}
	}

	test->base = test->bar[test_reg_bar];
	if (!test->base) {
		ret = -ENOMEM;
		dev_err(dev, "Cannot perform PCI test without BAR%d\n",
			test_reg_bar);
		goto err_iounmap;
	}

	pci_set_drvdata(pdev, test);

	id = ida_alloc(&pci_endpoint_test_ida, GFP_KERNEL);
	if (id < 0) {
		ret = id;
		dev_err(dev, "Unable to get id\n");
		goto err_iounmap;
	}

	snprintf(name, sizeof(name), DRV_MODULE_NAME ".%d", id);
	test->name = kstrdup(name, GFP_KERNEL);
	if (!test->name) {
		ret = -ENOMEM;
		goto err_ida_remove;
	}

	pci_endpoint_test_get_capabilities(test);

	misc_device = &test->miscdev;
	misc_device->minor = MISC_DYNAMIC_MINOR;
	misc_device->name = kstrdup(name, GFP_KERNEL);
	if (!misc_device->name) {
		ret = -ENOMEM;
		goto err_kfree_test_name;
	}
	misc_device->parent = &pdev->dev;
	misc_device->fops = &pci_endpoint_test_fops;

	ret = misc_register(misc_device);
	if (ret) {
		dev_err(dev, "Failed to register device\n");
		goto err_kfree_name;
	}

	return 0;

err_kfree_name:
	kfree(misc_device->name);

err_kfree_test_name:
	kfree(test->name);

err_ida_remove:
	ida_free(&pci_endpoint_test_ida, id);

err_iounmap:
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (test->bar[bar])
			pci_iounmap(pdev, test->bar[bar]);
	}

	pci_release_regions(pdev);

err_disable_pdev:
	pci_disable_device(pdev);

	return ret;
}

static void pci_endpoint_test_remove(struct pci_dev *pdev)
{
	int id;
	enum pci_barno bar;
	struct pci_endpoint_test *test = pci_get_drvdata(pdev);
	struct miscdevice *misc_device = &test->miscdev;

	if (sscanf(misc_device->name, DRV_MODULE_NAME ".%d", &id) != 1)
		return;
	if (id < 0)
		return;

	pci_endpoint_test_release_irq(test);
	pci_endpoint_test_free_irq_vectors(test);

	misc_deregister(&test->miscdev);
	kfree(misc_device->name);
	kfree(test->name);
	ida_free(&pci_endpoint_test_ida, id);
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (test->bar[bar])
			pci_iounmap(pdev, test->bar[bar]);
	}

	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static const struct pci_endpoint_test_data default_data = {
	.test_reg_bar = BAR_0,
	.alignment = SZ_4K,
};

static const struct pci_endpoint_test_data am654_data = {
	.test_reg_bar = BAR_2,
	.alignment = SZ_64K,
};

static const struct pci_endpoint_test_data j721e_data = {
	.alignment = 256,
};

static const struct pci_endpoint_test_data rk3588_data = {
	.alignment = SZ_64K,
};

/*
 * If the controller's Vendor/Device ID are programmable, you may be able to
 * use one of the existing entries for testing instead of adding a new one.
 */
static const struct pci_device_id pci_endpoint_test_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_DRA74x),
	  .driver_data = (kernel_ulong_t)&default_data,
	},
	{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_DRA72x),
	  .driver_data = (kernel_ulong_t)&default_data,
	},
	{ PCI_DEVICE(PCI_VENDOR_ID_FREESCALE, 0x81c0),
	  .driver_data = (kernel_ulong_t)&default_data,
	},
	{ PCI_DEVICE(PCI_VENDOR_ID_FREESCALE, PCI_DEVICE_ID_IMX8),},
	{ PCI_DEVICE(PCI_VENDOR_ID_FREESCALE, PCI_DEVICE_ID_LS1088A),
	  .driver_data = (kernel_ulong_t)&default_data,
	},
	{ PCI_DEVICE_DATA(SYNOPSYS, EDDA, NULL) },
	{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_AM654),
	  .driver_data = (kernel_ulong_t)&am654_data
	},
	{ PCI_DEVICE(PCI_VENDOR_ID_RENESAS, PCI_DEVICE_ID_RENESAS_R8A774A1),},
	{ PCI_DEVICE(PCI_VENDOR_ID_RENESAS, PCI_DEVICE_ID_RENESAS_R8A774B1),},
	{ PCI_DEVICE(PCI_VENDOR_ID_RENESAS, PCI_DEVICE_ID_RENESAS_R8A774C0),},
	{ PCI_DEVICE(PCI_VENDOR_ID_RENESAS, PCI_DEVICE_ID_RENESAS_R8A774E1),},
	{ PCI_DEVICE(PCI_VENDOR_ID_RENESAS, PCI_DEVICE_ID_RENESAS_R8A779F0),
	  .driver_data = (kernel_ulong_t)&default_data,
	},
	{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_J721E),
	  .driver_data = (kernel_ulong_t)&j721e_data,
	},
	{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_J7200),
	  .driver_data = (kernel_ulong_t)&j721e_data,
	},
	{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_AM64),
	  .driver_data = (kernel_ulong_t)&j721e_data,
	},
	{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_J721S2),
	  .driver_data = (kernel_ulong_t)&j721e_data,
	},
	{ PCI_DEVICE(PCI_VENDOR_ID_ROCKCHIP, PCI_DEVICE_ID_ROCKCHIP_RK3588),
	  .driver_data = (kernel_ulong_t)&rk3588_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(pci, pci_endpoint_test_tbl);

static struct pci_driver pci_endpoint_test_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= pci_endpoint_test_tbl,
	.probe		= pci_endpoint_test_probe,
	.remove		= pci_endpoint_test_remove,
	.sriov_configure = pci_sriov_configure_simple,
};
module_pci_driver(pci_endpoint_test_driver);

MODULE_DESCRIPTION("PCI ENDPOINT TEST HOST DRIVER");
MODULE_AUTHOR("Kishon Vijay Abraham I <kishon@ti.com>");
MODULE_LICENSE("GPL v2");
