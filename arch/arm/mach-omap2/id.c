/*
 * linux/arch/arm/mach-omap2/id.c
 *
 * OMAP2 CPU identification code
 *
 * Copyright (C) 2005 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/cputype.h>

#include <mach/control.h>
#include <mach/cpu.h>

#if defined(CONFIG_ARCH_OMAP2420)
#define TAP_BASE	io_p2v(0x48014000)
#elif defined(CONFIG_ARCH_OMAP2430)
#define TAP_BASE	io_p2v(0x4900A000)
#elif defined(CONFIG_ARCH_OMAP34XX)
#define TAP_BASE	io_p2v(0x4830A000)
#endif

#define OMAP_TAP_IDCODE		0x0204
#if defined(CONFIG_ARCH_OMAP34XX)
#define OMAP_TAP_PROD_ID	0x0210
#else
#define OMAP_TAP_PROD_ID	0x0208
#endif

#define OMAP_TAP_DIE_ID_0	0x0218
#define OMAP_TAP_DIE_ID_1	0x021C
#define OMAP_TAP_DIE_ID_2	0x0220
#define OMAP_TAP_DIE_ID_3	0x0224

/* system_rev fields for OMAP2 processors:
 *   CPU id bits     [31:16],
 *   CPU device type [15:12], (unprg,normal,POP)
 *   CPU revision    [11:08]
 *   CPU class bits  [07:00]
 */

struct omap_id {
	u16	hawkeye;	/* Silicon type (Hawkeye id) */
	u8	dev;		/* Device type from production_id reg */
	u32	type;		/* combined type id copied to system_rev */
};

/* Register values to detect the OMAP version */
static struct omap_id omap_ids[] __initdata = {
	{ .hawkeye = 0xb5d9, .dev = 0x0, .type = 0x24200000 },
	{ .hawkeye = 0xb5d9, .dev = 0x1, .type = 0x24201000 },
	{ .hawkeye = 0xb5d9, .dev = 0x2, .type = 0x24202000 },
	{ .hawkeye = 0xb5d9, .dev = 0x4, .type = 0x24220000 },
	{ .hawkeye = 0xb5d9, .dev = 0x8, .type = 0x24230000 },
	{ .hawkeye = 0xb68a, .dev = 0x0, .type = 0x24300000 },
};

static struct omap_chip_id omap_chip;

/**
 * omap_chip_is - test whether currently running OMAP matches a chip type
 * @oc: omap_chip_t to test against
 *
 * Test whether the currently-running OMAP chip matches the supplied
 * chip type 'oc'.  Returns 1 upon a match; 0 upon failure.
 */
int omap_chip_is(struct omap_chip_id oci)
{
	return (oci.oc & omap_chip.oc) ? 1 : 0;
}
EXPORT_SYMBOL(omap_chip_is);

static u32 __init read_tap_reg(int reg)
{
	unsigned int regval = 0;
	u32 cpuid;

	/* Reading the IDCODE register on 3430 ES1 results in a
	 * data abort as the register is not exposed on the OCP
	 * Hence reading the Cortex Rev
	 */
	cpuid = read_cpuid(CPUID_ID);

	/* If the processor type is Cortex-A8 and the revision is 0x0
	 * it means its Cortex r0p0 which is 3430 ES1
	 */
	if ((((cpuid >> 4) & 0xFFF) == 0xC08) && ((cpuid & 0xF) == 0x0)) {
		switch (reg) {
		case OMAP_TAP_IDCODE  : regval = 0x0B7AE02F; break;
		/* Making DevType as 0xF in ES1 to differ from ES2 */
		case OMAP_TAP_PROD_ID : regval = 0x000F00F0; break;
		case OMAP_TAP_DIE_ID_0: regval = 0x01000000; break;
		case OMAP_TAP_DIE_ID_1: regval = 0x1012d687; break;
		case OMAP_TAP_DIE_ID_2:	regval = 0x00000000; break;
		case OMAP_TAP_DIE_ID_3:	regval = 0x2d2c0000; break;
		}
	} else
		regval = __raw_readl(TAP_BASE + reg);

	return regval;

}

/*
 * _set_system_rev - set the system_rev global based on current OMAP chip type
 *
 * Set the system_rev global.  This is primarily used by the cpu_is_omapxxxx()
 * macros.
 */
static void __init _set_system_rev(u32 type, u8 rev)
{
	u32 i, ctrl_status;

	/*
	 * system_rev encoding is as follows
	 * system_rev & 0xff000000 -> Omap Class (24xx/34xx)
	 * system_rev & 0xfff00000 -> Omap Sub Class (242x/343x)
	 * system_rev & 0xffff0000 -> Omap type (2420/2422/2423/2430/3430)
	 * system_rev & 0x0000f000 -> Silicon revision (ES1, ES2 )
	 * system_rev & 0x00000700 -> Device Type ( EMU/HS/GP/BAD )
	 * system_rev & 0x000000c0 -> IDCODE revision[6:7]
	 * system_rev & 0x0000003f -> sys_boot[0:5]
	 */
	/* Embedding the ES revision info in type field */
	system_rev = type;
	/* Also add IDCODE revision info only two lower bits */
	system_rev |= ((rev & 0x3) << 6);

	/* Add in the device type and sys_boot fields (see above) */
	if (cpu_is_omap24xx()) {
		i = OMAP24XX_CONTROL_STATUS;
	} else if (cpu_is_omap343x()) {
		i = OMAP343X_CONTROL_STATUS;
	} else {
		printk(KERN_ERR "id: unknown CPU type\n");
		BUG();
	}
	ctrl_status = omap_ctrl_readl(i);
	system_rev |= (ctrl_status & (OMAP2_SYSBOOT_5_MASK |
				      OMAP2_SYSBOOT_4_MASK |
				      OMAP2_SYSBOOT_3_MASK |
				      OMAP2_SYSBOOT_2_MASK |
				      OMAP2_SYSBOOT_1_MASK |
				      OMAP2_SYSBOOT_0_MASK));
	system_rev |= (ctrl_status & OMAP2_DEVICETYPE_MASK);
}


/*
 * _set_omap_chip - set the omap_chip global based on OMAP chip type
 *
 * Build the omap_chip bits.  This variable is used by powerdomain and
 * clockdomain code to indicate whether structures are applicable for
 * the current OMAP chip type by ANDing it against a 'platform' bitfield
 * in the structure.
 */
static void __init _set_omap_chip(void)
{
	if (cpu_is_omap343x()) {

		omap_chip.oc = CHIP_IS_OMAP3430;
		if (is_sil_rev_equal_to(OMAP3430_REV_ES1_0))
			omap_chip.oc |= CHIP_IS_OMAP3430ES1;
		else if (is_sil_rev_greater_than(OMAP3430_REV_ES1_0))
			omap_chip.oc |= CHIP_IS_OMAP3430ES2;

	} else if (cpu_is_omap243x()) {

		/* Currently only supports 2430ES2.1 and 2430-all */
		omap_chip.oc |= CHIP_IS_OMAP2430;

	} else if (cpu_is_omap242x()) {

		/* Currently only supports 2420ES2.1.1 and 2420-all */
		omap_chip.oc |= CHIP_IS_OMAP2420;

	} else {

		/* Current CPU not supported by this code. */
		printk(KERN_WARNING "OMAP chip type code does not yet support "
		       "this CPU type.\n");
		WARN_ON(1);

	}

}

void __init omap2_check_revision(void)
{
	int i, j;
	u32 idcode;
	u32 prod_id;
	u16 hawkeye;
	u8  dev_type;
	u8  rev;

	idcode = read_tap_reg(OMAP_TAP_IDCODE);
	prod_id = read_tap_reg(OMAP_TAP_PROD_ID);
	hawkeye = (idcode >> 12) & 0xffff;
	rev = (idcode >> 28) & 0x0f;
	dev_type = (prod_id >> 16) & 0x0f;

	pr_debug("OMAP_TAP_IDCODE 0x%08x REV %i HAWKEYE 0x%04x MANF %03x\n",
		 idcode, rev, hawkeye, (idcode >> 1) & 0x7ff);
	pr_debug("OMAP_TAP_DIE_ID_0: 0x%08x\n",
		 read_tap_reg(OMAP_TAP_DIE_ID_0));
	pr_debug("OMAP_TAP_DIE_ID_1: 0x%08x DEV_REV: %i\n",
		 read_tap_reg(OMAP_TAP_DIE_ID_1),
		 (read_tap_reg(OMAP_TAP_DIE_ID_1) >> 28) & 0xf);
	pr_debug("OMAP_TAP_DIE_ID_2: 0x%08x\n",
		 read_tap_reg(OMAP_TAP_DIE_ID_2));
	pr_debug("OMAP_TAP_DIE_ID_3: 0x%08x\n",
		 read_tap_reg(OMAP_TAP_DIE_ID_3));
	pr_debug("OMAP_TAP_PROD_ID_0: 0x%08x DEV_TYPE: %i\n",
		 prod_id, dev_type);

	/*
	 * Detection for 34xx ES2.0 and above can be done with just
	 * hawkeye and rev. See TRM 1.5.2 Device Identification.
	 * Note that rev cannot be used directly as ES1.0 uses value 0.
	 */
	if (hawkeye == 0xb7ae) {
		system_rev = 0x34300000 | ((1 + rev) << 12);
		pr_info("OMAP%04x ES2.%i\n", system_rev >> 16, rev);
		_set_omap_chip();
		return;
	}

	/* Check hawkeye ids */
	for (i = 0; i < ARRAY_SIZE(omap_ids); i++) {
		if (hawkeye == omap_ids[i].hawkeye)
			break;
	}

	if (i == ARRAY_SIZE(omap_ids)) {
		printk(KERN_ERR "Unknown OMAP CPU id\n");
		return;
	}

	for (j = i; j < ARRAY_SIZE(omap_ids); j++) {
		if (dev_type == omap_ids[j].dev)
			break;
	}

	if (j == ARRAY_SIZE(omap_ids)) {
		printk(KERN_ERR "Unknown OMAP device type. "
				"Handling it as OMAP%04x\n",
				omap_ids[i].type >> 16);
		j = i;
	}

	_set_system_rev(omap_ids[j].type, rev);

	_set_omap_chip();

	pr_info("OMAP%04x", system_rev >> 16);
	if ((system_rev >> 8) & 0x0f)
		pr_info("ES%x", (system_rev >> 12) & 0xf);
	pr_info("\n");

}

