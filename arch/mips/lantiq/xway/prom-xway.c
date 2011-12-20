/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#include <linux/export.h>
#include <linux/clk.h>
#include <asm/bootinfo.h>
#include <asm/time.h>

#include <lantiq_soc.h>

#include "../prom.h"

#define SOC_DANUBE	"Danube"
#define SOC_TWINPASS	"Twinpass"
#define SOC_AR9		"AR9"

#define PART_SHIFT	12
#define PART_MASK	0x0FFFFFFF
#define REV_SHIFT	28
#define REV_MASK	0xF0000000

void __init ltq_soc_detect(struct ltq_soc_info *i)
{
	i->partnum = (ltq_r32(LTQ_MPS_CHIPID) & PART_MASK) >> PART_SHIFT;
	i->rev = (ltq_r32(LTQ_MPS_CHIPID) & REV_MASK) >> REV_SHIFT;
	switch (i->partnum) {
	case SOC_ID_DANUBE1:
	case SOC_ID_DANUBE2:
		i->name = SOC_DANUBE;
		i->type = SOC_TYPE_DANUBE;
		break;

	case SOC_ID_TWINPASS:
		i->name = SOC_TWINPASS;
		i->type = SOC_TYPE_DANUBE;
		break;

	case SOC_ID_ARX188:
	case SOC_ID_ARX168:
	case SOC_ID_ARX182:
		i->name = SOC_AR9;
		i->type = SOC_TYPE_AR9;
		break;

	default:
		unreachable();
		break;
	}
}
