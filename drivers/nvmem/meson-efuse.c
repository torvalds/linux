/*
 * Amlogic eFuse Driver
 *
 * Copyright (c) 2016 Endless Computers, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <linux/firmware/meson/meson_sm.h>

static int meson_efuse_read(void *context, unsigned int offset,
			    void *val, size_t bytes)
{
	u8 *buf = val;
	int ret;

	ret = meson_sm_call_read(buf, SM_EFUSE_READ, offset,
				 bytes, 0, 0, 0);
	if (ret < 0)
		return ret;

	return 0;
}

static struct nvmem_config econfig = {
	.name = "meson-efuse",
	.owner = THIS_MODULE,
	.stride = 1,
	.word_size = 1,
	.read_only = true,
};

static const struct of_device_id meson_efuse_match[] = {
	{ .compatible = "amlogic,meson-gxbb-efuse", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, meson_efuse_match);

static int meson_efuse_probe(struct platform_device *pdev)
{
	struct nvmem_device *nvmem;
	unsigned int size;

	if (meson_sm_call(SM_EFUSE_USER_MAX, &size, 0, 0, 0, 0, 0) < 0)
		return -EINVAL;

	econfig.dev = &pdev->dev;
	econfig.reg_read = meson_efuse_read;
	econfig.size = size;

	nvmem = nvmem_register(&econfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	platform_set_drvdata(pdev, nvmem);

	return 0;
}

static int meson_efuse_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	return nvmem_unregister(nvmem);
}

static struct platform_driver meson_efuse_driver = {
	.probe = meson_efuse_probe,
	.remove = meson_efuse_remove,
	.driver = {
		.name = "meson-efuse",
		.of_match_table = meson_efuse_match,
	},
};

module_platform_driver(meson_efuse_driver);

MODULE_AUTHOR("Carlo Caione <carlo@endlessm.com>");
MODULE_DESCRIPTION("Amlogic Meson NVMEM driver");
MODULE_LICENSE("GPL v2");
