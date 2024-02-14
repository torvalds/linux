// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "encodings.h"

#include <linux/log2.h>

#include "logger.h"
#include "memory-alloc.h"
#include "permassert.h"

#include "constants.h"
#include "status-codes.h"
#include "types.h"

/** The maximum logical space is 4 petabytes, which is 1 terablock. */
static const block_count_t MAXIMUM_VDO_LOGICAL_BLOCKS = 1024ULL * 1024 * 1024 * 1024;

/** The maximum physical space is 256 terabytes, which is 64 gigablocks. */
static const block_count_t MAXIMUM_VDO_PHYSICAL_BLOCKS = 1024ULL * 1024 * 1024 * 64;

struct geometry_block {
	char magic_number[VDO_GEOMETRY_MAGIC_NUMBER_SIZE];
	struct packed_header header;
	u32 checksum;
} __packed;

static const struct header GEOMETRY_BLOCK_HEADER_5_0 = {
	.id = VDO_GEOMETRY_BLOCK,
	.version = {
		.major_version = 5,
		.minor_version = 0,
	},
	/*
	 * Note: this size isn't just the payload size following the header, like it is everywhere
	 * else in VDO.
	 */
	.size = sizeof(struct geometry_block) + sizeof(struct volume_geometry),
};

static const struct header GEOMETRY_BLOCK_HEADER_4_0 = {
	.id = VDO_GEOMETRY_BLOCK,
	.version = {
		.major_version = 4,
		.minor_version = 0,
	},
	/*
	 * Note: this size isn't just the payload size following the header, like it is everywhere
	 * else in VDO.
	 */
	.size = sizeof(struct geometry_block) + sizeof(struct volume_geometry_4_0),
};

const u8 VDO_GEOMETRY_MAGIC_NUMBER[VDO_GEOMETRY_MAGIC_NUMBER_SIZE + 1] = "dmvdo001";

#define PAGE_HEADER_4_1_SIZE (8 + 8 + 8 + 1 + 1 + 1 + 1)

static const struct version_number BLOCK_MAP_4_1 = {
	.major_version = 4,
	.minor_version = 1,
};

const struct header VDO_BLOCK_MAP_HEADER_2_0 = {
	.id = VDO_BLOCK_MAP,
	.version = {
		.major_version = 2,
		.minor_version = 0,
	},
	.size = sizeof(struct block_map_state_2_0),
};

const struct header VDO_RECOVERY_JOURNAL_HEADER_7_0 = {
	.id = VDO_RECOVERY_JOURNAL,
	.version = {
			.major_version = 7,
			.minor_version = 0,
		},
	.size = sizeof(struct recovery_journal_state_7_0),
};

const struct header VDO_SLAB_DEPOT_HEADER_2_0 = {
	.id = VDO_SLAB_DEPOT,
	.version = {
		.major_version = 2,
		.minor_version = 0,
	},
	.size = sizeof(struct slab_depot_state_2_0),
};

static const struct header VDO_LAYOUT_HEADER_3_0 = {
	.id = VDO_LAYOUT,
	.version = {
		.major_version = 3,
		.minor_version = 0,
	},
	.size = sizeof(struct layout_3_0) + (sizeof(struct partition_3_0) * VDO_PARTITION_COUNT),
};

static const enum partition_id REQUIRED_PARTITIONS[] = {
	VDO_BLOCK_MAP_PARTITION,
	VDO_SLAB_DEPOT_PARTITION,
	VDO_RECOVERY_JOURNAL_PARTITION,
	VDO_SLAB_SUMMARY_PARTITION,
};

/*
 * The current version for the data encoded in the super block. This must be changed any time there
 * is a change to encoding of the component data of any VDO component.
 */
static const struct version_number VDO_COMPONENT_DATA_41_0 = {
	.major_version = 41,
	.minor_version = 0,
};

const struct version_number VDO_VOLUME_VERSION_67_0 = {
	.major_version = 67,
	.minor_version = 0,
};

static const struct header SUPER_BLOCK_HEADER_12_0 = {
	.id = VDO_SUPER_BLOCK,
	.version = {
			.major_version = 12,
			.minor_version = 0,
		},

	/* This is the minimum size, if the super block contains no components. */
	.size = VDO_SUPER_BLOCK_FIXED_SIZE - VDO_ENCODED_HEADER_SIZE,
};

/**
 * validate_version() - Check whether a version matches an expected version.
 * @expected_version: The expected version.
 * @actual_version: The version being validated.
 * @component_name: The name of the component or the calling function (for error logging).
 *
 * Logs an error describing a mismatch.
 *
 * Return: VDO_SUCCESS             if the versions are the same,
 *         VDO_UNSUPPORTED_VERSION if the versions don't match.
 */
static int __must_check validate_version(struct version_number expected_version,
					 struct version_number actual_version,
					 const char *component_name)
{
	if (!vdo_are_same_version(expected_version, actual_version)) {
		return vdo_log_error_strerror(VDO_UNSUPPORTED_VERSION,
					      "%s version mismatch, expected %d.%d, got %d.%d",
					      component_name,
					      expected_version.major_version,
					      expected_version.minor_version,
					      actual_version.major_version,
					      actual_version.minor_version);
	}

	return VDO_SUCCESS;
}

/**
 * vdo_validate_header() - Check whether a header matches expectations.
 * @expected_header: The expected header.
 * @actual_header: The header being validated.
 * @exact_size: If true, the size fields of the two headers must be the same, otherwise it is
 *              required that actual_header.size >= expected_header.size.
 * @name: The name of the component or the calling function (for error logging).
 *
 * Logs an error describing the first mismatch found.
 *
 * Return: VDO_SUCCESS             if the header meets expectations,
 *         VDO_INCORRECT_COMPONENT if the component ids don't match,
 *         VDO_UNSUPPORTED_VERSION if the versions or sizes don't match.
 */
int vdo_validate_header(const struct header *expected_header,
			const struct header *actual_header, bool exact_size,
			const char *name)
{
	int result;

	if (expected_header->id != actual_header->id) {
		return vdo_log_error_strerror(VDO_INCORRECT_COMPONENT,
					      "%s ID mismatch, expected %d, got %d",
					      name, expected_header->id,
					      actual_header->id);
	}

	result = validate_version(expected_header->version, actual_header->version,
				  name);
	if (result != VDO_SUCCESS)
		return result;

	if ((expected_header->size > actual_header->size) ||
	    (exact_size && (expected_header->size < actual_header->size))) {
		return vdo_log_error_strerror(VDO_UNSUPPORTED_VERSION,
					      "%s size mismatch, expected %zu, got %zu",
					      name, expected_header->size,
					      actual_header->size);
	}

	return VDO_SUCCESS;
}

static void encode_version_number(u8 *buffer, size_t *offset,
				  struct version_number version)
{
	struct packed_version_number packed = vdo_pack_version_number(version);

	memcpy(buffer + *offset, &packed, sizeof(packed));
	*offset += sizeof(packed);
}

void vdo_encode_header(u8 *buffer, size_t *offset, const struct header *header)
{
	struct packed_header packed = vdo_pack_header(header);

	memcpy(buffer + *offset, &packed, sizeof(packed));
	*offset += sizeof(packed);
}

static void decode_version_number(u8 *buffer, size_t *offset,
				  struct version_number *version)
{
	struct packed_version_number packed;

	memcpy(&packed, buffer + *offset, sizeof(packed));
	*offset += sizeof(packed);
	*version = vdo_unpack_version_number(packed);
}

void vdo_decode_header(u8 *buffer, size_t *offset, struct header *header)
{
	struct packed_header packed;

	memcpy(&packed, buffer + *offset, sizeof(packed));
	*offset += sizeof(packed);

	*header = vdo_unpack_header(&packed);
}

/**
 * decode_volume_geometry() - Decode the on-disk representation of a volume geometry from a buffer.
 * @buffer: A buffer to decode from.
 * @offset: The offset in the buffer at which to decode.
 * @geometry: The structure to receive the decoded fields.
 * @version: The geometry block version to decode.
 */
static void decode_volume_geometry(u8 *buffer, size_t *offset,
				   struct volume_geometry *geometry, u32 version)
{
	u32 unused, mem;
	enum volume_region_id id;
	nonce_t nonce;
	block_count_t bio_offset = 0;
	bool sparse;

	/* This is for backwards compatibility. */
	decode_u32_le(buffer, offset, &unused);
	geometry->unused = unused;

	decode_u64_le(buffer, offset, &nonce);
	geometry->nonce = nonce;

	memcpy((unsigned char *) &geometry->uuid, buffer + *offset, sizeof(uuid_t));
	*offset += sizeof(uuid_t);

	if (version > 4)
		decode_u64_le(buffer, offset, &bio_offset);
	geometry->bio_offset = bio_offset;

	for (id = 0; id < VDO_VOLUME_REGION_COUNT; id++) {
		physical_block_number_t start_block;
		enum volume_region_id saved_id;

		decode_u32_le(buffer, offset, &saved_id);
		decode_u64_le(buffer, offset, &start_block);

		geometry->regions[id] = (struct volume_region) {
			.id = saved_id,
			.start_block = start_block,
		};
	}

	decode_u32_le(buffer, offset, &mem);
	*offset += sizeof(u32);
	sparse = buffer[(*offset)++];

	geometry->index_config = (struct index_config) {
		.mem = mem,
		.sparse = sparse,
	};
}

/**
 * vdo_parse_geometry_block() - Decode and validate an encoded geometry block.
 * @block: The encoded geometry block.
 * @geometry: The structure to receive the decoded fields.
 */
int __must_check vdo_parse_geometry_block(u8 *block, struct volume_geometry *geometry)
{
	u32 checksum, saved_checksum;
	struct header header;
	size_t offset = 0;
	int result;

	if (memcmp(block, VDO_GEOMETRY_MAGIC_NUMBER, VDO_GEOMETRY_MAGIC_NUMBER_SIZE) != 0)
		return VDO_BAD_MAGIC;
	offset += VDO_GEOMETRY_MAGIC_NUMBER_SIZE;

	vdo_decode_header(block, &offset, &header);
	if (header.version.major_version <= 4) {
		result = vdo_validate_header(&GEOMETRY_BLOCK_HEADER_4_0, &header,
					     true, __func__);
	} else {
		result = vdo_validate_header(&GEOMETRY_BLOCK_HEADER_5_0, &header,
					     true, __func__);
	}
	if (result != VDO_SUCCESS)
		return result;

	decode_volume_geometry(block, &offset, geometry, header.version.major_version);

	result = VDO_ASSERT(header.size == offset + sizeof(u32),
			    "should have decoded up to the geometry checksum");
	if (result != VDO_SUCCESS)
		return result;

	/* Decode and verify the checksum. */
	checksum = vdo_crc32(block, offset);
	decode_u32_le(block, &offset, &saved_checksum);

	return ((checksum == saved_checksum) ? VDO_SUCCESS : VDO_CHECKSUM_MISMATCH);
}

struct block_map_page *vdo_format_block_map_page(void *buffer, nonce_t nonce,
						 physical_block_number_t pbn,
						 bool initialized)
{
	struct block_map_page *page = buffer;

	memset(buffer, 0, VDO_BLOCK_SIZE);
	page->version = vdo_pack_version_number(BLOCK_MAP_4_1);
	page->header.nonce = __cpu_to_le64(nonce);
	page->header.pbn = __cpu_to_le64(pbn);
	page->header.initialized = initialized;
	return page;
}

enum block_map_page_validity vdo_validate_block_map_page(struct block_map_page *page,
							 nonce_t nonce,
							 physical_block_number_t pbn)
{
	BUILD_BUG_ON(sizeof(struct block_map_page_header) != PAGE_HEADER_4_1_SIZE);

	if (!vdo_are_same_version(BLOCK_MAP_4_1,
				  vdo_unpack_version_number(page->version)) ||
	    !page->header.initialized || (nonce != __le64_to_cpu(page->header.nonce)))
		return VDO_BLOCK_MAP_PAGE_INVALID;

	if (pbn != vdo_get_block_map_page_pbn(page))
		return VDO_BLOCK_MAP_PAGE_BAD;

	return VDO_BLOCK_MAP_PAGE_VALID;
}

static int decode_block_map_state_2_0(u8 *buffer, size_t *offset,
				      struct block_map_state_2_0 *state)
{
	size_t initial_offset;
	block_count_t flat_page_count, root_count;
	physical_block_number_t flat_page_origin, root_origin;
	struct header header;
	int result;

	vdo_decode_header(buffer, offset, &header);
	result = vdo_validate_header(&VDO_BLOCK_MAP_HEADER_2_0, &header, true, __func__);
	if (result != VDO_SUCCESS)
		return result;

	initial_offset = *offset;

	decode_u64_le(buffer, offset, &flat_page_origin);
	result = VDO_ASSERT(flat_page_origin == VDO_BLOCK_MAP_FLAT_PAGE_ORIGIN,
			    "Flat page origin must be %u (recorded as %llu)",
			    VDO_BLOCK_MAP_FLAT_PAGE_ORIGIN,
			    (unsigned long long) state->flat_page_origin);
	if (result != VDO_SUCCESS)
		return result;

	decode_u64_le(buffer, offset, &flat_page_count);
	result = VDO_ASSERT(flat_page_count == 0,
			    "Flat page count must be 0 (recorded as %llu)",
			    (unsigned long long) state->flat_page_count);
	if (result != VDO_SUCCESS)
		return result;

	decode_u64_le(buffer, offset, &root_origin);
	decode_u64_le(buffer, offset, &root_count);

	result = VDO_ASSERT(VDO_BLOCK_MAP_HEADER_2_0.size == *offset - initial_offset,
			    "decoded block map component size must match header size");
	if (result != VDO_SUCCESS)
		return result;

	*state = (struct block_map_state_2_0) {
		.flat_page_origin = flat_page_origin,
		.flat_page_count = flat_page_count,
		.root_origin = root_origin,
		.root_count = root_count,
	};

	return VDO_SUCCESS;
}

static void encode_block_map_state_2_0(u8 *buffer, size_t *offset,
				       struct block_map_state_2_0 state)
{
	size_t initial_offset;

	vdo_encode_header(buffer, offset, &VDO_BLOCK_MAP_HEADER_2_0);

	initial_offset = *offset;
	encode_u64_le(buffer, offset, state.flat_page_origin);
	encode_u64_le(buffer, offset, state.flat_page_count);
	encode_u64_le(buffer, offset, state.root_origin);
	encode_u64_le(buffer, offset, state.root_count);

	VDO_ASSERT_LOG_ONLY(VDO_BLOCK_MAP_HEADER_2_0.size == *offset - initial_offset,
			    "encoded block map component size must match header size");
}

/**
 * vdo_compute_new_forest_pages() - Compute the number of pages which must be allocated at each
 *                                  level in order to grow the forest to a new number of entries.
 * @entries: The new number of entries the block map must address.
 *
 * Return: The total number of non-leaf pages required.
 */
block_count_t vdo_compute_new_forest_pages(root_count_t root_count,
					   struct boundary *old_sizes,
					   block_count_t entries,
					   struct boundary *new_sizes)
{
	page_count_t leaf_pages = max(vdo_compute_block_map_page_count(entries), 1U);
	page_count_t level_size = DIV_ROUND_UP(leaf_pages, root_count);
	block_count_t total_pages = 0;
	height_t height;

	for (height = 0; height < VDO_BLOCK_MAP_TREE_HEIGHT; height++) {
		block_count_t new_pages;

		level_size = DIV_ROUND_UP(level_size, VDO_BLOCK_MAP_ENTRIES_PER_PAGE);
		new_sizes->levels[height] = level_size;
		new_pages = level_size;
		if (old_sizes != NULL)
			new_pages -= old_sizes->levels[height];
		total_pages += (new_pages * root_count);
	}

	return total_pages;
}

/**
 * encode_recovery_journal_state_7_0() - Encode the state of a recovery journal.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static void encode_recovery_journal_state_7_0(u8 *buffer, size_t *offset,
					      struct recovery_journal_state_7_0 state)
{
	size_t initial_offset;

	vdo_encode_header(buffer, offset, &VDO_RECOVERY_JOURNAL_HEADER_7_0);

	initial_offset = *offset;
	encode_u64_le(buffer, offset, state.journal_start);
	encode_u64_le(buffer, offset, state.logical_blocks_used);
	encode_u64_le(buffer, offset, state.block_map_data_blocks);

	VDO_ASSERT_LOG_ONLY(VDO_RECOVERY_JOURNAL_HEADER_7_0.size == *offset - initial_offset,
			    "encoded recovery journal component size must match header size");
}

/**
 * decode_recovery_journal_state_7_0() - Decode the state of a recovery journal saved in a buffer.
 * @buffer: The buffer containing the saved state.
 * @state: A pointer to a recovery journal state to hold the result of a successful decode.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int __must_check decode_recovery_journal_state_7_0(u8 *buffer, size_t *offset,
							  struct recovery_journal_state_7_0 *state)
{
	struct header header;
	int result;
	size_t initial_offset;
	sequence_number_t journal_start;
	block_count_t logical_blocks_used, block_map_data_blocks;

	vdo_decode_header(buffer, offset, &header);
	result = vdo_validate_header(&VDO_RECOVERY_JOURNAL_HEADER_7_0, &header, true,
				     __func__);
	if (result != VDO_SUCCESS)
		return result;

	initial_offset = *offset;
	decode_u64_le(buffer, offset, &journal_start);
	decode_u64_le(buffer, offset, &logical_blocks_used);
	decode_u64_le(buffer, offset, &block_map_data_blocks);

	result = VDO_ASSERT(VDO_RECOVERY_JOURNAL_HEADER_7_0.size == *offset - initial_offset,
			    "decoded recovery journal component size must match header size");
	if (result != VDO_SUCCESS)
		return result;

	*state = (struct recovery_journal_state_7_0) {
		.journal_start = journal_start,
		.logical_blocks_used = logical_blocks_used,
		.block_map_data_blocks = block_map_data_blocks,
	};

	return VDO_SUCCESS;
}

/**
 * vdo_get_journal_operation_name() - Get the name of a journal operation.
 * @operation: The operation to name.
 *
 * Return: The name of the operation.
 */
const char *vdo_get_journal_operation_name(enum journal_operation operation)
{
	switch (operation) {
	case VDO_JOURNAL_DATA_REMAPPING:
		return "data remapping";

	case VDO_JOURNAL_BLOCK_MAP_REMAPPING:
		return "block map remapping";

	default:
		return "unknown journal operation";
	}
}

/**
 * encode_slab_depot_state_2_0() - Encode the state of a slab depot into a buffer.
 */
static void encode_slab_depot_state_2_0(u8 *buffer, size_t *offset,
					struct slab_depot_state_2_0 state)
{
	size_t initial_offset;

	vdo_encode_header(buffer, offset, &VDO_SLAB_DEPOT_HEADER_2_0);

	initial_offset = *offset;
	encode_u64_le(buffer, offset, state.slab_config.slab_blocks);
	encode_u64_le(buffer, offset, state.slab_config.data_blocks);
	encode_u64_le(buffer, offset, state.slab_config.reference_count_blocks);
	encode_u64_le(buffer, offset, state.slab_config.slab_journal_blocks);
	encode_u64_le(buffer, offset, state.slab_config.slab_journal_flushing_threshold);
	encode_u64_le(buffer, offset, state.slab_config.slab_journal_blocking_threshold);
	encode_u64_le(buffer, offset, state.slab_config.slab_journal_scrubbing_threshold);
	encode_u64_le(buffer, offset, state.first_block);
	encode_u64_le(buffer, offset, state.last_block);
	buffer[(*offset)++] = state.zone_count;

	VDO_ASSERT_LOG_ONLY(VDO_SLAB_DEPOT_HEADER_2_0.size == *offset - initial_offset,
			    "encoded block map component size must match header size");
}

/**
 * decode_slab_depot_state_2_0() - Decode slab depot component state version 2.0 from a buffer.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int decode_slab_depot_state_2_0(u8 *buffer, size_t *offset,
				       struct slab_depot_state_2_0 *state)
{
	struct header header;
	int result;
	size_t initial_offset;
	struct slab_config slab_config;
	block_count_t count;
	physical_block_number_t first_block, last_block;
	zone_count_t zone_count;

	vdo_decode_header(buffer, offset, &header);
	result = vdo_validate_header(&VDO_SLAB_DEPOT_HEADER_2_0, &header, true,
				     __func__);
	if (result != VDO_SUCCESS)
		return result;

	initial_offset = *offset;
	decode_u64_le(buffer, offset, &count);
	slab_config.slab_blocks = count;

	decode_u64_le(buffer, offset, &count);
	slab_config.data_blocks = count;

	decode_u64_le(buffer, offset, &count);
	slab_config.reference_count_blocks = count;

	decode_u64_le(buffer, offset, &count);
	slab_config.slab_journal_blocks = count;

	decode_u64_le(buffer, offset, &count);
	slab_config.slab_journal_flushing_threshold = count;

	decode_u64_le(buffer, offset, &count);
	slab_config.slab_journal_blocking_threshold = count;

	decode_u64_le(buffer, offset, &count);
	slab_config.slab_journal_scrubbing_threshold = count;

	decode_u64_le(buffer, offset, &first_block);
	decode_u64_le(buffer, offset, &last_block);
	zone_count = buffer[(*offset)++];

	result = VDO_ASSERT(VDO_SLAB_DEPOT_HEADER_2_0.size == *offset - initial_offset,
			    "decoded slab depot component size must match header size");
	if (result != VDO_SUCCESS)
		return result;

	*state = (struct slab_depot_state_2_0) {
		.slab_config = slab_config,
		.first_block = first_block,
		.last_block = last_block,
		.zone_count = zone_count,
	};

	return VDO_SUCCESS;
}

/**
 * vdo_configure_slab_depot() - Configure the slab depot.
 * @partition: The slab depot partition
 * @slab_config: The configuration of a single slab.
 * @zone_count: The number of zones the depot will use.
 * @state: The state structure to be configured.
 *
 * Configures the slab_depot for the specified storage capacity, finding the number of data blocks
 * that will fit and still leave room for the depot metadata, then return the saved state for that
 * configuration.
 *
 * Return: VDO_SUCCESS or an error code.
 */
int vdo_configure_slab_depot(const struct partition *partition,
			     struct slab_config slab_config, zone_count_t zone_count,
			     struct slab_depot_state_2_0 *state)
{
	block_count_t total_slab_blocks, total_data_blocks;
	size_t slab_count;
	physical_block_number_t last_block;
	block_count_t slab_size = slab_config.slab_blocks;

	vdo_log_debug("slabDepot %s(block_count=%llu, first_block=%llu, slab_size=%llu, zone_count=%u)",
		      __func__, (unsigned long long) partition->count,
		      (unsigned long long) partition->offset,
		      (unsigned long long) slab_size, zone_count);

	/* We do not allow runt slabs, so we waste up to a slab's worth. */
	slab_count = (partition->count / slab_size);
	if (slab_count == 0)
		return VDO_NO_SPACE;

	if (slab_count > MAX_VDO_SLABS)
		return VDO_TOO_MANY_SLABS;

	total_slab_blocks = slab_count * slab_config.slab_blocks;
	total_data_blocks = slab_count * slab_config.data_blocks;
	last_block = partition->offset + total_slab_blocks;

	*state = (struct slab_depot_state_2_0) {
		.slab_config = slab_config,
		.first_block = partition->offset,
		.last_block = last_block,
		.zone_count = zone_count,
	};

	vdo_log_debug("slab_depot last_block=%llu, total_data_blocks=%llu, slab_count=%zu, left_over=%llu",
		      (unsigned long long) last_block,
		      (unsigned long long) total_data_blocks, slab_count,
		      (unsigned long long) (partition->count - (last_block - partition->offset)));

	return VDO_SUCCESS;
}

/**
 * vdo_configure_slab() - Measure and initialize the configuration to use for each slab.
 * @slab_size: The number of blocks per slab.
 * @slab_journal_blocks: The number of blocks for the slab journal.
 * @slab_config: The slab configuration to initialize.
 *
 * Return: VDO_SUCCESS or an error code.
 */
int vdo_configure_slab(block_count_t slab_size, block_count_t slab_journal_blocks,
		       struct slab_config *slab_config)
{
	block_count_t ref_blocks, meta_blocks, data_blocks;
	block_count_t flushing_threshold, remaining, blocking_threshold;
	block_count_t minimal_extra_space, scrubbing_threshold;

	if (slab_journal_blocks >= slab_size)
		return VDO_BAD_CONFIGURATION;

	/*
	 * This calculation should technically be a recurrence, but the total number of metadata
	 * blocks is currently less than a single block of ref_counts, so we'd gain at most one
	 * data block in each slab with more iteration.
	 */
	ref_blocks = vdo_get_saved_reference_count_size(slab_size - slab_journal_blocks);
	meta_blocks = (ref_blocks + slab_journal_blocks);

	/* Make sure test code hasn't configured slabs to be too small. */
	if (meta_blocks >= slab_size)
		return VDO_BAD_CONFIGURATION;

	/*
	 * If the slab size is very small, assume this must be a unit test and override the number
	 * of data blocks to be a power of two (wasting blocks in the slab). Many tests need their
	 * data_blocks fields to be the exact capacity of the configured volume, and that used to
	 * fall out since they use a power of two for the number of data blocks, the slab size was
	 * a power of two, and every block in a slab was a data block.
	 *
	 * TODO: Try to figure out some way of structuring testParameters and unit tests so this
	 * hack isn't needed without having to edit several unit tests every time the metadata size
	 * changes by one block.
	 */
	data_blocks = slab_size - meta_blocks;
	if ((slab_size < 1024) && !is_power_of_2(data_blocks))
		data_blocks = ((block_count_t) 1 << ilog2(data_blocks));

	/*
	 * Configure the slab journal thresholds. The flush threshold is 168 of 224 blocks in
	 * production, or 3/4ths, so we use this ratio for all sizes.
	 */
	flushing_threshold = ((slab_journal_blocks * 3) + 3) / 4;
	/*
	 * The blocking threshold should be far enough from the flushing threshold to not produce
	 * delays, but far enough from the end of the journal to allow multiple successive recovery
	 * failures.
	 */
	remaining = slab_journal_blocks - flushing_threshold;
	blocking_threshold = flushing_threshold + ((remaining * 5) / 7);
	/* The scrubbing threshold should be at least 2048 entries before the end of the journal. */
	minimal_extra_space = 1 + (MAXIMUM_VDO_USER_VIOS / VDO_SLAB_JOURNAL_FULL_ENTRIES_PER_BLOCK);
	scrubbing_threshold = blocking_threshold;
	if (slab_journal_blocks > minimal_extra_space)
		scrubbing_threshold = slab_journal_blocks - minimal_extra_space;
	if (blocking_threshold > scrubbing_threshold)
		blocking_threshold = scrubbing_threshold;

	*slab_config = (struct slab_config) {
		.slab_blocks = slab_size,
		.data_blocks = data_blocks,
		.reference_count_blocks = ref_blocks,
		.slab_journal_blocks = slab_journal_blocks,
		.slab_journal_flushing_threshold = flushing_threshold,
		.slab_journal_blocking_threshold = blocking_threshold,
		.slab_journal_scrubbing_threshold = scrubbing_threshold};
	return VDO_SUCCESS;
}

/**
 * vdo_decode_slab_journal_entry() - Decode a slab journal entry.
 * @block: The journal block holding the entry.
 * @entry_count: The number of the entry.
 *
 * Return: The decoded entry.
 */
struct slab_journal_entry vdo_decode_slab_journal_entry(struct packed_slab_journal_block *block,
							journal_entry_count_t entry_count)
{
	struct slab_journal_entry entry =
		vdo_unpack_slab_journal_entry(&block->payload.entries[entry_count]);

	if (block->header.has_block_map_increments &&
	    ((block->payload.full_entries.entry_types[entry_count / 8] &
	      ((u8) 1 << (entry_count % 8))) != 0))
		entry.operation = VDO_JOURNAL_BLOCK_MAP_REMAPPING;

	return entry;
}

/**
 * allocate_partition() - Allocate a partition and add it to a layout.
 * @layout: The layout containing the partition.
 * @id: The id of the partition.
 * @offset: The offset into the layout at which the partition begins.
 * @size: The size of the partition in blocks.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int allocate_partition(struct layout *layout, u8 id,
			      physical_block_number_t offset, block_count_t size)
{
	struct partition *partition;
	int result;

	result = vdo_allocate(1, struct partition, __func__, &partition);
	if (result != VDO_SUCCESS)
		return result;

	partition->id = id;
	partition->offset = offset;
	partition->count = size;
	partition->next = layout->head;
	layout->head = partition;

	return VDO_SUCCESS;
}

/**
 * make_partition() - Create a new partition from the beginning or end of the unused space in a
 *                    layout.
 * @layout: The layout.
 * @id: The id of the partition to make.
 * @size: The number of blocks to carve out; if 0, all remaining space will be used.
 * @beginning: True if the partition should start at the beginning of the unused space.
 *
 * Return: A success or error code, particularly VDO_NO_SPACE if there are fewer than size blocks
 *         remaining.
 */
static int __must_check make_partition(struct layout *layout, enum partition_id id,
				       block_count_t size, bool beginning)
{
	int result;
	physical_block_number_t offset;
	block_count_t free_blocks = layout->last_free - layout->first_free;

	if (size == 0) {
		if (free_blocks == 0)
			return VDO_NO_SPACE;
		size = free_blocks;
	} else if (size > free_blocks) {
		return VDO_NO_SPACE;
	}

	result = vdo_get_partition(layout, id, NULL);
	if (result != VDO_UNKNOWN_PARTITION)
		return VDO_PARTITION_EXISTS;

	offset = beginning ? layout->first_free : (layout->last_free - size);

	result = allocate_partition(layout, id, offset, size);
	if (result != VDO_SUCCESS)
		return result;

	layout->num_partitions++;
	if (beginning)
		layout->first_free += size;
	else
		layout->last_free = layout->last_free - size;

	return VDO_SUCCESS;
}

/**
 * vdo_initialize_layout() - Lay out the partitions of a vdo.
 * @size: The entire size of the vdo.
 * @origin: The start of the layout on the underlying storage in blocks.
 * @block_map_blocks: The size of the block map partition.
 * @journal_blocks: The size of the journal partition.
 * @summary_blocks: The size of the slab summary partition.
 * @layout: The layout to initialize.
 *
 * Return: VDO_SUCCESS or an error.
 */
int vdo_initialize_layout(block_count_t size, physical_block_number_t offset,
			  block_count_t block_map_blocks, block_count_t journal_blocks,
			  block_count_t summary_blocks, struct layout *layout)
{
	int result;
	block_count_t necessary_size =
		(offset + block_map_blocks + journal_blocks + summary_blocks);

	if (necessary_size > size)
		return vdo_log_error_strerror(VDO_NO_SPACE,
					      "Not enough space to make a VDO");

	*layout = (struct layout) {
		.start = offset,
		.size = size,
		.first_free = offset,
		.last_free = size,
		.num_partitions = 0,
		.head = NULL,
	};

	result = make_partition(layout, VDO_BLOCK_MAP_PARTITION, block_map_blocks, true);
	if (result != VDO_SUCCESS) {
		vdo_uninitialize_layout(layout);
		return result;
	}

	result = make_partition(layout, VDO_SLAB_SUMMARY_PARTITION, summary_blocks,
				false);
	if (result != VDO_SUCCESS) {
		vdo_uninitialize_layout(layout);
		return result;
	}

	result = make_partition(layout, VDO_RECOVERY_JOURNAL_PARTITION, journal_blocks,
				false);
	if (result != VDO_SUCCESS) {
		vdo_uninitialize_layout(layout);
		return result;
	}

	result = make_partition(layout, VDO_SLAB_DEPOT_PARTITION, 0, true);
	if (result != VDO_SUCCESS)
		vdo_uninitialize_layout(layout);

	return result;
}

/**
 * vdo_uninitialize_layout() - Clean up a layout.
 * @layout: The layout to clean up.
 *
 * All partitions created by this layout become invalid pointers.
 */
void vdo_uninitialize_layout(struct layout *layout)
{
	while (layout->head != NULL) {
		struct partition *part = layout->head;

		layout->head = part->next;
		vdo_free(part);
	}

	memset(layout, 0, sizeof(struct layout));
}

/**
 * vdo_get_partition() - Get a partition by id.
 * @layout: The layout from which to get a partition.
 * @id: The id of the partition.
 * @partition_ptr: A pointer to hold the partition.
 *
 * Return: VDO_SUCCESS or an error.
 */
int vdo_get_partition(struct layout *layout, enum partition_id id,
		      struct partition **partition_ptr)
{
	struct partition *partition;

	for (partition = layout->head; partition != NULL; partition = partition->next) {
		if (partition->id == id) {
			if (partition_ptr != NULL)
				*partition_ptr = partition;
			return VDO_SUCCESS;
		}
	}

	return VDO_UNKNOWN_PARTITION;
}

/**
 * vdo_get_known_partition() - Get a partition by id from a validated layout.
 * @layout: The layout from which to get a partition.
 * @id: The id of the partition.
 *
 * Return: the partition
 */
struct partition *vdo_get_known_partition(struct layout *layout, enum partition_id id)
{
	struct partition *partition;
	int result = vdo_get_partition(layout, id, &partition);

	VDO_ASSERT_LOG_ONLY(result == VDO_SUCCESS, "layout has expected partition: %u", id);

	return partition;
}

static void encode_layout(u8 *buffer, size_t *offset, const struct layout *layout)
{
	const struct partition *partition;
	size_t initial_offset;
	struct header header = VDO_LAYOUT_HEADER_3_0;

	BUILD_BUG_ON(sizeof(enum partition_id) != sizeof(u8));
	VDO_ASSERT_LOG_ONLY(layout->num_partitions <= U8_MAX,
			    "layout partition count must fit in a byte");

	vdo_encode_header(buffer, offset, &header);

	initial_offset = *offset;
	encode_u64_le(buffer, offset, layout->first_free);
	encode_u64_le(buffer, offset, layout->last_free);
	buffer[(*offset)++] = layout->num_partitions;

	VDO_ASSERT_LOG_ONLY(sizeof(struct layout_3_0) == *offset - initial_offset,
			    "encoded size of a layout header must match structure");

	for (partition = layout->head; partition != NULL; partition = partition->next) {
		buffer[(*offset)++] = partition->id;
		encode_u64_le(buffer, offset, partition->offset);
		/* This field only exists for backwards compatibility */
		encode_u64_le(buffer, offset, 0);
		encode_u64_le(buffer, offset, partition->count);
	}

	VDO_ASSERT_LOG_ONLY(header.size == *offset - initial_offset,
			    "encoded size of a layout must match header size");
}

static int decode_layout(u8 *buffer, size_t *offset, physical_block_number_t start,
			 block_count_t size, struct layout *layout)
{
	struct header header;
	struct layout_3_0 layout_header;
	struct partition *partition;
	size_t initial_offset;
	physical_block_number_t first_free, last_free;
	u8 partition_count;
	u8 i;
	int result;

	vdo_decode_header(buffer, offset, &header);
	/* Layout is variable size, so only do a minimum size check here. */
	result = vdo_validate_header(&VDO_LAYOUT_HEADER_3_0, &header, false, __func__);
	if (result != VDO_SUCCESS)
		return result;

	initial_offset = *offset;
	decode_u64_le(buffer, offset, &first_free);
	decode_u64_le(buffer, offset, &last_free);
	partition_count = buffer[(*offset)++];
	layout_header = (struct layout_3_0) {
		.first_free = first_free,
		.last_free = last_free,
		.partition_count = partition_count,
	};

	result = VDO_ASSERT(sizeof(struct layout_3_0) == *offset - initial_offset,
			    "decoded size of a layout header must match structure");
	if (result != VDO_SUCCESS)
		return result;

	layout->start = start;
	layout->size = size;
	layout->first_free = layout_header.first_free;
	layout->last_free = layout_header.last_free;
	layout->num_partitions = layout_header.partition_count;

	if (layout->num_partitions > VDO_PARTITION_COUNT) {
		return vdo_log_error_strerror(VDO_UNKNOWN_PARTITION,
					      "layout has extra partitions");
	}

	for (i = 0; i < layout->num_partitions; i++) {
		u8 id;
		u64 partition_offset, count;

		id = buffer[(*offset)++];
		decode_u64_le(buffer, offset, &partition_offset);
		*offset += sizeof(u64);
		decode_u64_le(buffer, offset, &count);

		result = allocate_partition(layout, id, partition_offset, count);
		if (result != VDO_SUCCESS) {
			vdo_uninitialize_layout(layout);
			return result;
		}
	}

	/* Validate that the layout has all (and only) the required partitions */
	for (i = 0; i < VDO_PARTITION_COUNT; i++) {
		result = vdo_get_partition(layout, REQUIRED_PARTITIONS[i], &partition);
		if (result != VDO_SUCCESS) {
			vdo_uninitialize_layout(layout);
			return vdo_log_error_strerror(result,
						      "layout is missing required partition %u",
						      REQUIRED_PARTITIONS[i]);
		}

		start += partition->count;
	}

	if (start != size) {
		vdo_uninitialize_layout(layout);
		return vdo_log_error_strerror(UDS_BAD_STATE,
					      "partitions do not cover the layout");
	}

	return VDO_SUCCESS;
}

/**
 * pack_vdo_config() - Convert a vdo_config to its packed on-disk representation.
 * @config: The vdo config to convert.
 *
 * Return: The platform-independent representation of the config.
 */
static struct packed_vdo_config pack_vdo_config(struct vdo_config config)
{
	return (struct packed_vdo_config) {
		.logical_blocks = __cpu_to_le64(config.logical_blocks),
		.physical_blocks = __cpu_to_le64(config.physical_blocks),
		.slab_size = __cpu_to_le64(config.slab_size),
		.recovery_journal_size = __cpu_to_le64(config.recovery_journal_size),
		.slab_journal_blocks = __cpu_to_le64(config.slab_journal_blocks),
	};
}

/**
 * pack_vdo_component() - Convert a vdo_component to its packed on-disk representation.
 * @component: The VDO component data to convert.
 *
 * Return: The platform-independent representation of the component.
 */
static struct packed_vdo_component_41_0 pack_vdo_component(const struct vdo_component component)
{
	return (struct packed_vdo_component_41_0) {
		.state = __cpu_to_le32(component.state),
		.complete_recoveries = __cpu_to_le64(component.complete_recoveries),
		.read_only_recoveries = __cpu_to_le64(component.read_only_recoveries),
		.config = pack_vdo_config(component.config),
		.nonce = __cpu_to_le64(component.nonce),
	};
}

static void encode_vdo_component(u8 *buffer, size_t *offset,
				 struct vdo_component component)
{
	struct packed_vdo_component_41_0 packed;

	encode_version_number(buffer, offset, VDO_COMPONENT_DATA_41_0);
	packed = pack_vdo_component(component);
	memcpy(buffer + *offset, &packed, sizeof(packed));
	*offset += sizeof(packed);
}

/**
 * unpack_vdo_config() - Convert a packed_vdo_config to its native in-memory representation.
 * @config: The packed vdo config to convert.
 *
 * Return: The native in-memory representation of the vdo config.
 */
static struct vdo_config unpack_vdo_config(struct packed_vdo_config config)
{
	return (struct vdo_config) {
		.logical_blocks = __le64_to_cpu(config.logical_blocks),
		.physical_blocks = __le64_to_cpu(config.physical_blocks),
		.slab_size = __le64_to_cpu(config.slab_size),
		.recovery_journal_size = __le64_to_cpu(config.recovery_journal_size),
		.slab_journal_blocks = __le64_to_cpu(config.slab_journal_blocks),
	};
}

/**
 * unpack_vdo_component_41_0() - Convert a packed_vdo_component_41_0 to its native in-memory
 *				 representation.
 * @component: The packed vdo component data to convert.
 *
 * Return: The native in-memory representation of the component.
 */
static struct vdo_component unpack_vdo_component_41_0(struct packed_vdo_component_41_0 component)
{
	return (struct vdo_component) {
		.state = __le32_to_cpu(component.state),
		.complete_recoveries = __le64_to_cpu(component.complete_recoveries),
		.read_only_recoveries = __le64_to_cpu(component.read_only_recoveries),
		.config = unpack_vdo_config(component.config),
		.nonce = __le64_to_cpu(component.nonce),
	};
}

/**
 * decode_vdo_component() - Decode the component data for the vdo itself out of the super block.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int decode_vdo_component(u8 *buffer, size_t *offset, struct vdo_component *component)
{
	struct version_number version;
	struct packed_vdo_component_41_0 packed;
	int result;

	decode_version_number(buffer, offset, &version);
	result = validate_version(version, VDO_COMPONENT_DATA_41_0,
				  "VDO component data");
	if (result != VDO_SUCCESS)
		return result;

	memcpy(&packed, buffer + *offset, sizeof(packed));
	*offset += sizeof(packed);
	*component = unpack_vdo_component_41_0(packed);
	return VDO_SUCCESS;
}

/**
 * vdo_validate_config() - Validate constraints on a VDO config.
 * @config: The VDO config.
 * @physical_block_count: The minimum block count of the underlying storage.
 * @logical_block_count: The expected logical size of the VDO, or 0 if the logical size may be
 *			 unspecified.
 *
 * Return: A success or error code.
 */
int vdo_validate_config(const struct vdo_config *config,
			block_count_t physical_block_count,
			block_count_t logical_block_count)
{
	struct slab_config slab_config;
	int result;

	result = VDO_ASSERT(config->slab_size > 0, "slab size unspecified");
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(is_power_of_2(config->slab_size),
			    "slab size must be a power of two");
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(config->slab_size <= (1 << MAX_VDO_SLAB_BITS),
			    "slab size must be less than or equal to 2^%d",
			    MAX_VDO_SLAB_BITS);
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(config->slab_journal_blocks >= MINIMUM_VDO_SLAB_JOURNAL_BLOCKS,
			    "slab journal size meets minimum size");
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(config->slab_journal_blocks <= config->slab_size,
			    "slab journal size is within expected bound");
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_configure_slab(config->slab_size, config->slab_journal_blocks,
				    &slab_config);
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT((slab_config.data_blocks >= 1),
			    "slab must be able to hold at least one block");
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(config->physical_blocks > 0, "physical blocks unspecified");
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(config->physical_blocks <= MAXIMUM_VDO_PHYSICAL_BLOCKS,
			    "physical block count %llu exceeds maximum %llu",
			    (unsigned long long) config->physical_blocks,
			    (unsigned long long) MAXIMUM_VDO_PHYSICAL_BLOCKS);
	if (result != VDO_SUCCESS)
		return VDO_OUT_OF_RANGE;

	if (physical_block_count != config->physical_blocks) {
		vdo_log_error("A physical size of %llu blocks was specified, not the %llu blocks configured in the vdo super block",
			      (unsigned long long) physical_block_count,
			      (unsigned long long) config->physical_blocks);
		return VDO_PARAMETER_MISMATCH;
	}

	if (logical_block_count > 0) {
		result = VDO_ASSERT((config->logical_blocks > 0),
				    "logical blocks unspecified");
		if (result != VDO_SUCCESS)
			return result;

		if (logical_block_count != config->logical_blocks) {
			vdo_log_error("A logical size of %llu blocks was specified, but that differs from the %llu blocks configured in the vdo super block",
				      (unsigned long long) logical_block_count,
				      (unsigned long long) config->logical_blocks);
			return VDO_PARAMETER_MISMATCH;
		}
	}

	result = VDO_ASSERT(config->logical_blocks <= MAXIMUM_VDO_LOGICAL_BLOCKS,
			    "logical blocks too large");
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(config->recovery_journal_size > 0,
			    "recovery journal size unspecified");
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(is_power_of_2(config->recovery_journal_size),
			    "recovery journal size must be a power of two");
	if (result != VDO_SUCCESS)
		return result;

	return result;
}

/**
 * vdo_destroy_component_states() - Clean up any allocations in a vdo_component_states.
 * @states: The component states to destroy.
 */
void vdo_destroy_component_states(struct vdo_component_states *states)
{
	if (states == NULL)
		return;

	vdo_uninitialize_layout(&states->layout);
}

/**
 * decode_components() - Decode the components now that we know the component data is a version we
 *                       understand.
 * @buffer: The buffer being decoded.
 * @offset: The offset to start decoding from.
 * @geometry: The vdo geometry
 * @states: An object to hold the successfully decoded state.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int __must_check decode_components(u8 *buffer, size_t *offset,
					  struct volume_geometry *geometry,
					  struct vdo_component_states *states)
{
	int result;

	decode_vdo_component(buffer, offset, &states->vdo);

	result = decode_layout(buffer, offset, vdo_get_data_region_start(*geometry) + 1,
			       states->vdo.config.physical_blocks, &states->layout);
	if (result != VDO_SUCCESS)
		return result;

	result = decode_recovery_journal_state_7_0(buffer, offset,
						   &states->recovery_journal);
	if (result != VDO_SUCCESS)
		return result;

	result = decode_slab_depot_state_2_0(buffer, offset, &states->slab_depot);
	if (result != VDO_SUCCESS)
		return result;

	result = decode_block_map_state_2_0(buffer, offset, &states->block_map);
	if (result != VDO_SUCCESS)
		return result;

	VDO_ASSERT_LOG_ONLY(*offset == VDO_COMPONENT_DATA_OFFSET + VDO_COMPONENT_DATA_SIZE,
			    "All decoded component data was used");
	return VDO_SUCCESS;
}

/**
 * vdo_decode_component_states() - Decode the payload of a super block.
 * @buffer: The buffer containing the encoded super block contents.
 * @geometry: The vdo geometry
 * @states: A pointer to hold the decoded states.
 *
 * Return: VDO_SUCCESS or an error.
 */
int vdo_decode_component_states(u8 *buffer, struct volume_geometry *geometry,
				struct vdo_component_states *states)
{
	int result;
	size_t offset = VDO_COMPONENT_DATA_OFFSET;

	/* This is for backwards compatibility. */
	decode_u32_le(buffer, &offset, &states->unused);

	/* Check the VDO volume version */
	decode_version_number(buffer, &offset, &states->volume_version);
	result = validate_version(VDO_VOLUME_VERSION_67_0, states->volume_version,
				  "volume");
	if (result != VDO_SUCCESS)
		return result;

	result = decode_components(buffer, &offset, geometry, states);
	if (result != VDO_SUCCESS)
		vdo_uninitialize_layout(&states->layout);

	return result;
}

/**
 * vdo_validate_component_states() - Validate the decoded super block configuration.
 * @states: The state decoded from the super block.
 * @geometry_nonce: The nonce from the geometry block.
 * @physical_size: The minimum block count of the underlying storage.
 * @logical_size: The expected logical size of the VDO, or 0 if the logical size may be
 *                unspecified.
 *
 * Return: VDO_SUCCESS or an error if the configuration is invalid.
 */
int vdo_validate_component_states(struct vdo_component_states *states,
				  nonce_t geometry_nonce, block_count_t physical_size,
				  block_count_t logical_size)
{
	if (geometry_nonce != states->vdo.nonce) {
		return vdo_log_error_strerror(VDO_BAD_NONCE,
					      "Geometry nonce %llu does not match superblock nonce %llu",
					      (unsigned long long) geometry_nonce,
					      (unsigned long long) states->vdo.nonce);
	}

	return vdo_validate_config(&states->vdo.config, physical_size, logical_size);
}

/**
 * vdo_encode_component_states() - Encode the state of all vdo components in the super block.
 */
static void vdo_encode_component_states(u8 *buffer, size_t *offset,
					const struct vdo_component_states *states)
{
	/* This is for backwards compatibility. */
	encode_u32_le(buffer, offset, states->unused);
	encode_version_number(buffer, offset, states->volume_version);
	encode_vdo_component(buffer, offset, states->vdo);
	encode_layout(buffer, offset, &states->layout);
	encode_recovery_journal_state_7_0(buffer, offset, states->recovery_journal);
	encode_slab_depot_state_2_0(buffer, offset, states->slab_depot);
	encode_block_map_state_2_0(buffer, offset, states->block_map);

	VDO_ASSERT_LOG_ONLY(*offset == VDO_COMPONENT_DATA_OFFSET + VDO_COMPONENT_DATA_SIZE,
			    "All super block component data was encoded");
}

/**
 * vdo_encode_super_block() - Encode a super block into its on-disk representation.
 */
void vdo_encode_super_block(u8 *buffer, struct vdo_component_states *states)
{
	u32 checksum;
	struct header header = SUPER_BLOCK_HEADER_12_0;
	size_t offset = 0;

	header.size += VDO_COMPONENT_DATA_SIZE;
	vdo_encode_header(buffer, &offset, &header);
	vdo_encode_component_states(buffer, &offset, states);

	checksum = vdo_crc32(buffer, offset);
	encode_u32_le(buffer, &offset, checksum);

	/*
	 * Even though the buffer is a full block, to avoid the potential corruption from a torn
	 * write, the entire encoding must fit in the first sector.
	 */
	VDO_ASSERT_LOG_ONLY(offset <= VDO_SECTOR_SIZE,
			    "entire superblock must fit in one sector");
}

/**
 * vdo_decode_super_block() - Decode a super block from its on-disk representation.
 */
int vdo_decode_super_block(u8 *buffer)
{
	struct header header;
	int result;
	u32 checksum, saved_checksum;
	size_t offset = 0;

	/* Decode and validate the header. */
	vdo_decode_header(buffer, &offset, &header);
	result = vdo_validate_header(&SUPER_BLOCK_HEADER_12_0, &header, false, __func__);
	if (result != VDO_SUCCESS)
		return result;

	if (header.size > VDO_COMPONENT_DATA_SIZE + sizeof(u32)) {
		/*
		 * We can't check release version or checksum until we know the content size, so we
		 * have to assume a version mismatch on unexpected values.
		 */
		return vdo_log_error_strerror(VDO_UNSUPPORTED_VERSION,
					      "super block contents too large: %zu",
					      header.size);
	}

	/* Skip past the component data for now, to verify the checksum. */
	offset += VDO_COMPONENT_DATA_SIZE;

	checksum = vdo_crc32(buffer, offset);
	decode_u32_le(buffer, &offset, &saved_checksum);

	result = VDO_ASSERT(offset == VDO_SUPER_BLOCK_FIXED_SIZE + VDO_COMPONENT_DATA_SIZE,
			    "must have decoded entire superblock payload");
	if (result != VDO_SUCCESS)
		return result;

	return ((checksum != saved_checksum) ? VDO_CHECKSUM_MISMATCH : VDO_SUCCESS);
}
