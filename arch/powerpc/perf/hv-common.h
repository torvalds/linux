#ifndef LINUX_POWERPC_PERF_HV_COMMON_H_
#define LINUX_POWERPC_PERF_HV_COMMON_H_

#include <linux/perf_event.h>
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


#define EVENT_DEFINE_RANGE_FORMAT(name, attr_var, bit_start, bit_end)	\
PMU_FORMAT_ATTR(name, #attr_var ":" #bit_start "-" #bit_end);		\
EVENT_DEFINE_RANGE(name, attr_var, bit_start, bit_end)

#define EVENT_DEFINE_RANGE(name, attr_var, bit_start, bit_end)	\
static u64 event_get_##name##_max(void)					\
{									\
	BUILD_BUG_ON((bit_start > bit_end)				\
		    || (bit_end >= (sizeof(1ull) * 8)));		\
	return (((1ull << (bit_end - bit_start)) - 1) << 1) + 1;	\
}									\
static u64 event_get_##name(struct perf_event *event)			\
{									\
	return (event->attr.attr_var >> (bit_start)) &			\
		event_get_##name##_max();				\
}

#endif
