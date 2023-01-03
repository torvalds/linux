// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip PolarFire SoC (MPFS) hardware random driver
 *
 * Copyright (c) 2020-2022 Microchip Corporation. All rights reserved.
 *
 * Author: Conor Dooley <conor.dooley@microchip.com>
 */

#include <linux/module.h>
#include <linux/hw_random.h>
#include <linux/platform_device.h>
#include <soc/microchip/mpfs.h>

#define CMD_OPCODE	0x21
#define CMD_DATA_SIZE	0U
#define CMD_DATA	NULL
#define MBOX_OFFSET	0U
#define RESP_OFFSET	0U
#define RNG_RESP_BYTES	32U

struct mpfs_rng {
	struct mpfs_sys_controller *sys_controller;
	struct hwrng rng;
};

static int mpfs_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct mpfs_rng *rng_priv = container_of(rng, struct mpfs_rng, rng);
	u32 response_msg[RNG_RESP_BYTES / sizeof(u32)];
	unsigned int count = 0, copy_size_bytes;
	int ret;

	struct mpfs_mss_response response = {
		.resp_status = 0U,
		.resp_msg = (u32 *)response_msg,
		.resp_size = RNG_RESP_BYTES
	};
	struct mpfs_mss_msg msg = {
		.cmd_opcode = CMD_OPCODE,
		.cmd_data_size = CMD_DATA_SIZE,
		.response = &response,
		.cmd_data = CMD_DATA,
		.mbox_offset = MBOX_OFFSET,
		.resp_offset = RESP_OFFSET
	};

	while (count < max) {
		ret = mpfs_blocking_transaction(rng_priv->sys_controller, &msg);
		if (ret)
			return ret;

		copy_size_bytes = max - count > RNG_RESP_BYTES ? RNG_RESP_BYTES : max - count;
		memcpy(buf + count, response_msg, copy_size_bytes);

		count += copy_size_bytes;
		if (!wait)
			break;
	}

	return count;
}

static int mpfs_rng_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpfs_rng *rng_priv;
	int ret;

	rng_priv = devm_kzalloc(dev, sizeof(*rng_priv), GFP_KERNEL);
	if (!rng_priv)
		return -ENOMEM;

	rng_priv->sys_controller =  mpfs_sys_controller_get(&pdev->dev);
	if (IS_ERR(rng_priv->sys_controller))
		return dev_err_probe(dev, PTR_ERR(rng_priv->sys_controller),
				     "Failed to register system controller hwrng sub device\n");

	rng_priv->rng.read = mpfs_rng_read;
	rng_priv->rng.name = pdev->name;

	platform_set_drvdata(pdev, rng_priv);

	ret = devm_hwrng_register(&pdev->dev, &rng_priv->rng);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to register MPFS hwrng\n");

	dev_info(&pdev->dev, "Registered MPFS hwrng\n");

	return 0;
}

static struct platform_driver mpfs_rng_driver = {
	.driver = {
		.name = "mpfs-rng",
	},
	.probe = mpfs_rng_probe,
};
module_platform_driver(mpfs_rng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_DESCRIPTION("PolarFire SoC (MPFS) hardware random driver");
