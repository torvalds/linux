/*
 * ISA bus.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/isa.h>

static struct device isa_bus = {
	.init_name	= "isa"
};

struct isa_dev {
	struct device dev;
	struct device *next;
	unsigned int id;
};

#define to_isa_dev(x) container_of((x), struct isa_dev, dev)

static int isa_bus_match(struct device *dev, struct device_driver *driver)
{
	struct isa_driver *isa_driver = to_isa_driver(driver);

	if (dev->platform_data == isa_driver) {
		if (!isa_driver->match ||
			isa_driver->match(dev, to_isa_dev(dev)->id))
			return 1;
		dev->platform_data = NULL;
	}
	return 0;
}

static int isa_bus_probe(struct device *dev)
{
	struct isa_driver *isa_driver = dev->platform_data;

	if (isa_driver->probe)
		return isa_driver->probe(dev, to_isa_dev(dev)->id);

	return 0;
}

static int isa_bus_remove(struct device *dev)
{
	struct isa_driver *isa_driver = dev->platform_data;

	if (isa_driver->remove)
		return isa_driver->remove(dev, to_isa_dev(dev)->id);

	return 0;
}

static void isa_bus_shutdown(struct device *dev)
{
	struct isa_driver *isa_driver = dev->platform_data;

	if (isa_driver->shutdown)
		isa_driver->shutdown(dev, to_isa_dev(dev)->id);
}

static int isa_bus_suspend(struct device *dev, pm_message_t state)
{
	struct isa_driver *isa_driver = dev->platform_data;

	if (isa_driver->suspend)
		return isa_driver->suspend(dev, to_isa_dev(dev)->id, state);

	return 0;
}

static int isa_bus_resume(struct device *dev)
{
	struct isa_driver *isa_driver = dev->platform_data;

	if (isa_driver->resume)
		return isa_driver->resume(dev, to_isa_dev(dev)->id);

	return 0;
}

static struct bus_type isa_bus_type = {
	.name		= "isa",
	.match		= isa_bus_match,
	.probe		= isa_bus_probe,
	.remove		= isa_bus_remove,
	.shutdown	= isa_bus_shutdown,
	.suspend	= isa_bus_suspend,
	.resume		= isa_bus_resume
};

static void isa_dev_release(struct device *dev)
{
	kfree(to_isa_dev(dev));
}

void isa_unregister_driver(struct isa_driver *isa_driver)
{
	struct device *dev = isa_driver->devices;

	while (dev) {
		struct device *tmp = to_isa_dev(dev)->next;
		device_unregister(dev);
		dev = tmp;
	}
	driver_unregister(&isa_driver->driver);
}
EXPORT_SYMBOL_GPL(isa_unregister_driver);

int isa_register_driver(struct isa_driver *isa_driver, unsigned int ndev)
{
	int error;
	unsigned int id;

	isa_driver->driver.bus	= &isa_bus_type;
	isa_driver->devices	= NULL;

	error = driver_register(&isa_driver->driver);
	if (error)
		return error;

	for (id = 0; id < ndev; id++) {
		struct isa_dev *isa_dev;

		isa_dev = kzalloc(sizeof *isa_dev, GFP_KERNEL);
		if (!isa_dev) {
			error = -ENOMEM;
			break;
		}

		isa_dev->dev.parent	= &isa_bus;
		isa_dev->dev.bus	= &isa_bus_type;

		dev_set_name(&isa_dev->dev, "%s.%u",
			     isa_driver->driver.name, id);
		isa_dev->dev.platform_data	= isa_driver;
		isa_dev->dev.release		= isa_dev_release;
		isa_dev->id			= id;

		isa_dev->dev.coherent_dma_mask = DMA_BIT_MASK(24);
		isa_dev->dev.dma_mask = &isa_dev->dev.coherent_dma_mask;

		error = device_register(&isa_dev->dev);
		if (error) {
			put_device(&isa_dev->dev);
			break;
		}

		if (isa_dev->dev.platform_data) {
			isa_dev->next = isa_driver->devices;
			isa_driver->devices = &isa_dev->dev;
		} else
			device_unregister(&isa_dev->dev);
	}

	if (!error && !isa_driver->devices)
		error = -ENODEV;

	if (error)
		isa_unregister_driver(isa_driver);

	return error;
}
EXPORT_SYMBOL_GPL(isa_register_driver);

static int __init isa_bus_init(void)
{
	int error;

	error = bus_register(&isa_bus_type);
	if (!error) {
		error = device_register(&isa_bus);
		if (error)
			bus_unregister(&isa_bus_type);
	}
	return error;
}

device_initcall(isa_bus_init);
