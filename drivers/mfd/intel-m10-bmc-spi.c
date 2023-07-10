// SPDX-License-Identifier: GPL-2.0
/*
 * Intel MAX 10 Board Management Controller chip
 *
 * Copyright (C) 2018-2020 Intel Corporation. All rights reserved.
 */
#include <linux/bitfield.h>
#include <linux/dev_printk.h>
#include <linux/init.h>
#include <linux/mfd/core.h>
#include <linux/mfd/intel-m10-bmc.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

static const struct regmap_range m10bmc_regmap_range[] = {
	regmap_reg_range(M10BMC_N3000_LEGACY_BUILD_VER, M10BMC_N3000_LEGACY_BUILD_VER),
	regmap_reg_range(M10BMC_N3000_SYS_BASE, M10BMC_N3000_SYS_END),
	regmap_reg_range(M10BMC_N3000_FLASH_BASE, M10BMC_N3000_FLASH_END),
};

static const struct regmap_access_table m10bmc_access_table = {
	.yes_ranges	= m10bmc_regmap_range,
	.n_yes_ranges	= ARRAY_SIZE(m10bmc_regmap_range),
};

static struct regmap_config intel_m10bmc_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.wr_table = &m10bmc_access_table,
	.rd_table = &m10bmc_access_table,
	.max_register = M10BMC_N3000_MEM_END,
};

static int check_m10bmc_version(struct intel_m10bmc *ddata)
{
	unsigned int v;
	int ret;

	/*
	 * This check is to filter out the very old legacy BMC versions. In the
	 * old BMC chips, the BMC version info is stored in the old version
	 * register (M10BMC_N3000_LEGACY_BUILD_VER), so its read out value would have
	 * not been M10BMC_N3000_VER_LEGACY_INVALID (0xffffffff). But in new BMC
	 * chips that the driver supports, the value of this register should be
	 * M10BMC_N3000_VER_LEGACY_INVALID.
	 */
	ret = m10bmc_raw_read(ddata, M10BMC_N3000_LEGACY_BUILD_VER, &v);
	if (ret)
		return -ENODEV;

	if (v != M10BMC_N3000_VER_LEGACY_INVALID) {
		dev_err(ddata->dev, "bad version M10BMC detected\n");
		return -ENODEV;
	}

	return 0;
}

static int intel_m10_bmc_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	const struct intel_m10bmc_platform_info *info;
	struct device *dev = &spi->dev;
	struct intel_m10bmc *ddata;
	int ret;

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	info = (struct intel_m10bmc_platform_info *)id->driver_data;
	ddata->dev = dev;

	ddata->regmap = devm_regmap_init_spi_avmm(spi, &intel_m10bmc_regmap_config);
	if (IS_ERR(ddata->regmap)) {
		ret = PTR_ERR(ddata->regmap);
		dev_err(dev, "Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	spi_set_drvdata(spi, ddata);

	ret = check_m10bmc_version(ddata);
	if (ret) {
		dev_err(dev, "Failed to identify m10bmc hardware\n");
		return ret;
	}

	return m10bmc_dev_init(ddata, info);
}

static const struct m10bmc_csr_map m10bmc_n3000_csr_map = {
	.base = M10BMC_N3000_SYS_BASE,
	.build_version = M10BMC_N3000_BUILD_VER,
	.fw_version = NIOS2_N3000_FW_VERSION,
	.mac_low = M10BMC_N3000_MAC_LOW,
	.mac_high = M10BMC_N3000_MAC_HIGH,
	.doorbell = M10BMC_N3000_DOORBELL,
	.auth_result = M10BMC_N3000_AUTH_RESULT,
	.bmc_prog_addr = M10BMC_N3000_BMC_PROG_ADDR,
	.bmc_reh_addr = M10BMC_N3000_BMC_REH_ADDR,
	.bmc_magic = M10BMC_N3000_BMC_PROG_MAGIC,
	.sr_prog_addr = M10BMC_N3000_SR_PROG_ADDR,
	.sr_reh_addr = M10BMC_N3000_SR_REH_ADDR,
	.sr_magic = M10BMC_N3000_SR_PROG_MAGIC,
	.pr_prog_addr = M10BMC_N3000_PR_PROG_ADDR,
	.pr_reh_addr = M10BMC_N3000_PR_REH_ADDR,
	.pr_magic = M10BMC_N3000_PR_PROG_MAGIC,
	.rsu_update_counter = M10BMC_N3000_STAGING_FLASH_COUNT,
};

static struct mfd_cell m10bmc_d5005_subdevs[] = {
	{ .name = "d5005bmc-hwmon" },
	{ .name = "d5005bmc-sec-update" },
};

static const struct regmap_range m10bmc_d5005_fw_handshake_regs[] = {
	regmap_reg_range(M10BMC_N3000_TELEM_START, M10BMC_D5005_TELEM_END),
};

static struct mfd_cell m10bmc_pacn3000_subdevs[] = {
	{ .name = "n3000bmc-hwmon" },
	{ .name = "n3000bmc-retimer" },
	{ .name = "n3000bmc-sec-update" },
};

static const struct regmap_range m10bmc_n3000_fw_handshake_regs[] = {
	regmap_reg_range(M10BMC_N3000_TELEM_START, M10BMC_N3000_TELEM_END),
};

static struct mfd_cell m10bmc_n5010_subdevs[] = {
	{ .name = "n5010bmc-hwmon" },
};

static const struct intel_m10bmc_platform_info m10bmc_spi_n3000 = {
	.cells = m10bmc_pacn3000_subdevs,
	.n_cells = ARRAY_SIZE(m10bmc_pacn3000_subdevs),
	.handshake_sys_reg_ranges = m10bmc_n3000_fw_handshake_regs,
	.handshake_sys_reg_nranges = ARRAY_SIZE(m10bmc_n3000_fw_handshake_regs),
	.csr_map = &m10bmc_n3000_csr_map,
};

static const struct intel_m10bmc_platform_info m10bmc_spi_d5005 = {
	.cells = m10bmc_d5005_subdevs,
	.n_cells = ARRAY_SIZE(m10bmc_d5005_subdevs),
	.handshake_sys_reg_ranges = m10bmc_d5005_fw_handshake_regs,
	.handshake_sys_reg_nranges = ARRAY_SIZE(m10bmc_d5005_fw_handshake_regs),
	.csr_map = &m10bmc_n3000_csr_map,
};

static const struct intel_m10bmc_platform_info m10bmc_spi_n5010 = {
	.cells = m10bmc_n5010_subdevs,
	.n_cells = ARRAY_SIZE(m10bmc_n5010_subdevs),
	.handshake_sys_reg_ranges = m10bmc_n3000_fw_handshake_regs,
	.handshake_sys_reg_nranges = ARRAY_SIZE(m10bmc_n3000_fw_handshake_regs),
	.csr_map = &m10bmc_n3000_csr_map,
};

static const struct spi_device_id m10bmc_spi_id[] = {
	{ "m10-n3000", (kernel_ulong_t)&m10bmc_spi_n3000 },
	{ "m10-d5005", (kernel_ulong_t)&m10bmc_spi_d5005 },
	{ "m10-n5010", (kernel_ulong_t)&m10bmc_spi_n5010 },
	{ }
};
MODULE_DEVICE_TABLE(spi, m10bmc_spi_id);

static struct spi_driver intel_m10bmc_spi_driver = {
	.driver = {
		.name = "intel-m10-bmc",
		.dev_groups = m10bmc_dev_groups,
	},
	.probe = intel_m10_bmc_spi_probe,
	.id_table = m10bmc_spi_id,
};
module_spi_driver(intel_m10bmc_spi_driver);

MODULE_DESCRIPTION("Intel MAX 10 BMC SPI bus interface");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:intel-m10-bmc");
MODULE_IMPORT_NS(INTEL_M10_BMC_CORE);
