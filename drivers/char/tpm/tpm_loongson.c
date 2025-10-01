// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Loongson Technology Corporation Limited. */

#include <linux/device.h>
#include <linux/mfd/loongson-se.h>
#include <linux/platform_device.h>
#include <linux/wait.h>

#include "tpm.h"

struct tpm_loongson_cmd {
	u32 cmd_id;
	u32 data_off;
	u32 data_len;
	u32 pad[5];
};

static int tpm_loongson_recv(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct loongson_se_engine *tpm_engine = dev_get_drvdata(&chip->dev);
	struct tpm_loongson_cmd *cmd_ret = tpm_engine->command_ret;

	if (cmd_ret->data_len > count)
		return -EIO;

	memcpy(buf, tpm_engine->data_buffer, cmd_ret->data_len);

	return cmd_ret->data_len;
}

static int tpm_loongson_send(struct tpm_chip *chip, u8 *buf, size_t bufsiz, size_t count)
{
	struct loongson_se_engine *tpm_engine = dev_get_drvdata(&chip->dev);
	struct tpm_loongson_cmd *cmd = tpm_engine->command;

	if (count > tpm_engine->buffer_size)
		return -E2BIG;

	cmd->data_len = count;
	memcpy(tpm_engine->data_buffer, buf, count);

	return loongson_se_send_engine_cmd(tpm_engine);
}

static const struct tpm_class_ops tpm_loongson_ops = {
	.flags = TPM_OPS_AUTO_STARTUP,
	.recv = tpm_loongson_recv,
	.send = tpm_loongson_send,
};

static int tpm_loongson_probe(struct platform_device *pdev)
{
	struct loongson_se_engine *tpm_engine;
	struct device *dev = &pdev->dev;
	struct tpm_loongson_cmd *cmd;
	struct tpm_chip *chip;

	tpm_engine = loongson_se_init_engine(dev->parent, SE_ENGINE_TPM);
	if (!tpm_engine)
		return -ENODEV;
	cmd = tpm_engine->command;
	cmd->cmd_id = SE_CMD_TPM;
	cmd->data_off = tpm_engine->buffer_off;

	chip = tpmm_chip_alloc(dev, &tpm_loongson_ops);
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	chip->flags = TPM_CHIP_FLAG_TPM2 | TPM_CHIP_FLAG_IRQ;
	dev_set_drvdata(&chip->dev, tpm_engine);

	return tpm_chip_register(chip);
}

static struct platform_driver tpm_loongson = {
	.probe   = tpm_loongson_probe,
	.driver  = {
		.name  = "tpm_loongson",
	},
};
module_platform_driver(tpm_loongson);

MODULE_ALIAS("platform:tpm_loongson");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Loongson TPM driver");
