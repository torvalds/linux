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

ENTRY(__ashldi3)

	abi_entry_default
	ssl	a4
	bgei	a4, 32, .Llow_only
	src	uh, uh, ul
	sll	ul, ul
	abi_ret_default

.Llow_only:
	sll	uh, ul
	movi	ul, 0
	abi_ret_default

ENDPROC(__ashldi3)
