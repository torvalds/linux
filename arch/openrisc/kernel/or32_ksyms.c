/*
 * OpenRISC or32_ksyms.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/elfcore.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/semaphore.h>

#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/hardirq.h>
#include <asm/delay.h>
#include <asm/pgalloc.h>

#define DECLARE_EXPORT(name) extern void name(void); EXPORT_SYMBOL(name)

/* compiler generated symbols */
DECLARE_EXPORT(__udivsi3);
DECLARE_EXPORT(__divsi3);
DECLARE_EXPORT(__umodsi3);
DECLARE_EXPORT(__modsi3);
DECLARE_EXPORT(__muldi3);
DECLARE_EXPORT(__ashrdi3);
DECLARE_EXPORT(__ashldi3);
DECLARE_EXPORT(__lshrdi3);

EXPORT_SYMBOL(__copy_tofrom_user);
