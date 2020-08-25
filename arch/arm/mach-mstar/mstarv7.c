// SPDX-License-Identifier: GPL-2.0
/*
 * Device Tree support for MStar/Sigmastar Armv7 SoCs
 *
 * Copyright (c) 2020 thingy.jp
 * Author: Daniel Palmer <daniel@thingy.jp>
 */

#include <linux/init.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>

/*
 * In the u-boot code the area these registers are in is
 * called "L3 bridge" and there are register descriptions
 * for something in the same area called "AXI".
 *
 * It's not exactly known what this is but the vendor code
 * for both u-boot and linux share calls to "flush the miu pipe".
 * This seems to be to force pending CPU writes to memory so that
 * the state is right before DMA capable devices try to read
 * descriptors and data the CPU has prepared. Without doing this
 * ethernet doesn't work reliably for example.
 */

#define MSTARV7_L3BRIDGE_FLUSH		0x14
#define MSTARV7_L3BRIDGE_STATUS		0x40
#define MSTARV7_L3BRIDGE_FLUSH_TRIGGER	BIT(0)
#define MSTARV7_L3BRIDGE_STATUS_DONE	BIT(12)

static void __iomem *l3bridge;

static const char * const mstarv7_board_dt_compat[] __initconst = {
	"mstar,infinity",
	"mstar,infinity3",
	"mstar,mercury5",
	NULL,
};

/*
 * This may need locking to deal with situations where an interrupt
 * happens while we are in here and mb() gets called by the interrupt handler.
 *
 * The vendor code did have a spin lock but it doesn't seem to be needed and
 * removing it hasn't caused any side effects so far.
 *
 * [writel|readl]_relaxed have to be used here because otherwise
 * we'd end up right back in here.
 */
static void mstarv7_mb(void)
{
	/* toggle the flush miu pipe fire bit */
	writel_relaxed(0, l3bridge + MSTARV7_L3BRIDGE_FLUSH);
	writel_relaxed(MSTARV7_L3BRIDGE_FLUSH_TRIGGER, l3bridge
			+ MSTARV7_L3BRIDGE_FLUSH);
	while (!(readl_relaxed(l3bridge + MSTARV7_L3BRIDGE_STATUS)
			& MSTARV7_L3BRIDGE_STATUS_DONE)) {
		/* wait for flush to complete */
	}
}

static void __init mstarv7_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "mstar,l3bridge");
	l3bridge = of_iomap(np, 0);
	if (l3bridge)
		soc_mb = mstarv7_mb;
	else
		pr_warn("Failed to install memory barrier, DMA will be broken!\n");
}

DT_MACHINE_START(MSTARV7_DT, "MStar/Sigmastar Armv7 (Device Tree)")
	.dt_compat	= mstarv7_board_dt_compat,
	.init_machine	= mstarv7_init,
MACHINE_END
