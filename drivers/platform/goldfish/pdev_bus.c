/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (C) 2011 Intel, Inc.
 * Copyright (C) 2013 Intel, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>

#define PDEV_BUS_OP_DONE        (0x00)
#define PDEV_BUS_OP_REMOVE_DEV  (0x04)
#define PDEV_BUS_OP_ADD_DEV     (0x08)

#define PDEV_BUS_OP_INIT        (0x00)

#define PDEV_BUS_OP             (0x00)
#define PDEV_BUS_GET_NAME       (0x04)
#define PDEV_BUS_NAME_LEN       (0x08)
#define PDEV_BUS_ID             (0x0c)
#define PDEV_BUS_IO_BASE        (0x10)
#define PDEV_BUS_IO_SIZE        (0x14)
#define PDEV_BUS_IRQ            (0x18)
#define PDEV_BUS_IRQ_COUNT      (0x1c)
#define PDEV_BUS_GET_NAME_HIGH  (0x20)

struct pdev_bus_dev {
	struct list_head list;
	struct platform_device pdev;
	struct resource resources[0];
};

static void goldfish_pdev_worker(struct work_struct *work);

static void __iomem *pdev_bus_base;
static unsigned long pdev_bus_addr;
static unsigned long pdev_bus_len;
static u32 pdev_bus_irq;
static LIST_HEAD(pdev_bus_new_devices);
static LIST_HEAD(pdev_bus_registered_devices);
static LIST_HEAD(pdev_bus_removed_devices);
static DECLARE_WORK(pdev_bus_worker, goldfish_pdev_worker);


static void goldfish_pdev_worker(struct work_struct *work)
{
	int ret;
	struct pdev_bus_dev *pos, *n;

	list_for_each_entry_safe(pos, n, &pdev_bus_removed_devices, list) {
		list_del(&pos->list);
		platform_device_unregister(&pos->pdev);
		kfree(pos);
	}
	list_for_each_entry_safe(pos, n, &pdev_bus_new_devices, list) {
		list_del(&pos->list);
		ret = platform_device_register(&pos->pdev);
		if (ret)
			pr_err("goldfish_pdev_worker failed to register device, %s\n",
								pos->pdev.name);
		list_add_tail(&pos->list, &pdev_bus_registered_devices);
	}
}

static void goldfish_pdev_remove(void)
{
	struct pdev_bus_dev *pos, *n;
	u32 base;

	base = readl(pdev_bus_base + PDEV_BUS_IO_BASE);

	list_for_each_entry_safe(pos, n, &pdev_bus_new_devices, list) {
		if (pos->resources[0].start == base) {
			list_del(&pos->list);
			kfree(pos);
			return;
		}
	}
	list_for_each_entry_safe(pos, n, &pdev_bus_registered_devices, list) {
		if (pos->resources[0].start == base) {
			list_del(&pos->list);
			list_add_tail(&pos->list, &pdev_bus_removed_devices);
			schedule_work(&pdev_bus_worker);
			return;
		}
	};
	pr_err("goldfish_pdev_remove could not find device at %x\n", base);
}

static int goldfish_new_pdev(void)
{
	struct pdev_bus_dev *dev;
	u32 name_len;
	u32 irq = -1, irq_count;
	int resource_count = 2;
	u32 base;
	char *name;

	base = readl(pdev_bus_base + PDEV_BUS_IO_BASE);

	irq_count = readl(pdev_bus_base + PDEV_BUS_IRQ_COUNT);
	name_len = readl(pdev_bus_base + PDEV_BUS_NAME_LEN);
	if (irq_count)
		resource_count++;

	dev = kzalloc(sizeof(*dev) +
		sizeof(struct resource) * resource_count +
		name_len + 1 + sizeof(*dev->pdev.dev.dma_mask), GFP_ATOMIC);
	if (dev == NULL)
		return -ENOMEM;

	dev->pdev.num_resources = resource_count;
	dev->pdev.resource = (struct resource *)(dev + 1);
	dev->pdev.name = name = (char *)(dev->pdev.resource + resource_count);
	dev->pdev.dev.coherent_dma_mask = ~0;
	dev->pdev.dev.dma_mask = (void *)(dev->pdev.name + name_len + 1);
	*dev->pdev.dev.dma_mask = ~0;

#ifdef CONFIG_64BIT
	writel((u32)((u64)name>>32), pdev_bus_base + PDEV_BUS_GET_NAME_HIGH);
#endif
	writel((u32)(unsigned long)name, pdev_bus_base + PDEV_BUS_GET_NAME);
	name[name_len] = '\0';
	dev->pdev.id = readl(pdev_bus_base + PDEV_BUS_ID);
	dev->pdev.resource[0].start = base;
	dev->pdev.resource[0].end = base +
				readl(pdev_bus_base + PDEV_BUS_IO_SIZE) - 1;
	dev->pdev.resource[0].flags = IORESOURCE_MEM;
	if (irq_count) {
		irq = readl(pdev_bus_base + PDEV_BUS_IRQ);
		dev->pdev.resource[1].start = irq;
		dev->pdev.resource[1].end = irq + irq_count - 1;
		dev->pdev.resource[1].flags = IORESOURCE_IRQ;
	}

	pr_debug("goldfish_new_pdev %s at %x irq %d\n", name, base, irq);
	list_add_tail(&dev->list, &pdev_bus_new_devices);
	schedule_work(&pdev_bus_worker);

	return 0;
}

static irqreturn_t goldfish_pdev_bus_interrupt(int irq, void *dev_id)
{
	irqreturn_t ret = IRQ_NONE;
	while (1) {
		u32 op = readl(pdev_bus_base + PDEV_BUS_OP);
		switch (op) {
		case PDEV_BUS_OP_DONE:
			return IRQ_NONE;

		case PDEV_BUS_OP_REMOVE_DEV:
			goldfish_pdev_remove();
			break;

		case PDEV_BUS_OP_ADD_DEV:
			goldfish_new_pdev();
			break;
		}
		ret = IRQ_HANDLED;
	}
	return ret;
}

static int goldfish_pdev_bus_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *r;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL)
		return -EINVAL;

	pdev_bus_addr = r->start;
	pdev_bus_len = resource_size(r);

	pdev_bus_base = ioremap(pdev_bus_addr, pdev_bus_len);
	if (pdev_bus_base == NULL) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "unable to map Goldfish MMIO.\n");
		goto free_resources;
	}

	r = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (r == NULL) {
		ret = -ENOENT;
		goto free_map;
	}

	pdev_bus_irq = r->start;

	ret = request_irq(pdev_bus_irq, goldfish_pdev_bus_interrupt,
				IRQF_SHARED, "goldfish_pdev_bus", pdev);
	if (ret) {
		dev_err(&pdev->dev, "unable to request Goldfish IRQ\n");
		goto free_map;
	}

	writel(PDEV_BUS_OP_INIT, pdev_bus_base + PDEV_BUS_OP);
	return 0;

free_map:
	iounmap(pdev_bus_base);
free_resources:
	release_mem_region(pdev_bus_addr, pdev_bus_len);
	return ret;
}

static int goldfish_pdev_bus_remove(struct platform_device *pdev)
{
	iounmap(pdev_bus_base);
	free_irq(pdev_bus_irq, pdev);
	release_mem_region(pdev_bus_addr, pdev_bus_len);
	return 0;
}

static struct platform_driver goldfish_pdev_bus_driver = {
	.probe = goldfish_pdev_bus_probe,
	.remove = goldfish_pdev_bus_remove,
	.driver = {
		.name = "goldfish_pdev_bus"
	}
};

module_platform_driver(goldfish_pdev_bus_driver);
