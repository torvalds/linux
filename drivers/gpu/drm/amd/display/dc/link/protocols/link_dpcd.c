/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

/* FILE POLICY AND INTENDED USAGE:
 *
 * This file implements basic dpcd read/write functionality. It also does basic
 * dpcd range check to ensure that every dpcd request is compliant with specs
 * range requirements.
 */

#include "link_dpcd.h"
#include <drm/display/drm_dp_helper.h>
#include "dm_helpers.h"

#define END_ADDRESS(start, size) (start + size - 1)
#define ADDRESS_RANGE_SIZE(start, end) (end - start + 1)
struct dpcd_address_range {
	uint32_t start;
	uint32_t end;
};

static enum dc_status internal_link_read_dpcd(
	struct dc_link *link,
	uint32_t address,
	uint8_t *data,
	uint32_t size)
{
	if (!link->aux_access_disabled &&
			!dm_helpers_dp_read_dpcd(link->ctx,
			link, address, data, size)) {
		return DC_ERROR_UNEXPECTED;
	}

	return DC_OK;
}

static enum dc_status internal_link_write_dpcd(
	struct dc_link *link,
	uint32_t address,
	const uint8_t *data,
	uint32_t size)
{
	if (!link->aux_access_disabled &&
			!dm_helpers_dp_write_dpcd(link->ctx,
			link, address, data, size)) {
		return DC_ERROR_UNEXPECTED;
	}

	return DC_OK;
}

/*
 * Partition the entire DPCD address space
 * XXX: This partitioning must cover the entire DPCD address space,
 * and must contain no gaps or overlapping address ranges.
 */
static const struct dpcd_address_range mandatory_dpcd_partitions[] = {
	{ 0, DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR1) - 1},
	{ DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR1), DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR2) - 1 },
	{ DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR2), DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR3) - 1 },
	{ DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR3), DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR4) - 1 },
	{ DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR4), DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR5) - 1 },
	{ DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR5), DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR6) - 1 },
	{ DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR6), DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR7) - 1 },
	{ DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR7), DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR8) - 1 },
	{ DP_TRAINING_PATTERN_SET_PHY_REPEATER(DP_PHY_LTTPR8), DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR1) - 1 },
	/*
	 * The FEC registers are contiguous
	 */
	{ DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR1), DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR1) - 1 },
	{ DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR2), DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR2) - 1 },
	{ DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR3), DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR3) - 1 },
	{ DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR4), DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR4) - 1 },
	{ DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR5), DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR5) - 1 },
	{ DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR6), DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR6) - 1 },
	{ DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR7), DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR7) - 1 },
	{ DP_FEC_STATUS_PHY_REPEATER(DP_PHY_LTTPR8), DP_LTTPR_MAX_ADD },
	/* all remaining DPCD addresses */
	{ DP_LTTPR_MAX_ADD + 1, DP_DPCD_MAX_ADD } };

static inline bool do_addresses_intersect_with_range(
		const struct dpcd_address_range *range,
		const uint32_t start_address,
		const uint32_t end_address)
{
	return start_address <= range->end && end_address >= range->start;
}

static uint32_t dpcd_get_next_partition_size(const uint32_t address, const uint32_t size)
{
	const uint32_t end_address = END_ADDRESS(address, size);
	uint32_t partition_iterator = 0;

	/*
	 * find current partition
	 * this loop spins forever if partition map above is not surjective
	 */
	while (!do_addresses_intersect_with_range(&mandatory_dpcd_partitions[partition_iterator],
				address, end_address))
		partition_iterator++;
	if (end_address < mandatory_dpcd_partitions[partition_iterator].end)
		return size;
	return ADDRESS_RANGE_SIZE(address, mandatory_dpcd_partitions[partition_iterator].end);
}

/*
 * Ranges of DPCD addresses that must be read in a single transaction
 * XXX: Do not allow any two address ranges in this array to overlap
 */
static const struct dpcd_address_range mandatory_dpcd_blocks[] = {
	{ DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV, DP_PHY_REPEATER_128B132B_RATES }};

/*
 * extend addresses to read all mandatory blocks together
 */
static void dpcd_extend_address_range(
		const uint32_t in_address,
		uint8_t * const in_data,
		const uint32_t in_size,
		uint32_t *out_address,
		uint8_t **out_data,
		uint32_t *out_size)
{
	const uint32_t end_address = END_ADDRESS(in_address, in_size);
	const struct dpcd_address_range *addr_range;
	struct dpcd_address_range new_addr_range;
	uint32_t i;

	new_addr_range.start = in_address;
	new_addr_range.end = end_address;
	for (i = 0; i < ARRAY_SIZE(mandatory_dpcd_blocks); i++) {
		addr_range = &mandatory_dpcd_blocks[i];
		if (addr_range->start <= in_address && addr_range->end >= in_address)
			new_addr_range.start = addr_range->start;

		if (addr_range->start <= end_address && addr_range->end >= end_address)
			new_addr_range.end = addr_range->end;
	}
	*out_address = in_address;
	*out_size = in_size;
	*out_data = in_data;
	if (new_addr_range.start != in_address || new_addr_range.end != end_address) {
		*out_address = new_addr_range.start;
		*out_size = ADDRESS_RANGE_SIZE(new_addr_range.start, new_addr_range.end);
		*out_data = kcalloc(*out_size, sizeof(**out_data), GFP_KERNEL);
		ASSERT(*out_data);
	}
}

/*
 * Reduce the AUX reply down to the values the caller requested
 */
static void dpcd_reduce_address_range(
		const uint32_t extended_address,
		uint8_t * const extended_data,
		const uint32_t extended_size,
		const uint32_t reduced_address,
		uint8_t * const reduced_data,
		const uint32_t reduced_size)
{
	const uint32_t offset = reduced_address - extended_address;

	/*
	 * If the address is same, address was not extended.
	 * So we do not need to free any memory.
	 * The data is in original buffer(reduced_data).
	 */
	if (extended_data == reduced_data)
		return;

	memcpy(&extended_data[offset], reduced_data, reduced_size);
	kfree(extended_data);
}

enum dc_status core_link_read_dpcd(
	struct dc_link *link,
	uint32_t address,
	uint8_t *data,
	uint32_t size)
{
	uint32_t extended_address;
	uint32_t partitioned_address;
	uint8_t *extended_data;
	uint32_t extended_size;
	/* size of the remaining partitioned address space */
	uint32_t size_left_to_read;
	enum dc_status status = DC_ERROR_UNEXPECTED;
	/* size of the next partition to be read from */
	uint32_t partition_size;
	uint32_t data_index = 0;

	dpcd_extend_address_range(address, data, size, &extended_address, &extended_data, &extended_size);
	partitioned_address = extended_address;
	size_left_to_read = extended_size;
	while (size_left_to_read) {
		partition_size = dpcd_get_next_partition_size(partitioned_address, size_left_to_read);
		status = internal_link_read_dpcd(link, partitioned_address, &extended_data[data_index], partition_size);
		if (status != DC_OK)
			break;
		partitioned_address += partition_size;
		data_index += partition_size;
		size_left_to_read -= partition_size;
	}
	dpcd_reduce_address_range(extended_address, extended_data, extended_size, address, data, size);
	return status;
}

enum dc_status core_link_write_dpcd(
	struct dc_link *link,
	uint32_t address,
	const uint8_t *data,
	uint32_t size)
{
	uint32_t partition_size;
	uint32_t data_index = 0;
	enum dc_status status = DC_ERROR_UNEXPECTED;

	while (size) {
		partition_size = dpcd_get_next_partition_size(address, size);
		status = internal_link_write_dpcd(link, address, &data[data_index], partition_size);
		if (status != DC_OK)
			break;
		address += partition_size;
		data_index += partition_size;
		size -= partition_size;
	}
	return status;
}
