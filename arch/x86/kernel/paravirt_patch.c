// SPDX-License-Identifier: GPL-2.0
#include <linux/stringify.h>

#include <asm/paravirt.h>
#include <asm/asm-offsets.h>

unsigned int native_patch(u8 type, void *insn_buff, unsigned long addr,
			  unsigned int len)
{
	return paravirt_patch_default(type, insn_buff, addr, len);
}
