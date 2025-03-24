// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Rivos Inc. */

#include <asm/sbi.h>
#define CREATE_TRACE_POINTS
#include <asm/trace.h>

long __sbi_base_ecall(int fid)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_BASE, fid, 0, 0, 0, 0, 0, 0);
	if (!ret.error)
		return ret.value;
	else
		return sbi_err_map_linux_errno(ret.error);
}
EXPORT_SYMBOL(__sbi_base_ecall);

struct sbiret __sbi_ecall(unsigned long arg0, unsigned long arg1,
			  unsigned long arg2, unsigned long arg3,
			  unsigned long arg4, unsigned long arg5,
			  int fid, int ext)
{
	struct sbiret ret;

	trace_sbi_call(ext, fid);

	register uintptr_t a0 asm ("a0") = (uintptr_t)(arg0);
	register uintptr_t a1 asm ("a1") = (uintptr_t)(arg1);
	register uintptr_t a2 asm ("a2") = (uintptr_t)(arg2);
	register uintptr_t a3 asm ("a3") = (uintptr_t)(arg3);
	register uintptr_t a4 asm ("a4") = (uintptr_t)(arg4);
	register uintptr_t a5 asm ("a5") = (uintptr_t)(arg5);
	register uintptr_t a6 asm ("a6") = (uintptr_t)(fid);
	register uintptr_t a7 asm ("a7") = (uintptr_t)(ext);
	asm volatile ("ecall"
		       : "+r" (a0), "+r" (a1)
		       : "r" (a2), "r" (a3), "r" (a4), "r" (a5), "r" (a6), "r" (a7)
		       : "memory");
	ret.error = a0;
	ret.value = a1;

	trace_sbi_return(ext, ret.error, ret.value);

	return ret;
}
EXPORT_SYMBOL(__sbi_ecall);
