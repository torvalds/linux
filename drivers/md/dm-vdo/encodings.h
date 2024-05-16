/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_ENCODINGS_H
#define VDO_ENCODINGS_H

#include <linux/blk_types.h>
#include <linux/crc32.h>
#include <linux/limits.h>
#include <linux/uuid.h>

#include "numeric.h"

#include "constants.h"
#include "types.h"

/*
 * An in-memory representation of a version number for versioned structures on disk.
 *
 * A version number consists of two portions, a major version and a minor version. Any format
 * change which does not require an explicit upgrade step from the previous version should
 * increment the minor version. Any format change which either requires an explicit upgrade step,
 * or is wholly incompatible (i.e. can not be upgraded to), should increment the major version, and
 * set the minor version to 0.
 */
struct version_number {
	u32 major_version;
	u32 minor_version;
};

/*
 * A packed, machine-independent, on-disk representation of a version_number. Both fields are
 * stored in little-endian byte order.
 */
struct packed_version_number {
	__le32 major_version;
	__le32 minor_version;
} __packed;

/* The registry of component ids for use in headers */
#define VDO_SUPER_BLOCK 0
#define VDO_LAYOUT 1
#define VDO_RECOVERY_JOURNAL 2
#define VDO_SLAB_DEPOT 3
#define VDO_BLOCK_MAP 4
#define VDO_GEOMETRY_BLOCK 5

/* The header for versioned data stored on disk. */
struct header {
	u32 id; /* The component this is a header for */
	struct version_number version; /* The version of the data format */
	size_t size; /* The size of the data following this header */
};

/* A packed, machine-independent, on-disk representation of a component header. */
struct packed_header {
	__le32 id;
	struct packed_version_number version;
	__le64 size;
} __packed;

enum {
	VDO_GEOMETRY_BLOCK_LOCATION = 0,
	VDO_GEOMETRY_MAGIC_NUMBER_SIZE = 8,
	VDO_DEFAULT_GEOMETRY_BLOCK_VERSION = 5,
};

struct index_config {
	u32 mem;
	u32 unused;
	bool sparse;
} __packed;

enum volume_region_id {
	VDO_INDEX_REGION = 0,
	VDO_DATA_REGION = 1,
	VDO_VOLUME_REGION_COUNT,
};

struct volume_region {
	/* The ID of the region */
	enum volume_region_id id;
	/*
	 * The absolute starting offset on the device. The region continues until the next region
	 * begins.
	 */
	physical_block_number_t start_block;
} __packed;

struct volume_geometry {
	/* For backwards compatibility */
	u32 unused;
	/* The nonce of this volume */
	nonce_t nonce;
	/* The uuid of this volume */
	uuid_t uuid;
	/* The block offset to be applied to bios */
	block_count_t bio_offset;
	/* The regions in ID order */
	struct volume_region regions[VDO_VOLUME_REGION_COUNT];
	/* The index config */
	struct index_config index_config;
} __packed;

/* This volume geometry struct is used for sizing only */
struct volume_geometry_4_0 {
	/* For backwards compatibility */
	u32 unused;
	/* The nonce of this volume */
	nonce_t nonce;
	/* The uuid of this volume */
	uuid_t uuid;
	/* The regions in ID order */
	struct volume_region regions[VDO_VOLUME_REGION_COUNT];
	/* The index config */
	struct index_config index_config;
} __packed;

extern const u8 VDO_GEOMETRY_MAGIC_NUMBER[VDO_GEOMETRY_MAGIC_NUMBER_SIZE + 1];

/**
 * DOC: Block map entries
 *
 * The entry for each logical block in the block map is encoded into five bytes, which saves space
 * in both the on-disk and in-memory layouts. It consists of the 36 low-order bits of a
 * physical_block_number_t (addressing 256 terabytes with a 4KB block size) and a 4-bit encoding of
 * a block_mapping_state.
 *
 * Of the 8 high bits of the 5-byte structure:
 *
 * Bits 7..4: The four highest bits of the 36-bit physical block number
 * Bits 3..0: The 4-bit block_mapping_state
 *
 * The following 4 bytes are the low order bytes of the physical block number, in little-endian
 * order.
 *
 * Conversion functions to and from a data location are provided.
 */
struct block_map_entry {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	unsigned mapping_state : 4;
	unsigned pbn_high_nibble : 4;
#else
	unsigned pbn_high_nibble : 4;
	unsigned mapping_state : 4;
#endif

	__le32 pbn_low_word;
} __packed;

struct block_map_page_header {
	__le64 nonce;
	__le64 pbn;

	/* May be non-zero on disk */
	u8 unused_long_word[8];

	/* Whether this page has been written twice to disk */
	bool initialized;

	/* Always zero on disk */
	u8 unused_byte1;

	/* May be non-zero on disk */
	u8 unused_byte2;
	u8 unused_byte3;
} __packed;

struct block_map_page {
	struct packed_version_number version;
	struct block_map_page_header header;
	struct block_map_entry entries[];
} __packed;

enum block_map_page_validity {
	VDO_BLOCK_MAP_PAGE_VALID,
	VDO_BLOCK_MAP_PAGE_INVALID,
	/* Valid page found in the wrong location on disk */
	VDO_BLOCK_MAP_PAGE_BAD,
};

struct block_map_state_2_0 {
	physical_block_number_t flat_page_origin;
	block_count_t flat_page_count;
	physical_block_number_t root_origin;
	block_count_t root_count;
} __packed;

struct boundary {
	page_number_t levels[VDO_BLOCK_MAP_TREE_HEIGHT];
};

extern const struct header VDO_BLOCK_MAP_HEADER_2_0;

/* The state of the recovery journal as encoded in the VDO super block. */
struct recovery_journal_state_7_0 {
	/* Sequence number to start the journal */
	sequence_number_t journal_start;
	/* Number of logical blocks used by VDO */
	block_count_t logical_blocks_used;
	/* Number of block map pages allocated */
	block_count_t block_map_data_blocks;
} __packed;

extern const struct header VDO_RECOVERY_JOURNAL_HEADER_7_0;

typedef u16 journal_entry_count_t;

/*
 * A recovery journal entry stores three physical locations: a data location that is the value of a
 * single mapping in the block map tree, and the two locations of the block map pages and slots
 * that are acquiring and releasing a reference to the location. The journal entry also stores an
 * operation code that says whether the mapping is for a logical block or for the block map tree
 * itself.
 */
struct recovery_journal_entry {
	struct block_map_slot slot;
	struct data_location mapping;
	struct data_location unmapping;
	enum journal_operation operation;
};

/* The packed, on-disk representation of a recovery journal entry. */
struct packed_recovery_journal_entry {
	/*
	 * In little-endian bit order:
	 * Bits 15..12: The four highest bits of the 36-bit physical block number of the block map
	 * tree page
	 * Bits 11..2: The 10-bit block map page slot number
	 * Bit 1..0: The journal_operation of the entry (this actually only requires 1 bit, but
	 *           it is convenient to keep the extra bit as part of this field.
	 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	unsigned operation : 2;
	unsigned slot_low : 6;
	unsigned slot_high : 4;
	unsigned pbn_high_nibble : 4;
#else
	unsigned slot_low : 6;
	unsigned operation : 2;
	unsigned pbn_high_nibble : 4;
	unsigned slot_high : 4;
#endif

	/*
	 * Bits 47..16: The 32 low-order bits of the block map page PBN, in little-endian byte
	 * order
	 */
	__le32 pbn_low_word;

	/*
	 * Bits 87..48: The five-byte block map entry encoding the location that will be stored in
	 * the block map page slot
	 */
	struct block_map_entry mapping;

	/*
	 * Bits 127..88: The five-byte block map entry encoding the location that was stored in the
	 * block map page slot
	 */
	struct block_map_entry unmapping;
} __packed;

/* The packed, on-disk representation of an old format recovery journal entry. */
struct packed_recovery_journal_entry_1 {
	/*
	 * In little-endian bit order:
	 * Bits 15..12: The four highest bits of the 36-bit physical block number of the block map
	 *              tree page
	 * Bits 11..2: The 10-bit block map page slot number
	 * Bits 1..0: The 2-bit journal_operation of the entry
	 *
	 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	unsigned operation : 2;
	unsigned slot_low : 6;
	unsigned slot_high : 4;
	unsigned pbn_high_nibble : 4;
#else
	unsigned slot_low : 6;
	unsigned operation : 2;
	unsigned pbn_high_nibble : 4;
	unsigned slot_high : 4;
#endif

	/*
	 * Bits 47..16: The 32 low-order bits of the block map page PBN, in little-endian byte
	 * order
	 */
	__le32 pbn_low_word;

	/*
	 * Bits 87..48: The five-byte block map entry encoding the location that was or will be
	 * stored in the block map page slot
	 */
	struct block_map_entry block_map_entry;
} __packed;

enum journal_operation_1 {
	VDO_JOURNAL_DATA_DECREMENT = 0,
	VDO_JOURNAL_DATA_INCREMENT = 1,
	VDO_JOURNAL_BLOCK_MAP_DECREMENT = 2,
	VDO_JOURNAL_BLOCK_MAP_INCREMENT = 3,
} __packed;

struct recovery_block_header {
	sequence_number_t block_map_head; /* Block map head sequence number */
	sequence_number_t slab_journal_head; /* Slab journal head seq. number */
	sequence_number_t sequence_number; /* Sequence number for this block */
	nonce_t nonce; /* A given VDO instance's nonce */
	block_count_t logical_blocks_used; /* Logical blocks in use */
	block_count_t block_map_data_blocks; /* Allocated block map pages */
	journal_entry_count_t entry_count; /* Number of entries written */
	u8 check_byte; /* The protection check byte */
	u8 recovery_count; /* Number of recoveries completed */
	enum vdo_metadata_type metadata_type; /* Metadata type */
};

/*
 * The packed, on-disk representation of a recovery journal block header. All fields are kept in
 * little-endian byte order.
 */
struct packed_journal_header {
	/* Block map head 64-bit sequence number */
	__le64 block_map_head;

	/* Slab journal head 64-bit sequence number */
	__le64 slab_journal_head;

	/* The 64-bit sequence number for this block */
	__le64 sequence_number;

	/* A given VDO instance's 64-bit nonce */
	__le64 nonce;

	/* 8-bit metadata type (should always be one for the recovery journal) */
	u8 metadata_type;

	/* 16-bit count of the entries encoded in the block */
	__le16 entry_count;

	/* 64-bit count of the logical blocks used when this block was opened */
	__le64 logical_blocks_used;

	/* 64-bit count of the block map blocks used when this block was opened */
	__le64 block_map_data_blocks;

	/* The protection check byte */
	u8 check_byte;

	/* The number of recoveries completed */
	u8 recovery_count;
} __packed;

struct packed_journal_sector {
	/* The protection check byte */
	u8 check_byte;

	/* The number of recoveries completed */
	u8 recovery_count;

	/* The number of entries in this sector */
	u8 entry_count;

	/* Journal entries for this sector */
	struct packed_recovery_journal_entry entries[];
} __packed;

enum {
	/* The number of entries in each sector (except the last) when filled */
	RECOVERY_JOURNAL_ENTRIES_PER_SECTOR =
		((VDO_SECTOR_SIZE - sizeof(struct packed_journal_sector)) /
		 sizeof(struct packed_recovery_journal_entry)),
	RECOVERY_JOURNAL_ENTRIES_PER_BLOCK = RECOVERY_JOURNAL_ENTRIES_PER_SECTOR * 7,
	/* The number of entries in a v1 recovery journal block. */
	RECOVERY_JOURNAL_1_ENTRIES_PER_BLOCK = 311,
	/* The number of entries in each v1 sector (except the last) when filled */
	RECOVERY_JOURNAL_1_ENTRIES_PER_SECTOR =
		((VDO_SECTOR_SIZE - sizeof(struct packed_journal_sector)) /
		 sizeof(struct packed_recovery_journal_entry_1)),
	/* The number of entries in the last sector when a block is full */
	RECOVERY_JOURNAL_1_ENTRIES_IN_LAST_SECTOR =
		(RECOVERY_JOURNAL_1_ENTRIES_PER_BLOCK % RECOVERY_JOURNAL_1_ENTRIES_PER_SECTOR),
};

/* A type representing a reference count of a block. */
typedef u8 vdo_refcount_t;

/* The absolute position of an entry in a recovery journal or slab journal. */
struct journal_point {
	sequence_number_t sequence_number;
	journal_entry_count_t entry_count;
};

/* A packed, platform-independent encoding of a struct journal_point. */
struct packed_journal_point {
	/*
	 * The packed representation is the little-endian 64-bit representation of the low-order 48
	 * bits of the sequence number, shifted up 16 bits, or'ed with the 16-bit entry count.
	 *
	 * Very long-term, the top 16 bits of the sequence number may not always be zero, as this
	 * encoding assumes--see BZ 1523240.
	 */
	__le64 encoded_point;
} __packed;

/* Special vdo_refcount_t values. */
#define EMPTY_REFERENCE_COUNT 0
enum {
	MAXIMUM_REFERENCE_COUNT = 254,
	PROVISIONAL_REFERENCE_COUNT = 255,
};

enum {
	COUNTS_PER_SECTOR =
		((VDO_SECTOR_SIZE - sizeof(struct packed_journal_point)) / sizeof(vdo_refcount_t)),
	COUNTS_PER_BLOCK = COUNTS_PER_SECTOR * VDO_SECTORS_PER_BLOCK,
};

/* The format of each sector of a reference_block on disk. */
struct packed_reference_sector {
	struct packed_journal_point commit_point;
	vdo_refcount_t counts[COUNTS_PER_SECTOR];
} __packed;

struct packed_reference_block {
	struct packed_reference_sector sectors[VDO_SECTORS_PER_BLOCK];
};

struct slab_depot_state_2_0 {
	struct slab_config slab_config;
	physical_block_number_t first_block;
	physical_block_number_t last_block;
	zone_count_t zone_count;
} __packed;

extern const struct header VDO_SLAB_DEPOT_HEADER_2_0;

/*
 * vdo_slab journal blocks may have one of two formats, depending upon whether or not any of the
 * entries in the block are block map increments. Since the steady state for a VDO is that all of
 * the necessary block map pages will be allocated, most slab journal blocks will have only data
 * entries. Such blocks can hold more entries, hence the two formats.
 */

/* A single slab journal entry */
struct slab_journal_entry {
	slab_block_number sbn;
	enum journal_operation operation;
	bool increment;
};

/* A single slab journal entry in its on-disk form */
typedef struct {
	u8 offset_low8;
	u8 offset_mid8;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	unsigned offset_high7 : 7;
	unsigned increment : 1;
#else
	unsigned increment : 1;
	unsigned offset_high7 : 7;
#endif
} __packed packed_slab_journal_entry;

/* The unpacked representation of the header of a slab journal block */
struct slab_journal_block_header {
	/* Sequence number for head of journal */
	sequence_number_t head;
	/* Sequence number for this block */
	sequence_number_t sequence_number;
	/* The nonce for a given VDO instance */
	nonce_t nonce;
	/* Recovery journal point for last entry */
	struct journal_point recovery_point;
	/* Metadata type */
	enum vdo_metadata_type metadata_type;
	/* Whether this block contains block map increments */
	bool has_block_map_increments;
	/* The number of entries in the block */
	journal_entry_count_t entry_count;
};

/*
 * The packed, on-disk representation of a slab journal block header. All fields are kept in
 * little-endian byte order.
 */
struct packed_slab_journal_block_header {
	/* 64-bit sequence number for head of journal */
	__le64 head;
	/* 64-bit sequence number for this block */
	__le64 sequence_number;
	/* Recovery journal point for the last entry, packed into 64 bits */
	struct packed_journal_point recovery_point;
	/* The 64-bit nonce for a given VDO instance */
	__le64 nonce;
	/* 8-bit metadata type (should always be two, for the slab journal) */
	u8 metadata_type;
	/* Whether this block contains block map increments */
	bool has_block_map_increments;
	/* 16-bit count of the entries encoded in the block */
	__le16 entry_count;
} __packed;

enum {
	VDO_SLAB_JOURNAL_PAYLOAD_SIZE =
		VDO_BLOCK_SIZE - sizeof(struct packed_slab_journal_block_header),
	VDO_SLAB_JOURNAL_FULL_ENTRIES_PER_BLOCK = (VDO_SLAB_JOURNAL_PAYLOAD_SIZE * 8) / 25,
	VDO_SLAB_JOURNAL_ENTRY_TYPES_SIZE =
		((VDO_SLAB_JOURNAL_FULL_ENTRIES_PER_BLOCK - 1) / 8) + 1,
	VDO_SLAB_JOURNAL_ENTRIES_PER_BLOCK =
		(VDO_SLAB_JOURNAL_PAYLOAD_SIZE / sizeof(packed_slab_journal_entry)),
};

/* The payload of a slab journal block which has block map increments */
struct full_slab_journal_entries {
	/* The entries themselves */
	packed_slab_journal_entry entries[VDO_SLAB_JOURNAL_FULL_ENTRIES_PER_BLOCK];
	/* The bit map indicating which entries are block map increments */
	u8 entry_types[VDO_SLAB_JOURNAL_ENTRY_TYPES_SIZE];
} __packed;

typedef union {
	/* Entries which include block map increments */
	struct full_slab_journal_entries full_entries;
	/* Entries which are only data updates */
	packed_slab_journal_entry entries[VDO_SLAB_JOURNAL_ENTRIES_PER_BLOCK];
	/* Ensure the payload fills to the end of the block */
	u8 space[VDO_SLAB_JOURNAL_PAYLOAD_SIZE];
} __packed slab_journal_payload;

struct packed_slab_journal_block {
	struct packed_slab_journal_block_header header;
	slab_journal_payload payload;
} __packed;

/* The offset of a slab journal tail block. */
typedef u8 tail_block_offset_t;

struct slab_summary_entry {
	/* Bits 7..0: The offset of the tail block within the slab journal */
	tail_block_offset_t tail_block_offset;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	/* Bits 13..8: A hint about the fullness of the slab */
	unsigned int fullness_hint : 6;
	/* Bit 14: Whether the ref_counts must be loaded from the layer */
	unsigned int load_ref_counts : 1;
	/* Bit 15: The believed cleanliness of this slab */
	unsigned int is_dirty : 1;
#else
	/* Bit 15: The believed cleanliness of this slab */
	unsigned int is_dirty : 1;
	/* Bit 14: Whether the ref_counts must be loaded from the layer */
	unsigned int load_ref_counts : 1;
	/* Bits 13..8: A hint about the fullness of the slab */
	unsigned int fullness_hint : 6;
#endif
} __packed;

enum {
	VDO_SLAB_SUMMARY_FULLNESS_HINT_BITS = 6,
	VDO_SLAB_SUMMARY_ENTRIES_PER_BLOCK = VDO_BLOCK_SIZE / sizeof(struct slab_summary_entry),
	VDO_SLAB_SUMMARY_BLOCKS_PER_ZONE = MAX_VDO_SLABS / VDO_SLAB_SUMMARY_ENTRIES_PER_BLOCK,
	VDO_SLAB_SUMMARY_BLOCKS = VDO_SLAB_SUMMARY_BLOCKS_PER_ZONE * MAX_VDO_PHYSICAL_ZONES,
};

struct layout {
	physical_block_number_t start;
	block_count_t size;
	physical_block_number_t first_free;
	physical_block_number_t last_free;
	size_t num_partitions;
	struct partition *head;
};

struct partition {
	enum partition_id id; /* The id of this partition */
	physical_block_number_t offset; /* The offset into the layout of this partition */
	block_count_t count; /* The number of blocks in the partition */
	struct partition *next; /* A pointer to the next partition in the layout */
};

struct layout_3_0 {
	physical_block_number_t first_free;
	physical_block_number_t last_free;
	u8 partition_count;
} __packed;

struct partition_3_0 {
	enum partition_id id;
	physical_block_number_t offset;
	physical_block_number_t base; /* unused but retained for backwards compatibility */
	block_count_t count;
} __packed;

/*
 * The configuration of the VDO service.
 */
struct vdo_config {
	block_count_t logical_blocks; /* number of logical blocks */
	block_count_t physical_blocks; /* number of physical blocks */
	block_count_t slab_size; /* number of blocks in a slab */
	block_count_t recovery_journal_size; /* number of recovery journal blocks */
	block_count_t slab_journal_blocks; /* number of slab journal blocks */
};

/* This is the structure that captures the vdo fields saved as a super block component. */
struct vdo_component {
	enum vdo_state state;
	u64 complete_recoveries;
	u64 read_only_recoveries;
	struct vdo_config config;
	nonce_t nonce;
};

/*
 * A packed, machine-independent, on-disk representation of the vdo_config in the VDO component
 * data in the super block.
 */
struct packed_vdo_config {
	__le64 logical_blocks;
	__le64 physical_blocks;
	__le64 slab_size;
	__le64 recovery_journal_size;
	__le64 slab_journal_blocks;
} __packed;

/*
 * A packed, machine-independent, on-disk representation of version 41.0 of the VDO component data
 * in the super block.
 */
struct packed_vdo_component_41_0 {
	__le32 state;
	__le64 complete_recoveries;
	__le64 read_only_recoveries;
	struct packed_vdo_config config;
	__le64 nonce;
} __packed;

/*
 * The version of the on-disk format of a VDO volume. This should be incremented any time the
 * on-disk representation of any VDO structure changes. Changes which require only online upgrade
 * steps should increment the minor version. Changes which require an offline upgrade or which can
 * not be upgraded to at all should increment the major version and set the minor version to 0.
 */
extern const struct version_number VDO_VOLUME_VERSION_67_0;

enum {
	VDO_ENCODED_HEADER_SIZE = sizeof(struct packed_header),
	BLOCK_MAP_COMPONENT_ENCODED_SIZE =
		VDO_ENCODED_HEADER_SIZE + sizeof(struct block_map_state_2_0),
	RECOVERY_JOURNAL_COMPONENT_ENCODED_SIZE =
		VDO_ENCODED_HEADER_SIZE + sizeof(struct recovery_journal_state_7_0),
	SLAB_DEPOT_COMPONENT_ENCODED_SIZE =
		VDO_ENCODED_HEADER_SIZE + sizeof(struct slab_depot_state_2_0),
	VDO_PARTITION_COUNT = 4,
	VDO_LAYOUT_ENCODED_SIZE = (VDO_ENCODED_HEADER_SIZE +
				   sizeof(struct layout_3_0) +
				   (sizeof(struct partition_3_0) * VDO_PARTITION_COUNT)),
	VDO_SUPER_BLOCK_FIXED_SIZE = VDO_ENCODED_HEADER_SIZE + sizeof(u32),
	VDO_MAX_COMPONENT_DATA_SIZE = VDO_SECTOR_SIZE - VDO_SUPER_BLOCK_FIXED_SIZE,
	VDO_COMPONENT_ENCODED_SIZE =
		(sizeof(struct packed_version_number) + sizeof(struct packed_vdo_component_41_0)),
	VDO_COMPONENT_DATA_OFFSET = VDO_ENCODED_HEADER_SIZE,
	VDO_COMPONENT_DATA_SIZE = (sizeof(u32) +
				   sizeof(struct packed_version_number) +
				   VDO_COMPONENT_ENCODED_SIZE +
				   VDO_LAYOUT_ENCODED_SIZE +
				   RECOVERY_JOURNAL_COMPONENT_ENCODED_SIZE +
				   SLAB_DEPOT_COMPONENT_ENCODED_SIZE +
				   BLOCK_MAP_COMPONENT_ENCODED_SIZE),
};

/* The entirety of the component data encoded in the VDO super block. */
struct vdo_component_states {
	/* For backwards compatibility */
	u32 unused;

	/* The VDO volume version */
	struct version_number volume_version;

	/* Components */
	struct vdo_component vdo;
	struct block_map_state_2_0 block_map;
	struct recovery_journal_state_7_0 recovery_journal;
	struct slab_depot_state_2_0 slab_depot;

	/* Our partitioning of the underlying storage */
	struct layout layout;
};

/**
 * vdo_are_same_version() - Check whether two version numbers are the same.
 * @version_a: The first version.
 * @version_b: The second version.
 *
 * Return: true if the two versions are the same.
 */
static inline bool vdo_are_same_version(struct version_number version_a,
					struct version_number version_b)
{
	return ((version_a.major_version == version_b.major_version) &&
		(version_a.minor_version == version_b.minor_version));
}

/**
 * vdo_is_upgradable_version() - Check whether an actual version is upgradable to an expected
 *                               version.
 * @expected_version: The expected version.
 * @actual_version: The version being validated.
 *
 * An actual version is upgradable if its major number is expected but its minor number differs,
 * and the expected version's minor number is greater than the actual version's minor number.
 *
 * Return: true if the actual version is upgradable.
 */
static inline bool vdo_is_upgradable_version(struct version_number expected_version,
					     struct version_number actual_version)
{
	return ((expected_version.major_version == actual_version.major_version) &&
		(expected_version.minor_version > actual_version.minor_version));
}

int __must_check vdo_validate_header(const struct header *expected_header,
				     const struct header *actual_header, bool exact_size,
				     const char *component_name);

void vdo_encode_header(u8 *buffer, size_t *offset, const struct header *header);
void vdo_decode_header(u8 *buffer, size_t *offset, struct header *header);

/**
 * vdo_pack_version_number() - Convert a version_number to its packed on-disk representation.
 * @version: The version number to convert.
 *
 * Return: the platform-independent representation of the version
 */
static inline struct packed_version_number vdo_pack_version_number(struct version_number version)
{
	return (struct packed_version_number) {
		.major_version = __cpu_to_le32(version.major_version),
		.minor_version = __cpu_to_le32(version.minor_version),
	};
}

/**
 * vdo_unpack_version_number() - Convert a packed_version_number to its native in-memory
 *                               representation.
 * @version: The version number to convert.
 *
 * Return: The platform-independent representation of the version.
 */
static inline struct version_number vdo_unpack_version_number(struct packed_version_number version)
{
	return (struct version_number) {
		.major_version = __le32_to_cpu(version.major_version),
		.minor_version = __le32_to_cpu(version.minor_version),
	};
}

/**
 * vdo_pack_header() - Convert a component header to its packed on-disk representation.
 * @header: The header to convert.
 *
 * Return: the platform-independent representation of the header
 */
static inline struct packed_header vdo_pack_header(const struct header *header)
{
	return (struct packed_header) {
		.id = __cpu_to_le32(header->id),
		.version = vdo_pack_version_number(header->version),
		.size = __cpu_to_le64(header->size),
	};
}

/**
 * vdo_unpack_header() - Convert a packed_header to its native in-memory representation.
 * @header: The header to convert.
 *
 * Return: The platform-independent representation of the version.
 */
static inline struct header vdo_unpack_header(const struct packed_header *header)
{
	return (struct header) {
		.id = __le32_to_cpu(header->id),
		.version = vdo_unpack_version_number(header->version),
		.size = __le64_to_cpu(header->size),
	};
}

/**
 * vdo_get_index_region_start() - Get the start of the index region from a geometry.
 * @geometry: The geometry.
 *
 * Return: The start of the index region.
 */
static inline physical_block_number_t __must_check
vdo_get_index_region_start(struct volume_geometry geometry)
{
	return geometry.regions[VDO_INDEX_REGION].start_block;
}

/**
 * vdo_get_data_region_start() - Get the start of the data region from a geometry.
 * @geometry: The geometry.
 *
 * Return: The start of the data region.
 */
static inline physical_block_number_t __must_check
vdo_get_data_region_start(struct volume_geometry geometry)
{
	return geometry.regions[VDO_DATA_REGION].start_block;
}

/**
 * vdo_get_index_region_size() - Get the size of the index region from a geometry.
 * @geometry: The geometry.
 *
 * Return: The size of the index region.
 */
static inline physical_block_number_t __must_check
vdo_get_index_region_size(struct volume_geometry geometry)
{
	return vdo_get_data_region_start(geometry) -
		vdo_get_index_region_start(geometry);
}

int __must_check vdo_parse_geometry_block(unsigned char *block,
					  struct volume_geometry *geometry);

static inline bool vdo_is_state_compressed(const enum block_mapping_state mapping_state)
{
	return (mapping_state > VDO_MAPPING_STATE_UNCOMPRESSED);
}

static inline struct block_map_entry
vdo_pack_block_map_entry(physical_block_number_t pbn, enum block_mapping_state mapping_state)
{
	return (struct block_map_entry) {
		.mapping_state = (mapping_state & 0x0F),
		.pbn_high_nibble = ((pbn >> 32) & 0x0F),
		.pbn_low_word = __cpu_to_le32(pbn & UINT_MAX),
	};
}

static inline struct data_location vdo_unpack_block_map_entry(const struct block_map_entry *entry)
{
	physical_block_number_t low32 = __le32_to_cpu(entry->pbn_low_word);
	physical_block_number_t high4 = entry->pbn_high_nibble;

	return (struct data_location) {
		.pbn = ((high4 << 32) | low32),
		.state = entry->mapping_state,
	};
}

static inline bool vdo_is_mapped_location(const struct data_location *location)
{
	return (location->state != VDO_MAPPING_STATE_UNMAPPED);
}

static inline bool vdo_is_valid_location(const struct data_location *location)
{
	if (location->pbn == VDO_ZERO_BLOCK)
		return !vdo_is_state_compressed(location->state);
	else
		return vdo_is_mapped_location(location);
}

static inline physical_block_number_t __must_check
vdo_get_block_map_page_pbn(const struct block_map_page *page)
{
	return __le64_to_cpu(page->header.pbn);
}

struct block_map_page *vdo_format_block_map_page(void *buffer, nonce_t nonce,
						 physical_block_number_t pbn,
						 bool initialized);

enum block_map_page_validity __must_check vdo_validate_block_map_page(struct block_map_page *page,
								      nonce_t nonce,
								      physical_block_number_t pbn);

static inline page_count_t vdo_compute_block_map_page_count(block_count_t entries)
{
	return DIV_ROUND_UP(entries, VDO_BLOCK_MAP_ENTRIES_PER_PAGE);
}

block_count_t __must_check vdo_compute_new_forest_pages(root_count_t root_count,
							struct boundary *old_sizes,
							block_count_t entries,
							struct boundary *new_sizes);

/**
 * vdo_pack_recovery_journal_entry() - Return the packed, on-disk representation of a recovery
 *                                     journal entry.
 * @entry: The journal entry to pack.
 *
 * Return: The packed representation of the journal entry.
 */
static inline struct packed_recovery_journal_entry
vdo_pack_recovery_journal_entry(const struct recovery_journal_entry *entry)
{
	return (struct packed_recovery_journal_entry) {
		.operation = entry->operation,
		.slot_low = entry->slot.slot & 0x3F,
		.slot_high = (entry->slot.slot >> 6) & 0x0F,
		.pbn_high_nibble = (entry->slot.pbn >> 32) & 0x0F,
		.pbn_low_word = __cpu_to_le32(entry->slot.pbn & UINT_MAX),
		.mapping = vdo_pack_block_map_entry(entry->mapping.pbn,
						    entry->mapping.state),
		.unmapping = vdo_pack_block_map_entry(entry->unmapping.pbn,
						      entry->unmapping.state),
	};
}

/**
 * vdo_unpack_recovery_journal_entry() - Unpack the on-disk representation of a recovery journal
 *                                       entry.
 * @entry: The recovery journal entry to unpack.
 *
 * Return: The unpacked entry.
 */
static inline struct recovery_journal_entry
vdo_unpack_recovery_journal_entry(const struct packed_recovery_journal_entry *entry)
{
	physical_block_number_t low32 = __le32_to_cpu(entry->pbn_low_word);
	physical_block_number_t high4 = entry->pbn_high_nibble;

	return (struct recovery_journal_entry) {
		.operation = entry->operation,
		.slot = {
			.pbn = ((high4 << 32) | low32),
			.slot = (entry->slot_low | (entry->slot_high << 6)),
		},
		.mapping = vdo_unpack_block_map_entry(&entry->mapping),
		.unmapping = vdo_unpack_block_map_entry(&entry->unmapping),
	};
}

const char * __must_check vdo_get_journal_operation_name(enum journal_operation operation);

/**
 * vdo_is_valid_recovery_journal_sector() - Determine whether the header of the given sector could
 *                                          describe a valid sector for the given journal block
 *                                          header.
 * @header: The unpacked block header to compare against.
 * @sector: The packed sector to check.
 * @sector_number: The number of the sector being checked.
 *
 * Return: true if the sector matches the block header.
 */
static inline bool __must_check
vdo_is_valid_recovery_journal_sector(const struct recovery_block_header *header,
				     const struct packed_journal_sector *sector,
				     u8 sector_number)
{
	if ((header->check_byte != sector->check_byte) ||
	    (header->recovery_count != sector->recovery_count))
		return false;

	if (header->metadata_type == VDO_METADATA_RECOVERY_JOURNAL_2)
		return sector->entry_count <= RECOVERY_JOURNAL_ENTRIES_PER_SECTOR;

	if (sector_number == 7)
		return sector->entry_count <= RECOVERY_JOURNAL_1_ENTRIES_IN_LAST_SECTOR;

	return sector->entry_count <= RECOVERY_JOURNAL_1_ENTRIES_PER_SECTOR;
}

/**
 * vdo_compute_recovery_journal_block_number() - Compute the physical block number of the recovery
 *                                               journal block which would have a given sequence
 *                                               number.
 * @journal_size: The size of the journal.
 * @sequence_number: The sequence number.
 *
 * Return: The pbn of the journal block which would the specified sequence number.
 */
static inline physical_block_number_t __must_check
vdo_compute_recovery_journal_block_number(block_count_t journal_size,
					  sequence_number_t sequence_number)
{
	/*
	 * Since journal size is a power of two, the block number modulus can just be extracted
	 * from the low-order bits of the sequence.
	 */
	return (sequence_number & (journal_size - 1));
}

/**
 * vdo_get_journal_block_sector() - Find the recovery journal sector from the block header and
 *                                  sector number.
 * @header: The header of the recovery journal block.
 * @sector_number: The index of the sector (1-based).
 *
 * Return: A packed recovery journal sector.
 */
static inline struct packed_journal_sector * __must_check
vdo_get_journal_block_sector(struct packed_journal_header *header, int sector_number)
{
	char *sector_data = ((char *) header) + (VDO_SECTOR_SIZE * sector_number);

	return (struct packed_journal_sector *) sector_data;
}

/**
 * vdo_pack_recovery_block_header() - Generate the packed representation of a recovery block
 *                                    header.
 * @header: The header containing the values to encode.
 * @packed: The header into which to pack the values.
 */
static inline void vdo_pack_recovery_block_header(const struct recovery_block_header *header,
						  struct packed_journal_header *packed)
{
	*packed = (struct packed_journal_header) {
		.block_map_head = __cpu_to_le64(header->block_map_head),
		.slab_journal_head = __cpu_to_le64(header->slab_journal_head),
		.sequence_number = __cpu_to_le64(header->sequence_number),
		.nonce = __cpu_to_le64(header->nonce),
		.logical_blocks_used = __cpu_to_le64(header->logical_blocks_used),
		.block_map_data_blocks = __cpu_to_le64(header->block_map_data_blocks),
		.entry_count = __cpu_to_le16(header->entry_count),
		.check_byte = header->check_byte,
		.recovery_count = header->recovery_count,
		.metadata_type = header->metadata_type,
	};
}

/**
 * vdo_unpack_recovery_block_header() - Decode the packed representation of a recovery block
 *                                      header.
 * @packed: The packed header to decode.
 *
 * Return: The unpacked header.
 */
static inline struct recovery_block_header
vdo_unpack_recovery_block_header(const struct packed_journal_header *packed)
{
	return (struct recovery_block_header) {
		.block_map_head = __le64_to_cpu(packed->block_map_head),
		.slab_journal_head = __le64_to_cpu(packed->slab_journal_head),
		.sequence_number = __le64_to_cpu(packed->sequence_number),
		.nonce = __le64_to_cpu(packed->nonce),
		.logical_blocks_used = __le64_to_cpu(packed->logical_blocks_used),
		.block_map_data_blocks = __le64_to_cpu(packed->block_map_data_blocks),
		.entry_count = __le16_to_cpu(packed->entry_count),
		.check_byte = packed->check_byte,
		.recovery_count = packed->recovery_count,
		.metadata_type = packed->metadata_type,
	};
}

/**
 * vdo_compute_slab_count() - Compute the number of slabs a depot with given parameters would have.
 * @first_block: PBN of the first data block.
 * @last_block: PBN of the last data block.
 * @slab_size_shift: Exponent for the number of blocks per slab.
 *
 * Return: The number of slabs.
 */
static inline slab_count_t vdo_compute_slab_count(physical_block_number_t first_block,
						  physical_block_number_t last_block,
						  unsigned int slab_size_shift)
{
	return (slab_count_t) ((last_block - first_block) >> slab_size_shift);
}

int __must_check vdo_configure_slab_depot(const struct partition *partition,
					  struct slab_config slab_config,
					  zone_count_t zone_count,
					  struct slab_depot_state_2_0 *state);

int __must_check vdo_configure_slab(block_count_t slab_size,
				    block_count_t slab_journal_blocks,
				    struct slab_config *slab_config);

/**
 * vdo_get_saved_reference_count_size() - Get the number of blocks required to save a reference
 *                                        counts state covering the specified number of data
 *                                        blocks.
 * @block_count: The number of physical data blocks that can be referenced.
 *
 * Return: The number of blocks required to save reference counts with the given block count.
 */
static inline block_count_t vdo_get_saved_reference_count_size(block_count_t block_count)
{
	return DIV_ROUND_UP(block_count, COUNTS_PER_BLOCK);
}

/**
 * vdo_get_slab_journal_start_block() - Get the physical block number of the start of the slab
 *                                      journal relative to the start block allocator partition.
 * @slab_config: The slab configuration of the VDO.
 * @origin: The first block of the slab.
 */
static inline physical_block_number_t __must_check
vdo_get_slab_journal_start_block(const struct slab_config *slab_config,
				 physical_block_number_t origin)
{
	return origin + slab_config->data_blocks + slab_config->reference_count_blocks;
}

/**
 * vdo_advance_journal_point() - Move the given journal point forward by one entry.
 * @point: The journal point to adjust.
 * @entries_per_block: The number of entries in one full block.
 */
static inline void vdo_advance_journal_point(struct journal_point *point,
					     journal_entry_count_t entries_per_block)
{
	point->entry_count++;
	if (point->entry_count == entries_per_block) {
		point->sequence_number++;
		point->entry_count = 0;
	}
}

/**
 * vdo_before_journal_point() - Check whether the first point precedes the second point.
 * @first: The first journal point.
 * @second: The second journal point.
 *
 * Return: true if the first point precedes the second point.
 */
static inline bool vdo_before_journal_point(const struct journal_point *first,
					    const struct journal_point *second)
{
	return ((first->sequence_number < second->sequence_number) ||
		((first->sequence_number == second->sequence_number) &&
		 (first->entry_count < second->entry_count)));
}

/**
 * vdo_pack_journal_point() - Encode the journal location represented by a
 *                            journal_point into a packed_journal_point.
 * @unpacked: The unpacked input point.
 * @packed: The packed output point.
 */
static inline void vdo_pack_journal_point(const struct journal_point *unpacked,
					  struct packed_journal_point *packed)
{
	packed->encoded_point =
		__cpu_to_le64((unpacked->sequence_number << 16) | unpacked->entry_count);
}

/**
 * vdo_unpack_journal_point() - Decode the journal location represented by a packed_journal_point
 *                              into a journal_point.
 * @packed: The packed input point.
 * @unpacked: The unpacked output point.
 */
static inline void vdo_unpack_journal_point(const struct packed_journal_point *packed,
					    struct journal_point *unpacked)
{
	u64 native = __le64_to_cpu(packed->encoded_point);

	unpacked->sequence_number = (native >> 16);
	unpacked->entry_count = (native & 0xffff);
}

/**
 * vdo_pack_slab_journal_block_header() - Generate the packed representation of a slab block
 *                                        header.
 * @header: The header containing the values to encode.
 * @packed: The header into which to pack the values.
 */
static inline void
vdo_pack_slab_journal_block_header(const struct slab_journal_block_header *header,
				   struct packed_slab_journal_block_header *packed)
{
	packed->head = __cpu_to_le64(header->head);
	packed->sequence_number = __cpu_to_le64(header->sequence_number);
	packed->nonce = __cpu_to_le64(header->nonce);
	packed->entry_count = __cpu_to_le16(header->entry_count);
	packed->metadata_type = header->metadata_type;
	packed->has_block_map_increments = header->has_block_map_increments;

	vdo_pack_journal_point(&header->recovery_point, &packed->recovery_point);
}

/**
 * vdo_unpack_slab_journal_block_header() - Decode the packed representation of a slab block
 *                                          header.
 * @packed: The packed header to decode.
 * @header: The header into which to unpack the values.
 */
static inline void
vdo_unpack_slab_journal_block_header(const struct packed_slab_journal_block_header *packed,
				     struct slab_journal_block_header *header)
{
	*header = (struct slab_journal_block_header) {
		.head = __le64_to_cpu(packed->head),
		.sequence_number = __le64_to_cpu(packed->sequence_number),
		.nonce = __le64_to_cpu(packed->nonce),
		.entry_count = __le16_to_cpu(packed->entry_count),
		.metadata_type = packed->metadata_type,
		.has_block_map_increments = packed->has_block_map_increments,
	};
	vdo_unpack_journal_point(&packed->recovery_point, &header->recovery_point);
}

/**
 * vdo_pack_slab_journal_entry() - Generate the packed encoding of a slab journal entry.
 * @packed: The entry into which to pack the values.
 * @sbn: The slab block number of the entry to encode.
 * @is_increment: The increment flag.
 */
static inline void vdo_pack_slab_journal_entry(packed_slab_journal_entry *packed,
					       slab_block_number sbn, bool is_increment)
{
	packed->offset_low8 = (sbn & 0x0000FF);
	packed->offset_mid8 = (sbn & 0x00FF00) >> 8;
	packed->offset_high7 = (sbn & 0x7F0000) >> 16;
	packed->increment = is_increment ? 1 : 0;
}

/**
 * vdo_unpack_slab_journal_entry() - Decode the packed representation of a slab journal entry.
 * @packed: The packed entry to decode.
 *
 * Return: The decoded slab journal entry.
 */
static inline struct slab_journal_entry __must_check
vdo_unpack_slab_journal_entry(const packed_slab_journal_entry *packed)
{
	struct slab_journal_entry entry;

	entry.sbn = packed->offset_high7;
	entry.sbn <<= 8;
	entry.sbn |= packed->offset_mid8;
	entry.sbn <<= 8;
	entry.sbn |= packed->offset_low8;
	entry.operation = VDO_JOURNAL_DATA_REMAPPING;
	entry.increment = packed->increment;
	return entry;
}

struct slab_journal_entry __must_check
vdo_decode_slab_journal_entry(struct packed_slab_journal_block *block,
			      journal_entry_count_t entry_count);

/**
 * vdo_get_slab_summary_hint_shift() - Compute the shift for slab summary hints.
 * @slab_size_shift: Exponent for the number of blocks per slab.
 *
 * Return: The hint shift.
 */
static inline u8 __must_check vdo_get_slab_summary_hint_shift(unsigned int slab_size_shift)
{
	return ((slab_size_shift > VDO_SLAB_SUMMARY_FULLNESS_HINT_BITS) ?
		(slab_size_shift - VDO_SLAB_SUMMARY_FULLNESS_HINT_BITS) :
		0);
}

int __must_check vdo_initialize_layout(block_count_t size,
				       physical_block_number_t offset,
				       block_count_t block_map_blocks,
				       block_count_t journal_blocks,
				       block_count_t summary_blocks,
				       struct layout *layout);

void vdo_uninitialize_layout(struct layout *layout);

int __must_check vdo_get_partition(struct layout *layout, enum partition_id id,
				   struct partition **partition_ptr);

struct partition * __must_check vdo_get_known_partition(struct layout *layout,
							enum partition_id id);

int vdo_validate_config(const struct vdo_config *config,
			block_count_t physical_block_count,
			block_count_t logical_block_count);

void vdo_destroy_component_states(struct vdo_component_states *states);

int __must_check vdo_decode_component_states(u8 *buffer,
					     struct volume_geometry *geometry,
					     struct vdo_component_states *states);

int __must_check vdo_validate_component_states(struct vdo_component_states *states,
					       nonce_t geometry_nonce,
					       block_count_t physical_size,
					       block_count_t logical_size);

void vdo_encode_super_block(u8 *buffer, struct vdo_component_states *states);
int __must_check vdo_decode_super_block(u8 *buffer);

/* We start with 0L and postcondition with ~0L to match our historical usage in userspace. */
static inline u32 vdo_crc32(const void *buf, unsigned long len)
{
	return (crc32(0L, buf, len) ^ ~0L);
}

#endif /* VDO_ENCODINGS_H */
