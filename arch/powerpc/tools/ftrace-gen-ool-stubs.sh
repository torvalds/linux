#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

# Error out on error
set -e

is_64bit="$1"
objdump="$2"
vmlinux_o="$3"
arch_vmlinux_S="$4"

RELOCATION=R_PPC64_ADDR64
if [ -z "$is_64bit" ]; then
	RELOCATION=R_PPC_ADDR32
fi

num_ool_stubs_text=$($objdump -r -j __patchable_function_entries "$vmlinux_o" |
		     grep -v ".init.text" | grep -c "$RELOCATION")
num_ool_stubs_inittext=$($objdump -r -j __patchable_function_entries "$vmlinux_o" |
			 grep ".init.text" | grep -c "$RELOCATION")

cat > "$arch_vmlinux_S" <<EOF
#include <asm/asm-offsets.h>
#include <linux/linkage.h>

.pushsection .tramp.ftrace.text,"aw"
SYM_DATA(ftrace_ool_stub_text_end_count, .long $num_ool_stubs_text)

SYM_CODE_START(ftrace_ool_stub_text_end)
	.space $num_ool_stubs_text * FTRACE_OOL_STUB_SIZE
SYM_CODE_END(ftrace_ool_stub_text_end)
.popsection

.pushsection .tramp.ftrace.init,"aw"
SYM_DATA(ftrace_ool_stub_inittext_count, .long $num_ool_stubs_inittext)

SYM_CODE_START(ftrace_ool_stub_inittext)
	.space $num_ool_stubs_inittext * FTRACE_OOL_STUB_SIZE
SYM_CODE_END(ftrace_ool_stub_inittext)
.popsection
EOF
