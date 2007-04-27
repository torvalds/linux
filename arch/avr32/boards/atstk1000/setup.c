/*
 * ATSTK1000 board-specific setup code.
 *
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/linkage.h>

#include <asm/setup.h>

#include <asm/arch/board.h>

/* Initialized by bootloader-specific startup code. */
struct tag *bootloader_tags __initdata;
