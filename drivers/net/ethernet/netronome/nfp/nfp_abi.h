/* SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause) */
/*
 * Copyright (C) 2018 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __NFP_ABI__
#define __NFP_ABI__ 1

#include <linux/types.h>

#define NFP_MBOX_SYM_NAME		"_abi_nfd_pf%u_mbox"
#define NFP_MBOX_SYM_MIN_SIZE		16 /* When no data needed */

#define NFP_MBOX_CMD		0x00
#define NFP_MBOX_RET		0x04
#define NFP_MBOX_DATA_LEN	0x08
#define NFP_MBOX_RESERVED	0x0c
#define NFP_MBOX_DATA		0x10

/**
 * enum nfp_mbox_cmd - PF mailbox commands
 *
 * @NFP_MBOX_NO_CMD:	null command
 * Used to indicate previous command has finished.
 */
enum nfp_mbox_cmd {
	NFP_MBOX_NO_CMD			= 0x00,
};

#endif
