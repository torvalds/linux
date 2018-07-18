/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef	__TYPE_DEF_H
#define	__TYPE_DEF_H

#include <linux/kernel.h>

#ifndef NULL
#define NULL	0
#endif

#define OK	0
#define ERROR	(-1)

#define FTL_ERROR	ERROR
#define FTL_OK		OK
#define FTL_NO_FLASH	-2
#define FTL_NO_IDB	-3
#define FTL_UNSUPPORTED_FLASH	-4

#define FALSE		0
#define TRUE		(!FALSE)

#define INVALID_UINT8	((u8)0xFF)
#define INVALID_UINT16	((u16)0xFFFF)
#define INVALID_UINT32	((u32)0xFFFFFFFFL)

#endif  /*__TYPEDEF_H */
