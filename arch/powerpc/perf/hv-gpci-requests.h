/* SPDX-License-Identifier: GPL-2.0 */

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
 *   hw_chip_id: hardware chip id or -1 for current hw chip
 *   partition_id
 *   sibling_part_id,
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

#define REQUEST_NAME entitled_capped_uncapped_donated_idle_timebase_by_partition
#define REQUEST_NUM 0x20
#define REQUEST_IDX_KIND "sibling_part_id=?"
#include I(REQUEST_BEGIN)
REQUEST(__field(0,	8,	partition_id)
	__count(0x8,	8,	entitled_cycles)
	__count(0x10,	8,	consumed_capped_cycles)
	__count(0x18,	8,	consumed_uncapped_cycles)
	__count(0x20,	8,	cycles_donated)
	__count(0x28,	8,	purr_idle_cycles)
)
#include I(REQUEST_END)

/*
 * Not available for counter_info_version >= 0x8, use
 * run_instruction_cycles_by_partition(0x100) instead.
 */
#define REQUEST_NAME run_instructions_run_cycles_by_partition
#define REQUEST_NUM 0x30
#define REQUEST_IDX_KIND "sibling_part_id=?"
#include I(REQUEST_BEGIN)
REQUEST(__field(0,	8,	partition_id)
	__count(0x8,	8,	instructions_completed)
	__count(0x10,	8,	cycles)
)
#include I(REQUEST_END)

#define REQUEST_NAME system_performance_capabilities
#define REQUEST_NUM 0x40
#define REQUEST_IDX_KIND "starting_index=0xffffffff"
#include I(REQUEST_BEGIN)
REQUEST(__field(0,	1,	perf_collect_privileged)
	__field(0x1,	1,	capability_mask)
	__array(0x2,	0xE,	reserved)
)
#include I(REQUEST_END)

#define REQUEST_NAME processor_bus_utilization_abc_links
#define REQUEST_NUM 0x50
#define REQUEST_IDX_KIND "hw_chip_id=?"
#include I(REQUEST_BEGIN)
REQUEST(__field(0,	4,	hw_chip_id)
	__array(0x4,	0xC,	reserved1)
	__count(0x10,	8,	total_link_cycles)
	__count(0x18,	8,	idle_cycles_for_a_link)
	__count(0x20,	8,	idle_cycles_for_b_link)
	__count(0x28,	8,	idle_cycles_for_c_link)
	__array(0x30,	0x20,	reserved2)
)
#include I(REQUEST_END)

#define REQUEST_NAME processor_bus_utilization_wxyz_links
#define REQUEST_NUM 0x60
#define REQUEST_IDX_KIND "hw_chip_id=?"
#include I(REQUEST_BEGIN)
REQUEST(__field(0,	4,	hw_chip_id)
	__array(0x4,	0xC,	reserved1)
	__count(0x10,	8,	total_link_cycles)
	__count(0x18,	8,	idle_cycles_for_w_link)
	__count(0x20,	8,	idle_cycles_for_x_link)
	__count(0x28,	8,	idle_cycles_for_y_link)
	__count(0x30,	8,	idle_cycles_for_z_link)
	__array(0x38,	0x28,	reserved2)
)
#include I(REQUEST_END)

#define REQUEST_NAME processor_bus_utilization_gx_links
#define REQUEST_NUM 0x70
#define REQUEST_IDX_KIND "hw_chip_id=?"
#include I(REQUEST_BEGIN)
REQUEST(__field(0,	4,	hw_chip_id)
	__array(0x4,	0xC,	reserved1)
	__count(0x10,	8,	gx0_in_address_cycles)
	__count(0x18,	8,	gx0_in_data_cycles)
	__count(0x20,	8,	gx0_in_retries)
	__count(0x28,	8,	gx0_in_bus_cycles)
	__count(0x30,	8,	gx0_in_cycles_total)
	__count(0x38,	8,	gx0_out_address_cycles)
	__count(0x40,	8,	gx0_out_data_cycles)
	__count(0x48,	8,	gx0_out_retries)
	__count(0x50,	8,	gx0_out_bus_cycles)
	__count(0x58,	8,	gx0_out_cycles_total)
	__count(0x60,	8,	gx1_in_address_cycles)
	__count(0x68,	8,	gx1_in_data_cycles)
	__count(0x70,	8,	gx1_in_retries)
	__count(0x78,	8,	gx1_in_bus_cycles)
	__count(0x80,	8,	gx1_in_cycles_total)
	__count(0x88,	8,	gx1_out_address_cycles)
	__count(0x90,	8,	gx1_out_data_cycles)
	__count(0x98,	8,	gx1_out_retries)
	__count(0xA0,	8,	gx1_out_bus_cycles)
	__count(0xA8,	8,	gx1_out_cycles_total)
)
#include I(REQUEST_END)

#define REQUEST_NAME processor_bus_utilization_mc_links
#define REQUEST_NUM 0x80
#define REQUEST_IDX_KIND "hw_chip_id=?"
#include I(REQUEST_BEGIN)
REQUEST(__field(0,	4,	hw_chip_id)
	__array(0x4,	0xC,	reserved1)
	__count(0x10,	8,	mc0_frames)
	__count(0x18,	8,	mc0_reads)
	__count(0x20,	8,	mc0_write)
	__count(0x28,	8,	mc0_total_cycles)
	__count(0x30,	8,	mc1_frames)
	__count(0x38,	8,	mc1_reads)
	__count(0x40,	8,	mc1_writes)
	__count(0x48,	8,	mc1_total_cycles)
)
#include I(REQUEST_END)

/* Processor_config (0x90) skipped, no counters */
/* Current_processor_frequency (0x91) skipped, no counters */

#define REQUEST_NAME processor_core_utilization
#define REQUEST_NUM 0x94
#define REQUEST_IDX_KIND "phys_processor_idx=?"
#include I(REQUEST_BEGIN)
REQUEST(__field(0,	4,	phys_processor_idx)
	__field(0x4,	4,	hw_processor_id)
	__count(0x8,	8,	cycles_across_any_thread)
	__count(0x10,	8,	timebase_at_collection)
	__count(0x18,	8,	purr_cycles)
	__count(0x20,	8,	sum_of_cycles_across_all_threads)
	__count(0x28,	8,	instructions_completed)
)
#include I(REQUEST_END)

/* Processor_core_power_mode (0x95) skipped, no counters */
/* Affinity_domain_information_by_virtual_processor (0xA0) skipped,
 *	no counters */
/* Affinity_domain_information_by_domain (0xB0) skipped, no counters */
/* Affinity_domain_information_by_partition (0xB1) skipped, no counters */
/* Physical_memory_info (0xC0) skipped, no counters */
/* Processor_bus_topology (0xD0) skipped, no counters */

#define REQUEST_NAME partition_hypervisor_queuing_times
#define REQUEST_NUM 0xE0
#define REQUEST_IDX_KIND "partition_id=?"
#include I(REQUEST_BEGIN)
REQUEST(__field(0,	2, partition_id)
	__array(0x2,	6, reserved1)
	__count(0x8,	8, time_waiting_for_entitlement)
	__count(0x10,	8, times_waited_for_entitlement)
	__count(0x18,	8, time_waiting_for_phys_processor)
	__count(0x20,	8, times_waited_for_phys_processor)
	__count(0x28,	8, dispatches_on_home_core)
	__count(0x30,	8, dispatches_on_home_primary_affinity_domain)
	__count(0x38,	8, dispatches_on_home_secondary_affinity_domain)
	__count(0x40,	8, dispatches_off_home_secondary_affinity_domain)
	__count(0x48,	8, dispatches_on_dedicated_processor_donating_cycles)
)
#include I(REQUEST_END)

#define REQUEST_NAME system_hypervisor_times
#define REQUEST_NUM 0xF0
#define REQUEST_IDX_KIND "starting_index=0xffffffff"
#include I(REQUEST_BEGIN)
REQUEST(__count(0,	8,	time_spent_to_dispatch_virtual_processors)
	__count(0x8,	8,	time_spent_processing_virtual_processor_timers)
	__count(0x10,	8,	time_spent_managing_partitions_over_entitlement)
	__count(0x18,	8,	time_spent_on_system_management)
)
#include I(REQUEST_END)

#define REQUEST_NAME system_tlbie_count_and_time
#define REQUEST_NUM 0xF4
#define REQUEST_IDX_KIND "starting_index=0xffffffff"
#include I(REQUEST_BEGIN)
REQUEST(__count(0,	8,	tlbie_instructions_issued)
	/*
	 * FIXME: The spec says the offset here is 0x10, which I suspect
	 *	  is wrong.
	 */
	__count(0x8,	8,	time_spent_issuing_tlbies)
)
#include I(REQUEST_END)

#define REQUEST_NAME partition_instruction_count_and_time
#define REQUEST_NUM 0x100
#define REQUEST_IDX_KIND "partition_id=?"
#include I(REQUEST_BEGIN)
REQUEST(__field(0,	2,	partition_id)
	__array(0x2,	0x6,	reserved1)
	__count(0x8,	8,	instructions_performed)
	__count(0x10,	8,	time_collected)
)
#include I(REQUEST_END)

/* set_mmcrh (0x80001000) skipped, no counters */
/* retrieve_hpmcx (0x80002000) skipped, no counters */

#include "req-gen/_end.h"
