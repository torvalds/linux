/*
 *  linux/arch/arm/mach-pxa/ssp.c
 *
 *  based on linux/arch/arm/mach-sa1100/ssp.c by Russell King
 *
 *  Copyright (C) 2003 Russell King.
 *  Copyright (C) 2003 Wolfson Microelectronics PLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  PXA2xx SSP driver.  This provides the generic core for simple
 *  IO-based SSP applications and allows easy port setup for DMA access.
 *
 *  Author: Liam Girdwood <liam.girdwood@wolfsonmicro.com>
 *
 *  Revision history:
 *   22nd Aug 2003 Initial version.
 *   20th Dec 2004 Added ssp_config for changing port config without
 *                 closing the port.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/arch/ssp.h>
#include <asm/arch/pxa-regs.h>

#define PXA_SSP_PORTS 	3

static DECLARE_MUTEX(sem);
static int use_count[PXA_SSP_PORTS] = {0, 0, 0};

static irqreturn_t ssp_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct ssp_dev *dev = (struct ssp_dev*) dev_id;
	unsigned int status = SSSR_P(dev->port);

	SSSR_P(dev->port) = status; /* clear status bits */

	if (status & SSSR_ROR)
		printk(KERN_WARNING "SSP(%d): receiver overrun\n", dev->port);

	if (status & SSSR_TUR)
		printk(KERN_WARNING "SSP(%d): transmitter underrun\n", dev->port);

	if (status & SSSR_BCE)
		printk(KERN_WARNING "SSP(%d): bit count error\n", dev->port);

	return IRQ_HANDLED;
}

/**
 * ssp_write_word - write a word to the SSP port
 * @data: 32-bit, MSB justified data to write.
 *
 * Wait for a free entry in the SSP transmit FIFO, and write a data
 * word to the SSP port.
 *
 * The caller is expected to perform the necessary locking.
 *
 * Returns:
 *   %-ETIMEDOUT	timeout occurred (for future)
 *   0			success
 */
int ssp_write_word(struct ssp_dev *dev, u32 data)
{
	while (!(SSSR_P(dev->port) & SSSR_TNF))
		cpu_relax();

	SSDR_P(dev->port) = data;

	return 0;
}

/**
 * ssp_read_word - read a word from the SSP port
 *
 * Wait for a data word in the SSP receive FIFO, and return the
 * received data.  Data is LSB justified.
 *
 * Note: Currently, if data is not expected to be received, this
 * function will wait for ever.
 *
 * The caller is expected to perform the necessary locking.
 *
 * Returns:
 *   %-ETIMEDOUT	timeout occurred (for future)
 *   32-bit data	success
 */
int ssp_read_word(struct ssp_dev *dev)
{
	while (!(SSSR_P(dev->port) & SSSR_RNE))
		cpu_relax();

	return SSDR_P(dev->port);
}

/**
 * ssp_flush - flush the transmit and receive FIFOs
 *
 * Wait for the SSP to idle, and ensure that the receive FIFO
 * is empty.
 *
 * The caller is expected to perform the necessary locking.
 */
void ssp_flush(struct ssp_dev *dev)
{
	do {
		while (SSSR_P(dev->port) & SSSR_RNE) {
			(void) SSDR_P(dev->port);
		}
	} while (SSSR_P(dev->port) & SSSR_BSY);
}

/**
 * ssp_enable - enable the SSP port
 *
 * Turn on the SSP port.
 */
void ssp_enable(struct ssp_dev *dev)
{
	SSCR0_P(dev->port) |= SSCR0_SSE;
}

/**
 * ssp_disable - shut down the SSP port
 *
 * Turn off the SSP port, optionally powering it down.
 */
void ssp_disable(struct ssp_dev *dev)
{
	SSCR0_P(dev->port) &= ~SSCR0_SSE;
}

/**
 * ssp_save_state - save the SSP configuration
 * @ssp: pointer to structure to save SSP configuration
 *
 * Save the configured SSP state for suspend.
 */
void ssp_save_state(struct ssp_dev *dev, struct ssp_state *ssp)
{
	ssp->cr0 = SSCR0_P(dev->port);
	ssp->cr1 = SSCR1_P(dev->port);
	ssp->to = SSTO_P(dev->port);
	ssp->psp = SSPSP_P(dev->port);

	SSCR0_P(dev->port) &= ~SSCR0_SSE;
}

/**
 * ssp_restore_state - restore a previously saved SSP configuration
 * @ssp: pointer to configuration saved by ssp_save_state
 *
 * Restore the SSP configuration saved previously by ssp_save_state.
 */
void ssp_restore_state(struct ssp_dev *dev, struct ssp_state *ssp)
{
	SSSR_P(dev->port) = SSSR_ROR | SSSR_TUR | SSSR_BCE;

	SSCR0_P(dev->port) = ssp->cr0 & ~SSCR0_SSE;
	SSCR1_P(dev->port) = ssp->cr1;
	SSTO_P(dev->port) = ssp->to;
	SSPSP_P(dev->port) = ssp->psp;

	SSCR0_P(dev->port) = ssp->cr0;
}

/**
 * ssp_config - configure SSP port settings
 * @mode: port operating mode
 * @flags: port config flags
 * @psp_flags: port PSP config flags
 * @speed: port speed
 *
 * Port MUST be disabled by ssp_disable before making any config changes.
 */
int ssp_config(struct ssp_dev *dev, u32 mode, u32 flags, u32 psp_flags, u32 speed)
{
	dev->mode = mode;
	dev->flags = flags;
	dev->psp_flags = psp_flags;
	dev->speed = speed;

	/* set up port type, speed, port settings */
	SSCR0_P(dev->port) = (dev->speed | dev->mode);
	SSCR1_P(dev->port) = dev->flags;
	SSPSP_P(dev->port) = dev->psp_flags;

	return 0;
}

/**
 * ssp_init - setup the SSP port
 *
 * initialise and claim resources for the SSP port.
 *
 * Returns:
 *   %-ENODEV	if the SSP port is unavailable
 *   %-EBUSY	if the resources are already in use
 *   %0		on success
 */
int ssp_init(struct ssp_dev *dev, u32 port)
{
	int ret, irq;

	if (port > PXA_SSP_PORTS || port == 0)
		return -ENODEV;

	down(&sem);
	if (use_count[port - 1]) {
		up(&sem);
		return -EBUSY;
	}
	use_count[port - 1]++;

	if (!request_mem_region(__PREG(SSCR0_P(port)), 0x2c, "SSP")) {
		use_count[port - 1]--;
		up(&sem);
		return -EBUSY;
	}

	switch (port) {
		case 1:
			irq = IRQ_SSP;
			break;
#if defined (CONFIG_PXA27x)
		case 2:
			irq = IRQ_SSP2;
			break;
		case 3:
			irq = IRQ_SSP3;
			break;
#else
		case 2:
			irq = IRQ_NSSP;
			break;
		case 3:
			irq = IRQ_ASSP;
			break;
#endif
		default:
			return -ENODEV;
	}

	dev->port = port;

	ret = request_irq(irq, ssp_interrupt, 0, "SSP", dev);
	if (ret)
		goto out_region;

	/* turn on SSP port clock */
	switch (dev->port) {
#if defined (CONFIG_PXA27x)
		case 1:
			pxa_set_cken(CKEN23_SSP1, 1);
			break;
		case 2:
			pxa_set_cken(CKEN3_SSP2, 1);
			break;
		case 3:
			pxa_set_cken(CKEN4_SSP3, 1);
			break;
#else
		case 1:
			pxa_set_cken(CKEN3_SSP, 1);
			break;
		case 2:
			pxa_set_cken(CKEN9_NSSP, 1);
			break;
		case 3:
			pxa_set_cken(CKEN10_ASSP, 1);
			break;
#endif
	}

	up(&sem);
	return 0;

out_region:
	release_mem_region(__PREG(SSCR0_P(port)), 0x2c);
	use_count[port - 1]--;
	up(&sem);
	return ret;
}

/**
 * ssp_exit - undo the effects of ssp_init
 *
 * release and free resources for the SSP port.
 */
void ssp_exit(struct ssp_dev *dev)
{
	int irq;

	down(&sem);
	SSCR0_P(dev->port) &= ~SSCR0_SSE;

	/* find irq, save power and turn off SSP port clock */
	switch (dev->port) {
#if defined (CONFIG_PXA27x)
		case 1:
			irq = IRQ_SSP;
			pxa_set_cken(CKEN23_SSP1, 0);
			break;
		case 2:
			irq = IRQ_SSP2;
			pxa_set_cken(CKEN3_SSP2, 0);
			break;
		case 3:
			irq = IRQ_SSP3;
			pxa_set_cken(CKEN4_SSP3, 0);
			break;
#else
		case 1:
			irq = IRQ_SSP;
			pxa_set_cken(CKEN3_SSP, 0);
			break;
		case 2:
			irq = IRQ_NSSP;
			pxa_set_cken(CKEN9_NSSP, 0);
			break;
		case 3:
			irq = IRQ_ASSP;
			pxa_set_cken(CKEN10_ASSP, 0);
			break;
#endif
		default:
			printk(KERN_WARNING "SSP: tried to close invalid port\n");
			return;
	}

	free_irq(irq, dev);
	release_mem_region(__PREG(SSCR0_P(dev->port)), 0x2c);
	use_count[dev->port - 1]--;
	up(&sem);
}

EXPORT_SYMBOL(ssp_write_word);
EXPORT_SYMBOL(ssp_read_word);
EXPORT_SYMBOL(ssp_flush);
EXPORT_SYMBOL(ssp_enable);
EXPORT_SYMBOL(ssp_disable);
EXPORT_SYMBOL(ssp_save_state);
EXPORT_SYMBOL(ssp_restore_state);
EXPORT_SYMBOL(ssp_init);
EXPORT_SYMBOL(ssp_exit);
EXPORT_SYMBOL(ssp_config);

MODULE_DESCRIPTION("PXA SSP driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");

