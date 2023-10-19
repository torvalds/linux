/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Micro-assembler generated clear_page/copy_page functions.
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.
 * Copyright (C) 2012  Ralf Baechle <ralf@linux-mips.org>
 */
#include <asm/asm.h>
#include <asm/export.h>
#include <asm/regdef.h>

#ifdef CONFIG_SIBYTE_DMA_PAGEOPS
#define cpu_clear_page_function_name	clear_page_cpu
#define cpu_copy_page_function_name	copy_page_cpu
#else
#define cpu_clear_page_function_name	clear_page
#define cpu_copy_page_function_name	copy_page
#endif

/*
 * Maximum sizes:
 *
 * R4000 128 bytes S-cache:		0x058 bytes
 * R4600 v1.7:				0x05c bytes
 * R4600 v2.0:				0x060 bytes
 * With prefetching, 16 word strides	0x120 bytes
 */
EXPORT(__clear_page_start)
LEAF(cpu_clear_page_function_name)
EXPORT_SYMBOL(cpu_clear_page_function_name)
1:	j	1b		/* Dummy, will be replaced. */
	.space 288
END(cpu_clear_page_function_name)
EXPORT(__clear_page_end)

/*
 * Maximum sizes:
 *
 * R4000 128 bytes S-cache:		0x11c bytes
 * R4600 v1.7:				0x080 bytes
 * R4600 v2.0:				0x07c bytes
 * With prefetching, 16 word strides	0x540 bytes
 */
EXPORT(__copy_page_start)
LEAF(cpu_copy_page_function_name)
EXPORT_SYMBOL(cpu_copy_page_function_name)
1:	j	1b		/* Dummy, will be replaced. */
	.space 1344
END(cpu_copy_page_function_name)
EXPORT(__copy_page_end)
