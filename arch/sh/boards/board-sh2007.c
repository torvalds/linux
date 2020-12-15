// SPDX-License-Identifier: GPL-2.0
/*
 * SH-2007 board support.
 *
 * Copyright (C) 2003, 2004  SUGIOKA Toshinobu
 * Copyright (C) 2010  Hitoshi Mitake <mitake@dcl.info.waseda.ac.jp>
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/smsc911x.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/io.h>
#include <asm/machvec.h>
#include <mach/sh2007.h>

/* Dummy supplies, where voltage doesn't matter */
static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x.0"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x.0"),
	REGULATOR_SUPPLY("vddvario", "smsc911x.1"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x.1"),
};

struct smsc911x_platform_config smc911x_info = {
	.flags		= SMSC911X_USE_32BIT,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct resource smsc9118_0_resources[] = {
	[0] = {
		.start	= SMC0_BASE,
		.end	= SMC0_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x240),
		.end	= evt2irq(0x240),
		.flags	= IORESOURCE_IRQ,
	}
};

static struct resource smsc9118_1_resources[] = {
	[0] = {
		.start	= SMC1_BASE,
		.end	= SMC1_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x280),
		.end	= evt2irq(0x280),
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device smsc9118_0_device = {
	.name		= "smsc911x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smsc9118_0_resources),
	.resource	= smsc9118_0_resources,
	.dev = {
		.platform_data = &smc911x_info,
	},
};

static struct platform_device smsc9118_1_device = {
	.name		= "smsc911x",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(smsc9118_1_resources),
	.resource	= smsc9118_1_resources,
	.dev = {
		.platform_data = &smc911x_info,
	},
};

static struct resource cf_resources[] = {
	[0] = {
		.start	= CF_BASE + CF_OFFSET,
		.end	= CF_BASE + CF_OFFSET + 0x0f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= CF_BASE + CF_OFFSET + 0x206,
		.end	= CF_BASE + CF_OFFSET + 0x20f,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= evt2irq(0x2c0),
		.end	= evt2irq(0x2c0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device cf_device  = {
	.name		= "pata_platform",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(cf_resources),
	.resource	= cf_resources,
};

static struct platform_device *sh2007_devices[] __initdata = {
	&smsc9118_0_device,
	&smsc9118_1_device,
	&cf_device,
};

static int __init sh2007_io_init(void)
{
	regulator_register_fixed(0, dummy_supplies, ARRAY_SIZE(dummy_supplies));

	platform_add_devices(sh2007_devices, ARRAY_SIZE(sh2007_devices));
	return 0;
}
subsys_initcall(sh2007_io_init);

static void __init sh2007_init_irq(void)
{
	plat_irq_setup_pins(IRQ_MODE_IRQ);
}

/*
 * Initialize the board
 */
static void __init sh2007_setup(char **cmdline_p)
{
	pr_info("SH-2007 Setup...");

	/* setup wait control registers for area 5 */
	__raw_writel(CS5BCR_D, CS5BCR);
	__raw_writel(CS5WCR_D, CS5WCR);
	__raw_writel(CS5PCR_D, CS5PCR);

	pr_cont(" done.\n");
}

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_sh2007 __initmv = {
	.mv_setup		= sh2007_setup,
	.mv_name		= "sh2007",
	.mv_init_irq		= sh2007_init_irq,
};
