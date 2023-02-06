/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_POWERPC_PERF_HV_GPCI_H_
#define LINUX_POWERPC_PERF_HV_GPCI_H_

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
#define ENABLE_EVENTS_COUNTERINFO_V6
#include "req-gen/perf.h"
#undef REQUEST_FILE
#undef NAME_LOWER
#undef NAME_UPPER

#endif
