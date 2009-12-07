/*
 * Copyright (C) 2009 Lemote Inc. & Insititute of Computing Technology
 * Author: Wu Zhangjin, wuzj@lemote.com
 *
 * Copyright (c) 2009 Zhang Le <r0bertz@gentoo.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/errno.h>
#include <asm/bootinfo.h>

#include <loongson.h>
#include <machine.h>

static const char *system_types[] = {
	[MACH_LOONGSON_UNKNOWN]         "unknown loongson machine",
	[MACH_LEMOTE_FL2E]              "lemote-fuloong-2e-box",
	[MACH_LEMOTE_FL2F]              "lemote-fuloong-2f-box",
	[MACH_LEMOTE_ML2F7]             "lemote-mengloong-2f-7inches",
	[MACH_LEMOTE_YL2F89]            "lemote-yeeloong-2f-8.9inches",
	[MACH_DEXXON_GDIUM2F10]         "dexxon-gidum-2f-10inches",
	[MACH_LOONGSON_END]             NULL,
};

const char *get_system_type(void)
{
	if (mips_machtype == MACH_UNKNOWN)
		mips_machtype = LOONGSON_MACHTYPE;

	return system_types[mips_machtype];
}

static __init int machtype_setup(char *str)
{
	int machtype = MACH_LEMOTE_FL2E;

	if (!str)
		return -EINVAL;

	for (; system_types[machtype]; machtype++)
		if (strstr(system_types[machtype], str)) {
			mips_machtype = machtype;
			break;
		}
	return 0;
}
__setup("machtype=", machtype_setup);
