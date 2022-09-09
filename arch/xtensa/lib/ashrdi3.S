/* SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0 */
#include <linux/linkage.h>
#include <asm/asmmacro.h>
#include <asm/core.h>

#ifdef __XTENSA_EB__
#define uh a2
#define ul a3
#else
#define uh a3
#define ul a2
#endif /* __XTENSA_EB__ */

ENTRY(__ashrdi3)

	abi_entry_default
	ssr	a4
	bgei	a4, 32, .Lhigh_only
	src	ul, uh, ul
	sra	uh, uh
	abi_ret_default

.Lhigh_only:
	sra	ul, uh
	srai	uh, uh, 31
	abi_ret_default

ENDPROC(__ashrdi3)
