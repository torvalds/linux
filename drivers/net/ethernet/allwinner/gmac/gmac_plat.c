/*******************************************************************************
  This contains the functions to handle the platform driver.

  Copyright (C) 2012 Shuge

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".
*******************************************************************************/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/clk.h>

#include <mach/gpio.h>
#include <mach/irqs.h>
#include <mach/clock.h>

#include "sunxi_gmac.h"

static int gmac_system_init(struct gmac_priv *priv)
{

	int ret = 0;

#ifndef CONFIG_GMAC_SCRIPT_SYS
	if(priv->gpiobase){
		writel(0x55555555, priv->gpiobase + PA_CFG0);
		writel(0x50555505, priv->gpiobase + PA_CFG1);
		writel(0x00000005, priv->gpiobase + PA_CFG2);
	}
#else
	priv->gpio_handle = gpio_request_ex("gmac_para", NULL);
	if(!priv->gpio_handle) {
		pr_warning("twi0 request gpio fail!\n");
		ret = -1;
	}
#endif
	return ret;
}

static int gmac_sys_request(struct platform_device *pdev, struct gmac_priv *priv)
{
	int ret = 0;
#ifndef CONFIG_GMAC_CLK_SYS
	struct resource *io_clk;
#endif
#ifndef CONFIG_GMAC_SCRIPT_SYS
	struct resource *io_gpio;
#endif
	struct resource *clk_reg;

	clk_reg = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	if (unlikely(!clk_reg)){
		ret = -ENODEV;
		printk(KERN_ERR "ERROR: Get gmac clk reg is failed!\n");
		goto out;
	}

	priv->gmac_clk_reg = ioremap(clk_reg->start, resource_size(clk_reg));
	if (unlikely(!priv->gmac_clk_reg)) {
		printk(KERN_ERR "%s: ERROR: memory mapping failed\n", __func__);
		ret = -ENOMEM;
		goto out;
	}

#ifndef CONFIG_GMAC_CLK_SYS
	io_clk = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (unlikely(!io_clk)){
		ret = -ENODEV;
		goto out_clk_reg;
	}

#if 0
	if (!request_mem_region(io_clk->start, resource_size(io_clk), pdev->name)) {
		printk(KERN_ERR "%s: ERROR: memory allocation failed\n"
		       "cannot get the I/O addr 0x%x\n",
		       __func__, (unsigned int)io_clk->start);
		ret = -EBUSY;
		goto out_clk_reg;
	}
#endif

	priv->clkbase = ioremap(io_clk->start, resource_size(io_clk));
	if (unlikely(!priv->clkbase)) {
		printk(KERN_ERR "%s: ERROR: memory mapping failed\n", __func__);
		ret = -ENOMEM;
		goto out_release_clk;
	}
#else
	priv->gmac_ahb_clk = clk_get(&pdev->dev, CLK_AHB_GMAC);
	if (unlikely(!priv->gmac_ahb_clk || IS_ERR(priv->gmac_ahb_clk))) {
		printk(KERN_ERR "ERROR: Get clock is failed!\n");
		ret = -1;
		goto out;
	}
    /*
	priv->gmac_mod_clk = clk_get(&pdev->dev, CLK_MOD_GMAC);
	if (unlikely(!priv->gmac_mod_clk || IS_ERR(priv->gmac_mod_clk))) {
		printk(KERN_ERR "ERROR: Get mod gmac is failed!\n");
		ret = -1;
		goto out;
	}
    */
#endif

#ifndef CONFIG_GMAC_SCRIPT_SYS
	io_gpio = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (unlikely(!io_gpio)){
		ret = -ENODEV;
		goto out_unmap_clk;
	}

	if (!request_mem_region(io_gpio->start, resource_size(io_gpio), pdev->name)) {
		printk(KERN_ERR "%s: ERROR: memory allocation failed"
		       "cannot get the I/O addr 0x%x\n",
		       __func__, (unsigned int)io_gpio->start);
		ret = -EBUSY;
		goto out_unmap_clk;
	}

	priv->gpiobase = ioremap(io_gpio->start, resource_size(io_gpio));
	if (unlikely(!priv->gpiobase)) {
		printk(KERN_ERR "%s: ERROR: memory mapping failed\n", __func__);
		ret = -ENOMEM;
		goto out_release_gpio;
	}
#endif

	return 0;

#ifndef CONFIG_GMAC_SCRIPT_SYS
out_release_gpio:
	release_mem_region(io_gpio->start, resource_size(io_gpio));
out_unmap_clk:
#endif
#ifndef CONFIG_GMAC_CLK_SYS
	iounmap(priv->clkbase);
out_release_clk:
	release_mem_region(io_clk->start, resource_size(io_clk));
out_clk_reg:
	iounmap(priv->gmac_clk_reg);
#endif
out:
	return ret;
}

static void gmac_sys_release(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct gmac_priv *priv = netdev_priv(ndev);
#if !defined(CONFIG_GMAC_SCRIPT_SYS) || !defined(CONFIG_GMAC_CLK_SYS)
	struct resource *res;
#endif

#ifndef CONFIG_GMAC_SCRIPT_SYS
	iounmap((void *)priv->gpiobase);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	release_mem_region(res->start, resource_size(res));
#else
	gpio_release(priv->gpio_handle, 0);
#endif

	iounmap(priv->gmac_clk_reg);

#ifndef CONFIG_GMAC_CLK_SYS
	iounmap((void *)priv->clkbase);
#else
	if (priv->gmac_ahb_clk)
		clk_put(priv->gmac_ahb_clk);
/*
    if (priv->gmac_mod_clk)
		clk_put(priv->gmac_mod_clk);
*/
#endif
}

static int gmac_pltfr_probe(struct platform_device *pdev)
{
	int ret = 0;
	int irq = 0;
	struct resource *io_gmac;
	void __iomem *addr = NULL;
	struct gmac_priv *priv = NULL;

	io_gmac = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!io_gmac)
		return -ENODEV;

	if (!request_mem_region(io_gmac->start, resource_size(io_gmac), pdev->name)) {
		pr_err("%s: ERROR: memory allocation failed"
		       "cannot get the I/O addr 0x%x\n",
		       __func__, (unsigned int)io_gmac->start);
		return -EBUSY;
	}

	addr = ioremap(io_gmac->start, resource_size(io_gmac));
	if (!addr) {
		pr_err("%s: ERROR: memory mapping failed\n", __func__);
		ret = -ENOMEM;
		goto out_release_region;
	}

	/* Get the MAC information */
	irq = platform_get_irq_byname(pdev, "gmacirq");
	if (irq == -ENXIO) {
		printk(KERN_ERR "%s: ERROR: MAC IRQ configuration "
		       "information not found\n", __func__);
		ret = -ENXIO;
		goto out_unmap;
	}

	priv = gmac_dvr_probe(&(pdev->dev), addr, irq);
	if (!priv) {
		printk("[gmac]: %s: main driver probe failed\n", __func__);
		goto out_unmap;
	}

	if(gmac_sys_request(pdev, priv))
		goto out_unmap;

	ret = gmac_system_init(priv);
	if (ret){
		printk(KERN_ERR "[gmac]: gmac_system_init is failed...\n");
		goto out_unmap;
	}
	platform_set_drvdata(pdev, priv->ndev);

	printk("[gmac]: sun6i_gmac platform driver registration completed\n");

	return 0;

out_unmap:
	iounmap(addr);
	platform_set_drvdata(pdev, NULL);

out_release_region:
	release_mem_region(io_gmac->start, resource_size(io_gmac));

	return ret;
}

static int gmac_pltfr_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct gmac_priv *priv = netdev_priv(ndev);
	struct resource *res;
	int ret = gmac_dvr_remove(ndev);


	iounmap((void *)priv->ioaddr);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res)
		release_mem_region(res->start, resource_size(res));

	gmac_sys_release(pdev);
	platform_set_drvdata(pdev, NULL);

	return ret;
}

#ifdef CONFIG_PM
static int gmac_pltfr_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	return gmac_suspend(ndev);
}

static int gmac_pltfr_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	return gmac_resume(ndev);
}

int gmac_pltfr_freeze(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	return gmac_freeze(ndev);
}

int gmac_pltfr_restore(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	return gmac_restore(ndev);
}

static const struct dev_pm_ops gmac_pltfr_pm_ops = {
	.suspend = gmac_pltfr_suspend,
	.resume = gmac_pltfr_resume,
	.freeze = gmac_pltfr_freeze,
	.thaw = gmac_pltfr_restore,
	.restore = gmac_pltfr_restore,
};
#else
static const struct dev_pm_ops gmac_pltfr_pm_ops;
#endif /* CONFIG_PM */

struct platform_driver gmac_driver = {
	.probe	= gmac_pltfr_probe,
	.remove = gmac_pltfr_remove,
	.driver = {
		   .name = GMAC_RESOURCE_NAME,
		   .owner = THIS_MODULE,
		   .pm = &gmac_pltfr_pm_ops,
		   },
};

static struct resource gmac_resources[] = {
	[0] = {
		.name	= "gmacio",
		.start	= GMAC_BASE,
		.end	= GMAC_BASE + 0x1054,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "clkbus",
		.start	= CCMU_BASE,
		.end	= CCMU_BASE + GMAC_CLK_REG,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.name	= "gpio",
		.start	= GPIO_BASE,
		.end	= GPIO_BASE + 0x0c,
		.flags	= IORESOURCE_MEM,
	},
	[3] = {
		.name	= "gmac_clk_reg",
		.start	= CCMU_BASE,
		.end	= CCMU_BASE + GMAC_CLK_REG,
		.flags	= IORESOURCE_MEM,
	},
	[4] = {
		.name	= "gmacirq",
		.start	= SW_INT_IRQNO_GMAC,
		.end	= SW_INT_IRQNO_GMAC,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct gmac_mdio_bus_data gmac_mdio_data = {
	.bus_id = 0,
	.phy_reset  = NULL,
	.phy_mask = 0,
	.irqs = NULL,
	.probed_phy_irq = 0,
};

static struct gmac_plat_data gmac_platdata ={
	.bus_id = 0,
	.phy_addr = -1,
	.phy_interface = PHY_INTERFACE_MODE_RGMII,
	.clk_csr = 2,

	.tx_coe = 1,
	.bugged_jumbo = 0,
	.force_sf_dma_mode = 1,
	.pbl = 2,
	.mdio_bus_data = &gmac_mdio_data,
};

static void gmac_device_release(struct device *dev)
{
}

struct platform_device gmac_device = {
	.name = GMAC_RESOURCE_NAME,
	.id = -1,
	.resource = gmac_resources,
	.num_resources = ARRAY_SIZE(gmac_resources),
	.dev = {
		.release = gmac_device_release,
		.platform_data = &gmac_platdata,
	},
};
