/*
 * Copyright (C) 2007 Atmel Corporation.
 * Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * Under GPLv2
 */

#define pr_fmt(fmt)	"AT91: " fmt

#include <linux/module.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/pm.h>
#include <linux/of_address.h>
#include <linux/pinctrl/machine.h>
#include <linux/clk/at91_pmc.h>

#include <asm/system_misc.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/cpu.h>
#include <mach/at91_dbgu.h>

#include "generic.h"
#include "pm.h"

struct at91_socinfo at91_soc_initdata;
EXPORT_SYMBOL(at91_soc_initdata);

static struct map_desc at91_io_desc __initdata __maybe_unused = {
	.virtual	= (unsigned long)AT91_VA_BASE_SYS,
	.pfn		= __phys_to_pfn(AT91_BASE_SYS),
	.length		= SZ_16K,
	.type		= MT_DEVICE,
};

static struct map_desc at91_alt_io_desc __initdata __maybe_unused = {
	.virtual	= (unsigned long)AT91_ALT_VA_BASE_SYS,
	.pfn		= __phys_to_pfn(AT91_ALT_BASE_SYS),
	.length		= 24 * SZ_1K,
	.type		= MT_DEVICE,
};

static void __init soc_detect(u32 dbgu_base)
{
	u32 cidr, socid;

	cidr = __raw_readl(AT91_IO_P2V(dbgu_base) + AT91_DBGU_CIDR);
	socid = cidr & ~AT91_CIDR_VERSION;

	switch (socid) {
	case ARCH_ID_AT91RM9200:
		at91_soc_initdata.type = AT91_SOC_RM9200;
		if (at91_soc_initdata.subtype == AT91_SOC_SUBTYPE_UNKNOWN)
			at91_soc_initdata.subtype = AT91_SOC_RM9200_BGA;
		break;

	case ARCH_ID_AT91SAM9260:
		at91_soc_initdata.type = AT91_SOC_SAM9260;
		at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_NONE;
		break;

	case ARCH_ID_AT91SAM9261:
		at91_soc_initdata.type = AT91_SOC_SAM9261;
		at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_NONE;
		break;

	case ARCH_ID_AT91SAM9263:
		at91_soc_initdata.type = AT91_SOC_SAM9263;
		at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_NONE;
		break;

	case ARCH_ID_AT91SAM9G20:
		at91_soc_initdata.type = AT91_SOC_SAM9G20;
		at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_NONE;
		break;

	case ARCH_ID_AT91SAM9G45:
		at91_soc_initdata.type = AT91_SOC_SAM9G45;
		if (cidr == ARCH_ID_AT91SAM9G45ES)
			at91_soc_initdata.subtype = AT91_SOC_SAM9G45ES;
		break;

	case ARCH_ID_AT91SAM9RL64:
		at91_soc_initdata.type = AT91_SOC_SAM9RL;
		at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_NONE;
		break;

	case ARCH_ID_AT91SAM9X5:
		at91_soc_initdata.type = AT91_SOC_SAM9X5;
		break;

	case ARCH_ID_AT91SAM9N12:
		at91_soc_initdata.type = AT91_SOC_SAM9N12;
		break;

	case ARCH_ID_SAMA5:
		at91_soc_initdata.exid = __raw_readl(AT91_IO_P2V(dbgu_base) + AT91_DBGU_EXID);
		if (at91_soc_initdata.exid & ARCH_EXID_SAMA5D3) {
			at91_soc_initdata.type = AT91_SOC_SAMA5D3;
		}
		break;
	}

	/* at91sam9g10 */
	if ((socid & ~AT91_CIDR_EXT) == ARCH_ID_AT91SAM9G10) {
		at91_soc_initdata.type = AT91_SOC_SAM9G10;
		at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_NONE;
	}
	/* at91sam9xe */
	else if ((cidr & AT91_CIDR_ARCH) == ARCH_FAMILY_AT91SAM9XE) {
		at91_soc_initdata.type = AT91_SOC_SAM9260;
		at91_soc_initdata.subtype = AT91_SOC_SAM9XE;
	}

	if (!at91_soc_is_detected())
		return;

	at91_soc_initdata.cidr = cidr;

	/* sub version of soc */
	if (!at91_soc_initdata.exid)
		at91_soc_initdata.exid = __raw_readl(AT91_IO_P2V(dbgu_base) + AT91_DBGU_EXID);

	if (at91_soc_initdata.type == AT91_SOC_SAM9G45) {
		switch (at91_soc_initdata.exid) {
		case ARCH_EXID_AT91SAM9M10:
			at91_soc_initdata.subtype = AT91_SOC_SAM9M10;
			break;
		case ARCH_EXID_AT91SAM9G46:
			at91_soc_initdata.subtype = AT91_SOC_SAM9G46;
			break;
		case ARCH_EXID_AT91SAM9M11:
			at91_soc_initdata.subtype = AT91_SOC_SAM9M11;
			break;
		}
	}

	if (at91_soc_initdata.type == AT91_SOC_SAM9X5) {
		switch (at91_soc_initdata.exid) {
		case ARCH_EXID_AT91SAM9G15:
			at91_soc_initdata.subtype = AT91_SOC_SAM9G15;
			break;
		case ARCH_EXID_AT91SAM9G35:
			at91_soc_initdata.subtype = AT91_SOC_SAM9G35;
			break;
		case ARCH_EXID_AT91SAM9X35:
			at91_soc_initdata.subtype = AT91_SOC_SAM9X35;
			break;
		case ARCH_EXID_AT91SAM9G25:
			at91_soc_initdata.subtype = AT91_SOC_SAM9G25;
			break;
		case ARCH_EXID_AT91SAM9X25:
			at91_soc_initdata.subtype = AT91_SOC_SAM9X25;
			break;
		}
	}

	if (at91_soc_initdata.type == AT91_SOC_SAMA5D3) {
		switch (at91_soc_initdata.exid) {
		case ARCH_EXID_SAMA5D31:
			at91_soc_initdata.subtype = AT91_SOC_SAMA5D31;
			break;
		case ARCH_EXID_SAMA5D33:
			at91_soc_initdata.subtype = AT91_SOC_SAMA5D33;
			break;
		case ARCH_EXID_SAMA5D34:
			at91_soc_initdata.subtype = AT91_SOC_SAMA5D34;
			break;
		case ARCH_EXID_SAMA5D35:
			at91_soc_initdata.subtype = AT91_SOC_SAMA5D35;
			break;
		case ARCH_EXID_SAMA5D36:
			at91_soc_initdata.subtype = AT91_SOC_SAMA5D36;
			break;
		}
	}
}

static void __init alt_soc_detect(u32 dbgu_base)
{
	u32 cidr, socid;

	/* SoC ID */
	cidr = __raw_readl(AT91_ALT_IO_P2V(dbgu_base) + AT91_DBGU_CIDR);
	socid = cidr & ~AT91_CIDR_VERSION;

	switch (socid) {
	case ARCH_ID_SAMA5:
		at91_soc_initdata.exid = __raw_readl(AT91_ALT_IO_P2V(dbgu_base) + AT91_DBGU_EXID);
		if (at91_soc_initdata.exid & ARCH_EXID_SAMA5D3) {
			at91_soc_initdata.type = AT91_SOC_SAMA5D3;
		} else if (at91_soc_initdata.exid & ARCH_EXID_SAMA5D4) {
			at91_soc_initdata.type = AT91_SOC_SAMA5D4;
		}
		break;
	}

	if (!at91_soc_is_detected())
		return;

	at91_soc_initdata.cidr = cidr;

	/* sub version of soc */
	if (!at91_soc_initdata.exid)
		at91_soc_initdata.exid = __raw_readl(AT91_ALT_IO_P2V(dbgu_base) + AT91_DBGU_EXID);

	if (at91_soc_initdata.type == AT91_SOC_SAMA5D4) {
		switch (at91_soc_initdata.exid) {
		case ARCH_EXID_SAMA5D41:
			at91_soc_initdata.subtype = AT91_SOC_SAMA5D41;
			break;
		case ARCH_EXID_SAMA5D42:
			at91_soc_initdata.subtype = AT91_SOC_SAMA5D42;
			break;
		case ARCH_EXID_SAMA5D43:
			at91_soc_initdata.subtype = AT91_SOC_SAMA5D43;
			break;
		case ARCH_EXID_SAMA5D44:
			at91_soc_initdata.subtype = AT91_SOC_SAMA5D44;
			break;
		}
	}
}

static const char *soc_name[] = {
	[AT91_SOC_RM9200]	= "at91rm9200",
	[AT91_SOC_SAM9260]	= "at91sam9260",
	[AT91_SOC_SAM9261]	= "at91sam9261",
	[AT91_SOC_SAM9263]	= "at91sam9263",
	[AT91_SOC_SAM9G10]	= "at91sam9g10",
	[AT91_SOC_SAM9G20]	= "at91sam9g20",
	[AT91_SOC_SAM9G45]	= "at91sam9g45",
	[AT91_SOC_SAM9RL]	= "at91sam9rl",
	[AT91_SOC_SAM9X5]	= "at91sam9x5",
	[AT91_SOC_SAM9N12]	= "at91sam9n12",
	[AT91_SOC_SAMA5D3]	= "sama5d3",
	[AT91_SOC_SAMA5D4]	= "sama5d4",
	[AT91_SOC_UNKNOWN]	= "Unknown",
};

const char *at91_get_soc_type(struct at91_socinfo *c)
{
	return soc_name[c->type];
}
EXPORT_SYMBOL(at91_get_soc_type);

static const char *soc_subtype_name[] = {
	[AT91_SOC_RM9200_BGA]	= "at91rm9200 BGA",
	[AT91_SOC_RM9200_PQFP]	= "at91rm9200 PQFP",
	[AT91_SOC_SAM9XE]	= "at91sam9xe",
	[AT91_SOC_SAM9G45ES]	= "at91sam9g45es",
	[AT91_SOC_SAM9M10]	= "at91sam9m10",
	[AT91_SOC_SAM9G46]	= "at91sam9g46",
	[AT91_SOC_SAM9M11]	= "at91sam9m11",
	[AT91_SOC_SAM9G15]	= "at91sam9g15",
	[AT91_SOC_SAM9G35]	= "at91sam9g35",
	[AT91_SOC_SAM9X35]	= "at91sam9x35",
	[AT91_SOC_SAM9G25]	= "at91sam9g25",
	[AT91_SOC_SAM9X25]	= "at91sam9x25",
	[AT91_SOC_SAMA5D31]	= "sama5d31",
	[AT91_SOC_SAMA5D33]	= "sama5d33",
	[AT91_SOC_SAMA5D34]	= "sama5d34",
	[AT91_SOC_SAMA5D35]	= "sama5d35",
	[AT91_SOC_SAMA5D36]	= "sama5d36",
	[AT91_SOC_SAMA5D41]	= "sama5d41",
	[AT91_SOC_SAMA5D42]	= "sama5d42",
	[AT91_SOC_SAMA5D43]	= "sama5d43",
	[AT91_SOC_SAMA5D44]	= "sama5d44",
	[AT91_SOC_SUBTYPE_NONE]	= "None",
	[AT91_SOC_SUBTYPE_UNKNOWN] = "Unknown",
};

const char *at91_get_soc_subtype(struct at91_socinfo *c)
{
	return soc_subtype_name[c->subtype];
}
EXPORT_SYMBOL(at91_get_soc_subtype);

void __init at91_map_io(void)
{
	/* Map peripherals */
	iotable_init(&at91_io_desc, 1);

	at91_soc_initdata.type = AT91_SOC_UNKNOWN;
	at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_UNKNOWN;

	soc_detect(AT91_BASE_DBGU0);
	if (!at91_soc_is_detected())
		soc_detect(AT91_BASE_DBGU1);

	if (!at91_soc_is_detected())
		panic(pr_fmt("Impossible to detect the SOC type"));

	pr_info("Detected soc type: %s\n",
		at91_get_soc_type(&at91_soc_initdata));
	if (at91_soc_initdata.subtype != AT91_SOC_SUBTYPE_NONE)
		pr_info("Detected soc subtype: %s\n",
			at91_get_soc_subtype(&at91_soc_initdata));
}

void __init at91_alt_map_io(void)
{
	/* Map peripherals */
	iotable_init(&at91_alt_io_desc, 1);

	at91_soc_initdata.type = AT91_SOC_UNKNOWN;
	at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_UNKNOWN;

	alt_soc_detect(AT91_BASE_DBGU2);
	if (!at91_soc_is_detected())
		panic("AT91: Impossible to detect the SOC type");

	pr_info("AT91: Detected soc type: %s\n",
		at91_get_soc_type(&at91_soc_initdata));
	if (at91_soc_initdata.subtype != AT91_SOC_SUBTYPE_NONE)
		pr_info("AT91: Detected soc subtype: %s\n",
			at91_get_soc_subtype(&at91_soc_initdata));
}
