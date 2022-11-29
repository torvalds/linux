// SPDX-License-Identifier: GPL-2.0-only
/**
 * Host side test driver to test endpoint functionality
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/crc32.h>
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

#define IRQ_TYPE_UNDEFINED			-1
#define IRQ_TYPE_LEGACY				0
#define IRQ_TYPE_MSI				1
#define IRQ_TYPE_MSIX				2

#define PCI_ENDPOINT_TEST_MAGIC			0x0

#define PCI_ENDPOINT_TEST_COMMAND		0x4
#define COMMAND_RAISE_LEGACY_IRQ		BIT(0)
#define COMMAND_RAISE_MSI_IRQ			BIT(1)
#define COMMAND_RAISE_MSIX_IRQ			BIT(2)
#define COMMAND_READ				BIT(3)
#define COMMAND_WRITE				BIT(4)
#define COMMAND_COPY				BIT(5)

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

#define PCI_DEVICE_ID_TI_J721E			0xb00d
#define PCI_DEVICE_ID_TI_AM654			0xb00c
#define PCI_DEVICE_ID_LS1088A			0x80c0

#define is_am654_pci_dev(pdev)		\
		((pdev)->device == PCI_DEVICE_ID_TI_AM654)

#define PCI_DEVICE_ID_RENESAS_R8A774A1		0x0028
#define PCI_DEVICE_ID_RENESAS_R8A774B1		0x002b
#define PCI_DEVICE_ID_RENESAS_R8A774C0		0x002d
#define PCI_DEVICE_ID_RENESAS_R8A774E1		0x0025

static DEFINE_IDA(pci_endpoint_test_ida);

#define to_endpoint_test(priv) container_of((priv), struct pci_endpoint_test, \
					    miscdev)

static bool no_msi;
module_param(no_msi, bool, 0444);
MODULE_PARM_DESC(no_msi, "Disable MSI interrupt in pci_endpoint_test");

static int irq_type = IRQ_TYPE_MSI;
module_param(irq_type, int, 0444);
MODULE_PARM_DESC(irq_type, "IRQ mode selection in pci_endpoint_test (0 - Legacy, 1 - MSI, 2 - MSI-X)");

enum pci_barno {
	BAR_0,
	BAR_1,
	BAR_2,
	BAR_3,
	BAR_4,
	BAR_5,
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
	const char *name;
};

struct pci_endpoint_test_data {
	enum pci_barno test_reg_bar;
	size_t alignment;
	int irq_type;
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

static inline u32 pci_endpoint_test_bar_readl(struct pci_endpoint_test *test,
					      int bar, int offset)
{
	return readl(test->bar[bar] + offset);
}

static inline void pci_endpoint_test_bar_writel(struct pci_endpoint_test *test,
						int bar, u32 offset, u32 value)
{
	writel(value, test->bar[bar] + offset);
}

static irqreturn_t pci_endpoint_test_irqhandler(int irq, void *dev_id)
{
	struct pci_endpoint_test *test = dev_id;
	u32 reg;

	reg = pci_endpoint_test_readl(test, PCI_ENDPOINT_TEST_STATUS);
	if (reg & STATUS_IRQ_RAISED) {
		test->last_irq = irq;
		complete(&test->irq_raised);
		reg &= ~STATUS_IRQ_RAISED;
	}
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_STATUS,
				 reg);

	return IRQ_HANDLED;
}

static void pci_endpoint_test_free_irq_vectors(struct pci_endpoint_test *test)
{
	struct pci_dev *pdev = test->pdev;

	pci_free_irq_vectors(pdev);
	test->irq_type = IRQ_TYPE_UNDEFINED;
}

static bool pci_endpoint_test_alloc_irq_vectors(struct pci_endpoint_test *test,
						int type)
{
	int irq = -1;
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;
	bool res = true;

	switch (type) {
	case IRQ_TYPE_LEGACY:
		irq = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_LEGACY);
		if (irq < 0)
			dev_err(dev, "Failed to get Legacy interrupt\n");
		break;
	case IRQ_TYPE_MSI:
		irq = pci_alloc_irq_vectors(pdev, 1, 32, PCI_IRQ_MSI);
		if (irq < 0)
			dev_err(dev, "Failed to get MSI interrupts\n");
		break;
	case IRQ_TYPE_MSIX:
		irq = pci_alloc_irq_vectors(pdev, 1, 2048, PCI_IRQ_MSIX);
		if (irq < 0)
			dev_err(dev, "Failed to get MSI-X interrupts\n");
		break;
	default:
		dev_err(dev, "Invalid IRQ type selected\n");
	}

	if (irq < 0) {
		irq = 0;
		res = false;
	}

	test->irq_type = type;
	test->num_irqs = irq;

	return res;
}

static void pci_endpoint_test_release_irq(struct pci_endpoint_test *test)
{
	int i;
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;

	for (i = 0; i < test->num_irqs; i++)
		devm_free_irq(dev, pci_irq_vector(pdev, i), test);

	test->num_irqs = 0;
}

static bool pci_endpoint_test_request_irq(struct pci_endpoint_test *test)
{
	int i;
	int err;
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;

	for (i = 0; i < test->num_irqs; i++) {
		err = devm_request_irq(dev, pci_irq_vector(pdev, i),
				       pci_endpoint_test_irqhandler,
				       IRQF_SHARED, test->name, test);
		if (err)
			goto fail;
	}

	return true;

fail:
	switch (irq_type) {
	case IRQ_TYPE_LEGACY:
		dev_err(dev, "Failed to request IRQ %d for Legacy\n",
			pci_irq_vector(pdev, i));
		break;
	case IRQ_TYPE_MSI:
		dev_err(dev, "Failed to request IRQ %d for MSI %d\n",
			pci_irq_vector(pdev, i),
			i + 1);
		break;
	case IRQ_TYPE_MSIX:
		dev_err(dev, "Failed to request IRQ %d for MSI-X %d\n",
			pci_irq_vector(pdev, i),
			i + 1);
		break;
	}

	return false;
}

static bool pci_endpoint_test_bar(struct pci_endpoint_test *test,
				  enum pci_barno barno)
{
	int j;
	u32 val;
	int size;
	struct pci_dev *pdev = test->pdev;

	if (!test->bar[barno])
		return false;

	size = pci_resource_len(pdev, barno);

	if (barno == test->test_reg_bar)
		size = 0x4;

	for (j = 0; j < size; j += 4)
		pci_endpoint_test_bar_writel(test, barno, j, 0xA0A0A0A0);

	for (j = 0; j < size; j += 4) {
		val = pci_endpoint_test_bar_readl(test, barno, j);
		if (val != 0xA0A0A0A0)
			return false;
	}

	return true;
}

static bool pci_endpoint_test_legacy_irq(struct pci_endpoint_test *test)
{
	u32 val;

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_TYPE,
				 IRQ_TYPE_LEGACY);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_NUMBER, 0);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_COMMAND,
				 COMMAND_RAISE_LEGACY_IRQ);
	val = wait_for_completion_timeout(&test->irq_raised,
					  msecs_to_jiffies(1000));
	if (!val)
		return false;

	return true;
}

static bool pci_endpoint_test_msi_irq(struct pci_endpoint_test *test,
				       u16 msi_num, bool msix)
{
	u32 val;
	struct pci_dev *pdev = test->pdev;

	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_TYPE,
				 msix == false ? IRQ_TYPE_MSI :
				 IRQ_TYPE_MSIX);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_IRQ_NUMBER, msi_num);
	pci_endpoint_test_writel(test, PCI_ENDPOINT_TEST_COMMAND,
				 msix == false ? COMMAND_RAISE_MSI_IRQ :
				 COMMAND_RAISE_MSIX_IRQ);
	val = wait_for_completion_timeout(&test->irq_raised,
					  msecs_to_jiffies(1000));
	if (!val)
		return false;

	if (pci_irq_vector(pdev, msi_num - 1) == test->last_irq)
		return true;

	return false;
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

static bool pci_endpoint_test_copy(struct pci_endpoint_test *test,
				   unsigned long arg)
{
	struct pci_endpoint_test_xfer_param param;
	bool ret = false;
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
	int err;

	err = copy_from_user(&param, (void __user *)arg, sizeof(param));
	if (err) {
		dev_err(dev, "Failed to get transfer param\n");
		return false;
	}

	err = pci_endpoint_test_validate_xfer_params(dev, &param, alignment);
	if (err)
		return false;

	size = param.size;

	use_dma = !!(param.flags & PCITEST_FLAGS_USE_DMA);
	if (use_dma)
		flags |= FLAG_USE_DMA;

	if (irq_type < IRQ_TYPE_LEGACY || irq_type > IRQ_TYPE_MSIX) {
		dev_err(dev, "Invalid IRQ type option\n");
		goto err;
	}

	orig_src_addr = kzalloc(size + alignment, GFP_KERNEL);
	if (!orig_src_addr) {
		dev_err(dev, "Failed to allocate source buffer\n");
		ret = false;
		goto err;
	}

	get_random_bytes(orig_src_addr, size + alignment);
	orig_src_phys_addr = dma_map_single(dev, orig_src_addr,
					    size + alignment, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, orig_src_phys_addr)) {
		dev_err(dev, "failed to map source buffer address\n");
		ret = false;
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
		ret = false;
		goto err_dst_addr;
	}

	orig_dst_phys_addr = dma_map_single(dev, orig_dst_addr,
					    size + alignment, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, orig_dst_phys_addr)) {
		dev_err(dev, "failed to map destination buffer address\n");
		ret = false;
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
	if (dst_crc32 == src_crc32)
		ret = true;

err_dst_phys_addr:
	kfree(orig_dst_addr);

err_dst_addr:
	dma_unmap_single(dev, orig_src_phys_addr, size + alignment,
			 DMA_TO_DEVICE);

err_src_phys_addr:
	kfree(orig_src_addr);

err:
	return ret;
}

static bool pci_endpoint_test_write(struct pci_endpoint_test *test,
				    unsigned long arg)
{
	struct pci_endpoint_test_xfer_param param;
	bool ret = false;
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
	int err;

	err = copy_from_user(&param, (void __user *)arg, sizeof(param));
	if (err != 0) {
		dev_err(dev, "Failed to get transfer param\n");
		return false;
	}

	err = pci_endpoint_test_validate_xfer_params(dev, &param, alignment);
	if (err)
		return false;

	size = param.size;

	use_dma = !!(param.flags & PCITEST_FLAGS_USE_DMA);
	if (use_dma)
		flags |= FLAG_USE_DMA;

	if (irq_type < IRQ_TYPE_LEGACY || irq_type > IRQ_TYPE_MSIX) {
		dev_err(dev, "Invalid IRQ type option\n");
		goto err;
	}

	orig_addr = kzalloc(size + alignment, GFP_KERNEL);
	if (!orig_addr) {
		dev_err(dev, "Failed to allocate address\n");
		ret = false;
		goto err;
	}

	get_random_bytes(orig_addr, size + alignment);

	orig_phys_addr = dma_map_single(dev, orig_addr, size + alignment,
					DMA_TO_DEVICE);
	if (dma_mapping_error(dev, orig_phys_addr)) {
		dev_err(dev, "failed to map source buffer address\n");
		ret = false;
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
	if (reg & STATUS_READ_SUCCESS)
		ret = true;

	dma_unmap_single(dev, orig_phys_addr, size + alignment,
			 DMA_TO_DEVICE);

err_phys_addr:
	kfree(orig_addr);

err:
	return ret;
}

static bool pci_endpoint_test_read(struct pci_endpoint_test *test,
				   unsigned long arg)
{
	struct pci_endpoint_test_xfer_param param;
	bool ret = false;
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
	int err;

	err = copy_from_user(&param, (void __user *)arg, sizeof(param));
	if (err) {
		dev_err(dev, "Failed to get transfer param\n");
		return false;
	}

	err = pci_endpoint_test_validate_xfer_params(dev, &param, alignment);
	if (err)
		return false;

	size = param.size;

	use_dma = !!(param.flags & PCITEST_FLAGS_USE_DMA);
	if (use_dma)
		flags |= FLAG_USE_DMA;

	if (irq_type < IRQ_TYPE_LEGACY || irq_type > IRQ_TYPE_MSIX) {
		dev_err(dev, "Invalid IRQ type option\n");
		goto err;
	}

	orig_addr = kzalloc(size + alignment, GFP_KERNEL);
	if (!orig_addr) {
		dev_err(dev, "Failed to allocate destination address\n");
		ret = false;
		goto err;
	}

	orig_phys_addr = dma_map_single(dev, orig_addr, size + alignment,
					DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, orig_phys_addr)) {
		dev_err(dev, "failed to map source buffer address\n");
		ret = false;
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
	if (crc32 == pci_endpoint_test_readl(test, PCI_ENDPOINT_TEST_CHECKSUM))
		ret = true;

err_phys_addr:
	kfree(orig_addr);
err:
	return ret;
}

static bool pci_endpoint_test_clear_irq(struct pci_endpoint_test *test)
{
	pci_endpoint_test_release_irq(test);
	pci_endpoint_test_free_irq_vectors(test);
	return true;
}

static bool pci_endpoint_test_set_irq(struct pci_endpoint_test *test,
				      int req_irq_type)
{
	struct pci_dev *pdev = test->pdev;
	struct device *dev = &pdev->dev;

	if (req_irq_type < IRQ_TYPE_LEGACY || req_irq_type > IRQ_TYPE_MSIX) {
		dev_err(dev, "Invalid IRQ type option\n");
		return false;
	}

	if (test->irq_type == req_irq_type)
		return true;

	pci_endpoint_test_release_irq(test);
	pci_endpoint_test_free_irq_vectors(test);

	if (!pci_endpoint_test_alloc_irq_vectors(test, req_irq_type))
		goto err;

	if (!pci_endpoint_test_request_irq(test))
		goto err;

	return true;

err:
	pci_endpoint_test_free_irq_vectors(test);
	return false;
}

static long pci_endpoint_test_ioctl(struct file *file, unsigned int cmd,
				    unsigned long arg)
{
	int ret = -EINVAL;
	enum pci_barno bar;
	struct pci_endpoint_test *test = to_endpoint_test(file->private_data);
	struct pci_dev *pdev = test->pdev;

	mutex_lock(&test->mutex);
	switch (cmd) {
	case PCITEST_BAR:
		bar = arg;
		if (bar < 0 || bar > 5)
			goto ret;
		if (is_am654_pci_dev(pdev) && bar == BAR_0)
			goto ret;
		ret = pci_endpoint_test_bar(test, bar);
		break;
	case PCITEST_LEGACY_IRQ:
		ret = pci_endpoint_test_legacy_irq(test);
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
		ret = irq_type;
		break;
	case PCITEST_CLEAR_IRQ:
		ret = pci_endpoint_test_clear_irq(test);
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

static int pci_endpoint_test_probe(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	int err;
	int id;
	char name[24];
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

	test->test_reg_bar = 0;
	test->alignment = 0;
	test->pdev = pdev;
	test->irq_type = IRQ_TYPE_UNDEFINED;

	if (no_msi)
		irq_type = IRQ_TYPE_LEGACY;

	data = (struct pci_endpoint_test_data *)ent->driver_data;
	if (data) {
		test_reg_bar = data->test_reg_bar;
		test->test_reg_bar = test_reg_bar;
		test->alignment = data->alignment;
		irq_type = data->irq_type;
	}

	init_completion(&test->irq_raised);
	mutex_init(&test->mutex);

	if ((dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(48)) != 0) &&
	    dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32)) != 0) {
		dev_err(dev, "Cannot set DMA mask\n");
		return -EINVAL;
	}

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Cannot enable PCI device\n");
		return err;
	}

	err = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (err) {
		dev_err(dev, "Cannot obtain PCI resources\n");
		goto err_disable_pdev;
	}

	pci_set_master(pdev);

	if (!pci_endpoint_test_alloc_irq_vectors(test, irq_type)) {
		err = -EINVAL;
		goto err_disable_irq;
	}

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
		err = -ENOMEM;
		dev_err(dev, "Cannot perform PCI test without BAR%d\n",
			test_reg_bar);
		goto err_iounmap;
	}

	pci_set_drvdata(pdev, test);

	id = ida_simple_get(&pci_endpoint_test_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		err = id;
		dev_err(dev, "Unable to get id\n");
		goto err_iounmap;
	}

	snprintf(name, sizeof(name), DRV_MODULE_NAME ".%d", id);
	test->name = kstrdup(name, GFP_KERNEL);
	if (!test->name) {
		err = -ENOMEM;
		goto err_ida_remove;
	}

	if (!pci_endpoint_test_request_irq(test)) {
		err = -EINVAL;
		goto err_kfree_test_name;
	}

	misc_device = &test->miscdev;
	misc_device->minor = MISC_DYNAMIC_MINOR;
	misc_device->name = kstrdup(name, GFP_KERNEL);
	if (!misc_device->name) {
		err = -ENOMEM;
		goto err_release_irq;
	}
	misc_device->fops = &pci_endpoint_test_fops,

	err = misc_register(misc_device);
	if (err) {
		dev_err(dev, "Failed to register device\n");
		goto err_kfree_name;
	}

	return 0;

err_kfree_name:
	kfree(misc_device->name);

err_release_irq:
	pci_endpoint_test_release_irq(test);

err_kfree_test_name:
	kfree(test->name);

err_ida_remove:
	ida_simple_remove(&pci_endpoint_test_ida, id);

err_iounmap:
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (test->bar[bar])
			pci_iounmap(pdev, test->bar[bar]);
	}

err_disable_irq:
	pci_endpoint_test_free_irq_vectors(test);
	pci_release_regions(pdev);

err_disable_pdev:
	pci_disable_device(pdev);

	return err;
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

	misc_deregister(&test->miscdev);
	kfree(misc_device->name);
	kfree(test->name);
	ida_simple_remove(&pci_endpoint_test_ida, id);
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (test->bar[bar])
			pci_iounmap(pdev, test->bar[bar]);
	}

	pci_endpoint_test_release_irq(test);
	pci_endpoint_test_free_irq_vectors(test);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static const struct pci_endpoint_test_data default_data = {
	.test_reg_bar = BAR_0,
	.alignment = SZ_4K,
	.irq_type = IRQ_TYPE_MSI,
};

static const struct pci_endpoint_test_data am654_data = {
	.test_reg_bar = BAR_2,
	.alignment = SZ_64K,
	.irq_type = IRQ_TYPE_MSI,
};

static const struct pci_endpoint_test_data j721e_data = {
	.alignment = 256,
	.irq_type = IRQ_TYPE_MSI,
};

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
	{ PCI_DEVICE(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_J721E),
	  .driver_data = (kernel_ulong_t)&j721e_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(pci, pci_endpoint_test_tbl);

static struct pci_driver pci_endpoint_test_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= pci_endpoint_test_tbl,
	.probe		= pci_endpoint_test_probe,
	.remove		= pci_endpoint_test_remove,
};
module_pci_driver(pci_endpoint_test_driver);

MODULE_DESCRIPTION("PCI ENDPOINT TEST HOST DRIVER");
MODULE_AUTHOR("Kishon Vijay Abraham I <kishon@ti.com>");
MODULE_LICENSE("GPL v2");
