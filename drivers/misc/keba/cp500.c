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
#include <linux/mtd/partitions.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/pci.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>

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
#define CP500_PRESENT_REG	0x20
#define CP500_AXI_REG		0x40

/* Bits in BUILD_REG */
#define CP500_BUILD_TEST        0x8000	/* FPGA test version */

/* Bits in RECONFIG_REG */
#define CP500_RECFG_REQ		0x01	/* reconfigure FPGA on next reset */

/* Bits in PRESENT_REG */
#define CP500_PRESENT_FAN0	0x01

/* MSIX */
#define CP500_AXI_MSIX		3
#define CP500_RFB_UART_MSIX	4
#define CP500_DEBUG_UART_MSIX	5
#define CP500_SI1_UART_MSIX	6
#define CP500_NUM_MSIX		8
#define CP500_NUM_MSIX_NO_MMI	2
#define CP500_NUM_MSIX_NO_AXI	3

/* EEPROM */
#define CP500_EEPROM_DA_OFFSET		0x016F
#define CP500_EEPROM_DA_ESC_TYPE_MASK	0x01
#define CP500_EEPROM_ESC_LAN9252	0x00
#define CP500_EEPROM_ESC_ET1100		0x01
#define CP500_EEPROM_CPU_NAME		"cpu_eeprom"
#define CP500_EEPROM_CPU_OFFSET		0
#define CP500_EEPROM_CPU_SIZE		3072
#define CP500_EEPROM_USER_NAME		"user_eeprom"
#define CP500_EEPROM_USER_OFFSET	3072
#define CP500_EEPROM_USER_SIZE		1024

/* SPI flash running at full speed */
#define CP500_FLASH_HZ		(33 * 1000 * 1000)

/* LAN9252 */
#define CP500_LAN9252_HZ	(10 * 1000 * 1000)

#define CP500_IS_CP035(dev)	((dev)->pci_dev->device == PCI_DEVICE_ID_KEBA_CP035)
#define CP500_IS_CP505(dev)	((dev)->pci_dev->device == PCI_DEVICE_ID_KEBA_CP505)
#define CP500_IS_CP520(dev)	((dev)->pci_dev->device == PCI_DEVICE_ID_KEBA_CP520)

struct cp500_dev_info {
	off_t offset;
	size_t size;
	unsigned int msix;
};

struct cp500_devs {
	struct cp500_dev_info startup;
	struct cp500_dev_info spi;
	struct cp500_dev_info i2c;
	struct cp500_dev_info fan;
	struct cp500_dev_info batt;
	struct cp500_dev_info uart0_rfb;
	struct cp500_dev_info uart1_dbg;
	struct cp500_dev_info uart2_si1;
};

/* list of devices within FPGA of CP035 family (CP035, CP056, CP057) */
static struct cp500_devs cp035_devices = {
	.startup   = { 0x0000, SZ_4K },
	.spi       = { 0x1000, SZ_4K },
	.i2c       = { 0x4000, SZ_4K },
	.fan       = { 0x9000, SZ_4K },
	.batt      = { 0xA000, SZ_4K },
	.uart0_rfb = { 0xB000, SZ_4K, CP500_RFB_UART_MSIX },
	.uart2_si1 = { 0xD000, SZ_4K, CP500_SI1_UART_MSIX },
};

/* list of devices within FPGA of CP505 family (CP503, CP505, CP507) */
static struct cp500_devs cp505_devices = {
	.startup   = { 0x0000, SZ_4K },
	.spi       = { 0x4000, SZ_4K },
	.i2c       = { 0x5000, SZ_4K },
	.fan       = { 0x9000, SZ_4K },
	.batt      = { 0xA000, SZ_4K },
	.uart0_rfb = { 0xB000, SZ_4K, CP500_RFB_UART_MSIX },
	.uart2_si1 = { 0xD000, SZ_4K, CP500_SI1_UART_MSIX },
};

/* list of devices within FPGA of CP520 family (CP520, CP530) */
static struct cp500_devs cp520_devices = {
	.startup   = { 0x0000, SZ_4K },
	.spi       = { 0x4000, SZ_4K },
	.i2c       = { 0x5000, SZ_4K },
	.fan       = { 0x8000, SZ_4K },
	.batt      = { 0x9000, SZ_4K },
	.uart0_rfb = { 0xC000, SZ_4K, CP500_RFB_UART_MSIX },
	.uart1_dbg = { 0xD000, SZ_4K, CP500_DEBUG_UART_MSIX },
};

struct cp500_nvmem {
	struct nvmem_device *base_nvmem;
	unsigned int offset;
	struct nvmem_device *nvmem;
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
	struct notifier_block nvmem_notifier;
	atomic_t nvmem_notified;

	/* system FPGA BAR */
	resource_size_t sys_hwbase;
	struct keba_spi_auxdev *spi;
	struct keba_i2c_auxdev *i2c;
	struct keba_fan_auxdev *fan;
	struct keba_batt_auxdev *batt;
	struct keba_uart_auxdev *uart0_rfb;
	struct keba_uart_auxdev *uart1_dbg;
	struct keba_uart_auxdev *uart2_si1;

	/* ECM EtherCAT BAR */
	resource_size_t ecm_hwbase;

	/* NVMEM devices */
	struct cp500_nvmem nvmem_cpu;
	struct cp500_nvmem nvmem_user;

	void __iomem *system_startup_addr;
};

/* I2C devices */
#define CP500_EEPROM_ADDR	0x50
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
		I2C_BOARD_INFO("24c32", CP500_EEPROM_ADDR),
	},
	{	/* interface board EEPROM */
		I2C_BOARD_INFO("24c32", CP500_EEPROM_ADDR + 1),
	},
	{	/*
		 * EEPROM (optional)
		 * CP505 family: CPU board
		 * CP520 family: MMI board
		 */
		I2C_BOARD_INFO("24c32", CP500_EEPROM_ADDR + 2),
	},
	{	/* extension module 0 EEPROM (optional) */
		I2C_BOARD_INFO("24c32", CP500_EEPROM_ADDR + 3),
	},
	{	/* extension module 1 EEPROM (optional) */
		I2C_BOARD_INFO("24c32", CP500_EEPROM_ADDR + 4),
	},
	{	/* extension module 2 EEPROM (optional) */
		I2C_BOARD_INFO("24c32", CP500_EEPROM_ADDR + 5),
	},
	{	/* extension module 3 EEPROM (optional) */
		I2C_BOARD_INFO("24c32", CP500_EEPROM_ADDR + 6),
	}
};

/* SPI devices */
static struct mtd_partition cp500_partitions[] = {
	{
		.name       = "system-flash-parts",
		.size       = MTDPART_SIZ_FULL,
		.offset     = 0,
		.mask_flags = 0
	}
};
static const struct flash_platform_data cp500_w25q32 = {
	.type     = "w25q32",
	.name     = "system-flash",
	.parts    = cp500_partitions,
	.nr_parts = ARRAY_SIZE(cp500_partitions),
};
static const struct flash_platform_data cp500_m25p16 = {
	.type     = "m25p16",
	.name     = "system-flash",
	.parts    = cp500_partitions,
	.nr_parts = ARRAY_SIZE(cp500_partitions),
};
static struct spi_board_info cp500_spi_info[] = {
	{       /* system FPGA configuration bitstream flash */
		.modalias      = "m25p80",
		.platform_data = &cp500_m25p16,
		.max_speed_hz  = CP500_FLASH_HZ,
		.chip_select   = 0,
		.mode          = SPI_MODE_3,
	}, {    /* LAN9252 EtherCAT slave controller */
		.modalias      = "lan9252",
		.platform_data = NULL,
		.max_speed_hz  = CP500_LAN9252_HZ,
		.chip_select   = 1,
		.mode          = SPI_MODE_3,
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
	int ret;

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

	ret = auxiliary_device_init(&cp500->i2c->auxdev);
	if (ret) {
		kfree(cp500->i2c);
		cp500->i2c = NULL;

		return ret;
	}
	ret = __auxiliary_device_add(&cp500->i2c->auxdev, "keba");
	if (ret) {
		auxiliary_device_uninit(&cp500->i2c->auxdev);
		cp500->i2c = NULL;

		return ret;
	}

	return 0;
}

static void cp500_spi_release(struct device *dev)
{
	struct keba_spi_auxdev *spi =
		container_of(dev, struct keba_spi_auxdev, auxdev.dev);

	kfree(spi);
}

static int cp500_register_spi(struct cp500 *cp500, u8 esc_type)
{
	int info_size;
	int ret;

	cp500->spi = kzalloc(sizeof(*cp500->spi), GFP_KERNEL);
	if (!cp500->spi)
		return -ENOMEM;

	if (CP500_IS_CP035(cp500))
		cp500_spi_info[0].platform_data = &cp500_w25q32;
	if (esc_type == CP500_EEPROM_ESC_LAN9252)
		info_size = ARRAY_SIZE(cp500_spi_info);
	else
		info_size = ARRAY_SIZE(cp500_spi_info) - 1;

	cp500->spi->auxdev.name = "spi";
	cp500->spi->auxdev.id = 0;
	cp500->spi->auxdev.dev.release = cp500_spi_release;
	cp500->spi->auxdev.dev.parent = &cp500->pci_dev->dev;
	cp500->spi->io = (struct resource) {
		 /* SPI register area */
		 .start = (resource_size_t) cp500->sys_hwbase +
			  cp500->devs->spi.offset,
		 .end   = (resource_size_t) cp500->sys_hwbase +
			  cp500->devs->spi.offset +
			  cp500->devs->spi.size - 1,
		 .flags = IORESOURCE_MEM,
	};
	cp500->spi->info_size = info_size;
	cp500->spi->info = cp500_spi_info;

	ret = auxiliary_device_init(&cp500->spi->auxdev);
	if (ret) {
		kfree(cp500->spi);
		cp500->spi = NULL;

		return ret;
	}
	ret = __auxiliary_device_add(&cp500->spi->auxdev, "keba");
	if (ret) {
		auxiliary_device_uninit(&cp500->spi->auxdev);
		cp500->spi = NULL;

		return ret;
	}

	return 0;
}

static void cp500_fan_release(struct device *dev)
{
	struct keba_fan_auxdev *fan =
		container_of(dev, struct keba_fan_auxdev, auxdev.dev);

	kfree(fan);
}

static int cp500_register_fan(struct cp500 *cp500)
{
	int ret;

	cp500->fan = kzalloc(sizeof(*cp500->fan), GFP_KERNEL);
	if (!cp500->fan)
		return -ENOMEM;

	cp500->fan->auxdev.name = "fan";
	cp500->fan->auxdev.id = 0;
	cp500->fan->auxdev.dev.release = cp500_fan_release;
	cp500->fan->auxdev.dev.parent = &cp500->pci_dev->dev;
	cp500->fan->io = (struct resource) {
		 /* fan register area */
		 .start = (resource_size_t) cp500->sys_hwbase +
			  cp500->devs->fan.offset,
		 .end   = (resource_size_t) cp500->sys_hwbase +
			  cp500->devs->fan.offset +
			  cp500->devs->fan.size - 1,
		 .flags = IORESOURCE_MEM,
	};

	ret = auxiliary_device_init(&cp500->fan->auxdev);
	if (ret) {
		kfree(cp500->fan);
		cp500->fan = NULL;

		return ret;
	}
	ret = __auxiliary_device_add(&cp500->fan->auxdev, "keba");
	if (ret) {
		auxiliary_device_uninit(&cp500->fan->auxdev);
		cp500->fan = NULL;

		return ret;
	}

	return 0;
}

static void cp500_batt_release(struct device *dev)
{
	struct keba_batt_auxdev *fan =
		container_of(dev, struct keba_batt_auxdev, auxdev.dev);

	kfree(fan);
}

static int cp500_register_batt(struct cp500 *cp500)
{
	int ret;

	cp500->batt = kzalloc(sizeof(*cp500->batt), GFP_KERNEL);
	if (!cp500->batt)
		return -ENOMEM;

	cp500->batt->auxdev.name = "batt";
	cp500->batt->auxdev.id = 0;
	cp500->batt->auxdev.dev.release = cp500_batt_release;
	cp500->batt->auxdev.dev.parent = &cp500->pci_dev->dev;
	cp500->batt->io = (struct resource) {
		 /* battery register area */
		 .start = (resource_size_t) cp500->sys_hwbase +
			  cp500->devs->batt.offset,
		 .end   = (resource_size_t) cp500->sys_hwbase +
			  cp500->devs->batt.offset +
			  cp500->devs->batt.size - 1,
		 .flags = IORESOURCE_MEM,
	};

	ret = auxiliary_device_init(&cp500->batt->auxdev);
	if (ret) {
		kfree(cp500->batt);
		cp500->batt = NULL;

		return ret;
	}
	ret = __auxiliary_device_add(&cp500->batt->auxdev, "keba");
	if (ret) {
		auxiliary_device_uninit(&cp500->batt->auxdev);
		cp500->batt = NULL;

		return ret;
	}

	return 0;
}

static void cp500_uart_release(struct device *dev)
{
	struct keba_uart_auxdev *uart =
		container_of(dev, struct keba_uart_auxdev, auxdev.dev);

	kfree(uart);
}

static int cp500_register_uart(struct cp500 *cp500,
			       struct keba_uart_auxdev **uart, const char *name,
			       struct cp500_dev_info *info, unsigned int irq)
{
	int ret;

	*uart = kzalloc(sizeof(**uart), GFP_KERNEL);
	if (!*uart)
		return -ENOMEM;

	(*uart)->auxdev.name = name;
	(*uart)->auxdev.id = 0;
	(*uart)->auxdev.dev.release = cp500_uart_release;
	(*uart)->auxdev.dev.parent = &cp500->pci_dev->dev;
	(*uart)->io = (struct resource) {
		 /* UART register area */
		 .start = (resource_size_t) cp500->sys_hwbase + info->offset,
		 .end   = (resource_size_t) cp500->sys_hwbase + info->offset +
			  info->size - 1,
		 .flags = IORESOURCE_MEM,
	};
	(*uart)->irq = irq;

	ret = auxiliary_device_init(&(*uart)->auxdev);
	if (ret) {
		kfree(*uart);
		*uart = NULL;

		return ret;
	}
	ret = __auxiliary_device_add(&(*uart)->auxdev, "keba");
	if (ret) {
		auxiliary_device_uninit(&(*uart)->auxdev);
		*uart = NULL;

		return ret;
	}

	return 0;
}

static int cp500_nvmem_read(void *priv, unsigned int offset, void *val,
			    size_t bytes)
{
	struct cp500_nvmem *nvmem = priv;
	int ret;

	ret = nvmem_device_read(nvmem->base_nvmem, nvmem->offset + offset,
				bytes, val);
	if (ret != bytes)
		return ret;

	return 0;
}

static int cp500_nvmem_write(void *priv, unsigned int offset, void *val,
			     size_t bytes)
{
	struct cp500_nvmem *nvmem = priv;
	int ret;

	ret = nvmem_device_write(nvmem->base_nvmem, nvmem->offset + offset,
				 bytes, val);
	if (ret != bytes)
		return ret;

	return 0;
}

static int cp500_nvmem_register(struct cp500 *cp500,
				struct nvmem_device *base_nvmem)
{
	struct device *dev = &cp500->pci_dev->dev;
	struct nvmem_config nvmem_config = {};
	struct nvmem_device *tmp;

	/*
	 * The main EEPROM of CP500 devices is logically split into two EEPROMs.
	 * The first logical EEPROM with 3 kB contains the type label which is
	 * programmed during production of the device. The second logical EEPROM
	 * with 1 kB is not programmed during production and can be used for
	 * arbitrary user data.
	 */

	nvmem_config.dev = dev;
	nvmem_config.owner = THIS_MODULE;
	nvmem_config.id = NVMEM_DEVID_NONE;
	nvmem_config.type = NVMEM_TYPE_EEPROM;
	nvmem_config.root_only = true;
	nvmem_config.reg_read = cp500_nvmem_read;
	nvmem_config.reg_write = cp500_nvmem_write;

	cp500->nvmem_cpu.base_nvmem = base_nvmem;
	cp500->nvmem_cpu.offset = CP500_EEPROM_CPU_OFFSET;
	nvmem_config.name = CP500_EEPROM_CPU_NAME;
	nvmem_config.size = CP500_EEPROM_CPU_SIZE;
	nvmem_config.priv = &cp500->nvmem_cpu;
	tmp = nvmem_register(&nvmem_config);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	cp500->nvmem_cpu.nvmem = tmp;

	cp500->nvmem_user.base_nvmem = base_nvmem;
	cp500->nvmem_user.offset = CP500_EEPROM_USER_OFFSET;
	nvmem_config.name = CP500_EEPROM_USER_NAME;
	nvmem_config.size = CP500_EEPROM_USER_SIZE;
	nvmem_config.priv = &cp500->nvmem_user;
	tmp = nvmem_register(&nvmem_config);
	if (IS_ERR(tmp)) {
		nvmem_unregister(cp500->nvmem_cpu.nvmem);
		cp500->nvmem_cpu.nvmem = NULL;

		return PTR_ERR(tmp);
	}
	cp500->nvmem_user.nvmem = tmp;

	return 0;
}

static void cp500_nvmem_unregister(struct cp500 *cp500)
{
	int notified;

	if (cp500->nvmem_user.nvmem) {
		nvmem_unregister(cp500->nvmem_user.nvmem);
		cp500->nvmem_user.nvmem = NULL;
	}
	if (cp500->nvmem_cpu.nvmem) {
		nvmem_unregister(cp500->nvmem_cpu.nvmem);
		cp500->nvmem_cpu.nvmem = NULL;
	}

	/* CPU and user nvmem use the same base_nvmem, put only once */
	notified = atomic_read(&cp500->nvmem_notified);
	if (notified)
		nvmem_device_put(cp500->nvmem_cpu.base_nvmem);
}

static int cp500_nvmem_match(struct device *dev, const void *data)
{
	const struct cp500 *cp500 = data;
	struct i2c_client *client;

	/* match only CPU EEPROM below the cp500 device */
	dev = dev->parent;
	client = i2c_verify_client(dev);
	if (!client || client->addr != CP500_EEPROM_ADDR)
		return 0;
	while ((dev = dev->parent))
		if (dev == &cp500->pci_dev->dev)
			return 1;

	return 0;
}

static int cp500_nvmem(struct notifier_block *nb, unsigned long action,
		       void *data)
{
	struct nvmem_device *nvmem;
	struct cp500 *cp500;
	struct device *dev;
	int notified;
	u8 esc_type;
	int ret;

	if (action != NVMEM_ADD)
		return NOTIFY_DONE;
	cp500 = container_of(nb, struct cp500, nvmem_notifier);
	dev = &cp500->pci_dev->dev;

	/* process CPU EEPROM content only once */
	notified = atomic_read(&cp500->nvmem_notified);
	if (notified)
		return NOTIFY_DONE;
	nvmem = nvmem_device_find(cp500, cp500_nvmem_match);
	if (IS_ERR_OR_NULL(nvmem))
		return NOTIFY_DONE;
	if (!atomic_try_cmpxchg_relaxed(&cp500->nvmem_notified, &notified, 1)) {
		nvmem_device_put(nvmem);

		return NOTIFY_DONE;
	}

	ret = cp500_nvmem_register(cp500, nvmem);
	if (ret)
		return ret;

	ret = nvmem_device_read(nvmem, CP500_EEPROM_DA_OFFSET, sizeof(esc_type),
				(void *)&esc_type);
	if (ret != sizeof(esc_type)) {
		dev_warn(dev, "Failed to read device assembly!\n");

		return NOTIFY_DONE;
	}
	esc_type &= CP500_EEPROM_DA_ESC_TYPE_MASK;

	if (cp500_register_spi(cp500, esc_type))
		dev_warn(dev, "Failed to register SPI!\n");

	return NOTIFY_OK;
}

static void cp500_register_auxiliary_devs(struct cp500 *cp500)
{
	struct device *dev = &cp500->pci_dev->dev;
	u8 present = ioread8(cp500->system_startup_addr + CP500_PRESENT_REG);

	if (cp500_register_i2c(cp500))
		dev_warn(dev, "Failed to register I2C!\n");
	if (present & CP500_PRESENT_FAN0)
		if (cp500_register_fan(cp500))
			dev_warn(dev, "Failed to register fan!\n");
	if (cp500_register_batt(cp500))
		dev_warn(dev, "Failed to register battery!\n");
	if (cp500->devs->uart0_rfb.size &&
	    cp500->devs->uart0_rfb.msix < cp500->msix_num) {
		int irq = pci_irq_vector(cp500->pci_dev,
					 cp500->devs->uart0_rfb.msix);

		if (cp500_register_uart(cp500, &cp500->uart0_rfb, "rs485-uart",
					&cp500->devs->uart0_rfb, irq))
			dev_warn(dev, "Failed to register RFB UART!\n");
	}
	if (cp500->devs->uart1_dbg.size &&
	    cp500->devs->uart1_dbg.msix < cp500->msix_num) {
		int irq = pci_irq_vector(cp500->pci_dev,
					 cp500->devs->uart1_dbg.msix);

		if (cp500_register_uart(cp500, &cp500->uart1_dbg, "rs232-uart",
					&cp500->devs->uart1_dbg, irq))
			dev_warn(dev, "Failed to register debug UART!\n");
	}
	if (cp500->devs->uart2_si1.size &&
	    cp500->devs->uart2_si1.msix < cp500->msix_num) {
		int irq = pci_irq_vector(cp500->pci_dev,
					 cp500->devs->uart2_si1.msix);

		if (cp500_register_uart(cp500, &cp500->uart2_si1, "uart",
					&cp500->devs->uart2_si1, irq))
			dev_warn(dev, "Failed to register SI1 UART!\n");
	}
}

static void cp500_unregister_dev(struct auxiliary_device *auxdev)
{
	auxiliary_device_delete(auxdev);
	auxiliary_device_uninit(auxdev);
}

static void cp500_unregister_auxiliary_devs(struct cp500 *cp500)
{
	if (cp500->spi) {
		cp500_unregister_dev(&cp500->spi->auxdev);
		cp500->spi = NULL;
	}
	if (cp500->i2c) {
		cp500_unregister_dev(&cp500->i2c->auxdev);
		cp500->i2c = NULL;
	}
	if (cp500->fan) {
		cp500_unregister_dev(&cp500->fan->auxdev);
		cp500->fan = NULL;
	}
	if (cp500->batt) {
		cp500_unregister_dev(&cp500->batt->auxdev);
		cp500->batt = NULL;
	}
	if (cp500->uart0_rfb) {
		cp500_unregister_dev(&cp500->uart0_rfb->auxdev);
		cp500->uart0_rfb = NULL;
	}
	if (cp500->uart1_dbg) {
		cp500_unregister_dev(&cp500->uart1_dbg->auxdev);
		cp500->uart1_dbg = NULL;
	}
	if (cp500->uart2_si1) {
		cp500_unregister_dev(&cp500->uart2_si1->auxdev);
		cp500->uart2_si1 = NULL;
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

	cp500->nvmem_notifier.notifier_call = cp500_nvmem;
	ret = nvmem_register_notifier(&cp500->nvmem_notifier);
	if (ret != 0)
		goto out_free_irq;

	ret = cp500_enable(cp500);
	if (ret != 0)
		goto out_unregister_nvmem;

	cp500_register_auxiliary_devs(cp500);

	return 0;

out_unregister_nvmem:
	nvmem_unregister_notifier(&cp500->nvmem_notifier);
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

	/*
	 * unregister CPU and user nvmem and put base_nvmem before parent
	 * auxiliary device of base_nvmem is unregistered
	 */
	nvmem_unregister_notifier(&cp500->nvmem_notifier);
	cp500_nvmem_unregister(cp500);

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
