/*
 * Copyright (C) 2008-2009 ST-Ericsson
 *
 * Author: Srinidhi KASAGAR <srinidhi.kasagar@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/mach/map.h>
#include <mach/hardware.h>
#include <mach/setup.h>
#include <mach/devices.h>

#include "devices-db8500.h"

static struct platform_device *platform_devs[] __initdata = {
	&u8500_dma40_device,
};

/* minimum static i/o mapping required to boot U8500 platforms */
static struct map_desc u8500_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_PRCMU_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO0_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO1_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO2_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO3_BASE, SZ_4K),
	__MEM_DEV_DESC(U8500_BOOT_ROM_BASE, SZ_1M),
};

static struct map_desc u8500_ed_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_MTU0_BASE_ED, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST7_BASE_ED, SZ_8K),
};

static struct map_desc u8500_v1_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_MTU0_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_PRCMU_TCDM_BASE_V1, SZ_4K),
};

static struct map_desc u8500_v2_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_PRCMU_TCDM_BASE, SZ_4K),
};

/*
 * Functions to differentiate between later ASICs
 * We look into the end of the ROM to locate the hardcoded ASIC ID.
 * This is only needed to differentiate between minor revisions and
 * process variants of an ASIC, the major revisions are encoded in
 * the cpuid.
 */
#define U8500_ASIC_ID_LOC_ED_V1	(U8500_BOOT_ROM_BASE + 0x1FFF4)
#define U8500_ASIC_ID_LOC_V2	(U8500_BOOT_ROM_BASE + 0x1DBF4)
#define U8500_ASIC_REV_ED	0x01
#define U8500_ASIC_REV_V10	0xA0
#define U8500_ASIC_REV_V11	0xA1
#define U8500_ASIC_REV_V20	0xB0

/**
 * struct db8500_asic_id - fields of the ASIC ID
 * @process: the manufacturing process, 0x40 is 40 nm
 *  0x00 is "standard"
 * @partnumber: hithereto 0x8500 for DB8500
 * @revision: version code in the series
 * This field definion is not formally defined but makes
 * sense.
 */
struct db8500_asic_id {
	u8 process;
	u16 partnumber;
	u8 revision;
};

/* This isn't going to change at runtime */
static struct db8500_asic_id db8500_id;

static void __init get_db8500_asic_id(void)
{
	u32 asicid;

	if (cpu_is_u8500v1() || cpu_is_u8500ed())
		asicid = readl(__io_address(U8500_ASIC_ID_LOC_ED_V1));
	else if (cpu_is_u8500v2())
		asicid = readl(__io_address(U8500_ASIC_ID_LOC_V2));
	else
		BUG();

	db8500_id.process = (asicid >> 24);
	db8500_id.partnumber = (asicid >> 16) & 0xFFFFU;
	db8500_id.revision = asicid & 0xFFU;
}

bool cpu_is_u8500v10(void)
{
	return (db8500_id.revision == U8500_ASIC_REV_V10);
}

bool cpu_is_u8500v11(void)
{
	return (db8500_id.revision == U8500_ASIC_REV_V11);
}

bool cpu_is_u8500v20(void)
{
	return (db8500_id.revision == U8500_ASIC_REV_V20);
}

void __init u8500_map_io(void)
{
	ux500_map_io();

	iotable_init(u8500_io_desc, ARRAY_SIZE(u8500_io_desc));

	if (cpu_is_u8500ed())
		iotable_init(u8500_ed_io_desc, ARRAY_SIZE(u8500_ed_io_desc));
	else if (cpu_is_u8500v1())
		iotable_init(u8500_v1_io_desc, ARRAY_SIZE(u8500_v1_io_desc));
	else if (cpu_is_u8500v2())
		iotable_init(u8500_v2_io_desc, ARRAY_SIZE(u8500_v2_io_desc));

	/* Read out the ASIC ID as early as we can */
	get_db8500_asic_id();
}

static resource_size_t __initdata db8500_gpio_base[] = {
	U8500_GPIOBANK0_BASE,
	U8500_GPIOBANK1_BASE,
	U8500_GPIOBANK2_BASE,
	U8500_GPIOBANK3_BASE,
	U8500_GPIOBANK4_BASE,
	U8500_GPIOBANK5_BASE,
	U8500_GPIOBANK6_BASE,
	U8500_GPIOBANK7_BASE,
	U8500_GPIOBANK8_BASE,
};

static void __init db8500_add_gpios(void)
{
	struct nmk_gpio_platform_data pdata = {
		/* No custom data yet */
	};

	dbx500_add_gpios(ARRAY_AND_SIZE(db8500_gpio_base),
			 IRQ_DB8500_GPIO0, &pdata);
}

/*
 * This function is called from the board init
 */
void __init u8500_init_devices(void)
{
	/* Display some ASIC boilerplate */
	pr_info("DB8500: process: %02x, revision ID: 0x%02x\n",
		db8500_id.process, db8500_id.revision);
	if (cpu_is_u8500ed())
		pr_info("DB8500: Early Drop (ED)\n");
	else if (cpu_is_u8500v10())
		pr_info("DB8500: version 1.0\n");
	else if (cpu_is_u8500v11())
		pr_info("DB8500: version 1.1\n");
	else if (cpu_is_u8500v20())
		pr_info("DB8500: version 2.0\n");
	else
		pr_warning("ASIC: UNKNOWN SILICON VERSION!\n");

	if (cpu_is_u8500ed())
		dma40_u8500ed_fixup();

	db8500_add_rtc();
	db8500_add_gpios();

	platform_add_devices(platform_devs, ARRAY_SIZE(platform_devs));

	return ;
}
