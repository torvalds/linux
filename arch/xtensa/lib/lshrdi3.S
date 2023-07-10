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

ENTRY(__lshrdi3)

	abi_entry_default
	ssr	a4
	bgei	a4, 32, .Lhigh_only
	src	ul, uh, ul
	srl	uh, uh
	abi_ret_default

.Lhigh_only:
	srl	ul, uh
	movi	uh, 0
	abi_ret_default

ENDPROC(__lshrdi3)
EXPORT_SYMBOL(__lshrdi3)
