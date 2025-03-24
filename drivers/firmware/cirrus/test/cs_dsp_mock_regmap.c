// SPDX-License-Identifier: GPL-2.0-only
//
// Mock regmap for cs_dsp KUnit tests.
//
// Copyright (C) 2024 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <kunit/test.h>
#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/cs_dsp_test_utils.h>
#include <linux/firmware/cirrus/wmfw.h>
#include <linux/regmap.h>

static int cs_dsp_mock_regmap_read(void *context, const void *reg_buf,
				   const size_t reg_size, void *val_buf,
				   size_t val_size)
{
	struct cs_dsp_test *priv = context;

	/* Should never get here because the regmap is cache-only */
	KUNIT_FAIL(priv->test, "Unexpected bus read @%#x", *(u32 *)reg_buf);

	return -EIO;
}

static int cs_dsp_mock_regmap_gather_write(void *context,
					   const void *reg_buf, size_t reg_size,
					   const void *val_buf, size_t val_size)
{
	struct cs_dsp_test *priv = context;

	priv->saw_bus_write = true;

	/* Should never get here because the regmap is cache-only */
	KUNIT_FAIL(priv->test, "Unexpected bus gather_write @%#x", *(u32 *)reg_buf);

	return -EIO;
}

static int cs_dsp_mock_regmap_write(void *context, const void *val_buf, size_t val_size)
{
	struct cs_dsp_test *priv = context;

	priv->saw_bus_write = true;

	/* Should never get here because the regmap is cache-only */
	KUNIT_FAIL(priv->test, "Unexpected bus write @%#x", *(u32 *)val_buf);

	return -EIO;
}

static const struct regmap_bus cs_dsp_mock_regmap_bus = {
	.read = cs_dsp_mock_regmap_read,
	.write = cs_dsp_mock_regmap_write,
	.gather_write = cs_dsp_mock_regmap_gather_write,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

static const struct reg_default adsp2_32bit_register_defaults[] = {
	{ 0xffe00, 0x0000 }, /* CONTROL */
	{ 0xffe02, 0x0000 }, /* CLOCKING */
	{ 0xffe04, 0x0001 }, /* STATUS1: RAM_RDY=1 */
	{ 0xffe30, 0x0000 }, /* WDMW_CONFIG_1 */
	{ 0xffe32, 0x0000 }, /* WDMA_CONFIG_2 */
	{ 0xffe34, 0x0000 }, /* RDMA_CONFIG_1 */
	{ 0xffe40, 0x0000 }, /* SCRATCH_0_1 */
	{ 0xffe42, 0x0000 }, /* SCRATCH_2_3 */
};

static const struct regmap_range adsp2_32bit_registers[] = {
	regmap_reg_range(0x80000, 0x88ffe), /* PM */
	regmap_reg_range(0xa0000, 0xa9ffe), /* XM */
	regmap_reg_range(0xc0000, 0xc1ffe), /* YM */
	regmap_reg_range(0xe0000, 0xe1ffe), /* ZM */
	regmap_reg_range(0xffe00, 0xffe7c), /* CORE CTRL */
};

const unsigned int cs_dsp_mock_adsp2_32bit_sysbase = 0xffe00;
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_adsp2_32bit_sysbase, "FW_CS_DSP_KUNIT_TEST_UTILS");

static const struct regmap_access_table adsp2_32bit_rw = {
	.yes_ranges = adsp2_32bit_registers,
	.n_yes_ranges = ARRAY_SIZE(adsp2_32bit_registers),
};

static const struct regmap_config cs_dsp_mock_regmap_adsp2_32bit = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 2,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.wr_table = &adsp2_32bit_rw,
	.rd_table = &adsp2_32bit_rw,
	.max_register = 0xffe7c,
	.reg_defaults = adsp2_32bit_register_defaults,
	.num_reg_defaults = ARRAY_SIZE(adsp2_32bit_register_defaults),
	.cache_type = REGCACHE_MAPLE,
};

static const struct reg_default adsp2_16bit_register_defaults[] = {
	{ 0x1100, 0x0000 }, /* CONTROL */
	{ 0x1101, 0x0000 }, /* CLOCKING */
	{ 0x1104, 0x0001 }, /* STATUS1: RAM_RDY=1 */
	{ 0x1130, 0x0000 }, /* WDMW_CONFIG_1 */
	{ 0x1131, 0x0000 }, /* WDMA_CONFIG_2 */
	{ 0x1134, 0x0000 }, /* RDMA_CONFIG_1 */
	{ 0x1140, 0x0000 }, /* SCRATCH_0 */
	{ 0x1141, 0x0000 }, /* SCRATCH_1 */
	{ 0x1142, 0x0000 }, /* SCRATCH_2 */
	{ 0x1143, 0x0000 }, /* SCRATCH_3 */
};

static const struct regmap_range adsp2_16bit_registers[] = {
	regmap_reg_range(0x001100, 0x001143), /* CORE CTRL */
	regmap_reg_range(0x100000, 0x105fff), /* PM */
	regmap_reg_range(0x180000, 0x1807ff), /* ZM */
	regmap_reg_range(0x190000, 0x1947ff), /* XM */
	regmap_reg_range(0x1a8000, 0x1a97ff), /* YM */
};

const unsigned int cs_dsp_mock_adsp2_16bit_sysbase = 0x001100;
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_adsp2_16bit_sysbase, "FW_CS_DSP_KUNIT_TEST_UTILS");

static const struct regmap_access_table adsp2_16bit_rw = {
	.yes_ranges = adsp2_16bit_registers,
	.n_yes_ranges = ARRAY_SIZE(adsp2_16bit_registers),
};

static const struct regmap_config cs_dsp_mock_regmap_adsp2_16bit = {
	.reg_bits = 32,
	.val_bits = 16,
	.reg_stride = 1,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.wr_table = &adsp2_16bit_rw,
	.rd_table = &adsp2_16bit_rw,
	.max_register = 0x1a97ff,
	.reg_defaults = adsp2_16bit_register_defaults,
	.num_reg_defaults = ARRAY_SIZE(adsp2_16bit_register_defaults),
	.cache_type = REGCACHE_MAPLE,
};

static const struct reg_default halo_register_defaults[] = {
	/* CORE */
	{ 0x2b80010, 0 },	/* HALO_CORE_SOFT_RESET */
	{ 0x2b805c0, 0 },	/* HALO_SCRATCH1 */
	{ 0x2b805c8, 0 },	/* HALO_SCRATCH2 */
	{ 0x2b805d0, 0 },	/* HALO_SCRATCH3 */
	{ 0x2b805c8, 0 },	/* HALO_SCRATCH4 */
	{ 0x2bc1000, 0 },	/* HALO_CCM_CORE_CONTROL */
	{ 0x2bc7000, 0 },	/* HALO_WDT_CONTROL */

	/* SYSINFO */
	{ 0x25e2040, 0 },	/* HALO_AHBM_WINDOW_DEBUG_0 */
	{ 0x25e2044, 0 },	/* HALO_AHBM_WINDOW_DEBUG_1 */
};

static const struct regmap_range halo_readable_registers[] = {
	regmap_reg_range(0x2000000, 0x2005fff), /* XM_PACKED */
	regmap_reg_range(0x25e0000, 0x25e004f), /* SYSINFO */
	regmap_reg_range(0x25e2000, 0x25e2047), /* SYSINFO */
	regmap_reg_range(0x2800000, 0x2807fff), /* XM */
	regmap_reg_range(0x2b80000, 0x2bc700b), /* CORE CTRL */
	regmap_reg_range(0x2c00000, 0x2c047f3), /* YM_PACKED */
	regmap_reg_range(0x3400000, 0x3405ff7), /* YM */
	regmap_reg_range(0x3800000, 0x3804fff), /* PM_PACKED */
};

static const struct regmap_range halo_writeable_registers[] = {
	regmap_reg_range(0x2000000, 0x2005fff), /* XM_PACKED */
	regmap_reg_range(0x2800000, 0x2807fff), /* XM */
	regmap_reg_range(0x2b80000, 0x2bc700b), /* CORE CTRL */
	regmap_reg_range(0x2c00000, 0x2c047f3), /* YM_PACKED */
	regmap_reg_range(0x3400000, 0x3405ff7), /* YM */
	regmap_reg_range(0x3800000, 0x3804fff), /* PM_PACKED */
};

const unsigned int cs_dsp_mock_halo_core_base = 0x2b80000;
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_halo_core_base, "FW_CS_DSP_KUNIT_TEST_UTILS");

const unsigned int cs_dsp_mock_halo_sysinfo_base = 0x25e0000;
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_halo_sysinfo_base, "FW_CS_DSP_KUNIT_TEST_UTILS");

static const struct regmap_access_table halo_readable = {
	.yes_ranges = halo_readable_registers,
	.n_yes_ranges = ARRAY_SIZE(halo_readable_registers),
};

static const struct regmap_access_table halo_writeable = {
	.yes_ranges = halo_writeable_registers,
	.n_yes_ranges = ARRAY_SIZE(halo_writeable_registers),
};

static const struct regmap_config cs_dsp_mock_regmap_halo = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.wr_table = &halo_writeable,
	.rd_table = &halo_readable,
	.max_register = 0x3804ffc,
	.reg_defaults = halo_register_defaults,
	.num_reg_defaults = ARRAY_SIZE(halo_register_defaults),
	.cache_type = REGCACHE_MAPLE,
};

/**
 * cs_dsp_mock_regmap_drop_range() - drop a range of registers from the cache.
 *
 * @priv:	Pointer to struct cs_dsp_test object.
 * @first_reg:	Address of first register to drop.
 * @last_reg:	Address of last register to drop.
 */
void cs_dsp_mock_regmap_drop_range(struct cs_dsp_test *priv,
				   unsigned int first_reg, unsigned int last_reg)
{
	regcache_drop_region(priv->dsp->regmap, first_reg, last_reg);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_regmap_drop_range, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_regmap_drop_regs() - drop a number of registers from the cache.
 *
 * @priv:	Pointer to struct cs_dsp_test object.
 * @first_reg:	Address of first register to drop.
 * @num_regs:	Number of registers to drop.
 */
void cs_dsp_mock_regmap_drop_regs(struct cs_dsp_test *priv,
				  unsigned int first_reg, size_t num_regs)
{
	int stride = regmap_get_reg_stride(priv->dsp->regmap);
	unsigned int last = first_reg + (stride * (num_regs - 1));

	cs_dsp_mock_regmap_drop_range(priv, first_reg, last);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_regmap_drop_regs, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_regmap_drop_bytes() - drop a number of bytes from the cache.
 *
 * @priv:	Pointer to struct cs_dsp_test object.
 * @first_reg:	Address of first register to drop.
 * @num_bytes:	Number of bytes to drop from the cache. Will be rounded
 *		down to a whole number of registers. Trailing bytes that
 *		are not a multiple of the register size will not be dropped.
 *		(This is intended to help detect math errors in test code.)
 */
void cs_dsp_mock_regmap_drop_bytes(struct cs_dsp_test *priv,
				   unsigned int first_reg, size_t num_bytes)
{
	size_t num_regs = num_bytes / regmap_get_val_bytes(priv->dsp->regmap);

	cs_dsp_mock_regmap_drop_regs(priv, first_reg, num_regs);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_regmap_drop_bytes, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_regmap_drop_system_regs() - Drop DSP system registers from the cache.
 *
 * @priv:	Pointer to struct cs_dsp_test object.
 *
 * Drops all DSP system registers from the regmap cache.
 */
void cs_dsp_mock_regmap_drop_system_regs(struct cs_dsp_test *priv)
{
	switch (priv->dsp->type) {
	case WMFW_ADSP2:
		if (priv->dsp->base) {
			regcache_drop_region(priv->dsp->regmap,
					     priv->dsp->base,
					     priv->dsp->base + 0x7c);
		}
		return;
	case WMFW_HALO:
		if (priv->dsp->base) {
			regcache_drop_region(priv->dsp->regmap,
					     priv->dsp->base,
					     priv->dsp->base + 0x47000);
		}

		/* sysinfo registers are read-only so don't drop them */
		return;
	default:
		return;
	}
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_regmap_drop_system_regs, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_regmap_is_dirty() - Test for dirty registers in the cache.
 *
 * @priv:		Pointer to struct cs_dsp_test object.
 * @drop_system_regs:	If true the DSP system regs will be dropped from
 *			the cache before checking for dirty.
 *
 * All registers that are expected to be written must have been dropped
 * from the cache (DSP system registers can be dropped by passing
 * drop_system_regs == true). If any unexpected registers were written
 * there will still be dirty entries in the cache and a cache sync will
 * cause a write.
 *
 * Returns: true if there were dirty entries, false if not.
 */
bool cs_dsp_mock_regmap_is_dirty(struct cs_dsp_test *priv, bool drop_system_regs)
{
	if (drop_system_regs)
		cs_dsp_mock_regmap_drop_system_regs(priv);

	priv->saw_bus_write = false;
	regcache_cache_only(priv->dsp->regmap, false);
	regcache_sync(priv->dsp->regmap);
	regcache_cache_only(priv->dsp->regmap, true);

	return priv->saw_bus_write;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_regmap_is_dirty, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_regmap_init() - Initialize a mock regmap.
 *
 * @priv:	Pointer to struct cs_dsp_test object. This must have a
 *		valid pointer to a struct cs_dsp in which the type and
 *		rev fields are set to the type of DSP to be simulated.
 *
 * On success the priv->dsp->regmap will point to the created
 * regmap instance.
 *
 * Return: zero on success, else negative error code.
 */
int cs_dsp_mock_regmap_init(struct cs_dsp_test *priv)
{
	const struct regmap_config *config;
	int ret;

	switch (priv->dsp->type) {
	case WMFW_HALO:
		config = &cs_dsp_mock_regmap_halo;
		break;
	case WMFW_ADSP2:
		if (priv->dsp->rev == 0)
			config = &cs_dsp_mock_regmap_adsp2_16bit;
		else
			config = &cs_dsp_mock_regmap_adsp2_32bit;
		break;
	default:
		config = NULL;
		break;
	}

	priv->dsp->regmap = devm_regmap_init(priv->dsp->dev,
					     &cs_dsp_mock_regmap_bus,
					     priv,
					     config);
	if (IS_ERR(priv->dsp->regmap)) {
		ret = PTR_ERR(priv->dsp->regmap);
		kunit_err(priv->test, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	/* Put regmap in cache-only so it accumulates the writes done by cs_dsp */
	regcache_cache_only(priv->dsp->regmap, true);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_regmap_init, "FW_CS_DSP_KUNIT_TEST_UTILS");
