/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2007,2020 VMware, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * svga_escape.h --
 *
 *    Definitions for our own (vendor-specific) SVGA Escape commands.
 */



#ifndef _SVGA_ESCAPE_H_
#define _SVGA_ESCAPE_H_

#define SVGA_ESCAPE_NSID_VMWARE 0x00000000
#define SVGA_ESCAPE_NSID_DEVEL 0xFFFFFFFF

#define SVGA_ESCAPE_VMWARE_MAJOR_MASK 0xFFFF0000

#define SVGA_ESCAPE_VMWARE_HINT 0x00030000
#define SVGA_ESCAPE_VMWARE_HINT_FULLSCREEN 0x00030001

#pragma pack(push, 1)
typedef struct {
	uint32 command;
	uint32 fullscreen;
	struct {
		int32 x, y;
	} monitorPosition;
} SVGAEscapeHintFullscreen;
#pragma pack(pop)

#endif
