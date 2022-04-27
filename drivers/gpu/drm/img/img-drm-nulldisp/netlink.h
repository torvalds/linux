/*
 * @File
 * @Title       Nulldisp/Netlink interface definition
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef __NETLINK_H__
#define __NETLINK_H__

/* For multi-plane pixel formats */
#define NLPVRDPY_MAX_NUM_PLANES 3

enum nlpvrdpy_cmd {
	__NLPVRDPY_CMD_INVALID,
	NLPVRDPY_CMD_CONNECT,
	NLPVRDPY_CMD_CONNECTED,
	NLPVRDPY_CMD_DISCONNECT,
	NLPVRDPY_CMD_FLIP,
	NLPVRDPY_CMD_FLIPPED,
	NLPVRDPY_CMD_COPY,
	NLPVRDPY_CMD_COPIED,
	__NLPVRDPY_CMD_MAX
};
#define NLPVRDPY_CMD_MAX (__NLPVRDPY_CMD_MAX - 1)

enum nlpvrdpy_attr {
	__NLPVRDPY_ATTR_INVALID,
	NLPVRDPY_ATTR_NAME,
	NLPVRDPY_ATTR_MINOR,
	NLPVRDPY_ATTR_NUM_PLANES,
	NLPVRDPY_ATTR_WIDTH,
	NLPVRDPY_ATTR_HEIGHT,
	NLPVRDPY_ATTR_PIXFMT,
	NLPVRDPY_ATTR_YUV_CSC,
	NLPVRDPY_ATTR_YUV_BPP,
	NLPVRDPY_ATTR_PLANE0_ADDR,
	NLPVRDPY_ATTR_PLANE0_SIZE,
	NLPVRDPY_ATTR_PLANE0_OFFSET,
	NLPVRDPY_ATTR_PLANE0_PITCH,
	NLPVRDPY_ATTR_PLANE0_GEM_OBJ_NAME,
	NLPVRDPY_ATTR_PLANE1_ADDR,
	NLPVRDPY_ATTR_PLANE1_SIZE,
	NLPVRDPY_ATTR_PLANE1_OFFSET,
	NLPVRDPY_ATTR_PLANE1_PITCH,
	NLPVRDPY_ATTR_PLANE1_GEM_OBJ_NAME,
	NLPVRDPY_ATTR_PLANE2_ADDR,
	NLPVRDPY_ATTR_PLANE2_SIZE,
	NLPVRDPY_ATTR_PLANE2_OFFSET,
	NLPVRDPY_ATTR_PLANE2_PITCH,
	NLPVRDPY_ATTR_PLANE2_GEM_OBJ_NAME,
	NLPVRDPY_ATTR_FB_MODIFIER,
	NLPVRDPY_ATTR_NAMING_REQUIRED,
	NLPVRDPY_ATTR_PAD,
	__NLPVRDPY_ATTR_MAX
};
#define NLPVRDPY_ATTR_MAX  (__NLPVRDPY_ATTR_MAX - 1)

static struct nla_policy __attribute__((unused))
nlpvrdpy_policy[NLPVRDPY_ATTR_MAX + 1] = {
	[NLPVRDPY_ATTR_NAME]                = { .type = NLA_STRING },
	[NLPVRDPY_ATTR_MINOR]               = { .type = NLA_U32 },
	[NLPVRDPY_ATTR_NUM_PLANES]          = { .type = NLA_U8  },
	[NLPVRDPY_ATTR_WIDTH]               = { .type = NLA_U32 },
	[NLPVRDPY_ATTR_HEIGHT]              = { .type = NLA_U32 },
	[NLPVRDPY_ATTR_PIXFMT]              = { .type = NLA_U32 },
	[NLPVRDPY_ATTR_YUV_CSC]             = { .type = NLA_U8  },
	[NLPVRDPY_ATTR_YUV_BPP]             = { .type = NLA_U8  },
	[NLPVRDPY_ATTR_PLANE0_ADDR]         = { .type = NLA_U64 },
	[NLPVRDPY_ATTR_PLANE0_SIZE]         = { .type = NLA_U64 },
	[NLPVRDPY_ATTR_PLANE0_OFFSET]       = { .type = NLA_U64 },
	[NLPVRDPY_ATTR_PLANE0_PITCH]        = { .type = NLA_U64 },
	[NLPVRDPY_ATTR_PLANE0_GEM_OBJ_NAME] = { .type = NLA_U32 },
	[NLPVRDPY_ATTR_PLANE1_ADDR]         = { .type = NLA_U64 },
	[NLPVRDPY_ATTR_PLANE1_SIZE]         = { .type = NLA_U64 },
	[NLPVRDPY_ATTR_PLANE1_OFFSET]       = { .type = NLA_U64 },
	[NLPVRDPY_ATTR_PLANE1_PITCH]        = { .type = NLA_U64 },
	[NLPVRDPY_ATTR_PLANE1_GEM_OBJ_NAME] = { .type = NLA_U32 },
	[NLPVRDPY_ATTR_PLANE2_ADDR]         = { .type = NLA_U64 },
	[NLPVRDPY_ATTR_PLANE2_SIZE]         = { .type = NLA_U64 },
	[NLPVRDPY_ATTR_PLANE2_OFFSET]       = { .type = NLA_U64 },
	[NLPVRDPY_ATTR_PLANE2_PITCH]        = { .type = NLA_U64 },
	[NLPVRDPY_ATTR_PLANE2_GEM_OBJ_NAME] = { .type = NLA_U32 },
	[NLPVRDPY_ATTR_FB_MODIFIER]         = { .type = NLA_U64 },
	[NLPVRDPY_ATTR_NAMING_REQUIRED]     = { .type = NLA_FLAG },
};

#define NLPVRDPY_ATTR_PLANE(index, type)				\
	({								\
		enum nlpvrdpy_attr __retval;				\
									\
		switch (index) {					\
		case 0:							\
			__retval = NLPVRDPY_ATTR_PLANE0_ ## type;	\
			break;						\
		case 1:							\
			__retval = NLPVRDPY_ATTR_PLANE1_ ## type;	\
			break;						\
		case 2:							\
			__retval = NLPVRDPY_ATTR_PLANE2_ ## type;	\
			break;						\
		default:						\
			__retval = __NLPVRDPY_ATTR_INVALID;		\
			break;						\
		};							\
									\
		__retval;						\
	})

#endif /* __NETLINK_H__ */
