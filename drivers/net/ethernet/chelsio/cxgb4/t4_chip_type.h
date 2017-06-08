/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2015 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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
#ifndef __T4_CHIP_TYPE_H__
#define __T4_CHIP_TYPE_H__

#define CHELSIO_T4		0x4
#define CHELSIO_T5		0x5
#define CHELSIO_T6		0x6

/* We code the Chelsio T4 Family "Chip Code" as a tuple:
 *
 *     (Chip Version, Chip Revision)
 *
 * where:
 *
 *     Chip Version: is T4, T5, etc.
 *     Chip Revision: is the FAB "spin" of the Chip Version.
 */
#define CHELSIO_CHIP_CODE(version, revision) (((version) << 4) | (revision))
#define CHELSIO_CHIP_VERSION(code) (((code) >> 4) & 0xf)
#define CHELSIO_CHIP_RELEASE(code) ((code) & 0xf)

enum chip_type {
	T4_A1 = CHELSIO_CHIP_CODE(CHELSIO_T4, 1),
	T4_A2 = CHELSIO_CHIP_CODE(CHELSIO_T4, 2),
	T4_FIRST_REV	= T4_A1,
	T4_LAST_REV	= T4_A2,

	T5_A0 = CHELSIO_CHIP_CODE(CHELSIO_T5, 0),
	T5_A1 = CHELSIO_CHIP_CODE(CHELSIO_T5, 1),
	T5_FIRST_REV	= T5_A0,
	T5_LAST_REV	= T5_A1,

	T6_A0 = CHELSIO_CHIP_CODE(CHELSIO_T6, 0),
	T6_FIRST_REV	= T6_A0,
	T6_LAST_REV	= T6_A0,
};

static inline int is_t4(enum chip_type chip)
{
	return (CHELSIO_CHIP_VERSION(chip) == CHELSIO_T4);
}

static inline int is_t5(enum chip_type chip)
{
	return (CHELSIO_CHIP_VERSION(chip) == CHELSIO_T5);
}

static inline int is_t6(enum chip_type chip)
{
	return (CHELSIO_CHIP_VERSION(chip) == CHELSIO_T6);
}

#endif /* __T4_CHIP_TYPE_H__ */
