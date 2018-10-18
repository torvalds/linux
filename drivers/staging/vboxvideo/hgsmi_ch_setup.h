/*
 * Copyright (C) 2006-2017 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __HGSMI_CH_SETUP_H__
#define __HGSMI_CH_SETUP_H__

/*
 * Tell the host the location of hgsmi_host_flags structure, where the host
 * can write information about pending buffers, etc, and which can be quickly
 * polled by the guest without a need to port IO.
 */
#define HGSMI_CC_HOST_FLAGS_LOCATION 0

struct hgsmi_buffer_location {
	u32 buf_location;
	u32 buf_len;
} __packed;

/* HGSMI setup and configuration data structures. */

#define HGSMIHOSTFLAGS_COMMANDS_PENDING    0x01u
#define HGSMIHOSTFLAGS_IRQ                 0x02u
#define HGSMIHOSTFLAGS_VSYNC               0x10u
#define HGSMIHOSTFLAGS_HOTPLUG             0x20u
#define HGSMIHOSTFLAGS_CURSOR_CAPABILITIES 0x40u

struct hgsmi_host_flags {
	u32 host_flags;
	u32 reserved[3];
} __packed;

#endif
