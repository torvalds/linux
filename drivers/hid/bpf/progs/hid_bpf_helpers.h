/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022 Benjamin Tissoires
 */

#ifndef __HID_BPF_HELPERS_H
#define __HID_BPF_HELPERS_H

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <linux/errno.h>
#include "hid_report_descriptor_helpers.h"

/* Compiler attributes */
#ifndef __packed
#define __packed __attribute__((packed))
#endif

#ifndef __maybe_unused
#define __maybe_unused __attribute__((__unused__))
#endif

extern __u8 *hid_bpf_get_data(struct hid_bpf_ctx *ctx,
			      unsigned int offset,
			      const size_t __sz) __ksym;
extern struct hid_bpf_ctx *hid_bpf_allocate_context(unsigned int hid_id) __ksym;
extern void hid_bpf_release_context(struct hid_bpf_ctx *ctx) __ksym;
extern int hid_bpf_hw_request(struct hid_bpf_ctx *ctx,
			      __u8 *data,
			      size_t buf__sz,
			      enum hid_report_type type,
			      enum hid_class_request reqtype) __ksym;
extern int hid_bpf_hw_output_report(struct hid_bpf_ctx *ctx,
				    __u8 *buf, size_t buf__sz) __weak __ksym;
extern int hid_bpf_input_report(struct hid_bpf_ctx *ctx,
				enum hid_report_type type,
				__u8 *data,
				size_t buf__sz) __weak __ksym;
extern int hid_bpf_try_input_report(struct hid_bpf_ctx *ctx,
				    enum hid_report_type type,
				    __u8 *data,
				    size_t buf__sz) __weak __ksym;

/* bpf_wq implementation */
extern int bpf_wq_init(struct bpf_wq *wq, void *p__map, unsigned int flags) __weak __ksym;
extern int bpf_wq_start(struct bpf_wq *wq, unsigned int flags) __weak __ksym;
extern int bpf_wq_set_callback(struct bpf_wq *wq,
		int (*callback_fn)(void *, int *, void *),
		unsigned int flags) __weak __ksym;

#define HID_MAX_DESCRIPTOR_SIZE	4096
#define HID_IGNORE_EVENT	-1

/**
 * Use: _cleanup_(somefunction) struct foo *bar;
 */
#define _cleanup_(_x) __attribute__((cleanup(_x)))

/**
 * Use: _release_(foo) *bar;
 *
 * This requires foo_releasep() to be present, use DEFINE_RELEASE_CLEANUP_FUNC.
 */
#define _release_(_type) struct _type __attribute__((cleanup(_type##_releasep)))

/**
 * Define a cleanup function for the struct type foo with a matching
 * foo_release(). Use:
 * DEFINE_RELEASE_CLEANUP_FUNC(foo)
 * _unref_(foo) struct foo *bar;
 */
#define DEFINE_RELEASE_CLEANUP_FUNC(_type)				\
	static inline void _type##_releasep(struct _type **_p) {	\
		if (*_p)						\
			_type##_release(*_p);				\
	}								\
	struct __useless_struct_to_allow_trailing_semicolon__

/* for being able to have a cleanup function */
#define hid_bpf_ctx_release hid_bpf_release_context
DEFINE_RELEASE_CLEANUP_FUNC(hid_bpf_ctx);

/*
 * Kernel-style guard macros adapted for BPF
 * Based on include/linux/cleanup.h from the Linux kernel
 *
 * These provide automatic lock/unlock using __attribute__((cleanup))
 * similar to how _release_() works for contexts.
 */

/**
 * DEFINE_GUARD(name, type, lock, unlock):
 *	Define a guard for automatic lock/unlock using the same pattern as _release_()
 *	@name: identifier for the guard (e.g., bpf_spin)
 *	@type: lock variable type (e.g., struct bpf_spin_lock)
 *	@lock: lock function name (e.g., bpf_spin_lock)
 *	@unlock: unlock function name (e.g., bpf_spin_unlock)
 *
 * guard(name):
 *	Declare and lock in one statement - lock held until end of scope
 *
 * Example:
 *	DEFINE_GUARD(bpf_spin, struct bpf_spin_lock, bpf_spin_lock, bpf_spin_unlock)
 *
 *	void foo(struct bpf_spin_lock *lock) {
 *		guard(bpf_spin)(lock);
 *		// lock held until end of scope
 *	}
 */

/* Guard helper struct - stores lock pointer for cleanup */
#define DEFINE_GUARD(_name, _type, _lock, _unlock)			\
struct _name##_guard {							\
	_type *lock;							\
};									\
static inline void _name##_guard_cleanup(struct _name##_guard *g) {	\
	if (g && g->lock) 						\
		_unlock(g->lock);					\
}									\
static inline struct _name##_guard _name##_guard_init(_type *l) {	\
	if (l)								\
		_lock(l);						\
	return (struct _name##_guard){.lock = l};			\
}									\
struct __useless_struct_to_allow_trailing_semicolon__

#define guard(_name) \
	struct _name##_guard COMBINE(guard, __LINE__) __attribute__((cleanup(_name##_guard_cleanup))) = \
		_name##_guard_init

/* Define BPF spinlock guard */
DEFINE_GUARD(bpf_spin, struct bpf_spin_lock, bpf_spin_lock, bpf_spin_unlock);

/* extracted from <linux/input.h> */
#define BUS_ANY			0x00
#define BUS_PCI			0x01
#define BUS_ISAPNP		0x02
#define BUS_USB			0x03
#define BUS_HIL			0x04
#define BUS_BLUETOOTH		0x05
#define BUS_VIRTUAL		0x06
#define BUS_ISA			0x10
#define BUS_I8042		0x11
#define BUS_XTKBD		0x12
#define BUS_RS232		0x13
#define BUS_GAMEPORT		0x14
#define BUS_PARPORT		0x15
#define BUS_AMIGA		0x16
#define BUS_ADB			0x17
#define BUS_I2C			0x18
#define BUS_HOST		0x19
#define BUS_GSC			0x1A
#define BUS_ATARI		0x1B
#define BUS_SPI			0x1C
#define BUS_RMI			0x1D
#define BUS_CEC			0x1E
#define BUS_INTEL_ISHTP		0x1F
#define BUS_AMD_SFH		0x20

/* extracted from <linux/hid.h> */
#define HID_GROUP_ANY				0x0000
#define HID_GROUP_GENERIC			0x0001
#define HID_GROUP_MULTITOUCH			0x0002
#define HID_GROUP_SENSOR_HUB			0x0003
#define HID_GROUP_MULTITOUCH_WIN_8		0x0004
#define HID_GROUP_RMI				0x0100
#define HID_GROUP_WACOM				0x0101
#define HID_GROUP_LOGITECH_DJ_DEVICE		0x0102
#define HID_GROUP_STEAM				0x0103
#define HID_GROUP_LOGITECH_27MHZ_DEVICE		0x0104
#define HID_GROUP_VIVALDI			0x0105

/* include/linux/mod_devicetable.h defines as (~0), but that gives us negative size arrays */
#define HID_VID_ANY				0x0000
#define HID_PID_ANY				0x0000

#define BIT(n) (1UL << (n))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Helper macro to convert (foo, __LINE__)  into foo134 so we can use __LINE__ for
 * field/variable names
 */
#define COMBINE1(X, Y) X ## Y
#define COMBINE(X, Y) COMBINE1(X, Y)

/* Macro magic:
 * __uint(foo, 123) creates a int (*foo)[1234]
 *
 * We use that macro to declare an anonymous struct with several
 * fields, each is the declaration of an pointer to an array of size
 * bus/group/vid/pid. (Because it's a pointer to such an array, actual storage
 * would be sizeof(pointer) rather than sizeof(array). Not that we ever
 * instantiate it anyway).
 *
 * This is only used for BTF introspection, we can later check "what size
 * is the bus array" in the introspection data and thus extract the bus ID
 * again.
 *
 * And we use the __LINE__ to give each of our structs a unique name so the
 * BPF program writer doesn't have to.
 *
 * $ bpftool btf dump file target/bpf/HP_Elite_Presenter.bpf.o
 * shows the inspection data, start by searching for .hid_bpf_config
 * and working backwards from that (each entry references the type_id of the
 * content).
 */

#define HID_DEVICE(b, g, ven, prod)	\
	struct {			\
		__uint(name, 0);	\
		__uint(bus, (b));	\
		__uint(group, (g));	\
		__uint(vid, (ven));	\
		__uint(pid, (prod));	\
	} COMBINE(_entry, __LINE__)

/* Macro magic below is to make HID_BPF_CONFIG() look like a function call that
 * we can pass multiple HID_DEVICE() invocations in.
 *
 * For up to 16 arguments, HID_BPF_CONFIG(one, two) resolves to
 *
 * union {
 *    HID_DEVICE(...);
 *    HID_DEVICE(...);
 * } _device_ids SEC(".hid_bpf_config")
 *
 */

/* Returns the number of macro arguments, this expands
 * NARGS(a, b, c) to NTH_ARG(a, b, c, 15, 14, 13, .... 4, 3, 2, 1).
 * NTH_ARG always returns the 16th argument which in our case is 3.
 *
 * If we want more than 16 values _COUNTDOWN and _NTH_ARG both need to be
 * updated.
 */
#define _NARGS(...)  _NARGS1(__VA_ARGS__, _COUNTDOWN)
#define _NARGS1(...) _NTH_ARG(__VA_ARGS__)

/* Add to this if we need more than 16 args */
#define _COUNTDOWN \
	15, 14, 13, 12, 11, 10, 9, 8,  \
	 7,  6,  5,  4,  3,  2, 1, 0

/* Return the 16 argument passed in. See _NARGS above for usage. Note this is
 * 1-indexed.
 */
#define _NTH_ARG( \
	_1,  _2,  _3,  _4,  _5,  _6,  _7, _8, \
	_9, _10, _11, _12, _13, _14, _15,\
	 N, ...) N

/* Turns EXPAND(_ARG, a, b, c) into _ARG3(a, b, c) */
#define _EXPAND(func, ...) COMBINE(func, _NARGS(__VA_ARGS__)) (__VA_ARGS__)

/* And now define all the ARG macros for each number of args we want to accept */
#define _ARG1(_1)                                                         _1;
#define _ARG2(_1, _2)                                                     _1; _2;
#define _ARG3(_1, _2, _3)                                                 _1; _2; _3;
#define _ARG4(_1, _2, _3, _4)                                             _1; _2; _3; _4;
#define _ARG5(_1, _2, _3, _4, _5)                                         _1; _2; _3; _4; _5;
#define _ARG6(_1, _2, _3, _4, _5, _6)                                     _1; _2; _3; _4; _5; _6;
#define _ARG7(_1, _2, _3, _4, _5, _6, _7)                                 _1; _2; _3; _4; _5; _6; _7;
#define _ARG8(_1, _2, _3, _4, _5, _6, _7, _8)                             _1; _2; _3; _4; _5; _6; _7; _8;
#define _ARG9(_1, _2, _3, _4, _5, _6, _7, _8, _9)                         _1; _2; _3; _4; _5; _6; _7; _8; _9;
#define _ARG10(_1, _2, _3, _4, _5, _6, _7, _8, _9, _a)                     _1; _2; _3; _4; _5; _6; _7; _8; _9; _a;
#define _ARG11(_1, _2, _3, _4, _5, _6, _7, _8, _9, _a, _b)                 _1; _2; _3; _4; _5; _6; _7; _8; _9; _a; _b;
#define _ARG12(_1, _2, _3, _4, _5, _6, _7, _8, _9, _a, _b, _c)             _1; _2; _3; _4; _5; _6; _7; _8; _9; _a; _b; _c;
#define _ARG13(_1, _2, _3, _4, _5, _6, _7, _8, _9, _a, _b, _c, _d)         _1; _2; _3; _4; _5; _6; _7; _8; _9; _a; _b; _c; _d;
#define _ARG14(_1, _2, _3, _4, _5, _6, _7, _8, _9, _a, _b, _c, _d, _e)     _1; _2; _3; _4; _5; _6; _7; _8; _9; _a; _b; _c; _d; _e;
#define _ARG15(_1, _2, _3, _4, _5, _6, _7, _8, _9, _a, _b, _c, _d, _e, _f) _1; _2; _3; _4; _5; _6; _7; _8; _9; _a; _b; _c; _d; _e; _f;


#define HID_BPF_CONFIG(...)  union { \
	_EXPAND(_ARG, __VA_ARGS__) \
} _device_ids SEC(".hid_bpf_config")


/* Equivalency macros for bpf_htons and friends which are
 * Big Endian only - HID needs little endian so these are the
 * corresponding macros for that. See bpf/bpf_endian.h
 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
# define __hid_bpf_le16_to_cpu(x)		(x)
# define __hid_bpf_le32_to_cpu(x)		(x)
# define __hid_bpf_le64_to_cpu(x)		(x)
# define __hid_bpf_cpu_to_le16(x)		(x)
# define __hid_bpf_cpu_to_le32(x)		(x)
# define __hid_bpf_cpu_to_le64(x)		(x)
# define __hid_bpf_constant_le16_to_cpu(x)	(x)
# define __hid_bpf_constant_le32_to_cpu(x)	(x)
# define __hid_bpf_constant_le64_to_cpu(x)	(x)
# define __hid_bpf_constant_cpu_to_le16(x)	(x)
# define __hid_bpf_constant_cpu_to_le32(x)	(x)
# define __hid_bpf_constant_cpu_to_le64(x)	(x)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define __hid_bpf_le16_to_cpu(x)		__builtin_bswap16(x)
# define __hid_bpf_le32_to_cpu(x)		__builtin_bswap32(x)
# define __hid_bpf_le64_to_cpu(x)		__builtin_bswap64(x)
# define __hid_bpf_cpu_to_le16(x)		__builtin_bswap16(x)
# define __hid_bpf_cpu_to_le32(x)		__builtin_bswap32(x)
# define __hid_bpf_cpu_to_le64(x)		__builtin_bswap64(x)
# define __hid_bpf_constant_le16_to_cpu(x)	__bpf_swab16(x)
# define __hid_bpf_constant_le32_to_cpu(x)	__bpf_swab32(x)
# define __hid_bpf_constant_le64_to_cpu(x)	__bpf_swab64(x)
# define __hid_bpf_constant_cpu_to_le16(x)	__bpf_swab16(x)
# define __hid_bpf_constant_cpu_to_le32(x)	__bpf_swab32(x)
# define __hid_bpf_constant_cpu_to_le64(x)	__bpf_swab64(x)
#else
# error "Invalid __BYTE_ORDER__"
#endif

#define hid_bpf_le16_to_cpu(x)				\
	(__builtin_constant_p(x) ?			\
	 __hid_bpf_constant_le16_to_cpu(x) : __hid_bpf_le16_to_cpu(x))

#define hid_bpf_le32_to_cpu(x)				\
	(__builtin_constant_p(x) ?			\
	 __hid_bpf_constant_le32_to_cpu(x) : __hid_bpf_le32_to_cpu(x))

#define hid_bpf_le64_to_cpu(x)				\
	(__builtin_constant_p(x) ?			\
	 __hid_bpf_constant_le64_to_cpu(x) : __hid_bpf_le64_to_cpu(x))

#define hid_bpf_cpu_to_le16(x)				\
	(__builtin_constant_p(x) ?			\
	 __hid_bpf_constant_cpu_to_le16(x) : __hid_bpf_cpu_to_le16(x))

#define hid_bpf_cpu_to_le32(x)				\
	(__builtin_constant_p(x) ?			\
	 __hid_bpf_constant_cpu_to_le32(x) : __hid_bpf_cpu_to_le32(x))

#define hid_bpf_cpu_to_le64(x)				\
	(__builtin_constant_p(x) ?			\
	 __hid_bpf_constant_cpu_to_le64(x) : __hid_bpf_cpu_to_le64(x))

#define hid_bpf_be16_to_cpu(x)	bpf_ntohs(x)
#define hid_bpf_be32_to_cpu(x)	bpf_ntohl(x)
#define hid_bpf_be64_to_cpu(x)	bpf_be64_to_cpu(x)
#define hid_bpf_cpu_to_be16(x)	bpf_htons(x)
#define hid_bpf_cpu_to_be32(x)	bpf_htonl(x)
#define hid_bpf_cpu_to_be64(x)	bpf_cpu_to_be64(x)

/*
 * The following macros are helpers for exporting udev properties:
 *
 * EXPORT_UDEV_PROP(name, len) generates:
 *  - a map with a single element UDEV_PROP_##name, of size len
 *  - a const global declaration of that len: SIZEOF_##name
 *
 * udev_prop_ptr(name) retrieves the data pointer behind the map.
 *
 * UDEV_PROP_SPRINTF(name, fmt, ...) writes data into the udev property.
 *
 *  Can be used as such:
 *  EXPORT_UDEV_PROP(HID_FOO, 32);
 *
 *  SEC("syscall")
 *  int probe(struct hid_bpf_probe_args *ctx)
 *  {
 *    const char *foo = "foo";
 *    UDEV_PROP_SPRINTF(HID_FOO, "%s", foo);
 *
 *    return 0;
 *  }
 */
#define EXPORT_UDEV_PROP(name, len) \
	const __u32 SIZEOF_##name = len; \
	struct COMBINE(udev_prop, __LINE__) { \
		__uint(type, BPF_MAP_TYPE_ARRAY); \
		__uint(max_entries, 1); \
		__type(key, __u32); \
		__type(value, __u8[len]); \
	} UDEV_PROP_##name SEC(".maps");

#define udev_prop_ptr(name) \
	bpf_map_lookup_elem(&UDEV_PROP_##name, &(__u32){0})

#define UDEV_PROP_SPRINTF(name, fmt, ...) \
	BPF_SNPRINTF(udev_prop_ptr(name), SIZEOF_##name, fmt, ##__VA_ARGS__)

static inline __maybe_unused __u16 field_start_byte(struct hid_rdesc_field *field)
{
	return field->bits_start / 8;
}

static inline __maybe_unused __u16 field_end_byte(struct hid_rdesc_field *field)
{
	if (!field->bits_end)
		return 0;

	return (__u16)(field->bits_end - 1) / 8;
}

static __maybe_unused __u32 extract_bits(__u8 *buffer, const size_t size, struct hid_rdesc_field *field)
{
	__s32 nbits = field->bits_end - field->bits_start;
	__u32 start = field_start_byte(field);
	__u32 end = field_end_byte(field);
	__u8 base_shift = field->bits_start % 8;

	if (nbits <= 0 || nbits > 32 || start >= size || end >= size)
		return 0;

	/* Fast path for byte-aligned standard-sized reads */
	if (base_shift == 0) {
		/* 8-bit aligned read */
		if (nbits == 8 && start < size)
			return buffer[start];

		/* 16-bit aligned read - use separate variables for verifier */
		if (nbits == 16) {
			__u32 off0 = start;
			__u32 off1 = start + 1;

			if (off0 < size && off1 < size) {
				return buffer[off0] |
				       ((__u32)buffer[off1] << 8);
			}
		}

		/* 32-bit aligned read - use separate variables for verifier */
		if (nbits == 32) {
			__u32 off0 = start;
			__u32 off1 = start + 1;
			__u32 off2 = start + 2;
			__u32 off3 = start + 3;

			if (off0 < size && off1 < size &&
			    off2 < size && off3 < size) {
				return buffer[off0] |
				       ((__u32)buffer[off1] << 8) |
				       ((__u32)buffer[off2] << 16) |
				       ((__u32)buffer[off3] << 24);
			}
		}
	}

	/* General case: bit manipulation for unaligned or non-standard sizes */
	int mask = 0xffffffff >> (32 - nbits);
	__u64 value = 0;
	__u32 i;

	bpf_for (i, start, end + 1) {
		value |= (__u64)buffer[i] << ((i - start) * 8);
	}

	return (value >> base_shift) & mask;
}

#define EXTRACT_BITS(buffer, field) extract_bits(buffer, sizeof(buffer), field)

/* Base macro for iterating over HID arrays with bounds checking.
 * Follows the bpf_for pattern from libbpf.
 */
#define __hid_bpf_for_each_array(array, num_elements, max_elements, var)              \
	for (                                                                          \
		/* initialize and define destructor */                                 \
		struct bpf_iter_num ___it __attribute__((aligned(8),                   \
							 cleanup(bpf_iter_num_destroy))),      \
		/* ___p pointer is necessary to call bpf_iter_num_new() *once* */      \
				    *___p __attribute__((unused)) = (                  \
			/* always initialize iterator; if bounds fail, iterate 0 times */ \
			bpf_iter_num_new(&___it, 0,                                    \
					 (num_elements) > (max_elements) ?             \
						0 : (num_elements)),                   \
			/* workaround for Clang bug */                                 \
			(void)bpf_iter_num_destroy, (void *)0);                        \
		({                                                                     \
			/* iteration step */                                           \
			int *___t = bpf_iter_num_next(&___it);                         \
			int ___i;                                                      \
			/* termination and bounds check, assign var */                 \
			(___t && (___i = *___t, ___i >= 0 && ___i < (num_elements)) && \
			 ((num_elements) <= (max_elements)) &&                         \
			 (var = &(array)[___i], 1));                                   \
		});                                                                    \
	)

/* Iterate over input reports in a descriptor */
#define hid_bpf_for_each_input_report(descriptor, report_var) \
	__hid_bpf_for_each_array((descriptor)->input_reports, \
				 (descriptor)->num_input_reports, \
				 HID_MAX_REPORTS, report_var)

/* Iterate over feature reports in a descriptor */
#define hid_bpf_for_each_feature_report(descriptor, report_var) \
	__hid_bpf_for_each_array((descriptor)->feature_reports, \
				 (descriptor)->num_feature_reports, \
				 HID_MAX_REPORTS, report_var)

/* Iterate over output reports in a descriptor */
#define hid_bpf_for_each_output_report(descriptor, report_var) \
	__hid_bpf_for_each_array((descriptor)->output_reports, \
				 (descriptor)->num_output_reports, \
				 HID_MAX_REPORTS, report_var)

/* Iterate over fields in a report */
#define hid_bpf_for_each_field(report, field_var) \
	__hid_bpf_for_each_array((report)->fields, (report)->num_fields, \
				 HID_MAX_FIELDS, field_var)

/* Iterate over collections in a field */
#define hid_bpf_for_each_collection(field, collection_var) \
	__hid_bpf_for_each_array((field)->collections, (field)->num_collections, \
				 HID_MAX_COLLECTIONS, collection_var)

#endif /* __HID_BPF_HELPERS_H */
