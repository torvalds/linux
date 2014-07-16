/*
 * PCI bus driver for Bosch C_CAN/D_CAN controller
 *
 * Copyright (C) 2012 Federico Vaga <federico.vaga@gmail.com>
 *
 * Borrowed from c_can_platform.c
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>

#include <linux/can/dev.h>

#include "c_can.h"

#define PCI_DEVICE_ID_PCH_CAN	0x8818
#define PCH_PCI_SOFT_RESET	0x01fc

enum c_can_pci_reg_align {
	C_CAN_REG_ALIGN_16,
	C_CAN_REG_ALIGN_32,
	C_CAN_REG_32,
};

struct c_can_pci_data {
	/* Specify if is C_CAN or D_CAN */
	enum c_can_dev_id type;
	/* Set the register alignment in the memory */
	enum c_can_pci_reg_align reg_align;
	/* Set the frequency */
	unsigned int freq;
	/* PCI bar number */
	int bar;
	/* Callback for reset */
	void (*init)(const struct c_can_priv *priv, bool enable);
};

/*
 * 16-bit c_can registers can be arranged differently in the memory
 * architecture of different implementations. For example: 16-bit
 * registers can be aligned to a 16-bit boundary or 32-bit boundary etc.
 * Handle the same by providing a common read/write interface.
 */
static u16 c_can_pci_read_reg_aligned_to_16bit(const struct c_can_priv *priv,
						enum reg index)
{
	return readw(priv->base + priv->regs[index]);
}

static void c_can_pci_write_reg_aligned_to_16bit(const struct c_can_priv *priv,
						enum reg index, u16 val)
{
	writew(val, priv->base + priv->regs[index]);
}

static u16 c_can_pci_read_reg_aligned_to_32bit(const struct c_can_priv *priv,
						enum reg index)
{
	return readw(priv->base + 2 * priv->regs[index]);
}

static void c_can_pci_write_reg_aligned_to_32bit(const struct c_can_priv *priv,
						enum reg index, u16 val)
{
	writew(val, priv->base + 2 * priv->regs[index]);
}

static u16 c_can_pci_read_reg_32bit(const struct c_can_priv *priv,
				    enum reg index)
{
	return (u16)ioread32(priv->base + 2 * priv->regs[index]);
}

static void c_can_pci_write_reg_32bit(const struct c_can_priv *priv,
				      enum reg index, u16 val)
{
	iowrite32((u32)val, priv->base + 2 * priv->regs[index]);
}

static u32 c_can_pci_read_reg32(const struct c_can_priv *priv, enum reg index)
{
	u32 val;

	val = priv->read_reg(priv, index);
	val |= ((u32) priv->read_reg(priv, index + 1)) << 16;

	return val;
}

static void c_can_pci_write_reg32(const struct c_can_priv *priv, enum reg index,
		u32 val)
{
	priv->write_reg(priv, index + 1, val >> 16);
	priv->write_reg(priv, index, val);
}

static void c_can_pci_reset_pch(const struct c_can_priv *priv, bool enable)
{
	if (enable) {
		u32 __iomem *addr = priv->base + PCH_PCI_SOFT_RESET;

		/* write to sw reset register */
		iowrite32(1, addr);
		iowrite32(0, addr);
	}
}

static int c_can_pci_probe(struct pci_dev *pdev,
			   const struct pci_device_id *ent)
{
	struct c_can_pci_data *c_can_pci_data = (void *)ent->driver_data;
	struct c_can_priv *priv;
	struct net_device *dev;
	void __iomem *addr;
	int ret;

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device FAILED\n");
		goto out;
	}

	ret = pci_request_regions(pdev, KBUILD_MODNAME);
	if (ret) {
		dev_err(&pdev->dev, "pci_request_regions FAILED\n");
		goto out_disable_device;
	}

	ret = pci_enable_msi(pdev);
	if (!ret) {
		dev_info(&pdev->dev, "MSI enabled\n");
		pci_set_master(pdev);
	}

	addr = pci_iomap(pdev, c_can_pci_data->bar,
			 pci_resource_len(pdev, c_can_pci_data->bar));
	if (!addr) {
		dev_err(&pdev->dev,
			"device has no PCI memory resources, "
			"failing adapter\n");
		ret = -ENOMEM;
		goto out_release_regions;
	}

	/* allocate the c_can device */
	dev = alloc_c_can_dev();
	if (!dev) {
		ret = -ENOMEM;
		goto out_iounmap;
	}

	priv = netdev_priv(dev);
	pci_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	dev->irq = pdev->irq;
	priv->base = addr;

	if (!c_can_pci_data->freq) {
		dev_err(&pdev->dev, "no clock frequency defined\n");
		ret = -ENODEV;
		goto out_free_c_can;
	} else {
		priv->can.clock.freq = c_can_pci_data->freq;
	}

	/* Configure CAN type */
	switch (c_can_pci_data->type) {
	case BOSCH_C_CAN:
		priv->regs = reg_map_c_can;
		break;
	case BOSCH_D_CAN:
		priv->regs = reg_map_d_can;
		priv->can.ctrlmode_supported |= CAN_CTRLMODE_3_SAMPLES;
		break;
	default:
		ret = -EINVAL;
		goto out_free_c_can;
	}

	priv->type = c_can_pci_data->type;

	/* Configure access to registers */
	switch (c_can_pci_data->reg_align) {
	case C_CAN_REG_ALIGN_32:
		priv->read_reg = c_can_pci_read_reg_aligned_to_32bit;
		priv->write_reg = c_can_pci_write_reg_aligned_to_32bit;
		break;
	case C_CAN_REG_ALIGN_16:
		priv->read_reg = c_can_pci_read_reg_aligned_to_16bit;
		priv->write_reg = c_can_pci_write_reg_aligned_to_16bit;
		break;
	case C_CAN_REG_32:
		priv->read_reg = c_can_pci_read_reg_32bit;
		priv->write_reg = c_can_pci_write_reg_32bit;
		break;
	default:
		ret = -EINVAL;
		goto out_free_c_can;
	}
	priv->read_reg32 = c_can_pci_read_reg32;
	priv->write_reg32 = c_can_pci_write_reg32;

	priv->raminit = c_can_pci_data->init;

	ret = register_c_can_dev(dev);
	if (ret) {
		dev_err(&pdev->dev, "registering %s failed (err=%d)\n",
			KBUILD_MODNAME, ret);
		goto out_free_c_can;
	}

	dev_dbg(&pdev->dev, "%s device registered (regs=%p, irq=%d)\n",
		 KBUILD_MODNAME, priv->regs, dev->irq);

	return 0;

out_free_c_can:
	free_c_can_dev(dev);
out_iounmap:
	pci_iounmap(pdev, addr);
out_release_regions:
	pci_disable_msi(pdev);
	pci_clear_master(pdev);
	pci_release_regions(pdev);
out_disable_device:
	pci_disable_device(pdev);
out:
	return ret;
}

static void c_can_pci_remove(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct c_can_priv *priv = netdev_priv(dev);

	unregister_c_can_dev(dev);

	free_c_can_dev(dev);

	pci_iounmap(pdev, priv->base);
	pci_disable_msi(pdev);
	pci_clear_master(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct c_can_pci_data c_can_sta2x11= {
	.type = BOSCH_C_CAN,
	.reg_align = C_CAN_REG_ALIGN_32,
	.freq = 52000000, /* 52 Mhz */
	.bar = 0,
};

static struct c_can_pci_data c_can_pch = {
	.type = BOSCH_C_CAN,
	.reg_align = C_CAN_REG_32,
	.freq = 50000000, /* 50 MHz */
	.init = c_can_pci_reset_pch,
	.bar = 1,
};

#define C_CAN_ID(_vend, _dev, _driverdata) {		\
	PCI_DEVICE(_vend, _dev),			\
	.driver_data = (unsigned long)&_driverdata,	\
}
static DEFINE_PCI_DEVICE_TABLE(c_can_pci_tbl) = {
	C_CAN_ID(PCI_VENDOR_ID_STMICRO, PCI_DEVICE_ID_STMICRO_CAN,
		 c_can_sta2x11),
	C_CAN_ID(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PCH_CAN,
		 c_can_pch),
	{},
};
static struct pci_driver c_can_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = c_can_pci_tbl,
	.probe = c_can_pci_probe,
	.remove = c_can_pci_remove,
};

module_pci_driver(c_can_pci_driver);

MODULE_AUTHOR("Federico Vaga <federico.vaga@gmail.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PCI CAN bus driver for Bosch C_CAN/D_CAN controller");
MODULE_DEVICE_TABLE(pci, c_can_pci_tbl);
