// SPDX-License-Identifier: GPL-2.0-only
//
// Mock DSP memory maps for cs_dsp KUnit tests.
//
// Copyright (C) 2024 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <kunit/test.h>
#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/cs_dsp_test_utils.h>
#include <linux/firmware/cirrus/wmfw.h>
#include <linux/math.h>

const struct cs_dsp_region cs_dsp_mock_halo_dsp1_regions[] = {
	{ .type = WMFW_HALO_PM_PACKED,	.base = 0x3800000 },
	{ .type = WMFW_HALO_XM_PACKED,	.base = 0x2000000 },
	{ .type = WMFW_HALO_YM_PACKED,	.base = 0x2C00000 },
	{ .type = WMFW_ADSP2_XM,	.base = 0x2800000 },
	{ .type = WMFW_ADSP2_YM,	.base = 0x3400000 },
};
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_halo_dsp1_regions, "FW_CS_DSP_KUNIT_TEST_UTILS");

/*  List of sizes in bytes, for each entry above */
const unsigned int cs_dsp_mock_halo_dsp1_region_sizes[] = {
	0x5000,		/* PM_PACKED */
	0x6000,		/* XM_PACKED */
	0x47F4,		/* YM_PACKED */
	0x8000,		/* XM_UNPACKED_24 */
	0x5FF8,		/* YM_UNPACKED_24 */

	0		/* terminator */
};
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_halo_dsp1_region_sizes, "FW_CS_DSP_KUNIT_TEST_UTILS");

const struct cs_dsp_region cs_dsp_mock_adsp2_32bit_dsp1_regions[] = {
	{ .type = WMFW_ADSP2_PM, .base = 0x080000 },
	{ .type = WMFW_ADSP2_XM, .base = 0x0a0000 },
	{ .type = WMFW_ADSP2_YM, .base = 0x0c0000 },
	{ .type = WMFW_ADSP2_ZM, .base = 0x0e0000 },
};
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_adsp2_32bit_dsp1_regions, "FW_CS_DSP_KUNIT_TEST_UTILS");

/* List of sizes in bytes, for each entry above */
const unsigned int cs_dsp_mock_adsp2_32bit_dsp1_region_sizes[] = {
	0x9000,	/* PM */
	0xa000,	/* ZM */
	0x2000,	/* XM */
	0x2000,	/* YM */

	0	/* terminator */
};
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_adsp2_32bit_dsp1_region_sizes, "FW_CS_DSP_KUNIT_TEST_UTILS");

const struct cs_dsp_region cs_dsp_mock_adsp2_16bit_dsp1_regions[] = {
	{ .type = WMFW_ADSP2_PM, .base = 0x100000 },
	{ .type = WMFW_ADSP2_ZM, .base = 0x180000 },
	{ .type = WMFW_ADSP2_XM, .base = 0x190000 },
	{ .type = WMFW_ADSP2_YM, .base = 0x1a8000 },
};
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_adsp2_16bit_dsp1_regions, "FW_CS_DSP_KUNIT_TEST_UTILS");

/* List of sizes in bytes, for each entry above */
const unsigned int cs_dsp_mock_adsp2_16bit_dsp1_region_sizes[] = {
	0x6000,	/* PM */
	0x800,	/* ZM */
	0x800,	/* XM */
	0x800,	/* YM */

	0	/* terminator */
};
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_adsp2_16bit_dsp1_region_sizes, "FW_CS_DSP_KUNIT_TEST_UTILS");

int cs_dsp_mock_count_regions(const unsigned int *region_sizes)
{
	int i;

	for (i = 0; region_sizes[i]; ++i)
		;

	return i;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_count_regions, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_size_of_region() - Return size of given memory region.
 *
 * @dsp:	Pointer to struct cs_dsp.
 * @mem_type:	Memory region type.
 *
 * Return: Size of region in bytes.
 */
unsigned int cs_dsp_mock_size_of_region(const struct cs_dsp *dsp, int mem_type)
{
	const unsigned int *sizes;
	int i;

	if (dsp->mem == cs_dsp_mock_halo_dsp1_regions)
		sizes = cs_dsp_mock_halo_dsp1_region_sizes;
	else if (dsp->mem == cs_dsp_mock_adsp2_32bit_dsp1_regions)
		sizes = cs_dsp_mock_adsp2_32bit_dsp1_region_sizes;
	else if (dsp->mem == cs_dsp_mock_adsp2_16bit_dsp1_regions)
		sizes = cs_dsp_mock_adsp2_16bit_dsp1_region_sizes;
	else
		return 0;

	for (i = 0; i < dsp->num_mems; ++i) {
		if (dsp->mem[i].type == mem_type)
			return sizes[i];
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_size_of_region, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_base_addr_for_mem() - Base register address for memory region.
 *
 * @priv:	Pointer to struct cs_dsp_test.
 * @mem_type:	Memory region type.
 *
 * Return: Base register address of region.
 */
unsigned int cs_dsp_mock_base_addr_for_mem(struct cs_dsp_test *priv, int mem_type)
{
	int num_mems = priv->dsp->num_mems;
	const struct cs_dsp_region *region = priv->dsp->mem;
	int i;

	for (i = 0; i < num_mems; ++i) {
		if (region[i].type == mem_type)
			return region[i].base;
	}

	KUNIT_FAIL(priv->test, "Unexpected region %d\n", mem_type);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_base_addr_for_mem, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_reg_addr_inc_per_unpacked_word() - Unpacked register address increment per DSP word.
 *
 * @priv:	Pointer to struct cs_dsp_test.
 *
 * Return: Amount by which register address increments to move to the next
 *	   DSP word in unpacked XM/YM/ZM.
 */
unsigned int cs_dsp_mock_reg_addr_inc_per_unpacked_word(struct cs_dsp_test *priv)
{
	switch (priv->dsp->type) {
	case WMFW_ADSP2:
		return 2; /* two 16-bit register indexes per XM/YM/ZM word */
	case WMFW_HALO:
		return 4; /* one byte-addressed 32-bit register per XM/YM/ZM word */
	default:
		KUNIT_FAIL(priv->test, "Unexpected DSP type\n");
		return -1;
	}
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_reg_addr_inc_per_unpacked_word, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_reg_block_length_bytes() - Number of bytes in an access block.
 *
 * @priv:	Pointer to struct cs_dsp_test.
 * @mem_type:	Memory region type.
 *
 * Return: Total number of bytes in a group of registers forming the
 * smallest bus access size (including any padding bits). For unpacked
 * memory this is the number of registers containing one DSP word.
 * For packed memory this is the number of registers in one packed
 * access block.
 */
unsigned int cs_dsp_mock_reg_block_length_bytes(struct cs_dsp_test *priv, int mem_type)
{
	switch (priv->dsp->type) {
	case WMFW_ADSP2:
		switch (mem_type) {
		case WMFW_ADSP2_PM:
			return 3 * regmap_get_val_bytes(priv->dsp->regmap);
		case WMFW_ADSP2_XM:
		case WMFW_ADSP2_YM:
		case WMFW_ADSP2_ZM:
			return sizeof(u32);
		default:
			break;
		}
		break;
	case WMFW_HALO:
		switch (mem_type) {
		case WMFW_ADSP2_XM:
		case WMFW_ADSP2_YM:
			return sizeof(u32);
		case WMFW_HALO_PM_PACKED:
			return 5 * sizeof(u32);
		case WMFW_HALO_XM_PACKED:
		case WMFW_HALO_YM_PACKED:
			return 3 * sizeof(u32);
		default:
			break;
		}
		break;
	default:
		KUNIT_FAIL(priv->test, "Unexpected DSP type\n");
		return 0;
	}

	KUNIT_FAIL(priv->test, "Unexpected mem type\n");

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_reg_block_length_bytes, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_reg_block_length_registers() - Number of registers in an access block.
 *
 * @priv:	Pointer to struct cs_dsp_test.
 * @mem_type:	Memory region type.
 *
 * Return: Total number of register forming the smallest bus access size.
 * For unpacked memory this is the number of registers containing one
 * DSP word. For packed memory this is the number of registers in one
 * packed access block.
 */
unsigned int cs_dsp_mock_reg_block_length_registers(struct cs_dsp_test *priv, int mem_type)
{
	return cs_dsp_mock_reg_block_length_bytes(priv, mem_type) /
	       regmap_get_val_bytes(priv->dsp->regmap);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_reg_block_length_registers, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_reg_block_length_dsp_words() - Number of dsp_words in an access block.
 *
 * @priv:	Pointer to struct cs_dsp_test.
 * @mem_type:	Memory region type.
 *
 * Return: Total number of DSP words in a group of registers forming the
 * smallest bus access size.
 */
unsigned int cs_dsp_mock_reg_block_length_dsp_words(struct cs_dsp_test *priv, int mem_type)
{
	switch (priv->dsp->type) {
	case WMFW_ADSP2:
		switch (mem_type) {
		case WMFW_ADSP2_PM:
			return regmap_get_val_bytes(priv->dsp->regmap) / 2;
		case WMFW_ADSP2_XM:
		case WMFW_ADSP2_YM:
		case WMFW_ADSP2_ZM:
			return 1;
		default:
			break;
		}
		break;
	case WMFW_HALO:
		switch (mem_type) {
		case WMFW_ADSP2_XM:
		case WMFW_ADSP2_YM:
			return 1;
		case WMFW_HALO_PM_PACKED:
		case WMFW_HALO_XM_PACKED:
		case WMFW_HALO_YM_PACKED:
			return 4;
		default:
			break;
		}
		break;
	default:
		KUNIT_FAIL(priv->test, "Unexpected DSP type\n");
		return 0;
	}

	KUNIT_FAIL(priv->test, "Unexpected mem type\n");

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_reg_block_length_dsp_words, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_has_zm() - DSP has ZM
 *
 * @priv:	Pointer to struct cs_dsp_test.
 *
 * Return: True if DSP has ZM.
 */
bool cs_dsp_mock_has_zm(struct cs_dsp_test *priv)
{
	switch (priv->dsp->type) {
	case WMFW_ADSP2:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_has_zm, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_packed_to_unpacked_mem_type() - Unpacked region that is
 * the same memory as a packed region.
 *
 * @packed_mem_type:	Type of packed memory region.
 *
 * Return: unpacked type that is the same memory as packed_mem_type.
 */
int cs_dsp_mock_packed_to_unpacked_mem_type(int packed_mem_type)
{
	switch (packed_mem_type) {
	case WMFW_HALO_XM_PACKED:
		return WMFW_ADSP2_XM;
	case WMFW_HALO_YM_PACKED:
		return WMFW_ADSP2_YM;
	default:
		return -1;
	}
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_packed_to_unpacked_mem_type, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_num_dsp_words_to_num_packed_regs() - Number of DSP words
 * to number of packed registers.
 *
 * @num_dsp_words:	Number of DSP words.
 *
 * Convert number of DSP words to number of packed registers rounded
 * down to the nearest register.
 *
 * Return: Number of packed registers.
 */
unsigned int cs_dsp_mock_num_dsp_words_to_num_packed_regs(unsigned int num_dsp_words)
{
	/* There are 3 registers for every 4 packed words */
	return (num_dsp_words * 3) / 4;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_num_dsp_words_to_num_packed_regs, "FW_CS_DSP_KUNIT_TEST_UTILS");

static const struct wmfw_halo_id_hdr cs_dsp_mock_halo_xm_hdr = {
	.fw = {
		.core_id = cpu_to_be32(WMFW_HALO << 16),
		.block_rev = cpu_to_be32(3 << 16),
		.vendor_id = cpu_to_be32(0x2),
		.id = cpu_to_be32(0xabcdef),
		.ver = cpu_to_be32(0x090101),
	},

	/*
	 * Leave enough space for this header and 40 algorithm descriptors.
	 * base and size are counted in DSP words.
	 */
	.xm_base = cpu_to_be32(((sizeof(struct wmfw_halo_id_hdr) +
				(40 * sizeof(struct wmfw_halo_alg_hdr)))
				/ 4) * 3),
	.xm_size = cpu_to_be32(0x20),

	/* Allocate a dummy word of YM */
	.ym_base = cpu_to_be32(0),
	.ym_size = cpu_to_be32(1),

	.n_algs = 0,
};

static const struct wmfw_adsp2_id_hdr cs_dsp_mock_adsp2_xm_hdr = {
	.fw = {
		.core_id = cpu_to_be32(WMFW_ADSP2 << 16),
		.core_rev = cpu_to_be32(2 << 16),
		.id = cpu_to_be32(0xabcdef),
		.ver = cpu_to_be32(0x090101),
	},

	/*
	 * Leave enough space for this header and 40 algorithm descriptors.
	 * base and size are counted in DSP words.
	 */
	.xm = cpu_to_be32(((sizeof(struct wmfw_adsp2_id_hdr) +
				(40 * sizeof(struct wmfw_adsp2_alg_hdr)))
				/ 4) * 3),

	.ym = cpu_to_be32(0),
	.zm = cpu_to_be32(0),

	.n_algs = 0,
};

/**
 * cs_dsp_mock_xm_header_get_alg_base_in_words() - Algorithm base offset in DSP words.
 *
 * @priv:	Pointer to struct cs_dsp_test.
 * @alg_id:	Algorithm ID.
 * @mem_type:	Memory region type.
 *
 * Lookup an algorithm in the XM header and return the base offset in
 * DSP words of the algorithm data in the requested memory region.
 *
 * Return: Offset in DSP words.
 */
unsigned int cs_dsp_mock_xm_header_get_alg_base_in_words(struct cs_dsp_test *priv,
							 unsigned int alg_id,
							 int mem_type)
{
	unsigned int xm = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_XM);
	union {
		struct wmfw_adsp2_alg_hdr adsp2;
		struct wmfw_halo_alg_hdr halo;
	} alg;
	unsigned int alg_hdr_addr;
	unsigned int val, xm_base = 0, ym_base = 0, zm_base = 0;
	int ret;

	switch (priv->dsp->type) {
	case WMFW_ADSP2:
		alg_hdr_addr = xm + (sizeof(struct wmfw_adsp2_id_hdr) / 2);
		for (;; alg_hdr_addr += sizeof(alg.adsp2) / 2) {
			ret = regmap_read(priv->dsp->regmap, alg_hdr_addr, &val);
			KUNIT_ASSERT_GE(priv->test, ret, 0);
			KUNIT_ASSERT_NE(priv->test, val, 0xbedead);
			ret = regmap_raw_read(priv->dsp->regmap, alg_hdr_addr,
					      &alg.adsp2, sizeof(alg.adsp2));
			KUNIT_ASSERT_GE(priv->test, ret, 0);
			if (be32_to_cpu(alg.adsp2.alg.id) == alg_id) {
				xm_base = be32_to_cpu(alg.adsp2.xm);
				ym_base = be32_to_cpu(alg.adsp2.ym);
				zm_base = be32_to_cpu(alg.adsp2.zm);
				break;
			}
		}
		break;
	case WMFW_HALO:
		alg_hdr_addr = xm + sizeof(struct wmfw_halo_id_hdr);
		for (;; alg_hdr_addr += sizeof(alg.halo)) {
			ret = regmap_read(priv->dsp->regmap, alg_hdr_addr, &val);
			KUNIT_ASSERT_GE(priv->test, ret, 0);
			KUNIT_ASSERT_NE(priv->test, val, 0xbedead);
			ret = regmap_raw_read(priv->dsp->regmap, alg_hdr_addr,
					      &alg.halo, sizeof(alg.halo));
			KUNIT_ASSERT_GE(priv->test, ret, 0);
			if (be32_to_cpu(alg.halo.alg.id) == alg_id) {
				xm_base = be32_to_cpu(alg.halo.xm_base);
				ym_base = be32_to_cpu(alg.halo.ym_base);
				break;
			}
		}
		break;
	default:
		KUNIT_FAIL(priv->test, "Unexpected DSP type %d\n", priv->dsp->type);
		return 0;
	}

	switch (mem_type) {
	case WMFW_ADSP2_XM:
	case WMFW_HALO_XM_PACKED:
		return xm_base;
	case WMFW_ADSP2_YM:
	case WMFW_HALO_YM_PACKED:
		return ym_base;
	case WMFW_ADSP2_ZM:
		return zm_base;
	default:
		KUNIT_FAIL(priv->test, "Bad mem_type\n");
		return 0;
	}
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_xm_header_get_alg_base_in_words, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_xm_header_get_fw_version() - Firmware version.
 *
 * @header:	Pointer to struct cs_dsp_mock_xm_header.
 *
 * Return: Firmware version word value.
 */
unsigned int cs_dsp_mock_xm_header_get_fw_version(struct cs_dsp_mock_xm_header *header)
{
	const struct wmfw_id_hdr *adsp2_hdr;
	const struct wmfw_v3_id_hdr *halo_hdr;

	switch (header->test_priv->dsp->type) {
	case WMFW_ADSP2:
		adsp2_hdr = header->blob_data;
		return be32_to_cpu(adsp2_hdr->ver);
	case WMFW_HALO:
		halo_hdr = header->blob_data;
		return be32_to_cpu(halo_hdr->ver);
	default:
		KUNIT_FAIL(header->test_priv->test, NULL);
		return 0;
	}
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_xm_header_get_fw_version, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_xm_header_drop_from_regmap_cache() - Drop XM header from regmap cache.
 *
 * @priv:	Pointer to struct cs_dsp_test.
 */
void cs_dsp_mock_xm_header_drop_from_regmap_cache(struct cs_dsp_test *priv)
{
	unsigned int xm = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_XM);
	unsigned int bytes;
	__be32 num_algs_be32;
	unsigned int num_algs;

	switch (priv->dsp->type) {
	case WMFW_ADSP2:
		/*
		 * Could be one 32-bit register or two 16-bit registers.
		 * A raw read will read the requested number of bytes.
		 */
		KUNIT_ASSERT_GE(priv->test, 0,
				regmap_raw_read(priv->dsp->regmap,
						xm +
						(offsetof(struct wmfw_adsp2_id_hdr, n_algs) / 2),
						&num_algs_be32, sizeof(num_algs_be32)));
		num_algs = be32_to_cpu(num_algs_be32);
		bytes = sizeof(struct wmfw_adsp2_id_hdr) +
			(num_algs * sizeof(struct wmfw_adsp2_alg_hdr)) +
			4 /* terminator word */;

		regcache_drop_region(priv->dsp->regmap, xm, xm + (bytes / 2) - 1);
		break;
	case WMFW_HALO:
		KUNIT_ASSERT_GE(priv->test, 0,
				regmap_read(priv->dsp->regmap,
					    xm + offsetof(struct wmfw_halo_id_hdr, n_algs),
					    &num_algs));
		bytes = sizeof(struct wmfw_halo_id_hdr) +
			(num_algs * sizeof(struct wmfw_halo_alg_hdr)) +
			4 /* terminator word */;

		regcache_drop_region(priv->dsp->regmap, xm, xm + bytes - 4);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_xm_header_drop_from_regmap_cache, "FW_CS_DSP_KUNIT_TEST_UTILS");

static void cs_dsp_mock_xm_header_add_adsp2_algs(struct cs_dsp_mock_xm_header *builder,
						 const struct cs_dsp_mock_alg_def *algs,
						 size_t num_algs)
{
	struct wmfw_adsp2_id_hdr *hdr = builder->blob_data;
	unsigned int next_free_xm_word, next_free_ym_word, next_free_zm_word;

	next_free_xm_word = be32_to_cpu(hdr->xm);
	next_free_ym_word = be32_to_cpu(hdr->ym);
	next_free_zm_word = be32_to_cpu(hdr->zm);

	/* Set num_algs in XM header. */
	hdr->n_algs = cpu_to_be32(num_algs);

	/* Create algorithm descriptor list */
	struct wmfw_adsp2_alg_hdr *alg_info =
			(struct wmfw_adsp2_alg_hdr *)(&hdr[1]);

	for (; num_algs > 0; num_algs--, algs++, alg_info++) {
		unsigned int alg_xm_last, alg_ym_last, alg_zm_last;

		alg_info->alg.id = cpu_to_be32(algs->id);
		alg_info->alg.ver = cpu_to_be32(algs->ver);
		alg_info->xm = cpu_to_be32(algs->xm_base_words);
		alg_info->ym = cpu_to_be32(algs->ym_base_words);
		alg_info->zm = cpu_to_be32(algs->zm_base_words);

		/* Check if we need to auto-allocate base addresses */
		if (!alg_info->xm && algs->xm_size_words)
			alg_info->xm = cpu_to_be32(next_free_xm_word);

		if (!alg_info->ym && algs->ym_size_words)
			alg_info->ym = cpu_to_be32(next_free_ym_word);

		if (!alg_info->zm && algs->zm_size_words)
			alg_info->zm = cpu_to_be32(next_free_zm_word);

		alg_xm_last = be32_to_cpu(alg_info->xm) + algs->xm_size_words - 1;
		if (alg_xm_last > next_free_xm_word)
			next_free_xm_word = alg_xm_last;

		alg_ym_last = be32_to_cpu(alg_info->ym) + algs->ym_size_words - 1;
		if (alg_ym_last > next_free_ym_word)
			next_free_ym_word = alg_ym_last;

		alg_zm_last = be32_to_cpu(alg_info->zm) + algs->zm_size_words - 1;
		if (alg_zm_last > next_free_zm_word)
			next_free_zm_word = alg_zm_last;
	}

	/* Write list terminator */
	*(__be32 *)(alg_info) = cpu_to_be32(0xbedead);
}

static void cs_dsp_mock_xm_header_add_halo_algs(struct cs_dsp_mock_xm_header *builder,
						const struct cs_dsp_mock_alg_def *algs,
						size_t num_algs)
{
	struct wmfw_halo_id_hdr *hdr = builder->blob_data;
	unsigned int next_free_xm_word, next_free_ym_word;

	/* Assume we're starting with bare header */
	next_free_xm_word = be32_to_cpu(hdr->xm_base) + be32_to_cpu(hdr->xm_size) - 1;
	next_free_ym_word = be32_to_cpu(hdr->ym_base) + be32_to_cpu(hdr->ym_size) - 1;

	/* Set num_algs in XM header */
	hdr->n_algs = cpu_to_be32(num_algs);

	/* Create algorithm descriptor list */
	struct wmfw_halo_alg_hdr *alg_info =
			(struct wmfw_halo_alg_hdr *)(&hdr[1]);

	for (; num_algs > 0; num_algs--, algs++, alg_info++) {
		unsigned int alg_xm_last, alg_ym_last;

		alg_info->alg.id = cpu_to_be32(algs->id);
		alg_info->alg.ver = cpu_to_be32(algs->ver);
		alg_info->xm_base = cpu_to_be32(algs->xm_base_words);
		alg_info->xm_size = cpu_to_be32(algs->xm_size_words);
		alg_info->ym_base = cpu_to_be32(algs->ym_base_words);
		alg_info->ym_size = cpu_to_be32(algs->ym_size_words);

		/* Check if we need to auto-allocate base addresses */
		if (!alg_info->xm_base && alg_info->xm_size)
			alg_info->xm_base = cpu_to_be32(next_free_xm_word);

		if (!alg_info->ym_base && alg_info->ym_size)
			alg_info->ym_base = cpu_to_be32(next_free_ym_word);

		alg_xm_last = be32_to_cpu(alg_info->xm_base) + be32_to_cpu(alg_info->xm_size) - 1;
		if (alg_xm_last > next_free_xm_word)
			next_free_xm_word = alg_xm_last;

		alg_ym_last = be32_to_cpu(alg_info->ym_base) + be32_to_cpu(alg_info->ym_size) - 1;
		if (alg_ym_last > next_free_ym_word)
			next_free_ym_word = alg_ym_last;
	}

	/* Write list terminator */
	*(__be32 *)(alg_info) = cpu_to_be32(0xbedead);
}

/**
 * cs_dsp_mock_xm_header_write_to_regmap() - Write XM header to regmap.
 *
 * @header:	Pointer to struct cs_dsp_mock_xm_header.
 *
 * The data in header is written to the XM addresses in the regmap.
 *
 * Return: 0 on success, else negative error code.
 */
int cs_dsp_mock_xm_header_write_to_regmap(struct cs_dsp_mock_xm_header *header)
{
	struct cs_dsp_test *priv = header->test_priv;
	unsigned int reg_addr = cs_dsp_mock_base_addr_for_mem(priv, WMFW_ADSP2_XM);

	/*
	 * One 32-bit word corresponds to one 32-bit unpacked XM word so the
	 * blob can be written directly to the regmap.
	 */
	return regmap_raw_write(priv->dsp->regmap, reg_addr,
				header->blob_data, header->blob_size_bytes);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_xm_header_write_to_regmap, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_create_mock_xm_header() - Create a dummy XM header.
 *
 * @priv:	Pointer to struct cs_dsp_test.
 * @algs:	Pointer to array of struct cs_dsp_mock_alg_def listing the
 *		dummy algorithm entries to include in the XM header.
 * @num_algs:	Number of entries in the algs array.
 *
 * Return: Pointer to created struct cs_dsp_mock_xm_header.
 */
struct cs_dsp_mock_xm_header *cs_dsp_create_mock_xm_header(struct cs_dsp_test *priv,
							   const struct cs_dsp_mock_alg_def *algs,
							   size_t num_algs)
{
	struct cs_dsp_mock_xm_header *builder;
	size_t total_bytes_required;
	const void *header;
	size_t header_size_bytes;

	builder = kunit_kzalloc(priv->test, sizeof(*builder), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(priv->test, builder);
	builder->test_priv = priv;

	switch (priv->dsp->type) {
	case WMFW_ADSP2:
		header = &cs_dsp_mock_adsp2_xm_hdr;
		header_size_bytes = sizeof(cs_dsp_mock_adsp2_xm_hdr);
		total_bytes_required = header_size_bytes +
				       (num_algs * sizeof(struct wmfw_adsp2_alg_hdr))
				       + 4; /* terminator word */
		break;
	case WMFW_HALO:
		header = &cs_dsp_mock_halo_xm_hdr,
		header_size_bytes = sizeof(cs_dsp_mock_halo_xm_hdr);
		total_bytes_required = header_size_bytes +
				       (num_algs * sizeof(struct wmfw_halo_alg_hdr))
				       + 4; /* terminator word */
		break;
	default:
		KUNIT_FAIL(priv->test, "%s unexpected DSP type %d\n",
			   __func__, priv->dsp->type);
		return NULL;
	}

	builder->blob_data = kunit_kzalloc(priv->test, total_bytes_required, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(priv->test, builder->blob_data);
	builder->blob_size_bytes = total_bytes_required;

	memcpy(builder->blob_data, header, header_size_bytes);

	switch (priv->dsp->type) {
	case WMFW_ADSP2:
		cs_dsp_mock_xm_header_add_adsp2_algs(builder, algs, num_algs);
		break;
	case WMFW_HALO:
		cs_dsp_mock_xm_header_add_halo_algs(builder, algs, num_algs);
		break;
	default:
		break;
	}

	return builder;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_create_mock_xm_header, "FW_CS_DSP_KUNIT_TEST_UTILS");
