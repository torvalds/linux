// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "config.h"

#include "logger.h"
#include "memory-alloc.h"
#include "numeric.h"
#include "string-utils.h"
#include "thread-utils.h"

static const u8 INDEX_CONFIG_MAGIC[] = "ALBIC";
static const u8 INDEX_CONFIG_VERSION_6_02[] = "06.02";
static const u8 INDEX_CONFIG_VERSION_8_02[] = "08.02";

#define DEFAULT_VOLUME_READ_THREADS 2
#define MAX_VOLUME_READ_THREADS 16
#define INDEX_CONFIG_MAGIC_LENGTH (sizeof(INDEX_CONFIG_MAGIC) - 1)
#define INDEX_CONFIG_VERSION_LENGTH ((int)(sizeof(INDEX_CONFIG_VERSION_6_02) - 1))

static bool is_version(const u8 *version, u8 *buffer)
{
	return memcmp(version, buffer, INDEX_CONFIG_VERSION_LENGTH) == 0;
}

static bool are_matching_configurations(struct uds_configuration *saved_config,
					struct index_geometry *saved_geometry,
					struct uds_configuration *user)
{
	struct index_geometry *geometry = user->geometry;
	bool result = true;

	if (saved_geometry->record_pages_per_chapter != geometry->record_pages_per_chapter) {
		vdo_log_error("Record pages per chapter (%u) does not match (%u)",
			      saved_geometry->record_pages_per_chapter,
			      geometry->record_pages_per_chapter);
		result = false;
	}

	if (saved_geometry->chapters_per_volume != geometry->chapters_per_volume) {
		vdo_log_error("Chapter count (%u) does not match (%u)",
			      saved_geometry->chapters_per_volume,
			      geometry->chapters_per_volume);
		result = false;
	}

	if (saved_geometry->sparse_chapters_per_volume != geometry->sparse_chapters_per_volume) {
		vdo_log_error("Sparse chapter count (%u) does not match (%u)",
			      saved_geometry->sparse_chapters_per_volume,
			      geometry->sparse_chapters_per_volume);
		result = false;
	}

	if (saved_config->cache_chapters != user->cache_chapters) {
		vdo_log_error("Cache size (%u) does not match (%u)",
			      saved_config->cache_chapters, user->cache_chapters);
		result = false;
	}

	if (saved_config->volume_index_mean_delta != user->volume_index_mean_delta) {
		vdo_log_error("Volume index mean delta (%u) does not match (%u)",
			      saved_config->volume_index_mean_delta,
			      user->volume_index_mean_delta);
		result = false;
	}

	if (saved_geometry->bytes_per_page != geometry->bytes_per_page) {
		vdo_log_error("Bytes per page value (%zu) does not match (%zu)",
			      saved_geometry->bytes_per_page, geometry->bytes_per_page);
		result = false;
	}

	if (saved_config->sparse_sample_rate != user->sparse_sample_rate) {
		vdo_log_error("Sparse sample rate (%u) does not match (%u)",
			      saved_config->sparse_sample_rate,
			      user->sparse_sample_rate);
		result = false;
	}

	if (saved_config->nonce != user->nonce) {
		vdo_log_error("Nonce (%llu) does not match (%llu)",
			      (unsigned long long) saved_config->nonce,
			      (unsigned long long) user->nonce);
		result = false;
	}

	return result;
}

/* Read the configuration and validate it against the provided one. */
int uds_validate_config_contents(struct buffered_reader *reader,
				 struct uds_configuration *user_config)
{
	int result;
	struct uds_configuration config;
	struct index_geometry geometry;
	u8 version_buffer[INDEX_CONFIG_VERSION_LENGTH];
	u32 bytes_per_page;
	u8 buffer[sizeof(struct uds_configuration_6_02)];
	size_t offset = 0;

	result = uds_verify_buffered_data(reader, INDEX_CONFIG_MAGIC,
					  INDEX_CONFIG_MAGIC_LENGTH);
	if (result != UDS_SUCCESS)
		return result;

	result = uds_read_from_buffered_reader(reader, version_buffer,
					       INDEX_CONFIG_VERSION_LENGTH);
	if (result != UDS_SUCCESS)
		return vdo_log_error_strerror(result, "cannot read index config version");

	if (!is_version(INDEX_CONFIG_VERSION_6_02, version_buffer) &&
	    !is_version(INDEX_CONFIG_VERSION_8_02, version_buffer)) {
		return vdo_log_error_strerror(UDS_CORRUPT_DATA,
					      "unsupported configuration version: '%.*s'",
					      INDEX_CONFIG_VERSION_LENGTH,
					      version_buffer);
	}

	result = uds_read_from_buffered_reader(reader, buffer, sizeof(buffer));
	if (result != UDS_SUCCESS)
		return vdo_log_error_strerror(result, "cannot read config data");

	decode_u32_le(buffer, &offset, &geometry.record_pages_per_chapter);
	decode_u32_le(buffer, &offset, &geometry.chapters_per_volume);
	decode_u32_le(buffer, &offset, &geometry.sparse_chapters_per_volume);
	decode_u32_le(buffer, &offset, &config.cache_chapters);
	offset += sizeof(u32);
	decode_u32_le(buffer, &offset, &config.volume_index_mean_delta);
	decode_u32_le(buffer, &offset, &bytes_per_page);
	geometry.bytes_per_page = bytes_per_page;
	decode_u32_le(buffer, &offset, &config.sparse_sample_rate);
	decode_u64_le(buffer, &offset, &config.nonce);

	result = VDO_ASSERT(offset == sizeof(struct uds_configuration_6_02),
			    "%zu bytes read but not decoded",
			    sizeof(struct uds_configuration_6_02) - offset);
	if (result != VDO_SUCCESS)
		return UDS_CORRUPT_DATA;

	if (is_version(INDEX_CONFIG_VERSION_6_02, version_buffer)) {
		user_config->geometry->remapped_virtual = 0;
		user_config->geometry->remapped_physical = 0;
	} else {
		u8 remapping[sizeof(u64) + sizeof(u64)];

		result = uds_read_from_buffered_reader(reader, remapping,
						       sizeof(remapping));
		if (result != UDS_SUCCESS)
			return vdo_log_error_strerror(result, "cannot read converted config");

		offset = 0;
		decode_u64_le(remapping, &offset,
			      &user_config->geometry->remapped_virtual);
		decode_u64_le(remapping, &offset,
			      &user_config->geometry->remapped_physical);
	}

	if (!are_matching_configurations(&config, &geometry, user_config)) {
		vdo_log_warning("Supplied configuration does not match save");
		return UDS_NO_INDEX;
	}

	return UDS_SUCCESS;
}

/*
 * Write the configuration to stable storage. If the superblock version is < 4, write the 6.02
 * version; otherwise write the 8.02 version, indicating the configuration is for an index that has
 * been reduced by one chapter.
 */
int uds_write_config_contents(struct buffered_writer *writer,
			      struct uds_configuration *config, u32 version)
{
	int result;
	struct index_geometry *geometry = config->geometry;
	u8 buffer[sizeof(struct uds_configuration_8_02)];
	size_t offset = 0;

	result = uds_write_to_buffered_writer(writer, INDEX_CONFIG_MAGIC,
					      INDEX_CONFIG_MAGIC_LENGTH);
	if (result != UDS_SUCCESS)
		return result;

	/*
	 * If version is < 4, the index has not been reduced by a chapter so it must be written out
	 * as version 6.02 so that it is still compatible with older versions of UDS.
	 */
	if (version >= 4) {
		result = uds_write_to_buffered_writer(writer, INDEX_CONFIG_VERSION_8_02,
						      INDEX_CONFIG_VERSION_LENGTH);
		if (result != UDS_SUCCESS)
			return result;
	} else {
		result = uds_write_to_buffered_writer(writer, INDEX_CONFIG_VERSION_6_02,
						      INDEX_CONFIG_VERSION_LENGTH);
		if (result != UDS_SUCCESS)
			return result;
	}

	encode_u32_le(buffer, &offset, geometry->record_pages_per_chapter);
	encode_u32_le(buffer, &offset, geometry->chapters_per_volume);
	encode_u32_le(buffer, &offset, geometry->sparse_chapters_per_volume);
	encode_u32_le(buffer, &offset, config->cache_chapters);
	encode_u32_le(buffer, &offset, 0);
	encode_u32_le(buffer, &offset, config->volume_index_mean_delta);
	encode_u32_le(buffer, &offset, geometry->bytes_per_page);
	encode_u32_le(buffer, &offset, config->sparse_sample_rate);
	encode_u64_le(buffer, &offset, config->nonce);

	result = VDO_ASSERT(offset == sizeof(struct uds_configuration_6_02),
			    "%zu bytes encoded, of %zu expected", offset,
			    sizeof(struct uds_configuration_6_02));
	if (result != VDO_SUCCESS)
		return result;

	if (version >= 4) {
		encode_u64_le(buffer, &offset, geometry->remapped_virtual);
		encode_u64_le(buffer, &offset, geometry->remapped_physical);
	}

	return uds_write_to_buffered_writer(writer, buffer, offset);
}

/* Compute configuration parameters that depend on memory size. */
static int compute_memory_sizes(uds_memory_config_size_t mem_gb, bool sparse,
				u32 *chapters_per_volume, u32 *record_pages_per_chapter,
				u32 *sparse_chapters_per_volume)
{
	u32 reduced_chapters = 0;
	u32 base_chapters;

	if (mem_gb == UDS_MEMORY_CONFIG_256MB) {
		base_chapters = DEFAULT_CHAPTERS_PER_VOLUME;
		*record_pages_per_chapter = SMALL_RECORD_PAGES_PER_CHAPTER;
	} else if (mem_gb == UDS_MEMORY_CONFIG_512MB) {
		base_chapters = DEFAULT_CHAPTERS_PER_VOLUME;
		*record_pages_per_chapter = 2 * SMALL_RECORD_PAGES_PER_CHAPTER;
	} else if (mem_gb == UDS_MEMORY_CONFIG_768MB) {
		base_chapters = DEFAULT_CHAPTERS_PER_VOLUME;
		*record_pages_per_chapter = 3 * SMALL_RECORD_PAGES_PER_CHAPTER;
	} else if ((mem_gb >= 1) && (mem_gb <= UDS_MEMORY_CONFIG_MAX)) {
		base_chapters = mem_gb * DEFAULT_CHAPTERS_PER_VOLUME;
		*record_pages_per_chapter = DEFAULT_RECORD_PAGES_PER_CHAPTER;
	} else if (mem_gb == UDS_MEMORY_CONFIG_REDUCED_256MB) {
		reduced_chapters = 1;
		base_chapters = DEFAULT_CHAPTERS_PER_VOLUME;
		*record_pages_per_chapter = SMALL_RECORD_PAGES_PER_CHAPTER;
	} else if (mem_gb == UDS_MEMORY_CONFIG_REDUCED_512MB) {
		reduced_chapters = 1;
		base_chapters = DEFAULT_CHAPTERS_PER_VOLUME;
		*record_pages_per_chapter = 2 * SMALL_RECORD_PAGES_PER_CHAPTER;
	} else if (mem_gb == UDS_MEMORY_CONFIG_REDUCED_768MB) {
		reduced_chapters = 1;
		base_chapters = DEFAULT_CHAPTERS_PER_VOLUME;
		*record_pages_per_chapter = 3 * SMALL_RECORD_PAGES_PER_CHAPTER;
	} else if ((mem_gb >= 1 + UDS_MEMORY_CONFIG_REDUCED) &&
		   (mem_gb <= UDS_MEMORY_CONFIG_REDUCED_MAX)) {
		reduced_chapters = 1;
		base_chapters = ((mem_gb - UDS_MEMORY_CONFIG_REDUCED) *
				 DEFAULT_CHAPTERS_PER_VOLUME);
		*record_pages_per_chapter = DEFAULT_RECORD_PAGES_PER_CHAPTER;
	} else {
		vdo_log_error("received invalid memory size");
		return -EINVAL;
	}

	if (sparse) {
		/* Make 95% of chapters sparse, allowing 10x more records. */
		*sparse_chapters_per_volume = (19 * base_chapters) / 2;
		base_chapters *= 10;
	} else {
		*sparse_chapters_per_volume = 0;
	}

	*chapters_per_volume = base_chapters - reduced_chapters;
	return UDS_SUCCESS;
}

static unsigned int __must_check normalize_zone_count(unsigned int requested)
{
	unsigned int zone_count = requested;

	if (zone_count == 0)
		zone_count = num_online_cpus() / 2;

	if (zone_count < 1)
		zone_count = 1;

	if (zone_count > MAX_ZONES)
		zone_count = MAX_ZONES;

	vdo_log_info("Using %u indexing zone%s for concurrency.",
		     zone_count, zone_count == 1 ? "" : "s");
	return zone_count;
}

static unsigned int __must_check normalize_read_threads(unsigned int requested)
{
	unsigned int read_threads = requested;

	if (read_threads < 1)
		read_threads = DEFAULT_VOLUME_READ_THREADS;

	if (read_threads > MAX_VOLUME_READ_THREADS)
		read_threads = MAX_VOLUME_READ_THREADS;

	return read_threads;
}

int uds_make_configuration(const struct uds_parameters *params,
			   struct uds_configuration **config_ptr)
{
	struct uds_configuration *config;
	u32 chapters_per_volume = 0;
	u32 record_pages_per_chapter = 0;
	u32 sparse_chapters_per_volume = 0;
	int result;

	result = compute_memory_sizes(params->memory_size, params->sparse,
				      &chapters_per_volume, &record_pages_per_chapter,
				      &sparse_chapters_per_volume);
	if (result != UDS_SUCCESS)
		return result;

	result = vdo_allocate(1, struct uds_configuration, __func__, &config);
	if (result != VDO_SUCCESS)
		return result;

	result = uds_make_index_geometry(DEFAULT_BYTES_PER_PAGE, record_pages_per_chapter,
					 chapters_per_volume, sparse_chapters_per_volume,
					 0, 0, &config->geometry);
	if (result != UDS_SUCCESS) {
		uds_free_configuration(config);
		return result;
	}

	config->zone_count = normalize_zone_count(params->zone_count);
	config->read_threads = normalize_read_threads(params->read_threads);

	config->cache_chapters = DEFAULT_CACHE_CHAPTERS;
	config->volume_index_mean_delta = DEFAULT_VOLUME_INDEX_MEAN_DELTA;
	config->sparse_sample_rate = (params->sparse ? DEFAULT_SPARSE_SAMPLE_RATE : 0);
	config->nonce = params->nonce;
	config->bdev = params->bdev;
	config->offset = params->offset;
	config->size = params->size;

	*config_ptr = config;
	return UDS_SUCCESS;
}

void uds_free_configuration(struct uds_configuration *config)
{
	if (config != NULL) {
		uds_free_index_geometry(config->geometry);
		vdo_free(config);
	}
}

void uds_log_configuration(struct uds_configuration *config)
{
	struct index_geometry *geometry = config->geometry;

	vdo_log_debug("Configuration:");
	vdo_log_debug("  Record pages per chapter:   %10u", geometry->record_pages_per_chapter);
	vdo_log_debug("  Chapters per volume:        %10u", geometry->chapters_per_volume);
	vdo_log_debug("  Sparse chapters per volume: %10u", geometry->sparse_chapters_per_volume);
	vdo_log_debug("  Cache size (chapters):      %10u", config->cache_chapters);
	vdo_log_debug("  Volume index mean delta:    %10u", config->volume_index_mean_delta);
	vdo_log_debug("  Bytes per page:             %10zu", geometry->bytes_per_page);
	vdo_log_debug("  Sparse sample rate:         %10u", config->sparse_sample_rate);
	vdo_log_debug("  Nonce:                      %llu", (unsigned long long) config->nonce);
}
