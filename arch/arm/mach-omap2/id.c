/*
 * linux/arch/arm/mach-omap2/id.c
 *
 * OMAP2 CPU identification code
 *
 * Copyright (C) 2005 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
 *
 * Copyright (C) 2009-11 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
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

#include "common.h"
#include <plat/cpu.h>

#include <mach/id.h>

#include "control.h"

static unsigned int omap_revision;
static const char *cpu_rev;
u32 omap_features;

unsigned int omap_rev(void)
{
	return omap_revision;
}
EXPORT_SYMBOL(omap_rev);

int omap_type(void)
{
	u32 val = 0;

	if (cpu_is_omap24xx()) {
		val = omap_ctrl_readl(OMAP24XX_CONTROL_STATUS);
	} else if (cpu_is_am33xx()) {
		val = omap_ctrl_readl(AM33XX_CONTROL_STATUS);
	} else if (cpu_is_omap34xx()) {
		val = omap_ctrl_readl(OMAP343X_CONTROL_STATUS);
	} else if (cpu_is_omap44xx()) {
		val = omap_ctrl_readl(OMAP4_CTRL_MODULE_CORE_STATUS);
	} else {
		pr_err("Cannot detect omap type!\n");
		goto out;
	}

	val &= OMAP2_DEVICETYPE_MASK;
	val >>= 8;

out:
	return val;
}
EXPORT_SYMBOL(omap_type);


/*----------------------------------------------------------------------------*/

#define OMAP_TAP_IDCODE		0x0204
#define OMAP_TAP_DIE_ID_0	0x0218
#define OMAP_TAP_DIE_ID_1	0x021C
#define OMAP_TAP_DIE_ID_2	0x0220
#define OMAP_TAP_DIE_ID_3	0x0224

#define OMAP_TAP_DIE_ID_44XX_0	0x0200
#define OMAP_TAP_DIE_ID_44XX_1	0x0208
#define OMAP_TAP_DIE_ID_44XX_2	0x020c
#define OMAP_TAP_DIE_ID_44XX_3	0x0210

#define read_tap_reg(reg)	__raw_readl(tap_base  + (reg))

struct omap_id {
	u16	hawkeye;	/* Silicon type (Hawkeye id) */
	u8	dev;		/* Device type from production_id reg */
	u32	type;		/* Combined type id copied to omap_revision */
};

/* Register values to detect the OMAP version */
static struct omap_id omap_ids[] __initdata = {
	{ .hawkeye = 0xb5d9, .dev = 0x0, .type = 0x24200024 },
	{ .hawkeye = 0xb5d9, .dev = 0x1, .type = 0x24201024 },
	{ .hawkeye = 0xb5d9, .dev = 0x2, .type = 0x24202024 },
	{ .hawkeye = 0xb5d9, .dev = 0x4, .type = 0x24220024 },
	{ .hawkeye = 0xb5d9, .dev = 0x8, .type = 0x24230024 },
	{ .hawkeye = 0xb68a, .dev = 0x0, .type = 0x24300024 },
};

static void __iomem *tap_base;
static u16 tap_prod_id;

void omap_get_die_id(struct omap_die_id *odi)
{
	if (cpu_is_omap44xx()) {
		odi->id_0 = read_tap_reg(OMAP_TAP_DIE_ID_44XX_0);
		odi->id_1 = read_tap_reg(OMAP_TAP_DIE_ID_44XX_1);
		odi->id_2 = read_tap_reg(OMAP_TAP_DIE_ID_44XX_2);
		odi->id_3 = read_tap_reg(OMAP_TAP_DIE_ID_44XX_3);

		return;
	}
	odi->id_0 = read_tap_reg(OMAP_TAP_DIE_ID_0);
	odi->id_1 = read_tap_reg(OMAP_TAP_DIE_ID_1);
	odi->id_2 = read_tap_reg(OMAP_TAP_DIE_ID_2);
	odi->id_3 = read_tap_reg(OMAP_TAP_DIE_ID_3);
}

void __init omap2xxx_check_revision(void)
{
	int i, j;
	u32 idcode, prod_id;
	u16 hawkeye;
	u8  dev_type, rev;
	struct omap_die_id odi;

	idcode = read_tap_reg(OMAP_TAP_IDCODE);
	prod_id = read_tap_reg(tap_prod_id);
	hawkeye = (idcode >> 12) & 0xffff;
	rev = (idcode >> 28) & 0x0f;
	dev_type = (prod_id >> 16) & 0x0f;
	omap_get_die_id(&odi);

	pr_debug("OMAP_TAP_IDCODE 0x%08x REV %i HAWKEYE 0x%04x MANF %03x\n",
		 idcode, rev, hawkeye, (idcode >> 1) & 0x7ff);
	pr_debug("OMAP_TAP_DIE_ID_0: 0x%08x\n", odi.id_0);
	pr_debug("OMAP_TAP_DIE_ID_1: 0x%08x DEV_REV: %i\n",
		 odi.id_1, (odi.id_1 >> 28) & 0xf);
	pr_debug("OMAP_TAP_DIE_ID_2: 0x%08x\n", odi.id_2);
	pr_debug("OMAP_TAP_DIE_ID_3: 0x%08x\n", odi.id_3);
	pr_debug("OMAP_TAP_PROD_ID_0: 0x%08x DEV_TYPE: %i\n",
		 prod_id, dev_type);

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

	pr_info("OMAP%04x", omap_rev() >> 16);
	if ((omap_rev() >> 8) & 0x0f)
		pr_info("ES%x", (omap_rev() >> 12) & 0xf);
	pr_info("\n");
}

#define OMAP3_SHOW_FEATURE(feat)		\
	if (omap3_has_ ##feat())		\
		printk(#feat" ");

static void __init omap3_cpuinfo(void)
{
	const char *cpu_name;

	/*
	 * OMAP3430 and OMAP3530 are assumed to be same.
	 *
	 * OMAP3525, OMAP3515 and OMAP3503 can be detected only based
	 * on available features. Upon detection, update the CPU id
	 * and CPU class bits.
	 */
	if (cpu_is_omap3630()) {
		cpu_name = "OMAP3630";
	} else if (soc_is_am35xx()) {
		cpu_name = (omap3_has_sgx()) ? "AM3517" : "AM3505";
	} else if (cpu_is_ti816x()) {
		cpu_name = "TI816X";
	} else if (cpu_is_am335x()) {
		cpu_name =  "AM335X";
	} else if (cpu_is_ti814x()) {
		cpu_name = "TI814X";
	} else if (omap3_has_iva() && omap3_has_sgx()) {
		/* OMAP3430, OMAP3525, OMAP3515, OMAP3503 devices */
		cpu_name = "OMAP3430/3530";
	} else if (omap3_has_iva()) {
		cpu_name = "OMAP3525";
	} else if (omap3_has_sgx()) {
		cpu_name = "OMAP3515";
	} else {
		cpu_name = "OMAP3503";
	}

	/* Print verbose information */
	pr_info("%s ES%s (", cpu_name, cpu_rev);

	OMAP3_SHOW_FEATURE(l2cache);
	OMAP3_SHOW_FEATURE(iva);
	OMAP3_SHOW_FEATURE(sgx);
	OMAP3_SHOW_FEATURE(neon);
	OMAP3_SHOW_FEATURE(isp);
	OMAP3_SHOW_FEATURE(192mhz_clk);

	printk(")\n");
}

#define OMAP3_CHECK_FEATURE(status,feat)				\
	if (((status & OMAP3_ ##feat## _MASK) 				\
		>> OMAP3_ ##feat## _SHIFT) != FEAT_ ##feat## _NONE) { 	\
		omap_features |= OMAP3_HAS_ ##feat;			\
	}

void __init omap3xxx_check_features(void)
{
	u32 status;

	omap_features = 0;

	status = omap_ctrl_readl(OMAP3_CONTROL_OMAP_STATUS);

	OMAP3_CHECK_FEATURE(status, L2CACHE);
	OMAP3_CHECK_FEATURE(status, IVA);
	OMAP3_CHECK_FEATURE(status, SGX);
	OMAP3_CHECK_FEATURE(status, NEON);
	OMAP3_CHECK_FEATURE(status, ISP);
	if (cpu_is_omap3630())
		omap_features |= OMAP3_HAS_192MHZ_CLK;
	if (cpu_is_omap3430() || cpu_is_omap3630())
		omap_features |= OMAP3_HAS_IO_WAKEUP;
	if (cpu_is_omap3630() || omap_rev() == OMAP3430_REV_ES3_1 ||
	    omap_rev() == OMAP3430_REV_ES3_1_2)
		omap_features |= OMAP3_HAS_IO_CHAIN_CTRL;

	omap_features |= OMAP3_HAS_SDRC;

	/*
	 * TODO: Get additional info (where applicable)
	 *       e.g. Size of L2 cache.
	 */

	omap3_cpuinfo();
}

void __init omap4xxx_check_features(void)
{
	u32 si_type;

	if (cpu_is_omap443x())
		omap_features |= OMAP4_HAS_MPU_1GHZ;


	if (cpu_is_omap446x()) {
		si_type =
			read_tap_reg(OMAP4_CTRL_MODULE_CORE_STD_FUSE_PROD_ID_1);
		switch ((si_type & (3 << 16)) >> 16) {
		case 2:
			/* High performance device */
			omap_features |= OMAP4_HAS_MPU_1_5GHZ;
			break;
		case 1:
		default:
			/* Standard device */
			omap_features |= OMAP4_HAS_MPU_1_2GHZ;
			break;
		}
	}
}

void __init ti81xx_check_features(void)
{
	omap_features = OMAP3_HAS_NEON;
	omap3_cpuinfo();
}

void __init omap3xxx_check_revision(void)
{
	u32 cpuid, idcode;
	u16 hawkeye;
	u8 rev;

	/*
	 * We cannot access revision registers on ES1.0.
	 * If the processor type is Cortex-A8 and the revision is 0x0
	 * it means its Cortex r0p0 which is 3430 ES1.0.
	 */
	cpuid = read_cpuid(CPUID_ID);
	if ((((cpuid >> 4) & 0xfff) == 0xc08) && ((cpuid & 0xf) == 0x0)) {
		omap_revision = OMAP3430_REV_ES1_0;
		cpu_rev = "1.0";
		return;
	}

	/*
	 * Detection for 34xx ES2.0 and above can be done with just
	 * hawkeye and rev. See TRM 1.5.2 Device Identification.
	 * Note that rev does not map directly to our defined processor
	 * revision numbers as ES1.0 uses value 0.
	 */
	idcode = read_tap_reg(OMAP_TAP_IDCODE);
	hawkeye = (idcode >> 12) & 0xffff;
	rev = (idcode >> 28) & 0xff;

	switch (hawkeye) {
	case 0xb7ae:
		/* Handle 34xx/35xx devices */
		switch (rev) {
		case 0: /* Take care of early samples */
		case 1:
			omap_revision = OMAP3430_REV_ES2_0;
			cpu_rev = "2.0";
			break;
		case 2:
			omap_revision = OMAP3430_REV_ES2_1;
			cpu_rev = "2.1";
			break;
		case 3:
			omap_revision = OMAP3430_REV_ES3_0;
			cpu_rev = "3.0";
			break;
		case 4:
			omap_revision = OMAP3430_REV_ES3_1;
			cpu_rev = "3.1";
			break;
		case 7:
		/* FALLTHROUGH */
		default:
			/* Use the latest known revision as default */
			omap_revision = OMAP3430_REV_ES3_1_2;
			cpu_rev = "3.1.2";
		}
		break;
	case 0xb868:
		/*
		 * Handle OMAP/AM 3505/3517 devices
		 *
		 * Set the device to be OMAP3517 here. Actual device
		 * is identified later based on the features.
		 */
		switch (rev) {
		case 0:
			omap_revision = AM35XX_REV_ES1_0;
			cpu_rev = "1.0";
			break;
		case 1:
		/* FALLTHROUGH */
		default:
			omap_revision = AM35XX_REV_ES1_1;
			cpu_rev = "1.1";
		}
		break;
	case 0xb891:
		/* Handle 36xx devices */

		switch(rev) {
		case 0: /* Take care of early samples */
			omap_revision = OMAP3630_REV_ES1_0;
			cpu_rev = "1.0";
			break;
		case 1:
			omap_revision = OMAP3630_REV_ES1_1;
			cpu_rev = "1.1";
			break;
		case 2:
		/* FALLTHROUGH */
		default:
			omap_revision = OMAP3630_REV_ES1_2;
			cpu_rev = "1.2";
		}
		break;
	case 0xb81e:
		switch (rev) {
		case 0:
			omap_revision = TI8168_REV_ES1_0;
			cpu_rev = "1.0";
			break;
		case 1:
		/* FALLTHROUGH */
		default:
			omap_revision = TI8168_REV_ES1_1;
			cpu_rev = "1.1";
			break;
		}
		break;
	case 0xb944:
		omap_revision = AM335X_REV_ES1_0;
		cpu_rev = "1.0";
		break;
	case 0xb8f2:
		switch (rev) {
		case 0:
		/* FALLTHROUGH */
		case 1:
			omap_revision = TI8148_REV_ES1_0;
			cpu_rev = "1.0";
			break;
		case 2:
			omap_revision = TI8148_REV_ES2_0;
			cpu_rev = "2.0";
			break;
		case 3:
		/* FALLTHROUGH */
		default:
			omap_revision = TI8148_REV_ES2_1;
			cpu_rev = "2.1";
			break;
		}
		break;
	default:
		/* Unknown default to latest silicon rev as default */
		omap_revision = OMAP3630_REV_ES1_2;
		cpu_rev = "1.2";
		pr_warn("Warning: unknown chip type; assuming OMAP3630ES1.2\n");
	}
}

void __init omap4xxx_check_revision(void)
{
	u32 idcode;
	u16 hawkeye;
	u8 rev;

	/*
	 * The IC rev detection is done with hawkeye and rev.
	 * Note that rev does not map directly to defined processor
	 * revision numbers as ES1.0 uses value 0.
	 */
	idcode = read_tap_reg(OMAP_TAP_IDCODE);
	hawkeye = (idcode >> 12) & 0xffff;
	rev = (idcode >> 28) & 0xf;

	/*
	 * Few initial 4430 ES2.0 samples IDCODE is same as ES1.0
	 * Use ARM register to detect the correct ES version
	 */
	if (!rev && (hawkeye != 0xb94e) && (hawkeye != 0xb975)) {
		idcode = read_cpuid(CPUID_ID);
		rev = (idcode & 0xf) - 1;
	}

	switch (hawkeye) {
	case 0xb852:
		switch (rev) {
		case 0:
			omap_revision = OMAP4430_REV_ES1_0;
			break;
		case 1:
		default:
			omap_revision = OMAP4430_REV_ES2_0;
		}
		break;
	case 0xb95c:
		switch (rev) {
		case 3:
			omap_revision = OMAP4430_REV_ES2_1;
			break;
		case 4:
			omap_revision = OMAP4430_REV_ES2_2;
			break;
		case 6:
		default:
			omap_revision = OMAP4430_REV_ES2_3;
		}
		break;
	case 0xb94e:
		switch (rev) {
		case 0:
		default:
			omap_revision = OMAP4460_REV_ES1_0;
			break;
		}
		break;
	case 0xb975:
		switch (rev) {
		case 0:
		default:
			omap_revision = OMAP4470_REV_ES1_0;
			break;
		}
		break;
	default:
		/* Unknown default to latest silicon rev as default */
		omap_revision = OMAP4430_REV_ES2_3;
	}

	pr_info("OMAP%04x ES%d.%d\n", omap_rev() >> 16,
		((omap_rev() >> 12) & 0xf), ((omap_rev() >> 8) & 0xf));
}

/*
 * Set up things for map_io and processor detection later on. Gets called
 * pretty much first thing from board init. For multi-omap, this gets
 * cpu_is_omapxxxx() working accurately enough for map_io. Then we'll try to
 * detect the exact revision later on in omap2_detect_revision() once map_io
 * is done.
 */
void __init omap2_set_globals_tap(struct omap_globals *omap2_globals)
{
	omap_revision = omap2_globals->class;
	tap_base = omap2_globals->tap;

	if (cpu_is_omap34xx())
		tap_prod_id = 0x0210;
	else
		tap_prod_id = 0x0208;
}
