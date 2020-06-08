// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */

#include <linux/types.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/bitfield.h>
#include <linux/dma-direction.h>

#include "gsi.h"
#include "gsi_trans.h"
#include "ipa.h"
#include "ipa_endpoint.h"
#include "ipa_table.h"
#include "ipa_cmd.h"
#include "ipa_mem.h"

/**
 * DOC:  IPA Immediate Commands
 *
 * The AP command TX endpoint is used to issue immediate commands to the IPA.
 * An immediate command is generally used to request the IPA do something
 * other than data transfer to another endpoint.
 *
 * Immediate commands are represented by GSI transactions just like other
 * transfer requests, represented by a single GSI TRE.  Each immediate
 * command has a well-defined format, having a payload of a known length.
 * This allows the transfer element's length field to be used to hold an
 * immediate command's opcode.  The payload for a command resides in DRAM
 * and is described by a single scatterlist entry in its transaction.
 * Commands do not require a transaction completion callback.  To commit
 * an immediate command transaction, either gsi_trans_commit_wait() or
 * gsi_trans_commit_wait_timeout() is used.
 */

/* Some commands can wait until indicated pipeline stages are clear */
enum pipeline_clear_options {
	pipeline_clear_hps	= 0,
	pipeline_clear_src_grp	= 1,
	pipeline_clear_full	= 2,
};

/* IPA_CMD_IP_V{4,6}_{FILTER,ROUTING}_INIT */

struct ipa_cmd_hw_ip_fltrt_init {
	__le64 hash_rules_addr;
	__le64 flags;
	__le64 nhash_rules_addr;
};

/* Field masks for ipa_cmd_hw_ip_fltrt_init structure fields */
#define IP_FLTRT_FLAGS_HASH_SIZE_FMASK			GENMASK_ULL(11, 0)
#define IP_FLTRT_FLAGS_HASH_ADDR_FMASK			GENMASK_ULL(27, 12)
#define IP_FLTRT_FLAGS_NHASH_SIZE_FMASK			GENMASK_ULL(39, 28)
#define IP_FLTRT_FLAGS_NHASH_ADDR_FMASK			GENMASK_ULL(55, 40)

/* IPA_CMD_HDR_INIT_LOCAL */

struct ipa_cmd_hw_hdr_init_local {
	__le64 hdr_table_addr;
	__le32 flags;
	__le32 reserved;
};

/* Field masks for ipa_cmd_hw_hdr_init_local structure fields */
#define HDR_INIT_LOCAL_FLAGS_TABLE_SIZE_FMASK		GENMASK(11, 0)
#define HDR_INIT_LOCAL_FLAGS_HDR_ADDR_FMASK		GENMASK(27, 12)

/* IPA_CMD_REGISTER_WRITE */

/* For IPA v4.0+, this opcode gets modified with pipeline clear options */

#define REGISTER_WRITE_OPCODE_SKIP_CLEAR_FMASK		GENMASK(8, 8)
#define REGISTER_WRITE_OPCODE_CLEAR_OPTION_FMASK	GENMASK(10, 9)

struct ipa_cmd_register_write {
	__le16 flags;		/* Unused/reserved for IPA v3.5.1 */
	__le16 offset;
	__le32 value;
	__le32 value_mask;
	__le32 clear_options;	/* Unused/reserved for IPA v4.0+ */
};

/* Field masks for ipa_cmd_register_write structure fields */
/* The next field is present for IPA v4.0 and above */
#define REGISTER_WRITE_FLAGS_OFFSET_HIGH_FMASK		GENMASK(14, 11)
/* The next field is present for IPA v3.5.1 only */
#define REGISTER_WRITE_FLAGS_SKIP_CLEAR_FMASK		GENMASK(15, 15)

/* The next field and its values are present for IPA v3.5.1 only */
#define REGISTER_WRITE_CLEAR_OPTIONS_FMASK		GENMASK(1, 0)

/* IPA_CMD_IP_PACKET_INIT */

struct ipa_cmd_ip_packet_init {
	u8 dest_endpoint;
	u8 reserved[7];
};

/* Field masks for ipa_cmd_ip_packet_init dest_endpoint field */
#define IPA_PACKET_INIT_DEST_ENDPOINT_FMASK		GENMASK(4, 0)

/* IPA_CMD_DMA_SHARED_MEM */

/* For IPA v4.0+, this opcode gets modified with pipeline clear options */

#define DMA_SHARED_MEM_OPCODE_SKIP_CLEAR_FMASK		GENMASK(8, 8)
#define DMA_SHARED_MEM_OPCODE_CLEAR_OPTION_FMASK	GENMASK(10, 9)

struct ipa_cmd_hw_dma_mem_mem {
	__le16 clear_after_read; /* 0 or DMA_SHARED_MEM_CLEAR_AFTER_READ */
	__le16 size;
	__le16 local_addr;
	__le16 flags;
	__le64 system_addr;
};

/* Flag allowing atomic clear of target region after reading data (v4.0+)*/
#define DMA_SHARED_MEM_CLEAR_AFTER_READ			GENMASK(15, 15)

/* Field masks for ipa_cmd_hw_dma_mem_mem structure fields */
#define DMA_SHARED_MEM_FLAGS_DIRECTION_FMASK		GENMASK(0, 0)
/* The next two fields are present for IPA v3.5.1 only. */
#define DMA_SHARED_MEM_FLAGS_SKIP_CLEAR_FMASK		GENMASK(1, 1)
#define DMA_SHARED_MEM_FLAGS_CLEAR_OPTIONS_FMASK	GENMASK(3, 2)

/* IPA_CMD_IP_PACKET_TAG_STATUS */

struct ipa_cmd_ip_packet_tag_status {
	__le64 tag;
};

#define IP_PACKET_TAG_STATUS_TAG_FMASK			GENMASK_ULL(63, 16)

/* Immediate command payload */
union ipa_cmd_payload {
	struct ipa_cmd_hw_ip_fltrt_init table_init;
	struct ipa_cmd_hw_hdr_init_local hdr_init_local;
	struct ipa_cmd_register_write register_write;
	struct ipa_cmd_ip_packet_init ip_packet_init;
	struct ipa_cmd_hw_dma_mem_mem dma_shared_mem;
	struct ipa_cmd_ip_packet_tag_status ip_packet_tag_status;
};

static void ipa_cmd_validate_build(void)
{
	/* The sizes of a filter and route tables need to fit into fields
	 * in the ipa_cmd_hw_ip_fltrt_init structure.  Although hashed tables
	 * might not be used, non-hashed and hashed tables have the same
	 * maximum size.  IPv4 and IPv6 filter tables have the same number
	 * of entries, as and IPv4 and IPv6 route tables have the same number
	 * of entries.
	 */
#define TABLE_SIZE	(TABLE_COUNT_MAX * IPA_TABLE_ENTRY_SIZE)
#define TABLE_COUNT_MAX	max_t(u32, IPA_ROUTE_COUNT_MAX, IPA_FILTER_COUNT_MAX)
	BUILD_BUG_ON(TABLE_SIZE > field_max(IP_FLTRT_FLAGS_HASH_SIZE_FMASK));
	BUILD_BUG_ON(TABLE_SIZE > field_max(IP_FLTRT_FLAGS_NHASH_SIZE_FMASK));
#undef TABLE_COUNT_MAX
#undef TABLE_SIZE
}

#ifdef IPA_VALIDATE

/* Validate a memory region holding a table */
bool ipa_cmd_table_valid(struct ipa *ipa, const struct ipa_mem *mem,
			 bool route, bool ipv6, bool hashed)
{
	struct device *dev = &ipa->pdev->dev;
	u32 offset_max;

	offset_max = hashed ? field_max(IP_FLTRT_FLAGS_HASH_ADDR_FMASK)
			    : field_max(IP_FLTRT_FLAGS_NHASH_ADDR_FMASK);
	if (mem->offset > offset_max ||
	    ipa->mem_offset > offset_max - mem->offset) {
		dev_err(dev, "IPv%c %s%s table region offset too large "
			      "(0x%04x + 0x%04x > 0x%04x)\n",
			      ipv6 ? '6' : '4', hashed ? "hashed " : "",
			      route ? "route" : "filter",
			      ipa->mem_offset, mem->offset, offset_max);
		return false;
	}

	if (mem->offset > ipa->mem_size ||
	    mem->size > ipa->mem_size - mem->offset) {
		dev_err(dev, "IPv%c %s%s table region out of range "
			      "(0x%04x + 0x%04x > 0x%04x)\n",
			      ipv6 ? '6' : '4', hashed ? "hashed " : "",
			      route ? "route" : "filter",
			      mem->offset, mem->size, ipa->mem_size);
		return false;
	}

	return true;
}

/* Validate the memory region that holds headers */
static bool ipa_cmd_header_valid(struct ipa *ipa)
{
	const struct ipa_mem *mem = &ipa->mem[IPA_MEM_MODEM_HEADER];
	struct device *dev = &ipa->pdev->dev;
	u32 offset_max;
	u32 size_max;
	u32 size;

	offset_max = field_max(HDR_INIT_LOCAL_FLAGS_HDR_ADDR_FMASK);
	if (mem->offset > offset_max ||
	    ipa->mem_offset > offset_max - mem->offset) {
		dev_err(dev, "header table region offset too large "
			      "(0x%04x + 0x%04x > 0x%04x)\n",
			      ipa->mem_offset + mem->offset, offset_max);
		return false;
	}

	size_max = field_max(HDR_INIT_LOCAL_FLAGS_TABLE_SIZE_FMASK);
	size = ipa->mem[IPA_MEM_MODEM_HEADER].size;
	size += ipa->mem[IPA_MEM_AP_HEADER].size;
	if (mem->offset > ipa->mem_size || size > ipa->mem_size - mem->offset) {
		dev_err(dev, "header table region out of range "
			      "(0x%04x + 0x%04x > 0x%04x)\n",
			      mem->offset, size, ipa->mem_size);
		return false;
	}

	return true;
}

/* Indicate whether an offset can be used with a register_write command */
static bool ipa_cmd_register_write_offset_valid(struct ipa *ipa,
						const char *name, u32 offset)
{
	struct ipa_cmd_register_write *payload;
	struct device *dev = &ipa->pdev->dev;
	u32 offset_max;
	u32 bit_count;

	/* The maximum offset in a register_write immediate command depends
	 * on the version of IPA.  IPA v3.5.1 supports a 16 bit offset, but
	 * newer versions allow some additional high-order bits.
	 */
	bit_count = BITS_PER_BYTE * sizeof(payload->offset);
	if (ipa->version != IPA_VERSION_3_5_1)
		bit_count += hweight32(REGISTER_WRITE_FLAGS_OFFSET_HIGH_FMASK);
	BUILD_BUG_ON(bit_count > 32);
	offset_max = ~0 >> (32 - bit_count);

	if (offset > offset_max || ipa->mem_offset > offset_max - offset) {
		dev_err(dev, "%s offset too large 0x%04x + 0x%04x > 0x%04x)\n",
				ipa->mem_offset + offset, offset_max);
		return false;
	}

	return true;
}

/* Check whether offsets passed to register_write are valid */
static bool ipa_cmd_register_write_valid(struct ipa *ipa)
{
	const char *name;
	u32 offset;

	offset = ipa_reg_filt_rout_hash_flush_offset(ipa->version);
	name = "filter/route hash flush";
	if (!ipa_cmd_register_write_offset_valid(ipa, name, offset))
		return false;

	offset = IPA_REG_ENDP_STATUS_N_OFFSET(IPA_ENDPOINT_COUNT);
	name = "maximal endpoint status";
	if (!ipa_cmd_register_write_offset_valid(ipa, name, offset))
		return false;

	return true;
}

bool ipa_cmd_data_valid(struct ipa *ipa)
{
	if (!ipa_cmd_header_valid(ipa))
		return false;

	if (!ipa_cmd_register_write_valid(ipa))
		return false;

	return true;
}

#endif /* IPA_VALIDATE */

int ipa_cmd_pool_init(struct gsi_channel *channel, u32 tre_max)
{
	struct gsi_trans_info *trans_info = &channel->trans_info;
	struct device *dev = channel->gsi->dev;
	int ret;

	/* This is as good a place as any to validate build constants */
	ipa_cmd_validate_build();

	/* Even though command payloads are allocated one at a time,
	 * a single transaction can require up to tlv_count of them,
	 * so we treat them as if that many can be allocated at once.
	 */
	ret = gsi_trans_pool_init_dma(dev, &trans_info->cmd_pool,
				      sizeof(union ipa_cmd_payload),
				      tre_max, channel->tlv_count);
	if (ret)
		return ret;

	/* Each TRE needs a command info structure */
	ret = gsi_trans_pool_init(&trans_info->info_pool,
				   sizeof(struct ipa_cmd_info),
				   tre_max, channel->tlv_count);
	if (ret)
		gsi_trans_pool_exit_dma(dev, &trans_info->cmd_pool);

	return ret;
}

void ipa_cmd_pool_exit(struct gsi_channel *channel)
{
	struct gsi_trans_info *trans_info = &channel->trans_info;
	struct device *dev = channel->gsi->dev;

	gsi_trans_pool_exit(&trans_info->info_pool);
	gsi_trans_pool_exit_dma(dev, &trans_info->cmd_pool);
}

static union ipa_cmd_payload *
ipa_cmd_payload_alloc(struct ipa *ipa, dma_addr_t *addr)
{
	struct gsi_trans_info *trans_info;
	struct ipa_endpoint *endpoint;

	endpoint = ipa->name_map[IPA_ENDPOINT_AP_COMMAND_TX];
	trans_info = &ipa->gsi.channel[endpoint->channel_id].trans_info;

	return gsi_trans_pool_alloc_dma(&trans_info->cmd_pool, addr);
}

/* If hash_size is 0, hash_offset and hash_addr ignored. */
void ipa_cmd_table_init_add(struct gsi_trans *trans,
			    enum ipa_cmd_opcode opcode, u16 size, u32 offset,
			    dma_addr_t addr, u16 hash_size, u32 hash_offset,
			    dma_addr_t hash_addr)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	enum dma_data_direction direction = DMA_TO_DEVICE;
	struct ipa_cmd_hw_ip_fltrt_init *payload;
	union ipa_cmd_payload *cmd_payload;
	dma_addr_t payload_addr;
	u64 val;

	/* Record the non-hash table offset and size */
	offset += ipa->mem_offset;
	val = u64_encode_bits(offset, IP_FLTRT_FLAGS_NHASH_ADDR_FMASK);
	val |= u64_encode_bits(size, IP_FLTRT_FLAGS_NHASH_SIZE_FMASK);

	/* The hash table offset and address are zero if its size is 0 */
	if (hash_size) {
		/* Record the hash table offset and size */
		hash_offset += ipa->mem_offset;
		val |= u64_encode_bits(hash_offset,
				       IP_FLTRT_FLAGS_HASH_ADDR_FMASK);
		val |= u64_encode_bits(hash_size,
				       IP_FLTRT_FLAGS_HASH_SIZE_FMASK);
	}

	cmd_payload = ipa_cmd_payload_alloc(ipa, &payload_addr);
	payload = &cmd_payload->table_init;

	/* Fill in all offsets and sizes and the non-hash table address */
	if (hash_size)
		payload->hash_rules_addr = cpu_to_le64(hash_addr);
	payload->flags = cpu_to_le64(val);
	payload->nhash_rules_addr = cpu_to_le64(addr);

	gsi_trans_cmd_add(trans, payload, sizeof(*payload), payload_addr,
			  direction, opcode);
}

/* Initialize header space in IPA-local memory */
void ipa_cmd_hdr_init_local_add(struct gsi_trans *trans, u32 offset, u16 size,
				dma_addr_t addr)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	enum ipa_cmd_opcode opcode = IPA_CMD_HDR_INIT_LOCAL;
	enum dma_data_direction direction = DMA_TO_DEVICE;
	struct ipa_cmd_hw_hdr_init_local *payload;
	union ipa_cmd_payload *cmd_payload;
	dma_addr_t payload_addr;
	u32 flags;

	offset += ipa->mem_offset;

	/* With this command we tell the IPA where in its local memory the
	 * header tables reside.  The content of the buffer provided is
	 * also written via DMA into that space.  The IPA hardware owns
	 * the table, but the AP must initialize it.
	 */
	cmd_payload = ipa_cmd_payload_alloc(ipa, &payload_addr);
	payload = &cmd_payload->hdr_init_local;

	payload->hdr_table_addr = cpu_to_le64(addr);
	flags = u32_encode_bits(size, HDR_INIT_LOCAL_FLAGS_TABLE_SIZE_FMASK);
	flags |= u32_encode_bits(offset, HDR_INIT_LOCAL_FLAGS_HDR_ADDR_FMASK);
	payload->flags = cpu_to_le32(flags);

	gsi_trans_cmd_add(trans, payload, sizeof(*payload), payload_addr,
			  direction, opcode);
}

void ipa_cmd_register_write_add(struct gsi_trans *trans, u32 offset, u32 value,
				u32 mask, bool clear_full)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	struct ipa_cmd_register_write *payload;
	union ipa_cmd_payload *cmd_payload;
	u32 opcode = IPA_CMD_REGISTER_WRITE;
	dma_addr_t payload_addr;
	u32 clear_option;
	u32 options;
	u16 flags;

	/* pipeline_clear_src_grp is not used */
	clear_option = clear_full ? pipeline_clear_full : pipeline_clear_hps;

	if (ipa->version != IPA_VERSION_3_5_1) {
		u16 offset_high;
		u32 val;

		/* Opcode encodes pipeline clear options */
		/* SKIP_CLEAR is always 0 (don't skip pipeline clear) */
		val = u16_encode_bits(clear_option,
				      REGISTER_WRITE_OPCODE_CLEAR_OPTION_FMASK);
		opcode |= val;

		/* Extract the high 4 bits from the offset */
		offset_high = (u16)u32_get_bits(offset, GENMASK(19, 16));
		offset &= (1 << 16) - 1;

		/* Extract the top 4 bits and encode it into the flags field */
		flags = u16_encode_bits(offset_high,
				REGISTER_WRITE_FLAGS_OFFSET_HIGH_FMASK);
		options = 0;	/* reserved */

	} else {
		flags = 0;	/* SKIP_CLEAR flag is always 0 */
		options = u16_encode_bits(clear_option,
					  REGISTER_WRITE_CLEAR_OPTIONS_FMASK);
	}

	cmd_payload = ipa_cmd_payload_alloc(ipa, &payload_addr);
	payload = &cmd_payload->register_write;

	payload->flags = cpu_to_le16(flags);
	payload->offset = cpu_to_le16((u16)offset);
	payload->value = cpu_to_le32(value);
	payload->value_mask = cpu_to_le32(mask);
	payload->clear_options = cpu_to_le32(options);

	gsi_trans_cmd_add(trans, payload, sizeof(*payload), payload_addr,
			  DMA_NONE, opcode);
}

/* Skip IP packet processing on the next data transfer on a TX channel */
static void ipa_cmd_ip_packet_init_add(struct gsi_trans *trans, u8 endpoint_id)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	enum ipa_cmd_opcode opcode = IPA_CMD_IP_PACKET_INIT;
	enum dma_data_direction direction = DMA_TO_DEVICE;
	struct ipa_cmd_ip_packet_init *payload;
	union ipa_cmd_payload *cmd_payload;
	dma_addr_t payload_addr;

	/* assert(endpoint_id <
		  field_max(IPA_PACKET_INIT_DEST_ENDPOINT_FMASK)); */

	cmd_payload = ipa_cmd_payload_alloc(ipa, &payload_addr);
	payload = &cmd_payload->ip_packet_init;

	payload->dest_endpoint = u8_encode_bits(endpoint_id,
					IPA_PACKET_INIT_DEST_ENDPOINT_FMASK);

	gsi_trans_cmd_add(trans, payload, sizeof(*payload), payload_addr,
			  direction, opcode);
}

/* Use a DMA command to read or write a block of IPA-resident memory */
void ipa_cmd_dma_shared_mem_add(struct gsi_trans *trans, u32 offset, u16 size,
				dma_addr_t addr, bool toward_ipa)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	enum ipa_cmd_opcode opcode = IPA_CMD_DMA_SHARED_MEM;
	struct ipa_cmd_hw_dma_mem_mem *payload;
	union ipa_cmd_payload *cmd_payload;
	enum dma_data_direction direction;
	dma_addr_t payload_addr;
	u16 flags;

	/* size and offset must fit in 16 bit fields */
	/* assert(size > 0 && size <= U16_MAX); */
	/* assert(offset <= U16_MAX && ipa->mem_offset <= U16_MAX - offset); */

	offset += ipa->mem_offset;

	cmd_payload = ipa_cmd_payload_alloc(ipa, &payload_addr);
	payload = &cmd_payload->dma_shared_mem;

	/* payload->clear_after_read was reserved prior to IPA v4.0.  It's
	 * never needed for current code, so it's 0 regardless of version.
	 */
	payload->size = cpu_to_le16(size);
	payload->local_addr = cpu_to_le16(offset);
	/* payload->flags:
	 *   direction:		0 = write to IPA, 1 read from IPA
	 * Starting at v4.0 these are reserved; either way, all zero:
	 *   pipeline clear:	0 = wait for pipeline clear (don't skip)
	 *   clear_options:	0 = pipeline_clear_hps
	 * Instead, for v4.0+ these are encoded in the opcode.  But again
	 * since both values are 0 we won't bother OR'ing them in.
	 */
	flags = toward_ipa ? 0 : DMA_SHARED_MEM_FLAGS_DIRECTION_FMASK;
	payload->flags = cpu_to_le16(flags);
	payload->system_addr = cpu_to_le64(addr);

	direction = toward_ipa ? DMA_TO_DEVICE : DMA_FROM_DEVICE;

	gsi_trans_cmd_add(trans, payload, sizeof(*payload), payload_addr,
			  direction, opcode);
}

static void ipa_cmd_ip_tag_status_add(struct gsi_trans *trans, u64 tag)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	enum ipa_cmd_opcode opcode = IPA_CMD_IP_PACKET_TAG_STATUS;
	enum dma_data_direction direction = DMA_TO_DEVICE;
	struct ipa_cmd_ip_packet_tag_status *payload;
	union ipa_cmd_payload *cmd_payload;
	dma_addr_t payload_addr;

	/* assert(tag <= field_max(IP_PACKET_TAG_STATUS_TAG_FMASK)); */

	cmd_payload = ipa_cmd_payload_alloc(ipa, &payload_addr);
	payload = &cmd_payload->ip_packet_tag_status;

	payload->tag = u64_encode_bits(tag, IP_PACKET_TAG_STATUS_TAG_FMASK);

	gsi_trans_cmd_add(trans, payload, sizeof(*payload), payload_addr,
			  direction, opcode);
}

/* Issue a small command TX data transfer */
static void ipa_cmd_transfer_add(struct gsi_trans *trans, u16 size)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	enum dma_data_direction direction = DMA_TO_DEVICE;
	enum ipa_cmd_opcode opcode = IPA_CMD_NONE;
	union ipa_cmd_payload *payload;
	dma_addr_t payload_addr;

	/* assert(size <= sizeof(*payload)); */

	/* Just transfer a zero-filled payload structure */
	payload = ipa_cmd_payload_alloc(ipa, &payload_addr);

	gsi_trans_cmd_add(trans, payload, sizeof(*payload), payload_addr,
			  direction, opcode);
}

void ipa_cmd_tag_process_add(struct gsi_trans *trans)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	struct ipa_endpoint *endpoint;

	endpoint = ipa->name_map[IPA_ENDPOINT_AP_LAN_RX];

	ipa_cmd_register_write_add(trans, 0, 0, 0, true);
	ipa_cmd_ip_packet_init_add(trans, endpoint->endpoint_id);
	ipa_cmd_ip_tag_status_add(trans, 0xcba987654321);
	ipa_cmd_transfer_add(trans, 4);
}

/* Returns the number of commands required for the tag process */
u32 ipa_cmd_tag_process_count(void)
{
	return 4;
}

static struct ipa_cmd_info *
ipa_cmd_info_alloc(struct ipa_endpoint *endpoint, u32 tre_count)
{
	struct gsi_channel *channel;

	channel = &endpoint->ipa->gsi.channel[endpoint->channel_id];

	return gsi_trans_pool_alloc(&channel->trans_info.info_pool, tre_count);
}

/* Allocate a transaction for the command TX endpoint */
struct gsi_trans *ipa_cmd_trans_alloc(struct ipa *ipa, u32 tre_count)
{
	struct ipa_endpoint *endpoint;
	struct gsi_trans *trans;

	endpoint = ipa->name_map[IPA_ENDPOINT_AP_COMMAND_TX];

	trans = gsi_channel_trans_alloc(&ipa->gsi, endpoint->channel_id,
					tre_count, DMA_NONE);
	if (trans)
		trans->info = ipa_cmd_info_alloc(endpoint, tre_count);

	return trans;
}
