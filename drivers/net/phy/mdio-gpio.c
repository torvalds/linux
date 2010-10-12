/*
 * GPIO based MDIO bitbang driver.
 * Supports OpenFirmware.
 *
 * Copyright (c) 2008 CSE Semaphore Belgium.
 *  by Laurent Pinchart <laurentp@cse-semaphore.com>
 *
 * Copyright (C) 2008, Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * Based on earlier work by
 *
 * Copyright (c) 2003 Intracom S.A.
 *  by Pantelis Antoniou <panto@intracom.gr>
 *
 * 2005 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/mdio-gpio.h>

#ifdef CONFIG_OF_GPIO
#include <linux/of_gpio.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#endif

struct mdio_gpio_info {
	struct mdiobb_ctrl ctrl;
	int mdc, mdio;
};

static void mdio_dir(struct mdiobb_ctrl *ctrl, int dir)
{
	struct mdio_gpio_info *bitbang =
		container_of(ctrl, struct mdio_gpio_info, ctrl);

	if (dir)
		gpio_direction_output(bitbang->mdio, 1);
	else
		gpio_direction_input(bitbang->mdio);
}

static int mdio_get(struct mdiobb_ctrl *ctrl)
{
	struct mdio_gpio_info *bitbang =
		container_of(ctrl, struct mdio_gpio_info, ctrl);

	return gpio_get_value(bitbang->mdio);
}

static void mdio_set(struct mdiobb_ctrl *ctrl, int what)
{
	struct mdio_gpio_info *bitbang =
		container_of(ctrl, struct mdio_gpio_info, ctrl);

	gpio_set_value(bitbang->mdio, what);
}

static void mdc_set(struct mdiobb_ctrl *ctrl, int what)
{
	struct mdio_gpio_info *bitbang =
		container_of(ctrl, struct mdio_gpio_info, ctrl);

	gpio_set_value(bitbang->mdc, what);
}

static struct mdiobb_ops mdio_gpio_ops = {
	.owner = THIS_MODULE,
	.set_mdc = mdc_set,
	.set_mdio_dir = mdio_dir,
	.set_mdio_data = mdio_set,
	.get_mdio_data = mdio_get,
};

static struct mii_bus * __devinit mdio_gpio_bus_init(struct device *dev,
					struct mdio_gpio_platform_data *pdata,
					int bus_id)
{
	struct mii_bus *new_bus;
	struct mdio_gpio_info *bitbang;
	int i;

	bitbang = kzalloc(sizeof(*bitbang), GFP_KERNEL);
	if (!bitbang)
		goto out;

	bitbang->ctrl.ops = &mdio_gpio_ops;
	bitbang->mdc = pdata->mdc;
	bitbang->mdio = pdata->mdio;

	new_bus = alloc_mdio_bitbang(&bitbang->ctrl);
	if (!new_bus)
		goto out_free_bitbang;

	new_bus->name = "GPIO Bitbanged MDIO",

	new_bus->phy_mask = pdata->phy_mask;
	new_bus->irq = pdata->irqs;
	new_bus->parent = dev;

	if (new_bus->phy_mask == ~0)
		goto out_free_bus;

	for (i = 0; i < PHY_MAX_ADDR; i++)
		if (!new_bus->irq[i])
			new_bus->irq[i] = PHY_POLL;

	snprintf(new_bus->id, MII_BUS_ID_SIZE, "%x", bus_id);

	if (gpio_request(bitbang->mdc, "mdc"))
		goto out_free_bus;

	if (gpio_request(bitbang->mdio, "mdio"))
		goto out_free_mdc;

	gpio_direction_output(bitbang->mdc, 0);

	dev_set_drvdata(dev, new_bus);

	return new_bus;

out_free_mdc:
	gpio_free(bitbang->mdc);
out_free_bus:
	free_mdio_bitbang(new_bus);
out_free_bitbang:
	kfree(bitbang);
out:
	return NULL;
}

static void mdio_gpio_bus_deinit(struct device *dev)
{
	struct mii_bus *bus = dev_get_drvdata(dev);
	struct mdio_gpio_info *bitbang = bus->priv;

	dev_set_drvdata(dev, NULL);
	gpio_free(bitbang->mdio);
	gpio_free(bitbang->mdc);
	free_mdio_bitbang(bus);
	kfree(bitbang);
}

static void __devexit mdio_gpio_bus_destroy(struct device *dev)
{
	struct mii_bus *bus = dev_get_drvdata(dev);

	mdiobus_unregister(bus);
	mdio_gpio_bus_deinit(dev);
}

static int __devinit mdio_gpio_probe(struct platform_device *pdev)
{
	struct mdio_gpio_platform_data *pdata = pdev->dev.platform_data;
	struct mii_bus *new_bus;
	int ret;

	if (!pdata)
		return -ENODEV;

	new_bus = mdio_gpio_bus_init(&pdev->dev, pdata, pdev->id);
	if (!new_bus)
		return -ENODEV;

	ret = mdiobus_register(new_bus);
	if (ret)
		mdio_gpio_bus_deinit(&pdev->dev);

	return ret;
}

static int __devexit mdio_gpio_remove(struct platform_device *pdev)
{
	mdio_gpio_bus_destroy(&pdev->dev);

	return 0;
}

#ifdef CONFIG_OF_GPIO

static int __devinit mdio_ofgpio_probe(struct platform_device *ofdev,
                                        const struct of_device_id *match)
{
	struct mdio_gpio_platform_data *pdata;
	struct mii_bus *new_bus;
	int ret;

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	ret = of_get_gpio(ofdev->dev.of_node, 0);
	if (ret < 0)
		goto out_free;
	pdata->mdc = ret;

	ret = of_get_gpio(ofdev->dev.of_node, 1);
	if (ret < 0)
		goto out_free;
	pdata->mdio = ret;

	new_bus = mdio_gpio_bus_init(&ofdev->dev, pdata, pdata->mdc);
	if (!new_bus)
		goto out_free;

	ret = of_mdiobus_register(new_bus, ofdev->dev.of_node);
	if (ret)
		mdio_gpio_bus_deinit(&ofdev->dev);

	return ret;

out_free:
	kfree(pdata);
	return -ENODEV;
}

static int __devexit mdio_ofgpio_remove(struct platform_device *ofdev)
{
	mdio_gpio_bus_destroy(&ofdev->dev);
	kfree(ofdev->dev.platform_data);

	return 0;
}

static struct of_device_id mdio_ofgpio_match[] = {
	{
		.compatible = "virtual,mdio-gpio",
	},
	{},
};
MODULE_DEVICE_TABLE(of, mdio_ofgpio_match);

static struct of_platform_driver mdio_ofgpio_driver = {
	.driver = {
		.name = "mdio-gpio",
		.owner = THIS_MODULE,
		.of_match_table = mdio_ofgpio_match,
	},
	.probe = mdio_ofgpio_probe,
	.remove = __devexit_p(mdio_ofgpio_remove),
};

static inline int __init mdio_ofgpio_init(void)
{
	return of_register_platform_driver(&mdio_ofgpio_driver);
}

static inline void __exit mdio_ofgpio_exit(void)
{
	of_unregister_platform_driver(&mdio_ofgpio_driver);
}
#else
static inline int __init mdio_ofgpio_init(void) { return 0; }
static inline void __exit mdio_ofgpio_exit(void) { }
#endif /* CONFIG_OF_GPIO */

static struct platform_driver mdio_gpio_driver = {
	.probe = mdio_gpio_probe,
	.remove = __devexit_p(mdio_gpio_remove),
	.driver		= {
		.name	= "mdio-gpio",
		.owner	= THIS_MODULE,
	},
};

static int __init mdio_gpio_init(void)
{
	int ret;

	ret = mdio_ofgpio_init();
	if (ret)
		return ret;

	ret = platform_driver_register(&mdio_gpio_driver);
	if (ret)
		mdio_ofgpio_exit();

	return ret;
}
module_init(mdio_gpio_init);

static void __exit mdio_gpio_exit(void)
{
	platform_driver_unregister(&mdio_gpio_driver);
	mdio_ofgpio_exit();
}
module_exit(mdio_gpio_exit);

MODULE_ALIAS("platform:mdio-gpio");
MODULE_AUTHOR("Laurent Pinchart, Paulius Zaleckas");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic driver for MDIO bus emulation using GPIO");
