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

#define SOC_AMAZON_SE	"Amazon_SE"

#define PART_SHIFT	12
#define PART_MASK	0x0FFFFFFF
#define REV_SHIFT	28
#define REV_MASK	0xF0000000

void __init ltq_soc_detect(struct ltq_soc_info *i)
{
	i->partnum = (ltq_r32(LTQ_MPS_CHIPID) & PART_MASK) >> PART_SHIFT;
	i->rev = (ltq_r32(LTQ_MPS_CHIPID) & REV_MASK) >> REV_SHIFT;
	switch (i->partnum) {
	case SOC_ID_AMAZON_SE:
		i->name = SOC_AMAZON_SE;
		i->type = SOC_TYPE_AMAZON_SE;
		break;

	default:
		unreachable();
		break;
	}
}
