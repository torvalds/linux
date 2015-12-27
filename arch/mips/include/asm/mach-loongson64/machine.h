/*
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Wu Zhangjin <wuzhangjin@gmail.com>
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_MACH_LOONGSON64_MACHINE_H
#define __ASM_MACH_LOONGSON64_MACHINE_H

#ifdef CONFIG_LEMOTE_FULOONG2E

#define LOONGSON_MACHTYPE MACH_LEMOTE_FL2E

#endif

/* use fuloong2f as the default machine of LEMOTE_MACH2F */
#ifdef CONFIG_LEMOTE_MACH2F

#define LOONGSON_MACHTYPE MACH_LEMOTE_FL2F

#endif

#ifdef CONFIG_LOONGSON_MACH3X

#define LOONGSON_MACHTYPE MACH_LOONGSON_GENERIC

#endif /* CONFIG_LOONGSON_MACH3X */

#endif /* __ASM_MACH_LOONGSON64_MACHINE_H */
