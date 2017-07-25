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
/* host->guest commands pending, should be accessed under FIFO lock only */
#define HGSMIHOSTFLAGS_COMMANDS_PENDING    0x01u
/* IRQ is fired, should be accessed under VGAState::lock only  */
#define HGSMIHOSTFLAGS_IRQ                 0x02u
/* vsync interrupt flag, should be accessed under VGAState::lock only */
#define HGSMIHOSTFLAGS_VSYNC               0x10u
/** monitor hotplug flag, should be accessed under VGAState::lock only */
#define HGSMIHOSTFLAGS_HOTPLUG             0x20u
/**
 * Cursor capability state change flag, should be accessed under
 * VGAState::lock only. @see vbva_conf32.
 */
#define HGSMIHOSTFLAGS_CURSOR_CAPABILITIES 0x40u

struct hgsmi_host_flags {
	/*
	 * Host flags can be accessed and modified in multiple threads
	 * concurrently, e.g. CrOpenGL HGCM and GUI threads when completing
	 * HGSMI 3D and Video Accel respectively, EMT thread when dealing with
	 * HGSMI command processing, etc.
	 * Besides settings/cleaning flags atomically, some flags have their
	 * own special sync restrictions, see comments for flags above.
	 */
	u32 host_flags;
	u32 reserved[3];
} __packed;

#endif
