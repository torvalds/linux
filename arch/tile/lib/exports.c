/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Exports from assembler code and from libtile-cc.
 */

#include <linux/module.h>

/* arch/tile/lib/usercopy.S */
#include <linux/uaccess.h>
EXPORT_SYMBOL(clear_user_asm);
EXPORT_SYMBOL(flush_user_asm);
EXPORT_SYMBOL(finv_user_asm);

/* arch/tile/kernel/entry.S */
#include <linux/kernel.h>
#include <asm/processor.h>
EXPORT_SYMBOL(current_text_addr);

/* arch/tile/kernel/head.S */
EXPORT_SYMBOL(empty_zero_page);

#ifdef CONFIG_FUNCTION_TRACER
/* arch/tile/kernel/mcount_64.S */
#include <asm/ftrace.h>
EXPORT_SYMBOL(__mcount);
#endif /* CONFIG_FUNCTION_TRACER */

/* arch/tile/lib/, various memcpy files */
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(__copy_to_user_inatomic);
EXPORT_SYMBOL(__copy_from_user_inatomic);
EXPORT_SYMBOL(__copy_from_user_zeroing);
#ifdef __tilegx__
EXPORT_SYMBOL(__copy_in_user_inatomic);
#endif

/* hypervisor glue */
#include <hv/hypervisor.h>
EXPORT_SYMBOL(hv_dev_open);
EXPORT_SYMBOL(hv_dev_pread);
EXPORT_SYMBOL(hv_dev_pwrite);
EXPORT_SYMBOL(hv_dev_preada);
EXPORT_SYMBOL(hv_dev_pwritea);
EXPORT_SYMBOL(hv_dev_poll);
EXPORT_SYMBOL(hv_dev_poll_cancel);
EXPORT_SYMBOL(hv_dev_close);
EXPORT_SYMBOL(hv_sysconf);
EXPORT_SYMBOL(hv_confstr);
EXPORT_SYMBOL(hv_get_rtc);
EXPORT_SYMBOL(hv_set_rtc);

/* libgcc.a */
uint32_t __udivsi3(uint32_t dividend, uint32_t divisor);
EXPORT_SYMBOL(__udivsi3);
int32_t __divsi3(int32_t dividend, int32_t divisor);
EXPORT_SYMBOL(__divsi3);
uint64_t __udivdi3(uint64_t dividend, uint64_t divisor);
EXPORT_SYMBOL(__udivdi3);
int64_t __divdi3(int64_t dividend, int64_t divisor);
EXPORT_SYMBOL(__divdi3);
uint32_t __umodsi3(uint32_t dividend, uint32_t divisor);
EXPORT_SYMBOL(__umodsi3);
int32_t __modsi3(int32_t dividend, int32_t divisor);
EXPORT_SYMBOL(__modsi3);
uint64_t __umoddi3(uint64_t dividend, uint64_t divisor);
EXPORT_SYMBOL(__umoddi3);
int64_t __moddi3(int64_t dividend, int64_t divisor);
EXPORT_SYMBOL(__moddi3);
#ifndef __tilegx__
int64_t __muldi3(int64_t, int64_t);
EXPORT_SYMBOL(__muldi3);
uint64_t __lshrdi3(uint64_t, unsigned int);
EXPORT_SYMBOL(__lshrdi3);
uint64_t __ashrdi3(uint64_t, unsigned int);
EXPORT_SYMBOL(__ashrdi3);
uint64_t __ashldi3(uint64_t, unsigned int);
EXPORT_SYMBOL(__ashldi3);
int __ffsdi2(uint64_t);
EXPORT_SYMBOL(__ffsdi2);
#endif
