// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ee1004 - driver for DDR4 SPD EEPROMs
 *
 * Copyright (C) 2017-2019 Jean Delvare
 *
 * Based on the at24 driver:
 * Copyright (C) 2005-2007 David Brownell
 * Copyright (C) 2008 Wolfram Sang, Pengutronix
 */

#include <linux/mfd/qnap-mcu.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Determined by trial and error until read anomalies appeared */
#define QNAP_MCU_EEPROM_SIZE		256
#define QNAP_MCU_EEPROM_BLOCK_SIZE	32

static int qnap_mcu_eeprom_read_block(struct qnap_mcu *mcu, unsigned int offset,
				      void *val, size_t bytes)
{
	const u8 cmd[] = { 0xf7, 0xa1, offset, bytes };
	u8 *reply;
	int ret = 0;

	reply = kzalloc(bytes + sizeof(cmd), GFP_KERNEL);
	if (!reply)
		return -ENOMEM;

	ret = qnap_mcu_exec(mcu, cmd, sizeof(cmd), reply, bytes + sizeof(cmd));
	if (ret)
		goto out;

	/* First bytes must mirror the sent command */
	if (memcmp(cmd, reply, sizeof(cmd))) {
		ret = -EIO;
		goto out;
	}

	memcpy(val, reply + sizeof(cmd), bytes);

out:
	kfree(reply);
	return ret;
}

static int qnap_mcu_eeprom_read(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct qnap_mcu *mcu = priv;
	int pos = 0, ret;
	u8 *buf = val;

	if (unlikely(!bytes))
		return 0;

	while (bytes > 0) {
		size_t to_read = (bytes > QNAP_MCU_EEPROM_BLOCK_SIZE) ?
						QNAP_MCU_EEPROM_BLOCK_SIZE : bytes;

		ret = qnap_mcu_eeprom_read_block(mcu, offset + pos, &buf[pos], to_read);
		if (ret < 0)
			return ret;

		pos += to_read;
		bytes -= to_read;
	}

	return 0;
}

static int qnap_mcu_eeprom_probe(struct platform_device *pdev)
{
	struct qnap_mcu *mcu = dev_get_drvdata(pdev->dev.parent);
	struct nvmem_config nvcfg = {};
	struct nvmem_device *ndev;

	nvcfg.dev = &pdev->dev;
	nvcfg.of_node = pdev->dev.parent->of_node;
	nvcfg.name = dev_name(&pdev->dev);
	nvcfg.id = NVMEM_DEVID_NONE;
	nvcfg.owner = THIS_MODULE;
	nvcfg.type = NVMEM_TYPE_EEPROM;
	nvcfg.read_only = true;
	nvcfg.root_only = false;
	nvcfg.reg_read = qnap_mcu_eeprom_read;
	nvcfg.size = QNAP_MCU_EEPROM_SIZE,
	nvcfg.word_size = 1,
	nvcfg.stride = 1,
	nvcfg.priv = mcu,

	ndev = devm_nvmem_register(&pdev->dev, &nvcfg);
	if (IS_ERR(ndev))
		return PTR_ERR(ndev);

	return 0;
}

static struct platform_driver qnap_mcu_eeprom_driver = {
	.probe = qnap_mcu_eeprom_probe,
	.driver = {
		.name = "qnap-mcu-eeprom",
	},
};
module_platform_driver(qnap_mcu_eeprom_driver);

MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("QNAP MCU EEPROM driver");
MODULE_LICENSE("GPL");
