/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/cputype.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/setup.h>

#include "id.h"

struct dbx500_asic_id dbx500_id;

static unsigned int ux500_read_asicid(phys_addr_t addr)
{
	phys_addr_t base = addr & ~0xfff;
	struct map_desc desc = {
		.virtual	= UX500_VIRT_ROM,
		.pfn		= __phys_to_pfn(base),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	};

	iotable_init(&desc, 1);

	/* As in devicemaps_init() */
	local_flush_tlb_all();
	flush_cache_all();

	return readl(IOMEM(UX500_VIRT_ROM + (addr & 0xfff)));
}

static void ux500_print_soc_info(unsigned int asicid)
{
	unsigned int rev = dbx500_revision();

	pr_info("DB%4x ", dbx500_partnumber());

	if (rev == 0x01)
		pr_cont("Early Drop");
	else if (rev >= 0xA0)
		pr_cont("v%d.%d" , (rev >> 4) - 0xA + 1, rev & 0xf);
	else
		pr_cont("Unknown");

	pr_cont(" [%#010x]\n", asicid);
}

static unsigned int partnumber(unsigned int asicid)
{
	return (asicid >> 8) & 0xffff;
}

/*
 * SOC		MIDR		ASICID ADDRESS		ASICID VALUE
 * DB8500ed	0x410fc090	0x9001FFF4		0x00850001
 * DB8500v1	0x411fc091	0x9001FFF4		0x008500A0
 * DB8500v1.1	0x411fc091	0x9001FFF4		0x008500A1
 * DB8500v2	0x412fc091	0x9001DBF4		0x008500B0
 * DB8520v2.2	0x412fc091	0x9001DBF4		0x008500B2
 * DB5500v1	0x412fc091	0x9001FFF4		0x005500A0
 * DB9540	0x413fc090	0xFFFFDBF4		0x009540xx
 */

void __init ux500_map_io(void)
{
	unsigned int cpuid = read_cpuid_id();
	unsigned int asicid = 0;
	phys_addr_t addr = 0;

	switch (cpuid) {
	case 0x410fc090: /* DB8500ed */
	case 0x411fc091: /* DB8500v1 */
		addr = 0x9001FFF4;
		break;

	case 0x412fc091: /* DB8520 / DB8500v2 / DB5500v1 */
		asicid = ux500_read_asicid(0x9001DBF4);
		if (partnumber(asicid) == 0x8500 ||
		    partnumber(asicid) == 0x8520)
			/* DB8500v2 */
			break;

		/* DB5500v1 */
		addr = 0x9001FFF4;
		break;

	case 0x413fc090: /* DB9540 */
		addr = 0xFFFFDBF4;
		break;
	}

	if (addr)
		asicid = ux500_read_asicid(addr);

	if (!asicid) {
		pr_err("Unable to identify SoC\n");
		ux500_unknown_soc();
	}

	dbx500_id.process = asicid >> 24;
	dbx500_id.partnumber = partnumber(asicid);
	dbx500_id.revision = asicid & 0xff;

	ux500_print_soc_info(asicid);
}
