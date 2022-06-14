/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __BASIC_TYPES_H__
#define __BASIC_TYPES_H__

#include <linux/types.h>
#define NDIS_OID uint

typedef void (*proc_t)(void *);

/* port from fw */
/*  TODO: Macros Below are Sync from SD7-Driver. It is necessary
 * to check correctness */

#define	N_BYTE_ALIGMENT(__value, __aligment) ((__aligment == 1) ? \
	(__value) : (((__value + __aligment - 1) / __aligment) * __aligment))

#endif /* __BASIC_TYPES_H__ */
