/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 ARM Ltd.
 */
#ifndef __ASM_GCS_H
#define __ASM_GCS_H

#include <asm/types.h>
#include <asm/uaccess.h>

static inline void gcsb_dsync(void)
{
	asm volatile(".inst 0xd503227f" : : : "memory");
}

static inline void gcsstr(u64 *addr, u64 val)
{
	register u64 *_addr __asm__ ("x0") = addr;
	register long _val __asm__ ("x1") = val;

	/* GCSSTTR x1, x0 */
	asm volatile(
		".inst 0xd91f1c01\n"
		:
		: "rZ" (_val), "r" (_addr)
		: "memory");
}

static inline void gcsss1(u64 Xt)
{
	asm volatile (
		"sys #3, C7, C7, #2, %0\n"
		:
		: "rZ" (Xt)
		: "memory");
}

static inline u64 gcsss2(void)
{
	u64 Xt;

	asm volatile(
		"SYSL %0, #3, C7, C7, #3\n"
		: "=r" (Xt)
		:
		: "memory");

	return Xt;
}

#endif
