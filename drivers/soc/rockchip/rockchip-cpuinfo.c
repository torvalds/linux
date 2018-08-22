/*
 * Copyright (C) 2017 Rockchip Electronics Co. Ltd.
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

#include <linux/crc32.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <asm/system_info.h>
#include <linux/rockchip/cpu.h>

unsigned long rockchip_soc_id;
EXPORT_SYMBOL(rockchip_soc_id);

static int rockchip_cpuinfo_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvmem_cell *cell;
	unsigned char *efuse_buf, buf[16];
	size_t len;
	int i;

	cell = nvmem_cell_get(dev, "cpu-version");
	if (!IS_ERR(cell)) {
		efuse_buf = nvmem_cell_read(cell, &len);
		nvmem_cell_put(cell);

		if (len == 1)
			rockchip_set_cpu_version(efuse_buf[0]);
		kfree(efuse_buf);
	}

	cell = nvmem_cell_get(dev, "id");
	if (IS_ERR(cell)) {
		dev_err(dev, "failed to get id cell: %ld\n", PTR_ERR(cell));
		if (PTR_ERR(cell) == -EPROBE_DEFER)
			return PTR_ERR(cell);
		return PTR_ERR(cell);
	}
	efuse_buf = nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);

	if (len != 16) {
		kfree(efuse_buf);
		dev_err(dev, "invalid id len: %zu\n", len);
		return -EINVAL;
	}

	for (i = 0; i < 8; i++) {
		buf[i] = efuse_buf[1 + (i << 1)];
		buf[i + 8] = efuse_buf[i << 1];
	}

	kfree(efuse_buf);

	system_serial_low = crc32(0, buf, 8);
	system_serial_high = crc32(system_serial_low, buf + 8, 8);

	dev_info(dev, "Serial\t\t: %08x%08x\n",
		 system_serial_high, system_serial_low);

	return 0;
}

static const struct of_device_id rockchip_cpuinfo_of_match[] = {
	{ .compatible = "rockchip,cpuinfo", },
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_cpuinfo_of_match);

static struct platform_driver rockchip_cpuinfo_driver = {
	.probe = rockchip_cpuinfo_probe,
	.driver = {
		.name = "rockchip-cpuinfo",
		.of_match_table = rockchip_cpuinfo_of_match,
	},
};

static void rk3288_init(void)
{
	void __iomem *base;

	rockchip_soc_id = ROCKCHIP_SOC_RK3288;
#define RK3288_HDMI_PHYS	0xFF980000
	base = ioremap(RK3288_HDMI_PHYS, SZ_4K);
	if (base) {
		/* RK3288W HDMI Revision ID is 0x1A */
		if (readl_relaxed(base + 4) == 0x1A)
			rockchip_soc_id = ROCKCHIP_SOC_RK3288W;
		iounmap(base);
	}
}

static void rk3126_init(void)
{
	void __iomem *base;

	rockchip_soc_id = ROCKCHIP_SOC_RK3126;
#define RK312X_GRF_PHYS		0x20008000
#define RK312X_GRF_SOC_CON1	0x00000144
#define RK312X_GRF_CHIP_TAG	0x00000300
	base = ioremap(RK312X_GRF_PHYS, SZ_4K);
	if (base) {
		if (readl_relaxed(base + RK312X_GRF_CHIP_TAG) == 0x3136) {
			if (readl_relaxed(base + RK312X_GRF_SOC_CON1) & 0x1)
				rockchip_soc_id = ROCKCHIP_SOC_RK3126C;
			else
				rockchip_soc_id = ROCKCHIP_SOC_RK3126B;
		}
		iounmap(base);
	}
}

static void rk3308_init(void)
{
	void __iomem *base;

	rockchip_soc_id = ROCKCHIP_SOC_RK3308;
#define RK3308_GRF_PHYS		0xFF000000
#define RK3308_GRF_CHIP_ID	0x800
	base = ioremap(RK3308_GRF_PHYS, SZ_4K);
	if (base) {
		if (readl_relaxed(base + RK3308_GRF_CHIP_ID) == 0x3308)
			rockchip_soc_id = ROCKCHIP_SOC_RK3308B;
		iounmap(base);
	}
}

static int __init rockchip_soc_id_init(void)
{
	if (cpu_is_rk3288()) {
		rk3288_init();
	} else if (cpu_is_rk312x()) {
		if (of_machine_is_compatible("rockchip,rk3128"))
			rockchip_soc_id = ROCKCHIP_SOC_RK3128;
		else
			rk3126_init();
	} else if (cpu_is_rk3308()) {
		rk3308_init();
	}

	return 0;
}
pure_initcall(rockchip_soc_id_init);

static int __init rockchip_cpuinfo_init(void)
{
	return platform_driver_register(&rockchip_cpuinfo_driver);
}
subsys_initcall_sync(rockchip_cpuinfo_init);
