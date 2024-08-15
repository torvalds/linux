/* SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0 */
#include <linux/linkage.h>
#include <asm/asmmacro.h>
#include <asm/core.h>

ENTRY(__bswapsi2)

	abi_entry_default
	ssai	8
	srli	a3, a2, 16
	src	a3, a3, a2
	src	a3, a3, a3
	src	a2, a2, a3
	abi_ret_default

ENDPROC(__bswapsi2)
