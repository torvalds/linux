// SPDX-License-Identifier: GPL-2.0
#include <linux/mmdebug.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <asm/page.h>

unsigned long __phys_addr(unsigned long x, bool is_31bit)
{
	VIRTUAL_BUG_ON(is_vmalloc_or_module_addr((void *)(x)));
	x = __pa_nodebug(x);
	if (is_31bit)
		VIRTUAL_BUG_ON(x >> 31);
	return x;
}
EXPORT_SYMBOL(__phys_addr);
