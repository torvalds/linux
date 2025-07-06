// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright 2020 Google Inc
// Copyright 2025 Linaro Ltd.
//
// NVMEM driver for Maxim MAX77759

#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/device/driver.h>
#include <linux/err.h>
#include <linux/mfd/max77759.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#define MAX77759_NVMEM_OPCODE_HEADER_LEN 3
/*
 * NVMEM commands have a three byte header (which becomes part of the command),
 * so we need to subtract that.
 */
#define MAX77759_NVMEM_SIZE (MAX77759_MAXQ_OPCODE_MAXLENGTH \
			     - MAX77759_NVMEM_OPCODE_HEADER_LEN)

struct max77759_nvmem {
	struct device *dev;
	struct max77759 *max77759;
};

static int max77759_nvmem_reg_read(void *priv, unsigned int offset,
				   void *val, size_t bytes)
{
	struct max77759_nvmem *nvmem = priv;
	DEFINE_FLEX(struct max77759_maxq_command, cmd, cmd, length,
		    MAX77759_NVMEM_OPCODE_HEADER_LEN);
	DEFINE_FLEX(struct max77759_maxq_response, rsp, rsp, length,
		    MAX77759_MAXQ_OPCODE_MAXLENGTH);
	int ret;

	cmd->cmd[0] = MAX77759_MAXQ_OPCODE_USER_SPACE_READ;
	cmd->cmd[1] = offset;
	cmd->cmd[2] = bytes;
	rsp->length = bytes + MAX77759_NVMEM_OPCODE_HEADER_LEN;

	ret = max77759_maxq_command(nvmem->max77759, cmd, rsp);
	if (ret < 0)
		return ret;

	if (memcmp(cmd->cmd, rsp->rsp, MAX77759_NVMEM_OPCODE_HEADER_LEN)) {
		dev_warn(nvmem->dev, "protocol error (read)\n");
		return -EIO;
	}

	memcpy(val, &rsp->rsp[MAX77759_NVMEM_OPCODE_HEADER_LEN], bytes);

	return 0;
}

static int max77759_nvmem_reg_write(void *priv, unsigned int offset,
				    void *val, size_t bytes)
{
	struct max77759_nvmem *nvmem = priv;
	DEFINE_FLEX(struct max77759_maxq_command, cmd, cmd, length,
		    MAX77759_MAXQ_OPCODE_MAXLENGTH);
	DEFINE_FLEX(struct max77759_maxq_response, rsp, rsp, length,
		    MAX77759_MAXQ_OPCODE_MAXLENGTH);
	int ret;

	cmd->cmd[0] = MAX77759_MAXQ_OPCODE_USER_SPACE_WRITE;
	cmd->cmd[1] = offset;
	cmd->cmd[2] = bytes;
	memcpy(&cmd->cmd[MAX77759_NVMEM_OPCODE_HEADER_LEN], val, bytes);
	cmd->length = bytes + MAX77759_NVMEM_OPCODE_HEADER_LEN;
	rsp->length = cmd->length;

	ret = max77759_maxq_command(nvmem->max77759, cmd, rsp);
	if (ret < 0)
		return ret;

	if (memcmp(cmd->cmd, rsp->rsp, cmd->length)) {
		dev_warn(nvmem->dev, "protocol error (write)\n");
		return -EIO;
	}

	return 0;
}

static int max77759_nvmem_probe(struct platform_device *pdev)
{
	struct nvmem_config config = {
		.dev = &pdev->dev,
		.name = dev_name(&pdev->dev),
		.id = NVMEM_DEVID_NONE,
		.type = NVMEM_TYPE_EEPROM,
		.ignore_wp = true,
		.size = MAX77759_NVMEM_SIZE,
		.word_size = sizeof(u8),
		.stride = sizeof(u8),
		.reg_read = max77759_nvmem_reg_read,
		.reg_write = max77759_nvmem_reg_write,
	};
	struct max77759_nvmem *nvmem;

	nvmem = devm_kzalloc(&pdev->dev, sizeof(*nvmem), GFP_KERNEL);
	if (!nvmem)
		return -ENOMEM;

	nvmem->dev = &pdev->dev;
	nvmem->max77759 = dev_get_drvdata(pdev->dev.parent);

	config.priv = nvmem;

	return PTR_ERR_OR_ZERO(devm_nvmem_register(config.dev, &config));
}

static const struct of_device_id max77759_nvmem_of_id[] = {
	{ .compatible = "maxim,max77759-nvmem", },
	{ }
};
MODULE_DEVICE_TABLE(of, max77759_nvmem_of_id);

static const struct platform_device_id max77759_nvmem_platform_id[] = {
	{ "max77759-nvmem", },
	{ }
};
MODULE_DEVICE_TABLE(platform, max77759_nvmem_platform_id);

static struct platform_driver max77759_nvmem_driver = {
	.driver = {
		.name = "max77759-nvmem",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = max77759_nvmem_of_id,
	},
	.probe = max77759_nvmem_probe,
	.id_table = max77759_nvmem_platform_id,
};

module_platform_driver(max77759_nvmem_driver);

MODULE_AUTHOR("André Draszik <andre.draszik@linaro.org>");
MODULE_DESCRIPTION("NVMEM driver for Maxim MAX77759");
MODULE_LICENSE("GPL");
