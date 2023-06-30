/* SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0 */
#include <linux/linkage.h>
#include <asm/asmmacro.h>
#include <asm/core.h>

ENTRY(__bswapdi2)

	abi_entry_default
	ssai	8
	srli	a4, a2, 16
	src	a4, a4, a2
	src	a4, a4, a4
	src	a4, a2, a4
	srli	a2, a3, 16
	src	a2, a2, a3
	src	a2, a2, a2
	src	a2, a3, a2
	mov	a3, a4
	abi_ret_default

ENDPROC(__bswapdi2)
EXPORT_SYMBOL(__bswapdi2)
