/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 */

#include <asm/mach-types.h>

static inline unsigned int __raw_readl(unsigned int ptr)
{
	return *((volatile unsigned int *)ptr);
}

static inline void __raw_writeb(unsigned char value, unsigned int ptr)
{
	*((volatile unsigned char *)ptr) = value;
}

static inline void __raw_writel(unsigned int value, unsigned int ptr)
{
	*((volatile unsigned int *)ptr) = value;
}

/*
 * Some bootloaders don't turn off DMA from the ethernet MAC before
 * jumping to linux, which means that we might end up with bits of RX
 * status and packet data scribbled over the uncompressed kernel image.
 * Work around this by resetting the ethernet MAC before we uncompress.
 */
#define PHYS_ETH_SELF_CTL		0x80010020
#define ETH_SELF_CTL_RESET		0x00000001

static inline void ep93xx_ethernet_reset(void)
{
	unsigned int v;

	/* Reset the ethernet MAC.  */
	v = __raw_readl(PHYS_ETH_SELF_CTL);
	__raw_writel(v | ETH_SELF_CTL_RESET, PHYS_ETH_SELF_CTL);

	/* Wait for reset to finish.  */
	while (__raw_readl(PHYS_ETH_SELF_CTL) & ETH_SELF_CTL_RESET)
		;
}

#define TS72XX_WDT_CONTROL_PHYS_BASE	0x23800000
#define TS72XX_WDT_FEED_PHYS_BASE	0x23c00000
#define TS72XX_WDT_FEED_VAL		0x05

static inline void __maybe_unused ts72xx_watchdog_disable(void)
{
	__raw_writeb(TS72XX_WDT_FEED_VAL, TS72XX_WDT_FEED_PHYS_BASE);
	__raw_writeb(0, TS72XX_WDT_CONTROL_PHYS_BASE);
}

static inline void ep93xx_decomp_setup(void)
{
	if (machine_is_ts72xx())
		ts72xx_watchdog_disable();

	if (machine_is_edb9301() ||
	    machine_is_edb9302() ||
	    machine_is_edb9302a() ||
	    machine_is_edb9302a() ||
	    machine_is_edb9307() ||
	    machine_is_edb9307a() ||
	    machine_is_edb9307a() ||
	    machine_is_edb9312() ||
	    machine_is_edb9315() ||
	    machine_is_edb9315a() ||
	    machine_is_edb9315a() ||
	    machine_is_ts72xx() ||
	    machine_is_bk3() ||
	    machine_is_vision_ep9307())
		ep93xx_ethernet_reset();
}
