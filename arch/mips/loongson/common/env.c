/*
 * Based on Ocelot Linux port, which is
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * Copyright 2003 ICT CAS
 * Author: Michael Guo <guoyi@ict.ac.cn>
 *
 * Copyright (C) 2007 Lemote Inc. & Insititute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>

#include <asm/bootinfo.h>

#include <loongson.h>

unsigned long cpu_clock_freq;
EXPORT_SYMBOL(cpu_clock_freq);
unsigned long memsize, highmemsize;

#define parse_even_earlier(res, option, p)				\
do {									\
	int ret;							\
	if (strncmp(option, (char *)p, strlen(option)) == 0)		\
		ret = strict_strtol((char *)p + strlen(option"="), 10, &res); \
} while (0)

void __init prom_init_env(void)
{
	/* pmon passes arguments in 32bit pointers */
	int *_prom_envp;
	unsigned long bus_clock;
	unsigned int processor_id;
	long l;

	/* firmware arguments are initialized in head.S */
	_prom_envp = (int *)fw_arg2;

	l = (long)*_prom_envp;
	while (l != 0) {
		parse_even_earlier(bus_clock, "busclock", l);
		parse_even_earlier(cpu_clock_freq, "cpuclock", l);
		parse_even_earlier(memsize, "memsize", l);
		parse_even_earlier(highmemsize, "highmemsize", l);
		_prom_envp++;
		l = (long)*_prom_envp;
	}
	if (memsize == 0)
		memsize = 256;
	if (bus_clock == 0)
		bus_clock = 66000000;
	if (cpu_clock_freq == 0) {
		processor_id = (&current_cpu_data)->processor_id;
		switch (processor_id & PRID_REV_MASK) {
		case PRID_REV_LOONGSON2E:
			cpu_clock_freq = 533080000;
			break;
		case PRID_REV_LOONGSON2F:
			cpu_clock_freq = 797000000;
			break;
		default:
			cpu_clock_freq = 100000000;
			break;
		}
	}

	pr_info("busclock=%ld, cpuclock=%ld, memsize=%ld, highmemsize=%ld\n",
		bus_clock, cpu_clock_freq, memsize, highmemsize);
}
