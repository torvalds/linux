/*
 * Copyright (C) 2009 Lemote, Inc. & Institute of Computing Technology
 * Author: Wu Zhangjin <wuzj@lemote.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_MACH_LOONGSON_MEM_H
#define __ASM_MACH_LOONGSON_MEM_H

/*
 * On Lemote Loongson 2e
 *
 * the high memory space starts from 512M.
 * the peripheral registers reside between 0x1000:0000 and 0x2000:0000.
 */

#ifdef CONFIG_LEMOTE_FULOONG2E

#define LOONGSON_HIGHMEM_START  0x20000000

#define LOONGSON_MMIO_MEM_START 0x10000000
#define LOONGSON_MMIO_MEM_END   0x20000000

#endif

#endif /* __ASM_MACH_LOONGSON_MEM_H */
