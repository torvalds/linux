// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Rivos Inc. */

#include <asm/sbi.h>
#define CREATE_TRACE_POINTS
#include <asm/trace.h>

long __sbi_base_ecall(int fid)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_BASE, fid);
	if (!ret.error)
		return ret.value;
	else
		return sbi_err_map_linux_errno(ret.error);
}
EXPORT_SYMBOL(__sbi_base_ecall);

#ifdef CONFIG_TRACEPOINTS
void do_trace_sbi_call(int ext, int fid)
{
	trace_sbi_call(ext, fid);
}
EXPORT_SYMBOL(do_trace_sbi_call);
EXPORT_TRACEPOINT_SYMBOL(sbi_call);

void do_trace_sbi_return(int ext, long error, long value)
{
	trace_sbi_return(ext, error, value);
}
EXPORT_SYMBOL(do_trace_sbi_return);
EXPORT_TRACEPOINT_SYMBOL(sbi_return);
#endif
