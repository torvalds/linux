// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "index-layout.h"

#include <linux/random.h>

#include "logger.h"
#include "memory-alloc.h"
#include "murmurhash3.h"
#include "numeric.h"
#include "time-utils.h"

#include "config.h"
#include "open-chapter.h"
#include "volume-index.h"

/*
 * The UDS layout on storage media is divided into a number of fixed-size regions, the sizes of
 * which are computed when the index is created. Every header and region begins on 4K block
 * boundary. Save regions are further sub-divided into regions of their own.
 *
 * Each region has a kind and an instance number. Some kinds only have one instance and therefore
 * use RL_SOLE_INSTANCE (-1) as the instance number. The RL_KIND_INDEX used to use instances to
 * represent sub-indices; now, however there is only ever one sub-index and therefore one instance.
 * The RL_KIND_VOLUME_INDEX uses instances to record which zone is being saved.
 *
 * Every region header has a type and version.
 *
 *     +-+-+---------+--------+--------+-+
 *     | | |   I N D E X  0   101, 0   | |
 *     |H|C+---------+--------+--------+S|
 *     |D|f| Volume  | Save   | Save   |e|
 *     |R|g| Region  | Region | Region |a|
 *     | | | 201, -1 | 202, 0 | 202, 1 |l|
 *     +-+-+--------+---------+--------+-+
 *
 * The header contains the encoded region layout table as well as some index configuration data.
 * The sub-index region and its subdivisions are maintained in the same table.
 *
 * There are two save regions to preserve the old state in case saving the new state is incomplete.
 * They are used in alternation. Each save region is further divided into sub-regions.
 *
 *     +-+-----+------+------+-----+-----+
 *     |H| IPM | MI   | MI   |     | OC  |
 *     |D|     | zone | zone | ... |     |
 *     |R| 301 | 302  | 302  |     | 303 |
 *     | | -1  |  0   |  1   |     | -1  |
 *     +-+-----+------+------+-----+-----+
 *
 * The header contains the encoded region layout table as well as index state data for that save.
 * Each save also has a unique nonce.
 */

#define MAGIC_SIZE 32
#define NONCE_INFO_SIZE 32
#define MAX_SAVES 2

enum region_kind {
	RL_KIND_EMPTY = 0,
	RL_KIND_HEADER = 1,
	RL_KIND_CONFIG = 100,
	RL_KIND_INDEX = 101,
	RL_KIND_SEAL = 102,
	RL_KIND_VOLUME = 201,
	RL_KIND_SAVE = 202,
	RL_KIND_INDEX_PAGE_MAP = 301,
	RL_KIND_VOLUME_INDEX = 302,
	RL_KIND_OPEN_CHAPTER = 303,
};

/* Some region types are historical and are no longer used. */
enum region_type {
	RH_TYPE_FREE = 0, /* unused */
	RH_TYPE_SUPER = 1,
	RH_TYPE_SAVE = 2,
	RH_TYPE_CHECKPOINT = 3, /* unused */
	RH_TYPE_UNSAVED = 4,
};

#define RL_SOLE_INSTANCE 65535

/*
 * Super block version 2 is the first released version.
 *
 * Super block version 3 is the normal version used from RHEL 8.2 onwards.
 *
 * Super block versions 4 through 6 were incremental development versions and
 * are not supported.
 *
 * Super block version 7 is used for volumes which have been reduced in size by one chapter in
 * order to make room to prepend LVM metadata to a volume originally created without lvm. This
 * allows the index to retain most its deduplication records.
 */
#define SUPER_VERSION_MINIMUM 3
#define SUPER_VERSION_CURRENT 3
#define SUPER_VERSION_MAXIMUM 7

static const u8 LAYOUT_MAGIC[MAGIC_SIZE] = "*ALBIREO*SINGLE*FILE*LAYOUT*001*";
static const u64 REGION_MAGIC = 0x416c6252676e3031; /* 'AlbRgn01' */

struct region_header {
	u64 magic;
	u64 region_blocks;
	u16 type;
	/* Currently always version 1 */
	u16 version;
	u16 region_count;
	u16 payload;
};

struct layout_region {
	u64 start_block;
	u64 block_count;
	u32 __unused;
	u16 kind;
	u16 instance;
};

struct region_table {
	size_t encoded_size;
	struct region_header header;
	struct layout_region regions[];
};

struct index_save_data {
	u64 timestamp;
	u64 nonce;
	/* Currently always version 1 */
	u32 version;
	u32 unused__;
};

struct index_state_version {
	s32 signature;
	s32 version_id;
};

static const struct index_state_version INDEX_STATE_VERSION_301 = {
	.signature  = -1,
	.version_id = 301,
};

struct index_state_data301 {
	struct index_state_version version;
	u64 newest_chapter;
	u64 oldest_chapter;
	u64 last_save;
	u32 unused;
	u32 padding;
};

struct index_save_layout {
	unsigned int zone_count;
	struct layout_region index_save;
	struct layout_region header;
	struct layout_region index_page_map;
	struct layout_region free_space;
	struct layout_region volume_index_zones[MAX_ZONES];
	struct layout_region open_chapter;
	struct index_save_data save_data;
	struct index_state_data301 state_data;
};

struct sub_index_layout {
	u64 nonce;
	struct layout_region sub_index;
	struct layout_region volume;
	struct index_save_layout *saves;
};

struct super_block_data {
	u8 magic_label[MAGIC_SIZE];
	u8 nonce_info[NONCE_INFO_SIZE];
	u64 nonce;
	u32 version;
	u32 block_size;
	u16 index_count;
	u16 max_saves;
	/* Padding reflects a blank field on permanent storage */
	u8 padding[4];
	u64 open_chapter_blocks;
	u64 page_map_blocks;
	u64 volume_offset;
	u64 start_offset;
};

struct index_layout {
	struct io_factory *factory;
	size_t factory_size;
	off_t offset;
	struct super_block_data super;
	struct layout_region header;
	struct layout_region config;
	struct sub_index_layout index;
	struct layout_region seal;
	u64 total_blocks;
};

struct save_layout_sizes {
	unsigned int save_count;
	size_t block_size;
	u64 volume_blocks;
	u64 volume_index_blocks;
	u64 page_map_blocks;
	u64 open_chapter_blocks;
	u64 save_blocks;
	u64 sub_index_blocks;
	u64 total_blocks;
	size_t total_size;
};

static inline bool is_converted_super_block(struct super_block_data *super)
{
	return super->version == 7;
}

static int __must_check compute_sizes(const struct uds_configuration *config,
				      struct save_layout_sizes *sls)
{
	int result;
	struct index_geometry *geometry = config->geometry;

	memset(sls, 0, sizeof(*sls));
	sls->save_count = MAX_SAVES;
	sls->block_size = UDS_BLOCK_SIZE;
	sls->volume_blocks = geometry->bytes_per_volume / sls->block_size;

	result = uds_compute_volume_index_save_blocks(config, sls->block_size,
						      &sls->volume_index_blocks);
	if (result != UDS_SUCCESS)
		return vdo_log_error_strerror(result, "cannot compute index save size");

	sls->page_map_blocks =
		DIV_ROUND_UP(uds_compute_index_page_map_save_size(geometry),
			     sls->block_size);
	sls->open_chapter_blocks =
		DIV_ROUND_UP(uds_compute_saved_open_chapter_size(geometry),
			     sls->block_size);
	sls->save_blocks =
		1 + (sls->volume_index_blocks + sls->page_map_blocks + sls->open_chapter_blocks);
	sls->sub_index_blocks = sls->volume_blocks + (sls->save_count * sls->save_blocks);
	sls->total_blocks = 3 + sls->sub_index_blocks;
	sls->total_size = sls->total_blocks * sls->block_size;

	return UDS_SUCCESS;
}

int uds_compute_index_size(const struct uds_parameters *parameters, u64 *index_size)
{
	int result;
	struct uds_configuration *index_config;
	struct save_layout_sizes sizes;

	if (index_size == NULL) {
		vdo_log_error("Missing output size pointer");
		return -EINVAL;
	}

	result = uds_make_configuration(parameters, &index_config);
	if (result != UDS_SUCCESS) {
		vdo_log_error_strerror(result, "cannot compute index size");
		return uds_status_to_errno(result);
	}

	result = compute_sizes(index_config, &sizes);
	uds_free_configuration(index_config);
	if (result != UDS_SUCCESS)
		return uds_status_to_errno(result);

	*index_size = sizes.total_size;
	return UDS_SUCCESS;
}

/* Create unique data using the current time and a pseudorandom number. */
static void create_unique_nonce_data(u8 *buffer)
{
	ktime_t now = current_time_ns(CLOCK_REALTIME);
	u32 rand;
	size_t offset = 0;

	get_random_bytes(&rand, sizeof(u32));
	memcpy(buffer + offset, &now, sizeof(now));
	offset += sizeof(now);
	memcpy(buffer + offset, &rand, sizeof(rand));
	offset += sizeof(rand);
	while (offset < NONCE_INFO_SIZE) {
		size_t len = min(NONCE_INFO_SIZE - offset, offset);

		memcpy(buffer + offset, buffer, len);
		offset += len;
	}
}

static u64 hash_stuff(u64 start, const void *data, size_t len)
{
	u32 seed = start ^ (start >> 27);
	u8 hash_buffer[16];

	murmurhash3_128(data, len, seed, hash_buffer);
	return get_unaligned_le64(hash_buffer + 4);
}

/* Generate a primary nonce from the provided data. */
static u64 generate_primary_nonce(const void *data, size_t len)
{
	return hash_stuff(0xa1b1e0fc, data, len);
}

/*
 * Deterministically generate a secondary nonce from an existing nonce and some arbitrary data by
 * hashing the original nonce and the data to produce a new nonce.
 */
static u64 generate_secondary_nonce(u64 nonce, const void *data, size_t len)
{
	return hash_stuff(nonce + 1, data, len);
}

static int __must_check open_layout_reader(struct index_layout *layout,
					   struct layout_region *lr, off_t offset,
					   struct buffered_reader **reader_ptr)
{
	return uds_make_buffered_reader(layout->factory, lr->start_block + offset,
					lr->block_count, reader_ptr);
}

static int open_region_reader(struct index_layout *layout, struct layout_region *region,
			      struct buffered_reader **reader_ptr)
{
	return open_layout_reader(layout, region, -layout->super.start_offset,
				  reader_ptr);
}

static int __must_check open_layout_writer(struct index_layout *layout,
					   struct layout_region *lr, off_t offset,
					   struct buffered_writer **writer_ptr)
{
	return uds_make_buffered_writer(layout->factory, lr->start_block + offset,
					lr->block_count, writer_ptr);
}

static int open_region_writer(struct index_layout *layout, struct layout_region *region,
			      struct buffered_writer **writer_ptr)
{
	return open_layout_writer(layout, region, -layout->super.start_offset,
				  writer_ptr);
}

static void generate_super_block_data(struct save_layout_sizes *sls,
				      struct super_block_data *super)
{
	memset(super, 0, sizeof(*super));
	memcpy(super->magic_label, LAYOUT_MAGIC, MAGIC_SIZE);
	create_unique_nonce_data(super->nonce_info);

	super->nonce = generate_primary_nonce(super->nonce_info,
					      sizeof(super->nonce_info));
	super->version = SUPER_VERSION_CURRENT;
	super->block_size = sls->block_size;
	super->index_count = 1;
	super->max_saves = sls->save_count;
	super->open_chapter_blocks = sls->open_chapter_blocks;
	super->page_map_blocks = sls->page_map_blocks;
	super->volume_offset = 0;
	super->start_offset = 0;
}

static void define_sub_index_nonce(struct index_layout *layout)
{
	struct sub_index_nonce_data {
		u64 offset;
		u16 index_id;
	};
	struct sub_index_layout *sil = &layout->index;
	u64 primary_nonce = layout->super.nonce;
	u8 buffer[sizeof(struct sub_index_nonce_data)] = { 0 };
	size_t offset = 0;

	encode_u64_le(buffer, &offset, sil->sub_index.start_block);
	encode_u16_le(buffer, &offset, 0);
	sil->nonce = generate_secondary_nonce(primary_nonce, buffer, sizeof(buffer));
	if (sil->nonce == 0) {
		sil->nonce = generate_secondary_nonce(~primary_nonce + 1, buffer,
						      sizeof(buffer));
	}
}

static void setup_sub_index(struct index_layout *layout, u64 start_block,
			    struct save_layout_sizes *sls)
{
	struct sub_index_layout *sil = &layout->index;
	u64 next_block = start_block;
	unsigned int i;

	sil->sub_index = (struct layout_region) {
		.start_block = start_block,
		.block_count = sls->sub_index_blocks,
		.kind = RL_KIND_INDEX,
		.instance = 0,
	};

	sil->volume = (struct layout_region) {
		.start_block = next_block,
		.block_count = sls->volume_blocks,
		.kind = RL_KIND_VOLUME,
		.instance = RL_SOLE_INSTANCE,
	};

	next_block += sls->volume_blocks;

	for (i = 0; i < sls->save_count; i++) {
		sil->saves[i].index_save = (struct layout_region) {
			.start_block = next_block,
			.block_count = sls->save_blocks,
			.kind = RL_KIND_SAVE,
			.instance = i,
		};

		next_block += sls->save_blocks;
	}

	define_sub_index_nonce(layout);
}

static void initialize_layout(struct index_layout *layout, struct save_layout_sizes *sls)
{
	u64 next_block = layout->offset / sls->block_size;

	layout->total_blocks = sls->total_blocks;
	generate_super_block_data(sls, &layout->super);
	layout->header = (struct layout_region) {
		.start_block = next_block++,
		.block_count = 1,
		.kind = RL_KIND_HEADER,
		.instance = RL_SOLE_INSTANCE,
	};

	layout->config = (struct layout_region) {
		.start_block = next_block++,
		.block_count = 1,
		.kind = RL_KIND_CONFIG,
		.instance = RL_SOLE_INSTANCE,
	};

	setup_sub_index(layout, next_block, sls);
	next_block += sls->sub_index_blocks;

	layout->seal = (struct layout_region) {
		.start_block = next_block,
		.block_count = 1,
		.kind = RL_KIND_SEAL,
		.instance = RL_SOLE_INSTANCE,
	};
}

static int __must_check make_index_save_region_table(struct index_save_layout *isl,
						     struct region_table **table_ptr)
{
	int result;
	unsigned int z;
	struct region_table *table;
	struct layout_region *lr;
	u16 region_count;
	size_t payload;
	size_t type;

	if (isl->zone_count > 0) {
		/*
		 * Normal save regions: header, page map, volume index zones,
		 * open chapter, and possibly free space.
		 */
		region_count = 3 + isl->zone_count;
		if (isl->free_space.block_count > 0)
			region_count++;

		payload = sizeof(isl->save_data) + sizeof(isl->state_data);
		type = RH_TYPE_SAVE;
	} else {
		/* Empty save regions: header, page map, free space. */
		region_count = 3;
		payload = sizeof(isl->save_data);
		type = RH_TYPE_UNSAVED;
	}

	result = vdo_allocate_extended(struct region_table, region_count,
				       struct layout_region,
				       "layout region table for ISL", &table);
	if (result != VDO_SUCCESS)
		return result;

	lr = &table->regions[0];
	*lr++ = isl->header;
	*lr++ = isl->index_page_map;
	for (z = 0; z < isl->zone_count; z++)
		*lr++ = isl->volume_index_zones[z];

	if (isl->zone_count > 0)
		*lr++ = isl->open_chapter;

	if (isl->free_space.block_count > 0)
		*lr++ = isl->free_space;

	table->header = (struct region_header) {
		.magic = REGION_MAGIC,
		.region_blocks = isl->index_save.block_count,
		.type = type,
		.version = 1,
		.region_count = region_count,
		.payload = payload,
	};

	table->encoded_size = (sizeof(struct region_header) + payload +
			       region_count * sizeof(struct layout_region));
	*table_ptr = table;
	return UDS_SUCCESS;
}

static void encode_region_table(u8 *buffer, size_t *offset, struct region_table *table)
{
	unsigned int i;

	encode_u64_le(buffer, offset, REGION_MAGIC);
	encode_u64_le(buffer, offset, table->header.region_blocks);
	encode_u16_le(buffer, offset, table->header.type);
	encode_u16_le(buffer, offset, table->header.version);
	encode_u16_le(buffer, offset, table->header.region_count);
	encode_u16_le(buffer, offset, table->header.payload);

	for (i = 0; i < table->header.region_count; i++) {
		encode_u64_le(buffer, offset, table->regions[i].start_block);
		encode_u64_le(buffer, offset, table->regions[i].block_count);
		encode_u32_le(buffer, offset, 0);
		encode_u16_le(buffer, offset, table->regions[i].kind);
		encode_u16_le(buffer, offset, table->regions[i].instance);
	}
}

static int __must_check write_index_save_header(struct index_save_layout *isl,
						struct region_table *table,
						struct buffered_writer *writer)
{
	int result;
	u8 *buffer;
	size_t offset = 0;

	result = vdo_allocate(table->encoded_size, u8, "index save data", &buffer);
	if (result != VDO_SUCCESS)
		return result;

	encode_region_table(buffer, &offset, table);
	encode_u64_le(buffer, &offset, isl->save_data.timestamp);
	encode_u64_le(buffer, &offset, isl->save_data.nonce);
	encode_u32_le(buffer, &offset, isl->save_data.version);
	encode_u32_le(buffer, &offset, 0);
	if (isl->zone_count > 0) {
		encode_u32_le(buffer, &offset, INDEX_STATE_VERSION_301.signature);
		encode_u32_le(buffer, &offset, INDEX_STATE_VERSION_301.version_id);
		encode_u64_le(buffer, &offset, isl->state_data.newest_chapter);
		encode_u64_le(buffer, &offset, isl->state_data.oldest_chapter);
		encode_u64_le(buffer, &offset, isl->state_data.last_save);
		encode_u64_le(buffer, &offset, 0);
	}

	result = uds_write_to_buffered_writer(writer, buffer, offset);
	vdo_free(buffer);
	if (result != UDS_SUCCESS)
		return result;

	return uds_flush_buffered_writer(writer);
}

static int write_index_save_layout(struct index_layout *layout,
				   struct index_save_layout *isl)
{
	int result;
	struct region_table *table;
	struct buffered_writer *writer;

	result = make_index_save_region_table(isl, &table);
	if (result != UDS_SUCCESS)
		return result;

	result = open_region_writer(layout, &isl->header, &writer);
	if (result != UDS_SUCCESS) {
		vdo_free(table);
		return result;
	}

	result = write_index_save_header(isl, table, writer);
	vdo_free(table);
	uds_free_buffered_writer(writer);

	return result;
}

static void reset_index_save_layout(struct index_save_layout *isl, u64 page_map_blocks)
{
	u64 free_blocks;
	u64 next_block = isl->index_save.start_block;

	isl->zone_count = 0;
	memset(&isl->save_data, 0, sizeof(isl->save_data));

	isl->header = (struct layout_region) {
		.start_block = next_block++,
		.block_count = 1,
		.kind = RL_KIND_HEADER,
		.instance = RL_SOLE_INSTANCE,
	};

	isl->index_page_map = (struct layout_region) {
		.start_block = next_block,
		.block_count = page_map_blocks,
		.kind = RL_KIND_INDEX_PAGE_MAP,
		.instance = RL_SOLE_INSTANCE,
	};

	next_block += page_map_blocks;

	free_blocks = isl->index_save.block_count - page_map_blocks - 1;
	isl->free_space = (struct layout_region) {
		.start_block = next_block,
		.block_count = free_blocks,
		.kind = RL_KIND_EMPTY,
		.instance = RL_SOLE_INSTANCE,
	};
}

static int __must_check invalidate_old_save(struct index_layout *layout,
					    struct index_save_layout *isl)
{
	reset_index_save_layout(isl, layout->super.page_map_blocks);
	return write_index_save_layout(layout, isl);
}

static int discard_index_state_data(struct index_layout *layout)
{
	int result;
	int saved_result = UDS_SUCCESS;
	unsigned int i;

	for (i = 0; i < layout->super.max_saves; i++) {
		result = invalidate_old_save(layout, &layout->index.saves[i]);
		if (result != UDS_SUCCESS)
			saved_result = result;
	}

	if (saved_result != UDS_SUCCESS) {
		return vdo_log_error_strerror(result,
					      "%s: cannot destroy all index saves",
					      __func__);
	}

	return UDS_SUCCESS;
}

static int __must_check make_layout_region_table(struct index_layout *layout,
						 struct region_table **table_ptr)
{
	int result;
	unsigned int i;
	/* Regions: header, config, index, volume, saves, seal */
	u16 region_count = 5 + layout->super.max_saves;
	u16 payload;
	struct region_table *table;
	struct layout_region *lr;

	result = vdo_allocate_extended(struct region_table, region_count,
				       struct layout_region, "layout region table",
				       &table);
	if (result != VDO_SUCCESS)
		return result;

	lr = &table->regions[0];
	*lr++ = layout->header;
	*lr++ = layout->config;
	*lr++ = layout->index.sub_index;
	*lr++ = layout->index.volume;

	for (i = 0; i < layout->super.max_saves; i++)
		*lr++ = layout->index.saves[i].index_save;

	*lr++ = layout->seal;

	if (is_converted_super_block(&layout->super)) {
		payload = sizeof(struct super_block_data);
	} else {
		payload = (sizeof(struct super_block_data) -
			   sizeof(layout->super.volume_offset) -
			   sizeof(layout->super.start_offset));
	}

	table->header = (struct region_header) {
		.magic = REGION_MAGIC,
		.region_blocks = layout->total_blocks,
		.type = RH_TYPE_SUPER,
		.version = 1,
		.region_count = region_count,
		.payload = payload,
	};

	table->encoded_size = (sizeof(struct region_header) + payload +
			       region_count * sizeof(struct layout_region));
	*table_ptr = table;
	return UDS_SUCCESS;
}

static int __must_check write_layout_header(struct index_layout *layout,
					    struct region_table *table,
					    struct buffered_writer *writer)
{
	int result;
	u8 *buffer;
	size_t offset = 0;

	result = vdo_allocate(table->encoded_size, u8, "layout data", &buffer);
	if (result != VDO_SUCCESS)
		return result;

	encode_region_table(buffer, &offset, table);
	memcpy(buffer + offset, &layout->super.magic_label, MAGIC_SIZE);
	offset += MAGIC_SIZE;
	memcpy(buffer + offset, &layout->super.nonce_info, NONCE_INFO_SIZE);
	offset += NONCE_INFO_SIZE;
	encode_u64_le(buffer, &offset, layout->super.nonce);
	encode_u32_le(buffer, &offset, layout->super.version);
	encode_u32_le(buffer, &offset, layout->super.block_size);
	encode_u16_le(buffer, &offset, layout->super.index_count);
	encode_u16_le(buffer, &offset, layout->super.max_saves);
	encode_u32_le(buffer, &offset, 0);
	encode_u64_le(buffer, &offset, layout->super.open_chapter_blocks);
	encode_u64_le(buffer, &offset, layout->super.page_map_blocks);

	if (is_converted_super_block(&layout->super)) {
		encode_u64_le(buffer, &offset, layout->super.volume_offset);
		encode_u64_le(buffer, &offset, layout->super.start_offset);
	}

	result = uds_write_to_buffered_writer(writer, buffer, offset);
	vdo_free(buffer);
	if (result != UDS_SUCCESS)
		return result;

	return uds_flush_buffered_writer(writer);
}

static int __must_check write_uds_index_config(struct index_layout *layout,
					       struct uds_configuration *config,
					       off_t offset)
{
	int result;
	struct buffered_writer *writer = NULL;

	result = open_layout_writer(layout, &layout->config, offset, &writer);
	if (result != UDS_SUCCESS)
		return vdo_log_error_strerror(result, "failed to open config region");

	result = uds_write_config_contents(writer, config, layout->super.version);
	if (result != UDS_SUCCESS) {
		uds_free_buffered_writer(writer);
		return vdo_log_error_strerror(result, "failed to write config region");
	}

	result = uds_flush_buffered_writer(writer);
	if (result != UDS_SUCCESS) {
		uds_free_buffered_writer(writer);
		return vdo_log_error_strerror(result, "cannot flush config writer");
	}

	uds_free_buffered_writer(writer);
	return UDS_SUCCESS;
}

static int __must_check save_layout(struct index_layout *layout, off_t offset)
{
	int result;
	struct buffered_writer *writer = NULL;
	struct region_table *table;

	result = make_layout_region_table(layout, &table);
	if (result != UDS_SUCCESS)
		return result;

	result = open_layout_writer(layout, &layout->header, offset, &writer);
	if (result != UDS_SUCCESS) {
		vdo_free(table);
		return result;
	}

	result = write_layout_header(layout, table, writer);
	vdo_free(table);
	uds_free_buffered_writer(writer);

	return result;
}

static int create_index_layout(struct index_layout *layout, struct uds_configuration *config)
{
	int result;
	struct save_layout_sizes sizes;

	result = compute_sizes(config, &sizes);
	if (result != UDS_SUCCESS)
		return result;

	result = vdo_allocate(sizes.save_count, struct index_save_layout, __func__,
			      &layout->index.saves);
	if (result != VDO_SUCCESS)
		return result;

	initialize_layout(layout, &sizes);

	result = discard_index_state_data(layout);
	if (result != UDS_SUCCESS)
		return result;

	result = write_uds_index_config(layout, config, 0);
	if (result != UDS_SUCCESS)
		return result;

	return save_layout(layout, 0);
}

static u64 generate_index_save_nonce(u64 volume_nonce, struct index_save_layout *isl)
{
	struct save_nonce_data {
		struct index_save_data data;
		u64 offset;
	} nonce_data;
	u8 buffer[sizeof(nonce_data)];
	size_t offset = 0;

	encode_u64_le(buffer, &offset, isl->save_data.timestamp);
	encode_u64_le(buffer, &offset, 0);
	encode_u32_le(buffer, &offset, isl->save_data.version);
	encode_u32_le(buffer, &offset, 0U);
	encode_u64_le(buffer, &offset, isl->index_save.start_block);
	VDO_ASSERT_LOG_ONLY(offset == sizeof(nonce_data),
			    "%zu bytes encoded of %zu expected",
			    offset, sizeof(nonce_data));
	return generate_secondary_nonce(volume_nonce, buffer, sizeof(buffer));
}

static u64 validate_index_save_layout(struct index_save_layout *isl, u64 volume_nonce)
{
	if ((isl->zone_count == 0) || (isl->save_data.timestamp == 0))
		return 0;

	if (isl->save_data.nonce != generate_index_save_nonce(volume_nonce, isl))
		return 0;

	return isl->save_data.timestamp;
}

static int find_latest_uds_index_save_slot(struct index_layout *layout,
					   struct index_save_layout **isl_ptr)
{
	struct index_save_layout *latest = NULL;
	struct index_save_layout *isl;
	unsigned int i;
	u64 save_time = 0;
	u64 latest_time = 0;

	for (i = 0; i < layout->super.max_saves; i++) {
		isl = &layout->index.saves[i];
		save_time = validate_index_save_layout(isl, layout->index.nonce);
		if (save_time > latest_time) {
			latest = isl;
			latest_time = save_time;
		}
	}

	if (latest == NULL) {
		vdo_log_error("No valid index save found");
		return UDS_INDEX_NOT_SAVED_CLEANLY;
	}

	*isl_ptr = latest;
	return UDS_SUCCESS;
}

int uds_discard_open_chapter(struct index_layout *layout)
{
	int result;
	struct index_save_layout *isl;
	struct buffered_writer *writer;

	result = find_latest_uds_index_save_slot(layout, &isl);
	if (result != UDS_SUCCESS)
		return result;

	result = open_region_writer(layout, &isl->open_chapter, &writer);
	if (result != UDS_SUCCESS)
		return result;

	result = uds_write_to_buffered_writer(writer, NULL, UDS_BLOCK_SIZE);
	if (result != UDS_SUCCESS) {
		uds_free_buffered_writer(writer);
		return result;
	}

	result = uds_flush_buffered_writer(writer);
	uds_free_buffered_writer(writer);
	return result;
}

int uds_load_index_state(struct index_layout *layout, struct uds_index *index)
{
	int result;
	unsigned int zone;
	struct index_save_layout *isl;
	struct buffered_reader *readers[MAX_ZONES];

	result = find_latest_uds_index_save_slot(layout, &isl);
	if (result != UDS_SUCCESS)
		return result;

	index->newest_virtual_chapter = isl->state_data.newest_chapter;
	index->oldest_virtual_chapter = isl->state_data.oldest_chapter;
	index->last_save = isl->state_data.last_save;

	result = open_region_reader(layout, &isl->open_chapter, &readers[0]);
	if (result != UDS_SUCCESS)
		return result;

	result = uds_load_open_chapter(index, readers[0]);
	uds_free_buffered_reader(readers[0]);
	if (result != UDS_SUCCESS)
		return result;

	for (zone = 0; zone < isl->zone_count; zone++) {
		result = open_region_reader(layout, &isl->volume_index_zones[zone],
					    &readers[zone]);
		if (result != UDS_SUCCESS) {
			for (; zone > 0; zone--)
				uds_free_buffered_reader(readers[zone - 1]);

			return result;
		}
	}

	result = uds_load_volume_index(index->volume_index, readers, isl->zone_count);
	for (zone = 0; zone < isl->zone_count; zone++)
		uds_free_buffered_reader(readers[zone]);
	if (result != UDS_SUCCESS)
		return result;

	result = open_region_reader(layout, &isl->index_page_map, &readers[0]);
	if (result != UDS_SUCCESS)
		return result;

	result = uds_read_index_page_map(index->volume->index_page_map, readers[0]);
	uds_free_buffered_reader(readers[0]);

	return result;
}

static struct index_save_layout *select_oldest_index_save_layout(struct index_layout *layout)
{
	struct index_save_layout *oldest = NULL;
	struct index_save_layout *isl;
	unsigned int i;
	u64 save_time = 0;
	u64 oldest_time = 0;

	for (i = 0; i < layout->super.max_saves; i++) {
		isl = &layout->index.saves[i];
		save_time = validate_index_save_layout(isl, layout->index.nonce);
		if (oldest == NULL || save_time < oldest_time) {
			oldest = isl;
			oldest_time = save_time;
		}
	}

	return oldest;
}

static void instantiate_index_save_layout(struct index_save_layout *isl,
					  struct super_block_data *super,
					  u64 volume_nonce, unsigned int zone_count)
{
	unsigned int z;
	u64 next_block;
	u64 free_blocks;
	u64 volume_index_blocks;

	isl->zone_count = zone_count;
	memset(&isl->save_data, 0, sizeof(isl->save_data));
	isl->save_data.timestamp = ktime_to_ms(current_time_ns(CLOCK_REALTIME));
	isl->save_data.version = 1;
	isl->save_data.nonce = generate_index_save_nonce(volume_nonce, isl);

	next_block = isl->index_save.start_block;
	isl->header = (struct layout_region) {
		.start_block = next_block++,
		.block_count = 1,
		.kind = RL_KIND_HEADER,
		.instance = RL_SOLE_INSTANCE,
	};

	isl->index_page_map = (struct layout_region) {
		.start_block = next_block,
		.block_count = super->page_map_blocks,
		.kind = RL_KIND_INDEX_PAGE_MAP,
		.instance = RL_SOLE_INSTANCE,
	};
	next_block += super->page_map_blocks;

	free_blocks = (isl->index_save.block_count - 1 -
		       super->page_map_blocks -
		       super->open_chapter_blocks);
	volume_index_blocks = free_blocks / isl->zone_count;
	for (z = 0; z < isl->zone_count; z++) {
		isl->volume_index_zones[z] = (struct layout_region) {
			.start_block = next_block,
			.block_count = volume_index_blocks,
			.kind = RL_KIND_VOLUME_INDEX,
			.instance = z,
		};

		next_block += volume_index_blocks;
		free_blocks -= volume_index_blocks;
	}

	isl->open_chapter = (struct layout_region) {
		.start_block = next_block,
		.block_count = super->open_chapter_blocks,
		.kind = RL_KIND_OPEN_CHAPTER,
		.instance = RL_SOLE_INSTANCE,
	};

	next_block += super->open_chapter_blocks;

	isl->free_space = (struct layout_region) {
		.start_block = next_block,
		.block_count = free_blocks,
		.kind = RL_KIND_EMPTY,
		.instance = RL_SOLE_INSTANCE,
	};
}

static int setup_uds_index_save_slot(struct index_layout *layout,
				     unsigned int zone_count,
				     struct index_save_layout **isl_ptr)
{
	int result;
	struct index_save_layout *isl;

	isl = select_oldest_index_save_layout(layout);
	result = invalidate_old_save(layout, isl);
	if (result != UDS_SUCCESS)
		return result;

	instantiate_index_save_layout(isl, &layout->super, layout->index.nonce,
				      zone_count);

	*isl_ptr = isl;
	return UDS_SUCCESS;
}

static void cancel_uds_index_save(struct index_save_layout *isl)
{
	memset(&isl->save_data, 0, sizeof(isl->save_data));
	memset(&isl->state_data, 0, sizeof(isl->state_data));
	isl->zone_count = 0;
}

int uds_save_index_state(struct index_layout *layout, struct uds_index *index)
{
	int result;
	unsigned int zone;
	struct index_save_layout *isl;
	struct buffered_writer *writers[MAX_ZONES];

	result = setup_uds_index_save_slot(layout, index->zone_count, &isl);
	if (result != UDS_SUCCESS)
		return result;

	isl->state_data	= (struct index_state_data301) {
		.newest_chapter = index->newest_virtual_chapter,
		.oldest_chapter = index->oldest_virtual_chapter,
		.last_save = index->last_save,
	};

	result = open_region_writer(layout, &isl->open_chapter, &writers[0]);
	if (result != UDS_SUCCESS) {
		cancel_uds_index_save(isl);
		return result;
	}

	result = uds_save_open_chapter(index, writers[0]);
	uds_free_buffered_writer(writers[0]);
	if (result != UDS_SUCCESS) {
		cancel_uds_index_save(isl);
		return result;
	}

	for (zone = 0; zone < index->zone_count; zone++) {
		result = open_region_writer(layout, &isl->volume_index_zones[zone],
					    &writers[zone]);
		if (result != UDS_SUCCESS) {
			for (; zone > 0; zone--)
				uds_free_buffered_writer(writers[zone - 1]);

			cancel_uds_index_save(isl);
			return result;
		}
	}

	result = uds_save_volume_index(index->volume_index, writers, index->zone_count);
	for (zone = 0; zone < index->zone_count; zone++)
		uds_free_buffered_writer(writers[zone]);
	if (result != UDS_SUCCESS) {
		cancel_uds_index_save(isl);
		return result;
	}

	result = open_region_writer(layout, &isl->index_page_map, &writers[0]);
	if (result != UDS_SUCCESS) {
		cancel_uds_index_save(isl);
		return result;
	}

	result = uds_write_index_page_map(index->volume->index_page_map, writers[0]);
	uds_free_buffered_writer(writers[0]);
	if (result != UDS_SUCCESS) {
		cancel_uds_index_save(isl);
		return result;
	}

	return write_index_save_layout(layout, isl);
}

static int __must_check load_region_table(struct buffered_reader *reader,
					  struct region_table **table_ptr)
{
	int result;
	unsigned int i;
	struct region_header header;
	struct region_table *table;
	u8 buffer[sizeof(struct region_header)];
	size_t offset = 0;

	result = uds_read_from_buffered_reader(reader, buffer, sizeof(buffer));
	if (result != UDS_SUCCESS)
		return vdo_log_error_strerror(result, "cannot read region table header");

	decode_u64_le(buffer, &offset, &header.magic);
	decode_u64_le(buffer, &offset, &header.region_blocks);
	decode_u16_le(buffer, &offset, &header.type);
	decode_u16_le(buffer, &offset, &header.version);
	decode_u16_le(buffer, &offset, &header.region_count);
	decode_u16_le(buffer, &offset, &header.payload);

	if (header.magic != REGION_MAGIC)
		return UDS_NO_INDEX;

	if (header.version != 1) {
		return vdo_log_error_strerror(UDS_UNSUPPORTED_VERSION,
					      "unknown region table version %hu",
					      header.version);
	}

	result = vdo_allocate_extended(struct region_table, header.region_count,
				       struct layout_region,
				       "single file layout region table", &table);
	if (result != VDO_SUCCESS)
		return result;

	table->header = header;
	for (i = 0; i < header.region_count; i++) {
		u8 region_buffer[sizeof(struct layout_region)];

		offset = 0;
		result = uds_read_from_buffered_reader(reader, region_buffer,
						       sizeof(region_buffer));
		if (result != UDS_SUCCESS) {
			vdo_free(table);
			return vdo_log_error_strerror(UDS_CORRUPT_DATA,
						      "cannot read region table layouts");
		}

		decode_u64_le(region_buffer, &offset, &table->regions[i].start_block);
		decode_u64_le(region_buffer, &offset, &table->regions[i].block_count);
		offset += sizeof(u32);
		decode_u16_le(region_buffer, &offset, &table->regions[i].kind);
		decode_u16_le(region_buffer, &offset, &table->regions[i].instance);
	}

	*table_ptr = table;
	return UDS_SUCCESS;
}

static int __must_check read_super_block_data(struct buffered_reader *reader,
					      struct index_layout *layout,
					      size_t saved_size)
{
	int result;
	struct super_block_data *super = &layout->super;
	u8 *buffer;
	size_t offset = 0;

	result = vdo_allocate(saved_size, u8, "super block data", &buffer);
	if (result != VDO_SUCCESS)
		return result;

	result = uds_read_from_buffered_reader(reader, buffer, saved_size);
	if (result != UDS_SUCCESS) {
		vdo_free(buffer);
		return vdo_log_error_strerror(result, "cannot read region table header");
	}

	memcpy(&super->magic_label, buffer, MAGIC_SIZE);
	offset += MAGIC_SIZE;
	memcpy(&super->nonce_info, buffer + offset, NONCE_INFO_SIZE);
	offset += NONCE_INFO_SIZE;
	decode_u64_le(buffer, &offset, &super->nonce);
	decode_u32_le(buffer, &offset, &super->version);
	decode_u32_le(buffer, &offset, &super->block_size);
	decode_u16_le(buffer, &offset, &super->index_count);
	decode_u16_le(buffer, &offset, &super->max_saves);
	offset += sizeof(u32);
	decode_u64_le(buffer, &offset, &super->open_chapter_blocks);
	decode_u64_le(buffer, &offset, &super->page_map_blocks);

	if (is_converted_super_block(super)) {
		decode_u64_le(buffer, &offset, &super->volume_offset);
		decode_u64_le(buffer, &offset, &super->start_offset);
	} else {
		super->volume_offset = 0;
		super->start_offset = 0;
	}

	vdo_free(buffer);

	if (memcmp(super->magic_label, LAYOUT_MAGIC, MAGIC_SIZE) != 0)
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "unknown superblock magic label");

	if ((super->version < SUPER_VERSION_MINIMUM) ||
	    (super->version == 4) || (super->version == 5) || (super->version == 6) ||
	    (super->version > SUPER_VERSION_MAXIMUM)) {
		return vdo_log_error_strerror(UDS_UNSUPPORTED_VERSION,
					      "unknown superblock version number %u",
					      super->version);
	}

	if (super->volume_offset < super->start_offset) {
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "inconsistent offsets (start %llu, volume %llu)",
					      (unsigned long long) super->start_offset,
					      (unsigned long long) super->volume_offset);
	}

	/* Sub-indexes are no longer used but the layout retains this field. */
	if (super->index_count != 1) {
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "invalid subindex count %u",
					      super->index_count);
	}

	if (generate_primary_nonce(super->nonce_info, sizeof(super->nonce_info)) != super->nonce) {
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "inconsistent superblock nonce");
	}

	return UDS_SUCCESS;
}

static int __must_check verify_region(struct layout_region *lr, u64 start_block,
				      enum region_kind kind, unsigned int instance)
{
	if (lr->start_block != start_block)
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "incorrect layout region offset");

	if (lr->kind != kind)
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "incorrect layout region kind");

	if (lr->instance != instance) {
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "incorrect layout region instance");
	}

	return UDS_SUCCESS;
}

static int __must_check verify_sub_index(struct index_layout *layout, u64 start_block,
					 struct region_table *table)
{
	int result;
	unsigned int i;
	struct sub_index_layout *sil = &layout->index;
	u64 next_block = start_block;

	sil->sub_index = table->regions[2];
	result = verify_region(&sil->sub_index, next_block, RL_KIND_INDEX, 0);
	if (result != UDS_SUCCESS)
		return result;

	define_sub_index_nonce(layout);

	sil->volume = table->regions[3];
	result = verify_region(&sil->volume, next_block, RL_KIND_VOLUME,
			       RL_SOLE_INSTANCE);
	if (result != UDS_SUCCESS)
		return result;

	next_block += sil->volume.block_count + layout->super.volume_offset;

	for (i = 0; i < layout->super.max_saves; i++) {
		sil->saves[i].index_save = table->regions[i + 4];
		result = verify_region(&sil->saves[i].index_save, next_block,
				       RL_KIND_SAVE, i);
		if (result != UDS_SUCCESS)
			return result;

		next_block += sil->saves[i].index_save.block_count;
	}

	next_block -= layout->super.volume_offset;
	if (next_block != start_block + sil->sub_index.block_count) {
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "sub index region does not span all saves");
	}

	return UDS_SUCCESS;
}

static int __must_check reconstitute_layout(struct index_layout *layout,
					    struct region_table *table, u64 first_block)
{
	int result;
	u64 next_block = first_block;

	result = vdo_allocate(layout->super.max_saves, struct index_save_layout,
			      __func__, &layout->index.saves);
	if (result != VDO_SUCCESS)
		return result;

	layout->total_blocks = table->header.region_blocks;

	layout->header = table->regions[0];
	result = verify_region(&layout->header, next_block++, RL_KIND_HEADER,
			       RL_SOLE_INSTANCE);
	if (result != UDS_SUCCESS)
		return result;

	layout->config = table->regions[1];
	result = verify_region(&layout->config, next_block++, RL_KIND_CONFIG,
			       RL_SOLE_INSTANCE);
	if (result != UDS_SUCCESS)
		return result;

	result = verify_sub_index(layout, next_block, table);
	if (result != UDS_SUCCESS)
		return result;

	next_block += layout->index.sub_index.block_count;

	layout->seal = table->regions[table->header.region_count - 1];
	result = verify_region(&layout->seal, next_block + layout->super.volume_offset,
			       RL_KIND_SEAL, RL_SOLE_INSTANCE);
	if (result != UDS_SUCCESS)
		return result;

	if (++next_block != (first_block + layout->total_blocks)) {
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "layout table does not span total blocks");
	}

	return UDS_SUCCESS;
}

static int __must_check load_super_block(struct index_layout *layout, size_t block_size,
					 u64 first_block, struct buffered_reader *reader)
{
	int result;
	struct region_table *table = NULL;
	struct super_block_data *super = &layout->super;

	result = load_region_table(reader, &table);
	if (result != UDS_SUCCESS)
		return result;

	if (table->header.type != RH_TYPE_SUPER) {
		vdo_free(table);
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "not a superblock region table");
	}

	result = read_super_block_data(reader, layout, table->header.payload);
	if (result != UDS_SUCCESS) {
		vdo_free(table);
		return vdo_log_error_strerror(result, "unknown superblock format");
	}

	if (super->block_size != block_size) {
		vdo_free(table);
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "superblock saved block_size %u differs from supplied block_size %zu",
					      super->block_size, block_size);
	}

	first_block -= (super->volume_offset - super->start_offset);
	result = reconstitute_layout(layout, table, first_block);
	vdo_free(table);
	return result;
}

static int __must_check read_index_save_data(struct buffered_reader *reader,
					     struct index_save_layout *isl,
					     size_t saved_size)
{
	int result;
	struct index_state_version file_version;
	u8 buffer[sizeof(struct index_save_data) + sizeof(struct index_state_data301)];
	size_t offset = 0;

	if (saved_size != sizeof(buffer)) {
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "unexpected index save data size %zu",
					      saved_size);
	}

	result = uds_read_from_buffered_reader(reader, buffer, sizeof(buffer));
	if (result != UDS_SUCCESS)
		return vdo_log_error_strerror(result, "cannot read index save data");

	decode_u64_le(buffer, &offset, &isl->save_data.timestamp);
	decode_u64_le(buffer, &offset, &isl->save_data.nonce);
	decode_u32_le(buffer, &offset, &isl->save_data.version);
	offset += sizeof(u32);

	if (isl->save_data.version > 1) {
		return vdo_log_error_strerror(UDS_UNSUPPORTED_VERSION,
					      "unknown index save version number %u",
					      isl->save_data.version);
	}

	decode_s32_le(buffer, &offset, &file_version.signature);
	decode_s32_le(buffer, &offset, &file_version.version_id);

	if ((file_version.signature != INDEX_STATE_VERSION_301.signature) ||
	    (file_version.version_id != INDEX_STATE_VERSION_301.version_id)) {
		return vdo_log_error_strerror(UDS_UNSUPPORTED_VERSION,
					      "index state version %d,%d is unsupported",
					      file_version.signature,
					      file_version.version_id);
	}

	decode_u64_le(buffer, &offset, &isl->state_data.newest_chapter);
	decode_u64_le(buffer, &offset, &isl->state_data.oldest_chapter);
	decode_u64_le(buffer, &offset, &isl->state_data.last_save);
	/* Skip past some historical fields that are now unused */
	offset += sizeof(u32) + sizeof(u32);
	return UDS_SUCCESS;
}

static int __must_check reconstruct_index_save(struct index_save_layout *isl,
					       struct region_table *table)
{
	int result;
	unsigned int z;
	struct layout_region *last_region;
	u64 next_block = isl->index_save.start_block;
	u64 last_block = next_block + isl->index_save.block_count;

	isl->zone_count = table->header.region_count - 3;

	last_region = &table->regions[table->header.region_count - 1];
	if (last_region->kind == RL_KIND_EMPTY) {
		isl->free_space = *last_region;
		isl->zone_count--;
	} else {
		isl->free_space = (struct layout_region) {
			.start_block = last_block,
			.block_count = 0,
			.kind = RL_KIND_EMPTY,
			.instance = RL_SOLE_INSTANCE,
		};
	}

	isl->header = table->regions[0];
	result = verify_region(&isl->header, next_block++, RL_KIND_HEADER,
			       RL_SOLE_INSTANCE);
	if (result != UDS_SUCCESS)
		return result;

	isl->index_page_map = table->regions[1];
	result = verify_region(&isl->index_page_map, next_block, RL_KIND_INDEX_PAGE_MAP,
			       RL_SOLE_INSTANCE);
	if (result != UDS_SUCCESS)
		return result;

	next_block += isl->index_page_map.block_count;

	for (z = 0; z < isl->zone_count; z++) {
		isl->volume_index_zones[z] = table->regions[z + 2];
		result = verify_region(&isl->volume_index_zones[z], next_block,
				       RL_KIND_VOLUME_INDEX, z);
		if (result != UDS_SUCCESS)
			return result;

		next_block += isl->volume_index_zones[z].block_count;
	}

	isl->open_chapter = table->regions[isl->zone_count + 2];
	result = verify_region(&isl->open_chapter, next_block, RL_KIND_OPEN_CHAPTER,
			       RL_SOLE_INSTANCE);
	if (result != UDS_SUCCESS)
		return result;

	next_block += isl->open_chapter.block_count;

	result = verify_region(&isl->free_space, next_block, RL_KIND_EMPTY,
			       RL_SOLE_INSTANCE);
	if (result != UDS_SUCCESS)
		return result;

	next_block += isl->free_space.block_count;
	if (next_block != last_block) {
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "index save layout table incomplete");
	}

	return UDS_SUCCESS;
}

static int __must_check load_index_save(struct index_save_layout *isl,
					struct buffered_reader *reader,
					unsigned int instance)
{
	int result;
	struct region_table *table = NULL;

	result = load_region_table(reader, &table);
	if (result != UDS_SUCCESS) {
		return vdo_log_error_strerror(result, "cannot read index save %u header",
					      instance);
	}

	if (table->header.region_blocks != isl->index_save.block_count) {
		u64 region_blocks = table->header.region_blocks;

		vdo_free(table);
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "unexpected index save %u region block count %llu",
					      instance,
					      (unsigned long long) region_blocks);
	}

	if (table->header.type == RH_TYPE_UNSAVED) {
		vdo_free(table);
		reset_index_save_layout(isl, 0);
		return UDS_SUCCESS;
	}


	if (table->header.type != RH_TYPE_SAVE) {
		vdo_log_error_strerror(UDS_CORRUPT_DATA,
				       "unexpected index save %u header type %u",
				       instance, table->header.type);
		vdo_free(table);
		return UDS_CORRUPT_DATA;
	}

	result = read_index_save_data(reader, isl, table->header.payload);
	if (result != UDS_SUCCESS) {
		vdo_free(table);
		return vdo_log_error_strerror(result,
					      "unknown index save %u data format",
					      instance);
	}

	result = reconstruct_index_save(isl, table);
	vdo_free(table);
	if (result != UDS_SUCCESS) {
		return vdo_log_error_strerror(result, "cannot reconstruct index save %u",
					      instance);
	}

	return UDS_SUCCESS;
}

static int __must_check load_sub_index_regions(struct index_layout *layout)
{
	int result;
	unsigned int j;
	struct index_save_layout *isl;
	struct buffered_reader *reader;

	for (j = 0; j < layout->super.max_saves; j++) {
		isl = &layout->index.saves[j];
		result = open_region_reader(layout, &isl->index_save, &reader);

		if (result != UDS_SUCCESS) {
			vdo_log_error_strerror(result,
					       "cannot get reader for index 0 save %u",
					       j);
			return result;
		}

		result = load_index_save(isl, reader, j);
		uds_free_buffered_reader(reader);
		if (result != UDS_SUCCESS) {
			/* Another save slot might be valid. */
			reset_index_save_layout(isl, 0);
			continue;
		}
	}

	return UDS_SUCCESS;
}

static int __must_check verify_uds_index_config(struct index_layout *layout,
						struct uds_configuration *config)
{
	int result;
	struct buffered_reader *reader = NULL;
	u64 offset;

	offset = layout->super.volume_offset - layout->super.start_offset;
	result = open_layout_reader(layout, &layout->config, offset, &reader);
	if (result != UDS_SUCCESS)
		return vdo_log_error_strerror(result, "failed to open config reader");

	result = uds_validate_config_contents(reader, config);
	if (result != UDS_SUCCESS) {
		uds_free_buffered_reader(reader);
		return vdo_log_error_strerror(result, "failed to read config region");
	}

	uds_free_buffered_reader(reader);
	return UDS_SUCCESS;
}

static int load_index_layout(struct index_layout *layout, struct uds_configuration *config)
{
	int result;
	struct buffered_reader *reader;

	result = uds_make_buffered_reader(layout->factory,
					  layout->offset / UDS_BLOCK_SIZE, 1, &reader);
	if (result != UDS_SUCCESS)
		return vdo_log_error_strerror(result, "unable to read superblock");

	result = load_super_block(layout, UDS_BLOCK_SIZE,
				  layout->offset / UDS_BLOCK_SIZE, reader);
	uds_free_buffered_reader(reader);
	if (result != UDS_SUCCESS)
		return result;

	result = verify_uds_index_config(layout, config);
	if (result != UDS_SUCCESS)
		return result;

	return load_sub_index_regions(layout);
}

static int create_layout_factory(struct index_layout *layout,
				 const struct uds_configuration *config)
{
	int result;
	size_t writable_size;
	struct io_factory *factory = NULL;

	result = uds_make_io_factory(config->bdev, &factory);
	if (result != UDS_SUCCESS)
		return result;

	writable_size = uds_get_writable_size(factory) & -UDS_BLOCK_SIZE;
	if (writable_size < config->size + config->offset) {
		uds_put_io_factory(factory);
		vdo_log_error("index storage (%zu) is smaller than the requested size %zu",
			      writable_size, config->size + config->offset);
		return -ENOSPC;
	}

	layout->factory = factory;
	layout->factory_size = (config->size > 0) ? config->size : writable_size;
	layout->offset = config->offset;
	return UDS_SUCCESS;
}

int uds_make_index_layout(struct uds_configuration *config, bool new_layout,
			  struct index_layout **layout_ptr)
{
	int result;
	struct index_layout *layout = NULL;
	struct save_layout_sizes sizes;

	result = compute_sizes(config, &sizes);
	if (result != UDS_SUCCESS)
		return result;

	result = vdo_allocate(1, struct index_layout, __func__, &layout);
	if (result != VDO_SUCCESS)
		return result;

	result = create_layout_factory(layout, config);
	if (result != UDS_SUCCESS) {
		uds_free_index_layout(layout);
		return result;
	}

	if (layout->factory_size < sizes.total_size) {
		vdo_log_error("index storage (%zu) is smaller than the required size %llu",
			      layout->factory_size,
			      (unsigned long long) sizes.total_size);
		uds_free_index_layout(layout);
		return -ENOSPC;
	}

	if (new_layout)
		result = create_index_layout(layout, config);
	else
		result = load_index_layout(layout, config);
	if (result != UDS_SUCCESS) {
		uds_free_index_layout(layout);
		return result;
	}

	*layout_ptr = layout;
	return UDS_SUCCESS;
}

void uds_free_index_layout(struct index_layout *layout)
{
	if (layout == NULL)
		return;

	vdo_free(layout->index.saves);
	if (layout->factory != NULL)
		uds_put_io_factory(layout->factory);

	vdo_free(layout);
}

int uds_replace_index_layout_storage(struct index_layout *layout,
				     struct block_device *bdev)
{
	return uds_replace_storage(layout->factory, bdev);
}

/* Obtain a dm_bufio_client for the volume region. */
int uds_open_volume_bufio(struct index_layout *layout, size_t block_size,
			  unsigned int reserved_buffers,
			  struct dm_bufio_client **client_ptr)
{
	off_t offset = (layout->index.volume.start_block +
			layout->super.volume_offset -
			layout->super.start_offset);

	return uds_make_bufio(layout->factory, offset, block_size, reserved_buffers,
			      client_ptr);
}

u64 uds_get_volume_nonce(struct index_layout *layout)
{
	return layout->index.nonce;
}
