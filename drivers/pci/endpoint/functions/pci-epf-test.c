/**
 * Test driver to test endpoint functionality
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci_ids.h>
#include <linux/random.h>

#include <linux/pci-epc.h>
#include <linux/pci-epf.h>
#include <linux/pci_regs.h>

#define COMMAND_RAISE_LEGACY_IRQ	BIT(0)
#define COMMAND_RAISE_MSI_IRQ		BIT(1)
#define MSI_NUMBER_SHIFT		2
#define MSI_NUMBER_MASK			(0x3f << MSI_NUMBER_SHIFT)
#define COMMAND_READ			BIT(8)
#define COMMAND_WRITE			BIT(9)
#define COMMAND_COPY			BIT(10)

#define STATUS_READ_SUCCESS		BIT(0)
#define STATUS_READ_FAIL		BIT(1)
#define STATUS_WRITE_SUCCESS		BIT(2)
#define STATUS_WRITE_FAIL		BIT(3)
#define STATUS_COPY_SUCCESS		BIT(4)
#define STATUS_COPY_FAIL		BIT(5)
#define STATUS_IRQ_RAISED		BIT(6)
#define STATUS_SRC_ADDR_INVALID		BIT(7)
#define STATUS_DST_ADDR_INVALID		BIT(8)

#define TIMER_RESOLUTION		1

static struct workqueue_struct *kpcitest_workqueue;

struct pci_epf_test {
	void			*reg[6];
	struct pci_epf		*epf;
	struct delayed_work	cmd_handler;
};

struct pci_epf_test_reg {
	u32	magic;
	u32	command;
	u32	status;
	u64	src_addr;
	u64	dst_addr;
	u32	size;
	u32	checksum;
} __packed;

static struct pci_epf_header test_header = {
	.vendorid	= PCI_ANY_ID,
	.deviceid	= PCI_ANY_ID,
	.baseclass_code = PCI_CLASS_OTHERS,
	.interrupt_pin	= PCI_INTERRUPT_INTA,
};

static int bar_size[] = { 512, 1024, 16384, 131072, 1048576 };

static int pci_epf_test_copy(struct pci_epf_test *epf_test)
{
	int ret;
	void __iomem *src_addr;
	void __iomem *dst_addr;
	phys_addr_t src_phys_addr;
	phys_addr_t dst_phys_addr;
	struct pci_epf *epf = epf_test->epf;
	struct device *dev = &epf->dev;
	struct pci_epc *epc = epf->epc;
	struct pci_epf_test_reg *reg = epf_test->reg[0];

	src_addr = pci_epc_mem_alloc_addr(epc, &src_phys_addr, reg->size);
	if (!src_addr) {
		dev_err(dev, "failed to allocate source address\n");
		reg->status = STATUS_SRC_ADDR_INVALID;
		ret = -ENOMEM;
		goto err;
	}

	ret = pci_epc_map_addr(epc, src_phys_addr, reg->src_addr, reg->size);
	if (ret) {
		dev_err(dev, "failed to map source address\n");
		reg->status = STATUS_SRC_ADDR_INVALID;
		goto err_src_addr;
	}

	dst_addr = pci_epc_mem_alloc_addr(epc, &dst_phys_addr, reg->size);
	if (!dst_addr) {
		dev_err(dev, "failed to allocate destination address\n");
		reg->status = STATUS_DST_ADDR_INVALID;
		ret = -ENOMEM;
		goto err_src_map_addr;
	}

	ret = pci_epc_map_addr(epc, dst_phys_addr, reg->dst_addr, reg->size);
	if (ret) {
		dev_err(dev, "failed to map destination address\n");
		reg->status = STATUS_DST_ADDR_INVALID;
		goto err_dst_addr;
	}

	memcpy(dst_addr, src_addr, reg->size);

	pci_epc_unmap_addr(epc, dst_phys_addr);

err_dst_addr:
	pci_epc_mem_free_addr(epc, dst_phys_addr, dst_addr, reg->size);

err_src_map_addr:
	pci_epc_unmap_addr(epc, src_phys_addr);

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
	phys_addr_t phys_addr;
	struct pci_epf *epf = epf_test->epf;
	struct device *dev = &epf->dev;
	struct pci_epc *epc = epf->epc;
	struct pci_epf_test_reg *reg = epf_test->reg[0];

	src_addr = pci_epc_mem_alloc_addr(epc, &phys_addr, reg->size);
	if (!src_addr) {
		dev_err(dev, "failed to allocate address\n");
		reg->status = STATUS_SRC_ADDR_INVALID;
		ret = -ENOMEM;
		goto err;
	}

	ret = pci_epc_map_addr(epc, phys_addr, reg->src_addr, reg->size);
	if (ret) {
		dev_err(dev, "failed to map address\n");
		reg->status = STATUS_SRC_ADDR_INVALID;
		goto err_addr;
	}

	buf = kzalloc(reg->size, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto err_map_addr;
	}

	memcpy(buf, src_addr, reg->size);

	crc32 = crc32_le(~0, buf, reg->size);
	if (crc32 != reg->checksum)
		ret = -EIO;

	kfree(buf);

err_map_addr:
	pci_epc_unmap_addr(epc, phys_addr);

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
	phys_addr_t phys_addr;
	struct pci_epf *epf = epf_test->epf;
	struct device *dev = &epf->dev;
	struct pci_epc *epc = epf->epc;
	struct pci_epf_test_reg *reg = epf_test->reg[0];

	dst_addr = pci_epc_mem_alloc_addr(epc, &phys_addr, reg->size);
	if (!dst_addr) {
		dev_err(dev, "failed to allocate address\n");
		reg->status = STATUS_DST_ADDR_INVALID;
		ret = -ENOMEM;
		goto err;
	}

	ret = pci_epc_map_addr(epc, phys_addr, reg->dst_addr, reg->size);
	if (ret) {
		dev_err(dev, "failed to map address\n");
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

	memcpy(dst_addr, buf, reg->size);

	/*
	 * wait 1ms inorder for the write to complete. Without this delay L3
	 * error in observed in the host system.
	 */
	mdelay(1);

	kfree(buf);

err_map_addr:
	pci_epc_unmap_addr(epc, phys_addr);

err_addr:
	pci_epc_mem_free_addr(epc, phys_addr, dst_addr, reg->size);

err:
	return ret;
}

static void pci_epf_test_raise_irq(struct pci_epf_test *epf_test)
{
	u8 irq;
	u8 msi_count;
	struct pci_epf *epf = epf_test->epf;
	struct pci_epc *epc = epf->epc;
	struct pci_epf_test_reg *reg = epf_test->reg[0];

	reg->status |= STATUS_IRQ_RAISED;
	msi_count = pci_epc_get_msi(epc);
	irq = (reg->command & MSI_NUMBER_MASK) >> MSI_NUMBER_SHIFT;
	if (irq > msi_count || msi_count <= 0)
		pci_epc_raise_irq(epc, PCI_EPC_IRQ_LEGACY, 0);
	else
		pci_epc_raise_irq(epc, PCI_EPC_IRQ_MSI, irq);
}

static void pci_epf_test_cmd_handler(struct work_struct *work)
{
	int ret;
	u8 irq;
	u8 msi_count;
	struct pci_epf_test *epf_test = container_of(work, struct pci_epf_test,
						     cmd_handler.work);
	struct pci_epf *epf = epf_test->epf;
	struct pci_epc *epc = epf->epc;
	struct pci_epf_test_reg *reg = epf_test->reg[0];

	if (!reg->command)
		goto reset_handler;

	if (reg->command & COMMAND_RAISE_LEGACY_IRQ) {
		reg->status = STATUS_IRQ_RAISED;
		pci_epc_raise_irq(epc, PCI_EPC_IRQ_LEGACY, 0);
		goto reset_handler;
	}

	if (reg->command & COMMAND_WRITE) {
		ret = pci_epf_test_write(epf_test);
		if (ret)
			reg->status |= STATUS_WRITE_FAIL;
		else
			reg->status |= STATUS_WRITE_SUCCESS;
		pci_epf_test_raise_irq(epf_test);
		goto reset_handler;
	}

	if (reg->command & COMMAND_READ) {
		ret = pci_epf_test_read(epf_test);
		if (!ret)
			reg->status |= STATUS_READ_SUCCESS;
		else
			reg->status |= STATUS_READ_FAIL;
		pci_epf_test_raise_irq(epf_test);
		goto reset_handler;
	}

	if (reg->command & COMMAND_COPY) {
		ret = pci_epf_test_copy(epf_test);
		if (!ret)
			reg->status |= STATUS_COPY_SUCCESS;
		else
			reg->status |= STATUS_COPY_FAIL;
		pci_epf_test_raise_irq(epf_test);
		goto reset_handler;
	}

	if (reg->command & COMMAND_RAISE_MSI_IRQ) {
		msi_count = pci_epc_get_msi(epc);
		irq = (reg->command & MSI_NUMBER_MASK) >> MSI_NUMBER_SHIFT;
		if (irq > msi_count || msi_count <= 0)
			goto reset_handler;
		reg->status = STATUS_IRQ_RAISED;
		pci_epc_raise_irq(epc, PCI_EPC_IRQ_MSI, irq);
		goto reset_handler;
	}

reset_handler:
	reg->command = 0;

	queue_delayed_work(kpcitest_workqueue, &epf_test->cmd_handler,
			   msecs_to_jiffies(1));
}

static void pci_epf_test_linkup(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);

	queue_delayed_work(kpcitest_workqueue, &epf_test->cmd_handler,
			   msecs_to_jiffies(1));
}

static void pci_epf_test_unbind(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	struct pci_epc *epc = epf->epc;
	int bar;

	cancel_delayed_work(&epf_test->cmd_handler);
	pci_epc_stop(epc);
	for (bar = BAR_0; bar <= BAR_5; bar++) {
		if (epf_test->reg[bar]) {
			pci_epf_free_space(epf, epf_test->reg[bar], bar);
			pci_epc_clear_bar(epc, bar);
		}
	}
}

static int pci_epf_test_set_bar(struct pci_epf *epf)
{
	int flags;
	int bar;
	int ret;
	struct pci_epf_bar *epf_bar;
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);

	flags = PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_32;
	if (sizeof(dma_addr_t) == 0x8)
		flags |= PCI_BASE_ADDRESS_MEM_TYPE_64;

	for (bar = BAR_0; bar <= BAR_5; bar++) {
		epf_bar = &epf->bar[bar];
		ret = pci_epc_set_bar(epc, bar, epf_bar->phys_addr,
				      epf_bar->size, flags);
		if (ret) {
			pci_epf_free_space(epf, epf_test->reg[bar], bar);
			dev_err(dev, "failed to set BAR%d\n", bar);
			if (bar == BAR_0)
				return ret;
		}
	}

	return 0;
}

static int pci_epf_test_alloc_space(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);
	struct device *dev = &epf->dev;
	void *base;
	int bar;

	base = pci_epf_alloc_space(epf, sizeof(struct pci_epf_test_reg),
				   BAR_0);
	if (!base) {
		dev_err(dev, "failed to allocated register space\n");
		return -ENOMEM;
	}
	epf_test->reg[0] = base;

	for (bar = BAR_1; bar <= BAR_5; bar++) {
		base = pci_epf_alloc_space(epf, bar_size[bar - 1], bar);
		if (!base)
			dev_err(dev, "failed to allocate space for BAR%d\n",
				bar);
		epf_test->reg[bar] = base;
	}

	return 0;
}

static int pci_epf_test_bind(struct pci_epf *epf)
{
	int ret;
	struct pci_epf_header *header = epf->header;
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;

	if (WARN_ON_ONCE(!epc))
		return -EINVAL;

	ret = pci_epc_write_header(epc, header);
	if (ret) {
		dev_err(dev, "configuration header write failed\n");
		return ret;
	}

	ret = pci_epf_test_alloc_space(epf);
	if (ret)
		return ret;

	ret = pci_epf_test_set_bar(epf);
	if (ret)
		return ret;

	ret = pci_epc_set_msi(epc, epf->msi_interrupts);
	if (ret)
		return ret;

	return 0;
}

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

static int pci_epf_test_remove(struct pci_epf *epf)
{
	struct pci_epf_test *epf_test = epf_get_drvdata(epf);

	kfree(epf_test);
	return 0;
}

static struct pci_epf_ops ops = {
	.unbind	= pci_epf_test_unbind,
	.bind	= pci_epf_test_bind,
	.linkup = pci_epf_test_linkup,
};

static const struct pci_epf_device_id pci_epf_test_ids[] = {
	{
		.name = "pci_epf_test",
	},
	{},
};

static struct pci_epf_driver test_driver = {
	.driver.name	= "pci_epf_test",
	.probe		= pci_epf_test_probe,
	.remove		= pci_epf_test_remove,
	.id_table	= pci_epf_test_ids,
	.ops		= &ops,
	.owner		= THIS_MODULE,
};

static int __init pci_epf_test_init(void)
{
	int ret;

	kpcitest_workqueue = alloc_workqueue("kpcitest",
					     WQ_MEM_RECLAIM | WQ_HIGHPRI, 0);
	ret = pci_epf_register_driver(&test_driver);
	if (ret) {
		pr_err("failed to register pci epf test driver --> %d\n", ret);
		return ret;
	}

	return 0;
}
module_init(pci_epf_test_init);

static void __exit pci_epf_test_exit(void)
{
	pci_epf_unregister_driver(&test_driver);
}
module_exit(pci_epf_test_exit);

MODULE_DESCRIPTION("PCI EPF TEST DRIVER");
MODULE_AUTHOR("Kishon Vijay Abraham I <kishon@ti.com>");
MODULE_LICENSE("GPL v2");
