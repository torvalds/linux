/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022 Benjamin Tissoires
 */

#ifndef __HID_BPF_HELPERS_H
#define __HID_BPF_HELPERS_H

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <linux/errno.h>

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
extern int bpf_wq_set_callback_impl(struct bpf_wq *wq,
		int (callback_fn)(void *map, int *key, void *value),
		unsigned int flags__k, void *aux__ign) __ksym;
#define bpf_wq_set_callback(wq, cb, flags) \
	bpf_wq_set_callback_impl(wq, cb, flags, NULL)

#define HID_MAX_DESCRIPTOR_SIZE	4096
#define HID_IGNORE_EVENT	-1

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

#endif /* __HID_BPF_HELPERS_H */
