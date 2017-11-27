/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_POWERPC_PERF_HV_24X7_H_
#define LINUX_POWERPC_PERF_HV_24X7_H_

#include <linux/types.h>

enum hv_perf_domains {
#define DOMAIN(n, v, x, c) HV_PERF_DOMAIN_##n = v,
#include "hv-24x7-domains.h"
#undef DOMAIN
	HV_PERF_DOMAIN_MAX,
};

#define H24x7_REQUEST_SIZE(iface_version)	(iface_version == 1 ? 16 : 32)

struct hv_24x7_request {
	/* PHYSICAL domains require enabling via phyp/hmc. */
	__u8 performance_domain;
	__u8 reserved[0x1];

	/* bytes to read starting at @data_offset. must be a multiple of 8 */
	__be16 data_size;

	/*
	 * byte offset within the perf domain to read from. must be 8 byte
	 * aligned
	 */
	__be32 data_offset;

	/*
	 * only valid for VIRTUAL_PROCESSOR domains, ignored for others.
	 * -1 means "current partition only"
	 *  Enabling via phyp/hmc required for non-"-1" values. 0 forbidden
	 *  unless requestor is 0.
	 */
	__be16 starting_lpar_ix;

	/*
	 * Ignored when @starting_lpar_ix == -1
	 * Ignored when @performance_domain is not VIRTUAL_PROCESSOR_*
	 * -1 means "infinite" or all
	 */
	__be16 max_num_lpars;

	/* chip, core, or virtual processor based on @performance_domain */
	__be16 starting_ix;
	__be16 max_ix;

	/* The following fields were added in v2 of the 24x7 interface. */

	__u8 starting_thread_group_ix;

	/* -1 means all thread groups starting at @starting_thread_group_ix */
	__u8 max_num_thread_groups;

	__u8 reserved2[0xE];
} __packed;

struct hv_24x7_request_buffer {
	/* 0 - ? */
	/* 1 - ? */
	__u8 interface_version;
	__u8 num_requests;
	__u8 reserved[0xE];
	struct hv_24x7_request requests[];
} __packed;

struct hv_24x7_result_element_v1 {
	__be16 lpar_ix;

	/*
	 * represents the core, chip, or virtual processor based on the
	 * request's @performance_domain
	 */
	__be16 domain_ix;

	/* -1 if @performance_domain does not refer to a virtual processor */
	__be32 lpar_cfg_instance_id;

	/* size = @result_element_data_size of containing result. */
	__u64 element_data[];
} __packed;

/*
 * We need a separate struct for v2 because the offset of @element_data changed
 * between versions.
 */
struct hv_24x7_result_element_v2 {
	__be16 lpar_ix;

	/*
	 * represents the core, chip, or virtual processor based on the
	 * request's @performance_domain
	 */
	__be16 domain_ix;

	/* -1 if @performance_domain does not refer to a virtual processor */
	__be32 lpar_cfg_instance_id;

	__u8 thread_group_ix;

	__u8 reserved[7];

	/* size = @result_element_data_size of containing result. */
	__u64 element_data[];
} __packed;

struct hv_24x7_result {
	/*
	 * The index of the 24x7 Request Structure in the 24x7 Request Buffer
	 * used to request this result.
	 */
	__u8 result_ix;

	/*
	 * 0 = not all result elements fit into the buffer, additional requests
	 *     required
	 * 1 = all result elements were returned
	 */
	__u8 results_complete;
	__be16 num_elements_returned;

	/*
	 * This is a copy of @data_size from the corresponding hv_24x7_request
	 *
	 * Warning: to obtain the size of each element in @elements you have
	 * to add the size of the other members of the result_element struct.
	 */
	__be16 result_element_data_size;
	__u8 reserved[0x2];

	/*
	 * Either
	 *	struct hv_24x7_result_element_v1[@num_elements_returned]
	 * or
	 *	struct hv_24x7_result_element_v2[@num_elements_returned]
	 *
	 * depending on the interface_version field of the
	 * struct hv_24x7_data_result_buffer containing this result.
	 */
	char elements[];
} __packed;

struct hv_24x7_data_result_buffer {
	/* See versioning for request buffer */
	__u8 interface_version;

	__u8 num_results;
	__u8 reserved[0x1];
	__u8 failing_request_ix;
	__be32 detailed_rc;
	__be64 cec_cfg_instance_id;
	__be64 catalog_version_num;
	__u8 reserved2[0x8];
	/* WARNING: only valid for the first result due to variable sizes of
	 *	    results */
	struct hv_24x7_result results[]; /* [@num_results] */
} __packed;

#endif
