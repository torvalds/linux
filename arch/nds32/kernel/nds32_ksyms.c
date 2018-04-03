// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/module.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/in6.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/ftrace.h>
#include <asm/proc-fns.h>

/* mem functions */
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memzero);

/* user mem (segment) */
EXPORT_SYMBOL(__arch_copy_from_user);
EXPORT_SYMBOL(__arch_copy_to_user);
EXPORT_SYMBOL(__arch_clear_user);

/* cache handling */
EXPORT_SYMBOL(cpu_icache_inval_all);
EXPORT_SYMBOL(cpu_dcache_wbinval_all);
EXPORT_SYMBOL(cpu_dma_inval_range);
EXPORT_SYMBOL(cpu_dma_wb_range);
