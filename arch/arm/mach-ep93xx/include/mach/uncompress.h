/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arch/arm/mach-ep93xx/include/mach/uncompress.h
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 */

#include <mach/ep93xx-regs.h>
#include <asm/mach-types.h>

static unsigned char __raw_readb(unsigned int ptr)
{
	return *((volatile unsigned char *)ptr);
}

static unsigned int __raw_readl(unsigned int ptr)
{
	return *((volatile unsigned int *)ptr);
}

static void __raw_writeb(unsigned char value, unsigned int ptr)
{
	*((volatile unsigned char *)ptr) = value;
}

static void __raw_writel(unsigned int value, unsigned int ptr)
{
	*((volatile unsigned int *)ptr) = value;
}

#define PHYS_UART_DATA		(CONFIG_DEBUG_UART_PHYS + 0x00)
#define PHYS_UART_FLAG		(CONFIG_DEBUG_UART_PHYS + 0x18)
#define UART_FLAG_TXFF		0x20

static inline void putc(int c)
{
	int i;

	for (i = 0; i < 10000; i++) {
		/* Transmit fifo not full? */
		if (!(__raw_readb(PHYS_UART_FLAG) & UART_FLAG_TXFF))
			break;
	}

	__raw_writeb(c, PHYS_UART_DATA);
}

static inline void flush(void)
{
}


/*
 * Some bootloaders don't turn off DMA from the ethernet MAC before
 * jumping to linux, which means that we might end up with bits of RX
 * status and packet data scribbled over the uncompressed kernel image.
 * Work around this by resetting the ethernet MAC before we uncompress.
 */
#define PHYS_ETH_SELF_CTL		0x80010020
#define ETH_SELF_CTL_RESET		0x00000001

static void ethernet_reset(void)
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

static void __maybe_unused ts72xx_watchdog_disable(void)
{
	__raw_writeb(TS72XX_WDT_FEED_VAL, TS72XX_WDT_FEED_PHYS_BASE);
	__raw_writeb(0, TS72XX_WDT_CONTROL_PHYS_BASE);
}

static void arch_decomp_setup(void)
{
	if (machine_is_ts72xx())
		ts72xx_watchdog_disable();
	ethernet_reset();
}
