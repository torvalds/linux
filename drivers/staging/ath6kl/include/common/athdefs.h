//------------------------------------------------------------------------------
// <copyright file="athdefs.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================
#ifndef __ATHDEFS_H__
#define __ATHDEFS_H__

/*
 * This file contains definitions that may be used across both
 * Host and Target software.  Nothing here is module-dependent
 * or platform-dependent.
 */

/*
 * Generic error codes that can be used by hw, sta, ap, sim, dk
 * and any other environments.
 * Feel free to add any more non-zero codes that you need.
 */

#define A_ERROR			(-1)	/* Generic error return */
#define A_DEVICE_NOT_FOUND	1	/* not able to find PCI device */
#define A_NO_MEMORY		2	/* not able to allocate memory,
					 * not avail#defineable */
#define A_MEMORY_NOT_AVAIL	3	/* memory region is not free for
					 * mapping */
#define A_NO_FREE_DESC		4	/* no free descriptors available */
#define A_BAD_ADDRESS		5	/* address does not match descriptor */
#define A_WIN_DRIVER_ERROR	6	/* used in NT_HW version,
					 * if problem at init */
#define A_REGS_NOT_MAPPED	7	/* registers not correctly mapped */
#define A_EPERM			8	/* Not superuser */
#define A_EACCES		0	/* Access denied */
#define A_ENOENT		10	/* No such entry, search failed, etc. */
#define A_EEXIST		11	/* The object already exists
					 * (can't create) */
#define A_EFAULT		12	/* Bad address fault */
#define A_EBUSY			13	/* Object is busy */
#define A_EINVAL		14	/* Invalid parameter */
#define A_EMSGSIZE		15	/* Bad message buffer length */
#define A_ECANCELED		16	/* Operation canceled */
#define A_ENOTSUP		17	/* Operation not supported */
#define A_ECOMM			18	/* Communication error on send */
#define A_EPROTO		19	/* Protocol error */
#define A_ENODEV		20	/* No such device */
#define A_EDEVNOTUP		21	/* device is not UP */
#define A_NO_RESOURCE		22	/* No resources for
					 * requested operation */
#define A_HARDWARE		23	/* Hardware failure */
#define A_PENDING		24	/* Asynchronous routine; will send up
					 * results later
					 * (typically in callback) */
#define A_EBADCHANNEL		25	/* The channel cannot be used */
#define A_DECRYPT_ERROR		26	/* Decryption error */
#define A_PHY_ERROR		27	/* RX PHY error */
#define A_CONSUMED		28	/* Object was consumed */

#endif /* __ATHDEFS_H__ */
