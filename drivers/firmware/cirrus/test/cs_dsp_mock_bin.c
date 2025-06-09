// SPDX-License-Identifier: GPL-2.0-only
//
// bin file builder for cs_dsp KUnit tests.
//
// Copyright (C) 2024 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <kunit/resource.h>
#include <kunit/test.h>
#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/cs_dsp_test_utils.h>
#include <linux/firmware/cirrus/wmfw.h>
#include <linux/firmware.h>
#include <linux/math.h>
#include <linux/overflow.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

/* Buffer large enough for bin file content */
#define CS_DSP_MOCK_BIN_BUF_SIZE	32768

KUNIT_DEFINE_ACTION_WRAPPER(vfree_action_wrapper, vfree, void *)

struct cs_dsp_mock_bin_builder {
	struct cs_dsp_test *test_priv;
	void *buf;
	void *write_p;
	size_t bytes_used;
};

/**
 * cs_dsp_mock_bin_get_firmware() - Get struct firmware wrapper for data.
 *
 * @builder:	Pointer to struct cs_dsp_mock_bin_builder.
 *
 * Return: Pointer to a struct firmware wrapper for the data.
 */
struct firmware *cs_dsp_mock_bin_get_firmware(struct cs_dsp_mock_bin_builder *builder)
{
	struct firmware *fw;

	fw = kunit_kzalloc(builder->test_priv->test, sizeof(*fw), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(builder->test_priv->test, fw);

	fw->data = builder->buf;
	fw->size = builder->bytes_used;

	return fw;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_bin_get_firmware, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_bin_add_raw_block() - Add a data block to the bin file.
 *
 * @builder:		Pointer to struct cs_dsp_mock_bin_builder.
 * @alg_id:		Algorithm ID.
 * @alg_ver:		Algorithm version.
 * @type:		Type of the block.
 * @offset:		Offset.
 * @payload_data:	Pointer to buffer containing the payload data.
 * @payload_len_bytes:	Length of payload data in bytes.
 */
void cs_dsp_mock_bin_add_raw_block(struct cs_dsp_mock_bin_builder *builder,
				   unsigned int alg_id, unsigned int alg_ver,
				   int type, unsigned int offset,
				   const void *payload_data, size_t payload_len_bytes)
{
	struct wmfw_coeff_item *item;
	size_t bytes_needed = struct_size_t(struct wmfw_coeff_item, data, payload_len_bytes);

	KUNIT_ASSERT_TRUE(builder->test_priv->test,
			  (builder->write_p + bytes_needed) <
			  (builder->buf + CS_DSP_MOCK_BIN_BUF_SIZE));

	item = builder->write_p;

	item->offset = cpu_to_le16(offset);
	item->type = cpu_to_le16(type);
	item->id = cpu_to_le32(alg_id);
	item->ver = cpu_to_le32(alg_ver << 8);
	item->len = cpu_to_le32(payload_len_bytes);

	if (payload_len_bytes)
		memcpy(item->data, payload_data, payload_len_bytes);

	builder->write_p += bytes_needed;
	builder->bytes_used += bytes_needed;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_bin_add_raw_block, "FW_CS_DSP_KUNIT_TEST_UTILS");

static void cs_dsp_mock_bin_add_name_or_info(struct cs_dsp_mock_bin_builder *builder,
					     const char *info, int type)
{
	size_t info_len = strlen(info);
	char *tmp = NULL;

	if (info_len % 4) {
		/* Create a padded string with length a multiple of 4 */
		size_t copy_len = info_len;
		info_len = round_up(info_len, 4);
		tmp = kunit_kzalloc(builder->test_priv->test, info_len, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(builder->test_priv->test, tmp);
		memcpy(tmp, info, copy_len);
		info = tmp;
	}

	cs_dsp_mock_bin_add_raw_block(builder, 0, 0, WMFW_INFO_TEXT, 0, info, info_len);
	kunit_kfree(builder->test_priv->test, tmp);
}

/**
 * cs_dsp_mock_bin_add_info() - Add an info block to the bin file.
 *
 * @builder:	Pointer to struct cs_dsp_mock_bin_builder.
 * @info:	Pointer to info string to be copied into the file.
 *
 * The string will be padded to a length that is a multiple of 4 bytes.
 */
void cs_dsp_mock_bin_add_info(struct cs_dsp_mock_bin_builder *builder,
			      const char *info)
{
	cs_dsp_mock_bin_add_name_or_info(builder, info, WMFW_INFO_TEXT);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_bin_add_info, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_bin_add_name() - Add a name block to the bin file.
 *
 * @builder:	Pointer to struct cs_dsp_mock_bin_builder.
 * @name:	Pointer to name string to be copied into the file.
 */
void cs_dsp_mock_bin_add_name(struct cs_dsp_mock_bin_builder *builder,
			      const char *name)
{
	cs_dsp_mock_bin_add_name_or_info(builder, name, WMFW_NAME_TEXT);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_bin_add_name, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_bin_add_patch() - Add a patch data block to the bin file.
 *
 * @builder:		Pointer to struct cs_dsp_mock_bin_builder.
 * @alg_id:		Algorithm ID for the patch.
 * @alg_ver:		Algorithm version for the patch.
 * @mem_region:		Memory region for the patch.
 * @reg_addr_offset:	Offset to start of data in register addresses.
 * @payload_data:	Pointer to buffer containing the payload data.
 * @payload_len_bytes:	Length of payload data in bytes.
 */
void cs_dsp_mock_bin_add_patch(struct cs_dsp_mock_bin_builder *builder,
			       unsigned int alg_id, unsigned int alg_ver,
			       int mem_region, unsigned int reg_addr_offset,
			       const void *payload_data, size_t payload_len_bytes)
{
	/* Payload length must be a multiple of 4 */
	KUNIT_ASSERT_EQ(builder->test_priv->test, payload_len_bytes % 4, 0);

	cs_dsp_mock_bin_add_raw_block(builder, alg_id, alg_ver,
				      mem_region, reg_addr_offset,
				      payload_data, payload_len_bytes);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_bin_add_patch, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_bin_init() - Initialize a struct cs_dsp_mock_bin_builder.
 *
 * @priv:		Pointer to struct cs_dsp_test.
 * @format_version:	Required bin format version.
 * @fw_version:		Firmware version to put in bin file.
 *
 * Return: Pointer to created struct cs_dsp_mock_bin_builder.
 */
struct cs_dsp_mock_bin_builder *cs_dsp_mock_bin_init(struct cs_dsp_test *priv,
						     int format_version,
						     unsigned int fw_version)
{
	struct cs_dsp_mock_bin_builder *builder;
	struct wmfw_coeff_hdr *hdr;

	KUNIT_ASSERT_LE(priv->test, format_version, 0xff);
	KUNIT_ASSERT_LE(priv->test, fw_version, 0xffffff);

	builder = kunit_kzalloc(priv->test, sizeof(*builder), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(priv->test, builder);
	builder->test_priv = priv;

	builder->buf = vmalloc(CS_DSP_MOCK_BIN_BUF_SIZE);
	KUNIT_ASSERT_NOT_NULL(priv->test, builder->buf);
	kunit_add_action_or_reset(priv->test, vfree_action_wrapper, builder->buf);

	/* Create header */
	hdr = builder->buf;
	memcpy(hdr->magic, "WMDR", sizeof(hdr->magic));
	hdr->len = cpu_to_le32(offsetof(struct wmfw_coeff_hdr, data));
	hdr->ver = cpu_to_le32(fw_version | (format_version << 24));
	hdr->core_ver = cpu_to_le32(((u32)priv->dsp->type << 24) | priv->dsp->rev);

	builder->write_p = hdr->data;
	builder->bytes_used = offsetof(struct wmfw_coeff_hdr, data);

	return builder;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_bin_init, "FW_CS_DSP_KUNIT_TEST_UTILS");
