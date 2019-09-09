/* SPDX-License-Identifier: GPL-2.0 */
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

/* capability mask masks. */
enum {
	HV_GPCI_CM_GA = (1 << 7),
	HV_GPCI_CM_EXPANDED = (1 << 6),
	HV_GPCI_CM_LAB = (1 << 5)
};

#define REQUEST_FILE "../hv-gpci-requests.h"
#define NAME_LOWER hv_gpci
#define NAME_UPPER HV_GPCI
#include "req-gen/perf.h"
#undef REQUEST_FILE
#undef NAME_LOWER
#undef NAME_UPPER

#endif
