/*
 * Copyright (C) 2007 Atmel Corporation.
 * Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * Under GPLv2
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/mm.h>

#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/cpu.h>
#include <mach/at91_dbgu.h>
#include <mach/at91_pmc.h>

#include "soc.h"
#include "generic.h"

struct at91_init_soc __initdata at91_boot_soc;

struct at91_socinfo at91_soc_initdata;
EXPORT_SYMBOL(at91_soc_initdata);

void __init at91rm9200_set_type(int type)
{
	if (type == ARCH_REVISON_9200_PQFP)
		at91_soc_initdata.subtype = AT91_SOC_RM9200_BGA;
	else
		at91_soc_initdata.subtype = AT91_SOC_RM9200_PQFP;
}

void __init at91_init_irq_default(void)
{
	at91_init_interrupts(at91_boot_soc.default_irq_priority);
}

void __init at91_init_interrupts(unsigned int *priority)
{
	/* Initialize the AIC interrupt controller */
	at91_aic_init(priority);

	/* Enable GPIO interrupts */
	at91_gpio_irq_setup();
}

static struct map_desc sram_desc[2] __initdata;

void __init at91_init_sram(int bank, unsigned long base, unsigned int length)
{
	struct map_desc *desc = &sram_desc[bank];

	desc->virtual = AT91_IO_VIRT_BASE - length;
	if (bank > 0)
		desc->virtual -= sram_desc[bank - 1].length;

	desc->pfn = __phys_to_pfn(base);
	desc->length = length;
	desc->type = MT_DEVICE;

	pr_info("AT91: sram at 0x%lx of 0x%x mapped at 0x%lx\n",
		base, length, desc->virtual);

	iotable_init(desc, 1);
}

static struct map_desc at91_io_desc __initdata = {
	.virtual	= AT91_VA_BASE_SYS,
	.pfn		= __phys_to_pfn(AT91_BASE_SYS),
	.length		= SZ_16K,
	.type		= MT_DEVICE,
};

#define AT91_DBGU0	0xfffff200
#define AT91_DBGU1	0xffffee00

static void __init soc_detect(u32 dbgu_base)
{
	u32 cidr, socid;

	cidr = __raw_readl(AT91_IO_P2V(dbgu_base) + AT91_DBGU_CIDR);
	socid = cidr & ~AT91_CIDR_VERSION;

	switch (socid) {
	case ARCH_ID_AT91CAP9: {
#ifdef CONFIG_AT91_PMC_UNIT
		u32 pmc_ver = at91_sys_read(AT91_PMC_VER);

		if (pmc_ver == ARCH_REVISION_CAP9_B)
			at91_soc_initdata.subtype = AT91_SOC_CAP9_REV_B;
		else if (pmc_ver == ARCH_REVISION_CAP9_C)
			at91_soc_initdata.subtype = AT91_SOC_CAP9_REV_C;
#endif
		at91_soc_initdata.type = AT91_SOC_CAP9;
		at91_boot_soc = at91cap9_soc;
		break;
	}

	case ARCH_ID_AT91RM9200:
		at91_soc_initdata.type = AT91_SOC_RM9200;
		at91_boot_soc = at91rm9200_soc;
		break;

	case ARCH_ID_AT91SAM9260:
		at91_soc_initdata.type = AT91_SOC_SAM9260;
		at91_boot_soc = at91sam9260_soc;
		break;

	case ARCH_ID_AT91SAM9261:
		at91_soc_initdata.type = AT91_SOC_SAM9261;
		at91_boot_soc = at91sam9261_soc;
		break;

	case ARCH_ID_AT91SAM9263:
		at91_soc_initdata.type = AT91_SOC_SAM9263;
		at91_boot_soc = at91sam9263_soc;
		break;

	case ARCH_ID_AT91SAM9G20:
		at91_soc_initdata.type = AT91_SOC_SAM9G20;
		at91_boot_soc = at91sam9260_soc;
		break;

	case ARCH_ID_AT91SAM9G45:
		at91_soc_initdata.type = AT91_SOC_SAM9G45;
		if (cidr == ARCH_ID_AT91SAM9G45ES)
			at91_soc_initdata.subtype = AT91_SOC_SAM9G45ES;
		at91_boot_soc = at91sam9g45_soc;
		break;

	case ARCH_ID_AT91SAM9RL64:
		at91_soc_initdata.type = AT91_SOC_SAM9RL;
		at91_boot_soc = at91sam9rl_soc;
		break;

	case ARCH_ID_AT91SAM9X5:
		at91_soc_initdata.type = AT91_SOC_SAM9X5;
		at91_boot_soc = at91sam9x5_soc;
		break;
	}

	/* at91sam9g10 */
	if ((cidr & ~AT91_CIDR_EXT) == ARCH_ID_AT91SAM9G10) {
		at91_soc_initdata.type = AT91_SOC_SAM9G10;
		at91_boot_soc = at91sam9261_soc;
	}
	/* at91sam9xe */
	else if ((cidr & AT91_CIDR_ARCH) == ARCH_FAMILY_AT91SAM9XE) {
		at91_soc_initdata.type = AT91_SOC_SAM9260;
		at91_soc_initdata.subtype = AT91_SOC_SAM9XE;
		at91_boot_soc = at91sam9260_soc;
	}

	if (!at91_soc_is_detected())
		return;

	at91_soc_initdata.cidr = cidr;

	/* sub version of soc */
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
}

static const char *soc_name[] = {
	[AT91_SOC_RM9200]	= "at91rm9200",
	[AT91_SOC_CAP9]		= "at91cap9",
	[AT91_SOC_SAM9260]	= "at91sam9260",
	[AT91_SOC_SAM9261]	= "at91sam9261",
	[AT91_SOC_SAM9263]	= "at91sam9263",
	[AT91_SOC_SAM9G10]	= "at91sam9g10",
	[AT91_SOC_SAM9G20]	= "at91sam9g20",
	[AT91_SOC_SAM9G45]	= "at91sam9g45",
	[AT91_SOC_SAM9RL]	= "at91sam9rl",
	[AT91_SOC_SAM9X5]	= "at91sam9x5",
	[AT91_SOC_NONE]		= "Unknown"
};

const char *at91_get_soc_type(struct at91_socinfo *c)
{
	return soc_name[c->type];
}
EXPORT_SYMBOL(at91_get_soc_type);

static const char *soc_subtype_name[] = {
	[AT91_SOC_RM9200_BGA]	= "at91rm9200 BGA",
	[AT91_SOC_RM9200_PQFP]	= "at91rm9200 PQFP",
	[AT91_SOC_CAP9_REV_B]	= "at91cap9 revB",
	[AT91_SOC_CAP9_REV_C]	= "at91cap9 revC",
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
	[AT91_SOC_SUBTYPE_NONE]	= "Unknown"
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

	at91_soc_initdata.type = AT91_SOC_NONE;
	at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_NONE;

	soc_detect(AT91_DBGU0);
	if (!at91_soc_is_detected())
		soc_detect(AT91_DBGU1);

	if (!at91_soc_is_detected())
		panic("AT91: Impossible to detect the SOC type");

	pr_info("AT91: Detected soc type: %s\n",
		at91_get_soc_type(&at91_soc_initdata));
	pr_info("AT91: Detected soc subtype: %s\n",
		at91_get_soc_subtype(&at91_soc_initdata));

	if (!at91_soc_is_enabled())
		panic("AT91: Soc not enabled");

	if (at91_boot_soc.map_io)
		at91_boot_soc.map_io();
}

void __init at91_initialize(unsigned long main_clock)
{
	/* Init clock subsystem */
	at91_clock_init(main_clock);

	/* Register the processor-specific clocks */
	at91_boot_soc.register_clocks();

	at91_boot_soc.init();
}
