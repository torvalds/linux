// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Benjamin Tissoires
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_HUION 0x256C
#define PID_KAMVAS_PRO_19 0x006B
#define NAME_KAMVAS_PRO_19 "HUION Huion Tablet_GT1902"

#define TEST_PREFIX "uhid test "

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_MULTITOUCH_WIN_8, VID_HUION, PID_KAMVAS_PRO_19),
);

bool prev_was_out_of_range;
bool in_eraser_mode;

/*
 * We need to amend the report descriptor for the following:
 * - the second button is reported through Secondary Tip Switch instead of Secondary Barrel Switch
 * - the third button is reported through Invert, and we need some room to report it.
 *
 */
static const __u8 fixed_rdesc[] = {
	0x05, 0x0d,                    // Usage Page (Digitizers)             0
	0x09, 0x02,                    // Usage (Pen)                         2
	0xa1, 0x01,                    // Collection (Application)            4
	0x85, 0x0a,                    //  Report ID (10)                     6
	0x09, 0x20,                    //  Usage (Stylus)                     8
	0xa1, 0x01,                    //  Collection (Application)           10
	0x09, 0x42,                    //   Usage (Tip Switch)                12
	0x09, 0x44,                    //   Usage (Barrel Switch)             14
	0x09, 0x5a,                    //   Usage (Secondary Barrel Switch)   16 /* changed from Secondary Tip Switch */
	0x09, 0x3c,                    //   Usage (Invert)                    18
	0x09, 0x45,                    //   Usage (Eraser)                    20
	0x15, 0x00,                    //   Logical Minimum (0)               22
	0x25, 0x01,                    //   Logical Maximum (1)               24
	0x75, 0x01,                    //   Report Size (1)                   26
	0x95, 0x05,                    //   Report Count (5)                  28 /* changed (was 5) */
	0x81, 0x02,                    //   Input (Data,Var,Abs)              30
	0x05, 0x09,                    //   Usage Page (Button)                  /* inserted */
	0x09, 0x4a,                    //   Usage (0x4a)                         /* inserted to be translated as input usage 0x149: BTN_STYLUS3 */
	0x95, 0x01,                    //   Report Count (1)                     /* inserted */
	0x81, 0x02,                    //   Input (Data,Var,Abs)                 /* inserted */
	0x05, 0x0d,                    //   Usage Page (Digitizers)              /* inserted */
	0x09, 0x32,                    //   Usage (In Range)                  32
	0x75, 0x01,                    //   Report Size (1)                   34
	0x95, 0x01,                    //   Report Count (1)                  36
	0x81, 0x02,                    //   Input (Data,Var,Abs)              38
	0x81, 0x03,                    //   Input (Cnst,Var,Abs)              40
	0x05, 0x01,                    //   Usage Page (Generic Desktop)      42
	0x09, 0x30,                    //   Usage (X)                         44
	0x09, 0x31,                    //   Usage (Y)                         46
	0x55, 0x0d,                    //   Unit Exponent (-3)                48
	0x65, 0x33,                    //   Unit (EnglishLinear: in³)         50
	0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           52
	0x35, 0x00,                    //   Physical Minimum (0)              55
	0x46, 0x00, 0x08,              //   Physical Maximum (2048)           57
	0x75, 0x10,                    //   Report Size (16)                  60
	0x95, 0x02,                    //   Report Count (2)                  62
	0x81, 0x02,                    //   Input (Data,Var,Abs)              64
	0x05, 0x0d,                    //   Usage Page (Digitizers)           66
	0x09, 0x30,                    //   Usage (Tip Pressure)              68
	0x26, 0xff, 0x3f,              //   Logical Maximum (16383)           70
	0x75, 0x10,                    //   Report Size (16)                  73
	0x95, 0x01,                    //   Report Count (1)                  75
	0x81, 0x02,                    //   Input (Data,Var,Abs)              77
	0x09, 0x3d,                    //   Usage (X Tilt)                    79
	0x09, 0x3e,                    //   Usage (Y Tilt)                    81
	0x15, 0xa6,                    //   Logical Minimum (-90)             83
	0x25, 0x5a,                    //   Logical Maximum (90)              85
	0x75, 0x08,                    //   Report Size (8)                   87
	0x95, 0x02,                    //   Report Count (2)                  89
	0x81, 0x02,                    //   Input (Data,Var,Abs)              91
	0xc0,                          //  End Collection                     93
	0xc0,                          // End Collection                      94
	0x05, 0x0d,                    // Usage Page (Digitizers)             95
	0x09, 0x04,                    // Usage (Touch Screen)                97
	0xa1, 0x01,                    // Collection (Application)            99
	0x85, 0x04,                    //  Report ID (4)                      101
	0x09, 0x22,                    //  Usage (Finger)                     103
	0xa1, 0x02,                    //  Collection (Logical)               105
	0x05, 0x0d,                    //   Usage Page (Digitizers)           107
	0x95, 0x01,                    //   Report Count (1)                  109
	0x75, 0x06,                    //   Report Size (6)                   111
	0x09, 0x51,                    //   Usage (Contact Id)                113
	0x15, 0x00,                    //   Logical Minimum (0)               115
	0x25, 0x3f,                    //   Logical Maximum (63)              117
	0x81, 0x02,                    //   Input (Data,Var,Abs)              119
	0x09, 0x42,                    //   Usage (Tip Switch)                121
	0x25, 0x01,                    //   Logical Maximum (1)               123
	0x75, 0x01,                    //   Report Size (1)                   125
	0x95, 0x01,                    //   Report Count (1)                  127
	0x81, 0x02,                    //   Input (Data,Var,Abs)              129
	0x75, 0x01,                    //   Report Size (1)                   131
	0x95, 0x01,                    //   Report Count (1)                  133
	0x81, 0x03,                    //   Input (Cnst,Var,Abs)              135
	0x05, 0x01,                    //   Usage Page (Generic Desktop)      137
	0x75, 0x10,                    //   Report Size (16)                  139
	0x55, 0x0e,                    //   Unit Exponent (-2)                141
	0x65, 0x11,                    //   Unit (SILinear: cm)               143
	0x09, 0x30,                    //   Usage (X)                         145
	0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           147
	0x35, 0x00,                    //   Physical Minimum (0)              150
	0x46, 0x15, 0x0c,              //   Physical Maximum (3093)           152
	0x81, 0x42,                    //   Input (Data,Var,Abs,Null)         155
	0x09, 0x31,                    //   Usage (Y)                         157
	0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           159
	0x46, 0xcb, 0x06,              //   Physical Maximum (1739)           162
	0x81, 0x42,                    //   Input (Data,Var,Abs,Null)         165
	0x05, 0x0d,                    //   Usage Page (Digitizers)           167
	0x09, 0x30,                    //   Usage (Tip Pressure)              169
	0x26, 0xff, 0x1f,              //   Logical Maximum (8191)            171
	0x75, 0x10,                    //   Report Size (16)                  174
	0x95, 0x01,                    //   Report Count (1)                  176
	0x81, 0x02,                    //   Input (Data,Var,Abs)              178
	0xc0,                          //  End Collection                     180
	0x05, 0x0d,                    //  Usage Page (Digitizers)            181
	0x09, 0x22,                    //  Usage (Finger)                     183
	0xa1, 0x02,                    //  Collection (Logical)               185
	0x05, 0x0d,                    //   Usage Page (Digitizers)           187
	0x95, 0x01,                    //   Report Count (1)                  189
	0x75, 0x06,                    //   Report Size (6)                   191
	0x09, 0x51,                    //   Usage (Contact Id)                193
	0x15, 0x00,                    //   Logical Minimum (0)               195
	0x25, 0x3f,                    //   Logical Maximum (63)              197
	0x81, 0x02,                    //   Input (Data,Var,Abs)              199
	0x09, 0x42,                    //   Usage (Tip Switch)                201
	0x25, 0x01,                    //   Logical Maximum (1)               203
	0x75, 0x01,                    //   Report Size (1)                   205
	0x95, 0x01,                    //   Report Count (1)                  207
	0x81, 0x02,                    //   Input (Data,Var,Abs)              209
	0x75, 0x01,                    //   Report Size (1)                   211
	0x95, 0x01,                    //   Report Count (1)                  213
	0x81, 0x03,                    //   Input (Cnst,Var,Abs)              215
	0x05, 0x01,                    //   Usage Page (Generic Desktop)      217
	0x75, 0x10,                    //   Report Size (16)                  219
	0x55, 0x0e,                    //   Unit Exponent (-2)                221
	0x65, 0x11,                    //   Unit (SILinear: cm)               223
	0x09, 0x30,                    //   Usage (X)                         225
	0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           227
	0x35, 0x00,                    //   Physical Minimum (0)              230
	0x46, 0x15, 0x0c,              //   Physical Maximum (3093)           232
	0x81, 0x42,                    //   Input (Data,Var,Abs,Null)         235
	0x09, 0x31,                    //   Usage (Y)                         237
	0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           239
	0x46, 0xcb, 0x06,              //   Physical Maximum (1739)           242
	0x81, 0x42,                    //   Input (Data,Var,Abs,Null)         245
	0x05, 0x0d,                    //   Usage Page (Digitizers)           247
	0x09, 0x30,                    //   Usage (Tip Pressure)              249
	0x26, 0xff, 0x1f,              //   Logical Maximum (8191)            251
	0x75, 0x10,                    //   Report Size (16)                  254
	0x95, 0x01,                    //   Report Count (1)                  256
	0x81, 0x02,                    //   Input (Data,Var,Abs)              258
	0xc0,                          //  End Collection                     260
	0x05, 0x0d,                    //  Usage Page (Digitizers)            261
	0x09, 0x56,                    //  Usage (Scan Time)                  263
	0x55, 0x00,                    //  Unit Exponent (0)                  265
	0x65, 0x00,                    //  Unit (None)                        267
	0x27, 0xff, 0xff, 0xff, 0x7f,  //  Logical Maximum (2147483647)       269
	0x95, 0x01,                    //  Report Count (1)                   274
	0x75, 0x20,                    //  Report Size (32)                   276
	0x81, 0x02,                    //  Input (Data,Var,Abs)               278
	0x09, 0x54,                    //  Usage (Contact Count)              280
	0x25, 0x7f,                    //  Logical Maximum (127)              282
	0x95, 0x01,                    //  Report Count (1)                   284
	0x75, 0x08,                    //  Report Size (8)                    286
	0x81, 0x02,                    //  Input (Data,Var,Abs)               288
	0x75, 0x08,                    //  Report Size (8)                    290
	0x95, 0x08,                    //  Report Count (8)                   292
	0x81, 0x03,                    //  Input (Cnst,Var,Abs)               294
	0x85, 0x05,                    //  Report ID (5)                      296
	0x09, 0x55,                    //  Usage (Contact Max)                298
	0x25, 0x0a,                    //  Logical Maximum (10)               300
	0x75, 0x08,                    //  Report Size (8)                    302
	0x95, 0x01,                    //  Report Count (1)                   304
	0xb1, 0x02,                    //  Feature (Data,Var,Abs)             306
	0x06, 0x00, 0xff,              //  Usage Page (Vendor Defined Page 1) 308
	0x09, 0xc5,                    //  Usage (Vendor Usage 0xc5)          311
	0x85, 0x06,                    //  Report ID (6)                      313
	0x15, 0x00,                    //  Logical Minimum (0)                315
	0x26, 0xff, 0x00,              //  Logical Maximum (255)              317
	0x75, 0x08,                    //  Report Size (8)                    320
	0x96, 0x00, 0x01,              //  Report Count (256)                 322
	0xb1, 0x02,                    //  Feature (Data,Var,Abs)             325
	0xc0,                          // End Collection                      327
};

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(hid_fix_rdesc_huion_kamvas_pro_19, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, HID_MAX_DESCRIPTOR_SIZE /* size */);

	if (!data)
		return 0; /* EPERM check */

	__builtin_memcpy(data, fixed_rdesc, sizeof(fixed_rdesc));

	return sizeof(fixed_rdesc);
}

/*
 * This tablet reports the 3rd button through invert, but this conflict
 * with the normal eraser mode.
 * Fortunately, before entering eraser mode, (so Invert = 1),
 * the tablet always sends an out-of-proximity event.
 * So we can detect that single event and:
 * - if there was none but the invert bit was toggled: this is the
 *   third button
 * - if there was this out-of-proximity event, we are entering
 *   eraser mode, and we will until the next out-of-proximity.
 */
SEC(HID_BPF_DEVICE_EVENT)
int BPF_PROG(kamvas_pro_19_fix_3rd_button, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 10 /* size */);

	if (!data)
		return 0; /* EPERM check */

	if (data[0] != 0x0a) /* not the pen report ID */
		return 0;

	/* stylus is out of range */
	if (!(data[1] & 0x40)) {
		prev_was_out_of_range = true;
		in_eraser_mode = false;
		return 0;
	}

	/* going into eraser mode (Invert = 1) only happens after an
	 * out of range event
	 */
	if (prev_was_out_of_range && (data[1] & 0x18))
		in_eraser_mode = true;

	/* eraser mode works fine */
	if (in_eraser_mode)
		return 0;

	/* copy the Invert bit reported for the 3rd button in bit 7 */
	if (data[1] & 0x08)
		data[1] |= 0x20;

	/* clear Invert bit now that it was copied */
	data[1] &= 0xf7;

	prev_was_out_of_range = false;

	return 0;
}

HID_BPF_OPS(huion_Kamvas_pro_19) = {
	.hid_rdesc_fixup = (void *)hid_fix_rdesc_huion_kamvas_pro_19,
	.hid_device_event = (void *)kamvas_pro_19_fix_3rd_button,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	ctx->retval = ctx->rdesc_size != 328;
	if (ctx->retval)
		ctx->retval = -EINVAL;

	/* ensure the kernel isn't fixed already */
	if (ctx->rdesc[17] != 0x43) /* Secondary Tip Switch */
		ctx->retval = -EINVAL;

	struct hid_bpf_ctx *hctx = hid_bpf_allocate_context(ctx->hid);

	if (!hctx) {
		return ctx->retval = -EINVAL;
		return 0;
	}

	const char *name = hctx->hid->name;

	/* strip out TEST_PREFIX */
	if (!__builtin_memcmp(name, TEST_PREFIX, sizeof(TEST_PREFIX) - 1))
		name += sizeof(TEST_PREFIX) - 1;

	if (__builtin_memcmp(name, NAME_KAMVAS_PRO_19, sizeof(NAME_KAMVAS_PRO_19)))
		ctx->retval = -EINVAL;

	hid_bpf_release_context(hctx);

	return 0;
}

char _license[] SEC("license") = "GPL";
