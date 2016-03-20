#ifndef LINUX_POWERPC_PERF_REQ_GEN_PERF_H_
#define LINUX_POWERPC_PERF_REQ_GEN_PERF_H_

#include <linux/perf_event.h>

#ifndef REQUEST_FILE
#error "REQUEST_FILE must be defined before including"
#endif

#ifndef NAME_LOWER
#error "NAME_LOWER must be defined before including"
#endif

#ifndef NAME_UPPER
#error "NAME_UPPER must be defined before including"
#endif

#define BE_TYPE_b1 __u8
#define BE_TYPE_b2 __be16
#define BE_TYPE_b4 __be32
#define BE_TYPE_b8 __be64

#define BYTES_TO_BE_TYPE(bytes) \
		BE_TYPE_b##bytes

#define CAT2_(a, b) a ## b
#define CAT2(a, b) CAT2_(a, b)
#define CAT3_(a, b, c) a ## b ## c
#define CAT3(a, b, c) CAT3_(a, b, c)

/*
 * enumerate the request values as
 * <NAME_UPPER>_<request name> = <request value>
 */
#define REQUEST_VALUE__(name_upper, r_name) name_upper ## _ ## r_name
#define REQUEST_VALUE_(name_upper, r_name) REQUEST_VALUE__(name_upper, r_name)
#define REQUEST_VALUE(r_name) REQUEST_VALUE_(NAME_UPPER, r_name)

#include "_clear.h"
#define REQUEST_(r_name, r_value, r_idx_1, r_fields) \
	REQUEST_VALUE(r_name) = r_value,
enum CAT2(NAME_LOWER, _requests) {
#include REQUEST_FILE
};

/*
 * For each request:
 * struct <NAME_LOWER>_<request name> {
 *	r_fields
 * };
 */
#include "_clear.h"
#define STRUCT_NAME__(name_lower, r_name) name_lower ## _ ## r_name
#define STRUCT_NAME_(name_lower, r_name) STRUCT_NAME__(name_lower, r_name)
#define STRUCT_NAME(r_name) STRUCT_NAME_(NAME_LOWER, r_name)
#define REQUEST_(r_name, r_value, r_idx_1, r_fields)	\
struct STRUCT_NAME(r_name) {				\
	r_fields					\
};
#define __field_(r_name, r_value, r_idx_1, f_offset, f_bytes, f_name) \
	BYTES_TO_BE_TYPE(f_bytes) f_name;
#define __count_(r_name, r_value, r_idx_1, f_offset, f_bytes, f_name) \
	__field_(r_name, r_value, r_idx_1, f_offset, f_bytes, f_name)
#define __array_(r_name, r_value, r_idx_1, a_offset, a_bytes, a_name) \
	__u8 a_name[a_bytes];

#include REQUEST_FILE

/*
 * Generate a check of the field offsets
 * <NAME_LOWER>_assert_offsets_correct()
 */
#include "_clear.h"
#define REQUEST_(r_name, r_value, index, r_fields)			\
r_fields
#define __field_(r_name, r_value, r_idx_1, f_offset, f_size, f_name) \
	BUILD_BUG_ON(offsetof(struct STRUCT_NAME(r_name), f_name) != f_offset);
#define __count_(r_name, r_value, r_idx_1, c_offset, c_size, c_name) \
	__field_(r_name, r_value, r_idx_1, c_offset, c_size, c_name)
#define __array_(r_name, r_value, r_idx_1, a_offset, a_size, a_name) \
	__field_(r_name, r_value, r_idx_1, a_offset, a_size, a_name)

static inline void CAT2(NAME_LOWER, _assert_offsets_correct)(void)
{
#include REQUEST_FILE
}

/*
 * Generate event attributes:
 * PMU_EVENT_ATTR_STRING(<request name>_<field name>,
 *	<NAME_LOWER>_event_attr_<request name>_<field name>,
 *		"request=<request value>"
 *		"starting_index=<starting index type>"
 *		"counter_info_version=CURRENT_COUNTER_INFO_VERSION"
 *		"length=<f_size>"
 *		"offset=<f_offset>")
 *
 *	TODO: counter_info_version may need to vary, we should interperate the
 *	value to some extent
 */
#define EVENT_ATTR_NAME__(name, r_name, c_name) \
	name ## _event_attr_ ## r_name ## _ ## c_name
#define EVENT_ATTR_NAME_(name, r_name, c_name) \
	EVENT_ATTR_NAME__(name, r_name, c_name)
#define EVENT_ATTR_NAME(r_name, c_name) \
	EVENT_ATTR_NAME_(NAME_LOWER, r_name, c_name)

#include "_clear.h"
#define __field_(r_name, r_value, r_idx_1, f_offset, f_size, f_name)
#define __array_(r_name, r_value, r_idx_1, a_offset, a_size, a_name)
#define __count_(r_name, r_value, r_idx_1, c_offset, c_size, c_name)	\
PMU_EVENT_ATTR_STRING(							\
		CAT3(r_name, _, c_name),				\
		EVENT_ATTR_NAME(r_name, c_name),			\
		"request=" __stringify(r_value) ","			\
		r_idx_1 ","						\
		"counter_info_version="					\
			__stringify(COUNTER_INFO_VERSION_CURRENT) ","	\
		"length=" #c_size ","					\
		"offset=" #c_offset)
#define REQUEST_(r_name, r_value, r_idx_1, r_fields)			\
	r_fields

#include REQUEST_FILE

/*
 * Define event attribute array
 * static struct attribute *hv_gpci_event_attrs[] = {
 *	&<NAME_LOWER>_event_attr_<request name>_<field name>.attr,
 * };
 */
#include "_clear.h"
#define __field_(r_name, r_value, r_idx_1, f_offset, f_size, f_name)
#define __count_(r_name, r_value, r_idx_1, c_offset, c_size, c_name)	\
	&EVENT_ATTR_NAME(r_name, c_name).attr.attr,
#define __array_(r_name, r_value, r_idx_1, a_offset, a_size, a_name)
#define REQUEST_(r_name, r_value, r_idx_1, r_fields)			\
	r_fields

static __maybe_unused struct attribute *hv_gpci_event_attrs[] = {
#include REQUEST_FILE
	NULL
};

/* cleanup */
#include "_clear.h"
#undef EVENT_ATTR_NAME
#undef EVENT_ATTR_NAME_
#undef BIT_NAME
#undef BIT_NAME_
#undef STRUCT_NAME
#undef REQUEST_VALUE
#undef REQUEST_VALUE_

#endif
