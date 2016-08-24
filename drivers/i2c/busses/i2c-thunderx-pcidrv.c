/*
 * Cavium ThunderX i2c driver.
 *
 * Copyright (C) 2015,2016 Cavium Inc.
 * Authors: Fred Martin <fmartin@caviumnetworks.com>
 *	    Jan Glauber <jglauber@cavium.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "i2c-octeon-core.h"

#define DRV_NAME "i2c-thunderx"

#define PCI_DEVICE_ID_THUNDER_TWSI	0xa012

#define SYS_FREQ_DEFAULT		700000000

#define TWSI_INT_ENA_W1C		0x1028
#define TWSI_INT_ENA_W1S		0x1030

/*
 * Enable the CORE interrupt.
 * The interrupt will be asserted when there is non-STAT_IDLE state in the
 * SW_TWSI_EOP_TWSI_STAT register.
 */
static void thunder_i2c_int_enable(struct octeon_i2c *i2c)
{
	octeon_i2c_writeq_flush(TWSI_INT_CORE_INT,
				i2c->twsi_base + TWSI_INT_ENA_W1S);
}

/*
 * Disable the CORE interrupt.
 */
static void thunder_i2c_int_disable(struct octeon_i2c *i2c)
{
	octeon_i2c_writeq_flush(TWSI_INT_CORE_INT,
				i2c->twsi_base + TWSI_INT_ENA_W1C);
}

static void thunder_i2c_hlc_int_enable(struct octeon_i2c *i2c)
{
	octeon_i2c_writeq_flush(TWSI_INT_ST_INT | TWSI_INT_TS_INT,
				i2c->twsi_base + TWSI_INT_ENA_W1S);
}

static void thunder_i2c_hlc_int_disable(struct octeon_i2c *i2c)
{
	octeon_i2c_writeq_flush(TWSI_INT_ST_INT | TWSI_INT_TS_INT,
				i2c->twsi_base + TWSI_INT_ENA_W1C);
}

static u32 thunderx_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK) |
	       I2C_FUNC_SMBUS_READ_BLOCK_DATA | I2C_SMBUS_BLOCK_PROC_CALL;
}

static const struct i2c_algorithm thunderx_i2c_algo = {
	.master_xfer = octeon_i2c_xfer,
	.functionality = thunderx_i2c_functionality,
};

static struct i2c_adapter thunderx_i2c_ops = {
	.owner	= THIS_MODULE,
	.name	= "ThunderX adapter",
	.algo	= &thunderx_i2c_algo,
};

static void thunder_i2c_clock_enable(struct device *dev, struct octeon_i2c *i2c)
{
	int ret;

	i2c->clk = clk_get(dev, NULL);
	if (IS_ERR(i2c->clk)) {
		i2c->clk = NULL;
		goto skip;
	}

	ret = clk_prepare_enable(i2c->clk);
	if (ret)
		goto skip;
	i2c->sys_freq = clk_get_rate(i2c->clk);

skip:
	if (!i2c->sys_freq)
		i2c->sys_freq = SYS_FREQ_DEFAULT;
}

static void thunder_i2c_clock_disable(struct device *dev, struct clk *clk)
{
	if (!clk)
		return;
	clk_disable_unprepare(clk);
	clk_put(clk);
}

static int thunder_i2c_probe_pci(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct octeon_i2c *i2c;
	int ret;

	i2c = devm_kzalloc(dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c->dev = dev;
	pci_set_drvdata(pdev, i2c);
	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret)
		return ret;

	i2c->twsi_base = pcim_iomap(pdev, 0, pci_resource_len(pdev, 0));
	if (!i2c->twsi_base)
		return -EINVAL;

	thunder_i2c_clock_enable(dev, i2c);
	ret = device_property_read_u32(dev, "clock-frequency", &i2c->twsi_freq);
	if (ret)
		i2c->twsi_freq = 100000;

	init_waitqueue_head(&i2c->queue);

	i2c->int_enable = thunder_i2c_int_enable;
	i2c->int_disable = thunder_i2c_int_disable;
	i2c->hlc_int_enable = thunder_i2c_hlc_int_enable;
	i2c->hlc_int_disable = thunder_i2c_hlc_int_disable;

	ret = pci_enable_msix(pdev, &i2c->i2c_msix, 1);
	if (ret)
		goto error;

	ret = devm_request_irq(dev, i2c->i2c_msix.vector, octeon_i2c_isr, 0,
			       DRV_NAME, i2c);
	if (ret)
		goto error;

	ret = octeon_i2c_init_lowlevel(i2c);
	if (ret)
		goto error;

	octeon_i2c_set_clock(i2c);

	i2c->adap = thunderx_i2c_ops;
	i2c->adap.retries = 5;
	i2c->adap.bus_recovery_info = &octeon_i2c_recovery_info;
	i2c->adap.dev.parent = dev;
	i2c->adap.dev.of_node = pdev->dev.of_node;
	snprintf(i2c->adap.name, sizeof(i2c->adap.name),
		 "Cavium ThunderX i2c adapter at %s", dev_name(dev));
	i2c_set_adapdata(&i2c->adap, i2c);

	ret = i2c_add_adapter(&i2c->adap);
	if (ret)
		goto error;

	dev_info(i2c->dev, "Probed. Set system clock to %u\n", i2c->sys_freq);
	return 0;

error:
	thunder_i2c_clock_disable(dev, i2c->clk);
	return ret;
}

static void thunder_i2c_remove_pci(struct pci_dev *pdev)
{
	struct octeon_i2c *i2c = pci_get_drvdata(pdev);

	thunder_i2c_clock_disable(&pdev->dev, i2c->clk);
	i2c_del_adapter(&i2c->adap);
}

static const struct pci_device_id thunder_i2c_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_THUNDER_TWSI) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, thunder_i2c_pci_id_table);

static struct pci_driver thunder_i2c_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= thunder_i2c_pci_id_table,
	.probe		= thunder_i2c_probe_pci,
	.remove		= thunder_i2c_remove_pci,
};

module_pci_driver(thunder_i2c_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fred Martin <fmartin@caviumnetworks.com>");
MODULE_DESCRIPTION("I2C-Bus adapter for Cavium ThunderX SOC");
