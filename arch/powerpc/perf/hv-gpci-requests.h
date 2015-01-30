
#include "req-gen/_begin.h"

/*
 * Based on the document "getPerfCountInfo v1.07"
 */

/*
 * #define REQUEST_NAME counter_request_name
 * #define REQUEST_NUM r_num
 * #define REQUEST_IDX_KIND starting_index_kind
 * #include I(REQUEST_BEGIN)
 * REQUEST(
 *	__field(...)
 *	__field(...)
 *	__array(...)
 *	__count(...)
 * )
 * #include I(REQUEST_END)
 *
 * - starting_index_kind is one of the following, depending on the event:
 *
 *   chip_id: hardware chip id or -1 for current hw chip
 *   phys_processor_idx:
 *   0xffffffffffffffff: or -1, which means it is irrelavant for the event
 *
 * __count(offset, bytes, name):
 *	a counter that should be exposed via perf
 * __field(offset, bytes, name)
 *	a normal field
 * __array(offset, bytes, name)
 *	an array of bytes
 *
 *
 *	@bytes for __count, and __field _must_ be a numeral token
 *	in decimal, not an expression and not in hex.
 *
 *
 * TODO:
 *	- expose secondary index (if any counter ever uses it, only 0xA0
 *	  appears to use it right now, and it doesn't have any counters)
 *	- embed versioning info
 *	- include counter descriptions
 */
#define REQUEST_NAME dispatch_timebase_by_processor
#define REQUEST_NUM 0x10
#define REQUEST_IDX_KIND "phys_processor_idx=?"
#include I(REQUEST_BEGIN)
REQUEST(__count(0,	8,	processor_time_in_timebase_cycles)
	__field(0x8,	4,	hw_processor_id)
	__field(0xC,	2,	owning_part_id)
	__field(0xE,	1,	processor_state)
	__field(0xF,	1,	version)
	__field(0x10,	4,	hw_chip_id)
	__field(0x14,	4,	phys_module_id)
	__field(0x18,	4,	primary_affinity_domain_idx)
	__field(0x1C,	4,	secondary_affinity_domain_idx)
	__field(0x20,	4,	processor_version)
	__field(0x24,	2,	logical_processor_idx)
	__field(0x26,	2,	reserved)
	__field(0x28,	4,	processor_id_register)
	__field(0x2C,	4,	phys_processor_idx)
)
#include I(REQUEST_END)

#define REQUEST_NAME system_performance_capabilities
#define REQUEST_NUM 0x40
#define REQUEST_IDX_KIND "starting_index=0xffffffffffffffff"
#include I(REQUEST_BEGIN)
REQUEST(__field(0,	1,	perf_collect_privileged)
	__field(0x1,	1,	capability_mask)
	__array(0x2,	0xE,	reserved)
)
#include I(REQUEST_END)

#include "req-gen/_end.h"
