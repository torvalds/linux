// SPDX-License-Identifier: GPL-2.0-only
//
// wmfw file builder for cs_dsp KUnit tests.
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
#define CS_DSP_MOCK_WMFW_BUF_SIZE	131072

struct cs_dsp_mock_wmfw_builder {
	struct cs_dsp_test *test_priv;
	int format_version;
	void *buf;
	size_t buf_size_bytes;
	void *write_p;
	size_t bytes_used;

	void *alg_data_header;
	unsigned int num_coeffs;
};

struct wmfw_adsp2_halo_header {
	struct wmfw_header header;
	struct wmfw_adsp2_sizes sizes;
	struct wmfw_footer footer;
} __packed;

struct wmfw_long_string {
	__le16 len;
	u8 data[] __nonstring __counted_by(len);
} __packed;

struct wmfw_short_string {
	u8 len;
	u8 data[] __nonstring __counted_by(len);
} __packed;

KUNIT_DEFINE_ACTION_WRAPPER(vfree_action_wrapper, vfree, void *)

/**
 * cs_dsp_mock_wmfw_format_version() - Return format version.
 *
 * @builder:	Pointer to struct cs_dsp_mock_wmfw_builder.
 *
 * Return: Format version.
 */
int cs_dsp_mock_wmfw_format_version(struct cs_dsp_mock_wmfw_builder *builder)
{
	return builder->format_version;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_wmfw_format_version, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_wmfw_get_firmware() - Get struct firmware wrapper for data.
 *
 * @builder:	Pointer to struct cs_dsp_mock_wmfw_builder.
 *
 * Return: Pointer to a struct firmware wrapper for the data.
 */
struct firmware *cs_dsp_mock_wmfw_get_firmware(struct cs_dsp_mock_wmfw_builder *builder)
{
	struct firmware *fw;

	if (!builder)
		return NULL;

	fw = kunit_kzalloc(builder->test_priv->test, sizeof(*fw), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(builder->test_priv->test, fw);

	fw->data = builder->buf;
	fw->size = builder->bytes_used;

	return fw;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_wmfw_get_firmware, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_wmfw_add_raw_block() - Add a block to the wmfw file.
 *
 * @builder:		Pointer to struct cs_dsp_mock_bin_builder.
 * @block_type:		Block type.
 * @offset:		Offset.
 * @payload_data:	Pointer to buffer containing the payload data,
 *			or NULL if no data.
 * @payload_len_bytes:	Length of payload data in bytes, or zero.
 */
void cs_dsp_mock_wmfw_add_raw_block(struct cs_dsp_mock_wmfw_builder *builder,
				    int block_type, unsigned int offset,
				    const void *payload_data, size_t payload_len_bytes)
{
	struct wmfw_region *header = builder->write_p;
	unsigned int bytes_needed = struct_size_t(struct wmfw_region, data, payload_len_bytes);

	KUNIT_ASSERT_TRUE(builder->test_priv->test,
			  (builder->write_p + bytes_needed) <
			  (builder->buf + CS_DSP_MOCK_WMFW_BUF_SIZE));

	header->offset = cpu_to_le32(offset | (block_type << 24));
	header->len = cpu_to_le32(payload_len_bytes);
	if (payload_len_bytes > 0)
		memcpy(header->data, payload_data, payload_len_bytes);

	builder->write_p += bytes_needed;
	builder->bytes_used += bytes_needed;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_wmfw_add_raw_block, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_wmfw_add_info() - Add an info block to the wmfw file.
 *
 * @builder:	Pointer to struct cs_dsp_mock_bin_builder.
 * @info:	Pointer to info string to be copied into the file.
 *
 * The string will be padded to a length that is a multiple of 4 bytes.
 */
void cs_dsp_mock_wmfw_add_info(struct cs_dsp_mock_wmfw_builder *builder,
			       const char *info)
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

	cs_dsp_mock_wmfw_add_raw_block(builder, WMFW_INFO_TEXT, 0, info, info_len);
	kunit_kfree(builder->test_priv->test, tmp);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_wmfw_add_info, "FW_CS_DSP_KUNIT_TEST_UTILS");

/**
 * cs_dsp_mock_wmfw_add_data_block() - Add a data block to the wmfw file.
 *
 * @builder:		  Pointer to struct cs_dsp_mock_bin_builder.
 * @mem_region:		  Memory region for the block.
 * @mem_offset_dsp_words: Offset to start of destination in DSP words.
 * @payload_data:	  Pointer to buffer containing the payload data.
 * @payload_len_bytes:	  Length of payload data in bytes.
 */
void cs_dsp_mock_wmfw_add_data_block(struct cs_dsp_mock_wmfw_builder *builder,
				     int mem_region, unsigned int mem_offset_dsp_words,
				     const void *payload_data, size_t payload_len_bytes)
{
	/* Blob payload length must be a multiple of 4 */
	KUNIT_ASSERT_EQ(builder->test_priv->test, payload_len_bytes % 4, 0);

	cs_dsp_mock_wmfw_add_raw_block(builder, mem_region, mem_offset_dsp_words,
				       payload_data, payload_len_bytes);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_wmfw_add_data_block, "FW_CS_DSP_KUNIT_TEST_UTILS");

void cs_dsp_mock_wmfw_start_alg_info_block(struct cs_dsp_mock_wmfw_builder *builder,
					   unsigned int alg_id,
					   const char *name,
					   const char *description)
{
	struct wmfw_region *rgn = builder->write_p;
	struct wmfw_adsp_alg_data *v1;
	struct wmfw_short_string *shortstring;
	struct wmfw_long_string *longstring;
	size_t bytes_needed, name_len, description_len;
	int offset;

	KUNIT_ASSERT_LE(builder->test_priv->test, alg_id, 0xffffff);

	/* Bytes needed for region header */
	bytes_needed = offsetof(struct wmfw_region, data);

	builder->alg_data_header = builder->write_p;
	builder->num_coeffs = 0;

	switch (builder->format_version) {
	case 0:
		KUNIT_FAIL(builder->test_priv->test, "wmfwV0 does not have alg blocks\n");
		return;
	case 1:
		bytes_needed += offsetof(struct wmfw_adsp_alg_data, data);
		KUNIT_ASSERT_TRUE(builder->test_priv->test,
				  (builder->write_p + bytes_needed) <
				  (builder->buf + CS_DSP_MOCK_WMFW_BUF_SIZE));

		memset(builder->write_p, 0, bytes_needed);

		/* Create region header */
		rgn->offset = cpu_to_le32(WMFW_ALGORITHM_DATA << 24);

		/* Create algorithm entry */
		v1 = (struct wmfw_adsp_alg_data *)&rgn->data[0];
		v1->id = cpu_to_le32(alg_id);
		if (name)
			strscpy(v1->name, name, sizeof(v1->name));

		if (description)
			strscpy(v1->descr, description, sizeof(v1->descr));
		break;
	default:
		name_len = 0;
		description_len = 0;

		if (name)
			name_len = strlen(name);

		if (description)
			description_len = strlen(description);

		bytes_needed += sizeof(__le32); /* alg id */
		bytes_needed += round_up(name_len + sizeof(u8), sizeof(__le32));
		bytes_needed += round_up(description_len + sizeof(__le16), sizeof(__le32));
		bytes_needed += sizeof(__le32); /* coeff count */

		KUNIT_ASSERT_TRUE(builder->test_priv->test,
				  (builder->write_p + bytes_needed) <
				  (builder->buf + CS_DSP_MOCK_WMFW_BUF_SIZE));

		memset(builder->write_p, 0, bytes_needed);

		/* Create region header */
		rgn->offset = cpu_to_le32(WMFW_ALGORITHM_DATA << 24);

		/* Create algorithm entry */
		*(__force __le32 *)&rgn->data[0] = cpu_to_le32(alg_id);

		shortstring = (struct wmfw_short_string *)&rgn->data[4];
		shortstring->len = name_len;

		if (name_len)
			memcpy(shortstring->data, name, name_len);

		/* Round up to next __le32 */
		offset = round_up(4 + struct_size_t(struct wmfw_short_string, data, name_len),
				  sizeof(__le32));

		longstring = (struct wmfw_long_string *)&rgn->data[offset];
		longstring->len = cpu_to_le16(description_len);

		if (description_len)
			memcpy(longstring->data, description, description_len);
		break;
	}

	builder->write_p += bytes_needed;
	builder->bytes_used += bytes_needed;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_wmfw_start_alg_info_block, "FW_CS_DSP_KUNIT_TEST_UTILS");

void cs_dsp_mock_wmfw_add_coeff_desc(struct cs_dsp_mock_wmfw_builder *builder,
				     const struct cs_dsp_mock_coeff_def *def)
{
	struct wmfw_adsp_coeff_data *v1;
	struct wmfw_short_string *shortstring;
	struct wmfw_long_string *longstring;
	size_t bytes_needed, shortname_len, fullname_len, description_len;
	__le32 *ple32;

	KUNIT_ASSERT_NOT_NULL(builder->test_priv->test, builder->alg_data_header);

	switch (builder->format_version) {
	case 0:
		return;
	case 1:
		bytes_needed = offsetof(struct wmfw_adsp_coeff_data, data);
		KUNIT_ASSERT_TRUE(builder->test_priv->test,
				  (builder->write_p + bytes_needed) <
				  (builder->buf + CS_DSP_MOCK_WMFW_BUF_SIZE));

		v1 = (struct wmfw_adsp_coeff_data *)builder->write_p;
		memset(v1, 0, sizeof(*v1));
		v1->hdr.offset = cpu_to_le16(def->offset_dsp_words);
		v1->hdr.type = cpu_to_le16(def->mem_type);
		v1->hdr.size = cpu_to_le32(bytes_needed - sizeof(v1->hdr));
		v1->ctl_type = cpu_to_le16(def->type);
		v1->flags = cpu_to_le16(def->flags);
		v1->len = cpu_to_le32(def->length_bytes);

		if (def->fullname)
			strscpy(v1->name, def->fullname, sizeof(v1->name));

		if (def->description)
			strscpy(v1->descr, def->description, sizeof(v1->descr));
		break;
	default:
		fullname_len = 0;
		description_len = 0;
		shortname_len = strlen(def->shortname);

		if (def->fullname)
			fullname_len = strlen(def->fullname);

		if (def->description)
			description_len = strlen(def->description);

		bytes_needed = sizeof(__le32) * 2; /* type, offset and size */
		bytes_needed += round_up(shortname_len + sizeof(u8), sizeof(__le32));
		bytes_needed += round_up(fullname_len + sizeof(u8), sizeof(__le32));
		bytes_needed += round_up(description_len + sizeof(__le16), sizeof(__le32));
		bytes_needed += sizeof(__le32) * 2; /* flags, type and length */
		KUNIT_ASSERT_TRUE(builder->test_priv->test,
				  (builder->write_p + bytes_needed) <
				  (builder->buf + CS_DSP_MOCK_WMFW_BUF_SIZE));

		ple32 = (__force __le32 *)builder->write_p;
		*ple32++ = cpu_to_le32(def->offset_dsp_words | (def->mem_type << 16));
		*ple32++ = cpu_to_le32(bytes_needed - sizeof(__le32) - sizeof(__le32));

		shortstring = (__force struct wmfw_short_string *)ple32;
		shortstring->len = shortname_len;
		memcpy(shortstring->data, def->shortname, shortname_len);

		/* Round up to next __le32 multiple */
		ple32 += round_up(struct_size_t(struct wmfw_short_string, data, shortname_len),
				  sizeof(*ple32)) / sizeof(*ple32);

		shortstring = (__force struct wmfw_short_string *)ple32;
		shortstring->len = fullname_len;
		memcpy(shortstring->data, def->fullname, fullname_len);

		/* Round up to next __le32 multiple */
		ple32 += round_up(struct_size_t(struct wmfw_short_string, data, fullname_len),
				  sizeof(*ple32)) / sizeof(*ple32);

		longstring = (__force struct wmfw_long_string *)ple32;
		longstring->len = cpu_to_le16(description_len);
		memcpy(longstring->data, def->description, description_len);

		/* Round up to next __le32 multiple */
		ple32 += round_up(struct_size_t(struct wmfw_long_string, data, description_len),
				  sizeof(*ple32)) / sizeof(*ple32);

		*ple32++ = cpu_to_le32(def->type | (def->flags << 16));
		*ple32 = cpu_to_le32(def->length_bytes);
		break;
	}

	builder->write_p += bytes_needed;
	builder->bytes_used += bytes_needed;
	builder->num_coeffs++;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_wmfw_add_coeff_desc, "FW_CS_DSP_KUNIT_TEST_UTILS");

void cs_dsp_mock_wmfw_end_alg_info_block(struct cs_dsp_mock_wmfw_builder *builder)
{
	struct wmfw_region *rgn = builder->alg_data_header;
	struct wmfw_adsp_alg_data *v1;
	const struct wmfw_short_string *shortstring;
	const struct wmfw_long_string *longstring;
	size_t offset;

	KUNIT_ASSERT_NOT_NULL(builder->test_priv->test, rgn);

	/* Fill in data size */
	rgn->len = cpu_to_le32((u8 *)builder->write_p - (u8 *)rgn->data);

	/* Fill in coefficient count */
	switch (builder->format_version) {
	case 0:
		return;
	case 1:
		v1 = (struct wmfw_adsp_alg_data *)&rgn->data[0];
		v1->ncoeff = cpu_to_le32(builder->num_coeffs);
		break;
	default:
		offset = 4; /* skip alg id */

		/* Get name length and round up to __le32 multiple */
		shortstring = (const struct wmfw_short_string *)&rgn->data[offset];
		offset += round_up(struct_size_t(struct wmfw_short_string, data, shortstring->len),
				   sizeof(__le32));

		/* Get description length and round up to __le32 multiple */
		longstring = (const struct wmfw_long_string *)&rgn->data[offset];
		offset += round_up(struct_size_t(struct wmfw_long_string, data,
				   le16_to_cpu(longstring->len)),
				   sizeof(__le32));

		*(__force __le32 *)&rgn->data[offset] = cpu_to_le32(builder->num_coeffs);
		break;
	}

	builder->alg_data_header = NULL;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_wmfw_end_alg_info_block, "FW_CS_DSP_KUNIT_TEST_UTILS");

static void cs_dsp_init_adsp2_halo_wmfw(struct cs_dsp_mock_wmfw_builder *builder)
{
	struct wmfw_adsp2_halo_header *hdr = builder->buf;
	const struct cs_dsp *dsp = builder->test_priv->dsp;

	memcpy(hdr->header.magic, "WMFW", sizeof(hdr->header.magic));
	hdr->header.len = cpu_to_le32(sizeof(*hdr));
	hdr->header.ver = builder->format_version;
	hdr->header.core = dsp->type;
	hdr->header.rev = cpu_to_le16(dsp->rev);

	hdr->sizes.pm = cpu_to_le32(cs_dsp_mock_size_of_region(dsp, WMFW_ADSP2_PM));
	hdr->sizes.xm = cpu_to_le32(cs_dsp_mock_size_of_region(dsp, WMFW_ADSP2_XM));
	hdr->sizes.ym = cpu_to_le32(cs_dsp_mock_size_of_region(dsp, WMFW_ADSP2_YM));

	switch (dsp->type) {
	case WMFW_ADSP2:
		hdr->sizes.zm = cpu_to_le32(cs_dsp_mock_size_of_region(dsp, WMFW_ADSP2_ZM));
		break;
	default:
		break;
	}

	builder->write_p = &hdr[1];
	builder->bytes_used += sizeof(*hdr);
}

/**
 * cs_dsp_mock_wmfw_init() - Initialize a struct cs_dsp_mock_wmfw_builder.
 *
 * @priv:		Pointer to struct cs_dsp_test.
 * @format_version:	Required wmfw format version.
 *
 * Return: Pointer to created struct cs_dsp_mock_wmfw_builder.
 */
struct cs_dsp_mock_wmfw_builder *cs_dsp_mock_wmfw_init(struct cs_dsp_test *priv,
						       int format_version)
{
	struct cs_dsp_mock_wmfw_builder *builder;

	KUNIT_ASSERT_LE(priv->test, format_version, 0xff);

	/* If format version isn't given use the default for the target core */
	if (format_version < 0) {
		switch (priv->dsp->type) {
		case WMFW_ADSP2:
			format_version = 2;
			break;
		default:
			format_version = 3;
			break;
		}
	}

	builder = kunit_kzalloc(priv->test, sizeof(*builder), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(priv->test, builder);

	builder->test_priv = priv;
	builder->format_version = format_version;

	builder->buf = vmalloc(CS_DSP_MOCK_WMFW_BUF_SIZE);
	KUNIT_ASSERT_NOT_NULL(priv->test, builder->buf);
	kunit_add_action_or_reset(priv->test, vfree_action_wrapper, builder->buf);

	builder->buf_size_bytes = CS_DSP_MOCK_WMFW_BUF_SIZE;

	switch (priv->dsp->type) {
	case WMFW_ADSP2:
	case WMFW_HALO:
		cs_dsp_init_adsp2_halo_wmfw(builder);
		break;
	default:
		break;
	}

	return builder;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mock_wmfw_init, "FW_CS_DSP_KUNIT_TEST_UTILS");
