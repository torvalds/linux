#ifndef LINUX_POWERPC_PERF_HV_COMMON_H_
#define LINUX_POWERPC_PERF_HV_COMMON_H_

#include <linux/types.h>

struct hv_perf_caps {
	u16 version;
	u16 collect_privileged:1,
	    ga:1,
	    expanded:1,
	    lab:1,
	    unused:12;
};

unsigned long hv_perf_caps_get(struct hv_perf_caps *caps);

#endif
