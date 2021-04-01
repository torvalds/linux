#include <inc/core_status.h>
#include <dc_link.h>
#include <inc/link_hwss.h>
#include <inc/link_dpcd.h>
#include "drm/drm_dp_helper.h"
#include <dc_dp_types.h>
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
 * Ranges of DPCD addresses that must be read in a single transaction
 * XXX: Do not allow any two address ranges in this array to overlap
 */
static const struct dpcd_address_range mandatory_dpcd_blocks[] = {
	{ DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV, DP_PHY_REPEATER_EXTENDED_WAIT_TIMEOUT }};

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
		*out_data = kzalloc(*out_size * sizeof(**out_data), GFP_KERNEL);
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
	const uint32_t reduced_end_address = END_ADDRESS(reduced_address, reduced_size);
	const uint32_t extended_end_address = END_ADDRESS(reduced_address, extended_size);
	const uint32_t offset = reduced_address - extended_address;

	if (extended_end_address == reduced_end_address && extended_address == reduced_address)
		return; /* extended and reduced address ranges point to the same data */

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
	uint8_t *extended_data;
	uint32_t extended_size;
	enum dc_status status;

	dpcd_extend_address_range(address, data, size, &extended_address, &extended_data, &extended_size);
	status = internal_link_read_dpcd(link, extended_address, extended_data, extended_size);
	dpcd_reduce_address_range(extended_address, extended_data, extended_size, address, data, size);
	return status;
}

enum dc_status core_link_write_dpcd(
	struct dc_link *link,
	uint32_t address,
	const uint8_t *data,
	uint32_t size)
{
	return internal_link_write_dpcd(link, address, data, size);
}
