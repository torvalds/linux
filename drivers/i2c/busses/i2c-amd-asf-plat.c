// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Alert Standard Format Platform Driver
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *	    Sanket Goswami <Sanket.Goswami@amd.com>
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/sprintf.h>

#include "i2c-piix4.h"

/* ASF register bits */
#define ASF_SLV_LISTN	0
#define ASF_SLV_INTR	1
#define ASF_SLV_RST	4
#define ASF_PEC_SP	5
#define ASF_DATA_EN	7
#define ASF_MSTR_EN	16
#define ASF_CLK_EN	17

/* ASF address offsets */
#define ASFLISADDR	(0x09 + piix4_smba)
#define ASFSTA		(0x0A + piix4_smba)
#define ASFSLVSTA	(0x0D + piix4_smba)
#define ASFDATABNKSEL	(0x13 + piix4_smba)
#define ASFSLVEN	(0x15 + piix4_smba)

#define ASF_BLOCK_MAX_BYTES	72

struct amd_asf_dev {
	struct i2c_adapter adap;
	struct i2c_client *target;
	struct sb800_mmio_cfg mmio_cfg;
	struct resource *port_addr;
};

static void amd_asf_update_ioport_target(unsigned short piix4_smba, u8 bit,
					 unsigned long offset, bool set)
{
	unsigned long reg;

	reg = inb_p(offset);
	__assign_bit(bit, &reg, set);
	outb_p(reg, offset);
}

static void amd_asf_update_mmio_target(struct amd_asf_dev *dev, u8 bit, bool set)
{
	unsigned long reg;

	reg = ioread32(dev->mmio_cfg.addr);
	__assign_bit(bit, &reg, set);
	iowrite32(reg, dev->mmio_cfg.addr);
}

static void amd_asf_setup_target(struct amd_asf_dev *dev)
{
	unsigned short piix4_smba = dev->port_addr->start;

	/* Reset both host and target before setting up */
	outb_p(0, SMBHSTSTS);
	outb_p(0, ASFSLVSTA);
	outb_p(0, ASFSTA);

	/* Update target address */
	amd_asf_update_ioport_target(piix4_smba, ASF_SLV_LISTN, ASFLISADDR, true);
	/* Enable target and set the clock */
	amd_asf_update_mmio_target(dev, ASF_MSTR_EN, false);
	amd_asf_update_mmio_target(dev, ASF_CLK_EN, true);
	/* Enable target interrupt */
	amd_asf_update_ioport_target(piix4_smba, ASF_SLV_INTR, ASFSLVEN, true);
	amd_asf_update_ioport_target(piix4_smba, ASF_SLV_RST, ASFSLVEN, false);
	/* Enable PEC and PEC append */
	amd_asf_update_ioport_target(piix4_smba, ASF_DATA_EN, SMBHSTCNT, true);
	amd_asf_update_ioport_target(piix4_smba, ASF_PEC_SP, SMBHSTCNT, true);
}

static int amd_asf_access(struct i2c_adapter *adap, u16 addr, u8 command, u8 *data)
{
	struct amd_asf_dev *dev = i2c_get_adapdata(adap);
	unsigned short piix4_smba = dev->port_addr->start;
	u8 i, len;

	outb_p((addr << 1), SMBHSTADD);
	outb_p(command, SMBHSTCMD);
	len = data[0];
	if (len == 0 || len > ASF_BLOCK_MAX_BYTES)
		return -EINVAL;

	outb_p(len, SMBHSTDAT0);
	/* Reset SMBBLKDAT */
	inb_p(SMBHSTCNT);
	for (i = 1; i <= len; i++)
		outb_p(data[i], SMBBLKDAT);

	outb_p(PIIX4_BLOCK_DATA, SMBHSTCNT);
	/* Enable PEC and PEC append */
	amd_asf_update_ioport_target(piix4_smba, ASF_DATA_EN, SMBHSTCNT, true);
	amd_asf_update_ioport_target(piix4_smba, ASF_PEC_SP, SMBHSTCNT, true);

	return piix4_transaction(adap, piix4_smba);
}

static int amd_asf_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct amd_asf_dev *dev = i2c_get_adapdata(adap);
	unsigned short piix4_smba = dev->port_addr->start;
	u8 asf_data[ASF_BLOCK_MAX_BYTES];
	struct i2c_msg *dev_msgs = msgs;
	u8 prev_port;
	int ret;

	if (msgs->flags & I2C_M_RD) {
		dev_err(&adap->dev, "ASF: Read not supported\n");
		return -EOPNOTSUPP;
	}

	/* Exclude the receive header and PEC */
	if (msgs->len > ASF_BLOCK_MAX_BYTES - 3) {
		dev_warn(&adap->dev, "ASF: max message length exceeded\n");
		return -EOPNOTSUPP;
	}

	asf_data[0] = dev_msgs->len;
	memcpy(asf_data + 1, dev_msgs[0].buf, dev_msgs->len);

	ret = piix4_sb800_region_request(&adap->dev, &dev->mmio_cfg);
	if (ret)
		return ret;

	amd_asf_update_ioport_target(piix4_smba, ASF_SLV_RST, ASFSLVEN, true);
	amd_asf_update_ioport_target(piix4_smba, ASF_SLV_LISTN, ASFLISADDR, false);
	/* Clear ASF target status */
	outb_p(0, ASFSLVSTA);

	/* Enable ASF SMBus controller function */
	amd_asf_update_mmio_target(dev, ASF_MSTR_EN, true);
	prev_port = piix4_sb800_port_sel(0, &dev->mmio_cfg);
	ret = amd_asf_access(adap, msgs->addr, msgs[0].buf[0], asf_data);
	piix4_sb800_port_sel(prev_port, &dev->mmio_cfg);
	amd_asf_setup_target(dev);
	piix4_sb800_region_release(&adap->dev, &dev->mmio_cfg);
	return ret;
}

static int amd_asf_reg_target(struct i2c_client *target)
{
	struct amd_asf_dev *dev = i2c_get_adapdata(target->adapter);
	unsigned short piix4_smba = dev->port_addr->start;
	int ret;
	u8 reg;

	if (dev->target)
		return -EBUSY;

	ret = piix4_sb800_region_request(&target->dev, &dev->mmio_cfg);
	if (ret)
		return ret;

	reg = (target->addr << 1) | I2C_M_RD;
	outb_p(reg, ASFLISADDR);

	amd_asf_setup_target(dev);
	dev->target = target;
	amd_asf_update_ioport_target(piix4_smba, ASF_DATA_EN, ASFDATABNKSEL, false);
	piix4_sb800_region_release(&target->dev, &dev->mmio_cfg);

	return 0;
}

static int amd_asf_unreg_target(struct i2c_client *target)
{
	struct amd_asf_dev *dev = i2c_get_adapdata(target->adapter);
	unsigned short piix4_smba = dev->port_addr->start;

	amd_asf_update_ioport_target(piix4_smba, ASF_SLV_INTR, ASFSLVEN, false);
	amd_asf_update_ioport_target(piix4_smba, ASF_SLV_RST, ASFSLVEN, true);
	dev->target = NULL;

	return 0;
}

static u32 amd_asf_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_WRITE_BLOCK_DATA | I2C_FUNC_SMBUS_BLOCK_DATA |
	       I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_PEC | I2C_FUNC_SLAVE;
}

static const struct i2c_algorithm amd_asf_smbus_algorithm = {
	.master_xfer = amd_asf_xfer,
	.reg_slave = amd_asf_reg_target,
	.unreg_slave = amd_asf_unreg_target,
	.functionality = amd_asf_func,
};

static int amd_asf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct amd_asf_dev *asf_dev;

	asf_dev = devm_kzalloc(dev, sizeof(*asf_dev), GFP_KERNEL);
	if (!asf_dev)
		return dev_err_probe(dev, -ENOMEM, "Failed to allocate memory\n");

	asf_dev->mmio_cfg.use_mmio = true;
	asf_dev->port_addr = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!asf_dev->port_addr)
		return dev_err_probe(dev, -EINVAL, "missing IO resources\n");

	asf_dev->adap.owner = THIS_MODULE;
	asf_dev->adap.algo = &amd_asf_smbus_algorithm;
	asf_dev->adap.dev.parent = dev;

	i2c_set_adapdata(&asf_dev->adap, asf_dev);
	snprintf(asf_dev->adap.name, sizeof(asf_dev->adap.name), "AMD ASF adapter");

	return devm_i2c_add_adapter(dev, &asf_dev->adap);
}

static const struct acpi_device_id amd_asf_acpi_ids[] = {
	{ "AMDI001A" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, amd_asf_acpi_ids);

static struct platform_driver amd_asf_driver = {
	.driver = {
		.name = "i2c-amd-asf",
		.acpi_match_table = amd_asf_acpi_ids,
	},
	.probe = amd_asf_probe,
};
module_platform_driver(amd_asf_driver);

MODULE_IMPORT_NS(PIIX4_SMBUS);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMD Alert Standard Format Driver");
