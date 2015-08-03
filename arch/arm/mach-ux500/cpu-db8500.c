/*
 * Copyright (C) 2008-2009 ST-Ericsson SA
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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regulator/machine.h>
#include <linux/random.h>

#include <asm/pmu.h>
#include <asm/mach/map.h>

#include "setup.h"

#include "board-mop500-regulators.h"
#include "board-mop500.h"
#include "db8500-regs.h"
#include "id.h"

static struct ab8500_platform_data ab8500_platdata = {
	.regulator	= &ab8500_regulator_plat_data,
};

static struct prcmu_pdata db8500_prcmu_pdata = {
	.ab_platdata	= &ab8500_platdata,
	.version_offset	= DB8500_PRCMU_FW_VERSION_OFFSET,
	.legacy_offset	= DB8500_PRCMU_LEGACY_OFFSET,
};

static void __init u8500_map_io(void)
{
	debug_ll_io_init();
	ux500_setup_id();
}

/*
 * The PMU IRQ lines of two cores are wired together into a single interrupt.
 * Bounce the interrupt to the other core if it's not ours.
 */
static irqreturn_t db8500_pmu_handler(int irq, void *dev, irq_handler_t handler)
{
	irqreturn_t ret = handler(irq, dev);
	int other = !smp_processor_id();

	if (ret == IRQ_NONE && cpu_online(other))
		irq_set_affinity(irq, cpumask_of(other));

	/*
	 * We should be able to get away with the amount of IRQ_NONEs we give,
	 * while still having the spurious IRQ detection code kick in if the
	 * interrupt really starts hitting spuriously.
	 */
	return ret;
}

static struct arm_pmu_platdata db8500_pmu_platdata = {
	.handle_irq		= db8500_pmu_handler,
};

static const char *db8500_read_soc_id(void)
{
	void __iomem *uid;

	uid = ioremap(U8500_BB_UID_BASE, 0x20);
	if (!uid)
		return NULL;
	/* Throw these device-specific numbers into the entropy pool */
	add_device_randomness(uid, 0x14);
	return kasprintf(GFP_KERNEL, "%08x%08x%08x%08x%08x",
			 readl((u32 *)uid+0),
			 readl((u32 *)uid+1), readl((u32 *)uid+2),
			 readl((u32 *)uid+3), readl((u32 *)uid+4));
	iounmap(uid);
}

static struct device * __init db8500_soc_device_init(void)
{
	const char *soc_id = db8500_read_soc_id();

	return ux500_soc_device_init(soc_id);
}

static struct of_dev_auxdata u8500_auxdata_lookup[] __initdata = {
	/* Requires call-back bindings. */
	OF_DEV_AUXDATA("arm,cortex-a9-pmu", 0, "arm-pmu", &db8500_pmu_platdata),
	/* Requires DMA bindings. */
	OF_DEV_AUXDATA("stericsson,ux500-msp-i2s", 0x80123000,
		       "ux500-msp-i2s.0", &msp0_platform_data),
	OF_DEV_AUXDATA("stericsson,ux500-msp-i2s", 0x80124000,
		       "ux500-msp-i2s.1", &msp1_platform_data),
	OF_DEV_AUXDATA("stericsson,ux500-msp-i2s", 0x80117000,
		       "ux500-msp-i2s.2", &msp2_platform_data),
	OF_DEV_AUXDATA("stericsson,ux500-msp-i2s", 0x80125000,
		       "ux500-msp-i2s.3", &msp3_platform_data),
	/* Requires non-DT:able platform data. */
	OF_DEV_AUXDATA("stericsson,db8500-prcmu", 0x80157000, "db8500-prcmu",
			&db8500_prcmu_pdata),
	OF_DEV_AUXDATA("stericsson,ux500-cryp", 0xa03cb000, "cryp1", NULL),
	OF_DEV_AUXDATA("stericsson,ux500-hash", 0xa03c2000, "hash1", NULL),
	OF_DEV_AUXDATA("stericsson,snd-soc-mop500", 0, "snd-soc-mop500.0",
			NULL),
	{},
};

static struct of_dev_auxdata u8540_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("stericsson,db8500-prcmu", 0x80157000, "db8500-prcmu",
			&db8500_prcmu_pdata),
	{},
};

static const struct of_device_id u8500_local_bus_nodes[] = {
	/* only create devices below soc node */
	{ .compatible = "stericsson,db8500", },
	{ .compatible = "stericsson,db8500-prcmu", },
	{ .compatible = "simple-bus"},
	{ },
};

static void __init u8500_init_machine(void)
{
	struct device *parent = db8500_soc_device_init();

	/* automatically probe child nodes of dbx5x0 devices */
	if (of_machine_is_compatible("st-ericsson,u8540"))
		of_platform_populate(NULL, u8500_local_bus_nodes,
				     u8540_auxdata_lookup, parent);
	else
		of_platform_populate(NULL, u8500_local_bus_nodes,
				     u8500_auxdata_lookup, parent);
}

static const char * stericsson_dt_platform_compat[] = {
	"st-ericsson,u8500",
	"st-ericsson,u8540",
	"st-ericsson,u9500",
	"st-ericsson,u9540",
	NULL,
};

DT_MACHINE_START(U8500_DT, "ST-Ericsson Ux5x0 platform (Device Tree Support)")
	.map_io		= u8500_map_io,
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.init_time	= ux500_timer_init,
	.init_machine	= u8500_init_machine,
	.init_late	= NULL,
	.dt_compat      = stericsson_dt_platform_compat,
	.restart        = ux500_restart,
MACHINE_END
