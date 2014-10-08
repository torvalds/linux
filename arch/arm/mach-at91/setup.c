/*
 * Copyright (C) 2007 Atmel Corporation.
 * Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * Under GPLv2
 */

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

#include "at91_shdwc.h"
#include "soc.h"
#include "generic.h"
#include "pm.h"

struct at91_init_soc __initdata at91_boot_soc;

struct at91_socinfo at91_soc_initdata;
EXPORT_SYMBOL(at91_soc_initdata);

void __init at91rm9200_set_type(int type)
{
	if (type == ARCH_REVISON_9200_PQFP)
		at91_soc_initdata.subtype = AT91_SOC_RM9200_PQFP;
	else
		at91_soc_initdata.subtype = AT91_SOC_RM9200_BGA;

	pr_info("AT91: filled in soc subtype: %s\n",
		at91_get_soc_subtype(&at91_soc_initdata));
}

void __init at91_init_irq_default(void)
{
	at91_init_interrupts(at91_boot_soc.default_irq_priority);
}

void __init at91_init_interrupts(unsigned int *priority)
{
	/* Initialize the AIC interrupt controller */
	if (IS_ENABLED(CONFIG_OLD_IRQ_AT91))
		at91_aic_init(priority, at91_boot_soc.extern_irq);

	/* Enable GPIO interrupts */
	at91_gpio_irq_setup();
}

void __iomem *at91_ramc_base[2];
EXPORT_SYMBOL_GPL(at91_ramc_base);

void __init at91_ioremap_ramc(int id, u32 addr, u32 size)
{
	if (id < 0 || id > 1) {
		pr_emerg("Wrong RAM controller id (%d), cannot continue\n", id);
		BUG();
	}
	at91_ramc_base[id] = ioremap(addr, size);
	if (!at91_ramc_base[id])
		panic("Impossible to ioremap ramc.%d 0x%x\n", id, addr);
}

static struct map_desc sram_desc[2] __initdata;

void __init at91_init_sram(int bank, unsigned long base, unsigned int length)
{
	struct map_desc *desc = &sram_desc[bank];

	desc->virtual = (unsigned long)AT91_IO_VIRT_BASE - length;
	if (bank > 0)
		desc->virtual -= sram_desc[bank - 1].length;

	desc->pfn = __phys_to_pfn(base);
	desc->length = length;
	desc->type = MT_MEMORY_RWX_NONCACHED;

	pr_info("AT91: sram at 0x%lx of 0x%x mapped at 0x%lx\n",
		base, length, desc->virtual);

	iotable_init(desc, 1);
}

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
		at91_boot_soc = at91rm9200_soc;
		break;

	case ARCH_ID_AT91SAM9260:
		at91_soc_initdata.type = AT91_SOC_SAM9260;
		at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_NONE;
		at91_boot_soc = at91sam9260_soc;
		break;

	case ARCH_ID_AT91SAM9261:
		at91_soc_initdata.type = AT91_SOC_SAM9261;
		at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_NONE;
		at91_boot_soc = at91sam9261_soc;
		break;

	case ARCH_ID_AT91SAM9263:
		at91_soc_initdata.type = AT91_SOC_SAM9263;
		at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_NONE;
		at91_boot_soc = at91sam9263_soc;
		break;

	case ARCH_ID_AT91SAM9G20:
		at91_soc_initdata.type = AT91_SOC_SAM9G20;
		at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_NONE;
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
		at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_NONE;
		at91_boot_soc = at91sam9rl_soc;
		break;

	case ARCH_ID_AT91SAM9X5:
		at91_soc_initdata.type = AT91_SOC_SAM9X5;
		at91_boot_soc = at91sam9x5_soc;
		break;

	case ARCH_ID_AT91SAM9N12:
		at91_soc_initdata.type = AT91_SOC_SAM9N12;
		at91_boot_soc = at91sam9n12_soc;
		break;

	case ARCH_ID_SAMA5:
		at91_soc_initdata.exid = __raw_readl(AT91_IO_P2V(dbgu_base) + AT91_DBGU_EXID);
		if (at91_soc_initdata.exid & ARCH_EXID_SAMA5D3) {
			at91_soc_initdata.type = AT91_SOC_SAMA5D3;
			at91_boot_soc = sama5d3_soc;
		}
		break;
	}

	/* at91sam9g10 */
	if ((socid & ~AT91_CIDR_EXT) == ARCH_ID_AT91SAM9G10) {
		at91_soc_initdata.type = AT91_SOC_SAM9G10;
		at91_soc_initdata.subtype = AT91_SOC_SUBTYPE_NONE;
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
			at91_boot_soc = sama5d3_soc;
		} else if (at91_soc_initdata.exid & ARCH_EXID_SAMA5D4) {
			at91_soc_initdata.type = AT91_SOC_SAMA5D4;
			at91_boot_soc = sama5d4_soc;
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
		panic("AT91: Impossible to detect the SOC type");

	pr_info("AT91: Detected soc type: %s\n",
		at91_get_soc_type(&at91_soc_initdata));
	if (at91_soc_initdata.subtype != AT91_SOC_SUBTYPE_NONE)
		pr_info("AT91: Detected soc subtype: %s\n",
			at91_get_soc_subtype(&at91_soc_initdata));

	if (!at91_soc_is_enabled())
		panic("AT91: Soc not enabled");

	if (at91_boot_soc.map_io)
		at91_boot_soc.map_io();
}

void __iomem *at91_shdwc_base = NULL;

static void at91sam9_poweroff(void)
{
	at91_shdwc_write(AT91_SHDW_CR, AT91_SHDW_KEY | AT91_SHDW_SHDW);
}

void __init at91_ioremap_shdwc(u32 base_addr)
{
	at91_shdwc_base = ioremap(base_addr, 16);
	if (!at91_shdwc_base)
		panic("Impossible to ioremap at91_shdwc_base\n");
	pm_power_off = at91sam9_poweroff;
}

void __iomem *at91_rstc_base;

void __init at91_ioremap_rstc(u32 base_addr)
{
	at91_rstc_base = ioremap(base_addr, 16);
	if (!at91_rstc_base)
		panic("Impossible to ioremap at91_rstc_base\n");
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

	if (!at91_soc_is_enabled())
		panic("AT91: Soc not enabled");

	if (at91_boot_soc.map_io)
		at91_boot_soc.map_io();
}

void __iomem *at91_matrix_base;
EXPORT_SYMBOL_GPL(at91_matrix_base);

void __init at91_ioremap_matrix(u32 base_addr)
{
	at91_matrix_base = ioremap(base_addr, 512);
	if (!at91_matrix_base)
		panic("Impossible to ioremap at91_matrix_base\n");
}

#if defined(CONFIG_OF) && !defined(CONFIG_ARCH_AT91X40)
static struct of_device_id rstc_ids[] = {
	{ .compatible = "atmel,at91sam9260-rstc", .data = at91sam9_alt_restart },
	{ .compatible = "atmel,at91sam9g45-rstc", .data = at91sam9g45_restart },
	{ /*sentinel*/ }
};

static void at91_dt_rstc(void)
{
	struct device_node *np;
	const struct of_device_id *of_id;

	np = of_find_matching_node(NULL, rstc_ids);
	if (!np)
		panic("unable to find compatible rstc node in dtb\n");

	at91_rstc_base = of_iomap(np, 0);
	if (!at91_rstc_base)
		panic("unable to map rstc cpu registers\n");

	of_id = of_match_node(rstc_ids, np);
	if (!of_id)
		panic("AT91: rtsc no restart function available\n");

	arm_pm_restart = of_id->data;

	of_node_put(np);
}

static struct of_device_id ramc_ids[] = {
	{ .compatible = "atmel,at91rm9200-sdramc", .data = at91rm9200_standby },
	{ .compatible = "atmel,at91sam9260-sdramc", .data = at91sam9_sdram_standby },
	{ .compatible = "atmel,at91sam9g45-ddramc", .data = at91_ddr_standby },
	{ /*sentinel*/ }
};

static void at91_dt_ramc(void)
{
	struct device_node *np;
	const struct of_device_id *of_id;

	np = of_find_matching_node(NULL, ramc_ids);
	if (!np)
		panic("unable to find compatible ram controller node in dtb\n");

	at91_ramc_base[0] = of_iomap(np, 0);
	if (!at91_ramc_base[0])
		panic("unable to map ramc[0] cpu registers\n");
	/* the controller may have 2 banks */
	at91_ramc_base[1] = of_iomap(np, 1);

	of_id = of_match_node(ramc_ids, np);
	if (!of_id)
		pr_warn("AT91: ramc no standby function available\n");
	else
		at91_pm_set_standby(of_id->data);

	of_node_put(np);
}

static struct of_device_id shdwc_ids[] = {
	{ .compatible = "atmel,at91sam9260-shdwc", },
	{ .compatible = "atmel,at91sam9rl-shdwc", },
	{ .compatible = "atmel,at91sam9x5-shdwc", },
	{ /*sentinel*/ }
};

static const char *shdwc_wakeup_modes[] = {
	[AT91_SHDW_WKMODE0_NONE]	= "none",
	[AT91_SHDW_WKMODE0_HIGH]	= "high",
	[AT91_SHDW_WKMODE0_LOW]		= "low",
	[AT91_SHDW_WKMODE0_ANYLEVEL]	= "any",
};

const int at91_dtget_shdwc_wakeup_mode(struct device_node *np)
{
	const char *pm;
	int err, i;

	err = of_property_read_string(np, "atmel,wakeup-mode", &pm);
	if (err < 0)
		return AT91_SHDW_WKMODE0_ANYLEVEL;

	for (i = 0; i < ARRAY_SIZE(shdwc_wakeup_modes); i++)
		if (!strcasecmp(pm, shdwc_wakeup_modes[i]))
			return i;

	return -ENODEV;
}

static void at91_dt_shdwc(void)
{
	struct device_node *np;
	int wakeup_mode;
	u32 reg;
	u32 mode = 0;

	np = of_find_matching_node(NULL, shdwc_ids);
	if (!np) {
		pr_debug("AT91: unable to find compatible shutdown (shdwc) controller node in dtb\n");
		return;
	}

	at91_shdwc_base = of_iomap(np, 0);
	if (!at91_shdwc_base)
		panic("AT91: unable to map shdwc cpu registers\n");

	wakeup_mode = at91_dtget_shdwc_wakeup_mode(np);
	if (wakeup_mode < 0) {
		pr_warn("AT91: shdwc unknown wakeup mode\n");
		goto end;
	}

	if (!of_property_read_u32(np, "atmel,wakeup-counter", &reg)) {
		if (reg > AT91_SHDW_CPTWK0_MAX) {
			pr_warn("AT91: shdwc wakeup counter 0x%x > 0x%x reduce it to 0x%x\n",
				reg, AT91_SHDW_CPTWK0_MAX, AT91_SHDW_CPTWK0_MAX);
			reg = AT91_SHDW_CPTWK0_MAX;
		}
		mode |= AT91_SHDW_CPTWK0_(reg);
	}

	if (of_property_read_bool(np, "atmel,wakeup-rtc-timer"))
			mode |= AT91_SHDW_RTCWKEN;

	if (of_property_read_bool(np, "atmel,wakeup-rtt-timer"))
			mode |= AT91_SHDW_RTTWKEN;

	at91_shdwc_write(AT91_SHDW_MR, wakeup_mode | mode);

end:
	pm_power_off = at91sam9_poweroff;

	of_node_put(np);
}

void __init at91rm9200_dt_initialize(void)
{
	at91_dt_ramc();

	/* Init clock subsystem */
	at91_dt_clock_init();

	/* Register the processor-specific clocks */
	if (at91_boot_soc.register_clocks)
		at91_boot_soc.register_clocks();

	at91_boot_soc.init();
}

void __init at91_dt_initialize(void)
{
	at91_dt_rstc();
	at91_dt_ramc();
	at91_dt_shdwc();

	/* Init clock subsystem */
	at91_dt_clock_init();

	/* Register the processor-specific clocks */
	if (at91_boot_soc.register_clocks)
		at91_boot_soc.register_clocks();

	if (at91_boot_soc.init)
		at91_boot_soc.init();
}
#endif

void __init at91_initialize(unsigned long main_clock)
{
	at91_boot_soc.ioremap_registers();

	/* Init clock subsystem */
	at91_clock_init(main_clock);

	/* Register the processor-specific clocks */
	at91_boot_soc.register_clocks();

	at91_boot_soc.init();

	pinctrl_provide_dummies();
}
