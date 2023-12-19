// SPDX-License-Identifier: GPL-2.0

#include "../cpuflags.h"
#include "../string.h"
#include "../io.h"
#include "error.h"

#include <vdso/limits.h>
#include <uapi/asm/vmx.h>

#include <asm/shared/tdx.h>

/* Called from __tdx_hypercall() for unrecoverable failure */
void __tdx_hypercall_failed(void)
{
	error("TDVMCALL failed. TDX module bug?");
}

static inline unsigned int tdx_io_in(int size, u16 port)
{
	struct tdx_module_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = hcall_func(EXIT_REASON_IO_INSTRUCTION),
		.r12 = size,
		.r13 = 0,
		.r14 = port,
	};

	if (__tdx_hypercall(&args))
		return UINT_MAX;

	return args.r11;
}

static inline void tdx_io_out(int size, u16 port, u32 value)
{
	struct tdx_module_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = hcall_func(EXIT_REASON_IO_INSTRUCTION),
		.r12 = size,
		.r13 = 1,
		.r14 = port,
		.r15 = value,
	};

	__tdx_hypercall(&args);
}

static inline u8 tdx_inb(u16 port)
{
	return tdx_io_in(1, port);
}

static inline void tdx_outb(u8 value, u16 port)
{
	tdx_io_out(1, port, value);
}

static inline void tdx_outw(u16 value, u16 port)
{
	tdx_io_out(2, port, value);
}

void early_tdx_detect(void)
{
	u32 eax, sig[3];

	cpuid_count(TDX_CPUID_LEAF_ID, 0, &eax, &sig[0], &sig[2],  &sig[1]);

	if (memcmp(TDX_IDENT, sig, sizeof(sig)))
		return;

	/* Use hypercalls instead of I/O instructions */
	pio_ops.f_inb  = tdx_inb;
	pio_ops.f_outb = tdx_outb;
	pio_ops.f_outw = tdx_outw;
}
