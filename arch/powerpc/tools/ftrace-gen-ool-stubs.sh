#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

# Error out on error
set -e

num_ool_stubs_text_builtin="$1"
is_64bit="$2"
objdump="$3"
vmlinux_o="$4"
arch_vmlinux_S="$5"

RELOCATION=R_PPC64_ADDR64
if [ -z "$is_64bit" ]; then
	RELOCATION=R_PPC_ADDR32
fi

num_ool_stubs_text=$($objdump -r -j __patchable_function_entries "$vmlinux_o" |
		     grep -v ".init.text" | grep -c "$RELOCATION")
num_ool_stubs_inittext=$($objdump -r -j __patchable_function_entries "$vmlinux_o" |
			 grep ".init.text" | grep -c "$RELOCATION")

if [ "$num_ool_stubs_text" -gt "$num_ool_stubs_text_builtin" ]; then
	num_ool_stubs_text_end=$((num_ool_stubs_text - num_ool_stubs_text_builtin))
else
	num_ool_stubs_text_end=0
fi

cat > "$arch_vmlinux_S" <<EOF
#include <asm/asm-offsets.h>
#include <asm/ppc_asm.h>
#include <linux/linkage.h>

.pushsection .tramp.ftrace.text,"aw"
SYM_DATA(ftrace_ool_stub_text_end_count, .long $num_ool_stubs_text_end)

SYM_START(ftrace_ool_stub_text_end, SYM_L_GLOBAL, .balign SZL)
#if $num_ool_stubs_text_end
	.space $num_ool_stubs_text_end * FTRACE_OOL_STUB_SIZE
#endif
SYM_CODE_END(ftrace_ool_stub_text_end)
.popsection

.pushsection .tramp.ftrace.init,"aw"
SYM_DATA(ftrace_ool_stub_inittext_count, .long $num_ool_stubs_inittext)

SYM_START(ftrace_ool_stub_inittext, SYM_L_GLOBAL, .balign SZL)
	.space $num_ool_stubs_inittext * FTRACE_OOL_STUB_SIZE
SYM_CODE_END(ftrace_ool_stub_inittext)
.popsection
EOF
