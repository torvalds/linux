/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */
#ifndef __ASM_GH_HCALL_H
#define __ASM_GH_HCALL_H

#include <linux/types.h>

#include <linux/gunyah/hcall_common.h>

/**
 * _gh_hcall: Performs an AArch64-specific call into hypervisor using Gunyah ABI
 * @hcall_num: Hypercall function ID to invoke
 * @args: Hypercall argument registers
 * @resp: Pointer to location to store response
 */
static inline int _gh_hcall(const gh_hcall_fnid_t hcall_num,
	const struct gh_hcall_args args,
	struct gh_hcall_resp *resp)
{
	uint64_t _x18;

	register uint64_t _x0 asm("x0") = args.arg0;
	register uint64_t _x1 asm("x1") = args.arg1;
	register uint64_t _x2 asm("x2") = args.arg2;
	register uint64_t _x3 asm("x3") = args.arg3;
	register uint64_t _x4 asm("x4") = args.arg4;
	register uint64_t _x5 asm("x5") = args.arg5;
	register uint64_t _x6 asm("x6") = args.arg6;
	register uint64_t _x7 asm("x7") = args.arg7;

	asm volatile (
#if IS_ENABLED(CONFIG_SHADOW_CALL_STACK)
		"str	x18, [%[_x18]]\n"
#endif
		"hvc	%[num]\n"
#if IS_ENABLED(CONFIG_SHADOW_CALL_STACK)
		"ldr	x18, [%[_x18]]\n"
		"str	xzr, [%[_x18]]\n"
#endif
		: "+r"(_x0), "+r"(_x1), "+r"(_x2), "+r"(_x3), "+r"(_x4),
		  "+r"(_x5), "+r"(_x6), "+r"(_x7)
		: [num] "i" (hcall_num), [_x18] "r"(&_x18)
		: "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
#if !IS_ENABLED(CONFIG_SHADOW_CALL_STACK)
		  "x18",
#endif
		  "memory"
		);

	resp->resp0 = _x0;
	resp->resp1 = _x1;
	resp->resp2 = _x2;
	resp->resp3 = _x3;
	resp->resp4 = _x4;
	resp->resp5 = _x5;
	resp->resp6 = _x6;
	resp->resp7 = _x7;

	return _x0;
}

#endif
