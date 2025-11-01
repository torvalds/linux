/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/linkage.h>

# This function is identical to __load_psw(), but the lx-symbols GDB command
# puts a breakpoint on it, so it needs to be kept separate.
SYM_CODE_START(jump_to_kernel)
	lpswe 0(%r2)
SYM_CODE_END(jump_to_kernel)
