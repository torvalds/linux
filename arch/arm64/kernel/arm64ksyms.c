/*
 * Based on arch/arm/kernel/armksyms.c
 *
 * Copyright (C) 2000 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/export.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/cryptohash.h>
#include <linux/delay.h>
#include <linux/in6.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/arm-smccc.h>
#include <linux/kprobes.h>

#include <asm/checksum.h>

EXPORT_SYMBOL(copy_page);
EXPORT_SYMBOL(clear_page);

	/* user mem (segment) */
EXPORT_SYMBOL(__arch_copy_from_user);
EXPORT_SYMBOL(__arch_copy_to_user);
EXPORT_SYMBOL(__arch_clear_user);
EXPORT_SYMBOL(__arch_copy_in_user);

	/* physical memory */
EXPORT_SYMBOL(memstart_addr);

	/* string / mem functions */
#ifndef CONFIG_KASAN
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memchr);
#endif

EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(__memset);
EXPORT_SYMBOL(__memcpy);
EXPORT_SYMBOL(__memmove);

	/* atomic bitops */
EXPORT_SYMBOL(set_bit);
EXPORT_SYMBOL(test_and_set_bit);
EXPORT_SYMBOL(clear_bit);
EXPORT_SYMBOL(test_and_clear_bit);
EXPORT_SYMBOL(change_bit);
EXPORT_SYMBOL(test_and_change_bit);

#ifdef CONFIG_FUNCTION_TRACER
EXPORT_SYMBOL(_mcount);
NOKPROBE_SYMBOL(_mcount);
#endif

	/* arm-smccc */
EXPORT_SYMBOL(__arm_smccc_smc);
EXPORT_SYMBOL(__arm_smccc_hvc);

	/* tishift.S */
extern long long __ashlti3(long long a, int b);
EXPORT_SYMBOL(__ashlti3);
extern long long __ashrti3(long long a, int b);
EXPORT_SYMBOL(__ashrti3);
extern long long __lshrti3(long long a, int b);
EXPORT_SYMBOL(__lshrti3);
