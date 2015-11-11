/*
 * Copyright (C) 2006, 2007 Eugene Konev <ejka@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Parts of the VLYNQ specification can be found here:
 * http://www.ti.com/litv/pdf/sprue36a
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/irq.h>

#include <linux/vlynq.h>

#define VLYNQ_CTRL_PM_ENABLE		0x80000000
#define VLYNQ_CTRL_CLOCK_INT		0x00008000
#define VLYNQ_CTRL_CLOCK_DIV(x)		(((x) & 7) << 16)
#define VLYNQ_CTRL_INT_LOCAL		0x00004000
#define VLYNQ_CTRL_INT_ENABLE		0x00002000
#define VLYNQ_CTRL_INT_VECTOR(x)	(((x) & 0x1f) << 8)
#define VLYNQ_CTRL_INT2CFG		0x00000080
#define VLYNQ_CTRL_RESET		0x00000001

#define VLYNQ_CTRL_CLOCK_MASK          (0x7 << 16)

#define VLYNQ_INT_OFFSET		0x00000014
#define VLYNQ_REMOTE_OFFSET		0x00000080

#define VLYNQ_STATUS_LINK		0x00000001
#define VLYNQ_STATUS_LERROR		0x00000080
#define VLYNQ_STATUS_RERROR		0x00000100

#define VINT_ENABLE			0x00000100
#define VINT_TYPE_EDGE			0x00000080
#define VINT_LEVEL_LOW			0x00000040
#define VINT_VECTOR(x)			((x) & 0x1f)
#define VINT_OFFSET(irq)		(8 * ((irq) % 4))

#define VLYNQ_AUTONEGO_V2		0x00010000

struct vlynq_regs {
	u32 revision;
	u32 control;
	u32 status;
	u32 int_prio;
	u32 int_status;
	u32 int_pending;
	u32 int_ptr;
	u32 tx_offset;
	struct vlynq_mapping rx_mapping[4];
	u32 chip;
	u32 autonego;
	u32 unused[6];
	u32 int_device[8];
};

#ifdef CONFIG_VLYNQ_DEBUG
static void vlynq_dump_regs(struct vlynq_device *dev)
{
	int i;

	printk(KERN_DEBUG "VLYNQ local=%p remote=%p\n",
			dev->local, dev->remote);
	for (i = 0; i < 32; i++) {
		printk(KERN_DEBUG "VLYNQ: local %d: %08x\n",
			i + 1, ((u32 *)dev->local)[i]);
		printk(KERN_DEBUG "VLYNQ: remote %d: %08x\n",
			i + 1, ((u32 *)dev->remote)[i]);
	}
}

static void vlynq_dump_mem(u32 *base, int count)
{
	int i;

	for (i = 0; i < (count + 3) / 4; i++) {
		if (i % 4 == 0)
			printk(KERN_DEBUG "\nMEM[0x%04x]:", i * 4);
		printk(KERN_DEBUG " 0x%08x", *(base + i));
	}
	printk(KERN_DEBUG "\n");
}
#endif

/* Check the VLYNQ link status with a given device */
static int vlynq_linked(struct vlynq_device *dev)
{
	int i;

	for (i = 0; i < 100; i++)
		if (readl(&dev->local->status) & VLYNQ_STATUS_LINK)
			return 1;
		else
			cpu_relax();

	return 0;
}

static void vlynq_reset(struct vlynq_device *dev)
{
	writel(readl(&dev->local->control) | VLYNQ_CTRL_RESET,
			&dev->local->control);

	/* Wait for the devices to finish resetting */
	msleep(5);

	/* Remove reset bit */
	writel(readl(&dev->local->control) & ~VLYNQ_CTRL_RESET,
			&dev->local->control);

	/* Give some time for the devices to settle */
	msleep(5);
}

static void vlynq_irq_unmask(struct irq_data *d)
{
	struct vlynq_device *dev = irq_data_get_irq_chip_data(d);
	int virq;
	u32 val;

	BUG_ON(!dev);
	virq = d->irq - dev->irq_start;
	val = readl(&dev->remote->int_device[virq >> 2]);
	val |= (VINT_ENABLE | virq) << VINT_OFFSET(virq);
	writel(val, &dev->remote->int_device[virq >> 2]);
}

static void vlynq_irq_mask(struct irq_data *d)
{
	struct vlynq_device *dev = irq_data_get_irq_chip_data(d);
	int virq;
	u32 val;

	BUG_ON(!dev);
	virq = d->irq - dev->irq_start;
	val = readl(&dev->remote->int_device[virq >> 2]);
	val &= ~(VINT_ENABLE << VINT_OFFSET(virq));
	writel(val, &dev->remote->int_device[virq >> 2]);
}

static int vlynq_irq_type(struct irq_data *d, unsigned int flow_type)
{
	struct vlynq_device *dev = irq_data_get_irq_chip_data(d);
	int virq;
	u32 val;

	BUG_ON(!dev);
	virq = d->irq - dev->irq_start;
	val = readl(&dev->remote->int_device[virq >> 2]);
	switch (flow_type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_BOTH:
		val |= VINT_TYPE_EDGE << VINT_OFFSET(virq);
		val &= ~(VINT_LEVEL_LOW << VINT_OFFSET(virq));
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		val &= ~(VINT_TYPE_EDGE << VINT_OFFSET(virq));
		val &= ~(VINT_LEVEL_LOW << VINT_OFFSET(virq));
		break;
	case IRQ_TYPE_LEVEL_LOW:
		val &= ~(VINT_TYPE_EDGE << VINT_OFFSET(virq));
		val |= VINT_LEVEL_LOW << VINT_OFFSET(virq);
		break;
	default:
		return -EINVAL;
	}
	writel(val, &dev->remote->int_device[virq >> 2]);
	return 0;
}

static void vlynq_local_ack(struct irq_data *d)
{
	struct vlynq_device *dev = irq_data_get_irq_chip_data(d);
	u32 status = readl(&dev->local->status);

	pr_debug("%s: local status: 0x%08x\n",
		       dev_name(&dev->dev), status);
	writel(status, &dev->local->status);
}

static void vlynq_remote_ack(struct irq_data *d)
{
	struct vlynq_device *dev = irq_data_get_irq_chip_data(d);
	u32 status = readl(&dev->remote->status);

	pr_debug("%s: remote status: 0x%08x\n",
		       dev_name(&dev->dev), status);
	writel(status, &dev->remote->status);
}

static irqreturn_t vlynq_irq(int irq, void *dev_id)
{
	struct vlynq_device *dev = dev_id;
	u32 status;
	int virq = 0;

	status = readl(&dev->local->int_status);
	writel(status, &dev->local->int_status);

	if (unlikely(!status))
		spurious_interrupt();

	while (status) {
		if (status & 1)
			do_IRQ(dev->irq_start + virq);
		status >>= 1;
		virq++;
	}

	return IRQ_HANDLED;
}

static struct irq_chip vlynq_irq_chip = {
	.name = "vlynq",
	.irq_unmask = vlynq_irq_unmask,
	.irq_mask = vlynq_irq_mask,
	.irq_set_type = vlynq_irq_type,
};

static struct irq_chip vlynq_local_chip = {
	.name = "vlynq local error",
	.irq_unmask = vlynq_irq_unmask,
	.irq_mask = vlynq_irq_mask,
	.irq_ack = vlynq_local_ack,
};

static struct irq_chip vlynq_remote_chip = {
	.name = "vlynq local error",
	.irq_unmask = vlynq_irq_unmask,
	.irq_mask = vlynq_irq_mask,
	.irq_ack = vlynq_remote_ack,
};

static int vlynq_setup_irq(struct vlynq_device *dev)
{
	u32 val;
	int i, virq;

	if (dev->local_irq == dev->remote_irq) {
		printk(KERN_ERR
		       "%s: local vlynq irq should be different from remote\n",
		       dev_name(&dev->dev));
		return -EINVAL;
	}

	/* Clear local and remote error bits */
	writel(readl(&dev->local->status), &dev->local->status);
	writel(readl(&dev->remote->status), &dev->remote->status);

	/* Now setup interrupts */
	val = VLYNQ_CTRL_INT_VECTOR(dev->local_irq);
	val |= VLYNQ_CTRL_INT_ENABLE | VLYNQ_CTRL_INT_LOCAL |
		VLYNQ_CTRL_INT2CFG;
	val |= readl(&dev->local->control);
	writel(VLYNQ_INT_OFFSET, &dev->local->int_ptr);
	writel(val, &dev->local->control);

	val = VLYNQ_CTRL_INT_VECTOR(dev->remote_irq);
	val |= VLYNQ_CTRL_INT_ENABLE;
	val |= readl(&dev->remote->control);
	writel(VLYNQ_INT_OFFSET, &dev->remote->int_ptr);
	writel(val, &dev->remote->int_ptr);
	writel(val, &dev->remote->control);

	for (i = dev->irq_start; i <= dev->irq_end; i++) {
		virq = i - dev->irq_start;
		if (virq == dev->local_irq) {
			irq_set_chip_and_handler(i, &vlynq_local_chip,
						 handle_level_irq);
			irq_set_chip_data(i, dev);
		} else if (virq == dev->remote_irq) {
			irq_set_chip_and_handler(i, &vlynq_remote_chip,
						 handle_level_irq);
			irq_set_chip_data(i, dev);
		} else {
			irq_set_chip_and_handler(i, &vlynq_irq_chip,
						 handle_simple_irq);
			irq_set_chip_data(i, dev);
			writel(0, &dev->remote->int_device[virq >> 2]);
		}
	}

	if (request_irq(dev->irq, vlynq_irq, IRQF_SHARED, "vlynq", dev)) {
		printk(KERN_ERR "%s: request_irq failed\n",
					dev_name(&dev->dev));
		return -EAGAIN;
	}

	return 0;
}

static void vlynq_device_release(struct device *dev)
{
	struct vlynq_device *vdev = to_vlynq_device(dev);
	kfree(vdev);
}

static int vlynq_device_match(struct device *dev,
			      struct device_driver *drv)
{
	struct vlynq_device *vdev = to_vlynq_device(dev);
	struct vlynq_driver *vdrv = to_vlynq_driver(drv);
	struct vlynq_device_id *ids = vdrv->id_table;

	while (ids->id) {
		if (ids->id == vdev->dev_id) {
			vdev->divisor = ids->divisor;
			vlynq_set_drvdata(vdev, ids);
			printk(KERN_INFO "Driver found for VLYNQ "
				"device: %08x\n", vdev->dev_id);
			return 1;
		}
		printk(KERN_DEBUG "Not using the %08x VLYNQ device's driver"
			" for VLYNQ device: %08x\n", ids->id, vdev->dev_id);
		ids++;
	}
	return 0;
}

static int vlynq_device_probe(struct device *dev)
{
	struct vlynq_device *vdev = to_vlynq_device(dev);
	struct vlynq_driver *drv = to_vlynq_driver(dev->driver);
	struct vlynq_device_id *id = vlynq_get_drvdata(vdev);
	int result = -ENODEV;

	if (drv->probe)
		result = drv->probe(vdev, id);
	if (result)
		put_device(dev);
	return result;
}

static int vlynq_device_remove(struct device *dev)
{
	struct vlynq_driver *drv = to_vlynq_driver(dev->driver);

	if (drv->remove)
		drv->remove(to_vlynq_device(dev));

	return 0;
}

int __vlynq_register_driver(struct vlynq_driver *driver, struct module *owner)
{
	driver->driver.name = driver->name;
	driver->driver.bus = &vlynq_bus_type;
	return driver_register(&driver->driver);
}
EXPORT_SYMBOL(__vlynq_register_driver);

void vlynq_unregister_driver(struct vlynq_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL(vlynq_unregister_driver);

/*
 * A VLYNQ remote device can clock the VLYNQ bus master
 * using a dedicated clock line. In that case, both the
 * remove device and the bus master should have the same
 * serial clock dividers configured. Iterate through the
 * 8 possible dividers until we actually link with the
 * device.
 */
static int __vlynq_try_remote(struct vlynq_device *dev)
{
	int i;

	vlynq_reset(dev);
	for (i = dev->dev_id ? vlynq_rdiv2 : vlynq_rdiv8; dev->dev_id ?
			i <= vlynq_rdiv8 : i >= vlynq_rdiv2;
		dev->dev_id ? i++ : i--) {

		if (!vlynq_linked(dev))
			break;

		writel((readl(&dev->remote->control) &
				~VLYNQ_CTRL_CLOCK_MASK) |
				VLYNQ_CTRL_CLOCK_INT |
				VLYNQ_CTRL_CLOCK_DIV(i - vlynq_rdiv1),
				&dev->remote->control);
		writel((readl(&dev->local->control)
				& ~(VLYNQ_CTRL_CLOCK_INT |
				VLYNQ_CTRL_CLOCK_MASK)) |
				VLYNQ_CTRL_CLOCK_DIV(i - vlynq_rdiv1),
				&dev->local->control);

		if (vlynq_linked(dev)) {
			printk(KERN_DEBUG
				"%s: using remote clock divisor %d\n",
				dev_name(&dev->dev), i - vlynq_rdiv1 + 1);
			dev->divisor = i;
			return 0;
		} else {
			vlynq_reset(dev);
		}
	}

	return -ENODEV;
}

/*
 * A VLYNQ remote device can be clocked by the VLYNQ bus
 * master using a dedicated clock line. In that case, only
 * the bus master configures the serial clock divider.
 * Iterate through the 8 possible dividers until we
 * actually get a link with the device.
 */
static int __vlynq_try_local(struct vlynq_device *dev)
{
	int i;

	vlynq_reset(dev);

	for (i = dev->dev_id ? vlynq_ldiv2 : vlynq_ldiv8; dev->dev_id ?
			i <= vlynq_ldiv8 : i >= vlynq_ldiv2;
		dev->dev_id ? i++ : i--) {

		writel((readl(&dev->local->control) &
				~VLYNQ_CTRL_CLOCK_MASK) |
				VLYNQ_CTRL_CLOCK_INT |
				VLYNQ_CTRL_CLOCK_DIV(i - vlynq_ldiv1),
				&dev->local->control);

		if (vlynq_linked(dev)) {
			printk(KERN_DEBUG
				"%s: using local clock divisor %d\n",
				dev_name(&dev->dev), i - vlynq_ldiv1 + 1);
			dev->divisor = i;
			return 0;
		} else {
			vlynq_reset(dev);
		}
	}

	return -ENODEV;
}

/*
 * When using external clocking method, serial clock
 * is supplied by an external oscillator, therefore we
 * should mask the local clock bit in the clock control
 * register for both the bus master and the remote device.
 */
static int __vlynq_try_external(struct vlynq_device *dev)
{
	vlynq_reset(dev);
	if (!vlynq_linked(dev))
		return -ENODEV;

	writel((readl(&dev->remote->control) &
			~VLYNQ_CTRL_CLOCK_INT),
			&dev->remote->control);

	writel((readl(&dev->local->control) &
			~VLYNQ_CTRL_CLOCK_INT),
			&dev->local->control);

	if (vlynq_linked(dev)) {
		printk(KERN_DEBUG "%s: using external clock\n",
			dev_name(&dev->dev));
			dev->divisor = vlynq_div_external;
		return 0;
	}

	return -ENODEV;
}

static int __vlynq_enable_device(struct vlynq_device *dev)
{
	int result;
	struct plat_vlynq_ops *ops = dev->dev.platform_data;

	result = ops->on(dev);
	if (result)
		return result;

	switch (dev->divisor) {
	case vlynq_div_external:
	case vlynq_div_auto:
		/* When the device is brought from reset it should have clock
		 * generation negotiated by hardware.
		 * Check which device is generating clocks and perform setup
		 * accordingly */
		if (vlynq_linked(dev) && readl(&dev->remote->control) &
		   VLYNQ_CTRL_CLOCK_INT) {
			if (!__vlynq_try_remote(dev) ||
				!__vlynq_try_local(dev)  ||
				!__vlynq_try_external(dev))
				return 0;
		} else {
			if (!__vlynq_try_external(dev) ||
				!__vlynq_try_local(dev)    ||
				!__vlynq_try_remote(dev))
				return 0;
		}
		break;
	case vlynq_ldiv1:
	case vlynq_ldiv2:
	case vlynq_ldiv3:
	case vlynq_ldiv4:
	case vlynq_ldiv5:
	case vlynq_ldiv6:
	case vlynq_ldiv7:
	case vlynq_ldiv8:
		writel(VLYNQ_CTRL_CLOCK_INT |
			VLYNQ_CTRL_CLOCK_DIV(dev->divisor -
			vlynq_ldiv1), &dev->local->control);
		writel(0, &dev->remote->control);
		if (vlynq_linked(dev)) {
			printk(KERN_DEBUG
				"%s: using local clock divisor %d\n",
				dev_name(&dev->dev),
				dev->divisor - vlynq_ldiv1 + 1);
			return 0;
		}
		break;
	case vlynq_rdiv1:
	case vlynq_rdiv2:
	case vlynq_rdiv3:
	case vlynq_rdiv4:
	case vlynq_rdiv5:
	case vlynq_rdiv6:
	case vlynq_rdiv7:
	case vlynq_rdiv8:
		writel(0, &dev->local->control);
		writel(VLYNQ_CTRL_CLOCK_INT |
			VLYNQ_CTRL_CLOCK_DIV(dev->divisor -
			vlynq_rdiv1), &dev->remote->control);
		if (vlynq_linked(dev)) {
			printk(KERN_DEBUG
				"%s: using remote clock divisor %d\n",
				dev_name(&dev->dev),
				dev->divisor - vlynq_rdiv1 + 1);
			return 0;
		}
		break;
	}

	ops->off(dev);
	return -ENODEV;
}

int vlynq_enable_device(struct vlynq_device *dev)
{
	struct plat_vlynq_ops *ops = dev->dev.platform_data;
	int result = -ENODEV;

	result = __vlynq_enable_device(dev);
	if (result)
		return result;

	result = vlynq_setup_irq(dev);
	if (result)
		ops->off(dev);

	dev->enabled = !result;
	return result;
}
EXPORT_SYMBOL(vlynq_enable_device);


void vlynq_disable_device(struct vlynq_device *dev)
{
	struct plat_vlynq_ops *ops = dev->dev.platform_data;

	dev->enabled = 0;
	free_irq(dev->irq, dev);
	ops->off(dev);
}
EXPORT_SYMBOL(vlynq_disable_device);

int vlynq_set_local_mapping(struct vlynq_device *dev, u32 tx_offset,
			    struct vlynq_mapping *mapping)
{
	int i;

	if (!dev->enabled)
		return -ENXIO;

	writel(tx_offset, &dev->local->tx_offset);
	for (i = 0; i < 4; i++) {
		writel(mapping[i].offset, &dev->local->rx_mapping[i].offset);
		writel(mapping[i].size, &dev->local->rx_mapping[i].size);
	}
	return 0;
}
EXPORT_SYMBOL(vlynq_set_local_mapping);

int vlynq_set_remote_mapping(struct vlynq_device *dev, u32 tx_offset,
			     struct vlynq_mapping *mapping)
{
	int i;

	if (!dev->enabled)
		return -ENXIO;

	writel(tx_offset, &dev->remote->tx_offset);
	for (i = 0; i < 4; i++) {
		writel(mapping[i].offset, &dev->remote->rx_mapping[i].offset);
		writel(mapping[i].size, &dev->remote->rx_mapping[i].size);
	}
	return 0;
}
EXPORT_SYMBOL(vlynq_set_remote_mapping);

int vlynq_set_local_irq(struct vlynq_device *dev, int virq)
{
	int irq = dev->irq_start + virq;
	if (dev->enabled)
		return -EBUSY;

	if ((irq < dev->irq_start) || (irq > dev->irq_end))
		return -EINVAL;

	if (virq == dev->remote_irq)
		return -EINVAL;

	dev->local_irq = virq;

	return 0;
}
EXPORT_SYMBOL(vlynq_set_local_irq);

int vlynq_set_remote_irq(struct vlynq_device *dev, int virq)
{
	int irq = dev->irq_start + virq;
	if (dev->enabled)
		return -EBUSY;

	if ((irq < dev->irq_start) || (irq > dev->irq_end))
		return -EINVAL;

	if (virq == dev->local_irq)
		return -EINVAL;

	dev->remote_irq = virq;

	return 0;
}
EXPORT_SYMBOL(vlynq_set_remote_irq);

static int vlynq_probe(struct platform_device *pdev)
{
	struct vlynq_device *dev;
	struct resource *regs_res, *mem_res, *irq_res;
	int len, result;

	regs_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!regs_res)
		return -ENODEV;

	mem_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mem");
	if (!mem_res)
		return -ENODEV;

	irq_res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "devirq");
	if (!irq_res)
		return -ENODEV;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		printk(KERN_ERR
		       "vlynq: failed to allocate device structure\n");
		return -ENOMEM;
	}

	dev->id = pdev->id;
	dev->dev.bus = &vlynq_bus_type;
	dev->dev.parent = &pdev->dev;
	dev_set_name(&dev->dev, "vlynq%d", dev->id);
	dev->dev.platform_data = pdev->dev.platform_data;
	dev->dev.release = vlynq_device_release;

	dev->regs_start = regs_res->start;
	dev->regs_end = regs_res->end;
	dev->mem_start = mem_res->start;
	dev->mem_end = mem_res->end;

	len = resource_size(regs_res);
	if (!request_mem_region(regs_res->start, len, dev_name(&dev->dev))) {
		printk(KERN_ERR "%s: Can't request vlynq registers\n",
		       dev_name(&dev->dev));
		result = -ENXIO;
		goto fail_request;
	}

	dev->local = ioremap(regs_res->start, len);
	if (!dev->local) {
		printk(KERN_ERR "%s: Can't remap vlynq registers\n",
		       dev_name(&dev->dev));
		result = -ENXIO;
		goto fail_remap;
	}

	dev->remote = (struct vlynq_regs *)((void *)dev->local +
					    VLYNQ_REMOTE_OFFSET);

	dev->irq = platform_get_irq_byname(pdev, "irq");
	dev->irq_start = irq_res->start;
	dev->irq_end = irq_res->end;
	dev->local_irq = dev->irq_end - dev->irq_start;
	dev->remote_irq = dev->local_irq - 1;

	if (device_register(&dev->dev))
		goto fail_register;
	platform_set_drvdata(pdev, dev);

	printk(KERN_INFO "%s: regs 0x%p, irq %d, mem 0x%p\n",
	       dev_name(&dev->dev), (void *)dev->regs_start, dev->irq,
	       (void *)dev->mem_start);

	dev->dev_id = 0;
	dev->divisor = vlynq_div_auto;
	result = __vlynq_enable_device(dev);
	if (result == 0) {
		dev->dev_id = readl(&dev->remote->chip);
		((struct plat_vlynq_ops *)(dev->dev.platform_data))->off(dev);
	}
	if (dev->dev_id)
		printk(KERN_INFO "Found a VLYNQ device: %08x\n", dev->dev_id);

	return 0;

fail_register:
	iounmap(dev->local);
fail_remap:
fail_request:
	release_mem_region(regs_res->start, len);
	kfree(dev);
	return result;
}

static int vlynq_remove(struct platform_device *pdev)
{
	struct vlynq_device *dev = platform_get_drvdata(pdev);

	device_unregister(&dev->dev);
	iounmap(dev->local);
	release_mem_region(dev->regs_start,
			   dev->regs_end - dev->regs_start + 1);

	kfree(dev);

	return 0;
}

static struct platform_driver vlynq_platform_driver = {
	.driver.name = "vlynq",
	.probe = vlynq_probe,
	.remove = vlynq_remove,
};

struct bus_type vlynq_bus_type = {
	.name = "vlynq",
	.match = vlynq_device_match,
	.probe = vlynq_device_probe,
	.remove = vlynq_device_remove,
};
EXPORT_SYMBOL(vlynq_bus_type);

static int vlynq_init(void)
{
	int res = 0;

	res = bus_register(&vlynq_bus_type);
	if (res)
		goto fail_bus;

	res = platform_driver_register(&vlynq_platform_driver);
	if (res)
		goto fail_platform;

	return 0;

fail_platform:
	bus_unregister(&vlynq_bus_type);
fail_bus:
	return res;
}

static void vlynq_exit(void)
{
	platform_driver_unregister(&vlynq_platform_driver);
	bus_unregister(&vlynq_bus_type);
}

module_init(vlynq_init);
module_exit(vlynq_exit);
