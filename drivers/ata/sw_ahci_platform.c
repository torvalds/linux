/*
 * drivers/ata/sw_ahci_platform.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Daniel Wang <danielwang@allwinnertech.com>
 *
 * Based on ahci_platform.c AHCI SATA platform driver
 *
 * Copyright 2004-2005  Red Hat, Inc.
 *   Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2010  MontaVista Software, LLC.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include <linux/ahci_platform.h>
#include "ahci.h"

#include <linux/clk.h>
#include <plat/sys_config.h>
#include "sw_ahci_platform.h"

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT("sw_ahci_platform"),
};

static char* sw_ahci_hclk_name = "ahb_sata";
static char* sw_ahci_mclk_name = "sata";
static char* sw_ahci_para_name = "sata_para";
static char* sw_ahci_used_name = "sata_used";
static char* sw_ahci_gpio_name = "sata_power_en";

static struct resource sw_ahci_resources[] = {
	[0] = {
		.start		= SW_AHCI_BASE,
		.end		= SW_AHCI_BASE + 0x1000 - 1,
		.flags		= IORESOURCE_MEM,
	},

	[1] = {
    	.start	= INTC_IRQNO_AHCI,
    	.end	= INTC_IRQNO_AHCI,
    	.flags	= IORESOURCE_IRQ,
    },
};

static int sw_ahci_phy_init(unsigned int base)
{
	unsigned int tmp;
	const unsigned int timeout_val = 0x100000;
	unsigned int timeout = timeout_val;

	for(tmp=0; tmp<0x1000; tmp++);

	SW_AHCI_ACCESS_LOCK(base, 0);

	tmp = ahci_readl(base, SW_AHCI_PHYCS1R_OFFSET);
	tmp |= (0x1<<19);
	ahci_writel(base, SW_AHCI_PHYCS1R_OFFSET, tmp);

	tmp = ahci_readl(base, SW_AHCI_PHYCS0R_OFFSET);
	tmp |= 0x1<<23;
	tmp |= 0x1<<18;
	tmp &= ~(0x7<<24);
	tmp |= 0x5<<24;
	ahci_writel(base, SW_AHCI_PHYCS0R_OFFSET, tmp);

	tmp = ahci_readl(base, SW_AHCI_PHYCS1R_OFFSET);
	tmp &= ~(0x3<<16);
	tmp |= (0x2<<16);
	tmp &= ~(0x1f<<8);
	tmp |= (6<<8);
	tmp &= ~(0x3<<6);
	tmp |= (2<<6);
	ahci_writel(base, SW_AHCI_PHYCS1R_OFFSET, tmp);

	tmp = ahci_readl(base, SW_AHCI_PHYCS1R_OFFSET);
	tmp |= (0x1<<28);
	tmp |= (0x1<<15);
	ahci_writel(base, SW_AHCI_PHYCS1R_OFFSET, tmp);

	tmp = ahci_readl(base, SW_AHCI_PHYCS1R_OFFSET);
	tmp &= ~(0x1<<19);
	ahci_writel(base, SW_AHCI_PHYCS1R_OFFSET, tmp);

	tmp = ahci_readl(base, SW_AHCI_PHYCS0R_OFFSET);
	tmp &= ~(0x7<<20);
	tmp |= (0x03<<20);
	ahci_writel(base, SW_AHCI_PHYCS0R_OFFSET, tmp);

	tmp = ahci_readl(base, SW_AHCI_PHYCS2R_OFFSET);
	tmp &= ~(0x1f<<5);
	tmp |= (0x19<<5);
	ahci_writel(base, SW_AHCI_PHYCS2R_OFFSET, tmp);

	for(tmp=0; tmp<0x1000; tmp++);

	tmp = ahci_readl(base, SW_AHCI_PHYCS0R_OFFSET);
	tmp |= 0x1<<19;
	ahci_writel(base, SW_AHCI_PHYCS0R_OFFSET, tmp);

	timeout = timeout_val;
	do{
		tmp = ahci_readl(base, SW_AHCI_PHYCS0R_OFFSET);
		timeout --;
		if(!timeout) break;
	}while((tmp&(0x7<<28))!=(0x02<<28));

	if(!timeout)
	{
		printk("SATA AHCI Phy Power Failed!!\n");
	}

	tmp = ahci_readl(base, SW_AHCI_PHYCS2R_OFFSET);
	tmp |= 0x1<<24;
	ahci_writel(base, SW_AHCI_PHYCS2R_OFFSET, tmp);

	timeout = timeout_val;
	do{
		tmp = ahci_readl(base, SW_AHCI_PHYCS2R_OFFSET);
		timeout --;
		if(!timeout) break;
	}while(tmp&(0x1<<24));

	if(!timeout)
	{
		printk("SATA AHCI Phy Calibration Failed!!\n");
	}

	for(tmp=0; tmp<0x3000; tmp++);

	SW_AHCI_ACCESS_LOCK(base, 0x07);

	return 0;
}

static int sw_ahci_start(struct device *dev, void __iomem *addr)
{
	struct clk *hclk;
	struct clk *mclk;
	u32 pio_hdle = 0;
	int rc = 0;

	/*Enable mclk and hclk for AHCI*/
	mclk = clk_get(dev, sw_ahci_mclk_name);
	if (IS_ERR(mclk))
    {
    	dev_err(dev, "Error to get module clk for AHCI\n");
    	rc = -EINVAL;
    	goto err2;
    }

	hclk = clk_get(dev, sw_ahci_hclk_name);
	if (IS_ERR(hclk))
	{
		dev_err(dev, "Error to get ahb clk for AHCI\n");
    	rc = -EINVAL;
    	goto err1;
	}

	/*Enable SATA Clock in SATA PLL*/
	ahci_writel(CCMU_PLL6_VBASE, 0, ahci_readl(CCMU_PLL6_VBASE, 0)|(0x1<<14));
	clk_enable(mclk);
	clk_enable(hclk);

	sw_ahci_phy_init((unsigned int)addr);

	pio_hdle = gpio_request_ex(sw_ahci_para_name, NULL);
	if(pio_hdle)
	{
		gpio_write_one_pin_value(pio_hdle, 1, sw_ahci_gpio_name);
		gpio_release(pio_hdle, 2);
	}

	clk_put(hclk);
err1:
	clk_put(mclk);
err2:
	return rc;
}

static void sw_ahci_stop(struct device *dev)
{
	struct clk *hclk;
	struct clk *mclk;
	u32 pio_hdle = 0;
	int rc = 0;

	mclk = clk_get(dev, sw_ahci_mclk_name);
	if (IS_ERR(mclk))
    {
    	dev_err(dev, "Error to get module clk for AHCI\n");
    	rc = -EINVAL;
    	goto err2;
    }

	hclk = clk_get(dev, sw_ahci_hclk_name);
	if (IS_ERR(hclk))
	{
		dev_err(dev, "Error to get ahb clk for AHCI\n");
    	rc = -EINVAL;
    	goto err1;
	}

	pio_hdle = gpio_request_ex(sw_ahci_para_name, NULL);
	if(pio_hdle)
	{
		gpio_write_one_pin_value(pio_hdle, 0, sw_ahci_gpio_name);
		gpio_release(pio_hdle, 2);
	}

	/*Disable mclk and hclk for AHCI*/
	clk_disable(mclk);
	clk_disable(hclk);
	clk_put(hclk);
err1:
	clk_put(mclk);
err2:
	return;// rc;
}

static struct ata_port_info sw_ahci_port_info = {
	.flags = AHCI_FLAG_COMMON,
	//.link_flags = ,
	.pio_mask = ATA_PIO4,
	//.mwdma_mask = ,
	.udma_mask = ATA_UDMA6,
	.port_ops = &ahci_ops,
	.private_data = (void*)(AHCI_HFLAG_32BIT_ONLY | AHCI_HFLAG_NO_MSI
							| AHCI_HFLAG_NO_PMP | AHCI_HFLAG_YES_NCQ),
};

static struct ahci_platform_data sw_ahci_platform_data = {
	.ata_port_info = &sw_ahci_port_info,
	.init = sw_ahci_start,
	.exit = sw_ahci_stop,
};

void sw_ahci_release(struct device *dev)
{
	/* FILL ME! */
}

static struct platform_device sw_ahci_device = {
	.name		= "sw_ahci",
	.id			= 0,
	.dev 		= {
		.platform_data = &sw_ahci_platform_data,
		.release = &sw_ahci_release,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},

	.num_resources	= ARRAY_SIZE(sw_ahci_resources),
	.resource		= sw_ahci_resources,
};

static int __init sw_ahci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ahci_platform_data *pdata = dev->platform_data;
	struct ata_port_info pi = {
		.flags		= AHCI_FLAG_COMMON,
		.pio_mask	= ATA_PIO4,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &ahci_ops,
	};
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct ahci_host_priv *hpriv;
	struct ata_host *host;
	struct resource *mem;
	int irq;
	int n_ports;
	int i;
	int rc;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(dev, "no mmio space\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(dev, "no irq\n");
		return -EINVAL;
	}

	if (pdata && pdata->ata_port_info)
		pi = *pdata->ata_port_info;

	hpriv = devm_kzalloc(dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv) {
		dev_err(dev, "can't alloc ahci_host_priv\n");
		return -ENOMEM;
	}

	hpriv->flags |= (unsigned long)pi.private_data;

	hpriv->mmio = devm_ioremap(dev, mem->start, resource_size(mem));
	if (!hpriv->mmio) {
		dev_err(dev, "can't map %pR\n", mem);
		return -ENOMEM;
	}

	/*
	 * Some platforms might need to prepare for mmio region access,
	 * which could be done in the following init call. So, the mmio
	 * region shouldn't be accessed before init (if provided) has
	 * returned successfully.
	 */
	if (pdata && pdata->init) {
		rc = pdata->init(dev, hpriv->mmio);
		if (rc)
			return rc;
	}

	ahci_save_initial_config(dev, hpriv,
		pdata ? pdata->force_port_map : 0,
		pdata ? pdata->mask_port_map  : 0);

	/* prepare host */
	if (hpriv->cap & HOST_CAP_NCQ)
		pi.flags |= ATA_FLAG_NCQ;

	if (hpriv->cap & HOST_CAP_PMP)
		pi.flags |= ATA_FLAG_PMP;

	ahci_set_em_messages(hpriv, &pi);

	/* CAP.NP sometimes indicate the index of the last enabled
	 * port, at other times, that of the last possible port, so
	 * determining the maximum port number requires looking at
	 * both CAP.NP and port_map.
	 */
	n_ports = max(ahci_nr_ports(hpriv->cap), fls(hpriv->port_map));

	host = ata_host_alloc_pinfo(dev, ppi, n_ports);
	if (!host) {
		rc = -ENOMEM;
		goto err0;
	}

	host->private_data = hpriv;

	if (!(hpriv->cap & HOST_CAP_SSS) || ahci_ignore_sss)
		host->flags |= ATA_HOST_PARALLEL_SCAN;
	else
		printk(KERN_INFO "ahci: SSS flag set, parallel bus scan disabled\n");

	if (pi.flags & ATA_FLAG_EM)
		ahci_reset_em(host);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		ata_port_desc(ap, "mmio %pR", mem);
		ata_port_desc(ap, "port 0x%x", 0x100 + ap->port_no * 0x80);

		/* set enclosure management message type */
		if (ap->flags & ATA_FLAG_EM)
			ap->em_message_type = hpriv->em_msg_type;

		/* disabled/not-implemented port */
		if (!(hpriv->port_map & (1 << i)))
			ap->ops = &ata_dummy_port_ops;
	}

	rc = ahci_reset_controller(host);
	if (rc)
		goto err0;

	ahci_init_controller(host);
	ahci_print_info(host, "platform");

	rc = ata_host_activate(host, irq, ahci_interrupt, IRQF_SHARED,
			       &ahci_platform_sht);
	if (rc)
		goto err0;

	return 0;
err0:
	if (pdata && pdata->exit)
		pdata->exit(dev);
	return rc;
}

static int __devexit sw_ahci_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ahci_platform_data *pdata = dev->platform_data;
	struct ata_host *host = dev_get_drvdata(dev);

	ata_host_detach(host);

	if (pdata && pdata->exit)
		pdata->exit(dev);

	return 0;
}


void sw_ahci_dump_reg(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	u32 base = (u32)hpriv->mmio;
	int i = 0;

	for(i=0; i<0x200; i+=0x10) {
		printk("0x%3x = 0x%x, 0x%3x = 0x%x, 0x%3x = 0x%x, 0x%3x = 0x%x\n", i, ahci_readl(base, i), i+4, ahci_readl(base, i+4), i+8, ahci_readl(base, i+8), i+12, ahci_readl(base, i+12));
	}
}

#ifdef CONFIG_PM

static int sw_ahci_suspend(struct device *dev)
{

	printk("sw_ahci_platform: sw_ahci_suspend\n"); //danielwang
	//sw_ahci_dump_reg(dev);

	sw_ahci_stop(dev);

	return 0;
}

extern int ahci_hardware_recover_for_controller_resume(struct ata_host *host);
static int sw_ahci_resume(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;

	printk("sw_ahci_platform: sw_ahci_resume\n"); //danielwang

	sw_ahci_start(dev, hpriv->mmio);
	//sw_ahci_dump_reg(dev);

	ahci_hardware_recover_for_controller_resume(host);

	return 0;
}


static const struct dev_pm_ops  sw_ahci_pmops = {
	.suspend	= sw_ahci_suspend,
	.resume		= sw_ahci_resume,
};

#define SW_AHCI_PMOPS &sw_ahci_pmops
#else
#define SW_AHCI_PMOPS NULL
#endif



static struct platform_driver sw_ahci_driver = {
	.remove = __devexit_p(sw_ahci_remove),
	.driver = {
		.name = "sw_ahci",
		.owner = THIS_MODULE,
		.pm = SW_AHCI_PMOPS,
	},
};

static int __init sw_ahci_init(void)
{
	int rc, ctrl = 0;
	script_parser_fetch(sw_ahci_para_name,
			sw_ahci_used_name, &ctrl, sizeof(int));
	if (!ctrl) {
		pr_warn("AHCI is disabled in script.bin\n");
		return -ENODEV;
	}

	rc = platform_device_register(&sw_ahci_device);
	if (rc)
		return rc;

	rc = platform_driver_probe(&sw_ahci_driver, sw_ahci_probe);
	if (rc)
		platform_device_unregister(&sw_ahci_device);

	return rc;
}
module_init(sw_ahci_init);

static void __exit sw_ahci_exit(void)
{
	platform_driver_unregister(&sw_ahci_driver);
	platform_device_unregister(&sw_ahci_device);
}
module_exit(sw_ahci_exit);

MODULE_DESCRIPTION("SW AHCI SATA platform driver");
MODULE_AUTHOR("Daniel Wang <danielwang@allwinnertech.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sw_ahci");
