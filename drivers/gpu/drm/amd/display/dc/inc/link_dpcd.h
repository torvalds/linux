#ifndef __LINK_DPCD_H__
#define __LINK_DPCD_H__
#include <inc/core_status.h>
#include <dc_link.h>
#include <inc/link_hwss.h>

enum dc_status core_link_read_dpcd(
		struct dc_link *link,
		uint32_t address,
		uint8_t *data,
		uint32_t size);

enum dc_status core_link_write_dpcd(
		struct dc_link *link,
		uint32_t address,
		const uint8_t *data,
		uint32_t size);
#endif
