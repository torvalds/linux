/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Wu Zhangjin <wuzhangjin@gmail.com>
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
