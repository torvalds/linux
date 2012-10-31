/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Copyright (C) 2012 Thomas Langer <thomas.langer@lantiq.com>
 * Copyright (C) 2012 John Crispin <blogic@openwrt.org>
 */

#include <linux/kernel.h>
#include <asm/cacheflush.h>
#include <asm/traps.h>
#include <asm/io.h>

#include <lantiq_soc.h>

#include "../prom.h"

#define SOC_FALCON	"Falcon"
#define SOC_FALCON_D	"Falcon-D"
#define SOC_FALCON_V	"Falcon-V"
#define SOC_FALCON_M	"Falcon-M"

#define COMP_FALCON	"lantiq,falcon"

#define PART_SHIFT	12
#define PART_MASK	0x0FFFF000
#define REV_SHIFT	28
#define REV_MASK	0xF0000000
#define SREV_SHIFT	22
#define SREV_MASK	0x03C00000
#define TYPE_SHIFT	26
#define TYPE_MASK	0x3C000000

/* reset, nmi and ejtag exception vectors */
#define BOOT_REG_BASE	(KSEG1 | 0x1F200000)
#define BOOT_RVEC	(BOOT_REG_BASE | 0x00)
#define BOOT_NVEC	(BOOT_REG_BASE | 0x04)
#define BOOT_EVEC	(BOOT_REG_BASE | 0x08)

void __init ltq_soc_nmi_setup(void)
{
	extern void (*nmi_handler)(void);

	ltq_w32((unsigned long)&nmi_handler, (void *)BOOT_NVEC);
}

void __init ltq_soc_ejtag_setup(void)
{
	extern void (*ejtag_debug_handler)(void);

	ltq_w32((unsigned long)&ejtag_debug_handler, (void *)BOOT_EVEC);
}

void __init ltq_soc_detect(struct ltq_soc_info *i)
{
	u32 type;
	i->partnum = (ltq_r32(FALCON_CHIPID) & PART_MASK) >> PART_SHIFT;
	i->rev = (ltq_r32(FALCON_CHIPID) & REV_MASK) >> REV_SHIFT;
	i->srev = ((ltq_r32(FALCON_CHIPCONF) & SREV_MASK) >> SREV_SHIFT);
	i->compatible = COMP_FALCON;
	i->type = SOC_TYPE_FALCON;
	sprintf(i->rev_type, "%c%d%d", (i->srev & 0x4) ? ('B') : ('A'),
		i->rev & 0x7, (i->srev & 0x3) + 1);

	switch (i->partnum) {
	case SOC_ID_FALCON:
		type = (ltq_r32(FALCON_CHIPTYPE) & TYPE_MASK) >> TYPE_SHIFT;
		switch (type) {
		case 0:
			i->name = SOC_FALCON_D;
			break;
		case 1:
			i->name = SOC_FALCON_V;
			break;
		case 2:
			i->name = SOC_FALCON_M;
			break;
		default:
			i->name = SOC_FALCON;
			break;
		}
		break;

	default:
		unreachable();
		break;
	}

	board_nmi_handler_setup = ltq_soc_nmi_setup;
	board_ejtag_handler_setup = ltq_soc_ejtag_setup;
}
