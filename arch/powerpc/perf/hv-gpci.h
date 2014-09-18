#ifndef LINUX_POWERPC_PERF_HV_GPCI_H_
#define LINUX_POWERPC_PERF_HV_GPCI_H_

#include <linux/types.h>

/* From the document "H_GetPerformanceCounterInfo Interface" v1.07 */

/* H_GET_PERF_COUNTER_INFO argument */
struct hv_get_perf_counter_info_params {
	__be32 counter_request; /* I */
	__be32 starting_index;  /* IO */
	__be16 secondary_index; /* IO */
	__be16 returned_values; /* O */
	__be32 detail_rc; /* O, only needed when called via *_norets() */

	/*
	 * O, size each of counter_value element in bytes, only set for version
	 * >= 0x3
	 */
	__be16 cv_element_size;

	/* I, 0 (zero) for versions < 0x3 */
	__u8 counter_info_version_in;

	/* O, 0 (zero) if version < 0x3. Must be set to 0 when making hcall */
	__u8 counter_info_version_out;
	__u8 reserved[0xC];
	__u8 counter_value[];
} __packed;

/*
 * counter info version => fw version/reference (spec version)
 *
 * 8 => power8 (1.07)
 * [7 is skipped by spec 1.07]
 * 6 => TLBIE (1.07)
 * 5 => v7r7m0.phyp (1.05)
 * [4 skipped]
 * 3 => v7r6m0.phyp (?)
 * [1,2 skipped]
 * 0 => v7r{2,3,4}m0.phyp (?)
 */
#define COUNTER_INFO_VERSION_CURRENT 0x8

/*
 * These determine the counter_value[] layout and the meaning of starting_index
 * and secondary_index.
 *
 * Unless otherwise noted, @secondary_index is unused and ignored.
 */
enum counter_info_requests {

	/* GENERAL */

	/* @starting_index: must be -1 (to refer to the current partition)
	 */
	CIR_SYSTEM_PERFORMANCE_CAPABILITIES = 0X40,
};

struct cv_system_performance_capabilities {
	/* If != 0, allowed to collect data from other partitions */
	__u8 perf_collect_privileged;

	/* These following are only valid if counter_info_version >= 0x3 */
#define CV_CM_GA       (1 << 7)
#define CV_CM_EXPANDED (1 << 6)
#define CV_CM_LAB      (1 << 5)
	/* remaining bits are reserved */
	__u8 capability_mask;
	__u8 reserved[0xE];
} __packed;

#endif
