// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) KEBA Industrial Automation Gmbh 2024
 *
 * Driver for KEBA system FPGA
 *
 * The KEBA system FPGA implements various devices. This driver registers
 * auxiliary devices for every device within the FPGA.
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/misc/keba.h>
#include <linux/module.h>
#include <linux/pci.h>

#define CP500 "cp500"

#define PCI_VENDOR_ID_KEBA		0xCEBA
#define PCI_DEVICE_ID_KEBA_CP035	0x2706
#define PCI_DEVICE_ID_KEBA_CP505	0x2703
#define PCI_DEVICE_ID_KEBA_CP520	0x2696

#define CP500_SYS_BAR		0
#define CP500_ECM_BAR		1

/* BAR 0 registers */
#define CP500_VERSION_REG	0x00
#define CP500_RECONFIG_REG	0x11	/* upper 8-bits of STARTUP register */
#define CP500_AXI_REG		0x40

/* Bits in BUILD_REG */
#define CP500_BUILD_TEST        0x8000	/* FPGA test version */

/* Bits in RECONFIG_REG */
#define CP500_RECFG_REQ		0x01	/* reconfigure FPGA on next reset */

/* MSIX */
#define CP500_AXI_MSIX		3
#define CP500_NUM_MSIX		8
#define CP500_NUM_MSIX_NO_MMI	2
#define CP500_NUM_MSIX_NO_AXI	3

/* EEPROM */
#define CP500_HW_CPU_EEPROM_NAME	"cp500_cpu_eeprom"

#define CP500_IS_CP035(dev)	((dev)->pci_dev->device == PCI_DEVICE_ID_KEBA_CP035)
#define CP500_IS_CP505(dev)	((dev)->pci_dev->device == PCI_DEVICE_ID_KEBA_CP505)
#define CP500_IS_CP520(dev)	((dev)->pci_dev->device == PCI_DEVICE_ID_KEBA_CP520)

struct cp500_dev_info {
	off_t offset;
	size_t size;
};

struct cp500_devs {
	struct cp500_dev_info startup;
	struct cp500_dev_info i2c;
};

/* list of devices within FPGA of CP035 family (CP035, CP056, CP057) */
static struct cp500_devs cp035_devices = {
	.startup   = { 0x0000, SZ_4K },
	.i2c       = { 0x4000, SZ_4K },
};

/* list of devices within FPGA of CP505 family (CP503, CP505, CP507) */
static struct cp500_devs cp505_devices = {
	.startup   = { 0x0000, SZ_4K },
	.i2c       = { 0x5000, SZ_4K },
};

/* list of devices within FPGA of CP520 family (CP520, CP530) */
static struct cp500_devs cp520_devices = {
	.startup     = { 0x0000, SZ_4K },
	.i2c         = { 0x5000, SZ_4K },
};

struct cp500 {
	struct pci_dev *pci_dev;
	struct cp500_devs *devs;
	int msix_num;
	struct {
		int major;
		int minor;
		int build;
	} version;

	/* system FPGA BAR */
	resource_size_t sys_hwbase;
	struct keba_i2c_auxdev *i2c;

	/* ECM EtherCAT BAR */
	resource_size_t ecm_hwbase;

	void __iomem *system_startup_addr;
};

/* I2C devices */
static struct i2c_board_info cp500_i2c_info[] = {
	{	/* temperature sensor */
		I2C_BOARD_INFO("emc1403", 0x4c),
	},
	{	/*
		 * CPU EEPROM
		 * CP035 family: CPU board
		 * CP505 family: bridge board
		 * CP520 family: carrier board
		 */
		I2C_BOARD_INFO("24c32", 0x50),
		.dev_name = CP500_HW_CPU_EEPROM_NAME,
	},
	{	/* interface board EEPROM */
		I2C_BOARD_INFO("24c32", 0x51),
	},
	{	/*
		 * EEPROM (optional)
		 * CP505 family: CPU board
		 * CP520 family: MMI board
		 */
		I2C_BOARD_INFO("24c32", 0x52),
	},
	{	/* extension module 0 EEPROM (optional) */
		I2C_BOARD_INFO("24c32", 0x53),
	},
	{	/* extension module 1 EEPROM (optional) */
		I2C_BOARD_INFO("24c32", 0x54),
	},
	{	/* extension module 2 EEPROM (optional) */
		I2C_BOARD_INFO("24c32", 0x55),
	},
	{	/* extension module 3 EEPROM (optional) */
		I2C_BOARD_INFO("24c32", 0x56),
	}
};

static ssize_t cp500_get_fpga_version(struct cp500 *cp500, char *buf,
				      size_t max_len)
{
	int n;

	if (CP500_IS_CP035(cp500))
		n = scnprintf(buf, max_len, "CP035");
	else if (CP500_IS_CP505(cp500))
		n = scnprintf(buf, max_len, "CP505");
	else
		n = scnprintf(buf, max_len, "CP500");

	n += scnprintf(buf + n, max_len - n, "_FPGA_%d.%02d",
		       cp500->version.major, cp500->version.minor);

	/* test versions have test bit set */
	if (cp500->version.build & CP500_BUILD_TEST)
		n += scnprintf(buf + n, max_len - n, "Test%d",
			       cp500->version.build & ~CP500_BUILD_TEST);

	n += scnprintf(buf + n, max_len - n, "\n");

	return n;
}

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct cp500 *cp500 = dev_get_drvdata(dev);

	return cp500_get_fpga_version(cp500, buf, PAGE_SIZE);
}
static DEVICE_ATTR_RO(version);

static ssize_t keep_cfg_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct cp500 *cp500 = dev_get_drvdata(dev);
	unsigned long keep_cfg = 1;

	/*
	 * FPGA configuration stream is kept during reset when RECONFIG bit is
	 * zero
	 */
	if (ioread8(cp500->system_startup_addr + CP500_RECONFIG_REG) &
		CP500_RECFG_REQ)
		keep_cfg = 0;

	return sysfs_emit(buf, "%lu\n", keep_cfg);
}

static ssize_t keep_cfg_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct cp500 *cp500 = dev_get_drvdata(dev);
	unsigned long keep_cfg;

	if (kstrtoul(buf, 10, &keep_cfg) < 0)
		return -EINVAL;

	/*
	 * In normal operation "keep_cfg" is "1". This means that the FPGA keeps
	 * its configuration stream during a reset.
	 * In case of a firmware update of the FPGA, the configuration stream
	 * needs to be reloaded. This can be done without a powercycle by
	 * writing a "0" into the "keep_cfg" attribute. After a reset/reboot th
	 * new configuration stream will be loaded.
	 */
	if (keep_cfg)
		iowrite8(0, cp500->system_startup_addr + CP500_RECONFIG_REG);
	else
		iowrite8(CP500_RECFG_REQ,
			 cp500->system_startup_addr + CP500_RECONFIG_REG);

	return count;
}
static DEVICE_ATTR_RW(keep_cfg);

static struct attribute *cp500_attrs[] = {
	&dev_attr_version.attr,
	&dev_attr_keep_cfg.attr,
	NULL
};
ATTRIBUTE_GROUPS(cp500);

static void cp500_i2c_release(struct device *dev)
{
	struct keba_i2c_auxdev *i2c =
		container_of(dev, struct keba_i2c_auxdev, auxdev.dev);

	kfree(i2c);
}

static int cp500_register_i2c(struct cp500 *cp500)
{
	int retval;

	cp500->i2c = kzalloc(sizeof(*cp500->i2c), GFP_KERNEL);
	if (!cp500->i2c)
		return -ENOMEM;

	cp500->i2c->auxdev.name = "i2c";
	cp500->i2c->auxdev.id = 0;
	cp500->i2c->auxdev.dev.release = cp500_i2c_release;
	cp500->i2c->auxdev.dev.parent = &cp500->pci_dev->dev;
	cp500->i2c->io = (struct resource) {
		 /* I2C register area */
		 .start = (resource_size_t) cp500->sys_hwbase +
			  cp500->devs->i2c.offset,
		 .end   = (resource_size_t) cp500->sys_hwbase +
			  cp500->devs->i2c.offset +
			  cp500->devs->i2c.size - 1,
		 .flags = IORESOURCE_MEM,
	};
	cp500->i2c->info_size = ARRAY_SIZE(cp500_i2c_info);
	cp500->i2c->info = cp500_i2c_info;

	retval = auxiliary_device_init(&cp500->i2c->auxdev);
	if (retval) {
		kfree(cp500->i2c);
		cp500->i2c = NULL;

		return retval;
	}
	retval = __auxiliary_device_add(&cp500->i2c->auxdev, "keba");
	if (retval) {
		auxiliary_device_uninit(&cp500->i2c->auxdev);
		cp500->i2c = NULL;

		return retval;
	}

	return 0;
}

static void cp500_register_auxiliary_devs(struct cp500 *cp500)
{
	struct device *dev = &cp500->pci_dev->dev;

	if (cp500_register_i2c(cp500))
		dev_warn(dev, "Failed to register i2c!\n");
}

static void cp500_unregister_dev(struct auxiliary_device *auxdev)
{
	auxiliary_device_delete(auxdev);
	auxiliary_device_uninit(auxdev);
}

static void cp500_unregister_auxiliary_devs(struct cp500 *cp500)
{

	if (cp500->i2c) {
		cp500_unregister_dev(&cp500->i2c->auxdev);
		cp500->i2c = NULL;
	}
}

static irqreturn_t cp500_axi_handler(int irq, void *dev)
{
	struct cp500 *cp500 = dev;
	u32 axi_address = ioread32(cp500->system_startup_addr + CP500_AXI_REG);

	/*
	 * FPGA signals AXI response error, print AXI address to indicate which
	 * IP core was affected
	 */
	dev_err(&cp500->pci_dev->dev, "AXI response error at 0x%08x\n",
		axi_address);

	return IRQ_HANDLED;
}

static int cp500_enable(struct cp500 *cp500)
{
	int axi_irq = -1;
	int ret;

	if (cp500->msix_num > CP500_NUM_MSIX_NO_AXI) {
		axi_irq = pci_irq_vector(cp500->pci_dev, CP500_AXI_MSIX);
		ret = request_irq(axi_irq, cp500_axi_handler, 0,
				  CP500, cp500);
		if (ret != 0) {
			dev_err(&cp500->pci_dev->dev,
				"Failed to register AXI response error!\n");
			return ret;
		}
	}

	return 0;
}

static void cp500_disable(struct cp500 *cp500)
{
	int axi_irq;

	if (cp500->msix_num > CP500_NUM_MSIX_NO_AXI) {
		axi_irq = pci_irq_vector(cp500->pci_dev, CP500_AXI_MSIX);
		free_irq(axi_irq, cp500);
	}
}

static int cp500_probe(struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	struct device *dev = &pci_dev->dev;
	struct resource startup;
	struct cp500 *cp500;
	u32 cp500_vers;
	char buf[64];
	int ret;

	cp500 = devm_kzalloc(dev, sizeof(*cp500), GFP_KERNEL);
	if (!cp500)
		return -ENOMEM;
	cp500->pci_dev = pci_dev;
	cp500->sys_hwbase = pci_resource_start(pci_dev, CP500_SYS_BAR);
	cp500->ecm_hwbase = pci_resource_start(pci_dev, CP500_ECM_BAR);
	if (!cp500->sys_hwbase || !cp500->ecm_hwbase)
		return -ENODEV;

	if (CP500_IS_CP035(cp500))
		cp500->devs = &cp035_devices;
	else if (CP500_IS_CP505(cp500))
		cp500->devs = &cp505_devices;
	else if (CP500_IS_CP520(cp500))
		cp500->devs = &cp520_devices;
	else
		return -ENODEV;

	ret = pci_enable_device(pci_dev);
	if (ret)
		return ret;
	pci_set_master(pci_dev);

	startup = *pci_resource_n(pci_dev, CP500_SYS_BAR);
	startup.end = startup.start + cp500->devs->startup.size - 1;
	cp500->system_startup_addr = devm_ioremap_resource(&pci_dev->dev,
							   &startup);
	if (IS_ERR(cp500->system_startup_addr)) {
		ret = PTR_ERR(cp500->system_startup_addr);
		goto out_disable;
	}

	cp500->msix_num = pci_alloc_irq_vectors(pci_dev, CP500_NUM_MSIX_NO_MMI,
						CP500_NUM_MSIX, PCI_IRQ_MSIX);
	if (cp500->msix_num < CP500_NUM_MSIX_NO_MMI) {
		dev_err(&pci_dev->dev,
			"Hardware does not support enough MSI-X interrupts\n");
		ret = -ENODEV;
		goto out_disable;
	}

	cp500_vers = ioread32(cp500->system_startup_addr + CP500_VERSION_REG);
	cp500->version.major = (cp500_vers & 0xff);
	cp500->version.minor = (cp500_vers >> 8) & 0xff;
	cp500->version.build = (cp500_vers >> 16) & 0xffff;
	cp500_get_fpga_version(cp500, buf, sizeof(buf));

	dev_info(&pci_dev->dev, "FPGA version %s", buf);

	pci_set_drvdata(pci_dev, cp500);


	ret = cp500_enable(cp500);
	if (ret != 0)
		goto out_free_irq;

	cp500_register_auxiliary_devs(cp500);

	return 0;

out_free_irq:
	pci_free_irq_vectors(pci_dev);
out_disable:
	pci_clear_master(pci_dev);
	pci_disable_device(pci_dev);

	return ret;
}

static void cp500_remove(struct pci_dev *pci_dev)
{
	struct cp500 *cp500 = pci_get_drvdata(pci_dev);

	cp500_unregister_auxiliary_devs(cp500);

	cp500_disable(cp500);

	pci_set_drvdata(pci_dev, 0);

	pci_free_irq_vectors(pci_dev);

	pci_clear_master(pci_dev);
	pci_disable_device(pci_dev);
}

static struct pci_device_id cp500_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_KEBA, PCI_DEVICE_ID_KEBA_CP035) },
	{ PCI_DEVICE(PCI_VENDOR_ID_KEBA, PCI_DEVICE_ID_KEBA_CP505) },
	{ PCI_DEVICE(PCI_VENDOR_ID_KEBA, PCI_DEVICE_ID_KEBA_CP520) },
	{ }
};
MODULE_DEVICE_TABLE(pci, cp500_ids);

static struct pci_driver cp500_driver = {
	.name = CP500,
	.id_table = cp500_ids,
	.probe = cp500_probe,
	.remove = cp500_remove,
	.dev_groups = cp500_groups,
};
module_pci_driver(cp500_driver);

MODULE_AUTHOR("Gerhard Engleder <eg@keba.com>");
MODULE_DESCRIPTION("KEBA CP500 system FPGA driver");
MODULE_LICENSE("GPL");
