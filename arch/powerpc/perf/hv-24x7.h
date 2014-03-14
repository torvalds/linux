#ifndef LINUX_POWERPC_PERF_HV_24X7_H_
#define LINUX_POWERPC_PERF_HV_24X7_H_

#include <linux/types.h>

struct hv_24x7_request {
	/* PHYSICAL domains require enabling via phyp/hmc. */
#define HV_24X7_PERF_DOMAIN_PHYSICAL_CHIP 0x01
#define HV_24X7_PERF_DOMAIN_PHYSICAL_CORE 0x02
#define HV_24X7_PERF_DOMAIN_VIRTUAL_PROCESSOR_HOME_CORE   0x03
#define HV_24X7_PERF_DOMAIN_VIRTUAL_PROCESSOR_HOME_CHIP   0x04
#define HV_24X7_PERF_DOMAIN_VIRTUAL_PROCESSOR_HOME_NODE   0x05
#define HV_24X7_PERF_DOMAIN_VIRTUAL_PROCESSOR_REMOTE_NODE 0x06
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
} __packed;

struct hv_24x7_request_buffer {
	/* 0 - ? */
	/* 1 - ? */
#define HV_24X7_IF_VERSION_CURRENT 0x01
	__u8 interface_version;
	__u8 num_requests;
	__u8 reserved[0xE];
	struct hv_24x7_request requests[];
} __packed;

struct hv_24x7_result_element {
	__be16 lpar_ix;

	/*
	 * represents the core, chip, or virtual processor based on the
	 * request's @performance_domain
	 */
	__be16 domain_ix;

	/* -1 if @performance_domain does not refer to a virtual processor */
	__be32 lpar_cfg_instance_id;

	/* size = @result_element_data_size of cointaining result. */
	__u8 element_data[];
} __packed;

struct hv_24x7_result {
	__u8 result_ix;

	/*
	 * 0 = not all result elements fit into the buffer, additional requests
	 *     required
	 * 1 = all result elements were returned
	 */
	__u8 results_complete;
	__be16 num_elements_returned;

	/* This is a copy of @data_size from the coresponding hv_24x7_request */
	__be16 result_element_data_size;
	__u8 reserved[0x2];

	/* WARNING: only valid for first result element due to variable sizes
	 *          of result elements */
	/* struct hv_24x7_result_element[@num_elements_returned] */
	struct hv_24x7_result_element elements[];
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
